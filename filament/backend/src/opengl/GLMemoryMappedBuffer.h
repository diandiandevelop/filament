/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLMEMORYMAPPEDBUFFER_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLMEMORYMAPPEDBUFFER_H

// 驱动基类
#include "DriverBase.h"

// Handle 分配器
#include <private/backend/HandleAllocator.h>

// 后端枚举和句柄
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

// OpenGL 头文件
#include "gl_headers.h"

#include <stddef.h>  // 标准定义
#include <stdint.h>  // 标准整数类型

namespace filament::backend {

class OpenGLContext;
class OpenGLDriver;
class BufferDescriptor;

struct GLBufferObject;

/**
 * OpenGL 内存映射缓冲区结构
 * 
 * 封装 OpenGL 缓冲区的内存映射功能。
 * 允许 CPU 直接访问 GPU 缓冲区内存，避免通过 glBufferSubData 等 API 进行数据传输。
 * 
 * 主要功能：
 * 1. 内存映射：将 GPU 缓冲区映射到 CPU 可访问的虚拟地址空间
 * 2. 数据复制：将数据复制到映射的内存区域
 * 3. 取消映射：释放内存映射
 * 
 * 实现方式：
 * - ES 3.0+ / GL 4.1+：使用 glMapBufferRange() 和 glUnmapBuffer()
 * - ES 2.0：使用客户端缓冲区（client-side buffer），直接访问缓冲区数据
 * 
 * 性能优化：
 * - 使用 GL_MAP_UNSYNCHRONIZED_BIT 避免同步等待
 * - 使用 GL_MAP_INVALIDATE_RANGE_BIT 允许驱动程序优化
 * - 支持异步写入，提高性能
 * 
 * 注意事项：
 * - 映射的缓冲区在取消映射前不能用于绘制
 * - 多个映射可以同时存在（通过 mappingCount 跟踪）
 * - WebGL 可能不支持内存映射，会回退到 glBufferSubData
 */
struct GLMemoryMappedBuffer : public HwMemoryMappedBuffer {
    BufferObjectHandle boh;  // 关联的缓冲区对象句柄
    MapBufferAccessFlags access;  // 映射访问标志（读/写/无效化等）

    /**
     * OpenGL 相关状态
     * 
     * 存储内存映射的 OpenGL 相关信息。
     */
    struct {
        void* vaddr = nullptr;  // 映射的虚拟地址（CPU 可访问的指针）
        uint32_t size = 0;      // 映射的大小（字节）
        uint32_t offset = 0;    // 映射的偏移量（字节）
        GLenum binding = 0;     // 缓冲区绑定目标（GL_ARRAY_BUFFER、GL_UNIFORM_BUFFER 等）
        GLuint id = 0;          // OpenGL 缓冲区对象 ID
    } gl;

    /**
     * 默认构造函数
     * 
     * 创建一个未初始化的内存映射缓冲区。
     */
    GLMemoryMappedBuffer();

    /**
     * 构造函数
     * 
     * 创建并初始化内存映射缓冲区。
     * 
     * @param glc OpenGLContext 引用，用于绑定缓冲区和检查 ES2
     * @param handleAllocator Handle 分配器，用于获取缓冲区对象
     * @param boh 缓冲区对象句柄
     * @param offset 映射的偏移量（字节）
     * @param size 映射的大小（字节）
     * @param access 映射访问标志（MapBufferAccessFlags）
     * 
     * 执行流程：
     * 1. ES2：使用客户端缓冲区，直接访问缓冲区数据
     * 2. ES 3.0+：使用 glMapBufferRange() 映射缓冲区
     * 3. 设置映射标志和访问权限
     * 4. 增加缓冲区对象的映射计数
     */
    GLMemoryMappedBuffer(OpenGLContext& glc, HandleAllocatorGL& handleAllocator,
            BufferObjectHandle boh, size_t offset, size_t size, MapBufferAccessFlags access);

    /**
     * 析构函数
     * 
     * 注意：析构函数不会自动取消映射。
     * 必须在销毁前调用 unmap() 取消映射。
     */
    ~GLMemoryMappedBuffer();

    /**
     * 取消映射
     * 
     * 释放内存映射，使缓冲区可以再次用于绘制。
     * 
     * @param glc OpenGLContext 引用，用于绑定缓冲区
     * @param handleAllocator Handle 分配器，用于获取缓冲区对象
     * 
     * 执行流程：
     * 1. ES2：无需操作（使用客户端缓冲区）
     * 2. ES 3.0+：调用 glUnmapBuffer() 取消映射
     * 3. 减少缓冲区对象的映射计数
     * 
     * 注意事项：
     * - 如果 glUnmapBuffer() 返回 GL_FALSE，映射已丢失（如屏幕模式改变时）
     * - 这不是 OpenGL 错误，但映射数据可能已失效
     */
    void unmap(OpenGLContext& gl, HandleAllocatorGL& handleAllocator) const;

    /**
     * 复制数据到映射缓冲区
     * 
     * 将数据复制到映射的缓冲区内存区域。
     * 
     * @param glc OpenGLContext 引用，用于绑定缓冲区
     * @param gld OpenGLDriver 引用，用于调度数据销毁
     * @param offset 相对于映射起始位置的偏移量（字节）
     * @param data 要复制的数据描述符（将被移动）
     * 
     * 执行流程：
     * 1. 如果映射成功（gl.vaddr 非空）：
     *    - 使用 memcpy 直接复制到映射的内存
     * 2. 如果映射失败（WebGL 或错误）：
     *    - 回退到 glBufferSubData() 进行数据传输
     * 3. 调度数据描述符的销毁
     * 
     * 性能考虑：
     * - 直接内存复制比 glBufferSubData 更快
     * - 未来可以改进：保留 BufferDescriptor 并合并 glBufferSubData 调用
     */
    void copy(OpenGLContext& glc, OpenGLDriver& gld,
            size_t offset, BufferDescriptor&& data) const;
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_GLMEMORYMAPPEDBUFFER_H
