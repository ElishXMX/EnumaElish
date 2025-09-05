# EnumaElish 引擎
![EnumaElish Logo](logo.png)

## 项目概述
EnumaElish 是一个基于Vulkan的现代3D游戏引擎，采用C++20开发，支持跨平台渲染和实时图形处理。

## 主要特性
- 基于Vulkan的高性能渲染管线
- 现代PBR材质系统
- 实时全局光照
- 物理基础渲染(PBR)
- 可扩展的ECS架构
- 多线程资源加载

## 系统要求
- Windows 10/11 64位
- Vulkan 1.2+ 兼容显卡
- CMake 3.19+
- Visual Studio 2019/2022 (MSVC)

## 构建指南
```bash
# 克隆仓库
git clone https://github.com/yourusername/EnumaElish.git
cd EnumaElish

# 生成构建系统
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019" -A x64

# 编译项目
cmake --build . --config Release
```

## 运行示例
```bash
cd build/Release
EnumaElish.exe
```

## 项目结构
```
engine/
├── runtime/        # 运行时核心
│   ├── core/       # 基础系统
│   ├── render/     # 渲染系统
│   └── content/    # 资源
├── 3rdparty/      # 第三方库
└── shaders/        # 着色器代码
```

## 依赖项
- Vulkan SDK
- GLFW (窗口管理)
- GLM (数学库)
- spdlog (日志系统)
