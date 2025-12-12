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

#ifndef TNT_FILAMENT_BACKEND_OPENGLBLOBCACHE_H
#define TNT_FILAMENT_BACKEND_OPENGLBLOBCACHE_H

// OpenGL 头文件
#include "gl_headers.h"

// Blob 缓存键
#include "BlobCacheKey.h"

namespace filament::backend {

class Platform;
class Program;
class OpenGLContext;

/**
 * OpenGL Blob 缓存类
 * 
 * 管理 OpenGL 程序二进制缓存（Blob Cache）。
 * 用于缓存已编译和链接的着色器程序二进制数据，避免重复编译。
 * 
 * 主要功能：
 * 1. 从缓存中检索程序二进制：retrieve()
 * 2. 将程序二进制插入缓存：insert()
 * 
 * 工作原理：
 * - 使用 OpenGL 的 glProgramBinary() 和 glGetProgramBinary() API
 * - 缓存键由程序的缓存 ID 和特化常量组成
 * - 平台层提供实际的存储和检索实现
 * 
 * 性能优化：
 * - 避免重复编译相同的着色器程序
 * - 显著减少应用程序启动时间和运行时卡顿
 * - 特别适用于移动设备，其中着色器编译可能很慢
 * 
 * 注意事项：
 * - 仅在支持程序二进制格式的 OpenGL 实现中工作（ES 3.0+ / GL 4.1+）
 * - 如果驱动程序更新，缓存的二进制可能失效
 * - 缓存失效时会回退到正常编译流程
 */
class OpenGLBlobCache {
public:
    /**
     * 构造函数
     * 
     * 初始化 Blob 缓存，检查是否支持程序二进制缓存。
     * 
     * @param gl OpenGLContext 引用，用于查询支持的功能
     */
    explicit OpenGLBlobCache(OpenGLContext& gl) noexcept;

    /**
     * 从缓存中检索程序二进制
     * 
     * 尝试从平台提供的缓存中检索已编译的程序二进制。
     * 如果找到有效的缓存，直接加载程序二进制，避免重新编译。
     * 
     * @param key 输出参数：如果提供，将返回使用的缓存键
     * @param platform Platform 引用，提供 retrieveBlob() 方法
     * @param program Program 引用，用于生成缓存键
     * @return 成功返回程序对象 ID，失败或未找到缓存返回 0
     * 
     * 执行流程：
     * 1. 检查是否支持缓存和平台是否提供检索函数
     * 2. 生成缓存键（程序缓存 ID + 特化常量）
     * 3. 从平台检索 Blob 数据
     * 4. 使用 glProgramBinary() 加载程序二进制
     * 5. 验证程序是否成功链接（检查 GL_LINK_STATUS）
     * 6. 如果验证失败，删除程序并返回 0（回退到正常编译）
     * 
     * 错误处理：
     * - 如果 glProgramBinary() 失败或程序未链接，返回 0
     * - 这通常发生在驱动程序更新后，缓存的二进制不再兼容
     * - 调用者应该回退到正常的编译和链接流程
     */
    GLuint retrieve(BlobCacheKey* key, Platform& platform,
            Program const& program) const noexcept;

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
    void insert(Platform& platform,
            BlobCacheKey const& key, GLuint program) noexcept;

private:
    /**
     * Blob 数据结构
     * 
     * 存储程序二进制数据，包括格式和二进制内容。
     * 使用灵活数组成员（flexible array member）存储二进制数据。
     */
    struct Blob;
    
    /**
     * 是否支持缓存
     * 
     * 如果 OpenGL 实现支持至少一种程序二进制格式，则为 true。
     * 检查条件：glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS) >= 1
     */
    bool mCachingSupported = false;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_OPENGLBLOBCACHE_H
