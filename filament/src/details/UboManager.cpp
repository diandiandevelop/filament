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

#include "details/UboManager.h"

#include "MaterialInstance.h"
#include "details/BufferAllocator.h"

#include <backend/DriverEnums.h>
#include <private/utils/Tracing.h>
#include <utils/Logger.h>

#include <vector>

namespace filament {

namespace {
using namespace utils;
using namespace backend;

using AllocationId = BufferAllocator::AllocationId;  // 分配 ID 类型别名
using allocation_size_t = BufferAllocator::allocation_size_t;  // 分配大小类型别名
} // anonymous namespace

// ------------------------------------------------------------------------------------------------
// FenceManager
// ------------------------------------------------------------------------------------------------

/**
 * 跟踪分配 ID
 * 
 * 创建一个新的栅栏来跟踪当前帧的一组分配 ID。
 * 这标记了 GPU 开始使用这些资源。
 * 
 * @param driver 驱动 API 引用
 * @param allocationIds 分配 ID 容器（会被移动）
 */
void UboManager::FenceManager::track(DriverApi& driver, AllocationIdContainer&& allocationIds) {
    if (allocationIds.empty()) {  // 如果分配 ID 列表为空
        return;  // 不需要创建栅栏
    }
    /**
     * 创建栅栏并将分配 ID 列表添加到跟踪列表
     */
    mFenceAllocationList.emplace_back(driver.createFence(), std::move(allocationIds));  // 添加栅栏和分配 ID
}

/**
 * 回收已完成的资源
 * 
 * 检查所有跟踪的栅栏，并为与已完成栅栏关联的资源调用回调。
 * 这应该每帧调用一次。
 * 
 * @param driver 驱动 API 引用
 * @param onReclaimed 回收回调函数
 */
void UboManager::FenceManager::reclaimCompletedResources(DriverApi& driver,
        std::function<void(AllocationId)> const& onReclaimed) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 性能追踪
    uint32_t signaledCount = 0;  // 已发出信号的栅栏数量
    bool seenSignaledFence = false;  // 是否已看到发出信号的栅栏

    /**
     * 从最新的栅栏到最旧的栅栏迭代
     */
    // Iterate from the newest fence to the oldest.
    for (auto it = mFenceAllocationList.rbegin(); it != mFenceAllocationList.rend(); ++it) {
        const Handle<HwFence>& fence = it->first;  // 获取栅栏句柄
        const FenceStatus status = driver.getFenceStatus(fence);  // 获取栅栏状态

        /**
         * 如果我们已经看到一个发出信号的栅栏，我们可以假设所有更旧的栅栏
         * 也都已完成，无论它们报告的状态如何（例如，TIMEOUT_EXPIRED）。
         * 这由 GPU 命令队列的按序执行保证。
         */
        // If we have already seen a signaled fence, we can assume all older fences
        // are also complete, regardless of their reported status (e.g., TIMEOUT_EXPIRED).
        // This is guaranteed by the in-order execution of GPU command queues.
        if (seenSignaledFence) {  // 如果已看到发出信号的栅栏
            signaledCount++;  // 增加计数
#ifndef NDEBUG
            /**
             * 在调试模式下，如果状态不是条件满足，记录警告
             */
            if (UTILS_UNLIKELY(status != FenceStatus::CONDITION_SATISFIED)) {
                LOG(WARNING) << "A fence is either in an error state or hasn't signaled, but a newer "
                                 "fence has. Will release the resource anyway.";
            }
#endif
            continue;  // 继续下一个栅栏
        }

        /**
         * 如果栅栏状态为条件满足，标记已看到发出信号的栅栏
         */
        if (status == FenceStatus::CONDITION_SATISFIED) {
            seenSignaledFence = true;  // 标记已看到发出信号的栅栏
            signaledCount++;  // 增加计数
        }
    }

    if (signaledCount == 0) {  // 如果没有栅栏完成
        /**
         * 没有栅栏完成，无事可做
         */
        // No fences have completed, nothing to do.
        return;  // 返回
    }

    /**
     * 计算要保留的第一个栅栏位置
     */
    auto firstToKeep = mFenceAllocationList.begin() + signaledCount;  // 计算保留位置

    /**
     * 为所有由已完成栅栏保护的资源调用回调
     */
    // Invoke the callback for all resources protected by completed fences.
    for (auto it = mFenceAllocationList.begin(); it != firstToKeep; ++it) {
        for (const AllocationId& id : it->second) {  // 遍历分配 ID
            onReclaimed(id);  // 调用回收回调
        }
        /**
         * 销毁栅栏句柄，因为不再需要
         */
        // Destroy the fence handle as it's no longer needed.
        driver.destroyFence(std::move(it->first));  // 销毁栅栏
    }

    /**
     * 从列表中擦除已完成的栅栏
     */
    mFenceAllocationList.erase(mFenceAllocationList.begin(), firstToKeep);  // 擦除已完成的栅栏
}

/**
 * 重置栅栏管理器
 * 
 * 销毁所有跟踪的栅栏并清除跟踪列表。
 * 这用于终止或重大重新分配期间的清理。
 * 
 * @param driver 驱动 API 引用
 */
void UboManager::FenceManager::reset(DriverApi& driver) {
    for (auto& [fence, _] : mFenceAllocationList) {  // 遍历所有栅栏
        if (fence) {  // 如果栅栏有效
            driver.destroyFence(std::move(fence));  // 销毁栅栏
        }
    }
    mFenceAllocationList.clear();  // 清除列表
}


// ------------------------------------------------------------------------------------------------
// UboManager
// ------------------------------------------------------------------------------------------------

/**
 * UBO 管理器构造函数
 * 
 * 创建 UBO 管理器并初始化分配器。
 * 
 * @param driver 驱动 API 引用
 * @param defaultSlotSizeInBytes 默认槽位大小（字节）
 * @param defaultTotalSizeInBytes 默认总大小（字节）
 */
UboManager::UboManager(DriverApi& driver, allocation_size_t defaultSlotSizeInBytes,
        allocation_size_t defaultTotalSizeInBytes)
        : mAllocator(defaultTotalSizeInBytes, defaultSlotSizeInBytes) {  // 初始化分配器
    reallocate(driver, defaultTotalSizeInBytes);  // 重新分配缓冲区
}

/**
 * 开始帧
 * 
 * 管理 UBO 分配生命周期的大部分工作，包括：
 * 1. 释放前一帧不再被 GPU 使用的 UBO 槽位
 * 2. 为需要槽位的材质实例分配新槽位（例如，新实例或修改了 uniform 的实例）
 * 3. 如果当前缓冲区不足，重新分配更大的共享 UBO
 * 4. 将共享 UBO 映射到 CPU 可访问的内存，准备写入 uniform 数据
 * 
 * 注意：这必须在提交所有材质实例之前发生。
 * 
 * @param driver 驱动 API 引用
 */
void UboManager::beginFrame(DriverApi& driver) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 性能追踪
    /**
     * 检查已完成的帧并相应地减少 GPU 计数
     */
    // Check finished frames and decrement GPU count accordingly.
    mFenceManager.reclaimCompletedResources(driver,
            [this](AllocationId id) { mAllocator.releaseGpu(id); });  // 释放 GPU 使用的分配

    /**
     * 实际合并槽位
     */
    // Actually merge the slots.
    mAllocator.releaseFreeSlots();  // 释放空闲槽位

    /**
     * 遍历所有材质实例，看看哪些需要槽位分配
     */
    // Traverse all MIs and see which of them need slot allocation.
    if (allocateOnDemand() == SUCCESS) {  // 如果按需分配成功
        /**
         * 不需要增长缓冲区，所以我们可以直接映射缓冲区进行写入并返回
         */
        // No need to grow the buffer, so we can just map the buffer for writing and return.
        mMemoryMappedBufferHandle = driver.mapBuffer(mUbHandle, 0, mUboSize, MapBufferAccessFlags::WRITE_BIT,
                "UboManager");  // 映射缓冲区

        return;  // 返回
    }

    /**
     * 计算所需大小并增长 UBO
     */
    // Calculate the required size and grow the Ubo.
    const allocation_size_t requiredSize = calculateRequiredSize();  // 计算所需大小
    reallocate(driver, requiredSize);  // 重新分配缓冲区

    /**
     * 在新 UBO 上为每个材质实例分配槽位
     */
    // Allocate slots for each MI on the new Ubo.
    allocateAllInstances();  // 分配所有实例

    /**
     * 映射缓冲区以便我们可以写入
     */
    // Map the buffer so that we can write to it
    mMemoryMappedBufferHandle =
            driver.mapBuffer(mUbHandle, 0, mUboSize, MapBufferAccessFlags::WRITE_BIT, "UboManager");  // 映射缓冲区

    /**
     * 使迁移的材质实例无效，以便下次 commit() 调用必须被触发
     */
    // Invalidate the migrated MIs, so that next commit() call must be triggered.
    for (const auto* mi : mManagedInstances) {  // 遍历所有管理的实例
        mi->getUniformBuffer().invalidate();  // 使 uniform 缓冲区无效
    }
}

/**
 * 完成开始帧
 * 
 * 取消映射缓冲区。
 * 
 * @param driver 驱动 API 引用
 */
void UboManager::finishBeginFrame(DriverApi& driver) {
    if (mMemoryMappedBufferHandle) {  // 如果缓冲区已映射
        driver.unmapBuffer(mMemoryMappedBufferHandle);  // 取消映射缓冲区
        mMemoryMappedBufferHandle.clear();  // 清除句柄
    }
}

/**
 * 结束帧
 * 
 * 创建栅栏并将其与一组分配 ID 关联。
 * 这些分配的 gpuUseCount 将增加，并在相应帧完成后减少。
 * 
 * @param driver 驱动 API 引用
 */
void UboManager::endFrame(DriverApi& driver) {
    /**
     * 创建分配 ID 容器，容量为管理的实例数量
     */
    auto allocationIds =
            FenceManager::AllocationIdContainer::with_capacity(mManagedInstances.size());  // 创建容器
    for (const auto* mi: mManagedInstances) {  // 遍历所有管理的实例
        const AllocationId id = mi->getAllocationId();  // 获取分配 ID
        if (UTILS_UNLIKELY(!BufferAllocator::isValid(id))) {  // 如果分配 ID 无效
            continue;  // 跳过
        }

        mAllocator.acquireGpu(id);  // 获取 GPU 使用
        allocationIds.push_back(id);  // 添加到列表
    }

    /**
     * 跟踪分配 ID
     */
    mFenceManager.track(driver, std::move(allocationIds));  // 跟踪分配 ID
}

/**
 * 终止 UBO 管理器
 * 
 * 清理所有资源。
 * 
 * @param driver 驱动 API 引用
 */
void UboManager::terminate(DriverApi& driver) {
    mFenceManager.reset(driver);  // 重置栅栏管理器
    driver.destroyBufferObject(mUbHandle);  // 销毁缓冲区对象
}

/**
 * 更新槽位
 * 
 * 将缓冲区描述符复制到内存映射缓冲区的指定偏移量。
 * 
 * @param driver 驱动 API 引用
 * @param id 分配 ID
 * @param bufferDescriptor 缓冲区描述符（会被移动）
 */
void UboManager::updateSlot(DriverApi& driver, AllocationId id,
        BufferDescriptor bufferDescriptor) const {
    if (!mMemoryMappedBufferHandle) {  // 如果缓冲区未映射
        return;  // 返回
    }

    const allocation_size_t offset = mAllocator.getAllocationOffset(id);  // 获取分配偏移量
    driver.copyToMemoryMappedBuffer(mMemoryMappedBufferHandle, offset, std::move(bufferDescriptor));  // 复制到内存映射缓冲区
}

/**
 * 管理材质实例
 * 
 * 注册新的材质实例到 UBO 管理器。
 * 
 * @param instance 材质实例指针
 */
void UboManager::manageMaterialInstance(FMaterialInstance* instance) {
    mPendingInstances.insert(instance);  // 添加到待处理实例集合
}

/**
 * 取消管理材质实例
 * 
 * 当材质实例被销毁时调用。
 * 
 * @param materialInstance 材质实例指针
 */
void UboManager::unmanageMaterialInstance(FMaterialInstance* materialInstance) {
    AllocationId id = materialInstance->getAllocationId();  // 获取分配 ID
    mPendingInstances.erase(materialInstance);  // 从待处理实例中移除
    mManagedInstances.erase(materialInstance);  // 从管理实例中移除

    if (!BufferAllocator::isValid(id)) {  // 如果分配 ID 无效
        return;  // 返回
    }

    mAllocator.retire(id);  // 回收分配
    materialInstance->assignUboAllocation(mUbHandle, BufferAllocator::UNALLOCATED, 0);  // 分配未分配状态
}

/**
 * 按需分配
 * 
 * 为需要槽位的材质实例分配槽位。
 * 
 * @return 分配结果（SUCCESS 或 REALLOCATION_REQUIRED）
 */
UboManager::AllocationResult UboManager::allocateOnDemand() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 性能追踪
    bool reallocationNeeded = false;  // 是否需要重新分配

    /**
     * 第一遍：为新的材质实例分配槽位（还没有槽位的）
     */
    // Pass 1: Allocate slots for new material instances (that don't have a slot yet).
    for (auto* mi : mPendingInstances) {  // 遍历待处理实例
        mManagedInstances.insert(mi);  // 添加到管理实例
        auto [newId, newOffset] = mAllocator.allocate(mi->getUniformBuffer().getSize());  // 分配槽位

        /**
         * 即使 newId 无效，我们也将其分配给材质实例，以便后续过程知道
         * 这个材质实例没有成功分配。然后我们可以正确计算新的所需 UBO 大小。
         */
        // Even if the newId is not valid, we assign it to the MI so that the following process knows
        // this material instance was not allocated successfully. Then we can calculate the new
        // required UBO size properly.
        mi->assignUboAllocation(mUbHandle, newId, newOffset);  // 分配 UBO 分配

        if (!BufferAllocator::isValid(newId)) {  // 如果分配 ID 无效
            reallocationNeeded = true;  // 标记需要重新分配
        }
    }
    mPendingInstances.clear();  // 清除待处理实例

    /**
     * 第二遍：为需要被孤立的现有材质实例分配槽位
     */
    // Pass 2: Allocate slots for existing material instances that need to be orphaned.
    for (auto* mi: mManagedInstances) {  // 遍历管理实例
        if (!BufferAllocator::isValid(mi->getAllocationId())) {  // 如果分配 ID 无效
            continue;  // 跳过
        }

        /**
         * 这个实例不需要孤立
         */
        // This instance doesn't need orphaning.
        if (!mi->getUniformBuffer().isDirty() || !mAllocator.isLockedByGpu(mi->getAllocationId())) {  // 如果不脏或未被 GPU 锁定
            continue;  // 跳过
        }

        mAllocator.retire(mi->getAllocationId());  // 回收旧分配

        /**
         * 如果空间已经不足，我们不需要再次尝试分配
         */
        // If the space is already not sufficient, we don't need to give another try on allocation.
        if (reallocationNeeded) {  // 如果需要重新分配
            mi->assignUboAllocation(mUbHandle, REALLOCATION_REQUIRED, 0);  // 分配重新分配所需状态
            continue;  // 继续下一个
        }

        auto [newId, newOffset] = mAllocator.allocate(mi->getUniformBuffer().getSize());  // 分配新槽位

        /**
         * 即使 newId 无效，我们也将其分配给材质实例，以便后续过程知道
         * 这个材质实例没有成功分配。然后我们可以正确计算新的所需 UBO 大小。
         */
        // Even if the newId is not valid, we assign it to the MI so that the following process knows
        // this material instance was not allocated successfully. Then we can calculate the new
        // required UBO size properly.
        mi->assignUboAllocation(mUbHandle, newId, newOffset);  // 分配 UBO 分配

        if (!BufferAllocator::isValid(newId)) {  // 如果分配 ID 无效
            reallocationNeeded = true;  // 标记需要重新分配
        }
    }

    return reallocationNeeded ? REALLOCATION_REQUIRED : SUCCESS;  // 返回结果
}

/**
 * 分配所有实例
 * 
 * 为所有管理的材质实例分配槽位。
 * 这在新缓冲区分配后调用。
 */
void UboManager::allocateAllInstances() {
    for (auto* mi: mManagedInstances) {  // 遍历所有管理实例
        auto [newId, newOffset] = mAllocator.allocate(mi->getUniformBuffer().getSize());  // 分配槽位
        assert_invariant(BufferAllocator::isValid(newId));  // 断言分配成功
        mi->assignUboAllocation(mUbHandle, newId, newOffset);  // 分配 UBO 分配
    }
}

/**
 * 获取总大小
 * 
 * 返回实际 UBO 的大小。
 * 注意：当分配失败时，它将在下一帧重新分配到更大的大小。
 * 
 * @return UBO 总大小（字节）
 */
allocation_size_t UboManager::getTotalSize() const noexcept {
    return mUboSize;  // 返回 UBO 大小
}

/**
 * 获取分配偏移量
 * 
 * 通过分配 ID 查询偏移量。
 * 
 * @param id 分配 ID
 * @return 分配偏移量（字节）
 */
allocation_size_t UboManager::getAllocationOffset(AllocationId id) const {
    return mAllocator.getAllocationOffset(id);  // 返回分配偏移量
}

/**
 * 重新分配缓冲区
 * 
 * 创建新的缓冲区对象并重置分配器。
 * 
 * @param driver 驱动 API 引用
 * @param requiredSize 所需大小（字节）
 */
void UboManager::reallocate(DriverApi& driver, allocation_size_t requiredSize) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 性能追踪
    if (mUbHandle) {  // 如果已有缓冲区句柄
        driver.destroyBufferObject(mUbHandle);  // 销毁旧缓冲区
    }

    mFenceManager.reset(driver);  // 重置栅栏管理器
    mAllocator.reset(requiredSize);  // 重置分配器
    mUboSize = requiredSize;  // 设置 UBO 大小
    /**
     * 创建新的缓冲区对象
     * 
     * 参数：
     * - requiredSize: 缓冲区大小
     * - BufferObjectBinding::UNIFORM: 绑定类型（Uniform）
     * - BufferUsage::DYNAMIC | BufferUsage::SHARED_WRITE_BIT: 使用方式（动态和共享写入）
     */
    mUbHandle = driver.createBufferObject(requiredSize, BufferObjectBinding::UNIFORM,
            BufferUsage::DYNAMIC | BufferUsage::SHARED_WRITE_BIT);  // 创建缓冲区对象
}

/**
 * 计算所需大小
 * 
 * 根据所有管理的材质实例计算所需的缓冲区大小。
 * 
 * @return 所需缓冲区大小（字节，已对齐）
 */
allocation_size_t UboManager::calculateRequiredSize() {
    allocation_size_t newBufferSize = 0;  // 新缓冲区大小
    for (const auto* mi: mManagedInstances) {  // 遍历所有管理实例
        const AllocationId allocationId = mi->getAllocationId();  // 获取分配 ID
        if (allocationId == BufferAllocator::REALLOCATION_REQUIRED) {  // 如果需要重新分配
            /**
             * 对于参数已更新的材质实例，除了它正在被 GPU 占用的槽位，
             * 我们需要为它保留一个额外的槽位。
             */
            // For MIs whose parameters have been updated, aside from the slot it is being
            // occupied by the GPU, we need to preserve an additional slot for it.
            newBufferSize += 2 * mAllocator.alignUp(mi->getUniformBuffer().getSize());  // 添加两倍大小
        } else {  // 否则
            newBufferSize += mAllocator.alignUp(mi->getUniformBuffer().getSize());  // 添加一倍大小
        }
    }
    /**
     * 返回对齐后的大小，乘以增长倍数
     */
    return mAllocator.alignUp(newBufferSize * BUFFER_SIZE_GROWTH_MULTIPLIER);  // 返回对齐后的大小
}

} // namespace filament
