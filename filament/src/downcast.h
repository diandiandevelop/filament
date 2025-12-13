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

#ifndef TNT_FILAMENT_DOWNCAST_H
#define TNT_FILAMENT_DOWNCAST_H

/**
 * 向下转换宏
 * 
 * 生成函数以安全地将指针 Bar* 向下转换为 FBar*。
 * 
 * FILAMENT_DOWNCAST() 应该包含在声明 FBar 的头文件中，例如：
 * 
 * ```cpp
 * #include <Bar.h>
 * 
 * class FBar : public Bar {
 * };
 * 
 * FILAMENT_DOWNCAST(Bar)
 * ```
 * 
 * 使用场景：
 * - Filament 使用 PIMPL（Pointer to Implementation）模式
 * - 公共 API 使用 Bar 类，内部实现使用 FBar 类
 * - downcast() 函数提供从公共 API 到内部实现的类型安全转换
 * 
 * @param CLASS 基类名称（例如 Bar）
 * 
 * 生成的函数：
 * - downcast(Bar&) -> FBar&
 * - downcast(const Bar&) -> const FBar&
 * - downcast(Bar*) -> FBar*
 * - downcast(const Bar*) -> const FBar*
 */
#define FILAMENT_DOWNCAST(CLASS)                                    \
    inline F##CLASS& downcast(CLASS& that) noexcept {               \
        return static_cast<F##CLASS &>(that);                       \
    }                                                               \
    inline const F##CLASS& downcast(const CLASS& that) noexcept {   \
        return static_cast<const F##CLASS &>(that);                 \
    }                                                               \
    inline F##CLASS* downcast(CLASS* that) noexcept {               \
        return static_cast<F##CLASS *>(that);                       \
    }                                                               \
    inline F##CLASS const* downcast(CLASS const* that) noexcept {   \
        return static_cast<F##CLASS const *>(that);                 \
    }

#endif // TNT_FILAMENT_DOWNCAST_H
