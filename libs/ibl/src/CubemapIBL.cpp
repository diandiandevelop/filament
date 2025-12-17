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


#include <ibl/CubemapIBL.h>

#include <ibl/Cubemap.h>
#include <ibl/CubemapUtils.h>
#include <ibl/utilities.h>

#include "CubemapUtilsImpl.h"

#include <utils/JobSystem.h>

#include <math/mat3.h>
#include <math/scalar.h>

#include <random>
#include <vector>

using namespace filament::math;
using namespace utils;

namespace filament {
namespace ibl {

/**
 * 计算x的5次方（优化版本）
 * 
 * 使用 x^5 = (x^2)^2 * x 来减少乘法次数。
 * 
 * @param x 输入值
 * @return x的5次方
 */
static float pow5(float x) {
    const float x2 = x * x;
    return x2 * x2 * x;
}

/**
 * 计算x的6次方（优化版本）
 * 
 * 使用 x^6 = (x^2)^3 来减少乘法次数。
 * 
 * @param x 输入值
 * @return x的6次方
 */
static float pow6(float x) {
    const float x2 = x * x;
    return x2 * x2 * x2;
}

/**
 * 使用GGX分布进行半球重要性采样实现
 * 
 * 根据GGX（Trowbridge-Reitz）分布函数进行重要性采样，
 * 用于镜面反射的预过滤。
 * 
 * pdf = D(a) * cosTheta，其中D是GGX分布函数，a是粗糙度参数。
 * 
 * @param u 均匀随机数 [0, 1]^2
 * @param a 粗糙度参数（线性空间）
 * @return 采样方向向量（半球坐标系）
 */
static float3 hemisphereImportanceSampleDggx(float2 u, float a) { // pdf = D(a) * cosTheta
    const float phi = 2.0f * (float) F_PI * u.x;  // 方位角 [0, 2π]
    // NOTE: (aa-1) == (a-1)(a+1) produces better fp accuracy
    // 注意：(aa-1) == (a-1)(a+1) 产生更好的浮点精度
    // 计算cos(theta)^2，使用GGX分布的逆CDF
    const float cosTheta2 = (1 - u.y) / (1 + (a + 1) * ((a - 1) * u.y));
    const float cosTheta = std::sqrt(cosTheta2);
    const float sinTheta = std::sqrt(1 - cosTheta2);
    // 转换为笛卡尔坐标
    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}

/**
 * 使用余弦加权进行半球采样实现（未使用）
 * 
 * pdf = cosTheta / π
 * 
 * 用于漫反射光照的采样。
 * 
 * @param u 均匀随机数 [0, 1]^2
 * @return 采样方向向量（半球坐标系）
 */
static float3 UTILS_UNUSED hemisphereCosSample(float2 u) {  // pdf = cosTheta / F_PI;
    const float phi = 2.0f * (float) F_PI * u.x;  // 方位角 [0, 2π]
    const float cosTheta2 = 1 - u.y;  // 余弦平方（均匀分布）
    const float cosTheta = std::sqrt(cosTheta2);
    const float sinTheta = std::sqrt(1 - cosTheta2);
    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}

/**
 * 均匀半球采样实现（未使用）
 * 
 * pdf = 1.0 / (2.0 * π)
 * 
 * 最简单的半球采样方法，但收敛速度较慢。
 * 
 * @param u 均匀随机数 [0, 1]^2
 * @return 采样方向向量（半球坐标系）
 */
static float3 UTILS_UNUSED hemisphereUniformSample(float2 u) { // pdf = 1.0 / (2.0 * F_PI);
    const float phi = 2.0f * (float) F_PI * u.x;  // 方位角 [0, 2π]
    const float cosTheta = 1 - u.y;  // 余弦（均匀分布）
    const float sinTheta = std::sqrt(1 - cosTheta * cosTheta);
    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}

/*
 *
 * Importance sampling Charlie
 * ---------------------------
 *
 * In order to pick the most significative samples and increase the convergence rate, we chose to
 * rely on Charlie's distribution function for the pdf as we do in hemisphereImportanceSampleDggx.
 *
 * To determine the direction we then need to resolve the cdf associated to the chosen pdf for random inputs.
 *
 * Knowing pdf() = DCharlie(h) <n•h>
 *
 * We need to find the cdf:
 *
 * / 2pi     / pi/2
 * |         |  (2 + (1 / a)) * sin(theta) ^ (1 / a) * cos(theta) * sin(theta)
 * / phi=0   / theta=0
 *
 * We sample theta and phi independently.
 *
 * 1. as in all the other isotropic cases phi = 2 * pi * epsilon
 *    (https://www.tobias-franke.eu/log/2014/03/30/notes_on_importance_sampling.html)
 *
 * 2. we need to solve the integral on theta:
 *
 *             / sTheta
 * P(sTheta) = |  (2 + (1 / a)) * sin(theta) ^ (1 / a + 1) * cos(theta) * dtheta
 *             / theta=0
 *
 * By subsitution of u = sin(theta) and du = cos(theta) * dtheta
 *
 * /
 * |  (2 + (1 / a)) * u ^ (1 / a + 1) * du
 * /
 *
 * = (2 + (1 / a)) * u ^ (1 / a + 2) / (1 / a + 2)
 *
 * = u ^ (1 / a + 2)
 *
 * = sin(theta) ^ (1 / a + 2)
 *
 *             +-                          -+ sTheta
 * P(sTheta) = |  sin(theta) ^ (1 / a + 2)  |
 *             +-                          -+ 0
 *
 * P(sTheta) = sin(sTheta) ^ (1 / a + 2)
 *
 * We now need to resolve the cdf for an epsilon value:
 *
 * epsilon = sin(theta) ^ (a / ( 2 * a + 1))
 *
 *  +--------------------------------------------+
 *  |                                            |
 *  |  sin(theta) = epsilon ^ (a / ( 2 * a + 1)) |
 *  |                                            |
 *  +--------------------------------------------+
 */
static float3 UTILS_UNUSED hemisphereImportanceSampleDCharlie(float2 u, float a) { // pdf = DistributionCharlie() * cosTheta
    const float phi = 2.0f * (float) F_PI * u.x;

    const float sinTheta = std::pow(u.y, a / (2 * a + 1));
    const float cosTheta = std::sqrt(1 - sinTheta * sinTheta);

    return { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };
}

/**
 * GGX（Trowbridge-Reitz）分布函数实现
 * 
 * 计算微表面法线分布函数D(h)，用于PBR渲染中的镜面反射。
 * 
 * @param NoH 法线n和半向量h的点积
 * @param linearRoughness 线性粗糙度参数
 * @return 分布函数值
 */
static float DistributionGGX(float NoH, float linearRoughness) {
    // NOTE: (aa-1) == (a-1)(a+1) produces better fp accuracy
    // 注意：(aa-1) == (a-1)(a+1) 产生更好的浮点精度
    float a = linearRoughness;
    float f = (a - 1) * ((a + 1) * (NoH * NoH)) + 1;
    return (a * a) / ((float) F_PI * f * f);
}

/**
 * Ashikhmin分布函数实现（未使用）
 * 
 * 另一种微表面分布函数，用于某些特殊材质。
 * 
 * @param NoH 法线n和半向量h的点积
 * @param linearRoughness 线性粗糙度参数
 * @return 分布函数值
 */
static float UTILS_UNUSED DistributionAshikhmin(float NoH, float linearRoughness) {
    float a = linearRoughness;
    float a2 = a * a;
    float cos2h = NoH * NoH;
    float sin2h = 1 - cos2h;
    float sin4h = sin2h * sin2h;
    return 1.0f / ((float) F_PI * (1 + 4 * a2)) * (sin4h + 4 * std::exp(-cos2h / (a2 * sin2h)));
}

/**
 * Charlie分布函数实现（未使用）
 * 
 * 用于布料材质的微表面分布函数。
 * 参考：Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
 * 
 * @param NoH 法线n和半向量h的点积
 * @param linearRoughness 线性粗糙度参数
 * @return 分布函数值
 */
static float UTILS_UNUSED DistributionCharlie(float NoH, float linearRoughness) {
    // Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
    float a = linearRoughness;
    float invAlpha = 1 / a;
    float cos2h = NoH * NoH;
    float sin2h = 1 - cos2h;
    return (2.0f + invAlpha) * std::pow(sin2h, invAlpha * 0.5f) / (2.0f * (float) F_PI);
}

/**
 * Fresnel项实现
 * 
 * 计算菲涅尔反射系数，用于模拟视角相关的反射强度。
 * 
 * @param f0 垂直入射时的反射率
 * @param f90 掠射角时的反射率
 * @param LoH 光线方向l和半向量h的点积
 * @return 菲涅尔系数
 */
static float Fresnel(float f0, float f90, float LoH) {
    const float Fc = pow5(1 - LoH);  // 使用5次方近似菲涅尔项
    return f0 * (1 - Fc) + f90 * Fc;
}

/**
 * 可见性函数（几何项）实现
 * 
 * 计算微表面的遮挡和阴影效应，使用高度相关的GGX模型。
 * 参考：Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param NoL 法线n和光线方向l的点积
 * @param a 粗糙度参数
 * @return 可见性系数
 */
static float Visibility(float NoV, float NoL, float a) {
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
    // Height-correlated GGX
    // 高度相关的GGX
    const float a2 = a * a;
    const float GGXL = NoV * std::sqrt((NoL - NoL * a2) * NoL + a2);
    const float GGXV = NoL * std::sqrt((NoV - NoV * a2) * NoV + a2);
    return 0.5f / (GGXV + GGXL);
}

///**
// * Ashikhmin可见性函数实现（未使用）
// * 
// * 另一种可见性函数，用于某些特殊材质。
// * 参考：Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
// * 
// * @param NoV 法线n和视线方向v的点积
// * @param NoL 法线n和光线方向l的点积
// * @param /*a*/ 粗糙度参数（未使用）
// * @return 可见性系数
// */
static float UTILS_UNUSED VisibilityAshikhmin(float NoV, float NoL, float /*a*/) {
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return 1 / (4 * (NoL + NoV - NoL * NoV));
}

/*
 *
 * Importance sampling GGX - Trowbridge-Reitz
 * ------------------------------------------
 *
 * Important samples are chosen to integrate Dggx() * cos(theta) over the hemisphere.
 *
 * All calculations are made in tangent space, with n = [0 0 1]
 *
 *             l        h (important sample)
 *             .\      /.
 *             . \    / .
 *             .  \  /  .
 *             .   \/   .
 *         ----+---o----+-------> n [0 0 1]
 *     cos(2*theta)     cos(theta)
 *        = n•l            = n•h
 *
 *  v = n
 *  f0 = f90 = 1
 *  V = 1
 *
 *  h is micro facet's normal
 *
 *  l is the reflection of v (i.e.: n) around h  ==>  n•h = l•h = v•h
 *
 *  h = important_sample_ggx()
 *
 *  n•h = [0 0 1]•h = h.z
 *
 *  l = reflect(-n, h)
 *    = 2 * (n•h) * h - n;
 *
 *  n•l = cos(2 * theta)
 *      = cos(theta)^2 - sin(theta)^2
 *      = (n•h)^2 - (1 - (n•h)^2)
 *      = 2(n•h)^2 - 1
 *
 *
 *  pdf() = D(h) <n•h> |J(h)|
 *
 *               1
 *  |J(h)| = ----------
 *            4 <v•h>
 *
 *
 * Pre-filtered importance sampling
 * --------------------------------
 *
 *  see: "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
 *  see: "GPU-Based Importance Sampling, GPU Gems 3", Mark Colbert
 *
 *
 *                   Ωs
 *     lod = log4(K ----)
 *                   Ωp
 *
 *     log4(K) = 1, works well for box filters
 *     K = 4
 *
 *             1
 *     Ωs = ---------, solid-angle of an important sample
 *           N * pdf
 *
 *              4 PI
 *     Ωp ~ --------------, solid-angle of a sample in the base cubemap
 *           texel_count
 *
 *
 * Evaluating the integral
 * -----------------------
 *
 *                    K     fr(h)
 *            Er() = --- ∑ ------- L(h) <n•l>
 *                    N  h   pdf
 *
 * with:
 *
 *            fr() = D(h)
 *
 *                       N
 *            K = -----------------
 *                    fr(h)
 *                 ∑ ------- <n•l>
 *                 h   pdf
 *
 *
 *  It results that:
 *
 *            K           4 <v•h>
 *    Er() = --- ∑ D(h) ------------ L(h) <n•l>
 *            N  h        D(h) <n•h>
 *
 *
 *              K
 *    Er() = 4 --- ∑ L(h) <n•l>
 *              N  h
 *
 *                  N       4
 *    Er() = ------------- --- ∑ L(v) <n•l>
 *             4 ∑ <n•l>    N
 *
 *
 *  +------------------------------+
 *  |          ∑ <n•l> L(h)        |
 *  |  Er() = --------------       |
 *  |            ∑ <n•l>           |
 *  +------------------------------+
 *
 */

/**
 * 粗糙度过滤实现（使用vector版本）
 * 
 * 这是使用std::vector的便捷版本，内部转换为Slice并调用完整版本。
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param levels 源立方体贴图Mipmap级别数组
 * @param linearRoughness 线性粗糙度
 * @param maxNumSamples 最大采样数
 * @param mirror 镜像向量（用于翻转方向）
 * @param prefilter 是否使用预过滤（Mipmap）
 * @param updater 进度更新回调
 * @param userdata 用户数据指针
 */
UTILS_ALWAYS_INLINE
void CubemapIBL::roughnessFilter(
        utils::JobSystem& js, Cubemap& dst, const std::vector<Cubemap>& levels,
        float linearRoughness, size_t maxNumSamples, math::float3 mirror, bool prefilter,
        Progress updater, void* userdata) {
    roughnessFilter(js, dst, { levels.data(), uint32_t(levels.size()) },
            linearRoughness, maxNumSamples, mirror, prefilter, updater, userdata);
}

/**
 * 粗糙度过滤实现（完整版本）
 * 
 * 使用重要性采样和GGX分布函数对立方体贴图进行粗糙度过滤。
 * 这是镜面反射预过滤的核心算法。
 * 
 * 执行步骤：
 * 1. 如果粗糙度为0，直接复制源立方体贴图（完美镜面反射）
 * 2. 否则：
 *    - 预计算重要性采样方向（使用GGX分布）
 *    - 计算每个采样的LOD级别（预过滤重要性采样）
 *    - 对每个像素：
 *      * 构建围绕法线的旋转矩阵
 *      * 使用重要性采样方向在源立方体贴图中采样
 *      * 使用三线性过滤在Mipmap级别之间插值
 *      * 累加加权结果
 *    - 归一化结果
 * 
 * 参考：
 * - "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
 * - "GPU-Based Importance Sampling, GPU Gems 3", Mark Colbert
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param levels 源立方体贴图Mipmap级别数组（Slice）
 * @param linearRoughness 线性粗糙度
 * @param maxNumSamples 最大采样数
 * @param mirror 镜像向量（用于翻转方向）
 * @param prefilter 是否使用预过滤（Mipmap）
 * @param updater 进度更新回调
 * @param userdata 用户数据指针
 */
void CubemapIBL::roughnessFilter(
        utils::JobSystem& js, Cubemap& dst, utils::Slice<const Cubemap> levels,
        float linearRoughness, size_t maxNumSamples, math::float3 mirror, bool prefilter,
        Progress updater, void* userdata)
{
    const float numSamples = maxNumSamples;
    const float inumSamples = 1.0f / numSamples;
    const size_t maxLevel = levels.size()-1;
    const float maxLevelf = maxLevel;
    const Cubemap& base(levels[0]);
    const size_t dim0 = base.getDimensions();
    const float omegaP = (4.0f * (float) F_PI) / float(6 * dim0 * dim0);
    std::atomic_uint progress = {0};

    if (linearRoughness == 0) {
        auto scanline = [&]
                (CubemapUtils::EmptyState&, size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
                    if (UTILS_UNLIKELY(updater)) {
                        size_t p = progress.fetch_add(1, std::memory_order_relaxed) + 1;
                        updater(0, (float)p / ((float) dim * 6.0f), userdata);
                    }
                    const Cubemap& cm = levels[0];
                    for (size_t x = 0; x < dim; ++x, ++data) {
                        const float2 p(Cubemap::center(x, y));
                        const float3 N(dst.getDirectionFor(f, p.x, p.y) * mirror);
                        // FIXME: we should pick the proper LOD here and do trilinear filtering
                        Cubemap::writeAt(data, cm.sampleAt(N));
                    }
        };
        // at least 256 pixel cubemap before we use multithreading -- the overhead of launching
        // jobs is too large compared to the work above.
        if (dst.getDimensions() <= 256) {
            CubemapUtils::processSingleThreaded<CubemapUtils::EmptyState>(
                    dst, js, std::ref(scanline));
        } else {
            CubemapUtils::process<CubemapUtils::EmptyState>(dst, js, std::ref(scanline));
        }
        return;
    }

    // be careful w/ the size of this structure, the smaller the better
    struct CacheEntry {
        float3 L;
        float brdf_NoL;
        float lerp;
        uint8_t l0;
        uint8_t l1;
    };

    std::vector<CacheEntry> cache;
    cache.reserve(maxNumSamples);

    // precompute everything that only depends on the sample #
    float weight = 0;
    // index of the sample to use
    // our goal is to use maxNumSamples for which NoL is > 0
    // to achieve this, we might have to try more samples than
    // maxNumSamples
    for (size_t sampleIndex = 0 ; sampleIndex < maxNumSamples; sampleIndex++) {

        // get Hammersley distribution for the half-sphere
        const float2 u = hammersley(uint32_t(sampleIndex), inumSamples);

        // Importance sampling GGX - Trowbridge-Reitz
        const float3 H = hemisphereImportanceSampleDggx(u, linearRoughness);

#if 0
        // This produces the same result that the code below using the the non-simplified
        // equation. This let's us see that N == V and that L = -reflect(V, H)
        // Keep this for reference.
        const float3 N = {0, 0, 1};
        const float3 V = N;
        const float3 L = 2 * dot(H, V) * H - V;
        const float NoL = dot(N, L);
        const float NoH = dot(N, H);
        const float NoH2 = NoH * NoH;
        const float NoV = dot(N, V);
#else
        const float NoH = H.z;
        const float NoH2 = H.z * H.z;
        const float NoL = 2 * NoH2 - 1;
        const float3 L(2 * NoH * H.x, 2 * NoH * H.y, NoL);
#endif

        if (NoL > 0) {
            const float pdf = DistributionGGX(NoH, linearRoughness) / 4;

            // K is a LOD bias that allows a bit of overlapping between samples
            constexpr float K = 4;
            const float omegaS = 1 / (numSamples * pdf);
            const float l = float(log4(omegaS) - log4(omegaP) + log4(K));
            const float mipLevel = prefilter ? clamp(float(l), 0.0f, maxLevelf) : 0.0f;

            const float brdf_NoL = float(NoL);

            weight += brdf_NoL;

            uint8_t l0 = uint8_t(mipLevel);
            uint8_t l1 = uint8_t(std::min(maxLevel, size_t(l0 + 1)));
            float lerp = mipLevel - (float) l0;

            cache.push_back({ L, brdf_NoL, lerp, l0, l1 });
        }
    }

    for (auto& entry : cache) {
        entry.brdf_NoL *= 1.0f / weight;
    }

    // we can sample the cubemap in any order, sort by the weight, it could improve fp precision
    std::sort(cache.begin(), cache.end(), [](CacheEntry const& lhs, CacheEntry const& rhs) {
        return lhs.brdf_NoL < rhs.brdf_NoL;
    });


    struct State {
        // maybe blue-noise instead would look even better
        std::default_random_engine gen;
        std::uniform_real_distribution<float> distribution{ -F_PI, F_PI };
    };

    auto scanline = [&](State& state, size_t y,
            Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
        if (UTILS_UNLIKELY(updater)) {
            size_t p = progress.fetch_add(1, std::memory_order_relaxed) + 1;
            updater(0, (float) p / ((float) dim * 6.0f), userdata);
        }
        mat3 R;
        const size_t numSamples = cache.size();
        for (size_t x = 0; x < dim; ++x, ++data) {
            const float2 p(Cubemap::center(x, y));
            const float3 N(dst.getDirectionFor(f, p.x, p.y) * mirror);

            // center the cone around the normal (handle case of normal close to up)
            const float3 up = std::abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
            R[0] = normalize(cross(up, N));
            R[1] = cross(N, R[0]);
            R[2] = N;

            R *= mat3f::rotation(state.distribution(state.gen), float3{0,0,1});

            float3 Li = 0;
            for (size_t sample = 0; sample < numSamples; sample++) {
                const CacheEntry& e = cache[sample];
                const float3 L(R * e.L);
                const Cubemap& cmBase = levels[e.l0];
                const Cubemap& next = levels[e.l1];
                const float3 c0 = Cubemap::trilinearFilterAt(cmBase, next, e.lerp, L);
                Li += c0 * e.brdf_NoL;
            }
            Cubemap::writeAt(data, Cubemap::Texel(Li));
        }
    };

    // don't use the jobsystem unless we have enough work per scanline -- or the overhead of
    // launching jobs will prevail.
    if (dst.getDimensions() * maxNumSamples <= 256) {
        CubemapUtils::processSingleThreaded<State>(dst, js, std::ref(scanline));
    } else {
        CubemapUtils::process<State>(dst, js, std::ref(scanline));
    }
}

/*
 *
 * Importance sampling
 * -------------------
 *
 * Important samples are chosen to integrate cos(theta) over the hemisphere.
 *
 * All calculations are made in tangent space, with n = [0 0 1]
 *
 *                      l (important sample)
 *                     /.
 *                    / .
 *                   /  .
 *                  /   .
 *         --------o----+-------> n (direction)
 *                   cos(theta)
 *                    = n•l
 *
 *
 *  'direction' is given as an input parameter, and serves as tge z direction of the tangent space.
 *
 *  l = important_sample_cos()
 *
 *  n•l = [0 0 1] • l = l.z
 *
 *           n•l
 *  pdf() = -----
 *           PI
 *
 *
 * Pre-filtered importance sampling
 * --------------------------------
 *
 *  see: "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
 *  see: "GPU-Based Importance Sampling, GPU Gems 3", Mark Colbert
 *
 *
 *                   Ωs
 *     lod = log4(K ----)
 *                   Ωp
 *
 *     log4(K) = 1, works well for box filters
 *     K = 4
 *
 *             1
 *     Ωs = ---------, solid-angle of an important sample
 *           N * pdf
 *
 *              4 PI
 *     Ωp ~ --------------, solid-angle of a sample in the base cubemap
 *           texel_count
 *
 *
 * Evaluating the integral
 * -----------------------
 *
 * We are trying to evaluate the following integral:
 *
 *                     /
 *             Ed() =  | L(s) <n•l> ds
 *                     /
 *                     Ω
 *
 * For this, we're using importance sampling:
 *
 *                    1     L(l)
 *            Ed() = --- ∑ ------- <n•l>
 *                    N  l   pdf
 *
 *
 *  It results that:
 *
 *             1           PI
 *    Ed() = ---- ∑ L(l) ------  <n•l>
 *            N   l        n•l
 *
 *
 *  To avoid multiplying by 1/PI in the shader, we do it here, which simplifies to:
 *
 *  +----------------------+
 *  |          1           |
 *  |  Ed() = ---- ∑ L(l)  |
 *  |          N   l       |
 *  +----------------------+
 *
 */

/**
 * 漫反射辐照度计算实现
 * 
 * 使用重要性采样计算立方体贴图的漫反射辐照度。
 * 这是用于Lambertian漫反射的预过滤环境贴图。
 * 
 * 执行步骤：
 * 1. 预计算重要性采样方向（使用余弦加权采样）
 * 2. 计算每个采样的LOD级别（预过滤重要性采样）
 * 3. 对每个像素：
 *    - 构建围绕法线的旋转矩阵
 *    - 使用重要性采样方向在源立方体贴图中采样
 *    - 使用三线性过滤在Mipmap级别之间插值
 *    - 累加采样结果
 * 4. 平均采样结果（除以采样数）
 * 
 * 积分公式：
 * Ed() = (1/N) * Σ L(l)
 * 
 * 参考：
 * - "Real-time Shading with Filtered Importance Sampling", Jaroslav Krivanek
 * - "GPU-Based Importance Sampling, GPU Gems 3", Mark Colbert
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param levels 源立方体贴图Mipmap级别数组
 * @param maxNumSamples 最大采样数
 * @param updater 进度更新回调
 * @param userdata 用户数据指针
 */
void CubemapIBL::diffuseIrradiance(JobSystem& js, Cubemap& dst, const std::vector<Cubemap>& levels,
        size_t maxNumSamples, CubemapIBL::Progress updater, void* userdata)
{
    const float numSamples = maxNumSamples;
    const float inumSamples = 1.0f / numSamples;
    const size_t maxLevel = levels.size()-1;
    const float maxLevelf = maxLevel;
    const Cubemap& base(levels[0]);
    const size_t dim0 = base.getDimensions();
    const float omegaP = (4.0f * (float) F_PI) / float(6 * dim0 * dim0);

    std::atomic_uint progress = {0};

    struct CacheEntry {
        float3 L;
        float lerp;
        uint8_t l0;
        uint8_t l1;
    };

    std::vector<CacheEntry> cache;
    cache.reserve(maxNumSamples);

    // precompute everything that only depends on the sample #
    for (size_t sampleIndex = 0; sampleIndex < maxNumSamples; sampleIndex++) {
        // get Hammersley distribution for the half-sphere
        const float2 u = hammersley(uint32_t(sampleIndex), inumSamples);
        const float3 L = hemisphereCosSample(u);
        const float3 N = { 0, 0, 1 };
        const float NoL = dot(N, L);

        if (NoL > 0) {
            float pdf = NoL * (float) F_1_PI;

            constexpr float K = 4;
            const float omegaS = 1.0f / (numSamples * pdf);
            const float l = float(log4(omegaS) - log4(omegaP) + log4(K));
            const float mipLevel = clamp(float(l), 0.0f, maxLevelf);

            uint8_t l0 = uint8_t(mipLevel);
            uint8_t l1 = uint8_t(std::min(maxLevel, size_t(l0 + 1)));
            float lerp = mipLevel - (float) l0;

            cache.push_back({ L, lerp, l0, l1 });
        }
    }

    CubemapUtils::process<CubemapUtils::EmptyState>(dst, js,
            [&](CubemapUtils::EmptyState&, size_t y,
                    Cubemap::Face f, Cubemap::Texel* data, size_t dim) {

        if (updater) {
            size_t p = progress.fetch_add(1, std::memory_order_relaxed) + 1;
            updater(0, (float)p / ((float) dim * 6.0f), userdata);
        }

        mat3 R;
        const size_t numSamples = cache.size();
        for (size_t x = 0; x < dim; ++x, ++data) {
            const float2 p(Cubemap::center(x, y));
            const float3 N(dst.getDirectionFor(f, p.x, p.y));

            // center the cone around the normal (handle case of normal close to up)
            const float3 up = std::abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
            R[0] = normalize(cross(up, N));
            R[1] = cross(N, R[0]);
            R[2] = N;

            float3 Li = 0;
            for (size_t sample = 0; sample < numSamples; sample++) {
                const CacheEntry& e = cache[sample];
                const float3 L(R * e.L);
                const Cubemap& cmBase = levels[e.l0];
                const Cubemap& next = levels[e.l1];
                const float3 c0 = Cubemap::trilinearFilterAt(cmBase, next, e.lerp, L);
                Li += c0;
            }
            Cubemap::writeAt(data, Cubemap::Texel(Li * inumSamples));
        }
    });
}

/**
 * DFV项计算实现（不使用重要性采样，仅用于调试）
 * 
 * 计算DFG（Diffuse-Fresnel-Glossy）项，不使用重要性采样。
 * 用于验证重要性采样实现的正确性。
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param roughness 粗糙度
 * @param numSamples 采样数
 * @return DFV项（x=scale, y=bias）
 */
// Not importance-sampled
// 不使用重要性采样
static float2 UTILS_UNUSED DFV_NoIS(float NoV, float roughness, size_t numSamples) {
    float2 r = 0;
    const float linearRoughness = roughness * roughness;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereCosSample(u);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            // Note: remember VoH == LoH  (H is half vector)
            const float J = 1.0f / (4.0f * VoH);
            const float pdf = NoH / (float) F_PI;
            const float d = DistributionGGX(NoH, linearRoughness) * NoL / (pdf * J);
            const float Fc = pow5(1 - VoH);
            const float v = Visibility(NoV, NoL, linearRoughness);
            r.x += d * v * (1.0f - Fc);
            r.y += d * v * Fc;
        }
    }
    return r / numSamples;
}

/*
 *
 * Importance sampling GGX - Trowbridge-Reitz
 * ------------------------------------------
 *
 * Important samples are chosen to integrate Dggx() * cos(theta) over the hemisphere.
 *
 * All calculations are made in tangent space, with n = [0 0 1]
 *
 *                      h (important sample)
 *                     /.
 *                    / .
 *                   /  .
 *                  /   .
 *         --------o----+-------> n
 *                   cos(theta)
 *                    = n•h
 *
 *  h is micro facet's normal
 *  l is the reflection of v around h, l = reflect(-v, h)  ==>  v•h = l•h
 *
 *  n•v is given as an input parameter at runtime
 *
 *  Since n = [0 0 1], we also have v.z = n•v
 *
 *  Since we need to compute v•h, we chose v as below. This choice only affects the
 *  computation of v•h (and therefore the fresnel term too), but doesn't affect
 *  n•l, which only relies on l.z (which itself only relies on v.z, i.e.: n•v)
 *
 *      | sqrt(1 - (n•v)^2)     (sin)
 *  v = | 0
 *      | n•v                   (cos)
 *
 *
 *  h = important_sample_ggx()
 *
 *  l = reflect(-v, h) = 2 * v•h * h - v;
 *
 *  n•l = [0 0 1] • l = l.z
 *
 *  n•h = [0 0 1] • l = h.z
 *
 *
 *  pdf() = D(h) <n•h> |J(h)|
 *
 *               1
 *  |J(h)| = ----------
 *            4 <v•h>
 *
 *
 * Evaluating the integral
 * -----------------------
 *
 * We are trying to evaluate the following integral:
 *
 *                    /
 *             Er() = | fr(s) <n•l> ds
 *                    /
 *                    Ω
 *
 * For this, we're using importance sampling:
 *
 *                    1     fr(h)
 *            Er() = --- ∑ ------- <n•l>
 *                    N  h   pdf
 *
 * with:
 *
 *            fr() = D(h) F(h) V(v, l)
 *
 *
 *  It results that:
 *
 *            1                        4 <v•h>
 *    Er() = --- ∑ D(h) F(h) V(v, l) ------------ <n•l>
 *            N  h                     D(h) <n•h>
 *
 *
 *  +-------------------------------------------+
 *  |          4                  <v•h>         |
 *  |  Er() = --- ∑ F(h) V(v, l) ------- <n•l>  |
 *  |          N  h               <n•h>         |
 *  +-------------------------------------------+
 *
 */

/**
 * DFV项计算实现（使用重要性采样）
 * 
 * 计算DFG（Diffuse-Fresnel-Glossy）项，使用GGX分布的重要性采样。
 * 这是split-sum近似中的关键项，用于预计算BRDF的积分。
 * 
 * 执行步骤：
 * 1. 对每个采样：
 *    - 使用Hammersley序列生成均匀随机数
 *    - 使用GGX分布进行重要性采样得到半向量H
 *    - 计算光线方向L（反射）
 *    - 计算Fresnel项和可见性项
 *    - 分离f0和f90项（允许运行时重建）
 * 2. 平均所有采样结果
 * 
 * 结果：
 * - DFV.x = f0项的系数
 * - DFV.y = f90项的系数
 * - Er() = f0 * DFV.x + f90 * DFV.y
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param linearRoughness 线性粗糙度
 * @param numSamples 采样数
 * @return DFV项（x=f0系数, y=f90系数）
 */
static float2 DFV(float NoV, float linearRoughness, size_t numSamples) {
    float2 r = 0;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            /*
             * Fc = (1 - V•H)^5
             * F(h) = f0*(1 - Fc) + f90*Fc
             *
             * f0 and f90 are known at runtime, but thankfully can be factored out, allowing us
             * to split the integral in two terms and store both terms separately in a LUT.
             *
             * At runtime, we can reconstruct Er() exactly as below:
             *
             *            4                      <v•h>
             *   DFV.x = --- ∑ (1 - Fc) V(v, l) ------- <n•l>
             *            N  h                   <n•h>
             *
             *
             *            4                      <v•h>
             *   DFV.y = --- ∑ (    Fc) V(v, l) ------- <n•l>
             *            N  h                   <n•h>
             *
             *
             *   Er() = f0 * DFV.x + f90 * DFV.y
             *
             */
            const float v = Visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow5(1 - VoH);
            r.x += v * (1.0f - Fc);
            r.y += v * Fc;
        }
    }
    return r * (4.0f / numSamples);
}

/**
 * DFV项计算实现（多重散射版本）
 * 
 * 计算考虑多重散射的DFG项。
 * 多重散射模型考虑了光线在微表面之间的多次反射，提供更真实的外观。
 * 
 * 执行步骤：
 * 1. 对每个采样：
 *    - 使用GGX分布进行重要性采样
 *    - 计算Fresnel项和可见性项
 *    - 应用多重散射能量补偿
 * 2. 平均所有采样结果
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param linearRoughness 线性粗糙度
 * @param numSamples 采样数
 * @return DFV项（x=f0系数, y=f90系数）
 */
static float2 DFV_Multiscatter(float NoV, float linearRoughness, size_t numSamples) {
    float2 r = 0;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            const float v = Visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow5(1 - VoH);
            /*
             * Assuming f90 = 1
             *   Fc = (1 - V•H)^5
             *   F(h) = f0*(1 - Fc) + Fc
             *
             * f0 and f90 are known at runtime, but thankfully can be factored out, allowing us
             * to split the integral in two terms and store both terms separately in a LUT.
             *
             * At runtime, we can reconstruct Er() exactly as below:
             *
             *            4                <v•h>
             *   DFV.x = --- ∑ Fc V(v, l) ------- <n•l>
             *            N  h             <n•h>
             *
             *
             *            4                <v•h>
             *   DFV.y = --- ∑    V(v, l) ------- <n•l>
             *            N  h             <n•h>
             *
             *
             *   Er() = (1 - f0) * DFV.x + f0 * DFV.y
             *
             *        = mix(DFV.xxx, DFV.yyy, f0)
             *
             */
            r.x += v * Fc;
            r.y += v;
        }
    }
    return r * (4.0f / numSamples);
}

/**
 * DFV Lazanyi项计算实现（未使用）
 * 
 * 计算DFV的Lazanyi项，用于某些特殊的光照模型。
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param linearRoughness 线性粗糙度
 * @param numSamples 采样数
 * @return DFV Lazanyi项
 */
static float UTILS_UNUSED DFV_LazanyiTerm(float NoV, float linearRoughness, size_t numSamples) {
    float r = 0;
    const float cosThetaMax = (float) std::cos(81.7 * F_PI / 180.0);
    const float q = 1.0f / (cosThetaMax * pow6(1.0f - cosThetaMax));
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereImportanceSampleDggx(u, linearRoughness);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            const float v = Visibility(NoV, NoL, linearRoughness) * NoL * (VoH / NoH);
            const float Fc = pow6(1 - VoH);
            r += v * Fc * VoH * q;
        }
    }
    return r * (4.0f / numSamples);
}

/**
 * DFV Charlie分布均匀采样实现
 * 
 * 使用Charlie分布和均匀采样计算DFV项，用于布料材质。
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param linearRoughness 线性粗糙度
 * @param numSamples 采样数
 * @return DFV项
 */
static float DFV_Charlie_Uniform(float NoV, float linearRoughness, size_t numSamples) {
    float r = 0.0;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereUniformSample(u);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            const float v = VisibilityAshikhmin(NoV, NoL, linearRoughness);
            const float d = DistributionCharlie(NoH, linearRoughness);
            r += v * d * NoL * VoH; // VoH comes from the Jacobian, 1/(4*VoH)
        }
    }
    // uniform sampling, the PDF is 1/2pi, 4 comes from the Jacobian
    return r * (4.0f * 2.0f * (float) F_PI / numSamples);
}

/*
 *
 * Importance sampling Charlie
 * ---------------------------
 *
 * Important samples are chosen to integrate DCharlie() * cos(theta) over the hemisphere.
 *
 * All calculations are made in tangent space, with n = [0 0 1]
 *
 *                      h (important sample)
 *                     /.
 *                    / .
 *                   /  .
 *                  /   .
 *         --------o----+-------> n
 *                   cos(theta)
 *                    = n•h
 *
 *  h is micro facet's normal
 *  l is the reflection of v around h, l = reflect(-v, h)  ==>  v•h = l•h
 *
 *  n•v is given as an input parameter at runtime
 *
 *  Since n = [0 0 1], we also have v.z = n•v
 *
 *  Since we need to compute v•h, we chose v as below. This choice only affects the
 *  computation of v•h (and therefore the fresnel term too), but doesn't affect
 *  n•l, which only relies on l.z (which itself only relies on v.z, i.e.: n•v)
 *
 *      | sqrt(1 - (n•v)^2)     (sin)
 *  v = | 0
 *      | n•v                   (cos)
 *
 *
 *  h = hemisphereImportanceSampleDCharlie()
 *
 *  l = reflect(-v, h) = 2 * v•h * h - v;
 *
 *  n•l = [0 0 1] • l = l.z
 *
 *  n•h = [0 0 1] • l = h.z
 *
 *
 *  pdf() = DCharlie(h) <n•h> |J(h)|
 *
 *               1
 *  |J(h)| = ----------
 *            4 <v•h>
 *
 *
 * Evaluating the integral
 * -----------------------
 *
 * We are trying to evaluate the following integral:
 *
 *                    /
 *             Er() = | fr(s) <n•l> ds
 *                    /
 *                    Ω
 *
 * For this, we're using importance sampling:
 *
 *                    1     fr(h)
 *            Er() = --- ∑ ------- <n•l>
 *                    N  h   pdf
 *
 * with:
 *
 *            fr() = DCharlie(h) V(v, l)
 *
 *
 *  It results that:
 *
 *            1                          4 <v•h>
 *    Er() = --- ∑ DCharlie(h) V(v, l) ------------ <n•l>
 *            N  h                     DCharlie(h) <n•h>
 *
 *
 *  +---------------------------------------+
 *  |          4             <v•h>          |
 *  |  Er() = --- ∑ V(v, l) ------- <n•l>   |
 *  |          N  h          <n•h>          |
 *  +---------------------------------------+
 *
 */
/**
 * DFV Charlie分布重要性采样实现（未使用）
 * 
 * 使用Charlie分布和重要性采样计算DFV项，用于布料材质。
 * 
 * @param NoV 法线n和视线方向v的点积
 * @param linearRoughness 线性粗糙度
 * @param numSamples 采样数
 * @return DFV项
 */
static float UTILS_UNUSED DFV_Charlie_IS(float NoV, float linearRoughness, size_t numSamples) {
    float r = 0.0;
    const float3 V(std::sqrt(1 - NoV * NoV), 0, NoV);
    for (size_t i = 0; i < numSamples; i++) {
        const float2 u = hammersley(uint32_t(i), 1.0f / numSamples);
        const float3 H = hemisphereImportanceSampleDCharlie(u, linearRoughness);
        const float3 L = 2 * dot(V, H) * H - V;
        const float VoH = saturate(dot(V, H));
        const float NoL = saturate(L.z);
        const float NoH = saturate(H.z);
        if (NoL > 0) {
            const float J = 1.0f / (4.0f * VoH);
            const float pdf = NoH; // D has been removed as it cancels out in the previous equation
            const float v = VisibilityAshikhmin(NoV, NoL, linearRoughness);

            r += v * NoL / (pdf * J);
        }
    }
    return r / numSamples;
}

/**
 * BRDF计算实现
 * 
 * 计算给定粗糙度的BRDF值并存储到立方体贴图中。
 * 用于生成BRDF查找表或可视化BRDF函数。
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 获取像素对应的半向量H
 *    - 计算光线方向L（反射）
 *    - 计算BRDF项：D * F * V * NoL
 *    - 写入立方体贴图
 * 
 * BRDF公式：
 * BRDF = D(h) * F(h) * V(v, l) * <n•l>
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图（会被修改）
 * @param linearRoughness 线性粗糙度
 */
void CubemapIBL::brdf(utils::JobSystem& js, Cubemap& dst, float linearRoughness) {
    CubemapUtils::process<CubemapUtils::EmptyState>(dst, js,
            [ & ](CubemapUtils::EmptyState&, size_t y,
                    Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
                for (size_t x=0 ; x<dim ; ++x, ++data) {
                    const float2 p(Cubemap::center(x, y));
                    const float3 H(dst.getDirectionFor(f, p.x, p.y));  // 半向量
                    const float3 N = { 0, 0, 1 };  // 法线（切线空间）
                    const float3 V = N;  // 视线方向（等于法线）
                    const float3 L = 2 * dot(H, V) * H - V;  // 光线方向（反射）
                    const float NoL = dot(N, L);
                    const float NoH = dot(N, H);
                    const float NoV = dot(N, V);
                    const float LoH = dot(L, H);
                    float brdf_NoL = 0;
                    if (NoL > 0 && LoH > 0) {
                        // 计算BRDF项：D * F * V * NoL
                        const float D = DistributionGGX(NoH, linearRoughness);  // 分布函数
                        const float F = Fresnel(0.04f, 1.0f, LoH);  // 菲涅尔项（f0=0.04, f90=1.0）
                        const float V = Visibility(NoV, NoL, linearRoughness);  // 可见性项
                        brdf_NoL = float(D * F * V * NoL);
                    }
                    Cubemap::writeAt(data, Cubemap::Texel{ brdf_NoL });
                }
            });
}

/**
 * DFG查找表生成实现
 * 
 * 生成DFG（Diffuse-Fresnel-Glossy）查找表，用于split-sum近似。
 * 查找表存储了不同NoV和粗糙度组合的DFV项。
 * 
 * 执行步骤：
 * 1. 选择DFV函数（多重散射或标准版本）
 * 2. 对图像的每一行（粗糙度）：
 *    - 将Y坐标映射到线性粗糙度（使用平方映射）
 * 3. 对图像的每一列（NoV）：
 *    - 将X坐标映射到NoV值 [0, 1]
 *    - 计算DFV项
 *    - 如果启用布料模式，额外计算Charlie分布的DFV项
 *    - 存储到图像中
 * 
 * 查找表格式：
 * - R通道：f0项的系数（DFV.x）
 * - G通道：f90项的系数（DFV.y）
 * - B通道：Charlie分布的DFV项（如果启用布料模式）
 * 
 * @param js 作业系统
 * @param dst 目标图像（会被修改，存储DFG查找表）
 * @param multiscatter 是否使用多重散射模型
 * @param cloth 是否启用布料模式（使用Charlie分布）
 */
void CubemapIBL::DFG(JobSystem& js, Image& dst, bool multiscatter, bool cloth) {
    // 选择DFV函数（多重散射或标准版本）
    auto dfvFunction = multiscatter ? DFV_Multiscatter : DFV;
    // 使用多线程并行处理每一行
    auto job = jobs::parallel_for<char>(js, nullptr, nullptr, uint32_t(dst.getHeight()),
            [&dst, dfvFunction, cloth](char const* d, size_t c) {
                const size_t width = dst.getWidth();
                const size_t height = dst.getHeight();
                size_t y0 = size_t(d);
                for (size_t y = y0; y < y0 + c; y++) {
                    Cubemap::Texel* UTILS_RESTRICT data =
                            static_cast<Cubemap::Texel*>(dst.getPixelRef(0, y));

                    const float h = (float) height;
                    // 将Y坐标映射到线性粗糙度
                    const float coord = saturate((h - y + 0.5f) / h);
                    // map the coordinate in the texture to a linear_roughness,
                    // here we're using ^2, but other mappings are possible.
                    // ==> coord = sqrt(linear_roughness)
                    // 将坐标映射到纹理中的线性粗糙度，这里使用^2，但也可以使用其他映射
                    // ==> coord = sqrt(linear_roughness)
                    const float linear_roughness = coord * coord;
                    for (size_t x = 0; x < width; x++, data++) {
                        // 将X坐标映射到NoV值 [0, 1]
                        // const float NoV = float(x) / (width-1);
                        const float NoV = saturate((x + 0.5f) / width);
                        // 计算DFV项（f0和f90系数）
                        float3 r = { dfvFunction(NoV, linear_roughness, 1024), 0 };
                        if (cloth) {
                            // 如果启用布料模式，计算Charlie分布的DFV项
                            r.b = float(DFV_Charlie_Uniform(NoV, linear_roughness, 4096));
                        }
                        *data = r;
                    }
                }
            }, jobs::CountSplitter<1, 8>());
    js.runAndWait(job);
}

} // namespace ibl
} // namespace filament
