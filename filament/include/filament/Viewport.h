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

#ifndef TNT_FILAMENT_VIEWPORT_H
#define TNT_FILAMENT_VIEWPORT_H

#include <backend/DriverEnums.h>

#include <utils/compiler.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

/**
 * Viewport describes a view port in pixel coordinates
 *
 * A view port is represented by its left-bottom coordinate, width and height in pixels.
 */
/**
 * Viewport 描述像素坐标中的视口
 *
 * 视口由其左下角坐标、宽度和高度（以像素为单位）表示。
 */
class UTILS_PUBLIC Viewport : public backend::Viewport {
public:
    /**
     * Creates a Viewport of zero width and height at the origin.
     */
    /**
     * 在原点创建一个宽度和高度为零的 Viewport
     */
    Viewport() noexcept : backend::Viewport{} {}

    /**
     * Creates a Viewport from its left-bottom coordinates, width and height in pixels
     *
     * @param left left coordinate in pixel
     * @param bottom bottom coordinate in pixel
     * @param width width in pixel
     * @param height height in pixel
     */
    /**
     * 从其左下角坐标、宽度和高度（以像素为单位）创建 Viewport
     *
     * @param left 左坐标（像素）
     * @param bottom 底坐标（像素）
     * @param width 宽度（像素）
     * @param height 高度（像素）
     */
    Viewport(int32_t left, int32_t bottom, uint32_t width, uint32_t height) noexcept
            : backend::Viewport{ left, bottom, width, height } {
    }

    /**
     * Returns whether the area of the view port is null.
     *
     * @return true if either width or height is 0 pixel.
     */
    /**
     * 返回视口的面积是否为零
     *
     * @return 如果宽度或高度为 0 像素，则返回 true
     */
    bool empty() const noexcept { return !width || !height; }

private:
    /**
     * Compares two Viewports for equality
     * @param lhs reference to the left hand side Viewport
     * @param rhs reference to the right hand side Viewport
     * @return true if \p rhs and \p lhs are identical.
     */
    /**
     * 比较两个 Viewport 是否相等
     * @param lhs 左侧 Viewport 的引用
     * @param rhs 右侧 Viewport 的引用
     * @return 如果 \p rhs 和 \p lhs 相同，则返回 true
     */
    friend bool operator==(Viewport const& lhs, Viewport const& rhs) noexcept {
        return (&rhs == &lhs) ||
               (rhs.left == lhs.left && rhs.bottom == lhs.bottom &&
                rhs.width == lhs.width && rhs.height == lhs.height);
    }

    /**
     * Compares two Viewports for inequality
     * @param lhs reference to the left hand side Viewport
     * @param rhs reference to the right hand side Viewport
     * @return true if \p rhs and \p lhs are different.
     */
    /**
     * 比较两个 Viewport 是否不相等
     * @param lhs 左侧 Viewport 的引用
     * @param rhs 右侧 Viewport 的引用
     * @return 如果 \p rhs 和 \p lhs 不同，则返回 true
     */
    friend bool operator!=(Viewport const& lhs, Viewport const& rhs) noexcept {
        return !(rhs == lhs);
    }
};

} // namespace filament

#endif // TNT_FILAMENT_VIEWPORT_H
