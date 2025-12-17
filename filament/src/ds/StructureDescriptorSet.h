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

#include "DescriptorSet.h"

#include "TypedUniformBuffer.h"

#include <private/filament/UibStructs.h>

#include <math/vec2.h>
#include <math/vec4.h>

#include <array>

namespace filament {
namespace backend {
struct Viewport;
} // namespace backend

class FEngine;
class DescriptorSetLayout;
struct CameraInfo;

/**
 * 结构描述符堆
 * 
 * 管理结构相关通道（如 SSAO、SSR 等）的每视图统一缓冲区（UBO）和描述符堆。
 * 用于存储和更新结构通道所需的每视图统一数据。
 */
class StructureDescriptorSet {
public:
    /**
     * 构造函数
     */
    StructureDescriptorSet() noexcept;
    
    /**
     * 析构函数
     */
    ~StructureDescriptorSet() noexcept;

    /**
     * 初始化描述符堆
     * 
     * 创建描述符堆布局和统一缓冲区。
     * 
     * @param engine 引擎引用
     */
    void init(FEngine& engine) noexcept;

    /**
     * 终止描述符堆
     * 
     * 释放所有资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver);

    /**
     * 绑定描述符堆
     * 
     * 如果需要则提交 UBO，并绑定描述符堆。
     * 
     * @param driver 驱动 API 引用
     */
    // this commits the UBO if needed and binds the descriptor set
    void bind(backend::DriverApi& driver) const noexcept;

    /**
     * 所有可能影响用户代码的 UBO 值必须在这里设置
     */
    // All UBO values that can affect user code must be set here

    /**
     * 准备相机统一数据
     * 
     * 设置相机相关的每视图统一数据（视图矩阵、投影矩阵等）。
     * 
     * @param engine 引擎常量引用
     * @param camera 相机信息
     */
    void prepareCamera(FEngine const& engine, const CameraInfo& camera) noexcept;

    /**
     * 准备 LOD 偏置统一数据
     * 
     * 设置细节层次（LOD）偏置和导数缩放。
     * 
     * @param bias LOD 偏置值
     * @param derivativesScale 导数缩放（用于纹理 LOD 计算）
     */
    void prepareLodBias(float bias, math::float2 derivativesScale) noexcept;

    /**
     * 准备视口统一数据
     * 
     * 设置物理视口和逻辑视口。
     * 
     * @param physicalViewport 物理视口（实际渲染分辨率）
     * @param logicalViewport 逻辑视口（应用分辨率）
     */
    void prepareViewport(const backend::Viewport& physicalViewport,
            const backend::Viewport& logicalViewport) noexcept;

    /**
     * 准备时间统一数据
     * 
     * 设置引擎时间和用户时间。
     * 
     * @param engine 引擎常量引用
     * @param userTime 用户时间（float4，可用于动画等）
     */
    void prepareTime(FEngine const& engine, math::float4 const& userTime) noexcept;

    /**
     * 准备材质全局统一数据
     * 
     * 设置材质全局参数（4 个 float4）。
     * 
     * @param materialGlobals 材质全局参数数组（4 个 float4）
     */
    void prepareMaterialGlobals(std::array<math::float4, 4> const& materialGlobals) noexcept;

private:
    DescriptorSetLayout const* mDescriptorSetLayout = nullptr;  // 描述符堆布局指针
    mutable DescriptorSet mDescriptorSet;  // 描述符堆（mutable 因为 bind 是 const）
    TypedUniformBuffer<PerViewUib> mUniforms;  // 每视图统一缓冲区
};

} // namespace filament
