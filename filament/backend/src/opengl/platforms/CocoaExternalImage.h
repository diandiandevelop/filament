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

#ifndef FILAMENT_DRIVER_OPENGL_COCOA_EXTERNAL_IMAGE
#define FILAMENT_DRIVER_OPENGL_COCOA_EXTERNAL_IMAGE

#include <backend/platforms/OpenGLPlatform.h>
#include <CoreVideo/CoreVideo.h>

#include "../gl_headers.h"

namespace filament::backend {

/**
 * Cocoa 外部图像类
 * 
 * 封装 macOS CoreVideo CVPixelBuffer 作为 OpenGL 纹理。
 * 
 * 主要功能：
 * 1. 将 CVPixelBuffer 转换为 OpenGL 纹理
 * 2. 处理矩形纹理到 2D 纹理的转换
 * 3. 管理纹理生命周期
 * 
 * 使用场景：
 * - 从 AVFoundation 获取视频帧
 * - 从相机捕获图像
 * - 其他 CoreVideo 像素缓冲区源
 */
class CocoaExternalImage final : public OpenGLPlatform::ExternalTexture {
public:
    /**
     * 共享 GL 对象类
     * 
     * 可以在多个 CocoaExternalImage 实例之间共享的 GL 对象。
     * 包括着色器程序和采样器对象。
     */
    class SharedGl {
    public:
        SharedGl() noexcept;
        ~SharedGl() noexcept;

        // 禁止拷贝
        SharedGl(const SharedGl&) = delete;
        SharedGl& operator=(const SharedGl&) = delete;

        GLuint program = 0;          // 着色器程序（用于矩形纹理到 2D 纹理的转换）
        GLuint sampler = 0;          // 采样器对象
        GLuint fragmentShader = 0;    // 片段着色器
        GLuint vertexShader = 0;      // 顶点着色器
    };

    /**
     * 构造函数
     * 
     * @param textureCache CoreVideo 纹理缓存
     * @param sharedGl 共享 GL 对象引用
     */
    CocoaExternalImage(const CVOpenGLTextureCacheRef textureCache,
            const SharedGl& sharedGl) noexcept;
    
    /**
     * 析构函数
     * 
     * 清理纹理资源。
     */
    ~CocoaExternalImage() noexcept;

    /**
     * 设置外部图像
     * 
     * 将此外部图像设置为传入的 CVPixelBuffer。
     * 之后，调用 glGetTexture 返回由 CVPixelBuffer 支持的 GL 纹理名称。
     * 
     * @param p CVPixelBuffer 指针
     * @return 如果成功返回 true
     */
    bool set(CVPixelBufferRef p) noexcept;

    /**
     * 获取 GL 纹理
     * 
     * @return GL 纹理 ID
     */
    GLuint getGlTexture() const noexcept;
    
    /**
     * 获取内部格式
     * 
     * @return GL 内部格式
     */
    GLuint getInternalFormat() const noexcept;
    
    /**
     * 获取纹理目标
     * 
     * @return GL 纹理目标（GL_TEXTURE_2D）
     */
    GLuint getTarget() const noexcept;

private:
    /**
     * 释放资源
     * 
     * 释放当前持有的纹理和像素缓冲区引用。
     */
    void release() noexcept;
    
    /**
     * 从图像创建纹理
     * 
     * 使用 CoreVideo 纹理缓存从 CVPixelBuffer 创建 OpenGL 纹理。
     * 
     * @param image CVPixelBuffer 指针
     * @return CVOpenGLTextureRef，失败返回 nullptr
     */
    CVOpenGLTextureRef createTextureFromImage(CVPixelBufferRef image) noexcept;
    
    /**
     * 编码复制矩形纹理到 2D 纹理
     * 
     * 使用渲染通道将矩形纹理（GL_TEXTURE_RECTANGLE）复制到 2D 纹理（GL_TEXTURE_2D）。
     * 
     * @param rectangle 矩形纹理 ID
     * @param width 宽度
     * @param height 高度
     * @return 2D 纹理 ID
     */
    GLuint encodeCopyRectangleToTexture2D(GLuint rectangle, size_t width, size_t height) noexcept;

    /**
     * GL 状态保存/恢复类
     * 
     * 在修改 GL 状态之前保存，之后恢复。
     */
    class State {
    public:
        /**
         * 保存当前 GL 状态
         */
        void save() noexcept;
        
        /**
         * 恢复之前保存的 GL 状态
         */
        void restore() noexcept;

    private:
        GLint activeTexture = 0;      // 活动纹理单元
        GLint textureBinding = { 0 };  // 纹理绑定
        GLint samplerBinding = { 0 };  // 采样器绑定
        GLint framebuffer = 0;         // 帧缓冲区绑定
        GLint viewport[4] = { 0 };     // 视口
        GLint vertexAttrib = 0;        // 顶点属性
    } mState;

    GLuint mFBO = 0;                              // 帧缓冲区对象（用于纹理转换）
    const SharedGl& mSharedGl;                    // 共享 GL 对象引用
    GLuint mRgbaTexture = 0;                      // RGBA 2D 纹理（最终输出）

    const CVOpenGLTextureCacheRef mTextureCache;  // CoreVideo 纹理缓存
    CVPixelBufferRef mImage = nullptr;            // 当前像素缓冲区
    CVOpenGLTextureRef mTexture = nullptr;       // 当前 CoreVideo 纹理
};

} // namespace filament::backend

#endif
