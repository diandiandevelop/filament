/*
* Copyright (C) 2022 The Android Open Source Project
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

#include "MetalArgumentBuffer.h"

#include <sstream>
#include <utility>
#include <variant>

namespace filamat {

// 设置参数缓冲区结构的名称
// @param name 结构名称
// @return 构建器引用（支持链式调用）
MetalArgumentBuffer::Builder& filamat::MetalArgumentBuffer::Builder::name(
        const std::string& name) noexcept {
    mName = name;
    return *this;
}

// 添加纹理参数到参数缓冲区结构
// @param index 纹理参数的[[id(n)]]索引
// @param name 纹理参数名称
// @param type 纹理数据类型
// @param format 纹理数据格式
// @param multisample 是否为多重采样纹理
// @return 构建器引用（支持链式调用）
MetalArgumentBuffer::Builder& MetalArgumentBuffer::Builder::texture(size_t index,
        const std::string& name, filament::backend::SamplerType type,
        filament::backend::SamplerFormat format,
        bool multisample) noexcept {

    using namespace filament::backend;

    // All combinations of SamplerType and SamplerFormat are valid except for SAMPLER_3D / SHADOW.
    assert_invariant(type != SamplerType::SAMPLER_3D || format != SamplerFormat::SHADOW);

    // multisample textures have restrictions too
    assert_invariant(!multisample || (
            format != SamplerFormat::SHADOW && (
                    type == SamplerType::SAMPLER_2D || type == SamplerType::SAMPLER_2D_ARRAY)));

    mArguments.emplace_back(TextureArgument { name, index, type, format, multisample });
    return *this;
}

// 添加采样器参数到参数缓冲区结构
// @param index 采样器参数的[[id(n)]]索引
// @param name 采样器参数名称
// @return 构建器引用（支持链式调用）
MetalArgumentBuffer::Builder& MetalArgumentBuffer::Builder::sampler(
        size_t index, const std::string& name) noexcept {
    mArguments.emplace_back(SamplerArgument { name , index });
    return *this;
}

// 添加缓冲区参数到参数缓冲区结构
// @param index 缓冲区参数的[[id(n)]]索引
// @param type 缓冲区指向的数据类型
// @param name 缓冲区参数名称
// @return 构建器引用（支持链式调用）
MetalArgumentBuffer::Builder& MetalArgumentBuffer::Builder::buffer(
        size_t index, const std::string& type, const std::string& name) noexcept {
    mArguments.emplace_back(BufferArgument { name, index, type });
    return *this;
}

// 构建MetalArgumentBuffer对象
// @return 新创建的MetalArgumentBuffer指针
MetalArgumentBuffer* MetalArgumentBuffer::Builder::build() {
    assert_invariant(!mName.empty());
    return new MetalArgumentBuffer(*this);
}

// 写入纹理参数到输出流（生成MSL代码）
// @param os 输出流
// @return 输出流引用
std::ostream& MetalArgumentBuffer::Builder::TextureArgument::write(std::ostream& os) const {
    switch (format) {
        case filament::backend::SamplerFormat::INT:
        case filament::backend::SamplerFormat::UINT:
        case filament::backend::SamplerFormat::FLOAT:
            os << "texture";
            break;
        case filament::backend::SamplerFormat::SHADOW:
            os << "depth";
            break;
    }

    switch (type) {
        case filament::backend::SamplerType::SAMPLER_EXTERNAL:
        case filament::backend::SamplerType::SAMPLER_2D:
            os << "2d";
            break;
        case filament::backend::SamplerType::SAMPLER_2D_ARRAY:
            os << "2d_array";
            break;
        case filament::backend::SamplerType::SAMPLER_CUBEMAP:
            os << "cube";
            break;
        case filament::backend::SamplerType::SAMPLER_3D:
            os << "3d";
            break;
        case filament::backend::SamplerType::SAMPLER_CUBEMAP_ARRAY:
            os << "cube_array";
            break;
    }

    if (multisample) {
        os << "_ms";
    }

    switch (format) {
        case filament::backend::SamplerFormat::INT:
            os << "<int>";
            break;
        case filament::backend::SamplerFormat::UINT:
            os << "<uint>";
            break;
        case filament::backend::SamplerFormat::FLOAT:
        case filament::backend::SamplerFormat::SHADOW:
            os << "<float>";
            break;
    }

    os << " " << name << " [[id(" << index << ")]];" << std::endl;
    return os;
}

// 写入采样器参数到输出流（生成MSL代码）
// @param os 输出流
// @return 输出流引用
std::ostream& MetalArgumentBuffer::Builder::SamplerArgument::write(std::ostream& os) const {
    os << "sampler " << name << " [[id(" << index << ")]];" << std::endl;
    return os;
}

// 写入缓冲区参数到输出流（生成MSL代码）
// @param os 输出流
// @return 输出流引用
std::ostream& MetalArgumentBuffer::Builder::BufferArgument::write(std::ostream& os) const {
    os << "constant " << type << "* " << name << " [[id(" << index << ")]];" << std::endl;
    return os;
}

// 构造函数：从构建器创建MetalArgumentBuffer对象
// @param builder 构建器对象
MetalArgumentBuffer::MetalArgumentBuffer(Builder& builder) {
    // 步骤1：保存参数缓冲区名称
    mName = builder.mName;

    auto& args = builder.mArguments;

    // Sort the arguments by index.
    // 步骤2：按索引对参数进行排序
    std::sort(std::begin(args), std::end(args), [](auto const& lhs, auto const& rhs) {
        return std::visit([](auto const& x, auto const& y) { return x.index < y.index; }, lhs, rhs);
    });

    // Check that all the indices are unique.
    // 步骤3：检查所有索引是否唯一
    assert_invariant(
            std::adjacent_find(args.begin(), args.end(), [](auto const& lhs, auto const& rhs) {
                return std::visit(
                        [](auto const& x, auto const& y) { return x.index == y.index; }, lhs, rhs);
            }) == args.end());

    std::stringstream ss;

    // Add forward declarations of buffers.
    // 步骤4：添加缓冲区的前向声明
    for (const auto& a : builder.mArguments) {
        if (std::holds_alternative<Builder::BufferArgument>(a)) {
            const auto& bufferArg = std::get<Builder::BufferArgument>(a);
            ss << "struct " << bufferArg.type << ";" << std::endl;
        }
    }

    // 步骤5：生成结构体定义开始
    ss << "struct " << mName << " {" << std::endl;

    // 步骤6：写入所有参数
    for (const auto& a : builder.mArguments) {
        std::visit([&](auto&& arg) {
            arg.write(ss);
        }, a);
    }

    // 步骤7：生成结构体定义结束并保存MSL代码
    ss << "}";
    mShaderText = ss.str();
}

// 销毁MetalArgumentBuffer对象
// @param argumentBuffer 指向MetalArgumentBuffer指针的指针（将被设置为nullptr）
void MetalArgumentBuffer::destroy(MetalArgumentBuffer** argumentBuffer) {
    delete *argumentBuffer;
    *argumentBuffer = nullptr;
}

// 检查字符是否为空白字符
// @param c 字符
// @return 如果是空白字符返回true
static bool isWhitespace(char c) {
    return (c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v');
}

// 在着色器中搜索目标参数缓冲区，并用替换字符串替换它
// @param shader 包含目标参数缓冲区定义的源MSL着色器（将被修改）
// @param targetArgBufferName 要替换的参数缓冲区定义名称
// @param replacement 替换的参数缓冲区定义
// @return 如果找到目标返回true，否则返回false
bool MetalArgumentBuffer::replaceInShader(std::string& shader,
        const std::string& targetArgBufferName, const std::string& replacement) noexcept {
    // We make some assumptions here, e.g., that the MSL is well-formed and has no comments.
    // This algorithm isn't a full-fledged parser, and isn't foolproof. In particular, we can't tell
    // the difference between source code and comments. However, at this stage, the MSL should have
    // all comments stripped.
    // 我们在这里做一些假设，例如MSL格式良好且没有注释
    // 此算法不是完整的解析器，也不是万无一失的。特别是，我们无法区分源代码和注释
    // 但是，在此阶段，MSL应该已经移除了所有注释

    // In order to do the replacement, we look for 4 key locations in the source shader.
    // s: the beginning of the 'struct' token
    // n: the beginning of the argument buffer name
    // b: the beginning of the structure block
    // e: the end of the argument buffer structure
    //
    // s      n               b e
    // struct targetArgBuffer { }
    // 为了进行替换，我们在源着色器中查找4个关键位置：
    // s: 'struct'标记的开始
    // n: 参数缓冲区名称的开始
    // b: 结构块的开始
    // e: 参数缓冲区结构的结束

    // We only want to match the definition of the argument buffer, not any of its usages.
    // For example:
    // struct ArgBuffer { };                // this should match
    // void aFunction(ArgBuffer& args);     // this should not
    // 我们只想匹配参数缓冲区的定义，而不是它的任何使用
    // 例如：struct ArgBuffer { }; 应该匹配，但 void aFunction(ArgBuffer& args); 不应该匹配

    const auto argBufferNameLength = targetArgBufferName.length();

    // First, find n.
    // 步骤1：查找参数缓冲区名称的位置（n）
    size_t n = shader.find(targetArgBufferName);
    while (n != std::string::npos) {
        // Now, find b, the opening curly brace {.
        // 步骤2：查找结构块开始位置（b，即开大括号{）
        size_t b = shader.find('{', n);
        if (b == std::string::npos) {
            // If there's no { character in the rest of the shader, the arg buffer definition
            // definitely doesn't exit.
            // 如果着色器其余部分没有{字符，参数缓冲区定义肯定不存在
            return false;
        }

        // After the arg buffer name, ensure that only whitespace characters exist until b.
        // 步骤3：确保参数缓冲区名称后到b之间只有空白字符
        if (!std::all_of(shader.begin() + n + argBufferNameLength, shader.begin() + b,
                    isWhitespace)) {
            // If there is a non-whitespace character, start over by looking for the next occurrence
            // of the arg buffer name.
            // 如果有非空白字符，重新开始查找下一个参数缓冲区名称的出现
            n = shader.find(targetArgBufferName, n + 1);
            continue;
        }

        // Now, we find s.
        // 步骤4：查找struct关键字的位置（s）
        size_t s = shader.rfind("struct", n);
        if (s == std::string::npos) {
            // If we can't find the "struct" keyword, it's not necessarily an error.
            // Start over and Look for the next occurrence of the arg buffer name.
            // 如果找不到"struct"关键字，不一定是错误。重新开始查找下一个参数缓冲区名称的出现
            n = shader.find(targetArgBufferName, n + 1);
            continue;
        }

        // After the struct keyword, ensure that only whitespace characters exist until n.
        // 步骤5：确保struct关键字后到n之间只有空白字符
        if (!std::all_of(shader.begin() + s + 6, shader.begin() + n, isWhitespace)) {
            // Look for the next occurrence of the arg buffer name.
            // 查找下一个参数缓冲区名称的出现
            n = shader.find(targetArgBufferName, n + 1);
            continue;
        }

        // Now, we find e.
        // 步骤6：查找结构结束位置（e，即闭大括号}）
        size_t e = shader.find('}', n);
        if (e == std::string::npos) {
            // If there's no } character in the rest of the shader, the arg buffer definition
            // definitely doesn't exit.
            // 如果着色器其余部分没有}字符，参数缓冲区定义肯定不存在
            return false;
        }

        // Perform the replacement.
        // 步骤7：执行替换（从s到e+1，包括闭大括号）
        shader.replace(s, e - s + 1, replacement);

        // Theoretically we could continue to find and replace other occurrences, but there should
        // only ever be a single definition of the argument buffer structure.
        // 理论上我们可以继续查找和替换其他出现，但应该只有一个参数缓冲区结构的定义
        return true;
    }

    return false;
}

} // namespace filamat
