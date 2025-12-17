# Filament HwBase 架构设计完整分析

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [HwBase 类型系统](#hwbase-类型系统)
4. [Handle 系统](#handle-系统)
5. [HandleAllocator](#handleallocator)
6. [资源生命周期管理](#资源生命周期管理)
7. [各后端实现](#各后端实现)
8. [使用示例](#使用示例)
9. [总结](#总结)

---

## 概述

### 1.1 设计目标

Filament 的 HwBase 架构旨在提供：

- **类型安全**：编译时类型检查，防止资源类型混淆
- **零开销抽象**：句柄只是 ID，不包含指针，可安全传递
- **统一接口**：跨后端统一的资源管理接口
- **高效分配**：基于池的内存分配，减少碎片
- **调试支持**：资源标签和泄漏检测

### 1.2 核心组件

```
┌─────────────────────────────────────────────────────────────┐
│                    HwBase 架构层次结构                         │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              HwBase (空基类)                          │   │
│  │  所有硬件资源类型的基类，用于类型系统识别                │   │
│  └─────────────────────────────────────────────────────┘   │
│                        ▲                                    │
│                        │ 继承                               │
│        ┌───────────────┼───────────────┐                   │
│        │               │               │                   │
│  ┌─────────┐    ┌──────────┐    ┌──────────┐            │
│  │HwTexture│    │HwBuffer  │    │HwProgram │  ...        │
│  │         │    │Object    │    │          │            │
│  └─────────┘    └──────────┘    └──────────┘            │
│        │               │               │                   │
│        └───────────────┼───────────────┘                   │
│                        │                                    │
│                        ▼                                    │
│  ┌─────────────────────────────────────────────────────┐   │
│  │         Handle<T> (类型安全的句柄包装)                │   │
│  │  - 存储 HandleId (uint32_t)                          │   │
│  │  - 提供类型安全的访问                                │   │
│  └─────────────────────────────────────────────────────┘   │
│                        │                                    │
│                        ▼                                    │
│  ┌─────────────────────────────────────────────────────┐   │
│  │      HandleAllocator (资源分配器)                     │   │
│  │  - 池分配（小对象）                                  │   │
│  │  - 堆分配（大对象）                                  │   │
│  │  - ID 到指针的映射                                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 架构设计

### 2.1 设计理念

#### 2.1.1 句柄模式（Handle Pattern）

Filament 使用句柄模式而非直接指针，原因：

1. **跨线程安全**：句柄只是 ID，可以安全地在命令流中传递
2. **类型安全**：`Handle<HwTexture>` 和 `Handle<HwBuffer>` 是不同的类型
3. **资源管理**：资源由 Driver 统一管理，句柄只是引用
4. **调试支持**：可以通过 ID 追踪资源

#### 2.1.2 空基类优化（Empty Base Optimization）

`HwBase` 是空基类（无成员变量），使用空基类优化：

```cpp
struct HwBase {
    // 空基类，仅用于类型系统识别
};
```

好处：
- 不增加派生类的内存开销
- 提供统一的类型标识
- 支持 SFINAE 和模板特化

### 2.2 数据流

```
应用层
  │
  │ 创建资源请求
  ▼
DriverApi
  │
  │ 分配句柄
  ▼
HandleAllocator
  │
  │ 分配内存 + 构造对象
  ▼
Hw* 对象 (存储在池或堆中)
  │
  │ 返回 Handle<Hw*>
  ▼
应用层
  │
  │ 使用句柄
  ▼
DriverApi
  │
  │ handle_cast<ConcreteType*>(handle)
  ▼
ConcreteType* (实际资源对象)
```

---

## HwBase 类型系统

### 3.1 HwBase 基类

**位置**：`filament/backend/src/DriverBase.h`

```cpp
/**
 * 硬件句柄基类
 * 所有硬件资源句柄都继承自此类，用于类型系统识别
 */
struct HwBase {
    // 空基类，仅用于类型系统识别
};
```

### 3.2 硬件资源类型

#### 3.2.1 缓冲区类型

**HwVertexBuffer** - 顶点缓冲区：
```cpp
struct HwVertexBuffer : public HwBase {
    uint32_t vertexCount{};               // 顶点数量
    uint8_t bufferObjectsVersion{0xff};    // 缓冲区对象版本号
    bool padding[3]{};                     // 填充字节
};
```

**HwIndexBuffer** - 索引缓冲区：
```cpp
struct HwIndexBuffer : public HwBase {
    uint32_t count : 27;      // 索引数量（最多 2^27 个）
    uint32_t elementSize : 5; // 每个索引的大小（2 或 4 字节）
};
```

**HwBufferObject** - 通用缓冲区：
```cpp
struct HwBufferObject : public HwBase {
    uint32_t byteCount{};  // 缓冲区大小（字节）
};
```

**HwMemoryMappedBuffer** - 内存映射缓冲区：
```cpp
struct HwMemoryMappedBuffer : public HwBase {
    // 空结构体，具体实现由后端决定
};
```

#### 3.2.2 纹理类型

**HwTexture** - 纹理：
```cpp
struct HwTexture : public HwBase {
    uint32_t width{};        // 纹理宽度
    uint32_t height{};       // 纹理高度
    uint32_t depth{};         // 纹理深度（3D 或数组层数）
    SamplerType target{};     // 采样器类型（2D、3D、CUBEMAP 等）
    uint8_t levels : 4;      // Mipmap 级别数（最多 15 级）
    uint8_t samples : 4;      // 多重采样数
    TextureFormat format{};   // 纹理格式
    TextureUsage usage{};     // 使用标志
    HwStream* hwStream;       // 关联的外部流（视频纹理）
};
```

#### 3.2.3 着色器类型

**HwProgram** - 着色器程序：
```cpp
struct HwProgram : public HwBase {
    utils::CString name;  // 程序名称（用于调试）
};
```

#### 3.2.4 渲染类型

**HwRenderPrimitive** - 渲染图元：
```cpp
struct HwRenderPrimitive : public HwBase {
    PrimitiveType type = PrimitiveType::TRIANGLES;
};
```

**HwRenderTarget** - 渲染目标：
```cpp
struct HwRenderTarget : public HwBase {
    uint32_t width{};   // 渲染目标宽度
    uint32_t height{};  // 渲染目标高度
};
```

#### 3.2.5 同步类型

**HwFence** - 栅栏：
```cpp
struct HwFence : public HwBase {
    Platform::Fence* fence = nullptr;  // 平台特定的栅栏对象
};
```

**HwSync** - 同步对象：
```cpp
struct HwSync : public HwBase {
    Platform::Sync* sync = nullptr;  // 平台特定的同步对象
};
```

#### 3.2.6 其他类型

**HwSwapChain** - 交换链：
```cpp
struct HwSwapChain : public HwBase {
    Platform::SwapChain* swapChain = nullptr;
};
```

**HwStream** - 流：
```cpp
struct HwStream : public HwBase {
    Platform::Stream* stream = nullptr;
    StreamType streamType = StreamType::ACQUIRED;
    uint32_t width{};
    uint32_t height{};
};
```

**HwTimerQuery** - 计时查询：
```cpp
struct HwTimerQuery : public HwBase {
    // 空结构体，具体实现由后端决定
};
```

**HwDescriptorSetLayout** - 描述符堆布局：
```cpp
struct HwDescriptorSetLayout : public HwBase {
    // 空结构体，具体实现由后端决定
};
```

**HwDescriptorSet** - 描述符堆：
```cpp
struct HwDescriptorSet : public HwBase {
    // 空结构体，具体实现由后端决定
};
```

**HwVertexBufferInfo** - 顶点缓冲区信息：
```cpp
struct HwVertexBufferInfo : public HwBase {
    uint8_t bufferCount{};      // 缓冲区对象数量
    uint8_t attributeCount{};   // 顶点属性数量
    bool padding[2]{};           // 填充字节
};
```

---

## Handle 系统

### 4.1 HandleBase

**位置**：`filament/backend/include/backend/Handle.h`

#### 4.1.1 数据结构

```cpp
class HandleBase {
public:
    using HandleId = uint32_t;
    static constexpr const HandleId nullid = UINT32_MAX;
    
    constexpr HandleBase() noexcept : object(nullid) {}
    explicit operator bool() const noexcept { 
        return object != nullid; 
    }
    void clear() noexcept { object = nullid; }
    HandleId getId() const noexcept { return object; }
    
private:
    HandleId object;  // 句柄 ID
};
```

#### 4.1.2 设计约束

1. **平凡类型**：必须是平凡类型（trivial），无用户定义的拷贝/移动构造函数
2. **命令流安全**：句柄可以在命令流中安全传递（只是 ID）
3. **不拥有资源**：句柄不拥有资源，只是引用

### 4.2 Handle<T> 模板

#### 4.2.1 定义

```cpp
template<typename T>
struct Handle : public HandleBase {
    Handle() noexcept = default;
    Handle(Handle const& rhs) noexcept = default;
    Handle(Handle&& rhs) noexcept = default;
    
    explicit Handle(HandleId id) noexcept : HandleBase(id) {}
    
    // 比较操作符
    bool operator==(const Handle& rhs) const noexcept {
        return getId() == rhs.getId();
    }
    // ... 其他比较操作符
    
    // 类型安全的转换（从基类句柄）
    template<typename B, typename = std::enable_if_t<std::is_base_of_v<T, B>>>
    Handle(Handle<B> const& base) noexcept : HandleBase(base) {}
};
```

#### 4.2.2 类型别名

```cpp
using TextureHandle = Handle<HwTexture>;
using BufferObjectHandle = Handle<HwBufferObject>;
using VertexBufferHandle = Handle<HwVertexBuffer>;
using IndexBufferHandle = Handle<HwIndexBuffer>;
using ProgramHandle = Handle<HwProgram>;
using RenderPrimitiveHandle = Handle<HwRenderPrimitive>;
using RenderTargetHandle = Handle<HwRenderTarget>;
using FenceHandle = Handle<HwFence>;
using SyncHandle = Handle<HwSync>;
using SwapChainHandle = Handle<HwSwapChain>;
using StreamHandle = Handle<HwStream>;
using TimerQueryHandle = Handle<HwTimerQuery>;
using DescriptorSetLayoutHandle = Handle<HwDescriptorSetLayout>;
using DescriptorSetHandle = Handle<HwDescriptorSet>;
using MemoryMappedBufferHandle = Handle<HwMemoryMappedBuffer>;
using VertexBufferInfoHandle = Handle<HwVertexBufferInfo>;
```

### 4.3 handle_cast

**位置**：`filament/backend/include/private/backend/HandleAllocator.h`

将句柄转换为实际对象指针：

```cpp
template<typename D, typename B>
std::enable_if_t<std::is_base_of_v<B, D>, D*> 
handle_cast(Handle<B> const& handle) noexcept {
    HandleBase::HandleId id = handle.getId();
    
    // 检查句柄是否有效
    if (id == HandleBase::nullid) {
        return nullptr;
    }
    
    // 从 HandleAllocator 获取对象指针
    void* ptr = handleToPointer(id);
    
    // 转换为目标类型
    return static_cast<D*>(ptr);
}
```

---

## HandleAllocator

### 5.1 概述

**位置**：`filament/backend/include/private/backend/HandleAllocator.h`

HandleAllocator 负责：
- 分配句柄 ID
- 管理资源内存（池分配 + 堆分配）
- 维护 ID 到指针的映射
- 资源生命周期管理

### 5.2 内存分配策略

#### 5.2.1 三级池分配

不同后端使用不同大小的池：

```cpp
// OpenGL: 32, 96, 184 字节
#define HandleAllocatorGL HandleAllocator<32, 96, 184>

// Vulkan: 64, 160, 312 字节
#define HandleAllocatorVK HandleAllocator<64, 160, 312>

// Metal: 32, 64, 552 字节
#define HandleAllocatorMTL HandleAllocator<32, 64, 552>

// WebGPU: 64, 160, 552 字节
#define HandleAllocatorWGPU HandleAllocator<64, 160, 552>
```

**分配逻辑**：
1. 如果对象大小 <= P0，使用池 0
2. 如果对象大小 <= P1，使用池 1
3. 如果对象大小 <= P2，使用池 2
4. 否则，使用堆分配

#### 5.2.2 数据结构

```cpp
template<size_t P0, size_t P1, size_t P2>
class HandleAllocator : public DebugTag {
private:
    // 三个对象池
    Pool mPool0;  // 小对象池（<= P0 字节）
    Pool mPool1;  // 中对象池（<= P1 字节）
    Pool mPool2;  // 大对象池（<= P2 字节）
    
    // 溢出映射（堆分配的对象）
    std::unordered_map<HandleId, void*> mOverflowMap;
    mutable utils::Mutex mLock;  // 保护溢出映射
    
    // 句柄 ID 分配器
    HandleIdAllocator mHandleIdAllocator;
    
    // 调试标签
    // (继承自 DebugTag)
};
```

#### 5.2.3 句柄 ID 编码

句柄 ID 的位布局：

```
31    30    29-0
┌─────┬─────┬──────────────┐
│ HEAP│ POOL│   INDEX      │
└─────┴─────┴──────────────┘
```

- **HEAP 位**：1 表示堆分配，0 表示池分配
- **POOL 位**：池索引（0、1 或 2）
- **INDEX**：池中的索引或堆分配的序列号

### 5.3 核心方法

#### 5.3.1 分配句柄

```cpp
template<typename D>
Handle<D> allocateHandle() {
    // 1. 分配句柄 ID
    HandleId id = mHandleIdAllocator.allocate();
    
    // 2. 计算对象大小
    size_t size = sizeof(D);
    
    // 3. 选择分配策略
    void* ptr = nullptr;
    if (size <= P0) {
        ptr = mPool0.allocate(id);
        id |= POOL0_FLAG;
    } else if (size <= P1) {
        ptr = mPool1.allocate(id);
        id |= POOL1_FLAG;
    } else if (size <= P2) {
        ptr = mPool2.allocate(id);
        id |= POOL2_FLAG;
    } else {
        // 堆分配
        ptr = malloc(size);
        std::lock_guard lock(mLock);
        mOverflowMap[id] = ptr;
        id |= HEAP_FLAG;
    }
    
    return Handle<D>(id);
}
```

#### 5.3.2 句柄转指针

```cpp
void* handleToPointer(HandleId id) const noexcept {
    // 检查是否为空句柄
    if (id == HandleBase::nullid) {
        return nullptr;
    }
    
    // 检查是否是堆分配
    if (id & HEAP_FLAG) {
        return handleToPointerSlow(id);
    }
    
    // 从池中获取
    HandleId index = id & INDEX_MASK;
    uint8_t pool = (id >> POOL_SHIFT) & POOL_MASK;
    
    switch (pool) {
        case 0: return mPool0.get(index);
        case 1: return mPool1.get(index);
        case 2: return mPool2.get(index);
        default: return nullptr;
    }
}
```

#### 5.3.3 构造对象

```cpp
template<typename D, typename B, typename... ARGS>
std::enable_if_t<std::is_base_of_v<B, D>, D*>
construct(Handle<B> const& handle, ARGS&&... args) noexcept {
    assert_invariant(handle);
    
    // 获取对象指针
    D* addr = handle_cast<D*>(handle);
    assert_invariant(addr);
    
    // 使用 placement new 构造对象
    new(addr) D(std::forward<ARGS>(args)...);
    
    return addr;
}
```

#### 5.3.4 销毁对象

```cpp
template<typename D, typename B>
void destroy(Handle<B> handle) noexcept {
    assert_invariant(handle);
    
    // 获取对象指针
    D* obj = handle_cast<D*>(handle);
    
    // 析构对象
    obj->~D();
    
    // 释放内存
    HandleId id = handle.getId();
    if (id & HEAP_FLAG) {
        // 堆分配，从溢出映射中移除
        std::lock_guard lock(mLock);
        mOverflowMap.erase(id);
        free(obj);
    } else {
        // 池分配，返回到池中
        uint8_t pool = (id >> POOL_SHIFT) & POOL_MASK;
        HandleId index = id & INDEX_MASK;
        switch (pool) {
            case 0: mPool0.free(index); break;
            case 1: mPool1.free(index); break;
            case 2: mPool2.free(index); break;
        }
    }
    
    // 释放句柄 ID
    mHandleIdAllocator.deallocate(id);
}
```

### 5.4 调试支持

#### 5.4.1 资源标签

```cpp
class DebugTag {
    mutable utils::Mutex mDebugTagLock;
    tsl::robin_map<HandleId, utils::ImmutableCString> mDebugTags;
    
public:
    void writePoolHandleTag(HandleId key, 
                           utils::ImmutableCString&& tag) noexcept;
    void writeHeapHandleTag(HandleId key, 
                           utils::ImmutableCString&& tag) noexcept;
    utils::ImmutableCString findHandleTag(HandleId key) const noexcept;
};
```

#### 5.4.2 使用后释放检测

HandleAllocator 可以检测资源在使用后被释放的情况：

```cpp
// 在对象中标记句柄已销毁
void setHandleConsiderDestroyed() noexcept {
    mHandleConsideredDestroyed = true;
}

// 在 handle_cast 中检查
template<typename D, typename B>
D* handle_cast(Handle<B> const& handle) {
    D* ptr = ...;
    FILAMENT_CHECK_PRECONDITION(!ptr->isHandleConsideredDestroyed())
        << "Handle id=" << ptr->id << " is being used after it has been freed";
    return ptr;
}
```

---

## 资源生命周期管理

### 6.1 资源创建流程

```
1. 应用层调用 DriverApi::createTexture(...)
   │
   ▼
2. DriverApi 分配句柄
   Handle<HwTexture> handle = mHandleAllocator.allocate<HwTexture>();
   │
   ▼
3. DriverApi 构造对象
   GLTexture* texture = mHandleAllocator.construct<GLTexture, HwTexture>(
       handle, width, height, format);
   │
   ▼
4. 后端初始化资源
   texture->gl.id = glGenTextures();
   glTexStorage2D(...);
   │
   ▼
5. 返回句柄给应用层
   return handle;
```

### 6.2 资源使用流程

```
1. 应用层传递句柄
   driverApi.updateTexture(handle, data);
   │
   ▼
2. DriverApi 转换句柄为指针
   GLTexture* texture = handle_cast<GLTexture*>(handle);
   │
   ▼
3. 后端使用资源
   glBindTexture(GL_TEXTURE_2D, texture->gl.id);
   glTexSubImage2D(...);
```

### 6.3 资源销毁流程

```
1. 应用层调用 DriverApi::destroyTexture(handle)
   │
   ▼
2. DriverApi 延迟销毁（添加到销毁队列）
   mDestroyQueue.push_back({handle, DestroyType::TEXTURE});
   │
   ▼
3. 在适当时机（如帧结束）执行销毁
   for (auto& item : mDestroyQueue) {
       switch (item.type) {
           case DestroyType::TEXTURE:
               GLTexture* texture = handle_cast<GLTexture*>(item.handle);
               glDeleteTextures(1, &texture->gl.id);
               mHandleAllocator.destroy<GLTexture>(item.handle);
               break;
       }
   }
```

---

## 各后端实现

### 7.1 OpenGL 后端

#### 7.1.1 具体类型

```cpp
// 纹理
struct GLTexture : public HwTexture {
    GLuint id;  // OpenGL 纹理 ID
    // ... 其他 OpenGL 特定数据
};

// 缓冲区
struct GLBufferObject : public HwBufferObject {
    GLuint id;  // OpenGL 缓冲区 ID
    // ... 其他 OpenGL 特定数据
};
```

#### 7.1.2 使用示例

```cpp
Handle<HwTexture> createTexture(...) {
    // 分配句柄
    Handle<HwTexture> handle = mHandleAllocator.allocate<HwTexture>();
    
    // 构造对象
    GLTexture* texture = mHandleAllocator.construct<GLTexture, HwTexture>(
        handle, width, height, format);
    
    // 创建 OpenGL 资源
    glGenTextures(1, &texture->id);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexStorage2D(...);
    
    return handle;
}
```

### 7.2 Vulkan 后端

#### 7.2.1 资源管理器

Vulkan 后端使用 `ResourceManager` 管理资源：

```cpp
class ResourceManager {
    HandleAllocatorVK mHandleAllocator;
    
public:
    template<typename D>
    Handle<D> allocHandle() noexcept {
        return mHandleAllocator.allocate<D>();
    }
    
    template<typename D, typename B, typename... ARGS>
    D* construct(Handle<B> const& handle, ARGS&&... args) noexcept {
        return mHandleAllocator.construct<D, B>(handle, 
                                               std::forward<ARGS>(args)...);
    }
    
    template<typename D, typename B>
    D* handle_cast(Handle<B> const& handle) noexcept {
        return mHandleAllocator.handle_cast<D*, B>(handle);
    }
};
```

#### 7.2.2 引用计数

Vulkan 后端使用 `resource_ptr` 进行引用计数：

```cpp
template<typename D>
struct resource_ptr {
    Resource* mRef;  // 引用计数对象
    
    ~resource_ptr() {
        if (mRef) {
            mRef->dec();  // 减少引用计数
        }
    }
    
    // 当引用计数为 0 时，自动销毁资源
};
```

### 7.3 Metal 后端

#### 7.3.1 具体类型

```cpp
// 纹理
struct MetalTexture : public HwTexture {
    id<MTLTexture> texture;  // Metal 纹理对象
    // ... 其他 Metal 特定数据
};

// 缓冲区
struct MetalBuffer : public HwBufferObject {
    id<MTLBuffer> buffer;  // Metal 缓冲区对象
    // ... 其他 Metal 特定数据
};
```

---

## 使用示例

### 8.1 创建纹理

```cpp
// 应用层
Texture* texture = Texture::Builder()
    .width(1024)
    .height(1024)
    .format(Texture::InternalFormat::RGBA8)
    .build(*engine);

// DriverApi 层
Handle<HwTexture> DriverApi::createTexture(...) {
    // 分配句柄
    Handle<HwTexture> handle = mHandleAllocator.allocate<HwTexture>();
    
    // 构造对象
    GLTexture* hwTexture = mHandleAllocator.construct<GLTexture, HwTexture>(
        handle, target, levels, samples, width, height, depth, format, usage);
    
    // 创建 OpenGL 资源
    glGenTextures(1, &hwTexture->gl.id);
    // ... 初始化纹理
    
    return handle;
}
```

### 8.2 更新纹理

```cpp
// 应用层
texture->setImage(*engine, 0, pixelBuffer);

// DriverApi 层
void DriverApi::updateTexture(Handle<HwTexture> handle, 
                              uint32_t level,
                              PixelBufferDescriptor&& data) {
    // 转换句柄为指针
    GLTexture* texture = handle_cast<GLTexture*>(handle);
    
    // 使用资源
    glBindTexture(GL_TEXTURE_2D, texture->gl.id);
    glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, 
                    width, height, format, type, data.buffer);
}
```

### 8.3 销毁纹理

```cpp
// 应用层
engine->destroy(texture);

// DriverApi 层
void DriverApi::destroyTexture(Handle<HwTexture> handle) {
    // 延迟销毁（添加到队列）
    mDestroyQueue.push_back({handle, DestroyType::TEXTURE});
}

// 在适当时机执行销毁
void DriverApi::gc() {
    for (auto& item : mDestroyQueue) {
        GLTexture* texture = handle_cast<GLTexture*>(item.handle);
        glDeleteTextures(1, &texture->gl.id);
        mHandleAllocator.destroy<GLTexture>(item.handle);
    }
    mDestroyQueue.clear();
}
```

### 8.4 类型安全示例

```cpp
// 编译时类型检查
Handle<HwTexture> textureHandle = ...;
Handle<HwBufferObject> bufferHandle = ...;

// 错误：类型不匹配，编译失败
// updateTexture(bufferHandle, ...);  // 编译错误

// 正确：类型匹配
updateTexture(textureHandle, ...);

// 类型转换（基类到派生类）
Handle<HwBase> baseHandle = textureHandle;  // 可以
// Handle<HwTexture> textureHandle2 = baseHandle;  // 需要显式转换
```

---

## 总结

### 9.1 核心设计原则

1. **类型安全**：通过模板和继承实现编译时类型检查
2. **零开销抽象**：句柄只是 ID，无额外开销
3. **统一接口**：所有后端使用相同的 HwBase 类型系统
4. **高效分配**：基于池的内存分配，减少碎片
5. **资源管理**：由 Driver 统一管理资源生命周期

### 9.2 关键组件

- **HwBase**：所有硬件资源类型的基类
- **Handle<T>**：类型安全的句柄包装
- **HandleAllocator**：资源分配和生命周期管理
- **handle_cast**：句柄到指针的类型安全转换

### 9.3 优势

- **类型安全**：编译时捕获类型错误
- **性能**：池分配减少内存碎片和分配开销
- **调试**：资源标签和泄漏检测
- **跨平台**：统一接口，后端特定实现

### 9.4 最佳实践

1. **使用类型别名**：使用 `TextureHandle` 而非 `Handle<HwTexture>`
2. **检查句柄有效性**：使用 `if (handle)` 检查句柄是否有效
3. **及时释放资源**：调用 `engine->destroy()` 释放资源
4. **避免跨线程传递指针**：只传递句柄，不传递指针

---

**文档版本**：1.0  
**最后更新**：2024  
**作者**：Filament 分析文档

