/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SAMPLE_SPHERE_H
#define TNT_FILAMENT_SAMPLE_SPHERE_H

#include <utils/Entity.h>
#include <math/vec3.h>

namespace filament {
class Engine;
class IndexBuffer;
class Material;
class MaterialInstance;
class VertexBuffer;
}

/**
 * Sphere - 球体几何体类
 * 
 * 用于创建和渲染球体几何体，基于IcoSphere（二十面体球）生成。
 * 支持设置位置和半径，提供可渲染实体供场景使用。
 */
class Sphere {
public:
    /**
     * 构造函数
     * @param engine Filament引擎引用
     * @param material 材质指针，用于渲染球体
     * @param culling 是否启用背面剔除，默认true
     */
    Sphere( filament::Engine& engine,
            filament::Material const* material,
            bool culling = true);
    /** 析构函数：清理资源 */
    ~Sphere();

    /** 禁用拷贝构造 */
    Sphere(Sphere const&) = delete;
    /** 禁用拷贝赋值 */
    Sphere& operator = (Sphere const&) = delete;

    /**
     * 移动构造函数
     * @param rhs 源对象
     */
    Sphere(Sphere&& rhs) noexcept
            : mEngine(rhs.mEngine),
              mMaterialInstance(rhs.mMaterialInstance),
              mRenderable(rhs.mRenderable) {
        rhs.mMaterialInstance = {};
        rhs.mRenderable = {};
    }

    /**
     * 获取实体渲染对象
     * @return 可渲染实体
     */
    utils::Entity getSolidRenderable() const {
        return mRenderable;
    }

    /**
     * 获取材质实例
     * @return 材质实例指针
     */
    filament::MaterialInstance* getMaterialInstance() {
        return mMaterialInstance;
    }

    /**
     * 设置球体位置
     * @param position 位置坐标（世界空间）
     * @return 自身引用，支持链式调用
     */
    Sphere& setPosition(filament::math::float3 const& position) noexcept;
    /**
     * 设置球体半径
     * @param radius 半径值
     * @return 自身引用，支持链式调用
     */
    Sphere& setRadius(float radius) noexcept;

private:
    filament::Engine& mEngine;                    // Filament引擎引用
    filament::MaterialInstance* mMaterialInstance = nullptr; // 材质实例
    utils::Entity mRenderable;                    // 可渲染实体

};

#endif //TNT_FILAMENT_SAMPLE_SPHERE_H
