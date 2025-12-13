/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "details/SkinningBuffer.h"

#include "components/RenderableManager.h"

#include "details/Engine.h"

#include "FilamentAPI-impl.h"

#include <math/half.h>
#include <math/mat4.h>

#include <utils/CString.h>
#include <utils/StaticString.h>

#include <string.h>
#include <stddef.h>
#include <stdint.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 构建器详情结构
 * 
 * 存储蒙皮缓冲区的构建参数。
 */
struct SkinningBuffer::BuilderDetails {
    /**
     * 骨骼数量
     * 
     * 要存储的骨骼变换矩阵数量。
     */
    uint32_t mBoneCount = 0;
    
    /**
     * 是否初始化
     * 
     * 如果为 true，所有骨骼将初始化为单位变换矩阵。
     */
    bool mInitialize = false;
};

/**
 * 构建器类型别名
 */
using BuilderType = SkinningBuffer;

/**
 * 构建器默认构造函数
 */
BuilderType::Builder::Builder() noexcept = default;

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
 * 设置骨骼数量
 * 
 * 设置要存储的骨骼变换矩阵数量。
 * 
 * @param boneCount 骨骼数量
 * @return 构建器引用（支持链式调用）
 */
SkinningBuffer::Builder& SkinningBuffer::Builder::boneCount(uint32_t const boneCount) noexcept {
    mImpl->mBoneCount = boneCount;  // 设置骨骼数量
    return *this;  // 返回自身引用
}

/**
 * 设置初始化标志
 * 
 * 设置是否在创建时将所有骨骼初始化为单位变换矩阵。
 * 
 * @param initialize 是否初始化（true = 初始化为单位矩阵）
 * @return 构建器引用（支持链式调用）
 */
SkinningBuffer::Builder& SkinningBuffer::Builder::initialize(bool const initialize) noexcept {
    mImpl->mInitialize = initialize;  // 设置初始化标志
    return *this;  // 返回自身引用
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串指针
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
SkinningBuffer::Builder& SkinningBuffer::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 委托给基类
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 静态字符串引用
 * @return 构建器引用（支持链式调用）
 */
SkinningBuffer::Builder& SkinningBuffer::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 委托给基类
}

/**
 * 构建蒙皮缓冲区
 * 
 * 根据构建器参数创建蒙皮缓冲区对象。
 * 
 * @param engine 引擎引用
 * @return 蒙皮缓冲区指针
 */
SkinningBuffer* SkinningBuffer::Builder::build(Engine& engine) {
    return downcast(engine).createSkinningBuffer(*this);  // 委托给引擎创建
}

// ------------------------------------------------------------------------------------------------

/**
 * 构造函数实现
 * 
 * 创建 UBO 用于存储骨骼变换数据。
 */
FSkinningBuffer::FSkinningBuffer(FEngine& engine, const Builder& builder)
        : mBoneCount(builder->mBoneCount) {  // 保存骨骼数量
    FEngine::DriverApi& driver = engine.getDriverApi();

    /**
     * 根据 OpenGL ES 3.2 规范 7.6.3 Uniform Buffer Object Bindings：
     * 
     * uniform 块必须使用大小不小于 uniform 块最小要求大小
     * （UNIFORM_BLOCK_DATA_SIZE 的值）的缓冲区对象填充。
     */
    // According to the OpenGL ES 3.2 specification in 7.6.3 Uniform
    // Buffer Object Bindings:
    //
    //     the uniform block must be populated with a buffer object with a size no smaller
    //     than the minimum required size of the uniform block (the value of
    //     UNIFORM_BLOCK_DATA_SIZE).

    /**
     * 创建 UBO 缓冲区对象
     * 
     * 大小 = 物理骨骼数量 × 每个骨骼数据的大小
     * 物理骨骼数量向上舍入到 CONFIG_MAX_BONE_COUNT 的倍数。
     */
    // TODO: We should also tag the texture created inside createIndicesAndWeightsHandle.
    mHandle = driver.createBufferObject(
            getPhysicalBoneCount(mBoneCount) * sizeof(PerRenderableBoneUib::BoneData),  // 缓冲区大小
            BufferObjectBinding::UNIFORM,  // 绑定类型：Uniform
            BufferUsage::DYNAMIC,  // 使用方式：动态（频繁更新）
            utils::ImmutableCString{ builder.getName() });  // 名称

    /**
     * 如果构建器要求初始化，将所有骨骼设置为单位变换
     */
    if (builder->mInitialize) {
        // initialize the bones to identity (before rounding up)
        /**
         * 分配临时缓冲区并初始化为单位变换
         */
        auto* out = driver.allocatePod<PerRenderableBoneUib::BoneData>(mBoneCount);
        std::uninitialized_fill_n(out, mBoneCount, makeBone({}));  // 使用单位矩阵创建骨骼数据
        /**
         * 更新缓冲区对象
         */
        driver.updateBufferObject(mHandle, {
            out, mBoneCount * sizeof(PerRenderableBoneUib::BoneData) }, 0);
    }
}

/**
 * 终止蒙皮缓冲区
 * 
 * 释放 GPU 资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FSkinningBuffer::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyBufferObject(mHandle);  // 销毁缓冲区对象
}

/**
 * 设置骨骼变换（RenderableManager::Bone 版本）
 * 
 * 更新蒙皮缓冲区中的骨骼变换矩阵。
 * 
 * @param engine 引擎引用
 * @param transforms 骨骼变换数组指针（RenderableManager::Bone 类型）
 * @param count 要更新的骨骼数量
 * @param offset 起始偏移量（骨骼索引）
 */
void FSkinningBuffer::setBones(FEngine& engine,
        RenderableManager::Bone const* transforms, size_t const count, size_t const offset) {
    /**
     * 检查边界：确保不会溢出缓冲区
     */
    FILAMENT_CHECK_PRECONDITION((offset + count) <= mBoneCount)
            << "SkinningBuffer (size=" << (unsigned)mBoneCount
            << ") overflow (boneCount=" << (unsigned)count << ", offset=" << (unsigned)offset
            << ")";

    /**
     * 调用内部方法更新骨骼
     */
    setBones(engine, mHandle, transforms, count, offset);
}

/**
 * 设置骨骼变换（mat4f 版本）
 * 
 * 更新蒙皮缓冲区中的骨骼变换矩阵。
 * 
 * @param engine 引擎引用
 * @param transforms 骨骼变换矩阵数组指针（mat4f 类型）
 * @param count 要更新的骨骼数量
 * @param offset 起始偏移量（骨骼索引）
 */
void FSkinningBuffer::setBones(FEngine& engine,
        mat4f const* transforms, size_t const count, size_t const offset) {
    /**
     * 检查边界：确保不会溢出缓冲区
     */
    FILAMENT_CHECK_PRECONDITION((offset + count) <= mBoneCount)
            << "SkinningBuffer (size=" << (unsigned)mBoneCount
            << ") overflow (boneCount=" << (unsigned)count << ", offset=" << (unsigned)offset
            << ")";

    /**
     * 调用内部方法更新骨骼
     */
    setBones(engine, mHandle, transforms, count, offset);
}

/**
 * 打包半精度浮点数对（未使用）
 * 
 * 将两个 half 值打包成一个 uint32_t。
 * 
 * @param v 半精度浮点数对
 * @return 打包后的 32 位整数
 */
UTILS_UNUSED
static uint32_t packHalf2x16(half2 v) noexcept {
    uint32_t lo = getBits(v[0]);  // 获取低 16 位的位表示
    uint32_t hi = getBits(v[1]);  // 获取高 16 位的位表示
    return (hi << 16) | lo;  // 组合成 32 位整数
}

/**
 * 设置骨骼变换（内部方法，RenderableManager::Bone 版本）
 * 
 * 将 RenderableManager::Bone 格式的骨骼变换转换为着色器格式并更新缓冲区。
 * 
 * 实现细节：
 * - 从单位四元数和平移向量构建变换矩阵
 * - 变换矩阵以行主序存储，最后一行不存储（总是 [0, 0, 0, 1]）
 * 
 * @param engine 引擎引用
 * @param handle 缓冲区对象句柄
 * @param transforms 骨骼变换数组指针（RenderableManager::Bone 类型）
 * @param boneCount 要更新的骨骼数量
 * @param offset 起始偏移量（骨骼索引）
 */
void FSkinningBuffer::setBones(FEngine& engine, Handle<HwBufferObject> handle,
        RenderableManager::Bone const* transforms, size_t const boneCount, size_t const offset) noexcept {
    auto& driverApi = engine.getDriverApi();  // 获取驱动 API
    /**
     * 分配临时缓冲区用于存储转换后的骨骼数据
     */
    auto* UTILS_RESTRICT out = driverApi.allocatePod<PerRenderableBoneUib::BoneData>(boneCount);
    
    /**
     * 转换每个骨骼的变换
     */
    for (size_t i = 0, c = boneCount; i < c; ++i) {
        /**
         * 从单位四元数构建变换矩阵
         * 
         * 变换矩阵以行主序存储，最后一行不存储。
         */
        // the transform is stored in row-major, last row is not stored.
        mat4f transform(transforms[i].unitQuaternion);  // 从四元数构建旋转部分
        transform[3] = float4{ transforms[i].translation, 1.0f };  // 设置平移部分
        out[i] = makeBone(transform);  // 转换为着色器格式
    }
    
    /**
     * 更新缓冲区对象
     */
    driverApi.updateBufferObject(handle, {
                    out, boneCount * sizeof(PerRenderableBoneUib::BoneData) },
            offset * sizeof(PerRenderableBoneUib::BoneData));  // 从指定偏移量开始更新
}

/**
 * 创建骨骼数据
 * 
 * 将变换矩阵转换为着色器使用的骨骼数据格式。
 * 
 * 转换过程：
 * 1. 计算上 3x3 矩阵的余子式（用于法线变换）
 * 2. 转置矩阵（行主序转换）
 * 3. 只存储前 3 行（第 4 行总是 [0, 0, 0, 1]）
 * 
 * @param transform 变换矩阵
 * @return 骨骼数据
 */
PerRenderableBoneUib::BoneData FSkinningBuffer::makeBone(mat4f transform) noexcept {
    /**
     * 计算上 3x3 矩阵的余子式
     * 
     * 余子式用于在着色器中计算法线变换，处理非均匀缩放。
     */
    const mat3f cofactors = cof(transform.upperLeft());
    
    /**
     * 转置矩阵（行主序转换）
     * 
     * OpenGL 使用列主序，但我们的数据是行主序，所以需要转置。
     */
    transform = transpose(transform); // row-major conversion
    
    /**
     * 构建骨骼数据
     * 
     * - transform: 变换矩阵的前 3 行（转置后）
     * - cof0: 余子式的第一行（用于法线变换）
     * - cof1x: 余子式第二行的 x 分量（用于法线变换）
     */
    return {
            .transform = {
                    transform[0],  // 第一行
                    transform[1],  // 第二行
                    transform[2]   // 第三行（第四行总是 [0, 0, 0, 1]）
            },
            .cof0 = cofactors[0],      // 余子式第一行
            .cof1x = cofactors[1].x   // 余子式第二行的 x 分量
    };
}

/**
 * 设置骨骼变换（内部方法，mat4f 版本）
 * 
 * 将 mat4f 格式的骨骼变换转换为着色器格式并更新缓冲区。
 * 
 * @param engine 引擎引用
 * @param handle 缓冲区对象句柄
 * @param transforms 骨骼变换矩阵数组指针（mat4f 类型）
 * @param boneCount 要更新的骨骼数量
 * @param offset 起始偏移量（骨骼索引）
 */
void FSkinningBuffer::setBones(FEngine& engine, Handle<HwBufferObject> handle,
        mat4f const* transforms, size_t const boneCount, size_t const offset) noexcept {
    auto& driverApi = engine.getDriverApi();  // 获取驱动 API
    /**
     * 分配临时缓冲区用于存储转换后的骨骼数据
     */
    auto* UTILS_RESTRICT out = driverApi.allocatePod<PerRenderableBoneUib::BoneData>(boneCount);
    
    /**
     * 转换每个骨骼的变换
     */
    for (size_t i = 0, c = boneCount; i < c; ++i) {
        /**
         * 变换矩阵以行主序存储，最后一行不存储。
         */
        // the transform is stored in row-major, last row is not stored.
        out[i] = makeBone(transforms[i]);  // 转换为着色器格式
    }
    
    /**
     * 更新缓冲区对象
     */
    driverApi.updateBufferObject(handle, { out, boneCount * sizeof(PerRenderableBoneUib::BoneData) },
            offset * sizeof(PerRenderableBoneUib::BoneData));  // 从指定偏移量开始更新
}

/**
 * 最大蒙皮缓冲区宽度
 * 
 * 此值受 ES3.0 限制，ES3.0 仅保证 2048。
 * 当更改此值时，必须同时更改 surface_getters.vs 中的 MAX_SKINNING_BUFFER_WIDTH。
 */
// This value is limited by ES3.0, ES3.0 only guarantees 2048.
// When you change this value, you must change MAX_SKINNING_BUFFER_WIDTH at surface_getters.vs
constexpr size_t MAX_SKINNING_BUFFER_WIDTH = 2048;

/**
 * 获取蒙皮缓冲区宽度
 * 
 * 计算用于存储骨骼索引和权重的纹理宽度。
 * 
 * @param pairCount 对数量（骨骼索引和权重的对数）
 * @return 纹理宽度（限制在 1 到 MAX_SKINNING_BUFFER_WIDTH 之间）
 */
static inline size_t getSkinningBufferWidth(size_t const pairCount) noexcept {
    return std::clamp(pairCount, size_t(1), MAX_SKINNING_BUFFER_WIDTH);  // 限制在有效范围内
}

/**
 * 获取蒙皮缓冲区高度
 * 
 * 计算用于存储骨骼索引和权重的纹理高度。
 * 
 * @param pairCount 对数量（骨骼索引和权重的对数）
 * @return 纹理高度（至少为 1）
 */
static inline size_t getSkinningBufferHeight(size_t const pairCount) noexcept {
    /**
     * 计算需要多少行才能存储所有对
     * 
     * 公式：(pairCount + MAX_SKINNING_BUFFER_WIDTH - 1) / MAX_SKINNING_BUFFER_WIDTH
     * 这等价于向上取整除法。
     */
    return std::max(size_t(1),
            (pairCount + MAX_SKINNING_BUFFER_WIDTH - 1) / MAX_SKINNING_BUFFER_WIDTH);
}

/**
 * 获取蒙皮缓冲区大小
 * 
 * 计算用于存储骨骼索引和权重的纹理数据大小。
 * 
 * @param pairCount 对数量（骨骼索引和权重的对数）
 * @return 数据大小（字节）
 */
inline size_t getSkinningBufferSize(size_t const pairCount) noexcept {
    const size_t stride = getSkinningBufferWidth(pairCount);  // 纹理宽度
    const size_t height = getSkinningBufferHeight(pairCount);  // 纹理高度
    /**
     * 计算数据大小
     * 
     * 格式：RG（两个浮点数通道）
     * 类型：FLOAT
     */
    return Texture::PixelBufferDescriptor::computeDataSize(
            Texture::PixelBufferDescriptor::PixelDataFormat::RG,  // RG 格式
            Texture::PixelBufferDescriptor::PixelDataType::FLOAT,  // 浮点类型
            stride, height, 1);  // 宽度、高度、深度
}

UTILS_NOINLINE
void updateDataAt(DriverApi& driver,
        Handle<HwTexture> handle, PixelDataFormat const format, PixelDataType const type,
        const utils::FixedCapacityVector<float2>& pairs,
        size_t const count) {

    size_t const elementSize = sizeof(float2);
    size_t const size = getSkinningBufferSize(count);
    auto* out = (float2*)malloc(size);
    memcpy(out, pairs.begin(), size);

    size_t const textureWidth = getSkinningBufferWidth(count);
    size_t const lineCount = count / textureWidth;
    size_t const lastLineCount = count % textureWidth;

    // 'out' buffer is going to be used up to 2 times, so for simplicity we use a shared_buffer
    // to manage its lifetime. One side effect of this is that the callbacks below will allocate
    // a small object on the heap. (inspired by MorphTargetBuffered)
    std::shared_ptr<void> const allocation((void*)out, free);

    if (lineCount) {
        // update the full-width lines if any
        driver.update3DImage(handle, 0, 0, 0, 0,
                textureWidth, lineCount, 1,
                PixelBufferDescriptor::make(
                        out, textureWidth * lineCount * elementSize,
                        format, type, [allocation](void const*, size_t) {}
                ));
        out += lineCount * textureWidth;
    }

    if (lastLineCount) {
        // update the last partial line if any
        driver.update3DImage(handle, 0, 0, lineCount, 0,
                lastLineCount, 1, 1,
                PixelBufferDescriptor::make(
                        out, lastLineCount * elementSize,
                        format, type, [allocation](void const*, size_t) {}
                ));
    }
}

TextureHandle FSkinningBuffer::createIndicesAndWeightsHandle(
        FEngine& engine, size_t const count) {
    FEngine::DriverApi& driver = engine.getDriverApi();
    // create a texture for skinning pairs data (bone index and weight)
    return driver.createTexture(SamplerType::SAMPLER_2D, 1,
            TextureFormat::RG32F, 1,
            getSkinningBufferWidth(count),
            getSkinningBufferHeight(count), 1,
            TextureUsage::DEFAULT);
}

void FSkinningBuffer::setIndicesAndWeightsData(FEngine& engine,
        Handle<HwTexture> textureHandle,
        const utils::FixedCapacityVector<float2>& pairs, size_t const count) {

    FEngine::DriverApi& driver = engine.getDriverApi();
    updateDataAt(driver, textureHandle,
            Texture::Format::RG, Texture::Type::FLOAT,
            pairs, count);
}

} // namespace filament

