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

#include "StructureDescriptorSet.h"

#include "PerViewDescriptorSetUtils.h"

#include "details/Camera.h"
#include "details/Engine.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <utils/compiler.h>
#include <utils/debug.h>

#include <math/vec2.h>
#include <math/vec4.h>

#include <array>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 默认构造函数
 * 
 * 创建一个空的结构描述符堆。
 */
StructureDescriptorSet::StructureDescriptorSet() noexcept = default;

/**
 * 析构函数
 * 
 * 确保描述符堆句柄已被释放。
 */
StructureDescriptorSet::~StructureDescriptorSet() noexcept {
    assert_invariant(!mDescriptorSet.getHandle());  // 断言句柄为空
}

/**
 * 初始化结构描述符堆
 * 
 * 创建统一缓冲区和描述符堆。
 * 
 * @param engine 引擎引用
 */
void StructureDescriptorSet::init(FEngine& engine) noexcept {

    mUniforms.init(engine.getDriverApi());  // 初始化统一缓冲区

    mDescriptorSetLayout = &engine.getPerViewDescriptorSetLayoutDepthVariant();  // 获取深度变体描述符堆布局

    /**
     * 从布局创建描述符堆
     */
    // create the descriptor-set from the layout
    mDescriptorSet = DescriptorSet{
        "StructureDescriptorSet", *mDescriptorSetLayout };  // 创建描述符堆

    /**
     * 初始化描述符堆
     * 
     * 将统一缓冲区绑定到 FRAME_UNIFORMS 绑定点。
     */
    // initialize the descriptor-set
    mDescriptorSet.setBuffer(*mDescriptorSetLayout, +PerViewBindingPoints::FRAME_UNIFORMS,  // 设置缓冲区
            mUniforms.getUboHandle(), 0, sizeof(PerViewUib));  // 统一缓冲区句柄、偏移量和大小
}

/**
 * 终止结构描述符堆
 * 
 * 释放描述符堆和统一缓冲区的硬件资源。
 * 
 * @param driver 驱动 API 引用
 */
void StructureDescriptorSet::terminate(DriverApi& driver) {
    mDescriptorSet.terminate(driver);  // 终止描述符堆
    mUniforms.terminate(driver);  // 终止统一缓冲区
}

/**
 * 绑定结构描述符堆
 * 
 * 如果统一缓冲区有脏数据，先更新并提交描述符堆，然后绑定到 PER_VIEW 绑定点。
 * 
 * @param driver 驱动 API 引用
 */
void StructureDescriptorSet::bind(DriverApi& driver) const noexcept {
    assert_invariant(mDescriptorSetLayout);  // 断言布局有效
    if (mUniforms.isDirty()) {  // 如果统一缓冲区有脏数据
        mUniforms.clean();  // 清除脏标志
        driver.updateBufferObject(mUniforms.getUboHandle(),  // 更新缓冲区对象
                mUniforms.toBufferDescriptor(driver), 0);  // 缓冲区描述符和偏移量
        mDescriptorSet.commit(*mDescriptorSetLayout, driver);  // 提交描述符堆
    }
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_VIEW);  // 绑定到每视图绑定点
}

/**
 * 准备视口
 * 
 * 更新统一缓冲区中的视口数据。
 * 
 * @param physicalViewport 物理视口
 * @param logicalViewport 逻辑视口
 */
void StructureDescriptorSet::prepareViewport(
        backend::Viewport const& physicalViewport,
        backend::Viewport const& logicalViewport) noexcept {
    PerViewDescriptorSetUtils::prepareViewport(mUniforms.edit(), physicalViewport, logicalViewport);  // 准备视口数据
}

/**
 * 准备相机
 * 
 * 更新统一缓冲区中的相机相关数据。
 * 
 * @param engine 引擎常量引用
 * @param camera 相机信息
 */
void StructureDescriptorSet::prepareCamera(FEngine const& engine,
        CameraInfo const& camera) noexcept {
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
void StructureDescriptorSet::prepareLodBias(float bias, float2 const derivativesScale) noexcept {
    PerViewDescriptorSetUtils::prepareLodBias(mUniforms.edit(), bias, derivativesScale);  // 准备 LOD 偏移数据
}

/**
 * 准备时间
 * 
 * 更新统一缓冲区中的时间数据。
 * 
 * @param engine 引擎常量引用
 * @param userTime 用户时间（float4）
 */
void StructureDescriptorSet::prepareTime(FEngine const& engine, float4 const& userTime) noexcept {
    PerViewDescriptorSetUtils::prepareTime(mUniforms.edit(), engine, userTime);  // 准备时间数据
}

/**
 * 准备材质全局变量
 * 
 * 更新统一缓冲区中的材质全局变量。
 * 
 * @param materialGlobals 材质全局变量数组（4 个 float4）
 */
void StructureDescriptorSet::prepareMaterialGlobals(
        std::array<float4, 4> const& materialGlobals) noexcept {
    PerViewDescriptorSetUtils::prepareMaterialGlobals(mUniforms.edit(), materialGlobals);  // 准备材质全局变量数据
}

} // namespace filament
