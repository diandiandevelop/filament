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

#ifndef TNT_FILAMENT_SCENE_H
#define TNT_FILAMENT_SCENE_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>
#include <utils/Invocable.h>

#include <stddef.h>

namespace utils {
    class Entity;
} // namespace utils

namespace filament {

class IndirectLight;
class Skybox;

/**
 * A Scene is a flat container of Renderable and Light instances.
 *
 * A Scene doesn't provide a hierarchy of Renderable objects, i.e.: it's not a scene-graph.
 * However, it manages the list of objects to render and the list of lights. Renderable
 * and Light objects can be added or removed from a Scene at any time.
 *
 * A Renderable *must* be added to a Scene in order to be rendered, and the Scene must be
 * provided to a View.
 *
 *
 * Creation and Destruction
 * ========================
 *
 * A Scene is created using Engine.createScene() and destroyed using
 * Engine.destroy(const Scene*).
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * #include <filament/Scene.h>
 * #include <filament/Engine.h>
 * using namespace filament;
 *
 * Engine* engine = Engine::create();
 *
 * Scene* scene = engine->createScene();
 * engine->destroy(&scene);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @see View, Renderable, Light
 */
/**
 * Scene 是 Renderable 和 Light 实例的扁平容器
 *
 * Scene 不提供 Renderable 对象的层次结构，即：它不是场景图。
 * 但是，它管理要渲染的对象列表和光源列表。Renderable
 * 和 Light 对象可以随时从 Scene 中添加或移除。
 *
 * Renderable *必须*添加到 Scene 才能被渲染，并且 Scene 必须
 * 提供给 View。
 *
 *
 * 创建和销毁
 * ========================
 *
 * Scene 使用 Engine.createScene() 创建，使用
 * Engine.destroy(const Scene*) 销毁。
 *
 * @see View, Renderable, Light
 */
class UTILS_PUBLIC Scene : public FilamentAPI {
public:

    /**
     * Sets the Skybox.
     *
     * The Skybox is drawn last and covers all pixels not touched by geometry.
     *
     * @param skybox The Skybox to use to fill untouched pixels, or nullptr to unset the Skybox.
     */
    /**
     * 设置天空盒
     *
     * 天空盒最后绘制，覆盖所有未被几何体触及的像素。
     *
     * @param skybox 用于填充未触及像素的 Skybox，或 nullptr 以取消设置 Skybox
     */
    void setSkybox(Skybox* UTILS_NULLABLE skybox) noexcept;

    /**
     * Returns the Skybox associated with the Scene.
     *
     * @return The associated Skybox, or nullptr if there is none.
     */
    /**
     * 返回与 Scene 关联的 Skybox
     *
     * @return 关联的 Skybox，如果没有则返回 nullptr
     */
    Skybox* UTILS_NULLABLE getSkybox() const noexcept;

    /**
     * Set the IndirectLight to use when rendering the Scene.
     *
     * Currently, a Scene may only have a single IndirectLight. This call replaces the current
     * IndirectLight.
     *
     * @param ibl The IndirectLight to use when rendering the Scene or nullptr to unset.
     * @see getIndirectLight
     */
    /**
     * 设置渲染 Scene 时使用的 IndirectLight
     *
     * 目前，Scene 只能有一个 IndirectLight。此调用会替换当前的
     * IndirectLight。
     *
     * @param ibl 渲染 Scene 时使用的 IndirectLight，或 nullptr 以取消设置
     * @see getIndirectLight
     */
    void setIndirectLight(IndirectLight* UTILS_NULLABLE ibl) noexcept;

    /**
     * Get the IndirectLight or nullptr if none is set.
     *
     * @return the the IndirectLight or nullptr if none is set
     * @see setIndirectLight
     */
    /**
     * 获取 IndirectLight，如果未设置则返回 nullptr
     *
     * @return IndirectLight，如果未设置则返回 nullptr
     * @see setIndirectLight
     */
    IndirectLight* UTILS_NULLABLE getIndirectLight() const noexcept;

    /**
     * Adds an Entity to the Scene.
     *
     * @param entity The entity is ignored if it doesn't have a Renderable or Light component.
     *
     * \attention
     *  A given Entity object can only be added once to a Scene.
     *
     */
    /**
     * 向 Scene 添加 Entity
     *
     * @param entity 如果实体没有 Renderable 或 Light 组件，则会被忽略
     *
     * \attention
     *  给定的 Entity 对象只能添加到 Scene 一次
     */
    void addEntity(utils::Entity entity);

    /**
     * Adds a list of entities to the Scene.
     *
     * @param entities Array containing entities to add to the scene.
     * @param count Size of the entity array.
     */
    /**
     * 向 Scene 添加实体列表
     *
     * @param entities 包含要添加到场景中的实体的数组
     * @param count 实体数组的大小
     */
    void addEntities(const utils::Entity* UTILS_NONNULL entities, size_t count);

    /**
     * Removes the Renderable from the Scene.
     *
     * @param entity The Entity to remove from the Scene. If the specified
     *                   \p entity doesn't exist, this call is ignored.
     */
    /**
     * 从 Scene 移除 Entity
     *
     * @param entity 要从 Scene 中移除的 Entity。如果指定的
     *                   \p entity 不存在，此调用将被忽略
     */
    void remove(utils::Entity entity);

    /**
     * Removes a list of entities to the Scene.
     *
     * This is equivalent to calling remove in a loop.
     * If any of the specified entities do not exist in the scene, they are skipped.
     *
     * @param entities Array containing entities to remove from the scene.
     * @param count Size of the entity array.
     */
    /**
     * 从 Scene 移除实体列表
     *
     * 这等同于在循环中调用 remove。
     * 如果任何指定的实体在场景中不存在，它们将被跳过。
     *
     * @param entities 包含要从场景中移除的实体的数组
     * @param count 实体数组的大小
     */
    void removeEntities(const utils::Entity* UTILS_NONNULL entities, size_t count);

    /**
     * Remove all entities to the Scene.
     */
    /**
     * 移除 Scene 中的所有实体
     */
    void removeAllEntities() noexcept;

    /**
     * Returns the total number of Entities in the Scene, whether alive or not.
     * @return Total number of Entities in the Scene.
     */
    /**
     * 返回 Scene 中的实体总数，无论是否存活
     * @return Scene 中的实体总数
     */
    size_t getEntityCount() const noexcept;

    /**
     * Returns the number of active (alive) Renderable objects in the Scene.
     *
     * @return The number of active (alive) Renderable objects in the Scene.
     */
    /**
     * 返回 Scene 中活动（存活）的 Renderable 对象数量
     *
     * @return Scene 中活动（存活）的 Renderable 对象数量
     */
    size_t getRenderableCount() const noexcept;

    /**
     * Returns the number of active (alive) Light objects in the Scene.
     *
     * @return The number of active (alive) Light objects in the Scene.
     */
    /**
     * 返回 Scene 中活动（存活）的 Light 对象数量
     *
     * @return Scene 中活动（存活）的 Light 对象数量
     */
    size_t getLightCount() const noexcept;

    /**
     * Returns true if the given entity is in the Scene.
     *
     * @return Whether the given entity is in the Scene.
     */
    /**
     * 如果给定实体在 Scene 中，则返回 true
     *
     * @return 给定实体是否在 Scene 中
     */
    bool hasEntity(utils::Entity entity) const noexcept;

    /**
     * Invokes user functor on each entity in the scene.
     *
     * It is not allowed to add or remove an entity from the scene within the functor.
     *
     * @param functor User provided functor called for each entity in the scene
     */
    /**
     * 对场景中的每个实体调用用户提供的函数对象
     *
     * 不允许在函数对象内添加或移除场景中的实体。
     *
     * @param functor 为场景中的每个实体调用的用户提供的函数对象
     */
    void forEach(utils::Invocable<void(utils::Entity entity)>&& functor) const noexcept;

protected:
    // prevent heap allocation
    ~Scene() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_SCENE_H
