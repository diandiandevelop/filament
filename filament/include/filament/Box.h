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

#ifndef TNT_FILAMENT_BOX_H
#define TNT_FILAMENT_BOX_H

#include <utils/compiler.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <float.h>

#include <stddef.h>

namespace filament {

/**
 * An axis aligned 3D box represented by its center and half-extent.
 */
/**
 * 由其中心和半延伸表示的轴对齐 3D 包围盒。
 */
class UTILS_PUBLIC Box {
public:
    /** Center of the 3D box */
    /**
     * 3D 包围盒的中心
     */
    math::float3 center = {};

    /** Half extent from the center on all 3 axis */
    /**
     * 从中心在所有 3 个轴上的半延伸
     */
    math::float3 halfExtent = {};

    /**
     * Whether the box is empty, i.e.: it's volume is null.
     * @return true if the volume of the box is null
     */
    /**
     * 包围盒是否为空，即：其体积为空。
     * @return 如果包围盒的体积为空则返回 true
     */
    constexpr bool isEmpty() const noexcept {
        return length2(halfExtent) == 0;
    }

    /**
     * Computes the lowest coordinates corner of the box.
     * @return center - halfExtent
     */
    /**
     * 计算包围盒的最低坐标角。
     * @return center - halfExtent
     */
    constexpr math::float3 getMin() const noexcept {
        return center - halfExtent;
    }

    /**
     * Computes the largest coordinates corner of the box.
     * @return center + halfExtent
     */
    /**
     * 计算包围盒的最大坐标角。
     * @return center + halfExtent
     */
    constexpr math::float3 getMax() const noexcept {
        return center + halfExtent;
    }

    /**
     * Initializes the 3D box from its min / max coordinates on each axis
     * @param min lowest coordinates corner of the box
     * @param max largest coordinates corner of the box
     * @return This bounding box
     */
    /**
     * 从每个轴上的最小/最大坐标初始化 3D 包围盒
     * @param min 包围盒的最低坐标角
     * @param max 包围盒的最大坐标角
     * @return 此包围盒
     */
    Box& set(const math::float3& min, const math::float3& max) noexcept {
        // float3 ctor needed for Visual Studio
        center     = (max + min) * math::float3(0.5f);
        halfExtent = (max - min) * math::float3(0.5f);
        return *this;
    }

    /**
     * Computes the bounding box of the union of two boxes
     * @param box The box to be combined with
     * @return The bounding box of the union of *this and box
     */
    /**
     * 计算两个包围盒的并集的包围盒
     * @param box 要合并的包围盒
     * @return *this 和 box 的并集的包围盒
     */
    Box& unionSelf(const Box& box) noexcept {
        set(min(getMin(), box.getMin()), max(getMax(), box.getMax()));
        return *this;
    }

    /**
     * Translates the box *to* a given center position
     * @param tr position to translate the box to
     * @return A box centered in \p tr with the same extent than *this
     */
    /**
     * 将包围盒平移到给定中心位置
     * @param tr 要平移包围盒到的位置
     * @return 以 \p tr 为中心且与 *this 具有相同延伸的包围盒
     */
    constexpr Box translateTo(const math::float3& tr) const noexcept {
        return Box{ tr, halfExtent };
    }

    /**
     * Computes the smallest bounding sphere of the box.
     * @return The smallest sphere defined by its center (.xyz) and radius (.w) that contains *this
     */
    /**
     * 计算包围盒的最小包围球。
     * @return 由其中心 (.xyz) 和半径 (.w) 定义且包含 *this 的最小球
     */
    math::float4 getBoundingSphere() const noexcept {
        return { center, length(halfExtent) };
    }

    /**
     * Transform a Box by a linear transform and a translation.
     *
     * @param m a 3x3 matrix, the linear transform
     * @param t a float3, the translation
     * @param box the box to transform
     * @return the bounding box of the transformed box
     */
    /**
     * 通过线性变换和平移变换 Box。
     *
     * @param m 3x3 矩阵，线性变换
     * @param t float3，平移
     * @param box 要变换的包围盒
     * @return 变换后的包围盒的包围盒
     */
    static Box transform(const math::mat3f& m, math::float3 const& t, const Box& box) noexcept {
        return { m * box.center + t, abs(m) * box.halfExtent };
    }

    /**
     * Transform a Box by a linear transform and a translation.
     *
     * @param m a linear transform matrix
     * @param box the box to transform
     * @return the bounding box of the transformed box
     */
    /**
     * 通过线性变换和平移变换 Box。
     *
     * @param m 线性变换矩阵
     * @param box 要变换的包围盒
     * @return 变换后的包围盒的包围盒
     */
    friend Box rigidTransform(Box const& box, const math::mat4f& m) noexcept {
        return transform(m.upperLeft(), m[3].xyz, box);
    }
};

/**
 * An axis aligned box represented by its min and max coordinates
 */
/**
 * 由其最小和最大坐标表示的轴对齐包围盒（AABB）
 */
struct UTILS_PUBLIC Aabb {

    /** min coordinates */
    /**
     * 最小坐标
     */
    math::float3 min = FLT_MAX;

    /** max coordinates */
    /**
     * 最大坐标
     */
    math::float3 max = -FLT_MAX;

    /**
     * Computes the center of the box.
     * @return (max + min)/2
     */
    /**
     * 计算包围盒的中心。
     * @return (max + min)/2
     */
    math::float3 center() const noexcept {
        // float3 ctor needed for Visual Studio
        return (max + min) * math::float3(0.5f);
    }

    /**
     * Computes the half-extent of the box.
     * @return (max - min)/2
     */
    /**
     * 计算包围盒的半延伸。
     * @return (max - min)/2
     */
    math::float3 extent() const noexcept {
        // float3 ctor needed for Visual Studio
        return (max - min) * math::float3(0.5f);
    }

    /**
     * Whether the box is empty, i.e.: it's volume is null or negative.
     * @return true if min >= max, i.e: the volume of the box is null or negative
     */
    /**
     * 包围盒是否为空，即：其体积为空或负数。
     * @return 如果 min >= max，即：包围盒的体积为空或负数，则返回 true
     */
    bool isEmpty() const noexcept {
        return any(greaterThanEqual(min, max));
    }

    /**
     * AABB 的 8 个角顶点容器
     */
    struct Corners {
        using value_type = math::float3;  //!< 顶点类型
        value_type const* begin() const { return vertices; }
        value_type const* end() const { return vertices + 8; }
        value_type * begin() { return vertices; }
        value_type * end() { return vertices + 8; }
        value_type const* data() const { return vertices; }
        value_type * data() { return vertices; }
        size_t size() const { return 8; }  //!< 顶点数量（8 个）
        value_type const& operator[](size_t i) const noexcept { return vertices[i]; }
        value_type& operator[](size_t i) noexcept { return vertices[i]; }
        value_type vertices[8];  //!< 8 个角顶点
    };

    /**
     * Returns the 8 corner vertices of the AABB.
     */
    /**
     * 返回 AABB 的 8 个角顶点。
     */
    Corners getCorners() const {
        return Corners{ .vertices = {
                { min.x, min.y, min.z },
                { max.x, min.y, min.z },
                { min.x, max.y, min.z },
                { max.x, max.y, min.z },
                { min.x, min.y, max.z },
                { max.x, min.y, max.z },
                { min.x, max.y, max.z },
                { max.x, max.y, max.z },
        }};
    }

    /**
     * Returns whether the box contains a given point.
     *
     * @param p the point to test
     * @return the maximum signed distance to the box. Negative if p is in the box
     */
    /**
     * 返回包围盒是否包含给定点。
     *
     * @param p 要测试的点
     * @return 到包围盒的最大有符号距离。如果 p 在包围盒内则为负数
     */
    float contains(math::float3 p) const noexcept {
        // we don't use std::max to avoid a dependency on <algorithm>
        auto const maximum = [](auto a, auto b) { return a > b ? a : b; };
        float d = min.x - p.x;
        d = maximum(d, min.y - p.y);
        d = maximum(d, min.z - p.z);
        d = maximum(d, p.x - max.x);
        d = maximum(d, p.y - max.y);
        d = maximum(d, p.z - max.z);
        return d;
    }

    /**
     * Applies an affine transformation to the AABB.
     *
     * @param m the 3x3 transformation to apply
     * @param t the translation
     * @return the transformed box
     */
    /**
     * 将仿射变换应用于 AABB。
     *
     * @param m 要应用的 3x3 变换
     * @param t 平移
     * @return 变换后的包围盒
     */
    static Aabb transform(const math::mat3f& m, math::float3 const& t, const Aabb& box) noexcept {
        // Fast AABB transformation per Jim Arvo in Graphics Gems (1990).
        // 根据 Jim Arvo 在 Graphics Gems (1990) 中的快速 AABB 变换
        Aabb result{ t, t };
        for (size_t col = 0; col < 3; ++col) {
            for (size_t row = 0; row < 3; ++row) {
                const float a = m[col][row] * box.min[col];
                const float b = m[col][row] * box.max[col];
                result.min[row] += a < b ? a : b;
                result.max[row] += a < b ? b : a;
            }
        }
        return result;
    }

    /**
     * Applies an affine transformation to the AABB.
     *
     * @param m the affine transformation to apply
     * @return the bounding box of the transformed box
     */
    /**
     * 将仿射变换应用于 AABB。
     *
     * @param m 要应用的仿射变换
     * @return 变换后的包围盒的包围盒
     */
    Aabb transform(const math::mat4f& m) const noexcept {
        return transform(m.upperLeft(), m[3].xyz, *this);
    }
};

} // namespace filament

#endif // TNT_FILAMENT_BOX_H
