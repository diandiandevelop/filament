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

#ifndef TNT_FILAMENT_FRAMEINFO_H
#define TNT_FILAMENT_FRAMEINFO_H

#include <filament/Renderer.h>

#include <details/SwapChain.h>

#include <backend/Platform.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/backend/DriverApi.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/AsyncJobQueue.h>
#include <utils/FixedCapacityVector.h>

#include <array>
#include <atomic>
#include <chrono>
#include <iterator>
#include <ratio>
#include <type_traits>

#include <stdint.h>
#include <stddef.h>

namespace filament {
class FEngine;

namespace details {
/**
 * 帧信息（公共接口）
 * 
 * 提供给用户的帧信息结构，包含去噪后的帧时间。
 */
struct FrameInfo {
    using duration = std::chrono::duration<float, std::milli>;
    duration gpuFrameDuration{};     // GPU 帧周期
    duration denoisedFrameTime{};    // 去噪后的帧周期（中值滤波器）
    bool valid = false;              // 如果结构的数据有效则为 true
};
} // namespace details

/**
 * 帧信息实现（内部使用）
 * 
 * 包含完整的帧时间戳信息，用于内部跟踪和计算。
 */
struct FrameInfoImpl : public details::FrameInfo {
    using FrameTimestamps = backend::FrameTimestamps;
    using CompositorTiming = backend::CompositorTiming;
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    uint32_t frameId;  // 帧 ID
    time_point beginFrame{};  // 主线程 beginFrame 时间
    time_point endFrame{};  // 主线程 endFrame 时间
    time_point backendBeginFrame{};  // 后端线程 beginFrame 时间（makeCurrent 时间）
    time_point backendEndFrame{};  // 后端线程 endFrame 时间（present 时间）
    time_point gpuFrameComplete{};  // GPU 完成渲染的时间
    time_point vsync{};  // 垂直同步时间
    FrameTimestamps::time_point_ns displayPresent{ FrameTimestamps::PENDING };  // 此帧的实际呈现时间
    CompositorTiming::time_point_ns presentDeadline{ FrameTimestamps::INVALID };  // 排队帧的截止时间 [ns]
    CompositorTiming::duration_ns displayPresentInterval{ FrameTimestamps::INVALID };  // 显示刷新率 [ns]
    CompositorTiming::duration_ns compositionToPresentLatency{ FrameTimestamps::INVALID };  // 从合成开始到预期呈现时间的时间 [ns]
    FrameTimestamps::time_point_ns expectedPresentTime{ FrameTimestamps::INVALID };  // 系统预期的呈现时间 [ns]

    backend::FenceHandle fence{};  // 用于 gpuFrameComplete 的栅栏
    std::atomic_bool ready{};  // 一旦后端线程填充了数据则为 true
    
    explicit FrameInfoImpl(uint32_t const id) noexcept
        : frameId(id) {
    }

    ~FrameInfoImpl() noexcept {
        assert_invariant(!fence);
    }

    // 禁止拷贝
    FrameInfoImpl(FrameInfoImpl& rhs) noexcept = delete;
    FrameInfoImpl& operator=(FrameInfoImpl& rhs) noexcept = delete;
};

/**
 * 循环队列
 * 
 * 固定容量的循环队列，支持前向插入（push_front）和后向删除（pop_back）。
 * 
 * 特性：
 * - 固定容量（编译时确定）
 * - 循环缓冲区实现
 * - 支持非平凡类型的构造和析构
 * - 前向迭代器
 * 
 * @tparam T 元素类型
 * @tparam CAPACITY 队列容量
 */
template<typename T, size_t CAPACITY>
class CircularQueue {
public:
    using value_type = T;
    using reference = value_type&;
    using const_reference = value_type const&;

private:
    /**
     * 迭代器
     * 
     * 前向迭代器，支持从 iterator 到 const_iterator 的转换。
     */
    template<typename U>
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename std::remove_const<T>::type;
        using difference_type = std::ptrdiff_t;
        using pointer = U*;
        using reference = U&;
        using QueuePtr = typename std::conditional<std::is_const<U>::value,
                const CircularQueue*, CircularQueue*>::type;

        Iterator() = default;
        Iterator(QueuePtr queue, size_t pos) noexcept : mQueue(queue), mPos(pos) {}

        // allow conversion from iterator to const_iterator
        operator Iterator<const T>() const { return { mQueue, mPos }; }

        reference operator*() const { return (*mQueue)[mPos]; }
        pointer operator->() const { return &(*mQueue)[mPos]; }
        Iterator& operator++() { ++mPos; return *this; }
        Iterator operator++(int) { Iterator temp = *this; ++(*this); return temp; }

        friend bool operator==(const Iterator& a, const Iterator& b) { return a.mQueue == b.mQueue && a.mPos == b.mPos; }
        friend bool operator!=(const Iterator& a, const Iterator& b) { return !(a == b); }

    private:
        QueuePtr mQueue = nullptr;
        size_t mPos = 0;
    };

public:
    using iterator = Iterator<T>;
    using const_iterator = Iterator<const T>;

    /**
     * 默认构造函数
     */
    CircularQueue() = default;

    /**
     * 析构函数
     * 
     * 如果元素类型不是平凡可析构的，手动调用析构函数。
     */
    ~CircularQueue() {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0, c = mSize; i < c; ++i) {
                size_t const index = (mFront + CAPACITY - i) % CAPACITY;
                std::destroy_at(std::launder(reinterpret_cast<T*>(&mStorage[index])));
            }
        }
    }

    // 禁止拷贝
    CircularQueue(const CircularQueue&) = delete;
    CircularQueue& operator=(const CircularQueue&) = delete;

    /**
     * 移动构造函数
     * 
     * 移动所有元素到新队列。
     */
    CircularQueue(CircularQueue&& other) noexcept {
        for (size_t i = 0; i < other.mSize; i++) {
            size_t const index = (other.mFront + CAPACITY - i) % CAPACITY;
            new(&mStorage[index]) T(std::move(*std::launder(reinterpret_cast<T*>(&other.mStorage[index]))));
        }
        mFront = other.mFront;
        mSize = other.mSize;
        other.mSize = 0;
    }

    /**
     * 移动赋值操作符
     */
    CircularQueue& operator=(CircularQueue&& other) noexcept {
        if (this != &other) {
            this->~CircularQueue();
            new(this) CircularQueue(std::move(other));
        }
        return *this;
    }

    iterator begin() noexcept { return iterator(this, 0); }
    iterator end() noexcept { return iterator(this, size()); }
    const_iterator begin() const noexcept { return const_iterator(this, 0); }
    const_iterator end() const noexcept { return const_iterator(this, size()); }
    const_iterator cbegin() const noexcept { return const_iterator(this, 0); }
    const_iterator cend() const noexcept { return const_iterator(this, size()); }

    size_t capacity() const noexcept {
        return CAPACITY;
    }

    size_t size() const noexcept {
        return mSize;
    }

    bool empty() const noexcept {
        return !size();
    }

    /**
     * 从后部删除元素
     * 
     * 删除队列中最旧的元素（后部）。
     */
    void pop_back() noexcept {
        assert_invariant(!empty());
        --mSize;
        size_t const index = (mFront + CAPACITY - mSize) % CAPACITY;
        std::destroy_at(std::launder(reinterpret_cast<T*>(&mStorage[index])));
    }

    /**
     * 在前部插入元素（拷贝）
     * 
     * @param v 要插入的值
     */
    void push_front(T const& v) noexcept {
        assert_invariant(size() < CAPACITY);
        mFront = advance(mFront);
        new(&mStorage[mFront]) T(v);
        ++mSize;
    }

    /**
     * 在前部插入元素（移动）
     * 
     * @param v 要插入的值（会被移动）
     */
    void push_front(T&& v) noexcept {
        assert_invariant(size() < CAPACITY);
        mFront = advance(mFront);
        new(&mStorage[mFront]) T(std::move(v));
        ++mSize;
    }

    /**
     * 在前部就地构造元素
     * 
     * @param args 构造参数
     * @return 新构造元素的引用
     */
    template<typename ...Args>
    T& emplace_front(Args&&... args) noexcept {
        assert_invariant(size() < CAPACITY);
        mFront = advance(mFront);
        new(&mStorage[mFront]) T(std::forward<Args>(args)...);
        ++mSize;
        return front();
    }

    T& operator[](size_t const pos) noexcept {
        assert_invariant(pos < size());
        size_t const index = (mFront + CAPACITY - pos) % CAPACITY;
        return *std::launder(reinterpret_cast<T*>(&mStorage[index]));
    }

    T const& operator[](size_t pos) const noexcept {
        assert_invariant(pos < size());
        size_t const index = (mFront + CAPACITY - pos) % CAPACITY;
        return *std::launder(reinterpret_cast<T const*>(&mStorage[index]));
    }

    T const& front() const noexcept {
        assert_invariant(!empty());
        return operator[](0);
    }

    T& front() noexcept {
        assert_invariant(!empty());
        return operator[](0);
    }

    T const& back() const noexcept {
        assert_invariant(!empty());
        return operator[](size() - 1);
    }

    T& back() noexcept {
        assert_invariant(!empty());
        return operator[](size() - 1);
    }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
    Storage mStorage[CAPACITY];
    uint32_t mFront = 0;
    uint32_t mSize = 0;
    [[nodiscard]] uint32_t advance(uint32_t const v) noexcept {
        return (v + 1) % CAPACITY;
    }
};

/**
 * 帧信息管理器
 * 
 * 管理帧时间戳信息的收集、处理和查询。
 * 
 * 功能：
 * - 收集主线程和后端线程的时间戳
 * - 使用计时查询获取 GPU 帧时间
 * - 使用栅栏获取 GPU 完成时间
 * - 使用中值滤波器去噪帧时间
 * - 查询合成器时序信息（如果支持）
 * 
 * 设计：
 * - 使用循环队列存储帧历史
 * - 使用查询池（POOL_COUNT）管理计时查询
 * - 使用异步任务队列等待 GPU 完成
 */
class FrameInfoManager {
    static constexpr size_t POOL_COUNT = 4;  // 计时查询池大小
    static constexpr size_t MAX_FRAMETIME_HISTORY = 16u;  // 最大帧时间历史大小

public:
    using duration = FrameInfoImpl::duration;
    using clock = FrameInfoImpl::clock;

    /**
     * 配置结构
     */
    struct Config {
        uint32_t historySize;  // 历史大小（用于中值滤波器）
    };

    explicit FrameInfoManager(FEngine& engine, backend::DriverApi& driver) noexcept;

    ~FrameInfoManager() noexcept;

    /**
     * 终止帧信息管理器
     * 
     * 在调用 terminate() 之前，命令队列必须为空。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 开始帧
     * 
     * 在 "make current" 之后立即调用。
     * 
     * @param swapChain 交换链指针
     * @param driver 驱动 API 引用
     * @param config 配置
     * @param frameId 帧 ID
     * @param vsync 垂直同步时间点
     */
    void beginFrame(FSwapChain* swapChain, backend::DriverApi& driver,
            Config const& config, uint32_t frameId, std::chrono::steady_clock::time_point vsync) noexcept;

    /**
     * 结束帧
     * 
     * 在 "swap buffers" 之前立即调用。
     * 
     * @param driver 驱动 API 引用
     */
    void endFrame(backend::DriverApi& driver) noexcept;

    /**
     * 获取最后一帧信息
     * 
     * 如果 pFront 尚未设置，返回 FrameInfo()。但 `valid` 字段在这种情况下将为 false。
     * 
     * @return 最后一帧信息
     */
    details::FrameInfo getLastFrameInfo() const noexcept {
        // 如果 pFront 尚未设置，返回 FrameInfo()。但 `valid` 字段在这种情况下将为 false。
        return pFront ? *pFront : details::FrameInfo{};
    }

    /**
     * 更新用户历史
     * 
     * 更新用户可见的帧信息历史，包括查询显示呈现时间。
     * 
     * @param swapChain 交换链指针
     * @param driver 驱动 API 引用
     */
    void updateUserHistory(FSwapChain* swapChain, backend::DriverApi& driver);

    /**
     * 获取帧信息历史
     * 
     * @param historySize 历史大小（默认 MAX_FRAMETIME_HISTORY）
     * @return 帧信息历史向量
     */
    utils::FixedCapacityVector<Renderer::FrameInfo>
            getFrameInfoHistory(size_t historySize = MAX_FRAMETIME_HISTORY) const;

private:
    using FrameHistoryQueue = CircularQueue<FrameInfoImpl, MAX_FRAMETIME_HISTORY>;  // 帧历史队列
    
    /**
     * 去噪帧时间
     * 
     * 使用中值滤波器对帧时间进行去噪。
     * 
     * @param history 帧历史队列
     * @param config 配置
     */
    static void denoiseFrameTime(FrameHistoryQueue& history, Config const& config) noexcept;
    
    /**
     * 查询结构
     * 
     * 关联计时查询句柄和帧信息指针。
     */
    struct Query {
        backend::Handle<backend::HwTimerQuery> handle{};  // 计时查询句柄
        FrameInfoImpl* pInfo = nullptr;  // 关联的帧信息指针
    };
    
    utils::FixedCapacityVector<Renderer::FrameInfo> mUserFrameHistory;  // 用户帧历史
    std::array<Query, POOL_COUNT> mQueries{};  // 查询池
    uint32_t mIndex = 0;  // 当前查询的索引
    uint32_t mLast = 0;  // 仍活跃的最旧查询的索引
    FrameInfoImpl* pFront = nullptr;  // 具有有效帧时间的最新槽位
    FrameHistoryQueue mFrameTimeHistory{};  // 帧时间历史
    utils::AsyncJobQueue mJobQueue;  // 异步任务队列（用于等待 GPU 完成）
    FSwapChain* mLastSeenSwapChain = nullptr;  // 最后看到的交换链
    bool mLastBeginFrameSkipped = false;  // 上次 beginFrame 是否被跳过
    bool const mHasTimerQueries = false;  // 是否支持计时查询
    bool const mDisableGpuFrameComplete = false;  // 是否禁用 GPU 帧完成指标
};


} // namespace filament

#endif // TNT_FILAMENT_FRAMEINFO_H
