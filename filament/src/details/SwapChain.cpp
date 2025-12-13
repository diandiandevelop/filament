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

#include <filament/SwapChain.h>

#include <backend/CallbackHandler.h>

#include <utils/CString.h>
#include <utils/Invocable.h>
#include <utils/Logger.h>

#include <new>
#include <utility>

#include <stdint.h>

namespace filament {

namespace {

/**
 * 获取被移除的标志字符串
 * 
 * 比较原始标志和修改后的标志，返回被移除的标志名称。
 * 
 * @param originalFlags 原始标志
 * @param modifiedFlags 修改后的标志
 * @return 被移除的标志名称字符串
 */
utils::CString getRemovedFlags(uint64_t originalFlags, uint64_t modifiedFlags) {
    const uint64_t diffFlags = originalFlags ^ modifiedFlags;  // 计算差异标志（异或）
    utils::CString removed;  // 结果字符串
    /**
     * 检查每个可能被移除的标志
     */
    if (diffFlags & backend::SWAP_CHAIN_CONFIG_SRGB_COLORSPACE) {
        removed += "SRGB_COLORSPACE ";  // sRGB 色彩空间标志
    }
    if (diffFlags & backend::SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES) {
        removed += "MSAA_4_SAMPLES ";  // MSAA 4x 采样标志
    }
    if (diffFlags & backend::SWAP_CHAIN_CONFIG_PROTECTED_CONTENT) {
        removed += "PROTECTED_CONTENT ";  // 受保护内容标志
    }
    return removed;  // 返回被移除的标志字符串
}

} // anonymous namespace

/**
 * 交换链构造函数（使用原生窗口）
 * 
 * 创建与原生窗口关联的交换链。
 * 
 * @param engine 引擎引用
 * @param nativeWindow 原生窗口指针
 * @param flags 配置标志
 */
FSwapChain::FSwapChain(FEngine& engine, void* nativeWindow, uint64_t const flags)
        : mEngine(engine),  // 保存引擎引用
          mNativeWindow(nativeWindow),  // 保存原生窗口指针
          mConfigFlags(initFlags(engine, flags)) {  // 初始化并验证标志
    mHwSwapChain = engine.getDriverApi().createSwapChain(nativeWindow, mConfigFlags);  // 创建硬件交换链
}

/**
 * 交换链构造函数（无头模式）
 * 
 * 创建无头交换链（不关联窗口）。
 * 
 * @param engine 引擎引用
 * @param width 宽度
 * @param height 高度
 * @param flags 配置标志
 */
FSwapChain::FSwapChain(FEngine& engine, uint32_t const width, uint32_t const height, uint64_t const flags)
        : mEngine(engine),  // 保存引擎引用
          mWidth(width),  // 保存宽度
          mHeight(height),  // 保存高度
          mConfigFlags(initFlags(engine, flags)) {  // 初始化并验证标志
    mHwSwapChain = engine.getDriverApi().createSwapChainHeadless(width, height, mConfigFlags);  // 创建无头交换链
}

/**
 * 使用新标志重新创建交换链
 * 
 * 如果新标志与当前标志不同，则销毁旧交换链并创建新的。
 * 
 * @param engine 引擎引用
 * @param flags 新配置标志
 */
void FSwapChain::recreateWithNewFlags(FEngine& engine, uint64_t flags) noexcept {
    flags = initFlags(engine, flags);  // 初始化并验证标志
    if (flags != mConfigFlags) {  // 如果标志发生变化
        FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
        driver.destroySwapChain(mHwSwapChain);  // 销毁旧交换链
        mConfigFlags = flags;  // 更新配置标志
        /**
         * 根据是否有原生窗口创建相应的交换链
         */
        if (mNativeWindow) {
            mHwSwapChain = driver.createSwapChain(mNativeWindow, flags);  // 创建窗口交换链
        } else {
            mHwSwapChain = driver.createSwapChainHeadless(mWidth, mHeight, flags);  // 创建无头交换链
        }
    }
}

/**
 * 初始化标志
 * 
 * 验证并移除不支持的特性标志。
 * 
 * @param engine 引擎引用
 * @param flags 原始标志
 * @return 修改后的标志（移除了不支持的特性）
 */
uint64_t FSwapChain::initFlags(FEngine& engine, uint64_t flags) noexcept {
    const uint64_t originalFlags = flags;  // 保存原始标志
    /**
     * 检查并移除不支持的特性
     */
    if (!isSRGBSwapChainSupported(engine)) {  // 如果不支持 sRGB 交换链
        flags &= ~CONFIG_SRGB_COLORSPACE;  // 移除 sRGB 色彩空间标志
    }
    if (!isMSAASwapChainSupported(engine, 4)) {  // 如果不支持 MSAA 4x 交换链
        flags &= ~CONFIG_MSAA_4_SAMPLES;  // 移除 MSAA 4x 采样标志
    }
    if (!isProtectedContentSupported(engine)) {  // 如果不支持受保护内容
        flags &= ~CONFIG_PROTECTED_CONTENT;  // 移除受保护内容标志
    }
    /**
     * 如果标志被修改，记录警告
     */
    if (originalFlags != flags) {
        LOG(WARNING) << "SwapChain flags were modified to remove features that are not supported. "
                     << "Removed: " << getRemovedFlags(originalFlags, flags).c_str_safe();  // 记录被移除的标志
    }
    return flags;  // 返回修改后的标志
}

/**
 * 终止交换链
 * 
 * 销毁硬件交换链。
 * 
 * @param engine 引擎引用
 */
void FSwapChain::terminate(FEngine& engine) noexcept {
    engine.getDriverApi().destroySwapChain(mHwSwapChain);  // 销毁硬件交换链
}

/**
 * 设置帧调度回调
 * 
 * 设置当帧被调度到 GPU 时调用的回调函数。
 * 
 * @param handler 回调处理器
 * @param callback 回调函数（会被移动）
 * @param flags 回调标志
 */
void FSwapChain::setFrameScheduledCallback(
        backend::CallbackHandler* handler, FrameScheduledCallback&& callback, uint64_t const flags) {
    mFrameScheduledCallbackIsSet = bool(callback);  // 记录回调是否已设置
    mEngine.getDriverApi().setFrameScheduledCallback(
            mHwSwapChain, handler, std::move(callback), flags);  // 设置回调
}

/**
 * 检查帧调度回调是否已设置
 * 
 * @return 如果回调已设置返回 true，否则返回 false
 */
bool FSwapChain::isFrameScheduledCallbackSet() const noexcept {
    return mFrameScheduledCallbackIsSet;  // 返回回调设置状态
}

/**
 * 设置帧完成回调
 * 
 * 设置当帧渲染完成时调用的回调函数。
 * 
 * @param handler 回调处理器
 * @param callback 回调函数（会被移动）
 */
void FSwapChain::setFrameCompletedCallback(
        backend::CallbackHandler* handler, FrameCompletedCallback&& callback) noexcept {
    using namespace std::placeholders;
    /**
     * 绑定交换链指针到回调函数
     */
    auto boundCallback = std::bind(std::move(callback), this);  // 绑定 this 指针
    mEngine.getDriverApi().setFrameCompletedCallback(mHwSwapChain, handler, std::move(boundCallback));  // 设置回调
}

/**
 * 检查是否支持 sRGB 交换链
 * 
 * @param engine 引擎引用
 * @return 如果支持返回 true，否则返回 false
 */
bool FSwapChain::isSRGBSwapChainSupported(FEngine& engine) noexcept {
    return engine.getDriverApi().isSRGBSwapChainSupported();  // 查询驱动是否支持
}

/**
 * 检查是否支持 MSAA 交换链
 * 
 * @param engine 引擎引用
 * @param samples 采样数
 * @return 如果支持返回 true，否则返回 false
 */
bool FSwapChain::isMSAASwapChainSupported(FEngine& engine, uint32_t samples) noexcept {
    return engine.getDriverApi().isMSAASwapChainSupported(samples);  // 查询驱动是否支持
}

/**
 * 检查是否支持受保护内容
 * 
 * @param engine 引擎引用
 * @return 如果支持返回 true，否则返回 false
 */
bool FSwapChain::isProtectedContentSupported(FEngine& engine) noexcept {
    return engine.getDriverApi().isProtectedContentSupported();  // 查询驱动是否支持
}

} // namespace filament
