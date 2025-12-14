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

//! \file

#ifndef TNT_FILAMENT_RENDERER_H
#define TNT_FILAMENT_RENDERER_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>

#include <math/vec4.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class Engine;
class RenderTarget;
class SwapChain;
class View;
class Viewport;

namespace backend {
class PixelBufferDescriptor;
} // namespace backend

/**
 * A Renderer instance represents an operating system's window.
 *
 * Typically, applications create a Renderer per window. The Renderer generates drawing commands
 * for the render thread and manages frame latency.
 *
 * A Renderer generates drawing commands from a View, itself containing a Scene description.
 *
 * Creation and Destruction
 * ========================
 *
 * A Renderer is created using Engine.createRenderer() and destroyed using
 * Engine.destroy(const Renderer*).
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * #include <filament/Renderer.h>
 * #include <filament/Engine.h>
 * using namespace filament;
 *
 * Engine* engine = Engine::create();
 *
 * Renderer* renderer = engine->createRenderer();
 * engine->destroy(&renderer);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @see Engine, View
 */
/**
 * 渲染器实例，代表一个操作系统窗口
 * 
 * 通常，应用程序为每个窗口创建一个 Renderer。Renderer 为渲染线程生成绘制命令
 * 并管理帧延迟。
 * 
 * Renderer 从 View 生成绘制命令，View 本身包含一个 Scene 描述。
 * 
 * 创建和销毁
 * ========================
 * 
 * Renderer 使用 Engine.createRenderer() 创建，使用
 * Engine.destroy(const Renderer*) 销毁。
 * 
 * @see Engine, View
 */
class UTILS_PUBLIC Renderer : public FilamentAPI {
public:

    /**
     * Use DisplayInfo to set important Display properties. This is used to achieve correct
     * frame pacing and dynamic resolution scaling.
     */
    /**
     * 使用 DisplayInfo 设置重要的显示属性
     * 用于实现正确的帧节奏控制和动态分辨率缩放
     */
    struct DisplayInfo {
        // refresh-rate of the display in Hz. set to 0 for offscreen or turn off frame-pacing.
        /**
         * 显示器的刷新率（Hz）
         * 设置为 0 用于离屏渲染或关闭帧节奏控制
         */
        float refreshRate = 60.0f;

        UTILS_DEPRECATED uint64_t presentationDeadlineNanos = 0;
        UTILS_DEPRECATED uint64_t vsyncOffsetNanos = 0;
    };

    /**
     * Timing information about a frame
     * @see getFrameInfoHistory()
     */
    /**
     * 帧的计时信息
     * @see getFrameInfoHistory()
     */
    struct FrameInfo {
        /** duration in nanosecond since epoch of std::steady_clock */
        /** 自 std::steady_clock 纪元起经过的纳秒数 */
        using time_point_ns = int64_t;
        /** duration in nanosecond on the std::steady_clock */
        /** std::steady_clock 上的纳秒持续时间 */
        using duration_ns = int64_t;
        static constexpr time_point_ns INVALID = -1;    //!< value not supported / 不支持的值
        static constexpr time_point_ns PENDING = -2;    //!< value not yet available / 值尚不可用
        uint32_t frameId;                   //!< monotonically increasing frame identifier / 单调递增的帧标识符
        duration_ns gpuFrameDuration;       //!< frame duration on the GPU in nanosecond [ns] / GPU 上的帧持续时间（纳秒）
        duration_ns denoisedGpuFrameDuration; //!< denoised frame duration on the GPU in [ns] / GPU 上去噪后的帧持续时间（纳秒）
        time_point_ns beginFrame;           //!< Renderer::beginFrame() time since epoch [ns] / Renderer::beginFrame() 的时间（自纪元起，纳秒）
        time_point_ns endFrame;             //!< Renderer::endFrame() time since epoch [ns] / Renderer::endFrame() 的时间（自纪元起，纳秒）
        time_point_ns backendBeginFrame;    //!< Backend thread time of frame start since epoch [ns] / 后端线程帧开始时间（自纪元起，纳秒）
        time_point_ns backendEndFrame;      //!< Backend thread time of frame end since epoch [ns] / 后端线程帧结束时间（自纪元起，纳秒）
        time_point_ns gpuFrameComplete;     //!< GPU thread time of frame end since epoch [ns] or 0 / GPU 线程帧结束时间（自纪元起，纳秒）或 0
        time_point_ns vsync;                //!< VSYNC time of this frame since epoch [ns] / 此帧的 VSYNC 时间（自纪元起，纳秒）
        time_point_ns displayPresent;       //!< Actual presentation time of this frame since epoch [ns] / 此帧的实际呈现时间（自纪元起，纳秒）
        time_point_ns presentDeadline;      //!< deadline for queuing a frame [ns] / 排队帧的截止时间（纳秒）
        duration_ns displayPresentInterval; //!< display refresh rate [ns] / 显示器刷新间隔（纳秒）
        duration_ns compositionToPresentLatency; //!< time between the start of composition and the expected present time [ns] / 合成开始到预期呈现时间之间的时间（纳秒）
        time_point_ns expectedPresentTime;  //!< system's expected presentation time since epoch [ns] / 系统预期的呈现时间（自纪元起，纳秒）
    };

    /**
     * Retrieve a history of frame timing information. The maximum frame history size is
     * given by getMaxFrameHistorySize().
     * All or part of the history can be lost when using a different SwapChain in beginFrame().
     * @param historySize requested history size. The returned vector could be smaller.
     * @return A vector of FrameInfo.
     * @see beginFrame()
     */
    /**
     * 检索帧计时信息的历史记录
     * 最大帧历史大小由 getMaxFrameHistorySize() 给出。
     * 在 beginFrame() 中使用不同的 SwapChain 时，可能会丢失全部或部分历史记录。
     * @param historySize 请求的历史记录大小。返回的向量可能更小。
     * @return FrameInfo 向量
     * @see beginFrame()
     */
    utils::FixedCapacityVector<FrameInfo> getFrameInfoHistory(
            size_t historySize = 1) const noexcept;

    /**
     * @return the maximum supported frame history size.
     * @see getFrameInfoHistory()
     */
    /**
     * @return 最大支持的帧历史大小
     * @see getFrameInfoHistory()
     */
    size_t getMaxFrameHistorySize() const noexcept;

    /**
     * Use FrameRateOptions to set the desired frame rate and control how quickly the system
     * reacts to GPU load changes.
     *
     * interval: desired frame interval in multiple of the refresh period, set in DisplayInfo
     *           (as 1 / DisplayInfo::refreshRate)
     *
     * The parameters below are relevant when some Views are using dynamic resolution scaling:
     *
     * headRoomRatio: additional headroom for the GPU as a ratio of the targetFrameTime.
     *                Useful for taking into account constant costs like post-processing or
     *                GPU drivers on different platforms.
     * history:   History size. higher values, tend to filter more (clamped to 31)
     * scaleRate: rate at which the gpu load is adjusted to reach the target frame rate
     *            This value can be computed as 1 / N, where N is the number of frames
     *            needed to reach 64% of the target scale factor.
     *            Higher values make the dynamic resolution react faster.
     *
     * @see View::DynamicResolutionOptions
     * @see Renderer::DisplayInfo
     *
     */
    /**
     * 使用 FrameRateOptions 设置期望的帧率并控制系统对 GPU 负载变化的反应速度
     *
     * interval: 期望的帧间隔，以 DisplayInfo 中设置的刷新周期倍数表示
     *           （即 1 / DisplayInfo::refreshRate）
     *
     * 当某些 View 使用动态分辨率缩放时，以下参数相关：
     *
     * headRoomRatio: GPU 的额外余量，以 targetFrameTime 的比率表示。
     *                用于考虑固定成本，如后处理或不同平台上的 GPU 驱动程序。
     * history:   历史记录大小。值越高，过滤效果越明显（限制在 31）
     * scaleRate: GPU 负载调整以达到目标帧率的速率
     *            该值可以计算为 1 / N，其中 N 是达到目标缩放因子 64% 所需的帧数。
     *            值越高，动态分辨率反应越快。
     *
     * @see View::DynamicResolutionOptions
     * @see Renderer::DisplayInfo
     */
    struct FrameRateOptions {
        float headRoomRatio = 0.0f;        //!< additional headroom for the GPU / GPU 的额外余量
        float scaleRate = 1.0f / 8.0f;     //!< rate at which the system reacts to load changes / 系统对负载变化的反应速率
        uint8_t history = 15;              //!< history size / 历史记录大小
        uint8_t interval = 1;              //!< desired frame interval in unit of 1.0 / DisplayInfo::refreshRate / 期望的帧间隔（单位：1.0 / DisplayInfo::refreshRate）
    };

    /**
     * ClearOptions are used at the beginning of a frame to clear or retain the SwapChain content.
     */
    /**
     * ClearOptions 用于在帧开始时清除或保留 SwapChain 内容
     */
    struct ClearOptions {
        /**
         * Color (sRGB linear) to use to clear the RenderTarget (typically the SwapChain).
         *
         * The RenderTarget is cleared using this color, which won't be tone-mapped since
         * tone-mapping is part of View rendering (this is not).
         *
         * When a View is rendered, there are 3 scenarios to consider:
         * - Pixels rendered by the View replace the clear color (or blend with it in
         *   `BlendMode::TRANSLUCENT` mode).
         *
         * - With blending mode set to `BlendMode::TRANSLUCENT`, Pixels untouched by the View
         *   are considered fulling transparent and let the clear color show through.
         *
         * - With blending mode set to `BlendMode::OPAQUE`, Pixels untouched by the View
         *   are set to the clear color. However, because it is now used in the context of a View,
         *   it will go through the post-processing stage, which includes tone-mapping.
         *
         * For consistency, it is recommended to always use a Skybox to clear an opaque View's
         * background, or to use black or fully-transparent (i.e. {0,0,0,0}) as the clear color.
         */
        /**
         * 用于清除 RenderTarget（通常是 SwapChain）的颜色（sRGB 线性空间）
         *
         * RenderTarget 使用此颜色清除，该颜色不会被色调映射，因为
         * 色调映射是 View 渲染的一部分（这里不是）。
         *
         * 渲染 View 时，需要考虑 3 种情况：
         * - View 渲染的像素替换清除颜色（或在 `BlendMode::TRANSLUCENT` 模式下与其混合）。
         *
         * - 混合模式设置为 `BlendMode::TRANSLUCENT` 时，View 未触及的像素
         *   被视为完全透明，让清除颜色显示出来。
         *
         * - 混合模式设置为 `BlendMode::OPAQUE` 时，View 未触及的像素
         *   设置为清除颜色。但是，由于现在在 View 的上下文中使用，
         *   它将经过后处理阶段，其中包括色调映射。
         *
         * 为了一致性，建议始终使用 Skybox 清除不透明 View 的
         * 背景，或使用黑色或完全透明（即 {0,0,0,0}）作为清除颜色。
         */
        math::float4 clearColor = {};

        /** Value to clear the stencil buffer */
        /** 用于清除模板缓冲区的值 */
        uint8_t clearStencil = 0u;

        /**
         * Whether the SwapChain should be cleared using the clearColor. Use this if translucent
         * View will be drawn, for instance.
         */
        /**
         * 是否应使用 clearColor 清除 SwapChain。例如，如果将要绘制半透明
         * View，请使用此项。
         */
        bool clear = false;

        /**
         * Whether the SwapChain content should be discarded. clear implies discard. Set this
         * to false (along with clear to false as well) if the SwapChain already has content that
         * needs to be preserved
         */
        /**
         * 是否应丢弃 SwapChain 内容。clear 意味着 discard。如果 SwapChain 已经有需要
         * 保留的内容，请将此设置为 false（同时将 clear 也设置为 false）
         */
        bool discard = true;
    };

    /**
     * Information about the display this Renderer is associated to. This information is needed
     * to accurately compute dynamic-resolution scaling and for frame-pacing.
     */
    /**
     * 设置与此 Renderer 关联的显示器的信息
     * 需要此信息以准确计算动态分辨率缩放和帧节奏控制
     */
    void setDisplayInfo(const DisplayInfo& info) noexcept;

    /**
     * Set options controlling the desired frame-rate.
     */
    /**
     * 设置控制期望帧率的选项
     */
    void setFrameRateOptions(FrameRateOptions const& options) noexcept;

    /**
     * Set ClearOptions which are used at the beginning of a frame to clear or retain the
     * SwapChain content.
     */
    /**
     * 设置 ClearOptions，用于在帧开始时清除或保留 SwapChain 内容
     */
    void setClearOptions(const ClearOptions& options);

    /**
     * Returns the ClearOptions currently set.
     * @return A reference to a ClearOptions structure.
     */
    /**
     * 返回当前设置的 ClearOptions
     * @return ClearOptions 结构的引用
     */
    ClearOptions const& getClearOptions() const noexcept;

    /**
     * Get the Engine that created this Renderer.
     *
     * @return A pointer to the Engine instance this Renderer is associated to.
     */
    /**
     * 获取创建此 Renderer 的 Engine
     *
     * @return 与此 Renderer 关联的 Engine 实例的指针
     */
    Engine* UTILS_NONNULL getEngine() noexcept;

    /**
     * Get the Engine that created this Renderer.
     *
     * @return A constant pointer to the Engine instance this Renderer is associated to.
     */
    /**
     * 获取创建此 Renderer 的 Engine（const 版本）
     *
     * @return 与此 Renderer 关联的 Engine 实例的常量指针
     */
    inline Engine const* UTILS_NONNULL getEngine() const noexcept {
        return const_cast<Renderer *>(this)->getEngine();
    }

    /**
     * Flags used to configure the behavior of copyFrame().
     *
     * @see
     * copyFrame()
     */
    /**
     * 用于配置 copyFrame() 行为的标志
     *
     * @see copyFrame()
     */
    using CopyFrameFlag = uint32_t;

    /**
     * Indicates that the dstSwapChain passed into copyFrame() should be
     * committed after the frame has been copied.
     *
     * @see
     * copyFrame()
     */
    /**
     * 表示传入 copyFrame() 的 dstSwapChain 应在帧复制后提交
     *
     * @see copyFrame()
     */
    static constexpr CopyFrameFlag COMMIT = 0x1;
    /**
     * Indicates that the presentation time should be set on the dstSwapChain
     * passed into copyFrame to the monotonic clock time when the frame is
     * copied.
     *
     * @see
     * copyFrame()
     */
    /**
     * 表示应在传入 copyFrame 的 dstSwapChain 上将呈现时间设置为
     * 帧复制时的单调时钟时间
     *
     * @see copyFrame()
     */
    static constexpr CopyFrameFlag SET_PRESENTATION_TIME = 0x2;
    /**
     * Indicates that the dstSwapChain passed into copyFrame() should be
     * cleared to black before the frame is copied into the specified viewport.
     *
     * @see
     * copyFrame()
     */
    /**
     * 表示传入 copyFrame() 的 dstSwapChain 应在将帧复制到指定视口之前
     * 清除为黑色
     *
     * @see copyFrame()
     */
    static constexpr CopyFrameFlag CLEAR = 0x4;


    /**
     * The use of this method is optional. It sets the VSYNC time expressed as the duration in
     * nanosecond since epoch of std::chrono::steady_clock.
     * If called, passing 0 to vsyncSteadyClockTimeNano in Renderer::BeginFrame will use this
     * time instead.
     * @param steadyClockTimeNano duration in nanosecond since epoch of std::chrono::steady_clock
     * @see Engine::getSteadyClockTimeNano()
     * @see Renderer::BeginFrame()
     */
    /**
     * 此方法的使用是可选的。它设置以自 std::chrono::steady_clock 纪元起经过的纳秒数
     * 表示的 VSYNC 时间。
     * 如果调用，在 Renderer::BeginFrame 中将 0 传递给 vsyncSteadyClockTimeNano 将使用此
     * 时间。
     * @param steadyClockTimeNano 自 std::chrono::steady_clock 纪元起经过的纳秒数
     * @see Engine::getSteadyClockTimeNano()
     * @see Renderer::BeginFrame()
     */
    void setVsyncTime(uint64_t steadyClockTimeNano) noexcept;

    /**
     * Call skipFrame when momentarily skipping frames, for instance if the content of the
     * scene doesn't change.
     *
     * @param vsyncSteadyClockTimeNano
     */
    /**
     * 当暂时跳过帧时调用 skipFrame，例如场景内容未改变时
     *
     * @param vsyncSteadyClockTimeNano VSYNC 时间（纳秒，自 std::chrono::steady_clock 纪元起）
     */
    void skipFrame(uint64_t vsyncSteadyClockTimeNano = 0u);

    /**
     * Returns true if the current frame should be rendered.
     *
     * This is a convenience method that returns the same value as beginFrame().
     *
     * @return
     *      *false* the current frame should be skipped,
     *      *true* the current frame can be rendered
     *
     * @see
     * beginFrame()
     */
    /**
     * 如果当前帧应该渲染，返回 true
     *
     * 这是一个便捷方法，返回与 beginFrame() 相同的值
     *
     * @return
     *      *false* 当前帧应该被跳过，
     *      *true* 当前帧可以渲染
     *
     * @see beginFrame()
     */
    bool shouldRenderFrame() const noexcept;

    /**
     * Set up a frame for this Renderer.
     *
     * beginFrame() manages frame-pacing, and returns whether a frame should be drawn. The
     * goal of this is to skip frames when the GPU falls behind in order to keep the frame
     * latency low.
     *
     * If a given frame takes too much time in the GPU, the CPU will get ahead of the GPU. The
     * display will draw the same frame twice producing a stutter. At this point, the CPU is
     * ahead of the GPU and depending on how many frames are buffered, latency increases.
     *
     * beginFrame() attempts to detect this situation and returns false in that case, indicating
     * to the caller to skip the current frame.
     *
     * When beginFrame() returns true, it is mandatory to render the frame and call endFrame().
     * However, when beginFrame() returns false, the caller has the choice to either skip the
     * frame and not call endFrame(), or proceed as though true was returned.
     *
     * @param vsyncSteadyClockTimeNano The time in nanosecond of when the current frame started,
     *                                 or 0 if unknown. This value should be the timestamp of
     *                                 the last h/w vsync. It is expressed in the
     *                                 std::chrono::steady_clock time base.
     *                                 On Android this should be the frame time received from
     *                                 a Choreographer.
     * @param swapChain A pointer to the SwapChain instance to use.
     *
     * @return
     *      *false* the current frame should be skipped,
     *      *true* the current frame must be drawn and endFrame() must be called.
     *
     * @remark
     * When skipping a frame, the whole frame is canceled, and endFrame() must not be called.
     *
     * @note
     * All calls to render() must happen *after* beginFrame().
     * It is recommended to use the same swapChain for every call to beginFrame, failing to do
     * so can result is losing all or part of the FrameInfo history.
     *
     * @see
     * endFrame()
     */
    /**
     * 为此 Renderer 设置一帧
     *
     * beginFrame() 管理帧节奏控制，并返回是否应该绘制一帧。目标是
     * 当 GPU 落后时跳过帧，以保持帧延迟较低。
     *
     * 如果给定帧在 GPU 上花费太多时间，CPU 将领先于 GPU。显示器
     * 将绘制同一帧两次，产生卡顿。此时，CPU 领先于 GPU，根据
     * 缓冲的帧数，延迟会增加。
     *
     * beginFrame() 尝试检测这种情况并在这种情况下返回 false，指示
     * 调用者跳过当前帧。
     *
     * 当 beginFrame() 返回 true 时，必须渲染该帧并调用 endFrame()。
     * 但是，当 beginFrame() 返回 false 时，调用者可以选择跳过
     * 该帧且不调用 endFrame()，或者继续执行，就像返回 true 一样。
     *
     * @param vsyncSteadyClockTimeNano 当前帧开始的时间（纳秒），
     *                                 如果未知则为 0。该值应该是
     *                                 最后一次硬件 vsync 的时间戳。它以
     *                                 std::chrono::steady_clock 时间基准表示。
     *                                 在 Android 上，这应该是从
     *                                 Choreographer 接收的帧时间。
     * @param swapChain 要使用的 SwapChain 实例的指针
     *
     * @return
     *      *false* 当前帧应该被跳过，
     *      *true* 必须绘制当前帧并必须调用 endFrame()
     *
     * @remark
     * 跳过帧时，整个帧被取消，不得调用 endFrame()。
     *
     * @note
     * 所有对 render() 的调用必须在 beginFrame() *之后* 发生。
     * 建议每次调用 beginFrame 时使用相同的 swapChain，否则
     * 可能导致丢失全部或部分 FrameInfo 历史记录。
     *
     * @see endFrame()
     */
    bool beginFrame(SwapChain* UTILS_NONNULL swapChain,
            uint64_t vsyncSteadyClockTimeNano = 0u);

    /**
     * Set the time at which the frame must be presented to the display.
     *
     * This must be called between beginFrame() and endFrame().
     *
     * @param monotonic_clock_ns  the time in nanoseconds corresponding to the system monotonic up-time clock.
     *                            the presentation time is typically set in the middle of the period
     *                            of interest. The presentation time cannot be too far in the
     *                            future because it is limited by how many buffers are available in
     *                            the display sub-system. Typically it is set to 1 or 2 vsync periods
     *                            away.
     */
    /**
     * 设置帧必须呈现到显示器的时间
     *
     * 必须在 beginFrame() 和 endFrame() 之间调用。
     *
     * @param monotonic_clock_ns  以纳秒为单位的时间，对应系统单调递增时钟。
     *                            呈现时间通常设置在关注时间段的中间。
     *                            呈现时间不能设置得太远，因为它受显示子系统中
     *                            可用缓冲区数量的限制。通常设置为距离当前 1 或 2 个 vsync 周期。
     */
    void setPresentationTime(int64_t monotonic_clock_ns);

    /**
     * Render a View into this renderer's window.
     *
     * This is filament main rendering method, most of the CPU-side heavy lifting is performed
     * here. render() main function is to generate render commands which are asynchronously
     * executed by the Engine's render thread.
     *
     * render() generates commands for each of the following stages:
     *
     * 1. Shadow map passes, if needed.
     * 2. Depth pre-pass.
     * 3. Color pass.
     * 4. Post-processing pass.
     *
     * A typical render loop looks like this:
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * #include <filament/Renderer.h>
     * #include <filament/View.h>
     * using namespace filament;
     *
     * void renderLoop(Renderer* renderer, SwapChain* swapChain) {
     *     do {
     *         // typically we wait for VSYNC and user input events
     *         if (renderer->beginFrame(swapChain)) {
     *             renderer->render(mView);
     *             renderer->endFrame();
     *         }
     *     } while (!quit());
     * }
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     *
     * @param view A pointer to the view to render.
     *
     * @attention
     * render() must be called *after* beginFrame() and *before* endFrame().
     *
     * @note
     * render() must be called from the Engine's main thread (or external synchronization
     * must be provided). In particular, calls to render() on different Renderer instances
     * **must** be synchronized.
     *
     * @remark
     * render() perform potentially heavy computations and cannot be multi-threaded. However,
     * internally, render() is highly multi-threaded to both improve performance in mitigate
     * the call's latency.
     *
     * @remark
     * render() is typically called once per frame (but not necessarily).
     *
     * @see
     * beginFrame(), endFrame(), View
     *
     */
    /**
     * 将 View 渲染到此渲染器的窗口中
     *
     * 这是 Filament 的主要渲染方法，大部分 CPU 端的繁重工作都在这里执行。
     * render() 的主要功能是生成渲染命令，这些命令由 Engine 的渲染线程异步执行。
     *
     * render() 为以下每个阶段生成命令：
     *
     * 1. 阴影贴图通道（如果需要）
     * 2. 深度预通道
     * 3. 颜色通道
     * 4. 后处理通道
     *
     * @param view 要渲染的 view 的指针
     *
     * @attention
     * render() 必须在 beginFrame() *之后* 和 endFrame() *之前* 调用。
     *
     * @note
     * render() 必须从 Engine 的主线程调用（或必须提供外部同步）。
     * 特别是，在不同 Renderer 实例上调用 render() **必须** 同步。
     *
     * @remark
     * render() 执行可能繁重的计算，不能多线程化。但是，
     * 内部，render() 高度多线程化，以提高性能并减轻调用的延迟。
     *
     * @remark
     * render() 通常每帧调用一次（但不一定）。
     *
     * @see beginFrame(), endFrame(), View
     */
    void render(View const* UTILS_NONNULL view);

    /**
     * Copy the currently rendered view to the indicated swap chain, using the
     * indicated source and destination rectangle.
     *
     * @param dstSwapChain The swap chain into which the frame should be copied.
     * @param dstViewport The destination rectangle in which to draw the view.
     * @param srcViewport The source rectangle to be copied.
     * @param flags One or more CopyFrameFlag behavior configuration flags.
     *
     * @remark
     * copyFrame() should be called after a frame is rendered using render()
     * but before endFrame() is called.
     */
    /**
     * 将当前渲染的 view 复制到指定的交换链，使用
     * 指定的源矩形和目标矩形
     *
     * @param dstSwapChain 应将帧复制到的交换链
     * @param dstViewport 绘制 view 的目标矩形
     * @param srcViewport 要复制的源矩形
     * @param flags 一个或多个 CopyFrameFlag 行为配置标志
     *
     * @remark
     * copyFrame() 应在使用 render() 渲染帧之后调用，
     * 但在调用 endFrame() 之前调用
     */
    void copyFrame(SwapChain* UTILS_NONNULL dstSwapChain, Viewport const& dstViewport,
            Viewport const& srcViewport, uint32_t flags = 0);

    /**
     * Reads back the content of the SwapChain associated with this Renderer.
     *
     * @param xoffset   Left offset of the sub-region to read back.
     * @param yoffset   Bottom offset of the sub-region to read back.
     * @param width     Width of the sub-region to read back.
     * @param height    Height of the sub-region to read back.
     * @param buffer    Client-side buffer where the read-back will be written.
     *
     * The following formats are always supported:
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA_INTEGER
     *
     * The following types are always supported:
     *                      - PixelBufferDescriptor::PixelDataType::UBYTE
     *                      - PixelBufferDescriptor::PixelDataType::UINT
     *                      - PixelBufferDescriptor::PixelDataType::INT
     *                      - PixelBufferDescriptor::PixelDataType::FLOAT
     *
     * Other combinations of format/type may be supported. If a combination is
     * not supported, this operation may fail silently. Use a DEBUG build
     * to get some logs about the failure.
     *
     *
     *  Framebuffer as seen on User buffer (PixelBufferDescriptor&)
     *  screen
     *
     *      +--------------------+
     *      |                    |                .stride         .alignment
     *      |                    |         ----------------------->-->
     *      |                    |         O----------------------+--+   low addresses
     *      |                    |         |          |           |  |
     *      |             w      |         |          | .top      |  |
     *      |       <--------->  |         |          V           |  |
     *      |       +---------+  |         |     +---------+      |  |
     *      |       |     ^   |  | ======> |     |         |      |  |
     *      |   x   |    h|   |  |         |.left|         |      |  |
     *      +------>|     v   |  |         +---->|         |      |  |
     *      |       +.........+  |         |     +.........+      |  |
     *      |            ^       |         |                      |  |
     *      |          y |       |         +----------------------+--+  high addresses
     *      O------------+-------+
     *
     *
     * readPixels() must be called within a frame, meaning after beginFrame() and before endFrame().
     * Typically, readPixels() will be called after render().
     *
     * After issuing this method, the callback associated with `buffer` will be invoked on the
     * main thread, indicating that the read-back has completed. Typically, this will happen
     * after multiple calls to beginFrame(), render(), endFrame().
     *
     * It is also possible to use a Fence to wait for the read-back.
     *
     * @remark
     * readPixels() is intended for debugging and testing. It will impact performance significantly.
     *
     */
    /**
     * 读取回与此 Renderer 关联的 SwapChain 的内容
     *
     * @param xoffset   要读取的子区域的左偏移量
     * @param yoffset   要读取的子区域的底偏移量
     * @param width     要读取的子区域的宽度
     * @param height    要读取的子区域的高度
     * @param buffer    将写入读取内容的客户端缓冲区
     *
     * 始终支持以下格式：
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA_INTEGER
     *
     * 始终支持以下类型：
     *                      - PixelBufferDescriptor::PixelDataType::UBYTE
     *                      - PixelBufferDescriptor::PixelDataType::UINT
     *                      - PixelBufferDescriptor::PixelDataType::INT
     *                      - PixelBufferDescriptor::PixelDataType::FLOAT
     *
     * 可能支持其他格式/类型组合。如果不支持某个组合，
     * 此操作可能会静默失败。使用 DEBUG 构建获取有关失败的日志。
     *
     * readPixels() 必须在帧内调用，即在 beginFrame() 之后和 endFrame() 之前。
     * 通常，readPixels() 将在 render() 之后调用。
     *
     * 发出此方法后，将在主线程上调用与 `buffer` 关联的回调，
     * 指示读取已完成。通常，这将在多次调用 beginFrame()、render()、endFrame() 后发生。
     *
     * 也可以使用 Fence 等待读取完成。
     *
     * @remark
     * readPixels() 用于调试和测试。它会显著影响性能。
     */
    void readPixels(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            backend::PixelBufferDescriptor&& buffer);

    /**
     * Finishes the current frame and schedules it for display.
     *
     * endFrame() schedules the current frame to be displayed on the Renderer's window.
     *
     * @note
     * All calls to render() must happen *before* endFrame(). endFrame() must be called if
     * beginFrame() returned true, otherwise, endFrame() must not be called unless the caller
     * ignored beginFrame()'s return value.
     *
     * @see
     * beginFrame()
     */
    /**
     * 完成当前帧并安排其显示
     *
     * endFrame() 安排当前帧显示在 Renderer 的窗口上。
     *
     * @note
     * 所有对 render() 的调用必须在 endFrame() *之前* 发生。
     * 如果 beginFrame() 返回 true，则必须调用 endFrame()，否则不得调用 endFrame()，
     * 除非调用者忽略了 beginFrame() 的返回值。
     *
     * @see beginFrame()
     */
    void endFrame();


    /**
     * Reads back the content of the provided RenderTarget.
     *
     * @param renderTarget  RenderTarget to read back from.
     * @param xoffset       Left offset of the sub-region to read back.
     * @param yoffset       Bottom offset of the sub-region to read back.
     * @param width         Width of the sub-region to read back.
     * @param height        Height of the sub-region to read back.
     * @param buffer        Client-side buffer where the read-back will be written.
     *
     * The following formats are always supported:
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA_INTEGER
     *
     * The following types are always supported:
     *                      - PixelBufferDescriptor::PixelDataType::UBYTE
     *                      - PixelBufferDescriptor::PixelDataType::UINT
     *                      - PixelBufferDescriptor::PixelDataType::INT
     *                      - PixelBufferDescriptor::PixelDataType::FLOAT
     *
     * Other combinations of format/type may be supported. If a combination is
     * not supported, this operation may fail silently. Use a DEBUG build
     * to get some logs about the failure.
     *
     *
     *  Framebuffer as seen on User buffer (PixelBufferDescriptor&)
     *  screen
     *
     *      +--------------------+
     *      |                    |                .stride         .alignment
     *      |                    |         ----------------------->-->
     *      |                    |         O----------------------+--+   low addresses
     *      |                    |         |          |           |  |
     *      |             w      |         |          | .top      |  |
     *      |       <--------->  |         |          V           |  |
     *      |       +---------+  |         |     +---------+      |  |
     *      |       |     ^   |  | ======> |     |         |      |  |
     *      |   x   |    h|   |  |         |.left|         |      |  |
     *      +------>|     v   |  |         +---->|         |      |  |
     *      |       +.........+  |         |     +.........+      |  |
     *      |            ^       |         |                      |  |
     *      |          y |       |         +----------------------+--+  high addresses
     *      O------------+-------+
     *
     *
     * Typically readPixels() will be called after render() and before endFrame().
     *
     * After issuing this method, the callback associated with `buffer` will be invoked on the
     * main thread, indicating that the read-back has completed. Typically, this will happen
     * after multiple calls to beginFrame(), render(), endFrame().
     *
     * It is also possible to use a Fence to wait for the read-back.
     *
     * OpenGL only: if issuing a readPixels on a RenderTarget backed by a Texture that had data
     * uploaded to it via setImage, the data returned from readPixels will be y-flipped with respect
     * to the setImage call.
     *
     * Note: the texture that backs the COLOR attachment for `renderTarget` must have
     * TextureUsage::BLIT_SRC as part of its usage.
     *
     * @remark
     * readPixels() is intended for debugging and testing. It will impact performance significantly.
     *
     */
    /**
     * 读取提供的 RenderTarget 的内容
     *
     * @param renderTarget  要读取的 RenderTarget
     * @param xoffset       要读取的子区域的左偏移量
     * @param yoffset       要读取的子区域的底偏移量
     * @param width         要读取的子区域的宽度
     * @param height        要读取的子区域的高度
     * @param buffer        将写入读取内容的客户端缓冲区
     *
     * 始终支持以下格式：
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA
     *                      - PixelBufferDescriptor::PixelDataFormat::RGBA_INTEGER
     *
     * 始终支持以下类型：
     *                      - PixelBufferDescriptor::PixelDataType::UBYTE
     *                      - PixelBufferDescriptor::PixelDataType::UINT
     *                      - PixelBufferDescriptor::PixelDataType::INT
     *                      - PixelBufferDescriptor::PixelDataType::FLOAT
     *
     * 可能支持其他格式/类型组合。如果不支持某个组合，
     * 此操作可能会静默失败。使用 DEBUG 构建获取有关失败的日志。
     *
     * 通常 readPixels() 将在 render() 之后和 endFrame() 之前调用。
     *
     * 发出此方法后，将在主线程上调用与 `buffer` 关联的回调，
     * 指示读取已完成。通常，这将在多次调用 beginFrame()、render()、endFrame() 后发生。
     *
     * 也可以使用 Fence 等待读取完成。
     *
     * 仅 OpenGL：如果对由通过 setImage 上传数据的 Texture 支持的 RenderTarget 发出 readPixels，
     * 从 readPixels 返回的数据将相对于 setImage 调用进行 y 翻转。
     *
     * 注意：为 `renderTarget` 的 COLOR 附件提供支持的纹理必须在其 usage 中包含 TextureUsage::BLIT_SRC。
     *
     * @remark
     * readPixels() 用于调试和测试。它会显著影响性能。
     */
    void readPixels(RenderTarget* UTILS_NONNULL renderTarget,
            uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            backend::PixelBufferDescriptor&& buffer);

    /**
     * Render a standalone View into its associated RenderTarget
     *
     * This call is mostly equivalent to calling render(View*) inside a
     * beginFrame / endFrame block, but incurs less overhead. It can be used
     * as a poor man's compute API.
     *
     * @param view A pointer to the view to render. This View must have a RenderTarget associated
     *             to it.
     *
     * @attention
     * renderStandaloneView() must be called outside of beginFrame() / endFrame().
     *
     * @note
     * renderStandaloneView() must be called from the Engine's main thread
     * (or external synchronization must be provided). In particular, calls to
     * renderStandaloneView() on different Renderer instances **must** be synchronized.
     *
     * @remark
     * renderStandaloneView() perform potentially heavy computations and cannot be multi-threaded.
     * However, internally, renderStandaloneView() is highly multi-threaded to both improve
     * performance in mitigate the call's latency.
     */
    /**
     * 将独立的 View 渲染到其关联的 RenderTarget 中
     *
     * 此调用基本等同于在 beginFrame / endFrame 块内调用 render(View*)，
     * 但开销更少。可用作简单的计算 API。
     *
     * @param view 要渲染的 view 的指针。此 View 必须有关联的 RenderTarget。
     *
     * @attention
     * renderStandaloneView() 必须在 beginFrame() / endFrame() 之外调用。
     *
     * @note
     * renderStandaloneView() 必须从 Engine 的主线程调用
     * （或必须提供外部同步）。特别是，在不同 Renderer 实例上调用
     * renderStandaloneView() **必须** 同步。
     *
     * @remark
     * renderStandaloneView() 执行可能繁重的计算，不能多线程化。
     * 但是，内部，renderStandaloneView() 高度多线程化，以提高性能并减轻调用的延迟。
     */
    void renderStandaloneView(View const* UTILS_NONNULL view);


    /**
     * Returns the time in second of the last call to beginFrame(). This value is constant for all
     * views rendered during a frame. The epoch is set with resetUserTime().
     *
     * In materials, this value can be queried using `vec4 getUserTime()`. The value returned
     * is a highp vec4 encoded as follows:
     *
     *      time.x = (float)Renderer.getUserTime();
     *      time.y = Renderer.getUserTime() - time.x;
     *
     * It follows that the following invariants are true:
     *
     *      (double)time.x + (double)time.y == Renderer.getUserTime()
     *      time.x == (float)Renderer.getUserTime()
     *
     * This encoding allows the shader code to perform high precision (i.e. double) time
     * calculations when needed despite the lack of double precision in the shader, for e.g.:
     *
     *      To compute (double)time * vertex in the material, use the following construct:
     *
     *              vec3 result = time.x * vertex + time.y * vertex;
     *
     *
     * Most of the time, high precision computations are not required, but be aware that the
     * precision of time.x rapidly diminishes as time passes:
     *
     *          time    | precision
     *          --------+----------
     *          16.7s   |    us
     *          4h39    |    ms
     *         77h      |   1/60s
     *
     *
     * In other words, it only possible to get microsecond accuracy for about 16s or millisecond
     * accuracy for just under 5h.
     *
     * This problem can be mitigated by calling resetUserTime(), or using high precision time as
     * described above.
     *
     * @return The time is seconds since resetUserTime() was last called.
     *
     * @see
     * resetUserTime()
     */
    /**
     * 返回最后一次调用 beginFrame() 的时间（秒）。该值对于帧期间渲染的所有
     * view 都是恒定的。纪元通过 resetUserTime() 设置。
     *
     * 在材质中，可以使用 `vec4 getUserTime()` 查询此值。返回的值
     * 是一个 highp vec4，编码如下：
     *
     *      time.x = (float)Renderer.getUserTime();
     *      time.y = Renderer.getUserTime() - time.x;
     *
     * 因此以下不变量为真：
     *
     *      (double)time.x + (double)time.y == Renderer.getUserTime()
     *      time.x == (float)Renderer.getUserTime()
     *
     * 这种编码允许着色器代码在需要时执行高精度（即双精度）时间
     * 计算，尽管着色器中缺少双精度，例如：
     *
     *      要在材质中计算 (double)time * vertex，请使用以下构造：
     *
     *              vec3 result = time.x * vertex + time.y * vertex;
     *
     *
     * 大多数情况下，不需要高精度计算，但请注意，time.x 的精度
     * 会随着时间流逝而迅速降低：
     *
     *          时间    | 精度
     *          --------+----------
     *          16.7s   |    微秒
     *          4h39    |    毫秒
     *         77h      |   1/60s
     *
     *
     * 换句话说，只能在约 16 秒内获得微秒精度，或在不到 5 小时内获得毫秒精度。
     *
     * 可以通过调用 resetUserTime() 或使用如上所述的高精度时间来缓解此问题。
     *
     * @return 自上次调用 resetUserTime() 以来的时间（秒）
     *
     * @see resetUserTime()
     */
    double getUserTime() const;

    /**
     * Sets the user time epoch to now, i.e. resets the user time to zero.
     *
     * Use this method used to keep the precision of time high in materials, in practice it should
     * be called at least when the application is paused, e.g. Activity.onPause() in Android.
     *
     * @see
     * getUserTime()
     */
    /**
     * 将用户时间纪元设置为当前时间，即将用户时间重置为零
     *
     * 使用此方法以在材质中保持高时间精度，实际上应该
     * 至少在应用程序暂停时调用，例如 Android 中的 Activity.onPause()。
     *
     * @see getUserTime()
     */
    void resetUserTime();


    /**
     * Requests the next frameCount frames to be skipped. For Debugging.
     * @param frameCount number of frames to skip.
     */
    /**
     * 请求跳过接下来的 frameCount 帧。用于调试。
     * @param frameCount 要跳过的帧数
     */
    void skipNextFrames(size_t frameCount) const noexcept;

    /**
     * Remainder count of frame to be skipped
     * @return remaining frames to be skipped
     */
    /**
     * 剩余待跳过的帧数
     * @return 剩余待跳过的帧数
     */
    size_t getFrameToSkipCount() const noexcept;

protected:
    // prevent heap allocation
    ~Renderer() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_RENDERER_H
