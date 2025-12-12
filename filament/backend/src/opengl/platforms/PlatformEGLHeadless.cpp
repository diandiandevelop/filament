/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <backend/platforms/PlatformEGLHeadless.h>

#include <bluegl/BlueGL.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/compiler.h>

using namespace utils;

namespace filament {
using namespace backend;

/**
 * 构造函数
 * 
 * 初始化无头 EGL 平台。
 */
PlatformEGLHeadless::PlatformEGLHeadless() noexcept
        : PlatformEGL() {
}

/**
 * 检查是否为 OpenGL（而非 GLES）
 * 
 * 根据编译时配置返回是否为 OpenGL。
 * 
 * @return 如果为 OpenGL 返回 true，否则返回 false
 */
bool PlatformEGLHeadless::isOpenGL() const noexcept {
#if defined(BACKEND_OPENGL_VERSION_GL)
    return true;
#else
    return false;
#endif  // defined(BACKEND_OPENGL_VERSION_GL)
}

/**
 * 创建驱动
 * 
 * 初始化无头 EGL 并创建 OpenGL 驱动。
 * 
 * @param sharedContext 共享上下文（可选）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针，失败返回 nullptr
 * 
 * 执行流程：
 * 1. 绑定 EGL API（OpenGL 或 OpenGL ES）
 * 2. 绑定 BlueGL（桌面 OpenGL 需要）
 * 3. 调用基类创建驱动
 */
backend::Driver* PlatformEGLHeadless::createDriver(void* sharedContext,
        const Platform::DriverConfig& driverConfig) {
    /**
     * 绑定 API 辅助函数
     * 
     * 绑定 EGL API 并处理错误。
     * 
     * @param api EGL API 类型
     * @param errorString 错误字符串（用于日志）
     * @return 如果成功返回 true，否则返回 false
     */
    auto bindApiHelper = [](EGLenum api, const char* errorString) -> bool {
        EGLBoolean bindAPI = eglBindAPI(api);
        if (UTILS_UNLIKELY(bindAPI == EGL_FALSE || bindAPI == EGL_BAD_PARAMETER)) {
            logEglError(errorString);
            eglReleaseThread();
            return false;
        };
        return true;
    };

    // 根据配置选择 API
    EGLenum api = isOpenGL() ? EGL_OPENGL_API : EGL_OPENGL_ES_API;
    const char* apiString = isOpenGL() ? "eglBindAPI EGL_OPENGL_API" : "eglBindAPI EGL_OPENGL_ES_API";
    if (!bindApiHelper(api, apiString)) {
        return nullptr;
    }

    // 绑定 BlueGL（桌面 OpenGL 需要）
    int bindBlueGL = bluegl::bind();
    if (UTILS_UNLIKELY(bindBlueGL != 0)) {
        LOG(ERROR) << "bluegl bind failed";
        return nullptr;
    }

    // 调用基类创建驱动
    return PlatformEGL::createDriver(sharedContext, driverConfig);
}

} // namespace filament
