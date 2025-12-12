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

#include <backend/platforms/PlatformEGL.h>

#include "opengl/GLUtils.h"

#include <backend/platforms/OpenGLPlatform.h>

#include <backend/Platform.h>
#include <backend/DriverEnums.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif
#include <utils/compiler.h>

#include <utils/Invocable.h>
#include <utils/Logger.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <algorithm>
#include <new>
#include <initializer_list>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE
#   define EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE 0x3483
#endif

using namespace utils;

namespace filament::backend {
using namespace backend;

/**
 * EGL 扩展函数指针命名空间
 * 
 * Android NDK 不暴露扩展函数，使用 eglGetProcAddress 获取。
 * 这些函数指针在初始化时通过 eglGetProcAddress 填充。
 */
namespace glext {
UTILS_PRIVATE PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR{}; // NOLINT(*-use-internal-linkage)
UTILS_PRIVATE PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR{}; // NOLINT(*-use-internal-linkage)
UTILS_PRIVATE PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR{}; // NOLINT(*-use-internal-linkage)
UTILS_PRIVATE PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR{}; // NOLINT(*-use-internal-linkage)
UTILS_PRIVATE PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR{}; // NOLINT(*-use-internal-linkage)
}
using namespace glext;

// ---------------------------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------------------------

/**
 * 记录 EGL 错误
 * 
 * 获取当前 EGL 错误并记录。
 * 
 * @param name 操作名称
 */
void PlatformEGL::logEglError(const char* name) noexcept {
    logEglError(name, eglGetError());
}

/**
 * 记录 EGL 错误
 * 
 * 记录指定的 EGL 错误。
 * 
 * @param name 操作名称
 * @param error EGL 错误代码
 */
void PlatformEGL::logEglError(const char* name, EGLint const error) noexcept {
    LOG(ERROR) << name << " failed with " << getEglErrorName(error);
}

/**
 * 获取 EGL 错误名称
 * 
 * 将 EGL 错误代码转换为可读的字符串。
 * 
 * @param error EGL 错误代码
 * @return 错误名称字符串
 */
const char* PlatformEGL::getEglErrorName(EGLint const error) noexcept {
    switch (error) {
        case EGL_NOT_INITIALIZED:       return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:             return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:         return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:           return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:   return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:           return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:           return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:             return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:         return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:     return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:     return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:          return "EGL_CONTEXT_LOST";
        default:                        return "unknown";
    }
}

/**
 * 清除 GL 错误
 * 
 * 清除可能由之前的调用设置的 GL 错误。
 * 如果发现错误，记录警告但继续执行。
 */
void PlatformEGL::clearGlError() noexcept {
    // 清除可能由之前的调用设置的 GL 错误
    GLenum const error = glGetError();
    if (error != GL_NO_ERROR) {
        LOG(WARNING) << "Ignoring pending GL error " << io::hex << error;
    }
}

// ---------------------------------------------------------------------------------------------

/**
 * 构造函数
 * 
 * 默认构造函数。
 */
PlatformEGL::PlatformEGL() noexcept = default;

/**
 * 获取操作系统版本
 * 
 * @return 操作系统版本（EGL 平台返回 0）
 */
int PlatformEGL::getOSVersion() const noexcept {
    return 0;
}

/**
 * 检查是否为 OpenGL（而非 GLES）
 * 
 * @return 如果为 OpenGL 返回 true，否则返回 false（EGL 平台使用 GLES）
 */
bool PlatformEGL::isOpenGL() const noexcept {
    return false;
}

/**
 * 外部图像析构函数
 * 
 * 默认析构函数。
 */
PlatformEGL::ExternalImageEGL::~ExternalImageEGL() = default;

/**
 * 创建驱动
 * 
 * 初始化 EGL 显示并创建 OpenGL 驱动。
 * 
 * @param sharedContext 共享上下文（可选）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针，失败返回 nullptr
 * 
 * 执行流程：
 * 1. 获取 EGL 显示
 * 2. 初始化 EGL（如果失败，尝试使用设备扩展）
 * 3. 导入 GLES 扩展入口点
 * 4. 查询 EGL 扩展
 * 5. 获取 EGL 扩展函数指针
 * 6. 创建上下文和驱动
 */
Driver* PlatformEGL::createDriver(void* sharedContext, const DriverConfig& driverConfig) {
    // 获取默认 EGL 显示
    mEGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert_invariant(mEGLDisplay != EGL_NO_DISPLAY);

    // 初始化 EGL
    EGLint major, minor;
    EGLBoolean initialized = eglInitialize(mEGLDisplay, &major, &minor);

    // 如果初始化失败，尝试使用设备扩展（用于无头渲染）
    if (!initialized) {
        EGLDeviceEXT eglDevice;
        EGLint numDevices;
        PFNEGLQUERYDEVICESEXTPROC const eglQueryDevicesEXT =
                PFNEGLQUERYDEVICESEXTPROC(eglGetProcAddress("eglQueryDevicesEXT"));
        if (eglQueryDevicesEXT != nullptr) {
            eglQueryDevicesEXT(1, &eglDevice, &numDevices);
            if(auto* getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                    eglGetProcAddress("eglGetPlatformDisplay"))) {
                // 使用设备扩展获取平台显示
                mEGLDisplay = getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, eglDevice, nullptr);
                initialized = eglInitialize(mEGLDisplay, &major, &minor);
            }
        }
    }

    if (UTILS_UNLIKELY(!initialized)) {
        LOG(ERROR) << "eglInitialize failed";
        return nullptr;
    }

    // 导入 GLES 扩展入口点（Android 需要）
#if defined(FILAMENT_IMPORT_ENTRY_POINTS)
    importGLESExtensionsEntryPoints();
#endif

    // 查询并解析 EGL 扩展字符串
    auto const extensions = GLUtils::split(eglQueryString(mEGLDisplay, EGL_EXTENSIONS));
    ext.egl.ANDROID_recordable = extensions.has("EGL_ANDROID_recordable");
    ext.egl.KHR_gl_colorspace = extensions.has("EGL_KHR_gl_colorspace");
    ext.egl.KHR_create_context = extensions.has("EGL_KHR_create_context");
    ext.egl.KHR_no_config_context = extensions.has("EGL_KHR_no_config_context");
    ext.egl.KHR_surfaceless_context = extensions.has("EGL_KHR_surfaceless_context");
    ext.egl.EXT_protected_content = extensions.has("EGL_EXT_protected_content");

    // 获取 EGL 同步扩展函数指针
    eglCreateSyncKHR = PFNEGLCREATESYNCKHRPROC(eglGetProcAddress("eglCreateSyncKHR"));
    eglDestroySyncKHR = PFNEGLDESTROYSYNCKHRPROC(eglGetProcAddress("eglDestroySyncKHR"));
    eglClientWaitSyncKHR = PFNEGLCLIENTWAITSYNCKHRPROC(eglGetProcAddress("eglClientWaitSyncKHR"));

    // 获取 EGL 图像扩展函数指针
    eglCreateImageKHR = PFNEGLCREATEIMAGEKHRPROC(eglGetProcAddress("eglCreateImageKHR"));
    eglDestroyImageKHR = PFNEGLDESTROYIMAGEKHRPROC(eglGetProcAddress("eglDestroyImageKHR"));

    EGLint const pbufferAttribs[] = {
            EGL_WIDTH,  1,
            EGL_HEIGHT, 1,
            EGL_NONE
    };

#ifdef __ANDROID__
    bool requestES2Context = driverConfig.forceGLES2Context;
    char property[PROP_VALUE_MAX];
    int const length = __system_property_get("debug.filament.es2", property);
    if (length > 0) {
        requestES2Context = bool(atoi(property));
    }
#else
    constexpr bool requestES2Context = false;
#endif

    Config contextAttribs;

    if (isOpenGL()) {
        // Request a OpenGL 4.1 context
        contextAttribs[EGL_CONTEXT_MAJOR_VERSION] = 4;
        contextAttribs[EGL_CONTEXT_MINOR_VERSION] = 1;
    } else {
        // Request a ES2 context, devices that support ES3 will return an ES3 context
        contextAttribs[EGL_CONTEXT_CLIENT_VERSION] = 2;
    }

    // FOR TESTING ONLY, enforce the ES version we're asking for.
    // FIXME: we should check EGL_ANGLE_create_context_backwards_compatible, however, at least
    //        some versions of ANGLE don't advertise this extension but do support it.
    if (requestES2Context) {
        // TODO: is there a way to request the ANGLE driver if available?
        contextAttribs[EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE] = EGL_FALSE;
    }

#ifdef NDEBUG
    // When we don't have a shared context, and we're in release mode, we always activate the
    // EGL_KHR_create_context_no_error extension.
    if (!sharedContext && extensions.has("EGL_KHR_create_context_no_error")) {
        contextAttribs[EGL_CONTEXT_OPENGL_NO_ERROR_KHR] = EGL_TRUE;
    }
#endif

    // Configure GPU context priority level for scheduling and preemption
    if (driverConfig.gpuContextPriority != GpuContextPriority::DEFAULT) {
        if (extensions.has("EGL_IMG_context_priority")) {
            EGLint priorityLevel = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
            const char* priorityName;
            switch (driverConfig.gpuContextPriority) {
                case GpuContextPriority::DEFAULT:
                    assert_invariant(false);
                    break;
                case GpuContextPriority::LOW:
                    priorityLevel = EGL_CONTEXT_PRIORITY_LOW_IMG;
                    priorityName = "LOW";
                    break;
                case GpuContextPriority::MEDIUM:
                    priorityLevel = EGL_CONTEXT_PRIORITY_MEDIUM_IMG;
                    priorityName = "MEDIUM";
                    break;
                case GpuContextPriority::HIGH:
                    priorityLevel = EGL_CONTEXT_PRIORITY_HIGH_IMG;
                    priorityName = "HIGH";
                    break;
                case GpuContextPriority::REALTIME:
                    priorityLevel = EGL_CONTEXT_PRIORITY_HIGH_IMG;
                    priorityName = "REALTIME(=HIGH)";
                    break;
            }
            contextAttribs[EGL_CONTEXT_PRIORITY_LEVEL_IMG] = priorityLevel;
            LOG(INFO) << "EGL: Enabling GPU context priority: " << priorityName;
        } else {
            LOG(WARNING) << "EGL: GPU context priority requested but not supported";
        }
    }

    // config use for creating the context
    EGLConfig eglConfig = EGL_NO_CONFIG_KHR;

    if (UTILS_UNLIKELY(!ext.egl.KHR_no_config_context)) {
        // find a config we can use if we don't have "EGL_KHR_no_config_context" and that we can use
        // for the dummy pbuffer surface.
        mEGLConfig = findSwapChainConfig(
                SWAP_CHAIN_CONFIG_TRANSPARENT |
                SWAP_CHAIN_HAS_STENCIL_BUFFER,
                true, true);
        if (UTILS_UNLIKELY(mEGLConfig == EGL_NO_CONFIG_KHR)) {
            goto error; // error already logged
        }
        // if we don't have the EGL_KHR_no_config_context the context must be created with
        // the same config as the swapchain, so we have no choice but to create a
        // transparent config.
        eglConfig = mEGLConfig;
    }

    for (size_t tries = 0; tries < 3; tries++) {
        mEGLContext = eglCreateContext(mEGLDisplay, eglConfig,
                sharedContext, contextAttribs.data());
        if (UTILS_LIKELY(mEGLContext != EGL_NO_CONTEXT)) {
            break;
        }

        GLint const error = eglGetError();
        if (error == EGL_BAD_ATTRIBUTE) {
            // ANGLE doesn't always advertise this extension, so we have to try
            contextAttribs.erase(EGL_CONTEXT_OPENGL_BACKWARDS_COMPATIBLE_ANGLE);
            continue;
        }
#ifdef NDEBUG
        else if (error == EGL_BAD_MATCH &&
                   sharedContext && extensions.has("EGL_KHR_create_context_no_error")) {
            // context creation could fail because of EGL_CONTEXT_OPENGL_NO_ERROR_KHR
            // not matching the sharedContext. Try with it.
            contextAttribs[EGL_CONTEXT_OPENGL_NO_ERROR_KHR] = EGL_TRUE;
            continue;
        }
#endif
        (void)error;
        break;
    }

    if (UTILS_UNLIKELY(mEGLContext == EGL_NO_CONTEXT)) {
        // eglCreateContext failed
        logEglError("eglCreateContext");
        goto error;
    }

    if (ext.egl.KHR_surfaceless_context) {
        // Adreno 306 driver advertises KHR_create_context but doesn't support passing
        // EGL_NO_SURFACE to eglMakeCurrent with a 3.0 context.
        if (UTILS_UNLIKELY(!eglMakeCurrent(mEGLDisplay,
                EGL_NO_SURFACE, EGL_NO_SURFACE, mEGLContext))) {
            if (eglGetError() == EGL_BAD_MATCH) {
                ext.egl.KHR_surfaceless_context = false;
            }
        }
    }

    if (UTILS_UNLIKELY(!ext.egl.KHR_surfaceless_context)) {
        // create the dummy surface, just for being able to make the context current.
        mEGLDummySurface = eglCreatePbufferSurface(mEGLDisplay, mEGLConfig, pbufferAttribs);
        if (UTILS_UNLIKELY(mEGLDummySurface == EGL_NO_SURFACE)) {
            logEglError("eglCreatePbufferSurface");
            goto error;
        }
    }

    if (UTILS_UNLIKELY(
            egl.makeCurrent(mEGLContext, mEGLDummySurface, mEGLDummySurface) == EGL_FALSE)) {
        // eglMakeCurrent failed
        logEglError("eglMakeCurrent");
        goto error;
    }

    mCurrentContextType = ContextType::UNPROTECTED;
    mContextAttribs = std::move(contextAttribs);
    mMSAA4XSupport = checkIfMSAASwapChainSupported(4);  // 检查 4x MSAA 支持

    initializeGlExtensions();  // 初始化 GL 扩展

    // 这在较旧的 Android 模拟器/API 级别上是必需的
    clearGlError();

    // 成功！！
    return createDefaultDriver(this, sharedContext, driverConfig);

error:
    // 如果到达这里，说明失败了
    if (mEGLDummySurface) {
        eglDestroySurface(mEGLDisplay, mEGLDummySurface);
    }
    if (mEGLContext) {
        eglDestroyContext(mEGLDisplay, mEGLContext);
    }
    if (mEGLContextProtected) {
        eglDestroyContext(mEGLDisplay, mEGLContextProtected);
    }

    mEGLDummySurface = EGL_NO_SURFACE;
    mEGLContext = EGL_NO_CONTEXT;
    mEGLContextProtected = EGL_NO_CONTEXT;

    eglTerminate(mEGLDisplay);
    eglReleaseThread();

    return nullptr;
}

/**
 * 检查是否支持额外上下文
 * 
 * 检查是否支持无表面上下文（用于多线程编译）。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool PlatformEGL::isExtraContextSupported() const noexcept {
    return ext.egl.KHR_surfaceless_context;
}

/**
 * 检查是否支持受保护上下文
 * 
 * 检查是否支持 EGL_EXT_protected_content 扩展。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool PlatformEGL::isProtectedContextSupported() const noexcept {
    return ext.egl.EXT_protected_content;
}

/**
 * 创建额外上下文
 * 
 * 创建额外的 OpenGL 上下文（用于多线程编译）。
 * 
 * @param shared 是否与主上下文共享
 */
void PlatformEGL::createContext(bool const shared) {
    EGLConfig const config = ext.egl.KHR_no_config_context ? EGL_NO_CONFIG_KHR : mEGLConfig;

    EGLContext const context = eglCreateContext(mEGLDisplay, config,
            shared ? mEGLContext : EGL_NO_CONTEXT, mContextAttribs.data());

    if (UTILS_UNLIKELY(context == EGL_NO_CONTEXT)) {
        // eglCreateContext 失败
        logEglError("eglCreateContext");
    }

    assert_invariant(context != EGL_NO_CONTEXT);

    // 使上下文成为当前上下文（无表面）
    eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, context);

    mAdditionalContexts.push_back(context);
}

/**
 * 释放上下文
 * 
 * 释放当前线程的上下文。
 */
void PlatformEGL::releaseContext() noexcept {
    EGLContext context = eglGetCurrentContext();
    eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) {
        eglDestroyContext(mEGLDisplay, context);
    }

    // 从额外上下文列表中移除
    mAdditionalContexts.erase(
            std::remove_if(mAdditionalContexts.begin(), mAdditionalContexts.end(),
                    [context](EGLContext const c) {
                        return c == context;
                    }), mAdditionalContexts.end());

    eglReleaseThread();
}

/**
 * 终止平台
 * 
 * 清理所有 EGL 资源。
 */
void PlatformEGL::terminate() noexcept {
    // 总是允许使用 EGL_NO_SURFACE, EGL_NO_CONTEXT
    eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (mEGLDummySurface) {
        eglDestroySurface(mEGLDisplay, mEGLDummySurface);
    }
    eglDestroyContext(mEGLDisplay, mEGLContext);
    if (mEGLContextProtected != EGL_NO_CONTEXT) {
        eglDestroyContext(mEGLDisplay, mEGLContextProtected);
    }
    for (auto const context : mAdditionalContexts) {
        eglDestroyContext(mEGLDisplay, context);
    }
    eglTerminate(mEGLDisplay);
    eglReleaseThread();
}

/**
 * 查找交换链配置
 * 
 * 查找符合指定标志的 EGL 配置。
 * 
 * @param flags 交换链标志（透明、模板缓冲区、MSAA 等）
 * @param window 是否支持窗口表面
 * @param pbuffer 是否支持像素缓冲区表面
 * @return EGL 配置，失败返回 EGL_NO_CONFIG_KHR
 */
EGLConfig PlatformEGL::findSwapChainConfig(
        uint64_t const flags, bool const window, bool const pbuffer) const {
    // 查找支持 ES3 的配置
    EGLConfig config = EGL_NO_CONFIG_KHR;
    EGLint configsCount;
    Config configAttribs = {
            { EGL_RED_SIZE,         8 },   // 红色通道 8 位
            { EGL_GREEN_SIZE,       8 },   // 绿色通道 8 位
            { EGL_BLUE_SIZE,        8 },   // 蓝色通道 8 位
            { EGL_ALPHA_SIZE,      (flags & SWAP_CHAIN_CONFIG_TRANSPARENT) ? 8 : 0 },  // Alpha 通道（如果透明）
            { EGL_DEPTH_SIZE,      24 },   // 深度缓冲区 24 位
            { EGL_STENCIL_SIZE,    (flags & SWAP_CHAIN_HAS_STENCIL_BUFFER) ? 8 : 0 }  // 模板缓冲区（如果需要）
    };

    if (!ext.egl.KHR_no_config_context) {
        if (isOpenGL()) {
            configAttribs[EGL_RENDERABLE_TYPE] = EGL_OPENGL_BIT;
        } else {
            configAttribs[EGL_RENDERABLE_TYPE] = EGL_OPENGL_ES2_BIT;
            if (ext.egl.KHR_create_context) {
                configAttribs[EGL_RENDERABLE_TYPE] |= EGL_OPENGL_ES3_BIT_KHR;
            }
        }
    }

    if (window) {
        configAttribs[EGL_SURFACE_TYPE] |= EGL_WINDOW_BIT;
    }

    if (pbuffer) {
        configAttribs[EGL_SURFACE_TYPE] |= EGL_PBUFFER_BIT;
    }

    if (ext.egl.ANDROID_recordable) {
        configAttribs[EGL_RECORDABLE_ANDROID] = EGL_TRUE;
    }

    if (flags & SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES) {
        configAttribs[EGL_SAMPLE_BUFFERS] = 1;
        configAttribs[EGL_SAMPLES] = 4;
    }

    if (UTILS_UNLIKELY(
            !eglChooseConfig(mEGLDisplay, configAttribs.data(), &config, 1, &configsCount))) {
        logEglError("eglChooseConfig");
            return EGL_NO_CONFIG_KHR;
    }

    if (UTILS_UNLIKELY(configsCount == 0)) {
        if (ext.egl.ANDROID_recordable) {
            // warn and retry without EGL_RECORDABLE_ANDROID
            logEglError(
                    "eglChooseConfig(..., EGL_RECORDABLE_ANDROID) failed. Continuing without it.");
            configAttribs[EGL_RECORDABLE_ANDROID] = EGL_DONT_CARE;
            if (UTILS_UNLIKELY(
                    !eglChooseConfig(mEGLDisplay, configAttribs.data(), &config, 1, &configsCount)
                            || configsCount == 0)) {
                logEglError("eglChooseConfig");
                return EGL_NO_CONFIG_KHR;
            }
        } else {
            // we found zero config matching our request!
            logEglError("eglChooseConfig() didn't find any matching config!");
            return EGL_NO_CONFIG_KHR;
        }
    }
    return config;
}

/**
 * 获取适合交换链的配置
 * 
 * 根据标志获取适合的 EGL 配置。
 * 如果支持 KHR_no_config_context，可以为每个交换链选择不同的配置。
 * 
 * @param flags 交换链标志
 * @param window 是否支持窗口表面
 * @param pbuffer 是否支持像素缓冲区表面
 * @return EGL 配置
 */
EGLConfig PlatformEGL::getSuitableConfigForSwapChain(
        uint64_t const flags, bool const window, bool const pbuffer) const {
    EGLConfig config = EGL_NO_CONFIG_KHR;
    if (UTILS_LIKELY(ext.egl.KHR_no_config_context)) {
        // 支持无配置上下文，可以为每个交换链选择不同配置
        config = findSwapChainConfig(flags, window, pbuffer);
    } else {
        // 不支持无配置上下文，必须使用主配置
        config = mEGLConfig;
    }
    return config;
}

// -----------------------------------------------------------------------------------------------

/**
 * 检查是否支持 sRGB 交换链
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool PlatformEGL::isSRGBSwapChainSupported() const noexcept {
    return ext.egl.KHR_gl_colorspace;
}

/**
 * 检查是否支持 MSAA 交换链
 * 
 * @param samples 采样数
 * @return 如果支持返回 true，否则返回 false
 */
bool PlatformEGL::isMSAASwapChainSupported(uint32_t const samples) const noexcept {
    if (samples <= 1) {
        return true;  // 无 MSAA 总是支持
    }

    if (samples == 4) {
        return mMSAA4XSupport;  // 检查缓存的 4x MSAA 支持
    }

    return false;  // 其他采样数不支持
}

/**
 * 创建交换链（从原生窗口）
 * 
 * @param nativeWindow 原生窗口指针
 * @param flags 交换链标志
 * @return 交换链指针
 */
Platform::SwapChain* PlatformEGL::createSwapChain(
        void* nativeWindow, uint64_t const flags) {
    auto* const sc = new(std::nothrow) SwapChainEGL(*this, nativeWindow, flags);
    return sc;
}

/**
 * 创建交换链（无头模式）
 * 
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * @return 交换链指针
 */
Platform::SwapChain* PlatformEGL::createSwapChain(
        uint32_t const width, uint32_t const height, uint64_t const flags) {
    auto* const sc = new(std::nothrow) SwapChainEGL(*this, width, height, flags);
    return sc;
}

/**
 * 销毁交换链
 * 
 * @param swapChain 交换链指针
 */
void PlatformEGL::destroySwapChain(SwapChain* swapChain) noexcept {
    if (swapChain) {
        SwapChainEGL* const sc = static_cast<SwapChainEGL*>(swapChain);
        sc->terminate(*this);
        delete sc;
    }
}

/**
 * 检查交换链是否受保护
 * 
 * @param swapChain 交换链指针
 * @return 如果受保护返回 true，否则返回 false
 */
bool PlatformEGL::isSwapChainProtected(SwapChain* swapChain) noexcept {
    if (swapChain) {
        SwapChainEGL const* const sc = static_cast<SwapChainEGL const*>(swapChain);
        return bool(sc->flags & SWAP_CHAIN_CONFIG_PROTECTED_CONTENT);
    }
    return false;
}

/**
 * 获取当前上下文类型
 * 
 * @return 当前上下文类型（UNPROTECTED 或 PROTECTED）
 */
OpenGLPlatform::ContextType PlatformEGL::getCurrentContextType() const noexcept {
    return mCurrentContextType;
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
bool PlatformEGL::makeCurrent(ContextType const type,
        SwapChain* drawSwapChain, SwapChain* readSwapChain) {
    SwapChainEGL const* const dsc = static_cast<SwapChainEGL const*>(drawSwapChain);
    SwapChainEGL const* const rsc = static_cast<SwapChainEGL const*>(readSwapChain);
    EGLContext const context = getContextForType(type);
    EGLBoolean const success = egl.makeCurrent(context, dsc->sur, rsc->sur);
    return success == EGL_TRUE;
}

/**
 * 设置当前上下文（带回调）
 * 
 * 使指定的上下文成为当前上下文，并在上下文切换时调用回调。
 * 
 * @param drawSwapChain 绘制交换链
 * @param readSwapChain 读取交换链
 * @param preContextChange 上下文切换前回调
 * @param postContextChange 上下文切换后回调（参数为上下文索引）
 * 
 * 执行流程：
 * 1. 检查交换链是否需要受保护上下文
 * 2. 如果需要且不存在，创建受保护上下文
 * 3. 如果上下文类型改变，调用回调并切换上下文
 * 4. 如果不再需要受保护上下文，立即销毁
 */
void PlatformEGL::makeCurrent(SwapChain* drawSwapChain,
        SwapChain* readSwapChain,
        Invocable<void()> preContextChange,
        Invocable<void(size_t index)> postContextChange) {

    assert_invariant(drawSwapChain);
    assert_invariant(readSwapChain);

    ContextType type = ContextType::UNPROTECTED;
    if (ext.egl.EXT_protected_content) {
        bool const swapChainProtected = isSwapChainProtected(drawSwapChain);
        if (UTILS_UNLIKELY(swapChainProtected)) {
            // 我们需要受保护上下文
            if (UTILS_UNLIKELY(mEGLContextProtected == EGL_NO_CONTEXT)) {
                // 我们没有，创建它！
                EGLConfig const config = ext.egl.KHR_no_config_context ? EGL_NO_CONFIG_KHR : mEGLConfig;
                Config protectedContextAttribs{ mContextAttribs };
                protectedContextAttribs[EGL_PROTECTED_CONTENT_EXT] = EGL_TRUE;
                mEGLContextProtected = eglCreateContext(mEGLDisplay, config, mEGLContext,
                        protectedContextAttribs.data());
                if (UTILS_UNLIKELY(mEGLContextProtected == EGL_NO_CONTEXT)) {
                    // 无法创建受保护上下文
                    logEglError("eglCreateContext[EGL_PROTECTED_CONTENT_EXT]");
                    ext.egl.EXT_protected_content = false;
                    goto error;
                }
            }
            type = ContextType::PROTECTED;
            error: ;
        }

        bool const contextChange = type != mCurrentContextType;
        mCurrentContextType = type;

        if (UTILS_UNLIKELY(contextChange)) {
            preContextChange();
            bool const success = makeCurrent(mCurrentContextType, drawSwapChain, readSwapChain);
            if (UTILS_UNLIKELY(!success)) {
                logEglError("PlatformEGL::makeCurrent");
                if (mEGLContextProtected != EGL_NO_CONTEXT) {
                    eglDestroyContext(mEGLDisplay, mEGLContextProtected);
                    mEGLContextProtected = EGL_NO_CONTEXT;
                }
                mCurrentContextType = ContextType::UNPROTECTED;
            }
            if (UTILS_LIKELY(!swapChainProtected && mEGLContextProtected != EGL_NO_CONTEXT)) {
                // 我们不再需要受保护上下文，立即解除绑定并销毁
                eglDestroyContext(mEGLDisplay, mEGLContextProtected);
                mEGLContextProtected = EGL_NO_CONTEXT;
            }
            size_t const contextIndex = (mCurrentContextType == ContextType::PROTECTED) ? 1 : 0;
            postContextChange(contextIndex);
            return;
        }
    }

    bool const success = makeCurrent(mCurrentContextType, drawSwapChain, readSwapChain);
    if (UTILS_UNLIKELY(!success)) {
        logEglError("PlatformEGL::makeCurrent");
    }
}

/**
 * 提交交换链
 * 
 * 交换前后缓冲区，将渲染结果呈现到屏幕。
 * 
 * @param swapChain 交换链指针
 */
void PlatformEGL::commit(SwapChain* swapChain) noexcept {
    if (swapChain) {
        SwapChainEGL const* const sc = static_cast<SwapChainEGL const*>(swapChain);
        if (sc->sur != EGL_NO_SURFACE) {
            eglSwapBuffers(mEGLDisplay, sc->sur);
        }
    }
}

// -----------------------------------------------------------------------------------------------

/**
 * 检查是否可以创建 Fence
 * 
 * EGL 平台总是可以创建 Fence（如果支持扩展）。
 * 
 * @return 总是返回 true
 */
bool PlatformEGL::canCreateFence() noexcept {
    return true;
}

/**
 * 创建 Fence
 * 
 * 创建 EGL 同步 Fence。
 * 
 * @return Fence 指针，失败返回 nullptr
 */
Platform::Fence* PlatformEGL::createFence() noexcept {
    Fence* f = nullptr;
#ifdef EGL_KHR_reusable_sync
    f = (Fence*) eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_FENCE_KHR, nullptr);
#endif
    return f;
}

/**
 * 销毁 Fence
 * 
 * @param fence Fence 指针
 */
void PlatformEGL::destroyFence(Fence* fence) noexcept {
#ifdef EGL_KHR_reusable_sync
    EGLSyncKHR const sync = fence;
    if (sync != EGL_NO_SYNC_KHR) {
        eglDestroySyncKHR(mEGLDisplay, sync);
    }
#endif
}

/**
 * 等待 Fence
 * 
 * 等待 Fence 完成或超时。
 * 
 * @param fence Fence 指针
 * @param timeout 超时时间（纳秒）
 * @return Fence 状态（CONDITION_SATISFIED、TIMEOUT_EXPIRED 或 ERROR）
 */
FenceStatus PlatformEGL::waitFence(
        Fence* fence, uint64_t const timeout) noexcept {
#ifdef EGL_KHR_reusable_sync
    EGLSyncKHR const sync = fence;
    if (sync != EGL_NO_SYNC_KHR) {
        EGLint const status = eglClientWaitSyncKHR(mEGLDisplay, sync, 0, EGLTimeKHR(timeout));
        if (status == EGL_CONDITION_SATISFIED_KHR) {
            return FenceStatus::CONDITION_SATISFIED;
        }
        if (status == EGL_TIMEOUT_EXPIRED_KHR) {
            return FenceStatus::TIMEOUT_EXPIRED;
        }
    }
#endif
    return FenceStatus::ERROR;
}

// -----------------------------------------------------------------------------------------------

/**
 * 创建外部图像纹理
 * 
 * 创建用于外部图像（EGL 图像）的纹理对象。
 * 
 * @return 外部纹理指针
 */
OpenGLPlatform::ExternalTexture* PlatformEGL::createExternalImageTexture() noexcept {
    ExternalTexture* outTexture = new(std::nothrow) ExternalTexture{};
    glGenTextures(1, &outTexture->id);
    return outTexture;
}

/**
 * 销毁外部图像纹理
 * 
 * @param texture 外部纹理指针
 */
void PlatformEGL::destroyExternalImageTexture(ExternalTexture* texture) noexcept {
    glDeleteTextures(1, &texture->id);
    delete texture;
}

/**
 * 设置外部图像
 * 
 * 将 EGL 图像附加到纹理。
 * 
 * @param externalImage EGL 图像指针
 * @param texture 外部纹理指针
 * @return 如果成功返回 true
 * 
 * 注意：
 * - 如果目标是 TEXTURE_EXTERNAL_OES，必须存在 OES_EGL_image_external_essl3
 * - 如果使用 TEXTURE_2D，必须存在 GL_OES_EGL_image
 */
bool PlatformEGL::setExternalImage(void* externalImage,
        UTILS_UNUSED_IN_RELEASE ExternalTexture* texture) noexcept {

    // 如果目标是 TEXTURE_EXTERNAL_OES，必须存在 OES_EGL_image_external_essl3
    // 如果使用 TEXTURE_2D，必须存在 GL_OES_EGL_image

#if defined(GL_OES_EGL_image) || defined(GL_OES_EGL_image_external_essl3)
        // 纹理保证在此处已绑定
        glEGLImageTargetTexture2DOES(texture->target,
                static_cast<GLeglImageOES>(externalImage));
#endif

    return true;
}

/**
 * 创建外部图像
 * 
 * 从 EGL 图像创建外部图像句柄。
 * 
 * @param eglImage EGL 图像
 * @return 外部图像句柄
 */
Platform::ExternalImageHandle PlatformEGL::createExternalImage(EGLImageKHR const eglImage) noexcept {
    auto* const p = new(std::nothrow) ExternalImageEGL;
    p->eglImage = eglImage;
    return ExternalImageHandle{p};
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
bool PlatformEGL::setExternalImage(ExternalImageHandleRef externalImage,
        UTILS_UNUSED_IN_RELEASE ExternalTexture* texture) noexcept {
    auto const* const eglExternalImage = static_cast<ExternalImageEGL const*>(externalImage.get());
    return setExternalImage(eglExternalImage->eglImage, texture);
}

// -----------------------------------------------------------------------------------------------

/**
 * 初始化 GL 扩展
 * 
 * 查询并初始化 OpenGL 扩展标志。
 */
void PlatformEGL::initializeGlExtensions() noexcept {
    // 我们保证在 ES 平台上，因为我们使用 EGL
    const char* const extensions = (const char*)glGetString(GL_EXTENSIONS);
    GLUtils::unordered_string_set const glExtensions = GLUtils::split(extensions);
    ext.gl.OES_EGL_image_external_essl3 = glExtensions.has("GL_OES_EGL_image_external_essl3");
}

/**
 * 根据类型获取上下文
 * 
 * @param type 上下文类型
 * @return EGL 上下文
 */
EGLContext PlatformEGL::getContextForType(ContextType const type) const noexcept {
    switch (type) {
        case ContextType::NONE:
            return EGL_NO_CONTEXT;
        case ContextType::UNPROTECTED:
            return mEGLContext;
        case ContextType::PROTECTED:
            return mEGLContextProtected;
    }
    return EGL_NO_CONTEXT;
}

/**
 * 检查是否支持 MSAA 交换链
 * 
 * 检索配置以查看是否支持给定数量的采样。结果被缓存。
 * 
 * @param samples 采样数
 * @return 如果支持返回 true，否则返回 false
 */
bool PlatformEGL::checkIfMSAASwapChainSupported(uint32_t const samples) const noexcept {
    // 检索配置以查看是否支持给定数量的采样。结果被缓存。
    Config configAttribs = {
            { EGL_SURFACE_TYPE,    EGL_WINDOW_BIT | EGL_PBUFFER_BIT },  // 支持窗口和像素缓冲区
            { EGL_RED_SIZE,        8 },
            { EGL_GREEN_SIZE,      8 },
            { EGL_BLUE_SIZE,       8 },
            { EGL_DEPTH_SIZE,      24 },
            { EGL_SAMPLE_BUFFERS,  1 },   // 启用多重采样
            { EGL_SAMPLES,         EGLint(samples) },  // 采样数
    };

    if (!ext.egl.KHR_no_config_context) {
        if (isOpenGL()) {
            configAttribs[EGL_RENDERABLE_TYPE] = EGL_OPENGL_BIT;
        } else {
            configAttribs[EGL_RENDERABLE_TYPE] = EGL_OPENGL_ES2_BIT;
            if (ext.egl.KHR_create_context) {
                configAttribs[EGL_RENDERABLE_TYPE] |= EGL_OPENGL_ES3_BIT_KHR;
            }
        }
    }

    EGLConfig config = EGL_NO_CONFIG_KHR;
    EGLint configsCount;
    if (!eglChooseConfig(mEGLDisplay, configAttribs.data(), &config, 1, &configsCount)) {
        return false;
    }

    return configsCount > 0;
}

// ---------------------------------------------------------------------------------------------
// PlatformEGL::SwapChainEGL

/**
 * SwapChainEGL 构造函数（从原生窗口）
 * 
 * 创建 EGL 窗口表面交换链。
 * 
 * @param platform 平台引用
 * @param nativeWindow 原生窗口指针
 * @param flags 交换链标志
 * 
 * 执行流程：
 * 1. 移除不支持功能的标志（sRGB、受保护内容、MSAA）
 * 2. 获取适合的 EGL 配置
 * 3. 创建 EGL 窗口表面
 * 4. 设置交换行为为 EGL_BUFFER_DESTROYED
 */
PlatformEGL::SwapChainEGL::SwapChainEGL(PlatformEGL const& platform, void* nativeWindow, uint64_t flags) {
    // 移除不支持功能的标志
    if (platform.isSRGBSwapChainSupported()) {
        if (flags & SWAP_CHAIN_CONFIG_SRGB_COLORSPACE) {
            attribs[EGL_GL_COLORSPACE_KHR] = EGL_GL_COLORSPACE_SRGB_KHR;
        }
    } else {
        flags &= ~SWAP_CHAIN_CONFIG_SRGB_COLORSPACE;
    }

    if (platform.isProtectedContextSupported()) {
        if (flags & SWAP_CHAIN_CONFIG_PROTECTED_CONTENT) {
            attribs[EGL_PROTECTED_CONTENT_EXT] = EGL_TRUE;
        }
    } else {
        flags &= ~SWAP_CHAIN_CONFIG_PROTECTED_CONTENT;
    }

    if (flags & SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES) {
        if (!platform.isMSAASwapChainSupported(4)) {
            flags &= ~SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES;
        }
    }

    // 检索给定标志的配置
    config = platform.getSuitableConfigForSwapChain(flags, true, false);

    sur = EGL_NO_SURFACE;
    if (UTILS_LIKELY(config != EGL_NO_CONFIG_KHR)) {
        EGLDisplay const dpy = platform.getEglDisplay();
        sur = eglCreateWindowSurface(dpy, config,
                EGLNativeWindowType(nativeWindow), attribs.data());

        if (UTILS_LIKELY(sur != EGL_NO_SURFACE)) {
            // 这不是致命的
            eglSurfaceAttrib(dpy, sur, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED);
        } else {
            logEglError("PlatformEGL::createSwapChain: eglCreateWindowSurface");
        }
    } else {
        // 错误已记录
    }
    this->nativeWindow = EGLNativeWindowType(nativeWindow);
    this->flags = flags;
}

/**
 * SwapChainEGL 构造函数（无头模式）
 * 
 * 创建 EGL 像素缓冲区表面交换链。
 * 
 * @param platform 平台引用
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * 
 * 执行流程：
 * 1. 设置像素缓冲区属性（宽度、高度）
 * 2. 移除不支持功能的标志（sRGB、受保护内容）
 * 3. 获取适合的 EGL 配置
 * 4. 创建 EGL 像素缓冲区表面
 */
PlatformEGL::SwapChainEGL::SwapChainEGL(PlatformEGL const& platform,
        uint32_t const width, uint32_t const height, uint64_t flags) {
    attribs = {
        { EGL_WIDTH, EGLint(width) },
        { EGL_HEIGHT, EGLint(height) },
    };

    if (platform.ext.egl.KHR_gl_colorspace) {
        if (flags & SWAP_CHAIN_CONFIG_SRGB_COLORSPACE) {
            attribs[EGL_GL_COLORSPACE_KHR] = EGL_GL_COLORSPACE_SRGB_KHR;
        }
    } else {
        flags &= ~SWAP_CHAIN_CONFIG_SRGB_COLORSPACE;
    }

    if (platform.ext.egl.EXT_protected_content) {
        if (flags & SWAP_CHAIN_CONFIG_PROTECTED_CONTENT) {
            attribs[EGL_PROTECTED_CONTENT_EXT] = EGL_TRUE;
        }
    } else {
        flags &= ~SWAP_CHAIN_CONFIG_PROTECTED_CONTENT;
    }

    config = platform.getSuitableConfigForSwapChain(flags, false, true);

    sur = EGL_NO_SURFACE;
    if (UTILS_LIKELY(config != EGL_NO_CONFIG_KHR)) {
        EGLDisplay const dpy = platform.getEglDisplay();
        sur = eglCreatePbufferSurface(dpy, config, attribs.data());
        if (UTILS_UNLIKELY(sur == EGL_NO_SURFACE)) {
            logEglError("PlatformEGL::createSwapChain: eglCreatePbufferSurface");
        }
    } else {
        // 错误已记录
    }
    this->flags = flags;
}

/**
 * 终止交换链
 * 
 * 销毁 EGL 表面。
 * 
 * @param platform 平台引用
 * 
 * 注意：
 * - 如果支持 EGL_KHR_surfaceless_context，mEGLDummySurface 是 EGL_NO_SURFACE
 * - 这实际上有点过于激进，但这是一个罕见的操作
 */
void PlatformEGL::SwapChainEGL::terminate(PlatformEGL& platform) {
    if (sur != EGL_NO_SURFACE) {
        // - 如果支持 EGL_KHR_surfaceless_context，mEGLDummySurface 是 EGL_NO_SURFACE
        // - 这实际上有点过于激进，但这是一个罕见的操作
        platform.egl.makeCurrent(platform.mEGLDummySurface, platform.mEGLDummySurface);
        eglDestroySurface(platform.mEGLDisplay, sur);
        sur = EGL_NO_SURFACE;
    }
}

// ---------------------------------------------------------------------------------------------
// PlatformEGL::Config

PlatformEGL::Config::Config() = default;

PlatformEGL::Config::Config(std::initializer_list<std::pair<EGLint, EGLint>> const list)
        : mConfig(list) {
    mConfig.emplace_back(EGL_NONE, EGL_NONE);
}

EGLint& PlatformEGL::Config::operator[](EGLint name) {
    auto pos = std::find_if(mConfig.begin(), mConfig.end(),
            [name](auto&& v) { return v.first == name; });
    if (pos == mConfig.end()) {
        mConfig.insert(pos - 1, { name, 0 });
        pos = mConfig.end() - 2;
    }
    return pos->second;
}

EGLint PlatformEGL::Config::operator[](EGLint name) const {
    auto const pos = std::find_if(mConfig.begin(), mConfig.end(),
            [name](auto&& v) { return v.first == name; });
    assert_invariant(pos != mConfig.end());
    return pos->second;
}

void PlatformEGL::Config::erase(EGLint name) noexcept {
    if (name != EGL_NONE) {
        auto const pos = std::find_if(mConfig.begin(), mConfig.end(),
                [name](auto&& v) { return v.first == name; });
        if (pos != mConfig.end()) {
            mConfig.erase(pos);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// PlatformEGL::EGL

EGLBoolean PlatformEGL::EGL::makeCurrent(EGLContext const context, EGLSurface const drawSurface,
        EGLSurface const readSurface) {
    if (UTILS_UNLIKELY((
            mCurrentContext != context ||
            drawSurface != mCurrentDrawSurface || readSurface != mCurrentReadSurface))) {
        EGLBoolean const success = eglMakeCurrent(
                mEGLDisplay, drawSurface, readSurface, context);
        if (success) {
            mCurrentDrawSurface = drawSurface;
            mCurrentReadSurface = readSurface;
            mCurrentContext = context;
        }
        return success;
    }
    return EGL_TRUE;
}

} // namespace filament::backend
