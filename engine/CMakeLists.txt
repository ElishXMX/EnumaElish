# 设置引擎根目录，基于 EnumaElish_ROOT_DIR 变量拼接 engine 路径
set(ENGINE_ROOT_DIR "${EnumaElish_ROOT_DIR}/engine")
# 设置第三方库目录，基于引擎根目录拼接 3rdparty 路径
set(THIRD_PARTY_DIR "${ENGINE_ROOT_DIR}/3rdparty")
# 设置引擎资源目录
# set(ENGINE_ASSET_DIR "/asset")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 检查编译器是否为 MSVC
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # 为 MSVC 编译器添加多处理器编译选项
    add_compile_options("/MP")
    # 设置 Visual Studio 启动项目为 EnumaElish
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT EnumaElish)
endif()

# 设置 Vulkan 头文件包含目录
set(vulkan_include ${THIRD_PARTY_DIR}/VulkanSDK/include)

# 检查当前平台是否为 Windows
if(WIN32)
    # 设置 Windows 平台下的 Vulkan 库路径
    set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Win32/vulkan-1.lib)
    # 设置 Windows 平台下的 glslangValidator 可执行文件路径
    set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32/glslangValidator.exe)
    # 添加编译定义，指定 Vulkan 层的路径
    add_compile_definitions("Elish_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Win32")
# 检查当前平台是否为 Unix 系列
elseif(UNIX)
    # 检查当前平台是否为 macOS
    if(APPLE)
        # 设置 macOS 平台下的 Vulkan 库路径
        set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/MacOS/libvulkan.1.dylib)
        # 设置 macOS 平台下的 glslangValidator 可执行文件路径
        set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/glslangValidator)
        # 添加编译定义，指定 Vulkan 层的路径
        add_compile_definitions("Elish_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS")
        # 添加编译定义，指定 Vulkan ICD 文件路径
        add_compile_definitions("Elish_VK_ICD_FILENAMES=${THIRD_PARTY_DIR}/VulkanSDK/bin/MacOS/MoltenVK_icd.json")
    else()
        # 设置 Linux 平台下的 Vulkan 库路径
        set(vulkan_lib ${THIRD_PARTY_DIR}/VulkanSDK/lib/Linux/libvulkan.so.1)
        # 设置 Linux 平台下的 glslangValidator 可执行文件路径
        set(glslangValidator_executable ${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux/glslangValidator)
        # 添加编译定义，指定 Vulkan 层的路径
        add_compile_definitions("Elish_VK_LAYER_PATH=${THIRD_PARTY_DIR}/VulkanSDK/bin/Linux")
    endif()
else()
    # 如果是未知平台，输出致命错误信息并终止配置过程
    message(FATAL_ERROR "Unknown Platform")
endif()

# 设置着色器编译目标名称
set(SHADER_COMPILE_TARGET EnumaElishShaderCompile)
# 添加 runtime/shader 子目录到编译过程
add_subdirectory(runtime/shader)

# 添加 3rdparty 子目录到编译过程
add_subdirectory(3rdparty)

# 引擎运行时 主要编写部分
# 添加 runtime 子目录到编译过程
add_subdirectory(runtime)

# 引擎入口点
add_executable(EnumaElish main.cpp)
target_link_libraries(EnumaElish PRIVATE EnumaElishRuntime)
