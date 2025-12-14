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

//! \file

#ifndef TNT_FILAMENT_FRUSTUM_H
#define TNT_FILAMENT_FRUSTUM_H

#include <utils/compiler.h>

#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/unwindows.h> // Because we define NEAR and FAR in the Plane enum.

#include <stdint.h>

namespace filament {

class Box;
class Culler;

/**
 * A frustum defined by six planes
 */
/**
 * 由六个平面定义的视锥体
 */
class UTILS_PUBLIC Frustum {
public:
    enum class Plane : uint8_t {
        LEFT,
        /**
         * 左平面
         */
        RIGHT,
        /**
         * 右平面
         */
        BOTTOM,
        /**
         * 底平面
         */
        TOP,
        /**
         * 顶平面
         */
        FAR,
        /**
         * 远平面
         */
        NEAR
        /**
         * 近平面
         */
    };

    Frustum() = default;
    Frustum(const Frustum& rhs) = default;
    Frustum(Frustum&& rhs) noexcept = default;
    Frustum& operator=(const Frustum& rhs) = default;
    Frustum& operator=(Frustum&& rhs) noexcept = default;

    /**
     * Creates a frustum from a projection matrix in GL convention
     * (usually the projection * view matrix)
     * @param pv a 4x4 projection matrix in GL convention
     */
    /**
     * 从 GL 约定中的投影矩阵创建视锥体
     * （通常是 projection * view 矩阵）
     * @param pv GL 约定中的 4x4 投影矩阵
     */
    explicit Frustum(const math::mat4f& pv);

    /**
     * Sets the frustum from the given projection matrix
     * @param pv a 4x4 projection matrix
     */
    /**
     * 从给定的投影矩阵设置视锥体
     * @param pv 4x4 投影矩阵
     */
    void setProjection(const math::mat4f& pv);

    /**
     * Returns the plane equation parameters with normalized normals
     * @param plane Identifier of the plane to retrieve the equation of
     * @return A plane equation encoded a float4 R such as R.x*x + R.y*y + R.z*z + R.w = 0
     */
    /**
     * 返回具有归一化法线的平面方程参数
     * @param plane 要检索其方程的平面的标识符
     * @return 编码为 float4 R 的平面方程，使得 R.x*x + R.y*y + R.z*z + R.w = 0
     */
    math::float4 getNormalizedPlane(Plane plane) const noexcept;

    /**
     * Returns a copy of all six frustum planes in left, right, bottom, top, far, near order
     * @param planes six plane equations encoded as in getNormalizedPlane() in
     *              left, right, bottom, top, far, near order
     */
    /**
     * 返回所有六个视锥体平面的副本，按左、右、底、顶、远、近的顺序
     * @param planes 六个平面方程，按 getNormalizedPlane() 中的编码方式，按
     *               左、右、底、顶、远、近的顺序
     */
    void getNormalizedPlanes(math::float4 planes[UTILS_NONNULL 6]) const noexcept;

    /**
     * Returns all six frustum planes in left, right, bottom, top, far, near order
     * @return six plane equations encoded as in getNormalizedPlane() in
     *              left, right, bottom, top, far, near order
     */
    /**
     * 返回所有六个视锥体平面，按左、右、底、顶、远、近的顺序
     * @return 六个平面方程，按 getNormalizedPlane() 中的编码方式，按
     *         左、右、底、顶、远、近的顺序
     */
    math::float4 const* UTILS_NONNULL getNormalizedPlanes() const noexcept { return mPlanes; }

    /**
     * Returns whether a box intersects the frustum (i.e. is visible)
     * @param box The box to test against the frustum
     * @return true if the box may intersects the frustum, false otherwise. In some situations
     * a box that doesn't intersect the frustum might be reported as though it does. However,
     * a box that does intersect the frustum is always reported correctly (true).
     */
    /**
     * 返回一个包围盒是否与视锥体相交（即是否可见）
     * @param box 要测试的包围盒
     * @return 如果包围盒可能与视锥体相交则返回 true，否则返回 false。在某些情况下，
     * 不与视锥体相交的包围盒可能被报告为相交。但是，
     * 与视锥体相交的包围盒总是被正确报告（true）。
     */
    bool intersects(const Box& box) const noexcept;

    /**
     * Returns whether a sphere intersects the frustum (i.e. is visible)
     * @param sphere A sphere encoded as a center + radius.
     * @return true if the sphere may intersects the frustum, false otherwise. In some situations
     * a sphere that doesn't intersect the frustum might be reported as though it does. However,
     * a sphere that does intersect the frustum is always reported correctly (true).
     */
    /**
     * 返回一个球体是否与视锥体相交（即是否可见）
     * @param sphere 编码为中心 + 半径的球体
     * @return 如果球体可能与视锥体相交则返回 true，否则返回 false。在某些情况下，
     * 不与视锥体相交的球体可能被报告为相交。但是，
     * 与视锥体相交的球体总是被正确报告（true）。
     */
    bool intersects(const math::float4& sphere) const noexcept;

    /**
     * Returns whether the frustum contains a given point.
     * @param p the point to test
     * @return the maximum signed distance to the frustum. Negative if p is inside.
     */
    /**
     * 返回视锥体是否包含给定点
     * @param p 要测试的点
     * @return 到视锥体的最大有符号距离。如果 p 在内部则为负值
     */
    float contains(math::float3 p) const noexcept;

private:
    friend class Culler;
    math::float4 mPlanes[6];
};

} // namespace filament

#if !defined(NDEBUG)
namespace utils::io {
class ostream;
} // namespace utils::io
utils::io::ostream& operator<<(utils::io::ostream& out, filament::Frustum const& frustum);
#endif

#endif // TNT_FILAMENT_FRUSTUM_H
