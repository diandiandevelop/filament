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

#include <ibl/CubemapSH.h>

#include <ibl/Cubemap.h>
#include <ibl/CubemapUtils.h>
#include <ibl/utilities.h>

#include "CubemapUtilsImpl.h"

#include <utils/Log.h>
#include <utils/JobSystem.h>

#include <math/mat4.h>

#include <array>
#include <limits>
#include <iomanip>

using namespace filament::math;
using namespace utils;

namespace filament {
namespace ibl {

// -----------------------------------------------------------------------------------------------
// A few useful utilities
// -----------------------------------------------------------------------------------------------

/**
 * 计算阶乘比 n! / d!
 * 
 * 用于计算球谐函数中的归一化因子。
 * 
 * @param n 分子阶乘的参数
 * @param d 分母阶乘的参数（默认为1）
 * @return n! / d! 的结果
 */
static constexpr float factorial(size_t n, size_t d = 1) {
   d = std::max(size_t(1), d);
   n = std::max(size_t(1), n);
   float r = 1.0;
   if (n == d) {
       // intentionally left blank
       // 如果n == d，结果为1
   } else if (n > d) {
       // 计算 n * (n-1) * ... * (d+1)
       for ( ; n>d ; n--) {
           r *= n;
       }
   } else {
       // 计算 d * (d-1) * ... * (n+1)，然后取倒数
       for ( ; d>n ; d--) {
           r *= d;
       }
       r = 1.0f / r;
   }
   return r;
}

// -----------------------------------------------------------------------------------------------

/**
 * 计算球谐函数缩放因子 K(m,l) 实现
 * 
 * 返回 sqrt((2*l + 1) / 4*pi) * sqrt( (l-|m|)! / (l+|m|)! )
 * 
 * 这是球谐函数归一化因子，用于确保球谐基函数的正交性。
 * 
 * @param m 球谐函数的m参数（方位角阶数）
 * @param l 球谐函数的l参数（球面阶数）
 * @return 缩放因子
 */
float CubemapSH::Kml(ssize_t m, size_t l) {
    m = m < 0 ? -m : m;  // abs() is not constexpr
    // 计算 (2*l + 1) * (l-|m|)! / (l+|m|)!
    const float K = (2 * l + 1) * factorial(size_t(l - m), size_t(l + m));
    // 返回 sqrt(K) / sqrt(4*pi) = sqrt(K) * sqrt(1/(4*pi))
    return std::sqrt(K) * (F_2_SQRTPI * 0.25);
}

/**
 * 计算所有球谐系数的缩放因子实现
 * 
 * 执行步骤：
 * 1. 为每个球谐系数创建缩放因子数组
 * 2. 对于m=0的系数，直接使用Kml(0, l)
 * 3. 对于m!=0的系数，使用F_SQRT2 * Kml(m, l)（因为cos和sin项需要额外的sqrt(2)因子）
 * 
 * @param numBands 球谐函数的频带数
 * @return 缩放因子数组（大小为numBands * numBands）
 */
std::vector<float> CubemapSH::Ki(size_t numBands) {
    const size_t numCoefs = numBands * numBands;
    std::vector<float> K(numCoefs);
    for (size_t l = 0; l < numBands; l++) {
        // m=0的系数
        K[SHindex(0, l)] = Kml(0, l);
        // m!=0的系数（正负m共享相同的缩放因子）
        for (size_t m = 1; m <= l; m++) {
            K[SHindex(m, l)] =
            K[SHindex(-m, l)] = F_SQRT2 * Kml(m, l);
        }
    }
    return K;
}

/**
 * 计算截断余弦函数的球谐系数实现
 * 
 * 计算 < cos(theta) > 的球谐系数，预乘以 1 / K(0,l)。
 * 
 * 用于计算环境光照的漫反射部分。
 * 
 * @param l 球谐函数的l参数（球面阶数）
 * @return 截断余弦的球谐系数
 */
constexpr float CubemapSH::computeTruncatedCosSh(size_t l) {
    if (l == 0) {
        return F_PI;
    } else if (l == 1) {
        return 2 * F_PI / 3;
    } else if (l & 1u) {
        // 奇数l的系数为0
        return 0;
    }
    // 偶数l的计算
    const size_t l_2 = l / 2;
    float A0 = ((l_2 & 1u) ? 1.0f : -1.0f) / ((l + 2) * (l - 1));
    float A1 = factorial(l, l_2) / (factorial(l_2) * (1 << l));
    return 2 * F_PI * A0 * A1;
}

/**
 * 计算非归一化球谐基函数实现
 * 
 * 计算球谐基函数的值：
 *  - m > 0: cos(m*phi) * P(m,l)
 *  - m < 0: sin(|m|*phi) * P(|m|,l)
 *  - m = 0: P(0,l)
 * 
 * 执行步骤：
 * 1. 使用递归计算关联勒让德多项式 P(m,l)
 * 2. 使用递归计算 cos(m*phi) 和 sin(m*phi)
 * 3. 组合得到球谐基函数值
 * 
 * 注意：为了数值稳定性，计算过程中移除了 sin(theta)^|m| 因子，
 * 然后在最后通过 cos(m*phi) 和 sin(m*phi) 递归进行修正。
 * 
 * @param SHb 输出的球谐基函数值数组（会被修改）
 * @param numBands 球谐函数的频带数
 * @param s 归一化的3D方向向量 (x, y, z) = (sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta))
 */
void CubemapSH::computeShBasis(
        float* UTILS_RESTRICT SHb,
        size_t numBands,
        const float3& s)
{
#if 0
    // Reference implementation
    float phi = atan2(s.x, s.y);
    for (size_t l = 0; l < numBands; l++) {
        SHb[SHindex(0, l)] = Legendre(l, 0, s.z);
        for (size_t m = 1; m <= l; m++) {
            float p = Legendre(l, m, s.z);
            SHb[SHindex(-m, l)] = std::sin(m * phi) * p;
            SHb[SHindex( m, l)] = std::cos(m * phi) * p;
        }
    }
#endif

    /*
     * TODO: all the Legendre computation below is identical for all faces, so it
     * might make sense to pre-compute it once. Also note that there is
     * a fair amount of symmetry within a face (which we could take advantage of
     * to reduce the pre-compute table).
     */

    /*
     * Below, we compute the associated Legendre polynomials using recursion.
     * see: http://mathworld.wolfram.com/AssociatedLegendrePolynomial.html
     *
     * Note [0]: s.z == cos(theta) ==> we only need to compute P(s.z)
     *
     * Note [1]: We in fact compute P(s.z) / sin(theta)^|m|, by removing
     * the "sqrt(1 - s.z*s.z)" [i.e.: sin(theta)] factor from the recursion.
     * This is later corrected in the ( cos(m*phi), sin(m*phi) ) recursion.
     */

    // s = (x, y, z) = (sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta))

    // handle m=0 separately, since it produces only one coefficient
    float Pml_2 = 0;
    float Pml_1 = 1;
    SHb[0] =  Pml_1;
    for (size_t l=1; l<numBands; l++) {
        float Pml = ((2*l-1.0f)*Pml_1*s.z - (l-1.0f)*Pml_2) / l;
        Pml_2 = Pml_1;
        Pml_1 = Pml;
        SHb[SHindex(0, l)] = Pml;
    }
    float Pmm = 1;
    for (size_t m=1 ; m<numBands ; m++) {
        Pmm = (1.0f - 2*m) * Pmm;      // See [1], divide by sqrt(1 - s.z*s.z);
        Pml_2 = Pmm;
        Pml_1 = (2*m + 1.0f)*Pmm*s.z;
        // l == m
        SHb[SHindex(-m, m)] = Pml_2;
        SHb[SHindex( m, m)] = Pml_2;
        if (m+1 < numBands) {
            // l == m+1
            SHb[SHindex(-m, m+1)] = Pml_1;
            SHb[SHindex( m, m+1)] = Pml_1;
            for (size_t l=m+2 ; l<numBands ; l++) {
                float Pml = ((2*l - 1.0f)*Pml_1*s.z - (l + m - 1.0f)*Pml_2) / (l-m);
                Pml_2 = Pml_1;
                Pml_1 = Pml;
                SHb[SHindex(-m, l)] = Pml;
                SHb[SHindex( m, l)] = Pml;
            }
        }
    }

    // At this point, SHb contains the associated Legendre polynomials divided
    // by sin(theta)^|m|. Below we compute the SH basis.
    //
    // ( cos(m*phi), sin(m*phi) ) recursion:
    // cos(m*phi + phi) == cos(m*phi)*cos(phi) - sin(m*phi)*sin(phi)
    // sin(m*phi + phi) == sin(m*phi)*cos(phi) + cos(m*phi)*sin(phi)
    // cos[m+1] == cos[m]*s.x - sin[m]*s.y
    // sin[m+1] == sin[m]*s.x + cos[m]*s.y
    //
    // Note that (d.x, d.y) == (cos(phi), sin(phi)) * sin(theta), so the
    // code below actually evaluates:
    //      (cos((m*phi), sin(m*phi)) * sin(theta)^|m|
    float Cm = s.x;
    float Sm = s.y;
    for (size_t m = 1; m <= numBands; m++) {
        for (size_t l = m; l < numBands; l++) {
            SHb[SHindex(-m, l)] *= Sm;
            SHb[SHindex( m, l)] *= Cm;
        }
        float Cm1 = Cm * s.x - Sm * s.y;
        float Sm1 = Sm * s.x + Cm * s.y;
        Cm = Cm1;
        Sm = Sm1;
    }
}


/**
 * 旋转1阶球谐函数实现
 * 
 * 将1阶球谐函数系数（3个系数）通过旋转矩阵M进行旋转。
 * 用于在环境光照旋转时更新球谐系数。
 * 
 * 执行步骤：
 * 1. 将旋转矩阵M应用到标准基向量N0, N1, N2
 * 2. 将旋转后的基向量投影到球谐空间
 * 3. 使用预计算的逆矩阵变换球谐系数
 * 
 * @param band1 1阶球谐函数系数（3个系数）
 * @param M 旋转矩阵
 * @return 旋转后的1阶球谐函数系数
 */
float3 CubemapSH::rotateShericalHarmonicBand1(float3 band1, mat3f const& M) {

    // inverse() is not constexpr -- so we pre-calculate it in mathematica
    //
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 1, 0 };
    //    constexpr float3 N2{ 0, 0, 1 };
    //
    //    constexpr mat3f A1 = { // this is the projection of N0, N1, N2 to SH space
    //            float3{ -N0.y, N0.z, -N0.x },
    //            float3{ -N1.y, N1.z, -N1.x },
    //            float3{ -N2.y, N2.z, -N2.x }
    //    };
    //
    //    const mat3f invA1 = inverse(A1);

    constexpr mat3f invA1TimesK = {
            float3{ 0, -1,  0 },
            float3{ 0,  0,  1 },
            float3{-1,  0,  0 }
    };

    // below can't be constexpr
    const float3 MN0 = M[0];  // M * N0;
    const float3 MN1 = M[1];  // M * N1;
    const float3 MN2 = M[2];  // M * N2;
    const mat3f R1OverK = {
            float3{ -MN0.y, MN0.z, -MN0.x },
            float3{ -MN1.y, MN1.z, -MN1.x },
            float3{ -MN2.y, MN2.z, -MN2.x }
    };

    return R1OverK * (invA1TimesK * band1);
}

/**
 * 旋转2阶球谐函数实现
 * 
 * 将2阶球谐函数系数（5个系数）通过旋转矩阵M进行旋转。
 * 用于在环境光照旋转时更新球谐系数。
 * 
 * 执行步骤：
 * 1. 将旋转矩阵M应用到5个标准基向量
 * 2. 将旋转后的基向量投影到球谐空间
 * 3. 使用预计算的逆矩阵变换球谐系数
 * 
 * @param band2 2阶球谐函数系数（5个系数）
 * @param M 旋转矩阵
 * @return 旋转后的2阶球谐函数系数
 */
CubemapSH::float5 CubemapSH::rotateShericalHarmonicBand2(float5 const& band2, mat3f const& M) {
    constexpr float M_SQRT_3  = 1.7320508076f;
    constexpr float n = F_SQRT1_2;

    //  Below we precompute (with help of Mathematica):
    //    constexpr float3 N0{ 1, 0, 0 };
    //    constexpr float3 N1{ 0, 0, 1 };
    //    constexpr float3 N2{ n, n, 0 };
    //    constexpr float3 N3{ n, 0, n };
    //    constexpr float3 N4{ 0, n, n };
    //    constexpr float M_SQRT_PI = 1.7724538509f;
    //    constexpr float M_SQRT_15 = 3.8729833462f;
    //    constexpr float k = M_SQRT_15 / (2.0f * M_SQRT_PI);
    //    --> k * inverse(mat5{project(N0), project(N1), project(N2), project(N3), project(N4)})
    constexpr float5 invATimesK[5] = {
            {    0,        1,   2,   0,  0 },
            {   -1,        0,   0,   0, -2 },
            {    0, M_SQRT_3,   0,   0,  0 },
            {    1,        1,   0,  -2,  0 },
            {    2,        1,   0,   0,  0 }
    };

    // This projects a vec3 to SH2/k space (i.e. we premultiply by 1/k)
    // below can't be constexpr
    auto project = [](float3 s) -> float5 {
        return {
                                       (s.y * s.x),
                                     - (s.y * s.z),
                  1 / (2 * M_SQRT_3) * ((3 * s.z * s.z - 1)),
                                     - (s.z * s.x),
                                0.5f * ((s.x * s.x - s.y * s.y))
        };
    };

    // this is: invA * k * band2
    // 5x5 matrix by vec5 (this a lot of zeroes and constants, which the compiler should eliminate)
    const float5 invATimesKTimesBand2 = multiply(invATimesK, band2);

    // this is: mat5{project(N0), project(N1), project(N2), project(N3), project(N4)} / k
    // (the 1/k comes from project(), see above)
    const float5 ROverK[5] = {
            project(M[0]),                  // M * N0
            project(M[2]),                  // M * N1
            project(n * (M[0] + M[1])),     // M * N2
            project(n * (M[0] + M[2])),     // M * N3
            project(n * (M[1] + M[2]))      // M * N4
    };

    // notice how "k" disappears
    // this is: (R / k) * (invA * k) * band2 == R * invA * band2
    const float5 result = multiply(ROverK, invATimesKTimesBand2);

    return result;
}

/**
 * Sinc窗口函数实现
 * 
 * 用于减少球谐函数重建时的"振铃"（ringing）和负值问题。
 * 高动态范围环境贴图（或高频内容）在重建时会出现振铃和负值。
 * 为了缓解这个问题，需要对输入图像进行低通滤波，或者等价地
 * 对球谐系数应用窗口函数，使系数随频带数逐渐衰减到零。
 * 
 * 参考：
 * - "Stupid Spherical Harmonics (SH)"
 * - "Deringing Spherical Harmonics"
 * 作者：Peter-Pike Sloan
 * https://www.ppsloan.org/publications/shdering.pdf
 * 
 * 执行步骤：
 * 1. l=0时返回1（DC分量不受影响）
 * 2. l>=w时返回0（完全衰减）
 * 3. 否则计算sinc窗口值并取4次方（相当于应用4次滤波器）
 * 
 * @param l 球谐函数的l参数（球面阶数）
 * @param w 窗口大小（频带单位）
 * @return 窗口系数
 */
float CubemapSH::sincWindow(size_t l, float w) {
    if (l == 0) {
        return 1.0f;  // DC分量不受影响
    } else if (l >= w) {
        return 0.0f;  // 完全衰减
    }

    // we use a sinc window scaled to the desired window size in bands units
    // a sinc window only has zonal harmonics
    // 我们使用缩放到所需窗口大小的sinc窗口（以频带为单位）
    // sinc窗口只有带状谐波
    float x = (float(F_PI) * l) / w;
    x = std::sin(x) / x;  // sinc函数

    // The convolution of a SH function f and a ZH function h is just the product of both
    // scaled by 1 / K(0,l) -- the window coefficients include this scale factor.
    // SH函数f和ZH函数h的卷积就是两者的乘积，缩放因子为1/K(0,l)
    // ——窗口系数包含此缩放因子

    // Taking the window to power N is equivalent to applying the filter N times
    // 将窗口取N次方等价于应用N次滤波器
    return std::pow(x, 4);
}

/**
 * 对球谐函数应用窗口函数实现
 * 
 * 用于减少球谐函数重建时的振铃和负值问题。
 * 如果cutoff为0，则自动计算合适的截止频带。
 * 
 * 执行步骤：
 * 1. 如果cutoff为0（自动窗口）：
 *    - 对每个颜色通道分别处理
 *    - 使用二分查找找到使最小值为非负的最大截止频带
 * 2. 对所有球谐系数应用sinc窗口函数
 * 
 * @param sh 球谐函数系数数组（会被修改）
 * @param numBands 球谐函数的频带数
 * @param cutoff 截止频带（0表示自动计算）
 */
void CubemapSH::windowSH(std::unique_ptr<float3[]>& sh, size_t numBands, float cutoff) {

    using SH3 = std::array<float, 9>;

    auto rotateSh3Bands = [](SH3 const& sh, mat3f M) -> SH3 {
        SH3 out;
        const float b0 = sh[0];
        const float3 band1{ sh[1], sh[2], sh[3] };
        const float3 b1 = rotateShericalHarmonicBand1(band1, M);
        const float5 band2{ sh[4], sh[5], sh[6], sh[7], sh[8] };
        const float5 b2 = rotateShericalHarmonicBand2(band2, M);
        return { b0, b1[0], b1[1], b1[2], b2[0], b2[1], b2[2], b2[3], b2[4] };
    };

    auto shmin = [rotateSh3Bands](SH3 f) -> float {
        // See "Deringing Spherical Harmonics" by Peter-Pike Sloan
        // https://www.ppsloan.org/publications/shdering.pdf

        constexpr float M_SQRT_PI = 1.7724538509f;
        constexpr float M_SQRT_3  = 1.7320508076f;
        constexpr float M_SQRT_5  = 2.2360679775f;
        constexpr float M_SQRT_15 = 3.8729833462f;
        constexpr float A[9] = {
                      1.0f / (2.0f * M_SQRT_PI),    // 0: 0  0
                -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1: 1 -1
                 M_SQRT_3  / (2.0f * M_SQRT_PI),    // 2: 1  0
                -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 3: 1  1
                 M_SQRT_15 / (2.0f * M_SQRT_PI),    // 4: 2 -2
                -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 5: 2 -1
                 M_SQRT_5  / (4.0f * M_SQRT_PI),    // 6: 2  0
                -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 7: 2  1
                 M_SQRT_15 / (4.0f * M_SQRT_PI)     // 8: 2  2
        };

        // first this to do is to rotate the SH to align Z with the optimal linear direction
        const float3 dir = normalize(float3{ -f[3], -f[1], f[2] });
        const float3 z_axis = -dir;
        const float3 x_axis = normalize(cross(z_axis, float3{ 0, 1, 0 }));
        const float3 y_axis = cross(x_axis, z_axis);
        const mat3f M = transpose(mat3f{ x_axis, y_axis, -z_axis });

        f = rotateSh3Bands(f, M);
        // here we're guaranteed to have normalize(float3{ -f[3], -f[1], f[2] }) == { 0, 0, 1 }


        // Find the min for |m| = 2
        // ------------------------
        //
        // Peter-Pike Sloan shows that the minimum can be expressed as a function
        // of z such as:  m2min = -m2max * (1 - z^2) =  m2max * z^2 - m2max
        //      with m2max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);
        // We can therefore include this in the ZH min computation (which is function of z^2 as well)
        float m2max = A[8] * std::sqrt(f[8] * f[8] + f[4] * f[4]);

        // Find the min of the zonal harmonics
        // -----------------------------------
        //
        // This comes from minimizing the function:
        //      ZH(z) = (A[0] * f[0])
        //            + (A[2] * f[2]) * z
        //            + (A[6] * f[6]) * (3 * s.z * s.z - 1)
        //
        // We do that by finding where it's derivative d/dz is zero:
        //      dZH(z)/dz = a * z^2 + b * z + c
        //      which is zero for z = -b / 2 * a
        //
        // We also needs to check that -1 < z < 1, otherwise the min is either in z = -1 or 1
        //
        const float a = 3 * A[6] * f[6] + m2max;
        const float b = A[2] * f[2];
        const float c = A[0] * f[0] - A[6] * f[6] - m2max;

        const float zmin = -b / (2.0f * a);
        const float m0min_z = a * zmin * zmin + b * zmin + c;
        const float m0min_b = std::min(a + b + c, a - b + c);

        const float m0min = (a > 0 && zmin >= -1 && zmin <= 1) ? m0min_z : m0min_b;

        // Find the min for l = 2, |m| = 1
        // -------------------------------
        //
        // Note l = 1, |m| = 1 is guaranteed to be 0 because of the rotation step
        //
        // The function considered is:
        //        Y(x, y, z) = A[5] * f[5] * s.y * s.z
        //                   + A[7] * f[7] * s.z * s.x
        float d = A[4] * std::sqrt(f[5] * f[5] + f[7] * f[7]);

        // the |m|=1 function is minimal in -0.5 -- use that to skip the Newton's loop when possible
        float minimum = m0min - 0.5f * d;
        if (minimum < 0) {
            // We could be negative, to find the minimum we will use Newton's method
            // See https://en.wikipedia.org/wiki/Newton%27s_method_in_optimization

            // this is the function we're trying to minimize
            auto func = [=](float x) {
                // first term accounts for ZH + |m| = 2, second terms for |m| = 1
                return (a * x * x + b * x + c) + (d * x * std::sqrt(1 - x * x));
            };

            // This is func' / func'' -- this was computed with Mathematica
            auto increment = [=](float x) {
                return (x * x - 1) * (d - 2 * d * x * x + (b + 2 * a * x) * std::sqrt(1 - x * x))
                       / (3 * d * x - 2 * d * x * x * x - 2 * a * std::pow(1 - x * x, 1.5f));

            };

            float dz;
            float z = -F_SQRT1_2;   // we start guessing at the min of |m|=1 function
            do {
                minimum = func(z); // evaluate our function
                dz = increment(z); // refine our guess by this amount
                z = z - dz;
                // exit if z goes out of range, or if we have reached enough precision
            } while (std::abs(z) <= 1 && std::abs(dz) > 1e-5f);

            if (std::abs(z) > 1) {
                // z was out of range
                minimum = std::min(func(1), func(-1));
            }
        }
        return minimum;
    };

    auto windowing = [numBands](SH3 f, float cutoff) -> SH3 {
        for (ssize_t l = 0; l < numBands; l++) {
            float w = sincWindow(l, cutoff);
            f[SHindex(0, l)] *= w;
            for (size_t m = 1; m <= l; m++) {
                f[SHindex(-m, l)] *= w;
                f[SHindex(m, l)]  *= w;
            }
        }
        return f;
    };

    if (cutoff == 0) { // auto windowing (default)
        if (numBands > 3) {
            // auto-windowing works only for 1, 2 or 3 bands
            slog.e << "--sh-window=auto can't work with more than 3 bands. Disabling." << io::endl;
            return;
        }

        cutoff = numBands * 4 + 1; // start at a large band
        // We need to process each channel separately
        SH3 SH = {};
        for (size_t channel = 0; channel < 3; channel++) {
            for (size_t i = 0; i < numBands * numBands; i++) {
                SH[i] = sh[i][channel];
            }

            // find a cut-off band that works
            float l = numBands;
            float r = cutoff;
            for (size_t i = 0; i < 16 && l + 0.1f < r; i++) {
                float m = 0.5f * (l + r);
                if (shmin(windowing(SH, m)) < 0) {
                    r = m;
                } else {
                    l = m;
                }
            }
            cutoff = std::min(cutoff, l);
        }
    }

    //slog.d << cutoff << io::endl;
    for (ssize_t l = 0; l < numBands; l++) {
        float w = sincWindow(l, cutoff);
        sh[SHindex(0, l)] *= w;
        for (size_t m = 1; m <= l; m++) {
            sh[SHindex(-m, l)] *= w;
            sh[SHindex(m, l)] *= w;
        }
    }
}

/**
 * 从立方体贴图计算球谐函数系数实现
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 获取像素对应的3D方向向量
 *    - 采样颜色值
 *    - 乘以立体角（重要性采样权重）
 *    - 计算球谐基函数值
 *    - 累加到球谐系数中
 * 2. 预计算缩放因子K
 * 3. 如果计算辐照度，应用截断余弦函数
 * 4. 应用所有缩放因子
 * 
 * @param js 作业系统
 * @param cm 源立方体贴图
 * @param numBands 球谐函数的频带数
 * @param irradiance 是否计算辐照度（true）或环境贴图（false）
 * @return 球谐函数系数数组
 */
std::unique_ptr<float3[]> CubemapSH::computeSH(JobSystem& js, const Cubemap& cm, size_t numBands, bool irradiance) {

    const size_t numCoefs = numBands * numBands;
    std::unique_ptr<float3[]> SH(new float3[numCoefs]{});

    // 状态结构体：存储每个线程的中间结果
    struct State {
        State() = default;
        explicit State(size_t numCoefs) : numCoefs(numCoefs) { }

        State& operator=(State const & rhs) {
            SH.reset(new float3[rhs.numCoefs]{}); // NOLINT(modernize-make-unique)
            SHb.reset(new float[rhs.numCoefs]{}); // NOLINT(modernize-make-unique)
            return *this;
        }
        size_t numCoefs = 0;
        std::unique_ptr<float3[]> SH;      // 球谐系数累加器
        std::unique_ptr<float[]> SHb;       // 球谐基函数值
    } prototype(numCoefs);

    // 使用多线程处理立方体贴图
    CubemapUtils::process<State>(const_cast<Cubemap&>(cm), js,
            [&](State& state, size_t y, Cubemap::Face f, Cubemap::Texel const* data, size_t dim) {
        for (size_t x=0 ; x<dim ; ++x, ++data) {

            float3 s(cm.getDirectionFor(f, x, y));  // 获取3D方向向量

            // sample a color
            // 采样颜色值
            float3 color(Cubemap::sampleAt(data));

            // take solid angle into account
            // 考虑立体角（重要性采样权重）
            color *= CubemapUtils::solidAngle(dim, x, y);

            // 计算球谐基函数值
            computeShBasis(state.SHb.get(), numBands, s);

            // apply coefficients to the sampled color
            // 将系数应用到采样的颜色
            for (size_t i=0 ; i<numCoefs ; i++) {
                state.SH[i] += color * state.SHb[i];
            }
        }
    },
    [&](State& state) {
        // 归约：合并所有线程的结果
        for (size_t i=0 ; i<numCoefs ; i++) {
            SH[i] += state.SH[i];
        }
    }, prototype);

    // precompute the scaling factor K
    // 预计算缩放因子K
    std::vector<float> K = Ki(numBands);

    // apply truncated cos (irradiance)
    // 应用截断余弦（辐照度）
    if (irradiance) {
        for (size_t l = 0; l < numBands; l++) {
            const float truncatedCosSh = computeTruncatedCosSh(size_t(l));
            K[SHindex(0, l)] *= truncatedCosSh;
            for (size_t m = 1; m <= l; m++) {
                K[SHindex(-m, l)] *= truncatedCosSh;
                K[SHindex( m, l)] *= truncatedCosSh;
            }
        }
    }

    // apply all the scale factors
    // 应用所有缩放因子
    for (size_t i = 0; i < numCoefs; i++) {
        SH[i] *= K[i];
    }
    return SH;
}

/**
 * 从球谐函数系数渲染立方体贴图实现
 * 
 * 执行步骤：
 * 1. 预计算缩放因子K
 * 2. 对立方体贴图的每个像素：
 *    - 获取像素对应的3D方向向量
 *    - 计算球谐基函数值
 *    - 使用球谐系数重建颜色值
 *    - 写入立方体贴图
 * 
 * @param js 作业系统
 * @param cm 目标立方体贴图（会被修改）
 * @param sh 球谐函数系数数组
 * @param numBands 球谐函数的频带数
 */
void CubemapSH::renderSH(JobSystem& js, Cubemap& cm,
        const std::unique_ptr<float3[]>& sh, size_t numBands) {
    const size_t numCoefs = numBands * numBands;

    // precompute the scaling factor K
    // 预计算缩放因子K
    const std::vector<float> K = Ki(numBands);

    // 状态结构体：用于调试（计算最小值）
    struct State {
        // we compute the min just for debugging -- it's not actually needed.
        // 我们计算最小值仅用于调试——实际上不需要
        float3 min = std::numeric_limits<float>::max();
    } prototype;

    // 使用多线程处理立方体贴图
    CubemapUtils::process<State>(cm, js,
            [&](State& state, size_t y,
                    Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
                std::vector<float> SHb(numCoefs);
                for (size_t x = 0; x < dim; ++x, ++data) {
                    float3 s(cm.getDirectionFor(f, x, y));
                    computeShBasis(SHb.data(), numBands, s);
                    float3 c = 0;
                    for (size_t i = 0; i < numCoefs; i++) {
                        c += sh[i] * (K[i] * SHb[i]);
                    }
                    c *= F_1_PI;
                    state.min = min(c, state.min);
                    Cubemap::writeAt(data, Cubemap::Texel(c));
                }
            },
            [&](State& state) {
                prototype.min = min(prototype.min, state.min);
            }, prototype);
    //slog.d << prototype.min << io::endl;
}

/**
 * 为着色器预处理球谐函数系数实现
 * 
 * 计算立方体贴图与截断余弦函数（即 saturate(s.z)）卷积的3阶球谐系数，
 * 并预乘以重建因子。
 * 
 * 执行步骤：
 * 1. 定义球谐函数的多项式形式系数A[i]
 * 2. 将球谐系数乘以A[i]和Lambertian漫反射BRDF（1/π）
 * 
 * 这样在着色器中可以直接使用多项式形式计算，无需额外的数学运算：
 * c += sh[0] * A[0];
 * c += sh[1] * A[1] * s.y;
 * c += sh[2] * A[2] * s.z;
 * ...
 * 
 * @param SH 球谐函数系数数组（会被修改）
 */
void CubemapSH::preprocessSHForShader(std::unique_ptr<filament::math::float3[]>& SH) {
    constexpr size_t numBands = 3;
    constexpr size_t numCoefs = numBands * numBands;

    // Coefficient for the polynomial form of the SH functions -- these were taken from
    // "Stupid Spherical Harmonics (SH)" by Peter-Pike Sloan
    // They simply come for expanding the computation of each SH function.
    //
    // To render spherical harmonics we can use the polynomial form, like this:
    //          c += sh[0] * A[0];
    //          c += sh[1] * A[1] * s.y;
    //          c += sh[2] * A[2] * s.z;
    //          c += sh[3] * A[3] * s.x;
    //          c += sh[4] * A[4] * s.y * s.x;
    //          c += sh[5] * A[5] * s.y * s.z;
    //          c += sh[6] * A[6] * (3 * s.z * s.z - 1);
    //          c += sh[7] * A[7] * s.z * s.x;
    //          c += sh[8] * A[8] * (s.x * s.x - s.y * s.y);
    //
    // To save math in the shader, we pre-multiply our SH coefficient by the A[i] factors.
    // Additionally, we include the lambertian diffuse BRDF 1/pi.

    constexpr float M_SQRT_PI = 1.7724538509f;
    constexpr float M_SQRT_3  = 1.7320508076f;
    constexpr float M_SQRT_5  = 2.2360679775f;
    constexpr float M_SQRT_15 = 3.8729833462f;
    constexpr float A[numCoefs] = {
                  1.0f / (2.0f * M_SQRT_PI),    // 0  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1 -1
             M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  0
            -M_SQRT_3  / (2.0f * M_SQRT_PI),    // 1  1
             M_SQRT_15 / (2.0f * M_SQRT_PI),    // 2 -2
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3 -1
             M_SQRT_5  / (4.0f * M_SQRT_PI),    // 3  0
            -M_SQRT_15 / (2.0f * M_SQRT_PI),    // 3  1
             M_SQRT_15 / (4.0f * M_SQRT_PI)     // 3  2
    };

    for (size_t i = 0; i < numCoefs; i++) {
        SH[i] *= A[i] * F_1_PI;
    }
}

/**
 * 使用预缩放球谐系数渲染3阶球谐立方体贴图实现
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 获取像素对应的3D方向向量
 *    - 使用多项式形式直接计算球谐函数值（系数已预缩放）
 *    - 写入立方体贴图
 * 
 * 使用预缩放的球谐系数，可以直接使用多项式形式计算，无需额外的缩放因子。
 * 这比renderSH更高效，因为系数已经包含了重建因子和Lambertian BRDF。
 * 
 * @param js 作业系统
 * @param cm 目标立方体贴图（会被修改）
 * @param sh 预缩放的3阶球谐函数系数数组（9个系数）
 */
void CubemapSH::renderPreScaledSH3Bands(JobSystem& js,
        Cubemap& cm, const std::unique_ptr<filament::math::float3[]>& sh) {
    CubemapUtils::process<CubemapUtils::EmptyState>(cm, js,
            [&](CubemapUtils::EmptyState&, size_t y, Cubemap::Face f, Cubemap::Texel* data,
                    size_t dim) {
                for (size_t x = 0; x < dim; ++x, ++data) {
                    float3 s(cm.getDirectionFor(f, x, y));  // 获取3D方向向量
                    float3 c = 0;
                    // 使用多项式形式计算球谐函数值（系数已预缩放）
                    c += sh[0];  // L0,0

                    c += sh[1] * s.y;  // L1,-1
                    c += sh[2] * s.z;  // L1,0
                    c += sh[3] * s.x;  // L1,1

                    c += sh[4] * s.y * s.x;  // L2,-2
                    c += sh[5] * s.y * s.z;  // L2,-1
                    c += sh[6] * (3 * s.z * s.z - 1);  // L2,0
                    c += sh[7] * s.z * s.x;  // L2,1
                    c += sh[8] * (s.x * s.x - s.y * s.y);  // L2,2
                    Cubemap::writeAt(data, Cubemap::Texel(c));
                }
            });
}

// -----------------------------------------------------------------------------------------------
// Only used for debugging
// 仅用于调试
// -----------------------------------------------------------------------------------------------

/**
 * 计算关联勒让德多项式实现（仅用于调试）
 * 
 * 计算关联勒让德多项式 P(l,m,x) 在 x 处的值。
 * 使用递归公式计算。
 * 
 * @param l 球面阶数
 * @param m 方位角阶数
 * @param x 输入值（通常是cos(theta)）
 * @return 关联勒让德多项式的值
 */
float UTILS_UNUSED CubemapSH::Legendre(ssize_t l, ssize_t m, float x) {
    // evaluate an Associated Legendre Polynomial P(l,m,x) at x
    // 计算关联勒让德多项式 P(l,m,x) 在 x 处的值
    float pmm = 1.0;
    if (m > 0) {
        float somx2 = sqrt((1.0f - x) * (1.0f + x));
        float fact = 1.0f;
        for (int i = 1; i <= m; i++) {
            pmm *= (-fact) * somx2;
            fact += 2.0f;
        }
    }
    if (l == m)
        return pmm;
    float pmmp1 = x * (2.0f * m + 1.0f) * pmm;
    if (l == m + 1)
        return pmmp1;
    float pll = 0.0;
    for (ssize_t ll = m + 2; ll <= l; ++ll) {
        pll = ((2.0f * ll - 1.0f) * x * pmmp1 - (ll + m - 1.0f) * pmm) / (ll - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }
    return pll;
}

/**
 * 计算球谐基函数值实现（仅用于调试）
 * 
 * 直接计算球谐基函数在给定方向上的值。
 * 仅支持前3阶（l=0,1,2）。
 * 
 * @param l 球面阶数
 * @param m 方位角阶数
 * @param d 归一化的3D方向向量
 * @return 球谐基函数值
 */
// Only used for debugging
// 仅用于调试
float UTILS_UNUSED CubemapSH::TSH(int l, int m, const float3& d) {
    if (l==0 && m==0) {
        return 1 / (2*sqrt(F_PI));
    } else if (l==1 && m==-1) {
        return -(sqrt(3)*d.y)/(2*sqrt(F_PI));
    } else if (l==1 && m==0) {
        return  (sqrt(3)*d.z)/(2*sqrt(F_PI));
    } else if (l==1 && m==1) {
        return -(sqrt(3)*d.x)/(2*sqrt(F_PI));
    } else if (l==2 && m==-2) {
        return (sqrt(15)*d.y*d.x)/(2*sqrt(F_PI));
    } else if (l==2 && m==-1) {
        return -(sqrt(15)*d.y*d.z)/(2*sqrt(F_PI));
    } else if (l==2 && m==0) {
        return (sqrt(5)*(3*d.z*d.z-1))/(4*sqrt(F_PI));
    } else if (l==2 && m==1) {
        return -(sqrt(15)*d.z*d.x)/(2*sqrt(F_PI));
    } else if (l==2 && m==2) {
        return (sqrt(15)*(d.x*d.x - d.y*d.y))/(4*sqrt(F_PI));
    }
    return 0;
}

/**
 * 打印球谐基函数的多项式形式实现（仅用于调试）
 * 
 * 将球谐基函数的多项式形式输出到流中，用于代码生成或验证。
 * 仅支持前3阶（l=0,1,2）。
 * 
 * @param out 输出流
 * @param l 球面阶数
 * @param m 方位角阶数
 */
void UTILS_UNUSED CubemapSH::printShBase(std::ostream& out, int l, int m) {
    if (l<3 && std::abs(m) <= l) {
        const char* d = nullptr;
        float c = 0;
        if (l==0 && m==0) {
            c = F_2_SQRTPI * 0.25;
            d = "               ";
        } else if (l==1 && m==-1) {
            c = -F_2_SQRTPI * sqrt(3) * 0.25;
            d = " * y;          ";
        } else if (l==1 && m==0) {
            c = F_2_SQRTPI * sqrt(3) * 0.25;
            d = " * z;          ";
        } else if (l==1 && m==1) {
            c = -F_2_SQRTPI * sqrt(3) * 0.25;
            d = " * x;          ";
        } else if (l==2 && m==-2) {
            c = F_2_SQRTPI * sqrt(15) * 0.25;
            d = " * y*x;        ";
        } else if (l==2 && m==-1) {
            c = -F_2_SQRTPI * sqrt(15) * 0.25;
            d = " * y*z;        ";
        } else if (l==2 && m==0) {
            c =  F_2_SQRTPI * sqrt(5) * 0.125;
            d = " * (3*z*z -1); ";
        } else if (l==2 && m==1) {
            c = -F_2_SQRTPI * sqrt(15) * 0.25;
            d = " * z*x;        ";
        } else if (l==2 && m==2) {
            c = F_2_SQRTPI * sqrt(15) * 0.125;
            d = " * (x*x - y*y);";
        }
        out << "SHb[" << SHindex(m, size_t(l)) << "] = ";
        out << std::fixed << std::setprecision(15) << std::setw(18) << c << d;
        out << " // L" << l << m;
    }
}

} // namespace ibl
} // namespace filament
