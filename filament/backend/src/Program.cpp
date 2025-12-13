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

#include <backend/Program.h>
#include <backend/DriverEnums.h>

#include <utils/debug.h>
#include <utils/CString.h>
#include <utils/ostream.h>
#include <utils/Invocable.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament::backend {

using namespace utils;

/**
 * Program 默认构造函数
 * 
 * 我们希望这些在 .cpp 文件中，这样它们不会被内联（不值得内联）。
 */
Program::Program() noexcept {  // NOLINT(modernize-use-equals-default)
}

/**
 * Program 移动构造函数
 */
Program::Program(Program&& rhs) noexcept = default;

/**
 * Program 移动赋值操作符
 */
Program& Program::operator=(Program&& rhs) noexcept = default;

/**
 * Program 析构函数
 */
Program::~Program() noexcept = default;

/**
 * 设置编译优先级队列
 * 
 * 设置着色器编译的优先级队列。
 * 
 * @param priorityQueue 优先级队列
 * @return Program 引用（支持链式调用）
 */
Program& Program::priorityQueue(CompilerPriorityQueue const priorityQueue) noexcept {
    mPriorityQueue = priorityQueue;
    return *this;
}

/**
 * 设置诊断信息
 * 
 * 设置程序名称和日志记录器，用于调试和错误报告。
 * 
 * @param name 程序名称
 * @param logger 日志记录器函数
 * @return Program 引用（支持链式调用）
 */
Program& Program::diagnostics(CString const& name,
        Invocable<io::ostream&(CString const& name, io::ostream&)>&& logger) {
    mName = name;
    mLogger = std::move(logger);
    return *this;
}

/**
 * 设置着色器源码
 * 
 * 为指定的着色器阶段设置源码。
 * 
 * @param shader 着色器阶段（VERTEX、FRAGMENT、COMPUTE 等）
 * @param data 源码数据指针
 * @param size 源码大小（字节）
 * @return Program 引用（支持链式调用）
 */
Program& Program::shader(ShaderStage shader, void const* data, size_t const size) {
    ShaderBlob blob(size);
    std::copy_n((const uint8_t *)data, size, blob.data());
    mShadersSource[size_t(shader)] = std::move(blob);
    return *this;
}

/**
 * 设置着色器语言
 * 
 * 指定着色器使用的语言（GLSL、SPIRV 等）。
 * 
 * @param shaderLanguage 着色器语言
 * @return Program 引用（支持链式调用）
 */
Program& Program::shaderLanguage(ShaderLanguage const shaderLanguage) {
    mShaderLanguage = shaderLanguage;
    return *this;
}

/**
 * 设置描述符绑定
 * 
 * 为指定的描述符堆设置绑定信息。
 * 
 * @param set 描述符堆索引
 * @param descriptorBindings 描述符绑定信息
 * @return Program 引用（支持链式调用）
 */
Program& Program::descriptorBindings(descriptor_set_t const set,
        DescriptorBindingsInfo descriptorBindings) noexcept {
    mDescriptorBindings[set] = std::move(descriptorBindings);
    return *this;
}

/**
 * 设置 Uniform 信息
 * 
 * 为指定的绑定索引设置 Uniform 信息。
 * 
 * @param index 绑定索引
 * @param name Uniform 名称
 * @param uniforms Uniform 信息
 * @return Program 引用（支持链式调用）
 */
Program& Program::uniforms(uint32_t index, CString name, UniformInfo uniforms) {
    mBindingUniformsInfo.reserve(mBindingUniformsInfo.capacity() + 1);
    mBindingUniformsInfo.emplace_back(index, std::move(name), std::move(uniforms));
    return *this;
}

/**
 * 设置顶点属性信息
 * 
 * 设置顶点着色器的输入属性信息。
 * 
 * @param attributes 属性信息
 * @return Program 引用（支持链式调用）
 */
Program& Program::attributes(AttributesInfo attributes) noexcept {
    mAttributes = std::move(attributes);
    return *this;
}

/**
 * 设置特化常量
 * 
 * 设置着色器特化常量（用于优化编译）。
 * 
 * @param specConstants 特化常量信息
 * @return Program 引用（支持链式调用）
 */
Program& Program::specializationConstants(SpecializationConstantsInfo specConstants) noexcept {
    mSpecializationConstants = std::move(specConstants);
    return *this;
}

/**
 * 设置推送常量
 * 
 * 为指定的着色器阶段设置推送常量。
 * 
 * @param stage 着色器阶段
 * @param constants 推送常量向量
 * @return Program 引用（支持链式调用）
 */
Program& Program::pushConstants(ShaderStage stage,
        FixedCapacityVector<PushConstant> constants) noexcept {
    mPushConstants[static_cast<uint8_t>(stage)] = std::move(constants);
    return *this;
}

/**
 * 设置缓存 ID
 * 
 * 设置程序二进制缓存的 ID，用于快速检索已编译的程序。
 * 
 * @param cacheId 缓存 ID
 * @return Program 引用（支持链式调用）
 */
Program& Program::cacheId(uint64_t const cacheId) noexcept {
    mCacheId = cacheId;
    return *this;
}

/**
 * 设置多视图支持
 * 
 * 启用或禁用多视图渲染（VR/AR 应用）。
 * 
 * @param multiview 是否启用多视图
 * @return Program 引用（支持链式调用）
 */
Program& Program::multiview(bool const multiview) noexcept {
    mMultiview = multiview;
    return *this;
}

io::ostream& operator<<(io::ostream& out, const Program& builder) {
    out << "Program{";
    builder.mLogger(builder.mName, out);
    out << "}";
    return out;
}

} // namespace filament::backend
