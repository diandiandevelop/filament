/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef TNT_SHADERMINIFIER_H
#define TNT_SHADERMINIFIER_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace filamat {

// Simple minifier for monolithic GLSL or MSL strings.
//
// Note that we already use a third party minifier, but it applies only to GLSL fragments.
// This custom minifier is designed for generated code such as uniform structs.
// 简单的GLSL或MSL字符串压缩器
// 注意：我们已经使用了第三方压缩器，但它只适用于GLSL片段
// 此自定义压缩器专为生成的代码（如uniform结构体）设计
class ShaderMinifier {
    public:
        // 移除空白字符（移除每行开头的空白字符和空行）
        // @param source 源字符串
        // @param mergeBraces 是否合并大括号（将单独的{或}移到上一行）
        // @return 压缩后的字符串
        std::string removeWhitespace(const std::string& source, bool mergeBraces = false) const;
        
        // 重命名结构体字段（压缩uniform结构体定义中的字段名）
        // @param source 源字符串
        // @return 压缩后的字符串（字段名被重命名为短名称）
        std::string renameStructFields(const std::string& source);

    private:
        using RenameEntry = std::pair<std::string, std::string>;  // 重命名条目（原名称，新名称）

        // 构建字段映射表（解析结构体定义，生成字段名映射）
        void buildFieldMapping();
        
        // 应用字段映射（使用映射表替换所有字段名）
        // @return 应用映射后的字符串
        std::string applyFieldMapping() const;

        // These fields do not need to be members, but they allow clients to reduce malloc churn
        // by persisting the minifier object.
        // 这些字段不需要是成员，但它们允许客户端通过持久化压缩器对象来减少malloc开销
        std::vector<std::string_view> mCodelines;  // 代码行列表
        std::vector<RenameEntry> mStructFieldMap;  // 结构体字段映射表
        std::unordered_map<std::string, std::string> mStructDefnMap;  // 结构体定义映射表
};

} // namespace filamat

#endif //TNT_SHADERMINIFIER_H
