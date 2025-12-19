/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_FILABRIDGE_SIBSTRUCTS_H
#define TNT_FILABRIDGE_SIBSTRUCTS_H

#include <stdint.h>
#include <stddef.h>

namespace filament {

/**
 * 每视图采样器接口块结构
 * 定义每个视图使用的采样器索引常量
 */
struct PerViewSib {
    // indices of each sampler in this SamplerInterfaceBlock (see: getPerViewSib()) - 此SamplerInterfaceBlock中每个采样器的索引
    static constexpr size_t SHADOW_MAP     = 0;     // user defined (1024x1024) DEPTH, array - 阴影贴图：用户定义(1024x1024)深度纹理数组
    static constexpr size_t IBL_DFG_LUT    = 1;     // user defined (128x128), RGB16F - IBL DFG查找表：用户定义(128x128)RGB16F
    static constexpr size_t IBL_SPECULAR   = 2;     // user defined, user defined, CUBEMAP - IBL镜面反射：用户定义立方体贴图
    static constexpr size_t SSAO           = 3;     // variable, RGB8 {AO, [depth]} - 屏幕空间环境光遮蔽：可变，RGB8{AO, [depth]}
    static constexpr size_t SSR            = 4;     // variable, RGB_11_11_10, mipmapped - 屏幕空间反射：可变，RGB_11_11_10，带Mipmap
    static constexpr size_t STRUCTURE      = 5;     // variable, DEPTH - 结构纹理：可变，深度纹理
    static constexpr size_t FOG            = 6;     // variable, user defined, CUBEMAP - 雾效：可变，用户定义立方体贴图

    static constexpr size_t SAMPLER_COUNT  = 7;     // 采样器数量
};

/**
 * 每个渲染图元变形采样器接口块结构
 * 定义变形目标使用的采样器索引常量
 */
struct PerRenderPrimitiveMorphingSib {
    static constexpr size_t POSITIONS      = 0;     // 位置纹理索引
    static constexpr size_t TANGENTS       = 1;     // 切线纹理索引

    static constexpr size_t SAMPLER_COUNT  = 2;     // 采样器数量
};

/**
 * 每个渲染图元蒙皮采样器接口块结构
 * 定义骨骼蒙皮使用的采样器索引常量
 */
struct PerRenderPrimitiveSkinningSib {
    static constexpr size_t BONE_INDICES_AND_WEIGHTS = 0;   //bone indices and weights - 骨骼索引和权重纹理索引

    static constexpr size_t SAMPLER_COUNT  = 1;     // 采样器数量
};

} // namespace filament

#endif //TNT_FILABRIDGE_SIBSTRUCTS_H
