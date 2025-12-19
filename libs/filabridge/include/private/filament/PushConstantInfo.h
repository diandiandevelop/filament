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

#ifndef TNT_FILAMENT_PUSHCONSTANTINFO_H
#define TNT_FILAMENT_PUSHCONSTANTINFO_H

#include <backend/DriverEnums.h>

#include <utils/CString.h>

namespace filament {

/**
 * 材质推送常量结构
 * 描述材质中的一个推送常量（push constant）参数
 */
struct MaterialPushConstant {
    using ShaderStage = backend::ShaderStage;        // 着色器阶段类型别名
    using ConstantType = backend::ConstantType;      // 常量类型别名

    utils::CString name;      // 推送常量名称
    ConstantType type;        // 推送常量类型
    ShaderStage stage;        // 适用的着色器阶段

    // 默认构造函数
    MaterialPushConstant() = default;
    /**
     * 构造函数
     * @param name 推送常量名称
     * @param type 推送常量类型
     * @param stage 适用的着色器阶段
     */
    MaterialPushConstant(const char* name, ConstantType type, ShaderStage stage)
        : name(name),
          type(type),
          stage(stage) {}
};

}

#endif  // TNT_FILAMENT_PUSHCONSTANTINFO_H
