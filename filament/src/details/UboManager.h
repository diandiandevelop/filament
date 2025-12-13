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

#ifndef TNT_FILAMENT_DETAILS_UBOMANAGER_H
#define TNT_FILAMENT_DETAILS_UBOMANAGER_H

#include "backend/DriverApiForward.h"

#include "details/BufferAllocator.h"

#include <backend/Handle.h>
#include <private/backend/DriverApi.h>

#include <functional>
#include <unordered_set>
#include <vector>

class UboManagerTest;

namespace filament {

class FMaterial;
class FMaterialInstance;

/**
 * UBO 管理器
 * 
 * 管理统一缓冲区对象（UBO）的分配和生命周期。
 * 使用 BufferAllocator 在共享 UBO 中分配槽位，支持动态调整大小。
 * 
 * 重要：此类不是线程安全的，设计为在单线程上使用。
 * 
 * 实现细节：
 * - 内部管理分配器（mAllocator），没有同步原语
 * - 分配器本身也不是线程安全的
 * - 从多个线程并发访问同一个 UboManager 实例会导致数据竞争和未定义行为
 * 
 * 生命周期管理：
 * - beginFrame(): 开始帧，回收不再使用的槽位，分配新槽位，必要时重新分配更大的 UBO
 * - finishBeginFrame(): 完成帧开始，取消映射缓冲区
 * - endFrame(): 结束帧，创建栅栏并关联分配 ID
 * - terminate(): 终止，清理所有资源
 */
class UboManager {
public:
    /**
     * 栅栏管理器
     * 
     * 此工具类跟踪跨多帧正在被 GPU 使用的资源。
     * 它使用后端栅栏来确定 GPU 何时完成对一组资源的使用，
     * 允许它们被安全地回收或重用。
     * 
     * 典型用法：
     * - 在帧结束时调用 `track()` 跟踪一组资源
     * - 在将来帧开始时调用 `reclaimCompletedResources()` 释放已完成 GPU 工作的资源
     * 
     * 此类设计为单线程访问。
     */
    class FenceManager {
    public:
        using AllocationId = BufferAllocator::AllocationId;
        using AllocationIdContainer = utils::FixedCapacityVector<AllocationId>;

        FenceManager() = default;
        ~FenceManager() = default;

        FenceManager(FenceManager const&) = delete;
        FenceManager(FenceManager&&) = delete;


        /**
         * 跟踪分配 ID
         * 
         * 创建新栅栏以跟踪当前帧的一组分配 ID。
         * 这标记了 GPU 开始使用这些资源的时刻。
         * 
         * @param driver 驱动 API 引用
         * @param allocationIds 分配 ID 容器（会被移动）
         */
        void track(backend::DriverApi& driver, AllocationIdContainer&& allocationIds);

        /**
         * 回收已完成的资源
         * 
         * 检查所有跟踪的栅栏，并为与已完成栅栏关联的资源调用回调。
         * 这应该在每帧开始时调用一次。
         * 
         * @param driver 驱动 API 引用
         * @param onReclaimed 回收回调函数（参数为分配 ID）
         */
        void reclaimCompletedResources(backend::DriverApi& driver,
                std::function<void(AllocationId)> const& onReclaimed);

        /**
         * 重置
         * 
         * 销毁所有跟踪的栅栏并清空跟踪列表。
         * 这用于在终止或重大重新分配期间进行清理。
         * 
         * @param driver 驱动 API 引用
         */
        void reset(backend::DriverApi& driver);

    private:
        // Not ideal, but we need to know which slots to decrement gpuUseCount for each frame.
        using FenceAndAllocations =
                std::pair<backend::Handle<backend::HwFence>, AllocationIdContainer>;
        std::vector<FenceAndAllocations> mFenceAllocationList;
    };

    /**
     * 构造函数
     * 
     * @param driver 驱动 API 引用
     * @param defaultSlotSizeInBytes 默认槽位大小（字节）
     * @param defaultTotalSizeInBytes 默认总大小（字节）
     */
    explicit UboManager(backend::DriverApi& driver,
            BufferAllocator::allocation_size_t defaultSlotSizeInBytes,
            BufferAllocator::allocation_size_t defaultTotalSizeInBytes);

    UboManager(UboManager const&) = delete;  // 禁止拷贝
    UboManager(UboManager&&) = delete;  // 禁止移动

    /**
     * 开始帧
     * 
     * 此方法管理 UBO 分配生命周期的大部分，包括：
     * 1. 释放前一帧中不再被 GPU 使用的 UBO 槽位
     * 2. 为需要它们的 MaterialInstance 分配新槽位（例如，新实例或修改了 uniform 的实例）
     * 3. 如果当前 UBO 不足，重新分配更大的共享 UBO
     * 4. 将共享 UBO 映射到 CPU 可访问的内存，准备写入 uniform 数据
     * 
     * 注意：这必须在提交所有 MI 之前发生。
     * 
     * @param driver 驱动 API 引用
     */
    void beginFrame(backend::DriverApi& driver);

    /**
     * 完成帧开始
     * 
     * 取消映射缓冲区。
     * 
     * @param driver 驱动 API 引用
     */
    void finishBeginFrame(backend::DriverApi& driver);

    /**
     * 结束帧
     * 
     * 创建栅栏并将其与一组分配 ID 关联。
     * 这些分配的 gpuUseCount 将被递增，并在相应帧完成后递减。
     * 
     * @param driver 驱动 API 引用
     */
    void endFrame(backend::DriverApi& driver);

    /**
     * 终止
     * 
     * 清理所有资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver);

    /**
     * 更新槽位
     * 
     * 更新指定分配 ID 的槽位数据。
     * 
     * @param driver 驱动 API 引用
     * @param id 分配 ID
     * @param bufferDescriptor 缓冲区描述符
     */
    void updateSlot(backend::DriverApi& driver, BufferAllocator::AllocationId id,
            backend::BufferDescriptor bufferDescriptor) const;

    /**
     * 管理材质实例
     * 
     * 调用此方法以向 UboManager 注册新的材质实例。
     * 
     * @param instance 材质实例指针
     */
    void manageMaterialInstance(FMaterialInstance* instance);

    /**
     * 取消管理材质实例
     * 
     * 当材质实例被销毁时调用此方法。
     * 
     * @param materialInstance 材质实例指针
     */
    void unmanageMaterialInstance(FMaterialInstance* materialInstance);

    /**
     * 获取总大小
     * 
     * 返回实际 UBO 的大小。
     * 注意：当分配失败时，它将在下一帧重新分配到更大的大小。
     * 
     * @return UBO 总大小（字节）
     */
    [[nodiscard]] BufferAllocator::allocation_size_t getTotalSize() const noexcept;

    // For testing
    [[nodiscard]] backend::MemoryMappedBufferHandle getMemoryMappedBufferHandle() const noexcept {
        return mMemoryMappedBufferHandle;
    }

private:
    friend class ::UboManagerTest;

    constexpr static float BUFFER_SIZE_GROWTH_MULTIPLIER = 1.5f;

    enum AllocationResult {
        SUCCESS,
        REALLOCATION_REQUIRED
    };

    // Query the offset by the allocation id.
    [[nodiscard]] BufferAllocator::allocation_size_t getAllocationOffset(
            BufferAllocator::AllocationId id) const;

    AllocationResult allocateOnDemand();

    void allocateAllInstances();

    void reallocate(backend::DriverApi& driver, BufferAllocator::allocation_size_t requiredSize);

    BufferAllocator::allocation_size_t calculateRequiredSize();

    backend::Handle<backend::HwBufferObject> mUbHandle;
    backend::MemoryMappedBufferHandle mMemoryMappedBufferHandle;
    BufferAllocator::allocation_size_t mUboSize{};
    std::unordered_set<FMaterialInstance*> mPendingInstances;
    std::unordered_set<FMaterialInstance*> mManagedInstances;

    FenceManager mFenceManager;
    BufferAllocator mAllocator;
};

} // namespace filament

#endif
