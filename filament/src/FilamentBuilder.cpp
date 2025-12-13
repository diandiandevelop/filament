/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <filament/FilamentAPI.h>

#include <utils/ImmutableCString.h>

#include <algorithm>

namespace filament {

/**
 * 构建器名称设置函数
 * 
 * 从输入名称创建不可变 C 字符串，限制最大长度为 128 字符。
 * 
 * @param outName 输出的不可变 C 字符串引用
 * @param name 输入名称指针（可以为 nullptr）
 * @param len 输入名称长度
 */
void builderMakeName(utils::ImmutableCString& outName, const char* name, size_t const len) noexcept {
    /**
     * 如果名称为空，直接返回（不修改 outName）
     */
    if (!name) {
        return;
    }
    /**
     * 限制长度为 128 字符（防止过长的名称）
     */
    size_t const length = std::min(len, size_t { 128u });
    /**
     * 创建不可变 C 字符串并赋值给输出
     */
    outName = utils::ImmutableCString(name, length);
}

} // namespace filament
