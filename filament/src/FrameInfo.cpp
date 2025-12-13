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

#include "FrameInfo.h"

#include <details/Engine.h>

#include <filament/Renderer.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>
#include <utils/JobSystem.h>
#include <utils/Logger.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <ratio>
#include <utility>

#include <stdint.h>
#include <stddef.h>

namespace filament {

using namespace utils;
using namespace backend;

/**
 * FrameInfoManager 构造函数
 * 
 * 初始化帧信息管理器，创建计时查询池（如果支持）。
 * 
 * @param engine 引擎引用
 * @param driver 驱动 API 引用
 */
FrameInfoManager::FrameInfoManager(FEngine& engine, DriverApi& driver) noexcept
    : mJobQueue("FrameInfoGpuComplete", JobSystem::Priority::URGENT_DISPLAY),  // 异步任务队列，用于等待 GPU 完成
      mHasTimerQueries(driver.isFrameTimeSupported()),  // 检查是否支持计时查询
      mDisableGpuFrameComplete(engine.features.engine.frame_info.disable_gpu_frame_complete_metric) {  // 检查是否禁用 GPU 帧完成指标
    if (mHasTimerQueries) {
        /**
         * 如果支持计时查询，创建查询池
         */
        for (auto& query : mQueries) {
            query.handle = driver.createTimerQuery();
        }
    }
}

FrameInfoManager::~FrameInfoManager() noexcept = default;

/**
 * 终止帧信息管理器
 * 
 * 清理所有资源，包括计时查询、栅栏和任务队列。
 * 
 * @param engine 引擎引用
 */
void FrameInfoManager::terminate(FEngine& engine) noexcept {
    DriverApi& driver = engine.getDriverApi();

    /**
     * 销毁所有计时查询
     */
    if (mHasTimerQueries) {
        for (auto const& query : mQueries) {
            driver.destroyTimerQuery(query.handle);
        }
    }

    if (!mDisableGpuFrameComplete) {
        /**
         * 移除所有待处理的回调。这样做是可以的，因为它们没有副作用。
         */
        mJobQueue.cancelAll();

        /**
         * 请求取消所有栅栏，这可能会加速下面的 drainAndExit()
         */
        for (auto& info : mFrameTimeHistory) {
            if (info.fence) {
                driver.fenceCancel(info.fence);
            }
        }

        /**
         * 等待所有待处理的回调被调用并终止线程
         */
        mJobQueue.drainAndExit();

        /**
         * 销毁仍然存活的栅栏，它们会出错。
         */
        for (size_t i = 0, c = mFrameTimeHistory.size(); i < c; i++) {
            auto& info = mFrameTimeHistory[i];
            if (info.fence) {
                driver.destroyFence(std::move(info.fence));
            }
        }
    }
}

/**
 * 开始帧
 * 
 * 在 "make current" 之后立即调用，记录帧开始时间并启动计时查询。
 * 
 * @param swapChain 交换链指针
 * @param driver 驱动 API 引用
 * @param config 配置
 * @param frameId 帧 ID
 * @param vsync 垂直同步时间点
 */
void FrameInfoManager::beginFrame(FSwapChain* swapChain, DriverApi& driver,
        Config const& config, uint32_t frameId, std::chrono::steady_clock::time_point const vsync) noexcept {
    auto const now = std::chrono::steady_clock::now();

    auto& history = mFrameTimeHistory;
    /**
     * 不要超过容量，丢弃最旧的条目
     */
    if (UTILS_LIKELY(history.size() == history.capacity())) {
        FrameInfoImpl& frameInfo = history.back();
        if (frameInfo.ready.load(std::memory_order_relaxed)) {
            /**
             * 最旧的条目已处理，可以安全删除
             */
            if (!mDisableGpuFrameComplete) {
                assert_invariant(frameInfo.fence);
                driver.destroyFence(std::move(frameInfo.fence));
            }
            history.pop_back();
        } else {
            /**
             * 这是一个大问题：循环队列已满，但该条目尚未处理。
             * 因为下面的代码保持对队列前端元素的引用，我们不能 pop/push。
             * 我们唯一的选择是不为此帧记录新条目，这会在数据中创建一个虚假的跳过帧。
             */
            LOG(WARNING) << "FrameInfo's circular queue is full, but the oldest item hasn't "
                            "been processed yet. Skipping this frame, id = " << frameId;
            mLastBeginFrameSkipped = true;
            return;
        }
    }

    /**
     * 创建新条目
     */
    FrameInfoImpl& front = history.emplace_front(frameId);

    /**
     * 存储当前时间
     */
    front.vsync = vsync;
    front.beginFrame = now;

    /**
     * 如果支持，存储合成器时序
     */
    CompositorTiming compositorTiming{};
    if (driver.isCompositorTimingSupported() &&
        driver.queryCompositorTiming(swapChain->getHwHandle(), &compositorTiming)) {
        front.presentDeadline = compositorTiming.compositeDeadline;
        front.displayPresentInterval = compositorTiming.compositeInterval;
        front.compositionToPresentLatency = compositorTiming.compositeToPresentLatency;
        front.expectedPresentTime = compositorTiming.expectedPresentTime;
        if (compositorTiming.frameTime != CompositorTiming::INVALID) {
            /**
             * 如果我们有来自合成器的 vsync 时间，忽略用户提供的
             */
            front.vsync = FrameInfoImpl::time_point{
                std::chrono::nanoseconds(compositorTiming.frameTime) };
        }
    }

    if (mHasTimerQueries) {
        /**
         * CircularQueue<> 不会使引用失效，所以我们可以将创建的槽位的引用
         * 关联到用于查找帧时间的计时查询。
         */
        mQueries[mIndex].pInfo = std::addressof(front);
        /**
         * 发出计时查询
         */
        driver.beginTimerQuery(mQueries[mIndex].handle);
    }

    /**
     * 发出自定义后端命令以获取后端时间
     */
    driver.queueCommand([&front](){
        front.backendBeginFrame = std::chrono::steady_clock::now();
    });

    if (mHasTimerQueries) {
        /**
         * 现在是检查最旧活动查询的好时机
         */
        while (mLast != mIndex) {
            uint64_t elapsed = 0;
            TimerQueryResult const result = driver.getTimerQueryValue(mQueries[mLast].handle, &elapsed);
            switch (result) {
                case TimerQueryResult::NOT_READY:
                    /**
                     * 查询尚未就绪，无需操作
                     */
                    break;
                case TimerQueryResult::ERROR:
                    /**
                     * 查询出错，跳过
                     */
                    mLast = (mLast + 1) % POOL_COUNT;
                    break;
                case TimerQueryResult::AVAILABLE: {
                    /**
                     * 查询结果可用，更新帧信息并去噪
                     */
                    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
                    FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "FrameInfo::elapsed", uint32_t(elapsed));
                    /**
                     * 转换为我们的 duration 类型
                     */
                    pFront = mQueries[mLast].pInfo;
                    pFront->gpuFrameDuration = std::chrono::duration<uint64_t, std::nano>(elapsed);
                    mLast = (mLast + 1) % POOL_COUNT;
                    denoiseFrameTime(history, config);
                    break;
                }
            }
            if (result != TimerQueryResult::AVAILABLE) {
                break;
            }
            /**
             * 读取待处理的计时查询，直到找到一个未就绪的
             */
        }
    } else {
        /**
         * 不支持计时查询，只需更新索引
         */
        if (mLast != mIndex) {
            mLast = (mLast + 1) % POOL_COUNT;
        }
    }

#if 0
    // keep this just for debugging
    using namespace utils;
    auto h = getFrameInfoHistory(1); // this can throw
    if (!h.empty()) {
        DLOG(INFO) << frameId << ": " << h[0].frameId << " (" << frameId - h[0].frameId <<
                ")"
                << ", Dm=" << h[0].endFrame - h[0].beginFrame
                << ", L =" << h[0].backendBeginFrame - h[0].beginFrame
                << ", Db=" << h[0].backendEndFrame - h[0].backendBeginFrame
                << ", T =" << h[0].gpuFrameDuration;
    }
#endif
}

/**
 * 结束帧
 * 
 * 在 "swap buffers" 之前立即调用，记录帧结束时间并创建栅栏以捕获 GPU 完成时间。
 * 
 * @param driver 驱动 API 引用
 */
void FrameInfoManager::endFrame(DriverApi& driver) noexcept {
    if (mLastBeginFrameSkipped) {
        /**
         * 如果我们必须跳过上次的 beginFrame()，endFrame() 也需要跳过
         * 因为 history.front() 现在引用了错误的帧。
         * 保证：如果调用了 beginFrame()，也会调用 endFrame()。
         */
        mLastBeginFrameSkipped = false;
        return;
    }

    auto& front = mFrameTimeHistory.front();
    front.endFrame = std::chrono::steady_clock::now();

    if (!mDisableGpuFrameComplete) {
        /**
         * 创建栅栏以捕获 GPU 完成时间
         */
        FenceHandle const fence = driver.createFence();
        front.fence = fence;
    }

    if (mHasTimerQueries) {
        /**
         * 关闭计时查询
         */
        driver.endTimerQuery(mQueries[mIndex].handle);
    }

    /**
     * 排队自定义后端命令以查询当前时间
     */
    driver.queueCommand([&jobQueue = mJobQueue, &driver, &front,
            disableGpuFrameComplete = mDisableGpuFrameComplete] {
        /**
         * 后端帧结束时间
         */
        front.backendEndFrame = std::chrono::steady_clock::now();

        if (UTILS_UNLIKELY(disableGpuFrameComplete || !jobQueue.isValid())) {
            front.gpuFrameComplete = {};
            front.ready.store(true, std::memory_order_release);
            return;
        }

        if (!disableGpuFrameComplete) {
            /**
             * 现在启动一个任务，等待 GPU 完成
             */
            jobQueue.push([&driver, &front] {
                FenceStatus const status = driver.fenceWait(front.fence, FENCE_WAIT_FOR_EVER);
                if (status == FenceStatus::CONDITION_SATISFIED) {
                    /**
                     * 栅栏条件满足，记录 GPU 完成时间
                     */
                    front.gpuFrameComplete = std::chrono::steady_clock::now();
                } else if (status == FenceStatus::TIMEOUT_EXPIRED) {
                    /**
                     * 这不应该发生，因为：
                     * - 我们永远等待
                     * - 确保 createFence() 命令已在后端处理
                     *   （因为我们在自定义命令内部）
                     */
                } else {
                    /**
                     * 我们遇到了错误，fenceWait 可能不受支持
                     */
                    front.gpuFrameComplete = {};
                }
                /**
                 * 最后，发出信号表示数据可用
                 */
                front.ready.store(true, std::memory_order_release);
            });
        }
    });

    /**
     * 更新查询索引
     */
    mIndex = (mIndex + 1) % POOL_COUNT;
}

/**
 * 去噪帧时间
 * 
 * 使用中值滤波器对帧时间进行去噪，以减少异常值的影响。
 * 
 * @param history 帧历史队列
 * @param config 配置
 */
void FrameInfoManager::denoiseFrameTime(FrameHistoryQueue& history, Config const& config) noexcept {
    assert_invariant(!history.empty());

    /**
     * 查找第一个具有有效帧持续时间的槽位
     */
    size_t first = history.size();
    for (size_t i = 0, c = history.size(); i < c; ++i) {
        if (history[i].gpuFrameDuration != duration(0)) {
            first = i;
            break;
        }
    }
    assert_invariant(first != history.size());

    /**
     * 我们需要至少 3 个有效帧时间来计算中值
     */
    if (history.size() >= first + 3) {
        /**
         * 应用中值滤波器以获得最后 N 帧的帧时间的良好表示
         */
        std::array<duration, MAX_FRAMETIME_HISTORY> median; // NOLINT -- 它会在下面初始化
        size_t const size = std::min({
            history.size() - first,
            median.size(),
            size_t(config.historySize) });

        /**
         * 复制帧持续时间到中值数组
         */
        for (size_t i = 0; i < size; ++i) {
            median[i] = history[first + i].gpuFrameDuration;
        }
        /**
         * 排序并取中值
         */
        std::sort(median.begin(), median.begin() + size);
        duration const denoisedFrameTime = median[size / 2];

        /**
         * 存储去噪后的帧时间并标记为有效
         */
        history[first].denoisedFrameTime = denoisedFrameTime;
        history[first].valid = true;
     }
}

/**
 * 更新用户历史
 * 
 * 更新用户可见的帧信息历史，包括查询显示呈现时间。
 * 
 * @param swapChain 交换链指针
 * @param driver 驱动 API 引用
 */
void FrameInfoManager::updateUserHistory(FSwapChain* swapChain, DriverApi& driver) {

    /**
     * 如果没有提供交换链，使用最后看到的交换链
     */
    if (!swapChain) {
        swapChain = mLastSeenSwapChain;
    } else {
        mLastSeenSwapChain = swapChain;
    }

    auto result = FixedCapacityVector<Renderer::FrameInfo>::with_capacity(MAX_FRAMETIME_HISTORY);
    auto& history = mFrameTimeHistory;
    size_t i = 0;
    size_t const c = history.size();
    
    /**
     * 查找第一个就绪的条目
     */
    for (; i < c; ++i) {
        auto const& entry = history[i];
        if (entry.ready.load(std::memory_order_acquire) && (entry.valid || !mHasTimerQueries)) {
            /**
             * 一旦我们找到一个就绪的条目，
             * 根据构造，我们知道所有后续的也都就绪
             */
            break;
        }
    }
    
    /**
     * 从第一个就绪的条目开始，收集帧信息
     */
    size_t historySize = MAX_FRAMETIME_HISTORY;
    for (; i < c && historySize; ++i, --historySize) {
        auto& entry = history[i];

        /**
         * 仅在我们还没有 displayPresentTime 时检索它
         */
        if (entry.displayPresent == Renderer::FrameInfo::PENDING) {
            FrameTimestamps frameTimestamps{
                .displayPresentTime = FrameTimestamps::INVALID
            };
            if (swapChain && driver.isCompositorTimingSupported()) {
                /**
                 * queryFrameTimestamps 可能会失败，如果此 frameId 不再可用
                 */
                bool const success = driver.queryFrameTimestamps(swapChain->getHwHandle(),
                        entry.frameId, &frameTimestamps);
                if (success) {
                    assert_invariant(entry.displayPresent < 0 ||
                            entry.displayPresent == frameTimestamps.displayPresentTime);
                    entry.displayPresent = frameTimestamps.displayPresentTime;
                }
            } else {
                entry.displayPresent = Renderer::FrameInfo::INVALID;
            }
        }

        /**
         * 转换时间类型到纳秒
         */
        using namespace std::chrono;
        // 根据构造，这不会抛出异常

        /**
         * 将 duration 转换为纳秒数
         */
        auto toDuration = [](details::FrameInfo::duration const d) {
            return duration_cast<nanoseconds>(d).count();
        };

        /**
         * 将 time_point 转换为纳秒数（自纪元以来）
         */
        auto toTimepoint = [](FrameInfoImpl::time_point const tp) {
            return duration_cast<nanoseconds>(tp.time_since_epoch()).count();
        };

        /**
         * 构建用户帧信息并添加到结果
         */
        result.push_back({
                .frameId                        = entry.frameId,
                .gpuFrameDuration               = toDuration(entry.gpuFrameDuration),
                .denoisedGpuFrameDuration       = toDuration(entry.denoisedFrameTime),
                .beginFrame                     = toTimepoint(entry.beginFrame),
                .endFrame                       = toTimepoint(entry.endFrame),
                .backendBeginFrame              = toTimepoint(entry.backendBeginFrame),
                .backendEndFrame                = toTimepoint(entry.backendEndFrame),
                .gpuFrameComplete               = toTimepoint(entry.gpuFrameComplete),
                .vsync                          = toTimepoint(entry.vsync),
                .displayPresent                 = entry.displayPresent,
                .presentDeadline                = entry.presentDeadline,
                .displayPresentInterval         = entry.displayPresentInterval,
                .compositionToPresentLatency    = entry.compositionToPresentLatency,
                .expectedPresentTime            = entry.expectedPresentTime,

        });
    }
    /**
     * 交换用户历史（原子操作）
     */
    std::swap(mUserFrameHistory, result);
}

/**
 * 获取帧信息历史
 * 
 * 返回用户可见的帧信息历史，限制为指定大小。
 * 
 * @param historySize 历史大小
 * @return 帧信息历史向量
 */
FixedCapacityVector<Renderer::FrameInfo> FrameInfoManager::getFrameInfoHistory(
        size_t const historySize) const {
    auto result = mUserFrameHistory;
    if (result.size() >= historySize) {
        result.resize(historySize);
    }
    return result;
}

} // namespace filament
