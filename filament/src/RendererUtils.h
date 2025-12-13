/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_RENDERERUTILS_H
#define TNT_FILAMENT_DETAILS_RENDERERUTILS_H

#include "PostProcessManager.h"
#include "RenderPass.h"

#include "fg/FrameGraphId.h"
#include "fg/FrameGraphTexture.h"

#include <filament/Viewport.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <math/vec2.h>
#include <math/vec4.h>

#include <stdint.h>

namespace filament {

namespace backend {
class PixelBufferDescriptor;
}

class FRenderTarget;
class FrameGraph;
class FrameGraph;
class FView;

/**
 * 渲染器工具类
 * 
 * 提供渲染相关的工具函数，主要用于颜色通道和折射通道的渲染。
 */
class RendererUtils {
public:

    /**
     * 颜色通道配置
     */
    struct ColorPassConfig {
        Viewport physicalViewport;  // 渲染视口（例如：动态分辨率缩小的视口）
        Viewport logicalViewport;  // 逻辑视口（例如：当有保护带时左下角非零），原点相对于 physicalViewport
        math::float2 scale;  // 动态分辨率缩放
        backend::TextureFormat hdrFormat;  // HDR 格式
        uint8_t msaa;  // MSAA 采样数
        backend::TargetBufferFlags clearFlags;  // 清除标志
        math::float4 clearColor = {};  // 清除颜色
        uint8_t clearStencil = 0u;  // 清除模板值
        bool hasContactShadows;  // 是否启用接触阴影
        bool hasScreenSpaceReflectionsOrRefractions;  // 是否启用屏幕空间反射或折射
        bool enabledStencilBuffer;  // 使用带模板分量的深度格式
    };

    /**
     * 颜色通道输入
     */
    struct ColorPassInput {
        FrameGraphId<FrameGraphTexture> linearColor;  // 线性颜色
        FrameGraphId<FrameGraphTexture> tonemappedColor;  // 色调映射后的颜色
        FrameGraphId<FrameGraphTexture> depth;  // 深度
        FrameGraphId<FrameGraphTexture> shadows;  // 阴影
        FrameGraphId<FrameGraphTexture> ssao;  // 屏幕空间环境光遮蔽
        FrameGraphId<FrameGraphTexture> ssr;  // 屏幕空间反射
        FrameGraphId<FrameGraphTexture> structure;  // 结构信息
    };
    
    /**
     * 颜色通道输出
     */
    struct ColorPassOutput {
        FrameGraphId<FrameGraphTexture> linearColor;  // 线性颜色
        FrameGraphId<FrameGraphTexture> tonemappedColor;  // 色调映射后的颜色
        FrameGraphId<FrameGraphTexture> depth;  // 深度
    };

    /**
     * 颜色通道
     * 
     * 执行颜色通道渲染。
     * 
     * @param fg 帧图
     * @param name 通道名称
     * @param engine 引擎引用
     * @param view 视图引用
     * @param colorPassInput 颜色通道输入
     * @param colorBufferDesc 颜色缓冲区描述符
     * @param config 配置
     * @param colorGradingConfig 颜色分级配置
     * @param passExecutor 通道执行器
     * @return 颜色通道输出
     */
    static ColorPassOutput colorPass(
            FrameGraph& fg, const char* name, FEngine& engine, FView const& view,
            ColorPassInput const& colorPassInput,
            FrameGraphTexture::Descriptor const& colorBufferDesc,
            ColorPassConfig const& config,
            PostProcessManager::ColorGradingConfig colorGradingConfig,
            RenderPass::Executor passExecutor) noexcept;

    /**
     * 折射通道
     * 
     * 执行折射通道渲染。
     * 
     * @param fg 帧图
     * @param engine 引擎引用
     * @param view 视图引用
     * @param colorPassInput 颜色通道输入
     * @param config 配置
     * @param ssrConfig 屏幕空间反射配置
     * @param colorGradingConfig 颜色分级配置
     * @param pass 渲染通道
     * @param firstRefractionCommand 第一个折射命令
     * @return 颜色通道输出
     */
    static ColorPassOutput refractionPass(
            FrameGraph& fg, FEngine& engine, FView const& view,
            ColorPassInput colorPassInput,
            ColorPassConfig config,
            PostProcessManager::ScreenSpaceRefConfig const& ssrConfig,
            PostProcessManager::ColorGradingConfig colorGradingConfig,
            RenderPass const& pass, RenderPass::Command const* firstRefractionCommand) noexcept;

    /**
     * 读取像素
     * 
     * 从渲染目标读取像素数据。
     * 
     * @param driver 驱动 API 引用
     * @param renderTargetHandle 渲染目标句柄
     * @param xoffset X 偏移
     * @param yoffset Y 偏移
     * @param width 宽度
     * @param height 高度
     * @param buffer 像素缓冲区描述符
     */
    static void readPixels(backend::DriverApi& driver,
            backend::Handle<backend::HwRenderTarget> renderTargetHandle,
            uint32_t xoffset, uint32_t yoffset, uint32_t width, uint32_t height,
            backend::PixelBufferDescriptor&& buffer);

    /**
     * 获取第一个折射命令
     * 
     * @param pass 渲染通道
     * @return 第一个折射命令指针
     */
    static RenderPass::Command const* getFirstRefractionCommand(RenderPass const& pass) noexcept;
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_RENDERERUTILS_H
