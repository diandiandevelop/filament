# Filament 同步与栅栏系统详细分析

## 目录
1. [概述](#概述)
2. [Fence 架构](#fence-架构)
3. [Sync 架构](#sync-架构)
4. [后端实现](#后端实现)
5. [使用场景](#使用场景)

---

## 概述

Filament 的同步与栅栏系统负责协调 CPU 和 GPU 之间的执行，确保资源安全和正确的执行顺序。`Fence` 用于等待 GPU 完成特定操作，`Sync` 用于异步回调。

### 核心组件

- **Fence**：栅栏，用于等待 GPU 完成操作
- **Sync**：同步对象，用于异步回调
- **FenceSignal**：栅栏信号，线程同步机制
- **后端实现**：OpenGL、Vulkan、Metal 等平台特定实现

---

## Fence 架构

### 1. 类结构

```cpp
class FFence : public Fence {
public:
    FFence(FEngine& engine);
    
    // 等待栅栏（阻塞）
    FenceStatus wait(Mode mode, uint64_t timeout);
    
    // 等待并销毁
    static FenceStatus waitAndDestroy(FFence* fence, Mode mode);
    
private:
    struct FenceSignal {
        enum State : uint8_t { 
            UNSIGNALED,   // 未信号
            SIGNALED,     // 已信号
            DESTROYED     // 已销毁
        };
        State mState = UNSIGNALED;
        
        void signal(State s = SIGNALED) noexcept;
        FenceStatus wait(uint64_t timeout) noexcept;
    };
    
    FEngine& mEngine;
    std::shared_ptr<FenceSignal> mFenceSignal;
    
    // 所有 Fence 共享的锁和条件变量
    static utils::Mutex sLock;
    static utils::Condition sCondition;
};
```

### 2. 等待机制

**`wait()` 实现：**

```cpp
FenceStatus FFence::wait(Mode const mode, uint64_t const timeout) {
    // 1. 如果模式是 FLUSH，先刷新命令队列
    if (mode == Mode::FLUSH) {
        engine.flush();
    }
    
    FenceSignal * const fs = mFenceSignal.get();
    FenceStatus status;
    
    // 2. 等待信号
    if (UTILS_LIKELY(!engine.pumpPlatformEvents())) {
        // 简单等待
        status = fs->wait(timeout);
    } else {
        // 需要处理平台事件，分片等待
        const auto startTime = std::chrono::system_clock::now();
        while (true) {
            status = fs->wait(ns(ms(PUMP_INTERVAL_MILLISECONDS)).count());
            if (status != FenceStatus::TIMEOUT_EXPIRED) {
                break;
            }
            engine.pumpPlatformEvents();
            const auto elapsed = std::chrono::system_clock::now() - startTime;
            if (timeout != FENCE_WAIT_FOR_EVER && elapsed >= ns(timeout)) {
                break;
            }
        }
    }
    
    return status;
}
```

**`FenceSignal::wait()` 实现：**

```cpp
FenceStatus FenceSignal::wait(uint64_t timeout) noexcept {
    std::unique_lock<utils::Mutex> lock(sLock);
    
    // 等待状态变为 SIGNALED 或 DESTROYED
    while (mState == UNSIGNALED) {
        if (timeout == 0) {
            return FenceStatus::TIMEOUT_EXPIRED;
        }
        
        if (timeout == FENCE_WAIT_FOR_EVER) {
            sCondition.wait(lock);
        } else {
            auto status = sCondition.wait_for(lock, 
                    std::chrono::nanoseconds(timeout));
            if (status == std::cv_status::timeout) {
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

### 3. 信号机制

**`signal()` 实现：**

```cpp
void FFence::FenceSignal::signal(State const s) noexcept {
    std::lock_guard const lock(sLock);
    mState = s;
    sCondition.notify_all();  // 通知所有等待的线程
}
```

---

## Sync 架构

### 1. 类结构

```cpp
class Sync {
public:
    // 创建同步对象
    static Sync* create(Engine& engine);
    
    // 等待同步（异步回调）
    void wait(CallbackHandler* handler, CallbackHandler::Callback callback, void* user);
    
    // 销毁同步对象
    void destroy(Engine& engine);
    
private:
    backend::SyncHandle mSyncHandle;  // 后端同步句柄
};
```

### 2. 异步回调

**`wait()` 实现：**

```cpp
void Sync::wait(CallbackHandler* handler, 
                CallbackHandler::Callback callback, 
                void* user) {
    // 1. 创建回调数据
    auto* cbData = new CallbackData{
        .handler = handler,
        .callback = callback,
        .user = user
    };
    
    // 2. 如果同步对象已创建，立即调度回调
    if (mSyncHandle) {
        scheduleCallback(handler, cbData, syncCallbackWrapper);
    } else {
        // 3. 否则，延迟到同步对象创建后
        mConversionCallbacks.push_back(cbData);
    }
}
```

**回调包装器：**

```cpp
void syncCallbackWrapper(void* user, backend::SyncHandle sync) {
    auto* cbData = static_cast<CallbackData*>(user);
    cbData->callback(cbData->handler, cbData->user);
    delete cbData;
}
```

---

## 后端实现

### 1. OpenGL 实现

**创建 Fence：**

```cpp
void OpenGLDriver::createFenceR(Handle<HwFence> fh, ImmutableCString&& tag) {
    GLFence* const f = handle_cast<GLFence*>(fh);
    
    bool const platformCanCreateFence = mPlatform.canCreateFence();
    
    if (mContext.isES2() || platformCanCreateFence) {
        std::lock_guard const lock(f->state->lock);
        if (platformCanCreateFence) {
            // 使用平台 Fence（如 EGL Sync）
            f->fence = mPlatform.createFence();
            f->state->cond.notify_all();
        } else {
            f->state->status = FenceStatus::ERROR;
        }
        return;
    }
    
    // 使用 OpenGL Fence（glFinish 模拟）
    std::weak_ptr<GLFence::State> const weak = f->state;
    whenGpuCommandsComplete([weak] {
        if (auto const state = weak.lock()) {
            std::lock_guard const lock(state->lock);
            state->status = FenceStatus::CONDITION_SATISFIED;
            state->cond.notify_all();
        }
    });
}
```

**创建 Sync：**

```cpp
void OpenGLDriver::createSyncR(Handle<HwSync> sh, ImmutableCString&& tag) {
    GLSyncFence* s = handle_cast<GLSyncFence*>(sh);
    {
        std::lock_guard const guard(s->lock);
        // 使用 GL_SYNC_GPU_COMMANDS_COMPLETE
        s->sync = mPlatform.createSync();
    }
    
    // 调度所有延迟的回调
    for (auto& cbData : s->conversionCallbacks) {
        cbData->sync = s->sync;
        scheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
    }
    
    s->conversionCallbacks.clear();
}
```

### 2. Vulkan 实现

**创建 Fence：**

```cpp
void VulkanDriver::createFenceR(Handle<HwFence> fh, ImmutableCString&& tag) {
    VulkanCommandBuffer* cmdbuf;
    if (mCurrentRenderPass.commandBuffer) {
        cmdbuf = mCurrentRenderPass.commandBuffer;
    } else {
        cmdbuf = &mCommands.get();
    }
    
    // 关联到当前命令缓冲区的 Fence
    auto fence = resource_ptr<VulkanFence>::cast(&mResourceManager, fh);
    fence->setFence(cmdbuf->getFenceStatus());
    mResourceManager.associateHandle(fh.getId(), std::move(tag));
}
```

**创建 Sync：**

```cpp
void VulkanDriver::createSyncR(Handle<HwSync> sh, ImmutableCString&& tag) {
    auto sync = resource_ptr<VulkanSync>::cast(&mResourceManager, sh);
    VkFence fence = VK_NULL_HANDLE;
    std::shared_ptr<VulkanCmdFence> fenceStatus;
    
    if (mCurrentRenderPass.commandBuffer) {
        VulkanCommandBuffer* cmdBuff = mCurrentRenderPass.commandBuffer;
        fence = cmdBuff->getVkFence();
        fenceStatus = cmdBuff->getFenceStatus();
        mCommands.flush();
    } else {
        fence = mCommands.getMostRecentFence();
        fenceStatus = mCommands.getMostRecentFenceStatus();
    }
    
    {
        std::lock_guard<std::mutex> guard(sync->lock);
        sync->sync = mPlatform->createSync(fence, fenceStatus);
    }
    
    // 调度回调
    for (auto& cbData : sync->conversionCallbacks) {
        cbData->sync = sync->sync;
        scheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
    }
    
    sync->conversionCallbacks.clear();
}
```

---

## 使用场景

### 1. UBO 管理

**在 `UboManager::endFrame()` 中：**

```cpp
void UboManager::endFrame(DriverApi& driver) {
    // 1. 收集所有分配 ID
    auto allocationIds = FenceManager::AllocationIdContainer::with_capacity(
            mManagedInstances.size());
    for (const auto* mi: mManagedInstances) {
        const AllocationId id = mi->getAllocationId();
        if (UTILS_UNLIKELY(!BufferAllocator::isValid(id))) {
            continue;
        }
        mAllocator.acquireGpu(id);  // 增加 GPU 使用计数
        allocationIds.push_back(id);
    }
    
    // 2. 创建 Fence 并跟踪
    mFenceManager.track(driver, std::move(allocationIds));
}
```

**在 `UboManager::beginFrame()` 中：**

```cpp
void UboManager::beginFrame(DriverApi& driver) {
    // 1. 回收已完成的资源
    mFenceManager.reclaimCompletedResources(driver,
            [this](AllocationId id) { 
                mAllocator.releaseGpu(id);  // 减少 GPU 使用计数
            });
    
    // 2. 释放空闲槽位
    mAllocator.releaseFreeSlots();
    
    // ...
}
```

### 2. 帧时间测量

**在 `FrameInfoManager` 中：**

```cpp
void FrameInfoManager::endFrame(DriverApi& driver) {
    // 1. 创建 Fence
    front.fence = driver.createFence();
    
    // 2. 在后台线程等待 GPU 完成
    jobQueue.push([&driver, &front] {
        FenceStatus status = driver.fenceWait(front.fence, FENCE_WAIT_FOR_EVER);
        if (status == FenceStatus::CONDITION_SATISFIED) {
            front.gpuFrameComplete = std::chrono::steady_clock::now();
        }
        front.ready.store(true, std::memory_order_release);
    });
}
```

---

## 总结

Filament 的同步与栅栏系统通过以下设计实现了高效的 CPU-GPU 同步：

1. **Fence**：阻塞等待 GPU 完成操作
2. **Sync**：异步回调机制
3. **共享同步**：所有 Fence 共享锁和条件变量，减少开销
4. **平台抽象**：支持 OpenGL、Vulkan、Metal 等不同后端
5. **资源管理**：与 UBO 管理等系统集成，确保资源安全

这些设计使得 Filament 能够在保持高性能的同时，确保 CPU 和 GPU 之间的正确同步。

