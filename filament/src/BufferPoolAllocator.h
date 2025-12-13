/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BUFFERPOOLALLOCATOR_H
#define TNT_FILAMENT_BUFFERPOOLALLOCATOR_H

#include <utils/Allocator.h>
#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>

#include <memory>
#include <utility>

#include <stdint.h>
#include <stdlib.h>

namespace filament {

/**
 * 缓冲区池分配器
 * 
 * 一个简单的缓冲区池分配器。池有固定大小，池中持有的所有缓冲区大小相同，
 * 大小由最近的最大分配请求定义——因此池中的缓冲区只能增长，除非重置池。
 * 池中的所有缓冲区都知道自己的大小。
 * 
 * 特性：
 * - 固定大小的池（POOL_SIZE）
 * - 所有缓冲区大小相同（由最大请求决定）
 * - 缓冲区只能增长，除非重置
 * - 每个缓冲区存储自己的大小信息
 * 
 * @tparam POOL_SIZE 池大小（最大缓冲区数量）
 * @tparam ALIGNMENT 对齐要求（默认最大对齐）
 * @tparam AllocatorPolicy 分配器策略（默认堆分配器）
 * @tparam LockingPolicy 锁定策略（默认无锁）
 */
template<size_t POOL_SIZE,
        size_t ALIGNMENT = alignof(std::max_align_t),
        typename AllocatorPolicy = utils::HeapAllocator,
        typename LockingPolicy = utils::LockingPolicy::NoLock>
class BufferPoolAllocator {
public:
    using size_type = uint32_t;

    BufferPoolAllocator() = default;

    // pool is not copyable
    BufferPoolAllocator(BufferPoolAllocator const& rhs) = delete;
    BufferPoolAllocator& operator=(BufferPoolAllocator const& rhs) = delete;

    // pool is movable
    BufferPoolAllocator(BufferPoolAllocator&& rhs) noexcept = default;
    BufferPoolAllocator& operator=(BufferPoolAllocator&& rhs) noexcept = default;

    // free all buffers in the pool
    ~BufferPoolAllocator() noexcept;

    /**
     * 获取缓冲区
     * 
     * 返回至少 size 字节的缓冲区。如果请求的大小大于池中的缓冲区大小，
     * 会清空池并重新分配更大的缓冲区。
     * 
     * @param size 请求的缓冲区大小（字节）
     * @return 缓冲区指针，如果池为空则分配新缓冲区
     */
    void* get(size_type size) noexcept;

    /**
     * 归还缓冲区到池
     * 
     * 将缓冲区返回到池中。如果缓冲区小于当前池大小或池已满，则释放缓冲区。
     * 
     * @param buffer 要归还的缓冲区指针
     */
    void put(void* buffer) noexcept;

    /**
     * 重置池
     * 
     * 清空池并将缓冲区大小重置为 0。
     */
    void reset() noexcept;

private:
    /**
     * 缓冲区头
     * 
     * 存储在缓冲区之前，包含缓冲区的大小信息。
     */
    struct alignas(ALIGNMENT) Header {
        size_type size;  // 缓冲区大小（不包括 Header）
    };

    /**
     * 分配对齐常量
     * 
     * 将分配大小向上舍入到 4KB 的倍数，以减少 malloc 调用。
     */
    static constexpr size_t ALLOCATION_ROUNDING = 4096;

    /**
     * 释放缓冲区
     * 
     * 减少未归还缓冲区计数并释放内存。
     * 
     * @param p 缓冲区头指针
     */
    void deallocate(Header const* p) noexcept {
        --mOutstandingBuffers;
        mAllocator.free((void*)p, p->size + sizeof(Header));
    }

    /**
     * 清空池内部实现
     * 
     * 释放池中的所有缓冲区并清空条目列表。
     */
    void clearInternal() noexcept {
        for (auto p: mEntries) {
            assert_invariant(p->size == mSize);
            deallocate(p);
        }
        mEntries.clear();
    }

    using Container = utils::FixedCapacityVector<Header*, std::allocator<Header*>, false>;
    size_type mSize = 0;  // 池中缓冲区的大小
    size_type mOutstandingBuffers = 0;  // 未归还的缓冲区数量
    Container mEntries = Container::with_capacity(POOL_SIZE);  // 池条目（空闲缓冲区）
    AllocatorPolicy mAllocator;  // 分配器
    LockingPolicy mLock;  // 锁（用于线程安全）
};

template<size_t POOL_SIZE, size_t ALIGNMENT, typename AllocatorPolicy, typename LockingPolicy>
BufferPoolAllocator<POOL_SIZE, ALIGNMENT, AllocatorPolicy, LockingPolicy>::~BufferPoolAllocator() noexcept {
    clearInternal();
}

template<size_t POOL_SIZE, size_t ALIGNMENT, typename AllocatorPolicy, typename LockingPolicy>
void BufferPoolAllocator<POOL_SIZE, ALIGNMENT, AllocatorPolicy, LockingPolicy>::reset() noexcept {
    std::lock_guard<LockingPolicy> guard(mLock);
    clearInternal();
    mSize = 0;
}

template<size_t POOL_SIZE, size_t ALIGNMENT, typename AllocatorPolicy, typename LockingPolicy>
void* BufferPoolAllocator<POOL_SIZE, ALIGNMENT, AllocatorPolicy, LockingPolicy>::get(size_type const size) noexcept {
    std::lock_guard<LockingPolicy> guard(mLock);

    /**
     * 如果请求的大小大于池中的缓冲区大小，清空池
     */
    if (UTILS_UNLIKELY(size > mSize)) {
        clearInternal();  // 释放所有缓冲区
        /**
         * 向上舍入到 4KB 分配，以减少 malloc 调用。
         */
        size_t const roundedSize = ((size + sizeof(Header)) + (ALLOCATION_ROUNDING - 1)) & ~(ALLOCATION_ROUNDING - 1);
        mSize = roundedSize - sizeof(Header);  // 记录新的缓冲区大小
        assert_invariant(mSize >= size);
    }

    /**
     * 如果池为空，分配一个新的缓冲区（大小为池缓冲区大小，可能大于请求的大小）
     */
    if (UTILS_UNLIKELY(mEntries.empty())) {
        ++mOutstandingBuffers;
        Header* p = static_cast<Header*>(mAllocator.alloc(mSize + sizeof(Header), ALIGNMENT));
        p->size = mSize;
        return p + 1;  // 返回缓冲区数据部分（跳过 Header）
    }

    /**
     * 如果池中有条目，我们知道它至少是请求的大小
     */
    assert_invariant(mSize >= size);
    /**
     * 返回最后一个条目（LIFO）
     */
    Header* p = mEntries.back();
    mEntries.pop_back();
    return p + 1;  // 返回缓冲区数据部分（跳过 Header）
}

template<size_t POOL_SIZE, size_t ALIGNMENT, typename AllocatorPolicy, typename LockingPolicy>
void BufferPoolAllocator<POOL_SIZE, ALIGNMENT, AllocatorPolicy, LockingPolicy>::put(void* buffer) noexcept {
    std::lock_guard<LockingPolicy> guard(mLock);

    /**
     * 获取此缓冲区的头
     * 
     * 缓冲区指针之前是 Header，所以需要减 1。
     */
    Header const* const p = static_cast<Header const*>(buffer) - 1;

    /**
     * 如果返回的缓冲区小于当前池缓冲区大小，或池已满，直接释放该缓冲区
     */
    if (UTILS_UNLIKELY(mEntries.size() == mEntries.capacity() || p->size < mSize)) {
        deallocate(p);
        return;
    }

    /**
     * 将此缓冲区添加到池中
     */
    assert_invariant(p->size == mSize);
    mEntries.push_back(const_cast<Header *>(p));
}

} // namespace filament

#endif // TNT_FILAMENT_BUFFERPOOLALLOCATOR_H
