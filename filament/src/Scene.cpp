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

#include "details/Scene.h"

#include "details/IndirectLight.h"
#include "details/Skybox.h"

using namespace utils;

namespace filament {

/**
 * 设置天空盒
 * 
 * 设置场景的天空盒。天空盒用于渲染背景环境。
 * 
 * @param skybox 天空盒指针
 *               - 如果为 nullptr，则移除天空盒
 *               - 如果非空，则设置新的天空盒
 * 
 * 实现：将调用转发到内部实现类设置天空盒
 */
void Scene::setSkybox(Skybox* skybox) noexcept {
    downcast(this)->setSkybox(downcast(skybox));
}

/**
 * 获取天空盒
 * 
 * 返回场景当前使用的天空盒。
 * 
 * @return 天空盒指针，如果没有设置则返回 nullptr
 * 
 * 实现：从内部实现类获取天空盒
 */
Skybox* Scene::getSkybox() const noexcept {
    return downcast(this)->getSkybox();
}

/**
 * 设置间接光
 * 
 * 设置场景的间接光（IBL - Image Based Lighting）。
 * 间接光用于环境光照和反射。
 * 
 * @param ibl 间接光指针
 *            - 如果为 nullptr，则移除间接光
 *            - 如果非空，则设置新的间接光
 * 
 * 实现：将调用转发到内部实现类设置间接光
 */
void Scene::setIndirectLight(IndirectLight* ibl) noexcept {
    downcast(this)->setIndirectLight(downcast(ibl));
}

/**
 * 获取间接光
 * 
 * 返回场景当前使用的间接光。
 * 
 * @return 间接光指针，如果没有设置则返回 nullptr
 * 
 * 实现：从内部实现类获取间接光
 */
IndirectLight* Scene::getIndirectLight() const noexcept {
    return downcast(this)->getIndirectLight();
}

/**
 * 添加实体
 * 
 * 向场景添加一个实体。实体可以是渲染对象、光源、相机等。
 * 
 * @param entity 实体ID
 * 
 * 实现：将调用转发到内部实现类添加实体
 * 
 * 注意：如果实体已存在于场景中，此操作可能无效
 */
void Scene::addEntity(Entity const entity) {
    downcast(this)->addEntity(entity);
}

/**
 * 批量添加实体
 * 
 * 向场景批量添加多个实体。这比逐个添加更高效。
 * 
 * @param entities 实体ID数组指针
 * @param count 实体数量
 * 
 * 实现：将调用转发到内部实现类批量添加实体
 * 
 * 注意：
 * - entities 数组必须至少包含 count 个元素
 * - 如果某些实体已存在于场景中，它们可能被跳过
 */
void Scene::addEntities(const Entity* entities, size_t const count) {
    downcast(this)->addEntities(entities, count);
}

/**
 * 移除实体
 * 
 * 从场景中移除一个实体。
 * 
 * @param entity 要移除的实体ID
 * 
 * 实现：将调用转发到内部实现类移除实体
 * 
 * 注意：如果实体不存在于场景中，此操作无效
 */
void Scene::remove(Entity const entity) {
    downcast(this)->remove(entity);
}

/**
 * 批量移除实体
 * 
 * 从场景中批量移除多个实体。这比逐个移除更高效。
 * 
 * @param entities 要移除的实体ID数组指针
 * @param count 实体数量
 * 
 * 实现：将调用转发到内部实现类批量移除实体
 * 
 * 注意：
 * - entities 数组必须至少包含 count 个元素
 * - 不存在的实体会被跳过
 */
void Scene::removeEntities(const Entity* entities, size_t const count) {
    downcast(this)->removeEntities(entities, count);
}

/**
 * 移除所有实体
 * 
 * 从场景中移除所有实体。这不会移除天空盒和间接光。
 * 
 * 实现：将调用转发到内部实现类清空所有实体
 */
void Scene::removeAllEntities() noexcept {
    downcast(this)->removeAllEntities();
}

/**
 * 获取实体数量
 * 
 * 返回场景中实体的总数（包括渲染对象、光源、相机等）。
 * 
 * @return 实体数量
 * 
 * 实现：从内部实现类获取实体计数
 */
size_t Scene::getEntityCount() const noexcept {
    return downcast(this)->getEntityCount();
}

/**
 * 获取可渲染对象数量
 * 
 * 返回场景中可渲染对象的数量。
 * 
 * @return 可渲染对象数量
 * 
 * 实现：从内部实现类获取可渲染对象计数
 */
size_t Scene::getRenderableCount() const noexcept {
    return downcast(this)->getRenderableCount();
}

/**
 * 获取光源数量
 * 
 * 返回场景中光源的数量。
 * 
 * @return 光源数量
 * 
 * 实现：从内部实现类获取光源计数
 */
size_t Scene::getLightCount() const noexcept {
    return downcast(this)->getLightCount();
}

/**
 * 检查实体是否存在
 * 
 * 检查指定的实体是否存在于场景中。
 * 
 * @param entity 要检查的实体ID
 * @return true 如果实体存在于场景中，false 否则
 * 
 * 实现：从内部实现类查询实体是否存在
 */
bool Scene::hasEntity(Entity const entity) const noexcept {
    return downcast(this)->hasEntity(entity);
}

/**
 * 遍历所有实体
 * 
 * 对场景中的每个实体调用指定的函数。
 * 
 * @param functor 函数对象（会被移动）
 *                函数签名：void(Entity)
 *                对每个实体调用此函数
 * 
 * 实现：将调用转发到内部实现类遍历实体
 * 
 * 注意：functor 使用移动语义，调用后原对象不再有效
 */
void Scene::forEach(Invocable<void(Entity)>&& functor) const noexcept {
    downcast(this)->forEach(std::move(functor));
}

} // namespace filament
