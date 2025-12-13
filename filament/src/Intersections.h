/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_INTERSECTIONS_H
#define TNT_FILAMENT_INTERSECTIONS_H

#include <utils/compiler.h>

#include <math/mat4.h>
#include <math/vec4.h>

namespace filament {

/**
 * 球面与平面相交
 * 
 * 计算球面与平面的相交，返回相交圆/球面的中心和半径。
 * 
 * 要求：
 * - 球面半径必须是平方的（s.w = r²）
 * - 平面方程必须归一化（p.xyz 是单位向量）
 * 
 * @param s 球面（xyz = 中心，w = 半径²）
 * @param p 平面（xyz = 法线，w = 距离原点的距离）
 * @return 相交结果（xyz = 相交圆/球面的中心，w = 相交圆/球面的半径²）
 *         如果 w <= 0，则没有相交
 */
inline constexpr math::float4 spherePlaneIntersection(math::float4 s, math::float4 p) noexcept {
    /**
     * 计算球心到平面的距离
     */
    const float d = dot(s.xyz, p.xyz) + p.w;
    /**
     * 计算相交圆/球面的半径²
     * 使用勾股定理：r² = R² - d²
     */
    const float rr = s.w - d * d;
    /**
     * 将球心投影到平面上，得到相交圆/球面的中心
     */
    s.x -= p.x * d;
    s.y -= p.y * d;
    s.z -= p.z * d;
    s.w = rr;  // 新圆/球面的半径是平方的
    return s;
}

/**
 * 球面与平面相交（简化版本）
 * 
 * 平面方程必须归一化且形式为 {0,0,1,w}（即法线为 z 轴方向）。
 * 
 * @param s 球面（xyz = 中心，w = 半径²）
 * @param pw 平面到原点的距离（沿 z 轴）
 * @return 相交结果（xyz = 相交圆/球面的中心，w = 相交圆/球面的半径²）
 *         如果 w <= 0，则没有相交
 */
inline constexpr math::float4 spherePlaneIntersection(math::float4 const s, float pw) noexcept {
    return spherePlaneIntersection(s, { 0.f, 0.f, 1.f, pw });
}

/**
 * 球面与圆锥相交（快速版本）
 * 
 * 此版本在圆锥原点附近的小区域（由球面半径向外延伸）中可能返回假阳性相交。
 * 
 * 要求：
 * - 球面半径必须是平方的（sphere.w = r²）
 * 
 * @param sphere 球面（xyz = 中心，w = 半径²）
 * @param conePosition 圆锥顶点位置
 * @param coneAxis 圆锥轴方向（归一化）
 * @param coneSinInverse 1 / sin(半角)
 * @param coneCosSquared cos²(半角)
 * @return true 如果相交（可能有假阳性）
 */
inline constexpr bool sphereConeIntersectionFast(
        math::float4 const& sphere,
        math::float3 const& conePosition,
        math::float3 const& coneAxis,
        float coneSinInverse,
        float coneCosSquared) noexcept {
    /**
     * 计算圆锥顶点沿轴向后移动的位置（考虑球面半径）
     */
    const math::float3 u = conePosition - (sphere.w * coneSinInverse) * coneAxis;
    math::float3 const d = sphere.xyz - u;
    float const e = dot(coneAxis, d);
    float const dd = dot(d, d);
    /**
     * 检查球心是否在圆锥内
     * 我们最后检查 e>0 以避免分支
     */
    return (e * e >= dd * coneCosSquared && e > 0);
}

/**
 * 球面与圆锥相交（精确版本）
 * 
 * 使用快速版本进行初步检查，然后进行精确测试。
 * 
 * @param sphere 球面（xyz = 中心，w = 半径²）
 * @param conePosition 圆锥顶点位置
 * @param coneAxis 圆锥轴方向（归一化）
 * @param coneSinInverse 1 / sin(半角)
 * @param coneCosSquared cos²(半角)
 * @return true 如果相交
 */
inline constexpr bool sphereConeIntersection(
        math::float4 const& sphere,
        math::float3 const& conePosition,
        math::float3 const& coneAxis,
        float const coneSinInverse,
        float const coneCosSquared) noexcept {
    /**
     * 首先使用快速版本进行初步检查
     */
    if (sphereConeIntersectionFast(sphere,
            conePosition, coneAxis, coneSinInverse, coneCosSquared)) {
        /**
         * 进行精确测试：检查球面是否与圆锥相交
         */
        math::float3 const d = sphere.xyz - conePosition;
        float const e = -dot(coneAxis, d);
        float const dd = dot(d, d);
        /**
         * 如果球心在圆锥内，检查球面是否与圆锥边界相交
         */
        if (e * e >= dd * (1 - coneCosSquared) && e > 0) {
            /**
             * 检查球心是否在圆锥内且距离顶点足够近
             */
            return dd <= sphere.w * sphere.w;
        }
        return true;
    }
    return false;
}

/**
 * 三个平面的交点
 * 
 * 计算三个平面的交点。
 * 假设所有平面都相交（即法线不共面）。
 * 
 * 公式：
 *      -d0.(n1 x n2) - d1.(n2 x n0) - d2.(n0 x n1)
 * P = ---------------------------------------------
 *                      n0.(n1 x n2)
 * 
 * 其中：
 * - n0, n1, n2 是三个平面的法线
 * - d0, d1, d2 是三个平面到原点的距离
 * 
 * @param p0 平面 0（xyz = 法线，w = 距离）
 * @param p1 平面 1（xyz = 法线，w = 距离）
 * @param p2 平面 2（xyz = 法线，w = 距离）
 * @return 三个平面的交点
 */
inline constexpr math::float3 planeIntersection(
        math::float4 const& p0,
        math::float4 const& p1,
        math::float4 const& p2) noexcept {
    /**
     * 计算法线的叉积
     */
    auto const c0 = cross(p1.xyz, p2.xyz);
    auto const c1 = cross(p2.xyz, p0.xyz);
    auto const c2 = cross(p0.xyz, p1.xyz);
    /**
     * 使用公式计算交点
     */
    return -(p0.w * c0 + p1.w * c1 + p2.w * c2) * (1.0f / dot(p0.xyz, c0));
}

} // namespace filament

#endif // TNT_FILAMENT_INTERSECTIONS_H
