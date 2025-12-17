/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "VulkanAsyncHandles.h"

#include <backend/DriverEnums.h>

#include <utils/debug.h>

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <chrono>

using namespace bluevk;

namespace filament::backend {

/**
 * 等待 VulkanCmdFence 完成的实现
 *
 * 执行逻辑：
 * 1. 先以共享读锁持有 mLock，保证在调用 vkWaitForFences 时不会与 resetFence 并发修改状态；
 * 2. 如果当前状态是 VK_INCOMPLETE，说明 fence 还未提交：
 *    - 通过条件变量等待状态变为 VK_NOT_READY（已提交）或被取消；
 *    - 如果超时则返回 TIMEOUT_EXPIRED，被取消则返回 ERROR；
 * 3. 如果状态已经是 VK_SUCCESS，表示 GPU 侧已经完成，直接返回 CONDITION_SATISFIED；
 * 4. 如果已被取消（mCanceled 为 true），返回 ERROR；
 * 5. 否则调用 vkWaitForFences 真正等待 GPU 完成：
 *    - 超时返回 TIMEOUT_EXPIRED；
 *    - 成功则升级为独占写锁更新 mStatus = VK_SUCCESS，并返回 CONDITION_SATISFIED；
 *    - 其他错误返回 ERROR。
 *
 * @param device   Vulkan 逻辑设备
 * @param timeout  vkWaitForFences 的超时时间（纳秒）
 * @param until    等待截止的时间点（用于配合条件变量的超时）
 * @return FenceStatus：表示超时、完成或错误等状态
 */
FenceStatus VulkanCmdFence::wait(VkDevice device, uint64_t const timeout,
    std::chrono::steady_clock::time_point const until) {

    // this lock MUST be held for READ when calling vkWaitForFences()
    std::shared_lock rl(mLock);

    // If the vulkan fence has not been submitted yet, we need to wait for that before we
    // can use vkWaitForFences()
    if (mStatus == VK_INCOMPLETE) {
        bool const success = mCond.wait_until(rl, until, [this] {
            // Internally we use the VK_INCOMPLETE status to mean "not yet submitted".
            // When this fence gets submitted, its status changes to VK_NOT_READY.
            return mStatus != VK_INCOMPLETE || mCanceled;
        });
        if (!success) {
            // !success indicates a timeout or cancel
            return mCanceled ? FenceStatus::ERROR : FenceStatus::TIMEOUT_EXPIRED;
        }
    }

    // The fence could have already signaled, avoid calling into vkWaitForFences()
    if (mStatus == VK_SUCCESS) {
        return FenceStatus::CONDITION_SATISFIED;
    }

    // Or it could have been canceled, return immediately
    if (mCanceled) {
        return FenceStatus::ERROR;
    }

    // If we're here, we know that vkQueueSubmit has been called (because it sets the status
    // to VK_NOT_READY).
    // Now really wait for the fence while holding the shared_lock, this allows several
    // threads to call vkWaitForFences(), but will prevent vkResetFence from taking
    // place simultaneously. vkResetFence is only called once it knows the fence has signaled,
    // which guaranties that vkResetFence won't have to wait too long, just enough for
    // all the vkWaitForFences() to return.
    VkResult const status = vkWaitForFences(device, 1, &mFence, VK_TRUE, timeout);
    if (status == VK_TIMEOUT) {
        return FenceStatus::TIMEOUT_EXPIRED;
    }

    if (status == VK_SUCCESS) {
        rl.unlock();
        std::lock_guard const wl(mLock);
        mStatus = status;
        return FenceStatus::CONDITION_SATISFIED;
    }

    return FenceStatus::ERROR; // not supported
}

/**
 * 重置 VulkanCmdFence 对应的底层 VkFence
 *
 * 设计要点：
 * - 通过独占锁防止与 vkWaitForFences 并发调用；
 * - 按约定，仅当 mStatus 为 VK_SUCCESS（即 GPU 已完成）时才允许重置；
 * - 调用 vkResetFences 之后，Fence 可以再次用于后续提交。
 *
 * @param device Vulkan 逻辑设备
 */
void VulkanCmdFence::resetFence(VkDevice device) {
    // This lock prevents vkResetFences() from being called simultaneously with vkWaitForFences(),
    // but by construction, when we're here, we know that the fence has signaled and
    // vkWaitForFences() will return shortly.
    std::lock_guard const l(mLock);
    assert_invariant(mStatus == VK_SUCCESS);
    vkResetFences(device, 1, &mFence);
}

} // namespace filament::backend
