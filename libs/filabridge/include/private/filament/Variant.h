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

#ifndef TNT_FILABRIDGE_VARIANT_H
#define TNT_FILABRIDGE_VARIANT_H

#include <filament/MaterialEnums.h>

#include <utils/compiler.h>
#include <utils/bitset.h>
#include <utils/Slice.h>

#include <stdint.h>
#include <stddef.h>

namespace filament {
// Variant 使用 8 位掩码，支持 256 种不同的变体组合
static constexpr size_t VARIANT_BITS = 8;
static constexpr size_t VARIANT_COUNT = 1 << VARIANT_BITS;

// VariantList 用于跟踪哪些变体已被使用（位集合）
using VariantList = utils::bitset<uint64_t, VARIANT_COUNT / 64>;

// Variant 结构：表示着色器变体的位掩码
// 重要：添加新变体时，必须更新 filterVariant() 函数
// 同时更新 CommonWriter.cpp 中的 formatVariantString
struct Variant {
    using type_t = uint8_t;

    Variant() noexcept = default;
    Variant(Variant const& rhs) noexcept = default;
    Variant& operator=(Variant const& rhs) noexcept = default;
    constexpr explicit Variant(type_t key) noexcept : key(key) { }


    // Variant 位掩码说明：
    // DIR: Directional Lighting（方向光）- 位 0
    // DYN: Dynamic Lighting（动态光源：点光源、聚光灯、面光源）- 位 1
    // SRE: Shadow Receiver（阴影接收）- 位 2
    // SKN: Skinning（蒙皮/变形）- 位 3
    // DEP: Depth only（仅深度）- 位 4
    // FOG: Fog（雾效）- 位 5（标准变体）
    // PCK: Picking（拾取）- 位 5（深度变体）
    // VSM: Variance shadow maps（方差阴影贴图，深度变体）/ sampler type（采样器类型，标准变体）- 位 6
    // STE: Instanced stereo rendering（实例化立体渲染）- 位 7
    //
    //   X: 可以是 1 或 0
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    // Variant              | STE | VSM | FOG | DEP | SKN | SRE | DYN | DIR |   256 种组合
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    //                                    PCK（深度变体时使用此位）
    //
    // 标准变体（Standard variants，DEP=0）：
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    //                      | STE | VSM | FOG |  0  | SKN | SRE | DYN | DIR |    128 - 44 = 84 个有效变体
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    //      Vertex shader      X     0     0     0     X     X     X     X  （顶点着色器关心的位）
    //    Fragment shader      0     X     X     0     0     X     X     X  （片段着色器关心的位）
    //       Fragment SSR      0     1     0     0     0     1     0     0  （屏幕空间反射变体）
    //           Reserved      X     1     1     0     X     1     0     0      [ -4]（保留变体）
    //           Reserved      X     0     X     0     X     1     0     0      [ -8]（保留变体）
    //           Reserved      X     1     X     0     X     0     X     X      [-32]（保留变体）
    //
    // 深度变体（Depth variants，DEP=1）：
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    //                      | STE | VSM | PCK |  1  | SKN |  0  |  0  |  0  |   16 - 4 = 12 个有效变体
    //                      +-----+-----+-----+-----+-----+-----+-----+-----+
    //       Vertex depth      X     X     0     1     X     0     0     0  （顶点深度着色器关心的位）
    //     Fragment depth      0     0     X     1     0     0     0     0  （片段深度着色器，无 VSM）
    //     Fragment depth      0     1     0     1     0     0     0     0  （片段深度着色器，有 VSM）
    //           Reserved      X     1     1     1     X     0     0     0     [  -4]（保留变体）
    //
    // 总计：96 个有效变体，160 个保留变体（256 - 96）
    //
    // 注意：一个有效的变体可能既不是有效的顶点变体，也不是有效的片段变体
    //       （例如：FOG|SKN 变体），这些位会被 filterVariantVertex() 和
    //       filterVariantFragment() 适当地过滤

    type_t key = 0u;  // 8 位变体键值

    // 变体位掩码常量定义
    // 注意：添加更多位时，需要更新 FRenderer::CommandKey::draw::materialVariant
    // 注意：添加更多位时，需要更新 VARIANT_COUNT
    static constexpr type_t DIR   = 0x01; // 方向光存在（每帧/世界位置）
    static constexpr type_t DYN   = 0x02; // 点光源、聚光灯或面光源存在（每帧/世界位置）
    static constexpr type_t SRE   = 0x04; // 接收阴影（每个 Renderable）
    static constexpr type_t SKN   = 0x08; // GPU 蒙皮和/或变形
    static constexpr type_t DEP   = 0x10; // 仅深度变体
    static constexpr type_t FOG   = 0x20; // 雾效（标准变体）
    static constexpr type_t PCK   = 0x20; // 拾取（深度变体，与 FOG 共享位）
    static constexpr type_t VSM   = 0x40; // 方差阴影贴图（深度变体）/ 采样器类型（标准变体）
    static constexpr type_t STE   = 0x80; // 实例化立体渲染

    // special variants (variants that use the reserved space)
    static constexpr type_t SPECIAL_SSR   = VSM | SRE; // screen-space reflections variant

    static constexpr type_t STANDARD_MASK      = DEP;
    static constexpr type_t STANDARD_VARIANT   = 0u;

    // the depth variant deactivates all variants that make no sense when writing the depth
    // only -- essentially, all fragment-only variants.
    static constexpr type_t DEPTH_MASK         = DEP | SRE | DYN | DIR;
    static constexpr type_t DEPTH_VARIANT      = DEP;

    // this mask filters out the lighting variants
    static constexpr type_t UNLIT_MASK         = STE | SKN | FOG;

    // returns raw variant bits
    bool hasDirectionalLighting() const noexcept { return key & DIR; }
    bool hasDynamicLighting() const noexcept     { return key & DYN; }
    bool hasSkinningOrMorphing() const noexcept  { return key & SKN; }
    bool hasStereo() const noexcept              { return key & STE; }

    void setDirectionalLighting(bool v) noexcept { set(v, DIR); }
    void setDynamicLighting(bool v) noexcept     { set(v, DYN); }
    void setShadowReceiver(bool v) noexcept      { set(v, SRE); }
    void setSkinning(bool v) noexcept            { set(v, SKN); }
    void setFog(bool v) noexcept                 { set(v, FOG); }
    void setPicking(bool v) noexcept             { set(v, PCK); }
    void setVsm(bool v) noexcept                 { set(v, VSM); }
    void setStereo(bool v) noexcept              { set(v, STE); }

    static constexpr bool isValidDepthVariant(Variant variant) noexcept {
        // Can't have VSM and PICKING together with DEPTH variants
        constexpr type_t RESERVED_MASK  = VSM | PCK | DEP | SRE | DYN | DIR;
        constexpr type_t RESERVED_VALUE = VSM | PCK | DEP;
        return ((variant.key & DEPTH_MASK) == DEPTH_VARIANT) &&
               ((variant.key & RESERVED_MASK) != RESERVED_VALUE);
   }

    static constexpr bool isValidStandardVariant(Variant variant) noexcept {
        // can't have shadow receiver if we don't have any lighting
        constexpr type_t RESERVED0_MASK  = VSM | FOG | SRE | DYN | DIR;
        constexpr type_t RESERVED0_VALUE = VSM | FOG | SRE;

        // can't have shadow receiver if we don't have any lighting
        constexpr type_t RESERVED1_MASK  = VSM | SRE | DYN | DIR;
        constexpr type_t RESERVED1_VALUE = SRE;

        // can't have VSM without shadow receiver
        constexpr type_t RESERVED2_MASK  = VSM | SRE;
        constexpr type_t RESERVED2_VALUE = VSM;

        return ((variant.key & STANDARD_MASK) == STANDARD_VARIANT) &&
               ((variant.key & RESERVED0_MASK) != RESERVED0_VALUE) &&
               ((variant.key & RESERVED1_MASK) != RESERVED1_VALUE) &&
               ((variant.key & RESERVED2_MASK) != RESERVED2_VALUE);
    }

    static constexpr bool isVertexVariant(Variant variant) noexcept {
        return filterVariantVertex(variant) == variant;
    }

    static constexpr bool isFragmentVariant(Variant variant) noexcept {
        return filterVariantFragment(variant) == variant;
    }

    static constexpr bool isReserved(Variant variant) noexcept {
        return !isValid(variant);
    }

    static constexpr bool isValid(Variant variant) noexcept {
        return isValidStandardVariant(variant) || isValidDepthVariant(variant);
    }

    static constexpr bool isSSRVariant(Variant variant) noexcept {
        return (variant.key & (STE | VSM | DEP | SRE | DYN | DIR)) == (VSM | SRE);
    }

    static constexpr bool isVSMVariant(Variant variant) noexcept {
        return !isSSRVariant(variant) && ((variant.key & VSM) == VSM);
    }

    static constexpr bool isShadowReceiverVariant(Variant variant) noexcept {
        return !isSSRVariant(variant) && ((variant.key & SRE) == SRE);
    }

    static constexpr bool isFogVariant(Variant variant) noexcept {
        return (variant.key & (FOG | DEP)) == FOG;
    }

    static constexpr bool isPickingVariant(Variant variant) noexcept {
        return (variant.key & (PCK | DEP)) == (PCK | DEP);
    }

    static constexpr bool isStereoVariant(Variant variant) noexcept {
        return (variant.key & STE) == STE;
    }

    // 过滤出顶点着色器需要的变体位
    // 过滤掉顶点着色器不需要的位（例如：雾效不影响顶点着色器）
    static constexpr Variant filterVariantVertex(Variant variant) noexcept {
        if ((variant.key & STANDARD_MASK) == STANDARD_VARIANT) {
            // 标准变体：顶点着色器只关心立体、蒙皮、阴影接收、动态光源、方向光
            if (isSSRVariant(variant)) {
                // SSR 变体特殊处理：移除 VSM 和 SRE 位（SSR 使用特殊变体）
                variant.key &= ~(VSM | SRE);
            }
            return variant & (STE | SKN | SRE | DYN | DIR);
        }
        if ((variant.key & DEPTH_MASK) == DEPTH_VARIANT) {
            // 深度变体：只有 VSM、蒙皮和立体影响顶点着色器的深度变体
            return variant & (STE | VSM | SKN | DEP);
        }
        return {};
    }

    // 过滤出片段着色器需要的变体位
    // 过滤掉片段着色器不需要的位（例如：蒙皮不影响片段着色器）
    static constexpr Variant filterVariantFragment(Variant variant) noexcept {
        if ((variant.key & STANDARD_MASK) == STANDARD_VARIANT) {
            // 标准变体：片段着色器只关心 VSM、雾效、阴影接收、动态光源、方向光
            return variant & (VSM | FOG | SRE | DYN | DIR);
        }
        if ((variant.key & DEPTH_MASK) == DEPTH_VARIANT) {
            // 深度变体：只有 VSM 和 PICKING 影响片段着色器的深度变体
            return variant & (VSM | PCK | DEP);
        }
        return {};
    }

    // 根据材质是否被光照过滤变体
    // variant: 原始变体
    // isLit: 材质是否为 Lit（光照）材质
    // 返回：过滤后的变体（移除不需要的位）
    static constexpr Variant filterVariant(Variant variant, bool isLit) noexcept {
        // 深度变体的特殊情况
        if (isValidDepthVariant(variant)) {
            if (!isLit) {
                // 如果是 Unlit 材质，永远不需要 VSM 变体
                return variant & ~VSM;
            }
            return variant;
        }
        // SSR 变体保持不变
        if (isSSRVariant(variant)) {
            return variant;
        }
        if (!isLit) {
            // 当着色模式为 Unlit 时，移除所有光照相关的变体
            // UNLIT_MASK = STE | SKN | FOG（只保留立体、蒙皮、雾效）
            return variant & UNLIT_MASK;
        }
        // 如果阴影接收被禁用，关闭 VSM
        if (!(variant.key & SRE)) {
            return variant & ~VSM;
        }
        return variant;
    }

    constexpr bool operator==(Variant rhs) const noexcept {
        return key == rhs.key;
    }

    constexpr bool operator!=(Variant rhs) const noexcept {
        return key != rhs.key;
    }

    constexpr Variant operator & (type_t rhs) const noexcept {
        return Variant(key & rhs);
    }

    static Variant filterUserVariant(
            Variant variant, UserVariantFilterMask filterMask) noexcept;

private:
    void set(bool v, type_t mask) noexcept {
        key = (key & ~mask) | (v ? mask : type_t(0));
    }
};

namespace VariantUtils {
// list of lit variants
utils::Slice<const Variant> getLitVariants() noexcept UTILS_PURE;
// list of unlit variants
utils::Slice<const Variant> getUnlitVariants() noexcept UTILS_PURE;
// list of depth variants
utils::Slice<const Variant> getDepthVariants() noexcept UTILS_PURE;
}

} // namespace filament

#endif // TNT_FILABRIDGE_VARIANT_H
