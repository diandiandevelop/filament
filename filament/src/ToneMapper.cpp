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

#include <filament/ToneMapper.h>

#include "ColorSpaceUtils.h"

#include <math/vec3.h>
#include <math/scalar.h>

namespace filament {

using namespace math;

namespace aces {

/**
 * 计算 RGB 颜色的饱和度
 * 
 * 饱和度定义为色域范围与最大值的比率。
 * 
 * @param rgb RGB 颜色值（ACES 颜色空间）
 * @return 饱和度值（0 到 1 之间）
 * 
 * 实现细节：
 * - 计算 RGB 的最小值和最大值
 * - 使用 TINY 常量避免除零
 * - 返回归一化的饱和度值
 */
inline float rgb_2_saturation(float3 const rgb) {
    // Input:  ACES
    // Output: OCES
    /**
     * 极小的常量，用于避免除零错误
     */
    constexpr float TINY = 1e-5f;
    /**
     * RGB 的最小值和最大值
     */
    float mi = min(rgb);
    float ma = max(rgb);
    /**
     * 饱和度 = (最大值 - 最小值) / 最大值
     * 使用 TINY 确保分母不为零
     */
    return (max(ma, TINY) - max(mi, TINY)) / max(ma, 1e-2f);
}

/**
 * 将 RGB 转换为亮度代理 YC
 * 
 * YC 是亮度和色度的组合：YC ≈ Y + K * Chroma
 * 在 RGB 空间中，恒定 YC 形成一个圆锥形表面，顶点在白色附近的中性轴上。
 * YC 已归一化：RGB(1, 1, 1) 映射到 YC = 1
 * 
 * @param rgb RGB 颜色值
 * @return YC 亮度代理值
 * 
 * 参数说明：
 * - ycRadiusWeight = 1.75（默认值）
 *   - ycRadiusWeight = 1：纯青色、洋红色、黄色的 YC == 相同值的中性色 YC
 *   - ycRadiusWeight = 2：纯红色、绿色、蓝色的 YC == 相同值的中性色 YC
 */
inline float rgb_2_yc(float3 const rgb) {
    /**
     * YC 半径权重，控制色度对亮度的影响
     */
    constexpr float ycRadiusWeight = 1.75f;

    // Converts RGB to a luminance proxy, here called YC
    // YC is ~ Y + K * Chroma
    // Constant YC is a cone-shaped surface in RGB space, with the tip on the
    // neutral axis, towards white.
    // YC is normalized: RGB 1 1 1 maps to YC = 1
    //
    // ycRadiusWeight defaults to 1.75, although can be overridden in function
    // call to rgb_2_yc
    // ycRadiusWeight = 1 -> YC for pure cyan, magenta, yellow == YC for neutral
    // of same value
    // ycRadiusWeight = 2 -> YC for pure red, green, blue  == YC for  neutral of
    // same value.

    /**
     * RGB 分量
     */
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;

    /**
     * 计算色度（chroma），使用欧几里得距离公式的变体
     * 公式：sqrt(b*(b-g) + g*(g-r) + r*(r-b))
     */
    float chroma = std::sqrt(b * (b - g) + g * (g - r) + r * (r - b));

    /**
     * 返回 YC = (R + G + B + ycRadiusWeight * Chroma) / 3
     */
    return (b + g + r + ycRadiusWeight * chroma) / 3.0f;
}

/**
 * Sigmoid 整形函数
 * 
 * 将输入值映射到 0 到 1 的范围，跨越 -2 到 +2 的输入范围。
 * 这是一个平滑的 S 形曲线函数。
 * 
 * @param x 输入值
 * @return 映射后的值（0 到 1 之间）
 */
inline float sigmoid_shaper(float const x) {
    // Sigmoid function in the range 0 to 1 spanning -2 to +2.
    /**
     * 计算中间值 t，基于 x/2 的绝对值
     * 当 |x| <= 2 时，t 在 0 到 1 之间
     */
    float t = max(1.0f - std::abs(x / 2.0f), 0.0f);
    /**
     * 计算 y，使用符号函数和 t^2
     */
    float y = 1.0f + sign(x) * (1.0f - t * t);
    /**
     * 归一化到 0-1 范围
     */
    return y / 2.0f;
}

/**
 * 前向辉光函数
 * 
 * 根据输入亮度 YC 计算辉光增益。
 * 在低亮度区域应用全增益，在高亮度区域逐渐减少增益。
 * 
 * @param ycIn 输入 YC 亮度值
 * @param glowGainIn 输入辉光增益
 * @param glowMid 辉光中间点（阈值）
 * @return 输出辉光增益
 * 
 * 分段函数：
 * - ycIn <= 2/3 * glowMid：全增益
 * - ycIn >= 2 * glowMid：无增益
 * - 中间：线性插值
 */
inline float glow_fwd(float const ycIn, float const glowGainIn, float const glowMid) {
    /**
     * 输出辉光增益
     */
    float glowGainOut;

    if (ycIn <= 2.0f / 3.0f * glowMid) {
        /**
         * 低亮度区域：应用全增益
         */
        glowGainOut = glowGainIn;
    } else if ( ycIn >= 2.0f * glowMid) {
        /**
         * 高亮度区域：无增益
         */
        glowGainOut = 0.0f;
    } else {
        /**
         * 中间区域：线性插值
         * 公式：glowGainIn * (glowMid / ycIn - 0.5)
         */
        glowGainOut = glowGainIn * (glowMid / ycIn - 1.0f / 2.0f);
    }

    return glowGainOut;
}

/**
 * 从 RGB 计算色调角度
 * 
 * 返回基于 RGB 值的几何色调角度（0-360 度）。
 * 对于中性色（RGB 相等），色调未定义，函数返回 NaN。
 * 
 * @param rgb RGB 颜色值
 * @return 色调角度（0-360 度），中性色返回 NaN
 * 
 * 算法：
 * - 使用 atan2 计算色调角度
 * - 将弧度转换为度数
 * - 如果角度为负，加 360 度使其在 0-360 范围内
 */
inline float rgb_2_hue(float3 const rgb) {
    // Returns a geometric hue angle in degrees (0-360) based on RGB values.
    // For neutral colors, hue is undefined and the function will return a quiet NaN value.
    /**
     * 初始色调值
     */
    float hue = 0.0f;
    // RGB triplets where RGB are equal have an undefined hue
    /**
     * 检查是否为中性色（RGB 分量相等）
     * 如果不是中性色，计算色调角度
     */
    if (!(rgb.x == rgb.y && rgb.y == rgb.z)) {
        /**
         * 使用 atan2 计算色调角度
         * y 分量：sqrt(3) * (G - B)
         * x 分量：2*R - G - B
         * 然后转换为度数
         */
        hue = f::RAD_TO_DEG * std::atan2(
                std::sqrt(3.0f) * (rgb.y - rgb.z),
                2.0f * rgb.x - rgb.y - rgb.z);
    }
    /**
     * 如果角度为负，加 360 度使其在 0-360 范围内
     */
    return (hue < 0.0f) ? hue + 360.0f : hue;
}

/**
 * 将色调居中
 * 
 * 将色调值相对于中心色调进行居中，确保结果在 -180 到 +180 度范围内。
 * 
 * @param hue 输入色调角度（0-360 度）
 * @param centerH 中心色调角度
 * @return 居中后的色调角度（-180 到 +180 度）
 */
inline float center_hue(float const hue, float const centerH) {
    /**
     * 计算相对于中心色调的偏移
     */
    float hueCentered = hue - centerH;
    /**
     * 如果偏移小于 -180 度，加 360 度
     */
    if (hueCentered < -180.0f) {
        hueCentered = hueCentered + 360.0f;
    /**
     * 如果偏移大于 +180 度，减 360 度
     */
    } else if (hueCentered > 180.0f) {
        hueCentered = hueCentered - 360.0f;
    }
    return hueCentered;
}

/**
 * 从暗环境转换到暗环境
 * 
 * 应用伽马调整以补偿环境亮度差异。
 * 将颜色从暗环境（dark surround）调整到暗环境（dim surround）。
 * 
 * @param linearCV 线性颜色值（AP1 颜色空间）
 * @return 调整后的颜色值（AP1 颜色空间）
 * 
 * 转换流程：
 * 1. AP1 -> XYZ
 * 2. XYZ -> xyY
 * 3. 对 Y（亮度）应用伽马校正
 * 4. xyY -> XYZ
 * 5. XYZ -> AP1
 */
inline float3 darkSurround_to_dimSurround(float3 linearCV) {
    /**
     * 暗环境伽马值，用于亮度调整
     */
    constexpr float DIM_SURROUND_GAMMA = 0.9811f;

    /**
     * 从 AP1 转换到 XYZ 颜色空间
     */
    float3 XYZ = AP1_to_XYZ * linearCV;
    /**
     * 从 XYZ 转换到 xyY 颜色空间（xy 是色度，Y 是亮度）
     */
    float3 xyY = XYZ_to_xyY(XYZ);

    /**
     * 限制亮度值在有效范围内，避免溢出
     */
    xyY.z = clamp(xyY.z, 0.0f, (float) std::numeric_limits<half>::max());
    /**
     * 对亮度应用伽马校正
     */
    xyY.z = std::pow(xyY.z, DIM_SURROUND_GAMMA);

    /**
     * 从 xyY 转换回 XYZ
     */
    XYZ = xyY_to_XYZ(xyY);
    /**
     * 从 XYZ 转换回 AP1
     */
    return XYZ_to_AP1 * XYZ;
}

/**
 * ACES 色调映射函数
 * 
 * 实现 ACES（Academy Color Encoding System）色调映射算法。
 * 将高动态范围颜色映射到显示范围。
 * 
 * @param color 输入颜色（Rec.2020 颜色空间）
 * @param brightness 亮度调整因子（Filament 特定参数）
 * @return 色调映射后的颜色（Rec.2020 颜色空间）
 * 
 * 处理流程：
 * 1. 转换到 AP0 颜色空间
 * 2. 应用辉光模块（Glow）
 * 3. 应用红色修正器（Red Modifier）
 * 4. 转换到 AP1 颜色空间
 * 5. 全局去饱和度
 * 6. 应用亮度调整
 * 7. RRT + ODT 拟合
 * 8. 环境亮度补偿
 * 9. 最终去饱和度
 * 10. 转换回 Rec.2020
 */
float3 ACES(float3 color, float brightness) noexcept {
    // Some bits were removed to adapt to our desired output

    // "Glow" module constants
    /**
     * 辉光增益常量
     */
    constexpr float RRT_GLOW_GAIN = 0.05f;
    /**
     * 辉光中间点常量
     */
    constexpr float RRT_GLOW_MID = 0.08f;

    // Red modifier constants
    /**
     * 红色修正缩放因子
     */
    constexpr float RRT_RED_SCALE = 0.82f;
    /**
     * 红色修正枢轴点
     */
    constexpr float RRT_RED_PIVOT = 0.03f;
    /**
     * 红色修正中心色调
     */
    constexpr float RRT_RED_HUE   = 0.0f;
    /**
     * 红色修正色调宽度
     */
    constexpr float RRT_RED_WIDTH = 135.0f;

    // Desaturation constants
    /**
     * RRT 去饱和度因子
     */
    constexpr float RRT_SAT_FACTOR = 0.96f;
    /**
     * ODT 去饱和度因子
     */
    constexpr float ODT_SAT_FACTOR = 0.93f;

    /**
     * 从 Rec.2020 转换到 AP0 颜色空间
     */
    float3 ap0 = Rec2020_to_AP0 * color;

    // Glow module
    /**
     * 计算饱和度
     */
    float saturation = rgb_2_saturation(ap0);
    /**
     * 计算 YC 亮度代理
     */
    float ycIn = rgb_2_yc(ap0);
    /**
     * 使用 sigmoid 整形函数处理饱和度
     */
    float s = sigmoid_shaper((saturation - 0.4f) / 0.2f);
    /**
     * 计算并应用辉光增益
     */
    float addedGlow = 1.0f + glow_fwd(ycIn, RRT_GLOW_GAIN * s, RRT_GLOW_MID);
    ap0 *= addedGlow;

    // Red modifier
    /**
     * 计算色调角度
     */
    float hue = rgb_2_hue(ap0);
    /**
     * 将色调居中
     */
    float centeredHue = center_hue(hue, RRT_RED_HUE);
    /**
     * 计算色调权重（使用 smoothstep 创建平滑过渡）
     */
    float hueWeight = smoothstep(0.0f, 1.0f, 1.0f - std::abs(2.0f * centeredHue / RRT_RED_WIDTH));
    /**
     * 平方权重以增强效果
     */
    hueWeight *= hueWeight;

    /**
     * 应用红色修正：调整红色分量
     */
    ap0.r += hueWeight * saturation * (RRT_RED_PIVOT - ap0.r) * (1.0f - RRT_RED_SCALE);

    // ACES to RGB rendering space
    /**
     * 从 AP0 转换到 AP1 颜色空间（RGB 渲染空间）
     * 限制值在有效范围内
     */
    float3 ap1 = clamp(AP0_to_AP1 * ap0, 0.0f, (float) std::numeric_limits<half>::max());

    // Global desaturation
    /**
     * 全局去饱和度：混合亮度值和原始颜色
     */
    ap1 = mix(float3(dot(ap1, LUMINANCE_AP1)), ap1, RRT_SAT_FACTOR);

    // NOTE: This is specific to Filament and added only to match ACES to our legacy tone mapper
    //       which was a fit of ACES in Rec.709 but with a brightness boost.
    /**
     * Filament 特定：应用亮度调整
     * 这是为了匹配 Filament 的旧版色调映射器
     */
    ap1 *= brightness;

    // Fitting of RRT + ODT (RGB monitor 100 nits dim) from:
    // https://github.com/colour-science/colour-unity/blob/master/Assets/Colour/Notebooks/CIECAM02_Unity.ipynb
    /**
     * RRT + ODT 拟合参数（RGB 显示器 100 nits 暗环境）
     * 来源：colour-science/colour-unity
     */
    constexpr float a = 2.785085f;
    constexpr float b = 0.107772f;
    constexpr float c = 2.936045f;
    constexpr float d = 0.887122f;
    constexpr float e = 0.806889f;
    /**
     * 应用 RRT + ODT 拟合曲线
     * 公式：(ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e)
     */
    float3 rgbPost = (ap1 * (a * ap1 + b)) / (ap1 * (c * ap1 + d) + e);

    // Apply gamma adjustment to compensate for dim surround
    /**
     * 应用伽马调整以补偿暗环境
     */
    float3 linearCV = darkSurround_to_dimSurround(rgbPost);

    // Apply desaturation to compensate for luminance difference
    /**
     * 应用去饱和度以补偿亮度差异
     */
    linearCV = mix(float3(dot(linearCV, LUMINANCE_AP1)), linearCV, ODT_SAT_FACTOR);

    /**
     * 从 AP1 转换回 Rec.2020 颜色空间
     */
    return AP1_to_Rec2020 * linearCV;
}

} // namespace aces

//------------------------------------------------------------------------------
// Tone mappers
//------------------------------------------------------------------------------

#define DEFAULT_CONSTRUCTORS(A) \
        A::A() noexcept = default; \
        A::~A() noexcept = default;

DEFAULT_CONSTRUCTORS(ToneMapper)

//------------------------------------------------------------------------------
// Linear tone mapper
//------------------------------------------------------------------------------

DEFAULT_CONSTRUCTORS(LinearToneMapper)

/**
 * 线性色调映射操作符
 * 
 * 简单的线性色调映射，仅将颜色值限制在 0-1 范围内。
 * 
 * @param v 输入颜色值
 * @return 限制后的颜色值（0-1 范围）
 */
float3 LinearToneMapper::operator()(float3 const v) const noexcept {
    /**
     * 使用 saturate 将值限制在 0-1 范围内
     */
    return saturate(v);
}

//------------------------------------------------------------------------------
// ACES tone mappers
//------------------------------------------------------------------------------

DEFAULT_CONSTRUCTORS(ACESToneMapper)

/**
 * ACES 色调映射操作符
 * 
 * 标准 ACES 色调映射，亮度因子为 1.0。
 * 
 * @param c 输入颜色值（Rec.2020 颜色空间）
 * @return 色调映射后的颜色值（Rec.2020 颜色空间）
 */
float3 ACESToneMapper::operator()(float3 const c) const noexcept {
    /**
     * 调用 ACES 函数，亮度因子为 1.0
     */
    return aces::ACES(c, 1.0f);
}

DEFAULT_CONSTRUCTORS(ACESLegacyToneMapper)

/**
 * ACES Legacy 色调映射操作符
 * 
 * ACES 色调映射的旧版本，使用更高的亮度因子（1.0/0.6 ≈ 1.667）
 * 以匹配 Filament 的旧版色调映射器。
 * 
 * @param c 输入颜色值（Rec.2020 颜色空间）
 * @return 色调映射后的颜色值（Rec.2020 颜色空间）
 */
float3 ACESLegacyToneMapper::operator()(float3 const c) const noexcept {
    /**
     * 调用 ACES 函数，亮度因子为 1.0/0.6（约 1.667）
     */
    return aces::ACES(c, 1.0f / 0.6f);
}

DEFAULT_CONSTRUCTORS(FilmicToneMapper)

/**
 * 电影色调映射操作符
 * 
 * 实现 Narkowicz 2015 的 "ACES Filmic Tone Mapping Curve"。
 * 这是一个简化的电影色调映射曲线，提供平滑的色调过渡。
 * 
 * @param x 输入颜色值
 * @return 色调映射后的颜色值
 * 
 * 公式：(x * (a * x + b)) / (x * (c * x + d) + e)
 * 其中 a=2.51, b=0.03, c=2.43, d=0.59, e=0.14
 */
float3 FilmicToneMapper::operator()(float3 const x) const noexcept {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    /**
     * 电影色调映射曲线参数
     */
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    /**
     * 应用电影色调映射曲线公式
     */
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

//------------------------------------------------------------------------------
// PBR Neutral tone mapper
//------------------------------------------------------------------------------

DEFAULT_CONSTRUCTORS(PBRNeutralToneMapper)

/**
 * PBR Neutral 色调映射操作符
 * 
 * 实现 PBR（Physically Based Rendering）中性色调映射。
 * 来源：https://modelviewer.dev/examples/tone-mapping.html
 * 
 * 该算法提供中性的色调映射，保持颜色的自然外观。
 * 
 * @param color 输入颜色值
 * @return 色调映射后的颜色值
 * 
 * 处理流程：
 * 1. 计算最小分量并应用偏移
 * 2. 如果峰值低于压缩起点，直接返回
 * 3. 计算新的峰值并缩放颜色
 * 4. 应用去饱和度
 */
float3 PBRNeutralToneMapper::operator()(float3 color) const noexcept {
    // PBR Tone Mapping, https://modelviewer.dev/examples/tone-mapping.html
    /**
     * 压缩起始点（开始压缩高亮区域的值）
     */
    constexpr float startCompression = 0.8f - 0.04f;
    /**
     * 去饱和度因子
     */
    constexpr float desaturation = 0.15f;

    /**
     * 计算 RGB 的最小分量
     */
    float x = min(color.r, min(color.g, color.b));
    /**
     * 计算偏移量：对于低值使用二次函数，否则使用固定值
     */
    float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    /**
     * 从颜色中减去偏移
     */
    color -= offset;

    /**
     * 计算 RGB 的最大分量（峰值）
     */
    float peak = max(color.r, max(color.g, color.b));
    /**
     * 如果峰值低于压缩起点，直接返回（无需压缩）
     */
    if (peak < startCompression) return color;

    /**
     * 计算压缩范围
     */
    float d = 1.0f - startCompression;
    /**
     * 计算新的峰值（压缩后的峰值）
     * 公式：1 - d^2 / (peak + d - startCompression)
     */
    float newPeak = 1.0f - d * d / (peak + d - startCompression);
    /**
     * 按新峰值与旧峰值的比率缩放颜色
     */
    color *= newPeak / peak;

    /**
     * 计算去饱和度因子
     * 公式：1 - 1 / (desaturation * (peak - newPeak) + 1)
     */
    float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
    /**
     * 混合原始颜色和峰值颜色以应用去饱和度
     */
    return mix(color, float3(newPeak), g);
}

//------------------------------------------------------------------------------
// AgX tone mapper
//------------------------------------------------------------------------------

AgxToneMapper::AgxToneMapper(AgxLook const look) noexcept : look(look) {}
AgxToneMapper::~AgxToneMapper() noexcept = default;

// These matrices taken from Blender's implementation of AgX, which works with Rec.2020 primaries.
// https://github.com/EaryChow/AgX_LUT_Gen/blob/main/AgXBaseRec2020.py
constexpr mat3f AgXInsetMatrix {
    0.856627153315983, 0.137318972929847, 0.11189821299995,
    0.0951212405381588, 0.761241990602591, 0.0767994186031903,
    0.0482516061458583, 0.101439036467562, 0.811302368396859
};
constexpr mat3f AgXOutsetMatrixInv {
    0.899796955911611, 0.11142098895748, 0.11142098895748,
    0.0871996192028351, 0.875575586156966, 0.0871996192028349,
    0.013003424885555, 0.0130034248855548, 0.801379391839686
};
constexpr mat3f AgXOutsetMatrix { inverse(AgXOutsetMatrixInv) };

// LOG2_MIN      = -10.0
// LOG2_MAX      =  +6.5
// MIDDLE_GRAY   =  0.18
const float AgxMinEv = -12.47393f;      // log2(pow(2, LOG2_MIN) * MIDDLE_GRAY)
const float AgxMaxEv = 4.026069f;       // log2(pow(2, LOG2_MAX) * MIDDLE_GRAY)

/**
 * AgX 默认对比度近似函数
 * 
 * 实现 AgX 色调映射的对比度曲线。
 * 来源：https://iolite-engine.com/blog_posts/minimal_agx_implementation
 * 
 * 使用 7 次多项式近似 sigmoid 函数，提供平滑的对比度调整。
 * 
 * @param x 输入值（0-1 范围）
 * @return 对比度调整后的值
 * 
 * 多项式系数：
 * - x^7: -17.86
 * - x^6: +78.01
 * - x^5: -126.7
 * - x^4: +92.06
 * - x^3: -28.72
 * - x^2: +4.361
 * - x^1: -0.1718
 * - x^0: +0.002857
 */
// Adapted from https://iolite-engine.com/blog_posts/minimal_agx_implementation
float3 agxDefaultContrastApprox(float3 x) {
    /**
     * 计算 x 的幂次
     */
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    float3 x6 = x4 * x2;
    /**
     * 应用 7 次多项式
     */
    return  - 17.86f    * x6 * x
            + 78.01f    * x6
            - 126.7f    * x4 * x
            + 92.06f    * x4
            - 28.72f    * x2 * x
            + 4.361f    * x2
            - 0.1718f   * x
            + 0.002857f;
}

/**
 * AgX Look 函数
 * 
 * 应用 AgX 色调映射的 Look 调整。
 * 来源：https://iolite-engine.com/blog_posts/minimal_agx_implementation
 * 
 * 使用 ASC CDL（American Society of Cinematographers Color Decision List）参数
 * 进行颜色调整：slope（斜率）、offset（偏移）、power（幂次）。
 * 
 * @param val 输入颜色值
 * @param look AgX Look 类型（NONE、GOLDEN、PUNCHY）
 * @return 调整后的颜色值
 * 
 * Look 类型：
 * - NONE：无调整
 * - GOLDEN：金色调，降低绿色和蓝色斜率，降低幂次，增加饱和度
 * - PUNCHY：增强对比度，增加幂次，增加饱和度
 */
// Adapted from https://iolite-engine.com/blog_posts/minimal_agx_implementation
float3 agxLook(float3 val, AgxToneMapper::AgxLook look) {
    /**
     * 如果无 Look 调整，直接返回
     */
    if (look == AgxToneMapper::AgxLook::NONE) {
        return val;
    }

    /**
     * Rec.709 亮度权重（用于计算亮度）
     */
    const float3 lw = float3(0.2126f, 0.7152f, 0.0722f);
    /**
     * 计算亮度
     */
    float luma = dot(val, lw);

    // Default
    /**
     * ASC CDL 参数默认值
     */
    float3 offset = float3(0.0f);  // 偏移量
    float3 slope = float3(1.0f);    // 斜率
    float3 power = float3(1.0f);    // 幂次
    float sat = 1.0f;               // 饱和度

    /**
     * GOLDEN Look：金色调
     * - 降低绿色和蓝色斜率（0.9 和 0.5）
     * - 降低幂次（0.8）
     * - 增加饱和度（1.3）
     */
    if (look == AgxToneMapper::AgxLook::GOLDEN) {
        slope = float3(1.0f, 0.9f, 0.5f);
        power = float3(0.8f);
        sat = 1.3;
    }
    /**
     * PUNCHY Look：增强对比度
     * - 保持斜率不变（1.0）
     * - 增加幂次（1.35）
     * - 增加饱和度（1.4）
     */
    if (look == AgxToneMapper::AgxLook::PUNCHY) {
        slope = float3(1.0f);
        power = float3(1.35f, 1.35f, 1.35f);
        sat = 1.4;
    }

    // ASC CDL
    /**
     * 应用 ASC CDL：val = pow(val * slope + offset, power)
     */
    val = pow(val * slope + offset, power);
    /**
     * 应用饱和度调整：混合亮度和调整后的颜色
     */
    return luma + sat * (val - luma);
}

/**
 * AgX 色调映射操作符
 * 
 * 实现 AgX（Agogic Exchange）色调映射算法。
 * AgX 是一个现代的色调映射系统，提供更好的颜色保真度和对比度控制。
 * 
 * @param v 输入颜色值
 * @return 色调映射后的颜色值
 * 
 * 处理流程：
 * 1. 确保无负值
 * 2. 应用 Inset 矩阵（转换到 AgX 内部颜色空间）
 * 3. Log2 编码
 * 4. 归一化到 0-1 范围
 * 5. 应用对比度曲线（sigmoid）
 * 6. 应用 AgX Look
 * 7. 应用 Outset 矩阵（转换回显示颜色空间）
 * 8. 线性化（应用 2.2 伽马）
 */
float3 AgxToneMapper::operator()(float3 v) const noexcept {
    // Ensure no negative values
    /**
     * 确保无负值，将负值限制为 0
     */
    v = max(float3(0.0f), v);

    /**
     * 应用 AgX Inset 矩阵，转换到 AgX 内部颜色空间
     */
    v = AgXInsetMatrix * v;

    // Log2 encoding
    /**
     * 确保值大于 1E-10，避免 log2 计算错误
     */
    v = max(v, 1E-10f); // avoid 0 or negative numbers for log2
    /**
     * Log2 编码
     */
    v = log2(v);
    /**
     * 归一化到 0-1 范围
     * 公式：(v - AgxMinEv) / (AgxMaxEv - AgxMinEv)
     */
    v = (v - AgxMinEv) / (AgxMaxEv - AgxMinEv);

    /**
     * 限制在 0-1 范围内
     */
    v = clamp(v, 0.0f, 1.0f);

    // Apply sigmoid
    /**
     * 应用对比度曲线（sigmoid 近似）
     */
    v = agxDefaultContrastApprox(v);

    // Apply AgX look
    /**
     * 应用 AgX Look 调整
     */
    v = agxLook(v, look);

    /**
     * 应用 AgX Outset 矩阵，转换回显示颜色空间
     */
    v = AgXOutsetMatrix * v;

    // Linearize
    /**
     * 线性化：应用 2.2 伽马校正
     */
    v = pow(max(float3(0.0f), v), 2.2f);

    return v;
}

//------------------------------------------------------------------------------
// Display range tone mapper
//------------------------------------------------------------------------------

DEFAULT_CONSTRUCTORS(DisplayRangeToneMapper)

/**
 * 显示范围色调映射操作符
 * 
 * 将颜色映射到 16 种调试颜色的调色板。
 * 用于可视化不同亮度级别的颜色分布。
 * 
 * @param c 输入颜色值
 * @return 映射后的调试颜色
 * 
 * 算法：
 * 1. 计算亮度（使用 Rec.2020 亮度权重）
 * 2. 转换为对数空间（以 18% 灰度为基准）
 * 3. 映射到 0-15 范围（对应 16 种颜色）
 * 4. 在相邻颜色之间插值
 */
float3 DisplayRangeToneMapper::operator()(float3 const c) const noexcept {
    // 16 debug colors + 1 duplicated at the end for easy indexing
    /**
     * 16 种调试颜色 + 1 个重复的白色（用于索引）
     * 颜色从黑色到白色，中间包含各种颜色
     */
    constexpr float3 debugColors[17] = {
            {0.0,     0.0,     0.0},         // black
            {0.0,     0.0,     0.1647},      // darkest blue
            {0.0,     0.0,     0.3647},      // darker blue
            {0.0,     0.0,     0.6647},      // dark blue
            {0.0,     0.0,     0.9647},      // blue
            {0.0,     0.9255,  0.9255},      // cyan
            {0.0,     0.5647,  0.0},         // dark green
            {0.0,     0.7843,  0.0},         // green
            {1.0,     1.0,     0.0},         // yellow
            {0.90588, 0.75294, 0.0},         // yellow-orange
            {1.0,     0.5647,  0.0},         // orange
            {1.0,     0.0,     0.0},         // bright red
            {0.8392,  0.0,     0.0},         // red
            {1.0,     0.0,     1.0},         // magenta
            {0.6,     0.3333,  0.7882},      // purple
            {1.0,     1.0,     1.0},         // white
            {1.0,     1.0,     1.0}          // white
    };

    // The 5th color in the array (cyan) represents middle gray (18%)
    // Every stop above or below middle gray causes a color shift
    // TODO: This should depend on the working color grading color space
    /**
     * 计算亮度（使用 Rec.2020 亮度权重）
     * 然后转换为对数空间，以 18% 灰度为基准
     */
    float v = log2(dot(c, LUMINANCE_Rec2020) / 0.18f);
    /**
     * 加 5.0 偏移并限制在 0-15 范围内
     * 这确保 18% 灰度（v=0）映射到索引 5（青色）
     */
    v = clamp(v + 5.0f, 0.0f, 15.0f);

    /**
     * 计算颜色索引（整数部分）
     */
    size_t index = size_t(v);
    /**
     * 在相邻颜色之间插值
     * 使用小数部分作为插值因子
     */
    return mix(debugColors[index], debugColors[index + 1], saturate(v - float(index)));
}

//------------------------------------------------------------------------------
// Generic tone mapper
//------------------------------------------------------------------------------

/**
 * GenericToneMapper 选项结构
 * 
 * 存储通用色调映射器的参数和预计算的缩放因子。
 */
struct GenericToneMapper::Options {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow"
#endif
    /**
     * 设置参数并预计算缩放因子
     * 
     * @param contrast 对比度（> 0）
     * @param midGrayIn 输入中灰值（0-1）
     * @param midGrayOut 输出中灰值（0-1）
     * @param hdrMax HDR 最大值（>= 1.0）
     * 
     * 预计算：
     * - a = pow(midGrayIn, contrast)
     * - b = pow(hdrMax, contrast)
     * - c = a - midGrayOut * b
     * - inputScale = (a * b * (midGrayOut - 1.0)) / c
     * - outputScale = midGrayOut * (a - b) / c
     */
    void setParameters(
            float contrast,
            float midGrayIn,
            float midGrayOut,
            float hdrMax
    ) {
        /**
         * 限制参数在有效范围内
         */
        contrast = max(contrast, 1e-5f);
        midGrayIn = clamp(midGrayIn, 1e-5f, 1.0f);
        midGrayOut = clamp(midGrayOut, 1e-5f, 1.0f);
        hdrMax = max(hdrMax, 1.0f);

        /**
         * 存储参数
         */
        this->contrast = contrast;
        this->midGrayIn = midGrayIn;
        this->midGrayOut = midGrayOut;
        this->hdrMax = hdrMax;

        /**
         * 预计算中间值
         */
        float a = pow(midGrayIn, contrast);
        float b = pow(hdrMax, contrast);
        float c = a - midGrayOut * b;

        /**
         * 预计算输入和输出缩放因子
         * 这些因子用于优化色调映射计算
         */
        inputScale = (a * b * (midGrayOut - 1.0f)) / c;
        outputScale = midGrayOut * (a - b) / c;
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    /**
     * 对比度参数
     */
    float contrast;
    /**
     * 输入中灰值
     */
    float midGrayIn;
    /**
     * 输出中灰值
     */
    float midGrayOut;
    /**
     * HDR 最大值
     */
    float hdrMax;

    // TEMP
    /**
     * 输入缩放因子（预计算）
     */
    float inputScale;
    /**
     * 输出缩放因子（预计算）
     */
    float outputScale;
};

GenericToneMapper::GenericToneMapper(
        float const contrast,
        float const midGrayIn,
        float const midGrayOut,
        float const hdrMax
) noexcept {
    mOptions = new Options();
    mOptions->setParameters(contrast, midGrayIn, midGrayOut, hdrMax);
}

GenericToneMapper::~GenericToneMapper() noexcept {
    delete mOptions;
}

GenericToneMapper::GenericToneMapper(GenericToneMapper&& rhs)  noexcept : mOptions(rhs.mOptions) {
    rhs.mOptions = nullptr;
}

GenericToneMapper& GenericToneMapper::operator=(GenericToneMapper&& rhs) noexcept {
    mOptions = rhs.mOptions;
    rhs.mOptions = nullptr;
    return *this;
}

/**
 * 通用色调映射操作符
 * 
 * 实现通用的色调映射曲线。
 * 公式：outputScale * pow(x, contrast) / (pow(x, contrast) + inputScale)
 * 
 * @param x 输入颜色值
 * @return 色调映射后的颜色值
 * 
 * 算法：
 * 1. 应用对比度：x = pow(x, contrast)
 * 2. 应用色调映射曲线：outputScale * x / (x + inputScale)
 */
float3 GenericToneMapper::operator()(float3 x) const noexcept {
    /**
     * 应用对比度调整
     */
    x = pow(x, mOptions->contrast);
    /**
     * 应用色调映射曲线
     * 公式：outputScale * x / (x + inputScale)
     */
    return mOptions->outputScale * x / (x + mOptions->inputScale);
}

float GenericToneMapper::getContrast() const noexcept { return  mOptions->contrast; }
float GenericToneMapper::getMidGrayIn() const noexcept { return  mOptions->midGrayIn; }
float GenericToneMapper::getMidGrayOut() const noexcept { return  mOptions->midGrayOut; }
float GenericToneMapper::getHdrMax() const noexcept { return  mOptions->hdrMax; }

void GenericToneMapper::setContrast(float const contrast) noexcept {
    mOptions->setParameters(
            contrast,
            mOptions->midGrayIn,
            mOptions->midGrayOut,
            mOptions->hdrMax
    );
}
void GenericToneMapper::setMidGrayIn(float const midGrayIn) noexcept {
    mOptions->setParameters(
            mOptions->contrast,
            midGrayIn,
            mOptions->midGrayOut,
            mOptions->hdrMax
    );
}

void GenericToneMapper::setMidGrayOut(float const midGrayOut) noexcept {
    mOptions->setParameters(
            mOptions->contrast,
            mOptions->midGrayIn,
            midGrayOut,
            mOptions->hdrMax
    );
}

void GenericToneMapper::setHdrMax(float const hdrMax) noexcept {
    mOptions->setParameters(
            mOptions->contrast,
            mOptions->midGrayIn,
            mOptions->midGrayOut,
            hdrMax
    );
}

#undef DEFAULT_CONSTRUCTORS

} // namespace filament
