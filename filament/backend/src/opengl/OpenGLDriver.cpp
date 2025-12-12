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

#include "OpenGLDriver.h"

#include "CommandStreamDispatcher.h"
#include "GLTexture.h"
#include "GLMemoryMappedBuffer.h"
#include "GLUtils.h"
#include "OpenGLContext.h"
#include "OpenGLDriverFactory.h"
#include "OpenGLProgram.h"
#include "OpenGLTimerQuery.h"
#include "SystraceProfile.h"
#include "gl_headers.h"

#include <backend/platforms/OpenGLPlatform.h>

#include <backend/BufferDescriptor.h>
#include <backend/CallbackHandler.h>
#include <backend/DescriptorSetOffsetArray.h>
#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>
#include <backend/Platform.h>
#include <backend/Program.h>
#include <backend/TargetBufferInfo.h>

#include "private/backend/CommandStream.h"
#include "private/backend/Dispatcher.h"
#include "private/backend/DriverApi.h"

#include <private/utils/Tracing.h>

#include <type_traits>
#include <utils/BitmaskEnum.h>
#include <utils/CString.h>
#include <utils/ImmutableCString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Invocable.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/Slice.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/mat3.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#endif

/**
 * 编译时配置常量
 */

// 2D 多重采样纹理支持（仅在 OpenGL ES 3.1+ 支持）
// 当前禁用此功能，因为我们不需要它
#define TEXTURE_2D_MULTISAMPLE_SUPPORTED false

// 缓冲区映射支持（WebGL 不支持）
#if defined(__EMSCRIPTEN__)
#define HAS_MAPBUFFERS 0  // WebGL 不支持缓冲区映射
#else
#define HAS_MAPBUFFERS 1  // 桌面 OpenGL 支持缓冲区映射
#endif

/**
 * 调试标记级别定义
 * 
 * DEBUG_GROUP_MARKER: 用于用户标记（默认：全部启用）
 * DEBUG_MARKER: 用于内部调试（默认：无）
 */
#define DEBUG_GROUP_MARKER_NONE       0x00    // 无调试标记
#define DEBUG_GROUP_MARKER_OPENGL     0x01    // OpenGL 命令队列中的标记（需要驱动支持）
#define DEBUG_GROUP_MARKER_BACKEND    0x02    // 后端侧的标记（perfetto）
#define DEBUG_GROUP_MARKER_ALL        0xFF    // 所有标记

#define DEBUG_MARKER_NONE             0x00    // 无调试标记
#define DEBUG_MARKER_OPENGL           0x01    // OpenGL 命令队列中的标记（需要驱动支持）
#define DEBUG_MARKER_BACKEND          0x02    // 后端侧的标记（perfetto）
#define DEBUG_MARKER_PROFILE          0x04    // 后端侧的性能分析（perfetto）
#define DEBUG_MARKER_ALL              (0xFF & ~DEBUG_MARKER_PROFILE) // 所有标记（除了性能分析）

// 设置所需的调试标记级别（用于用户标记 [默认：全部]）
#define DEBUG_GROUP_MARKER_LEVEL      DEBUG_GROUP_MARKER_ALL

// 设置所需的调试级别（用于内部调试 [默认：无]）
#define DEBUG_MARKER_LEVEL            DEBUG_MARKER_NONE

// Override the debug markers if we are forcing profiling mode
#if defined(FILAMENT_FORCE_PROFILING_MODE)
#   undef DEBUG_GROUP_MARKER_LEVEL
#   undef DEBUG_MARKER_LEVEL

#   define DEBUG_GROUP_MARKER_LEVEL   DEBUG_GROUP_MARKER_NONE
#   define DEBUG_MARKER_LEVEL         DEBUG_MARKER_PROFILE
#endif

#if DEBUG_MARKER_LEVEL == DEBUG_MARKER_PROFILE
#   define DEBUG_MARKER()
#   define PROFILE_MARKER(marker) PROFILE_SCOPE(marker);
#   if DEBUG_GROUP_MARKER_LEVEL != DEBUG_GROUP_MARKER_NONE
#      error PROFILING is exclusive; group markers must be disabled.
#   endif
#   ifndef NDEBUG
#      error PROFILING is meaningless in DEBUG mode.
#   endif
#elif DEBUG_MARKER_LEVEL > DEBUG_MARKER_NONE
#   define DEBUG_MARKER() DebugMarker _debug_marker(*this, __func__);
#   define PROFILE_MARKER(marker) DEBUG_MARKER()
#   if DEBUG_MARKER_LEVEL & DEBUG_MARKER_PROFILE
#      error PROFILING is exclusive; all other debug features must be disabled.
#   endif
#else
#   define DEBUG_MARKER()
#   define PROFILE_MARKER(marker)
#endif

using namespace filament::math;
using namespace utils;

namespace filament::backend {

namespace {

/**
 * 同步栅栏回调包装器
 * 
 * 用于将 OpenGL 同步栅栏的回调转换为 Filament 的回调格式。
 * 当 GPU 完成同步栅栏时，此回调会被调用。
 * 
 * @param userData 指向 GLSyncFence::CallbackData 的指针
 * 
 * 注意：此回调假设同步栅栏尚未被销毁。如果已销毁，行为未定义。
 */
CallbackHandler::Callback syncCallbackWrapper = [](void* userData) {
    std::unique_ptr<OpenGLDriver::GLSyncFence::CallbackData> const cbData(
            static_cast<OpenGLDriver::GLSyncFence::CallbackData*>(userData));
    // 此回调假设同步栅栏尚未被销毁。如果已销毁，行为未定义。
    cbData->cb(cbData->sync, cbData->userData);
};

} // namespace

/**
 * OpenGLDriverFactory::create
 * 
 * 工厂方法，创建 OpenGLDriver 实例。
 * 这是创建 OpenGL 驱动的统一入口点。
 * 
 * @param platform OpenGL 平台接口指针（不能为空）
 *                  负责创建和管理 OpenGL 上下文、交换链等
 * @param sharedGLContext 共享 OpenGL 上下文（可选，可为 nullptr）
 *                        用于在多个上下文之间共享资源
 *                        注意：当前实现中此参数未使用，但保留以保持接口一致性
 * @param driverConfig 驱动配置参数
 *                     - handleArenaSize: Handle 分配器大小
 *                     - disableHandleUseAfterFreeCheck: 禁用句柄释放后使用检查
 *                     - disableHeapHandleTags: 禁用堆句柄标签
 *                     - forceGLES2Context: 强制使用 GLES 2.0 上下文
 * @return Driver 指针，成功返回 OpenGLDriver 实例，失败返回 nullptr
 * 
 * 执行流程：
 * 1. 调用 OpenGLDriver::create() 进行实际的创建和初始化
 * 2. OpenGLDriver::create() 会：
 *    - 验证平台指针非空
 *    - 查询 OpenGL 版本（major.minor）
 *    - 验证版本支持（ES 2.0+ 或 GL 4.1+）
 *    - 验证失败则清理并返回 nullptr
 *    - 设置有效的配置
 *    - 创建并初始化 OpenGLDriver 实例
 * 
 * 版本要求：
 * - OpenGL ES 2.0+（最低要求）
 * - 桌面 OpenGL 4.1+（最低要求）
 * - 如果配置了 forceGLES2Context，强制使用 ES 2.0
 * 
 * 注意：
 * - 此方法会创建 OpenGLContext，因此必须在有效的 OpenGL 上下文中调用
 * - 返回的驱动实例由调用者负责管理生命周期
 * - 此工厂方法只是简单地委托给 OpenGLDriver::create()，保持接口一致性
 */
Driver* OpenGLDriverFactory::create(
        OpenGLPlatform* platform,
        void* sharedGLContext,
        const Platform::DriverConfig& driverConfig) noexcept {
    return OpenGLDriver::create(platform, sharedGLContext, driverConfig);
}

using namespace GLUtils;

// ------------------------------------------------------------------------------------------------

/**
 * OpenGLDriver::create
 * 
 * 静态工厂方法，创建 OpenGLDriver 实例。
 * 在创建驱动之前，会检查 OpenGL 版本是否支持。
 * 
 * @param platform OpenGL 平台接口，负责创建和管理 OpenGL 上下文
 * @param sharedGLContext 共享 OpenGL 上下文（当前未使用）
 * @param driverConfig 驱动配置参数
 * @return OpenGLDriver 实例指针，失败返回 nullptr
 * 
 * 执行流程：
 * 1. 验证平台指针非空
 * 2. 查询 OpenGL 版本（major.minor）
 * 3. 验证版本支持：
 *    - OpenGL ES: 至少 2.0
 *    - 桌面 OpenGL: 至少 4.1
 * 4. 验证失败则清理并返回 nullptr
 * 5. 设置有效的配置（确保 handleArenaSize 至少为默认值）
 * 6. 创建 OpenGLDriver 实例
 * 
 * 版本要求：
 * - OpenGL ES 2.0+（最低要求）
 * - 桌面 OpenGL 4.1+（最低要求）
 * - 如果配置了 forceGLES2Context，强制使用 ES 2.0
 */
UTILS_NOINLINE
OpenGLDriver* OpenGLDriver::create(OpenGLPlatform* platform,
        void* /*sharedGLContext*/, const Platform::DriverConfig& driverConfig) noexcept {
    assert_invariant(platform);
    OpenGLPlatform* ec = platform;

#if 0
    // this is useful for development, but too verbose even for debug builds
    // For reference on a 64-bits machine in Release mode:
    //    GLIndexBuffer             :   8       moderate
    //    GLSwapChain               :  16       few
    //    GLTimerQuery              :  16       few
    //    GLFence                   :  24       few
    //    GLRenderPrimitive         :  32       many
    //    GLBufferObject            :  32       many
    // -- less than or equal 32 bytes
    //    GLTexture                 :  64       moderate
    //    GLVertexBuffer            :  76       moderate
    //    OpenGLProgram             :  96       moderate
    // -- less than or equal 96 bytes
    //    GLStream                  : 104       few
    //    GLRenderTarget            : 112       few
    //    GLVertexBufferInfo        : 132       moderate
    // -- less than or equal to 136 bytes

    DLOG(INFO) << "GLSwapChain: " << sizeof(GLSwapChain);
    DLOG(INFO) << "GLBufferObject: " << sizeof(GLBufferObject);
    DLOG(INFO) << "GLVertexBuffer: " << sizeof(GLVertexBuffer);
    DLOG(INFO) << "GLVertexBufferInfo: " << sizeof(GLVertexBufferInfo);
    DLOG(INFO) << "GLIndexBuffer: " << sizeof(GLIndexBuffer);
    DLOG(INFO) << "GLRenderPrimitive: " << sizeof(GLRenderPrimitive);
    DLOG(INFO) << "GLTexture: " << sizeof(GLTexture);
    DLOG(INFO) << "GLTimerQuery: " << sizeof(GLTimerQuery);
    DLOG(INFO) << "GLStream: " << sizeof(GLStream);
    DLOG(INFO) << "GLRenderTarget: " << sizeof(GLRenderTarget);
    DLOG(INFO) << "GLFence: " << sizeof(GLFence);
    DLOG(INFO) << "OpenGLProgram: " << sizeof(OpenGLProgram);
#endif

    // 在初始化驱动之前，检查我们是否在支持的 OpenGL 版本上
    GLint major = 0, minor = 0;
    bool const success = OpenGLContext::queryOpenGLVersion(&major, &minor);

    if (UTILS_UNLIKELY(!success)) {
        PANIC_LOG("Can't get OpenGL version");
        cleanup:
        ec->terminate();
        return {};
    }

#if defined(BACKEND_OPENGL_VERSION_GLES)
    // OpenGL ES 版本检查：至少需要 ES 2.0
    if (UTILS_UNLIKELY(!(major >= 2 && minor >= 0))) {
        PANIC_LOG("OpenGL ES 2.0 minimum needed (current %d.%d)", major, minor);
        goto cleanup;
    }
    // 如果配置强制使用 ES 2.0，则强制版本为 2.0
    if (UTILS_UNLIKELY(driverConfig.forceGLES2Context)) {
        major = 2;
        minor = 0;
    }
#else
    // 桌面 OpenGL 版本检查：需要 GL 4.1 头文件和最低版本
    if (UTILS_UNLIKELY(!((major == 4 && minor >= 1) || major > 4))) {
        PANIC_LOG("OpenGL 4.1 minimum needed (current %d.%d)", major, minor);
        goto cleanup;
    }
#endif

    // 设置有效的配置：确保 handleArenaSize 至少为默认值
    constexpr size_t defaultSize = FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB * 1024U * 1024U;
    Platform::DriverConfig validConfig{ driverConfig };
    validConfig.handleArenaSize = std::max(driverConfig.handleArenaSize, defaultSize);
    
    // 创建 OpenGLDriver 实例
    OpenGLDriver* driver = new(std::nothrow) OpenGLDriver(ec, validConfig);
    return driver;
}

// ------------------------------------------------------------------------------------------------

/**
 * DebugMarker 构造函数
 * 
 * 创建调试标记，用于在调试工具（如 RenderDoc、Xcode GPU Debugger）中标记命令组。
 * 支持两种类型的标记：
 * 1. OpenGL 调试标记（glPushGroupMarkerEXT）：需要驱动支持
 * 2. 后端标记（Perfetto）：用于性能分析
 * 
 * @param driver OpenGLDriver 引用
 * @param string 标记名称（函数名或自定义名称）
 * 
 * 执行流程：
 * 1. 如果启用了 OpenGL 调试标记且驱动支持，调用 glPushGroupMarkerEXT
 * 2. 如果启用了后端标记，开始 Perfetto 追踪
 */
OpenGLDriver::DebugMarker::DebugMarker(OpenGLDriver& driver, const char* string) noexcept
        : driver(driver) {
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_debug_marker
#if DEBUG_MARKER_LEVEL & DEBUG_MARKER_OPENGL
    // OpenGL 调试标记：在驱动命令队列中插入标记（需要驱动支持）
    if (UTILS_LIKELY(driver.getContext().ext.EXT_debug_marker)) {
        glPushGroupMarkerEXT(GLsizei(strlen(string)), string);
    }
#endif
#endif

#if DEBUG_MARKER_LEVEL & DEBUG_MARKER_BACKEND
    // 后端标记：在 Perfetto 追踪中开始标记
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, string);
#endif
#endif
}

/**
 * DebugMarker 析构函数
 * 
 * 结束调试标记，与构造函数配对使用。
 * 
 * 执行流程：
 * 1. 如果启用了 OpenGL 调试标记且驱动支持，调用 glPopGroupMarkerEXT
 * 2. 如果启用了后端标记，结束 Perfetto 追踪
 */
OpenGLDriver::DebugMarker::~DebugMarker() noexcept {
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_debug_marker
#if DEBUG_MARKER_LEVEL & DEBUG_MARKER_OPENGL
    // OpenGL 调试标记：弹出标记组
    if (UTILS_LIKELY(driver.getContext().ext.EXT_debug_marker)) {
        glPopGroupMarkerEXT();
    }
#endif
#endif

#if DEBUG_MARKER_LEVEL & DEBUG_MARKER_BACKEND
    // 后端标记：在 Perfetto 追踪中结束标记
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);
#endif
#endif
}

// ------------------------------------------------------------------------------------------------

/**
 * OpenGLDriver 构造函数
 * 
 * 初始化 OpenGL 后端驱动的所有核心组件：
 * 
 * @param platform OpenGL 平台接口，负责创建和管理 OpenGL 上下文
 * @param driverConfig 驱动配置参数
 *                    - handleArenaSize: Handle 分配器大小
 *                    - disableHandleUseAfterFreeCheck: 禁用句柄释放后使用检查
 *                    - disableHeapHandleTags: 禁用堆句柄标签
 * 
 * 初始化流程：
 * 1. 保存平台引用和配置
 * 2. 创建 OpenGLContext：管理 OpenGL 状态缓存，减少状态切换
 * 3. 创建 ShaderCompilerService：负责异步着色器编译
 * 4. 创建 HandleAllocator：管理所有 GPU 资源的句柄
 * 5. 初始化 PushConstantBundle：用于推送常量
 * 6. 预分配流相关容器：减少运行时分配
 * 7. 验证定时器查询扩展：确保性能查询功能可用
 * 8. 初始化着色器编译服务
 */
OpenGLDriver::OpenGLDriver(OpenGLPlatform* platform, const Platform::DriverConfig& driverConfig) noexcept
        : mPlatform(*platform),                    // 保存平台引用，用于创建上下文、交换缓冲区等
          mContext(mPlatform, driverConfig),      // 创建 OpenGL 上下文，管理状态缓存
          mShaderCompilerService(*this),          // 着色器编译服务，支持异步编译
          mHandleAllocator("Handles",             // Handle 分配器，管理所有 GPU 资源句柄
                  driverConfig.handleArenaSize,
                  driverConfig.disableHandleUseAfterFreeCheck,
                  driverConfig.disableHeapHandleTags),
          mDriverConfig(driverConfig),            // 保存配置
          mCurrentPushConstants(new(std::nothrow) PushConstantBundle{}) { // 推送常量包
    // 预分配流相关容器，减少运行时内存分配
    mTexturesWithStreamsAttached.reserve(8);
    mStreamsWithPendingAcquiredImage.reserve(8);

#ifndef NDEBUG
    LOG(INFO) << "OS version: " << mPlatform.getOSVersion();
#endif

    // 定时器查询在 GL 3.3 中是核心功能，否则需要 EXT_disjoint_timer_query 扩展
    // iOS 头文件不定义 GL_EXT_disjoint_timer_query，所以需要确保不会使用它
#if defined(BACKEND_OPENGL_VERSION_GL)
    assert_invariant(mContext.ext.EXT_disjoint_timer_query);
#endif

    // 初始化着色器编译服务
    mShaderCompilerService.init();
}

/**
 * OpenGLDriver 析构函数
 * 
 * 注意：此析构函数在主线程调用，不能调用 OpenGL API。
 * 实际的清理工作由 terminate() 方法完成，该方法在渲染线程调用。
 * 
 * 清理流程：
 * - terminate() 方法会等待 GPU 完成所有命令
 * - 终止着色器编译服务
 * - 执行所有 GPU 命令完成回调
 * - 执行所有帧完成回调
 * - 清理 OpenGL 上下文
 * - 终止平台
 */
OpenGLDriver::~OpenGLDriver() noexcept { // NOLINT(modernize-use-equals-default)
    // 此方法在主线程调用，不能调用 OpenGL API。
    // 实际的清理工作由 terminate() 方法完成。
}

/**
 * 获取命令分发器
 * 
 * 返回 OpenGLDriver 的 Dispatcher，包含所有 Driver API 方法的函数指针映射。
 * 
 * 实现说明：
 * - 使用 ConcreteDispatcher 模板生成默认的 Dispatcher
 * - 对于 OpenGL ES 2.0，需要特殊处理 draw2 方法（使用不同的实现）
 * - Dispatcher 的函数指针在编译时确定，运行时直接调用
 * 
 * @return Dispatcher 对象，包含所有方法的函数指针
 */
Dispatcher OpenGLDriver::getDispatcher() const noexcept {
    // 使用 ConcreteDispatcher 模板生成 Dispatcher
    // ConcreteDispatcher 会为每个 Driver API 方法生成对应的执行函数
    auto dispatcher = ConcreteDispatcher<OpenGLDriver>::make();
    
    // OpenGL ES 2.0 特殊处理：draw2 方法使用不同的实现
    // 因为 ES 2.0 不支持某些特性，需要使用兼容的实现
    if (mContext.isES2()) {
        dispatcher.draw2_ = +[](Driver& driver, CommandBase* base, intptr_t* next){
            using Cmd = COMMAND_TYPE(draw2);
            OpenGLDriver& concreteDriver = static_cast<OpenGLDriver&>(driver);
            // 使用 ES 2.0 特定的 draw2 实现
            Cmd::execute(&OpenGLDriver::draw2GLES2, concreteDriver, base, next);
        };
    }
    return dispatcher;
}

// ------------------------------------------------------------------------------------------------
// Driver interface concrete implementation
// ------------------------------------------------------------------------------------------------

/**
 * 终止 OpenGLDriver
 * 
 * 清理所有资源并终止驱动。此方法必须在渲染线程调用。
 * 
 * 执行流程：
 * 1. 等待 GPU 完成所有命令（glFinish）
 * 2. 终止着色器编译服务
 * 3. 执行所有 GPU 命令完成回调（如果支持）
 * 4. 执行所有帧完成回调（如果支持）
 * 5. 验证所有回调已执行（因为调用了 glFinish）
 * 6. 删除推送常量包
 * 7. 终止 OpenGL 上下文
 * 8. 终止平台
 * 
 * 注意：
 * - 此方法会阻塞直到 GPU 完成所有工作
 * - 所有回调都会在此方法中执行
 * - 调用此方法后，驱动不能再使用
 */
void OpenGLDriver::terminate() {
    // 等待 GPU 完成执行所有命令
    glFinish();

    // 终止着色器编译服务
    mShaderCompilerService.terminate();

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 确保执行所有 GPU 命令完成回调
    executeGpuCommandsCompleteOps();

    // 以及所有帧完成回调
    if (UTILS_UNLIKELY(!mFrameCompleteOps.empty())) {
        for (auto&& op: mFrameCompleteOps) {
            op();
        }
        mFrameCompleteOps.clear();
    }

    // 因为我们调用了 glFinish()，所有回调应该都已执行
    assert_invariant(mGpuCommandCompleteOps.empty());
#endif

    // 删除推送常量包
    delete mCurrentPushConstants;
    mCurrentPushConstants = nullptr;

    // 终止 OpenGL 上下文
    mContext.terminate();

    // 终止平台
    mPlatform.terminate();
}

/**
 * 获取着色器模型
 * 
 * 返回当前 OpenGL 上下文支持的着色器模型。
 * 
 * @return ShaderModel 枚举值
 *         - ES 2.0: ShaderModel::GL_ES_20
 *         - ES 3.0+: ShaderModel::GL_ES_30
 *         - GL 4.1+: ShaderModel::GL_CORE_41
 */
ShaderModel OpenGLDriver::getShaderModel() const noexcept {
    return mContext.getShaderModel();
}

/**
 * 获取支持的着色器语言
 * 
 * 返回当前 OpenGL 上下文支持的着色器语言列表。
 * 
 * @param preferredLanguage 首选语言（当前未使用）
 * @return 支持的着色器语言列表
 *         - ES 2.0: { ShaderLanguage::ESSL1 }
 *         - ES 3.0+/GL 4.1+: { ShaderLanguage::ESSL3 }
 */
utils::FixedCapacityVector<ShaderLanguage> OpenGLDriver::getShaderLanguages(
        ShaderLanguage /*preferredLanguage*/) const noexcept {
    return { mContext.isES2() ? ShaderLanguage::ESSL1 : ShaderLanguage::ESSL3 };
}

// ------------------------------------------------------------------------------------------------
// 更改和跟踪 GL 状态
// ------------------------------------------------------------------------------------------------

/**
 * 重置 OpenGL 状态
 * 
 * 将所有 OpenGL 状态重置为默认值。
 * 这通常用于上下文切换或调试。
 * 
 * @param 未使用的参数（保持接口一致性）
 */
void OpenGLDriver::resetState(int) {
    mContext.resetState();
}

/**
 * 绑定采样器对象
 * 
 * 将采样器对象绑定到指定的纹理单元。
 * 采样器对象定义了纹理采样参数（过滤、包装等）。
 * 
 * @param unit 纹理单元索引
 * @param sampler 采样器对象 ID（0 表示解绑）
 */
void OpenGLDriver::bindSampler(GLuint const unit, GLuint const sampler) noexcept {
    mContext.bindSampler(unit, sampler);
}

/**
 * 设置推送常量
 * 
 * 设置着色器中的推送常量值。推送常量是频繁更新的小数据，
 * 直接通过 uniform 传递，而不是通过 uniform buffer。
 * 
 * @param stage 着色器阶段（VERTEX 或 FRAGMENT）
 * @param index 常量索引（在推送常量数组中的位置）
 * @param value 常量值（bool/float/int）
 * 
 * 执行流程：
 * 1. 验证着色器阶段有效
 * 2. 获取对应阶段的推送常量数组
 * 3. 验证索引有效
 * 4. 获取常量的 location 和 type
 * 5. 如果 location < 0，说明常量未在着色器中找到，直接返回
 * 6. 根据值类型设置 uniform：
 *    - bool: glUniform1i (0 或 1)
 *    - float: glUniform1f
 *    - int: glUniform1i
 * 
 * 注意：
 * - 推送常量必须在着色器编译时确定 location
 * - 如果常量未在着色器中找到，不会报错，只是忽略
 */
void OpenGLDriver::setPushConstant(ShaderStage const stage, uint8_t const index,
        PushConstantVariant const value) {
    assert_invariant(stage == ShaderStage::VERTEX || stage == ShaderStage::FRAGMENT);

#if FILAMENT_ENABLE_MATDBG
    // 材质调试模式下，如果程序无效，直接返回
    if (UTILS_UNLIKELY(!mValidProgram)) {
        return;
    }
#endif

    // 获取对应阶段的推送常量数组
    Slice<const std::pair<GLint, ConstantType>> constants;
    if (stage == ShaderStage::VERTEX) {
        constants = mCurrentPushConstants->vertexConstants;
    } else if (stage == ShaderStage::FRAGMENT) {
        constants = mCurrentPushConstants->fragmentConstants;
    }

    assert_invariant(index < constants.size());
    auto const& [location, type] = constants[index];

    // 如果推送常量未在着色器中找到，直接返回（不报错）
    if (location < 0) {
        return;
    }

    // 根据值类型设置 uniform
    if (std::holds_alternative<bool>(value)) {
        assert_invariant(type == ConstantType::BOOL);
        bool const bval = std::get<bool>(value);
        glUniform1i(location, bval ? 1 : 0);
    } else if (std::holds_alternative<float>(value)) {
        assert_invariant(type == ConstantType::FLOAT);
        float const fval = std::get<float>(value);
        glUniform1f(location, fval);
    } else {
        assert_invariant(type == ConstantType::INT);
        int const ival = std::get<int>(value);
        glUniform1i(location, ival);
    }
}

/**
 * 绑定纹理
 * 
 * 将纹理绑定到指定的纹理单元。
 * 
 * @param unit 纹理单元索引
 * @param t 纹理对象指针（不能为 nullptr）
 * 
 * 执行流程：
 * 1. 验证纹理指针非空
 * 2. 通过 OpenGLContext 绑定纹理（使用状态缓存）
 */
void OpenGLDriver::bindTexture(GLuint const unit, GLTexture const* t) noexcept {
    assert_invariant(t != nullptr);
    mContext.bindTexture(unit, t->gl.target, t->gl.id, t->gl.external);
}

/**
 * 使用着色器程序
 * 
 * 绑定着色器程序到当前 OpenGL 上下文。如果程序尚未编译/链接，会先编译/链接。
 * 
 * @param p 着色器程序指针
 * @return 成功返回 true，失败返回 false
 * 
 * 执行流程：
 * 1. 如果程序已绑定，直接返回成功
 * 2. 否则，编译/链接程序（如果需要）并调用 glUseProgram
 * 3. 如果成功：
 *    - 标记所有描述符集为无效（需要重新绑定）
 *    - 保存当前绑定的程序
 * 4. ES 2.0 特殊处理：设置输出色彩空间（linear 或 rec709）
 * 
 * 性能优化：
 * - 只在程序改变时重新绑定
 * - 使用程序状态缓存，避免重复绑定
 * 
 * 注意：
 * - 程序改变时，所有描述符集都需要重新绑定
 * - ES 2.0 不支持 sRGB 交换链时，需要手动设置色彩空间
 */
bool OpenGLDriver::useProgram(OpenGLProgram* p) noexcept {
    bool success = true;
    if (mBoundProgram != p) {
        // 如果需要，编译/链接程序并调用 glUseProgram
        success = p->use(this, mContext);
        assert_invariant(success == p->isValid());
        if (success) {
            // TODO: 如果程序能告诉我们哪些描述符集绑定实际改变了，可以进一步优化
            //       实际上，set 0 或 1 可能不会经常改变
            decltype(mInvalidDescriptorSetBindings) changed;
            changed.setValue((1 << MAX_DESCRIPTOR_SET_COUNT) - 1);
            mInvalidDescriptorSetBindings |= changed;

            mBoundProgram = p;
        }
    }

    // ES 2.0 特殊处理：设置输出色彩空间（linear 或 rec709）
    // 这仅在 mPlatform.isSRGBSwapChainSupported() 为 false 时相关（无需检查）
    if (UTILS_UNLIKELY(mContext.isES2() && success)) {
        p->setRec709ColorSpace(mRec709OutputColorspace);
    }

    return success;
}


/**
 * 设置光栅化状态
 * 
 * 配置 OpenGL 的光栅化相关状态，包括：
 * - 面剔除（Culling）
 * - 混合（Blending）
 * - 深度测试（Depth Test）
 * - 颜色/深度写入掩码
 * - Alpha to Coverage
 * - 深度裁剪（Depth Clamp）
 * 
 * @param rs 光栅化状态
 *          - culling: 面剔除模式（NONE/FRONT/BACK）
 *          - inverseFrontFaces: 是否反转正面
 *          - hasBlending: 是否启用混合
 *          - blendEquationRGB/Alpha: 混合方程
 *          - blendFunctionSrc/Dst: 混合函数
 *          - depthFunc: 深度测试函数
 *          - depthWrite: 是否写入深度
 *          - colorWrite: 是否写入颜色
 *          - alphaToCoverage: 是否启用 Alpha to Coverage
 *          - depthClamp: 是否启用深度裁剪
 * 
 * 执行流程：
 * 1. 更新渲染通道写入标志（用于 endRenderPass 优化）
 * 2. 设置面剔除状态
 * 3. 设置正面方向（顺时针/逆时针）
 * 4. 设置混合状态（如果启用）
 * 5. 设置深度测试状态
 * 6. 设置颜色写入掩码
 * 7. 设置 Alpha to Coverage（MSAA 相关）
 * 8. 设置深度裁剪（如果支持）
 * 
 * 性能优化：
 * - 使用 OpenGLContext 的状态缓存，避免重复设置相同状态
 * - 只在状态改变时调用 OpenGL API
 */
void OpenGLDriver::setRasterState(RasterState const rs) noexcept {
    auto& gl = mContext;

    // 更新渲染通道写入标志（用于 endRenderPass 优化）
    mRenderPassColorWrite |= rs.colorWrite;
    mRenderPassDepthWrite |= rs.depthWrite;

    // 面剔除状态
    if (rs.culling == CullingMode::NONE) {
        gl.disable(GL_CULL_FACE);
    } else {
        gl.enable(GL_CULL_FACE);
        gl.cullFace(getCullingMode(rs.culling));  // FRONT/BACK
    }

    // 正面方向（顺时针/逆时针）
    gl.frontFace(rs.inverseFrontFaces ? GL_CW : GL_CCW);

    // 混合状态
    if (!rs.hasBlending()) {
        gl.disable(GL_BLEND);
    } else {
        gl.enable(GL_BLEND);
        // 设置混合方程（RGB 和 Alpha 可以不同）
        gl.blendEquation(
                getBlendEquationMode(rs.blendEquationRGB),
                getBlendEquationMode(rs.blendEquationAlpha));

        // 设置混合函数（源和目标，RGB 和 Alpha 可以不同）
        gl.blendFunction(
                getBlendFunctionMode(rs.blendFunctionSrcRGB),
                getBlendFunctionMode(rs.blendFunctionSrcAlpha),
                getBlendFunctionMode(rs.blendFunctionDstRGB),
                getBlendFunctionMode(rs.blendFunctionDstAlpha));
    }

    // 深度测试
    // 如果深度函数是 A（总是通过）且不写入深度，则禁用深度测试
    if (rs.depthFunc == RasterState::DepthFunc::A && !rs.depthWrite) {
        gl.disable(GL_DEPTH_TEST);
    } else {
        gl.enable(GL_DEPTH_TEST);
        gl.depthFunc(getDepthFunc(rs.depthFunc));  // LESS/LEQUAL/GREATER 等
        gl.depthMask(GLboolean(rs.depthWrite));  // 深度写入掩码
    }

    // 颜色写入掩码
    gl.colorMask(GLboolean(rs.colorWrite));

    // Alpha to Coverage（MSAA 相关，用于透明物体的抗锯齿）
    if (rs.alphaToCoverage) {
        gl.enable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    } else {
        gl.disable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    // 深度裁剪（如果支持，将深度值限制在 [0, 1] 范围内）
    if (gl.ext.EXT_depth_clamp) {
        if (rs.depthClamp) {
            gl.enable(GL_DEPTH_CLAMP);
        } else {
            gl.disable(GL_DEPTH_CLAMP);
        }
    }
}

void OpenGLDriver::setStencilState(StencilState const ss) noexcept {
    auto& gl = mContext;

    mRenderPassStencilWrite |= ss.stencilWrite;

    // stencil test / operation
    // GL_STENCIL_TEST must be enabled if we're testing OR writing to the stencil buffer.
    if (UTILS_LIKELY(
            ss.front.stencilFunc == StencilState::StencilFunction::A &&
            ss.back.stencilFunc == StencilState::StencilFunction::A &&
            ss.front.stencilOpDepthFail == StencilOperation::KEEP &&
            ss.back.stencilOpDepthFail == StencilOperation::KEEP &&
            ss.front.stencilOpStencilFail == StencilOperation::KEEP &&
            ss.back.stencilOpStencilFail == StencilOperation::KEEP &&
            ss.front.stencilOpDepthStencilPass == StencilOperation::KEEP &&
            ss.back.stencilOpDepthStencilPass == StencilOperation::KEEP)) {
        // that's equivalent to having the stencil test disabled
        gl.disable(GL_STENCIL_TEST);
    } else {
        gl.enable(GL_STENCIL_TEST);
    }

    // glStencilFuncSeparate() also sets the reference value, which may be used depending
    // on the stencilOp, so we always need to call glStencilFuncSeparate().
    gl.stencilFuncSeparate(
            getStencilFunc(ss.front.stencilFunc), ss.front.ref, ss.front.readMask,
            getStencilFunc(ss.back.stencilFunc), ss.back.ref, ss.back.readMask);

    if (UTILS_LIKELY(!ss.stencilWrite)) {
        gl.stencilMaskSeparate(0x00, 0x00);
    } else {
        // Stencil ops are only relevant when stencil write is enabled
        gl.stencilOpSeparate(
                getStencilOp(ss.front.stencilOpStencilFail),
                getStencilOp(ss.front.stencilOpDepthFail),
                getStencilOp(ss.front.stencilOpDepthStencilPass),
                getStencilOp(ss.back.stencilOpStencilFail),
                getStencilOp(ss.back.stencilOpDepthFail),
                getStencilOp(ss.back.stencilOpDepthStencilPass));
        gl.stencilMaskSeparate(ss.front.writeMask, ss.back.writeMask);
    }
}

// ------------------------------------------------------------------------------------------------
// 创建驱动对象
// ------------------------------------------------------------------------------------------------

/**
 * 资源创建方法（S 后缀 = Synchronous，同步创建句柄）
 * 
 * 这些方法在主线程调用，只分配句柄，不执行实际的 OpenGL 操作。
 * 实际的资源创建在对应的 R 方法（Render thread）中完成。
 * 
 * 设计说明：
 * - S 方法：在主线程调用，分配句柄（快速，无 OpenGL 调用）
 * - R 方法：在渲染线程调用，执行实际的 OpenGL 操作（可能较慢）
 * - 这种设计实现了主线程和渲染线程的解耦，提高性能
 */

/**
 * 创建顶点缓冲区信息句柄
 * 顶点缓冲区信息定义了顶点属性的布局（位置、法线、UV 等）
 */
Handle<HwVertexBufferInfo> OpenGLDriver::createVertexBufferInfoS() noexcept {
    return initHandle<GLVertexBufferInfo>();
}

/**
 * 创建顶点缓冲区句柄
 * 顶点缓冲区存储实际的顶点数据
 */
Handle<HwVertexBuffer> OpenGLDriver::createVertexBufferS() noexcept {
    return initHandle<GLVertexBuffer>();
}

/**
 * 创建索引缓冲区句柄
 * 索引缓冲区存储顶点索引，用于索引绘制
 */
Handle<HwIndexBuffer> OpenGLDriver::createIndexBufferS() noexcept {
    return initHandle<GLIndexBuffer>();
}

/**
 * 创建缓冲区对象句柄
 * 缓冲区对象用于存储 uniform 数据、SSBO 等
 */
Handle<HwBufferObject> OpenGLDriver::createBufferObjectS() noexcept {
    return initHandle<GLBufferObject>();
}

/**
 * 创建渲染图元句柄
 * 渲染图元将顶点缓冲区和索引缓冲区组合在一起
 */
Handle<HwRenderPrimitive> OpenGLDriver::createRenderPrimitiveS() noexcept {
    return initHandle<GLRenderPrimitive>();
}

/**
 * 创建着色器程序句柄
 * 着色器程序包含顶点着色器和片段着色器
 */
Handle<HwProgram> OpenGLDriver::createProgramS() noexcept {
    return initHandle<OpenGLProgram>();
}

/**
 * 创建纹理句柄（标准纹理）
 */
Handle<HwTexture> OpenGLDriver::createTextureS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建纹理视图句柄
 * 纹理视图是现有纹理的视图，可以有不同的格式或 Mipmap 范围
 */
Handle<HwTexture> OpenGLDriver::createTextureViewS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建带 Swizzle 的纹理视图句柄
 * Swizzle 允许重新映射纹理通道（如 R->A, G->R 等）
 */
Handle<HwTexture> OpenGLDriver::createTextureViewSwizzleS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建外部图像纹理句柄（2D）
 * 用于从外部源（如相机预览）创建纹理
 */
Handle<HwTexture> OpenGLDriver::createTextureExternalImage2S() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建外部图像纹理句柄
 * 用于从外部源创建纹理
 */
Handle<HwTexture> OpenGLDriver::createTextureExternalImageS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建外部图像平面纹理句柄
 * 用于多平面图像（如 YUV 格式）
 */
Handle<HwTexture> OpenGLDriver::createTextureExternalImagePlaneS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 导入纹理句柄
 * 用于从外部 OpenGL 纹理创建 Filament 纹理
 */
Handle<HwTexture> OpenGLDriver::importTextureS() noexcept {
    return initHandle<GLTexture>();
}

/**
 * 创建默认渲染目标句柄
 * 默认渲染目标指向交换链的后缓冲区
 */
Handle<HwRenderTarget> OpenGLDriver::createDefaultRenderTargetS() noexcept {
    return initHandle<GLRenderTarget>();
}

/**
 * 创建渲染目标句柄
 * 渲染目标是离屏渲染的目标（FBO）
 */
Handle<HwRenderTarget> OpenGLDriver::createRenderTargetS() noexcept {
    return initHandle<GLRenderTarget>();
}

/**
 * 创建栅栏句柄
 * 栅栏用于 CPU-GPU 同步
 */
Handle<HwFence> OpenGLDriver::createFenceS() noexcept {
    return initHandle<GLFence>();
}

/**
 * 创建同步对象句柄
 * 同步对象用于更细粒度的 GPU 同步
 */
Handle<HwSync> OpenGLDriver::createSyncS() noexcept {
    return initHandle<GLSyncFence>();
}

/**
 * 创建交换链句柄
 * 交换链管理前后缓冲区的交换
 */
Handle<HwSwapChain> OpenGLDriver::createSwapChainS() noexcept {
    return initHandle<GLSwapChain>();
}

/**
 * 创建无头交换链句柄
 * 无头交换链用于离屏渲染（不显示到屏幕）
 */
Handle<HwSwapChain> OpenGLDriver::createSwapChainHeadlessS() noexcept {
    return initHandle<GLSwapChain>();
}

/**
 * 创建定时器查询句柄
 * 定时器查询用于测量 GPU 执行时间
 */
Handle<HwTimerQuery> OpenGLDriver::createTimerQueryS() noexcept {
    return initHandle<GLTimerQuery>();
}

/**
 * 创建描述符集布局句柄
 * 描述符集布局定义了描述符集的绑定结构
 */
Handle<HwDescriptorSetLayout> OpenGLDriver::createDescriptorSetLayoutS() noexcept {
    return initHandle<GLDescriptorSetLayout>();
}

/**
 * 创建描述符集句柄
 * 描述符集包含纹理、缓冲区等资源的绑定
 */
Handle<HwDescriptorSet> OpenGLDriver::createDescriptorSetS() noexcept {
    return initHandle<GLDescriptorSet>();
}

/**
 * 创建内存映射缓冲区句柄
 * 内存映射缓冲区允许 CPU 直接访问 GPU 缓冲区
 */
MemoryMappedBufferHandle OpenGLDriver::mapBufferS() noexcept {
    return initHandle<GLMemoryMappedBuffer>();
}

/**
 * 创建顶点缓冲区信息（渲染线程）
 * 
 * 在渲染线程中创建顶点缓冲区信息对象。
 * 顶点缓冲区信息定义了顶点属性的布局。
 * 
 * @param vbih 顶点缓冲区信息句柄（已在主线程分配）
 * @param bufferCount 缓冲区数量（顶点数据可以分布在多个缓冲区中）
 * @param attributeCount 属性数量（位置、法线、UV 等）
 * @param attributes 属性数组，定义每个属性的格式和位置
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 在句柄位置构造 GLVertexBufferInfo 对象
 * 2. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 只创建对象，不执行 OpenGL 操作
 */
void OpenGLDriver::createVertexBufferInfoR(
        Handle<HwVertexBufferInfo> vbih,
        uint8_t bufferCount,
        uint8_t attributeCount,
        AttributeArray attributes,
        ImmutableCString&& tag) {
    DEBUG_MARKER()
    construct<GLVertexBufferInfo>(vbih, bufferCount, attributeCount, attributes);
    mHandleAllocator.associateTagToHandle(vbih.getId(), std::move(tag));
}

/**
 * 创建顶点缓冲区（渲染线程）
 * 
 * 在渲染线程中创建顶点缓冲区对象。
 * 顶点缓冲区存储实际的顶点数据，但不分配 OpenGL 缓冲区（缓冲区对象在 setVertexBufferObject 时创建）。
 * 
 * @param vbh 顶点缓冲区句柄（已在主线程分配）
 * @param vertexCount 顶点数量
 * @param vbih 顶点缓冲区信息句柄（定义顶点属性布局）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 在句柄位置构造 GLVertexBuffer 对象
 * 2. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 只创建对象，不分配 OpenGL 缓冲区
 * - 实际的缓冲区对象通过 setVertexBufferObject 设置
 */
void OpenGLDriver::createVertexBufferR(
        Handle<HwVertexBuffer> vbh,
        uint32_t vertexCount,
        Handle<HwVertexBufferInfo> vbih,
        ImmutableCString&& tag) {
    DEBUG_MARKER()
    construct<GLVertexBuffer>(vbh, vertexCount, vbih);
    mHandleAllocator.associateTagToHandle(vbh.getId(), std::move(tag));
}

/**
 * 创建索引缓冲区（渲染线程）
 * 
 * 在渲染线程中创建索引缓冲区对象并分配 OpenGL 缓冲区。
 * 
 * @param ibh 索引缓冲区句柄（已在主线程分配）
 * @param elementType 索引元素类型（USHORT 或 UINT）
 * @param indexCount 索引数量
 * @param usage 缓冲区使用方式（STATIC/DYNAMIC/STREAM）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 计算元素大小（USHORT=2, UINT=4）
 * 2. 构造 GLIndexBuffer 对象
 * 3. 生成 OpenGL 缓冲区对象
 * 4. 绑定索引缓冲区（解绑 VAO 以避免影响）
 * 5. 分配缓冲区存储（不初始化数据，后续通过 updateIndexBuffer 填充）
 * 6. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 缓冲区初始为空，数据通过 updateIndexBuffer 上传
 * - 索引类型影响绘制时的偏移计算
 */
void OpenGLDriver::createIndexBufferR(
        Handle<HwIndexBuffer> ibh,
        ElementType const elementType,
        uint32_t indexCount,
        BufferUsage const usage,
        ImmutableCString&& tag) {
    DEBUG_MARKER()

    auto& gl = mContext;
    // 计算元素大小（USHORT=2, UINT=4）
    uint8_t const elementSize = static_cast<uint8_t>(getElementTypeSize(elementType));
    GLIndexBuffer* ib = construct<GLIndexBuffer>(ibh, elementSize, indexCount);
    
    // 生成 OpenGL 缓冲区对象
    glGenBuffers(1, &ib->gl.buffer);
    GLsizeiptr const size = elementSize * GLsizeiptr(indexCount);
    
    // 解绑 VAO 以避免影响，然后绑定索引缓冲区
    gl.bindVertexArray(nullptr);
    gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->gl.buffer);
    
    // 分配缓冲区存储（不初始化数据，后续通过 updateIndexBuffer 填充）
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, nullptr, getBufferUsage(usage));
    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(ibh.getId(), std::move(tag));
}

/**
 * 创建缓冲区对象（渲染线程）
 * 
 * 在渲染线程中创建缓冲区对象并分配 OpenGL 缓冲区。
 * 缓冲区对象用于存储 uniform 数据、SSBO 等。
 * 
 * @param boh 缓冲区对象句柄（已在主线程分配）
 * @param byteCount 缓冲区大小（字节）
 * @param bindingType 绑定类型（VERTEX/UNIFORM/STORAGE 等）
 * @param usage 缓冲区使用方式（STATIC/DYNAMIC/STREAM）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 验证缓冲区大小 > 0
 * 2. 如果是顶点缓冲区，解绑 VAO 以避免影响
 * 3. 构造 GLBufferObject 对象
 * 4. ES 2.0 特殊处理（uniform 缓冲区模拟）：
 *    - 使用 CPU 内存模拟（ES 2.0 不支持 uniform buffer）
 *    - 分配系统内存并初始化为 0
 * 5. 其他情况：
 *    - 获取绑定类型（GL_ARRAY_BUFFER/GL_UNIFORM_BUFFER 等）
 *    - 生成 OpenGL 缓冲区对象
 *    - 绑定缓冲区
 *    - 分配缓冲区存储（不初始化数据）
 * 6. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - ES 2.0 的 uniform 缓冲区使用 CPU 内存模拟
 * - 缓冲区初始为空，数据通过 updateBufferObject 上传
 */
void OpenGLDriver::createBufferObjectR(Handle<HwBufferObject> boh, uint32_t byteCount,
        BufferObjectBinding bindingType, BufferUsage usage, ImmutableCString&& tag) {
    DEBUG_MARKER()
    assert_invariant(byteCount > 0);

    auto& gl = mContext;
    // 如果是顶点缓冲区，解绑 VAO 以避免影响
    if (bindingType == BufferObjectBinding::VERTEX) {
        gl.bindVertexArray(nullptr);
    }

    GLBufferObject* bo = construct<GLBufferObject>(boh, byteCount, bindingType, usage);
    
    // ES 2.0 特殊处理：uniform 缓冲区使用 CPU 内存模拟
    if (UTILS_UNLIKELY(bindingType == BufferObjectBinding::UNIFORM && gl.isES2())) {
        bo->gl.id = ++mLastAssignedEmulatedUboId;
        bo->gl.buffer = malloc(byteCount);
        memset(bo->gl.buffer, 0, byteCount);
    } else {
        // 标准 OpenGL 缓冲区创建
        bo->gl.binding = getBufferBindingType(bindingType);
        glGenBuffers(1, &bo->gl.id);
        gl.bindBuffer(bo->gl.binding, bo->gl.id);
        glBufferData(bo->gl.binding, byteCount, nullptr, getBufferUsage(usage));
    }

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(boh.getId(), std::move(tag));
}

/**
 * 创建渲染图元（渲染线程）
 * 
 * 在渲染线程中创建渲染图元对象。
 * 渲染图元将顶点缓冲区和索引缓冲区组合在一起，并创建 VAO（顶点数组对象）。
 * 
 * @param rph 渲染图元句柄（已在主线程分配）
 * @param vbh 顶点缓冲区句柄
 * @param ibh 索引缓冲区句柄
 * @param pt 图元类型（TRIANGLES/LINES/POINTS 等）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 获取索引缓冲区和顶点缓冲区对象
 * 2. 验证索引元素大小（必须是 2 或 4 字节）
 * 3. 设置索引偏移量（用于绘制时的字节偏移计算）：
 *    - 32位索引：indicesShift = 2（偏移 = indexOffset * 4）
 *    - 16位索引：indicesShift = 1（偏移 = indexOffset * 2）
 * 4. 设置索引类型（GL_UNSIGNED_INT 或 GL_UNSIGNED_SHORT）
 * 5. 保存顶点缓冲区句柄和图元类型
 * 6. 为当前上下文生成 VAO 名称
 * 7. 记录状态版本（用于 VAO 缓存）
 * 8. 绑定 VAO（实际创建 VAO）
 * 9. 将索引缓冲区记录到 VAO 中
 * 10. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - VAO 包含索引缓冲区绑定，但顶点缓冲区绑定在 draw/bindRenderPrimitive 时更新
 * - 这是因为此时顶点缓冲区可能还没有所有缓冲区对象设置完成
 * - VAO 在多个上下文间共享时需要为每个上下文创建
 */
void OpenGLDriver::createRenderPrimitiveR(Handle<HwRenderPrimitive> rph,
        Handle<HwVertexBuffer> vbh, Handle<HwIndexBuffer> ibh,
        PrimitiveType const pt, ImmutableCString&& tag) {
    DEBUG_MARKER()

    auto& gl = mContext;

    GLIndexBuffer const* const ib = handle_cast<const GLIndexBuffer*>(ibh);
    assert_invariant(ib->elementSize == 2 || ib->elementSize == 4);

    GLVertexBuffer const* const vb = handle_cast<GLVertexBuffer*>(vbh);
    GLRenderPrimitive* const rp = handle_cast<GLRenderPrimitive*>(rph);
    
    // 设置索引偏移量（用于绘制时的字节偏移计算）
    rp->gl.indicesShift = (ib->elementSize == 4u) ? 2u : 1u;
    rp->gl.indicesType  = (ib->elementSize == 4u) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    rp->gl.vertexBufferWithObjects = vbh;
    rp->type = pt;
    rp->vbih = vb->vbih;

    // 为当前上下文创建 VAO 名称
    gl.procs.genVertexArrays(1, &rp->gl.vao[gl.contextIndex]);

    // 这表示我们的名称是最新的
    rp->gl.nameVersion = gl.state.age;

    // 绑定 VAO 会实际创建它
    gl.bindVertexArray(&rp->gl);

    // 注意：我们此时不更新 VAO 中的顶点缓冲区绑定，稍后在 draw() 或 bindRenderPrimitive() 中更新
    // 因为此时 HwVertexBuffer 可能还没有所有缓冲区设置完成

    // 将索引缓冲区记录到当前绑定的 VAO 中
    gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->gl.buffer);

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(rph.getId(), std::move(tag));
}

/**
 * 创建着色器程序（渲染线程）
 * 
 * 在渲染线程中创建着色器程序对象。
 * 着色器程序包含顶点着色器和片段着色器，但编译和链接是延迟的（在 useProgram 时）。
 * 
 * @param ph 着色器程序句柄（已在主线程分配）
 * @param program 程序对象（包含着色器源码）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 在句柄位置构造 OpenGLProgram 对象
 * 2. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 着色器的编译和链接是延迟的，在第一次 useProgram 时执行
 * - 这样可以避免阻塞，支持异步编译
 */
void OpenGLDriver::createProgramR(Handle<HwProgram> ph, Program&& program, ImmutableCString&& tag) {
    DEBUG_MARKER()

    construct<OpenGLProgram>(ph, *this, std::move(program));
    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(ph.getId(), std::move(tag));
}

/**
 * 分配纹理存储
 * 
 * 为纹理分配不可变的存储空间。使用 glTexStorage* 而不是 glTexImage*，
 * 这样可以获得更好的性能和驱动优化。
 * 
 * @param t 纹理对象指针
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param depth 纹理深度（3D/Array 纹理）
 * @param useProtectedMemory 是否使用保护内存（用于 DRM 内容保护）
 * 
 * 执行流程：
 * 1. 绑定纹理到虚拟纹理单元（用于设置参数）
 * 2. 如果使用保护内存，设置保护标志（需要 EXT_protected_textures 扩展）
 * 3. 根据纹理目标类型分配存储：
 *    - GL_TEXTURE_2D/CUBE_MAP: glTexStorage2D
 *    - GL_TEXTURE_3D/2D_ARRAY: glTexStorage3D
 *    - GL_TEXTURE_CUBE_MAP_ARRAY: glTexStorage3D（深度 * 6）
 *    - GL_TEXTURE_2D_MULTISAMPLE: glTexStorage2DMultisample
 *    - ES 2.0: 使用 glTexImage2D（不支持 glTexStorage）
 * 4. 更新纹理尺寸
 * 
 * 性能优化：
 * - 使用 glTexStorage* 而不是 glTexImage*（不可变存储，性能更好）
 * - ES 2.0 回退到 glTexImage*（不支持 glTexStorage）
 * 
 * 注意：
 * - 此方法可以用于重新分配纹理到新尺寸
 * - 保护内存仅在某些平台支持（如 Android 的 DRM 内容保护）
 */
UTILS_NOINLINE
void OpenGLDriver::textureStorage(GLTexture* t,
        uint32_t const width, uint32_t const height, uint32_t const depth, bool useProtectedMemory) noexcept {

    auto& gl = mContext;

    // 绑定纹理到虚拟纹理单元（用于设置参数）
    bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
    gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);

#ifdef GL_EXT_protected_textures
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 如果使用保护内存，设置保护标志（需要 EXT_protected_textures 扩展）
    if (UTILS_UNLIKELY(useProtectedMemory)) {
        assert_invariant(gl.ext.EXT_protected_textures);
        glTexParameteri(t->gl.target, GL_TEXTURE_PROTECTED_EXT, 1);
    }
#endif
#endif

    // 根据纹理目标类型分配存储
    switch (t->gl.target) {
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            if (UTILS_LIKELY(!gl.isES2())) {
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
                glTexStorage2D(t->gl.target, GLsizei(t->levels), t->gl.internalFormat,
                        GLsizei(width), GLsizei(height));
#endif
            }
#ifdef BACKEND_OPENGL_VERSION_GLES
            else {
                // FIXME: handle compressed texture format
                auto [format, type] = textureFormatToFormatAndType(t->format);
                assert_invariant(format != GL_NONE && type != GL_NONE);
                for (GLint level = 0 ; level < t->levels ; level++ ) {
                    if (t->gl.target == GL_TEXTURE_CUBE_MAP) {
                        for (GLint face = 0 ; face < 6 ; face++) {
                            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                                    level, GLint(t->gl.internalFormat),
                                    std::max(GLsizei(1), GLsizei(width >> level)),
                                    std::max(GLsizei(1), GLsizei(height >> level)),
                                    0, format, type, nullptr);
                        }
                    } else {
                        glTexImage2D(t->gl.target, level, GLint(t->gl.internalFormat),
                                std::max(GLsizei(1), GLsizei(width >> level)),
                                std::max(GLsizei(1), GLsizei(height >> level)),
                                0, format, type, nullptr);
                    }
                }
            }
#endif
            break;
        case GL_TEXTURE_3D:
        case GL_TEXTURE_2D_ARRAY: {
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            glTexStorage3D(t->gl.target, GLsizei(t->levels), t->gl.internalFormat,
                    GLsizei(width), GLsizei(height), GLsizei(depth));
#endif
            break;
        }
        case GL_TEXTURE_CUBE_MAP_ARRAY: {
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            glTexStorage3D(t->gl.target, GLsizei(t->levels), t->gl.internalFormat,
                    GLsizei(width), GLsizei(height), GLsizei(depth) * 6);
#endif
            break;
        }
#ifdef BACKEND_OPENGL_LEVEL_GLES31
        case GL_TEXTURE_2D_MULTISAMPLE:
            if constexpr (TEXTURE_2D_MULTISAMPLE_SUPPORTED) {
                // NOTE: if there is a mix of texture and renderbuffers, "fixed_sample_locations" must be true
                // NOTE: what's the benefit of setting "fixed_sample_locations" to false?

                if (mContext.isAtLeastGL<4, 3>() || mContext.isAtLeastGLES<3, 1>()) {
                    // only supported from GL 4.3 and GLES 3.1 headers
                    glTexStorage2DMultisample(t->gl.target, t->samples, t->gl.internalFormat,
                            GLsizei(width), GLsizei(height), GL_TRUE);
                }
#ifdef BACKEND_OPENGL_VERSION_GL
                else {
                    // only supported in GL (GL4.1 doesn't support glTexStorage2DMultisample)
                    glTexImage2DMultisample(t->gl.target, t->samples, t->gl.internalFormat,
                            GLsizei(width), GLsizei(height), GL_TRUE);
                }
#endif
            } else {
                PANIC_LOG("GL_TEXTURE_2D_MULTISAMPLE is not supported");
            }
            break;
#endif // BACKEND_OPENGL_LEVEL_GLES31
        default: // cannot happen
            break;
    }

    // textureStorage can be used to reallocate the texture at a new size
    t->width = width;
    t->height = height;
    t->depth = depth;
}

/**
 * 创建纹理资源
 * 
 * 创建 OpenGL 纹理或渲染缓冲区（Renderbuffer）。根据使用场景选择：
 * - 纹理（Texture）：可采样、可上传、有 Mipmap、非 2D 等
 * - 渲染缓冲区（Renderbuffer）：仅用于渲染目标，性能更好
 * 
 * @param th 纹理句柄
 * @param target 采样器类型（2D/3D/Cube/Array/External 等）
 * @param levels Mipmap 级别数量（1 = 无 Mipmap）
 * @param format 纹理格式（RGBA8/RGB8/DEPTH24 等）
 * @param samples 多重采样数量（1 = 无 MSAA）
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param depth 纹理深度（3D/Array 纹理）
 * @param usage 使用标志（SAMPLEABLE/RENDERABLE/UPLOADABLE/PROTECTED 等）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 获取内部格式（GLenum）
 * 2. 根据使用场景决定使用纹理还是渲染缓冲区：
 *    - PROTECTED：必须使用纹理（渲染缓冲区不支持）
 *    - UPLOADABLE：必须使用纹理（需要上传数据）
 *    - 非 2D：必须使用纹理（渲染缓冲区只能是 2D）
 *    - 有 Mipmap：必须使用纹理（渲染缓冲区不支持 Mipmap）
 *    - 其他：可以使用渲染缓冲区（性能更好）
 * 3. 限制采样数量（不超过驱动支持的最大值）
 * 4. 构造 GLTexture 对象
 * 5. 如果是纹理：
 *    - ES 2.0 特殊处理（格式必须匹配）
 *    - 外部纹理特殊处理（相机预览等）
 *    - 创建 OpenGL 纹理对象
 *    - 分配纹理存储（glTexStorage*）
 * 6. 如果是渲染缓冲区：
 *    - 创建渲染缓冲区对象
 *    - 分配渲染缓冲区存储（glRenderbufferStorage*）
 * 
 * 性能优化：
 * - 使用 glTexStorage* 而不是 glTexImage*（不可变存储，性能更好）
 * - 对于纯渲染目标，使用渲染缓冲区（性能更好）
 */
void OpenGLDriver::createTextureR(Handle<HwTexture> th, SamplerType target, uint8_t levels,
        TextureFormat format, uint8_t samples, uint32_t width, uint32_t height, uint32_t depth,
        TextureUsage usage, ImmutableCString&& tag) {
    DEBUG_MARKER()

    // 获取内部格式（GLenum）
    GLenum internalFormat = getInternalFormat(format);
    assert_invariant(internalFormat);

    // 根据使用场景决定使用纹理还是渲染缓冲区
    if (any(usage & TextureUsage::PROTECTED)) {
        // 渲染缓冲区没有保护模式，所以需要使用纹理
        // 因为保护纹理仅在 GLES 3.2 上支持，MSAA 将可用
        usage |= TextureUsage::SAMPLEABLE;
    } else if (any(usage & TextureUsage::UPLOADABLE)) {
        // 如果有可上传标志，也需要使用纹理
        usage |= TextureUsage::SAMPLEABLE;
    } else if (target != SamplerType::SAMPLER_2D) {
        // 渲染缓冲区只能是 2D
        usage |= TextureUsage::SAMPLEABLE;
    } else if (levels > 1) {
        // 渲染缓冲区不能有 Mipmap
        usage |= TextureUsage::SAMPLEABLE;
    }

    auto const& gl = mContext;
    // 限制采样数量（不超过驱动支持的最大值）
    samples = std::clamp(samples, uint8_t(1u), uint8_t(gl.gets.max_samples));
    GLTexture* t = construct<GLTexture>(th, target, levels, samples, width, height, depth, format, usage);
    
    // 如果是纹理（可采样）
    if (UTILS_LIKELY(usage & TextureUsage::SAMPLEABLE)) {

        if (UTILS_UNLIKELY(gl.isES2())) {
            // on ES2, format and internal format must match
            // FIXME: handle compressed texture format
            internalFormat = textureFormatToFormatAndType(format).first;
        }

        if (UTILS_UNLIKELY(t->target == SamplerType::SAMPLER_EXTERNAL)) {
            t->externalTexture = mPlatform.createExternalImageTexture();
            if (t->externalTexture) {
                if (target == SamplerType::SAMPLER_EXTERNAL) {
                    if (UTILS_LIKELY(gl.ext.OES_EGL_image_external_essl3)) {
                        t->externalTexture->target = GL_TEXTURE_EXTERNAL_OES;
                    } else {
                        // revert to texture 2D if external is not supported; what else can we do?
                        t->externalTexture->target = GL_TEXTURE_2D;
                    }
                } else {
                    t->externalTexture->target = getTextureTargetNotExternal(target);
                }

                t->gl.target = t->externalTexture->target;
                t->gl.id = t->externalTexture->id;
                // internalFormat actually depends on the external image, but it doesn't matter
                // because it's not used anywhere for anything important.
                t->gl.internalFormat = internalFormat;
                t->gl.baseLevel = 0;
                t->gl.maxLevel = 0;
            }
        } else {
            glGenTextures(1, &t->gl.id);

            t->gl.internalFormat = internalFormat;

            t->gl.target = getTextureTargetNotExternal(target);

            if (t->samples > 1) {
                // Note: we can't be here in practice because filament's user API doesn't
                // allow the creation of multi-sampled textures.
#if defined(BACKEND_OPENGL_LEVEL_GLES31)
                if (gl.features.multisample_texture) {
                    // multi-sample texture on GL 3.2 / GLES 3.1 and above
                    if (depth <= 1) {
                        // We forcibly change the target to 2D-multisample only for flat texture.
                        // A depth value greater than 1 may indicate multiview usage, which requires
                        // GL_TEXTURE_2D_ARRAY. Also 2D MSAA won't work with non-flat texture anyway.
                        t->gl.target = GL_TEXTURE_2D_MULTISAMPLE;
                    }
                } else {
                    // Turn off multi-sampling for that texture. It's just not supported.
                }
#endif
            }

            textureStorage(t, width, height, depth, bool(usage & TextureUsage::PROTECTED));
        }
    } else {
        t->gl.internalFormat = internalFormat;
        t->gl.target = GL_RENDERBUFFER;
        glGenRenderbuffers(1, &t->gl.id);
        renderBufferStorage(t->gl.id, internalFormat, width, height, samples);
    }

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 创建纹理视图（渲染线程）
 * 
 * 在渲染线程中创建纹理视图对象。
 * 纹理视图是现有纹理的视图，可以有不同的 Mipmap 范围，共享底层纹理数据。
 * 
 * @param th 纹理视图句柄（已在主线程分配）
 * @param srch 源纹理句柄（要创建视图的纹理）
 * @param baseLevel 基础 Mipmap 级别
 * @param levelCount Mipmap 级别数量
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 验证源纹理是可采样的（SAMPLEABLE）
 * 2. 验证源纹理不是导入的（导入纹理不支持视图）
 * 3. 如果源纹理还没有引用句柄，延迟创建（大多数纹理不会有视图）
 * 4. 构造纹理视图对象（复制源纹理的属性）
 * 5. 复制 OpenGL 状态（但重置 sidecar 相关字段）
 * 6. 计算视图的 Mipmap 范围（基于源纹理的 baseLevel/maxLevel）
 * 7. 增加源纹理的引用计数（多个视图共享同一纹理）
 * 8. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 纹理视图共享底层纹理数据，不复制数据
 * - 多个视图可以有不同的 Mipmap 范围
 * - 使用引用计数管理共享纹理的生命周期
 */
void OpenGLDriver::createTextureViewR(Handle<HwTexture> th,
        Handle<HwTexture> srch, uint8_t const baseLevel, uint8_t const levelCount, ImmutableCString&& tag) {
    DEBUG_MARKER()
    GLTexture const* const src = handle_cast<GLTexture const*>(srch);

    // 验证源纹理是可采样的
    FILAMENT_CHECK_PRECONDITION(any(src->usage & TextureUsage::SAMPLEABLE))
            << "TextureView can only be created on a SAMPLEABLE texture";

    // 验证源纹理不是导入的
    FILAMENT_CHECK_PRECONDITION(!src->gl.imported)
            << "TextureView can't be created on imported textures";

    // 如果源纹理还没有引用句柄，延迟创建（大多数纹理不会有视图）
    if (!src->ref) {
        src->ref = initHandle<GLTextureRef>();
    }

    // 构造纹理视图对象（复制源纹理的属性）
    GLTexture* t = construct<GLTexture>(th,
            src->target,
            src->levels,
            src->samples,
            src->width, src->height, src->depth,
            src->format,
            src->usage);

    // 复制 OpenGL 状态（但重置 sidecar 相关字段）
    t->gl = src->gl;
    t->gl.sidecarRenderBufferMS = 0;
    t->gl.sidecarSamples = 1;

    // 计算视图的 Mipmap 范围（基于源纹理的 baseLevel/maxLevel）
    auto srcBaseLevel = src->gl.baseLevel;
    auto srcMaxLevel = src->gl.maxLevel;
    if (srcBaseLevel > srcMaxLevel) {
        srcBaseLevel = 0;
        srcMaxLevel = 127;
    }
    t->gl.baseLevel = int8_t(std::min(127, srcBaseLevel + baseLevel));
    t->gl.maxLevel  = int8_t(std::min(127, srcBaseLevel + baseLevel + levelCount - 1));

    // 增加源纹理的引用计数（多个视图共享同一纹理）
    t->ref = src->ref;
    GLTextureRef* ref = handle_cast<GLTextureRef*>(t->ref);
    assert_invariant(ref);
    ref->count++;

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 创建带 Swizzle 的纹理视图（渲染线程）
 * 
 * 在渲染线程中创建带通道重映射（Swizzle）的纹理视图对象。
 * Swizzle 允许重新映射纹理通道（如 R->A, G->R 等），用于格式转换或特殊效果。
 * 
 * @param th 纹理视图句柄（已在主线程分配）
 * @param srch 源纹理句柄（要创建视图的纹理）
 * @param r 红色通道的 Swizzle 映射
 * @param g 绿色通道的 Swizzle 映射
 * @param b 蓝色通道的 Swizzle 映射
 * @param a Alpha 通道的 Swizzle 映射
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 验证源纹理是可采样的（SAMPLEABLE）
 * 2. 验证源纹理不是导入的（导入纹理不支持视图）
 * 3. 如果源纹理还没有引用句柄，延迟创建
 * 4. 构造纹理视图对象（复制源纹理的属性）
 * 5. 复制 OpenGL 状态（包括 baseLevel/maxLevel）
 * 6. 计算 Swizzle 映射：
 *    - 如果请求的通道是 SUBSTITUTE_ZERO/ONE，直接使用
 *    - 如果请求的通道是 CHANNEL_0/1/2/3，从源纹理的 swizzle 中获取
 * 7. 设置视图的 Swizzle
 * 8. 增加源纹理的引用计数
 * 9. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - Swizzle 可以嵌套（视图的 Swizzle 基于源纹理的 Swizzle）
 * - 用于实现格式转换（如 R8 -> RGBA8，通过 Swizzle R->R, R->G, R->B, 1->A）
 */
void OpenGLDriver::createTextureViewSwizzleR(Handle<HwTexture> th, Handle<HwTexture> srch,
        TextureSwizzle const r, TextureSwizzle const g, TextureSwizzle const b, TextureSwizzle const a,
        ImmutableCString&& tag) {

    DEBUG_MARKER()
    GLTexture const* const src = handle_cast<GLTexture const*>(srch);

    // 验证源纹理是可采样的
    FILAMENT_CHECK_PRECONDITION(any(src->usage & TextureUsage::SAMPLEABLE))
                    << "TextureView can only be created on a SAMPLEABLE texture";

    // 验证源纹理不是导入的
    FILAMENT_CHECK_PRECONDITION(!src->gl.imported)
                    << "TextureView can't be created on imported textures";

    // 如果源纹理还没有引用句柄，延迟创建
    if (!src->ref) {
        src->ref = initHandle<GLTextureRef>();
    }

    // 构造纹理视图对象（复制源纹理的属性）
    GLTexture* t = construct<GLTexture>(th,
            src->target,
            src->levels,
            src->samples,
            src->width, src->height, src->depth,
            src->format,
            src->usage);

    // 复制 OpenGL 状态（包括 baseLevel/maxLevel）
    t->gl = src->gl;
    t->gl.baseLevel = src->gl.baseLevel;
    t->gl.maxLevel = src->gl.maxLevel;
    t->gl.sidecarRenderBufferMS = 0;
    t->gl.sidecarSamples = 1;

    // 计算 Swizzle 映射（可以嵌套，基于源纹理的 swizzle）
    auto getChannel = [&swizzle = src->gl.swizzle](TextureSwizzle const ch) {
        switch (ch) {
            case TextureSwizzle::SUBSTITUTE_ZERO:
            case TextureSwizzle::SUBSTITUTE_ONE:
                return ch;  // 直接使用 0 或 1
            case TextureSwizzle::CHANNEL_0:
                return swizzle[0];  // 从源纹理的 swizzle 获取
            case TextureSwizzle::CHANNEL_1:
                return swizzle[1];
            case TextureSwizzle::CHANNEL_2:
                return swizzle[2];
            case TextureSwizzle::CHANNEL_3:
                return swizzle[3];
        }
        return ch;
    };

    // 设置视图的 Swizzle
    t->gl.swizzle = {
            getChannel(r),
            getChannel(g),
            getChannel(b),
            getChannel(a),
    };

    // 增加源纹理的引用计数
    t->ref = src->ref;
    GLTextureRef* const ref = handle_cast<GLTextureRef*>(t->ref);
    assert_invariant(ref);
    ref->count++;

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 创建外部图像纹理（2D，渲染线程）
 * 
 * 在渲染线程中创建外部图像纹理对象。
 * 外部图像纹理用于从外部源（如相机预览、视频帧）创建纹理。
 * 
 * @param th 纹理句柄（已在主线程分配）
 * @param target 采样器类型（通常是 SAMPLER_EXTERNAL 或 SAMPLER_2D）
 * @param format 纹理格式
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param usage 使用标志（自动添加 SAMPLEABLE，移除 UPLOADABLE）
 * @param image 外部图像句柄引用（平台特定）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 设置使用标志（必须是 SAMPLEABLE，不能是 UPLOADABLE）
 * 2. 获取内部格式（ES 2.0 特殊处理）
 * 3. 构造纹理对象
 * 4. 创建外部纹理对象（通过平台接口）
 * 5. 设置纹理目标（GL_TEXTURE_EXTERNAL_OES 或 GL_TEXTURE_2D）
 * 6. 绑定纹理并设置外部图像
 * 7. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 外部纹理的格式取决于外部图像，内部格式仅供参考
 * - 外部纹理总是标记为 external，强制每次绑定（不缓存）
 */
void OpenGLDriver::createTextureExternalImage2R(Handle<HwTexture> th, SamplerType target,
    TextureFormat format, uint32_t width, uint32_t height, TextureUsage usage,
    Platform::ExternalImageHandleRef image, ImmutableCString&& tag) {
    DEBUG_MARKER()

    // 外部纹理必须是可采样的，不能是可上传的
    usage |= TextureUsage::SAMPLEABLE;
    usage &= ~TextureUsage::UPLOADABLE;

    auto const& gl = mContext;
    GLenum internalFormat = getInternalFormat(format);
    if (UTILS_UNLIKELY(gl.isES2())) {
        // on ES2, format and internal format must match
        // FIXME: handle compressed texture format
        internalFormat = textureFormatToFormatAndType(format).first;
    }
    assert_invariant(internalFormat);

    GLTexture* const t = construct<GLTexture>(th, target, 1, 1, width, height, 1, format, usage);
    assert_invariant(t);

    t->externalTexture = mPlatform.createExternalImageTexture();
    if (t->externalTexture) {
        if (target == SamplerType::SAMPLER_EXTERNAL) {
            if (UTILS_LIKELY(gl.ext.OES_EGL_image_external_essl3)) {
                t->externalTexture->target = GL_TEXTURE_EXTERNAL_OES;
            } else {
                // revert to texture 2D if external is not supported; what else can we do?
                t->externalTexture->target = GL_TEXTURE_2D;
            }
        } else {
            t->externalTexture->target = getTextureTargetNotExternal(target);
        }

        t->gl.target = t->externalTexture->target;
        t->gl.id = t->externalTexture->id;
        // internalFormat actually depends on the external image, but it doesn't matter
        // because it's not used anywhere for anything important.
        t->gl.internalFormat = internalFormat;
        t->gl.baseLevel = 0;
        t->gl.maxLevel = 0;
        t->gl.external = true; // forces bindTexture() call (they're never cached)
    }

    bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
    if (mPlatform.setExternalImage(image, t->externalTexture)) {
        // the target and id can be reset each time
        t->gl.target = t->externalTexture->target;
        t->gl.id = t->externalTexture->id;
    }
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 创建外部图像纹理（渲染线程）
 * 
 * 在渲染线程中创建外部图像纹理对象。
 * 与 createTextureExternalImage2R 类似，但使用 void* 图像指针。
 * 
 * @param th 纹理句柄（已在主线程分配）
 * @param target 采样器类型
 * @param format 纹理格式
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param usage 使用标志
 * @param image 外部图像指针（平台特定）
 * @param tag 调试标签
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 与 createTextureExternalImage2R 功能相同，但参数类型不同
 */
void OpenGLDriver::createTextureExternalImageR(Handle<HwTexture> th, SamplerType target,
        TextureFormat format, uint32_t width, uint32_t height, TextureUsage usage, void* image,
        ImmutableCString&& tag) {
    DEBUG_MARKER()

    // 外部纹理必须是可采样的，不能是可上传的
    usage |= TextureUsage::SAMPLEABLE;
    usage &= ~TextureUsage::UPLOADABLE;

    auto const& gl = mContext;
    GLenum internalFormat = getInternalFormat(format);
    if (UTILS_UNLIKELY(gl.isES2())) {
        // on ES2, format and internal format must match
        // FIXME: handle compressed texture format
        internalFormat = textureFormatToFormatAndType(format).first;
    }
    assert_invariant(internalFormat);

    GLTexture* const t = construct<GLTexture>(th, target, 1, 1, width, height, 1, format, usage);
    assert_invariant(t);

    t->externalTexture = mPlatform.createExternalImageTexture();
    if (t->externalTexture) {
        if (target == SamplerType::SAMPLER_EXTERNAL) {
            if (UTILS_LIKELY(gl.ext.OES_EGL_image_external_essl3)) {
                t->externalTexture->target = GL_TEXTURE_EXTERNAL_OES;
            } else {
                // revert to texture 2D if external is not supported; what else can we do?
                t->externalTexture->target = GL_TEXTURE_2D;
            }
        } else {
            t->externalTexture->target = getTextureTargetNotExternal(target);
        }
        t->gl.target = t->externalTexture->target;
        t->gl.id = t->externalTexture->id;
        // internalFormat actually depends on the external image, but it doesn't matter
        // because it's not used anywhere for anything important.
        t->gl.internalFormat = internalFormat;
        t->gl.baseLevel = 0;
        t->gl.maxLevel = 0;
        t->gl.external = true; // forces bindTexture() call (they're never cached)
    }

    bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
    if (mPlatform.setExternalImage(image, t->externalTexture)) {
        // the target and id can be reset each time
        t->gl.target = t->externalTexture->target;
        t->gl.id = t->externalTexture->id;
    }
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 创建外部图像平面纹理（渲染线程）
 * 
 * 在渲染线程中创建外部图像平面纹理对象。
 * 用于多平面图像（如 YUV 格式，Y、U、V 分别在不同平面）。
 * 
 * @param th 纹理句柄（已在主线程分配）
 * @param format 纹理格式
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param usage 使用标志
 * @param image 外部图像指针（平台特定）
 * @param plane 平面索引（0=Y, 1=U, 2=V 等）
 * @param tag 调试标签（未使用）
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - OpenGL 后端不支持多平面图像，此方法为空实现
 * - 多平面图像通常在 Vulkan/Metal 后端使用
 */
void OpenGLDriver::createTextureExternalImagePlaneR(Handle<HwTexture> th,
        TextureFormat format, uint32_t width, uint32_t height, TextureUsage usage,
        void* image, uint32_t plane, ImmutableCString&&) {
    // OpenGL 后端不相关（不支持多平面图像）
}

/**
 * 导入纹理（渲染线程）
 * 
 * 在渲染线程中导入外部 OpenGL 纹理。
 * 用于从外部 OpenGL 应用程序或库导入现有纹理。
 * 
 * @param th 纹理句柄（已在主线程分配）
 * @param id 外部 OpenGL 纹理 ID
 * @param target 采样器类型（2D/3D/Cube/Array/External 等）
 * @param levels Mipmap 级别数量
 * @param format 纹理格式
 * @param samples 多重采样数量
 * @param width 纹理宽度
 * @param height 纹理高度
 * @param depth 纹理深度（3D/Array 纹理）
 * @param usage 使用标志
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 限制采样数量（不超过驱动支持的最大值）
 * 2. 构造纹理对象
 * 3. 设置导入的纹理 ID（不创建新纹理）
 * 4. 标记为导入纹理（imported = true）
 * 5. 获取内部格式
 * 6. 根据采样器类型设置 OpenGL 目标
 * 7. 处理多采样纹理（如果适用）
 * 8. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 导入的纹理不归 Filament 管理，外部负责生命周期
 * - 导入的纹理不支持纹理视图
 * - 外部纹理（SAMPLER_EXTERNAL）总是标记为 external，强制每次绑定
 */
void OpenGLDriver::importTextureR(Handle<HwTexture> th, intptr_t const id,
        SamplerType target, uint8_t levels, TextureFormat format, uint8_t samples,
        uint32_t width, uint32_t height, uint32_t depth, TextureUsage usage, ImmutableCString&& tag) {
    DEBUG_MARKER()

    auto const& gl = mContext;
    // 限制采样数量（不超过驱动支持的最大值）
    samples = std::clamp(samples, uint8_t(1u), uint8_t(gl.gets.max_samples));
    GLTexture* t = construct<GLTexture>(th, target, levels, samples, width, height, depth, format, usage);

    // 设置导入的纹理 ID（不创建新纹理）
    t->gl.id = GLuint(id);
    t->gl.imported = true;  // 标记为导入纹理
    t->gl.internalFormat = getInternalFormat(format);
    assert_invariant(t->gl.internalFormat);

    switch (target) {
        case SamplerType::SAMPLER_EXTERNAL:
            t->gl.target = GL_TEXTURE_EXTERNAL_OES;
            t->gl.external = true; // forces bindTexture() call (they're never cached)
            break;
        case SamplerType::SAMPLER_2D:
            t->gl.target = GL_TEXTURE_2D;
            break;
        case SamplerType::SAMPLER_3D:
            t->gl.target = GL_TEXTURE_3D;
            break;
        case SamplerType::SAMPLER_2D_ARRAY:
            t->gl.target = GL_TEXTURE_2D_ARRAY;
            break;
        case SamplerType::SAMPLER_CUBEMAP:
            t->gl.target = GL_TEXTURE_CUBE_MAP;
            break;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            t->gl.target = GL_TEXTURE_CUBE_MAP_ARRAY;
            break;
    }

    if (t->samples > 1) {
        // Note: we can't be here in practice because filament's user API doesn't
        // allow the creation of multi-sampled textures.
#if defined(BACKEND_OPENGL_LEVEL_GLES31)
        if (gl.features.multisample_texture) {
            // multi-sample texture on GL 3.2 / GLES 3.1 and above
            if (depth <= 1) {
                // We forcibly change the target to 2D-multisample only for flat texture.
                // A depth value greater than 1 may indicate multiview usage, which requires
                // GL_TEXTURE_2D_ARRAY. Also 2D MSAA won't work with non-flat texture anyway.
                t->gl.target = GL_TEXTURE_2D_MULTISAMPLE;
            }
        } else {
            // Turn off multi-sampling for that texture. It's just not supported.
        }
#endif
    }

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(th.getId(), std::move(tag));
}

/**
 * 更新顶点数组对象（VAO）
 * 
 * 更新 VAO 中的顶点缓冲区绑定和顶点属性配置。
 * 此方法被频繁调用，必须尽可能高效。
 * 
 * @param rp 渲染图元指针
 * @param vb 顶点缓冲区指针
 * 
 * 执行流程：
 * 1. 验证 VAO 已绑定（调试模式）
 * 2. 检查版本号，如果 VAO 已是最新，直接返回（性能优化）
 * 3. 获取顶点缓冲区信息（属性布局）
 * 4. 遍历所有属性，设置顶点属性指针：
 *    - 绑定对应的缓冲区对象
 *    - 启用顶点属性
 *    - 设置顶点属性指针（位置、步长、偏移）
 *    - 设置整数属性标志（如果适用）
 * 5. 禁用未使用的属性
 * 6. 更新版本号
 * 
 * 性能优化：
 * - 使用版本号检查，避免不必要的更新
 * - 只在缓冲区或状态改变时更新
 * - 禁用未使用的属性，避免驱动开销
 * 
 * 注意：
 * - 此方法被频繁调用（每次 draw 都可能调用）
 * - VAO 必须在调用前已绑定
 * - 版本号用于缓存，避免重复设置相同状态
 */
void OpenGLDriver::updateVertexArrayObject(GLRenderPrimitive* rp, GLVertexBuffer const* vb) {
    // 注意：此方法被频繁调用，必须尽可能高效

    auto& gl = mContext;

#ifndef NDEBUG
    // 验证 VAO 已绑定（调试模式）
    if (UTILS_LIKELY(gl.ext.OES_vertex_array_object)) {
        GLint vaoBinding;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vaoBinding);
        assert_invariant(vaoBinding == GLint(rp->gl.vao[gl.contextIndex]));
    }
#endif

    // 检查版本号，如果 VAO 已是最新，直接返回（性能优化）
    if (UTILS_LIKELY(rp->gl.vertexBufferVersion == vb->bufferObjectsVersion &&
                     rp->gl.stateVersion == gl.state.age)) {
        return;
    }

    // 获取顶点缓冲区信息（属性布局）
    GLVertexBufferInfo const* const vbi = handle_cast<const GLVertexBufferInfo*>(vb->vbih);

    // 遍历所有属性，设置顶点属性指针
    for (size_t i = 0, n = vbi->attributes.size(); i < n; i++) {
        const auto& attribute = vbi->attributes[i];
        const uint8_t bi = attribute.buffer;
        if (bi != Attribute::BUFFER_UNUSED) {
            // if a buffer is defined it must not be invalid.
            assert_invariant(vb->gl.buffers[bi]);

            // if we're on ES2, the user shouldn't use FLAG_INTEGER_TARGET
            assert_invariant(!(gl.isES2() && (attribute.flags & Attribute::FLAG_INTEGER_TARGET)));

            gl.bindBuffer(GL_ARRAY_BUFFER, vb->gl.buffers[bi]);
            GLuint const index = i;
            GLint const size = GLint(getComponentCount(attribute.type));
            GLenum const type = getComponentType(attribute.type);
            GLboolean const normalized = getNormalization(attribute.flags & Attribute::FLAG_NORMALIZED);
            GLsizei const stride = attribute.stride;
            void const* pointer = reinterpret_cast<void const *>(attribute.offset);

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            if (UTILS_UNLIKELY(attribute.flags & Attribute::FLAG_INTEGER_TARGET)) {
                // integer attributes can't be floats
                assert_invariant(type == GL_BYTE || type == GL_UNSIGNED_BYTE || type == GL_SHORT ||
                        type == GL_UNSIGNED_SHORT || type == GL_INT || type == GL_UNSIGNED_INT);
                glVertexAttribIPointer(index, size, type, stride, pointer);
            } else
#endif
            {
                glVertexAttribPointer(index, size, type, normalized, stride, pointer);
            }

            gl.enableVertexAttribArray(&rp->gl, GLuint(i));
        } else {
            // In some OpenGL implementations, we must supply a properly-typed placeholder for
            // every integer input that is declared in the vertex shader.
            // Note that the corresponding doesn't have to be enabled and in fact won't be. If it
            // was enabled, it would indicate a user-error (providing the wrong type of array).
            // With a disabled array, the vertex shader gets the attribute from glVertexAttrib,
            // and must have the proper intergerness.
            // But at this point, we don't know what the shader requirements are, and so we must
            // rely on the attribute.

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            if (UTILS_UNLIKELY(attribute.flags & Attribute::FLAG_INTEGER_TARGET)) {
                if (!gl.isES2()) {
                    // on ES2, we know the shader doesn't have integer attributes
                    glVertexAttribI4ui(GLuint(i), 0, 0, 0, 0);
                }
            } else
#endif
            {
                glVertexAttrib4f(GLuint(i), 0, 0, 0, 0);
            }

            gl.disableVertexAttribArray(&rp->gl, GLuint(i));
        }
    }

    rp->gl.stateVersion = gl.state.age;
    if (UTILS_LIKELY(gl.ext.OES_vertex_array_object)) {
        rp->gl.vertexBufferVersion = vb->bufferObjectsVersion;
    } else {
        // if we don't have OES_vertex_array_object, we never update the buffer version so
        // that it's always reset in draw
    }
}

/**
 * 将纹理附加到 Framebuffer
 * 
 * 将纹理或渲染缓冲区附加到渲染目标的指定附件（颜色、深度、模板）。
 * 处理多种纹理类型（2D、3D、Cube、Array）和 MSAA 情况。
 * 
 * @param binfo 目标缓冲区信息（纹理句柄、Mipmap 级别、层等）
 * @param rt 渲染目标指针
 * @param attachment 附件类型（GL_COLOR_ATTACHMENT0、GL_DEPTH_ATTACHMENT 等）
 * @param layerCount 层数量（用于 Array 纹理）
 * 
 * 执行流程：
 * 1. 验证纹理有效且不是外部纹理
 * 2. 验证尺寸匹配（渲染目标尺寸 <= 纹理 Mipmap 级别尺寸）
 * 3. 确定 resolve 标志（用于 MSAA resolve）
 * 4. 验证深度/模板附件的采样数匹配（EXT_multisampled_render_to_texture 限制）
 * 5. 根据纹理类型确定目标（2D、Cube face、Array 等）
 * 6. 如果是纹理：
 *    - 使用 glFramebufferTexture2D/3D 附加
 *    - 处理 MSAA（使用 EXT_multisampled_render_to_texture）
 * 7. 如果是渲染缓冲区：
 *    - 使用 glFramebufferRenderbuffer 附加
 * 8. 更新 resolve 标志
 * 
 * 注意：
 * - 深度/模板附件必须与渲染目标的采样数匹配（EXT_multisampled_render_to_texture 限制）
 * - MSAA 纹理使用 sidecar renderbuffer 进行实际渲染
 */
void OpenGLDriver::framebufferTexture(TargetBufferInfo const& binfo,
        GLRenderTarget const* rt, GLenum attachment, uint8_t const layerCount) noexcept {

#if !defined(NDEBUG)
    // Only used by assert_invariant() checks below
    UTILS_UNUSED_IN_RELEASE auto valueForLevel = [](size_t const level, size_t const value) {
        return std::max(size_t(1), value >> level);
    };
#endif

    GLTexture* t = handle_cast<GLTexture*>(binfo.handle);

    assert_invariant(t);
    assert_invariant(t->target != SamplerType::SAMPLER_EXTERNAL);
    assert_invariant(rt->width  <= valueForLevel(binfo.level, t->width) &&
           rt->height <= valueForLevel(binfo.level, t->height));

    // Declare a small mask of bits that will later be OR'd into the texture's resolve mask.
    TargetBufferFlags resolveFlags = {};

    switch (attachment) {
        case GL_COLOR_ATTACHMENT0:
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_COLOR_ATTACHMENT1:
        case GL_COLOR_ATTACHMENT2:
        case GL_COLOR_ATTACHMENT3:
        case GL_COLOR_ATTACHMENT4:
        case GL_COLOR_ATTACHMENT5:
        case GL_COLOR_ATTACHMENT6:
        case GL_COLOR_ATTACHMENT7:
#endif
            assert_invariant((attachment != GL_COLOR_ATTACHMENT0 && !mContext.isES2())
                             || attachment == GL_COLOR_ATTACHMENT0);

            static_assert(MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT == 8);

            resolveFlags = getTargetBufferFlagsAt(attachment - GL_COLOR_ATTACHMENT0);
            break;
        case GL_DEPTH_ATTACHMENT:
            resolveFlags = TargetBufferFlags::DEPTH;
            break;
        case GL_STENCIL_ATTACHMENT:
            resolveFlags = TargetBufferFlags::STENCIL;
            break;
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_DEPTH_STENCIL_ATTACHMENT:
            assert_invariant(!mContext.isES2());
            resolveFlags = TargetBufferFlags::DEPTH;
            resolveFlags |= TargetBufferFlags::STENCIL;
            break;
#endif
        default:
            break;
    }

    // depth/stencil attachments must match the rendertarget sample count
    // because EXT_multisampled_render_to_texture[2] doesn't resolve the depth/stencil
    // buffers:
    // for EXT_multisampled_render_to_texture
    //      "the contents of the multisample buffer become undefined"
    // for EXT_multisampled_render_to_texture2
    //      "the contents of the multisample buffer is discarded rather than resolved -
    //       equivalent to the application calling InvalidateFramebuffer for this attachment"
    UTILS_UNUSED bool attachmentTypeNotSupportedByMSRTT = false;
    switch (attachment) {
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_DEPTH_STENCIL_ATTACHMENT:
            assert_invariant(!mContext.isES2());
            UTILS_FALLTHROUGH;
#endif
        case GL_DEPTH_ATTACHMENT:
        case GL_STENCIL_ATTACHMENT:
            attachmentTypeNotSupportedByMSRTT = rt->gl.samples != t->samples;
            break;
        default:
            break;
    }

    auto& gl = mContext;

    GLenum target = GL_TEXTURE_2D;
    if (any(t->usage & TextureUsage::SAMPLEABLE)) {
        switch (t->target) {
            case SamplerType::SAMPLER_2D:
            case SamplerType::SAMPLER_3D:
            case SamplerType::SAMPLER_2D_ARRAY:
            case SamplerType::SAMPLER_CUBEMAP_ARRAY:
                // this could be GL_TEXTURE_2D_MULTISAMPLE or GL_TEXTURE_2D_ARRAY
                target = t->gl.target;
                // note: multi-sampled textures can't have mipmaps
                break;
            case SamplerType::SAMPLER_CUBEMAP:
                target = getCubemapTarget(binfo.layer);
                // note: cubemaps can't be multi-sampled
                break;
            case SamplerType::SAMPLER_EXTERNAL:
                // This is an error. We have asserted in debug build.
                target = t->gl.target;
                break;
        }
    }

    // We can't use FramebufferTexture2DMultisampleEXT with GL_TEXTURE_2D_ARRAY or GL_TEXTURE_CUBE_MAP_ARRAY.
    if (!(target == GL_TEXTURE_2D ||
          target == GL_TEXTURE_CUBE_MAP_POSITIVE_X ||
          target == GL_TEXTURE_CUBE_MAP_NEGATIVE_X ||
          target == GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
          target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Y ||
          target == GL_TEXTURE_CUBE_MAP_POSITIVE_Z ||
          target == GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)) {
        attachmentTypeNotSupportedByMSRTT = true;
    }

    if (rt->gl.samples <= 1 ||
        (rt->gl.samples > 1 && t->samples > 1 && gl.features.multisample_texture)) {
        // On GL3.2 / GLES3.1 and above multisample is handled when creating the texture.
        // If multisampled textures are not supported and we end-up here, things should
        // still work, albeit without MSAA.
        gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
        switch (target) {
            case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            case GL_TEXTURE_2D:
#if defined(BACKEND_OPENGL_LEVEL_GLES31)
            case GL_TEXTURE_2D_MULTISAMPLE:
#endif
                if (any(t->usage & TextureUsage::SAMPLEABLE)) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                            target, t->gl.id, binfo.level);
                } else {
                    assert_invariant(target == GL_TEXTURE_2D);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment,
                            GL_RENDERBUFFER, t->gl.id);
                }
                break;
            case GL_TEXTURE_3D:
            case GL_TEXTURE_2D_ARRAY:
            case GL_TEXTURE_CUBE_MAP_ARRAY:
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2

                // TODO: support multiview for iOS and WebGL
#if !defined(__EMSCRIPTEN__) && !defined(FILAMENT_IOS)
                if (layerCount > 1) {
                    // if layerCount > 1, it means we use the multiview extension.
                    if (rt->gl.samples > 1) {
                        // For MSAA
                        glFramebufferTextureMultisampleMultiviewOVR(GL_FRAMEBUFFER, attachment,
                                t->gl.id, 0, rt->gl.samples, binfo.layer, layerCount);
                    }
                    else {
                        glFramebufferTextureMultiviewOVR(GL_FRAMEBUFFER, attachment, t->gl.id, 0,
                                binfo.layer, layerCount);
                    }
                } else
#endif // !defined(__EMSCRIPTEN__) && !defined(FILAMENT_IOS)
                {
                    // GL_TEXTURE_2D_MULTISAMPLE_ARRAY is not supported in GLES
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment,
                        t->gl.id, binfo.level, binfo.layer);
                }
#endif
                break;
            default:
                // we shouldn't be here
                break;
        }
        CHECK_GL_ERROR()
    } else
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_multisampled_render_to_texture
        // EXT_multisampled_render_to_texture only support GL_COLOR_ATTACHMENT0
    if (!attachmentTypeNotSupportedByMSRTT && (t->depth <= 1)
        && ((gl.ext.EXT_multisampled_render_to_texture && attachment == GL_COLOR_ATTACHMENT0)
            || gl.ext.EXT_multisampled_render_to_texture2)) {
        assert_invariant(rt->gl.samples > 1);
        // We have a multi-sample rendertarget, and we have EXT_multisampled_render_to_texture,
        // so, we can directly use a 1-sample texture as attachment, multi-sample resolve,
        // will happen automagically and efficiently in the driver.
        // This extension only exists on OpenGL ES.
        gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
        if (any(t->usage & TextureUsage::SAMPLEABLE)) {
            glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
                    attachment, target, t->gl.id, binfo.level, rt->gl.samples);
        } else {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment,
                    GL_RENDERBUFFER, t->gl.id);
        }
        CHECK_GL_ERROR()
    } else
#endif // GL_EXT_multisampled_render_to_texture
#endif // __EMSCRIPTEN__
    if (!any(t->usage & TextureUsage::SAMPLEABLE) && t->samples > 1) {
        assert_invariant(rt->gl.samples > 1);
        assert_invariant(glIsRenderbuffer(t->gl.id));

        // Since this attachment is not sampleable, there is no need for a sidecar or explicit
        // resolve. We can simply render directly into the renderbuffer that was allocated in
        // createTexture.
        gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, t->gl.id);

        // Clear the resolve bit for this particular attachment. Note that other attachment(s)
        // might be sampleable, so this does not necessarily prevent the resolve from occurring.
        resolveFlags = TargetBufferFlags::NONE;

    } else {
        // Here we emulate EXT_multisampled_render_to_texture.
        //
        // This attachment needs to be explicitly resolved in endRenderPass().
        // The first step is to create a sidecar multi-sampled renderbuffer, which is where drawing
        // will actually take place, and use that in lieu of the requested attachment.
        // The sidecar will be destroyed when the render target handle is destroyed.

        assert_invariant(rt->gl.samples > 1);

        gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);

        if (UTILS_UNLIKELY(t->gl.sidecarRenderBufferMS == 0 ||
                rt->gl.samples != t->gl.sidecarSamples))
        {
            if (t->gl.sidecarRenderBufferMS == 0) {
                glGenRenderbuffers(1, &t->gl.sidecarRenderBufferMS);
            }
            renderBufferStorage(t->gl.sidecarRenderBufferMS,
                    t->gl.internalFormat, t->width, t->height, rt->gl.samples);
            t->gl.sidecarSamples = rt->gl.samples;
        }

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER,
                t->gl.sidecarRenderBufferMS);

        // Here we lazily create a "read" sidecar FBO, used later as the resolve target. Note that
        // at least one of the render target's attachments needs to be both MSAA and sampleable in
        // order for fbo_read to be created. If we never bother to create it, then endRenderPass()
        // will skip doing an explicit resolve.
        if (!rt->gl.fbo_read) {
            glGenFramebuffers(1, &rt->gl.fbo_read);
        }
        gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo_read);
        switch (target) {
            case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            case GL_TEXTURE_2D:
                if (any(t->usage & TextureUsage::SAMPLEABLE)) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                            target, t->gl.id, binfo.level);
                } else {
                    assert_invariant(target == GL_TEXTURE_2D);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment,
                            GL_RENDERBUFFER, t->gl.id);
                }
                break;
            case GL_TEXTURE_3D:
            case GL_TEXTURE_2D_ARRAY:
            case GL_TEXTURE_CUBE_MAP_ARRAY:
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
                glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment,
                        t->gl.id, binfo.level, binfo.layer);
#endif
                break;
            default:
                // we shouldn't be here
                break;
        }

        CHECK_GL_ERROR()
    }

    rt->gl.resolve |= resolveFlags;

    CHECK_GL_ERROR()
    CHECK_GL_FRAMEBUFFER_STATUS(GL_FRAMEBUFFER)
}

/**
 * 分配渲染缓冲区存储
 * 
 * 为渲染缓冲区分配存储空间。
 * 渲染缓冲区用于离屏渲染目标（不能采样，只能作为附件）。
 * 
 * @param rbo 渲染缓冲区对象 ID
 * @param internalformat 内部格式（GLenum）
 * @param width 宽度
 * @param height 高度
 * @param samples 多重采样数量（1 = 无 MSAA）
 * 
 * 执行流程：
 * 1. 绑定渲染缓冲区
 * 2. 如果 samples > 1（MSAA）：
 *    - 检查 EXT_multisampled_render_to_texture 扩展
 *    - 如果支持，使用 glRenderbufferStorageMultisampleEXT
 *    - 否则使用 glRenderbufferStorageMultisample（ES 3.0+/GL 4.1+）
 * 3. 如果 samples == 1（无 MSAA）：
 *    - 使用 glRenderbufferStorage
 * 4. 解绑渲染缓冲区（避免后续混淆）
 * 
 * 注意：
 * - 渲染缓冲区不能采样，只能作为 Framebuffer 附件
 * - MSAA 渲染缓冲区需要相应扩展或 ES 3.0+/GL 4.1+
 */
void OpenGLDriver::renderBufferStorage(GLuint const rbo, GLenum internalformat, uint32_t width, // NOLINT(readability-convert-member-functions-to-static)
        uint32_t height, uint8_t samples) const noexcept {
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    if (samples > 1) {
        // MSAA 渲染缓冲区
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_multisampled_render_to_texture
        auto& gl = mContext;
        if (gl.ext.EXT_multisampled_render_to_texture ||
            gl.ext.EXT_multisampled_render_to_texture2) {
            // 使用扩展 API
            glext::glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER,
                    samples, internalformat, width, height);
        } else
#endif // GL_EXT_multisampled_render_to_texture
#endif // __EMSCRIPTEN__
        {
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            // 使用标准 API（ES 3.0+/GL 4.1+）
            glRenderbufferStorageMultisample(GL_RENDERBUFFER,
                    samples, internalformat, GLsizei(width), GLsizei(height));
#endif
        }
    } else {
        // 无 MSAA 渲染缓冲区
        glRenderbufferStorage(GL_RENDERBUFFER, internalformat, GLsizei(width), GLsizei(height));
    }
    // 解绑渲染缓冲区，避免后续混淆
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    CHECK_GL_ERROR()
}

/**
 * 创建默认渲染目标（渲染线程）
 * 
 * 在渲染线程中创建默认渲染目标对象。
 * 默认渲染目标指向交换链的后缓冲区（默认帧缓冲区）。
 * 
 * @param rth 渲染目标句柄（已在主线程分配）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 构造 GLRenderTarget 对象（宽度/高度未知，设为 0）
 * 2. 标记为默认渲染目标（isDefault = true）
 * 3. 设置 FBO = 0（默认帧缓冲区，实际 ID 在绑定时解析）
 * 4. 设置采样数 = 1（默认无 MSAA）
 * 5. 设置目标标志（COLOR0 | DEPTH，假设有颜色和深度附件）
 * 6. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - FBO = 0 表示默认帧缓冲区（交换链的后缓冲区）
 * - 宽度/高度在运行时从交换链获取
 * - TODO: 目标标志应该反映实际存在的附件
 */
void OpenGLDriver::createDefaultRenderTargetR(
        Handle<HwRenderTarget> rth, ImmutableCString&& tag) {
    DEBUG_MARKER()

    construct<GLRenderTarget>(rth, 0, 0);  // FIXME: 我们不知道宽度/高度

    GLRenderTarget* rt = handle_cast<GLRenderTarget*>(rth);
    rt->gl.isDefault = true;  // 标记为默认渲染目标
    rt->gl.fbo = 0;  // 实际 ID 在绑定时解析（0 = 默认帧缓冲区）
    rt->gl.samples = 1;
    // FIXME: 这些标志应该反映实际存在的附件
    rt->targets = TargetBufferFlags::COLOR0 | TargetBufferFlags::DEPTH;
    mHandleAllocator.associateTagToHandle(rth.getId(), std::move(tag));
}

/**
 * 创建渲染目标（渲染线程）
 * 
 * 在渲染线程中创建离屏渲染目标对象（FBO）。
 * 渲染目标可以包含多个颜色附件、深度附件和模板附件。
 * 
 * @param rth 渲染目标句柄（已在主线程分配）
 * @param targets 目标缓冲区标志（COLOR0-COLOR7、DEPTH、STENCIL）
 * @param width 渲染目标宽度
 * @param height 渲染目标高度
 * @param samples 多重采样数量
 * @param layerCount 层数量（用于 Array 纹理）
 * @param color 颜色附件数组（MRT，最多 8 个）
 * @param depth 深度附件信息
 * @param stencil 模板附件信息
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 构造 GLRenderTarget 对象
 * 2. 生成 Framebuffer 对象（FBO）
 * 3. 限制采样数量（不超过驱动支持的最大值）
 * 4. 附加颜色附件（COLOR0-COLOR7）：
 *    - 遍历所有颜色附件
 *    - 调用 framebufferTexture 附加到 FBO
 *    - 设置 glDrawBuffers（ES 3.0+）
 * 5. 处理深度/模板附件：
 *    - 如果深度和模板打包（DEPTH_AND_STENCIL），使用 GL_DEPTH_STENCIL_ATTACHMENT
 *    - 否则分别附加深度和模板附件
 * 6. 验证所有附件尺寸相同（OpenGL 要求）
 * 7. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 所有附件必须具有相同的尺寸（OpenGL 要求）
 * - MSAA 附件的采样数必须匹配（GLES 3.0 限制）
 * - 混合渲染缓冲区和纹理需要 GLES 3.1+ 或 EXT_multisampled_render_to_texture
 */
void OpenGLDriver::createRenderTargetR(Handle<HwRenderTarget> rth,
        TargetBufferFlags const targets,
        uint32_t width,
        uint32_t height,
        uint8_t samples,
        uint8_t const layerCount,
        MRT color,
        TargetBufferInfo depth,
        TargetBufferInfo stencil,
        ImmutableCString&& tag) {
    DEBUG_MARKER()

    GLRenderTarget* rt = construct<GLRenderTarget>(rth, width, height);
    // 生成 Framebuffer 对象（FBO）
    glGenFramebuffers(1, &rt->gl.fbo);

    /*
     * The GLES 3.0 spec states:
     *
     *                             --------------
     *
     * GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE is returned
     * - if the value of GL_RENDERBUFFER_SAMPLES is not the same for all attached renderbuffers or,
     * - if the attached images are a mix of renderbuffers and textures,
     *      the value of GL_RENDERBUFFER_SAMPLES is not zero.
     *
     * GLES 3.1 (spec, refpages are wrong) and EXT_multisampled_render_to_texture add:
     *
     * The value of RENDERBUFFER_SAMPLES is the same for all
     *    attached renderbuffers; the value of TEXTURE_SAMPLES
     *    is the same for all texture attachments; and, if the attached
     *    images are a mix of renderbuffers and textures, the value of
     *    RENDERBUFFER_SAMPLES matches the value of TEXTURE_SAMPLES.
     *
     *
     * In other words, heterogeneous (renderbuffer/textures) attachments are not supported in
     * GLES3.0, unless EXT_multisampled_render_to_texture is present.
     *
     * 'features.multisample_texture' below is a proxy for "GLES3.1 or GL4.x".
     *
     *                             --------------
     *
     * About the size of the attachments:
     *
     *  If the attachment sizes are not all identical, the results of rendering are defined only
     *  within the largest area that can fit in all the attachments. This area is defined as
     *  the intersection of rectangles having a lower left of (0, 0) and an upper right of
     *  (width, height) for each attachment. Contents of attachments outside this area are
     *  undefined after execution of a rendering command.
     */

    samples = std::clamp(samples, uint8_t(1u), uint8_t(mContext.gets.max_samples));

    rt->gl.samples = samples;
    rt->targets = targets;

    UTILS_UNUSED_IN_RELEASE vec2<uint32_t> tmin = { std::numeric_limits<uint32_t>::max() };
    UTILS_UNUSED_IN_RELEASE vec2<uint32_t> tmax = { 0 };
    auto checkDimensions = [&tmin, &tmax](GLTexture const* t, uint8_t const level) {
        const auto twidth = std::max(1u, t->width >> level);
        const auto theight = std::max(1u, t->height >> level);
        tmin = { std::min(tmin.x, twidth), std::min(tmin.y, theight) };
        tmax = { std::max(tmax.x, twidth), std::max(tmax.y, theight) };
    };


#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    if (any(targets & TargetBufferFlags::COLOR_ALL)) {
        GLenum bufs[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT] = { GL_NONE };
        const size_t maxDrawBuffers = getMaxDrawBuffers();
        for (size_t i = 0; i < maxDrawBuffers; i++) {
            if (any(targets & getTargetBufferFlagsAt(i))) {
                assert_invariant(color[i].handle);
                rt->gl.color[i] = handle_cast<GLTexture*>(color[i].handle);
                framebufferTexture(color[i], rt, GL_COLOR_ATTACHMENT0 + i, layerCount);
                bufs[i] = GL_COLOR_ATTACHMENT0 + i;
                checkDimensions(rt->gl.color[i], color[i].level);
            }
        }
        if (UTILS_LIKELY(!getContext().isES2())) {
            glDrawBuffers(GLsizei(maxDrawBuffers), bufs);
        }
        CHECK_GL_ERROR()
    }
#endif

    // handle special cases first (where depth/stencil are packed)
    bool specialCased = false;

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    if (!getContext().isES2() &&
            (targets & TargetBufferFlags::DEPTH_AND_STENCIL) == TargetBufferFlags::DEPTH_AND_STENCIL) {
        assert_invariant(depth.handle);
        // either we supplied only the depth handle or both depth/stencil are identical and not null
        if (depth.handle && (stencil.handle == depth.handle || !stencil.handle)) {
            rt->gl.depth = handle_cast<GLTexture*>(depth.handle);
            framebufferTexture(depth, rt, GL_DEPTH_STENCIL_ATTACHMENT, layerCount);
            specialCased = true;
            checkDimensions(rt->gl.depth, depth.level);
        }
    }
#endif

    if (!specialCased) {
        if (any(targets & TargetBufferFlags::DEPTH)) {
            assert_invariant(depth.handle);
            rt->gl.depth = handle_cast<GLTexture*>(depth.handle);
            framebufferTexture(depth, rt, GL_DEPTH_ATTACHMENT, layerCount);
            checkDimensions(rt->gl.depth, depth.level);
        }
        if (any(targets & TargetBufferFlags::STENCIL)) {
            assert_invariant(stencil.handle);
            rt->gl.stencil = handle_cast<GLTexture*>(stencil.handle);
            framebufferTexture(stencil, rt, GL_STENCIL_ATTACHMENT, layerCount);
            checkDimensions(rt->gl.stencil, stencil.level);
        }
    }

    // Verify that all attachments have the same dimensions.
    assert_invariant(any(targets & TargetBufferFlags::ALL));
    assert_invariant(tmin == tmax);

    CHECK_GL_ERROR()
    mHandleAllocator.associateTagToHandle(rth.getId(), std::move(tag));
}

/**
 * 创建栅栏（渲染线程）
 * 
 * 在渲染线程中创建栅栏对象，用于 CPU-GPU 同步。
 * 
 * @param fh 栅栏句柄（已在主线程分配）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 关联调试标签到句柄
 * 2. 获取栅栏状态
 * 3. 如果平台支持栅栏或 ES 2.0：
 *    - 创建平台栅栏（如果支持）
 *    - 或设置状态为 ERROR（如果不支持）
 *    - 通知等待者
 * 4. 否则使用 OpenGL 栅栏（ES 3.0+/GL 4.1+）：
 *    - 使用弱引用保存状态（因为用户可能在返回后销毁栅栏）
 *    - 调度回调，在 GPU 完成时设置状态为 CONDITION_SATISFIED
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 返回后用户可能立即销毁栅栏，所以使用弱引用
 * - 平台栅栏可能异步创建
 */
void OpenGLDriver::createFenceR(Handle<HwFence> fh, ImmutableCString&& tag) {
    DEBUG_MARKER()

    mHandleAllocator.associateTagToHandle(fh.getId(), std::move(tag));

    GLFence* const f = handle_cast<GLFence*>(fh);
    assert_invariant(f->state);

    bool const platformCanCreateFence = mPlatform.canCreateFence();

    // 如果平台支持栅栏或 ES 2.0
    if (mContext.isES2() || platformCanCreateFence) {
        std::lock_guard const lock(f->state->lock);
        if (platformCanCreateFence) {
            // 创建平台栅栏
            f->fence = mPlatform.createFence();
            f->state->cond.notify_all();
        } else {
            // 不支持栅栏，设置错误状态
            f->state->status = FenceStatus::ERROR;
        }
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 这是需要使用 OpenGL 栅栏的情况
    // 一旦返回，用户就可以销毁栅栏，所以我们需要保持对内部状态的引用
    std::weak_ptr<GLFence::State> const weak = f->state;
    whenGpuCommandsComplete([weak] {
        if (auto const state = weak.lock()) {
            std::lock_guard const lock(state->lock);
            state->status = FenceStatus::CONDITION_SATISFIED;
            state->cond.notify_all();
        }
    });
#endif
}

/**
 * 创建同步对象（渲染线程）
 * 
 * 在渲染线程中创建同步对象，用于更细粒度的 GPU 同步。
 * 同步对象从 GLsync 转换为平台同步对象。
 * 
 * @param sh 同步对象句柄（已在主线程分配）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 创建平台同步对象（从 GLsync 转换）
 * 2. 处理转换回调（在同步对象创建前注册的回调）：
 *    - 设置同步对象句柄
 *    - 调度回调（在 GPU 完成时调用）
 * 3. 清空转换回调列表
 * 4. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 同步对象从 GLsync 转换为平台同步对象（异步）
 * - 在转换前注册的回调会在转换完成后调用
 */
void OpenGLDriver::createSyncR(Handle<HwSync> sh, ImmutableCString&& tag) {
    DEBUG_MARKER()

    GLSyncFence* s = handle_cast<GLSyncFence*>(sh);
    {
        std::lock_guard const guard(s->lock);
        // 创建平台同步对象（从 GLsync 转换）
        s->sync = mPlatform.createSync();
    }

    // 处理转换回调（在同步对象创建前注册的回调）
    for (auto& cbData : s->conversionCallbacks) {
        cbData->sync = s->sync;
        scheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
    }

    s->conversionCallbacks.clear();
    mHandleAllocator.associateTagToHandle(sh.getId(), std::move(tag));
}

/**
 * 创建交换链（渲染线程）
 * 
 * 在渲染线程中创建交换链对象，用于管理前后缓冲区的交换。
 * 
 * @param sch 交换链句柄（已在主线程分配）
 * @param nativeWindow 原生窗口句柄（平台特定，如 HWND、NSWindow 等）
 * @param flags 交换链配置标志（SRGB_COLORSPACE、TRIPLE_BUFFERING 等）
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 通过平台接口创建交换链
 * 2. 验证创建成功（非 Emscripten）
 * 3. ES 2.0 特殊处理：检查是否需要模拟 rec709 输出转换
 *    - 如果请求 sRGB 但平台不支持，启用模拟
 * 4. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 交换链管理前后缓冲区的交换和呈现
 * - ES 2.0 不支持 sRGB 交换链时，需要模拟 rec709 转换
 */
void OpenGLDriver::createSwapChainR(Handle<HwSwapChain> sch, void* nativeWindow, uint64_t const flags,
        ImmutableCString&& tag) {
    DEBUG_MARKER()

    GLSwapChain* sc = handle_cast<GLSwapChain*>(sch);
    // 通过平台接口创建交换链
    sc->swapChain = mPlatform.createSwapChain(nativeWindow, flags);

#if !defined(__EMSCRIPTEN__)
    // 注意：实际上这在 Android 上不应该发生
    FILAMENT_CHECK_POSTCONDITION(sc->swapChain) << "createSwapChain(" << nativeWindow << ", "
                                                << flags << ") failed. See logs for details.";
#endif

    // 检查是否需要模拟 rec709 输出转换
    if (UTILS_UNLIKELY(mContext.isES2())) {
        sc->rec709 = ((flags & SWAP_CHAIN_CONFIG_SRGB_COLORSPACE) &&
                !mPlatform.isSRGBSwapChainSupported());
    }

    mHandleAllocator.associateTagToHandle(sch.getId(), std::move(tag));
}

/**
 * 创建无头交换链（渲染线程）
 * 
 * 在渲染线程中创建无头交换链对象，用于离屏渲染（不显示到屏幕）。
 * 
 * @param sch 交换链句柄（已在主线程分配）
 * @param width 交换链宽度
 * @param height 交换链高度
 * @param flags 交换链配置标志
 * @param tag 调试标签
 * 
 * 执行流程：
 * 1. 通过平台接口创建无头交换链（指定尺寸）
 * 2. 验证创建成功
 * 3. ES 2.0 特殊处理：检查是否需要模拟 rec709 输出转换
 * 4. 关联调试标签到句柄
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 无头交换链用于离屏渲染（截图、视频录制等）
 * - 与 createSwapChainR 类似，但不使用原生窗口
 */
void OpenGLDriver::createSwapChainHeadlessR(Handle<HwSwapChain> sch,
        uint32_t const width, uint32_t const height, uint64_t const flags, ImmutableCString&& tag) {
    DEBUG_MARKER()

    GLSwapChain* sc = handle_cast<GLSwapChain*>(sch);
    // 通过平台接口创建无头交换链（指定尺寸）
    sc->swapChain = mPlatform.createSwapChain(width, height, flags);

#if !defined(__EMSCRIPTEN__)
    // 注意：实际上这在 Android 上不应该发生
    FILAMENT_CHECK_POSTCONDITION(sc->swapChain)
            << "createSwapChainHeadless(" << width << ", " << height << ", " << flags
            << ") failed. See logs for details.";
#endif

    // 检查是否需要模拟 rec709 输出转换
    if (UTILS_UNLIKELY(mContext.isES2())) {
        sc->rec709 = (flags & SWAP_CHAIN_CONFIG_SRGB_COLORSPACE &&
                      !mPlatform.isSRGBSwapChainSupported());
    }

    mHandleAllocator.associateTagToHandle(sch.getId(), std::move(tag));
}

/**
 * 创建定时器查询（渲染线程）
 * 
 * 在渲染线程中创建定时器查询对象，用于测量 GPU 执行时间。
 * 
 * @param tqh 定时器查询句柄（已在主线程分配）
 * @param tag 调试标签
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 定时器查询用于性能分析
 */
void OpenGLDriver::createTimerQueryR(Handle<HwTimerQuery> tqh, ImmutableCString&& tag) {
    DEBUG_MARKER()
    GLTimerQuery* tq = handle_cast<GLTimerQuery*>(tqh);
    mContext.createTimerQuery(tq);
    mHandleAllocator.associateTagToHandle(tqh.getId(), std::move(tag));
}

/**
 * 创建描述符集布局（渲染线程）
 * 
 * 在渲染线程中创建描述符集布局对象。
 * 描述符集布局定义了描述符集的绑定结构（哪些槽位绑定什么类型的资源）。
 * 
 * @param dslh 描述符集布局句柄（已在主线程分配）
 * @param info 描述符集布局信息（绑定数组）
 * @param tag 调试标签
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 描述符集布局定义资源绑定结构
 */
void OpenGLDriver::createDescriptorSetLayoutR(Handle<HwDescriptorSetLayout> dslh,
        DescriptorSetLayout&& info, ImmutableCString&& tag) {
    DEBUG_MARKER()
    construct<GLDescriptorSetLayout>(dslh, std::move(info));
    mHandleAllocator.associateTagToHandle(dslh.getId(), std::move(tag));
}

/**
 * 创建描述符集（渲染线程）
 * 
 * 在渲染线程中创建描述符集对象。
 * 描述符集包含实际的资源绑定（纹理、缓冲区等）。
 * 
 * @param dsh 描述符集句柄（已在主线程分配）
 * @param dslh 描述符集布局句柄（定义绑定结构）
 * @param tag 调试标签
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 描述符集基于描述符集布局创建
 * - 资源绑定通过 updateDescriptorSet* 方法设置
 */
void OpenGLDriver::createDescriptorSetR(Handle<HwDescriptorSet> dsh,
        Handle<HwDescriptorSetLayout> dslh, ImmutableCString&& tag) {
    DEBUG_MARKER()
    GLDescriptorSetLayout const* dsl = handle_cast<GLDescriptorSetLayout*>(dslh);
    construct<GLDescriptorSet>(dsh, mContext, dslh, dsl);
    mHandleAllocator.associateTagToHandle(dslh.getId(), std::move(tag));
}

/**
 * 映射缓冲区（渲染线程）
 * 
 * 在渲染线程中创建内存映射缓冲区对象。
 * 内存映射缓冲区允许 CPU 直接访问 GPU 缓冲区。
 * 
 * @param mmbh 内存映射缓冲区句柄（已在主线程分配）
 * @param boh 缓冲区对象句柄（要映射的缓冲区）
 * @param offset 映射偏移（字节）
 * @param size 映射大小（字节）
 * @param access 访问标志（READ/WRITE）
 * @param tag 调试标签
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 内存映射允许 CPU 直接访问 GPU 缓冲区
 * - 映射期间缓冲区可能被锁定
 */
void OpenGLDriver::mapBufferR(MemoryMappedBufferHandle mmbh,
        BufferObjectHandle boh, size_t offset,
        size_t size, MapBufferAccessFlags access, ImmutableCString&& tag) {
    DEBUG_MARKER()
    construct<GLMemoryMappedBuffer>(mmbh, mContext, mHandleAllocator, boh, offset, size, access);
    mHandleAllocator.associateTagToHandle(mmbh.getId(), std::move(tag));
}

// ------------------------------------------------------------------------------------------------
// 销毁驱动对象
// ------------------------------------------------------------------------------------------------

/**
 * 销毁顶点缓冲区信息
 * 
 * 销毁顶点缓冲区信息对象，释放相关资源。
 * 
 * @param vbih 顶点缓冲区信息句柄
 */
void OpenGLDriver::destroyVertexBufferInfo(Handle<HwVertexBufferInfo> vbih) {
    DEBUG_MARKER()
    if (vbih) {
        GLVertexBufferInfo const* vbi = handle_cast<const GLVertexBufferInfo*>(vbih);
        destruct(vbih, vbi);
    }
}

/**
 * 销毁顶点缓冲区
 * 
 * 销毁顶点缓冲区对象，释放相关资源。
 * 注意：只销毁对象，不销毁缓冲区对象（通过 destroyBufferObject）。
 * 
 * @param vbh 顶点缓冲区句柄
 */
void OpenGLDriver::destroyVertexBuffer(Handle<HwVertexBuffer> vbh) {
    DEBUG_MARKER()
    if (vbh) {
        GLVertexBuffer const* vb = handle_cast<const GLVertexBuffer*>(vbh);
        destruct(vbh, vb);
    }
}

/**
 * 销毁索引缓冲区
 * 
 * 销毁索引缓冲区对象并释放 OpenGL 缓冲区。
 * 
 * @param ibh 索引缓冲区句柄
 * 
 * 执行流程：
 * 1. 删除 OpenGL 缓冲区对象
 * 2. 销毁对象
 */
void OpenGLDriver::destroyIndexBuffer(Handle<HwIndexBuffer> ibh) {
    DEBUG_MARKER()

    if (ibh) {
        auto& gl = mContext;
        GLIndexBuffer const* ib = handle_cast<const GLIndexBuffer*>(ibh);
        // 删除 OpenGL 缓冲区对象
        gl.deleteBuffer(ib->gl.buffer, GL_ELEMENT_ARRAY_BUFFER);
        destruct(ibh, ib);
    }
}

/**
 * 销毁缓冲区对象
 * 
 * 销毁缓冲区对象并释放 OpenGL 缓冲区或系统内存。
 * 
 * @param boh 缓冲区对象句柄
 * 
 * 执行流程：
 * 1. 验证没有活动的映射（mappingCount == 0）
 * 2. ES 2.0 特殊处理（uniform 缓冲区使用系统内存）：
 *    - 释放系统内存
 * 3. 其他情况：
 *    - 删除 OpenGL 缓冲区对象
 * 4. 销毁对象
 * 
 * 注意：
 * - 不能销毁有活动映射的缓冲区
 * - ES 2.0 的 uniform 缓冲区使用系统内存，需要 free
 */
void OpenGLDriver::destroyBufferObject(Handle<HwBufferObject> boh) {
    DEBUG_MARKER()
    if (boh) {
        auto& gl = mContext;
        GLBufferObject const* bo = handle_cast<const GLBufferObject*>(boh);
        // 检查我们不是在销毁有活动映射的缓冲区
        assert_invariant(bo->mappingCount == 0);
        
        // ES 2.0 特殊处理：uniform 缓冲区使用系统内存
        if (UTILS_UNLIKELY(bo->bindingType == BufferObjectBinding::UNIFORM && gl.isES2())) {
            free(bo->gl.buffer);
        } else {
            // 删除 OpenGL 缓冲区对象
            gl.deleteBuffer(bo->gl.id, bo->gl.binding);
        }
        destruct(boh, bo);
    }
}

/**
 * 销毁渲染图元
 * 
 * 销毁渲染图元对象并释放 VAO。
 * 
 * @param rph 渲染图元句柄
 * 
 * 执行流程：
 * 1. 删除当前上下文的 VAO
 * 2. 如果其他上下文也有 VAO，调度延迟销毁：
 *    - VAO 是"容器对象"，不在上下文间共享
 *    - 必须在对应的上下文中删除
 * 3. 销毁对象
 * 
 * 注意：
 * - VAO 不在上下文间共享，每个上下文需要单独删除
 * - 其他上下文的 VAO 会延迟销毁
 */
void OpenGLDriver::destroyRenderPrimitive(Handle<HwRenderPrimitive> rph) {
    DEBUG_MARKER()

    if (rph) {
        auto& gl = mContext;
        GLRenderPrimitive const* rp = handle_cast<const GLRenderPrimitive*>(rph);
        // 删除当前上下文的 VAO
        gl.deleteVertexArray(rp->gl.vao[gl.contextIndex]);

        // 如果其他上下文也有 VAO，我们需要调度延迟销毁
        // 因为不能在这里完成。VAO 是"容器对象"，不在上下文间共享
        size_t const otherContextIndex = 1 - gl.contextIndex;
        GLuint const nameInOtherContext = rp->gl.vao[otherContextIndex];
        if (UTILS_UNLIKELY(nameInOtherContext)) {
            // 调度在其他上下文中删除 VAO
            gl.destroyWithContext(otherContextIndex,
                    [name = nameInOtherContext](OpenGLContext& gl) {
                gl.deleteVertexArray(name);
            });
        }

        destruct(rph, rp);
    }
}

/**
 * 销毁着色器程序
 * 
 * 销毁着色器程序对象，释放相关资源（着色器对象、程序对象等）。
 * 
 * @param ph 着色器程序句柄
 * 
 * 注意：
 * - 着色器对象的释放由 OpenGLProgram 析构函数处理
 */
void OpenGLDriver::destroyProgram(Handle<HwProgram> ph) {
    DEBUG_MARKER()
    if (ph) {
        OpenGLProgram const* p = handle_cast<OpenGLProgram*>(ph);
        destruct(ph, p);
    }
}

/**
 * 销毁纹理
 * 
 * 销毁纹理对象并释放 OpenGL 纹理或渲染缓冲区。
 * 处理纹理视图的引用计数和共享纹理的生命周期。
 * 
 * @param th 纹理句柄
 * 
 * 执行流程：
 * 1. 如果是导入纹理：
 *    - 解绑纹理（不删除，外部负责生命周期）
 *    - 销毁对象
 * 2. 如果是可采样纹理：
 *    - 减少引用计数（如果有纹理视图）
 *    - 如果引用计数为 0（最后一个引用）：
 *      * 解绑纹理
 *      * 如果附加了流，分离流
 *      * 如果是外部纹理，销毁外部纹理对象
 *      * 否则删除 OpenGL 纹理对象
 *    - 如果引用计数 > 0（还有纹理视图）：
 *      * 只销毁句柄，不删除 OpenGL 纹理（共享）
 * 3. 如果是渲染缓冲区：
 *    - 删除渲染缓冲区对象
 * 4. 删除 sidecar 渲染缓冲区（MSAA 相关）
 * 5. 销毁对象
 * 
 * 注意：
 * - 纹理视图共享底层纹理数据，使用引用计数管理
 * - 导入纹理不归 Filament 管理，只解绑不删除
 * - sidecar 渲染缓冲区用于 MSAA 模拟
 */
void OpenGLDriver::destroyTexture(Handle<HwTexture> th) {
    DEBUG_MARKER()

    if (th) {
        auto& gl = mContext;
        GLTexture* t = handle_cast<GLTexture*>(th);

        // 如果不是导入纹理
        if (UTILS_LIKELY(!t->gl.imported)) {
            // 如果是可采样纹理
            if (UTILS_LIKELY(t->usage & TextureUsage::SAMPLEABLE)) {
                // 减少引用计数（如果有纹理视图）
                uint16_t count = 0;
                if (UTILS_UNLIKELY(t->ref)) {
                    // 常见情况是我们没有 ref 句柄
                    GLTextureRef* const ref = handle_cast<GLTextureRef*>(t->ref);
                    count = --(ref->count);
                    if (count == 0) {
                        destruct(t->ref, ref);
                    }
                }
                // 如果这是最后一个引用，销毁引用计数以及 GL 纹理名称本身
                if (count == 0) {
                    gl.unbindTexture(t->gl.target, t->gl.id);
                    // 如果附加了流，分离流
                    if (UTILS_UNLIKELY(t->hwStream)) {
                        detachStream(t);
                    }
                    // 如果是外部纹理，销毁外部纹理对象
                    if (UTILS_UNLIKELY(t->externalTexture)) {
                        mPlatform.destroyExternalImageTexture(t->externalTexture);
                    } else {
                        // 否则删除 OpenGL 纹理对象
                        glDeleteTextures(1, &t->gl.id);
                    }
                } else {
                    // Handle<HwTexture> 总是被销毁。为了额外的预防措施，我们还
                    // 检查 GLTexture 是否有平凡的析构函数
                    static_assert(std::is_trivially_destructible_v<GLTexture>);
                }
            } else {
                // 如果是渲染缓冲区
                assert_invariant(t->gl.target == GL_RENDERBUFFER);
                glDeleteRenderbuffers(1, &t->gl.id);
            }
            // 删除 sidecar 渲染缓冲区（MSAA 相关）
            if (t->gl.sidecarRenderBufferMS) {
                glDeleteRenderbuffers(1, &t->gl.sidecarRenderBufferMS);
            }
        } else {
            // 导入纹理：只解绑，不删除（外部负责生命周期）
            gl.unbindTexture(t->gl.target, t->gl.id);
        }
        destruct(th, t);
    }
}

/**
 * 销毁渲染目标
 * 
 * 销毁渲染目标对象并释放 Framebuffer 对象（FBO）。
 * 
 * @param rth 渲染目标句柄
 * 
 * 执行流程：
 * 1. 如果 FBO 已绑定，解绑（避免删除已绑定的 FBO）
 * 2. 处理驱动 bug（延迟 FBO 销毁）：
 *    - 某些驱动在帧完成前删除 FBO 会导致问题
 *    - 调度延迟销毁（在帧完成后删除）
 * 3. 否则立即删除 FBO：
 *    - 删除主 FBO（rt->gl.fbo）
 *    - 删除读取 FBO（rt->gl.fbo_read，用于 MSAA resolve）
 * 4. 销毁对象
 * 
 * 注意：
 * - 某些驱动需要延迟 FBO 销毁（在帧完成后）
 * - fbo_read 用于 MSAA resolve（EXT_multisampled_render_to_texture 模拟）
 */
void OpenGLDriver::destroyRenderTarget(Handle<HwRenderTarget> rth) {
    DEBUG_MARKER()

    if (rth) {
        auto& gl = mContext;
        GLRenderTarget const* rt = handle_cast<GLRenderTarget*>(rth);
        // 如果 FBO 已绑定，先解绑（避免删除已绑定的 FBO）
        if (rt->gl.fbo) {
            gl.unbindFramebuffer(GL_FRAMEBUFFER);
        }
        if (rt->gl.fbo_read) {
            gl.unbindFramebuffer(GL_FRAMEBUFFER);
        }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        // 处理驱动 bug：某些驱动在帧完成前删除 FBO 会导致问题
        if (UTILS_UNLIKELY(gl.bugs.delay_fbo_destruction)) {
            // 调度延迟销毁（在帧完成后删除）
            if (rt->gl.fbo) {
                whenFrameComplete([fbo = rt->gl.fbo]() {
                    glDeleteFramebuffers(1, &fbo);
                });
            }
            if (rt->gl.fbo_read) {
                whenFrameComplete([fbo_read = rt->gl.fbo_read]() {
                    glDeleteFramebuffers(1, &fbo_read);
                });
            }
        } else
#endif
        {
            // 立即删除 FBO
            if (rt->gl.fbo) {
                glDeleteFramebuffers(1, &rt->gl.fbo);
            }
            if (rt->gl.fbo_read) {
                glDeleteFramebuffers(1, &rt->gl.fbo_read);
            }
        }
        destruct(rth, rt);
    }
}

/**
 * 销毁交换链
 * 
 * 销毁交换链对象并释放平台交换链。
 * 
 * @param sch 交换链句柄
 */
void OpenGLDriver::destroySwapChain(Handle<HwSwapChain> sch) {
    DEBUG_MARKER()

    if (sch) {
        GLSwapChain const* sc = handle_cast<GLSwapChain*>(sch);
        mPlatform.destroySwapChain(sc->swapChain);
        destruct(sch, sc);
    }
}

/**
 * 销毁流
 * 
 * 销毁流对象并释放相关资源。
 * 
 * @param sh 流句柄
 * 
 * 执行流程：
 * 1. 如果流仍附加到纹理，先分离（避免悬空指针）
 * 2. 如果是 NATIVE 流，销毁平台流对象
 * 3. 销毁句柄
 * 
 * 注意：
 * - 只有 NATIVE 流有 Platform::Stream 关联
 * - ACQUIRED 流没有平台对象
 */
void OpenGLDriver::destroyStream(Handle<HwStream> sh) {
    DEBUG_MARKER()

    if (sh) {
        GLStream const* s = handle_cast<GLStream*>(sh);

        // 如果流仍附加到纹理，先分离
        auto& texturesWithStreamsAttached = mTexturesWithStreamsAttached;
        auto const pos = std::find_if(
                texturesWithStreamsAttached.begin(), texturesWithStreamsAttached.end(),
                [s](GLTexture const* t) {
                    return t->hwStream == s;
                });

        if (pos != texturesWithStreamsAttached.end()) {
            detachStream(*pos);
        }

        // 然后销毁流。只有 NATIVE 流有 Platform::Stream 关联
        if (s->streamType == StreamType::NATIVE) {
            mPlatform.destroyStream(s->stream);
        }

        // 最后销毁 HwStream 句柄
        destruct(sh, s);
    }
}

/**
 * 销毁同步对象
 * 
 * 销毁同步对象并释放平台同步对象。
 * 
 * @param sh 同步对象句柄
 */
void OpenGLDriver::destroySync(Handle<HwSync> sh) {
    DEBUG_MARKER()

    if (sh) {
        GLSyncFence const* s = handle_cast<GLSyncFence*>(sh);
        mPlatform.destroySync(s->sync);
        destruct(sh, s);
    }
}

/**
 * 销毁定时器查询
 * 
 * 销毁定时器查询对象并释放相关资源。
 * 
 * @param tqh 定时器查询句柄
 */
void OpenGLDriver::destroyTimerQuery(Handle<HwTimerQuery> tqh) {
    DEBUG_MARKER()

    if (tqh) {
        GLTimerQuery* tq = handle_cast<GLTimerQuery*>(tqh);
        mContext.destroyTimerQuery(tq);
        destruct(tqh, tq);
    }
}

/**
 * 销毁描述符集布局
 * 
 * 销毁描述符集布局对象。
 * 
 * @param dslh 描述符集布局句柄
 */
void OpenGLDriver::destroyDescriptorSetLayout(Handle<HwDescriptorSetLayout> dslh) {
    DEBUG_MARKER()
    if (dslh) {
        GLDescriptorSetLayout const* dsl = handle_cast<GLDescriptorSetLayout*>(dslh);
        destruct(dslh, dsl);
    }
}

/**
 * 销毁描述符集
 * 
 * 销毁描述符集对象。
 * 
 * @param dsh 描述符集句柄
 * 
 * 执行流程：
 * 1. 解绑描述符集（避免 use-after-free）
 * 2. 销毁对象
 */
void OpenGLDriver::destroyDescriptorSet(Handle<HwDescriptorSet> dsh) {
    DEBUG_MARKER()
    if (dsh) {
        // 解绑描述符集，避免 use-after-free
        for (auto& bound : mBoundDescriptorSets) {
            if (bound.dsh == dsh) {
                bound = {};
            }
        }
        GLDescriptorSet const* ds = handle_cast<GLDescriptorSet*>(dsh);
        destruct(dsh, ds);
    }
}

/**
 * 取消映射缓冲区
 * 
 * 取消内存映射缓冲区，释放映射并销毁对象。
 * 
 * @param mmbh 内存映射缓冲区句柄
 */
void OpenGLDriver::unmapBuffer(MemoryMappedBufferHandle mmbh) {
    DEBUG_MARKER()
    if (mmbh) {
        GLMemoryMappedBuffer* const mmb = handle_cast<GLMemoryMappedBuffer*>(mmbh);
        mmb->unmap(mContext, mHandleAllocator);
        destruct(mmbh, mmb);
    }
}

// ------------------------------------------------------------------------------------------------
// Synchronous APIs
// These are called on the application's thread
// ------------------------------------------------------------------------------------------------

Handle<HwStream> OpenGLDriver::createStreamNative(void* nativeStream, ImmutableCString tag) {
    Platform::Stream* stream = mPlatform.createStream(nativeStream);
    auto handle = initHandle<GLStream>(stream);
    mHandleAllocator.associateTagToHandle(handle.getId(), std::move(tag));
    return handle;
}

Handle<HwStream> OpenGLDriver::createStreamAcquired(ImmutableCString tag) {
    auto handle = initHandle<GLStream>();
    mHandleAllocator.associateTagToHandle(handle.getId(), std::move(tag));
    return handle;
}

// Stashes an acquired external image and a release callback. The image is not bound to OpenGL until
// the subsequent call to beginFrame (see updateStreamAcquired).
//
// setAcquiredImage should be called by the user outside of beginFrame / endFrame, and should be
// called only once per frame. If the user pushes images to the same stream multiple times in a
// single frame, we emit a warning and honor only the final image, but still invoke all callbacks.
void OpenGLDriver::setAcquiredImage(Handle<HwStream> sh, void* hwbuffer, const mat3f& transform,
        CallbackHandler* handler, StreamCallback const cb, void* userData) {
    GLStream* glstream = handle_cast<GLStream*>(sh);
    assert_invariant(glstream->streamType == StreamType::ACQUIRED);

    if (UTILS_UNLIKELY(glstream->user_thread.pending.image)) {
        scheduleRelease(glstream->user_thread.pending);
        LOG(WARNING) << "Acquired image is set more than once per frame.";
    }

    glstream->user_thread.pending = mPlatform.transformAcquiredImage({
            hwbuffer, cb, userData, handler });
    glstream->user_thread.transform = transform;

    if (glstream->user_thread.pending.image != nullptr) {
        // If there's no pending image, do nothing. Note that GL_OES_EGL_image does not let you pass
        // NULL to glEGLImageTargetTexture2DOES, and there is no concept of "detaching" an
        // EGLimage from a texture.
        mStreamsWithPendingAcquiredImage.push_back(glstream);
    }
}

// updateStreams() and setAcquiredImage() are both called from on the application's thread
// and therefore do not require synchronization. The former is always called immediately before
// beginFrame, the latter is called by the user from anywhere outside beginFrame / endFrame.
void OpenGLDriver::updateStreams(DriverApi* driver) {
    if (UTILS_UNLIKELY(!mStreamsWithPendingAcquiredImage.empty())) {
        for (GLStream* s : mStreamsWithPendingAcquiredImage) {
            assert_invariant(s);
            assert_invariant(s->streamType == StreamType::ACQUIRED);

            AcquiredImage const previousImage = s->user_thread.acquired;
            s->user_thread.acquired = s->user_thread.pending;
            s->user_thread.pending = { nullptr };

            // Bind the stashed EGLImage to its corresponding GL texture as soon as we start
            // making the GL calls for the upcoming frame.
            driver->queueCommand([this, s, image = s->user_thread.acquired.image, previousImage, transform = s->user_thread.transform]() {

                auto& streams = mTexturesWithStreamsAttached;
                auto const pos = std::find_if(streams.begin(), streams.end(),
                        [s](GLTexture const* t) {
                            return t->hwStream == s;
                        });
                if (pos != streams.end()) {
                    GLTexture* t = *pos;
                    bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
                    if (mPlatform.setExternalImage(image, t->externalTexture)) {
                        // the target and id can be reset each time
                        t->gl.target = t->externalTexture->target;
                        t->gl.id = t->externalTexture->id;
                        bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
                        s->transform = transform;
                    }
                }

                if (previousImage.image) {
                    scheduleRelease(AcquiredImage(previousImage));
                }
            });
        }
        mStreamsWithPendingAcquiredImage.clear();
    }
}

/**
 * 设置流尺寸
 * 
 * 设置外部流（如相机预览）的尺寸。
 * 
 * @param sh 流句柄
 * @param width 流宽度
 * @param height 流高度
 */
void OpenGLDriver::setStreamDimensions(Handle<HwStream> sh, uint32_t const width, uint32_t const height) {
    if (sh) {
        GLStream* s = handle_cast<GLStream*>(sh);
        s->width = width;
        s->height = height;
    }
}

/**
 * 获取流时间戳
 * 
 * 获取外部流（如相机预览）的当前帧时间戳。
 * 
 * @param sh 流句柄
 * @return 时间戳（纳秒），如果句柄无效返回 0
 */
int64_t OpenGLDriver::getStreamTimestamp(Handle<HwStream> sh) {
    if (sh) {
        GLStream const* s = handle_cast<GLStream*>(sh);
        return s->user_thread.timestamp;
    }
    return 0;
}

/**
 * 获取流变换矩阵
 * 
 * 获取外部流（如相机预览）的变换矩阵，用于处理旋转、翻转等。
 * 
 * @param sh 流句柄
 * @return 3x3 变换矩阵，如果句柄无效返回单位矩阵
 * 
 * 注意：
 * - NATIVE 流：从平台获取变换矩阵（可能包含旋转、翻转等）
 * - ACQUIRED 流：使用存储的变换矩阵
 */
mat3f OpenGLDriver::getStreamTransformMatrix(Handle<HwStream> sh) {
    if (sh) {
        GLStream const* s = handle_cast<GLStream*>(sh);
        if (s->streamType == StreamType::NATIVE) {
            return mPlatform.getTransformMatrix(s->stream);
        } else {
            return s->transform;
        }
    }
    return mat3f();
}

/**
 * 销毁栅栏
 * 
 * 销毁栅栏对象并释放相关资源。
 * 
 * @param fh 栅栏句柄
 * 
 * 注意：
 * - 在另一个线程调用 fenceWait(fh) 期间调用此方法是无效的
 * - 因此不需要通知等待者，应该没有等待者
 */
void OpenGLDriver::destroyFence(Handle<HwFence> fh) {
    if (fh) {
        GLFence const* const f = handle_cast<GLFence*>(fh);
        // 如果平台支持栅栏或 ES 2.0，销毁平台栅栏
        if (mPlatform.canCreateFence() || mContext.isES2()) {
            mPlatform.destroyFence(f->fence);
        }
        // 注意：在另一个线程调用 fenceWait(fh) 期间调用此方法是无效的
        // 因此不需要通知等待者，应该没有等待者
        destruct(fh, f);
    }
}

/**
 * 取消栅栏
 * 
 * 取消栅栏等待，将所有等待者唤醒并设置为错误状态。
 * 
 * @param fh 栅栏句柄（必须有效）
 * 
 * 执行流程：
 * 1. 验证句柄有效
 * 2. 获取栅栏状态
 * 3. 加锁
 * 4. 设置状态为 ERROR
 * 5. 通知所有等待者
 */
void OpenGLDriver::fenceCancel(FenceHandle fh) {
    // 即使这是同步调用，栅栏句柄必须（并保持）有效
    assert_invariant(fh);
    GLFence const* const f = handle_cast<GLFence*>(fh);
    assert_invariant(f->state);

    std::lock_guard const lock(f->state->lock);
    f->state->status = FenceStatus::ERROR;
    f->state->cond.notify_all();
}

/**
 * 获取栅栏状态
 * 
 * 非阻塞地查询栅栏状态。
 * 
 * @param fh 栅栏句柄
 * @return 栅栏状态（CONDITION_SATISFIED/TIMEOUT_EXPIRED/ERROR）
 */
FenceStatus OpenGLDriver::getFenceStatus(Handle<HwFence> fh) {
    return fenceWait(fh, 0);  // 超时 0 = 非阻塞查询
}

/**
 * 等待栅栏
 * 
 * 等待栅栏信号，直到条件满足或超时。
 * 
 * @param fh 栅栏句柄（必须有效）
 * @param timeout 超时时间（纳秒），0 表示非阻塞查询
 * @return 栅栏状态（CONDITION_SATISFIED/TIMEOUT_EXPIRED/ERROR）
 * 
 * 执行流程：
 * 1. 验证句柄有效
 * 2. 计算超时时间点
 * 3. 如果平台支持栅栏或 ES 2.0：
 *    - 等待平台栅栏创建（如果是异步创建）
 *    - 调用平台 waitFence
 * 4. 否则使用 OpenGL 栅栏：
 *    - 等待状态改变
 *    - 返回状态
 * 
 * 注意：
 * - 平台栅栏可能异步创建，需要等待创建完成
 * - ES 2.0 或平台不支持栅栏时返回 ERROR
 */
FenceStatus OpenGLDriver::fenceWait(FenceHandle fh, uint64_t const timeout) {
    // Even though this is a synchronous call, the fence handle must be (and stay) valid
    assert_invariant(fh);
    GLFence const* const f = handle_cast<GLFence*>(fh);
    assert_invariant(f->state);

    // we have to take into account that the STL's wait_for() actually works with
    // time_points relative to steady_clock::now() internally.
    using namespace std::chrono;
    auto const now = steady_clock::now();
    steady_clock::time_point until = steady_clock::time_point::max();
    if (now <= steady_clock::time_point::max() - nanoseconds(timeout)) {
        until = now + nanoseconds(timeout);
    }

    // we don't need to acquire a reference to f->state here because `f` already has one, and
    // `f` is not supposed to become invalid while we wait.

    bool const platformCanCreateFence = mPlatform.canCreateFence();
    if (mContext.isES2() || platformCanCreateFence) {
        if (platformCanCreateFence) {
            std::unique_lock lock(f->state->lock);
            if (f->fence == nullptr) {
                // we've been called before the fence was created asynchronously,
                // so we need to wait for that, before using the real fence.
                // By construction, "f" can't be destroyed while we wait, because its
                // construction call is in the queue and a destroy call will have to come later.
                f->state->cond.wait_until(lock, until, [f] {
                    return f->fence != nullptr;
                });
                if (f->fence == nullptr) {
                    // the only possible choice here is that we timed out
                    assert_invariant(f->state->status == FenceStatus::TIMEOUT_EXPIRED);
                    return FenceStatus::TIMEOUT_EXPIRED;
                }
            }
            lock.unlock();
            // here we know that we have the platform fence
            assert_invariant(f->fence);
            return mPlatform.waitFence(f->fence, timeout);
        }
        // platform doesn't support fences -- nothing we can do.
        return FenceStatus::ERROR;
    }

    // This is the case where we need to use OpenGL fences
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    std::unique_lock lock(f->state->lock);
    f->state->cond.wait_until(lock, until, [f] {
        return f->state->status != FenceStatus::TIMEOUT_EXPIRED;
    });
    return f->state->status;
#endif
}

/**
 * 获取平台同步对象
 * 
 * 获取 OpenGL 同步对象的平台句柄，并注册回调。
 * 当 GPU 完成同步对象时，回调会被调用。
 * 
 * @param sh 同步对象句柄
 * @param handler 回调处理器
 * @param cb 回调函数
 * @param userData 用户数据
 * 
 * 执行流程：
 * 1. 验证句柄有效
 * 2. 创建回调数据
 * 3. 如果同步对象尚未创建（异步创建）：
 *    - 将回调添加到转换回调列表
 *    - 等待同步对象创建后调用
 * 4. 如果同步对象已创建：
 *    - 立即调度回调
 * 
 * 注意：
 * - 同步对象可能异步创建（从 GLsync 转换为平台同步对象）
 * - 回调在 GPU 完成同步对象时调用
 */
void OpenGLDriver::getPlatformSync(Handle<HwSync> sh, CallbackHandler* handler,
        Platform::SyncCallback const cb, void* userData) {
    if (!sh) {
        return;
    }

    GLSyncFence* s = handle_cast<GLSyncFence*>(sh);
    auto cbData = std::make_unique<GLSyncFence::CallbackData>();
    cbData->handler = handler;
    cbData->cb = cb;
    cbData->userData = userData;

    // 如果同步对象尚未创建，将转换回调添加到列表末尾
    {
        std::lock_guard const guard(s->lock);
        if (s->sync == nullptr) {
            s->conversionCallbacks.push_back(std::move(cbData));
            return;
        }
    }

    // 否则，立即调度回调
    cbData->sync = s->sync;
    scheduleCallback(cbData->handler, cbData.release(), syncCallbackWrapper);
}

/**
 * 检查纹理格式是否支持
 * 
 * 检查指定的纹理格式是否在当前 OpenGL 上下文中支持。
 * 
 * @param format 纹理格式
 * @return 如果支持返回 true，否则返回 false
 * 
 * 检查逻辑：
 * 1. ETC2 压缩格式：检查 EXT_texture_compression_etc2 或 WEBGL_compressed_texture_etc
 * 2. S3TC sRGB 压缩格式：检查 WEBGL/ES/GL 扩展
 * 3. S3TC 压缩格式：检查 EXT_texture_compression_s3tc 或 WEBGL 扩展
 * 4. RGTC 压缩格式：检查 EXT_texture_compression_rgtc
 * 5. BPTC 压缩格式：检查 EXT_texture_compression_bptc
 * 6. ASTC 压缩格式：检查 KHR_texture_compression_astc_hdr
 * 7. ES 2.0：检查格式和类型是否有效
 * 8. ES 3.0+/GL 4.1+：检查内部格式是否有效
 */
bool OpenGLDriver::isTextureFormatSupported(TextureFormat const format) {
    const auto& ext = mContext.ext;
    if (isETC2Compression(format)) {
        return ext.EXT_texture_compression_etc2 ||
               ext.WEBGL_compressed_texture_etc; // WEBGL 特定，显然包含 ETC2
    }
    if (isS3TCSRGBCompression(format)) {
        // 参见 https://www.khronos.org/registry/OpenGL/extensions/EXT/EXT_texture_sRGB.txt
        return  ext.WEBGL_compressed_texture_s3tc_srgb || // WEBGL 特定
                ext.EXT_texture_compression_s3tc_srgb || // ES 特定
               (ext.EXT_texture_compression_s3tc && ext.EXT_texture_sRGB);
    }
    if (isS3TCCompression(format)) {
        return  ext.EXT_texture_compression_s3tc || // ES 特定
                ext.WEBGL_compressed_texture_s3tc; // WEBGL 特定
    }
    if (isRGTCCompression(format)) {
        return  ext.EXT_texture_compression_rgtc;
    }
    if (isBPTCCompression(format)) {
        return  ext.EXT_texture_compression_bptc;
    }
    if (isASTCCompression(format)) {
        return ext.KHR_texture_compression_astc_hdr;
    }
    // ES 2.0：检查格式和类型是否有效
    if (mContext.isES2()) {
        return textureFormatToFormatAndType(format).first != GL_NONE;
    }
    // ES 3.0+/GL 4.1+：检查内部格式是否有效
    return getInternalFormat(format) != 0;
}

/**
 * 检查纹理 Swizzle 是否支持
 * 
 * 检查当前 OpenGL 上下文是否支持纹理通道重映射（Swizzle）。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 支持情况：
 * - WebGL2：不支持（规范限制）
 * - OpenGL ES 2.0：不支持
 * - OpenGL ES 3.0+：支持
 * - 桌面 OpenGL：支持
 */
bool OpenGLDriver::isTextureSwizzleSupported() {
#if defined(__EMSCRIPTEN__)
    // WebGL2 不支持纹理 swizzle
    // 参见 https://registry.khronos.org/webgl/specs/latest/2.0/#5.19
    return false;
#elif defined(BACKEND_OPENGL_VERSION_GLES)
    return !mContext.isES2();  // ES 3.0+ 支持
#else
    return true;  // 桌面 OpenGL 支持
#endif
}

/**
 * 检查纹理格式是否支持 Mipmap
 * 
 * 检查指定的纹理格式是否支持生成 Mipmap。
 * 
 * @param format 纹理格式
 * @return 如果支持返回 true，否则返回 false
 * 
 * OpenGL 规范规定，GenerateMipmap 会返回 INVALID_OPERATION，除非
 * 内部格式既是颜色可渲染的，又是纹理可过滤的。
 * 
 * 深度/模板格式不支持 Mipmap（不是颜色可渲染的）。
 * 其他格式需要检查是否是渲染目标格式（颜色可渲染的）。
 */
bool OpenGLDriver::isTextureFormatMipmappable(TextureFormat const format) {
    // OpenGL 规范规定，GenerateMipmap 会返回 INVALID_OPERATION，除非
    // 内部格式既是颜色可渲染的，又是纹理可过滤的
    switch (format) {
        // 深度/模板格式不支持 Mipmap（不是颜色可渲染的）
        case TextureFormat::DEPTH16:
        case TextureFormat::DEPTH24:
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            return false;
        default:
            // 其他格式需要检查是否是渲染目标格式（颜色可渲染的）
            return isRenderTargetFormatSupported(format);
    }
}

/**
 * 检查渲染目标格式是否支持
 * 
 * 检查指定的纹理格式是否可以作为渲染目标（Framebuffer 附件）。
 * 
 * @param format 纹理格式
 * @return 如果支持返回 true，否则返回 false
 * 
 * 支持的格式（根据 http://docs.gl/es3/glRenderbufferStorage）：
 * - 核心格式：R8/RG8/RGB565/RGBA8/...（标准格式）
 * - 三分量 sRGB：SRGB8（需要 GL 4.5+）
 * - 半精度浮点：R16F/RG16F/RGBA16F（需要扩展）
 * - RGB16F：需要 EXT_color_buffer_half_float（WebGL 不支持）
 * - 浮点格式：R32F/RG32F/RGBA32F（需要 EXT_color_buffer_float）
 * - RGB_11_11_10：需要 EXT_color_buffer_float 或 APPLE_color_buffer_packed_float
 * 
 * 注意：
 * - 桌面 OpenGL 可能支持更多格式，但需要查询 GL_INTERNALFORMAT_SUPPORTED
 * - OpenGL ES 不支持 GL_INTERNALFORMAT_SUPPORTED
 * - ES 2.0 需要检查格式和类型是否有效
 */
bool OpenGLDriver::isRenderTargetFormatSupported(TextureFormat const format) {
    // Supported formats per http://docs.gl/es3/glRenderbufferStorage, note that desktop OpenGL may
    // support more formats, but it requires querying GL_INTERNALFORMAT_SUPPORTED which is not
    // available in OpenGL ES.
    auto const& gl = mContext;
    if (UTILS_UNLIKELY(gl.isES2())) {
        auto [es2format, type] = textureFormatToFormatAndType(format);
        return es2format != GL_NONE && type != GL_NONE;
    }
    switch (format) {
        // Core formats.
        case TextureFormat::R8:
        case TextureFormat::R8UI:
        case TextureFormat::R8I:
        case TextureFormat::STENCIL8:
        case TextureFormat::R16UI:
        case TextureFormat::R16I:
        case TextureFormat::RG8:
        case TextureFormat::RG8UI:
        case TextureFormat::RG8I:
        case TextureFormat::RGB565:
        case TextureFormat::RGB5_A1:
        case TextureFormat::RGBA4:
        case TextureFormat::DEPTH16:
        case TextureFormat::RGB8:
        case TextureFormat::DEPTH24:
        case TextureFormat::R32UI:
        case TextureFormat::R32I:
        case TextureFormat::RG16UI:
        case TextureFormat::RG16I:
        case TextureFormat::RGBA8:
        case TextureFormat::SRGB8_A8:
        case TextureFormat::RGB10_A2:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA8I:
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
        case TextureFormat::RG32UI:
        case TextureFormat::RG32I:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA16I:
            return true;

        // Three-component SRGB is a color-renderable texture format in core OpenGL on desktop.
        case TextureFormat::SRGB8:
            return mContext.isAtLeastGL<4, 5>();

        // Half-float formats, requires extension.
        case TextureFormat::R16F:
        case TextureFormat::RG16F:
        case TextureFormat::RGBA16F:
            return gl.ext.EXT_color_buffer_float || gl.ext.EXT_color_buffer_half_float;

        // RGB16F is only supported with EXT_color_buffer_half_float, however
        // some WebGL implementations do not consider this extension to be sufficient:
        // https://bugs.chromium.org/p/chromium/issues/detail?id=941671#c10
        case TextureFormat::RGB16F:
        #if defined(__EMSCRIPTEN__)
            return false;
        #else
            return gl.ext.EXT_color_buffer_half_float;
        #endif

        // Float formats from GL_EXT_color_buffer_float
        case TextureFormat::R32F:
        case TextureFormat::RG32F:
        case TextureFormat::RGBA32F:
            return gl.ext.EXT_color_buffer_float;

        // RGB_11_11_10 is only supported with some  specific extensions
        case TextureFormat::R11F_G11F_B10F:
            return gl.ext.EXT_color_buffer_float || gl.ext.APPLE_color_buffer_packed_float;

        default:
            return false;
    }
}

/**
 * 检查是否支持帧缓冲区获取
 * 
 * 检查是否支持 EXT_shader_framebuffer_fetch 扩展。
 * 此扩展允许片段着色器读取当前片段的帧缓冲区值。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isFrameBufferFetchSupported() {
    auto const& gl = mContext;
    return gl.ext.EXT_shader_framebuffer_fetch;
}

/**
 * 检查是否支持多重采样帧缓冲区获取
 * 
 * 检查是否支持多重采样帧缓冲区获取。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - 当前实现与 isFrameBufferFetchSupported 相同
 */
bool OpenGLDriver::isFrameBufferFetchMultiSampleSupported() {
    return isFrameBufferFetchSupported();
}

/**
 * 检查是否支持帧时间查询
 * 
 * 检查是否支持 GPU 帧时间查询。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isFrameTimeSupported() {
    return TimerQueryFactory::isGpuTimeSupported();
}

/**
 * 检查是否支持自动深度解析
 * 
 * 检查是否支持自动深度/模板解析（MSAA resolve）。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - TODO: 这应该只在 GLES 3.1+ 和 EXT_multisampled_render_to_texture2 时返回 true
 * - 当前总是返回 true
 */
bool OpenGLDriver::isAutoDepthResolveSupported() {
    // TODO: 这应该只在 GLES 3.1+ 和 EXT_multisampled_render_to_texture2 时返回 true
    return true;
}

/**
 * 检查是否支持 sRGB 交换链
 * 
 * 检查是否支持 sRGB 交换链。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - ES 2.0 后端（即功能级别 0），我们总是向客户端假装 sRGB 交换链可用
 * - 如果实际有此功能，将使用它，否则在着色器中模拟
 */
bool OpenGLDriver::isSRGBSwapChainSupported() {
    if (UTILS_UNLIKELY(mContext.isES2())) {
        // 在 ES 2.0 后端（即功能级别 0），我们总是向客户端假装 sRGB 交换链可用
        // 如果实际有此功能，将使用它，否则在着色器中模拟
        return true;
    }
    return mPlatform.isSRGBSwapChainSupported();
}

/**
 * 检查是否支持 MSAA 交换链
 * 
 * 检查是否支持指定采样数的 MSAA 交换链。
 * 
 * @param samples 采样数
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isMSAASwapChainSupported(uint32_t const samples) {
    return mPlatform.isMSAASwapChainSupported(samples);
}

/**
 * 检查是否支持保护内容
 * 
 * 检查是否支持保护上下文（用于 DRM 内容保护）。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isProtectedContentSupported() {
    return mPlatform.isProtectedContextSupported();
}

/**
 * 检查是否支持立体渲染
 * 
 * 检查是否支持立体渲染（Instanced 或 Multiview）。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 支持情况：
 * - Instanced 立体：需要实例化和 EXT_clip_cull_distance
 * - Multiview 立体：需要 ES 3.0 和 OVR_multiview2
 * - ES 2.0：不支持
 */
bool OpenGLDriver::isStereoSupported() {
    // Instanced 立体需要实例化和 EXT_clip_cull_distance
    // Multiview 立体需要 ES 3.0 和 OVR_multiview2
    if (UTILS_UNLIKELY(mContext.isES2())) {
        return false;
    }
    switch (mDriverConfig.stereoscopicType) {
        case StereoscopicType::INSTANCED:
            return mContext.ext.EXT_clip_cull_distance;
        case StereoscopicType::MULTIVIEW:
            return mContext.ext.OVR_multiview2;
        case StereoscopicType::NONE:
            return false;
    }
    return false;
}

/**
 * 检查是否支持并行着色器编译
 * 
 * 检查是否支持并行着色器编译。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - 即使在平台不支持的情况下，我们也通过将着色器编译成本分摊到 N 帧来模拟并行编译
 * - 如果禁用了分摊编译，返回实际的平台支持情况
 */
bool OpenGLDriver::isParallelShaderCompileSupported() {
    // 即使在平台不支持的情况下，我们也通过将着色器编译成本分摊到 N 帧来模拟并行编译
    // 如果禁用了分摊编译，返回实际的平台支持情况
    if (mDriverConfig.disableAmortizedShaderCompile) {
        return mShaderCompilerService.isParallelShaderCompileSupported();
    }
    return true;
}

/**
 * 检查是否支持深度/模板解析
 * 
 * 检查是否支持深度/模板缓冲区的 MSAA resolve。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - OpenGL 后端总是返回 true（支持）
 */
bool OpenGLDriver::isDepthStencilResolveSupported() {
    return true;
}

/**
 * 检查是否支持深度/模板 Blit
 * 
 * 检查是否支持深度/模板缓冲区的 Blit 操作。
 * 
 * @param 未使用的格式参数（保持接口一致性）
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - OpenGL 后端总是返回 true（支持）
 */
bool OpenGLDriver::isDepthStencilBlitSupported(TextureFormat) {
    return true;
}

/**
 * 检查是否支持保护纹理
 * 
 * 检查是否支持 EXT_protected_textures 扩展（用于 DRM 内容保护）。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isProtectedTexturesSupported() {
    return getContext().ext.EXT_protected_textures;
}

/**
 * 检查是否支持深度裁剪
 * 
 * 检查是否支持 EXT_depth_clamp 扩展。
 * 深度裁剪将深度值限制在 [0, 1] 范围内。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool OpenGLDriver::isDepthClampSupported() {
    return getContext().ext.EXT_depth_clamp;
}

/**
 * 检查是否需要工作区
 * 
 * 检查是否需要特定的驱动工作区（bug 修复）。
 * 
 * @param workaround 工作区类型
 * @return 如果需要返回 true，否则返回 false
 * 
 * 支持的工作区：
 * - SPLIT_EASU：分割 EASU（某些驱动需要）
 * - ALLOW_READ_ONLY_ANCILLARY_FEEDBACK_LOOP：允许只读辅助反馈循环
 * - ADRENO_UNIFORM_ARRAY_CRASH：Adreno uniform 数组崩溃修复
 * - DISABLE_BLIT_INTO_TEXTURE_ARRAY：禁用 Blit 到纹理数组
 * - POWER_VR_SHADER_WORKAROUNDS：PowerVR 着色器工作区
 * - DISABLE_DEPTH_PRECACHE_FOR_DEFAULT_MATERIAL：禁用默认材质的深度预缓存
 * - EMULATE_SRGB_SWAPCHAIN：模拟 sRGB 交换链（ES 2.0）
 */
bool OpenGLDriver::isWorkaroundNeeded(Workaround const workaround) {
    switch (workaround) {
        case Workaround::SPLIT_EASU:
            return mContext.bugs.split_easu;
        case Workaround::ALLOW_READ_ONLY_ANCILLARY_FEEDBACK_LOOP:
            return mContext.bugs.allow_read_only_ancillary_feedback_loop;
        case Workaround::ADRENO_UNIFORM_ARRAY_CRASH:
            return mContext.bugs.enable_initialize_non_used_uniform_array;
        case Workaround::DISABLE_BLIT_INTO_TEXTURE_ARRAY:
            return mContext.bugs.disable_blit_into_texture_array;
        case Workaround::POWER_VR_SHADER_WORKAROUNDS:
            return mContext.bugs.powervr_shader_workarounds;
        case Workaround::DISABLE_DEPTH_PRECACHE_FOR_DEFAULT_MATERIAL:
            return mContext.bugs.disable_depth_precache_for_default_material;
        case Workaround::EMULATE_SRGB_SWAPCHAIN:
            return mContext.isES2() && !mPlatform.isSRGBSwapChainSupported();
        default:
            return false;
    }
    return false;
}

/**
 * 获取功能级别
 * 
 * 返回当前 OpenGL 上下文的功能级别。
 * 
 * @return FeatureLevel 枚举值
 */
FeatureLevel OpenGLDriver::getFeatureLevel() {
    return mContext.getFeatureLevel();
}

/**
 * 获取裁剪空间参数
 * 
 * 返回裁剪空间的参数，用于深度值的转换。
 * 
 * @return float2，包含缩放和偏移参数
 *         - 如果支持 EXT_clip_control：
 *           * 缩放 = 1.0，偏移 = 0.0（虚拟和物理裁剪空间的 z 坐标都在 [-w, 0]）
 *         - 否则：
 *           * 缩放 = 2.0，偏移 = -1.0（虚拟裁剪空间的 z 坐标在 [-w, 0]，物理在 [-w, w]）
 * 
 * 注意：
 * - 用于将 Filament 的虚拟裁剪空间转换为 OpenGL 的物理裁剪空间
 */
float2 OpenGLDriver::getClipSpaceParams() {
    return mContext.ext.EXT_clip_control ?
           // 虚拟和物理裁剪空间的 z 坐标都在 [-w, 0]
           float2{ 1.0f, 0.0f } :
           // 虚拟裁剪空间的 z 坐标在 [-w, 0]，物理在 [-w, w]
           float2{ 2.0f, -1.0f };
}

/**
 * 获取最大绘制缓冲区数量
 * 
 * 返回支持的最大绘制缓冲区数量（MRT）。
 * 
 * @return 最大绘制缓冲区数量（最多 MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT）
 */
uint8_t OpenGLDriver::getMaxDrawBuffers() {
    return std::min(MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT, uint8_t(mContext.gets.max_draw_buffers));
}

/**
 * 获取最大 Uniform 缓冲区大小
 * 
 * 返回支持的最大 Uniform 缓冲区大小（字节）。
 * 
 * @return 最大 Uniform 缓冲区大小（字节）
 */
size_t OpenGLDriver::getMaxUniformBufferSize() {
    return mContext.gets.max_uniform_block_size;
}

/**
 * 获取最大纹理尺寸
 * 
 * 返回指定采样器类型支持的最大纹理尺寸。
 * 
 * @param target 采样器类型（2D/3D/Cube/Array/External）
 * @return 最大纹理尺寸（像素），如果类型无效返回 0
 */
size_t OpenGLDriver::getMaxTextureSize(SamplerType const target) {
    switch (target) {
        case SamplerType::SAMPLER_2D:
        case SamplerType::SAMPLER_2D_ARRAY:
        case SamplerType::SAMPLER_EXTERNAL:
            return mContext.gets.max_texture_size;
        case SamplerType::SAMPLER_CUBEMAP:
            return mContext.gets.max_cubemap_texture_size;
        case SamplerType::SAMPLER_3D:
            return mContext.gets.max_3d_texture_size;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return mContext.gets.max_cubemap_texture_size;
    }
    return 0;
}

/**
 * 获取最大数组纹理层数
 * 
 * 返回支持的最大数组纹理层数。
 * 
 * @return 最大数组纹理层数
 */
size_t OpenGLDriver::getMaxArrayTextureLayers() {
    return mContext.gets.max_array_texture_layers;
}

/**
 * 获取 Uniform 缓冲区偏移对齐
 * 
 * 返回 Uniform 缓冲区偏移的对齐要求（字节）。
 * 
 * @return Uniform 缓冲区偏移对齐（字节）
 * 
 * 注意：
 * - 用于确保 Uniform 缓冲区偏移满足驱动要求
 * - 通常为 256 字节对齐
 */
size_t OpenGLDriver::getUniformBufferOffsetAlignment() {
    return mContext.gets.uniform_buffer_offset_alignment;
}

// ------------------------------------------------------------------------------------------------
// 交换链
// ------------------------------------------------------------------------------------------------

/**
 * 提交交换链
 * 
 * 提交当前帧到交换链，交换前后缓冲区并呈现到屏幕。
 * 
 * @param sch 交换链句柄
 * 
 * 执行流程：
 * 1. 通过平台接口提交交换链（交换缓冲区）
 * 2. 如果有帧调度回调，调度回调（在渲染线程执行）
 * 3. 如果有帧完成操作，调度在 GPU 完成时执行
 * 
 * 注意：
 * - 此方法在渲染线程调用
 * - 提交后，后缓冲区变为前缓冲区，前缓冲区变为后缓冲区
 * - 帧调度回调用于通知帧已调度到显示队列
 * - 帧完成操作在 GPU 完成所有命令后执行
 */
void OpenGLDriver::commit(Handle<HwSwapChain> sch) {
    DEBUG_MARKER()

    GLSwapChain* sc = handle_cast<GLSwapChain*>(sch);
    // 通过平台接口提交交换链（交换缓冲区）
    mPlatform.commit(sc->swapChain);

    // 如果有帧调度回调，调度回调
    auto& fs = sc->frameScheduled;
    if (fs.callback) {
        scheduleCallback(fs.handler, [callback = fs.callback]() {
            callback->operator()(PresentCallable{ PresentCallable::noopPresent, nullptr });
        });
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 如果有帧完成操作，调度在 GPU 完成时执行
    if (UTILS_UNLIKELY(!mFrameCompleteOps.empty())) {
        whenGpuCommandsComplete([ops = std::move(mFrameCompleteOps)]() {
            for (auto&& op: ops) {
                op();
            }
        });
    }
#endif
}

/**
 * 检查是否支持合成器时序
 * 
 * 检查平台是否支持合成器时序查询。
 * 
 * @return 如果支持返回 true，否则返回 false
 * 
 * 注意：
 * - 这是同步调用
 * - 合成器时序用于测量帧在显示管道中的时间
 */
bool OpenGLDriver::isCompositorTimingSupported() {
    // 这是同步调用
    return mPlatform.isCompositorTimingSupported();
}

/**
 * 查询合成器时序
 * 
 * 查询交换链的合成器时序信息。
 * 
 * @param swapChain 交换链句柄
 * @param outCompositorTiming 输出参数，返回合成器时序
 * @return 如果成功返回 true，否则返回 false
 * 
 * 注意：
 * - 这是同步调用
 * - 如果句柄无效或未初始化，返回 false
 */
bool OpenGLDriver::queryCompositorTiming(SwapChainHandle swapChain,
        CompositorTiming* outCompositorTiming) {
    // 这是同步调用
    if (!swapChain) {
        return false;
    }
    GLSwapChain const* const sc = handle_cast<GLSwapChain*>(swapChain);
    if (!sc) {
        // 如果 SwapChainHandle 尚未初始化（仍在 CommandStream 中），可能发生
        return false;
    }
    return mPlatform.queryCompositorTiming(sc->swapChain, outCompositorTiming);
}

/**
 * 查询帧时间戳
 * 
 * 查询指定帧的时间戳信息。
 * 
 * @param swapChain 交换链句柄
 * @param frameId 帧 ID
 * @param outFrameTimestamps 输出参数，返回帧时间戳
 * @return 如果成功返回 true，否则返回 false
 * 
 * 注意：
 * - 这是同步调用
 * - 如果句柄无效或未初始化，返回 false
 * - 帧时间戳用于性能分析
 */
bool OpenGLDriver::queryFrameTimestamps(SwapChainHandle swapChain, uint64_t const frameId,
        FrameTimestamps* outFrameTimestamps) {
    // 这是同步调用
    if (!swapChain) {
        return false;
    }
    GLSwapChain const* const sc = handle_cast<GLSwapChain*>(swapChain);
    if (!sc) {
        // 如果 SwapChainHandle 尚未初始化（仍在 CommandStream 中），可能发生
        return false;
    }
    return mPlatform.queryFrameTimestamps(sc->swapChain, frameId, outFrameTimestamps);
}

/**
 * 使交换链成为当前上下文
 * 
 * 将指定的交换链设置为当前 OpenGL 上下文。
 * 用于上下文切换（如从默认上下文切换到保护上下文）。
 * 
 * @param schDraw 绘制交换链句柄
 * @param schRead 读取交换链句柄（可选）
 * 
 * 执行流程：
 * 1. 上下文切换前回调：
 *    - 分离所有 NATIVE 流（上下文切换会删除纹理 ID）
 *    - 解绑所有 OpenGL 对象（避免上下文切换问题）
 * 2. 上下文切换后回调：
 *    - 重新附加所有 NATIVE 流（生成新纹理 ID）
 *    - 强制所有绑定的描述符集失效（需要重新绑定）
 *    - 同步状态和缓存（上下文切换后状态可能改变）
 * 3. 保存当前绘制交换链
 * 4. 重置视口和裁剪区域（上下文切换后可能改变）
 * 
 * 注意：
 * - 上下文切换会删除所有 OpenGL 对象名称（纹理、缓冲区等）
 * - 需要重新创建和绑定所有资源
 * - 视口和裁剪区域在上下文切换后可能被重置
 */
void OpenGLDriver::makeCurrent(Handle<HwSwapChain> schDraw, Handle<HwSwapChain> schRead) {
    DEBUG_MARKER()

    GLSwapChain* scDraw = handle_cast<GLSwapChain*>(schDraw);
    GLSwapChain const* scRead = handle_cast<GLSwapChain*>(schRead);

    mPlatform.makeCurrent(scDraw->swapChain, scRead->swapChain,
            // 上下文切换前回调
            [this]() {
                // 分离所有 NATIVE 流（上下文切换会删除纹理 ID）
                for (auto const t: mTexturesWithStreamsAttached) {
                    if (t->hwStream->streamType == StreamType::NATIVE) {
                        mPlatform.detach(t->hwStream->stream);
                    }
                }
                // OpenGL 上下文即将改变，解绑所有内容
                mContext.unbindEverything();
            },
            // 上下文切换后回调
            [this](size_t const index) {
                // 重新附加所有 NATIVE 流（生成新纹理 ID）
                for (auto const t: mTexturesWithStreamsAttached) {
                    if (t->hwStream->streamType == StreamType::NATIVE) {
                        if (t->externalTexture) {
                            glGenTextures(1, &t->externalTexture->id);
                            t->gl.id = t->externalTexture->id;
                        } else {
                            glGenTextures(1, &t->gl.id);
                        }
                        mPlatform.attach(t->hwStream->stream, t->gl.id);
                        mContext.updateTexImage(GL_TEXTURE_EXTERNAL_OES, t->gl.id);
                    }
                }

                // 强制所有绑定的描述符集失效（需要重新绑定）
                decltype(mInvalidDescriptorSetBindings) changed;
                changed.setValue((1 << MAX_DESCRIPTOR_SET_COUNT) - 1);
                mInvalidDescriptorSetBindings |= changed;

                // OpenGL 上下文已改变，重新同步状态和缓存
                mContext.synchronizeStateAndCache(index);
                DLOG(INFO) << "*** OpenGL context change : " << (index ? "protected" : "default");
            });

    mCurrentDrawSwapChain = scDraw;

    // 根据 GL 规范，glViewport 和 glScissor：
    // 当 GL 上下文首次附加到窗口时，宽度和高度设置为该窗口的尺寸
    // 所以基本上，我们的 viewport/scissor 可能在这里被重置为"某些值"
    mContext.state.window.viewport = {};
    mContext.state.window.scissor = {};
}

// ------------------------------------------------------------------------------------------------
// 更新驱动对象
// ------------------------------------------------------------------------------------------------

/**
 * 设置顶点缓冲区对象
 * 
 * 将缓冲区对象设置到顶点缓冲区的指定槽位。
 * 
 * @param vbh 顶点缓冲区句柄
 * @param index 槽位索引（0, 1, 2, ...）
 * @param boh 缓冲区对象句柄
 * 
 * 执行流程：
 * 1. 验证缓冲区对象绑定类型是 GL_ARRAY_BUFFER
 * 2. 如果指定的 VBO 句柄与槽位中的不同：
 *    - 更新槽位
 *    - 增加循环版本号（用于 VAO 缓存失效）
 * 3. 依赖的 VAO 使用版本号来检测何时需要更新
 * 
 * 性能优化：
 * - 使用版本号避免不必要的 VAO 更新
 * - 版本号是循环的（避免溢出）
 */
void OpenGLDriver::setVertexBufferObject(Handle<HwVertexBuffer> vbh,
        uint32_t const index, Handle<HwBufferObject> boh) {
   DEBUG_MARKER()

    GLVertexBuffer* vb = handle_cast<GLVertexBuffer *>(vbh);
    GLBufferObject const* bo = handle_cast<GLBufferObject *>(boh);

    assert_invariant(bo->gl.binding == GL_ARRAY_BUFFER);

    // 如果指定的 VBO 句柄与槽位中的不同，更新槽位并增加循环版本号
    // 依赖的 VAO 使用版本号来检测何时需要更新
    if (vb->gl.buffers[index] != bo->gl.id) {
        vb->gl.buffers[index] = bo->gl.id;
        static constexpr uint32_t kMaxVersion = 
                std::numeric_limits<decltype(vb->bufferObjectsVersion)>::max();
        const uint32_t version = vb->bufferObjectsVersion;
        vb->bufferObjectsVersion = (version + 1) % kMaxVersion;  // 循环版本号
    }

    CHECK_GL_ERROR()
}

/**
 * 更新索引缓冲区
 * 
 * 更新索引缓冲区的数据。
 * 
 * @param ibh 索引缓冲区句柄
 * @param p 缓冲区描述符（包含数据和大小）
 * @param byteOffset 更新偏移（字节）
 * 
 * 执行流程：
 * 1. 验证元素大小（必须是 2 或 4 字节）
 * 2. 解绑 VAO（避免影响）
 * 3. 绑定索引缓冲区
 * 4. 使用 glBufferSubData 更新数据
 * 5. 调度销毁缓冲区描述符
 * 
 * 注意：
 * - 数据更新是同步的（可能阻塞）
 * - 使用 glBufferSubData 进行部分更新
 */
void OpenGLDriver::updateIndexBuffer(
        Handle<HwIndexBuffer> ibh, BufferDescriptor&& p, uint32_t const byteOffset) {
    DEBUG_MARKER()

    auto& gl = mContext;
    GLIndexBuffer const* ib = handle_cast<GLIndexBuffer *>(ibh);
    assert_invariant(ib->elementSize == 2 || ib->elementSize == 4);

    // 解绑 VAO（避免影响）
    gl.bindVertexArray(nullptr);
    gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->gl.buffer);
    // 更新索引缓冲区数据
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, byteOffset, GLsizeiptr(p.size), p.buffer);

    scheduleDestroy(std::move(p));

    CHECK_GL_ERROR()
}

/**
 * 更新缓冲区对象
 * 
 * 更新缓冲区对象的数据。
 * 
 * @param boh 缓冲区对象句柄
 * @param bd 缓冲区描述符（包含数据和大小）
 * @param byteOffset 更新偏移（字节）
 * 
 * 执行流程：
 * 1. 验证更新范围有效（offset + size <= byteCount）
 * 2. 如果是顶点缓冲区，解绑 VAO（避免影响）
 * 3. ES 2.0 特殊处理（uniform 缓冲区使用系统内存）：
 *    - 直接复制到系统内存
 *    - 增加 age 计数器
 * 4. 其他情况：
 *    - 绑定缓冲区
 *    - 如果更新整个缓冲区（offset=0, size=byteCount）：
 *      * 使用 glBufferData（通常更快）
 *    - 否则：
 *      * 使用 glBufferSubData（部分更新）
 * 5. 调度销毁缓冲区描述符
 * 
 * 性能优化：
 * - 更新整个缓冲区时使用 glBufferData（通常更快）
 * - 部分更新时使用 glBufferSubData
 * - 注意：glBufferSubData 在同一帧多次调用时可能效率低下
 */
void OpenGLDriver::updateBufferObject(
        Handle<HwBufferObject> boh, BufferDescriptor&& bd, uint32_t const byteOffset) {
    DEBUG_MARKER()

    auto& gl = mContext;
    GLBufferObject* bo = handle_cast<GLBufferObject *>(boh);

    assert_invariant(bd.size + byteOffset <= bo->byteCount);

    // 如果是顶点缓冲区，解绑 VAO（避免影响）
    if (bo->gl.binding == GL_ARRAY_BUFFER) {
        gl.bindVertexArray(nullptr);
    }

    // ES 2.0 特殊处理：uniform 缓冲区使用系统内存
    if (UTILS_UNLIKELY(bo->bindingType == BufferObjectBinding::UNIFORM && gl.isES2())) {
        assert_invariant(bo->gl.buffer);
        // 直接复制到系统内存
        memcpy(static_cast<uint8_t*>(bo->gl.buffer) + byteOffset, bd.buffer, bd.size);
        bo->age++;  // 增加 age 计数器
    } else {
        // 标准 OpenGL 缓冲区更新
        assert_invariant(bo->gl.id);
        gl.bindBuffer(bo->gl.binding, bo->gl.id);
        if (byteOffset == 0 && bd.size == bo->byteCount) {
            // 看起来使用 glBufferData() 通常更快（或不更差）
            glBufferData(bo->gl.binding, GLsizeiptr(bd.size), bd.buffer, getBufferUsage(bo->usage));
        } else {
            // glBufferSubData() 在同一帧多次调用时可能效率低下
            // 目前我们没有这样做
            glBufferSubData(bo->gl.binding, byteOffset, GLsizeiptr(bd.size), bd.buffer);
        }
    }

    scheduleDestroy(std::move(bd));

    CHECK_GL_ERROR()
}

/**
 * 异步更新缓冲区对象
 * 
 * 使用内存映射异步更新缓冲区对象的数据（不阻塞 CPU）。
 * 
 * @param boh 缓冲区对象句柄
 * @param bd 缓冲区描述符（包含数据和大小）
 * @param byteOffset 更新偏移（字节）
 * 
 * 执行流程：
 * 1. ES 2.0 或不支持映射：回退到 updateBufferObject（同步）
 * 2. 如果不支持映射：回退到 updateBufferObject
 * 3. 如果不是 uniform 缓冲区：回退到 updateBufferObject（TODO: 支持所有类型）
 * 4. 如果是 uniform 缓冲区：
 *    - 绑定缓冲区
 *    - 映射缓冲区范围（GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT）
 *    - 复制数据到映射的内存
 *    - 取消映射（如果失败，重试）
 *    - 如果映射失败，回退到 glBufferSubData
 * 
 * 性能优化：
 * - 使用 GL_MAP_UNSYNCHRONIZED_BIT 避免 CPU-GPU 同步（不阻塞）
 * - 使用 GL_MAP_INVALIDATE_RANGE_BIT 允许驱动优化（丢弃旧数据）
 * - 映射失败时回退到同步更新
 * 
 * 注意：
 * - 当前只支持 uniform 缓冲区的异步更新
 * - 根据规范，UnmapBuffer 在极少数情况下可能返回 FALSE（如屏幕模式改变）
 * - 这不是 GL 错误，可以通过重试处理
 */
void OpenGLDriver::updateBufferObjectUnsynchronized(
        Handle<HwBufferObject> boh, BufferDescriptor&& bd, uint32_t const byteOffset) {
    DEBUG_MARKER()

    // ES 2.0 不支持映射，回退到同步更新
    if (UTILS_UNLIKELY(mContext.isES2())) {
        updateBufferObject(boh, std::move(bd), byteOffset);
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 如果不支持映射，回退到同步更新
    if constexpr (!HAS_MAPBUFFERS) {
        updateBufferObject(boh, std::move(bd), byteOffset);
    } else {
        GLBufferObject const* bo = handle_cast<GLBufferObject*>(boh);

        assert_invariant(bo->gl.id);
        assert_invariant(bd.size + byteOffset <= bo->byteCount);

        // 如果不是 uniform 缓冲区，回退到同步更新
        if (bo->gl.binding != GL_UNIFORM_BUFFER) {
            // TODO: 对所有类型的缓冲区使用 updateBuffer？确保 GL 支持
            updateBufferObject(boh, std::move(bd), byteOffset);
        } else {
            // uniform 缓冲区：使用内存映射异步更新
            auto& gl = mContext;
            gl.bindBuffer(bo->gl.binding, bo->gl.id);
retry:
            // 映射缓冲区范围（不阻塞，允许驱动优化）
            void* const vaddr = glMapBufferRange(bo->gl.binding, byteOffset, GLsizeiptr(bd.size),
                    GL_MAP_WRITE_BIT |
                    GL_MAP_INVALIDATE_RANGE_BIT |
                    GL_MAP_UNSYNCHRONIZED_BIT);
            if (UTILS_LIKELY(vaddr)) {
                // 复制数据到映射的内存
                memcpy(vaddr, bd.buffer, bd.size);
                // 取消映射（如果失败，重试）
                if (UTILS_UNLIKELY(glUnmapBuffer(bo->gl.binding) == GL_FALSE)) {
                    // 根据规范，UnmapBuffer 在极少数情况下可能返回 FALSE（如屏幕模式改变）
                    // 注意：这不是 GL 错误，可以通过重试处理
                    goto retry; // NOLINT(cppcoreguidelines-avoid-goto,hicpp-avoid-goto)
                }
            } else {
                // 处理映射错误，回退到 glBufferSubData()
                glBufferSubData(bo->gl.binding, byteOffset, GLsizeiptr(bd.size), bd.buffer);
            }
            scheduleDestroy(std::move(bd));
        }
    }
    CHECK_GL_ERROR()
#endif
}

/**
 * 重置缓冲区对象
 * 
 * 重置缓冲区对象的数据（清零或重新分配）。
 * 
 * @param boh 缓冲区对象句柄
 * 
 * 执行流程：
 * 1. ES 2.0 特殊处理（uniform 缓冲区使用系统内存）：
 *    - 无需操作（系统内存保持原值）
 * 2. 其他情况：
 *    - 绑定缓冲区
 *    - 使用 glBufferData 重新分配（数据清零）
 * 
 * 注意：
 * - 重置会重新分配缓冲区，旧数据丢失
 * - ES 2.0 的 uniform 缓冲区不重置（系统内存保持原值）
 */
void OpenGLDriver::resetBufferObject(Handle<HwBufferObject> boh) {
    DEBUG_MARKER()

    auto& gl = mContext;
    GLBufferObject const* bo = handle_cast<GLBufferObject*>(boh);

    // ES 2.0 特殊处理：uniform 缓冲区使用系统内存，无需操作
    if (UTILS_UNLIKELY(bo->bindingType == BufferObjectBinding::UNIFORM && gl.isES2())) {
        // 这里无需操作
    } else {
        // 重新分配缓冲区（数据清零）
        assert_invariant(bo->gl.id);
        gl.bindBuffer(bo->gl.binding, bo->gl.id);
        glBufferData(bo->gl.binding, bo->byteCount, nullptr, getBufferUsage(bo->usage));
    }
}

/**
 * 更新 3D 图像
 * 
 * 更新纹理的 3D 图像数据（3D 纹理、2D 数组纹理、Cube 纹理等）。
 * 
 * @param th 纹理句柄
 * @param level Mipmap 级别
 * @param xoffset X 偏移
 * @param yoffset Y 偏移
 * @param zoffset Z 偏移（层索引）
 * @param width 更新区域宽度
 * @param height 更新区域高度
 * @param depth 更新区域深度（层数）
 * @param data 像素缓冲区描述符
 * 
 * 执行流程：
 * 1. 根据数据类型选择更新方法：
 *    - 压缩数据：使用 setCompressedTextureData
 *    - 非压缩数据：使用 setTextureData
 * 
 * 注意：
 * - 此方法支持 3D 纹理、2D 数组纹理、Cube 纹理等
 * - 压缩和非压缩数据使用不同的更新路径
 */
void OpenGLDriver::update3DImage(Handle<HwTexture> th,
        uint32_t const level, uint32_t const xoffset, uint32_t const yoffset, uint32_t const zoffset,
        uint32_t const width, uint32_t const height, uint32_t const depth,
        PixelBufferDescriptor&& data) {
    DEBUG_MARKER()

    GLTexture const* t = handle_cast<GLTexture *>(th);
    // 根据数据类型选择更新方法
    if (data.type == PixelDataType::COMPRESSED) {
        // 压缩数据：使用压缩纹理更新
        setCompressedTextureData(t,
                level, xoffset, yoffset, zoffset, width, height, depth, std::move(data));
    } else {
        // 非压缩数据：使用标准纹理更新
        setTextureData(t,
                level, xoffset, yoffset, zoffset, width, height, depth, std::move(data));
    }
}

/**
 * 生成 Mipmap
 * 
 * 为纹理生成 Mipmap 链。
 * 
 * @param th 纹理句柄
 * 
 * 执行流程：
 * 1. 验证不是多采样纹理（多采样纹理不支持 Mipmap）
 * 2. 绑定纹理到虚拟纹理单元
 * 3. 调用 glGenerateMipmap 生成 Mipmap
 * 
 * 注意：
 * - glGenerateMipmap 可能失败，如果内部格式不是既颜色可渲染又纹理可过滤的
 * - 深度纹理不支持 Mipmap（不是颜色可渲染的）
 * - 多采样纹理不支持 Mipmap
 */
void OpenGLDriver::generateMipmaps(Handle<HwTexture> th) {
    DEBUG_MARKER()

    auto& gl = mContext;
    GLTexture const* t = handle_cast<GLTexture *>(th);
#if defined(BACKEND_OPENGL_LEVEL_GLES31)
    // 多采样纹理不支持 Mipmap
    assert_invariant(t->gl.target != GL_TEXTURE_2D_MULTISAMPLE);
#endif
    // 注意：glGenerateMipmap 也可能失败，如果内部格式不是既颜色可渲染又纹理可过滤的
    // （即：对深度纹理不起作用）
    // 绑定纹理到虚拟纹理单元
    bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
    gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);

    // 生成 Mipmap
    glGenerateMipmap(t->gl.target);

    CHECK_GL_ERROR()
}

/**
 * 设置纹理数据
 * 
 * 更新纹理的像素数据（非压缩格式）。
 * 
 * @param t 纹理指针
 * @param level Mipmap 级别
 * @param xoffset X 偏移
 * @param yoffset Y 偏移
 * @param zoffset Z 偏移（层索引，用于 3D/Array 纹理）
 * @param width 更新区域宽度
 * @param height 更新区域高度
 * @param depth 更新区域深度（层数）
 * @param p 像素缓冲区描述符（格式、类型、对齐、步长等）
 * 
 * 执行流程：
 * 1. 验证偏移和尺寸有效
 * 2. 验证不是多采样纹理（不支持更新）
 * 3. 如果是外部纹理，直接返回（无操作）
 * 4. 获取格式和类型（ES 2.0 特殊处理）
 * 5. 设置像素解包参数（步长、对齐）
 * 6. 计算缓冲区指针（考虑步长、对齐、偏移）
 * 7. 根据纹理类型调用相应的 OpenGL API：
 *    - SAMPLER_2D: glTexSubImage2D
 *    - SAMPLER_3D: glTexSubImage3D
 *    - SAMPLER_2D_ARRAY/CUBEMAP_ARRAY: glTexSubImage3D
 *    - SAMPLER_CUBEMAP: 循环每个面，调用 glTexSubImage2D
 * 8. 调度销毁缓冲区描述符
 * 
 * 注意：
 * - 外部纹理不能更新（无操作）
 * - 多采样纹理不支持更新
 * - Cube 纹理需要逐面更新
 * - ES 2.0 格式和类型必须匹配纹理格式
 */
void OpenGLDriver::setTextureData(GLTexture const* t, uint32_t const level,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const zoffset,
        uint32_t const width, uint32_t const height, uint32_t const depth,
        PixelBufferDescriptor&& p) {
    auto& gl = mContext;

    assert_invariant(t != nullptr);
    assert_invariant(xoffset + width <= std::max(1u, t->width >> level));
    assert_invariant(yoffset + height <= std::max(1u, t->height >> level));
    assert_invariant(t->samples <= 1);

    // 如果是外部纹理，直接返回（无操作）
    if (UTILS_UNLIKELY(t->gl.target == GL_TEXTURE_EXTERNAL_OES)) {
        return;
    }

    // 获取格式和类型
    GLenum glFormat;
    GLenum glType;
    if (mContext.isES2()) {
        // ES 2.0：格式和类型必须匹配纹理格式
        auto const formatAndType = textureFormatToFormatAndType(t->format);
        glFormat = formatAndType.first;
        glType = formatAndType.second;
    } else {
        // ES 3.0+：从描述符获取格式和类型
        glFormat = getFormat(p.format);
        glType = getType(p.type);
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // ES 3.0+：设置行长度（步长）
    if (!gl.isES2()) {
        gl.pixelStore(GL_UNPACK_ROW_LENGTH, GLint(p.stride));
    }
#endif
    // 设置像素对齐
    gl.pixelStore(GL_UNPACK_ALIGNMENT, GLint(p.alignment));

    // 这等价于使用 GL_UNPACK_SKIP_PIXELS 和 GL_UNPACK_SKIP_ROWS
    using PBD = PixelBufferDescriptor;
    size_t const stride = p.stride ? p.stride : width;
    size_t const bpp = PBD::computeDataSize(p.format, p.type, 1, 1, 1);  // 每像素字节数
    size_t const bpr = PBD::computeDataSize(p.format, p.type, stride, 1, p.alignment);  // 每行字节数
    size_t const bpl = bpr * height;  // TODO: PBD 应该有"层步长"
    // 计算缓冲区指针（考虑偏移、步长、对齐）
    void const* const buffer = static_cast<char const*>(p.buffer)
            + bpp* p.left + bpr * p.top + bpl * 0;  // TODO: PBD 应该有 p.depth

    switch (t->target) {
        case SamplerType::SAMPLER_EXTERNAL:
            // if we get there, it's because the user is trying to use an external texture,
            // but it's not supported, so instead, we behave like a texture2d.
            // fallthrough...
        case SamplerType::SAMPLER_2D:
            // NOTE: GL_TEXTURE_2D_MULTISAMPLE is not allowed
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);
            assert_invariant(t->gl.target == GL_TEXTURE_2D);
            glTexSubImage2D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset),
                    GLsizei(width), GLsizei(height), glFormat, glType, buffer);
            break;
        case SamplerType::SAMPLER_3D:
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            assert_invariant(zoffset + depth <= std::max(1u, t->depth >> level));
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);
            assert_invariant(t->gl.target == GL_TEXTURE_3D);
            glTexSubImage3D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset), GLint(zoffset),
                    GLsizei(width), GLsizei(height), GLsizei(depth), glFormat, glType, buffer);
#endif
            break;
        case SamplerType::SAMPLER_2D_ARRAY:
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            assert_invariant(zoffset + depth <= t->depth);
            // NOTE: GL_TEXTURE_2D_MULTISAMPLE is not allowed
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);
            assert_invariant(t->gl.target == GL_TEXTURE_2D_ARRAY ||
                    t->gl.target == GL_TEXTURE_CUBE_MAP_ARRAY);
            glTexSubImage3D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset), GLint(zoffset),
                    GLsizei(width), GLsizei(height), GLsizei(depth), glFormat, glType, buffer);
#endif
            break;
        case SamplerType::SAMPLER_CUBEMAP: {
            assert_invariant(t->gl.target == GL_TEXTURE_CUBE_MAP);
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);

            assert_invariant(width == height);
            const size_t faceSize = PixelBufferDescriptor::computeDataSize(
                    p.format, p.type, p.stride ? p.stride : width, height, p.alignment);
            assert_invariant(zoffset + depth <= 6);
            UTILS_NOUNROLL
            for (size_t face = 0; face < depth; face++) {
                GLenum const target = getCubemapTarget(zoffset + face);
                glTexSubImage2D(target, GLint(level), GLint(xoffset), GLint(yoffset),
                        GLsizei(width), GLsizei(height), glFormat, glType,
                        static_cast<uint8_t const*>(buffer) + faceSize * face);
            }
            break;
        }
    }

    scheduleDestroy(std::move(p));

    CHECK_GL_ERROR()
}

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
 * @param width 更新区域宽度
 * @param height 更新区域高度
 * @param depth 更新区域深度（层数）
 * @param p 像素缓冲区描述符（包含压缩数据）
 * 
 * 执行流程：
 * 1. 验证偏移和尺寸有效
 * 2. 验证不是多采样纹理
 * 3. 如果是外部纹理，直接返回（无操作）
 * 4. 获取压缩图像大小
 * 5. 根据纹理类型调用相应的 OpenGL API：
 *    - SAMPLER_2D: glCompressedTexSubImage2D
 *    - SAMPLER_3D: glCompressedTexSubImage3D
 *    - SAMPLER_2D_ARRAY/CUBEMAP_ARRAY: glCompressedTexSubImage3D
 *    - SAMPLER_CUBEMAP: 循环每个面，调用 glCompressedTexSubImage2D
 * 6. 调度销毁缓冲区描述符
 * 
 * 注意：
 * - 压缩格式使用内部格式（不是格式和类型）
 * - 压缩数据大小由 format 和尺寸决定
 * - TODO: 可能应该断言 CompressedPixelDataType 与 internalFormat 相同
 * - TODO: 可能应该断言大小正确（因为我们可以自己计算）
 */
void OpenGLDriver::setCompressedTextureData(GLTexture const* t, uint32_t const level,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const zoffset,
        uint32_t const width, uint32_t const height, uint32_t const depth,
        PixelBufferDescriptor&& p) {
    auto& gl = mContext;

    assert_invariant(xoffset + width <= std::max(1u, t->width >> level));
    assert_invariant(yoffset + height <= std::max(1u, t->height >> level));
    assert_invariant(zoffset + depth <= t->depth);
    assert_invariant(t->samples <= 1);

    // 如果是外部纹理，直接返回（无操作）
    if (UTILS_UNLIKELY(t->gl.target == GL_TEXTURE_EXTERNAL_OES)) {
        return;
    }

    // TODO: 可能应该断言 CompressedPixelDataType 与 internalFormat 相同

    GLsizei const imageSize = GLsizei(p.imageSize);

    //  TODO: 可能应该断言大小正确（因为我们可以自己计算）

    switch (t->target) {
        case SamplerType::SAMPLER_EXTERNAL:
            // if we get there, it's because the user is trying to use an external texture,
            // but it's not supported, so instead, we behave like a texture2d.
            // fallthrough...
        case SamplerType::SAMPLER_2D:
            // NOTE: GL_TEXTURE_2D_MULTISAMPLE is not allowed
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);
            assert_invariant(t->gl.target == GL_TEXTURE_2D);
            glCompressedTexSubImage2D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset),
                    GLsizei(width), GLsizei(height),
                    t->gl.internalFormat, imageSize, p.buffer);
            break;
        case SamplerType::SAMPLER_3D:
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);
            assert_invariant(t->gl.target == GL_TEXTURE_3D);
            glCompressedTexSubImage3D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset), GLint(zoffset),
                    GLsizei(width), GLsizei(height), GLsizei(depth),
                    t->gl.internalFormat, imageSize, p.buffer);
#endif
            break;
        case SamplerType::SAMPLER_2D_ARRAY:
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            assert_invariant(!gl.isES2());
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
            assert_invariant(t->gl.target == GL_TEXTURE_2D_ARRAY ||
                    t->gl.target == GL_TEXTURE_CUBE_MAP_ARRAY);
            glCompressedTexSubImage3D(t->gl.target, GLint(level),
                    GLint(xoffset), GLint(yoffset), GLint(zoffset),
                    GLsizei(width), GLsizei(height), GLsizei(depth),
                    t->gl.internalFormat, imageSize, p.buffer);
#endif
            break;
        case SamplerType::SAMPLER_CUBEMAP: {
            assert_invariant(t->gl.target == GL_TEXTURE_CUBE_MAP);
            bindTexture(OpenGLContext::DUMMY_TEXTURE_BINDING, t);
            gl.activeTexture(OpenGLContext::DUMMY_TEXTURE_BINDING);

            assert_invariant(width == height);
            const size_t faceSize = PixelBufferDescriptor::computeDataSize(
                    p.format, p.type, p.stride ? p.stride : width, height, p.alignment);

            UTILS_NOUNROLL
            for (size_t face = 0; face < depth; face++) {
                GLenum const target = getCubemapTarget(zoffset + face);
                glCompressedTexSubImage2D(target, GLint(level), GLint(xoffset), GLint(yoffset),
                        GLsizei(width), GLsizei(height), t->gl.internalFormat,
                        imageSize, static_cast<uint8_t const*>(p.buffer) + faceSize * face);
            }
            break;
        }
    }

    scheduleDestroy(std::move(p));

    CHECK_GL_ERROR()
}

/**
 * 设置外部图像（2）
 * 
 * 保留外部图像引用计数（防止图像被释放）。
 * 
 * @param image 外部图像句柄引用（平台特定）
 * 
 * 注意：
 * - 用于外部图像的生命周期管理
 * - 增加引用计数
 */
void OpenGLDriver::setupExternalImage2(Platform::ExternalImageHandleRef image) {
    mPlatform.retainExternalImage(image);
}

/**
 * 设置外部图像
 * 
 * 保留外部图像引用计数（防止图像被释放）。
 * 
 * @param image 外部图像指针（平台特定）
 * 
 * 注意：
 * - 用于外部图像的生命周期管理
 * - 增加引用计数
 */
void OpenGLDriver::setupExternalImage(void* image) {
    mPlatform.retainExternalImage(image);
}

/**
 * 设置外部流
 * 
 * 将纹理附加到外部流或从外部流分离。
 * 外部流用于相机预览、视频播放等外部数据源。
 * 
 * @param th 纹理句柄
 * @param sh 流句柄（nullptr 表示分离）
 * 
 * 执行流程：
 * 1. 验证支持外部图像扩展（OES_EGL_image_external_essl3）
 * 2. 如果流句柄非空：
 *    - 如果纹理未附加流：附加流
 *    - 如果附加到不同流：替换流（先分离旧流）
 *    - 如果附加到相同流：无操作
 * 3. 如果流句柄为空且纹理已附加流：分离流
 * 
 * 注意：
 * - 需要 OES_EGL_image_external_essl3 扩展支持
 * - 流用于外部数据源（相机、视频等）
 */
void OpenGLDriver::setExternalStream(Handle<HwTexture> th, Handle<HwStream> sh) {
    auto const& gl = mContext;
    // 验证支持外部图像扩展
    if (gl.ext.OES_EGL_image_external_essl3) {
        DEBUG_MARKER()

        GLTexture* t = handle_cast<GLTexture*>(th);
        if (UTILS_LIKELY(sh)) {
            GLStream* s = handle_cast<GLStream*>(sh);
            if (UTILS_LIKELY(!t->hwStream)) {
                // 纹理未附加流，附加流
                attachStream(t, s);
            } else {
                // 如果附加到不同流，先分离旧流
                if (s->stream != t->hwStream->stream) {
                    replaceStream(t, s);
                }
            }
        } else if (t->hwStream) {
            // 流句柄为空且纹理已附加流，分离流
            detachStream(t);
        }
    }
}

/**
 * 附加流到纹理
 * 
 * 将外部流附加到纹理，使纹理从外部数据源获取数据。
 * 
 * @param t 纹理指针
 * @param hwStream 流指针
 * 
 * 执行流程：
 * 1. 将纹理添加到附加流列表（用于上下文切换时重新附加）
 * 2. 根据流类型处理：
 *    - NATIVE 流：附加到平台（平台管理纹理 ID）
 *    - ACQUIRED 流：无需操作（纹理 ID 在更新时设置）
 * 3. 更新纹理的流指针
 * 
 * 注意：
 * - NATIVE 流由平台管理纹理 ID
 * - ACQUIRED 流的纹理 ID 在 updateStreams 时设置
 */
UTILS_NOINLINE
void OpenGLDriver::attachStream(GLTexture* t, GLStream* hwStream) {
    // 将纹理添加到附加流列表（用于上下文切换时重新附加）
    mTexturesWithStreamsAttached.push_back(t);

    switch (hwStream->streamType) {
        case StreamType::NATIVE:
            // NATIVE 流：附加到平台（平台管理纹理 ID）
            mPlatform.attach(hwStream->stream, t->gl.id);
            mContext.updateTexImage(GL_TEXTURE_EXTERNAL_OES, t->gl.id);
            break;
        case StreamType::ACQUIRED:
            // ACQUIRED 流：无需操作（纹理 ID 在 updateStreams 时设置）
            break;
    }
    t->hwStream = hwStream;
}

/**
 * 从纹理分离流
 * 
 * 将外部流从纹理分离，停止从外部数据源获取数据。
 * 
 * @param t 纹理指针
 * 
 * 执行流程：
 * 1. 从附加流列表中移除纹理
 * 2. 根据流类型处理：
 *    - NATIVE 流：从平台分离（这会删除纹理 ID）
 *    - ACQUIRED 流：解绑并删除纹理
 * 3. 重新生成纹理 ID（用于后续使用）
 * 4. 清除纹理的流指针
 * 
 * 注意：
 * - NATIVE 流分离时会删除纹理 ID（平台管理）
 * - ACQUIRED 流分离时手动删除纹理
 * - 分离后重新生成纹理 ID，以便后续使用
 */
UTILS_NOINLINE
void OpenGLDriver::detachStream(GLTexture* t) noexcept {
    auto& gl = mContext;
    auto& texturesWithStreamsAttached = mTexturesWithStreamsAttached;
    // 从附加流列表中移除纹理
    auto const pos = std::find(texturesWithStreamsAttached.begin(), texturesWithStreamsAttached.end(), t);
    if (pos != texturesWithStreamsAttached.end()) {
        texturesWithStreamsAttached.erase(pos);
    }

    GLStream const* s = static_cast<GLStream*>(t->hwStream);
    
    switch (s->streamType) {
        case StreamType::NATIVE:
            // NATIVE 流：从平台分离（这会删除纹理 ID）
            mPlatform.detach(t->hwStream->stream);
            break;
        case StreamType::ACQUIRED:
            // ACQUIRED 流：解绑并删除纹理
            gl.unbindTexture(t->gl.target, t->gl.id);
            glDeleteTextures(1, &t->gl.id);
            break;
    }

    // 重新生成纹理 ID（用于后续使用）
    if (t->externalTexture) {
        glGenTextures(1, &t->externalTexture->id);
        t->gl.id = t->externalTexture->id;
    } else {
        glGenTextures(1, &t->gl.id);
    }

    t->hwStream = nullptr;
}

/**
 * 替换纹理流
 * 
 * 将纹理的流从一个流替换为另一个流。
 * 用于动态切换纹理的数据源（如从相机预览切换到视频播放）。
 * 
 * @param texture 纹理指针
 * @param newStream 新流指针（不能为 nullptr）
 * 
 * 执行流程：
 * 1. 验证新流非空（不能用于分离流，使用 detachStream）
 * 2. 处理旧流：
 *    - NATIVE 流：从平台分离（这会删除纹理 ID）
 *    - ACQUIRED 流：无需操作
 * 3. 处理新流：
 *    - NATIVE 流：生成新纹理 ID，附加到平台，更新纹理图像
 *    - ACQUIRED 流：重用旧纹理 ID
 * 4. 更新纹理的流指针
 * 
 * 性能优化：
 * - 内联实现，避免操作 mExternalStreams 列表
 * - 可以优化为 detachStream + attachStream，但内联更高效
 * 
 * 注意：
 * - 不能用于分离流（newStream 必须非空）
 * - NATIVE 流分离时会删除纹理 ID，需要重新生成
 * - ACQUIRED 流重用纹理 ID，无需重新生成
 */
UTILS_NOINLINE
void OpenGLDriver::replaceStream(GLTexture* texture, GLStream* newStream) noexcept {
    assert_invariant(newStream && "Do not use replaceStream to detach a stream.");

    // 这可以通过 detachStream + attachStream 实现，但内联允许一些小的优化
    // 例如不操作 mExternalStreams 列表

    GLStream const* oldStream = static_cast<GLStream*>(texture->hwStream);
    
    // 处理旧流
    switch (oldStream->streamType) {
        case StreamType::NATIVE:
            // 从平台分离（这会删除纹理 ID）
            mPlatform.detach(texture->hwStream->stream);
            break;
        case StreamType::ACQUIRED:
            // 无需操作
            break;
    }

    // 处理新流
    switch (newStream->streamType) {
        case StreamType::NATIVE:
            // 生成新纹理 ID
            if (texture->externalTexture) {
                glGenTextures(1, &texture->externalTexture->id);
                texture->gl.id = texture->externalTexture->id;
            } else {
                glGenTextures(1, &texture->gl.id);
            }
            // 附加到平台并更新纹理图像
            mPlatform.attach(newStream->stream, texture->gl.id);
            mContext.updateTexImage(GL_TEXTURE_EXTERNAL_OES, texture->gl.id);
            break;
        case StreamType::ACQUIRED:
            // 重用旧纹理 ID
            break;
    }

    // 更新纹理的流指针
    texture->hwStream = newStream;
}

/**
 * 开始定时器查询
 * 
 * 开始 GPU 定时器查询，用于测量 GPU 执行时间。
 * 
 * @param tqh 定时器查询句柄
 * 
 * 注意：
 * - 定时器查询用于性能分析
 * - 必须在 endTimerQuery 之前调用
 */
void OpenGLDriver::beginTimerQuery(Handle<HwTimerQuery> tqh) {
    DEBUG_MARKER()
    GLTimerQuery* tq = handle_cast<GLTimerQuery*>(tqh);
    mContext.beginTimeElapsedQuery(tq);
}

/**
 * 结束定时器查询
 * 
 * 结束 GPU 定时器查询。
 * 
 * @param tqh 定时器查询句柄
 * 
 * 注意：
 * - 必须在 beginTimerQuery 之后调用
 * - 查询结果通过 getTimerQueryValue 获取
 */
void OpenGLDriver::endTimerQuery(Handle<HwTimerQuery> tqh) {
    DEBUG_MARKER()
    GLTimerQuery* tq = handle_cast<GLTimerQuery*>(tqh);
    mContext.endTimeElapsedQuery(*this, tq);
}

/**
 * 获取定时器查询值
 * 
 * 获取定时器查询的结果（GPU 执行时间）。
 * 
 * @param tqh 定时器查询句柄
 * @param elapsedTime 输出参数，返回经过的时间（纳秒）
 * @return 查询结果状态（SUCCESS/ERROR/NOT_READY）
 */
TimerQueryResult OpenGLDriver::getTimerQueryValue(Handle<HwTimerQuery> tqh, uint64_t* elapsedTime) {
    GLTimerQuery* tq = handle_cast<GLTimerQuery*>(tqh);
    return TimerQueryFactoryInterface::getTimerQueryValue(tq, elapsedTime);
}

/**
 * 编译着色器程序
 * 
 * 请求编译所有待编译的着色器程序，并在完成后调用回调。
 * 
 * @param 未使用的优先级队列参数（保持接口一致性）
 * @param handler 回调处理器
 * @param callback 回调函数（所有程序编译完成后调用）
 * @param user 用户数据
 * 
 * 注意：
 * - 着色器编译是异步的
 * - 回调在所有程序编译完成后调用
 * - 如果 callback 为 nullptr，不注册回调
 */
void OpenGLDriver::compilePrograms(CompilerPriorityQueue,
        CallbackHandler* handler, CallbackHandler::Callback const callback, void* user) {
    if (callback) {
        getShaderCompilerService().notifyWhenAllProgramsAreReady(handler, callback, user);
    }
}

/**
 * 开始一个渲染通道（Render Pass）
 * 
 * 渲染通道是渲染的基本单位，定义了：
 * - 渲染目标（Framebuffer）
 * - 视口和裁剪区域
 * - 清除操作（颜色、深度、模板）
 * - 丢弃操作（优化性能）
 * 
 * @param rth 渲染目标句柄（RenderTargetHandle）
 * @param params 渲染通道参数
 *              - viewport: 视口区域
 *              - depthRange: 深度范围
 *              - clearColor/clearDepth/clearStencil: 清除值
 *              - flags.clear: 需要清除的缓冲区
 *              - flags.discardStart: 开始时丢弃的缓冲区（优化）
 *              - flags.discardEnd: 结束时丢弃的缓冲区（优化）
 * 
 * 执行流程：
 * 1. 着色器编译服务 tick（处理异步编译任务）
 * 2. 保存渲染目标和参数（用于 endRenderPass）
 * 3. 确定输出色彩空间（默认 RT 使用 SwapChain 设置，否则线性）
 * 4. 计算清除和丢弃标志
 * 5. 绑定 Framebuffer（FBO）
 * 6. 禁用裁剪测试（每个渲染通道开始时禁用）
 * 7. 处理丢弃缓冲区（使用 glInvalidateFramebuffer 或 glClear）
 * 8. 处理 MSAA resolve（如果有 fbo_read，需要处理多采样）
 * 9. 清除缓冲区（颜色、深度、模板）
 * 10. 设置视口和深度范围
 * 11. 调试模式下清除丢弃的缓冲区（用红色标记）
 * 
 * 性能优化：
 * - 使用 glInvalidateFramebuffer 丢弃不需要的缓冲区，避免回写，提高性能
 * - 对于 MSAA RenderTarget，非多采样附件总是被丢弃（避免复杂的 load 操作）
 */
void OpenGLDriver::beginRenderPass(Handle<HwRenderTarget> rth,
        const RenderPassParams& params) {
    DEBUG_MARKER()

    // 着色器编译服务 tick（处理异步编译任务）
    getShaderCompilerService().tick();

    auto& gl = mContext;

    // 保存渲染目标和参数（用于 endRenderPass）
    mRenderPassTarget = rth;
    mRenderPassParams = params;

    GLRenderTarget const* rt = handle_cast<GLRenderTarget*>(rth);

    // 如果渲染到默认渲染目标（即当前 SwapChain），从那里获取输出色彩空间
    // 否则总是使用线性色彩空间
    assert_invariant(!rt->gl.isDefault || mCurrentDrawSwapChain);
    mRec709OutputColorspace = rt->gl.isDefault ? mCurrentDrawSwapChain->rec709 : false;

    // 计算需要清除和丢弃的缓冲区标志
    const TargetBufferFlags clearFlags = params.flags.clear & rt->targets;
    TargetBufferFlags discardFlags = params.flags.discardStart & rt->targets;

    // 绑定 Framebuffer（FBO = 0 表示默认帧缓冲区）
    GLuint const fbo = gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
    CHECK_GL_FRAMEBUFFER_STATUS(GL_FRAMEBUFFER)

    // 每个渲染通道开始时禁用裁剪测试
    gl.disable(GL_SCISSOR_TEST);

    // 处理丢弃缓冲区（性能优化：避免回写不需要的缓冲区）
    if (gl.ext.EXT_discard_framebuffer
            && !gl.bugs.disable_invalidate_framebuffer) {
        // 使用 glInvalidateFramebuffer 丢弃缓冲区（推荐，性能更好）
        AttachmentArray attachments; // NOLINT
        if (GLsizei const attachmentCount = getAttachments(attachments, discardFlags, !fbo)) {
            gl.procs.invalidateFramebuffer(GL_FRAMEBUFFER, attachmentCount, attachments.data());
        }
        CHECK_GL_ERROR()
    } else {
        // 如果不支持丢弃扩展，使用 glClear 清除缓冲区
        // 清除帧缓冲区很重要，因为它将帧缓冲区重置为已知状态
        // （重置帧缓冲区压缩和其他可能的状态）
        clearWithRasterPipe(discardFlags & ~clearFlags, { 0.0f }, 0.0f, 0);
    }

    // 处理 MSAA RenderTarget 的特殊情况
    if (rt->gl.fbo_read) {
        // 我们有一个多采样 RenderTarget，带有非多采样附件
        // （即 EXT_multisampled_render_to_texture 模拟）
        // 我们需要执行"向后"resolve，即将已解析的纹理加载到 tile 中
        // 但 Filament 规定，多采样 RenderTarget 的非多采样附件总是被丢弃
        // 这样做是因为在 Metal 上实现 load 并不简单，而且我们目前不依赖此功能
        discardFlags |= rt->gl.resolve;
    }

    // 清除缓冲区（颜色、深度、模板）
    if (any(clearFlags)) {
        clearWithRasterPipe(clearFlags,
                params.clearColor, GLfloat(params.clearDepth), GLint(params.clearStencil));
    }

    // 在调用 clearWithRasterPipe() 后需要重置这些标志
    mRenderPassColorWrite   = any(clearFlags & TargetBufferFlags::COLOR_ALL);
    mRenderPassDepthWrite   = any(clearFlags & TargetBufferFlags::DEPTH);
    mRenderPassStencilWrite = any(clearFlags & TargetBufferFlags::STENCIL);

    // 设置视口（注意：OpenGL 使用左下角为原点）
    static_assert(sizeof(GLsizei) >= sizeof(uint32_t));
    gl.viewport(params.viewport.left, params.viewport.bottom,
            GLsizei(std::min(uint32_t(std::numeric_limits<int32_t>::max()), params.viewport.width)),
            GLsizei(std::min(uint32_t(std::numeric_limits<int32_t>::max()), params.viewport.height)));

    // 设置深度范围
    gl.depthRange(params.depthRange.near, params.depthRange.far);

#ifndef NDEBUG
    // 在调试构建中清除丢弃的（但不是已清除的）缓冲区（用红色标记，便于调试）
    clearWithRasterPipe(discardFlags & ~clearFlags,
            { 1, 0, 0, 1 }, 1.0, 0);
#endif
}

/**
 * 结束当前渲染通道
 * 
 * 在渲染通道结束时调用，主要职责：
 * 1. 执行 MSAA resolve（如果有）
 * 2. 处理结束时的丢弃缓冲区（性能优化）
 * 3. 清理渲染通道状态
 * 
 * 执行流程：
 * 1. 验证渲染通道已开始（必须有 beginRenderPass）
 * 2. 计算结束时的丢弃标志
 * 3. 执行 MSAA resolve（如果有 fbo_read，需要将多采样缓冲区解析到纹理）
 * 4. 根据实际写入情况调整丢弃标志（未写入的缓冲区不需要丢弃）
 * 5. 对于默认 RT，考虑平台保留标志（某些平台需要保留某些缓冲区）
 * 6. 处理结束时的丢弃缓冲区（使用 glInvalidateFramebuffer 或 glClear）
 * 7. 调试模式下清除丢弃的缓冲区（用绿色标记）
 * 8. 清除渲染通道状态
 * 
 * 性能优化：
 * - 只丢弃实际写入的缓冲区（避免无效操作）
 * - 使用 glInvalidateFramebuffer 避免回写，提高性能
 * - 某些驱动 bug 需要特殊处理（只在开始时丢弃时才在结束时丢弃）
 */
void OpenGLDriver::endRenderPass(int) {
    DEBUG_MARKER()
    auto& gl = mContext;

    // 验证渲染通道已开始
    assert_invariant(mRenderPassTarget); // endRenderPass() 在没有 beginRenderPass() 的情况下被调用？

    GLRenderTarget const* const rt = handle_cast<GLRenderTarget*>(mRenderPassTarget);

    // 计算结束时的丢弃标志
    TargetBufferFlags discardFlags = mRenderPassParams.flags.discardEnd & rt->targets;
    
    // 如果有 fbo_read，执行 MSAA resolve（将多采样缓冲区解析到纹理）
    if (rt->gl.fbo_read) {
        resolvePass(ResolveAction::STORE, rt, discardFlags);
    }

    // 根据实际写入情况调整丢弃标志
    // 如果缓冲区根本没有写入，忽略丢弃标志（避免无效操作）
    if (!mRenderPassColorWrite) {
        discardFlags &= ~TargetBufferFlags::COLOR_ALL;
    }
    if (!mRenderPassDepthWrite) {
        discardFlags &= ~TargetBufferFlags::DEPTH;
    }
    if (!mRenderPassStencilWrite) {
        discardFlags &= ~TargetBufferFlags::STENCIL;
    }

    // 对于默认 RT，考虑平台保留标志（某些平台需要保留某些缓冲区）
    if (rt->gl.isDefault) {
        assert_invariant(mCurrentDrawSwapChain);
        discardFlags &= ~mPlatform.getPreservedFlags(mCurrentDrawSwapChain->swapChain);
    }

    // 处理结束时的丢弃缓冲区
    if (gl.ext.EXT_discard_framebuffer) {
        auto effectiveDiscardFlags = discardFlags;
        // 某些驱动 bug：只在开始时丢弃时才在结束时丢弃
        if (gl.bugs.invalidate_end_only_if_invalidate_start) {
            effectiveDiscardFlags &= mRenderPassParams.flags.discardStart;
        }
        if (!gl.bugs.disable_invalidate_framebuffer) {
            // 如果我们有 glInvalidateNamedFramebuffer()，就不需要绑定帧缓冲区
            GLuint const fbo = gl.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
            AttachmentArray attachments; // NOLINT
            if (GLsizei const attachmentCount = getAttachments(attachments, effectiveDiscardFlags, !fbo)) {
                gl.procs.invalidateFramebuffer(GL_FRAMEBUFFER, attachmentCount, attachments.data());
            }
           CHECK_GL_ERROR()
        }
    }

#ifndef NDEBUG
    // 在调试构建中清除丢弃的缓冲区（用绿色标记，便于调试）
    mContext.bindFramebuffer(GL_FRAMEBUFFER, rt->gl.fbo);
    mContext.disable(GL_SCISSOR_TEST);
    clearWithRasterPipe(discardFlags,
            { 0, 1, 0, 1 }, 1.0, 0);
#endif

    // 清除渲染通道状态
    mRenderPassTarget.clear();
}


/**
 * 下一个子通道
 * 
 * OpenGL 不支持子通道（Subpass），此方法为空实现。
 * 子通道是 Vulkan 的概念，用于优化多通道渲染。
 */
void OpenGLDriver::nextSubpass(int) {}

/**
 * 执行 MSAA Resolve
 * 
 * 将多采样缓冲区解析到非多采样纹理。
 * 用于 EXT_multisampled_render_to_texture 扩展的模拟。
 * 
 * @param action Resolve 操作（LOAD 或 STORE）
 *              - LOAD: 从非多采样纹理加载到多采样缓冲区
 *              - STORE: 从多采样缓冲区解析到非多采样纹理
 * @param rt 渲染目标指针
 * @param discardFlags 丢弃标志（被丢弃的缓冲区不进行 resolve）
 * 
 * 执行流程：
 * 1. ES 2.0 不支持手动 resolve，直接返回
 * 2. 验证 fbo_read 存在（必须有解析目标）
 * 3. 计算需要 resolve 的缓冲区（排除被丢弃的）
 * 4. 如果 mask 非空：
 *    - 验证只 resolve COLOR0（当前限制）
 *    - 根据 action 确定 read/draw FBO：
 *      * STORE: read=多采样, draw=非多采样
 *      * LOAD: read=非多采样, draw=多采样
 *    - 绑定 read 和 draw framebuffer
 *    - 禁用裁剪测试
 *    - 使用 glBlitFramebuffer 执行 resolve
 * 
 * 注意：
 * - 当前只支持 COLOR0 的 resolve
 * - 使用 glBlitFramebuffer 进行 resolve（GL_NEAREST 过滤）
 * - 这是 EXT_multisampled_render_to_texture 的模拟实现
 */
void OpenGLDriver::resolvePass(ResolveAction const action, GLRenderTarget const* rt,
        TargetBufferFlags const discardFlags) noexcept {

    // ES 2.0 不支持手动 resolve
    if (UTILS_UNLIKELY(getContext().isES2())) {
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    assert_invariant(rt->gl.fbo_read);
    auto& gl = mContext;
    // 计算需要 resolve 的缓冲区（排除被丢弃的）
    const TargetBufferFlags resolve = rt->gl.resolve & ~discardFlags;
    GLbitfield const mask = getAttachmentBitfield(resolve);
    if (UTILS_UNLIKELY(mask)) {

        // 我们目前只能 resolve COLOR0
        assert_invariant(!(rt->targets &
                (TargetBufferFlags::COLOR_ALL & ~TargetBufferFlags::COLOR0)));

        GLint read = GLint(rt->gl.fbo_read);
        GLint draw = GLint(rt->gl.fbo);
        // 根据 action 确定 read/draw FBO
        if (action == ResolveAction::STORE) {
            std::swap(read, draw);  // STORE: 从多采样解析到非多采样
        }
        gl.bindFramebuffer(GL_READ_FRAMEBUFFER, read);
        gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);

        CHECK_GL_FRAMEBUFFER_STATUS(GL_READ_FRAMEBUFFER)
        CHECK_GL_FRAMEBUFFER_STATUS(GL_DRAW_FRAMEBUFFER)

        // 禁用裁剪测试并执行 resolve
        gl.disable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, GLint(rt->width), GLint(rt->height),
                0, 0, GLint(rt->width), GLint(rt->height), mask, GL_NEAREST);
        CHECK_GL_ERROR()
    }
#endif
}

/**
 * 获取附件数组
 * 
 * 将 TargetBufferFlags 转换为 OpenGL 附件常量数组。
 * 用于 glInvalidateFramebuffer 等 API。
 * 
 * @param attachments 输出参数，附件常量数组
 * @param buffers 目标缓冲区标志
 * @param isDefaultFramebuffer 是否是默认帧缓冲区
 * @return 附件数量
 * 
 * 执行流程：
 * 1. 遍历所有颜色附件（COLOR0-COLOR7）
 * 2. 处理深度附件（DEPTH）
 * 3. 处理模板附件（STENCIL）
 * 4. 注意：默认帧缓冲区使用不同的常量（GL_COLOR 而不是 GL_COLOR_ATTACHMENT0）
 * 
 * 注意：
 * - 默认帧缓冲区和 FBO 使用不同的附件常量
 * - ES 2.0 只支持 COLOR0
 */
GLsizei OpenGLDriver::getAttachments(AttachmentArray& attachments,
        TargetBufferFlags const buffers, bool const isDefaultFramebuffer) noexcept {
    GLsizei attachmentCount = 0;
    // 默认帧缓冲区使用不同的常量！！！

    // COLOR0 附件
    if (any(buffers & TargetBufferFlags::COLOR0)) {
        attachments[attachmentCount++] = isDefaultFramebuffer ? GL_COLOR : GL_COLOR_ATTACHMENT0;
    }
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    if (any(buffers & TargetBufferFlags::COLOR1)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT1;
    }
    if (any(buffers & TargetBufferFlags::COLOR2)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT2;
    }
    if (any(buffers & TargetBufferFlags::COLOR3)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT3;
    }
    if (any(buffers & TargetBufferFlags::COLOR4)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT4;
    }
    if (any(buffers & TargetBufferFlags::COLOR5)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT5;
    }
    if (any(buffers & TargetBufferFlags::COLOR6)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT6;
    }
    if (any(buffers & TargetBufferFlags::COLOR7)) {
        assert_invariant(!isDefaultFramebuffer);
        attachments[attachmentCount++] = GL_COLOR_ATTACHMENT7;
    }
#endif
    if (any(buffers & TargetBufferFlags::DEPTH)) {
        attachments[attachmentCount++] = isDefaultFramebuffer ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
    }
    if (any(buffers & TargetBufferFlags::STENCIL)) {
        attachments[attachmentCount++] = isDefaultFramebuffer ? GL_STENCIL : GL_STENCIL_ATTACHMENT;
    }
    return attachmentCount;
}

/**
 * 设置裁剪矩形
 * 
 * 设置裁剪矩形，自动裁剪到视口范围内。
 * 裁剪测试用于限制渲染区域，只渲染指定矩形内的像素。
 * 
 * @param scissor 裁剪矩形（视口坐标）
 * 
 * 执行流程：
 * 1. 如果裁剪矩形覆盖整个有效区域（从 0,0 开始，尺寸 >= maxvalu）：
 *    - 禁用裁剪测试（优化：避免不必要的裁剪）
 *    - 直接返回
 * 2. 否则：
 *    - 设置裁剪矩形（通过 OpenGLContext，使用状态缓存）
 *    - 启用裁剪测试
 * 
 * 性能优化：
 * - 如果裁剪矩形覆盖整个区域，禁用裁剪测试（避免驱动开销）
 * - 使用 OpenGLContext 的状态缓存，避免重复设置
 * 
 * 注意：
 * - 裁剪矩形会自动裁剪到视口范围内
 * - TODO: 当裁剪矩形大于当前表面时，是否应该禁用裁剪？
 */
void OpenGLDriver::setScissor(Viewport const& scissor) noexcept {
    constexpr uint32_t maxvalu = std::numeric_limits<int32_t>::max();

    auto& gl = mContext;

    // TODO: 当裁剪矩形大于当前表面时，是否应该禁用裁剪？
    // 如果裁剪矩形覆盖整个有效区域，禁用裁剪测试（优化）
    if (scissor.left == 0 && scissor.bottom == 0 &&
        scissor.width >= maxvalu && scissor.height >= maxvalu) {
        gl.disable(GL_SCISSOR_TEST);
        return;
    }

    // 设置裁剪矩形并启用裁剪测试
    gl.setScissor(
            GLint(scissor.left), GLint(scissor.bottom),
            GLint(scissor.width), GLint(scissor.height));
    gl.enable(GL_SCISSOR_TEST);
}

// ------------------------------------------------------------------------------------------------
// Setting rendering state
// ------------------------------------------------------------------------------------------------

/**
 * 插入事件标记
 * 
 * 在 OpenGL 命令队列中插入事件标记，用于调试工具（如 RenderDoc、Xcode GPU Debugger）。
 * 
 * @param string 标记名称
 * 
 * 注意：
 * - 需要驱动支持 EXT_debug_marker 扩展
 * - WebGL 不支持
 */
void OpenGLDriver::insertEventMarker(char const* string) {
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_debug_marker
    auto const& gl = mContext;
    if (gl.ext.EXT_debug_marker) {
        glInsertEventMarkerEXT(GLsizei(strlen(string)), string);
    }
#endif
#endif
}

/**
 * 推送组标记
 * 
 * 开始一个调试组，用于在调试工具中组织命令。
 * 必须与 popGroupMarker 配对使用。
 * 
 * @param string 组名称
 * 
 * 支持两种类型的标记：
 * 1. OpenGL 调试标记（glPushGroupMarkerEXT）：需要驱动支持
 * 2. 后端标记（Perfetto）：用于性能分析
 */
void OpenGLDriver::pushGroupMarker(char const* string) {
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_debug_marker
#if DEBUG_GROUP_MARKER_LEVEL & DEBUG_GROUP_MARKER_OPENGL
    // OpenGL 调试标记
    if (UTILS_LIKELY(mContext.ext.EXT_debug_marker)) {
        glPushGroupMarkerEXT(GLsizei(strlen(string)), string);
    }
#endif
#endif

#if DEBUG_GROUP_MARKER_LEVEL & DEBUG_GROUP_MARKER_BACKEND
    // 后端标记（Perfetto）
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, string);
#endif
#endif
}

/**
 * 弹出组标记
 * 
 * 结束一个调试组，与 pushGroupMarker 配对使用。
 * 
 * @param 未使用的参数（保持接口一致性）
 */
void OpenGLDriver::popGroupMarker(int) {
#ifndef __EMSCRIPTEN__
#ifdef GL_EXT_debug_marker
#if DEBUG_GROUP_MARKER_LEVEL & DEBUG_GROUP_MARKER_OPENGL
    // OpenGL 调试标记
    if (UTILS_LIKELY(mContext.ext.EXT_debug_marker)) {
        glPopGroupMarkerEXT();
    }
#endif
#endif

#if DEBUG_GROUP_MARKER_LEVEL & DEBUG_GROUP_MARKER_BACKEND
    // 后端标记（Perfetto）
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);
#endif
#endif
}

/**
 * 开始捕获
 * 
 * 开始 GPU 命令捕获（用于调试工具）。
 * OpenGL 后端当前未实现，此方法为空。
 * 
 * @param 未使用的参数（保持接口一致性）
 */
void OpenGLDriver::startCapture(int) {
}

/**
 * 停止捕获
 * 
 * 停止 GPU 命令捕获。
 * OpenGL 后端当前未实现，此方法为空。
 * 
 * @param 未使用的参数（保持接口一致性）
 */
void OpenGLDriver::stopCapture(int) {
}

// ------------------------------------------------------------------------------------------------
// Read-back ops
// ------------------------------------------------------------------------------------------------

/**
 * 读取像素
 * 
 * 从渲染目标读取像素数据到 CPU 内存。
 * 支持异步读取（使用 PBO）和同步读取（ES 2.0）。
 * 
 * @param src 源渲染目标句柄
 * @param x 读取区域的 X 坐标
 * @param y 读取区域的 Y 坐标
 * @param width 读取区域的宽度
 * @param height 读取区域的高度
 * @param p 像素缓冲区描述符（格式、类型、对齐、步长等）
 * 
 * 执行流程：
 * 1. 获取格式和类型（GLenum）
 * 2. 设置像素打包对齐
 * 3. ES 2.0 路径（同步读取）：
 *    - 分配临时缓冲区
 *    - 绑定 framebuffer（使用 fbo_read 如果有）
 *    - 调用 glReadPixels（同步，可能阻塞）
 *    - 垂直翻转缓冲区（OpenGL 使用左下角原点）
 *    - 复制到用户缓冲区
 * 4. ES 3.0+/GL 4.1+ 路径（异步读取）：
 *    - 创建 PBO（Pixel Buffer Object）
 *    - 绑定 PBO 并分配存储
 *    - 调用 glReadPixels（异步，不阻塞）
 *    - 调度回调，在 GPU 完成时：
 *      * 映射 PBO 到 CPU 内存
 *      * 垂直翻转缓冲区
 *      * 复制到用户缓冲区
 *      * 取消映射并删除 PBO
 * 
 * 性能优化：
 * - ES 3.0+ 使用 PBO 实现异步读取，避免阻塞 CPU
 * - 自动处理 MSAA resolve（使用 fbo_read）
 * - 垂直翻转在 CPU 端完成（OpenGL 使用左下角原点）
 * 
 * 注意：
 * - 图像垂直翻转：OpenGL 使用左下角原点，Filament 使用左上角原点
 * - ES 2.0 使用同步读取（可能阻塞）
 * - ES 3.0+ 使用异步读取（不阻塞）
 */
void OpenGLDriver::readPixels(Handle<HwRenderTarget> src,
        uint32_t const x, uint32_t const y, uint32_t width, uint32_t height,
        PixelBufferDescriptor&& p) {
    DEBUG_MARKER()
    auto& gl = mContext;

    // 获取格式和类型（GLenum）
    GLenum const glFormat = getFormat(p.format);
    GLenum const glType = getType(p.type);

    // 设置像素打包对齐
    gl.pixelStore(GL_PACK_ALIGNMENT, (GLint)p.alignment);

    /*
     * glReadPixel() operation...
     *
     *  Framebuffer as seen on         User buffer
     *  screen
     *  +--------------------+
     *  |                    |                stride         alignment
     *  |                    |         ----------------------->-->
     *  |                    |         +----------------------+--+   low addresses
     *  |                    |         |          |           |  |
     *  |             w      |         |          | bottom    |  |
     *  |       <--------->  |         |          V           |  |
     *  |       +---------+  |         |     +.........+      |  |
     *  |       |     ^   |  | =====>  |     |         |      |  |
     *  |   x   |    h|   |  |         |left |         |      |  |
     *  +------>|     v   |  |         +---->|         |      |  |
     *  |       +.........+  |         |     +---------+      |  |
     *  |            ^       |         |                      |  |
     *  |          y |       |         +----------------------+--+  high addresses
     *  +------------+-------+
     *                                  Image is "flipped" vertically
     *                                  "bottom" is from the "top" (low addresses)
     *                                  of the buffer.
     */

    GLRenderTarget const* s = handle_cast<GLRenderTarget const*>(src);

    using PBD = PixelBufferDescriptor;

    // The PBO only needs to accommodate the area we're reading, with alignment.
    auto const pboSize = GLsizeiptr(PBD::computeDataSize(
            p.format, p.type, width, height, p.alignment));

    // ES 2.0 路径：同步读取（可能阻塞）
    if (UTILS_UNLIKELY(gl.isES2())) {
        void* buffer = malloc(pboSize);
        if (buffer) {
            // 绑定 framebuffer（使用 fbo_read 如果有，用于 MSAA resolve）
            gl.bindFramebuffer(GL_FRAMEBUFFER, s->gl.fbo_read ? s->gl.fbo_read : s->gl.fbo);
            // 同步读取像素（可能阻塞 CPU）
            glReadPixels(GLint(x), GLint(y), GLint(width), GLint(height), glFormat, glType, buffer);
            CHECK_GL_ERROR()

            // 现在需要垂直翻转缓冲区以匹配我们的 API（OpenGL 使用左下角原点）
            size_t const stride = p.stride ? p.stride : width;
            size_t const bpp = PBD::computeDataSize(p.format, p.type, 1, 1, 1);
            size_t const dstBpr = PBD::computeDataSize(p.format, p.type, stride, 1, p.alignment);
            char* pDst = static_cast<char*>(p.buffer) + p.left * bpp + dstBpr * (p.top + height - 1);

            size_t const srcBpr = PBD::computeDataSize(p.format, p.type, width, 1, p.alignment);
            char const* pSrc = static_cast<char const*>(buffer);
            for (size_t i = 0; i < height; ++i) {
                memcpy(pDst, pSrc, bpp * width);
                pSrc += srcBpr;
                pDst -= dstBpr;
            }
        }
        free(buffer);
        scheduleDestroy(std::move(p));
        return;
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // ES 3.0+/GL 4.1+ 路径：异步读取（使用 PBO）
    // glReadPixels 不会自动 resolve，但我们总是模拟 auto-resolve 扩展
    // 所以如果有已解析的 fbo（fbo_read），使用它
    gl.bindFramebuffer(GL_READ_FRAMEBUFFER, s->gl.fbo_read ? s->gl.fbo_read : s->gl.fbo);

    // 创建 PBO（Pixel Buffer Object）用于异步读取
    GLuint pbo;
    glGenBuffers(1, &pbo);
    gl.bindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, pboSize, nullptr, GL_STATIC_DRAW);
    // 异步读取像素（不阻塞 CPU，数据写入 PBO）
    glReadPixels(GLint(x), GLint(y), GLint(width), GLint(height), glFormat, glType, nullptr);
    gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    CHECK_GL_ERROR()

    // 我们被迫在堆上创建副本，否则会删除 std::function<> 的复制构造函数
    auto* const pUserBuffer = new PixelBufferDescriptor(std::move(p));
    // 调度回调，在 GPU 完成时执行
    whenGpuCommandsComplete([this, width, height, pbo, pboSize, pUserBuffer]() mutable {
        PixelBufferDescriptor& p = *pUserBuffer;
        auto& gl = mContext;
        gl.bindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        void const* vaddr = nullptr;
#if defined(__EMSCRIPTEN__)
        std::unique_ptr<uint8_t[]> clientBuffer = std::make_unique<uint8_t[]>(pboSize);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, pboSize, clientBuffer.get());
        vaddr = clientBuffer.get();
#else
        vaddr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, pboSize, GL_MAP_READ_BIT);
#endif
        if (vaddr) {
            // now we need to flip the buffer vertically to match our API
            size_t const stride = p.stride ? p.stride : width;
            size_t const bpp = PBD::computeDataSize(p.format, p.type, 1, 1, 1);
            size_t const dstBpr = PBD::computeDataSize(p.format, p.type, stride, 1, p.alignment);
            char* pDst = static_cast<char*>(p.buffer) + p.left * bpp + dstBpr * (p.top + height - 1);

            size_t const srcBpr = PBD::computeDataSize(p.format, p.type, width, 1, p.alignment);
            char const* pSrc = static_cast<char const*>(vaddr);

            for (size_t i = 0; i < height; ++i) {
                memcpy(pDst, pSrc, bpp * width);
                pSrc += srcBpr;
                pDst -= dstBpr;
            }
#if !defined(__EMSCRIPTEN__)
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
#endif
        }
        gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glDeleteBuffers(1, &pbo);
        scheduleDestroy(std::move(p));
        delete pUserBuffer;
        CHECK_GL_ERROR()
    });
#endif
}

/**
 * 读取缓冲区子数据
 * 
 * 从缓冲区对象读取数据到 CPU 内存。
 * 使用 PBO 实现异步读取，避免阻塞 CPU。
 * 
 * @param boh 缓冲区对象句柄
 * @param offset 读取偏移（字节）
 * @param size 读取大小（字节）
 * @param p 缓冲区描述符（包含用户缓冲区指针）
 * 
 * 执行流程：
 * 1. 验证不是 ES 2.0（ES 2.0 不支持）
 * 2. 创建 PBO（Pixel Buffer Object）
 * 3. 使用 glCopyBufferSubData 将数据从源缓冲区复制到 PBO（异步，不阻塞）
 * 4. 调度回调，在 GPU 完成时：
 *    - 映射 PBO 到 CPU 内存
 *    - 复制数据到用户缓冲区
 *    - 取消映射并删除 PBO
 * 
 * 性能优化：
 * - 使用 PBO 实现异步读取，避免阻塞 CPU
 * - 使用 glCopyBufferSubData 而不是直接映射源缓冲区（避免锁定源缓冲区）
 * 
 * 注意：
 * - ES 2.0 不支持此功能
 * - 数据读取是异步的，回调在 GPU 完成时执行
 */
void OpenGLDriver::readBufferSubData(BufferObjectHandle boh,
        uint32_t const offset, uint32_t size, BufferDescriptor&& p) {
    UTILS_UNUSED_IN_RELEASE auto& gl = mContext;
    assert_invariant(!gl.isES2());

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    GLBufferObject const* bo = handle_cast<GLBufferObject const*>(boh);

    // TODO: 测量两种解决方案的性能
    if constexpr (true) {
        // 方案 1：使用 PBO 异步读取（推荐）
        // 创建 PBO（Pixel Buffer Object）用于异步数据传输
        GLuint pbo;
        glGenBuffers(1, &pbo);
        // 绑定 PBO 并分配存储（GL_PIXEL_PACK_BUFFER 用于读取操作）
        gl.bindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)size, nullptr, GL_STATIC_DRAW);
        // 绑定源缓冲区
        gl.bindBuffer(bo->gl.binding, bo->gl.id);
        // 从源缓冲区复制到 PBO（异步，不阻塞 CPU）
        // 这会将数据从源缓冲区复制到 PBO，但不会等待 GPU 完成
        glCopyBufferSubData(bo->gl.binding, GL_PIXEL_PACK_BUFFER, offset, 0, size);
        // 解绑缓冲区
        gl.bindBuffer(bo->gl.binding, 0);
        gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        CHECK_GL_ERROR()

        // 调度回调：在 GPU 完成复制后映射 PBO 并复制到用户缓冲区
        // 使用 shared_ptr 管理用户缓冲区生命周期（确保回调执行时缓冲区仍然有效）
        auto* pUserBuffer = new BufferDescriptor(std::move(p));
        whenGpuCommandsComplete([this, size, pbo, pUserBuffer]() mutable {
            BufferDescriptor& p = *pUserBuffer;
            auto& gl = mContext;
            // 绑定 PBO
            gl.bindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
            // 映射 PBO 到 CPU 内存（只读）
            if (void const* vaddr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, size, GL_MAP_READ_BIT)) {
                // 复制数据到用户缓冲区
                memcpy(p.buffer, vaddr, size);
                // 取消映射
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            // 解绑并删除 PBO
            gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glDeleteBuffers(1, &pbo);
            // 调度销毁用户缓冲区
            scheduleDestroy(std::move(p));
            delete pUserBuffer;
            CHECK_GL_ERROR()
        });
    } else {
        // 方案 2：直接映射源缓冲区（可能阻塞，不推荐）
        gl.bindBuffer(bo->gl.binding, bo->gl.id);
        // TODO: 这个 glMapBufferRange 可能会阻塞
        // 理想情况下我们想使用 whenGpuCommandsComplete，
        // 但这很棘手，因为 boh 可能在此调用后立即被销毁
        if (void const* vaddr = glMapBufferRange(bo->gl.binding, offset, size, GL_MAP_READ_BIT)) {
            // 直接复制数据到用户缓冲区（可能阻塞，等待 GPU 完成）
            memcpy(p.buffer, vaddr, size);
            glUnmapBuffer(bo->gl.binding);
        }
        gl.bindBuffer(bo->gl.binding, 0);
        scheduleDestroy(std::move(p));
        CHECK_GL_ERROR()
    }
#endif
}


/**
 * 注册"偶尔执行"的操作
 * 
 * 注册一个操作，该操作会在每次 tick() 时执行，直到返回 true。
 * 用于需要定期检查但不需要立即执行的任务（如定时器查询结果检查）。
 * 
 * @param fn 操作函数，返回 true 表示完成（会从队列中移除），false 表示继续执行
 * 
 * 注意：
 * - 操作会在每次 tick() 时执行
 * - 如果操作返回 true，会从队列中移除
 * - 如果操作返回 false，会在下次 tick() 时再次执行
 * - 用于延迟执行的任务（如等待 GPU 查询结果）
 */
void OpenGLDriver::runEveryNowAndThen(std::function<bool()> fn) {
    mEveryNowAndThenOps.push_back(std::move(fn));
}

/**
 * 执行"偶尔执行"的操作
 * 
 * 遍历并执行所有注册的"偶尔执行"操作。
 * 如果操作返回 true（表示完成），则从队列中移除。
 * 
 * 执行流程：
 * 1. 遍历所有注册的操作
 * 2. 执行每个操作
 * 3. 如果操作返回 true（完成），从队列中移除
 * 4. 如果操作返回 false（未完成），保留在队列中，下次继续执行
 * 
 * 注意：
 * - 此方法在 tick() 中调用
 * - 操作可能依赖独立线程发布结果（如定时器查询），所以可能不会立即完成
 * - 即使调用了 glFinish()，某些操作可能仍然未完成
 */
void OpenGLDriver::executeEveryNowAndThenOps() noexcept { // NOLINT(*-exception-escape)
    auto& v = mEveryNowAndThenOps;
    auto it = v.begin();
    while (it != v.end()) {
        // 执行操作，如果返回 true 表示完成，从队列中移除
        if ((*it)()) {
            it = v.erase(it);  // 不能抛出异常（通过构造保证）
        } else {
            // 未完成，保留在队列中，下次继续执行
            ++it;
        }
    }
}

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
/**
 * 注册帧完成回调
 * 
 * 注册一个回调，在帧完成时执行（在 commit() 时调度）。
 * 
 * @param fn 回调函数
 * 
 * 注意：
 * - 回调在 commit() 时调度到 GPU 完成时执行
 * - 用于在帧完成时执行清理或统计任务
 * - ES 2.0 不支持此功能
 */
void OpenGLDriver::whenFrameComplete(const std::function<void()>& fn) {
    mFrameCompleteOps.push_back(fn);
}

/**
 * 注册 GPU 命令完成回调
 * 
 * 注册一个回调，在 GPU 完成所有已提交的命令时执行。
 * 使用 GLsync 对象实现异步等待。
 * 
 * @param fn 回调函数
 * 
 * 执行流程：
 * 1. 创建 GLsync 对象（glFenceSync），标记当前 GPU 命令位置
 * 2. 将 sync 对象和回调添加到队列
 * 3. 在 executeGpuCommandsCompleteOps() 中检查 sync 状态
 * 4. 当 sync 信号时，执行回调
 * 
 * 性能优化：
 * - 使用 GLsync 实现非阻塞等待
 * - 回调在 tick() 时检查，不会阻塞主线程
 * 
 * 注意：
 * - ES 2.0 不支持此功能
 * - 回调是异步的，不会立即执行
 * - 用于在 GPU 完成时执行清理或数据读取任务
 */
void OpenGLDriver::whenGpuCommandsComplete(const std::function<void()>& fn) {
    // 创建 GLsync 对象，标记当前 GPU 命令位置
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    // 将 sync 对象和回调添加到队列
    mGpuCommandCompleteOps.emplace_back(sync, fn);
    CHECK_GL_ERROR()
}

/**
 * 执行 GPU 命令完成回调
 * 
 * 检查所有 GLsync 对象的状态，如果已信号，执行对应的回调。
 * 
 * 执行流程：
 * 1. 遍历所有注册的 sync 对象和回调
 * 2. 使用 glClientWaitSync 检查 sync 状态（非阻塞，timeout=0）
 * 3. 根据状态处理：
 *    - GL_TIMEOUT_EXPIRED: 未就绪，保留在队列中
 *    - GL_ALREADY_SIGNALED/GL_CONDITION_SATISFIED: 已就绪，执行回调并移除
 *    - 其他: 错误情况，清理 sync 对象并移除（避免泄漏）
 * 
 * 性能优化：
 * - 使用非阻塞检查（timeout=0），不会阻塞 CPU
 * - 未就绪的 sync 对象保留在队列中，下次继续检查
 * 
 * 注意：
 * - 此方法在 tick() 中调用
 * - ES 2.0 不支持此功能
 * - 错误情况下会清理 sync 对象，避免资源泄漏
 */
void OpenGLDriver::executeGpuCommandsCompleteOps() noexcept { // NOLINT(*-exception-escape)
    auto& v = mGpuCommandCompleteOps;
    auto it = v.begin();
    while (it != v.end()) {
        auto const& [sync, fn] = *it;
        // 非阻塞检查 sync 状态（timeout=0，立即返回）
        GLenum const syncStatus = glClientWaitSync(sync, 0, 0u);
        switch (syncStatus) {
            case GL_TIMEOUT_EXPIRED:
                // 未就绪，保留在队列中，下次继续检查
                ++it;
                break;
            case GL_ALREADY_SIGNALED:
            case GL_CONDITION_SATISFIED:
                // 已就绪，执行回调
                it->second();
                // 删除 sync 对象
                glDeleteSync(sync);
                // 从队列中移除
                it = v.erase(it);  // 不能抛出异常（通过构造保证）
                break;
            default:
                // 这不应该发生，但如果发生会非常有问题，因为可能会泄漏数据
                // （取决于回调做什么）。但是我们会清理自己的状态。
                glDeleteSync(sync);
                it = v.erase(it);  // 不能抛出异常（通过构造保证）
                break;
        }
    }
}
#endif

// ------------------------------------------------------------------------------------------------
// Rendering ops
// ------------------------------------------------------------------------------------------------

/**
 * 驱动 Tick
 * 
 * 每帧调用一次，执行驱动的定期维护任务。
 * 包括检查 GPU 命令完成、执行延迟任务、更新着色器编译服务。
 * 
 * @param 未使用的参数（保持接口一致性）
 * 
 * 执行流程：
 * 1. 执行 GPU 命令完成回调（检查 GLsync 对象，执行已完成的回调）
 * 2. 执行"偶尔执行"的操作（如定时器查询结果检查）
 * 3. Tick 着色器编译服务（检查异步编译完成情况）
 * 
 * 性能优化：
 * - 所有操作都是非阻塞的
 * - 用于定期维护，不会影响渲染性能
 * 
 * 注意：
 * - 此方法在每帧调用（通常在 beginFrame 或 endFrame 时）
 * - 所有操作都是异步的，不会阻塞主线程
 */
void OpenGLDriver::tick(int) {
    DEBUG_MARKER()
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 检查并执行 GPU 命令完成回调
    executeGpuCommandsCompleteOps();
#endif
    // 执行"偶尔执行"的操作（如定时器查询结果检查）
    executeEveryNowAndThenOps();
    // Tick 着色器编译服务（检查异步编译完成情况）
    getShaderCompilerService().tick();
}

/**
 * 开始新的一帧渲染
 * 
 * 这是每帧渲染的入口点，在渲染循环开始时调用。主要职责：
 * 1. 通知平台层开始新帧（用于帧同步、VSync 等）
 * 2. 更新外部纹理流（如相机预览、视频流等）
 * 3. 插入性能分析标记
 * 
 * @param monotonic_clock_ns 单调时钟时间戳（纳秒），用于帧同步
 * @param refreshIntervalNs 刷新间隔（纳秒），用于 VSync 计算
 * @param frameId 帧 ID，用于帧追踪和调试
 * 
 * 执行流程：
 * 1. 插入性能分析标记（用于性能分析工具）
 * 2. 通知平台层开始新帧（可能触发 VSync 等待）
 * 3. 更新所有附加了外部流的纹理：
 *    - 对于 NATIVE 类型流（如相机预览），调用平台接口更新纹理图像
 *    - 更新纹理时间戳
 *    - 绑定并更新外部纹理（GL_TEXTURE_EXTERNAL_OES）
 * 
 * 注意：
 * - 外部纹理流通常来自相机、视频播放器等，需要在每帧更新
 * - OpenGLPlatform::updateTexImage() 会绑定纹理，所以这里只需要更新图像
 */
void OpenGLDriver::beginFrame(
        UTILS_UNUSED int64_t const monotonic_clock_ns,
        UTILS_UNUSED int64_t const refreshIntervalNs,
        UTILS_UNUSED uint32_t const frameId) {
    PROFILE_MARKER(PROFILE_NAME_BEGINFRAME)  // 性能分析标记
    auto& gl = mContext;
    insertEventMarker("beginFrame");  // 插入调试事件标记
    
    // 通知平台层开始新帧（可能触发 VSync 等待、帧同步等）
    mPlatform.beginFrame(monotonic_clock_ns, refreshIntervalNs, frameId);
    
    // 更新所有附加了外部流的纹理（如相机预览、视频流等）
    if (UTILS_UNLIKELY(!mTexturesWithStreamsAttached.empty())) {
        OpenGLPlatform& platform = mPlatform;
        for (GLTexture const* t : mTexturesWithStreamsAttached) {
            assert_invariant(t && t->hwStream);
            if (t->hwStream->streamType == StreamType::NATIVE) {
                assert_invariant(t->hwStream->stream);
                // 更新外部纹理图像（从平台层获取最新帧）
                platform.updateTexImage(t->hwStream->stream,
                        &static_cast<GLStream*>(t->hwStream)->user_thread.timestamp);
                // 注意：OpenGLPlatform::updateTexImage() 会绑定纹理，这里只需要更新图像
                gl.updateTexImage(GL_TEXTURE_EXTERNAL_OES, t->gl.id);
            }
        }
    }
}

/**
 * 设置帧调度回调
 * 
 * 设置交换链的帧调度回调，在帧被调度到显示队列时调用。
 * 
 * @param sch 交换链句柄
 * @param handler 回调处理器
 * @param callback 回调函数（nullptr 表示清除回调）
 * @param flags 标志（未使用）
 * 
 * 执行流程：
 * 1. 如果回调为空（nullptr）：
 *    - 清除处理器和回调
 *    - 返回
 * 2. 否则：
 *    - 保存处理器和回调（使用 shared_ptr 管理生命周期）
 * 
 * 注意：
 * - 回调在 commit() 时调度（在渲染线程执行）
 * - 回调用于通知帧已调度到显示队列
 * - 使用 shared_ptr 确保回调生命周期正确
 */
void OpenGLDriver::setFrameScheduledCallback(Handle<HwSwapChain> sch, CallbackHandler* handler,
        FrameScheduledCallback&& callback, uint64_t /*flags*/) {
    DEBUG_MARKER()
    GLSwapChain* sc = handle_cast<GLSwapChain*>(sch);
    if (!callback) {
        // 清除回调
        sc->frameScheduled.handler = nullptr;
        sc->frameScheduled.callback.reset();
        return;
    }
    // 保存回调（使用 shared_ptr 管理生命周期）
    sc->frameScheduled.handler = handler;
    sc->frameScheduled.callback = std::make_shared<FrameScheduledCallback>(std::move(callback));
}

/**
 * 设置帧完成回调
 * 
 * 设置交换链的帧完成回调，在帧完成渲染时调用。
 * OpenGL 后端当前未实现，此方法为空。
 * 
 * @param 交换链句柄（未使用）
 * @param 回调处理器（未使用）
 * @param 回调函数（未使用）
 * 
 * 注意：
 * - OpenGL 后端当前未实现此功能
 * - 帧完成通知通过其他机制实现（如 Fence）
 */
void OpenGLDriver::setFrameCompletedCallback(Handle<HwSwapChain>,
        CallbackHandler*, Invocable<void()>&& /*callback*/) {
    DEBUG_MARKER()
}

/**
 * 设置呈现时间
 * 
 * 设置交换链的呈现时间，用于帧同步和时序控制。
 * 
 * @param monotonic_clock_ns 单调时钟时间（纳秒）
 * 
 * 注意：
 * - 呈现时间用于帧同步（如 Vsync）
 * - 使用单调时钟（不受系统时间调整影响）
 * - 委托给平台层实现
 */
void OpenGLDriver::setPresentationTime(int64_t const monotonic_clock_ns) {
    DEBUG_MARKER()
    mPlatform.setPresentationTime(monotonic_clock_ns);
}

/**
 * 结束当前帧的渲染
 * 
 * 这是每帧渲染的出口点，在渲染循环结束时调用。主要职责：
 * 1. 通知平台层结束帧（交换缓冲区、呈现到屏幕）
 * 2. 在 WebGL 环境下重置 OpenGL 状态（单线程环境需要）
 * 3. 插入性能分析标记
 * 
 * @param frameId 帧 ID，用于帧追踪和调试
 * 
 * 执行流程：
 * 1. 插入性能分析标记
 * 2. WebGL 特殊处理：重置 OpenGL 状态
 *    - 绑定 VAO 为 0（避免状态泄漏）
 *    - 解绑所有纹理单元（避免状态泄漏）
 *    - 重置渲染状态（裁剪、深度测试等）
 * 3. 通知平台层结束帧（交换前后缓冲区，呈现到屏幕）
 * 4. 插入调试事件标记
 * 
 * 注意：
 * - WebGL 是单线程的，用户可能在帧结束后操作 GL 状态，所以需要重置
 * - 桌面 OpenGL 是多线程的，不需要重置状态
 * - 通常不调用 glFinish()，让 GPU 异步执行，提高性能
 */
void OpenGLDriver::endFrame(UTILS_UNUSED uint32_t const frameId) {
    PROFILE_MARKER(PROFILE_NAME_ENDFRAME)  // 性能分析标记
    
#if defined(__EMSCRIPTEN__)
    // WebGL 构建是单线程的，用户可能在帧结束后操作 GL 状态
    // 我们不正式支持这种方式，但至少可以做一些基本的安全处理
    // 例如重置 VAO 为 0，避免状态泄漏
    auto& gl = mContext;
    gl.bindVertexArray(nullptr);  // 重置 VAO
    // 解绑所有纹理单元
    for (int unit = OpenGLContext::DUMMY_TEXTURE_BINDING; unit >= 0; unit--) {
        gl.bindTexture(unit, GL_TEXTURE_2D, 0, false);
    }
    // 重置渲染状态
    gl.disable(GL_CULL_FACE);
    gl.depthFunc(GL_LESS);
    gl.disable(GL_SCISSOR_TEST);
#endif
    
    // 注意：通常不调用 glFinish()，让 GPU 异步执行，提高性能
    //FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "glFinish");
    //glFinish();
    
    // 通知平台层结束帧（交换前后缓冲区，呈现到屏幕）
    mPlatform.endFrame(frameId);
    insertEventMarker("endFrame");  // 插入调试事件标记
}

/**
 * 更新描述符集中的缓冲区绑定
 * 
 * 更新描述符集中指定绑定的缓冲区对象。
 * 
 * @param dsh 描述符集句柄
 * @param binding 绑定索引
 * @param boh 缓冲区对象句柄（nullptr 表示解绑）
 * @param offset 缓冲区偏移（字节）
 * @param size 缓冲区大小（字节）
 * 
 * 执行流程：
 * 1. 获取描述符集对象
 * 2. 获取缓冲区对象（如果句柄有效）
 * 3. 调用描述符集的 update 方法更新绑定
 * 
 * 注意：
 * - 用于更新 uniform 缓冲区、存储缓冲区等
 * - 偏移和大小用于动态 uniform 缓冲区
 * - nullptr 句柄表示解绑
 */
void OpenGLDriver::updateDescriptorSetBuffer(
        DescriptorSetHandle dsh,
        descriptor_binding_t const binding,
        BufferObjectHandle boh,
        uint32_t const offset, uint32_t const size) {
    GLDescriptorSet* ds = handle_cast<GLDescriptorSet*>(dsh);
    GLBufferObject* bo = boh ? handle_cast<GLBufferObject*>(boh) : nullptr;
    ds->update(mContext, binding, bo, offset, size);
}

/**
 * 更新描述符集中的纹理绑定
 * 
 * 更新描述符集中指定绑定的纹理和采样器参数。
 * 
 * @param dsh 描述符集句柄
 * @param binding 绑定索引
 * @param th 纹理句柄
 * @param params 采样器参数（过滤、包装等）
 * 
 * 执行流程：
 * 1. 获取描述符集对象
 * 2. 调用描述符集的 update 方法更新绑定
 * 
 * 注意：
 * - 用于更新纹理和采样器绑定
 * - 采样器参数定义纹理采样方式（过滤、包装等）
 * - 纹理和采样器可以分别绑定（ES 3.0+）或组合绑定（ES 2.0）
 */
void OpenGLDriver::updateDescriptorSetTexture(
        DescriptorSetHandle dsh,
        descriptor_binding_t const binding,
        TextureHandle th,
        SamplerParams const params) {
    GLDescriptorSet* ds = handle_cast<GLDescriptorSet*>(dsh);
    ds->update(mContext, mHandleAllocator, binding, th, params);
}

/**
 * 复制数据到内存映射缓冲区
 * 
 * 将数据复制到内存映射缓冲区中。
 * 内存映射缓冲区允许 CPU 直接访问 GPU 缓冲区。
 * 
 * @param mmbh 内存映射缓冲区句柄
 * @param offset 目标偏移（字节）
 * @param data 要复制的数据
 * 
 * 执行流程：
 * 1. 获取内存映射缓冲区对象
 * 2. 调用缓冲区的 copy 方法复制数据
 * 
 * 注意：
 * - 内存映射缓冲区允许 CPU 直接访问 GPU 缓冲区
 * - 复制操作可能需要同步（取决于映射模式）
 * - 数据在复制后会被销毁（移动语义）
 */
void OpenGLDriver::copyToMemoryMappedBuffer(MemoryMappedBufferHandle mmbh, size_t offset,
        BufferDescriptor&& data) {
    GLMemoryMappedBuffer* const mmb = handle_cast<GLMemoryMappedBuffer*>(mmbh);
    mmb->copy(mContext, *this, offset, std::move(data));
}

/**
 * 刷新命令缓冲区
 * 
 * 将当前命令缓冲区中的 OpenGL 命令提交给 GPU 执行。
 * 注意：这不会等待 GPU 完成，只是提交命令。
 * 
 * 执行流程：
 * 1. 调用 glFlush() 提交命令给 GPU
 * 2. 某些驱动有 bug，需要禁用 glFlush（通过配置）
 * 
 * 与 finish() 的区别：
 * - flush(): 提交命令，不等待完成（非阻塞）
 * - finish(): 提交命令并等待完成（阻塞）
 * 
 * 使用场景：
 * - 在帧结束时调用，确保命令被提交
 * - 在需要同步时调用（但通常使用 Fence 更好）
 */
void OpenGLDriver::flush(int) {
    DEBUG_MARKER()
    auto const& gl = mContext;
    // 某些驱动有 bug，需要禁用 glFlush
    if (!gl.bugs.disable_glFlush) {
        glFlush();  // 提交命令给 GPU，但不等待完成
    }
}

/**
 * 完成所有 GPU 命令
 * 
 * 等待 GPU 完成所有已提交的命令（阻塞调用）。
 * 与 flush() 不同，此方法会阻塞直到所有命令完成。
 * 
 * @param 未使用的参数（保持接口一致性）
 * 
 * 执行流程：
 * 1. 调用 glFinish() 等待 GPU 完成所有命令
 * 2. 执行所有 GPU 命令完成回调（如果支持）
 * 3. 验证 GPU 命令完成回调已清空（因为调用了 glFinish）
 * 4. 执行所有"偶尔执行"的回调
 * 
 * 注意：
 * - 此方法会阻塞直到 GPU 完成所有工作
 * - 某些任务依赖独立线程发布结果（如 endTimerQuery），
 *   所以结果可能尚未准备好，任务会再停留一段时间
 * - 这只适用于 mEveryNowAndThenOps 任务
 * - 因此不能断言 mEveryNowAndThenOps 为空
 * 
 * 与 flush() 的区别：
 * - flush(): 提交命令，不等待完成（非阻塞）
 * - finish(): 提交命令并等待完成（阻塞）
 * 
 * 使用场景：
 * - 在需要确保所有 GPU 工作完成时调用
 * - 通常使用 Fence 更好（非阻塞）
 */
void OpenGLDriver::finish(int) {
    DEBUG_MARKER()
    // 等待 GPU 完成所有命令（阻塞）
    glFinish();
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 执行所有 GPU 命令完成回调
    executeGpuCommandsCompleteOps();
    // 因为调用了 glFinish，所有 GPU 命令应该已完成
    assert_invariant(mGpuCommandCompleteOps.empty());
#endif
    // 执行所有"偶尔执行"的回调
    executeEveryNowAndThenOps();
    // 注意：由于我们执行了 glFinish()，所有待处理的任务应该已完成
    
    // 但是，某些任务依赖独立线程发布结果（如 endTimerQuery），
    // 所以结果可能尚未准备好，任务会再停留一段时间
    // 这只适用于 mEveryNowAndThenOps 任务
    // 因此不能断言 mEveryNowAndThenOps 为空
}

/**
 * 使用光栅化管线清除缓冲区
 * 
 * 使用 OpenGL 的清除命令清除指定的缓冲区。
 * 支持多渲染目标（MRT）和 ES 2.0 兼容模式。
 * 
 * @param clearFlags 要清除的缓冲区标志（COLOR、DEPTH、STENCIL）
 * @param linearColor 清除颜色（线性空间，RGBA）
 * @param depth 清除深度值
 * @param stencil 清除模板值
 * 
 * 执行流程：
 * 1. 根据清除标志启用相应的写入掩码：
 *    - COLOR: 启用颜色写入掩码
 *    - DEPTH: 启用深度写入掩码
 *    - STENCIL: 启用模板写入掩码（前后面）
 * 2. ES 3.0+/GL 4.1+ 路径（支持 MRT）：
 *    - 使用 glClearBufferfv/iv/fi 清除每个颜色缓冲区（COLOR0-7）
 *    - 使用 glClearBufferfi 同时清除深度和模板
 *    - 或分别清除深度和模板
 * 3. ES 2.0 路径（单渲染目标）：
 *    - 设置清除颜色（glClearColor）
 *    - 设置清除深度（glClearDepthf）
 *    - 设置清除模板（glClearStencil）
 *    - 使用 glClear 清除（组合掩码）
 * 
 * 性能优化：
 * - ES 3.0+ 使用 glClearBuffer* 可以精确控制每个缓冲区
 * - ES 2.0 使用 glClear 组合清除（一次调用）
 * 
 * 注意：
 * - 清除前必须确保写入掩码已启用
 * - ES 2.0 只支持单渲染目标（COLOR0）
 * - ES 3.0+ 支持多渲染目标（COLOR0-7）
 * - 深度和模板可以同时清除（更高效）
 */
UTILS_NOINLINE
void OpenGLDriver::clearWithRasterPipe(TargetBufferFlags const clearFlags,
        float4 const& linearColor, GLfloat const depth, GLint const stencil) noexcept {

    // 根据清除标志启用相应的写入掩码
    if (any(clearFlags & TargetBufferFlags::COLOR_ALL)) {
        mContext.colorMask(GL_TRUE);
    }
    if (any(clearFlags & TargetBufferFlags::DEPTH)) {
        mContext.depthMask(GL_TRUE);
    }
    if (any(clearFlags & TargetBufferFlags::STENCIL)) {
        mContext.stencilMaskSeparate(0xFF, mContext.state.stencil.back.stencilMask);
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // ES 3.0+/GL 4.1+ 路径：使用 glClearBuffer* 精确控制每个缓冲区（支持 MRT）
    if (UTILS_LIKELY(!mContext.isES2())) {
        // 清除每个颜色缓冲区（COLOR0-7，支持多渲染目标）
        if (any(clearFlags & TargetBufferFlags::COLOR0)) {
            glClearBufferfv(GL_COLOR, 0, linearColor.v);  // 清除颜色缓冲区 0
        }
        if (any(clearFlags & TargetBufferFlags::COLOR1)) {
            glClearBufferfv(GL_COLOR, 1, linearColor.v);  // 清除颜色缓冲区 1
        }
        if (any(clearFlags & TargetBufferFlags::COLOR2)) {
            glClearBufferfv(GL_COLOR, 2, linearColor.v);  // 清除颜色缓冲区 2
        }
        if (any(clearFlags & TargetBufferFlags::COLOR3)) {
            glClearBufferfv(GL_COLOR, 3, linearColor.v);  // 清除颜色缓冲区 3
        }
        if (any(clearFlags & TargetBufferFlags::COLOR4)) {
            glClearBufferfv(GL_COLOR, 4, linearColor.v);  // 清除颜色缓冲区 4
        }
        if (any(clearFlags & TargetBufferFlags::COLOR5)) {
            glClearBufferfv(GL_COLOR, 5, linearColor.v);  // 清除颜色缓冲区 5
        }
        if (any(clearFlags & TargetBufferFlags::COLOR6)) {
            glClearBufferfv(GL_COLOR, 6, linearColor.v);  // 清除颜色缓冲区 6
        }
        if (any(clearFlags & TargetBufferFlags::COLOR7)) {
            glClearBufferfv(GL_COLOR, 7, linearColor.v);  // 清除颜色缓冲区 7
        }
        // 清除深度和模板缓冲区
        if ((clearFlags & TargetBufferFlags::DEPTH_AND_STENCIL) == TargetBufferFlags::DEPTH_AND_STENCIL) {
            // 同时清除深度和模板（更高效，一次调用）
            glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
        } else {
            // 分别清除深度和模板
            if (any(clearFlags & TargetBufferFlags::DEPTH)) {
                glClearBufferfv(GL_DEPTH, 0, &depth);  // 清除深度缓冲区
            }
            if (any(clearFlags & TargetBufferFlags::STENCIL)) {
                glClearBufferiv(GL_STENCIL, 0, &stencil);  // 清除模板缓冲区
            }
        }
    } else
#endif
    {
        // ES 2.0 路径：使用 glClear 组合清除（单渲染目标）
        // 构建清除掩码并设置清除值
        GLbitfield mask = 0;
        if (any(clearFlags & TargetBufferFlags::COLOR0)) {
            // 设置清除颜色（RGBA）
            glClearColor(linearColor.r, linearColor.g, linearColor.b, linearColor.a);
            mask |= GL_COLOR_BUFFER_BIT;  // 添加颜色缓冲区到掩码
        }
        if (any(clearFlags & TargetBufferFlags::DEPTH)) {
            // 设置清除深度值
            glClearDepthf(depth);
            mask |= GL_DEPTH_BUFFER_BIT;  // 添加深度缓冲区到掩码
        }
        if (any(clearFlags & TargetBufferFlags::STENCIL)) {
            // 设置清除模板值
            glClearStencil(stencil);
            mask |= GL_STENCIL_BUFFER_BIT;  // 添加模板缓冲区到掩码
        }
        // 如果掩码非空，执行组合清除（一次调用清除所有缓冲区）
        if (mask) {
            glClear(mask);
        }
    }

    CHECK_GL_ERROR()
}

/**
 * 解析多重采样纹理
 * 
 * 将多重采样纹理解析为单采样纹理（MSAA resolve）。
 * 这是 blit 的便捷方法，用于从多采样纹理解析到单采样纹理。
 * 
 * @param dst 目标纹理句柄（单采样）
 * @param srcLevel 源 Mipmap 级别
 * @param srcLayer 源层索引（用于数组纹理）
 * @param src 源纹理句柄（多采样）
 * @param dstLevel 目标 Mipmap 级别
 * @param dstLayer 目标层索引（用于数组纹理）
 * 
 * 执行流程：
 * 1. 验证源和目标纹理有效
 * 2. 验证尺寸匹配（宽度和高度必须相同）
 * 3. 验证源是多采样，目标是单采样
 * 4. 调用 blit 执行解析（从源到目标，全尺寸）
 * 
 * 注意：
 * - 源纹理必须是多采样（samples > 1）
 * - 目标纹理必须是单采样（samples == 1）
 * - 源和目标尺寸必须匹配
 * - 这是 blit 的便捷方法，专门用于 MSAA resolve
 */
void OpenGLDriver::resolve(
        Handle<HwTexture> dst, uint8_t const srcLevel, uint8_t const srcLayer,
        Handle<HwTexture> src, uint8_t const dstLevel, uint8_t const dstLayer) {
    DEBUG_MARKER()
    GLTexture const* const s = handle_cast<GLTexture*>(src);
    GLTexture const* const d = handle_cast<GLTexture*>(dst);
    assert_invariant(s);
    assert_invariant(d);

    // 验证尺寸匹配
    FILAMENT_CHECK_PRECONDITION(d->width == s->width && d->height == s->height)
            << "invalid resolve: src and dst sizes don't match";

    // 验证源是多采样，目标是单采样
    FILAMENT_CHECK_PRECONDITION(s->samples > 1 && d->samples == 1)
            << "invalid resolve: src.samples=" << +s->samples << ", dst.samples=" << +d->samples;

    // 调用 blit 执行解析（从源到目标，全尺寸）
    blit(   dst, dstLevel, dstLayer, {},
            src, srcLevel, srcLayer, {},
            { d->width, d->height });
}

/**
 * 纹理块传输
 * 
 * 在两个纹理之间传输像素数据（blit）。
 * 支持不同 Mipmap 级别、不同层、不同区域的传输。
 * 可用于 MSAA resolve、纹理复制、格式转换等。
 * 
 * @param dst 目标纹理句柄
 * @param srcLevel 源 Mipmap 级别
 * @param srcLayer 源层索引（用于数组纹理）
 * @param dstOrigin 目标区域原点（像素坐标）
 * @param src 源纹理句柄
 * @param dstLevel 目标 Mipmap 级别
 * @param dstLayer 目标层索引（用于数组纹理）
 * @param srcOrigin 源区域原点（像素坐标）
 * @param size 传输区域大小（像素）
 * 
 * 执行流程：
 * 1. 验证不是 ES 2.0（ES 2.0 不支持 blit）
 * 2. 验证纹理有效
 * 3. 验证纹理使用标志（BLIT_SRC/BLIT_DST）
 * 4. 验证格式匹配（源和目标格式必须相同）
 * 5. 确定附件类型（COLOR/DEPTH/STENCIL/DEPTH_STENCIL）
 * 6. 创建临时 FBO（读取和绘制各一个）
 * 7. 将源纹理附加到读取 FBO
 * 8. 将目标纹理附加到绘制 FBO
 * 9. 禁用裁剪测试
 * 10. 调用 glBlitFramebuffer 执行传输
 * 11. 清理临时 FBO
 * 
 * 性能优化：
 * - 使用临时 FBO 避免修改现有绑定
 * - 禁用裁剪测试以提高性能
 * - 使用 GL_NEAREST 过滤（最快）
 * 
 * 注意：
 * - ES 2.0 不支持此方法
 * - 源和目标格式必须匹配
 * - 纹理必须具有相应的使用标志
 * - 支持 2D、3D、Array、Cube 纹理
 * - 支持颜色、深度、模板缓冲区
 */
void OpenGLDriver::blit(
        Handle<HwTexture> dst, uint8_t const srcLevel, uint8_t const srcLayer, uint2 const dstOrigin,
        Handle<HwTexture> src, uint8_t const dstLevel, uint8_t const dstLayer, uint2 const srcOrigin,
        uint2 const size) {
    DEBUG_MARKER()
    UTILS_UNUSED_IN_RELEASE auto& gl = mContext;
    assert_invariant(!gl.isES2());

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2

    GLTexture const* d = handle_cast<GLTexture*>(dst);
    GLTexture const* s = handle_cast<GLTexture*>(src);
    assert_invariant(d);
    assert_invariant(s);

    // 验证目标纹理具有 BLIT_DST 使用标志
    ASSERT_PRECONDITION_NON_FATAL(any(d->usage & TextureUsage::BLIT_DST),
            "texture doesn't have BLIT_DST");

    // 验证源纹理具有 BLIT_SRC 使用标志
    ASSERT_PRECONDITION_NON_FATAL(any(s->usage & TextureUsage::BLIT_SRC),
            "texture doesn't have BLIT_SRC");

    // 验证格式匹配
    ASSERT_PRECONDITION_NON_FATAL(s->format == d->format,
            "src and dst texture format don't match");

    // 附件类型枚举：定义 FBO 附件的类型（颜色、深度、模板等）
    enum class AttachmentType : GLenum {
        COLOR = GL_COLOR_ATTACHMENT0,           // 颜色附件
        DEPTH = GL_DEPTH_ATTACHMENT,            // 深度附件
        STENCIL = GL_STENCIL_ATTACHMENT,        // 模板附件
        DEPTH_STENCIL = GL_DEPTH_STENCIL_ATTACHMENT,  // 深度模板组合附件
    };

    // Lambda 函数：根据纹理格式确定附件类型
    // 用于判断纹理是颜色、深度、模板还是深度模板组合格式
    auto getFormatType = [](TextureFormat const format) -> AttachmentType {
        bool const depth = isDepthFormat(format);      // 是否为深度格式
        bool const stencil = isStencilFormat(format);  // 是否为模板格式
        if (depth && stencil) return AttachmentType::DEPTH_STENCIL;  // 深度模板组合
        if (depth) return AttachmentType::DEPTH;       // 仅深度
        if (stencil) return AttachmentType::STENCIL;   // 仅模板
        return AttachmentType::COLOR;                  // 颜色
    };

    // 确定附件类型（源和目标必须相同）
    AttachmentType const type = getFormatType(d->format);
    assert_invariant(type == getFormatType(s->format));

    // 确定 blit 掩码：根据附件类型设置要传输的缓冲区位
    // 注意：如果掩码包含 GL_DEPTH_BUFFER_BIT 或 GL_STENCIL_BUFFER_BIT，
    // 过滤模式必须是 GL_NEAREST（不能使用 GL_LINEAR）
    GLbitfield mask = {};
    switch (type) {
        case AttachmentType::COLOR:
            mask = GL_COLOR_BUFFER_BIT;  // 传输颜色缓冲区
            break;
        case AttachmentType::DEPTH:
            mask = GL_DEPTH_BUFFER_BIT;  // 传输深度缓冲区
            break;
        case AttachmentType::STENCIL:
            mask = GL_STENCIL_BUFFER_BIT;  // 传输模板缓冲区
            break;
        case AttachmentType::DEPTH_STENCIL:
            mask = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;  // 同时传输深度和模板
            break;
    };

    // 创建临时 FBO（读取和绘制各一个）
    GLuint fbo[2] = {};
    glGenFramebuffers(2, fbo);

    // 绑定目标纹理到绘制 FBO
    gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[0]);
    switch (d->target) {
        case SamplerType::SAMPLER_2D:
            // 2D 纹理：根据使用标志选择纹理或渲染缓冲区
            if (any(d->usage & TextureUsage::SAMPLEABLE)) {
                // 可采样纹理：附加纹理到 FBO
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GLenum(type),
                        GL_TEXTURE_2D, d->gl.id, dstLevel);
            } else {
                // 不可采样：附加渲染缓冲区到 FBO
                glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GLenum(type),
                        GL_RENDERBUFFER, d->gl.id);
            }
            break;
        case SamplerType::SAMPLER_CUBEMAP:
            // Cube 纹理：附加指定的面到 FBO
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GLenum(type),
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + dstLayer, d->gl.id, dstLevel);
            break;
        case SamplerType::SAMPLER_2D_ARRAY:
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
        case SamplerType::SAMPLER_3D:
            // 数组纹理或 3D 纹理：使用纹理层附加
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GLenum(type),
                    d->gl.id, dstLevel, dstLayer);
            break;
        case SamplerType::SAMPLER_EXTERNAL:
            // 外部纹理：不支持 blit
            break;
    }
    // 验证绘制 FBO 状态
    CHECK_GL_FRAMEBUFFER_STATUS(GL_DRAW_FRAMEBUFFER)

    // 绑定源纹理到读取 FBO
    gl.bindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
    switch (s->target) {
        case SamplerType::SAMPLER_2D:
            // 2D 纹理：根据使用标志选择纹理或渲染缓冲区
            if (any(s->usage & TextureUsage::SAMPLEABLE)) {
                // 可采样纹理：附加纹理到 FBO
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GLenum(type),
                        GL_TEXTURE_2D, s->gl.id, srcLevel);
            } else {
                // 不可采样：附加渲染缓冲区到 FBO
                glFramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GLenum(type),
                        GL_RENDERBUFFER, s->gl.id);
            }
            break;
        case SamplerType::SAMPLER_CUBEMAP:
            // Cube 纹理：附加指定的面到 FBO
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GLenum(type),
                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + srcLayer, s->gl.id, srcLevel);
            break;
        case SamplerType::SAMPLER_2D_ARRAY:
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
        case SamplerType::SAMPLER_3D:
            // 数组纹理或 3D 纹理：使用纹理层附加
            glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GLenum(type),
                    s->gl.id, srcLevel, srcLayer);
            break;
        case SamplerType::SAMPLER_EXTERNAL:
            // 外部纹理：不支持 blit
            break;
    }
    // 验证读取 FBO 状态
    CHECK_GL_FRAMEBUFFER_STATUS(GL_READ_FRAMEBUFFER)

    // 禁用裁剪测试（blit 不受裁剪影响）
    gl.disable(GL_SCISSOR_TEST);
    // 执行 blit 操作：从源区域传输到目标区域
    // 使用 GL_NEAREST 过滤（深度/模板必须使用 NEAREST）
    glBlitFramebuffer(
            GLint(srcOrigin.x), GLint(srcOrigin.y), GLint(srcOrigin.x + size.x), GLint(srcOrigin.y + size.y),  // 源区域
            GLint(dstOrigin.x), GLint(dstOrigin.y), GLint(dstOrigin.x + size.x), GLint(dstOrigin.y + size.y),  // 目标区域
            mask, GL_NEAREST);  // 传输掩码和过滤模式
    CHECK_GL_ERROR()

    // 清理：解绑并删除临时 FBO
    gl.unbindFramebuffer(GL_DRAW_FRAMEBUFFER);
    gl.unbindFramebuffer(GL_READ_FRAMEBUFFER);
    glDeleteFramebuffers(2, fbo);
#endif
}

/**
 * 渲染目标块传输（已弃用）
 * 
 * 在两个渲染目标之间传输像素数据（blit）。
 * 此方法已弃用，仅由 Renderer::copyFrame 使用。
 * 
 * @param buffers 要传输的缓冲区标志（仅支持 COLOR0）
 * @param dst 目标渲染目标句柄
 * @param dstRect 目标区域（视口坐标）
 * @param src 源渲染目标句柄
 * @param srcRect 源区域（视口坐标）
 * @param filter 过滤模式（NEAREST/LINEAR）
 * 
 * 执行流程：
 * 1. 验证不是 ES 2.0（ES 2.0 不支持 blit）
 * 2. 验证只支持 COLOR0（不支持多渲染目标）
 * 3. 验证矩形坐标为正数
 * 4. 对于 MSAA 渲染目标，从 MSAA 侧缓冲区复制
 *    （这应该产生与从解析纹理复制相同的输出）
 * 5. 验证目标不是多采样（GLES 3.x 要求）
 * 6. 如果源是多采样，验证源和目标矩形相同
 * 7. 绑定源和目标 FBO
 * 8. 禁用裁剪测试
 * 9. 调用 glBlitFramebuffer 执行传输
 * 
 * 注意：
 * - 此方法已弃用，仅由 Renderer::copyFrame 使用
 * - ES 2.0 不支持此方法
 * - 只支持 COLOR0（不支持多渲染目标）
 * - 对于 MSAA 渲染目标，从 MSAA 侧缓冲区复制
 * - 源是多采样时，源和目标矩形必须相同
 * - 目标不能是多采样
 */
void OpenGLDriver::blitDEPRECATED(TargetBufferFlags const buffers,
        Handle<HwRenderTarget> dst, Viewport const dstRect,
        Handle<HwRenderTarget> src, Viewport const srcRect,
        SamplerMagFilter const filter) {

    // 注意：blitDEPRECATED 仅由 Renderer::copyFrame 使用

    DEBUG_MARKER()
    UTILS_UNUSED_IN_RELEASE auto& gl = mContext;
    assert_invariant(!gl.isES2());

    // 验证只支持 COLOR0
    FILAMENT_CHECK_PRECONDITION(buffers == TargetBufferFlags::COLOR0)
            << "blitDEPRECATED only supports COLOR0";

    // 验证矩形坐标为正数
    FILAMENT_CHECK_PRECONDITION(
            srcRect.left >= 0 && srcRect.bottom >= 0 && dstRect.left >= 0 && dstRect.bottom >= 0)
            << "Source and destination rects must be positive.";

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2

    // 转换过滤模式：从 Filament 枚举转换为 OpenGL 常量
    GLenum const glFilterMode = (filter == SamplerMagFilter::NEAREST) ? GL_NEAREST : GL_LINEAR;

    // 注意：对于具有非 MSAA 附件的 MSAA 渲染目标，我们从 MSAA 侧缓冲区复制
    // 这应该产生与从解析纹理复制相同的输出
    // EXT_multisampled_render_to_texture 似乎允许这两种行为，这是对它的模拟
    // 我们不能轻易使用解析纹理，因为它实际上没有附加到 RenderTarget
    // 另一种实现是进行反向解析，但这不会给我们带来任何好处
    GLRenderTarget const* s = handle_cast<GLRenderTarget const*>(src);
    GLRenderTarget const* d = handle_cast<GLRenderTarget const*>(dst);

    // GLES 3.x 限制：如果绘制缓冲区的 GL_SAMPLE_BUFFERS 值大于零，会生成 GL_INVALID_OPERATION
    // 这在 OpenGL 上可以工作，所以我们要确保捕获这种情况
    // 目标不能是多采样（必须是单采样）
    assert_invariant(d->gl.samples <= 1);

    // GLES 3.x 限制：如果读取缓冲区的 GL_SAMPLE_BUFFERS 大于零，
    // 且绘制和读取缓冲区的格式不完全相同，会生成 GL_INVALID_OPERATION
    // 但是规范中没有明确定义"格式"的含义，所以很难在这里断言
    // 特别是在处理默认 framebuffer 时

    // GLES 3.x 限制：如果读取缓冲区的 GL_SAMPLE_BUFFERS 大于零，
    // 且源和目标矩形没有定义相同的 (X0, Y0) 和 (X1, Y1) 边界，会生成 GL_INVALID_OPERATION

    // 此外，EXT_multisampled_render_to_texture 扩展没有指定
    // 从"隐式"解析渲染目标进行 blit 时会发生什么（它有效吗？）
    // 所以为了安全起见，我们不允许它
    // 如果源是多采样，源和目标矩形必须完全相同
    if (s->gl.samples > 1) {
        assert_invariant(!memcmp(&dstRect, &srcRect, sizeof(srcRect)));
    }

    // 绑定源和目标 FBO（使用渲染目标已有的 FBO，不需要创建临时 FBO）
    gl.bindFramebuffer(GL_READ_FRAMEBUFFER, s->gl.fbo);
    gl.bindFramebuffer(GL_DRAW_FRAMEBUFFER, d->gl.fbo);

    // 验证 FBO 状态（确保绑定成功）
    CHECK_GL_FRAMEBUFFER_STATUS(GL_READ_FRAMEBUFFER)
    CHECK_GL_FRAMEBUFFER_STATUS(GL_DRAW_FRAMEBUFFER)

    // 禁用裁剪测试（blit 不受裁剪影响）
    gl.disable(GL_SCISSOR_TEST);
    // 执行 blit 操作：从源区域传输颜色缓冲区到目标区域
    // 注意：OpenGL 使用左下角原点，所以 bottom 和 top 的顺序
    glBlitFramebuffer(
            srcRect.left, srcRect.bottom, srcRect.right(), srcRect.top(),  // 源区域（左下角到右上角）
            dstRect.left, dstRect.bottom, dstRect.right(), dstRect.top(),  // 目标区域（左下角到右上角）
            GL_COLOR_BUFFER_BIT, glFilterMode);  // 只传输颜色缓冲区，使用指定的过滤模式
    CHECK_GL_ERROR()
#endif
}

/**
 * 绑定渲染管线
 * 
 * 绑定完整的渲染管线状态，包括：
 * - 光栅化状态（culling、blending、depth test 等）
 * - 模板状态（stencil test、functions、operations）
 * - 多边形偏移（用于阴影贴图等）
 * - 着色器程序
 * - 推送常量
 * - 管线布局（描述符集布局）
 * 
 * @param state 管线状态（包含所有渲染状态）
 * 
 * 执行流程：
 * 1. 设置光栅化状态（culling、blending、depth test 等）
 * 2. 设置模板状态（stencil test、functions、operations）
 * 3. 设置多边形偏移（slope 和 constant）
 * 4. 绑定着色器程序（如果程序无效，标记为无效）
 * 5. 更新推送常量（从程序获取）
 * 6. 保存管线布局（描述符集布局）
 * 
 * 性能优化：
 * - 状态缓存：OpenGLContext 会缓存状态，避免重复设置
 * - 延迟绑定：描述符集在绘制时才绑定
 * 
 * 注意：
 * - 如果程序编译/链接失败，mValidProgram 会被设置为 false
 * - 推送常量从程序获取（在编译时确定）
 * - TODO: 应该验证管线布局与程序的布局匹配
 */
void OpenGLDriver::bindPipeline(PipelineState const& state) {
    DEBUG_MARKER()
    auto& gl = mContext;
    // 设置光栅化状态（culling、blending、depth test 等）
    setRasterState(state.rasterState);
    // 设置模板状态（stencil test、functions、operations）
    setStencilState(state.stencilState);
    // 设置多边形偏移（用于阴影贴图等）
    gl.polygonOffset(state.polygonOffset.slope, state.polygonOffset.constant);
    // 绑定着色器程序（如果程序无效，标记为无效）
    OpenGLProgram* const p = handle_cast<OpenGLProgram*>(state.program);
    mValidProgram = useProgram(p);
    // 更新推送常量（从程序获取）
    (*mCurrentPushConstants) = p->getPushConstants();
    // 保存管线布局（描述符集布局）
    mCurrentSetLayout = state.pipelineLayout.setLayout;
    // TODO: 应该验证管线布局与程序的布局匹配
}

/**
 * 绑定渲染图元
 * 
 * 绑定渲染图元（Render Primitive），包括：
 * - 顶点数组对象（VAO）
 * - 顶点缓冲区（VBO）
 * - 索引缓冲区（IBO）
 * 
 * @param rph 渲染图元句柄
 * 
 * 执行流程：
 * 1. 获取渲染图元对象
 * 2. 检查顶点缓冲区是否有效（如果未设置，优雅地返回）
 * 3. 绑定 VAO（如果 VAO 已过期，会重新创建）
 * 4. 更新 VAO 中的顶点缓冲区绑定（如果缓冲区改变）
 * 5. 保存当前绑定的渲染图元
 * 
 * 性能优化：
 * - VAO 缓存：VAO 在创建时记录状态版本，如果状态未改变，直接使用
 * - 延迟更新：只在缓冲区改变时更新 VAO 绑定
 * 
 * 注意：
 * - VAO 包含顶点属性配置和索引缓冲区绑定
 * - 顶点缓冲区绑定在 draw 时更新（因为可能动态改变）
 */
void OpenGLDriver::bindRenderPrimitive(Handle<HwRenderPrimitive> rph) {
    DEBUG_MARKER()
    auto& gl = mContext;

    GLRenderPrimitive* const rp = handle_cast<GLRenderPrimitive*>(rph);

    // 如果渲染图元未设置，优雅地返回（不做任何操作）
    VertexBufferHandle vb = rp->gl.vertexBufferWithObjects;
    if (UTILS_UNLIKELY(!vb)) {
        mBoundRenderPrimitive = nullptr;
        return;
    }

    // 如果需要，更新 VAO 中的绑定
    // bindVertexArray 会检查 VAO 是否过期，如果过期会重新创建
    gl.bindVertexArray(&rp->gl);
    GLVertexBuffer const* const glvb = handle_cast<GLVertexBuffer*>(vb);
    // 更新 VAO 中的顶点缓冲区绑定（如果缓冲区改变）
    updateVertexArrayObject(rp, glvb);

    // 保存当前绑定的渲染图元
    mBoundRenderPrimitive = rp;
}

/**
 * 绑定描述符集
 * 
 * 将描述符集绑定到指定的槽位，用于后续绘制调用。
 * 描述符集包含纹理、采样器、缓冲区等资源绑定。
 * 
 * @param dsh 描述符集句柄（nullptr 表示解绑）
 * @param set 描述符集槽位（0, 1, 2, ...）
 * @param offsets 动态偏移数组（用于动态 uniform 缓冲区）
 * 
 * 执行流程：
 * 1. 如果描述符集为空（nullptr）：
 *    - 标记绑定无效（需要重新绑定）
 *    - 标记偏移无效（需要重新设置）
 *    - 返回
 * 2. 如果描述符集与之前绑定的不同：
 *    - 标记绑定无效（在下次绘制时重新绑定）
 * 3. 如果偏移发生变化：
 *    - 标记偏移无效（在下次绘制时重新设置）
 * 4. 保存描述符集句柄和偏移数组（复制数据，因为原数据生命周期在 CommandStream 中）
 * 
 * 性能优化：
 * - 延迟绑定：标记无效，在绘制时才实际绑定（避免不必要的状态切换）
 * - 偏移复制：复制偏移数据以确保生命周期正确
 * 
 * 注意：
 * - 绑定不会立即生效，在绘制时才会应用
 * - 偏移数组用于动态 uniform 缓冲区（允许在同一绑定点使用缓冲区的不同部分）
 */
void OpenGLDriver::bindDescriptorSet(
        DescriptorSetHandle dsh,
        descriptor_set_t const set,
        DescriptorSetOffsetArray&& offsets) {

    if (UTILS_UNLIKELY(!dsh)) {
        // 描述符集为空，标记绑定和偏移无效
        mBoundDescriptorSets[set].dsh = dsh;
        mInvalidDescriptorSetBindings.set(set, true);
        mInvalidDescriptorSetBindingOffsets.set(set, true);
        return;
    }

    // handle_cast<> 也用于验证句柄（实际上不能返回 nullptr）
    if (GLDescriptorSet const* const ds = handle_cast<GLDescriptorSet*>(dsh)) {
        assert_invariant(set < MAX_DESCRIPTOR_SET_COUNT);
        if (mBoundDescriptorSets[set].dsh != dsh) {
            // 如果描述符集本身改变，标记绑定无效
            // 将在下次绘制时重新绑定
            mInvalidDescriptorSetBindings.set(set, true);
        } else if (!offsets.empty()) {
            // 如果重置偏移，标记偏移无效，这些描述符只能在下次绘制时重新绑定
            mInvalidDescriptorSetBindingOffsets.set(set, true);
        }

        // `offsets` 数据的生命周期在此函数返回时结束，我们必须复制
        // （数据在 CommandStream 内分配）
        mBoundDescriptorSets[set].dsh = dsh;
        assert_invariant(offsets.data() != nullptr || ds->getDynamicBufferCount() == 0);
        std::copy_n(offsets.data(), ds->getDynamicBufferCount(),
                mBoundDescriptorSets[set].offsets.data());
    }
}

/**
 * 更新描述符
 * 
 * 将无效的描述符集绑定到 OpenGL 上下文。
 * 此方法在绘制前调用，确保所有描述符集绑定是最新的。
 * 
 * @param invalidDescriptorSets 无效描述符集的位掩码（每位表示一个槽位）
 * 
 * 执行流程：
 * 1. 计算只有偏移无效的描述符集（绑定未改变，只有偏移改变）
 * 2. 遍历所有无效的描述符集：
 *    - 如果描述符集句柄有效：
 *      * 调试模式下验证描述符集布局与管线布局匹配（只在绑定改变时检查）
 *      * 调用描述符集的 bind 方法绑定到 OpenGL 上下文
 * 3. 清除所有无效标记
 * 
 * 性能优化：
 * - 只更新无效的描述符集（避免不必要的状态切换）
 * - 只有偏移改变时跳过布局验证（减少开销）
 * 
 * 注意：
 * - 此方法在每次绘制前调用
 * - 必须在绑定程序后调用（描述符绑定依赖于程序）
 */
void OpenGLDriver::updateDescriptors(bitset8 const invalidDescriptorSets) noexcept {
    assert_invariant(mBoundProgram);
    // 计算只有偏移无效的描述符集（绑定未改变，只有偏移改变）
    auto const offsetOnly = mInvalidDescriptorSetBindingOffsets & ~mInvalidDescriptorSetBindings;
    // 遍历所有无效的描述符集
    invalidDescriptorSets.forEachSetBit([this, offsetOnly,
            &boundDescriptorSets = mBoundDescriptorSets,
            &context = mContext,
            &boundProgram = *mBoundProgram](size_t const set) {
        assert_invariant(set < MAX_DESCRIPTOR_SET_COUNT);
        auto const& entry = boundDescriptorSets[set];
        if (entry.dsh) {
            GLDescriptorSet const* const ds = handle_cast<GLDescriptorSet*>(entry.dsh);
#ifndef NDEBUG
            // 调试模式下验证描述符集布局与管线布局匹配
            if (UTILS_UNLIKELY(!offsetOnly[set])) {
                // 验证此描述符集布局与管线中设置的布局匹配
                // 如果只有偏移改变，则不需要检查
                ds->validate(mHandleAllocator, mCurrentSetLayout[set]);
            }
#endif
            // 绑定描述符集到 OpenGL 上下文
            ds->bind(context, mHandleAllocator, boundProgram,
                    set, entry.offsets.data(), offsetOnly[set]);
        }
    });
    // 清除所有无效标记
    mInvalidDescriptorSetBindings.clear();
    mInvalidDescriptorSetBindingOffsets.clear();
}

/**
 * 绘制（实例化）
 * 
 * 执行 GPU 绘制调用，绘制索引几何体（支持实例化）。
 * 
 * @param indexOffset 索引缓冲区偏移（索引数量，不是字节）
 * @param indexCount 索引数量
 * @param instanceCount 实例数量（1 表示非实例化绘制）
 * 
 * 执行流程：
 * 1. 验证前提条件：
 *    - 不是 ES 2.0（ES 2.0 不支持实例化绘制）
 *    - 渲染图元已绑定
 *    - 程序已绑定且有效
 * 2. 如果描述符集无效（程序改变或绑定改变）：
 *    - 更新描述符（重新绑定纹理、采样器、缓冲区等）
 * 3. 调用 glDrawElementsInstanced 执行绘制
 * 
 * 性能优化：
 * - 延迟描述符绑定：只在需要时更新描述符（避免不必要的状态切换）
 * - 实例化绘制：一次调用绘制多个实例（减少 CPU 开销）
 * 
 * 注意：
 * - ES 2.0 不支持此方法（不支持实例化绘制）
 * - 索引偏移是索引数量，不是字节偏移
 * - 实例数量 >= 1（1 表示非实例化绘制）
 * - 材质调试模式下程序无效时不绘制
 */
void OpenGLDriver::draw2(uint32_t const indexOffset, uint32_t const indexCount, uint32_t const instanceCount) {
    DEBUG_MARKER()
    assert_invariant(!mContext.isES2());
    assert_invariant(mBoundRenderPrimitive);
#if FILAMENT_ENABLE_MATDBG
    // 材质调试模式：程序无效时不绘制
    if (UTILS_UNLIKELY(!mValidProgram)) {
        return;
    }
#endif
    assert_invariant(mBoundProgram);
    assert_invariant(mValidProgram);

    // 当程序改变时，我们可能需要重新绑定所有或部分描述符
    auto const invalidDescriptorSets =
            mInvalidDescriptorSetBindings | mInvalidDescriptorSetBindingOffsets;
    if (UTILS_UNLIKELY(invalidDescriptorSets.any())) {
        updateDescriptors(invalidDescriptorSets);
    }

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    // 执行 GPU 绘制调用
    GLRenderPrimitive const* const rp = mBoundRenderPrimitive;
    glDrawElementsInstanced(GLenum(rp->type), GLsizei(indexCount),
            rp->gl.getIndicesType(),
            reinterpret_cast<const void*>(indexOffset << rp->gl.indicesShift),
            GLsizei(instanceCount));
#endif

#if FILAMENT_ENABLE_MATDBG
    CHECK_GL_ERROR_NON_FATAL()
#else
    CHECK_GL_ERROR()
#endif
}

/**
 * 绘制（ES 2.0 版本）
 * 
 * 这是 draw2() 的 ES 2.0 版本，不支持实例化绘制。
 * 执行 GPU 绘制调用，绘制索引几何体（非实例化）。
 * 
 * @param indexOffset 索引缓冲区偏移（索引数量，不是字节）
 * @param indexCount 索引数量
 * @param instanceCount 实例数量（ES 2.0 必须为 1）
 * 
 * 执行流程：
 * 1. 验证前提条件：
 *    - 是 ES 2.0
 *    - 渲染图元已绑定
 *    - 程序已绑定且有效
 *    - 实例数量为 1（ES 2.0 不支持实例化）
 * 2. 如果描述符集无效：
 *    - 更新描述符（重新绑定纹理、采样器、缓冲区等）
 * 3. 调用 glDrawElements 执行绘制（非实例化版本）
 * 
 * 注意：
 * - 这是 ES 2.0 专用版本
 * - ES 2.0 不支持实例化绘制，instanceCount 必须为 1
 * - 使用 glDrawElements 而不是 glDrawElementsInstanced
 */
void OpenGLDriver::draw2GLES2(uint32_t const indexOffset, uint32_t const indexCount, uint32_t const instanceCount) {
    DEBUG_MARKER()
    assert_invariant(mContext.isES2());
    assert_invariant(mBoundRenderPrimitive);
#if FILAMENT_ENABLE_MATDBG
    // 材质调试模式：程序无效时不绘制
    if (UTILS_UNLIKELY(!mValidProgram)) {
        return;
    }
#endif
    assert_invariant(mBoundProgram);
    assert_invariant(mValidProgram);

    // 当程序改变时，我们可能需要重新绑定所有或部分描述符
    auto const invalidDescriptorSets =
            mInvalidDescriptorSetBindings | mInvalidDescriptorSetBindingOffsets;
    if (UTILS_UNLIKELY(invalidDescriptorSets.any())) {
        updateDescriptors(invalidDescriptorSets);
    }

    GLRenderPrimitive const* const rp = mBoundRenderPrimitive;
    // ES 2.0 不支持实例化绘制，实例数量必须为 1
    assert_invariant(instanceCount == 1);
    // 调用 glDrawElements（非实例化版本）
    glDrawElements(GLenum(rp->type), GLsizei(indexCount), rp->gl.getIndicesType(),
            reinterpret_cast<const void*>(indexOffset << rp->gl.indicesShift));

#if FILAMENT_ENABLE_MATDBG
    CHECK_GL_ERROR_NON_FATAL()
#else
    CHECK_GL_ERROR()
#endif
}

/**
 * 设置裁剪区域
 * 
 * 设置裁剪测试的矩形区域。
 * 只有在裁剪区域内的像素才会被渲染。
 * 
 * @param scissor 裁剪区域（视口坐标）
 * 
 * 注意：
 * - 裁剪区域在视口坐标系中
 * - 委托给 setScissor 实现
 */
void OpenGLDriver::scissor(Viewport const scissor) {
    DEBUG_MARKER()
    setScissor(scissor);
}

/**
 * 执行绘制调用
 * 
 * 这是渲染的核心方法，执行实际的绘制操作。主要职责：
 * 1. 绑定渲染管线状态（着色器程序、渲染状态等）
 * 2. 绑定渲染图元（顶点/索引缓冲区、VAO）
 * 3. 执行绘制调用（glDrawElementsInstanced 或 glDrawElements）
 * 
 * @param state 管线状态（包含着色器程序、渲染状态等）
 * @param rph 渲染图元句柄（包含顶点/索引缓冲区信息）
 * @param indexOffset 索引缓冲区偏移（以索引为单位）
 * @param indexCount 要绘制的索引数量
 * @param instanceCount 实例数量（用于实例化渲染）
 * 
 * 执行流程：
 * 1. 从渲染图元获取图元类型和顶点缓冲区信息
 * 2. 更新管线状态（设置图元类型和顶点缓冲区信息）
 * 3. 绑定管线（着色器程序、渲染状态等）
 * 4. 绑定渲染图元（VAO、顶点/索引缓冲区）
 * 5. 根据 OpenGL 版本选择绘制方法：
 *    - ES 2.0: 使用 draw2GLES2（不支持实例化）
 *    - ES 3.0+/GL 4.1+: 使用 draw2（支持实例化）
 * 
 * 注意：
 * - OpenGL ES 2.0 不支持实例化渲染，instanceCount 必须为 1
 * - 索引偏移需要根据索引类型（16位/32位）进行转换
 */
void OpenGLDriver::draw(PipelineState state, Handle<HwRenderPrimitive> rph,
        uint32_t const indexOffset, uint32_t const indexCount, uint32_t const instanceCount) {
    DEBUG_MARKER()
    GLRenderPrimitive const* const rp = handle_cast<GLRenderPrimitive*>(rph);
    
    // 从渲染图元获取图元类型和顶点缓冲区信息
    state.primitiveType = rp->type;
    state.vertexBufferInfo = rp->vbih;
    
    // 绑定管线（着色器程序、渲染状态等）
    bindPipeline(state);
    
    // 绑定渲染图元（VAO、顶点/索引缓冲区）
    bindRenderPrimitive(rph);
    
    // 根据 OpenGL 版本选择绘制方法
    if (UTILS_UNLIKELY(mContext.isES2())) {
        // ES 2.0：不支持实例化渲染
        draw2GLES2(indexOffset, indexCount, instanceCount);
    } else {
        // ES 3.0+/GL 4.1+：支持实例化渲染
        draw2(indexOffset, indexCount, instanceCount);
    }
}

/**
 * 调度计算着色器
 * 
 * 执行计算着色器程序，用于 GPGPU 计算任务。
 * 
 * @param program 计算着色器程序句柄
 * @param workGroupCount 工作组数量（x, y, z 维度）
 * 
 * 执行流程：
 * 1. Tick 着色器编译服务（检查异步编译完成情况）
 * 2. 绑定计算着色器程序
 * 3. 如果程序无效（编译/链接失败）：
 *    - 直接返回（避免致命错误或级联错误）
 *    - 着色器编译错误已经输出到控制台
 * 4. 调用 glDispatchCompute 执行计算
 * 
 * 性能优化：
 * - 异步着色器编译：在绘制前 tick 编译服务
 * 
 * 注意：
 * - 需要 ES 3.1 或 GL 4.3+ 支持（计算着色器）
 * - Android 上 GLES 3.1+ 入口点定义在 glext 中（临时，直到淘汰 API < 21）
 * - 工作组数量是三维的（x, y, z）
 * - 计算着色器程序与渲染着色器程序不同
 */
void OpenGLDriver::dispatchCompute(Handle<HwProgram> program, uint3 const workGroupCount) {
    DEBUG_MARKER()
    // Tick 着色器编译服务（检查异步编译完成情况）
    getShaderCompilerService().tick();

    OpenGLProgram* const p = handle_cast<OpenGLProgram*>(program);

    // 绑定计算着色器程序
    bool const success = useProgram(p);
    if (UTILS_UNLIKELY(!success)) {
        // 避免在程序无效时发生致命错误或级联错误
        // 着色器编译错误已经输出到控制台，可以直接返回
        return;
    }

#if defined(BACKEND_OPENGL_LEVEL_GLES31)

#if defined(__ANDROID__)
    // Android 上 GLES 3.1+ 入口点定义在 glext 中
    // （这是临时的，直到我们淘汰 API < 21）
    using glext::glDispatchCompute;
#endif

    // 调用计算着色器
    glDispatchCompute(workGroupCount.x, workGroupCount.y, workGroupCount.z);
#endif // BACKEND_OPENGL_LEVEL_GLES31

#if FILAMENT_ENABLE_MATDBG
    CHECK_GL_ERROR_NON_FATAL()
#else
    CHECK_GL_ERROR()
#endif
}

// 显式实例化 Dispatcher
template class ConcreteDispatcher<OpenGLDriver>;

} // namespace filament::backend

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
