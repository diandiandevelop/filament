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

#ifndef TNT_FILAMENT_FG_DETAILS_UTILITIES_H
#define TNT_FILAMENT_FG_DETAILS_UTILITIES_H

#include "Allocators.h"

#include <vector>
#include <memory>

namespace filament {

class FrameGraph;

/**
 * 删除器模板类
 * 
 * 用于 unique_ptr，使用 Arena 分配器销毁对象。
 * 
 * @tparam T 对象类型
 * @tparam ARENA Arena 类型
 */
template<typename T, typename ARENA>
struct Deleter {
    ARENA* arena = nullptr;  // Arena 指针
    
    /**
     * 构造函数
     * 
     * @param arena Arena 引用
     */
    Deleter(ARENA& arena) noexcept: arena(&arena) {} // NOLINT
    
    /**
     * 调用操作符
     * 
     * 使用 Arena 销毁对象。
     * 
     * @param object 对象指针
     */
    void operator()(T* object) noexcept { arena->destroy(object); }  // 调用 Arena 的销毁方法
};

/**
 * 唯一指针类型别名
 * 
 * 使用 Arena 删除器的 unique_ptr。
 * 
 * @tparam T 对象类型
 * @tparam ARENA Arena 类型
 */
template<typename T, typename ARENA> using UniquePtr = std::unique_ptr<T, Deleter<T, ARENA>>;

/**
 * 分配器类型别名
 * 
 * 使用线性分配器 Arena 的 STL 分配器。
 * 
 * @tparam T 元素类型
 */
template<typename T> using Allocator = utils::STLAllocator<T, LinearAllocatorArena>;

/**
 * 向量类型别名
 * 
 * 使用 Arena 分配器的 std::vector。
 * 
 * @tparam T 元素类型
 * 
 * 注意：std::vector 本身占用 32 字节。
 */
template<typename T> using Vector = std::vector<T, Allocator<T>>; // 32 bytes

} // namespace filament

#endif // TNT_FILAMENT_FG_DETAILS_UTILITIES_H
