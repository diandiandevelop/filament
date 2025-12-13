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

#include <filament/Frustum.h>

#include "Culler.h"

#include <utils/compiler.h>
#include <utils/ostream.h>

#include <math/vec3.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <algorithm>

using namespace filament::math;

namespace filament {

/**
 * Frustum 构造函数
 * 
 * @param pv 投影-视图矩阵（projection-view matrix）
 */
/**
 * Frustum 构造函数
 * 
 * 从投影-视图矩阵创建视锥体。
 * 
 * @param pv 投影-视图矩阵（projection-view matrix）
 *           这是投影矩阵和视图矩阵的乘积：PV = Projection * View
 * 
 * 实现：调用 setProjection() 方法从矩阵提取视锥体平面
 */
Frustum::Frustum(const mat4f& pv) {
    setProjection(pv);
}

/**
 * 设置投影矩阵
 * 
 * 注意：如果我们不在这里指定 noinline，LLVM 会将这个巨大的函数内联到
 * Frustum(const mat4f& pv) 构造函数的两个（？！）版本中！
 * 
 * 参考："Fast Extraction of Viewing Frustum Planes from the WorldView-Projection Matrix"
 * by Gil Gribb & Klaus Hartmann
 * 
 * 另一种理解方式是，我们将裁剪空间中的每个平面变换到视图空间。
 * 这种变换执行方式为：
 *      transpose(inverse(viewFromClipMatrix))，即：transpose(projection)
 * 
 * @param pv 投影-视图矩阵
 */
UTILS_NOINLINE
void Frustum::setProjection(const mat4f& pv) {
    /**
     * 设置投影矩阵
     * 
     * 注意：如果我们不在这里指定 noinline，LLVM 会将这个巨大的函数内联到
     * Frustum(const mat4f& pv) 构造函数的两个（？！）版本中！
     * 
     * 参考："Fast Extraction of Viewing Frustum Planes from the WorldView-Projection Matrix"
     * by Gil Gribb & Klaus Hartmann
     * 
     * 另一种理解方式是，我们将裁剪空间中的每个平面变换到视图空间。
     * 这种变换执行方式为：
     *      transpose(inverse(viewFromClipMatrix))，即：transpose(projection)
     * 
     * @param pv 投影-视图矩阵
     * 
     * 算法说明：
     * 1. 转置矩阵以便提取平面
     * 2. 从转置矩阵的列向量提取六个平面方程
     * 3. 归一化平面方程（用于球体测试）
     * 4. 存储归一化的平面
     */
    
    /**
     * 转置矩阵以便提取平面
     * 
     * 转置投影-视图矩阵，使得我们可以从列向量中提取平面方程。
     * 转置后的矩阵 m 的列向量包含了平面方程的系数。
     */
    const mat4f m(transpose(pv));

    /**
     * 提取六个平面（左、右、底、顶、近、远）
     * 
     * 注意：这些"法线"未归一化——对于剔除测试不是必需的。
     * 
     * 平面方程形式：ax + by + cz + d = 0
     * 其中 (a, b, c) 是法线向量，d 是距离
     * 
     * 从裁剪空间的六个平面（在裁剪空间中定义）变换到视图空间：
     * - 左平面（x = -1）：m * { -1,  0,  0, -1 }
     * - 右平面（x =  1）：m * {  1,  0,  0, -1 }
     * - 底平面（y = -1）：m * {  0, -1,  0, -1 }
     * - 顶平面（y =  1）：m * {  0,  1,  0, -1 }
     * - 近平面（z = -1）：m * {  0,  0, -1, -1 }
     * - 远平面（z =  1）：m * {  0,  0,  1, -1 }
     * 
     * 变量说明：
     * - m[0], m[1], m[2], m[3]: 转置矩阵的列向量
     * - m[3]: 矩阵的第四列（包含平移和齐次坐标信息）
     * - l, r, b, t, n, f: 六个平面的方程系数（float4，xyz=法线，w=距离）
     */
    float4 l = -m[3] - m[0];    // 左平面：-m[3] - m[0]
    float4 r = -m[3] + m[0];    // 右平面：-m[3] + m[0]
    float4 b = -m[3] - m[1];    // 底平面：-m[3] - m[1]
    float4 t = -m[3] + m[1];    // 顶平面：-m[3] + m[1]
    float4 n = -m[3] - m[2];    // 近平面：-m[3] - m[2]
    float4 f = -m[3] + m[2];    // 远平面：-m[3] + m[2]

    /**
     * 归一化平面方程
     * 
     * 注意：对于我们的包围盒/视锥相交例程，归一化这些向量不是必需的，
     * 但是它们必须为球体/视锥测试进行归一化。
     * 
     * 归一化方法：
     * - 计算法线向量的长度：length(plane.xyz)
     * - 将整个平面方程除以长度：plane *= 1 / length(plane.xyz)
     * 
     * 这样做的目的是使法线向量成为单位向量，便于后续的距离计算。
     */
    l *= 1 / length(l.xyz);  // 归一化左平面
    r *= 1 / length(r.xyz);  // 归一化右平面
    b *= 1 / length(b.xyz);  // 归一化底平面
    t *= 1 / length(t.xyz);  // 归一化顶平面
    n *= 1 / length(n.xyz);  // 归一化近平面
    f *= 1 / length(f.xyz);  // 归一化远平面

    /**
     * 存储归一化的平面
     * 
     * 将归一化后的平面方程存储到成员数组中。
     * 数组索引对应 Plane 枚举值。
     */
    mPlanes[0] = l;  // 左平面
    mPlanes[1] = r;  // 右平面
    mPlanes[2] = b;  // 底平面
    mPlanes[3] = t;  // 顶平面
    mPlanes[4] = f;  // 远平面
    mPlanes[5] = n;  // 近平面
}

/**
 * 获取归一化的平面
 * 
 * @param plane 平面枚举
 * @return 归一化的平面方程（xyz = 法线，w = 距离）
 */
float4 Frustum::getNormalizedPlane(Plane plane) const noexcept {
    return mPlanes[size_t(plane)];
}

/**
 * 获取所有归一化的平面
 * 
 * @param planes 输出数组（6 个平面）
 */
void Frustum::getNormalizedPlanes(float4 planes[6]) const noexcept {
    planes[0] = mPlanes[0];
    planes[1] = mPlanes[1];
    planes[2] = mPlanes[2];
    planes[3] = mPlanes[3];
    planes[4] = mPlanes[4];
    planes[5] = mPlanes[5];
}

/**
 * 检查视锥是否与包围盒相交
 * 
 * @param box 包围盒
 * @return true 如果相交
 */
bool Frustum::intersects(const Box& box) const noexcept {
    return Culler::intersects(*this, box);
}

/**
 * 检查视锥是否与球体相交
 * 
 * @param sphere 球体（xyz = 中心，w = 半径）
 * @return true 如果相交
 */
bool Frustum::intersects(const float4& sphere) const noexcept {
    return Culler::intersects(*this, sphere);
}

/**
 * 检查点是否在视锥内
 * 
 * 返回点到所有平面的最大距离（如果为负，则点在视锥内）。
 * 
 * @param p 点
 * @return 点到所有平面的最大距离（负值表示在视锥内）
 */
float Frustum::contains(float3 const p) const noexcept {
    /**
     * 检查点是否在视锥内
     * 
     * 返回点到所有平面的最大距离（如果为负，则点在视锥内）。
     * 
     * 算法：
     * 对于每个平面，计算点到平面的有符号距离：
     * distance = dot(plane.xyz, point) + plane.w
     * 
     * 如果所有距离都为负，则点在视锥内。
     * 如果任何距离为正，则点在视锥外。
     * 
     * 返回最大距离：
     * - 如果为负，点在视锥内
     * - 如果为正，点在视锥外
     * - 如果为 0，点在视锥边界上
     * 
     * @param p 要检查的点（视图空间坐标）
     * @return 点到所有平面的最大距离（负值表示在视锥内）
     * 
     * 变量说明：
     * - l, r, b, t, f, n: 点到各个平面的有符号距离
     *   - 负值：点在平面内侧（朝向视锥中心）
     *   - 正值：点在平面外侧（远离视锥中心）
     *   - 零值：点在平面上
     * - d: 所有距离中的最大值
     */
    
    /**
     * 计算点到各个平面的距离
     * 
     * 平面方程：ax + by + cz + d = 0
     * 点到平面的距离：distance = dot(plane.xyz, point) + plane.w
     * 
     * 注意：由于平面方程已归一化，这个距离是有符号的：
     * - 负值：点在平面内侧
     * - 正值：点在平面外侧
     */
    float const l = dot(mPlanes[0].xyz, p) + mPlanes[0].w;  // 左平面距离
    float const b = dot(mPlanes[1].xyz, p) + mPlanes[1].w;  // 底平面距离
    float const r = dot(mPlanes[2].xyz, p) + mPlanes[2].w;  // 右平面距离
    float const t = dot(mPlanes[3].xyz, p) + mPlanes[3].w;  // 顶平面距离
    float const f = dot(mPlanes[4].xyz, p) + mPlanes[4].w;  // 远平面距离
    float const n = dot(mPlanes[5].xyz, p) + mPlanes[5].w;  // 近平面距离
    
    /**
     * 找到点到所有平面的最大距离
     * 
     * 使用 std::max 逐个比较，找到最大的距离值。
     * 这个最大值决定了点是否在视锥内：
     * - 如果最大值为负，所有距离都为负，点在视锥内
     * - 如果最大值为正，至少有一个距离为正，点在视锥外
     */
    float d = l;              // 初始化为左平面距离
    d = std::max(d, b);      // 与底平面距离比较
    d = std::max(d, r);      // 与右平面距离比较
    d = std::max(d, t);      // 与顶平面距离比较
    d = std::max(d, f);      // 与远平面距离比较
    d = std::max(d, n);      // 与近平面距离比较
    return d;                // 返回最大距离
}

} // namespace filament

#if !defined(NDEBUG)

utils::io::ostream& operator<<(utils::io::ostream& out, filament::Frustum const& frustum) {
    float4 planes[6];
    frustum.getNormalizedPlanes(planes);
    out     << planes[0] << '\n'
            << planes[1] << '\n'
            << planes[2] << '\n'
            << planes[3] << '\n'
            << planes[4] << '\n'
            << planes[5] << utils::io::endl;
    return out;
}

#endif
