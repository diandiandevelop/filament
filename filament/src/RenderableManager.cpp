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

#include "components/RenderableManager.h"

#include "details/Engine.h"
#include "details/VertexBuffer.h"
#include "details/Material.h"

using namespace utils;

namespace filament {

using namespace backend;
using namespace math;

/**
 * 检查实体是否有可渲染组件
 * 
 * @param e 实体
 * @return 如果有组件则返回 true
 */
bool RenderableManager::hasComponent(Entity const e) const noexcept {
    return downcast(this)->hasComponent(e);
}

/**
 * 获取组件数量
 * 
 * @return 组件数量
 */
size_t RenderableManager::getComponentCount() const noexcept {
    return downcast(this)->getComponentCount();
}

/**
 * 检查是否为空
 * 
 * @return 如果为空则返回 true
 */
bool RenderableManager::empty() const noexcept {
    return downcast(this)->empty();
}

/**
 * 获取实例对应的实体
 * 
 * @param i 实例
 * @return 实体
 */
Entity RenderableManager::getEntity(Instance const i) const noexcept {
    return downcast(this)->getEntity(i);
}

/**
 * 获取所有实体数组
 * 
 * @return 实体数组指针
 */
Entity const* RenderableManager::getEntities() const noexcept {
    return downcast(this)->getEntities();
}

/**
 * 获取实体对应的实例
 * 
 * @param e 实体
 * @return 实例
 */
RenderableManager::Instance
RenderableManager::getInstance(Entity const e) const noexcept {
    return downcast(this)->getInstance(e);
}

/**
 * 销毁可渲染组件
 * 
 * @param e 实体
 */
void RenderableManager::destroy(Entity const e) noexcept {
    return downcast(this)->destroy(e);
}

/**
 * 设置轴对齐包围盒
 * 
 * @param instance 实例
 * @param aabb 包围盒
 */
void RenderableManager::setAxisAlignedBoundingBox(Instance const instance, const Box& aabb) {
    downcast(this)->setAxisAlignedBoundingBox(instance, aabb);
}

/**
 * 设置层掩码
 * 
 * @param instance 实例
 * @param select 选择掩码
 * @param values 值掩码
 */
void RenderableManager::setLayerMask(Instance const instance, uint8_t const select, uint8_t const values) noexcept {
    downcast(this)->setLayerMask(instance, select, values);
}

/**
 * 设置渲染优先级
 * 
 * @param instance 实例
 * @param priority 优先级值
 */
void RenderableManager::setPriority(Instance const instance, uint8_t const priority) noexcept {
    downcast(this)->setPriority(instance, priority);
}

/**
 * 设置渲染通道
 * 
 * @param instance 实例
 * @param channel 通道索引
 */
void RenderableManager::setChannel(Instance const instance, uint8_t const channel) noexcept{
    downcast(this)->setChannel(instance, channel);
}

/**
 * 设置剔除启用状态
 * 
 * @param instance 实例
 * @param enable 是否启用剔除
 */
void RenderableManager::setCulling(Instance const instance, bool const enable) noexcept {
    downcast(this)->setCulling(instance, enable);
}

/**
 * 设置是否投射阴影
 * 
 * @param instance 实例
 * @param enable 是否投射阴影
 */
void RenderableManager::setCastShadows(Instance const instance, bool const enable) noexcept {
    downcast(this)->setCastShadows(instance, enable);
}

/**
 * 设置是否接收阴影
 * 
 * @param instance 实例
 * @param enable 是否接收阴影
 */
void RenderableManager::setReceiveShadows(Instance const instance, bool const enable) noexcept {
    downcast(this)->setReceiveShadows(instance, enable);
}

/**
 * 设置是否启用屏幕空间接触阴影
 * 
 * @param instance 实例
 * @param enable 是否启用
 */
void RenderableManager::setScreenSpaceContactShadows(Instance const instance, bool const enable) noexcept {
    downcast(this)->setScreenSpaceContactShadows(instance, enable);
}

/**
 * 检查是否为阴影投射者
 * 
 * @param instance 实例
 * @return 如果是阴影投射者则返回 true
 */
bool RenderableManager::isShadowCaster(Instance const instance) const noexcept {
    return downcast(this)->isShadowCaster(instance);
}

/**
 * 检查是否为阴影接收者
 * 
 * @param instance 实例
 * @return 如果是阴影接收者则返回 true
 */
bool RenderableManager::isShadowReceiver(Instance const instance) const noexcept {
    return downcast(this)->isShadowReceiver(instance);
}

/**
 * 获取轴对齐包围盒
 * 
 * @param instance 实例
 * @return 包围盒引用
 */
const Box& RenderableManager::getAxisAlignedBoundingBox(Instance const instance) const noexcept {
    return downcast(this)->getAxisAlignedBoundingBox(instance);
}

/**
 * 获取层掩码
 * 
 * @param instance 实例
 * @return 层掩码
 */
uint8_t RenderableManager::getLayerMask(Instance const instance) const noexcept {
    return downcast(this)->getLayerMask(instance);
}

/**
 * 获取图元数量
 * 
 * @param instance 实例
 * @return 图元数量
 */
size_t RenderableManager::getPrimitiveCount(Instance const instance) const noexcept {
    return downcast(this)->getPrimitiveCount(instance, 0);
}

/**
 * 获取实例数量（用于实例化渲染）
 * 
 * @param instance 实例
 * @return 实例数量
 */
size_t RenderableManager::getInstanceCount(Instance instance) const noexcept {
    return downcast(this)->getInstanceCount(instance);
}

/**
 * 设置指定图元的材质实例
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @param materialInstance 材质实例指针
 */
void RenderableManager::setMaterialInstanceAt(Instance const instance,
        size_t const primitiveIndex, MaterialInstance const* materialInstance) {
    downcast(this)->setMaterialInstanceAt(instance, 0, primitiveIndex, downcast(materialInstance));
}

/**
 * 清除指定图元的材质实例
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 */
void RenderableManager::clearMaterialInstanceAt(Instance instance, size_t primitiveIndex) {
    downcast(this)->clearMaterialInstanceAt(instance, 0, primitiveIndex);
}

/**
 * 获取指定图元的材质实例
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @return 材质实例指针
 */
MaterialInstance* RenderableManager::getMaterialInstanceAt(
        Instance const instance, size_t const primitiveIndex) const noexcept {
    return downcast(this)->getMaterialInstanceAt(instance, 0, primitiveIndex);
}

/**
 * 设置指定图元的混合顺序
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @param order 混合顺序值
 */
void RenderableManager::setBlendOrderAt(Instance const instance, size_t const primitiveIndex, uint16_t const order) noexcept {
    downcast(this)->setBlendOrderAt(instance, 0, primitiveIndex, order);
}

/**
 * 设置指定图元的全局混合顺序启用状态
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @param enabled 是否启用全局混合顺序
 */
void RenderableManager::setGlobalBlendOrderEnabledAt(Instance const instance,
        size_t const primitiveIndex, bool const enabled) noexcept {
    downcast(this)->setGlobalBlendOrderEnabledAt(instance, 0, primitiveIndex, enabled);
}

/**
 * 获取指定图元启用的属性
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @return 属性位集合
 */
AttributeBitset RenderableManager::getEnabledAttributesAt(Instance const instance, size_t const primitiveIndex) const noexcept {
    return downcast(this)->getEnabledAttributesAt(instance, 0, primitiveIndex);
}

/**
 * 设置指定图元的几何数据
 * 
 * @param instance 实例
 * @param primitiveIndex 图元索引
 * @param type 图元类型
 * @param vertices 顶点缓冲区指针
 * @param indices 索引缓冲区指针
 * @param offset 索引偏移量
 * @param count 索引数量
 */
void RenderableManager::setGeometryAt(Instance const instance, size_t const primitiveIndex,
        PrimitiveType const type, VertexBuffer* vertices, IndexBuffer* indices,
        size_t const offset, size_t const count) noexcept {
    downcast(this)->setGeometryAt(instance, 0, primitiveIndex,
            type, downcast(vertices), downcast(indices), offset, count);
}

/**
 * 设置骨骼变换（Bone 结构体版本）
 * 
 * @param instance 实例
 * @param transforms 骨骼变换数组指针（Bone 结构体）
 * @param boneCount 骨骼数量
 * @param offset 起始偏移量
 */
void RenderableManager::setBones(Instance const instance,
        Bone const* transforms, size_t const boneCount, size_t const offset) {
    downcast(this)->setBones(instance, transforms, boneCount, offset);
}

/**
 * 设置骨骼变换（mat4f 矩阵版本）
 * 
 * @param instance 实例
 * @param transforms 骨骼变换矩阵数组指针（4x4 矩阵）
 * @param boneCount 骨骼数量
 * @param offset 起始偏移量
 */
void RenderableManager::setBones(Instance const instance,
        mat4f const* transforms, size_t const boneCount, size_t const offset) {
    downcast(this)->setBones(instance, transforms, boneCount, offset);
}

/**
 * 设置蒙皮缓冲区
 * 
 * @param instance 实例
 * @param skinningBuffer 蒙皮缓冲区指针
 * @param count 骨骼数量
 * @param offset 起始偏移量
 */
void RenderableManager::setSkinningBuffer(Instance const instance,
        SkinningBuffer* skinningBuffer, size_t const count, size_t const offset) {
    downcast(this)->setSkinningBuffer(instance, downcast(skinningBuffer), count, offset);
}

/**
 * 设置变形权重
 * 
 * @param instance 实例
 * @param weights 权重数组指针
 * @param count 权重数量
 * @param offset 起始偏移量
 */
void RenderableManager::setMorphWeights(Instance const instance, float const* weights,
        size_t const count, size_t const offset) {
    downcast(this)->setMorphWeights(instance, weights, count, offset);
}

/**
 * 设置指定图元的变形目标缓冲区偏移
 * 
 * @param instance 实例
 * @param level 级别
 * @param primitiveIndex 图元索引
 * @param offset 偏移量
 */
void RenderableManager::setMorphTargetBufferOffsetAt(Instance const instance, uint8_t const level,
        size_t const primitiveIndex,
        size_t const offset) {
    downcast(this)->setMorphTargetBufferOffsetAt(instance, level, primitiveIndex, offset);
}

/**
 * 获取变形目标缓冲区
 * 
 * @param instance 实例
 * @return 变形目标缓冲区指针
 */
MorphTargetBuffer* RenderableManager::getMorphTargetBuffer(Instance const instance) const noexcept {
    return downcast(this)->getMorphTargetBuffer(instance);
}

/**
 * 获取变形目标数量
 * 
 * @param instance 实例
 * @return 变形目标数量
 */
size_t RenderableManager::getMorphTargetCount(Instance const instance) const noexcept {
    return downcast(this)->getMorphTargetCount(instance);
}

/**
 * 设置光源通道
 * 
 * @param instance 实例
 * @param channel 通道索引
 * @param enable 是否启用
 */
void RenderableManager::setLightChannel(Instance const instance, unsigned int const channel, bool const enable) noexcept {
    downcast(this)->setLightChannel(instance, channel, enable);
}

/**
 * 获取光源通道状态
 * 
 * @param instance 实例
 * @param channel 通道索引
 * @return 如果启用则返回 true
 */
bool RenderableManager::getLightChannel(Instance const instance, unsigned int const channel) const noexcept {
    return downcast(this)->getLightChannel(instance, channel);
}

/**
 * 设置雾效启用状态
 * 
 * @param instance 实例
 * @param enable 是否启用雾效
 */
void RenderableManager::setFogEnabled(Instance const instance, bool const enable) noexcept {
    downcast(this)->setFogEnabled(instance, enable);
}

/**
 * 获取雾效启用状态
 * 
 * @param instance 实例
 * @return 如果启用则返回 true
 */
bool RenderableManager::getFogEnabled(Instance const instance) const noexcept {
    return downcast(this)->getFogEnabled(instance);
}

} // namespace filament
