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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLBUFFEROBJECT_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLBUFFEROBJECT_H

// 驱动基类
#include "DriverBase.h"

// OpenGL 头文件
#include "gl_headers.h"

// 后端枚举
#include <backend/DriverEnums.h>

#include <stdint.h>  // 标准整数类型

namespace filament::backend {

/**
 * OpenGL 缓冲区对象结构
 * 
 * 封装 OpenGL 缓冲区对象（Uniform 缓冲区、存储缓冲区等）。
 * 
 * 主要功能：
 * 1. 存储 OpenGL 缓冲区对象 ID
 * 2. 跟踪缓冲区使用方式和绑定类型
 * 3. 管理内存映射状态（mappingCount）
 * 4. 跟踪缓冲区年龄（age，用于检测更新）
 * 
 * 设计特点：
 * - ES 3.0+：使用 OpenGL 缓冲区对象（gl.id + gl.binding）
 * - ES2：使用客户端缓冲区（gl.buffer），直接访问内存
 * - 使用联合体（union）在 binding 和 buffer 之间切换
 * 
 * 使用场景：
 * - Uniform 缓冲区（UBO）
 * - 着色器存储缓冲区（SSBO）
 * - 其他类型的缓冲区对象
 */
struct GLBufferObject : public HwBufferObject {
    using HwBufferObject::HwBufferObject;
    
    /**
     * 构造函数
     * 
     * 创建缓冲区对象并初始化基本属性。
     * 
     * @param size 缓冲区大小（字节）
     * @param bindingType 缓冲区绑定类型（VERTEX、UNIFORM、SHADER_STORAGE）
     * @param usage 缓冲区使用方式（STATIC、DYNAMIC、SHARED_WRITE_BIT 等）
     */
    GLBufferObject(uint32_t size,
            BufferObjectBinding bindingType, BufferUsage usage) noexcept
            : HwBufferObject(size), usage(usage), bindingType(bindingType) {
    }

    /**
     * OpenGL 相关状态
     * 
     * 存储 OpenGL 缓冲区对象的实际状态。
     * 使用联合体在 ES 3.0+ 的 binding 和 ES2 的 buffer 之间切换。
     */
    struct {
        GLuint id;  // OpenGL 缓冲区对象 ID（ES 3.0+）
        union {
            GLenum binding;  // 缓冲区绑定目标（ES 3.0+）：GL_UNIFORM_BUFFER、GL_SHADER_STORAGE_BUFFER 等
            void* buffer;    // 客户端缓冲区指针（ES2）：直接访问的内存地址
        };
    } gl{};
    
    BufferUsage usage                   : 4;  // 缓冲区使用方式（位字段）
    BufferObjectBinding bindingType     : 4;  // 缓冲区绑定类型（位字段）
    uint8_t mappingCount = 0;                // 当前映射数量（用于跟踪内存映射状态）
    uint16_t age = 0;                        // 缓冲区年龄（用于检测更新，每次更新时递增）

};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_GLBUFFEROBJECT_H
