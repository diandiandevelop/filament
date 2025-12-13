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

#ifndef TNT_FILAMENT_DETAILS_FRAMESKIPPER_H
#define TNT_FILAMENT_DETAILS_FRAMESKIPPER_H

#include <backend/Handle.h>
#include <private/backend/DriverApi.h>

#include <array>

#include <stddef.h>
#include <stdint.h>

namespace filament {

/**
 * 帧跳过器
 * 
 * 用于确定当前帧是否需要跳过，以防止 CPU 超过 GPU。
 * 
 * 工作原理：
 * - 使用栅栏跟踪未完成的帧
 * - 如果 GPU 仍在处理较旧的帧，则跳过当前帧
 * - 这有助于保持帧率稳定并避免卡顿
 */
class FrameSkipper {
    /**
     * 最大帧延迟
     * 
     * 在 ANDROID 上可接受的最大帧延迟为 2，因为更高的延迟会在
     * BufferQueueProducer::dequeueBuffer() 中被限制，因为 ANDROID 通常最多是三缓冲；
     * 这种情况实际上很糟糕，因为 GL 线程可能在任何地方阻塞
     * （通常在触及交换链的第一个绘制命令内部）。
     * 
     * 帧延迟为 1 的好处是减少渲染延迟，
     * 但缺点是阻止 CPU / GPU 重叠。
     * 
     * 通常帧延迟为 2 是最好的折衷方案。
     */
    static constexpr size_t MAX_FRAME_LATENCY = 2;
public:
    /**
     * 构造函数
     * 
     * latency 参数定义在开始丢帧之前我们想要接受多少个未完成的帧。
     * 这会影响帧延迟。
     * 
     * - 延迟为 1：GPU 必须完成前一帧，这样我们才不会丢当前帧。
     *   虽然这提供了最好的延迟，但这不允许主线程、后端线程和 GPU 之间有很多重叠。
     * 
     * - 延迟为 2（默认）：允许 CPU 和 GPU 完全重叠，但主线程和驱动线程不能完全重叠。
     * 
     * - 延迟为 3：允许主线程、驱动线程和 GPU 重叠，每个都可以使用最多 16ms
     *   （或任何刷新率）。
     * 
     * @param latency 帧延迟（默认 2）
     */
    explicit FrameSkipper(size_t latency = 2) noexcept;
    ~FrameSkipper() noexcept;

    /**
     * 终止帧跳过器
     * 
     * 清理所有栅栏资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver) noexcept;

    /**
     * 是否应该渲染帧
     * 
     * 如果 GPU 落后于 CPU，返回 false，在这种情况下，不要调用 submitFrame()。
     * 如果渲染可以继续，返回 true。完成后始终调用 submitFrame()。
     * 
     * @param driver 驱动 API 引用
     * @return true 如果可以渲染，false 如果应该跳过
     */
    bool shouldRenderFrame(backend::DriverApi& driver) const noexcept;

    /**
     * 提交帧
     * 
     * 在帧渲染完成后调用，创建新的栅栏并更新栅栏队列。
     * 
     * @param driver 驱动 API 引用
     */
    void submitFrame(backend::DriverApi& driver) noexcept;

    /**
     * 跳过接下来的帧
     * 
     * 设置要报告为"跳过"的帧数。用于调试。
     * 
     * @param frameCount 要跳过的帧数
     */
    void skipNextFrames(size_t frameCount) const noexcept;
    
    /**
     * 获取待跳过的帧数
     * 
     * @return 剩余要跳过的帧数
     */
    size_t getFrameToSkipCount() const noexcept;

private:
    using Container = std::array<backend::Handle<backend::HwFence>, MAX_FRAME_LATENCY>;  // 栅栏容器
    Container mDelayedFences{};  // 延迟的栅栏数组（用于跟踪未完成的帧）
    uint8_t const mLatency;  // 帧延迟（实际值减 1）
    mutable uint16_t mFrameToSkip{};  // 待跳过的帧数（用于调试）
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_FRAMESKIPPER_H
