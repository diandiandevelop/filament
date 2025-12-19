/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef TNT_FILAMAT_MATERIALINFO_H
#define TNT_FILAMAT_MATERIALINFO_H

#include <backend/DriverEnums.h>

#include <filament/MaterialEnums.h>

#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>

#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>

namespace filamat {

// 类型别名
using UniformType = filament::backend::UniformType;  // 统一变量类型
using SamplerType = filament::backend::SamplerType;  // 采样器类型
using CullingMode = filament::backend::CullingMode;  // 剔除模式

// 材质信息结构，包含材质的所有配置和状态信息
struct UTILS_PUBLIC MaterialInfo {
    bool isLit;                               // 是否有光照
    bool hasDoubleSidedCapability;            // 是否有双面能力
    bool hasExternalSamplers;                 // 是否有外部采样器
    bool has3dSamplers;                       // 是否有3D采样器
    bool hasShadowMultiplier;                 // 是否有阴影倍增器
    bool hasTransparentShadow;                // 是否有透明阴影
    bool specularAntiAliasing;                // 是否启用镜面反锯齿
    bool clearCoatIorChange;                  // 清漆折射率是否改变
    bool flipUV;                              // 是否翻转UV
    bool linearFog;                           // 是否使用线性雾
    bool shadowFarAttenuation;                // 阴影远距离衰减
    bool multiBounceAO;                       // 多反弹环境光遮蔽
    bool multiBounceAOSet;                    // 多反弹AO是否已设置
    bool specularAOSet;                       // 镜面AO是否已设置
    bool hasCustomSurfaceShading;             // 是否有自定义表面着色
    bool useLegacyMorphing;                   // 是否使用传统变形
    bool instanced;                           // 是否使用实例化
    bool vertexDomainDeviceJittered;          // 顶点域设备抖动
    bool userMaterialHasCustomDepth;          // 用户材质是否有自定义深度
    int stereoscopicEyeCount;                 // 立体眼数量
    filament::SpecularAmbientOcclusion specularAO;  // 镜面环境光遮蔽类型
    filament::RefractionMode refractionMode;  // 折射模式
    filament::RefractionType refractionType;  // 折射类型
    filament::ReflectionMode reflectionMode;  // 反射模式
    filament::AttributeBitset requiredAttributes;  // 必需的顶点属性位集合
    filament::BlendingMode blendingMode;      // 混合模式
    filament::BlendingMode postLightingBlendingMode;  // 光照后混合模式
    filament::Shading shading;                // 着色模型
    filament::BufferInterfaceBlock uib;       // 统一接口块（Uniform Interface Block）
    filament::SamplerInterfaceBlock sib;      // 采样器接口块（Sampler Interface Block）
    filament::SubpassInfo subpass;            // 子通道信息
    filament::ShaderQuality quality;          // 着色器质量
    filament::backend::FeatureLevel featureLevel;  // 功能级别
    filament::backend::StereoscopicType stereoscopicType;  // 立体类型
    filament::math::uint3 groupSize;          // 计算着色器组大小

    // 缓冲区容器类型
    using BufferContainer = utils::FixedCapacityVector<filament::BufferInterfaceBlock const*>;
    BufferContainer buffers{ BufferContainer::with_capacity(filament::backend::MAX_SSBO_COUNT) };  // SSBO缓冲区列表
};

}
#endif // TNT_FILAMAT_MATERIALINFO_H
