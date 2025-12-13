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

#include "details/View.h"

#include "Allocators.h"
#include "BufferPoolAllocator.h"
#include "Culler.h"
#include "DebugRegistry.h"
#include "FrameHistory.h"
#include "FrameInfo.h"
#include "Froxelizer.h"
#include "RenderPrimitive.h"
#include "ResourceAllocator.h"
#include "ShadowMap.h"
#include "ShadowMapManager.h"

#include "components/TransformManager.h"

#include "details/Engine.h"
#include "details/IndirectLight.h"
#include "details/InstanceBuffer.h"
#include "details/RenderTarget.h"
#include "details/Renderer.h"
#include "details/Scene.h"
#include "details/Skybox.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <fg/FrameGraphTexture.h>
#include <fg/FrameGraphId.h>

#include <filament/Exposure.h>
#include <filament/Frustum.h>
#include <filament/DebugRegistry.h>
#include <filament/View.h>

#include <private/filament/UibStructs.h>
#include <private/filament/EngineEnums.h>

#include <private/utils/Tracing.h>

#include <utils/architecture.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>
#include <utils/Range.h>
#include <utils/Slice.h>
#include <utils/Zip2Iterator.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/scalar.h>

#include <assert.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <functional>
#include <memory>
#include <new>
#include <ratio>
#include <utility>

using namespace utils;

namespace filament {

using namespace backend;
using namespace math;

/**
 * PID 控制器积分项系数
 * 
 * 用于动态分辨率控制的 PID 控制器积分项系数。
 */
static constexpr float PID_CONTROLLER_Ki = 0.002f;

/**
 * PID 控制器微分项系数
 * 
 * 用于动态分辨率控制的 PID 控制器微分项系数（当前未使用）。
 */
static constexpr float PID_CONTROLLER_Kd = 0.0f;

/**
 * 视图构造函数
 * 
 * 初始化视图的所有组件，包括描述符集、Froxelizer、雾实体、立体支持检测等。
 * 
 * @param engine 引擎引用
 */
FView::FView(FEngine& engine)
        : mCommonRenderableDescriptorSet("mCommonRenderableDescriptorSet",  // 通用可渲染对象描述符集（名称和布局）
                engine.getPerRenderableDescriptorSetLayout()),
          mFroxelizer(engine),  // Froxelizer（用于光照计算）
          mFogEntity(engine.getEntityManager().create()),  // 雾实体
          mIsStereoSupported(engine.getDriverApi().isStereoSupported()),  // 立体渲染支持检测
          mUniforms(engine.getDriverApi()),  // 统一缓冲区管理器
          mColorPassDescriptorSet{  // 颜色通道描述符集（两个变体：深度和非深度）
                { engine, false, mUniforms },  // 非深度变体
                { engine, true, mUniforms } },  // 深度变体
          mSharedState(std::make_shared<SharedState>())  // 共享状态（用于多线程）
{
    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API

    auto const& layout = engine.getPerRenderableDescriptorSetLayout();  // 获取每个可渲染对象的描述符集布局

    /**
     * 使用虚拟描述符初始化通用描述符集
     * 
     * 这些虚拟描述符用于可渲染对象，当它们没有骨骼、变形等数据时使用。
     */
    // initialize the common descriptor set with dummy descriptors
    mCommonRenderableDescriptorSet.setBuffer(layout,  // 设置骨骼统一缓冲区（虚拟）
            +PerRenderableBindingPoints::BONES_UNIFORMS,
            engine.getDummyUniformBuffer(), 0, sizeof(PerRenderableBoneUib));

    mCommonRenderableDescriptorSet.setBuffer(layout,  // 设置变形统一缓冲区（虚拟）
            +PerRenderableBindingPoints::MORPHING_UNIFORMS,
            engine.getDummyUniformBuffer(), 0, sizeof(PerRenderableMorphingUib));

    mCommonRenderableDescriptorSet.setSampler(layout,  // 设置变形目标位置采样器（虚拟）
            +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,
            engine.getDummyMorphTargetBuffer()->getPositionsHandle(), {});

    mCommonRenderableDescriptorSet.setSampler(layout,  // 设置变形目标切线采样器（虚拟）
            +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,
            engine.getDummyMorphTargetBuffer()->getTangentsHandle(), {});

    mCommonRenderableDescriptorSet.setSampler(layout,  // 设置骨骼索引和权重采样器（虚拟，使用零纹理）
            +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS,
            engine.getZeroTexture(), {});


    FDebugRegistry& debugRegistry = engine.getDebugRegistry();  // 获取调试注册表

    /**
     * 注册调试属性：相机是否在原点
     */
    debugRegistry.registerProperty("d.view.camera_at_origin",
            &engine.debug.view.camera_at_origin);

    /**
     * 积分项用于对抗下面的死区，我们限制它可以作用的范围。
     */
    // The integral term is used to fight back the dead-band below, we limit how much it can act.
    mPidController.setIntegralLimits(-100.0f, 100.0f);  // 设置积分项限制

    /**
     * 死区：向下缩放 1%，向上缩放 5%。这稳定了所有抖动。
     */
    // Dead-band, 1% for scaling down, 5% for scaling up. This stabilizes all the jitters.
    mPidController.setOutputDeadBand(-0.01f, 0.05f);  // 设置输出死区

#ifndef NDEBUG
    // This can fail if another view has already registered this data source
    mDebugState->owner = debugRegistry.registerDataSource("d.view.frame_info",
            [weak = std::weak_ptr(mDebugState)]() -> DebugRegistry::DataSource {
                // the View could have been destroyed by the time we do this
                auto const state = weak.lock();
                if (!state) {
                    return { nullptr, 0 };
                }
                // Lazily allocate the buffer for the debug data source, and mark this
                // data source as active. It can never go back to inactive.
                assert_invariant(!state->debugFrameHistory);
                state->active = true;
                state->debugFrameHistory =
                        std::make_unique<std::array<DebugRegistry::FrameHistory, 5 * 60>>();
                return { state->debugFrameHistory->data(), state->debugFrameHistory->size() };
            });

    if (UTILS_UNLIKELY(mDebugState->owner)) {
        // publish the properties (they will be initialized in the main loop)
        debugRegistry.registerProperty("d.view.pid.kp", &engine.debug.view.pid.kp);
        debugRegistry.registerProperty("d.view.pid.ki", &engine.debug.view.pid.ki);
        debugRegistry.registerProperty("d.view.pid.kd", &engine.debug.view.pid.kd);
    }
#endif

#if FILAMENT_ENABLE_FGVIEWER
    fgviewer::DebugServer* fgviewerServer = engine.debug.fgviewerServer;
    if (UTILS_LIKELY(fgviewerServer)) {
        mFrameGraphViewerViewHandle =
            fgviewerServer->createView(utils::CString(getName()));
    }
#endif

    /**
     * 分配统一缓冲区对象（UBO）
     */
    // allocate UBOs
    mLightUbh = driver.createBufferObject(CONFIG_MAX_LIGHT_COUNT * sizeof(LightsUib),  // 创建光源统一缓冲区
            BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);

    /**
     * 检查是否支持动态分辨率
     * 
     * 动态分辨率需要帧时间查询支持。
     */
    mIsDynamicResolutionSupported = driver.isFrameTimeSupported();  // 检查帧时间支持

    /**
     * 设置默认颜色分级
     */
    mDefaultColorGrading = mColorGrading = engine.getDefaultColorGrading();  // 设置默认和当前颜色分级

    /**
     * 初始化颜色通道描述符集
     * 
     * 为每个变体（深度和非深度）初始化描述符集。
     */
    for (auto&& colorPassDescriptorSet : mColorPassDescriptorSet) {
        colorPassDescriptorSet.init(  // 初始化描述符集
                engine,
                mLightUbh,  // 光源统一缓冲区句柄
                mFroxelizer.getRecordBuffer(),  // Froxel 记录缓冲区
                mFroxelizer.getFroxelBuffer());  // Froxel 缓冲区
    }
}

/**
 * 视图析构函数
 */
FView::~FView() noexcept = default;

/**
 * 终止视图
 * 
 * 释放所有分配的资源。
 * 
 * @param engine 引擎引用
 */
void FView::terminate(FEngine& engine) {
    /**
     * 这里我们会清理分配的资源，或我们拥有的资源（目前没有）。
     */
    // Here we would cleanly free resources we've allocated, or we own (currently none).

    clearPickingQueries();  // 清除拾取查询

    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyBufferObject(mLightUbh);  // 销毁光源统一缓冲区
    driver.destroyBufferObject(mRenderableUbh);  // 销毁可渲染对象统一缓冲区
    clearFrameHistory(engine);  // 清除帧历史

    ShadowMapManager::terminate(engine, mShadowMapManager);  // 终止阴影贴图管理器
    mUniforms.terminate(driver);  // 终止统一缓冲区管理器
    for (auto&& colorPassDescriptorSet : mColorPassDescriptorSet) {
        colorPassDescriptorSet.terminate(engine.getDescriptorSetLayoutFactory(), driver);  // 终止颜色通道描述符集
    }
    mFroxelizer.terminate(driver);  // 终止 Froxelizer
    mCommonRenderableDescriptorSet.terminate(driver);  // 终止通用可渲染对象描述符集

    engine.getEntityManager().destroy(mFogEntity);  // 销毁雾实体

#ifndef NDEBUG
    if (UTILS_UNLIKELY(mDebugState->owner)) {
        engine.getDebugRegistry().unregisterDataSource("d.view.frame_info");
    }
#endif

#if FILAMENT_ENABLE_FGVIEWER
    fgviewer::DebugServer* fgviewerServer = engine.debug.fgviewerServer;
    if (UTILS_LIKELY(fgviewerServer)) {
        fgviewerServer->destroyView(mFrameGraphViewerViewHandle);
    }
#endif
}

/**
 * 设置视口
 * 
 * 设置渲染视口的大小和位置。
 * 
 * @param viewport 视口结构（包含 x, y, width, height）
 */
void FView::setViewport(filament::Viewport const& viewport) noexcept {
    /**
     * 捕获用户发生下溢但未捕获的情况。
     */
    // catch the cases were user had an underflow and didn't catch it.
    assert(int32_t(viewport.width) > 0);  // 断言宽度大于 0
    assert(int32_t(viewport.height) > 0);  // 断言高度大于 0
    mViewport = viewport;  // 设置视口
}

/**
 * 设置动态分辨率选项
 * 
 * 配置动态分辨率系统，根据性能自动调整渲染分辨率。
 * 
 * @param options 动态分辨率选项
 */
void FView::setDynamicResolutionOptions(DynamicResolutionOptions const& options) noexcept {
    DynamicResolutionOptions& dynamicResolution = mDynamicResolution;  // 获取动态分辨率选项引用
    dynamicResolution = options;  // 复制选项

    /**
     * 只有在支持动态分辨率或实际上不是动态的情况下才启用
     * （如果 minScale == maxScale，则不是动态的）
     */
    // only enable if dynamic resolution is supported or if it's not actually dynamic
    dynamicResolution.enabled = dynamicResolution.enabled &&
            (mIsDynamicResolutionSupported || dynamicResolution.minScale == dynamicResolution.maxScale);
    if (dynamicResolution.enabled) {  // 如果启用
        /**
         * 如果启用，清理参数
         */
        // if enabled, sanitize the parameters

        /**
         * minScale 不能为 0 或负数
         */
        // minScale cannot be 0 or negative
        dynamicResolution.minScale = max(dynamicResolution.minScale, float2(1.0f / 1024.0f));  // 限制最小缩放

        /**
         * maxScale 不能 < minScale
         */
        // maxScale cannot be < minScale
        dynamicResolution.maxScale = max(dynamicResolution.maxScale, dynamicResolution.minScale);  // 确保 maxScale >= minScale

        /**
         * 将 maxScale 限制为 2x，因为我们使用双线性过滤，所以超采样
         * 超过这个值没有用处。
         */
        // clamp maxScale to 2x because we're doing bilinear filtering, so super-sampling
        // is not useful above that.
        dynamicResolution.maxScale = min(dynamicResolution.maxScale, float2(2.0f));  // 限制最大缩放为 2x

        dynamicResolution.sharpness = clamp(dynamicResolution.sharpness, 0.0f, 2.0f);  // 限制锐度在 0.0-2.0 之间
    }
}

/**
 * 设置动态光照选项
 * 
 * 设置光源的近远平面，用于 Froxelizer 的光照计算。
 * 
 * @param zLightNear 光源近平面
 * @param zLightFar 光源远平面
 */
void FView::setDynamicLightingOptions(float const zLightNear, float const zLightFar) noexcept {
    mFroxelizer.setOptions(zLightNear, zLightFar);  // 设置 Froxelizer 选项
}

/**
 * 更新缩放比例
 * 
 * 根据帧时间和目标帧率使用 PID 控制器更新动态分辨率缩放比例。
 * 
 * @param engine 引擎引用
 * @param info 帧信息（包含帧时间等）
 * @param frameRateOptions 帧率选项（包含目标帧率和缩放速率）
 * @param displayInfo 显示信息（包含刷新率等）
 * @return 新的缩放比例（float2，x 和 y 方向）
 */
float2 FView::updateScale(FEngine& engine,
        filament::details::FrameInfo const& info,
        Renderer::FrameRateOptions const& frameRateOptions,
        Renderer::DisplayInfo const& displayInfo) noexcept {

#ifndef NDEBUG
    if (UTILS_LIKELY(!mDebugState->active)) {  // 如果调试状态未激活
        /**
         * 如果我们未激活，使用正常值更新调试属性
         * 并使用这些值配置 PID 控制器。
         */
        // if we're not active, update the debug properties with the normal values
        // and use that for configuring the PID controller.
        engine.debug.view.pid.kp = 1.0f - std::exp(-frameRateOptions.scaleRate);  // 计算比例项系数
        engine.debug.view.pid.ki = PID_CONTROLLER_Ki;  // 设置积分项系数
        engine.debug.view.pid.kd = PID_CONTROLLER_Kd;  // 设置微分项系数
    }
#endif

    DynamicResolutionOptions const& options = mDynamicResolution;  // 获取动态分辨率选项
    if (options.enabled) {  // 如果启用动态分辨率
        /**
         * 如果不支持时间查询，info.valid 将始终为 false；但在这种情况下
         * 我们保证 minScale == maxScale。
         */
        // if timerQueries are not supported, info.valid will always be false; but in that case
        // we're guaranteed that minScale == maxScale.
        if (!UTILS_UNLIKELY(info.valid)) {  // 如果帧信息无效（不支持时间查询）
            /**
             * 始终限制在最小/最大缩放范围内
             */
            // always clamp to the min/max scale range
            mScale = clamp(1.0f, options.minScale, options.maxScale);  // 限制缩放比例
            return mScale;  // 返回缩放比例
        }

#ifndef NDEBUG
        /**
         * 调试模式：使用调试属性中的 PID 系数
         */
        const float Kp = engine.debug.view.pid.kp;  // 比例项系数（调试）
        const float Ki = engine.debug.view.pid.ki;  // 积分项系数（调试）
        const float Kd = engine.debug.view.pid.kd;  // 微分项系数（调试）
#else
        /**
         * 发布模式：使用计算出的 PID 系数
         */
        const float Kp = (1.0f - std::exp(-frameRateOptions.scaleRate));  // 计算比例项系数
        const float Ki = PID_CONTROLLER_Ki;  // 积分项系数
        const float Kd = PID_CONTROLLER_Kd;  // 微分项系数
#endif
        mPidController.setParallelGains(Kp, Ki, Kd);  // 设置 PID 控制器增益（并行形式）

        /**
         * 以下所有值以毫秒为单位
         */
        // all values in ms below
        using std::chrono::duration;
        const float dt = 1.0f;  // 我们这里不需要 dt，设置为 1 意味着我们的参数以"帧"为单位
        // we don't really need dt here, setting it to 1, means our parameters are in "frames"
        const float target = (1000.0f * float(frameRateOptions.interval)) / displayInfo.refreshRate;  // 目标帧时间（毫秒）
        const float targetWithHeadroom = target * (1.0f - frameRateOptions.headRoomRatio);  // 带余量的目标帧时间
        float const measured = duration{ info.denoisedFrameTime }.count();  // 测量的帧时间（去噪后，毫秒）
        float const out = mPidController.update(measured / targetWithHeadroom, 1.0f, dt);  // 更新 PID 控制器，返回输出

        /**
         * 将 PID 命令映射到缩放比例（绝对或相对，见下文）
         */
        // maps pid command to a scale (absolute or relative, see below)
        const float command = out < 0.0f ? (1.0f / (1.0f - out)) : (1.0f + out);  // 将 PID 输出转换为缩放命令

        /**
         * 有两种方式可以控制缩放因子：
         * 1. 让 PID 控制器直接输出新的缩放因子（类似"位置"控制）
         * 2. 让它评估相对缩放因子（类似"速度"控制）
         * 需要更多实验来确定哪种方式在更多情况下效果更好。
         */
        /*
         * There is two ways we can control the scale factor, either by having the PID controller
         * output a new scale factor directly (like a "position" control), or having it evaluate
         * a relative scale factor (like a "velocity" control).
         * More experimentation is needed to figure out which works better in more cases.
         */

        // direct scaling ("position" control)  // 直接缩放（"位置"控制）
        //const float scale = command;
        // relative scaling ("velocity" control)  // 相对缩放（"速度"控制）
        const float scale = mScale.x * mScale.y * command;  // 使用相对缩放（当前缩放 * 命令）

        const float w = float(mViewport.width);  // 视口宽度
        const float h = float(mViewport.height);  // 视口高度
        if (scale < 1.0f && !options.homogeneousScaling) {  // 如果缩放小于 1 且不使用均匀缩放
            /**
             * 确定主轴和次轴
             */
            // figure out the major and minor axis
            const float major = std::max(w, h);  // 主轴（较大的维度）
            const float minor = std::min(w, h);  // 次轴（较小的维度）

            /**
             * 主轴首先缩放，直到次轴大小
             */
            // the major axis is scaled down first, down to the minor axis
            const float maxMajorScale = minor / major;  // 主轴的最大缩放（保持宽高比）
            const float majorScale = std::max(scale, maxMajorScale);  // 主轴缩放

            /**
             * 然后次轴缩放到原始宽高比
             */
            // then the minor axis is scaled down to the original aspect-ratio
            const float minorScale = std::max(scale / majorScale, majorScale * maxMajorScale);  // 次轴缩放

            /**
             * 如果还有剩余的缩放容量，进行均匀缩放
             */
            // if we have some scaling capacity left, scale homogeneously
            const float homogeneousScale = scale / (majorScale * minorScale);  // 均匀缩放因子

            /**
             * 最后，写入缩放因子
             */
            // finally, write the scale factors
            float& majorRef = w > h ? mScale.x : mScale.y;  // 主轴引用（宽度或高度）
            float& minorRef = w > h ? mScale.y : mScale.x;  // 次轴引用（高度或宽度）
            majorRef = std::sqrt(homogeneousScale) * majorScale;  // 计算主轴缩放（应用均匀缩放）
            minorRef = std::sqrt(homogeneousScale) * minorScale;  // 计算次轴缩放（应用均匀缩放）
        } else {  // 如果缩放 >= 1 或使用均匀缩放
            /**
             * 当向上缩放时，我们总是使用均匀缩放。
             */
            // when scaling up, we're always using homogeneous scaling.
            mScale = std::sqrt(scale);  // 均匀缩放（x 和 y 相同）
        }

        /**
         * 始终限制在最小/最大缩放范围内
         */
        // always clamp to the min/max scale range
        const auto s = mScale;  // 保存原始缩放值
        mScale = clamp(s, options.minScale, options.maxScale);  // 限制缩放范围

        /**
         * 当我们在可控范围外时（即我们进行了限制），禁用积分项。
         * 这有助于在限制事件后不必等待太长时间让积分项生效。
         */
        // disable the integration term when we're outside the controllable range
        // (i.e. we clamped). This help not to have to wait too long for the Integral term
        // to kick in after a clamping event.
        mPidController.setIntegralInhibitionEnabled(mScale != s);  // 如果缩放被限制，禁用积分项
    } else {  // 如果未启用动态分辨率
        mScale = 1.0f;  // 使用 1.0 缩放（无缩放）
    }

#ifndef NDEBUG
    /**
     * 仅用于调试...
     */
    // only for debugging...
    if (UTILS_UNLIKELY(mDebugState->active && mDebugState->debugFrameHistory)) {  // 如果调试状态激活且有帧历史
        auto* const debugFrameHistory = mDebugState->debugFrameHistory.get();  // 获取调试帧历史指针
        using namespace std::chrono;
        using duration_ms = duration<float, std::milli>;  // 毫秒持续时间类型
        const float target = (1000.0f * float(frameRateOptions.interval)) / displayInfo.refreshRate;  // 目标帧时间
        const float targetWithHeadroom = target * (1.0f - frameRateOptions.headRoomRatio);  // 带余量的目标帧时间
        /**
         * 将帧历史向前移动（移除最旧的，为新数据腾出空间）
         */
        std::move(debugFrameHistory->begin() + 1,
                debugFrameHistory->end(), debugFrameHistory->begin());
        /**
         * 添加新的调试帧历史条目
         */
        debugFrameHistory->back() = {
                .target             = target,  // 目标帧时间
                .targetWithHeadroom = targetWithHeadroom,  // 带余量的目标帧时间
                .frameTime          = duration_cast<duration_ms>(info.gpuFrameDuration).count(),  // GPU 帧时间（毫秒）
                .frameTimeDenoised  = duration_cast<duration_ms>(info.denoisedFrameTime).count(),  // 去噪后的帧时间（毫秒）
                .scale              = mScale.x * mScale.y,  // 缩放比例（总面积）
                .pid_e              = mPidController.getError(),  // PID 误差
                .pid_i              = mPidController.getIntegral(),  // PID 积分项
                .pid_d              = mPidController.getDerivative()  // PID 微分项
        };
    }
#endif

    return mScale;  // 返回缩放比例
}

/**
 * 设置可见层
 * 
 * 使用位操作更新可见层的特定位。
 * 
 * @param select 选择掩码（哪些位有效）
 * @param values 值掩码（位的值）
 */
void FView::setVisibleLayers(uint8_t const select, uint8_t const values) noexcept {
    mVisibleLayers = (mVisibleLayers & ~select) | (values & select);  // 清除选择位，然后设置新值
}

/**
 * 检查天空盒是否可见
 * 
 * 检查场景是否有天空盒，以及天空盒的层掩码是否与可见层匹配。
 * 
 * @return 如果天空盒可见返回 true，否则返回 false
 */
bool FView::isSkyboxVisible() const noexcept {
    FSkybox const* skybox = mScene ? mScene->getSkybox() : nullptr;  // 获取天空盒指针
    return skybox != nullptr && (skybox->getLayerMask() & mVisibleLayers);  // 检查天空盒存在且层掩码匹配
}

/**
 * 准备阴影
 * 
 * 收集阴影投射者并构建阴影贴图列表；触发阴影贴图剔除。
 * 
 * @param engine 引擎引用
 * @param renderableData 可渲染对象数据（SoA 布局）
 * @param lightData 光源数据（SoA 布局）
 * @param cameraInfo 相机信息
 */
// Gather shadow-casters and build shadow-map list; kicks shadow-map culling.
void FView::prepareShadowing(FEngine& engine, FScene::RenderableSoa& renderableData,
        FScene::LightSoa const& lightData, CameraInfo const& cameraInfo) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用

    mHasShadowing = false;  // 重置阴影标志
    mNeedsShadowMap = false;  // 重置阴影贴图需求标志
    if (!mShadowingEnabled) {  // 如果未启用阴影
        return;  // 直接返回
    }

    auto& lcm = engine.getLightManager();  // 获取光源管理器

    ShadowMapManager::Builder builder;  // 创建阴影贴图管理器构建器

    /**
     * 主导方向光总是在索引 0
     */
    // dominant directional light is always as index 0
    FLightManager::Instance const directionalLight = lightData.elementAt<FScene::LIGHT_INSTANCE>(0);  // 获取方向光实例
    const bool hasDirectionalShadows = directionalLight && lcm.isShadowCaster(directionalLight);  // 检查是否有方向光阴影
    if (UTILS_UNLIKELY(hasDirectionalShadows)) {  // 如果有方向光阴影
        const auto& shadowOptions = lcm.getShadowOptions(directionalLight);  // 获取阴影选项
        assert_invariant(shadowOptions.shadowCascades >= 1 &&
                shadowOptions.shadowCascades <= CONFIG_MAX_SHADOW_CASCADES);  // 断言级联数量在有效范围内
        builder.directionalShadowMap(0, &shadowOptions);  // 添加方向光阴影贴图
    }

    /**
     * 查找所有投射阴影的聚光灯
     */
    // Find all shadow-casting spotlights.
    size_t shadowMapCount = CONFIG_MAX_SHADOW_CASCADES;  // 阴影贴图计数（从方向光级联数开始）

    /**
     * 我们允许最多 CONFIG_MAX_SHADOWMAPS 个点光源/聚光灯阴影。任何额外的
     * 投射阴影的聚光灯都会被忽略。
     * 注意：点光源阴影需要 6 个阴影贴图，这会减少总计数。
     */
    // We allow a max of CONFIG_MAX_SHADOWMAPS point/spotlight shadows. Any additional
    // shadow-casting spotlights are ignored.
    // Note that pointlight shadows cost 6 shadowmaps, reducing the total count.
    for (size_t l = FScene::DIRECTIONAL_LIGHTS_COUNT; l < lightData.size(); l++) {  // 遍历所有非方向光

        /**
         * 当我们到达这里时，所有光源都应该是可见的
         */
        // when we get here all the lights should be visible
        assert_invariant(lightData.elementAt<FScene::VISIBILITY>(l));  // 断言光源可见

        FLightManager::Instance const li = lightData.elementAt<FScene::LIGHT_INSTANCE>(l);  // 获取光源实例

        if (UTILS_LIKELY(!li)) {  // 如果实例无效
            continue;  // 跳过：无效实例
        }

        if (UTILS_LIKELY(!lcm.isShadowCaster(li))) {  // 如果光源不投射阴影
            /**
             * 因为我们在这里提前退出，我们需要确保将光源标记为非投射。
             * 参见 `ShadowMapManager::updateSpotShadowMaps` 了解 const_cast<> 的理由。
             */
            // Because we early exit here, we need to make sure we mark the light as non-casting.
            // See `ShadowMapManager::updateSpotShadowMaps` for const_cast<> justification.
            auto& shadowInfo = const_cast<FScene::ShadowInfo&>(
                lightData.elementAt<FScene::SHADOW_INFO>(l));  // 获取阴影信息（需要 const_cast）
            shadowInfo.castsShadows = false;  // 标记为不投射阴影
            continue;  // 跳过：不投射阴影
        }

        const bool spotLight = lcm.isSpotLight(li);  // 检查是否为聚光灯

        const size_t maxShadowMapCount = engine.getMaxShadowMapCount();  // 获取最大阴影贴图数量
        const size_t shadowMapCountNeeded = spotLight ? 1 : 6;  // 计算需要的阴影贴图数量（聚光灯 1 个，点光源 6 个）
        if (shadowMapCount + shadowMapCountNeeded <= maxShadowMapCount) {  // 如果还有空间
            shadowMapCount += shadowMapCountNeeded;  // 增加阴影贴图计数
            const auto& shadowOptions = lcm.getShadowOptions(li);  // 获取阴影选项
            builder.shadowMap(l, spotLight, &shadowOptions);  // 添加阴影贴图
        }

        if (shadowMapCount >= maxShadowMapCount) {  // 如果达到最大数量
            break;  // 退出：我们耗尽了聚光灯阴影投射容量
        }
    }

    if (builder.hasShadowMaps()) {  // 如果有阴影贴图
        ShadowMapManager::createIfNeeded(engine, mShadowMapManager);  // 如果需要则创建阴影贴图管理器
        auto const shadowTechnique = mShadowMapManager->update(builder, engine, *this,  // 更新阴影贴图管理器
                cameraInfo, renderableData, lightData);

        mHasShadowing = any(shadowTechnique);  // 设置是否有阴影标志
        mNeedsShadowMap = any(shadowTechnique & ShadowMapManager::ShadowTechnique::SHADOW_MAP);  // 设置是否需要阴影贴图标志
    }
}

/**
 * 准备光照
 * 
 * 填充与光照相关的 UBO/描述符数据（动态光源、曝光、IBL、方向光）。
 * 
 * @param engine 引擎引用
 * @param cameraInfo 相机信息
 */
// Fill lighting-related UBO/descriptor data (dynamic lights, exposure, IBL, directional light).
void FView::prepareLighting(FEngine& engine, CameraInfo const& cameraInfo) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪上下文

    FScene* const scene = mScene;  // 获取场景指针
    auto const& lightData = scene->getLightData();  // 获取光源数据

    /**
     * 动态光源
     */
    /*
     * Dynamic lights
     */

    if (hasDynamicLighting()) {  // 如果启用动态光照
        scene->prepareDynamicLights(cameraInfo, mLightUbh);  // 准备动态光源数据
    }

    /**
     * 这里可见光源数组已缩小到 CONFIG_MAX_LIGHT_COUNT
     */
    // here the array of visible lights has been shrunk to CONFIG_MAX_LIGHT_COUNT
    FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT,
            "visibleLights", lightData.size() - FScene::DIRECTIONAL_LIGHTS_COUNT);  // 跟踪可见光源数量

    /**
     * 曝光
     */
    /*
     * Exposure
     */

    const float exposure = Exposure::exposure(cameraInfo.ev100);  // 计算曝光值
    getColorPassDescriptorSet().prepareExposure(cameraInfo.ev100);  // 准备曝光统一缓冲区

    /**
     * 间接光（IBL）
     */
    /*
     * Indirect light (IBL)
     */

    /**
     * 如果场景没有 IBL，使用黑色 1x1 IBL 并尊重与天空盒关联的回退强度。
     */
    // If the scene does not have an IBL, use the black 1x1 IBL and honor the fallback intensity
    // associated with the skybox.
    float intensity;  // 强度
    FIndirectLight const* ibl = scene->getIndirectLight();  // 获取间接光
    if (UTILS_LIKELY(ibl)) {  // 如果有间接光
        intensity = ibl->getIntensity();  // 使用间接光强度
    } else {  // 如果没有间接光
        ibl = engine.getDefaultIndirectLight();  // 使用默认间接光（黑色 1x1）
        FSkybox const* const skybox = scene->getSkybox();  // 获取天空盒
        intensity = skybox ? skybox->getIntensity() : FIndirectLight::DEFAULT_INTENSITY;  // 使用天空盒强度或默认强度
    }
    getColorPassDescriptorSet().prepareAmbientLight(engine, *ibl, intensity, exposure);  // 准备环境光统一缓冲区

    /**
     * 方向光（总是在索引 0）
     */
    /*
     * Directional light (always at index 0)
     */

    FLightManager::Instance const directionalLight = lightData.elementAt<FScene::LIGHT_INSTANCE>(0);  // 获取方向光实例
    const float3 sceneSpaceDirection = lightData.elementAt<FScene::DIRECTION>(0);  // 获取方向（保证已归一化）
    // guaranteed normalized
    getColorPassDescriptorSet().prepareDirectionalLight(engine, exposure, sceneSpaceDirection, directionalLight);  // 准备方向光统一缓冲区
}

/**
 * 计算相机信息
 * 
 * 计算渲染此视图此帧所需的所有相机参数。
 * 
 * @param engine 引擎常量引用
 * @return 相机信息结构
 */
CameraInfo FView::computeCameraInfo(FEngine const& engine) const noexcept {
    FScene const* const scene = getScene();  // 获取场景指针

    /**
     * 我们对"所有内容"应用"世界原点"以实现 IBL 旋转。
     * "世界原点"也用于保持原点靠近相机位置，以
     * 提高大场景中着色器的浮点精度。
     */
    /*
     * We apply a "world origin" to "everything" in order to implement the IBL rotation.
     * The "world origin" is also used to keep the origin close to the camera position to
     * improve fp precision in the shader for large scenes.
     */
    double3 translation;  // 平移（双精度）
    mat3 rotation;  // 旋转矩阵

    /**
     * 计算渲染此视图此帧所需的所有相机参数。
     */
    /*
     * Calculate all camera parameters needed to render this View for this frame.
     */
    FCamera const* const camera = mViewingCamera ? mViewingCamera : mCullingCamera;  // 获取相机（优先使用观察相机）
    if (engine.debug.view.camera_at_origin) {  // 如果调试选项启用（相机在原点）
        /**
         * 这会将相机移动到原点，有效地在视图空间中进行所有着色器计算，
         * 通过保持在零附近（fp 精度最高的地方）来提高着色器中的浮点精度。
         * 这也确保当相机放置在离原点很远的地方时，对象仍然能够正确渲染和光照。
         */
        // this moves the camera to the origin, effectively doing all shader computations in
        // view-space, which improves floating point precision in the shader by staying around
        // zero, where fp precision is highest. This also ensures that when the camera is placed
        // very far from the origin, objects are still rendered and lit properly.
        translation = -camera->getPosition();  // 计算平移（将相机移动到原点）
    }

    FIndirectLight const* const ibl = scene->getIndirectLight();  // 获取间接光
    if (ibl) {  // 如果有间接光
        /**
         * IBL 变换必须是刚体变换
         */
        // the IBL transformation must be a rigid transform
        rotation = mat3{ transpose(scene->getIndirectLight()->getRotation()) };  // 计算旋转矩阵（转置 IBL 旋转）
        /**
         * 在转换为双精度时正交化矩阵很重要，因为
         * 作为浮点数，基向量的大小只有大约 1e-8 的精度
         */
        // it is important to orthogonalize the matrix when converting it to doubles, because
        // as float, it only has about a 1e-8 precision on the size of the basis vectors
        rotation = orthogonalize(rotation);
    }
    return { *camera, mat4{ rotation } * mat4::translation(translation) };  // 返回相机信息（包含世界变换）
}

/**
 * 准备视图
 * 
 * 准备渲染视图所需的所有数据，包括场景准备、光源剔除、可渲染对象剔除、
 * 阴影准备、光照准备等。这是渲染管线的主要准备阶段。
 * 
 * @param engine 引擎引用
 * @param driver 驱动 API 引用
 * @param rootArenaScope 根内存分配器作用域
 * @param viewport 视口
 * @param cameraInfo 相机信息
 * @param userTime 用户时间（float4，可用于动画等）
 * @param needsAlphaChannel 是否需要 Alpha 通道
 */
void FView::prepare(FEngine& engine, DriverApi& driver, RootArenaScope& rootArenaScope,
        filament::Viewport const viewport, CameraInfo cameraInfo,
        float4 const& userTime, bool const needsAlphaChannel) noexcept {

        FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用
        FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪上下文

    JobSystem& js = engine.getJobSystem();  // 获取作业系统

    /**
     * 准备场景 -- 这里我们收集所有添加到场景的对象，
     * 特别是它们的世界空间 AABB。
     */
    /*
     * Prepare the scene -- this is where we gather all the objects added to the scene,
     * and in particular their world-space AABB.
     */

    /**
     * Lambda 函数：获取剔除视锥
     * 
     * 根据是否有观察相机计算剔除视锥。
     */
    auto getFrustum = [this, &cameraInfo]() -> Frustum {
        if (UTILS_LIKELY(mViewingCamera == nullptr)) {  // 如果没有观察相机（常见情况）
            /**
             * 在常见情况下，当我们没有观察相机时，cameraInfo.view 已经是剔除视图矩阵
             */
            // In the common case when we don't have a viewing camera, cameraInfo.view is
            // already the culling view matrix
            return Frustum{ mat4f{ highPrecisionMultiply(cameraInfo.cullingProjection, cameraInfo.view) }};  // 计算视锥
        }
        /**
         * 否则，我们需要从剔除相机重新计算。
         * 注意：从 mCullingCamera 进行数学计算是正确的，但这隐藏了代码的意图，
         * 即我们应该只依赖 CameraInfo。这是一个极其罕见的情况。
         */
        // Otherwise, we need to recalculate it from the culling camera.
        // Note: it is correct to always do the math from mCullingCamera, but it hides the
        // intent of the code, which is that we should only depend on CameraInfo here.
        // This is an extremely uncommon case.
        const mat4 projection = mCullingCamera->getCullingProjectionMatrix();  // 获取剔除投影矩阵
        const mat4 view = inverse(cameraInfo.worldTransform * mCullingCamera->getModelMatrix());  // 计算视图矩阵
        return Frustum{ mat4f{ projection * view }};  // 计算视锥
    };

    const Frustum cullingFrustum = getFrustum();  // 获取剔除视锥

    FScene* const scene = getScene();  // 获取场景指针

    /**
     * 收集渲染此场景所需的所有信息。将世界原点应用到场景中的所有对象。
     */
    /*
     * Gather all information needed to render this scene. Apply the world origin to all
     * objects in the scene.
     */
    scene->prepare(js, rootArenaScope,  // 准备场景
            cameraInfo.worldTransform,  // 世界变换
            hasVSM());  // 是否有 VSM 阴影

    /**
     * 光源剔除：与可渲染对象剔除并行运行（见下文）
     */
    /*
     * Light culling: runs in parallel with Renderable culling (below)
     */

    JobSystem::Job* froxelizeLightsJob = nullptr;  // Froxelize 光源作业
    JobSystem::Job* prepareVisibleLightsJob = nullptr;  // 准备可见光源作业
    size_t const lightCount = scene->getLightData().size();  // 获取光源数量
    if (lightCount > FScene::DIRECTIONAL_LIGHTS_COUNT) {  // 如果有位置光源
        /**
         * 创建并启动 prepareVisibleLights 作业
         * 注意：此作业更新 LightData（非常量）
         * 在作业外部分配距离的暂存缓冲区，这样我们不需要
         * 使用锁定分配器；缺点是我们需要考虑最坏情况。
         */
        // create and start the prepareVisibleLights job
        // note: this job updates LightData (non const)
        // allocate a scratch buffer for distances outside the job below, so we don't need
        // to use a locked allocator; the downside is that we need to account for the worst case.
        size_t const positionalLightCount = lightCount - FScene::DIRECTIONAL_LIGHTS_COUNT;  // 位置光源数量
        float* const distances = rootArenaScope.allocate<float>(  // 分配距离缓冲区（对齐到缓存行）
                (positionalLightCount + 3u) & ~3u, CACHELINE_SIZE);

        prepareVisibleLightsJob = js.runAndRetain(js.createJob(nullptr,  // 创建并运行作业
                [&engine, distances, positionalLightCount, &viewMatrix = cameraInfo.view, &cullingFrustum,
                 &lightData = scene->getLightData()]  // 捕获变量
                        (JobSystem&, JobSystem::Job*) {
                    prepareVisibleLights(engine.getLightManager(),  // 准备可见光源
                            { distances, distances + positionalLightCount },  // 距离缓冲区
                            viewMatrix, cullingFrustum, lightData);  // 视图矩阵、剔除视锥、光源数据
                }));
    }

    /**
     * 这用于稍后（在 Renderer.cpp 中）等待 Froxelization 完成
     */
    // this is used later (in Renderer.cpp) to wait for froxelization to finishes
    setFroxelizerSync(froxelizeLightsJob);  // 设置 Froxelizer 同步（初始为 nullptr）

    Range merged;  // 合并的范围（用于可渲染对象）

    {  // 此作用域中的所有操作必须顺序执行
        // all the operations in this scope must happen sequentially
        FScene::RenderableSoa& renderableData = scene->getRenderableData();  // 获取可渲染对象数据（SoA 布局）

        Slice<Culler::result_type> cullingMask = renderableData.slice<FScene::VISIBLE_MASK>();  // 获取剔除掩码切片
        std::uninitialized_fill(cullingMask.begin(), cullingMask.end(), 0);  // 初始化剔除掩码为 0

        /**
         * 剔除：尽快执行相机剔除
         * （这将设置 VISIBLE_RENDERABLE 位）
         */
        /*
         * Culling: as soon as possible we perform our camera-culling
         * (this will set the VISIBLE_RENDERABLE bit)
         */

        prepareVisibleRenderables(js, cullingFrustum, renderableData);  // 准备可见可渲染对象（相机剔除）


        /**
         * 阴影：计算阴影相机并剔除阴影投射者
         * （这将设置 VISIBLE_DIR_SHADOW_CASTER 位和 VISIBLE_SPOT_SHADOW_CASTER 位）
         */
        /*
         * Shadowing: compute the shadow camera and cull shadow casters
         * (this will set the VISIBLE_DIR_SHADOW_CASTER bit and VISIBLE_SPOT_SHADOW_CASTER bits)
         */

        /**
         * prepareShadowing 依赖于 prepareVisibleLights()。
         */
        // prepareShadowing relies on prepareVisibleLights().
        if (prepareVisibleLightsJob) {  // 如果有准备可见光源作业
            js.waitAndRelease(prepareVisibleLightsJob);  // 等待并释放作业
        }

        /**
         * 从这一点开始 lightData 是常量（只能在 prepareVisibleLightsJob 之后发生）
         */
        // lightData is const from this point on (can only happen after prepareVisibleLightsJob)
        auto const& lightData = scene->getLightData();  // 获取光源数据（常量引用）

        /**
         * 现在我们知道是否有动态光照（即：动态光源可见）
         */
        // now we know if we have dynamic lighting (i.e.: dynamic lights are visible)
        mHasDynamicLighting = lightData.size() > FScene::DIRECTIONAL_LIGHTS_COUNT;  // 设置动态光照标志

        /**
         * 我们也知道是否有方向光
         */
        // we also know if we have a directional light
        FLightManager::Instance const directionalLight =
                lightData.elementAt<FScene::LIGHT_INSTANCE>(0);  // 获取方向光实例
        mHasDirectionalLighting = directionalLight.isValid();  // 设置方向光照标志

        // As soon as prepareVisibleLight finishes, we can kick-off the froxelization
        if (hasDynamicLighting()) {
            auto& froxelizer = mFroxelizer;
            if (froxelizer.prepare(driver, rootArenaScope, viewport,
                    cameraInfo.projection, cameraInfo.zn, cameraInfo.zf,
                    cameraInfo.clipTransform)) {
                // TODO: might be more consistent to do this in prepareLighting(), but it's not
                //       strictly necessary
                getColorPassDescriptorSet().prepareDynamicLights(mFroxelizer, mFroxelVizEnabled);
                mFroxelConfigurationAge++;
            }
            // We need to pass viewMatrix by value here because it extends the scope of this
            // function.
            std::function froxelizerWork =
                    [&froxelizer = mFroxelizer, &engine, viewMatrix = cameraInfo.view, &lightData]
                            (JobSystem&, JobSystem::Job*) {
                        froxelizer.froxelizeLights(engine, viewMatrix, lightData);
                    };
            froxelizeLightsJob = js.runAndRetain(js.createJob(nullptr, std::move(froxelizerWork)));
        }

        setFroxelizerSync(froxelizeLightsJob);

        prepareShadowing(engine, renderableData, lightData, cameraInfo);

        /*
         * Partition the SoA so that renderables are partitioned w.r.t their visibility into the
         * following groups:
         *
         * 1. visible (main camera) renderables
         * 2. visible (main camera) renderables and directional shadow casters
         * 3. directional shadow casters only
         * 4. potential punctual light shadow casters only
         * 5. definitely invisible renderables
         *
         * Note that the first three groups are partitioned based only on the lowest two bits of the
         * VISIBLE_MASK (VISIBLE_RENDERABLE and VISIBLE_DIR_SHADOW_CASTER), and thus can also
         * contain punctual light shadow casters as well. The fourth group contains *only* punctual
         * shadow casters.
         *
         * This operation is somewhat heavy as it sorts the whole SoA. We use std::partition instead
         * of sort(), which gives us O(4.N) instead of O(N.log(N)) application of swap().
         */

        // TODO: we need to compare performance of doing this partitioning vs not doing it.
        //       and rely on checking visibility in the loops

        FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, "Partitioning");

        // calculate the sorting key for all elements, based on their visibility
        uint8_t const* layers = renderableData.data<FScene::LAYERS>();
        auto const* visibility = renderableData.data<FScene::VISIBILITY_STATE>();
        computeVisibilityMasks(getVisibleLayers(), layers, visibility, cullingMask.begin(),
                renderableData.size());

        auto const beginRenderables = renderableData.begin();

        auto beginDirCasters = partition(beginRenderables, renderableData.end(),
                VISIBLE_RENDERABLE | VISIBLE_DIR_SHADOW_RENDERABLE,
                VISIBLE_RENDERABLE);

        auto const beginDirCastersOnly = partition(beginDirCasters, renderableData.end(),
                VISIBLE_RENDERABLE | VISIBLE_DIR_SHADOW_RENDERABLE,
                VISIBLE_RENDERABLE | VISIBLE_DIR_SHADOW_RENDERABLE);

        auto const endDirCastersOnly = partition(beginDirCastersOnly, renderableData.end(),
                VISIBLE_RENDERABLE | VISIBLE_DIR_SHADOW_RENDERABLE,
                VISIBLE_DIR_SHADOW_RENDERABLE);

        auto const endPotentialSpotCastersOnly = partition(endDirCastersOnly, renderableData.end(),
                VISIBLE_DYN_SHADOW_RENDERABLE,
                VISIBLE_DYN_SHADOW_RENDERABLE);

        // convert to indices
        mVisibleRenderables = { 0, uint32_t(beginDirCastersOnly - beginRenderables) };

        mVisibleDirectionalShadowCasters = {
                uint32_t(beginDirCasters - beginRenderables),
                uint32_t(endDirCastersOnly - beginRenderables)};

        merged = { 0, uint32_t(endPotentialSpotCastersOnly - beginRenderables) };
        if (!needsShadowMap() || !mShadowMapManager->hasSpotShadows()) {
            // we know we don't have spot shadows, we can reduce the range to not even include
            // the potential spot casters
            merged = { 0, uint32_t(endDirCastersOnly - beginRenderables) };
        }

        mSpotLightShadowCasters = merged;

        FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);

        // TODO: when any spotlight is used, `merged` ends-up being the whole list. However,
        //       some of the items will end-up not being visible by any light. Can we do better?
        //       e.g. could we deffer some of the prepareVisibleRenderables() to later?
        scene->prepareVisibleRenderables(merged);

        // update those UBOs
        if (!merged.empty()) {
            updateUBOs(driver, renderableData, merged);

            mCommonRenderableDescriptorSet.setBuffer(
                engine.getPerRenderableDescriptorSetLayout(),
                    +PerRenderableBindingPoints::OBJECT_UNIFORMS, mRenderableUbh,
                    0, sizeof(PerRenderableUib));

            mCommonRenderableDescriptorSet.commit(
                    engine.getPerRenderableDescriptorSetLayout(), driver);
        }
    }

    { // this must happen after mRenderableUbh is created/updated
        // prepare skinning, morphing and hybrid instancing
        auto& sceneData = scene->getRenderableData();
        for (uint32_t const i : merged) {
            auto const& skinning = sceneData.elementAt<FScene::SKINNING_BUFFER>(i);
            auto const& morphing = sceneData.elementAt<FScene::MORPHING_BUFFER>(i);

            // FIXME: when only one is active the UBO handle of the other is null
            //        (probably a problem on vulkan)
            if (UTILS_UNLIKELY(skinning.handle || morphing.handle)) {
                auto const ci = sceneData.elementAt<FScene::RENDERABLE_INSTANCE>(i);
                FRenderableManager& rcm = engine.getRenderableManager();
                auto& descriptorSet = rcm.getDescriptorSet(ci);

                auto const& layout = engine.getPerRenderableDescriptorSetLayout();

                // initialize the descriptor set the first time it's needed
                if (UTILS_UNLIKELY(!descriptorSet.getHandle())) {
                    descriptorSet = DescriptorSet{ "FView::descriptorSet", layout };
                }

                descriptorSet.setBuffer(layout,
                        +PerRenderableBindingPoints::OBJECT_UNIFORMS, mRenderableUbh,
                        0, sizeof(PerRenderableUib));

                descriptorSet.setBuffer(layout,
                        +PerRenderableBindingPoints::BONES_UNIFORMS,
                        engine.getDummyUniformBuffer(), 0, sizeof(PerRenderableBoneUib));

                descriptorSet.setBuffer(layout,
                        +PerRenderableBindingPoints::MORPHING_UNIFORMS,
                        engine.getDummyUniformBuffer(), 0, sizeof(PerRenderableMorphingUib));

                descriptorSet.setSampler(layout,
                        +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,
                        engine.getDummyMorphTargetBuffer()->getPositionsHandle(), {});

                descriptorSet.setSampler(layout,
                        +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,
                        engine.getDummyMorphTargetBuffer()->getTangentsHandle(), {});

                descriptorSet.setSampler(layout,
                        +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS,
                        engine.getZeroTexture(), {});

                if (UTILS_UNLIKELY(skinning.handle || morphing.handle)) {
                    descriptorSet.setBuffer(layout,
                            +PerRenderableBindingPoints::BONES_UNIFORMS,
                            skinning.handle, 0, sizeof(PerRenderableBoneUib));

                    descriptorSet.setSampler(layout,
                            +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS,
                            skinning.boneIndicesAndWeightHandle, {});

                    descriptorSet.setBuffer(layout,
                            +PerRenderableBindingPoints::MORPHING_UNIFORMS,
                            morphing.handle, 0, sizeof(PerRenderableMorphingUib));

                    descriptorSet.setSampler(layout,
                            +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,
                            morphing.morphTargetBuffer->getPositionsHandle(), {});

                    descriptorSet.setSampler(layout,
                            +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,
                            morphing.morphTargetBuffer->getTangentsHandle(), {});
                }

                descriptorSet.commit(layout, driver);

                // write the descriptor-set handle to the sceneData array for access later
                sceneData.elementAt<FScene::DESCRIPTOR_SET_HANDLE>(i) =
                        descriptorSet.getHandle();
            } else {
                // use the shared descriptor-set
                sceneData.elementAt<FScene::DESCRIPTOR_SET_HANDLE>(i) =
                        mCommonRenderableDescriptorSet.getHandle();
            }
        }
    }

    /*
     * Prepare lighting -- this is where we update the lights UBOs, set up the IBL,
     * set up the froxelization parameters.
     * Relies on FScene::prepare() and prepareVisibleLights()
     */

    prepareLighting(engine, cameraInfo);

    /*
     * Update driver state
     */

    auto const& tcm = engine.getTransformManager();
    auto const fogTransform = tcm.getWorldTransformAccurate(tcm.getInstance(mFogEntity));

    auto& colorPassDescriptorSet = getColorPassDescriptorSet();
    colorPassDescriptorSet.prepareCamera(engine, cameraInfo);
    colorPassDescriptorSet.prepareTime(engine, userTime);
    colorPassDescriptorSet.prepareFog(engine, cameraInfo, fogTransform, mFogOptions,
            scene->getIndirectLight());
    colorPassDescriptorSet.prepareTemporalNoise(engine, mTemporalAntiAliasingOptions);
    colorPassDescriptorSet.prepareBlending(needsAlphaChannel);
    colorPassDescriptorSet.prepareMaterialGlobals(mMaterialGlobals);
}

void FView::computeVisibilityMasks(
        uint8_t const visibleLayers,
        uint8_t const* UTILS_RESTRICT layers,
        FRenderableManager::Visibility const* UTILS_RESTRICT visibility,
        Culler::result_type* UTILS_RESTRICT visibleMask, size_t count) {
    // __restrict__ seems to only be taken into account as function parameters. This is very
    // important here, otherwise, this loop doesn't get vectorized.
    // This is vectorized 16x.
    count = (count + 0xFu) & ~0xFu; // capacity guaranteed to be multiple of 16
    for (size_t i = 0; i < count; ++i) {
        const Culler::result_type mask = visibleMask[i];
        const FRenderableManager::Visibility v = visibility[i];
        const bool inVisibleLayer = layers[i] & visibleLayers;

        const bool visibleRenderable = inVisibleLayer &&
                (!v.culling || (mask & VISIBLE_RENDERABLE));

        const bool visibleDirectionalShadowRenderable = (v.castShadows && inVisibleLayer) &&
                (!v.culling || (mask & VISIBLE_DIR_SHADOW_RENDERABLE));

        const bool potentialSpotShadowRenderable = v.castShadows && inVisibleLayer;

        using Type = Culler::result_type;

        visibleMask[i] =
                Type(visibleRenderable << VISIBLE_RENDERABLE_BIT) |
                Type(visibleDirectionalShadowRenderable << VISIBLE_DIR_SHADOW_RENDERABLE_BIT) |
                Type(potentialSpotShadowRenderable << VISIBLE_DYN_SHADOW_RENDERABLE_BIT);
    }
}

void FView::updateUBOs(
        FEngine::DriverApi& driver,
        FScene::RenderableSoa& renderableData,
        utils::Range<uint32_t> visibleRenderables) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    FRenderableManager::InstancesInfo const* instancesData = renderableData.data<FScene::INSTANCES>();
    PerRenderableData const* const uboData = renderableData.data<FScene::UBO>();
    mat4f const* const worldTransformData = renderableData.data<FScene::WORLD_TRANSFORM>();

    // regular renderables count
    size_t const rcount = visibleRenderables.size();

    // instanced renderables count
    size_t icount = 0;
    for (uint32_t const i : visibleRenderables) {
        auto& instancesInfo = instancesData[i];
        if (instancesInfo.buffer) {
            assert_invariant(instancesInfo.count <= instancesInfo.buffer->getInstanceCount());
            icount += instancesInfo.count;
        }
    }

    // total count of PerRenderableData slots we need
    size_t const tcount = rcount + icount;

    // resize the UBO accordingly
    if (mRenderableUBOElementCount < tcount) {
        // allocate 1/3 extra, with a minimum of 16 objects
        const size_t count = std::max(size_t(16u), (4u * tcount + 2u) / 3u);
        mRenderableUBOElementCount = count;
        driver.destroyBufferObject(mRenderableUbh);
        mRenderableUbh = driver.createBufferObject(
                count * sizeof(PerRenderableData) + sizeof(PerRenderableUib),
                BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);
    } else {
        // TODO: should we shrink the underlying UBO at some point?
    }
    assert_invariant(mRenderableUbh);


    // Allocate a staging CPU buffer:
    // Don't allocate more than 16 KiB directly into the render stream
    static constexpr size_t MAX_STREAM_ALLOCATION_COUNT = 64;   // 16 KiB
    PerRenderableData* buffer = [&]{
        if (tcount >= MAX_STREAM_ALLOCATION_COUNT) {
            // use the heap allocator
            auto& bufferPoolAllocator = mSharedState->mBufferPoolAllocator;
            return static_cast<PerRenderableData*>(bufferPoolAllocator.get(tcount * sizeof(PerRenderableData)));
        }
        // allocate space into the command stream directly
        return driver.allocatePod<PerRenderableData>(tcount);
    }();


    // TODO: consider using JobSystem to parallelize this.
    uint32_t j = rcount;
    for (uint32_t const i: visibleRenderables) {
        // even the instanced ones are copied here because we need to maintain the offsets
        // into the buffer currently (we could skip then because it won't be used, but
        // for now it's more trouble than it's worth)
        buffer[i] = uboData[i];

        auto& instancesInfo = instancesData[i];
        if (instancesInfo.buffer) {
            instancesInfo.buffer->prepare(
                    buffer,  j, instancesInfo.count,
                    worldTransformData[i], uboData[i]);
            j += instancesInfo.count;
        }
    }

    // We capture state shared between Scene and the update buffer callback, because the Scene could
    // be destroyed before the callback executes.
    std::weak_ptr<SharedState>* const weakShared =
            new (std::nothrow) std::weak_ptr<SharedState>(mSharedState);

    // update the UBO
    driver.resetBufferObject(mRenderableUbh);
    driver.updateBufferObjectUnsynchronized(mRenderableUbh, {
        buffer, tcount * sizeof(PerRenderableData),
        +[](void* p, size_t const s, void* user) {
            std::weak_ptr<SharedState> const* const weakShared =
                    static_cast<std::weak_ptr<SharedState>*>(user);
            if (s >= MAX_STREAM_ALLOCATION_COUNT * sizeof(PerRenderableData)) {
                if (auto state = weakShared->lock()) {
                    state->mBufferPoolAllocator.put(p);
                }
            }
            delete weakShared;
        }, weakShared
    }, 0);
}

UTILS_NOINLINE
/* static */ FScene::RenderableSoa::iterator FView::partition(
        FScene::RenderableSoa::iterator const begin,
        FScene::RenderableSoa::iterator const end,
        Culler::result_type mask, Culler::result_type value) noexcept {
    return std::partition(begin, end, [mask, value](auto it) {
        // Mask VISIBLE_MASK to ignore higher bits related to spot shadows. We only partition based
        // on renderable and directional shadow visibility.
        return (it.template get<FScene::VISIBLE_MASK>() & mask) == value;
    });
}

void FView::prepareCamera(FEngine& engine, const CameraInfo& cameraInfo) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    getColorPassDescriptorSet().prepareCamera(engine, cameraInfo);
}

void FView::prepareLodBias(float const bias, float2 const derivativesScale) const noexcept {
    getColorPassDescriptorSet().prepareLodBias(bias, derivativesScale);
}

void FView::prepareViewport(
        const filament::Viewport& physicalViewport,
        const filament::Viewport& logicalViewport) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    // TODO: we should pass viewport.{left|bottom} to the backend, so it can offset the
    //       scissor properly.
    getColorPassDescriptorSet().prepareViewport(physicalViewport, logicalViewport);
}

void FView::prepareSSAO(Handle<HwTexture> ssao) const noexcept {
    getColorPassDescriptorSet().prepareSSAO(ssao, mAmbientOcclusionOptions);
}

void FView::prepareSSAO(AmbientOcclusionOptions const& options) const noexcept {
    // High quality sampling is enabled only if AO itself is enabled and upsampling quality is at
    // least set to high and of course only if upsampling is needed.
    const bool highQualitySampling = options.upsampling >= QualityLevel::HIGH
            && options.resolution < 1.0f;

    const float edgeDistance = 1.0f / options.bilateralThreshold;
    auto& s = mUniforms.edit();
    s.aoSamplingQualityAndEdgeDistance =
            options.enabled ? (highQualitySampling ? edgeDistance : 0.0f) : -1.0f;
    s.aoBentNormals = options.enabled && options.bentNormals ? 1.0f : 0.0f;
}

void FView::prepareSSR(Handle<HwTexture> ssr) const noexcept {
    getColorPassDescriptorSet().prepareScreenSpaceRefraction(ssr);
}

void FView::prepareSSR(FEngine& engine, CameraInfo const& cameraInfo,
        float const refractionLodOffset,
        ScreenSpaceReflectionsOptions const& options) const noexcept {

    auto const& ssr = getFrameHistory().getPrevious().ssr;

    bool const disableSSR = !ssr.color.handle;
    mat4 const& historyProjection = ssr.projection;
    mat4f const& uvFromClipMatrix = engine.getUvFromClipMatrix();
    mat4f const projection = cameraInfo.projection;
    mat4 const userViewMatrix = cameraInfo.getUserViewMatrix();

    // set screen-space reflections and screen-space refractions
    mat4f const uvFromViewMatrix = uvFromClipMatrix * projection;
    mat4f const reprojection = uvFromClipMatrix *
            mat4f{ historyProjection * inverse(userViewMatrix) };

    auto& s = mUniforms.edit();
    s.ssrReprojection = reprojection;
    s.ssrUvFromViewMatrix = uvFromViewMatrix;
    s.ssrThickness = options.thickness;
    s.ssrBias = options.bias;
    s.ssrStride = options.stride;
    s.refractionLodOffset = refractionLodOffset;
    s.ssrDistance = (options.enabled && !disableSSR) ? options.maxDistance : 0.0f;
}


void FView::prepareStructure(Handle<HwTexture> structure) const noexcept {
    // sampler must be NEAREST
    getColorPassDescriptorSet().prepareStructure(structure);
}

void FView::prepareShadowMapping(FEngine const& engine, Handle<HwTexture> texture) const noexcept {
    // when needsShadowMap() is not set, this method only just sets a dummy texture
    // in the needed samplers (in that case `texture` is actually a dummy texture).

    BufferObjectHandle ubo{ engine.getDummyUniformBuffer() };
    if (mHasShadowing) {
        assert_invariant(mShadowMapManager);
        ubo = mShadowMapManager->getShadowUniformsHandle();
    }
    getColorPassDescriptorSet().prepareShadowMapping(ubo);

    switch (mShadowType) {
        case ShadowType::PCF:
            getColorPassDescriptorSet().prepareShadowPCF(texture);
            break;
        case ShadowType::VSM:
            getColorPassDescriptorSet().prepareShadowVSM(texture, mVsmShadowOptions);
            break;
        case ShadowType::DPCF:
            getColorPassDescriptorSet().prepareShadowDPCF(texture);
            break;
        case ShadowType::PCSS:
            getColorPassDescriptorSet().prepareShadowPCSS(texture);
            break;
        case ShadowType::PCFd:
            getColorPassDescriptorSet().prepareShadowPCFDebug(texture);
            break;
    }
}

void FView::prepareShadowMapping() const noexcept {

    ShadowMapManager::ShadowMappingUniforms uniforms{};
    if (needsShadowMap()) {
        uniforms = mShadowMapManager->getShadowMappingUniforms();
    }

    constexpr float low  = 5.54f; // ~ std::log(std::numeric_limits<math::half>::max()) * 0.5f;
    constexpr float high = 42.0f; // ~ std::log(std::numeric_limits<float>::max()) * 0.5f;
    constexpr uint32_t SHADOW_SAMPLING_RUNTIME_PCF = 0u;
    constexpr uint32_t SHADOW_SAMPLING_RUNTIME_EVSM = 1u;
    constexpr uint32_t SHADOW_SAMPLING_RUNTIME_DPCF = 2u;
    constexpr uint32_t SHADOW_SAMPLING_RUNTIME_PCSS = 3u;
    auto& s = mUniforms.edit();
    s.cascadeSplits = uniforms.cascadeSplits;
    s.ssContactShadowDistance = uniforms.ssContactShadowDistance;
    s.directionalShadows = int32_t(uniforms.directionalShadows);
    s.cascades = int32_t(uniforms.cascades);
    switch (mShadowType) {
        case ShadowType::PCF:
            s.shadowSamplingType = SHADOW_SAMPLING_RUNTIME_PCF;
            break;
        case ShadowType::VSM:
            s.shadowSamplingType = SHADOW_SAMPLING_RUNTIME_EVSM;
            s.vsmExponent = mVsmShadowOptions.highPrecision ? high : low;
            s.vsmDepthScale = mVsmShadowOptions.minVarianceScale * 0.01f * s.vsmExponent;
            s.vsmLightBleedReduction = mVsmShadowOptions.lightBleedReduction;
            break;
        case ShadowType::DPCF:
            s.shadowSamplingType = SHADOW_SAMPLING_RUNTIME_DPCF;
            s.shadowPenumbraRatioScale = mSoftShadowOptions.penumbraRatioScale;
            break;
        case ShadowType::PCSS:
            s.shadowSamplingType = SHADOW_SAMPLING_RUNTIME_PCSS;
            s.shadowPenumbraRatioScale = mSoftShadowOptions.penumbraRatioScale;
            break;
        case ShadowType::PCFd:
            s.shadowSamplingType = SHADOW_SAMPLING_RUNTIME_PCF;
            break;
    }
}

void FView::commitUniforms(DriverApi& driver) const noexcept {
    if (mUniforms.isDirty()) {
        mUniforms.clean();
        driver.updateBufferObject(mUniforms.getUboHandle(),
                mUniforms.toBufferDescriptor(driver), 0);
    }
}

void FView::commitDescriptorSet(DriverApi& driver) const noexcept {
    getColorPassDescriptorSet().commit(driver);
}

void FView::commitFroxels(DriverApi& driverApi) const noexcept {
    if (mHasDynamicLighting) {
        mFroxelizer.commit(driverApi);
    }
}

UTILS_NOINLINE
void FView::prepareVisibleRenderables(JobSystem& js,
        Frustum const& frustum, FScene::RenderableSoa& renderableData) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    if (UTILS_LIKELY(isFrustumCullingEnabled())) {
        cullRenderables(js, renderableData, frustum, VISIBLE_RENDERABLE_BIT);
    } else {
        std::uninitialized_fill(renderableData.begin<FScene::VISIBLE_MASK>(),
                  renderableData.end<FScene::VISIBLE_MASK>(), VISIBLE_RENDERABLE);
    }
}

void FView::cullRenderables(JobSystem&,
        FScene::RenderableSoa& renderableData, Frustum const& frustum, size_t bit) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    float3 const* worldAABBCenter = renderableData.data<FScene::WORLD_AABB_CENTER>();
    float3 const* worldAABBExtent = renderableData.data<FScene::WORLD_AABB_EXTENT>();
    FScene::VisibleMaskType* visibleArray = renderableData.data<FScene::VISIBLE_MASK>();

    // culling job (this runs on multiple threads)
    auto functor = [&frustum, worldAABBCenter, worldAABBExtent, visibleArray, bit]
            (uint32_t const index, uint32_t const c) {
        Culler::intersects(
                visibleArray + index,
                frustum,
                worldAABBCenter + index,
                worldAABBExtent + index, c, bit);
    };

    // Note: we can't use jobs::parallel_for() here because Culler::intersects() must process
    //       multiples of eight primitives.
    // Moreover, even with a large number of primitives, the overhead of the JobSystem is too
    // large compared to the run time of Culler::intersects, e.g.: ~100us for 4000 primitives
    // on Pixel4.
    functor(0, renderableData.size());
}

void FView::prepareVisibleLights(FLightManager const& lcm,
        Slice<float> scratch,
        mat4f const& viewMatrix, Frustum const& frustum,
        FScene::LightSoa& lightData) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    assert_invariant(lightData.size() > FScene::DIRECTIONAL_LIGHTS_COUNT);

    auto const* UTILS_RESTRICT sphereArray     = lightData.data<FScene::POSITION_RADIUS>();
    auto const* UTILS_RESTRICT directions      = lightData.data<FScene::DIRECTION>();
    auto const* UTILS_RESTRICT instanceArray   = lightData.data<FScene::LIGHT_INSTANCE>();
    auto      * UTILS_RESTRICT visibleArray    = lightData.data<FScene::VISIBILITY>();

    Culler::intersects(visibleArray, frustum, sphereArray, lightData.size());

    const float4* const UTILS_RESTRICT planes = frustum.getNormalizedPlanes();
    // the directional light is considered visible
    size_t visibleLightCount = FScene::DIRECTIONAL_LIGHTS_COUNT;
    // skip directional light
    for (size_t i = FScene::DIRECTIONAL_LIGHTS_COUNT; i < lightData.size(); i++) {
        FLightManager::Instance const li = instanceArray[i];
        if (visibleArray[i]) {
            if (!lcm.isLightCaster(li)) {
                visibleArray[i] = 0;
                continue;
            }
            if (lcm.getIntensity(li) <= 0.0f) {
                visibleArray[i] = 0;
                continue;
            }
            // cull spotlights that cannot possibly intersect the view frustum
            if (lcm.isSpotLight(li)) {
                const float3 position = sphereArray[i].xyz;
                const float3 axis = directions[i];
                const float cosSqr = lcm.getCosOuterSquared(li);
                bool invisible = false;
                for (size_t j = 0; j < 6; ++j) {
                    const float p = dot(position + planes[j].xyz * planes[j].w, planes[j].xyz);
                    const float c = dot(planes[j].xyz, axis);
                    invisible |= ((1.0f - c * c) < cosSqr && c > 0 && p > 0);
                }
                if (invisible) {
                    visibleArray[i] = 0;
                    continue;
                }
            }
            visibleLightCount++;
        }
    }

    // Partition array such that all visible lights appear first
    UTILS_UNUSED_IN_RELEASE auto last =
            std::partition(lightData.begin() + FScene::DIRECTIONAL_LIGHTS_COUNT, lightData.end(),
                    [](auto const& it) {
                        return it.template get<FScene::VISIBILITY>() != 0;
                    });
    assert_invariant(visibleLightCount == size_t(last - lightData.begin()));


    /*
     * Some lights might be left out if there are more than the GPU buffer allows (i.e. 256).
     *
     * We always sort lights by distance to the camera so that:
     * - we can build light trees later
     * - lights farther from the camera are dropped when in excess
     *   Note this doesn't always work well, e.g. for search-lights, we might need to also
     *   take the radius into account.
     * - This helps our limited numbers of spot-shadow as well.
     */

    // number of point/spotlights
    size_t const positionalLightCount = visibleLightCount - FScene::DIRECTIONAL_LIGHTS_COUNT;
    if (positionalLightCount) {
        assert_invariant(positionalLightCount <= scratch.size());
        // pre-compute the lights' distance to the camera, for sorting below
        // - we don't skip the directional light, because we don't care, it's ignored during sorting
        float* const UTILS_RESTRICT distances = scratch.data();
        float4 const* const UTILS_RESTRICT spheres = lightData.data<FScene::POSITION_RADIUS>();
        computeLightCameraDistances(distances, viewMatrix, spheres, visibleLightCount);

        // skip directional light
        Zip2Iterator b = { lightData.begin(), distances };
        std::sort(b + FScene::DIRECTIONAL_LIGHTS_COUNT, b + visibleLightCount,
                [](auto const& lhs, auto const& rhs) { return lhs.second < rhs.second; });
    }

    // drop excess lights
    lightData.resize(std::min(visibleLightCount,
            CONFIG_MAX_LIGHT_COUNT + FScene::DIRECTIONAL_LIGHTS_COUNT));
}

// These methods need to exist so clang honors the __restrict__ keyword, which in turn
// produces much better vectorization. The ALWAYS_INLINE keyword makes sure we actually don't
// pay the price of the call!
UTILS_ALWAYS_INLINE
inline void FView::computeLightCameraDistances(
        float* UTILS_RESTRICT const distances,
        mat4f const& UTILS_RESTRICT viewMatrix,
        float4 const* UTILS_RESTRICT spheres, size_t count) noexcept {

    // without this, the vectorization is less efficient
    // we're guaranteed to have a multiple of 4 lights (at least)
    count = uint32_t(count + 3u) & ~3u;
    for (size_t i = 0 ; i < count; i++) {
        const float4 sphere = spheres[i];
        const float4 center = viewMatrix * sphere.xyz; // camera points towards the -z axis
        distances[i] = length(center);
    }
}

void FView::updatePrimitivesLod(FScene::RenderableSoa& renderableData,
        FEngine const& engine, CameraInfo const&, Range visible) noexcept {
    FRenderableManager const& rcm = engine.getRenderableManager();
    for (uint32_t const index : visible) {
        uint8_t const level = 0; // TODO: pick the proper level of detail
        auto ri = renderableData.elementAt<FScene::RENDERABLE_INSTANCE>(index);
        renderableData.elementAt<FScene::PRIMITIVES>(index) = rcm.getRenderPrimitives(ri, level);
    }
}

FrameGraphId<FrameGraphTexture> FView::renderShadowMaps(FEngine& engine, FrameGraph& fg,
        CameraInfo const& cameraInfo, float4 const& userTime,
        RenderPassBuilder const& passBuilder) noexcept {
    assert_invariant(needsShadowMap());
    return mShadowMapManager->render(engine, fg, passBuilder, *this, cameraInfo, userTime);
}

void FView::commitFrameHistory(FEngine& engine) noexcept {
    // Here we need to destroy resources in mFrameHistory.back()
    auto& disposer = engine.getResourceAllocatorDisposer();
    auto& frameHistory = mFrameHistory;

    FrameHistoryEntry& last = frameHistory.back();
    disposer.destroy(std::move(last.taa.color.handle));
    disposer.destroy(std::move(last.ssr.color.handle));

    // and then push the new history entry to the history stack
    frameHistory.commit();
}

void FView::clearFrameHistory(FEngine& engine) noexcept {
    // make sure we free all resources in the history
    auto& disposer = engine.getResourceAllocatorDisposer();
    auto& frameHistory = mFrameHistory;
    for (size_t i = 0; i < frameHistory.size(); ++i) {
        FrameHistoryEntry& last = frameHistory[i];
        disposer.destroy(std::move(last.taa.color.handle));
        disposer.destroy(std::move(last.ssr.color.handle));
    }
}

void FView::executePickingQueries(DriverApi& driver,
        RenderTargetHandle handle, float2 const scale) noexcept {

    while (mActivePickingQueriesList) {
        FPickingQuery* const pQuery = mActivePickingQueriesList;
        mActivePickingQueriesList = pQuery->next;

        // adjust for dynamic resolution and structure buffer scale
        const uint32_t x = uint32_t(float(pQuery->x) * scale.x);
        const uint32_t y = uint32_t(float(pQuery->y) * scale.y);

        if (UTILS_UNLIKELY(driver.getFeatureLevel() == FeatureLevel::FEATURE_LEVEL_0)) {
            driver.readPixels(handle, x, y, 1, 1, {
                    &pQuery->result.reserved1, 4u, // 4
                    PixelDataFormat::RGBA, PixelDataType::UBYTE,
                    pQuery->handler, [](void*, size_t, void* user) {
                        FPickingQuery* pQuery = static_cast<FPickingQuery*>(user);
                        uint8_t const* const p =
                                reinterpret_cast<uint8_t const *>(&pQuery->result.reserved1);
                        uint32_t const r = p[0];
                        uint32_t const g = p[1];
                        uint32_t const b = p[2];
                        uint32_t const a = p[3];
                        int32_t const identity = int32_t(a << 16u | (b << 8u) | g);
                        float const depth = float(r) / 255.0f;
                        pQuery->result.renderable = Entity::import(identity);
                        pQuery->result.depth = depth;
                        pQuery->result.fragCoords = {
                                pQuery->x, pQuery->y, float(1.0 - depth) };
                        pQuery->callback(pQuery->result, pQuery);
                        FPickingQuery::put(pQuery);
                    }, pQuery
            });
        } else {
            driver.readPixels(handle, x, y, 1, 1, {
                    &pQuery->result.renderable, 4u * 4u, // 4*uint
                    PixelDataFormat::RGBA_INTEGER, PixelDataType::UINT,
                    pQuery->handler, [](void*, size_t, void* user) {
                        FPickingQuery* const pQuery = static_cast<FPickingQuery*>(user);
                        // pQuery->result.renderable already contains the right value!
                        pQuery->result.fragCoords = {
                                pQuery->x, pQuery->y, float(1.0 - pQuery->result.depth) };
                        pQuery->callback(pQuery->result, pQuery);
                        FPickingQuery::put(pQuery);
                    }, pQuery
            });
        }
    }
}

void FView::clearPickingQueries() noexcept {
    while (mActivePickingQueriesList) {
        FPickingQuery* const pQuery = mActivePickingQueriesList;
        mActivePickingQueriesList = pQuery->next;
        pQuery->callback(pQuery->result, pQuery);
        FPickingQuery::put(pQuery);
    }
}

void FView::setTemporalAntiAliasingOptions(TemporalAntiAliasingOptions options) noexcept {
    options.feedback = clamp(options.feedback, 0.0f, 1.0f);
    options.filterWidth = std::max(0.2f, options.filterWidth); // below 0.2 causes issues
    mTemporalAntiAliasingOptions = options;
}

void FView::setMultiSampleAntiAliasingOptions(MultiSampleAntiAliasingOptions options) noexcept {
    options.sampleCount = uint8_t(options.sampleCount < 1u ? 1u : options.sampleCount);
    mMultiSampleAntiAliasingOptions = options;
    assert_invariant(!options.enabled || !mRenderTarget || !mRenderTarget->hasSampleableDepth());
}

void FView::setScreenSpaceReflectionsOptions(ScreenSpaceReflectionsOptions options) noexcept {
    options.thickness = std::max(0.0f, options.thickness);
    options.bias = std::max(0.0f, options.bias);
    options.maxDistance = std::max(0.0f, options.maxDistance);
    options.stride = std::max(1.0f, options.stride);
    mScreenSpaceReflectionsOptions = options;
}

void FView::setGuardBandOptions(GuardBandOptions const options) noexcept {
    mGuardBandOptions = options;
}

void FView::setAmbientOcclusionOptions(AmbientOcclusionOptions options) noexcept {
    options.radius = max(0.0f, options.radius);
    options.power = std::max(0.0f, options.power);
    options.bias = clamp(options.bias, 0.0f, 0.1f);
    // snap to the closer of 0.5 or 1.0
    options.resolution = std::floor(
            clamp(options.resolution * 2.0f, 1.0f, 2.0f) + 0.5f) * 0.5f;
    options.intensity = std::max(0.0f, options.intensity);
    options.bilateralThreshold = std::max(0.0f, options.bilateralThreshold);
    options.minHorizonAngleRad = clamp(options.minHorizonAngleRad, 0.0f, f::PI_2);
    options.ssct.lightConeRad = clamp(options.ssct.lightConeRad, 0.0f, f::PI_2);
    options.ssct.shadowDistance = std::max(0.0f, options.ssct.shadowDistance);
    options.ssct.contactDistanceMax = std::max(0.0f, options.ssct.contactDistanceMax);
    options.ssct.intensity = std::max(0.0f, options.ssct.intensity);
    options.ssct.lightDirection = normalize(options.ssct.lightDirection);
    options.ssct.depthBias = std::max(0.0f, options.ssct.depthBias);
    options.ssct.depthSlopeBias = std::max(0.0f, options.ssct.depthSlopeBias);
    options.ssct.sampleCount = clamp(unsigned(options.ssct.sampleCount), 1u, 255u);
    options.ssct.rayCount = clamp(unsigned(options.ssct.rayCount), 1u, 255u);
    mAmbientOcclusionOptions = options;
}
void FView::setVsmShadowOptions(VsmShadowOptions options) noexcept {
    options.msaaSamples = std::max(uint8_t(0), options.msaaSamples);
    mVsmShadowOptions = options;
}

void FView::setSoftShadowOptions(SoftShadowOptions options) noexcept {
    options.penumbraScale = std::max(0.0f, options.penumbraScale);
    options.penumbraRatioScale = std::max(1.0f, options.penumbraRatioScale);
    mSoftShadowOptions = options;
}

void FView::setBloomOptions(BloomOptions options) noexcept {
    options.dirtStrength = saturate(options.dirtStrength);
    options.resolution = clamp(options.resolution, 2u, 2048u);
    options.levels = clamp(options.levels, uint8_t(1),
            FTexture::maxLevelCount(options.resolution));
    options.highlight = std::max(10.0f, options.highlight);
    mBloomOptions = options;
}

void FView::setFogOptions(FogOptions options) noexcept {
    options.distance = std::max(0.0f, options.distance);
    options.maximumOpacity = clamp(options.maximumOpacity, 0.0f, 1.0f);
    options.density = std::max(0.0f, options.density);
    options.heightFalloff = std::max(0.0f, options.heightFalloff);
    options.inScatteringStart = std::max(0.0f, options.inScatteringStart);
    mFogOptions = options;
}

void FView::setDepthOfFieldOptions(DepthOfFieldOptions options) noexcept {
    options.cocScale = std::max(0.0f, options.cocScale);
    options.maxApertureDiameter = std::max(0.0f, options.maxApertureDiameter);
    mDepthOfFieldOptions = options;
}

void FView::setVignetteOptions(VignetteOptions options) noexcept {
    options.roundness = saturate(options.roundness);
    options.midPoint = saturate(options.midPoint);
    options.feather = clamp(options.feather, 0.05f, 1.0f);
    mVignetteOptions = options;
}

View::PickingQuery& FView::pick(uint32_t const x, uint32_t const y, CallbackHandler* handler,
        PickingQueryResultCallback const callback) noexcept {
    FPickingQuery* pQuery = FPickingQuery::get(x, y, handler, callback);
    pQuery->next = mActivePickingQueriesList;
    mActivePickingQueriesList = pQuery;
    return *pQuery;
}

void FView::setStereoscopicOptions(const StereoscopicOptions& options) noexcept {
    mStereoscopicOptions = options;
}

View::FroxelConfigurationInfoWithAge FView::getFroxelConfigurationInfo() const noexcept {
    return { mFroxelizer.getFroxelConfigurationInfo(), mFroxelConfigurationAge };
}

void FView::setMaterialGlobal(uint32_t const index, float4 const& value) {
    FILAMENT_CHECK_PRECONDITION(index < 4)
            << "material global variable index (" << +index << ") out of range";
    mMaterialGlobals[index] = value;
}

float4 FView::getMaterialGlobal(uint32_t const index) const {
    FILAMENT_CHECK_PRECONDITION(index < 4)
            << "material global variable index (" << +index << ") out of range";
    return mMaterialGlobals[index];
}

} // namespace filament
