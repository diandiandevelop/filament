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

#include "RendererUtils.h"

#include "PostProcessManager.h"

#include "details/Engine.h"
#include "details/View.h"

#include "ds/DescriptorSet.h"

#include "fg/FrameGraph.h"
#include "fg/FrameGraphId.h"
#include "fg/FrameGraphResources.h"
#include "fg/FrameGraphTexture.h"

#include <private/filament/EngineEnums.h>

#include <filament/Options.h>
#include <filament/RenderableManager.h>
#include <filament/Viewport.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PixelBufferDescriptor.h>

#include <utils/BitmaskEnum.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>

#include <algorithm>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 颜色通道实现
 * 
 * 设置颜色通道的帧图资源，包括阴影、SSAO、SSR、结构信息等。
 * 处理深度/模板缓冲区的创建和 MSAA 配置。
 */
RendererUtils::ColorPassOutput RendererUtils::colorPass(
        FrameGraph& fg, const char* name, FEngine& engine, FView const& view,
        ColorPassInput const& colorPassInput,
        FrameGraphTexture::Descriptor const& colorBufferDesc,
        ColorPassConfig const& config, PostProcessManager::ColorGradingConfig const colorGradingConfig,
        RenderPass::Executor passExecutor) noexcept {

    /**
     * 颜色通道数据
     */
    struct ColorPassData {
        FrameGraphId<FrameGraphTexture> shadows;  // 阴影
        FrameGraphId<FrameGraphTexture> color;  // 颜色
        FrameGraphId<FrameGraphTexture> output;  // 输出（色调映射后的颜色）
        FrameGraphId<FrameGraphTexture> depth;  // 深度
        FrameGraphId<FrameGraphTexture> stencil;  // 模板
        FrameGraphId<FrameGraphTexture> ssao;  // 屏幕空间环境光遮蔽
        FrameGraphId<FrameGraphTexture> ssr;  // 屏幕空间反射或折射
        FrameGraphId<FrameGraphTexture> structure;  // 结构信息
    };

    auto& colorPass = fg.addPass<ColorPassData>(name,
            [&](FrameGraph::Builder& builder, ColorPassData& data) {

                /**
                 * 提取清除标志：分别提取颜色、深度和模板缓冲区的清除标志
                 */
                TargetBufferFlags const clearColorFlags = config.clearFlags & TargetBufferFlags::COLOR;
                TargetBufferFlags clearDepthFlags = config.clearFlags & TargetBufferFlags::DEPTH;
                TargetBufferFlags clearStencilFlags = config.clearFlags & TargetBufferFlags::STENCIL;

                /**
                 * 从输入中获取已有的纹理资源
                 */
                data.color = colorPassInput.linearColor;
                data.depth = colorPassInput.depth;
                data.shadows = colorPassInput.shadows;
                data.ssao = colorPassInput.ssao;

                /**
                 * Screen-space reflection or refractions
                 * 屏幕空间反射或折射：如果需要，获取 SSR 纹理并设置为可采样
                 */
                if (config.hasScreenSpaceReflectionsOrRefractions) {
                    data.ssr = colorPassInput.ssr;
                    if (data.ssr) {
                        data.ssr = builder.sample(data.ssr);
                    }
                }

                /**
                 * 接触阴影需要结构信息纹理（用于深度测试）
                 */
                if (config.hasContactShadows) {
                    data.structure = colorPassInput.structure;
                    assert_invariant(data.structure);
                    data.structure = builder.sample(data.structure);
                }

                /**
                 * 设置阴影和 SSAO 纹理为可采样
                 */
                if (data.shadows) {
                    data.shadows = builder.sample(data.shadows);
                }

                if (data.ssao) {
                    data.ssao = builder.sample(data.ssao);
                }

                /**
                 * 如果颜色缓冲区不存在，创建新的颜色纹理
                 */
                if (!data.color) {
                    data.color = builder.createTexture("Color Buffer", colorBufferDesc);
                }

                /**
                 * 检查驱动是否支持深度自动解析（auto-resolve）
                 */
                const bool canAutoResolveDepth = engine.getDriverApi().isAutoDepthResolveSupported();

                FrameGraphTexture::Usage depthStencilUsage = FrameGraphTexture::Usage::DEPTH_ATTACHMENT;

                /**
                 * 如果深度缓冲区不存在，创建新的深度/模板纹理
                 */
                if (!data.depth) {
                    /**
                     * clear newly allocated depth/stencil buffers, regardless of given clear flags
                     * 清除新分配的深度/模板缓冲区，无论给定的清除标志如何
                     */
                    clearDepthFlags = TargetBufferFlags::DEPTH;
                    clearStencilFlags = config.enabledStencilBuffer ?
                            TargetBufferFlags::STENCIL : TargetBufferFlags::NONE;
                    utils::StaticString const textureName = config.enabledStencilBuffer ?
                            utils::StaticString{"Depth/Stencil Buffer"} :
                            utils::StaticString{"Depth Buffer"};

                    /**
                     * 根据功能级别选择深度格式：
                     * - ES2 (FEATURE_LEVEL_0): 使用 24 位深度
                     * - 更高级别: 使用 32 位浮点深度
                     */
                    bool const isES2 =
                            engine.getDriverApi().getFeatureLevel() == FeatureLevel::FEATURE_LEVEL_0;

                    TextureFormat const stencilFormat = isES2 ?
                            TextureFormat::DEPTH24_STENCIL8 : TextureFormat::DEPTH32F_STENCIL8;

                    TextureFormat const depthOnlyFormat = isES2 ?
                            TextureFormat::DEPTH24 : TextureFormat::DEPTH32F;

                    TextureFormat const format = config.enabledStencilBuffer ?
                            stencilFormat : depthOnlyFormat;

                    /**
                     * If the color attachment requested MS, we assume this means the MS
                     * buffer must be kept, and for that reason we allocate the depth
                     * buffer with MS as well.
                     * On the other hand, if the color attachment was allocated without
                     * MS, no need to allocate the depth buffer with MS; Either it's not
                     * multi-sampled or it is auto-resolved.
                     * One complication above is that some backends don't support
                     * depth auto-resolve, in which case we must allocate the depth
                     * buffer with MS and manually resolve it (see "Resolved Depth Buffer"
                     * pass).
                     * 
                     * 如果颜色附件请求了 MS（多重采样），我们假设这意味着必须保留 MS 缓冲区，
                     * 因此我们也为深度缓冲区分配 MS。
                     * 另一方面，如果颜色附件是在没有 MS 的情况下分配的，则不需要为深度缓冲区分配 MS；
                     * 要么它不是多重采样的，要么它是自动解析的。
                     * 上面的一个复杂情况是某些后端不支持深度自动解析，
                     * 在这种情况下，我们必须为深度缓冲区分配 MS 并手动解析它
                     * （请参阅"Resolved Depth Buffer"通道）。
                     */
                    data.depth = builder.createTexture(textureName, {
                            .width = colorBufferDesc.width,
                            .height = colorBufferDesc.height,
                            .depth = colorBufferDesc.depth,
                            .samples = canAutoResolveDepth ? colorBufferDesc.samples : uint8_t(config.msaa),
                            .type = colorBufferDesc.type,
                            .format = format,
                    });
                    /**
                     * 如果启用了模板缓冲区，将模板附件添加到使用标志中
                     */
                    if (config.enabledStencilBuffer) {
                        depthStencilUsage |= FrameGraphTexture::Usage::STENCIL_ATTACHMENT;
                        data.stencil = data.depth;
                    }
                }

                /**
                 * 如果颜色分级作为子通道（Vulkan 子通道），创建色调映射输出纹理
                 */
                if (colorGradingConfig.asSubpass) {
                    assert_invariant(config.msaa <= 1);
                    assert_invariant(colorBufferDesc.samples <= 1);
                    data.output = builder.createTexture("Tonemapped Buffer", {
                            .width = colorBufferDesc.width,
                            .height = colorBufferDesc.height,
                            .format = colorGradingConfig.ldrFormat
                    });
                    /**
                     * 设置颜色纹理为子通道输入，输出纹理为子通道颜色附件
                     */
                    data.color = builder.read(data.color, FrameGraphTexture::Usage::SUBPASS_INPUT);
                    data.output = builder.write(data.output, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                } else if (colorGradingConfig.customResolve) {
                    /**
                     * 自定义解析模式：设置颜色纹理为子通道输入
                     */
                    data.color = builder.read(data.color, FrameGraphTexture::Usage::SUBPASS_INPUT);
                }

                /**
                 * We set a "read" constraint on these attachments here because we need to preserve them
                 * when the color pass happens in several passes (e.g. with SSR)
                 * 
                 * 我们在这些附件上设置"读取"约束，因为当颜色通道在多个通道中发生
                 * （例如使用 SSR）时，我们需要保留它们
                 */
                data.color = builder.read(data.color, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.depth = builder.read(data.depth, depthStencilUsage);

                /**
                 * 设置写入约束：这些纹理将被写入
                 */
                data.color = builder.write(data.color, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.depth = builder.write(data.depth, depthStencilUsage);

                /*
                 * There is a bit of magic happening here regarding the viewport used.
                 * We do not specify the viewport in declareRenderPass() below, so it will be
                 * deduced automatically to be { 0, 0, w, h }, with w,h the min width/height of
                 * all the attachments. This has the side effect of moving the viewport to the
                 * origin and ignore the left/bottom of 'svp'. The attachment sizes are set from
                 * svp's width/height, however.
                 * But that's not all! When we're rendering directly into the swap-chain (by way
                 * of calling forwardResource() later), the effective viewport comes from the
                 * imported resource (i.e. the swap-chain) and is set to 'vp' which has its
                 * left/bottom honored -- the view is therefore rendered directly where it should
                 * be (the imported resource viewport is set to 'vp', see  how 'fgViewRenderTarget'
                 * is initialized in this file).
                 */
                builder.declareRenderPass("Color Pass Target", {
                        .attachments = { .color = { data.color, data.output },
                        .depth = data.depth,
                        .stencil = data.stencil },
                        .clearColor = config.clearColor,
                        .samples = config.msaa,
                        .layerCount = static_cast<uint8_t>(colorBufferDesc.depth),
                        .clearFlags = clearColorFlags | clearDepthFlags | clearStencilFlags});
            },
            [=, passExecutor = std::move(passExecutor), &view, &engine](FrameGraphResources const& resources,
                    ColorPassData const& data, DriverApi& driver) {
                auto out = resources.getRenderPassInfo();

                // set samplers and uniforms
                view.prepareSSAO(data.ssao ?
                        resources.getTexture(data.ssao) : engine.getOneTextureArray());

                // set screen-space reflections and screen-space refractions
                view.prepareSSR(data.ssr ?
                        resources.getTexture(data.ssr) : engine.getOneTextureArray());

                // set structure sampler
                view.prepareStructure(data.structure ?
                        resources.getTexture(data.structure) : engine.getOneTexture());

                // set shadow sampler
                view.prepareShadowMapping(engine,
                        data.shadows
                            ? resources.getTexture(data.shadows)
                            : (view.getShadowType() != ShadowType::PCF
                                   ? engine.getOneTextureArray()
                                   : engine.getOneTextureArrayDepth()));

                view.commitDescriptorSet(driver);

                // TODO: this should be a parameter of FrameGraphRenderPass::Descriptor
                out.params.clearStencil = config.clearStencil;
                if (view.getBlendMode() == BlendMode::TRANSLUCENT) {
                    if (any(out.params.flags.discardStart & TargetBufferFlags::COLOR0)) {
                        // if the buffer is discarded (e.g. it's new) and we're blending,
                        // then clear it to transparent
                        out.params.flags.clear |= TargetBufferFlags::COLOR;
                        out.params.clearColor = {};
                    }
                }

                if (colorGradingConfig.asSubpass || colorGradingConfig.customResolve) {
                    out.params.subpassMask = 1;
                }

                driver.beginRenderPass(out.target, out.params);
                passExecutor.execute(engine, driver);
                driver.endRenderPass();

                // unbind all descriptor sets to avoid false dependencies with the next pass
                DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_VIEW);
                DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_RENDERABLE);
                DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);
            }
    );

    return {
            .linearColor = colorPass->color,
            .tonemappedColor = colorPass->output,   // can be null
            .depth = colorPass->depth
    };
}


/**
 * 获取第一个折射命令
 * 
 * 在渲染通道中查找第一个折射对象（在默认通道 2 中）。
 * 使用二分查找定位第一个折射渲染命令。
 * 
 * @param pass 渲染通道引用
 * @return 如果找到折射命令则返回其指针，否则返回 nullptr
 */
RenderPass::Command const* RendererUtils::getFirstRefractionCommand(
    RenderPass const& pass) noexcept {

    /**
     * find the first refractive object in channel 2
     * 在通道 2 中查找第一个折射对象
     * 使用 std::partition_point 进行二分查找，查找第一个匹配条件的命令
     */
    RenderPass::Command const* const refraction = std::partition_point(pass.begin(), pass.end(),
        [](auto const& command) {
            /**
             * 构建掩码和查找值：查找默认通道中的折射（REFRACT）渲染命令
             */
            constexpr uint64_t mask  = RenderPass::CHANNEL_MASK | RenderPass::PASS_MASK;
            constexpr uint64_t channel = uint64_t(RenderableManager::Builder::DEFAULT_CHANNEL) << RenderPass::CHANNEL_SHIFT;
            constexpr uint64_t value = channel | uint64_t(RenderPass::Pass::REFRACT);
            return (command.key & mask) < value;
        });

    /**
     * 检查找到的命令是否真的是折射命令（屏幕空间折射）
     */
    const bool hasScreenSpaceRefraction =
            (refraction->key & RenderPass::PASS_MASK) == uint64_t(RenderPass::Pass::REFRACT);

    return hasScreenSpaceRefraction ? refraction : nullptr;

}

/**
 * 折射通道
 * 
 * 执行包含屏幕空间折射（Screen-Space Refraction, SSR）的渲染通道。
 * 分为两个阶段：
 * 1. 不透明物体通道：渲染所有不透明物体
 * 2. 透明/折射物体通道：渲染透明和折射物体，使用不透明通道的输出作为背景
 * 
 * @param fg 帧图引用
 * @param engine 引擎引用
 * @param view 视图引用
 * @param colorPassInput 颜色通道输入
 * @param config 颜色通道配置
 * @param ssrConfig 屏幕空间反射/折射配置
 * @param colorGradingConfig 颜色分级配置
 * @param pass 渲染通道引用
 * @param firstRefractionCommand 第一个折射命令指针
 * @return 颜色通道输出
 */
RendererUtils::ColorPassOutput RendererUtils::refractionPass(
        FrameGraph& fg, FEngine& engine, FView const& view,
        ColorPassInput colorPassInput,
        ColorPassConfig config,
        PostProcessManager::ScreenSpaceRefConfig const& ssrConfig,
        PostProcessManager::ColorGradingConfig const colorGradingConfig,
        RenderPass const& pass, RenderPass::Command const* const firstRefractionCommand) noexcept {

    assert_invariant(firstRefractionCommand);
    RenderPass::Command const* const refraction = firstRefractionCommand;

    /**
     * if there wasn't any refractive object, just skip everything below.
     * 如果没有折射对象，应该跳过下面的所有内容。
     * 确保输入颜色和深度缓冲区为空（因为它们将从不透明通道输出中获取）
     */
    assert_invariant(!colorPassInput.linearColor);
    assert_invariant(!colorPassInput.depth);
    config.hasScreenSpaceReflectionsOrRefractions = true;

    /**
     * 渲染不透明物体通道
     * 这是第一阶段，渲染所有不透明物体
     */
    PostProcessManager& ppm = engine.getPostProcessManager();
    auto const opaquePassOutput = colorPass(fg,
            "Color Pass (opaque)", engine, view, colorPassInput, {
                    /**
                     * When rendering the opaques, we need to conserve the sample buffer,
                     * so create a config that specifies the sample count.
                     * 渲染不透明物体时，需要保留多重采样缓冲区，
                     * 因此创建一个指定采样数的配置
                     */
                    .width = config.physicalViewport.width,
                    .height = config.physicalViewport.height,
                    .samples = config.msaa,
                    .format = config.hdrFormat
            },
            config, { .asSubpass = false, .customResolve = false },
            pass.getExecutor(pass.begin(), refraction));


    /**
     * Generate the mipmap chain
     * Note: we can run some post-processing effects while the "color pass" descriptor set
     * in bound because only the descriptor 0 (frame uniforms) matters, and it's
     * present in both.
     * 
     * 生成 mipmap 链
     * 注意：我们可以在"颜色通道"描述符堆绑定的情况下运行一些后处理效果，
     * 因为只有描述符 0（帧统一缓冲区）是重要的，它在两者中都存在。
     * 
     * 为屏幕空间折射生成 mipmap，用于更好的采样质量
     */
    PostProcessManager::generateMipmapSSR(ppm, fg,
            opaquePassOutput.linearColor,
            ssrConfig.refraction,
            true, ssrConfig);

    /**
     * Now we're doing the refraction pass proper.
     * This uses the same framebuffer (color and depth) used by the opaque pass.
     * For this reason, the `colorBufferDesc` parameter of colorPass() below is only used  for
     * the width and height.
     * 
     * 现在执行真正的折射通道。
     * 这使用与不透明通道相同的帧缓冲区（颜色和深度）。
     * 因此，下面 colorPass() 的 `colorBufferDesc` 参数仅用于宽度和高度。
     */
    colorPassInput.linearColor = opaquePassOutput.linearColor;
    colorPassInput.depth = opaquePassOutput.depth;

    /**
     * Since we're reusing the existing target we don't want to clear any of its buffer.
     * Important: if this target ended up being an imported target, then the clearFlags
     * specified here wouldn't apply (the clearFlags of the imported target take precedence),
     * and we'd end up clearing the opaque pass. This scenario never happens because it is
     * prevented in Renderer.cpp's final blit.
     * 
     * 由于我们重用现有目标，不希望清除其任何缓冲区。
     * 重要：如果此目标最终是导入的目标，则此处指定的 clearFlags 不适用
     * （导入目标的 clearFlags 优先），我们将最终清除不透明通道。
     * 这种情况永远不会发生，因为它在 Renderer.cpp 的最终 blit 中被阻止。
     */
    config.clearFlags = TargetBufferFlags::NONE;
    /**
     * 渲染透明和折射物体通道（第二阶段）
     * 使用不透明通道的输出作为背景
     */
    auto transparentPassOutput = colorPass(fg, "Color Pass (transparent)",
            engine, view, colorPassInput, {
                    .width = config.physicalViewport.width,
                    .height = config.physicalViewport.height },
            config, colorGradingConfig,
            pass.getExecutor(refraction, pass.end()));

    /**
     * We need to do a resolve here because later passes (such as color grading or DoF) will
     * need to sample from 'output'. However, because we have MSAA, we know we're not
     * sampleable. And this is because in the SSR case, we had to use a renderbuffer to
     * conserve the multi-sample buffer.
     * 
     * 我们需要在这里进行解析（resolve），因为后续通道（如颜色分级或景深）
     * 需要从 'output' 采样。但是，由于我们有 MSAA，我们知道它是不可采样的。
     * 这是因为在 SSR 的情况下，我们必须使用渲染缓冲区来保留多重采样缓冲区。
     * 
     * 如果启用了多重采样且未使用子通道，需要解析多重采样缓冲区为单采样纹理
     */
    if (config.msaa > 1 && !colorGradingConfig.asSubpass) {
        transparentPassOutput.linearColor = ppm.resolve(fg, "Resolved Color Buffer",
                transparentPassOutput.linearColor, { .levels = 1 });
    }
    return transparentPassOutput;
}

/**
 * 读取像素数据
 * 
 * 从渲染目标中读取指定区域的像素数据到 CPU 缓冲区。
 * 用于屏幕截图、像素查询等功能。
 * 
 * @param driver 驱动 API 引用
 * @param renderTargetHandle 渲染目标句柄
 * @param xoffset X 偏移（以像素为单位）
 * @param yoffset Y 偏移（以像素为单位）
 * @param width 宽度（以像素为单位）
 * @param height 高度（以像素为单位）
 * @param buffer 像素缓冲区描述符（将被移动到驱动中）
 */
UTILS_NOINLINE
void RendererUtils::readPixels(DriverApi& driver, Handle<HwRenderTarget> renderTargetHandle,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const width, uint32_t const height,
        PixelBufferDescriptor&& buffer) {
    /**
     * 检查：缓冲区格式不能是压缩格式（压缩格式无法直接读取）
     */
    FILAMENT_CHECK_PRECONDITION(buffer.type != PixelDataType::COMPRESSED)
            << "buffer.format cannot be COMPRESSED";

    /**
     * 检查：对齐必须为 1、2、4 或 8（必须是 2 的幂且 <= 8）
     */
    FILAMENT_CHECK_PRECONDITION(buffer.alignment > 0 && buffer.alignment <= 8 &&
            !(buffer.alignment & (buffer.alignment - 1u)))
            << "buffer.alignment must be 1, 2, 4 or 8";

    /**
     * It's not really possible to know here which formats will be supported because
     * it can vary depending on the RenderTarget, in GL the following are ALWAYS supported though:
     * format: RGBA, RGBA_INTEGER
     * type: UBYTE, UINT, INT, FLOAT
     * 
     * 这里无法真正知道哪些格式会被支持，因为它可能因渲染目标而异。
     * 但在 GL 中，以下格式始终受支持：
     * - 格式：RGBA, RGBA_INTEGER
     * - 类型：UBYTE, UINT, INT, FLOAT
     */

    /**
     * 计算所需的缓冲区大小
     * 考虑格式、类型、步长、高度和对齐要求
     */
    const size_t sizeNeeded = PixelBufferDescriptor::computeDataSize(
            buffer.format, buffer.type,
            buffer.stride ? buffer.stride : width,
            buffer.top + height,
            buffer.alignment);

    /**
     * 检查缓冲区大小是否足够
     */
    FILAMENT_CHECK_PRECONDITION(buffer.size >= sizeNeeded)
            << "Pixel buffer too small: has " << buffer.size << " bytes, needs " << sizeNeeded
            << " bytes";

    /**
     * 调用驱动 API 读取像素数据
     */
    driver.readPixels(renderTargetHandle, xoffset, yoffset, width, height, std::move(buffer));
}

} // namespace filament


