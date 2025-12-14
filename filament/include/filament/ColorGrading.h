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

//! \file

#ifndef TNT_FILAMENT_COLORGRADING_H
#define TNT_FILAMENT_COLORGRADING_H

#include <filament/FilamentAPI.h>
#include <filament/ToneMapper.h>

#include <utils/compiler.h>

#include <math/mathfwd.h>

#include <stdint.h>
#include <stddef.h>

namespace filament {

class Engine;
class FColorGrading;

namespace color {
class ColorSpace;
}

/**
 * ColorGrading is used to transform (either to modify or correct) the colors of the HDR buffer
 * rendered by Filament. Color grading transforms are applied after lighting, and after any lens
 * effects (bloom for instance), and include tone mapping.
 *
 * Creation, usage and destruction
 * ===============================
 *
 * A ColorGrading object is created using the ColorGrading::Builder and destroyed by calling
 * Engine::destroy(const ColorGrading*). A ColorGrading object is meant to be set on a View.
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *
 *  filament::ColorGrading* colorGrading = filament::ColorGrading::Builder()
 *              .toneMapping(filament::ColorGrading::ToneMapping::ACES)
 *              .build(*engine);
 *
 *  myView->setColorGrading(colorGrading);
 *
 *  engine->destroy(colorGrading);
 * ~~~~~~~~~~~
 *
 * Performance
 * ===========
 *
 * Creating a new ColorGrading object may be more expensive than other Filament objects as a LUT may
 * need to be generated. The generation of this LUT, if necessary, may happen on the CPU.
 *
 * Ordering
 * ========
 *
 * The various transforms held by ColorGrading are applied in the following order:
 * - Exposure
 * - Night adaptation
 * - White balance
 * - Channel mixer
 * - Shadows/mid-tones/highlights
 * - Slope/offset/power (CDL)
 * - Contrast
 * - Vibrance
 * - Saturation
 * - Curves
 * - Tone mapping
 * - Luminance scaling
 * - Gamut mapping
 *
 * Defaults
 * ========
 *
 * Here are the default color grading options:
 * - Exposure: 0.0
 * - Night adaptation: 0.0
 * - White balance: temperature 0, and tint 0
 * - Channel mixer: red {1,0,0}, green {0,1,0}, blue {0,0,1}
 * - Shadows/mid-tones/highlights: shadows {1,1,1,0}, mid-tones {1,1,1,0}, highlights {1,1,1,0},
 *   ranges {0,0.333,0.550,1}
 * - Slope/offset/power: slope 1.0, offset 0.0, and power 1.0
 * - Contrast: 1.0
 * - Vibrance: 1.0
 * - Saturation: 1.0
 * - Curves: gamma {1,1,1}, midPoint {1,1,1}, and scale {1,1,1}
 * - Tone mapping: ACESLegacyToneMapper
 * - Luminance scaling: false
 * - Gamut mapping: false
 * - Output color space: Rec709-sRGB-D65
 *
 * @see View
 */
/**
 * ColorGrading 用于转换（修改或校正）Filament 渲染的 HDR 缓冲区的颜色。
 * 颜色分级变换在光照之后以及任何镜头效果（例如泛光）之后应用，并包括色调映射。
 *
 * 创建、使用和销毁
 * ===============================
 *
 * ColorGrading 对象使用 ColorGrading::Builder 创建，通过调用
 * Engine::destroy(const ColorGrading*) 销毁。ColorGrading 对象应在 View 上设置。
 *
 * 性能
 * ===========
 *
 * 创建新的 ColorGrading 对象可能比其他 Filament 对象更昂贵，因为可能需要生成 LUT。
 * 如果需要，此 LUT 的生成可能在 CPU 上进行。
 *
 * 顺序
 * ========
 *
 * ColorGrading 包含的各种变换按以下顺序应用：
 * - 曝光
 * - 夜间适应
 * - 白平衡
 * - 通道混合器
 * - 阴影/中间调/高光
 * - 斜率/偏移/幂 (CDL)
 * - 对比度
 * - 自然饱和度
 * - 饱和度
 * - 曲线
 * - 色调映射
 * - 亮度缩放
 * - 色域映射
 *
 * 默认值
 * ========
 *
 * 以下是默认的颜色分级选项：
 * - 曝光: 0.0
 * - 夜间适应: 0.0
 * - 白平衡: 色温 0，色调 0
 * - 通道混合器: 红色 {1,0,0}，绿色 {0,1,0}，蓝色 {0,0,1}
 * - 阴影/中间调/高光: 阴影 {1,1,1,0}，中间调 {1,1,1,0}，高光 {1,1,1,0}，
 *   范围 {0,0.333,0.550,1}
 * - 斜率/偏移/幂: 斜率 1.0，偏移 0.0，幂 1.0
 * - 对比度: 1.0
 * - 自然饱和度: 1.0
 * - 饱和度: 1.0
 * - 曲线: gamma {1,1,1}，midPoint {1,1,1}，scale {1,1,1}
 * - 色调映射: ACESLegacyToneMapper
 * - 亮度缩放: false
 * - 色域映射: false
 * - 输出色彩空间: Rec709-sRGB-D65
 *
 * @see View
 */
class UTILS_PUBLIC ColorGrading : public FilamentAPI {
    struct BuilderDetails;
public:

    enum class QualityLevel : uint8_t {
        LOW,
        /**
         * 低质量
         */
        MEDIUM,
        /**
         * 中等质量
         */
        HIGH,
        /**
         * 高质量
         */
        ULTRA
        /**
         * 超高质量
         */
    };

    enum class LutFormat : uint8_t {
        INTEGER,    //!< 10 bits per component
        /**
         * 每个分量 10 位
         */
        FLOAT,      //!< 16 bits per component (10 bits mantissa precision)
        /**
         * 每个分量 16 位（10 位尾数精度）
         */
    };


    /**
     * List of available tone-mapping operators.
     *
     * @deprecated Use Builder::toneMapper(ToneMapper*) instead
     */
    /**
     * 可用色调映射操作符列表
     *
     * @deprecated 改用 Builder::toneMapper(ToneMapper*)
     */
    enum class UTILS_DEPRECATED ToneMapping : uint8_t {
        LINEAR        = 0,     //!< Linear tone mapping (i.e. no tone mapping)
        /**
         * 线性色调映射（即无色调映射）
         */
        ACES_LEGACY   = 1,     //!< ACES tone mapping, with a brightness modifier to match Filament's legacy tone mapper
        /**
         * ACES 色调映射，带有亮度修改器以匹配 Filament 的传统色调映射器
         */
        ACES          = 2,     //!< ACES tone mapping
        /**
         * ACES 色调映射
         */
        FILMIC        = 3,     //!< Filmic tone mapping, modelled after ACES but applied in sRGB space
        /**
         * Filmic 色调映射，仿照 ACES 但在 sRGB 空间中应用
         */
        DISPLAY_RANGE = 4,     //!< Tone mapping used to validate/debug scene exposure
        /**
         * 用于验证/调试场景曝光的色调映射
         */
    };

    //! Use Builder to construct a ColorGrading object instance
    /**
     * 使用 Builder 构造 ColorGrading 对象实例
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Sets the quality level of the color grading. When color grading is implemented using
         * a 3D LUT, the quality level may impact the resolution and bit depth of the backing
         * 3D texture. For instance, a low quality level will use a 16x16x16 10 bit LUT, a medium
         * quality level will use a 32x32x32 10 bit LUT, a high quality will use a 32x32x32 16 bit
         * LUT, and a ultra quality will use a 64x64x64 16 bit LUT.
         *
         * This setting has no effect if generating a 1D LUT.
         *
         * This overrides the values set by format() and dimensions().
         *
         * The default quality is medium.
         *
         * @param qualityLevel The desired quality of the color grading process
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 设置颜色分级的质量级别。当使用 3D LUT 实现颜色分级时，
         * 质量级别可能影响底层 3D 纹理的分辨率和位深度。例如，低质量级别
         * 将使用 16x16x16 10 位 LUT，中等质量级别将使用 32x32x32 10 位 LUT，
         * 高质量将使用 32x32x32 16 位 LUT，超高质量将使用 64x64x64 16 位 LUT。
         *
         * 如果生成 1D LUT，此设置无效。
         *
         * 这会覆盖 format() 和 dimensions() 设置的值。
         *
         * 默认质量为中等。
         *
         * @param qualityLevel 颜色分级过程所需的质量
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& quality(QualityLevel qualityLevel) noexcept;

        /**
         * When color grading is implemented using a 3D LUT, this sets the texture format of
         * of the LUT. This overrides the value set by quality().
         *
         * This setting has no effect if generating a 1D LUT.
         *
         * The default is INTEGER
         *
         * @param format The desired format of the 3D LUT.
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 当使用 3D LUT 实现颜色分级时，设置 LUT 的纹理格式。
         * 这会覆盖 quality() 设置的值。
         *
         * 如果生成 1D LUT，此设置无效。
         *
         * 默认值为 INTEGER
         *
         * @param format 3D LUT 所需的格式
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& format(LutFormat format) noexcept;

        /**
         * When color grading is implemented using a 3D LUT, this sets the dimension of the LUT.
         * This overrides the value set by quality().
         *
         * This setting has no effect if generating a 1D LUT.
         *
         * The default is 32
         *
         * @param dim The desired dimension of the LUT. Between 16 and 64.
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 当使用 3D LUT 实现颜色分级时，设置 LUT 的维度。
         * 这会覆盖 quality() 设置的值。
         *
         * 如果生成 1D LUT，此设置无效。
         *
         * 默认值为 32
         *
         * @param dim LUT 所需的维度。在 16 和 64 之间。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& dimensions(uint8_t dim) noexcept;

        /**
         * Selects the tone mapping operator to apply to the HDR color buffer as the last
         * operation of the color grading post-processing step.
         *
         * The default tone mapping operator is ACESLegacyToneMapper.
         *
         * The specified tone mapper must have a lifecycle that exceeds the lifetime of
         * this builder. Since the build(Engine&) method is synchronous, it is safe to
         * delete the tone mapper object after that finishes executing.
         *
         * @param toneMapper The tone mapping operator to apply to the HDR color buffer
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 选择要应用到 HDR 颜色缓冲区的色调映射操作符，作为颜色分级
         * 后处理步骤的最后一个操作。
         *
         * 默认色调映射操作符是 ACESLegacyToneMapper。
         *
         * 指定的色调映射器的生命周期必须超过
         * 此构建器的生命周期。由于 build(Engine&) 方法是同步的，在
         * 执行完成后删除色调映射器对象是安全的。
         *
         * @param toneMapper 要应用到 HDR 颜色缓冲区的色调映射操作符
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& toneMapper(ToneMapper const* UTILS_NULLABLE toneMapper) noexcept;

        /**
         * Selects the tone mapping operator to apply to the HDR color buffer as the last
         * operation of the color grading post-processing step.
         *
         * The default tone mapping operator is ACES_LEGACY.
         *
         * @param toneMapping The tone mapping operator to apply to the HDR color buffer
         *
         * @return This Builder, for chaining calls
         *
         * @deprecated Use toneMapper(ToneMapper*) instead
         */
        /**
         * 选择要应用到 HDR 颜色缓冲区的色调映射操作符，作为颜色分级
         * 后处理步骤的最后一个操作。
         *
         * 默认色调映射操作符是 ACES_LEGACY。
         *
         * @param toneMapping 要应用到 HDR 颜色缓冲区的色调映射操作符
         *
         * @return 此 Builder，用于链接调用
         *
         * @deprecated 改用 toneMapper(ToneMapper*)
         */
        UTILS_DEPRECATED
        Builder& toneMapping(ToneMapping toneMapping) noexcept;

        /**
         * Enables or disables the luminance scaling component (LICH) from the exposure value
         * invariant luminance system (EVILS). When this setting is enabled, pixels with high
         * chromatic values will roll-off to white to offer a more natural rendering. This step
         * also helps avoid undesirable hue skews caused by out of gamut colors clipped
         * to the destination color gamut.
         *
         * When luminance scaling is enabled, tone mapping is performed on the luminance of each
         * pixel instead of per-channel.
         *
         * @param luminanceScaling Enables or disables luminance scaling post-tone mapping
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 启用或禁用来自曝光值不变亮度系统 (EVILS) 的亮度缩放分量 (LICH)。
         * 启用此设置后，具有高色度值的像素将衰减为白色以提供更自然的渲染。
         * 此步骤还有助于避免由于超出色域的颜色被裁剪到目标色域而导致的
         * 不希望的色调偏移。
         *
         * 启用亮度缩放后，色调映射在每个
         * 像素的亮度上执行，而不是按通道执行。
         *
         * @param luminanceScaling 启用或禁用色调映射后的亮度缩放
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& luminanceScaling(bool luminanceScaling) noexcept;

        /**
         * Enables or disables gamut mapping to the destination color space's gamut. When gamut
         * mapping is turned off, out-of-gamut colors are clipped to the destination's gamut,
         * which may produce hue skews (blue skewing to purple, green to yellow, etc.). When
         * gamut mapping is enabled, out-of-gamut colors are brought back in gamut by trying to
         * preserve the perceived chroma and lightness of the original values.
         *
         * @param gamutMapping Enables or disables gamut mapping
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 启用或禁用到目标色彩空间色域的色域映射。关闭色域映射时，
         * 超出色域的颜色会被裁剪到目标色域，
         * 这可能会产生色调偏移（蓝色偏移到紫色，绿色到黄色等）。启用
         * 色域映射时，通过尝试保留原始值的感知色度和亮度，将超出色域的颜色
         * 带回色域内。
         *
         * @param gamutMapping 启用或禁用色域映射
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& gamutMapping(bool gamutMapping) noexcept;

        /**
         * Adjusts the exposure of this image. The exposure is specified in stops:
         * each stop brightens (positive values) or darkens (negative values) the image by
         * a factor of 2. This means that an exposure of 3 will brighten the image 8 times
         * more than an exposure of 0 (2^3 = 8 and 2^0 = 1). Contrary to the camera's exposure,
         * this setting is applied after all post-processing (bloom, etc.) are applied.
         *
         * @param exposure Value in EV stops. Can be negative, 0, or positive.
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 调整图像的曝光。曝光以档位指定：
         * 每个档位使图像变亮（正值）或变暗（负值）
         * 2 倍。这意味着曝光 3 将使图像比曝光 0 亮 8 倍
         * （2^3 = 8 和 2^0 = 1）。与相机曝光相反，
         * 此设置在应用所有后处理（泛光等）之后应用。
         *
         * @param exposure EV 档位值。可以是负数、0 或正数。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& exposure(float exposure) noexcept;

        /**
         * Controls the amount of night adaptation to replicate a more natural representation of
         * low-light conditions as perceived by the human vision system. In low-light conditions,
         * peak luminance sensitivity of the eye shifts toward the blue end of the color spectrum:
         * darker tones appear brighter, reducing contrast, and colors are blue shifted (the darker
         * the more intense the effect).
         *
         * @param adaptation Amount of adaptation, between 0 (no adaptation) and 1 (full adaptation).
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 控制夜间适应的量，以复制更自然的
         * 人类视觉系统感知的低光照条件表示。在低光照条件下，
         * 眼睛的峰值亮度灵敏度向光谱的蓝色端移动：
         * 较暗的色调显得更亮，降低了对比度，颜色被蓝移（越暗
         * 效果越强烈）。
         *
         * @param adaptation 适应量，在 0（无适应）和 1（完全适应）之间。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& nightAdaptation(float adaptation) noexcept;

        /**
         * Adjusts the while balance of the image. This can be used to remove color casts
         * and correct the appearance of the white point in the scene, or to alter the
         * overall chromaticity of the image for artistic reasons (to make the image appear
         * cooler or warmer for instance).
         *
         * The while balance adjustment is defined with two values:
         * - Temperature, to modify the color temperature. This value will modify the colors
         *   on a blue/yellow axis. Lower values apply a cool color temperature, and higher
         *   values apply a warm color temperature. The lowest value, -1.0f, is equivalent to
         *   a temperature of 50,000K. The highest value, 1.0f, is equivalent to a temperature
         *   of 2,000K.
         * - Tint, to modify the colors on a green/magenta axis. The lowest value, -1.0f, will
         *   apply a strong green cast, and the highest value, 1.0f, will apply a strong magenta
         *   cast.
         *
         * Both values are expected to be in the range [-1.0..+1.0]. Values outside of that
         * range will be clipped to that range.
         *
         * @param temperature Modification on the blue/yellow axis, as a value between -1.0 and +1.0.
         * @param tint Modification on the green/magenta axis, as a value between -1.0 and +1.0.
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 调整图像的白平衡。这可用于去除色偏
         * 并校正场景中白点的外观，或为了艺术原因改变
         * 图像的整体色度（例如使图像看起来
         * 更冷或更暖）。
         *
         * 白平衡调整由两个值定义：
         * - 色温，用于修改色温。此值将修改
         *   蓝色/黄色轴上的颜色。较低的值应用冷色温，较高
         *   的值应用暖色温。最低值 -1.0f 相当于
         *   50,000K 的色温。最高值 1.0f 相当于
         *   2,000K 的色温。
         * - 色调，用于修改绿色/品红色轴上的颜色。最低值 -1.0f 将
         *   应用强烈的绿色色偏，最高值 1.0f 将应用强烈的品红色
         *   色偏。
         *
         * 两个值都应在 [-1.0..+1.0] 范围内。超出该
         * 范围的值将被钳制到该范围。
         *
         * @param temperature 蓝色/黄色轴上的修改，作为 -1.0 和 +1.0 之间的值
         * @param tint 绿色/品红色轴上的修改，作为 -1.0 和 +1.0 之间的值
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& whiteBalance(float temperature, float tint) noexcept;

        /**
         * The channel mixer adjustment modifies each output color channel using the specified
         * mix of the source color channels.
         *
         * By default each output color channel is set to use 100% of the corresponding source
         * channel and 0% of the other channels. For instance, the output red channel is set to
         * {1.0, 0.0, 1.0} or 100% red, 0% green and 0% blue.
         *
         * Each output channel can add or subtract data from the source channel by using values
         * in the range [-2.0..+2.0]. Values outside of that range will be clipped to that range.
         *
         * Using the channel mixer adjustment you can for instance create a monochrome output
         * by setting all 3 output channels to the same mix. For instance: {0.4, 0.4, 0.2} for
         * all 3 output channels(40% red, 40% green and 20% blue).
         *
         * More complex mixes can be used to create more complex effects. For instance, here is
         * a mix that creates a sepia tone effect:
         * - outRed   = {0.255, 0.858, 0.087}
         * - outGreen = {0.213, 0.715, 0.072}
         * - outBlue  = {0.170, 0.572, 0.058}
         *
         * @param outRed The mix of source RGB for the output red channel, between -2.0 and +2.0
         * @param outGreen The mix of source RGB for the output green channel, between -2.0 and +2.0
         * @param outBlue The mix of source RGB for the output blue channel, between -2.0 and +2.0
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 通道混合器调整使用指定的
         * 源颜色通道混合来修改每个输出颜色通道。
         *
         * 默认情况下，每个输出颜色通道设置为使用相应源通道的 100%
         * 和其他通道的 0%。例如，输出红色通道设置为
         * {1.0, 0.0, 0.0} 或 100% 红色，0% 绿色和 0% 蓝色。
         *
         * 每个输出通道可以通过使用 [-2.0..+2.0] 范围内的值
         * 从源通道添加或减去数据。超出该范围的值将被钳制到该范围。
         *
         * 使用通道混合器调整，你可以例如通过将所有 3 个输出通道设置为相同的混合
         * 来创建单色输出。例如：所有 3 个输出通道都使用 {0.4, 0.4, 0.2}
         * （40% 红色，40% 绿色和 20% 蓝色）。
         *
         * 可以使用更复杂的混合来创建更复杂的效果。例如，以下是
         * 创建棕褐色调效果的混合：
         * - outRed   = {0.255, 0.858, 0.087}
         * - outGreen = {0.213, 0.715, 0.072}
         * - outBlue  = {0.170, 0.572, 0.058}
         *
         * @param outRed 输出红色通道的源 RGB 混合，在 -2.0 和 +2.0 之间
         * @param outGreen 输出绿色通道的源 RGB 混合，在 -2.0 和 +2.0 之间
         * @param outBlue 输出蓝色通道的源 RGB 混合，在 -2.0 和 +2.0 之间
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& channelMixer(
                math::float3 outRed, math::float3 outGreen, math::float3 outBlue) noexcept;

        /**
         * Adjusts the colors separately in 3 distinct tonal ranges or zones: shadows, mid-tones,
         * and highlights.
         *
         * The tonal zones are by the ranges parameter: the x and y components define the beginning
         * and end of the transition from shadows to mid-tones, and the z and w components define
         * the beginning and end of the transition from mid-tones to highlights.
         *
         * A smooth transition is applied between the zones which means for instance that the
         * correction color of the shadows range will partially apply to the mid-tones, and the
         * other way around. This ensure smooth visual transitions in the final image.
         *
         * Each correction color is defined as a linear RGB color and a weight. The weight is a
         * value (which may be positive or negative) that is added to the linear RGB color before
         * mixing. This can be used to darken or brighten the selected tonal range.
         *
         * Shadows/mid-tones/highlights adjustment are performed linear space.
         *
         * @param shadows Linear RGB color (.rgb) and weight (.w) to apply to the shadows
         * @param midtones Linear RGB color (.rgb) and weight (.w) to apply to the mid-tones
         * @param highlights Linear RGB color (.rgb) and weight (.w) to apply to the highlights
         * @param ranges Range of the shadows (x and y), and range of the highlights (z and w)
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 在 3 个不同的色调范围或区域中分别调整颜色：阴影、中间调
         * 和高光。
         *
         * 色调区域由 ranges 参数定义：x 和 y 分量定义从阴影到中间调的过渡的
         * 开始和结束，z 和 w 分量定义
         * 从中间调到高光的过渡的开始和结束。
         *
         * 在区域之间应用平滑过渡，这意味着例如
         * 阴影范围的校正颜色将部分应用到中间调，反之
         * 亦然。这确保了最终图像中的平滑视觉过渡。
         *
         * 每个校正颜色定义为线性 RGB 颜色和权重。权重是
         * 在混合之前添加到线性 RGB 颜色的值（可以是正数或负数）。
         * 这可用于使所选色调范围变暗或变亮。
         *
         * 阴影/中间调/高光调整在线性空间中进行。
         *
         * @param shadows 应用于阴影的线性 RGB 颜色 (.rgb) 和权重 (.w)
         * @param midtones 应用于中间调的线性 RGB 颜色 (.rgb) 和权重 (.w)
         * @param highlights 应用于高光的线性 RGB 颜色 (.rgb) 和权重 (.w)
         * @param ranges 阴影的范围（x 和 y），以及高光的范围（z 和 w）
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& shadowsMidtonesHighlights(
                math::float4 shadows, math::float4 midtones, math::float4 highlights,
                math::float4 ranges) noexcept;

        /**
         * Applies a slope, offset, and power, as defined by the ASC CDL (American Society of
         * Cinematographers Color Decision List) to the image. The CDL can be used to adjust the
         * colors of different tonal ranges in the image.
         *
         * The ASC CDL is similar to the lift/gamma/gain controls found in many color grading tools.
         * Lift is equivalent to a combination of offset and slope, gain is equivalent to slope,
         * and gamma is equivalent to power.
         *
         * The slope and power values must be strictly positive. Values less than or equal to 0 will
         * be clamped to a small positive value, offset can be any positive or negative value.
         *
         * Version 1.2 of the ASC CDL adds saturation control, which is here provided as a separate
         * API. See the saturation() method for more information.
         *
         * Slope/offset/power adjustments are performed in log space.
         *
         * @param slope Multiplier of the input color, must be a strictly positive number
         * @param offset Added to the input color, can be a negative or positive number, including 0
         * @param power Power exponent of the input color, must be a strictly positive number
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 将斜率、偏移和幂应用到图像，如 ASC CDL（美国电影摄影师协会
         * 颜色决策列表）所定义。CDL 可用于调整
         * 图像中不同色调范围的颜色。
         *
         * ASC CDL 类似于许多颜色分级工具中的 lift/gamma/gain 控件。
         * Lift 等效于偏移和斜率的组合，gain 等效于斜率，
         * gamma 等效于幂。
         *
         * 斜率和幂值必须严格为正。小于或等于 0 的值将
         * 被钳制为小的正值，偏移可以是任何正数或负数。
         *
         * ASC CDL 版本 1.2 添加了饱和度控制，此处作为单独的
         * API 提供。有关更多信息，请参见 saturation() 方法。
         *
         * 斜率/偏移/幂调整在对数空间中进行。
         *
         * @param slope 输入颜色的乘数，必须是严格的正数
         * @param offset 添加到输入颜色，可以是负数或正数，包括 0
         * @param power 输入颜色的幂指数，必须是严格的正数
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& slopeOffsetPower(math::float3 slope, math::float3 offset, math::float3 power) noexcept;

        /**
         * Adjusts the contrast of the image. Lower values decrease the contrast of the image
         * (the tonal range is narrowed), and higher values increase the contrast of the image
         * (the tonal range is widened). A value of 1.0 has no effect.
         *
         * The contrast is defined as a value in the range [0.0...2.0]. Values outside of that
         * range will be clipped to that range.
         *
         * Contrast adjustment is performed in log space.
         *
         * @param contrast Contrast expansion, between 0.0 and 2.0. 1.0 leaves contrast unaffected
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 调整图像的对比度。较低的值会降低图像的对比度
         * （色调范围变窄），较高的值会增加图像的对比度
         * （色调范围变宽）。值为 1.0 没有效果。
         *
         * 对比度定义为 [0.0...2.0] 范围内的值。超出该
         * 范围的值将被钳制到该范围。
         *
         * 对比度调整在对数空间中进行。
         *
         * @param contrast 对比度扩展，在 0.0 和 2.0 之间。1.0 使对比度不受影响
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& contrast(float contrast) noexcept;

        /**
         * Adjusts the saturation of the image based on the input color's saturation level.
         * Colors with a high level of saturation are less affected than colors with low saturation
         * levels.
         *
         * Lower vibrance values decrease intensity of the colors present in the image, and
         * higher values increase the intensity of the colors in the image. A value of 1.0 has
         * no effect.
         *
         * The vibrance is defined as a value in the range [0.0...2.0]. Values outside of that
         * range will be clipped to that range.
         *
         * Vibrance adjustment is performed in linear space.
         *
         * @param vibrance Vibrance, between 0.0 and 2.0. 1.0 leaves vibrance unaffected
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 根据输入颜色的饱和度水平调整图像的饱和度。
         * 高饱和度水平的颜色比低饱和度水平的颜色受影响较小。
         *
         * 较低的自然饱和度值会降低图像中颜色的强度，
         * 较高的值会增加图像中颜色的强度。值为 1.0 没有
         * 效果。
         *
         * 自然饱和度定义为 [0.0...2.0] 范围内的值。超出该
         * 范围的值将被钳制到该范围。
         *
         * 自然饱和度调整在线性空间中进行。
         *
         * @param vibrance 自然饱和度，在 0.0 和 2.0 之间。1.0 使自然饱和度不受影响
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& vibrance(float vibrance) noexcept;

        /**
         * Adjusts the saturation of the image. Lower values decrease intensity of the colors
         * present in the image, and higher values increase the intensity of the colors in the
         * image. A value of 1.0 has no effect.
         *
         * The saturation is defined as a value in the range [0.0...2.0]. Values outside of that
         * range will be clipped to that range.
         *
         * Saturation adjustment is performed in linear space.
         *
         * @param saturation Saturation, between 0.0 and 2.0. 1.0 leaves saturation unaffected
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 调整图像的饱和度。较低的值会降低图像中颜色的
         * 强度，较高的值会增加图像中颜色的
         * 强度。值为 1.0 没有效果。
         *
         * 饱和度定义为 [0.0...2.0] 范围内的值。超出该
         * 范围的值将被钳制到该范围。
         *
         * 饱和度调整在线性空间中进行。
         *
         * @param saturation 饱和度，在 0.0 和 2.0 之间。1.0 使饱和度不受影响
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& saturation(float saturation) noexcept;

        /**
         * Applies a curve to each RGB channel of the image. Each curve is defined by 3 values:
         * a gamma value applied to the shadows only, a mid-point indicating where shadows stop
         * and highlights start, and a scale factor for the highlights.
         *
         * The gamma and mid-point must be strictly positive values. If they are not, they will be
         * clamped to a small positive value. The scale can be any negative of positive value.
         *
         * Curves are applied in linear space.
         *
         * @param shadowGamma Power value to apply to the shadows, must be strictly positive
         * @param midPoint Mid-point defining where shadows stop and highlights start, must be strictly positive
         * @param highlightScale Scale factor for the highlights, can be any negative or positive value
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 将曲线应用于图像的每个 RGB 通道。每条曲线由 3 个值定义：
         * 仅应用于阴影的 gamma 值，指示阴影停止
         * 和高光开始的中点，以及高光的缩放因子。
         *
         * gamma 和中点必须是严格的正值。如果不是，它们将被
         * 钳制为小的正值。缩放可以是任何负数或正值。
         *
         * 曲线在线性空间中应用。
         *
         * @param shadowGamma 应用于阴影的幂值，必须是严格的正数
         * @param midPoint 定义阴影停止和高光开始的中点，必须是严格的正数
         * @param highlightScale 高光的缩放因子，可以是任何负数或正数
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& curves(math::float3 shadowGamma, math::float3 midPoint, math::float3 highlightScale) noexcept;

        /**
         * Sets the output color space for this ColorGrading object. After all color grading steps
         * have been applied, the final color will be converted in the desired color space.
         *
         * NOTE: Currently the output color space must be one of Rec709-sRGB-D65 or
         *       Rec709-Linear-D65. Only the transfer function is taken into account.
         *
         * @param colorSpace The output color space.
         *
         * @return This Builder, for chaining calls
         */
        /**
         * 设置此 ColorGrading 对象的输出色彩空间。应用所有颜色分级步骤
         * 后，最终颜色将在所需的色彩空间中转换。
         *
         * 注意：当前输出色彩空间必须是 Rec709-sRGB-D65 或
         *       Rec709-Linear-D65 之一。仅考虑传递函数。
         *
         * @param colorSpace 输出色彩空间
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& outputColorSpace(const color::ColorSpace& colorSpace) noexcept;

        /**
         * Creates the ColorGrading object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this ColorGrading with.
         *
         * @return pointer to the newly created object.
         */
        /**
         * 创建 ColorGrading 对象并返回指向它的指针
         *
         * @param engine 要与此 ColorGrading 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         */
        ColorGrading* UTILS_NONNULL build(Engine& engine);

    private:
        friend class FColorGrading;
    };

protected:
    // prevent heap allocation
    ~ColorGrading() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_COLORGRADING_H
