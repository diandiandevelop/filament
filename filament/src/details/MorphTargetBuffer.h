/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_MORPHTARGETBUFFER_H
#define TNT_FILAMENT_DETAILS_MORPHTARGETBUFFER_H

#include "downcast.h"

#include <filament/MorphTargetBuffer.h>

#include <backend/DriverEnums.h>
#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

#include <math/vec3.h>
#include <math/vec4.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FEngine;

/**
 * 变形目标缓冲区实现类
 * 
 * 管理变形目标（Morph Target）数据，用于顶点动画。
 * 变形目标允许在运行时通过混合多个预定义的顶点位置来实现动画效果。
 * 
 * 实现细节：
 * - 使用 2D 数组纹理存储位置和切线数据
 * - 位置数据使用 RGBA32F 格式（float4）
 * - 切线数据使用 RGBA16I 格式（压缩的 short4）
 * - 纹理宽度限制为 2048（ES3.0 限制）
 */
class FMorphTargetBuffer : public MorphTargetBuffer {
public:
    /**
     * 空变形目标构建器
     * 
     * 用于创建最小尺寸的变形目标缓冲区（1 个顶点，1 个目标）。
     */
    class EmptyMorphTargetBuilder : public Builder {
    public:
        /**
         * 构造函数
         * 
         * 初始化构建器，设置默认值（1 个顶点，1 个目标）。
         */
        EmptyMorphTargetBuilder();
    };

    /**
     * 构造函数
     * 
     * 根据构建器创建变形目标缓冲区。
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FMorphTargetBuffer(FEngine& engine, const Builder& builder);

    /**
     * 终止变形目标缓冲区
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 设置指定变形目标的位置（float3 版本）
     * 
     * @param engine 引擎引用
     * @param targetIndex 变形目标索引
     * @param positions 位置数组指针（float3）
     * @param count 顶点数量
     * @param offset 起始偏移量
     */
    void setPositionsAt(FEngine& engine, size_t targetIndex,
            math::float3 const* positions, size_t count, size_t offset);

    /**
     * 设置指定变形目标的位置（float4 版本）
     * 
     * @param engine 引擎引用
     * @param targetIndex 变形目标索引
     * @param positions 位置数组指针（float4）
     * @param count 顶点数量
     * @param offset 起始偏移量
     */
    void setPositionsAt(FEngine& engine, size_t targetIndex,
            math::float4 const* positions, size_t count, size_t offset);

    /**
     * 设置指定变形目标的切线
     * 
     * @param engine 引擎引用
     * @param targetIndex 变形目标索引
     * @param tangents 切线数组指针（short4，压缩格式）
     * @param count 顶点数量
     * @param offset 起始偏移量
     */
    void setTangentsAt(FEngine& engine, size_t targetIndex,
            math::short4 const* tangents, size_t count, size_t offset);

    /**
     * 获取顶点数量
     * 
     * @return 顶点数量
     */
    inline size_t getVertexCount() const noexcept { return mVertexCount; }
    
    /**
     * 获取变形目标数量
     * 
     * @return 变形目标数量
     */
    inline size_t getCount() const noexcept { return mCount; }

    /**
     * 获取位置纹理句柄
     * 
     * @return 位置纹理句柄
     */
    backend::TextureHandle getPositionsHandle() const noexcept {
        return mPbHandle;
    }

    /**
     * 获取切线纹理句柄
     * 
     * @return 切线纹理句柄
     */
    backend::TextureHandle getTangentsHandle() const noexcept {
        return mTbHandle;
    }

private:
    /**
     * 更新指定纹理的数据
     * 
     * 将数据更新到纹理的指定位置，处理跨行的情况。
     * 
     * @param driver 驱动 API 引用
     * @param handle 纹理句柄
     * @param format 像素数据格式
     * @param type 像素数据类型
     * @param out 数据指针
     * @param elementSize 元素大小（字节）
     * @param targetIndex 目标索引（数组层）
     * @param count 元素数量
     * @param offset 起始偏移量
     */
    void updateDataAt(backend::DriverApi& driver, backend::Handle <backend::HwTexture> handle,
            backend::PixelDataFormat format, backend::PixelDataType type, const char* out,
            size_t elementSize, size_t targetIndex, size_t count, size_t offset);

    backend::TextureHandle mPbHandle;  // 位置缓冲区纹理句柄（Position Buffer）
    backend::TextureHandle mTbHandle;  // 切线缓冲区纹理句柄（Tangent Buffer）
    uint32_t mVertexCount;  // 顶点数量
    uint32_t mCount;  // 变形目标数量
};

FILAMENT_DOWNCAST(MorphTargetBuffer)

} // namespace filament

#endif //TNT_FILAMENT_DETAILS_MORPHTARGETBUFFER_H
