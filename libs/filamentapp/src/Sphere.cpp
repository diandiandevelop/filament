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


#include <filamentapp/Sphere.h>

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <utils/EntityManager.h>
#include <math/norm.h>

#include <geometry/SurfaceOrientation.h>

#include <filamentapp/IcoSphere.h>

using namespace filament;
using namespace filament::math;
using namespace utils;


/**
 * Geometry - 几何体数据结构
 * 存储球体的顶点、索引、切向量等几何数据
 */
struct Geometry {
    IcoSphere sphere = IcoSphere{ 2 };                    // 二十面体球（细分级别2）
    std::vector<filament::math::short4> tangents;         // 切向量数据（打包为short4）
    filament::VertexBuffer* vertexBuffer = nullptr;        // 顶点缓冲区
    filament::IndexBuffer* indexBuffer = nullptr;          // 索引缓冲区
};

// note: this will be leaked since we don't have a good time to free it.
// this should be a cache indexed on the sphere's subdivisions
/** 全局几何体缓存（避免重复创建相同细分级别的球体） */
static Geometry* gGeometry = nullptr;

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 检查全局几何体缓存，如果不存在则创建
 * 2. 从IcoSphere获取顶点和索引数据
 * 3. 计算切向量（使用SurfaceOrientation）
 * 4. 创建顶点缓冲区和索引缓冲区
 * 5. 创建材质实例
 * 6. 创建可渲染实体
 */
Sphere::Sphere(Engine& engine, Material const* material, bool culling)
        : mEngine(engine) {
    Geometry* geometry = gGeometry;

    // 确保三角形结构体是紧密打包的
    static_assert(sizeof(IcoSphere::Triangle) == sizeof(IcoSphere::Index) * 3,
            "indices are not packed");

    // 如果全局几何体不存在，创建新的几何体
    if (geometry == nullptr) {
        geometry = gGeometry = new Geometry;

        // 从IcoSphere获取索引和顶点数据
        auto const& indices = geometry->sphere.getIndices();
        auto const& vertices = geometry->sphere.getVertices();
        uint32_t indexCount = (uint32_t)(indices.size() * 3);

        // 计算切向量：使用SurfaceOrientation从法线生成切向量四元数
        geometry->tangents.resize(vertices.size());
        auto* quats = geometry::SurfaceOrientation::Builder()
            .vertexCount(vertices.size())
            .normals(vertices.data(), sizeof(float3))
            .build();
        quats->getQuats((short4*) geometry->tangents.data(), vertices.size(),
                sizeof(filament::math::short4));
        delete quats;

        // todo produce correct u,v

        // 创建顶点缓冲区：包含位置和切向量两个属性
        geometry->vertexBuffer = VertexBuffer::Builder()
                .vertexCount((uint32_t)vertices.size())
                .bufferCount(2)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
                .attribute(VertexAttribute::TANGENTS, 1, VertexBuffer::AttributeType::SHORT4)
                .normalized(VertexAttribute::TANGENTS)
                .build(engine);

        // 设置位置数据
        geometry->vertexBuffer->setBufferAt(engine, 0,
                VertexBuffer::BufferDescriptor(vertices.data(), vertices.size() * sizeof(float3)));

        // 设置切向量数据
        geometry->vertexBuffer->setBufferAt(engine, 1,
                VertexBuffer::BufferDescriptor(geometry->tangents.data(), geometry->tangents.size() * sizeof(filament::math::short4)));


        // 创建索引缓冲区：使用USHORT类型
        geometry->indexBuffer = IndexBuffer::Builder()
                .bufferType(IndexBuffer::IndexType::USHORT)
                .indexCount(indexCount)
                .build(engine);

        // 设置索引数据
        geometry->indexBuffer->setBuffer(engine,
                IndexBuffer::BufferDescriptor(indices.data(), indexCount * sizeof(IcoSphere::Index)));
    }

    // 如果提供了材质，创建材质实例
    if (material) {
        mMaterialInstance = material->createInstance();
    }

    // 创建可渲染实体
    utils::EntityManager& em = utils::EntityManager::get();
    mRenderable = em.create();
    RenderableManager::Builder(1)
            .boundingBox({{0}, {1}})  // 设置包围盒（单位球）
            .material(0, mMaterialInstance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, geometry->vertexBuffer, geometry->indexBuffer)
            .culling(culling)
            .build(engine, mRenderable);
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁材质实例
 * 2. 销毁可渲染实体
 * 3. 销毁实体本身
 */
Sphere::~Sphere() {
    mEngine.destroy(mMaterialInstance);
    mEngine.destroy(mRenderable);
    utils::EntityManager& em = utils::EntityManager::get();
    em.destroy(mRenderable);
}

/**
 * 设置球体位置实现
 * 
 * 执行步骤：
 * 1. 获取变换管理器
 * 2. 获取实体的变换实例
 * 3. 获取当前变换矩阵
 * 4. 修改变换矩阵的平移部分
 * 5. 设置新的变换矩阵
 */
Sphere& Sphere::setPosition(filament::math::float3 const& position) noexcept {
    auto& tcm = mEngine.getTransformManager();
    auto ci = tcm.getInstance(mRenderable);
    mat4f model = tcm.getTransform(ci);
    model[3].xyz = position;  // 设置平移向量
    tcm.setTransform(ci, model);
    return *this;
}

/**
 * 设置球体半径实现
 * 
 * 执行步骤：
 * 1. 获取变换管理器
 * 2. 获取实体的变换实例
 * 3. 获取当前变换矩阵
 * 4. 修改变换矩阵的缩放部分（对角线元素）
 * 5. 设置新的变换矩阵
 */
Sphere& Sphere::setRadius(float radius) noexcept {
    auto& tcm = mEngine.getTransformManager();
    auto ci = tcm.getInstance(mRenderable);
    mat4f model = tcm.getTransform(ci);
    model[0].x = radius;  // X轴缩放
    model[1].y = radius;  // Y轴缩放
    model[2].z = radius;  // Z轴缩放
    tcm.setTransform(ci, model);
    return *this;
}
