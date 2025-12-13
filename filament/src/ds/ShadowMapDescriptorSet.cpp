/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "ShadowMapDescriptorSet.h"

#include "PerViewDescriptorSetUtils.h"

#include "details/Camera.h"
#include "details/Engine.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>

#include <utils/debug.h>

#include <math/vec4.h>

#include <array>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 阴影贴图描述符集构造函数
 * 
 * 创建阴影贴图描述符集并初始化统一缓冲区。
 * 
 * @param engine 引擎引用
 */
ShadowMapDescriptorSet::ShadowMapDescriptorSet(FEngine& engine) noexcept {
    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API

    /**
     * 创建统一缓冲区对象
     */
    mUniformBufferHandle = driver.createBufferObject(sizeof(PerViewUib),  // 大小
            BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);  // 绑定类型、使用方式

    /**
     * 从布局创建描述符集
     */
    // create the descriptor-set from the layout
    mDescriptorSet = DescriptorSet{
            "ShadowMapDescriptorSet", engine.getPerViewDescriptorSetLayoutDepthVariant() };  // 名称、布局

    /**
     * 初始化描述符集
     */
    // initialize the descriptor-set
    mDescriptorSet.setBuffer(engine.getPerViewDescriptorSetLayoutDepthVariant(),  // 布局
            +PerViewBindingPoints::FRAME_UNIFORMS, mUniformBufferHandle, 0, sizeof(PerViewUib));  // 绑定点、句柄、偏移、大小
}

/**
 * 终止阴影贴图描述符集
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param driver 驱动 API 引用
 */
void ShadowMapDescriptorSet::terminate(DriverApi& driver) {
    mDescriptorSet.terminate(driver);  // 终止描述符集
    driver.destroyBufferObject(mUniformBufferHandle);  // 销毁统一缓冲区对象
}

/**
 * 编辑统一缓冲区数据
 * 
 * 获取事务中的统一缓冲区数据引用。
 * 
 * @param transaction 事务常量引用
 * @return 统一缓冲区数据引用
 */
PerViewUib& ShadowMapDescriptorSet::edit(Transaction const& transaction) noexcept {
    assert_invariant(transaction.uniforms);  // 断言统一缓冲区数据有效
    return *transaction.uniforms;  // 返回统一缓冲区数据引用
}

/**
 * 准备相机数据
 * 
 * 更新统一缓冲区中的相机相关数据。
 * 
 * @param transaction 事务常量引用
 * @param engine 引擎常量引用
 * @param camera 相机信息
 */
void ShadowMapDescriptorSet::prepareCamera(Transaction const& transaction,
        FEngine const& engine, const CameraInfo& camera) noexcept {
    PerViewDescriptorSetUtils::prepareCamera(edit(transaction), engine, camera);  // 准备相机数据
    // TODO: stereo values didn't used to be set  // TODO: 立体值以前没有设置
}

/**
 * 准备 LOD 偏移
 * 
 * 更新统一缓冲区中的 LOD 偏移。
 * 
 * @param transaction 事务常量引用
 * @param bias LOD 偏移
 */
void ShadowMapDescriptorSet::prepareLodBias(Transaction const& transaction, float const bias) noexcept {
    PerViewDescriptorSetUtils::prepareLodBias(edit(transaction), bias, 0);  // 准备 LOD 偏移（导数缩放为 0）
    // TODO: check why derivativesScale was missing  // TODO: 检查为什么导数缩放缺失
}

/**
 * 准备视口
 * 
 * 更新统一缓冲区中的视口数据。
 * 
 * @param transaction 事务常量引用
 * @param viewport 视口
 */
void ShadowMapDescriptorSet::prepareViewport(Transaction const& transaction,
        backend::Viewport const& viewport) noexcept {
    PerViewDescriptorSetUtils::prepareViewport(edit(transaction), viewport, viewport);  // 准备视口（物理和逻辑视口相同）
    // TODO: offset calculation is now different  // TODO: 偏移计算现在不同了
}

/**
 * 准备时间
 * 
 * 更新统一缓冲区中的时间数据。
 * 
 * @param transaction 事务常量引用
 * @param engine 引擎常量引用
 * @param userTime 用户时间（float4）
 */
void ShadowMapDescriptorSet::prepareTime(Transaction const& transaction,
        FEngine const& engine, float4 const& userTime) noexcept {
    PerViewDescriptorSetUtils::prepareTime(edit(transaction), engine, userTime);  // 准备时间
}

/**
 * 准备材质全局变量
 * 
 * 更新统一缓冲区中的材质全局变量。
 * 
 * @param transaction 事务常量引用
 * @param materialGlobals 材质全局变量数组（4 个 float4）
 */
void ShadowMapDescriptorSet::prepareMaterialGlobals(Transaction const& transaction,
        std::array<float4, 4> const& materialGlobals) noexcept {
    PerViewDescriptorSetUtils::prepareMaterialGlobals(edit(transaction), materialGlobals);  // 准备材质全局变量
}

/**
 * 准备阴影映射
 * 
 * 更新统一缓冲区中的 VSM（方差阴影贴图）指数。
 * 
 * @param transaction 事务常量引用
 * @param highPrecision 是否使用高精度
 */
void ShadowMapDescriptorSet::prepareShadowMapping(Transaction const& transaction,
        bool const highPrecision) noexcept {
    auto& s = edit(transaction);  // 获取统一缓冲区数据引用
    /**
     * VSM 指数常量
     * 
     * low: 约等于 log(half::max()) * 0.5
     * high: 约等于 log(float::max()) * 0.5
     */
    constexpr float low  = 5.54f;  // 低精度指数
    // ~ std::log(std::numeric_limits<math::half>::max()) * 0.5f;
    constexpr float high = 42.0f;  // 高精度指数
    // ~ std::log(std::numeric_limits<float>::max()) * 0.5f;
    s.vsmExponent = highPrecision ? high : low;  // 根据精度设置指数
}

/**
 * 打开事务
 * 
 * 分配统一缓冲区数据并返回事务对象。
 * 
 * @param driver 驱动 API 引用
 * @return 事务对象
 */
ShadowMapDescriptorSet::Transaction ShadowMapDescriptorSet::open(DriverApi& driver) noexcept {
    Transaction transaction;  // 创建事务对象
    // TODO: use out-of-line buffer if too large  // TODO: 如果太大则使用行外缓冲区
    transaction.uniforms = (PerViewUib *)driver.allocate(sizeof(PerViewUib), 16);  // 分配统一缓冲区数据（16 字节对齐）
    assert_invariant(transaction.uniforms);  // 断言分配成功
    return transaction;  // 返回事务对象
}

/**
 * 提交事务
 * 
 * 将统一缓冲区数据更新到 GPU 并提交描述符集。
 * 
 * @param transaction 事务引用
 * @param engine 引擎引用
 * @param driver 驱动 API 引用
 */
void ShadowMapDescriptorSet::commit(Transaction& transaction,
        FEngine& engine, DriverApi& driver) noexcept {
    driver.updateBufferObject(mUniformBufferHandle, {  // 更新缓冲区对象
            transaction.uniforms, sizeof(PerViewUib) }, 0);  // 数据指针、大小、偏移
    mDescriptorSet.commit(engine.getPerViewDescriptorSetLayoutDepthVariant(), driver);  // 提交描述符集
    transaction.uniforms = nullptr;  // 清空统一缓冲区数据指针
}

/**
 * 绑定描述符集
 * 
 * 将描述符集绑定到指定绑定点。
 * 
 * @param driver 驱动 API 引用
 */
void ShadowMapDescriptorSet::bind(DriverApi& driver) noexcept {
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_VIEW);  // 绑定到每视图绑定点
}

} // namespace filament

