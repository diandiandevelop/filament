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

#define COREVIDEO_SILENCE_GL_DEPRECATION

#include "CocoaExternalImage.h"
#include "../GLUtils.h"

#include <utils/Panic.h>
#include <utils/Log.h>

namespace filament::backend {

/**
 * 顶点着色器源码
 * 
 * 使用 gl_VertexID 生成全屏三角形，无需顶点缓冲区。
 */
static const char *s_vertex = R"SHADER(#version 410 core
void main() {
    float x = -1.0 + float(((gl_VertexID & 1) <<2));
    float y = -1.0 + float(((gl_VertexID & 2) <<1));
    gl_Position=vec4(x, y, 0.0, 1.0);
}
)SHADER";

/**
 * 片段着色器源码
 * 
 * 从矩形纹理（GL_TEXTURE_RECTANGLE）采样到 2D 纹理。
 */
static const char *s_fragment = R"SHADER(#version 410 core
precision mediump float;

uniform sampler2DRect rectangle;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(rectangle, gl_FragCoord.xy);
}
)SHADER";

/**
 * SharedGl 构造函数
 * 
 * 初始化共享 GL 对象，包括：
 * 1. 创建采样器对象（最近邻过滤，边缘夹紧）
 * 2. 编译顶点和片段着色器
 * 3. 链接着色器程序
 * 4. 设置 uniform 变量
 */
CocoaExternalImage::SharedGl::SharedGl() noexcept {
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    GLint status;

    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &s_vertex, nullptr);
    glCompileShader(vertexShader);
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    assert_invariant(status == GL_TRUE);

    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &s_fragment, nullptr);
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
    GLint samplerLoc = glGetUniformLocation(program, "rectangle");
    glUniform1i(samplerLoc, 0);

    // Restore state.
    glUseProgram(currentProgram);
}

/**
 * SharedGl 析构函数
 * 
 * 清理共享 GL 对象。
 */
CocoaExternalImage::SharedGl::~SharedGl() noexcept {
    glDeleteSamplers(1, &sampler);
    glDetachShader(program, vertexShader);
    glDetachShader(program, fragmentShader);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(program);
}

/**
 * CocoaExternalImage 构造函数
 * 
 * @param textureCache CoreVideo 纹理缓存
 * @param sharedGl 共享 GL 对象引用
 */
CocoaExternalImage::CocoaExternalImage(const CVOpenGLTextureCacheRef textureCache,
        const SharedGl &sharedGl) noexcept : mSharedGl(sharedGl), mTextureCache(textureCache) {
    glGenFramebuffers(1, &mFBO);
    CHECK_GL_ERROR()
}

/**
 * CocoaExternalImage 析构函数
 * 
 * 清理帧缓冲区和外部图像资源。
 */
CocoaExternalImage::~CocoaExternalImage() noexcept {
    glDeleteFramebuffers(1, &mFBO);
    release();
}

/**
 * 设置外部图像
 * 
 * 将 CVPixelBuffer 转换为 OpenGL 纹理。
 * 
 * @param image CVPixelBuffer 指针
 * @return 如果成功返回 true
 * 
 * 执行流程：
 * 1. 释放之前的外部图像引用
 * 2. 验证像素格式（必须是 32BGRA）
 * 3. 锁定像素缓冲区
 * 4. 从图像创建矩形纹理
 * 5. 将矩形纹理复制到 2D 纹理
 */
bool CocoaExternalImage::set(CVPixelBufferRef image) noexcept {
    // 释放之前的外部图像引用（如果有）
    release();

    if (!image) {
        return false;
    }

    OSType formatType = CVPixelBufferGetPixelFormatType(image);
    FILAMENT_CHECK_POSTCONDITION(formatType == kCVPixelFormatType_32BGRA)
            << "macOS external images must be 32BGRA format.";

    // 像素缓冲区在进行渲染时必须锁定。我们会在释放前解锁。
    UTILS_UNUSED_IN_RELEASE CVReturn lockStatus = CVPixelBufferLockBaseAddress(image, 0);
    assert_invariant(lockStatus == kCVReturnSuccess);

    mImage = image;
    mTexture = createTextureFromImage(image);
    // 将矩形纹理复制到 2D 纹理
    mRgbaTexture = encodeCopyRectangleToTexture2D(CVOpenGLTextureGetName(mTexture),
            CVPixelBufferGetWidth(image), CVPixelBufferGetHeight(image));
    CHECK_GL_ERROR()

    return true;
}

GLuint CocoaExternalImage::getGlTexture() const noexcept {
    return mRgbaTexture;
}

GLuint CocoaExternalImage::getInternalFormat() const noexcept {
    if (mRgbaTexture) {
        return GL_RGBA8;
    }
    return 0;
}

GLuint CocoaExternalImage::getTarget() const noexcept {
    if (mRgbaTexture) {
        return GL_TEXTURE_2D;
    }
    return 0;
}

/**
 * 释放资源
 * 
 * 释放当前持有的纹理和像素缓冲区引用。
 */
void CocoaExternalImage::release() noexcept {
    if (mImage) {
        CVPixelBufferUnlockBaseAddress(mImage, 0);
        CVPixelBufferRelease(mImage);
    }
    if (mTexture) {
        CFRelease(mTexture);
    }
    if (mRgbaTexture) {
        glDeleteTextures(1, &mRgbaTexture);
        mRgbaTexture = 0;
    }
}

/**
 * 从图像创建纹理
 * 
 * 使用 CoreVideo 纹理缓存从 CVPixelBuffer 创建 OpenGL 矩形纹理。
 * 
 * @param image CVPixelBuffer 指针
 * @return CVOpenGLTextureRef，失败返回 nullptr
 */
CVOpenGLTextureRef CocoaExternalImage::createTextureFromImage(CVPixelBufferRef image) noexcept {
    CVOpenGLTextureRef texture = nullptr;
    UTILS_UNUSED_IN_RELEASE CVReturn success =
            CVOpenGLTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
            mTextureCache, image, nil, &texture);
    assert_invariant(success == kCVReturnSuccess);

    return texture;
}

/**
 * 编码复制矩形纹理到 2D 纹理
 * 
 * 使用渲染通道将矩形纹理（GL_TEXTURE_RECTANGLE）复制到 2D 纹理（GL_TEXTURE_2D）。
 * 
 * @param rectangle 矩形纹理 ID
 * @param width 宽度
 * @param height 高度
 * @return 2D 纹理 ID
 * 
 * 执行流程：
 * 1. 保存当前 GL 状态
 * 2. 创建目标 2D 纹理
 * 3. 绑定源矩形纹理
 * 4. 设置帧缓冲区
 * 5. 执行全屏三角形绘制
 * 6. 恢复 GL 状态
 */
GLuint CocoaExternalImage::encodeCopyRectangleToTexture2D(GLuint rectangle,
        size_t width, size_t height) noexcept {
    GLuint texture;
    glGenTextures(1, &texture);

    mState.save();

    // 创建纹理以保存 blit 图像的结果
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    CHECK_GL_ERROR()

    // 源纹理
    glBindSampler(0, mSharedGl.sampler);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_RECTANGLE, rectangle);
    CHECK_GL_ERROR()

    // 目标纹理
    glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    CHECK_GL_ERROR()

    CHECK_GL_FRAMEBUFFER_STATUS(GL_FRAMEBUFFER)
    CHECK_GL_ERROR()

    // 绘制
    glViewport(0, 0, width, height);
    CHECK_GL_ERROR()
    glUseProgram(mSharedGl.program);
    CHECK_GL_ERROR()
    glDisableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);  // 全屏三角形
    CHECK_GL_ERROR()

    mState.restore();
    CHECK_GL_ERROR()

    return texture;
}

/**
 * 保存当前 GL 状态
 * 
 * 保存活动纹理单元、纹理绑定、采样器绑定、帧缓冲区绑定、视口和顶点属性状态。
 */
void CocoaExternalImage::State::save() noexcept {
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);
    glGetIntegerv(GL_SAMPLER_BINDING, &samplerBinding);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &vertexAttrib);
}

/**
 * 恢复之前保存的 GL 状态
 * 
 * 恢复活动纹理单元、纹理绑定、采样器绑定、帧缓冲区绑定、视口和顶点属性状态。
 */
void CocoaExternalImage::State::restore() noexcept {
    glActiveTexture(activeTexture);
    glBindTexture(GL_TEXTURE_2D, textureBinding);
    glBindSampler(0, samplerBinding);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

    if (vertexAttrib) {
        glEnableVertexAttribArray(0);
    }
}

} // namespace filament::backend
