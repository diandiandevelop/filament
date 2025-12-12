/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <backend/platforms/PlatformWGL.h>

#include <Wingdi.h>

#ifdef _MSC_VER
    // this variable is checked in BlueGL.h (included from "gl_headers.h" right after this),
    // and prevents duplicate definition of OpenGL apis when building this file.
    // However, GL_GLEXT_PROTOTYPES need to be defined in BlueGL.h when included from other files.
    #define FILAMENT_PLATFORM_WGL
#endif

#include "../gl_headers.h"

#include "Windows.h"
#include <GL/gl.h>
#include "GL/glext.h"
#include "GL/wglext.h"

#include <utils/Logger.h>
#include <utils/Panic.h>

namespace {

/**
 * 报告 Windows 错误
 * 
 * 将 Windows 错误代码转换为可读的错误消息并记录。
 * 
 * @param dwError Windows 错误代码
 */
void reportWindowsError(DWORD dwError) {
    LPSTR lpMessageBuffer = nullptr;

    if (dwError == 0) {
        return;
    }

    // 使用 FormatMessage 获取错误消息
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        lpMessageBuffer,
        0, nullptr
	);

    LOG(ERROR) << "Windows error code: " << dwError << ". " << lpMessageBuffer;

    LocalFree(lpMessageBuffer);
}

} // namespace

namespace filament::backend {

using namespace backend;

/**
 * WGL 交换链结构
 * 
 * 存储 Windows GL 交换链的相关信息。
 */
struct WGLSwapChain {
    HDC hDc = NULL;        // 设备上下文句柄
    HWND hWnd = NULL;      // 窗口句柄
    bool isHeadless = false;  // 是否为无头模式
};

/**
 * WGL 创建上下文函数指针
 * 
 * 用于创建带属性的 OpenGL 上下文。
 */
static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribs = nullptr;

/**
 * 创建驱动
 * 
 * 初始化 WGL 并创建 OpenGL 驱动。
 * 
 * @param sharedGLContext 共享 GL 上下文（可选）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针，失败返回 nullptr
 * 
 * 执行流程：
 * 1. 创建虚拟窗口和像素格式
 * 2. 创建临时上下文以获取 wglCreateContextAttribsARB
 * 3. 尝试创建 GL 4.5 到 4.1 的上下文
 * 4. 创建共享上下文（Windows 特定工作区）
 */
Driver* PlatformWGL::createDriver(void* sharedGLContext,
        const Platform::DriverConfig& driverConfig) {
    int result = 0;
    int pixelFormat = 0;
    DWORD dwError = 0;

    // 配置像素格式描述符
    mPfd = {
        sizeof(PIXELFORMATDESCRIPTOR),
        1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    // 标志：绘制到窗口、支持 OpenGL、双缓冲
        PFD_TYPE_RGBA,        // 帧缓冲区类型：RGBA 或调色板
        32,                   // 帧缓冲区颜色深度
        0, 0, 0, 0, 0, 0,
        0,
        0,
        0,
        0, 0, 0, 0,
        24,                   // 深度缓冲区位数
        0,                    // 模板缓冲区位数
        0,                    // 帧缓冲区中的辅助缓冲区数量
        PFD_MAIN_PLANE,
        0,
        0, 0, 0
    };

    HGLRC tempContext = NULL;

    // 创建虚拟窗口（用于初始化）
    mHWnd = CreateWindowA("STATIC", "dummy", 0, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    HDC whdc = mWhdc = GetDC(mHWnd);
    if (whdc == NULL) {
        dwError = GetLastError();
        LOG(ERROR) << "CreateWindowA() failed";
        goto error;
    }

    // 选择并设置像素格式
    pixelFormat = ChoosePixelFormat(whdc, &mPfd);
    SetPixelFormat(whdc, pixelFormat, &mPfd);

    // 我们需要一个临时上下文来检索和调用 wglCreateContextAttribsARB
    tempContext = wglCreateContext(whdc);
    if (!wglMakeCurrent(whdc, tempContext)) {
        dwError = GetLastError();
        LOG(ERROR) << "wglMakeCurrent() failed, whdc=" << whdc << ", tempContext=" << tempContext;
        goto error;
    }

    // 获取 wglCreateContextAttribsARB 函数指针
    wglCreateContextAttribs =
            (PFNWGLCREATECONTEXTATTRIBSARBPROC) wglGetProcAddress("wglCreateContextAttribsARB");

    // 尝试所有版本，从 GL 4.5 到 4.1
    for (int minor = 5; minor >= 1; minor--) {
        mAttribs = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
                WGL_CONTEXT_MINOR_VERSION_ARB, minor,
                0
        };
        mContext = wglCreateContextAttribs(whdc, (HGLRC)sharedGLContext, mAttribs.data());
        if (mContext) {
            break;
        }
        dwError = GetLastError();
    }

    if (!mContext) {
        LOG(ERROR) << "wglCreateContextAttribs() failed, whdc=" << whdc;
        goto error;
    }

    // 在此处创建共享上下文以供其他线程使用。这是 Windows 特定的工作区，
    // 因为共享上下文必须在与主上下文相同的线程上初始化。
    // 如果需要更多共享上下文，必须更新常量 SHARED_CONTEXT_NUM。
    for (int i = 0; i < SHARED_CONTEXT_NUM; ++i) {
        HGLRC context = wglCreateContextAttribs(mWhdc, mContext, mAttribs.data());
        if (context) {
            mAdditionalContexts.push_back(context);
        }
    }

    // Delete the temporary context
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(tempContext);
    tempContext = NULL;

    if (!wglMakeCurrent(whdc, mContext)) {
        dwError = GetLastError();
        LOG(ERROR) << "wglMakeCurrent() failed, whdc=" << whdc << ", mContext=" << mContext;
        goto error;
    }

    result = bluegl::bind();
    FILAMENT_CHECK_POSTCONDITION(!result) << "Unable to load OpenGL entry points.";

    return OpenGLPlatform::createDefaultDriver(this, sharedGLContext, driverConfig);

error:
    if (tempContext) {
        wglDeleteContext(tempContext);
    }
    reportWindowsError(dwError);
    terminate();
    return NULL;
}

/**
 * 检查是否支持额外上下文
 * 
 * Windows WGL 支持共享上下文。
 * 
 * @return 总是返回 true
 */
bool PlatformWGL::isExtraContextSupported() const noexcept {
    return true;
}

/**
 * 创建额外上下文
 * 
 * 使用预创建的共享上下文（在 createDriver 中创建）。
 * 
 * @param shared 是否与主上下文共享（在 WGL 中总是 true）
 */
void PlatformWGL::createContext(bool shared) {
    int nextIndex = mNextFreeSharedContextIndex.fetch_add(1, std::memory_order_relaxed);
    FILAMENT_CHECK_PRECONDITION(nextIndex < SHARED_CONTEXT_NUM)
            << "Shared context index out of range. Increase SHARED_CONTEXT_NUM.";

    HGLRC context = mAdditionalContexts[nextIndex];
    BOOL result = wglMakeCurrent(mWhdc, context);
    FILAMENT_CHECK_POSTCONDITION(result) << "Failed to make current.";
}

/**
 * 终止平台
 * 
 * 清理所有资源，包括上下文、窗口和 BlueGL。
 */
void PlatformWGL::terminate() noexcept {
    wglMakeCurrent(NULL, NULL);
    if (mContext) {
        wglDeleteContext(mContext);
        mContext = NULL;
    }
    for (auto& context : mAdditionalContexts) {
        wglDeleteContext(context);  // 注意：这里应该是 context 而不是 mContext
    }
    if (mHWnd && mWhdc) {
        ReleaseDC(mHWnd, mWhdc);
        DestroyWindow(mHWnd);
        mHWnd = NULL;
        mWhdc = NULL;
    } else if (mHWnd) {
        DestroyWindow(mHWnd);
        mHWnd = NULL;
    }
    bluegl::unbind();
}

/**
 * 创建交换链（从原生窗口）
 * 
 * @param nativeWindow HWND 指针
 * @param flags 交换链标志
 * @return 交换链指针
 */
Platform::SwapChain* PlatformWGL::createSwapChain(void* nativeWindow, uint64_t flags) noexcept {
    auto* swapChain = new WGLSwapChain();
    swapChain->isHeadless = false;

    // 在 Windows 上，nativeWindow 映射到 HWND
    swapChain->hWnd = (HWND) nativeWindow;
    swapChain->hDc = GetDC(swapChain->hWnd);
    if (!swapChain->hDc) {
        DWORD dwError = GetLastError();
        ASSERT_POSTCONDITION_NON_FATAL(swapChain->hDc,
           "Unable to create the SwapChain (nativeWindow = %p)", nativeWindow);
        reportWindowsError(dwError);
    }

	// 我们必须在 HDC 和 HGLRC (mContext) 之间匹配像素格式
    int pixelFormat = ChoosePixelFormat(swapChain->hDc, &mPfd);
    SetPixelFormat(swapChain->hDc, pixelFormat, &mPfd);

    return (Platform::SwapChain*) swapChain;
}

/**
 * 创建交换链（无头模式）
 * 
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * @return 交换链指针
 * 
 * 注意：WS_POPUP 窗口样式是经过实验后选择的。
 * 由于某种原因，使用其他窗口样式在使用 readPixels 时会导致像素缓冲区损坏。
 */
Platform::SwapChain* PlatformWGL::createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept {
    auto* swapChain = new WGLSwapChain();
    swapChain->isHeadless = true;

    // WS_POPUP 窗口样式是经过实验后选择的。
    // 由于某种原因，使用其他窗口样式在使用 readPixels 时会导致像素缓冲区损坏。
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_POPUP, FALSE);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;

    swapChain->hWnd = CreateWindowA("STATIC", "headless", WS_POPUP, 0, 0,
            width, height, NULL, NULL, NULL, NULL);
    swapChain->hDc = GetDC(swapChain->hWnd);
    int pixelFormat = ChoosePixelFormat(swapChain->hDc, &mPfd);
    SetPixelFormat(swapChain->hDc, pixelFormat, &mPfd);

    return (Platform::SwapChain*) swapChain;
}

/**
 * 销毁交换链
 * 
 * @param swapChain 交换链指针
 */
void PlatformWGL::destroySwapChain(Platform::SwapChain* swapChain) noexcept {
    auto* wglSwapChain = (WGLSwapChain*) swapChain;

    HDC dc = wglSwapChain->hDc;
    HWND window = wglSwapChain->hWnd;
    ReleaseDC(window, dc);

    if (wglSwapChain->isHeadless) {
        DestroyWindow(window);
    }

    delete wglSwapChain;

    // 使此交换链不再是当前的（通过使虚拟交换链成为当前的）
    wglMakeCurrent(mWhdc, mContext);
}

/**
 * 设置当前上下文
 * 
 * 使指定的上下文成为当前上下文。
 * 
 * @param type 上下文类型
 * @param drawSwapChain 绘制交换链
 * @param readSwapChain 读取交换链
 * @return 如果成功返回 true，否则返回 false
 */
bool PlatformWGL::makeCurrent(ContextType type, SwapChain* drawSwapChain,
        SwapChain* readSwapChain) {
    ASSERT_PRECONDITION_NON_FATAL(drawSwapChain == readSwapChain,
                                  "PlatformWGL does not support distinct draw/read swap chains.");

    auto* wglSwapChain = (WGLSwapChain*) drawSwapChain;
    HDC hdc = wglSwapChain->hDc;
    if (hdc != NULL) {
        BOOL success = wglMakeCurrent(hdc, mContext);
        if (!success) {
            DWORD dwError = GetLastError();
            ASSERT_POSTCONDITION_NON_FATAL(success, "wglMakeCurrent() failed. hdc = %p", hdc);
            reportWindowsError(dwError);
            wglMakeCurrent(0, NULL);
        }
    }
    return true;
}

/**
 * 提交交换链
 * 
 * 交换前后缓冲区，将渲染结果呈现到屏幕。
 * 
 * @param swapChain 交换链指针
 */
void PlatformWGL::commit(Platform::SwapChain* swapChain) noexcept {
    auto* wglSwapChain = (WGLSwapChain*) swapChain;
    HDC hdc = wglSwapChain->hDc;
    if (hdc != NULL) {
        SwapBuffers(hdc);
    }
}

} // namespace filament::backend
