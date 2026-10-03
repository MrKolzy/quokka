# Building Quokka

## Requirements

- Windows x64
- GPU supporting Vulkan 1.3 or newer
- Visual Studio Community 2026 Insiders
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.4.357.0 with the GLM headers component (sets the `VULKAN_SDK` environment variable)
- [GLFW](https://www.glfw.org/download.html) 3.5.1, 64-bit Windows binaries
- [stb_image](https://github.com/nothings/stb) 2.30, the single header `stb_image.h`
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) 2.0, the single header `tiny_obj_loader.h`

## Project Setup

Create a new **Empty Project** in Visual Studio with the following settings:

- **Project name**: `Quokka`
- **Place solution and project in the same directory**: checked

Open a terminal in the project folder and download the repository into it:

    git init
    git remote add origin https://github.com/MrKolzy/quokka.git
    git fetch origin
    git checkout main

Then right-click the project in **Solution Explorer**, select **Add > Existing Item**, and add the files in `src`, `include/quokka` and `shaders`.

## Project Properties

Right-click the **Quokka** project in **Solution Explorer**, select **Properties**, and set **Configuration** to `All Configurations` and **Platform** to `x64`.

When adding values, keep the existing and inherited ones.

`<GLFW>`, `<stb>` and `<tinyobjloader>` stand for your local GLFW, stb_image and tinyobjloader folders.

### C/C++

| Property | Value |
|---|---|
| General > Additional Include Directories | `$(ProjectDir)include`<br>`$(VULKAN_SDK)\Include`<br>`<GLFW>\include`<br>`<stb>`<br>`<tinyobjloader>` |
| General > Multi-processor Compilation | `Yes (/MP)` |
| Preprocessor > Preprocessor Definitions | `VULKAN_HPP_NO_CONSTRUCTORS`<br>`VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`<br>`GLFW_INCLUDE_VULKAN`<br>`GLM_FORCE_DEPTH_ZERO_TO_ONE` |
| Language > C++ Language Standard | `Preview - Features from the Latest C++ Working Draft (/std:c++latest)` |
| Precompiled Headers > Precompiled Header | `Use (/Yu)` |
| Precompiled Headers > Precompiled Header File | `pch.hpp` |
| External Includes > Treat Files Included with Angle Brackets as External | `Yes (/external:anglebrackets)` |
| External Includes > External Header Warning Level | `Level3 (/external:W3)` |
| Advanced > Forced Include File | `pch.hpp` |

- `VULKAN_HPP_NO_CONSTRUCTORS` turns Vulkan-Hpp structs into aggregates, allowing designated initializers.
- `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS` returns `eErrorOutOfDateKHR` instead of throwing an exception, so the engine can recreate the swapchain when it becomes out of date.
- `GLFW_INCLUDE_VULKAN` makes GLFW include Vulkan and declare `glfwCreateWindowSurface`.
- `GLM_FORCE_DEPTH_ZERO_TO_ONE` makes GLM's projection matrices use Vulkan's 0 to 1 depth range instead of OpenGL's -1 to 1.
- `/std:c++latest` is required for `std::println` (C++23).
- `/MP` compiles the source files in parallel.
- The precompiled header compiles Vulkan-Hpp once instead of once per source file. Forced Include adds it at the start of every source file, so they don't need to include it.
- Headers included with angle brackets are treated as external, so the warning level 3 applies to them instead of `/W4`, and third-party code like stb_image doesn't break the build with `/WX`.

Then right-click `src/pch.cpp` in **Solution Explorer**, select **Properties**, and set **C/C++ > Precompiled Headers > Precompiled Header** to `Create (/Yc)`.

### Linker

| Property | Value |
|---|---|
| General > Additional Library Directories | `<GLFW>\lib-vc2026` |
| Input > Additional Dependencies | `glfw3dll.lib` |

### Build Events

**Pre-Build Event > Command Line** compiles the Slang shaders to SPIR-V:

    "$(VULKAN_SDK)\Bin\slangc.exe" "$(ProjectDir)shaders\shader.slang" -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vert_main -entry frag_main -o "$(ProjectDir)shaders\shader.spv"

**Post-Build Event > Command Line** copies the GLFW runtime next to the executable:

    copy /Y "<GLFW>\lib-vc2026\glfw3.dll" "$(OutDir)"

## Build Configurations

Select the configuration from the **Solution Configurations** dropdown in the toolbar.

- **Debug** enables the Vulkan validation layers and prints debug output to the console.
- **Release** defines `NDEBUG` by default, which disables both, and enables compiler optimizations. Use it to measure performance.

## Running

Run from Visual Studio, whose working directory is the project folder, so `shaders/shader.spv`, `models/viking_room.obj` and `textures/viking_room.png` are found.
