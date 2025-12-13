/*
* Copyright (C) 2023 The Android Open Source Project
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

#include "details/InstanceBuffer.h"

#include <filament/InstanceBuffer.h>

#include <math/mat4.h>

#include <cstddef>

namespace filament {

/**
 * 获取实例数量
 * 
 * @return 实例数量
 */
size_t InstanceBuffer::getInstanceCount() const noexcept {
    return downcast(this)->getInstanceCount();
}

/**
 * 设置局部变换矩阵
 * 
 * @param localTransforms 局部变换矩阵数组指针
 * @param count 要设置的实例数量
 * @param offset 起始偏移量
 */
void InstanceBuffer::setLocalTransforms(
        math::mat4f const* localTransforms, size_t const count, size_t const offset) {
    downcast(this)->setLocalTransforms(localTransforms, count, offset);
}

/**
 * 获取指定索引的局部变换矩阵
 * 
 * @param index 实例索引
 * @return 局部变换矩阵引用
 */
math::mat4f const& InstanceBuffer::getLocalTransform(size_t index) {
    return downcast(this)->getLocalTransform(index);
}

} // namespace filament
