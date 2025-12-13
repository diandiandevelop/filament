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

#include "details/Material.h"

#include <filament/Material.h>
#include <filament/MaterialEnums.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>

#include <utils/Invocable.h>

#include <utility>

#include <stddef.h>

namespace filament {

class MaterialInstance;

using namespace backend;

/**
 * 创建材质实例
 * 
 * @param name 实例名称
 * @return 材质实例指针
 */
MaterialInstance* Material::createInstance(const char* name) const noexcept {
    return downcast(this)->createInstance(name);
}

/**
 * 获取材质名称
 * 
 * @return 材质名称（C 字符串）
 */
const char* Material::getName() const noexcept {
    return downcast(this)->getName().c_str_safe();
}

/**
 * 获取着色模型
 * 
 * @return 着色模型枚举值
 */
Shading Material::getShading()  const noexcept {
    return downcast(this)->getShading();
}

/**
 * 获取插值模式
 * 
 * @return 插值模式枚举值
 */
Interpolation Material::getInterpolation() const noexcept {
    return downcast(this)->getInterpolation();
}

/**
 * 获取混合模式
 * 
 * @return 混合模式枚举值
 */
BlendingMode Material::getBlendingMode() const noexcept {
    return downcast(this)->getBlendingMode();
}

/**
 * 获取顶点域
 * 
 * @return 顶点域枚举值
 */
VertexDomain Material::getVertexDomain() const noexcept {
    return downcast(this)->getVertexDomain();
}

/**
 * 获取材质域
 * 
 * @return 材质域枚举值
 */
MaterialDomain Material::getMaterialDomain() const noexcept {
    return downcast(this)->getMaterialDomain();
}

/**
 * 获取剔除模式
 * 
 * @return 剔除模式枚举值
 */
CullingMode Material::getCullingMode() const noexcept {
    return downcast(this)->getCullingMode();
}

/**
 * 获取透明度模式
 * 
 * @return 透明度模式枚举值
 */
TransparencyMode Material::getTransparencyMode() const noexcept {
    return downcast(this)->getTransparencyMode();
}

/**
 * 检查是否启用颜色写入
 * 
 * @return 如果启用则返回 true
 */
bool Material::isColorWriteEnabled() const noexcept {
    return downcast(this)->isColorWriteEnabled();
}

/**
 * 检查是否启用深度写入
 * 
 * @return 如果启用则返回 true
 */
bool Material::isDepthWriteEnabled() const noexcept {
    return downcast(this)->isDepthWriteEnabled();
}

/**
 * 检查是否启用深度剔除
 * 
 * @return 如果启用则返回 true
 */
bool Material::isDepthCullingEnabled() const noexcept {
    return downcast(this)->isDepthCullingEnabled();
}

/**
 * 检查是否为双面材质
 * 
 * @return 如果是双面则返回 true
 */
bool Material::isDoubleSided() const noexcept {
    return downcast(this)->isDoubleSided();
}

/**
 * 检查是否启用 Alpha to Coverage
 * 
 * @return 如果启用则返回 true
 */
bool Material::isAlphaToCoverageEnabled() const noexcept {
    return downcast(this)->isAlphaToCoverageEnabled();
}

/**
 * 获取遮罩阈值
 * 
 * @return 遮罩阈值
 */
float Material::getMaskThreshold() const noexcept {
    return downcast(this)->getMaskThreshold();
}

/**
 * 检查是否有阴影倍增器
 * 
 * @return 如果有则返回 true
 */
bool Material::hasShadowMultiplier() const noexcept {
    return downcast(this)->hasShadowMultiplier();
}

/**
 * 检查是否有镜面反射抗锯齿
 * 
 * @return 如果有则返回 true
 */
bool Material::hasSpecularAntiAliasing() const noexcept {
    return downcast(this)->hasSpecularAntiAliasing();
}

/**
 * 获取镜面反射抗锯齿方差
 * 
 * @return 方差值
 */
float Material::getSpecularAntiAliasingVariance() const noexcept {
    return downcast(this)->getSpecularAntiAliasingVariance();
}

/**
 * 获取镜面反射抗锯齿阈值
 * 
 * @return 阈值
 */
float Material::getSpecularAntiAliasingThreshold() const noexcept {
    return downcast(this)->getSpecularAntiAliasingThreshold();
}

/**
 * 获取参数数量
 * 
 * @return 参数数量
 */
size_t Material::getParameterCount() const noexcept {
    return downcast(this)->getParameterCount();
}

/**
 * 获取参数信息
 * 
 * @param parameters 输出参数信息数组
 * @param count 数组大小
 * @return 实际获取的参数数量
 */
size_t Material::getParameters(ParameterInfo* parameters, size_t const count) const noexcept {
    return downcast(this)->getParameters(parameters, count);
}

/**
 * 获取必需的顶点属性
 * 
 * @return 属性位集合
 */
AttributeBitset Material::getRequiredAttributes() const noexcept {
    return downcast(this)->getRequiredAttributes();
}

/**
 * 获取折射模式
 * 
 * @return 折射模式枚举值
 */
RefractionMode Material::getRefractionMode() const noexcept {
    return downcast(this)->getRefractionMode();
}

/**
 * 获取折射类型
 * 
 * @return 折射类型枚举值
 */
RefractionType Material::getRefractionType() const noexcept {
    return downcast(this)->getRefractionType();
}

/**
 * 获取反射模式
 * 
 * @return 反射模式枚举值
 */
ReflectionMode Material::getReflectionMode() const noexcept {
    return downcast(this)->getReflectionMode();
}

/**
 * 获取功能级别
 * 
 * @return 功能级别枚举值
 */
FeatureLevel Material::getFeatureLevel() const noexcept {
    return downcast(this)->getFeatureLevel();
}

/**
 * 检查是否有指定名称的参数
 * 
 * @param name 参数名称
 * @return 如果存在则返回 true
 */
bool Material::hasParameter(const char* name) const noexcept {
    return downcast(this)->hasParameter(name);
}

/**
 * 检查指定名称的参数是否为采样器
 * 
 * @param name 参数名称
 * @return 如果是采样器则返回 true
 */
bool Material::isSampler(const char* name) const noexcept {
    return downcast(this)->isSampler(name);
}

/**
 * 获取材质源字符串
 * 
 * @return 材质源字符串视图
 */
std::string_view Material::getSource() const noexcept {
    return downcast(this)->getSource();
}

/**
 * 获取采样器参数的变换名称
 * 
 * @param samplerName 采样器名称
 * @return 变换名称（C 字符串）
 */
const char* Material::getParameterTransformName(const char* samplerName) const noexcept {
    return downcast(this)->getParameterTransformName(samplerName);
}

/**
 * 获取默认材质实例（非 const 版本）
 * 
 * @return 默认材质实例指针
 */
MaterialInstance* Material::getDefaultInstance() noexcept {
    return downcast(this)->getDefaultInstance();
}

/**
 * 获取默认材质实例（const 版本）
 * 
 * @return 默认材质实例指针（const）
 */
MaterialInstance const* Material::getDefaultInstance() const noexcept {
    return downcast(this)->getDefaultInstance();
}

/**
 * 编译材质
 * 
 * @param priority 编译优先级
 * @param variants 变体过滤掩码
 * @param handler 回调处理器
 * @param callback 完成回调函数
 */
void Material::compile(CompilerPriorityQueue const priority, UserVariantFilterMask const variants,
        CallbackHandler* handler, utils::Invocable<void(Material*)>&& callback) noexcept {
    downcast(this)->compile(priority, variants, handler, std::move(callback));
}

/**
 * 获取支持的变体
 * 
 * @return 变体过滤掩码
 */
UserVariantFilterMask Material::getSupportedVariants() const noexcept {
    return downcast(this)->getSupportedVariants();
}


} // namespace filament
