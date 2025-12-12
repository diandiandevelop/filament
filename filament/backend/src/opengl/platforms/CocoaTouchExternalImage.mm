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

#include "CocoaTouchExternalImage.h"

#define GLES_SILENCE_DEPRECATION
#include <OpenGLES/EAGL.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

#include "../GLUtils.h"

#include <math/vec2.h>

#include <utils/compiler.h>
#include <utils/Panic.h>
#include <utils/debug.h>
#include <utils/Log.h>

namespace filament::backend {

/**
 * 顶点着色器源码（OpenGL ES 3.0）
 * 
 * 简单的全屏三角形顶点着色器。
 */
static const char* s_vertexES = R"SHADER(#version 300 es
in vec4 pos;
void main() {
    gl_Position = pos;
}
)SHADER";

/**
 * 片段着色器源码（OpenGL ES 3.0）
 * 
 * 将 YCbCr 格式转换为 RGB 格式。
 * 使用两个纹理：亮度平面和色度平面。
 */
static const char* s_fragmentES = R"SHADER(#version 300 es
precision mediump float;
uniform sampler2D samplerLuminance;
uniform sampler2D samplerColor;
layout(location = 0) out vec4 fragColor;
void main() {
    float luminance = texelFetch(samplerLuminance, ivec2(gl_FragCoord.xy), 0).r;
    // 色度平面是亮度平面的一半大小
    vec2 color = texelFetch(samplerColor, ivec2(gl_FragCoord.xy) / 2, 0).ra;

    vec4 ycbcr = vec4(luminance, color, 1.0);

    // YCbCr 到 RGB 转换矩阵
    mat4 ycbcrToRgbTransform = mat4(
        vec4(+1.0000f, +1.0000f, +1.0000f, +0.0000f),
        vec4(+0.0000f, -0.3441f, +1.7720f, +0.0000f),
        vec4(+1.4020f, -0.7141f, +0.0000f, +0.0000f),
        vec4(-0.7010f, +0.5291f, -0.8860f, +1.0000f)
    );

    fragColor = ycbcrToRgbTransform * ycbcr;
}
)SHADER";

/**
 * SharedGl 构造函数
 * 
 * 初始化共享 GL 对象，包括：
 * 1. 创建采样器对象（最近邻过滤，边缘夹紧）
 * 2. 编译顶点和片段着色器
 * 3. 链接着色器程序
 * 4. 设置 uniform 变量（亮度采样器和色度采样器）
 */
CocoaTouchExternalImage::SharedGl::SharedGl() noexcept {
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER,   GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER,   GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S,       GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T,       GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R,       GL_CLAMP_TO_EDGE);

    GLint status;

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &s_vertexES, nullptr);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    assert_invariant(status == GL_TRUE);

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &s_fragmentES, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
    assert_invariant(status == GL_TRUE);

    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    assert_invariant(status == GL_TRUE);

    // Save current program state.
    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);

    glUseProgram(program);
    GLint luminanceLoc = glGetUniformLocation(program, "samplerLuminance");
    GLint colorLoc = glGetUniformLocation(program, "samplerColor");
    glUniform1i(luminanceLoc, 0);
    glUniform1i(colorLoc, 1);

    // Restore state.
    glUseProgram(currentProgram);
}

/**
 * SharedGl 析构函数
 * 
 * 清理共享 GL 对象。
 */
CocoaTouchExternalImage::SharedGl::~SharedGl() noexcept {
    glDeleteSamplers(1, &sampler);
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(program);
}

/**
 * CocoaTouchExternalImage 构造函数
 * 
 * @param textureCache CoreVideo 纹理缓存
 * @param sharedGl 共享 GL 对象引用
 */
CocoaTouchExternalImage::CocoaTouchExternalImage(const CVOpenGLESTextureCacheRef textureCache,
        const SharedGl& sharedGl) noexcept : mSharedGl(sharedGl), mTextureCache(textureCache) {

    glGenFramebuffers(1, &mFBO);

    CHECK_GL_ERROR()
}

/**
 * CocoaTouchExternalImage 析构函数
 * 
 * 清理帧缓冲区和外部图像资源。
 */
CocoaTouchExternalImage::~CocoaTouchExternalImage() noexcept {
    release();
    glDeleteFramebuffers(1, &mFBO);
}

/**
 * 设置外部图像
 * 
 * 将 CVPixelBuffer 转换为 OpenGL ES 纹理。
 * 支持两种格式：
 * 1. 32BGRA：直接创建纹理
 * 2. 420YpCbCr8BiPlanarFullRange：执行 YCbCr 到 RGB 转换
 * 
 * @param image CVPixelBuffer 指针
 * @return 如果成功返回 true
 * 
 * 执行流程：
 * 1. 释放之前的外部图像引用
 * 2. 验证像素格式和平面数
 * 3. 锁定像素缓冲区
 * 4. 根据平面数处理：
 *    - 0 平面：直接创建纹理（32BGRA）
 *    - 2 平面：创建 Y 和 CbCr 纹理，执行颜色转换（YCbCr）
 */
bool CocoaTouchExternalImage::set(CVPixelBufferRef image) noexcept {
    // 释放之前的外部图像引用（如果有）
    release();

    if (!image) {
        return false;
    }

    OSType formatType = CVPixelBufferGetPixelFormatType(image);
    FILAMENT_CHECK_POSTCONDITION(formatType == kCVPixelFormatType_32BGRA ||
            formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange)
            << "iOS external images must be in either 32BGRA or 420f format.";

    size_t planeCount = CVPixelBufferGetPlaneCount(image);
    FILAMENT_CHECK_POSTCONDITION(planeCount == 0 || planeCount == 2)
            << "The OpenGL backend does not support images with plane counts of " << planeCount
            << ".";

    // 像素缓冲区在进行渲染时必须锁定。我们会在释放前解锁。
    UTILS_UNUSED_IN_RELEASE CVReturn lockStatus = CVPixelBufferLockBaseAddress(image, 0);
    assert_invariant(lockStatus == kCVReturnSuccess);

    if (planeCount == 0) {
        // 单平面格式（32BGRA）：直接创建纹理
        mImage = image;
        mTexture = createTextureFromImage(image, GL_RGBA, GL_BGRA, 0);
        mEncodedToRgb = false;
    }

    if (planeCount == 2) {
        // 双平面格式（YCbCr）：创建 Y 和 CbCr 纹理，执行颜色转换
        CVOpenGLESTextureRef yPlane = createTextureFromImage(image, GL_LUMINANCE, GL_LUMINANCE, 0);
        CVOpenGLESTextureRef colorPlane = createTextureFromImage(image, GL_LUMINANCE_ALPHA,
                GL_LUMINANCE_ALPHA, 1);

        size_t width, height;
        width = CVPixelBufferGetWidthOfPlane(image, 0);
        height = CVPixelBufferGetHeightOfPlane(image, 0);
        mRgbTexture = encodeColorConversionPass(CVOpenGLESTextureGetName(yPlane),
                CVOpenGLESTextureGetName(colorPlane), width, height);
        mEncodedToRgb = true;

        // 外部图像在传递给驱动时被保留。我们现在完成了，所以释放它。
        CVPixelBufferUnlockBaseAddress(image, 0);
        CVPixelBufferRelease(image);

        // 同样释放临时 CVOpenGLESTextureRefs
        CFRelease(yPlane);
        CFRelease(colorPlane);
    }

    return true;
}

GLuint CocoaTouchExternalImage::getGlTexture() const noexcept {
    if (mEncodedToRgb) {
        return mRgbTexture;
    }
    return CVOpenGLESTextureGetName(mTexture);
}

GLuint CocoaTouchExternalImage::getInternalFormat() const noexcept {
    if (mEncodedToRgb) {
        return GL_RGBA8;
    }
    return GL_R8;
}

GLuint CocoaTouchExternalImage::getTarget() const noexcept {
    if (mEncodedToRgb) {
        return GL_TEXTURE_2D;
    }
    return CVOpenGLESTextureGetTarget(mTexture);
}

void CocoaTouchExternalImage::release() noexcept {
    if (mImage) {
        CVPixelBufferUnlockBaseAddress(mImage, 0);
        CVPixelBufferRelease(mImage);
    }
    if (mTexture) {
        CFRelease(mTexture);
    }
    if (mEncodedToRgb) {
        glDeleteTextures(1, &mRgbTexture);
        mRgbTexture = 0;
    }
}

/**
 * 从图像创建纹理
 * 
 * 使用 CoreVideo 纹理缓存从 CVPixelBuffer 的指定平面创建 OpenGL ES 纹理。
 * 
 * @param image CVPixelBuffer 指针
 * @param glFormat GL 内部格式
 * @param format GL 格式
 * @param plane 平面索引
 * @return CVOpenGLESTextureRef，失败返回 nullptr
 */
CVOpenGLESTextureRef CocoaTouchExternalImage::createTextureFromImage(CVPixelBufferRef image, GLuint
        glFormat, GLenum format, size_t plane) noexcept {
    const size_t width = CVPixelBufferGetWidthOfPlane(image, plane);
    const size_t height = CVPixelBufferGetHeightOfPlane(image, plane);

    CVOpenGLESTextureRef texture = nullptr;
    UTILS_UNUSED_IN_RELEASE CVReturn success =
            CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
            mTextureCache, image, nullptr, GL_TEXTURE_2D, glFormat, width, height,
            format, GL_UNSIGNED_BYTE, plane, &texture);
    assert_invariant(success == kCVReturnSuccess);

    return texture;
}

/**
 * 编码颜色转换通道
 * 
 * 使用渲染通道将 YCbCr 格式转换为 RGB 格式。
 * 
 * @param yPlaneTexture Y 平面纹理 ID
 * @param colorTexture CbCr 平面纹理 ID
 * @param width 宽度
 * @param height 高度
 * @return RGB 纹理 ID
 * 
 * 执行流程：
 * 1. 保存当前 GL 状态
 * 2. 创建目标 RGB 纹理
 * 3. 绑定源 Y 和 CbCr 纹理
 * 4. 设置帧缓冲区
 * 5. 设置几何体（全屏三角形）
 * 6. 执行绘制（使用 YCbCr 到 RGB 转换着色器）
 * 7. 恢复 GL 状态
 */
GLuint CocoaTouchExternalImage::encodeColorConversionPass(GLuint yPlaneTexture,
    GLuint colorTexture, size_t width, size_t height) noexcept {

    // 全屏三角形顶点（覆盖整个屏幕并延伸到外部以确保覆盖）
    const math::float2 vtx[3] = {{ -1.0f,  3.0f },
                                 { -1.0f, -1.0f },
                                 {  3.0f, -1.0f }};

    GLuint texture;
    glGenTextures(1, &texture);

    mState.save();

    // 创建纹理以保存 RGB 转换的结果
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);

    CHECK_GL_ERROR()

    // 源纹理
    glBindSampler(0, mSharedGl.sampler);
    glBindSampler(1, mSharedGl.sampler);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, yPlaneTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, colorTexture);

    // 目标纹理
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    CHECK_GL_ERROR()
    CHECK_GL_FRAMEBUFFER_STATUS(GL_FRAMEBUFFER)

    // 几何体
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vtx);

    // 绘制
    glViewport(0, 0, width, height);
    glUseProgram(mSharedGl.program);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    CHECK_GL_ERROR()

    mState.restore();

    return texture;
}

void CocoaTouchExternalImage::State::save() noexcept {
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &array);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexAttrib);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_VIEWPORT, viewport);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding[0]);

    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler[1]);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding[1]);
}

void CocoaTouchExternalImage::State::restore() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureBinding[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textureBinding[1]);

    glBindBuffer(GL_ARRAY_BUFFER, array);
    glBindVertexArray(vertexArray);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindSampler(0, sampler[0]);
    glBindSampler(1, sampler[1]);
    glActiveTexture(activeTexture);
    if (!vertexAttrib) {
        glDisableVertexAttribArray(0);
    }
}

} // namespace filament::backend
