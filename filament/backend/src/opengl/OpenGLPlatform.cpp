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

#include <backend/platforms/OpenGLPlatform.h>

#include "OpenGLDriverBase.h"
#include "OpenGLDriverFactory.h"

#include <backend/AcquiredImage.h>
#include <backend/DriverEnums.h>
#include <backend/Platform.h>

#include <utils/compiler.h>
#include <utils/Panic.h>
#include <utils/CString.h>
#include <utils/Invocable.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "OpenGLDriver.h"

namespace filament::backend {

/**
 * OpenGLDriverBase 析构函数
 * 
 * 默认析构函数。
 */
OpenGLDriverBase::~OpenGLDriverBase() = default;

/**
 * 创建默认 OpenGLDriver
 * 
 * 创建默认的 OpenGLDriver 后端实例。
 * 
 * @param platform OpenGL 平台指针
 * @param sharedContext 共享 OpenGL 上下文
 * @param driverConfig 驱动配置参数
 * @return Driver 指针，失败返回 nullptr
 */
Driver* OpenGLPlatform::createDefaultDriver(OpenGLPlatform* platform,
        void* sharedContext, const DriverConfig& driverConfig) {
    return OpenGLDriverFactory::create(platform, sharedContext, driverConfig);
}

/**
 * OpenGLPlatform 析构函数
 * 
 * 默认析构函数。
 */
OpenGLPlatform::~OpenGLPlatform() noexcept = default;

/**
 * 获取 OpenGL 供应商字符串
 * 
 * 返回指定 Driver 实例的 OpenGL 供应商字符串。
 * 
 * @param driver Driver 实例指针
 * @return GL_VENDOR 字符串
 */
utils::CString OpenGLPlatform::getVendorString(Driver const* driver) {
    auto const p = static_cast<OpenGLDriverBase const*>(driver);
#if UTILS_HAS_RTTI
    // 运行时类型检查：确保 Driver 是由 OpenGLPlatform 分配的
    FILAMENT_CHECK_POSTCONDITION(dynamic_cast<OpenGLDriverBase const*>(driver))
            << "Driver* has not been allocated with OpenGLPlatform";
#endif
    return p->getVendorString();
}

/**
 * 获取 OpenGL 渲染器字符串
 * 
 * 返回指定 Driver 实例的 OpenGL 渲染器字符串。
 * 
 * @param driver Driver 实例指针
 * @return GL_RENDERER 字符串
 */
utils::CString OpenGLPlatform::getRendererString(Driver const* driver) {
    auto const p = static_cast<OpenGLDriverBase const*>(driver);
#if UTILS_HAS_RTTI
    // 运行时类型检查：确保 Driver 是由 OpenGLPlatform 分配的
    FILAMENT_CHECK_POSTCONDITION(dynamic_cast<OpenGLDriverBase const*>(driver))
            << "Driver* has not been allocated with OpenGLPlatform";
#endif
    return p->getRendererString();
}

/**
 * 使 OpenGL 上下文成为当前上下文（默认实现）
 * 
 * 默认实现只调用 makeCurrent(getCurrentContextType(), SwapChain*, SwapChain*)。
 * 
 * @param drawSwapChain 绘制交换链
 * @param readSwapChain 读取交换链
 * @param 未使用的回调参数（保持接口一致性）
 * @param 未使用的回调参数（保持接口一致性）
 */
void OpenGLPlatform::makeCurrent(SwapChain* drawSwapChain, SwapChain* readSwapChain,
        utils::Invocable<void()>, utils::Invocable<void(size_t)>) {
    makeCurrent(getCurrentContextType(), drawSwapChain, readSwapChain);
}

/**
 * 检查是否支持保护上下文（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @return 如果支持保护上下文返回 true，否则返回 false
 */
bool OpenGLPlatform::isProtectedContextSupported() const noexcept {
    return false;
}

/**
 * 检查是否支持 sRGB 交换链（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @return 如果支持 sRGB 交换链返回 true，否则返回 false
 */
bool OpenGLPlatform::isSRGBSwapChainSupported() const noexcept {
    return false;
}

/**
 * 检查是否支持 MSAA 交换链（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @param 未使用的采样数参数（保持接口一致性）
 * @return 如果支持 MSAA 交换链返回 true，否则返回 false
 */
bool OpenGLPlatform::isMSAASwapChainSupported(uint32_t) const noexcept {
    return false;
}

/**
 * 获取默认 FBO（默认实现）
 * 
 * 默认实现返回 0（表示默认 framebuffer）。
 * 
 * @return 默认 FBO ID（0 表示默认 framebuffer）
 */
uint32_t OpenGLPlatform::getDefaultFramebufferObject() noexcept {
    return 0;
}

/**
 * 开始帧（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param monotonic_clock_ns 单调时钟时间戳（纳秒）
 * @param refreshIntervalNs 刷新间隔（纳秒）
 * @param frameId 帧 ID
 */
void OpenGLPlatform::beginFrame(int64_t monotonic_clock_ns, int64_t refreshIntervalNs,
        uint32_t frameId) noexcept {
}

/**
 * 结束帧（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param frameId 帧 ID
 */
void OpenGLPlatform::endFrame(uint32_t frameId) noexcept {
}

/**
 * 提交前回调（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 */
void OpenGLPlatform::preCommit() noexcept {
}

/**
 * 获取当前上下文类型（默认实现）
 * 
 * 默认实现返回 UNPROTECTED。
 * 
 * @return 当前上下文类型（默认返回 UNPROTECTED）
 */
OpenGLPlatform::ContextType OpenGLPlatform::getCurrentContextType() const noexcept {
    return ContextType::UNPROTECTED;
}

/**
 * 设置呈现时间（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param presentationTimeInNanosecond 呈现时间（纳秒）
 */
void OpenGLPlatform::setPresentationTime(
        UTILS_UNUSED int64_t presentationTimeInNanosecond) noexcept {
}


/**
 * 检查是否可以创建 Fence（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @return 如果可以创建 Fence 返回 true，否则返回 false
 */
bool OpenGLPlatform::canCreateFence() noexcept {
    return false;
}

/**
 * 创建 Fence（默认实现）
 * 
 * 默认实现返回 nullptr（不支持）。
 * 
 * @return Fence 对象指针，默认返回 nullptr
 */
Platform::Fence* OpenGLPlatform::createFence() noexcept {
    return nullptr;
}

/**
 * 销毁 Fence（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param fence Fence 指针
 */
void OpenGLPlatform::destroyFence(
        UTILS_UNUSED Fence* fence) noexcept {
}

/**
 * 等待 Fence（默认实现）
 * 
 * 默认实现总是返回 ERROR（不支持）。
 * 
 * @param fence Fence 指针
 * @param timeout 超时时间（纳秒）
 * @return Fence 状态，默认返回 ERROR
 */
FenceStatus OpenGLPlatform::waitFence(
        UTILS_UNUSED Fence* fence,
        UTILS_UNUSED uint64_t timeout) noexcept {
    return FenceStatus::ERROR;
}

/**
 * 创建 Sync（默认实现）
 * 
 * 默认实现创建一个新的 Platform::Sync 对象。
 * 
 * @return Sync 对象指针（不能为空）
 */
Platform::Sync* OpenGLPlatform::createSync() noexcept {
    return new Platform::Sync();
}

/**
 * 销毁 Sync（默认实现）
 * 
 * 默认实现删除 Sync 对象。
 * sync 必须是 Platform::Sync，因为它是由此平台对象创建的。
 * 
 * @param sync Sync 指针（不能为空）
 */
void OpenGLPlatform::destroySync(Platform::Sync* sync) noexcept {
    // sync 必须是 Platform::Sync，因为它是由此平台对象创建的
    delete sync;
}

/**
 * 创建 Stream（默认实现）
 * 
 * 默认实现返回 nullptr（不支持）。
 * 
 * @param nativeStream 原生流指针
 * @return Stream 对象指针，默认返回 nullptr
 */
Platform::Stream* OpenGLPlatform::createStream(
        UTILS_UNUSED void* nativeStream) noexcept {
    return nullptr;
}

/**
 * 销毁 Stream（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param stream Stream 指针
 */
void OpenGLPlatform::destroyStream(
        UTILS_UNUSED Stream* stream) noexcept {
}

/**
 * 附加纹理到流（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param stream Stream 指针
 * @param tname GL 纹理 ID
 */
void OpenGLPlatform::attach(
        UTILS_UNUSED Stream* stream,
        UTILS_UNUSED intptr_t tname) noexcept {
}

/**
 * 从流分离纹理（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param stream Stream 指针
 */
void OpenGLPlatform::detach(
        UTILS_UNUSED Stream* stream) noexcept {
}

/**
 * 更新流纹理图像（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param stream Stream 指针
 * @param timestamp 时间戳输出参数
 */
void OpenGLPlatform::updateTexImage(
        UTILS_UNUSED Stream* stream,
        UTILS_UNUSED int64_t* timestamp) noexcept {
}

/**
 * 获取流变换矩阵（默认实现）
 * 
 * 默认实现返回单位矩阵。
 * 
 * @param stream Stream 指针
 * @return 变换矩阵（默认返回单位矩阵）
 */
math::mat3f OpenGLPlatform::getTransformMatrix(
    UTILS_UNUSED Stream* stream) noexcept {
return math::mat3f();
}

/**
 * 创建外部图像纹理（默认实现）
 * 
 * 默认实现返回 nullptr（不支持）。
 * 
 * @return ExternalTexture 指针，默认返回 nullptr
 */
OpenGLPlatform::ExternalTexture* OpenGLPlatform::createExternalImageTexture() noexcept {
    return nullptr;
}

/**
 * 销毁外部图像纹理（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param texture ExternalTexture 指针
 */
void OpenGLPlatform::destroyExternalImageTexture(
        UTILS_UNUSED ExternalTexture* texture) noexcept {
}

/**
 * 保留外部图像（HandleRef 版本，默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param externalImage 外部图像句柄引用
 */
void OpenGLPlatform::retainExternalImage(
        UTILS_UNUSED ExternalImageHandleRef externalImage) noexcept {
}

/**
 * 保留外部图像（void* 版本，默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param externalImage 外部图像指针
 */
void OpenGLPlatform::retainExternalImage(
        UTILS_UNUSED void* externalImage) noexcept {
}

/**
 * 设置外部图像（HandleRef 版本，默认实现）
 * 
 * 默认实现返回 false（不支持）。
 * 
 * @param externalImage 外部图像句柄引用
 * @param texture ExternalTexture 指针
 * @return 成功返回 true，默认返回 false
 */
bool OpenGLPlatform::setExternalImage(
        UTILS_UNUSED ExternalImageHandleRef externalImage,
        UTILS_UNUSED ExternalTexture* texture) noexcept {
    return false;
}

/**
 * 设置外部图像（void* 版本，默认实现）
 * 
 * 默认实现返回 false（不支持）。
 * 
 * @param externalImage 外部图像指针
 * @param texture ExternalTexture 指针
 * @return 成功返回 true，默认返回 false
 */
bool OpenGLPlatform::setExternalImage(
        UTILS_UNUSED void* externalImage,
        UTILS_UNUSED ExternalTexture* texture) noexcept {
    return false;
}

/**
 * 转换获取的图像（默认实现）
 * 
 * 默认实现返回 source（不转换）。
 * 
 * @param source 源图像
 * @return 转换后的图像（默认返回 source）
 */
AcquiredImage OpenGLPlatform::transformAcquiredImage(AcquiredImage source) noexcept {
    return source;
}

/**
 * 获取保留标志（默认实现）
 * 
 * 默认实现返回 NONE（不保留任何缓冲区）。
 * 
 * @param swapChain 交换链指针
 * @return 保留标志，默认返回 NONE
 */
TargetBufferFlags OpenGLPlatform::getPreservedFlags(UTILS_UNUSED SwapChain*) noexcept {
    return TargetBufferFlags::NONE;
}

/**
 * 检查交换链是否为保护模式（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @param swapChain 交换链指针
 * @return 如果交换链是保护模式返回 true，默认返回 false
 */
bool OpenGLPlatform::isSwapChainProtected(UTILS_UNUSED SwapChain*) noexcept {
    return false;
}

/**
 * 检查是否支持额外上下文（默认实现）
 * 
 * 默认实现返回 false。
 * 
 * @return 如果支持额外上下文返回 true，默认返回 false
 */
bool OpenGLPlatform::isExtraContextSupported() const noexcept {
    return false;
}

/**
 * 创建上下文（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 * 
 * @param shared 是否共享上下文
 */
void OpenGLPlatform::createContext(bool) {
}

/**
 * 释放上下文（默认实现）
 * 
 * 默认实现为空（不执行任何操作）。
 */
void OpenGLPlatform::releaseContext() noexcept {
}

} // namespace filament::backend
