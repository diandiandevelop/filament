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

#include <filamentapp/Grid.h>

#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialEnums.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>

#include <utils/EntityManager.h>

#include <math/vec3.h>
#include <math/mat4.h>

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
#include <utils/Log.h>

using namespace filament;

using namespace filament::math;
using namespace filament;

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 如果提供了材质，创建材质实例
 * 2. 设置材质参数（颜色、深度剔除）
 * 3. 创建可渲染实体
 * 4. 构建渲染对象（线框模式，禁用剔除）
 */
Grid::Grid(Engine& engine, Material const* material, float3 linearColor)
        : mEngine(engine),
          mMaterial(material) {

    // 如果提供了材质，创建材质实例并设置参数
    if (mMaterial) {
        mMaterialInstanceWireFrame = mMaterial->createInstance();
        mMaterialInstanceWireFrame->setDepthCulling(true);  // 启用深度剔除
        mMaterialInstanceWireFrame->setParameter("color", RgbaType::LINEAR,
                LinearColorA{linearColor.r, linearColor.g, linearColor.b, 0.25f});
    }

    // 创建可渲染实体
    utils::EntityManager& em = utils::EntityManager::get();
    mWireFrameRenderable = em.create();

    // 构建渲染对象：线框模式，禁用背面剔除（网格需要从各个角度可见）
    RenderableManager::Builder(1)
            .boundingBox({ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } })
            .material(0, mMaterialInstanceWireFrame)
            .priority(6)
            .culling(false)  // 禁用剔除
            .build(engine, mWireFrameRenderable);
}

Grid::Grid(Grid&& rhs) noexcept
        : mEngine(rhs.mEngine) {
    using std::swap;
    swap(rhs.mVertexBuffer, mVertexBuffer);
    swap(rhs.mIndexBuffer, mIndexBuffer);
    swap(rhs.mMaterial, mMaterial);
    swap(rhs.mMaterialInstanceWireFrame, mMaterialInstanceWireFrame);
    swap(rhs.mWireFrameRenderable, mWireFrameRenderable);
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁顶点缓冲区和索引缓冲区
 * 2. 销毁可渲染实体（线框）
 * 3. 销毁材质实例（必须在可渲染实体之后销毁）
 * 4. 销毁实体本身
 * 
 * 注意：我们不拥有材质对象的所有权，只拥有材质实例的所有权
 */
Grid::~Grid() {
    // 销毁缓冲区
    mEngine.destroy(mVertexBuffer);
    mEngine.destroy(mIndexBuffer);

    // We don't own the material, only instances
    // 我们不拥有材质对象，只拥有材质实例
    // 销毁可渲染实体
    mEngine.destroy(mWireFrameRenderable);

    // material instances must be destroyed after the renderables
    // 材质实例必须在可渲染实体之后销毁
    mEngine.destroy(mMaterialInstanceWireFrame);

    // 销毁实体本身
    utils::EntityManager& em = utils::EntityManager::get();
    em.destroy(mWireFrameRenderable);
}

/**
 * 将网格映射到相机视锥体实现
 * 
 * 执行步骤：
 * 1. 获取相机的模型矩阵（视图矩阵的逆）
 * 2. 获取相机的投影矩阵的逆
 * 3. 计算视锥体的变换矩阵（模型矩阵 * 投影矩阵的逆）
 * 4. 调用通用映射函数
 */
void Grid::mapFrustum(Engine& engine, Camera const* camera) {
    // the Camera far plane is at infinity, but we want it closer for display
    // 相机远平面在无穷远处，但我们希望它更近以便显示
    const mat4 vm(camera->getModelMatrix());
    mat4 const p(vm * inverse(camera->getProjectionMatrix()));
    mapFrustum(engine, p);
}

/**
 * 将网格映射到指定变换矩阵实现
 * 
 * 执行步骤：
 * 1. 获取变换管理器
 * 2. 获取线框渲染对象的变换实例
 * 3. 设置变换矩阵
 */
void Grid::mapFrustum(Engine& engine, mat4 const& transform) {
    // the Camera far plane is at infinity, but we want it closer for display
    // 相机远平面在无穷远处，但我们希望它更近以便显示
    mat4f const p(transform);
    auto& tcm = engine.getTransformManager();
    tcm.setTransform(tcm.getInstance(mWireFrameRenderable), p);
}

/**
 * 将网格映射到包围盒实现
 * 
 * 执行步骤：
 * 1. 构建变换矩阵：先缩放（halfExtent），再平移（center）
 * 2. 调用通用映射函数
 */
void Grid::mapAabb(Engine& engine, Box const& box) {
    mat4 const p = mat4::translation(box.center) * mat4::scaling(box.halfExtent);
    mapFrustum(engine, p);
}

/**
 * 更新网格尺寸实现（使用默认坐标生成器）
 * 
 * 执行步骤：
 * 1. 调用带自定义生成器的update方法
 * 2. 使用默认生成器：将索引映射到[-1, 1]范围
 * 
 * @param width X轴网格数量
 * @param height Y轴网格数量
 * @param depth Z轴网格数量
 */
void Grid::update(uint32_t width, uint32_t height, uint32_t depth) {
    // [-1, 1] range (default behavior)
    // 默认行为：坐标范围[-1, 1]
    update(width, height, depth,
            [](int const index) { return float(index) * 2.0f - 1.0f; },  // X轴：线性映射到[-1, 1]
            [](int const index) { return float(index) * 2.0f - 1.0f; },  // Y轴：线性映射到[-1, 1]
            [](int const index) { return float(index) * 2.0f - 1.0f; }); // Z轴：线性映射到[-1, 1]
}

/**
 * 更新网格实现（使用自定义坐标生成器）
 * 
 * 执行步骤：
 * 1. 销毁现有的顶点和索引缓冲区
 * 2. 使用生成器函数生成顶点坐标
 * 3. 生成索引数据（连接顶点形成网格线）
 * 4. 创建新的顶点和索引缓冲区
 * 5. 更新渲染对象的几何数据
 * 
 * @param width X轴网格数量
 * @param height Y轴网格数量
 * @param depth Z轴网格数量
 * @param genWidth X轴坐标生成器
 * @param genHeight Y轴坐标生成器
 * @param genDepth Z轴坐标生成器
 */
void Grid::update(uint32_t const width, uint32_t const height, uint32_t const depth,
            Generator const& genWidth, Generator const& genHeight, Generator const& genDepth) {

    // 首先销毁现有的缓冲区
    mEngine.destroy(mVertexBuffer);
    mEngine.destroy(mIndexBuffer);

    std::vector<float3> vertices;
    std::vector<uint32_t> indices;
    size_t const verticeCount = (depth + 1) * (height + 1) * (width + 1);
    vertices.reserve(verticeCount);
    indices.reserve(verticeCount * 2);

    // Generate vertices
    // The vertices are generated to form a grid in the [-1, 1] range on all axes.
    // 生成顶点：使用生成器函数计算每个顶点的坐标
    for (int k = 0; k <= depth; ++k) {
        auto const z = genDepth(k);
        for (int j = 0; j <= height; ++j) {
            auto const y = genHeight(j);
            for (int i = 0; i <= width; ++i) {
                auto const x = genWidth(i);
                vertices.push_back({ x, y, z });
            }
        }
    }

    // Generate indices for the lines
    // The indices connect the vertices to form the grid lines.
    // 生成索引：连接顶点形成网格线
    // 辅助函数：根据网格坐标计算顶点索引
    auto getVertexIndex = [=](uint32_t const i, uint32_t const j, uint32_t const k) {
        return k * (width + 1) * (height + 1) + j * (width + 1) + i;
    };

    // Lines along X axis
    // 沿X轴的线：连接每个YZ平面的左右边界
    for (uint32_t k = 0; k <= depth; ++k) {
        for (uint32_t j = 0; j <= height; ++j) {
            indices.push_back(getVertexIndex(0, j, k));
            indices.push_back(getVertexIndex(width, j, k));
        }
    }

    // Lines along Y axis
    // 沿Y轴的线：连接每个XZ平面的上下边界
    for (uint32_t k = 0; k <= depth; ++k) {
        for (uint32_t i = 0; i <= width; ++i) {
            indices.push_back(getVertexIndex(i, 0, k));
            indices.push_back(getVertexIndex(i, height, k));
        }
    }

    // Lines along Z axis
    // 沿Z轴的线：连接每个XY平面的前后边界
    for (uint32_t j = 0; j <= height; ++j) {
        for (uint32_t i = 0; i <= width; ++i) {
            indices.push_back(getVertexIndex(i, j, 0));
            indices.push_back(getVertexIndex(i, j, depth));
        }
    }

    const size_t vertexCount = vertices.size();
    mVertexBuffer = VertexBuffer::Builder()
            .vertexCount(vertexCount)
            .bufferCount(1)
            .attribute(POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
            .build(mEngine);

    auto vertexData = vertices.data();
    mVertexBuffer->setBufferAt(mEngine, 0,
            VertexBuffer::BufferDescriptor::make(
                    vertexData, vertexCount * sizeof(vertices[0]),
                    [v = std::move(vertices)](void*, size_t) {}));

    const size_t indexCount = indices.size();
    mIndexBuffer = IndexBuffer::Builder()
            .indexCount(indexCount)
            .build(mEngine);

    auto indexData = indices.data();
    mIndexBuffer->setBuffer(mEngine,
            IndexBuffer::BufferDescriptor::make(
                indexData, indexCount * sizeof(uint32_t),
                [i = std::move(indices)](void*, size_t) {}));

    auto& rcm = mEngine.getRenderableManager();
    auto instance = rcm.getInstance(mWireFrameRenderable);
    rcm.setGeometryAt(instance, 0, RenderableManager::PrimitiveType::LINES,
            mVertexBuffer, mIndexBuffer, 0, indexCount);
}
