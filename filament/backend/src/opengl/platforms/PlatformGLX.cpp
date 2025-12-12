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

#include <backend/platforms/PlatformGLX.h>

#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/ThreadUtils.h>

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include <dlfcn.h>

#include <mutex>

// 库名称定义
#define LIBRARY_GLX "libGL.so.1"  // GLX 库
#define LIBRARY_X11 "libX11.so.6"  // X11 库

// X11 函数指针类型
typedef Display* (* X11_OPEN_DISPLAY)(const char*);
typedef Display* (* X11_CLOSE_DISPLAY)(Display*);
typedef int (* X11_FREE)(void*);

// GLX 函数指针类型
typedef void (* GLX_DESTROY_CONTEXT)(Display*, GLXContext);
typedef void (* GLX_SWAP_BUFFERS)(Display* dpy, GLXDrawable drawable);

/**
 * GLX 函数结构
 * 
 * 存储 GLX 函数指针和系统 GLX 库的句柄。
 * 使用动态加载以避免硬链接依赖。
 */
struct GLXFunctions {
    PFNGLXCHOOSEFBCONFIGPROC chooseFbConfig;        // 选择帧缓冲区配置
    PFNGLXCREATECONTEXTATTRIBSARBPROC createContext; // 创建上下文（带属性）
    PFNGLXCREATEPBUFFERPROC createPbuffer;          // 创建像素缓冲区
    PFNGLXDESTROYPBUFFERPROC destroyPbuffer;         // 销毁像素缓冲区
    PFNGLXMAKECONTEXTCURRENTPROC setCurrentContext; // 设置当前上下文

    /**
     * 查询上下文
     * 
     * 创建共享 GL 上下文时，我们查询使用的 GLX_FBCONFIG_ID
     * 以确保我们的显示帧缓冲区属性匹配；否则使我们的上下文
     * 成为当前上下文会导致 BadMatch。
     * https://gist.github.com/roxlu/c282d642c353ce96ef19b6359c741bcb
     */
    PFNGLXQUERYCONTEXTPROC queryContext;

    /**
     * 获取帧缓冲区配置
     * 
     * 创建共享 GL 上下文时，我们选择与共享 GL 上下文匹配的
     * GLXFBConfig。`getFBConfigs` 将返回所有可用的 GLXFBConfigs。
     */
    PFNGLXGETFBCONFIGSPROC getFbConfigs;

    /**
     * 获取帧缓冲区配置属性
     * 
     * 创建共享 GL 上下文时，我们遍历 `getFBConfigs` 返回的
     * 可用 GLXFBConfigs，使用 `getFbConfigAttrib` 查找匹配的
     * `GLX_FBCONFIG_ID`。
     */
    PFNGLXGETFBCONFIGATTRIBPROC getFbConfigAttrib;

    GLX_DESTROY_CONTEXT destroyContext;  // 销毁上下文
    GLX_SWAP_BUFFERS swapBuffers;        // 交换缓冲区
    void* library;                        // 库句柄
} g_glx;

/**
 * X11 函数结构
 * 
 * 存储 X11 函数指针和系统 X11 库的句柄。
 */
struct X11Functions {
    X11_OPEN_DISPLAY openDisplay;   // 打开显示
    X11_CLOSE_DISPLAY closeDisplay; // 关闭显示
    X11_FREE free;                  // 释放内存
    void* library;                   // 库句柄
} g_x11;

/**
 * 获取函数地址
 * 
 * GLX 函数地址获取函数指针。
 */
static PFNGLXGETPROCADDRESSPROC getProcAddress;

/**
 * 加载库
 * 
 * 动态加载 GLX 和 X11 库，并获取函数指针。
 * 
 * @return 如果成功返回 true，否则返回 false
 */
static bool loadLibraries() {
    // 加载 GLX 库
    g_glx.library = dlopen(LIBRARY_GLX, RTLD_LOCAL | RTLD_NOW);
    if (!g_glx.library) {
        LOG(ERROR) << "Could not find library " << LIBRARY_GLX;
        return false;
    }

    // 获取 glXGetProcAddressARB 函数指针
    getProcAddress =
            (PFNGLXGETPROCADDRESSPROC)dlsym(g_glx.library, "glXGetProcAddressARB");

    // 获取 GLX 函数指针
    g_glx.chooseFbConfig = (PFNGLXCHOOSEFBCONFIGPROC)
            getProcAddress((const GLubyte*)"glXChooseFBConfig");
    g_glx.createContext = (PFNGLXCREATECONTEXTATTRIBSARBPROC)
            getProcAddress((const GLubyte*)"glXCreateContextAttribsARB");
    g_glx.createPbuffer = (PFNGLXCREATEPBUFFERPROC)
            getProcAddress((const GLubyte*)"glXCreatePbuffer");
    g_glx.destroyPbuffer = (PFNGLXDESTROYPBUFFERPROC)
            getProcAddress((const GLubyte*)"glXDestroyPbuffer");
    g_glx.setCurrentContext = (PFNGLXMAKECONTEXTCURRENTPROC)
            getProcAddress((const GLubyte*)"glXMakeContextCurrent");
    g_glx.destroyContext = (GLX_DESTROY_CONTEXT)
            getProcAddress((const GLubyte*)"glXDestroyContext");
    g_glx.swapBuffers = (GLX_SWAP_BUFFERS)
            getProcAddress((const GLubyte*)"glXSwapBuffers");

    g_glx.queryContext = (PFNGLXQUERYCONTEXTPROC)
            getProcAddress((const GLubyte*)"glXQueryContext");
    g_glx.getFbConfigs = (PFNGLXGETFBCONFIGSPROC)
            getProcAddress((const GLubyte*)"glXGetFBConfigs");
    g_glx.getFbConfigAttrib = (PFNGLXGETFBCONFIGATTRIBPROC)
            getProcAddress((const GLubyte*)"glXGetFBConfigAttrib");

    g_x11.library = dlopen(LIBRARY_X11, RTLD_LOCAL | RTLD_NOW);
    if (!g_x11.library) {
        LOG(ERROR) << "Could not find library " << LIBRARY_X11;
        return false;
    }

    g_x11.openDisplay = (X11_OPEN_DISPLAY)dlsym(g_x11.library, "XOpenDisplay");
    g_x11.closeDisplay = (X11_CLOSE_DISPLAY)dlsym(g_x11.library, "XCloseDisplay");
    g_x11.free = (X11_FREE)dlsym(g_x11.library, "XFree");
    return true;
}

namespace filament::backend {

using namespace backend;

Driver* PlatformGLX::createDriver(void* sharedGLContext,
        const DriverConfig& driverConfig) {
    loadLibraries();
    // Get the display device
    mGLXDisplay = g_x11.openDisplay(NULL);
    if (mGLXDisplay == nullptr) {
        LOG(ERROR) << "Failed to open X display. (exiting).";
        exit(1);
    }

    if (sharedGLContext != nullptr) {
        int r = -1;
        int usedFbId = -1;
        GLXContext sharedCtx = (GLXContext)((void*)sharedGLContext);

        r = g_glx.queryContext(mGLXDisplay, sharedCtx, GLX_FBCONFIG_ID, &usedFbId);
        if (r != 0) {
            LOG(ERROR) << "Failed to get GLX_FBCONFIG_ID from shared GL context.";
            return nullptr;
        }

        int numConfigs = 0;
        GLXFBConfig* fbConfigs = g_glx.getFbConfigs(mGLXDisplay, 0, &numConfigs);
        if (fbConfigs == nullptr) {
            LOG(ERROR) << "Failed to get the available GLXFBConfigs.";
            return nullptr;
        }

        for (int i = 0; i < numConfigs; ++i) {
            int fbId = 0;
            r = g_glx.getFbConfigAttrib(mGLXDisplay, fbConfigs[i], GLX_FBCONFIG_ID, &fbId);
            if (r != 0) {
                LOG(ERROR) << "Failed to get GLX_FBCONFIG_ID for entry " << i << ".";
                continue;
            }

            if (fbId == usedFbId) {
                mGLXConfig = fbConfigs[i];
                break;
            }
        }

        g_x11.free(fbConfigs);

        if (!mGLXConfig) {
            LOG(ERROR) << "Failed to find an `GLXFBConfig` with the requested ID.";
            return nullptr;
        }
    } else {
        // Create a context
        static int attribs[] = {
                GLX_DOUBLEBUFFER, True,
                GLX_DEPTH_SIZE, 24,
                None
        };

        int configCount = 0;
        GLXFBConfig* fbConfigs = g_glx.chooseFbConfig(mGLXDisplay, DefaultScreen(mGLXDisplay),
                attribs, &configCount);
        if (fbConfigs == nullptr || configCount == 0) {
            return nullptr;
        }

        mGLXConfig = fbConfigs[0];
        g_x11.free(fbConfigs);
    }

    if (g_glx.createContext == nullptr) {
        LOG(INFO) << "Unable to retrieve function pointer for `glXCreateContextAttribs()`.";
        return nullptr;
    }

    int contextAttribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
            GLX_CONTEXT_MINOR_VERSION_ARB, 1,
            GL_NONE
    };

    mGLXContext = g_glx.createContext(mGLXDisplay, mGLXConfig,
            (GLXContext)sharedGLContext, True, contextAttribs);

    int pbufferAttribs[] = {
            GLX_PBUFFER_WIDTH, 1,
            GLX_PBUFFER_HEIGHT, 1,
            GL_NONE
    };

    mDummySurface = g_glx.createPbuffer(mGLXDisplay, mGLXConfig, pbufferAttribs);
    g_glx.setCurrentContext(mGLXDisplay, mDummySurface, mDummySurface, mGLXContext);

    int result = bluegl::bind();
    FILAMENT_CHECK_POSTCONDITION(!result) << "Unable to load OpenGL entry points.";

    return OpenGLPlatform::createDefaultDriver(this, sharedGLContext, driverConfig);
}

void PlatformGLX::terminate() noexcept {
    g_glx.setCurrentContext(mGLXDisplay, None, None, nullptr);
    g_glx.destroyPbuffer(mGLXDisplay, mDummySurface);
    for (auto it : mAdditionalContexts) {
        g_glx.destroyContext(mGLXDisplay, it.second);
    }
    g_glx.destroyContext(mGLXDisplay, mGLXContext);
    g_x11.closeDisplay(mGLXDisplay);
    bluegl::unbind();
}

bool PlatformGLX::isExtraContextSupported() const noexcept {
    return true;
}

void PlatformGLX::createContext(bool shared) {
    std::thread::id currentThreadId = utils::ThreadUtils::getThreadId();

    {
        std::shared_lock<std::shared_mutex> lock(mAdditionalContextsLock);
        auto it = mAdditionalContexts.find(currentThreadId);
        if (it != mAdditionalContexts.end()) {
            LOG(WARNING) << "Shared context is already created";
            return;
        }
    }

    int contextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
        GLX_CONTEXT_MINOR_VERSION_ARB, 1,
        GL_NONE
    };

    GLXContext context = g_glx.createContext(mGLXDisplay, mGLXConfig,
            mGLXContext, True, contextAttribs);
    if (!context) {
        LOG(ERROR) << "Failed to create shared context";
        return;
    }

    g_glx.setCurrentContext(mGLXDisplay,
            (GLXDrawable)None, (GLXDrawable)None, context);

    std::lock_guard<std::shared_mutex> lock(mAdditionalContextsLock);
    mAdditionalContexts[currentThreadId] = context;
}

void PlatformGLX::releaseContext() noexcept {
    g_glx.setCurrentContext(mGLXDisplay,
            (GLXDrawable)None, (GLXDrawable)None, nullptr);

    std::thread::id currentThreadId = utils::ThreadUtils::getThreadId();
    GLXContext context;
    {
        std::lock_guard<std::shared_mutex> lock(mAdditionalContextsLock);
        auto it = mAdditionalContexts.find(currentThreadId);
        if (it == mAdditionalContexts.end()) {
            LOG(WARNING) << "Attempted to destroy non-existing shared context";
            return;
        }
        context = it->second;
        mAdditionalContexts.erase(it);
    }

    g_glx.destroyContext(mGLXDisplay, context);
}

Platform::SwapChain* PlatformGLX::createSwapChain(void* nativeWindow, uint64_t flags) noexcept {
    return (SwapChain*)nativeWindow;
}

Platform::SwapChain* PlatformGLX::createSwapChain(
        uint32_t width, uint32_t height, uint64_t flags) noexcept {
    int pbufferAttribs[] = {
            GLX_PBUFFER_WIDTH, int(width),
            GLX_PBUFFER_HEIGHT, int(height),
            GL_NONE
    };
    GLXPbuffer sur = g_glx.createPbuffer(mGLXDisplay, mGLXConfig, pbufferAttribs);
    if (sur) {
        mPBuffers.push_back(sur);
    }
    return (SwapChain*)sur;
}

void PlatformGLX::destroySwapChain(Platform::SwapChain* swapChain) noexcept {
    auto it = std::find(mPBuffers.begin(), mPBuffers.end(), (GLXPbuffer)swapChain);
    if (it != mPBuffers.end()) {
        g_glx.destroyPbuffer(mGLXDisplay, (GLXPbuffer)swapChain);
        mPBuffers.erase(it);
    }
}

bool PlatformGLX::makeCurrent(ContextType type, SwapChain* drawSwapChain,
        SwapChain* readSwapChain) {
    g_glx.setCurrentContext(mGLXDisplay,
            (GLXDrawable)drawSwapChain, (GLXDrawable)readSwapChain, mGLXContext);
    return true;
}

void PlatformGLX::commit(Platform::SwapChain* swapChain) noexcept {
    g_glx.swapBuffers(mGLXDisplay, (GLXDrawable)swapChain);
}

} // namespace filament::backend
