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

#ifndef TNT_FILAMENT_VIEW_H
#define TNT_FILAMENT_VIEW_H

#include <filament/FilamentAPI.h>
#include <filament/Options.h>

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/FixedCapacityVector.h>

#include <math/mathfwd.h>
#include <math/mat4.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

namespace backend {
class CallbackHandler;
} // namespace backend

class Camera;
class ColorGrading;
class Engine;
class MaterialInstance;
class RenderTarget;
class Scene;
class Viewport;

/**
 * A View encompasses all the state needed for rendering a Scene.
 *
 * Renderer::render() operates on View objects. These View objects specify important parameters
 * such as:
 *  - The Scene
 *  - The Camera
 *  - The Viewport
 *  - Some rendering parameters
 *
 * \note
 * View instances are heavy objects that internally cache a lot of data needed for rendering.
 * It is not advised for an application to use many View objects.
 *
 * For example, in a game, a View could be used for the main scene and another one for the
 * game's user interface. More View instances could be used for creating special effects (e.g.
 * a View is akin to a rendering pass).
 *
 *
 * @see Renderer, Scene, Camera, RenderTarget
 */
/**
 * View 包含渲染 Scene 所需的所有状态
 *
 * Renderer::render() 对 View 对象进行操作。这些 View 对象指定重要参数，
 * 例如：
 *  - Scene
 *  - Camera
 *  - Viewport
 *  - 一些渲染参数
 *
 * \note
 * View 实例是重量级对象，内部缓存大量渲染所需的数据。
 * 不建议应用程序使用多个 View 对象。
 *
 * 例如，在游戏中，一个 View 可用于主场景，另一个用于
 * 游戏用户界面。更多 View 实例可用于创建特殊效果（例如
 * View 类似于渲染通道）。
 *
 * @see Renderer, Scene, Camera, RenderTarget
 */
class UTILS_PUBLIC View : public FilamentAPI {
public:
    using QualityLevel = filament::QualityLevel;
    using BlendMode = filament::BlendMode;
    using AntiAliasing = filament::AntiAliasing;
    using Dithering = filament::Dithering;
    using ShadowType = filament::ShadowType;

    using DynamicResolutionOptions = filament::DynamicResolutionOptions;
    using BloomOptions = filament::BloomOptions;
    using FogOptions = filament::FogOptions;
    using DepthOfFieldOptions = filament::DepthOfFieldOptions;
    using VignetteOptions = filament::VignetteOptions;
    using RenderQuality = filament::RenderQuality;
    using AmbientOcclusionOptions = filament::AmbientOcclusionOptions;
    using TemporalAntiAliasingOptions = filament::TemporalAntiAliasingOptions;
    using MultiSampleAntiAliasingOptions = filament::MultiSampleAntiAliasingOptions;
    using VsmShadowOptions = filament::VsmShadowOptions;
    using SoftShadowOptions = filament::SoftShadowOptions;
    using ScreenSpaceReflectionsOptions = filament::ScreenSpaceReflectionsOptions;
    using GuardBandOptions = filament::GuardBandOptions;
    using StereoscopicOptions = filament::StereoscopicOptions;

    /**
     * Sets the View's name. Only useful for debugging.
     * @param name Pointer to the View's name. The string is copied.
     */
    /**
     * 设置 View 的名称。仅用于调试。
     * @param name 指向 View 名称的指针。字符串会被复制。
     */
    void setName(const char* UTILS_NONNULL name) noexcept;

    /**
     * Returns the View's name
     *
     * @return a pointer owned by the View instance to the View's name.
     *
     * @attention Do *not* free the pointer or modify its content.
     */
    /**
     * 返回 View 的名称
     *
     * @return 由 View 实例拥有的指向 View 名称的指针
     *
     * @attention 请*不要*释放指针或修改其内容。
     */
    const char* UTILS_NULLABLE getName() const noexcept;

    /**
     * Set this View instance's Scene.
     *
     * @param scene Associate the specified Scene to this View. A Scene can be associated to
     *              several View instances.\n
     *              \p scene can be nullptr to dissociate the currently set Scene
     *              from this View.\n
     *              The View doesn't take ownership of the Scene pointer (which
     *              acts as a reference).
     *
     * @note
     *  There is no reference-counting.
     *  Make sure to dissociate a Scene from all Views before destroying it.
     */
    /**
     * 设置此 View 实例的 Scene
     *
     * @param scene 将指定的 Scene 关联到此 View。一个 Scene 可以关联到
     *              多个 View 实例。\n
     *              \p scene 可以为 nullptr 以从此 View 解关联当前设置的 Scene。\n
     *              View 不拥有 Scene 指针的所有权（它作为引用）。
     *
     * @note
     *  没有引用计数。
     *  在销毁 Scene 之前，确保从所有 View 中解关联它。
     */
    void setScene(Scene* UTILS_NULLABLE scene);

    /**
     * Returns the Scene currently associated with this View.
     * @return A pointer to the Scene associated to this View. nullptr if no Scene is set.
     */
    /**
     * 返回当前与此 View 关联的 Scene
     * @return 与此 View 关联的 Scene 的指针。如果未设置 Scene，则返回 nullptr。
     */
    Scene* UTILS_NULLABLE getScene() noexcept;

    /**
     * Returns the Scene currently associated with this View.
     * @return A pointer to the Scene associated to this View. nullptr if no Scene is set.
     */
    /**
     * 返回当前与此 View 关联的 Scene（const 版本）
     * @return 与此 View 关联的 Scene 的常量指针。如果未设置 Scene，则返回 nullptr。
     */
    Scene const* UTILS_NULLABLE getScene() const noexcept {
        return const_cast<View*>(this)->getScene();
    }

    /**
     * Specifies an offscreen render target to render into.
     *
     * By default, the view's associated render target is nullptr, which corresponds to the
     * SwapChain associated with the engine.
     *
     * A view with a custom render target cannot rely on Renderer::ClearOptions, which only apply
     * to the SwapChain. Such view can use a Skybox instead.
     *
     * @param renderTarget Render target associated with view, or nullptr for the swap chain.
     */
    /**
     * 指定要渲染到的离屏渲染目标
     *
     * 默认情况下，view 关联的渲染目标为 nullptr，这对应于与引擎
     * 关联的 SwapChain。
     *
     * 具有自定义渲染目标的 view 不能依赖 Renderer::ClearOptions，它仅适用于
     * SwapChain。此类 view 可以改用 Skybox。
     *
     * @param renderTarget 与 view 关联的渲染目标，或 nullptr 表示交换链
     */
    void setRenderTarget(RenderTarget* UTILS_NULLABLE renderTarget) noexcept;

    /**
     * Gets the offscreen render target associated with this view.
     *
     * Returns nullptr if the render target is the swap chain (which is default).
     *
     * @see setRenderTarget
     */
    /**
     * 获取与此 view 关联的离屏渲染目标
     *
     * 如果渲染目标是交换链（默认），则返回 nullptr。
     *
     * @see setRenderTarget
     */
    RenderTarget* UTILS_NULLABLE getRenderTarget() const noexcept;

    /**
     * Sets the rectangular region to render to.
     *
     * The viewport specifies where the content of the View (i.e. the Scene) is rendered in
     * the render target. The Render target is automatically clipped to the Viewport.
     *
     * @param viewport  The Viewport to render the Scene into. The Viewport is a value-type, it is
     *                  therefore copied. The parameter can be discarded after this call returns.
     */
    /**
     * 设置要渲染到的矩形区域
     *
     * 视口指定 View 的内容（即 Scene）在渲染目标中的渲染位置。
     * 渲染目标会自动裁剪到 Viewport。
     *
     * @param viewport  要渲染 Scene 的 Viewport。Viewport 是值类型，因此
     *                  会被复制。此调用返回后可以丢弃该参数。
     */
    void setViewport(Viewport const& viewport) noexcept;

    /**
     * Returns the rectangular region that gets rendered to.
     * @return A constant reference to View's viewport.
     */
    /**
     * 返回要渲染到的矩形区域
     * @return View 的 viewport 的常量引用
     */
    Viewport const& getViewport() const noexcept;

    /**
     * Sets this View's Camera.
     *
     * @param camera    Associate the specified Camera to this View. A Camera can be associated to
     *                  several View instances.\n
     *                  \p camera can be nullptr to dissociate the currently set Camera from this
     *                  View.\n
     *                  The View doesn't take ownership of the Camera pointer (which
     *                  acts as a reference).
     *                  If the camera isn't set, Renderer::render() will result in a no-op.
     *
     * @note
     *  There is no reference-counting.
     *  Make sure to dissociate a Camera from all Views before destroying it.
     */
    /**
     * 设置此 View 的 Camera
     *
     * @param camera    将指定的 Camera 关联到此 View。一个 Camera 可以关联到
     *                  多个 View 实例。\n
     *                  \p camera 可以为 nullptr 以从此 View 解关联当前设置的 Camera。\n
     *                  View 不拥有 Camera 指针的所有权（它作为引用）。
     *                  如果未设置 camera，Renderer::render() 将导致无操作。
     *
     * @note
     *  没有引用计数。
     *  在销毁 Camera 之前，确保从所有 View 中解关联它。
     */
    void setCamera(Camera* UTILS_NULLABLE camera) noexcept;

    /**
     * Returns whether a Camera is set.
     * @return true if a camera is set.
     * @see setCamera()
     */
    /**
     * 返回是否设置了 Camera
     * @return 如果设置了 camera，则返回 true
     * @see setCamera()
     */
    bool hasCamera() const noexcept;

    /**
     * Returns the Camera currently associated with this View.
     * Undefined behavior if hasCamera() is false.
     * @return A reference to the Camera associated to this View if hasCamera() is true.
     * @see hasCamera()
     */
    /**
     * 返回当前与此 View 关联的 Camera
     * 如果 hasCamera() 为 false，则行为未定义。
     * @return 如果 hasCamera() 为 true，则返回与此 View 关联的 Camera 的引用
     * @see hasCamera()
     */
    Camera& getCamera() noexcept;

    /**
     * Returns the Camera currently associated with this View.
     * Undefined behavior if hasCamera() is false.
     * @return A reference to the Camera associated to this View.
     * @see hasCamera()
     */
    /**
     * 返回当前与此 View 关联的 Camera（const 版本）
     * 如果 hasCamera() 为 false，则行为未定义。
     * @return 与此 View 关联的 Camera 的常量引用
     * @see hasCamera()
     */
    Camera const& getCamera() const noexcept {
        return const_cast<View*>(this)->getCamera();
    }


    /**
     * Sets whether a channel must clear the depth buffer before all primitives are rendered.
     * Channel depth clear is off by default for all channels.
     * This is orthogonal to Renderer::setClearOptions().
     * @param channel between 0 and 7
     * @param enabled true to enable clear, false to disable
     */
    /**
     * 设置通道是否必须在渲染所有图元之前清除深度缓冲区
     * 默认情况下，所有通道的通道深度清除都是关闭的。
     * 这与 Renderer::setClearOptions() 正交。
     * @param channel 0 到 7 之间的通道
     * @param enabled true 启用清除，false 禁用
     */
    void setChannelDepthClearEnabled(uint8_t channel, bool enabled) noexcept;

    /**
     * @param channel between 0 and 7
     * @return true if this channel has depth clear enabled.
     */
    /**
     * @param channel 0 到 7 之间的通道
     * @return 如果此通道启用了深度清除，则返回 true
     */
    bool isChannelDepthClearEnabled(uint8_t channel) const noexcept;

    /**
     * Sets the blending mode used to draw the view into the SwapChain.
     *
     * @param blendMode either BlendMode::OPAQUE or BlendMode::TRANSLUCENT
      * @see getBlendMode
     */
    /**
     * 设置用于将 view 绘制到 SwapChain 的混合模式
     *
     * @param blendMode BlendMode::OPAQUE 或 BlendMode::TRANSLUCENT
     * @see getBlendMode
     */
    void setBlendMode(BlendMode blendMode) noexcept;

    /**
     *
     * @return blending mode set by setBlendMode
     * @see setBlendMode
     */
    /**
     * @return 由 setBlendMode 设置的混合模式
     * @see setBlendMode
     */
    BlendMode getBlendMode() const noexcept;

    /**
     * Sets which layers are visible.
     *
     * Renderable objects can have one or several layers associated to them. Layers are
     * represented with an 8-bits bitmask, where each bit corresponds to a layer.
     *
     * This call sets which of those layers are visible. Renderables in invisible layers won't be
     * rendered.
     *
     * @param select    a bitmask specifying which layer to set or clear using \p values.
     * @param values    a bitmask where each bit sets the visibility of the corresponding layer
     *                  (1: visible, 0: invisible), only layers in \p select are affected.
     *
     * @see RenderableManager::setLayerMask().
     *
     * @note By default only layer 0 (bitmask 0x01) is visible.
     * @note This is a convenient way to quickly show or hide sets of Renderable objects.
     */
    /**
     * 设置哪些层可见
     *
     * 可渲染对象可以关联一个或多个层。层用
     * 8 位位掩码表示，其中每个位对应一个层。
     *
     * 此调用设置哪些层可见。不可见层中的可渲染对象不会
     * 被渲染。
     *
     * @param select    指定要使用 \p values 设置或清除哪些层的位掩码
     * @param values    位掩码，其中每个位设置对应层的可见性
     *                  （1：可见，0：不可见），只有 \p select 中的层受影响。
     *
     * @see RenderableManager::setLayerMask()
     *
     * @note 默认情况下只有层 0（位掩码 0x01）可见
     * @note 这是快速显示或隐藏可渲染对象集合的便捷方法
     */
    void setVisibleLayers(uint8_t select, uint8_t values) noexcept;

    /**
     * Helper function to enable or disable a visibility layer.
     * @param layer     layer between 0 and 7 to enable or disable
     * @param enabled   true to enable the layer, false to disable it
     * @see RenderableManager::setVisibleLayers()
     */
    /**
     * 启用或禁用可见层的辅助函数
     * @param layer     要启用或禁用的层（0 到 7 之间）
     * @param enabled   true 启用层，false 禁用它
     * @see RenderableManager::setVisibleLayers()
     */
    inline void setLayerEnabled(size_t layer, bool enabled) noexcept {
        const uint8_t mask = 1u << layer;
        setVisibleLayers(mask, enabled ? mask : 0);
    }

    /**
     * Get the visible layers.
     *
     * @see View::setVisibleLayers()
     */
    /**
     * 获取可见层
     *
     * @see View::setVisibleLayers()
     */
    uint8_t getVisibleLayers() const noexcept;

    /**
     * Enables or disables shadow mapping. Enabled by default.
     *
     * @param enabled true enables shadow mapping, false disables it.
     *
     * @see LightManager::Builder::castShadows(),
     *      RenderableManager::Builder::receiveShadows(),
     *      RenderableManager::Builder::castShadows(),
     */
    /**
     * 启用或禁用阴影贴图。默认启用。
     *
     * @param enabled true 启用阴影贴图，false 禁用它
     *
     * @see LightManager::Builder::castShadows(),
     *      RenderableManager::Builder::receiveShadows(),
     *      RenderableManager::Builder::castShadows()
     */
    void setShadowingEnabled(bool enabled) noexcept;

    /**
     * @return whether shadowing is enabled
     */
    /**
     * @return 是否启用了阴影
     */
    bool isShadowingEnabled() const noexcept;

    /**
     * Enables or disables screen space refraction. Enabled by default.
     *
     * @param enabled true enables screen space refraction, false disables it.
     */
    /**
     * 启用或禁用屏幕空间折射。默认启用。
     *
     * @param enabled true 启用屏幕空间折射，false 禁用它
     */
    void setScreenSpaceRefractionEnabled(bool enabled) noexcept;

    /**
     * @return whether screen space refraction is enabled
     */
    /**
     * @return 是否启用了屏幕空间折射
     */
    bool isScreenSpaceRefractionEnabled() const noexcept;

    /**
     * Sets how many samples are to be used for MSAA in the post-process stage.
     * Default is 1 and disables MSAA.
     *
     * @param count number of samples to use for multi-sampled anti-aliasing.\n
     *              0: treated as 1
     *              1: no anti-aliasing
     *              n: sample count. Effective sample could be different depending on the
     *                 GPU capabilities.
     *
     * @note Anti-aliasing can also be performed in the post-processing stage, generally at lower
     *       cost. See setAntialiasing.
     *
     * @see setAntialiasing
     * @deprecated use setMultiSampleAntiAliasingOptions instead
     */
    /**
     * 设置后处理阶段用于 MSAA 的样本数
     * 默认值为 1，禁用 MSAA。
     *
     * @param count 用于多重采样抗锯齿的样本数。\n
     *              0: 被视为 1
     *              1: 无抗锯齿
     *              n: 样本数。有效样本数可能因 GPU 能力而异
     *
     * @note 抗锯齿也可以在后处理阶段执行，通常成本较低。
     *       参见 setAntialiasing。
     *
     * @see setAntialiasing
     * @deprecated 改用 setMultiSampleAntiAliasingOptions
     */
    UTILS_DEPRECATED
    void setSampleCount(uint8_t count = 1) noexcept;

    /**
     * Returns the sample count set by setSampleCount(). Effective sample count could be different.
     * A value of 0 or 1 means MSAA is disabled.
     *
     * @return value set by setSampleCount().
     * @deprecated use getMultiSampleAntiAliasingOptions instead
     */
    /**
     * 返回由 setSampleCount() 设置的样本数。有效样本数可能不同。
     * 值为 0 或 1 表示 MSAA 已禁用。
     *
     * @return 由 setSampleCount() 设置的值
     * @deprecated 改用 getMultiSampleAntiAliasingOptions
     */
    UTILS_DEPRECATED
    uint8_t getSampleCount() const noexcept;

    /**
     * Enables or disables anti-aliasing in the post-processing stage. Enabled by default.
     * MSAA can be enabled in addition, see setSampleCount().
     *
     * @param type FXAA for enabling, NONE for disabling anti-aliasing.
     *
     * @note For MSAA anti-aliasing, see setSamplerCount().
     *
     * @see setSampleCount
     */
    /**
     * 启用或禁用后处理阶段的抗锯齿。默认启用。
     * 此外可以启用 MSAA，参见 setSampleCount()。
     *
     * @param type FXAA 启用，NONE 禁用抗锯齿
     *
     * @note 对于 MSAA 抗锯齿，参见 setSamplerCount()。
     *
     * @see setSampleCount
     */
    void setAntiAliasing(AntiAliasing type) noexcept;

    /**
     * Queries whether anti-aliasing is enabled during the post-processing stage. To query
     * whether MSAA is enabled, see getSampleCount().
     *
     * @return The post-processing anti-aliasing method.
     */
    /**
     * 查询后处理阶段是否启用了抗锯齿。要查询
     * 是否启用了 MSAA，参见 getSampleCount()。
     *
     * @return 后处理抗锯齿方法
     */
    AntiAliasing getAntiAliasing() const noexcept;

    /**
     * Enables or disable temporal anti-aliasing (TAA). Disabled by default.
     *
     * @param options temporal anti-aliasing options
     */
    /**
     * 启用或禁用时间抗锯齿（TAA）。默认禁用。
     *
     * @param options 时间抗锯齿选项
     */
    void setTemporalAntiAliasingOptions(TemporalAntiAliasingOptions options) noexcept;

    /**
     * Returns temporal anti-aliasing options.
     *
     * @return temporal anti-aliasing options
     */
    /**
     * 返回时间抗锯齿选项
     *
     * @return 时间抗锯齿选项
     */
    TemporalAntiAliasingOptions const& getTemporalAntiAliasingOptions() const noexcept;

    /**
     * Enables or disable screen-space reflections. Disabled by default.
     *
     * @param options screen-space reflections options
     */
    /**
     * 启用或禁用屏幕空间反射。默认禁用。
     *
     * @param options 屏幕空间反射选项
     */
    void setScreenSpaceReflectionsOptions(ScreenSpaceReflectionsOptions options) noexcept;

    /**
     * Returns screen-space reflections options.
     *
     * @return screen-space reflections options
     */
    /**
     * 返回屏幕空间反射选项
     *
     * @return 屏幕空间反射选项
     */
    ScreenSpaceReflectionsOptions const& getScreenSpaceReflectionsOptions() const noexcept;

    /**
     * Enables or disable screen-space guard band. Disabled by default.
     *
     * @param options guard band options
     */
    /**
     * 启用或禁用屏幕空间保护带。默认禁用。
     *
     * @param options 保护带选项
     */
    void setGuardBandOptions(GuardBandOptions options) noexcept;

    /**
     * Returns screen-space guard band options.
     *
     * @return guard band options
     */
    /**
     * 返回屏幕空间保护带选项
     *
     * @return 保护带选项
     */
    GuardBandOptions const& getGuardBandOptions() const noexcept;

    /**
     * Enables or disable multi-sample anti-aliasing (MSAA). Disabled by default.
     *
     * @param options multi-sample anti-aliasing options
     */
    /**
     * 启用或禁用多重采样抗锯齿（MSAA）。默认禁用。
     *
     * @param options 多重采样抗锯齿选项
     */
    void setMultiSampleAntiAliasingOptions(MultiSampleAntiAliasingOptions options) noexcept;

    /**
     * Returns multi-sample anti-aliasing options.
     *
     * @return multi-sample anti-aliasing options
     */
    /**
     * 返回多重采样抗锯齿选项
     *
     * @return 多重采样抗锯齿选项
     */
    MultiSampleAntiAliasingOptions const& getMultiSampleAntiAliasingOptions() const noexcept;

    /**
     * Sets this View's color grading transforms.
     *
     * @param colorGrading Associate the specified ColorGrading to this View. A ColorGrading can be
     *                     associated to several View instances.\n
     *                     \p colorGrading can be nullptr to dissociate the currently set
     *                     ColorGrading from this View. Doing so will revert to the use of the
     *                     default color grading transforms.\n
     *                     The View doesn't take ownership of the ColorGrading pointer (which
     *                     acts as a reference).
     *
     * @note
     *  There is no reference-counting.
     *  Make sure to dissociate a ColorGrading from all Views before destroying it.
     */
    /**
     * 设置此 View 的色彩分级变换
     *
     * @param colorGrading 将指定的 ColorGrading 关联到此 View。一个 ColorGrading 可以
     *                     关联到多个 View 实例。\n
     *                     \p colorGrading 可以为 nullptr 以从此 View 解关联当前设置的
     *                     ColorGrading。这样做将恢复使用默认的色彩分级变换。\n
     *                     View 不拥有 ColorGrading 指针的所有权（它作为引用）。
     *
     * @note
     *  没有引用计数。
     *  在销毁 ColorGrading 之前，确保从所有 View 中解关联它。
     */
    void setColorGrading(ColorGrading* UTILS_NULLABLE colorGrading) noexcept;

    /**
     * Returns the color grading transforms currently associated to this view.
     * @return A pointer to the ColorGrading associated to this View.
     */
    /**
     * 返回当前与此 view 关联的色彩分级变换
     * @return 与此 View 关联的 ColorGrading 的指针
     */
    const ColorGrading* UTILS_NULLABLE getColorGrading() const noexcept;

    /**
     * Sets ambient occlusion options.
     *
     * @param options Options for ambient occlusion.
     */
    /**
     * 设置环境光遮蔽选项
     *
     * @param options 环境光遮蔽的选项
     */
    void setAmbientOcclusionOptions(AmbientOcclusionOptions const& options) noexcept;

    /**
     * Gets the ambient occlusion options.
     *
     * @return ambient occlusion options currently set.
     */
    /**
     * 获取环境光遮蔽选项
     *
     * @return 当前设置的环境光遮蔽选项
     */
    AmbientOcclusionOptions const& getAmbientOcclusionOptions() const noexcept;

    /**
     * Enables or disables bloom in the post-processing stage. Disabled by default.
     *
     * @param options options
     */
    /**
     * 启用或禁用后处理阶段的泛光效果。默认禁用。
     *
     * @param options 选项
     */
    void setBloomOptions(BloomOptions options) noexcept;

    /**
     * Queries the bloom options.
     *
     * @return the current bloom options for this view.
     */
    /**
     * 查询泛光选项
     *
     * @return 此 view 当前的泛光选项
     */
    BloomOptions getBloomOptions() const noexcept;

    /**
     * Enables or disables fog. Disabled by default.
     *
     * @param options options
     */
    /**
     * 启用或禁用雾效。默认禁用。
     *
     * @param options 选项
     */
    void setFogOptions(FogOptions options) noexcept;

    /**
     * Queries the fog options.
     *
     * @return the current fog options for this view.
     */
    /**
     * 查询雾效选项
     *
     * @return 此 view 当前的雾效选项
     */
    FogOptions getFogOptions() const noexcept;

    /**
     * Enables or disables Depth of Field. Disabled by default.
     *
     * @param options options
     */
    /**
     * 启用或禁用景深。默认禁用。
     *
     * @param options 选项
     */
    void setDepthOfFieldOptions(DepthOfFieldOptions options) noexcept;

    /**
     * Queries the depth of field options.
     *
     * @return the current depth of field options for this view.
     */
    /**
     * 查询景深选项
     *
     * @return 此 view 当前的景深选项
     */
    DepthOfFieldOptions getDepthOfFieldOptions() const noexcept;

    /**
     * Enables or disables the vignetted effect in the post-processing stage. Disabled by default.
     *
     * @param options options
     */
    /**
     * 启用或禁用后处理阶段的暗角效果。默认禁用。
     *
     * @param options 选项
     */
    void setVignetteOptions(VignetteOptions options) noexcept;

    /**
     * Queries the vignette options.
     *
     * @return the current vignette options for this view.
     */
    /**
     * 查询暗角选项
     *
     * @return 此 view 当前的暗角选项
     */
    VignetteOptions getVignetteOptions() const noexcept;

    /**
     * Enables or disables dithering in the post-processing stage. Enabled by default.
     *
     * @param dithering dithering type
     */
    /**
     * 启用或禁用后处理阶段的抖动。默认启用。
     *
     * @param dithering 抖动类型
     */
    void setDithering(Dithering dithering) noexcept;

    /**
     * Queries whether dithering is enabled during the post-processing stage.
     *
     * @return the current dithering type for this view.
     */
    /**
     * 查询后处理阶段是否启用了抖动
     *
     * @return 此 view 当前的抖动类型
     */
    Dithering getDithering() const noexcept;

    /**
     * Sets the dynamic resolution options for this view. Dynamic resolution options
     * controls whether dynamic resolution is enabled, and if it is, how it behaves.
     *
     * @param options The dynamic resolution options to use on this view
     */
    /**
     * 设置此 view 的动态分辨率选项。动态分辨率选项
     * 控制是否启用动态分辨率，如果启用，它的行为如何。
     *
     * @param options 要在此 view 上使用的动态分辨率选项
     */
    void setDynamicResolutionOptions(DynamicResolutionOptions const& options) noexcept;

    /**
     * Returns the dynamic resolution options associated with this view.
     * @return value set by setDynamicResolutionOptions().
     */
    /**
     * 返回与此 view 关联的动态分辨率选项
     * @return 由 setDynamicResolutionOptions() 设置的值
     */
    DynamicResolutionOptions getDynamicResolutionOptions() const noexcept;

    /**
     * Returns the last dynamic resolution scale factor used by this view. This value is updated
     * when Renderer::render(View*) is called
     * @return a float2 where x is the horizontal and y the vertical scale factor.
     * @see Renderer::render
     */
    /**
     * 返回此 view 使用的最后一个动态分辨率缩放因子。此值在
     * 调用 Renderer::render(View*) 时更新
     * @return float2，其中 x 是水平缩放因子，y 是垂直缩放因子
     * @see Renderer::render
     */
    math::float2 getLastDynamicResolutionScale() const noexcept;

    /**
     * Sets the rendering quality for this view. Refer to RenderQuality for more
     * information about the different settings available.
     *
     * @param renderQuality The render quality to use on this view
     */
    /**
     * 设置此 view 的渲染质量。有关可用不同设置的更多信息，
     * 请参阅 RenderQuality。
     *
     * @param renderQuality 要在此 view 上使用的渲染质量
     */
    void setRenderQuality(RenderQuality const& renderQuality) noexcept;

    /**
     * Returns the render quality used by this view.
     * @return value set by setRenderQuality().
     */
    /**
     * 返回此 view 使用的渲染质量
     * @return 由 setRenderQuality() 设置的值
     */
    RenderQuality getRenderQuality() const noexcept;

    /**
     * Sets options relative to dynamic lighting for this view.
     *
     * @param zLightNear Distance from the camera where the lights are expected to shine.
     *                   This parameter can affect performance and is useful because depending
     *                   on the scene, lights that shine close to the camera may not be
     *                   visible -- in this case, using a larger value can improve performance.
     *                   e.g. when standing and looking straight, several meters of the ground
     *                   isn't visible and if lights are expected to shine there, there is no
     *                   point using a short zLightNear. This value is clamped between
     *                   the camera near and far plane. (Default 5m).
     *
     * @param zLightFar Distance from the camera after which lights are not expected to be visible.
     *                  Similarly to zLightNear, setting this value properly can improve
     *                  performance.  This value is clamped between the camera near and far plane.
     *                  (Default 100m).
     *
     * Together zLightNear and zLightFar must be chosen so that the visible influence of lights
     * is spread between these two values.
     *
     */
    /**
     * 设置此 view 的动态光照相关选项
     *
     * @param zLightNear 预期光源开始照射的距离（从相机起）。
     *                   此参数可能影响性能，因为根据场景不同，
     *                   靠近相机的光源可能不可见——在这种情况下，使用更大的值可以提高性能。
     *                   例如，当站立并直视时，地面几米不可见，如果预期光源在那里照射，
     *                   使用较小的 zLightNear 没有意义。此值被限制在
     *                   相机近平面和远平面之间。（默认 5 米）。
     *
     * @param zLightFar 预期光源不再可见的距离（从相机起）。
     *                  类似于 zLightNear，正确设置此值可以提高
     *                  性能。此值被限制在相机近平面和远平面之间。
     *                  （默认 100 米）。
     *
     * zLightNear 和 zLightFar 必须一起选择，以便光源的可见影响
     * 分布在这两个值之间。
     */
    void setDynamicLightingOptions(float zLightNear, float zLightFar) noexcept;

    /*
     * Set the shadow mapping technique this View uses.
     *
     * The ShadowType affects all the shadows seen within the View.
     *
     * ShadowType::VSM imposes a restriction on marking renderables as only shadow receivers (but
     * not casters). To ensure correct shadowing with VSM, all shadow participant renderables should
     * be marked as both receivers and casters. Objects that are guaranteed to not cast shadows on
     * themselves or other objects (such as flat ground planes) can be set to not cast shadows,
     * which might improve shadow quality.
     *
     * @warning This API is still experimental and subject to change.
     */
    /**
     * 设置此 View 使用的阴影贴图技术
     *
     * ShadowType 影响 View 内看到的所有阴影。
     *
     * ShadowType::VSM 对仅将可渲染对象标记为阴影接收者（但不
     * 是投射者）有限制。为确保 VSM 的阴影正确，所有参与阴影的可渲染对象应该
     * 被标记为接收者和投射者。保证不会在自身或其他对象上投射阴影的对象
     * （如平坦的地面）可以设置为不投射阴影，
     * 这可能会提高阴影质量。
     *
     * @warning 此 API 仍处于实验阶段，可能会更改。
     */
    void setShadowType(ShadowType shadow) noexcept;

    /**
     * Returns the shadow mapping technique used by this View.
     *
     * @return value set by setShadowType().
     */
    /**
     * 返回此 View 使用的阴影贴图技术
     *
     * @return 由 setShadowType() 设置的值
     */
    ShadowType getShadowType() const noexcept;

    /**
     * Sets VSM shadowing options that apply across the entire View.
     *
     * Additional light-specific VSM options can be set with LightManager::setShadowOptions.
     *
     * Only applicable when shadow type is set to ShadowType::VSM.
     *
     * @param options Options for shadowing.
     *
     * @see setShadowType
     *
     * @warning This API is still experimental and subject to change.
     */
    /**
     * 设置适用于整个 View 的 VSM 阴影选项
     *
     * 可以使用 LightManager::setShadowOptions 设置额外的特定光源 VSM 选项。
     *
     * 仅在阴影类型设置为 ShadowType::VSM 时适用。
     *
     * @param options 阴影选项
     *
     * @see setShadowType
     *
     * @warning 此 API 仍处于实验阶段，可能会更改。
     */
    void setVsmShadowOptions(VsmShadowOptions const& options) noexcept;

    /**
     * Returns the VSM shadowing options associated with this View.
     *
     * @return value set by setVsmShadowOptions().
     */
    /**
     * 返回与此 View 关联的 VSM 阴影选项
     *
     * @return 由 setVsmShadowOptions() 设置的值
     */
    VsmShadowOptions getVsmShadowOptions() const noexcept;

    /**
     * Sets soft shadowing options that apply across the entire View.
     *
     * Additional light-specific soft shadow parameters can be set with LightManager::setShadowOptions.
     *
     * Only applicable when shadow type is set to ShadowType::DPCF or ShadowType::PCSS.
     *
     * @param options Options for shadowing.
     *
     * @see setShadowType
     *
     * @warning This API is still experimental and subject to change.
     */
    /**
     * 设置适用于整个 View 的软阴影选项
     *
     * 可以使用 LightManager::setShadowOptions 设置额外的特定光源软阴影参数。
     *
     * 仅在阴影类型设置为 ShadowType::DPCF 或 ShadowType::PCSS 时适用。
     *
     * @param options 阴影选项
     *
     * @see setShadowType
     *
     * @warning 此 API 仍处于实验阶段，可能会更改。
     */
    void setSoftShadowOptions(SoftShadowOptions const& options) noexcept;

    /**
     * Returns the soft shadowing options associated with this View.
     *
     * @return value set by setSoftShadowOptions().
     */
    /**
     * 返回与此 View 关联的软阴影选项
     *
     * @return 由 setSoftShadowOptions() 设置的值
     */
    SoftShadowOptions getSoftShadowOptions() const noexcept;

    /**
     * Enables or disables post processing. Enabled by default.
     *
     * Post-processing includes:
     *  - Depth-of-field
     *  - Bloom
     *  - Vignetting
     *  - Temporal Anti-aliasing (TAA)
     *  - Color grading & gamma encoding
     *  - Dithering
     *  - FXAA
     *  - Dynamic scaling
     *
     * Disabling post-processing forgoes color correctness as well as some anti-aliasing techniques
     * and should only be used for debugging, UI overlays or when using custom render targets
     * (see RenderTarget).
     *
     * @param enabled true enables post processing, false disables it.
     *
     * @see setBloomOptions, setColorGrading, setAntiAliasing, setDithering, setSampleCount
     */
    /**
     * 启用或禁用后处理。默认启用。
     *
     * 后处理包括：
     *  - 景深
     *  - 泛光
     *  - 暗角
     *  - 时间抗锯齿（TAA）
     *  - 色彩分级和 gamma 编码
     *  - 抖动
     *  - FXAA
     *  - 动态缩放
     *
     * 禁用后处理会放弃色彩正确性以及某些抗锯齿技术，
     * 应仅用于调试、UI 叠加或使用自定义渲染目标时
     * （参见 RenderTarget）。
     *
     * @param enabled true 启用后处理，false 禁用它
     *
     * @see setBloomOptions, setColorGrading, setAntiAliasing, setDithering, setSampleCount
     */
    void setPostProcessingEnabled(bool enabled) noexcept;

    //! Returns true if post-processing is enabled. See setPostProcessingEnabled() for more info.
    /**
     * 如果启用了后处理，返回 true。更多信息请参见 setPostProcessingEnabled()。
     */
    bool isPostProcessingEnabled() const noexcept;

    /**
     * Inverts the winding order of front faces. By default front faces use a counter-clockwise
     * winding order. When the winding order is inverted, front faces are faces with a clockwise
     * winding order.
     *
     * Changing the winding order will directly affect the culling mode in materials
     * (see Material::getCullingMode()).
     *
     * Inverting the winding order of front faces is useful when rendering mirrored reflections
     * (water, mirror surfaces, front camera in AR, etc.).
     *
     * @param inverted True to invert front faces, false otherwise.
     */
    /**
     * 反转正面面的环绕顺序。默认情况下正面面使用逆时针
     * 环绕顺序。当环绕顺序反转时，正面面是顺时针
     * 环绕顺序的面。
     *
     * 更改环绕顺序将直接影响材质中的剔除模式
     * （参见 Material::getCullingMode()）。
     *
     * 反转正面面的环绕顺序在渲染镜像反射时很有用
     * （水面、镜面、AR 中的前置摄像头等）。
     *
     * @param inverted true 反转正面面，否则为 false
     */
    void setFrontFaceWindingInverted(bool inverted) noexcept;

    /**
     * Returns true if the winding order of front faces is inverted.
     * See setFrontFaceWindingInverted() for more information.
     */
    /**
     * 如果正面面的环绕顺序已反转，返回 true。
     * 更多信息请参见 setFrontFaceWindingInverted()。
     */
    bool isFrontFaceWindingInverted() const noexcept;

    /**
     * Enables or disables transparent picking. Disabled by default.
     *
     * When transparent picking is enabled, View::pick() will pick from both
     * transparent and opaque renderables. When disabled, View::pick() will only
     * pick from opaque renderables.
     *
     * @param enabled true enables transparent picking, false disables it.
     *
     * @note Transparent picking will create an extra pass for rendering depth
     *       from both transparent and opaque renderables. 
     */
    /**
     * 启用或禁用透明拾取。默认禁用。
     *
     * 启用透明拾取时，View::pick() 将从透明和
     * 不透明可渲染对象中拾取。禁用时，View::pick() 将仅
     * 从不透明可渲染对象中拾取。
     *
     * @param enabled true 启用透明拾取，false 禁用它
     *
     * @note 透明拾取将创建一个额外的通道来渲染来自
     *       透明和不透明可渲染对象的深度
     */
    void setTransparentPickingEnabled(bool enabled) noexcept;

    /**
     * Returns true if transparent picking is enabled.
     * See setTransparentPickingEnabled() for more information.
     */
    /**
     * 如果启用了透明拾取，返回 true。
     * 更多信息请参见 setTransparentPickingEnabled()。
     */
    bool isTransparentPickingEnabled() const noexcept;

    /**
     * Enables use of the stencil buffer.
     *
     * The stencil buffer is an 8-bit, per-fragment unsigned integer stored alongside the depth
     * buffer. The stencil buffer is cleared at the beginning of a frame and discarded after the
     * color pass.
     *
     * Each fragment's stencil value is set during rasterization by specifying stencil operations on
     * a Material. The stencil buffer can be used as a mask for later rendering by setting a
     * Material's stencil comparison function and reference value. Fragments that don't pass the
     * stencil test are then discarded.
     *
     * If post-processing is disabled, then the SwapChain must have the CONFIG_HAS_STENCIL_BUFFER
     * flag set in order to use the stencil buffer.
     *
     * A renderable's priority (see RenderableManager::setPriority) is useful to control the order
     * in which primitives are drawn.
     *
     * @param enabled True to enable the stencil buffer, false disables it (default)
     */
    /**
     * 启用模板缓冲区的使用
     *
     * 模板缓冲区是一个 8 位、每片元的无符号整数，与深度
     * 缓冲区一起存储。模板缓冲区在帧开始时清除，在
     * 颜色通道后丢弃。
     *
     * 通过在材质上指定模板操作，在光栅化期间设置每个片元的模板值。
     * 模板缓冲区可以通过设置材质的模板比较函数和参考值
     * 用作后续渲染的遮罩。未通过模板测试的片元
     * 将被丢弃。
     *
     * 如果后处理被禁用，则 SwapChain 必须设置 CONFIG_HAS_STENCIL_BUFFER
     * 标志才能使用模板缓冲区。
     *
     * 可渲染对象的优先级（参见 RenderableManager::setPriority）对于控制图元
     * 的绘制顺序很有用。
     *
     * @param enabled true 启用模板缓冲区，false 禁用它（默认）
     */
    void setStencilBufferEnabled(bool enabled) noexcept;

    /**
     * Returns true if the stencil buffer is enabled.
     * See setStencilBufferEnabled() for more information.
     */
    /**
     * 如果启用了模板缓冲区，返回 true。
     * 更多信息请参见 setStencilBufferEnabled()。
     */
    bool isStencilBufferEnabled() const noexcept;

    /**
     * Sets the stereoscopic rendering options for this view.
     *
     * Currently, only one type of stereoscopic rendering is supported: side-by-side.
     * Side-by-side stereo rendering splits the viewport into two halves: a left and right half.
     * Eye 0 will render to the left half, while Eye 1 will render into the right half.
     *
     * Currently, the following features are not supported with stereoscopic rendering:
     * - post-processing
     * - shadowing
     * - punctual lights
     *
     * Stereo rendering depends on device and platform support. To check if stereo rendering is
     * supported, use Engine::isStereoSupported(). If stereo rendering is not supported, then the
     * stereoscopic options have no effect.
     *
     * @param options The stereoscopic options to use on this view
     */
    /**
     * 设置此 view 的立体渲染选项
     *
     * 目前，仅支持一种立体渲染类型：并排。
     * 并排立体渲染将视口分为两半：左半部分和右半部分。
     * 眼睛 0 将渲染到左半部分，而眼睛 1 将渲染到右半部分。
     *
     * 目前，立体渲染不支持以下功能：
     * - 后处理
     * - 阴影
     * - 点光源
     *
     * 立体渲染取决于设备和平台支持。要检查是否支持立体渲染，
     * 请使用 Engine::isStereoSupported()。如果不支持立体渲染，则
     * 立体选项无效。
     *
     * @param options 要在此 view 上使用的立体选项
     */
    void setStereoscopicOptions(StereoscopicOptions const& options) noexcept;

    /**
     * Returns the stereoscopic options associated with this View.
     *
     * @return value set by setStereoscopicOptions().
     */
    /**
     * 返回与此 View 关联的立体选项
     *
     * @return 由 setStereoscopicOptions() 设置的值
     */
    StereoscopicOptions const& getStereoscopicOptions() const noexcept;

    // for debugging...

    //! debugging: allows to entirely disable frustum culling. (culling enabled by default).
    /**
     * 调试：允许完全禁用视锥体剔除。（默认启用剔除）。
     */
    void setFrustumCullingEnabled(bool culling) noexcept;

    //! debugging: returns whether frustum culling is enabled.
    /**
     * 调试：返回是否启用了视锥体剔除。
     */
    bool isFrustumCullingEnabled() const noexcept;

    //! debugging: sets the Camera used for rendering. It may be different from the culling camera.
    /**
     * 调试：设置用于渲染的 Camera。它可能与剔除相机不同。
     */
    void setDebugCamera(Camera* UTILS_NULLABLE camera) noexcept;

    //! debugging: returns a Camera from the point of view of *the* dominant directional light used for shadowing.
    /**
     * 调试：从用于阴影的*主*定向光源的角度返回 Camera。
     */
    utils::FixedCapacityVector<Camera const*> getDirectionalShadowCameras() const noexcept;

    //! debugging: enable or disable froxel visualisation for this view.
    /**
     * 调试：启用或禁用此 view 的 froxel 可视化。
     */
    void setFroxelVizEnabled(bool enabled) noexcept;

    //! debugging: returns information about the froxel configuration
    struct FroxelConfigurationInfo {
        uint16_t width;
        uint16_t height;
        uint16_t depth;
        uint32_t viewportWidth;
        uint32_t viewportHeight;
        math::uint2 froxelDimension;
        float zLightFar;
        float linearizer;
        math::mat4f p;
        math::float4 clipTransform;
    };

    struct FroxelConfigurationInfoWithAge {
        FroxelConfigurationInfo info;
        uint32_t age;
    };

    FroxelConfigurationInfoWithAge getFroxelConfigurationInfo() const noexcept;

    /** Result of a picking query */
    /**
     * 拾取查询的结果
     */
    struct PickingQueryResult {
        /**
         * screen space coordinates in GL convention, this can be used to compute the view or
         * world space position of the picking hit. For e.g.:
         *   clip_space_position  = (fragCoords.xy / viewport.wh, fragCoords.z) * 2.0 - 1.0
         *   view_space_position  = inverse(projection) * clip_space_position
         *   world_space_position = model * view_space_position
         *
         * The viewport, projection and model matrices can be obtained from Camera. Because
         * pick() has some latency, it might be more accurate to obtain these values at the
         * time the View::pick() call is made.
         *
         * Note: if the Engine is running at FEATURE_LEVEL_0, the precision or `depth` and
         *       `fragCoords.z` is only 8-bits.
         */
        /**
         * GL 约定下的屏幕空间坐标，可用于计算拾取命中的视图或
         * 世界空间位置。例如：
         *   clip_space_position  = (fragCoords.xy / viewport.wh, fragCoords.z) * 2.0 - 1.0
         *   view_space_position  = inverse(projection) * clip_space_position
         *   world_space_position = model * view_space_position
         *
         * 视口、投影和模型矩阵可以从 Camera 获取。因为
         * pick() 有一些延迟，在调用 View::pick() 时获取这些值
         * 可能更准确。
         *
         * 注意：如果 Engine 运行在 FEATURE_LEVEL_0，`depth` 和
         *       `fragCoords.z` 的精度仅为 8 位。
         */
        utils::Entity renderable{};     //! RenderableManager Entity at the queried coordinates ! 查询坐标处的 RenderableManager Entity / 可渲染对象实体
        float depth{};                  //! Depth buffer value (1 (near plane) to 0 (infinity))! 深度缓冲区值（1（近平面）到 0（无限远））
        uint32_t reserved1{};           //! 保留字段 1
        uint32_t reserved2{};           //! 保留字段 2
        math::float3 fragCoords;        //! GL 约定下的屏幕空间坐标
    };

    /** User data for PickingQueryResultCallback */
    /**
     * PickingQueryResultCallback 的用户数据
     */
    struct PickingQuery {
        // note: this is enough to store a std::function<> -- just saying...
        /**
         * 注意：这足够存储一个 std::function<>——只是说一下...
         */
        void* UTILS_NULLABLE storage[4]; //! 存储用户数据的数组（足够存储 std::function<>）
    };

    /** callback type used for picking queries. */
    /**
     * 用于拾取查询的回调类型
     */
    using PickingQueryResultCallback =
            void(*)(PickingQueryResult const& result, PickingQuery* UTILS_NONNULL pq);

    /**
     * Helper for creating a picking query from Foo::method, by pointer.
     * e.g.: pick<Foo, &Foo::bar>(x, y, &foo);
     *
     * @tparam T        Class of the method to call (e.g.: Foo)
     * @tparam method   Method to call on T (e.g.: &Foo::bar)
     * @param x         Horizontal coordinate to query in the viewport with origin on the left.
     * @param y         Vertical coordinate to query on the viewport with origin at the bottom.
     * @param instance  A pointer to an instance of T
     * @param handler   Handler to dispatch the callback or nullptr for the default handler.
     */
    /**
     * 辅助函数：通过指针从 Foo::method 创建拾取查询
     * 例如：pick<Foo, &Foo::bar>(x, y, &foo);
     *
     * @tparam T        要调用的方法的类（例如：Foo）
     * @tparam method   要在 T 上调用的方法（例如：&Foo::bar）
     * @param x         视口中要查询的水平坐标（原点在左侧）
     * @param y         视口中要查询的垂直坐标（原点在底部）
     * @param instance  T 实例的指针
     * @param handler   用于分派回调的处理器，或 nullptr 表示默认处理器
     */
    template<typename T, void(T::*method)(PickingQueryResult const&)>
    void pick(uint32_t x, uint32_t y, T* UTILS_NONNULL instance,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr) noexcept {
        PickingQuery& query = pick(x, y, [](PickingQueryResult const& result, PickingQuery* pq) {
            (static_cast<T*>(pq->storage[0])->*method)(result);
        }, handler);
        query.storage[0] = instance;
    }

    /**
     * Helper for creating a picking query from Foo::method, by copy for a small object
     * e.g.: pick<Foo, &Foo::bar>(x, y, foo);
     *
     * @tparam T        Class of the method to call (e.g.: Foo)
     * @tparam method   Method to call on T (e.g.: &Foo::bar)
     * @param x         Horizontal coordinate to query in the viewport with origin on the left.
     * @param y         Vertical coordinate to query on the viewport with origin at the bottom.
     * @param instance  An instance of T
     * @param handler   Handler to dispatch the callback or nullptr for the default handler.
     */
    /**
     * 辅助函数：通过复制小对象从 Foo::method 创建拾取查询
     * 例如：pick<Foo, &Foo::bar>(x, y, foo);
     *
     * @tparam T        要调用的方法的类（例如：Foo）
     * @tparam method   要在 T 上调用的方法（例如：&Foo::bar）
     * @param x         视口中要查询的水平坐标（原点在左侧）
     * @param y         视口中要查询的垂直坐标（原点在底部）
     * @param instance  T 的实例
     * @param handler   用于分派回调的处理器，或 nullptr 表示默认处理器
     */
    template<typename T, void(T::*method)(PickingQueryResult const&)>
    void pick(uint32_t x, uint32_t y, T instance,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr) noexcept {
        static_assert(sizeof(instance) <= sizeof(PickingQuery::storage), "user data too large");
        PickingQuery& query = pick(x, y, [](PickingQueryResult const& result, PickingQuery* pq) {
            T* const that = static_cast<T*>(reinterpret_cast<void*>(pq->storage));
            (that->*method)(result);
            that->~T();
        }, handler);
        new(query.storage) T(std::move(instance));
    }

    /**
     * Helper for creating a picking query from a small functor
     * e.g.: pick(x, y, [](PickingQueryResult const& result){});
     *
     * @param x         Horizontal coordinate to query in the viewport with origin on the left.
     * @param y         Vertical coordinate to query on the viewport with origin at the bottom.
     * @param functor   A functor, typically a lambda function.
     * @param handler   Handler to dispatch the callback or nullptr for the default handler.
     */
    /**
     * 辅助函数：从小函数对象创建拾取查询
     * 例如：pick(x, y, [](PickingQueryResult const& result){});
     *
     * @param x         视口中要查询的水平坐标（原点在左侧）
     * @param y         视口中要查询的垂直坐标（原点在底部）
     * @param functor   函数对象，通常是 lambda 函数
     * @param handler   用于分派回调的处理器，或 nullptr 表示默认处理器
     */
    template<typename T>
    void pick(uint32_t x, uint32_t y, T functor,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr) noexcept {
        static_assert(sizeof(functor) <= sizeof(PickingQuery::storage), "functor too large");
        PickingQuery& query = pick(x, y, handler,
                (PickingQueryResultCallback)[](PickingQueryResult const& result, PickingQuery* pq) {
                    T* const that = static_cast<T*>(reinterpret_cast<void*>(pq->storage));
                    that->operator()(result);
                    that->~T();
                });
        new(query.storage) T(std::move(functor));
    }

    /**
     * Creates a picking query. Multiple queries can be created (e.g.: multi-touch).
     * Picking queries are all executed when Renderer::render() is called on this View.
     * The provided callback is guaranteed to be called at some point in the future.
     *
     * Typically it takes a couple frames to receive the result of a picking query.
     *
     * @param x         Horizontal coordinate to query in the viewport with origin on the left.
     * @param y         Vertical coordinate to query on the viewport with origin at the bottom.
     * @param callback  User callback, called when the picking query result is available.
     * @param handler   Handler to dispatch the callback or nullptr for the default handler.
     * @return          A reference to a PickingQuery structure, which can be used to store up to
     *                  8*sizeof(void*) bytes of user data. This user data is later accessible
     *                  in the PickingQueryResultCallback callback 3rd parameter.
     */
    /**
     * 创建拾取查询。可以创建多个查询（例如：多点触控）。
     * 拾取查询都在此 View 上调用 Renderer::render() 时执行。
     * 保证提供的回调在未来的某个时刻被调用。
     *
     * 通常需要几帧才能接收到拾取查询的结果。
     *
     * @param x         视口中要查询的水平坐标（原点在左侧）
     * @param y         视口中要查询的垂直坐标（原点在底部）
     * @param callback  用户回调，在拾取查询结果可用时调用
     * @param handler   用于分派回调的处理器，或 nullptr 表示默认处理器
     * @return          PickingQuery 结构的引用，可用于存储最多
     *                  8*sizeof(void*) 字节的用户数据。此用户数据稍后可在
     *                  PickingQueryResultCallback 回调的第 3 个参数中访问
     */
    PickingQuery& pick(uint32_t x, uint32_t y,
            backend::CallbackHandler* UTILS_NULLABLE handler,
            PickingQueryResultCallback UTILS_NONNULL callback) noexcept;

    /**
     * Set the value of material global variables. There are up-to four such variable each of
     * type float4. These variables can be read in a user Material with
     * `getMaterialGlobal{0|1|2|3}()`. All variable start with a default value of { 0, 0, 0, 1 }
     *
     * @param index index of the variable to set between 0 and 3.
     * @param value new value for the variable.
     * @see getMaterialGlobal
     */
    /**
     * 设置材质全局变量的值。最多有四个这样的变量，每个都是
     * float4 类型。这些变量可以在用户材质中使用
     * `getMaterialGlobal{0|1|2|3}()` 读取。所有变量的默认值都是 { 0, 0, 0, 1 }
     *
     * @param index 要设置的变量的索引（0 到 3 之间）
     * @param value 变量的新值
     * @see getMaterialGlobal
     */
    void setMaterialGlobal(uint32_t index, math::float4 const& value);

    /**
     * Get the value of the material global variables.
     * All variable start with a default value of { 0, 0, 0, 1 }
     *
     * @param index index of the variable to set between 0 and 3.
     * @return current value of the variable.
     * @see setMaterialGlobal
     */
    /**
     * 获取材质全局变量的值
     * 所有变量的默认值都是 { 0, 0, 0, 1 }
     *
     * @param index 要获取的变量的索引（0 到 3 之间）
     * @return 变量的当前值
     * @see setMaterialGlobal
     */
    math::float4 getMaterialGlobal(uint32_t index) const;

    /**
     * Get an Entity representing the large scale fog object.
     * This entity is always inherited by the View's Scene.
     *
     * It is for example possible to create a TransformManager component with this
     * Entity and apply a transformation globally on the fog.
     *
     * @return an Entity representing the large scale fog object.
     */
    /**
     * 获取表示大规模雾对象的 Entity
     * 此实体始终由 View 的 Scene 继承。
     *
     * 例如，可以使用此 Entity 创建 TransformManager 组件
     * 并在雾上全局应用变换。
     *
     * @return 表示大规模雾对象的 Entity
     */
    utils::Entity getFogEntity() const noexcept;


    /**
     * When certain temporal features are used (e.g.: TAA or Screen-space reflections), the view
     * keeps a history of previous frame renders associated with the Renderer the view was last
     * used with. When switching Renderer, it may be necessary to clear that history by calling
     * this method. Similarly, if the whole content of the screen change, like when a cut-scene
     * starts, clearing the history might be needed to avoid artifacts due to the previous frame
     * being very different.
     */
    /**
     * 当使用某些时间特性（例如：TAA 或屏幕空间反射）时，view
     * 保持与上次使用的 Renderer 关联的先前帧渲染历史。
     * 切换 Renderer 时，可能需要通过调用此方法来清除该历史。
     * 类似地，如果整个屏幕内容发生变化，例如过场动画
     * 开始时，可能需要清除历史以避免由于前一帧
     * 非常不同而导致的伪影。
     */
    void clearFrameHistory(Engine& engine) noexcept;

    /**
     * List of available ambient occlusion techniques
     * @deprecated use AmbientOcclusionOptions::enabled instead
     */
    enum class UTILS_DEPRECATED AmbientOcclusion : uint8_t {
        NONE = 0,       //!< No Ambient Occlusion
        SSAO = 1        //!< Basic, sampling SSAO
    };

    /**
     * Activates or deactivates ambient occlusion.
     * @deprecated use setAmbientOcclusionOptions() instead
     * @see setAmbientOcclusionOptions
     *
     * @param ambientOcclusion Type of ambient occlusion to use.
     */
    UTILS_DEPRECATED
    void setAmbientOcclusion(AmbientOcclusion ambientOcclusion) noexcept;

    /**
     * Queries the type of ambient occlusion active for this View.
     * @deprecated use getAmbientOcclusionOptions() instead
     * @see getAmbientOcclusionOptions
     *
     * @return ambient occlusion type.
     */
    UTILS_DEPRECATED
    AmbientOcclusion getAmbientOcclusion() const noexcept;

protected:
    // prevent heap allocation
    ~View() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_VIEW_H
