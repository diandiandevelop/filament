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

#ifndef TNT_FILAMENT_COLOR_SPACE_H
#define TNT_FILAMENT_COLOR_SPACE_H

#include <math/vec2.h>

namespace filament::color {

using namespace math;

/**
 * Holds the chromaticities of a color space's primaries as xy coordinates
 * in xyY (Y is assumed to be 1).
 */
/**
 * 保存色彩空间基色的色度坐标，作为 xyY 空间中的 xy 坐标
 * （假设 Y 为 1）。
 */
struct Primaries {
    float2 r;  //!< 红色基色的 xy 坐标
    float2 g;  //!< 绿色基色的 xy 坐标
    float2 b;  //!< 蓝色基色的 xy 坐标

    /**
     * 比较两个 Primaries 是否相等
     * @param rhs 要比较的另一个 Primaries
     * @return 如果所有基色坐标都相等则返回 true
     */
    bool operator==(const Primaries& rhs) const noexcept {
        return r == rhs.r && b == rhs.b && g == rhs.g;
    }
};

//! Reference white for a color space, defined as the xy coordinates in the xyY space.
/**
 * 色彩空间的参考白点，定义为 xyY 空间中的 xy 坐标。
 */
using WhitePoint = float2;

/**
 * <p>Defines the parameters for the ICC parametric curve type 4, as
 * defined in ICC.1:2004-10, section 10.15.</p>
 *
 * <p>The EOTF is of the form:</p>
 *
 * \(\begin{equation}
 * Y = \begin{cases}c X + f & X \lt d \\\
 * \left( a X + b \right) ^{g} + e & X \ge d \end{cases}
 * \end{equation}\)
 *
 * <p>The corresponding OETF is simply the inverse function.</p>
 *
 * <p>The parameters defined by this class form a valid transfer
 * function only if all the following conditions are met:</p>
 * <ul>
 *     <li>No parameter is a NaN</li>
 *     <li>\(d\) is in the range \([0..1]\)</li>
 *     <li>The function is not constant</li>
 *     <li>The function is positive and increasing</li>
 * </ul>
 */
/**
 * 定义 ICC 参数曲线类型 4 的参数，如
 * ICC.1:2004-10 第 10.15 节所定义。
 *
 * EOTF 的形式为：
 *
 * Y = {c X + f           (X < d)
 *      (a X + b)^g + e   (X >= d)}
 *
 * 相应的 OETF 就是逆函数。
 *
 * 此类定义的参数形成有效的传递
 * 函数，仅当满足以下所有条件时：
 * - 没有参数是 NaN
 * - d 在范围 [0..1] 内
 * - 函数不是常数
 * - 函数是正的且递增的
 */
struct TransferFunction {
    /**
     * <p>Defines the parameters for the ICC parametric curve type 4, as
     * defined in ICC.1:2004-10, section 10.15.</p>
     *
     * <p>The EOTF is of the form:</p>
     *
     * \(\begin{equation}
     * Y = \begin{cases}c X + f & X \lt d \\\
     * \left( a X + b \right) ^{g} + e & X \ge d \end{cases}
     * \end{equation}\)
     *
     * @param a The value of \(a\) in the equation of the EOTF described above
     * @param b The value of \(b\) in the equation of the EOTF described above
     * @param c The value of \(c\) in the equation of the EOTF described above
     * @param d The value of \(d\) in the equation of the EOTF described above
     * @param e The value of \(e\) in the equation of the EOTF described above
     * @param f The value of \(f\) in the equation of the EOTF described above
     * @param g The value of \(g\) in the equation of the EOTF described above
     */
    /**
     * 定义 ICC 参数曲线类型 4 的参数（完整版本，包含所有 7 个参数）。
     * 定义传递函数的完整参数。
     *
     * @param a EOTF 方程中 \(a\) 的值
     * @param b EOTF 方程中 \(b\) 的值
     * @param c EOTF 方程中 \(c\) 的值
     * @param d EOTF 方程中 \(d\) 的值（阈值）
     * @param e EOTF 方程中 \(e\) 的值
     * @param f EOTF 方程中 \(f\) 的值
     * @param g EOTF 方程中 \(g\) 的值（gamma）
     */
    constexpr TransferFunction(
            double a,
            double b,
            double c,
            double d,
            double e,
            double f,
            double g
    ) : a(a),
        b(b),
        c(c),
        d(d),
        e(e),
        f(f),
        g(g) {
    }

    /**
     * <p>Defines the parameters for the ICC parametric curve type 3, as
     * defined in ICC.1:2004-10, section 10.15.</p>
     *
     * <p>The EOTF is of the form:</p>
     *
     * \(\begin{equation}
     * Y = \begin{cases}c X & X \lt d \\\
     * \left( a X + b \right) ^{g} & X \ge d \end{cases}
     * \end{equation}\)
     *
     * <p>This constructor is equivalent to setting  \(e\) and \(f\) to 0.</p>
     *
     * @param a The value of \(a\) in the equation of the EOTF described above
     * @param b The value of \(b\) in the equation of the EOTF described above
     * @param c The value of \(c\) in the equation of the EOTF described above
     * @param d The value of \(d\) in the equation of the EOTF described above
     * @param g The value of \(g\) in the equation of the EOTF described above
     */
    /**
     * 定义 ICC 参数曲线类型 3 的参数（简化版本，e 和 f 为 0）。
     * 定义传递函数的简化参数。
     *
     * EOTF 的形式为：
     * Y = {c X          (X < d)
     *      (a X + b)^g  (X >= d)}
     *
     * 此构造函数等效于将 \(e\) 和 \(f\) 设置为 0。
     *
     * @param a EOTF 方程中 \(a\) 的值
     * @param b EOTF 方程中 \(b\) 的值
     * @param c EOTF 方程中 \(c\) 的值
     * @param d EOTF 方程中 \(d\) 的值（阈值）
     * @param g EOTF 方程中 \(g\) 的值（gamma）
     */
    constexpr TransferFunction(
            double a,
            double b,
            double c,
            double d,
            double g
    ) : TransferFunction(a, b, c, d, 0.0, 0.0, g) {
    }

    /**
     * 比较两个 TransferFunction 是否相等
     * @param rhs 要比较的另一个 TransferFunction
     * @return 如果所有参数都相等则返回 true
     */
    bool operator==(const TransferFunction& rhs) const noexcept {
        return
                a == rhs.a &&
                b == rhs.b &&
                c == rhs.c &&
                d == rhs.d &&
                e == rhs.e &&
                f == rhs.f &&
                g == rhs.g;
    }

    double a;  //!< 传递函数的参数 a
    double b;  //!< 传递函数的参数 b
    double c;  //!< 传递函数的参数 c
    double d;  //!< 传递函数的参数 d（阈值）
    double e;  //!< 传递函数的参数 e
    double f;  //!< 传递函数的参数 f
    double g;  //!< 传递函数的参数 g（gamma）
};

/**
 * <p>A color space in Filament is always an RGB color space. A specific RGB color space
 * is defined by the following properties:</p>
 * <ul>
 *     <li>Three chromaticities of the red, green and blue primaries, which
 *     define the gamut of the color space.</li>
 *     <li>A white point chromaticity that defines the stimulus to which
 *     color space values are normalized (also just called "white").</li>
 *     <li>An opto-electronic transfer function, also called opto-electronic
 *     conversion function or often, and approximately, gamma function.</li>
 *     <li>An electro-optical transfer function, also called electo-optical
 *     conversion function or often, and approximately, gamma function.</li>
 * </ul>
 *
 * <h3>Primaries and white point chromaticities</h3>
 * <p>In this implementation, the chromaticity of the primaries and the white
 * point of an RGB color space is defined in the CIE xyY color space. This
 * color space separates the chromaticity of a color, the x and y components,
 * and its luminance, the Y component. Since the primaries and the white
 * point have full brightness, the Y component is assumed to be 1 and only
 * the x and y components are needed to encode them.</p>
 *
 * <h3>Transfer functions</h3>
 * <p>A transfer function is a color component conversion function, defined as
 * a single variable, monotonic mathematical function. It is applied to each
 * individual component of a color. They are used to perform the mapping
 * between linear tristimulus values and non-linear electronic signal value.</p>
 * <p>The <em>opto-electronic transfer function</em> (OETF or OECF) encodes
 * tristimulus values in a scene to a non-linear electronic signal value.</p>
 */
/**
 * Filament 中的色彩空间始终是 RGB 色彩空间。特定的 RGB 色彩空间
 * 由以下属性定义：
 * - 红、绿、蓝基色的三个色度坐标，它们
 *   定义色彩空间的色域。
 * - 白点色度坐标，定义色彩空间值
 *   归一化到的刺激（也称为"白点"）。
 * - 光电传递函数（OETF），也称为光电
 *   转换函数，通常近似为 gamma 函数。
 * - 电光传递函数（EOTF），也称为电光
 *   转换函数，通常近似为 gamma 函数。
 *
 * 基色和白点色度
 * ======================
 * 在此实现中，RGB 色彩空间的基色和白点
 * 的色度在 CIE xyY 色彩空间中定义。此
 * 色彩空间将颜色的色度（x 和 y 分量）
 * 与其亮度（Y 分量）分开。由于基色和白点
 * 具有全亮度，因此 Y 分量假设为 1，只需要
 * x 和 y 分量来编码它们。
 *
 * 传递函数
 * ==========
 * 传递函数是颜色分量转换函数，定义为
 * 单变量单调数学函数。它应用于每个
 * 颜色的各个分量。它们用于执行
 * 线性三刺激值和非线性电子信号值之间的映射。
 * 光电传递函数（OETF 或 OECF）将
 * 场景中的三刺激值编码为非线性的电子信号值。
 */
class ColorSpace {
public:
    /**
     * 构造一个 ColorSpace
     * @param primaries 基色色度坐标
     * @param transferFunction 传递函数
     * @param whitePoint 白点色度坐标
     */
    constexpr ColorSpace(
            const Primaries primaries,
            const TransferFunction transferFunction,
            const WhitePoint whitePoint
    ) : mPrimaries(primaries),
        mTransferFunction(transferFunction),
        mWhitePoint(whitePoint) {
    }

    /**
     * 比较两个 ColorSpace 是否相等
     * @param rhs 要比较的另一个 ColorSpace
     * @return 如果基色、传递函数和白点都相等则返回 true
     */
    bool operator==(const ColorSpace& rhs) const noexcept {
        return mPrimaries == rhs.mPrimaries &&
                mTransferFunction == rhs.mTransferFunction &&
                mWhitePoint == rhs.mWhitePoint;
    }

    /**
     * 获取基色色度坐标
     */
    constexpr const Primaries& getPrimaries() const { return mPrimaries; }
    /**
     * 获取传递函数
     */
    constexpr const TransferFunction& getTransferFunction() const { return mTransferFunction; }
    /**
     * 获取白点色度坐标
     */
    constexpr const WhitePoint& getWhitePoint() const { return mWhitePoint; }

private:
    Primaries mPrimaries;
    TransferFunction mTransferFunction;
    WhitePoint mWhitePoint;
};

/**
 * Intermediate class used when building a color space using the "-" syntax:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * // Declares a "linear sRGB" color space.
 * ColorSpace myColorSpace = Rec709-Linear-D65;
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/**
 * 使用 "-" 语法构建色彩空间时使用的中间类：
 *
 * // 声明一个"线性 sRGB"色彩空间。
 * ColorSpace myColorSpace = Rec709-Linear-D65;
 */
class PartialColorSpace {
public:
    /**
     * 使用 "-" 操作符指定白点以完成 ColorSpace 的构建
     * @param whitePoint 白点色度坐标
     * @return 完整的 ColorSpace 对象
     */
    constexpr ColorSpace operator-(const WhitePoint& whitePoint) const {
        return { mPrimaries, mTransferFunction, whitePoint };
    }

private:
    constexpr PartialColorSpace(
            const Primaries primaries,
            const TransferFunction transferFunction
    ) : mPrimaries(primaries),
        mTransferFunction(transferFunction) {
    }

    Primaries mPrimaries;
    TransferFunction mTransferFunction;

    friend class Gamut;
};

/**
 * Defines the chromaticities of the primaries for a color space. The chromaticities
 * are expressed as three pairs of xy coordinates (in xyY) for the red, green, and blue
 * chromaticities.
 */
/**
 * 定义色彩空间的基色色度坐标。色度
 * 表示为红、绿、蓝
 * 色度的三对 xy 坐标（在 xyY 中）。
 */
class Gamut {
public:
    /**
     * 使用 Primaries 构造 Gamut
     * @param primaries 基色色度坐标
     */
    constexpr explicit Gamut(const Primaries primaries) : mPrimaries(primaries) {
    }

    /**
     * 使用三个基色的 xy 坐标构造 Gamut
     *
     * @param r 红色基色的 xy 坐标
     * @param g 绿色基色的 xy 坐标
     * @param b 蓝色基色的 xy 坐标
     */
    constexpr Gamut(float2 r, float2 g, float2 b) : Gamut(Primaries{ r, g, b }) {
    }

    /**
     * 使用 "-" 操作符指定传递函数
     *
     * @param transferFunction 传递函数
     * @return PartialColorSpace 对象，可用于进一步指定白点
     */
    constexpr PartialColorSpace operator-(const TransferFunction& transferFunction) const {
        return { mPrimaries, transferFunction };
    }

    /**
     * 获取基色色度坐标
     */
    constexpr const Primaries& getPrimaries() const { return mPrimaries; }

private:
    Primaries mPrimaries;
};

//! Rec.709 color gamut, used in the sRGB and DisplayP3 color spaces.
/**
 * Rec.709 色域，用于 sRGB 和 DisplayP3 色彩空间。
 */
constexpr Gamut Rec709 = {{ 0.640f, 0.330f },
                          { 0.300f, 0.600f },
                          { 0.150f, 0.060f }};

//! Linear transfer function.
/**
 * 线性传递函数。
 */
constexpr TransferFunction Linear = { 1.0, 0.0, 0.0, 0.0, 1.0 };

//! sRGB transfer function.
/**
 * sRGB 传递函数。
 */
constexpr TransferFunction sRGB = { 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045, 2.4 };

//! Standard CIE 1931 2° illuminant D65. This illuminant has a color temperature of 6504K.
/**
 * 标准 CIE 1931 2° 光源 D65。此光源的色温为 6504K。
 */
constexpr WhitePoint D65 = { 0.31271f, 0.32902f };

} // namespace filament::color

#endif // TNT_FILAMENT_COLOR_SPACE_H
