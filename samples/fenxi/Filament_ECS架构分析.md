# Filament ECS（Entity Component System）架构详细分析

## 目录
1. [ECS 概述](#ecs-概述)
2. [Entity（实体）](#entity实体)
3. [Component（组件）](#component组件)
4. [Component Manager（组件管理器）](#component-manager组件管理器)
5. [System（系统）](#system系统)
6. [数据布局：SoA（Structure of Arrays）](#数据布局soastructure-of-arrays)
7. [完整示例分析](#完整示例分析)
8. [性能优化策略](#性能优化策略)

---

## ECS 概述

### 什么是 ECS？

ECS（Entity Component System）是一种软件架构模式，将游戏对象分解为三个核心概念：

- **Entity（实体）**：唯一标识符，本身不包含数据
- **Component（组件）**：数据容器，描述实体的某个方面
- **System（系统）**：处理具有特定组件组合的实体

### Filament 中的 ECS

Filament 使用 ECS 架构来管理场景中的所有对象：
- **Entity**：场景中的对象（如三角形、相机、光源）
- **Component**：对象的属性（如 Transform、Renderable、Camera、Light）
- **System**：渲染系统（如 TransformManager、RenderableManager）

### ECS 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                    EntityManager (单例)                       │
│  - 创建/销毁 Entity                                           │
│  - 管理 Entity 生命周期                                        │
│  - 支持最多 2^17 - 1 个 Entity                                │
└──────────────────────┬──────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
┌──────────────┐ ┌──────────────┐ ┌──────────────┐
│ Transform    │ │ Renderable   │ │ Camera       │
│ Manager      │ │ Manager      │ │ Manager      │
│              │ │              │ │              │
│ - Transform  │ │ - Renderable │ │ - Camera     │
│   Component │ │   Component │ │   Component  │
└──────────────┘ └──────────────┘ └──────────────┘
        │              │              │
        └──────────────┼──────────────┘
                       │
        ┌──────────────▼──────────────┐
        │      Scene (场景)             │
        │  - 包含所有 Entity            │
        │  - 管理 Entity 集合           │
        └──────────────────────────────┘
```

---

## Entity（实体）

### Entity 的定义

**位置**: `libs/utils/include/utils/Entity.h`

```cpp
class Entity {
public:
    Entity() noexcept;  // 默认构造，创建空实体
    
    bool operator==(Entity e) const;
    bool operator!=(Entity e) const;
    bool operator<(Entity e) const;  // 可排序
    
    bool isNull() const noexcept;     // 检查是否为空
    uint32_t getId() const noexcept;  // 获取 ID
    
private:
    using Type = uint32_t;
    Type mIdentity = 0;  // 32位标识符
};
```

### Entity ID 的编码

Entity 的 `mIdentity` 是一个 32 位整数，编码了以下信息：

```
┌─────────────────────────────────────────────────────────┐
│  32位 Entity ID                                         │
├──────────────────┬──────────────────────────────────────┤
│ Generation (15位)│ Index (17位)                         │
│                  │                                      │
│ 用于检测 Entity  │ 数组索引，最多支持 2^17 - 1 个 Entity │
│ 是否已被销毁     │                                      │
└──────────────────┴──────────────────────────────────────┘
```

**编码方式**：
```cpp
// EntityManager.h
static constexpr const int GENERATION_SHIFT = 17;
static constexpr const size_t RAW_INDEX_COUNT = (1 << GENERATION_SHIFT);  // 131072

static inline Entity::Type getGeneration(Entity e) noexcept {
    return e.getId() >> GENERATION_SHIFT;  // 高15位：Generation
}

static inline Entity::Type getIndex(Entity e) noexcept {
    return e.getId() & INDEX_MASK;  // 低17位：Index
}
```

**为什么需要 Generation？**
- 当 Entity 被销毁后，其 Index 可以被重用
- Generation 递增，用于检测 Entity 是否有效
- 防止使用已销毁的 Entity

### EntityManager

**位置**: `libs/utils/include/utils/EntityManager.h`

**职责**：
- 创建和销毁 Entity
- 跟踪 Entity 的生命周期
- 检测 Entity 是否有效（isAlive）

**关键方法**：
```cpp
class EntityManager {
public:
    // 单例模式
    static EntityManager& get() noexcept;
    
    // 创建 Entity
    Entity create() {
        Entity e;
        create(1, &e);
        return e;
    }
    
    // 销毁 Entity
    void destroy(Entity e) noexcept {
        destroy(1, &e);
    }
    
    // 检查 Entity 是否有效
    bool isAlive(Entity e) const noexcept {
        return (!e.isNull()) && 
               (getGeneration(e) == mGens[getIndex(e)]);
    }
    
private:
    uint8_t* const mGens;  // Generation 数组
};
```

**使用示例**：
```cpp
// 创建 Entity
Entity entity = EntityManager::get().create();

// 检查有效性
if (EntityManager::get().isAlive(entity)) {
    // Entity 有效
}

// 销毁 Entity
EntityManager::get().destroy(entity);
```

---

## Component（组件）

### Component 的概念

在 Filament 中，Component 不是独立的类，而是通过 **Component Manager** 管理的**数据集合**。

### Filament 中的组件类型

1. **Transform Component**
   - 位置、旋转、缩放
   - 父子关系
   - 世界变换矩阵

2. **Renderable Component**
   - 几何数据（顶点缓冲区、索引缓冲区）
   - 材质实例
   - 边界盒
   - 可见性设置

3. **Camera Component**
   - 视图矩阵
   - 投影矩阵
   - 相机参数

4. **Light Component**
   - 光源类型（方向光、点光源、聚光灯）
   - 颜色、强度
   - 阴影设置

### Component 的数据存储

Component 数据存储在 **SoA（Structure of Arrays）** 布局中，而不是传统的 AoS（Array of Structures）。

**AoS vs SoA**：
```
AoS (Array of Structures) - 传统方式
┌─────────┬─────────┬─────────┐
│ Entity1 │ Entity2 │ Entity3 │
├─────────┼─────────┼─────────┤
│ Transform│Transform│Transform│
│ Renderable│Renderable│Renderable│
│ ...     │ ...     │ ...     │
└─────────┴─────────┴─────────┘

SoA (Structure of Arrays) - Filament 方式
┌─────────────┬─────────────┬─────────────┐
│ Transform[] │ Renderable[]│ Camera[]    │
├─────────────┼─────────────┼─────────────┤
│ Entity1     │ Entity1     │ Entity1     │
│ Entity2     │ Entity2     │ Entity2     │
│ Entity3     │ Entity3     │ Entity3     │
└─────────────┴─────────────┴─────────────┘
```

**SoA 的优势**：
- **缓存友好**：相同类型的数据连续存储，提高缓存命中率
- **SIMD 友好**：可以批量处理相同类型的数据
- **内存对齐**：每个数组可以独立对齐

---

## Component Manager（组件管理器）

### SingleInstanceComponentManager

**位置**: `libs/utils/include/utils/SingleInstanceComponentManager.h`

这是所有 Component Manager 的**基类模板**，提供了组件管理的基础功能。

**模板定义**：
```cpp
template <typename ... Elements>
class SingleInstanceComponentManager {
    using SoA = StructureOfArrays<Elements ..., Entity>;
    SoA mData;  // SoA 数据存储
    
    // Entity -> Instance 映射
    std::unordered_map<Entity, Instance> mInstanceMap;
    
public:
    // 检查 Entity 是否有此组件
    bool hasComponent(Entity e) const noexcept;
    
    // 获取组件实例（Instance）
    Instance getInstance(Entity e) const noexcept;
    
    // 添加组件
    Instance addComponent(Entity e);
    
    // 移除组件
    Instance removeComponent(Entity e);
    
    // 获取组件数据
    template<size_t ElementIndex>
    TypeAt<ElementIndex>& elementAt(Instance index) noexcept;
};
```

### Instance（组件实例）

**Instance** 是组件在 SoA 数组中的**索引**，不是 Entity ID。

```cpp
using Instance = EntityInstanceBase::Type;  // 通常是 uint32_t
```

**Entity vs Instance**：
- **Entity**：全局唯一标识符，用于标识场景中的对象
- **Instance**：组件数组中的索引，用于访问组件数据

**映射关系**：
```
Entity (全局) → Instance (组件内索引)
    ↓
mInstanceMap[entity] = instance
    ↓
mData[instance] = component_data
```

### TransformManager 示例

**位置**: `filament/src/components/TransformManager.h`

```cpp
class FTransformManager : public TransformManager {
private:
    enum {
        LOCAL,          // 局部变换矩阵
        WORLD,          // 世界变换矩阵
        LOCAL_LO,       // 精确局部平移
        WORLD_LO,       // 精确世界平移
        PARENT,         // 父节点 Instance
        FIRST_CHILD,    // 第一个子节点 Instance
        NEXT,           // 下一个兄弟节点 Instance
        PREV,           // 上一个兄弟节点 Instance
    };
    
    // SoA 布局：8个数组
    using Base = utils::SingleInstanceComponentManager<
        math::mat4f,    // LOCAL
        math::mat4f,    // WORLD
        math::float3,   // LOCAL_LO
        math::float3,   // WORLD_LO
        Instance,       // PARENT
        Instance,       // FIRST_CHILD
        Instance,       // NEXT
        Instance        // PREV
    >;
    
    struct Sim : public Base {
        // Proxy 类，提供便捷访问
        struct Proxy {
            Field<LOCAL>        local;
            Field<WORLD>        world;
            Field<PARENT>       parent;
            // ...
        };
        
        Proxy operator[](Instance i) noexcept {
            return { *this, i };
        }
    };
    
    Sim mManager;
};
```

**使用示例**：
```cpp
// 创建 Transform 组件
Entity entity = EntityManager::get().create();
TransformManager& tcm = engine.getTransformManager();
tcm.create(entity);  // 添加 Transform 组件

// 获取组件实例
TransformManager::Instance instance = tcm.getInstance(entity);

// 设置变换
tcm.setTransform(instance, mat4f::translation({1, 2, 3}));

// 获取变换
const mat4f& transform = tcm.getTransform(instance);
const mat4f& worldTransform = tcm.getWorldTransform(instance);
```

### RenderableManager 示例

**位置**: `filament/src/components/RenderableManager.h`

```cpp
class FRenderableManager : public RenderableManager {
private:
    enum {
        RENDERABLE_INSTANCE,    // Renderable 实例
        WORLD_TRANSFORM,        // 世界变换
        VISIBILITY_STATE,       // 可见性状态
        SKINNING_BUFFER,        // 蒙皮缓冲区
        MORPHING_BUFFER,        // 变形缓冲区
        INSTANCES,              // 实例化信息
        WORLD_AABB_CENTER,      // 世界空间 AABB 中心
        VISIBLE_MASK,           // 可见性掩码
        CHANNELS,               // 通道
        LAYERS,                 // 层
        WORLD_AABB_EXTENT,      // 世界空间 AABB 半长
        PRIMITIVES,             // 图元
        SUMMED_PRIMITIVE_COUNT, // 图元计数
        UBO,                    // Uniform 缓冲区
        DESCRIPTOR_SET_HANDLE,  // 描述符集句柄
        // ... 更多字段
    };
    
    using Base = utils::SingleInstanceComponentManager<
        // 所有组件字段类型...
    >;
    
    Base mManager;
};
```

**使用示例**：
```cpp
// 创建 Renderable 组件
Entity entity = EntityManager::get().create();
RenderableManager::Builder(1)
    .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
    .material(0, materialInstance)
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, 
              vertexBuffer, indexBuffer, 0, 3)
    .build(*engine, entity);

// 获取组件实例
RenderableManager::Instance instance = 
    engine.getRenderableManager().getInstance(entity);

// 设置属性
engine.getRenderableManager().setCastShadows(instance, true);
engine.getRenderableManager().setReceiveShadows(instance, true);
```

### CameraManager 示例

**位置**: `filament/src/components/CameraManager.h`

```cpp
class FCameraManager : public CameraManager {
private:
    enum {
        CAMERA,                 // Camera 指针
        OWNS_TRANSFORM_COMPONENT  // 是否拥有 Transform 组件
    };
    
    using Base = utils::SingleInstanceComponentManager<
        FCamera*,  // CAMERA
        bool       // OWNS_TRANSFORM_COMPONENT
    >;
    
    Base mManager;
};
```

**使用示例**：
```cpp
// 创建 Camera 组件
Entity cameraEntity = EntityManager::get().create();
Camera* camera = engine->createCamera(cameraEntity);

// CameraManager 会自动创建 Transform 组件（如果不存在）
// 因为 Camera 需要变换矩阵

// 获取组件实例
CameraManager::Instance instance = 
    engine->getCameraManager().getInstance(cameraEntity);

// 获取 Camera 对象
FCamera* fCamera = engine->getCameraManager().getCamera(instance);
```

---

## System（系统）

在 Filament 中，**System** 不是独立的类，而是通过 **Component Manager** 实现的。

### System 的工作方式

1. **查询阶段**：查找具有特定组件的 Entity
2. **处理阶段**：批量处理这些 Entity 的组件数据
3. **更新阶段**：更新组件数据

### TransformManager 作为 System

TransformManager 不仅管理 Transform 组件，还负责：
- **计算世界变换**：根据父子关系计算世界矩阵
- **维护层次结构**：管理父子关系
- **批量更新**：高效更新所有变换

**关键方法**：
```cpp
class FTransformManager {
    // 计算所有世界变换
    void computeAllWorldTransforms() noexcept;
    
    // 更新单个节点的变换
    void updateNodeTransform(Instance i) noexcept;
    
    // 设置父子关系
    void setParent(Instance i, Instance parent) noexcept;
    
    // 获取世界变换（批量）
    utils::Slice<const math::mat4f> getWorldTransforms() const noexcept {
        return mManager.slice<WORLD>();
    }
};
```

### RenderableManager 作为 System

RenderableManager 负责：
- **准备渲染数据**：为渲染准备组件数据
- **管理可见性**：处理可见性掩码
- **更新 Uniform 缓冲区**：更新渲染所需的 Uniform 数据

---

## 数据布局：SoA（Structure of Arrays）

### SoA 详细说明

**StructureOfArrays** 是 Filament 的核心数据结构，用于高效存储组件数据。

**定义**：
```cpp
template<typename... Types>
class StructureOfArrays {
    // 每个类型一个数组
    std::array<std::vector<Types>...> mArrays;
    
public:
    // 获取第 N 个数组
    template<size_t N>
    std::vector<TypeAt<N>>& data() noexcept;
    
    // 添加一行数据（所有数组同时添加）
    void push_back(const Structure& s);
};
```

### TransformManager 的 SoA 布局

```cpp
// TransformManager 的 SoA 布局
SoA {
    // 数组 0: LOCAL（局部变换矩阵）
    mat4f[] local = [
        mat4f(...),  // Entity 1 的局部变换
        mat4f(...),  // Entity 2 的局部变换
        mat4f(...),  // Entity 3 的局部变换
    ];
    
    // 数组 1: WORLD（世界变换矩阵）
    mat4f[] world = [
        mat4f(...),  // Entity 1 的世界变换
        mat4f(...),  // Entity 2 的世界变换
        mat4f(...),  // Entity 3 的世界变换
    ];
    
    // 数组 2: PARENT（父节点 Instance）
    Instance[] parent = [
        0,    // Entity 1 的父节点（无）
        1,    // Entity 2 的父节点（Entity 1）
        1,    // Entity 3 的父节点（Entity 1）
    ];
    
    // ... 其他数组
}
```

### SoA 的优势

1. **缓存友好**
   ```cpp
   // 批量处理所有世界变换矩阵
   const mat4f* worldTransforms = tcm.getWorldTransforms().data();
   for (size_t i = 0; i < count; ++i) {
       // 连续内存访问，缓存命中率高
       processTransform(worldTransforms[i]);
   }
   ```

2. **SIMD 优化**
   ```cpp
   // 可以批量处理相同类型的数据
   // 例如：批量计算所有世界变换
   ```

3. **内存效率**
   - 只存储实际需要的数据
   - 避免结构体填充浪费

---

## 完整示例分析

### 示例：创建并渲染一个三角形

**代码位置**: `samples/animation.cpp`

#### 步骤 1：创建 Entity

```cpp
// 创建可渲染实体的 Entity
app.renderable = EntityManager::get().create();
// renderable 现在是一个有效的 Entity ID（例如：0x00010001）

// 创建相机的 Entity
app.camera = utils::EntityManager::get().create();
// camera 现在是一个有效的 Entity ID（例如：0x00010002）
```

**Entity ID 解析**：
```
renderable = 0x00010001
├─ Generation: 0x0001 (高15位)
└─ Index: 0x0001 (低17位)

camera = 0x00010002
├─ Generation: 0x0001 (高15位)
└─ Index: 0x0002 (低17位)
```

#### 步骤 2：创建 Renderable 组件

```cpp
RenderableManager::Builder(1)
    .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
    .material(0, app.mat->getDefaultInstance())
    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, 
              app.vb, app.ib, 0, 3)
    .culling(false)
    .receiveShadows(false)
    .castShadows(false)
    .build(*engine, app.renderable);
```

**内部流程**：
```cpp
// RenderableManager::create() 内部
void FRenderableManager::create(const Builder& builder, Entity entity) {
    // 1. 添加组件到 Entity
    Instance instance = mManager.addComponent(entity);
    
    // 2. 初始化组件数据（存储在 SoA 中）
    mManager[instance].boundingBox = builder.boundingBox;
    mManager[instance].materialInstance = builder.materialInstance;
    mManager[instance].vertexBuffer = builder.vertexBuffer;
    mManager[instance].indexBuffer = builder.indexBuffer;
    // ... 设置其他字段
    
    // 3. 创建底层渲染图元
    // ...
}
```

**SoA 数据布局**：
```
RenderableManager::mManager.mData {
    boundingBox[] = [
        Box{{-1,-1,-1}, {1,1,1}},  // instance 1
    ];
    
    materialInstance[] = [
        MaterialInstance*,  // instance 1
    ];
    
    vertexBuffer[] = [
        VertexBuffer*,  // instance 1
    ];
    
    // ...
}
```

#### 步骤 3：创建 Transform 组件

```cpp
// 在动画循环中
auto& tcm = engine->getTransformManager();
tcm.setTransform(
    tcm.getInstance(app.renderable),
    filament::math::mat4f::rotation(now, filament::math::float3{ 0, 0, 1 })
);
```

**内部流程**：
```cpp
// TransformManager::setTransform() 内部
void FTransformManager::setTransform(Instance ci, const mat4f& model) noexcept {
    // 1. 设置局部变换
    mManager[ci].local = model;
    
    // 2. 更新世界变换（考虑父子关系）
    updateNodeTransform(ci);
}

void FTransformManager::updateNodeTransform(Instance i) noexcept {
    Instance parent = mManager[i].parent;
    if (parent) {
        // 有父节点：世界变换 = 父世界变换 × 局部变换
        computeWorldTransform(
            mManager[i].world,
            mManager[i].worldTranslationLo,
            mManager[parent].world,  // 父世界变换
            mManager[i].local,       // 局部变换
            // ...
        );
    } else {
        // 无父节点：世界变换 = 局部变换
        mManager[i].world = mManager[i].local;
    }
    
    // 3. 递归更新所有子节点
    Instance child = mManager[i].firstChild;
    while (child) {
        updateNodeTransform(child);
        child = mManager[child].next;
    }
}
```

**SoA 数据布局**：
```
TransformManager::mManager.mData {
    local[] = [
        mat4f::rotation(...),  // instance 1
    ];
    
    world[] = [
        mat4f(...),  // instance 1（计算后的世界变换）
    ];
    
    parent[] = [
        0,  // instance 1（无父节点）
    ];
    
    // ...
}
```

#### 步骤 4：创建 Camera 组件

```cpp
app.camera = utils::EntityManager::get().create();
app.cam = engine->createCamera(app.camera);
```

**内部流程**：
```cpp
// CameraManager::create() 内部
FCamera* FCameraManager::create(FEngine& engine, Entity entity) {
    // 1. 添加组件
    Instance instance = mManager.addComponent(entity);
    
    // 2. 创建 Camera 对象
    FCamera* camera = engine.getHeapAllocator().make<FCamera>(engine, entity);
    mManager.elementAt<CAMERA>(instance) = camera;
    
    // 3. 确保有 Transform 组件（Camera 需要变换）
    FTransformManager& tcm = engine.getTransformManager();
    if (!tcm.hasComponent(entity)) {
        tcm.create(entity);
        mManager.elementAt<OWNS_TRANSFORM_COMPONENT>(instance) = true;
    }
    
    return camera;
}
```

**SoA 数据布局**：
```
CameraManager::mManager.mData {
    camera[] = [
        FCamera*,  // instance 1
    ];
    
    ownsTransformComponent[] = [
        true,  // instance 1（CameraManager 创建了 Transform）
    ];
}
```

#### 步骤 5：添加到场景

```cpp
scene->addEntity(app.renderable);
```

**内部流程**：
```cpp
// Scene::addEntity() 内部
void FScene::addEntity(Entity entity) {
    // 检查 Entity 是否有 Renderable 组件
    if (mRenderableManager.hasComponent(entity)) {
        // 添加到场景的 Entity 列表
        mEntities.insert(entity);
    }
}
```

#### 步骤 6：渲染时查询

```cpp
// 在渲染时，Scene 会查询所有有 Renderable 组件的 Entity
void FScene::prepare(...) {
    // 1. 获取所有有 Renderable 组件的 Entity
    Entity const* entities = mRenderableManager.getEntities();
    size_t count = mRenderableManager.getComponentCount();
    
    // 2. 批量获取组件数据（SoA 布局）
    auto& worldTransforms = mTransformManager.getWorldTransforms();
    auto& aabbs = mRenderableManager.getAABBs();
    
    // 3. 批量处理（高效）
    for (size_t i = 0; i < count; ++i) {
        Entity entity = entities[i];
        RenderableManager::Instance ri = mRenderableManager.getInstance(entity);
        TransformManager::Instance ti = mTransformManager.getInstance(entity);
        
        // 使用组件数据
        const mat4f& world = worldTransforms[ti];
        const Box& aabb = aabbs[ri];
        // ... 渲染
    }
}
```

---

## 性能优化策略

### 1. SoA 布局优化

**优势**：
- **缓存友好**：相同类型的数据连续存储
- **批量处理**：可以批量处理相同类型的数据
- **SIMD 优化**：支持 SIMD 指令

**示例**：
```cpp
// 批量获取所有世界变换矩阵
const mat4f* worldTransforms = 
    transformManager.getWorldTransforms().data();
size_t count = transformManager.getComponentCount();

// 批量处理（缓存友好）
for (size_t i = 0; i < count; ++i) {
    processTransform(worldTransforms[i]);
}
```

### 2. Instance 缓存

**避免重复查询**：
```cpp
// ❌ 低效：每次都查询
for (Entity e : entities) {
    TransformManager::Instance i = tcm.getInstance(e);
    mat4f transform = tcm.getTransform(i);
}

// ✅ 高效：缓存 Instance
std::vector<TransformManager::Instance> instances;
instances.reserve(entities.size());
for (Entity e : entities) {
    instances.push_back(tcm.getInstance(e));
}
for (auto i : instances) {
    mat4f transform = tcm.getTransform(i);
}
```

### 3. 批量操作

**批量创建/销毁**：
```cpp
// 批量创建 Entity
Entity entities[100];
EntityManager::get().create(100, entities);

// 批量添加组件
for (size_t i = 0; i < 100; ++i) {
    transformManager.create(entities[i]);
}
```

### 4. 组件查询优化

**使用 hasComponent() 检查**：
```cpp
// 检查 Entity 是否有组件（O(1) 查找）
if (renderableManager.hasComponent(entity)) {
    // 处理 Renderable
}
```

### 5. 内存对齐

**SoA 布局支持独立对齐**：
```cpp
// 每个数组可以独立对齐，提高访问效率
alignas(16) mat4f worldTransforms[];
```

---

## Entity 生命周期管理

### Entity 创建

```cpp
// 1. 创建 Entity
Entity entity = EntityManager::get().create();
// entity.getId() 返回唯一的 ID

// 2. 添加组件
TransformManager& tcm = engine.getTransformManager();
tcm.create(entity);  // 添加 Transform 组件

// 3. 添加到场景
scene->addEntity(entity);
```

### Entity 销毁

```cpp
// 1. 从场景移除
scene->removeEntity(entity);

// 2. 销毁组件（自动或手动）
// TransformManager 会在 gc() 时自动清理
// 或者手动销毁：
tcm.destroy(entity);

// 3. 销毁 Entity
EntityManager::get().destroy(entity);
// 此时 Entity 的 Generation 会递增
```

### 垃圾回收（GC）

**Component Manager 的 GC**：
```cpp
// 定期调用 GC，清理已销毁 Entity 的组件
void FTransformManager::gc(EntityManager& em) noexcept {
    mManager.gc(em, [this](Entity e) {
        destroy(e);  // 清理组件
    });
}
```

**GC 流程**：
1. 遍历所有组件实例
2. 检查对应的 Entity 是否有效（isAlive）
3. 如果无效，移除组件数据
4. 压缩数组（移除空隙）

---

## 总结

### Filament ECS 架构特点

1. **轻量级 Entity**
   - Entity 只是一个 ID（32位整数）
   - 不包含任何数据
   - 支持最多 131,071 个 Entity

2. **SoA 数据布局**
   - 组件数据存储在 Structure of Arrays 中
   - 提高缓存命中率
   - 支持批量处理

3. **Component Manager**
   - 每个组件类型有自己的 Manager
   - Manager 管理组件的创建、查询、更新
   - 使用 Instance 作为组件数组索引

4. **高效查询**
   - Entity → Instance 映射（O(1)）
   - 批量获取组件数据
   - 支持迭代所有组件

5. **自动生命周期管理**
   - GC 机制自动清理无效组件
   - Entity 销毁时自动清理组件

### 关键设计模式

- **单例模式**：EntityManager 是单例
- **模板特化**：Component Manager 使用模板
- **SoA 布局**：提高性能
- **Instance 模式**：组件数组索引

### 性能优势

- **缓存友好**：SoA 布局提高缓存命中率
- **批量处理**：可以批量处理相同类型的数据
- **内存效率**：只存储需要的数据
- **查询高效**：O(1) 的 Entity → Instance 映射

---

## 参考资料

- Entity: `libs/utils/include/utils/Entity.h`
- EntityManager: `libs/utils/include/utils/EntityManager.h`
- SingleInstanceComponentManager: `libs/utils/include/utils/SingleInstanceComponentManager.h`
- TransformManager: `filament/src/components/TransformManager.h`
- RenderableManager: `filament/src/components/RenderableManager.h`
- CameraManager: `filament/src/components/CameraManager.h`
- 示例代码: `samples/animation.cpp`

