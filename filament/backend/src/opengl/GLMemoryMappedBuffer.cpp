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

#include "GLMemoryMappedBuffer.h"

#include "GLBufferObject.h"
#include "GLUtils.h"
#include "OpenGLContext.h"
#include "OpenGLDriver.h"

#include "gl_headers.h"

#include <private/backend/HandleAllocator.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/BitmaskEnum.h>

#include <limits>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace filament::backend {

/**
 * 默认构造函数
 * 
 * 创建一个未初始化的内存映射缓冲区。
 */
GLMemoryMappedBuffer::GLMemoryMappedBuffer() = default;

/**
 * 析构函数
 * 
 * 注意：析构函数不会自动取消映射。
 * 必须在销毁前调用 unmap() 取消映射。
 */
GLMemoryMappedBuffer::~GLMemoryMappedBuffer() = default;

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
 * 1. 验证缓冲区对象和参数有效性
 * 2. ES2 路径：使用客户端缓冲区，直接访问缓冲区数据
 * 3. ES 3.0+ 路径：使用 glMapBufferRange() 映射缓冲区
 * 4. 设置映射标志和访问权限
 * 5. 增加缓冲区对象的映射计数
 */
GLMemoryMappedBuffer::GLMemoryMappedBuffer(OpenGLContext& glc, HandleAllocatorGL& handleAllocator,
        BufferObjectHandle boh,
        size_t const offset, size_t const size, MapBufferAccessFlags const access)
            : boh(boh), access(access) {

    // 获取缓冲区对象
    GLBufferObject* const bo = handleAllocator.handle_cast<GLBufferObject*>(boh);

    // 验证缓冲区对象和参数
    assert_invariant(bo);
    assert_invariant(bo->mappingCount < std::numeric_limits<uint8_t>::max());
    assert_invariant(offset + size <= bo->byteCount);

    // 如果请求写入访问，验证缓冲区支持共享写入
    if (any(access & MapBufferAccessFlags::WRITE_BIT)) {
        assert_invariant(any(bo->usage & BufferUsage::SHARED_WRITE_BIT));
    }

    // ES2 路径：使用客户端缓冲区
    if (UTILS_UNLIKELY(glc.isES2())) {
        // ES2 不支持 glMapBufferRange，直接使用客户端缓冲区指针
        gl.vaddr = static_cast<char*>(bo->gl.buffer) + offset;
        gl.size = size;
        gl.offset = offset;
        gl.binding = 0; // ES2 模式下，bo->gl.binding 不可用
        gl.id = bo->gl.id;
        bo->age++; // 技术上我们可以只在 copy() 中执行此操作
        bo->mappingCount++;
        return;
    }

#if !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
        void* addr = nullptr;

#   if !defined(__EMSCRIPTEN__)
        // 隐式使用非同步映射（但与读取不兼容）
        GLbitfield gl_access = GL_MAP_UNSYNCHRONIZED_BIT;
        
        // 如果请求写入访问，添加写入标志
        if (any(access & MapBufferAccessFlags::WRITE_BIT)) {
            gl_access |= GL_MAP_WRITE_BIT;
        }
        
        // 如果请求无效化范围，添加无效化标志
        // 注意：GL_MAP_INVALIDATE_RANGE_BIT 与 GL_MAP_READ_BIT 不兼容
        if (any(access & MapBufferAccessFlags::INVALIDATE_RANGE_BIT)) {
            gl_access |= GL_MAP_INVALIDATE_RANGE_BIT;
        }

        // 绑定缓冲区并映射
        glc.bindBuffer(bo->gl.binding, bo->gl.id);
        addr = glMapBufferRange(bo->gl.binding, GLsizeiptr(offset), GLsizeiptr(size), gl_access);

        CHECK_GL_ERROR();
#   endif

    // 如果映射失败，addr 将为 nullptr
    gl.vaddr = addr;
    gl.size = size;
    gl.offset = offset;
    gl.binding = bo->gl.binding;
    gl.id = bo->gl.id;
    bo->mappingCount++;

#endif
}

/**
 * 取消映射
 * 
 * 释放内存映射，使缓冲区可以再次用于绘制。
 * 
 * @param glc OpenGLContext 引用，用于绑定缓冲区
 * @param handleAllocator Handle 分配器，用于获取缓冲区对象
 * 
 * 执行流程：
 * 1. 验证缓冲区对象和映射计数
 * 2. ES2 路径：无需操作（使用客户端缓冲区）
 * 3. ES 3.0+ 路径：调用 glUnmapBuffer() 取消映射
 * 4. 减少缓冲区对象的映射计数
 * 
 * 注意事项：
 * - 如果 glUnmapBuffer() 返回 GL_FALSE，映射已丢失（如屏幕模式改变时）
 * - 这不是 OpenGL 错误，但映射数据可能已失效
 * - 这种情况很少见，但对我们来说是个问题
 */
void GLMemoryMappedBuffer::unmap(OpenGLContext& glc, HandleAllocatorGL& handleAllocator) const {
    GLBufferObject* const bo = handleAllocator.handle_cast<GLBufferObject*>(boh);
    assert_invariant(bo);
    assert_invariant(bo->mappingCount > 0);

    // 减少映射计数
    bo->mappingCount--;

    // ES2 路径：无需操作
    if (UTILS_UNLIKELY(glc.isES2())) {
        // 无需操作
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 绑定缓冲区
    glc.bindBuffer(gl.binding, gl.id);
#   if !defined(__EMSCRIPTEN__)
        // 如果没有映射或映射失败，不执行取消映射
        if (UTILS_LIKELY(gl.vaddr)) {
            // 取消映射缓冲区
            if (UTILS_UNLIKELY(glUnmapBuffer(gl.binding) == GL_FALSE)) {
                // TODO: 根据规范，UnmapBuffer 在罕见情况下可能返回 FALSE（例如
                //   在屏幕模式改变时）。注意这不是 GL 错误，但整个映射已丢失。
                //   这对我们来说是个问题。
            }
            CHECK_GL_ERROR();
        }
#   endif
#endif
}

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
 * 1. 验证写入访问权限和偏移范围
 * 2. 如果映射成功（gl.vaddr 非空）：
 *    - 使用 memcpy 直接复制到映射的内存（最快）
 * 3. 如果映射失败（WebGL 或错误）：
 *    - 回退到 glBufferSubData() 进行数据传输
 * 4. 调度数据描述符的销毁
 * 
 * 性能考虑：
 * - 直接内存复制比 glBufferSubData 更快，因为避免了 GPU 同步
 * - 未来可以改进：保留 BufferDescriptor 并合并 glBufferSubData 调用
 *   以减少 API 调用次数
 */
void GLMemoryMappedBuffer::copy(OpenGLContext& glc, OpenGLDriver& gld,
        size_t const offset, BufferDescriptor&& data) const {
    // 验证写入访问权限
    assert_invariant(any(access & MapBufferAccessFlags::WRITE_BIT));
    // 验证偏移范围
    assert_invariant(offset + data.size <= gl.size);

    // 如果映射成功，直接复制到映射的内存
    if (UTILS_LIKELY(gl.vaddr)) {
        memcpy(static_cast<char *>(gl.vaddr) + offset, data.buffer, data.size);
    } else {
        // 映射失败（WebGL 或错误），回退到 glBufferSubData
        assert_invariant(!glc.isES2());
        // 我们无法映射（WebGL 或错误），回退到 glBufferSubData。
        // 未来可以改进：保留 BufferDescriptor 并合并 glBufferSubData 调用
        glc.bindBuffer(gl.binding, gl.id);
        glBufferSubData(gl.binding, GLintptr(gl.offset + offset), GLsizeiptr(data.size), data.buffer);
        CHECK_GL_ERROR();
    }

    // 调度数据描述符的销毁
    gld.scheduleDestroy(std::move(data));
}

} // namespace filament::backend
