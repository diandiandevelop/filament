/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_DETAILS_VIEW_H
#define TNT_FILAMENT_DETAILS_VIEW_H

#include "downcast.h"

#include "Allocators.h"
#include "BufferPoolAllocator.h"
#include "Culler.h"
#include "FrameHistory.h"
#include "FrameInfo.h"
#include "Froxelizer.h"
#include "PIDController.h"
#include "ShadowMapManager.h"

#include "ds/ColorPassDescriptorSet.h"
#include "ds/DescriptorSet.h"
#include "ds/TypedUniformBuffer.h"

#include "components/LightManager.h"
#include "components/RenderableManager.h"

#include "details/Camera.h"
#include "details/ColorGrading.h"
#include "details/RenderTarget.h"
#include "details/Scene.h"

#include <private/filament/UibStructs.h>

#include <filament/Frustum.h>
#include <filament/Renderer.h>
#include <filament/View.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/StructureOfArrays.h>
#include <utils/Range.h>
#include <utils/Slice.h>

#if FILAMENT_ENABLE_FGVIEWER
#include <fgviewer/DebugServer.h>
#else
namespace filament::fgviewer {
    using ViewHandle = uint32_t;
}
#endif

#include <math/mat4.h>
#include <math/vec4.h>

#include <array>
#include <memory>
#include <new>

#include <stddef.h>
#include <stdint.h>

namespace utils {
class JobSystem;
} // namespace utils;

// Avoid warnings for using the deprecated APIs.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

namespace filament {

class FEngine;
class FMaterialInstance;
class FRenderer;
class FScene;

// ------------------------------------------------------------------------------------------------

/**
 * View 实现类
 * 
 * View 表示一个渲染视图，包含相机、场景、渲染选项等。
 * 每个 View 可以独立配置渲染参数（抗锯齿、动态分辨率、阴影等）。
 * 
 * 实现细节：
 * - 管理描述符堆（per-view uniforms、per-renderable uniforms）
 * - 管理 Froxelizer（用于动态光照）
 * - 管理阴影贴图管理器
 * - 支持动态分辨率（使用 PID 控制器）
 * - 支持立体渲染
 */
class FView : public View {
public:
    using MaterialGlobals = std::array<math::float4, 4>;  // 材质全局变量类型（4 个 float4）
    using Range = utils::Range<uint32_t>;  // 范围类型别名

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FView(FEngine& engine);
    
    /**
     * 析构函数
     */
    ~FView() noexcept;

    /**
     * 终止
     * 
     * 释放所有资源。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 计算相机信息
     * 
     * 从视图的相机计算相机信息（投影矩阵、视图矩阵等）。
     * 
     * @param engine 引擎常量引用
     * @return 相机信息
     */
    CameraInfo computeCameraInfo(FEngine const& engine) const noexcept;

    /**
     * 准备
     * 
     * 准备视图进行渲染。这是每帧调用的主要方法。
     * 
     * 注意：viewport 和 cameraInfo 按值传递，以明确 prepare 不能保持对它们的引用，
     * 这些引用会在 prepare() 的作用域之外（例如，使用 JobSystem）。
     * 
     * @param engine 引擎引用
     * @param driver 驱动 API 引用
     * @param rootArenaScope 根内存池作用域
     * @param viewport 视口
     * @param cameraInfo 相机信息
     * @param userTime 用户时间（float4，包含时间、帧计数等）
     * @param needsAlphaChannel 是否需要 Alpha 通道
     */
    void prepare(FEngine& engine, backend::DriverApi& driver, RootArenaScope& rootArenaScope,
            Viewport viewport, CameraInfo cameraInfo,
            math::float4 const& userTime, bool needsAlphaChannel) noexcept;

    void setScene(FScene* scene) { mScene = scene; }
    FScene const* getScene() const noexcept { return mScene; }
    FScene* getScene() noexcept { return mScene; }

    void setCullingCamera(FCamera* camera) noexcept { mCullingCamera = camera; }
    void setViewingCamera(FCamera* camera) noexcept { mViewingCamera = camera; }

    void setViewport(Viewport const& viewport) noexcept;
    Viewport const& getViewport() const noexcept {
        return mViewport;
    }

    bool getClearTargetColor() const noexcept {
        // don't clear the color buffer if we have a skybox
        return !isSkyboxVisible();
    }
    bool isSkyboxVisible() const noexcept;

    void setFrustumCullingEnabled(bool const culling) noexcept { mCulling = culling; }
    bool isFrustumCullingEnabled() const noexcept { return mCulling; }

    void setFrontFaceWindingInverted(bool const inverted) noexcept { mFrontFaceWindingInverted = inverted; }
    bool isFrontFaceWindingInverted() const noexcept { return mFrontFaceWindingInverted; }

    void setTransparentPickingEnabled(bool const enabled) noexcept { mIsTransparentPickingEnabled = enabled; }
    bool isTransparentPickingEnabled() const noexcept { return mIsTransparentPickingEnabled; }


    void setVisibleLayers(uint8_t select, uint8_t values) noexcept;
    uint8_t getVisibleLayers() const noexcept {
        return mVisibleLayers;
    }

    /**
     * 设置名称
     * 
     * @param name 视图名称
     */
    void setName(const char* name) noexcept {
        mName = utils::CString(name);  // 保存名称
    }

    /**
     * 获取名称
     * 
     * 返回视图的名称。指针由 View 拥有。
     * 
     * @return 视图名称（C 字符串）
     */
    const char* getName() const noexcept {
        return mName.c_str_safe();  // 返回名称字符串
    }

    /**
     * 准备相机
     * 
     * 准备相机的 uniform 数据（投影矩阵、视图矩阵等）。
     * 
     * @param engine 引擎引用
     * @param cameraInfo 相机信息
     */
    void prepareCamera(FEngine& engine, const CameraInfo& cameraInfo) const noexcept;

    /**
     * 准备 LOD 偏差
     * 
     * 准备细节级别（LOD）偏差和导数缩放。
     * 
     * @param bias LOD 偏差
     * @param derivativesScale 导数缩放（用于纹理 LOD 计算）
     */
    void prepareLodBias(float bias, math::float2 derivativesScale) const noexcept;

    /**
     * 准备视口
     * 
     * 准备物理视口和逻辑视口的 uniform 数据。
     * 
     * @param physicalViewport 物理视口（实际渲染尺寸）
     * @param logicalViewport 逻辑视口（用户指定的视口）
     */
    void prepareViewport(
            const Viewport& physicalViewport,
            const Viewport& logicalViewport) const noexcept;

    /**
     * 准备阴影
     * 
     * 准备阴影相关的数据（阴影贴图、阴影参数等）。
     * 
     * @param engine 引擎引用
     * @param renderableData 可渲染对象数据
     * @param lightData 光源数据
     * @param cameraInfo 相机信息
     */
    void prepareShadowing(FEngine& engine, FScene::RenderableSoa& renderableData,
            FScene::LightSoa const& lightData, CameraInfo const& cameraInfo) noexcept;
    
    /**
     * 准备光照
     * 
     * 准备光照相关的数据（光源列表、Froxel 数据等）。
     * 
     * @param engine 引擎引用
     * @param cameraInfo 相机信息
     */
    void prepareLighting(FEngine& engine, CameraInfo const& cameraInfo) noexcept;

    /**
     * 准备 SSAO（屏幕空间环境光遮蔽）
     * 
     * 设置 SSAO 纹理。
     * 
     * @param ssao SSAO 纹理句柄
     */
    void prepareSSAO(backend::Handle<backend::HwTexture> ssao) const noexcept;
    
    /**
     * 准备 SSAO（带选项）
     * 
     * 设置 SSAO 选项。
     * 
     * @param options SSAO 选项
     */
    void prepareSSAO(AmbientOcclusionOptions const& options) const noexcept;

    /**
     * 准备 SSR（屏幕空间反射）
     * 
     * 设置 SSR 纹理。
     * 
     * @param ssr SSR 纹理句柄
     */
    void prepareSSR(backend::Handle<backend::HwTexture> ssr) const noexcept;
    
    /**
     * 准备 SSR（带选项）
     * 
     * 设置 SSR 选项和参数。
     * 
     * @param engine 引擎引用
     * @param cameraInfo 相机信息
     * @param refractionLodOffset 折射 LOD 偏移
     * @param options SSR 选项
     */
    void prepareSSR(FEngine& engine, CameraInfo const& cameraInfo,
            float refractionLodOffset, ScreenSpaceReflectionsOptions const& options) const noexcept;

    /**
     * 准备结构纹理
     * 
     * 设置结构纹理（用于 SSR 和阴影）。
     * 
     * @param structure 结构纹理句柄
     */
    void prepareStructure(backend::Handle<backend::HwTexture> structure) const noexcept;
    
    /**
     * 准备阴影映射（带结构纹理）
     * 
     * 准备阴影映射的 uniform 数据。
     * 
     * @param engine 引擎常量引用
     * @param structure 结构纹理句柄
     */
    void prepareShadowMapping(FEngine const& engine, backend::Handle<backend::HwTexture> structure) const noexcept;
    
    /**
     * 准备阴影映射
     * 
     * 准备阴影映射的 uniform 数据（使用当前结构纹理）。
     */
    void prepareShadowMapping() const noexcept;

    /**
     * 提交 Froxel 数据
     * 
     * 将 Froxel 数据提交到 GPU。
     * 
     * @param driverApi 驱动 API 引用
     */
    void commitFroxels(backend::DriverApi& driverApi) const noexcept;
    
    /**
     * 提交 Uniform 数据
     * 
     * 将 uniform 数据提交到 GPU。
     * 
     * @param driver 驱动 API 引用
     */
    void commitUniforms(backend::DriverApi& driver) const noexcept;
    
    /**
     * 提交描述符堆
     * 
     * 将描述符堆提交到 GPU。
     * 
     * @param driver 驱动 API 引用
     */
    void commitDescriptorSet(backend::DriverApi& driver) const noexcept;

    utils::JobSystem::Job* getFroxelizerSync() const noexcept { return mFroxelizerSync; }
    void setFroxelizerSync(utils::JobSystem::Job* sync) noexcept { mFroxelizerSync = sync; }

    /**
     * 是否有方向光照
     * 
     * 最终决定是否使用 DIR（方向光）变体。
     * 
     * @return 如果有方向光照返回 true，否则返回 false
     */
    bool hasDirectionalLighting() const noexcept { return mHasDirectionalLighting; }

    /**
     * 是否有动态光照
     * 
     * 最终决定是否使用 DYN（动态光照）变体。
     * 
     * @return 如果有动态光照返回 true，否则返回 false
     */
    bool hasDynamicLighting() const noexcept { return mHasDynamicLighting; }

    /**
     * 是否有阴影
     * 
     * 最终决定是否使用 SRE（屏幕空间反射）变体。
     * 
     * @return 如果有阴影返回 true，否则返回 false
     */
    bool hasShadowing() const noexcept { return mHasShadowing; }

    bool needsDirectionalShadowMaps() const noexcept { return mHasShadowing && mHasDirectionalLighting; }
    bool needsPointShadowMaps() const noexcept { return mHasShadowing && mHasDynamicLighting; }
    bool needsShadowMap() const noexcept { return mNeedsShadowMap; }
    bool hasFog() const noexcept { return mFogOptions.enabled && mFogOptions.density > 0.0f; }
    bool hasVSM() const noexcept { return mShadowType == ShadowType::VSM; }
    bool hasDPCF() const noexcept { return mShadowType == ShadowType::DPCF; }
    bool hasPCSS() const noexcept { return mShadowType == ShadowType::PCSS; }
    bool hasPicking() const noexcept { return mActivePickingQueriesList != nullptr; }
    bool hasStereo() const noexcept {
        return mIsStereoSupported && mStereoscopicOptions.enabled;
    }

    void setChannelDepthClearEnabled(uint8_t const channel, bool const enabled) noexcept {
        mChannelDepthClearMask.set(channel, enabled);
    }

    bool isChannelDepthClearEnabled(uint8_t channel) const noexcept {
        return mChannelDepthClearMask[channel];
    }

    utils::bitset32 getChannelDepthClearMask() const noexcept { return mChannelDepthClearMask; }

    FrameGraphId<FrameGraphTexture> renderShadowMaps(FEngine& engine, FrameGraph& fg,
            CameraInfo const& cameraInfo, math::float4 const& userTime,
            RenderPassBuilder const& passBuilder) noexcept;

    static void updatePrimitivesLod(FScene::RenderableSoa& renderableData,
            FEngine const& engine, CameraInfo const& camera,
            Range visible) noexcept;

    void setShadowingEnabled(bool const enabled) noexcept { mShadowingEnabled = enabled; }

    bool isShadowingEnabled() const noexcept { return mShadowingEnabled; }

    void setScreenSpaceRefractionEnabled(bool const enabled) noexcept { mScreenSpaceRefractionEnabled = enabled; }

    bool isScreenSpaceRefractionEnabled() const noexcept { return mScreenSpaceRefractionEnabled; }

    bool isScreenSpaceReflectionEnabled() const noexcept { return mScreenSpaceReflectionsOptions.enabled; }

    void setStencilBufferEnabled(bool const enabled) noexcept { mStencilBufferEnabled = enabled; }

    bool isStencilBufferEnabled() const noexcept { return mStencilBufferEnabled; }

    void setStereoscopicOptions(StereoscopicOptions const& options) noexcept;

    utils::FixedCapacityVector<Camera const*> getDirectionalShadowCameras() const noexcept {
        if (!mShadowMapManager) return {};
        return mShadowMapManager->getDirectionalShadowCameras();
    }

    void setFroxelVizEnabled(bool const enabled) noexcept {
        mFroxelVizEnabled = enabled;
    }

    FroxelConfigurationInfoWithAge getFroxelConfigurationInfo() const noexcept;

    void setRenderTarget(FRenderTarget* renderTarget) noexcept {
        assert_invariant(!renderTarget || !mMultiSampleAntiAliasingOptions.enabled ||
                !renderTarget->hasSampleableDepth());
        mRenderTarget = renderTarget;
    }

    FRenderTarget* getRenderTarget() const noexcept {
        return mRenderTarget;
    }

    void setSampleCount(uint8_t count) noexcept {
        count = uint8_t(count < 1u ? 1u : count);
        mMultiSampleAntiAliasingOptions.sampleCount = count;
        mMultiSampleAntiAliasingOptions.enabled = count > 1u;
    }

    uint8_t getSampleCount() const noexcept {
        return mMultiSampleAntiAliasingOptions.sampleCount;
    }

    void setAntiAliasing(AntiAliasing const type) noexcept {
        mAntiAliasing = type;
    }

    AntiAliasing getAntiAliasing() const noexcept {
        return mAntiAliasing;
    }

    void setTemporalAntiAliasingOptions(TemporalAntiAliasingOptions options) noexcept ;

    const TemporalAntiAliasingOptions& getTemporalAntiAliasingOptions() const noexcept {
        return mTemporalAntiAliasingOptions;
    }

    void setMultiSampleAntiAliasingOptions(MultiSampleAntiAliasingOptions options) noexcept;

    const MultiSampleAntiAliasingOptions& getMultiSampleAntiAliasingOptions() const noexcept {
        return mMultiSampleAntiAliasingOptions;
    }

    void setScreenSpaceReflectionsOptions(ScreenSpaceReflectionsOptions options) noexcept;

    const ScreenSpaceReflectionsOptions& getScreenSpaceReflectionsOptions() const noexcept {
        return mScreenSpaceReflectionsOptions;
    }

    void setGuardBandOptions(GuardBandOptions options) noexcept;

    GuardBandOptions const& getGuardBandOptions() const noexcept {
        return mGuardBandOptions;
    }

    void setColorGrading(FColorGrading const* colorGrading) noexcept {
        mColorGrading = colorGrading == nullptr ? mDefaultColorGrading : colorGrading;
    }

    const FColorGrading* getColorGrading() const noexcept {
        return mColorGrading;
    }

    void setDithering(Dithering const dithering) noexcept {
        mDithering = dithering;
    }

    Dithering getDithering() const noexcept {
        return mDithering;
    }

    const StereoscopicOptions& getStereoscopicOptions() const noexcept {
        return mStereoscopicOptions;
    }

    bool hasPostProcessPass() const noexcept {
        return mHasPostProcessPass;
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
    math::float2 updateScale(FEngine& engine,
            details::FrameInfo const& info,
            Renderer::FrameRateOptions const& frameRateOptions,
            Renderer::DisplayInfo const& displayInfo) noexcept;

    /**
     * 设置动态分辨率选项
     * 
     * 配置动态分辨率系统，根据性能自动调整渲染分辨率。
     * 
     * @param options 动态分辨率选项
     */
    void setDynamicResolutionOptions(DynamicResolutionOptions const& options) noexcept;

    /**
     * 获取动态分辨率选项
     * 
     * @return 动态分辨率选项
     */
    DynamicResolutionOptions getDynamicResolutionOptions() const noexcept {
        return mDynamicResolution;
    }

    /**
     * 获取最后动态分辨率缩放比例
     * 
     * @return 最后的缩放比例（float2，x 和 y 方向）
     */
    math::float2 getLastDynamicResolutionScale() const noexcept {
        return mScale;
    }

    /**
     * 设置渲染质量
     * 
     * @param renderQuality 渲染质量
     */
    void setRenderQuality(RenderQuality const& renderQuality) noexcept {
        mRenderQuality = renderQuality;
    }

    /**
     * 获取渲染质量
     * 
     * @return 渲染质量
     */
    RenderQuality getRenderQuality() const noexcept {
        return mRenderQuality;
    }

    /**
     * 设置动态光照选项
     * 
     * 设置光源的近远平面，用于 Froxelizer 的光照计算。
     * 
     * @param zLightNear 光源近平面
     * @param zLightFar 光源远平面
     */
    void setDynamicLightingOptions(float zLightNear, float zLightFar) noexcept;

    /**
     * 设置后处理是否启用
     * 
     * @param enabled 是否启用
     */
    void setPostProcessingEnabled(bool const enabled) noexcept {
        mHasPostProcessPass = enabled;
    }

    void setAmbientOcclusion(AmbientOcclusion const ambientOcclusion) noexcept {
        mAmbientOcclusionOptions.enabled = ambientOcclusion == AmbientOcclusion::SSAO;
    }

    AmbientOcclusion getAmbientOcclusion() const noexcept {
        return mAmbientOcclusionOptions.enabled ? AmbientOcclusion::SSAO : AmbientOcclusion::NONE;
    }

    void setAmbientOcclusionOptions(AmbientOcclusionOptions options) noexcept;

    ShadowType getShadowType() const noexcept {
        return mShadowType;
    }

    void setShadowType(ShadowType const shadow) noexcept {
        mShadowType = shadow;
    }

    void setVsmShadowOptions(VsmShadowOptions options) noexcept;

    VsmShadowOptions getVsmShadowOptions() const noexcept {
        return mVsmShadowOptions;
    }

    void setSoftShadowOptions(SoftShadowOptions options) noexcept;

    SoftShadowOptions getSoftShadowOptions() const noexcept {
        return mSoftShadowOptions;
    }

    AmbientOcclusionOptions const& getAmbientOcclusionOptions() const noexcept {
        return mAmbientOcclusionOptions;
    }

    void setBloomOptions(BloomOptions options) noexcept;

    BloomOptions getBloomOptions() const noexcept {
        return mBloomOptions;
    }

    void setFogOptions(FogOptions options) noexcept;

    FogOptions getFogOptions() const noexcept {
        return mFogOptions;
    }

    void setDepthOfFieldOptions(DepthOfFieldOptions options) noexcept;

    DepthOfFieldOptions getDepthOfFieldOptions() const noexcept {
        return mDepthOfFieldOptions;
    }

    void setVignetteOptions(VignetteOptions options) noexcept;

    VignetteOptions getVignetteOptions() const noexcept {
        return mVignetteOptions;
    }

    void setBlendMode(BlendMode const blendMode) noexcept {
        mBlendMode = blendMode;
    }

    BlendMode getBlendMode() const noexcept {
        return mBlendMode;
    }

    /**
     * 获取可见可渲染对象范围
     * 
     * @return 可见可渲染对象范围常量引用
     */
    Range const& getVisibleRenderables() const noexcept {
        return mVisibleRenderables;
    }

    /**
     * 获取可见方向光阴影投射者范围
     * 
     * @return 可见方向光阴影投射者范围常量引用
     */
    Range const& getVisibleDirectionalShadowCasters() const noexcept {
        return mVisibleDirectionalShadowCasters;
    }

    /**
     * 获取可见聚光灯阴影投射者范围
     * 
     * @return 可见聚光灯阴影投射者范围常量引用
     */
    Range const& getVisibleSpotShadowCasters() const noexcept {
        return mSpotLightShadowCasters;
    }

    /**
     * 获取用户相机（常量版本）
     * 
     * @return 用户相机常量引用
     */
    FCamera const& getCameraUser() const noexcept { return *mCullingCamera; }
    
    /**
     * 获取用户相机
     * 
     * @return 用户相机引用
     */
    FCamera& getCameraUser() noexcept { return *mCullingCamera; }
    
    /**
     * 设置用户相机
     * 
     * @param camera 相机指针
     */
    void setCameraUser(FCamera* camera) noexcept { setCullingCamera(camera); }

    /**
     * 是否有相机
     * 
     * @return 如果有相机返回 true，否则返回 false
     */
    bool hasCamera() const noexcept {
        return mCullingCamera != nullptr;
    }

    /**
     * 获取渲染目标句柄
     * 
     * @return 渲染目标句柄，如果没有渲染目标返回空句柄
     */
    backend::Handle<backend::HwRenderTarget> getRenderTargetHandle() const noexcept {
        backend::Handle<backend::HwRenderTarget> const kEmptyHandle;
        return mRenderTarget == nullptr ? kEmptyHandle : mRenderTarget->getHwHandle();
    }

    /**
     * 获取渲染目标附件掩码
     * 
     * @return 渲染目标附件掩码，如果没有渲染目标返回 NONE
     */
    backend::TargetBufferFlags getRenderTargetAttachmentMask() const noexcept {
        if (mRenderTarget == nullptr) {
            return backend::TargetBufferFlags::NONE;
        }
        return mRenderTarget->getAttachmentMask();
    }

    /**
     * 剔除可渲染对象（静态方法）
     * 
     * 使用视锥体剔除可渲染对象。
     * 
     * @param js 作业系统引用
     * @param renderableData 可渲染对象数据
     * @param frustum 视锥体
     * @param bit 位标志
     */
    static void cullRenderables(utils::JobSystem& js, FScene::RenderableSoa& renderableData,
            Frustum const& frustum, size_t bit) noexcept;

    /**
     * 获取颜色通道描述符堆
     * 
     * 根据阴影类型返回相应的描述符堆（PCF 使用索引 0，其他使用索引 1）。
     * 
     * @return 颜色通道描述符堆引用
     */
    ColorPassDescriptorSet& getColorPassDescriptorSet() const noexcept {
            return mColorPassDescriptorSet[mShadowType == ShadowType::PCF ? 0 : 1];
    }

    /**
     * 获取帧历史（非常量版本）
     * 
     * 返回帧历史 FIFO。这通常由 FrameGraph 用于访问前一帧数据。
     * 
     * @return 帧历史引用
     */
    FrameHistory& getFrameHistory() noexcept { return mFrameHistory; }
    
    /**
     * 获取帧历史（常量版本）
     * 
     * @return 帧历史常量引用
     */
    FrameHistory const& getFrameHistory() const noexcept { return mFrameHistory; }

    /**
     * 提交帧历史
     * 
     * 清理最旧的帧并保存当前帧信息。
     * 这通常在视图的所有渲染操作完成后调用（例如：在 FrameGraph 执行后）。
     * 
     * @param engine 引擎引用
     */
    void commitFrameHistory(FEngine& engine) noexcept;

    /**
     * 清除帧历史
     * 
     * 清理整个历史，释放所有资源。
     * 这通常在视图被终止时调用，或者当我们更改 Renderer 时。
     * 
     * @param engine 引擎引用
     */
    void clearFrameHistory(FEngine& engine) noexcept;

    /**
     * 创建拾取查询
     * 
     * 在指定位置创建拾取查询。
     * 
     * @param x X 坐标
     * @param y Y 坐标
     * @param handler 回调处理器
     * @param callback 拾取查询结果回调
     * @return 拾取查询引用
     */
    PickingQuery& pick(uint32_t x, uint32_t y, backend::CallbackHandler* handler,
            PickingQueryResultCallback callback) noexcept;

    /**
     * 执行拾取查询
     * 
     * 执行所有待处理的拾取查询。
     * 
     * @param driver 驱动 API 引用
     * @param handle 渲染目标句柄
     * @param scale 缩放比例（用于坐标转换）
     */
    void executePickingQueries(backend::DriverApi& driver,
            backend::RenderTargetHandle handle, math::float2 scale) noexcept;

    /**
     * 清除拾取查询
     * 
     * 清除所有待处理的拾取查询。
     */
    void clearPickingQueries() noexcept;

    /**
     * 设置材质全局变量
     * 
     * 设置材质的全局变量值。
     * 
     * @param index 索引（0-3，对应 4 个 float4）
     * @param value 值（float4）
     */
    void setMaterialGlobal(uint32_t index, math::float4 const& value);

    /**
     * 获取材质全局变量
     * 
     * @param index 索引（0-3，对应 4 个 float4）
     * @return 值（float4）
     */
    math::float4 getMaterialGlobal(uint32_t index) const;

    /**
     * 获取雾实体
     * 
     * @return 雾实体
     */
    utils::Entity getFogEntity() const noexcept {
        return mFogEntity;
    }

    /**
     * 获取帧 Uniform 缓冲区
     * 
     * @return 帧 Uniform 缓冲区引用
     */
    TypedUniformBuffer<PerViewUib>& getFrameUniforms() const noexcept {
        return mUniforms;
    }

    /**
     * 获取视图句柄（用于 FrameGraph 查看器）
     * 
     * @return 视图句柄
     */
    fgviewer::ViewHandle getViewHandle() const noexcept {
        return mFrameGraphViewerViewHandle;
    }

    /**
     * 获取材质全局变量
     * 
     * @return 材质全局变量数组（4 个 float4）
     */
    MaterialGlobals getMaterialGlobals() const { return mMaterialGlobals; }

private:
    /**
     * 拾取查询结构
     * 
     * 用于实现拾取查询功能。
     */
    struct FPickingQuery : public PickingQuery {
    private:
        /**
         * 构造函数
         * 
         * @param x X 坐标
         * @param y Y 坐标
         * @param handler 回调处理器
         * @param callback 拾取查询结果回调
         */
        FPickingQuery(uint32_t const x, uint32_t const y,
                backend::CallbackHandler* handler,
                PickingQueryResultCallback const callback) noexcept
                : PickingQuery{}, x(x), y(y), handler(handler), callback(callback) {}
        
        /**
         * 析构函数
         */
        ~FPickingQuery() noexcept = default;
    public:
        /**
         * 获取拾取查询实例
         * 
         * TODO: 使用小对象池优化
         * 
         * @param x X 坐标
         * @param y Y 坐标
         * @param handler 回调处理器
         * @param callback 拾取查询结果回调
         * @return 拾取查询指针
         */
        static FPickingQuery* get(uint32_t const x, uint32_t const y, backend::CallbackHandler* handler,
                PickingQueryResultCallback const callback) noexcept {
            return new(std::nothrow) FPickingQuery(x, y, handler, callback);
        }
        
        /**
         * 释放拾取查询实例
         * 
         * @param pQuery 拾取查询指针
         */
        static void put(FPickingQuery const* pQuery) noexcept {
            delete pQuery;
        }
        
        mutable FPickingQuery* next = nullptr;  // 下一个查询（链表）
        
        /**
         * 拾取查询参数
         */
        uint32_t const x;  // X 坐标
        uint32_t const y;  // Y 坐标
        backend::CallbackHandler* const handler;  // 回调处理器
        PickingQueryResultCallback const callback;  // 拾取查询结果回调
        
        /**
         * 拾取查询结果
         */
        PickingQueryResult result{};  // 查询结果
    };

    /**
     * 准备可见可渲染对象
     * 
     * 使用视锥体剔除准备可见的可渲染对象。
     * 
     * @param js 作业系统引用
     * @param frustum 视锥体
     * @param renderableData 可渲染对象数据
     */
    void prepareVisibleRenderables(utils::JobSystem& js,
            Frustum const& frustum, FScene::RenderableSoa& renderableData) const noexcept;

    /**
     * 更新 UBO
     * 
     * 更新可见可渲染对象的统一缓冲区对象。
     * 
     * @param driver 驱动 API 引用
     * @param renderableData 可渲染对象数据
     * @param visibleRenderables 可见可渲染对象范围
     */
    void updateUBOs(backend::DriverApi& driver,
            FScene::RenderableSoa& renderableData,
            utils::Range<uint32_t> visibleRenderables) noexcept;

    /**
     * 准备可见光源（静态方法）
     * 
     * 使用视锥体剔除准备可见的光源。
     * 
     * @param lcm 光源管理器常量引用
     * @param scratch 临时缓冲区
     * @param viewMatrix 视图矩阵
     * @param frustum 视锥体
     * @param lightData 光源数据
     */
    static void prepareVisibleLights(FLightManager const& lcm,
            utils::Slice<float> scratch,
            math::mat4f const& viewMatrix, Frustum const& frustum,
            FScene::LightSoa& lightData) noexcept;

    /**
     * 计算光源相机距离（静态内联方法）
     * 
     * 计算光源到相机的距离，用于排序。
     * 
     * @param distances 输出距离数组
     * @param viewMatrix 视图矩阵
     * @param spheres 光源包围球数组
     * @param count 数量
     */
    static inline void computeLightCameraDistances(float* distances,
            math::mat4f const& viewMatrix, const math::float4* spheres, size_t count) noexcept;

    /**
     * 计算可见性掩码（静态方法）
     * 
     * 根据可见层和可见性标志计算可见性掩码。
     * 
     * @param visibleLayers 可见层
     * @param layers 层数组
     * @param visibility 可见性数组
     * @param visibleMask 输出可见性掩码数组
     * @param count 数量
     */
    static void computeVisibilityMasks(
            uint8_t visibleLayers, uint8_t const* layers,
            FRenderableManager::Visibility const* visibility,
            Culler::result_type* visibleMask,
            size_t count);

    /**
     * 分区（静态方法）
     * 
     * 根据掩码和值对可渲染对象进行分区。
     * 
     * 注意：我们不内联这个方法，因为函数相当大，内联没有太大收益。
     * 
     * @param begin 开始迭代器
     * @param end 结束迭代器
     * @param mask 掩码
     * @param value 值
     * @return 分区点迭代器
     */
    static FScene::RenderableSoa::iterator partition(
            FScene::RenderableSoa::iterator begin,
            FScene::RenderableSoa::iterator end,
            Culler::result_type mask, Culler::result_type value) noexcept;

    /**
     * 这些在渲染循环中被访问，保持在一起以提高缓存局部性
     */
    backend::Handle<backend::HwBufferObject> mLightUbh;  // 光源统一缓冲区句柄
    backend::Handle<backend::HwBufferObject> mRenderableUbh;  // 可渲染对象统一缓冲区句柄
    DescriptorSet mCommonRenderableDescriptorSet;  // 通用可渲染对象描述符堆

    /**
     * 场景指针
     */
    FScene* mScene = nullptr;
    
    /**
     * 用户设置的相机，用于剔除和查看
     */
    FCamera* mCullingCamera = nullptr;
    
    /**
     * 可选的（调试）相机，仅用于查看
     */
    FCamera* mViewingCamera = nullptr;

    /**
     * Froxelizer（用于动态光照）
     */
    mutable Froxelizer mFroxelizer;
    
    /**
     * Froxelizer 同步作业
     */
    utils::JobSystem::Job* mFroxelizerSync = nullptr;
    
    /**
     * Froxel 可视化是否启用
     */
    bool mFroxelVizEnabled = false;
    
    /**
     * Froxel 配置年龄（用于检测配置变化）
     */
    uint32_t mFroxelConfigurationAge = 0;

    /**
     * 视口
     */
    Viewport mViewport;
    
    /**
     * 是否启用剔除
     */
    bool mCulling = true;
    
    /**
     * 前表面缠绕顺序是否反转
     */
    bool mFrontFaceWindingInverted = false;
    
    /**
     * 是否启用透明拾取
     */
    bool mIsTransparentPickingEnabled = false;

    /**
     * 渲染目标指针
     */
    FRenderTarget* mRenderTarget = nullptr;

    /**
     * 可见层（位掩码）
     */
    uint8_t mVisibleLayers = 0x1;
    
    /**
     * 抗锯齿类型
     */
    AntiAliasing mAntiAliasing = AntiAliasing::FXAA;
    
    /**
     * 抖动类型
     */
    Dithering mDithering = Dithering::TEMPORAL;
    
    /**
     * 是否启用阴影
     */
    bool mShadowingEnabled = true;
    
    /**
     * 是否启用屏幕空间折射
     */
    bool mScreenSpaceRefractionEnabled = true;
    
    /**
     * 是否有后处理通道
     */
    bool mHasPostProcessPass = true;
    
    /**
     * 是否启用模板缓冲区
     */
    bool mStencilBufferEnabled = false;
    AmbientOcclusionOptions mAmbientOcclusionOptions{};
    ShadowType mShadowType = ShadowType::PCF;
    VsmShadowOptions mVsmShadowOptions; // FIXME: this should probably be per-light
    SoftShadowOptions mSoftShadowOptions;
    BloomOptions mBloomOptions;
    FogOptions mFogOptions;
    DepthOfFieldOptions mDepthOfFieldOptions;
    VignetteOptions mVignetteOptions;
    TemporalAntiAliasingOptions mTemporalAntiAliasingOptions;
    MultiSampleAntiAliasingOptions mMultiSampleAntiAliasingOptions;
    ScreenSpaceReflectionsOptions mScreenSpaceReflectionsOptions;
    GuardBandOptions mGuardBandOptions;
    StereoscopicOptions mStereoscopicOptions;
    BlendMode mBlendMode = BlendMode::OPAQUE;
    const FColorGrading* mColorGrading = nullptr;
    const FColorGrading* mDefaultColorGrading = nullptr;
    utils::Entity mFogEntity{};
    bool mIsStereoSupported : 1;
    utils::bitset32 mChannelDepthClearMask{};

    PIDController mPidController;
    DynamicResolutionOptions mDynamicResolution;
    math::float2 mScale = 1.0f;
    bool mIsDynamicResolutionSupported = false;

    RenderQuality mRenderQuality;

    mutable TypedUniformBuffer<PerViewUib> mUniforms;
    mutable ColorPassDescriptorSet mColorPassDescriptorSet[2];

    mutable FrameHistory mFrameHistory{};

    FPickingQuery* mActivePickingQueriesList = nullptr;

    utils::CString mName;

    // the following values are set by prepare()
    Range mVisibleRenderables;
    Range mVisibleDirectionalShadowCasters;
    Range mSpotLightShadowCasters;
    uint32_t mRenderableUBOElementCount = 0;
    mutable bool mHasDirectionalLighting = false;
    mutable bool mHasDynamicLighting = false;
    mutable bool mHasShadowing = false;
    mutable bool mNeedsShadowMap = false;

    // State shared between Scene and driver callbacks.
    struct SharedState {
        BufferPoolAllocator<3> mBufferPoolAllocator = {};
    };
    std::shared_ptr<SharedState> mSharedState;

    std::unique_ptr<ShadowMapManager> mShadowMapManager;

    MaterialGlobals mMaterialGlobals = {{
            { 0, 0, 0, 1 },
            { 0, 0, 0, 1 },
            { 0, 0, 0, 1 },
            { 0, 0, 0, 1 },
    }};

    fgviewer::ViewHandle mFrameGraphViewerViewHandle{};

#ifndef NDEBUG
    struct DebugState {
        std::unique_ptr<std::array<DebugRegistry::FrameHistory, 5*60>> debugFrameHistory{};
        bool owner = false;
        bool active = false;
    };
    std::shared_ptr<DebugState> mDebugState{ new DebugState };
#endif
};

FILAMENT_DOWNCAST(View)

} // namespace filament

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // TNT_FILAMENT_DETAILS_VIEW_H
