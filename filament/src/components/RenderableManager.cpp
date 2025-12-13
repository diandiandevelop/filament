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

#include "FilamentAPI-impl.h"

#include "RenderPrimitive.h"

#include "components/RenderableManager.h"

#include "ds/DescriptorSet.h"

#include "details/Engine.h"
#include "details/VertexBuffer.h"
#include "details/IndexBuffer.h"
#include "details/InstanceBuffer.h"
#include "details/Material.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <filament/Box.h>
#include <filament/FilamentAPI.h>
#include <filament/MaterialEnums.h>
#include <filament/RenderableManager.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/EntityManager.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Log.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/Slice.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/mat4.h>
#include <math/scalar.h>
#include <math/vec2.h>
#include <math/vec4.h>

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

using namespace filament::math;
using namespace utils;

namespace filament {

using namespace backend;

/**
 * 可渲染对象构建器详情结构
 * 
 * 存储可渲染对象构建器的所有配置参数。
 */
struct RenderableManager::BuilderDetails {
    using Entry = FRenderableManager::Entry;  // 条目类型别名
    std::vector<Entry> mEntries;  // 图元条目列表
    Box mAABB;  // 轴对齐包围盒
    uint8_t mLayerMask = 0x1;  // 层掩码（用于可见性控制）
    uint8_t mPriority = 0x4;  // 渲染优先级
    uint8_t mCommandChannel = Builder::DEFAULT_CHANNEL;  // 命令通道
    uint8_t mLightChannels = 1;  // 光源通道（用于光源分组）
    uint16_t mInstanceCount = 1;  // 实例数量
    bool mCulling : 1;  // 是否启用剔除
    bool mCastShadows : 1;  // 是否投射阴影
    bool mReceiveShadows : 1;  // 是否接收阴影
    bool mScreenSpaceContactShadows : 1;  // 是否启用屏幕空间接触阴影
    bool mSkinningBufferMode : 1;  // 是否使用蒙皮缓冲区模式
    bool mFogEnabled : 1;  // 是否启用雾效
    Builder::GeometryType mGeometryType : 2;  // 几何类型（静态/动态）
    size_t mSkinningBoneCount = 0;  // 蒙皮骨骼数量
    size_t mMorphTargetCount = 0;  // 变形目标数量
    FMorphTargetBuffer* mMorphTargetBuffer = nullptr;  // 变形目标缓冲区指针
    Bone const* mUserBones = nullptr;  // 用户骨骼数据指针
    mat4f const* mUserBoneMatrices = nullptr;  // 用户骨骼矩阵指针
    FSkinningBuffer* mSkinningBuffer = nullptr;  // 蒙皮缓冲区指针
    FInstanceBuffer* mInstanceBuffer = nullptr;  // 实例缓冲区指针
    uint32_t mSkinningBufferOffset = 0;  // 蒙皮缓冲区偏移量
    FixedCapacityVector<float2> mBoneIndicesAndWeights;  // 骨骼索引和权重
    size_t mBoneIndicesAndWeightsCount = 0;  // 骨骼索引和权重数量

    /**
     * 骨骼索引和权重映射
     * 
     * 为每个图元索引定义的骨骼索引和权重。
     */
    // bone indices and weights defined for primitive index
    std::unordered_map<size_t, FixedCapacityVector<
        FixedCapacityVector<float2>>> mBonePairs;

    /**
     * 构造函数
     * 
     * @param count 图元数量
     */
    explicit BuilderDetails(size_t const count)
            : mEntries(count),  // 初始化条目列表
              mCulling(true),  // 默认启用剔除
              mCastShadows(false),  // 默认不投射阴影
              mReceiveShadows(true),  // 默认接收阴影
              mScreenSpaceContactShadows(false),  // 默认不启用屏幕空间接触阴影
              mSkinningBufferMode(false),  // 默认不使用蒙皮缓冲区模式
              mFogEnabled(true),  // 默认启用雾效
              mGeometryType(Builder::GeometryType::DYNAMIC),  // 默认动态几何
              mBonePairs() {  // 初始化骨骼对映射
    }
    
    /**
     * 默认构造函数
     * 
     * 仅用于下面的显式实例化。
     */
    // this is only needed for the explicit instantiation below
    BuilderDetails() = default;

    /**
     * 处理骨骼索引和权重
     * 
     * @param engine 引擎引用
     * @param entity 实体
     */
    void processBoneIndicesAndWights(Engine& engine, Entity entity);

};

using BuilderType = RenderableManager;

/**
 * 可渲染对象构建器构造函数
 * 
 * @param count 图元数量
 */
BuilderType::Builder::Builder(size_t count) noexcept
        : BuilderBase<BuilderDetails>(count) {
    assert_invariant(mImpl->mEntries.size() == count);  // 断言条目数量匹配
}
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;


/**
 * 设置几何数据（简化版本）
 * 
 * 使用整个顶点缓冲区和索引缓冲区。
 * 
 * @param index 图元索引
 * @param type 图元类型
 * @param vertices 顶点缓冲区指针
 * @param indices 索引缓冲区指针
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::geometry(size_t const index,
        PrimitiveType const type, VertexBuffer* vertices, IndexBuffer* indices) noexcept {
    return geometry(index, type, vertices, indices,
            0, 0, vertices->getVertexCount() - 1, indices->getIndexCount());  // 使用整个缓冲区
}

/**
 * 设置几何数据（带偏移和计数）
 * 
 * @param index 图元索引
 * @param type 图元类型
 * @param vertices 顶点缓冲区指针
 * @param indices 索引缓冲区指针
 * @param offset 索引偏移量
 * @param count 索引数量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::geometry(size_t const index,
        PrimitiveType const type, VertexBuffer* vertices, IndexBuffer* indices,
        size_t const offset, size_t const count) noexcept {
    return geometry(index, type, vertices, indices, offset,
            0, vertices->getVertexCount() - 1, count);  // 使用默认最小/最大索引
}

/**
 * 设置几何数据（完整版本）
 * 
 * @param index 图元索引
 * @param type 图元类型
 * @param vertices 顶点缓冲区指针
 * @param indices 索引缓冲区指针
 * @param offset 索引偏移量
 * @param minIndex 最小索引（未使用，保留以保持 API 兼容性）
 * @param maxIndex 最大索引（未使用，保留以保持 API 兼容性）
 * @param count 索引数量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::geometry(size_t const index,
        PrimitiveType const type, VertexBuffer* vertices, IndexBuffer* indices,
        size_t const offset, UTILS_UNUSED size_t minIndex, UTILS_UNUSED size_t maxIndex, size_t const count) noexcept {
    std::vector<BuilderDetails::Entry>& entries = mImpl->mEntries;  // 获取条目列表
    if (index < entries.size()) {  // 如果索引有效
        entries[index].vertices = vertices;  // 设置顶点缓冲区
        entries[index].indices = indices;  // 设置索引缓冲区
        entries[index].offset = offset;  // 设置偏移量
        entries[index].count = count;  // 设置数量
        entries[index].type = type;  // 设置图元类型
    }
    return *this;
}

/**
 * 设置几何类型
 * 
 * 指定几何是静态还是动态，用于优化。
 * 
 * @param type 几何类型（静态/动态）
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::geometryType(GeometryType const type) noexcept {
    mImpl->mGeometryType = type;  // 设置几何类型
    return *this;
}

/**
 * 设置材质实例
 * 
 * 为指定图元设置材质实例。
 * 
 * @param index 图元索引
 * @param materialInstance 材质实例指针
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::material(size_t const index,
        MaterialInstance const* materialInstance) noexcept {
    if (index < mImpl->mEntries.size()) {  // 如果索引有效
        mImpl->mEntries[index].materialInstance = materialInstance;  // 设置材质实例
    }
    return *this;
}

/**
 * 设置轴对齐包围盒
 * 
 * 用于视锥剔除和碰撞检测。
 * 
 * @param axisAlignedBoundingBox 轴对齐包围盒
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::boundingBox(const Box& axisAlignedBoundingBox) noexcept {
    mImpl->mAABB = axisAlignedBoundingBox;  // 设置包围盒
    return *this;
}

/**
 * 设置层掩码
 * 
 * 用于可见性控制。可以选择性地设置某些位的值。
 * 
 * @param select 选择掩码（指定要修改的位）
 * @param values 值掩码（要设置的值）
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::layerMask(uint8_t const select, uint8_t const values) noexcept {
    mImpl->mLayerMask = (mImpl->mLayerMask & ~select) | (values & select);  // 按位更新层掩码
    return *this;
}

/**
 * 设置渲染优先级
 * 
 * 优先级范围：0-7。数值越大，优先级越高。
 * 
 * @param priority 优先级值
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::priority(uint8_t const priority) noexcept {
    mImpl->mPriority = std::min(priority, uint8_t(0x7));  // 限制优先级范围（0-7）
    return *this;
}

/**
 * 设置命令通道
 * 
 * 用于渲染通道分组。
 * 
 * @param channel 通道索引
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::channel(uint8_t const channel) noexcept {
    mImpl->mCommandChannel = std::min(channel, uint8_t(CONFIG_RENDERPASS_CHANNEL_COUNT - 1));  // 限制通道范围
    return *this;
}

/**
 * 设置是否启用剔除
 * 
 * @param enable 是否启用剔除
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::culling(bool const enable) noexcept {
    mImpl->mCulling = enable;  // 设置剔除标志
    return *this;
}

/**
 * 设置光源通道
 * 
 * 控制对象受哪些光源通道影响。
 * 
 * @param channel 通道索引（0-7）
 * @param enable 是否启用此通道
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::lightChannel(unsigned int const channel, bool const enable) noexcept {
    if (channel < 8) {  // 如果通道索引有效
        const uint8_t mask = 1u << channel;  // 创建位掩码
        mImpl->mLightChannels &= ~mask;  // 清除该位
        mImpl->mLightChannels |= enable ? mask : 0u;  // 根据 enable 设置该位
    }
    return *this;
}

/**
 * 设置是否投射阴影
 * 
 * @param enable 是否启用阴影投射
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::castShadows(bool const enable) noexcept {
    mImpl->mCastShadows = enable;  // 设置阴影投射标志
    return *this;
}

/**
 * 设置是否接收阴影
 * 
 * @param enable 是否启用阴影接收
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::receiveShadows(bool const enable) noexcept {
    mImpl->mReceiveShadows = enable;  // 设置阴影接收标志
    return *this;
}

/**
 * 设置是否启用屏幕空间接触阴影
 * 
 * @param enable 是否启用屏幕空间接触阴影
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::screenSpaceContactShadows(bool const enable) noexcept {
    mImpl->mScreenSpaceContactShadows = enable;  // 设置屏幕空间接触阴影标志
    return *this;
}

/**
 * 设置蒙皮（仅骨骼数量）
 * 
 * 仅指定骨骼数量，不提供骨骼数据。
 * 
 * @param boneCount 骨骼数量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::skinning(size_t const boneCount) noexcept {
    mImpl->mSkinningBoneCount = boneCount;  // 设置骨骼数量
    return *this;
}

/**
 * 设置蒙皮（使用骨骼数据）
 * 
 * 提供骨骼数据用于蒙皮计算。
 * 
 * @param boneCount 骨骼数量
 * @param bones 骨骼数据指针
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::skinning(
        size_t const boneCount, Bone const* bones) noexcept {
    mImpl->mSkinningBoneCount = boneCount;  // 设置骨骼数量
    mImpl->mUserBones = bones;  // 设置骨骼数据指针
    return *this;
}

/**
 * 设置蒙皮（使用变换矩阵）
 * 
 * 提供骨骼变换矩阵用于蒙皮计算。
 * 
 * @param boneCount 骨骼数量
 * @param transforms 变换矩阵数组指针
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::skinning(
        size_t const boneCount, mat4f const* transforms) noexcept {
    mImpl->mSkinningBoneCount = boneCount;  // 设置骨骼数量
    mImpl->mUserBoneMatrices = transforms;  // 设置变换矩阵指针
    return *this;
}

/**
 * 设置蒙皮（使用蒙皮缓冲区）
 * 
 * 使用蒙皮缓冲区存储骨骼数据。
 * 
 * @param skinningBuffer 蒙皮缓冲区指针
 * @param count 骨骼数量
 * @param offset 缓冲区偏移量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::skinning(
        SkinningBuffer* skinningBuffer, size_t const count, size_t const offset) noexcept {
    mImpl->mSkinningBuffer = downcast(skinningBuffer);  // 转换为实现类指针
    mImpl->mSkinningBoneCount = count;  // 设置骨骼数量
    mImpl->mSkinningBufferOffset = offset;  // 设置缓冲区偏移量
    return *this;
}

/**
 * 启用蒙皮缓冲区模式
 * 
 * 指定是否使用蒙皮缓冲区模式。
 * 
 * @param enabled 是否启用
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::enableSkinningBuffers(bool const enabled) noexcept {
    mImpl->mSkinningBufferMode = enabled;  // 设置蒙皮缓冲区模式标志
    return *this;
}

/**
 * 设置骨骼索引和权重（数组版本）
 * 
 * 为指定图元设置每个顶点的骨骼索引和权重。
 * 
 * @param primitiveIndex 图元索引
 * @param indicesAndWeights 骨骼索引和权重数组（交错存储：索引0,权重0,索引1,权重1,...）
 * @param count 数组元素总数
 * @param bonesPerVertex 每个顶点的骨骼数量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::boneIndicesAndWeights(size_t const primitiveIndex,
               float2 const* indicesAndWeights, size_t const count, size_t const bonesPerVertex) noexcept {
    size_t const vertexCount = count / bonesPerVertex;  // 计算顶点数量
    FixedCapacityVector<FixedCapacityVector<float2>> bonePairs(vertexCount);  // 创建顶点数据向量
    /**
     * 将交错存储的数据转换为每顶点一个向量的格式
     */
    for (size_t iVertex = 0; iVertex < vertexCount; iVertex++) {
        FixedCapacityVector<float2> vertexData(bonesPerVertex);  // 创建顶点数据向量
        std::copy_n(indicesAndWeights + iVertex * bonesPerVertex,  // 源起始位置
                bonesPerVertex, vertexData.data());  // 复制数据
        bonePairs[iVertex] = std::move(vertexData);  // 移动数据到向量
    }
    return boneIndicesAndWeights(primitiveIndex, bonePairs);  // 调用向量版本
}

/**
 * 设置骨骼索引和权重（向量版本）
 * 
 * 为指定图元设置每个顶点的骨骼索引和权重。
 * 
 * @param primitiveIndex 图元索引
 * @param indicesAndWeightsVector 骨骼索引和权重向量（每顶点一个向量）
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::boneIndicesAndWeights(size_t const primitiveIndex,
        FixedCapacityVector<
            FixedCapacityVector<float2>> indicesAndWeightsVector) noexcept {
    mImpl->mBonePairs[primitiveIndex] = std::move(indicesAndWeightsVector);  // 移动数据到构建器
    return *this;
}

/**
 * 设置是否启用雾效
 * 
 * @param enabled 是否启用雾效
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::fog(bool const enabled) noexcept {
    mImpl->mFogEnabled = enabled;  // 设置雾效标志
    return *this;
}

/**
 * 设置变形目标数量
 * 
 * @param targetCount 变形目标数量
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::morphing(size_t const targetCount) noexcept {
    mImpl->mMorphTargetCount = targetCount;  // 设置变形目标数量
    return *this;
}

/**
 * 设置变形目标（使用变形目标缓冲区）
 * 
 * 使用变形目标缓冲区存储变形目标数据。
 * 
 * @param morphTargetBuffer 变形目标缓冲区指针（非空）
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::morphing(
        MorphTargetBuffer* UTILS_NONNULL morphTargetBuffer) noexcept {
    mImpl->mMorphTargetBuffer = downcast(morphTargetBuffer);  // 转换为实现类指针
    mImpl->mMorphTargetCount = morphTargetBuffer->getCount();  // 从缓冲区获取变形目标数量
    return *this;
}

/**
 * 设置变形目标（为指定图元设置偏移量）
 * 
 * 为指定图元设置变形目标缓冲区中的偏移量。
 * 
 * @param level 变形级别（未使用，保留以保持 API 兼容性）
 * @param primitiveIndex 图元索引
 * @param offset 缓冲区偏移量
 * @return 构建器引用（支持链式调用）
 * 
 * 注意：最后一个参数 "count" 未使用，因为它必须等于图元的顶点数量。
 */
RenderableManager::Builder& RenderableManager::Builder::morphing(uint8_t level,
        size_t const primitiveIndex, size_t const offset) noexcept {
    // the last parameter "count" is unused, because it must be equal to the primitive's vertex count
    std::vector<BuilderDetails::Entry>& entries = mImpl->mEntries;  // 获取条目列表
    if (primitiveIndex < entries.size()) {  // 如果图元索引有效
        auto& morphing = entries[primitiveIndex].morphing;  // 获取变形目标引用
        morphing.offset = uint32_t(offset);  // 设置偏移量
    }
    return *this;
}

/**
 * 设置混合顺序
 * 
 * 用于控制透明对象的渲染顺序。
 * 
 * @param index 图元索引
 * @param blendOrder 混合顺序值
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::blendOrder(
        size_t const index, uint16_t const blendOrder) noexcept {
    if (index < mImpl->mEntries.size()) {  // 如果索引有效
        mImpl->mEntries[index].blendOrder = blendOrder;  // 设置混合顺序
    }
    return *this;
}

/**
 * 设置全局混合顺序启用状态
 * 
 * 如果启用，混合顺序将在全局范围内排序，而不仅限于同一对象内的图元。
 * 
 * @param index 图元索引
 * @param enabled 是否启用全局混合顺序
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::globalBlendOrderEnabled(
        size_t const index, bool const enabled) noexcept {
    if (index < mImpl->mEntries.size()) {  // 如果索引有效
        mImpl->mEntries[index].globalBlendOrderEnabled = enabled;  // 设置全局混合顺序标志
    }
    return *this;
}

/**
 * 处理骨骼索引和权重
 * 
 * 处理高级蒙皮的骨骼索引和权重数据，将其转换为顶点属性和纹理数据。
 * 
 * @param engine 引擎引用
 * @param entity 实体
 * 
 * 功能：
 * - 验证骨骼索引和权重数据
 * - 将数据转换为顶点属性格式（最多 4 个骨骼/顶点）
 * - 将超过 4 个骨骼的数据存储到纹理中
 * - 归一化权重
 */
UTILS_NOINLINE
void RenderableManager::BuilderDetails::processBoneIndicesAndWights(Engine& engine, Entity const entity) {
    size_t maxPairsCount = 0;  // 纹理大小，骨骼对数量
    size_t maxPairsCountPerVertex = 0;  // 每个顶点的最大骨骼数量

    /**
     * 遍历所有图元的骨骼对，计算最大骨骼对数量和每顶点最大骨骼数
     */
    for (auto& bonePair: mBonePairs) {
        auto primitiveIndex = bonePair.first;  // 图元索引
        auto entries = mEntries;  // 条目列表
        /**
         * 验证图元索引有效
         */
        FILAMENT_CHECK_PRECONDITION(primitiveIndex < entries.size() && primitiveIndex >= 0)
                << "[primitive @ " << primitiveIndex << "] primitiveindex is out of size ("
                << entries.size() << ")";
        auto entry = mEntries[primitiveIndex];  // 获取条目
        auto bonePairsForPrimitive = bonePair.second;  // 获取该图元的骨骼对
        auto vertexCount = entry.vertices->getVertexCount();  // 获取顶点数量
        /**
         * 验证骨骼对数量等于顶点数量
         */
        FILAMENT_CHECK_PRECONDITION(bonePairsForPrimitive.size() == vertexCount)
                << "[primitive @ " << primitiveIndex << "] bone indices and weights pairs count ("
                << bonePairsForPrimitive.size() << ") must be equal to vertex count ("
                << vertexCount << ")";
        /**
         * 验证顶点缓冲区声明了骨骼索引或权重属性
         */
        auto const& declaredAttributes = downcast(entry.vertices)->getDeclaredAttributes();  // 获取声明的属性
        FILAMENT_CHECK_PRECONDITION(declaredAttributes[VertexAttribute::BONE_INDICES] ||
                declaredAttributes[VertexAttribute::BONE_WEIGHTS])
                << "[entity=" << entity.getId() << ", primitive @ " << primitiveIndex
                << "] for advanced skinning set VertexBuffer::Builder::advancedSkinning()";
        /**
         * 计算最大骨骼对数量和每顶点最大骨骼数
         */
        for (size_t iVertex = 0; iVertex < vertexCount; iVertex++) {
            size_t const bonesPerVertex = bonePairsForPrimitive[iVertex].size();  // 该顶点的骨骼数量
            maxPairsCount += bonesPerVertex;  // 累加骨骼对数量
            maxPairsCountPerVertex = std::max(bonesPerVertex, maxPairsCountPerVertex);  // 更新最大值
        }
    }

    /**
     * 处理骨骼数据，转换为顶点属性和纹理格式
     */
    size_t pairsCount = 0;  // 存储在纹理中的骨骼对数量计数
    if (maxPairsCount) {  // 如果至少有一个图元有骨骼索引和权重
        /**
         * 最终纹理数据，存储索引和权重
         */
        // final texture data, indices and weights
        mBoneIndicesAndWeights = utils::FixedCapacityVector<float2>(maxPairsCount);  // 分配纹理数据
        /**
         * 临时存储一个顶点的索引和权重
         */
        // temporary indices and weights for one vertex
        auto const tempPairs = std::make_unique<float2[]>(maxPairsCountPerVertex);  // 临时数组
        /**
         * 遍历所有图元的骨骼对
         */
        for (auto& bonePair: mBonePairs) {
            auto primitiveIndex = bonePair.first;  // 图元索引
            auto bonePairsForPrimitive = bonePair.second;  // 该图元的骨骼对
            if (bonePairsForPrimitive.empty()) {  // 如果为空，跳过
                continue;
            }
            size_t const vertexCount = mEntries[primitiveIndex].vertices->getVertexCount();  // 获取顶点数量
            /**
             * 临时存储一个顶点的索引（最多 4 个）
             */
            // temporary indices for one vertex
            auto skinJoints = std::make_unique<uint16_t[]>(4 * vertexCount);  // 骨骼索引数组
            /**
             * 临时存储一个顶点的权重（最多 4 个）
             */
            // temporary weights for one vertex
            auto skinWeights = std::make_unique<float[]>(4 * vertexCount);  // 骨骼权重数组
            /**
             * 遍历每个顶点，处理其骨骼数据
             */
            for (size_t iVertex = 0; iVertex < vertexCount; iVertex++) {
                size_t tempPairCount = 0;  // 临时骨骼对计数
                double boneWeightsSum = 0;  // 骨骼权重总和
                /**
                 * 遍历该顶点的所有骨骼对
                 */
                for (size_t k = 0; k < bonePairsForPrimitive[iVertex].size(); k++) {
                    auto boneWeight = bonePairsForPrimitive[iVertex][k][1];  // 获取骨骼权重
                    auto boneIndex = bonePairsForPrimitive[iVertex][k][0];  // 获取骨骼索引
                    /**
                     * 验证骨骼权重非负
                     */
                    FILAMENT_CHECK_PRECONDITION(boneWeight >= 0)
                            << "[entity=" << entity.getId() << ", primitive @ " << primitiveIndex
                            << "] bone weight (" << boneWeight << ") of vertex=" << iVertex
                            << " is negative";
                    if (boneWeight > 0.0f) {  // 如果权重大于 0
                        /**
                         * 验证骨骼索引有效
                         */
                        FILAMENT_CHECK_PRECONDITION(boneIndex >= 0)
                                << "[entity=" << entity.getId() << ", primitive @ "
                                << primitiveIndex << "] bone index (" << (int)boneIndex
                                << ") of vertex=" << iVertex << " is negative";
                        FILAMENT_CHECK_PRECONDITION(boneIndex < mSkinningBoneCount)
                                << "[entity=" << entity.getId() << ", primitive @ "
                                << primitiveIndex << "] bone index (" << (int)boneIndex
                                << ") of vertex=" << iVertex << " is bigger then bone count ("
                                << mSkinningBoneCount << ")";
                        boneWeightsSum += boneWeight;  // 累加权重
                        tempPairs[tempPairCount][0] = boneIndex;  // 存储索引
                        tempPairs[tempPairCount][1] = boneWeight;  // 存储权重
                        tempPairCount++;  // 增加计数
                    }
                }

                /**
                 * 验证权重总和大于 0
                 */
                FILAMENT_CHECK_PRECONDITION(boneWeightsSum > 0)
                        << "[entity=" << entity.getId() << ", primitive @ " << primitiveIndex
                        << "] sum of bone weights of vertex=" << iVertex << " is " << boneWeightsSum
                        << ", it should be positive.";

                /**
                 * 归一化权重
                 * 
                 * 参考 glTF 2.0 规范：
                 * https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes
                 * 
                 * 如果权重总和接近 1.0（在容差范围内），则设为 1.0
                 */
                // see https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skinned-mesh-attributes
                double const epsilon = 2e-7 * double(tempPairCount);  // 计算容差
                if (abs(boneWeightsSum - 1.0) <= epsilon) {  // 如果接近 1.0
                    boneWeightsSum = 1.0;  // 设为 1.0
                }
#ifndef NDEBUG
                else {
                    /**
                     * 在调试模式下，如果权重总和不是 1.0，记录警告
                     */
                    LOG(WARNING) << "Warning of skinning: [entity=%" << entity.getId()
                                 << ", primitive @ %" << primitiveIndex
                                 << "] sum of bone weights of vertex=" << iVertex << " is "
                                 << boneWeightsSum
                                 << ", it should be one. Weights will be normalized.";  // 记录警告
                }
#endif

                /**
                 * 准备顶点属性数据
                 */
                // prepare data for vertex attributes
                auto offset = iVertex * 4;  // 计算偏移量（每顶点 4 个骨骼）
                /**
                 * 设置属性、索引和权重，用于 <= 4 个骨骼对的情况
                 */
                // set attributes, indices and weights, for <= 4 pairs
                for (size_t j = 0, c = std::min((int)tempPairCount, 4); j < c; j++) {
                    skinJoints[j + offset] = uint16_t(tempPairs[j][0]);  // 设置骨骼索引
                    skinWeights[j + offset] = tempPairs[j][1] / float(boneWeightsSum);  // 设置归一化权重
                }
                /**
                 * 准备纹理数据（用于 > 4 个骨骼对的情况）
                 */
                // prepare data for texture
                if (tempPairCount > 4) {  // 如果超过 4 个骨骼对
                    /**
                     * 设置属性、索引和权重，用于 > 4 个骨骼对的情况
                     */
                    // set attributes, indices and weights, for > 4 pairs
                    /**
                     * 纹理中每顶点的骨骼对数量
                     */
                    // number pairs per vertex in texture
                    skinJoints[3 + offset] = (uint16_t)tempPairCount;  // 存储骨骼对数量
                    /**
                     * 到纹理的负偏移量：0..-1, 1..-2
                     */
                    // negative offset to texture 0..-1, 1..-2
                    skinWeights[3 + offset] = -float(pairsCount + 1);  // 存储负偏移量
                    /**
                     * 将超过 4 个的骨骼对存储到纹理中
                     */
                    for (size_t j = 3; j < tempPairCount; j++) {
                        mBoneIndicesAndWeights[pairsCount][0] = tempPairs[j][0];  // 存储索引
                        mBoneIndicesAndWeights[pairsCount][1] = tempPairs[j][1] / float(boneWeightsSum);  // 存储归一化权重
                        pairsCount++;  // 增加计数
                    }
                }
            }  // 遍历完该图元的所有顶点
            /**
             * 更新顶点缓冲区的骨骼索引和权重
             */
            downcast(mEntries[primitiveIndex].vertices)
                ->updateBoneIndicesAndWeights(downcast(engine),  // 引擎引用
                                              std::move(skinJoints),  // 移动骨骼索引数组
                                              std::move(skinWeights));  // 移动骨骼权重数组
        }  // 遍历完所有图元
    }
    /**
     * 只使用 mBoneIndicesAndWeights 的一部分存储实际数据
     */
    mBoneIndicesAndWeightsCount = pairsCount;  // 设置实际使用的骨骼对数量
}

/**
 * 设置实例数量
 * 
 * 设置可渲染对象的实例数量（不使用实例缓冲区）。
 * 
 * @param instanceCount 实例数量（1-32767）
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::instances(size_t const instanceCount) noexcept {
    mImpl->mInstanceCount = clamp((unsigned int)instanceCount, 1u, 32767u);  // 限制实例数量范围
    return *this;
}

/**
 * 设置实例数量和实例缓冲区
 * 
 * 使用实例缓冲区提供实例变换数据。
 * 
 * @param instanceCount 实例数量
 * @param instanceBuffer 实例缓冲区指针
 * @return 构建器引用（支持链式调用）
 */
RenderableManager::Builder& RenderableManager::Builder::instances(
        size_t const instanceCount, InstanceBuffer* instanceBuffer) noexcept {
    mImpl->mInstanceCount = clamp(instanceCount, (size_t)1, CONFIG_MAX_INSTANCES);  // 限制实例数量范围
    mImpl->mInstanceBuffer = downcast(instanceBuffer);  // 转换为实现类指针
    return *this;
}

/**
 * 构建可渲染对象组件
 * 
 * 根据构建器配置创建可渲染对象组件。
 * 
 * @param engine 引擎引用
 * @param entity 实体
 * @return 构建结果
 */
RenderableManager::Builder::Result RenderableManager::Builder::build(Engine& engine, Entity const entity) {
    bool isEmpty = true;  // 是否为空（没有有效图元）

    /**
     * 验证骨骼数量不超过最大值
     */
    FILAMENT_CHECK_PRECONDITION(mImpl->mSkinningBoneCount <= CONFIG_MAX_BONE_COUNT)
            << "bone count > " << CONFIG_MAX_BONE_COUNT;

    /**
     * 验证实例数量（如果使用实例缓冲区）
     */
    FILAMENT_CHECK_PRECONDITION(
            mImpl->mInstanceCount <= CONFIG_MAX_INSTANCES || !mImpl->mInstanceBuffer)
            << "instance count is " << mImpl->mInstanceCount
            << ", but instance count is limited to CONFIG_MAX_INSTANCES (" << CONFIG_MAX_INSTANCES
            << ") instances when supplying transforms via an InstanceBuffer.";

    /**
     * 静态几何不能使用蒙皮和变形
     */
    if (mImpl->mGeometryType == GeometryType::STATIC) {
        FILAMENT_CHECK_PRECONDITION(mImpl->mSkinningBoneCount == 0)
                << "Skinning can't be used with STATIC geometry";  // 静态几何不能使用蒙皮

        FILAMENT_CHECK_PRECONDITION(mImpl->mMorphTargetCount == 0)
                << "Morphing can't be used with STATIC geometry";  // 静态几何不能使用变形
    }

    /**
     * 验证实例数量不超过实例缓冲区的实例数量
     */
    if (mImpl->mInstanceBuffer) {
        size_t const bufferInstanceCount = mImpl->mInstanceBuffer->mInstanceCount;  // 获取缓冲区实例数量
        FILAMENT_CHECK_PRECONDITION(mImpl->mInstanceCount <= bufferInstanceCount)
                << "instance count (" << mImpl->mInstanceCount
                << ") must be less than or equal to the InstanceBuffer's instance "
                   "count (" << bufferInstanceCount << ").";
    }

    /**
     * 如果有蒙皮数据，处理骨骼索引和权重
     */
    if (UTILS_LIKELY(mImpl->mSkinningBoneCount || mImpl->mSkinningBufferMode)) {
        mImpl->processBoneIndicesAndWights(engine, entity);  // 处理骨骼数据
    }

    /**
     * 遍历所有图元条目，验证和准备数据
     */
    for (size_t i = 0, c = mImpl->mEntries.size(); i < c; i++) {
        auto& entry = mImpl->mEntries[i];  // 获取条目引用

        /**
         * 即使索引/顶点为空，entry.materialInstance 也必须设置为某个值
         */
        // entry.materialInstance must be set to something even if indices/vertices are null
        FMaterial const* material;  // 材质指针
        if (!entry.materialInstance) {  // 如果没有材质实例
            material = downcast(engine.getDefaultMaterial());  // 使用默认材质
            entry.materialInstance = material->getDefaultInstance();  // 设置默认材质实例
        } else {
            material = downcast(entry.materialInstance->getMaterial());  // 获取材质
        }

        /**
         * 没有索引或顶点的图元将被忽略
         */
        // primitives without indices or vertices will be ignored
        if (!entry.indices || !entry.vertices) {  // 如果缺少索引或顶点
            continue;  // 跳过
        }

        /**
         * 我们希望特性级别违规是一个硬错误（如果启用则抛出异常，否则崩溃）
         */
        // we want a feature level violation to be a hard error (exception if enabled, or crash)
        FILAMENT_CHECK_PRECONDITION(downcast(engine).hasFeatureLevel(material->getFeatureLevel()))
                << "Material \"" << material->getName().c_str_safe() << "\" has feature level "
                << (uint8_t)material->getFeatureLevel() << " which is not supported by this Engine";

        /**
         * 拒绝无效的几何参数
         */
        // reject invalid geometry parameters
        FILAMENT_CHECK_PRECONDITION(entry.offset + entry.count <= entry.indices->getIndexCount())
                << "[entity=" << entity.getId() << ", primitive @ " << i << "] offset ("
                << entry.offset << ") + count (" << entry.count << ") > indexCount ("
                << entry.indices->getIndexCount() << ")";

        /**
         * 这不能是错误，因为：
         * (1) 这些值不是不可变的，调用者可以稍后修复
         * (2) 材质的着色器将工作（即编译），并使用此属性的默认值，这可能是可接受的
         */
        // this can't be an error because (1) those values are not immutable, so the caller
        // could fix later, and (2) the material's shader will work (i.e. compile), and
        // use the default values for this attribute, which maybe be acceptable.
        AttributeBitset const declared = downcast(entry.vertices)->getDeclaredAttributes();  // 获取声明的属性
        AttributeBitset const required = material->getRequiredAttributes();  // 获取必需的属性
        if ((declared & required) != required) {  // 如果缺少必需的属性
            LOG(WARNING) << "[entity=" << entity.getId() << ", primitive @ " << i
                         << "] missing required attributes (" << required
                         << "), declared=" << declared;  // 记录警告
        }

        /**
         * 我们至少有一个有效图元
         */
        // we have at least one valid primitive
        isEmpty = false;  // 标记为非空
    }

    /**
     * 验证包围盒
     * 
     * AABB 不能为空，除非：
     * - 剔除被禁用，且
     * - 对象不是阴影投射者/接收者，或
     * - 对象为空（没有有效图元）
     */
    FILAMENT_CHECK_PRECONDITION(!mImpl->mAABB.isEmpty() ||
            (!mImpl->mCulling && (!(mImpl->mReceiveShadows || mImpl->mCastShadows)) || isEmpty))
            << "[entity=" << entity.getId()
            << "] AABB can't be empty, unless culling is disabled and "
               "the object is not a shadow caster/receiver";

    /**
     * 创建可渲染对象组件
     */
    downcast(engine).createRenderable(*this, entity);  // 创建组件
    return Success;  // 返回成功
}

// ------------------------------------------------------------------------------------------------

/**
 * 可渲染对象管理器构造函数
 * 
 * @param engine 引擎引用
 * 
 * 注意：不要在构造函数中使用 engine，因为它还没有完全构造完成。
 */
FRenderableManager::FRenderableManager(FEngine& engine) noexcept : mEngine(engine) {
    // DON'T use engine here in the ctor, because it's not fully constructed yet.
}

/**
 * 可渲染对象管理器析构函数
 * 
 * 所有组件应该在此处之前已被销毁
 * （terminate 应该已经从 Engine 的 shutdown() 调用）
 */
FRenderableManager::~FRenderableManager() {
    // all components should have been destroyed when we get here
    // (terminate should have been called from Engine's shutdown())
    assert_invariant(mManager.getComponentCount() == 0);  // 断言组件数量为 0
}

/**
 * 创建可渲染对象组件
 * 
 * 根据构建器配置创建可渲染对象组件。
 * 
 * @param builder 构建器引用
 * @param entity 实体
 */
void FRenderableManager::create(
        const Builder& UTILS_RESTRICT builder, Entity const entity) {
    FEngine& engine = mEngine;  // 获取引擎引用
    auto& manager = mManager;  // 获取管理器引用
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API

    /**
     * 如果实体已有组件，先销毁它
     */
    if (UTILS_UNLIKELY(manager.hasComponent(entity))) {
        destroy(entity);  // 销毁现有组件
    }
    /**
     * 添加可渲染对象组件
     */
    Instance const ci = manager.addComponent(entity);  // 添加组件并获取实例
    assert_invariant(ci);  // 断言实例有效

    if (ci) {
        /**
         * 创建并初始化所有需要的渲染图元
         */
        // create and initialize all needed RenderPrimitives
        using size_type = Slice<const FRenderPrimitive>::size_type;  // 大小类型别名
        auto const * const entries = builder->mEntries.data();  // 获取条目数据指针
        const size_t entryCount = builder->mEntries.size();  // 获取条目数量
        FRenderPrimitive* rp = new FRenderPrimitive[entryCount];  // 分配渲染图元数组
        auto& factory = mHwRenderPrimitiveFactory;  // 获取硬件渲染图元工厂
        /**
         * 遍历所有条目，初始化渲染图元
         */
        for (size_t i = 0; i < entryCount; ++i) {
            rp[i].init(factory, driver, entries[i]);  // 初始化渲染图元
        }
        setPrimitives(ci, { rp, size_type(entryCount) });  // 设置渲染图元

        /**
         * 设置可渲染对象属性
         */
        setAxisAlignedBoundingBox(ci, builder->mAABB);  // 设置轴对齐包围盒
        setLayerMask(ci, builder->mLayerMask);  // 设置层掩码
        setPriority(ci, builder->mPriority);  // 设置优先级
        setChannel(ci, builder->mCommandChannel);  // 设置命令通道
        setCastShadows(ci, builder->mCastShadows);  // 设置是否投射阴影
        setReceiveShadows(ci, builder->mReceiveShadows);  // 设置是否接收阴影
        setScreenSpaceContactShadows(ci, builder->mScreenSpaceContactShadows);  // 设置屏幕空间接触阴影
        setCulling(ci, builder->mCulling);  // 设置是否启用剔除
        setSkinning(ci, false);  // 初始设置蒙皮为 false（稍后根据实际情况设置）
        setMorphing(ci, builder->mMorphTargetCount);  // 设置变形目标数量
        setFogEnabled(ci, builder->mFogEnabled);  // 设置是否启用雾效
        /**
         * 在调用 setAxisAlignedBoundingBox 之后执行
         */
        // do this after calling setAxisAlignedBoundingBox
        static_cast<Visibility&>(mManager[ci].visibility).geometryType = builder->mGeometryType;  // 设置几何类型
        mManager[ci].channels = builder->mLightChannels;  // 设置光源通道

        /**
         * 设置实例信息
         */
        InstancesInfo& instances = manager[ci].instances;  // 获取实例信息引用
        instances.count = builder->mInstanceCount;  // 设置实例数量
        instances.buffer = builder->mInstanceBuffer;  // 设置实例缓冲区

        /**
         * 处理蒙皮数据
         */
        const uint32_t boneCount = builder->mSkinningBoneCount;  // 获取骨骼数量
        const uint32_t targetCount = builder->mMorphTargetCount;  // 获取变形目标数量
        if (builder->mSkinningBufferMode) {  // 如果使用蒙皮缓冲区模式
            if (builder->mSkinningBuffer) {  // 如果提供了蒙皮缓冲区
                setSkinning(ci, boneCount > 0);  // 设置蒙皮标志
                Bones& bones = manager[ci].bones;  // 获取骨骼引用
                /**
                 * 使用蒙皮缓冲区的句柄和偏移量
                 */
                bones = Bones{
                        .handle = builder->mSkinningBuffer->getHwHandle(),  // 硬件句柄
                        .count = (uint16_t)boneCount,  // 骨骼数量
                        .offset = (uint16_t)builder->mSkinningBufferOffset,  // 缓冲区偏移量
                        .skinningBufferMode = true };  // 标记为蒙皮缓冲区模式
            }
        } else {  // 如果不使用蒙皮缓冲区模式
            if (UTILS_UNLIKELY(boneCount > 0 || targetCount > 0)) {  // 如果有骨骼或变形目标
                setSkinning(ci, boneCount > 0);  // 设置蒙皮标志
                Bones& bones = manager[ci].bones;  // 获取骨骼引用
                /**
                 * 注意：我们根据 CONFIG_MAX_BONE_COUNT 而不是 mSkinningBoneCount 来调整骨骼 UBO 的大小。
                 * 根据 OpenGL ES 3.2 规范 7.6.3 Uniform Buffer Object Bindings：
                 *
                 *     uniform 块必须填充一个大小不小于 uniform 块最小所需大小
                 *     （UNIFORM_BLOCK_DATA_SIZE 的值）的缓冲区对象。
                 *
                 * 这不幸意味着我们为蒙皮可渲染对象使用大的内存占用。
                 * 将来，我们可以尝试通过实现分页系统来解决这个问题，
                 * 使得多个蒙皮可渲染对象将在单个大骨骼块内共享区域。
                 */
                // Note that we are sizing the bones UBO according to CONFIG_MAX_BONE_COUNT rather than
                // mSkinningBoneCount. According to the OpenGL ES 3.2 specification in 7.6.3 Uniform
                // Buffer Object Bindings:
                //
                //     the uniform block must be populated with a buffer object with a size no smaller
                //     than the minimum required size of the uniform block (the value of
                //     UNIFORM_BLOCK_DATA_SIZE).
                //
                // This unfortunately means that we are using a large memory footprint for skinned
                // renderables. In the future we could try addressing this by implementing a paging
                // system such that multiple skinned renderables will share regions within a single
                // large block of bones.
                bones = Bones{
                        .handle = driver.createBufferObject(  // 创建缓冲区对象
                                sizeof(PerRenderableBoneUib),  // 大小
                                BufferObjectBinding::UNIFORM,  // 绑定类型：Uniform
                                BufferUsage::DYNAMIC),  // 使用方式：动态
                        .count = (uint16_t)boneCount,  // 骨骼数量
                        .offset = 0,  // 偏移量为 0
                        .skinningBufferMode = false };  // 标记为非蒙皮缓冲区模式

                if (boneCount) {  // 如果有骨骼
                    if (builder->mUserBones) {  // 如果提供了用户骨骼数据
                        FSkinningBuffer::setBones(mEngine, bones.handle,  // 设置骨骼数据
                                builder->mUserBones, boneCount, 0);
                    } else if (builder->mUserBoneMatrices) {  // 如果提供了用户骨骼矩阵
                        FSkinningBuffer::setBones(mEngine, bones.handle,  // 设置骨骼矩阵
                                builder->mUserBoneMatrices, boneCount, 0);
                    } else {
                        /**
                         * 将骨骼初始化为单位矩阵
                         */
                        // initialize the bones to identity
                        auto* out = driver.allocatePod<PerRenderableBoneUib::BoneData>(boneCount);  // 分配骨骼数据
                        std::uninitialized_fill_n(out, boneCount, FSkinningBuffer::makeBone({}));  // 填充为单位骨骼
                        driver.updateBufferObject(bones.handle, {  // 更新缓冲区对象
                                out, boneCount * sizeof(PerRenderableBoneUib::BoneData) }, 0);
                    }
                }
                else {  // 如果骨骼数量为 0
                    /**
                     * 当 boneCount 为 0 时，对骨骼 uniform 数组进行初始化以避免 Adreno GPU 崩溃
                     */
                    // When boneCount is 0, do an initialization for the bones uniform array to
                    // avoid crash on adreno gpu.
                    if (UTILS_UNLIKELY(driver.isWorkaroundNeeded(  // 如果需要 Adreno 工作区
                            Workaround::ADRENO_UNIFORM_ARRAY_CRASH))) {
                        auto *initBones = driver.allocatePod<PerRenderableBoneUib::BoneData>(1);  // 分配一个骨骼数据
                        std::uninitialized_fill_n(initBones, 1, FSkinningBuffer::makeBone({}));  // 填充为单位骨骼
                        driver.updateBufferObject(bones.handle, {  // 更新缓冲区对象
                                initBones, sizeof(PerRenderableBoneUib::BoneData) }, 0);
                    }
                }
            }
        }

        /**
         * 创建并初始化所有需要的变形目标
         * 
         * 这是必需的，以避免在热循环中的分支。
         */
        // Create and initialize all needed MorphTargets.
        // It's required to avoid branches in hot loops.
        FMorphTargetBuffer* morphTargetBuffer = builder->mMorphTargetBuffer;  // 获取变形目标缓冲区
        if (morphTargetBuffer == nullptr) {  // 如果为空
            morphTargetBuffer = mEngine.getDummyMorphTargetBuffer();  // 使用虚拟变形目标缓冲区
        }

        /**
         * 如果启用了蒙皮或变形之一，总是创建蒙皮和变形资源
         * 
         * 因为着色器总是处理两者。参见 Variant::SKINNING_OR_MORPHING。
         */
        // Always create skinning and morphing resources if one of them is enabled because
        // the shader always handles both. See Variant::SKINNING_OR_MORPHING.
        if (UTILS_UNLIKELY(boneCount > 0 || targetCount > 0)) {  // 如果有骨骼或变形目标

            Bones& bones = manager[ci].bones;  // 获取骨骼引用
            /**
             * 创建骨骼索引和权重纹理句柄
             */
            bones.handleTexture = FSkinningBuffer::createIndicesAndWeightsHandle(
                    engine, builder->mBoneIndicesAndWeightsCount);  // 创建纹理句柄
            if (builder->mBoneIndicesAndWeightsCount > 0) {  // 如果有骨骼索引和权重数据
                /**
                 * 设置骨骼索引和权重数据
                 */
                FSkinningBuffer::setIndicesAndWeightsData(engine,
                        bones.handleTexture,  // 纹理句柄
                        builder->mBoneIndicesAndWeights,
                        builder->mBoneIndicesAndWeightsCount);
            }

            // Instead of using a UBO per primitive, we could also have a single UBO for all primitives
            // and use bindUniformBufferRange which might be more efficient.
            MorphWeights& morphWeights = manager[ci].morphWeights;
            morphWeights = MorphWeights {
                .handle = driver.createBufferObject(
                        sizeof(PerRenderableMorphingUib),
                        BufferObjectBinding::UNIFORM,
                        BufferUsage::DYNAMIC),
                .count = targetCount };

            Slice<FRenderPrimitive> primitives = mManager[ci].primitives;
            mManager[ci].morphTargetBuffer = morphTargetBuffer;
            if (builder->mMorphTargetBuffer) {
                for (size_t i = 0; i < entryCount; ++i) {
                    const auto& morphing = builder->mEntries[i].morphing;
                    primitives[i].setMorphingBufferOffset(morphing.offset);
                }
            }

            // When targetCount equal 0, boneCount>0 in this case, do an initialization for the
            // morphWeights uniform array to avoid crash on adreno gpu.
            if (UTILS_UNLIKELY(targetCount == 0 &&
                    driver.isWorkaroundNeeded(Workaround::ADRENO_UNIFORM_ARRAY_CRASH))) {
                float initWeights[1] = { 0 };
                setMorphWeights(ci, initWeights, 1, 0);
            }
        }
    }
    engine.flushIfNeeded();
}

// this destroys a single component from an entity
void FRenderableManager::destroy(Entity const e) noexcept {
    Instance const ci = getInstance(e);
    if (ci) {
        destroyComponent(ci);
        mManager.removeComponent(e);
    }
}

// this destroys all components in this manager
void FRenderableManager::terminate() noexcept {
    auto& manager = mManager;
    if (!manager.empty()) {
        DLOG(INFO) << "cleaning up " << manager.getComponentCount()
                   << " leaked Renderable components";
        while (!manager.empty()) {
            Instance const ci = manager.end() - 1;
            destroyComponent(ci);
            manager.removeComponent(manager.getEntity(ci));
        }
    }
    mHwRenderPrimitiveFactory.terminate(mEngine.getDriverApi());
}

void FRenderableManager::gc(EntityManager& em) noexcept {
    mManager.gc(em, [this](Entity const e) {
        destroy(e);
    });
}

// This is basically a Renderable's destructor.
void FRenderableManager::destroyComponent(Instance const ci) noexcept {
    auto& manager = mManager;
    FEngine& engine = mEngine;

    FEngine::DriverApi& driver = engine.getDriverApi();

    // See create(RenderableManager::Builder&, Entity)
    destroyComponentPrimitives(mHwRenderPrimitiveFactory, driver, manager[ci].primitives);

    // destroy the per-renderable descriptor set if we have one
    DescriptorSet& descriptorSet = manager[ci].descriptorSet;
    descriptorSet.terminate(driver);

    // destroy the bones structures if any
    Bones const& bones = manager[ci].bones;
    if (bones.handle && !bones.skinningBufferMode) {
        // when not in skinningBufferMode, we now the handle, so we destroy it
        driver.destroyBufferObject(bones.handle);
    }
    if (bones.handleTexture) {
        driver.destroyTexture(bones.handleTexture);
    }

    // destroy the weights structures if any
    MorphWeights const& morphWeights = manager[ci].morphWeights;
    if (morphWeights.handle) {
        driver.destroyBufferObject(morphWeights.handle);
    }
}

void FRenderableManager::destroyComponentPrimitives(
        HwRenderPrimitiveFactory& factory, DriverApi& driver,
        Slice<FRenderPrimitive> primitives) noexcept {
    for (auto& primitive : primitives) {
        primitive.terminate(factory, driver);
    }
    delete[] primitives.data();
}

void FRenderableManager::setMaterialInstanceAt(Instance const instance, uint8_t const level,
        size_t const primitiveIndex, FMaterialInstance const* mi) {
    assert_invariant(mi);
    if (instance) {
        Slice<FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size() && mi) {
            FMaterial const* material = mi->getMaterial();

            // we want a feature level violation to be a hard error (exception if enabled, or crash)
            FILAMENT_CHECK_PRECONDITION(mEngine.hasFeatureLevel(material->getFeatureLevel()))
                    << "Material \"" << material->getName().c_str_safe() << "\" has feature level "
                    << (uint8_t)material->getFeatureLevel()
                    << " which is not supported by this Engine";

            primitives[primitiveIndex].setMaterialInstance(mi);
            AttributeBitset const required = material->getRequiredAttributes();
            AttributeBitset const declared = primitives[primitiveIndex].getEnabledAttributes();
            // Print the warning only when the handle is available. Otherwise this may end up
            // emitting many invalid warnings as the `declared` bitset is not populated yet.
            bool const isPrimitiveInitialized = !!primitives[primitiveIndex].getHwHandle();
            if (UTILS_UNLIKELY(isPrimitiveInitialized && (declared & required) != required)) {
                LOG(WARNING) << "[instance=" << instance.asValue() << ", primitive @ "
                             << primitiveIndex << "] missing required attributes (" << required
                             << "), declared=" << declared;
            }
        }
    }
}

void FRenderableManager::clearMaterialInstanceAt(Instance instance, uint8_t level,
        size_t primitiveIndex) {
    if (instance) {
        Slice<FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            primitives[primitiveIndex].setMaterialInstance(nullptr);
        }
    }
}

MaterialInstance* FRenderableManager::getMaterialInstanceAt(
        Instance const instance, uint8_t const level, size_t const primitiveIndex) const noexcept {
    if (instance) {
        Slice<const FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            // We store the material instance as const because we don't want to change it internally
            // but when the user queries it, we want to allow them to call setParameter()
            return const_cast<FMaterialInstance*>(primitives[primitiveIndex].getMaterialInstance());
        }
    }
    return nullptr;
}

void FRenderableManager::setBlendOrderAt(Instance const instance, uint8_t const level,
        size_t const primitiveIndex, uint16_t const order) noexcept {
    if (instance) {
        Slice<FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            primitives[primitiveIndex].setBlendOrder(order);
        }
    }
}

void FRenderableManager::setGlobalBlendOrderEnabledAt(Instance const instance, uint8_t const level,
        size_t const primitiveIndex, bool const enabled) noexcept {
    if (instance) {
        Slice<FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            primitives[primitiveIndex].setGlobalBlendOrderEnabled(enabled);
        }
    }
}

AttributeBitset FRenderableManager::getEnabledAttributesAt(
        Instance const instance, uint8_t const level, size_t const primitiveIndex) const noexcept {
    if (instance) {
        Slice<const FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            return primitives[primitiveIndex].getEnabledAttributes();
        }
    }
    return AttributeBitset{};
}

void FRenderableManager::setGeometryAt(Instance const instance, uint8_t const level, size_t const primitiveIndex,
        PrimitiveType const type, FVertexBuffer* vertices, FIndexBuffer* indices,
        size_t const offset, size_t const count) noexcept {
    if (instance) {
        Slice<FRenderPrimitive> primitives = getRenderPrimitives(instance, level);
        if (primitiveIndex < primitives.size()) {
            primitives[primitiveIndex].set(mHwRenderPrimitiveFactory, mEngine.getDriverApi(),
                    type, vertices, indices, offset, count);
        }
    }
}

void FRenderableManager::setBones(Instance const ci,
        Bone const* UTILS_RESTRICT transforms, size_t boneCount, size_t const offset) {
    if (ci) {
        Bones const& bones = mManager[ci].bones;

        FILAMENT_CHECK_PRECONDITION(!bones.skinningBufferMode)
                << "Disable skinning buffer mode to use this API";

        assert_invariant(bones.handle && offset + boneCount <= bones.count);
        if (bones.handle) {
            boneCount = std::min(boneCount, bones.count - offset);
            FSkinningBuffer::setBones(mEngine, bones.handle, transforms, boneCount, offset);
        }
    }
}

void FRenderableManager::setBones(Instance const ci,
        mat4f const* UTILS_RESTRICT transforms, size_t boneCount, size_t const offset) {
    if (ci) {
        Bones const& bones = mManager[ci].bones;

        FILAMENT_CHECK_PRECONDITION(!bones.skinningBufferMode)
                << "Disable skinning buffer mode to use this API";

        assert_invariant(bones.handle && offset + boneCount <= bones.count);
        if (bones.handle) {
            boneCount = std::min(boneCount, bones.count - offset);
            FSkinningBuffer::setBones(mEngine, bones.handle, transforms, boneCount, offset);
        }
    }
}

void FRenderableManager::setSkinningBuffer(Instance const ci,
        FSkinningBuffer* skinningBuffer, size_t count, size_t const offset) {

    Bones& bones = mManager[ci].bones;

    FILAMENT_CHECK_PRECONDITION(bones.skinningBufferMode)
            << "Enable skinning buffer mode to use this API";

    FILAMENT_CHECK_PRECONDITION(count <= CONFIG_MAX_BONE_COUNT)
            << "SkinningBuffer larger than 256 (count=" << count << ")";

    // According to the OpenGL ES 3.2 specification in 7.6.3 Uniform
    // Buffer Object Bindings:
    //
    //     the uniform block must be populated with a buffer object with a size no smaller
    //     than the minimum required size of the uniform block (the value of
    //     UNIFORM_BLOCK_DATA_SIZE).
    //

    count = CONFIG_MAX_BONE_COUNT;

    FILAMENT_CHECK_PRECONDITION(count + offset <= skinningBuffer->getBoneCount())
            << "SkinningBuffer overflow (size=" << skinningBuffer->getBoneCount()
            << ", count=" << count << ", offset=" << offset << ")";

    bones.handle = skinningBuffer->getHwHandle();
    bones.count = uint16_t(count);
    bones.offset = uint16_t(offset);
}

static void updateMorphWeights(FEngine& engine, Handle<HwBufferObject> handle,
        float const* weights, size_t const count, size_t const offset) noexcept {
    auto& driver = engine.getDriverApi();
    auto size = sizeof(float4) * count;
    auto* UTILS_RESTRICT out = (float4*)driver.allocate(size);
    std::transform(weights, weights + count, out,
            [](float const value) { return float4(value, 0, 0, 0); });
    driver.updateBufferObject(handle, { out, size }, sizeof(float4) * offset);
}

void FRenderableManager::setMorphWeights(Instance const instance, float const* weights,
        size_t const count, size_t const offset) {
    if (instance) {
        FILAMENT_CHECK_PRECONDITION(count + offset <= CONFIG_MAX_MORPH_TARGET_COUNT)
                << "Only " << CONFIG_MAX_MORPH_TARGET_COUNT
                << " morph targets are supported (count=" << count << ", offset=" << offset << ")";

        MorphWeights const& morphWeights = mManager[instance].morphWeights;
        if (morphWeights.handle) {
            updateMorphWeights(mEngine, morphWeights.handle, weights, count, offset);
        }
    }
}

void FRenderableManager::setMorphTargetBufferOffsetAt(Instance const instance, uint8_t level,
        size_t const primitiveIndex,
        size_t const offset) {
    if (instance) {
        assert_invariant(mManager[instance].morphTargetBuffer);
        Slice<FRenderPrimitive> primitives = mManager[instance].primitives;
        if (primitiveIndex < primitives.size()) {
            primitives[primitiveIndex].setMorphingBufferOffset(offset);
        }
    }
}

MorphTargetBuffer* FRenderableManager::getMorphTargetBuffer(Instance const instance) const noexcept {
    if (instance) {
        return mManager[instance].morphTargetBuffer;
    }
    return nullptr;
}

size_t FRenderableManager::getMorphTargetCount(Instance const instance) const noexcept {
    if (instance) {
        const MorphWeights& morphWeights = mManager[instance].morphWeights;
        return morphWeights.count;
    }
    return 0;
}

void FRenderableManager::setLightChannel(Instance const ci, unsigned int const channel, bool const enable) noexcept {
    if (ci) {
        if (channel < 8) {
            const uint8_t mask = 1u << channel;
            mManager[ci].channels &= ~mask;
            mManager[ci].channels |= enable ? mask : 0u;
        }
    }
}

bool FRenderableManager::getLightChannel(Instance const ci, unsigned int const channel) const noexcept {
    if (ci) {
        if (channel < 8) {
            const uint8_t mask = 1u << channel;
            return bool(mManager[ci].channels & mask);
        }
    }
    return false;
}

size_t FRenderableManager::getPrimitiveCount(Instance const instance, uint8_t const level) const noexcept {
    return getRenderPrimitives(instance, level).size();
}


size_t FRenderableManager::getInstanceCount(Instance const instance) const noexcept {
    if (instance) {
        InstancesInfo const& info = mManager[instance].instances;
        return info.count;
    }
    return 0;
}

} // namespace filament
