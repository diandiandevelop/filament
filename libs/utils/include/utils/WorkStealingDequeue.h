/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_UTILS_WORKSTEALINGDEQUEUE_H
#define TNT_UTILS_WORKSTEALINGDEQUEUE_H

#include <atomic>
#include <type_traits>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

namespace utils {

/*
 * A templated, lockless, fixed-size work-stealing dequeue
 *
 *
 *     top                          bottom
 *      v                             v
 *      |----|----|----|----|----|----|
 *    steal()                      push(), pop()
 *  any thread                     main thread
 *
 * References:
 * - This code is largely inspired from
 *   https://blog.molecular-matters.com/2015/09/25/job-system-2-0-lock-free-work-stealing-part-3-going-lock-free/
 * - other implementations
 *   https://github.com/ConorWilliams/ConcurrentDeque/blob/main/include/riften/deque.hpp
 *   https://github.com/ssbl/concurrent-deque/blob/master/include/deque.hpp
 *   https://github.com/taskflow/work-stealing-queue/blob/master/wsq.hpp
 */
template <typename TYPE, size_t COUNT>
class WorkStealingDequeue {
    static_assert(!(COUNT & (COUNT - 1)), "COUNT must be a power of two");
    static constexpr size_t MASK = COUNT - 1;

    // mTop and mBottom must be signed integers. We use 64-bits atomics so we don't have
    // to worry about wrapping around.
    using index_t = int64_t;

    std::atomic<index_t> mTop    = { 0 };   // written/read in pop()/steal()
    std::atomic<index_t> mBottom = { 0 };   // written only in pop(), read in push(), steal()

    TYPE mItems[COUNT];

    // NOTE: it's not safe to return a reference because getItemAt() can be called
    // concurrently and the caller could std::move() the item unsafely.
    TYPE getItemAt(index_t index) noexcept { return mItems[index & MASK]; }

    void setItemAt(index_t index, TYPE item) noexcept { mItems[index & MASK] = item; }

public:
    using value_type = TYPE;

    inline void push(TYPE item) noexcept;
    inline TYPE pop() noexcept;
    inline TYPE steal() noexcept;

    size_t getSize() const noexcept { return COUNT; }

    // for debugging only...
    size_t getCount() const noexcept {
        index_t bottom = mBottom.load(std::memory_order_relaxed);
        index_t top = mTop.load(std::memory_order_relaxed);
        return bottom - top;
    }
};

/**
 * 从队列底部添加元素
 * 
 * 这是主线程（Job 创建者）使用的操作。
 * 从底部添加可以保持 LIFO（后进先出）顺序，提高缓存局部性。
 * 
 * 线程安全：
 * - push() 只能从主线程调用（不与 pop() 并发）
 * - 但可能与 steal() 并发，需要同步
 * 
 * 内存顺序：
 * - load: relaxed（因为不与 pop() 并发）
 * - store: seq_cst（需要与 steal() 同步，且不能混合其他内存顺序）
 * 
 * @param item 要添加的元素
 */
template <typename TYPE, size_t COUNT>
void WorkStealingDequeue<TYPE, COUNT>::push(TYPE item) noexcept {
    /**
     * 读取当前底部索引
     * 
     * memory_order_relaxed 足够，因为：
     * - mBottom 只在 pop() 中写入，而 pop() 不与 push() 并发
     * - 这个 load 不需要从其他线程获取任何数据
     */
    index_t bottom = mBottom.load(std::memory_order_relaxed);
    
    /**
     * 将元素存储到队列中
     * 
     * 使用位掩码索引：index & MASK
     * 因为 COUNT 是 2 的幂，MASK = COUNT - 1
     * 这相当于 index % COUNT，但更高效
     */
    setItemAt(bottom, item);

    /**
     * 更新底部索引
     * 
     * 这里需要 memory_order_seq_cst 因为：
     * 1. 我们需要 release 刚刚 push 的元素给调用 steal() 的其他线程
     * 2. 但通常 seq_cst 不能与其他内存顺序混合
     * 3. 参考：https://plv.mpi-sws.org/scfix/paper.pdf
     */
    mBottom.store(bottom + 1, std::memory_order_seq_cst);
}

/**
 * 从队列底部移除元素
 * 
 * 这是主线程（Job 创建者）使用的操作。
 * 从底部移除保持 LIFO（后进先出）顺序，提高缓存局部性。
 * 
 * 线程安全：
 * - pop() 只能从主线程调用（不与 push() 并发）
 * - 但可能与 steal() 并发，需要处理竞争条件
 * 
 * 竞争条件处理：
 * - 当队列只有一个元素时，pop() 和 steal() 可能同时访问
 * - 使用 compare_exchange_strong 原子地解决竞争
 * 
 * @return 移除的元素，如果队列为空返回默认构造的元素
 */
template <typename TYPE, size_t COUNT>
TYPE WorkStealingDequeue<TYPE, COUNT>::pop() noexcept {
    /**
     * 原子地减少底部索引
     * 
     * fetch_sub 返回减少前的值，所以 bottom = 原值 - 1
     * 
     * memory_order_seq_cst 用于保证与 steal() 的顺序
     * 注意：这不是典型的 acquire/release 操作：
     * - 不是 acquire：mBottom 只在 push() 中写入，而 push() 不与 pop() 并发
     * - 不是 release：我们不向 steal() 发布任何数据
     * 
     * 问题：这能防止下面的 mTop load 被重排序到 fetch_sub 的 "store" 部分之前吗？
     * 希望可以。如果不行，我们需要完整的内存屏障。
     */
    index_t bottom = mBottom.fetch_sub(1, std::memory_order_seq_cst) - 1;

    /**
     * bottom 可能为 -1（如果从空队列 pop）
     * 这会在下面修正
     */
    assert( bottom >= -1 );

    /**
     * 读取顶部索引
     * 
     * memory_order_seq_cst 用于保证与 steal() 的顺序
     * 注意：这不是典型的 acquire 操作（其他线程的 mTop 写入不发布数据）
     */
    index_t top = mTop.load(std::memory_order_seq_cst);

    /**
     * 情况 1：队列不为空，且不是最后一个元素
     * 
     * 这是最常见的情况，直接返回元素
     */
    if (top < bottom) {
        // 队列不为空，且不是最后一个元素，直接返回（这是常见情况）
        return getItemAt(bottom);
    }

    /**
     * 情况 2：队列只有一个元素（top == bottom）
     * 
     * 这是竞争条件的关键情况：
     * - pop() 和 steal() 可能同时访问最后一个元素
     * - 需要通过原子操作解决竞争
     */
    TYPE item{};
    if (top == bottom) {
        // 我们刚刚取了最后一个元素
        item = getItemAt(bottom);

        /**
         * 解决竞争条件
         * 
         * 因为我们知道这是最后一个元素，可能与 steal() 竞争
         * （最后一个元素同时在队列的顶部和底部）
         * 
         * 解决方法：我们也从自己这里"窃取"这个元素
         * 如果成功，说明并发的 steal() 会失败
         */
        if (mTop.compare_exchange_strong(top, top + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            /**
             * 成功：我们从自己这里窃取了最后一个元素
             * 这意味着并发的 steal() 会失败
             * mTop 现在等于 top + 1，我们调整 top 使队列为空
             */
            top++;
        } else {
            /**
             * 失败：mTop 不等于 top，说明元素在我们脚下被窃取了
             * `top` 现在等于 mTop
             * 简单地丢弃我们刚刚 pop 的元素
             * 队列现在为空
             */
            item = TYPE();
        }
    } else {
        /**
         * 情况 3：队列为空（top > bottom）
         * 
         * 这可能是因为在我们读取 mTop 之前，元素被窃取了
         * 我们会在下面调整 mBottom
         */
        assert(top - bottom == 1);
    }

    /**
     * 调整底部索引
     * 
     * 这里只需要 memory_order_relaxed，因为我们不发布任何数据
     * 没有对 mBottom 的并发写入，写入 mBottom 总是安全的
     * 但是，通常 seq_cst 不能与其他内存顺序混合，所以必须使用 seq_cst
     * 参考：https://plv.mpi-sws.org/scfix/paper.pdf
     */
    mBottom.store(top, std::memory_order_seq_cst);
    return item;
}

/**
 * 从队列顶部窃取元素
 * 
 * 这是工作线程（工作窃取者）使用的操作。
 * 从顶部窃取保持 FIFO（先进先出）顺序，有助于负载均衡。
 * 
 * 线程安全：
 * - 可以与 steal()、push() 或 pop() 并发调用
 * - 使用原子操作保证线程安全
 * 
 * 算法关键点：
 * - mTop 必须在 mBottom 之前读取（在其他线程中观察到的顺序）
 * - 使用 compare_exchange_strong 原子地获取元素
 * 
 * 返回值：
 * - 成功：返回窃取的元素
 * - 失败：返回默认构造的元素（队列为空或竞争失败）
 * 
 * @return 窃取的元素，如果失败返回默认构造的元素
 */
template <typename TYPE, size_t COUNT>
TYPE WorkStealingDequeue<TYPE, COUNT>::steal() noexcept {
    while (true) {
        /**
         * 算法关键点：mTop 必须在 mBottom 之前读取
         * 
         * 这确保了在其他线程中观察到的顺序一致性
         * 这是算法正确性的关键
         */

        /**
         * 读取顶部索引
         * 
         * memory_order_seq_cst 用于保证与 pop() 的顺序
         * 注意：这不是典型的 acquire 操作（其他线程的 mTop 写入不发布数据）
         */
        index_t top = mTop.load(std::memory_order_seq_cst);

        /**
         * 读取底部索引
         * 
         * memory_order_acquire 需要，因为我们正在获取 push() 中发布的元素
         * memory_order_seq_cst 用于保证与 pop() 的顺序
         */
        index_t bottom = mBottom.load(std::memory_order_seq_cst);

        /**
         * 检查队列是否为空
         * 
         * 如果 top >= bottom，队列为空，返回空元素
         */
        if (top >= bottom) {
            // 队列为空
            return TYPE();
        }

        /**
         * 队列不为空，尝试窃取元素
         * 
         * 1. 读取顶部元素（可能被其他线程修改）
         * 2. 使用 compare_exchange_strong 原子地增加 mTop
         * 3. 如果成功，返回元素
         * 4. 如果失败，重试（元素可能被 pop() 或另一个 steal() 取走）
         */
        // 队列不为空
        TYPE item(getItemAt(top));
        
        /**
         * 原子地增加顶部索引
         * 
         * compare_exchange_strong 保证：
         * - 如果 mTop == top，将其设置为 top + 1，返回 true
         * - 如果 mTop != top，将 top 设置为 mTop，返回 false
         */
        if (mTop.compare_exchange_strong(top, top + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            /**
             * 成功：我们窃取了一个元素，直接返回
             */
            return item;
        }
        /**
         * 失败：我们刚刚尝试窃取的元素在我们脚下被 pop() 了
         * 简单地丢弃它；无需操作——重试即可
         * 
         * 但是，item 可能已损坏，所以它必须是平凡可析构的
         */
        static_assert(std::is_trivially_destructible_v<TYPE>);
    }
}


} // namespace utils

#endif // TNT_UTILS_WORKSTEALINGDEQUEUE_H
