#include "quokka/model.hpp"

#include "quokka/barrier.hpp"
#include "quokka/device.hpp"
#include "quokka/mesh.hpp"
#include "quokka/one_time_commands.hpp"

#include <ktx.h>

#include <algorithm> // std::max
#include <array> // std::array
#include <bit> // std::bit_width
#include <cstdint> // std::int32_t, std::uint32_t
#include <memory> // std::unique_ptr
#include <span> // std::as_bytes, std::span
#include <stdexcept> // std::runtime_error
#include <utility> // std::move
#include <vector> // std::vector

namespace quokka
{
	namespace
	{
		// Blits each level into the next, so every level must start as a transfer destination,
		// and all of them end up ready for the fragment shader
		void generate_mipmaps(
			const vk::raii::CommandBuffer& command_buffer,
			vk::Image image,
			vk::Extent2D extent,
			std::uint32_t mip_levels) noexcept
		{
			// Signed, like the blit offsets
			auto mip_width { static_cast<std::int32_t>(extent.width) };
			auto mip_height { static_cast<std::int32_t>(extent.height) };

			for (std::uint32_t mip_level { 1 }; mip_level < mip_levels; ++mip_level)
			{
				// The copy or the previous blit wrote the previous level, and this blit reads it
				transition_image_layout(
					command_buffer,
					image,
					mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_level - 1),
					vk::ImageLayout::eTransferDstOptimal,
					vk::ImageLayout::eTransferSrcOptimal,
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferWrite,
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferRead
				);

				// Never below 1, so the longer side of a non-square image keeps shrinking alone
				const std::int32_t next_mip_width { mip_width > 1 ? mip_width / 2 : 1 };
				const std::int32_t next_mip_height { mip_height > 1 ? mip_height / 2 : 1 };

				// Each region goes from the origin to the opposite corner of its whole level
				const std::array source_corners {
					vk::Offset3D {},
					vk::Offset3D { .x { mip_width }, .y { mip_height }, .z { 1 } }
				};
				const std::array destination_corners {
					vk::Offset3D {},
					vk::Offset3D { .x { next_mip_width }, .y { next_mip_height }, .z { 1 } }
				};

				const vk::ImageBlit blit {
					.srcSubresource {
						.aspectMask { vk::ImageAspectFlagBits::eColor },
						.mipLevel { mip_level - 1 },
						.baseArrayLayer { 0 },
						.layerCount { 1 }
					},
					.srcOffsets { source_corners },
					.dstSubresource {
						.aspectMask { vk::ImageAspectFlagBits::eColor },
						.mipLevel { mip_level },
						.baseArrayLayer { 0 },
						.layerCount { 1 }
					},
					.dstOffsets { destination_corners }
				};

				// Linear filtering averages the texels that shrink into each one
				command_buffer.blitImage(
					image,
					vk::ImageLayout::eTransferSrcOptimal,
					image,
					vk::ImageLayout::eTransferDstOptimal,
					blit,
					vk::Filter::eLinear
				);

				// The previous level is final, so it can be sampled once this blit reads it
				// Waiting for a read needs no source access, only the stage
				transition_image_layout(
					command_buffer,
					image,
					mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_level - 1),
					vk::ImageLayout::eTransferSrcOptimal,
					vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::PipelineStageFlagBits2::eTransfer,
					{},
					vk::PipelineStageFlagBits2::eFragmentShader,
					vk::AccessFlagBits2::eShaderRead
				);

				mip_width = next_mip_width;
				mip_height = next_mip_height;
			}

			// The last level is never blitted from, so it goes straight to being sampled
			transition_image_layout(
				command_buffer,
				image,
				mip_level_subresource_range(vk::ImageAspectFlagBits::eColor, mip_levels - 1),
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::PipelineStageFlagBits2::eFragmentShader,
				vk::AccessFlagBits2::eShaderRead
			);
		}

		struct TextureImage
		{
			Image image;

			// Read from the texture file, which can store its texels in any format
			vk::Format format;
		};

		[[nodiscard]] TextureImage create_texture_image(
			const Device& device,
			const vk::raii::CommandPool& command_pool,
			const std::filesystem::path& texture_path)
		{
			// Also inflates the texels if the file is supercompressed with zstd
			ktxTexture2* loaded_texture { nullptr };
			const auto load_result {
				ktxTexture2_CreateFromNamedFile(
					texture_path.string().c_str(),
					KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
					&loaded_texture)
			};
			if (load_result != KTX_SUCCESS)
			{
				throw std::runtime_error("Failed to load texture file: " + texture_path.string());
			}

			// Owned by a unique_ptr, so it's destroyed even if an exception is thrown
			const std::unique_ptr<ktxTexture2, decltype(&ktxTexture2_Destroy)> texture {
				loaded_texture, &ktxTexture2_Destroy
			};

			// Block compressed formats need a GPU feature, and Basis Universal needs transcoding
			if (texture->isCompressed || ktxTexture2_NeedsTranscoding(texture.get()))
			{
				throw std::runtime_error(
					"Compressed textures aren't supported yet: " + texture_path.string());
			}

			const auto format { static_cast<vk::Format>(texture->vkFormat) };

			const vk::Extent2D extent {
				.width { texture->baseWidth },
				.height { texture->baseHeight }
			};

			// Mipmaps in the file are used as they are, and only generated if it has just one level
			const bool should_generate_mipmaps { texture->numLevels == 1 };

			// Halving down to 1x1 gives one level per bit needed to write the longest side
			const std::uint32_t longest_side { std::max(extent.width, extent.height) };
			const auto mip_levels {
				should_generate_mipmaps
					? static_cast<std::uint32_t>(std::bit_width(longest_side))
					: texture->numLevels
			};

			// The format comes from the file, so the GPU must be able to sample it,
			// and to filter it linearly when the blits generate the mipmaps
			const auto format_features {
				device.physical_device().getFormatProperties(format).optimalTilingFeatures
			};
			const bool is_format_supported {
				(format_features & vk::FormatFeatureFlagBits::eSampledImage) &&
				(!should_generate_mipmaps ||
					(format_features & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
			};
			if (!is_format_supported)
			{
				throw std::runtime_error(
					"The GPU can't sample the format of the texture file: " +
					texture_path.string());
			}

			// Every level stored in the file, one after another
			const std::span<const ktx_uint8_t> texels {
				ktxTexture_GetData(ktxTexture(texture.get())),
				ktxTexture_GetDataSize(ktxTexture(texture.get()))
			};

			// Only lives until its contents are copied into the image
			const auto staging_buffer { create_staging_buffer(device, std::as_bytes(texels)) };

			// One copy per level stored in the file, each from that level's offset in the buffer
			std::vector<vk::BufferImageCopy> copy_regions {};
			copy_regions.reserve(texture->numLevels);

			for (std::uint32_t mip_level { 0 }; mip_level < texture->numLevels; ++mip_level)
			{
				ktx_size_t level_offset {};
				ktxTexture_GetImageOffset(
					ktxTexture(texture.get()), mip_level, 0, 0, &level_offset);

				const vk::BufferImageCopy copy_region {
					.bufferOffset { level_offset },
					// 0 means the texels are tightly packed, without padding between rows
					.bufferRowLength { 0 },
					.bufferImageHeight { 0 },
					.imageSubresource {
						.aspectMask { vk::ImageAspectFlagBits::eColor },
						.mipLevel { mip_level },
						.baseArrayLayer { 0 },
						.layerCount { 1 }
					},
					.imageOffset { .x { 0 }, .y { 0 }, .z { 0 } },
					// Each level halves the previous one, but never goes below 1 texel
					.imageExtent {
						.width { std::max(extent.width >> mip_level, 1u) },
						.height { std::max(extent.height >> mip_level, 1u) },
						.depth { 1 }
					}
				};

				copy_regions.push_back(copy_region);
			}

			// Written by the copy and the blits, read by the blits and then sampled by the shaders
			Image image {
				create_image(
					device,
					extent,
					mip_levels,
					vk::SampleCountFlagBits::e1,
					format,
					vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
					vk::ImageUsageFlagBits::eSampled,
					vk::MemoryPropertyFlagBits::eDeviceLocal
				)
			};

			// All in one submission, so the CPU waits for the GPU only once
			submit_one_time_commands(device, command_pool, [&](const auto& command_buffer)
			{
				// Every level at once, nothing to wait for since the old contents are discarded
				transition_image_layout(
					command_buffer,
					*image.handle,
					full_subresource_range(vk::ImageAspectFlagBits::eColor),
					vk::ImageLayout::eUndefined,
					vk::ImageLayout::eTransferDstOptimal,
					vk::PipelineStageFlagBits2::eNone,
					{},
					vk::PipelineStageFlagBits2::eTransfer,
					vk::AccessFlagBits2::eTransferWrite
				);

				command_buffer.copyBufferToImage(
					*staging_buffer.handle,
					*image.handle,
					vk::ImageLayout::eTransferDstOptimal,
					copy_regions
				);

				if (should_generate_mipmaps)
				{
					generate_mipmaps(command_buffer, *image.handle, extent, mip_levels);
				}
				else
				{
					// Every level came from the file, so all can be sampled once the copy is done
					transition_image_layout(
						command_buffer,
						*image.handle,
						full_subresource_range(vk::ImageAspectFlagBits::eColor),
						vk::ImageLayout::eTransferDstOptimal,
						vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::PipelineStageFlagBits2::eTransfer,
						vk::AccessFlagBits2::eTransferWrite,
						vk::PipelineStageFlagBits2::eFragmentShader,
						vk::AccessFlagBits2::eShaderRead
					);
				}
			});

			return { std::move(image), format };
		}
	}

	// ***

	Model load_model(
		const Device& device,
		const vk::raii::CommandPool& command_pool,
		const std::filesystem::path& model_path,
		const std::filesystem::path& texture_path)
	{
		// Only needed until it's uploaded, so the CPU copy is freed when this function returns
		// glTF uses y up like the world, so the vertices need no conversion
		const Mesh mesh { load_gltf(model_path) };

		Model model {};

		model.vertex_buffer = create_device_local_buffer(
			device,
			command_pool,
			std::as_bytes(std::span { mesh.vertices }),
			vk::BufferUsageFlagBits::eVertexBuffer
		);

		model.index_buffer = create_device_local_buffer(
			device,
			command_pool,
			std::as_bytes(std::span { mesh.indices }),
			vk::BufferUsageFlagBits::eIndexBuffer
		);

		model.index_count = static_cast<std::uint32_t>(mesh.indices.size());

		auto [texture_image, texture_format] {
			create_texture_image(device, command_pool, texture_path)
		};

		model.texture_image_view = create_image_view(
			device,
			*texture_image.handle,
			texture_format,
			vk::ImageAspectFlagBits::eColor
		);
		model.texture_image = std::move(texture_image);

		return model;
	}
}
