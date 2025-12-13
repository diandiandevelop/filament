/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_VERTEXBUFFER_H
#define TNT_FILAMENT_DETAILS_VERTEXBUFFER_H

#include "downcast.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <filament/VertexBuffer.h>

#include <utils/bitset.h>
#include <utils/compiler.h>

#include <math/vec2.h>

#include <array>
#include <memory>
#include <type_traits>

namespace filament {

class FBufferObject;
class FEngine;

/**
 * 顶点缓冲区实现类
 * 
 * 管理顶点数据的 GPU 缓冲区。
 * 顶点缓冲区可以包含多个缓冲区槽位，每个槽位可以存储不同类型的顶点属性
 * （位置、法线、纹理坐标、颜色等）。
 * 
 * 实现细节：
 * - 支持多个缓冲区槽位（最多 MAX_VERTEX_BUFFER_COUNT 个）
 * - 每个槽位可以绑定 BufferDescriptor 或 BufferObject
 * - 维护顶点属性声明和顶点数量
 * - 支持骨骼索引和权重的更新（用于蒙皮）
 */
class FVertexBuffer : public VertexBuffer {
public:
    using VertexBufferInfoHandle = backend::VertexBufferInfoHandle;  // 顶点缓冲区信息句柄类型
    using VertexBufferHandle = backend::VertexBufferHandle;  // 顶点缓冲区句柄类型
    using BufferObjectHandle = backend::BufferObjectHandle;  // 缓冲区对象句柄类型

    /**
     * 构造函数（从构建器）
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FVertexBuffer(FEngine& engine, const Builder& builder);
    
    /**
     * 构造函数（从另一个顶点缓冲区）
     * 
     * 创建现有顶点缓冲区的副本。
     * 
     * @param engine 引擎引用
     * @param buffer 源顶点缓冲区
     */
    FVertexBuffer(FEngine& engine, FVertexBuffer* buffer);

    /**
     * 终止顶点缓冲区
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 获取硬件句柄
     * 
     * @return 顶点缓冲区句柄
     */
    VertexBufferHandle getHwHandle() const noexcept { return mHandle; }

    /**
     * 获取顶点缓冲区信息句柄
     * 
     * @return 顶点缓冲区信息句柄
     */
    VertexBufferInfoHandle getVertexBufferInfoHandle() const { return mVertexBufferInfoHandle; }

    /**
     * 获取顶点数量
     * 
     * @return 顶点数量
     */
    size_t getVertexCount() const noexcept;

    /**
     * 获取声明的属性位集
     * 
     * @return 声明的属性位集
     */
    AttributeBitset getDeclaredAttributes() const noexcept {
        return mDeclaredAttributes;
    }

    /**
     * 设置指定槽位的缓冲区
     * 
     * 如果 bufferIndex 超出范围，则不执行任何操作。
     * 
     * @param engine 引擎引用
     * @param bufferIndex 缓冲区槽位索引
     * @param buffer 缓冲区描述符（会被移动）
     * @param byteOffset 字节偏移量
     */
    // no-op if bufferIndex out of range
    void setBufferAt(FEngine& engine, uint8_t bufferIndex,
            backend::BufferDescriptor&& buffer, uint32_t byteOffset = 0);

    /**
     * 设置指定槽位的缓冲区对象
     * 
     * @param engine 引擎引用
     * @param bufferIndex 缓冲区槽位索引
     * @param bufferObject 缓冲区对象指针
     */
    void setBufferObjectAt(FEngine& engine, uint8_t bufferIndex,
            FBufferObject const * bufferObject);

    /**
     * 更新骨骼索引和权重
     * 
     * 更新用于蒙皮的骨骼索引和权重数据。
     * 
     * @param engine 引擎引用
     * @param skinJoints 骨骼关节索引数组（唯一指针）
     * @param skinWeights 骨骼权重数组（唯一指针）
     */
    void updateBoneIndicesAndWeights(FEngine& engine, std::unique_ptr<uint16_t[]> skinJoints,
                                        std::unique_ptr<float[]> skinWeights);

private:
    friend class VertexBuffer;  // 允许 VertexBuffer 访问私有成员
    
    /**
     * 顶点缓冲区信息句柄
     * 
     * 包含顶点属性的布局信息（位置、偏移量、步长等）。
     */
    VertexBufferInfoHandle mVertexBufferInfoHandle;  // 顶点缓冲区信息句柄
    
    /**
     * 顶点缓冲区句柄
     * 
     * 驱动层的顶点缓冲区对象句柄。
     */
    VertexBufferHandle mHandle;  // 顶点缓冲区句柄
    
    /**
     * 属性数组
     * 
     * 存储每个顶点属性的定义（位置、法线、纹理坐标等）。
     */
    backend::AttributeArray mAttributes;  // 属性数组
    
    /**
     * 缓冲区对象数组
     * 
     * 存储每个槽位的缓冲区对象句柄。
     * 如果使用 BufferObject，则存储其句柄；否则为空。
     */
    std::array<BufferObjectHandle, backend::MAX_VERTEX_BUFFER_COUNT> mBufferObjects;  // 缓冲区对象数组
    
    /**
     * 声明的属性位集
     * 
     * 标记哪些顶点属性已声明。
     */
    AttributeBitset mDeclaredAttributes;  // 声明的属性位集
    
    /**
     * 顶点数量
     * 
     * 缓冲区中的顶点数量。
     */
    uint32_t mVertexCount = 0;  // 顶点数量
    
    /**
     * 缓冲区数量
     * 
     * 实际使用的缓冲区槽位数量。
     */
    uint8_t mBufferCount = 0;  // 缓冲区数量
    
    /**
     * 缓冲区对象是否启用
     * 
     * 如果为 true，使用 BufferObject；否则使用 BufferDescriptor。
     */
    bool mBufferObjectsEnabled = false;  // 缓冲区对象是否启用
    
    /**
     * 高级蒙皮是否启用
     * 
     * 如果为 true，使用纹理存储骨骼索引和权重；否则使用顶点属性。
     */
    bool mAdvancedSkinningEnabled = false;  // 高级蒙皮是否启用
};

FILAMENT_DOWNCAST(VertexBuffer)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_VERTEXBUFFER_H
