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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPHRENDERPASS_H
#define TNT_FILAMENT_FG_FRAMEGRAPHRENDERPASS_H

#include "fg/FrameGraphTexture.h"

#include <backend/DriverEnums.h>
#include <backend/TargetBufferInfo.h>

#include <filament/Viewport.h>
#include <utils/debug.h>

namespace filament {

/**
 * 帧图渲染通道结构
 * 
 * 用于绘制到一组 FrameGraphTexture 资源中。
 * 这些是瞬态对象，仅在通道内存在。
 * 
 * 功能：
 * - 定义渲染通道的附件（颜色、深度、模板）
 * - 指定视口、清除颜色等渲染参数
 * - 支持导入外部渲染目标
 */
struct FrameGraphRenderPass {
    /**
     * 附件数量
     * 
     * 最大支持的颜色附件数 + 深度 + 模板 = 总附件数
     */
    static constexpr size_t ATTACHMENT_COUNT = backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 2;
    
    /**
     * 附件结构
     * 
     * 包含颜色、深度和模板附件。
     */
    struct Attachments {
        FrameGraphId<FrameGraphTexture> color[backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];  // 颜色附件数组
        FrameGraphId<FrameGraphTexture> depth;  // 深度附件
        FrameGraphId<FrameGraphTexture> stencil;  // 模板附件

        /**
         * 下标操作符
         * 
         * 通过索引访问附件。
         * 
         * @param index 附件索引
         * @return 纹理 ID 引用
         */
        FrameGraphId<FrameGraphTexture>& operator[](size_t index) noexcept {
            assert_invariant(index < ATTACHMENT_COUNT);  // 断言索引有效
            if (index < backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT) {
                return color[index];  // 颜色附件
            } else if (index == backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT) {
                return depth;  // 深度附件
            } else {
                return stencil;  // 模板附件
            }
        }
    };

    /**
     * 渲染通道描述符
     * 
     * 描述渲染通道的配置。
     */
    struct Descriptor {
        Attachments attachments{};  // 附件
        Viewport viewport{};  // 视口
        math::float4 clearColor{};  // 清除颜色
        uint8_t samples = 0;    // 采样数（0 = 未设置，使用默认值）
        uint8_t layerCount = 1; // 层数（> 1 = 多视图）
        backend::TargetBufferFlags clearFlags{};  // 清除标志
        backend::TargetBufferFlags discardStart{};  // 开始丢弃标志
    };

    /**
     * 导入描述符
     * 
     * 用于导入外部渲染目标。
     * 某些字段会覆盖 Descriptor 中的对应字段。
     */
    struct ImportDescriptor {
        backend::TargetBufferFlags attachments = backend::TargetBufferFlags::COLOR0;  // 附件标志
        Viewport viewport{};  // 视口
        math::float4 clearColor{};  // 清除颜色（覆盖 Descriptor::clearColor）
        uint8_t samples = 0;        // 采样数（0 = 未设置，使用默认值）
        backend::TargetBufferFlags clearFlags{};  // 清除标志（覆盖 Descriptor::clearFlags）
        backend::TargetBufferFlags keepOverrideStart{};  // 开始保持覆盖标志
        backend::TargetBufferFlags keepOverrideEnd{};  // 结束保持覆盖标志
    };

    uint32_t id = 0;  // 渲染通道 ID
};

} // namespace filament

#endif // TNT_FILAMENT_FG_FRAMEGRAPHRENDERPASS_H
