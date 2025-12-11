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

// No include guard!!!

/*
 * How to use this header file.
 *
 * The DECL_DRIVER_API() macro must be defined before including this file:
 *     DECL_DRIVER_API(method_name, params_decl, params)
 *
 *     method_name          the name of the method (e.g. beginRenderPass)
 *     params_decl          the list of typed params
 *     params               the parameters, without their type (e.g.: for a function call)
 *
 * The following example adds all the driver methods as virtual
 * members of class:
 *
 * class MyDriver : public Driver {
 * public:
 * #define DRIVER_API(method_name, params_decl, params) \
 *     virtual void method_name(params_decl) override
 * #include "DriverAPI.h"
 * };
 *
 * DECL_DRIVER_API is automatically undefined.
 */

#ifndef DECL_DRIVER_API
#error "The DECL_DRIVER_API() macro must be defined before including this file"
#define DECL_DRIVER_API(M, D, P)
#endif

#ifndef DECL_DRIVER_API_SYNCHRONOUS
#error "The DECL_DRIVER_API_SYNCHRONOUS() macro must be defined before including this file"
#define DECL_DRIVER_API_SYNCHRONOUS(R, M, D, P)
#endif

#ifndef DECL_DRIVER_API_RETURN
#error "The DECL_DRIVER_API_RETURN() macro must be defined before including this file"
#define DECL_DRIVER_API_RETURN(R, M, D, P)
#endif


/*
 * Convenience macros. These are PRIVATE, don't use.
 */

#define EXPAND(x) x

#define APPLY0(M,...)
#define APPLY1(M, A, ...) EXPAND(M(A))
#define APPLY2(M, A, ...) EXPAND(M(A)), EXPAND(APPLY1(M, __VA_ARGS__))
#define APPLY3(M, A, ...) EXPAND(M(A)), EXPAND(APPLY2(M, __VA_ARGS__))
#define APPLY4(M, A, ...) EXPAND(M(A)), EXPAND(APPLY3(M, __VA_ARGS__))
#define APPLY5(M, A, ...) EXPAND(M(A)), EXPAND(APPLY4(M, __VA_ARGS__))
#define APPLY6(M, A, ...) EXPAND(M(A)), EXPAND(APPLY5(M, __VA_ARGS__))
#define APPLY7(M, A, ...) EXPAND(M(A)), EXPAND(APPLY6(M, __VA_ARGS__))
#define APPLY8(M, A, ...) EXPAND(M(A)), EXPAND(APPLY7(M, __VA_ARGS__))
#define APPLY9(M, A, ...) EXPAND(M(A)), EXPAND(APPLY8(M, __VA_ARGS__))
#define APPLY10(M, A, ...) EXPAND(M(A)), EXPAND(APPLY9(M, __VA_ARGS__))
#define APPLY11(M, A, ...) EXPAND(M(A)), EXPAND(APPLY10(M, __VA_ARGS__))
#define APPLY12(M, A, ...) EXPAND(M(A)), EXPAND(APPLY11(M, __VA_ARGS__))
#define APPLY_N__(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, X, ...) APPLY##X
#define APPLY(M, ...) EXPAND(EXPAND(APPLY_N__(M, __VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))(M, __VA_ARGS__))


#define PAIR_ARGS_0(M, ...)
#define PAIR_ARGS_1(M, X, Y, ...) M(X, Y)
#define PAIR_ARGS_2(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_1(M, __VA_ARGS__))
#define PAIR_ARGS_3(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_2(M, __VA_ARGS__))
#define PAIR_ARGS_4(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_3(M, __VA_ARGS__))
#define PAIR_ARGS_5(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_4(M, __VA_ARGS__))
#define PAIR_ARGS_6(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_5(M, __VA_ARGS__))
#define PAIR_ARGS_7(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_6(M, __VA_ARGS__))
#define PAIR_ARGS_8(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_7(M, __VA_ARGS__))
#define PAIR_ARGS_9(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_8(M, __VA_ARGS__))
#define PAIR_ARGS_10(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_9(M, __VA_ARGS__))
#define PAIR_ARGS_11(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_10(M, __VA_ARGS__))
#define PAIR_ARGS_12(M, X, Y, ...) M(X, Y), EXPAND(PAIR_ARGS_11(M, __VA_ARGS__))

#define PAIR_ARGS_N__(_0, E1, _1, E2, _2, E3, _3, E4, _4, E5, _5, E6, _6, E7, _7, E8, _8, E9, _9, E10, _10, E11, _11, E12, _12, X, ...) PAIR_ARGS_##X

#define PAIR_ARGS_N(M, ...) \
    EXPAND(EXPAND(PAIR_ARGS_N__(0, ##__VA_ARGS__, 12, E, 11, E, 10, E, 9, E, 8, E, 7, E, 6, E, 5, E, 4, E, 3, E, 2, E, 1, E, 0))(M, __VA_ARGS__))

#define ARG(T, P) T P

#define PARAM(T, P) P

#define DECL_DRIVER_API_N(N, ...) \
    DECL_DRIVER_API(N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_R_N(R, N, ...) \
    DECL_DRIVER_API_RETURN(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_TAGGED_R_N(R, N, ...) \
    DECL_DRIVER_API_RETURN(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__, utils::ImmutableCString&&, tag = {}), \
            PAIR_ARGS_N(PARAM, ##__VA_ARGS__, utils::ImmutableCString&&, tag))

#define DECL_DRIVER_API_SYNCHRONOUS_N(R, N, ...) \
    DECL_DRIVER_API_SYNCHRONOUS(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__), PAIR_ARGS_N(PARAM, ##__VA_ARGS__))

#define DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(R, N, ...) \
    DECL_DRIVER_API_SYNCHRONOUS(R, N, PAIR_ARGS_N(ARG, ##__VA_ARGS__, utils::ImmutableCString, tag = {}), \
            PAIR_ARGS_N(PARAM, ##__VA_ARGS__, utils::ImmutableCString, tag))

// on some compilers the ##__VA_ARGS__ hack is not supported, so we can't handle 0-parameter APIs
// with DECL_DRIVER_API_SYNCHRONOUS_N
#define DECL_DRIVER_API_SYNCHRONOUS_0(R, N) \
    DECL_DRIVER_API_SYNCHRONOUS(R, N,,)

// we can't handle empty lists because we don't have __VA_OPT__() in C++14 and the ## hack
// only works for omitted arguments, not empty lists
#define PARAM_1( P, ...) P
#define ARG_1( T, P, ...) T P
#define DECL_DRIVER_API_0(N)      DECL_DRIVER_API(N, ARG_1(int, dummy=0), PARAM_1(dummy))
#define DECL_DRIVER_API_R_0(R, N) DECL_DRIVER_API_RETURN(R, N, ARG_1(int, dummy=0), PARAM_1(dummy))

/*
 * Driver API 定义
 * 
 * 以下定义了所有 Driver 的 API 方法。
 * 
 * 宏说明：
 * - DECL_DRIVER_API: 异步方法（通过命令流执行）
 * - DECL_DRIVER_API_SYNCHRONOUS: 同步方法（立即执行，返回结果）
 * - DECL_DRIVER_API_RETURN: 返回句柄的异步方法（先同步获取句柄，再异步执行）
 * 
 * 方法分类：
 * 1. 帧管理：beginFrame、endFrame、flush、finish 等
 * 2. 资源创建：createVertexBuffer、createTexture、createProgram 等
 * 3. 资源更新：updateBufferObject、update3DImage 等
 * 4. 资源销毁：destroyVertexBuffer、destroyTexture 等
 * 5. 渲染状态：beginRenderPass、bindPipeline、bindRenderPrimitive 等
 * 6. 绘制命令：draw2、draw、dispatchCompute 等
 * 7. 同步操作：createFence、getFenceStatus、fenceWait 等
 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

/**
 * 执行周期性任务
 * 
 * 给后端一个机会执行周期性任务（如资源清理、状态更新等）。
 * 通常在每帧调用一次。
 */
DECL_DRIVER_API_0(tick)

/**
 * 开始新帧
 * 
 * 通知后端开始新的一帧渲染。
 * 
 * @param monotonic_clock_ns 单调时钟时间戳（纳秒）
 * @param refreshIntervalNs 刷新间隔（纳秒），0 表示未知
 * @param frameId 帧 ID（用于调试和追踪）
 */
DECL_DRIVER_API_N(beginFrame,
        int64_t, monotonic_clock_ns,
        int64_t, refreshIntervalNs,
        uint32_t, frameId)

DECL_DRIVER_API_N(setFrameScheduledCallback,
        backend::SwapChainHandle, sch,
        backend::CallbackHandler*, handler,
        backend::FrameScheduledCallback&&, callback,
        uint64_t, flags)

DECL_DRIVER_API_N(setFrameCompletedCallback,
        backend::SwapChainHandle, sch,
        backend::CallbackHandler*, handler,
        utils::Invocable<void(void)>&&, callback)

/**
 * 设置呈现时间
 * 
 * 设置帧的呈现时间，用于 VSYNC 同步。
 * 
 * @param monotonic_clock_ns 单调时钟时间戳（纳秒）
 */
DECL_DRIVER_API_N(setPresentationTime,
        int64_t, monotonic_clock_ns)

/**
 * 结束帧
 * 
 * 通知后端当前帧渲染完成。
 * 
 * @param frameId 帧 ID（用于调试和追踪）
 */
DECL_DRIVER_API_N(endFrame,
        uint32_t, frameId)

/**
 * 刷新命令缓冲区
 * 
 * 提示驱动已完成所有渲染目标的操作，可以开始渲染。
 * 例如对应 GLES 驱动的 glFlush()。
 * 
 * 注意：这是异步的，不会等待 GPU 完成。
 */
DECL_DRIVER_API_0(flush)

/**
 * 完成所有操作
 * 
 * 刷新并等待所有效果完成。
 * 对应 GLES 驱动的 glFinish()。
 * 
 * 注意：这会阻塞直到 GPU 完成所有操作，性能开销较大。
 */
DECL_DRIVER_API_0(finish)

/**
 * 重置状态跟踪
 * 
 * 如果驱动进行状态跟踪（如 OpenGL），重置所有状态。
 * 用于确保状态一致性。
 */
DECL_DRIVER_API_0(resetState)

/*
 * 创建驱动对象
 * -----------------------
 */

/**
 * 创建顶点缓冲区信息
 * 
 * 定义顶点缓冲区的布局（哪些属性在哪些缓冲区中）。
 * 
 * @param bufferCount 缓冲区对象数量（最多 16 个）
 * @param attributeCount 顶点属性数量（最多 16 个）
 * @param attributes 属性数组，定义每个属性的位置、格式、缓冲区索引等
 * @return 顶点缓冲区信息句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::VertexBufferInfoHandle, createVertexBufferInfo,
        uint8_t, bufferCount,
        uint8_t, attributeCount,
        backend::AttributeArray, attributes)

/**
 * 创建顶点缓冲区
 * 
 * 创建存储顶点数据的缓冲区。
 * 
 * @param vertexCount 顶点数量
 * @param vbih 顶点缓冲区信息句柄（定义布局）
 * @return 顶点缓冲区句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::VertexBufferHandle, createVertexBuffer,
        uint32_t, vertexCount,
        backend::VertexBufferInfoHandle, vbih)

/**
 * 创建索引缓冲区
 * 
 * 创建存储索引数据的缓冲区。
 * 
 * @param elementType 元素类型（USHORT 或 UINT）
 * @param indexCount 索引数量
 * @param usage 使用标志（STATIC 或 DYNAMIC）
 * @return 索引缓冲区句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::IndexBufferHandle, createIndexBuffer,
        backend::ElementType, elementType,
        uint32_t, indexCount,
        backend::BufferUsage, usage)

/**
 * 创建缓冲区对象
 * 
 * 创建通用的 GPU 缓冲区（Uniform Buffer、Storage Buffer 等）。
 * 
 * @param byteCount 缓冲区大小（字节）
 * @param bindingType 绑定类型（UNIFORM、STORAGE 等）
 * @param usage 使用标志（STATIC 或 DYNAMIC）
 * @return 缓冲区对象句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::BufferObjectHandle, createBufferObject,
        uint32_t, byteCount,
        backend::BufferObjectBinding, bindingType,
        backend::BufferUsage, usage)

/**
 * 创建纹理
 * 
 * 创建 GPU 纹理资源。
 * 
 * @param target 采样器类型（2D、3D、CUBEMAP、2D_ARRAY 等）
 * @param levels Mipmap 级别数（1 表示无 Mipmap）
 * @param format 纹理格式（RGBA8、RGB16F、R11F_G11F_B10F 等）
 * @param samples 多重采样数（1 表示无多重采样，必须是 2 的幂）
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param depth 纹理深度（3D 纹理或数组纹理的层数）
 * @param usage 使用标志（SAMPLEABLE、COLOR_ATTACHMENT、DEPTH_ATTACHMENT 等）
 * @return 纹理句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTexture,
        backend::SamplerType, target,
        uint8_t, levels,
        backend::TextureFormat, format,
        uint8_t, samples,
        uint32_t, width,
        uint32_t, height,
        uint32_t, depth,
        backend::TextureUsage, usage)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTextureView,
        backend::TextureHandle, texture,
        uint8_t, baseLevel,
        uint8_t, levelCount)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTextureViewSwizzle,
        backend::TextureHandle, texture,
        backend::TextureSwizzle, r,
        backend::TextureSwizzle, g,
        backend::TextureSwizzle, b,
        backend::TextureSwizzle, a)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTextureExternalImage2,
        backend::SamplerType, target,
        backend::TextureFormat, format,
        uint32_t, width,
        uint32_t, height,
        backend::TextureUsage, usage,
        backend::Platform::ExternalImageHandleRef, image)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTextureExternalImage,
        backend::SamplerType, target,
        backend::TextureFormat, format,
        uint32_t, width,
        uint32_t, height,
        backend::TextureUsage, usage,
        void*, image)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, createTextureExternalImagePlane,
        backend::TextureFormat, format,
        uint32_t, width,
        uint32_t, height,
        backend::TextureUsage, usage,
        void*, image,
        uint32_t, plane)

DECL_DRIVER_API_TAGGED_R_N(backend::TextureHandle, importTexture,
        intptr_t, id,
        backend::SamplerType, target,
        uint8_t, levels,
        backend::TextureFormat, format,
        uint8_t, samples,
        uint32_t, width,
        uint32_t, height,
        uint32_t, depth,
        backend::TextureUsage, usage)

/**
 * 创建渲染图元
 * 
 * 组合顶点缓冲区和索引缓冲区，定义如何绘制几何体。
 * 
 * @param vbh 顶点缓冲区句柄
 * @param ibh 索引缓冲区句柄（可以为空句柄，表示无索引绘制）
 * @param pt 图元类型（TRIANGLES、LINES、POINTS 等）
 * @return 渲染图元句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::RenderPrimitiveHandle, createRenderPrimitive,
        backend::VertexBufferHandle, vbh,
        backend::IndexBufferHandle, ibh,
        backend::PrimitiveType, pt)

/**
 * 创建着色器程序
 * 
 * 创建编译后的着色器程序。
 * 
 * @param program 程序对象（包含顶点着色器和片段着色器）
 * @return 程序句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::ProgramHandle, createProgram,
        backend::Program&&, program)

/**
 * 创建默认渲染目标
 * 
 * 创建默认的渲染目标（通常是窗口的帧缓冲区）。
 * 
 * @return 渲染目标句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::RenderTargetHandle, createDefaultRenderTarget)

/**
 * 创建渲染目标
 * 
 * 创建自定义的渲染目标（离屏渲染）。
 * 
 * @param targetBufferFlags 目标缓冲区标志（COLOR、DEPTH、STENCIL 的组合）
 * @param width 渲染目标宽度
 * @param height 渲染目标高度
 * @param samples 多重采样数（1 表示无多重采样）
 * @param layerCount 层数（用于数组渲染目标）
 * @param color 颜色附件（MRT 结构，包含多个颜色纹理）
 * @param depth 深度附件信息
 * @param stencil 模板附件信息
 * @return 渲染目标句柄
 */
DECL_DRIVER_API_TAGGED_R_N(backend::RenderTargetHandle, createRenderTarget,
        backend::TargetBufferFlags, targetBufferFlags,
        uint32_t, width,
        uint32_t, height,
        uint8_t, samples,
        uint8_t, layerCount,
        backend::MRT, color,
        backend::TargetBufferInfo, depth,
        backend::TargetBufferInfo, stencil)

DECL_DRIVER_API_TAGGED_R_N(backend::FenceHandle, createFence)

DECL_DRIVER_API_TAGGED_R_N(backend::SyncHandle, createSync)

DECL_DRIVER_API_TAGGED_R_N(backend::SwapChainHandle, createSwapChain,
        void*, nativeWindow,
        uint64_t, flags)

DECL_DRIVER_API_TAGGED_R_N(backend::SwapChainHandle, createSwapChainHeadless,
        uint32_t, width,
        uint32_t, height,
        uint64_t, flags)

DECL_DRIVER_API_TAGGED_R_N(backend::TimerQueryHandle, createTimerQuery)

DECL_DRIVER_API_TAGGED_R_N(backend::DescriptorSetLayoutHandle, createDescriptorSetLayout,
        backend::DescriptorSetLayout&&, info)

DECL_DRIVER_API_TAGGED_R_N(backend::DescriptorSetHandle, createDescriptorSet,
        backend::DescriptorSetLayoutHandle, dslh)

DECL_DRIVER_API_TAGGED_R_N(backend::MemoryMappedBufferHandle, mapBuffer,
        backend::BufferObjectHandle, boh,
        size_t, offset,
        size_t, size,
        backend::MapBufferAccessFlags, access)

/*
 * Destroying driver objects
 * -------------------------
 */

DECL_DRIVER_API_N(destroyVertexBuffer,          backend::VertexBufferHandle, vbh)
DECL_DRIVER_API_N(destroyVertexBufferInfo,      backend::VertexBufferInfoHandle, vbih)
DECL_DRIVER_API_N(destroyIndexBuffer,           backend::IndexBufferHandle, ibh)
DECL_DRIVER_API_N(destroyBufferObject,          backend::BufferObjectHandle, boh)
DECL_DRIVER_API_N(destroyRenderPrimitive,       backend::RenderPrimitiveHandle, rph)
DECL_DRIVER_API_N(destroyProgram,               backend::ProgramHandle, ph)
DECL_DRIVER_API_N(destroyTexture,               backend::TextureHandle, th)
DECL_DRIVER_API_N(destroyRenderTarget,          backend::RenderTargetHandle, rth)
DECL_DRIVER_API_N(destroySwapChain,             backend::SwapChainHandle, sch)
DECL_DRIVER_API_N(destroyStream,                backend::StreamHandle, sh)
DECL_DRIVER_API_N(destroyTimerQuery,            backend::TimerQueryHandle, sh)
DECL_DRIVER_API_N(destroyFence,                 backend::FenceHandle, fh)
DECL_DRIVER_API_N(destroySync,                  backend::SyncHandle, sh)
DECL_DRIVER_API_N(destroyDescriptorSetLayout,   backend::DescriptorSetLayoutHandle, dslh)
DECL_DRIVER_API_N(destroyDescriptorSet,         backend::DescriptorSetHandle, dsh)
DECL_DRIVER_API_N(unmapBuffer,                  backend::MemoryMappedBufferHandle, mmbh)

/*
 * Synchronous APIs
 * ----------------
 */

DECL_DRIVER_API_SYNCHRONOUS_0(void, terminate)
DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(backend::StreamHandle, createStreamNative, void*, stream)
DECL_DRIVER_API_SYNCHRONOUS_TAGGED_N(backend::StreamHandle, createStreamAcquired)
DECL_DRIVER_API_SYNCHRONOUS_N(void, setAcquiredImage, backend::StreamHandle, stream, void*, image, const math::mat3f&, transform, backend::CallbackHandler*, handler, backend::StreamCallback, cb, void*, userData)
DECL_DRIVER_API_SYNCHRONOUS_N(void, setStreamDimensions, backend::StreamHandle, stream, uint32_t, width, uint32_t, height)
DECL_DRIVER_API_SYNCHRONOUS_N(int64_t, getStreamTimestamp, backend::StreamHandle, stream)
DECL_DRIVER_API_SYNCHRONOUS_N(void, updateStreams, backend::DriverApi*, driver)
DECL_DRIVER_API_SYNCHRONOUS_N(backend::FenceStatus, getFenceStatus, backend::FenceHandle, fh)
DECL_DRIVER_API_SYNCHRONOUS_N(backend::FenceStatus, fenceWait, backend::FenceHandle, fh, uint64_t, timeout)
DECL_DRIVER_API_SYNCHRONOUS_N(void, fenceCancel, backend::FenceHandle, fh)
DECL_DRIVER_API_SYNCHRONOUS_N(void, getPlatformSync, backend::SyncHandle, sh,
        backend::CallbackHandler*, handler, backend::Platform::SyncCallback, cb, void*, userData)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isTextureFormatSupported, backend::TextureFormat, format)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isTextureSwizzleSupported)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isTextureFormatMipmappable, backend::TextureFormat, format)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isRenderTargetFormatSupported, backend::TextureFormat, format)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isFrameBufferFetchSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isFrameBufferFetchMultiSampleSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isFrameTimeSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isAutoDepthResolveSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isSRGBSwapChainSupported)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isMSAASwapChainSupported, uint32_t, samples)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isProtectedContentSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isStereoSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isParallelShaderCompileSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isDepthStencilResolveSupported)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isDepthStencilBlitSupported, backend::TextureFormat, format)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isProtectedTexturesSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isDepthClampSupported)
DECL_DRIVER_API_SYNCHRONOUS_0(uint8_t, getMaxDrawBuffers)
DECL_DRIVER_API_SYNCHRONOUS_0(size_t, getMaxUniformBufferSize)
DECL_DRIVER_API_SYNCHRONOUS_N(size_t, getMaxTextureSize, backend::SamplerType, target)
DECL_DRIVER_API_SYNCHRONOUS_0(size_t, getMaxArrayTextureLayers)
DECL_DRIVER_API_SYNCHRONOUS_0(math::float2, getClipSpaceParams)
DECL_DRIVER_API_SYNCHRONOUS_N(void, setupExternalImage2, backend::Platform::ExternalImageHandleRef, image)
DECL_DRIVER_API_SYNCHRONOUS_N(void, setupExternalImage, void*, image)
DECL_DRIVER_API_SYNCHRONOUS_N(backend::TimerQueryResult, getTimerQueryValue, backend::TimerQueryHandle, tqh, uint64_t*, elapsedTime)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, isWorkaroundNeeded, backend::Workaround, workaround)
DECL_DRIVER_API_SYNCHRONOUS_0(backend::FeatureLevel, getFeatureLevel)
DECL_DRIVER_API_SYNCHRONOUS_0(size_t, getUniformBufferOffsetAlignment)
DECL_DRIVER_API_SYNCHRONOUS_0(bool, isCompositorTimingSupported)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, queryFrameTimestamps, backend::SwapChainHandle, swapChain, uint64_t, frameId, backend::FrameTimestamps*, outFrameTimestamps)
DECL_DRIVER_API_SYNCHRONOUS_N(bool, queryCompositorTiming, backend::SwapChainHandle, swapChain, backend::CompositorTiming*, outCompositorTiming)

/*
 * Updating driver objects
 * -----------------------
 */

DECL_DRIVER_API_N(setVertexBufferObject,
        backend::VertexBufferHandle, vbh,
        uint32_t, index,
        backend::BufferObjectHandle, bufferObject)

DECL_DRIVER_API_N(updateIndexBuffer,
        backend::IndexBufferHandle, ibh,
        backend::BufferDescriptor&&, data,
        uint32_t, byteOffset)

DECL_DRIVER_API_N(updateBufferObject,
        backend::BufferObjectHandle, ibh,
        backend::BufferDescriptor&&, data,
        uint32_t, byteOffset)

DECL_DRIVER_API_N(updateBufferObjectUnsynchronized,
        backend::BufferObjectHandle, boh,
        backend::BufferDescriptor&&, data,
        uint32_t, byteOffset)

DECL_DRIVER_API_N(resetBufferObject,
        backend::BufferObjectHandle, boh)

DECL_DRIVER_API_N(update3DImage,
        backend::TextureHandle, th,
        uint32_t, level,
        uint32_t, xoffset,
        uint32_t, yoffset,
        uint32_t, zoffset,
        uint32_t, width,
        uint32_t, height,
        uint32_t, depth,
        backend::PixelBufferDescriptor&&, data)

DECL_DRIVER_API_N(generateMipmaps,
        backend::TextureHandle, th)

DECL_DRIVER_API_N(setExternalStream,
        backend::TextureHandle, th,
        backend::StreamHandle, sh)

DECL_DRIVER_API_N(beginTimerQuery,
        backend::TimerQueryHandle, tqh)

DECL_DRIVER_API_N(endTimerQuery,
        backend::TimerQueryHandle, tqh)

DECL_DRIVER_API_N(compilePrograms,
        backend::CompilerPriorityQueue, priority,
        backend::CallbackHandler*, handler,
        backend::CallbackHandler::Callback, callback,
        void*, user)

DECL_DRIVER_API_N(updateDescriptorSetBuffer,
        backend::DescriptorSetHandle, dsh,
        backend::descriptor_binding_t, binding,
        backend::BufferObjectHandle, boh,
        uint32_t, offset,
        uint32_t, size
)

DECL_DRIVER_API_N(updateDescriptorSetTexture,
        backend::DescriptorSetHandle, dsh,
        backend::descriptor_binding_t, binding,
        backend::TextureHandle, th,
        SamplerParams, params
)

DECL_DRIVER_API_N(copyToMemoryMappedBuffer,
        backend::MemoryMappedBufferHandle, mmbh,
        size_t, offset,
        backend::BufferDescriptor&&, data)

DECL_DRIVER_API_N(setPushConstant,
        backend::ShaderStage, stage,
        uint8_t, index,
        backend::PushConstantVariant, value)

/*
 * 设置渲染状态
 * -----------------------
 */

/**
 * 开始渲染通道
 * 
 * 开始一个新的渲染通道，设置渲染目标和清除参数。
 * 
 * @param rth 渲染目标句柄
 * @param params 渲染通道参数
 *   - viewport: 视口
 *   - clearColor: 清除颜色
 *   - clearFlags: 清除标志（COLOR、DEPTH、STENCIL）
 *   - discardStart/End: 丢弃标志（优化用）
 */
DECL_DRIVER_API_N(beginRenderPass,
        backend::RenderTargetHandle, rth,
        const backend::RenderPassParams&, params)

/**
 * 结束渲染通道
 * 
 * 结束当前渲染通道，执行必要的解析和转换操作。
 */
DECL_DRIVER_API_0(endRenderPass)

/**
 * 下一个子通道
 * 
 * 在支持子通道的 API（如 Vulkan）中，移动到下一个子通道。
 * 用于优化渲染（避免回写到内存）。
 */
DECL_DRIVER_API_0(nextSubpass)

/**
 * 绑定管线状态
 * 
 * 绑定完整的管线状态，包括：
 * - 着色器程序
 * - 光栅化状态（混合、深度测试、面剔除等）
 * - 顶点缓冲区信息
 * - 图元类型
 * - 多边形偏移
 * - 模板状态
 * 
 * @param state 管线状态对象
 */
DECL_DRIVER_API_N(bindPipeline,
        backend::PipelineState const&, state)

/**
 * 绑定渲染图元
 * 
 * 绑定要绘制的几何体（顶点缓冲区和索引缓冲区的组合）。
 * 
 * @param rph 渲染图元句柄
 */
DECL_DRIVER_API_N(bindRenderPrimitive,
        backend::RenderPrimitiveHandle, rph)

/**
 * 绑定描述符集
 * 
 * 绑定资源集合到着色器（纹理、Uniform Buffer 等）。
 * 
 * @param dsh 描述符集句柄
 * @param set 描述符集索引（PER_VIEW、PER_MATERIAL、PER_RENDERABLE 等）
 * @param offsets 偏移数组（用于动态 Uniform Buffer 偏移）
 */
DECL_DRIVER_API_N(bindDescriptorSet,
        backend::DescriptorSetHandle, dsh,
        backend::descriptor_set_t, set,
        backend::DescriptorSetOffsetArray&&, offsets
)

/*
 * Swap chain
 */

DECL_DRIVER_API_N(makeCurrent,
        backend::SwapChainHandle, schDraw,
        backend::SwapChainHandle, schRead)


DECL_DRIVER_API_N(commit,
        backend::SwapChainHandle, sch)

/*
 * Setting rendering state
 * -----------------------
 */

DECL_DRIVER_API_N(insertEventMarker,
        const char*, string)

DECL_DRIVER_API_N(pushGroupMarker,
        const char*, string)

DECL_DRIVER_API_0(popGroupMarker)

DECL_DRIVER_API_0(startCapture)

DECL_DRIVER_API_0(stopCapture)


/*
 * Read-back operations
 * --------------------
 */

DECL_DRIVER_API_N(readPixels,
        backend::RenderTargetHandle, src,
        uint32_t, x,
        uint32_t, y,
        uint32_t, width,
        uint32_t, height,
        backend::PixelBufferDescriptor&&, data)

DECL_DRIVER_API_N(readBufferSubData,
        backend::BufferObjectHandle, src,
        uint32_t, offset,
        uint32_t, size,
        backend::BufferDescriptor&&, data)

/*
 * 渲染操作
 * --------------------
 */

/**
 * 位块传输（已废弃）
 * 
 * 在渲染目标之间复制数据。
 * 已废弃，使用 blit() 代替。
 * 
 * @param buffers 要复制的缓冲区标志
 * @param dst 目标渲染目标
 * @param dstRect 目标矩形
 * @param src 源渲染目标
 * @param srcRect 源矩形
 * @param filter 放大过滤器（LINEAR 或 NEAREST）
 */
DECL_DRIVER_API_N(blitDEPRECATED,
        backend::TargetBufferFlags, buffers,
        backend::RenderTargetHandle, dst,
        backend::Viewport, dstRect,
        backend::RenderTargetHandle, src,
        backend::Viewport, srcRect,
        backend::SamplerMagFilter, filter)

/**
 * 解析多重采样纹理
 * 
 * 将多重采样纹理解析为单采样纹理。
 * 
 * @param dst 目标纹理句柄
 * @param dstLevel 目标 Mipmap 级别
 * @param dstLayer 目标层（数组纹理）
 * @param src 源纹理句柄（必须是多重采样）
 * @param srcLevel 源 Mipmap 级别
 * @param srcLayer 源层（数组纹理）
 */
DECL_DRIVER_API_N(resolve,
        backend::TextureHandle, dst,
        uint8_t, dstLevel, uint8_t, dstLayer,
        backend::TextureHandle, src,
        uint8_t, srcLevel, uint8_t, srcLayer)

/**
 * 位块传输
 * 
 * 在纹理之间复制数据。
 * 
 * @param dst 目标纹理句柄
 * @param dstLevel 目标 Mipmap 级别
 * @param dstLayer 目标层（数组纹理）
 * @param dstOrigin 目标原点（x, y）
 * @param src 源纹理句柄
 * @param srcLevel 源 Mipmap 级别
 * @param srcLayer 源层（数组纹理）
 * @param srcOrigin 源原点（x, y）
 * @param size 复制区域大小（width, height）
 */
DECL_DRIVER_API_N(blit,
        backend::TextureHandle, dst,
        uint8_t, dstLevel, uint8_t, dstLayer,
        math::uint2, dstOrigin,
        backend::TextureHandle, src,
        uint8_t, srcLevel, uint8_t, srcLayer,
        math::uint2, srcOrigin,
        math::uint2, size)

/**
 * 绘制（使用当前绑定的状态）
 * 
 * 执行绘制调用，使用当前绑定的管线状态、渲染图元和描述符集。
 * 这是推荐的绘制方法，性能更好。
 * 
 * @param indexOffset 索引偏移（从索引缓冲区的哪个位置开始）
 * @param indexCount 索引数量（要绘制多少个索引）
 * @param instanceCount 实例数量（实例化绘制）
 */
DECL_DRIVER_API_N(draw2,
        uint32_t, indexOffset,
        uint32_t, indexCount,
        uint32_t, instanceCount)

/**
 * 绘制（包含完整状态，已废弃）
 * 
 * 执行绘制调用，包含完整的管线状态和渲染图元。
 * 已废弃，使用 draw2() 代替。
 * 
 * @param state 管线状态
 * @param rph 渲染图元句柄
 * @param indexOffset 索引偏移
 * @param indexCount 索引数量
 * @param instanceCount 实例数量
 */
DECL_DRIVER_API_N(draw,
        backend::PipelineState, state,
        backend::RenderPrimitiveHandle, rph,
        uint32_t, indexOffset,
        uint32_t, indexCount,
        uint32_t, instanceCount)

/**
 * 调度计算着色器
 * 
 * 执行计算着色器程序。
 * 
 * @param program 计算着色器程序句柄
 * @param workGroupCount 工作组数量（x, y, z）
 */
DECL_DRIVER_API_N(dispatchCompute,
        backend::ProgramHandle, program,
        math::uint3, workGroupCount)

/**
 * 设置裁剪矩形
 * 
 * 设置裁剪矩形，限制渲染区域。
 * 
 * @param scissor 裁剪矩形（left, bottom, width, height）
 */
DECL_DRIVER_API_N(scissor,
        Viewport, scissor)


#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#undef EXPAND

#undef PARAM_1
#undef ARG_1
#undef DECL_DRIVER_API_0
#undef DECL_DRIVER_API_R_0

#undef PARAM
#undef ARG
#undef DECL_DRIVER_API_N
#undef DECL_DRIVER_API_R_N
#undef DECL_DRIVER_API_SYNCHRONOUS_N
#undef DECL_DRIVER_API_SYNCHRONOUS_0

#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_SYNCHRONOUS
#undef DECL_DRIVER_API_RETURN

#undef PAIR_ARGS_1
#undef PAIR_ARGS_2
#undef PAIR_ARGS_3
#undef PAIR_ARGS_4
#undef PAIR_ARGS_5
#undef PAIR_ARGS_6
#undef PAIR_ARGS_7
#undef PAIR_ARGS_8
#undef PAIR_ARGS_N__
#undef PAIR_ARGS_N

// No include guard!!!
