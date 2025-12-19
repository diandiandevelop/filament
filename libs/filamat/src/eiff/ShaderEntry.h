/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMAT_SHADER_ENTRY_H
#define TNT_FILAMAT_SHADER_ENTRY_H

#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>

#include <string>
#include <vector>

namespace filamat {

// TextEntry stores a shader in ASCII text format, like GLSL.
// TextEntry以ASCII文本格式（如GLSL）存储着色器
struct TextEntry {
    filament::backend::ShaderModel shaderModel;  // 着色器模型
    filament::Variant variant;                    // 着色器变体
    filament::backend::ShaderStage stage;        // 着色器阶段
    std::string shader;                          // 着色器代码（文本格式）
};

// 二进制条目结构，存储二进制着色器信息
struct BinaryEntry {
    filament::backend::ShaderModel shaderModel;  // 着色器模型
    filament::Variant variant;                    // 着色器变体
    filament::backend::ShaderStage stage;        // 着色器阶段
    size_t dictionaryIndex;  // maps to an index in the blob dictionary - 映射到blob字典中的索引

    // temporarily holds this entry's binary data until added to the dictionary
    // 临时保存此条目的二进制数据，直到添加到字典中
    std::vector<uint8_t> data;
};

}  // namespace filamat

#endif // TNT_FILAMAT_SHADER_ENTRY_H
