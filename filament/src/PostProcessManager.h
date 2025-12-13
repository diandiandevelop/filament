/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_POSTPROCESSMANAGER_H
#define TNT_FILAMENT_POSTPROCESSMANAGER_H

#include "backend/DriverApiForward.h"

#include "FrameHistory.h"
#include "MaterialInstanceManager.h"

#include "ds/PostProcessDescriptorSet.h"
#include "ds/SsrPassDescriptorSet.h"
#include "ds/StructureDescriptorSet.h"
#include "ds/TypedUniformBuffer.h"

#include "materials/StaticMaterialInfo.h"

#include <fg/FrameGraphId.h>
#include <fg/FrameGraphResources.h>
#include <fg/FrameGraphTexture.h>

#include <filament/Options.h>
#include <filament/Viewport.h>

#include <private/filament/EngineEnums.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>

#include <math/vec2.h>
#include <math/vec4.h>

#include <utils/Slice.h>

#include <tsl/robin_map.h>

#include <array>
#include <random>
#include <string_view>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FColorGrading;
class FEngine;
class FMaterial;
class FMaterialInstance;
class FrameGraph;
class RenderPass;
class RenderPassBuilder;
class UboManager;
struct CameraInfo;

/**
 * PostProcessManager 类
 * 
 * 管理后处理效果，包括抗锯齿、环境光遮蔽、反射、颜色分级等。
 */
class PostProcessManager {
public:

    /**
     * 静态材质信息类型
     */
    using StaticMaterialInfo = filament::StaticMaterialInfo;

    /**
     * 颜色分级配置结构
     */
    struct ColorGradingConfig {
        bool asSubpass{};  // 是否作为子通道
        bool customResolve{};  // 是否自定义解析
        bool translucent{};  // 是否半透明
        /**
         * 是否在 alpha 通道输出亮度
         * 注意：TRANSLUCENT 变体忽略此选项
         */
        bool outputLuminance{}; // Whether to output luminance in the alpha channel. Ignored by the TRANSLUCENT variant.
        bool dithering{};  // 是否抖动
        backend::TextureFormat ldrFormat{};  // LDR 格式
    };

    /**
     * 结构通道配置结构
     */
    struct StructurePassConfig {
        float scale = 0.5f;  // 缩放比例
        bool picking{};  // 是否启用拾取
    };

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit PostProcessManager(FEngine& engine) noexcept;
    /**
     * 析构函数
     */
    ~PostProcessManager() noexcept;

    /**
     * 初始化
     */
    void init() noexcept;
    /**
     * 终止
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver) noexcept;

    /**
     * 配置时间抗锯齿材质
     * 
     * @param taaOptions 时间抗锯齿选项
     */
    void configureTemporalAntiAliasingMaterial(
            TemporalAntiAliasingOptions const& taaOptions) noexcept;

    // methods below are ordered relative to their position in the pipeline (as much as possible)

    /**
     * 结构通道（深度通道）
     */
    // structure (depth) pass
    /**
     * 结构通道输出结构
     */
    struct StructurePassOutput {
        FrameGraphId<FrameGraphTexture> structure;  // 结构纹理（深度）
        FrameGraphId<FrameGraphTexture> picking;   // 拾取纹理
    };
    /**
     * 执行结构通道
     * 
     * @param fg 帧图引用
     * @param passBuilder 渲染通道构建器
     * @param structureRenderFlags 结构渲染标志
     * @param width 宽度
     * @param height 高度
     * @param config 配置
     * @return 结构通道输出
     */
    StructurePassOutput structure(FrameGraph& fg,
            RenderPassBuilder const& passBuilder, uint8_t structureRenderFlags,
            uint32_t width, uint32_t height, StructurePassConfig const& config) noexcept;

    /**
     * 透明拾取
     * 
     * @param fg 帧图引用
     * @param passBuilder 渲染通道构建器
     * @param structureRenderFlags 结构渲染标志
     * @param width 宽度
     * @param height 高度
     * @param scale 缩放比例
     * @return 拾取纹理 ID
     */
    FrameGraphId<FrameGraphTexture> transparentPicking(FrameGraph& fg,
            RenderPassBuilder const& passBuilder, uint8_t structureRenderFlags,
            uint32_t width, uint32_t height, float scale) noexcept;

    /**
     * 屏幕空间反射通道
     */
    // reflections pass
    /**
     * 执行屏幕空间反射
     * 
     * @param fg 帧图引用
     * @param passBuilder 渲染通道构建器
     * @param frameHistory 帧历史
     * @param structure 结构纹理
     * @param desc 纹理描述符
     * @return 反射纹理 ID
     */
    FrameGraphId<FrameGraphTexture> ssr(FrameGraph& fg,
            RenderPassBuilder const& passBuilder,
            FrameHistory const& frameHistory,
            FrameGraphId<FrameGraphTexture> structure,
            FrameGraphTexture::Descriptor const& desc) noexcept;

    /**
     * 屏幕空间环境光遮蔽
     */
    // SSAO
    /**
     * 执行屏幕空间环境光遮蔽
     * 
     * @param fg 帧图引用
     * @param svp 视口
     * @param cameraInfo 相机信息
     * @param depth 深度纹理
     * @param options 环境光遮蔽选项
     * @return SSAO 纹理 ID
     */
    FrameGraphId<FrameGraphTexture> screenSpaceAmbientOcclusion(FrameGraph& fg,
            Viewport const& svp, const CameraInfo& cameraInfo,
            FrameGraphId<FrameGraphTexture> depth,
            AmbientOcclusionOptions const& options) noexcept;

    /**
     * 高斯 Mipmap
     */
    // Gaussian mipmap
    /**
     * 生成高斯 Mipmap
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param levels Mip 级别数
     * @param reinhard 是否使用 Reinhard 色调映射
     * @param kernelWidth 核宽度
     * @param sigma 标准差
     * @return 生成的 Mipmap 纹理 ID
     */
    FrameGraphId<FrameGraphTexture> generateGaussianMipmap(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input, size_t levels, bool reinhard,
            size_t kernelWidth, float sigma) noexcept;

    /**
     * 屏幕空间反射配置结构
     */
    struct ScreenSpaceRefConfig {
        /**
         * SSR 纹理（即 2D 数组）
         */
        // The SSR texture (i.e. the 2d Array)
        FrameGraphId<FrameGraphTexture> ssr;
        /**
         * 接收折射的子资源句柄
         */
        // handle to subresource to receive the refraction
        FrameGraphId<FrameGraphTexture> refraction;
        /**
         * 接收反射的子资源句柄
         */
        // handle to subresource to receive the reflections
        FrameGraphId<FrameGraphTexture> reflection;
        float lodOffset;            // LOD 偏移
        uint8_t roughnessLodCount;  // LOD 数量
        uint8_t kernelSize;         // 核大小
        float sigma0;               // sigma0
    };

    /**
     * 准备 Mipmap SSR
     * 
     * 创建将接收反射和折射缓冲区的 2D 数组。
     * 
     * @param fg 帧图引用
     * @param width 宽度
     * @param height 高度
     * @param format 纹理格式
     * @param verticalFieldOfView 垂直视场角
     * @param scale 缩放比例
     * @return 屏幕空间反射配置
     */
    /*
     * Create the 2D array that will receive the reflection and refraction buffers
     */
    static ScreenSpaceRefConfig prepareMipmapSSR(FrameGraph& fg,
            uint32_t width, uint32_t height, backend::TextureFormat format,
            float verticalFieldOfView, math::float2 scale) noexcept;

    /*
     * Helper to generate gaussian mipmaps for SSR (refraction and reflections).
     * This performs the following tasks:
     *  - resolves input if needed
     *  - optionally duplicates the input
     *  - rescale input, so it has a homogenous scale
     *  - generate a new texture with gaussian mips
     */
    static FrameGraphId<FrameGraphTexture> generateMipmapSSR(
            PostProcessManager& ppm, FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> output,
            bool needInputDuplication, ScreenSpaceRefConfig const& config) noexcept;

    /**
     * 景深（Depth-of-field）
     */
    // Depth-of-field
    /**
     * 执行景深效果
     * 
     * 根据深度信息应用景深模糊效果。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param depth 深度纹理
     * @param cameraInfo 相机信息（用于计算焦距等）
     * @param translucent 是否半透明
     * @param bokehScale 散景缩放
     * @param dofOptions 景深选项
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> dof(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> depth,
            const CameraInfo& cameraInfo,
            bool translucent,
            math::float2 bokehScale,
            const DepthOfFieldOptions& dofOptions) noexcept;

    /**
     * 泛光（Bloom）
     */
    // Bloom
    /**
     * 泛光通道输出结构
     */
    struct BloomPassOutput {
        FrameGraphId<FrameGraphTexture> bloom;  // 泛光纹理
        FrameGraphId<FrameGraphTexture> flare;  // 光晕纹理
    };
    
    /**
     * 执行泛光效果
     * 
     * 提取高光区域并应用泛光效果。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param outFormat 输出格式
     * @param inoutBloomOptions 泛光选项（输入输出）
     * @param taaOptions 时间抗锯齿选项
     * @param scale 缩放比例
     * @return 泛光通道输出
     */
    BloomPassOutput bloom(FrameGraph& fg, FrameGraphId<FrameGraphTexture> input,
            backend::TextureFormat outFormat,
            BloomOptions& inoutBloomOptions,
            TemporalAntiAliasingOptions const& taaOptions,
            math::float2 scale) noexcept;

    /**
     * 执行光晕通道
     * 
     * 生成镜头光晕效果。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param width 宽度
     * @param height 高度
     * @param outFormat 输出格式
     * @param bloomOptions 泛光选项
     * @return 光晕纹理 ID
     */
    FrameGraphId<FrameGraphTexture> flarePass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            uint32_t width, uint32_t height,
            backend::TextureFormat outFormat,
            BloomOptions const& bloomOptions) noexcept;

    /**
     * 颜色分级、色调映射、抖动和泛光
     */
    // Color grading, tone mapping, dithering and bloom
    /**
     * 执行颜色分级
     * 
     * 应用颜色分级、色调映射、抖动和泛光混合。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param vp 视口
     * @param bloom 泛光纹理
     * @param flare 光晕纹理
     * @param colorGrading 颜色分级对象
     * @param colorGradingConfig 颜色分级配置
     * @param bloomOptions 泛光选项
     * @param vignetteOptions 晕影选项
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> colorGrading(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input, Viewport const& vp,
            FrameGraphId<FrameGraphTexture> bloom,
            FrameGraphId<FrameGraphTexture> flare,
            const FColorGrading* colorGrading,
            ColorGradingConfig const& colorGradingConfig,
            BloomOptions const& bloomOptions,
            VignetteOptions const& vignetteOptions) noexcept;

    /**
     * 准备颜色分级子通道
     * 
     * 在子通道模式下准备颜色分级材质。
     * 
     * @param driver 驱动 API 引用
     * @param colorGrading 颜色分级对象
     * @param colorGradingConfig 颜色分级配置
     * @param vignetteOptions 晕影选项
     * @param width 宽度
     * @param height 高度
     */
    void colorGradingPrepareSubpass(backend::DriverApi& driver, const FColorGrading* colorGrading,
            ColorGradingConfig const& colorGradingConfig,
            VignetteOptions const& vignetteOptions,
            uint32_t width, uint32_t height) noexcept;

    /**
     * 执行颜色分级子通道
     * 
     * 在子通道模式下执行颜色分级。
     * 
     * @param driver 驱动 API 引用
     * @param colorGradingConfig 颜色分级配置
     */
    void colorGradingSubpass(backend::DriverApi& driver,
            ColorGradingConfig const& colorGradingConfig) noexcept;

    /**
     * 自定义 MSAA 解析作为子通道
     */
    // custom MSAA resolve as subpass
    /**
     * 自定义解析操作枚举
     */
    enum class CustomResolveOp { COMPRESS, UNCOMPRESS };  // 压缩、解压缩
    
    /**
     * 准备自定义解析子通道
     * 
     * @param driver 驱动 API 引用
     * @param op 解析操作
     */
    void customResolvePrepareSubpass(backend::DriverApi& driver, CustomResolveOp op) noexcept;
    
    /**
     * 执行自定义解析子通道
     * 
     * @param driver 驱动 API 引用
     */
    void customResolveSubpass(backend::DriverApi& driver) noexcept;
    
    /**
     * 自定义解析解压缩通道
     * 
     * @param fg 帧图引用
     * @param inout 输入输出纹理
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> customResolveUncompressPass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> inout) noexcept;

    /**
     * 清除深度缓冲区通道
     */
    // clear depth buffer pass
    /**
     * 准备清除辅助缓冲区
     * 
     * @param driver 驱动 API 引用
     */
    void clearAncillaryBuffersPrepare(backend::DriverApi& driver) noexcept;
    
    /**
     * 清除辅助缓冲区
     * 
     * @param driver 驱动 API 引用
     * @param attachments 要清除的附件标志
     */
    void clearAncillaryBuffers(backend::DriverApi& driver,
            backend::TargetBufferFlags attachments) const noexcept;

    /**
     * 抗锯齿
     */
    // Anti-aliasing
    /**
     * 执行 FXAA（快速近似抗锯齿）
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param vp 视口
     * @param outFormat 输出格式
     * @param preserveAlphaChannel 是否保留 Alpha 通道
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> fxaa(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input, Viewport const& vp,
            backend::TextureFormat outFormat, bool preserveAlphaChannel) noexcept;

    /**
     * 时间抗锯齿
     */
    // Temporal Anti-aliasing
    /**
     * TAA 抖动相机
     * 
     * 为时间抗锯齿应用相机抖动。
     * 
     * @param svp 视口
     * @param taaOptions 时间抗锯齿选项
     * @param frameHistory 帧历史
     * @param pTaa TAA 数据成员指针
     * @param inoutCameraInfo 相机信息（输入输出）
     */
    void TaaJitterCamera(
            Viewport const& svp,
            TemporalAntiAliasingOptions const& taaOptions,
            FrameHistory& frameHistory,
            FrameHistoryEntry::TemporalAA FrameHistoryEntry::*pTaa,
            CameraInfo* inoutCameraInfo) const noexcept;

    /**
     * 执行时间抗锯齿
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param depth 深度纹理
     * @param frameHistory 帧历史
     * @param pTaa TAA 数据成员指针
     * @param taaOptions 时间抗锯齿选项
     * @param colorGradingConfig 颜色分级配置
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> taa(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> depth,
            FrameHistory& frameHistory,
            FrameHistoryEntry::TemporalAA FrameHistoryEntry::*pTaa,
            TemporalAntiAliasingOptions const& taaOptions,
            ColorGradingConfig const& colorGradingConfig) noexcept;

    /**
     * Blit/重新缩放/解析
     */
    /*
     * Blit/rescaling/resolves
     */

    /**
     * 高质量上采样器
     * 
     * - 当半透明时，回退到 LINEAR
     * - 不处理子资源
     * 
     * @param fg 帧图引用
     * @param translucent 是否半透明
     * @param sourceHasLuminance 源是否有亮度
     * @param dsrOptions 动态分辨率选项
     * @param input 输入纹理
     * @param vp 视口
     * @param outDesc 输出描述符
     * @param filter 放大过滤器
     * @return 上采样后的纹理 ID
     */
    // high quality upscaler
    //  - when translucent, reverts to LINEAR
    //  - doesn't handle sub-resouces
    FrameGraphId<FrameGraphTexture> upscale(FrameGraph& fg, bool translucent,
            bool sourceHasLuminance, DynamicResolutionOptions dsrOptions,
            FrameGraphId<FrameGraphTexture> input, Viewport const& vp,
            FrameGraphTexture::Descriptor const& outDesc, backend::SamplerMagFilter filter) noexcept;

    /**
     * 双线性上采样
     * 
     * @param fg 帧图引用
     * @param translucent 是否半透明
     * @param dsrOptions 动态分辨率选项
     * @param input 输入纹理
     * @param vp 视口
     * @param outDesc 输出描述符
     * @param filter 过滤器
     * @return 上采样后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> upscaleBilinear(FrameGraph& fg, bool translucent,
            DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> input,
            Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
            backend::SamplerMagFilter filter) noexcept;

    /**
     * FSR1 上采样
     * 
     * 使用 FidelityFX Super Resolution 1.0 进行上采样。
     * 
     * @param fg 帧图引用
     * @param dsrOptions 动态分辨率选项
     * @param input 输入纹理
     * @param vp 视口
     * @param outDesc 输出描述符
     * @return 上采样后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> upscaleFSR1(FrameGraph& fg,
            DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> input,
            filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc) noexcept;

    /**
     * SGSR1 上采样
     * 
     * 使用 SGSR（可能是自定义上采样器）进行上采样。
     * 
     * @param fg 帧图引用
     * @param sourceHasLuminance 源是否有亮度
     * @param dsrOptions 动态分辨率选项
     * @param input 输入纹理
     * @param vp 视口
     * @param outDesc 输出描述符
     * @return 上采样后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> upscaleSGSR1(FrameGraph& fg, bool sourceHasLuminance,
            DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> input,
            filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc) noexcept;

    /**
     * RCAS 模式枚举
     * 
     * RCAS（Robust Contrast Adaptive Sharpening）是 FSR 的锐化阶段。
     */
    enum class RcasMode {
        OPAQUE,  // 不透明
        ALPHA_PASSTHROUGH,  // Alpha 直通
        BLENDED  // 混合
    };

    /**
     * 执行 RCAS 锐化
     * 
     * @param fg 帧图引用
     * @param sharpness 锐度
     * @param input 输入纹理
     * @param outDesc 输出描述符
     * @param mode RCAS 模式
     * @return 锐化后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> rcas(
            FrameGraph& fg,
            float sharpness,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphTexture::Descriptor const& outDesc,
            RcasMode mode);

    /**
     * 使用着色器的颜色 Blit
     */
    // color blitter using shaders
    /**
     * 执行颜色 Blit
     * 
     * @param fg 帧图引用
     * @param translucent 是否半透明
     * @param input 输入纹理
     * @param vp 视口
     * @param outDesc 输出描述符
     * @param filterMag 放大过滤器
     * @param filterMin 缩小过滤器
     * @return Blit 后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> blit(FrameGraph& fg, bool translucent,
            FrameGraphId<FrameGraphTexture> input,
            Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
            backend::SamplerMagFilter filterMag,
            backend::SamplerMinFilter filterMin) noexcept;

    /**
     * 使用着色器的深度 Blit
     */
    // depth blitter using shaders
    /**
     * 执行深度 Blit
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @return Blit 后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> blitDepth(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input) noexcept;

    /**
     * 解析输入的基础级别并输出一个纹理。
     * outDesc 的宽度、高度、格式和采样数将被覆盖。
     */
    // Resolves base level of input and outputs a texture from outDesc.
    // outDesc with, height, format and samples will be overridden.
    /**
     * 解析颜色纹理
     * 
     * @param fg 帧图引用
     * @param outputBufferName 输出缓冲区名称
     * @param input 输入纹理
     * @param outDesc 输出描述符
     * @return 解析后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> resolve(FrameGraph& fg,
            utils::StaticString outputBufferName, FrameGraphId<FrameGraphTexture> input,
            FrameGraphTexture::Descriptor outDesc) noexcept;

    /**
     * 解析输入的基础级别并输出一个纹理。
     * outDesc 的宽度、高度、格式和采样数将被覆盖。
     */
    // Resolves base level of input and outputs a texture from outDesc.
    // outDesc with, height, format and samples will be overridden.
    /**
     * 解析深度纹理
     * 
     * @param fg 帧图引用
     * @param outputBufferName 输出缓冲区名称
     * @param input 输入纹理
     * @param outDesc 输出描述符
     * @return 解析后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> resolveDepth(FrameGraph& fg,
            utils::StaticString outputBufferName, FrameGraphId<FrameGraphTexture> input,
            FrameGraphTexture::Descriptor outDesc) noexcept;

    /**
     * VSM 阴影 Mipmap 通道
     */
    // VSM shadow mipmap pass
    /**
     * 执行 VSM 阴影 Mipmap 通道
     * 
     * 为方差阴影贴图生成 Mipmap。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param layer 层索引
     * @param level Mip 级别
     * @param clearColor 清除颜色
     * @return 处理后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> vsmMipmapPass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input, uint8_t layer, size_t level,
            math::float4 clearColor) noexcept;

    /**
     * 执行高斯模糊通道
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param output 输出纹理
     * @param reinhard 是否使用 Reinhard 色调映射
     * @param kernelWidth 核宽度
     * @param sigma 标准差
     * @return 模糊后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> gaussianBlurPass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> output,
            bool reinhard, size_t kernelWidth, float sigma) noexcept;

    /**
     * 调试阴影级联
     * 
     * 可视化阴影级联边界。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param depth 深度纹理
     * @return 调试纹理 ID
     */
    FrameGraphId<FrameGraphTexture> debugShadowCascades(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> depth) noexcept;

    /**
     * 调试显示阴影纹理
     * 
     * 在屏幕上显示阴影贴图内容（用于调试）。
     * 
     * @param fg 帧图引用
     * @param input 输入纹理
     * @param shadowmap 阴影贴图纹理
     * @param scale 缩放比例
     * @param layer 层索引
     * @param level Mip 级别
     * @param channel 通道索引
     * @param power 幂值（用于调整显示）
     * @return 调试纹理 ID
     */
    FrameGraphId<FrameGraphTexture> debugDisplayShadowTexture(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphId<FrameGraphTexture> shadowmap, float scale,
            uint8_t layer, uint8_t level, uint8_t channel, float power) noexcept;

    /**
     * 调试：合并数组纹理
     * 
     * 将由 `input` 指向的数组纹理合并为单个图像，然后返回它。
     * 这仅用于检查多视图渲染的场景作为调试目的，因此
     * 在正常情况下不应使用。
     */
    // Combine an array texture pointed to by `input` into a single image, then return it.
    // This is only useful to check the multiview rendered scene as a debugging purpose, thus this
    // is not expected to be used in normal cases.
    /**
     * 合并数组纹理（调试用）
     * 
     * @param fg 帧图引用
     * @param translucent 是否半透明
     * @param input 输入纹理（数组）
     * @param vp 视口
     * @param outDesc 输出描述符
     * @param filterMag 放大过滤器
     * @param filterMin 缩小过滤器
     * @return 合并后的纹理 ID
     */
    FrameGraphId<FrameGraphTexture> debugCombineArrayTexture(FrameGraph& fg, bool translucent,
        FrameGraphId<FrameGraphTexture> input,
        Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
        backend::SamplerMagFilter filterMag,
        backend::SamplerMinFilter filterMin) noexcept;

    /**
     * 获取单色纹理（全 1）
     * 
     * @return 单色纹理句柄
     */
    backend::Handle<backend::HwTexture> getOneTexture() const;
    
    /**
     * 获取零纹理（全 0）
     * 
     * @return 零纹理句柄
     */
    backend::Handle<backend::HwTexture> getZeroTexture() const;
    
    /**
     * 获取单色纹理数组（全 1）
     * 
     * @return 单色纹理数组句柄
     */
    backend::Handle<backend::HwTexture> getOneTextureArray() const;
    
    /**
     * 获取零纹理数组（全 0）
     * 
     * @return 零纹理数组句柄
     */
    backend::Handle<backend::HwTexture> getZeroTextureArray() const;

    /**
     * 后处理材质类
     * 
     * 管理后处理效果的材质，支持延迟加载。
     */
    class PostProcessMaterial {
    public:
        /**
         * 构造函数
         * 
         * @param info 静态材质信息
         */
        explicit PostProcessMaterial(StaticMaterialInfo const& info) noexcept;

        /**
         * 禁止拷贝构造
         */
        PostProcessMaterial(PostProcessMaterial const& rhs) = delete;
        
        /**
         * 禁止拷贝赋值
         */
        PostProcessMaterial& operator=(PostProcessMaterial const& rhs) = delete;

        /**
         * 移动构造函数
         */
        PostProcessMaterial(PostProcessMaterial&& rhs) noexcept;
        
        /**
         * 移动赋值操作符
         */
        PostProcessMaterial& operator=(PostProcessMaterial&& rhs) noexcept;

        /**
         * 析构函数
         */
        ~PostProcessMaterial() noexcept;

        /**
         * 终止后处理材质
         * 
         * 释放材质资源。
         * 
         * @param engine 引擎引用
         */
        void terminate(FEngine& engine) noexcept;

        /**
         * 获取材质
         * 
         * 如果材质未加载，则延迟加载。
         * 
         * @param engine 引擎引用
         * @param variant 后处理变体（默认不透明）
         * @return 材质指针
         */
        FMaterial* getMaterial(FEngine& engine,
                PostProcessVariant variant = PostProcessVariant::OPAQUE) const noexcept;

    private:
        /**
         * 加载材质
         * 
         * 从静态数据创建材质对象。
         * 
         * @param engine 引擎引用
         */
        void loadMaterial(FEngine& engine) const noexcept;

        /**
         * 联合体：材质指针或数据指针
         * 
         * 在加载前存储数据指针，加载后存储材质指针。
         */
        union {
            mutable FMaterial* mMaterial;  // 材质指针（加载后）
            uint8_t const* mData;  // 数据指针（加载前）
        };
        /**
         * mSize == 0 如果 mMaterial 有效，否则 mSize > 0
         */
        // mSize == 0 if mMaterial is valid, otherwise mSize > 0
        mutable uint32_t mSize{};  // 数据大小（0 表示已加载）
        /**
         * 对象必须比 Slice<> 生命周期更长
         */
        // the objects' must outlive the Slice<>
        utils::Slice<const StaticMaterialInfo::ConstantInfo> mConstants{};  // 常量信息切片
    };

    /**
     * 注册后处理材质
     * 
     * 将后处理材质注册到材质注册表中。
     * 
     * @param name 材质名称
     * @param info 静态材质信息
     */
    void registerPostProcessMaterial(std::string_view name, StaticMaterialInfo const& info);

    /**
     * 获取后处理材质
     * 
     * @param name 材质名称
     * @return 后处理材质常量引用
     */
    PostProcessManager::PostProcessMaterial const& getPostProcessMaterial(
            std::string_view name) const noexcept;

    /**
     * 设置帧统一数据
     * 
     * 将每视图统一缓冲区绑定到后处理描述符集。
     * 
     * @param driver 驱动 API 引用
     * @param uniforms 每视图统一缓冲区引用
     */
    void setFrameUniforms(backend::DriverApi& driver,
            TypedUniformBuffer<PerViewUib>& uniforms) noexcept;

    /**
     * 绑定后处理描述符集
     * 
     * @param driver 驱动 API 引用
     */
    void bindPostProcessDescriptorSet(backend::DriverApi& driver) const noexcept;

    /**
     * 获取管线状态
     * 
     * 从材质获取管线状态。
     * 
     * @param ma 材质指针
     * @param variant 后处理变体（默认不透明）
     * @return 管线状态
     */
    backend::PipelineState getPipelineState(
            FMaterial const* ma,
            PostProcessVariant variant = PostProcessVariant::OPAQUE) const noexcept;

    /**
     * 渲染全屏四边形
     * 
     * 使用指定的管线状态渲染全屏四边形。
     * 
     * @param out 渲染通道信息
     * @param pipeline 管线状态
     * @param driver 驱动 API 引用
     */
    void renderFullScreenQuad(FrameGraphResources::RenderPassInfo const& out,
            backend::PipelineState const& pipeline,
            backend::DriverApi& driver) const noexcept;

    /**
     * 使用裁剪渲染全屏四边形
     * 
     * 使用指定的管线状态和裁剪矩形渲染全屏四边形。
     * 
     * @param out 渲染通道信息
     * @param pipeline 管线状态
     * @param scissor 裁剪矩形
     * @param driver 驱动 API 引用
     */
    void renderFullScreenQuadWithScissor(FrameGraphResources::RenderPassInfo const& out,
            backend::PipelineState const& pipeline,
            backend::Viewport scissor,
            backend::DriverApi& driver) const noexcept;

    /**
     * 提交并渲染全屏四边形（辅助方法）
     * 
     * 用于常见情况的辅助方法。不要在循环中使用，因为从
     * FMaterialInstance 检索 PipelineState 不是简单的操作。
     */
    // Helper for a common case. Don't use in a loop because retrieving the PipelineState
    // from FMaterialInstance is not trivial.
    /**
     * 提交并渲染全屏四边形
     * 
     * @param driver 驱动 API 引用
     * @param out 渲染通道信息
     * @param mi 材质实例指针
     * @param variant 后处理变体（默认不透明）
     */
    void commitAndRenderFullScreenQuad(backend::DriverApi& driver,
            FrameGraphResources::RenderPassInfo const& out,
            FMaterialInstance const* mi,
            PostProcessVariant variant = PostProcessVariant::OPAQUE) const noexcept;

    /**
     * 配置颜色分级材质
     * 
     * 设置 colorGrading.mat 和 colorGradingAsSubpass.mat 共有的
     * 必要的规格常量和统一数据。
     */
    // Sets the necessary spec constants and uniforms common to both colorGrading.mat and
    // colorGradingAsSubpass.mat.
    /**
     * 配置颜色分级材质
     * 
     * @param material 后处理材质引用
     * @param colorGrading 颜色分级对象
     * @param colorGradingConfig 颜色分级配置
     * @param vignetteOptions 晕影选项
     * @param width 宽度
     * @param height 高度
     * @return 配置后的材质实例指针
     */
    FMaterialInstance* configureColorGradingMaterial(
            PostProcessMaterial const& material, FColorGrading const* colorGrading,
            ColorGradingConfig const& colorGradingConfig, VignetteOptions const& vignetteOptions,
            uint32_t width, uint32_t height) noexcept;

    /**
     * 获取结构描述符集
     * 
     * @return 结构描述符集引用
     */
    StructureDescriptorSet& getStructureDescriptorSet() const noexcept { return mStructureDescriptorSet; }

    /**
     * 重置渲染状态
     * 
     * 为新的渲染帧重置状态。
     */
    void resetForRender();

private:
    static void unbindAllDescriptorSets(backend::DriverApi& driver) noexcept;

    void bindPerRenderableDescriptorSet(backend::DriverApi& driver) const noexcept;

    // Helper to get a MaterialInstance from a FMaterial
    // This currently just call FMaterial::getDefaultInstance().
    FMaterialInstance* getMaterialInstance(FMaterial const* ma) {
        return mMaterialInstanceManager.getMaterialInstance(ma);
    }

    // Helper to get a MaterialInstance from a PostProcessMaterial.
    FMaterialInstance* getMaterialInstance(FEngine& engine, PostProcessMaterial const& material,
            PostProcessVariant variant = PostProcessVariant::OPAQUE) {
        FMaterial const* ma = material.getMaterial(engine, variant);
        return getMaterialInstance(ma);
    }

    UboManager* getUboManager() const noexcept;

    backend::RenderPrimitiveHandle mFullScreenQuadRph;
    backend::VertexBufferInfoHandle mFullScreenQuadVbih;
    backend::DescriptorSetLayoutHandle mPerRenderableDslh;

    // We need to have a dummy descriptor set because each post processing pass is expected to have
    // a descriptor set bound at the renderable bind point. But the set itself contains dummy
    // values.
    backend::DescriptorSetHandle mDummyPerRenderableDsh;

    FEngine& mEngine;

    mutable SsrPassDescriptorSet mSsrPassDescriptorSet;
    mutable PostProcessDescriptorSet mPostProcessDescriptorSet;
    mutable StructureDescriptorSet mStructureDescriptorSet;

    struct BilateralPassConfig {
        uint8_t kernelSize = 11;
        bool bentNormals = false;
        float standardDeviation = 1.0f;
        float bilateralThreshold = 0.0625f;
        float scale = 1.0f;
    };

    FrameGraphId<FrameGraphTexture> bilateralBlurPass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input, FrameGraphId<FrameGraphTexture> depth,
            math::int2 axis, float zf, backend::TextureFormat format,
            BilateralPassConfig const& config) noexcept;

    FrameGraphId<FrameGraphTexture> downscalePass(FrameGraph& fg,
            FrameGraphId<FrameGraphTexture> input,
            FrameGraphTexture::Descriptor const& outDesc,
            bool threshold, float highlight, bool fireflies) noexcept;

    using MaterialRegistryMap = tsl::robin_map<
            std::string_view,
            PostProcessMaterial>;

    MaterialRegistryMap mMaterialRegistry;

    MaterialInstanceManager mMaterialInstanceManager;

    struct {
        int32_t colorGradingTranslucent = MaterialInstanceManager::INVALID_FIXED_INDEX;
        int32_t colorGradingOpaque = MaterialInstanceManager::INVALID_FIXED_INDEX;
        int32_t customResolve = MaterialInstanceManager::INVALID_FIXED_INDEX;
        int32_t clearDepth = MaterialInstanceManager::INVALID_FIXED_INDEX;
    } mFixedMaterialInstanceIndex;

    backend::Handle<backend::HwTexture> mStarburstTexture;

    std::uniform_real_distribution<float> mUniformDistribution{0.0f, 1.0f};

    template<size_t SIZE>
    struct JitterSequence {
        math::float2 operator()(size_t const i) const noexcept { return positions[i % SIZE] - 0.5f; }
        const std::array<math::float2, SIZE> positions;
    };

    static const JitterSequence<4> sRGSS4;
    static const JitterSequence<4> sUniformHelix4;
    static const JitterSequence<32> sHaltonSamples;

    bool mWorkaroundSplitEasu : 1;
    bool mWorkaroundAllowReadOnlyAncillaryFeedbackLoop : 1;
};

} // namespace filament

#endif // TNT_FILAMENT_POSTPROCESSMANAGER_H
