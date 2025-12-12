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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSETLAYOUT_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSETLAYOUT_H

// 驱动基类
#include "DriverBase.h"

// 后端枚举
#include <backend/DriverEnums.h>

// 标准库
#include <algorithm>  // 算法（sort、max_element）
#include <utility>    // 工具（move）

#include <stdint.h>   // 标准整数类型

namespace filament::backend {

/**
 * OpenGL 描述符集布局结构
 * 
 * 封装描述符集的布局信息，定义描述符集中每个绑定的类型和属性。
 * 
 * 主要功能：
 * 1. 存储描述符绑定信息（类型、阶段标志、绑定索引等）
 * 2. 对绑定进行排序（按绑定索引）
 * 3. 计算最大绑定索引（用于分配描述符数组大小）
 * 
 * 设计目的：
 * - 描述符集布局定义了描述符集的结构
 * - 用于验证描述符集是否与程序布局匹配
 * - 用于分配描述符集的存储空间
 */
struct GLDescriptorSetLayout : public HwDescriptorSetLayout, public DescriptorSetLayout {
    using HwDescriptorSetLayout::HwDescriptorSetLayout;
    
    /**
     * 构造函数
     * 
     * 从 DescriptorSetLayout 创建 GLDescriptorSetLayout。
     * 对绑定进行排序并计算最大绑定索引。
     * 
     * @param layout 描述符集布局（将被移动）
     * 
     * 执行流程：
     * 1. 移动布局数据
     * 2. 按绑定索引对绑定进行排序
     * 3. 查找最大绑定索引
     * 4. 存储最大绑定索引（用于分配描述符数组）
     */
    explicit GLDescriptorSetLayout(DescriptorSetLayout&& layout) noexcept
            : DescriptorSetLayout(std::move(layout)) {

        // 按绑定索引对绑定进行排序
        std::sort(bindings.begin(), bindings.end(),
                [](auto&& lhs, auto&& rhs){
            return lhs.binding < rhs.binding;
        });

        // 查找最大绑定索引
        auto p = std::max_element(bindings.cbegin(), bindings.cend(),
                [](auto const& lhs, auto const& rhs) {
            return lhs.binding < rhs.binding;
        });
        maxDescriptorBinding = p->binding;
    }
    
    /**
     * 最大描述符绑定索引
     * 
     * 描述符集中最大的绑定索引。
     * 用于分配描述符数组的大小（maxDescriptorBinding + 1）。
     */
    uint8_t maxDescriptorBinding = 0;
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSETLAYOUT_H
