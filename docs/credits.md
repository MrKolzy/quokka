# Credits

- [Viking Room](https://sketchfab.com/3d-models/viking-room-a49f1b8e4f5c4ecf9e1fe7d81915ad38) by [nigelgoh](https://sketchfab.com/nigelgoh), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), used by the Vulkan Tutorial
  - `models/viking_room.glb` is the tutorial's glTF version of the model
  - `textures/viking_room.ktx2` was generated from the tutorial's `viking_room.png` with [KTX-Software](https://github.com/KhronosGroup/KTX-Software) 4.4.2, in sRGB, with mipmaps and supercompressed with zstd:

        ktx create --format R8G8B8A8_SRGB --assign-tf srgb --assign-primaries bt709 --generate-mipmap --zstd 18 viking_room.png viking_room.ktx2
