# Filament Driver 层详细分析

## 概述

Driver 层是 Filament 渲染引擎的底层抽象接口，负责将高级渲染命令转换为具体图形 API（OpenGL、Vulkan、Metal、WebGPU）的调用。Driver 层采用命令流（Command Stream）架构，实现主线程和渲染线程的解耦。

## 架构层次

```
┌─────────────────────────────────────────┐
│  Engine / Renderer (主线程)              │
│  - 生成渲染命令                          │
│  - 调用 DriverApi                        │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  DriverApi (命令流接口)                  │
│  - 序列化命令到 CircularBuffer          │
│  - 异步执行                              │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  CommandStream (命令流)                  │
│  - 命令序列化/反序列化                    │
│  - 命令执行调度                          │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│  Driver (抽象基类)                       │
│  - 定义所有后端 API                      │
│  - 提供虚函数接口                        │
└──────────────┬──────────────────────────┘
               │
       ┌───────┴───────┬───────────┬──────────┐
       ▼               ▼           ▼          ▼
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│ OpenGL   │  │ Vulkan   │  │ Metal    │  │ WebGPU   │
│ Driver   │  │ Driver   │  │ Driver   │  │ Driver   │
└──────────┘  └──────────┘  └──────────┘  └──────────┘
```

## 核心组件

### 1. Driver 基类

**位置**: `filament/backend/include/private/backend/Driver.h`

Driver 是所有后端实现的抽象基类，定义了所有渲染 API 的接口。

**关键特性**:
- **虚函数接口**: 所有 API 方法都是虚函数，由具体后端实现
- **命令流支持**: 通过 `getDispatcher()` 返回命令分发器
- **调试支持**: `debugCommandBegin/End()` 用于命令调试

**主要方法分类**:

#### 1.1 帧管理
- `beginFrame()` - 开始新帧
- `endFrame()` - 结束帧
- `flush()` - 刷新命令缓冲区
- `finish()` - 等待所有命令完成
- `tick()` - 执行周期性任务

#### 1.2 资源创建
- `createVertexBuffer()` - 创建顶点缓冲区
- `createIndexBuffer()` - 创建索引缓冲区
- `createTexture()` - 创建纹理
- `createRenderTarget()` - 创建渲染目标
- `createProgram()` - 创建着色器程序
- `createDescriptorSet()` - 创建描述符集

#### 1.3 资源更新
- `updateBufferObject()` - 更新缓冲区对象
- `update3DImage()` - 更新纹理数据
- `updateDescriptorSetBuffer()` - 更新描述符集的缓冲区绑定
- `updateDescriptorSetTexture()` - 更新描述符集的纹理绑定

#### 1.4 渲染状态
- `beginRenderPass()` - 开始渲染通道
- `endRenderPass()` - 结束渲染通道
- `bindPipeline()` - 绑定管线状态
- `bindRenderPrimitive()` - 绑定渲染图元
- `bindDescriptorSet()` - 绑定描述符集

#### 1.5 绘制命令
- `draw2()` - 执行绘制（使用当前绑定的状态）
- `draw()` - 执行绘制（包含完整状态）
- `dispatchCompute()` - 调度计算着色器

#### 1.6 同步操作
- `createFence()` - 创建栅栏
- `getFenceStatus()` - 查询栅栏状态
- `fenceWait()` - 等待栅栏

### 2. DriverBase 基类

**位置**: `filament/backend/src/DriverBase.h`

DriverBase 是 Driver 的实现基类，提供所有后端共用的功能。

**关键功能**:

#### 2.1 回调管理
- **回调队列**: 管理异步回调的执行
- **服务线程**: 专用线程处理回调（如果启用多线程）
- **主线程回调**: 单线程模式下在主线程执行回调

```cpp
void scheduleCallback(CallbackHandler* handler, void* user, CallbackHandler::Callback callback);
void purge() noexcept; // 在主线程调用，执行所有待处理的回调
```

#### 2.2 硬件句柄（Hw*）

DriverBase 定义了所有硬件资源的句柄结构：

- `HwVertexBuffer` - 顶点缓冲区句柄
- `HwIndexBuffer` - 索引缓冲区句柄
- `HwTexture` - 纹理句柄
- `HwRenderTarget` - 渲染目标句柄
- `HwProgram` - 着色器程序句柄
- `HwDescriptorSet` - 描述符集句柄
- `HwFence` - 栅栏句柄
- `HwSwapChain` - 交换链句柄

这些句柄是类型安全的包装器，实际存储的是后端特定的资源 ID。

### 3. DriverApi 接口

**位置**: `filament/backend/include/private/backend/DriverApi.h`

DriverApi 是主线程访问 Driver 的接口，所有方法都是异步的（除了同步方法）。

**关键特性**:
- **命令序列化**: 所有调用都被序列化到 CircularBuffer
- **异步执行**: 命令在渲染线程执行
- **内存分配**: 提供 `allocate()` 方法在命令流中分配内存

### 4. CommandStream 命令流

**位置**: `filament/backend/include/private/backend/CommandStream.h`

CommandStream 负责命令的序列化和执行。

#### 4.1 命令结构

```cpp
template<void(Driver::*METHOD)(ARGS...)>
class Command : public CommandBase {
    std::tuple<std::remove_reference_t<ARGS>...> mArgs; // 保存参数
    // ...
};
```

**命令类型**:
- **Driver 方法命令**: 对应 Driver 的每个方法
- **自定义命令**: `CustomCommand` - 执行任意函数
- **空操作命令**: `NoopCommand` - 用于对齐

#### 4.2 命令执行流程

1. **序列化**: 主线程调用 DriverApi 方法，参数被序列化到 CircularBuffer
2. **提交**: `flush()` 或 `submitFrame()` 时，命令缓冲区被提交到渲染线程
3. **执行**: 渲染线程调用 `CommandStream::execute()` 执行所有命令
4. **分发**: 通过 Dispatcher 调用对应的 Driver 方法

#### 4.3 命令对齐

所有命令都按 `FILAMENT_OBJECT_ALIGNMENT` 对齐，确保在不同平台上正确访问。

### 5. CircularBuffer 循环缓冲区

**位置**: `filament/backend/src/CircularBuffer.cpp`

CircularBuffer 是命令流的存储缓冲区，采用循环缓冲区设计。

**特性**:
- **双缓冲**: 写入和读取可以并行进行
- **自动扩容**: 当空间不足时自动分配新缓冲区
- **内存管理**: 自动管理缓冲区的生命周期

### 6. Dispatcher 分发器

**位置**: `filament/backend/include/private/backend/Dispatcher.h`

Dispatcher 负责将命令分发到对应的 Driver 方法。

**工作原理**:
- 每个 Driver 方法都有一个唯一的函数指针
- Dispatcher 维护方法指针到执行函数的映射
- 执行时通过函数指针调用对应的 Driver 方法

## API 分类详解

### 资源管理 API

#### 创建资源

所有创建资源的 API 都返回 `Handle<T>`，这是一个类型安全的资源句柄。

```cpp
// 创建顶点缓冲区
VertexBufferHandle createVertexBuffer(
    uint32_t vertexCount,              // 顶点数量
    VertexBufferInfoHandle vbih        // 顶点缓冲区信息句柄
);

// 创建纹理
TextureHandle createTexture(
    SamplerType target,                // 采样器类型（2D、3D、CUBEMAP 等）
    uint8_t levels,                    // Mipmap 级别数
    TextureFormat format,              // 纹理格式
    uint8_t samples,                   // 多重采样数
    uint32_t width,                    // 宽度
    uint32_t height,                   // 高度
    uint32_t depth,                    // 深度（3D 纹理）
    TextureUsage usage                 // 使用标志
);
```

#### 更新资源

```cpp
// 更新缓冲区对象
void updateBufferObject(
    BufferObjectHandle boh,            // 缓冲区对象句柄
    BufferDescriptor&& data,           // 数据描述符（包含数据和回调）
    uint32_t byteOffset                // 字节偏移
);

// 更新纹理
void update3DImage(
    TextureHandle th,                  // 纹理句柄
    uint32_t level,                    // Mipmap 级别
    uint32_t xoffset, yoffset, zoffset, // 偏移
    uint32_t width, height, depth,     // 尺寸
    PixelBufferDescriptor&& data       // 像素数据描述符
);
```

#### 销毁资源

```cpp
void destroyVertexBuffer(VertexBufferHandle vbh);
void destroyTexture(TextureHandle th);
void destroyRenderTarget(RenderTargetHandle rth);
// ... 等等
```

### 渲染状态 API

#### 渲染通道

```cpp
// 开始渲染通道
void beginRenderPass(
    RenderTargetHandle rth,            // 渲染目标句柄
    const RenderPassParams& params     // 渲染通道参数
    // params 包含：
    // - viewport: 视口
    // - clearColor: 清除颜色
    // - clearFlags: 清除标志（COLOR、DEPTH、STENCIL）
    // - discardStart/End: 丢弃标志
);

// 结束渲染通道
void endRenderPass();
```

#### 管线状态

```cpp
// 绑定管线状态
void bindPipeline(
    PipelineState const& state         // 管线状态
    // state 包含：
    // - program: 着色器程序
    // - rasterState: 光栅化状态（混合、深度测试、面剔除等）
    // - vertexBufferInfo: 顶点缓冲区信息
    // - primitiveType: 图元类型
    // - polygonOffset: 多边形偏移
    // - scissor: 裁剪矩形
    // - stencilState: 模板测试状态
);
```

#### 资源绑定

```cpp
// 绑定渲染图元
void bindRenderPrimitive(
    RenderPrimitiveHandle rph          // 渲染图元句柄
);

// 绑定描述符集
void bindDescriptorSet(
    DescriptorSetHandle dsh,           // 描述符集句柄
    descriptor_set_t set,              // 描述符集索引
    DescriptorSetOffsetArray&& offsets // 偏移数组（用于动态 UBO）
);
```

### 绘制 API

```cpp
// 绘制（使用当前绑定的状态）
void draw2(
    uint32_t indexOffset,              // 索引偏移
    uint32_t indexCount,               // 索引数量
    uint32_t instanceCount             // 实例数量
);

// 绘制（包含完整状态，已废弃）
void draw(
    PipelineState state,               // 管线状态
    RenderPrimitiveHandle rph,         // 渲染图元句柄
    uint32_t indexOffset,              // 索引偏移
    uint32_t indexCount,               // 索引数量
    uint32_t instanceCount             // 实例数量
);
```

### 同步 API

```cpp
// 创建栅栏
FenceHandle createFence();

// 查询栅栏状态
FenceStatus getFenceStatus(FenceHandle fh);
// 返回：
// - FenceStatus::ERROR: 错误
// - FenceStatus::CONDITION_SATISFIED: 条件满足
// - FenceStatus::TIMEOUT_EXPIRED: 超时

// 等待栅栏
FenceStatus fenceWait(FenceHandle fh, uint64_t timeout);
```

## 命令流执行流程

### 1. 命令生成（主线程）

```cpp
// 主线程调用
driverApi.beginRenderPass(rt, params);

// 内部实现（简化）
void DriverApi::beginRenderPass(RenderTargetHandle rth, const RenderPassParams& params) {
    auto* cmd = allocateCommand<COMMAND_TYPE(beginRenderPass)>();
    new(cmd) COMMAND_TYPE(beginRenderPass)(mDispatcher, rth, params);
}
```

### 2. 命令提交

```cpp
// 主线程调用
engine.flush();

// 内部流程：
// 1. 将当前 CircularBuffer 标记为可执行
// 2. 切换到新的 CircularBuffer
// 3. 通知渲染线程有新命令
```

### 3. 命令执行（渲染线程）

```cpp
// 渲染线程执行
void CommandStream::execute(void* buffer) {
    CommandBase* cmd = static_cast<CommandBase*>(buffer);
    while (cmd) {
        cmd = cmd->execute(mDriver); // 执行命令并获取下一个
    }
}

// 命令执行（简化）
CommandBase* Command::execute(Driver& driver) {
    // 调用对应的 Driver 方法
    apply(method, driver, mArgs);
    // 返回下一个命令
    return next;
}
```

## 多线程架构

### 线程模型

- **主线程**: 生成渲染命令，调用 DriverApi
- **渲染线程**: 执行命令，调用 Driver 方法
- **服务线程**（可选）: 处理异步回调

### 同步机制

1. **Fence**: 用于等待 GPU 完成特定操作
2. **CircularBuffer**: 双缓冲设计，读写分离
3. **回调队列**: 异步回调在主线程或服务线程执行

## 后端实现

### OpenGL Driver

**位置**: `filament/backend/src/opengl/`

- 直接调用 OpenGL/ES API
- 状态跟踪和缓存，减少状态切换
- 支持 OpenGL ES 2.0/3.0/3.1

### Vulkan Driver

**位置**: `filament/backend/src/vulkan/`

- 使用 Vulkan API
- 命令缓冲区管理
- 描述符集管理
- 同步对象管理

### Metal Driver

**位置**: `filament/backend/src/metal/`

- 使用 Metal API
- 命令缓冲区编码
- 资源追踪
- 自动释放池管理

### WebGPU Driver

**位置**: `filament/backend/src/webgpu/`

- 使用 WebGPU API
- 适配 Web 环境
- 异步资源管理

## 性能优化

### 1. 命令批处理

- 多个命令打包到一个缓冲区
- 减少线程切换开销
- 提高缓存局部性

### 2. 状态缓存

- Driver 跟踪当前状态
- 只在状态变化时更新
- 减少冗余 API 调用

### 3. 资源池

- 重用资源对象
- 减少分配/释放开销
- 预分配常用资源

### 4. 异步操作

- 命令异步执行
- 主线程不阻塞
- 提高 CPU 利用率

## 调试支持

### 命令调试

- `FILAMENT_DEBUG_COMMANDS`: 启用命令调试
- `debugCommandBegin/End()`: 标记命令边界
- Systrace 集成: 在 systrace 中查看命令执行

### 资源追踪

- Handle 验证: 检查句柄有效性
- 资源泄漏检测: 跟踪资源生命周期
- 状态验证: 检查渲染状态一致性

## 总结

Driver 层是 Filament 的核心抽象层，提供了：

1. **统一接口**: 所有后端实现相同的接口
2. **异步执行**: 命令流架构实现主线程和渲染线程解耦
3. **类型安全**: Handle 系统提供类型安全的资源管理
4. **高性能**: 命令批处理、状态缓存等优化
5. **可扩展**: 易于添加新的后端实现

通过 Driver 层，Filament 可以在不同的图形 API 上运行，同时保持上层代码的一致性。

