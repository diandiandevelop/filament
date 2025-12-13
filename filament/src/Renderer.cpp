/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <filament/Renderer.h>

#include "ResourceAllocator.h"

#include "details/Engine.h"
#include "details/Renderer.h"
#include "details/View.h"

#include <utils/FixedCapacityVector.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

using namespace math;
using namespace backend;

/**
 * 获取引擎引用
 * 
 * 返回与此渲染器关联的引擎引用。
 * 
 * @return 引擎指针
 * 
 * 实现：从内部实现类获取引擎的地址
 */
Engine* Renderer::getEngine() noexcept {
    return &downcast(this)->getEngine();
}

/**
 * 渲染视图
 * 
 * 执行视图的渲染操作。这会渲染视图关联的场景。
 * 
 * 注意：在调用此方法之前，必须先调用 beginFrame()。
 * 
 * @param view 要渲染的视图指针（不能为 nullptr）
 * 
 * 实现：将调用转发到内部实现类进行实际渲染
 */
void Renderer::render(View const* view) {
    downcast(this)->render(downcast(view));
}

/**
 * 设置呈现时间
 * 
 * 设置帧的呈现时间戳。这用于帧同步和时序控制。
 * 
 * @param monotonic_clock_ns 单调时钟时间戳（纳秒）
 *                          应该使用系统单调时钟，而不是挂钟时间
 * 
 * 实现：将调用转发到内部实现类设置呈现时间
 */
void Renderer::setPresentationTime(int64_t const monotonic_clock_ns) {
    downcast(this)->setPresentationTime(monotonic_clock_ns);
}

/**
 * 跳过帧
 * 
 * 指示渲染器跳过当前帧的渲染。这对于帧率控制很有用。
 * 
 * @param vsyncSteadyClockTimeNano VSync 的稳定时钟时间戳（纳秒）
 * 
 * 实现：将调用转发到内部实现类跳过帧
 */
void Renderer::skipFrame(uint64_t const vsyncSteadyClockTimeNano) {
    downcast(this)->skipFrame(vsyncSteadyClockTimeNano);
}

/**
 * 是否应该渲染帧
 * 
 * 检查是否应该渲染当前帧。这考虑了帧跳过逻辑。
 * 
 * @return true 如果应该渲染帧，false 如果应该跳过
 * 
 * 实现：从内部实现类查询是否应该渲染
 */
bool Renderer::shouldRenderFrame() const noexcept {
    return downcast(this)->shouldRenderFrame();
}

/**
 * 开始帧
 * 
 * 开始新的一帧渲染。必须在调用 render() 之前调用此方法。
 * 
 * @param swapChain 交换链指针（用于呈现渲染结果）
 * @param vsyncSteadyClockTimeNano VSync 的稳定时钟时间戳（纳秒）
 *                                 用于帧同步和时序控制
 * @return true 如果成功开始帧，false 如果失败（例如交换链无效）
 * 
 * 实现：将调用转发到内部实现类开始帧
 */
bool Renderer::beginFrame(SwapChain* swapChain, uint64_t const vsyncSteadyClockTimeNano) {
    return downcast(this)->beginFrame(downcast(swapChain), vsyncSteadyClockTimeNano);
}

/**
 * 复制帧
 * 
 * 将当前帧的内容复制到目标交换链。
 * 这对于多窗口渲染、截图等功能很有用。
 * 
 * @param dstSwapChain 目标交换链指针（接收复制的内容）
 * @param dstViewport 目标视口（在目标交换链中的位置和大小）
 * @param srcViewport 源视口（要复制的区域）
 * @param flags 复制标志
 *              - DEFAULT: 默认行为
 *              - COMMIT: 提交帧（用于多窗口同步）
 *              - SET_PRESENTATION_TIME: 设置呈现时间
 * 
 * 实现：将调用转发到内部实现类执行帧复制
 */
void Renderer::copyFrame(SwapChain* dstSwapChain, filament::Viewport const& dstViewport,
        filament::Viewport const& srcViewport, CopyFrameFlag const flags) {
    downcast(this)->copyFrame(downcast(dstSwapChain), dstViewport, srcViewport, flags);
}

/**
 * 读取像素（从默认渲染目标）
 * 
 * 从当前帧的默认渲染目标读取像素数据。
 * 
 * @param xoffset X 偏移量（像素）
 * @param yoffset Y 偏移量（像素）
 * @param width 读取宽度（像素）
 * @param height 读取高度（像素）
 * @param buffer 像素缓冲区描述符（会被移动）
 *               包含：
 *               - 数据指针
 *               - 数据大小
 *               - 格式和类型
 *               - 回调函数（可选，用于读取完成后的处理）
 * 
 * 实现：将调用转发到内部实现类读取像素
 * 
 * 注意：读取操作是异步的，数据可能在多帧后才可用
 */
void Renderer::readPixels(uint32_t const xoffset, uint32_t const yoffset, uint32_t const width, uint32_t const height,
        PixelBufferDescriptor&& buffer) {
    downcast(this)->readPixels(xoffset, yoffset, width, height, std::move(buffer));
}

/**
 * 读取像素（从指定渲染目标）
 * 
 * 从指定的渲染目标读取像素数据。
 * 
 * @param renderTarget 渲染目标指针（要读取的渲染目标）
 * @param xoffset X 偏移量（像素）
 * @param yoffset Y 偏移量（像素）
 * @param width 读取宽度（像素）
 * @param height 读取高度（像素）
 * @param buffer 像素缓冲区描述符（会被移动）
 *               包含：
 *               - 数据指针
 *               - 数据大小
 *               - 格式和类型
 *               - 回调函数（可选，用于读取完成后的处理）
 * 
 * 实现：将调用转发到内部实现类读取像素
 * 
 * 注意：读取操作是异步的，数据可能在多帧后才可用
 */
void Renderer::readPixels(RenderTarget* renderTarget,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const width, uint32_t const height,
        PixelBufferDescriptor&& buffer) {
    downcast(this)->readPixels(downcast(renderTarget),
            xoffset, yoffset, width, height, std::move(buffer));
}

/**
 * 结束帧
 * 
 * 结束当前帧的渲染。这会将命令提交到 GPU 并呈现结果。
 * 
 * 注意：必须在调用 beginFrame() 和 render() 之后调用此方法。
 * 
 * 实现：将调用转发到内部实现类结束帧
 */
void Renderer::endFrame() {
    downcast(this)->endFrame();
}

/**
 * 获取用户时间
 * 
 * 返回渲染器的用户时间。用户时间是一个自定义的时间值，可以用于动画等。
 * 
 * @return 用户时间（秒，双精度浮点数）
 * 
 * 实现：从内部实现类获取用户时间
 */
double Renderer::getUserTime() const {
    return downcast(this)->getUserTime();
}

/**
 * 重置用户时间
 * 
 * 将用户时间重置为 0。这通常用于重新开始动画或时间相关的效果。
 * 
 * 实现：将调用转发到内部实现类重置用户时间
 */
void Renderer::resetUserTime() {
    downcast(this)->resetUserTime();
}

/**
 * 跳过接下来的帧
 * 
 * 指示渲染器跳过接下来指定数量的帧。这对于性能测试、帧率控制等很有用。
 * 
 * @param frameCount 要跳过的帧数
 *                   - 0: 不跳过任何帧
 *                   - N: 跳过接下来的 N 帧
 * 
 * 实现：将调用转发到内部实现类设置跳帧计数
 */
void Renderer::skipNextFrames(size_t frameCount) const noexcept {
    downcast(this)->skipNextFrames(frameCount);
}

/**
 * 获取待跳过的帧数
 * 
 * 返回当前待跳过的帧数。
 * 
 * @return 待跳过的帧数
 *         0 表示不跳过任何帧
 * 
 * 实现：从内部实现类获取跳帧计数
 */
size_t Renderer::getFrameToSkipCount() const noexcept {
    return downcast(this)->getFrameToSkipCount();
}

/**
 * 设置显示信息
 * 
 * 设置显示器的信息，包括刷新率、分辨率等。这用于帧率同步和优化。
 * 
 * @param info 显示信息结构
 *             包含：
 *             - refreshRate: 刷新率（Hz）
 *             - presentationDeadline: 呈现截止时间（纳秒）
 *             - vsyncOffset: VSync 偏移（纳秒）
 * 
 * 实现：将调用转发到内部实现类设置显示信息
 */
void Renderer::setDisplayInfo(const DisplayInfo& info) noexcept {
    downcast(this)->setDisplayInfo(info);
}

/**
 * 设置帧率选项
 * 
 * 设置帧率相关的选项，包括目标帧率、帧率上限等。
 * 
 * @param options 帧率选项结构
 *                包含：
 *                - headRoom: 头部空间（用于帧率预测）
 *                - scaleRate: 缩放速率
 *                - minScaleRate: 最小缩放速率
 *                - maxScaleRate: 最大缩放速率
 * 
 * 实现：将调用转发到内部实现类设置帧率选项
 */
void Renderer::setFrameRateOptions(FrameRateOptions const& options) noexcept {
    downcast(this)->setFrameRateOptions(options);
}

/**
 * 设置清除选项
 * 
 * 设置帧缓冲区的清除选项，包括清除颜色、深度值等。
 * 
 * @param options 清除选项结构
 *                包含：
 *                - clear: 清除标志（颜色、深度、模板）
 *                - discardStart: 开始时的丢弃标志
 *                - discardEnd: 结束时的丢弃标志
 *                - clearColor: 清除颜色（RGBA）
 *                - clearDepth: 清除深度值
 *                - clearStencil: 清除模板值
 * 
 * 实现：将调用转发到内部实现类设置清除选项
 */
void Renderer::setClearOptions(const ClearOptions& options) {
    downcast(this)->setClearOptions(options);
}

/**
 * 获取清除选项
 * 
 * 返回当前的清除选项。
 * 
 * @return 清除选项常量引用
 * 
 * 实现：从内部实现类获取清除选项
 */
Renderer::ClearOptions const& Renderer::getClearOptions() const noexcept {
    return downcast(this)->getClearOptions();
}

/**
 * 渲染独立视图
 * 
 * 渲染一个独立的视图，不依赖于帧的开始/结束。这对于离屏渲染等场景很有用。
 * 
 * @param view 要渲染的视图指针（不能为 nullptr）
 * 
 * 实现：将调用转发到内部实现类渲染独立视图
 * 
 * 注意：此方法不需要调用 beginFrame() 和 endFrame()
 */
void Renderer::renderStandaloneView(View const* view) {
    downcast(this)->renderStandaloneView(downcast(view));
}

/**
 * 设置 VSync 时间
 * 
 * 设置 VSync（垂直同步）的时间戳。这用于帧同步和时序控制。
 * 
 * @param steadyClockTimeNano VSync 的稳定时钟时间戳（纳秒）
 *                            应该使用系统单调时钟，而不是挂钟时间
 * 
 * 实现：将调用转发到内部实现类设置 VSync 时间
 */
void Renderer::setVsyncTime(uint64_t const steadyClockTimeNano) noexcept {
    downcast(this)->setVsyncTime(steadyClockTimeNano);
}

/**
 * 获取帧信息历史
 * 
 * 返回最近帧的信息历史，包括帧时间、GPU 时间等。这对于性能分析很有用。
 * 
 * @param historySize 要返回的历史记录数量
 *                    - 如果为 0，返回所有可用历史
 *                    - 如果大于可用历史，返回所有可用历史
 * @return 帧信息向量
 *         每个元素包含：
 *         - frameTime: 帧时间（纳秒）
 *         - cpuTime: CPU 时间（纳秒）
 *         - gpuTime: GPU 时间（纳秒）
 * 
 * 实现：从内部实现类获取帧信息历史
 */
utils::FixedCapacityVector<Renderer::FrameInfo> Renderer::getFrameInfoHistory(size_t const historySize) const noexcept {
    return downcast(this)->getFrameInfoHistory(historySize);
}

/**
 * 获取最大帧历史大小
 * 
 * 返回可以存储的最大帧历史记录数量。
 * 
 * @return 最大帧历史大小
 * 
 * 实现：从内部实现类获取最大历史大小
 */
size_t Renderer::getMaxFrameHistorySize() const noexcept {
    return downcast(this)->getMaxFrameHistorySize();
}

} // namespace filament
