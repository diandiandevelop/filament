/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <private/filament/UibStructs.h>

#include <math/vec2.h>
#include <math/vec4.h>

#include <array>

namespace filament {
namespace backend {
struct Viewport;
} // namespace backend

class FEngine;
struct CameraInfo;

/**
 * 每视图描述符集工具类
 * 
 * 提供用于准备每视图统一数据的静态工具方法。
 * 这些方法可以被不同的描述符集类（如 ColorPassDescriptorSet、
 * ShadowMapDescriptorSet、StructureDescriptorSet）共享使用。
 */
class PerViewDescriptorSetUtils {
public:
    /**
     * 准备相机统一数据
     * 
     * 设置相机相关的每视图统一数据（视图矩阵、投影矩阵、相机位置等）。
     * 
     * @param uniforms 每视图统一数据引用
     * @param engine 引擎常量引用
     * @param camera 相机信息
     */
    static void prepareCamera(PerViewUib& uniforms,
            FEngine const& engine, const CameraInfo& camera) noexcept;

    /**
     * 准备 LOD 偏置统一数据
     * 
     * 设置细节层次（LOD）偏置和导数缩放（用于纹理 LOD 计算）。
     * 
     * @param uniforms 每视图统一数据引用
     * @param bias LOD 偏置值
     * @param derivativesScale 导数缩放（用于纹理 LOD 计算）
     */
    static void prepareLodBias(PerViewUib& uniforms,
            float bias, math::float2 derivativesScale) noexcept;

    /**
     * 准备视口统一数据
     * 
     * 设置物理视口和逻辑视口（用于处理动态分辨率和 UI 缩放）。
     * 
     * @param uniforms 每视图统一数据引用
     * @param physicalViewport 物理视口（实际渲染分辨率）
     * @param logicalViewport 逻辑视口（应用分辨率）
     */
    static void prepareViewport(PerViewUib& uniforms,
            backend::Viewport const& physicalViewport,
            backend::Viewport const& logicalViewport) noexcept;

    /**
     * 准备时间统一数据
     * 
     * 设置引擎时间和用户时间（用于动画、时间累积等）。
     * 
     * @param uniforms 每视图统一数据引用
     * @param engine 引擎常量引用
     * @param userTime 用户时间（float4，可用于动画等）
     */
    static void prepareTime(PerViewUib& uniforms,
            FEngine const& engine, math::float4 const& userTime) noexcept;

    /**
     * 准备材质全局统一数据
     * 
     * 设置材质全局参数（4 个 float4，可用于材质系统配置）。
     * 
     * @param uniforms 每视图统一数据引用
     * @param materialGlobals 材质全局参数数组（4 个 float4）
     */
    static void prepareMaterialGlobals(PerViewUib& uniforms,
            std::array<math::float4, 4> const& materialGlobals) noexcept;
};

} //namespace filament
