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

#include "components/CameraManager.h"

#include "details/Engine.h"
#include "details/Camera.h"

#include <utils/Entity.h>
#include <utils/Logger.h>
#include <utils/debug.h>

using namespace utils;
using namespace filament::math;

namespace filament {

/**
 * 相机管理器构造函数
 * 
 * @param engine 引擎引用（未使用，但保留以保持接口一致性）
 */
FCameraManager::FCameraManager(FEngine&) noexcept {
}

/**
 * 相机管理器析构函数
 */
FCameraManager::~FCameraManager() noexcept = default;

/**
 * 终止相机管理器
 * 
 * 清理所有泄漏的相机组件。
 * 
 * @param engine 引擎引用
 */
void FCameraManager::terminate(FEngine& engine) noexcept {
    auto& manager = mManager;
    if (!manager.empty()) {  // 如果管理器不为空
        DLOG(INFO) << "cleaning up " << manager.getComponentCount() << " leaked Camera components";  // 记录泄漏的组件数
        Slice<const Entity> entities{ manager.getEntities(), manager.getComponentCount() };  // 获取所有实体
        for (Entity const e : entities) {
            destroy(engine, e);  // 销毁每个实体的相机组件
        }
    }
}

/**
 * 垃圾回收
 * 
 * 清理已删除实体的相机组件。
 * 
 * @param engine 引擎引用
 * @param em 实体管理器引用
 */
void FCameraManager::gc(FEngine& engine, EntityManager& em) noexcept {
    auto& manager = mManager;
    /**
     * 调用管理器的垃圾回收，对每个已删除的实体调用销毁函数
     */
    manager.gc(em, [this, &engine](Entity const e) {
        destroy(engine, e);  // 销毁相机组件
    });
}

/**
 * 创建相机组件
 * 
 * 为指定实体创建相机组件。
 * 如果实体已有相机组件，则先销毁旧的。
 * 
 * @param engine 引擎引用
 * @param entity 实体
 * @return 相机指针
 */
FCamera* FCameraManager::create(FEngine& engine, Entity entity) {
    auto& manager = mManager;

    /**
     * 如果实体已有相机组件，先销毁它
     */
    // if this entity already has Camera component, destroy it.
    if (UTILS_UNLIKELY(manager.hasComponent(entity))) {
        destroy(engine, entity);  // 销毁现有组件
    }

    /**
     * 添加相机组件到实体
     */
    // add the Camera component to the entity
    Instance const i = manager.addComponent(entity);  // 添加组件并获取实例

    /**
     * 创建 FCamera 对象
     * 
     * 由于历史原因，FCamera 不能移动，所以相机管理器存储指针。
     */
    // For historical reasons, FCamera must not move. So the CameraManager stores a pointer.
    FCamera* const camera = engine.getHeapAllocator().make<FCamera>(engine, entity);  // 在堆上分配相机对象
    manager.elementAt<CAMERA>(i) = camera;  // 存储相机指针
    manager.elementAt<OWNS_TRANSFORM_COMPONENT>(i) = false;  // 初始化为不拥有变换组件

    /**
     * 确保实体有变换组件
     * 
     * 相机需要变换组件来定位和定向。
     */
    // Make sure we have a transform component
    FTransformManager& tcm = engine.getTransformManager();  // 获取变换管理器
    if (!tcm.hasComponent(entity)) {  // 如果实体没有变换组件
        tcm.create(entity);  // 创建变换组件
        manager.elementAt<OWNS_TRANSFORM_COMPONENT>(i) = true;  // 标记为拥有变换组件
    }
    return camera;  // 返回相机指针
}

/**
 * 销毁相机组件
 * 
 * 销毁指定实体的相机组件。
 * 如果相机管理器创建了变换组件，也会一并销毁。
 * 
 * @param engine 引擎引用
 * @param e 实体
 */
void FCameraManager::destroy(FEngine& engine, Entity const e) noexcept {
    auto& manager = mManager;
    if (Instance const i = manager.getInstance(e) ; i) {  // 如果实体有相机组件
        /**
         * 检查是否拥有变换组件
         */
        // destroy the FCamera object
        bool const ownsTransformComponent = manager.elementAt<OWNS_TRANSFORM_COMPONENT>(i);  // 获取是否拥有变换组件

        { 
            /**
             * 作用域：相机在此作用域后失效
             */
            // scope for camera -- it's invalid after this scope.
            FCamera* const camera = manager.elementAt<CAMERA>(i);  // 获取相机指针
            assert_invariant(camera);  // 断言相机指针有效
            camera->terminate(engine);  // 终止相机
            engine.getHeapAllocator().destroy(camera);  // 销毁相机对象

            /**
             * 移除相机组件
             */
            // Remove the camera component
            manager.removeComponent(e);  // 从管理器移除组件
        }

        /**
         * 如果相机管理器创建了变换组件，也一并销毁
         */
        // if we added the transform component, remove it.
        if (ownsTransformComponent) {
            engine.getTransformManager().destroy(e);  // 销毁变换组件
        }
    }
}

} // namespace filament
