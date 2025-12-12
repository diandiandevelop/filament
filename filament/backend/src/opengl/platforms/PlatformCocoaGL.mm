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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <backend/platforms/PlatformCocoaGL.h>

#include "opengl/gl_headers.h"

#include <utils/compiler.h>
#include <utils/Panic.h>

#include <OpenGL/OpenGL.h>
#include <Cocoa/Cocoa.h>

#include <vector>
#include "CocoaExternalImage.h"

namespace filament::backend {

using namespace backend;

/**
 * Cocoa GL 交换链结构
 * 
 * 封装 macOS NSView 作为 OpenGL 交换链。
 * 跟踪视图边界和窗口框架变化。
 */
struct CocoaGLSwapChain : public Platform::SwapChain {
    /**
     * 构造函数
     * 
     * @param inView NSView 指针
     */
    explicit CocoaGLSwapChain(NSView* inView);
    
    /**
     * 析构函数
     * 
     * 清理通知观察者。
     */
    ~CocoaGLSwapChain() noexcept;

    NSView* view;                                    // NSView 指针
    NSRect previousBounds;                          // 之前的边界
    NSRect previousWindowFrame;                     // 之前的窗口框架
    NSMutableArray<id<NSObject>>* observers;        // 通知观察者数组
    NSRect currentBounds;                           // 当前边界
    NSRect currentWindowFrame;                      // 当前窗口框架
};

/**
 * Cocoa GL 平台实现结构
 * 
 * 存储 macOS OpenGL 平台的内部状态。
 */
struct PlatformCocoaGLImpl {
    NSOpenGLContext* mGLContext = nullptr;                          // 主 OpenGL 上下文
    CocoaGLSwapChain* mCurrentSwapChain = nullptr;                   // 当前交换链
    std::vector<NSView*> mHeadlessSwapChains;                       // 无头交换链视图
    std::vector<NSOpenGLContext*> mAdditionalContexts;              // 额外上下文（用于多线程）
    CVOpenGLTextureCacheRef mTextureCache = nullptr;                // CoreVideo 纹理缓存
    std::unique_ptr<CocoaExternalImage::SharedGl> mExternalImageSharedGl;  // 外部图像共享 GL 对象
    void updateOpenGLContext(NSView *nsView, bool resetView, bool clearView);  // 更新 OpenGL 上下文
    
    /**
     * 外部图像结构
     * 
     * 封装 CVPixelBuffer 作为外部图像。
     */
    struct ExternalImageCocoaGL final : public Platform::ExternalImage {
        CVPixelBufferRef cvBuffer;  // CoreVideo 像素缓冲区
    protected:
        ~ExternalImageCocoaGL() noexcept final;
    };
};

PlatformCocoaGLImpl::ExternalImageCocoaGL::~ExternalImageCocoaGL() noexcept = default;

/**
 * 构造函数
 * 
 * 初始化交换链并注册视图变化通知。
 * 
 * @param inView NSView 指针
 * 
 * 执行流程：
 * 1. 初始化成员变量
 * 2. 在主线程上注册通知观察者
 * 3. 监听窗口和视图的尺寸/位置变化
 */
CocoaGLSwapChain::CocoaGLSwapChain( NSView* inView )
        : view(inView)
        , previousBounds(NSZeroRect)
        , previousWindowFrame(NSZeroRect)
        , observers([NSMutableArray array])
        , currentBounds(NSZeroRect)
        , currentWindowFrame(NSZeroRect) {
    NSView* __weak weakView = view;
    NSMutableArray* __weak weakObservers = observers;
    
    /**
     * 通知处理块
     * 
     * 当视图或窗口尺寸/位置改变时更新当前边界和窗口框架。
     */
    void (^notificationHandler)(NSNotification *notification) = ^(NSNotification *notification) {
        NSView* strongView = weakView;
        if ((weakView != nil) && (weakObservers != nil)) {
            // 更新当前边界（转换为后备存储坐标）
            this->currentBounds = [strongView convertRectToBacking: strongView.bounds];
            // 更新当前窗口框架
            this->currentWindowFrame = strongView.window.frame;
        }
    };
    
    // 以下各种方法应仅从主线程调用：
    // -[NSView bounds], -[NSView convertRectToBacking:], -[NSView window],
    // -[NSWindow frame], -[NSView superview],
    // -[NSView setPostsFrameChangedNotifications:],
    // -[NSView setPostsBoundsChangedNotifications:]
    dispatch_async(dispatch_get_main_queue(), ^(void) {
        NSView* strongView = weakView;
        NSMutableArray* strongObservers = weakObservers;
        if ((weakView == nil) || (weakObservers == nil)) {
            return;
        }
        @synchronized (strongObservers) {
            this->currentBounds = [strongView convertRectToBacking: strongView.bounds];
            this->currentWindowFrame = strongView.window.frame;

            id<NSObject> observer = [NSNotificationCenter.defaultCenter
                addObserverForName: NSWindowDidResizeNotification
                object: strongView.window
                queue: nil
                usingBlock: notificationHandler];
            [strongObservers addObject: observer];
            observer = [NSNotificationCenter.defaultCenter
                addObserverForName: NSWindowDidMoveNotification
                object: strongView.window
                queue: nil
                usingBlock: notificationHandler];
           [strongObservers addObject: observer];

            NSView* aView = strongView;
            while (aView != nil) {
                aView.postsFrameChangedNotifications = YES;
                aView.postsBoundsChangedNotifications = YES;
                observer = [NSNotificationCenter.defaultCenter
                    addObserverForName: NSViewFrameDidChangeNotification
                    object: aView
                    queue: nil
                    usingBlock: notificationHandler];
                [strongObservers addObject: observer];
                observer = [NSNotificationCenter.defaultCenter
                    addObserverForName: NSViewBoundsDidChangeNotification
                    object: aView
                    queue: nil
                    usingBlock: notificationHandler];
                [strongObservers addObject: observer];
                
                aView = aView.superview;
            }
        }
    });
}

CocoaGLSwapChain::~CocoaGLSwapChain() noexcept {
    @synchronized (observers) {
        for (id<NSObject> observer in observers) {
             [NSNotificationCenter.defaultCenter removeObserver: observer];
        }
    }
}

PlatformCocoaGL::PlatformCocoaGL()
        : pImpl(new PlatformCocoaGLImpl) {
}

PlatformCocoaGL::~PlatformCocoaGL() noexcept {
    delete pImpl;
}

/**
 * 创建驱动
 * 
 * 初始化 macOS OpenGL 上下文并创建驱动。
 * 
 * @param sharedContext 共享上下文（可选）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针
 * 
 * 执行流程：
 * 1. 配置像素格式属性（OpenGL 3.2 Core Profile）
 * 2. 创建 OpenGL 上下文
 * 3. 设置交换间隔为 0（禁用垂直同步）
 * 4. 绑定 BlueGL（加载 OpenGL 入口点）
 * 5. 创建 CoreVideo 纹理缓存
 * 6. 创建驱动
 */
Driver* PlatformCocoaGL::createDriver(void* sharedContext, const Platform::DriverConfig& driverConfig) {
    // NSOpenGLPFAColorSize: 未指定时，优先选择与屏幕匹配的格式
    NSOpenGLPixelFormatAttribute pixelFormatAttributes[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,  // OpenGL 3.2 Core Profile
            NSOpenGLPFADepthSize,    (NSOpenGLPixelFormatAttribute) 24,  // 24 位深度缓冲区
            NSOpenGLPFADoubleBuffer, (NSOpenGLPixelFormatAttribute) true,  // 双缓冲
            NSOpenGLPFAAccelerated,  (NSOpenGLPixelFormatAttribute) true,  // 硬件加速
            0, 0,
    };

    NSOpenGLContext* shareContext = (__bridge NSOpenGLContext*) sharedContext;
    NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttributes];
    NSOpenGLContext* nsOpenGLContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:shareContext];

    // 设置交换间隔为 0（禁用垂直同步）
    GLint interval = 0;
    [nsOpenGLContext makeCurrentContext];
    [nsOpenGLContext setValues:&interval forParameter:NSOpenGLCPSwapInterval];

    pImpl->mGLContext = nsOpenGLContext;

    // 绑定 BlueGL（加载 OpenGL 入口点）
    int result = bluegl::bind();
    FILAMENT_CHECK_POSTCONDITION(!result) << "Unable to load OpenGL entry points.";

    // 创建 CoreVideo 纹理缓存（用于外部图像）
    UTILS_UNUSED_IN_RELEASE CVReturn success = CVOpenGLTextureCacheCreate(kCFAllocatorDefault, nullptr,
            [pImpl->mGLContext CGLContextObj], [pImpl->mGLContext.pixelFormat CGLPixelFormatObj], nullptr,
            &pImpl->mTextureCache);
    assert_invariant(success == kCVReturnSuccess);

    return OpenGLPlatform::createDefaultDriver(this, sharedContext, driverConfig);
}

/**
 * 检查是否支持额外上下文
 * 
 * macOS 支持共享上下文，但实现看起来在所有 GL API 周围使用全局锁。
 * 这对于需要长时间执行的 API 调用是个问题，例如：glCompileProgram。
 * 
 * @return 总是返回 true（macOS 支持共享上下文）
 */
bool PlatformCocoaGL::isExtraContextSupported() const noexcept {
    // macOS 支持共享上下文，但实现看起来在所有 GL API 周围使用全局锁。
    // 这对于需要长时间执行的 API 调用是个问题，例如：glCompileProgram。
    return true;
}

/**
 * 创建额外上下文
 * 
 * 创建额外的 OpenGL 上下文（用于多线程编译）。
 * 
 * @param shared 是否与主上下文共享
 */
void PlatformCocoaGL::createContext(bool shared) {
    NSOpenGLPixelFormatAttribute pixelFormatAttributes[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,  // OpenGL 3.2 Core Profile
            NSOpenGLPFAAccelerated,  (NSOpenGLPixelFormatAttribute) true,  // 硬件加速
            0, 0,
    };
    NSOpenGLContext* const sharedContext = shared ? pImpl->mGLContext : nil;
    NSOpenGLPixelFormat* const pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttributes];
    NSOpenGLContext* const nsOpenGLContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:sharedContext];
    [nsOpenGLContext makeCurrentContext];
    pImpl->mAdditionalContexts.push_back(nsOpenGLContext);
}

/**
 * 获取操作系统版本
 * 
 * macOS 平台返回 0（无操作系统版本概念）。
 * 
 * @return 操作系统版本（macOS 返回 0）
 */
int PlatformCocoaGL::getOSVersion() const noexcept {
    return 0;
}

/**
 * 终止平台
 * 
 * 清理所有资源，包括纹理缓存、外部图像共享 GL 对象、上下文和 BlueGL。
 */
void PlatformCocoaGL::terminate() noexcept {
    CFRelease(pImpl->mTextureCache);
    pImpl->mExternalImageSharedGl.reset();
    pImpl->mGLContext = nil;
    for (auto& context : pImpl->mAdditionalContexts) {
        context = nil;
    }
    bluegl::unbind();
}

/**
 * 创建交换链（从原生窗口）
 * 
 * @param nativewindow NSView 指针
 * @param flags 交换链标志
 * @return 交换链指针
 * 
 * 注意：如果交换链正在重新创建（例如底层表面已调整大小），
 * 我们需要在后续的 makeCurrent 中强制更新，这可以通过简单地重置当前视图来完成。
 * 在多窗口情况下，这会自动发生。
 */
Platform::SwapChain* PlatformCocoaGL::createSwapChain(void* nativewindow, uint64_t flags) noexcept {
    NSView* nsView = (__bridge NSView*)nativewindow;

    CocoaGLSwapChain* swapChain = new CocoaGLSwapChain( nsView );

    // 如果交换链正在重新创建（例如底层表面已调整大小），
    // 我们需要在后续的 makeCurrent 中强制更新，这可以通过简单地重置当前视图来完成。
    // 在多窗口情况下，这会自动发生。
    if (pImpl->mCurrentSwapChain && pImpl->mCurrentSwapChain->view == nsView) {
        pImpl->mCurrentSwapChain = nullptr;
    }

    return swapChain;
}

/**
 * 创建交换链（无头模式）
 * 
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * @return 交换链指针
 */
Platform::SwapChain* PlatformCocoaGL::createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept {
    NSView* nsView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];

    // 将指针添加到数组会保留 NSView
    pImpl->mHeadlessSwapChains.push_back(nsView);

    CocoaGLSwapChain* swapChain = new CocoaGLSwapChain( nsView );

    return swapChain;
}

/**
 * 销毁交换链
 * 
 * @param swapChain 交换链指针
 */
void PlatformCocoaGL::destroySwapChain(Platform::SwapChain* swapChain) noexcept {
    CocoaGLSwapChain* cocoaSwapChain = static_cast<CocoaGLSwapChain*>(swapChain);
    if (pImpl->mCurrentSwapChain == cocoaSwapChain) {
        pImpl->mCurrentSwapChain = nullptr;
    }

    auto& v = pImpl->mHeadlessSwapChains;
    auto it = std::find(v.begin(), v.end(), cocoaSwapChain->view);
    if (it != v.end()) {
        // 从数组中移除指针会释放 NSView
        v.erase(it);
    }
    delete cocoaSwapChain;
}

bool PlatformCocoaGL::makeCurrent(ContextType type, SwapChain* drawSwapChain,
        SwapChain* readSwapChain) {
    ASSERT_PRECONDITION_NON_FATAL(drawSwapChain == readSwapChain,
            "ContextManagerCocoa does not support using distinct draw/read swap chains.");
    CocoaGLSwapChain* swapChain = (CocoaGLSwapChain*)drawSwapChain;
    NSRect currentBounds = swapChain->currentBounds;
    NSRect currentWindowFrame = swapChain->currentWindowFrame;

    // Check if the view has been swapped out or resized.
    // updateOpenGLContext() needs to call -clearDrawable if the view was
    // resized, otherwise we do not want to call -clearDrawable, as in addition
    // to disassociating the context from the view as the documentation says,
    // it also does what its name implies and clears the drawable pixels.
    // This is problematic with multiple windows, but still necessary if the
    // window has resized.
    if (pImpl->mCurrentSwapChain != swapChain) {
        pImpl->mCurrentSwapChain = swapChain;
        if (!NSEqualRects(currentBounds, swapChain->previousBounds)) {
            // A window is being resized or moved, but the last draw was a
            // different window (for example, it updates on a timer):
            // just call -setView first, otherwise -clearDrawable would clear
            // the other window. But if we do this the first time that the swap
            // chain has been created then resizing will show a black image.
            if (!NSIsEmptyRect(swapChain->previousBounds)) {
                pImpl->updateOpenGLContext(swapChain->view, true, false);
            }
            // Now call -clearDrawable, otherwise we get garbage during if we
            // are resizing.
            pImpl->updateOpenGLContext(swapChain->view, true, true);
        } else {
            // We are drawing another window: only call -setView.
            pImpl->updateOpenGLContext(swapChain->view, true, false);
        }
    } else if (!NSEqualRects(currentWindowFrame, swapChain->previousWindowFrame)) {
        // Same window has moved or resized: need to clear and set view.
        pImpl->updateOpenGLContext(swapChain->view, true, true);
    }

    swapChain->previousBounds = currentBounds;
    swapChain->previousWindowFrame = currentWindowFrame;
    return true;
}

/**
 * 提交交换链
 * 
 * 刷新 OpenGL 缓冲区并刷新 CoreVideo 纹理缓存。
 * 
 * @param swapChain 交换链指针
 */
void PlatformCocoaGL::commit(Platform::SwapChain* swapChain) noexcept {
    [pImpl->mGLContext flushBuffer];

    // 这需要定期执行
    CVOpenGLTextureCacheFlush(pImpl->mTextureCache, 0);
}

/**
 * 处理事件
 * 
 * 在主线程上处理 Cocoa 事件循环。
 * 
 * @return 如果成功返回 true，否则返回 false
 */
bool PlatformCocoaGL::pumpEvents() noexcept {
    if (![NSThread isMainThread]) {
        return false;
    }
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate distantPast]];
    return true;
}

/**
 * 创建外部图像纹理
 * 
 * 创建用于外部图像（CVPixelBuffer）的纹理对象。
 * 
 * @return 外部纹理指针
 */
OpenGLPlatform::ExternalTexture* PlatformCocoaGL::createExternalImageTexture() noexcept {
    if (!pImpl->mExternalImageSharedGl) {
        pImpl->mExternalImageSharedGl = std::make_unique<CocoaExternalImage::SharedGl>();
    }
    ExternalTexture* outTexture = new CocoaExternalImage(pImpl->mTextureCache,
            *pImpl->mExternalImageSharedGl);
    return outTexture;
}

/**
 * 销毁外部图像纹理
 * 
 * @param texture 外部纹理指针
 */
void PlatformCocoaGL::destroyExternalImageTexture(ExternalTexture* texture) noexcept {
    auto* p = static_cast<CocoaExternalImage*>(texture);
    delete p;
}

/**
 * 保留外部图像
 * 
 * 获取传入缓冲区的所有权。它将在下次调用 setExternalImage 时或纹理销毁时释放。
 * 
 * @param externalImage CVPixelBuffer 指针
 */
void PlatformCocoaGL::retainExternalImage(void* externalImage) noexcept {
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
bool PlatformCocoaGL::setExternalImage(void* externalImage, ExternalTexture* texture) noexcept {
    CVPixelBufferRef cvPixelBuffer = (CVPixelBufferRef) externalImage;
    CocoaExternalImage* cocoaExternalImage = static_cast<CocoaExternalImage*>(texture);
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
Platform::ExternalImageHandle PlatformCocoaGL::createExternalImage(void* cvPixelBuffer) noexcept {
    auto* p = new(std::nothrow) PlatformCocoaGLImpl::ExternalImageCocoaGL;
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
void PlatformCocoaGL::retainExternalImage(ExternalImageHandleRef externalImage) noexcept {
    auto const* const cocoaGlExternalImage
            = static_cast<PlatformCocoaGLImpl::ExternalImageCocoaGL const*>(externalImage.get());
    // 获取传入缓冲区的所有权。它将在下次调用 setExternalImage 时或纹理销毁时释放。
    CVPixelBufferRef pixelBuffer = cocoaGlExternalImage->cvBuffer;
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
bool PlatformCocoaGL::setExternalImage(ExternalImageHandleRef externalImage, ExternalTexture* texture) noexcept {
    auto const* const cocoaGlExternalImage
            = static_cast<PlatformCocoaGLImpl::ExternalImageCocoaGL const*>(externalImage.get());
    CVPixelBufferRef cvPixelBuffer = cocoaGlExternalImage->cvBuffer;
    CocoaExternalImage* cocoaExternalImage = static_cast<CocoaExternalImage*>(texture);
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
 * 更新 OpenGL 上下文
 * 
 * 更新 OpenGL 上下文与 NSView 的关联。
 * 
 * @param nsView NSView 指针
 * @param resetView 是否重置视图
 * @param clearView 是否清除可绘制对象
 * 
 * 注意：NSOpenGLContext 要求 "setView" 和 "update" 从 UI 线程调用。
 * 这在 macOS 10.15 (Catalina) 中成为硬性要求。
 * 如果从 GL 线程调用这些方法，我们会看到 EXC_BAD_INSTRUCTION。
 */
void PlatformCocoaGLImpl::updateOpenGLContext(NSView *nsView, bool resetView,
                                              bool clearView) {
    NSOpenGLContext* glContext = mGLContext;

    // 注意：这没有很好的文档记录（如果有的话），但 NSOpenGLContext 要求 "setView" 和
    // "update" 从 UI 线程调用。这在 macOS 10.15 (Catalina) 中成为硬性要求。
    // 如果从 GL 线程调用这些方法，我们会看到 EXC_BAD_INSTRUCTION。
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^(void) {
            if (resetView) {
                if (clearView) {
                    [glContext clearDrawable];
                }
                [glContext setView:nsView];
            }
            [glContext update];
        });
    } else {
        if (resetView) {
            if (clearView) {
                [glContext clearDrawable];
            }
            [glContext setView:nsView];
        }
        [glContext update];
    }
}

} // namespace filament::backend

#pragma clang diagnostic pop
