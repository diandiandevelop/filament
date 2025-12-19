/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMAT_MATERIAL_VARIANTS_H
#define TNT_FILAMAT_MATERIAL_VARIANTS_H

#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>

#include <vector>

namespace filamat {

// 材质变体结构：表示一个着色器变体和其对应的着色器阶段
// @param variant 着色器变体（包含各种渲染选项的组合）
// @param stage 着色器阶段（顶点、片段或计算）
struct Variant {
    using Stage = filament::backend::ShaderStage;
    // 构造函数：初始化变体和阶段
    // @param v 着色器变体
    // @param s 着色器阶段
    Variant(filament::Variant v, Stage s) noexcept : variant(v), stage(s) {}
    filament::Variant variant;  // 着色器变体
    Stage stage;                // 着色器阶段
};

// 确定表面材质的变体列表（根据用户过滤掩码、是否光照、是否有阴影倍增器）
// @param userVariantFilter 用户变体过滤掩码（用于过滤不需要的变体）
// @param isLit 是否为光照材质
// @param shadowMultiplier 是否有阴影倍增器
// @return 变体列表
std::vector<Variant> determineSurfaceVariants(
        filament::UserVariantFilterMask, bool isLit, bool shadowMultiplier);

// 确定后处理材质的变体列表
// @return 后处理变体列表（包含所有后处理变体）
std::vector<Variant> determinePostProcessVariants();

// 确定计算材质的变体列表
// @return 计算变体列表（目前只有一个计算变体）
std::vector<Variant> determineComputeVariants();

} // namespace filamat

#endif
