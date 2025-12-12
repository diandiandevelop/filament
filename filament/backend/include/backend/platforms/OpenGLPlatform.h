/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_OPENGLPLATFORM_H
#define TNT_FILAMENT_BACKEND_PRIVATE_OPENGLPLATFORM_H

// Filament 后端公共头文件
#include <backend/AcquiredImage.h>   // 获取的图像
#include <backend/DriverEnums.h>    // 驱动枚举
#include <backend/Platform.h>       // 平台基类

// Utils 工具库
#include <utils/compiler.h>          // 编译器工具
#include <utils/Invocable.h>         // 可调用对象
#include <utils/CString.h>           // C 字符串

// 标准库
#include <stddef.h>                  // 标准定义
#include <stdint.h>                  // 标准整数类型

// 数学库
#include <math/mat3.h>               // 3x3 矩阵

namespace filament::backend {

class Driver;

/**
 * OpenGL 平台接口
 * 
 * 创建 OpenGL 后端的平台接口。
 * 派生类需要实现平台特定的 OpenGL 上下文管理、交换链创建等功能。
 * 
 * 警告：
 * - 以下所有方法都不允许改变 GL 状态，必须在返回时恢复原状态
 * - 这是为了避免与后端的状态缓存机制冲突
 * 
 * 主要职责：
 * 1. 创建和管理 OpenGL 上下文
 * 2. 创建和管理交换链（SwapChain）
 * 3. 管理外部纹理和流
 * 4. 提供 Fence 和 Sync 支持
 * 5. 处理帧生命周期（beginFrame/endFrame/commit）
 */
class OpenGLPlatform : public Platform {
protected:

    /**
     * 创建默认 OpenGLDriver 后端
     * 
     * 派生类可以使用此方法实例化默认的 OpenGLDriver 后端。
     * 这通常在 createDriver() 实现中调用。
     * 
     * @param platform OpenGL 平台指针（不能为空）
     * @param sharedContext 共享 OpenGL 上下文（可为空）
     * @param driverConfig 驱动配置参数
     * @return Driver 指针，失败返回 nullptr
     */
    static Driver* UTILS_NULLABLE createDefaultDriver(OpenGLPlatform* UTILS_NONNULL platform,
            void* UTILS_NULLABLE sharedContext, const DriverConfig& driverConfig);

    /**
     * OpenGLPlatform 析构函数
     * 
     * 清理平台资源。
     */
    ~OpenGLPlatform() noexcept override;

public:
    /**
     * 外部纹理结构
     * 
     * 存储外部纹理的 OpenGL 句柄信息。
     * 
     * 字段说明：
     * - target: OpenGL 纹理目标（GL_TEXTURE_2D、GL_TEXTURE_EXTERNAL_OES 等）
     * - id: OpenGL 纹理对象 ID
     */
    struct ExternalTexture {
        unsigned int target;  // GLenum 纹理目标
        unsigned int id;      // GLuint 纹理对象 ID
    };

    /**
     * 获取 OpenGL 供应商字符串
     * 
     * 返回指定 Driver 实例的 OpenGL 供应商字符串。
     * 
     * @param driver Driver 实例指针（不能为空）
     * @return GL_VENDOR 字符串（如 "NVIDIA Corporation"、"Intel Inc." 等）
     */
    static utils::CString getVendorString(Driver const* UTILS_NONNULL driver);

    /**
     * 获取 OpenGL 渲染器字符串
     * 
     * 返回指定 Driver 实例的 OpenGL 渲染器字符串。
     * 
     * @param driver Driver 实例指针（不能为空）
     * @return GL_RENDERER 字符串（如 "NVIDIA GeForce RTX 3080/PCIe/SSE2" 等）
     */
    static utils::CString getRendererString(Driver const* UTILS_NONNULL driver);

    /**
     * 终止 OpenGL 平台
     * 
     * 由驱动调用以销毁 OpenGL 上下文。
     * 应该清理初始化时创建的所有窗口或缓冲区。
     * 例如，这里会调用 `eglDestroyContext`。
     */
    virtual void terminate() noexcept = 0;

    /**
     * 检查是否支持 sRGB 交换链
     * 
     * 返回 createSwapChain 是否支持 SWAP_CHAIN_CONFIG_SRGB_COLORSPACE 标志。
     * 默认实现返回 false。
     * 
     * @return 如果支持 SWAP_CHAIN_CONFIG_SRGB_COLORSPACE 返回 true，否则返回 false
     */
    virtual bool isSRGBSwapChainSupported() const noexcept;

    /**
     * 检查是否支持 MSAA 交换链
     * 
     * 返回 createSwapChain 是否支持 SWAP_CHAIN_CONFIG_MSAA_*_SAMPLES 标志。
     * 默认实现返回 false。
     * 
     * @param samples 采样数
     * @return 如果支持 SWAP_CHAIN_CONFIG_MSAA_*_SAMPLES 返回 true，否则返回 false
     */
    virtual bool isMSAASwapChainSupported(uint32_t samples) const noexcept;

    /**
     * 检查是否支持保护上下文
     * 
     * 返回此后端是否支持保护上下文。
     * 如果支持保护上下文，创建 SwapChain 时可以使用 SWAP_CHAIN_CONFIG_PROTECTED_CONTENT 标志。
     * 默认实现返回 false。
     * 
     * @return 如果支持保护上下文返回 true，否则返回 false
     */
    virtual bool isProtectedContextSupported() const noexcept;

    /**
     * 创建交换链（从原生窗口）
     * 
     * 由驱动调用以创建交换链。
     * 
     * @param nativeWindow 表示原生窗口的令牌。具体实现请参考具体平台实现
     * @param flags 实现使用的额外标志，参见 filament::SwapChain
     * @return 驱动的 SwapChain 对象，失败返回 nullptr
     */
    virtual SwapChain* UTILS_NULLABLE createSwapChain(
            void* UTILS_NULLABLE nativeWindow, uint64_t flags) = 0;

    /**
     * 创建无头交换链
     * 
     * 由驱动调用以创建无头交换链（用于离屏渲染）。
     * 
     * @param width 缓冲区宽度
     * @param height 缓冲区高度
     * @param flags 实现使用的额外标志，参见 filament::SwapChain
     * @return 驱动的 SwapChain 对象，失败返回 nullptr
     * 
     * TODO: 我们需要一个更通用的方式来传递构造参数
     *       一个 void* 可能就足够了
     */
    virtual SwapChain* UTILS_NULLABLE createSwapChain(
            uint32_t width, uint32_t height, uint64_t flags) = 0;

    /**
     * 销毁交换链
     * 
     * 由驱动调用以销毁交换链。
     * 
     * @param swapChain 要销毁的 SwapChain 指针（不能为空）
     */
    virtual void destroySwapChain(SwapChain* UTILS_NONNULL swapChain) noexcept = 0;

    /**
     * 获取保留标志
     * 
     * 返回必须保留到 commit() 调用时的缓冲区集合。
     * 默认值为 TargetBufferFlags::NONE。
     * 颜色缓冲区总是被保留，但辅助缓冲区（如深度缓冲区）通常会被丢弃。
     * 保留标志可用于确保这些辅助缓冲区保留到 commit() 调用。
     * 
     * @param swapChain 交换链指针（不能为空）
     * @return 必须保留的缓冲区标志
     * @see commit()
     */
    virtual TargetBufferFlags getPreservedFlags(SwapChain* UTILS_NONNULL swapChain) noexcept;

    /**
     * 检查交换链是否为保护模式
     * 
     * @param swapChain 交换链指针（不能为空）
     * @return 如果交换链是保护模式返回 true，否则返回 false
     */
    virtual bool isSwapChainProtected(Platform::SwapChain* UTILS_NONNULL swapChain) noexcept;

    /**
     * 获取默认 FBO
     * 
     * 由驱动调用以建立默认 FBO。默认实现返回 0。
     * 
     * 此方法可以在常规或保护 OpenGL 上下文上调用，
     * 可以返回不同或相同的名称，因为这些名称存在于不同的命名空间中。
     * 
     * @return 转换为 uint32_t 的 GLuint，表示 OpenGL framebuffer 对象
     */
    virtual uint32_t getDefaultFramebufferObject() noexcept;

    /**
     * 开始帧
     * 
     * 由后端在帧开始时调用。
     * 
     * @param monotonic_clock_ns 单调时钟上的 VSync 时间点（纳秒）
     * @param refreshIntervalNs 刷新间隔（纳秒）
     * @param frameId 帧 ID
     */
    virtual void beginFrame(
            int64_t monotonic_clock_ns,
            int64_t refreshIntervalNs,
            uint32_t frameId) noexcept;

    /**
     * 结束帧
     * 
     * 由后端在帧结束时调用。
     * 
     * @param frameId 在 beginFrame 中使用的帧 ID
     */
    virtual void endFrame(
            uint32_t frameId) noexcept;

    /**
     * 上下文类型
     * 
     * 可用的上下文类型枚举。
     */
    enum class ContextType {
        NONE,           //!< 无当前上下文
        UNPROTECTED,    //!< 当前上下文是未保护的
        PROTECTED       //!< 当前上下文支持保护内容
    };

    /**
     * 获取当前上下文类型
     * 
     * 返回当前使用的上下文类型。
     * 此值由 makeCurrent() 更新，因此可以在调用之间缓存。
     * 只有当 isProtectedContextSupported() 返回 true 时，才能返回 ContextType::PROTECTED。
     * 
     * @return 上下文类型
     */
    virtual ContextType getCurrentContextType() const noexcept;

    /**
     * 使上下文成为当前上下文
     * 
     * 将请求的上下文绑定到当前线程，并将 drawSwapChain 绑定到
     * getDefaultFramebufferObject() 返回的默认 FBO。
     * 
     * @param type 要绑定到当前线程的上下文类型
     * @param drawSwapChain 要绘制到的 SwapChain（必须绑定到默认 FBO）
     * @param readSwapChain 要读取的 SwapChain（用于 `glBlitFramebuffer` 等操作）
     * @return 成功返回 true，错误返回 false
     */
    virtual bool makeCurrent(ContextType type,
            SwapChain* UTILS_NONNULL drawSwapChain,
            SwapChain* UTILS_NONNULL readSwapChain) = 0;

    /**
     * 使 OpenGL 上下文成为当前上下文（带回调）
     * 
     * 由驱动调用以使 OpenGL 上下文在当前线程上激活，
     * 并将 drawSwapChain 绑定到 getDefaultFramebufferObject() 返回的默认 FBO。
     * 使用的上下文是默认上下文或保护上下文。
     * 当需要上下文切换时，preContextChange 和 postContextChange 回调
     * 分别在上下文切换之前和之后调用。
     * postContextChange 被赋予新上下文的索引（0 表示默认，1 表示保护）。
     * 默认实现只调用 makeCurrent(getCurrentContextType(), SwapChain*, SwapChain*)。
     * 
     * @param drawSwapChain 要绘制到的 SwapChain（必须绑定到默认 FBO）
     * @param readSwapChain 要读取的 SwapChain（用于 `glBlitFramebuffer` 等操作）
     * @param preContextChange 上下文切换前调用的回调
     * @param postContextChange 上下文切换后调用的回调（参数为新上下文索引）
     */
    virtual void makeCurrent(
            SwapChain* UTILS_NONNULL drawSwapChain,
            SwapChain* UTILS_NONNULL readSwapChain,
            utils::Invocable<void()> preContextChange,
            utils::Invocable<void(size_t index)> postContextChange);

    /**
     * 提交前回调
     * 
     * 由后端在调用 commit() 之前调用。
     * 
     * @see commit()
     */
    virtual void preCommit() noexcept;

    /**
     * 提交交换链
     * 
     * 由驱动在当前帧完成绘制后调用。
     * 通常，这应该呈现 drawSwapChain。
     * 例如，这里会调用 `eglSwapBuffers()`。
     * 
     * @param swapChain 要呈现的 SwapChain 指针（不能为空）
     */
    virtual void commit(SwapChain* UTILS_NONNULL swapChain) noexcept = 0;

    /**
     * 设置呈现时间
     * 
     * 设置下一个提交的缓冲区应该呈现给用户的时间。
     * 
     * @param presentationTimeInNanosecond 未来的时间（纳秒）。
     *                                     使用的时钟取决于具体平台实现
     */
    virtual void setPresentationTime(int64_t presentationTimeInNanosecond) noexcept;

    // --------------------------------------------------------------------------------------------
    // Fence 支持

    /**
     * 检查是否可以创建 Fence
     * 
     * 检查此实现是否可以创建 Fence。
     * 
     * @return 如果支持返回 true，否则返回 false。默认实现返回 false
     */
    virtual bool canCreateFence() noexcept;

    /**
     * 创建 Fence
     * 
     * 创建 Fence（例如 eglCreateSyncKHR）。
     * 如果 `canCreateFence` 返回 true，则必须实现此方法。
     * Fence 用于帧同步。
     * 
     * @return Fence 对象。默认实现返回 nullptr
     */
    virtual Fence* UTILS_NULLABLE createFence() noexcept;

    /**
     * 销毁 Fence 对象
     * 
     * 销毁 Fence 对象。默认实现不执行任何操作。
     * 
     * @param fence 要销毁的 Fence 指针（不能为空）
     */
    virtual void destroyFence(Fence* UTILS_NONNULL fence) noexcept;

    /**
     * 等待 Fence
     * 
     * 等待 Fence 信号。
     * 
     * @param fence Fence 指针（不能为空）
     * @param timeout 超时时间（纳秒）
     * @return Fence 是否已信号或超时。参见 backend::FenceStatus。
     *         默认实现总是返回 backend::FenceStatus::ERROR
     */
    virtual backend::FenceStatus waitFence(Fence* UTILS_NONNULL fence, uint64_t timeout) noexcept;

    // --------------------------------------------------------------------------------------------
    // Sync 支持

    /**
     * 创建 Sync
     * 
     * 创建 Sync 对象。这些可用于外部帧同步
     * （某些平台实现可以导出为可在其他进程中使用的句柄）。
     * 
     * @return Sync 对象指针（不能为空）
     */
    virtual Platform::Sync* UTILS_NONNULL createSync() noexcept;

    /**
     * 销毁 Sync
     * 
     * 销毁 Sync 对象。
     * 如果使用不是由此平台对象创建的 sync 调用，将导致未定义行为。
     * 
     * @param sync 要销毁的 Sync 指针（不能为空），必须由此平台实例创建
     */
    virtual void destroySync(Platform::Sync* UTILS_NONNULL sync) noexcept;

    // --------------------------------------------------------------------------------------------
    // 流支持

    /**
     * 从原生流创建 Stream
     * 
     * 警告：此方法从应用程序线程同步调用（不是 Driver 线程）
     * 
     * @param nativeStream 原生流，此参数取决于具体实现
     * @return 新的 Stream 对象，失败返回 nullptr
     */
    virtual Stream* UTILS_NULLABLE createStream(void* UTILS_NULLABLE nativeStream) noexcept;

    /**
     * 销毁 Stream
     * 
     * @param stream 要销毁的 Stream 指针（不能为空）
     */
    virtual void destroyStream(Stream* UTILS_NONNULL stream) noexcept;

    /**
     * 附加纹理到流
     * 
     * 指定的流获得纹理（tname）对象的所有权。
     * 一旦附加，纹理会自动更新为流的内容（例如视频流）。
     * 
     * @param stream 要获得纹理所有权的 Stream 指针（不能为空）
     * @param tname 要"绑定"到流的 GL 纹理 ID
     */
    virtual void attach(Stream* UTILS_NONNULL stream, intptr_t tname) noexcept;

    /**
     * 从流分离纹理
     * 
     * 销毁与流关联的纹理。
     * 
     * @param stream 要分离纹理的 Stream 指针（不能为空）
     */
    virtual void detach(Stream* UTILS_NONNULL stream) noexcept;

    /**
     * 更新流纹理图像
     * 
     * 更新附加到流的纹理内容。
     * 
     * @param stream 要更新的 Stream 指针（不能为空）
     * @param timestamp 输出参数：绑定到纹理的图像的时间戳
     */
    virtual void updateTexImage(Stream* UTILS_NONNULL stream,
            int64_t* UTILS_NONNULL timestamp) noexcept;

    /**
     * 获取流变换矩阵
     * 
     * 返回附加到流的纹理的变换矩阵。
     * 
     * @param stream 要获取变换矩阵的 Stream 指针（不能为空）
     * @return 绑定到纹理的图像变换矩阵。如果不支持，返回单位矩阵
     */
    virtual math::mat3f getTransformMatrix(Stream* UTILS_NONNULL stream) noexcept;


    // --------------------------------------------------------------------------------------------
    // 外部图像支持

    /**
     * 创建外部纹理句柄
     * 
     * 创建外部纹理句柄。
     * 外部纹理没有任何参数，因为在调用 setExternalImage() 之前这些参数是未定义的。
     * 
     * @return 指向填充了有效令牌的 ExternalTexture 结构的指针。
     *         但是，实现此时可以只返回 { 0, GL_TEXTURE_2D }。
     *         实际值可以延迟到 setExternalImage 时设置。
     */
    virtual ExternalTexture* UTILS_NULLABLE createExternalImageTexture() noexcept;

    /**
     * 销毁外部纹理句柄
     * 
     * 销毁外部纹理句柄和关联数据。
     * 
     * @param texture 要销毁的句柄指针（不能为空）
     */
    virtual void destroyExternalImageTexture(ExternalTexture* UTILS_NONNULL texture) noexcept;

    // 在应用程序线程上调用，允许 Filament 获得图像的所有权

    /**
     * 保留外部图像
     * 
     * 获得 externalImage 的所有权。
     * externalImage 参数取决于 Platform 的具体实现。
     * 当调用 destroyExternalImageTexture() 时释放所有权。
     * 
     * 警告：此方法从应用程序线程同步调用（不是 Driver 线程）
     * 
     * @param externalImage 表示平台外部图像的令牌
     * @see destroyExternalImageTexture
     * @{
     */
    virtual void retainExternalImage(void* UTILS_NONNULL externalImage) noexcept;

    virtual void retainExternalImage(ExternalImageHandleRef externalImage) noexcept;
    /** @}*/

    /**
     * 设置外部图像
     * 
     * 调用以将平台特定的 externalImage 绑定到 ExternalTexture。
     * 调用此方法时，保证 ExternalTexture::id 已绑定，
     * 如果需要，ExternalTexture 会用新的 id/target 值更新。
     * 
     * 警告：此方法不允许改变绑定的纹理，或者必须在返回时恢复之前的绑定。
     * 这是为了避免后端进行状态缓存时出现问题。
     * 
     * @param externalImage 平台特定的外部图像
     * @param texture 输入/输出参数，指向 ExternalTexture，如果需要可以更新 id 和 target
     * @return 成功返回 true，错误返回 false
     * @{
     */
    virtual bool setExternalImage(void* UTILS_NONNULL externalImage,
            ExternalTexture* UTILS_NONNULL texture) noexcept;

    virtual bool setExternalImage(ExternalImageHandleRef externalImage,
            ExternalTexture* UTILS_NONNULL texture) noexcept;
    /** @}*/

    /**
     * 转换获取的图像
     * 
     * 此方法允许平台将用户提供的外部图像对象转换为新类型
     * （例如 HardwareBuffer => EGLImage）。
     * 默认实现返回 source。
     * 
     * @param source 要转换的图像
     * @return 转换后的图像
     */
    virtual AcquiredImage transformAcquiredImage(AcquiredImage source) noexcept;

    // --------------------------------------------------------------------------------------------

    /**
     * 检查是否支持额外上下文
     * 
     * 返回是否可以创建额外的 OpenGL 上下文。默认：false。
     * 
     * @return 如果可以创建额外的 OpenGL 上下文返回 true，否则返回 false
     * @see createContext
     */
    virtual bool isExtraContextSupported() const noexcept;

    /**
     * 创建 OpenGL 上下文
     * 
     * 创建与主上下文具有相同配置的 OpenGL 上下文，并使其成为当前线程的当前上下文。
     * 不能从主驱动线程调用。
     * 只有当 isExtraContextSupported() 返回 true 时才支持 createContext()。
     * 这些额外的上下文将在 terminate() 中自动终止。
     * 
     * @param shared 新上下文是否与主上下文共享
     * @see isExtraContextSupported()
     * @see terminate()
     */
    virtual void createContext(bool shared);

    /**
     * 释放上下文
     * 
     * 分离并销毁当前上下文（如果有），并释放与此线程关联的所有资源。
     * 必须从调用 createContext() 的同一线程调用。
     */
    virtual void releaseContext() noexcept;
};

} // namespace filament

#endif // TNT_FILAMENT_BACKEND_PRIVATE_OPENGLPLATFORM_H
