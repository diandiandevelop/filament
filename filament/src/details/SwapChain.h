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

#ifndef TNT_FILAMENT_DETAILS_SWAPCHAIN_H
#define TNT_FILAMENT_DETAILS_SWAPCHAIN_H

#include "downcast.h"

#include "private/backend/DriverApi.h"

#include <filament/SwapChain.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

#include <utils/Invocable.h>

#include <stdint.h>

namespace filament {

class FEngine;

/**
 * 交换链实现类
 * 
 * 管理渲染到窗口或离屏表面的交换链。
 * 交换链用于在多个缓冲区之间切换，实现双缓冲或三缓冲渲染。
 */
class FSwapChain : public SwapChain {
public:
    /**
     * 构造函数（使用原生窗口）
     * 
     * @param engine 引擎引用
     * @param nativeWindow 原生窗口指针
     * @param flags 配置标志
     */
    FSwapChain(FEngine& engine, void* nativeWindow, uint64_t flags);
    
    /**
     * 构造函数（离屏表面）
     * 
     * @param engine 引擎引用
     * @param width 宽度
     * @param height 高度
     * @param flags 配置标志
     */
    FSwapChain(FEngine& engine, uint32_t width, uint32_t height, uint64_t flags);
    
    /**
     * 终止交换链
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 使交换链成为当前上下文
     * 
     * 将交换链设置为当前渲染目标。
     * 
     * @param driverApi 驱动 API 引用
     */
    void makeCurrent(backend::DriverApi& driverApi) noexcept {
        driverApi.makeCurrent(mHwSwapChain, mHwSwapChain);  // 设置当前交换链
    }

    /**
     * 提交交换链
     * 
     * 将当前帧提交到交换链，准备显示。
     * 
     * @param driverApi 驱动 API 引用
     */
    void commit(backend::DriverApi& driverApi) noexcept {
        driverApi.commit(mHwSwapChain);  // 提交交换链
    }

    /**
     * 获取原生窗口
     * 
     * @return 原生窗口指针
     */
    void* getNativeWindow() const noexcept {
        return mNativeWindow;  // 返回原生窗口指针
    }

    /**
     * 检查是否透明
     * 
     * @return 如果交换链支持透明返回 true，否则返回 false
     */
    bool isTransparent() const noexcept {
        return (mConfigFlags & CONFIG_TRANSPARENT) != 0;  // 检查透明标志
    }

    /**
     * 检查是否可读
     * 
     * @return 如果交换链可读返回 true，否则返回 false
     */
    bool isReadable() const noexcept {
        return (mConfigFlags & CONFIG_READABLE) != 0;  // 检查可读标志
    }

    /**
     * 检查是否有模板缓冲区
     * 
     * @return 如果交换链有模板缓冲区返回 true，否则返回 false
     */
    bool hasStencilBuffer() const noexcept {
        return (mConfigFlags & CONFIG_HAS_STENCIL_BUFFER) != 0;  // 检查模板缓冲区标志
    }

    /**
     * 检查是否受保护
     * 
     * @return 如果交换链使用受保护内容返回 true，否则返回 false
     */
    bool isProtected() const noexcept {
        return (mConfigFlags & CONFIG_PROTECTED_CONTENT) != 0;  // 检查受保护内容标志
    }

    /**
     * 获取配置标志
     * 
     * 返回有效标志。不支持的标志会自动清除。
     * 
     * @return 配置标志
     */
    // This returns the effective flags. Unsupported flags are cleared automatically.
    uint64_t getFlags() const noexcept {
        return mConfigFlags;  // 返回配置标志
    }

    /**
     * 获取硬件句柄
     * 
     * @return 交换链硬件句柄
     */
    backend::Handle<backend::HwSwapChain> getHwHandle() const noexcept {
      return mHwSwapChain;  // 返回硬件句柄
    }

    /**
     * 设置帧调度回调
     * 
     * 当帧被调度到 GPU 时调用回调。
     * 
     * @param handler 回调处理器指针
     * @param callback 回调函数（会被移动）
     * @param flags 回调标志
     */
    void setFrameScheduledCallback(
            backend::CallbackHandler* handler, FrameScheduledCallback&& callback, uint64_t flags);

    /**
     * 检查帧调度回调是否已设置
     * 
     * @return 如果回调已设置返回 true，否则返回 false
     */
    bool isFrameScheduledCallbackSet() const noexcept;

    /**
     * 设置帧完成回调
     * 
     * 当帧渲染完成时调用回调。
     * 
     * @param handler 回调处理器指针
     * @param callback 回调函数（会被移动）
     */
    void setFrameCompletedCallback(backend::CallbackHandler* handler,
                utils::Invocable<void(SwapChain*)>&& callback) noexcept;

    /**
     * 检查是否支持 sRGB 交换链
     * 
     * @param engine 引擎引用
     * @return 如果支持返回 true，否则返回 false
     */
    static bool isSRGBSwapChainSupported(FEngine& engine) noexcept;

    /**
     * 检查是否支持 MSAA 交换链
     * 
     * @param engine 引擎引用
     * @param samples 采样数
     * @return 如果支持返回 true，否则返回 false
     */
    static bool isMSAASwapChainSupported(FEngine& engine, uint32_t samples) noexcept;

    /**
     * 检查是否支持受保护内容
     * 
     * @param engine 引擎引用
     * @return 如果支持返回 true，否则返回 false
     */
    static bool isProtectedContentSupported(FEngine& engine) noexcept;

    /**
     * 使用新标志重新创建交换链
     * 
     * 这目前仅用于调试。允许使用新标志重新创建 HwSwapChain。
     * 
     * @param engine 引擎引用
     * @param flags 新标志
     */
    // This is currently only used for debugging. This allows to recreate the HwSwapChain with
    // new flags.
    void recreateWithNewFlags(FEngine& engine, uint64_t flags) noexcept;

private:
    FEngine& mEngine;  // 引擎引用
    backend::Handle<backend::HwSwapChain> mHwSwapChain;  // 硬件交换链句柄
    bool mFrameScheduledCallbackIsSet = false;  // 帧调度回调是否已设置
    void* mNativeWindow{};  // 原生窗口指针
    uint32_t mWidth{};  // 宽度
    uint32_t mHeight{};  // 高度
    uint64_t mConfigFlags{};  // 配置标志
    
    /**
     * 初始化标志
     * 
     * 根据引擎能力过滤和设置有效标志。
     * 
     * @param engine 引擎引用
     * @param flags 请求的标志
     * @return 有效标志
     */
    static uint64_t initFlags(FEngine& engine, uint64_t flags) noexcept;
};

FILAMENT_DOWNCAST(SwapChain)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_SWAPCHAIN_H
