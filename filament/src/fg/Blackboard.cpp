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

#include "fg/Blackboard.h"

#include <string_view>

namespace filament {

/**
 * 构造函数
 */
Blackboard::Blackboard() noexcept = default;

/**
 * 析构函数
 */
Blackboard::~Blackboard() noexcept = default;

/**
 * 获取句柄（内部方法）
 * 
 * 查找指定名称的句柄，如果不存在则返回空句柄。
 * 
 * @param name 资源名称
 * @return 帧图句柄
 */
FrameGraphHandle Blackboard::getHandle(std::string_view const name) const noexcept {
    auto it = mMap.find(name);  // 查找名称
    if (it != mMap.end()) {
        return it->second;  // 找到则返回句柄
    }
    return {};  // 未找到则返回空句柄
}

/**
 * 下标操作符实现
 * 
 * 获取或创建指定名称的句柄引用。
 * 如果名称不存在，会创建一个新条目并初始化为空句柄。
 * 
 * @param name 资源名称
 * @return 句柄引用
 */
FrameGraphHandle& Blackboard::operator [](std::string_view const name) noexcept {
    /**
     * 插入或赋值：如果名称存在则更新，不存在则创建
     */
    auto[pos, _] = mMap.insert_or_assign(name, FrameGraphHandle{});
    return pos->second;  // 返回句柄引用
}

/**
 * 放置句柄实现
 * 
 * 将句柄存储到指定名称下。
 * 
 * @param name 资源名称
 * @param handle 帧图句柄
 */
void Blackboard::put(std::string_view const name, FrameGraphHandle const handle) noexcept {
    operator[](name) = handle;  // 使用下标操作符赋值
}

/**
 * 移除句柄实现
 * 
 * 从黑板中移除指定名称的句柄。
 * 
 * @param name 资源名称
 */
void Blackboard::remove(std::string_view const name) noexcept {
    mMap.erase(name);  // 从映射中删除
}

} // namespace filament
