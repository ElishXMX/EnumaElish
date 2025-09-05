# PackageProject.cmake will be used to make our target installable
# 设置目标名称为 EnumaElishRuntime
set(TARGET_NAME EnumaElishRuntime)
# 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
# 要求必须使用指定的 C++ 标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ENGINE_RUNTIME_DIR "${EnumaElish_ROOT_DIR}/engine/runtime")
# 输出状态信息，表明正在为指定路径下的示例生成项目文件
message(STATUS "Generating project file for example in ${ENGINE_RUNTIME_DIR}")

# 配置JSON11库路径
set(JSON_INCLUDE ${THIRD_PARTY_DIR}/json11)
# 添加JSON11库（单个cpp实现）
add_library(json11 ${JSON_INCLUDE}/json11.cpp)
set_target_properties(json11 PROPERTIES FOLDER "ThirdParty/json11")

# 自动收集头文件和源文件（CMake不推荐做法）
file(GLOB_RECURSE HEADER_FILES "*.h")    # 递归收集所有.h文件
file(GLOB_RECURSE SOURCE_FILES "*.cpp") # 递归收集所有.cpp文件
# 组织VS解决方案中的文件结构
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HEADER_FILES} ${SOURCE_FILES})
# 创建运行时库
add_library(${TARGET_NAME} ${HEADER_FILES} ${SOURCE_FILES})

# 设置编译属性
set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Engine")

# MSVC编译器特殊设置
target_compile_options(${TARGET_NAME} PUBLIC 
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->"  # 严格标准模式
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/WX->")          # 警告视为错误

# 依赖库链接配置
target_link_libraries(${TARGET_NAME} PUBLIC spdlog::spdlog)  # 日志库
target_link_libraries(${TARGET_NAME} PRIVATE tinyobjloader stb) # 模型加载库
target_link_libraries(${TARGET_NAME} PUBLIC glfw)           # 窗口管理
target_link_libraries(${TARGET_NAME} PUBLIC glm)           # 数学库
target_link_libraries(${TARGET_NAME} PUBLIC imgui)          # GUI库
# target_link_libraries(${TARGET_NAME} PUBLIC Jolt)           # 物理引擎
# target_link_libraries(${TARGET_NAME} PUBLIC lua_static sol2) # Lua脚本
target_link_libraries(${TARGET_NAME} PUBLIC ${vulkan_lib})  # Vulkan库
target_link_libraries(${TARGET_NAME} PRIVATE $<BUILD_INTERFACE:json11>) # JSON库

target_include_directories(${TARGET_NAME} 
  PUBLIC $<BUILD_INTERFACE:${THIRD_PARTY_DIR}/vulkanmemoryallocator/include>
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/shader/generated/cpp>)

add_dependencies(${TARGET_NAME} ${SHADER_COMPILE_TARGET}) # 着色器编译依赖

target_include_directories(${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${vulkan_include}>)

