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

//! \file

#ifndef TNT_FILAMENT_TEXTURE_H
#define TNT_FILAMENT_TEXTURE_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <backend/Platform.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FTexture;

class Engine;
class Stream;

/**
 * Texture
 *
 * The Texture class supports:
 *  - 2D textures
 *  - 3D textures
 *  - Cube maps
 *  - mip mapping
 *
 *
 * Creation and destruction
 * ========================
 *
 * A Texture object is created using the Texture::Builder and destroyed by calling
 * Engine::destroy(const Texture*).
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *
 *  filament::Texture* texture = filament::Texture::Builder()
 *              .width(64)
 *              .height(64)
 *              .build(*engine);
 *
 *  engine->destroy(texture);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */
/**
 * Texture（纹理）
 *
 * Texture 类支持：
 *  - 2D 纹理
 *  - 3D 纹理
 *  - 立方体贴图
 *  - mip 映射
 *
 *
 * 创建和销毁
 * ========================
 *
 * Texture 对象使用 Texture::Builder 创建，通过调用
 * Engine::destroy(const Texture*) 销毁。
 *
 */
class UTILS_PUBLIC Texture : public FilamentAPI {
    struct BuilderDetails;

public:
    static constexpr size_t BASE_LEVEL = 0;
    /**
     * 基础 mip 级别
     */

    //! Face offsets for all faces of a cubemap
    /**
     * 立方体贴图所有面的面偏移量
     */
    struct FaceOffsets;

    using PixelBufferDescriptor = backend::PixelBufferDescriptor;    //!< Geometry of a pixel buffer
    /**
     * 像素缓冲区的几何结构
     */
    using Sampler = backend::SamplerType;                            //!< Type of sampler
    /**
     * 采样器类型
     */
    using InternalFormat = backend::TextureFormat;                   //!< Internal texel format
    /**
     * 内部纹素格式
     */
    using CubemapFace = backend::TextureCubemapFace;                 //!< Cube map faces
    /**
     * 立方体贴图面
     */
    using Format = backend::PixelDataFormat;                         //!< Pixel color format
    /**
     * 像素颜色格式
     */
    using Type = backend::PixelDataType;                             //!< Pixel data format
    /**
     * 像素数据类型
     */
    using CompressedType = backend::CompressedPixelDataType;         //!< Compressed pixel data format
    /**
     * 压缩像素数据格式
     */
    using Usage = backend::TextureUsage;                             //!< Usage affects texel layout
    /**
     * 使用方式影响纹素布局
     */
    using Swizzle = backend::TextureSwizzle;                         //!< Texture swizzle
    /**
     * 纹理通道重排
     */
    using ExternalImageHandle = backend::Platform::ExternalImageHandle;
    /**
     * 外部图像句柄
     */
    using ExternalImageHandleRef = backend::Platform::ExternalImageHandleRef;
    /**
     * 外部图像句柄引用
     */

    /** @return Whether a backend supports a particular format. */
    /**
     * @return 后端是否支持特定格式
     */
    static bool isTextureFormatSupported(Engine& engine, InternalFormat format) noexcept;

    /** @return Whether a backend supports mipmapping of a particular format. */
    /**
     * @return 后端是否支持特定格式的 mipmap
     */
    static bool isTextureFormatMipmappable(Engine& engine, InternalFormat format) noexcept;

    /** @return Whether particular format is compressed */
    /**
     * @return 特定格式是否被压缩
     */
    static bool isTextureFormatCompressed(InternalFormat format) noexcept;

    /** @return Whether this backend supports protected textures. */
    /**
     * @return 此后端是否支持受保护纹理
     */
    static bool isProtectedTexturesSupported(Engine& engine) noexcept;

    /** @return Whether a backend supports texture swizzling. */
    /**
     * @return 后端是否支持纹理通道重排
     */
    static bool isTextureSwizzleSupported(Engine& engine) noexcept;

    static size_t computeTextureDataSize(Format format, Type type,
            size_t stride, size_t height, size_t alignment) noexcept;
    /**
     * 计算纹理数据大小
     *
     * @param format 像素格式
     * @param type 像素数据类型
     * @param stride 行跨度（字节）
     * @param height 高度（行数）
     * @param alignment 对齐方式
     * @return 纹理数据大小（字节）
     */

    /** @return Whether a combination of texture format, pixel format and type is valid. */
    /**
     * @return 纹理格式、像素格式和类型的组合是否有效
     */
    static bool validatePixelFormatAndType(InternalFormat internalFormat, Format format, Type type) noexcept;

    /** @return the maximum size in texels of a texture of type \p type. At least 2048 for
     * 2D textures, 256 for 3D textures. */
    /**
     * @return 类型为 \p type 的纹理的最大尺寸（以纹素为单位）。
     * 2D 纹理至少为 2048，3D 纹理至少为 256
     */
    static size_t getMaxTextureSize(Engine& engine, Sampler type) noexcept;

    /** @return the maximum number of layers supported by texture arrays. At least 256. */
    /**
     * @return 纹理数组支持的最大层数。至少为 256
     */
    static size_t getMaxArrayTextureLayers(Engine& engine) noexcept;

    //! Use Builder to construct a Texture object instance
    /**
     * 使用 Builder 构造 Texture 对象实例
     */
    class Builder : public BuilderBase<BuilderDetails>, public BuilderNameMixin<Builder> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Specifies the width in texels of the texture. Doesn't need to be a power-of-two.
         * @param width Width of the texture in texels (default: 1).
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定纹理的宽度（以纹素为单位）。不需要是 2 的幂。
         * @param width 纹理的宽度（纹素）（默认值：1）
         * @return 此 Builder，用于链接调用
         */
        Builder& width(uint32_t width) noexcept;

        /**
         * Specifies the height in texels of the texture. Doesn't need to be a power-of-two.
         * @param height Height of the texture in texels (default: 1).
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定纹理的高度（以纹素为单位）。不需要是 2 的幂。
         * @param height 纹理的高度（纹素）（默认值：1）
         * @return 此 Builder，用于链接调用
         */
        Builder& height(uint32_t height) noexcept;

        /**
         * Specifies the depth in texels of the texture. Doesn't need to be a power-of-two.
         * The depth controls the number of layers in a 2D array texture. Values greater than 1
         * effectively create a 3D texture.
         * @param depth Depth of the texture in texels (default: 1).
         * @return This Builder, for chaining calls.
         * @attention This Texture instance must use Sampler::SAMPLER_3D or
         *            Sampler::SAMPLER_2D_ARRAY or it has no effect.
         */
        /**
         * 指定纹理的深度（以纹素为单位）。不需要是 2 的幂。
         * 深度控制 2D 数组纹理中的层数。大于 1 的值
         * 有效地创建 3D 纹理。
         * @param depth 纹理的深度（纹素）（默认值：1）
         * @return 此 Builder，用于链接调用
         * @attention 此 Texture 实例必须使用 Sampler::SAMPLER_3D 或
         *            Sampler::SAMPLER_2D_ARRAY，否则无效
         */
        Builder& depth(uint32_t depth) noexcept;

        /**
         * Specifies the numbers of mip map levels.
         * This creates a mip-map pyramid. The maximum number of levels a texture can have is
         * such that max(width, height, level) / 2^MAX_LEVELS = 1
         * @param levels Number of mipmap levels for this texture.
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定 mip map 级别数
         * 这会创建一个 mip-map 金字塔。纹理可以拥有的最大级别数是
         * 使得 max(width, height, level) / 2^MAX_LEVELS = 1
         * @param levels 此纹理的 mipmap 级别数
         * @return 此 Builder，用于链接调用
         */
        Builder& levels(uint8_t levels) noexcept;

        /**
         * Specifies the numbers of samples used for MSAA (Multisample Anti-Aliasing).
         *
         * Calling this method implicitly indicates the texture is used as a render target. Hence,
         * this method should not be used in conjunction with other methods that are semantically
         * conflicting like `setImage`.
         *
         * If this is invoked for array textures, it means this texture is used for multiview.
         *
         * @param samples Number of samples for this texture.
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定用于 MSAA（多重采样抗锯齿）的采样数
         *
         * 调用此方法隐式表示纹理用作渲染目标。因此，
         * 此方法不应与语义上冲突的其他方法（如 `setImage`）一起使用。
         *
         * 如果为数组纹理调用此方法，则表示此纹理用于多视图。
         *
         * @param samples 此纹理的采样数
         * @return 此 Builder，用于链接调用
         */
        Builder& samples(uint8_t samples) noexcept;

        /**
         * Specifies the type of sampler to use.
         * @param target Sampler type
         * @return This Builder, for chaining calls.
         * @see Sampler
         */
        /**
         * 指定要使用的采样器类型
         * @param target 采样器类型
         * @return 此 Builder，用于链接调用
         * @see Sampler
         */
        Builder& sampler(Sampler target) noexcept;

        /**
         * Specifies the *internal* format of this texture.
         *
         * The internal format specifies how texels are stored (which may be different from how
         * they're specified in setImage()). InternalFormat specifies both the color components
         * and the data type used.
         *
         * @param format Format of the texture's texel.
         * @return This Builder, for chaining calls.
         * @see InternalFormat, setImage
         */
        /**
         * 指定此纹理的*内部*格式
         *
         * 内部格式指定如何存储纹素（可能与
         * setImage() 中的指定方式不同）。InternalFormat 指定颜色分量
         * 和使用的数据类型。
         *
         * @param format 纹理的纹素格式
         * @return 此 Builder，用于链接调用
         * @see InternalFormat, setImage
         */
        Builder& format(InternalFormat format) noexcept;

        /**
         * Specifies if the texture will be used as a render target attachment.
         *
         * If the texture is potentially rendered into, it may require a different memory layout,
         * which needs to be known during construction.
         *
         * @param usage Defaults to Texture::Usage::DEFAULT; c.f. Texture::Usage::COLOR_ATTACHMENT.
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定纹理是否将用作渲染目标附件
         *
         * 如果纹理可能被渲染到，它可能需要不同的内存布局，
         * 这需要在构造时知道。
         *
         * @param usage 默认为 Texture::Usage::DEFAULT；参见 Texture::Usage::COLOR_ATTACHMENT
         * @return 此 Builder，用于链接调用
         */
        Builder& usage(Usage usage) noexcept;

        /**
         * Specifies how a texture's channels map to color components
         *
         * Texture Swizzle is only supported if isTextureSwizzleSupported() returns true.
         *
         * @param r  texture channel for red component
         * @param g  texture channel for green component
         * @param b  texture channel for blue component
         * @param a  texture channel for alpha component
         * @return This Builder, for chaining calls.
         * @see Texture::isTextureSwizzleSupported()
         */
        /**
         * 指定纹理的通道如何映射到颜色分量
         *
         * 只有在 isTextureSwizzleSupported() 返回 true 时才支持纹理通道重排。
         *
         * @param r  红色分量的纹理通道
         * @param g  绿色分量的纹理通道
         * @param b  蓝色分量的纹理通道
         * @param a  alpha 分量的纹理通道
         * @return 此 Builder，用于链接调用
         * @see Texture::isTextureSwizzleSupported()
         */
        Builder& swizzle(Swizzle r, Swizzle g, Swizzle b, Swizzle a) noexcept;

        /**
         * Associate an optional name with this Texture for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this Texture
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 Texture 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。名称
         * 会被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 Texture 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this Texture for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this Texture
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 Texture 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。
         *
         * @param name 用于标识此 Texture 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates an external texture. The content must be set using setExternalImage().
         * The sampler can be SAMPLER_EXTERNAL or SAMPLER_2D depending on the format. Generally
         * YUV formats must use SAMPLER_EXTERNAL. This depends on the backend features and is not
         * validated.
         *
         * If the Sampler is set to SAMPLER_EXTERNAL, external() is implied.
         *
         * @return
         */
        /**
         * 创建外部纹理。必须使用 setExternalImage() 设置内容。
         * 根据格式，采样器可以是 SAMPLER_EXTERNAL 或 SAMPLER_2D。通常
         * YUV 格式必须使用 SAMPLER_EXTERNAL。这取决于后端特性，不会
         * 进行验证。
         *
         * 如果 Sampler 设置为 SAMPLER_EXTERNAL，则隐含 external()。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& external() noexcept;

        /**
         * Creates the Texture object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this Texture with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 创建 Texture 对象并返回指向它的指针
         *
         * @param engine 要与此 Texture 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），
         *           则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         */
        Texture* UTILS_NONNULL build(Engine& engine);

        /* no user serviceable parts below */
        /**
         * 以下部分不可由用户维护
         */

        /**
         * Specify a native texture to import as a Filament texture.
         *
         * The texture id is backend-specific:
         *   - OpenGL: GLuint texture ID
         *   - Metal: id<MTLTexture>
         *
         * With Metal, the id<MTLTexture> object should be cast to an intptr_t using
         * CFBridgingRetain to transfer ownership to Filament. Filament will release ownership of
         * the texture object when the Filament texture is destroyed.
         *
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
         *  id <MTLTexture> metalTexture = ...
         *  filamentTexture->import((intptr_t) CFBridgingRetain(metalTexture));
         *  // free to release metalTexture
         *
         *  // after using texture:
         *  engine->destroy(filamentTexture);   // metalTexture is released
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *
         * @warning This method should be used as a last resort. This API is subject to change or
         * removal.
         *
         * @param id a backend specific texture identifier
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 指定要导入为 Filament 纹理的本机纹理。
         *
         * 纹理 ID 是后端特定的：
         *   - OpenGL: GLuint 纹理 ID
         *   - Metal: id<MTLTexture>
         *
         * 对于 Metal，id<MTLTexture> 对象应使用
         * CFBridgingRetain 转换为 intptr_t 以将所有权转移给 Filament。当 Filament 纹理被销毁时，
         * Filament 将释放纹理对象的所有权。
         *
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cpp}
         *  id <MTLTexture> metalTexture = ...
         *  filamentTexture->import((intptr_t) CFBridgingRetain(metalTexture));
         *  // 可以释放 metalTexture
         *
         *  // 使用纹理后：
         *  engine->destroy(filamentTexture);   // metalTexture 被释放
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *
         * @warning 此方法应作为最后手段使用。此 API 可能会更改或
         * 移除。
         *
         * @param id 后端特定的纹理标识符
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& import(intptr_t id) noexcept;

    private:
        friend class FTexture;
    };

    /**
     * Returns the width of a 2D or 3D texture level
     * @param level texture level.
     * @return Width in texel of the specified \p level, clamped to 1.
     * @attention If this texture is using Sampler::SAMPLER_EXTERNAL, the dimension
     * of the texture are unknown and this method always returns whatever was set on the Builder.
     */
    /**
     * 返回 2D 或 3D 纹理级别的宽度
     * @param level 纹理级别
     * @return 指定 \p level 的宽度（纹素），钳制到 1
     * @attention 如果此纹理使用 Sampler::SAMPLER_EXTERNAL，纹理的
     * 尺寸未知，此方法总是返回 Builder 上设置的任何值
     */
    size_t getWidth(size_t level = BASE_LEVEL) const noexcept;

    /**
     * Returns the height of a 2D or 3D texture level
     * @param level texture level.
     * @return Height in texel of the specified \p level, clamped to 1.
     * @attention If this texture is using Sampler::SAMPLER_EXTERNAL, the dimension
     * of the texture are unknown and this method always returns whatever was set on the Builder.
     */
    /**
     * 返回 2D 或 3D 纹理级别的高度
     * @param level 纹理级别
     * @return 指定 \p level 的高度（纹素），钳制到 1
     * @attention 如果此纹理使用 Sampler::SAMPLER_EXTERNAL，纹理的
     * 尺寸未知，此方法总是返回 Builder 上设置的任何值
     */
    size_t getHeight(size_t level = BASE_LEVEL) const noexcept;

    /**
     * Returns the depth of a 3D texture level
     * @param level texture level.
     * @return Depth in texel of the specified \p level, clamped to 1.
     * @attention If this texture is using Sampler::SAMPLER_EXTERNAL, the dimension
     * of the texture are unknown and this method always returns whatever was set on the Builder.
     */
    /**
     * 返回 3D 纹理级别的深度
     * @param level 纹理级别
     * @return 指定 \p level 的深度（纹素），钳制到 1
     * @attention 如果此纹理使用 Sampler::SAMPLER_EXTERNAL，纹理的
     * 尺寸未知，此方法总是返回 Builder 上设置的任何值
     */
    size_t getDepth(size_t level = BASE_LEVEL) const noexcept;

    /**
     * Returns the maximum number of levels this texture can have.
     * @return maximum number of levels this texture can have.
     * @attention If this texture is using Sampler::SAMPLER_EXTERNAL, the dimension
     * of the texture are unknown and this method always returns whatever was set on the Builder.
     */
    /**
     * 返回此纹理可以拥有的最大级别数
     * @return 此纹理可以拥有的最大级别数
     * @attention 如果此纹理使用 Sampler::SAMPLER_EXTERNAL，纹理的
     * 尺寸未知，此方法总是返回 Builder 上设置的任何值
     */
    size_t getLevels() const noexcept;

    /**
     * Return this texture Sampler as set by Builder::sampler().
     * @return this texture Sampler as set by Builder::sampler()
     */
    /**
     * 返回由 Builder::sampler() 设置的此纹理的 Sampler
     * @return 由 Builder::sampler() 设置的此纹理的 Sampler
     */
    Sampler getTarget() const noexcept;

    /**
     * Return this texture InternalFormat as set by Builder::format().
     * @return this texture InternalFormat as set by Builder::format().
     */
    /**
     * 返回由 Builder::format() 设置的此纹理的 InternalFormat
     * @return 由 Builder::format() 设置的此纹理的 InternalFormat
     */
    InternalFormat getFormat() const noexcept;

    /**
     * Updates a sub-image of a 3D texture or 2D texture array for a level. Cubemaps are treated
     * like a 2D array of six layers.
     *
     * @param engine    Engine this texture is associated to.
     * @param level     Level to set the image for.
     * @param xoffset   Left offset of the sub-region to update.
     * @param yoffset   Bottom offset of the sub-region to update.
     * @param zoffset   Depth offset of the sub-region to update.
     * @param width     Width of the sub-region to update.
     * @param height    Height of the sub-region to update.
     * @param depth     Depth of the sub-region to update.
     * @param buffer    Client-side buffer containing the image to set.
     *
     * @attention \p engine must be the instance passed to Builder::build()
     * @attention \p level must be less than getLevels().
     * @attention \p buffer's Texture::Format must match that of getFormat().
     * @attention This Texture instance must use Sampler::SAMPLER_3D, Sampler::SAMPLER_2D_ARRAY
     *             or Sampler::SAMPLER_CUBEMAP.
     *
     * @see Builder::sampler()
     */
    /**
     * 更新 3D 纹理或 2D 纹理数组的某个级别的子图像。立方体贴图被
     * 视为六层的 2D 数组。
     *
     * @param engine    此纹理关联的 Engine
     * @param level     要设置图像的级别
     * @param xoffset   要更新的子区域的左偏移
     * @param yoffset   要更新的子区域的底偏移
     * @param zoffset   要更新的子区域的深度偏移
     * @param width     要更新的子区域的宽度
     * @param height    要更新的子区域的高度
     * @param depth     要更新的子区域的深度
     * @param buffer    包含要设置的图像的客户端缓冲区
     *
     * @attention \p engine 必须是传递给 Builder::build() 的实例
     * @attention \p level 必须小于 getLevels()
     * @attention \p buffer 的 Texture::Format 必须与 getFormat() 匹配
     * @attention 此 Texture 实例必须使用 Sampler::SAMPLER_3D、Sampler::SAMPLER_2D_ARRAY
     *            或 Sampler::SAMPLER_CUBEMAP
     *
     * @see Builder::sampler()
     */
    void setImage(Engine& engine, size_t level,
            uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
            uint32_t width, uint32_t height, uint32_t depth,
            PixelBufferDescriptor&& buffer) const;

    /**
     * inline helper to update a 2D texture
     *
     * @see setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    /**
     * 内联辅助函数，用于更新 2D 纹理
     *
     * @see setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    void setImage(Engine& engine, size_t level, PixelBufferDescriptor&& buffer) const {
        setImage(engine, level, 0, 0, 0,
            uint32_t(getWidth(level)), uint32_t(getHeight(level)), 1, std::move(buffer));
    }

    /**
     * inline helper to update a 2D texture
     *
     * @see setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    /**
     * 内联辅助函数，用于更新 2D 纹理（指定偏移和尺寸）
     *
     * @see setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    void setImage(Engine& engine, size_t level,
            uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            PixelBufferDescriptor&& buffer) const {
        setImage(engine, level, xoffset, yoffset, 0, width, height, 1, std::move(buffer));
    }

    /**
     * Specify all six images of a cube map level.
     *
     * This method follows exactly the OpenGL conventions.
     *
     * @param engine        Engine this texture is associated to.
     * @param level         Level to set the image for.
     * @param buffer        Client-side buffer containing the images to set.
     * @param faceOffsets   Offsets in bytes into \p buffer for all six images. The offsets
     *                      are specified in the following order: +x, -x, +y, -y, +z, -z
     *
     * @attention \p engine must be the instance passed to Builder::build()
     * @attention \p level must be less than getLevels().
     * @attention \p buffer's Texture::Format must match that of getFormat().
     * @attention This Texture instance must use Sampler::SAMPLER_CUBEMAP or it has no effect
     *
     * @see Texture::CubemapFace, Builder::sampler()
     *
     * @deprecated Instead, use setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    /**
     * 指定立方体贴图级别的所有六个图像
     *
     * 此方法完全遵循 OpenGL 约定。
     *
     * @param engine        此纹理关联的 Engine
     * @param level         要设置图像的级别
     * @param buffer        包含要设置的图像的客户端缓冲区
     * @param faceOffsets   所有六个图像在 \p buffer 中的字节偏移量。偏移量
     *                      按以下顺序指定：+x, -x, +y, -y, +z, -z
     *
     * @attention \p engine 必须是传递给 Builder::build() 的实例
     * @attention \p level 必须小于 getLevels()
     * @attention \p buffer 的 Texture::Format 必须与 getFormat() 匹配
     * @attention 此 Texture 实例必须使用 Sampler::SAMPLER_CUBEMAP，否则无效
     *
     * @see Texture::CubemapFace, Builder::sampler()
     *
     * @deprecated 改用 setImage(Engine& engine, size_t level,
     *              uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
     *              uint32_t width, uint32_t height, uint32_t depth,
     *              PixelBufferDescriptor&& buffer)
     */
    UTILS_DEPRECATED
    void setImage(Engine& engine, size_t level,
            PixelBufferDescriptor&& buffer, const FaceOffsets& faceOffsets) const;


    /**
     * Specify the external image to associate with this Texture. Typically, the external
     * image is OS specific, and can be a video or camera frame.
     * There are many restrictions when using an external image as a texture, such as:
     *   - only the level of detail (lod) 0 can be specified
     *   - only nearest or linear filtering is supported
     *   - the size and format of the texture is defined by the external image
     *   - only the CLAMP_TO_EDGE wrap mode is supported
     *
     * @param engine        Engine this texture is associated to.
     * @param image         An opaque handle to a platform specific image. It must be created using Platform
     *                      specific APIs. For example PlatformEGL::createExternalImage(EGLImageKHR eglImage)
     *
     * @see PlatformEGL::createExternalImage
     * @see PlatformEGLAndroid::createExternalImage
     * @see PlatformCocoaGL::createExternalImage
     * @see PlatformCocoaTouchGL::createExternalImage
     */
    /**
     * 指定要与此 Texture 关联的外部图像。通常，外部
     * 图像是特定于操作系统的，可以是视频或相机帧。
     * 使用外部图像作为纹理时有许多限制，例如：
     *   - 只能指定细节级别 (lod) 0
     *   - 仅支持最近或线性过滤
     *   - 纹理的大小和格式由外部图像定义
     *   - 仅支持 CLAMP_TO_EDGE 包装模式
     *
     * @param engine        此纹理关联的 Engine
     * @param image         特定于平台的不透明图像句柄。必须使用 Platform
     *                      特定 API 创建。例如 PlatformEGL::createExternalImage(EGLImageKHR eglImage)
     *
     * @see PlatformEGL::createExternalImage
     * @see PlatformEGLAndroid::createExternalImage
     * @see PlatformCocoaGL::createExternalImage
     * @see PlatformCocoaTouchGL::createExternalImage
     */
    void setExternalImage(Engine& engine, ExternalImageHandleRef image) noexcept;

    /**
     * Specify the external image to associate with this Texture. Typically, the external
     * image is OS specific, and can be a video or camera frame.
     * There are many restrictions when using an external image as a texture, such as:
     *   - only the level of detail (lod) 0 can be specified
     *   - only nearest or linear filtering is supported
     *   - the size and format of the texture is defined by the external image
     *   - only the CLAMP_TO_EDGE wrap mode is supported
     *
     * @param engine        Engine this texture is associated to.
     * @param image         An opaque handle to a platform specific image. Supported types are
     *                      eglImageOES on Android and CVPixelBufferRef on iOS.
     *
     *                      On iOS the following pixel formats are supported:
     *                        - kCVPixelFormatType_32BGRA
     *                        - kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
     *
     * @attention \p engine must be the instance passed to Builder::build()
     * @attention This Texture instance must use Sampler::SAMPLER_EXTERNAL or it has no effect
     *
     * @see Builder::sampler()
     *
     * @deprecated Instead, use setExternalImage(Engine& engine, ExternalImageHandleRef image)
     */
    /**
     * 指定要与此 Texture 关联的外部图像。通常，外部
     * 图像是特定于操作系统的，可以是视频或相机帧。
     * 使用外部图像作为纹理时有许多限制，例如：
     *   - 只能指定细节级别 (lod) 0
     *   - 仅支持最近或线性过滤
     *   - 纹理的大小和格式由外部图像定义
     *   - 仅支持 CLAMP_TO_EDGE 包装模式
     *
     * @param engine        此纹理关联的 Engine
     * @param image         特定于平台的不透明图像句柄。支持的类型是
     *                      Android 上的 eglImageOES 和 iOS 上的 CVPixelBufferRef
     *
     *                      在 iOS 上支持以下像素格式：
     *                        - kCVPixelFormatType_32BGRA
     *                        - kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
     *
     * @attention \p engine 必须是传递给 Builder::build() 的实例
     * @attention 此 Texture 实例必须使用 Sampler::SAMPLER_EXTERNAL，否则无效
     *
     * @see Builder::sampler()
     *
     * @deprecated 改用 setExternalImage(Engine& engine, ExternalImageHandleRef image)
     */
    UTILS_DEPRECATED
    void setExternalImage(Engine& engine, void* UTILS_NONNULL image) noexcept;

    /**
     * Specify the external image and plane to associate with this Texture. Typically, the external
     * image is OS specific, and can be a video or camera frame. When using this method, the
     * external image must be a planar type (such as a YUV camera frame). The plane parameter
     * selects which image plane is bound to this texture.
     *
     * A single external image can be bound to different Filament textures, with each texture
     * associated with a separate plane:
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * textureA->setExternalImage(engine, image, 0);
     * textureB->setExternalImage(engine, image, 1);
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * There are many restrictions when using an external image as a texture, such as:
     *   - only the level of detail (lod) 0 can be specified
     *   - only nearest or linear filtering is supported
     *   - the size and format of the texture is defined by the external image
     *   - only the CLAMP_TO_EDGE wrap mode is supported
     *
     * @param engine        Engine this texture is associated to.
     * @param image         An opaque handle to a platform specific image. Supported types are
     *                      eglImageOES on Android and CVPixelBufferRef on iOS.
     * @param plane         The plane index of the external image to associate with this texture.
     *
     *                      This method is only meaningful on iOS with
     *                      kCVPixelFormatType_420YpCbCr8BiPlanarFullRange images. On platforms
     *                      other than iOS, this method is a no-op.
     */
    /**
     * 指定要与此 Texture 关联的外部图像和平面。通常，外部
     * 图像是特定于操作系统的，可以是视频或相机帧。使用此方法时，
     * 外部图像必须是平面类型（例如 YUV 相机帧）。plane 参数
     * 选择哪个图像平面绑定到此纹理。
     *
     * 单个外部图像可以绑定到不同的 Filament 纹理，每个纹理
     * 关联到单独的平面。
     *
     * 使用外部图像作为纹理时有许多限制，例如：
     *   - 只能指定细节级别 (lod) 0
     *   - 仅支持最近或线性过滤
     *   - 纹理的大小和格式由外部图像定义
     *   - 仅支持 CLAMP_TO_EDGE 包装模式
     *
     * @param engine        此纹理关联的 Engine
     * @param image         特定于平台的不透明图像句柄。支持的类型是
     *                      Android 上的 eglImageOES 和 iOS 上的 CVPixelBufferRef
     * @param plane         要与此纹理关联的外部图像的平面索引
     *
     *                      此方法仅在 iOS 上对
     *                      kCVPixelFormatType_420YpCbCr8BiPlanarFullRange 图像有意义。在
     *                      iOS 以外的平台上，此方法是无操作。
     */
    void setExternalImage(Engine& engine, void* UTILS_NONNULL image, size_t plane) noexcept;

    /**
     * Specify the external stream to associate with this Texture. Typically, the external
     * stream is OS specific, and can be a video or camera stream.
     * There are many restrictions when using an external stream as a texture, such as:
     *   - only the level of detail (lod) 0 can be specified
     *   - only nearest or linear filtering is supported
     *   - the size and format of the texture is defined by the external stream
     *
     * @param engine        Engine this texture is associated to.
     */
    /**
     * 指定要与此 Texture 关联的外部流。通常，外部
     * 流是特定于操作系统的，可以是视频或相机流。
     * 使用外部流作为纹理时有许多限制，例如：
     *   - 只能指定细节级别 (lod) 0
     *   - 仅支持最近或线性过滤
     *   - 纹理的大小和格式由外部流定义
     *
     * @param engine        此纹理关联的 Engine
     * @param stream        Stream 对象
     *
     * @attention \p engine 必须是传递给 Builder::build() 的实例
     * @attention 此 Texture 实例必须使用 Sampler::SAMPLER_EXTERNAL，否则无效
     *
     * @see Builder::sampler(), Stream
     *
     */
    void setExternalStream(Engine& engine, Stream* UTILS_NULLABLE stream) noexcept;

    /**
     * Generates all the mipmap levels automatically. This requires the texture to have a
     * color-renderable format and usage set to BLIT_SRC | BLIT_DST. If unspecified,
     * usage bits are set automatically.
     *
     * @param engine        Engine this texture is associated to.
     *
     * @attention \p engine must be the instance passed to Builder::build()
     * @attention This Texture instance must NOT use SamplerType::SAMPLER_3D or it has no effect
     */
    /**
     * 自动生成所有 mipmap 级别。这要求纹理具有
     * 可渲染颜色格式，并且使用方式设置为 BLIT_SRC | BLIT_DST。如果未指定，
     * 使用位会自动设置。
     *
     * @param engine        此纹理关联的 Engine
     *
     * @attention \p engine 必须是传递给 Builder::build() 的实例
     * @attention 此 Texture 实例不得使用 SamplerType::SAMPLER_3D，否则无效
     */
    void generateMipmaps(Engine& engine) const noexcept;

    /** @deprecated */
    /**
     * @deprecated 已弃用
     * 立方体贴图所有面的偏移量结构体
     */
    struct FaceOffsets {
        using size_type = size_t;
        union {
            struct {
                size_type px;   //!< +x face offset in bytes
                /**
                 * +x 面的偏移量（字节）
                 */
                size_type nx;   //!< -x face offset in bytes
                /**
                 * -x 面的偏移量（字节）
                 */
                size_type py;   //!< +y face offset in bytes
                /**
                 * +y 面的偏移量（字节）
                 */
                size_type ny;   //!< -y face offset in bytes
                /**
                 * -y 面的偏移量（字节）
                 */
                size_type pz;   //!< +z face offset in bytes
                /**
                 * +z 面的偏移量（字节）
                 */
                size_type nz;   //!< -z face offset in bytes
                /**
                 * -z 面的偏移量（字节）
                 */
            };
            size_type offsets[6];
            /**
             * 六个面的偏移量数组
             */
        };
        size_type  operator[](size_t n) const noexcept { return offsets[n]; }
        size_type& operator[](size_t n) { return offsets[n]; }
        FaceOffsets() noexcept = default;
        explicit FaceOffsets(size_type faceSize) noexcept {
            px = faceSize * 0;
            nx = faceSize * 1;
            py = faceSize * 2;
            ny = faceSize * 3;
            pz = faceSize * 4;
            nz = faceSize * 5;
        }
        FaceOffsets(const FaceOffsets& rhs) noexcept {
            px = rhs.px;
            nx = rhs.nx;
            py = rhs.py;
            ny = rhs.ny;
            pz = rhs.pz;
            nz = rhs.nz;
        }
        FaceOffsets& operator=(const FaceOffsets& rhs) noexcept {
            px = rhs.px;
            nx = rhs.nx;
            py = rhs.py;
            ny = rhs.ny;
            pz = rhs.pz;
            nz = rhs.nz;
            return *this;
        }
    };

protected:
    // prevent heap allocation
    ~Texture() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_TEXTURE_H
