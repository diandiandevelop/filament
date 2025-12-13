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

#include "components/TransformManager.h"

using namespace utils;

namespace filament {

using namespace math;

/**
 * 检查实体是否有变换组件
 * 
 * @param e 实体
 * @return 如果有组件则返回 true
 */
bool TransformManager::hasComponent(Entity const e) const noexcept {
    return downcast(this)->hasComponent(e);
}

/**
 * 获取组件数量
 * 
 * @return 组件数量
 */
size_t TransformManager::getComponentCount() const noexcept {
    return downcast(this)->getComponentCount();
}

/**
 * 检查是否为空
 * 
 * @return 如果为空则返回 true
 */
bool TransformManager::empty() const noexcept {
    return downcast(this)->empty();
}

/**
 * 获取实例对应的实体
 * 
 * @param i 实例
 * @return 实体
 */
Entity TransformManager::getEntity(Instance const i) const noexcept {
    return downcast(this)->getEntity(i);
}

/**
 * 获取所有实体数组
 * 
 * @return 实体数组指针
 */
Entity const* TransformManager::getEntities() const noexcept {
    return downcast(this)->getEntities();
}

/**
 * 获取实体对应的实例
 * 
 * @param e 实体
 * @return 实例
 */
TransformManager::Instance TransformManager::getInstance(Entity const e) const noexcept {
    return downcast(this)->getInstance(e);
}

/**
 * 创建变换组件（mat4f 版本）
 * 
 * @param entity 实体
 * @param parent 父实例
 * @param worldTransform 世界变换矩阵（4x4 float）
 */
void TransformManager::create(Entity const entity, Instance const parent, const mat4f& worldTransform) {
    downcast(this)->create(entity, parent, worldTransform);
}

/**
 * 创建变换组件（mat4 版本）
 * 
 * @param entity 实体
 * @param parent 父实例
 * @param worldTransform 世界变换矩阵（4x4 double，高精度）
 */
void TransformManager::create(Entity const entity, Instance const parent, const mat4& worldTransform) {
    downcast(this)->create(entity, parent, worldTransform);
}

/**
 * 创建变换组件（默认单位矩阵）
 * 
 * @param entity 实体
 * @param parent 父实例
 */
void TransformManager::create(Entity const entity, Instance const parent) {
    downcast(this)->create(entity, parent, mat4f{});
}

/**
 * 销毁变换组件
 * 
 * @param e 实体
 */
void TransformManager::destroy(Entity const e) noexcept {
    downcast(this)->destroy(e);
}

/**
 * 设置局部变换矩阵（mat4f 版本）
 * 
 * @param ci 实例
 * @param model 模型变换矩阵（4x4 float）
 */
void TransformManager::setTransform(Instance const ci, const mat4f& model) noexcept {
    downcast(this)->setTransform(ci, model);
}

/**
 * 设置局部变换矩阵（mat4 版本）
 * 
 * @param ci 实例
 * @param model 模型变换矩阵（4x4 double，高精度）
 */
void TransformManager::setTransform(Instance const ci, const mat4& model) noexcept {
    downcast(this)->setTransform(ci, model);
}

/**
 * 获取局部变换矩阵（mat4f 版本）
 * 
 * @param ci 实例
 * @return 局部变换矩阵引用（4x4 float）
 */
const mat4f& TransformManager::getTransform(Instance const ci) const noexcept {
    return downcast(this)->getTransform(ci);
}

/**
 * 获取局部变换矩阵（mat4 版本，高精度）
 * 
 * @param ci 实例
 * @return 局部变换矩阵（4x4 double）
 */
mat4 TransformManager::getTransformAccurate(Instance const ci) const noexcept {
    return downcast(this)->getTransformAccurate(ci);
}

/**
 * 获取世界变换矩阵（mat4f 版本）
 * 
 * @param ci 实例
 * @return 世界变换矩阵引用（4x4 float）
 */
const mat4f& TransformManager::getWorldTransform(Instance const ci) const noexcept {
    return downcast(this)->getWorldTransform(ci);
}

/**
 * 获取世界变换矩阵（mat4 版本，高精度）
 * 
 * @param ci 实例
 * @return 世界变换矩阵（4x4 double）
 */
mat4 TransformManager::getWorldTransformAccurate(Instance const ci) const noexcept {
    return downcast(this)->getWorldTransformAccurate(ci);
}

/**
 * 设置父节点
 * 
 * @param i 实例
 * @param newParent 新父实例
 */
void TransformManager::setParent(Instance const i, Instance const newParent) noexcept {
    downcast(this)->setParent(i, newParent);
}

/**
 * 获取父实体
 * 
 * @param i 实例
 * @return 父实体
 */
Entity TransformManager::getParent(Instance const i) const noexcept {
    return downcast(this)->getParent(i);
}

/**
 * 获取子节点数量
 * 
 * @param i 实例
 * @return 子节点数量
 */
size_t TransformManager::getChildCount(Instance const i) const noexcept {
    return downcast(this)->getChildCount(i);
}

/**
 * 获取子节点列表
 * 
 * @param i 实例
 * @param children 输出子实体数组
 * @param count 数组大小
 * @return 实际获取的子节点数量
 */
size_t TransformManager::getChildren(Instance const i, Entity* children,
        size_t const count) const noexcept {
    return downcast(this)->getChildren(i, children, count);
}

/**
 * 打开局部变换事务
 * 
 * 开始一个事务，允许批量修改局部变换而不立即更新世界变换。
 * 必须在修改完成后调用 commitLocalTransformTransaction()。
 */
void TransformManager::openLocalTransformTransaction() noexcept {
    downcast(this)->openLocalTransformTransaction();
}

/**
 * 提交局部变换事务
 * 
 * 结束事务并更新所有受影响的世界变换。
 */
void TransformManager::commitLocalTransformTransaction() noexcept {
    downcast(this)->commitLocalTransformTransaction();
}

/**
 * 获取子节点迭代器起始位置
 * 
 * @param parent 父实例
 * @return 子节点迭代器起始位置
 */
TransformManager::children_iterator TransformManager::getChildrenBegin(
        Instance const parent) const noexcept {
    return downcast(this)->getChildrenBegin(parent);
}

/**
 * 获取子节点迭代器结束位置
 * 
 * @param parent 父实例
 * @return 子节点迭代器结束位置
 */
TransformManager::children_iterator TransformManager::getChildrenEnd(
        Instance const parent) const noexcept {
    return downcast(this)->getChildrenEnd(parent);
}

/**
 * 设置是否启用精确平移
 * 
 * 启用后使用双精度浮点数进行平移计算，提高精度。
 * 
 * @param enable 是否启用
 */
void TransformManager::setAccurateTranslationsEnabled(bool const enable) noexcept {
    downcast(this)->setAccurateTranslationsEnabled(enable);
}

/**
 * 检查是否启用精确平移
 * 
 * @return 如果启用则返回 true
 */
bool TransformManager::isAccurateTranslationsEnabled() const noexcept {
    return downcast(this)->isAccurateTranslationsEnabled();
}

} // namespace filament
