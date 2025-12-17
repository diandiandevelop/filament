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

#include <filamentapp/Cube.h>

#include <utils/EntityManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>

using namespace filament::math;
using namespace filament;

const uint32_t Cube::mIndices[] = {
        // solid
        2,0,1, 2,1,3,  // far
        6,4,5, 6,5,7,  // near
        2,0,4, 2,4,6,  // left
        3,1,5, 3,5,7,  // right
        0,4,5, 0,5,1,  // bottom
        2,6,7, 2,7,3,  // top

        // wire-frame
        0,1, 1,3, 3,2, 2,0,     // far
        4,5, 5,7, 7,6, 6,4,     // near
        0,4, 1,5, 3,7, 2,6,
};

const filament::math::float3 Cube::mVertices[] = {
        { -1, -1,  1},  // 0. left bottom far
        {  1, -1,  1},  // 1. right bottom far
        { -1,  1,  1},  // 2. left top far
        {  1,  1,  1},  // 3. right top far
        { -1, -1, -1},  // 4. left bottom near
        {  1, -1, -1},  // 5. right bottom near
        { -1,  1, -1},  // 6. left top near
        {  1,  1, -1}}; // 7. right top near


/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 创建顶点缓冲区（8个顶点）
 * 2. 创建索引缓冲区（实体三角形+线框线段）
 * 3. 创建材质实例（实体和线框各一个，透明度不同）
 * 4. 设置顶点和索引数据
 * 5. 创建实体渲染对象（使用三角形）
 * 6. 创建线框渲染对象（使用线段）
 */
Cube::Cube(Engine& engine, filament::Material const* material, float3 linearColor, bool culling) :
        mEngine(engine),
        mMaterial(material) {

    // 创建顶点缓冲区：8个顶点，只有位置属性
    mVertexBuffer = VertexBuffer::Builder()
            .vertexCount(8)
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3)
            .build(engine);

    // 创建索引缓冲区：12个三角形（实体）+ 24条线段（线框）
    mIndexBuffer = IndexBuffer::Builder()
            .indexCount(12*2 + 3*2*6)
            .build(engine);

    // 如果提供了材质，创建两个材质实例（实体和线框）
    if (mMaterial) {
        mMaterialInstanceSolid = mMaterial->createInstance();
        mMaterialInstanceWireFrame = mMaterial->createInstance();
        // 实体材质：低透明度（0.05）
        mMaterialInstanceSolid->setParameter("color", RgbaType::LINEAR,
                LinearColorA{linearColor.r, linearColor.g, linearColor.b, 0.05f});
        // 线框材质：较高透明度（0.25）
        mMaterialInstanceWireFrame->setParameter("color", RgbaType::LINEAR,
                LinearColorA{linearColor.r, linearColor.g, linearColor.b, 0.25f});
    }

    // 设置顶点数据
    mVertexBuffer->setBufferAt(engine, 0,
            VertexBuffer::BufferDescriptor(
                    mVertices, mVertexBuffer->getVertexCount() * sizeof(mVertices[0])));

    // 设置索引数据
    mIndexBuffer->setBuffer(engine,
            IndexBuffer::BufferDescriptor(
                    mIndices, mIndexBuffer->getIndexCount() * sizeof(uint32_t)));

    // 创建实体渲染对象：使用三角形图元，优先级7
    utils::EntityManager& em = utils::EntityManager::get();
    mSolidRenderable = em.create();
    RenderableManager::Builder(1)
            .boundingBox({{ 0, 0, 0 },
                          { 1, 1, 1 }})
            .material(0, mMaterialInstanceSolid)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, mVertexBuffer, mIndexBuffer, 0, 3*2*6)
            .priority(7)
            .culling(culling)
            .build(engine, mSolidRenderable);

    // 创建线框渲染对象：使用线段图元，优先级6（低于实体）
    mWireFrameRenderable = em.create();
    RenderableManager::Builder(1)
            .boundingBox({{ 0, 0, 0 },
                          { 1, 1, 1 }})
            .material(0, mMaterialInstanceWireFrame)
            .geometry(0, RenderableManager::PrimitiveType::LINES, mVertexBuffer, mIndexBuffer, WIREFRAME_OFFSET, 24)
            .priority(6)
            .culling(culling)
            .build(engine, mWireFrameRenderable);
}

/**
 * 移动构造函数实现
 * 
 * 执行步骤：
 * 1. 保存引擎引用
 * 2. 使用swap交换所有资源（顶点缓冲区、索引缓冲区、材质、材质实例、渲染实体）
 * 3. 源对象的资源被移动到新对象，源对象处于有效但未指定状态
 * 
 * @param rhs 源对象（右值引用）
 */
Cube::Cube(Cube&& rhs) noexcept
        : mEngine(rhs.mEngine) {
    using std::swap;
    swap(rhs.mVertexBuffer, mVertexBuffer);
    swap(rhs.mIndexBuffer, mIndexBuffer);
    swap(rhs.mMaterial, mMaterial);
    swap(rhs.mMaterialInstanceSolid, mMaterialInstanceSolid);
    swap(rhs.mMaterialInstanceWireFrame, mMaterialInstanceWireFrame);
    swap(rhs.mSolidRenderable, mSolidRenderable);
    swap(rhs.mWireFrameRenderable, mWireFrameRenderable);
}

/**
 * 将立方体映射到相机视锥体实现
 * 
 * 执行步骤：
 * 1. 获取相机的模型矩阵（视图矩阵的逆）
 * 2. 获取相机的裁剪投影矩阵的逆
 * 3. 计算视锥体的变换矩阵（模型矩阵 * 投影矩阵的逆）
 * 4. 调用通用映射函数
 */
void Cube::mapFrustum(filament::Engine& engine, Camera const* camera) {
    // the Camera far plane is at infinity, but we want it closer for display
    const mat4 vm(camera->getModelMatrix());
    mat4 p(vm * inverse(camera->getCullingProjectionMatrix()));
    mapFrustum(engine, p);
}

/**
 * 将立方体映射到指定变换矩阵实现
 * 
 * 执行步骤：
 * 1. 获取变换管理器
 * 2. 获取实体和线框渲染对象的变换实例
 * 3. 设置变换矩阵
 */
void Cube::mapFrustum(filament::Engine& engine, filament::math::mat4 const& transform) {
    // the Camera far plane is at infinity, but we want it closer for display
    mat4f p(transform);
    auto& tcm = engine.getTransformManager();
    tcm.setTransform(tcm.getInstance(mSolidRenderable), p);
    tcm.setTransform(tcm.getInstance(mWireFrameRenderable), p);
}


/**
 * 将立方体映射到包围盒实现
 * 
 * 执行步骤：
 * 1. 构建变换矩阵：先缩放（halfExtent），再平移（center）
 * 2. 调用通用映射函数
 */
void Cube::mapAabb(filament::Engine& engine, filament::Box const& box) {
    mat4 p = mat4::translation(box.center) * mat4::scaling(box.halfExtent);
    mapFrustum(engine, p);
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁顶点缓冲区和索引缓冲区
 * 2. 销毁可渲染实体（实体和线框）
 * 3. 销毁材质实例（必须在可渲染实体之后销毁）
 * 4. 销毁实体本身
 * 
 * 注意：我们不拥有材质对象的所有权，只拥有材质实例的所有权
 */
Cube::~Cube() {
    // 销毁缓冲区
    mEngine.destroy(mVertexBuffer);
    mEngine.destroy(mIndexBuffer);

    // We don't own the material, only instances
    // 我们不拥有材质对象，只拥有材质实例
    // 销毁可渲染实体
    mEngine.destroy(mSolidRenderable);
    mEngine.destroy(mWireFrameRenderable);

    // material instances must be destroyed after the renderables
    // 材质实例必须在可渲染实体之后销毁
    mEngine.destroy(mMaterialInstanceSolid);
    mEngine.destroy(mMaterialInstanceWireFrame);

    // 销毁实体本身
    utils::EntityManager& em = utils::EntityManager::get();
    em.destroy(mSolidRenderable);
    em.destroy(mWireFrameRenderable);
}
