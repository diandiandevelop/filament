/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "PostProcessDescriptorSet.h"

#include "HwDescriptorSetLayoutFactory.h"
#include "TypedUniformBuffer.h"

#include "details/Engine.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 默认构造函数
 */
PostProcessDescriptorSet::PostProcessDescriptorSet() noexcept = default;

/**
 * 初始化后处理描述符堆
 * 
 * 创建描述符堆布局和描述符堆对象。
 * 
 * @param engine 引擎引用
 */
void PostProcessDescriptorSet::init(FEngine& engine) noexcept {

    /**
     * 创建描述符堆布局
     * 
     * 使用深度变体布局（用于后处理通道）。
     */
    // create the descriptor-set layout
    mDescriptorSetLayout = filament::DescriptorSetLayout{
            engine.getDescriptorSetLayoutFactory(),  // 工厂引用
            engine.getDriverApi(),  // 驱动 API 引用
            descriptor_sets::getDepthVariantLayout() };  // 深度变体布局

    /**
     * 从布局创建描述符堆
     */
    // create the descriptor-set from the layout
    mDescriptorSet = DescriptorSet{ "PostProcessDescriptorSet", mDescriptorSetLayout };
}

/**
 * 终止后处理描述符堆
 * 
 * 释放描述符堆和布局的硬件资源。
 * 
 * @param factory 硬件描述符堆布局工厂
 * @param driver 驱动 API 引用
 */
void PostProcessDescriptorSet::terminate(HwDescriptorSetLayoutFactory& factory, DriverApi& driver) {
    mDescriptorSet.terminate(driver);  // 终止描述符堆
    mDescriptorSetLayout.terminate(factory, driver);  // 终止描述符堆布局
}

/**
 * 设置帧 Uniform 数据
 * 
 * 将每视图 Uniform 缓冲区绑定到描述符堆，并提交更新。
 * 
 * @param driver 驱动 API 引用
 * @param uniforms 每视图 Uniform 缓冲区引用
 */
void PostProcessDescriptorSet::setFrameUniforms(DriverApi& driver,
        TypedUniformBuffer<PerViewUib>& uniforms) noexcept {
    /**
     * 初始化描述符堆
     * 
     * 将 Uniform 缓冲区绑定到 FRAME_UNIFORMS 绑定点。
     */
    // initialize the descriptor-set
    mDescriptorSet.setBuffer(mDescriptorSetLayout,
            +PerViewBindingPoints::FRAME_UNIFORMS,  // 绑定点：帧 Uniform
            uniforms.getUboHandle(),  // Uniform 缓冲区句柄
            0,  // 偏移量
            uniforms.getSize());  // 大小

    /**
     * 提交描述符堆更新
     */
    mDescriptorSet.commit(mDescriptorSetLayout, driver);
}

/**
 * 绑定后处理描述符堆
 * 
 * 将描述符堆绑定到 PER_VIEW 绑定点。
 * 
 * @param driver 驱动 API 引用
 */
void PostProcessDescriptorSet::bind(DriverApi& driver) noexcept {
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_VIEW);  // 绑定到每视图绑定点
}

} // namespace filament

