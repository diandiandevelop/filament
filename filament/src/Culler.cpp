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

#include "Culler.h"

#include <filament/Box.h>

#include <math/fast.h>

using namespace filament::math;

// use 8 if Culler::result_type is 8-bits, on ARMv8 it allows the compiler to write eight
// results in one go.
#define FILAMENT_CULLER_VECTORIZE_HINT 4

namespace filament {

static_assert(Culler::MODULO % FILAMENT_CULLER_VECTORIZE_HINT == 0,
        "MODULO m=must be a multiple of FILAMENT_CULLER_VECTORIZE_HINT");

// 批量剔除：球体数组
// 对每个球体，检查它是否与视锥体的所有 6 个平面相交
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float4 const* UTILS_RESTRICT b,      // 球体数组（xyz=中心，w=半径）
        size_t count) noexcept {

    float4 const * const UTILS_RESTRICT planes = frustum.mPlanes;

    // 向上舍入到 MODULO（8）的倍数，以支持 SIMD 向量化
    count = round(count);
#if defined(__clang__)
    // 提示编译器使用 SIMD 向量化（一次处理 4 个对象）
    #pragma clang loop vectorize_width(FILAMENT_CULLER_VECTORIZE_HINT)
#endif
    for (size_t i = 0; i < count; i++) {
        int visible = ~0;  // 初始化为全 1（可见）
        float4 const sphere(b[i]);

#if defined(__clang__)
        // 完全展开内层循环（6 个平面），减少循环开销
        #pragma clang loop unroll(full)
#endif
        for (size_t j = 0; j < 6; j++) {
            // 计算球心到平面的距离（减去半径）
            // 注意：clang 似乎不会生成向量*标量指令，这会导致寄存器压力增加和栈溢出
            const float dot = planes[j].x * sphere.x +
                              planes[j].y * sphere.y +
                              planes[j].z * sphere.z +
                              planes[j].w - sphere.w;  // 减去半径
            // 如果 dot < 0，球体在平面外侧（不可见）
            // 使用 signbit 避免分支，提高性能
            visible &= fast::signbit(dot);
        }
        results[i] = result_type(visible);
    }
}

// 批量剔除：AABB 数组
// 对每个 AABB，检查它是否与视锥体的所有 6 个平面相交
// 使用分离轴定理（Separating Axis Theorem）计算 AABB 到平面的最近距离
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float3 const* UTILS_RESTRICT center,      // AABB 中心数组
        float3 const* UTILS_RESTRICT extent,      // AABB 半边长数组
        size_t count, 
        size_t const bit) noexcept {              // 结果掩码位（用于多级可见性）

    float4 const * UTILS_RESTRICT const planes = frustum.mPlanes;

    // 向上舍入到 MODULO（8）的倍数，以支持 SIMD 向量化
    count = round(count);
#if defined(__clang__)
    // 提示编译器使用 SIMD 向量化（一次处理 4 个对象）
    #pragma clang loop vectorize_width(FILAMENT_CULLER_VECTORIZE_HINT)
#endif
    for (size_t i = 0; i < count; i++) {
        int visible = ~0;  // 初始化为全 1（可见）

#if defined(__clang__)
        // 完全展开内层循环（6 个平面），减少循环开销
        #pragma clang loop unroll(full)
#endif
        for (size_t j = 0; j < 6; j++) {
            // 计算 AABB 到平面的最近距离（使用分离轴定理）
            // 注意：clang 似乎不会生成向量*标量指令，这会导致寄存器压力增加和栈溢出
            const float dot =
                    planes[j].x * center[i].x - std::abs(planes[j].x) * extent[i].x +
                    planes[j].y * center[i].y - std::abs(planes[j].y) * extent[i].y +
                    planes[j].z * center[i].z - std::abs(planes[j].z) * extent[i].z +
                    planes[j].w;
            
            // 如果 dot < 0，AABB 在平面外侧（不可见）
            // 使用 signbit 避免分支，并将结果左移到指定位
            visible &= fast::signbit(dot) << bit;
        }

        // 更新结果掩码：清除对应位，然后设置可见性
        auto r = results[i];
        r &= ~result_type(1u << bit);  // 清除对应位
        r |= result_type(visible);      // 设置可见性
        results[i] = r;
    }
}

/*
 * returns whether a box intersects with the frustum
 */

bool Culler::intersects(Frustum const& frustum, Box const& box) noexcept {
    // The main intersection routine assumes multiples of 8 items
    float3 centers[MODULO];
    float3 extents[MODULO];
    result_type results[MODULO];
    centers[0] = box.center;
    extents[0] = box.halfExtent;
    intersects(results, frustum, centers, extents, MODULO, 0);
    return bool(results[0] & 1);
}

/*
 * returns whether a sphere intersects with the frustum
 */
bool Culler::intersects(Frustum const& frustum, float4 const& sphere) noexcept {
    // The main intersection routine assumes multiples of 8 items
    float4 spheres[MODULO];
    result_type results[MODULO];
    spheres[0] = sphere;
    intersects(results, frustum, spheres, MODULO);
    return bool(results[0] & 1);
}

// For testing...

void Culler::Test::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float3 const* UTILS_RESTRICT c,
        float3 const* UTILS_RESTRICT e,
        size_t const count) noexcept {
    Culler::intersects(results, frustum, c, e, count, 0);
}

void Culler::Test::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float4 const* UTILS_RESTRICT b, size_t const count) noexcept {
    Culler::intersects(results, frustum, b, count);
}

} // namespace filament
