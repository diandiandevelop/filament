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

#ifndef TNT_FILAMENT_DETAILS_RENDERER_H
#define TNT_FILAMENT_DETAILS_RENDERER_H

#include "downcast.h"

#include "Allocators.h"
#include "FrameInfo.h"
#include "FrameSkipper.h"
#include "PostProcessManager.h"
#include "RenderPass.h"

#include "details/SwapChain.h"

#include "backend/DriverApiForward.h"

#include <filament/Renderer.h>
#include <filament/Viewport.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/Allocator.h>
#include <utils/FixedCapacityVector.h>

#include <math/vec4.h>

#include <tsl/robin_set.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class ResourceAllocator;

namespace backend {
class Driver;
} // namespace backend

class FEngine;
class FRenderTarget;
class FView;

/**
 * 渲染器实现类
 * 
 * Renderer 接口的具体实现。
 * 渲染器负责渲染视图和管理帧。
 * 
 * 实现细节：
 * - 管理帧历史和帧跳过
 * - 处理后处理
 * - 管理渲染通道
 * - 支持独立视图渲染
 */
/*
 * A concrete implementation of the Renderer Interface.
 */
class FRenderer : public Renderer {
    /**
     * 最大帧时间历史大小
     */
    static constexpr unsigned MAX_FRAMETIME_HISTORY = 32u;  // 最大帧时间历史大小

public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FRenderer(FEngine& engine);

    /**
     * 析构函数
     */
    ~FRenderer() noexcept;

    /**
     * 终止渲染器
     * 
     * 清理资源。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 获取引擎
     * 
     * @return 引擎引用
     */
    FEngine& getEngine() const noexcept { return mEngine; }

    /**
     * 获取着色器用户时间
     * 
     * 返回传递给着色器的用户时间（float4）。
     * 
     * @return 着色器用户时间
     */
    math::float4 getShaderUserTime() const { return mShaderUserTime; }

    /**
     * 重置用户时间
     * 
     * 将用户时间重置为 0。
     */
    void resetUserTime();

    /**
     * 跳过接下来的帧
     * 
     * @param frameCount 要跳过的帧数
     */
    void skipNextFrames(size_t frameCount) const noexcept {
        mFrameSkipper.skipNextFrames(frameCount);  // 跳过帧
    }

    /**
     * 获取要跳过的帧数
     * 
     * @return 要跳过的帧数
     */
    size_t getFrameToSkipCount() const noexcept {
        return mFrameSkipper.getFrameToSkipCount();  // 返回要跳过的帧数
    }

    /**
     * 渲染独立视图
     * 
     * 渲染单个独立视图。视图必须有一个自定义渲染目标。
     * 
     * @param view 视图常量指针
     */
    // renders a single standalone view. The view must have a a custom rendertarget.
    void renderStandaloneView(FView const* view);

    /**
     * 设置呈现时间
     * 
     * 设置帧的呈现时间（单调时钟纳秒）。
     * 
     * @param monotonic_clock_ns 单调时钟纳秒
     */
    void setPresentationTime(int64_t monotonic_clock_ns) const;

    /**
     * 设置垂直同步时间
     * 
     * 设置垂直同步时间（稳定时钟纳秒）。
     * 
     * @param steadyClockTimeNano 稳定时钟纳秒
     */
    void setVsyncTime(uint64_t steadyClockTimeNano) noexcept;

    /**
     * 跳过一帧
     * 
     * 跳过当前帧的渲染。
     * 
     * @param vsyncSteadyClockTimeNano 垂直同步稳定时钟纳秒
     */
    // skip a frame
    void skipFrame(uint64_t vsyncSteadyClockTimeNano);

    /**
     * 是否应该渲染帧
     * 
     * 根据帧跳过逻辑判断是否应该渲染当前帧。
     * 
     * @return 如果应该渲染返回 true，否则返回 false
     */
    // Whether a frame should be rendered or not.
    bool shouldRenderFrame() const noexcept;

    /**
     * 开始帧
     * 
     * 开始新的一帧渲染。
     * 
     * @param swapChain 交换链指针
     * @param vsyncSteadyClockTimeNano 垂直同步稳定时钟纳秒
     * @return 如果成功开始帧返回 true，否则返回 false
     */
    // start a frame
    bool beginFrame(FSwapChain* swapChain, uint64_t vsyncSteadyClockTimeNano);

    /**
     * 结束帧
     * 
     * 结束当前帧的渲染并提交。
     */
    // end a frame
    void endFrame();

    /**
     * 渲染视图
     * 
     * 渲染一个视图。必须在 beginFrame/endFrame 之间调用。
     * 
     * @param view 视图常量指针
     */
    // render a view. must be called between beginFrame/enfFrame.
    void render(FView const* view);

    /**
     * 从当前交换链读取像素
     * 
     * 从当前交换链读取像素数据。
     * 必须在 beginFrame/endFrame 之间调用。
     * 
     * @param xoffset X 偏移量
     * @param yoffset Y 偏移量
     * @param width 宽度
     * @param height 高度
     * @param buffer 像素缓冲区描述符（会被移动）
     */
    // read pixel from the current swapchain. must be called between beginFrame/enfFrame.
    void readPixels(uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            backend::PixelBufferDescriptor&& buffer);

    /**
     * 从渲染目标读取像素
     * 
     * 从指定渲染目标读取像素数据。
     * 必须在 beginFrame/endFrame 之间调用。
     * 
     * @param renderTarget 渲染目标常量指针
     * @param xoffset X 偏移量
     * @param yoffset Y 偏移量
     * @param width 宽度
     * @param height 高度
     * @param buffer 像素缓冲区描述符（会被移动）
     */
    // read pixel from a rendertarget. must be called between beginFrame/enfFrame.
    void readPixels(FRenderTarget const* renderTarget,
            uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            backend::PixelBufferDescriptor&& buffer);

    /**
     * 复制帧
     * 
     * 将当前交换链的内容复制到另一个交换链。
     * 
     * @param dstSwapChain 目标交换链指针
     * @param dstViewport 目标视口
     * @param srcViewport 源视口
     * @param flags 复制标志
     */
    // blits the current swapchain to another one
    void copyFrame(FSwapChain* dstSwapChain, Viewport const& dstViewport,
            Viewport const& srcViewport, CopyFrameFlag flags);


    /**
     * 设置显示信息
     * 
     * @param info 显示信息
     */
    void setDisplayInfo(DisplayInfo const& info) noexcept {
        mDisplayInfo.refreshRate = info.refreshRate;  // 设置刷新率
    }

    /**
     * 设置帧率选项
     * 
     * 设置帧率控制选项，包括历史大小、间隔和头部空间比例。
     * 
     * @param options 帧率选项
     */
    void setFrameRateOptions(FrameRateOptions const& options) noexcept {
        FrameRateOptions& frameRateOptions = mFrameRateOptions;  // 获取帧率选项引用
        frameRateOptions = options;  // 设置选项

        /**
         * 历史不能超过 31 帧（约 0.5 秒），使其为奇数
         */
        // History can't be more than 31 frames (~0.5s), make it odd.
        frameRateOptions.history = std::min(frameRateOptions.history / 2u * 2u + 1u,
                MAX_FRAMETIME_HISTORY);  // 限制历史大小并使其为奇数

        /**
         * 历史必须至少为 3 帧
         */
        // History must at least be 3 frames
        frameRateOptions.history = std::max(frameRateOptions.history, uint8_t(3));  // 确保至少 3 帧

        frameRateOptions.interval = std::max(uint8_t(1), frameRateOptions.interval);  // 间隔至少为 1

        /**
         * 头部空间不能大于帧时间，或小于 0
         */
        // headroom can't be larger than frame time, or less than 0
        frameRateOptions.headRoomRatio = std::min(frameRateOptions.headRoomRatio, 1.0f);  // 限制最大值为 1.0
        frameRateOptions.headRoomRatio = std::max(frameRateOptions.headRoomRatio, 0.0f);  // 限制最小值为 0.0
    }

    /**
     * 设置清除选项
     * 
     * @param options 清除选项
     */
    void setClearOptions(const ClearOptions& options) {
        mClearOptions = options;  // 设置清除选项
    }

    /**
     * 获取清除选项
     * 
     * @return 清除选项常量引用
     */
    ClearOptions const& getClearOptions() const noexcept {
        return mClearOptions;  // 返回清除选项
    }

    /**
     * 获取帧信息历史
     * 
     * 获取指定大小的帧信息历史。
     * 
     * @param historySize 历史大小
     * @return 帧信息历史向量
     */
    utils::FixedCapacityVector<FrameInfo> getFrameInfoHistory(size_t const historySize) const noexcept {
        return mFrameInfoManager.getFrameInfoHistory(historySize);  // 获取帧信息历史
    }

    /**
     * 获取最大帧历史大小
     * 
     * @return 最大帧历史大小
     */
    size_t getMaxFrameHistorySize() const noexcept {
        return MAX_FRAMETIME_HISTORY;  // 返回最大帧时间历史大小
    }

private:
    friend class Renderer;  // 允许 Renderer 访问私有成员
    
    using Command = RenderPass::Command;  // 命令类型别名
    using clock = std::chrono::steady_clock;  // 时钟类型别名
    using Epoch = clock::time_point;  // 时间点类型别名
    using duration = clock::duration;  // 持续时间类型别名

    /**
     * 获取清除标志
     * 
     * @return 清除标志
     */
    backend::TargetBufferFlags getClearFlags() const noexcept;
    
    /**
     * 初始化清除标志
     */
    void initializeClearFlags() noexcept;

    /**
     * 获取 HDR 格式
     * 
     * @param view 视图引用
     * @param translucent 是否半透明
     * @return HDR 纹理格式
     */
    backend::TextureFormat getHdrFormat(const FView& view, bool translucent) const noexcept;
    
    /**
     * 获取 LDR 格式
     * 
     * @param translucent 是否半透明
     * @return LDR 纹理格式
     */
    backend::TextureFormat getLdrFormat(bool translucent) const noexcept;

    /**
     * 获取用户纪元
     * 
     * @return 用户纪元时间点
     */
    Epoch getUserEpoch() const { return mUserEpoch; }
    
    /**
     * 获取用户时间
     * 
     * 计算从用户纪元到现在的经过时间（秒）。
     * 
     * @return 用户时间（秒，双精度）
     */
    double getUserTime() const noexcept {
        duration const d = clock::now() - getUserEpoch();  // 计算持续时间
        /**
         * 将持续时间（无论是什么类型）转换为以双精度编码的秒数
         */
        // convert the duration (whatever it is) to a duration in seconds encoded as double
        return std::chrono::duration<double>(d).count();  // 转换为秒数
    }

    /**
     * 获取渲染目标
     * 
     * 获取视图的渲染目标句柄和标志。
     * 
     * @param view 视图引用
     * @return 渲染目标句柄和标志的配对
     */
    std::pair<backend::Handle<backend::HwRenderTarget>, backend::TargetBufferFlags>
            getRenderTarget(FView const& view) const noexcept;

    /**
     * 记录高水位标记
     * 
     * 记录命令缓冲区的高水位标记（最大使用量）。
     * 
     * @param watermark 水位标记
     */
    void recordHighWatermark(size_t const watermark) noexcept {
        mCommandsHighWatermark = std::max(mCommandsHighWatermark, watermark);  // 更新高水位标记
    }

    /**
     * 获取命令高水位标记
     * 
     * @return 命令高水位标记
     */
    size_t getCommandsHighWatermark() const noexcept {
        return mCommandsHighWatermark;  // 返回高水位标记
    }

    /**
     * 内部渲染方法
     * 
     * @param view 视图常量指针
     * @param flush 是否刷新
     */
    void renderInternal(FView const* view, bool flush);
    
    /**
     * 渲染作业
     * 
     * 在作业系统中执行渲染任务。
     * 
     * @param rootArenaScope 根内存池作用域
     * @param view 视图引用
     */
    void renderJob(RootArenaScope& rootArenaScope, FView& view);

    /**
     * 准备上采样器
     * 
     * 根据 TAA 和动态分辨率选项准备上采样器参数。
     * 
     * @param scale 缩放值
     * @param taaOptions 时间抗锯齿选项
     * @param dsrOptions 动态分辨率选项
     * @return 上采样器参数对（缩放和偏移）
     */
    static std::pair<float, math::float2> prepareUpscaler(math::float2 scale,
            TemporalAntiAliasingOptions const& taaOptions,
            DynamicResolutionOptions const& dsrOptions);

    /**
     * 保持对引擎的引用
     */
    // keep a reference to our engine
    FEngine& mEngine;  // 引擎引用
    FrameSkipper mFrameSkipper;  // 帧跳过器
    backend::Handle<backend::HwRenderTarget> mRenderTargetHandle;  // 渲染目标句柄
    FSwapChain* mSwapChain = nullptr;  // 交换链指针
    size_t mCommandsHighWatermark = 0;  // 命令高水位标记
    uint32_t mFrameId = 1;  // 帧 ID（ID 0 保留给独立视图）
    FrameInfoManager mFrameInfoManager;  // 帧信息管理器
    backend::TextureFormat mHdrTranslucent;  // HDR 半透明格式
    backend::TextureFormat mHdrQualityMedium;  // HDR 中等质量格式
    backend::TextureFormat mHdrQualityHigh;  // HDR 高质量格式
    bool mIsRGB8Supported : 1;  // 是否支持 RGB8 格式
    Epoch mUserEpoch;  // 用户纪元
    math::float4 mShaderUserTime{};  // 着色器用户时间
    DisplayInfo mDisplayInfo;  // 显示信息
    FrameRateOptions mFrameRateOptions;  // 帧率选项
    ClearOptions mClearOptions;  // 清除选项
    backend::TargetBufferFlags mDiscardStartFlags{};  // 开始丢弃标志
    backend::TargetBufferFlags mClearFlags{};  // 清除标志
    tsl::robin_set<FRenderTarget*> mPreviousRenderTargets;  // 之前的渲染目标集合
    std::function<void()> mBeginFrameInternal;  // 开始帧内部函数
    uint64_t mVsyncSteadyClockTimeNano = 0;  // 垂直同步稳定时钟纳秒
    std::unique_ptr<ResourceAllocator> mResourceAllocator{};  // 资源分配器唯一指针
};

FILAMENT_DOWNCAST(Renderer)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_RENDERER_H
