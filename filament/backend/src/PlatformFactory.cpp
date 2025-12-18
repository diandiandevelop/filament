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

#include <private/backend/PlatformFactory.h>

#include <private/utils/Tracing.h>

#include <utils/debug.h>

// We need to keep this up top for the linux (X11) name collisions.
#if defined(FILAMENT_SUPPORTS_WEBGPU)
    #if defined(__ANDROID__)
        #include "backend/platforms/WebGPUPlatformAndroid.h"
    #elif defined(__APPLE__)
        #include "backend/platforms/WebGPUPlatformApple.h"
    #elif defined(__linux__)
        #include "backend/platforms/WebGPUPlatformLinux.h"
    #elif defined(WIN32)
        #include "backend/platforms/WebGPUPlatformWindows.h"
    #endif
#endif

#if defined(__ANDROID__)
    #include <sys/system_properties.h>
    #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
        #include "backend/platforms/PlatformEGLAndroid.h"
    #endif
#elif defined(FILAMENT_IOS)
    #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
        #include "backend/platforms/PlatformCocoaTouchGL.h"
    #endif
#elif defined(__APPLE__)
    #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
        #if defined(FILAMENT_SUPPORTS_OSMESA)
            #include <backend/platforms/PlatformOSMesa.h>
        #else
            #include <backend/platforms/PlatformCocoaGL.h>
        #endif
    #endif
#elif defined(__linux__)
    #if defined(FILAMENT_SUPPORTS_X11)
        #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
            #include "backend/platforms/PlatformGLX.h"
        #endif
    #elif defined(FILAMENT_SUPPORTS_EGL_ON_LINUX)
        #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
            #include "backend/platforms/PlatformEGLHeadless.h"
        #endif
    #elif defined(FILAMENT_SUPPORTS_OSMESA)
        #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
            #include "backend/platforms/PlatformOSMesa.h"
        #endif
    #endif
#elif defined(WIN32)
    #if defined(FILAMENT_SUPPORTS_OPENGL) && !defined(FILAMENT_USE_EXTERNAL_GLES3)
        #include "backend/platforms/PlatformWGL.h"
    #endif
#elif defined(__EMSCRIPTEN__)
    #include "backend/platforms/PlatformWebGL.h"
#endif

#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
    #if defined(__ANDROID__)
        #include "backend/platforms/VulkanPlatformAndroid.h"
    #elif defined(__APPLE__)
        #include "backend/platforms/VulkanPlatformApple.h"
    #elif defined(__linux__)
        #include "backend/platforms/VulkanPlatformLinux.h"
    #elif defined(WIN32)
        #include "backend/platforms/VulkanPlatformWindows.h"
    #endif
#endif

#if defined (FILAMENT_SUPPORTS_METAL)
namespace filament::backend {
filament::backend::Platform* createDefaultMetalPlatform();
}
#endif

#include "noop/PlatformNoop.h"

namespace filament::backend {

/**
 * 创建平台特定的 Platform 对象
 * 
 * 根据平台和后端类型自动选择合适的 Platform 实现。
 * 调用者拥有返回的 Platform 对象，负责销毁它。
 * 后端 API 的初始化延迟到 createDriver() 调用时。
 * 
 * @param backend 后端类型（输入/输出参数）
 *                - 输入：期望的后端类型（可以是 DEFAULT）
 *                - 输出：解析后的实际后端类型
 * @return Platform 指针，失败返回 nullptr
 */
Platform* PlatformFactory::create(Backend* backend) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    assert_invariant(backend);

#if defined(__ANDROID__)
    /**
     * Android 特定：检查系统属性覆盖
     * 
     * 允许通过系统属性覆盖后端选择，用于调试和测试。
     * 设置方法：setprop debug.filament.backend <backend_id>
     */
    char scratch[PROP_VALUE_MAX + 1];
    int length = __system_property_get("debug.filament.backend", scratch);
    if (length > 0) {
        *backend = Backend(atoi(scratch));  // 使用系统属性指定的后端
    }
#endif

    /**
     * 解析 DEFAULT 后端
     * 
     * 如果后端为 DEFAULT，根据平台自动选择最合适的后端：
     * - Web (Emscripten): OpenGL (WebGL)
     * - Android: OpenGL (OpenGL ES)
     * - iOS/macOS: Metal（Apple 平台首选）
     * - Linux/Windows: Vulkan（如果支持）或 OpenGL
     * - 其他: OpenGL（默认回退）
     */
    if (*backend == Backend::DEFAULT) {
#if defined(__EMSCRIPTEN__)
        *backend = Backend::OPENGL;  // Web 平台使用 WebGL
#elif defined(__ANDROID__)
        *backend = Backend::OPENGL;  // Android 默认使用 OpenGL ES
#elif defined(FILAMENT_IOS) || defined(__APPLE__)
        *backend = Backend::METAL;   // Apple 平台默认使用 Metal
#elif defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
        *backend = Backend::VULKAN;  // Linux/Windows 优先使用 Vulkan
#else
        *backend = Backend::OPENGL;  // 默认回退到 OpenGL
#endif
    }
    /**
     * 创建对应的 Platform 实例
     * 
     * 根据解析后的后端类型和平台创建相应的 Platform 对象。
     */
    
    // No-op 后端（用于测试）
    if (*backend == Backend::NOOP) {
        return new PlatformNoop();
    }
    
    // Vulkan 后端
    if (*backend == Backend::VULKAN) {
        #if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
            // 根据平台创建对应的 Vulkan Platform
            #if defined(__ANDROID__)
                return new VulkanPlatformAndroid();  // Android Vulkan Platform
            #elif defined(__APPLE__)
                return new VulkanPlatformApple();    // Apple Vulkan Platform
            #elif defined(__linux__)
                return new VulkanPlatformLinux();    // Linux Vulkan Platform
            #elif defined(WIN32)
                return new VulkanPlatformWindows();  // Windows Vulkan Platform
            #else
                return nullptr;  // 平台不支持 Vulkan
            #endif
        #else
            return nullptr;  // Vulkan 后端未编译
        #endif
    }
    
    // WebGPU 后端
    if (*backend == Backend::WEBGPU) {
        #if defined(FILAMENT_SUPPORTS_WEBGPU)
            #if defined(__ANDROID__)
                return new WebGPUPlatformAndroid();
            #elif defined(__APPLE__)
                return new WebGPUPlatformApple();
            #elif defined(__linux__)
                return new WebGPUPlatformLinux();
            #elif defined(WIN32)
                return new WebGPUPlatformWindows();
            #else
                 return nullptr;
            #endif
        #else
            return nullptr;  // WebGPU 后端未编译
        #endif
    }
    
    // Metal 后端（Apple 平台）
    if (*backend == Backend::METAL) {
#if defined(FILAMENT_SUPPORTS_METAL)
        return createDefaultMetalPlatform();  // 创建默认 Metal Platform
#else
        return nullptr;  // Metal 后端未编译
#endif
    }
    
    // OpenGL 后端（默认或显式指定）
    assert_invariant(*backend == Backend::OPENGL);
    
    /**
     * 创建 OpenGL Platform
     * 
     * 根据平台选择对应的 OpenGL Platform 实现：
     * - Android: EGL (PlatformEGLAndroid)
     * - iOS: Cocoa Touch GL (PlatformCocoaTouchGL)
     * - macOS: Cocoa GL (PlatformCocoaGL) 或 OSMesa (PlatformOSMesa)
     * - Linux: GLX (PlatformGLX) 或 EGL (PlatformEGLHeadless) 或 OSMesa
     * - Windows: WGL (PlatformWGL)
     * - Web: WebGL (PlatformWebGL)
     */
    #if defined(FILAMENT_SUPPORTS_OPENGL)
        #if defined(FILAMENT_USE_EXTERNAL_GLES3)
            // 使用外部 GLES3，不创建 Platform
            return nullptr;
        #elif defined(__ANDROID__)
            return new PlatformEGLAndroid();  // Android EGL Platform
        #elif defined(FILAMENT_IOS)
            return new PlatformCocoaTouchGL();  // iOS Cocoa Touch GL Platform
        #elif defined(__APPLE__)
            #if defined(FILAMENT_SUPPORTS_OSMESA)
                return new PlatformOSMesa();  // macOS OSMesa Platform（无窗口）
            #else
                return new PlatformCocoaGL();  // macOS Cocoa GL Platform
            #endif
        #elif defined(__linux__)
            #if defined(FILAMENT_SUPPORTS_X11)
                return new PlatformGLX();  // Linux GLX Platform（X11）
            #elif defined(FILAMENT_SUPPORTS_EGL_ON_LINUX)
                return new PlatformEGLHeadless();  // Linux EGL Platform（无头）
            #elif defined(FILAMENT_SUPPORTS_OSMESA)
                return new PlatformOSMesa();  // Linux OSMesa Platform（无窗口）
            #else
                return nullptr;  // Linux 平台无支持的 OpenGL 实现
            #endif
        #elif defined(WIN32)
            return new PlatformWGL();  // Windows WGL Platform
        #elif defined(__EMSCRIPTEN__)
            return new PlatformWebGL();  // Web WebGL Platform
        #else
            return nullptr;  // 未知平台
        #endif
    #else
        return nullptr;  // OpenGL 后端未编译
    #endif
}

/**
 * 销毁 Platform 实例
 * 
 * 释放 Platform 对象并清空指针。
 * 
 * @param platform Platform 指针的指针（会被设置为 nullptr）
 */
void PlatformFactory::destroy(Platform** platform) noexcept {
    delete *platform;
    *platform = nullptr;
}

} // namespace filament::backend
