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

#ifndef TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H
#define TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H

#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>

namespace filament {

/**
 * 缓冲区分配器类
 * 
 * 管理统一缓冲区对象（UBO）中的槽分配。
 * 使用最佳适配算法和槽合并来高效管理内存。
 * 
 * 注意：此类不是线程安全的。
 * 
 * 它在内部管理共享状态（例如 mSlotPool、mFreeList、mOffsetMap），
 * 没有任何同步原语。从多个线程并发访问同一个 BufferAllocator
 * 实例将导致数据竞争和未定义行为。
 * 
 * 如果此类的实例要在线程之间共享，所有对其成员函数的调用
 * 必须由外部同步保护（例如 std::mutex）。
 */
// This class is NOT thread-safe.
//
// It internally manages shared state (e.g., mSlotPool, mFreeList, mOffsetMap) without any
// synchronization primitives. Concurrent access from multiple threads to the same
// BufferAllocator instance will result in data races and undefined behavior.
//
// If an instance of this class is to be shared between threads, all calls to its member
// functions MUST be protected by external synchronization (e.g., a std::mutex).
class BufferAllocator {
public:
    using allocation_size_t = uint32_t;  // 分配大小类型
    using AllocationId = uint32_t;  // 分配 ID 类型

    static constexpr AllocationId UNALLOCATED = 0;  // 未分配 ID
    static constexpr AllocationId REALLOCATION_REQUIRED = ~0u;  // 需要重新分配 ID

    /**
     * 槽结构
     * 
     * 表示缓冲区中的一个槽，包含偏移、大小、分配状态和 GPU 使用计数。
     */
    struct Slot {
        const allocation_size_t offset;       // 偏移量（4 字节）
        allocation_size_t slotSize;           // 槽大小（4 字节）
        bool isAllocated;                     // 是否已分配（1 字节）
        char padding[3];                      // 填充（3 字节）
        uint32_t gpuUseCount;                 // GPU 使用计数（4 字节）

        /**
         * 检查槽是否空闲
         * 
         * @return 如果槽空闲（未分配且 GPU 使用计数为 0）返回 true，否则返回 false
         */
        [[nodiscard]] bool isFree() const noexcept {
            return !isAllocated && gpuUseCount == 0;  // 未分配且 GPU 使用计数为 0
        }
    };

    /**
     * 构造函数
     * 
     * `slotSize` 派生自 GPU 的统一缓冲区偏移对齐要求，最多可达 256 字节。
     * 
     * @param totalSize 总大小（字节）
     * @param slotSize 槽大小（字节，必须是 2 的幂）
     */
    // `slotSize` is derived from the GPU's uniform buffer offset alignment requirement,
    // which can be up to 256 bytes.
    explicit BufferAllocator(allocation_size_t totalSize,
            allocation_size_t slotSize);

    BufferAllocator(BufferAllocator const&) = delete;  // 禁止复制构造
    BufferAllocator(BufferAllocator&&) = delete;  // 禁止移动构造

    /**
     * 分配新槽
     * 
     * 分配新槽并返回其 ID 和 UBO 中的槽偏移量。
     * 如果返回的 ID 无效，表示没有足够大的槽用于分配。
     * 
     * @param size 请求的大小（字节）
     * @return 分配 ID 和槽偏移量的对
     */
    // Allocate a new slot and return its id and slot offset in the UBO.
    // If the returned id is not valid, that means there's no large enough slot for allocation.
    [[nodiscard]] std::pair<AllocationId, allocation_size_t> allocate(
            allocation_size_t size) noexcept;

    /**
     * 回收分配
     * 
     * 当 MaterialInstance 放弃分配的所有权时调用。
     * 即使槽未被使用，我们也不会在此函数中立即释放槽，
     * 释放集中在 releaseFreeSlots() 中。
     * 
     * @param id 分配 ID
     */
    // Call it when MaterialInstance gives up the ownership of the allocation.
    // We don't release the slot immediately in this function even if it is not being used,
    // the release is centralized in releaseFreeSlots().
    void retire(AllocationId id);

    /**
     * 获取 GPU 读取锁
     * 
     * 增加 GPU 读取锁计数。
     * 
     * @param id 分配 ID
     */
    // Increments the GPU read-lock.
    void acquireGpu(AllocationId id);

    /**
     * 释放 GPU 读取锁
     * 
     * 减少 GPU 读取锁计数。
     * 即使槽未被使用，我们也不会在此函数中立即释放槽，
     * 释放集中在 releaseFreeSlots() 中。
     * 
     * @param id 分配 ID
     */
    // Decrements the GPU read-lock.
    // We don't release the slot immediately in this function even if it is not being used,
    // the release is centralized in releaseFreeSlots().
    void releaseGpu(AllocationId id);

    /**
     * 释放空闲槽
     * 
     * 遍历所有槽并释放既不被 CPU 也不被 GPU 使用的所有槽。
     * 同时执行合并操作。
     */
    // Traverse all slots and free all slots that are not being used by both CPU and GPU.
    // Perform the merge at the same time.
    void releaseFreeSlots();

    /**
     * 重置分配器
     * 
     * 将分配器重置为初始状态，使用新的总大小。
     * 所有现有分配都被清除。
     * 
     * @param newTotalSize 新的总大小（必须是槽大小的倍数）
     */
    // Resets the allocator to its initial state with a new total size.
    // All existing allocations are cleared.
    void reset(allocation_size_t newTotalSize);

    /**
     * 获取总大小
     * 
     * @return UBO 的总大小（字节）
     */
    // Size of the UBO in bytes.
    [[nodiscard]] allocation_size_t getTotalSize() const noexcept;

    /**
     * 根据分配 ID 查询分配偏移
     * 
     * @param id 分配 ID
     * @return 分配偏移（字节）
     */
    // Query the allocation offset by AllocationId.
    [[nodiscard]] allocation_size_t getAllocationOffset(AllocationId id) const;

    /**
     * 检查是否被 GPU 锁定
     * 
     * @param id 分配 ID
     * @return 如果被 GPU 锁定返回 true，否则返回 false
     */
    [[nodiscard]] bool isLockedByGpu(AllocationId id) const;

    /**
     * 向上对齐大小
     * 
     * 将大小向上对齐到槽大小的倍数。
     * 
     * @param size 要对齐的大小
     * @return 对齐后的大小
     */
    [[nodiscard]] allocation_size_t alignUp(allocation_size_t size) const noexcept;

    /**
     * 获取分配大小
     * 
     * @param id 分配 ID
     * @return 分配大小（字节）
     */
    [[nodiscard]] allocation_size_t getAllocationSize(AllocationId id) const;

    /**
     * 检查分配 ID 是否有效
     * 
     * @param id 分配 ID
     * @return 如果有效返回 true，否则返回 false
     */
    [[nodiscard]] static bool isValid(AllocationId id);

private:
    /**
     * 根据偏移计算分配 ID
     * 
     * @param offset 偏移（必须是槽大小的倍数）
     * @return 分配 ID（1-based）
     */
    [[nodiscard]] AllocationId calculateIdByOffset(allocation_size_t offset) const;

    /**
     * 内部槽节点结构
     * 
     * 包含基础槽节点和附加信息（迭代器），用于在多个数据结构中跟踪节点。
     */
    // Having an internal node type holding the base slot node and additional information.
    struct InternalSlotNode {
        Slot slot;  // 槽数据
        std::list<InternalSlotNode>::iterator slotPoolIterator;  // 槽池迭代器
        std::multimap<allocation_size_t, InternalSlotNode*>::iterator freeListIterator;  // 空闲列表迭代器
        std::unordered_map<allocation_size_t, InternalSlotNode*>::iterator offsetMapIterator;  // 偏移映射迭代器
    };

    /**
     * 根据分配 ID 获取节点
     * 
     * @param id 分配 ID
     * @return 内部槽节点指针（如果无效则返回 nullptr）
     */
    [[nodiscard]] InternalSlotNode* getNodeById(AllocationId id) const noexcept;

    bool mHasPendingFrees = false;  // 是否有待释放的槽
    allocation_size_t mTotalSize;  // 总大小（字节）
    const allocation_size_t mSlotSize;  // 单个槽的大小（字节）
    std::list<InternalSlotNode> mSlotPool;  // 所有槽，包括已分配和已释放的
    std::multimap</*slot size*/allocation_size_t, InternalSlotNode*> mFreeList;  // 空闲列表（按槽大小排序）
    std::unordered_map</*slot offset*/allocation_size_t, InternalSlotNode*> mOffsetMap;  // 偏移映射（按槽偏移索引）
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H
