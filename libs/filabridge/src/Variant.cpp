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

#include <private/filament/Variant.h>

#include <filament/MaterialEnums.h>

#include <utils/Slice.h>

#include <array>

#include <stddef.h>
#include <stdint.h>

namespace filament {

/**
 * 根据用户变体过滤器掩码过滤变体
 * @param variant 原始变体
 * @param filterMask 用户变体过滤器掩码
 * @return 过滤后的变体
 */
Variant Variant::filterUserVariant(
        Variant variant, UserVariantFilterMask filterMask) noexcept {
    // these are easy to filter by just removing the corresponding bit - 这些很容易通过移除对应位来过滤
    // 如果过滤器包含方向光，移除方向光位
    if (filterMask & (uint32_t)UserVariantFilterBit::DIRECTIONAL_LIGHTING) {
        variant.key &= ~DIR;
    }
    // 如果过滤器包含动态光源，移除动态光源位
    if (filterMask & (uint32_t)UserVariantFilterBit::DYNAMIC_LIGHTING) {
        variant.key &= ~DYN;
    }
    // 如果过滤器包含蒙皮，移除蒙皮位
    if (filterMask & (uint32_t)UserVariantFilterBit::SKINNING) {
        variant.key &= ~SKN;
    }
    // 如果过滤器包含立体渲染，移除立体渲染位
    if (filterMask & (uint32_t)UserVariantFilterBit::STE) {
        variant.key &= ~(filterMask & STE);
    }
    // 如果不是深度变体，可以移除雾效位
    if (!isValidDepthVariant(variant)) {
        // we can't remove FOG from depth variants, this would, in fact, remove picking - 我们不能从深度变体中移除FOG，这实际上会移除拾取
        if (filterMask & (uint32_t)UserVariantFilterBit::FOG) {
            variant.key &= ~FOG;
        }
    } else {
        // depth variants can have their VSM bit filtered - 深度变体可以过滤其VSM位
        if (filterMask & (uint32_t)UserVariantFilterBit::VSM) {
            variant.key &= ~VSM;
        }
    }
    // 如果不是SSR变体，可以移除阴影接收和VSM位
    if (!isSSRVariant(variant)) {
        // SSR variant needs to be handled separately - SSR变体需要单独处理
        if (filterMask & (uint32_t)UserVariantFilterBit::SHADOW_RECEIVER) {
            variant.key &= ~SRE;
        }
        if (filterMask & (uint32_t)UserVariantFilterBit::VSM) {
            variant.key &= ~VSM;
        }
    } else {
        // see if we need to filter out the SSR variants - 查看是否需要过滤掉SSR变体
        if (filterMask & (uint32_t)UserVariantFilterBit::SSR) {
            variant.key &= ~SPECIAL_SSR;
        }
    }
    return variant;
}

namespace details {

namespace {

/**
 * Compile-time variant count for lit and unlit - 编译时计算有光照和无光照变体数量
 * @param lit 是否为有光照材质
 * @return 变体数量
 */
constexpr inline size_t variant_count(bool lit) noexcept {
    size_t count = 0;
    // 遍历所有可能的变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        Variant variant(i);
        // 跳过无效变体
        if (!Variant::isValid(variant)) {
            continue;
        }
        // 根据材质类型过滤变体
        variant = Variant::filterVariant(variant, lit);
        // 如果过滤后键值未改变，说明该变体有效
        if (i == variant.key) {
            count++;
        }
    }
    return count;
}

/**
 * 编译时计算深度变体数量
 * @return 深度变体数量
 */
constexpr inline size_t depth_variant_count() noexcept {
    size_t count = 0;
    // 遍历所有可能的变体，统计有效的深度变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        Variant const variant(i);
        if (Variant::isValidDepthVariant(variant)) {
            count++;
        }
    }
    return count;
}

/**
 * Compile-time variant list for lit and unlit - 编译时生成有光照和无光照变体列表
 * @tparam LIT 是否为有光照材质
 * @return 变体数组
 */
template<bool LIT>
constexpr auto get_variants() noexcept {
    std::array<Variant, variant_count(LIT)> variants;
    size_t count = 0;
    // 遍历所有可能的变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        Variant variant(i);
        // 跳过保留的变体
        if (Variant::isReserved(variant)) {
            continue;
        }
        // 根据材质类型过滤变体
        variant = Variant::filterVariant(variant, LIT);
        // 如果过滤后键值未改变，添加到列表中
        if (i == variant.key) {
            variants[count++] = variant;
        }
    }
    return variants;
}

/**
 * 编译时生成深度变体列表
 * @return 深度变体数组
 */
constexpr auto get_depth_variants() noexcept {
    std::array<Variant, depth_variant_count()> variants;
    size_t count = 0;
    // 遍历所有可能的变体，收集有效的深度变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        Variant const variant(i);
        if (Variant::isValidDepthVariant(variant)) {
            variants[count++] = variant;
        }
    }
    return variants;
}

/**
 * Below are compile time sanity-check tests - 以下是编译时完整性检查测试
 * 检查保留的变体不是有效变体（有效和保留应该互斥）
 */
constexpr inline bool reserved_is_not_valid() noexcept {
    // 遍历所有变体，确保有效和保留互斥
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        const Variant variant(i);
        bool const is_valid = Variant::isValid(variant);
        bool const is_reserved = Variant::isReserved(variant);
        // 如果同时有效和保留（或同时无效和非保留），返回false
        if (is_valid == is_reserved) {
            return false;
        }
    }
    return true;
}

/**
 * 编译时计算保留变体数量
 * @return 保留变体数量
 */
constexpr inline size_t reserved_variant_count() noexcept {
    size_t count = 0;
    // 遍历所有变体，统计保留的变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        const Variant variant(i);
        if (Variant::isReserved(variant)) {
            count++;
        }
    }
    return count;
}

/**
 * 编译时计算有效变体数量
 * @return 有效变体数量
 */
constexpr inline size_t valid_variant_count() noexcept {
    size_t count = 0;
    // 遍历所有变体，统计有效变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        const Variant variant(i);
        if (Variant::isValid(variant)) {
            count++;
        }
    }
    return count;
}

/**
 * 编译时计算顶点着色器变体数量
 * @return 顶点着色器变体数量
 */
constexpr inline size_t vertex_variant_count() noexcept {
    size_t count = 0;
    // 遍历所有变体，统计有效的顶点着色器变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        const Variant variant(i);
        if (Variant::isValid(variant)) {
            if (Variant::isVertexVariant(variant)) {
                count++;
            }
        }
    }
    return count;
}

/**
 * 编译时计算片段着色器变体数量
 * @return 片段着色器变体数量
 */
constexpr inline size_t fragment_variant_count() noexcept {
    size_t count = 0;
    // 遍历所有变体，统计有效的片段着色器变体
    for (size_t i = 0; i < VARIANT_COUNT; i++) {
        const Variant variant(i);
        if (Variant::isValid(variant)) {
            if (Variant::isFragmentVariant(variant)) {
                count++;
            }
        }
    }
    return count;
}

} // anonymous namespace

// 有光照变体列表（编译时生成）
static auto const gLitVariants{ details::get_variants<true>() };
// 无光照变体列表（编译时生成）
static auto const gUnlitVariants{ details::get_variants<false>() };
// 深度变体列表（编译时生成）
static auto const gDepthVariants{ details::get_depth_variants() };

// 编译时断言：检查保留变体和有效变体互斥
static_assert(reserved_is_not_valid());
// 编译时断言：保留变体数量应为160
static_assert(reserved_variant_count() == 160);
// 编译时断言：有效变体数量应为96
static_assert(valid_variant_count() == 96);
// 编译时断言：顶点着色器变体数量应为36（32 - 4 + 8 - 0）
static_assert(vertex_variant_count() == 32 - (4 + 0) + 8 - 0);        // 36
// 编译时断言：片段着色器变体数量应为24（33 - (2 + 2 + 8) + 4 - 1）
static_assert(fragment_variant_count() == 33 - (2 + 2 + 8) + 4 - 1);    // 24

} // namespace details


namespace VariantUtils {

/**
 * 获取有光照变体列表
 * @return 有光照变体的切片
 */
utils::Slice<const Variant> getLitVariants() noexcept {
    return { details::gLitVariants.data(), details::gLitVariants.size() };
}

/**
 * 获取无光照变体列表
 * @return 无光照变体的切片
 */
utils::Slice<const Variant> getUnlitVariants() noexcept {
    return { details::gUnlitVariants.data(), details::gUnlitVariants.size() };
}

/**
 * 获取深度变体列表
 * @return 深度变体的切片
 */
utils::Slice<const Variant> getDepthVariants() noexcept {
    return { details::gDepthVariants.data(), details::gDepthVariants.size() };
}

}; // VariantUtils

} // namespace filament
