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

#include "FrameSkipper.h"

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/debug.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include <stddef.h>

namespace filament {

using namespace utils;
using namespace backend;

/**
 * FrameSkipper 构造函数
 * 
 * @param latency 帧延迟（会被限制在 1 到 MAX_FRAME_LATENCY 之间）
 */
FrameSkipper::FrameSkipper(size_t const latency) noexcept
        : mLatency(std::clamp(latency, size_t(1), MAX_FRAME_LATENCY) - 1) {
}

FrameSkipper::~FrameSkipper() noexcept = default;

/**
 * 终止帧跳过器
 * 
 * 销毁所有栅栏资源。
 * 
 * @param driver 驱动 API 引用
 */
void FrameSkipper::terminate(DriverApi& driver) noexcept {
    for (auto& fence : mDelayedFences) {
        if (fence) {
            driver.destroyFence(std::move(fence));
        }
    }
}

/**
 * 是否应该渲染帧
 * 
 * 检查 GPU 是否准备好渲染新帧。
 * 
 * @param driver 驱动 API 引用
 * @return true 如果可以渲染，false 如果应该跳过
 */
bool FrameSkipper::shouldRenderFrame(DriverApi& driver) const noexcept {

    /**
     * 如果设置了调试跳过标志，跳过帧
     */
    if (UTILS_UNLIKELY(mFrameToSkip)) {
        mFrameToSkip--;
        return false;
    }

    auto& fences = mDelayedFences;
    if (fences.front()) {
        /**
         * 我们有一个延迟旧的栅栏吗？
         * 检查最旧的栅栏状态，如果它还没有发出信号，说明 GPU 仍在处理该帧。
         */
        FenceStatus const status = driver.getFenceStatus(fences.front());
        if (UTILS_UNLIKELY(status == FenceStatus::TIMEOUT_EXPIRED)) {
            /**
             * 栅栏尚未发出信号，跳过此帧
             */
            return false;
        }
        // If we get a FenceStatus::ERROR, it doesn't necessarily indicate a "bug", it could
        // just be that fences are not supported. Regardless, we should return `true` in that
        // case.
        assert_invariant(status != FenceStatus::TIMEOUT_EXPIRED);
    }
    return true;
}

/**
 * 提交帧
 * 
 * 在帧渲染完成后调用，更新栅栏队列。
 * 
 * @param driver 驱动 API 引用
 */
void FrameSkipper::submitFrame(DriverApi& driver) noexcept {
    auto& fences = mDelayedFences;
    size_t const last = mLatency;

    /**
     * 弹出最旧的栅栏并推进其他栅栏
     */
    if (fences.front()) {
        driver.destroyFence(fences.front());
    }
    std::move(fences.begin() + 1, fences.end(), fences.begin());

    /**
     * 在末尾添加新栅栏
     */
    assert_invariant(!fences[last]);

    fences[last] = driver.createFence();
}

/**
 * 跳过接下来的帧
 * 
 * 设置要跳过的帧数（用于调试）。
 * 
 * @param frameCount 要跳过的帧数
 */
void FrameSkipper::skipNextFrames(size_t frameCount) const noexcept {
    frameCount = std::min(frameCount, size_t(std::numeric_limits<decltype(mFrameToSkip)>::max()));
    mFrameToSkip = uint16_t(frameCount);
}

/**
 * 获取待跳过的帧数
 * 
 * @return 剩余要跳过的帧数
 */
size_t FrameSkipper::getFrameToSkipCount() const noexcept {
    return mFrameToSkip;
}


} // namespace filament
