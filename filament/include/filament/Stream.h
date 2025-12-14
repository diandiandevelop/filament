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

#ifndef TNT_FILAMENT_STREAM_H
#define TNT_FILAMENT_STREAM_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>
#include <backend/CallbackHandler.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <math/mat3.h>

#include <stdint.h>

namespace filament {

class FStream;

class Engine;

/**
 * Stream is used to attach a video stream to a Filament `Texture`.
 *
 * Note that the `Stream` class is fairly Android centric. It supports two different
 * configurations:
 *
 *   - ACQUIRED.....connects to an Android AHardwareBuffer
 *   - NATIVE.......connects to an Android SurfaceTexture
 *
 * Before explaining these different configurations, let's review the high-level structure of an AR
 * or video application that uses Filament:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * while (true) {
 *
 *     // Misc application work occurs here, such as:
 *     // - Writing the image data for a video frame into a Stream
 *     // - Moving the Filament Camera
 *
 *     if (renderer->beginFrame(swapChain)) {
 *         renderer->render(view);
 *         renderer->endFrame();
 *     }
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Let's say that the video image data at the time of a particular invocation of `beginFrame`
 * becomes visible to users at time A. The 3D scene state (including the camera) at the time of
 * that same invocation becomes apparent to users at time B.
 *
 * - If time A matches time B, we say that the stream is \em{synchronized}.
 * - Filament invokes low-level graphics commands on the \em{driver thread}.
 * - The thread that calls `beginFrame` is called the \em{main thread}.
 *
 * For ACQUIRED streams, there is no need to perform the copy because Filament explictly acquires
 * the stream, then releases it later via a callback function. This configuration is especially
 * useful when the Vulkan backend is enabled.
 *
 * For NATIVE streams, Filament does not make any synchronization guarantee. However they are simple
 * to use and do not incur a copy. These are often appropriate in video applications.
 *
 * Please see `sample-stream-test` and `sample-hello-camera` for usage examples.
 *
 * @see backend::StreamType
 * @see Texture#setExternalStream
 * @see Engine#destroyStream
 */
/**
 * Stream 用于将视频流附加到 Filament `Texture`。
 *
 * 请注意 `Stream` 类主要面向 Android。它支持两种不同的
 * 配置：
 *
 *   - ACQUIRED（获取）..... 连接到 Android AHardwareBuffer
 *   - NATIVE（原生）....... 连接到 Android SurfaceTexture
 *
 * 在解释这些不同配置之前，让我们回顾一下使用 Filament 的 AR
 * 或视频应用程序的高级结构：
 *
 * 对于 ACQUIRED 流，由于 Filament 显式获取
 * 流，然后通过回调函数稍后释放它，因此无需执行复制。当启用 Vulkan 后端时，此配置特别
 * 有用。
 *
 * 对于 NATIVE 流，Filament 不提供任何同步保证。但它们使用简单
 * 且不会产生复制开销。这些通常适用于视频应用程序。
 *
 * 如果时间 A 与时间 B 匹配，我们说流是\em{同步的}。
 * Filament 在\em{驱动线程}上调用低级图形命令。
 * 调用 `beginFrame` 的线程称为\em{主线程}。
 *
 * 请参阅 `sample-stream-test` 和 `sample-hello-camera` 以获取使用示例。
 *
 * @see backend::StreamType
 * @see Texture#setExternalStream
 * @see Engine#destroyStream
 */
class UTILS_PUBLIC Stream : public FilamentAPI {
    struct BuilderDetails;

public:
    using Callback = backend::StreamCallback;
    using StreamType = backend::StreamType;

    /**
     * Constructs a Stream object instance.
     *
     * By default, Stream objects are ACQUIRED and must have external images pushed to them via
     * <pre>Stream::setAcquiredImage</pre>.
     *
     * To create a NATIVE stream, call the <pre>stream</pre> method on the builder.
     */
    /**
     * 构造 Stream 对象实例。
     *
     * 默认情况下，Stream 对象是 ACQUIRED（获取）类型，必须通过
     * <pre>Stream::setAcquiredImage</pre> 向其推送外部图像。
     *
     * 要创建 NATIVE（原生）流，请在构建器上调用 <pre>stream</pre> 方法。
     */
    class Builder : public BuilderBase<BuilderDetails>, public BuilderNameMixin<Builder> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Creates a NATIVE stream. Native streams can sample data directly from an
         * opaque platform object such as a SurfaceTexture on Android.
         *
         * @param stream An opaque native stream handle. e.g.: on Android this is an
         *                     `android/graphics/SurfaceTexture` JNI jobject. The wrap mode must
         *                     be CLAMP_TO_EDGE.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 创建 NATIVE（原生）流。原生流可以直接从
         * 不透明的平台对象（如 Android 上的 SurfaceTexture）采样数据。
         *
         * @param stream 不透明的原生流句柄。例如：在 Android 上这是
         *                      `android/graphics/SurfaceTexture` JNI jobject。包装模式必须
         *                      是 CLAMP_TO_EDGE。
         *
         * @return 此 Builder，用于链接调用。
         */
        Builder& stream(void* UTILS_NULLABLE stream) noexcept;

        /**
         *
         * @param width initial width of the incoming stream. Whether this value is used is
         *              stream dependent. On Android, it must be set when using
         *              Builder::stream(long externalTextureId).
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 设置传入流的初始宽度。是否使用此值取决于
         * 流。在 Android 上，使用
         * Builder::stream(long externalTextureId) 时必须设置。
         *
         * @param width 传入流的初始宽度
         *
         * @return 此 Builder，用于链接调用。
         */
        Builder& width(uint32_t width) noexcept;

        /**
         *
         * @param height initial height of the incoming stream. Whether this value is used is
         *              stream dependent. On Android, it must be set when using
         *              Builder::stream(long externalTextureId).
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 设置传入流的初始高度。是否使用此值取决于
         * 流。在 Android 上，使用
         * Builder::stream(long externalTextureId) 时必须设置。
         *
         * @param height 传入流的初始高度
         *
         * @return 此 Builder，用于链接调用。
         */
        Builder& height(uint32_t height) noexcept;

        /**
         * Associate an optional name with this Stream for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this Stream
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 为此 Stream 关联一个可选名称，用于调试目的。
         *
         * 名称将显示在错误消息中，应尽可能简短。名称
         * 最多截断为 128 个字符。
         *
         * 名称字符串在此方法中复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 Stream 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用。
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this Stream for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this Stream
         * @return This Builder, for chaining calls.
         */
        /**
         * 为此 Stream 关联一个可选名称，用于调试目的。
         *
         * 名称将显示在错误消息中，应尽可能简短。
         *
         * @param name 用于标识此 Stream 的字符串字面量
         * @return 此 Builder，用于链接调用。
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the Stream object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this Stream with.
         *
         * @return pointer to the newly created object.
         */
        /**
         * 创建 Stream 对象并返回指向它的指针。
         *
         * @param engine 要与此 Stream 关联的 filament::Engine 的引用。
         *
         * @return 指向新创建对象的指针。
         */
        Stream* UTILS_NONNULL build(Engine& engine);

    private:
        friend class FStream;
    };

    /**
     * Indicates whether this stream is a NATIVE stream or ACQUIRED stream.
     */
    /**
     * 指示此流是 NATIVE（原生）流还是 ACQUIRED（获取）流。
     */
    StreamType getStreamType() const noexcept;

    /**
     * Updates an ACQUIRED stream with an image that is guaranteed to be used in the next frame.
     *
     * This method tells Filament to immediately "acquire" the image and trigger a callback
     * when it is done with it. This should be called by the user outside of beginFrame / endFrame,
     * and should be called only once per frame. If the user pushes images to the same stream
     * multiple times in a single frame, only the final image is honored, but all callbacks are
     * invoked.
     *
     * This method should be called on the same thread that calls Renderer::beginFrame, which is
     * also where the callback is invoked. This method can only be used for streams that were
     * constructed without calling the `stream` method on the builder.
     *
     * @see Stream for more information about NATIVE and ACQUIRED configurations.
     *
     * @param image      Pointer to AHardwareBuffer, casted to void* since this is a public header.
     * @param callback   This is triggered by Filament when it wishes to release the image.
     *                   The callback tales two arguments: the AHardwareBuffer and the userdata.
     * @param userdata   Optional closure data. Filament will pass this into the callback when it
     *                   releases the image.
     * @param transform  Optional transform matrix to apply to the image.
     */
    /**
     * 使用保证在下一帧中使用的图像更新 ACQUIRED（获取）流。
     *
     * 此方法告诉 Filament 立即"获取"图像，并在
     * 使用完图像时触发回调。这应该由用户在 beginFrame / endFrame 之外调用，
     * 并且每帧只应调用一次。如果用户在同一帧中多次向同一流推送图像，
     * 只有最后一个图像会被使用，但所有回调都会被
     * 调用。
     *
     * 此方法应在调用 Renderer::beginFrame 的同一线程上调用，这也是
     * 调用回调的地方。此方法只能用于
     * 在构建器上未调用 `stream` 方法构造的流。
     *
     * @see Stream 获取有关 NATIVE 和 ACQUIRED 配置的更多信息。
     *
     * @param image      指向 AHardwareBuffer 的指针，由于这是公共头文件，因此转换为 void*。
     * @param callback   当 Filament 希望释放图像时触发。
     *                   回调接受两个参数：AHardwareBuffer 和 userdata。
     * @param userdata   可选的闭包数据。Filament 将在
     *                   释放图像时将其传递给回调。
     * @param transform  要应用于图像的可选变换矩阵。
     */
    void setAcquiredImage(void* UTILS_NONNULL image,
            Callback UTILS_NONNULL callback, void* UTILS_NULLABLE userdata, math::mat3f const& transform = math::mat3f()) noexcept;

    /**
     * @see setAcquiredImage(void*, Callback, void*)
     *
     * @param image      Pointer to AHardwareBuffer, casted to void* since this is a public header.
     * @param handler    Handler to dispatch the AcquiredImage or nullptr for the default handler.
     * @param callback   This is triggered by Filament when it wishes to release the image.
     *                   It callback tales two arguments: the AHardwareBuffer and the userdata.
     * @param userdata   Optional closure data. Filament will pass this into the callback when it
     *                   releases the image.
     * @param transform  Optional transform matrix to apply to the image.
     */
    /**
     * @see setAcquiredImage(void*, Callback, void*)
     *
     * @param image      指向 AHardwareBuffer 的指针，由于这是公共头文件，因此转换为 void*。
     * @param handler    用于分派 AcquiredImage 的处理程序，或 nullptr 使用默认处理程序。
     * @param callback   当 Filament 希望释放图像时触发。
     *                   回调接受两个参数：AHardwareBuffer 和 userdata。
     * @param userdata   可选的闭包数据。Filament 将在
     *                   释放图像时将其传递给回调。
     * @param transform  要应用于图像的可选变换矩阵。
     */
    void setAcquiredImage(void* UTILS_NONNULL image,
            backend::CallbackHandler* UTILS_NULLABLE handler,
            Callback UTILS_NONNULL callback, void* UTILS_NULLABLE userdata, math::mat3f const& transform = math::mat3f()) noexcept;

    /**
     * Updates the size of the incoming stream. Whether this value is used is
     *              stream dependent. On Android, it must be set when using
     *              Builder::stream(long externalTextureId).
     *
     * @param width     new width of the incoming stream
     * @param height    new height of the incoming stream
     */
    /**
     * 更新传入流的尺寸。是否使用此值取决于
     * 流。在 Android 上，使用
     * Builder::stream(long externalTextureId) 时必须设置。
     *
     * @param width     传入流的新宽度
     * @param height    传入流的新高度
     */
    void setDimensions(uint32_t width, uint32_t height) noexcept;

    /**
     * Returns the presentation time of the currently displayed frame in nanosecond.
     *
     * This value can change at any time.
     *
     * @return timestamp in nanosecond.
     */
    /**
     * 返回当前显示帧的呈现时间（以纳秒为单位）。
     *
     * 此值可以随时更改。
     *
     * @return 时间戳（以纳秒为单位）。
     */
    int64_t getTimestamp() const noexcept;

protected:
    // prevent heap allocation
    ~Stream() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_STREAM_H
