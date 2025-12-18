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

#include <backend/Platform.h>

#include <backend/platforms/PlatformWebGL.h>

#include <cstdint>

namespace filament::backend {

using namespace backend;

/**
 * 创建驱动
 * 
 * WebGL 平台使用默认的 OpenGL 驱动创建方法。
 * 
 * @param sharedGLContext 共享 GL 上下文（WebGL 中通常为 nullptr）
 * @param driverConfig 驱动配置
 * @return 创建的驱动指针
 */
Driver* PlatformWebGL::createDriver(void* sharedGLContext,
        const DriverConfig& driverConfig) {
    return createDefaultDriver(this, sharedGLContext, driverConfig);
}

/**
 * 获取操作系统版本
 * 
 * WebGL 平台返回 0（无操作系统版本概念）。
 * 
 * @return 操作系统版本（WebGL 返回 0）
 */
int PlatformWebGL::getOSVersion() const noexcept {
    return 0;
}

/**
 * 终止平台
 * 
 * WebGL 平台无需特殊清理。
 */
void PlatformWebGL::terminate() noexcept {
}

/**
 * 创建交换链（从原生窗口）
 * 
 * WebGL 中，原生窗口直接作为交换链使用。
 * 
 * @param nativeWindow 原生窗口指针（WebGL 中为 HTMLCanvasElement）
 * @param flags 交换链标志
 * @return 交换链指针（直接返回 nativeWindow）
 */
Platform::SwapChain* PlatformWebGL::createSwapChain(
        void* nativeWindow, uint64_t flags) noexcept {
    return static_cast<SwapChain*>(nativeWindow);
}

/**
 * 创建交换链（无头模式）
 * 
 * TODO: 实现无头交换链
 * 
 * @param width 宽度
 * @param height 高度
 * @param flags 交换链标志
 * @return 交换链指针（当前返回 nullptr）
 */
Platform::SwapChain* PlatformWebGL::createSwapChain(
        uint32_t width, uint32_t height, uint64_t flags) noexcept {
    // TODO: 实现无头交换链
    return nullptr;
}

/**
 * 销毁交换链
 * 
 * WebGL 平台无需特殊清理。
 * 
 * @param swapChain 交换链指针
 */
void PlatformWebGL::destroySwapChain(SwapChain* swapChain) noexcept {
}

/**
 * 设置当前上下文
 * 
 * WebGL 中上下文由浏览器管理，此函数总是返回 true。
 * 
 * @param type 上下文类型
 * @param drawSwapChain 绘制交换链
 * @param readSwapChain 读取交换链
 * @return 总是返回 true
 */
bool PlatformWebGL::makeCurrent(ContextType type, SwapChain* drawSwapChain,
        SwapChain* readSwapChain) {
    return true;
}

/**
 * 提交交换链
 * 
 * WebGL 中缓冲区交换由浏览器自动处理，此函数为空。
 * 
 * @param swapChain 交换链指针
 */
void PlatformWebGL::commit(SwapChain* swapChain) noexcept {
}

} // namespace filament::backend
