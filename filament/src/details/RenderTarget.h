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

#ifndef TNT_FILAMENT_DETAILS_RENDERTAGET_H
#define TNT_FILAMENT_DETAILS_RENDERTAGET_H

#include "downcast.h"

#include <backend/Handle.h>

#include <filament/RenderTarget.h>

#include <utils/compiler.h>

namespace filament {

class FEngine;
class FTexture;

/**
 * 渲染目标实现类
 * 
 * 管理渲染目标，定义渲染输出的位置。
 * 渲染目标可以附加多个颜色附件和一个深度/模板附件。
 */
class FRenderTarget : public RenderTarget {
public:
    using HwHandle = backend::Handle<backend::HwRenderTarget>;  // 硬件句柄类型别名

    /**
     * 附件结构
     * 
     * 描述渲染目标的一个附件。
     */
    struct Attachment {
        FTexture* texture = nullptr;  // 纹理指针
        uint8_t mipLevel = 0;  // Mip 级别
        CubemapFace face = CubemapFace::POSITIVE_X;  // 立方体贴图面（如果是立方体贴图）
        uint32_t layer = 0;  // 层索引（用于数组纹理或多视图）
        /**
         * 指示用于多视图的层数，从 `layer`（baseIndex）开始。
         * 这意味着 `layer` + `layerCount` 不能超过附件的深度数量。
         */
        // Indicates the number of layers used for multiview, starting from the `layer` (baseIndex).
        // This means `layer` + `layerCount` cannot exceed the number of depth for the attachment.
        uint16_t layerCount = 0;  // 层数量
    };

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FRenderTarget(FEngine& engine, const Builder& builder);

    /**
     * 终止渲染目标
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 获取硬件句柄
     * 
     * @return 渲染目标硬件句柄
     */
    HwHandle getHwHandle() const noexcept { return mHandle; }

    /**
     * 获取附件
     * 
     * 获取指定附件点的附件信息。
     * 
     * @param attachment 附件点
     * @return 附件结构
     */
    Attachment getAttachment(AttachmentPoint attachment) const noexcept {
        return mAttachments[(int) attachment];  // 返回附件
    }

    /**
     * 获取附件掩码
     * 
     * 返回所有已附加的缓冲区标志。
     * 
     * @return 附件掩码
     */
    backend::TargetBufferFlags getAttachmentMask() const noexcept {
        return mAttachmentMask;  // 返回附件掩码
    }

    /**
     * 获取可采样附件掩码
     * 
     * 返回所有可采样的附件标志。
     * 
     * @return 可采样附件掩码
     */
    backend::TargetBufferFlags getSampleableAttachmentsMask() const noexcept {
        return mSampleableAttachmentsMask;  // 返回可采样附件掩码
    }

    /**
     * 获取支持的颜色附件数量
     * 
     * @return 支持的颜色附件数量
     */
    uint8_t getSupportedColorAttachmentsCount() const noexcept {
        return mSupportedColorAttachmentsCount;  // 返回支持的颜色附件数量
    }

    /**
     * 检查是否有可采样的深度附件
     * 
     * @return 如果有可采样的深度附件返回 true，否则返回 false
     */
    bool hasSampleableDepth() const noexcept;

    /**
     * 检查是否支持读取像素
     * 
     * @return 如果支持读取像素返回 true，否则返回 false
     */
    bool supportsReadPixels() const noexcept {
        return mSupportsReadPixels;  // 返回是否支持读取像素
    }

private:
    friend class RenderTarget;  // 允许 RenderTarget 访问私有成员
    
    /**
     * 附件数量
     * 
     * 最大支持的颜色附件数量 + 1（深度/模板附件）
     */
    static constexpr size_t ATTACHMENT_COUNT = MAX_SUPPORTED_COLOR_ATTACHMENTS_COUNT + 1u;
    
    Attachment mAttachments[ATTACHMENT_COUNT];  // 附件数组
    HwHandle mHandle{};  // 硬件句柄
    backend::TargetBufferFlags mAttachmentMask = {};  // 附件掩码
    backend::TargetBufferFlags mSampleableAttachmentsMask = {};  // 可采样附件掩码
    const uint8_t mSupportedColorAttachmentsCount;  // 支持的颜色附件数量（常量）
    bool mSupportsReadPixels = false;  // 是否支持读取像素
};

FILAMENT_DOWNCAST(RenderTarget)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_RENDERTARGET_H
