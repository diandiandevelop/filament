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

#ifndef TNT_FILAMENT_TEXTURESAMPLER_H
#define TNT_FILAMENT_TEXTURESAMPLER_H

#include <backend/DriverEnums.h>

#include <utils/compiler.h>

#include <math.h>

#include <stdint.h>

namespace filament {

/**
 * TextureSampler defines how a texture is accessed.
 */
/**
 * TextureSampler 定义如何访问纹理。
 */
class UTILS_PUBLIC TextureSampler {
public:
    using WrapMode = backend::SamplerWrapMode;      //!< 纹理坐标包装模式
    using MinFilter = backend::SamplerMinFilter;    //!< 纹理缩小过滤模式
    using MagFilter = backend::SamplerMagFilter;    //!< 纹理放大过滤模式
    using CompareMode = backend::SamplerCompareMode; //!< 纹理比较模式
    using CompareFunc = backend::SamplerCompareFunc; //!< 纹理比较函数

    /**
     * Creates a default sampler.
     * The default parameters are:
     * - filterMag      : NEAREST
     * - filterMin      : NEAREST
     * - wrapS          : CLAMP_TO_EDGE
     * - wrapT          : CLAMP_TO_EDGE
     * - wrapR          : CLAMP_TO_EDGE
     * - compareMode    : NONE
     * - compareFunc    : Less or equal
     * - no anisotropic filtering
     */
    /**
     * 创建默认采样器。
     * 默认参数为：
     * - filterMag      : NEAREST（最近邻）
     * - filterMin      : NEAREST（最近邻）
     * - wrapS          : CLAMP_TO_EDGE（边缘钳制）
     * - wrapT          : CLAMP_TO_EDGE（边缘钳制）
     * - wrapR          : CLAMP_TO_EDGE（边缘钳制）
     * - compareMode    : NONE（无比较）
     * - compareFunc    : Less or equal（小于等于）
     * - 无各向异性过滤
     */
    TextureSampler() noexcept = default;

    /**
     * 使用给定的采样器参数创建 TextureSampler
     * @param params 采样器参数
     */
    explicit TextureSampler(backend::SamplerParams params) noexcept : mSamplerParams(params) { }

    TextureSampler(const TextureSampler& rhs) noexcept = default;
    TextureSampler& operator=(const TextureSampler& rhs) noexcept = default;

    /**
     * Creates a TextureSampler with the default parameters but setting the filtering and wrap modes.
     * @param minMag filtering for both minification and magnification
     * @param str wrapping mode for all texture coordinate axes
     */
    /**
     * 创建 TextureSampler，使用默认参数但设置过滤和包装模式。
     * @param minMag 缩小和放大的过滤模式
     * @param str 所有纹理坐标轴的包装模式
     */
    explicit TextureSampler(MagFilter minMag, WrapMode str = WrapMode::CLAMP_TO_EDGE) noexcept  {
        mSamplerParams.filterMin = MinFilter(minMag);
        mSamplerParams.filterMag = minMag;
        mSamplerParams.wrapS = str;
        mSamplerParams.wrapT = str;
        mSamplerParams.wrapR = str;
    }

    /**
     * Creates a TextureSampler with the default parameters but setting the filtering and wrap modes.
     * @param min filtering for minification
     * @param mag filtering for magnification
     * @param str wrapping mode for all texture coordinate axes
     */
    /**
     * 创建 TextureSampler，使用默认参数但设置过滤和包装模式。
     * @param min 缩小的过滤模式
     * @param mag 放大的过滤模式
     * @param str 所有纹理坐标轴的包装模式
     */
    TextureSampler(MinFilter min, MagFilter mag, WrapMode str = WrapMode::CLAMP_TO_EDGE) noexcept  {
        mSamplerParams.filterMin = min;
        mSamplerParams.filterMag = mag;
        mSamplerParams.wrapS = str;
        mSamplerParams.wrapT = str;
        mSamplerParams.wrapR = str;
    }

    /**
     * Creates a TextureSampler with the default parameters but setting the filtering and wrap modes.
     * @param min filtering for minification
     * @param mag filtering for magnification
     * @param s wrap mode for the s (horizontal)texture coordinate
     * @param t wrap mode for the t (vertical) texture coordinate
     * @param r wrap mode for the r (depth) texture coordinate
     */
    /**
     * 创建 TextureSampler，使用默认参数但设置过滤和包装模式。
     * @param min 缩小的过滤模式
     * @param mag 放大的过滤模式
     * @param s s（水平）纹理坐标的包装模式
     * @param t t（垂直）纹理坐标的包装模式
     * @param r r（深度）纹理坐标的包装模式
     */
    TextureSampler(MinFilter min, MagFilter mag, WrapMode s, WrapMode t, WrapMode r) noexcept  {
        mSamplerParams.filterMin = min;
        mSamplerParams.filterMag = mag;
        mSamplerParams.wrapS = s;
        mSamplerParams.wrapT = t;
        mSamplerParams.wrapR = r;
    }

    /**
     * Creates a TextureSampler with the default parameters but setting the compare mode and function
     * @param mode Compare mode
     * @param func Compare function
     */
    /**
     * 创建 TextureSampler，使用默认参数但设置比较模式和函数
     * @param mode 比较模式
     * @param func 比较函数
     */
    explicit TextureSampler(CompareMode mode, CompareFunc func = CompareFunc::LE) noexcept  {
        mSamplerParams.compareMode = mode;
        mSamplerParams.compareFunc = func;
    }

    /**
     * Sets the minification filter
     * @param v Minification filter
     */
    /**
     * 设置缩小过滤模式
     * @param v 缩小过滤模式
     */
    void setMinFilter(MinFilter v) noexcept {
        mSamplerParams.filterMin = v;
    }

    /**
     * Sets the magnification filter
     * @param v Magnification filter
     */
    /**
     * 设置放大过滤模式
     * @param v 放大过滤模式
     */
    void setMagFilter(MagFilter v) noexcept {
        mSamplerParams.filterMag = v;
    }

    /**
     * Sets the wrap mode for the s (horizontal) texture coordinate
     * @param v wrap mode
     */
    /**
     * 设置 s（水平）纹理坐标的包装模式
     * @param v 包装模式
     */
    void setWrapModeS(WrapMode v) noexcept {
        mSamplerParams.wrapS = v;
    }

    /**
     * Sets the wrap mode for the t (vertical) texture coordinate
     * @param v wrap mode
     */
    /**
     * 设置 t（垂直）纹理坐标的包装模式
     * @param v 包装模式
     */
    void setWrapModeT(WrapMode v) noexcept {
        mSamplerParams.wrapT = v;
    }

    /**
     * Sets the wrap mode for the r (depth, for 3D textures) texture coordinate
     * @param v wrap mode
     */
    /**
     * 设置 r（深度，用于 3D 纹理）纹理坐标的包装模式
     * @param v 包装模式
     */
    void setWrapModeR(WrapMode v) noexcept {
        mSamplerParams.wrapR = v;
    }

    /**
     * This controls anisotropic filtering.
     * @param anisotropy Amount of anisotropy, should be a power-of-two. The default is 1.
     *                   The maximum permissible value is 128.
     */
    /**
     * 控制各向异性过滤。
     * @param anisotropy 各向异性量，应该是 2 的幂。默认值为 1。
     *                   最大允许值为 128。
     */
    void setAnisotropy(float anisotropy) noexcept {
        const int log2 = ilogbf(anisotropy > 0 ? anisotropy : -anisotropy);
        mSamplerParams.anisotropyLog2 = uint8_t(log2 < 7 ? log2 : 7);
    }

    /**
     * Sets the compare mode and function.
     * @param mode Compare mode
     * @param func Compare function
     */
    /**
     * 设置比较模式和函数。
     * @param mode 比较模式
     * @param func 比较函数
     */
    void setCompareMode(CompareMode mode, CompareFunc func = CompareFunc::LE) noexcept {
        mSamplerParams.compareMode = mode;
        mSamplerParams.compareFunc = func;
    }

    //! returns the minification filter value
    /**
     * 返回缩小过滤模式值
     */
    MinFilter getMinFilter() const noexcept { return mSamplerParams.filterMin; }

    //! returns the magnification filter value
    /**
     * 返回放大过滤模式值
     */
    MagFilter getMagFilter() const noexcept { return mSamplerParams.filterMag; }

    //! returns the s-coordinate wrap mode (horizontal)
    /**
     * 返回 s 坐标的包装模式（水平）
     */
    WrapMode getWrapModeS() const noexcept  { return mSamplerParams.wrapS; }

    //! returns the t-coordinate wrap mode (vertical)
    /**
     * 返回 t 坐标的包装模式（垂直）
     */
    WrapMode getWrapModeT() const noexcept  { return mSamplerParams.wrapT; }

    //! returns the r-coordinate wrap mode (depth)
    /**
     * 返回 r 坐标的包装模式（深度）
     */
    WrapMode getWrapModeR() const noexcept  { return mSamplerParams.wrapR; }

    //! returns the anisotropy value
    /**
     * 返回各向异性值
     */
    float getAnisotropy() const noexcept { return float(1u << mSamplerParams.anisotropyLog2); }

    //! returns the compare mode
    /**
     * 返回比较模式
     */
    CompareMode getCompareMode() const noexcept { return mSamplerParams.compareMode; }

    //! returns the compare function
    /**
     * 返回比较函数
     */
    CompareFunc getCompareFunc() const noexcept { return mSamplerParams.compareFunc; }


    // no user-serviceable parts below...
    backend::SamplerParams getSamplerParams() const noexcept  { return mSamplerParams; }

private:
    backend::SamplerParams mSamplerParams{};
};

} // namespace filament

#endif // TNT_FILAMENT_TEXTURESAMPLER_H
