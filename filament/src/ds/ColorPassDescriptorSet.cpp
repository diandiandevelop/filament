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

#include "ColorPassDescriptorSet.h"


#include "Froxelizer.h"
#include "PerViewDescriptorSetUtils.h"
#include "TypedUniformBuffer.h"

#include "components/LightManager.h"

#include "details/Camera.h"
#include "details/Engine.h"
#include "details/IndirectLight.h"
#include "details/Texture.h"

#include <filament/Exposure.h>
#include <filament/Options.h>
#include <filament/MaterialEnums.h>
#include <filament/Viewport.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <math/mat4.h>
#include <math/mat3.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/compiler.h>
#include <utils/debug.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <limits>
#include <random>

#include <stdint.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 获取描述符集索引
 * 
 * 根据光照、屏幕空间反射和雾的状态计算描述符集布局索引。
 * 使用位掩码编码不同的配置组合。
 * 
 * @param lit 是否启用光照
 * @param ssr 是否启用屏幕空间反射
 * @param fog 是否启用雾
 * @return 描述符集布局索引（0-7）
 */
uint8_t ColorPassDescriptorSet::getIndex(
        bool const lit, bool const ssr, bool const fog) noexcept {

    uint8_t index = 0;  // 初始索引

    if (!lit) {  // 如果未启用光照
        /**
         * 这将移除未使用光照时的采样器
         */
        // this will remove samplers unused when unit
        index |= 0x1;  // 设置位 0
    }

    if (ssr) {  // 如果启用屏幕空间反射
        /**
         * 这将添加屏幕空间 SSR 所需的采样器
         */
        // this will add samplers needed for screen-space SSR
        index |= 0x2;  // 设置位 1
    }

    if (fog) {  // 如果启用雾
        /**
         * 这将添加雾所需的采样器
         */
        // this will remove samplers needed for fog
        index |= 0x4;  // 设置位 2
    }

    assert_invariant(index < DESCRIPTOR_LAYOUT_COUNT);  // 确保索引在有效范围内
    return index;  // 返回索引
}

/**
 * 颜色通道描述符集构造函数
 * 
 * 为所有可能的配置组合创建描述符集布局。
 * 
 * @param engine 引擎引用
 * @param vsm 是否使用 VSM（方差阴影贴图）
 * @param uniforms 每视图统一缓冲区引用
 */
ColorPassDescriptorSet::ColorPassDescriptorSet(FEngine& engine, bool const vsm,
        TypedUniformBuffer<PerViewUib>& uniforms) noexcept
    : mUniforms(uniforms),  // 初始化统一缓冲区引用
      mIsVsm(vsm) {  // 初始化 VSM 标志
    /**
     * 为所有配置组合创建描述符集布局
     * 
     * 遍历所有可能的 lit、ssr、fog 组合（2^3 = 8 种组合）。
     */
    for (bool const lit: { false, true }) {  // 遍历光照状态
        for (bool const ssr: { false, true }) {  // 遍历 SSR 状态
            for (bool const fog: { false, true }) {  // 遍历雾状态
                auto const index = getIndex(lit, ssr, fog);  // 获取索引
                mDescriptorSetLayout[index] = {  // 创建描述符集布局
                        engine.getDescriptorSetLayoutFactory(),  // 描述符集布局工厂
                        engine.getDriverApi(),  // 驱动 API
                        descriptor_sets::getPerViewDescriptorSetLayout(
                                MaterialDomain::SURFACE, lit, ssr, fog, vsm)
                };
                mDescriptorSet[index] = DescriptorSet{
                        "ColorPassDescriptorSet", mDescriptorSetLayout[index] };
            }
        }
    }

    /**
     * 设置帧统一缓冲区
     */
    setBuffer(+PerViewBindingPoints::FRAME_UNIFORMS,  // 绑定点
            uniforms.getUboHandle(), 0, uniforms.getSize());  // UBO 句柄、偏移、大小

    /**
     * 设置 IBL DFG LUT 采样器
     * 
     * 如果 DFG LUT 有效则使用它，否则使用零纹理。
     */
    setSampler(+PerViewBindingPoints::IBL_DFG_LUT,  // 绑定点
            engine.getDFG().isValid() ?  // 如果 DFG 有效
                    engine.getDFG().getTexture() : engine.getZeroTexture(),  // 使用 DFG 纹理或零纹理
            SamplerParams{  // 采样器参数
                    .filterMag = SamplerMagFilter::LINEAR  // 放大过滤器为线性
            });
}

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
void ColorPassDescriptorSet::init(
        FEngine& engine,
        BufferObjectHandle lights,  // 光源缓冲区句柄
        BufferObjectHandle recordBuffer,  // 记录缓冲区句柄
        BufferObjectHandle froxelBuffer) noexcept {  // Froxel 缓冲区句柄
    for (size_t i = 0; i < DESCRIPTOR_LAYOUT_COUNT; i++) {  // 遍历所有布局
        auto& descriptorSet = mDescriptorSet[i];  // 获取描述符集
        auto const& layout = mDescriptorSetLayout[i];  // 获取布局
        /**
         * 设置光源缓冲区
         */
        descriptorSet.setBuffer(layout, +PerViewBindingPoints::LIGHTS,  // 绑定点
                lights, 0, CONFIG_MAX_LIGHT_COUNT * sizeof(LightsUib));  // 句柄、偏移、大小
        /**
         * 设置记录缓冲区
         */
        descriptorSet.setBuffer(layout, +PerViewBindingPoints::RECORD_BUFFER,  // 绑定点
                recordBuffer, 0, sizeof(FroxelRecordUib));  // 句柄、偏移、大小
        /**
         * 设置 Froxel 缓冲区
         */
        descriptorSet.setBuffer(layout, +PerViewBindingPoints::FROXEL_BUFFER,  // 绑定点
                froxelBuffer, 0, Froxelizer::getFroxelBufferByteCount(engine.getDriverApi()));  // 句柄、偏移、大小
    }
}

/**
 * 终止描述符集
 * 
 * 释放所有描述符集和布局资源。
 * 
 * @param factory 描述符集布局工厂引用
 * @param driver 驱动 API 引用
 */
void ColorPassDescriptorSet::terminate(HwDescriptorSetLayoutFactory& factory, DriverApi& driver) {
    for (auto&& entry : mDescriptorSet) {  // 遍历所有描述符集
        entry.terminate(driver);  // 终止描述符集
    }
    for (auto&& entry : mDescriptorSetLayout) {  // 遍历所有布局
        entry.terminate(factory, driver);  // 终止布局
    }
}

/**
 * 准备相机数据
 * 
 * 更新统一缓冲区中的相机相关数据。
 * 
 * @param engine 引擎引用
 * @param camera 相机信息
 */
void ColorPassDescriptorSet::prepareCamera(FEngine& engine, const CameraInfo& camera) noexcept {
    PerViewDescriptorSetUtils::prepareCamera(mUniforms.edit(), engine, camera);  // 准备相机数据
}

/**
 * 准备 LOD 偏移
 * 
 * 更新统一缓冲区中的 LOD 偏移和导数缩放。
 * 
 * @param bias LOD 偏移
 * @param derivativesScale 导数缩放
 */
void ColorPassDescriptorSet::prepareLodBias(float const bias, float2 const derivativesScale) noexcept {
    PerViewDescriptorSetUtils::prepareLodBias(mUniforms.edit(), bias, derivativesScale);  // 准备 LOD 偏移
}

/**
 * 准备曝光
 * 
 * 更新统一缓冲区中的曝光值。
 * 
 * @param ev100 曝光值（EV100）
 */
void ColorPassDescriptorSet::prepareExposure(float const ev100) noexcept {
    const float exposure = Exposure::exposure(ev100);  // 计算曝光值
    auto& s = mUniforms.edit();  // 获取统一缓冲区编辑引用
    s.exposure = exposure;  // 设置曝光值
    s.ev100 = ev100;  // 设置 EV100
}

/**
 * 准备视口
 * 
 * 更新统一缓冲区中的视口数据。
 * 
 * @param physicalViewport 物理视口
 * @param logicalViewport 逻辑视口
 */
void ColorPassDescriptorSet::prepareViewport(
        const filament::Viewport& physicalViewport,  // 物理视口
        const filament::Viewport& logicalViewport) noexcept {  // 逻辑视口
    PerViewDescriptorSetUtils::prepareViewport(mUniforms.edit(), physicalViewport, logicalViewport);  // 准备视口
}

/**
 * 准备时间
 * 
 * 更新统一缓冲区中的时间数据。
 * 
 * @param engine 引擎引用
 * @param userTime 用户时间（float4）
 */
void ColorPassDescriptorSet::prepareTime(FEngine& engine, float4 const& userTime) noexcept {
    PerViewDescriptorSetUtils::prepareTime(mUniforms.edit(), engine, userTime);  // 准备时间
}

/**
 * 准备时间抗锯齿噪声
 * 
 * 生成并更新统一缓冲区中的时间抗锯齿噪声值。
 * 
 * @param engine 引擎引用
 * @param options 时间抗锯齿选项
 */
void ColorPassDescriptorSet::prepareTemporalNoise(FEngine& engine,
        TemporalAntiAliasingOptions const& options) noexcept {
    std::uniform_real_distribution<float> uniformDistribution{ 0.0f, 1.0f };  // 均匀分布（0-1）
    auto& s = mUniforms.edit();  // 获取统一缓冲区编辑引用
    const float temporalNoise = uniformDistribution(engine.getRandomEngine());  // 生成随机噪声
    s.temporalNoise = options.enabled ? temporalNoise : 0.0f;  // 如果启用则设置噪声，否则为 0
}

void ColorPassDescriptorSet::prepareFog(FEngine& engine, const CameraInfo& cameraInfo,
        mat4 const& userWorldFromFog, FogOptions const& options, FIndirectLight const* ibl) noexcept {

    auto packHalf2x16 = [](half2 v) -> uint32_t {
        short2 s;
        memcpy(&s[0], &v[0], sizeof(s));
        return s.y << 16 | s.x;
    };

    // Fog should be calculated in the "user's world coordinates" so that it's not
    // affected by the IBL rotation.
    // fogFromWorldMatrix below is only used to transform the view vector in the shader, which is
    // why we store the cofactor matrix.

    mat4f const viewFromWorld       = cameraInfo.view;
    mat4 const worldFromUserWorld   = cameraInfo.worldTransform;
    mat4 const worldFromFog         = worldFromUserWorld * userWorldFromFog;
    mat4 const viewFromFog          = viewFromWorld * worldFromFog;

    mat4 const fogFromView          = inverse(viewFromFog);
    mat3 const fogFromWorld         = inverse(worldFromFog.upperLeft());

    // camera position relative to the fog's origin
    auto const userCameraPosition = fogFromView[3].xyz;

    const float heightFalloff = std::max(0.0f, options.heightFalloff);

    // precalculate the constant part of density integral
    const float density = -float(heightFalloff * (userCameraPosition.y - options.height));

    auto& s = mUniforms.edit();

    // note: this code is written so that near/far/minLod/maxLod could be user settable
    //       currently they're inferred.
    Handle<HwTexture> fogColorTextureHandle;
    if (options.skyColor) {
        fogColorTextureHandle = downcast(options.skyColor)->getHwHandleForSampling();
        half2 const minMaxMip{ 0.0f, float(options.skyColor->getLevels()) - 1.0f };
        s.fogMinMaxMip = packHalf2x16(minMaxMip);
        s.fogOneOverFarMinusNear = 1.0f / (cameraInfo.zf - cameraInfo.zn);
        s.fogNearOverFarMinusNear = cameraInfo.zn / (cameraInfo.zf - cameraInfo.zn);
    }
    if (!fogColorTextureHandle && options.fogColorFromIbl) {
        if (ibl) {
            // When using the IBL, because we don't have mip levels, we don't have a mop to
            // select based on the distance. However, we can cheat a little and use
            // mip_roughnessOne-1 as the horizon base color and mip_roughnessOne as the near
            // camera base color. This will give a distant fog that's a bit too sharp, but it
            // improves the effect overall.
            fogColorTextureHandle = ibl->getReflectionHwHandle();
            float const levelCount = float(ibl->getLevelCount());
            half2 const minMaxMip{ levelCount - 2.0f, levelCount - 1.0f };
            s.fogMinMaxMip = packHalf2x16(minMaxMip);
            s.fogOneOverFarMinusNear = 1.0f / (cameraInfo.zf - cameraInfo.zn);
            s.fogNearOverFarMinusNear = cameraInfo.zn / (cameraInfo.zf - cameraInfo.zn);
        }
    }

    setSampler(+PerViewBindingPoints::FOG,
            fogColorTextureHandle ?
                    fogColorTextureHandle : engine.getDummyCubemap()->getHwHandleForSampling(), SamplerParams{
                    .filterMag = SamplerMagFilter::LINEAR,
                    .filterMin = SamplerMinFilter::LINEAR_MIPMAP_LINEAR
            });

    // Fog calculation details:
    // Optical path: (
    //   f = heightFalloff
    //   Te(y, z) = z * density * (exp(-f * eye_y) - exp(-f * eye_y - f * y)) / (f * y)
    // Transmittance:
    //   t(y , z) = exp(-Te(y, z))

    // In Linear Mode, formally the slope of the linear equation is: dt(y,z)/dz(0, eye_y)
    // (the derivative of the transmittance at distance 0 and camera height). When the height
    // falloff is disabled, the density parameter exactly represents this value.
    constexpr double EPSILON = std::numeric_limits<float>::epsilon();
    double const f = heightFalloff;
    double const eye = userCameraPosition.y - options.height;
    double const dt = options.density * (f <= EPSILON ? 1.0 : (std::exp(-f * eye) - std::exp(-2.0 * f * eye)) / (f * eye));

    s.fogStart             = options.distance;
    s.fogMaxOpacity        = options.maximumOpacity;
    s.fogHeightFalloff     = heightFalloff;
    s.fogCutOffDistance    = options.cutOffDistance;
    s.fogColor             = options.color;
    s.fogDensity           = { options.density, density, options.density * std::exp(density) };
    s.fogInscatteringStart = options.inScatteringStart;
    s.fogInscatteringSize  = options.inScatteringSize;
    s.fogColorFromIbl      = fogColorTextureHandle ? 1.0f : 0.0f;
    s.fogFromWorldMatrix   = mat3f{ cof(fogFromWorld) };
    s.fogLinearParams       = { dt, -dt * options.distance };
}

void ColorPassDescriptorSet::prepareSSAO(Handle<HwTexture> ssao,
        AmbientOcclusionOptions const& options) noexcept {
    // High quality sampling is enabled only if AO itself is enabled and upsampling quality is at
    // least set to high and of course only if upsampling is needed.
    const bool highQualitySampling = options.upsampling >= QualityLevel::HIGH
            && options.resolution < 1.0f;

    // LINEAR filtering is only needed when AO is enabled and low-quality upsampling is used.
    setSampler(+PerViewBindingPoints::SSAO, ssao, SamplerParams{
        .filterMag = options.enabled && !highQualitySampling ?
                SamplerMagFilter::LINEAR : SamplerMagFilter::NEAREST
    });
}

void ColorPassDescriptorSet::prepareBlending(bool const needsAlphaChannel) noexcept {
    mUniforms.edit().needsAlphaChannel = needsAlphaChannel ? 1.0f : 0.0f;
}

void ColorPassDescriptorSet::prepareMaterialGlobals(
        std::array<float4, 4> const& materialGlobals) noexcept {
    PerViewDescriptorSetUtils::prepareMaterialGlobals(mUniforms.edit(), materialGlobals);
}

void ColorPassDescriptorSet::prepareScreenSpaceRefraction(Handle<HwTexture> ssr) noexcept {
    setSampler(+PerViewBindingPoints::SSR, ssr, SamplerParams{
        .filterMag = SamplerMagFilter::LINEAR,
        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_LINEAR
    });
}

void ColorPassDescriptorSet::prepareStructure(Handle<HwTexture> structure) noexcept {
    // sampler must be NEAREST
    setSampler(+PerViewBindingPoints::STRUCTURE, structure, {});
}

void ColorPassDescriptorSet::prepareDirectionalLight(FEngine& engine,
        float exposure,
        float3 const& sceneSpaceDirection,
        LightManagerInstance directionalLight) noexcept {
    FLightManager const& lcm = engine.getLightManager();
    auto& s = mUniforms.edit();

    float const shadowFar = lcm.getShadowFar(directionalLight);
    s.shadowFarAttenuationParams = shadowFar > 0.0f ?
            0.5f * float2{ 10.0f, 10.0f / (shadowFar * shadowFar) } : float2{ 1.0f, 0.0f };

    const float3 l = -sceneSpaceDirection; // guaranteed normalized

    if (directionalLight.isValid()) {
        const float4 colorIntensity = {
                lcm.getColor(directionalLight), lcm.getIntensity(directionalLight) * exposure };

        s.lightDirection = l;
        s.lightColorIntensity = colorIntensity;
        s.lightChannels = lcm.getLightChannels(directionalLight);

        const bool isSun = lcm.isSunLight(directionalLight);
        // The last parameter must be < 0.0f for regular directional lights
        float4 sun{ 0.0f, 0.0f, 0.0f, -1.0f };
        if (UTILS_UNLIKELY(isSun && colorIntensity.w > 0.0f)) {
            // Currently we have only a single directional light, so it's probably likely that it's
            // also the Sun. However, conceptually, most directional lights won't be sun lights.
            float const radius = lcm.getSunAngularRadius(directionalLight);
            float const haloSize = lcm.getSunHaloSize(directionalLight);
            float const haloFalloff = lcm.getSunHaloFalloff(directionalLight);
            sun.x = std::cos(radius);
            sun.y = std::sin(radius);
            sun.z = 1.0f / (std::cos(radius * haloSize) - sun.x);
            sun.w = haloFalloff;
        }
        s.sun = sun;
    } else {
        // Disable the sun if there's no directional light
        s.sun = float4{ 0.0f, 0.0f, 0.0f, -1.0f };
    }
}

void ColorPassDescriptorSet::prepareAmbientLight(FEngine const& engine, FIndirectLight const& ibl,
        float const intensity, float const exposure) noexcept {
    auto& s = mUniforms.edit();

    // Set up uniforms and sampler for the IBL, guaranteed to be non-null at this point.
    float const iblRoughnessOneLevel = float(ibl.getLevelCount() - 1);
    s.iblRoughnessOneLevel = iblRoughnessOneLevel;
    s.iblLuminance = intensity * exposure;
    std::transform(ibl.getSH(), ibl.getSH() + 9, s.iblSH, [](float3 const v) {
        return float4(v, 0.0f);
    });

    // We always sample from the reflection texture, so provide a dummy texture if necessary.
    Handle<HwTexture> reflection = ibl.getReflectionHwHandle();
    if (!reflection) {
        reflection = engine.getDummyCubemap()->getHwHandle();
    }
    setSampler(+PerViewBindingPoints::IBL_SPECULAR,
            reflection, SamplerParams{
                    .filterMag = SamplerMagFilter::LINEAR,
                    .filterMin = SamplerMinFilter::LINEAR_MIPMAP_LINEAR
            });
}

void ColorPassDescriptorSet::prepareDynamicLights(Froxelizer& froxelizer, bool const enableFroxelViz) noexcept {
    auto& s = mUniforms.edit();
    froxelizer.updateUniforms(s);
    float const f = froxelizer.getLightFar();
    // TODO: make the falloff rate a parameter
    s.lightFarAttenuationParams = 0.5f * float2{ 10.0f, 10.0f / (f * f) };
    s.enableFroxelViz = enableFroxelViz;
}

void ColorPassDescriptorSet::prepareShadowMapping(BufferObjectHandle shadowUniforms) noexcept {
    setBuffer(+PerViewBindingPoints::SHADOWS, shadowUniforms, 0, sizeof(ShadowUib));
}

void ColorPassDescriptorSet::prepareShadowVSM(Handle<HwTexture> texture,
        VsmShadowOptions const& options) noexcept {
    SamplerMinFilter filterMin = SamplerMinFilter::LINEAR;
    if (options.anisotropy > 0 || options.mipmapping) {
        filterMin = SamplerMinFilter::LINEAR_MIPMAP_LINEAR;
    }
    setSampler(+PerViewBindingPoints::SHADOW_MAP,
            texture, SamplerParams{
                    .filterMag = SamplerMagFilter::LINEAR,
                    .filterMin = filterMin,
                    .anisotropyLog2 = options.anisotropy,
            });
}

void ColorPassDescriptorSet::prepareShadowPCF(Handle<HwTexture> texture) noexcept {
    setSampler(+PerViewBindingPoints::SHADOW_MAP,
            texture, SamplerParams{
                    .filterMag = SamplerMagFilter::LINEAR,
                    .filterMin = SamplerMinFilter::LINEAR,
                    .compareMode = SamplerCompareMode::COMPARE_TO_TEXTURE,
                    .compareFunc = SamplerCompareFunc::GE
            });
}

void ColorPassDescriptorSet::prepareShadowDPCF(Handle<HwTexture> texture) noexcept {
    setSampler(+PerViewBindingPoints::SHADOW_MAP, texture, {});
}

void ColorPassDescriptorSet::prepareShadowPCSS(Handle<HwTexture> texture) noexcept {
    setSampler(+PerViewBindingPoints::SHADOW_MAP, texture, {});
}

void ColorPassDescriptorSet::prepareShadowPCFDebug(Handle<HwTexture> texture) noexcept {
    setSampler(+PerViewBindingPoints::SHADOW_MAP, texture, SamplerParams{
            .filterMag = SamplerMagFilter::NEAREST,
            .filterMin = SamplerMinFilter::NEAREST
    });
}

void ColorPassDescriptorSet::commit(DriverApi& driver) noexcept {
    for (size_t i = 0; i < DESCRIPTOR_LAYOUT_COUNT; i++) {
        mDescriptorSet[i].commit(mDescriptorSetLayout[i], driver);
    }
}

void ColorPassDescriptorSet::setSampler(descriptor_binding_t const binding,
        TextureHandle th, SamplerParams const params) noexcept {
    for (size_t i = 0; i < DESCRIPTOR_LAYOUT_COUNT; i++) {
        auto samplers = mDescriptorSetLayout[i].getSamplerDescriptors();
        if (samplers[binding]) {
            mDescriptorSet[i].setSampler(mDescriptorSetLayout[i], binding, th, params);
        }
    }
}

void ColorPassDescriptorSet::setBuffer(descriptor_binding_t const binding,
        BufferObjectHandle boh, uint32_t const offset, uint32_t const size) noexcept {
    for (size_t i = 0; i < DESCRIPTOR_LAYOUT_COUNT; i++) {
        auto const& layout = mDescriptorSetLayout[i];
        auto ubos = layout.getUniformBufferDescriptors();
        if (ubos[binding]) {
            mDescriptorSet[i].setBuffer(layout, binding, boh, offset, size);
        }
    }
}

} // namespace filament
