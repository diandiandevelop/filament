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

#include "details/MorphTargetBuffer.h"

#include "details/Engine.h"

namespace filament {

/**
 * 设置指定变形目标的位置（float3 版本）
 * 
 * @param engine 引擎引用
 * @param targetIndex 变形目标索引
 * @param positions 位置数组指针（float3）
 * @param count 顶点数量
 * @param offset 起始偏移量
 */
void MorphTargetBuffer::setPositionsAt(Engine& engine, size_t const targetIndex,
        math::float3 const* positions, size_t const count, size_t const offset) {
    downcast(this)->setPositionsAt(downcast(engine), targetIndex, positions, count, offset);
}

/**
 * 设置指定变形目标的位置（float4 版本）
 * 
 * @param engine 引擎引用
 * @param targetIndex 变形目标索引
 * @param positions 位置数组指针（float4）
 * @param count 顶点数量
 * @param offset 起始偏移量
 */
void MorphTargetBuffer::setPositionsAt(Engine& engine, size_t const targetIndex,
        math::float4 const* positions, size_t const count, size_t const offset) {
    downcast(this)->setPositionsAt(downcast(engine), targetIndex, positions, count, offset);
}

/**
 * 设置指定变形目标的切线
 * 
 * @param engine 引擎引用
 * @param targetIndex 变形目标索引
 * @param tangents 切线数组指针（short4，压缩格式）
 * @param count 顶点数量
 * @param offset 起始偏移量
 */
void MorphTargetBuffer::setTangentsAt(Engine& engine, size_t const targetIndex,
        math::short4 const* tangents, size_t const count, size_t const offset) {
    downcast(this)->setTangentsAt(downcast(engine), targetIndex, tangents, count, offset);
}

/**
 * 获取顶点数量
 * 
 * @return 顶点数量
 */
size_t MorphTargetBuffer::getVertexCount() const noexcept {
    return downcast(this)->getVertexCount();
}

/**
 * 获取变形目标数量
 * 
 * @return 变形目标数量
 */
size_t MorphTargetBuffer::getCount() const noexcept {
    return downcast(this)->getCount();
}

} // namespace filament

