# Building Quokka

## Integrated Development Environment (IDE)

- Visual Studio Community 2026 Insiders

## Dependencies

- Vulkan SDK (Vulkan Software Development Kit)
- GLFW (OpenGL Framework)
- GLM (OpenGL Mathematics)

## Project Configuration

Right-click Quokka and select Properties

Select:

- Configuration: `All Configurations`
- Platform: `x64`

## Compiler Settings

Navigate to:

C/C++ > General > Additional Include Directories

Add the following paths, replacing the placeholders with your local paths:

- `<...>\Quokka\include`
- `<...>\glfw-3.5.1.bin.WIN64\include`

## Linker Settings

Navigate to:

Linker > General > Additional Library Directories

Add the following path, replacing the placeholder with your local path:

- `<...>\glfw-3.5.1.bin.WIN64\lib-vc2026`

Linker > Input > Additional Dependencies

Add the following library to the additional dependencies:

- `glfw3dll.lib`

## Build Events

Navigate to:

Build Events > Post-Build Event > Command Line

Add the following command, replacing the placeholder with your local path:

- `copy /Y "<...>\glfw-3.5.1.bin.WIN64\lib-vc2026\glfw3.dll" "$(OutDir)"`
