/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef FILAMENT_DRIVER_OPENGL_COCOA_TOUCH_EXTERNAL_IMAGE
#define FILAMENT_DRIVER_OPENGL_COCOA_TOUCH_EXTERNAL_IMAGE

#include "../gl_headers.h"

#include <backend/platforms/OpenGLPlatform.h>

#include <CoreVideo/CoreVideo.h>

namespace filament::backend {

/**
 * Cocoa Touch 外部图像类
 * 
 * 封装 iOS CoreVideo CVPixelBuffer 作为 OpenGL ES 纹理。
 * 
 * 主要功能：
 * 1. 将 CVPixelBuffer 转换为 OpenGL ES 纹理
 * 2. 处理 YCbCr 到 RGB 的颜色空间转换
 * 3. 管理纹理生命周期
 * 
 * 使用场景：
 * - 从 AVFoundation 获取视频帧
 * - 从相机捕获图像
 * - 其他 CoreVideo 像素缓冲区源
 */
class CocoaTouchExternalImage final : public OpenGLPlatform::ExternalTexture {
public:

    /**
     * 共享 GL 对象类
     * 
     * 可以在多个 CocoaTouchExternalImage 实例之间共享的 GL 对象。
     * 包括着色器程序（用于 YCbCr 到 RGB 转换）和采样器对象。
     */
    class SharedGl {
    public:
        SharedGl() noexcept;
        ~SharedGl() noexcept;

        // 禁止拷贝
        SharedGl(const SharedGl&) = delete;
        SharedGl& operator=(const SharedGl&) = delete;

        GLuint program = 0;          // 着色器程序（用于 YCbCr 到 RGB 转换）
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
    CocoaTouchExternalImage(const CVOpenGLESTextureCacheRef textureCache,
            const SharedGl& sharedGl) noexcept;
    
    /**
     * 析构函数
     * 
     * 清理纹理资源。
     */
    ~CocoaTouchExternalImage() noexcept;

    /**
     * 设置外部图像
     * 
     * 将此外部图像设置为传入的 CVPixelBuffer。
     * 之后，调用 glGetTexture 返回由 CVPixelBuffer 支持的 GL 纹理名称。
     * 
     * 使用 YCbCr 图像调用 set 会执行渲染通道，将图像从 YCbCr 转换为 RGB。
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

    void release() noexcept;
    CVOpenGLESTextureRef createTextureFromImage(CVPixelBufferRef image, GLuint glFormat,
            GLenum format, size_t plane) noexcept;
    GLuint encodeColorConversionPass(GLuint yPlaneTexture, GLuint colorTexture, size_t width,
            size_t height) noexcept;

    class State {
    public:
        void save() noexcept;
        void restore() noexcept;

    private:
        GLint textureBinding[2] = { 0 };
        GLint framebuffer = 0;
        GLint array = 0;
        GLint vertexAttrib = 0;
        GLint vertexArray = 0;
        GLint viewport[4] = { 0 };
        GLint activeTexture = 0;
        GLint sampler[2] = { 0 };
    } mState;

    GLuint mFBO = 0;
    const SharedGl& mSharedGl;

    bool mEncodedToRgb = false;
    GLuint mRgbTexture = 0;

    const CVOpenGLESTextureCacheRef mTextureCache;
    CVPixelBufferRef mImage = nullptr;
    CVOpenGLESTextureRef mTexture = nullptr;

};

} // namespace filament::backend

#endif
