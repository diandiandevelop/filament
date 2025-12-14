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

#ifndef TNT_FILAMENT_TRANSFORMMANAGER_H
#define TNT_FILAMENT_TRANSFORMMANAGER_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>
#include <utils/EntityInstance.h>

#include <math/mathfwd.h>

#include <iterator>

#include <stddef.h>

namespace utils {
class Entity;
} // namespace utils

namespace filament {

class FTransformManager;

/**
 * TransformManager is used to add transform components to entities.
 *
 * A Transform component gives an entity a position and orientation in space in the coordinate
 * space of its parent transform. The TransformManager takes care of computing the world-space
 * transform of each component (i.e. its transform relative to the root).
 *
 * Creation and destruction
 * ========================
 *
 * A transform component is created using TransformManager::create() and destroyed by calling
 * TransformManager::destroy().
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *  utils::Entity object = utils::EntityManager.get().create();
 *
 *  auto& tcm = engine->getTransformManager();
 *
 *  // create the transform component
 *  tcm.create(object);
 *
 *  // set its transform
 *  auto i = tcm.getInstance(object);
 *  tcm.setTransform(i, mat4f::translation({ 0, 0, -1 }));
 *
 *  // destroy the transform component
 *  tcm.destroy(object);
 * ~~~~~~~~~~~
 *
 */
/**
 * TransformManager 用于向实体添加变换组件。
 *
 * 变换组件为实体提供在其父变换的坐标空间中的位置和方向。
 * TransformManager 负责计算每个组件的世界空间变换（即相对于根节点的变换）。
 *
 * 创建和销毁
 * ========================
 *
 * 变换组件使用 TransformManager::create() 创建，并通过调用
 * TransformManager::destroy() 销毁。
 */
class UTILS_PUBLIC TransformManager : public FilamentAPI {
public:
    using Instance = utils::EntityInstance<TransformManager>;  //!< 实例类型

    /**
     * 子节点迭代器类，用于遍历变换组件的子节点
     */
    class children_iterator {
        friend class FTransformManager;
        TransformManager const& mManager;
        Instance mInstance;
        children_iterator(TransformManager const& mgr, Instance instance) noexcept
                : mManager(mgr), mInstance(instance) { }
    public:
        using value_type = Instance;
        using difference_type = ptrdiff_t;
        using pointer = Instance*;
        using reference = Instance&;
        using iterator_category = std::forward_iterator_tag;

        children_iterator& operator++();

        children_iterator operator++(int) { // NOLINT
            children_iterator ret(*this);
            ++(*this);
            return ret;
        }

        bool operator == (const children_iterator& other) const noexcept {
            return mInstance == other.mInstance;
        }

        bool operator != (const children_iterator& other) const noexcept {
            return mInstance != other.mInstance;
        }

        value_type operator*() const { return mInstance; }
    };

    /**
     * Returns whether a particular Entity is associated with a component of this TransformManager
     * @param e An Entity.
     * @return true if this Entity has a component associated with this manager.
     */
    /**
     * 返回特定实体是否与此 TransformManager 的组件关联
     * @param e 实体
     * @return 如果此实体有与此管理器关联的组件则返回 true
     */
    bool hasComponent(utils::Entity e) const noexcept;

    /**
     * Gets an Instance representing the transform component associated with the given Entity.
     * @param e An Entity.
     * @return An Instance object, which represents the transform component associated with the Entity e.
     * @note Use Instance::isValid() to make sure the component exists.
     * @see hasComponent()
     */
    /**
     * 获取表示与给定实体关联的变换组件的 Instance。
     * @param e 实体
     * @return 表示与实体 e 关联的变换组件的 Instance 对象
     * @note 使用 Instance::isValid() 确保组件存在
     * @see hasComponent()
     */
    Instance getInstance(utils::Entity e) const noexcept;

    /**
     * @return the number of Components
     */
    /**
     * @return 组件数量
     */
    size_t getComponentCount() const noexcept;

    /**
     * @return true if the this manager has no components
     */
    /**
     * @return 如果此管理器没有组件则返回 true
     */
    bool empty() const noexcept;

    /**
     * Retrieve the `Entity` of the component from its `Instance`.
     * @param i Instance of the component obtained from getInstance()
     * @return
     */
    /**
     * 从其 `Instance` 检索组件的 `Entity`。
     * @param i 从 getInstance() 获取的组件实例
     * @return 实体
     */
    utils::Entity getEntity(Instance i) const noexcept;

    /**
     * Retrieve the Entities of all the components of this manager.
     * @return A list, in no particular order, of all the entities managed by this manager.
     */
    /**
     * 检索此管理器所有组件的实体。
     * @return 此管理器管理的所有实体的列表（无特定顺序）
     */
    utils::Entity const* UTILS_NONNULL getEntities() const noexcept;

    /**
     * Enables or disable the accurate translation mode. Disabled by default.
     *
     * When accurate translation mode is active, the translation component of all transforms is
     * maintained at double precision. This is only useful if the mat4 version of setTransform()
     * is used, as well as getTransformAccurate().
     *
     * @param enable true to enable the accurate translation mode, false to disable.
     *
     * @see isAccurateTranslationsEnabled
     * @see create(utils::Entity, Instance, const math::mat4&);
     * @see setTransform(Instance, const math::mat4&)
     * @see getTransformAccurate
     * @see getWorldTransformAccurate
     */
    /**
     * 启用或禁用精确平移模式。默认禁用。
     *
     * 当精确平移模式激活时，所有变换的平移分量
     * 保持在双精度。这仅在使用 setTransform() 的 mat4 版本
     * 以及 getTransformAccurate() 时才有用。
     *
     * @param enable true 表示启用精确平移模式，false 表示禁用
     *
     * @see isAccurateTranslationsEnabled
     * @see create(utils::Entity, Instance, const math::mat4&);
     * @see setTransform(Instance, const math::mat4&)
     * @see getTransformAccurate
     * @see getWorldTransformAccurate
     */
    void setAccurateTranslationsEnabled(bool enable) noexcept;

    /**
     * Returns whether the high precision translation mode is active.
     * @return true if accurate translations mode is active, false otherwise
     * @see setAccurateTranslationsEnabled
     */
    /**
     * 返回高精度平移模式是否激活。
     * @return 如果精确平移模式激活则返回 true，否则返回 false
     * @see setAccurateTranslationsEnabled
     */
    bool isAccurateTranslationsEnabled() const noexcept;

    /**
     * Creates a transform component and associate it with the given entity.
     * @param entity            An Entity to associate a transform component to.
     * @param parent            The Instance of the parent transform, or Instance{} if no parent.
     * @param localTransform    The transform to initialize the transform component with.
     *                          This is always relative to the parent.
     *
     * If this component already exists on the given entity, it is first destroyed as if
     * destroy(utils::Entity e) was called.
     *
     * @see destroy()
     */
    /**
     * 创建变换组件并将其与给定实体关联。
     * @param entity            要关联变换组件的实体
     * @param parent            父变换的 Instance，如果没有父节点则为 Instance{}
     * @param localTransform    用于初始化变换组件的变换。
     *                          这始终相对于父节点。
     *
     * 如果给定实体上已存在此组件，则首先销毁它，就像
     * 调用了 destroy(utils::Entity e) 一样。
     *
     * @see destroy()
     */
    void create(utils::Entity entity, Instance parent, const math::mat4f& localTransform);
    void create(utils::Entity entity, Instance parent, const math::mat4& localTransform); //!< \overload
    /**
     * 重载版本：使用双精度矩阵，支持精确平移
     */
    void create(utils::Entity entity, Instance parent = {}); //!< \overload
    /**
     * 重载版本：创建时不指定局部变换，使用单位矩阵
     */

    /**
     * Destroys this component from the given entity, children are orphaned.
     * @param e An entity.
     *
     * @note If this transform had children, these are orphaned, which means their local
     * transform becomes a world transform. Usually it's nonsensical. It's recommended to make
     * sure that a destroyed transform doesn't have children.
     *
     * @see create()
     */
    /**
     * 从给定实体销毁此组件，子节点将变为孤立节点。
     * @param e 实体
     *
     * @note 如果此变换有子节点，这些子节点将变为孤立节点，这意味着它们的局部
     * 变换变为世界变换。通常这是没有意义的。建议确保
     * 被销毁的变换没有子节点。
     *
     * @see create()
     */
    void destroy(utils::Entity e) noexcept;

    /**
     * Re-parents an entity to a new one.
     * @param i             The instance of the transform component to re-parent
     * @param newParent     The instance of the new parent transform
     * @attention It is an error to re-parent an entity to a descendant and will cause undefined behaviour.
     * @see getInstance()
     */
    /**
     * 将实体重新指定父节点。
     * @param i             要重新指定父节点的变换组件实例
     * @param newParent     新父变换的实例
     * @attention 将实体重新指定为其后代的父节点是错误的，会导致未定义行为。
     * @see getInstance()
     */
    void setParent(Instance i, Instance newParent) noexcept;

    /**
     * Returns the parent of a transform component, or the null entity if it is a root.
     * @param i The instance of the transform component to query.
     */
    /**
     * 返回变换组件的父节点，如果是根节点则返回空实体。
     * @param i 要查询的变换组件实例
     */
    utils::Entity getParent(Instance i) const noexcept;

    /**
     * Returns the number of children of a transform component.
     * @param i The instance of the transform component to query.
     * @return The number of children of the queried component.
     */
    /**
     * 返回变换组件的子节点数量。
     * @param i 要查询的变换组件实例
     * @return 查询组件的子节点数量
     */
    size_t getChildCount(Instance i) const noexcept;

    /**
     * Gets a list of children for a transform component.
     *
     * @param i The instance of the transform component to query.
     * @param children Pointer to array-of-Entity. The array must have at least "count" elements.
     * @param count The maximum number of children to retrieve.
     * @return The number of children written to the pointer.
     */
    /**
     * 获取变换组件的子节点列表。
     *
     * @param i 要查询的变换组件实例
     * @param children 指向 Entity 数组的指针。数组必须至少有 "count" 个元素
     * @param count 要检索的最大子节点数量
     * @return 写入指针的子节点数量
     */
    size_t getChildren(Instance i, utils::Entity* UTILS_NONNULL children, size_t count) const noexcept;

    /**
     * Returns an iterator to the Instance of the first child of the given parent.
     *
     * @param parent Instance of the parent
     * @return A forward iterator pointing to the first child of the given parent.
     *
     * A child_iterator can only safely be dereferenced if it's different from getChildrenEnd(parent)
     */
    /**
     * 返回指向给定父节点第一个子节点的 Instance 的迭代器。
     *
     * @param parent 父节点实例
     * @return 指向给定父节点第一个子节点的前向迭代器
     *
     * 只有当 child_iterator 与 getChildrenEnd(parent) 不同时才能安全解引用
     */
    children_iterator getChildrenBegin(Instance parent) const noexcept;

    /**
     * Returns an undreferencable iterator representing the end of the children list
     *
     * @param parent Instance of the parent
     * @return A forward iterator.
     *
     * This iterator cannot be dereferenced
     */
    /**
     * 返回表示子节点列表末尾的不可解引用迭代器
     *
     * @param parent 父节点实例
     * @return 前向迭代器
     *
     * 此迭代器不能解引用
     */
    children_iterator getChildrenEnd(Instance parent) const noexcept;

    /**
     * Sets a local transform of a transform component.
     * @param ci              The instance of the transform component to set the local transform to.
     * @param localTransform  The local transform (i.e. relative to the parent).
     * @see getTransform()
     * @attention This operation can be slow if the hierarchy of transform is too deep, and this
     *            will be particularly bad when updating a lot of transforms. In that case,
     *            consider using openLocalTransformTransaction() / commitLocalTransformTransaction().
     */
    /**
     * 设置变换组件的局部变换。
     * @param ci              要设置局部变换的变换组件实例
     * @param localTransform  局部变换（即相对于父节点）
     * @see getTransform()
     * @attention 如果变换层次结构太深，此操作可能会很慢，当更新大量变换时
     *            这尤其糟糕。在这种情况下，
     *            考虑使用 openLocalTransformTransaction() / commitLocalTransformTransaction()。
     */
    void setTransform(Instance ci, const math::mat4f& localTransform) noexcept;

    /**
     * Sets a local transform of a transform component and keeps double precision translation.
     * All other values of the transform are stored at single precision.
     * @param ci              The instance of the transform component to set the local transform to.
     * @param localTransform  The local transform (i.e. relative to the parent).
     * @see getTransform()
     * @attention This operation can be slow if the hierarchy of transform is too deep, and this
     *            will be particularly bad when updating a lot of transforms. In that case,
     *            consider using openLocalTransformTransaction() / commitLocalTransformTransaction().
     */
    /**
     * 设置变换组件的局部变换并保持双精度平移。
     * 变换的所有其他值以单精度存储。
     * @param ci              要设置局部变换的变换组件实例
     * @param localTransform  局部变换（即相对于父节点）
     * @see getTransform()
     * @attention 如果变换层次结构太深，此操作可能会很慢，当更新大量变换时
     *            这尤其糟糕。在这种情况下，
     *            考虑使用 openLocalTransformTransaction() / commitLocalTransformTransaction()。
     */
    void setTransform(Instance ci, const math::mat4& localTransform) noexcept;

    /**
     * Returns the local transform of a transform component.
     * @param ci The instance of the transform component to query the local transform from.
     * @return The local transform of the component (i.e. relative to the parent). This always
     *         returns the value set by setTransform().
     * @see setTransform()
     */
    /**
     * 返回变换组件的局部变换。
     * @param ci 要查询局部变换的变换组件实例
     * @return 组件的局部变换（即相对于父节点）。这始终
     *         返回由 setTransform() 设置的值。
     * @see setTransform()
     */
    const math::mat4f& getTransform(Instance ci) const noexcept;

    /**
     * Returns the local transform of a transform component.
     * @param ci The instance of the transform component to query the local transform from.
     * @return The local transform of the component (i.e. relative to the parent). This always
     *         returns the value set by setTransform().
     * @see setTransform()
     */
    /**
     * 返回变换组件的局部变换（精确版本，双精度平移）。
     * @param ci 要查询局部变换的变换组件实例
     * @return 组件的局部变换（即相对于父节点）。这始终
     *         返回由 setTransform() 设置的值。
     * @see setTransform()
     */
    math::mat4 getTransformAccurate(Instance ci) const noexcept;

    /**
     * Return the world transform of a transform component.
     * @param ci The instance of the transform component to query the world transform from.
     * @return The world transform of the component (i.e. relative to the root). This is the
     *         composition of this component's local transform with its parent's world transform.
     * @see setTransform()
     */
    /**
     * 返回变换组件的世界变换。
     * @param ci 要查询世界变换的变换组件实例
     * @return 组件的世界变换（即相对于根节点）。这是
     *         此组件的局部变换与其父节点的世界变换的组合。
     * @see setTransform()
     */
    const math::mat4f& getWorldTransform(Instance ci) const noexcept;

    /**
     * Return the world transform of a transform component.
     * @param ci The instance of the transform component to query the world transform from.
     * @return The world transform of the component (i.e. relative to the root). This is the
     *         composition of this component's local transform with its parent's world transform.
     * @see setTransform()
     */
    /**
     * 返回变换组件的世界变换（精确版本，双精度平移）。
     * @param ci 要查询世界变换的变换组件实例
     * @return 组件的世界变换（即相对于根节点）。这是
     *         此组件的局部变换与其父节点的世界变换的组合。
     * @see setTransform()
     */
    math::mat4 getWorldTransformAccurate(Instance ci) const noexcept;

    /**
     * Opens a local transform transaction. During a transaction, getWorldTransform() can
     * return an invalid transform until commitLocalTransformTransaction() is called. However,
     * setTransform() will perform significantly better and in constant time.
     *
     * This is useful when updating many transforms and the transform hierarchy is deep (say more
     * than 4 or 5 levels).
     *
     * @note If the local transform transaction is already open, this is a no-op.
     *
     * @see commitLocalTransformTransaction(), setTransform()
     */
    /**
     * 打开局部变换事务。在事务期间，getWorldTransform() 可以
     * 返回无效变换，直到调用 commitLocalTransformTransaction()。但是，
     * setTransform() 将执行得更好，并且是常数时间。
     *
     * 这在更新大量变换且变换层次结构很深（例如超过
     * 4 或 5 级）时很有用。
     *
     * @note 如果局部变换事务已打开，这是无操作。
     *
     * @see commitLocalTransformTransaction(), setTransform()
     */
    void openLocalTransformTransaction() noexcept;

    /**
     * Commits the currently open local transform transaction. When this returns, calls
     * to getWorldTransform() will return the proper value.
     *
     * @attention failing to call this method when done updating the local transform will cause
     *            a lot of rendering problems. The system never closes the transaction
     *            automatically.
     *
     * @note If the local transform transaction is not open, this is a no-op.
     *
     * @see openLocalTransformTransaction(), setTransform()
     */
    /**
     * 提交当前打开的局部变换事务。当此方法返回时，对
     * getWorldTransform() 的调用将返回正确的值。
     *
     * @attention 在完成局部变换更新后未能调用此方法将导致
     *            大量渲染问题。系统永远不会自动关闭事务。
     *
     * @note 如果局部变换事务未打开，这是无操作。
     *
     * @see openLocalTransformTransaction(), setTransform()
     */
    void commitLocalTransformTransaction() noexcept;

protected:
    // prevent heap allocation
    ~TransformManager() = default;
};

} // namespace filament


#endif // TNT_TRANSFORMMANAGER_H
