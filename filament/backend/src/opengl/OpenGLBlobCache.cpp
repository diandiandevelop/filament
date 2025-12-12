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

#include "OpenGLBlobCache.h"

#include "OpenGLContext.h"

#include <backend/Platform.h>
#include <backend/Program.h>

#include <private/utils/Tracing.h>

#include <utils/Logger.h>

namespace filament::backend {

/**
 * Blob 数据结构
 * 
 * 存储程序二进制数据，包括格式和二进制内容。
 * 使用灵活数组成员（flexible array member）存储二进制数据。
 * 
 * 字段说明：
 * - format: 程序二进制格式（由 OpenGL 实现定义）
 * - data: 程序二进制数据（灵活数组成员，实际大小在运行时确定）
 */
struct OpenGLBlobCache::Blob {
    GLenum format;  // 程序二进制格式
    char data[];    // 程序二进制数据（灵活数组成员）
};

/**
 * 构造函数
 * 
 * 初始化 Blob 缓存，检查是否支持程序二进制缓存。
 * 
 * @param gl OpenGLContext 引用，用于查询支持的功能
 */
OpenGLBlobCache::OpenGLBlobCache(OpenGLContext& gl) noexcept
    : mCachingSupported(gl.gets.num_program_binary_formats >= 1) {
    // 如果 OpenGL 实现支持至少一种程序二进制格式，则启用缓存
    // num_program_binary_formats >= 1 表示支持 glProgramBinary() 和 glGetProgramBinary()
}

/**
 * 从缓存中检索程序二进制
 * 
 * 尝试从平台提供的缓存中检索已编译的程序二进制。
 * 如果找到有效的缓存，直接加载程序二进制，避免重新编译。
 * 
 * @param outKey 输出参数：如果提供，将返回使用的缓存键
 * @param platform Platform 引用，提供 retrieveBlob() 方法
 * @param program Program 引用，用于生成缓存键
 * @return 成功返回程序对象 ID，失败或未找到缓存返回 0
 * 
 * 执行流程：
 * 1. 检查是否支持缓存和平台是否提供检索函数
 * 2. 生成缓存键（程序缓存 ID + 特化常量）
 * 3. 从平台检索 Blob 数据（初始尝试 64 KiB 缓冲区）
 * 4. 如果缓冲区太小，重新分配正确大小的缓冲区并重试
 * 5. 使用 glProgramBinary() 加载程序二进制
 * 6. 验证程序是否成功链接（检查 GL_LINK_STATUS）
 * 7. 如果验证失败，删除程序并返回 0（回退到正常编译）
 * 
 * 错误处理：
 * - 如果 glProgramBinary() 失败或程序未链接，返回 0
 * - 这通常发生在驱动程序更新后，缓存的二进制不再兼容
 * - 调用者应该回退到正常的编译和链接流程
 */
GLuint OpenGLBlobCache::retrieve(BlobCacheKey* outKey, Platform& platform,
        Program const& program) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    
    // 检查是否支持缓存和平台是否提供检索函数
    if (!mCachingSupported || !platform.hasRetrieveBlobFunc()) {
        // 在这种情况下，键不会被更新
        return 0;
    }

    GLuint programId = 0;

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 生成缓存键：程序缓存 ID + 特化常量
    BlobCacheKey key{ program.getCacheId(), program.getSpecializationConstants() };

    // FIXME: 使用静态缓冲区以避免系统分配
    // 始终尝试使用 64 KiB
    constexpr size_t DEFAULT_BLOB_SIZE = 65536;
    std::unique_ptr<Blob, decltype(&::free)> blob{ (Blob*)malloc(DEFAULT_BLOB_SIZE), &::free };

    // 从平台检索 Blob 数据
    size_t const blobSize = platform.retrieveBlob(
            key.data(), key.size(), blob.get(), DEFAULT_BLOB_SIZE);

    if (blobSize > 0) {
        // 如果缓冲区太小，使用正确的大小重试
        if (blobSize > DEFAULT_BLOB_SIZE) {
            blob.reset((Blob*)malloc(blobSize));
            platform.retrieveBlob(
                    key.data(), key.size(), blob.get(), blobSize);
        }

        // 计算程序二进制大小（总大小减去 Blob 结构体大小）
        GLsizei const programBinarySize = GLsizei(blobSize - sizeof(Blob));

        // 创建程序对象
        programId = glCreateProgram();

        { // systrace 作用域
            FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "glProgramBinary");
            // 从二进制数据加载程序
            glProgramBinary(programId, blob->format, blob->data, programBinarySize);
        }

        // 验证从 Blob 缓存检索的程序。
        // glProgramBinary 可能成功但导致程序未链接，
        // 因此我们必须检查 glGetError() 和 GL_LINK_STATUS。
        // 例如，如果图形驱动程序已更新，可能会发生这种情况。
        // 如果加载失败，返回 0 以回退到正常编译和链接。
        GLenum glError = glGetError();
        GLint linkStatus = GL_FALSE;
        if (glError == GL_NO_ERROR) {
            glGetProgramiv(programId, GL_LINK_STATUS, &linkStatus);
        }

        // 如果加载失败，删除程序并返回 0
        if (UTILS_UNLIKELY(glError != GL_NO_ERROR || linkStatus != GL_TRUE)) {
            LOG(WARNING) << "Failed to load program binary, name=" << program.getName().c_str_safe()
                         << ", size=" << blobSize << ", format=" << blob->format
                         << ", glError=" << glError << ", linkStatus=" << linkStatus;
            glDeleteProgram(programId);
            programId = 0;
        }
    }

    // 如果提供了输出参数，返回使用的缓存键
    if (UTILS_LIKELY(outKey)) {
        using std::swap;
        swap(*outKey, key);
    }
#endif

    return programId;
}

/**
 * 将程序二进制插入缓存
 * 
 * 将已编译和链接的程序二进制保存到平台提供的缓存中。
 * 保存的程序可以在后续运行中通过 retrieve() 快速加载。
 * 
 * @param platform Platform 引用，提供 insertBlob() 方法
 * @param key 缓存键，用于标识此程序二进制
 * @param program 已编译和链接的程序对象 ID
 * 
 * 执行流程：
 * 1. 检查是否支持缓存和平台是否提供插入函数
 * 2. 查询程序二进制长度（glGetProgramiv(GL_PROGRAM_BINARY_LENGTH)）
 * 3. 分配缓冲区并获取程序二进制（glGetProgramBinary()）
 * 4. 将 Blob 数据（格式 + 二进制）保存到平台缓存
 * 
 * 注意事项：
 * - 仅在程序成功编译和链接后调用
 * - 平台层负责实际的持久化存储（如磁盘缓存）
 * - 如果插入失败（如磁盘空间不足），不会影响程序运行
 */
void OpenGLBlobCache::insert(Platform& platform,
        BlobCacheKey const& key, GLuint program) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    
    // 检查是否支持缓存和平台是否提供插入函数
    if (!mCachingSupported || !platform.hasInsertBlobFunc()) {
        // 在这种情况下，键不会被更新
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    GLenum format;  // 程序二进制格式
    GLint programBinarySize = 0;
    
    { // systrace 作用域
        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "glGetProgramiv");
        // 查询程序二进制长度
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &programBinarySize);
    }
    
    // 如果程序有二进制数据，获取并保存
    if (programBinarySize) {
        // 分配缓冲区：Blob 结构体 + 程序二进制数据
        size_t const size = sizeof(Blob) + programBinarySize;
        std::unique_ptr<Blob, decltype(&::free)> blob{ (Blob*)malloc(size), &::free };
        
        if (UTILS_LIKELY(blob)) {
            { // systrace 作用域
                FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "glGetProgramBinary");
                // 获取程序二进制数据
                glGetProgramBinary(program, programBinarySize,
                        &programBinarySize, &format, blob->data);
            }
            
            // 检查是否有错误
            GLenum const error = glGetError();
            if (error == GL_NO_ERROR) {
                // 设置格式并保存到平台缓存
                blob->format = format;
                platform.insertBlob(key.data(), key.size(), blob.get(), size);
            }
        }
    }
#endif
}

} // namespace filament::backend
