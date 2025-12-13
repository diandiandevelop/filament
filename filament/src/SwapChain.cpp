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

#include "details/SwapChain.h"

#include "details/Engine.h"

#include <backend/CallbackHandler.h>

#include <utility>

namespace filament {

/**
 * 获取原生窗口句柄
 * 
 * @return 原生窗口指针
 */
void* SwapChain::getNativeWindow() const noexcept {
    return downcast(this)->getNativeWindow();
}

/**
 * 设置帧调度回调
 * 
 * @param handler 回调处理器
 * @param callback 回调函数（移动语义）
 * @param flags 标志位
 */
void SwapChain::setFrameScheduledCallback(
        backend::CallbackHandler* handler, FrameScheduledCallback&& callback, uint64_t const flags) {
    downcast(this)->setFrameScheduledCallback(handler, std::move(callback), flags);
}

/**
 * 检查是否设置了帧调度回调
 * 
 * @return 如果已设置则返回 true
 */
bool SwapChain::isFrameScheduledCallbackSet() const noexcept {
    return downcast(this)->isFrameScheduledCallbackSet();
}

/**
 * 设置帧完成回调
 * 
 * @param handler 回调处理器
 * @param callback 回调函数（移动语义）
 */
void SwapChain::setFrameCompletedCallback(backend::CallbackHandler* handler,
            utils::Invocable<void(SwapChain*)>&& callback) noexcept {
    return downcast(this)->setFrameCompletedCallback(handler, std::move(callback));
}

/**
 * 检查是否支持 sRGB 交换链
 * 
 * @param engine 引擎引用
 * @return 如果支持则返回 true
 */
bool SwapChain::isSRGBSwapChainSupported(Engine& engine) noexcept {
    return FSwapChain::isSRGBSwapChainSupported(downcast(engine));
}

/**
 * 检查是否支持 MSAA 交换链
 * 
 * @param engine 引擎引用
 * @param samples 采样数
 * @return 如果支持则返回 true
 */
bool SwapChain::isMSAASwapChainSupported(Engine& engine, uint32_t samples) noexcept {
    return FSwapChain::isMSAASwapChainSupported(downcast(engine), samples);
}

/**
 * 检查是否支持受保护内容
 * 
 * @param engine 引擎引用
 * @return 如果支持则返回 true
 */
bool SwapChain::isProtectedContentSupported(Engine& engine) noexcept {
    return FSwapChain::isProtectedContentSupported(downcast(engine));
}

} // namespace filament
