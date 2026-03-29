# 物理碰撞模拟系统开发需求

## 项目背景

EnumaElish Engine 是一个基于 Vulkan 的自研渲染引擎，已集成 JoltPhysics 物理引擎基础框架。当前状态：
- JoltPhysics 已初始化，支持静态碰撞体创建、射线检测、调试绘制
- 所有碰撞体当前都是 **Static（静态）**，不会参与物理模拟
- 碰撞体仅用于鼠标拾选（Picking），未用于物理碰撞/运动模拟
- 引擎主循环已有 `logicalTick()` 和 `rendererTick()` 分离

## 核心目标

让引擎从"碰撞体仅用于拾取"进化到"真实的物理碰撞模拟"——动态刚体受力运动、碰撞响应、物理驱动的渲染同步。

---

## 阶段一：动态刚体基础

### 任务 1.1：扩展 PhysicsScene 支持多种形状
**当前**：只有 Box 和 ConvexHull
**需要新增**：
- `createSphereBody()` — 球体碰撞体
- `createCapsuleBody()` — 胶囊体碰撞体
- `createCylinderBody()` — 圆柱体碰撞体
- `createMeshBody()` — 三角网格静态碰撞体（凹面体）

在 `RigidBodyCreateInfo` 中增加 `m_shape_type` 枚举字段（Box/Sphere/Capsule/Cylinder/Mesh/ConvexHull）和必要的参数（如球体半径、胶囊体半径+高度等）。

### 任务 1.2：动态刚体创建与激活
**当前**：`createBoxBody()` 创建的都是 Static 体，`DontActivate`
**需要修改**：
- 当 `m_is_dynamic = true` 时，使用 `EActivation::Activate` 激活刚体
- 支持设置刚体物理属性：质量(mass)、摩擦系数(friction)、弹性系数(restitution)
- 在 `RigidBodyCreateInfo` 中增加 `m_mass`、`m_friction`、`m_restitution` 字段

### 任务 1.3：力与冲量接口
在 `PhysicsScene` 中新增：
```cpp
// 施加力（持续力，需每帧调用）
void applyForce(JPH::BodyID body_id, const glm::vec3& force);

// 施加冲量（瞬时力，一次调用）
void applyImpulse(JPH::BodyID body_id, const glm::vec3& impulse);

// 施加点力（在特定世界坐标位置施力）
void applyForceAtPoint(JPH::BodyID body_id, const glm::vec3& force, const glm::vec3& point);

// 施加点冲量
void applyImpulseAtPoint(JPH::BodyID body_id, const glm::vec3& impulse, const glm::vec3& point);

// 设置线速度
void setLinearVelocity(JPH::BodyID body_id, const glm::vec3& velocity);

// 设置角速度
void setAngularVelocity(JPH::BodyID body_id, const glm::vec3& velocity);

// 获取线速度
glm::vec3 getLinearVelocity(JPH::BodyID body_id) const;

// 获取角速度
glm::vec3 getAngularVelocity(JPH::BodyID body_id) const;
```

---

## 阶段二：物理-渲染双向同步

### 任务 2.1：物理位置驱动渲染（Physics → Rendering）
**这是最关键的改变**。

当前 `global_context.cpp` 的 `syncCollidersWithRenderObjects()` 是 **渲染驱动物理**（把渲染对象的位置写进物理体）。

需要反过来实现：**物理驱动渲染**：
- 在 `logicalTick()` 中先调用 `physics_scene->update(delta_time)` 进行物理步进
- 然后遍历所有标记为 `dynamic` 的刚体
- 读取它们的物理位置/旋转，写回到对应渲染对象的 `animationParams`
- 静态物体保持"渲染驱动物理"的同步方式不变

**实现思路**：
```cpp
void RuntimeGlobalContext::syncPhysicsToRenderObjects()
{
    // 仅同步动态刚体：物理 → 渲染
    // 静态物体保持现有的渲染 → 物理同步
}
```

在 `logicalTick()` 中的执行顺序变为：
```
1. input_system->tick()
2. physics_scene->update(delta_time)     // 物理步进
3. syncPhysicsToRenderObjects()           // 动态物体：物理→渲染
4. syncCollidersWithRenderObjects()       // 静态物体：渲染→物理
```

### 任务 2.2：渲染对象标记系统
需要一种方式标记哪些渲染对象是动态物理对象：
- 方案：在渲染对象的 `animationParams` 中增加 `bool isPhysicsDynamic = false` 和 `JPH::BodyID physicsBodyId`
- 或者：在场景 JSON 中配置哪些物体是动态的

---

## 阶段三：碰撞事件系统

### 任务 3.1：碰撞监听器实现
当前 `MyContactListener` 的回调是空的。需要实现：
```cpp
class PhysicsContactListener : public JPH::ContactListener
{
public:
    // 碰撞开始
    virtual void OnContactAdded(...) override;
    // 碰撞持续
    virtual void OnContactPersisted(...) override;
    // 碰撞分离
    virtual void OnContactRemoved(...) override;
};
```

### 任务 3.2：碰撞事件回调机制
定义碰撞事件结构：
```cpp
struct CollisionEvent
{
    std::string body_a_name;   // 碰撞体A名称
    std::string body_b_name;   // 碰撞体B名称
    glm::vec3  contact_point;  // 碰撞点
    glm::vec3  contact_normal; // 碰撞法线
    float      penetration;    // 穿透深度
};

using CollisionCallback = std::function<void(const CollisionEvent&)>;
```

在 `PhysicsScene` 中提供注册碰撞回调的接口。

---

## 阶段四：物理场景演示

### 任务 4.1：创建物理演示场景
创建一个演示场景文件或代码，展示物理模拟效果：
- 地面（静态平面或大 Box）
- 若干球体/立方体从高处自由落体
- 碰撞弹跳效果
- 可以通过 ImGui UI 控制：
  - 暂停/恢复物理模拟
  - 调整重力方向和大小
  - 点击按钮生成新的动态物体
  - 施加爆炸力（给所有动态物体施加一个向外的冲量）

### 任务 4.2：ImGui 物理调试面板
在编辑器 UI 中增加物理调试面板：
- 显示活跃刚体数量
- 显示当前帧物理步进耗时
- 选中物体时显示其速度、角速度、质量
- 重力调节滑条

---

## 技术约束

1. **遵循现有架构**：使用 RHI 抽象层，不直接调用 Vulkan API
2. **C++20 标准**，MSVC 编译器
3. **参考项目**：Piccolo 引擎架构
4. **物理步进频率**：建议固定时间步长 1/60s，与渲染帧率解耦
5. **线程安全**：Jolt 的 BodyInterface 已经内部加锁，但注意跨线程访问顺序
6. **性能**：物体数量 < 1000 时保持 60fps

## 文件修改范围预估

| 文件 | 改动 |
|------|------|
| `physics_scene.h` | 新增形状类型枚举、力/冲量接口声明、碰撞事件结构 |
| `physics_scene.cpp` | 实现多种形状创建、力/冲量、碰撞监听器 |
| `global_context.h/cpp` | 物理更新接入、双向同步逻辑 |
| `engine.cpp` | logicalTick 中加入物理步进 |
| `render_resource.h/cpp` | 渲染对象增加物理标记字段 |
| UI 相关文件 | 物理调试面板 |
| 场景文件 | 物理演示场景配置 |

## 完成标准

- [ ] 能在场景中创建动态刚体并看到物理运动
- [ ] 物体自由落体并在地面弹跳
- [ ] 物理位置正确同步到渲染
- [ ] 碰撞事件能被捕获和日志输出
- [ ] ImGui 面板可控制物理参数
- [ ] 编译通过无报错，运行稳定
