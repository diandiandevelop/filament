/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "details/Texture.h"

#include "details/Engine.h"
#include "details/Stream.h"

#include "private/backend/BackendUtils.h"

#include "FilamentAPI-impl.h"

#include <filament/Texture.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <math/half.h>
#include <math/scalar.h>
#include <math/vec3.h>

#include <utils/Allocator.h>
#include <utils/algorithm.h>
#include <utils/BitmaskEnum.h>
#include <utils/compiler.h>
#include <utils/CString.h>
#include <utils/StaticString.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Panic.h>

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

#include <stddef.h>
#include <stdint.h>

using namespace utils;

namespace filament {

using namespace backend;
using namespace math;

/**
 * 创建可复制的函数对象
 * 
 * 这是一个技巧，用于创建包含不可复制闭包的 std::function<>。
 * 
 * 实现细节：
 * 1. 将闭包包装在 shared_ptr 中（使其可复制）
 * 2. 返回一个 lambda，该 lambda 捕获 shared_ptr 并调用原始闭包
 * 
 * @tparam F 函数类型
 * @param f 函数对象（会被移动）
 * @return 可复制的函数对象
 */
template<class F>
static auto make_copyable_function(F&& f) {
    using dF = std::decay_t<F>;  // 获取衰减后的类型
    auto spf = std::make_shared<dF>(std::forward<F>(f));  // 将闭包包装在 shared_ptr 中
    return [spf](auto&& ... args) -> decltype(auto) {  // 返回捕获 shared_ptr 的 lambda
        return (*spf)(decltype(args)(args)...);  // 调用原始闭包
    };
}

/**
 * 构建器详情结构
 * 
 * 存储纹理的构建参数。
 * 这些参数在 build() 方法中用于创建实际的纹理对象。
 */
struct Texture::BuilderDetails {
    intptr_t mImportedId = 0;  // 导入的纹理 ID（0 表示非导入，非零表示从外部 API 导入）
    uint32_t mWidth = 1;  // 宽度（默认 1，像素）
    uint32_t mHeight = 1;  // 高度（默认 1，像素）
    uint32_t mDepth = 1;  // 深度（默认 1，用于 3D 纹理或数组纹理的层数）
    uint8_t mLevels = 1;  // Mip 级别数（默认 1，至少为 1）
    uint8_t mSamples = 1;  // 采样数（默认 1，用于 MSAA，1 表示无多重采样）
    Sampler mTarget = Sampler::SAMPLER_2D;  // 采样器目标类型（默认 2D 纹理）
    InternalFormat mFormat = InternalFormat::RGBA8;  // 内部格式（默认 RGBA8，每通道 8 位）
    Usage mUsage = Usage::NONE;  // 使用方式（默认 NONE，会在 build() 中设置为 DEFAULT）
    bool mHasBlitSrc = false;  // 是否有 Blit 源使用方式（用于 readPixels 等操作）
    bool mTextureIsSwizzled = false;  // 纹理是否被重排（通道重映射）
    bool mExternal = false;  // 是否为外部纹理（由外部系统管理，如 Android SurfaceTexture）
    std::array<Swizzle, 4> mSwizzle = {  // 重排配置（RGBA 通道的重映射）
           Swizzle::CHANNEL_0, Swizzle::CHANNEL_1,  // R 和 G 通道保持原样
           Swizzle::CHANNEL_2, Swizzle::CHANNEL_3 };  // B 和 A 通道保持原样
};

/**
 * 构建器类型别名
 */
using BuilderType = Texture;

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
 * 设置宽度
 * 
 * @param width 宽度
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::width(uint32_t const width) noexcept {
    mImpl->mWidth = width;  // 设置宽度
    return *this;  // 返回自身引用
}

/**
 * 设置高度
 * 
 * @param height 高度
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::height(uint32_t const height) noexcept {
    mImpl->mHeight = height;  // 设置高度
    return *this;  // 返回自身引用
}

/**
 * 设置深度
 * 
 * 用于 3D 纹理或数组纹理。
 * 
 * @param depth 深度
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::depth(uint32_t const depth) noexcept {
    mImpl->mDepth = depth;  // 设置深度
    return *this;  // 返回自身引用
}

/**
 * 设置 Mip 级别数
 * 
 * @param levels Mip 级别数（至少为 1）
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::levels(uint8_t const levels) noexcept {
    mImpl->mLevels = std::max(uint8_t(1), levels);  // 确保至少为 1
    return *this;  // 返回自身引用
}

/**
 * 设置采样数
 * 
 * 用于多重采样抗锯齿（MSAA）。
 * 
 * @param samples 采样数（至少为 1）
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::samples(uint8_t const samples) noexcept {
    mImpl->mSamples = std::max(uint8_t(1), samples);  // 确保至少为 1
    return *this;  // 返回自身引用
}

/**
 * 设置采样器目标类型
 * 
 * @param target 采样器目标类型
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::sampler(Sampler const target) noexcept {
    mImpl->mTarget = target;  // 设置目标类型
    return *this;  // 返回自身引用
}

/**
 * 设置内部格式
 * 
 * @param format 内部格式
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::format(InternalFormat const format) noexcept {
    mImpl->mFormat = format;  // 设置格式
    return *this;  // 返回自身引用
}

/**
 * 设置使用方式
 * 
 * @param usage 使用方式（位掩码）
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::usage(Usage const usage) noexcept {
    mImpl->mUsage = Usage(usage);  // 设置使用方式
    return *this;  // 返回自身引用
}

/**
 * 导入纹理
 * 
 * 从外部 API 导入已存在的纹理。
 * 
 * @param id 导入的纹理 ID（不能为 0）
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::import(intptr_t const id) noexcept {
    assert_invariant(id);  // 导入的 ID 不能为零
    // imported id can't be zero
    mImpl->mImportedId = id;  // 设置导入 ID
    return *this;  // 返回自身引用
}

/**
 * 设置为外部纹理
 * 
 * 标记纹理为外部纹理（由外部系统管理）。
 * 
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::external() noexcept {
    mImpl->mExternal = true;  // 设置为外部纹理
    return *this;  // 返回自身引用
}

/**
 * 设置纹理重排
 * 
 * 设置纹理通道的重排配置。
 * 
 * @param r 红色通道重排
 * @param g 绿色通道重排
 * @param b 蓝色通道重排
 * @param a Alpha 通道重排
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::swizzle(Swizzle const r, Swizzle const g, Swizzle const b, Swizzle const a) noexcept {
    mImpl->mTextureIsSwizzled = true;  // 标记为重排纹理
    mImpl->mSwizzle = { r, g, b, a };  // 设置重排配置
    return *this;  // 返回自身引用
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 调用名称混入方法
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 名称静态字符串
 * @return 构建器引用（支持链式调用）
 */
Texture::Builder& Texture::Builder::name(StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 调用名称混入方法
}

/**
 * 构建纹理
 * 
 * 根据构建器配置创建纹理对象。
 * 
 * @param engine 引擎引用
 * @return 纹理指针
 */
Texture* Texture::Builder::build(Engine& engine) {
    /**
     * 验证纹理格式（非外部纹理）
     */
    if (mImpl->mTarget != SamplerType::SAMPLER_EXTERNAL) {  // 如果不是外部纹理
        FILAMENT_CHECK_PRECONDITION(Texture::isTextureFormatSupported(engine, mImpl->mFormat))  // 检查格式是否支持
                << "Texture format " << uint16_t(mImpl->mFormat)
                << " not supported on this platform, texture name="
                << getNameOrDefault().c_str_safe();

        FILAMENT_CHECK_PRECONDITION(mImpl->mWidth > 0 && mImpl->mHeight > 0)  // 检查尺寸是否有效
                << "Texture has invalid dimensions: (" << mImpl->mWidth << ", " << mImpl->mHeight
                << "), texture name=" << getNameOrDefault().c_str_safe();
    }

    /**
     * 验证多重采样纹理必须可采样
     */
    if (mImpl->mSamples > 1) {  // 如果是多重采样纹理
        FILAMENT_CHECK_PRECONDITION(any(mImpl->mUsage & Texture::Usage::SAMPLEABLE))  // 检查是否可采样
                << "Multisample (" << unsigned(mImpl->mSamples)
                << ") texture is not sampleable, texture name=" << getNameOrDefault().c_str_safe();
    }

    /**
     * 验证受保护纹理支持
     */
    const bool isProtectedTexturesSupported =
            downcast(engine).getDriverApi().isProtectedTexturesSupported();  // 检查是否支持受保护纹理
    const bool useProtectedMemory = bool(mImpl->mUsage & TextureUsage::PROTECTED);  // 检查是否使用受保护内存

    FILAMENT_CHECK_PRECONDITION(
            (isProtectedTexturesSupported && useProtectedMemory) || !useProtectedMemory)  // 验证受保护纹理支持
            << "Texture is PROTECTED but protected textures are not supported";

    /**
     * 验证纹理尺寸
     */
    size_t const maxTextureDimension = getMaxTextureSize(engine, mImpl->mTarget);  // 获取最大纹理尺寸
    size_t const maxTextureDepth = (mImpl->mTarget == Sampler::SAMPLER_2D_ARRAY ||  // 如果是数组纹理
                                    mImpl->mTarget == Sampler::SAMPLER_CUBEMAP_ARRAY)
                                       ? getMaxArrayTextureLayers(engine)  // 使用最大数组层数
                                       : maxTextureDimension;  // 否则使用最大纹理尺寸

    FILAMENT_CHECK_PRECONDITION(
            mImpl->mWidth <= maxTextureDimension &&  // 检查宽度
            mImpl->mHeight <= maxTextureDimension &&  // 检查高度
            mImpl->mDepth <= maxTextureDepth) << "Texture dimensions out of range: "  // 检查深度
                    << "width= " << mImpl->mWidth << " (>" << maxTextureDimension << ")"
                    <<", height= " << mImpl->mHeight << " (>" << maxTextureDimension << ")"
                    << ", depth= " << mImpl->mDepth << " (>" << maxTextureDepth << ")";

    /**
     * Lambda 函数：验证采样器类型
     * 
     * 检查采样器类型是否在当前特性级别下受支持。
     */
    const auto validateSamplerType = [&engine = downcast(engine)](SamplerType const sampler) -> bool {
        switch (sampler) {
            case SamplerType::SAMPLER_2D:  // 2D 纹理
            case SamplerType::SAMPLER_CUBEMAP:  // 立方体贴图
            case SamplerType::SAMPLER_EXTERNAL:  // 外部纹理
                return true;  // 所有特性级别都支持
            case SamplerType::SAMPLER_3D:  // 3D 纹理
            case SamplerType::SAMPLER_2D_ARRAY:  // 2D 数组纹理
                return engine.hasFeatureLevel(FeatureLevel::FEATURE_LEVEL_1);  // 需要特性级别 1
            case SamplerType::SAMPLER_CUBEMAP_ARRAY:  // 立方体贴图数组
                return engine.hasFeatureLevel(FeatureLevel::FEATURE_LEVEL_2);  // 需要特性级别 2
        }
        return false;  // 未知类型
    };

    /**
     * 验证采样器类型
     */
    // Validate sampler before any further interaction with it.
    FILAMENT_CHECK_PRECONDITION(validateSamplerType(mImpl->mTarget))  // 验证采样器类型
            << "SamplerType " << uint8_t(mImpl->mTarget) << " not support at feature level "
            << uint8_t(engine.getActiveFeatureLevel());

    /**
     * SAMPLER_EXTERNAL 意味着导入
     */
    // SAMPLER_EXTERNAL implies imported.
    if (mImpl->mTarget == SamplerType::SAMPLER_EXTERNAL) {  // 如果是外部采样器
        mImpl->mExternal = true;  // 设置为外部纹理
    }

    /**
     * 计算最大 Mip 级别数
     * 
     * 根据纹理类型和尺寸计算最大可能的 Mip 级别数。
     */
    uint8_t maxLevelCount;  // 最大 Mip 级别数
    switch (mImpl->mTarget) {                    // 根据采样器目标类型
        case SamplerType::SAMPLER_2D:            // 2D 纹理
        case SamplerType::SAMPLER_2D_ARRAY:      // 2D 数组纹理
        case SamplerType::SAMPLER_CUBEMAP:       // 立方体贴图
        case SamplerType::SAMPLER_CUBEMAP_ARRAY: // 立方体贴图数组
            maxLevelCount =
                    FTexture::maxLevelCount(mImpl->mWidth, mImpl->mHeight); // 基于宽度和高度计算
            break;
        case SamplerType::SAMPLER_3D:                         // 3D 纹理
            maxLevelCount = FTexture::maxLevelCount(std::max( // 基于最大维度计算
                    { mImpl->mWidth, mImpl->mHeight, mImpl->mDepth }));
            break;
        case SamplerType::SAMPLER_EXTERNAL:
            // external samplers can't mipmap
            maxLevelCount = 1;
            break;
    }
    // SAMPLER_EXTERNAL implies external textures.
    if (mImpl->mTarget == SamplerType::SAMPLER_EXTERNAL) {
        mImpl->mExternal = true;
    }

    mImpl->mLevels = std::min(mImpl->mLevels, maxLevelCount);  // 限制级别数不超过最大值

    /**
     * 如果使用方式为 NONE，设置为默认值
     */
    if (mImpl->mUsage == TextureUsage::NONE) {  // 如果未指定使用方式
        mImpl->mUsage = TextureUsage::DEFAULT;  // 设置为默认使用方式
         if (mImpl->mExternal) {
            // external textures can't be uploadable
            mImpl->mUsage = TextureUsage::SAMPLEABLE;
        }
    }

    /**
     * 自动添加 GEN_MIPMAPPABLE 使用方式（向后兼容）
     * 
     * TODO: 这存在是为了向后兼容，但在安全时应该移除。
     */
    auto const& featureFlags = downcast(engine).features.engine.debug;  // 获取特性标志

    bool const formatGenMipmappable =
            downcast(engine).getDriverApi().isTextureFormatMipmappable(mImpl->mFormat);  // 检查格式是否可生成 Mipmap
    // TODO: This exists for backwards compatibility, but should remove when safe.
    if (!featureFlags.assert_texture_can_generate_mipmap &&  // 如果未启用断言
            /**
             * 根据以下条件猜测是否应该添加 GEN_MIPMAPPABLE：
             * - 格式可生成 Mipmap
             * - 级别数大于 1
             * - 宽度或高度大于 1
             * - 不是外部纹理
             */
            // Guess whether GEN_MIPMAPPABLE should be added or not based the following criteria.
            (formatGenMipmappable &&  // 格式可生成 Mipmap
                    mImpl->mLevels > 1 &&  // 级别数大于 1
                    (mImpl->mWidth > 1 || mImpl->mHeight > 1) &&  // 尺寸大于 1
                    !mImpl->mExternal)) {  // 不是外部纹理
        mImpl->mUsage |= TextureUsage::GEN_MIPMAPPABLE;  // 添加生成 Mipmap 使用方式
    }

    /**
     * 自动添加 BLIT_SRC 使用方式（向后兼容）
     * 
     * TODO: 在未来的 Filament 版本中移除。
     * 客户端可能不知道需要读取的纹理必须具有 BLIT_SRC 使用方式。
     * 目前，我们通过确保任何颜色附件都可以作为 readPixels() 的复制源来解决此问题。
     */
    // TODO: remove in a future filament release.
    // Clients might not have known that textures that are read need to have BLIT_SRC as usages. For
    // now, we workaround the issue by making sure any color attachment can be the source of a copy
    // for readPixels().
    mImpl->mHasBlitSrc = any(mImpl->mUsage & TextureUsage::BLIT_SRC);  // 检查是否有 BLIT_SRC
    if (!mImpl->mHasBlitSrc && any(mImpl->mUsage & TextureUsage::COLOR_ATTACHMENT)) {  // 如果没有 BLIT_SRC 但是颜色附件
        mImpl->mUsage |= TextureUsage::BLIT_SRC;  // 添加 BLIT_SRC 使用方式
    }

    /**
     * 验证纹理配置
     */
    const bool sampleable = bool(mImpl->mUsage & TextureUsage::SAMPLEABLE);  // 是否可采样
    const bool swizzled = mImpl->mTextureIsSwizzled;  // 是否重排
    const bool imported = mImpl->mImportedId;  // 是否导入

    /**
     * WebGL 不支持纹理重排
     */
    #if defined(__EMSCRIPTEN__)
    FILAMENT_CHECK_PRECONDITION(!swizzled) << "WebGL does not support texture swizzling.";  // 检查重排
    #endif

    /**
     * 重排纹理必须可采样
     */
    FILAMENT_CHECK_PRECONDITION((swizzled && sampleable) || !swizzled)  // 验证重排纹理可采样
            << "Swizzled texture must be SAMPLEABLE";

    /**
     * 导入纹理必须可采样
     */
    FILAMENT_CHECK_PRECONDITION((imported && sampleable) || !imported)  // 验证导入纹理可采样
            << "Imported texture must be SAMPLEABLE";

    return downcast(engine).createTexture(*this);  // 创建纹理
}

// ------------------------------------------------------------------------------------------------

/**
 * 纹理构造函数
 * 
 * 创建纹理对象并分配驱动资源。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FTexture::FTexture(FEngine& engine, const Builder& builder)
    : mHasBlitSrc(false),  // 初始化无 Blit 源
      mExternal(false) {  // 初始化非外部纹理
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    mDriver = &driver;  // 保存驱动指针（不幸的是，getHwHandleForSampling() 需要这个）
    // this is unfortunately needed for getHwHandleForSampling()
    mWidth  = static_cast<uint32_t>(builder->mWidth);  // 设置宽度
    mHeight = static_cast<uint32_t>(builder->mHeight);  // 设置高度
    mDepth  = static_cast<uint32_t>(builder->mDepth);  // 设置深度
    mFormat = builder->mFormat;  // 设置格式
    mUsage = builder->mUsage;  // 设置使用方式
    mTarget = builder->mTarget;  // 设置目标类型
    mLevelCount = builder->mLevels;  // 设置 Mip 级别数
    mSampleCount = builder->mSamples;  // 设置采样数
    mSwizzle = builder->mSwizzle;  // 设置重排配置
    mTextureIsSwizzled = builder->mTextureIsSwizzled;  // 设置重排标志
    mHasBlitSrc = builder->mHasBlitSrc;  // 设置 Blit 源标志
    mExternal = builder->mExternal;  // 设置外部纹理标志
    mTextureType = backend::getTextureType(mFormat);  // 获取纹理类型

    /**
     * 处理外部纹理（非导入）
     * 
     * mHandle 和 mHandleForSampling 将在 setExternalImage() 中创建。
     * 如果此纹理在调用 setExternalImage() 之前用于采样，
     * 我们将延迟创建一个 1x1 占位符纹理。
     */
    bool const isImported = builder->mImportedId != 0;  // 检查是否导入
    if (mExternal && !isImported) {  // 如果是外部纹理且未导入
        // mHandle and mHandleForSampling will be created in setExternalImage()
        // If this Texture is used for sampling before setExternalImage() is called,
        // we'll lazily create a 1x1 placeholder texture.
        return;  // 提前返回，稍后创建句柄
    }

    /**
     * 创建纹理标签
     */
    ImmutableCString tag{ !builder.getName().empty() ? builder.getName() : "FTexture" };  // 使用构建器名称或默认名称

    /**
     * 创建或导入纹理
     */
    if (UTILS_LIKELY(!isImported)) {  // 如果不是导入纹理（常见情况）
        mHandle = driver.createTexture(  // 创建纹理
                mTarget, mLevelCount, mFormat, mSampleCount, mWidth, mHeight, mDepth, mUsage,
                std::move(tag));  // 移动标签
    } else {  // 如果是导入纹理
        mHandle = driver.importTexture(builder->mImportedId,  // 导入纹理
                mTarget, mLevelCount, mFormat, mSampleCount, mWidth, mHeight, mDepth, mUsage,
                std::move(tag));  // 移动标签
    }

    /**
     * 处理纹理重排
     * 
     * 如果纹理被重排，创建一个重排视图并替换原始句柄。
     */
    if (UTILS_UNLIKELY(builder->mTextureIsSwizzled)) {  // 如果纹理被重排（不常见）
        auto const& s = builder->mSwizzle;  // 获取重排配置
        auto swizzleView = driver.createTextureViewSwizzle(mHandle, s[0], s[1], s[2], s[3]);  // 创建重排视图
        driver.destroyTexture(mHandle);  // 销毁原始纹理
        mHandle = swizzleView;  // 使用重排视图
    }

    mHandleForSampling = mHandle;  // 设置采样句柄

}

/**
 * 终止纹理
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
// frees driver resources, object becomes invalid
void FTexture::terminate(FEngine& engine) {
    setHandles({});  // 清空句柄
}

/**
 * 获取纹理宽度
 * 
 * 获取指定 Mip 级别的纹理宽度。
 * 
 * @param level Mip 级别（0 为基础级别）
 * @return 纹理宽度
 */
size_t FTexture::getWidth(size_t const level) const noexcept {
    return valueForLevel(level, mWidth);  // 根据级别计算宽度
}

/**
 * 获取纹理高度
 * 
 * 获取指定 Mip 级别的纹理高度。
 * 
 * @param level Mip 级别（0 为基础级别）
 * @return 纹理高度
 */
size_t FTexture::getHeight(size_t const level) const noexcept {
    return valueForLevel(level, mHeight);  // 根据级别计算高度
}

/**
 * 获取纹理深度
 * 
 * 获取指定 Mip 级别的纹理深度（用于 3D 纹理或数组纹理）。
 * 
 * @param level Mip 级别（0 为基础级别）
 * @return 纹理深度
 */
size_t FTexture::getDepth(size_t const level) const noexcept {
    return valueForLevel(level, mDepth);  // 根据级别计算深度
}

/**
 * 设置纹理图像数据
 * 
 * 将像素数据上传到纹理的指定区域。
 * 
 * @param engine 引擎引用
 * @param level Mip 级别
 * @param xoffset X 偏移量
 * @param yoffset Y 偏移量
 * @param zoffset Z 偏移量（用于 3D 纹理或数组纹理）
 * @param width 区域宽度
 * @param height 区域高度
 * @param depth 区域深度
 * @param p 像素缓冲区描述符（会被移动）
 */
void FTexture::setImage(FEngine& engine, size_t const level,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const zoffset,
        uint32_t const width, uint32_t const height, uint32_t const depth,
        PixelBufferDescriptor&& p) const {

    /**
     * 在特性级别 0 下，步长必须为 0 或等于宽度
     */
    if (UTILS_UNLIKELY(!engine.hasFeatureLevel(FeatureLevel::FEATURE_LEVEL_1))) {  // 如果是特性级别 0
        FILAMENT_CHECK_PRECONDITION(p.stride == 0 || p.stride == width)  // 检查步长
                << "PixelBufferDescriptor stride must be 0 (or width) at FEATURE_LEVEL_0";
    }

    /**
     * 验证纹理格式（应该已经验证过）
     */
    // this should have been validated already
    assert_invariant(isTextureFormatSupported(engine, mFormat));  // 断言格式支持

    /**
     * 验证像素格式和类型组合
     */
    FILAMENT_CHECK_PRECONDITION(p.type == PixelDataType::COMPRESSED ||  // 如果是压缩数据或
            validatePixelFormatAndType(mFormat, p.format, p.type))  // 格式和类型组合有效
            << "The combination of internal format=" << unsigned(mFormat)
            << " and {format=" << unsigned(p.format) << ", type=" << unsigned(p.type)
            << "} is not supported.";

    /**
     * 验证不是流纹理
     */
    FILAMENT_CHECK_PRECONDITION(!mStream) << "setImage() called on a Stream texture.";  // 检查流纹理

    /**
     * 验证 Mip 级别
     */
    FILAMENT_CHECK_PRECONDITION(level < mLevelCount)  // 检查级别
            << "level=" << unsigned(level) << " is >= to levelCount=" << unsigned(mLevelCount)
            << ".";

    /**
     * 验证不是外部纹理
     */
    FILAMENT_CHECK_PRECONDITION(!mExternal)  // 检查外部纹理
            << "External Texture not supported for this operation.";

    /**
     * 验证纹理可上传
     */
    FILAMENT_CHECK_PRECONDITION(any(mUsage & Texture::Usage::UPLOADABLE))  // 检查使用方式
            << "Texture is not uploadable.";

    /**
     * 验证不是多重采样纹理
     */
    FILAMENT_CHECK_PRECONDITION(mSampleCount <= 1)  // 检查采样数
            << "Operation not supported with multisample ("
            << unsigned(mSampleCount) << ") texture.";

    /**
     * 验证 X 方向边界
     */
    FILAMENT_CHECK_PRECONDITION(xoffset + width <= valueForLevel(level, mWidth))  // 检查 X 边界
            << "xoffset (" << unsigned(xoffset) << ") + width (" << unsigned(width)
            << ") > texture width (" << valueForLevel(level, mWidth) << ") at level ("
            << unsigned(level) << ")";

    /**
     * 验证 Y 方向边界
     */
    FILAMENT_CHECK_PRECONDITION(yoffset + height <= valueForLevel(level, mHeight))  // 检查 Y 边界
            << "yoffset (" << unsigned(yoffset) << ") + height (" << unsigned(height)
            << ") > texture height (" << valueForLevel(level, mHeight) << ") at level ("
            << unsigned(level) << ")";

    /**
     * 验证缓冲区不为空
     */
    FILAMENT_CHECK_PRECONDITION(p.buffer) << "Data buffer is nullptr.";  // 检查缓冲区

    /**
     * 计算有效纹理深度或层数
     * 
     * 根据纹理类型确定有效的深度或层数。
     */
    uint32_t effectiveTextureDepthOrLayers = 0;  // 有效深度或层数
    switch (mTarget) {  // 根据采样器目标类型
        case SamplerType::SAMPLER_EXTERNAL:  // 外部纹理
            // can't happen by construction, fallthrough...  // 构造时不会发生，继续...
        case SamplerType::SAMPLER_2D:  // 2D 纹理
            assert_invariant(mDepth == 1);  // 断言深度为 1
            effectiveTextureDepthOrLayers = 1;  // 深度为 1
            break;
        case SamplerType::SAMPLER_3D:  // 3D 纹理
            effectiveTextureDepthOrLayers = valueForLevel(level, mDepth);  // 根据级别计算深度
            break;
        case SamplerType::SAMPLER_2D_ARRAY:  // 2D 数组纹理
            effectiveTextureDepthOrLayers = mDepth;  // 深度为层数
            break;
        case SamplerType::SAMPLER_CUBEMAP:  // 立方体贴图
            effectiveTextureDepthOrLayers = 6;  // 6 个面
            break;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:  // 立方体贴图数组
            effectiveTextureDepthOrLayers = mDepth * 6;  // 层数乘以 6
            break;
    }

    /**
     * 验证 Z 方向边界
     */
    FILAMENT_CHECK_PRECONDITION(zoffset + depth <= effectiveTextureDepthOrLayers)  // 检查 Z 边界
            << "zoffset (" << unsigned(zoffset) << ") + depth (" << unsigned(depth)
            << ") > texture depth (" << effectiveTextureDepthOrLayers << ") at level ("
            << unsigned(level) << ")";

    /**
     * 如果尺寸为零，直接返回（无操作）
     * 
     * PixelBufferDescriptor 的回调应该在对象销毁时自动调用。
     * 下面的前提条件检查假设 width、height、depth 不为零。
     */
    if (UTILS_UNLIKELY(!width || !height || !depth)) {  // 如果尺寸为零
        // The operation is a no-op, return immediately. The PixelBufferDescriptor callback
        // should be called automatically when the object is destroyed.
        // The precondition check below assumes width, height, depth non null.
        return;  // 直接返回
    }

    /**
     * 对于非压缩数据，验证缓冲区大小
     */
    if (p.type != PixelDataType::COMPRESSED) {  // 如果不是压缩数据
        using PBD = PixelBufferDescriptor;  // 类型别名
        size_t const stride = p.stride ? p.stride : width;  // 步长（如果未指定则使用宽度）
        size_t const bpp = PBD::computeDataSize(p.format, p.type, 1, 1, 1);  // 每像素字节数
        size_t const bpr = PBD::computeDataSize(p.format, p.type, stride, 1, p.alignment);  // 每行字节数
        size_t const bpl = bpr * height;  // 每层字节数
        // TODO: PBD should have a "layer stride"  // TODO: PBD 应该有"层步长"
        // TODO: PBD should have a p.depth (# layers to skip)  // TODO: PBD 应该有 p.depth（跳过的层数）

        /**
         * Lambda 函数：计算 3D 子区域中最后一个像素的字节偏移
         */
        /* Calculates the byte offset of the last pixel in a 3D sub-region. */
        auto const calculateLastPixelOffset = [bpp, bpr, bpl](  // Lambda 函数
                size_t xoff, size_t yoff, size_t zoff,  // 偏移量
                size_t width, size_t height, size_t depth) {  // 尺寸
            /**
             * 最后一个像素的 0 索引坐标是：
             * x = xoff + width - 1
             * y = yoff + height - 1
             * z = zoff + depth - 1
             * 偏移量计算公式为：(z * bpl) + (y * bpr) + (x * bpp)
             */
            // The 0-indexed coordinates of the last pixel are:
            // x = xoff + width - 1
            // y = yoff + height - 1
            // z = zoff + depth - 1
            // The offset is calculated as: (z * bpl) + (y * bpr) + (x * bpp)
            return ((zoff + depth  - 1) * bpl) +  // Z 方向偏移
                   ((yoff + height - 1) * bpr) +  // Y 方向偏移
                   ((xoff + width  - 1) * bpp);  // X 方向偏移
        };

        /**
         * 计算最后一个像素的偏移
         */
        size_t const lastPixelOffset = calculateLastPixelOffset(
                p.left, p.top, 0, width, height, depth);  // 计算偏移

        /**
         * 确保整个最后一个像素都在缓冲区中
         */
        // make sure the whole last pixel is in the buffer
        FILAMENT_CHECK_PRECONDITION(lastPixelOffset + bpp <= p.size)  // 检查缓冲区溢出
                << "buffer overflow: (size=" << size_t(p.size) << ", stride=" << size_t(p.stride)
                << ", left=" << unsigned(p.left) << ", top=" << unsigned(p.top)
                << ") smaller than specified region "
                   "{{"
                << unsigned(xoffset) << "," << unsigned(yoffset) << "," << unsigned(zoffset) << "},{"
                << unsigned(width) << "," << unsigned(height) << "," << unsigned(depth) << ")}}";
    }

    /**
     * 更新 3D 图像数据
     */
    engine.getDriverApi().update3DImage(mHandle, uint8_t(level), xoffset, yoffset, zoffset, width,
            height, depth, std::move(p));  // 更新图像

    /**
     * 更新 LOD 范围
     * 
     * 注意：此方法不应该是 const，但为了保持 API 兼容性而保留。
     */
    // this method shouldn't have been const
    const_cast<FTexture*>(this)->updateLodRange(level);  // 更新 LOD 范围
}

// deprecated
void FTexture::setImage(FEngine& engine, size_t const level,
        PixelBufferDescriptor&& buffer, const FaceOffsets& faceOffsets) const {

    auto validateTarget = [](SamplerType const sampler) -> bool {
        switch (sampler) {
            case SamplerType::SAMPLER_CUBEMAP:
                return true;
            case SamplerType::SAMPLER_2D:
            case SamplerType::SAMPLER_3D:
            case SamplerType::SAMPLER_2D_ARRAY:
            case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            case SamplerType::SAMPLER_EXTERNAL:
                return false;
        }
        return false;
    };

    // this should have been validated already
    assert_invariant(isTextureFormatSupported(engine, mFormat));

    FILAMENT_CHECK_PRECONDITION(buffer.type == PixelDataType::COMPRESSED ||
            validatePixelFormatAndType(mFormat, buffer.format, buffer.type))
            << "The combination of internal format=" << unsigned(mFormat)
            << " and {format=" << unsigned(buffer.format) << ", type=" << unsigned(buffer.type)
            << "} is not supported.";

    FILAMENT_CHECK_PRECONDITION(!mStream) << "setImage() called on a Stream texture.";

    FILAMENT_CHECK_PRECONDITION(level < mLevelCount)
            << "level=" << unsigned(level) << " is >= to levelCount=" << unsigned(mLevelCount)
            << ".";

    FILAMENT_CHECK_PRECONDITION(validateTarget(mTarget))
            << "Texture Sampler type (" << unsigned(mTarget)
            << ") not supported for this operation.";

    FILAMENT_CHECK_PRECONDITION(buffer.buffer) << "Data buffer is nullptr.";

    auto w = std::max(1u, mWidth >> level);
    auto h = std::max(1u, mHeight >> level);
    assert_invariant(w == h);
    const size_t faceSize = PixelBufferDescriptor::computeDataSize(buffer.format, buffer.type,
            buffer.stride ? buffer.stride : w, h, buffer.alignment);

    if (faceOffsets[0] == 0 &&
        faceOffsets[1] == 1 * faceSize &&
        faceOffsets[2] == 2 * faceSize &&
        faceOffsets[3] == 3 * faceSize &&
        faceOffsets[4] == 4 * faceSize &&
        faceOffsets[5] == 5 * faceSize) {
        // in this special case, we can upload all 6 faces in one call
        engine.getDriverApi().update3DImage(mHandle, uint8_t(level),
                0, 0, 0, w, h, 6, std::move(buffer));
    } else {
        UTILS_NOUNROLL
        for (size_t face = 0; face < 6; face++) {
            engine.getDriverApi().update3DImage(mHandle, uint8_t(level), 0, 0, face, w, h, 1, {
                    (char*)buffer.buffer + faceOffsets[face],
                    faceSize, buffer.format, buffer.type, buffer.alignment,
                    buffer.left, buffer.top, buffer.stride });
        }
        engine.getDriverApi().queueCommand(
                make_copyable_function([buffer = std::move(buffer)]() {}));
    }

    // this method shouldn't been const
    const_cast<FTexture*>(this)->updateLodRange(level);
}

void FTexture::setExternalImage(FEngine& engine, ExternalImageHandleRef image) noexcept {
    FILAMENT_CHECK_PRECONDITION(mExternal) << "The texture must be external.";

    // The call to setupExternalImage2 is synchronous, and allows the driver to take ownership of the
    // external image on this thread, if necessary.
    auto& api = engine.getDriverApi();
    api.setupExternalImage2(image);

    auto texture = api.createTextureExternalImage2(mTarget, mFormat, mWidth, mHeight, mUsage, image);

    if (mTextureIsSwizzled) {
        auto const& s = mSwizzle;
        auto swizzleView = api.createTextureViewSwizzle(texture, s[0], s[1], s[2], s[3]);
        api.destroyTexture(texture);
        texture = swizzleView;
    }

    setHandles(texture);
}

void FTexture::setExternalImage(FEngine& engine, void* image) noexcept {
    FILAMENT_CHECK_PRECONDITION(mExternal) << "The texture must be external.";

    // The call to setupExternalImage is synchronous, and allows the driver to take ownership of the
    // external image on this thread, if necessary.
    auto& api = engine.getDriverApi();
    api.setupExternalImage(image);

    auto texture = api.createTextureExternalImage(mTarget, mFormat, mWidth, mHeight, mUsage, image);

    if (mTextureIsSwizzled) {
        auto const& s = mSwizzle;
        auto swizzleView = api.createTextureViewSwizzle(texture, s[0], s[1], s[2], s[3]);
        api.destroyTexture(texture);
        texture = swizzleView;
    }

    setHandles(texture);
}

void FTexture::setExternalImage(FEngine& engine, void* image, size_t const plane) noexcept {
    FILAMENT_CHECK_PRECONDITION(mExternal) << "The texture must be external.";

    // The call to setupExternalImage is synchronous, and allows the driver to take ownership of
    // the external image on this thread, if necessary.
    auto& api = engine.getDriverApi();
    api.setupExternalImage(image);

    auto texture =
            api.createTextureExternalImagePlane(mFormat, mWidth, mHeight, mUsage, image, plane);

    if (mTextureIsSwizzled) {
        auto const& s = mSwizzle;
        auto swizzleView = api.createTextureViewSwizzle(texture, s[0], s[1], s[2], s[3]);
        api.destroyTexture(texture);
        texture = swizzleView;
    }

    setHandles(texture);
}

void FTexture::setExternalStream(FEngine& engine, FStream* stream) noexcept {
    FILAMENT_CHECK_PRECONDITION(mExternal) << "The texture must be external.";

    auto& api = engine.getDriverApi();
    auto texture = api.createTexture(
            mTarget, mLevelCount, mFormat, mSampleCount, mWidth, mHeight, mDepth, mUsage);

    if (mTextureIsSwizzled) {
        auto const& s = mSwizzle;
        auto swizzleView = api.createTextureViewSwizzle(texture, s[0], s[1], s[2], s[3]);
        api.destroyTexture(texture);
        texture = swizzleView;
    }

    setHandles(texture);

    if (stream) {
        mStream = stream;
        api.setExternalStream(mHandle, stream->getHandle());
    } else {
        mStream = nullptr;
        api.setExternalStream(mHandle, backend::Handle<HwStream>());
    }
}

void FTexture::generateMipmaps(FEngine& engine) const noexcept {
    FILAMENT_CHECK_PRECONDITION(!mExternal)
            << "External Textures are not mipmappable.";

    FILAMENT_CHECK_PRECONDITION(mTarget != SamplerType::SAMPLER_3D)
            << "3D Textures are not mipmappable.";

    const bool formatMipmappable = engine.getDriverApi().isTextureFormatMipmappable(mFormat);
    FILAMENT_CHECK_PRECONDITION(formatMipmappable)
            << "Texture format " << (unsigned)mFormat << " is not mipmappable.";

    auto const& featureFlags = downcast(engine).features.engine.debug;
    FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION(any(mUsage & TextureUsage::GEN_MIPMAPPABLE),
            featureFlags.assert_texture_can_generate_mipmap)
            << "Texture usage does not have GEN_MIPMAPPABLE set";

    if (mLevelCount < 2 || (mWidth == 1 && mHeight == 1)) {
        return;
    }

    engine.getDriverApi().generateMipmaps(mHandle);
    // this method shouldn't have been const
    const_cast<FTexture*>(this)->updateLodRange(0, mLevelCount);
}

bool FTexture::textureHandleCanMutate() const noexcept {
    return (any(mUsage & Usage::SAMPLEABLE) && mLevelCount > 1) || mExternal;
}

void FTexture::updateLodRange(uint8_t const baseLevel, uint8_t const levelCount) noexcept {
    if (any(mUsage & Usage::SAMPLEABLE) && mLevelCount > 1) {
        auto& range = mLodRange;
        uint8_t const last = int8_t(baseLevel + levelCount);
        if (range.first > baseLevel || range.last < last) {
            if (range.empty()) {
                range = { baseLevel, last };
            } else {
                range.first = std::min(range.first, baseLevel);
                range.last = std::max(range.last, last);
            }
            // We defer the creation of the texture view to getHwHandleForSampling() because it
            // is a common case that by then, the view won't be needed. Creating the first view on a
            // texture has a backend cost.
        }
    }
}

void FTexture::setHandles(Handle<HwTexture> handle) noexcept {
    assert_invariant(!mHandle || mHandleForSampling);
    if (mHandle) {
        mDriver->destroyTexture(mHandle);
    }
    if (mHandleForSampling != mHandle) {
        mDriver->destroyTexture(mHandleForSampling);
    }
    mHandle = handle;
    mHandleForSampling = handle;
}

Handle<HwTexture> FTexture::setHandleForSampling(
        Handle<HwTexture> handle) const noexcept {
    assert_invariant(!mHandle || mHandleForSampling);
    if (mHandleForSampling && mHandleForSampling != mHandle) {
        mDriver->destroyTexture(mHandleForSampling);
    }
    return mHandleForSampling = handle;
}

Handle<HwTexture> FTexture::createPlaceholderTexture(
        DriverApi& driver) noexcept {
    auto handle = driver.createTexture(
            Sampler::SAMPLER_2D, 1, InternalFormat::RGBA8, 1, 1, 1, 1, Usage::DEFAULT);
    static uint8_t pixels[4] = { 0, 0, 0, 0 };
    driver.update3DImage(handle, 0, 0, 0, 0, 1, 1, 1,
            { (char*)&pixels[0], sizeof(pixels),
                    PixelBufferDescriptor::PixelDataFormat::RGBA,
                    PixelBufferDescriptor::PixelDataType::UBYTE });
    return handle;
}

Handle<HwTexture> FTexture::getHwHandleForSampling() const noexcept {
    if (UTILS_UNLIKELY(mExternal && !mHandleForSampling)) {
        return setHandleForSampling(createPlaceholderTexture(*mDriver));
    }
    auto const& range = mLodRange;
    auto& activeRange = mActiveLodRange;
    bool const lodRangeChanged = activeRange.first != range.first || activeRange.last != range.last;
    if (UTILS_UNLIKELY(lodRangeChanged)) {
        activeRange = range;
        if (range.empty() || hasAllLods(range)) {
            std::ignore = setHandleForSampling(mHandle);
        } else {
            std::ignore = setHandleForSampling(mDriver->createTextureView(
                mHandle, range.first, range.size()));
        }
    }
    return mHandleForSampling;
}

void FTexture::updateLodRange(uint8_t const level) noexcept {
    updateLodRange(level, 1);
}

bool FTexture::isTextureFormatSupported(FEngine& engine, InternalFormat const format) noexcept {
    return engine.getDriverApi().isTextureFormatSupported(format);
}

bool FTexture::isTextureFormatMipmappable(FEngine& engine, InternalFormat const format) noexcept {
    return engine.getDriverApi().isTextureFormatMipmappable(format);
}

bool FTexture::isTextureFormatCompressed(InternalFormat const format) noexcept {
    return isCompressedFormat(format);
}

bool FTexture::isProtectedTexturesSupported(FEngine& engine) noexcept {
    return engine.getDriverApi().isProtectedTexturesSupported();
}

bool FTexture::isTextureSwizzleSupported(FEngine& engine) noexcept {
    return engine.getDriverApi().isTextureSwizzleSupported();
}

size_t FTexture::getMaxTextureSize(FEngine& engine, Sampler type) noexcept {
    return engine.getDriverApi().getMaxTextureSize(type);
}

size_t FTexture::getMaxArrayTextureLayers(FEngine& engine) noexcept {
    return engine.getDriverApi().getMaxArrayTextureLayers();
}

size_t FTexture::computeTextureDataSize(Format const format, Type const type,
        size_t const stride, size_t const height, size_t const alignment) noexcept {
    return PixelBufferDescriptor::computeDataSize(format, type, stride, height, alignment);
}

size_t FTexture::getFormatSize(InternalFormat const format) noexcept {
    return backend::getFormatSize(format);
}

TextureType FTexture::getTextureType() const noexcept {
    return mTextureType;
}

bool FTexture::validatePixelFormatAndType(TextureFormat const internalFormat,
        PixelDataFormat const format, PixelDataType const type) noexcept {

    switch (internalFormat) {
        case TextureFormat::R8:
        case TextureFormat::R8_SNORM:
        case TextureFormat::R16F:
        case TextureFormat::R32F:
            if (format != PixelDataFormat::R) {
                return false;
            }
            break;

        case TextureFormat::R8UI:
        case TextureFormat::R8I:
        case TextureFormat::R16UI:
        case TextureFormat::R16I:
        case TextureFormat::R32UI:
        case TextureFormat::R32I:
            if (format != PixelDataFormat::R_INTEGER) {
                return false;
            }
            break;

        case TextureFormat::RG8:
        case TextureFormat::RG8_SNORM:
        case TextureFormat::RG16F:
        case TextureFormat::RG32F:
            if (format != PixelDataFormat::RG) {
                return false;
            }
            break;

        case TextureFormat::RG8UI:
        case TextureFormat::RG8I:
        case TextureFormat::RG16UI:
        case TextureFormat::RG16I:
        case TextureFormat::RG32UI:
        case TextureFormat::RG32I:
            if (format != PixelDataFormat::RG_INTEGER) {
                return false;
            }
            break;

        case TextureFormat::RGB565:
        case TextureFormat::RGB9_E5:
        case TextureFormat::RGB5_A1:
        case TextureFormat::RGBA4:
        case TextureFormat::RGB8:
        case TextureFormat::SRGB8:
        case TextureFormat::RGB8_SNORM:
        case TextureFormat::R11F_G11F_B10F:
        case TextureFormat::RGB16F:
        case TextureFormat::RGB32F:
            if (format != PixelDataFormat::RGB) {
                return false;
            }
            break;

        case TextureFormat::RGB8UI:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB16I:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGB32I:
            if (format != PixelDataFormat::RGB_INTEGER) {
                return false;
            }
            break;

        case TextureFormat::RGBA8:
        case TextureFormat::SRGB8_A8:
        case TextureFormat::RGBA8_SNORM:
        case TextureFormat::RGB10_A2:
        case TextureFormat::RGBA16F:
        case TextureFormat::RGBA32F:
            if (format != PixelDataFormat::RGBA) {
                return false;
            }
            break;

        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA32UI:
        case TextureFormat::RGBA32I:
            if (format != PixelDataFormat::RGBA_INTEGER) {
                return false;
            }
            break;

        case TextureFormat::STENCIL8:
            // there is no pixel data type that can be used for this format
            return false;

        case TextureFormat::DEPTH16:
        case TextureFormat::DEPTH24:
        case TextureFormat::DEPTH32F:
            if (format != PixelDataFormat::DEPTH_COMPONENT) {
                return false;
            }
            break;

        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            if (format != PixelDataFormat::DEPTH_STENCIL) {
                return false;
            }
            break;

        case TextureFormat::UNUSED:
        case TextureFormat::EAC_R11:
        case TextureFormat::EAC_R11_SIGNED:
        case TextureFormat::EAC_RG11:
        case TextureFormat::EAC_RG11_SIGNED:
        case TextureFormat::ETC2_RGB8:
        case TextureFormat::ETC2_SRGB8:
        case TextureFormat::ETC2_RGB8_A1:
        case TextureFormat::ETC2_SRGB8_A1:
        case TextureFormat::ETC2_EAC_RGBA8:
        case TextureFormat::ETC2_EAC_SRGBA8:
        case TextureFormat::DXT1_RGB:
        case TextureFormat::DXT1_RGBA:
        case TextureFormat::DXT3_RGBA:
        case TextureFormat::DXT5_RGBA:
        case TextureFormat::DXT1_SRGB:
        case TextureFormat::DXT1_SRGBA:
        case TextureFormat::DXT3_SRGBA:
        case TextureFormat::DXT5_SRGBA:
        case TextureFormat::RED_RGTC1:
        case TextureFormat::SIGNED_RED_RGTC1:
        case TextureFormat::RED_GREEN_RGTC2:
        case TextureFormat::SIGNED_RED_GREEN_RGTC2:
        case TextureFormat::RGB_BPTC_SIGNED_FLOAT:
        case TextureFormat::RGB_BPTC_UNSIGNED_FLOAT:
        case TextureFormat::RGBA_BPTC_UNORM:
        case TextureFormat::SRGB_ALPHA_BPTC_UNORM:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::RGBA_ASTC_5x4:
        case TextureFormat::RGBA_ASTC_5x5:
        case TextureFormat::RGBA_ASTC_6x5:
        case TextureFormat::RGBA_ASTC_6x6:
        case TextureFormat::RGBA_ASTC_8x5:
        case TextureFormat::RGBA_ASTC_8x6:
        case TextureFormat::RGBA_ASTC_8x8:
        case TextureFormat::RGBA_ASTC_10x5:
        case TextureFormat::RGBA_ASTC_10x6:
        case TextureFormat::RGBA_ASTC_10x8:
        case TextureFormat::RGBA_ASTC_10x10:
        case TextureFormat::RGBA_ASTC_12x10:
        case TextureFormat::RGBA_ASTC_12x12:
        case TextureFormat::SRGB8_ALPHA8_ASTC_4x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x12:
            return false;
    }

    switch (internalFormat) {
        case TextureFormat::R8:
        case TextureFormat::R8UI:
        case TextureFormat::RG8:
        case TextureFormat::RG8UI:
        case TextureFormat::RGB8:
        case TextureFormat::SRGB8:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGBA8:
        case TextureFormat::SRGB8_A8:
        case TextureFormat::RGBA8UI:
            if (type != PixelDataType::UBYTE) {
                return false;
            }
            break;

        case TextureFormat::R8_SNORM:
        case TextureFormat::R8I:
        case TextureFormat::RG8_SNORM:
        case TextureFormat::RG8I:
        case TextureFormat::RGB8_SNORM:
        case TextureFormat::RGB8I:
        case TextureFormat::RGBA8_SNORM:
        case TextureFormat::RGBA8I:
            if (type != PixelDataType::BYTE) {
                return false;
            }
            break;

        case TextureFormat::R16F:
        case TextureFormat::RG16F:
        case TextureFormat::RGB16F:
        case TextureFormat::RGBA16F:
            if (type != PixelDataType::FLOAT && type != PixelDataType::HALF) {
                return false;
            }
            break;

        case TextureFormat::R32F:
        case TextureFormat::RG32F:
        case TextureFormat::RGB32F:
        case TextureFormat::RGBA32F:
        case TextureFormat::DEPTH32F:
            if (type != PixelDataType::FLOAT) {
                return false;
            }
            break;

        case TextureFormat::R16UI:
        case TextureFormat::RG16UI:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGBA16UI:
            if (type != PixelDataType::USHORT) {
                return false;
            }
            break;

        case TextureFormat::R16I:
        case TextureFormat::RG16I:
        case TextureFormat::RGB16I:
        case TextureFormat::RGBA16I:
            if (type != PixelDataType::SHORT) {
                return false;
            }
            break;


        case TextureFormat::R32UI:
        case TextureFormat::RG32UI:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGBA32UI:
            if (type != PixelDataType::UINT) {
                return false;
            }
            break;

        case TextureFormat::R32I:
        case TextureFormat::RG32I:
        case TextureFormat::RGB32I:
        case TextureFormat::RGBA32I:
            if (type != PixelDataType::INT) {
                return false;
            }
            break;

        case TextureFormat::RGB565:
            if (type != PixelDataType::UBYTE && type != PixelDataType::USHORT_565) {
                return false;
            }
            break;


        case TextureFormat::RGB9_E5:
            // TODO: we're missing UINT_5_9_9_9_REV
            if (type != PixelDataType::FLOAT && type != PixelDataType::HALF) {
                return false;
            }
            break;

        case TextureFormat::RGB5_A1:
            // TODO: we're missing USHORT_5_5_5_1
            if (type != PixelDataType::UBYTE && type != PixelDataType::UINT_2_10_10_10_REV) {
                return false;
            }
            break;

        case TextureFormat::RGBA4:
            // TODO: we're missing USHORT_4_4_4_4
            if (type != PixelDataType::UBYTE) {
                return false;
            }
            break;

        case TextureFormat::R11F_G11F_B10F:
            if (type != PixelDataType::FLOAT && type != PixelDataType::HALF
                && type != PixelDataType::UINT_10F_11F_11F_REV) {
                return false;
            }
            break;

        case TextureFormat::RGB10_A2:
            if (type != PixelDataType::UINT_2_10_10_10_REV) {
                return false;
            }
            break;

        case TextureFormat::STENCIL8:
            // there is no pixel data type that can be used for this format
            return false;

        case TextureFormat::DEPTH16:
            if (type != PixelDataType::UINT && type != PixelDataType::USHORT) {
                return false;
            }
            break;

        case TextureFormat::DEPTH24:
            if (type != PixelDataType::UINT) {
                return false;
            }
            break;

        case TextureFormat::DEPTH24_STENCIL8:
            // TODO: we're missing UINT_24_8
            return false;

        case TextureFormat::DEPTH32F_STENCIL8:
            // TODO: we're missing FLOAT_UINT_24_8_REV
            return false;

        case TextureFormat::UNUSED:
        case TextureFormat::EAC_R11:
        case TextureFormat::EAC_R11_SIGNED:
        case TextureFormat::EAC_RG11:
        case TextureFormat::EAC_RG11_SIGNED:
        case TextureFormat::ETC2_RGB8:
        case TextureFormat::ETC2_SRGB8:
        case TextureFormat::ETC2_RGB8_A1:
        case TextureFormat::ETC2_SRGB8_A1:
        case TextureFormat::ETC2_EAC_RGBA8:
        case TextureFormat::ETC2_EAC_SRGBA8:
        case TextureFormat::DXT1_RGB:
        case TextureFormat::DXT1_RGBA:
        case TextureFormat::DXT3_RGBA:
        case TextureFormat::DXT5_RGBA:
        case TextureFormat::DXT1_SRGB:
        case TextureFormat::DXT1_SRGBA:
        case TextureFormat::DXT3_SRGBA:
        case TextureFormat::DXT5_SRGBA:
        case TextureFormat::RED_RGTC1:
        case TextureFormat::SIGNED_RED_RGTC1:
        case TextureFormat::RED_GREEN_RGTC2:
        case TextureFormat::SIGNED_RED_GREEN_RGTC2:
        case TextureFormat::RGB_BPTC_SIGNED_FLOAT:
        case TextureFormat::RGB_BPTC_UNSIGNED_FLOAT:
        case TextureFormat::RGBA_BPTC_UNORM:
        case TextureFormat::SRGB_ALPHA_BPTC_UNORM:
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::RGBA_ASTC_5x4:
        case TextureFormat::RGBA_ASTC_5x5:
        case TextureFormat::RGBA_ASTC_6x5:
        case TextureFormat::RGBA_ASTC_6x6:
        case TextureFormat::RGBA_ASTC_8x5:
        case TextureFormat::RGBA_ASTC_8x6:
        case TextureFormat::RGBA_ASTC_8x8:
        case TextureFormat::RGBA_ASTC_10x5:
        case TextureFormat::RGBA_ASTC_10x6:
        case TextureFormat::RGBA_ASTC_10x8:
        case TextureFormat::RGBA_ASTC_10x10:
        case TextureFormat::RGBA_ASTC_12x10:
        case TextureFormat::RGBA_ASTC_12x12:
        case TextureFormat::SRGB8_ALPHA8_ASTC_4x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x12:
            return false;
    }

    return true;
}

} // namespace filament
