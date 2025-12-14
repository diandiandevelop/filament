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

#ifndef TNT_FILAMENT_SWAPCHAIN_H
#define TNT_FILAMENT_SWAPCHAIN_H

#include <filament/FilamentAPI.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/Invocable.h>

#include <stdint.h>

namespace filament {

class Engine;

/**
 * A swap chain represents an Operating System's *native* renderable surface.
 *
 * Typically, it's a native window or a view. Because a SwapChain is initialized from a
 * native object, it is given to filament as a `void *`, which must be of the proper type
 * for each platform filament is running on.
 *
 * \code
 * SwapChain* swapChain = engine->createSwapChain(nativeWindow);
 * \endcode
 *
 * When Engine::create() is used without specifying a Platform, the `nativeWindow`
 * parameter above must be of type:
 *
 * Platform        | nativeWindow type
 * :---------------|:----------------------------:
 * Android         | ANativeWindow*
 * macOS - OpenGL  | NSView*
 * macOS - Metal   | CAMetalLayer*
 * iOS - OpenGL    | CAEAGLLayer*
 * iOS - Metal     | CAMetalLayer*
 * X11             | Window
 * Windows         | HWND
 *
 * Otherwise, the `nativeWindow` is defined by the concrete implementation of Platform.
 *
 *
 * Examples:
 *
 * Android
 * -------
 *
 * On Android, an `ANativeWindow*` can be obtained from a Java `Surface` object using:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  #include <android/native_window_jni.h>
 *  // parameters
 *  // env:         JNIEnv*
 *  // surface:     jobject
 *  ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * \warning
 * Don't use reflection to access the `mNativeObject` field, it won't work.
 *
 * A `Surface` can be retrieved from a `SurfaceView` or `SurfaceHolder` easily using
 * `SurfaceHolder.getSurface()` and/or `SurfaceView.getHolder()`.
 *
 * \note
 * To use a `TextureView` as a SwapChain, it is necessary to first get its `SurfaceTexture`,
 * for instance using `TextureView.SurfaceTextureListener` and then create a `Surface`:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.java}
 *  // using a TextureView.SurfaceTextureListener:
 *  public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
 *      mSurface = new Surface(surfaceTexture);
 *      // mSurface can now be used in JNI to create an ANativeWindow.
 *  }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Linux
 * -----
 *
 * Example using SDL:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * SDL_SysWMinfo wmi;
 * SDL_VERSION(&wmi.version);
 * SDL_GetWindowWMInfo(sdlWindow, &wmi);
 * Window nativeWindow = (Window) wmi.info.x11.window;
 *
 * using namespace filament;
 * Engine* engine       = Engine::create();
 * SwapChain* swapChain = engine->createSwapChain((void*) nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Windows
 * -------
 *
 * Example using SDL:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * SDL_SysWMinfo wmi;
 * SDL_VERSION(&wmi.version);
 * FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi)) << "SDL version unsupported!";
 * HDC nativeWindow = (HDC) wmi.info.win.hdc;
 *
 * using namespace filament;
 * Engine* engine       = Engine::create();
 * SwapChain* swapChain = engine->createSwapChain((void*) nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * OSX
 * ---
 *
 * On OSX, any `NSView` can be used *directly* as a `nativeWindow` with createSwapChain().
 *
 * Example using SDL/Objective-C:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.mm}
 *  #include <filament/Engine.h>
 *
 *  #include <Cocoa/Cocoa.h>
 *  #include <SDL_syswm.h>
 *
 *  SDL_SysWMinfo wmi;
 *  SDL_VERSION(&wmi.version);
 *  NSWindow* win = (NSWindow*) wmi.info.cocoa.window;
 *  NSView* view = [win contentView];
 *  void* nativeWindow = view;
 *
 *  using namespace filament;
 *  Engine* engine       = Engine::create();
 *  SwapChain* swapChain = engine->createSwapChain(nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @see Engine
 */
/**
 * SwapChain 表示操作系统的*原生*可渲染表面
 *
 * 通常，它是原生窗口或视图。因为 SwapChain 是从原生对象初始化的，
 * 所以它以 `void *` 的形式提供给 Filament，对于 Filament 运行的每个平台，
 * 它必须是正确的类型。
 *
 * \code
 * SwapChain* swapChain = engine->createSwapChain(nativeWindow);
 * \endcode
 *
 * 当 Engine::create() 在不指定 Platform 的情况下使用时，上述 `nativeWindow`
 * 参数必须是以下类型：
 *
 * 平台        | nativeWindow 类型
 * :---------------|:----------------------------:
 * Android         | ANativeWindow*
 * macOS - OpenGL  | NSView*
 * macOS - Metal   | CAMetalLayer*
 * iOS - OpenGL    | CAEAGLLayer*
 * iOS - Metal     | CAMetalLayer*
 * X11             | Window
 * Windows         | HWND
 *
 * 否则，`nativeWindow` 由 Platform 的具体实现定义。
 *
 *
 * 示例：
 *
 * Android
 * -------
 *
 * 在 Android 上，可以使用以下方法从 Java `Surface` 对象获取 `ANativeWindow*`：
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  #include <android/native_window_jni.h>
 *  // 参数
 *  // env:         JNIEnv*
 *  // surface:     jobject
 *  ANativeWindow* win = ANativeWindow_fromSurface(env, surface);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * \warning
 * 不要使用反射访问 `mNativeObject` 字段，这不会起作用。
 *
 * 可以轻松使用 `SurfaceHolder.getSurface()` 和/或 `SurfaceView.getHolder()` 从
 * `SurfaceView` 或 `SurfaceHolder` 获取 `Surface`。
 *
 * \note
 * 要将 `TextureView` 用作 SwapChain，首先需要获取其 `SurfaceTexture`，
 * 例如使用 `TextureView.SurfaceTextureListener`，然后创建 `Surface`：
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.java}
 *  // 使用 TextureView.SurfaceTextureListener:
 *  public void onSurfaceTextureAvailable(SurfaceTexture surfaceTexture, int width, int height) {
 *      mSurface = new Surface(surfaceTexture);
 *      // mSurface 现在可以在 JNI 中用于创建 ANativeWindow。
 *  }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Linux
 * -----
 *
 * 使用 SDL 的示例：
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * SDL_SysWMinfo wmi;
 * SDL_VERSION(&wmi.version);
 * SDL_GetWindowWMInfo(sdlWindow, &wmi);
 * Window nativeWindow = (Window) wmi.info.x11.window;
 *
 * using namespace filament;
 * Engine* engine       = Engine::create();
 * SwapChain* swapChain = engine->createSwapChain((void*) nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Windows
 * -------
 *
 * 使用 SDL 的示例：
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * SDL_SysWMinfo wmi;
 * SDL_VERSION(&wmi.version);
 * FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi)) << "SDL version unsupported!";
 * HDC nativeWindow = (HDC) wmi.info.win.hdc;
 *
 * using namespace filament;
 * Engine* engine       = Engine::create();
 * SwapChain* swapChain = engine->createSwapChain((void*) nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * OSX
 * ---
 *
 * 在 OSX 上，任何 `NSView` 都可以*直接*用作 createSwapChain() 的 `nativeWindow`。
 *
 * 使用 SDL/Objective-C 的示例：
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.mm}
 *  #include <filament/Engine.h>
 *
 *  #include <Cocoa/Cocoa.h>
 *  #include <SDL_syswm.h>
 *
 *  SDL_SysWMinfo wmi;
 *  SDL_VERSION(&wmi.version);
 *  NSWindow* win = (NSWindow*) wmi.info.cocoa.window;
 *  NSView* view = [win contentView];
 *  void* nativeWindow = view;
 *
 *  using namespace filament;
 *  Engine* engine       = Engine::create();
 *  SwapChain* swapChain = engine->createSwapChain(nativeWindow);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @see Engine
 */
class UTILS_PUBLIC SwapChain : public FilamentAPI {
public:
    using FrameScheduledCallback = backend::FrameScheduledCallback;
    /**
     * 帧调度回调类型
     */
    using FrameCompletedCallback = utils::Invocable<void(SwapChain* UTILS_NONNULL)>;
    /**
     * 帧完成回调类型
     */

    /**
     * Requests a SwapChain with an alpha channel.
     */
    /**
     * 请求带 alpha 通道的 SwapChain
     */
    static constexpr uint64_t CONFIG_TRANSPARENT = backend::SWAP_CHAIN_CONFIG_TRANSPARENT;

    /**
     * This flag indicates that the swap chain may be used as a source surface
     * for reading back render results.  This config must be set when creating
     * any swap chain that will be used as the source for a blit operation.
     *
     * @see
     * Renderer.copyFrame()
     */
    /**
     * 此标志表示交换链可用作读取渲染结果的源表面。
     * 创建将用作 blit 操作源的任何交换链时，必须设置此配置。
     *
     * @see Renderer.copyFrame()
     */
    static constexpr uint64_t CONFIG_READABLE = backend::SWAP_CHAIN_CONFIG_READABLE;

    /**
     * Indicates that the native X11 window is an XCB window rather than an XLIB window.
     * This is ignored on non-Linux platforms and in builds that support only one X11 API.
     */
    /**
     * 表示原生 X11 窗口是 XCB 窗口而不是 XLIB 窗口。
     * 在非 Linux 平台和仅支持一个 X11 API 的构建中，此标志会被忽略。
     */
    static constexpr uint64_t CONFIG_ENABLE_XCB = backend::SWAP_CHAIN_CONFIG_ENABLE_XCB;

    /**
     * Indicates that the native window is a CVPixelBufferRef.
     *
     * This is only supported by the Metal backend. The CVPixelBuffer must be in the
     * kCVPixelFormatType_32BGRA format.
     *
     * It is not necessary to add an additional retain call before passing the pixel buffer to
     * Filament. Filament will call CVPixelBufferRetain during Engine::createSwapChain, and
     * CVPixelBufferRelease when the swap chain is destroyed.
     */
    /**
     * 表示原生窗口是 CVPixelBufferRef
     *
     * 这仅由 Metal 后端支持。CVPixelBuffer 必须为
     * kCVPixelFormatType_32BGRA 格式。
     *
     * 在将像素缓冲区传递给 Filament 之前，不需要添加额外的 retain 调用。
     * Filament 将在 Engine::createSwapChain 期间调用 CVPixelBufferRetain，并在
     * 交换链被销毁时调用 CVPixelBufferRelease。
     */
    static constexpr uint64_t CONFIG_APPLE_CVPIXELBUFFER =
            backend::SWAP_CHAIN_CONFIG_APPLE_CVPIXELBUFFER;

    /**
     * Indicates that the SwapChain must automatically perform linear to sRGB encoding.
     *
     * This flag is ignored if isSRGBSwapChainSupported() is false.
     *
     * When using this flag, the output colorspace in ColorGrading should be set to
     * Rec709-Linear-D65, or post-processing should be disabled.
     *
     * @see isSRGBSwapChainSupported()
     * @see ColorGrading.outputColorSpace()
     * @see View.setPostProcessingEnabled()
     */
    /**
     * 表示 SwapChain 必须自动执行线性到 sRGB 编码
     *
     * 如果 isSRGBSwapChainSupported() 为 false，则忽略此标志。
     *
     * 使用此标志时，ColorGrading 中的输出色彩空间应设置为
     * Rec709-Linear-D65，或者应禁用后处理。
     *
     * @see isSRGBSwapChainSupported()
     * @see ColorGrading.outputColorSpace()
     * @see View.setPostProcessingEnabled()
     */
    static constexpr uint64_t CONFIG_SRGB_COLORSPACE = backend::SWAP_CHAIN_CONFIG_SRGB_COLORSPACE;

    /**
     * Indicates that this SwapChain should allocate a stencil buffer in addition to a depth buffer.
     *
     * This flag is necessary when using View::setStencilBufferEnabled and rendering directly into
     * the SwapChain (when post-processing is disabled).
     *
     * The specific format of the stencil buffer depends on platform support. The following pixel
     * formats are tried, in order of preference:
     *
     * Depth only (without CONFIG_HAS_STENCIL_BUFFER):
     * - DEPTH32F
     * - DEPTH24
     *
     * Depth + stencil (with CONFIG_HAS_STENCIL_BUFFER):
     * - DEPTH32F_STENCIL8
     * - DEPTH24F_STENCIL8
     *
     * Note that enabling the stencil buffer may hinder depth precision and should only be used if
     * necessary.
     *
     * @see View.setStencilBufferEnabled
     * @see View.setPostProcessingEnabled
     */
    /**
     * 表示此 SwapChain 除了深度缓冲区外还应分配模板缓冲区
     *
     * 当使用 View::setStencilBufferEnabled 并直接渲染到
     * SwapChain 时（当后处理被禁用时），此标志是必需的。
     *
     * 模板缓冲区的具体格式取决于平台支持。按优先顺序尝试以下像素
     * 格式：
     *
     * 仅深度（无 CONFIG_HAS_STENCIL_BUFFER）：
     * - DEPTH32F
     * - DEPTH24
     *
     * 深度 + 模板（使用 CONFIG_HAS_STENCIL_BUFFER）：
     * - DEPTH32F_STENCIL8
     * - DEPTH24F_STENCIL8
     *
     * 注意，启用模板缓冲区可能会影响深度精度，只有在必要时才应使用。
     *
     * @see View.setStencilBufferEnabled
     * @see View.setPostProcessingEnabled
     */
    static constexpr uint64_t CONFIG_HAS_STENCIL_BUFFER = backend::SWAP_CHAIN_CONFIG_HAS_STENCIL_BUFFER;

    /**
     * The SwapChain contains protected content. Only supported when isProtectedContentSupported()
     * is true.
     */
    /**
     * SwapChain 包含受保护内容。仅在 isProtectedContentSupported()
     * 为 true 时支持。
     */
    static constexpr uint64_t CONFIG_PROTECTED_CONTENT = backend::SWAP_CHAIN_CONFIG_PROTECTED_CONTENT;

    /**
     * Indicates that the SwapChain is configured to use Multi-Sample Anti-Aliasing (MSAA) with the
     * given sample points within each pixel. Only supported when isMSAASwapChainSupported(4) is
     * true.
     *
     * This is supported by EGL(Android) and Metal. Other GL platforms (GLX, WGL, etc) don't support
     * it because the swapchain MSAA settings must be configured before window creation.
     *
     * With Metal, this flag should only be used when rendering a single View into a SwapChain. This
     * flag is not supported when rendering multiple Filament Views into this SwapChain.
     *
     * @see isMSAASwapChainSupported(4)
     */
    /**
     * 表示 SwapChain 配置为使用多重采样抗锯齿（MSAA），每个像素内
     * 具有给定的采样点。仅在 isMSAASwapChainSupported(4) 为
     * true 时支持。
     *
     * 这由 EGL(Android) 和 Metal 支持。其他 GL 平台（GLX、WGL 等）不支持
     * 它，因为交换链 MSAA 设置必须在窗口创建之前配置。
     *
     * 对于 Metal，此标志应仅在将单个 View 渲染到 SwapChain 时使用。
     * 将多个 Filament Views 渲染到此 SwapChain 时不支持此标志。
     *
     * @see isMSAASwapChainSupported(4)
     */
    static constexpr uint64_t CONFIG_MSAA_4_SAMPLES = backend::SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES;

    /**
     * Return whether createSwapChain supports the CONFIG_PROTECTED_CONTENT flag.
     * The default implementation returns false.
     *
     * @param engine A pointer to the filament Engine
     * @return true if CONFIG_PROTECTED_CONTENT is supported, false otherwise.
     */
    /**
     * 返回 createSwapChain 是否支持 CONFIG_PROTECTED_CONTENT 标志
     * 默认实现返回 false。
     *
     * @param engine 指向 Filament Engine 的指针
     * @return 如果支持 CONFIG_PROTECTED_CONTENT，返回 true，否则返回 false
     */
    static bool isProtectedContentSupported(Engine& engine) noexcept;

    /**
     * Return whether createSwapChain supports the CONFIG_SRGB_COLORSPACE flag.
     * The default implementation returns false.
     *
     * @param engine A pointer to the filament Engine
     * @return true if CONFIG_SRGB_COLORSPACE is supported, false otherwise.
     */
    /**
     * 返回 createSwapChain 是否支持 CONFIG_SRGB_COLORSPACE 标志
     * 默认实现返回 false。
     *
     * @param engine 指向 Filament Engine 的指针
     * @return 如果支持 CONFIG_SRGB_COLORSPACE，返回 true，否则返回 false
     */
    static bool isSRGBSwapChainSupported(Engine& engine) noexcept;

    /**
     * Return whether createSwapChain supports the CONFIG_MSAA_*_SAMPLES flag.
     * The default implementation returns false.
     *
     * @param engine A pointer to the filament Engine
     * @param samples The number of samples
     * @return true if CONFIG_MSAA_*_SAMPLES is supported, false otherwise.
     */
    /**
     * 返回 createSwapChain 是否支持 CONFIG_MSAA_*_SAMPLES 标志
     * 默认实现返回 false。
     *
     * @param engine 指向 Filament Engine 的指针
     * @param samples 采样数
     * @return 如果支持 CONFIG_MSAA_*_SAMPLES，返回 true，否则返回 false
     */
    static bool isMSAASwapChainSupported(Engine& engine, uint32_t samples) noexcept;

    void* UTILS_NULLABLE getNativeWindow() const noexcept;
    /**
     * 返回原生窗口句柄
     */

    /**
     * If this flag is passed to setFrameScheduledCallback, then the behavior of the default
     * CallbackHandler (when nullptr is passed as the handler argument) is altered to call the
     * callback on the Metal completion handler thread (as opposed to the main Filament thread).
     * This flag also instructs the Metal backend to release the associated CAMetalDrawable on the
     * completion handler thread.
     *
     * This flag has no effect if a custom CallbackHandler is passed or on backends other than Metal.
     *
     * @see setFrameScheduledCallback
     */
    /**
     * 如果将此标志传递给 setFrameScheduledCallback，则默认
     * CallbackHandler 的行为（当 nullptr 作为 handler 参数传递时）会改变为在
     * Metal 完成处理程序线程上调用回调（而不是主 Filament 线程）。
     * 此标志还指示 Metal 后端在
     * 完成处理程序线程上释放关联的 CAMetalDrawable。
     *
     * 如果传递了自定义 CallbackHandler 或在 Metal 以外的后端上，此标志无效。
     *
     * @see setFrameScheduledCallback
     */
    static constexpr uint64_t CALLBACK_DEFAULT_USE_METAL_COMPLETION_HANDLER = 1;

    /**
     * FrameScheduledCallback is a callback function that notifies an application about the status
     * of a frame after Filament has finished its processing.
     *
     * The exact timing and semantics of this callback differ depending on the graphics backend in
     * use.
     *
     * Metal Backend
     * =============
     */
    /**
     * FrameScheduledCallback 是一个回调函数，在 Filament 完成其处理后
     * 通知应用程序帧的状态。
     *
     * 此回调的确切时序和语义因使用的图形后端而异。
     *
     * Metal 后端
     * =============
     */
    /**
     * 使用 Metal 后端时，此回调表示 Filament 已完成帧的所有 CPU 端
     * 处理，帧已准备好进行呈现调度。
     *
     * 通常，Filament 负责将帧的呈现调度到 SwapChain。
     * 但是，如果设置了 SwapChain::FrameScheduledCallback，应用程序承担
     * 通过调用传递给回调函数的 backend::PresentCallable 来调度帧呈现的
     * 责任。在此模式下，Filament 将*不会*
     * 自动调度帧进行呈现。
     *
     * 使用 Metal 后端时，如果应用程序延迟对 PresentCallable 的调用
     * （例如，通过在单独线程上调用它），必须确保在关闭 Filament Engine 之前
     * 已调用所有 PresentCallables。可以通过在 Engine::shutdown() 之前调用
     * Engine::flushAndWait() 来保证这一点。这对于确保 Engine 有机会
     * 清理与帧呈现相关的所有内存是必要的。
     *
     * 其他后端（OpenGL、Vulkan、WebGPU）
     * =======================================
     *
     * 在其他后端上，此回调用作 Filament 已完成帧的所有
     * CPU 端处理的通知。Filament 自动继续其正常的呈现逻辑，
     * 传递给回调的 PresentCallable 是一个可以安全
     * 忽略的无操作。
     *
     * 一般行为
     * ================
     *
     * 可以通过 SwapChain::setFrameScheduledCallback 在单个 SwapChain 上设置
     * FrameScheduledCallback。每个 SwapChain 每帧只能设置一个回调。
     * 如果在 Renderer::endFrame() 之前在同一 SwapChain 上多次调用 setFrameScheduledCallback，
     * 最近的调用会有效地覆盖任何先前设置的
     * 回调。
     *
     * 由 setFrameScheduledCallback 设置的回调在 Renderer::endFrame() 执行时被"锁定"。
     * 此时，回调对于刚刚编码的帧是固定的。
     * 在 endFrame() 之后对 setFrameScheduledCallback 的后续调用将应用于下一帧。
     *
     * 使用 \c setFrameScheduledCallback()（使用默认参数）来取消设置回调。
     *
     * @param handler    用于分派回调的处理器，或 nullptr 表示默认处理器
     * @param callback   帧处理完成时要调用的回调
     * @param flags      参见 CALLBACK_DEFAULT_USE_METAL_COMPLETION_HANDLER
     *
     * @see CallbackHandler
     * @see PresentCallable
     
     *
     * With the Metal backend, this callback signifies that Filament has completed all CPU-side
     * processing for a frame and the frame is ready to be scheduled for presentation.
     *
     * Typically, Filament is responsible for scheduling the frame's presentation to the SwapChain.
     * If a SwapChain::FrameScheduledCallback is set, however, the application bears the
     * responsibility of scheduling the frame for presentation by calling the
     * backend::PresentCallable passed to the callback function. In this mode, Filament will *not*
     * automatically schedule the frame for presentation.
     *
     * When using the Metal backend, if your application delays the call to the PresentCallable
     * (e.g., by invoking it on a separate thread), you must ensure all PresentCallables have been
     * called before shutting down the Filament Engine. You can guarantee this by calling
     * Engine::flushAndWait() before Engine::shutdown(). This is necessary to ensure the Engine has
     * a chance to clean up all memory related to frame presentation.
     *
     * Other Backends (OpenGL, Vulkan, WebGPU)
     * =======================================
     *
     * On other backends, this callback serves as a notification that Filament has completed all
     * CPU-side processing for a frame. Filament proceeds with its normal presentation logic
     * automatically, and the PresentCallable passed to the callback is a no-op that can be safely
     * ignored.
     *
     * General Behavior
     * ================
     *
     * A FrameScheduledCallback can be set on an individual SwapChain through
     * SwapChain::setFrameScheduledCallback. Each SwapChain can have only one callback set per
     * frame. If setFrameScheduledCallback is called multiple times on the same SwapChain before
     * Renderer::endFrame(), the most recent call effectively overwrites any previously set
     * callback.
     *
     * The callback set by setFrameScheduledCallback is "latched" when Renderer::endFrame() is
     * executed. At this point, the callback is fixed for the frame that was just encoded.
     * Subsequent calls to setFrameScheduledCallback after endFrame() will apply to the next frame.
     *
     * Use \c setFrameScheduledCallback() (with default arguments) to unset the callback.
     *
     * @param handler    Handler to dispatch the callback or nullptr for the default handler.
     * @param callback   Callback to be invoked when the frame processing is complete.
     * @param flags      See CALLBACK_DEFAULT_USE_METAL_COMPLETION_HANDLER
     *
     * @see CallbackHandler
     * @see PresentCallable
     */
    void setFrameScheduledCallback(backend::CallbackHandler* UTILS_NULLABLE handler = nullptr,
            FrameScheduledCallback&& callback = {}, uint64_t flags = 0);

    /**
     * Returns whether this SwapChain currently has a FrameScheduledCallback set.
     *
     * @return true, if the last call to setFrameScheduledCallback set a callback
     *
     * @see SwapChain::setFrameCompletedCallback
     */
    /**
     * 返回此 SwapChain 当前是否设置了 FrameScheduledCallback
     *
     * @return 如果最后一次调用 setFrameScheduledCallback 设置了回调，返回 true
     *
     * @see SwapChain::setFrameCompletedCallback
     */
    bool isFrameScheduledCallbackSet() const noexcept;

    /**
     * FrameCompletedCallback is a callback function that notifies an application when a frame's
     * contents have completed rendering on the GPU.
     *
     * Use SwapChain::setFrameCompletedCallback to set a callback on an individual SwapChain. Each
     * time a frame completes GPU rendering, the callback will be called.
     *
     * If handler is nullptr, the callback is guaranteed to be called on the main Filament thread.
     *
     * Use \c setFrameCompletedCallback() (with default arguments) to unset the callback.
     *
     * @param handler     Handler to dispatch the callback or nullptr for the default handler.
     * @param callback    Callback called when each frame completes.
     *
     * @remark Only Filament's Metal backend supports frame callbacks. Other backends ignore the
     * callback (which will never be called) and proceed normally.
     *
     * @see CallbackHandler
     */
    /**
     * FrameCompletedCallback 是一个回调函数，当帧的
     * 内容在 GPU 上完成渲染时通知应用程序。
     *
     * 使用 SwapChain::setFrameCompletedCallback 在单个 SwapChain 上设置回调。
     * 每次帧完成 GPU 渲染时，都会调用回调。
     *
     * 如果 handler 为 nullptr，保证在主 Filament 线程上调用回调。
     *
     * 使用 \c setFrameCompletedCallback()（使用默认参数）来取消设置回调。
     *
     * @param handler     用于分派回调的处理器，或 nullptr 表示默认处理器
     * @param callback    每帧完成时调用的回调
     *
     * @remark 只有 Filament 的 Metal 后端支持帧回调。其他后端忽略
     * 回调（永远不会被调用）并正常进行。
     *
     * @see CallbackHandler
     */
    void setFrameCompletedCallback(backend::CallbackHandler* UTILS_NULLABLE handler = nullptr,
            FrameCompletedCallback&& callback = {}) noexcept;


protected:
    // prevent heap allocation
    ~SwapChain() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_SWAPCHAIN_H
