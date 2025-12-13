/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "details/IndirectLight.h"

#include "details/Engine.h"
#include "details/Texture.h"

#include "FilamentAPI-impl.h"

#include <backend/DriverEnums.h>
#include <filament/IndirectLight.h>

#include <utils/Panic.h>

#include <math/scalar.h>

#define IBL_INTEGRATION_PREFILTERED_CUBEMAP         0
#define IBL_INTEGRATION_IMPORTANCE_SAMPLING         1
#define IBL_INTEGRATION                             IBL_INTEGRATION_PREFILTERED_CUBEMAP

using namespace filament::math;

namespace filament {

// TODO: This should be a quality setting on View or LightManager
static constexpr bool CONFIG_IBL_USE_IRRADIANCE_MAP = false;

// ------------------------------------------------------------------------------------------------

/**
 * 构建器详情结构
 * 
 * 存储间接光的构建参数。
 */
struct IndirectLight::BuilderDetails {
    Texture const* mReflectionsMap = nullptr;  // 反射贴图指针（立方体贴图，用于镜面反射）
    Texture const* mIrradianceMap = nullptr;  // 辐照度贴图指针（立方体贴图，用于漫反射）
    /**
     * 辐照度球谐函数系数（9 个 float3）
     * 魔法值 65504.0f（fp16 最大值）表示球谐函数未设置
     */
    float3 mIrradianceCoefs[9] = { 65504.0f };  // magic value (max fp16) to indicate sh are not set
    mat3f mRotation = {};  // 旋转矩阵（用于旋转环境贴图）
    float mIntensity = FIndirectLight::DEFAULT_INTENSITY;  // 强度（默认太阳照度）
};

using BuilderType = IndirectLight;  // 构建器类型别名
BuilderType::Builder::Builder() noexcept = default;  // 默认构造函数
BuilderType::Builder::~Builder() noexcept = default;  // 析构函数
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;  // 拷贝构造函数
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;  // 移动构造函数
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;  // 拷贝赋值运算符
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;  // 移动赋值运算符

/**
 * 设置反射贴图
 * 
 * 设置用于镜面反射的立方体贴图。
 * 
 * @param cubemap 立方体贴图常量指针
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::reflections(Texture const* cubemap) noexcept {
    mImpl->mReflectionsMap = cubemap;  // 设置反射贴图
    return *this;  // 返回自身引用
}

/**
 * 设置辐照度（球谐函数版本）
 * 
 * 使用球谐函数系数设置辐照度。
 * 
 * @param bands 球谐函数带数（最多 3 带）
 * @param sh 球谐函数系数数组指针
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::irradiance(uint8_t bands, float3 const* sh) noexcept {
    /**
     * 限制为 3 带（目前只支持 3 带）
     */
    // clamp to 3 bands for now
    bands = std::min(bands, uint8_t(3));  // 限制带数
    size_t numCoefs = bands * bands;  // 计算系数数量（带数的平方）
    std::fill(std::begin(mImpl->mIrradianceCoefs), std::end(mImpl->mIrradianceCoefs), 0.0f);  // 清零所有系数
    std::copy_n(sh, numCoefs, std::begin(mImpl->mIrradianceCoefs));  // 复制系数
    return *this;  // 返回自身引用
}

/**
 * 设置辐射度（球谐函数版本）
 * 
 * 使用球谐函数系数设置辐射度，并转换为辐照度。
 * 系数来自 Peter-Pike Sloan 的"Stupid Spherical Harmonics (SH)"。
 * 
 * @param bands 球谐函数带数（最多 3 带）
 * @param sh 球谐函数系数数组指针（辐射度）
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::radiance(uint8_t bands, float3 const* sh) noexcept {
    /**
     * 球谐函数多项式形式的系数——这些来自
     * Peter-Pike Sloan 的"Stupid Spherical Harmonics (SH)"
     * 它们来自展开每个球谐函数的计算。
     *
     * 要渲染球谐函数，我们可以使用多项式形式，像这样：
     *          c += sh[0] * A[0];
     *          c += sh[1] * A[1] * s.y;
     *          c += sh[2] * A[2] * s.z;
     *          c += sh[3] * A[3] * s.x;
     *          c += sh[4] * A[4] * s.y * s.x;
     *          c += sh[5] * A[5] * s.y * s.z;
     *          c += sh[6] * A[6] * (3 * s.z * s.z - 1);
     *          c += sh[7] * A[7] * s.z * s.x;
     *          c += sh[8] * A[8] * (s.x * s.x - s.y * s.y);
     *
     * 为了在着色器中节省计算，我们预先将球谐函数系数乘以 A[i] 因子。
     * 此外，我们包含朗伯漫反射 BRDF 1/pi 和截断余弦。
     */
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
    // Additionally, we include the lambertian diffuse BRDF 1/pi and truncated cos.

    /**
     * 定义数学常数
     */
    constexpr float F_SQRT_PI = 1.7724538509f;  // sqrt(π)
    constexpr float F_SQRT_3  = 1.7320508076f;  // sqrt(3)
    constexpr float F_SQRT_5  = 2.2360679775f;  // sqrt(5)
    constexpr float F_SQRT_15 = 3.8729833462f;  // sqrt(15)
    constexpr float C[] = { F_PI, 2.0943951f, 0.785398f };  // <cos>（截断余弦的平均值）
    /**
     * 球谐函数系数 A[i]，包含：
     * - 球谐函数归一化因子
     * - 截断余弦平均值 C[i]
     * - 朗伯漫反射 BRDF 1/π
     */
    constexpr float A[] = {
                  1.0f / (2.0f * F_SQRT_PI) * C[0] * F_1_PI,    // 0  0
            -F_SQRT_3  / (2.0f * F_SQRT_PI) * C[1] * F_1_PI,    // 1 -1
             F_SQRT_3  / (2.0f * F_SQRT_PI) * C[1] * F_1_PI,    // 1  0
            -F_SQRT_3  / (2.0f * F_SQRT_PI) * C[1] * F_1_PI,    // 1  1
             F_SQRT_15 / (2.0f * F_SQRT_PI) * C[2] * F_1_PI,    // 2 -2
            -F_SQRT_15 / (2.0f * F_SQRT_PI) * C[2] * F_1_PI,    // 3 -1
             F_SQRT_5  / (4.0f * F_SQRT_PI) * C[2] * F_1_PI,    // 3  0
            -F_SQRT_15 / (2.0f * F_SQRT_PI) * C[2] * F_1_PI,    // 3  1
             F_SQRT_15 / (4.0f * F_SQRT_PI) * C[2] * F_1_PI     // 3  2
    };

    /**
     * 这是一种"记录"这些系数实际值的方法，同时
     * 确保表达式和值始终保持同步。
     */
    // this is a way to "document" the actual value of these coefficients and at the same
    // time make sure the expression and values are always in sync.
    struct Debug {  // 调试结构
        /**
         * 检查两个浮点数是否几乎相等
         */
        static constexpr bool almost(float const a, float const b) {
            constexpr float e = 1e-6f;  // 误差容限
            return (a > b - e) && (a < b + e);  // 检查是否在误差范围内
        }
    };
    /**
     * 静态断言：验证系数值
     */
    static_assert(Debug::almost(A[0],  0.282095f), "coefficient mismatch");  // 验证系数 0
    static_assert(Debug::almost(A[1], -0.325735f), "coefficient mismatch");  // 验证系数 1
    static_assert(Debug::almost(A[2],  0.325735f), "coefficient mismatch");  // 验证系数 2
    static_assert(Debug::almost(A[3], -0.325735f), "coefficient mismatch");  // 验证系数 3
    static_assert(Debug::almost(A[4],  0.273137f), "coefficient mismatch");  // 验证系数 4
    static_assert(Debug::almost(A[5], -0.273137f), "coefficient mismatch");  // 验证系数 5
    static_assert(Debug::almost(A[6],  0.078848f), "coefficient mismatch");  // 验证系数 6
    static_assert(Debug::almost(A[7], -0.273137f), "coefficient mismatch");  // 验证系数 7
    static_assert(Debug::almost(A[8],  0.136569f), "coefficient mismatch");  // 验证系数 8

    /**
     * 将辐射度转换为辐照度
     */
    float3 irradiance[9];  // 辐照度系数数组
    bands = std::min(bands, uint8_t(3));  // 限制带数
    for (size_t i = 0, c = bands * bands; i<c; ++i) {  // 遍历系数
        irradiance[i] = sh[i] * A[i];  // 乘以系数（包含 BRDF 和截断余弦）
    }
    return this->irradiance(bands, irradiance);  // 调用辐照度设置方法
}

/**
 * 设置辐照度贴图
 * 
 * 设置用于漫反射的辐照度立方体贴图。
 * 
 * @param cubemap 立方体贴图常量指针
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::irradiance(Texture const* cubemap) noexcept {
    mImpl->mIrradianceMap = cubemap;  // 设置辐照度贴图
    return *this;  // 返回自身引用
}

/**
 * 设置强度
 * 
 * 设置间接光的强度。
 * 
 * @param envIntensity 环境强度
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::intensity(float const envIntensity) noexcept {
    mImpl->mIntensity = envIntensity;  // 设置强度
    return *this;  // 返回自身引用
}

/**
 * 设置旋转
 * 
 * 设置环境贴图的旋转矩阵。
 * 
 * @param rotation 旋转矩阵
 * @return 构建器引用（支持链式调用）
 */
IndirectLight::Builder& IndirectLight::Builder::rotation(mat3f const& rotation) noexcept {
    mImpl->mRotation = rotation;  // 设置旋转矩阵
    return *this;  // 返回自身引用
}

/**
 * 构建间接光
 * 
 * 根据构建器配置创建间接光。
 * 
 * @param engine 引擎引用
 * @return 间接光指针
 */
IndirectLight* IndirectLight::Builder::build(Engine& engine) {
    if (mImpl->mReflectionsMap) {  // 如果有反射贴图
        FILAMENT_CHECK_PRECONDITION(  // 检查是否为立方体贴图
                mImpl->mReflectionsMap->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
                << "reflection map must a cubemap";

        /**
         * 如果使用重要性采样，生成 Mipmap
         */
        if constexpr (IBL_INTEGRATION == IBL_INTEGRATION_IMPORTANCE_SAMPLING) {  // 如果使用重要性采样
            mImpl->mReflectionsMap->generateMipmaps(engine);  // 生成 Mipmap
        }
    }

    if (mImpl->mIrradianceMap) {  // 如果有辐照度贴图
        FILAMENT_CHECK_PRECONDITION(  // 检查是否为立方体贴图
                mImpl->mIrradianceMap->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
                << "irradiance map must a cubemap";
    }

    return downcast(engine).createIndirectLight(*this);  // 调用引擎的创建方法
}

// ------------------------------------------------------------------------------------------------

/**
 * 间接光构造函数
 * 
 * 创建间接光对象，初始化反射贴图、辐照度数据和旋转。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FIndirectLight::FIndirectLight(FEngine& engine, const Builder& builder) noexcept {
    if (builder->mReflectionsMap) {  // 如果有反射贴图
        mReflectionsTexture = downcast(builder->mReflectionsMap);  // 转换反射贴图
        mLevelCount = builder->mReflectionsMap->getLevels();  // 获取 Mip 级别数量
    }

    /**
     * 复制辐照度球谐函数系数
     */
    std::copy(
            std::begin(builder->mIrradianceCoefs),  // 源开始
            std::end(builder->mIrradianceCoefs),  // 源结束
            mIrradianceCoefs.begin());  // 目标开始

    mRotation = builder->mRotation;  // 设置旋转矩阵
    mIntensity = builder->mIntensity;  // 设置强度
    if (builder->mIrradianceMap) {  // 如果有辐照度贴图
        mIrradianceTexture = downcast(builder->mIrradianceMap);  // 转换辐照度贴图
    } else {  // 否则
        /**
         * TODO: 如果需要，生成辐照度贴图，这是引擎配置
         */
        // TODO: if needed, generate the irradiance map, this is an engine config
        if (CONFIG_IBL_USE_IRRADIANCE_MAP) {  // 如果配置使用辐照度贴图
            // 生成辐照度贴图的代码（目前为空）
        }
    }
}

/**
 * 终止间接光
 * 
 * 释放间接光资源。
 * 
 * @param engine 引擎引用
 */
void FIndirectLight::terminate(FEngine& engine) {
    if (CONFIG_IBL_USE_IRRADIANCE_MAP) {  // 如果配置使用辐照度贴图
        FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
        driver.destroyTexture(getIrradianceHwHandle());  // 销毁辐照度贴图
    }
}

/**
 * 获取反射贴图硬件句柄
 * 
 * @return 反射贴图硬件句柄（如果没有则返回空句柄）
 */
backend::Handle<backend::HwTexture> FIndirectLight::getReflectionHwHandle() const noexcept {
    return mReflectionsTexture ? mReflectionsTexture->getHwHandleForSampling()  // 如果有纹理则返回句柄
                               : backend::Handle<backend::HwTexture>{};  // 否则返回空句柄
}

/**
 * 获取辐照度贴图硬件句柄
 * 
 * @return 辐照度贴图硬件句柄（如果没有则返回空句柄）
 */
backend::Handle<backend::HwTexture> FIndirectLight::getIrradianceHwHandle() const noexcept {
    return mIrradianceTexture ? mIrradianceTexture->getHwHandleForSampling()  // 如果有纹理则返回句柄
                              : backend::Handle<backend::HwTexture>{};  // 否则返回空句柄
}

/**
 * 获取方向估计（静态方法）
 * 
 * 从球谐函数系数估计主要光照方向。
 * 
 * 线性方向计算为 normalize(-sh[3], -sh[1], sh[2])，但我们存储的系数
 * 已经预先归一化，所以负号消失。
 * 注意：我们只在混合后归一化方向，这与在其他地方使用的代码匹配——
 * 向量的长度在某种程度上与该方向上的强度相关。
 * 
 * @param f 球谐函数系数数组指针（9 个 float3）
 * @return 主要光照方向（归一化）
 */
float3 FIndirectLight::getDirectionEstimate(float3 const* f) noexcept {
    /**
     * 线性方向计算为 normalize(-sh[3], -sh[1], sh[2])，但我们存储的系数
     * 已经预先归一化，所以负号消失。
     * 注意：我们只在混合后归一化方向，这与在其他地方使用的代码匹配——
     * 向量的长度在某种程度上与该方向上的强度相关。
     */
    // The linear direction is found as normalize(-sh[3], -sh[1], sh[2]), but the coefficients
    // we store are already pre-normalized, so the negative sign disappears.
    // Note: we normalize the directions only after blending, this matches code used elsewhere --
    // the length of the vector is somewhat related to the intensity in that direction.
    float3 r = float3{ f[3].r, f[1].r, f[2].r };  // 红色通道方向向量
    float3 g = float3{ f[3].g, f[1].g, f[2].g };  // 绿色通道方向向量
    float3 b = float3{ f[3].b, f[1].b, f[2].b };  // 蓝色通道方向向量
    /**
     * 我们假设有一个单一的白光。
     * 使用 RGB 到亮度的权重（ITU-R BT.709）计算亮度方向。
     */
    // We're assuming there is a single white light.
    return -normalize(r * 0.2126f + g * 0.7152f + b * 0.0722f);  // 归一化并取反（ITU-R BT.709 权重）
}

/**
 * 获取颜色估计（静态方法）
 * 
 * 从球谐函数系数估计指定方向的光照颜色。
 * 
 * 参考：https://www.gamasutra.com/view/news/129689/Indepth_Extracting_dominant_light_from_Spherical_Harmonics.php
 * 
 * @param Le 球谐函数系数数组指针（9 个 float3，预先卷积和缩放的环境系数）
 * @param direction 方向向量（归一化）
 * @return 光照颜色（RGBA，其中 A 是强度）
 */
float4 FIndirectLight::getColorEstimate(float3 const* Le, float3 direction) noexcept {
    /**
     * 参考：https://www.gamasutra.com/view/news/129689/Indepth_Extracting_dominant_light_from_Spherical_Harmonics.php
     *
     * 注意 Le 是我们预先卷积、预先缩放的球谐函数系数（环境）
     */
    // See: https://www.gamasutra.com/view/news/129689/Indepth_Extracting_dominant_light_from_Spherical_Harmonics.php

    // note Le is our pre-convolved, pre-scaled SH coefficients for the environment

    /**
     * 首先获取方向
     */
    // first get the direction
    const float3 s = -direction;  // 取反方向

    /**
     * 一个通道上的光强度由下式给出：dot(Ld, Le) / dot(Ld, Ld)
     */

    /**
     * 方向光的球谐函数系数，预先缩放 1/A[i]
     * （我们预先缩放 1/A[i] 以撤销 Le 的 A[i] 预缩放）
     */
    // The light intensity on one channel is given by: dot(Ld, Le) / dot(Ld, Ld)

    // SH coefficients of the directional light pre-scaled by 1/A[i]
    // (we pre-scale by 1/A[i] to undo Le's pre-scaling by A[i]
    const float Ld[9] = {
            1.0f,  // L0,0
            s.y, s.z, s.x,  // L1,-1, L1,0, L1,1
            s.y * s.x, s.y * s.z, (3 * s.z * s.z - 1), s.z * s.x, (s.x * s.x - s.y * s.y)  // L2,-2 到 L2,2
    };

    /**
     * dot(Ld, Le) —— 注意这等价于在光方向上"采样"球面；
     * 这与着色器中用于球谐函数重建的代码完全相同。
     */
    // dot(Ld, Le) -- notice that this is equivalent to "sampling" the sphere in the light
    // direction; this is the exact same code used in the shader for SH reconstruction.
    float3 LdDotLe = Ld[0] * Le[0]  // 计算点积
                   + Ld[1] * Le[1] + Ld[2] * Le[2] + Ld[3] * Le[3]
                   + Ld[4] * Le[4] + Ld[5] * Le[5] + Ld[6] * Le[6] + Ld[7] * Le[7] + Ld[8] * Le[8];

    /**
     * 下面的缩放因子在 gamasutra 文章中解释，但它似乎
     * 导致光的强度过低。
     */
    // The scale factor below is explained in the gamasutra article above, however it seems
    // to cause the intensity of the light to be too low.
    //      constexpr float c = (16.0f * F_PI / 17.0f);
    //      constexpr float LdSquared = (9.0f / (4.0f * F_PI)) * c * c;
    //      LdDotLe *= c / LdSquared; // Note the final coefficient is 17/36

    /**
     * 我们乘以 π，因为我们的球谐函数系数包含 1/π 朗伯漫反射 BRDF。
     */
    // We multiply by PI because our SH coefficients contain the 1/PI lambertian BRDF.
    LdDotLe *= F_PI;  // 乘以 π

    /**
     * 确保我们没有负强度
     */
    // Make sure we don't have negative intensities
    LdDotLe = max(LdDotLe, float3{0});  // 限制为非负

    const float intensity = max(LdDotLe);  // 计算最大强度
    return { LdDotLe / intensity, intensity };  // 返回归一化颜色和强度
}

float3 FIndirectLight::getDirectionEstimate() const noexcept {
    return getDirectionEstimate(mIrradianceCoefs.data());
}

float4 FIndirectLight::getColorEstimate(float3 const direction) const noexcept {
   return getColorEstimate(mIrradianceCoefs.data(), direction);
}

} // namespace filament
