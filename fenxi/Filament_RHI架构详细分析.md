# Filament RHI（Render Hardware Interface）架构详细分析

## 概述

Filament 使用分层架构来抽象不同的图形 API（OpenGL、Vulkan、Metal、WebGPU），提供统一的渲染硬件接口（RHI）。这种设计使得 Filament 可以在不同平台上使用最适合的后端，同时保持上层代码的统一性。

## 架构层次

```
┌─────────────────────────────────────────────────────────┐
│  应用层（Application Layer）                             │
│  - Engine、Renderer、View、Scene                        │
└──────────────┬──────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────┐
│  DriverApi（统一 API 接口）                              │
│  - CommandStream（命令流）                               │
│  - 类型别名：using DriverApi = CommandStream            │
└──────────────┬──────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────┐
│  Driver（抽象基类）                                       │
│  - 定义所有后端必须实现的接口                            │
│  - 提供 Dispatcher（命令分发器）                         │
└──────────────┬──────────────────────────────────────────┘
               │
       ┌───────┴───────┬───────────┬──────────┐
       ▼               ▼           ▼          ▼
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│OpenGL    │  │Vulkan    │  │Metal     │  │WebGPU    │
│Driver    │  │Driver    │  │Driver    │  │Driver    │
└────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘
     │              │              │              │
     ▼              ▼              ▼              ▼
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│OpenGL    │  │Vulkan    │  │Metal     │  │WebGPU    │
│API       │  │API       │  │API       │  │API       │
└──────────┘  └──────────┘  └──────────┘  └──────────┘
     │              │              │              │
     └──────────────┴──────────────┴──────────────┘
                     │
                     ▼
         ┌───────────────────────┐
         │  Platform（平台抽象）   │
         │  - 创建 Driver         │
         │  - 管理上下文/实例      │
         │  - 处理平台特定操作     │
         └───────────────────────┘
```

## 核心组件详解

### 1. Platform（平台抽象层）

**位置**：
- 接口：`filament/backend/include/backend/Platform.h`
- 实现：`filament/backend/include/backend/platforms/*.h`

**职责**：
- **创建 Driver**：根据平台和后端类型创建对应的 Driver 实例
- **管理图形上下文**：创建和管理 OpenGL/Vulkan/Metal 上下文
- **平台特定操作**：处理窗口、事件、交换链等平台相关操作
- **缓存管理**：提供着色器缓存接口（Blob Cache）
- **调试支持**：提供调试统计信息接口

**关键方法**：

```cpp
/**
 * 创建 Driver 实例
 * 
 * 这是 Platform 的核心方法，负责：
 * 1. 初始化底层图形 API（创建上下文/实例）
 * 2. 创建对应的 Driver 实例
 * 3. 返回 Driver 指针（调用者负责销毁）
 * 
 * @param sharedContext 共享上下文（可选，用于多上下文场景）
 * @param driverConfig Driver 配置参数
 * @return Driver 指针，失败返回 nullptr
 */
virtual Driver* createDriver(void* sharedContext, const DriverConfig& driverConfig) = 0;
```

**Platform 实现**：

| 平台 | OpenGL | Vulkan | Metal | WebGPU |
|------|--------|--------|-------|--------|
| Android | PlatformEGLAndroid | VulkanPlatformAndroid | - | - |
| iOS | PlatformCocoaTouchGL | VulkanPlatformApple | PlatformMetal | - |
| macOS | PlatformCocoaGL | VulkanPlatformApple | PlatformMetal | - |
| Linux | PlatformGLX / PlatformEGLHeadless | VulkanPlatformLinux | - | - |
| Windows | PlatformWGL | VulkanPlatformWindows | - | - |
| Web | PlatformWebGL | - | - | WebGPUPlatform |

**PlatformFactory**：

```cpp
/**
 * 创建 Platform 实例
 * 
 * 根据平台和后端类型自动选择合适的 Platform 实现。
 * 
 * @param backend 后端类型（会被解析为实际的后端）
 * @return Platform 指针，失败返回 nullptr
 */
Platform* PlatformFactory::create(Backend* backend);
```

**后端选择策略**：
- `DEFAULT`：根据平台自动选择
  - Android：OpenGL
  - iOS/macOS：Metal
  - Linux/Windows：Vulkan（如果支持）或 OpenGL
  - Web：OpenGL（WebGL）
- 显式指定：使用用户指定的后端

### 2. Driver（驱动抽象层）

**位置**：
- 接口：`filament/backend/include/private/backend/Driver.h`
- 基类：`filament/backend/src/DriverBase.h`

**职责**：
- **定义统一接口**：所有后端必须实现的渲染 API
- **提供 Dispatcher**：返回命令分发器，用于命令执行
- **回调管理**：处理异步回调（purge）
- **调试支持**：提供调试标记接口

**关键特性**：

#### 2.1 命令模式

Driver 使用**命令模式**将所有渲染操作封装为命令：

```cpp
// 异步命令：序列化到命令流，延迟执行
void beginRenderPass(RenderTargetHandle rt, const RenderPassParams& params);

// 同步命令：立即执行，返回结果
FeatureLevel getFeatureLevel() const;

// 返回命令：先同步获取结果，再异步执行操作
Handle<HwTexture> createTexture(...);
```

#### 2.2 Dispatcher（命令分发器）

```cpp
/**
 * 获取命令分发器
 * 
 * Dispatcher 包含所有 Driver 方法的函数指针映射。
 * 命令执行时，通过函数指针调用对应的 Driver 方法。
 * 
 * @return Dispatcher 对象
 */
virtual Dispatcher getDispatcher() const noexcept = 0;
```

**Dispatcher 结构**：
```cpp
class Dispatcher {
public:
    using Execute = void (*)(Driver& driver, CommandBase* self, intptr_t* next);
    
    Execute beginRenderPass_;      // beginRenderPass 的执行函数
    Execute endRenderPass_;        // endRenderPass 的执行函数
    Execute draw_;                 // draw 的执行函数
    // ... 所有 Driver API 的执行函数
};
```

#### 2.3 回调管理

```cpp
/**
 * 清理回调队列
 * 
 * 在主线程调用，执行所有待处理的回调。
 * 这是 Driver 执行用户回调的唯一入口点。
 */
virtual void purge() noexcept = 0;
```

**回调执行策略**：
- **多线程模式**：服务线程执行回调，避免阻塞渲染线程
- **单线程模式**：在主线程的 `purge()` 中执行回调

### 3. CommandStream（命令流）

**位置**：
- 头文件：`filament/backend/include/private/backend/CommandStream.h`
- 实现：`filament/backend/src/CommandStream.cpp`

**职责**：
- **命令序列化**：将 Driver API 调用序列化为命令对象
- **命令执行**：在渲染线程执行所有命令
- **内存管理**：管理命令缓冲区的分配

**关键特性**：

#### 3.1 命令类型

**Driver 方法命令**：
```cpp
template<void(Driver::*METHOD)(ARGS...)>
class Command : public CommandBase {
    std::tuple<ARGS...> mArgs;  // 保存的参数
    
    static void execute(METHOD, Driver& driver, CommandBase* base, intptr_t* next) {
        Command* self = static_cast<Command*>(base);
        // 展开参数并调用 Driver 方法
        apply(METHOD, driver, self->mArgs);
    }
};
```

**自定义命令**：
```cpp
class CustomCommand : public CommandBase {
    std::function<void()> mCommand;  // 任意函数对象
};
```

**空操作命令**：
```cpp
class NoopCommand : public CommandBase {
    intptr_t mNext;  // 下一个命令的偏移量
};
```

#### 3.2 命令执行流程

```
1. 主线程调用 DriverApi 方法
   └─> CommandStream::methodName()
       └─> 分配命令内存
       └─> 构造 Command 对象（保存参数）
       └─> 将命令写入 CircularBuffer

2. flush() 或 submitFrame()
   └─> 命令缓冲区被提交到渲染线程

3. 渲染线程调用 CommandStream::execute()
   └─> 循环执行所有命令
       └─> Command::execute()
           └─> 展开参数
           └─> 调用 Driver 方法
           └─> 返回下一个命令
```

#### 3.3 内存布局

```
CircularBuffer:
┌─────────────────────────────────────────────────────────┐
│ Command1 │ Command2 │ Command3 │ ... │ NoopCommand │  │
│ (size)   │ (size)   │ (size)   │     │ (offset)    │  │
└─────────────────────────────────────────────────────────┘
     │          │          │              │
     └──────────┴──────────┴──────────────┘
           通过偏移量链接
```

**对齐**：所有命令对齐到 `alignof(std::max_align_t)` 边界

### 4. 具体后端实现

#### 4.1 OpenGLDriver

**位置**：`filament/backend/src/opengl/OpenGLDriver.h/cpp`

**特点**：
- **状态机管理**：跟踪 OpenGL 状态，避免冗余调用
- **资源管理**：管理 GL 对象（纹理、缓冲区、程序等）
- **扩展支持**：检测和使用 OpenGL 扩展
- **上下文管理**：通过 OpenGLContext 管理 GL 上下文

**关键组件**：
- `OpenGLContext`：GL 上下文封装
- `OpenGLProgram`：着色器程序管理
- `GLTexture`：纹理资源
- `GLBufferObject`：缓冲区对象

**示例**：
```cpp
void OpenGLDriver::beginRenderPass(RenderTargetHandle rth, const RenderPassParams& params) {
    // 获取渲染目标
    GLRenderTarget const* rt = handle_cast<GLRenderTarget*>(rth);
    
    // 绑定帧缓冲区
    bindFramebuffer(rt->gl.fbo);
    
    // 设置视口和裁剪矩形
    setViewport(params.viewport);
    setScissor(params.scissor);
    
    // 清除缓冲区
    if (params.flags.clear) {
        glClear(params.flags.clear);
    }
}
```

#### 4.2 VulkanDriver

**位置**：`filament/backend/src/vulkan/VulkanDriver.h/cpp`

**特点**：
- **显式资源管理**：使用 VMA（Vulkan Memory Allocator）管理内存
- **命令缓冲区**：管理 VkCommandBuffer 的分配和提交
- **描述符堆缓存**：缓存描述符堆布局和描述符堆
- **管道缓存**：缓存 VkPipeline 对象
- **同步原语**：使用信号量和栅栏进行同步

**关键组件**：
- `VulkanContext`：Vulkan 实例和设备管理
- `VulkanCommands`：命令缓冲区管理
- `VulkanPipelineCache`：管道缓存
- `VulkanDescriptorSetCache`：描述符堆缓存
- `VulkanStagePool`：暂存缓冲区池
- `VulkanSemaphoreManager`：信号量管理

**示例**：
```cpp
void VulkanDriver::beginRenderPass(RenderTargetHandle rth, const RenderPassParams& params) {
    // 获取渲染目标
    VulkanRenderTarget* rt = handle_cast<VulkanRenderTarget*>(rth);
    
    // 开始渲染通道
    VkRenderPassBeginInfo beginInfo = {};
    beginInfo.renderPass = rt->renderPass;
    beginInfo.framebuffer = rt->framebuffer;
    vkCmdBeginRenderPass(mCommands.get().cmdbuffer, &beginInfo, ...);
    
    // 设置视口和裁剪矩形
    VkViewport viewport = {...};
    vkCmdSetViewport(mCommands.get().cmdbuffer, 0, 1, &viewport);
}
```

#### 4.3 MetalDriver

**位置**：`filament/backend/src/metal/MetalDriver.h/mm`

**特点**：
- **Metal 对象管理**：管理 MTLDevice、MTLCommandQueue 等
- **命令编码器**：管理 MTLRenderCommandEncoder、MTLComputeCommandEncoder
- **资源追踪**：追踪 Metal 资源的使用
- **缓冲区池**：使用 MetalBufferPool 管理缓冲区

**关键组件**：
- `MetalContext`：Metal 设备上下文
- `MetalState`：渲染状态管理
- `MetalBlitter`：Blit 操作封装
- `MetalBufferPool`：缓冲区池

#### 4.4 WebGPUDriver

**位置**：`filament/backend/src/webgpu/WebGPUDriver.h/cpp`

**特点**：
- **WebGPU API**：使用 WebGPU/Dawn API
- **异步操作**：处理 WebGPU 的异步特性
- **资源管理**：管理 WebGPU 资源（纹理、缓冲区等）

## 命令流系统详解

### 命令序列化

**流程**：
```
1. 应用调用 DriverApi::beginRenderPass(...)
   └─> CommandStream::beginRenderPass(...)
       └─> 分配命令内存（CircularBuffer::allocate()）
       └─> 构造 Command<&Driver::beginRenderPass> 对象
           └─> 保存参数到 mArgs（std::tuple）
           └─> 保存执行函数指针（mExecute）

2. 命令写入 CircularBuffer
   └─> 命令在缓冲区中连续存储
   └─> 通过偏移量链接
```

**代码示例**：
```cpp
// DriverApi::beginRenderPass 的实现
void CommandStream::beginRenderPass(RenderTargetHandle rt, const RenderPassParams& params) {
    using Cmd = COMMAND_TYPE(beginRenderPass);
    void* p = allocateCommand(CommandBase::align(sizeof(Cmd)));
    new(p) Cmd(mDispatcher.beginRenderPass_, rt, params);
}
```

### 命令执行

**流程**：
```
1. 渲染线程调用 CommandStream::execute(buffer)
   └─> 通过 Driver::execute() 包装执行
       └─> 循环执行所有命令
           └─> CommandBase::execute(driver)
               └─> 调用 mExecute（Dispatcher 的函数指针）
                   └─> Command::execute()
                       └─> 展开参数（std::apply）
                       └─> 调用 Driver 方法
                       └─> 返回下一个命令

2. 命令执行完成
   └─> 析构命令对象
   └─> 继续执行下一个命令
```

**代码示例**：
```cpp
void CommandStream::execute(void* buffer) {
    CommandBase* base = static_cast<CommandBase*>(buffer);
    mDriver.execute([&driver, base] {
        auto p = base;
        while (p) {
            p = p->execute(driver);  // 执行命令并获取下一个
        }
    });
}
```

### 同步 vs 异步

**异步命令**（默认）：
- 参数序列化到命令流
- 在渲染线程执行
- 立即返回，不等待执行

**同步命令**：
- 立即在调用线程执行
- 返回结果
- 用于查询操作（如 `getFeatureLevel()`）

**返回命令**：
- 先同步获取结果（如 Handle）
- 再异步执行操作（如资源创建）
- 用于需要立即返回句柄的操作

## 资源管理

### Handle 系统

**位置**：`filament/backend/include/backend/Handle.h`

**设计**：
- **类型安全**：使用模板确保类型安全
- **轻量级**：Handle 只是一个整数 ID
- **验证**：可以验证 Handle 的有效性

**示例**：
```cpp
using TextureHandle = Handle<HwTexture>;
TextureHandle texture = driver.createTexture(...);
driver.bindTexture(0, texture);
```

### HandleAllocator

**位置**：`filament/backend/src/HandleAllocator.cpp`

**职责**：
- **分配 Handle**：分配唯一的资源 ID
- **验证 Handle**：检查 Handle 是否有效
- **回收 Handle**：Handle 销毁后可以重用

**实现**：
- 使用位图跟踪 Handle 的使用情况
- 支持快速分配和验证

### 资源生命周期

```
1. 创建资源
   └─> DriverApi::createTexture(...)
       └─> 分配 Handle
       └─> 创建命令（createTextureR）
       └─> 返回 Handle（立即）

2. 使用资源
   └─> DriverApi::bindTexture(..., texture)
       └─> 创建命令（bindTexture）
       └─> Handle 在命令中传递

3. 销毁资源
   └─> DriverApi::destroyTexture(texture)
       └─> 创建命令（destroyTexture）
       └─> Handle 被回收
```

## 线程模型

### 多线程架构

```
┌─────────────────┐
│   主线程        │
│  - Engine       │
│  - 应用逻辑     │
└────────┬────────┘
         │ DriverApi 调用
         ▼
┌─────────────────┐
│  CircularBuffer │
│  - 命令队列     │
└────────┬────────┘
         │ flush/submitFrame
         ▼
┌─────────────────┐
│   渲染线程      │
│  - Driver       │
│  - GPU 命令     │
└─────────────────┘
```

### 线程安全

**主线程**：
- 调用 DriverApi（线程安全）
- 命令序列化到 CircularBuffer
- 不直接调用 Driver 方法

**渲染线程**：
- 执行所有命令
- 调用 Driver 方法
- 与 GPU 交互

**同步点**：
- `flush()`：刷新命令缓冲区
- `flushAndWait()`：等待命令执行完成
- `submitFrame()`：提交帧

## 性能优化

### 1. 命令批处理

**优势**：
- 减少函数调用开销
- 提高缓存局部性
- 减少线程切换

**实现**：
- 命令在 CircularBuffer 中连续存储
- 批量执行命令

### 2. 状态缓存

**OpenGL**：
- 跟踪 GL 状态
- 避免冗余状态设置

**Vulkan**：
- 缓存管道对象
- 缓存描述符堆

### 3. 内存管理

**CircularBuffer**：
- 循环缓冲区，避免频繁分配
- 预分配固定大小

**HandleAllocator**：
- 位图分配，O(1) 分配和验证

### 4. 异步操作

**命令流**：
- 异步命令不阻塞主线程
- 提高 CPU 利用率

**回调**：
- 服务线程执行回调
- 不阻塞渲染线程

## 后端切换

### 编译时选择

**CMake 配置**：
```cmake
# 启用 OpenGL 后端
FILAMENT_SUPPORTS_OPENGL=ON

# 启用 Vulkan 后端
FILAMENT_DRIVER_SUPPORTS_VULKAN=ON

# 启用 Metal 后端
FILAMENT_SUPPORTS_METAL=ON

# 启用 WebGPU 后端
FILAMENT_SUPPORTS_WEBGPU=ON
```

### 运行时选择

**代码示例**：
```cpp
// 自动选择后端
Engine* engine = Engine::create();

// 显式指定后端
Engine* engine = Engine::create(Engine::Backend::VULKAN);

// 使用自定义 Platform
CustomPlatform* platform = new CustomPlatform();
Engine* engine = Engine::create(Engine::Backend::OPENGL, platform);
```

## 调试和性能分析

### 调试标记

**实现**：
```cpp
void Driver::debugCommandBegin(CommandStream* cmds, bool synchronous, const char* methodName);
void Driver::debugCommandEnd(CommandStream* cmds, bool synchronous, const char* methodName);
```

**用途**：
- 在调试工具中标记命令执行
- 性能分析（systrace、RenderDoc 等）

### 性能计数器

**Android**：
```bash
# 启用性能计数器
setprop debug.filament.perfcounters 1
```

**输出**：
- CPU 周期数
- 指令数
- 分支预测失败次数
- CPI（Cycles Per Instruction）

## 总结

### Filament RHI 架构特点

1. **分层抽象**
   - Platform：平台抽象
   - Driver：后端抽象
   - CommandStream：命令流抽象

2. **命令模式**
   - 所有操作封装为命令
   - 异步执行，提高性能
   - 支持批处理

3. **类型安全**
   - Handle 系统提供类型安全
   - 模板特化确保正确性

4. **性能优化**
   - 命令批处理
   - 状态缓存
   - 异步操作

5. **可扩展性**
   - 易于添加新后端
   - 统一的接口设计
   - 平台特定优化

通过这套 RHI 架构，Filament 能够在不同平台上使用最适合的图形 API，同时保持代码的统一性和可维护性。

