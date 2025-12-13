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

#ifndef TNT_FILAMENT_FSR_H
#define TNT_FILAMENT_FSR_H

#include <filament/Viewport.h>

#include <math/vec4.h>

#include <stdint.h>

namespace filament {

/**
 * FSR 缩放配置
 * 
 * FSR (FidelityFX Super Resolution) 是 AMD 的开源超分辨率技术。
 * 用于将低分辨率图像上采样到高分辨率。
 */
struct FSRScalingConfig {
    backend::Backend backend;  // 后端类型（用于处理坐标系统差异）
    Viewport input;  // 要缩放的源区域
    uint32_t inputWidth;  // 源宽度
    uint32_t inputHeight;  // 源高度
    uint32_t outputWidth;  // 目标宽度
    uint32_t outputHeight;  // 目标高度
};

/**
 * FSR 锐化配置
 * 
 * FSR 的 RCAS (Robust Contrast Adaptive Sharpening) 阶段用于锐化图像。
 */
struct FSRSharpeningConfig {
    /**
     * 锐度值
     * 
     * 范围：{0.0 := 最大锐度，到 N>0，其中 N 是锐度减半的档数}
     * 
     * 例如：
     * - 0.0 = 最大锐度
     * - 1.0 = 锐度减半一次
     * - 2.0 = 锐度减半两次
     */
    float sharpness;
};

/**
 * FSR 统一变量
 * 
 * 包含 FSR 着色器所需的常量。
 */
struct FSRUniforms {
    math::float4 EasuCon0;  // EASU (Edge Adaptive Spatial Upsampling) 常量 0
    math::float4 EasuCon1;  // EASU 常量 1
    math::float4 EasuCon2;  // EASU 常量 2
    math::float4 EasuCon3;  // EASU 常量 3
    math::uint4 RcasCon;  // RCAS (Robust Contrast Adaptive Sharpening) 常量
};

/**
 * FSR 缩放设置
 * 
 * 设置 FSR EASU 阶段的统一变量。
 * 
 * @param inoutUniforms 输入/输出统一变量
 * @param config 缩放配置
 */
void FSR_ScalingSetup(FSRUniforms* inoutUniforms, FSRScalingConfig config) noexcept;

/**
 * FSR 锐化设置
 * 
 * 设置 FSR RCAS 阶段的统一变量。
 * 
 * @param inoutUniforms 输入/输出统一变量
 * @param config 锐化配置
 */
void FSR_SharpeningSetup(FSRUniforms* inoutUniforms, FSRSharpeningConfig config) noexcept;

} // namespace filament

#endif // TNT_FILAMENT_FSR_H
