# Building Quokka

## Requirements

- Windows x64
- GPU supporting Vulkan 1.3 or newer
- Visual Studio Community 2026 Insiders
- [Vulkan SDK](https://vulkan.lunarg.com/) 1.4.357.0 (sets the `VULKAN_SDK` environment variable)
- [GLFW](https://www.glfw.org/download.html) 3.5.1, 64-bit Windows binaries

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

`<GLFW>` stands for your local GLFW folder.

### C/C++

| Property | Value |
|---|---|
| General > Additional Include Directories | `$(ProjectDir)include`<br>`$(VULKAN_SDK)\Include`<br>`<GLFW>\include` |
| Preprocessor > Preprocessor Definitions | `VULKAN_HPP_NO_CONSTRUCTORS`<br>`VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`<br>`GLFW_INCLUDE_VULKAN` |
| Language > C++ Language Standard | `Preview - Features from the Latest C++ Working Draft (/std:c++latest)` |

- `VULKAN_HPP_NO_CONSTRUCTORS` turns Vulkan-Hpp structs into aggregates, allowing designated initializers.
- `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS` returns `eErrorOutOfDateKHR` instead of throwing an exception, so the engine can recreate the swapchain when it becomes out of date.
- `GLFW_INCLUDE_VULKAN` makes GLFW include Vulkan and declare `glfwCreateWindowSurface`.
- `/std:c++latest` is required for `std::println` (C++23).

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

Run from Visual Studio, whose working directory is the project folder, so `shaders/shader.spv` is found.
