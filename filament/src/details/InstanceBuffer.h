/*
* Copyright (C) 2023 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_INSTANCEBUFFER_H
#define TNT_FILAMENT_DETAILS_INSTANCEBUFFER_H

#include "downcast.h"

#include <filament/InstanceBuffer.h>

#include <backend/Handle.h>

#include <math/mat4.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>

#include <cstddef>
#include <cstdint>

namespace filament {

class RenderableManager;
class FEngine;

struct PerRenderableData;

/**
 * 实例缓冲区实现类
 * 
 * 管理实例化渲染的本地变换矩阵。
 * 实例化渲染允许使用相同的几何体但不同的变换矩阵来渲染多个对象。
 * 
 * 实现细节：
 * - 存储每个实例的本地变换矩阵（mat4f）
 * - 在渲染时，将本地变换与根变换相乘得到世界变换
 * - 支持批量更新变换矩阵
 */
class FInstanceBuffer : public InstanceBuffer {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FInstanceBuffer(FEngine& engine, const Builder& builder);
    
    /**
     * 析构函数
     */
    ~FInstanceBuffer() noexcept;

    /**
     * 终止实例缓冲区
     * 
     * 清理资源，重置索引。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 获取实例数量
     * 
     * @return 实例数量
     */
    size_t getInstanceCount() const noexcept { return mInstanceCount; }

    /**
     * 设置本地变换矩阵
     * 
     * 批量设置实例的本地变换矩阵。
     * 
     * @param localTransforms 本地变换矩阵数组
     * @param count 数量
     * @param offset 起始偏移量
     */
    void setLocalTransforms(math::mat4f const* localTransforms, size_t count, size_t offset);

    /**
     * 获取指定实例的本地变换矩阵
     * 
     * @param index 实例索引
     * @return 本地变换矩阵常量引用
     */
    math::mat4f const& getLocalTransform(size_t index) const noexcept;

    /**
     * 准备渲染数据
     * 
     * 将本地变换与根变换相乘，计算世界变换矩阵和法线变换矩阵，
     * 并将结果写入缓冲区。
     * 
     * @param buffer 输出缓冲区
     * @param index 起始索引
     * @param count 实例数量
     * @param rootTransform 根变换矩阵
     * @param ubo 每可渲染对象的 UBO 数据
     */
    void prepare(
            PerRenderableData* buffer, uint32_t index, uint32_t count,
            math::mat4f const& rootTransform, PerRenderableData const& ubo);

    /**
     * 获取名称
     * 
     * @return 名称常量引用
     */
    utils::ImmutableCString const& getName() const noexcept { return mName; }

    /**
     * 获取索引
     * 
     * @return 索引值
     */
    uint32_t getIndex() const noexcept { return mIndex; }

private:
    friend class RenderableManager;

    utils::FixedCapacityVector<math::mat4f> mLocalTransforms;  // 本地变换矩阵数组
    utils::ImmutableCString mName;  // 名称
    uint32_t mInstanceCount;  // 实例数量
    uint32_t mIndex = 0;  // 当前索引
};

FILAMENT_DOWNCAST(InstanceBuffer)

} // namespace filament

#endif //TNT_FILAMENT_DETAILS_INSTANCEBUFFER_H
