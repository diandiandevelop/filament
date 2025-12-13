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

#ifndef TNT_FILAMENT_COMPONENTS_CAMERAMANAGER_H
#define TNT_FILAMENT_COMPONENTS_CAMERAMANAGER_H

#include "downcast.h"

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>
#include <utils/SingleInstanceComponentManager.h>
#include <utils/Entity.h>

namespace filament {

/**
 * 相机管理器公共接口
 * 
 * 提供相机组件的管理接口。
 */
class CameraManager : public FilamentAPI {
public:
    using Instance = utils::EntityInstance<CameraManager>;  // 实例类型
};

class FEngine;
class FCamera;

/**
 * 相机管理器实现类
 * 
 * 管理实体上的相机组件。
 * 
 * 功能：
 * - 创建和销毁相机组件
 * - 管理相机组件的生命周期
 * - 处理变换组件的创建和销毁（如果需要）
 */
class UTILS_PRIVATE FCameraManager : public CameraManager {
public:
    using Instance = Instance;  // 实例类型别名

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FCameraManager(FEngine& engine) noexcept;

    /**
     * 析构函数
     */
    ~FCameraManager() noexcept;

    /**
     * 终止相机管理器
     * 
     * 释放所有资源。
     * 
     * @param engine 引擎引用
     */
    // free-up all resources
    void terminate(FEngine& engine) noexcept;

    /**
     * 垃圾回收
     * 
     * 清理已删除实体的相机组件。
     * 
     * @param engine 引擎引用
     * @param em 实体管理器引用
     */
    void gc(FEngine& engine, utils::EntityManager& em) noexcept;

    /*
    * Component Manager APIs
    */

    /**
     * 检查实体是否有相机组件
     * 
     * @param e 实体
     * @return 如果有组件返回 true，否则返回 false
     */
    bool hasComponent(utils::Entity const e) const noexcept {
        return mManager.hasComponent(e);
    }

    /**
     * 获取实体的组件实例
     * 
     * @param e 实体
     * @return 组件实例
     */
    Instance getInstance(utils::Entity const e) const noexcept {
        return { mManager.getInstance(e) };
    }

    /**
     * 获取组件数量
     * 
     * @return 组件数量
     */
    size_t getComponentCount() const noexcept {
        return mManager.getComponentCount();
    }

    /**
     * 检查管理器是否为空
     * 
     * @return 如果为空返回 true，否则返回 false
     */
    bool empty() const noexcept {
        return mManager.empty();
    }

    /**
     * 获取实例对应的实体
     * 
     * @param i 组件实例
     * @return 实体
     */
    utils::Entity getEntity(Instance const i) const noexcept {
        return mManager.getEntity(i);
    }

    /**
     * 获取所有实体的数组
     * 
     * @return 实体数组指针
     */
    utils::Entity const* getEntities() const noexcept {
        return mManager.getEntities();
    }

    /**
     * 获取相机指针
     * 
     * @param i 组件实例
     * @return 相机指针
     */
    FCamera* getCamera(Instance const i) noexcept {
        return mManager.elementAt<CAMERA>(i);
    }

    /**
     * 创建相机组件
     * 
     * @param engine 引擎引用
     * @param entity 实体
     * @return 相机指针
     */
    FCamera* create(FEngine& engine, utils::Entity entity);

    /**
     * 销毁相机组件
     * 
     * @param engine 引擎引用
     * @param e 实体
     */
    void destroy(FEngine& engine, utils::Entity e) noexcept;

private:

    /**
     * 组件数据索引枚举
     */
    enum {
        CAMERA,  // 相机指针索引
        OWNS_TRANSFORM_COMPONENT  // 是否拥有变换组件标志索引
    };

    /**
     * 基类类型别名
     * 
     * 单实例组件管理器，存储相机指针和布尔标志。
     */
    using Base = utils::SingleInstanceComponentManager<FCamera*, bool>;

    /**
     * 相机管理器实现结构
     * 
     * 继承自基类，提供组件管理功能。
     */
    struct CameraManagerImpl : public Base {
        using Base::gc;  // 垃圾回收
        using Base::swap;  // 交换
        using Base::hasComponent;  // 检查组件
    } mManager;  // 管理器实例
};

} // namespace filament

#endif // TNT_FILAMENT_COMPONENTS_CAMERAMANAGER_H
