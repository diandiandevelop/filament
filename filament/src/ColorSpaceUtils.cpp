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

#include "ColorSpaceUtils.h"

namespace filament {

using namespace math;

/**
 * 色域裁剪实现
 * 
 * 以下部分遵循以下许可证：
 * 来源：https://bottosson.github.io/posts/gamutclipping/
 * 
 * Copyright (c) 2021 Björn Ottosson
 * 
 * 许可条款：允许自由使用、修改、分发等。
 */

/**
 * TODO: 用来自以下链接的解决方案替换 compute_max_saturation()：
 *       https://simonstechblog.blogspot.com/2021/06/implementing-gamut-mapping.html
 *       以支持任意输出色域。下面的解决方案仅适用于 sRGB/Rec.709
 */

/**
 * 计算给定色调在 sRGB 色域内的最大饱和度
 * 
 * 饱和度定义为 S = C/L（色度/亮度）。
 * 
 * @param a OkLab 颜色空间的 a 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @param b OkLab 颜色空间的 b 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @return 最大饱和度
 * 
 * 实现细节：
 * - 最大饱和度出现在 r、g 或 b 之一降为零时
 * - 根据哪个分量先降为零选择不同的系数
 * - 使用多项式近似，然后用 Halley 方法进行一步迭代以提高精度
 */
static float compute_max_saturation(float const a, float const b) noexcept {
    /**
     * 最大饱和度出现在 r、g 或 b 之一降为零时。
     * 
     * 根据哪个分量先降为零选择不同的系数。
     */
    float k0, k1, k2, k3, k4, wl, wm, ws;

    if (-1.88170328f * a - 0.80936493f * b > 1) {
        /**
         * 红色分量先降为零
         */
        k0 = +1.19086277f;
        k1 = +1.76576728f;
        k2 = +0.59662641f;
        k3 = +0.75515197f;
        k4 = +0.56771245f;
        wl = +4.0767416621f;
        wm = -3.3077115913f;
        ws = +0.2309699292f;
    } else if (1.81444104f * a - 1.19445276f * b > 1) {
        /**
         * 绿色分量先降为零
         */
        k0 = +0.73956515f;
        k1 = -0.45954404f;
        k2 = +0.08285427f;
        k3 = +0.12541070f;
        k4 = +0.14503204f;
        wl = -1.2681437731f;
        wm = +2.6097574011f;
        ws = -0.3413193965f;
    } else {
        /**
         * 蓝色分量先降为零
         */
        k0 = +1.35733652f;
        k1 = -0.00915799f;
        k2 = -1.15130210f;
        k3 = -0.50559606f;
        k4 = +0.00692167f;
        wl = -0.0041960863f;
        wm = -0.7034186147f;
        ws = +1.7076147010f;
    }

    /**
     * 使用多项式近似最大饱和度
     */
    float S = k0 + k1 * a + k2 * b + k3 * a * a + k4 * a * b;

    /**
     * 使用 Halley 方法进行一步迭代以提高精度
     * 
     * 这给出的误差小于 10e-6，除了某些蓝色色调（dS/dh 接近无穷大）。
     * 对于大多数应用来说这应该足够了，否则可以进行 2-3 步迭代。
     */

    float const k_l = +0.3963377774f * a + 0.2158037573f * b;
    float const k_m = -0.1055613458f * a - 0.0638541728f * b;
    float const k_s = -0.0894841775f * a - 1.2914855480f * b;

    {
        float const l_ = 1.f + S * k_l;
        float const m_ = 1.f + S * k_m;
        float const s_ = 1.f + S * k_s;

        float const l = l_ * l_ * l_;
        float const m = m_ * m_ * m_;
        float const s = s_ * s_ * s_;

        float const l_dS = 3.f * k_l * l_ * l_;
        float const m_dS = 3.f * k_m * m_ * m_;
        float const s_dS = 3.f * k_s * s_ * s_;

        float const l_dS2 = 6.f * k_l * k_l * l_;
        float const m_dS2 = 6.f * k_m * k_m * m_;
        float const s_dS2 = 6.f * k_s * k_s * s_;

        float const f = wl * l + wm * m + ws * s;
        float const f1 = wl * l_dS + wm * m_dS + ws * s_dS;
        float const f2 = wl * l_dS2 + wm * m_dS2 + ws * s_dS2;

        S = S - f * f1 / (f1 * f1 - 0.5f * f * f2);
    }

    return S;
}

/**
 * 查找给定色调的 L_cusp 和 C_cusp
 * 
 * cusp 是色域三角形的顶点，表示该色调在色域边界上的点。
 * 
 * @param a OkLab 颜色空间的 a 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @param b OkLab 颜色空间的 b 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @return (L_cusp, C_cusp) 对
 */
static float2 find_cusp(float const a, float const b) noexcept {
    /**
     * 首先，找到最大饱和度（饱和度 S = C/L）
     */
    float const S_cusp = compute_max_saturation(a, b);

    /**
     * 转换为线性 sRGB，找到至少一个 r、g 或 b >= 1 的第一个点
     */
    float3 const rgb_at_max = OkLab_to_sRGB({1.0f, S_cusp * a, S_cusp * b});
    float const L_cusp = std::cbrt(1.0f / max(rgb_at_max));
    float const C_cusp = L_cusp * S_cusp;

    return { L_cusp, C_cusp };
}

/**
 * 查找由以下直线定义的色域交点
 * 
 * L = L0 * (1 - t) + t * L1;
 * C = t * C1;
 * 
 * @param a OkLab 颜色空间的 a 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @param b OkLab 颜色空间的 b 分量（必须归一化，使得 a^2 + b^2 == 1）
 * @param L1 目标亮度
 * @param C1 目标色度
 * @param L0 起始亮度
 * @return 交点参数 t
 */
static float find_gamut_intersection(float const a, float const b, float const L1, float const C1, float const L0) noexcept {
    /**
     * 查找色域三角形的 cusp（顶点）
     */
    float2 const cusp = find_cusp(a, b);

    /**
     * 分别查找上半部分和下半部分的交点
     */
    float t;
    if (((L1 - L0) * cusp.y - (cusp.x - L0) * C1) <= 0.f) {
        /**
         * 下半部分
         */
        t = cusp.y * L0 / (C1 * cusp.x + cusp.y * (L0 - L1));
    } else {
        /**
         * 上半部分
         */

        /**
         * 首先与三角形相交
         */
        t = cusp.y * (L0 - 1.f) / (C1 * (cusp.x - 1.f) + cusp.y * (L0 - L1));

        /**
         * 然后使用 Halley 方法进行一步迭代
         */
        {
            float const dL = L1 - L0;
            float const dC = C1;

            float const k_l = +0.3963377774f * a + 0.2158037573f * b;
            float const k_m = -0.1055613458f * a - 0.0638541728f * b;
            float const k_s = -0.0894841775f * a - 1.2914855480f * b;

            float const l_dt = dL + dC * k_l;
            float const m_dt = dL + dC * k_m;
            float const s_dt = dL + dC * k_s;


            // If higher accuracy is required, 2 or 3 iterations of the
            // following block can be used:
            {
                float const L = L0 * (1.f - t) + t * L1;
                float const C = t * C1;

                float const l_ = L + C * k_l;
                float const m_ = L + C * k_m;
                float const s_ = L + C * k_s;

                float const l = l_ * l_ * l_;
                float const m = m_ * m_ * m_;
                float const s = s_ * s_ * s_;

                float const ldt = 3 * l_dt * l_ * l_;
                float const mdt = 3 * m_dt * m_ * m_;
                float const sdt = 3 * s_dt * s_ * s_;

                float const ldt2 = 6 * l_dt * l_dt * l_;
                float const mdt2 = 6 * m_dt * m_dt * m_;
                float const sdt2 = 6 * s_dt * s_dt * s_;

                float const r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s - 1;
                float const r1 = 4.0767416621f * ldt - 3.3077115913f * mdt + 0.2309699292f * sdt;
                float const r2 = 4.0767416621f * ldt2 - 3.3077115913f * mdt2 + 0.2309699292f * sdt2;

                float const u_r = r1 / (r1 * r1 - 0.5f * r * r2);
                float t_r = -r * u_r;

                float const g = -1.2681437731f * l + 2.6097574011f * m - 0.3413193965f * s - 1;
                float const g1 = -1.2681437731f * ldt + 2.6097574011f * mdt - 0.3413193965f * sdt;
                float const g2 = -1.2681437731f * ldt2 + 2.6097574011f * mdt2 - 0.3413193965f * sdt2;

                float const u_g = g1 / (g1 * g1 - 0.5f * g * g2);
                float t_g = -g * u_g;

                float const b0 = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s - 1;
                float const b1 = -0.0041960863f * ldt - 0.7034186147f * mdt + 1.7076147010f * sdt;
                float const b2 = -0.0041960863f * ldt2 - 0.7034186147f * mdt2 + 1.7076147010f * sdt2;

                float const u_b = b1 / (b1 * b1 - 0.5f * b0 * b2);
                float t_b = -b0 * u_b;

                t_r = u_r >= 0.f ? t_r : std::numeric_limits<float>::max();
                t_g = u_g >= 0.f ? t_g : std::numeric_limits<float>::max();
                t_b = u_b >= 0.f ? t_b : std::numeric_limits<float>::max();

                t += min(t_r, min(t_g, t_b));
            }
        }
    }

    return t;
}

/**
 * 符号函数
 * 
 * 返回 x 的符号：正数返回 1，负数返回 -1，零返回 0。
 * 
 * @param x 输入值
 * @return 符号（1、-1 或 0）
 */
constexpr float sgn(float const x) noexcept {
    return (float) (0.f < x) - (float) (x < 0.f);
}

/**
 * 自适应 L0 = 0.5 的色域裁剪
 * 
 * 使用自适应 L0 = 0.5，alpha 默认设置为 0.05。
 * 
 * threshold 参数定义了 1.0 以上和 0.0 以下的灵活范围，
 * 在此范围内的超出色域值仍被视为在色域内。
 * 这有助于控制先前颜色分级步骤中的不准确性，
 * 这些步骤可能在不应该偏离色域值时略微偏离。
 * 
 * @param rgb RGB 颜色值
 * @param alpha 自适应参数（默认 0.05）
 * @param threshold 阈值参数（默认 0.03）
 * @return 裁剪后的 RGB 颜色值
 */
inline float3 gamut_clip_adaptive_L0_0_5(float3 rgb,
        float alpha = 0.05f, float threshold = 0.03f) noexcept {

    if (all(lessThanEqual(rgb, float3{1.0f + threshold})) &&
            all(greaterThanEqual(rgb, float3{-threshold}))) {
        return rgb;
    }

    float3 const lab = sRGB_to_OkLab(rgb);

    float const L = lab.x;
    float const eps = 0.00001f;
    float const C = max(eps, std::sqrt(lab.y * lab.y + lab.z * lab.z));
    float const a_ = lab.y / C;
    float const b_ = lab.z / C;

    float const Ld = L - 0.5f;
    float const e1 = 0.5f + std::abs(Ld) + alpha * C;
    float const L0 = 0.5f * (1.0f + sgn(Ld) * (e1 - std::sqrt(e1 * e1 - 2.0f * std::abs(Ld))));

    float const t = find_gamut_intersection(a_, b_, L, C, L0);
    float const L_clipped = L0 * (1.f - t) + t * L;
    float const C_clipped = t * C;

    return OkLab_to_sRGB({L_clipped, C_clipped * a_, C_clipped * b_});
}

/**
 * sRGB 色域映射
 * 
 * 将颜色映射到 sRGB 色域内，使用自适应 L0 = 0.5 的色域裁剪算法。
 * 
 * @param rgb RGB 颜色值
 * @return 映射后的 RGB 颜色值（在 sRGB 色域内）
 */
float3 gamutMapping_sRGB(float3 const rgb) noexcept {
    return gamut_clip_adaptive_L0_0_5(rgb);
}

/**
 * 结束来源：https://bottosson.github.io/posts/gamutclipping/
 */

} //namespace filament
