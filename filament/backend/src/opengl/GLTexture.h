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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLTEXTURE_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLTEXTURE_H

// 驱动基类
#include "DriverBase.h"

// OpenGL 头文件
#include "gl_headers.h"

// 后端句柄和枚举
#include <backend/Handle.h>
#include <backend/DriverEnums.h>
#include <backend/platforms/OpenGLPlatform.h>

// 标准库
#include <array>  // 数组

#include <stdint.h>  // 标准整数类型

namespace filament::backend {

/**
 * OpenGL 纹理引用结构
 * 
 * 用于管理纹理视图（Texture View）的引用计数和状态。
 * 
 * 主要功能：
 * 1. 引用计数：跟踪有多少个视图引用此纹理
 * 2. 视图状态跟踪：跟踪当前活动的视图状态（baseLevel、maxLevel、swizzle）
 * 
 * 设计目的：
 * - 在 OpenGL 中，一次只能有一个视图处于活动状态
 * - 此结构跟踪该状态，避免不必要的状态更改
 * - 用于优化性能，只在视图参数改变时更新 OpenGL 状态
 */
struct GLTextureRef {
    GLTextureRef() = default;
    
    /**
     * 视图引用计数器
     * 
     * 跟踪有多少个视图引用此纹理。
     * 当引用计数为 0 时，可以安全地删除此引用对象。
     */
    uint16_t count = 1;
    
    /**
     * 当前每个视图的纹理值
     * 
     * 在 OpenGL 中，一次只能有一个视图处于活动状态，
     * 此结构跟踪该状态。用于避免不必要的状态更改。
     */
    int8_t baseLevel = 127;  // 基础 Mip 级别（127 表示未设置）
    int8_t maxLevel = -1;    // 最大 Mip 级别（-1 表示未设置）
    
    /**
     * 通道重映射
     * 
     * 纹理视图的通道重映射（RGBA 四个通道）。
     * 默认值：CHANNEL_0、CHANNEL_1、CHANNEL_2、CHANNEL_3（无重映射）
     */
    std::array<TextureSwizzle, 4> swizzle{
            TextureSwizzle::CHANNEL_0,  // R 通道
            TextureSwizzle::CHANNEL_1,  // G 通道
            TextureSwizzle::CHANNEL_2,  // B 通道
            TextureSwizzle::CHANNEL_3   // A 通道
    };
};

/**
 * OpenGL 纹理结构
 * 
 * 封装 OpenGL 纹理对象，包括 2D、3D、立方体贴图、数组纹理等。
 * 
 * 主要功能：
 * 1. 存储 OpenGL 纹理对象 ID 和状态
 * 2. 管理纹理视图（通过 GLTextureRef）
 * 3. 支持外部纹理（External Texture）
 * 4. 支持多重采样侧车渲染缓冲区（MSAA sidecar renderbuffer）
 * 
 * 设计特点：
 * - 使用位字段优化内存使用
 * - 支持纹理视图（Texture View）引用计数
 * - 支持外部纹理（如相机、视频流）
 * - 支持多重采样纹理（通过侧车渲染缓冲区）
 * 
 * 纹理参数：
 * - anisotropy：各向异性过滤级别
 * - baseLevel/maxLevel：Mip 级别范围
 * - swizzle：通道重映射
 */
struct GLTexture : public HwTexture {
    using HwTexture::HwTexture;
    
    /**
     * OpenGL 相关状态
     * 
     * 存储 OpenGL 纹理对象的所有状态信息。
     */
    struct GL {
        GL() noexcept : imported(false), external(false), sidecarSamples(1), reserved1(0) {}
        
        GLuint id = 0;          // 纹理或渲染缓冲区 ID
        GLenum target = 0;       // 纹理目标（GL_TEXTURE_2D、GL_TEXTURE_CUBE_MAP 等）
        GLenum internalFormat = 0;  // 内部格式（GL_RGB8、GL_RGBA8 等）
        GLuint sidecarRenderBufferMS = 0;  // 多重采样侧车渲染缓冲区（用于 MSAA）

        // 纹理参数也存储在这里
        GLfloat anisotropy = 1.0;  // 各向异性过滤级别
        int8_t baseLevel = 127;    // 基础 Mip 级别（127 表示未设置）
        int8_t maxLevel = -1;      // 最大 Mip 级别（-1 表示未设置）
        uint8_t reserved0 = 0;     // 保留字段
        
        bool imported           : 1;  // 是否为导入的纹理（从外部创建）
        bool external           : 1;  // 是否为外部纹理（GL_TEXTURE_EXTERNAL_OES）
        uint8_t sidecarSamples  : 3;  // 侧车渲染缓冲区的采样数（用于 MSAA）
        uint8_t reserved1       : 3;  // 保留字段
        
        /**
         * 通道重映射
         * 
         * 纹理的通道重映射（RGBA 四个通道）。
         * 默认值：CHANNEL_0、CHANNEL_1、CHANNEL_2、CHANNEL_3（无重映射）
         */
        std::array<TextureSwizzle, 4> swizzle{
                TextureSwizzle::CHANNEL_0,  // R 通道
                TextureSwizzle::CHANNEL_1,  // G 通道
                TextureSwizzle::CHANNEL_2,  // B 通道
                TextureSwizzle::CHANNEL_3   // A 通道
        };
    } gl;
    
    /**
     * 纹理视图引用
     * 
     * 如果纹理有视图（View），此句柄指向 GLTextureRef 对象。
     * 用于管理视图的引用计数和状态。
     * 
     * mutable：允许在 const 方法中修改（用于更新视图状态）
     */
    mutable Handle<GLTextureRef> ref;
    
    /**
     * 外部纹理对象
     * 
     * 如果纹理是外部纹理（如相机、视频流），此指针指向外部纹理对象。
     * 用于管理外部纹理的生命周期和更新。
     */
    OpenGLPlatform::ExternalTexture* externalTexture = nullptr;
};


} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_GLTEXTURE_H
