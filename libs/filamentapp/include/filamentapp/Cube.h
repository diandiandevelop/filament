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

#ifndef TNT_FILAMENT_SAMPLE_CUBE_H
#define TNT_FILAMENT_SAMPLE_CUBE_H

#include <vector>

#include <filament/Engine.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <utils/Entity.h>

/**
 * Cube - 立方体几何体类
 * 
 * 用于创建和渲染立方体几何体，支持实体和线框两种渲染模式。
 * 主要用于可视化相机视锥体、方向光阴影视锥体、包围盒等。
 */
class Cube {
public:

    /**
     * 构造函数
     * @param engine Filament引擎引用
     * @param material 材质指针
     * @param linearColor 线性空间颜色值
     * @param culling 是否启用背面剔除，默认true
     */
    Cube(filament::Engine& engine, filament::Material const* material, filament::math::float3 linearColor, bool culling = true);

    /** 禁用拷贝构造 */
    Cube(Cube const&) = delete;
    /** 禁用拷贝赋值 */
    Cube& operator=(Cube const&) = delete;

    /**
     * 移动构造函数
     * @param rhs 源对象
     */
    Cube(Cube&& rhs) noexcept;

    /**
     * 获取实体渲染对象
     * @return 实体渲染实体
     */
    utils::Entity getSolidRenderable() {
        return mSolidRenderable;
    }

    /**
     * 获取线框渲染对象
     * @return 线框渲染实体
     */
    utils::Entity getWireFrameRenderable() {
        return mWireFrameRenderable;
    }

    /** 析构函数：清理资源 */
    ~Cube();

    /**
     * 将立方体映射到相机视锥体
     * @param engine Filament引擎引用
     * @param camera 相机指针
     */
    void mapFrustum(filament::Engine& engine, filament::Camera const* camera);
    /**
     * 将立方体映射到指定的变换矩阵
     * @param engine Filament引擎引用
     * @param transform 4x4变换矩阵
     */
    void mapFrustum(filament::Engine& engine, filament::math::mat4 const& transform);
    /**
     * 将立方体映射到包围盒（AABB）
     * @param engine Filament引擎引用
     * @param box 轴对齐包围盒
     */
    void mapAabb(filament::Engine& engine, filament::Box const& box);

private:
    static constexpr size_t WIREFRAME_OFFSET = 3*2*6; // 线框索引在索引缓冲区中的偏移量
    static const uint32_t mIndices[];                // 索引数据（实体+线框）
    static const filament::math::float3 mVertices[];  // 顶点数据（8个顶点）

    filament::Engine& mEngine;                        // Filament引擎引用
    filament::VertexBuffer* mVertexBuffer = nullptr;  // 顶点缓冲区
    filament::IndexBuffer* mIndexBuffer = nullptr;    // 索引缓冲区
    filament::Material const* mMaterial = nullptr;    // 材质指针
    filament::MaterialInstance* mMaterialInstanceSolid = nullptr;     // 实体材质实例
    filament::MaterialInstance* mMaterialInstanceWireFrame = nullptr; // 线框材质实例
    utils::Entity mSolidRenderable{};                 // 实体渲染实体
    utils::Entity mWireFrameRenderable{};             // 线框渲染实体
};


#endif // TNT_FILAMENT_SAMPLE_CUBE_H
