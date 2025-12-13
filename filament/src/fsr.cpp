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

#include "fsr.h"

#include <math/vec4.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

namespace filament {

using namespace math;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wignored-qualifiers"
#endif

#define A_CPU  1
#include "materials/fsr/ffx_a.h"
#define FSR_EASU_F 1
#define FSR_RCAS_F 1
#include "materials/fsr/ffx_fsr1.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/**
 * FSR 缩放设置
 * 
 * 设置 FSR EASU 阶段的统一变量。
 * 
 * 注意：FsrEasu API 声称它需要左上偏移，但这在 OpenGL 中不成立，
 * 在这种情况下它使用左下偏移。
 * 
 * @param outUniforms 输出统一变量
 * @param config 缩放配置
 */
void FSR_ScalingSetup(FSRUniforms* outUniforms, FSRScalingConfig config) noexcept {
    /**
     * 计算 Y 偏移
     * 
     * 不同后端使用不同的坐标系统：
     * - OpenGL：左下角为原点
     * - Metal/Vulkan/WebGPU：左上角为原点
     */
    auto yoffset = config.input.bottom;
    if (config.backend == backend::Backend::METAL || config.backend == backend::Backend::VULKAN ||
            config.backend == backend::Backend::WEBGPU) {
        /**
         * 对于使用左上角为原点的后端，需要转换 Y 坐标
         */
        yoffset = config.inputHeight - (config.input.bottom + config.input.height);
    }

    /**
     * 调用 FSR EASU 常量设置函数
     * 
     * 参数：
     * - 视口大小（左上对齐）在要缩放的输入图像中
     * - 输入图像的大小
     * - 输出分辨率
     * - 输入图像偏移
     */
    FsrEasuConOffset( outUniforms->EasuCon0.v, outUniforms->EasuCon1.v,
                outUniforms->EasuCon2.v, outUniforms->EasuCon3.v,
            // 视口大小（左上对齐）在要缩放的输入图像中
            config.input.width, config.input.height,
            // 输入图像的大小
            config.inputWidth, config.inputHeight,
            // 输出分辨率
            config.outputWidth, config.outputHeight,
            // 输入图像偏移
            config.input.left, yoffset);
}

/**
 * FSR 锐化设置
 * 
 * 设置 FSR RCAS 阶段的统一变量。
 * 
 * @param outUniforms 输出统一变量
 * @param config 锐化配置
 */
void FSR_SharpeningSetup(FSRUniforms* outUniforms, FSRSharpeningConfig config) noexcept {
    FsrRcasCon(outUniforms->RcasCon.v, config.sharpness);
}

} // namespace filament


