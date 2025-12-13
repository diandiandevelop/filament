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

#include "details/SkinningBuffer.h"

#include "details/Engine.h"

namespace filament {

using namespace backend;
using namespace math;

/**
 * 设置骨骼变换（Bone 结构体版本）
 * 
 * @param engine 引擎引用
 * @param transforms 骨骼变换数组指针（Bone 结构体）
 * @param count 骨骼数量
 * @param offset 起始偏移量
 */
void SkinningBuffer::setBones(Engine& engine,
        RenderableManager::Bone const* transforms, size_t const count, size_t const offset) {
    downcast(this)->setBones(downcast(engine), transforms, count, offset);
}

/**
 * 设置骨骼变换（mat4f 矩阵版本）
 * 
 * @param engine 引擎引用
 * @param transforms 骨骼变换矩阵数组指针（4x4 矩阵）
 * @param count 骨骼数量
 * @param offset 起始偏移量
 */
void SkinningBuffer::setBones(Engine& engine,
        mat4f const* transforms, size_t const count, size_t const offset) {
    downcast(this)->setBones(downcast(engine), transforms, count, offset);
}

/**
 * 获取骨骼数量
 * 
 * @return 骨骼数量
 */
size_t SkinningBuffer::getBoneCount() const noexcept {
    return downcast(this)->getBoneCount();
}

} // namespace filament

