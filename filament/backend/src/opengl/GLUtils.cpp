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

#include "GLUtils.h"

#include "private/backend/Driver.h"

#include <utils/Logger.h>
#include <utils/compiler.h>
#include <utils/ostream.h>
#include <utils/trap.h>

#include <string_view>

#include <stddef.h>
#include <cstdio>

namespace filament::backend {

using namespace backend;
using namespace utils;

namespace GLUtils {

/**
 * 获取 OpenGL 错误字符串
 * 
 * 将 OpenGL 错误代码转换为可读的字符串。
 * 
 * @param error OpenGL 错误代码
 * @return 错误字符串视图
 */
UTILS_NOINLINE
std::string_view getGLErrorString(GLenum error) noexcept {
    switch (error) {
        case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            break;
    }
    return "unknown";
}

/**
 * 检查 OpenGL 错误（非致命）
 * 
 * 检查是否有 OpenGL 错误，如果有则记录日志但不中断执行。
 * 
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 * @return OpenGL 错误代码（GL_NO_ERROR 表示无错误）
 */
UTILS_NOINLINE
GLenum checkGLError(const char* function, size_t line) noexcept {
    GLenum const error = glGetError();
    if (UTILS_VERY_UNLIKELY(error != GL_NO_ERROR)) {
        auto const string = getGLErrorString(error);
        char hexError[16];
        snprintf(hexError, sizeof(hexError), "%#x", error);
        LOG(ERROR) << "OpenGL error " << hexError << " (" << string << ") in \"" << function
                   << "\" at line " << line;
    }
    return error;
}

/**
 * 断言 OpenGL 错误（致命）
 * 
 * 检查是否有 OpenGL 错误，如果有则记录日志并触发调试陷阱。
 * 
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 */
UTILS_NOINLINE
void assertGLError(const char* function, size_t line) noexcept {
    GLenum const err = checkGLError(function, line);
    if (UTILS_VERY_UNLIKELY(err != GL_NO_ERROR)) {
        debug_trap();
    }
}

/**
 * 获取帧缓冲区状态字符串
 * 
 * 将帧缓冲区状态代码转换为可读的字符串。
 * 
 * @param status 帧缓冲区状态代码
 * @return 状态字符串视图
 */
UTILS_NOINLINE
std::string_view getFramebufferStatusString(GLenum status) noexcept {
    switch (status) {
        case GL_FRAMEBUFFER_COMPLETE:
            return "GL_FRAMEBUFFER_COMPLETE";
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
        case GL_FRAMEBUFFER_UNSUPPORTED:
            return "GL_FRAMEBUFFER_UNSUPPORTED";
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_FRAMEBUFFER_UNDEFINED:
            return "GL_FRAMEBUFFER_UNDEFINED";
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
#endif
        default:
            break;
    }
    return "unknown";
}

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
UTILS_NOINLINE
GLenum checkFramebufferStatus(GLenum target, const char* function, size_t line) noexcept {
    GLenum const status = glCheckFramebufferStatus(target);
    if (UTILS_VERY_UNLIKELY(status != GL_FRAMEBUFFER_COMPLETE)) {
        auto const string = getFramebufferStatusString(status);
        char hexStatus[16];
        snprintf(hexStatus, sizeof(hexStatus), "%#x", status);
        LOG(ERROR) << "OpenGL framebuffer error " << hexStatus << " (" << string << ") in \""
                   << function << "\" at line " << line;
    }
    return status;
}

/**
 * 断言帧缓冲区状态（致命）
 * 
 * 检查帧缓冲区状态，如果不完整则记录日志并触发调试陷阱。
 * 
 * @param target 帧缓冲区目标
 * @param function 函数名（用于日志）
 * @param line 行号（用于日志）
 */
UTILS_NOINLINE
void assertFramebufferStatus(GLenum target, const char* function, size_t line) noexcept {
    GLenum const status = checkFramebufferStatus(target, function, line);
    if (UTILS_VERY_UNLIKELY(status != GL_FRAMEBUFFER_COMPLETE)) {
        debug_trap();
    }
}

/**
 * 检查是否包含字符串
 * 
 * 检查集合中是否包含指定的字符串。
 * 
 * @param str 要查找的字符串视图
 * @return 如果包含返回 true，否则返回 false
 */
bool unordered_string_set::has(std::string_view str) const noexcept {
    return find(str) != end();
}

/**
 * 分割扩展字符串
 * 
 * 将 OpenGL 扩展字符串（空格分隔）分割为字符串集合。
 * 用于解析 glGetString(GL_EXTENSIONS) 返回的扩展列表。
 * 
 * @param extensions 扩展字符串（空格分隔，如 "GL_EXT_texture_filter_anisotropic GL_EXT_debug_marker"）
 * @return 包含所有扩展名称的无序字符串集合
 * 
 * 算法：
 * 1. 创建字符串视图
 * 2. 循环查找空格分隔符
 * 3. 将每个扩展名称添加到集合中
 * 4. 继续处理剩余字符串
 */
unordered_string_set split(const char* extensions) noexcept {
    unordered_string_set set;
    std::string_view string(extensions);
    do {
        // 查找下一个空格或字符串结束
        auto p = string.find(' ');
        p = (p == std::string_view::npos ? string.length() : p);
        // 将扩展名称添加到集合
        set.emplace(string.data(), p);
        // 移除已处理的扩展名称（包括空格）
        string.remove_prefix(p == string.length() ? p : p + 1);
    } while(!string.empty());
    return set;
}

} // namespace GLUtils
} // namespace filament::backend
