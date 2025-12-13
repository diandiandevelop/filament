/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "details/ColorGrading.h"

#include "details/Engine.h"
#include "details/Texture.h"

#include "FilamentAPI-impl.h"

#include "ColorSpaceUtils.h"

#include <filament/ColorSpace.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <private/utils/Tracing.h>

#include <utils/JobSystem.h>
#include <utils/Mutex.h>

#include <cmath>
#include <cstdlib>
#include <tuple>

namespace filament {

using namespace utils;
using namespace math;
using namespace color;
using namespace backend;

//------------------------------------------------------------------------------
// Builder
//------------------------------------------------------------------------------

/**
 * 构建器详情结构
 * 
 * 存储颜色分级的构建参数。
 */
struct ColorGrading::BuilderDetails {
    const ToneMapper* toneMapper = nullptr;  // 色调映射器指针（自定义色调映射器）

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    ToneMapping toneMapping = ToneMapping::ACES_LEGACY;  // 色调映射类型（已弃用，使用 toneMapper）
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    bool hasAdjustments = false;  // 是否有调整（用于优化，如果没有任何调整则跳过 LUT 生成）

    /**
     * 以下所有内容必须是 == 比较运算符的一部分
     */
    // Everything below must be part of the == comparison operator
    LutFormat format = LutFormat::INTEGER;  // LUT 格式（INTEGER 或 FLOAT）
    uint8_t dimension = 32;  // LUT 维度（例如 32 表示 32x32x32）

    /**
     * 色域外颜色处理
     */
    // Out-of-gamut color handling
    bool   luminanceScaling = false;  // 是否启用亮度缩放（用于处理超出色域的颜色）
    bool   gamutMapping     = false;  // 是否启用色域映射（将超出色域的颜色映射到色域内）
    /**
     * 曝光
     */
    // Exposure
    float  exposure         = 0.0f;  // 曝光值（EV，0.0 表示无变化）
    /**
     * 夜间适应（Purkinje 效应）
     */
    // Night adaptation
    float  nightAdaptation  = 0.0f;  // 夜间适应强度（0.0 表示无效果，1.0 表示完全效果）
    /**
     * 白平衡
     */
    // White balance
    float2 whiteBalance     = {0.0f, 0.0f};  // 白平衡（x: 色温，y: 色调）
    /**
     * 通道混合器
     */
    // Channel mixer
    float3 outRed           = {1.0f, 0.0f, 0.0f};  // 红色输出通道混合系数（R, G, B）
    float3 outGreen         = {0.0f, 1.0f, 0.0f};  // 绿色输出通道混合系数（R, G, B）
    float3 outBlue          = {0.0f, 0.0f, 1.0f};  // 蓝色输出通道混合系数（R, G, B）
    /**
     * 色调范围（阴影/中间调/高光）
     */
    // Tonal ranges
    float3 shadows          = {1.0f, 1.0f, 1.0f};  // 阴影调整（RGB 乘数）
    float3 midtones         = {1.0f, 1.0f, 1.0f};  // 中间调调整（RGB 乘数）
    float3 highlights       = {1.0f, 1.0f, 1.0f};  // 高光调整（RGB 乘数）
    float4 tonalRanges      = {0.0f, 0.333f, 0.550f, 1.0f};  // 色调范围（阴影开始、暗部结束、亮部开始、高光结束），DaVinci Resolve 默认值
    /**
     * ASC CDL（American Society of Cinematographers Color Decision List）
     */
    // ASC CDL
    float3 slope            = {1.0f};  // 斜率（RGB）
    float3 offset           = {0.0f};  // 偏移（RGB）
    float3 power            = {1.0f};  // 幂（RGB）
    /**
     * 颜色调整
     */
    // Color adjustments
    float  contrast         = 1.0f;  // 对比度（1.0 表示无变化）
    float  vibrance         = 1.0f;  // 自然饱和度（1.0 表示无变化，只影响低饱和度颜色）
    float  saturation       = 1.0f;  // 饱和度（1.0 表示无变化，影响所有颜色）
    /**
     * 曲线
     */
    // Curves
    float3 shadowGamma      = {1.0f};  // 阴影伽马（RGB，用于调整阴影曲线）
    float3 midPoint         = {1.0f};  // 中间点（RGB，用于调整中间调曲线）
    float3 highlightScale   = {1.0f};  // 高光缩放（RGB，用于调整高光曲线）

    /**
     * 输出色彩空间
     */
    // Output color space
    ColorSpace outputColorSpace = Rec709-sRGB-D65;  // 输出色彩空间（默认 Rec.709-sRGB-D65）

    bool operator!=(const BuilderDetails &rhs) const {
        return !(rhs == *this);
    }

    bool operator==(const BuilderDetails &rhs) const {
        // Note: Do NOT compare hasAdjustments and toneMapper
        return format == rhs.format &&
               dimension == rhs.dimension &&
               luminanceScaling == rhs.luminanceScaling &&
               gamutMapping == rhs.gamutMapping &&
               exposure == rhs.exposure &&
               nightAdaptation == rhs.nightAdaptation &&
               whiteBalance == rhs.whiteBalance &&
               outRed == rhs.outRed &&
               outGreen == rhs.outGreen &&
               outBlue == rhs.outBlue &&
               shadows == rhs.shadows &&
               midtones == rhs.midtones &&
               highlights == rhs.highlights &&
               tonalRanges == rhs.tonalRanges &&
               slope == rhs.slope &&
               offset == rhs.offset &&
               power == rhs.power &&
               contrast == rhs.contrast &&
               vibrance == rhs.vibrance &&
               saturation == rhs.saturation &&
               shadowGamma == rhs.shadowGamma &&
               midPoint == rhs.midPoint &&
               highlightScale == rhs.highlightScale &&
               outputColorSpace == rhs.outputColorSpace;
    }
};

using BuilderType = ColorGrading;
BuilderType::Builder::Builder() noexcept = default;
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置质量级别
 * 
 * 根据质量级别设置 LUT 格式和维度。
 * 
 * @param qualityLevel 质量级别（LOW、MEDIUM、HIGH、ULTRA）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::quality(QualityLevel const qualityLevel) noexcept {
    switch (qualityLevel) {  // 根据质量级别设置
        case QualityLevel::LOW:  // 低质量
            mImpl->format = LutFormat::INTEGER;  // 整数格式
            mImpl->dimension = 16;  // 16x16x16
            break;
        case QualityLevel::MEDIUM:  // 中等质量
            mImpl->format = LutFormat::INTEGER;  // 整数格式
            mImpl->dimension = 32;  // 32x32x32
            break;
        case QualityLevel::HIGH:  // 高质量
            mImpl->format = LutFormat::FLOAT;  // 浮点格式
            mImpl->dimension = 32;  // 32x32x32
            break;
        case QualityLevel::ULTRA:  // 超高质量
            mImpl->format = LutFormat::FLOAT;  // 浮点格式
            mImpl->dimension = 64;  // 64x64x64
            break;
    }
    return *this;  // 返回自身引用
}

/**
 * 设置 LUT 格式
 * 
 * @param format LUT 格式（INTEGER 或 FLOAT）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::format(LutFormat const format) noexcept {
    mImpl->format = format;  // 设置格式
    return *this;  // 返回自身引用
}

/**
 * 设置 LUT 维度
 * 
 * @param dim 维度（16-64）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::dimensions(uint8_t const dim) noexcept {
    mImpl->dimension = clamp(+dim, 16, 64);  // 限制维度在 16-64 之间
    return *this;  // 返回自身引用
}

/**
 * 设置色调映射器
 * 
 * 设置自定义色调映射器。
 * 
 * @param toneMapper 色调映射器指针
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::toneMapper(const ToneMapper* toneMapper) noexcept {
    mImpl->toneMapper = toneMapper;  // 设置色调映射器
    return *this;  // 返回自身引用
}

/**
 * 设置色调映射类型（已弃用）
 * 
 * 使用 toneMapper() 代替。
 * 
 * @param toneMapping 色调映射类型
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::toneMapping(ToneMapping const toneMapping) noexcept {
    mImpl->toneMapping = toneMapping;  // 设置色调映射类型
    return *this;  // 返回自身引用
}

/**
 * 设置亮度缩放
 * 
 * 启用或禁用亮度缩放（用于处理超出色域的颜色）。
 * 
 * @param luminanceScaling 是否启用亮度缩放
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::luminanceScaling(bool const luminanceScaling) noexcept {
    mImpl->luminanceScaling = luminanceScaling;  // 设置亮度缩放标志
    return *this;  // 返回自身引用
}

/**
 * 设置色域映射
 * 
 * 启用或禁用色域映射（将超出色域的颜色映射到色域内）。
 * 
 * @param gamutMapping 是否启用色域映射
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::gamutMapping(bool const gamutMapping) noexcept {
    mImpl->gamutMapping = gamutMapping;  // 设置色域映射标志
    return *this;  // 返回自身引用
}

/**
 * 设置曝光
 * 
 * 设置曝光值（EV）。
 * 
 * @param exposure 曝光值（0.0 表示无变化）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::exposure(float const exposure) noexcept {
    mImpl->exposure = exposure;  // 设置曝光值
    return *this;  // 返回自身引用
}

/**
 * 设置夜间适应
 * 
 * 设置夜间适应强度（Purkinje 效应）。
 * 
 * @param adaptation 适应强度（0.0-1.0）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::nightAdaptation(float const adaptation) noexcept {
    mImpl->nightAdaptation = saturate(adaptation);  // 限制在 0.0-1.0 之间
    return *this;  // 返回自身引用
}

/**
 * 设置白平衡
 * 
 * 设置白平衡的色温和色调。
 * 
 * @param temperature 色温（-1.0 到 1.0，负值偏冷，正值偏暖）
 * @param tint 色调（-1.0 到 1.0，负值偏绿，正值偏品红）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::whiteBalance(float const temperature, float const tint) noexcept {
    mImpl->whiteBalance = float2{
        clamp(temperature, -1.0f, 1.0f),  // 限制色温
        clamp(tint, -1.0f, 1.0f)  // 限制色调
    };
    return *this;  // 返回自身引用
}

/**
 * 设置通道混合器
 * 
 * 设置每个输出通道的混合系数。
 * 
 * @param outRed 红色输出通道混合系数（R, G, B）
 * @param outGreen 绿色输出通道混合系数（R, G, B）
 * @param outBlue 蓝色输出通道混合系数（R, G, B）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::channelMixer(
        float3 outRed, float3 outGreen, float3 outBlue) noexcept {
    mImpl->outRed   = clamp(outRed,   -2.0f, 2.0f);  // 限制红色通道系数
    mImpl->outGreen = clamp(outGreen, -2.0f, 2.0f);  // 限制绿色通道系数
    mImpl->outBlue  = clamp(outBlue,  -2.0f, 2.0f);  // 限制蓝色通道系数
    return *this;  // 返回自身引用
}

/**
 * 设置阴影/中间调/高光
 * 
 * 设置阴影、中间调和高光的调整参数。
 * 
 * @param shadows 阴影调整（RGB + 偏移，w 分量是偏移）
 * @param midtones 中间调调整（RGB + 偏移，w 分量是偏移）
 * @param highlights 高光调整（RGB + 偏移，w 分量是偏移）
 * @param ranges 色调范围（x: 阴影开始，y: 暗部结束，z: 亮部开始，w: 高光结束）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::shadowsMidtonesHighlights(
        float4 shadows, float4 midtones, float4 highlights, float4 ranges) noexcept {
    /**
     * 将 RGB 和偏移相加，并限制为非负
     */
    mImpl->shadows = max(shadows.rgb + shadows.w, 0.0f);  // 阴影（RGB + 偏移）
    mImpl->midtones = max(midtones.rgb + midtones.w, 0.0f);  // 中间调（RGB + 偏移）
    mImpl->highlights = max(highlights.rgb + highlights.w, 0.0f);  // 高光（RGB + 偏移）

    /**
     * 验证和限制范围值
     */
    ranges.x = saturate(ranges.x);  // 阴影开始（限制在 [0, 1]）
    ranges.w = saturate(ranges.w);  // 高光结束（限制在 [0, 1]）
    ranges.y = clamp(ranges.y, ranges.x + 1e-5f, ranges.w - 1e-5f);  // 暗部结束（确保在阴影和高光之间）
    ranges.z = clamp(ranges.z, ranges.x + 1e-5f, ranges.w - 1e-5f);  // 亮部开始（确保在阴影和高光之间）
    mImpl->tonalRanges = ranges;  // 保存范围

    return *this;  // 返回自身引用
}

/**
 * 设置 ASC CDL（斜率/偏移/幂）
 * 
 * 设置 ASC CDL（American Society of Cinematographers Color Decision List）参数。
 * 
 * @param slope 斜率（RGB，必须 > 0）
 * @param offset 偏移（RGB）
 * @param power 幂（RGB，必须 > 0）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::slopeOffsetPower(
        float3 slope, float3 offset, float3 power) noexcept {
    mImpl->slope = max(1e-5f, slope);  // 限制斜率最小值为 1e-5
    mImpl->offset = offset;  // 设置偏移
    mImpl->power = max(1e-5f, power);  // 限制幂最小值为 1e-5
    return *this;  // 返回自身引用
}

/**
 * 设置对比度
 * 
 * 设置对比度值。
 * 
 * @param contrast 对比度值（0.0-2.0，1.0 表示无变化）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::contrast(float const contrast) noexcept {
    mImpl->contrast = clamp(contrast, 0.0f, 2.0f);  // 限制对比度在 0.0-2.0 之间
    return *this;  // 返回自身引用
}

/**
 * 设置自然饱和度
 * 
 * 设置自然饱和度值（只影响低饱和度颜色）。
 * 
 * @param vibrance 自然饱和度值（0.0-2.0，1.0 表示无变化）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::vibrance(float const vibrance) noexcept {
    mImpl->vibrance = clamp(vibrance, 0.0f, 2.0f);  // 限制自然饱和度在 0.0-2.0 之间
    return *this;  // 返回自身引用
}

/**
 * 设置饱和度
 * 
 * 设置饱和度值（影响所有颜色）。
 * 
 * @param saturation 饱和度值（0.0-2.0，1.0 表示无变化）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::saturation(float const saturation) noexcept {
    mImpl->saturation = clamp(saturation, 0.0f, 2.0f);  // 限制饱和度在 0.0-2.0 之间
    return *this;  // 返回自身引用
}

/**
 * 设置曲线
 * 
 * 设置 RGB 曲线参数。
 * 
 * @param shadowGamma 阴影伽马（RGB，用于调整阴影曲线，必须 > 0）
 * @param midPoint 中间点（RGB，用于分割阴影和高光，必须 > 0）
 * @param highlightScale 高光缩放（RGB，用于调整高光曲线）
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::curves(
        float3 shadowGamma, float3 midPoint, float3 highlightScale) noexcept {
    mImpl->shadowGamma = max(1e-5f, shadowGamma);  // 限制阴影伽马最小值为 1e-5
    mImpl->midPoint = max(1e-5f, midPoint);  // 限制中间点最小值为 1e-5
    mImpl->highlightScale = highlightScale;  // 设置高光缩放
    return *this;  // 返回自身引用
}

/**
 * 设置输出色彩空间
 * 
 * 设置输出色彩空间。
 * 
 * @param colorSpace 色彩空间
 * @return 构建器引用（支持链式调用）
 */
ColorGrading::Builder& ColorGrading::Builder::outputColorSpace(
        const ColorSpace& colorSpace) noexcept {
    mImpl->outputColorSpace = colorSpace;  // 设置输出色彩空间
    return *this;  // 返回自身引用
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
/**
 * 构建颜色分级
 * 
 * 根据构建器配置创建颜色分级对象。
 * 
 * @param engine 引擎引用
 * @return 颜色分级指针
 */
ColorGrading* ColorGrading::Builder::build(Engine& engine) {
    /**
     * 我们想查看是否有任何默认调整值已被修改
     * 我们故意跳过色调映射操作符，因为我们总是想应用它
     */
    // We want to see if any of the default adjustment values have been modified
    // We skip the tonemapping operator on purpose since we always want to apply it
    BuilderDetails defaults;  // 创建默认配置
    bool hasAdjustments = defaults != *mImpl;  // 比较是否有调整
    mImpl->hasAdjustments = hasAdjustments;  // 保存调整标志

    /**
     * 为仍使用已弃用的 ToneMapping API 的客户端提供回退
     */
    // Fallback for clients that still use the deprecated ToneMapping API
    bool needToneMapper = mImpl->toneMapper == nullptr;  // 检查是否需要创建色调映射器
    if (needToneMapper) {  // 如果需要
        switch (mImpl->toneMapping) {  // 根据色调映射类型创建
            case ToneMapping::LINEAR:  // 线性
                mImpl->toneMapper = new LinearToneMapper();  // 创建线性色调映射器
                break;
            case ToneMapping::ACES_LEGACY:  // ACES 传统
                mImpl->toneMapper = new ACESLegacyToneMapper();  // 创建 ACES 传统色调映射器
                break;
            case ToneMapping::ACES:  // ACES
                mImpl->toneMapper = new ACESToneMapper();  // 创建 ACES 色调映射器
                break;
            case ToneMapping::FILMIC:  // Filmic
                mImpl->toneMapper = new FilmicToneMapper();  // 创建 Filmic 色调映射器
                break;
            case ToneMapping::DISPLAY_RANGE:  // 显示范围
                mImpl->toneMapper = new DisplayRangeToneMapper();  // 创建显示范围色调映射器
                break;
        }
    }

    FColorGrading* colorGrading = downcast(engine).createColorGrading(*this);  // 创建颜色分级对象

    if (needToneMapper) {  // 如果创建了临时色调映射器
        delete mImpl->toneMapper;  // 删除临时色调映射器
        mImpl->toneMapper = nullptr;  // 清空指针
    }

    return colorGrading;  // 返回颜色分级指针
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

//------------------------------------------------------------------------------
// Exposure
//------------------------------------------------------------------------------

/**
 * 调整曝光
 * 
 * 通过乘以 2^exposure 来调整曝光。
 * 
 * @param v 输入颜色（RGB）
 * @param exposure 曝光值（EV）
 * @return 调整后的颜色
 */
UTILS_ALWAYS_INLINE
inline float3 adjustExposure(float3 const v, float const exposure) {
    return v * std::exp2(exposure);  // 乘以 2^exposure
}

//------------------------------------------------------------------------------
// Purkinje shift/scotopic vision
//------------------------------------------------------------------------------

/**
 * 暗视适应（Purkinje 效应）
 * 
 * 在低光照条件下，人眼的峰值亮度敏感性向可见光谱的蓝色端移动。
 * 这种称为 Purkinje 效应的现象发生在从明视（基于视锥细胞）视觉
 * 到暗视（基于视杆细胞）视觉的过渡期间。由于视杆细胞和视锥细胞
 * 使用相同的神经通路，当视杆细胞接管以改善低光感知时，会引入颜色偏移。
 *
 * 此函数旨在（在一定程度上）复制这种颜色偏移和峰值亮度敏感性的增加，
 * 以更忠实地再现低光照条件下的场景，就像人类观察者感知的那样
 * （而不是像相机传感器这样的人工观察者）。
 *
 * 下面的实现基于两篇论文：
 * "Rod Contributions to Color Perception: Linear with Rod Contrast", Cao et al., 2008
 *     https://www.ncbi.nlm.nih.gov/pmc/articles/PMC2630540/pdf/nihms80286.pdf
 * "Perceptually Based Tone Mapping for Low-Light Conditions", Kirk & O'Brien, 2011
 *     http://graphics.berkeley.edu/papers/Kirk-PBT-2011-08/Kirk-PBT-2011-08.pdf
 *
 * 特别感谢 Jasmin Patry 在 "Real-Time Samurai Cinema"（SIGGRAPH 2021）中的解释，
 * 以及基于 "Maximum Entropy Spectral Modeling Approach to Mesopic Tone Mapping",
 * Rezagholizadeh & Clark, 2013 使用对数亮度的想法。
 * 
 * @param v 输入颜色（RGB，Rec.709）
 * @param nightAdaptation 夜间适应强度（0.0-1.0）
 * @return 适应后的颜色（RGB）
 */
// In low-light conditions, peak luminance sensitivity of the eye shifts toward
// the blue end of the visible spectrum. This effect called the Purkinje effect
// occurs during the transition from photopic (cone-based) vision to scotopic
// (rod-based) vision. Because the rods and cones use the same neural pathways,
// a color shift is introduced as the rods take over to improve low-light
// perception.
//
// This function aims to (somewhat) replicate this color shift and peak luminance
// sensitivity increase to more faithfully reproduce scenes in low-light conditions
// as they would be perceived by a human observer (as opposed to an artificial
// observer such as a camera sensor).
//
// The implementation below is based on two papers:
// "Rod Contributions to Color Perception: Linear with Rod Contrast", Cao et al., 2008
//     https://www.ncbi.nlm.nih.gov/pmc/articles/PMC2630540/pdf/nihms80286.pdf
// "Perceptually Based Tone Mapping for Low-Light Conditions", Kirk & O'Brien, 2011
//     http://graphics.berkeley.edu/papers/Kirk-PBT-2011-08/Kirk-PBT-2011-08.pdf
//
// Many thanks to Jasmin Patry for his explanations in "Real-Time Samurai Cinema",
// SIGGRAPH 2021, and the idea of using log-luminance based on "Maximum Entropy
// Spectral Modeling Approach to Mesopic Tone Mapping", Rezagholizadeh & Clark, 2013
float3 scotopicAdaptation(float3 v, float nightAdaptation) noexcept {
    /**
     * 下面的 4 个向量由命令行工具 rgb-to-lmsr 生成。
     * 它们一起形成一个 4x3 矩阵，可用于将 Rec.709 输入颜色
     * 转换为 LMSR（长/中/短视锥细胞 + 视杆细胞受体）空间。
     * 该矩阵使用以下公式计算：
     *     Mij = \Integral Ei(lambda) I(lambda) Rj(lambda) d(lambda)
     * 其中：
     *     i in {L, M, S, R}（长/中/短视锥细胞/视杆细胞）
     *     j in {R, G, B}（红/绿/蓝）
     *     lambda: 波长
     *     Ei(lambda): 相应受体的响应曲线
     *     I(lambda): CIE 标准光源 D65 的相对光谱功率
     *     Rj(lambda): 相应 Rec.709 颜色的光谱功率
     */
    // The 4 vectors below are generated by the command line tool rgb-to-lmsr.
    // Together they form a 4x3 matrix that can be used to convert a Rec.709
    // input color to the LMSR (long/medium/short cone + rod receptors) space.
    // That matrix is computed using this formula:
    //     Mij = \Integral Ei(lambda) I(lambda) Rj(lambda) d(lambda)
    // Where:
    //     i in {L, M, S, R}
    //     j in {R, G, B}
    //     lambda: wavelength
    //     Ei(lambda): response curve of the corresponding receptor
    //     I(lambda): relative spectral power of the CIE illuminant D65
    //     Rj(lambda): spectral power of the corresponding Rec.709 color
    constexpr float3 L{7.696847f, 18.424824f,  2.068096f};  // 长视锥细胞响应（RGB）
    constexpr float3 M{2.431137f, 18.697937f,  3.012463f};  // 中视锥细胞响应（RGB）
    constexpr float3 S{0.289117f,  1.401833f, 13.792292f};  // 短视锥细胞响应（RGB）
    constexpr float3 R{0.466386f, 15.564362f, 10.059963f};  // 视杆细胞响应（RGB）

    constexpr mat3f LMS_to_RGB = inverse(transpose(mat3f{L, M, S}));  // LMS 到 RGB 的逆变换矩阵

    /**
     * 最大 LMS 视锥细胞敏感性，Cao et al. 表 1
     */
    // Maximal LMS cone sensitivity, Cao et al. Table 1
    constexpr float3 m{0.63721f, 0.39242f, 1.6064f};  // 最大 LMS 敏感性（L, M, S）
    /**
     * 视杆细胞输入强度，Cao et al. 中的自由参数，根据我们的需求手动调整
     * 我们遵循 Kirk & O'Brien 的建议使用常数值，而不是 Cao et al. 提出的
     * 根据视网膜照度调整这些值。相反，我们在过程结束时提供艺术控制。
     * 下面的向量在 Kirk & O'Brien 中是 {k1, k1, k2}，但在 Cao et al. 中是 {k5, k5, k6}
     */
    // Strength of rod input, free parameters in Cao et al., manually tuned for our needs
    // We follow Kirk & O'Brien who recommend constant values as opposed to Cao et al.
    // who propose to adapt those values based on retinal illuminance. We instead offer
    // artistic control at the end of the process
    // The vector below is {k1, k1, k2} in Kirk & O'Brien, but {k5, k5, k6} in Cao et al.
    constexpr float3 k{0.2f, 0.2f, 0.3f};  // 视杆细胞输入强度（L, M, S）

    /**
     * 从对立空间转换回 LMS
     */
    // Transform from opponent space back to LMS
    constexpr mat3f opponent_to_LMS{  // 对立空间到 LMS 的变换矩阵
        -0.5f, 0.5f, 0.0f,  // 第一行
         0.0f, 0.0f, 1.0f,  // 第二行
         0.5f, 0.5f, 1.0f   // 第三行
    };

    /**
     * 以下常量遵循 Cao et al.，使用 KC 通路
     */
    // The constants below follow Cao et al, using the KC pathway
    constexpr float K_ = 45.0f;  // 缩放常数
    constexpr float S_ = 10.0f;  // 静态饱和度
    constexpr float k3 = 0.6f;  // 对立信号的环境强度
    constexpr float rw = 0.139f;  // 白光的响应比
    constexpr float p  = 0.6189f;  // L 视锥细胞的相对权重

    /**
     * 加权视锥细胞响应，如 Cao et al. 第 3.3 节所述
     * 论文中定义的近似线性关系在这里以矩阵形式表示以简化代码
     */
    // Weighted cone response as described in Cao et al., section 3.3
    // The approximately linear relation defined in the paper is represented here
    // in matrix form to simplify the code
    constexpr mat3f weightedRodResponse = (K_ / S_) * mat3f{  // 加权视杆细胞响应矩阵
       -(k3 + rw),       p * k3,          p * S_,  // 第一行
        1.0f + k3 * rw, (1.0f - p) * k3, (1.0f - p) * S_,  // 第二行
        0.0f,            1.0f,            0.0f  // 第三行
    } * mat3f{k} * inverse(mat3f{m});  // 乘以视杆细胞输入强度和最大敏感性的逆

    /**
     * 移动到对数亮度，或由 Minolta Spotmeter F 测量的 EV 值。
     * 关系是 EV = log2(L * 100 / 14)，或 2^EV = L / 0.14。因此我们可以
     * 将输入乘以 0.14 以获得对数亮度值。
     * 然后我们遵循 Patry 的建议将对数亮度偏移约 +11.4EV 以
     * 匹配 Rezagholizadeh & Clark 2013 中描述的中间视觉测量值。
     * 结果是 0.14 * exp2(11.40) ~= 380.0（我们使用 +11.406 EV 以获得整数）
     */
    // Move to log-luminance, or the EV values as measured by a Minolta Spotmeter F.
    // The relationship is EV = log2(L * 100 / 14), or 2^EV = L / 0.14. We can therefore
    // multiply our input by 0.14 to obtain our log-luminance values.
    // We then follow Patry's recommendation to shift the log-luminance by ~ +11.4EV to
    // match luminance values to mesopic measurements as described in Rezagholizadeh &
    // Clark 2013,
    // The result is 0.14 * exp2(11.40) ~= 380.0 (we use +11.406 EV to get a round number)
    constexpr float logExposure = 380.0f;  // 对数曝光缩放因子

    /**
     * 移动到缩放的对数亮度
     */
    // Move to scaled log-luminance
    v *= logExposure;  // 缩放输入颜色

    /**
     * 将场景颜色从 Rec.709 转换为 LMSR 响应
     */
    // Convert the scene color from Rec.709 to LMSR response
    float4 q{dot(v, L), dot(v, M), dot(v, S), dot(v, R)};  // 计算 LMSR 响应（L, M, S, R）
    /**
     * 通过选定通路（Cao et al. 中的 KC）的调节信号
     */
    // Regulated signal through the selected pathway (KC in Cao et al.)
    float3 g = inversesqrt(1.0f + max(float3{0.0f}, (0.33f / m) * (q.rgb + k * q.w)));  // 计算调节信号

    /**
     * 计算视杆细胞在对立空间中的增量效应
     */
    // Compute the incremental effect that rods have in opponent space
    float3 deltaOpponent = weightedRodResponse * g * q.w * nightAdaptation;  // 计算增量效应
    /**
     * LMS 空间中的明视响应
     */
    // Photopic response in LMS space
    float3 qHat = q.rgb + opponent_to_LMS * deltaOpponent;  // 计算明视响应（LMS + 视杆细胞贡献）

    /**
     * 最后，回到 RGB
     */
    // And finally, back to RGB
    return (LMS_to_RGB * qHat) / logExposure;  // 转换回 RGB 并取消对数曝光缩放
}

//------------------------------------------------------------------------------
// White balance
//------------------------------------------------------------------------------

/**
 * 计算色适应变换矩阵
 * 
 * 返回给定色温/色调偏移的 LMS 空间中的色适应系数。
 * 色适应遵循 von Kries 方法，使用 CIECAT16 变换。
 * 
 * 参考：
 * - https://en.wikipedia.org/wiki/Chromatic_adaptation
 * - https://en.wikipedia.org/wiki/CIECAM02#Chromatic_adaptation
 * 
 * @param whiteBalance 白平衡（x: 色温，y: 色调）
 * @return 色适应变换矩阵（3x3）
 */
// Return the chromatic adaptation coefficients in LMS space for the given
// temperature/tint offsets. The chromatic adaption is perfomed following
// the von Kries method, using the CIECAT16 transform.
// See https://en.wikipedia.org/wiki/Chromatic_adaptation
// See https://en.wikipedia.org/wiki/CIECAM02#Chromatic_adaptation
constexpr mat3f adaptationTransform(float2 const whiteBalance) noexcept {
    /**
     * 参见 Mathematica 笔记本 docs/math/White Balance.nb
     */
    // See Mathematica notebook in docs/math/White Balance.nb
    float k = whiteBalance.x;  // 色温
    float t = whiteBalance.y;  // 色调

    /**
     * 计算目标白点的色度坐标
     */
    float x = ILLUMINANT_D65_xyY[0] - k * (k < 0.0f ? 0.0214f : 0.066f);  // x 色度坐标
    float y = chromaticityCoordinateIlluminantD(x) + t * 0.066f;  // y 色度坐标

    /**
     * 转换为 LMS 空间并计算适应变换
     */
    float3 lms = XYZ_to_CIECAT16 * xyY_to_XYZ({x, y, 1.0f});  // 转换为 LMS 空间
    return LMS_CAT16_to_Rec2020 * mat3f{ILLUMINANT_D65_LMS_CAT16 / lms} * Rec2020_to_LMS_CAT16;  // 计算适应变换矩阵
}

/**
 * 色适应
 * 
 * 应用色适应变换矩阵。
 * 
 * @param v 输入颜色（RGB）
 * @param adaptationTransform 色适应变换矩阵
 * @return 适应后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline float3 chromaticAdaptation(float3 const v, mat3f adaptationTransform) {
    return adaptationTransform * v;  // 应用变换矩阵
}

//------------------------------------------------------------------------------
// General color grading
//------------------------------------------------------------------------------

using ColorTransform = float3(*)(float3);  // 颜色变换函数指针类型

/**
 * 通道混合器
 * 
 * 混合输入颜色的通道以生成新的 RGB 值。
 * 
 * @param v 输入颜色（RGB）
 * @param r 红色输出通道混合系数（R, G, B）
 * @param g 绿色输出通道混合系数（R, G, B）
 * @param b 蓝色输出通道混合系数（R, G, B）
 * @return 混合后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline constexpr float3 channelMixer(float3 v, float3 r, float3 g, float3 b) {
    return {dot(v, r), dot(v, g), dot(v, b)};  // 对每个输出通道进行点积
}

/**
 * 色调范围（阴影/中间调/高光）
 * 
 * 根据亮度值将颜色分为阴影、中间调和高光区域，并分别应用调整。
 * 
 * 参见 Mathematica 笔记本 docs/math/Shadows Midtones Highlight.nb 了解
 * 曲线设计的详细信息。默认曲线值基于 DaVinci Resolve 中 "Log" 颜色轮的默认值。
 * 
 * @param v 输入颜色（RGB）
 * @param luminance 亮度权重（用于计算亮度）
 * @param shadows 阴影调整（RGB 乘数）
 * @param midtones 中间调调整（RGB 乘数）
 * @param highlights 高光调整（RGB 乘数）
 * @param ranges 色调范围（阴影开始、暗部结束、亮部开始、高光结束）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline constexpr float3 tonalRanges(
        float3 v, float3 luminance,
        float3 shadows, float3 midtones, float3 highlights,
        float4 ranges
) {
    /**
     * 参见 Mathematica 笔记本 docs/math/Shadows Midtones Highlight.nb 了解
     * 曲线设计的详细信息。默认曲线值基于 DaVinci Resolve 中 "Log" 颜色轮的默认值。
     */
    // See the Mathematica notebook at docs/math/Shadows Midtones Highlight.nb for
    // details on how the curves were designed. The default curve values are based
    // on the defaults from the "Log" color wheels in DaVinci Resolve.
    float y = dot(v, luminance);  // 计算亮度

    /**
     * 阴影曲线（从 1.0 平滑过渡到 0.0）
     */
    // Shadows curve
    float s = 1.0f - smoothstep(ranges.x, ranges.y, y);  // 阴影权重
    /**
     * 高光曲线（从 0.0 平滑过渡到 1.0）
     */
    // Highlights curve
    float h = smoothstep(ranges.z, ranges.w, y);  // 高光权重
    /**
     * 中间调曲线（1.0 - 阴影 - 高光）
     */
    // Mid-tones curves
    float m = 1.0f - s - h;  // 中间调权重

    return v * s * shadows + v * m * midtones + v * h * highlights;  // 加权混合
}

/**
 * 颜色决策列表（ASC CDL）
 * 
 * 应用 ASC CSL（American Society of Cinematographers Color Science Language）
 * 在对数空间中，如 S-2016-001 中定义。
 * 
 * 公式：output = pow(input * slope + offset, power)
 * 
 * @param v 输入颜色（RGB，对数空间）
 * @param slope 斜率（RGB）
 * @param offset 偏移（RGB）
 * @param power 幂（RGB）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline float3 colorDecisionList(float3 v, float3 slope, float3 offset, float3 power) {
    /**
     * 在对数空间中应用 ASC CSL，如 S-2016-001 中定义
     */
    // Apply the ASC CSL in log space, as defined in S-2016-001
    v = v * slope + offset;  // 应用斜率和偏移
    float3 pv = pow(v, power);  // 应用幂
    /**
     * 如果输入为负，则跳过幂运算（保持负值）
     */
    return float3{
            v.r <= 0.0f ? v.r : pv.r,  // 红色通道
            v.g <= 0.0f ? v.g : pv.g,  // 绿色通道
            v.b <= 0.0f ? v.b : pv.b   // 蓝色通道
    };
}

/**
 * 对比度
 * 
 * 调整对比度，匹配 DaVinci Resolve 中的应用方式。
 * 
 * @param v 输入颜色（RGB，对数空间）
 * @param contrast 对比度值（1.0 表示无变化）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline constexpr float3 contrast(float3 const v, float const contrast) {
    /**
     * 匹配 DaVinci Resolve 中应用的对比度
     */
    // Matches contrast as applied in DaVinci Resolve
    return MIDDLE_GRAY_ACEScct + contrast * (v - MIDDLE_GRAY_ACEScct);  // 以中间灰为基准调整
}

/**
 * 饱和度
 * 
 * 调整饱和度，影响所有颜色。
 * 
 * @param v 输入颜色（RGB）
 * @param luminance 亮度权重（用于计算亮度）
 * @param saturation 饱和度值（1.0 表示无变化）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline constexpr float3 saturation(float3 v, float3 luminance, float saturation) {
    const float3 y = dot(v, luminance);  // 计算亮度
    return y + saturation * (v - y);  // 在亮度和颜色之间插值
}

/**
 * 自然饱和度（Vibrance）
 * 
 * 调整自然饱和度，只影响低饱和度颜色，保护已经饱和的颜色。
 * 
 * @param v 输入颜色（RGB）
 * @param luminance 亮度权重（用于计算亮度）
 * @param vibrance 自然饱和度值（1.0 表示无变化）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline float3 vibrance(float3 v, float3 luminance, float vibrance) {
    float r = v.r - max(v.g, v.b);  // 计算红色通道的相对强度
    /**
     * 使用 sigmoid 函数根据饱和度调整强度
     */
    float s = (vibrance - 1.0f) / (1.0f + std::exp(-r * 3.0f)) + 1.0f;  // sigmoid 函数
    float3 l{(1.0f - s) * luminance};  // 亮度分量
    /**
     * 对每个通道应用不同的饱和度
     */
    return float3{
        dot(v, l + float3{s, 0.0f, 0.0f}),  // 红色通道
        dot(v, l + float3{0.0f, s, 0.0f}),  // 绿色通道
        dot(v, l + float3{0.0f, 0.0f, s}),  // 蓝色通道
    };

}

/**
 * 曲线调整
 * 
 * 应用 RGB 曲线调整，包括阴影伽马、中间点和高光缩放。
 * 
 * 参考："Practical HDR and Wide Color Techniques in Gran Turismo SPORT", Uchimura 2018
 * 
 * @param v 输入颜色（RGB）
 * @param shadowGamma 阴影伽马（RGB，用于调整阴影曲线）
 * @param midPoint 中间点（RGB，用于分割阴影和高光）
 * @param highlightScale 高光缩放（RGB，用于调整高光曲线）
 * @return 调整后的颜色（RGB）
 */
UTILS_ALWAYS_INLINE
inline float3 curves(float3 v, float3 shadowGamma, float3 midPoint, float3 highlightScale) {
    /**
     * 参考："Practical HDR and Wide Color Techniques in Gran Turismo SPORT", Uchimura 2018
     */
    // "Practical HDR and Wide Color Techniques in Gran Turismo SPORT", Uchimura 2018
    float3 d = 1.0f / (pow(midPoint, shadowGamma - 1.0f));  // 阴影曲线归一化因子
    float3 dark = pow(v, shadowGamma) * d;  // 阴影曲线（幂函数）
    float3 light = highlightScale * (v - midPoint) + midPoint;  // 高光曲线（线性）
    /**
     * 根据中间点选择阴影或高光曲线
     */
    return float3{
        v.r <= midPoint.r ? dark.r : light.r,  // 红色通道
        v.g <= midPoint.g ? dark.g : light.g,  // 绿色通道
        v.b <= midPoint.b ? dark.b : light.b,  // 蓝色通道
    };
}

//------------------------------------------------------------------------------
// Luminance scaling
//------------------------------------------------------------------------------

/**
 * 亮度缩放
 * 
 * 实现 EVILS（Exposure Value Invariant Luminance Scaling）算法，
 * 用于在保持色度的同时调整亮度。
 * 
 * 参考：Troy Sobotka, 2021, "EVILS - Exposure Value Invariant Luminance Scaling"
 * https://colab.research.google.com/drive/1iPJzNNKR7PynFmsqSnQm3bCZmQ3CvAJ-#scrollTo=psU43hb-BLzB
 * 
 * @param x 输入颜色（RGB）
 * @param toneMapper 色调映射器引用
 * @param luminanceWeights 亮度权重（用于计算亮度）
 * @return 调整后的颜色（RGB）
 */
static float3 luminanceScaling(float3 x,
        const ToneMapper& toneMapper, float3 luminanceWeights) noexcept {

    /**
     * 参考：Troy Sobotka, 2021, "EVILS - Exposure Value Invariant Luminance Scaling"
     * https://colab.research.google.com/drive/1iPJzNNKR7PynFmsqSnQm3bCZmQ3CvAJ-#scrollTo=psU43hb-BLzB
     */
    // Troy Sobotka, 2021, "EVILS - Exposure Value Invariant Luminance Scaling"
    // https://colab.research.google.com/drive/1iPJzNNKR7PynFmsqSnQm3bCZmQ3CvAJ-#scrollTo=psU43hb-BLzB

    float luminanceIn = dot(x, luminanceWeights);  // 计算输入亮度

    /**
     * TODO: 我们可以针对单通道亮度的情况进行优化
     */
    // TODO: We could optimize for the case of single-channel luminance
    float luminanceOut = toneMapper(luminanceIn).y;  // 计算输出亮度（使用色调映射器）

    float peak = max(x);  // 计算峰值（最大通道值）
    float3 chromaRatio = max(x / peak, 0.0f);  // 计算色度比（归一化颜色，避免除零）

    float chromaRatioLuminance = dot(chromaRatio, luminanceWeights);  // 计算色度比亮度

    float3 maxReserves = 1.0f - chromaRatio;  // 计算最大储备（可用于增加亮度的空间）
    float maxReservesLuminance = dot(maxReserves, luminanceWeights);  // 计算最大储备亮度

    float luminanceDifference = std::max(luminanceOut - chromaRatioLuminance, 0.0f);  // 计算亮度差（需要增加的亮度）
    float scaledLuminanceDifference =
            luminanceDifference / std::max(maxReservesLuminance, std::numeric_limits<float>::min());  // 缩放亮度差

    float chromaScale = (luminanceOut - luminanceDifference) /
            std::max(chromaRatioLuminance, std::numeric_limits<float>::min());  // 计算色度缩放因子

    return chromaScale * chromaRatio + scaledLuminanceDifference * maxReserves;  // 组合色度和亮度调整
}

//------------------------------------------------------------------------------
// Quality
//------------------------------------------------------------------------------

/**
 * 选择 LUT 纹理参数
 * 
 * 根据 LUT 格式和维度选择适当的纹理格式、像素数据格式和数据类型。
 * 
 * @param lutFormat LUT 格式（INTEGER 或 FLOAT）
 * @param isOneDimensional 是否为一维 LUT
 * @return 纹理格式、像素数据格式和数据类型的元组
 */
static std::tuple<TextureFormat, PixelDataFormat, PixelDataType> selectLutTextureParams(
        ColorGrading::LutFormat const lutFormat, const bool isOneDimensional) noexcept {
    if (isOneDimensional) {  // 如果是一维 LUT
        return { TextureFormat::R16F, PixelDataFormat::R, PixelDataType::HALF };  // 使用单通道半精度浮点
    }
    /**
     * 我们使用 RGBA16F 而不是 RGB16F 用于高质量模式，因为 RGB16F
     * 并非在所有地方都受支持
     */
    // We use RGBA16F for high quality modes instead of RGB16F because RGB16F
    // is not supported everywhere
    switch (lutFormat) {  // 根据格式选择
        case ColorGrading::LutFormat::INTEGER:  // 整数格式
            return { TextureFormat::RGB10_A2, PixelDataFormat::RGBA, PixelDataType::UINT_2_10_10_10_REV };  // 使用 10-10-10-2 打包格式
        case ColorGrading::LutFormat::FLOAT:  // 浮点格式
            return { TextureFormat::RGBA16F, PixelDataFormat::RGBA, PixelDataType::HALF };  // 使用半精度浮点
    }
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
/**
 * 以下函数存在是为了保持与通过已弃用的 `ToneMapping` API 设置的 `FILMIC` 的向后兼容性。
 * 选择 `ToneMapping::FILMIC` 强制后处理在 sRGB 中执行，以确保着色器中的逆色调映射函数
 * 与正向色调映射步骤完全匹配。
 */
// The following functions exist to preserve backward compatibility with the
// `FILMIC` set via the deprecated `ToneMapping` API. Selecting `ToneMapping::FILMIC`
// forces post-processing to be performed in sRGB to guarantee that the inverse tone
// mapping function in the shaders will match the forward tone mapping step exactly.

/**
 * 选择颜色分级输入变换
 * 
 * 根据色调映射类型选择输入色彩空间变换矩阵。
 * 
 * @param toneMapping 色调映射类型
 * @return 输入变换矩阵（3x3）
 */
static mat3f selectColorGradingTransformIn(ColorGrading::ToneMapping const toneMapping) noexcept {
    if (toneMapping == ColorGrading::ToneMapping::FILMIC) {  // 如果是 FILMIC
        return mat3f{};  // 返回单位矩阵（sRGB 空间，无需变换）
    }
    return sRGB_to_Rec2020;  // 否则从 sRGB 转换到 Rec.2020
}

/**
 * 选择颜色分级输出变换
 * 
 * 根据色调映射类型选择输出色彩空间变换矩阵。
 * 
 * @param toneMapping 色调映射类型
 * @return 输出变换矩阵（3x3）
 */
static mat3f selectColorGradingTransformOut(ColorGrading::ToneMapping const toneMapping) noexcept {
    if (toneMapping == ColorGrading::ToneMapping::FILMIC) {  // 如果是 FILMIC
        return mat3f{};  // 返回单位矩阵（sRGB 空间，无需变换）
    }
    return Rec2020_to_sRGB;  // 否则从 Rec.2020 转换回 sRGB
}

/**
 * 选择颜色分级亮度权重
 * 
 * 根据色调映射类型选择亮度权重。
 * 
 * @param toneMapping 色调映射类型
 * @return 亮度权重（RGB）
 */
static float3 selectColorGradingLuminance(ColorGrading::ToneMapping const toneMapping) noexcept {
    if (toneMapping == ColorGrading::ToneMapping::FILMIC) {  // 如果是 FILMIC
        return LUMINANCE_Rec709;  // 使用 Rec.709 亮度权重
    }
    return LUMINANCE_Rec2020;  // 否则使用 Rec.2020 亮度权重
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

using ColorTransform = float3(*)(float3);  // 颜色变换函数指针类型

/**
 * 选择输出电光传递函数（OETF）
 * 
 * 根据色彩空间选择输出电光传递函数。
 * 
 * @param colorSpace 色彩空间
 * @return 输出电光传递函数指针
 */
static ColorTransform selectOETF(const ColorSpace& colorSpace) noexcept {
    if (colorSpace.getTransferFunction() == Linear) {  // 如果是线性传递函数
        return OETF_Linear;  // 返回线性 OETF
    }
    return OETF_sRGB;  // 否则返回 sRGB OETF
}

//------------------------------------------------------------------------------
// Color grading implementation
//------------------------------------------------------------------------------

/**
 * 配置结构
 * 
 * 存储颜色分级的配置参数。
 */
struct Config {
    size_t lutDimension{};  // LUT 维度
    mat3f  adaptationTransform;  // 色适应变换矩阵（白平衡）
    mat3f  colorGradingIn;  // 颜色分级输入变换矩阵（工作色彩空间）
    mat3f  colorGradingOut;  // 颜色分级输出变换矩阵（显示色彩空间）
    float3 colorGradingLuminance{};  // 颜色分级亮度权重

    ColorTransform oetf;  // 输出电光传递函数
};

/**
 * 颜色分级构造函数
 * 
 * 创建颜色分级对象并生成查找表（LUT）。
 * 
 * 注意：在 FColorGrading 构造函数内部，TSAN 偶尔会检测到 config 结构上的数据竞争；
 * Filament 线程写入，Job 线程读取。实际上不应该有数据竞争，所以我们强制关闭 TSAN 以消除警告。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
// Inside the FColorGrading constructor, TSAN sporadically detects a data race on the config struct;
// the Filament thread writes and the Job thread reads. In practice there should be no data race, so
// we force TSAN off to silence the warning.
UTILS_NO_SANITIZE_THREAD
FColorGrading::FColorGrading(FEngine& engine, const Builder& builder) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用

    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API

    /**
     * XXX: 以下两个条件也仅在输入和输出色彩空间相同时成立，
     * 但我们目前不检查这一点。如果我们处理这种情况，必须修改这些条件。
     */
    // XXX: The following two conditions also only hold true as long as the input and output color
    // spaces are the same, but we currently don't check that. We must revise these conditions if we
    // ever handle this case.
    /**
     * 确定是否使用一维 LUT
     * 
     * 条件：
     * - 没有调整
     * - 没有亮度缩放
     * - 色调映射器是一维的
     * - 引擎支持一维 LUT
     */
    mIsOneDimensional = !builder->hasAdjustments && !builder->luminanceScaling
            && builder->toneMapper->isOneDimensional()
            && engine.features.engine.color_grading.use_1d_lut;  // 检查是否使用一维 LUT
    mIsLDR = mIsOneDimensional && builder->toneMapper->isLDR();  // 如果是一维 LUT 且色调映射器是 LDR，则为 LDR

    /**
     * 创建配置
     */
    const Config config = {
            mIsOneDimensional ? 512u : builder->dimension,  // LUT 维度（一维使用 512，三维使用构建器维度）
            adaptationTransform(builder->whiteBalance),  // 色适应变换（白平衡）
            selectColorGradingTransformIn(builder->toneMapping),  // 输入变换矩阵
            selectColorGradingTransformOut(builder->toneMapping),  // 输出变换矩阵
            selectColorGradingLuminance(builder->toneMapping),  // 亮度权重
            selectOETF(builder->outputColorSpace),  // 输出电光传递函数
    };

    mDimension = config.lutDimension;  // 保存维度

    /**
     * 计算纹理尺寸
     */
    uint32_t width;   // 宽度
    uint32_t height;  // 高度
    uint32_t depth;   // 深度
    if (mIsOneDimensional) {  // 如果是一维 LUT
        width = config.lutDimension;  // 宽度等于维度
        height = 1;  // 高度为 1
        depth = 1;   // 深度为 1
    } else {  // 否则是三维 LUT
        width = config.lutDimension;   // 宽度等于维度
        height = config.lutDimension;  // 高度等于维度
        depth = config.lutDimension;  // 深度等于维度
    }

    size_t lutElementCount = width * height * depth;  // 计算元素总数
    size_t elementSize = mIsOneDimensional ? sizeof(half) : sizeof(half4);  // 计算元素大小（一维为 half，三维为 half4）
    void* data = malloc(lutElementCount * elementSize);  // 分配内存

    /**
     * 选择纹理参数
     */
    auto [textureFormat, format, type] =
            selectLutTextureParams(builder->format, mIsOneDimensional);  // 选择纹理参数
    assert_invariant(FTexture::isTextureFormatSupported(engine, textureFormat));  // 断言格式受支持
    assert_invariant(FTexture::validatePixelFormatAndType(textureFormat, format, type));  // 断言格式和类型有效

    /**
     * 如果需要转换为 UINT_2_10_10_10_REV，分配转换缓冲区
     */
    void* converted = nullptr;  // 转换缓冲区指针
    if (type == PixelDataType::UINT_2_10_10_10_REV) {  // 如果需要转换为打包格式
        /**
         * 如果需要，将输入转换为 UINT_2_10_10_10_REV
         */
        // convert input to UINT_2_10_10_10_REV if needed
        converted = malloc(lutElementCount * sizeof(uint32_t));  // 分配转换缓冲区
    }

    /**
     * HDR 颜色计算 Lambda 函数
     * 
     * 计算给定 LUT 索引处的 HDR 颜色值。
     * 这个函数实现了完整的颜色分级管线。
     * 
     * @param r 红色索引
     * @param g 绿色索引
     * @param b 蓝色索引
     * @return 处理后的颜色（RGB）
     */
    auto hdrColorAt = [builder, config](size_t r, size_t g, size_t b) {
        /**
         * 将索引归一化到 [0, 1] 范围
         */
        float3 v = float3{r, g, b} * (1.0f / float(config.lutDimension - 1u));  // 归一化索引

        /**
         * LogC 编码（从对数空间转换到线性空间）
         */
        // LogC encoding
        v = LogC_to_linear(v);  // 转换为线性空间

        /**
         * 消除对数转换中由于精度问题导致的接近 0.0f 的负值
         */
        // Kill negative values near 0.0f due to imprecision in the log conversion
        v = max(v, 0.0f);  // 限制为非负

        if (builder->hasAdjustments) {  // 如果有调整
            /**
             * 曝光调整
             */
            // Exposure
            v = adjustExposure(v, builder->exposure);  // 调整曝光

            /**
             * Purkinje 偏移（"低光"视觉）
             */
            // Purkinje shift ("low-light" vision)
            v = scotopicAdaptation(v, builder->nightAdaptation);  // 暗视适应
        }

        /**
         * 移动到颜色分级色彩空间
         */
        // Move to color grading color space
        v = config.colorGradingIn * v;  // 应用输入变换

        if (builder->hasAdjustments) {  // 如果有调整
            /**
             * 白平衡
             */
            // White balance
            v = chromaticAdaptation(v, config.adaptationTransform);  // 色适应（白平衡）

            /**
             * 在下一个变换之前消除负值
             */
            // Kill negative values before the next transforms
            v = max(v, 0.0f);  // 限制为非负

            /**
             * 通道混合器
             */
            // Channel mixer
            v = channelMixer(v, builder->outRed, builder->outGreen, builder->outBlue);  // 通道混合

            /**
             * 阴影/中间调/高光
             */
            // Shadows/mid-tones/highlights
            v = tonalRanges(v, config.colorGradingLuminance,
                    builder->shadows, builder->midtones, builder->highlights,
                    builder->tonalRanges);  // 色调范围调整

            /**
             * 以下调整在对数空间中表现更好
             */
            // The adjustments below behave better in log space
            v = linear_to_LogC(v);  // 转换到对数空间

            /**
             * ASC CDL
             */
            // ASC CDL
            v = colorDecisionList(v, builder->slope, builder->offset, builder->power);  // ASC CDL

            /**
             * 对数空间中的对比度
             */
            // Contrast in log space
            v = contrast(v, builder->contrast);  // 对比度调整

            /**
             * 回到线性空间
             */
            // Back to linear space
            v = LogC_to_linear(v);  // 转换回线性空间

            /**
             * 线性空间中的自然饱和度
             */
            // Vibrance in linear space
            v = vibrance(v, config.colorGradingLuminance, builder->vibrance);  // 自然饱和度

            /**
             * 线性空间中的饱和度
             */
            // Saturation in linear space
            v = saturation(v, config.colorGradingLuminance, builder->saturation);  // 饱和度

            /**
             * 在曲线之前消除负值
             */
            // Kill negative values before curves
            v = max(v, 0.0f);  // 限制为非负

            /**
             * RGB 曲线
             */
            // RGB curves
            v = curves(v,
                    builder->shadowGamma, builder->midPoint, builder->highlightScale);  // RGB 曲线
        }

        /**
         * 色调映射
         */
        // Tone mapping
        if (builder->luminanceScaling) {  // 如果启用亮度缩放
            v = luminanceScaling(v, *builder->toneMapper, config.colorGradingLuminance);  // 使用亮度缩放
        } else {  // 否则
            v = (*builder->toneMapper)(v);  // 直接应用色调映射器
        }

        /**
         * 回到显示色彩空间
         */
        // Go back to display color space
        v = config.colorGradingOut * v;  // 应用输出变换

        /**
         * 应用色域映射
         */
        // Apply gamut mapping
        if (builder->gamutMapping) {  // 如果启用色域映射
            /**
             * TODO: 这应该取决于输出色彩空间
             */
            // TODO: This should depend on the output color space
            v = gamutMapping_sRGB(v);  // 应用 sRGB 色域映射
        }

        /**
         * TODO: 如果我们使用不是 sRGB 的工作色彩空间，应该转换到输出色彩空间
         * TODO: 允许用户自定义输出色彩空间
         */
        // TODO: We should convert to the output color space if we use a working
        //       color space that's not sRGB
        // TODO: Allow the user to customize the output color space

        /**
         * 我们需要为输出传递函数进行限制
         */
        // We need to clamp for the output transfer function
        v = saturate(v);  // 限制到 [0, 1]

        /**
         * 应用 OETF
         */
        // Apply OETF
        v = config.oetf(v);  // 应用输出电光传递函数

        return v;  // 返回处理后的颜色
    };

    /**
     * 性能计时（已注释，用于调试）
     */
    //auto now = std::chrono::steady_clock::now();

    if (mIsOneDimensional) {  // 如果是一维 LUT
        half* UTILS_RESTRICT p = (half*) data;  // 获取数据指针
        if (mIsLDR) {  // 如果是 LDR
            /**
             * LDR 一维 LUT：只应用色调映射
             */
            for (size_t rgb = 0; rgb < config.lutDimension; rgb++) {  // 遍历每个索引
                float3 v = float3(rgb) * (1.0f / float(config.lutDimension - 1u));  // 归一化索引

                v = (*builder->toneMapper)(float3(v));  // 应用色调映射器

                /**
                 * 我们需要为输出传递函数进行限制
                 */
                // We need to clamp for the output transfer function
                v = saturate(v);  // 限制到 [0, 1]

                /**
                 * 应用 OETF
                 */
                // Apply OETF
                v = config.oetf(v);  // 应用输出电光传递函数

                *p++ = half(v.r);  // 存储红色通道
            }
        } else {  // 否则是 HDR
            /**
             * HDR 一维 LUT：使用完整管线（对角线，r=g=b）
             */
            for (size_t rgb = 0; rgb < config.lutDimension; rgb++) {  // 遍历每个索引
                *p++ = half(hdrColorAt(rgb, rgb, rgb).r);  // 计算并存储（对角线）
            }
        }
    } else {  // 否则是三维 LUT
        /**
         * 多线程生成色调映射 3D 查找表，使用多个作业
         * 切片之间相距 8 KiB（128 个缓存行）。
         * 在 Android Release 版本中大约需要 3-6ms
         */
        // Multithreadedly generate the tone mapping 3D look-up table using 32 jobs
        // Slices are 8 KiB (128 cache lines) apart.
        // This takes about 3-6ms on Android in Release
        JobSystem& js = engine.getJobSystem();  // 获取作业系统
        auto *slices = js.createJob();  // 创建父作业
        for (size_t b = 0; b < config.lutDimension; b++) {  // 遍历每个蓝色切片
            auto* job = js.createJob(slices,  // 创建子作业
                    [data, converted, b, &config, builder, &hdrColorAt](  // Lambda 捕获
                            JobSystem&, JobSystem::Job*) {
                /**
                 * 获取当前切片的指针
                 */
                half4* UTILS_RESTRICT p =
                        (half4*) data + b * config.lutDimension * config.lutDimension;  // 切片指针
                for (size_t g = 0; g < config.lutDimension; g++) {  // 遍历绿色索引
                    for (size_t r = 0; r < config.lutDimension; r++) {  // 遍历红色索引
                        *p++ = half4{hdrColorAt(r, g, b), 0.0f};  // 计算并存储颜色
                    }
                }

                if (converted) {  // 如果需要转换
                    /**
                     * 转换到 UINT_2_10_10_10_REV 格式
                     */
                    uint32_t* const UTILS_RESTRICT dst = (uint32_t*) converted +
                            b * config.lutDimension * config.lutDimension;  // 目标指针
                    half4* UTILS_RESTRICT src = (half4*) data +
                            b * config.lutDimension * config.lutDimension;  // 源指针
                    /**
                     * 我们使用向量化宽度为 8，因为在 ARMv8 上它允许编译器
                     * 一次写入八个 32 位结果。
                     */
                    // we use a vectorize width of 8 because, on ARMv8 it allows the compiler to
                    // write eight 32-bits results in one go.
                    const size_t count = (config.lutDimension * config.lutDimension) & ~0x7u;  // 告诉编译器我们是 8 的倍数
#if defined(__clang__)
#pragma clang loop vectorize_width(8)  // 向量化宽度为 8
#endif
                    for (size_t i = 0; i < count; ++i) {  // 遍历元素
                        float4 v{src[i]};  // 读取源值
                        uint32_t pr = uint32_t(std::floor(v.x * 1023.0f + 0.5f));  // 红色通道（10 位）
                        uint32_t pg = uint32_t(std::floor(v.y * 1023.0f + 0.5f));  // 绿色通道（10 位）
                        uint32_t pb = uint32_t(std::floor(v.z * 1023.0f + 0.5f));  // 蓝色通道（10 位）
                        dst[i] = (pb << 20u) | (pg << 10u) | pr;  // 打包为 32 位（RGB10_A2）
                    }
                }
            });
            js.run(job);  // 运行作业
        }

        /**
         * TODO: 我们应该执行 runAndRetain() 并延迟 wait() + 纹理创建，直到
         *       getHwHandle() 被调用？
         */
        // TODO: Should we do a runAndRetain() and defer the wait() + texture creation until
        //       getHwHandle() is invoked?
        js.runAndWait(slices);  // 运行并等待所有作业完成
    }

    /**
     * 性能计时（已注释，用于调试）
     */
    //std::chrono::duration<float, std::milli> duration = std::chrono::steady_clock::now() - now;
    //DLOG(INFO) << "LUT generation time: " << duration.count() << " ms";

    if (converted) {  // 如果使用了转换缓冲区
        free(data);  // 释放原始数据
        data = converted;  // 使用转换后的数据
        elementSize = sizeof(uint32_t);  // 更新元素大小
    }

    /**
     * 创建纹理
     */
    // Create texture.
    mLutHandle = driver.createTexture(SamplerType::SAMPLER_3D, 1, textureFormat, 1,
            width, height, depth, TextureUsage::DEFAULT);  // 创建 3D 纹理

    /**
     * 更新纹理数据
     */
    driver.update3DImage(mLutHandle, 0,  // 更新 3D 图像
            0, 0, 0,  // 偏移（x, y, z）
            width, height, depth,  // 尺寸
            PixelBufferDescriptor{  // 像素缓冲区描述符
                data, lutElementCount * elementSize, format, type,  // 数据、大小、格式、类型
                        [](void* buffer, size_t, void*) { free(buffer); }  // 释放回调
                        });
}

/**
 * 颜色分级析构函数
 */
FColorGrading::~FColorGrading() noexcept = default;

/**
 * 终止颜色分级
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FColorGrading::terminate(FEngine& engine) {
    DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyTexture(mLutHandle);  // 销毁 LUT 纹理
}

} //namespace filament
