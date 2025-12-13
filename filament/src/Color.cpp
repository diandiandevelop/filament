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

#include <filament/Color.h>

#include "ColorSpaceUtils.h"

#include <math/mat3.h>
#include <math/scalar.h>

namespace filament {

using namespace math;

/**
 * sRGB 到线性转换
 * 
 * 将 sRGB 非线性值（用于显示）转换为线性值（用于计算）。
 * 
 * @param color sRGB 颜色值
 * @return 线性颜色值
 */
float3 Color::sRGBToLinear(float3 const color) noexcept {
    /**
     * sRGB 到线性转换
     * 
     * 将 sRGB 非线性值（用于显示）转换为线性值（用于计算）。
     * 
     * sRGB 使用 gamma 编码（约 2.2），需要解码为线性空间才能进行正确的光照计算。
     * 
     * EOTF (Electro-Optical Transfer Function) 是电光转换函数，
     * 用于将编码后的信号转换为线性光值。
     * 
     * @param color sRGB 颜色值（范围 [0, 1]）
     * @return 线性颜色值（范围 [0, 1]）
     * 
     * 实现：调用 EOTF_sRGB 函数进行转换
     */
    return EOTF_sRGB(color);
}

/**
 * 线性到 sRGB 转换
 * 
 * 将线性值（用于计算）转换为 sRGB 非线性值（用于显示）。
 * 
 * 线性颜色值需要编码为 sRGB 格式才能在标准显示器上正确显示。
     * 
     * OETF (Opto-Electronic Transfer Function) 是光电转换函数，
     * 用于将线性光值转换为编码后的信号。
     * 
     * @param color 线性颜色值（范围 [0, 1]）
     * @return sRGB 颜色值（范围 [0, 1]）
     * 
     * 实现：调用 OETF_sRGB 函数进行转换
     */
float3 Color::linearToSRGB(float3 const color) noexcept {
    return OETF_sRGB(color);
}

/**
 * 色温到颜色转换（CCT - Correlated Color Temperature）
 * 
 * 根据色温（开尔文）计算对应的颜色。
 * 使用 CIE 1960 色度坐标系统。
 * 
 * @param K 色温（开尔文）
 * @return 线性颜色值（归一化并饱和）
 */
LinearColor Color::cct(float const K) {
    /**
     * 色温到 CIE 1960 色度坐标转换
     * 
     * 使用多项式近似计算 u 和 v 色度坐标。
     * 这些公式基于 CIE 1960 UCS（均匀色度标度）系统。
     * 
     * 变量说明：
     * - K: 色温（开尔文），例如：2000K（暖白），6500K（日光），10000K（冷白）
     * - K2: 色温的平方，用于多项式计算
     * - u, v: CIE 1960 UCS 色度坐标
     */
    float const K2 = K * K;  // 色温的平方，用于多项式计算
    
    /**
     * 计算 u 色度坐标
     * 
     * 使用有理多项式近似：
     * u = (a0 + a1*K + a2*K²) / (b0 + b1*K + b2*K²)
     * 
     * 系数基于经验数据拟合。
     */
    float const u = (0.860117757f + 1.54118254e-4f * K + 1.28641212e-7f * K2) /
               (1.0f + 8.42420235e-4f * K + 7.08145163e-7f * K2);
    
    /**
     * 计算 v 色度坐标
     * 
     * 使用有理多项式近似：
     * v = (c0 + c1*K + c2*K²) / (d0 + d1*K + d2*K²)
     * 
     * 系数基于经验数据拟合。
     */
    float const v = (0.317398726f + 4.22806245e-5f * K + 4.20481691e-8f * K2) /
               (1.0f - 2.89741816e-5f * K + 1.61456053e-7f * K2);

    /**
     * 转换为 xyY 色度坐标
     * 
     * 从 CIE 1960 UCS (u, v) 转换为 CIE 1931 xyY：
     * - x = 3u / (2u - 8v + 4)
     * - y = 2v / (2u - 8v + 4)
     * - Y = 1.0（假设亮度为 1）
     * 
     * d 是分母的倒数，用于优化计算。
     */
    float const d = 1.0f / (2.0f * u - 8.0f * v + 4.0f);
    
    /**
     * 从 xyY 转换为 XYZ，然后转换为线性 sRGB
     * 
     * 转换链：xyY -> XYZ -> sRGB
     * - xyY_to_XYZ: 将 xyY 色度坐标转换为 XYZ 三刺激值
     * - XYZ_to_sRGB: 将 XYZ 转换为线性 sRGB 颜色空间
     */
    float3 const linear = XYZ_to_sRGB * xyY_to_XYZ({3.0f * u * d, 2.0f * v * d, 1.0f});
    
    /**
     * 归一化并饱和
     * 
     * 由于色温转换可能产生超出 [0, 1] 范围的值，需要：
     * 1. 找到最大分量值（避免除零）
     * 2. 除以最大值进行归一化
     * 3. 使用 saturate 将值限制在 [0, 1] 范围内
     */
    return saturate(linear / max(1e-5f, max(linear)));
}

/**
 * D 系列光源
 * 
 * 根据色温计算 D 系列光源（标准日光光源）的颜色。
 * 
 * @param K 色温（开尔文）
 * @return 线性颜色值（归一化并饱和）
 * 
 * 实现细节：
 * - 使用不同的多项式系数，取决于色温是否 <= 7000K
 * - y 坐标使用 D 系列光源的公式计算
 */
LinearColor Color::illuminantD(float const K) {
    /**
     * 色温到 xyY 转换（D 系列光源）
     * 
     * D 系列光源是 CIE 定义的标准日光光源，用于模拟不同时间的自然光。
     * 例如：D50（5000K，中午阳光），D65（6500K，标准日光），D75（7500K，北窗光）。
     * 
     * 变量说明：
     * - K: 色温（开尔文）
     * - iK: 1/K，色温的倒数，用于多项式计算
     * - iK2: iK 的平方
     * - x, y: CIE 1931 xyY 色度坐标
     */
    const float iK = 1.0f / K;      // 色温的倒数
    float const iK2 = iK * iK;      // 色温倒数的平方
    
    /**
     * 根据色温范围选择不同的系数计算 x 坐标
     * 
     * D 系列光源的 x 坐标计算公式：
     * - 当 K <= 7000K 时，使用一组系数
     * - 当 K > 7000K 时，使用另一组系数
     * 
     * 公式：x = a0 + a1*(1/K) + a2*(1/K)² + a3*(1/K)³
     * 
     * 这些系数基于 CIE 标准数据拟合。
     */
    float const x = K <= 7000.0f ?
            // 低色温范围（<= 7000K）的系数
            0.244063f + 0.09911e3f * iK + 2.9678e6f * iK2 - 4.6070e9f * iK2 * iK :
            // 高色温范围（> 7000K）的系数
            0.237040f + 0.24748e3f * iK + 1.9018e6f * iK2 - 2.0064e9f * iK2 * iK;
    
    /**
     * 使用 D 系列光源公式计算 y 坐标
     * 
     * D 系列光源的 y 坐标与 x 坐标有固定的关系：
     * y = -3x² + 2.87x - 0.275
     * 
     * 这个公式确保 (x, y) 坐标位于 D 系列光源的色度轨迹上。
     */
    float const y = -3.0f * x * x + 2.87f * x - 0.275f;

    /**
     * 从 xyY 转换为 XYZ，然后转换为线性 sRGB
     * 
     * 转换链：xyY -> XYZ -> sRGB
     * - xyY_to_XYZ: 将 xyY 色度坐标转换为 XYZ 三刺激值
     * - XYZ_to_sRGB: 将 XYZ 转换为线性 sRGB 颜色空间
     */
    float3 const linear = XYZ_to_sRGB * xyY_to_XYZ({x, y, 1.0f});
    
    /**
     * 归一化并饱和
     * 
     * 由于色温转换可能产生超出 [0, 1] 范围的值，需要：
     * 1. 找到最大分量值（避免除零，使用 1e-5 作为最小值）
     * 2. 除以最大值进行归一化
     * 3. 使用 saturate 将值限制在 [0, 1] 范围内
     */
    return saturate(linear / max(1e-5f, max(linear)));
}

/**
 * 距离处的吸收
 * 
 * 计算颜色在给定距离处的吸收系数。
 * 用于体积渲染和大气散射等效果。
 * 
 * @param color 颜色值
 * @param distance 距离
 * @return 吸收系数
 * 
 * 公式：absorption = -log(color) / distance
 */
LinearColor Color::absorptionAtDistance(LinearColor const& color, float const distance) {
    /**
     * 距离处的吸收
     * 
     * 计算颜色在给定距离处的吸收系数。
     * 用于体积渲染和大气散射等效果。
     * 
     * 物理原理：
     * 当光线穿过介质时，会被吸收。根据 Beer-Lambert 定律：
     * I(d) = I(0) * e^(-σ * d)
     * 
     * 其中：
     * - I(0): 初始强度（颜色值）
     * - I(d): 距离 d 处的强度
     * - σ: 吸收系数
     * - d: 距离
     * 
     * 从 I(d) = I(0) * e^(-σ * d) 可以推导出：
     * σ = -ln(I(d) / I(0)) / d
     * 
     * 如果 I(d) 是已知的（即 color 参数），I(0) = 1.0，则：
     * σ = -ln(color) / distance
     * 
     * @param color 距离 d 处的颜色值（范围 [0, 1]）
     *              值越小，表示吸收越多
     * @param distance 距离（单位：米）
     *                 必须大于 0
     * @return 吸收系数（每米的吸收率）
     * 
     * 实现细节：
     * 1. clamp(color, 1e-5f, 1.0f): 将颜色值限制在 [1e-5, 1.0] 范围内
     *    - 最小值 1e-5 避免 log(0) 导致无穷大
     *    - 最大值 1.0 是颜色的上限
     * 2. -log(...): 计算负对数，得到吸收系数
     * 3. / max(1e-5f, distance): 除以距离，避免除零
     */
    return -log(clamp(color, 1e-5f, 1.0f)) / max(1e-5f, distance);
}

} // namespace filament
