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

#include "GLDescriptorSet.h"

#include "GLBufferObject.h"
#include "GLDescriptorSetLayout.h"
#include "GLTexture.h"
#include "GLUtils.h"
#include "OpenGLDriver.h"
#include "OpenGLContext.h"
#include "OpenGLProgram.h"

#include "gl_headers.h"

#include <private/backend/HandleAllocator.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/BitmaskEnum.h>
#include <utils/Log.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/bitset.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <algorithm>

#include <type_traits>
#include <utility>
#include <variant>

#include <stddef.h>
#include <stdint.h>

namespace filament::backend {

/**
 * 构造函数
 * 
 * 从描述符堆布局创建描述符堆，初始化所有描述符。
 * 
 * @param gl OpenGLContext 引用，用于检查 ES2
 * @param dslh 描述符堆布局句柄
 * @param layout 描述符堆布局指针
 * 
 * 执行流程：
 * 1. 根据布局分配描述符数组（maxDescriptorBinding + 1）
 * 2. 为每个绑定初始化对应的描述符类型
 * 3. 根据描述符类型、动态偏移标志和 ES2 支持选择适当的变体类型
 */
GLDescriptorSet::GLDescriptorSet(OpenGLContext& gl, DescriptorSetLayoutHandle dslh,
        GLDescriptorSetLayout const* layout)
        : descriptors(layout->maxDescriptorBinding + 1),
          dslh(std::move(dslh)) {

    // 我们已经为所有描述符分配了足够的存储空间。现在初始化每个描述符。

    for (auto const& entry : layout->bindings) {
        size_t const index = entry.binding;

        // 现在我们将为每种处理方式初始化对应的变体类型
        auto& desc = descriptors[index].desc;
        switch (entry.type) {
            case DescriptorType::UNIFORM_BUFFER: {
                // Uniform 缓冲区可以有动态偏移或没有，并且对 ES2 有特殊处理
                // （需要模拟它）。这是四种变体。
                bool const dynamicOffset = any(entry.flags & DescriptorFlags::DYNAMIC_OFFSET);
                dynamicBuffers.set(index, dynamicOffset);
                
                if (UTILS_UNLIKELY(gl.isES2())) {
                    // ES2 路径：使用 BufferGLES2 模拟 UBO
                    if (dynamicOffset) {
                        dynamicBufferCount++;
                    }
                    desc.emplace<BufferGLES2>(dynamicOffset);
                } else {
                    // ES 3.0+ 路径：使用 Buffer 或 DynamicBuffer
                    auto const type = GLUtils::getBufferBindingType(BufferObjectBinding::UNIFORM);
                    if (dynamicOffset) {
                        dynamicBufferCount++;
                        desc.emplace<DynamicBuffer>(type);
                    } else {
                        desc.emplace<Buffer>(type);
                    }
                }
                break;
            }
            case DescriptorType::SHADER_STORAGE_BUFFER: {
                // 着色器存储缓冲区在 ES2 上不支持，所以只有两种变体。
                bool const dynamicOffset = any(entry.flags & DescriptorFlags::DYNAMIC_OFFSET);
                dynamicBuffers.set(index, dynamicOffset);
                auto const type = GLUtils::getBufferBindingType(BufferObjectBinding::SHADER_STORAGE);
                if (dynamicOffset) {
                    dynamicBufferCount++;
                    desc.emplace<DynamicBuffer>(type);
                } else {
                    desc.emplace<Buffer>(type);
                }
                break;
            }

            // 所有类型的采样器描述符
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
            case DescriptorType::SAMPLER_EXTERNAL:
                if (UTILS_UNLIKELY(gl.isES2())) {
                    // ES2 路径：使用 SamplerGLES2（在纹理上设置采样器参数）
                    desc.emplace<SamplerGLES2>();
                } else {
                    // ES 3.0+ 路径：检查是否需要各向异性过滤工作区
                    const bool anisotropyWorkaround =
                            gl.ext.EXT_texture_filter_anisotropic &&
                            gl.bugs.texture_filter_anisotropic_broken_on_sampler;
                    if (anisotropyWorkaround) {
                        // 某些驱动在采样器上设置各向异性过滤会失败，需要在纹理上设置
                        desc.emplace<SamplerWithAnisotropyWorkaround>();
                    } else {
                        // 正常路径：使用采样器对象
                        desc.emplace<Sampler>();
                    }
                }
                break;
            case DescriptorType::INPUT_ATTACHMENT:
                // 输入附件不需要描述符（在渲染通道中处理）
                break;
        }
    }
}

/**
 * 更新缓冲区描述符
 * 
 * 更新描述符堆中的缓冲区绑定（Uniform 缓冲区或存储缓冲区）。
 * 
 * @param gl OpenGLContext 引用（当前未使用）
 * @param binding 绑定索引
 * @param bo 缓冲区对象指针（可为 nullptr，表示解绑）
 * @param offset 缓冲区偏移量（字节）
 * @param size 缓冲区大小（字节）
 * 
 * 执行流程：
 * 1. 验证绑定索引有效性
 * 2. 使用 std::visit 访问描述符变体
 * 3. 根据描述符类型更新：
 *    - Buffer/DynamicBuffer：更新 ID、偏移和大小
 *    - BufferGLES2：更新缓冲区对象指针和偏移
 *    - 其他类型：记录错误（不应该发生）
 */
void GLDescriptorSet::update(OpenGLContext&,
        descriptor_binding_t binding, GLBufferObject* bo, size_t offset, size_t size) noexcept {
    assert_invariant(binding < descriptors.size());
    std::visit([=](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Buffer> || std::is_same_v<T, DynamicBuffer>) {
            // ES 3.0+ 缓冲区描述符
            assert_invariant(arg.target != 0);
            arg.id = bo ? bo->gl.id : 0;
            arg.offset = uint32_t(offset);
            arg.size = uint32_t(size);
            // 如果缓冲区 ID 为 0，大小和偏移也应该为 0
            assert_invariant(arg.id || (!arg.size && !offset));
        } else if constexpr (std::is_same_v<T, BufferGLES2>) {
            // ES2 缓冲区描述符
            arg.bo = bo;
            arg.offset = uint32_t(offset);
        } else {
            // 用户请求更新错误类型的描述符。这不应该发生，
            // 因为我们在 Filament 端已经检查过了
            LOG(ERROR) << "descriptor " << +binding << " is not a buffer";
        }
    }, descriptors[binding].desc);
}

/**
 * 更新采样器描述符
 * 
 * 更新描述符堆中的纹理和采样器绑定。
 * 
 * @param gl OpenGLContext 引用，用于获取采样器对象
 * @param handleAllocator Handle 分配器，用于获取纹理对象
 * @param binding 绑定索引
 * @param th 纹理句柄（可为空句柄，表示解绑）
 * @param params 采样器参数
 * 
 * 执行流程：
 * 1. 获取纹理对象（如果句柄非空）
 * 2. 验证绑定索引有效性
 * 3. 使用 std::visit 访问描述符变体
 * 4. 特殊处理：
 *    - 外部纹理：强制使用 CLAMP_TO_EDGE 环绕模式
 *    - 深度纹理：限制过滤模式（不能使用线性过滤）
 * 5. 根据描述符类型更新：
 *    - Sampler：获取采样器对象 ID
 *    - SamplerWithAnisotropyWorkaround：获取采样器对象 ID 并存储各向异性级别
 *    - SamplerGLES2：存储采样器参数（ES2 在绑定时才设置）
 */
void GLDescriptorSet::update(OpenGLContext& gl, HandleAllocatorGL& handleAllocator,
        descriptor_binding_t binding, TextureHandle th, SamplerParams params) noexcept {

    // 获取纹理对象（如果句柄非空）
    GLTexture* t = th ? handleAllocator.handle_cast<GLTexture*>(th) : nullptr;

    assert_invariant(binding < descriptors.size());
    std::visit([=, &gl](auto&& arg) mutable {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Sampler> ||
                      std::is_same_v<T, SamplerWithAnisotropyWorkaround> ||
                      std::is_same_v<T, SamplerGLES2>) {
            // 外部纹理特殊处理：强制使用 CLAMP_TO_EDGE 环绕模式
            if (UTILS_UNLIKELY(t && t->target == SamplerType::SAMPLER_EXTERNAL)) {
                // 根据 OES_EGL_image_external 规范：
                // "默认的 s 和 t 环绕模式是 CLAMP_TO_EDGE，将环绕模式设置为任何其他值都是 INVALID_ENUM 错误。"
                params.wrapS = SamplerWrapMode::CLAMP_TO_EDGE;
                params.wrapT = SamplerWrapMode::CLAMP_TO_EDGE;
                params.wrapR = SamplerWrapMode::CLAMP_TO_EDGE;
            }
            
            // GLES 3.x 规范禁止深度纹理进行过滤
            if (t && isDepthFormat(t->format)
                    && params.compareMode == SamplerCompareMode::NONE) {
                params.filterMag = SamplerMagFilter::NEAREST;
                switch (params.filterMin) {
                    case SamplerMinFilter::LINEAR:
                        params.filterMin = SamplerMinFilter::NEAREST;
                        break;
                    case SamplerMinFilter::LINEAR_MIPMAP_NEAREST:
                    case SamplerMinFilter::NEAREST_MIPMAP_LINEAR:
                    case SamplerMinFilter::LINEAR_MIPMAP_LINEAR:
                        params.filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST;
                        break;
                    default:
                        break;
                }
            }

            // 更新纹理句柄
            arg.handle = th;
            
            if constexpr (std::is_same_v<T, Sampler> ||
                          std::is_same_v<T, SamplerWithAnisotropyWorkaround>) {
                // ES 3.0+ 采样器描述符
                if constexpr (std::is_same_v<T, SamplerWithAnisotropyWorkaround>) {
                    // 存储各向异性级别（用于工作区）
                    arg.anisotropy = float(1u << params.anisotropyLog2);
                }
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
                // 获取或创建采样器对象
                arg.sampler = gl.getSampler(params);
#else
                (void)gl;
#endif
            } else {
                // ES2 采样器描述符：存储参数（在绑定时才设置）
                arg.params = params;
            }
        } else {
            // 用户请求更新错误类型的描述符。这不应该发生，
            // 因为我们在 Filament 端已经检查过了
            LOG(ERROR) << "descriptor " << +binding << " is not a texture";
        }
    }, descriptors[binding].desc);
}

/**
 * 更新纹理视图
 * 
 * 更新纹理的视图参数（baseLevel、maxLevel、swizzle）。
 * 当纹理有视图（View）时，需要同步视图参数。
 * 
 * @param gl OpenGLContext 引用，用于激活纹理单元
 * @param handleAllocator Handle 分配器，用于获取纹理引用对象
 * @param unit 纹理单元索引
 * @param t 纹理对象指针
 * 
 * 执行流程：
 * 1. 获取纹理引用对象（存储视图状态）
 * 2. 如果 baseLevel 或 maxLevel 不匹配，更新它们
 * 3. 如果 swizzle 不匹配，更新通道重映射
 * 
 * 性能考虑：
 * - 常见情况是我们没有 ref 句柄（只有当纹理曾经有 View 时才有）
 * - 即使有视图，也很少会频繁切换
 * - 不幸的是，我们必须在这里调用 activeTexture
 */
void GLDescriptorSet::updateTextureView(OpenGLContext& gl,
        HandleAllocatorGL& handleAllocator, GLuint unit, GLTexture const* t) noexcept {
    // 常见情况是我们没有 ref 句柄（只有当纹理曾经有 View 时才有）
    assert_invariant(t);
    assert_invariant(t->ref);
    GLTextureRef* const ref = handleAllocator.handle_cast<GLTextureRef*>(t->ref);
    
    // 如果 baseLevel 或 maxLevel 不匹配，更新它们
    if (UTILS_UNLIKELY((t->gl.baseLevel != ref->baseLevel || t->gl.maxLevel != ref->maxLevel))) {
        // 如果我们有视图，仍然很少会频繁切换
        // 处理重置到原始纹理的情况
        GLint baseLevel = GLint(t->gl.baseLevel); // NOLINT(*-signed-char-misuse)
        GLint maxLevel = GLint(t->gl.maxLevel); // NOLINT(*-signed-char-misuse)
        if (baseLevel > maxLevel) {
            baseLevel = 0;
            maxLevel = 1000; // 根据 OpenGL 规范
        }
        // 不幸的是，我们必须在这里调用 activeTexture
        gl.activeTexture(unit);
        glTexParameteri(t->gl.target, GL_TEXTURE_BASE_LEVEL, baseLevel);
        glTexParameteri(t->gl.target, GL_TEXTURE_MAX_LEVEL,  maxLevel);
        ref->baseLevel = t->gl.baseLevel;
        ref->maxLevel = t->gl.maxLevel;
    }
    
    // 如果 swizzle 不匹配，更新通道重映射
    if (UTILS_UNLIKELY(t->gl.swizzle != ref->swizzle)) {
        using namespace GLUtils;
        gl.activeTexture(unit);
#if !defined(__EMSCRIPTEN__)  && !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
        glTexParameteri(t->gl.target, GL_TEXTURE_SWIZZLE_R, (GLint)getSwizzleChannel(t->gl.swizzle[0]));
        glTexParameteri(t->gl.target, GL_TEXTURE_SWIZZLE_G, (GLint)getSwizzleChannel(t->gl.swizzle[1]));
        glTexParameteri(t->gl.target, GL_TEXTURE_SWIZZLE_B, (GLint)getSwizzleChannel(t->gl.swizzle[2]));
        glTexParameteri(t->gl.target, GL_TEXTURE_SWIZZLE_A, (GLint)getSwizzleChannel(t->gl.swizzle[3]));
#endif
        ref->swizzle = t->gl.swizzle;
    }
}

/**
 * 绑定描述符堆
 * 
 * 概念上将描述符堆绑定到命令缓冲区。
 * 实际执行所有描述符的 OpenGL 绑定操作。
 * 
 * @param gl OpenGLContext 引用，用于绑定缓冲区和纹理
 * @param handleAllocator Handle 分配器，用于获取资源对象
 * @param p OpenGLProgram 引用，用于获取绑定点和纹理单元
 * @param set 描述符堆索引
 * @param offsets 动态偏移数组（用于动态缓冲区）
 * @param offsetsOnly 是否仅更新动态偏移（true 时只绑定动态缓冲区）
 * 
 * 执行流程：
 * 1. 获取程序的活动描述符绑定
 * 2. 如果 offsetsOnly，只处理动态缓冲区
 * 3. 遍历活动绑定，根据描述符类型执行绑定：
 *    - Buffer：使用静态偏移绑定缓冲区范围
 *    - DynamicBuffer：使用静态偏移 + 动态偏移绑定缓冲区范围
 *    - BufferGLES2：更新 Uniform（ES2 模拟 UBO）
 *    - Sampler：绑定纹理和采样器对象
 *    - SamplerWithAnisotropyWorkaround：绑定纹理和采样器，并在纹理上设置各向异性
 *    - SamplerGLES2：绑定纹理并在纹理上设置采样器参数（ES2）
 * 4. 处理纹理视图（baseLevel、maxLevel、swizzle）
 * 
 * 性能优化：
 * - 只绑定程序实际使用的描述符
 * - 支持仅更新动态偏移（offsetsOnly 模式），避免重复绑定静态资源
 */
void GLDescriptorSet::bind(
        OpenGLContext& gl,
        HandleAllocatorGL& handleAllocator,
        OpenGLProgram const& p,
        descriptor_set_t set, uint32_t const* offsets, bool offsetsOnly) const noexcept {
    // TODO: 检查 offsets 的大小是否正确
    size_t dynamicOffsetIndex = 0;

    // 获取程序的活动描述符绑定
    utils::bitset64 activeDescriptorBindings = p.getActiveDescriptors(set);
    
    // 如果只更新偏移，只处理动态缓冲区
    if (offsetsOnly) {
        activeDescriptorBindings &= dynamicBuffers;
    }

    // 只遍历此程序的活动索引
    activeDescriptorBindings.forEachSetBit(
            [this,&gl, &handleAllocator, &p, set, offsets, &dynamicOffsetIndex]
            (size_t binding) {

        // 如果我们试图设置程序中不存在的描述符，这里会失败。
        // 换句话说，程序布局与此描述符堆不匹配。
        assert_invariant(binding < descriptors.size());

        auto const& entry = descriptors[binding];
        std::visit(
                [&gl, &handleAllocator, &p, &dynamicOffsetIndex, set, binding, offsets]
                (auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Buffer>) {
                // 静态偏移缓冲区：使用静态偏移绑定缓冲区范围
                GLuint const bindingPoint = p.getBufferBinding(set, binding);
                GLintptr const offset = arg.offset;
                assert_invariant(arg.id || (!arg.size && !offset));
                gl.bindBufferRange(arg.target, bindingPoint, arg.id, offset, arg.size);
            } else if constexpr (std::is_same_v<T, DynamicBuffer>) {
                // 动态偏移缓冲区：使用静态偏移 + 动态偏移绑定缓冲区范围
                GLuint const bindingPoint = p.getBufferBinding(set, binding);
                GLintptr const offset = arg.offset + offsets[dynamicOffsetIndex++];
                assert_invariant(arg.id || (!arg.size && !offset));
                gl.bindBufferRange(arg.target, bindingPoint, arg.id, offset, arg.size);
            } else if constexpr (std::is_same_v<T, BufferGLES2>) {
                // ES2 缓冲区：更新 Uniform（模拟 UBO）
                GLuint const bindingPoint = p.getBufferBinding(set, binding);
                GLintptr offset = arg.offset;
                if (arg.dynamicOffset) {
                    offset += offsets[dynamicOffsetIndex++];
                }
                if (arg.bo) {
                    p.updateUniforms(bindingPoint, arg.bo->gl.id, arg.bo->gl.buffer, arg.bo->age, offset);
                }
            } else if constexpr (std::is_same_v<T, Sampler>) {
                // 正常采样器：绑定纹理和采样器对象
                GLuint const unit = p.getTextureUnit(set, binding);
                if (arg.handle) {
                    GLTexture const* const t = handleAllocator.handle_cast<GLTexture*>(arg.handle);
                    gl.bindTexture(unit, t->gl.target, t->gl.id, t->gl.external);
                    gl.bindSampler(unit, arg.sampler);
                    // 如果纹理有视图，更新视图参数
                    if (UTILS_UNLIKELY(t->ref)) {
                        updateTextureView(gl, handleAllocator, unit, t);
                    }
                } else {
                    gl.unbindTextureUnit(unit);
                }
            } else if constexpr (std::is_same_v<T, SamplerWithAnisotropyWorkaround>) {
                // 各向异性过滤工作区：绑定纹理和采样器，并在纹理上设置各向异性
                GLuint const unit = p.getTextureUnit(set, binding);
                if (arg.handle) {
                    GLTexture const* const t = handleAllocator.handle_cast<GLTexture*>(arg.handle);
                    gl.bindTexture(unit, t->gl.target, t->gl.id, t->gl.external);
                    gl.bindSampler(unit, arg.sampler);
                    // 如果纹理有视图，更新视图参数
                    if (UTILS_UNLIKELY(t->ref)) {
                        updateTextureView(gl, handleAllocator, unit, t);
                    }
#if defined(GL_EXT_texture_filter_anisotropic)
                    // 驱动声称支持各向异性过滤，但在采样器上设置时会失败，
                    // 我们必须在纹理上设置它。
                    glTexParameterf(t->gl.target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            std::min(gl.gets.max_anisotropy, float(arg.anisotropy)));
#endif
                } else {
                    gl.unbindTextureUnit(unit);
                }
            } else if constexpr (std::is_same_v<T, SamplerGLES2>) {
                // ES2 采样器：在纹理上设置采样器参数（ES2 不支持采样器对象）
                GLuint const unit = p.getTextureUnit(set, binding);
                if (arg.handle) {
                    GLTexture const* const t = handleAllocator.handle_cast<GLTexture*>(arg.handle);
                    gl.bindTexture(unit, t->gl.target, t->gl.id, t->gl.external);
                    SamplerParams const params = arg.params;
                    // 在纹理上设置采样器参数
                    glTexParameteri(t->gl.target, GL_TEXTURE_MIN_FILTER,
                            (GLint)GLUtils::getTextureFilter(params.filterMin));
                    glTexParameteri(t->gl.target, GL_TEXTURE_MAG_FILTER,
                            (GLint)GLUtils::getTextureFilter(params.filterMag));
                    glTexParameteri(t->gl.target, GL_TEXTURE_WRAP_S,
                            (GLint)GLUtils::getWrapMode(params.wrapS));
                    glTexParameteri(t->gl.target, GL_TEXTURE_WRAP_T,
                            (GLint)GLUtils::getWrapMode(params.wrapT));
#if defined(GL_EXT_texture_filter_anisotropic)
                    glTexParameterf(t->gl.target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                            std::min(gl.gets.max_anisotropy, arg.anisotropy));
#endif
                } else {
                    gl.unbindTextureUnit(unit);
                }
            }
        }, entry.desc);
    });
    CHECK_GL_ERROR()
}

/**
 * 验证描述符堆
 * 
 * 验证描述符堆布局是否与管线布局匹配。
 * 
 * @param allocator Handle 分配器，用于获取布局对象
 * @param pipelineLayout 管线布局句柄
 * 
 * 验证内容：
 * - 绑定类型（type）
 * - 阶段标志（stageFlags）
 * - 绑定索引（binding）
 * - 标志（flags）
 * - 数量（count）
 * 
 * 注意：
 * - 如果布局句柄相同，跳过验证（假设匹配）
 * - 如果布局句柄不同，比较所有绑定以确保匹配
 */
void GLDescriptorSet::validate(HandleAllocatorGL& allocator,
        DescriptorSetLayoutHandle pipelineLayout) const {

    // 如果布局句柄相同，跳过验证（假设匹配）
    if (UTILS_UNLIKELY(dslh != pipelineLayout)) {
        auto* const dsl = allocator.handle_cast < GLDescriptorSetLayout const * > (dslh);
        auto* const cur = allocator.handle_cast < GLDescriptorSetLayout const * > (pipelineLayout);

        UTILS_UNUSED_IN_RELEASE
        // 比较两个布局的所有绑定
        bool const pipelineLayoutMatchesDescriptorSetLayout = std::equal(
                dsl->bindings.begin(), dsl->bindings.end(),
                cur->bindings.begin(),
                [](DescriptorSetLayoutBinding const& lhs,
                        DescriptorSetLayoutBinding const& rhs) {
                    // 验证所有字段是否匹配
                    return lhs.type == rhs.type &&
                           lhs.stageFlags == rhs.stageFlags &&
                           lhs.binding == rhs.binding &&
                           lhs.flags == rhs.flags &&
                           lhs.count == rhs.count;
                });

        // 如果布局不匹配，断言失败
        assert_invariant(pipelineLayoutMatchesDescriptorSetLayout);
    }
}

} // namespace filament::backend
