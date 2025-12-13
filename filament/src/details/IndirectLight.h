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

#ifndef TNT_FILAMENT_DETAILS_INDIRECTLIGHT_H
#define TNT_FILAMENT_DETAILS_INDIRECTLIGHT_H

#include "downcast.h"

#include <backend/Handle.h>

#include <filament/IndirectLight.h>
#include <filament/Texture.h>

#include <utils/compiler.h>

#include <math/mat3.h>

#include <array>

namespace filament {

class FEngine;

/**
 * 间接光实现类
 * 
 * 管理环境光照（IBL - Image-Based Lighting）。
 * 间接光使用预计算的反射贴图和辐照度数据来模拟环境光照。
 * 
 * 实现细节：
 * - 使用球谐函数（SH）存储辐照度数据（9 个系数）
 * - 支持反射贴图（用于镜面反射）和辐照度贴图（用于漫反射）
 * - 可以旋转环境贴图
 * - 可以调整光照强度
 */
class FIndirectLight : public IndirectLight {
public:
    /**
     * 默认强度
     * 
     * 太阳的照度值（勒克斯）。
     */
    static constexpr float DEFAULT_INTENSITY = 30000.0f;    // lux of the sun

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FIndirectLight(FEngine& engine, const Builder& builder) noexcept;

    /**
     * 终止间接光
     * 
     * 释放资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 获取反射贴图硬件句柄
     * 
     * @return 反射贴图硬件句柄
     */
    backend::Handle<backend::HwTexture> getReflectionHwHandle() const noexcept;
    
    /**
     * 获取辐照度贴图硬件句柄
     * 
     * @return 辐照度贴图硬件句柄
     */
    backend::Handle<backend::HwTexture> getIrradianceHwHandle() const noexcept;
    
    /**
     * 获取球谐函数系数
     * 
     * 返回辐照度的球谐函数系数数组（9 个 float3）。
     * 
     * @return 球谐函数系数数组指针
     */
    math::float3 const* getSH() const noexcept{ return mIrradianceCoefs.data(); }
    
    /**
     * 获取强度
     * 
     * @return 光照强度
     */
    float getIntensity() const noexcept { return mIntensity; }
    
    /**
     * 设置强度
     * 
     * @param intensity 光照强度
     */
    void setIntensity(float const intensity) noexcept { mIntensity = intensity; }
    
    /**
     * 设置旋转
     * 
     * 设置环境贴图的旋转矩阵。
     * 
     * @param rotation 旋转矩阵
     */
    void setRotation(math::mat3f const& rotation) noexcept { mRotation = rotation; }
    
    /**
     * 获取旋转
     * 
     * @return 旋转矩阵常量引用
     */
    const math::mat3f& getRotation() const noexcept { return mRotation; }
    
    /**
     * 获取反射贴图
     * 
     * @return 反射贴图常量指针（如果没有则返回 nullptr）
     */
    FTexture const* getReflectionsTexture() const noexcept { return mReflectionsTexture; }
    
    /**
     * 获取辐照度贴图
     * 
     * @return 辐照度贴图常量指针（如果没有则返回 nullptr）
     */
    FTexture const* getIrradianceTexture() const noexcept { return mIrradianceTexture; }
    
    /**
     * 获取级别数量
     * 
     * 返回反射贴图的 Mip 级别数量。
     * 
     * @return Mip 级别数量
     */
    size_t getLevelCount() const noexcept { return mLevelCount; }
    
    /**
     * 获取方向估计
     * 
     * 从球谐函数系数估计主要光照方向。
     * 
     * @return 主要光照方向（归一化）
     */
    math::float3 getDirectionEstimate() const noexcept;
    
    /**
     * 获取颜色估计
     * 
     * 从球谐函数系数估计指定方向的光照颜色。
     * 
     * @param direction 方向向量（归一化）
     * @return 光照颜色（RGBA）
     */
    math::float4 getColorEstimate(math::float3 direction) const noexcept;
    
    /**
     * 获取方向估计（静态方法）
     * 
     * 从球谐函数系数估计主要光照方向。
     * 
     * @param sh 球谐函数系数数组（9 个 float3）
     * @return 主要光照方向（归一化）
     */
    static math::float3 getDirectionEstimate(const math::float3 sh[9]) noexcept;
    
    /**
     * 获取颜色估计（静态方法）
     * 
     * 从球谐函数系数估计指定方向的光照颜色。
     * 
     * @param sh 球谐函数系数数组（9 个 float3）
     * @param direction 方向向量（归一化）
     * @return 光照颜色（RGBA）
     */
    static math::float4 getColorEstimate(const math::float3 sh[9], math::float3 direction) noexcept;

private:
    FTexture const* mReflectionsTexture = nullptr;  // 反射贴图指针（不拥有）
    FTexture const* mIrradianceTexture = nullptr;  // 辐照度贴图指针（不拥有）
    std::array<math::float3, 9> mIrradianceCoefs;  // 辐照度球谐函数系数（9 个 float3）
    float mIntensity = DEFAULT_INTENSITY;  // 光照强度
    math::mat3f mRotation;  // 旋转矩阵
    uint8_t mLevelCount = 0;  // Mip 级别数量
};

FILAMENT_DOWNCAST(IndirectLight)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_INDIRECTLIGHT_H
