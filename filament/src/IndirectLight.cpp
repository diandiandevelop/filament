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

#include "details/IndirectLight.h"

#include "details/Texture.h"

namespace filament {

using namespace math;

/**
 * 设置间接光强度
 * 
 * @param intensity 强度值
 */
void IndirectLight::setIntensity(float const intensity) noexcept {
    downcast(this)->setIntensity(intensity);
}

/**
 * 获取间接光强度
 * 
 * @return 强度值
 */
float IndirectLight::getIntensity() const noexcept {
    return downcast(this)->getIntensity();
}

/**
 * 设置间接光旋转矩阵
 * 
 * @param rotation 旋转矩阵（3x3）
 */
void IndirectLight::setRotation(mat3f const& rotation) noexcept {
    downcast(this)->setRotation(rotation);
}

/**
 * 获取间接光旋转矩阵
 * 
 * @return 旋转矩阵引用（3x3）
 */
const mat3f& IndirectLight::getRotation() const noexcept {
    return downcast(this)->getRotation();
}

/**
 * 获取反射纹理
 * 
 * @return 反射纹理指针
 */
Texture const* IndirectLight::getReflectionsTexture() const noexcept {
    return downcast(this)->getReflectionsTexture();
}

/**
 * 获取辐照度纹理
 * 
 * @return 辐照度纹理指针
 */
Texture const* IndirectLight::getIrradianceTexture() const noexcept {
    return downcast(this)->getIrradianceTexture();
}

/**
 * 获取方向估计（从球谐函数）
 * 
 * @return 方向向量
 */
float3 IndirectLight::getDirectionEstimate() const noexcept {
    return downcast(this)->getDirectionEstimate();
}

/**
 * 获取颜色估计（从球谐函数）
 * 
 * @param direction 方向向量
 * @return 颜色值（RGBA）
 */
float4 IndirectLight::getColorEstimate(float3 const direction) const noexcept {
    return downcast(this)->getColorEstimate(direction);
}

/**
 * 从球谐函数获取方向估计（静态方法）
 * 
 * @param sh 球谐函数系数数组（9 个 float3）
 * @return 方向向量
 */
float3 IndirectLight::getDirectionEstimate(const float3* sh) noexcept {
    return FIndirectLight::getDirectionEstimate(sh);
}

/**
 * 从球谐函数获取颜色估计（静态方法）
 * 
 * @param sh 球谐函数系数数组（9 个 float3）
 * @param direction 方向向量
 * @return 颜色值（RGBA）
 */
float4 IndirectLight::getColorEstimate(const float3* sh, float3 const direction) noexcept {
    return FIndirectLight::getColorEstimate(sh, direction);
}

} // namespace filament
