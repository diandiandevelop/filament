/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_COLORGRADING_H
#define TNT_FILAMENT_DETAILS_COLORGRADING_H

#include "downcast.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <filament/ColorGrading.h>

#include <math/mathfwd.h>

namespace filament {

class FEngine;

/**
 * 颜色分级实现类
 * 
 * 管理颜色分级查找表（LUT - Look-Up Table）。
 * 颜色分级用于后处理，调整最终图像的色调、饱和度、对比度等。
 * 
 * 实现细节：
 * - 使用 3D 纹理存储颜色查找表
 * - 支持 1D 和 3D LUT
 * - 支持 LDR（低动态范围）和 HDR（高动态范围）
 * - LUT 维度通常是 32x32x32 或 64x64x64
 */
class FColorGrading : public ColorGrading {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FColorGrading(FEngine& engine, const Builder& builder);
    
    /**
     * 禁止拷贝构造
     */
    FColorGrading(const FColorGrading& rhs) = delete;
    
    /**
     * 禁止拷贝赋值
     */
    FColorGrading& operator=(const FColorGrading& rhs) = delete;

    /**
     * 析构函数
     */
    ~FColorGrading() noexcept;

    /**
     * 终止颜色分级
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 获取硬件句柄
     * 
     * @return LUT 纹理句柄
     */
    backend::TextureHandle getHwHandle() const noexcept { return mLutHandle; }
    
    /**
     * 获取维度
     * 
     * 返回 LUT 的维度（例如 32 表示 32x32x32）。
     * 
     * @return LUT 维度
     */
    uint32_t getDimension() const noexcept { return mDimension; }
    
    /**
     * 检查是否为一维 LUT
     * 
     * @return 如果是一维 LUT 返回 true，否则返回 false
     */
    bool isOneDimensional() const noexcept { return mIsOneDimensional; }
    
    /**
     * 检查是否为 LDR
     * 
     * @return 如果是 LDR 返回 true，否则返回 false（HDR）
     */
    bool isLDR() const noexcept { return mIsLDR; }

private:
    backend::TextureHandle mLutHandle;  // LUT 纹理句柄
    uint32_t mDimension;  // LUT 维度
    bool mIsOneDimensional;  // 是否为一维 LUT
    bool mIsLDR;  // 是否为 LDR（低动态范围）
};

FILAMENT_DOWNCAST(ColorGrading)

} // namespace filament

#endif //TNT_FILAMENT_DETAILS_COLORGRADING_H
