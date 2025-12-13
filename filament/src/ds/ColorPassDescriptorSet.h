/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_PERVIEWUNIFORMS_H
#define TNT_FILAMENT_PERVIEWUNIFORMS_H

#include <filament/Viewport.h>

#include "DescriptorSet.h"

#include "TypedUniformBuffer.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/EntityInstance.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <array>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class DescriptorSetLayout;
class HwDescriptorSetLayoutFactory;

struct AmbientOcclusionOptions;
struct DynamicResolutionOptions;
struct FogOptions;
struct ScreenSpaceReflectionsOptions;
struct SoftShadowOptions;
struct TemporalAntiAliasingOptions;
struct VsmShadowOptions;

struct CameraInfo;
struct ShadowMappingUniforms;

class FEngine;
class FIndirectLight;
class Froxelizer;
class LightManager;

/**
 * 颜色通道描述符集类
 * 
 * 管理颜色渲染通道的描述符集，包括统一缓冲区和采样器。
 * 支持多种配置组合（光照、SSR、雾等），每种组合使用不同的描述符集布局。
 * 
 * 实现细节：
 * - 支持 8 种不同的描述符集布局（2^3 种组合）
 * - 使用 TypedUniformBuffer 管理统一缓冲区数据
 * - 支持 VSM（方差阴影贴图）和传统阴影贴图
 */
class ColorPassDescriptorSet {

    using LightManagerInstance = utils::EntityInstance<LightManager>;  // 光源管理器实例类型
    using TextureHandle = backend::Handle<backend::HwTexture>;  // 纹理句柄类型

public:

    /**
     * 获取描述符集索引
     * 
     * 根据光照、屏幕空间反射和雾的状态计算描述符集布局索引。
     * 
     * @param lit 是否启用光照
     * @param ssr 是否启用屏幕空间反射
     * @param fog 是否启用雾
     * @return 描述符集布局索引（0-7）
     */
    static uint8_t getIndex(bool lit, bool ssr, bool fog)  noexcept;

    /**
     * 构造函数
     * 
     * 为所有可能的配置组合创建描述符集布局。
     * 
     * @param engine 引擎引用
     * @param vsm 是否使用 VSM（方差阴影贴图）
     * @param uniforms 每视图统一缓冲区引用
     */
    ColorPassDescriptorSet(FEngine& engine, bool vsm,
            TypedUniformBuffer<PerViewUib>& uniforms) noexcept;

    /**
     * 初始化描述符集
     * 
     * 为所有描述符集布局设置光源、记录缓冲区和 Froxel 缓冲区。
     * 
     * @param engine 引擎引用
     * @param lights 光源缓冲区句柄
     * @param recordBuffer 记录缓冲区句柄
     * @param froxelBuffer Froxel 缓冲区句柄
     */
    void init(
            FEngine& engine,
            backend::BufferObjectHandle lights,
            backend::BufferObjectHandle recordBuffer,
            backend::BufferObjectHandle froxelBuffer) noexcept;

    /**
     * 终止描述符集
     * 
     * 释放所有描述符集和布局资源。
     * 
     * @param factory 描述符集布局工厂引用
     * @param driver 驱动 API 引用
     */
    void terminate(HwDescriptorSetLayoutFactory& factory, backend::DriverApi& driver);

    /**
     * 准备相机数据
     * 
     * 更新统一缓冲区中的相机相关数据。
     * 
     * @param engine 引擎引用
     * @param camera 相机信息
     */
    void prepareCamera(FEngine& engine, const CameraInfo& camera) noexcept;
    
    /**
     * 准备 LOD 偏移
     * 
     * 更新统一缓冲区中的 LOD 偏移和导数缩放。
     * 
     * @param bias LOD 偏移
     * @param derivativesScale 导数缩放
     */
    void prepareLodBias(float bias, math::float2 derivativesScale) noexcept;

    /**
     * 准备视口
     * 
     * 更新统一缓冲区中的视口数据。
     * 
     * @param physicalViewport 物理视口（应该与 RenderPassParams::viewport 相同）
     * @param logicalViewport 逻辑视口
     * 
     * 注意：当有保护带时，视口内的渲染偏移可能非零。
     */
    /*
     * @param viewport  viewport (should be same as RenderPassParams::viewport)
     * @param xoffset   horizontal rendering offset *within* the viewport.
     *                  Non-zero when we have guard bands.
     * @param yoffset   vertical rendering offset *within* the viewport.
     *                  Non-zero when we have guard bands.
     */
    void prepareViewport(
            const Viewport& physicalViewport,
            const Viewport& logicalViewport) noexcept;

    /**
     * 准备时间
     * 
     * 更新统一缓冲区中的时间数据。
     * 
     * @param engine 引擎引用
     * @param userTime 用户时间（float4）
     */
    void prepareTime(FEngine& engine, math::float4 const& userTime) noexcept;
    
    /**
     * 准备时间抗锯齿噪声
     * 
     * 生成并更新统一缓冲区中的时间抗锯齿噪声值。
     * 
     * @param engine 引擎引用
     * @param options 时间抗锯齿选项
     */
    void prepareTemporalNoise(FEngine& engine, TemporalAntiAliasingOptions const& options) noexcept;
    
    /**
     * 准备曝光
     * 
     * 更新统一缓冲区中的曝光值。
     * 
     * @param ev100 曝光值（EV100）
     */
    void prepareExposure(float ev100) noexcept;
    
    /**
     * 准备雾
     * 
     * 更新统一缓冲区中的雾相关数据。
     * 
     * @param engine 引擎引用
     * @param cameraInfo 相机信息
     * @param fogTransform 雾变换矩阵
     * @param options 雾选项
     * @param ibl 间接光常量指针
     */
    void prepareFog(FEngine& engine, const CameraInfo& cameraInfo,
            math::mat4 const& fogTransform, FogOptions const& options,
            FIndirectLight const* ibl) noexcept;
    
    /**
     * 准备结构纹理
     * 
     * 设置结构纹理采样器。
     * 
     * @param structure 结构纹理句柄
     */
    void prepareStructure(TextureHandle structure) noexcept;
    
    /**
     * 准备 SSAO
     * 
     * 设置屏幕空间环境光遮蔽纹理采样器。
     * 
     * @param ssao SSAO 纹理句柄
     * @param options 环境光遮蔽选项
     */
    void prepareSSAO(TextureHandle ssao, AmbientOcclusionOptions const& options) noexcept;
    
    /**
     * 准备混合
     * 
     * 更新统一缓冲区中的混合相关数据。
     * 
     * @param needsAlphaChannel 是否需要 Alpha 通道
     */
    void prepareBlending(bool needsAlphaChannel) noexcept;
    
    /**
     * 准备材质全局变量
     * 
     * 更新统一缓冲区中的材质全局变量。
     * 
     * @param materialGlobals 材质全局变量数组（4 个 float4）
     */
    void prepareMaterialGlobals(std::array<math::float4, 4> const& materialGlobals) noexcept;

    /**
     * 准备屏幕空间折射
     * 
     * 设置屏幕空间反射和/或折射（SSR）纹理采样器。
     * 
     * @param ssr SSR 纹理句柄
     */
    // screen-space reflection and/or refraction (SSR)
    void prepareScreenSpaceRefraction(TextureHandle ssr) noexcept;

    /**
     * 准备阴影映射
     * 
     * 设置阴影映射统一缓冲区。
     * 
     * @param shadowUniforms 阴影统一缓冲区句柄
     */
    void prepareShadowMapping(backend::BufferObjectHandle shadowUniforms) noexcept;

    /**
     * 准备方向光
     * 
     * 更新统一缓冲区中的方向光数据。
     * 
     * @param engine 引擎引用
     * @param exposure 曝光值
     * @param sceneSpaceDirection 场景空间方向
     * @param instance 光源管理器实例
     */
    void prepareDirectionalLight(FEngine& engine, float exposure,
            math::float3 const& sceneSpaceDirection, LightManagerInstance instance) noexcept;

    /**
     * 准备环境光
     * 
     * 更新统一缓冲区中的环境光数据。
     * 
     * @param engine 引擎常量引用
     * @param ibl 间接光常量引用
     * @param intensity 强度
     * @param exposure 曝光值
     */
    void prepareAmbientLight(FEngine const& engine,
            FIndirectLight const& ibl, float intensity, float exposure) noexcept;

    /**
     * 准备动态光源
     * 
     * 更新统一缓冲区中的动态光源数据（Froxel 数据）。
     * 
     * @param froxelizer Froxel 化器引用
     * @param enableFroxelViz 是否启用 Froxel 可视化
     */
    void prepareDynamicLights(Froxelizer& froxelizer, bool enableFroxelViz) noexcept;

    /**
     * 准备 VSM 阴影
     * 
     * 设置方差阴影贴图纹理采样器。
     * 
     * @param texture VSM 阴影贴图纹理句柄
     * @param options VSM 阴影选项
     */
    void prepareShadowVSM(TextureHandle texture,
            VsmShadowOptions const& options) noexcept;

    /**
     * 准备 PCF 阴影
     * 
     * 设置百分比接近过滤（PCF）阴影贴图纹理采样器。
     * 
     * @param texture PCF 阴影贴图纹理句柄
     */
    void prepareShadowPCF(TextureHandle texture) noexcept;

    /**
     * 准备 DPCF 阴影
     * 
     * 设置双百分比接近过滤（DPCF）阴影贴图纹理采样器。
     * 
     * @param texture DPCF 阴影贴图纹理句柄
     */
    void prepareShadowDPCF(TextureHandle texture) noexcept;

    /**
     * 准备 PCSS 阴影
     * 
     * 设置百分比接近软阴影（PCSS）阴影贴图纹理采样器。
     * 
     * @param texture PCSS 阴影贴图纹理句柄
     */
    void prepareShadowPCSS(TextureHandle texture) noexcept;

    /**
     * 准备 PCF 调试阴影
     * 
     * 设置用于调试的 PCF 阴影贴图纹理采样器。
     * 
     * @param texture PCF 调试阴影贴图纹理句柄
     */
    void prepareShadowPCFDebug(TextureHandle texture) noexcept;

    /**
     * 提交
     * 
     * 将本地数据更新到 GPU 统一缓冲区。
     * 
     * @param driver 驱动 API 引用
     */
    // update local data into GPU UBO
    void commit(backend::DriverApi& driver) noexcept;

    /**
     * 绑定
     * 
     * 将描述符集绑定到指定绑定点。
     * 
     * @param driver 驱动 API 引用
     * @param index 描述符集索引
     */
    // bind this UBO
    void bind(backend::DriverApi& driver, uint8_t const index) const noexcept {
        mDescriptorSet[index].bind(driver, DescriptorSetBindingPoints::PER_VIEW);  // 绑定到每视图绑定点
    }

    /**
     * 检查是否使用 VSM
     * 
     * @return 如果使用 VSM 返回 true，否则返回 false
     */
    bool isVSM() const noexcept { return mIsVsm; }  // 返回 VSM 标志

private:
    /**
     * 描述符布局数量
     * 
     * 支持 8 种不同的配置组合（2^3 = 8）。
     */
    static constexpr size_t DESCRIPTOR_LAYOUT_COUNT = 8;  // 描述符布局数量

    /**
     * 设置采样器
     * 
     * 为所有描述符集设置采样器。
     * 
     * @param binding 绑定点
     * @param th 纹理句柄
     * @param params 采样器参数
     */
    void setSampler(backend::descriptor_binding_t binding,
            backend::TextureHandle th, backend::SamplerParams params) noexcept;

    /**
     * 设置缓冲区
     * 
     * 为所有描述符集设置缓冲区。
     * 
     * @param binding 绑定点
     * @param boh 缓冲区对象句柄
     * @param offset 偏移
     * @param size 大小
     */
    void setBuffer(backend::descriptor_binding_t binding,
            backend::BufferObjectHandle boh, uint32_t offset, uint32_t size) noexcept;

    TypedUniformBuffer<PerViewUib>& mUniforms;  // 每视图统一缓冲区引用
    std::array<DescriptorSetLayout, DESCRIPTOR_LAYOUT_COUNT> mDescriptorSetLayout;  // 描述符集布局数组
    std::array<DescriptorSet, DESCRIPTOR_LAYOUT_COUNT> mDescriptorSet;  // 描述符集数组
    bool const mIsVsm;  // 是否使用 VSM（常量）
};

} // namespace filament

#endif //TNT_FILAMENT_PERVIEWUNIFORMS_H
