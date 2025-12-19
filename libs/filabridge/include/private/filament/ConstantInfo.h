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

#ifndef TNT_FILAMENT_CONSTANTINFO_H
#define TNT_FILAMENT_CONSTANTINFO_H

#include <backend/DriverEnums.h>

#include <utils/CString.h>

namespace filament {

/**
 * 材质常量结构
 * 描述材质中的一个常量参数
 */
struct MaterialConstant {
    using ConstantType = backend::ConstantType;      // 常量类型别名
    using ConstantValue = backend::ConstantValue;    // 常量值类型别名

    utils::CString name;              // 常量名称
    ConstantType type;                // 常量类型
    ConstantValue defaultValue;       // 默认值

    // 默认构造函数
    MaterialConstant() = default;
    /**
     * 构造函数
     * @param name 常量名称
     * @param type 常量类型
     * @param defaultValue 默认值
     */
    MaterialConstant(utils::CString name, ConstantType type, ConstantValue defaultValue)
            : name(std::move(name)), type(type), defaultValue(defaultValue) {}
};

}

#endif  // TNT_FILAMENT_CONSTANTINFO_H
