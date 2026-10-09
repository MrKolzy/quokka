# Building Quokka

## Requirements

- Windows x64 and a GPU with Vulkan 1.3 or newer
- Visual Studio Community 2026 Insiders
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.4.363.0 with the **GLM headers** component
- [GLFW](https://www.glfw.org/download.html) 3.5.1 (64-bit Windows binaries)
- [tinygltf](https://github.com/syoyo/tinygltf) 2.9.7
  (only the `tiny_gltf.h` and `json.hpp` headers)
- [KTX-Software](https://github.com/KhronosGroup/KTX-Software/releases) 4.4.2
  with the **Development** component
- [Dear ImGui](https://github.com/ocornut/imgui/releases) 1.92.9b
  (the source code, compiled through `src/dear_imgui.cpp`)

## Setup

Create a Visual Studio **Empty Project** named `Quokka` with the solution in the same directory
and run in its folder:

	git init
	git remote add origin https://github.com/MrKolzy/quokka.git
	git fetch origin
	git checkout main

Add the files in the `src`, `include/quokka` and `shaders` folders with **Add > Existing Item**.

## Properties

In the project **Properties**, set **Configuration** to `All Configurations`
and **Platform** to `x64`. When adding values, keep the inherited ones.

`<GLFW>`, `<tinygltf>`, `<KTX>` and `<ImGui>` are your local folders.

| Property | Value |
|---|---|
| C/C++ > General > Additional Include Directories | `$(ProjectDir)include`<br>`$(VULKAN_SDK)\Include`<br>`<GLFW>\include`<br>`<tinygltf>`<br>`<KTX>\include`<br>`<ImGui>`<br>`<ImGui>\backends` |
| C/C++ > General > Warning Level | `Level4 (/W4)` |
| C/C++ > General > Treat Warnings As Errors | `Yes (/WX)` |
| C/C++ > General > Multi-processor Compilation | `Yes (/MP)` |
| C/C++ > Preprocessor > Preprocessor Definitions | `VULKAN_HPP_NO_CONSTRUCTORS`<br>`VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`<br>`GLFW_INCLUDE_VULKAN`<br>`GLM_FORCE_DEPTH_ZERO_TO_ONE`<br>`TINYGLTF_NO_STB_IMAGE`<br>`TINYGLTF_NO_STB_IMAGE_WRITE`<br>`TINYGLTF_NO_EXTERNAL_IMAGE` |
| C/C++ > Language > C++ Language Standard | `Preview - Features from the Latest C++ Working Draft (/std:c++latest)` |
| C/C++ > Precompiled Headers > Precompiled Header | `Use (/Yu)` |
| C/C++ > Precompiled Headers > Precompiled Header File | `pch.hpp` |
| C/C++ > External Includes > Treat Files Included with Angle Brackets as External | `Yes (/external:anglebrackets)` |
| C/C++ > External Includes > External Header Warning Level | `Level3 (/external:W3)` |
| C/C++ > Advanced > Forced Include File | `pch.hpp` |
| C/C++ > Command Line > Additional Options | `/w44365` |
| Linker > General > Additional Library Directories | `<GLFW>\lib-vc2026`<br>`<KTX>\lib`<br>`$(VULKAN_SDK)\Lib` |
| Linker > Input > Additional Dependencies | `glfw3dll.lib`<br>`ktx.lib`<br>`vulkan-1.lib` |

In the properties of `src/pch.cpp`,
set **C/C++ > Precompiled Headers > Precompiled Header** to `Create (/Yc)`
and **C/C++ > General > Multi-processor Compilation** to `No`.

**Build Events > Pre-Build Event > Command Line:**

	"$(VULKAN_SDK)\Bin\slangc.exe" "$(ProjectDir)shaders\shader.slang" -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vert_main -entry frag_main -o "$(ProjectDir)shaders\shader.spv"
	"$(VULKAN_SDK)\Bin\slangc.exe" "$(ProjectDir)shaders\damage_numbers.slang" -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vert_main -entry frag_main -o "$(ProjectDir)shaders\damage_numbers.spv"

**Build Events > Post-Build Event > Command Line:**

	copy /Y "<GLFW>\lib-vc2026\glfw3.dll" "$(OutDir)"
	copy /Y "<KTX>\bin\ktx.dll" "$(OutDir)"

## Running

Start Quokka from Visual Studio so the `shaders`, `models` and `textures` folders are found.

- **Debug** enables the validation layers.
- **Release** is optimized and is the one to measure performance with.
