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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLUTILS_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLUTILS_H

// Utils 工具库
#include <utils/debug.h>    // 调试工具
#include <utils/ostream.h>  // 输出流

// 后端枚举
#include <backend/DriverEnums.h>

// 标准库
#include <string_view>      // 字符串视图
#include <unordered_set>    // 无序集合

#include <stddef.h>         // 标准定义
#include <stdint.h>         // 标准整数类型

// OpenGL 头文件
#include "gl_headers.h"

/**
 * OpenGL 工具命名空间
 * 
 * 提供 OpenGL 相关的工具函数，包括：
 * 1. 错误检查和报告
 * 2. Filament 枚举到 OpenGL 枚举的转换
 * 3. 扩展字符串解析
 */
namespace filament::backend::GLUtils {

/**
 * 获取 OpenGL 错误字符串
 * 
 * 将 OpenGL 错误代码转换为可读的字符串。
 * 
 * @param error OpenGL 错误代码
 * @return 错误字符串视图
 */
std::string_view getGLErrorString(GLenum error) noexcept;

/**
 * 检查 OpenGL 错误（非致命）
 * 
 * 检查是否有 OpenGL 错误，如果有则记录日志但不中断执行。
 * 
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 * @return OpenGL 错误代码（GL_NO_ERROR 表示无错误）
 */
GLenum checkGLError(const char* function, size_t line) noexcept;

/**
 * 断言 OpenGL 错误（致命）
 * 
 * 检查是否有 OpenGL 错误，如果有则记录日志并触发调试陷阱。
 * 
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 */
void assertGLError(const char* function, size_t line) noexcept;

/**
 * 获取帧缓冲区状态字符串
 * 
 * 将帧缓冲区状态代码转换为可读的字符串。
 * 
 * @param err 帧缓冲区状态代码
 * @return 状态字符串视图
 */
std::string_view getFramebufferStatusString(GLenum err) noexcept;

/**
 * 检查帧缓冲区状态（非致命）
 * 
 * 检查帧缓冲区状态，如果不完整则记录日志但不中断执行。
 * 
 * @param target 帧缓冲区目标（GL_FRAMEBUFFER、GL_DRAW_FRAMEBUFFER 等）
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 * @return 帧缓冲区状态代码
 */
GLenum checkFramebufferStatus(GLenum target, const char* function, size_t line) noexcept;

/**
 * 断言帧缓冲区状态（致命）
 * 
 * 检查帧缓冲区状态，如果不完整则记录日志并触发调试陷阱。
 * 
 * @param target 帧缓冲区目标
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 */
void assertFramebufferStatus(GLenum target, const char* function, size_t line) noexcept;

/**
 * OpenGL 错误检查宏
 * 
 * 在调试模式下检查 OpenGL 错误和帧缓冲区状态。
 * 在发布模式下这些宏为空操作。
 */
#ifdef NDEBUG
#   define CHECK_GL_ERROR()                    // 发布模式：空操作
#   define CHECK_GL_ERROR_NON_FATAL()          // 发布模式：空操作
#   define CHECK_GL_FRAMEBUFFER_STATUS(target)  // 发布模式：空操作
#else
#   define CHECK_GL_ERROR() { GLUtils::assertGLError(__func__, __LINE__); }  // 调试模式：断言错误（致命）
#   define CHECK_GL_ERROR_NON_FATAL() { GLUtils::checkGLError(__func__, __LINE__); }  // 调试模式：检查错误（非致命）
#   define CHECK_GL_FRAMEBUFFER_STATUS(target) { GLUtils::checkFramebufferStatus( target, __func__, __LINE__); }  // 调试模式：检查帧缓冲区状态
#endif

/**
 * 获取元素类型的组件数量
 * 
 * 返回指定元素类型的组件数量（1、2、3 或 4）。
 * 
 * @param type 元素类型
 * @return 组件数量（1-4）
 */
constexpr GLuint getComponentCount(ElementType const type) noexcept {
    using ElementType = ElementType;
    switch (type) {
        // 单组件类型
        case ElementType::BYTE:
        case ElementType::UBYTE:
        case ElementType::SHORT:
        case ElementType::USHORT:
        case ElementType::INT:
        case ElementType::UINT:
        case ElementType::FLOAT:
        case ElementType::HALF:
            return 1;
        // 双组件类型
        case ElementType::FLOAT2:
        case ElementType::HALF2:
        case ElementType::BYTE2:
        case ElementType::UBYTE2:
        case ElementType::SHORT2:
        case ElementType::USHORT2:
            return 2;
        // 三组件类型
        case ElementType::FLOAT3:
        case ElementType::HALF3:
        case ElementType::BYTE3:
        case ElementType::UBYTE3:
        case ElementType::SHORT3:
        case ElementType::USHORT3:
            return 3;
        // 四组件类型
        case ElementType::FLOAT4:
        case ElementType::HALF4:
        case ElementType::BYTE4:
        case ElementType::UBYTE4:
        case ElementType::SHORT4:
        case ElementType::USHORT4:
            return 4;
    }
    // 不应该发生
    return 1;
}

// ------------------------------------------------------------------------------------------------
// Filament 枚举到 GLenum 的转换函数
// ------------------------------------------------------------------------------------------------

/**
 * 获取附件位字段
 * 
 * 将 TargetBufferFlags 转换为 OpenGL 清除掩码位字段。
 * 
 * @param flags 目标缓冲区标志
 * @return OpenGL 清除掩码位字段（GL_COLOR_BUFFER_BIT、GL_DEPTH_BUFFER_BIT、GL_STENCIL_BUFFER_BIT 的组合）
 */
constexpr GLbitfield getAttachmentBitfield(TargetBufferFlags const flags) noexcept {
    GLbitfield mask = 0;
    if (any(flags & TargetBufferFlags::COLOR_ALL)) {
        mask |= GLbitfield(GL_COLOR_BUFFER_BIT);
    }
    if (any(flags & TargetBufferFlags::DEPTH)) {
        mask |= GLbitfield(GL_DEPTH_BUFFER_BIT);
    }
    if (any(flags & TargetBufferFlags::STENCIL)) {
        mask |= GLbitfield(GL_STENCIL_BUFFER_BIT);
    }
    return mask;
}

/**
 * 获取缓冲区使用方式
 * 
 * 将 BufferUsage 转换为 OpenGL 缓冲区使用方式。
 * 
 * @param usage 缓冲区使用方式
 * @return OpenGL 缓冲区使用方式（GL_STATIC_DRAW 或 GL_DYNAMIC_DRAW）
 */
constexpr GLenum getBufferUsage(BufferUsage const usage) noexcept {
    switch (usage) {
        case BufferUsage::STATIC:
            return GL_STATIC_DRAW;
        default:
            return GL_DYNAMIC_DRAW;
    }
}

/**
 * 获取缓冲区绑定类型
 * 
 * 将 BufferObjectBinding 转换为 OpenGL 缓冲区目标。
 * 
 * @param bindingType 缓冲区对象绑定类型
 * @return OpenGL 缓冲区目标（GL_ARRAY_BUFFER、GL_UNIFORM_BUFFER、GL_SHADER_STORAGE_BUFFER 等）
 */
constexpr GLenum getBufferBindingType(BufferObjectBinding const bindingType) noexcept {
    switch (bindingType) {
        case BufferObjectBinding::VERTEX:
            return GL_ARRAY_BUFFER;
        case BufferObjectBinding::UNIFORM:
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            return GL_UNIFORM_BUFFER;
#else
            utils::panic(__func__, __FILE__, __LINE__, "UNIFORM not supported");
            return 0x8A11;
#endif
        case BufferObjectBinding::SHADER_STORAGE:
#ifdef BACKEND_OPENGL_LEVEL_GLES31
            return GL_SHADER_STORAGE_BUFFER;
#else
            utils::panic(__func__, __FILE__, __LINE__, "SHADER_STORAGE not supported");
            return 0x90D2; // 仅为了返回某个值
#endif
    }
    // 不应该发生
    return GL_ARRAY_BUFFER;
}

/**
 * 获取归一化标志
 * 
 * 将布尔值转换为 OpenGL 归一化标志。
 * 
 * @param normalized 是否归一化
 * @return OpenGL 归一化标志（GL_TRUE 或 GL_FALSE）
 */
constexpr GLboolean getNormalization(bool const normalized) noexcept {
    return GLboolean(normalized ? GL_TRUE : GL_FALSE);
}

/**
 * 获取组件类型
 * 
 * 将 ElementType 转换为 OpenGL 组件类型。
 * 
 * @param type 元素类型
 * @return OpenGL 组件类型（GL_BYTE、GL_UNSIGNED_BYTE、GL_SHORT、GL_FLOAT 等）
 */
constexpr GLenum getComponentType(ElementType const type) noexcept {
    using ElementType = ElementType;
    switch (type) {
        case ElementType::BYTE:
        case ElementType::BYTE2:
        case ElementType::BYTE3:
        case ElementType::BYTE4:
            return GL_BYTE;
        case ElementType::UBYTE:
        case ElementType::UBYTE2:
        case ElementType::UBYTE3:
        case ElementType::UBYTE4:
            return GL_UNSIGNED_BYTE;
        case ElementType::SHORT:
        case ElementType::SHORT2:
        case ElementType::SHORT3:
        case ElementType::SHORT4:
            return GL_SHORT;
        case ElementType::USHORT:
        case ElementType::USHORT2:
        case ElementType::USHORT3:
        case ElementType::USHORT4:
            return GL_UNSIGNED_SHORT;
        case ElementType::INT:
            return GL_INT;
        case ElementType::UINT:
            return GL_UNSIGNED_INT;
        case ElementType::FLOAT:
        case ElementType::FLOAT2:
        case ElementType::FLOAT3:
        case ElementType::FLOAT4:
            return GL_FLOAT;
        case ElementType::HALF:
        case ElementType::HALF2:
        case ElementType::HALF3:
        case ElementType::HALF4:
            // 在 ES2 上不应该到达这里
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            return GL_HALF_FLOAT;
#else
            return GL_HALF_FLOAT_OES;
#endif
    }
    // 不应该发生
    return GL_INT;
}

/**
 * 获取纹理目标（非外部纹理）
 * 
 * 将 SamplerType 转换为 OpenGL 纹理目标。
 * 注意：此函数不处理外部纹理（SAMPLER_EXTERNAL）。
 * 
 * @param target 采样器类型
 * @return OpenGL 纹理目标（GL_TEXTURE_2D、GL_TEXTURE_3D、GL_TEXTURE_CUBE_MAP 等）
 */
constexpr GLenum getTextureTargetNotExternal(SamplerType const target) noexcept {
    switch (target) {
        case SamplerType::SAMPLER_2D:
            return GL_TEXTURE_2D;
        case SamplerType::SAMPLER_3D:
            return GL_TEXTURE_3D;
        case SamplerType::SAMPLER_2D_ARRAY:
            return GL_TEXTURE_2D_ARRAY;
        case SamplerType::SAMPLER_CUBEMAP:
            return GL_TEXTURE_CUBE_MAP;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return GL_TEXTURE_CUBE_MAP_ARRAY;
        case SamplerType::SAMPLER_EXTERNAL:
            // 不应该到达这里
            return GL_TEXTURE_2D;
    }
    // 不应该发生
    return GL_TEXTURE_2D;
}

/**
 * 获取立方体贴图面目标
 * 
 * 将立方体贴图层索引（0-5）转换为对应的 OpenGL 纹理目标。
 * 
 * @param layer 立方体贴图层索引（0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z）
 * @return OpenGL 立方体贴图面目标（GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer）
 */
constexpr GLenum getCubemapTarget(uint16_t const layer) noexcept {
    assert_invariant(layer <= 5);
    return GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer;
}

/**
 * 获取纹理环绕模式
 * 
 * 将 SamplerWrapMode 转换为 OpenGL 纹理环绕模式。
 * 
 * @param mode 采样器环绕模式
 * @return OpenGL 纹理环绕模式（GL_REPEAT、GL_CLAMP_TO_EDGE、GL_MIRRORED_REPEAT）
 */
constexpr GLenum getWrapMode(SamplerWrapMode const mode) noexcept {
    using SamplerWrapMode = SamplerWrapMode;
    switch (mode) {
        case SamplerWrapMode::REPEAT:
            return GL_REPEAT;
        case SamplerWrapMode::CLAMP_TO_EDGE:
            return GL_CLAMP_TO_EDGE;
        case SamplerWrapMode::MIRRORED_REPEAT:
            return GL_MIRRORED_REPEAT;
    }
    // 不应该发生
    return GL_CLAMP_TO_EDGE;
}

/**
 * 获取纹理最小过滤模式
 * 
 * 将 SamplerMinFilter 转换为 OpenGL 纹理最小过滤模式。
 * 
 * @param filter 采样器最小过滤器
 * @return OpenGL 纹理过滤模式（GL_NEAREST、GL_LINEAR、GL_NEAREST_MIPMAP_NEAREST 等）
 */
constexpr GLenum getTextureFilter(SamplerMinFilter filter) noexcept {
    using SamplerMinFilter = SamplerMinFilter;
    switch (filter) {
        case SamplerMinFilter::NEAREST:
        case SamplerMinFilter::LINEAR:
            return GL_NEAREST + GLenum(filter);
        case SamplerMinFilter::NEAREST_MIPMAP_NEAREST:
        case SamplerMinFilter::LINEAR_MIPMAP_NEAREST:
        case SamplerMinFilter::NEAREST_MIPMAP_LINEAR:
        case SamplerMinFilter::LINEAR_MIPMAP_LINEAR:
            return GL_NEAREST_MIPMAP_NEAREST
                   - GLenum(SamplerMinFilter::NEAREST_MIPMAP_NEAREST) + GLenum(filter);
    }
    // 不应该发生
    return GL_NEAREST;
}

/**
 * 获取纹理放大过滤模式
 * 
 * 将 SamplerMagFilter 转换为 OpenGL 纹理放大过滤模式。
 * 
 * @param filter 采样器放大过滤器
 * @return OpenGL 纹理过滤模式（GL_NEAREST 或 GL_LINEAR）
 */
constexpr GLenum getTextureFilter(SamplerMagFilter filter) noexcept {
    return GL_NEAREST + GLenum(filter);
}


/**
 * 获取混合方程模式
 * 
 * 将 BlendEquation 转换为 OpenGL 混合方程模式。
 * 
 * @param mode 混合方程
 * @return OpenGL 混合方程模式（GL_FUNC_ADD、GL_FUNC_SUBTRACT、GL_MIN 等）
 */
constexpr GLenum getBlendEquationMode(BlendEquation const mode) noexcept {
    using BlendEquation = BlendEquation;
    switch (mode) {
        case BlendEquation::ADD:               return GL_FUNC_ADD;
        case BlendEquation::SUBTRACT:          return GL_FUNC_SUBTRACT;
        case BlendEquation::REVERSE_SUBTRACT:  return GL_FUNC_REVERSE_SUBTRACT;
        case BlendEquation::MIN:               return GL_MIN;
        case BlendEquation::MAX:               return GL_MAX;
    }
    // 不应该发生
    return GL_FUNC_ADD;
}

/**
 * 获取混合函数模式
 * 
 * 将 BlendFunction 转换为 OpenGL 混合函数模式。
 * 
 * @param mode 混合函数
 * @return OpenGL 混合函数模式（GL_ZERO、GL_ONE、GL_SRC_ALPHA 等）
 */
constexpr GLenum getBlendFunctionMode(BlendFunction const mode) noexcept {
    using BlendFunction = BlendFunction;
    switch (mode) {
        case BlendFunction::ZERO:                  return GL_ZERO;
        case BlendFunction::ONE:                   return GL_ONE;
        case BlendFunction::SRC_COLOR:             return GL_SRC_COLOR;
        case BlendFunction::ONE_MINUS_SRC_COLOR:   return GL_ONE_MINUS_SRC_COLOR;
        case BlendFunction::DST_COLOR:             return GL_DST_COLOR;
        case BlendFunction::ONE_MINUS_DST_COLOR:   return GL_ONE_MINUS_DST_COLOR;
        case BlendFunction::SRC_ALPHA:             return GL_SRC_ALPHA;
        case BlendFunction::ONE_MINUS_SRC_ALPHA:   return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFunction::DST_ALPHA:             return GL_DST_ALPHA;
        case BlendFunction::ONE_MINUS_DST_ALPHA:   return GL_ONE_MINUS_DST_ALPHA;
        case BlendFunction::SRC_ALPHA_SATURATE:    return GL_SRC_ALPHA_SATURATE;
    }
    // 不应该发生
    return GL_ONE;
}

/**
 * 获取比较函数
 * 
 * 将 SamplerCompareFunc 转换为 OpenGL 比较函数。
 * 用于深度测试、模板测试和阴影贴图比较。
 * 
 * @param func 采样器比较函数
 * @return OpenGL 比较函数（GL_LEQUAL、GL_GEQUAL、GL_LESS 等）
 */
constexpr GLenum getCompareFunc(SamplerCompareFunc const func) noexcept {
    switch (func) {
        case SamplerCompareFunc::LE:    return GL_LEQUAL;   // <=
        case SamplerCompareFunc::GE:    return GL_GEQUAL;   // >=
        case SamplerCompareFunc::L:     return GL_LESS;     // <
        case SamplerCompareFunc::G:     return GL_GREATER;  // >
        case SamplerCompareFunc::E:     return GL_EQUAL;    // ==
        case SamplerCompareFunc::NE:    return GL_NOTEQUAL; // !=
        case SamplerCompareFunc::A:     return GL_ALWAYS;   // 总是通过
        case SamplerCompareFunc::N:     return GL_NEVER;    // 永远不通过
    }
    // 不应该发生
    return GL_LEQUAL;
}

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
/**
 * 获取纹理比较模式
 * 
 * 将 SamplerCompareMode 转换为 OpenGL 纹理比较模式。
 * 用于阴影贴图（Shadow Mapping）。
 * 
 * @param mode 采样器比较模式
 * @return OpenGL 纹理比较模式（GL_NONE 或 GL_COMPARE_REF_TO_TEXTURE）
 */
constexpr GLenum getTextureCompareMode(SamplerCompareMode const mode) noexcept {
    return mode == SamplerCompareMode::NONE ?
           GL_NONE : GL_COMPARE_REF_TO_TEXTURE;
}

/**
 * 获取纹理比较函数
 * 
 * 将 SamplerCompareFunc 转换为 OpenGL 纹理比较函数。
 * 用于阴影贴图（Shadow Mapping）。
 * 
 * @param func 采样器比较函数
 * @return OpenGL 纹理比较函数
 */
constexpr GLenum getTextureCompareFunc(SamplerCompareFunc const func) noexcept {
    return getCompareFunc(func);
}
#endif

constexpr GLenum getDepthFunc(SamplerCompareFunc const func) noexcept {
    return getCompareFunc(func);
}

/**
 * 获取深度测试函数
 * 
 * 将 SamplerCompareFunc 转换为 OpenGL 深度测试函数。
 * 
 * @param func 采样器比较函数
 * @return OpenGL 深度测试函数
 */
constexpr GLenum getDepthFunc(SamplerCompareFunc const func) noexcept {
    return getCompareFunc(func);
}

/**
 * 获取模板测试函数
 * 
 * 将 SamplerCompareFunc 转换为 OpenGL 模板测试函数。
 * 
 * @param func 采样器比较函数
 * @return OpenGL 模板测试函数
 */
constexpr GLenum getStencilFunc(SamplerCompareFunc const func) noexcept {
    return getCompareFunc(func);
}

/**
 * 获取模板操作
 * 
 * 将 StencilOperation 转换为 OpenGL 模板操作。
 * 
 * @param op 模板操作
 * @return OpenGL 模板操作（GL_KEEP、GL_ZERO、GL_REPLACE、GL_INCR 等）
 */
constexpr GLenum getStencilOp(StencilOperation const op) noexcept {
    switch (op) {
        case StencilOperation::KEEP:        return GL_KEEP;
        case StencilOperation::ZERO:        return GL_ZERO;
        case StencilOperation::REPLACE:     return GL_REPLACE;
        case StencilOperation::INCR:        return GL_INCR;
        case StencilOperation::INCR_WRAP:   return GL_INCR_WRAP;
        case StencilOperation::DECR:        return GL_DECR;
        case StencilOperation::DECR_WRAP:   return GL_DECR_WRAP;
        case StencilOperation::INVERT:      return GL_INVERT;
    }
    // 不应该发生
    return GL_KEEP;
}

/**
 * 获取像素数据格式
 * 
 * 将 PixelDataFormat 转换为 OpenGL 像素数据格式。
 * 
 * @param format 像素数据格式
 * @return OpenGL 像素数据格式（GL_RGB、GL_RGBA、GL_DEPTH_COMPONENT 等）
 */
constexpr GLenum getFormat(PixelDataFormat const format) noexcept {
    using PixelDataFormat = PixelDataFormat;
    switch (format) {
        case PixelDataFormat::RGB:              return GL_RGB;
        case PixelDataFormat::RGBA:             return GL_RGBA;
        case PixelDataFormat::UNUSED:           return GL_RGBA; // should never happen (used to be rgbm)
        case PixelDataFormat::DEPTH_COMPONENT:  return GL_DEPTH_COMPONENT;
        case PixelDataFormat::ALPHA:            return GL_ALPHA;
        case PixelDataFormat::DEPTH_STENCIL:    return GL_DEPTH_STENCIL;
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        // when context is ES2 we should never end-up here
        case PixelDataFormat::R:                return GL_RED;
        case PixelDataFormat::R_INTEGER:        return GL_RED_INTEGER;
        case PixelDataFormat::RG:               return GL_RG;
        case PixelDataFormat::RG_INTEGER:       return GL_RG_INTEGER;
        case PixelDataFormat::RGB_INTEGER:      return GL_RGB_INTEGER;
        case PixelDataFormat::RGBA_INTEGER:     return GL_RGBA_INTEGER;
#else
        // silence compiler warning in ES2 headers mode
        default: return GL_NONE;
#endif
    }
    // should never happen
    return GL_RGBA;
}

/**
 * 获取像素数据类型
 * 
 * 将 PixelDataType 转换为 OpenGL 像素数据类型。
 * 
 * @param type 像素数据类型
 * @return OpenGL 像素数据类型（GL_UNSIGNED_BYTE、GL_FLOAT、GL_UNSIGNED_SHORT_5_6_5 等）
 */
constexpr GLenum getType(PixelDataType const type) noexcept {
    using PixelDataType = PixelDataType;
    switch (type) {
        case PixelDataType::UBYTE:                return GL_UNSIGNED_BYTE;
        case PixelDataType::BYTE:                 return GL_BYTE;
        case PixelDataType::USHORT:               return GL_UNSIGNED_SHORT;
        case PixelDataType::SHORT:                return GL_SHORT;
        case PixelDataType::UINT:                 return GL_UNSIGNED_INT;
        case PixelDataType::INT:                  return GL_INT;
        case PixelDataType::FLOAT:                return GL_FLOAT;
        case PixelDataType::USHORT_565:           return GL_UNSIGNED_SHORT_5_6_5;
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        // 当上下文是 ES2 时不应该到达这里
        case PixelDataType::HALF:                 return GL_HALF_FLOAT;
        case PixelDataType::UINT_10F_11F_11F_REV: return GL_UNSIGNED_INT_10F_11F_11F_REV;
        case PixelDataType::UINT_2_10_10_10_REV:  return GL_UNSIGNED_INT_2_10_10_10_REV;
        case PixelDataType::COMPRESSED:           return 0; // 不应该发生
#else
        // 在 ES2 头文件模式下静默编译器警告
        default: return GL_NONE;
#endif
    }
    // 不应该发生
    return GL_UNSIGNED_INT;
}

#if !defined(__EMSCRIPTEN__)  && !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
/**
 * 获取纹理通道重映射
 * 
 * 将 TextureSwizzle 转换为 OpenGL 纹理通道重映射值。
 * 用于纹理通道重映射（Texture Swizzle），允许重新映射纹理通道。
 * 
 * @param c 纹理通道重映射
 * @return OpenGL 纹理通道重映射值（GL_ZERO、GL_ONE、GL_RED、GL_GREEN、GL_BLUE、GL_ALPHA）
 */
constexpr GLenum getSwizzleChannel(TextureSwizzle const c) noexcept {
    using TextureSwizzle = TextureSwizzle;
    switch (c) {
        case TextureSwizzle::SUBSTITUTE_ZERO:   return GL_ZERO;
        case TextureSwizzle::SUBSTITUTE_ONE:    return GL_ONE;
        case TextureSwizzle::CHANNEL_0:         return GL_RED;
        case TextureSwizzle::CHANNEL_1:         return GL_GREEN;
        case TextureSwizzle::CHANNEL_2:         return GL_BLUE;
        case TextureSwizzle::CHANNEL_3:         return GL_ALPHA;
    }
    // 不应该发生
    return GL_RED;
}
#endif

/**
 * 获取剔除模式
 * 
 * 将 CullingMode 转换为 OpenGL 剔除模式。
 * 
 * @param mode 剔除模式
 * @return OpenGL 剔除模式（GL_FRONT、GL_BACK、GL_FRONT_AND_BACK）
 */
constexpr GLenum getCullingMode(CullingMode const mode) noexcept {
    switch (mode) {
        case CullingMode::NONE:
            // 不应该发生（NONE 应该在调用此函数前被过滤）
            return GL_FRONT_AND_BACK;
        case CullingMode::FRONT:
            return GL_FRONT;
        case CullingMode::BACK:
            return GL_BACK;
        case CullingMode::FRONT_AND_BACK:
            return GL_FRONT_AND_BACK;
    }
    // 不应该发生
    return GL_FRONT_AND_BACK;
}

/**
 * 纹理格式转换为格式和类型（ES2 支持）
 * 
 * 将 TextureFormat 转换为 ES2 支持的格式/类型对。
 * 用于 ES2 上下文中，因为 ES2 不支持所有内部格式。
 * 
 * @param format 纹理格式
 * @return 格式和类型对（format, type）
 */
constexpr std::pair<GLenum, GLenum> textureFormatToFormatAndType(
        TextureFormat const format) noexcept {
    switch (format) {
        case TextureFormat::R8:         return { 0x1909 /*GL_LUMINANCE*/, GL_UNSIGNED_BYTE };
        case TextureFormat::RGB8:       return { GL_RGB,                  GL_UNSIGNED_BYTE };
        case TextureFormat::SRGB8:      return { GL_RGB,                  GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8:      return { GL_RGBA,                 GL_UNSIGNED_BYTE };
        case TextureFormat::SRGB8_A8:   return { GL_RGBA,                 GL_UNSIGNED_BYTE };
        case TextureFormat::RGB565:     return { GL_RGB,                  GL_UNSIGNED_SHORT_5_6_5 };
        case TextureFormat::RGB5_A1:    return { GL_RGBA,                 GL_UNSIGNED_SHORT_5_5_5_1 };
        case TextureFormat::RGBA4:      return { GL_RGBA,                 GL_UNSIGNED_SHORT_4_4_4_4 };
        case TextureFormat::DEPTH16:    return { GL_DEPTH_COMPONENT,      GL_UNSIGNED_SHORT };
        case TextureFormat::DEPTH24:    return { GL_DEPTH_COMPONENT,      GL_UNSIGNED_INT };
        case TextureFormat::DEPTH24_STENCIL8:
                                        return { GL_DEPTH24_STENCIL8,     GL_UNSIGNED_INT_24_8 };
        default:                        return { GL_NONE,                 GL_NONE };
    }
}

/**
 * 获取内部格式
 * 
 * 将 TextureFormat 转换为 OpenGL 内部格式。
 * 
 * 注意：clang 在将此函数内联时会生成巨大的跳转表。
 * 因此我们不将其标记为 inline（仅 constexpr），这解决了问题。
 * 奇怪的是，当不内联时，clang 简单地生成数组查找。
 * 
 * @param format 纹理格式
 * @return OpenGL 内部格式（GL_RGB8、GL_RGBA8、GL_COMPRESSED_RGB8_ETC2 等）
 */
constexpr /* inline */ GLenum getInternalFormat(TextureFormat const format) noexcept {
    switch (format) {

        /* Formats supported by our ES2 implementations */

        // 8-bits per element
        case TextureFormat::STENCIL8:          return GL_STENCIL_INDEX8;

        // 16-bits per element
        case TextureFormat::RGB565:            return GL_RGB565;
        case TextureFormat::RGB5_A1:           return GL_RGB5_A1;
        case TextureFormat::RGBA4:             return GL_RGBA4;
        case TextureFormat::DEPTH16:           return GL_DEPTH_COMPONENT16;

        // 24-bits per element
        case TextureFormat::RGB8:              return GL_RGB8;
        case TextureFormat::DEPTH24:           return GL_DEPTH_COMPONENT24;

        // 32-bits per element
        case TextureFormat::RGBA8:             return GL_RGBA8;
        case TextureFormat::DEPTH24_STENCIL8:  return GL_DEPTH24_STENCIL8;

        /* Formats not supported by our ES2 implementations */

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        // 8-bits per element
        case TextureFormat::R8:                return GL_R8;
        case TextureFormat::R8_SNORM:          return GL_R8_SNORM;
        case TextureFormat::R8UI:              return GL_R8UI;
        case TextureFormat::R8I:               return GL_R8I;

        // 16-bits per element
        case TextureFormat::R16F:              return GL_R16F;
        case TextureFormat::R16UI:             return GL_R16UI;
        case TextureFormat::R16I:              return GL_R16I;
        case TextureFormat::RG8:               return GL_RG8;
        case TextureFormat::RG8_SNORM:         return GL_RG8_SNORM;
        case TextureFormat::RG8UI:             return GL_RG8UI;
        case TextureFormat::RG8I:              return GL_RG8I;

        // 24-bits per element
        case TextureFormat::SRGB8:             return GL_SRGB8;
        case TextureFormat::RGB8_SNORM:        return GL_RGB8_SNORM;
        case TextureFormat::RGB8UI:            return GL_RGB8UI;
        case TextureFormat::RGB8I:             return GL_RGB8I;

        // 32-bits per element
        case TextureFormat::R32F:              return GL_R32F;
        case TextureFormat::R32UI:             return GL_R32UI;
        case TextureFormat::R32I:              return GL_R32I;
        case TextureFormat::RG16F:             return GL_RG16F;
        case TextureFormat::RG16UI:            return GL_RG16UI;
        case TextureFormat::RG16I:             return GL_RG16I;
        case TextureFormat::R11F_G11F_B10F:    return GL_R11F_G11F_B10F;
        case TextureFormat::RGB9_E5:           return GL_RGB9_E5;
        case TextureFormat::SRGB8_A8:          return GL_SRGB8_ALPHA8;
        case TextureFormat::RGBA8_SNORM:       return GL_RGBA8_SNORM;
        case TextureFormat::RGB10_A2:          return GL_RGB10_A2;
        case TextureFormat::RGBA8UI:           return GL_RGBA8UI;
        case TextureFormat::RGBA8I:            return GL_RGBA8I;
        case TextureFormat::DEPTH32F:          return GL_DEPTH_COMPONENT32F;
        case TextureFormat::DEPTH32F_STENCIL8: return GL_DEPTH32F_STENCIL8;

        // 48-bits per element
        case TextureFormat::RGB16F:            return GL_RGB16F;
        case TextureFormat::RGB16UI:           return GL_RGB16UI;
        case TextureFormat::RGB16I:            return GL_RGB16I;

        // 64-bits per element
        case TextureFormat::RG32F:             return GL_RG32F;
        case TextureFormat::RG32UI:            return GL_RG32UI;
        case TextureFormat::RG32I:             return GL_RG32I;
        case TextureFormat::RGBA16F:           return GL_RGBA16F;
        case TextureFormat::RGBA16UI:          return GL_RGBA16UI;
        case TextureFormat::RGBA16I:           return GL_RGBA16I;

        // 96-bits per element
        case TextureFormat::RGB32F:            return GL_RGB32F;
        case TextureFormat::RGB32UI:           return GL_RGB32UI;
        case TextureFormat::RGB32I:            return GL_RGB32I;

        // 128-bits per element
        case TextureFormat::RGBA32F:           return GL_RGBA32F;
        case TextureFormat::RGBA32UI:          return GL_RGBA32UI;
        case TextureFormat::RGBA32I:           return GL_RGBA32I;
#else
        default:
            // this is just to squash the IDE warning about not having all cases when in
            // ES2 header mode.
            return 0;
#endif

        // compressed formats
#if defined(GL_ES_VERSION_3_0) || defined(BACKEND_OPENGL_VERSION_GL) || defined(GL_ARB_ES3_compatibility)
        case TextureFormat::EAC_R11:           return GL_COMPRESSED_R11_EAC;
        case TextureFormat::EAC_R11_SIGNED:    return GL_COMPRESSED_SIGNED_R11_EAC;
        case TextureFormat::EAC_RG11:          return GL_COMPRESSED_RG11_EAC;
        case TextureFormat::EAC_RG11_SIGNED:   return GL_COMPRESSED_SIGNED_RG11_EAC;
        case TextureFormat::ETC2_RGB8:         return GL_COMPRESSED_RGB8_ETC2;
        case TextureFormat::ETC2_SRGB8:        return GL_COMPRESSED_SRGB8_ETC2;
        case TextureFormat::ETC2_RGB8_A1:      return GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        case TextureFormat::ETC2_SRGB8_A1:     return GL_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        case TextureFormat::ETC2_EAC_RGBA8:    return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case TextureFormat::ETC2_EAC_SRGBA8:   return GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
#else
        case TextureFormat::EAC_R11:
        case TextureFormat::EAC_R11_SIGNED:
        case TextureFormat::EAC_RG11:
        case TextureFormat::EAC_RG11_SIGNED:
        case TextureFormat::ETC2_RGB8:
        case TextureFormat::ETC2_SRGB8:
        case TextureFormat::ETC2_RGB8_A1:
        case TextureFormat::ETC2_SRGB8_A1:
        case TextureFormat::ETC2_EAC_RGBA8:
        case TextureFormat::ETC2_EAC_SRGBA8:
            // this should not happen
            return 0;
#endif

#if defined(GL_EXT_texture_compression_s3tc)
        case TextureFormat::DXT1_RGB:          return GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        case TextureFormat::DXT1_RGBA:         return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case TextureFormat::DXT3_RGBA:         return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        case TextureFormat::DXT5_RGBA:         return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
#else
        case TextureFormat::DXT1_RGB:
        case TextureFormat::DXT1_RGBA:
        case TextureFormat::DXT3_RGBA:
        case TextureFormat::DXT5_RGBA:
            // this should not happen
            return 0;
#endif

#if defined(GL_EXT_texture_sRGB) || defined(GL_EXT_texture_compression_s3tc_srgb)
        case TextureFormat::DXT1_SRGB:         return GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;
        case TextureFormat::DXT1_SRGBA:        return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
        case TextureFormat::DXT3_SRGBA:        return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT;
        case TextureFormat::DXT5_SRGBA:        return GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT;
#else
        case TextureFormat::DXT1_SRGB:
        case TextureFormat::DXT1_SRGBA:
        case TextureFormat::DXT3_SRGBA:
        case TextureFormat::DXT5_SRGBA:
            // this should not happen
            return 0;
#endif

#if defined(GL_EXT_texture_compression_rgtc)
        case TextureFormat::RED_RGTC1:              return GL_COMPRESSED_RED_RGTC1_EXT;
        case TextureFormat::SIGNED_RED_RGTC1:       return GL_COMPRESSED_SIGNED_RED_RGTC1_EXT;
        case TextureFormat::RED_GREEN_RGTC2:        return GL_COMPRESSED_RED_GREEN_RGTC2_EXT;
        case TextureFormat::SIGNED_RED_GREEN_RGTC2: return GL_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT;
#else
        case TextureFormat::RED_RGTC1:
        case TextureFormat::SIGNED_RED_RGTC1:
        case TextureFormat::RED_GREEN_RGTC2:
        case TextureFormat::SIGNED_RED_GREEN_RGTC2:
            // this should not happen
            return 0;
#endif

#if defined(GL_EXT_texture_compression_bptc)
        case TextureFormat::RGB_BPTC_SIGNED_FLOAT:      return GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT;
        case TextureFormat::RGB_BPTC_UNSIGNED_FLOAT:    return GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT;
        case TextureFormat::RGBA_BPTC_UNORM:            return GL_COMPRESSED_RGBA_BPTC_UNORM_EXT;
        case TextureFormat::SRGB_ALPHA_BPTC_UNORM:      return GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT;
#else
        case TextureFormat::RGB_BPTC_SIGNED_FLOAT:
        case TextureFormat::RGB_BPTC_UNSIGNED_FLOAT:
        case TextureFormat::RGBA_BPTC_UNORM:
        case TextureFormat::SRGB_ALPHA_BPTC_UNORM:
            // this should not happen
            return 0;
#endif

#if defined(GL_KHR_texture_compression_astc_hdr)
        case TextureFormat::RGBA_ASTC_4x4:     return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case TextureFormat::RGBA_ASTC_5x4:     return GL_COMPRESSED_RGBA_ASTC_5x4_KHR;
        case TextureFormat::RGBA_ASTC_5x5:     return GL_COMPRESSED_RGBA_ASTC_5x5_KHR;
        case TextureFormat::RGBA_ASTC_6x5:     return GL_COMPRESSED_RGBA_ASTC_6x5_KHR;
        case TextureFormat::RGBA_ASTC_6x6:     return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        case TextureFormat::RGBA_ASTC_8x5:     return GL_COMPRESSED_RGBA_ASTC_8x5_KHR;
        case TextureFormat::RGBA_ASTC_8x6:     return GL_COMPRESSED_RGBA_ASTC_8x6_KHR;
        case TextureFormat::RGBA_ASTC_8x8:     return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        case TextureFormat::RGBA_ASTC_10x5:    return GL_COMPRESSED_RGBA_ASTC_10x5_KHR;
        case TextureFormat::RGBA_ASTC_10x6:    return GL_COMPRESSED_RGBA_ASTC_10x6_KHR;
        case TextureFormat::RGBA_ASTC_10x8:    return GL_COMPRESSED_RGBA_ASTC_10x8_KHR;
        case TextureFormat::RGBA_ASTC_10x10:   return GL_COMPRESSED_RGBA_ASTC_10x10_KHR;
        case TextureFormat::RGBA_ASTC_12x10:   return GL_COMPRESSED_RGBA_ASTC_12x10_KHR;
        case TextureFormat::RGBA_ASTC_12x12:   return GL_COMPRESSED_RGBA_ASTC_12x12_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_4x4:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x4:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x5:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x5:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x6:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x5:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x6:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x8:   return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x5:  return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x6:  return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x8:  return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x10: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x10: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR;
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x12: return GL_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR;
#else
        case TextureFormat::RGBA_ASTC_4x4:
        case TextureFormat::RGBA_ASTC_5x4:
        case TextureFormat::RGBA_ASTC_5x5:
        case TextureFormat::RGBA_ASTC_6x5:
        case TextureFormat::RGBA_ASTC_6x6:
        case TextureFormat::RGBA_ASTC_8x5:
        case TextureFormat::RGBA_ASTC_8x6:
        case TextureFormat::RGBA_ASTC_8x8:
        case TextureFormat::RGBA_ASTC_10x5:
        case TextureFormat::RGBA_ASTC_10x6:
        case TextureFormat::RGBA_ASTC_10x8:
        case TextureFormat::RGBA_ASTC_10x10:
        case TextureFormat::RGBA_ASTC_12x10:
        case TextureFormat::RGBA_ASTC_12x12:
        case TextureFormat::SRGB8_ALPHA8_ASTC_4x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x4:
        case TextureFormat::SRGB8_ALPHA8_ASTC_5x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_6x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_8x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x5:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x6:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x8:
        case TextureFormat::SRGB8_ALPHA8_ASTC_10x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x10:
        case TextureFormat::SRGB8_ALPHA8_ASTC_12x12:
            // this should not happen
            return 0;
#endif
        case TextureFormat::UNUSED:
            return 0;
    }
}

/**
 * 无序字符串集合
 * 
 * 扩展 std::unordered_set<std::string_view>，提供便捷的 has() 方法。
 * 用于存储和查询 OpenGL 扩展名称。
 */
class unordered_string_set : public std::unordered_set<std::string_view> {
public:
    /**
     * 检查是否包含字符串
     * 
     * @param str 要查找的字符串视图
     * @return 如果包含返回 true，否则返回 false
     */
    bool has(std::string_view str) const noexcept;
};

/**
 * 分割扩展字符串
 * 
 * 将 OpenGL 扩展字符串（空格分隔）分割为字符串集合。
 * 用于解析 glGetString(GL_EXTENSIONS) 返回的扩展列表。
 * 
 * @param extensions 扩展字符串（空格分隔，如 "GL_EXT_texture_filter_anisotropic GL_EXT_debug_marker"）
 * @return 包含所有扩展名称的无序字符串集合
 */
unordered_string_set split(const char* extensions) noexcept;

} // namespace filament::backend::GLUtils


#endif // TNT_FILAMENT_BACKEND_OPENGL_GLUTILS_H
