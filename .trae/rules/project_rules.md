# Project Norms for EnumaElish Engine
This document outlines the key project norms and configurations derived from the CMake build system and project structure.

## 1. Build System
- **Tool**: CMake (minimum version 3.19)
- **Build Type**: Out-of-source builds are enforced; in-source builds are not allowed.

## 2. C++ Standard
- **Version**: C++20 is required for the main project (`EnumaElish`).
- **Note**: The `EnumaElishRuntime` library explicitly sets C++17, but the overall project adheres to C++20.

## 3. Compiler
- **Supported**: MSVC (Microsoft Visual C++ Compiler).
- **Options**: Multi-processor compilation (`/MP`) is enabled for MSVC.

## 4. Dependency Management
Third-party libraries are managed via CMake and located in the `engine/3rdparty` directory. Key dependencies include:
- Vulkan SDK
- spdlog (logging library)
- tinyobjloader (model loading)
- stb (single-file public domain libraries)
- glfw (windowing and input)
- glm (OpenGL Mathematics)
- imgui (immediate mode GUI)
- json11 (JSON parsing)

## 5. Directory Structure
- `EnumaElish_ROOT_DIR`: Root directory of the project (`d:\Coding\codespaceFortrae\vulkanRenderer`).
- `ENGINE_ROOT_DIR`: Root directory for the engine source code (`$(EnumaElish_ROOT_DIR)/engine`).
- `THIRD_PARTY_DIR`: Directory for third-party libraries (`$(ENGINE_ROOT_DIR)/3rdparty`).
- `engine/runtime/core`: Contains core engine functionalities, including base classes and logging.
- `engine/runtime/global`: Manages global contexts and singletons.
- `engine/runtime/render`: Houses the rendering system, including RHI interfaces, render passes, pipelines, and resources.
- `engine/runtime/content`: Stores assets like models, shaders, and textures.
- `engine/runtime/shader`: Contains shader source code and generated C++ headers.

## 6. Build Artifacts
- **Shared Libraries**: Building of shared libraries is explicitly turned OFF (`BUILD_SHARED_LIBS OFF`). The project builds static libraries and executables.
- **Installation Prefix**: Executables and other build artifacts are installed into the `bin` directory within the project root (`$(EnumaElish_ROOT_DIR)/bin`).
- **Ninja Build Specifics**:
  - **Build Directory**: When using Ninja, the primary build output is located in `$(EnumaElish_ROOT_DIR)/build-ninja`.
  - **Executable Path**: The main executable, `EnumaElish.exe`, can be found at `$(EnumaElish_ROOT_DIR)/build-ninja/engine/EnumaElish.exe`.
  - **Build Command**: To compile the project using Ninja, navigate to the build directory and run: `cmake --build .` or `ninja`.

## 7. Shader Compilation
- **Target**: A dedicated CMake target `EnumaElishShaderCompile` is used for compiling shaders.
- **Generated Code**: Shaders are compiled into C++ code and located in `engine/runtime/shader/generated/cpp`.

## 8. Graphics Interface Usage
- **RHI Layer**: When interacting with graphics APIs (like Vulkan), always use the interfaces encapsulated by the Render Hardware Interface (RHI) layer. Avoid direct API calls to maintain portability and abstraction.