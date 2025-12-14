/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_RENDERTARGET_H
#define TNT_FILAMENT_RENDERTARGET_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>
#include <backend/TargetBufferInfo.h>

#include <utils/compiler.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FRenderTarget;

class Engine;
class Texture;

/**
 * An offscreen render target that can be associated with a View and contains
 * weak references to a set of attached Texture objects.
 *
 * RenderTarget is intended to be used with the View's post-processing disabled for the most part.
 * especially when a DEPTH attachment is also used (see Builder::texture()).
 *
 * Custom RenderTarget are ultimately intended to render into textures that might be used during
 * the main render pass.
 *
 * Clients are responsible for the lifetime of all associated Texture attachments.
 *
 * @see View
 */
/**
 * 可以与 View 关联的离屏渲染目标，包含
 * 对一组附加 Texture 对象的弱引用。
 *
 * RenderTarget 主要旨在与禁用后处理的 View 一起使用，
 * 特别是在还使用 DEPTH 附件时（参见 Builder::texture()）。
 *
 * 自定义 RenderTarget 最终旨在渲染到可能在使用
 * 主渲染通道期间使用的纹理中。
 *
 * 客户端负责所有关联 Texture 附件的生命周期。
 *
 * @see View
 */
class UTILS_PUBLIC RenderTarget : public FilamentAPI {
    struct BuilderDetails;

public:
    using CubemapFace = backend::TextureCubemapFace;
    /**
     * 立方体贴图面类型
     */

    /** Minimum number of color attachment supported */
    /**
     * 支持的最小颜色附件数量
     */
    static constexpr uint8_t MIN_SUPPORTED_COLOR_ATTACHMENTS_COUNT =
            backend::MRT::MIN_SUPPORTED_RENDER_TARGET_COUNT;

    /** Maximum number of color attachment supported */
    /**
     * 支持的最大颜色附件数量
     */
    static constexpr uint8_t MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT =
            backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT;

    /**
     * Attachment identifiers
     */
    /**
     * 附件标识符
     */
    enum class AttachmentPoint : uint8_t {
        COLOR0 = 0,          //!< identifies the 1st color attachment
        /** 标识第 1 个颜色附件 */
        COLOR1 = 1,          //!< identifies the 2nd color attachment
        /** 标识第 2 个颜色附件 */
        COLOR2 = 2,          //!< identifies the 3rd color attachment
        /** 标识第 3 个颜色附件 */
        COLOR3 = 3,          //!< identifies the 4th color attachment
        /** 标识第 4 个颜色附件 */
        COLOR4 = 4,          //!< identifies the 5th color attachment
        /** 标识第 5 个颜色附件 */
        COLOR5 = 5,          //!< identifies the 6th color attachment
        /** 标识第 6 个颜色附件 */
        COLOR6 = 6,          //!< identifies the 7th color attachment
        /** 标识第 7 个颜色附件 */
        COLOR7 = 7,          //!< identifies the 8th color attachment
        /** 标识第 8 个颜色附件 */
        DEPTH  = MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT,   //!< identifies the depth attachment
        /** 标识深度附件 */
        COLOR  = COLOR0,     //!< identifies the 1st color attachment
        /** 标识第 1 个颜色附件 */
    };

    //! Use Builder to construct a RenderTarget object instance
    /**
     * 使用 Builder 构造 RenderTarget 对象实例
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
         * Sets a texture to a given attachment point.
         *
         * When using a DEPTH attachment, it is important to always disable post-processing
         * in the View. Failing to do so will cause the DEPTH attachment to be ignored in most
         * cases.
         *
         * When the intention is to keep the content of the DEPTH attachment after rendering,
         * Usage::SAMPLEABLE must be set on the DEPTH attachment, otherwise the content of the
         * DEPTH buffer may be discarded.
         *
         * @param attachment The attachment point of the texture.
         * @param texture The associated texture object.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 将纹理设置到给定的附件点
         *
         * 使用 DEPTH 附件时，在 View 中始终禁用后处理很重要。
         * 否则，在大多数情况下会导致 DEPTH 附件被忽略。
         *
         * 如果意图是在渲染后保留 DEPTH 附件的内容，
         * 必须在 DEPTH 附件上设置 Usage::SAMPLEABLE，否则
         * DEPTH 缓冲区的内容可能被丢弃。
         *
         * @param attachment 纹理的附件点
         * @param texture 关联的纹理对象
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& texture(AttachmentPoint attachment, Texture* UTILS_NULLABLE texture) noexcept;

        /**
         * Sets the mipmap level for a given attachment point.
         *
         * @param attachment The attachment point of the texture.
         * @param level The associated mipmap level, 0 by default.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 为给定的附件点设置 mipmap 级别
         *
         * @param attachment 纹理的附件点
         * @param level 关联的 mipmap 级别，默认为 0
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& mipLevel(AttachmentPoint attachment, uint8_t level) noexcept;

        /**
         * Sets the face for cubemap textures at the given attachment point.
         *
         * @param attachment The attachment point.
         * @param face The associated cubemap face.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 为给定附件点的立方体贴图纹理设置面
         *
         * @param attachment 附件点
         * @param face 关联的立方体贴图面
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& face(AttachmentPoint attachment, CubemapFace face) noexcept;

        /**
         * Sets an index of a single layer for 2d array, cubemap array, and 3d textures at the given
         * attachment point.
         *
         * For cubemap array textures, layer is translated into an array index and face according to
         *  - index: layer / 6
         *  - face: layer % 6
         *
         * @param attachment The attachment point.
         * @param layer The associated cubemap layer.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 为给定附件点的 2D 数组、立方体贴图数组和 3D 纹理设置单个层的索引
         *
         * 对于立方体贴图数组纹理，层根据以下公式转换为数组索引和面：
         *  - index: layer / 6
         *  - face: layer % 6
         *
         * @param attachment 附件点
         * @param layer 关联的立方体贴图层
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& layer(AttachmentPoint attachment, uint32_t layer) noexcept;

        /**
         * Sets the starting index of the 2d array textures for multiview at the given attachment
         * point.
         *
         * This requires COLOR and DEPTH attachments (if set) to be of 2D array textures.
         *
         * @param attachment The attachment point.
         * @param layerCount The number of layers used for multiview, starting from baseLayer.
         * @param baseLayer The starting index of the 2d array texture.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 为给定附件点的多视图设置 2D 数组纹理的起始索引
         *
         * 这要求 COLOR 和 DEPTH 附件（如果设置）必须是 2D 数组纹理。
         *
         * @param attachment 附件点
         * @param layerCount 用于多视图的层数，从 baseLayer 开始
         * @param baseLayer 2D 数组纹理的起始索引
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& multiview(AttachmentPoint attachment, uint8_t layerCount, uint8_t baseLayer = 0) noexcept;

        /**
         * Sets the number of samples used for MSAA (Multisample Anti-Aliasing).
         *
         * @param samples The number of samples used for multisampling.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 设置用于 MSAA（多重采样抗锯齿）的采样数
         *
         * @param samples 用于多重采样的采样数
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& samples(uint8_t samples) noexcept;

        /**
         * Creates the RenderTarget object and returns a pointer to it.
         *
         * @return pointer to the newly created object.
         */
        /**
         * 创建 RenderTarget 对象并返回指向它的指针
         *
         * @return 指向新创建对象的指针
         */
        RenderTarget* UTILS_NONNULL build(Engine& engine);

    private:
        friend class FRenderTarget;
    };

    /**
     * Gets the texture set on the given attachment point
     * @param attachment Attachment point
     * @return A Texture object or nullptr if no texture is set for this attachment point
     */
    /**
     * 获取在给定附件点上设置的纹理
     * @param attachment 附件点
     * @return Texture 对象，如果此附件点未设置纹理，则返回 nullptr
     */
    Texture* UTILS_NULLABLE getTexture(AttachmentPoint attachment) const noexcept;

    /**
     * Returns the mipmap level set on the given attachment point
     * @param attachment Attachment point
     * @return the mipmap level set on the given attachment point
     */
    /**
     * 返回在给定附件点上设置的 mipmap 级别
     * @param attachment 附件点
     * @return 在给定附件点上设置的 mipmap 级别
     */
    uint8_t getMipLevel(AttachmentPoint attachment) const noexcept;

    /**
     * Returns the face of a cubemap set on the given attachment point
     * @param attachment Attachment point
     * @return A cubemap face identifier. This is only relevant if the attachment's texture is
     * a cubemap.
     */
    /**
     * 返回在给定附件点上设置的立方体贴图的面
     * @param attachment 附件点
     * @return 立方体贴图面标识符。仅当附件的纹理是
     * 立方体贴图时相关。
     */
    CubemapFace getFace(AttachmentPoint attachment) const noexcept;

    /**
     * Returns the texture-layer set on the given attachment point
     * @param attachment Attachment point
     * @return A texture layer. This is only relevant if the attachment's texture is a 3D texture.
     */
    /**
     * 返回在给定附件点上设置的纹理层
     * @param attachment 附件点
     * @return 纹理层。仅当附件的纹理是 3D 纹理时相关。
     */
    uint32_t getLayer(AttachmentPoint attachment) const noexcept;

    /**
     * Returns the number of color attachments usable by this instance of Engine. This method is
     * guaranteed to return at least MIN_SUPPORTED_COLOR_ATTACHMENTS_COUNT and at most
     * MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT.
     * @return Number of color attachments usable in a render target.
     */
    /**
     * 返回此 Engine 实例可用的颜色附件数量。此方法保证
     * 返回至少 MIN_SUPPORTED_COLOR_ATTACHMENTS_COUNT，最多
     * MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT。
     * @return 渲染目标中可用的颜色附件数量
     */
    uint8_t getSupportedColorAttachmentsCount() const noexcept;

protected:
    // prevent heap allocation
    ~RenderTarget() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_RENDERTARGET_H
