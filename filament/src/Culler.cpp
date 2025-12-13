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

/**
 * 批量剔除：球体数组
 * 
 * 对每个球体，检查它是否与视锥体的所有 6 个平面相交。
 * 使用优化的 SIMD 向量化实现以提高性能。
 * 
 * @param results 结果数组（输出），每个元素表示对应球体的可见性
 *                - 非零：球体可见（与视锥相交）
 *                - 零：球体不可见（完全在视锥外）
 * @param frustum 视锥体引用（包含 6 个平面方程）
 * @param b 球体数组（输入）
 *          每个球体用 float4 表示：
 *          - xyz: 球心坐标（视图空间）
 *          - w: 半径
 * @param count 球体数量
 * 
 * 算法说明：
 * 1. 对每个球体，检查它与所有 6 个视锥平面的关系
 * 2. 计算球心到每个平面的有符号距离，减去半径
 * 3. 如果所有距离都为负，球体在视锥内（可见）
 * 4. 如果任何距离为正，球体在视锥外（不可见）
 * 
 * 优化：
 * - 使用 SIMD 向量化一次处理多个球体
 * - 完全展开内层循环以减少分支
 * - 使用 signbit 避免条件分支
 */
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float4 const* UTILS_RESTRICT b,      // 球体数组（xyz=中心，w=半径）
        size_t count) noexcept {

    /**
     * 获取视锥体的平面数组
     * 
     * 每个平面用 float4 表示：
     * - xyz: 归一化的法线向量
     * - w: 平面到原点的距离
     */
    float4 const * const UTILS_RESTRICT planes = frustum.mPlanes;

    /**
     * 向上舍入到 MODULO（8）的倍数，以支持 SIMD 向量化
     * 
     * 这确保我们可以使用 SIMD 指令一次处理多个球体。
     * 多余的球体会被处理，但结果会被忽略。
     */
    count = round(count);
    
#if defined(__clang__)
    /**
     * 提示编译器使用 SIMD 向量化（一次处理 4 个对象）
     * 
     * 这允许编译器生成向量化指令，提高性能。
     */
    #pragma clang loop vectorize_width(FILAMENT_CULLER_VECTORIZE_HINT)
#endif
    /**
     * 遍历所有球体
     */
    for (size_t i = 0; i < count; i++) {
        /**
         * 初始化可见性标志
         * 
         * visible 初始化为全 1（所有位都为 1），表示初始状态为可见。
         * 对于每个平面，如果球体在平面外侧，对应的位会被清除。
         */
        int visible = ~0;  // 初始化为全 1（可见）
        
        /**
         * 获取当前球体
         */
        float4 const sphere(b[i]);

#if defined(__clang__)
        /**
         * 完全展开内层循环（6 个平面），减少循环开销
         * 
         * 这会将 6 次迭代展开为 6 个连续的代码块，消除循环控制开销。
         */
        #pragma clang loop unroll(full)
#endif
        /**
         * 检查球体与所有 6 个视锥平面的关系
         */
        for (size_t j = 0; j < 6; j++) {
            /**
             * 计算球心到平面的有符号距离，减去半径
             * 
             * 平面方程：ax + by + cz + d = 0
             * 点到平面的距离：distance = dot(plane.xyz, point) + plane.w
             * 
             * 对于球体，我们需要考虑半径：
             * - 如果球心到平面的距离 < 半径，球体与平面相交
             * - 如果球心到平面的距离 >= 半径，球体在平面外侧
             * 
             * 因此，我们计算：dot = distance - radius
             * - 如果 dot < 0，球体在平面内侧或相交（可见）
             * - 如果 dot >= 0，球体在平面外侧（不可见）
             * 
             * 注意：clang 似乎不会生成向量*标量指令，这会导致寄存器压力增加和栈溢出。
             * 因此我们手动展开点积计算。
             */
            const float dot = planes[j].x * sphere.x +      // 平面法线 X 分量 * 球心 X
                              planes[j].y * sphere.y +      // 平面法线 Y 分量 * 球心 Y
                              planes[j].z * sphere.z +      // 平面法线 Z 分量 * 球心 Z
                              planes[j].w - sphere.w;       // 平面距离 - 球体半径
            
            /**
             * 更新可见性标志
             * 
             * 如果 dot < 0，球体在平面内侧（可见），signbit 返回 1
             * 如果 dot >= 0，球体在平面外侧（不可见），signbit 返回 0
             * 
             * 使用按位与操作累积可见性：
             * - 如果所有平面都返回 1，visible 保持为全 1（可见）
             * - 如果任何平面返回 0，visible 的对应位被清除（不可见）
             * 
             * 使用 signbit 避免分支，提高性能。
             */
            visible &= fast::signbit(dot);
        }
        
        /**
         * 存储结果
         * 
         * 将可见性标志转换为 result_type 并存储到结果数组。
         */
        results[i] = result_type(visible);
    }
}

/**
 * 批量剔除：AABB 数组
 * 
 * 对每个 AABB（轴对齐包围盒），检查它是否与视锥体的所有 6 个平面相交。
 * 使用分离轴定理（Separating Axis Theorem）计算 AABB 到平面的最近距离。
 * 
 * @param results 结果数组（输入/输出），每个元素是一个位掩码，表示多级可见性
 * @param frustum 视锥体引用（包含 6 个平面方程）
 * @param center AABB 中心数组（输入）
 *               每个元素是 float3，表示 AABB 的中心点（视图空间坐标）
 * @param extent AABB 半边长数组（输入）
 *               每个元素是 float3，表示 AABB 在 X、Y、Z 轴上的半边长
 * @param count AABB 数量
 * @param bit 结果掩码位（用于多级可见性）
 *            指定在结果掩码中设置哪个位来表示可见性
 * 
 * 算法说明：
 * 1. 对每个 AABB，使用分离轴定理计算它到每个视锥平面的最近距离
 * 2. 如果所有距离都为负，AABB 在视锥内（可见）
 * 3. 如果任何距离为正，AABB 在视锥外（不可见）
 * 
 * 分离轴定理：
 * 对于 AABB 和平面，最近距离的计算公式为：
 * distance = dot(plane.xyz, center) - dot(abs(plane.xyz), extent) + plane.w
 * 
 * 优化：
 * - 使用 SIMD 向量化一次处理多个 AABB
 * - 完全展开内层循环以减少分支
 * - 使用 signbit 避免条件分支
 */
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float3 const* UTILS_RESTRICT center,      // AABB 中心数组
        float3 const* UTILS_RESTRICT extent,      // AABB 半边长数组
        size_t count, 
        size_t const bit) noexcept {              // 结果掩码位（用于多级可见性）

    /**
     * 获取视锥体的平面数组
     */
    float4 const * UTILS_RESTRICT const planes = frustum.mPlanes;

    /**
     * 向上舍入到 MODULO（8）的倍数，以支持 SIMD 向量化
     */
    count = round(count);
    
#if defined(__clang__)
    /**
     * 提示编译器使用 SIMD 向量化（一次处理 4 个对象）
     */
    #pragma clang loop vectorize_width(FILAMENT_CULLER_VECTORIZE_HINT)
#endif
    /**
     * 遍历所有 AABB
     */
    for (size_t i = 0; i < count; i++) {
        /**
         * 初始化可见性标志
         */
        int visible = ~0;  // 初始化为全 1（可见）

#if defined(__clang__)
        /**
         * 完全展开内层循环（6 个平面），减少循环开销
         */
        #pragma clang loop unroll(full)
#endif
        /**
         * 检查 AABB 与所有 6 个视锥平面的关系
         */
        for (size_t j = 0; j < 6; j++) {
            /**
             * 计算 AABB 到平面的最近距离（使用分离轴定理）
             * 
             * 分离轴定理（SAT）用于计算 AABB 到平面的最近距离。
             * 
             * 对于 AABB，最近距离的计算公式为：
             * distance = dot(plane.xyz, center) - dot(abs(plane.xyz), extent) + plane.w
             * 
             * 解释：
             * - dot(plane.xyz, center): 中心点到平面的距离
             * - dot(abs(plane.xyz), extent): AABB 在平面法线方向上的投影（半长度）
             * - plane.w: 平面到原点的距离
             * 
             * 减去投影是因为我们要找最近距离（最接近平面的点）。
             * 
             * 注意：clang 似乎不会生成向量*标量指令，这会导致寄存器压力增加和栈溢出。
             * 因此我们手动展开点积计算。
             */
            const float dot =
                    planes[j].x * center[i].x - std::abs(planes[j].x) * extent[i].x +  // X 轴贡献
                    planes[j].y * center[i].y - std::abs(planes[j].y) * extent[i].y +  // Y 轴贡献
                    planes[j].z * center[i].z - std::abs(planes[j].z) * extent[i].z +  // Z 轴贡献
                    planes[j].w;                                                         // 平面距离
            
            /**
             * 更新可见性标志
             * 
             * 如果 dot < 0，AABB 在平面内侧（可见），signbit 返回 1
             * 如果 dot >= 0，AABB 在平面外侧（不可见），signbit 返回 0
             * 
             * 将结果左移到指定位（bit），用于多级可见性掩码。
             */
            visible &= fast::signbit(dot) << bit;
        }

        /**
         * 更新结果掩码：清除对应位，然后设置可见性
         * 
         * 这允许在同一个结果值中存储多个可见性级别。
         * 
         * 步骤：
         * 1. 读取当前结果值
         * 2. 清除指定位（bit）
         * 3. 设置新的可见性值
         * 4. 写回结果
         */
        auto r = results[i];                        // 读取当前结果
        r &= ~result_type(1u << bit);              // 清除对应位
        r |= result_type(visible);                  // 设置可见性
        results[i] = r;                            // 写回结果
    }
}

/**
 * 检查包围盒是否与视锥相交
 * 
 * 检查单个包围盒（Box）是否与视锥体相交。
 * 
 * @param frustum 视锥体引用
 * @param box 包围盒
 *               包含：
 *               - center: 中心点（视图空间坐标）
 *               - halfExtent: 半边长（X、Y、Z 轴方向）
 * @return true 如果包围盒与视锥相交，false 如果完全在视锥外
 * 
 * 实现：
 * 1. 将单个包围盒包装成数组（批量剔除函数需要数组）
 * 2. 调用批量剔除函数
 * 3. 检查结果的第一位（bit 0）
 * 
 * 注意：批量剔除函数假设处理 MODULO（8）的倍数个对象，
 * 因此我们创建大小为 MODULO 的数组，但只使用第一个元素。
 */
bool Culler::intersects(Frustum const& frustum, Box const& box) noexcept {
    // 批量剔除函数假设处理 MODULO（8）的倍数个对象
    float3 centers[MODULO];      // AABB 中心数组
    float3 extents[MODULO];       // AABB 半边长数组
    result_type results[MODULO];   // 结果数组
    
    /**
     * 设置第一个元素为实际的包围盒数据
     */
    centers[0] = box.center;      // 包围盒中心
    extents[0] = box.halfExtent;  // 包围盒半边长
    
    /**
     * 调用批量剔除函数
     * 
     * 参数说明：
     * - results: 结果数组
     * - frustum: 视锥体
     * - centers: AABB 中心数组
     * - extents: AABB 半边长数组
     * - MODULO: 数量（8）
     * - 0: 结果掩码位（使用 bit 0）
     */
    intersects(results, frustum, centers, extents, MODULO, 0);
    
    /**
     * 检查结果的第一位（bit 0）
     * 
     * 如果 results[0] & 1 为真，表示包围盒可见（与视锥相交）。
     */
    return bool(results[0] & 1);
}

/**
 * 检查球体是否与视锥相交
 * 
 * 检查单个球体是否与视锥体相交。
 * 
 * @param frustum 视锥体引用
 * @param sphere 球体
 *               用 float4 表示：
 *               - xyz: 球心坐标（视图空间）
 *               - w: 半径
 * @return true 如果球体与视锥相交，false 如果完全在视锥外
 * 
 * 实现：
 * 1. 将单个球体包装成数组（批量剔除函数需要数组）
 * 2. 调用批量剔除函数
 * 3. 检查结果的第一位（bit 0）
 * 
 * 注意：批量剔除函数假设处理 MODULO（8）的倍数个对象，
 * 因此我们创建大小为 MODULO 的数组，但只使用第一个元素。
 */
bool Culler::intersects(Frustum const& frustum, float4 const& sphere) noexcept {
    // 批量剔除函数假设处理 MODULO（8）的倍数个对象
    float4 spheres[MODULO];       // 球体数组
    result_type results[MODULO];   // 结果数组
    
    /**
     * 设置第一个元素为实际的球体数据
     */
    spheres[0] = sphere;
    
    /**
     * 调用批量剔除函数
     * 
     * 参数说明：
     * - results: 结果数组
     * - frustum: 视锥体
     * - spheres: 球体数组
     * - MODULO: 数量（8）
     */
    intersects(results, frustum, spheres, MODULO);
    
    /**
     * 检查结果的第一位（bit 0）
     * 
     * 如果 results[0] & 1 为真，表示球体可见（与视锥相交）。
     */
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
