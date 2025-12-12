/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include <backend/platforms/PlatformCocoaTouchGL.h>

#define GLES_SILENCE_DEPRECATION
#include <OpenGLES/EAGL.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

#include <UIKit/UIKit.h>
#include <CoreVideo/CoreVideo.h>

#include "DriverBase.h"

#include <backend/Platform.h>

#include <utils/Panic.h>

#include "CocoaTouchExternalImage.h"

namespace filament::backend {

using namespace backend;

/**
 * Cocoa Touch GL 平台实现结构
 * 
 * 存储 iOS OpenGL ES 平台的内部状态。
 */
struct PlatformCocoaTouchGLImpl {
    EAGLContext* mGLContext = nullptr;                              // 主 OpenGL ES 上下文
    CAEAGLLayer* mCurrentGlLayer = nullptr;                         // 当前 GL 层
    std::vector<CAEAGLLayer*> mHeadlessGlLayers;                    // 无头 GL 层
    std::vector<EAGLContext*> mAdditionalContexts;                 // 额外上下文（用于多线程）
    CGRect mCurrentGlLayerRect;                                      // 当前 GL 层矩形
    GLuint mDefaultFramebuffer = 0;                                 // 默认帧缓冲区
    GLuint mDefaultColorbuffer = 0;                                 // 默认颜色缓冲区
    GLuint mDefaultDepthbuffer = 0;                                 // 默认深度缓冲区
    CVOpenGLESTextureCacheRef mTextureCache = nullptr;              // CoreVideo 纹理缓存
    CocoaTouchExternalImage::SharedGl* mExternalImageSharedGl = nullptr;  // 外部图像共享 GL 对象
    
    /**
     * 外部图像结构
     * 
     * 封装 CVPixelBuffer 作为外部图像。
     */
    struct ExternalImageCocoaTouchGL : public Platform::ExternalImage {
        CVPixelBufferRef cvBuffer;  // CoreVideo 像素缓冲区
    protected:
        ~ExternalImageCocoaTouchGL() noexcept final;
    };
};

PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL::~ExternalImageCocoaTouchGL() noexcept = default;

PlatformCocoaTouchGL::PlatformCocoaTouchGL()
        : pImpl(new PlatformCocoaTouchGLImpl) {
}

PlatformCocoaTouchGL::~PlatformCocoaTouchGL() noexcept {
    delete pImpl;
}

/**
 * 创建驱动
 * 
 * 初始化 iOS OpenGL ES 上下文并创建驱动。
 * 
 * @param sharedGLContext 共享 GL 上下文（可选）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针
 * 
 * 执行流程：
 * 1. 创建 EAGL 上下文（OpenGL ES 3.0）
 * 2. 设置当前上下文
 * 3. 创建默认帧缓冲区（带颜色和深度附件）
 * 4. 创建 CoreVideo 纹理缓存
 * 5. 创建外部图像共享 GL 对象
 * 6. 创建驱动
 */
Driver* PlatformCocoaTouchGL::createDriver(void* sharedGLContext, const Platform::DriverConfig& driverConfig) {
    EAGLSharegroup* sharegroup = (__bridge EAGLSharegroup*) sharedGLContext;

    // 创建 OpenGL ES 3.0 上下文
    EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3 sharegroup:sharegroup];
    FILAMENT_CHECK_POSTCONDITION(context) << "Unable to create OpenGL ES context.";

    [EAGLContext setCurrentContext:context];

    pImpl->mGLContext = context;

    // 创建带颜色和深度附件的默认帧缓冲区
    GLuint framebuffer;
    GLuint renderbuffer[2]; // 颜色和深度
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(2, renderbuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // 绑定颜色渲染缓冲区
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer[0]);

    // 绑定深度渲染缓冲区
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer[1]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, renderbuffer[1]);

    pImpl->mDefaultFramebuffer = framebuffer;
    pImpl->mDefaultColorbuffer = renderbuffer[0];
    pImpl->mDefaultDepthbuffer = renderbuffer[1];

    // 创建 CoreVideo 纹理缓存（用于外部图像）
    UTILS_UNUSED_IN_RELEASE CVReturn success = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault,
            nullptr, pImpl->mGLContext, nullptr, &pImpl->mTextureCache);
    assert_invariant(success == kCVReturnSuccess);

    // 创建外部图像共享 GL 对象
    pImpl->mExternalImageSharedGl = new CocoaTouchExternalImage::SharedGl();

    return OpenGLPlatform::createDefaultDriver(this, sharedGLContext, driverConfig);
}

bool PlatformCocoaTouchGL::isExtraContextSupported() const noexcept {
    return true;
}

void PlatformCocoaTouchGL::createContext(bool shared) {
    EAGLSharegroup* const sharegroup = shared ? pImpl->mGLContext.sharegroup : nil;
    EAGLContext* const context = [[EAGLContext alloc]
                                  initWithAPI:kEAGLRenderingAPIOpenGLES3
                                   sharegroup:sharegroup];
    FILAMENT_CHECK_POSTCONDITION(context) << "Unable to create extra OpenGL ES context.";
    [EAGLContext setCurrentContext:context];
    pImpl->mAdditionalContexts.push_back(context);
}

/**
 * 终止平台
 * 
 * 清理所有资源，包括纹理缓存、上下文和外部图像共享 GL 对象。
 */
void PlatformCocoaTouchGL::terminate() noexcept {
    CFRelease(pImpl->mTextureCache);
    pImpl->mGLContext = nil;
    delete pImpl->mExternalImageSharedGl;
}

/**
 * 创建交换链（从原生窗口）
 * 
 * iOS 中，原生窗口直接作为交换链使用。
 * 
 * @param nativewindow CAEAGLLayer 指针
 * @param flags 交换链标志
 * @return 交换链指针（直接返回 nativewindow）
 */
Platform::SwapChain* PlatformCocoaTouchGL::createSwapChain(void* nativewindow, uint64_t flags) noexcept {
    return (SwapChain*) nativewindow;
}

/**
 * 创建交换链（无头模式）
 * 
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * @return 交换链指针
 */
Platform::SwapChain* PlatformCocoaTouchGL::createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept {
    CAEAGLLayer* glLayer = [CAEAGLLayer layer];
    glLayer.frame = CGRectMake(0, 0, width, height);

    // 将指针添加到数组会保留 CAEAGLLayer
    pImpl->mHeadlessGlLayers.push_back(glLayer);
  
    return (__bridge SwapChain*) glLayer;
}

/**
 * 销毁交换链
 * 
 * @param swapChain 交换链指针
 */
void PlatformCocoaTouchGL::destroySwapChain(Platform::SwapChain* swapChain) noexcept {
    CAEAGLLayer* glLayer = (__bridge CAEAGLLayer*) swapChain;

    if (pImpl->mCurrentGlLayer == glLayer) {
        pImpl->mCurrentGlLayer = nullptr;
    }
  
    auto& v = pImpl->mHeadlessGlLayers;
    auto it = std::find(v.begin(), v.end(), glLayer);
    if(it != v.end()) {
        // 从数组中移除指针会释放 CAEAGLLayer
        v.erase(it);
    }
}

/**
 * 获取默认帧缓冲区对象
 * 
 * @return 默认帧缓冲区 ID
 */
uint32_t PlatformCocoaTouchGL::getDefaultFramebufferObject() noexcept {
    return pImpl->mDefaultFramebuffer;
}

bool PlatformCocoaTouchGL::makeCurrent(ContextType type, SwapChain* drawSwapChain,
        SwapChain* readSwapChain) {
    ASSERT_PRECONDITION_NON_FATAL(drawSwapChain == readSwapChain,
            "PlatformCocoaTouchGL does not support using distinct draw/read swap chains.");
    CAEAGLLayer* const glLayer = (__bridge CAEAGLLayer*) drawSwapChain;

    [EAGLContext setCurrentContext:pImpl->mGLContext];

    if (pImpl->mCurrentGlLayer != glLayer ||
                !CGRectEqualToRect(pImpl->mCurrentGlLayerRect, glLayer.bounds)) {
        pImpl->mCurrentGlLayer = glLayer;
        pImpl->mCurrentGlLayerRect = glLayer.bounds;

        // associate our default color renderbuffer with the swapchain
        // this renderbuffer is an attachment of the default FBO
        glBindRenderbuffer(GL_RENDERBUFFER, pImpl->mDefaultColorbuffer);
        [pImpl->mGLContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:glLayer];

        // Retrieve width and height of color buffer.
        GLint width, height;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);

        // resize the depth buffer accordingly
        glBindRenderbuffer(GL_RENDERBUFFER, pImpl->mDefaultDepthbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

        // Test the framebuffer for completeness.
        // We must save/restore the framebuffer binding because filament is tracking the state
        GLint oldFramebuffer;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, pImpl->mDefaultFramebuffer);
        GLenum const status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        FILAMENT_CHECK_POSTCONDITION(status == GL_FRAMEBUFFER_COMPLETE)
                << "Incomplete framebuffer.";
        glBindFramebuffer(GL_FRAMEBUFFER, oldFramebuffer);
    }
    return true;
}

/**
 * 提交交换链
 * 
 * 呈现渲染缓冲区并刷新 CoreVideo 纹理缓存。
 * 
 * @param swapChain 交换链指针
 */
void PlatformCocoaTouchGL::commit(Platform::SwapChain* swapChain) noexcept {
    glBindRenderbuffer(GL_RENDERBUFFER, pImpl->mDefaultColorbuffer);
    [pImpl->mGLContext presentRenderbuffer:GL_RENDERBUFFER];

    // 这需要定期执行
    CVOpenGLESTextureCacheFlush(pImpl->mTextureCache, 0);
}

/**
 * 创建外部图像纹理
 * 
 * 创建用于外部图像（CVPixelBuffer）的纹理对象。
 * 
 * @return 外部纹理指针
 */
OpenGLPlatform::ExternalTexture* PlatformCocoaTouchGL::createExternalImageTexture() noexcept {
    ExternalTexture* outTexture = new CocoaTouchExternalImage(pImpl->mTextureCache,
            *pImpl->mExternalImageSharedGl);
    return outTexture;
}

/**
 * 销毁外部图像纹理
 * 
 * @param texture 外部纹理指针
 */
void PlatformCocoaTouchGL::destroyExternalImageTexture(ExternalTexture* texture) noexcept {
    auto* p = static_cast<CocoaTouchExternalImage*>(texture);
    delete p;
}

/**
 * 保留外部图像
 * 
 * 获取传入缓冲区的所有权。它将在下次调用 setExternalImage 时或纹理销毁时释放。
 * 
 * @param externalImage CVPixelBuffer 指针
 */
void PlatformCocoaTouchGL::retainExternalImage(void* externalImage) noexcept {
    // 获取传入缓冲区的所有权。它将在下次调用 setExternalImage 时或纹理销毁时释放。
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef) externalImage;
    CVPixelBufferRetain(pixelBuffer);
}

/**
 * 设置外部图像
 * 
 * 将 CVPixelBuffer 附加到纹理。
 * 
 * @param externalImage CVPixelBuffer 指针
 * @param texture 外部纹理指针
 * @return 如果成功返回 true
 */
bool PlatformCocoaTouchGL::setExternalImage(void* externalImage, ExternalTexture* texture) noexcept {
    CVPixelBufferRef cvPixelBuffer = (CVPixelBufferRef) externalImage;
    CocoaTouchExternalImage* cocoaExternalImage = static_cast<CocoaTouchExternalImage*>(texture);
    if (!cocoaExternalImage->set(cvPixelBuffer)) {
        return false;
    }
    texture->target = cocoaExternalImage->getTarget();
    texture->id = cocoaExternalImage->getGlTexture();
    // 我们过去设置 internalFormat，但它在 gl 后端侧的任何地方都不使用
    // cocoaExternalImage->getInternalFormat();
    return true;
}

/**
 * 创建外部图像
 * 
 * 从 CVPixelBuffer 创建外部图像句柄。
 * 
 * @param cvPixelBuffer CVPixelBuffer 指针
 * @return 外部图像句柄
 */
Platform::ExternalImageHandle PlatformCocoaTouchGL::createExternalImage(void* cvPixelBuffer) noexcept {
    auto* p = new(std::nothrow) PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL;
    p->cvBuffer = (CVPixelBufferRef) cvPixelBuffer;
    return ExternalImageHandle{ p };
}

/**
 * 保留外部图像（从句柄）
 * 
 * 从外部图像句柄保留外部图像。
 * 
 * @param externalImage 外部图像句柄引用
 */
void PlatformCocoaTouchGL::retainExternalImage(ExternalImageHandleRef externalImage) noexcept {
    auto const* const cocoaTouchGlExternalImage
            = static_cast<PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL const*>(externalImage.get());
    // 获取传入缓冲区的所有权。它将在下次调用 setExternalImage 时或纹理销毁时释放。
    CVPixelBufferRef pixelBuffer = cocoaTouchGlExternalImage->cvBuffer;
    CVPixelBufferRetain(pixelBuffer);
}

/**
 * 设置外部图像（从句柄）
 * 
 * 从外部图像句柄设置外部图像。
 * 
 * @param externalImage 外部图像句柄引用
 * @param texture 外部纹理指针
 * @return 如果成功返回 true
 */
bool PlatformCocoaTouchGL::setExternalImage(ExternalImageHandleRef externalImage, ExternalTexture* texture) noexcept {
    auto const* const cocoaTouchGlExternalImage
            = static_cast<PlatformCocoaTouchGLImpl::ExternalImageCocoaTouchGL const*>(externalImage.get());
    CVPixelBufferRef cvPixelBuffer = cocoaTouchGlExternalImage->cvBuffer;
    CocoaTouchExternalImage* cocoaExternalImage = static_cast<CocoaTouchExternalImage*>(texture);
    if (!cocoaExternalImage->set(cvPixelBuffer)) {
        return false;
    }
    texture->target = cocoaExternalImage->getTarget();
    texture->id = cocoaExternalImage->getGlTexture();
    // 我们过去设置 internalFormat，但它在 gl 后端侧的任何地方都不使用
    // cocoaExternalImage->getInternalFormat();
    return true;
}


} // namespace filament::backend
