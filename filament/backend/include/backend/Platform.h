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

//! \file

#ifndef TNT_FILAMENT_BACKEND_PLATFORM_H
#define TNT_FILAMENT_BACKEND_PLATFORM_H

#include <utils/CString.h>
#include <utils/compiler.h>
#include <utils/Invocable.h>
#include <utils/Mutex.h>

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace filament::backend {

class CallbackHandler;
class Driver;

/**
 * Platform is an interface that abstracts how the backend (also referred to as Driver) is
 * created. The backend provides several common Platform concrete implementations, which are
 * selected automatically. It is possible however to provide a custom Platform when creating
 * the filament Engine.
 */
/**
 * 平台接口
 * 
 * Platform 是一个接口，用于抽象后端（也称为 Driver）的创建方式。
 * 
 * 功能：
 * - 抽象底层图形 API 的初始化（OpenGL、Vulkan、Metal 等）
 * - 创建和管理 Driver 实例
 * - 提供平台特定的功能（事件处理、缓存、调试等）
 * 
 * 实现：
 * - 后端提供了多个常见的 Platform 具体实现
 * - 这些实现会根据平台自动选择
 * - 也可以在创建 Filament Engine 时提供自定义 Platform
 * 
 * 使用场景：
 * - 需要自定义图形上下文创建
 * - 需要集成到现有的图形系统
 * - 需要平台特定的优化
 */
class UTILS_PUBLIC Platform {
public:
    /**
     * 交换链类型标记
     * 
     * 空结构体，仅用于类型标记。
     * 实际类型由 Platform 实现定义。
     */
    struct SwapChain {};
    
    /**
     * 栅栏类型标记
     * 
     * 空结构体，仅用于类型标记。
     */
    struct Fence {};
    
    /**
     * 流类型标记
     * 
     * 空结构体，仅用于类型标记。
     */
    struct Stream {};
    
    /**
     * 同步对象类型标记
     * 
     * 空结构体，仅用于类型标记。
     */
    struct Sync {};

    /**
     * 同步回调函数类型
     * 
     * 签名：void(*)(Sync* sync, void* userData)
     * - sync: 同步对象指针
     * - userData: 用户数据指针
     */
    using SyncCallback = void(*)(Sync* UTILS_NONNULL sync, void* UTILS_NULLABLE userData);

    /**
     * 外部图像句柄类前向声明
     */
    class ExternalImageHandle;

    /**
     * 外部图像类
     * 
     * 表示由外部系统管理的图像（如 Android 的 SurfaceTexture、iOS 的 CVPixelBuffer）。
     * 
     * 特性：
     * - 使用引用计数管理生命周期
     * - 线程安全的引用计数（使用原子操作）
     * - 只能通过 ExternalImageHandle 访问
     * 
     * 用途：
     * - 视频纹理
     * - 相机预览
     * - 外部渲染结果
     */
    class ExternalImage {
        friend class ExternalImageHandle;
        
        /**
         * 引用计数
         * 
         * 使用原子操作保证线程安全。
         * 初始值为 0。
         */
        std::atomic_uint32_t mRefCount{0};
        
    protected:
        /**
         * 受保护的虚析构函数
         * 
         * 确保只能通过基类指针删除，防止直接删除派生类对象。
         */
        virtual ~ExternalImage() noexcept;
    };

    /**
     * 外部图像句柄类
     * 
     * 智能指针风格的句柄，用于管理 ExternalImage 的生命周期。
     * 
     * 特性：
     * - 自动管理引用计数
     * - 支持拷贝和移动语义
     * - 提供类似指针的接口（->、*、get()）
     * - 线程安全（引用计数使用原子操作）
     * 
     * 使用方式：
     * - 类似 std::shared_ptr，但针对 ExternalImage 优化
     * - 拷贝时增加引用计数
     * - 析构时减少引用计数，计数为 0 时删除对象
     */
    class ExternalImageHandle {
        /**
         * 目标外部图像指针
         * 
         * 指向被管理的 ExternalImage 对象。
         * nullptr 表示空句柄。
         */
        ExternalImage* UTILS_NULLABLE mTarget = nullptr;
        
        /**
         * 增加引用计数（静态方法）
         * 
         * @param p 外部图像指针
         * 
         * 实现：原子地增加 p->mRefCount
         */
        static void incref(ExternalImage* UTILS_NULLABLE p) noexcept;
        
        /**
         * 减少引用计数（静态方法）
         * 
         * @param p 外部图像指针
         * 
         * 实现：
         * 1. 原子地减少 p->mRefCount
         * 2. 如果计数为 0，删除对象
         */
        static void decref(ExternalImage* UTILS_NULLABLE p) noexcept;

    public:
        /**
         * 默认构造函数
         * 
         * 创建一个空句柄（不管理任何对象）。
         */
        ExternalImageHandle() noexcept;
        
        /**
         * 析构函数
         * 
         * 减少引用计数，如果计数为 0 则删除对象。
         */
        ~ExternalImageHandle() noexcept;
        
        /**
         * 从指针构造
         * 
         * @param p 外部图像指针
         * 
         * 如果 p 不为 nullptr，增加引用计数。
         */
        explicit ExternalImageHandle(ExternalImage* UTILS_NULLABLE p) noexcept;
        
        /**
         * 拷贝构造函数
         * 
         * @param rhs 要拷贝的源对象
         * 
         * 如果 rhs 管理对象，增加引用计数。
         */
        ExternalImageHandle(ExternalImageHandle const& rhs) noexcept;
        
        /**
         * 移动构造函数
         * 
         * @param rhs 要移动的源对象
         * 
         * 转移所有权，不改变引用计数。
         */
        ExternalImageHandle(ExternalImageHandle&& rhs) noexcept;
        
        /**
         * 拷贝赋值操作符
         * 
         * @param rhs 要拷贝的源对象
         * @return 当前对象的引用
         * 
         * 实现步骤：
         * 1. 如果当前对象管理对象，减少其引用计数
         * 2. 复制指针
         * 3. 如果新对象不为空，增加其引用计数
         */
        ExternalImageHandle& operator=(ExternalImageHandle const& rhs) noexcept;
        
        /**
         * 移动赋值操作符
         * 
         * @param rhs 要移动的源对象
         * @return 当前对象的引用
         * 
         * 实现步骤：
         * 1. 如果当前对象管理对象，减少其引用计数
         * 2. 转移指针所有权
         * 3. 将源对象指针设置为 nullptr
         */
        ExternalImageHandle& operator=(ExternalImageHandle&& rhs) noexcept;

        /**
         * 相等比较操作符
         * 
         * @param rhs 要比较的对象
         * @return 如果两个句柄指向同一个对象返回 true
         */
        bool operator==(const ExternalImageHandle& rhs) const noexcept {
            return mTarget == rhs.mTarget;
        }
        
        /**
         * 布尔转换操作符
         * 
         * @return 如果句柄管理对象返回 true，否则返回 false
         */
        explicit operator bool() const noexcept { return mTarget != nullptr; }

        /**
         * 获取原始指针（非 const）
         * 
         * @return 指向外部图像的指针
         */
        ExternalImage* UTILS_NULLABLE get() noexcept { return mTarget; }
        
        /**
         * 获取原始指针（const）
         * 
         * @return 指向外部图像的常量指针
         */
        ExternalImage const* UTILS_NULLABLE get() const noexcept { return mTarget; }

        /**
         * 箭头操作符（非 const）
         * 
         * @return 指向外部图像的指针
         */
        ExternalImage* UTILS_NULLABLE operator->() noexcept { return mTarget; }
        
        /**
         * 箭头操作符（const）
         * 
         * @return 指向外部图像的常量指针
         */
        ExternalImage const* UTILS_NULLABLE operator->() const noexcept { return mTarget; }

        /**
         * 解引用操作符（非 const）
         * 
         * @return 外部图像的引用
         */
        ExternalImage& operator*() noexcept { return *mTarget; }
        
        /**
         * 解引用操作符（const）
         * 
         * @return 外部图像的常量引用
         */
        ExternalImage const& operator*() const noexcept { return *mTarget; }

        /**
         * 清空句柄
         * 
         * 释放当前管理的对象（减少引用计数），句柄变为空。
         */
        void clear() noexcept;
        
        /**
         * 重置句柄
         * 
         * 释放当前管理的对象，然后管理新对象。
         * 
         * @param p 新的外部图像指针（可以为 nullptr）
         * 
         * 实现步骤：
         * 1. 如果当前对象不为空，减少其引用计数
         * 2. 设置新指针
         * 3. 如果新指针不为空，增加其引用计数
         */
        void reset(ExternalImage* UTILS_NULLABLE p) noexcept;

    private:
        friend utils::io::ostream& operator<<(utils::io::ostream& out,
                ExternalImageHandle const& handle);
    };

    /**
     * 外部图像句柄常量引用类型别名
     * 
     * 用于函数参数，避免不必要的拷贝。
     */
    using ExternalImageHandleRef = ExternalImageHandle const&;

    /**
     * 合成器时序信息结构
     * 
     * 包含合成器（compositor）的时序信息，用于帧同步和延迟优化。
     * 
     * 用途：
     * - 预测下一帧的合成时间
     * - 优化帧提交时机
     * - 减少延迟
     * 
     * 时间单位：
     * - 所有时间使用纳秒（nanosecond）
     * - 基于 std::steady_clock
     */
    struct CompositorTiming {
        /**
         * 时间点类型
         * 
         * 自 std::steady_clock 纪元以来的纳秒数。
         */
        /** duration in nanosecond since epoch of std::steady_clock */
        using time_point_ns = int64_t;
        
        /**
         * 持续时间类型
         * 
         * 纳秒为单位的持续时间。
         */
        /** duration in nanosecond on the std::steady_clock */
        using duration_ns = int64_t;
        
        /**
         * 无效值常量
         * 
         * 表示该值不受支持或不可用。
         */
        static constexpr time_point_ns INVALID = -1;    //!< value not supported
        
        /**
         * The timestamp [ns] since epoch of the next time the compositor will begin composition.
         * This is effectively the deadline for when the compositor must receive a newly queued
         * frame.
         */
        /**
         * 合成截止时间
         * 
         * 自纪元以来，合成器下一次开始合成的时间戳（纳秒）。
         * 这实际上是合成器必须接收新排队帧的截止时间。
         * 
         * 用途：
         * - 确定帧提交的最后期限
         * - 避免错过合成窗口
         */
        time_point_ns compositeDeadline;

        /**
         * The time delta [ns] between subsequent composition events.
         */
        /**
         * 合成间隔
         * 
         * 连续合成事件之间的时间差（纳秒）。
         * 通常等于显示刷新率（如 16.67ms 对应 60Hz）。
         */
        duration_ns compositeInterval;

        /**
         * The time delta [ns] between the start of composition and the expected present time of
         * that composition. This can be used to estimate the latency of the actual present time.
         */
        /**
         * 合成到呈现延迟
         * 
         * 从合成开始到预期呈现时间之间的时间差（纳秒）。
         * 可用于估算实际呈现时间的延迟。
         */
        duration_ns compositeToPresentLatency;

        /**
         * The timestamp [ns] since epoch of the system's expected presentation time.
         * INVALID if not supported.
         */
        /**
         * 预期呈现时间
         * 
         * 自纪元以来，系统预期的呈现时间戳（纳秒）。
         * 如果不支持，则为 INVALID。
         */
        time_point_ns expectedPresentTime;

        /**
         * The timestamp [ns] since epoch of the current frame's start (i.e. vsync)
         * INVALID if not supported.
         */
        /**
         * 帧开始时间
         * 
         * 自纪元以来，当前帧的开始时间戳（纳秒），即垂直同步时间。
         * 如果不支持，则为 INVALID。
         */
        time_point_ns frameTime;

        /**
         * The timestamp [ns] since epoch of the current frame's deadline
         * INVALID if not supported.
         */
        /**
         * 帧时间线截止时间
         * 
         * 自纪元以来，当前帧的截止时间戳（纳秒）。
         * 如果不支持，则为 INVALID。
         */
        time_point_ns frameTimelineDeadline;
    };

    /**
     * 帧时间戳结构
     * 
     * 包含一帧从提交到显示的完整时间线信息，用于性能分析和延迟测量。
     * 
     * 用途：
     * - 分析渲染管线的各个阶段耗时
     * - 测量端到端延迟
     * - 优化帧提交时机
     * 
     * 时间单位：
     * - 所有时间使用纳秒（nanosecond）
     * - 基于 std::steady_clock
     */
    struct FrameTimestamps {
        /** duration in nanosecond since epoch of std::steady_clock */
        /**
         * 时间点类型
         * 
         * 自 std::steady_clock 纪元以来的纳秒数。
         */
        using time_point_ns = int64_t;
        /** value not supported */
        /**
         * 无效值常量
         * 
         * 表示该时间戳不受支持或不可用。
         */
        static constexpr time_point_ns INVALID = -1;    //!< value not supported
        /** value not yet available */
        /**
         * 待定值常量
         * 
         * 表示该时间戳尚未可用（仍在处理中）。
         */
        static constexpr time_point_ns PENDING = -2;    //!< value not yet available

        /**
         * The time the application requested this frame be presented.
         * If the application does not request a presentation time explicitly,
         * this will correspond to buffer's queue time.
         */
        /**
         * 请求呈现时间
         * 
         * 应用程序请求此帧呈现的时间。
         * 如果应用程序没有明确请求呈现时间，则对应缓冲区的排队时间。
         */
        time_point_ns requestedPresentTime;

        /**
         * The time when all the application's rendering to the surface was completed.
         */
        /**
         * 获取时间
         * 
         * 应用程序完成所有渲染到表面的时间。
         * 即渲染完成，缓冲区可以提交给合成器的时间。
         */
        time_point_ns acquireTime;

        /**
         * The time when the compositor selected this frame as the one to use for the next
         * composition. This is the earliest indication that the frame was submitted in time.
         */
        /**
         * 锁定时间
         * 
         * 合成器选择此帧用于下一次合成的时间。
         * 这是帧及时提交的最早指示。
         */
        time_point_ns latchTime;

        /**
         * The first time at which the compositor began preparing composition for this frame.
         * Zero if composition was handled by the display and the compositor didn't do any
         * rendering.
         */
        /**
         * 首次合成开始时间
         * 
         * 合成器开始为此帧准备合成的首次时间。
         * 如果合成由显示器处理且合成器未进行任何渲染，则为 0。
         */
        time_point_ns firstCompositionStartTime;

        /**
         * The last time at which the compositor began preparing composition for this frame, for
         * frames composited more than once. Zero if composition was handled by the display and the
         * compositor didn't do any rendering.
         */
        /**
         * 最后合成开始时间
         * 
         * 合成器开始为此帧准备合成的最后时间（对于多次合成的帧）。
         * 如果合成由显示器处理且合成器未进行任何渲染，则为 0。
         */
        time_point_ns lastCompositionStartTime;

        /**
         * The time at which the compositor's rendering work for this frame finished. This will be
         * INVALID if composition was handled by the display and the compositor didn't do any
         * rendering.
         */
        /**
         * GPU 合成完成时间
         * 
         * 合成器为此帧完成的渲染工作时间。
         * 如果合成由显示器处理且合成器未进行任何渲染，则为 INVALID。
         */
        time_point_ns gpuCompositionDoneTime;

        /**
         * The time at which this frame started to scan out to the physical display.
         */
        /**
         * 显示呈现时间
         * 
         * 此帧开始扫描输出到物理显示器的时间。
         * 即实际显示在屏幕上的时间。
         */
        time_point_ns displayPresentTime;

        /**
         * The time when the buffer became available for reuse as a buffer the client can target
         * without blocking. This is generally the point when all read commands of the buffer have
         * been submitted, but not necessarily completed.
         */
        /**
         * 出队就绪时间
         * 
         * 缓冲区变为可重用（客户端可以无阻塞地将其作为目标）的时间。
         * 通常是缓冲区的所有读取命令已提交（但不一定完成）的时间点。
         */
        time_point_ns dequeueReadyTime;

        /**
         * The time at which all reads for the purpose of display/composition were completed for
         * this frame.
         */
        /**
         * 释放时间
         * 
         * 此帧的所有显示/合成读取完成的时间。
         * 即缓冲区可以安全释放或重用给应用程序的时间。
         */
        time_point_ns releaseTime;
    };

    /**
     * The type of technique for stereoscopic rendering. (Note that the materials used will need to
     * be compatible with the chosen technique.)
     */
    /**
     * 立体渲染技术类型
     * 
     * 定义用于立体渲染（VR/AR）的技术方法。
     * 
     * 注意：使用的材质需要与所选技术兼容。
     */
    enum class StereoscopicType : uint8_t {
        /**
         * No stereoscopic rendering
         */
        /**
         * 无立体渲染
         * 
         * 禁用立体渲染，使用标准单眼渲染。
         */
        NONE,
        /**
         * Stereoscopic rendering is performed using instanced rendering technique.
         */
        /**
         * 实例化渲染
         * 
         * 使用实例化渲染技术进行立体渲染。
         * 通过绘制两次（左右眼）实现立体效果。
         */
        INSTANCED,
        /**
         * Stereoscopic rendering is performed using the multiview feature from the graphics
         * backend.
         */
        /**
         * 多视图渲染
         * 
         * 使用图形后端的多视图（multiview）功能进行立体渲染。
         * 更高效，一次绘制同时生成左右眼视图。
         */
        MULTIVIEW,
    };

    /**
     * This controls the priority level for GPU work scheduling, which helps prioritize the
     * submitted GPU work and enables preemption.
     */
    /**
     * GPU 上下文优先级
     * 
     * 控制 GPU 工作调度的优先级级别，有助于优先处理提交的 GPU 工作并启用抢占。
     * 
     * 用途：
     * - 控制 GPU 任务的执行顺序
     * - 允许高优先级任务抢占低优先级任务
     * - 优化延迟敏感应用的性能
     */
    enum class GpuContextPriority : uint8_t {
        /**
         * Backend default GPU context priority (typically MEDIUM)
         */
        /**
         * 默认优先级
         * 
         * 使用后端默认的 GPU 上下文优先级（通常是 MEDIUM）。
         */
        DEFAULT,
        /**
         * For non-interactive, deferrable workloads. This should not interfere with standard
         * applications.
         */
        /**
         * 低优先级
         * 
         * 用于非交互式、可延迟的工作负载。
         * 不应干扰标准应用程序。
         */
        LOW,
        /**
         * The default priority level for standard applications.
         */
        /**
         * 中等优先级
         * 
         * 标准应用程序的默认优先级级别。
         */
        MEDIUM,
        /**
         * For high-priority, latency-sensitive workloads that are more important than standard
         * applications.
         */
        /**
         * 高优先级
         * 
         * 用于高优先级、延迟敏感的工作负载，比标准应用程序更重要。
         */
        HIGH,
        /**
         * The highest priority, intended for system-critical, real-time applications where missing
         * deadlines is unacceptable (e.g., VR/AR compositors or other system-critical tasks).
         */
        /**
         * 实时优先级
         * 
         * 最高优先级，用于系统关键、实时应用程序，其中错过截止时间是不可接受的
         * （例如，VR/AR 合成器或其他系统关键任务）。
         */
        REALTIME,
    };

    /**
     * Defines how asynchronous operations are handled by the engine.
     */
    /**
     * 异步操作模式
     * 
     * 定义引擎如何处理异步操作（如着色器编译、资源加载等）。
     * 
     * 用途：
     * - 控制异步任务的执行方式
     * - 平衡性能和响应性
     * - 适应不同平台的能力
     */
    enum class AsynchronousMode : uint8_t {
        /**
         * Asynchronous operations are disabled. This is the default.
         */
        /**
         * 禁用异步操作
         * 
         * 禁用异步操作，所有任务同步执行。这是默认值。
         */
        NONE,

        /**
         * Attempts to use a dedicated worker thread for asynchronous tasks. If threading is not
         * supported by the platform, it automatically falls back to using an amortization strategy.
         */
        /**
         * 优先使用线程
         * 
         * 尝试使用专用工作线程处理异步任务。
         * 如果平台不支持线程，则自动回退到使用摊销策略。
         */
        THREAD_PREFERRED,

        /**
         * Uses an amortization strategy, processing a small number of asynchronous tasks during
         * each engine update cycle.
         */
        /**
         * 摊销策略
         * 
         * 使用摊销策略，在每个引擎更新周期中处理少量异步任务。
         * 将工作分散到多个帧中，避免单帧卡顿。
         */
        AMORTIZATION,
    };

    /**
     * 驱动配置结构
     * 
     * 包含创建 Driver 时的各种配置参数。
     * 不同后端可能只支持部分配置项。
     */
    struct DriverConfig {
        /**
         * Size of handle arena in bytes. Setting to 0 indicates default value is to be used.
         * Driver clamps to valid values.
         */
        /**
         * Handle 分配器大小（字节）
         * 
         * Handle 分配器使用的内存池大小（字节）。
         * 设置为 0 表示使用默认值。
         * Driver 会将其限制为有效值（至少为默认最小值）。
         */
        size_t handleArenaSize = 0;

        /**
         * Metal 上传缓冲区大小（字节）
         * 
         * Metal 后端用于上传数据到 GPU 的缓冲区大小。
         * 默认值为 512 KB。
         */
        size_t metalUploadBufferSizeBytes = 512 * 1024;

        /**
         * Set to `true` to forcibly disable parallel shader compilation in the backend.
         * Currently only honored by the GL and Metal backends.
         */
        /**
         * 禁用并行着色器编译
         * 
         * 设置为 true 以强制禁用后端的并行着色器编译。
         * 目前仅在 GL 和 Metal 后端中生效。
         * 
         * 用途：
         * - 调试着色器编译问题
         * - 减少线程开销（单线程环境）
         */
        bool disableParallelShaderCompile = false;

        /**
         * Set to `true` to forcibly disable amortized shader compilation in the backend.
         * Currently only honored by the GL backend.
         */
        /**
         * 禁用摊销着色器编译
         * 
         * 设置为 true 以强制禁用后端的摊销着色器编译。
         * 目前仅在 GL 后端中生效。
         * 
         * 用途：
         * - 强制同步编译（调试时）
         * - 避免编译延迟分散到多帧
         */
        bool disableAmortizedShaderCompile = true;

        /**
         * Disable backend handles use-after-free checks.
         */
        /**
         * 禁用句柄释放后使用检查
         * 
         * 禁用后端句柄的释放后使用检查。
         * 可以提升性能，但会降低调试能力。
         */
        bool disableHandleUseAfterFreeCheck = false;

        /**
         * Disable backend handles tags for heap allocated (fallback) handles
         */
        /**
         * 禁用堆分配句柄标签
         * 
         * 禁用后端为堆分配（回退）句柄添加的标签。
         * 可以节省内存，但会降低调试能力。
         */
        bool disableHeapHandleTags = false;

        /**
         * Force GLES2 context if supported, or pretend the context is ES2. Only meaningful on
         * GLES 3.x backends.
         */
        /**
         * 强制 GLES2 上下文
         * 
         * 如果支持，强制使用 GLES2 上下文，或假装上下文是 ES2。
         * 仅在 GLES 3.x 后端上有意义。
         * 
         * 用途：
         * - 测试兼容性
         * - 限制功能集
         */
        bool forceGLES2Context = false;

        /**
         * Sets the technique for stereoscopic rendering.
         */
        /**
         * 立体渲染技术
         * 
         * 设置用于立体渲染的技术。
         * 默认值为 NONE（无立体渲染）。
         */
        StereoscopicType stereoscopicType = StereoscopicType::NONE;

        /**
         * Assert the native window associated to a SwapChain is valid when calling makeCurrent().
         * This is only supported for:
         *      - PlatformEGLAndroid
         */
        /**
         * 断言原生窗口有效性
         * 
         * 在调用 makeCurrent() 时断言与 SwapChain 关联的原生窗口有效。
         * 仅在以下平台支持：
         *      - PlatformEGLAndroid
         * 
         * 用途：
         * - 调试窗口生命周期问题
         */
        bool assertNativeWindowIsValid = false;

        /**
         * The action to take if a Drawable cannot be acquired. If true, the
         * frame is aborted instead of panic. This is only supported for:
         *      - PlatformMetal
         */
        /**
         * Metal 禁用 Drawable 获取失败时的 panic
         * 
         * 如果无法获取 Drawable 时采取的操作。
         * 如果为 true，帧会被中止而不是 panic。
         * 仅在以下平台支持：
         *      - PlatformMetal
         * 
         * 用途：
         * - 优雅处理窗口关闭等情况
         */
        bool metalDisablePanicOnDrawableFailure = false;

        /**
         * GPU context priority level. Controls GPU work scheduling and preemption.
         * This is only supported for:
         *      - PlatformEGL
         */
        /**
         * GPU 上下文优先级
         * 
         * GPU 上下文优先级级别。控制 GPU 工作调度和抢占。
         * 仅在以下平台支持：
         *      - PlatformEGL
         */
        GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT;

        /**
         * Bypass the staging buffer because the device is of Unified Memory Architecture.
         * This is only supported for:
         *      - VulkanPlatform
         */
        /**
         * Vulkan 启用暂存缓冲区绕过
         * 
         * 因为设备是统一内存架构（UMA）而绕过暂存缓冲区。
         * 仅在以下平台支持：
         *      - VulkanPlatform
         * 
         * 用途：
         * - 优化 UMA 设备的性能
         * - 减少内存拷贝
         */
        bool vulkanEnableStagingBufferBypass = false;

        /**
         * Asynchronous mode for the engine. Defines how asynchronous operations are handled.
         */
        /**
         * 异步操作模式
         * 
         * 引擎的异步操作模式。定义如何处理异步操作。
         * 默认值为 NONE（禁用异步操作）。
         */
        AsynchronousMode asynchronousMode = AsynchronousMode::NONE;
    };

    Platform() noexcept;

    virtual ~Platform() noexcept;

    /**
     * Queries the underlying OS version.
     * @return The OS version.
     */
    /**
     * 查询底层操作系统版本
     * 
     * 查询底层操作系统的版本号。
     * 
     * @return 操作系统版本号
     *         - Android: API 级别（如 28 表示 Android 9）
     *         - iOS: 主版本号（如 13 表示 iOS 13）
     *         - 其他平台：平台特定的版本号
     */
    virtual int getOSVersion() const noexcept = 0;

    /**
     * 创建并初始化底层图形 API，然后创建具体的 Driver
     * 
     * 这是 Platform 的核心方法，负责：
     * 1. 初始化底层图形 API：
     *    - OpenGL: 创建 OpenGL 上下文（通过 EGL、GLX、WGL 等）
     *    - Vulkan: 创建 VkInstance 和 VkDevice
     *    - Metal: 创建 MTLDevice 和 MTLCommandQueue
     *    - WebGPU: 创建 WebGPU 设备
     * 2. 创建对应的 Driver 实例
     * 3. 返回 Driver 指针（调用者负责销毁）
     * 
     * 实现说明：
     * - 每个 Platform 实现必须重写此方法
     * - 失败时返回 nullptr
     * - 成功时返回 Driver 指针，调用者必须使用 delete 销毁
     * 
     * @param sharedContext 共享上下文（可选）
     *                      - 对于 EGL 平台：EGLContext
     *                      - 对于其他平台：可能无意义或为 nullptr
     *                      - 用于多上下文场景（如多线程渲染）
     * 
     * @param driverConfig Driver 配置参数
     *                    - handleArenaSize: Handle 分配器大小
     *                    - disableParallelShaderCompile: 禁用并行着色器编译
     *                    - stereoscopicType: 立体渲染类型
     *                    - 等等...
     * 
     * @return Driver 指针，失败返回 nullptr
     * 
     * 示例：
     * ```cpp
     * Platform* platform = PlatformFactory::create(&backend);
     * Driver* driver = platform->createDriver(nullptr, config);
     * if (driver) {
     *     // 使用 driver...
     *     delete driver;
     * }
     * ```
     */
    virtual Driver* UTILS_NULLABLE createDriver(void* UTILS_NULLABLE sharedContext,
            const DriverConfig& driverConfig) = 0;

    /**
     * Processes the platform's event queue when called from its primary event-handling thread.
     *
     * Internally, Filament might need to call this when waiting on a fence. It is only implemented
     * on platforms that need it, such as macOS + OpenGL. Returns false if this is not the main
     * thread, or if the platform does not need to perform any special processing.
     */
    /**
     * 处理平台事件队列
     * 
     * 从主事件处理线程调用时处理平台的事件队列。
     * 
     * 内部实现：
     * - Filament 在等待栅栏时可能需要调用此方法
     * - 仅在需要此功能的平台上实现（如 macOS + OpenGL）
     * 
     * @return 如果成功处理事件返回 true，否则返回 false
     *         - 如果不在主线程上调用，返回 false
     *         - 如果平台不需要特殊处理，返回 false
     */
    virtual bool pumpEvents() noexcept;

    // --------------------------------------------------------------------------------------------
    // Swapchain timing APIs

    /**
     * Whether this platform supports compositor timing querying.
     *
     * @return true if this Platform supports compositor timings, false otherwise [default]
     * @see queryCompositorTiming()
     * @see setPresentFrameId()
     * @see queryFrameTimestamps()
     */
    /**
     * 是否支持合成器时序查询
     * 
     * 检查此平台是否支持合成器时序查询功能。
     * 
     * @return 如果此 Platform 支持合成器时序返回 true，否则返回 false（默认）
     * 
     * @see queryCompositorTiming()
     * @see setPresentFrameId()
     * @see queryFrameTimestamps()
     */
    virtual bool isCompositorTimingSupported() const noexcept;

    /**
     * If compositor timing is supported, fills the provided CompositorTiming structure
     * with timing information form the compositor the swapchain's native window is using.
     * The swapchain'snative window must be valid (i.e. not a headless swapchain).
     * @param swapchain to query the compositor timing from
     * @return true on success, false otherwise (e.g. if not supported)
     * @see isCompositorTimingSupported()
     */
    /**
     * 查询合成器时序
     * 
     * 如果支持合成器时序，用交换链原生窗口使用的合成器的时序信息填充提供的
     * CompositorTiming 结构。
     * 
     * 要求：
     * - 交换链的原生窗口必须有效（即不是无头交换链）
     * 
     * @param swapchain 要查询合成器时序的交换链
     * @param outCompositorTiming 输出结构，接收合成器时序信息
     * @return 成功返回 true，否则返回 false（例如不支持）
     * 
     * @see isCompositorTimingSupported()
     */
    virtual bool queryCompositorTiming(SwapChain const* UTILS_NONNULL swapchain,
            CompositorTiming* UTILS_NONNULL outCompositorTiming) const noexcept;

    /**
     * Associate a generic frameId which must be monotonically increasing (albeit not strictly) with
     * the next frame to be presented on the specified swapchain.
     *
     * This must be called from the backend thread.
     *
     * @param swapchain
     * @param frameId
     * @return true on success, false otherwise
     * @see isCompositorTimingSupported()
     * @see queryFrameTimestamps()
     */
    /**
     * 设置呈现帧 ID
     * 
     * 将必须单调递增（但不严格）的通用 frameId 与指定交换链上要呈现的下一帧关联。
     * 
     * 要求：
     * - 必须从后端线程调用
     * 
     * @param swapchain 交换链
     * @param frameId 帧 ID（必须单调递增）
     * @return 成功返回 true，否则返回 false
     * 
     * @see isCompositorTimingSupported()
     * @see queryFrameTimestamps()
     */
    virtual bool setPresentFrameId(SwapChain const* UTILS_NONNULL swapchain,
            uint64_t frameId) noexcept;

    /**
     * If compositor timing is supported, fills the provided FrameTimestamps structure
     * with timing information of a given frame, identified by the frame id, of the specified
     * swapchain. The system only keeps a limited history of frames timings.
     *
     * This API is thread safe and can be called from any thread.
     *
     * @param swapchain swapchain to query the timestamps of
     * @param frameId frame we're interested it
     * @param outFrameTimestamps output structure receiving the timestamps
     * @return true if successful, false otherwise
     * @see isCompositorTimingSupported()
     * @see setPresentFrameId()
     */
    /**
     * 查询帧时间戳
     * 
     * 如果支持合成器时序，用指定交换链的给定帧（由帧 ID 标识）的时间戳信息填充
     * 提供的 FrameTimestamps 结构。
     * 
     * 注意：
     * - 系统仅保留有限的历史帧时序
     * - 此 API 是线程安全的，可以从任何线程调用
     * 
     * @param swapchain 要查询时间戳的交换链
     * @param frameId 我们感兴趣的帧 ID
     * @param outFrameTimestamps 输出结构，接收时间戳
     * @return 成功返回 true，否则返回 false
     * 
     * @see isCompositorTimingSupported()
     * @see setPresentFrameId()
     */
    virtual bool queryFrameTimestamps(SwapChain const* UTILS_NONNULL swapchain,
            uint64_t frameId, FrameTimestamps* UTILS_NONNULL outFrameTimestamps) const noexcept;

    // --------------------------------------------------------------------------------------------
    // Caching APIs

    /**
     * InsertBlobFunc is an Invocable to an application-provided function that a
     * backend implementation may use to insert a key/value pair into the
     * cache.
     */
    /**
     * 插入 Blob 函数类型
     * 
     * 应用程序提供的函数的 Invocable，后端实现可以使用它来将键/值对插入缓存。
     * 
     * 签名：void(const void* key, size_t keySize, const void* value, size_t valueSize)
     */
    using InsertBlobFunc = utils::Invocable<
            void(const void* UTILS_NONNULL key, size_t keySize,
                    const void* UTILS_NONNULL value, size_t valueSize)>;

    /*
     * RetrieveBlobFunc is an Invocable to an application-provided function that a
     * backend implementation may use to retrieve a cached value from the
     * cache.
     */
    /**
     * 检索 Blob 函数类型
     * 
     * 应用程序提供的函数的 Invocable，后端实现可以使用它从缓存中检索缓存值。
     * 
     * 签名：size_t(const void* key, size_t keySize, void* value, size_t valueSize)
     * 返回：如果找到缓存值，返回其大小（字节）；否则返回 0
     */
    using RetrieveBlobFunc = utils::Invocable<
            size_t(const void* UTILS_NONNULL key, size_t keySize,
                    void* UTILS_NONNULL value, size_t valueSize)>;

    /**
     * Sets the callback functions that the backend can use to interact with caching functionality
     * provided by the application.
     *
     * Cache functions may only be specified once during the lifetime of a
     * Platform.  The <insert> and <retrieve> Invocables may be called at any time and
     * from any thread from the time at which setBlobFunc is called until the time that Platform
     * is destroyed. Concurrent calls to these functions from different threads is also allowed.
     * Either function can be null.
     *
     * @param insertBlob    an Invocable that inserts a new value into the cache and associates
     *                      it with the given key
     * @param retrieveBlob  an Invocable that retrieves from the cache the value associated with a
     *                      given key
     */
    /**
     * 设置 Blob 缓存回调函数
     * 
     * 设置后端可用于与应用程序提供的缓存功能交互的回调函数。
     * 
     * 重要说明：
     * - 缓存函数在 Platform 的生命周期内只能指定一次
     * - insert 和 retrieve Invocables 可以从调用 setBlobFunc 时到 Platform 销毁时的
     *   任何时间和任何线程调用
     * - 允许从不同线程并发调用这些函数
     * - 任一函数都可以为 null
     * 
     * @param insertBlob    将新值插入缓存并将其与给定键关联的 Invocable
     * @param retrieveBlob 从缓存中检索与给定键关联的值的 Invocable
     */
    void setBlobFunc(InsertBlobFunc&& insertBlob, RetrieveBlobFunc&& retrieveBlob) noexcept;

    /**
     * @return true if insertBlob is valid.
     */
    /**
     * 检查 insertBlob 是否有效
     * 
     * @return 如果 insertBlob 有效返回 true
     */
    bool hasInsertBlobFunc() const noexcept;

    /**
     * @return true if retrieveBlob is valid.
     */
    /**
     * 检查 retrieveBlob 是否有效
     * 
     * @return 如果 retrieveBlob 有效返回 true
     */
    bool hasRetrieveBlobFunc() const noexcept;

    /**
     * @return true if either of insertBlob or retrieveBlob are valid.
     */
    /**
     * 检查是否有有效的 Blob 函数
     * 
     * @return 如果 insertBlob 或 retrieveBlob 任一有效返回 true
     */
    bool hasBlobFunc() const noexcept {
        return hasInsertBlobFunc() || hasRetrieveBlobFunc();
    }

    /**
     * To insert a new binary value into the cache and associate it with a given
     * key, the backend implementation can call the application-provided callback
     * function insertBlob.
     *
     * No guarantees are made as to whether a given key/value pair is present in
     * the cache after the set call.  If a different value has been associated
     * with the given key in the past then it is undefined which value, if any, is
     * associated with the key after the set call.  Note that while there are no
     * guarantees, the cache implementation should attempt to cache the most
     * recently set value for a given key.
     *
     * @param key           pointer to the beginning of the key data that is to be inserted
     * @param keySize       specifies the size in byte of the data pointed to by <key>
     * @param value         pointer to the beginning of the value data that is to be inserted
     * @param valueSize     specifies the size in byte of the data pointed to by <value>
     */
    /**
     * 插入 Blob 到缓存
     * 
     * 要将新的二进制值插入缓存并将其与给定键关联，后端实现可以调用应用程序提供的
     * 回调函数 insertBlob。
     * 
     * 注意：
     * - 不保证在 set 调用后给定的键/值对是否存在于缓存中
     * - 如果过去已将不同的值与给定键关联，则在 set 调用后与键关联的值（如果有）是未定义的
     * - 虽然没有保证，但缓存实现应尝试缓存给定键的最 recently set 值
     * 
     * @param key       指向要插入的键数据开头的指针
     * @param keySize   指定 key 指向的数据的大小（字节）
     * @param value     指向要插入的值数据开头的指针
     * @param valueSize 指定 value 指向的数据的大小（字节）
     */
    void insertBlob(const void* UTILS_NONNULL key, size_t keySize,
            const void* UTILS_NONNULL value, size_t valueSize);

    /**
     * To retrieve the binary value associated with a given key from the cache, a
     * the backend implementation can call the application-provided callback
     * function retrieveBlob.
     *
     * If the cache contains a value for the given key and its size in bytes is
     * less than or equal to <valueSize> then the value is written to the memory
     * pointed to by <value>.  Otherwise nothing is written to the memory pointed
     * to by <value>.
     *
     * @param key          pointer to the beginning of the key
     * @param keySize      specifies the size in bytes of the binary key pointed to by <key>
     * @param value        pointer to a buffer to receive the cached binary data, if it exists
     * @param valueSize    specifies the size in bytes of the memory pointed to by <value>
     * @return             If the cache contains a value associated with the given key then the
     *                     size of that binary value in bytes is returned. Otherwise 0 is returned.
     */
    /**
     * 从缓存检索 Blob
     * 
     * 要从缓存中检索与给定键关联的二进制值，后端实现可以调用应用程序提供的回调函数
     * retrieveBlob。
     * 
     * 行为：
     * - 如果缓存包含给定键的值且其大小（字节）小于或等于 valueSize，则将值写入 value
     *   指向的内存
     * - 否则，不会向 value 指向的内存写入任何内容
     * 
     * @param key       指向键开头的指针
     * @param keySize   指定 key 指向的二进制键的大小（字节）
     * @param value     指向接收缓存二进制数据的缓冲区的指针（如果存在）
     * @param valueSize 指定 value 指向的内存的大小（字节）
     * @return 如果缓存包含与给定键关联的值，则返回该二进制值的大小（字节）。否则返回 0
     */
    size_t retrieveBlob(const void* UTILS_NONNULL key, size_t keySize,
            void* UTILS_NONNULL value, size_t valueSize);

    // --------------------------------------------------------------------------------------------
    // Debugging APIs

    /**
     * 调试更新统计函数类型
     * 
     * 应用程序提供的函数的 Invocable，后端可以使用它来更新后端特定的统计信息以帮助调试。
     * 
     * 签名：void(const char* key, uint64_t intValue, utils::CString stringValue)
     * 
     * 注意：对于任何给定的调用，只有一个值参数（intValue 或 stringValue）有意义，具体取决于键。
     */
    using DebugUpdateStatFunc = utils::Invocable<void(const char* UTILS_NONNULL key,
            uint64_t intValue, utils::CString stringValue)>;

    /**
     * Sets the callback function that the backend can use to update backend-specific statistics
     * to aid with debugging. This callback is guaranteed to be called on the Filament driver
     * thread.
     *
     * The callback signature is (key, intValue, stringValue). Note that for any given call,
     * only one of the value parameters (intValue or stringValue) will be meaningful, depending on
     * the specific key.
     *
     * IMPORTANT_NOTE: because the callback is called on the driver thread, only quick, non-blocking
     * work should be done inside it. Furthermore, no graphics API calls (such as GL calls) should
     * be made, which could interfere with Filament's driver state.
     *
     * @param debugUpdateStat   an Invocable that updates debug statistics
     */
    /**
     * 设置调试更新统计回调函数
     * 
     * 设置后端可用于更新后端特定统计信息以帮助调试的回调函数。
     * 此回调保证在 Filament 驱动线程上调用。
     * 
     * 回调签名：
     * - (key, intValue, stringValue)
     * - 对于任何给定的调用，只有一个值参数（intValue 或 stringValue）有意义，具体取决于键
     * 
     * 重要提示：
     * - 因为回调在驱动线程上调用，只应在其中执行快速、非阻塞的工作
     * - 不应进行任何图形 API 调用（如 GL 调用），这可能会干扰 Filament 的驱动状态
     * 
     * @param debugUpdateStat 更新调试统计信息的 Invocable
     */
    void setDebugUpdateStatFunc(DebugUpdateStatFunc&& debugUpdateStat) noexcept;

    /**
     * @return true if debugUpdateStat is valid.
     */
    /**
     * 检查 debugUpdateStat 是否有效
     * 
     * @return 如果 debugUpdateStat 有效返回 true
     */
    bool hasDebugUpdateStatFunc() const noexcept;

    /**
     * To track backend-specific statistics, the backend implementation can call the
     * application-provided callback function debugUpdateStatFunc to associate or update a value
     * with a given key. It is possible for this function to be called multiple times with the
     * same key, in which case newer values should overwrite older values.
     *
     * This function is guaranteed to be called only on a single thread, the Filament driver
     * thread.
     *
     * @param key           a null-terminated C-string with the key of the debug statistic
     * @param intValue      the updated integer value of key (the string value passed to the
     *                      callback will be empty)
     */
    /**
     * 更新调试统计信息（整数）
     * 
     * 要跟踪后端特定的统计信息，后端实现可以调用应用程序提供的回调函数 debugUpdateStatFunc
     * 来将值与给定键关联或更新。
     * 
     * 注意：
     * - 可以使用相同的键多次调用此函数，在这种情况下，新值应覆盖旧值
     * - 此函数保证仅在单个线程（Filament 驱动线程）上调用
     * 
     * @param key       调试统计信息的键（以 null 结尾的 C 字符串）
     * @param intValue  键的更新整数值（传递给回调的字符串值将为空）
     */
    void debugUpdateStat(const char* UTILS_NONNULL key, uint64_t intValue);

    /**
     * To track backend-specific statistics, the backend implementation can call the
     * application-provided callback function debugUpdateStatFunc to associate or update a value
     * with a given key. It is possible for this function to be called multiple times with the
     * same key, in which case newer values should overwrite older values.
     *
     * This function is guaranteed to be called only on a single thread, the Filament driver
     * thread.
     *
     * @param key           a null-terminated C-string with the key of the debug statistic
     * @param stringValue   the updated string value of key (the integer value passed to the
     *                      callback will be 0)
     */
    /**
     * 更新调试统计信息（字符串）
     * 
     * 要跟踪后端特定的统计信息，后端实现可以调用应用程序提供的回调函数 debugUpdateStatFunc
     * 来将值与给定键关联或更新。
     * 
     * 注意：
     * - 可以使用相同的键多次调用此函数，在这种情况下，新值应覆盖旧值
     * - 此函数保证仅在单个线程（Filament 驱动线程）上调用
     * 
     * @param key         调试统计信息的键（以 null 结尾的 C 字符串）
     * @param stringValue 键的更新字符串值（传递给回调的整数值将为 0）
     */
    void debugUpdateStat(const char* UTILS_NONNULL key, utils::CString stringValue);

private:
    std::shared_ptr<InsertBlobFunc> mInsertBlob;
    std::shared_ptr<RetrieveBlobFunc> mRetrieveBlob;
    std::shared_ptr<DebugUpdateStatFunc> mDebugUpdateStat;
    mutable utils::Mutex mMutex;
};

} // namespace filament

#endif // TNT_FILAMENT_BACKEND_PLATFORM_H
