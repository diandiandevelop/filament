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

#include "details/InstanceBuffer.h"

#include "details/Engine.h"

#include "FilamentAPI-impl.h"

#include <private/filament/UibStructs.h>

#include <filament/FilamentAPI.h>
#include <filament/Engine.h>
#include <filament/InstanceBuffer.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>

#include <math/mat3.h>
#include <math/mat4.h>

#include <utility>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstddef>

namespace filament {

using namespace backend;

/**
 * 构建器详情结构
 * 
 * 存储构建器的内部状态。
 */
struct InstanceBuffer::BuilderDetails {
    size_t mInstanceCount = 0;  // 实例数量
    math::mat4f const* mLocalTransforms = nullptr;  // 本地变换矩阵数组指针
};

using BuilderType = InstanceBuffer;
/**
 * 构造函数
 * 
 * @param instanceCount 实例数量
 */
BuilderType::Builder::Builder(size_t const instanceCount) noexcept {
    mImpl->mInstanceCount = instanceCount;
}

/**
 * 构建器析构函数
 */
BuilderType::Builder::~Builder() noexcept = default;

/**
 * 构建器拷贝构造函数
 */
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;

/**
 * 构建器移动构造函数
 */
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;

/**
 * 构建器拷贝赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;

/**
 * 构建器移动赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置本地变换矩阵
 * 
 * 设置实例的本地变换矩阵数组。
 * 这些变换矩阵将在渲染时与根变换矩阵相乘，得到每个实例的世界变换矩阵。
 * 
 * @param localTransforms 本地变换矩阵数组指针（必须包含至少 instanceCount 个矩阵）
 * @return 构建器引用（支持链式调用）
 */
InstanceBuffer::Builder& InstanceBuffer::Builder::localTransforms(
        math::mat4f const* localTransforms) noexcept {
    mImpl->mLocalTransforms = localTransforms;  // 设置变换矩阵数组指针
    return *this;  // 返回自身引用
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串指针
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
InstanceBuffer::Builder& InstanceBuffer::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 委托给基类
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 静态字符串引用
 * @return 构建器引用（支持链式调用）
 */
InstanceBuffer::Builder& InstanceBuffer::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 委托给基类
}

/**
 * 构建实例缓冲区
 * 
 * 根据构建器参数创建实例缓冲区对象。
 * 
 * 前置条件检查：
 * - 实例数量必须 >= 1
 * - 实例数量不能超过引擎的最大自动实例数（当提供变换矩阵时）
 * 
 * @param engine 引擎引用
 * @return 实例缓冲区指针
 */
InstanceBuffer* InstanceBuffer::Builder::build(Engine& engine) const {
    /**
     * 检查实例数量是否有效
     */
    FILAMENT_CHECK_PRECONDITION(mImpl->mInstanceCount >= 1) << "instanceCount must be >= 1.";
    
    /**
     * 检查实例数量是否超过引擎限制
     * 
     * 当提供变换矩阵时，实例数量受引擎的最大自动实例数限制。
     */
    FILAMENT_CHECK_PRECONDITION(mImpl->mInstanceCount <= engine.getMaxAutomaticInstances())
            << "instanceCount is " << mImpl->mInstanceCount
            << ", but instance count is limited to Engine::getMaxAutomaticInstances() ("
            << engine.getMaxAutomaticInstances() << ") instances when supplying transforms.";
    
    /**
     * 委托给引擎创建实例缓冲区
     */
    return downcast(engine).createInstanceBuffer(*this);
}

// ------------------------------------------------------------------------------------------------

/**
 * 构造函数实现
 * 
 * 初始化实例缓冲区，分配本地变换矩阵数组。
 */
FInstanceBuffer::FInstanceBuffer(FEngine&, const Builder& builder)
    : mName(builder.getName()) {  // 保存名称
    mInstanceCount = builder->mInstanceCount;  // 保存实例数量

    /**
     * 预分配并初始化变换矩阵数组
     * 
     * resize() 会将所有变换矩阵初始化为单位矩阵。
     */
    mLocalTransforms.reserve(mInstanceCount);  // 预分配空间
    mLocalTransforms.resize(mInstanceCount);    // this will initialize all transforms to identity

    /**
     * 如果构建器提供了初始变换矩阵，则复制它们
     */
    if (builder->mLocalTransforms) {
        memcpy(mLocalTransforms.data(), builder->mLocalTransforms,
                sizeof(math::mat4f) * mInstanceCount);
    }
}

/**
 * 终止实例缓冲区
 * 
 * 重置索引，准备重用。
 */
void FInstanceBuffer::terminate(FEngine&) {
    mIndex = 0;  // 重置索引
}

FInstanceBuffer::~FInstanceBuffer() noexcept = default;

/**
 * 设置本地变换矩阵
 * 
 * 批量更新指定范围的实例变换矩阵。
 */
void FInstanceBuffer::setLocalTransforms(
        math::mat4f const* localTransforms, size_t const count, size_t const offset) {
    /**
     * 检查边界
     */
    FILAMENT_CHECK_PRECONDITION(offset + count <= mInstanceCount)
            << "setLocalTransforms overflow. InstanceBuffer has only " << mInstanceCount
            << " instances, but trying to set " << count 
            << " transforms at offset " << offset << ".";
    /**
     * 复制变换矩阵数据
     */
    memcpy(mLocalTransforms.data() + offset, localTransforms, sizeof(math::mat4f) * count);
}

/**
 * 获取指定实例的本地变换矩阵
 * 
 * @param index 实例索引
 * @return 本地变换矩阵常量引用
 */
math::mat4f const& FInstanceBuffer::getLocalTransform(size_t index) const noexcept {
    /**
     * 检查边界
     */
    FILAMENT_CHECK_PRECONDITION(index < mInstanceCount)
            << "getLocalTransform overflow: 'index (" << index
            << ") must be < getInstanceCount() ("<< mInstanceCount << ").";
    return mLocalTransforms[index];
}

/**
 * 准备渲染数据
 * 
 * 计算每个实例的世界变换矩阵和法线变换矩阵，并写入缓冲区。
 */
void FInstanceBuffer::prepare(
            PerRenderableData* const UTILS_RESTRICT buffer, uint32_t const index, uint32_t const count,
            math::mat4f const& rootTransform, PerRenderableData const& ubo) {

    /**
     * 断言检查（应该有前置条件检查，所以这个断言不应该触发）
     */
    // there is a precondition check for this, so this assert really should never trigger
    assert_invariant(count <= mInstanceCount);

    /**
     * 为每个实例计算世界变换
     */
    for (size_t i = 0, c = count; i < c; i++) {
        /**
         * 计算世界变换矩阵：世界 = 根变换 × 本地变换
         */
        math::mat4f const model = rootTransform * mLocalTransforms[i];
        
        /**
         * 计算法线变换矩阵
         * 
         * 法线变换需要特殊处理，因为法线是协变向量。
         * 使用上 3x3 矩阵的逆转置。
         */
        math::mat3f const m = math::mat3f::getTransformForNormals(model.upperLeft());
        
        /**
         * 复制 UBO 数据并设置变换矩阵
         */
        buffer[index + i] = ubo;  // 复制基础数据
        buffer[index + i].worldFromModelMatrix = model;  // 世界变换矩阵
        buffer[index + i].worldFromModelNormalMatrix = math::prescaleForNormals(m);  // 法线变换矩阵（处理非均匀缩放）
    }
    mIndex = index;  // 保存当前索引
}

} // namespace filament

