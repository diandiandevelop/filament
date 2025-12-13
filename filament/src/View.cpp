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
#include "filament/View.h"

#include <stdint.h>

namespace filament {

/**
 * 设置场景
 * 
 * 设置视图要渲染的场景。场景包含要渲染的实体（渲染对象、光源等）。
 * 
 * @param scene 场景指针
 *              - 如果为 nullptr，则移除场景
 *              - 如果非空，则设置新的场景
 * 
 * 实现：将调用转发到内部实现类设置场景
 */
void View::setScene(Scene* scene) {
    return downcast(this)->setScene(downcast(scene));
}

/**
 * 获取场景
 * 
 * 返回视图当前关联的场景。
 * 
 * @return 场景指针，如果没有设置则返回 nullptr
 * 
 * 实现：从内部实现类获取场景
 */
Scene* View::getScene() noexcept {
    return downcast(this)->getScene();
}

/**
 * 设置相机
 * 
 * 设置视图使用的相机。相机定义了观察位置和投影参数。
 * 
 * @param camera 相机指针（不能为 nullptr）
 * 
 * 实现：将调用转发到内部实现类设置用户相机
 */
void View::setCamera(Camera* camera) noexcept {
    downcast(this)->setCameraUser(downcast(camera));
}

/**
 * 检查是否有相机
 * 
 * 检查视图是否已设置相机。
 * 
 * @return true 如果已设置相机，false 否则
 * 
 * 实现：从内部实现类查询是否有相机
 */
bool View::hasCamera() const noexcept {
    return downcast(this)->hasCamera();
}

/**
 * 获取相机
 * 
 * 返回视图当前使用的相机。
 * 
 * @return 相机引用
 * 
 * 实现：从内部实现类获取用户相机
 * 
 * 注意：如果未设置相机，此方法可能抛出异常或返回无效引用
 */
Camera& View::getCamera() noexcept {
    return downcast(this)->getCameraUser();
}

void View::setChannelDepthClearEnabled(uint8_t channel, bool enabled) noexcept {
    downcast(this)->setChannelDepthClearEnabled(channel, enabled);
}

bool View::isChannelDepthClearEnabled(uint8_t channel) const noexcept {
    return downcast(this)->isChannelDepthClearEnabled(channel);
}

/**
 * 设置视口
 * 
 * 设置视图的渲染视口。视口定义了渲染区域在屏幕上的位置和大小。
 * 
 * @param viewport 视口结构
 *                 - left: 左边界（像素）
 *                 - bottom: 底边界（像素）
 *                 - width: 宽度（像素）
 *                 - height: 高度（像素）
 * 
 * 实现：将调用转发到内部实现类设置视口
 */
void View::setViewport(Viewport const& viewport) noexcept {
    downcast(this)->setViewport(viewport);
}

/**
 * 获取视口
 * 
 * 返回视图当前的渲染视口。
 * 
 * @return 视口常量引用
 * 
 * 实现：从内部实现类获取视口
 */
Viewport const& View::getViewport() const noexcept {
    return downcast(this)->getViewport();
}

/**
 * 设置视锥体剔除启用状态
 * 
 * 启用或禁用视锥体剔除。视锥体剔除用于跳过视锥体外的对象，提高渲染性能。
 * 
 * @param culling true 启用视锥体剔除，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置剔除状态
 * 
 * 注意：禁用剔除会降低性能，但可能对调试有用
 */
void View::setFrustumCullingEnabled(bool const culling) noexcept {
    downcast(this)->setFrustumCullingEnabled(culling);
}

/**
 * 检查视锥体剔除是否启用
 * 
 * 检查视图是否启用了视锥体剔除。
 * 
 * @return true 如果启用剔除，false 如果禁用
 * 
 * 实现：从内部实现类查询剔除状态
 */
bool View::isFrustumCullingEnabled() const noexcept {
    return downcast(this)->isFrustumCullingEnabled();
}

/**
 * 设置调试相机
 * 
 * 设置用于调试的相机。调试相机用于覆盖视图的默认相机，用于调试目的。
 * 
 * @param camera 调试相机指针
 *               - 如果为 nullptr，则使用默认相机
 *               - 如果非空，则使用指定的调试相机
 * 
 * 实现：将调用转发到内部实现类设置查看相机
 */
void View::setDebugCamera(Camera* camera) noexcept {
    downcast(this)->setViewingCamera(downcast(camera));
}

/**
 * 设置可见层
 * 
 * 设置视图的可见层。层用于控制哪些对象可见。
 * 
 * @param select 层选择掩码（位掩码，指定哪些位有效）
 * @param values 层值掩码（位掩码，指定哪些层可见）
 * 
 * 实现：将调用转发到内部实现类设置可见层
 * 
 * 注意：只有 select 中设置的位对应的层才会被 values 影响
 */
void View::setVisibleLayers(uint8_t const select, uint8_t const values) noexcept {
    downcast(this)->setVisibleLayers(select, values);
}

/**
 * 设置视图名称
 * 
 * 设置视图的名称。名称用于调试和日志记录。
 * 
 * @param name 视图名称（C 字符串，可以为 nullptr）
 * 
 * 实现：将调用转发到内部实现类设置名称
 */
void View::setName(const char* name) noexcept {
    downcast(this)->setName(name);
}

/**
 * 获取视图名称
 * 
 * 返回视图的名称。
 * 
 * @return 视图名称（C 字符串），如果没有设置则返回 nullptr
 * 
 * 实现：从内部实现类获取名称
 */
const char* View::getName() const noexcept {
    return downcast(this)->getName();
}

/**
 * 获取方向光阴影相机
 * 
 * 返回用于方向光阴影的相机列表。
 * 
 * @return 方向光阴影相机向量
 * 
 * 实现：从内部实现类获取方向光阴影相机
 */
utils::FixedCapacityVector<Camera const*> View::getDirectionalShadowCameras() const noexcept {
    return downcast(this)->getDirectionalShadowCameras();
}

/**
 * 设置 Froxel 可视化启用状态
 * 
 * 启用或禁用 Froxel（体积光照网格）的可视化。这用于调试体积光照。
 * 
 * @param enabled true 启用可视化，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置 Froxel 可视化状态
 */
void View::setFroxelVizEnabled(bool const enabled) noexcept {
    downcast(this)->setFroxelVizEnabled(enabled);
}

/**
 * 获取 Froxel 配置信息
 * 
 * 返回 Froxel（体积光照网格）的配置信息和年龄。
 * 
 * @return Froxel 配置信息和年龄结构
 * 
 * 实现：从内部实现类获取 Froxel 配置信息
 */
View::FroxelConfigurationInfoWithAge View::getFroxelConfigurationInfo() const noexcept {
    return downcast(this)->getFroxelConfigurationInfo();
}

/**
 * 设置阴影启用状态
 * 
 * 启用或禁用阴影渲染。
 * 
 * @param enabled true 启用阴影，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置阴影状态
 */
void View::setShadowingEnabled(bool const enabled) noexcept {
    downcast(this)->setShadowingEnabled(enabled);
}

/**
 * 设置渲染目标
 * 
 * 设置视图的渲染目标。如果为 nullptr，则使用默认渲染目标（交换链）。
 * 
 * @param renderTarget 渲染目标指针
 *                     - 如果为 nullptr，使用默认渲染目标
 *                     - 如果非空，使用指定的渲染目标
 * 
 * 实现：将调用转发到内部实现类设置渲染目标
 */
void View::setRenderTarget(RenderTarget* renderTarget) noexcept {
    downcast(this)->setRenderTarget(downcast(renderTarget));
}

/**
 * 获取渲染目标
 * 
 * 返回视图当前使用的渲染目标。
 * 
 * @return 渲染目标指针，如果使用默认渲染目标则返回 nullptr
 * 
 * 实现：从内部实现类获取渲染目标
 */
RenderTarget* View::getRenderTarget() const noexcept {
    return downcast(this)->getRenderTarget();
}

/**
 * 设置采样数
 * 
 * 设置多采样抗锯齿（MSAA）的采样数。
 * 
 * @param count 采样数（1, 2, 4, 8 等）
 *              1 表示禁用 MSAA
 * 
 * 实现：将调用转发到内部实现类设置采样数
 */
void View::setSampleCount(uint8_t const count) noexcept {
    downcast(this)->setSampleCount(count);
}

/**
 * 获取采样数
 * 
 * 返回当前的多采样抗锯齿（MSAA）采样数。
 * 
 * @return 采样数（1, 2, 4, 8 等）
 * 
 * 实现：从内部实现类获取采样数
 */
uint8_t View::getSampleCount() const noexcept {
    return downcast(this)->getSampleCount();
}

/**
 * 设置抗锯齿类型
 * 
 * 设置视图使用的抗锯齿类型。
 * 
 * @param type 抗锯齿类型
 *             - NONE: 无抗锯齿
 *             - FXAA: 快速近似抗锯齿
 *             - MSAA: 多采样抗锯齿
 *             - TAA: 时间抗锯齿
 * 
 * 实现：将调用转发到内部实现类设置抗锯齿类型
 */
void View::setAntiAliasing(AntiAliasing const type) noexcept {
    downcast(this)->setAntiAliasing(type);
}

/**
 * 获取抗锯齿类型
 * 
 * 返回视图当前使用的抗锯齿类型。
 * 
 * @return 抗锯齿类型枚举值
 * 
 * 实现：从内部实现类获取抗锯齿类型
 */
View::AntiAliasing View::getAntiAliasing() const noexcept {
    return downcast(this)->getAntiAliasing();
}

/**
 * 设置时间抗锯齿选项
 * 
 * 设置时间抗锯齿（TAA）的选项。
 * 
 * @param options 时间抗锯齿选项结构
 *                包含：
 *                - filterWidth: 滤波器宽度
 *                - feedback: 反馈系数（0-1）
 *                - enabled: 是否启用
 * 
 * 实现：将调用转发到内部实现类设置 TAA 选项
 */
void View::setTemporalAntiAliasingOptions(TemporalAntiAliasingOptions options) noexcept {
    downcast(this)->setTemporalAntiAliasingOptions(options);
}

/**
 * 获取时间抗锯齿选项
 * 
 * 返回当前的时间抗锯齿（TAA）选项。
 * 
 * @return 时间抗锯齿选项常量引用
 * 
 * 实现：从内部实现类获取 TAA 选项
 */
const View::TemporalAntiAliasingOptions& View::getTemporalAntiAliasingOptions() const noexcept {
    return downcast(this)->getTemporalAntiAliasingOptions();
}

/**
 * 设置多采样抗锯齿选项
 * 
 * 设置多采样抗锯齿（MSAA）的选项。
 * 
 * @param options 多采样抗锯齿选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - sampleCount: 采样数
 * 
 * 实现：将调用转发到内部实现类设置 MSAA 选项
 */
void View::setMultiSampleAntiAliasingOptions(MultiSampleAntiAliasingOptions const options) noexcept {
    downcast(this)->setMultiSampleAntiAliasingOptions(options);
}

/**
 * 获取多采样抗锯齿选项
 * 
 * 返回当前的多采样抗锯齿（MSAA）选项。
 * 
 * @return 多采样抗锯齿选项常量引用
 * 
 * 实现：从内部实现类获取 MSAA 选项
 */
const View::MultiSampleAntiAliasingOptions& View::getMultiSampleAntiAliasingOptions() const noexcept {
    return downcast(this)->getMultiSampleAntiAliasingOptions();
}

/**
 * 设置屏幕空间反射选项
 * 
 * 设置屏幕空间反射（SSR）的选项。
 * 
 * @param options 屏幕空间反射选项结构
 *                包含：
 *                - thickness: 厚度
 *                - bias: 偏移
 *                - maxDistance: 最大距离
 *                - stride: 步长
 * 
 * 实现：将调用转发到内部实现类设置 SSR 选项
 */
void View::setScreenSpaceReflectionsOptions(ScreenSpaceReflectionsOptions options) noexcept {
    downcast(this)->setScreenSpaceReflectionsOptions(options);
}

/**
 * 获取屏幕空间反射选项
 * 
 * 返回当前的屏幕空间反射（SSR）选项。
 * 
 * @return 屏幕空间反射选项常量引用
 * 
 * 实现：从内部实现类获取 SSR 选项
 */
const View::ScreenSpaceReflectionsOptions& View::getScreenSpaceReflectionsOptions() const noexcept {
    return downcast(this)->getScreenSpaceReflectionsOptions();
}

/**
 * 设置保护带选项
 * 
 * 设置保护带（Guard Band）的选项。保护带用于扩展渲染区域，减少边界伪影。
 * 
 * @param options 保护带选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - size: 保护带大小
 * 
 * 实现：将调用转发到内部实现类设置保护带选项
 */
void View::setGuardBandOptions(GuardBandOptions const options) noexcept {
    downcast(this)->setGuardBandOptions(options);
}

/**
 * 获取保护带选项
 * 
 * 返回当前的保护带选项。
 * 
 * @return 保护带选项常量引用
 * 
 * 实现：从内部实现类获取保护带选项
 */
GuardBandOptions const& View::getGuardBandOptions() const noexcept {
    return downcast(this)->getGuardBandOptions();
}

/**
 * 设置颜色分级
 * 
 * 设置视图的颜色分级对象。颜色分级用于后处理颜色调整。
 * 
 * @param colorGrading 颜色分级指针
 *                     - 如果为 nullptr，则移除颜色分级
 *                     - 如果非空，则设置新的颜色分级
 * 
 * 实现：将调用转发到内部实现类设置颜色分级
 */
void View::setColorGrading(ColorGrading* colorGrading) noexcept {
    return downcast(this)->setColorGrading(downcast(colorGrading));
}

/**
 * 获取颜色分级
 * 
 * 返回视图当前使用的颜色分级。
 * 
 * @return 颜色分级常量指针，如果没有设置则返回 nullptr
 * 
 * 实现：从内部实现类获取颜色分级
 */
const ColorGrading* View::getColorGrading() const noexcept {
    return downcast(this)->getColorGrading();
}

/**
 * 设置抖动
 * 
 * 设置视图的抖动类型。抖动用于减少颜色量化伪影。
 * 
 * @param dithering 抖动类型
 *                  - NONE: 无抖动
 *                  - TEMPORAL: 时间抖动
 * 
 * 实现：将调用转发到内部实现类设置抖动类型
 */
void View::setDithering(Dithering const dithering) noexcept {
    downcast(this)->setDithering(dithering);
}

/**
 * 获取抖动类型
 * 
 * 返回视图当前使用的抖动类型。
 * 
 * @return 抖动类型枚举值
 * 
 * 实现：从内部实现类获取抖动类型
 */
View::Dithering View::getDithering() const noexcept {
    return downcast(this)->getDithering();
}

/**
 * 设置动态分辨率选项
 * 
 * 设置动态分辨率的选项。动态分辨率可以根据性能自动调整渲染分辨率。
 * 
 * @param options 动态分辨率选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - minScale: 最小缩放比例
 *                - maxScale: 最大缩放比例
 *                - quality: 质量级别
 *                - sharpness: 锐度
 * 
 * 实现：将调用转发到内部实现类设置动态分辨率选项
 */
void View::setDynamicResolutionOptions(const DynamicResolutionOptions& options) noexcept {
    downcast(this)->setDynamicResolutionOptions(options);
}

/**
 * 获取动态分辨率选项
 * 
 * 返回当前的动态分辨率选项。
 * 
 * @return 动态分辨率选项结构
 * 
 * 实现：从内部实现类获取动态分辨率选项
 */
View::DynamicResolutionOptions View::getDynamicResolutionOptions() const noexcept {
    return downcast(this)->getDynamicResolutionOptions();
}

/**
 * 获取最后动态分辨率缩放
 * 
 * 返回上一帧使用的动态分辨率缩放比例。
 * 
 * @return 缩放比例（x, y）
 * 
 * 实现：从内部实现类获取最后动态分辨率缩放
 */
math::float2 View::getLastDynamicResolutionScale() const noexcept {
    return downcast(this)->getLastDynamicResolutionScale();
}

/**
 * 设置渲染质量
 * 
 * 设置视图的渲染质量选项。
 * 
 * @param renderQuality 渲染质量结构
 *                      包含：
 *                      - hdrColorBuffer: HDR 颜色缓冲区质量
 * 
 * 实现：将调用转发到内部实现类设置渲染质量
 */
void View::setRenderQuality(const RenderQuality& renderQuality) noexcept {
    downcast(this)->setRenderQuality(renderQuality);
}

/**
 * 获取渲染质量
 * 
 * 返回视图当前的渲染质量选项。
 * 
 * @return 渲染质量结构
 * 
 * 实现：从内部实现类获取渲染质量
 */
View::RenderQuality View::getRenderQuality() const noexcept {
    return downcast(this)->getRenderQuality();
}

/**
 * 设置后处理启用状态
 * 
 * 启用或禁用后处理效果（如色调映射、泛光等）。
 * 
 * @param enabled true 启用后处理，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置后处理状态
 */
void View::setPostProcessingEnabled(bool const enabled) noexcept {
    downcast(this)->setPostProcessingEnabled(enabled);
}

/**
 * 检查后处理是否启用
 * 
 * 检查视图是否启用了后处理效果。
 * 
 * @return true 如果启用了后处理，false 如果禁用
 * 
 * 实现：从内部实现类查询是否有后处理通道
 */
bool View::isPostProcessingEnabled() const noexcept {
    return downcast(this)->hasPostProcessPass();
}

/**
 * 设置前向面缠绕反转
 * 
 * 设置是否反转前向面的缠绕顺序。这用于处理镜像或翻转的几何体。
 * 
 * @param inverted true 反转缠绕顺序，false 正常
 * 
 * 实现：将调用转发到内部实现类设置前向面缠绕
 */
void View::setFrontFaceWindingInverted(bool const inverted) noexcept {
    downcast(this)->setFrontFaceWindingInverted(inverted);
}

/**
 * 检查前向面缠绕是否反转
 * 
 * 检查视图是否反转了前向面的缠绕顺序。
 * 
 * @return true 如果反转了缠绕顺序，false 如果正常
 * 
 * 实现：从内部实现类查询前向面缠绕状态
 */
bool View::isFrontFaceWindingInverted() const noexcept {
    return downcast(this)->isFrontFaceWindingInverted();
}

/**
 * 设置透明拾取启用状态
 * 
 * 启用或禁用透明对象的拾取。如果启用，拾取操作会考虑透明对象。
 * 
 * @param enabled true 启用透明拾取，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置透明拾取状态
 */
void View::setTransparentPickingEnabled(bool const enabled) noexcept {
    downcast(this)->setTransparentPickingEnabled(enabled);
}

/**
 * 检查透明拾取是否启用
 * 
 * 检查视图是否启用了透明对象的拾取。
 * 
 * @return true 如果启用了透明拾取，false 如果禁用
 * 
 * 实现：从内部实现类查询透明拾取状态
 */
bool View::isTransparentPickingEnabled() const noexcept {
    return downcast(this)->isTransparentPickingEnabled();
}

/**
 * 设置动态光照选项
 * 
 * 设置动态光照（体积光照）的近远平面距离。
 * 
 * @param zLightNear 光照近平面距离（视图空间 Z 坐标）
 * @param zLightFar 光照远平面距离（视图空间 Z 坐标）
 * 
 * 实现：将调用转发到内部实现类设置动态光照选项
 */
void View::setDynamicLightingOptions(float const zLightNear, float const zLightFar) noexcept {
    downcast(this)->setDynamicLightingOptions(zLightNear, zLightFar);
}

/**
 * 设置阴影类型
 * 
 * 设置视图使用的阴影类型。
 * 
 * @param shadow 阴影类型
 *               - HARD: 硬阴影
 *               - PCF: 百分比接近滤波（软阴影）
 *               - VSM: 方差阴影贴图
 *               - DPCF: 降噪百分比接近滤波
 *               - PCSS: 百分比接近软阴影
 * 
 * 实现：将调用转发到内部实现类设置阴影类型
 */
void View::setShadowType(ShadowType const shadow) noexcept {
    downcast(this)->setShadowType(shadow);
}

/**
 * 获取阴影类型
 * 
 * 返回视图当前使用的阴影类型。
 * 
 * @return 阴影类型枚举值
 * 
 * 实现：从内部实现类获取阴影类型
 */
View::ShadowType View::getShadowType() const noexcept {
    return downcast(this)->getShadowType();
}

/**
 * 设置 VSM 阴影选项
 * 
 * 设置方差阴影贴图（VSM）的选项。
 * 
 * @param options VSM 阴影选项结构
 *                包含：
 *                - anisotropic: 各向异性滤波
 *                - blur: 模糊参数
 *                - lightBleedReduction: 光渗减少
 *                - minVarianceScale: 最小方差缩放
 * 
 * 实现：将调用转发到内部实现类设置 VSM 阴影选项
 */
void View::setVsmShadowOptions(VsmShadowOptions const& options) noexcept {
    downcast(this)->setVsmShadowOptions(options);
}

/**
 * 获取 VSM 阴影选项
 * 
 * 返回当前的方差阴影贴图（VSM）选项。
 * 
 * @return VSM 阴影选项结构
 * 
 * 实现：从内部实现类获取 VSM 阴影选项
 */
View::VsmShadowOptions View::getVsmShadowOptions() const noexcept {
    return downcast(this)->getVsmShadowOptions();
}

/**
 * 设置软阴影选项
 * 
 * 设置软阴影的选项。
 * 
 * @param options 软阴影选项结构
 *                包含：
 *                - penumbraScale: 半影缩放
 *                - penumbraRatioScale: 半影比例缩放
 * 
 * 实现：将调用转发到内部实现类设置软阴影选项
 */
void View::setSoftShadowOptions(SoftShadowOptions const& options) noexcept {
    downcast(this)->setSoftShadowOptions(options);
}

/**
 * 获取软阴影选项
 * 
 * 返回当前的软阴影选项。
 * 
 * @return 软阴影选项结构
 * 
 * 实现：从内部实现类获取软阴影选项
 */
SoftShadowOptions View::getSoftShadowOptions() const noexcept {
    return downcast(this)->getSoftShadowOptions();
}

/**
 * 设置环境光遮蔽
 * 
 * 设置视图使用的环境光遮蔽（AO）类型。
 * 
 * @param ambientOcclusion 环境光遮蔽类型
 *                         - NONE: 无环境光遮蔽
 *                         - SSAO: 屏幕空间环境光遮蔽
 *                         - NONE: 无（已弃用，使用 NONE）
 * 
 * 实现：将调用转发到内部实现类设置环境光遮蔽类型
 */
void View::setAmbientOcclusion(AmbientOcclusion const ambientOcclusion) noexcept {
    downcast(this)->setAmbientOcclusion(ambientOcclusion);
}

/**
 * 获取环境光遮蔽类型
 * 
 * 返回视图当前使用的环境光遮蔽（AO）类型。
 * 
 * @return 环境光遮蔽类型枚举值
 * 
 * 实现：从内部实现类获取环境光遮蔽类型
 */
View::AmbientOcclusion View::getAmbientOcclusion() const noexcept {
    return downcast(this)->getAmbientOcclusion();
}

/**
 * 设置环境光遮蔽选项
 * 
 * 设置环境光遮蔽（AO）的选项。
 * 
 * @param options 环境光遮蔽选项结构
 *                包含：
 *                - radius: 采样半径
 *                - power: 强度
 *                - bias: 偏移
 *                - quality: 质量级别
 *                - lowPassFilter: 低通滤波
 *                - upsampling: 上采样
 *                - enabled: 是否启用
 *                - bentNormals: 弯曲法线
 *                - minHorizonAngle: 最小地平线角度
 *                - ssct: 屏幕空间接触阴影
 * 
 * 实现：将调用转发到内部实现类设置环境光遮蔽选项
 */
void View::setAmbientOcclusionOptions(AmbientOcclusionOptions const& options) noexcept {
    downcast(this)->setAmbientOcclusionOptions(options);
}

/**
 * 获取环境光遮蔽选项
 * 
 * 返回当前的环境光遮蔽（AO）选项。
 * 
 * @return 环境光遮蔽选项常量引用
 * 
 * 实现：从内部实现类获取环境光遮蔽选项
 */
View::AmbientOcclusionOptions const& View::getAmbientOcclusionOptions() const noexcept {
    return downcast(this)->getAmbientOcclusionOptions();
}

/**
 * 设置泛光选项
 * 
 * 设置泛光（Bloom）后处理效果的选项。
 * 
 * @param options 泛光选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - strength: 强度
 *                - levels: 级别数
 *                - blendMode: 混合模式
 *                - threshold: 阈值
 *                - resolution: 分辨率
 *                - anamorphism: 变形
 *                - levels: 级别数
 * 
 * 实现：将调用转发到内部实现类设置泛光选项
 */
void View::setBloomOptions(BloomOptions options) noexcept {
    downcast(this)->setBloomOptions(options);
}

/**
 * 获取泛光选项
 * 
 * 返回当前的泛光（Bloom）选项。
 * 
 * @return 泛光选项结构
 * 
 * 实现：从内部实现类获取泛光选项
 */
View::BloomOptions View::getBloomOptions() const noexcept {
    return downcast(this)->getBloomOptions();
}

/**
 * 设置雾选项
 * 
 * 设置雾（Fog）后处理效果的选项。
 * 
 * @param options 雾选项结构
 *                包含：
 *                - distance: 距离
 *                - maximumOpacity: 最大不透明度
 *                - height: 高度
 *                - heightFalloff: 高度衰减
 *                - color: 颜色
 *                - density: 密度
 *                - inScatteringStart: 内散射开始
 *                - inScatteringSize: 内散射大小
 *                - fogColorFromIbl: 从 IBL 获取雾颜色
 *                - enabled: 是否启用
 * 
 * 实现：将调用转发到内部实现类设置雾选项
 */
void View::setFogOptions(FogOptions options) noexcept {
    downcast(this)->setFogOptions(options);
}

/**
 * 获取雾选项
 * 
 * 返回当前的雾（Fog）选项。
 * 
 * @return 雾选项结构
 * 
 * 实现：从内部实现类获取雾选项
 */
View::FogOptions View::getFogOptions() const noexcept {
    return downcast(this)->getFogOptions();
}

/**
 * 设置景深选项
 * 
 * 设置景深（Depth of Field）后处理效果的选项。
 * 
 * @param options 景深选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - cocScale: 弥散圆缩放
 *                - maxApertureDiameter: 最大光圈直径
 *                - enabled: 是否启用
 * 
 * 实现：将调用转发到内部实现类设置景深选项
 */
void View::setDepthOfFieldOptions(DepthOfFieldOptions options) noexcept {
    downcast(this)->setDepthOfFieldOptions(options);
}

/**
 * 获取景深选项
 * 
 * 返回当前的景深（Depth of Field）选项。
 * 
 * @return 景深选项结构
 * 
 * 实现：从内部实现类获取景深选项
 */
View::DepthOfFieldOptions View::getDepthOfFieldOptions() const noexcept {
    return downcast(this)->getDepthOfFieldOptions();
}

/**
 * 设置暗角选项
 * 
 * 设置暗角（Vignette）后处理效果的选项。
 * 
 * @param options 暗角选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - roundness: 圆度
 *                - feather: 羽化
 *                - color: 颜色
 *                - midPoint: 中点
 * 
 * 实现：将调用转发到内部实现类设置暗角选项
 */
void View::setVignetteOptions(VignetteOptions options) noexcept {
    downcast(this)->setVignetteOptions(options);
}

/**
 * 获取暗角选项
 * 
 * 返回当前的暗角（Vignette）选项。
 * 
 * @return 暗角选项结构
 * 
 * 实现：从内部实现类获取暗角选项
 */
View::VignetteOptions View::getVignetteOptions() const noexcept {
    return downcast(this)->getVignetteOptions();
}

/**
 * 设置混合模式
 * 
 * 设置视图的混合模式。混合模式控制视图如何与背景混合。
 * 
 * @param blendMode 混合模式
 *                  - OPAQUE: 不透明（默认）
 *                  - TRANSLUCENT: 半透明
 * 
 * 实现：将调用转发到内部实现类设置混合模式
 */
void View::setBlendMode(BlendMode const blendMode) noexcept {
    downcast(this)->setBlendMode(blendMode);
}

/**
 * 获取混合模式
 * 
 * 返回视图当前的混合模式。
 * 
 * @return 混合模式枚举值
 * 
 * 实现：从内部实现类获取混合模式
 */
View::BlendMode View::getBlendMode() const noexcept {
    return downcast(this)->getBlendMode();
}

/**
 * 获取可见层
 * 
 * 返回视图的可见层值。
 * 
 * @return 可见层值（位掩码）
 * 
 * 实现：从内部实现类获取可见层
 */
uint8_t View::getVisibleLayers() const noexcept {
  return downcast(this)->getVisibleLayers();
}

/**
 * 检查阴影是否启用
 * 
 * 检查视图是否启用了阴影渲染。
 * 
 * @return true 如果启用了阴影，false 如果禁用
 * 
 * 实现：从内部实现类查询阴影状态
 */
bool View::isShadowingEnabled() const noexcept {
    return downcast(this)->isShadowingEnabled();
}

/**
 * 设置屏幕空间折射启用状态
 * 
 * 启用或禁用屏幕空间折射（SSR - Screen Space Refraction）效果。
 * 
 * @param enabled true 启用屏幕空间折射，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置屏幕空间折射状态
 */
void View::setScreenSpaceRefractionEnabled(bool const enabled) noexcept {
    downcast(this)->setScreenSpaceRefractionEnabled(enabled);
}

/**
 * 检查屏幕空间折射是否启用
 * 
 * 检查视图是否启用了屏幕空间折射（SSR）。
 * 
 * @return true 如果启用了屏幕空间折射，false 如果禁用
 * 
 * 实现：从内部实现类查询屏幕空间折射状态
 */
bool View::isScreenSpaceRefractionEnabled() const noexcept {
    return downcast(this)->isScreenSpaceRefractionEnabled();
}

/**
 * 设置模板缓冲区启用状态
 * 
 * 启用或禁用模板缓冲区。模板缓冲区用于模板测试和遮罩。
 * 
 * @param enabled true 启用模板缓冲区，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置模板缓冲区状态
 */
void View::setStencilBufferEnabled(bool const enabled) noexcept {
    downcast(this)->setStencilBufferEnabled(enabled);
}

/**
 * 检查模板缓冲区是否启用
 * 
 * 检查视图是否启用了模板缓冲区。
 * 
 * @return true 如果启用了模板缓冲区，false 如果禁用
 * 
 * 实现：从内部实现类查询模板缓冲区状态
 */
bool View::isStencilBufferEnabled() const noexcept {
    return downcast(this)->isStencilBufferEnabled();
}

/**
 * 设置立体渲染选项
 * 
 * 设置视图的立体渲染选项。用于 VR/AR 等立体显示。
 * 
 * @param options 立体渲染选项结构
 *                包含：
 *                - enabled: 是否启用
 *                - eyeCount: 眼睛数量
 *                - reserved: 保留字段
 * 
 * 实现：将调用转发到内部实现类设置立体渲染选项
 */
void View::setStereoscopicOptions(const StereoscopicOptions& options) noexcept {
    return downcast(this)->setStereoscopicOptions(options);
}

/**
 * 获取立体渲染选项
 * 
 * 返回视图当前的立体渲染选项。
 * 
 * @return 立体渲染选项常量引用
 * 
 * 实现：从内部实现类获取立体渲染选项
 */
const View::StereoscopicOptions& View::getStereoscopicOptions() const noexcept {
    return downcast(this)->getStereoscopicOptions();
}

/**
 * 拾取
 * 
 * 在指定屏幕坐标处执行拾取操作，返回该位置的实体信息。
 * 
 * @param x 屏幕 X 坐标（像素）
 * @param y 屏幕 Y 坐标（像素）
 * @param handler 回调处理器（用于异步回调）
 * @param callback 拾取结果回调函数
 *                 函数签名：void(PickingQueryResult const& result)
 *                 当拾取完成时调用此函数
 * @return 拾取查询对象引用（可用于查询拾取状态）
 * 
 * 实现：将调用转发到内部实现类执行拾取操作
 * 
 * 注意：拾取操作是异步的，结果通过回调函数返回
 */
View::PickingQuery& View::pick(uint32_t const x, uint32_t const y, backend::CallbackHandler* handler,
        PickingQueryResultCallback const callback) noexcept {
    return downcast(this)->pick(x, y, handler, callback);
}

/**
 * 设置材质全局变量
 * 
 * 设置视图级别的材质全局变量。这些变量对所有材质实例都可用。
 * 
 * @param index 变量索引（0-3，对应全局变量 0-3）
 * @param value 变量值（float4）
 * 
 * 实现：将调用转发到内部实现类设置材质全局变量
 */
void View::setMaterialGlobal(uint32_t const index, math::float4 const& value) {
    downcast(this)->setMaterialGlobal(index, value);
}

/**
 * 获取材质全局变量
 * 
 * 获取视图级别的材质全局变量的值。
 * 
 * @param index 变量索引（0-3，对应全局变量 0-3）
 * @return 变量值（float4）
 * 
 * 实现：从内部实现类获取材质全局变量
 */
math::float4 View::getMaterialGlobal(uint32_t const index) const {
    return downcast(this)->getMaterialGlobal(index);
}

/**
 * 获取雾实体
 * 
 * 返回与视图关联的雾实体。雾实体用于体积雾效果。
 * 
 * @return 雾实体ID，如果没有设置则返回无效实体
 * 
 * 实现：从内部实现类获取雾实体
 */
utils::Entity View::getFogEntity() const noexcept {
    return downcast(this)->getFogEntity();
}

/**
 * 清除帧历史
 * 
 * 清除视图的帧历史记录。这用于重置时间相关的效果（如 TAA）。
 * 
 * @param engine 引擎引用
 * 
 * 实现：将调用转发到内部实现类清除帧历史
 */
void View::clearFrameHistory(Engine& engine) noexcept {
    downcast(this)->clearFrameHistory(downcast(engine));
}

} // namespace filament
