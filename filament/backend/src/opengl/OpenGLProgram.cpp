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

#include "OpenGLProgram.h"

#include "GLUtils.h"
#include "GLTexture.h"
#include "OpenGLDriver.h"
#include "ShaderCompilerService.h"

#include <backend/DriverEnums.h>
#include <backend/Program.h>
#include <backend/Handle.h>

#include <private/utils/Tracing.h>

#include <utils/BitmaskEnum.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <algorithm>
#include <new>
#include <string_view>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament::backend {

using namespace filament::math;
using namespace utils;
using namespace backend;

/**
 * 延迟初始化数据结构
 * 
 * 存储程序首次使用前需要的数据。
 * 这些数据在程序编译完成后会被删除。
 */
struct OpenGLProgram::LazyInitializationData {
    Program::DescriptorSetInfo descriptorBindings;        // 描述符堆绑定信息
    Program::BindingUniformsInfo bindingUniformInfo;      // 绑定 Uniform 信息（仅 ES2）
    FixedCapacityVector<Program::PushConstant> vertexPushConstants;      // 顶点着色器 Push Constant
    FixedCapacityVector<Program::PushConstant> fragmentPushConstants;    // 片段着色器 Push Constant
};


/**
 * 默认构造函数
 * 
 * 创建一个空的 OpenGLProgram。
 */
OpenGLProgram::OpenGLProgram() noexcept = default;

/**
 * 构造函数
 * 
 * 从 Program 对象创建 OpenGLProgram。
 * 程序不会立即编译，而是在首次使用时延迟编译。
 * 
 * @param gld OpenGLDriver 引用
 * @param program 程序对象（将被移动）
 */
OpenGLProgram::OpenGLProgram(OpenGLDriver& gld, Program&& program) noexcept
        : HwProgram(std::move(program.getName())), mRec709Location(-1) {
    // 创建延迟初始化数据
    auto* const lazyInitializationData = new(std::nothrow) LazyInitializationData();
    
    // ES2 需要额外的 Uniform 信息
    if (UTILS_UNLIKELY(gld.getContext().isES2())) {
        lazyInitializationData->bindingUniformInfo = std::move(program.getBindingUniformInfo());
    }
    
    // 保存 Push Constant 和描述符绑定信息
    lazyInitializationData->vertexPushConstants = std::move(program.getPushConstants(ShaderStage::VERTEX));
    lazyInitializationData->fragmentPushConstants = std::move(program.getPushConstants(ShaderStage::FRAGMENT));
    lazyInitializationData->descriptorBindings = std::move(program.getDescriptorBindings());

    // 创建编译令牌（程序会在首次使用时编译）
    ShaderCompilerService& compiler = gld.getShaderCompilerService();
    mToken = compiler.createProgram(name, std::move(program));

    // 将延迟初始化数据附加到编译令牌
    ShaderCompilerService::setUserData(mToken, lazyInitializationData);
}

/**
 * 析构函数
 * 
 * 清理程序资源，包括删除 OpenGL 程序和编译令牌。
 */
OpenGLProgram::~OpenGLProgram() noexcept {
    if (mToken) {
        // 如果 token 非空，说明程序尚未使用，需要清理
        assert_invariant(gl.program == 0);

        // 获取并删除延迟初始化数据
        LazyInitializationData* const lazyInitializationData =
                (LazyInitializationData *)ShaderCompilerService::getUserData(mToken);
        delete lazyInitializationData;

        // 终止编译令牌
        ShaderCompilerService::terminate(mToken);
        assert_invariant(!mToken);
    }

    // 删除 ES2 Uniform 记录
    delete [] mUniformsRecords;
    
    // 删除 OpenGL 程序对象
    const GLuint program = gl.program;
    if (program) {
        glDeleteProgram(program);
    }
}

/**
 * 初始化程序
 * 
 * 从编译令牌获取已编译的程序，并初始化程序状态。
 * 
 * @param gld OpenGLDriver 引用
 */
void OpenGLProgram::initialize(OpenGLDriver& gld) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    assert_invariant(gl.program == 0);
    assert_invariant(mToken);

    // 获取延迟初始化数据
    LazyInitializationData* const lazyInitializationData =
            (LazyInitializationData *)ShaderCompilerService::getUserData(mToken);

    // 从编译服务获取已编译的程序
    ShaderCompilerService& compiler = gld.getShaderCompilerService();
    gl.program = compiler.getProgram(mToken);

    // 编译完成后，token 应该被清空
    assert_invariant(mToken == nullptr);
    
    if (gl.program) {
        // 程序编译成功，初始化程序状态
        assert_invariant(lazyInitializationData);
        initializeProgramState(gld.getContext(), gl.program, *lazyInitializationData);
        delete lazyInitializationData;
    }
}

/**
 * 初始化程序状态
 * 
 * 从已编译的程序初始化绑定映射、Push Constant 等状态。
 * 这必须在程序编译成功后调用。
 * 
 * @param context OpenGLContext 引用
 * @param program OpenGL 程序对象 ID
 * @param lazyInitializationData 延迟初始化数据
 */
void OpenGLProgram::initializeProgramState(OpenGLContext& context, GLuint program,
        LazyInitializationData& lazyInitializationData) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    // 从管线布局计算 {set, binding} 到 {binding} 的映射
    // 用于缓冲区和纹理

    // 对每个描述符堆的描述符按绑定索引排序
    for (auto&& entry: lazyInitializationData.descriptorBindings) {
        std::sort(entry.begin(), entry.end(),
                [](Program::Descriptor const& lhs, Program::Descriptor const& rhs) {
                    return lhs.binding < rhs.binding;
                });
    }

    GLuint tmu = 0;      // 纹理单元计数器
    GLuint binding = 0;  // 缓冲区绑定点计数器

    // 需要先使用程序，以便设置采样器 Uniform
    context.useProgram(program);

    // 遍历所有描述符堆
    UTILS_NOUNROLL
    for (descriptor_set_t set = 0; set < MAX_DESCRIPTOR_SET_COUNT; set++) {
        // 遍历当前描述符堆的所有描述符
        for (Program::Descriptor const& entry: lazyInitializationData.descriptorBindings[set]) {
            switch (entry.type) {
                case DescriptorType::UNIFORM_BUFFER:
                case DescriptorType::SHADER_STORAGE_BUFFER: {
                    // 处理 Uniform 缓冲区和存储缓冲区
                    if (!entry.name.empty()) {
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
                        if (UTILS_LIKELY(!context.isES2())) {
                            // ES 3.0+：使用 Uniform Block
                            GLuint const index = glGetUniformBlockIndex(program,
                                    entry.name.c_str());
                            if (index != GL_INVALID_INDEX) {
                                // 如果程序不使用此描述符，这可能会失败
                                glUniformBlockBinding(program, index, binding);
                                mBindingMap.insert(set, entry.binding,
                                        { binding, entry.type });
                                ++binding;
                            }
                        } else
#endif
                        {
                            // ES2：查找绑定信息
                            auto pos = std::find_if(lazyInitializationData.bindingUniformInfo.begin(),
                                    lazyInitializationData.bindingUniformInfo.end(),
                                    [&name = entry.name](const auto& item) {
                                return std::get<1>(item) == name;
                            });
                            if (pos != lazyInitializationData.bindingUniformInfo.end()) {
                                binding = std::get<0>(*pos);
                                mBindingMap.insert(set, entry.binding, { binding, entry.type });
                            }
                        }
                    }
                    break;
                }
                case DescriptorType::SAMPLER_2D_FLOAT:
                case DescriptorType::SAMPLER_2D_INT:
                case DescriptorType::SAMPLER_2D_UINT:
                case DescriptorType::SAMPLER_2D_DEPTH:
                case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_2D_ARRAY_INT:
                case DescriptorType::SAMPLER_2D_ARRAY_UINT:
                case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:
                case DescriptorType::SAMPLER_CUBE_FLOAT:
                case DescriptorType::SAMPLER_CUBE_INT:
                case DescriptorType::SAMPLER_CUBE_UINT:
                case DescriptorType::SAMPLER_CUBE_DEPTH:
                case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_INT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:
                case DescriptorType::SAMPLER_3D_FLOAT:
                case DescriptorType::SAMPLER_3D_INT:
                case DescriptorType::SAMPLER_3D_UINT:
                case DescriptorType::SAMPLER_2D_MS_FLOAT:
                case DescriptorType::SAMPLER_2D_MS_INT:
                case DescriptorType::SAMPLER_2D_MS_UINT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:
                case DescriptorType::SAMPLER_EXTERNAL: {
                    // 处理所有类型的采样器
                    if (!entry.name.empty()) {
                        GLint const loc = glGetUniformLocation(program, entry.name.c_str());
                        if (loc >= 0) {
                            // 如果程序不使用此描述符，这可能会失败
                            // 将采样器绑定到纹理单元
                            mBindingMap.insert(set, entry.binding, { tmu, entry.type });
                            glUniform1i(loc, GLint(tmu));
                            ++tmu;
                        }
                    }
                    break;
                }
                case DescriptorType::INPUT_ATTACHMENT:
                    // 输入附件不需要绑定（在渲染通道中处理）
                    break;
            }
        }
        CHECK_GL_ERROR()
    }

    // ES2 专用初始化：初始化（伪）UBO
    if (context.isES2()) {
        UniformsRecord* const uniformsRecords = new(std::nothrow) UniformsRecord[Program::UNIFORM_BINDING_COUNT];
        UTILS_NOUNROLL
        for (auto&& [index, name, uniforms] : lazyInitializationData.bindingUniformInfo) {
            // 为每个 Uniform 获取位置
            uniformsRecords[index].locations.reserve(uniforms.size());
            uniformsRecords[index].locations.resize(uniforms.size());
            for (size_t j = 0, c = uniforms.size(); j < c; j++) {
                GLint const loc = glGetUniformLocation(program, uniforms[j].name.c_str());
                uniformsRecords[index].locations[j] = loc;
                if (UTILS_UNLIKELY(index == 0)) {
                    // 这是一个有点粗糙的 hack：我们存储 "frameUniforms.rec709" 的位置，
                    // 这显然后端不应该知道，它用于在着色器中模拟 "rec709" 色彩空间。
                    // 后端也不应该知道绑定 0 是 frameUniform 的位置。
                    std::string_view const uniformName{
                            uniforms[j].name.data(), uniforms[j].name.size() };
                    if (uniformName == "frameUniforms.rec709") {
                        mRec709Location = loc;
                    }
                }
            }
            uniformsRecords[index].uniforms = std::move(uniforms);
        }
        mUniformsRecords = uniformsRecords;
    }

    // 初始化 Push Constant
    auto& vertexConstants = lazyInitializationData.vertexPushConstants;
    auto& fragmentConstants = lazyInitializationData.fragmentPushConstants;

    size_t const totalConstantCount = vertexConstants.size() + fragmentConstants.size();
    if (totalConstantCount > 0) {
        mPushConstants.reserve(totalConstantCount);
        mPushConstantFragmentStageOffset = vertexConstants.size();
        
        // 将 Push Constant 转换为 Uniform 位置和类型对
        auto const transformAndAdd = [&](Program::PushConstant const& constant) {
            GLint const loc = glGetUniformLocation(program, constant.name.c_str());
            mPushConstants.push_back({loc, constant.type});
        };
        
        // 添加顶点和片段着色器的 Push Constant
        std::for_each(vertexConstants.cbegin(), vertexConstants.cend(), transformAndAdd);
        std::for_each(fragmentConstants.cbegin(), fragmentConstants.cend(), transformAndAdd);
    }
}

/**
 * 更新 Uniform（仅 ES2）
 * 
 * 更新指定 Uniform 缓冲区的 Uniform 值。
 * 仅用于 ES2，因为 ES2 不支持 UBO，需要手动更新每个 Uniform。
 * 
 * @param index Uniform 绑定索引
 * @param id Uniform 缓冲区 ID
 * @param buffer Uniform 数据缓冲区
 * @param age Uniform 缓冲区年龄（用于检测更新）
 * @param offset Uniform 缓冲区偏移
 */
void OpenGLProgram::updateUniforms(
        uint32_t const index, GLuint const id, void const* buffer,
        uint16_t const age, uint32_t const offset) const noexcept {
    assert_invariant(mUniformsRecords);
    assert_invariant(buffer);

    // 只有当 UBO 自上次更新以来发生变化时才更新 Uniform
    UniformsRecord const& records = mUniformsRecords[index];
    if (records.id == id && records.age == age && records.offset == offset) {
        return;
    }
    
    // 更新记录
    records.id = id;
    records.age = age;
    records.offset = offset;

    assert_invariant(records.uniforms.size() == records.locations.size());

    // 应用偏移到缓冲区
    buffer = static_cast<char const*>(buffer) + offset;

    // 更新每个 Uniform
    for (size_t i = 0, c = records.uniforms.size(); i < c; i++) {
        Program::Uniform const& u = records.uniforms[i];
        GLint const loc = records.locations[i];
        
        // mRec709Location 是特殊的，它由 setRec709ColorSpace() 处理，
        // `buffer` 中对应的条目通常未初始化，所以跳过它
        if (loc < 0 || loc == mRec709Location) {
            continue;
        }
        
        // u.offset 以 'uint32_t' 为单位
        GLfloat const* const bf = reinterpret_cast<GLfloat const*>(buffer) + u.offset;
        GLint const* const bi = reinterpret_cast<GLint const*>(buffer) + u.offset;

        // 根据 Uniform 类型更新
        switch(u.type) {
            case UniformType::FLOAT:
                glUniform1fv(loc, u.size, bf);
                break;
            case UniformType::FLOAT2:
                glUniform2fv(loc, u.size, bf);
                break;
            case UniformType::FLOAT3:
                glUniform3fv(loc, u.size, bf);
                break;
            case UniformType::FLOAT4:
                glUniform4fv(loc, u.size, bf);
                break;

            case UniformType::BOOL:
            case UniformType::INT:
            case UniformType::UINT:
                glUniform1iv(loc, u.size, bi);
                break;
            case UniformType::BOOL2:
            case UniformType::INT2:
            case UniformType::UINT2:
                glUniform2iv(loc, u.size, bi);
                break;
            case UniformType::BOOL3:
            case UniformType::INT3:
            case UniformType::UINT3:
                glUniform3iv(loc, u.size, bi);
                break;
            case UniformType::BOOL4:
            case UniformType::INT4:
            case UniformType::UINT4:
                glUniform4iv(loc, u.size, bi);
                break;

            case UniformType::MAT3:
                glUniformMatrix3fv(loc, u.size, GL_FALSE, bf);
                break;
            case UniformType::MAT4:
                glUniformMatrix4fv(loc, u.size, GL_FALSE, bf);
                break;

            case UniformType::STRUCT:
                // 不支持结构体类型
                break;
        }
    }
}

/**
 * 设置 Rec709 色彩空间（仅 ES2）
 * 
 * 设置 Rec709 色彩空间标志。
 * 仅用于 ES2。
 * 
 * @param rec709 是否为 Rec709 色彩空间
 */
void OpenGLProgram::setRec709ColorSpace(bool rec709) const noexcept {
    glUniform1i(mRec709Location, rec709);
}


} // namespace filament::backend
