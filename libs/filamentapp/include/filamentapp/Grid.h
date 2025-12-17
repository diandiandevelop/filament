/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SAMPLE_GRID_H
#define TNT_FILAMENT_SAMPLE_GRID_H

#include <filament/Engine.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>

#include <math/mat4.h>
#include <math/vec3.h>

#include <utils/Entity.h>

#include <functional>

/**
 * Grid - 网格几何体类
 * 
 * 用于创建和渲染3D网格，主要用于可视化Froxel网格（光照计算网格）。
 * 支持动态更新网格尺寸和坐标生成函数。
 */
class Grid {
public:

    /**
     * 构造函数
     * @param engine Filament引擎引用
     * @param material 材质指针
     * @param linearColor 线性空间颜色值
     */
    Grid(filament::Engine& engine, filament::Material const* material,
        filament::math::float3 linearColor);

    /** 禁用拷贝构造 */
    Grid(Grid const&) = delete;
    /** 禁用拷贝赋值 */
    Grid& operator=(Grid const&) = delete;

    /**
     * 移动构造函数
     * @param rhs 源对象
     */
    Grid(Grid&& rhs) noexcept;

    /**
     * 获取线框渲染对象
     * @return 线框渲染实体
     */
    utils::Entity getWireFrameRenderable() const {
        return mWireFrameRenderable;
    }

    /** 析构函数：清理资源 */
    ~Grid();

    /** 坐标生成器函数类型：根据索引生成坐标值 */
    using Generator = std::function<float(int index)>;

    /**
     * 更新网格尺寸（使用默认坐标生成器，范围[-1, 1]）
     * @param width X轴网格数量
     * @param height Y轴网格数量
     * @param depth Z轴网格数量
     */
    void update(uint32_t width, uint32_t height, uint32_t depth);

    /**
     * 更新网格尺寸（使用自定义坐标生成器）
     * @param width X轴网格数量
     * @param height Y轴网格数量
     * @param depth Z轴网格数量
     * @param genWidth X轴坐标生成器
     * @param genHeight Y轴坐标生成器
     * @param genDepth Z轴坐标生成器
     */
    void update(uint32_t width, uint32_t height, uint32_t depth,
            Generator const& genWidth, Generator const& genHeight, Generator const& genDepth);

    /**
     * 将网格映射到相机视锥体
     * @param engine Filament引擎引用
     * @param camera 相机指针
     */
    void mapFrustum(filament::Engine& engine, filament::Camera const* camera);
    /**
     * 将网格映射到指定的变换矩阵
     * @param engine Filament引擎引用
     * @param transform 4x4变换矩阵
     */
    void mapFrustum(filament::Engine& engine, filament::math::mat4 const& transform);
    /**
     * 将网格映射到包围盒（AABB）
     * @param engine Filament引擎引用
     * @param box 轴对齐包围盒
     */
    void mapAabb(filament::Engine& engine, filament::Box const& box);

private:
    filament::Engine& mEngine;                        // Filament引擎引用
    filament::VertexBuffer* mVertexBuffer = nullptr;  // 顶点缓冲区
    filament::IndexBuffer* mIndexBuffer = nullptr;    // 索引缓冲区
    filament::Material const* mMaterial = nullptr;    // 材质指针
    filament::MaterialInstance* mMaterialInstanceWireFrame = nullptr; // 线框材质实例
    utils::Entity mWireFrameRenderable{};             // 线框渲染实体
};


#endif // TNT_FILAMENT_SAMPLE_GRID_H
