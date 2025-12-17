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

#include "SsrPassDescriptorSet.h"

#include "TypedUniformBuffer.h"

#include "details/Engine.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/debug.h>

#include <math/mat4.h>

#include <memory>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 默认构造函数
 * 
 * 创建一个空的 SSR 通道描述符堆。
 */
SsrPassDescriptorSet::SsrPassDescriptorSet() noexcept = default;

/**
 * 初始化 SSR 通道描述符堆
 * 
 * 创建描述符堆和虚拟阴影统一缓冲区。
 * 
 * @param engine 引擎引用
 */
void SsrPassDescriptorSet::init(FEngine& engine) noexcept {
    /**
     * 从布局创建描述符堆
     */
    // create the descriptor-set from the layout
    mDescriptorSet = DescriptorSet{
            "SsrPassDescriptorSet", engine.getPerViewDescriptorSetLayoutSsrVariant() };  // 创建 SSR 变体描述符堆

    /**
     * 创建虚拟阴影统一缓冲区（参见下面的 setFrameUniforms() 注释）
     */
    // create a dummy Shadow UBO (see comment in setFrameUniforms() below)
    mShadowUbh = engine.getDriverApi().createBufferObject(sizeof(ShadowUib),  // 创建缓冲区对象
            BufferObjectBinding::UNIFORM, BufferUsage::STATIC);  // 统一缓冲区绑定，静态使用
}

/**
 * 终止 SSR 通道描述符堆
 * 
 * 释放描述符堆和阴影统一缓冲区的硬件资源。
 * 
 * @param driver 驱动 API 引用
 */
void SsrPassDescriptorSet::terminate(DriverApi& driver) {
    mDescriptorSet.terminate(driver);  // 终止描述符堆
    driver.destroyBufferObject(mShadowUbh);  // 销毁阴影统一缓冲区
}

/**
 * 设置帧 Uniform 数据
 * 
 * 将每视图统一缓冲区和虚拟阴影统一缓冲区绑定到描述符堆。
 * 
 * @param engine 引擎常量引用
 * @param uniforms 每视图统一缓冲区引用
 */
void SsrPassDescriptorSet::setFrameUniforms(FEngine const& engine,
        TypedUniformBuffer<PerViewUib>& uniforms) noexcept {
    /**
     * 初始化描述符堆
     * 
     * 将每视图统一缓冲区绑定到 FRAME_UNIFORMS 绑定点。
     */
    // initialize the descriptor-set
    mDescriptorSet.setBuffer(engine.getPerViewDescriptorSetLayoutSsrVariant(),  // 设置缓冲区
            +PerViewBindingPoints::FRAME_UNIFORMS,  // 绑定点：帧 Uniform
            uniforms.getUboHandle(), 0, uniforms.getSize());  // 统一缓冲区句柄、偏移量和大小

    /**
     * 这实际上不用于 SSR 变体，但描述符堆布局需要
     * 有这个统一缓冲区，因为使用的片段着色器是"通用"的。
     * Metal 和 GL 没有这个也可以，但 Vulkan 的验证层会抱怨。
     */
    // This is actually not used for the SSR variants, but the descriptor-set layout needs
    // to have this UBO because the fragment shader used is the "generic" one. Both Metal
    // and GL would be okay without this, but Vulkan's validation layer would complain.
    mDescriptorSet.setBuffer(engine.getPerViewDescriptorSetLayoutSsrVariant(),  // 设置缓冲区
            +PerViewBindingPoints::SHADOWS, mShadowUbh, 0, sizeof(ShadowUib));  // 绑定点：阴影，虚拟统一缓冲区
}

/**
 * 准备历史 SSR 纹理
 * 
 * 将历史 SSR 纹理绑定到描述符堆，使用线性过滤。
 * 
 * @param engine 引擎常量引用
 * @param ssr SSR 纹理句柄
 */
void SsrPassDescriptorSet::prepareHistorySSR(FEngine const& engine, Handle<HwTexture> ssr) noexcept {
    mDescriptorSet.setSampler(engine.getPerViewDescriptorSetLayoutSsrVariant(),  // 设置采样器
            +PerViewBindingPoints::SSR_HISTORY, ssr, SamplerParams{  // 绑定点：SSR 历史
                .filterMag = SamplerMagFilter::LINEAR,  // 放大过滤：线性
                .filterMin = SamplerMinFilter::LINEAR  // 缩小过滤：线性
            });
}

/**
 * 准备结构纹理
 * 
 * 将结构纹理绑定到描述符堆，采样器必须是 NEAREST（最近邻）。
 * 
 * @param engine 引擎常量引用
 * @param structure 结构纹理句柄
 */
void SsrPassDescriptorSet::prepareStructure(FEngine const& engine,
        Handle<HwTexture> structure) noexcept {
    /**
     * 采样器必须是 NEAREST
     */
    // sampler must be NEAREST
    mDescriptorSet.setSampler(engine.getPerViewDescriptorSetLayoutSsrVariant(),  // 设置采样器
            +PerViewBindingPoints::STRUCTURE, structure, SamplerParams{});  // 绑定点：结构，默认参数（NEAREST）
}

/**
 * 提交 SSR 通道描述符堆
 * 
 * 将描述符堆的更改提交到驱动。
 * 
 * @param engine 引擎引用
 */
void SsrPassDescriptorSet::commit(FEngine& engine) noexcept {
    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    mDescriptorSet.commit(engine.getPerViewDescriptorSetLayoutSsrVariant(), driver);  // 提交描述符堆
}

/**
 * 绑定 SSR 通道描述符堆
 * 
 * 将描述符堆绑定到 PER_VIEW 绑定点。
 * 
 * @param driver 驱动 API 引用
 */
void SsrPassDescriptorSet::bind(DriverApi& driver) noexcept {
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_VIEW);  // 绑定到每视图绑定点
}

} // namespace filament

