/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_BACKEND_VULKANASYNCHANDLES_H
#define TNT_FILAMENT_BACKEND_VULKANASYNCHANDLES_H

#include <bluevk/BlueVK.h>

#include "DriverBase.h"
#include "backend/DriverEnums.h"
#include "backend/Platform.h"

#include "vulkan/memory/Resource.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace filament::backend {

// Wrapper to enable use of shared_ptr for implementing shared ownership of low-level Vulkan fences.
// 包装器：用于通过 shared_ptr 在多个高层对象之间共享底层 Vulkan Fence 的所有权。
struct VulkanCmdFence {
    explicit VulkanCmdFence(VkFence fence) : mFence(fence) { }
    ~VulkanCmdFence() = default;

    void setStatus(VkResult const value) {
        std::lock_guard const l(mLock);
        mStatus = value;
        mCond.notify_all();
    }

    VkResult getStatus() {
        std::shared_lock const l(mLock);
        return mStatus;
    }

    void resetFence(VkDevice device);

    FenceStatus wait(VkDevice device, uint64_t timeout,
        std::chrono::steady_clock::time_point until);

    void cancel() {
        std::lock_guard const l(mLock);
        mCanceled = true;
        mCond.notify_all();
    }

private:
    std::shared_mutex mLock; // NOLINT(*-include-cleaner)
    std::condition_variable_any mCond;
    bool mCanceled = false;
    // Internally we use the VK_INCOMPLETE status to mean "not yet submitted". When this fence
    // gets submitted, its status changes to VK_NOT_READY. Finally, when the GPU actually
    // finishes executing the command buffer, the status changes to VK_SUCCESS.
    // 内部约定：VK_INCOMPLETE 表示“尚未提交”；提交后状态变为 VK_NOT_READY；
    // 当 GPU 真正执行完对应的命令缓冲后，状态变为 VK_SUCCESS。
    VkResult mStatus{ VK_INCOMPLETE };
    VkFence mFence;
};

/**
 * VulkanFence - Vulkan Fence 高层包装
 *
 * 将 VulkanCmdFence 封装为 HwFence，实现 CPU 侧 fence 的共享和等待逻辑，
 * 同时继承 ThreadSafeResource 以确保跨线程安全。
 */
struct VulkanFence : public HwFence, fvkmemory::ThreadSafeResource {
    VulkanFence() {}

    // 设置底层共享 Fence
    void setFence(std::shared_ptr<VulkanCmdFence> fence) {
        std::lock_guard const l(lock);
        sharedFence = std::move(fence);
        cond.notify_all();
    }

    // 获取底层共享 Fence 引用
    std::shared_ptr<VulkanCmdFence>& getSharedFence() {
        std::lock_guard const l(lock);
        return sharedFence;
    }

    // 在指定时间点之前等待 Fence 可用或被取消
    // 返回值：{ sharedFence, canceled }，如果超时 sharedFence 为空
    std::pair<std::shared_ptr<VulkanCmdFence>, bool>
            wait(std::chrono::steady_clock::time_point const until) {
        // hold a reference so that our state doesn't disappear while we wait
        std::unique_lock l(lock);
        cond.wait_until(l, until, [this] {
            return bool(sharedFence) || canceled;
        });
        // here mSharedFence will be null if we timed out
        return { sharedFence, canceled };
    }

    // 取消当前 Fence：标记为 canceled 并通知等待线程，同时取消底层 VulkanCmdFence
    void cancel() const {
        std::lock_guard const l(lock);
        if (sharedFence) {
            sharedFence->cancel();
        }
        canceled = true;
        cond.notify_all();
    }

private:
    mutable std::mutex lock;                // 保护 sharedFence / canceled 状态
    mutable std::condition_variable cond;   // 条件变量，用于等待 / 唤醒
    mutable bool canceled = false;          // 是否已被取消
    std::shared_ptr<VulkanCmdFence> sharedFence; // 底层共享 Fence
};

/**
 * VulkanSync - Vulkan 同步对象包装
 *
 * 封装 HwSync，管理平台回调（如 Android 的 SyncFence 等），
 * 用于在 GPU 完成特定操作时通知上层平台。
 */
struct VulkanSync : fvkmemory::ThreadSafeResource, public HwSync {
    struct CallbackData {
        CallbackHandler* handler;
        Platform::SyncCallback cb;
        Platform::Sync* sync;
        void* userData;
    };

    VulkanSync() {}
    std::mutex lock;    // 保护回调列表
    std::vector<std::unique_ptr<CallbackData>> conversionCallbacks; // 平台回调列表
};

/**
 * VulkanTimerQuery - Vulkan 定时器查询包装
 *
 * 负责追踪 GPU 时间戳查询的起止索引，并通过 Fence 判断查询是否完成。
 */
struct VulkanTimerQuery : public HwTimerQuery, fvkmemory::ThreadSafeResource {
    VulkanTimerQuery(uint32_t startingIndex, uint32_t stoppingIndex)
        : mStartingQueryIndex(startingIndex),
          mStoppingQueryIndex(stoppingIndex) {}

    // 关联用于标记查询完成的 Fence
    void setFence(std::shared_ptr<VulkanCmdFence> fence) noexcept {
        std::lock_guard const lock(mFenceMutex);
        mFence = std::move(fence);
    }

    // 查询对应的 GPU 命令是否已完成（通过 Fence 状态判断）
    bool isCompleted() noexcept {
        std::lock_guard const lock(mFenceMutex);
        // QueryValue is a synchronous call and might occur before beginTimerQuery has written
        // anything into the command buffer, which is an error according to the validation layer
        // that ships in the Android NDK.  Even when AVAILABILITY_BIT is set, validation seems to
        // require that the timestamp has at least been written into a processed command buffer.

        // This fence indicates that the corresponding buffer has been completed.
        return mFence && mFence->getStatus() == VK_SUCCESS;
    }

    // 获取起始查询索引
    uint32_t getStartingQueryIndex() const { return mStartingQueryIndex; }

    // 获取结束查询索引
    uint32_t getStoppingQueryIndex() const {
        return mStoppingQueryIndex;
    }

private:
    uint32_t mStartingQueryIndex;               // 起始查询索引
    uint32_t mStoppingQueryIndex;               // 结束查询索引

    std::shared_ptr<VulkanCmdFence> mFence;     // 标记查询完成的 Fence
    utils::Mutex mFenceMutex;                   // 保护 Fence 的互斥锁
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_VULKANHASYNCANDLES_H
