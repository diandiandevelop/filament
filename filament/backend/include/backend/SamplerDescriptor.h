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

//! \file

#ifndef TNT_FILAMENT_BACKEND_SAMPLERDESCRIPTOR_H
#define TNT_FILAMENT_BACKEND_SAMPLERDESCRIPTOR_H

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>

namespace filament::backend {

/**
 * 采样器描述符
 * 
 * 采样器描述符用于将纹理和采样参数组合在一起，描述如何在着色器中采样纹理。
 * 
 * 用途：
 * - 绑定纹理和采样器状态
 * - 在描述符堆中使用
 * - 传递给渲染命令
 * 
 * 组成：
 * - t: 纹理句柄，指向要采样的纹理资源
 * - s: 采样参数，包含过滤、包装、各向异性等设置
 */
struct UTILS_PUBLIC SamplerDescriptor {
    /**
     * Texture handle
     * 
     * 纹理句柄
     * - 指向要采样的纹理资源
     * - 必须是有效的纹理句柄
     */
    Handle<HwTexture> t;
    
    /**
     * Sampler parameters
     * 
     * 采样器参数
     * - 包含过滤模式（min/mag filter）
     * - 包装模式（wrap S/T/R）
     * - 各向异性级别
     * - 比较模式（用于深度纹理）
     * - 默认值：所有参数使用默认设置
     */
    SamplerParams s{};
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_SAMPLERDESCRIPTOR_H
