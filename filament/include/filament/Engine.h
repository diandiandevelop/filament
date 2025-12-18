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

#ifndef TNT_FILAMENT_ENGINE_H
#define TNT_FILAMENT_ENGINE_H


#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>
#include <backend/Platform.h>

#include <utils/compiler.h>
#include <utils/Invocable.h>
#include <utils/Slice.h>

#include <initializer_list>
#include <optional>

#include <stdint.h>
#include <stddef.h>


namespace utils {
class Entity;
class EntityManager;
class JobSystem;
} // namespace utils

namespace filament {

namespace backend {
class Driver;
} // backend

class BufferObject;
class Camera;
class ColorGrading;
class DebugRegistry;
class Fence;
class IndexBuffer;
class SkinningBuffer;
class IndirectLight;
class Material;
class MaterialInstance;
class MorphTargetBuffer;
class Renderer;
class RenderTarget;
class Scene;
class Skybox;
class Stream;
class SwapChain;
class Sync;
class Texture;
class VertexBuffer;
class View;
class InstanceBuffer;

class LightManager;
class RenderableManager;
class TransformManager;

#ifndef FILAMENT_PER_RENDER_PASS_ARENA_SIZE_IN_MB
#    define FILAMENT_PER_RENDER_PASS_ARENA_SIZE_IN_MB 3
#endif

#ifndef FILAMENT_PER_FRAME_COMMANDS_SIZE_IN_MB
#    define FILAMENT_PER_FRAME_COMMANDS_SIZE_IN_MB 2
#endif

#ifndef FILAMENT_MIN_COMMAND_BUFFERS_SIZE_IN_MB
#    define FILAMENT_MIN_COMMAND_BUFFERS_SIZE_IN_MB 1
#endif

#ifndef FILAMENT_COMMAND_BUFFER_SIZE_IN_MB
#    define FILAMENT_COMMAND_BUFFER_SIZE_IN_MB (FILAMENT_MIN_COMMAND_BUFFERS_SIZE_IN_MB * 3)
#endif

/**
 * Engine is filament's main entry-point.
 *
 * An Engine instance main function is to keep track of all resources created by the user and
 * manage the rendering thread as well as the hardware renderer.
 *
 * To use filament, an Engine instance must be created first:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * #include <filament/Engine.h>
 * using namespace filament;
 *
 * Engine* engine = Engine::create();
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Engine essentially represents (or is associated to) a hardware context
 * (e.g. an OpenGL ES context).
 *
 * Rendering typically happens in an operating system's window (which can be full screen), such
 * window is managed by a filament.Renderer.
 *
 * A typical filament render loop looks like this:
 *
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * #include <filament/Engine.h>
 * #include <filament/Renderer.h>
 * #include <filament/Scene.h>
 * #include <filament/View.h>
 * using namespace filament;
 *
 * Engine* engine       = Engine::create();
 * SwapChain* swapChain = engine->createSwapChain(nativeWindow);
 * Renderer* renderer   = engine->createRenderer();
 * Scene* scene         = engine->createScene();
 * View* view           = engine->createView();
 *
 * view->setScene(scene);
 *
 * do {
 *     // typically we wait for VSYNC and user input events
 *     if (renderer->beginFrame(swapChain)) {
 *         renderer->render(view);
 *         renderer->endFrame();
 *     }
 * } while (!quit);
 *
 * engine->destroy(view);
 * engine->destroy(scene);
 * engine->destroy(renderer);
 * engine->destroy(swapChain);
 * Engine::destroy(&engine); // clears engine*
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Resource Tracking
 * =================
 *
 *  Each Engine instance keeps track of all objects created by the user, such as vertex and index
 *  buffers, lights, cameras, etc...
 *  The user is expected to free those resources, however, leaked resources are freed when the
 *  engine instance is destroyed and a warning is emitted in the console.
 *
 * Thread safety
 * =============
 *
 * An Engine instance is not thread-safe. The implementation makes no attempt to synchronize
 * calls to an Engine instance methods.
 * If multi-threading is needed, synchronization must be external.
 *
 * Multi-threading
 * ===============
 *
 * When created, the Engine instance starts a render thread as well as multiple worker threads,
 * these threads have an elevated priority appropriate for rendering, based on the platform's
 * best practices. The number of worker threads depends on the platform and is automatically
 * chosen for best performance.
 *
 * On platforms with asymmetric cores (e.g. ARM's Big.Little), Engine makes some educated guesses
 * as to which cores to use for the render thread and worker threads. For example, it'll try to
 * keep an OpenGL ES thread on a Big core.
 *
 * Swap Chains
 * ===========
 *
 * A swap chain represents an Operating System's *native* renderable surface. Typically it's a window
 * or a view. Because a SwapChain is initialized from a native object, it is given to filament
 * as a `void*`, which must be of the proper type for each platform filament is running on.
 *
 * @see SwapChain
 *
 *
 * @see Renderer
 */
/**
 * Engine 是 Filament 的主入口点
 *
 * Engine 实例的主要功能是跟踪用户创建的所有资源，并
 * 管理渲染线程以及硬件渲染器。
 *
 * 要使用 Filament，必须先创建 Engine 实例：
 *
 * Engine 本质上表示（或关联到）一个硬件上下文
 * （例如 OpenGL ES 上下文）。
 *
 * 渲染通常在操作系统的窗口（可以是全屏）中进行，此类
 * 窗口由 filament.Renderer 管理。
 *
 * 资源跟踪
 * =================
 *
 *  每个 Engine 实例跟踪用户创建的所有对象，例如顶点和索引
 *  缓冲区、光源、相机等...
 *  用户应该释放这些资源，但是，泄漏的资源会在
 *  engine 实例被销毁时释放，并在控制台发出警告。
 *
 * 线程安全
 * =============
 *
 * Engine 实例不是线程安全的。实现不会尝试同步
 * 对 Engine 实例方法的调用。
 * 如果需要多线程，必须进行外部同步。
 *
 * 多线程
 * ===============
 *
 * 创建时，Engine 实例会启动一个渲染线程以及多个工作线程，
 * 这些线程具有适合渲染的较高优先级，基于平台的
 * 最佳实践。工作线程的数量取决于平台，并自动
 * 选择以获得最佳性能。
 *
 * 在具有非对称核心的平台（例如 ARM 的 Big.Little）上，Engine 会做出一些
 * 有根据的猜测，决定将哪些核心用于渲染线程和工作线程。例如，它会尝试
 * 将 OpenGL ES 线程保持在 Big 核心上。
 *
 * 交换链
 * ===========
 *
 * 交换链表示操作系统的*原生*可渲染表面。通常是窗口
 * 或视图。因为 SwapChain 是从原生对象初始化的，所以它被提供给 Filament
 * 作为一个 `void*`，对于 Filament 运行的每个平台，它必须是正确的类型。
 *
 * @see SwapChain, Renderer
 */
class UTILS_PUBLIC Engine {
    struct BuilderDetails;
public:
    using Platform = backend::Platform;
    using Backend = backend::Backend;
    using DriverConfig = backend::Platform::DriverConfig;
    using FeatureLevel = backend::FeatureLevel;
    using StereoscopicType = backend::StereoscopicType;
    using Driver = backend::Driver;
    using GpuContextPriority = backend::Platform::GpuContextPriority;
    using AsynchronousMode = backend::AsynchronousMode;

    /**
     * Config is used to define the memory footprint used by the engine, such as the
     * command buffer size. Config can be used to customize engine requirements based
     * on the applications needs.
     *
     *    .perRenderPassArenaSizeMB (default: 3 MiB)
     *   +--------------------------+
     *   |                          |
     *   | .perFrameCommandsSizeMB  |
     *   |    (default 2 MiB)       |
     *   |                          |
     *   +--------------------------+
     *   |  (froxel, etc...)        |
     *   +--------------------------+
     *
     *
     *      .commandBufferSizeMB (default 3MiB)
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   :                          :
     *   :                          :
     *
     */
    /**
     * Config 用于定义引擎使用的内存占用，例如
     * 命令缓冲区大小。Config 可用于根据
     * 应用程序的需求自定义引擎要求。
     *
     *    .perRenderPassArenaSizeMB (默认：3 MiB)
     *   +--------------------------+
     *   |                          |
     *   | .perFrameCommandsSizeMB  |
     *   |    (默认 2 MiB)           |
     *   |                          |
     *   +--------------------------+
     *   |  (froxel 等...)           |
     *   +--------------------------+
     *
     *
     *      .commandBufferSizeMB (默认 3MiB)
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   | .minCommandBufferSizeMB  |
     *   +--------------------------+
     *   :                          :
     *   :                          :
     */
    struct Config {
        /**
         * Size in MiB of the low-level command buffer arena.
         *
         * Each new command buffer is allocated from here. If this buffer is too small the program
         * might terminate or rendering errors might occur.
         *
         * This is typically set to minCommandBufferSizeMB * 3, so that up to 3 frames can be
         * batched-up at once.
         *
         * This value affects the application's memory usage.
         */
        /**
         * 低级命令缓冲区区域的大小（MiB）
         *
         * 每个新的命令缓冲区都从这里分配。如果此缓冲区太小，程序
         * 可能会终止或可能发生渲染错误。
         *
         * 通常设置为 minCommandBufferSizeMB * 3，以便最多可以
         * 一次批处理 3 帧。
         *
         * 此值影响应用程序的内存使用。
         */
        uint32_t commandBufferSizeMB = FILAMENT_COMMAND_BUFFER_SIZE_IN_MB;


        /**
         * Size in MiB of the per-frame data arena.
         *
         * This is the main arena used for allocations when preparing a frame.
         * e.g.: Froxel data and high-level commands are allocated from this arena.
         *
         * If this size is too small, the program will abort on debug builds and have undefined
         * behavior otherwise.
         *
         * This value affects the application's memory usage.
         */
        /**
         * 每帧数据区域的大小（MiB）
         *
         * 这是在准备帧时用于分配的主区域。
         * 例如：Froxel 数据和高级命令从此区域分配。
         *
         * 如果此大小太小，程序将在调试构建时中止，否则会有未定义
         * 的行为。
         *
         * 此值影响应用程序的内存使用。
         */
        uint32_t perRenderPassArenaSizeMB = FILAMENT_PER_RENDER_PASS_ARENA_SIZE_IN_MB;


        /**
         * Size in MiB of the backend's handle arena.
         *
         * Backends will fallback to slower heap-based allocations when running out of space and
         * log this condition.
         *
         * If 0, then the default value for the given platform is used
         *
         * This value affects the application's memory usage.
         */
        /**
         * 后端的句柄区域大小（MiB）
         *
         * 当空间用完时，后端将回退到较慢的基于堆的分配，并
         * 记录此条件。
         *
         * 如果为 0，则使用给定平台的默认值
         *
         * 此值影响应用程序的内存使用。
         */
        uint32_t driverHandleArenaSizeMB = 0;


        /**
         * Minimum size in MiB of a low-level command buffer.
         *
         * This is how much space is guaranteed to be available for low-level commands when a new
         * buffer is allocated. If this is too small, the engine might have to stall to wait for
         * more space to become available, this situation is logged.
         *
         * This value does not affect the application's memory usage.
         */
        /**
         * 低级命令缓冲区的最小大小（MiB）
         *
         * 这是在分配新缓冲区时保证可用于低级命令的
         * 空间量。如果太小，引擎可能需要暂停以等待
         * 更多空间可用，此情况会被记录。
         *
         * 此值不影响应用程序的内存使用。
         */
        uint32_t minCommandBufferSizeMB = FILAMENT_MIN_COMMAND_BUFFERS_SIZE_IN_MB;


        /**
         * Size in MiB of the per-frame high level command buffer.
         *
         * This buffer is related to the number of draw calls achievable within a frame, if it is
         * too small, the program will abort on debug builds and have undefined behavior otherwise.
         *
         * It is allocated from the 'per-render-pass arena' above. Make sure that at least 1 MiB is
         * left in the per-render-pass arena when deciding the size of this buffer.
         *
         * This value does not affect the application's memory usage.
         */
        /**
         * 每帧高级命令缓冲区的大小（MiB）
         *
         * 此缓冲区与帧内可实现的绘制调用次数相关，如果它
         * 太小，程序将在调试构建时中止，否则会有未定义行为。
         *
         * 它从上面的"每渲染通道区域"分配。决定此缓冲区的大小时，
         * 请确保在每渲染通道区域中至少保留 1 MiB。
         *
         * 此值不影响应用程序的内存使用。
         */
        uint32_t perFrameCommandsSizeMB = FILAMENT_PER_FRAME_COMMANDS_SIZE_IN_MB;

        /**
         * Number of threads to use in Engine's JobSystem.
         *
         * Engine uses a utils::JobSystem to carry out paralleization of Engine workloads. This
         * value sets the number of threads allocated for JobSystem. Configuring this value can be
         * helpful in CPU-constrained environments where too many threads can cause contention of
         * CPU and reduce performance.
         *
         * The default value is 0, which implies that the Engine will use a heuristic to determine
         * the number of threads to use.
         */
        /**
         * Engine 的 JobSystem 中使用的线程数
         *
         * Engine 使用 utils::JobSystem 来执行 Engine 工作负载的并行化。此
         * 值设置为 JobSystem 分配的线程数。在 CPU 受限的环境中配置此值可能
         * 很有帮助，因为太多线程可能导致 CPU 竞争并降低性能。
         *
         * 默认值为 0，这意味着 Engine 将使用启发式方法来确定
         * 要使用的线程数。
         */
        uint32_t jobSystemThreadCount = 0;

        /**
         * When uploading vertex or index data, the Filament Metal backend copies data
         * into a shared staging area before transferring it to the GPU. This setting controls
         * the total size of the buffer used to perform these allocations.
         *
         * Higher values can improve performance when performing many uploads across a small
         * number of frames.
         *
         * This buffer remains alive throughout the lifetime of the Engine, so this size adds to the
         * memory footprint of the app and should be set as conservative as possible.
         *
         * A value of 0 disables the shared staging buffer entirely; uploads will acquire an
         * individual buffer from a pool of shared buffers.
         *
         * Only respected by the Metal backend.
         */
        /**
         * 上传顶点或索引数据时，Filament Metal 后端在将数据传输到 GPU 之前
         * 将数据复制到共享暂存区域。此设置控制用于执行这些分配的
         * 缓冲区总大小。
         *
         * 在少量帧中执行多次上传时，更高的值可以提高性能。
         *
         * 此缓冲区在 Engine 的整个生命周期内保持活动状态，因此此大小会增加
         * 应用程序的内存占用，应尽可能保守设置。
         *
         * 值为 0 会完全禁用共享暂存缓冲区；上传将从共享缓冲区池中获取
         * 单独的缓冲区。
         *
         * 仅由 Metal 后端遵守。
         */
        size_t metalUploadBufferSizeBytes = 512 * 1024;

        /**
         * The action to take if a Drawable cannot be acquired.
         *
         * Each frame rendered requires a CAMetalDrawable texture, which is
         * presented on-screen at the completion of each frame. These are
         * limited and provided round-robin style by the system.
         */
        /**
         * 如果无法获取 Drawable 时要采取的操作。
         *
         * 渲染的每一帧都需要一个 CAMetalDrawable 纹理，该纹理在
         * 每帧完成时在屏幕上呈现。这些由系统以轮询方式
         * 提供，数量有限。
         */
        bool metalDisablePanicOnDrawableFailure = false;

        /**
         * Set to `true` to forcibly disable parallel shader compilation in the backend.
         * Currently only honored by the GL and Metal backends.
         * @deprecated use "backend.disable_parallel_shader_compile" feature flag instead
         */
        /**
         * 设置为 `true` 以强制禁用后端中的并行着色器编译。
         * 目前仅由 GL 和 Metal 后端遵守。
         * @deprecated 改用 "backend.disable_parallel_shader_compile" 功能标志
         */
        bool disableParallelShaderCompile = false;

        /*
         * The type of technique for stereoscopic rendering.
         *
         * This setting determines the algorithm used when stereoscopic rendering is enabled. This
         * decision applies to the entire Engine for the lifetime of the Engine. E.g., multiple
         * Views created from the Engine must use the same stereoscopic type.
         *
         * Each view can enable stereoscopic rendering via the StereoscopicOptions::enable flag.
         *
         * @see View::setStereoscopicOptions
         */
        /**
         * 立体渲染的技术类型
         *
         * 此设置确定启用立体渲染时使用的算法。此
         * 决定在整个 Engine 的生命周期内应用于整个 Engine。例如，从
         * Engine 创建的多个 View 必须使用相同的立体类型。
         *
         * 每个视图可以通过 StereoscopicOptions::enable 标志启用立体渲染。
         *
         * @see View::setStereoscopicOptions
         */
        StereoscopicType stereoscopicType = StereoscopicType::NONE;

        /*
         * The number of eyes to render when stereoscopic rendering is enabled. Supported values are
         * between 1 and Engine::getMaxStereoscopicEyes() (inclusive).
         *
         * @see View::setStereoscopicOptions
         * @see Engine::getMaxStereoscopicEyes
         */
        /**
         * 启用立体渲染时要渲染的眼睛数量。支持的值在
         * 1 和 Engine::getMaxStereoscopicEyes()（含）之间。
         *
         * @see View::setStereoscopicOptions
         * @see Engine::getMaxStereoscopicEyes
         */
        uint8_t stereoscopicEyeCount = 2;

        /*
         * @deprecated This value is no longer used.
         */
        /**
         * @deprecated 此值不再使用
         */
        uint32_t resourceAllocatorCacheSizeMB = 64;

        /*
         * This value determines how many frames texture entries are kept for in the cache. This
         * is a soft limit, meaning some texture older than this are allowed to stay in the cache.
         * Typically only one texture is evicted per frame.
         * The default is 1.
         */
        /**
         * 此值确定纹理条目在缓存中保留多少帧。这是
         * 一个软限制，意味着允许比此更旧的纹理保留在缓存中。
         * 通常每帧只驱逐一个纹理。
         * 默认值为 1。
         */
        uint32_t resourceAllocatorCacheMaxAge = 1;

        /*
         * Disable backend handles use-after-free checks.
         * @deprecated use "backend.disable_handle_use_after_free_check" feature flag instead
         */
        /**
         * 禁用后端句柄的释放后使用检查
         * @deprecated 改用 "backend.disable_handle_use_after_free_check" 功能标志
         */
        bool disableHandleUseAfterFreeCheck = false;

        /*
         * Sets a preferred shader language for Filament to use.
         *
         * The Metal backend supports two shader languages: MSL (Metal Shading Language) and
         * METAL_LIBRARY (precompiled .metallib). This option controls which shader language is
         * used when materials contain both.
         *
         * By default, when preferredShaderLanguage is unset, Filament will prefer METAL_LIBRARY
         * shaders if present within a material, falling back to MSL. Setting
         * preferredShaderLanguage to ShaderLanguage::MSL will instead instruct Filament to check
         * for the presence of MSL in a material first, falling back to METAL_LIBRARY if MSL is not
         * present.
         *
         * When using a non-Metal backend, setting this has no effect.
         */
        /**
         * 设置 Filament 使用的首选着色器语言
         *
         * Metal 后端支持两种着色器语言：MSL（Metal 着色语言）和
         * METAL_LIBRARY（预编译的 .metallib）。此选项控制在
         * 材质同时包含两者时使用哪种着色器语言。
         *
         * 默认情况下，当 preferredShaderLanguage 未设置时，如果材质中存在
         * METAL_LIBRARY 着色器，Filament 将优先使用它，否则回退到 MSL。将
         * preferredShaderLanguage 设置为 ShaderLanguage::MSL 将指示 Filament 首先检查
         * 材质中是否存在 MSL，如果不存在 MSL，则回退到 METAL_LIBRARY。
         *
         * 使用非 Metal 后端时，设置此选项无效。
         */
        enum class ShaderLanguage {
            DEFAULT = 0,
            /**
             * 默认值
             */
            MSL = 1,
            /**
             * Metal 着色语言
             */
            METAL_LIBRARY = 2,
            /**
             * 预编译的 Metal 库
             */
        };
        ShaderLanguage preferredShaderLanguage = ShaderLanguage::DEFAULT;

        /*
         * When the OpenGL ES backend is used, setting this value to true will force a GLES2.0
         * context if supported by the Platform, or if not, will have the backend pretend
         * it's a GLES2 context. Ignored on other backends.
         */
        /**
         * 当使用 OpenGL ES 后端时，如果 Platform 支持，将此值设置为 true 将强制使用 GLES2.0
         * 上下文，如果不支持，将让后端假装
         * 它是 GLES2 上下文。在其他后端上会被忽略。
         */
        bool forceGLES2Context = false;

        /**
         * Assert the native window associated to a SwapChain is valid when calling makeCurrent().
         * This is only supported for:
         *      - PlatformEGLAndroid
         * @deprecated use "backend.opengl.assert_native_window_is_valid" feature flag instead
         */
        /**
         * 在调用 makeCurrent() 时断言与 SwapChain 关联的本机窗口有效。
         * 仅支持：
         *      - PlatformEGLAndroid
         * @deprecated 改用 "backend.opengl.assert_native_window_is_valid" 功能标志
         */
        bool assertNativeWindowIsValid = false;

        /**
         * GPU context priority level. Controls GPU work scheduling and preemption.
         */
        /**
         * GPU 上下文优先级级别。控制 GPU 工作调度和抢占。
         */
        GpuContextPriority gpuContextPriority = GpuContextPriority::DEFAULT;

        /**
         * The initial size in bytes of the shared uniform buffer used for material instance
         * batching.
         *
         * If the buffer runs out of space during a frame, it will be automatically reallocated
         * with a larger capacity. Setting an appropriate initial size can help avoid runtime
         * reallocations, which can cause a minor performance stutter, at the cost of higher
         * initial memory usage.
         */
        /**
         * 用于材质实例批处理的共享 uniform 缓冲区的初始大小（字节）
         *
         * 如果缓冲区在一帧期间空间不足，它将自动重新分配
         * 为更大的容量。设置适当的初始大小可以帮助避免运行时
         * 重新分配，这可能造成轻微的性能抖动，代价是更高的
         * 初始内存使用。
         */
        uint32_t sharedUboInitialSizeInBytes = 256 * 64;

        /**
         * Asynchronous mode for the engine. Defines how asynchronous operations are handled.
         */
        /**
         * 引擎的异步模式。定义如何处理异步操作。
         */
        AsynchronousMode asynchronousMode = AsynchronousMode::NONE;
    };


    /**
     * Feature flags can be enabled or disabled when the Engine is built. Some Feature flags can
     * also be toggled at any time. Feature flags should alawys use their default value unless
     * the feature enabled by the flag is faulty. Feature flags provide a last resort way to
     * disable problematic features.
     * Feature flags are intended to have a short life-time and are regularly removed as features
     * mature.
     */
    /**
     * 功能标志可以在构建 Engine 时启用或禁用。某些功能标志也可以
     * 随时切换。功能标志应始终使用其默认值，除非
     * 标志启用的功能有故障。功能标志提供了最后的手段来
     * 禁用有问题的功能。
     * 功能标志预期具有较短的生存期，并随着功能
     * 成熟而定期移除。
     */
    struct FeatureFlag {
        char const* UTILS_NONNULL name;         //!< name of the feature flag
        /**
         * 功能标志的名称
         */
        char const* UTILS_NONNULL description;  //!< short description
        /**
         * 简短描述
         */
        bool const* UTILS_NONNULL value;        //!< pointer to the value of the flag
        /**
         * 指向标志值的指针
         */
        bool constant = true;                          //!< whether the flag is constant after construction
        /**
         * 标志在构造后是否恒定
         */

    };

    /**
     * Returns the list of available feature flags
     */
    /**
     * 返回可用功能标志列表
     */
    utils::Slice<const FeatureFlag> getFeatureFlags() const noexcept;

#if UTILS_HAS_THREADING
    using CreateCallback = void(void* UTILS_NULLABLE user, void* UTILS_NONNULL token);
#endif

    /**
     * Engine::Builder is used to create a new filament Engine.
     */
    /**
     * Engine::Builder 用于创建新的 Filament Engine
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
        friend class FEngine;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * @param backend Which driver backend to use
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * @param backend 要使用的驱动程序后端
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& backend(Backend backend) noexcept;

        /**
         * @param platform A pointer to an object that implements Platform. If this is
         *                 provided, then this object is used to create the hardware context
         *                 and expose platform features to it.
         *
         *                 If not provided (or nullptr is used), an appropriate Platform
         *                 is created automatically.
         *
         *                 All methods of this interface are called from filament's
         *                 render thread, which is different from the main thread.
         *
         *                 The lifetime of \p platform must exceed the lifetime of
         *                 the Engine object.
         *
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * @param platform 指向实现 Platform 的对象的指针。如果提供，
         *                 则使用此对象创建硬件上下文
         *                 并向其公开平台功能。
         *
         *                 如果未提供（或使用 nullptr），则会自动创建
         *                 适当的 Platform。
         *
         *                 此接口的所有方法都从 Filament 的
         *                 渲染线程调用，这与主线程不同。
         *
         *                 \p platform 的生存期必须超过
         *                 Engine 对象的生存期。
         *
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& platform(Platform* UTILS_NULLABLE platform) noexcept;

        /**
         * @param config    A pointer to optional parameters to specify memory size
         *                  configuration options.  If nullptr, then defaults used.
         *
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * @param config    指向可选参数的指针，用于指定内存大小
         *                  配置选项。如果为 nullptr，则使用默认值。
         *
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& config(const Config* UTILS_NULLABLE config) noexcept;

        /**
         * @param sharedContext A platform-dependant context used as a shared context
         *                      when creating filament's internal context.
         *
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * @param sharedContext 平台相关的上下文，在创建 Filament 的
         *                      内部上下文时用作共享上下文。
         *
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& sharedContext(void* UTILS_NULLABLE sharedContext) noexcept;

        /**
         * @param featureLevel The feature level at which initialize Filament.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * @param featureLevel 初始化 Filament 的功能级别
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& featureLevel(FeatureLevel featureLevel) noexcept;

        /**
         * Warning: This is an experimental API. See Engine::setPaused(bool) for caveats.
         *
         * @param paused Whether to start the rendering thread paused.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 警告：这是一个实验性 API。有关注意事项，请参见 Engine::setPaused(bool)。
         *
         * @param paused 是否以暂停状态启动渲染线程
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& paused(bool paused) noexcept;

        /**
         * Set a feature flag value. This is the only way to set constant feature flags.
         * @param name feature name
         * @param value true to enable, false to disable
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 设置功能标志值。这是设置常量功能标志的唯一方法。
         * @param name 功能名称
         * @param value true 表示启用，false 表示禁用
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& feature(char const* UTILS_NONNULL name, bool value) noexcept;

        /**
         * Enables a list of features.
         * @param list list of feature names to enable.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 启用功能列表。
         * @param list 要启用的功能名称列表
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& features(std::initializer_list<char const *> list) noexcept;

#if UTILS_HAS_THREADING
        /**
         * Creates the filament Engine asynchronously.
         *
         * @param callback  Callback called once the engine is initialized and it is safe to
         *                  call Engine::getEngine().
         */
        /**
         * 异步创建 Filament Engine。
         *
         * @param callback  在引擎初始化完成后调用，此时可以安全地
         *                  调用 Engine::getEngine()。
         */
        void build(utils::Invocable<void(void* UTILS_NONNULL token)>&& callback) const;
#endif

        /**
         * Creates an instance of Engine.
         *
         * @return  A pointer to the newly created Engine, or nullptr if the Engine couldn't be
         *          created.
         *          nullptr if the GPU driver couldn't be initialized, for instance if it doesn't
         *          support the right version of OpenGL or OpenGL ES.
         *
         * @exception   utils::PostConditionPanic can be thrown if there isn't enough memory to
         *              allocate the command buffer. If exceptions are disabled, this condition if
         *              fatal and this function will abort.
         */
        /**
         * 创建 Engine 实例
         *
         * @return  指向新创建的 Engine 的指针，如果无法创建 Engine，则为 nullptr。
         *          如果 GPU 驱动程序无法初始化，例如它不支持
         *          正确版本的 OpenGL 或 OpenGL ES，则返回 nullptr。
         *
         * @exception   如果没有足够的内存来
         *              分配命令缓冲区，可能会抛出 utils::PostConditionPanic。如果禁用了异常，此条件是
         *              致命的，此函数将中止。
         */
        Engine* UTILS_NULLABLE build() const;
    };

    /**
     * Backward compatibility helper to create an Engine.
     * @see Builder
     */
    /**
     * 用于创建 Engine 的向后兼容辅助函数
     * @see Builder
     */
    static inline Engine* UTILS_NULLABLE create(Backend backend = Backend::DEFAULT,
            Platform* UTILS_NULLABLE platform = nullptr,
            void* UTILS_NULLABLE sharedContext = nullptr,
            const Config* UTILS_NULLABLE config = nullptr) {
        return Builder()
                .backend(backend)
                .platform(platform)
                .sharedContext(sharedContext)
                .config(config)
                .build();
    }


#if UTILS_HAS_THREADING
    /**
     * Backward compatibility helper to create an Engine asynchronously.
     * @see Builder
     */
    /**
     * 用于异步创建 Engine 的向后兼容辅助函数
     * @see Builder
     */
    static inline void createAsync(CreateCallback callback,
            void* UTILS_NULLABLE user,
            Backend backend = Backend::DEFAULT,
            Platform* UTILS_NULLABLE platform = nullptr,
            void* UTILS_NULLABLE sharedContext = nullptr,
            const Config* UTILS_NULLABLE config = nullptr) {
        Builder()
                .backend(backend)
                .platform(platform)
                .sharedContext(sharedContext)
                .config(config)
                .build([callback, user](void* UTILS_NONNULL token) {
                    callback(user, token);
                });
    }

    /**
     * Retrieve an Engine* from createAsync(). This must be called from the same thread than
     * Engine::createAsync() was called from.
     *
     * @param token An opaque token given in the createAsync() callback function.
     *
     * @return A pointer to the newly created Engine, or nullptr if the Engine couldn't be created.
     *
     * @exception utils::PostConditionPanic can be thrown if there isn't enough memory to
     * allocate the command buffer. If exceptions are disabled, this condition if fatal and
     * this function will abort.
     */
    /**
     * 从 createAsync() 检索 Engine*。必须从与调用 Engine::createAsync() 相同的线程调用。
     *
     * @param token 在 createAsync() 回调函数中提供的不透明令牌
     *
     * @return 指向新创建的 Engine 的指针，如果无法创建 Engine，则为 nullptr
     *
     * @exception 如果没有足够的内存来
     * 分配命令缓冲区，可能会抛出 utils::PostConditionPanic。如果禁用了异常，此条件是致命的，
     * 此函数将中止。
     */
    static Engine* UTILS_NULLABLE getEngine(void* UTILS_NONNULL token);
#endif

    /**
     * @return the Driver instance used by this Engine.
     * @see OpenGLPlatform
     */
    /**
     * @return 此 Engine 使用的 Driver 实例
     * @see OpenGLPlatform
     */
    backend::Driver const* UTILS_NONNULL getDriver() const noexcept;

    /**
     * Destroy the Engine instance and all associated resources.
     *
     * Engine.destroy() should be called last and after all other resources have been destroyed,
     * it ensures all filament resources are freed.
     *
     * Destroy performs the following tasks:
     * 1. Destroy all internal software and hardware resources.
     * 2. Free all user allocated resources that are not already destroyed and logs a warning.
     *    This indicates a "leak" in the user's code.
     * 3. Terminate the rendering engine's thread.
     *
     * @param engine A pointer to the filament.Engine* to be destroyed.
     *               \p engine is cleared upon return.
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * #include <filament/Engine.h>
     * using namespace filament;
     *
     * Engine* engine = Engine::create();
     * Engine::destroy(&engine); // clears engine*
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * \remark
     * This method is thread-safe.
     */
    /**
     * 销毁 Engine 实例和所有关联的资源
     *
     * Engine.destroy() 应该最后调用，并且在所有其他资源已被销毁之后调用，
     * 它确保所有 Filament 资源都被释放。
     *
     * Destroy 执行以下任务：
     * 1. 销毁所有内部软件和硬件资源。
     * 2. 释放所有尚未销毁的用户分配的资源并记录警告。
     *    这表示用户代码中的"泄漏"。
     * 3. 终止渲染引擎的线程。
     *
     * @param engine 指向要销毁的 filament.Engine* 的指针。
     *               \p engine 在返回时被清除。
     *
     * \remark
     * 此方法是线程安全的。
     */
    static void destroy(Engine* UTILS_NULLABLE* UTILS_NULLABLE engine);

    /**
     * Destroy the Engine instance and all associated resources.
     *
     * Engine.destroy() should be called last and after all other resources have been destroyed,
     * it ensures all filament resources are freed.
     *
     * Destroy performs the following tasks:
     * 1. Destroy all internal software and hardware resources.
     * 2. Free all user allocated resources that are not already destroyed and logs a warning.
     *    This indicates a "leak" in the user's code.
     * 3. Terminate the rendering engine's thread.
     *
     * @param engine A pointer to the filament.Engine to be destroyed.
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * #include <filament/Engine.h>
     * using namespace filament;
     *
     * Engine* engine = Engine::create();
     * Engine::destroy(engine);
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * \remark
     * This method is thread-safe.
     */
    /**
     * 销毁 Engine 实例和所有关联的资源
     *
     * Engine.destroy() 应该最后调用，并且在所有其他资源已被销毁之后调用，
     * 它确保所有 Filament 资源都被释放。
     *
     * Destroy 执行以下任务：
     * 1. 销毁所有内部软件和硬件资源。
     * 2. 释放所有尚未销毁的用户分配的资源并记录警告。
     *    这表示用户代码中的"泄漏"。
     * 3. 终止渲染引擎的线程。
     *
     * @param engine 指向要销毁的 filament.Engine 的指针
     *
     * \remark
     * 此方法是线程安全的。
     */
    static void destroy(Engine* UTILS_NULLABLE engine);

    /**
     * Query the feature level supported by the selected backend.
     *
     * A specific feature level needs to be set before the corresponding features can be used.
     *
     * @return FeatureLevel supported the selected backend.
     * @see setActiveFeatureLevel
     */
    /**
     * 查询所选后端支持的功能级别。
     *
     * 在使用相应功能之前，需要设置特定的功能级别。
     *
     * @return 所选后端支持的功能级别
     * @see setActiveFeatureLevel
     */
    FeatureLevel getSupportedFeatureLevel() const noexcept;

    /**
     * Activate all features of a given feature level. If an explicit feature level is not specified
     * at Engine initialization time via Builder::featureLevel, the default feature level is
     * FeatureLevel::FEATURE_LEVEL_0 on devices not compatible with GLES 3.0; otherwise, the default
     * is FeatureLevel::FEATURE_LEVEL_1. The selected feature level must not be higher than the
     * value returned by getActiveFeatureLevel() and it's not possible lower the active feature
     * level. Additionally, it is not possible to modify the feature level at all if the Engine was
     * initialized at FeatureLevel::FEATURE_LEVEL_0.
     *
     * @param featureLevel the feature level to activate. If featureLevel is lower than
     *                     getActiveFeatureLevel(), the current (higher) feature level is kept. If
     *                     featureLevel is higher than getSupportedFeatureLevel(), or if the engine
     *                     was initialized at feature level 0, an exception is thrown, or the
     *                     program is terminated if exceptions are disabled.
     *
     * @return the active feature level.
     *
     * @see Builder::featureLevel
     * @see getSupportedFeatureLevel
     * @see getActiveFeatureLevel
     */
    /**
     * 激活给定功能级别的所有功能。如果在 Engine 初始化时未通过 Builder::featureLevel 指定
     * 显式功能级别，则在非 GLES 3.0 兼容设备上默认功能级别为 FeatureLevel::FEATURE_LEVEL_0；
     * 否则默认值为 FeatureLevel::FEATURE_LEVEL_1。所选功能级别不得高于
     * getActiveFeatureLevel() 返回的值，并且无法降低活动功能级别。
     * 此外，如果 Engine 是在 FeatureLevel::FEATURE_LEVEL_0 初始化的，则无法修改功能级别。
     *
     * @param featureLevel 要激活的功能级别。如果 featureLevel 低于
     *                     getActiveFeatureLevel()，则保持当前（更高）的功能级别。如果
     *                     featureLevel 高于 getSupportedFeatureLevel()，或者如果引擎
     *                     是在功能级别 0 初始化的，则抛出异常，或者如果禁用了异常则程序终止。
     *
     * @return 活动功能级别
     *
     * @see Builder::featureLevel
     * @see getSupportedFeatureLevel
     * @see getActiveFeatureLevel
     */
    FeatureLevel setActiveFeatureLevel(FeatureLevel featureLevel);

    /**
     * Returns the currently active feature level.
     * @return currently active feature level
     * @see getSupportedFeatureLevel
     * @see setActiveFeatureLevel
     */
    /**
     * 返回当前活动功能级别。
     * @return 当前活动功能级别
     * @see getSupportedFeatureLevel
     * @see setActiveFeatureLevel
     */
    FeatureLevel getActiveFeatureLevel() const noexcept;

    /**
     * Queries the maximum number of GPU instances that Filament creates when automatic instancing
     * is enabled. This value is also the limit for the number of transforms that can be stored in
     * an InstanceBuffer. This value may depend on the device and platform, but will remain constant
     * during the lifetime of this Engine.
     *
     * This value does not apply when using the instances(size_t) method on
     * RenderableManager::Builder.
     *
     * @return the number of max automatic instances
     * @see setAutomaticInstancingEnabled
     * @see RenderableManager::Builder::instances(size_t)
     * @see RenderableManager::Builder::instances(size_t, InstanceBuffer*)
     */
    /**
     * 查询启用自动实例化时 Filament 创建的 GPU 实例的最大数量。此值也是可以在
     * InstanceBuffer 中存储的变换数量的限制。此值可能取决于设备和平台，但在
     * 此 Engine 的生命周期内将保持不变。
     *
     * 在 RenderableManager::Builder 上使用 instances(size_t) 方法时，此值不适用。
     *
     * @return 最大自动实例数量
     * @see setAutomaticInstancingEnabled
     * @see RenderableManager::Builder::instances(size_t)
     * @see RenderableManager::Builder::instances(size_t, InstanceBuffer*)
     */
    size_t getMaxAutomaticInstances() const noexcept;

    /**
     * Queries the device and platform for support of the given stereoscopic type.
     *
     * @return true if the given stereo rendering is supported, false otherwise
     * @see View::setStereoscopicOptions
     */
    /**
     * 查询设备和平台对给定立体类型的支持。
     *
     * @return 如果支持给定的立体渲染则返回 true，否则返回 false
     * @see View::setStereoscopicOptions
     */
    bool isStereoSupported(StereoscopicType stereoscopicType) const noexcept;

    /**
     * Checks if the engine is set up for asynchronous operation. If it returns true, the
     * asynchronous versions of the APIs are available for use.
     *
     * @return true if the engine supports asynchronous operation.
     */
    /**
     * 检查引擎是否设置为异步操作。如果返回 true，则可以使用异步版本的 API。
     *
     * @return 如果引擎支持异步操作则返回 true
     */
    bool isAsynchronousOperationSupported() const noexcept;

    /**
     * Retrieves the configuration settings of this Engine.
     *
     * This method returns the configuration object that was supplied to the Engine's
     * Builder::config method during the creation of this Engine. If the Builder::config method was
     * not explicitly called (or called with nullptr), this method returns the default configuration
     * settings.
     *
     * @return a Config object with this Engine's configuration
     * @see Builder::config
     */
    /**
     * 检索此 Engine 的配置设置。
     *
     * 此方法返回在创建此 Engine 期间提供给 Engine 的
     * Builder::config 方法的配置对象。如果未显式调用 Builder::config 方法
     * （或使用 nullptr 调用），此方法返回默认配置设置。
     *
     * @return 包含此 Engine 配置的 Config 对象
     * @see Builder::config
     */
    const Config& getConfig() const noexcept;

    /**
     * Returns the maximum number of stereoscopic eyes supported by Filament. The actual number of
     * eyes rendered is set at Engine creation time with the Engine::Config::stereoscopicEyeCount
     * setting.
     *
     * @return the max number of stereoscopic eyes supported
     * @see Engine::Config::stereoscopicEyeCount
     */
    /**
     * 返回 Filament 支持的最大立体眼睛数量。实际渲染的眼睛数量
     * 在 Engine 创建时通过 Engine::Config::stereoscopicEyeCount 设置。
     *
     * @return 支持的最大立体眼睛数量
     * @see Engine::Config::stereoscopicEyeCount
     */
    static size_t getMaxStereoscopicEyes() noexcept;

    /**
     * @return EntityManager used by filament
     */
    /**
     * @return Filament 使用的 EntityManager
     */
    utils::EntityManager& getEntityManager() noexcept;

    /**
     * @return RenderableManager reference
     */
    /**
     * @return RenderableManager 引用
     */
    RenderableManager& getRenderableManager() noexcept;

    /**
     * @return LightManager reference
     */
    /**
     * @return LightManager 引用
     */
    LightManager& getLightManager() noexcept;

    /**
     * @return TransformManager reference
     */
    /**
     * @return TransformManager 引用
     */
    TransformManager& getTransformManager() noexcept;

    /**
     * Helper to enable accurate translations.
     * If you need this Engine to handle a very large world space, one way to achieve this
     * automatically is to enable accurate translations in the TransformManager. This helper
     * provides a convenient way of doing that.
     * This is typically called once just after creating the Engine.
     */
    /**
     * 启用精确平移的辅助函数。
     * 如果需要此 Engine 处理非常大的世界空间，实现此目的的一种方法是
     * 在 TransformManager 中启用精确平移。此辅助函数提供了执行此操作的便捷方法。
     * 通常只需在创建 Engine 后调用一次。
     */
    void enableAccurateTranslations() noexcept;

    /**
     * Enables or disables automatic instancing of render primitives. Instancing of render
     * primitives can greatly reduce CPU overhead but requires the instanced primitives to be
     * identical (i.e. use the same geometry) and use the same MaterialInstance. If it is known
     * that the scene doesn't contain any identical primitives, automatic instancing can have some
     * overhead and it is then best to disable it.
     *
     * Disabled by default.
     *
     * @param enable true to enable, false to disable automatic instancing.
     *
     * @see RenderableManager
     * @see MaterialInstance
     */
    /**
     * 启用或禁用渲染图元的自动实例化。渲染图元的实例化可以大大减少 CPU 开销，
     * 但要求实例化的图元相同（即使用相同的几何体）并使用相同的 MaterialInstance。
     * 如果已知场景不包含任何相同的图元，自动实例化可能会有一些开销，最好禁用它。
     *
     * 默认禁用。
     *
     * @param enable true 表示启用，false 表示禁用自动实例化
     *
     * @see RenderableManager
     * @see MaterialInstance
     */
    void setAutomaticInstancingEnabled(bool enable) noexcept;

    /**
     * @return true if automatic instancing is enabled, false otherwise.
     * @see setAutomaticInstancingEnabled
     */
    /**
     * @return 如果启用了自动实例化则返回 true，否则返回 false
     * @see setAutomaticInstancingEnabled
     */
    bool isAutomaticInstancingEnabled() const noexcept;

    /**
     * Creates a SwapChain from the given Operating System's native window handle.
     *
     * @param nativeWindow An opaque native window handle. e.g.: on Android this is an
     *                     `ANativeWindow*`.
     * @param flags One or more configuration flags as defined in `SwapChain`.
     *
     * @return A pointer to the newly created SwapChain.
     *
     * @see Renderer.beginFrame()
     */
    /**
     * 从给定的操作系统的原生窗口句柄创建 SwapChain
     *
     * @param nativeWindow 不透明的原生窗口句柄。例如：在 Android 上这是
     *                     `ANativeWindow*`。
     * @param flags 在 `SwapChain` 中定义的一个或多个配置标志
     *
     * @return 指向新创建的 SwapChain 的指针
     *
     * @see Renderer.beginFrame()
     */
    SwapChain* UTILS_NONNULL createSwapChain(void* UTILS_NULLABLE nativeWindow, uint64_t flags = 0) noexcept;


    /**
     * Creates a headless SwapChain.
     *
      * @param width    Width of the drawing buffer in pixels.
      * @param height   Height of the drawing buffer in pixels.
     * @param flags     One or more configuration flags as defined in `SwapChain`.
     *
     * @return A pointer to the newly created SwapChain.
     *
     * @see Renderer.beginFrame()
     */
    /**
     * 创建无头 SwapChain
     *
     * @param width    绘制缓冲区的宽度（像素）
     * @param height   绘制缓冲区的高度（像素）
     * @param flags    在 `SwapChain` 中定义的一个或多个配置标志
     *
     * @return 指向新创建的 SwapChain 的指针
     *
     * @see Renderer.beginFrame()
     */
    SwapChain* UTILS_NONNULL createSwapChain(uint32_t width, uint32_t height, uint64_t flags = 0) noexcept;

    /**
     * Creates a renderer associated to this engine.
     *
     * A Renderer is intended to map to a *window* on screen.
     *
     * @return A pointer to the newly created Renderer.
     */
    /**
     * 创建与此引擎关联的渲染器
     *
     * Renderer 旨在映射到屏幕上的*窗口*。
     *
     * @return 指向新创建的 Renderer 的指针
     */
    Renderer* UTILS_NONNULL createRenderer() noexcept;

    /**
     * Creates a View.
     *
     * @return A pointer to the newly created View.
     */
    /**
     * 创建 View
     *
     * @return 指向新创建的 View 的指针
     */
    View* UTILS_NONNULL createView() noexcept;

    /**
     * Creates a Scene.
     *
     * @return A pointer to the newly created Scene.
     */
    /**
     * 创建 Scene
     *
     * @return 指向新创建的 Scene 的指针
     */
    Scene* UTILS_NONNULL createScene() noexcept;

    /**
     * Creates a Camera component.
     *
     * @param entity Entity to add the camera component to.
     * @return A pointer to the newly created Camera.
     */
    /**
     * 创建 Camera 组件
     *
     * @param entity 要添加相机组件的实体
     * @return 指向新创建的 Camera 的指针
     */
    Camera* UTILS_NONNULL createCamera(utils::Entity entity) noexcept;

    /**
     * Returns the Camera component of the given entity.
     *
     * @param entity An entity.
     * @return A pointer to the Camera component for this entity or nullptr if the entity didn't
     *         have a Camera component. The pointer is valid until destroyCameraComponent()
     *         is called or the entity itself is destroyed.
     */
    /**
     * 返回给定实体的 Camera 组件。
     *
     * @param entity 实体
     * @return 指向此实体的 Camera 组件的指针，如果实体没有 Camera 组件则为 nullptr。
     *         指针在调用 destroyCameraComponent() 或实体本身被销毁之前有效。
     */
    Camera* UTILS_NULLABLE getCameraComponent(utils::Entity entity) noexcept;

    /**
     * Destroys the Camera component associated with the given entity.
     *
     * @param entity An entity.
     */
    /**
     * 销毁与给定实体关联的 Camera 组件。
     *
     * @param entity 实体
     */
    void destroyCameraComponent(utils::Entity entity) noexcept;

    /**
     * Creates a Fence.
     *
     * @return A pointer to the newly created Fence.
     */
    /**
     * 创建 Fence。
     *
     * @return 指向新创建的 Fence 的指针
     */
    Fence* UTILS_NONNULL createFence() noexcept;

    /**
     * Creates a Sync.
     * @param callback A callback that will be invoked when the handle for
     *                 the created sync is set
     *
     * @return A pointer to the newly created Sync.
     */
    /**
     * 创建 Sync。
     *
     * @return 指向新创建的 Sync 的指针
     */
    Sync* UTILS_NONNULL createSync() noexcept;

    //!< Destroys a BufferObject object.
    /**
     * 销毁 BufferObject 对象
     */
    bool destroy(const BufferObject* UTILS_NULLABLE p);
    //!< Destroys an VertexBuffer object.
    /**
     * 销毁 VertexBuffer 对象
     */
    bool destroy(const VertexBuffer* UTILS_NULLABLE p);
    //!< Destroys a Fence object.
    /**
     * 销毁 Fence 对象
     */
    bool destroy(const Fence* UTILS_NULLABLE p);
    //!< Destroys a Sync object.
    /**
     * 销毁 Sync 对象
     */
    bool destroy(const Sync* UTILS_NULLABLE p);
    //!< Destroys an IndexBuffer object.
    /**
     * 销毁 IndexBuffer 对象
     */
    bool destroy(const IndexBuffer* UTILS_NULLABLE p);
    //!< Destroys a SkinningBuffer object.
    /**
     * 销毁 SkinningBuffer 对象
     */
    bool destroy(const SkinningBuffer* UTILS_NULLABLE p);
    //!< Destroys a MorphTargetBuffer object.
    /**
     * 销毁 MorphTargetBuffer 对象
     */
    bool destroy(const MorphTargetBuffer* UTILS_NULLABLE p);
    //!< Destroys an IndirectLight object.
    /**
     * 销毁 IndirectLight 对象
     */
    bool destroy(const IndirectLight* UTILS_NULLABLE p);

    /**
     * Destroys a Material object
     * @param p the material object to destroy
     * @attention All MaterialInstance of the specified material must be destroyed before
     *            destroying it.
     * @exception utils::PreConditionPanic is thrown if some MaterialInstances remain.
     * no-op if exceptions are disabled and some MaterialInstances remain.
     */
    /**
     * 销毁 Material 对象
     * @param p 要销毁的材质对象
     * @attention 在销毁指定材质之前，必须销毁该材质的所有 MaterialInstance。
     * @exception 如果仍有 MaterialInstance，则抛出 utils::PreConditionPanic。
     * 如果禁用了异常且仍有 MaterialInstance，则为 no-op。
     */
    bool destroy(const Material* UTILS_NULLABLE p);
    bool destroy(const MaterialInstance* UTILS_NULLABLE p); //!< Destroys a MaterialInstance object.
    /**
     * 销毁 MaterialInstance 对象
     */
    bool destroy(const Renderer* UTILS_NULLABLE p);         //!< Destroys a Renderer object.
    /**
     * 销毁 Renderer 对象
     */
    bool destroy(const Scene* UTILS_NULLABLE p);            //!< Destroys a Scene object.
    /**
     * 销毁 Scene 对象
     */
    bool destroy(const Skybox* UTILS_NULLABLE p);           //!< Destroys a SkyBox object.
    /**
     * 销毁 SkyBox 对象
     */
    bool destroy(const ColorGrading* UTILS_NULLABLE p);     //!< Destroys a ColorGrading object.
    /**
     * 销毁 ColorGrading 对象
     */
    bool destroy(const SwapChain* UTILS_NULLABLE p);        //!< Destroys a SwapChain object.
    /**
     * 销毁 SwapChain 对象
     */
    bool destroy(const Stream* UTILS_NULLABLE p);           //!< Destroys a Stream object.
    /**
     * 销毁 Stream 对象
     */
    bool destroy(const Texture* UTILS_NULLABLE p);          //!< Destroys a Texture object.
    /**
     * 销毁 Texture 对象
     */
    bool destroy(const RenderTarget* UTILS_NULLABLE p);     //!< Destroys a RenderTarget object.
    /**
     * 销毁 RenderTarget 对象
     */
    bool destroy(const View* UTILS_NULLABLE p);             //!< Destroys a View object.
    /**
     * 销毁 View 对象
     */
    bool destroy(const InstanceBuffer* UTILS_NULLABLE p);   //!< Destroys an InstanceBuffer object.
    /**
     * 销毁 InstanceBuffer 对象
     */
    void destroy(utils::Entity e);    //!< Destroys all filament-known components from this entity
    /**
     * 销毁此实体的所有 Filament 已知组件
     */

    /** Tells whether a BufferObject object is valid */
    /**
     * 判断 BufferObject 对象是否有效
     */
    bool isValid(const BufferObject* UTILS_NULLABLE p) const;
    /** Tells whether an VertexBuffer object is valid */
    /**
     * 判断 VertexBuffer 对象是否有效
     */
    bool isValid(const VertexBuffer* UTILS_NULLABLE p) const;
    /** Tells whether a Fence object is valid */
    /**
     * 判断 Fence 对象是否有效
     */
    bool isValid(const Fence* UTILS_NULLABLE p) const;
    /** Tells whether a Sync object is valid */
    /**
     * 判断 Sync 对象是否有效
     */
    bool isValid(const Sync* UTILS_NULLABLE p) const;
    /** Tells whether an IndexBuffer object is valid */
    /**
     * 判断 IndexBuffer 对象是否有效
     */
    bool isValid(const IndexBuffer* UTILS_NULLABLE p) const;
    /** Tells whether a SkinningBuffer object is valid */
    /**
     * 判断 SkinningBuffer 对象是否有效
     */
    bool isValid(const SkinningBuffer* UTILS_NULLABLE p) const;
    /** Tells whether a MorphTargetBuffer object is valid */
    /**
     * 判断 MorphTargetBuffer 对象是否有效
     */
    bool isValid(const MorphTargetBuffer* UTILS_NULLABLE p) const;
    /** Tells whether an IndirectLight object is valid */
    /**
     * 判断 IndirectLight 对象是否有效
     */
    bool isValid(const IndirectLight* UTILS_NULLABLE p) const;
    /** Tells whether an Material object is valid */
    /**
     * 判断 Material 对象是否有效
     */
    bool isValid(const Material* UTILS_NULLABLE p) const;
    /** Tells whether an MaterialInstance object is valid. Use this if you already know
     * which Material this MaterialInstance belongs to. DO NOT USE getMaterial(), this would
     * defeat the purpose of validating the MaterialInstance.
     */
    /**
     * 判断 MaterialInstance 对象是否有效。如果已知此 MaterialInstance 属于哪个 Material，请使用此方法。
     * 不要使用 getMaterial()，这会破坏验证 MaterialInstance 的目的。
     */
    bool isValid(const Material* UTILS_NONNULL m, const MaterialInstance* UTILS_NULLABLE p) const;
    /** Tells whether an MaterialInstance object is valid. Use this if the Material the
     * MaterialInstance belongs to is not known. This method can be expensive.
     */
    /**
     * 判断 MaterialInstance 对象是否有效。如果不知道 MaterialInstance 属于哪个 Material，请使用此方法。
     * 此方法可能比较昂贵。
     */
    bool isValidExpensive(const MaterialInstance* UTILS_NULLABLE p) const;
    /** Tells whether a Renderer object is valid */
    /**
     * 判断 Renderer 对象是否有效
     */
    bool isValid(const Renderer* UTILS_NULLABLE p) const;
    /** Tells whether a Scene object is valid */
    /**
     * 判断 Scene 对象是否有效
     */
    bool isValid(const Scene* UTILS_NULLABLE p) const;
    /** Tells whether a SkyBox object is valid */
    /**
     * 判断 SkyBox 对象是否有效
     */
    bool isValid(const Skybox* UTILS_NULLABLE p) const;
    /** Tells whether a ColorGrading object is valid */
    /**
     * 判断 ColorGrading 对象是否有效
     */
    bool isValid(const ColorGrading* UTILS_NULLABLE p) const;
    /** Tells whether a SwapChain object is valid */
    /**
     * 判断 SwapChain 对象是否有效
     */
    bool isValid(const SwapChain* UTILS_NULLABLE p) const;
    /** Tells whether a Stream object is valid */
    /**
     * 判断 Stream 对象是否有效
     */
    bool isValid(const Stream* UTILS_NULLABLE p) const;
    /** Tells whether a Texture object is valid */
    /**
     * 判断 Texture 对象是否有效
     */
    bool isValid(const Texture* UTILS_NULLABLE p) const;
    /** Tells whether a RenderTarget object is valid */
    /**
     * 判断 RenderTarget 对象是否有效
     */
    bool isValid(const RenderTarget* UTILS_NULLABLE p) const;
    /** Tells whether a View object is valid */
    /**
     * 判断 View 对象是否有效
     */
    bool isValid(const View* UTILS_NULLABLE p) const;
    /** Tells whether an InstanceBuffer object is valid */
    /**
     * 判断 InstanceBuffer 对象是否有效
     */
    bool isValid(const InstanceBuffer* UTILS_NULLABLE p) const;

    /**
     * Retrieve the count of each resource tracked by Engine.
     * This is intended for debugging.
     * @{
     */
    /**
     * 检索 Engine 跟踪的每种资源的计数。
     * 此方法用于调试。
     * @{
     */
    size_t getBufferObjectCount() const noexcept;        //!< 获取 BufferObject 数量
    size_t getViewCount() const noexcept;                //!< 获取 View 数量
    size_t getSceneCount() const noexcept;               //!< 获取 Scene 数量
    size_t getSwapChainCount() const noexcept;           //!< 获取 SwapChain 数量
    size_t getStreamCount() const noexcept;              //!< 获取 Stream 数量
    size_t getIndexBufferCount() const noexcept;         //!< 获取 IndexBuffer 数量
    size_t getSkinningBufferCount() const noexcept;      //!< 获取 SkinningBuffer 数量
    size_t getMorphTargetBufferCount() const noexcept;   //!< 获取 MorphTargetBuffer 数量
    size_t getInstanceBufferCount() const noexcept;      //!< 获取 InstanceBuffer 数量
    size_t getVertexBufferCount() const noexcept;        //!< 获取 VertexBuffer 数量
    size_t getIndirectLightCount() const noexcept;       //!< 获取 IndirectLight 数量
    size_t getMaterialCount() const noexcept;            //!< 获取 Material 数量
    size_t getTextureCount() const noexcept;             //!< 获取 Texture 数量
    size_t getSkyboxeCount() const noexcept;             //!< 获取 Skybox 数量
    size_t getColorGradingCount() const noexcept;        //!< 获取 ColorGrading 数量
    size_t getRenderTargetCount() const noexcept;        //!< 获取 RenderTarget 数量
    /**  @} */

    /**
     * Kicks the hardware thread (e.g. the OpenGL, Vulkan or Metal thread) and blocks until
     * all commands to this point are executed. Note that does guarantee that the
     * hardware is actually finished.
     *
     * <p>This is typically used right after destroying the <code>SwapChain</code>,
     * in cases where a guarantee about the <code>SwapChain</code> destruction is needed in a
     * timely fashion, such as when responding to Android's
     * <code>android.view.SurfaceHolder.Callback.surfaceDestroyed</code></p>
     */
    /**
     * 触发硬件线程（例如 OpenGL、Vulkan 或 Metal 线程）并阻塞直到
     * 执行到此点的所有命令。注意，这并不保证硬件实际上已完成。
     *
     * <p>这通常在销毁 <code>SwapChain</code> 之后立即使用，
     * 在需要及时保证 <code>SwapChain</code> 销毁的情况下，例如响应 Android 的
     * <code>android.view.SurfaceHolder.Callback.surfaceDestroyed</code> 时</p>
     */
    void flushAndWait();

    /**
     * Kicks the hardware thread (e.g. the OpenGL, Vulkan or Metal thread) and blocks until
     * all commands to this point are executed. Note that does guarantee that the
     * hardware is actually finished.
     *
     * A timeout can be specified, if for some reason this flushAndWait doesn't complete before the timeout, it will
     * return false, true otherwise.
     *
     * <p>This is typically used right after destroying the <code>SwapChain</code>,
     * in cases where a guarantee about the <code>SwapChain</code> destruction is needed in a
     * timely fashion, such as when responding to Android's
     * <code>android.view.SurfaceHolder.Callback.surfaceDestroyed</code></p>
     *
     * @param timeout A timeout in nanoseconds
     * @return true if successful, false if flushAndWait timed out, in which case it wasn't successful and commands
     * might still be executing on both the CPU and GPU sides.
     */
    /**
     * 触发硬件线程（例如 OpenGL、Vulkan 或 Metal 线程）并阻塞直到
     * 执行到此点的所有命令。注意，这并不保证硬件实际上已完成。
     *
     * 可以指定超时，如果由于某种原因此 flushAndWait 在超时之前未完成，它将
     * 返回 false，否则返回 true。
     *
     * <p>这通常在销毁 <code>SwapChain</code> 之后立即使用，
     * 在需要及时保证 <code>SwapChain</code> 销毁的情况下，例如响应 Android 的
     * <code>android.view.SurfaceHolder.Callback.surfaceDestroyed</code> 时</p>
     *
     * @param timeout 超时时间（纳秒）
     * @return 如果成功则返回 true，如果 flushAndWait 超时则返回 false，在这种情况下它不成功，命令
     * 可能仍在 CPU 和 GPU 两侧执行。
     */
    bool flushAndWait(uint64_t timeout);

    /**
     * Kicks the hardware thread (e.g. the OpenGL, Vulkan or Metal thread) but does not wait
     * for commands to be either executed or the hardware finished.
     *
     * <p>This is typically used after creating a lot of objects to start draining the command
     * queue which has a limited size.</p>
      */
    /**
     * 触发硬件线程（例如 OpenGL、Vulkan 或 Metal 线程），但不等待
     * 命令执行或硬件完成。
     *
     * <p>这通常在创建大量对象后使用，以开始清空命令
     * 队列，该队列的大小有限。</p>
     */
    void flush();

    /**
     * Get paused state of rendering thread.
     *
     * <p>Warning: This is an experimental API.
     *
     * @see setPaused
     */
    /**
     * 获取渲染线程的暂停状态。
     *
     * <p>警告：这是一个实验性 API。</p>
     *
     * @see setPaused
     */
    bool isPaused() const noexcept(UTILS_HAS_THREADING);

    /**
     * Pause or resume rendering thread.
     *
     * <p>Warning: This is an experimental API. In particular, note the following caveats.
     *
     * <ul><li>
     * Buffer callbacks will never be called as long as the rendering thread is paused.
     * Do not rely on a buffer callback to unpause the thread.
     * </li><li>
     * While the rendering thread is paused, rendering commands will continue to be queued until the
     * buffer limit is reached. When the limit is reached, the program will abort.
     * </li></ul>
     */
    /**
     * 暂停或恢复渲染线程。
     *
     * <p>警告：这是一个实验性 API。特别要注意以下注意事项。</p>
     *
     * <ul><li>
     * 只要渲染线程暂停，缓冲区回调将永远不会被调用。
     * 不要依赖缓冲区回调来取消暂停线程。
     * </li><li>
     * 当渲染线程暂停时，渲染命令将继续排队，直到
     * 达到缓冲区限制。达到限制时，程序将中止。
     * </li></ul>
     *
     * @param paused true 表示暂停，false 表示恢复
     */
    void setPaused(bool paused);

    /**
     * Drains the user callback message queue and immediately execute all pending callbacks.
     *
     * <p> Typically this should be called once per frame right after the application's vsync tick,
     * and typically just before computing parameters (e.g. object positions) for the next frame.
     * This is useful because otherwise callbacks will be executed by filament at a later time,
     * which may increase latency in certain applications.</p>
     */
    /**
     * 清空用户回调消息队列并立即执行所有挂起的回调。
     *
     * <p> 通常这应该在应用程序的垂直同步节拍之后每帧调用一次，
     * 通常就在为下一帧计算参数（例如对象位置）之前。
     * 这很有用，因为否则回调将在稍后由 Filament 执行，
     * 这可能会增加某些应用程序的延迟。</p>
     */
    void pumpMessageQueues();

    /**
     * Switch the command queue to unprotected mode. Protected mode can be activated via
     * Renderer::beginFrame() using a protected SwapChain.
     * @see Renderer
     * @see SwapChain
     */
    /**
     * 将命令队列切换到非保护模式。可以通过
     * 使用受保护的 SwapChain 调用 Renderer::beginFrame() 来激活保护模式。
     * @see Renderer
     * @see SwapChain
     */
    void unprotected() noexcept;

    /**
     * Returns the default Material.
     *
     * The default material is 80% white and uses the Material.Shading.LIT shading.
     *
     * @return A pointer to the default Material instance (a singleton).
     */
    /**
     * 返回默认 Material。
     *
     * 默认材质为 80% 白色，使用 Material.Shading.LIT 着色。
     *
     * @return 指向默认 Material 实例（单例）的指针
     */
    Material const* UTILS_NONNULL getDefaultMaterial() const noexcept;

    /**
     * Returns the resolved backend.
     */
    /**
     * 返回已解析的后端。
     */
    Backend getBackend() const noexcept;

    /**
     * Returns the Platform object that belongs to this Engine.
     *
     * When Engine::create is called with no platform argument, Filament creates an appropriate
     * Platform subclass automatically. The specific subclass created depends on the backend and
     * OS. For example, when the OpenGL backend is used, the Platform object will be a descendant of
     * OpenGLPlatform.
     *
     * dynamic_cast should be used to cast the returned Platform object into a specific subclass.
     * Note that RTTI must be available to use dynamic_cast.
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * Platform* platform = engine->getPlatform();
     * // static_cast also works, but more dangerous.
     * SpecificPlatform* specificPlatform = dynamic_cast<SpecificPlatform*>(platform);
     * specificPlatform->platformSpecificMethod();
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * When a custom Platform is passed to Engine::create, Filament will use it instead, and this
     * method will return it.
     *
     * @return A pointer to the Platform object that was provided to Engine::create, or the
     * Filament-created one.
     */
    /**
     * 返回属于此 Engine 的 Platform 对象。
     *
     * 当调用 Engine::create 时没有 platform 参数时，Filament 会自动创建适当的
     * Platform 子类。创建的特定子类取决于后端和
     * 操作系统。例如，当使用 OpenGL 后端时，Platform 对象将是
     * OpenGLPlatform 的派生类。
     *
     * 应使用 dynamic_cast 将返回的 Platform 对象转换为特定的子类。
     * 注意，必须提供 RTTI 才能使用 dynamic_cast。
     *
     * 当向 Engine::create 传递自定义 Platform 时，Filament 将使用它，此
     * 方法将返回它。
     *
     * @return 指向提供给 Engine::create 的 Platform 对象的指针，或
     * Filament 创建的对象
     */
    Platform* UTILS_NULLABLE getPlatform() const noexcept;

    /**
     * Allocate a small amount of memory directly in the command stream. The allocated memory is
     * guaranteed to be preserved until the current command buffer is executed
     *
     * @param size       size to allocate in bytes. This should be small (e.g. < 1 KB)
     * @param alignment  alignment requested
     * @return           a pointer to the allocated buffer or nullptr if no memory was available.
     *
     * @note there is no need to destroy this buffer, it will be freed automatically when
     *       the current command buffer is executed.
     */
    /**
     * 直接在命令流中分配少量内存。分配的内存保证
     * 在当前命令缓冲区执行之前保持不变
     *
     * @param size       要分配的字节大小。这应该很小（例如 < 1 KB）
     * @param alignment  请求的对齐方式
     * @return           指向分配缓冲区的指针，如果没有可用内存则为 nullptr
     *
     * @note 不需要销毁此缓冲区，它将在
     *       当前命令缓冲区执行时自动释放。
     */
    void* UTILS_NULLABLE streamAlloc(size_t size, size_t alignment = alignof(double)) noexcept;

    /**
      * Invokes one iteration of the render loop, used only on single-threaded platforms.
      *
      * This should be called every time the windowing system needs to paint (e.g. at 60 Hz).
      */
    /**
     * 调用渲染循环的一次迭代，仅在单线程平台上使用。
     *
     * 每当窗口系统需要绘制时（例如 60 Hz）都应调用此方法。
     */
    void execute();

    /**
      * Retrieves the job system that the Engine has ownership over.
      *
      * @return JobSystem used by filament
      */
    /**
     * 检索 Engine 拥有的作业系统。
     *
     * @return Filament 使用的 JobSystem
     */
    utils::JobSystem& getJobSystem() noexcept;

#if defined(__EMSCRIPTEN__)
    /**
      * WebGL only: Tells the driver to reset any internal state tracking if necessary.
      *
      * This is only useful when integrating an external renderer into Filament on platforms
      * like WebGL, where share contexts do not exist. Filament keeps track of the GL
      * state it has set (like which texture is bound), and does not re-set that state if
      * it does not think it needs to. However, if an external renderer has set different
      * state in the mean time, Filament will use that new state unknowingly.
      *
      * If you are in this situation, call this function - ideally only once per frame,
      * immediately after calling Engine::execute().
      */
    /**
     * 仅 WebGL：告诉驱动程序在必要时重置任何内部状态跟踪。
     *
     * 这仅在 WebGL 等平台上将外部渲染器集成到 Filament 时有用，这些平台上不存在共享上下文。
     * Filament 跟踪它设置的 GL 状态（例如绑定了哪个纹理），如果它认为不需要，则不会重新设置该状态。
     * 但是，如果外部渲染器在此期间设置了不同的状态，Filament 会在不知情的情况下使用该新状态。
     *
     * 如果遇到这种情况，请调用此函数 - 理想情况下每帧只调用一次，
     * 在调用 Engine::execute() 之后立即调用。
     */
    void resetBackendState() noexcept;
#endif

    /**
     * Get the current time. This is a convenience function that simply returns the
     * time in nanosecond since epoch of std::chrono::steady_clock.
     * A possible implementation is:
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *     return std::chrono::steady_clock::now().time_since_epoch().count();
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * @return current time in nanosecond since epoch of std::chrono::steady_clock.
     * @see Renderer::beginFrame()
     */
    /**
     * 获取当前时间。这是一个便利函数，简单地返回
     * 自 std::chrono::steady_clock 纪元以来的纳秒时间。
     *
     * @return 自 std::chrono::steady_clock 纪元以来的当前时间（纳秒）
     * @see Renderer::beginFrame()
     */
    static uint64_t getSteadyClockTimeNano() noexcept;


    /**
     * 返回此 Engine 的 DebugRegistry 引用
     */
    DebugRegistry& getDebugRegistry() noexcept;

    /**
     * Check if a feature flag exists
     * @param name name of the feature flag to check
     * @return true if the feature flag exists, false otherwise
     */
    /**
     * 检查功能标志是否存在
     * @param name 要检查的功能标志名称
     * @return 如果功能标志存在则返回 true，否则返回 false
     */
    inline bool hasFeatureFlag(char const* UTILS_NONNULL name) noexcept {
        return getFeatureFlag(name).has_value();
    }

    /**
     * Set the value of a non-constant feature flag.
     * @param name name of the feature flag to set
     * @param value value to set
     * @return true if the value was set, false if the feature flag is constant or doesn't exist.
     */
    /**
     * 设置非常量功能标志的值。
     * @param name 要设置的功能标志名称
     * @param value 要设置的值
     * @return 如果值已设置则返回 true，如果功能标志是常量或不存在则返回 false
     */
    bool setFeatureFlag(char const* UTILS_NONNULL name, bool value) noexcept;

    /**
     * Retrieves the value of any feature flag.
     * @param name name of the feature flag
     * @return the value of the flag if it exists
     */
    /**
     * 检索任何功能标志的值。
     * @param name 功能标志名称
     * @return 如果标志存在则返回其值
     */
    std::optional<bool> getFeatureFlag(char const* UTILS_NONNULL name) const noexcept;

    /**
     * Returns a pointer to a non-constant feature flag value.
     * @param name name of the feature flag
     * @return a pointer to the feature flag value, or nullptr if the feature flag is constant or doesn't exist
     */
    /**
     * 返回指向非常量功能标志值的指针。
     * @param name 功能标志名称
     * @return 指向功能标志值的指针，如果功能标志是常量或不存在则返回 nullptr
     */
    bool* UTILS_NULLABLE getFeatureFlagPtr(char const* UTILS_NONNULL name) const noexcept;

protected:
    //! \privatesection
    Engine() noexcept = default;
    ~Engine() = default;

public:
    //! \privatesection
    Engine(Engine const&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine const&) = delete;
    Engine& operator=(Engine&&) = delete;
};

} // namespace filament

#endif // TNT_FILAMENT_ENGINE_H
