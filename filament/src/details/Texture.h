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

#ifndef TNT_FILAMENT_DETAILS_TEXTURE_H
#define TNT_FILAMENT_DETAILS_TEXTURE_H

#include "downcast.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <filament/Texture.h>

#include <utils/compiler.h>

#include <array>
#include <cmath>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FEngine;
class FStream;

/**
 * 纹理实现类
 * 
 * 管理 GPU 纹理对象。
 * 纹理用于存储图像数据，可以用于采样、渲染目标等。
 * 
 * 实现细节：
 * - 支持多种纹理类型（2D、3D、立方体贴图、数组纹理等）
 * - 支持多种像素格式（RGBA8、RGBA16F、压缩格式等）
 * - 支持 Mipmap 级别
 * - 支持外部图像和流式纹理
 */
class FTexture : public Texture {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FTexture(FEngine& engine, const Builder& builder);

    /**
     * 终止纹理
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 获取硬件句柄
     * 
     * @return 纹理硬件句柄
     */
    backend::Handle<backend::HwTexture> getHwHandle() const noexcept { return mHandle; }
    
    /**
     * 获取用于采样的硬件句柄
     * 
     * 返回用于采样的纹理句柄（可能不同于主句柄，例如用于 LOD 范围）。
     * 
     * @return 用于采样的纹理硬件句柄
     */
    backend::Handle<backend::HwTexture> getHwHandleForSampling() const noexcept;

    /**
     * 获取宽度
     * 
     * @param level Mip 级别（默认为 0）
     * @return 宽度（像素）
     */
    size_t getWidth(size_t level = 0) const noexcept;
    
    /**
     * 获取高度
     * 
     * @param level Mip 级别（默认为 0）
     * @return 高度（像素）
     */
    size_t getHeight(size_t level = 0) const noexcept;
    
    /**
     * 获取深度
     * 
     * @param level Mip 级别（默认为 0）
     * @return 深度（像素，对于 3D 纹理或数组纹理）
     */
    size_t getDepth(size_t level = 0) const noexcept;
    
    /**
     * 获取 Mip 级别数量
     * 
     * @return Mip 级别数量
     */
    size_t getLevelCount() const noexcept { return mLevelCount; }
    
    /**
     * 获取采样器目标
     * 
     * @return 采样器目标（2D、3D、立方体贴图等）
     */
    Sampler getTarget() const noexcept { return mTarget; }
    
    /**
     * 获取内部格式
     * 
     * @return 内部格式
     */
    InternalFormat getFormat() const noexcept { return mFormat; }
    
    /**
     * 获取使用方式
     * 
     * @return 使用方式标志
     */
    Usage getUsage() const noexcept { return mUsage; }

    /**
     * 设置图像数据
     * 
     * 更新纹理的指定区域。
     * 
     * @param engine 引擎引用
     * @param level Mip 级别
     * @param xoffset X 偏移量
     * @param yoffset Y 偏移量
     * @param zoffset Z 偏移量（对于 3D 纹理）
     * @param width 宽度
     * @param height 高度
     * @param depth 深度
     * @param buffer 像素缓冲区描述符（会被移动）
     */
    void setImage(FEngine& engine, size_t level,
            uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
            uint32_t width, uint32_t height, uint32_t depth,
            PixelBufferDescriptor&& buffer) const;

    /**
     * 设置图像数据（已弃用）
     * 
     * 使用面偏移量设置立方体贴图图像。
     * 
     * @param engine 引擎引用
     * @param level Mip 级别
     * @param buffer 像素缓冲区描述符（会被移动）
     * @param faceOffsets 面偏移量
     */
    UTILS_DEPRECATED
    void setImage(FEngine& engine, size_t level,
            PixelBufferDescriptor&& buffer, const FaceOffsets& faceOffsets) const;

    /**
     * 设置外部图像（ExternalImageHandleRef 版本）
     * 
     * 将纹理绑定到外部图像句柄。
     * 
     * @param engine 引擎引用
     * @param image 外部图像句柄引用
     */
    void setExternalImage(FEngine& engine, ExternalImageHandleRef image) noexcept;
    
    /**
     * 设置外部图像（void* 版本）
     * 
     * 将纹理绑定到外部图像指针。
     * 
     * @param engine 引擎引用
     * @param image 外部图像指针
     */
    void setExternalImage(FEngine& engine, void* image) noexcept;
    
    /**
     * 设置外部图像（多平面版本）
     * 
     * 将纹理绑定到外部图像指针的指定平面。
     * 
     * @param engine 引擎引用
     * @param image 外部图像指针
     * @param plane 平面索引
     */
    void setExternalImage(FEngine& engine, void* image, size_t plane) noexcept;
    
    /**
     * 设置外部流
     * 
     * 将纹理绑定到外部流（如相机预览流）。
     * 
     * @param engine 引擎引用
     * @param stream 流指针
     */
    void setExternalStream(FEngine& engine, FStream* stream) noexcept;

    /**
     * 生成 Mipmap
     * 
     * 为纹理生成所有 Mip 级别。
     * 
     * @param engine 引擎引用
     */
    void generateMipmaps(FEngine& engine) const noexcept;

    /**
     * 检查是否压缩
     * 
     * @return 如果格式是压缩格式返回 true，否则返回 false
     */
    bool isCompressed() const noexcept { return isCompressedFormat(mFormat); }

    /**
     * 检查是否为立方体贴图
     * 
     * @return 如果是立方体贴图返回 true，否则返回 false
     */
    bool isCubemap() const noexcept { return mTarget == Sampler::SAMPLER_CUBEMAP; }

    /**
     * 获取流
     * 
     * @return 流常量指针（如果没有流则返回 nullptr）
     */
    FStream const* getStream() const noexcept { return mStream; }

    /*
     * 工具函数
     */
    /**
     * Utilities
     */

    /**
     * 检查纹理格式是否受支持
     * 
     * 同步调用后端。返回后端是否支持特定格式。
     * 
     * @param engine 引擎引用
     * @param format 内部格式
     * @return 如果支持返回 true，否则返回 false
     */
    // Synchronous call to the backend. Returns whether a backend supports a particular format.
    static bool isTextureFormatSupported(FEngine& engine, InternalFormat format) noexcept;

    /**
     * 检查纹理格式是否支持 Mipmap
     * 
     * 同步调用后端。返回后端是否支持特定格式的 Mipmap。
     * 
     * @param engine 引擎引用
     * @param format 内部格式
     * @return 如果支持 Mipmap 返回 true，否则返回 false
     */
    // Synchronous call to the backend. Returns whether a backend supports mipmapping of a particular format.
    static bool isTextureFormatMipmappable(FEngine& engine, InternalFormat format) noexcept;

    /**
     * 检查格式是否压缩
     * 
     * @param format 内部格式
     * @return 如果格式是压缩格式返回 true，否则返回 false
     */
    // Returns whether particular format is compressed
    static bool isTextureFormatCompressed(InternalFormat format) noexcept;

    /**
     * 检查是否支持受保护纹理
     * 
     * 同步调用后端。返回后端是否支持受保护纹理。
     * 
     * @param engine 引擎引用
     * @return 如果支持返回 true，否则返回 false
     */
    // Synchronous call to the backend. Returns whether a backend supports protected textures.
    static bool isProtectedTexturesSupported(FEngine& engine) noexcept;

    /**
     * 检查是否支持纹理通道重排
     * 
     * 同步调用后端。返回后端是否支持纹理通道重排。
     * 
     * @param engine 引擎引用
     * @return 如果支持返回 true，否则返回 false
     */
    // Synchronous call to the backend. Returns whether a backend supports texture swizzling.
    static bool isTextureSwizzleSupported(FEngine& engine) noexcept;

    /**
     * 计算纹理数据大小
     * 
     * 计算在 CPU 端上传纹理数据所需的存储空间。
     * 
     * @param format 像素格式
     * @param type 像素数据类型
     * @param stride 步长（字节）
     * @param height 高度（像素）
     * @param alignment 对齐（字节）
     * @return 数据大小（字节）
     */
    // storage needed on the CPU side for texture data uploads
    static size_t computeTextureDataSize(Format format, Type type,
            size_t stride, size_t height, size_t alignment) noexcept;

    /**
     * 获取格式大小
     * 
     * 返回给定格式的像素大小（字节）。
     * 
     * @param format 内部格式
     * @return 像素大小（字节）
     */
    // Size a of a pixel in bytes for the given format
    static size_t getFormatSize(InternalFormat format) noexcept;

    /**
     * 获取纹理类型
     * 
     * @return 纹理类型
     */
    backend::TextureType getTextureType() const noexcept;

    /**
     * 计算指定 Mip 级别的宽度或高度
     * 
     * 从基础值计算给定 Mipmap 级别的宽度或高度。
     * 
     * @param level Mip 级别
     * @param baseLevelValue 基础级别值
     * @return Mip 级别的值（至少为 1）
     */
    // Returns the with or height for a given mipmap level from the base value.
    static size_t valueForLevel(uint8_t const level, size_t const baseLevelValue) {
        return std::max(size_t(1), baseLevelValue >> level);  // 右移 level 位，至少为 1
    }

    /**
     * 计算最大 Mip 级别数量
     * 
     * 返回给定最大维度的纹理的最大级别数量。
     * 
     * @param maxDimension 最大维度
     * @return 最大 Mip 级别数量
     */
    // Returns the max number of levels for a texture of given max dimensions
    static uint8_t maxLevelCount(uint32_t const maxDimension) noexcept {
        return std::max(1, std::ilogbf(float(maxDimension)) + 1);  // 计算对数并加 1
    }

    /**
     * 计算最大 Mip 级别数量
     * 
     * 返回给定维度的纹理的最大级别数量。
     * 
     * @param width 宽度
     * @param height 高度
     * @return 最大 Mip 级别数量
     */
    // Returns the max number of levels for a texture of given dimensions
    static uint8_t maxLevelCount(uint32_t const width, uint32_t const height) noexcept {
        uint32_t const maxDimension = std::max(width, height);  // 计算最大维度
        return maxLevelCount(maxDimension);  // 调用单参数版本
    }

    /**
     * 验证像素格式和类型
     * 
     * 检查内部格式、像素数据格式和像素数据类型是否兼容。
     * 
     * @param internalFormat 内部格式
     * @param format 像素数据格式
     * @param type 像素数据类型
     * @return 如果兼容返回 true，否则返回 false
     */
    static bool validatePixelFormatAndType(backend::TextureFormat internalFormat,
            backend::PixelDataFormat format, backend::PixelDataType type) noexcept;

    /**
     * 获取最大纹理大小
     * 
     * @param engine 引擎引用
     * @param type 采样器类型
     * @return 最大纹理大小（像素）
     */
    static size_t getMaxTextureSize(FEngine& engine, Sampler type) noexcept;

    /**
     * 获取最大数组纹理层数
     * 
     * @param engine 引擎引用
     * @return 最大数组纹理层数
     */
    static size_t getMaxArrayTextureLayers(FEngine& engine) noexcept;

    /**
     * 检查纹理句柄是否可以改变
     * 
     * @return 如果句柄可以改变返回 true，否则返回 false
     */
    bool textureHandleCanMutate() const noexcept;
    
    /**
     * 更新 LOD 范围
     * 
     * 更新活动 LOD 范围。
     * 
     * @param level Mip 级别
     */
    void updateLodRange(uint8_t level) noexcept;

    /**
     * 检查是否有 Blit 源使用方式
     * 
     * TODO: 在未来的 Filament 版本中移除。见下面的描述。
     * 
     * @return 如果有 Blit 源使用方式返回 true，否则返回 false
     */
    // TODO: remove in a future filament release.  See below for description.
    bool hasBlitSrcUsage() const noexcept {
        return mHasBlitSrc;  // 返回 Blit 源标志
    }

private:
    friend class Texture;  // 允许 Texture 访问私有成员
    
    /**
     * LOD 范围结构
     * 
     * 表示 Mip 级别的范围。
     */
    struct LodRange {
        /**
         * 0,0 表示 LOD 范围未设置（所有级别都可用）
         */
        // 0,0 means lod-range unset (all levels are available)
        uint8_t first = 0;  // 第一个 LOD
        uint8_t last = 0;   // 最后一个 LOD 之后（不包含）
        
        /**
         * 检查范围是否为空
         * 
         * @return 如果范围为空返回 true，否则返回 false
         */
        bool empty() const noexcept { return first == last; }
        
        /**
         * 获取范围大小
         * 
         * @return 范围大小（级别数）
         */
        size_t size() const noexcept { return last - first; }
    };

    /**
     * 检查是否有所有 LOD
     * 
     * @param range LOD 范围
     * @return 如果范围包含所有 LOD 返回 true，否则返回 false
     */
    bool hasAllLods(LodRange const range) const noexcept {
        return range.first == 0 && range.last == mLevelCount;  // 检查是否从 0 开始到级别数量
    }

    /**
     * 更新 LOD 范围
     * 
     * @param baseLevel 基础级别
     * @param levelCount 级别数量
     */
    void updateLodRange(uint8_t baseLevel, uint8_t levelCount) noexcept;
    
    /**
     * 设置句柄
     * 
     * @param handle 纹理句柄
     */
    void setHandles(backend::Handle<backend::HwTexture> handle) noexcept;
    
    /**
     * 设置用于采样的句柄
     * 
     * @param handle 纹理句柄
     * @return 用于采样的句柄
     */
    backend::Handle<backend::HwTexture> setHandleForSampling(
            backend::Handle<backend::HwTexture> handle) const noexcept;
    
    /**
     * 创建占位符纹理
     * 
     * 创建一个占位符纹理句柄。
     * 
     * @param driver 驱动 API 引用
     * @return 占位符纹理句柄
     */
    static backend::Handle<backend::HwTexture> createPlaceholderTexture(
            backend::DriverApi& driver) noexcept;

    backend::Handle<backend::HwTexture> mHandle;  // 纹理硬件句柄
    mutable backend::Handle<backend::HwTexture> mHandleForSampling;  // 用于采样的纹理句柄（可变）
    backend::DriverApi* mDriver = nullptr;  // 驱动 API 指针（仅用于 getHwHandleForSampling()）
    LodRange mLodRange{};  // LOD 范围
    mutable LodRange mActiveLodRange{};  // 活动 LOD 范围（可变）

    uint32_t mWidth = 1;  // 宽度（像素）
    uint32_t mHeight = 1;  // 高度（像素）
    uint32_t mDepth = 1;  // 深度（像素）

    InternalFormat mFormat = InternalFormat::RGBA8;  // 内部格式（默认 RGBA8）
    Sampler mTarget = Sampler::SAMPLER_2D;  // 采样器目标（默认 2D）
    uint8_t mLevelCount = 1;  // Mip 级别数量
    uint8_t mSampleCount = 1;  // 采样数量（用于 MSAA）

    /**
     * 通道重排数组
     * 
     * 定义纹理通道的重排方式（RGBA）。
     */
    std::array<Swizzle, 4> mSwizzle = {
           Swizzle::CHANNEL_0, Swizzle::CHANNEL_1,
           Swizzle::CHANNEL_2, Swizzle::CHANNEL_3 };  // 默认不重排

    Usage mUsage = Usage::DEFAULT;  // 使用方式（默认）
    backend::TextureType mTextureType;  // 纹理类型

    /**
     * TODO: 在未来的 Filament 版本中移除。
     * 指示用户是否设置了 TextureUsage::BLIT_SRC 使用方式。
     * 这将用于临时验证此纹理是否可用于 readPixels。
     */
    // TODO: remove in a future filament release.
    // Indicates whether the user has set the TextureUsage::BLIT_SRC usage. This will be used to
    // temporarily validate whether this texture can be used for readPixels.
    bool mHasBlitSrc        : 1;  // 是否有 Blit 源使用方式
    bool mExternal          : 1;  // 是否为外部纹理
    bool mTextureIsSwizzled : 1;  // 纹理是否已重排

    FStream* mStream = nullptr;  // 流指针（仅用于流式纹理）
};

FILAMENT_DOWNCAST(Texture)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_TEXTURE_H
