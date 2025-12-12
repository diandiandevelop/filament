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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVER_H
#define TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVER_H

// OpenGL 后端核心头文件
#include "DriverBase.h"              // 驱动基类
#include "OpenGLContext.h"           // OpenGL 上下文（状态管理）
#include "OpenGLDriverBase.h"        // OpenGL 驱动基类
#include "OpenGLTimerQuery.h"        // OpenGL 定时器查询
#include "GLBufferObject.h"          // OpenGL 缓冲区对象
#include "GLDescriptorSet.h"         // OpenGL 描述符集
#include "GLDescriptorSetLayout.h"   // OpenGL 描述符集布局
#include "GLMemoryMappedBuffer.h"   // OpenGL 内存映射缓冲区
#include "GLTexture.h"               // OpenGL 纹理
#include "ShaderCompilerService.h"   // 着色器编译服务

// Filament 后端公共头文件
#include <backend/AcquiredImage.h>   // 获取的图像
#include <backend/CallbackHandler.h> // 回调处理器
#include <backend/DriverEnums.h>     // 驱动枚举
#include <backend/Handle.h>          // 句柄类型
#include <backend/PipelineState.h>   // 管线状态
#include <backend/Platform.h>        // 平台接口
#include <backend/Program.h>          // 着色器程序
#include <backend/TargetBufferInfo.h> // 目标缓冲区信息

// Filament 后端私有头文件
#include "private/backend/Driver.h"      // 驱动接口
#include "private/backend/HandleAllocator.h"  // 句柄分配器

// Utils 工具库
#include <utils/bitset.h>            // 位集合
#include <utils/FixedCapacityVector.h>  // 固定容量向量
#include <utils/compiler.h>          // 编译器工具
#include <utils/CString.h>           // C 字符串
#include <utils/ImmutableCString.h> // 不可变 C 字符串
#include <utils/debug.h>             // 调试工具

// 数学库
#include <math/vec4.h>               // 4D 向量

// 第三方库
#include <tsl/robin_map.h>           // Robin Hood 哈希表

// 标准库
#include <array>                     // 数组
#include <condition_variable>        // 条件变量
#include <functional>                // 函数对象
#include <memory>                    // 智能指针
#include <mutex>                     // 互斥锁
#include <tuple>                     // 元组
#include <type_traits>               // 类型特征
#include <utility>                    // 工具函数
#include <variant>                   // 变体类型
#include <vector>                    // 向量
#include <unordered_map>             // 无序映射

// C 标准库
#include <stddef.h>                  // 标准定义
#include <stdint.h>                  // 标准整数类型

/**
 * Handle 分配器内存池大小（MB）
 * 
 * 定义 Handle 分配器使用的内存池大小。
 * 可以通过编译时定义覆盖默认值（4 MB）。
 */
#ifndef FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB
#    define FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB 4
#endif

namespace filament::backend {

// 前向声明
class OpenGLPlatform;                // OpenGL 平台接口
class PixelBufferDescriptor;         // 像素缓冲区描述符
struct TargetBufferInfo;             // 目标缓冲区信息
class OpenGLProgram;                 // OpenGL 着色器程序
class TimerQueryFactoryInterface;    // 定时器查询工厂接口
struct PushConstantBundle;           // 推送常量包

/**
 * OpenGL 后端驱动实现类
 * 
 * 这是 Filament 引擎中 OpenGL/OpenGL ES 后端的核心实现类，负责：
 * 1. 管理 OpenGL 上下文和状态
 * 2. 创建和管理 GPU 资源（纹理、缓冲区、渲染目标等）
 * 3. 执行渲染命令（绘制调用、状态设置等）
 * 4. 处理帧生命周期（beginFrame/endFrame）
 * 5. 管理着色器编译和程序链接
 * 
 * 架构说明：
 * - 继承自 OpenGLDriverBase，实现 Driver 接口
 * - 使用 OpenGLContext 管理 OpenGL 状态缓存，减少状态切换开销
 * - 使用 HandleAllocator 管理资源句柄
 * - 支持多线程渲染（命令在主线程生成，在渲染线程执行）
 * - 支持 OpenGL ES 2.0/3.0/3.1 和桌面 OpenGL 4.1+
 * 
 * 主要流程：
 * 1. 初始化：创建 OpenGL 上下文，初始化状态缓存
 * 2. 资源创建：创建纹理、缓冲区、渲染目标等
 * 3. 渲染循环：
 *    - beginFrame: 开始新帧，更新外部纹理
 *    - beginRenderPass: 开始渲染通道，绑定 FBO，清除缓冲区
 *    - bindPipeline: 绑定着色器程序和渲染状态
 *    - bindRenderPrimitive: 绑定顶点/索引缓冲区
 *    - draw: 执行绘制调用
 *    - endRenderPass: 结束渲染通道，执行 MSAA resolve
 *    - endFrame: 结束帧，交换缓冲区
 * 4. 清理：释放所有资源
 */
class OpenGLDriver final : public OpenGLDriverBase {
    /**
     * OpenGLDriver 构造函数
     * 
     * 初始化 OpenGL 后端驱动的所有核心组件。
     * 
     * @param platform OpenGL 平台接口
     * @param driverConfig 驱动配置参数
     */
    inline explicit OpenGLDriver(OpenGLPlatform* platform,
            const Platform::DriverConfig& driverConfig) noexcept;
    
    /**
     * OpenGLDriver 析构函数
     * 
     * 注意：此析构函数在主线程调用，不能调用 OpenGL API。
     * 实际的清理工作由 terminate() 方法完成。
     */
    ~OpenGLDriver() noexcept override;
    
    /**
     * 获取命令分发器
     * 
     * 返回 OpenGLDriver 的 Dispatcher，包含所有 Driver API 方法的函数指针映射。
     * 
     * @return Dispatcher 对象
     */
    Dispatcher getDispatcher() const noexcept override;

public:
    /**
     * 创建 OpenGLDriver 实例
     * 
     * 静态工厂方法，创建 OpenGLDriver 实例。
     * 在创建驱动之前，会检查 OpenGL 版本是否支持。
     * 
     * @param platform OpenGL 平台接口
     * @param sharedGLContext 共享 OpenGL 上下文（当前未使用）
     * @param driverConfig 驱动配置参数
     * @return OpenGLDriver 实例指针，失败返回 nullptr
     */
    static OpenGLDriver* create(OpenGLPlatform* platform, void* sharedGLContext,
            const Platform::DriverConfig& driverConfig) noexcept;

    /**
     * 调试标记类
     * 
     * RAII 类，用于在作用域内插入调试标记。
     * 构造时推送调试组，析构时弹出调试组。
     * 
     * 用法：
     * {
     *     DebugMarker marker(driver, "MyDebugGroup");
     *     // ... 代码 ...
     * } // 自动弹出调试组
     */
    class DebugMarker {
        UTILS_UNUSED OpenGLDriver& driver;
    public:
        DebugMarker(OpenGLDriver& driver, const char* string) noexcept;
        ~DebugMarker() noexcept;
    };

    // OpenGLDriver 特定字段

    /**
     * OpenGL 交换链结构
     * 
     * 扩展 HwSwapChain，添加 OpenGL 特定的字段。
     * 
     * 字段说明：
     * - rec709: 是否使用 Rec.709 颜色空间（用于 sRGB 模拟）
     * - frameScheduled: 帧调度回调（在帧被调度到显示队列时调用）
     */
    struct GLSwapChain : public HwSwapChain {
        using HwSwapChain::HwSwapChain;
        bool rec709 = false;  // 是否使用 Rec.709 颜色空间（ES 2.0 sRGB 模拟）
        struct {
            CallbackHandler* handler = nullptr;  // 回调处理器
            std::shared_ptr<FrameScheduledCallback> callback = nullptr;  // 帧调度回调
        } frameScheduled;  // 帧调度回调信息
    };

    /**
     * OpenGL 顶点缓冲区信息结构
     * 
     * 扩展 HwVertexBufferInfo，存储顶点属性配置。
     * 
     * 字段说明：
     * - attributes: 顶点属性数组（位置、法线、UV 等）
     */
    struct GLVertexBufferInfo : public HwVertexBufferInfo {
        GLVertexBufferInfo() noexcept = default;
        GLVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount,
                AttributeArray const& attributes)
                : HwVertexBufferInfo(bufferCount, attributeCount),
                  attributes(attributes) {
        }
        AttributeArray attributes;  // 顶点属性数组（位置、法线、UV 等）
    };

    /**
     * OpenGL 顶点缓冲区结构
     * 
     * 扩展 HwVertexBuffer，存储 OpenGL 特定的字段。
     * 
     * 字段说明：
     * - vbih: 顶点缓冲区信息句柄
     * - gl.buffers: OpenGL 缓冲区对象 ID 数组（每个属性一个缓冲区）
     */
    struct GLVertexBuffer : public HwVertexBuffer {
        GLVertexBuffer() noexcept = default;
        GLVertexBuffer(uint32_t vertexCount, Handle<HwVertexBufferInfo> vbih)
                : HwVertexBuffer(vertexCount), vbih(vbih) {
        }
        Handle<HwVertexBufferInfo> vbih;  // 顶点缓冲区信息句柄
        struct {
            // 4 * MAX_VERTEX_ATTRIBUTE_COUNT 字节
            std::array<GLuint, MAX_VERTEX_ATTRIBUTE_COUNT> buffers{};  // OpenGL 缓冲区对象 ID 数组
        } gl;
    };

    /**
     * OpenGL 索引缓冲区结构
     * 
     * 扩展 HwIndexBuffer，存储 OpenGL 缓冲区对象 ID。
     * 
     * 字段说明：
     * - gl.buffer: OpenGL 索引缓冲区对象 ID
     */
    struct GLIndexBuffer : public HwIndexBuffer {
        using HwIndexBuffer::HwIndexBuffer;
        struct {
            GLuint buffer{};  // OpenGL 索引缓冲区对象 ID
        } gl;
    };

    /**
     * OpenGL 渲染图元结构
     * 
     * 扩展 HwRenderPrimitive，存储 VAO 和顶点缓冲区信息。
     * 
     * 字段说明：
     * - gl: OpenGL VAO 状态（包含顶点属性配置和索引缓冲区绑定）
     * - vbih: 顶点缓冲区信息句柄
     */
    struct GLRenderPrimitive : public HwRenderPrimitive {
        using HwRenderPrimitive::HwRenderPrimitive;
        OpenGLContext::RenderPrimitive gl;  // OpenGL VAO 状态
        Handle<HwVertexBufferInfo> vbih;  // 顶点缓冲区信息句柄
    };

    // OpenGL 资源类型别名（使用 backend 命名空间中的类型）
    using GLBufferObject = filament::backend::GLBufferObject;              // OpenGL 缓冲区对象
    using GLTexture = filament::backend::GLTexture;                        // OpenGL 纹理
    using GLTimerQuery = filament::backend::GLTimerQuery;                  // OpenGL 定时器查询
    using GLDescriptorSetLayout = filament::backend::GLDescriptorSetLayout; // OpenGL 描述符集布局
    using GLDescriptorSet = filament::backend::GLDescriptorSet;             // OpenGL 描述符集
    using GLMemoryMappedBuffer = filament::backend::GLMemoryMappedBuffer;  // OpenGL 内存映射缓冲区

    /**
     * OpenGL 流结构
     * 
     * 扩展 HwStream，存储外部流（如相机预览、视频流）的信息。
     * 
     * 字段说明：
     * - Info: 流尺寸信息（宽度、高度）
     * - user_thread: 主线程访问的字段（不在 GL 线程访问）
     *   - timestamp: 时间戳
     *   - cur: 当前图像索引
     *   - acquired: 已获取的图像
     *   - pending: 待处理的图像
     *   - transform: 变换矩阵
     * - transform: 变换矩阵（GL 线程访问）
     */
    struct GLStream : public HwStream {
        using HwStream::HwStream;
        struct Info {
            GLuint width = 0;   // 流宽度
            GLuint height = 0;  // 流高度
        };
        /*
         * 以下字段从主应用程序线程访问（不在 GL 线程访问）
         */
        struct {
            int64_t timestamp = 0;  // 时间戳
            uint8_t cur = 0;        // 当前图像索引
            AcquiredImage acquired;  // 已获取的图像
            AcquiredImage pending;   // 待处理的图像
            math::mat3f transform;   // 变换矩阵
        } user_thread;

        math::mat3f transform;  // 变换矩阵（GL 线程访问）
    };

    /**
     * OpenGL 渲染目标结构
     * 
     * 扩展 HwRenderTarget，存储 FBO 和附件信息。
     * 
     * 字段说明：
     * - gl.color: 颜色附件纹理数组（支持多渲染目标）
     * - gl.depth: 深度附件纹理
     * - gl.stencil: 模板附件纹理
     * - gl.fbo: 绘制 FBO（主 FBO）
     * - gl.fbo_read: 读取 FBO（用于 MSAA resolve，延迟创建）
     * - gl.resolve: 需要解析的附件标志（MSAA resolve）
     * - gl.samples: 采样数（1 表示非 MSAA）
     * - gl.isDefault: 是否为默认 framebuffer
     * - targets: 目标缓冲区标志（哪些缓冲区被使用）
     */
    struct GLRenderTarget : public HwRenderTarget {
        using HwRenderTarget::HwRenderTarget;
        struct {
            // 字段顺序优化 64 位系统上的大小
            GLTexture* color[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT];  // 颜色附件纹理数组
            GLTexture* depth;   // 深度附件纹理
            GLTexture* stencil; // 模板附件纹理
            GLuint fbo = 0;     // 绘制 FBO（主 FBO）
            mutable GLuint fbo_read = 0;  // 读取 FBO（用于 MSAA resolve，延迟创建）
            mutable TargetBufferFlags resolve = TargetBufferFlags::NONE;  // 需要解析的附件标志
            uint8_t samples = 1;  // 采样数（1 表示非 MSAA）
            bool isDefault = false;  // 是否为默认 framebuffer
        } gl;
        TargetBufferFlags targets = {};  // 目标缓冲区标志
    };

    /**
     * OpenGL Fence 结构
     * 
     * 扩展 HwFence，存储 Fence 状态和同步原语。
     * 
     * 字段说明：
     * - state: 共享状态（使用 shared_ptr 支持多线程访问）
     *   - lock: 互斥锁（保护状态）
     *   - cond: 条件变量（用于等待）
     *   - status: Fence 状态（TIMEOUT_EXPIRED/SIGNALED/ERROR）
     */
    struct GLFence : public HwFence {
        using HwFence::HwFence;
        struct State {
            utils::Mutex lock;  // NOLINT(*-include-cleaner) 互斥锁
            utils::Condition cond;  // NOLINT(*-include-cleaner) 条件变量
            FenceStatus status{ FenceStatus::TIMEOUT_EXPIRED };  // Fence 状态
        };
        std::shared_ptr<State> state{ std::make_shared<State>() };  // 共享状态
    };

    /**
     * OpenGL Sync Fence 结构
     * 
     * 注意：命名为 "GLSyncFence" 以避免与 GL 句柄 "GLsync"（小写 S）混淆
     * 
     * 扩展 HwSync，存储平台同步对象和回调。
     * 
     * 字段说明：
     * - CallbackData: 回调数据
     *   - handler: 回调处理器
     *   - cb: 同步回调函数
     *   - sync: 平台同步对象
     *   - userData: 用户数据
     * - lock: 互斥锁（保护回调列表）
     * - conversionCallbacks: 转换回调列表（平台同步到 GLsync 的转换）
     */
    struct GLSyncFence : public HwSync {
        struct CallbackData {
            CallbackHandler* handler;      // 回调处理器
            Platform::SyncCallback cb;     // 同步回调函数
            Platform::Sync* sync;         // 平台同步对象
            void* userData;               // 用户数据
        };
        std::mutex lock;  // 互斥锁（保护回调列表）
        std::vector<std::unique_ptr<CallbackData>> conversionCallbacks;  // 转换回调列表
    };

    // 禁止拷贝构造和赋值（单例模式）
    OpenGLDriver(OpenGLDriver const&) = delete;
    OpenGLDriver& operator=(OpenGLDriver const&) = delete;

    // 使用基类的 scheduleDestroy 方法（延迟销毁资源）
    using DriverBase::scheduleDestroy;

private:
    OpenGLPlatform& mPlatform;              // OpenGL 平台接口（创建上下文、交换缓冲区等）
    OpenGLContext mContext;                 // OpenGL 上下文（管理状态缓存）
    ShaderCompilerService mShaderCompilerService;  // 着色器编译服务（异步编译）

    friend class TimerQueryFactory;
    friend class TimerQueryNativeFactory;
    /**
     * 获取 OpenGL 上下文引用
     * 
     * @return OpenGL 上下文引用
     */
    OpenGLContext& getContext() noexcept { return mContext; }

    /**
     * 获取着色器编译服务引用
     * 
     * @return 着色器编译服务引用
     */
    ShaderCompilerService& getShaderCompilerService() noexcept {
        return mShaderCompilerService;
    }

    /**
     * 获取着色器模型
     * 
     * 返回当前 OpenGL 上下文支持的着色器模型。
     * 
     * @return ShaderModel 枚举值
     */
    ShaderModel getShaderModel() const noexcept override;
    
    /**
     * 获取支持的着色器语言
     * 
     * 返回当前 OpenGL 上下文支持的着色器语言列表。
     * 
     * @param preferredLanguage 首选语言（当前未使用）
     * @return 支持的着色器语言列表
     */
    utils::FixedCapacityVector<ShaderLanguage> getShaderLanguages(
            ShaderLanguage preferredLanguage) const noexcept override;

    /*
     * OpenGLDriver 接口方法
     */

    /**
     * 获取 OpenGL 供应商字符串
     * 
     * 返回 OpenGL 驱动程序的供应商名称。
     * 
     * @return 供应商字符串（如 "NVIDIA Corporation"、"Intel Inc." 等）
     */
    utils::CString getVendorString() const noexcept override {
        return utils::CString{ mContext.state.vendor };
    }

    /**
     * 获取 OpenGL 渲染器字符串
     * 
     * 返回 OpenGL 渲染器的名称和版本。
     * 
     * @return 渲染器字符串（如 "NVIDIA GeForce RTX 3080/PCIe/SSE2" 等）
     */
    utils::CString getRendererString() const noexcept override {
        return utils::CString{ mContext.state.renderer };
    }

    /**
     * ConcreteDispatcher 友元类
     * 
     * 允许 ConcreteDispatcher 访问 OpenGLDriver 的私有成员，
     * 用于生成命令分发器的函数指针映射。
     */
    template<typename T>
    friend class ConcreteDispatcher;

    /**
     * Driver API 方法声明宏
     * 
     * 这些宏用于声明 Driver API 方法，由 DriverAPI.inc 包含文件使用。
     * 
     * DECL_DRIVER_API: 声明普通方法（内联，在渲染线程执行）
     * DECL_DRIVER_API_SYNCHRONOUS: 声明同步方法（在主线程执行）
     * DECL_DRIVER_API_RETURN: 声明返回值方法（S 版本在主线程，R 版本在渲染线程）
     */
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    UTILS_ALWAYS_INLINE inline void methodName(paramsDecl);

#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    RetType methodName(paramsDecl) override;

#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    RetType methodName##S() noexcept override; \
    UTILS_ALWAYS_INLINE inline void methodName##R(RetType, paramsDecl);

#include "private/backend/DriverAPI.inc"

    // 内存管理...

    // 另请参见 HandleAllocator.cpp 中的显式模板实例化
    HandleAllocatorGL mHandleAllocator;  // Handle 分配器（管理所有 GPU 资源句柄）

    /**
     * 初始化句柄
     * 
     * 分配并构造新对象，返回句柄。
     * 
     * @tparam D 派生类型
     * @tparam ARGS 构造参数类型
     * @param args 构造参数
     * @return 句柄
     */
    template<typename D, typename ... ARGS>
    Handle<D> initHandle(ARGS&& ... args) {
        return mHandleAllocator.allocateAndConstruct<D>(std::forward<ARGS>(args) ...);
    }

    /**
     * 构造对象
     * 
     * 销毁旧对象并构造新对象（在同一句柄位置）。
     * 
     * @tparam D 派生类型
     * @tparam B 基类型
     * @tparam ARGS 构造参数类型
     * @param handle 句柄
     * @param args 构造参数
     * @return 对象指针
     */
    template<typename D, typename B, typename ... ARGS>
    std::enable_if_t<std::is_base_of_v<B, D>, D>*
    construct(Handle<B> const& handle, ARGS&& ... args) {
        return mHandleAllocator.destroyAndConstruct<D, B>(handle, std::forward<ARGS>(args) ...);
    }

    /**
     * 析构对象
     * 
     * 销毁对象并释放句柄。
     * 
     * @tparam B 基类型
     * @tparam D 派生类型
     * @param handle 句柄
     * @param p 对象指针
     */
    template<typename B, typename D,
            typename = std::enable_if_t<std::is_base_of_v<B, D>, D>>
    void destruct(Handle<B>& handle, D const* p) noexcept {
        return mHandleAllocator.deallocate(handle, p);
    }

    /**
     * 句柄转换（非 const）
     * 
     * 将句柄转换为派生类型指针。
     * 
     * @tparam Dp 派生类型指针
     * @tparam B 基类型
     * @param handle 句柄
     * @return 派生类型指针
     */
    template<typename Dp, typename B>
    std::enable_if_t<
            std::is_pointer_v<Dp> &&
            std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp>
    handle_cast(Handle<B>& handle) {
        return mHandleAllocator.handle_cast<Dp, B>(handle);
    }

    /**
     * 验证句柄有效性
     * 
     * @tparam B 基类型
     * @param handle 句柄
     * @return 如果句柄有效返回 true，否则返回 false
     */
    template<typename B>
    bool is_valid(Handle<B>& handle) {
        return mHandleAllocator.is_valid(handle);
    }

    /**
     * 句柄转换（const）
     * 
     * 将 const 句柄转换为派生类型指针。
     * 
     * @tparam Dp 派生类型指针
     * @tparam B 基类型
     * @param handle 句柄
     * @return 派生类型指针
     */
    template<typename Dp, typename B>
    std::enable_if_t<
            std::is_pointer_v<Dp> &&
            std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp>
    handle_cast(Handle<B> const& handle) {
        return mHandleAllocator.handle_cast<Dp, B>(handle);
    }

    /**
     * OpenGLProgram 友元类
     * 
     * 允许 OpenGLProgram 访问 OpenGLDriver 的私有成员，
     * 用于程序编译、链接和状态管理。
     */
    friend class OpenGLProgram;
    
    /**
     * ShaderCompilerService 友元类
     * 
     * 允许 ShaderCompilerService 访问 OpenGLDriver 的私有成员，
     * 用于异步着色器编译。
     */
    friend class ShaderCompilerService;

    /* 扩展管理... */

    /**
     * 函数指针类型（必须转换为正确类型）
     * 
     * 用于类型安全的函数指针转换。
     */
    using MustCastToRightType = void (*)();
    
    /**
     * 获取扩展函数地址的函数指针类型
     * 
     * 用于动态加载 OpenGL 扩展函数。
     * 
     * @param name 函数名称
     * @return 函数指针（必须转换为正确类型）
     */
    using GetProcAddressType = MustCastToRightType (*)(const char* name);
    
    GetProcAddressType getProcAddress = nullptr;  // 获取扩展函数地址的函数指针

    /* 杂项辅助方法... */

    /**
     * 更新顶点数组对象（VAO）
     * 
     * 更新 VAO 中的顶点缓冲区绑定和属性配置。
     * 
     * @param rp 渲染图元指针
     * @param vb 顶点缓冲区指针
     */
    void updateVertexArrayObject(GLRenderPrimitive* rp, GLVertexBuffer const* vb);

    /**
     * 将纹理附加到 FBO
     * 
     * 将纹理或渲染缓冲区附加到 FBO 的指定附件。
     * 
     * @param binfo 目标缓冲区信息
     * @param rt 渲染目标指针
     * @param attachment 附件类型（GL_COLOR_ATTACHMENT0 等）
     * @param layerCount 层数（用于数组纹理）
     */
    void framebufferTexture(TargetBufferInfo const& binfo,
            GLRenderTarget const* rt, GLenum attachment, uint8_t layerCount) noexcept;

    /**
     * 设置光栅化状态
     * 
     * 设置 OpenGL 光栅化状态（culling、blending、depth test 等）。
     * 
     * @param rs 光栅化状态
     */
    void setRasterState(RasterState rs) noexcept;

    /**
     * 设置模板状态
     * 
     * 设置 OpenGL 模板测试状态（stencil test、functions、operations）。
     * 
     * @param ss 模板状态
     */
    void setStencilState(StencilState ss) noexcept;

    /**
     * 设置纹理数据（非压缩）
     * 
     * 更新纹理的像素数据（非压缩格式）。
     * 
     * @param t 纹理指针
     * @param level Mipmap 级别
     * @param xoffset X 偏移
     * @param yoffset Y 偏移
     * @param zoffset Z 偏移（层索引）
     * @param width 宽度
     * @param height 高度
     * @param depth 深度（层数）
     * @param p 像素缓冲区描述符
     */
    void setTextureData(GLTexture const* t,
            uint32_t level,
            uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
            uint32_t width, uint32_t height, uint32_t depth,
            PixelBufferDescriptor&& p);

    /**
     * 设置压缩纹理数据
     * 
     * 更新纹理的压缩像素数据（压缩格式，如 ETC2、S3TC 等）。
     * 
     * @param t 纹理指针
     * @param level Mipmap 级别
     * @param xoffset X 偏移
     * @param yoffset Y 偏移
     * @param zoffset Z 偏移（层索引）
     * @param width 宽度
     * @param height 高度
     * @param depth 深度（层数）
     * @param p 像素缓冲区描述符
     */
    void setCompressedTextureData(GLTexture const* t,
            uint32_t level,
            uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
            uint32_t width, uint32_t height, uint32_t depth,
            PixelBufferDescriptor&& p);

    /**
     * 分配渲染缓冲区存储
     * 
     * 为渲染缓冲区分配存储空间。
     * 
     * @param rbo 渲染缓冲区对象 ID
     * @param internalformat 内部格式
     * @param width 宽度
     * @param height 高度
     * @param samples 采样数（MSAA）
     */
    void renderBufferStorage(GLuint rbo, GLenum internalformat, uint32_t width,
            uint32_t height, uint8_t samples) const noexcept;

    /**
     * 分配纹理存储
     * 
     * 为纹理分配存储空间（不填充数据）。
     * 
     * @param t 纹理指针
     * @param width 宽度
     * @param height 高度
     * @param depth 深度（层数）
     * @param useProtectedMemory 是否使用保护内存
     */
    void textureStorage(GLTexture* t, uint32_t width, uint32_t height,
            uint32_t depth, bool useProtectedMemory) noexcept;

    /* 状态跟踪 GL 包装器... */

    /**
     * 绑定纹理到纹理单元
     * 
     * @param unit 纹理单元索引
     * @param t 纹理指针
     */
    void bindTexture(GLuint unit, GLTexture const* t) noexcept;
    
    /**
     * 绑定采样器到纹理单元
     * 
     * @param unit 纹理单元索引
     * @param sampler 采样器对象 ID
     */
    void bindSampler(GLuint unit, GLuint sampler) noexcept;
    
    /**
     * 使用着色器程序
     * 
     * 绑定着色器程序，如果未编译则编译。
     * 
     * @param p 程序指针
     * @return 如果程序有效返回 true，否则返回 false
     */
    inline bool useProgram(OpenGLProgram* p) noexcept;

    /**
     * MSAA 解析操作
     * 
     * LOAD: 从解析纹理加载到 MSAA 缓冲区
     * STORE: 从 MSAA 缓冲区解析到纹理
     */
    enum class ResolveAction { LOAD, STORE };
    
    /**
     * 执行 MSAA 解析
     * 
     * 执行 MSAA resolve 操作（LOAD 或 STORE）。
     * 
     * @param action 解析操作（LOAD/STORE）
     * @param rt 渲染目标指针
     * @param discardFlags 丢弃标志（哪些附件可以丢弃）
     */
    void resolvePass(ResolveAction action, GLRenderTarget const* rt,
            TargetBufferFlags discardFlags) noexcept;

    /**
     * 附件数组类型
     * 
     * 用于存储 FBO 附件列表（颜色附件 + 深度 + 模板）
     */
    using AttachmentArray = std::array<GLenum, MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 2>;
    
    /**
     * 获取附件数组
     * 
     * 将 TargetBufferFlags 转换为 OpenGL 附件常量数组。
     * 
     * @param attachments 输出附件数组
     * @param buffers 缓冲区标志
     * @param isDefaultFramebuffer 是否为默认 framebuffer
     * @return 附件数量
     */
    static GLsizei getAttachments(AttachmentArray& attachments, TargetBufferFlags buffers,
            bool isDefaultFramebuffer) noexcept;

    // 表示当前渲染通道所需的状态
    Handle<HwRenderTarget> mRenderPassTarget;  // 当前渲染目标句柄
    RenderPassParams mRenderPassParams;        // 渲染通道参数
    GLboolean mRenderPassColorWrite{};         // 渲染通道颜色写入掩码
    GLboolean mRenderPassDepthWrite{};         // 渲染通道深度写入掩码
    GLboolean mRenderPassStencilWrite{};       // 渲染通道模板写入掩码

    GLRenderPrimitive const* mBoundRenderPrimitive = nullptr;  // 当前绑定的渲染图元
    OpenGLProgram* mBoundProgram = nullptr;                     // 当前绑定的程序
    bool mValidProgram = false;                                 // 程序是否有效（编译/链接成功）
    utils::bitset8 mInvalidDescriptorSetBindings;              // 无效的描述符集绑定位掩码
    utils::bitset8 mInvalidDescriptorSetBindingOffsets;       // 无效的描述符集偏移位掩码
    
    /**
     * 更新描述符
     * 
     * 将无效的描述符集绑定到 OpenGL 上下文。
     * 
     * @param invalidDescriptorSets 无效描述符集的位掩码
     */
    void updateDescriptors(utils::bitset8 invalidDescriptorSets) noexcept;

    /**
     * 绑定的描述符集数组
     * 
     * 存储每个槽位绑定的描述符集和偏移。
     */
    struct {
        DescriptorSetHandle dsh;                                    // 描述符集句柄
        std::array<uint32_t, CONFIG_UNIFORM_BINDING_COUNT> offsets; // 动态偏移数组
    } mBoundDescriptorSets[MAX_DESCRIPTOR_SET_COUNT] = {};

    /**
     * 使用光栅化管线清除缓冲区
     * 
     * @param clearFlags 清除标志
     * @param linearColor 清除颜色
     * @param depth 清除深度值
     * @param stencil 清除模板值
     */
    void clearWithRasterPipe(TargetBufferFlags clearFlags,
            math::float4 const& linearColor, GLfloat depth, GLint stencil) noexcept;

    /**
     * 设置裁剪区域
     * 
     * @param scissor 裁剪区域
     */
    void setScissor(Viewport const& scissor) noexcept;

    /**
     * 绘制（ES 2.0 版本）
     * 
     * ES 2.0 专用的绘制方法（不支持实例化）。
     * 
     * @param indexOffset 索引偏移
     * @param indexCount 索引数量
     * @param instanceCount 实例数量（ES 2.0 必须为 1）
     */
    void draw2GLES2(uint32_t indexOffset, uint32_t indexCount, uint32_t instanceCount);

    // ES 2.0 专用：Uniform 缓冲区模拟绑定点
    GLuint mLastAssignedEmulatedUboId = 0;  // 最后分配的模拟 UBO ID

    // 这些必须只在驱动线程访问
    std::vector<GLTexture*> mTexturesWithStreamsAttached;  // 附加了流的纹理列表

    // 这些必须只在用户线程访问
    std::vector<GLStream*> mStreamsWithPendingAcquiredImage;  // 有待处理获取图像的流列表

    /**
     * 附加流到纹理
     * 
     * @param t 纹理指针
     * @param stream 流指针
     */
    void attachStream(GLTexture* t, GLStream* stream);
    
    /**
     * 从纹理分离流
     * 
     * @param t 纹理指针
     */
    void detachStream(GLTexture* t) noexcept;
    
    /**
     * 替换纹理流
     * 
     * @param t 纹理指针
     * @param stream 新流指针
     */
    void replaceStream(GLTexture* t, GLStream* stream) noexcept;
    
    /**
     * 获取流变换矩阵
     * 
     * @param sh 流句柄
     * @return 变换矩阵
     */
    math::mat3f getStreamTransformMatrix(Handle<HwStream> sh);

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 在栅栏信号后在主线程执行的任务
    /**
     * 注册 GPU 命令完成回调
     * 
     * @param fn 回调函数
     */
    void whenGpuCommandsComplete(const std::function<void()>& fn);
    
    /**
     * 执行 GPU 命令完成回调
     */
    void executeGpuCommandsCompleteOps() noexcept;
    
    std::vector<std::pair<GLsync, std::function<void()>>> mGpuCommandCompleteOps;  // GPU 命令完成回调队列

    /**
     * 注册帧完成回调
     * 
     * @param fn 回调函数
     */
    void whenFrameComplete(const std::function<void()>& fn);
    
    std::vector<std::function<void()>> mFrameCompleteOps;  // 帧完成回调队列
#endif

    // 在主线程定期执行的任务，直到返回 true
    /**
     * 注册"偶尔执行"的操作
     * 
     * @param fn 操作函数，返回 true 表示完成
     */
    void runEveryNowAndThen(std::function<bool()> fn);
    
    /**
     * 执行"偶尔执行"的操作
     */
    void executeEveryNowAndThenOps() noexcept;
    
    std::vector<std::function<bool()>> mEveryNowAndThenOps;  // "偶尔执行"操作队列

    const Platform::DriverConfig mDriverConfig;  // 驱动配置
    /**
     * 获取驱动配置
     * 
     * @return 驱动配置引用
     */
    Platform::DriverConfig const& getDriverConfig() const noexcept { return mDriverConfig; }

    // 用于 ES 2.0 sRGB 支持
    GLSwapChain* mCurrentDrawSwapChain = nullptr;  // 当前绘制交换链
    bool mRec709OutputColorspace = false;          // 是否使用 Rec.709 输出颜色空间

    PushConstantBundle* mCurrentPushConstants = nullptr;  // 当前推送常量包
    PipelineLayout::SetLayout mCurrentSetLayout;            // 当前管线布局（描述符集布局）
};

// ------------------------------------------------------------------------------------------------


} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVER_H
