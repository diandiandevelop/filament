/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "MaterialVariants.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>

#include <filament/MaterialEnums.h>

#include <vector>

namespace filamat {

// 确定表面材质的变体列表（根据用户过滤掩码、是否光照、是否有阴影倍增器）
// @param userVariantFilter 用户变体过滤掩码（用于过滤不需要的变体）
// @param isLit 是否为光照材质
// @param shadowMultiplier 是否有阴影倍增器
// @return 变体列表
std::vector<Variant> determineSurfaceVariants(
        filament::UserVariantFilterMask userVariantFilter, bool isLit, bool shadowMultiplier) {
    std::vector<Variant> variants;
    // 步骤1：遍历所有可能的变体
    for (size_t k = 0; k < filament::VARIANT_COUNT; k++) {
        filament::Variant const variant(k);
        // 步骤2：跳过保留的变体
        if (filament::Variant::isReserved(variant)) {
            continue;
        }

        // 步骤3：应用用户变体过滤
        filament::Variant filteredVariant =
                filament::Variant::filterUserVariant(variant, userVariantFilter);

        // Remove variants for unlit materials
        // 步骤4：对于非光照材质，移除不需要的变体（除非有阴影倍增器）
        filteredVariant = filament::Variant::filterVariant(
                filteredVariant, isLit || shadowMultiplier);

        // 步骤5：检查顶点着色器变体
        auto const vertexVariant = filament::Variant::filterVariantVertex(filteredVariant);
        if (vertexVariant == variant) {
            variants.emplace_back(variant, filament::backend::ShaderStage::VERTEX);
        }

        // 步骤6：检查片段着色器变体
        auto const fragmentVariant = filament::Variant::filterVariantFragment(filteredVariant);
        if (fragmentVariant == variant) {
            variants.emplace_back(variant, filament::backend::ShaderStage::FRAGMENT);
        }
    }
    return variants;
}

// 确定后处理材质的变体列表
// @return 后处理变体列表（包含所有后处理变体）
std::vector<Variant> determinePostProcessVariants() {
    std::vector<Variant> variants;
    // TODO: add a way to filter out post-process variants (e.g., the transparent variant if only
    // opaque is needed)
    // TODO: 添加过滤后处理变体的方法（例如，如果只需要不透明变体，则过滤透明变体）
    // 步骤1：遍历所有后处理变体
    for (filament::Variant::type_t k = 0; k < filament::POST_PROCESS_VARIANT_COUNT; k++) {
        filament::Variant const variant(k);
        // 步骤2：为每个变体添加顶点和片段着色器阶段
        variants.emplace_back(variant, filament::backend::ShaderStage::VERTEX);
        variants.emplace_back(variant, filament::backend::ShaderStage::FRAGMENT);
    }
    return variants;
}

// 确定计算材质的变体列表
// @return 计算变体列表（目前只有一个计算变体）
std::vector<Variant> determineComputeVariants() {
    // TODO: should we have variants for compute shaders?
    // TODO: 计算着色器是否应该有变体？
    std::vector<Variant> variants;
    // 步骤1：创建默认计算变体（索引0）
    filament::Variant const variant(0);
    // 步骤2：添加计算着色器阶段
    variants.emplace_back(variant, filament::backend::ShaderStage::COMPUTE);
    return variants;
}

} // namespace filamat
