/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "components/LightManager.h"

using namespace utils;

namespace filament {

using namespace math;

/**
 * 检查实体是否有光源组件
 * 
 * @param e 实体
 * @return 如果有组件则返回 true
 */
bool LightManager::hasComponent(Entity const e) const noexcept {
    return downcast(this)->hasComponent(e);
}

/**
 * 获取组件数量
 * 
 * @return 组件数量
 */
size_t LightManager::getComponentCount() const noexcept {
    return downcast(this)->getComponentCount();
}

/**
 * 检查是否为空
 * 
 * @return 如果为空则返回 true
 */
bool LightManager::empty() const noexcept {
    return downcast(this)->empty();
}

/**
 * 获取实例对应的实体
 * 
 * @param i 实例
 * @return 实体
 */
Entity LightManager::getEntity(Instance const i) const noexcept {
    return downcast(this)->getEntity(i);
}

/**
 * 获取所有实体数组
 * 
 * @return 实体数组指针
 */
Entity const* LightManager::getEntities() const noexcept {
    return downcast(this)->getEntities();
}

/**
 * 获取实体对应的实例
 * 
 * @param e 实体
 * @return 实例
 */
LightManager::Instance LightManager::getInstance(Entity const e) const noexcept {
    return downcast(this)->getInstance(e);
}

/**
 * 销毁光源组件
 * 
 * @param e 实体
 */
void LightManager::destroy(Entity const e) noexcept {
    return downcast(this)->destroy(e);
}

/**
 * 设置光源通道
 * 
 * @param i 实例
 * @param channel 通道索引
 * @param enable 是否启用
 */
void LightManager::setLightChannel(Instance const i, unsigned int const channel, bool const enable) noexcept {
    downcast(this)->setLightChannel(i, channel, enable);
}

/**
 * 获取光源通道状态
 * 
 * @param i 实例
 * @param channel 通道索引
 * @return 如果启用则返回 true
 */
bool LightManager::getLightChannel(Instance const i, unsigned int const channel) const noexcept {
    return downcast(this)->getLightChannel(i, channel);
}

/**
 * 设置光源位置（局部空间）
 * 
 * @param i 实例
 * @param position 位置向量
 */
void LightManager::setPosition(Instance const i, const float3& position) noexcept {
    downcast(this)->setLocalPosition(i, position);
}

/**
 * 获取光源位置（局部空间）
 * 
 * @param i 实例
 * @return 位置向量引用
 */
const float3& LightManager::getPosition(Instance const i) const noexcept {
    return downcast(this)->getLocalPosition(i);
}

/**
 * 设置光源方向（局部空间）
 * 
 * @param i 实例
 * @param direction 方向向量
 */
void LightManager::setDirection(Instance const i, const float3& direction) noexcept {
    downcast(this)->setLocalDirection(i, direction);
}

/**
 * 获取光源方向（局部空间）
 * 
 * @param i 实例
 * @return 方向向量引用
 */
const float3& LightManager::getDirection(Instance const i) const noexcept {
    return downcast(this)->getLocalDirection(i);
}

/**
 * 设置光源颜色
 * 
 * @param i 实例
 * @param color 线性颜色
 */
void LightManager::setColor(Instance const i, const LinearColor& color) noexcept {
    downcast(this)->setColor(i, color);
}

/**
 * 获取光源颜色
 * 
 * @param i 实例
 * @return 颜色向量引用
 */
const float3& LightManager::getColor(Instance const i) const noexcept {
    return downcast(this)->getColor(i);
}

/**
 * 设置光源强度（流明/勒克斯单位）
 * 
 * @param i 实例
 * @param intensity 强度值
 */
void LightManager::setIntensity(Instance const i, float const intensity) noexcept {
    downcast(this)->setIntensity(i, intensity, FLightManager::IntensityUnit::LUMEN_LUX);
}

/**
 * 设置光源强度（坎德拉单位）
 * 
 * @param i 实例
 * @param intensity 强度值
 */
void LightManager::setIntensityCandela(Instance const i, float const intensity) noexcept {
    downcast(this)->setIntensity(i, intensity, FLightManager::IntensityUnit::CANDELA);
}

/**
 * 获取光源强度
 * 
 * @param i 实例
 * @return 强度值
 */
float LightManager::getIntensity(Instance const i) const noexcept {
    return downcast(this)->getIntensity(i);
}

/**
 * 设置光源衰减半径
 * 
 * @param i 实例
 * @param radius 衰减半径
 */
void LightManager::setFalloff(Instance const i, float const radius) noexcept {
    downcast(this)->setFalloff(i, radius);
}

/**
 * 获取光源衰减半径
 * 
 * @param i 实例
 * @return 衰减半径
 */
float LightManager::getFalloff(Instance const i) const noexcept {
    return downcast(this)->getFalloff(i);
}

/**
 * 设置聚光灯锥角
 * 
 * @param i 实例
 * @param inner 内锥角（弧度）
 * @param outer 外锥角（弧度）
 */
void LightManager::setSpotLightCone(Instance const i, float const inner, float const outer) noexcept {
    downcast(this)->setSpotLightCone(i, inner, outer);
}

/**
 * 获取聚光灯外锥角
 * 
 * @param i 实例
 * @return 外锥角（弧度）
 */
float LightManager::getSpotLightOuterCone(Instance const i) const noexcept {
    return downcast(this)->getSpotParams(i).outerClamped;
}

/**
 * 获取聚光灯内锥角
 * 
 * @param i 实例
 * @return 内锥角（弧度）
 */
float LightManager::getSpotLightInnerCone(Instance const i) const noexcept {
    return downcast(this)->getSpotLightInnerCone(i);
}

/**
 * 设置太阳角半径（弧度）
 * 
 * @param i 实例
 * @param angularRadius 角半径（弧度）
 */
void LightManager::setSunAngularRadius(Instance const i, float const angularRadius) noexcept {
    downcast(this)->setSunAngularRadius(i, angularRadius);
}

/**
 * 获取太阳角半径（度）
 * 
 * @param i 实例
 * @return 角半径（度）
 */
float LightManager::getSunAngularRadius(Instance const i) const noexcept {
    float radius = downcast(this)->getSunAngularRadius(i);
    return radius * f::RAD_TO_DEG;
}

/**
 * 设置太阳光晕大小
 * 
 * @param i 实例
 * @param haloSize 光晕大小
 */
void LightManager::setSunHaloSize(Instance const i, float const haloSize) noexcept {
    downcast(this)->setSunHaloSize(i, haloSize);
}

/**
 * 获取太阳光晕大小
 * 
 * @param i 实例
 * @return 光晕大小
 */
float LightManager::getSunHaloSize(Instance const i) const noexcept {
    return downcast(this)->getSunHaloSize(i);
}

/**
 * 设置太阳光晕衰减
 * 
 * @param i 实例
 * @param haloFalloff 光晕衰减值
 */
void LightManager::setSunHaloFalloff(Instance const i, float const haloFalloff) noexcept {
    downcast(this)->setSunHaloFalloff(i, haloFalloff);
}

/**
 * 获取太阳光晕衰减
 * 
 * @param i 实例
 * @return 光晕衰减值
 */
float LightManager::getSunHaloFalloff(Instance const i) const noexcept {
    return downcast(this)->getSunHaloFalloff(i);
}

/**
 * 获取光源类型
 * 
 * @param i 实例
 * @return 光源类型枚举值
 */
LightManager::Type LightManager::getType(Instance const i) const noexcept {
    return downcast(this)->getType(i);
}

/**
 * 获取阴影选项
 * 
 * @param i 实例
 * @return 阴影选项引用
 */
const LightManager::ShadowOptions& LightManager::getShadowOptions(Instance const i) const noexcept {
    return downcast(this)->getShadowOptions(i);
}

/**
 * 设置阴影选项
 * 
 * @param i 实例
 * @param options 阴影选项
 */
void LightManager::setShadowOptions(Instance const i, ShadowOptions const& options) noexcept {
    downcast(this)->setShadowOptions(i, options);
}

/**
 * 检查是否为阴影投射者
 * 
 * @param i 实例
 * @return 如果是阴影投射者则返回 true
 */
bool LightManager::isShadowCaster(Instance const i) const noexcept {
    return downcast(this)->isShadowCaster(i);
}

/**
 * 设置是否为阴影投射者
 * 
 * @param i 实例
 * @param castShadows 是否投射阴影
 */
void LightManager::setShadowCaster(Instance const i, bool const castShadows) noexcept {
    downcast(this)->setShadowCaster(i, castShadows);
}

} // namespace filament
