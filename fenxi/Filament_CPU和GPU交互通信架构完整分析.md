# Filament CPU/GPU 交互通信架构完整分析

## 目录
1. [概述](#概述)
2. [架构设计](#架构设计)
3. [命令缓冲区系统](#命令缓冲区系统)
4. [同步机制](#同步机制)
5. [数据传输](#数据传输)
6. [回调系统](#回调系统)
7. [各后端实现](#各后端实现)
8. [性能优化](#性能优化)
9. [总结](#总结)

---

## 概述

### 1.1 设计目标

Filament 的 CPU/GPU 交互通信架构旨在实现：

- **异步执行**：CPU 和 GPU 并行工作，最大化系统吞吐量
- **线程安全**：支持多线程环境下的安全命令提交
- **低延迟**：最小化命令提交和执行的开销
- **资源管理**：自动管理命令缓冲区和同步资源
- **跨平台**：统一接口，支持 OpenGL、Vulkan、Metal、WebGPU

### 1.2 核心组件

```
┌─────────────────────────────────────────────────────────────┐
│                    CPU/GPU 交互通信架构                        │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐      ┌──────────────┐      ┌───────────┐ │
│  │  DriverApi   │─────▶│ CommandStream│─────▶│ Circular  │ │
│  │  (CPU端)     │      │              │      │  Buffer   │ │
│  └──────────────┘      └──────────────┘      └───────────┘ │
│         │                      │                      │       │
│         │                      ▼                      │       │
│         │              ┌──────────────┐               │       │
│         │              │ CommandBuffer│               │       │
│         │              │    Queue    │               │       │
│         │              └──────────────┘               │       │
│         │                      │                       │       │
│         ▼                      ▼                       ▼       │
│  ┌──────────────┐      ┌──────────────┐      ┌───────────┐ │
│  │   Fence      │      │   Driver     │      │  GPU      │ │
│  │ (同步机制)    │      │  (GPU端)     │      │  Hardware │ │
│  └──────────────┘      └──────────────┘      └───────────┘ │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 架构设计

### 2.1 整体架构

Filament 采用**命令缓冲区模式**（Command Buffer Pattern）实现 CPU/GPU 解耦：

```
主线程 (CPU)                   渲染线程 (GPU)
    │                              │
    │  1. 调用 DriverApi           │
    │     (如 draw, updateBuffer)   │
    │                              │
    │  2. 序列化命令到              │
    │     CircularBuffer           │
    │                              │
    │  3. flush() 提交命令         │
    │     ────────────────────────▶│
    │                              │
    │                              │  4. 从队列获取命令
    │                              │
    │                              │  5. 执行命令
    │                              │     (调用 Driver 方法)
    │                              │
    │                              │  6. 释放缓冲区
    │                              │     ────────────────────▶
    │                              │
    │  7. 继续下一帧                │
```

### 2.2 数据流

```
┌─────────────────────────────────────────────────────────────┐
│                      命令提交流程                               │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  1. 应用层调用                                                 │
│     vertexBuffer->setBufferAt(engine, 0, buffer)             │
│           │                                                   │
│           ▼                                                   │
│  2. DriverApi 层                                              │
│     driverApi.updateBufferObject(handle, buffer)            │
│           │                                                   │
│           ▼                                                   │
│  3. CommandStream 层                                          │
│     - 分配命令内存 (CircularBuffer)                          │
│     - 序列化参数到命令对象                                    │
│     - 存储执行函数指针                                        │
│           │                                                   │
│           ▼                                                   │
│  4. flush() 提交                                              │
│     - 添加终止命令 (NoopCommand)                            │
│     - 获取缓冲区范围                                          │
│     - 添加到执行队列                                          │
│     - 通知渲染线程                                            │
│           │                                                   │
│           ▼                                                   │
│  5. 渲染线程执行                                              │
│     - 从队列获取命令缓冲区                                    │
│     - 遍历执行所有命令                                        │
│     - 调用 Driver 方法                                        │
│     - 释放缓冲区                                              │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 命令缓冲区系统

### 3.1 CircularBuffer（环形缓冲区）

**位置**：`filament/backend/include/private/backend/CircularBuffer.h`

**职责**：提供高效的命令存储，支持循环复用。

#### 3.1.1 数据结构

```cpp
class CircularBuffer {
    void* mData;        // 缓冲区起始地址
    size_t mSize;       // 缓冲区总大小
    void* mTail;        // 已提交数据的起始位置
    void* mHead;        // 下一个可用位置
    int mAshmemFd;      // Android 共享内存文件描述符
};
```

#### 3.1.2 内存分配策略

**硬循环缓冲区**（Hard Circular Buffer）：
- 使用 `mmap` 映射两倍大小的虚拟内存
- 物理内存只分配一份，通过虚拟地址映射实现循环
- 适用于 Linux、Android

**软循环缓冲区**（Soft Circular Buffer）：
- 使用 `VirtualAlloc`（Windows）或 `mmap`（其他平台）
- 当写入指针超过缓冲区末尾时，回绕到开头
- 需要检查边界条件

**实现示例**：
```cpp
void* CircularBuffer::allocate(size_t s) noexcept {
    assert_invariant(getUsed() + s <= size());
    char* const cur = static_cast<char*>(mHead);
    mHead = cur + s;
    return cur;
}
```

#### 3.1.3 缓冲区获取

```cpp
CircularBuffer::Range CircularBuffer::getBuffer() noexcept {
    Range range = { mTail, mHead };
    mTail = mHead;  // 重置尾部指针
    return range;
}
```

### 3.2 CommandStream（命令流）

**位置**：`filament/backend/include/private/backend/CommandStream.h`

**职责**：命令的序列化和执行。

#### 3.2.1 命令基类

```cpp
class CommandBase {
    Execute mExecute;  // 执行函数指针
    
    CommandBase* execute(Driver& driver) {
        intptr_t next;
        mExecute(driver, this, &next);
        return reinterpret_cast<CommandBase*>(
            reinterpret_cast<intptr_t>(this) + next);
    }
};
```

#### 3.2.2 命令类型

**Driver 方法命令**：
```cpp
template<void(Driver::*METHOD)(ARGS...)>
class Command : public CommandBase {
    std::tuple<std::remove_reference_t<ARGS>...> mArgs;
    
    static void execute(METHOD, Driver& driver, 
                       CommandBase* base, intptr_t* next) {
        Command* self = static_cast<Command*>(base);
        *next = align(sizeof(Command));
        apply(METHOD, driver, std::move(self->mArgs));
        self->~Command();
    }
};
```

**自定义命令**：
```cpp
class CustomCommand : public CommandBase {
    std::function<void()> mCommand;
    
    static void execute(Driver&, CommandBase* base, intptr_t* next) {
        CustomCommand* self = static_cast<CustomCommand*>(base);
        self->mCommand();
        *next = align(sizeof(CustomCommand));
        self->~CustomCommand();
    }
};
```

**空操作命令**：
```cpp
class NoopCommand : public CommandBase {
    intptr_t mNext;  // 下一个命令的偏移量
    
    static void execute(Driver&, CommandBase* self, intptr_t* next) {
        *next = static_cast<NoopCommand*>(self)->mNext;
    }
};
```

#### 3.2.3 命令执行

```cpp
void CommandStream::execute(void* buffer) {
    Driver& driver = mDriver;
    CommandBase* base = static_cast<CommandBase*>(buffer);
    
    mDriver.execute([&driver, base] {
        auto p = base;
        while (p) {
            p = p->execute(driver);  // 执行命令并获取下一个
        }
    });
}
```

### 3.3 CommandBufferQueue（命令缓冲区队列）

**位置**：`filament/backend/include/private/backend/CommandBufferQueue.h`

**职责**：生产者-消费者队列，管理命令缓冲区的提交和执行。

#### 3.3.1 数据结构

```cpp
class CommandBufferQueue {
    CircularBuffer mCircularBuffer;
    size_t mRequiredSize;              // 所需的最小缓冲区大小
    size_t mFreeSpace;                 // 当前空闲空间
    std::vector<Range> mCommandBuffersToExecute;  // 待执行的命令缓冲区
    mutable utils::Mutex mLock;
    mutable utils::Condition mCondition;
    bool mPaused;                      // 是否暂停
    uint32_t mExitRequested;           // 退出标志
};
```

#### 3.3.2 flush() - 提交命令

```cpp
void CommandBufferQueue::flush() {
    if (circularBuffer.empty()) {
        return;
    }
    
    // 1. 添加终止命令
    new(circularBuffer.allocate(sizeof(NoopCommand))) 
        NoopCommand(nullptr);
    
    // 2. 获取缓冲区范围
    auto const [begin, end] = circularBuffer.getBuffer();
    
    // 3. 计算使用的空间
    size_t const used = std::distance(
        static_cast<char const*>(begin), 
        static_cast<char const*>(end));
    
    std::unique_lock lock(mLock);
    
    // 4. 检查溢出
    FILAMENT_CHECK_POSTCONDITION(used <= mFreeSpace);
    
    // 5. 更新空闲空间并添加到队列
    mFreeSpace -= used;
    mCommandBuffersToExecute.push_back({ begin, end });
    mCondition.notify_one();  // 通知渲染线程
    
    // 6. 如果空间不足，等待
    if (mFreeSpace < mRequiredSize) {
        mCondition.wait(lock, [this]() {
            return mFreeSpace >= mRequiredSize;
        });
    }
}
```

#### 3.3.3 waitForCommands() - 等待命令

```cpp
std::vector<CommandBufferQueue::Range> 
CommandBufferQueue::waitForCommands() const {
    std::unique_lock lock(mLock);
    
    // 等待条件：
    // - 有命令可执行
    // - 且未暂停
    // - 或请求退出
    while ((mCommandBuffersToExecute.empty() || mPaused) 
           && !mExitRequested) {
        mCondition.wait(lock);
    }
    
    return std::move(mCommandBuffersToExecute);
}
```

#### 3.3.4 releaseBuffer() - 释放缓冲区

```cpp
void CommandBufferQueue::releaseBuffer(Range const& buffer) {
    size_t const used = std::distance(
        static_cast<char const*>(buffer.begin),
        static_cast<char const*>(buffer.end));
    
    std::lock_guard const lock(mLock);
    mFreeSpace += used;  // 增加空闲空间
    mCondition.notify_one();  // 通知等待的线程
}
```

---

## 同步机制

### 4.1 Fence（栅栏）

**位置**：`filament/src/details/Fence.cpp`

**职责**：CPU-GPU 同步，允许 CPU 等待 GPU 完成特定操作。

#### 4.1.1 数据结构

```cpp
class FFence {
    FEngine& mEngine;
    std::shared_ptr<FenceSignal> mFenceSignal;
    
    class FenceSignal {
        State mState;  // UNSIGNALED, SIGNALED, DESTROYED
        static utils::Mutex sLock;
        static utils::Condition sCondition;
    };
};
```

#### 4.1.2 创建 Fence

```cpp
FFence::FFence(FEngine& engine)
    : mEngine(engine),
      mFenceSignal(std::make_shared<FenceSignal>()) {
    DriverApi& driverApi = engine.getDriverApi();
    
    // 在命令流中排队信号命令
    auto& fs = mFenceSignal;
    driverApi.queueCommand([fs]() {
        fs->signal();  // 当命令执行时发出信号
    });
}
```

#### 4.1.3 等待 Fence

```cpp
FenceStatus FFence::wait(Mode mode, uint64_t timeout) {
    // 如果模式为 FLUSH，先刷新命令流
    if (mode == Mode::FLUSH) {
        engine.flush();
    }
    
    FenceSignal* const fs = mFenceSignal.get();
    
    // 如果不需要轮询平台事件，直接等待
    if (!engine.pumpPlatformEvents()) {
        status = fs->wait(timeout);
    } else {
        // 需要轮询平台事件（某些平台要求）
        const auto startTime = std::chrono::system_clock::now();
        while (true) {
            status = fs->wait(ns(ms(1)).count());  // 等待 1ms
            if (status != FenceStatus::TIMEOUT_EXPIRED) {
                break;
            }
            engine.pumpPlatformEvents();  // 泵送平台事件
            const auto elapsed = std::chrono::system_clock::now() - startTime;
            if (timeout != FENCE_WAIT_FOR_EVER && elapsed >= ns(timeout)) {
                break;
            }
        }
    }
    
    return status;
}
```

#### 4.1.4 信号机制

```cpp
void FFence::FenceSignal::signal(State s) noexcept {
    std::lock_guard const lock(sLock);
    mState = s;
    sCondition.notify_all();  // 通知所有等待的线程
}

FenceStatus FFence::FenceSignal::wait(uint64_t timeout) noexcept {
    std::unique_lock lock(sLock);
    while (mState == UNSIGNALED) {
        if (timeout == FENCE_WAIT_FOR_EVER) {
            sCondition.wait(lock);
        } else {
            if (timeout == 0 || 
                sCondition.wait_for(lock, ns(timeout)) == 
                    std::cv_status::timeout) {
                return FenceStatus::TIMEOUT_EXPIRED;
            }
        }
    }
    
    if (mState == DESTROYED) {
        return FenceStatus::ERROR;
    }
    
    return FenceStatus::CONDITION_SATISFIED;
}
```

### 4.2 后端同步机制

#### 4.2.1 OpenGL

**flush()** - 非阻塞提交：
```cpp
void OpenGLDriver::flush(int) {
    if (!gl.bugs.disable_glFlush) {
        glFlush();  // 提交命令给 GPU，但不等待完成
    }
}
```

**finish()** - 阻塞等待：
```cpp
void OpenGLDriver::finish(int) {
    glFinish();  // 等待 GPU 完成所有命令
    
    // 执行所有 GPU 命令完成回调
    executeGpuCommandsCompleteOps();
    
    // 执行所有"偶尔执行"的回调
    executeEveryNowAndThenOps();
}
```

#### 4.2.2 Vulkan

**命令提交**：
```cpp
fvkmemory::resource_ptr<VulkanSemaphore> 
VulkanCommandBuffer::submit() {
    vkEndCommandBuffer(mBuffer);
    
    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = mWaitSemaphores.size(),
        .pWaitSemaphores = mWaitSemaphores.data(),
        .pWaitDstStageMask = mWaitSemaphoreStages.data(),
        .commandBufferCount = 1u,
        .pCommandBuffers = &mBuffer,
        .signalSemaphoreCount = 1u,
        .pSignalSemaphores = &submissionSemaphore,
    };
    
    vkQueueSubmit(mQueue, 1, &submitInfo, mFence);
    mFenceStatus->setStatus(VK_NOT_READY);
    
    return mSubmission;
}
```

**等待完成**：
```cpp
void VulkanDriver::finish(int) {
    // 等待所有图形队列完成
    for (VkQueue queue : mGraphicsQueues) {
        vkQueueWaitIdle(queue);
    }
    
    // 更新栅栏状态
    mCommands.wait();
}
```

#### 4.2.3 Metal

Metal 使用命令缓冲区的完成处理器：

```cpp
[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    // GPU 完成时执行回调
    bufferPool->releaseBuffer(entry);
}];
```

---

## 数据传输

### 5.1 缓冲区数据传输

#### 5.1.1 OpenGL

**同步更新**：
```cpp
void OpenGLDriver::updateBufferObject(
    Handle<HwBufferObject> handle,
    BufferDescriptor&& data, uint32_t byteOffset) {
    
    GLBufferObject* bo = handle_cast<GLBufferObject*>(handle);
    
    // 绑定缓冲区
    glBindBuffer(GL_ARRAY_BUFFER, bo->gl.id);
    
    // 更新数据
    if (byteOffset == 0 && data.size == bo->size) {
        // 完整更新
        glBufferData(GL_ARRAY_BUFFER, data.size, 
                     data.buffer, GL_STATIC_DRAW);
    } else {
        // 部分更新
        glBufferSubData(GL_ARRAY_BUFFER, byteOffset, 
                        data.size, data.buffer);
    }
    
    // 调度销毁（数据上传完成后释放 CPU 内存）
    scheduleDestroy(std::move(data));
}
```

**异步更新**（Uniform 缓冲区）：
```cpp
void OpenGLDriver::updateBufferObjectUnsynchronized(
    Handle<HwBufferObject> handle,
    BufferDescriptor&& data, uint32_t byteOffset) {
    
    GLBufferObject* bo = handle_cast<GLBufferObject*>(handle);
    
    // 映射缓冲区（非阻塞）
    void* mapped = glMapBufferRange(GL_UNIFORM_BUFFER, 
                                    byteOffset, data.size,
                                    GL_MAP_WRITE_BIT | 
                                    GL_MAP_UNSYNCHRONIZED_BIT);
    if (mapped) {
        memcpy(mapped, data.buffer, data.size);
        glUnmapBuffer(GL_UNIFORM_BUFFER);
    }
    
    scheduleDestroy(std::move(data));
}
```

#### 5.1.2 Vulkan

**UMA（统一内存架构）直接复制**：
```cpp
void VulkanBufferProxy::loadFromCpu(
    VulkanCommandBuffer& commands,
    const void* cpuData, uint32_t byteOffset, uint32_t numBytes) {
    
    // 检查是否可以直接 memcpy
    bool const isMemcopyable = 
        mBuffer->getGpuBuffer()->allocationInfo.pMappedData != nullptr;
    
    bool const useMemcpy = isMemcopyable && isAvailable;
    
    if (useMemcpy) {
        // 直接复制到映射的内存
        char* dest = static_cast<char*>(
            mBuffer->getGpuBuffer()->allocationInfo.pMappedData) + 
            byteOffset;
        memcpy(dest, cpuData, numBytes);
        
        // 刷新内存范围
        vmaFlushAllocation(mAllocator, 
                          mBuffer->getGpuBuffer()->vmaAllocation,
                          byteOffset, numBytes);
        return;
    }
    
    // 否则使用暂存缓冲区
    // ...
}
```

**暂存缓冲区传输**：
```cpp
// 获取暂存缓冲区
fvkmemory::resource_ptr<VulkanStage::Segment> stage = 
    mStagePool.acquireStage(numBytes);

// 复制数据到暂存缓冲区
memcpy(stage->mapping(), cpuData, numBytes);
vmaFlushAllocation(mAllocator, stage->memory(), 
                   stage->offset(), numBytes);

// 记录复制命令
VkBufferCopy copyRegion{
    .srcOffset = stage->offset(),
    .dstOffset = byteOffset,
    .size = numBytes
};
vkCmdCopyBuffer(commands.cmdbuffer(), 
                stage->buffer(), 
                mBuffer->getGpuBuffer()->buffer, 
                1, &copyRegion);
```

#### 5.1.3 Metal

**使用暂存缓冲区**：
```cpp
void MetalBuffer::uploadWithPoolBuffer(
    void* src, size_t size, size_t byteOffset, 
    TagResolver&& getHandleTag) const {
    
    // 获取暂存缓冲区
    MetalBufferPool* bufferPool = mContext.bufferPool;
    const MetalBufferPoolEntry* const staging = 
        bufferPool->acquireBuffer(size);
    
    // 复制数据到暂存缓冲区
    memcpy(staging->buffer.get().contents, src, size);
    
    // 编码 blit 命令（从暂存缓冲区复制到 GPU 缓冲区）
    id<MTLBlitCommandEncoder> blitEncoder = 
        [commandBuffer blitCommandEncoder];
    [blitEncoder copyFromBuffer:staging->buffer.get()
                   sourceOffset:0
                       toBuffer:mGpuBuffer
              destinationOffset:byteOffset
                           size:size];
    [blitEncoder endEncoding];
    
    // 在完成时释放暂存缓冲区
    MetalBufferPool* bufferPool = this->context.bufferPool;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        bufferPool->releaseBuffer(entry);
    }];
}
```

### 5.2 纹理数据传输

#### 5.2.1 OpenGL

```cpp
void OpenGLDriver::setTextureData(
    Handle<HwTexture> th, uint32_t level,
    uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
    uint32_t width, uint32_t height, uint32_t depth,
    PixelBufferDescriptor&& p) {
    
    GLTexture* t = handle_cast<GLTexture*>(th);
    
    // 绑定纹理
    glBindTexture(t->target, t->gl.id);
    
    // 设置像素解包参数
    setPixelUnpackState(p);
    
    // 更新纹理数据
    if (t->target == GL_TEXTURE_2D) {
        glTexSubImage2D(GL_TEXTURE_2D, level,
                        xoffset, yoffset,
                        width, height,
                        format, type, p.buffer);
    } else if (t->target == GL_TEXTURE_3D) {
        glTexSubImage3D(GL_TEXTURE_3D, level,
                        xoffset, yoffset, zoffset,
                        width, height, depth,
                        format, type, p.buffer);
    }
    
    scheduleDestroy(std::move(p));
}
```

#### 5.2.2 Vulkan

```cpp
void VulkanTexture::setImage(
    VulkanCommandBuffer& commands,
    uint32_t level, uint32_t xoffset, uint32_t yoffset,
    uint32_t zoffset, uint32_t width, uint32_t height,
    uint32_t depth, PixelBufferDescriptor const& p) {
    
    // 获取暂存缓冲区
    fvkmemory::resource_ptr<VulkanStage::Segment> stage = 
        mStagePool.acquireStage(pixelSize);
    
    // 复制数据
    memcpy(stage->mapping(), p.buffer, pixelSize);
    vmaFlushAllocation(mAllocator, stage->memory(), 
                       stage->offset(), pixelSize);
    
    // 记录复制命令
    VkBufferImageCopy copyRegion{
        .bufferOffset = stage->offset(),
        .bufferRowLength = width,
        .bufferImageHeight = height,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = level,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {xoffset, yoffset, zoffset},
        .imageExtent = {width, height, depth}
    };
    
    vkCmdCopyBufferToImage(commands.cmdbuffer(),
                          stage->buffer(),
                          mTexture->image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          1, &copyRegion);
}
```

#### 5.2.3 Metal

**使用缓冲区复制**（优先）：
```cpp
void MetalTexture::loadWithCopyBuffer(
    uint32_t level, uint32_t slice, MTLRegion region,
    PixelBufferDescriptor const& data, 
    const PixelBufferShape& shape) {
    
    // 获取暂存缓冲区
    auto entry = context.bufferPool->acquireBuffer(
        shape.totalBytes);
    
    // 复制数据
    memcpy(entry->buffer.get().contents,
           static_cast<uint8_t*>(data.buffer) + shape.sourceOffset,
           shape.totalBytes);
    
    // 编码 blit 命令
    id<MTLBlitCommandEncoder> blitEncoder = 
        [commandBuffer blitCommandEncoder];
    [blitEncoder copyFromBuffer:entry->buffer.get()
                   sourceOffset:0
              sourceBytesPerRow:shape.bytesPerRow
            sourceBytesPerImage:shape.bytesPerSlice
                     sourceSize:region.size
                      toTexture:texture
               destinationSlice:slice
               destinationLevel:level
              destinationOrigin:region.origin];
    [blitEncoder endEncoding];
    
    // 完成时释放
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
        bufferPool->releaseBuffer(entry);
    }];
}
```

**使用纹理 Blit**（格式转换或大上传）：
```cpp
void MetalTexture::loadWithBlit(...) {
    // 创建暂存纹理
    MTLTextureDescriptor* stagingDesc = 
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:stagingFormat
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    id<MTLTexture> stagingTexture = 
        [device newTextureWithDescriptor:stagingDesc];
    
    // 更新暂存纹理数据
    [stagingTexture replaceRegion:region
                      mipmapLevel:0
                            slice:0
                        withBytes:data.buffer
                      bytesPerRow:shape.bytesPerRow
                    bytesPerImage:shape.bytesPerSlice];
    
    // 编码 blit 命令（格式转换）
    id<MTLBlitCommandEncoder> blitEncoder = 
        [commandBuffer blitCommandEncoder];
    [blitEncoder copyFromTexture:stagingTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:region.origin
                      sourceSize:region.size
                       toTexture:texture
                destinationSlice:slice
                destinationLevel:level
               destinationOrigin:region.origin];
    [blitEncoder endEncoding];
}
```

### 5.3 内存映射缓冲区

**位置**：`filament/backend/src/opengl/OpenGLDriver.cpp`

**用途**：允许 CPU 直接访问 GPU 缓冲区（UMA 系统）。

```cpp
void OpenGLDriver::copyToMemoryMappedBuffer(
    MemoryMappedBufferHandle mmbh, size_t offset,
    BufferDescriptor&& data) {
    
    GLMemoryMappedBuffer* const mmb = 
        handle_cast<GLMemoryMappedBuffer*>(mmbh);
    
    // 直接复制到映射的内存
    mmb->copy(mContext, *this, offset, std::move(data));
}
```

---

## 回调系统

### 6.1 CallbackHandler

**位置**：`filament/backend/include/backend/CallbackHandler.h`

**职责**：定义回调接口，允许在特定线程执行回调。

```cpp
class CallbackHandler {
public:
    using Callback = void(*)(void* user);
    
    // 在适当的线程上执行回调（必须是线程安全的）
    virtual void post(void* user, Callback callback) = 0;
};
```

### 6.2 DriverBase 回调管理

**位置**：`filament/backend/src/Driver.cpp`

#### 6.2.1 服务线程

```cpp
DriverBase::DriverBase() noexcept {
    if constexpr (UTILS_HAS_THREADING) {
        // 创建服务线程处理回调
        mServiceThread = std::thread([this]() {
            do {
                std::unique_lock<std::mutex> lock(mServiceThreadLock);
                
                // 等待有回调需要处理
                while (mServiceThreadCallbackQueue.empty() 
                       && !mExitRequested) {
                    mServiceThreadCondition.wait(lock);
                }
                
                if (mExitRequested) {
                    break;
                }
                
                // 移动回调队列（避免长时间持有锁）
                auto callbacks(std::move(mServiceThreadCallbackQueue));
                lock.unlock();
                
                // 执行所有回调
                for (auto[handler, callback, user] : callbacks) {
                    handler->post(user, callback);
                }
            } while (true);
        });
    }
}
```

#### 6.2.2 调度回调

```cpp
void DriverBase::scheduleCallback(
    CallbackHandler* handler, void* user, 
    CallbackHandler::Callback callback) {
    
    if (handler && UTILS_HAS_THREADING) {
        // 添加到服务线程队列
        std::lock_guard<std::mutex> lock(mServiceThreadLock);
        mServiceThreadCallbackQueue.emplace_back(handler, callback, user);
        mServiceThreadCondition.notify_one();
    } else {
        // 添加到主线程队列（由 purge() 执行）
        std::lock_guard<std::mutex> lock(mCallbackLock);
        mCallbacks.emplace_back(handler, callback, user);
    }
}
```

#### 6.2.3 执行回调

```cpp
void DriverBase::purge() {
    // 执行所有主线程回调
    decltype(mCallbacks) callbacks;
    {
        std::lock_guard<std::mutex> lock(mCallbackLock);
        callbacks = std::move(mCallbacks);
    }
    
    for (auto[handler, callback, user] : callbacks) {
        if (handler) {
            handler->post(user, callback);
        } else {
            callback(user);  // 直接执行
        }
    }
}
```

### 6.3 CallbackManager

**位置**：`filament/backend/src/CallbackManager.h`

**职责**：管理条件回调，当所有条件满足时执行回调。

```cpp
class CallbackManager {
    std::atomic<int> mRefCount{0};  // 引用计数
    CallbackHandler* mHandler = nullptr;
    CallbackHandler::Callback mCallback = nullptr;
    void* mUser = nullptr;
    
public:
    // 创建条件（增加引用计数）
    Handle get() {
        mRefCount.fetch_add(1);
        return Handle(this);
    }
    
    // 满足条件（减少引用计数）
    void put(Handle& curr) {
        if (curr.mManager) {
            int prev = curr.mManager->mRefCount.fetch_sub(1);
            if (prev == 1 && curr.mManager->mCallback) {
                // 所有条件都满足，执行回调
                curr.mManager->mHandler->post(
                    curr.mManager->mUser,
                    curr.mManager->mCallback);
            }
            curr.mManager = nullptr;
        }
    }
    
    // 设置回调
    void setCallback(CallbackHandler* handler,
                    CallbackHandler::Callback func,
                    void* user) {
        mHandler = handler;
        mCallback = func;
        mUser = user;
        
        // 如果没有条件或所有条件都已满足，立即执行
        if (mRefCount.load() == 0 && mCallback) {
            mHandler->post(mUser, mCallback);
        }
    }
};
```

---

## 各后端实现

### 7.1 OpenGL 后端

#### 7.1.1 命令执行

```cpp
void OpenGLDriver::execute() {
    // OpenGL 是立即模式，命令直接执行
    // 不需要额外的命令缓冲区管理
}
```

#### 7.1.2 同步点

- **glFlush()**：提交命令，不等待
- **glFinish()**：提交命令并等待完成
- **Fence**：使用自定义信号机制

### 7.2 Vulkan 后端

#### 7.2.1 命令缓冲区管理

```cpp
class VulkanCommandBuffer {
    VkCommandBuffer mBuffer;
    VkQueue mQueue;
    VkFence mFence;
    VulkanCmdFence mFenceStatus;
    std::vector<VkSemaphore> mWaitSemaphores;
    std::vector<VkPipelineStageFlags> mWaitSemaphoreStages;
    
    void begin() {
        VkCommandBufferBeginInfo binfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(mBuffer, &binfo);
    }
    
    fvkmemory::resource_ptr<VulkanSemaphore> submit() {
        vkEndCommandBuffer(mBuffer);
        
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = mWaitSemaphores.size(),
            .pWaitSemaphores = mWaitSemaphores.data(),
            .pWaitDstStageMask = mWaitSemaphoreStages.data(),
            .commandBufferCount = 1u,
            .pCommandBuffers = &mBuffer,
            .signalSemaphoreCount = 1u,
            .pSignalSemaphores = &submissionSemaphore,
        };
        
        vkQueueSubmit(mQueue, 1, &submitInfo, mFence);
        mFenceStatus->setStatus(VK_NOT_READY);
        
        return mSubmission;
    }
};
```

#### 7.2.2 栅栏管理

```cpp
class VulkanCmdFence {
    VkFence mFence;
    std::atomic<VkResult> mStatus{VK_NOT_READY};
    
    FenceStatus waitForCompletion(uint64_t timeout) {
        if (mStatus.load() == VK_SUCCESS) {
            return FenceStatus::CONDITION_SATISFIED;
        }
        
        VkResult result = vkWaitForFences(
            mDevice, 1, &mFence, VK_TRUE, timeout);
        
        if (result == VK_SUCCESS) {
            mStatus.store(VK_SUCCESS);
            return FenceStatus::CONDITION_SATISFIED;
        } else if (result == VK_TIMEOUT) {
            return FenceStatus::TIMEOUT_EXPIRED;
        } else {
            return FenceStatus::ERROR;
        }
    }
};
```

### 7.3 Metal 后端

#### 7.3.1 命令缓冲区

```cpp
id<MTLCommandBuffer> getPendingCommandBuffer(MetalContext* context) {
    if (!context->currentCommandBuffer) {
        context->currentCommandBuffer = 
            [context->commandQueue commandBuffer];
    }
    return context->currentCommandBuffer;
}
```

#### 7.3.2 完成处理器

```cpp
[commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> cb) {
    // GPU 完成时执行
    if (cb.error) {
        // 处理错误
    }
    // 释放资源
    bufferPool->releaseBuffer(entry);
}];
```

---

## 性能优化

### 8.1 命令批处理

- **合并命令**：将多个小命令合并为一个大命令
- **延迟执行**：延迟非关键命令的执行
- **命令排序**：按资源访问模式排序命令

### 8.2 内存优化

- **循环缓冲区复用**：避免频繁分配/释放
- **暂存缓冲区池**：复用暂存缓冲区
- **内存映射**：UMA 系统直接访问 GPU 内存

### 8.3 同步优化

- **最小化同步点**：减少 `glFinish()` / `vkQueueWaitIdle()` 调用
- **使用 Fence**：非阻塞同步替代阻塞同步
- **异步回调**：使用回调而非轮询

### 8.4 数据传输优化

- **批量传输**：合并多个小传输为一个大传输
- **直接内存访问**：UMA 系统使用 memcpy
- **压缩格式**：使用压缩纹理减少传输量

---

## 总结

### 9.1 核心设计原则

1. **解耦 CPU 和 GPU**：通过命令缓冲区实现异步执行
2. **线程安全**：使用互斥锁和条件变量保护共享资源
3. **资源复用**：循环缓冲区和暂存缓冲区池减少分配开销
4. **统一接口**：DriverApi 提供跨平台统一接口
5. **灵活同步**：支持阻塞和非阻塞同步机制

### 9.2 关键组件

- **CircularBuffer**：高效的命令存储
- **CommandStream**：命令序列化和执行
- **CommandBufferQueue**：生产者-消费者队列
- **Fence**：CPU-GPU 同步机制
- **CallbackHandler**：异步回调系统

### 9.3 性能特点

- **低延迟**：命令立即提交，不等待执行
- **高吞吐**：CPU 和 GPU 并行工作
- **内存高效**：循环复用，减少分配
- **跨平台**：统一接口，后端优化

### 9.4 使用建议

1. **避免频繁同步**：只在必要时使用 `finish()` 或 `Fence::wait()`
2. **批量操作**：合并多个小操作为一个大操作
3. **使用回调**：异步操作使用回调而非轮询
4. **合理配置缓冲区大小**：根据应用需求调整 `minCommandBufferSizeMB`

---

**文档版本**：1.0  
**最后更新**：2024  
**作者**：Filament 分析文档

