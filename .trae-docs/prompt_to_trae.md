# TRAE 开发任务：物理碰撞模拟系统

请阅读 `.trae-docs/physics_simulation_requirements.md` 中的详细需求文档。

## 概要

当前引擎已集成 JoltPhysics 基础框架，但所有碰撞体都是 Static（仅用于鼠标拾取）。现在需要实现完整的物理碰撞模拟功能。

## 开发顺序（严格按此顺序执行）

### 第一步：扩展 PhysicsScene（阶段一）
1. 在 `RigidBodyCreateInfo` 中增加 `m_shape_type` 枚举和物理属性字段（mass/friction/restitution/半径/高度等）
2. 新增创建方法：`createSphereBody()`、`createCapsuleBody()`、`createCylinderBody()`、`createMeshBody()`
3. 修改 `createBoxBody()`：当 `m_is_dynamic=true` 时正确激活刚体并设置物理属性
4. 新增力与冲量接口：`applyForce`、`applyImpulse`、`applyForceAtPoint`、`applyImpulseAtPoint`、`setLinearVelocity`、`setAngularVelocity` 及对应的 get 方法

### 第二步：物理-渲染双向同步（阶段二）
1. 修改 `logicalTick()` 执行顺序：先物理步进 → 物理驱动渲染(动态体) → 渲染驱动物理(静态体)
2. 实现 `syncPhysicsToRenderObjects()`：读取动态刚体的物理位置/旋转，写回渲染对象的 animationParams
3. 在渲染对象中增加 `isPhysicsDynamic` 标记

### 第三步：碰撞事件系统（阶段三）
1. 实现碰撞监听器，填充 `OnContactAdded/Removed` 回调
2. 定义 `CollisionEvent` 结构和回调注册接口

### 第四步：演示场景（阶段四）
1. 创建演示场景：地面 + 若干动态球体/立方体自由落体
2. ImGui 物理调试面板：暂停/恢复、重力调节、生成物体、爆炸力

## 重要约束
- 遵循现有项目架构和代码风格
- 使用 RHI 抽象层，不直接调用 Vulkan API
- 参考 Piccolo 项目架构
- 物理步进用固定时间步长 1/60s
- 每完成一个阶段确保编译通过

## 参考文件
- 项目规则：`.trae/rules/project_rules.md`
- 物理场景当前实现：`engine/runtime/physics/physics_scene.h` 和 `physics_scene.cpp`
- 全局上下文：`engine/runtime/global/global_context.h` 和 `global_context.cpp`
- 引擎主循环：`engine/runtime/engine.cpp`
