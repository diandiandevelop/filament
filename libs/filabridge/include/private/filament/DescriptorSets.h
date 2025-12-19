/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DESCRIPTORSETS_H
#define TNT_FILAMENT_DESCRIPTORSETS_H

#include <backend/DriverEnums.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <filament/MaterialEnums.h>

#include <utils/CString.h>

namespace filament::descriptor_sets {

// 获取深度变体的描述符集布局
backend::DescriptorSetLayout const& getDepthVariantLayout() noexcept;
// 获取SSR变体的描述符集布局
backend::DescriptorSetLayout const& getSsrVariantLayout() noexcept;

// 获取每个可渲染对象的描述符集布局
backend::DescriptorSetLayout const& getPerRenderableLayout() noexcept;

/**
 * 获取每视图描述符集布局
 * @param domain 材质域
 * @param isLit 是否为有光照材质
 * @param isSSR 是否使用屏幕空间反射
 * @param hasFog 是否有雾效
 * @param isVSM 是否使用方差阴影贴图
 * @return 描述符集布局
 */
backend::DescriptorSetLayout getPerViewDescriptorSetLayout(
        MaterialDomain domain,
        bool isLit, bool isSSR, bool hasFog,
        bool isVSM) noexcept;

/**
 * 根据变体获取每视图描述符集布局
 * @param variant 着色器变体
 * @param domain 材质域
 * @param isLit 是否为有光照材质
 * @param isSSR 是否使用屏幕空间反射
 * @param hasFog 是否有雾效
 * @return 描述符集布局
 */
backend::DescriptorSetLayout getPerViewDescriptorSetLayoutWithVariant(
        Variant variant,
        MaterialDomain domain,
        bool isLit, bool isSSR, bool hasFog) noexcept;

/**
 * 获取描述符名称
 * @param set 描述符集绑定点
 * @param binding 绑定索引
 * @return 描述符名称
 */
utils::CString getDescriptorName(
        DescriptorSetBindingPoints set,
        backend::descriptor_binding_t binding) noexcept;

/**
 * 根据采样器类型和格式获取描述符类型
 * @param type 采样器类型
 * @param format 采样器格式
 * @return 描述符类型
 */
backend::DescriptorType getDescriptorType(backend::SamplerType type, backend::SamplerFormat format);

} // namespace filament::descriptor_sets


#endif //TNT_FILAMENT_DESCRIPTORSETS_H
