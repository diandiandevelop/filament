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

#include "details/RenderTarget.h"

#include "details/Engine.h"
#include "details/Texture.h"

#include "FilamentAPI-impl.h"

#include <filament/RenderTarget.h>

#include <utils/compiler.h>
#include <utils/BitmaskEnum.h>
#include <utils/Panic.h>

#include <algorithm>
#include <iterator>
#include <limits>

#include <stdint.h>
#include <stddef.h>


namespace filament {

using namespace backend;

/**
 * 构建器详情结构
 * 
 * 存储渲染目标的构建参数。
 */
struct RenderTarget::BuilderDetails {
    FRenderTarget::Attachment mAttachments[FRenderTarget::ATTACHMENT_COUNT] = {};  // 附件数组
    uint32_t mWidth{};  // 宽度
    uint32_t mHeight{};  // 高度
    uint8_t mSamples = 1;  // 采样数（默认 1，用于 MSAA）
    /**
     * 渲染目标的层数。值应该为 1，除非使用多视图。
     * 如果启用多视图，此值会根据每个附件的 layerCount 值适当更新。
     * 因此，值 >1 表示使用多视图。
     */
    // The number of layers for the render target. The value should be 1 except for multiview.
    // If multiview is enabled, this value is appropriately updated based on the layerCount value
    // from each attachment. Hence, #>1 means using multiview
    uint8_t mLayerCount = 1;  // 层数量（默认 1）
};

/**
 * 构建器类型别名
 */
using BuilderType = RenderTarget;

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
 * 设置纹理
 * 
 * 为指定附件点设置纹理。
 * 
 * @param pt 附件点
 * @param texture 纹理指针
 * @return 构建器引用（支持链式调用）
 */
RenderTarget::Builder& RenderTarget::Builder::texture(AttachmentPoint pt, Texture* texture) noexcept {
    mImpl->mAttachments[(size_t)pt].texture = downcast(texture);  // 设置纹理
    return *this;  // 返回自身引用
}

/**
 * 设置 Mip 级别
 * 
 * 为指定附件点设置 Mip 级别。
 * 
 * @param pt 附件点
 * @param level Mip 级别
 * @return 构建器引用（支持链式调用）
 */
RenderTarget::Builder& RenderTarget::Builder::mipLevel(AttachmentPoint pt, uint8_t const level) noexcept {
    mImpl->mAttachments[(size_t)pt].mipLevel = level;  // 设置 Mip 级别
    return *this;  // 返回自身引用
}

/**
 * 设置立方体贴图面
 * 
 * 为指定附件点设置立方体贴图面。
 * 
 * @param pt 附件点
 * @param face 立方体贴图面
 * @return 构建器引用（支持链式调用）
 */
RenderTarget::Builder& RenderTarget::Builder::face(AttachmentPoint pt, CubemapFace const face) noexcept {
    mImpl->mAttachments[(size_t)pt].face = face;  // 设置面
    return *this;  // 返回自身引用
}

/**
 * 设置层
 * 
 * 为指定附件点设置层索引（用于数组纹理）。
 * 
 * @param pt 附件点
 * @param layer 层索引
 * @return 构建器引用（支持链式调用）
 */
RenderTarget::Builder& RenderTarget::Builder::layer(AttachmentPoint pt, uint32_t const layer) noexcept {
    mImpl->mAttachments[(size_t)pt].layer = layer;  // 设置层索引
    return *this;  // 返回自身引用
}

/**
 * 设置多视图
     * 
     * 为指定附件点设置多视图参数。
     * 
     * @param pt 附件点
     * @param layerCount 层数量
     * @param baseLayer 基础层索引（默认为 0）
     * @return 构建器引用（支持链式调用）
     */
RenderTarget::Builder& RenderTarget::Builder::multiview(AttachmentPoint pt, uint8_t const layerCount,
        uint8_t const baseLayer/*= 0*/) noexcept {
    mImpl->mAttachments[(size_t)pt].layer = baseLayer;  // 设置基础层
    mImpl->mAttachments[(size_t)pt].layerCount = layerCount;  // 设置层数量
    return *this;  // 返回自身引用
}

/**
 * 设置采样数
 * 
 * 设置渲染目标的采样数（用于 MSAA）。
 * 
 * @param samples 采样数
 * @return 构建器引用（支持链式调用）
 */
RenderTarget::Builder& RenderTarget::Builder::samples(uint8_t samples) noexcept {
    mImpl->mSamples = samples;  // 设置采样数
    return *this;  // 返回自身引用
}

/**
 * 构建渲染目标
 * 
 * 根据构建器配置创建渲染目标。
 * 
 * @param engine 引擎引用
 * @return 渲染目标指针
 */
RenderTarget* RenderTarget::Builder::build(Engine& engine) {
    using backend::TextureUsage;  // 纹理使用方式类型别名
    const FRenderTarget::Attachment& color = mImpl->mAttachments[(size_t)AttachmentPoint::COLOR0];  // 获取颜色附件
    const FRenderTarget::Attachment& depth = mImpl->mAttachments[(size_t)AttachmentPoint::DEPTH];  // 获取深度附件

    /**
     * 验证颜色附件
     */
    if (color.texture) {  // 如果有颜色纹理
        FILAMENT_CHECK_PRECONDITION(color.texture->getUsage() & TextureUsage::COLOR_ATTACHMENT)  // 检查使用方式
                << "Texture usage must contain COLOR_ATTACHMENT";
        FILAMENT_CHECK_PRECONDITION(color.texture->getTarget() != Texture::Sampler::SAMPLER_EXTERNAL)  // 检查目标类型
                << "Color attachment can't be an external texture";
    }

    /**
     * 验证深度附件
     */
    if (depth.texture) {  // 如果有深度纹理
        FILAMENT_CHECK_PRECONDITION(depth.texture->getUsage() & TextureUsage::DEPTH_ATTACHMENT)  // 检查使用方式
                << "Texture usage must contain DEPTH_ATTACHMENT";
        FILAMENT_CHECK_PRECONDITION(depth.texture->getTarget() != Texture::Sampler::SAMPLER_EXTERNAL)  // 检查目标类型
                        << "Depth attachment can't be an external texture";
    }

    /**
     * 验证颜色附件数量不超过支持的最大值
     */
    const size_t maxDrawBuffers = downcast(engine).getDriverApi().getMaxDrawBuffers();  // 获取最大绘制缓冲区数
    for (size_t i = maxDrawBuffers; i < MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT; i++) {  // 遍历超出支持范围的附件
        FILAMENT_CHECK_PRECONDITION(!mImpl->mAttachments[i].texture)  // 检查是否设置了超出范围的附件
                << "Only " << maxDrawBuffers << " color attachments are supported, but COLOR" << i
                << " attachment is set";
    }
    
    /**
     * 计算所有附件的尺寸范围
     */
    uint32_t minWidth = std::numeric_limits<uint32_t>::max();  // 最小宽度
    uint32_t maxWidth = 0;  // 最大宽度
    uint32_t minHeight = std::numeric_limits<uint32_t>::max();  // 最小高度
    uint32_t maxHeight = 0;  // 最大高度
    uint32_t minLayerCount = std::numeric_limits<uint32_t>::max();  // 最小层数量
    uint32_t maxLayerCount = 0;  // 最大层数量
    for (auto const& attachment : mImpl->mAttachments) {  // 遍历所有附件
        if (attachment.texture) {  // 如果有纹理
            const uint32_t w = attachment.texture->getWidth(attachment.mipLevel);  // 获取宽度
            const uint32_t h = attachment.texture->getHeight(attachment.mipLevel);  // 获取高度
            const uint32_t d = attachment.texture->getDepth(attachment.mipLevel);  // 获取深度
            const uint32_t l = attachment.layerCount;  // 获取层数量
            if (l > 0) {  // 如果使用多视图
                FILAMENT_CHECK_PRECONDITION(
                        attachment.texture->getTarget() == Texture::Sampler::SAMPLER_2D_ARRAY)  // 检查目标类型
                        << "Texture sampler must be of 2d array for multiview";
            }
            FILAMENT_CHECK_PRECONDITION(attachment.layer + l <= d)  // 检查层索引和数量
                    << "layer + layerCount cannot exceed the number of depth";
            minWidth  = std::min(minWidth, w);  // 更新最小宽度
            minHeight = std::min(minHeight, h);  // 更新最小高度
            minLayerCount = std::min(minLayerCount, l);  // 更新最小层数量
            maxWidth  = std::max(maxWidth, w);  // 更新最大宽度
            maxHeight = std::max(maxHeight, h);  // 更新最大高度
            maxLayerCount = std::max(maxLayerCount, l);  // 更新最大层数量
        }
    }

    /**
     * 验证所有附件的尺寸必须匹配
     */
    FILAMENT_CHECK_PRECONDITION(minWidth == maxWidth && minHeight == maxHeight
            && minLayerCount == maxLayerCount) << "All attachments dimensions must match";  // 检查尺寸是否匹配

    mImpl->mWidth  = minWidth;  // 设置宽度
    mImpl->mHeight = minHeight;  // 设置高度
    if (minLayerCount > 0) {  // 如果使用多视图
        /**
         * mLayerCount 应该为 1，除非使用多视图，在这种情况下我们将此变量
         * 更新为多视图的 layerCount 数量。
         */
        // mLayerCount should be 1 except for multiview use where we update this variable
        // to the number of layerCount for multiview.
        mImpl->mLayerCount = minLayerCount;  // 设置层数量
    }
    return downcast(engine).createRenderTarget(*this);  // 创建渲染目标
}

// ------------------------------------------------------------------------------------------------

/**
 * 渲染目标构造函数
 * 
 * 创建渲染目标并初始化所有附件。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FRenderTarget::FRenderTarget(FEngine& engine, const Builder& builder)
    : mSupportedColorAttachmentsCount(engine.getDriverApi().getMaxDrawBuffers()),  // 初始化支持的颜色附件数量
      mSupportsReadPixels(false) {  // 初始化不支持读取像素
    /**
     * 复制附件数组
     */
    std::copy(std::begin(builder.mImpl->mAttachments), std::end(builder.mImpl->mAttachments),
            std::begin(mAttachments));  // 复制附件

    MRT mrt{};  // 多渲染目标结构
    TargetBufferInfo dinfo{};  // 深度缓冲区信息

    /**
     * Lambda 函数：设置附件信息
     * 
     * 将附件信息设置到目标缓冲区信息结构中。
     */
    auto setAttachment = [this, &driver = engine.getDriverApi()]
            (TargetBufferInfo& info, AttachmentPoint attachmentPoint) {
        Attachment const& attachment = mAttachments[(size_t)attachmentPoint];  // 获取附件
        auto t = downcast(attachment.texture);  // 转换为实现类指针
        info.handle = t->getHwHandle();  // 设置硬件句柄
        info.level  = attachment.mipLevel;  // 设置 Mip 级别
        if (t->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP) {  // 如果是立方体贴图
            info.layer = +attachment.face;  // 使用面索引作为层
        } else {  // 否则
            info.layer = attachment.layer;  // 使用层索引
        }
        t->updateLodRange(info.level);  // 更新 LOD 范围
    };

    /**
     * 处理所有颜色附件
     */
    UTILS_NOUNROLL  // 不展开循环提示
    for (size_t i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {  // 遍历所有颜色附件
        Attachment const& attachment = mAttachments[i];  // 获取附件
        if (attachment.texture) {  // 如果有纹理
            TargetBufferFlags const targetBufferBit = getTargetBufferFlagsAt(i);  // 获取目标缓冲区标志位
            mAttachmentMask |= targetBufferBit;  // 累积附件掩码
            setAttachment(mrt[i], (AttachmentPoint)i);  // 设置附件信息
            /**
             * 如果纹理可采样或用于子通道输入，标记为可采样
             */
            if (any(attachment.texture->getUsage() &
                    (TextureUsage::SAMPLEABLE | Texture::Usage::SUBPASS_INPUT))) {
                mSampleableAttachmentsMask |= targetBufferBit;  // 累积可采样附件掩码
            }

            /**
             * readPixels() 仅适用于绑定在索引 0 的颜色附件
             */
            // readPixels() only applies to the color attachment that binds at index 0.
            if (i == 0 && any(attachment.texture->getUsage() & TextureUsage::COLOR_ATTACHMENT)) {  // 如果是第一个颜色附件

                /**
                 * TODO: 以下内容将在以后的 Filament 版本中更改为：
                 *    mSupportsReadPixels =
                 *            any(attachment.texture->getUsage() & TextureUsage::BLIT_SRC);
                 *    当客户端正确添加了正确的使用方式时。
                 */
                // TODO: the following will be changed to
                //    mSupportsReadPixels =
                //            any(attachment.texture->getUsage() & TextureUsage::BLIT_SRC);
                //    in a later filament version when clients have properly added the right usage.
                mSupportsReadPixels = attachment.texture->hasBlitSrcUsage();  // 检查是否有 Blit 源使用方式
            }
        }
    }

    /**
     * 处理深度附件
     */
    Attachment const& depthAttachment = mAttachments[(size_t)AttachmentPoint::DEPTH];  // 获取深度附件
    if (depthAttachment.texture) {  // 如果有深度纹理
        mAttachmentMask |= TargetBufferFlags::DEPTH;  // 累积深度标志
        setAttachment(dinfo, AttachmentPoint::DEPTH);  // 设置深度附件信息
        /**
         * 如果深度纹理可采样或用于子通道输入，标记为可采样
         */
        if (any(depthAttachment.texture->getUsage() &
                (TextureUsage::SAMPLEABLE | Texture::Usage::SUBPASS_INPUT))) {
            mSampleableAttachmentsMask |= TargetBufferFlags::DEPTH;  // 累积可采样深度标志
        }
    }

    /**
     * TODO: 当我们支持模板缓冲区时，在这里添加
     */
    // TODO: add stencil here when we support it

    /**
     * 创建渲染目标
     */
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    mHandle = driver.createRenderTarget(mAttachmentMask,  // 附件掩码
            builder.mImpl->mWidth, builder.mImpl->mHeight, builder.mImpl->mSamples,  // 宽度、高度、采样数
            builder.mImpl->mLayerCount, mrt, dinfo, {},  // 层数量、多渲染目标、深度信息、模板信息
            utils::ImmutableCString{ builder.getName() });  // 名称
}

/**
 * 终止渲染目标
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FRenderTarget::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyRenderTarget(mHandle);  // 销毁渲染目标
}

/**
 * 检查是否有可采样的深度附件
 * 
 * @return 如果有可采样的深度附件返回 true，否则返回 false
 */
bool FRenderTarget::hasSampleableDepth() const noexcept {
    const FTexture* depth = mAttachments[(size_t)AttachmentPoint::DEPTH].texture;  // 获取深度纹理
    return depth && (depth->getUsage() & TextureUsage::SAMPLEABLE) == TextureUsage::SAMPLEABLE;  // 检查是否可采样
}

} // namespace filament
