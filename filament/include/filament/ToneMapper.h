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

#ifndef TNT_FILAMENT_TONEMAPPER_H
#define TNT_FILAMENT_TONEMAPPER_H

#include <utils/compiler.h>

#include <math/mathfwd.h>

#include <cstdint>

namespace filament {

/**
 * Interface for tone mapping operators. A tone mapping operator, or tone mapper,
 * is responsible for compressing the dynamic range of the rendered scene to a
 * dynamic range suitable for display.
 *
 * In Filament, tone mapping is a color grading step. ToneMapper instances are
 * created and passed to the ColorGrading::Builder to produce a 3D LUT that will
 * be used during post-processing to prepare the final color buffer for display.
 *
 * Filament provides several default tone mapping operators that fall into three
 * categories:
 *
 * - Configurable tone mapping operators
 *   - GenericToneMapper
 *   - AgXToneMapper
 * - Fixed-aesthetic tone mapping operators
 *   - ACESToneMapper
 *   - ACESLegacyToneMapper
 *   - FilmicToneMapper
 *   - PBRNeutralToneMapper
 * - Debug/validation tone mapping operators
 *   - LinearToneMapper
 *   - DisplayRangeToneMapper
 *
 * You can create custom tone mapping operators by subclassing ToneMapper.
 */
/**
 * 色调映射操作符接口。色调映射操作符（tone mapper）负责
 * 将渲染场景的动态范围压缩到适合显示的动态范围。
 *
 * 在 Filament 中，色调映射是颜色分级步骤。ToneMapper 实例被
 * 创建并传递给 ColorGrading::Builder 以生成 3D LUT，该 LUT 将在
 * 后处理期间用于准备最终的颜色缓冲区以供显示。
 *
 * Filament 提供几种默认的色调映射操作符，分为三类：
 *
 * - 可配置的色调映射操作符
 *   - GenericToneMapper
 *   - AgXToneMapper
 * - 固定美学的色调映射操作符
 *   - ACESToneMapper
 *   - ACESLegacyToneMapper
 *   - FilmicToneMapper
 *   - PBRNeutralToneMapper
 * - 调试/验证色调映射操作符
 *   - LinearToneMapper
 *   - DisplayRangeToneMapper
 *
 * 你可以通过子类化 ToneMapper 来创建自定义色调映射操作符。
 */
struct UTILS_PUBLIC ToneMapper {
    ToneMapper() noexcept;
    virtual ~ToneMapper() noexcept;

    /**
     * Maps an open domain (or "scene referred" values) color value to display
     * domain (or "display referred") color value. Both the input and output
     * color values are defined in the Rec.2020 color space, with no transfer
     * function applied ("linear Rec.2020").
     *
     * @param c Input color to tone map, in the Rec.2020 color space with no
     *          transfer function applied ("linear")
     *
     * @return A tone mapped color in the Rec.2020 color space, with no transfer
     *         function applied ("linear")
     */
    /**
     * 将开放域（或"场景参考"值）颜色值映射到显示
     * 域（或"显示参考"）颜色值。输入和输出
     * 颜色值都在 Rec.2020 颜色空间中定义，没有应用
     * 传递函数（"线性 Rec.2020"）。
     *
     * @param c 要进行色调映射的输入颜色，在 Rec.2020 颜色空间中，
     *          未应用传递函数（"线性"）
     *
     * @return 色调映射后的颜色，在 Rec.2020 颜色空间中，未应用
     *         传递函数（"线性"）
     */
    virtual math::float3 operator()(math::float3 c) const noexcept = 0;

    /**
     * If true, then this function holds that f(x) = vec3(f(x.r), f(x.g), f(x.b))
     *
     * This may be used to indicate that the color grading's LUT only requires a 1D texture instead
     * of a 3D texture, potentially saving a significant amount of memory and generation time.
     */
    /**
     * 如果为 true，则此函数满足 f(x) = vec3(f(x.r), f(x.g), f(x.b))
     *
     * 这可用于指示颜色分级的 LUT 只需要 1D 纹理而不是
     * 3D 纹理，可能节省大量内存和生成时间。
     */
    virtual bool isOneDimensional() const noexcept { return false; }

    /**
     * True if this tonemapper only works in low-dynamic-range.
     *
     * This may be used to indicate that the color grading's LUT doesn't need to be log encoded.
     */
    /**
     * 如果此色调映射器仅在低动态范围下工作，则为 true
     *
     * 这可用于指示颜色分级的 LUT 不需要对数编码。
     */
    virtual bool isLDR() const noexcept { return false; }
};

/**
 * Linear tone mapping operator that returns the input color but clamped to
 * the 0..1 range. This operator is mostly useful for debugging.
 */
/**
 * 线性色调映射操作符，返回输入颜色但钳制到
 * 0..1 范围。此操作符主要用于调试。
 */
struct UTILS_PUBLIC LinearToneMapper final : public ToneMapper {
    LinearToneMapper() noexcept;
    ~LinearToneMapper() noexcept final;

    math::float3 operator()(math::float3 c) const noexcept override;
    bool isOneDimensional() const noexcept override { return true; }
    bool isLDR() const noexcept override { return true; }
};

/**
 * ACES tone mapping operator. This operator is an implementation of the
 * ACES Reference Rendering Transform (RRT) combined with the Output Device
 * Transform (ODT) for sRGB monitors (dim surround, 100 nits).
 */
/**
 * ACES 色调映射操作符。此操作符是
 * ACES 参考渲染变换 (RRT) 与输出设备
 * 变换 (ODT) 的组合实现，适用于 sRGB 显示器（暗环境，100 尼特）。
 */
struct UTILS_PUBLIC ACESToneMapper final : public ToneMapper {
    ACESToneMapper() noexcept;
    ~ACESToneMapper() noexcept final;

    math::float3 operator()(math::float3 c) const noexcept override;
    bool isOneDimensional() const noexcept override { return false; }
    bool isLDR() const noexcept override { return false; }
};

/**
 * ACES tone mapping operator, modified to match the perceived brightness
 * of FilmicToneMapper. This operator is the same as ACESToneMapper but
 * applies a brightness multiplier of ~1.6 to the input color value to
 * target brighter viewing environments.
 */
/**
 * ACES 色调映射操作符，修改以匹配
 * FilmicToneMapper 的感知亮度。此操作符与 ACESToneMapper 相同，但
 * 对输入颜色值应用约 1.6 的亮度乘数以
 * 针对更亮的观看环境。
 */
struct UTILS_PUBLIC ACESLegacyToneMapper final : public ToneMapper {
    ACESLegacyToneMapper() noexcept;
    ~ACESLegacyToneMapper() noexcept final;

    math::float3 operator()(math::float3 c) const noexcept override;
    bool isOneDimensional() const noexcept override { return false; }
    bool isLDR() const noexcept override { return false; }
};

/**
 * "Filmic" tone mapping operator. This tone mapper was designed to
 * approximate the aesthetics of the ACES RRT + ODT for Rec.709
 * and historically Filament's default tone mapping operator. It exists
 * only for backward compatibility purposes and is not otherwise recommended.
 */
/**
 * "Filmic" 色调映射操作符。此色调映射器旨在
 * 近似 Rec.709 的 ACES RRT + ODT 的美学效果，
 * 历史上是 Filament 的默认色调映射操作符。它存在
 * 仅用于向后兼容目的，否则不推荐使用。
 */
struct UTILS_PUBLIC FilmicToneMapper final : public ToneMapper {
    FilmicToneMapper() noexcept;
    ~FilmicToneMapper() noexcept final;

    math::float3 operator()(math::float3 x) const noexcept override;
    bool isOneDimensional() const noexcept override { return true; }
    bool isLDR() const noexcept override { return false; }
};

/**
 * Khronos PBR Neutral tone mapping operator. This tone mapper was designed
 * to preserve the appearance of materials across lighting conditions while
 * avoiding artifacts in the highlights in high dynamic range conditions.
 */
/**
 * Khronos PBR Neutral 色调映射操作符。此色调映射器旨在
 * 在不同光照条件下保持材质外观，同时
 * 避免高动态范围条件下高光中的伪影。
 */
struct UTILS_PUBLIC PBRNeutralToneMapper final : public ToneMapper {
    PBRNeutralToneMapper() noexcept;
    ~PBRNeutralToneMapper() noexcept final;

    math::float3 operator()(math::float3 x) const noexcept override;
    bool isOneDimensional() const noexcept override { return false; }
    bool isLDR() const noexcept override { return false; }
};

/**
 * AgX tone mapping operator.
 */
/**
 * AgX 色调映射操作符
 */
struct UTILS_PUBLIC AgxToneMapper final : public ToneMapper {
    enum class AgxLook : uint8_t {
        NONE = 0,   //!< Base contrast with no look applied
        /**
         * 基础对比度，不应用外观
         */
        PUNCHY,     //!< A punchy and more chroma laden look for sRGB displays
        /**
         * 适用于 sRGB 显示器的饱满且更多色度的外观
         */
        GOLDEN      //!< A golden tinted, slightly washed look for BT.1886 displays
        /**
         * 适用于 BT.1886 显示器的金色调、略微淡化的外观
         */
    };

    /**
     * Builds a new AgX tone mapper.
     *
     * @param look an optional creative adjustment to contrast and saturation
     */
    /**
     * 构建新的 AgX 色调映射器
     *
     * @param look 对比度和饱和度的可选创意调整
     */
    explicit AgxToneMapper(AgxLook look = AgxLook::NONE) noexcept;
    ~AgxToneMapper() noexcept final;

    math::float3 operator()(math::float3 x) const noexcept override;
    bool isOneDimensional() const noexcept override { return false; }
    bool isLDR() const noexcept override { return false; }

    AgxLook look;
};

/**
 * Generic tone mapping operator that gives control over the tone mapping
 * curve. This operator can be used to control the aesthetics of the final
 * image. This operator also allows to control the dynamic range of the
 * scene referred values.
 *
 * The tone mapping curve is defined by 5 parameters:
 * - contrast: controls the contrast of the curve
 * - midGrayIn: sets the input middle gray
 * - midGrayOut: sets the output middle gray
 * - hdrMax: defines the maximum input value that will be mapped to
 *           output white
 */
/**
 * 通用色调映射操作符，可控制色调映射
 * 曲线。此操作符可用于控制最终
 * 图像的美学效果。此操作符还允许控制
 * 场景参考值的动态范围。
 *
 * 色调映射曲线由 5 个参数定义：
 * - contrast: 控制曲线的对比度
 * - midGrayIn: 设置输入中间灰
 * - midGrayOut: 设置输出中间灰
 * - hdrMax: 定义将映射到
 *           输出白色的最大输入值
 */
struct UTILS_PUBLIC GenericToneMapper final : public ToneMapper {
    /**
     * Builds a new generic tone mapper. The default values of the
     * constructor parameters approximate an ACES tone mapping curve
     * and the maximum input value is set to 10.0.
     *
     * @param contrast controls the contrast of the curve, must be > 0.0, values
     *                 in the range 0.5..2.0 are recommended.
     * @param midGrayIn sets the input middle gray, between 0.0 and 1.0.
     * @param midGrayOut sets the output middle gray, between 0.0 and 1.0.
     * @param hdrMax defines the maximum input value that will be mapped to
     *               output white. Must be >= 1.0.
     */
    /**
     * 构建新的通用色调映射器。构造函数参数的
     * 默认值近似于 ACES 色调映射曲线，
     * 最大输入值设置为 10.0。
     *
     * @param contrast 控制曲线的对比度，必须 > 0.0，建议值
     *                 在 0.5..2.0 范围内
     * @param midGrayIn 设置输入中间灰，在 0.0 和 1.0 之间
     * @param midGrayOut 设置输出中间灰，在 0.0 和 1.0 之间
     * @param hdrMax 定义将映射到
     *               输出白色的最大输入值。必须 >= 1.0
     */
    explicit GenericToneMapper(
            float contrast = 1.55f,
            float midGrayIn = 0.18f,
            float midGrayOut = 0.215f,
            float hdrMax = 10.0f
    ) noexcept;
    ~GenericToneMapper() noexcept final;

    GenericToneMapper(GenericToneMapper const&) = delete;
    GenericToneMapper& operator=(GenericToneMapper const&) = delete;
    GenericToneMapper(GenericToneMapper&& rhs)  noexcept;
    GenericToneMapper& operator=(GenericToneMapper&& rhs) noexcept;

    math::float3 operator()(math::float3 x) const noexcept override;
    bool isOneDimensional() const noexcept override { return true; }
    bool isLDR() const noexcept override { return false; }

    /** Returns the contrast of the curve as a strictly positive value. */
    /**
     * 返回曲线的对比度，作为严格正值
     */
    float getContrast() const noexcept;

    /** Returns the middle gray point for input values as a value between 0.0 and 1.0. */
    /**
     * 返回输入值的中间灰点，作为 0.0 和 1.0 之间的值
     */
    float getMidGrayIn() const noexcept;

    /** Returns the middle gray point for output values as a value between 0.0 and 1.0. */
    /**
     * 返回输出值的中间灰点，作为 0.0 和 1.0 之间的值
     */
    float getMidGrayOut() const noexcept;

    /** Returns the maximum input value that will map to output white, as a value >= 1.0. */
    /**
     * 返回将映射到输出白色的最大输入值，作为 >= 1.0 的值
     */
    float getHdrMax() const noexcept;

    /** Sets the contrast of the curve, must be > 0.0, values in the range 0.5..2.0 are recommended. */
    /**
     * 设置曲线的对比度，必须 > 0.0，建议值在 0.5..2.0 范围内
     */
    void setContrast(float contrast) noexcept;

    /** Sets the input middle gray, between 0.0 and 1.0. */
    /**
     * 设置输入中间灰，在 0.0 和 1.0 之间
     */
    void setMidGrayIn(float midGrayIn) noexcept;

    /** Sets the output middle gray, between 0.0 and 1.0. */
    /**
     * 设置输出中间灰，在 0.0 和 1.0 之间
     */
    void setMidGrayOut(float midGrayOut) noexcept;

    /** Defines the maximum input value that will be mapped to output white. Must be >= 1.0. */
    /**
     * 定义将映射到输出白色的最大输入值。必须 >= 1.0
     */
    void setHdrMax(float hdrMax) noexcept;

private:
    struct Options;
    Options* mOptions;
};

/**
 * A tone mapper that converts the input HDR RGB color into one of 16 debug colors
 * that represent the pixel's exposure. When the output is cyan, the input color
 * represents  middle gray (18% exposure). Every exposure stop above or below middle
 * gray causes a color shift.
 *
 * The relationship between exposures and colors is:
 *
 * - -5EV  black
 * - -4EV  darkest blue
 * - -3EV  darker blue
 * - -2EV  dark blue
 * - -1EV  blue
 * -  OEV  cyan
 * - +1EV  dark green
 * - +2EV  green
 * - +3EV  yellow
 * - +4EV  yellow-orange
 * - +5EV  orange
 * - +6EV  bright red
 * - +7EV  red
 * - +8EV  magenta
 * - +9EV  purple
 * - +10EV white
 *
 * This tone mapper is useful to validate and tweak scene lighting.
 */
/**
 * 将输入 HDR RGB 颜色转换为 16 种调试颜色之一的色调映射器，
 * 这些颜色表示像素的曝光。当输出为青色时，输入颜色
 * 表示中间灰（18% 曝光）。中间灰上方或下方的每个曝光档
 * 都会导致颜色偏移。
 *
 * 曝光和颜色之间的关系：
 *
 * - -5EV  黑色
 * - -4EV  最深蓝
 * - -3EV  深蓝
 * - -2EV  暗蓝
 * - -1EV  蓝色
 * -  OEV  青色
 * - +1EV  深绿
 * - +2EV  绿色
 * - +3EV  黄色
 * - +4EV  黄橙
 * - +5EV  橙色
 * - +6EV  亮红
 * - +7EV  红色
 * - +8EV  品红
 * - +9EV  紫色
 * - +10EV 白色
 *
 * 此色调映射器对于验证和调整场景光照很有用。
 */
struct UTILS_PUBLIC DisplayRangeToneMapper final : public ToneMapper {
    DisplayRangeToneMapper() noexcept;
    ~DisplayRangeToneMapper() noexcept override;

    math::float3 operator()(math::float3 c) const noexcept override;
    bool isOneDimensional() const noexcept override { return false; }
    bool isLDR() const noexcept override { return false; }
};

} // namespace filament

#endif // TNT_FILAMENT_TONEMAPPER_H
