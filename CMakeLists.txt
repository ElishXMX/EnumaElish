# 指定CMake的最低版本要求为3.19，若版本不满足则报错终止
cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

# 设置项目名称为EnumaElish，版本号为0.1.0
project(EnumaElish VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 设置C++标准为C++17
set(CMAKE_CXX_STANDARD 17)
# 要求必须使用指定的C++标准
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 设置不构建共享库
set(BUILD_SHARED_LIBS OFF)


# 包含CMakeDependentOption模块，用于创建依赖选项
include(CMakeDependentOption)

# ---- Include guards ----
# 检查源代码目录和构建目录是否相同
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  # 若相同，则输出错误信息并终止CMake配置
  message(
    FATAL_ERROR
      "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

# 设置项目根目录变量为当前CMakeLists.txt所在目录
set(EnumaElish_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# 设置安装目录为项目根目录下的bin文件夹
set(CMAKE_INSTALL_PREFIX "${EnumaElish_ROOT_DIR}/bin")
# 设置二进制文件根目录为安装目录
set(BINARY_ROOT_DIR "${CMAKE_INSTALL_PREFIX}/")

# 添加engine子目录，处理该目录下的CMakeLists.txt文件
add_subdirectory(engine)