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

#include "details/BufferAllocator.h"

#include <private/utils/Tracing.h>
#include <utils/Panic.h>
#include <utils/debug.h>

namespace filament {
namespace {

/**
 * 检查是否为 2 的幂（仅在调试模式下）
 * 
 * @param n 要检查的数字
 * @return 如果是 2 的幂返回 true，否则返回 false
 */
#ifndef NDEBUG
constexpr static bool isPowerOfTwo(uint32_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);  // 2 的幂的特征：n & (n - 1) == 0
}
#endif

} // anonymous namespace

/**
 * 缓冲区分配器构造函数
 * 
 * 创建缓冲区分配器并初始化槽池。
 * 
 * @param totalSize 总大小（字节）
 * @param slotSize 槽大小（字节，必须是 2 的幂）
 */
BufferAllocator::BufferAllocator(allocation_size_t totalSize, allocation_size_t slotSize)
    : mTotalSize(totalSize),  // 初始化总大小
      mSlotSize(slotSize) {  // 初始化槽大小
    assert_invariant(mSlotSize > 0);  // 断言槽大小大于 0
    assert_invariant(isPowerOfTwo(mSlotSize));  // 断言槽大小是 2 的幂

    reset(mTotalSize);  // 重置分配器
}

/**
 * 重置分配器
 * 
 * 将分配器重置为初始状态，使用新的总大小。
 * 所有现有分配都被清除。
 * 
 * @param newTotalSize 新的总大小（必须是槽大小的倍数）
 */
void BufferAllocator::reset(allocation_size_t newTotalSize) {
    assert_invariant(newTotalSize % mSlotSize == 0);  // 断言新总大小是槽大小的倍数

    mTotalSize = newTotalSize;  // 更新总大小

    /**
     * 清空所有数据结构
     */
    mSlotPool.clear();  // 清空槽池
    mFreeList.clear();  // 清空空闲列表
    mOffsetMap.clear();  // 清空偏移映射

    /**
     * 使用单个大的空闲槽初始化池
     */
    // Initialize the pool with a single large free slot.
    mSlotPool.emplace_back(InternalSlotNode{
        .slot = {
            .offset = 0,  // 偏移量为 0
            .slotSize = mTotalSize,  // 槽大小等于总大小
            .isAllocated = false,  // 未分配
            .gpuUseCount = 0  // GPU 使用计数为 0
        }
    });

    InternalSlotNode* firstNode = &mSlotPool.front();  // 获取第一个节点

    /**
     * 将此初始空闲槽添加到空闲列表和偏移映射
     */
    // Add this initial free slot to the free list and offset map.
    auto freeListIter = mFreeList.emplace(newTotalSize, firstNode);  // 添加到空闲列表（按大小）
    auto offsetMapIter = mOffsetMap.emplace(0, firstNode);  // 添加到偏移映射（按偏移）

    firstNode->slotPoolIterator = mSlotPool.begin();  // 设置槽池迭代器
    firstNode->freeListIterator = freeListIter;  // 设置空闲列表迭代器
    firstNode->offsetMapIterator = offsetMapIter.first;  // 设置偏移映射迭代器
}

/**
 * 最佳适配分配槽（对齐到槽大小）
 * 
 * 如果返回的 ID 无效，表示没有足够大的槽用于分配。
 * 
 * @param size 请求的大小（字节）
 * @return 分配 ID 和槽偏移量的对
 */
// Best-fit allocate a slot (aligned to slotSize); may return REALLOCATION_REQUIRED if no space.
std::pair<BufferAllocator::AllocationId, BufferAllocator::allocation_size_t>
    BufferAllocator::allocate(allocation_size_t size) noexcept {
    if (size == 0) {  // 如果大小为 0
        return { UNALLOCATED, 0 };  // 返回未分配
    }

    const allocation_size_t alignedSize = alignUp(size);  // 对齐大小
    auto bestFitIter = mFreeList.lower_bound(alignedSize);  // 查找最佳适配（大于等于请求大小的最小槽）

    if (bestFitIter == mFreeList.end()) {  // 如果没有找到
        return { REALLOCATION_REQUIRED, 0 };  // 返回需要重新分配
    }

    InternalSlotNode* targetNode = bestFitIter->second;  // 获取目标节点
    const allocation_size_t originalSlotSize = targetNode->slot.slotSize;  // 保存原始槽大小

    /**
     * 从空闲列表中移除并标记为已分配
     */
    mFreeList.erase(bestFitIter);  // 从空闲列表移除
    targetNode->freeListIterator = mFreeList.end();  // 清空闲列表迭代器
    targetNode->slot.isAllocated = true;  // 标记为已分配

    /**
     * 如果槽大于所需大小，则分割槽
     */
    // Split the slot if it is larger than what we need.
    if (originalSlotSize > alignedSize) {  // 如果原始槽大小大于对齐大小
        targetNode->slot.slotSize = alignedSize;  // 更新槽大小

        allocation_size_t remainingSize = originalSlotSize - alignedSize;  // 计算剩余大小
        allocation_size_t newSlotOffset = targetNode->slot.offset + alignedSize;  // 计算新槽偏移
        assert_invariant(remainingSize % mSlotSize == 0);  // 断言剩余大小是槽大小的倍数
        assert_invariant(newSlotOffset % mSlotSize == 0);  // 断言新槽偏移是槽大小的倍数

        /**
         * 为剩余的空闲空间创建新节点
         */
        // Create a new node for the remaining free space.
        auto insertPos = std::next(targetNode->slotPoolIterator);  // 获取插入位置
        auto newNodeIter = mSlotPool.emplace(insertPos, InternalSlotNode{  // 插入新节点
            .slot = {
                .offset = newSlotOffset,  // 新槽偏移
                .slotSize = remainingSize,  // 剩余大小
                .isAllocated = false,  // 未分配
                .gpuUseCount = 0  // GPU 使用计数为 0
            }
        });
        InternalSlotNode* newNode = &(*newNodeIter);  // 获取新节点指针

        /**
         * 将新空闲槽添加到跟踪映射
         */
        // Add the new free slot to our tracking maps.
        auto freeListIter = mFreeList.emplace(remainingSize, newNode);  // 添加到空闲列表
        auto offsetMapIter = mOffsetMap.emplace(newSlotOffset, newNode);  // 添加到偏移映射
        newNode->slotPoolIterator = newNodeIter;  // 设置槽池迭代器
        newNode->freeListIterator = freeListIter;  // 设置空闲列表迭代器
        newNode->offsetMapIterator = offsetMapIter.first;  // 设置偏移映射迭代器
    }

    AllocationId allocationId = calculateIdByOffset(targetNode->slot.offset);  // 计算分配 ID
    return { allocationId, targetNode->slot.offset };  // 返回分配 ID 和偏移量
}

/**
 * 根据分配 ID 获取节点
 * 
 * @param id 分配 ID
 * @return 内部槽节点指针（如果无效则返回 nullptr）
 */
BufferAllocator::InternalSlotNode* BufferAllocator::getNodeById(
        AllocationId id) const noexcept {
    if (!isValid(id)) {  // 如果 ID 无效
        return nullptr;  // 返回空指针
    }

    allocation_size_t offset = getAllocationOffset(id);  // 获取分配偏移
    auto iter = mOffsetMap.find(offset);  // 在偏移映射中查找

    /**
     * 我们无法在映射中找到对应的节点
     */
    // We cannot find the corresponding node in the map.
    if (iter == mOffsetMap.end()) {  // 如果未找到
        return nullptr;  // 返回空指针
    }
    return iter->second;  // 返回节点指针
}

/**
 * 回收分配
 * 
 * 当 MaterialInstance 放弃分配的所有权时调用。
 * 即使槽未被使用，我们也不会在此函数中立即释放槽，
 * 释放集中在 releaseFreeSlots() 中。
 * 
 * @param id 分配 ID
 */
void BufferAllocator::retire(AllocationId id) {
    InternalSlotNode* targetNode = getNodeById(id);  // 获取节点
    assert_invariant(targetNode != nullptr);  // 断言节点有效

    Slot& slot = targetNode->slot;  // 获取槽引用
    slot.isAllocated = false;  // 标记为未分配
    if (slot.gpuUseCount == 0) {  // 如果 GPU 使用计数为 0
        mHasPendingFrees = true;  // 标记有待释放的槽
    }
}

/**
 * 获取 GPU 读取锁
 * 
 * 增加 GPU 读取锁计数。
 * 
 * @param id 分配 ID
 */
void BufferAllocator::acquireGpu(AllocationId id) {
    InternalSlotNode* targetNode = getNodeById(id);  // 获取节点
    assert_invariant(targetNode != nullptr);  // 断言节点有效

    targetNode->slot.gpuUseCount++;  // 增加 GPU 使用计数
}

/**
 * 释放 GPU 读取锁
 * 
 * 减少 GPU 读取锁计数。
 * 即使槽未被使用，我们也不会在此函数中立即释放槽，
 * 释放集中在 releaseFreeSlots() 中。
 * 
 * @param id 分配 ID
 */
void BufferAllocator::releaseGpu(AllocationId id) {
    InternalSlotNode* targetNode = getNodeById(id);  // 获取节点
    assert_invariant(targetNode != nullptr);  // 断言节点有效
    assert_invariant(targetNode->slot.gpuUseCount > 0);  // 断言 GPU 使用计数大于 0

    Slot& slot = targetNode->slot;  // 获取槽引用
    slot.gpuUseCount--;  // 减少 GPU 使用计数
    if (slot.gpuUseCount == 0 && !slot.isAllocated) {  // 如果 GPU 使用计数为 0 且未分配
        mHasPendingFrees = true;  // 标记有待释放的槽
    }
}

/**
 * 释放空闲槽
 * 
 * 遍历所有槽并释放既不被 CPU 也不被 GPU 使用的所有槽。
 * 同时执行合并操作。
 */
void BufferAllocator::releaseFreeSlots() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用
    if (!mHasPendingFrees) {  // 如果没有待释放的槽
        return;  // 直接返回
    }

    auto curr = mSlotPool.begin();  // 从槽池开始遍历
    while (curr != mSlotPool.end()) {  // 遍历所有槽
        if (!curr->slot.isFree()) {  // 如果槽不是空闲的
            ++curr;  // 移动到下一个
            continue;  // 继续循环
        }

        auto next = std::next(curr);  // 获取下一个槽
        bool merged = false;  // 是否合并标志
        /**
         * 合并连续的空闲槽
         */
        while (next != mSlotPool.end() && next->slot.isFree()) {  // 如果下一个槽也是空闲的
            merged = true;  // 标记为已合并
            /**
             * 合并空闲槽的大小
             */
            // Combine the size of free slots
            curr->slot.slotSize += next->slot.slotSize;  // 增加槽大小
            assert_invariant(curr->slot.slotSize % mSlotSize == 0);  // 断言槽大小是槽大小的倍数

            /**
             * 从所有映射中擦除被合并的槽
             */
            // Erase the merged slot from all maps
            if (next->freeListIterator != mFreeList.end()) {  // 如果空闲列表迭代器有效
                mFreeList.erase(next->freeListIterator);  // 从空闲列表移除
            }
            mOffsetMap.erase(next->offsetMapIterator);  // 从偏移映射移除
            next = mSlotPool.erase(next);  // 从槽池移除
        }

        /**
         * 如果我们执行了任何合并，当前块的大小已更改。
         * 我们需要更新它在 mFreeList 中的位置。
         */
        // If we performed any merge, the current block's size has changed.
        // We need to update its position in the mFreeList.
        if (curr->freeListIterator != mFreeList.end()) {  // 如果已在空闲列表中
            /**
             * 如果它已在空闲列表中且我们合并了，我们需要更新它
             */
            // If it's already in the free list and we merged, we need to update it.
            if (merged) {  // 如果合并了
                mFreeList.erase(curr->freeListIterator);  // 从空闲列表移除
                curr->freeListIterator = mFreeList.emplace(curr->slot.slotSize, &(*curr));  // 重新插入
            }
        } else {  // 如果不在空闲列表中
            /**
             * 如果它不在空闲列表中，它必须是一个新释放的块。添加它。
             */
            // If it's not in the free list, it must be a newly freed block. Add it.
            curr->freeListIterator = mFreeList.emplace(curr->slot.slotSize, &(*curr));  // 添加到空闲列表
        }

        curr = next;  // 移动到下一个槽
    }
    mHasPendingFrees = false;  // 清除待释放标志
}

/**
 * 获取总大小
 * 
 * @return UBO 的总大小（字节）
 */
BufferAllocator::allocation_size_t BufferAllocator::getTotalSize() const noexcept {
    return mTotalSize;  // 返回总大小
}

/**
 * 根据分配 ID 查询分配偏移
 * 
 * @param id 分配 ID
 * @return 分配偏移（字节）
 */
BufferAllocator::allocation_size_t
    BufferAllocator::getAllocationOffset(AllocationId id) const {
    assert_invariant(isValid(id));  // 断言 ID 有效

    return (id - 1) * mSlotSize;  // 计算偏移（ID 是 1-based）
}

/**
 * 检查是否被 GPU 锁定
 * 
 * @param id 分配 ID
 * @return 如果被 GPU 锁定返回 true，否则返回 false
 */
bool BufferAllocator::isLockedByGpu(AllocationId id) const {
    InternalSlotNode* targetNode = getNodeById(id);  // 获取节点
    assert_invariant(targetNode != nullptr);  // 断言节点有效

    return targetNode->slot.gpuUseCount > 0;  // 返回 GPU 使用计数是否大于 0
}

/**
 * 根据偏移计算分配 ID
 * 
 * @param offset 偏移（必须是槽大小的倍数）
 * @return 分配 ID（1-based，因为 0 用于 UNALLOCATED）
 */
BufferAllocator::AllocationId BufferAllocator::calculateIdByOffset(
        allocation_size_t offset) const {
    assert_invariant(offset % mSlotSize == 0);  // 断言偏移是槽大小的倍数

    /**
     * ID 是 1-based，因为我们使用 0 表示 UNALLOCATED
     */
    // The ID is 1-based since we use 0 for UNALLOCATED.
    return (offset / mSlotSize) + 1;  // 计算 ID
}

/**
 * 获取分配大小
 * 
 * @param id 分配 ID
 * @return 分配大小（字节）
 */
BufferAllocator::allocation_size_t BufferAllocator::getAllocationSize(AllocationId id) const {
    InternalSlotNode* targetNode = getNodeById(id);  // 获取节点
    assert_invariant(targetNode != nullptr);  // 断言节点有效

    return targetNode->slot.slotSize;  // 返回槽大小
}

/**
 * 检查分配 ID 是否有效
 * 
 * @param id 分配 ID
 * @return 如果有效返回 true，否则返回 false
 */
bool BufferAllocator::isValid(AllocationId id) {
    return id != UNALLOCATED && id != REALLOCATION_REQUIRED;  // 不是未分配且不是需要重新分配
}

/**
 * 向上对齐大小
 * 
 * 将大小向上对齐到槽大小的倍数。
 * 
 * @param size 要对齐的大小
 * @return 对齐后的大小
 */
BufferAllocator::allocation_size_t BufferAllocator::alignUp(
        allocation_size_t size) const noexcept {
    if (size == 0) return 0;  // 如果大小为 0，返回 0

    return (size + mSlotSize - 1) & ~(mSlotSize - 1);  // 向上对齐到槽大小的倍数
}

} // namespace filament
