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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERFACTORY_H
#define TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERFACTORY_H

// 平台接口
#include <backend/Platform.h>

namespace filament::backend {

class OpenGLPlatform;
class Driver;

/**
 * OpenGL 驱动工厂类
 * 
 * 负责创建 OpenGLDriver 实例的工厂类。
 * 这是创建 OpenGL 驱动的统一入口点。
 * 
 * 设计模式：
 * - 使用工厂模式封装驱动的创建逻辑
 * - 提供静态方法，无需实例化即可使用
 * - 隐藏具体的驱动实现细节
 * 
 * 使用场景：
 * - OpenGLPlatform::createDefaultDriver() 调用此工厂创建驱动
 * - 应用程序通常不直接使用此类，而是通过 OpenGLPlatform 创建驱动
 */
class OpenGLDriverFactory {
public:
    /**
     * 创建 OpenGL 驱动实例
     * 
     * 工厂方法，创建并初始化 OpenGLDriver 实例。
     * 
     * @param platform OpenGL 平台接口指针（不能为空）
     *                  负责创建和管理 OpenGL 上下文、交换链等
     * @param sharedGLContext 共享 OpenGL 上下文（可选，可为 nullptr）
     *                        用于在多个上下文之间共享资源
     * @param driverConfig 驱动配置参数
     *                     - handleArenaSize: Handle 分配器大小
     *                     - disableHandleUseAfterFreeCheck: 禁用句柄释放后使用检查
     *                     - disableHeapHandleTags: 禁用堆句柄标签
     *                     - forceGLES2Context: 强制使用 GLES 2.0 上下文
     * @return Driver 指针，成功返回 OpenGLDriver 实例，失败返回 nullptr
     * 
     * 执行流程：
     * 1. 验证平台指针非空
     * 2. 查询 OpenGL 版本（major.minor）
     * 3. 验证版本支持：
     *    - OpenGL ES: 至少 2.0
     *    - 桌面 OpenGL: 至少 4.1
     * 4. 验证失败则清理并返回 nullptr
     * 5. 设置有效的配置（确保 handleArenaSize 至少为默认值）
     * 6. 创建并初始化 OpenGLDriver 实例
     * 
     * 版本要求：
     * - OpenGL ES 2.0+（最低要求）
     * - 桌面 OpenGL 4.1+（最低要求）
     * - 如果配置了 forceGLES2Context，强制使用 ES 2.0
     * 
     * 注意：
     * - 此方法会创建 OpenGLContext，因此必须在有效的 OpenGL 上下文中调用
     * - 返回的驱动实例由调用者负责管理生命周期
     */
    static Driver* create(OpenGLPlatform* platform, void* sharedGLContext,
            const Platform::DriverConfig& driverConfig) noexcept;
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERFACTORY_H
