/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_DFG_H
#define TNT_FILAMENT_DETAILS_DFG_H

#include <backend/Handle.h>

#include "details/Texture.h"

#include <utils/compiler.h>

#include <cstdint>
#include <cstddef>

namespace filament {

class FEngine;

/**
 * DFG LUT 大小
 * 
 * 默认大小为 128x128。可以通过定义 FILAMENT_DFG_LUT_SIZE 覆盖。
 */
#if !defined(FILAMENT_DFG_LUT_SIZE)
#define FILAMENT_DFG_LUT_SIZE 128
#endif

/**
 * DFG（Distribution Function for Glossy）查找表
 * 
 * DFG LUT 用于预计算镜面反射的分布函数，用于基于物理的渲染（PBR）。
 * 
 * DFG 项是 Cook-Torrance 微表面 BRDF 的一部分，用于计算：
 * - D（法线分布函数）
 * - F（菲涅尔项）
 * - G（几何遮蔽函数）
 * 
 * 通过预计算 LUT，可以在运行时快速查找这些值，而不需要实时计算。
 */
class DFG {
public:
    explicit DFG() noexcept = default;

    // 禁止拷贝和移动
    DFG(DFG const& rhs) = delete;
    DFG(DFG&& rhs) = delete;
    DFG& operator=(DFG const& rhs) = delete;
    DFG& operator=(DFG&& rhs) = delete;

    /**
     * 初始化 DFG LUT
     * 
     * 创建并填充 DFG 查找表纹理。
     * 
     * @param engine 引擎引用
     */
    void init(FEngine& engine);

    /**
     * 获取 LUT 大小
     * 
     * @return LUT 大小（宽度/高度）
     */
    size_t getLutSize() const noexcept {
        return DFG_LUT_SIZE;
    }

    /**
     * 检查 LUT 是否有效
     * 
     * @return 如果 LUT 已初始化返回 true，否则返回 false
     */
    bool isValid() const noexcept {
        return mLUT != nullptr;
    }

    /**
     * 获取 LUT 纹理句柄
     * 
     * @return 硬件纹理句柄
     */
    backend::Handle<backend::HwTexture> getTexture() const noexcept {
        return mLUT->getHwHandle();
    }

    /**
     * 终止 DFG LUT
     * 
     * 释放 LUT 纹理资源。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

private:
    FTexture* mLUT = nullptr;  // LUT 纹理

    /**
     * DFG LUT 大小
     * 
     * 确保使用正确的大小。
     */
    static constexpr size_t DFG_LUT_SIZE = FILAMENT_DFG_LUT_SIZE;
};

#undef FILAMENT_DFG_LUT_SIZE

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_DFG_H
