/*
 * Copyright (C) 2015 The Android Open Source Project
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
/**
 * \file
 * Color 实用函数头文件
 */

#ifndef TNT_FILAMENT_COLOR_H
#define TNT_FILAMENT_COLOR_H

#include <utils/compiler.h>

#include <math/vec3.h>
#include <math/vec4.h>

#include <stdint.h>
#include <stddef.h>

namespace filament {

//! RGB color in linear space
/**
 * 线性空间中的 RGB 颜色
 */
using LinearColor = math::float3;

//! RGB color in sRGB space
/**
 * sRGB 空间中的 RGB 颜色
 */
using sRGBColor  = math::float3;

//! RGBA color in linear space, with alpha
/**
 * 线性空间中的 RGBA 颜色，带 alpha
 */
using LinearColorA = math::float4;

//! RGBA color in sRGB space, with alpha
/**
 * sRGB 空间中的 RGBA 颜色，带 alpha
 */
using sRGBColorA  = math::float4;

//! types of RGB colors
/**
 * RGB 颜色类型
 */
enum class RgbType : uint8_t {
    sRGB,   //!< the color is defined in Rec.709-sRGB-D65 (sRGB) space
    /**
     * 颜色在 Rec.709-sRGB-D65 (sRGB) 空间中定义
     */
    LINEAR, //!< the color is defined in Rec.709-Linear-D65 ("linear sRGB") space
    /**
     * 颜色在 Rec.709-Linear-D65 ("线性 sRGB") 空间中定义
     */
};

//! types of RGBA colors
/**
 * RGBA 颜色类型
 */
enum class RgbaType : uint8_t {
    /**
     * the color is defined in Rec.709-sRGB-D65 (sRGB) space and the RGB values
     * have not been pre-multiplied by the alpha (for instance, a 50%
     * transparent red is <1,0,0,0.5>)
     */
    /**
     * 颜色在 Rec.709-sRGB-D65 (sRGB) 空间中定义，RGB 值
     * 未被 alpha 预乘（例如，50%
     * 透明的红色是 <1,0,0,0.5>）
     */
    sRGB,
    /**
     * the color is defined in Rec.709-Linear-D65 ("linear sRGB") space and the
     * RGB values have not been pre-multiplied by the alpha (for instance, a 50%
     * transparent red is <1,0,0,0.5>)
     */
    /**
     * 颜色在 Rec.709-Linear-D65 ("线性 sRGB") 空间中定义，
     * RGB 值未被 alpha 预乘（例如，50%
     * 透明的红色是 <1,0,0,0.5>）
     */
    LINEAR,
    /**
     * the color is defined in Rec.709-sRGB-D65 (sRGB) space and the RGB values
     * have been pre-multiplied by the alpha (for instance, a 50%
     * transparent red is <0.5,0,0,0.5>)
     */
    /**
     * 颜色在 Rec.709-sRGB-D65 (sRGB) 空间中定义，RGB 值
     * 已被 alpha 预乘（例如，50%
     * 透明的红色是 <0.5,0,0,0.5>）
     */
    PREMULTIPLIED_sRGB,
    /**
     * the color is defined in Rec.709-Linear-D65 ("linear sRGB") space and the
     * RGB values have been pre-multiplied by the alpha (for instance, a 50%
     * transparent red is <0.5,0,0,0.5>)
     */
    /**
     * 颜色在 Rec.709-Linear-D65 ("线性 sRGB") 空间中定义，
     * RGB 值已被 alpha 预乘（例如，50%
     * 透明的红色是 <0.5,0,0,0.5>）
     */
    PREMULTIPLIED_LINEAR
};

//! type of color conversion to use when converting to/from sRGB and linear spaces
/**
 * 在 sRGB 和线性空间之间转换时使用的颜色转换类型
 */
enum ColorConversion {
    ACCURATE,   //!< accurate conversion using the sRGB standard
    /**
     * 使用 sRGB 标准的精确转换
     */
    FAST        //!< fast conversion using a simple gamma 2.2 curve
    /**
     * 使用简单 gamma 2.2 曲线的快速转换
     */
};

/**
 * Utilities to manipulate and convert colors
 */
/**
 * 用于操作和转换颜色的实用函数
 */
class UTILS_PUBLIC Color {
public:
    //! converts an RGB color to linear space, the conversion depends on the specified type
    /**
     * 将 RGB 颜色转换为线性空间，转换取决于指定的类型
     */
    static LinearColor toLinear(RgbType type, math::float3 color);

    //! converts an RGBA color to linear space, the conversion depends on the specified type
    /**
     * 将 RGBA 颜色转换为线性空间，转换取决于指定的类型
     */
    static LinearColorA toLinear(RgbaType type, math::float4 color);

    //! converts an RGB color in sRGB space to an RGB color in linear space
    /**
     * 将 sRGB 空间中的 RGB 颜色转换为线性空间中的 RGB 颜色
     */
    template<ColorConversion = ACCURATE>
    static LinearColor toLinear(sRGBColor const& color);

    /**
     * Converts an RGB color in Rec.709-Linear-D65 ("linear sRGB") space to an
     * RGB color in Rec.709-sRGB-D65 (sRGB) space.
     */
    /**
     * 将 Rec.709-Linear-D65 ("线性 sRGB") 空间中的 RGB 颜色转换为
     * Rec.709-sRGB-D65 (sRGB) 空间中的 RGB 颜色
     */
    template<ColorConversion = ACCURATE>
    static sRGBColor toSRGB(LinearColor const& color);

    /**
     * Converts an RGBA color in Rec.709-sRGB-D65 (sRGB) space to an RGBA color in
     * Rec.709-Linear-D65 ("linear sRGB") space the alpha component is left unmodified.
     */
    /**
     * 将 Rec.709-sRGB-D65 (sRGB) 空间中的 RGBA 颜色转换为
     * Rec.709-Linear-D65 ("线性 sRGB") 空间中的 RGBA 颜色，alpha 分量保持不变
     */
    template<ColorConversion = ACCURATE>
    static LinearColorA toLinear(sRGBColorA const& color);

    /**
     * Converts an RGBA color in Rec.709-Linear-D65 ("linear sRGB") space to
     * an RGBA color in Rec.709-sRGB-D65 (sRGB) space the alpha component is
     * left unmodified.
     */
    /**
     * 将 Rec.709-Linear-D65 ("线性 sRGB") 空间中的 RGBA 颜色转换为
     * Rec.709-sRGB-D65 (sRGB) 空间中的 RGBA 颜色，alpha 分量
     * 保持不变
     */
    template<ColorConversion = ACCURATE>
    static sRGBColorA toSRGB(LinearColorA const& color);

    /**
     * Converts a correlated color temperature to a linear RGB color in sRGB
     * space the temperature must be expressed in kelvin and must be in the
     * range 1,000K to 15,000K.
     */
    /**
     * 将相关色温转换为 sRGB 空间中的线性 RGB 颜色。
     * 温度必须以开尔文表示，并且必须在
     * 1,000K 到 15,000K 的范围内。
     */
    static LinearColor cct(float K);

    /**
     * Converts a CIE standard illuminant series D to a linear RGB color in
     * sRGB space the temperature must be expressed in kelvin and must be in
     * the range 4,000K to 25,000K
     */
    /**
     * 将 CIE 标准光源 D 系列转换为
     * sRGB 空间中的线性 RGB 颜色。温度必须以开尔文表示，并且必须在
     * 4,000K 到 25,000K 的范围内
     */
    static LinearColor illuminantD(float K);

    /**
     * Computes the Beer-Lambert absorption coefficients from the specified
     * transmittance color and distance. The computed absorption will guarantee
     * the white light will become the specified color at the specified distance.
     * The output of this function can be used as the absorption parameter of
     * materials that use refraction.
     *
     * @param color the desired linear RGB color in sRGB space
     * @param distance the distance at which white light should become the specified color
     *
     * @return absorption coefficients for the Beer-Lambert law
     */
    /**
     * 从指定的
     * 透射颜色和距离计算 Beer-Lambert 吸收系数。计算出的吸收将保证
     * 白光在指定距离处变为指定颜色。
     * 此函数的输出可用作使用折射的
     * 材质的吸收参数。
     *
     * @param color sRGB 空间中所需的线性 RGB 颜色
     * @param distance 白光应变为指定颜色的距离
     *
     * @return Beer-Lambert 定律的吸收系数
     */
    static math::float3 absorptionAtDistance(LinearColor const& color, float distance);

private:
    /**
     * 将 sRGB 颜色转换为线性颜色（私有方法，使用标准 sRGB 转换）
     * @param color sRGB 颜色
     * @return 线性颜色
     */
    static math::float3 sRGBToLinear(math::float3 color) noexcept;
    /**
     * 将线性颜色转换为 sRGB 颜色（私有方法，使用标准 sRGB 转换）
     * @param color 线性颜色
     * @return sRGB 颜色
     */
    static math::float3 linearToSRGB(math::float3 color) noexcept;
};

// Use the default implementation from the header
/**
 * 使用头文件中的默认实现
 */
/**
 * FAST 模式的 toLinear 模板特化：使用简单的 gamma 2.2 曲线进行快速转换
 */
template<>
inline LinearColor Color::toLinear<FAST>(sRGBColor const& color) {
    return pow(color, 2.2f);
}

/**
 * FAST 模式的 toLinear RGBA 模板特化：使用简单的 gamma 2.2 曲线进行快速转换
 */
template<>
inline LinearColorA Color::toLinear<FAST>(sRGBColorA const& color) {
    return LinearColorA{pow(color.rgb, 2.2f), color.a};
}

/**
 * ACCURATE 模式的 toLinear 模板特化：使用标准 sRGB 转换进行精确转换
 */
template<>
inline LinearColor Color::toLinear<ACCURATE>(sRGBColor const& color) {
    return sRGBToLinear(color);
}

/**
 * ACCURATE 模式的 toLinear RGBA 模板特化：使用标准 sRGB 转换进行精确转换
 */
template<>
inline LinearColorA Color::toLinear<ACCURATE>(sRGBColorA const& color) {
    return LinearColorA{sRGBToLinear(color.rgb), color.a};
}

// Use the default implementation from the header
/**
 * 使用头文件中的默认实现
 */
/**
 * FAST 模式的 toSRGB 模板特化：使用简单的 gamma 1/2.2 曲线进行快速转换
 */
template<>
inline sRGBColor Color::toSRGB<FAST>(LinearColor const& color) {
    return pow(color, 1.0f / 2.2f);
}

/**
 * FAST 模式的 toSRGB RGBA 模板特化：使用简单的 gamma 1/2.2 曲线进行快速转换
 */
template<>
inline sRGBColorA Color::toSRGB<FAST>(LinearColorA const& color) {
    return sRGBColorA{pow(color.rgb, 1.0f / 2.2f), color.a};
}

/**
 * ACCURATE 模式的 toSRGB 模板特化：使用标准 sRGB 转换进行精确转换
 */
template<>
inline sRGBColor Color::toSRGB<ACCURATE>(LinearColor const& color) {
    return linearToSRGB(color);
}

/**
 * ACCURATE 模式的 toSRGB RGBA 模板特化：使用标准 sRGB 转换进行精确转换
 */
template<>
inline sRGBColorA Color::toSRGB<ACCURATE>(LinearColorA const& color) {
    return sRGBColorA{linearToSRGB(color.rgb), color.a};
}

/**
 * toLinear(RgbType type, math::float3 color) 的内联实现
 */
inline LinearColor Color::toLinear(RgbType type, math::float3 color) {
    return (type == RgbType::LINEAR) ? color : toLinear<ACCURATE>(color);
}

// converts an RGBA color to linear space
// the conversion depends on the specified type
/**
 * 将 RGBA 颜色转换为线性空间
 * 转换取决于指定的类型
 */
inline LinearColorA Color::toLinear(RgbaType type, math::float4 color) {
    switch (type) {
        case RgbaType::sRGB:
            return toLinear<ACCURATE>(color) * math::float4{color.a, color.a, color.a, 1.0f};
        case RgbaType::LINEAR:
            return color * math::float4{color.a, color.a, color.a, 1.0f};
        case RgbaType::PREMULTIPLIED_sRGB:
            return toLinear<ACCURATE>(color);
        case RgbaType::PREMULTIPLIED_LINEAR:
            return color;
    }
}

} // namespace filament

#endif // TNT_FILAMENT_COLOR_H
