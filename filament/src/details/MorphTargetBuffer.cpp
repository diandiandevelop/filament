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

#include "details/MorphTargetBuffer.h"

#include <private/filament/SibStructs.h>

#include <details/Engine.h>

#include "FilamentAPI-impl.h"

#include <math/mat4.h>
#include <math/norm.h>

#include <utils/CString.h>
#include <utils/StaticString.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 构建器详情结构
 * 
 * 存储构建器的内部状态。
 */
struct MorphTargetBuffer::BuilderDetails {
    size_t mVertexCount = 0;  // 顶点数量
    size_t mCount = 0;  // 变形目标数量
};

using BuilderType = MorphTargetBuffer;
BuilderType::Builder::Builder() noexcept = default;
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置顶点数量
 * 
 * @param vertexCount 顶点数量
 * @return 构建器引用
 */
MorphTargetBuffer::Builder& MorphTargetBuffer::Builder::vertexCount(size_t const vertexCount) noexcept {
    mImpl->mVertexCount = vertexCount;
    return *this;
}

/**
 * 设置变形目标数量
 * 
 * @param count 变形目标数量
 * @return 构建器引用
 */
MorphTargetBuffer::Builder& MorphTargetBuffer::Builder::count(size_t const count) noexcept {
    mImpl->mCount = count;
    return *this;
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串指针
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
MorphTargetBuffer::Builder& MorphTargetBuffer::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 委托给基类
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 静态字符串引用
 * @return 构建器引用（支持链式调用）
 */
MorphTargetBuffer::Builder& MorphTargetBuffer::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 委托给基类
}

/**
 * 构建变形目标缓冲区
 * 
 * 根据构建器参数创建变形目标缓冲区对象。
 * 
 * @param engine 引擎引用
 * @return 变形目标缓冲区指针
 */
MorphTargetBuffer* MorphTargetBuffer::Builder::build(Engine& engine) {
    return downcast(engine).createMorphTargetBuffer(*this);  // 委托给引擎创建
}

// ------------------------------------------------------------------------------------------------

/**
 * 最大变形目标缓冲区宽度
 * 
 * 此值受 ES3.0 限制，ES3.0 仅保证 2048。
 * 更改此值时，必须同时更改 surface_getters.vs 中的 MAX_MORPH_TARGET_BUFFER_WIDTH。
 */
// This value is limited by ES3.0, ES3.0 only guarantees 2048.
// When you change this value, you must change MAX_MORPH_TARGET_BUFFER_WIDTH at surface_getters.vs
constexpr size_t MAX_MORPH_TARGET_BUFFER_WIDTH = 2048;

/**
 * 获取纹理宽度
 * 
 * @param vertexCount 顶点数量
 * @return 纹理宽度（不超过最大值）
 */
static inline size_t getWidth(size_t const vertexCount) noexcept {
    return std::min(vertexCount, MAX_MORPH_TARGET_BUFFER_WIDTH);
}

/**
 * 获取纹理高度
 * 
 * 根据顶点数量计算所需的纹理高度。
 * 
 * @param vertexCount 顶点数量
 * @return 纹理高度
 */
static inline size_t getHeight(size_t const vertexCount) noexcept {
    return (vertexCount + MAX_MORPH_TARGET_BUFFER_WIDTH) / MAX_MORPH_TARGET_BUFFER_WIDTH;
}

/**
 * 获取指定属性的数据大小（模板函数）
 * 
 * @tparam A 顶点属性类型
 * @param vertexCount 顶点数量
 * @return 数据大小（字节）
 */
template<VertexAttribute A>
inline size_t getSize(size_t vertexCount) noexcept;

/**
 * 获取位置属性的数据大小（特化）
 * 
 * @param vertexCount 顶点数量
 * @return 数据大小（字节）
 */
template<>
inline size_t getSize<POSITION>(size_t const vertexCount) noexcept {
    const size_t stride = getWidth(vertexCount);  // 纹理宽度
    const size_t height = getHeight(vertexCount);  // 纹理高度
    return Texture::PixelBufferDescriptor::computeDataSize(
            Texture::PixelBufferDescriptor::PixelDataFormat::RGBA,  // RGBA 格式
            Texture::PixelBufferDescriptor::PixelDataType::FLOAT,  // FLOAT 类型
            stride, height, 1);
}

/**
 * 获取切线属性的数据大小（特化）
 * 
 * @param vertexCount 顶点数量
 * @return 数据大小（字节）
 */
template<>
inline size_t getSize<TANGENTS>(size_t const vertexCount) noexcept {
    const size_t stride = getWidth(vertexCount);  // 纹理宽度
    const size_t height = getHeight(vertexCount);  // 纹理高度
    return Texture::PixelBufferDescriptor::computeDataSize(
            Texture::PixelBufferDescriptor::PixelDataFormat::RGBA_INTEGER,  // RGBA_INTEGER 格式
            Texture::PixelBufferDescriptor::PixelDataType::SHORT,  // SHORT 类型
            stride, height, 1);
}

/**
 * 空变形目标构建器构造函数
 * 
 * 初始化构建器，设置默认值（1 个顶点，1 个目标）。
 */
FMorphTargetBuffer::EmptyMorphTargetBuilder::EmptyMorphTargetBuilder() {
    mImpl->mVertexCount = 1;  // 1 个顶点
    mImpl->mCount = 1;  // 1 个变形目标
}

/**
 * 构造函数实现
 * 
 * 创建位置和切线纹理，用于存储变形目标数据。
 */
FMorphTargetBuffer::FMorphTargetBuffer(FEngine& engine, const Builder& builder)
        : mVertexCount(builder->mVertexCount),  // 顶点数量
          mCount(builder->mCount) {  // 变形目标数量

    /**
     * 功能级别 0 不支持变形目标缓冲区
     */
    if (UTILS_UNLIKELY(engine.getSupportedFeatureLevel() <= FeatureLevel::FEATURE_LEVEL_0)) {
        // feature level 0 doesn't support morph target buffers
        return;
    }

    FEngine::DriverApi& driver = engine.getDriverApi();

    /**
     * 创建位置缓冲区纹理（2D 数组纹理）
     * 
     * 使用 RGBA32F 格式存储 float4 位置数据。
     * 纹理尺寸根据顶点数量计算，每个变形目标占用一个数组层。
     */
    // create buffer (here a texture) to store the morphing vertex data
    mPbHandle = driver.createTexture(SamplerType::SAMPLER_2D_ARRAY, 1,  // 2D 数组纹理，1 个 mip 级别
            TextureFormat::RGBA32F, 1,  // RGBA32F 格式
            getWidth(mVertexCount),  // 纹理宽度
            getHeight(mVertexCount),  // 纹理高度
            mCount,  // 数组层数（变形目标数量）
            TextureUsage::DEFAULT,  // 默认使用方式
            utils::ImmutableCString{ builder.getName() });  // 纹理名称

    /**
     * 创建切线缓冲区纹理（2D 数组纹理）
     * 
     * 使用 RGBA16I 格式存储压缩的 short4 切线数据。
     * 纹理尺寸与位置缓冲区相同。
     */
    mTbHandle = driver.createTexture(SamplerType::SAMPLER_2D_ARRAY, 1,  // 2D 数组纹理，1 个 mip 级别
            TextureFormat::RGBA16I, 1,  // RGBA16I 格式（压缩）
            getWidth(mVertexCount),  // 纹理宽度
            getHeight(mVertexCount),  // 纹理高度
            mCount,  // 数组层数（变形目标数量）
            TextureUsage::DEFAULT,  // 默认使用方式
            utils::ImmutableCString{ builder.getName() });  // 纹理名称
}

/**
 * 终止变形目标缓冲区
 * 
 * 销毁位置和切线纹理，释放驱动资源。
 */
void FMorphTargetBuffer::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();
    if (mTbHandle) {
        driver.destroyTexture(mTbHandle);  // 销毁切线纹理
    }
    if (mPbHandle) {
        driver.destroyTexture(mPbHandle);  // 销毁位置纹理
    }
}

/**
 * 设置指定变形目标的位置（float3 版本）
 * 
 * 将 float3 位置数据转换为 float4（添加 w=1.0）并更新到纹理。
 */
void FMorphTargetBuffer::setPositionsAt(FEngine& engine, size_t const targetIndex,
        float3 const* positions, size_t const count, size_t const offset) {
    /**
     * 检查边界：偏移量 + 数量不能超过顶点总数
     */
    FILAMENT_CHECK_PRECONDITION(offset + count <= mVertexCount)
            << "MorphTargetBuffer (size=" << (unsigned)mVertexCount
            << ") overflow (count=" << (unsigned)count << ", offset=" << (unsigned)offset << ")";

    /**
     * 计算所需的数据大小
     */
    auto size = getSize<POSITION>(count);

    /**
     * 检查目标索引有效性
     */
    FILAMENT_CHECK_PRECONDITION(targetIndex < mCount)
            << targetIndex << " target index must be < " << mCount;

    /**
     * 分配临时缓冲区并转换数据
     * 
     * 注意：可以使用内存池代替直接 malloc()。
     */
    // We could use a pool instead of malloc() directly.
    auto* out = (float4*) malloc(size);
    /**
     * 将 float3 转换为 float4（添加 w=1.0）
     */
    std::transform(positions, positions + count, out,
            [](const float3& p) { return float4(p, 1.0f); });

    FEngine::DriverApi& driver = engine.getDriverApi();
    /**
     * 更新纹理数据
     */
    updateDataAt(driver, mPbHandle,
            Texture::Format::RGBA, Texture::Type::FLOAT,
            (char const*)out, sizeof(float4), targetIndex,
            count, offset);
}

/**
 * 设置指定变形目标的位置（float4 版本）
 * 
 * 直接将 float4 位置数据更新到纹理。
 */
void FMorphTargetBuffer::setPositionsAt(FEngine& engine, size_t const targetIndex,
        float4 const* positions, size_t const count, size_t const offset) {
    /**
     * 检查边界
     */
    FILAMENT_CHECK_PRECONDITION(offset + count <= mVertexCount)
            << "MorphTargetBuffer (size=" << mVertexCount
            << ") overflow (count=" << (unsigned)count << ", offset=" << (unsigned)offset << ")";

    /**
     * 计算所需的数据大小
     */
    auto size = getSize<POSITION>(count);

    /**
     * 检查目标索引有效性
     */
    FILAMENT_CHECK_PRECONDITION(targetIndex < mCount)
            << targetIndex << " target index must be < " << mCount;

    /**
     * 分配临时缓冲区并复制数据
     */
    // We could use a pool instead of malloc() directly.
    auto* out = (float4*) malloc(size);
    memcpy(out, positions, sizeof(float4) * count);

    FEngine::DriverApi& driver = engine.getDriverApi();
    /**
     * 更新纹理数据
     */
    updateDataAt(driver, mPbHandle,
            Texture::Format::RGBA, Texture::Type::FLOAT,
            (char const*)out, sizeof(float4), targetIndex,
            count, offset);
}

/**
 * 设置指定变形目标的切线
 * 
 * 将压缩的 short4 切线数据更新到纹理。
 */
void FMorphTargetBuffer::setTangentsAt(FEngine& engine, size_t const targetIndex,
        short4 const* tangents, size_t const count, size_t const offset) {
    /**
     * 检查边界
     */
    FILAMENT_CHECK_PRECONDITION(offset + count <= mVertexCount)
            << "MorphTargetBuffer (size=" << mVertexCount
            << ") overflow (count=" << (unsigned)count << ", offset=" << (unsigned)offset << ")";

    /**
     * 计算所需的数据大小
     */
    const auto size = getSize<TANGENTS>(count);

    /**
     * 检查目标索引有效性
     */
    FILAMENT_CHECK_PRECONDITION(targetIndex < mCount)
            << targetIndex << " target index must be < " << mCount;

    /**
     * 分配临时缓冲区并复制数据
     */
    // We could use a pool instead of malloc() directly.
    auto* out = (short4*) malloc(size);
    memcpy(out, tangents, sizeof(short4) * count);

    FEngine::DriverApi& driver = engine.getDriverApi();
    /**
     * 更新纹理数据
     */
    updateDataAt(driver, mTbHandle,
            Texture::Format::RGBA_INTEGER, Texture::Type::SHORT,
            (char const*)out, sizeof(short4), targetIndex,
            count, offset);
}

/**
 * 更新指定纹理的数据
 * 
 * 将数据更新到纹理的指定位置，处理跨行的情况。
 * 由于纹理宽度限制为 2048，数据可能需要分成多行存储。
 * 
 * 更新策略：
 * 1. 如果起始位置不在行首，先更新第一行的部分数据
 * 2. 更新中间的完整行（如果有）
 * 3. 更新最后一行的部分数据（如果有）
 */
UTILS_NOINLINE
void FMorphTargetBuffer::updateDataAt(DriverApi& driver,
        Handle<HwTexture> handle, PixelDataFormat const format, PixelDataType const type,
        const char* out, size_t const elementSize,
        size_t const targetIndex, size_t const count, size_t const offset) {

    /**
     * 计算纹理坐标
     */
    size_t yoffset              = offset / MAX_MORPH_TARGET_BUFFER_WIDTH;  // Y 偏移（行号）
    size_t const xoffset        = offset % MAX_MORPH_TARGET_BUFFER_WIDTH;  // X 偏移（列号）
    size_t const textureWidth   = getWidth(mVertexCount);  // 纹理宽度
    size_t const alignment      = ((textureWidth - xoffset) % textureWidth);  // 第一行剩余空间
    size_t const lineCount      = (count > alignment) ? (count - alignment) / textureWidth : 0;  // 完整行数
    size_t const lastLineCount  = (count > alignment) ? (count - alignment) % textureWidth : 0;  // 最后一行元素数

    /**
     * 使用 shared_ptr 管理缓冲区生命周期
     * 
     * 'out' 缓冲区可能被使用最多 3 次（部分行、完整行、最后部分行），
     * 因此使用 shared_ptr 来管理其生命周期。
     * 副作用是回调函数会在堆上分配一个小对象。
     */
    // 'out' buffer is going to be used up to 3 times, so for simplicity we use a shared_buffer
    // to manage its lifetime. One side effect of this is that the callbacks below will allocate
    // a small object on the heap.
    std::shared_ptr<void> const allocation((void*)out, free);

    /**
     * 注意：由于纹理宽度最多为 2048，大多数情况下只需要一次纹理更新调用
     * （即顶点数不超过 2048）。
     */
    // Note: because the texture width is up to 2048, we're expecting that most of the time
    // only a single texture update call will be necessary (i.e. that there are no more
    // than 2048 vertices).

    /**
     * TODO: 可以通过在第一次 update3DImage 调用中处理"部分单行"和"前几个完整行"
     *       来稍微改善代码局部性。
     */
    // TODO: we could improve code locality a bit if we handled the "partial single line" and
    //       the "full first several lines" with the first call to update3DImage below.

    /**
     * 更新第一行的部分数据（如果有）
     */
    if (xoffset) {
        // update the first partial line if any
        driver.update3DImage(handle, 0, xoffset, yoffset, targetIndex,  // mip=0, xoffset, yoffset, layer=targetIndex
                min(count, textureWidth - xoffset), 1, 1,  // width, height, depth
                PixelBufferDescriptor::make(
                        out, (textureWidth - xoffset) * elementSize,  // 数据指针和大小
                        format, type,  // 格式和类型
                        [allocation](void const*, size_t) {}  // 回调（保持缓冲区存活）
                ));
        yoffset++;  // 移动到下一行
        out += min(count, textureWidth - xoffset) * elementSize;  // 更新数据指针
    }

    /**
     * 更新完整行（如果有）
     */
    if (lineCount) {
        // update the full-width lines if any
        driver.update3DImage(handle, 0, 0, yoffset, targetIndex,  // mip=0, x=0, yoffset, layer=targetIndex
                textureWidth, lineCount, 1,  // width, height, depth
                PixelBufferDescriptor::make(
                        out, (textureWidth * lineCount) * elementSize,  // 数据指针和大小
                        format, type,  // 格式和类型
                        [allocation](void const*, size_t) {}  // 回调（保持缓冲区存活）
                ));
        yoffset += lineCount;  // 移动到下一行
        out += (lineCount * textureWidth) * elementSize;  // 更新数据指针
    }

    /**
     * 更新最后一行的部分数据（如果有）
     */
    if (lastLineCount) {
        // update the last partial line if any
        driver.update3DImage(handle, 0, 0, yoffset, targetIndex,  // mip=0, x=0, yoffset, layer=targetIndex
                lastLineCount, 1, 1,  // width, height, depth
                PixelBufferDescriptor::make(
                        out, lastLineCount * elementSize,  // 数据指针和大小
                        format, type,  // 格式和类型
                        [allocation](void const*, size_t) {}  // 回调（保持缓冲区存活）
                ));
    }
}

} // namespace filament

