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

#ifndef TNT_FILAMENT_SHADOWMAPDESCRIPTORSET_H
#define TNT_FILAMENT_SHADOWMAPDESCRIPTORSET_H

#include "DescriptorSet.h"

#include "DescriptorSetLayout.h"

#include "private/filament/UibStructs.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <math/vec4.h>

#include <array>

namespace filament {

struct CameraInfo;

class FEngine;
class LightManager;

/**
 * 阴影贴图描述符集
 * 
 * 管理生成阴影贴图所需的 UBO。内部只持有 `PerViewUniform` UBO 句柄，
 * 但不保留任何影子副本，而是直接将数据写入 CommandStream，
 * 因此无法进行部分数据更新。
 * 
 * 使用事务模式（Transaction）来管理统一数据的更新，确保数据一致性。
 */
/*
 * PerShadowMapUniforms manages the UBO needed to generate our shadow maps. Internally it just
 * holds onto a `PerViewUniform` UBO handle, but doesn't keep any shadow copy of it, instead it
 * writes the data directly into the CommandStream, for this reason partial update of the data
 * is not possible.
 */
class ShadowMapDescriptorSet {

public:
    /**
     * 事务类
     * 
     * 用于管理统一数据的更新事务。只能由 ShadowMapDescriptorSet 创建。
     */
    class Transaction {
        friend ShadowMapDescriptorSet;  // 友元类
        PerViewUib* uniforms = nullptr;  // 每视图统一数据指针
        Transaction() = default;  // 禁止调用者创建
        // disallow creation by the caller
    };

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit ShadowMapDescriptorSet(FEngine& engine) noexcept;

    /**
     * 终止描述符集
     * 
     * 释放所有资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver);

    /**
     * 所有可能影响用户代码的 UBO 值必须在这里设置
     */
    // All UBO values that can affect user code must be set here

    /**
     * 准备相机统一数据
     * 
     * 设置相机相关的每视图统一数据（视图矩阵、投影矩阵等）。
     * 
     * @param transaction 事务引用
     * @param engine 引擎常量引用
     * @param camera 相机信息
     */
    static void prepareCamera(Transaction const& transaction,
            FEngine const& engine, const CameraInfo& camera) noexcept;

    /**
     * 准备 LOD 偏置统一数据
     * 
     * 设置细节层次（LOD）偏置。
     * 
     * @param transaction 事务引用
     * @param bias LOD 偏置值
     */
    static void prepareLodBias(Transaction const& transaction,
            float bias) noexcept;

    /**
     * 准备视口统一数据
     * 
     * 设置视口。
     * 
     * @param transaction 事务引用
     * @param viewport 视口
     */
    static void prepareViewport(Transaction const& transaction,
            backend::Viewport const& viewport) noexcept;

    /**
     * 准备时间统一数据
     * 
     * 设置引擎时间和用户时间。
     * 
     * @param transaction 事务引用
     * @param engine 引擎常量引用
     * @param userTime 用户时间（float4，可用于动画等）
     */
    static void prepareTime(Transaction const& transaction,
            FEngine const& engine, math::float4 const& userTime) noexcept;

    /**
     * 准备材质全局统一数据
     * 
     * 设置材质全局参数（4 个 float4）。
     * 
     * @param transaction 事务引用
     * @param materialGlobals 材质全局参数数组（4 个 float4）
     */
    static void prepareMaterialGlobals(Transaction const& transaction,
            std::array<math::float4, 4> const& materialGlobals) noexcept;

    /**
     * 准备阴影映射统一数据
     * 
     * 设置阴影映射相关参数（高精度标志等）。
     * 
     * @param transaction 事务引用
     * @param highPrecision 是否使用高精度阴影
     */
    static void prepareShadowMapping(Transaction const& transaction,
            bool highPrecision) noexcept;

    /**
     * 打开事务
     * 
     * 开始一个统一数据更新事务。
     * 
     * @param driver 驱动 API 引用
     * @return 事务对象
     */
    static Transaction open(backend::DriverApi& driver) noexcept;

    /**
     * 提交统一数据
     * 
     * 将本地数据更新到 GPU UBO。
     * 
     * @param transaction 事务引用
     * @param engine 引擎引用
     * @param driver 驱动 API 引用
     */
    // update local data into GPU UBO
    void commit(Transaction& transaction, FEngine& engine, backend::DriverApi& driver) noexcept;

    /**
     * 绑定描述符集
     * 
     * 绑定此 UBO 到渲染管线。
     * 
     * @param driver 驱动 API 引用
     */
    // bind this UBO
    void bind(backend::DriverApi& driver) noexcept;

private:
    /**
     * 编辑统一数据
     * 
     * 获取事务中的统一数据引用，用于直接修改。
     * 
     * @param transaction 事务引用
     * @return 每视图统一数据引用
     */
    static PerViewUib& edit(Transaction const& transaction) noexcept;
    backend::Handle<backend::HwBufferObject> mUniformBufferHandle;  // 统一缓冲区对象句柄
    DescriptorSet mDescriptorSet;  // 描述符集
};

} // namespace filament

#endif //TNT_FILAMENT_SHADOWMAPDESCRIPTORSET_H
