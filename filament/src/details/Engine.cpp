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

#include "details/Engine.h"

#include "MaterialParser.h"
#include "ResourceAllocator.h"
#include "RenderPrimitive.h"

#include "details/BufferObject.h"
#include "details/Camera.h"
#include "details/Fence.h"
#include "details/IndexBuffer.h"
#include "details/IndirectLight.h"
#include "details/InstanceBuffer.h"
#include "details/Material.h"
#include "details/MorphTargetBuffer.h"
#include "details/Renderer.h"
#include "details/Scene.h"
#include "details/SkinningBuffer.h"
#include "details/Skybox.h"
#include "details/Stream.h"
#include "details/SwapChain.h"
#include "details/Sync.h"
#include "details/Texture.h"
#include "details/VertexBuffer.h"
#include "details/View.h"

#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/MaterialEnums.h>

#include <private/filament/DescriptorSets.h>
#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <private/backend/PlatformFactory.h>

#include <backend/DriverEnums.h>

#include <private/utils/Tracing.h>

#include <utils/Allocator.h>
#include <utils/CallStack.h>
#include <utils/CString.h>
#include <utils/Invocable.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/PrivateImplementation-impl.h>
#include <utils/ThreadUtils.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/vec3.h>
#include <math/vec4.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "generated/resources/materials.h"

using namespace filament::math;
using namespace utils;

namespace filament {

using namespace backend;
using namespace filaflat;

namespace {

/**
 * 获取驱动配置
 * 
 * 从 Engine 实例中提取驱动配置参数。
 * 
 * @param instance Engine 实例指针
 * @return 驱动配置结构
 */
backend::Platform::DriverConfig getDriverConfig(FEngine* instance) {
    Platform::DriverConfig driverConfig {
        .handleArenaSize = instance->getRequestedDriverHandleArenaSize(),  // 句柄竞技场大小
        .metalUploadBufferSizeBytes = instance->getConfig().metalUploadBufferSizeBytes,  // Metal 上传缓冲区大小
        .disableParallelShaderCompile = instance->features.backend.disable_parallel_shader_compile,  // 禁用并行着色器编译
        .disableAmortizedShaderCompile =
                instance->features.backend.disable_amortized_shader_compile,  // 禁用摊销着色器编译
        .disableHandleUseAfterFreeCheck =
                instance->features.backend.disable_handle_use_after_free_check,  // 禁用句柄释放后使用检查
        .disableHeapHandleTags = instance->features.backend.disable_heap_handle_tags,  // 禁用堆句柄标签
        .forceGLES2Context = instance->getConfig().forceGLES2Context,  // 强制 GLES2 上下文
        .stereoscopicType = instance->getConfig().stereoscopicType,  // 立体渲染类型
        .assertNativeWindowIsValid =
                instance->features.backend.opengl.assert_native_window_is_valid,  // 断言原生窗口有效
        .metalDisablePanicOnDrawableFailure =
                instance->getConfig().metalDisablePanicOnDrawableFailure,  // Metal 绘制失败时不崩溃
        .gpuContextPriority = instance->getConfig().gpuContextPriority,  // GPU 上下文优先级
        .vulkanEnableStagingBufferBypass =
                instance->features.backend.vulkan.enable_staging_buffer_bypass,  // Vulkan 启用暂存缓冲区绕过
        .asynchronousMode = instance->features.backend.enable_asynchronous_operation ?
                instance->getConfig().asynchronousMode : AsynchronousMode::NONE,  // 异步模式
    };

    return driverConfig;
}

} // anonymous

/**
 * Engine 构建器详情结构
 * 
 * 存储 Engine 的构建参数。
 */
struct Engine::BuilderDetails {
    Backend mBackend = Backend::DEFAULT;  // 后端类型（默认自动选择）
    Platform* mPlatform = nullptr;  // 平台指针（如果为 nullptr，将创建默认平台）
    Config mConfig;  // Engine 配置
    FeatureLevel mFeatureLevel = FeatureLevel::FEATURE_LEVEL_1;  // 特性级别（默认级别 1）
    void* mSharedContext = nullptr;  // 共享上下文（用于多线程渲染）
    bool mPaused = false;  // 是否暂停（用于延迟初始化）
    std::unordered_map<CString, bool> mFeatureFlags;  // 特性标志映射（用于启用/禁用特定特性）
    
    /**
     * 验证配置
     * 
     * 验证并修正 Engine 配置参数。
     * 
     * @param config 配置结构
     * @return 验证后的配置
     */
    static Config validateConfig(Config config) noexcept;
};

/**
 * 创建 Engine 实例
 * 
 * 这是 Engine 创建的主要入口点。创建过程包括：
 * 1. 分配 FEngine 对象
 * 2. 创建 Platform 和 Driver（多线程或单线程模式）
 * 3. 初始化 Engine 的所有子系统
 * 
 * @param builder Engine 构建器，包含配置参数
 * @return 创建的 Engine 实例，如果失败返回 nullptr
 */
Engine* FEngine::create(Builder const& builder) {
    FILAMENT_TRACING_ENABLE(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 分配 FEngine 实例
     * 
     * 使用对齐分配（因为 FEngine 可能包含需要对齐的成员）。
     */
    FEngine* instance = new FEngine(builder);

    /**
     * 初始化所有需要 FEngine 实例的字段
     * 
     * 这不能在构造函数中安全地完成，因为某些初始化需要完整的 FEngine 对象。
     */

    /**
     * 创建 Platform 和 Driver
     * 
     * 通常我们在单独的线程中启动并创建上下文和 Driver（见 FEngine::loop）。
     * 在单线程情况下，我们在这里立即创建。
     */
    if constexpr (!UTILS_HAS_THREADING) {
        /**
         * 单线程模式：立即创建 Platform 和 Driver
         * 
         * 在单线程构建中，所有操作都在主线程中执行。
         */
        Platform* platform = builder->mPlatform;  // 获取平台指针
        void* const sharedContext = builder->mSharedContext;  // 获取共享上下文

        /**
         * 如果没有提供 Platform，创建默认的
         * 
         * PlatformFactory 会根据后端类型创建相应的平台实现。
         */
        if (platform == nullptr) {
            platform = PlatformFactory::create(&instance->mBackend);  // 创建默认平台
            instance->mPlatform = platform;  // 保存平台指针
            instance->mOwnPlatform = true;  // 标记为拥有 Platform，析构时释放
        }
        if (platform == nullptr) {
            LOG(ERROR) << "Selected backend not supported in this build.";  // 后端不支持
            delete instance;  // 清理实例
            return nullptr;  // 返回失败
        }
        /**
         * 创建 Driver（在当前线程）
         * 
         * Driver 是后端抽象层的实现，负责实际的 GPU 命令执行。
         */
        instance->mDriver = platform->createDriver(sharedContext, getDriverConfig(instance));

    } else {
        /**
         * 多线程模式：在单独线程中创建 Driver
         * 
         * 在多线程构建中，Driver 在专用线程中运行，避免阻塞主线程。
         */
        // 启动驱动线程（在 FEngine::loop 中创建 Driver）
        instance->mDriverThread = std::thread(&FEngine::loop, instance);

        /**
         * 等待 Driver 准备就绪（通过栅栏同步）
         * 
         * mDriverBarrier 确保在 Driver 完全初始化之前不会继续执行。
         */
        instance->mDriverBarrier.await();

        if (UTILS_UNLIKELY(!instance->mDriver)) {
            /**
             * Driver 初始化失败
             * 
             * 清理资源并返回失败。
             */
            instance->mDriverThread.join();  // 等待线程结束
            delete instance;  // 清理实例
            return nullptr;  // 返回失败
        }
    }

    /**
     * 现在可以初始化 Engine 的大部分子系统
     * 
     * 此时 Driver 已经可用，可以执行 Driver 命令。
     * init() 方法会初始化所有子系统（材质缓存、后处理管理器等）。
     */
    instance->init();

    // 单线程模式：立即执行一次命令队列
    if constexpr (!UTILS_HAS_THREADING) {
        instance->execute();
    }

    return instance;
}

#if UTILS_HAS_THREADING

/**
 * 异步创建 Engine 实例
 * 
 * 在单独线程中创建 Engine，并通过回调函数通知创建完成。
 * 这对于需要非阻塞初始化很有用。
 * 
 * @param builder Engine 构建器，包含配置参数
 * @param callback 回调函数（会被移动）
 *                回调函数签名：void(void*)，参数是创建的 Engine 指针
 *                回调会在 Driver 初始化完成后在单独线程中执行
 * 
 * 实现：
 * 1. 创建 FEngine 实例
 * 2. 在单独线程中启动 Driver（在 FEngine::loop 中创建 Driver）
 * 3. 在另一个线程中等待 Driver 初始化完成，然后调用回调
 * 4. 回调线程会自动分离，完成后自动销毁
 * 
 * 注意：回调函数会在非主线程中执行，不能进行线程相关的操作
 */
void FEngine::create(Builder const& builder, Invocable<void(void*)>&& callback) {
    FILAMENT_TRACING_ENABLE(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 创建 FEngine 实例
     */
    FEngine* instance = new FEngine(builder);

    /**
     * 启动驱动线程（在 FEngine::loop 中创建 Driver）
     */
    instance->mDriverThread = std::thread(&FEngine::loop, instance);

    /**
     * 启动回调线程
     * 
     * 在单独线程中等待 Driver 初始化完成，然后调用回调。
     * 这样可以确保回调在 Driver 完全准备好后才执行。
     */
    std::thread callbackThread = std::thread([instance, callback = std::move(callback)] {
        instance->mDriverBarrier.await();  // 等待 Driver 初始化完成
        callback(instance);  // 调用回调函数
    });

    /**
     * 分离回调线程，让它自动完成并销毁
     * 
     * 使用 detach() 而不是 join()，因为我们不等待回调完成。
     * 回调线程会在完成后自动销毁。
     */
    callbackThread.detach();
}

/**
 * 获取异步创建的 Engine 实例
 * 
 * 在异步创建后获取 Engine 实例。
 * 必须在调用 createAsync() 的同一线程中调用此方法。
 * 
 * @param token Engine 指针（由 createAsync() 的回调函数传递）
 * @return Engine 实例指针
 * 
 * 实现：
 * 1. 验证调用线程（必须与 createAsync() 在同一线程）
 * 2. 如果尚未初始化，则执行初始化
 * 3. 返回 Engine 实例
 * 
 * 注意：
 * - 必须在调用 createAsync() 的同一线程中调用
 * - 如果 Driver 初始化失败，返回 nullptr
 */
FEngine* FEngine::getEngine(void* token) {

    /**
     * 将 token 转换为 FEngine 指针
     */
    FEngine* instance = static_cast<FEngine*>(token);

    /**
     * 验证调用线程
     * 
     * createAsync() 和 getEngine() 必须在同一线程中调用。
     * 这确保了线程安全性。
     */
    FILAMENT_CHECK_PRECONDITION(ThreadUtils::isThisThread(instance->mMainThreadId))
            << "Engine::createAsync() and Engine::getEngine() must be called on the same thread.";

    /**
     * 如果尚未初始化，则执行初始化
     */
    if (!instance->mInitialized) {
        /**
         * 检查 Driver 是否初始化成功
         * 
         * 如果 Driver 初始化失败，清理资源并返回 nullptr。
         */
        if (UTILS_UNLIKELY(!instance->mDriver)) {
            /**
             * Driver 初始化失败
             * 
             * 等待驱动线程结束，然后清理实例。
             */
            instance->mDriverThread.join();  // 等待驱动线程结束
            delete instance;  // 清理实例
            return nullptr;  // 返回失败
        }

        /**
         * 初始化 Engine 的大部分子系统
         * 
         * 此时 Driver 已经可用，可以执行 Driver 命令。
         * init() 方法会初始化所有子系统（材质缓存、后处理管理器等）。
         */
        instance->init();
    }

    return instance;  // 返回 Engine 实例
}

#endif

/**
 * 全屏三角形顶点数据
 * 
 * 用于全屏后处理渲染的三角形顶点。
 * 这些坐标定义在 OpenGL 裁剪空间中，其他后端可以在顶点着色器中转换。
 * 
 * 注意：必须是静态的，因为只有指针被复制到渲染流中。
 * 
 * 三角形覆盖整个屏幕（从 (-1, -1) 到 (1, 1)），使用单个大三角形而不是两个。
 * 这种技术避免了图元设置的开销，比使用两个三角形更高效。
 * 
 * 顶点布局：
 * - 顶点 0: (-1, -1, 1, 1) - 左下角
 * - 顶点 1: ( 3, -1, 1, 1) - 右下角（延伸到屏幕外）
 * - 顶点 2: (-1,  3, 1, 1) - 左上角（延伸到屏幕外）
 */
static constexpr float4 sFullScreenTriangleVertices[3] = {
        { -1.0f, -1.0f, 1.0f, 1.0f },  // 左下角
        {  3.0f, -1.0f, 1.0f, 1.0f },  // 右下角（延伸到屏幕外）
        { -1.0f,  3.0f, 1.0f, 1.0f }   // 左上角（延伸到屏幕外）
};

/**
 * 全屏三角形索引数据
 * 
 * 定义全屏三角形的顶点索引顺序。
 * 
 * 注意：必须是静态的，因为只有指针被复制到渲染流中。
 * 
 * 索引顺序：0 -> 1 -> 2，形成一个顺时针方向的三角形。
 */
static constexpr uint16_t sFullScreenTriangleIndices[3] = { 0, 1, 2 };

/**
 * FEngine 构造函数
 * 
 * 初始化 Engine 的所有成员变量。注意：Driver 的创建在 create() 中进行，
 * 因为需要根据单线程/多线程模式选择不同的创建方式。
 * 
 * @param builder Engine 构建器，包含所有配置参数
 */
FEngine::FEngine(Builder const& builder) :
        mBackend(builder->mBackend),                    // 后端类型（OpenGL、Vulkan、Metal 等）
        mActiveFeatureLevel(builder->mFeatureLevel),    // 激活的特性级别
        mPlatform(builder->mPlatform),                  // 平台对象（可能为 nullptr）
        mSharedGLContext(builder->mSharedContext),      // 共享 OpenGL 上下文（可能为 nullptr）
        mPostProcessManager(*this),                     // 后处理管理器
        mEntityManager(EntityManager::get()),          // Entity 管理器（单例）
        mRenderableManager(*this),                      // Renderable 组件管理器
        mLightManager(*this),                          // Light 组件管理器
        mCameraManager(*this),                         // Camera 组件管理器
        mCommandBufferQueue(                            // 命令缓冲区队列
                builder->mConfig.minCommandBufferSizeMB * MiB,      // 最小缓冲区大小
                builder->mConfig.commandBufferSizeMB * MiB,         // 总缓冲区大小
                builder->mPaused),                      // 是否暂停
        mPerRenderPassArena(                            // 每渲染通道内存池
                "FEngine::mPerRenderPassAllocator",
                builder->mConfig.perRenderPassArenaSizeMB * MiB),
        mHeapAllocator("FEngine::mHeapAllocator", AreaPolicy::NullArea{}),  // 堆分配器
        mJobSystem(getJobSystemThreadPoolSize(builder->mConfig)),  // JobSystem（工作线程池）
        mEngineEpoch(std::chrono::steady_clock::now()), // Engine 启动时间点
        mDriverBarrier(1),                              // Driver 初始化栅栏（用于同步）
        mMainThreadId(ThreadUtils::getThreadId()),      // 主线程 ID
        mConfig(builder->mConfig)                       // 配置对象
{
    /**
     * 特性标志向后兼容处理
     * 
     * 如果 Builder 中没有指定特性标志，则从 Engine::Config 中读取。
     * 这允许旧的配置方式继续工作。
     */
    auto const featureFlagsBackwardCompatibility =
            [this, &builder](std::string_view const name, bool const value) {
        if (builder->mFeatureFlags.find(name) == builder->mFeatureFlags.end()) {
            auto* const p = getFeatureFlagPtr(name, true);
            if (p) {
                *p = value;  // 设置特性标志值
            }
        }
    };

    // 更新 Builder 中指定的所有特性标志
    for (auto const& feature : builder->mFeatureFlags) {
        auto* const p = getFeatureFlagPtr(feature.first.c_str_safe(), true);
        if (p) {
            *p = feature.second;
        }
    }

    // 更新 Engine::Config 中指定的"旧"特性标志（向后兼容）
    featureFlagsBackwardCompatibility("backend.disable_parallel_shader_compile",
            mConfig.disableParallelShaderCompile);
    featureFlagsBackwardCompatibility("backend.disable_handle_use_after_free_check",
            mConfig.disableHandleUseAfterFreeCheck);
    featureFlagsBackwardCompatibility("backend.opengl.assert_native_window_is_valid",
            mConfig.assertNativeWindowIsValid);

    // 假设我们在主线程中（可能不是这样）
    // 让 JobSystem 采用当前线程作为主线程
    mJobSystem.adopt();

    LOG(INFO) << "FEngine (" << sizeof(void*) * 8 << " bits) created at " << this << " "
              << "(threading is " << (UTILS_HAS_THREADING ? "enabled)" : "disabled)");
}

/**
 * 获取 JobSystem 线程池大小
 * 
 * 根据配置和硬件并发数计算工作线程数量。
 * 
 * 策略：
 * - 如果配置中指定了线程数，使用配置值
 * - 否则：硬件并发数 - 2（1 个用户线程 + 1 个后端线程）
 * - 确保至少有 1 个工作线程
 * 
 * @param config Engine 配置
 * @return 工作线程数量
 */
uint32_t FEngine::getJobSystemThreadPoolSize(Config const& config) noexcept {
    if (config.jobSystemThreadCount > 0) {
        return config.jobSystemThreadCount;  // 使用配置值
    }

    // 1 个线程给用户，1 个线程给后端
    // 剩余线程用于 JobSystem 工作线程
    int threadCount = int(std::thread::hardware_concurrency()) - 2;
    // 确保至少有 1 个工作线程
    threadCount = std::max(1, threadCount);
    return threadCount;
}

/**
 * 初始化 Engine
 * 
 * 在 Driver 线程初始化后调用。此时可以执行 Driver 命令。
 * 
 * 初始化流程：
 * 1. 创建 DriverApi（命令流接口）
 * 2. 确定特性级别
 * 3. 创建资源分配器
 * 4. 创建全屏三角形（用于后处理）
 * 5. 创建默认资源（材质、纹理、IBL 等）
 * 6. 创建描述符堆布局
 * 7. 初始化 UboManager（如果启用）
 */
void FEngine::init() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    // 这必须是第一步：创建 DriverApi
    // 确保 mDriverApiStorage 正确对齐
    assert_invariant( intptr_t(&mDriverApiStorage) % alignof(DriverApi) == 0 );
    // 使用 placement new 在预分配的存储中构造 DriverApi
    ::new(&mDriverApiStorage) DriverApi(*mDriver, mCommandBufferQueue.getCircularBuffer());

    DriverApi& driverApi = getDriverApi();

    mActiveFeatureLevel = std::min(mActiveFeatureLevel, driverApi.getFeatureLevel());

#ifndef FILAMENT_ENABLE_FEATURE_LEVEL_0
    assert_invariant(mActiveFeatureLevel > FeatureLevel::FEATURE_LEVEL_0);
#endif

    LOG(INFO) << "Backend feature level: " << int(driverApi.getFeatureLevel());
    LOG(INFO) << "FEngine feature level: " << int(mActiveFeatureLevel);


    mResourceAllocatorDisposer = std::make_shared<ResourceAllocatorDisposer>(driverApi);

    mFullScreenTriangleVb = downcast(VertexBuffer::Builder()
            .vertexCount(3)
            .bufferCount(1)
            .attribute(POSITION, 0, VertexBuffer::AttributeType::FLOAT4, 0)
            .build(*this));

    mFullScreenTriangleVb->setBufferAt(*this, 0,
            { sFullScreenTriangleVertices, sizeof(sFullScreenTriangleVertices) });

    mFullScreenTriangleIb = downcast(IndexBuffer::Builder()
            .indexCount(3)
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(*this));

    mFullScreenTriangleIb->setBuffer(*this,
            { sFullScreenTriangleIndices, sizeof(sFullScreenTriangleIndices) });

    mFullScreenTriangleRph = driverApi.createRenderPrimitive(
            mFullScreenTriangleVb->getHwHandle(), mFullScreenTriangleIb->getHwHandle(),
            PrimitiveType::TRIANGLES);

    /**
     * 计算从裁剪空间 [-1, 1] 到纹理空间 [0, 1] 的变换矩阵
     * 
     * 考虑到不同后端的差异：
     * - OpenGL: Y 轴不翻转（从下到上）
     * - Metal/Vulkan/WebGPU: Y 轴翻转（从上到下）
     * 
     * 矩阵将裁剪坐标转换为纹理坐标：
     * - X: [-1, 1] -> [0, 1]（缩放 0.5，偏移 0.5）
     * - Y: [-1, 1] -> [0, 1]（缩放 0.5，偏移 0.5，可能翻转）
     * - Z: 保持不变（用于深度）
     */
    const bool textureSpaceYFlipped = mBackend == Backend::METAL || mBackend == Backend::VULKAN ||
                                      mBackend == Backend::WEBGPU;
    if (textureSpaceYFlipped) {
        /**
         * Metal/Vulkan/WebGPU: Y 轴翻转
         * 
         * Y 坐标变换：y_tex = -0.5 * y_clip + 0.5
         * 将裁剪空间的 [-1, 1] 映射到纹理空间的 [1, 0]（翻转）
         */
        mUvFromClipMatrix = mat4f(mat4f::row_major_init{
                0.5f,  0.0f,   0.0f, 0.5f,  // X: 缩放 0.5，偏移 0.5
                0.0f, -0.5f,   0.0f, 0.5f,  // Y: 缩放 -0.5（翻转），偏移 0.5
                0.0f,  0.0f,   1.0f, 0.0f,  // Z: 保持不变
                0.0f,  0.0f,   0.0f, 1.0f   // W: 保持不变
        });
    } else {
        /**
         * OpenGL: Y 轴不翻转
         * 
         * Y 坐标变换：y_tex = 0.5 * y_clip + 0.5
         * 将裁剪空间的 [-1, 1] 映射到纹理空间的 [0, 1]（正常）
         */
        mUvFromClipMatrix = mat4f(mat4f::row_major_init{
                0.5f,  0.0f,   0.0f, 0.5f,  // X: 缩放 0.5，偏移 0.5
                0.0f,  0.5f,   0.0f, 0.5f,  // Y: 缩放 0.5，偏移 0.5
                0.0f,  0.0f,   1.0f, 0.0f,  // Z: 保持不变
                0.0f,  0.0f,   0.0f, 1.0f   // W: 保持不变
        });
    }

    /**
     * 初始化默认纹理
     * 
     * 创建引擎内部使用的默认纹理，确保其内容已定义。
     */

    /**
     * 创建默认 IBL 纹理（立方体贴图）
     * 
     * 用于没有设置间接光的场景，避免着色器读取未定义的纹理。
     */
    mDefaultIblTexture = downcast(Texture::Builder()
            .width(1).height(1).levels(1)
            .format(Texture::InternalFormat::RGBA8)
            .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
            .build(*this));

    /**
     * 默认纹理数据
     * 
     * - zeroCubemap: 零值立方体贴图（6 个面，每个面 1 个像素）
     * - zeroRGBA: 零值 RGBA（黑色，Alpha=0）
     * - oneRGBA: 全一 RGBA（白色，Alpha=1）
     * - oneFloat: 全一浮点数（1.0f，用于深度纹理）
     */
    static constexpr std::array<uint32_t, 6> zeroCubemap{};  // 零值立方体贴图（6 个面）
    static constexpr std::array<uint32_t, 1> zeroRGBA{};     // 零值 RGBA（黑色）
    static constexpr std::array<uint32_t, 1> oneRGBA{ 0xffffffff };  // 全一 RGBA（白色）
    static constexpr std::array<float   , 1> oneFloat{ 1.0f };       // 全一浮点数（用于深度）
    
    /**
     * Lambda 函数：计算数组大小（字节数）
     * 
     * 用于计算数组在内存中的字节大小。
     */
    auto const size = [](auto&& array) {
        return array.size() * sizeof(decltype(array[0]));
    };

    driverApi.update3DImage(mDefaultIblTexture->getHwHandle(), 0, 0, 0, 0, 1, 1, 6,
            { zeroCubemap.data(), size(zeroCubemap), Texture::Format::RGBA, Texture::Type::UBYTE });

    // 3 bands = 9 float3
    constexpr float sh[9 * 3] = { 0.0f };
    mDefaultIbl = downcast(IndirectLight::Builder()
            .irradiance(3, reinterpret_cast<const float3*>(sh))
            .build(*this));

    /**
     * 创建默认渲染目标
     * 
     * 创建默认的渲染目标句柄（通常是交换链的后台缓冲区）。
     */
    mDefaultRenderTarget = driverApi.createDefaultRenderTarget();

    // Create a dummy morph target buffer, without using the builder
    /**
     * 创建虚拟变形目标缓冲区（不使用构建器）
     * 
     * 用于未指定变形目标的情况，避免着色器读取未定义的缓冲区。
     */
    mDummyMorphTargetBuffer = createMorphTargetBuffer(
            FMorphTargetBuffer::EmptyMorphTargetBuilder());

    // create dummy textures we need throughout the engine
    /**
     * 创建引擎中需要的虚拟纹理
     * 
     * 这些虚拟纹理用于：
     * - mDummyOneTexture: 全一纹理（白色，用于未绑定的纹理槽）
     * - mDummyZeroTexture: 零值纹理（黑色，用于未绑定的纹理槽）
     */
    mDummyOneTexture = driverApi.createTexture(SamplerType::SAMPLER_2D, 1,
            TextureFormat::RGBA8, 1, 1, 1, 1, TextureUsage::DEFAULT);

    mDummyZeroTexture = driverApi.createTexture(SamplerType::SAMPLER_2D, 1,
            TextureFormat::RGBA8, 1, 1, 1, 1, TextureUsage::DEFAULT);

    /**
     * 更新虚拟纹理数据
     * 
     * - mDummyOneTexture: 填充全一 RGBA（白色，Alpha=1）
     * - mDummyZeroTexture: 填充零值 RGBA（黑色，Alpha=0）
     */
    driverApi.update3DImage(mDummyOneTexture, 0, 0, 0, 0, 1, 1, 1,
            { oneRGBA.data(), size(oneRGBA), Texture::Format::RGBA, Texture::Type::UBYTE });

    driverApi.update3DImage(mDummyZeroTexture, 0, 0, 0, 0, 1, 1, 1,
            { zeroRGBA.data(), size(zeroRGBA), Texture::Format::RGBA, Texture::Type::UBYTE });


    /**
     * 创建每视图描述符堆布局（SSR 变体）
     * 
     * 用于屏幕空间反射（SSR）的着色器变体的描述符堆布局。
     */
    mPerViewDescriptorSetLayoutSsrVariant = {
            mHwDescriptorSetLayoutFactory,
            driverApi,
            descriptor_sets::getSsrVariantLayout() };

    /**
     * 创建每视图描述符堆布局（深度变体）
     * 
     * 用于深度通道的着色器变体的描述符堆布局。
     */
    mPerViewDescriptorSetLayoutDepthVariant = {
            mHwDescriptorSetLayoutFactory,
            driverApi,
            descriptor_sets::getDepthVariantLayout() };

    /**
     * 创建每可渲染对象描述符堆布局
     * 
     * 用于可渲染对象的描述符堆布局，包含材质相关的资源绑定。
     */
    mPerRenderableDescriptorSetLayout = {
            mHwDescriptorSetLayoutFactory,
            driverApi,
            descriptor_sets::getPerRenderableLayout() };

    /**
     * 创建默认材质
     * 
     * 根据特性级别和立体渲染类型选择相应的默认材质：
     * - 特性级别 0: 使用特性级别 0 的默认材质
     * - 特性级别 1+: 根据立体渲染类型选择：
     *   - NONE/INSTANCED: 标准默认材质
     *   - MULTIVIEW: 多视图默认材质（需要多视图支持）
     */
#ifdef FILAMENT_ENABLE_FEATURE_LEVEL_0
    if (UTILS_UNLIKELY(mActiveFeatureLevel == FeatureLevel::FEATURE_LEVEL_0)) {
        FMaterial::DefaultMaterialBuilder defaultMaterialBuilder;
        defaultMaterialBuilder.package(
                MATERIALS_DEFAULTMATERIAL_FL0_DATA, MATERIALS_DEFAULTMATERIAL_FL0_SIZE);
        mDefaultMaterial = downcast(defaultMaterialBuilder.build(*const_cast<FEngine*>(this)));
    } else
#endif
    {
        FMaterial::DefaultMaterialBuilder defaultMaterialBuilder;
        switch (mConfig.stereoscopicType) {
            case StereoscopicType::NONE:
            case StereoscopicType::INSTANCED:
                defaultMaterialBuilder.package(
                        MATERIALS_DEFAULTMATERIAL_DATA, MATERIALS_DEFAULTMATERIAL_SIZE);
                break;
            case StereoscopicType::MULTIVIEW:
#ifdef FILAMENT_ENABLE_MULTIVIEW
                defaultMaterialBuilder.package(
                        MATERIALS_DEFAULTMATERIAL_MULTIVIEW_DATA,
                        MATERIALS_DEFAULTMATERIAL_MULTIVIEW_SIZE);
#else
                assert_invariant(false);
#endif
                break;
        }
        mDefaultMaterial = downcast(defaultMaterialBuilder.build(*this));
    }

    // We must commit the default material instance here. It may not be used in a scene, but its
    // descriptor set may still be used for shared variants.
    //
    // Note that this material instance is instantiated before the creation of UboManager, so at
    // this point `isUboBatchingEnabled` is `false`, and it will fall back to individual UBO
    // automatically.
    /**
     * 提交默认材质实例
     * 
     * 必须在此时提交默认材质实例，因为：
     * - 它可能不在场景中使用，但其描述符堆仍可能用于共享变体
     * - 此材质实例在 UboManager 创建之前实例化，此时 `isUboBatchingEnabled` 为 false
     * - 会自动回退到单独的 UBO
     */
    mDefaultMaterial->getDefaultInstance()->commit(driverApi, mUboManager);

    /**
     * 特性级别 1 及以上的初始化
     * 
     * UBO 批处理、颜色分级等功能仅在特性级别 1 及以上支持。
     */
    if (UTILS_UNLIKELY(getSupportedFeatureLevel() >= FeatureLevel::FEATURE_LEVEL_1)) {
        // UBO batching is not supported in feature level 0
        /**
         * 创建 UBO 管理器（如果启用材质实例 uniform 批处理）
         * 
         * UBO 批处理用于合并多个材质实例的 uniform 数据到一个共享缓冲区，
         * 减少绘制调用次数，提高性能。
         * 
         * 槽大小计算：
         * - 最小 16 字节（每个材质实例的 UBO 大小至少为 16 字节）
         * - 或驱动要求的 UBO 偏移对齐值（取较大者）
         */
        if (features.material.enable_material_instance_uniform_batching) {
            // Ubo size of each material instance is at least 16 bytes.
            constexpr BufferAllocator::allocation_size_t minSlotSize = 16;
            auto const uboOffsetAlignment = static_cast<BufferAllocator::allocation_size_t>(
                    driverApi.getUniformBufferOffsetAlignment());
            BufferAllocator::allocation_size_t slotSize = std::max(minSlotSize, uboOffsetAlignment);
            mUboManager = new UboManager(getDriverApi(), slotSize, mConfig.sharedUboInitialSizeInBytes);
        }

        /**
         * 创建默认颜色分级
         */
        mDefaultColorGrading = downcast(ColorGrading::Builder().build(*this));

        /**
         * 初始化虚拟变形目标缓冲区数据
         * 
         * 设置零值位置和切线数据，确保缓冲区已初始化。
         */
        constexpr float3 dummyPositions[1] = {};
        constexpr short4 dummyTangents[1] = {};
        mDummyMorphTargetBuffer->setPositionsAt(*this, 0, dummyPositions, 1, 0);
        mDummyMorphTargetBuffer->setTangentsAt(*this, 0, dummyTangents, 1, 0);

        /**
         * 创建虚拟纹理数组
         * 
         * 用于未绑定的纹理数组槽：
         * - mDummyOneTextureArray: 全一 RGBA 纹理数组
         * - mDummyOneTextureArrayDepth: 全一深度纹理数组
         * - mDummyZeroTextureArray: 零值 RGBA 纹理数组
         */
        mDummyOneTextureArray = driverApi.createTexture(SamplerType::SAMPLER_2D_ARRAY, 1,
                TextureFormat::RGBA8, 1, 1, 1, 1, TextureUsage::DEFAULT);

        mDummyOneTextureArrayDepth = driverApi.createTexture(SamplerType::SAMPLER_2D_ARRAY, 1,
                TextureFormat::DEPTH32F, 1, 1, 1, 1, TextureUsage::DEFAULT);

        mDummyZeroTextureArray = driverApi.createTexture(SamplerType::SAMPLER_2D_ARRAY, 1,
                TextureFormat::RGBA8, 1, 1, 1, 1, TextureUsage::DEFAULT);

        /**
         * 更新虚拟纹理数组数据
         */
        driverApi.update3DImage(mDummyOneTextureArray, 0, 0, 0, 0, 1, 1, 1,
                { oneRGBA.data(), size(oneRGBA), Texture::Format::RGBA, Texture::Type::UBYTE });

        driverApi.update3DImage(mDummyOneTextureArrayDepth, 0, 0, 0, 0, 1, 1, 1,
                { oneFloat.data(), size(oneFloat), Texture::Format::DEPTH_COMPONENT, Texture::Type::FLOAT });

        driverApi.update3DImage(mDummyZeroTextureArray, 0, 0, 0, 0, 1, 1, 1,
                { zeroRGBA.data(), size(zeroRGBA), Texture::Format::RGBA, Texture::Type::UBYTE });

        /**
         * 创建虚拟 uniform 缓冲区
         * 
         * 用于未绑定的 uniform 缓冲区槽，大小为最小规范 UBO 大小。
         */
        mDummyUniformBuffer = driverApi.createBufferObject(CONFIG_MINSPEC_UBO_SIZE,
                BufferObjectBinding::UNIFORM, BufferUsage::STATIC);

        /**
         * 初始化光源管理器和 DFG（分布函数）
         */
        mLightManager.init(*this);
        mDFG.init(*this);
    }

    /**
     * 初始化后处理管理器
     * 
     * 后处理管理器负责所有后处理效果的渲染（如 Bloom、TAA、SSR 等）。
     */
    mPostProcessManager.init();

    /**
     * 注册调试属性：方向阴影贴图调试
     * 
     * 用于可视化方向光阴影贴图。当属性改变时，更新所有表面材质的
     * 专用常量并使其相关变体无效化，触发重新编译。
     */
    mDebugRegistry.registerProperty("d.shadowmap.debug_directional_shadowmap",
            &debug.shadowmap.debug_directional_shadowmap, [this] {
                mMaterials.forEach([this](FMaterial* material) {
                    if (material->getMaterialDomain() == MaterialDomain::SURFACE) {

                        material->setConstant(
                                +ReservedSpecializationConstants::CONFIG_DEBUG_DIRECTIONAL_SHADOWMAP,
                                debug.shadowmap.debug_directional_shadowmap);

                        material->invalidate(
                                Variant::DIR | Variant::SRE | Variant::DEP,
                                Variant::DIR | Variant::SRE);
                    }
                });
            });

    /**
     * 注册调试属性：Froxel 可视化调试
     * 
     * 用于可视化光照的 Froxel（体素）结构。当属性改变时，更新所有表面材质的
     * 专用常量并使其相关变体无效化，触发重新编译。
     */
    mDebugRegistry.registerProperty("d.lighting.debug_froxel_visualization",
            &debug.lighting.debug_froxel_visualization, [this] {
                mMaterials.forEach([this](FMaterial* material) {
                    if (material->getMaterialDomain() == MaterialDomain::SURFACE) {

                        material->setConstant(
                                +ReservedSpecializationConstants::CONFIG_DEBUG_FROXEL_VISUALIZATION,
                                debug.lighting.debug_froxel_visualization);

                        material->invalidate(
                                Variant::DYN | Variant::DEP,
                                Variant::DYN);
                    }
                });
            });

    /**
     * 标记 Engine 已初始化
     * 
     * 所有子系统初始化完成后，设置此标志表示 Engine 已准备好使用。
     */
    mInitialized = true;
}

/**
 * FEngine 析构函数
 * 
 * 清理 Engine 资源。
 * 注意：必须先调用 shutdown() 再销毁 Engine，否则会触发断言。
 * 
 * 清理顺序：
 * 1. 验证资源分配器已释放（应通过 shutdown() 完成）
 * 2. 销毁 Driver
 * 3. 如果拥有 Platform，销毁 Platform
 */
FEngine::~FEngine() noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    /**
     * 验证资源分配器已释放
     * 
     * 资源分配器应在 shutdown() 中释放。如果此处仍存在，说明 shutdown() 未被调用。
     */
    assert_invariant(!mResourceAllocatorDisposer);
    /**
     * 销毁 Driver
     */
    delete mDriver;
    /**
     * 如果拥有 Platform，销毁它
     */
    if (mOwnPlatform) {
        PlatformFactory::destroy(&mPlatform);
    }
}

/**
 * 关闭 Engine
 * 
 * 清理所有资源并关闭后端。必须在销毁 Engine 前调用此方法。
 * 
 * 清理顺序：
 * 1. 验证在主线程中调用
 * 2. 打印统计信息（调试模式）
 * 3. 释放后处理管理器资源
 * 4. 释放资源分配器
 * 5. 释放 DFG（分布函数）
 * 6. 释放所有组件管理器（Renderable、Light、Camera）
 * 7. 释放描述符堆布局
 * 8. 释放全屏三角形
 * 9. 释放默认资源（IBL、材质、颜色分级等）
 * 10. 清理用户创建的资源列表
 * 11. 释放虚拟纹理
 * 12. 关闭后端（刷新命令、等待线程退出）
 * 13. 终止 JobSystem
 */
void FEngine::shutdown() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 验证资源分配器存在
     * 
     * 根据构造方式，资源分配器应该始终存在。
     */
    assert_invariant(mResourceAllocatorDisposer);

    FILAMENT_CHECK_PRECONDITION(ThreadUtils::isThisThread(mMainThreadId))
            << "Engine::shutdown() called from the wrong thread!";

#ifndef NDEBUG
    // print out some statistics about this run
    size_t const wm = mCommandBufferQueue.getHighWatermark();
    size_t const wmpct = wm / (getCommandBufferSize() / 100);
    DLOG(INFO) << "CircularBuffer: High watermark " << wm / 1024 << " KiB (" << wmpct << "%)";
#endif

    DriverApi& driver = getDriverApi();

    /*
     * Destroy our own state first
     * 首先销毁我们自己的状态
     */

    // 释放后处理管理器资源
    mPostProcessManager.terminate(driver);  // free-up post-process manager resources
    // 终止资源分配器并释放
    mResourceAllocatorDisposer->terminate();
    mResourceAllocatorDisposer.reset();
    // 释放 DFG（分布函数）资源
    mDFG.terminate(*this);                  // free-up the DFG
    // 释放所有可渲染对象
    mRenderableManager.terminate();         // free-up all renderables
    // 释放所有光源
    mLightManager.terminate();              // free-up all lights
    // 释放所有摄像机
    mCameraManager.terminate(*this);        // free-up all cameras

    // 释放描述符堆布局
    mPerViewDescriptorSetLayoutDepthVariant.terminate(mHwDescriptorSetLayoutFactory, driver);
    mPerViewDescriptorSetLayoutSsrVariant.terminate(mHwDescriptorSetLayoutFactory, driver);
    mPerRenderableDescriptorSetLayout.terminate(mHwDescriptorSetLayoutFactory, driver);

    // 销毁全屏三角形渲染图元
    driver.destroyRenderPrimitive(std::move(mFullScreenTriangleRph));

    // 销毁全屏三角形索引缓冲区
    destroy(mFullScreenTriangleIb);
    mFullScreenTriangleIb = nullptr;

    // 销毁全屏三角形顶点缓冲区
    destroy(mFullScreenTriangleVb);
    mFullScreenTriangleVb = nullptr;

    // 销毁虚拟变形目标缓冲区
    destroy(mDummyMorphTargetBuffer);
    mDummyMorphTargetBuffer = nullptr;

    // 销毁默认 IBL 纹理
    destroy(mDefaultIblTexture);
    mDefaultIblTexture = nullptr;

    // 销毁默认 IBL（间接光）
    destroy(mDefaultIbl);
    mDefaultIbl = nullptr;

    // 销毁默认颜色分级
    destroy(mDefaultColorGrading);
    mDefaultColorGrading = nullptr;

    // 销毁默认材质
    destroy(mDefaultMaterial);
    mDefaultMaterial = nullptr;

    // 销毁未保护的虚拟交换链
    destroy(mUnprotectedDummySwapchain);
    mUnprotectedDummySwapchain = nullptr;

    /*
     * clean-up after the user -- we call terminate on each "leaked" object and clear each list.
     *
     * This should free up everything.
     * 清理用户创建的资源 - 我们对每个"泄漏"的对象调用 terminate 并清空每个列表。
     * 这应该释放所有资源。
     */

    // try to destroy objects in the inverse dependency
    // 尝试按逆依赖顺序销毁对象（从依赖者到被依赖者）
    cleanupResourceList(std::move(mRenderers));
    cleanupResourceList(std::move(mViews));
    cleanupResourceList(std::move(mScenes));
    cleanupResourceList(std::move(mSkyboxes));
    cleanupResourceList(std::move(mColorGradings));

    // this must be done after Skyboxes and before materials
    // 这必须在 Skyboxes 之后、Materials 之前完成（因为天空盒材质可能被天空盒使用）
    destroy(mSkyboxMaterial);
    mSkyboxMaterial = nullptr;

    // 清理各种缓冲区资源
    cleanupResourceList(std::move(mBufferObjects));
    cleanupResourceList(std::move(mIndexBuffers));
    cleanupResourceList(std::move(mMorphTargetBuffers));
    cleanupResourceList(std::move(mSkinningBuffers));
    cleanupResourceList(std::move(mVertexBuffers));
    // 清理纹理和渲染目标
    cleanupResourceList(std::move(mTextures));
    cleanupResourceList(std::move(mRenderTargets));
    // 清理材质和实例缓冲区
    cleanupResourceList(std::move(mMaterials));
    cleanupResourceList(std::move(mInstanceBuffers));
    // 清理所有材质实例（按材质分组）
    for (auto& item : mMaterialInstances) {
        cleanupResourceList(std::move(item.second));
    }

    // 清理需要锁保护的资源（栅栏）
    cleanupResourceListLocked(mFenceListLock, std::move(mFences));

    // 销毁虚拟纹理（用于未绑定的纹理槽）
    driver.destroyTexture(std::move(mDummyOneTexture));
    driver.destroyTexture(std::move(mDummyOneTextureArray));
    driver.destroyTexture(std::move(mDummyZeroTexture));
    driver.destroyTexture(std::move(mDummyZeroTextureArray));
    driver.destroyTexture(std::move(mDummyOneTextureArrayDepth));

    // 销毁虚拟 uniform 缓冲区
    driver.destroyBufferObject(std::move(mDummyUniformBuffer));

    // 销毁默认渲染目标
    driver.destroyRenderTarget(std::move(mDefaultRenderTarget));

    // 如果启用 UBO 批处理，终止 UBO 管理器
    if (isUboBatchingEnabled()) {
        mUboManager->terminate(driver);
        delete mUboManager;
        mUboManager = nullptr;
    }

    /*
     * Shutdown the backend...
     * 关闭后端...
     */

    // There might be commands added by the `terminate()` calls, so we need to flush all commands
    // up to this point. After flushCommandBuffer() is called, all pending commands are guaranteed
    // to be executed before the driver thread exits.
    // `terminate()` 调用可能添加了命令，因此我们需要刷新到此为止的所有命令。
    // 调用 flushCommandBuffer() 后，保证所有待处理的命令在驱动线程退出前执行。
    flushCommandBuffer(mCommandBufferQueue);

    // now wait for all pending commands to be executed and the thread to exit
    // 现在等待所有待处理的命令执行完成，线程退出
    mCommandBufferQueue.requestExit();
    if constexpr (!UTILS_HAS_THREADING) {
        // 单线程模式：立即执行并终止
        execute();
        getDriverApi().terminate();
    } else {
        // 多线程模式：等待驱动线程退出
        mDriverThread.join();
        // Driver::terminate() has been called here.
        // Driver::terminate() 已在此处调用。
    }

    // Finally, call user callbacks that might have been scheduled.
    // These callbacks CANNOT call driver APIs.
    // 最后，调用可能已调度的用户回调。
    // 这些回调不能调用驱动 API。
    getDriver().purge();

    // and destroy the CommandStream
    // 销毁命令流（DriverApi）
    std::destroy_at(std::launder(reinterpret_cast<DriverApi*>(&mDriverApiStorage)));

    /*
     * Terminate the JobSystem...
     * 终止 JobSystem...
     */

    // detach this thread from the JobSystem
    // 将此线程从 JobSystem 分离
    mJobSystem.emancipate();
}

/**
 * 准备帧
 * 
 * 在每帧渲染前调用一次，准备材质实例和 UBO。
 * 
 * 处理流程：
 * 1. 如果启用 UBO 批处理，开始帧（分配 UBO 槽位）
 * 2. 提交所有表面材质实例的 uniform（后处理材质实例需要显式提交）
 * 3. 如果启用 UBO 批处理，完成帧开始（映射 UBO 缓冲区）
 * 4. 检查材质程序编辑（调试模式）
 * 
 * 注意：
 * - 理想情况下只上传可见对象的 UBO，但当前实现上传所有实例
 * - 由于 UBO 未改变时跳过上传，性能影响不大
 * - 后处理材质实例的 uniform 通常在此时还未设置，需要显式提交
 */
void FEngine::prepare() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    DriverApi& driver = getDriverApi();
    const bool useUboBatching = isUboBatchingEnabled();

    /**
     * 如果启用 UBO 批处理，开始帧
     * 
     * 这会：
     * - 释放不再被 GPU 使用的 UBO 槽位
     * - 为需要槽位的材质实例分配新槽位
     * - 如果缓冲区不足，重新分配更大的共享 UBO
     * - 映射共享 UBO 到 CPU 可访问的内存
     */
    if (useUboBatching) {
        assert_invariant(mUboManager != nullptr);
        mUboManager->beginFrame(driver);
    }

    /**
     * 提交所有表面材质实例的 uniform
     * 
     * 只提交表面材质实例，因为：
     * - 后处理材质实例的 uniform 通常在此时还未设置
     * - 后处理材质实例需要显式调用 commit()
     */
    UboManager* uboManager = mUboManager;
    for (auto& materialInstanceList: mMaterialInstances) {
        materialInstanceList.second.forEach([&driver, uboManager](FMaterialInstance* item) {
            if (item->getMaterial()->getMaterialDomain() == MaterialDomain::SURFACE) {
                item->commit(driver, uboManager);
            }
        });
    }

    /**
     * 如果启用 UBO 批处理，完成帧开始
     * 
     * 这会取消映射共享 UBO 缓冲区。
     */
    if (useUboBatching) {
        assert_invariant(mUboManager != nullptr);
        getUboManager()->finishBeginFrame(getDriverApi());
    }

    /**
     * 检查材质程序编辑（仅在调试模式启用）
     * 
     * 用于材质调试工具，检查是否有程序修改。
     */
    mMaterials.forEach([](FMaterial* material) {
#if FILAMENT_ENABLE_MATDBG // NOLINT(*-include-cleaner)
        material->checkProgramEdits();
#endif
    });
}

/**
 * 垃圾回收
 * 
 * 清理已删除实体的组件。此方法在 Job 中运行。
 * 
 * 处理流程：
 * 1. 获取实体管理器
 * 2. 对所有组件管理器执行垃圾回收（Renderable、Light、Transform、Camera）
 * 
 * 注意：此方法在后台线程中运行，必须确保线程安全。
 */
void FEngine::gc() {
    auto& em = mEntityManager;
    mRenderableManager.gc(em);   // 清理 Renderable 组件
    mLightManager.gc(em);        // 清理 Light 组件
    mTransformManager.gc(em);    // 清理 Transform 组件
    mCameraManager.gc(*this, em); // 清理 Camera 组件
}

/**
 * 提交帧
 * 
 * 在帧结束时调用，标记帧完成。
 * 
 * 如果启用 UBO 批处理，会创建栅栏并跟踪该帧使用的 UBO 分配。
 * 这些分配的 gpuUseCount 会增加，并在相应帧完成后减少。
 */
void FEngine::submitFrame() {
    if (isUboBatchingEnabled()) {
        DriverApi& driver = getDriverApi();
        getUboManager()->endFrame(driver);
    }
}

/**
 * 刷新命令缓冲区
 * 
 * 将当前命令缓冲区中的命令提交到 Driver 线程执行。
 * 这是一个异步操作，不会等待 GPU 完成。
 */
void FEngine::flush() {
    flushCommandBuffer(mCommandBufferQueue);
}

/**
 * 刷新并等待（无限等待）
 * 
 * 刷新命令缓冲区并等待所有 GPU 命令完成。
 * 这是一个同步操作，会阻塞直到 GPU 完成所有命令。
 * 
 * @return 如果所有命令成功完成返回 true，否则返回 false
 */
void FEngine::flushAndWait() {
    flushAndWait(FENCE_WAIT_FOR_EVER);
}

/**
 * 刷新并等待（指定超时）
 * 
 * 刷新命令缓冲区并等待所有 GPU 命令完成，最多等待指定时间。
 * 
 * @param timeout 超时时间（纳秒）
 *                - FENCE_WAIT_FOR_EVER: 无限等待
 *                - 其他值: 最大等待时间
 * @return 如果所有命令在超时前完成返回 true，否则返回 false
 * 
 * 实现流程：
 * 1. 验证命令队列未暂停
 * 2. 验证 Engine 未关闭
 * 3. 入队 finish 命令（阻塞直到 GPU 完成）
 * 4. 创建栅栏并等待
 * 5. 执行可能已调度的回调
 * 
 * 注意：如果渲染线程已暂停或 Engine 已关闭，会触发断言。
 */
bool FEngine::flushAndWait(uint64_t const timeout) {
    /**
     * 验证命令队列未暂停
     */
    FILAMENT_CHECK_PRECONDITION(!mCommandBufferQueue.isPaused())
            << "Cannot call Engine::flushAndWait() when rendering thread is paused!";

    /**
     * 验证 Engine 未关闭
     */
    FILAMENT_CHECK_PRECONDITION(!mCommandBufferQueue.isExitRequested())
            << "Calling Engine::flushAndWait() after Engine::shutdown()!";

    /**
     * 入队 finish 命令
     * 
     * 这会阻塞 Driver 线程直到 GPU 完成所有命令。
     */
    getDriverApi().finish();

    /**
     * 创建栅栏并等待
     */
    FFence* fence = createFence();
    FenceStatus const status = fence->wait(FFence::Mode::FLUSH, timeout);
    destroy(fence);

    /**
     * 执行可能已调度的回调
     * 
     * 这些回调可能包含资源清理等操作。
     */
    getDriver().purge();

    /**
     * 返回是否成功完成
     */
    return status == FenceStatus::CONDITION_SATISFIED;
}

// -----------------------------------------------------------------------------------------------
// Render thread / command queue
// -----------------------------------------------------------------------------------------------

/**
 * Driver 线程循环
 * 
 * 在单独线程中运行，处理 GPU 命令执行。
 * 这是多线程模式下的后端线程主循环。
 * 
 * 处理流程：
 * 1. 如果没有 Platform，创建默认 Platform
 * 2. 设置线程名称和优先级
 * 3. 创建 Driver
 * 4. 通知主线程 Driver 已创建（通过栅栏）
 * 5. 进入命令处理循环（执行命令队列）
 * 6. 关闭时终止 Driver
 * 
 * @return 退出代码（0 表示成功）
 * 
 * 注意：
 * - 此方法在单独线程中运行
 * - 使用栅栏与主线程同步
 * - 如果 Platform 创建失败，会通知主线程并退出
 */
int FEngine::loop() {
    /**
     * 如果没有 Platform，创建默认 Platform
     * 
     * 在多线程模式下，如果没有提供 Platform，则创建默认的。
     */
    if (mPlatform == nullptr) {
        mPlatform = PlatformFactory::create(&mBackend);
        mOwnPlatform = true;
        LOG(INFO) << "FEngine resolved backend: " << to_string(mBackend);
        if (mPlatform == nullptr) {
            /**
             * Platform 创建失败
             * 
             * 通知主线程并返回失败。
             */
            LOG(ERROR) << "Selected backend not supported in this build.";
            mDriverBarrier.latch();  // 通知主线程（创建失败）
            return 0;
        }
    }

    /**
     * 设置线程名称和优先级
     * 
     * 线程名称用于调试和性能分析。
     * 优先级设置为 DISPLAY，确保渲染线程有足够的 CPU 时间。
     */
    JobSystem::setThreadName("FEngine::loop");
    JobSystem::setThreadPriority(JobSystem::Priority::DISPLAY);

    mDriver = mPlatform->createDriver(mSharedGLContext, getDriverConfig(this));

    mDriverBarrier.latch();
    if (UTILS_UNLIKELY(!mDriver)) {
        // if we get here, it's because the driver couldn't be initialized and the problem has
        // been logged.
        return 0;
    }

#if FILAMENT_ENABLE_MATDBG
    #ifdef __ANDROID__
        const char* portString = "8081";
    #else
        const char* portString = getenv("FILAMENT_MATDBG_PORT");
    #endif
    if (portString != nullptr) {
        const int port = atoi(portString);
        debug.server = new matdbg::DebugServer(mBackend,
                mDriver->getShaderLanguages(ShaderLanguage::UNSPECIFIED).front(),
                matdbg::DbgShaderModel((uint8_t) mDriver->getShaderModel()), port);

        // Sometimes the server can fail to spin up (e.g. if the above port is already in use).
        // When this occurs, carry onward, developers can look at civetweb.txt for details.
        if (!debug.server->isReady()) {
            delete debug.server;
            debug.server = nullptr;
        } else {
            debug.server->setEditCallback(FMaterial::onEditCallback);
            debug.server->setQueryCallback(FMaterial::onQueryCallback);
        }
    }
#endif

#if FILAMENT_ENABLE_FGVIEWER // NOLINT(*-include-cleaner)
#ifdef __ANDROID__
    const char* fgviewerPortString = "8085";
#else
    const char* fgviewerPortString = getenv("FILAMENT_FGVIEWER_PORT");
#endif
    if (fgviewerPortString != nullptr) {
        const int fgviewerPort = atoi(fgviewerPortString);
        debug.fgviewerServer = new fgviewer::DebugServer(fgviewerPort);

        // Sometimes the server can fail to spin up (e.g. if the above port is already in use).
        // When this occurs, carry onward, developers can look at civetweb.txt for details.
        if (!debug.fgviewerServer->isReady()) {
            delete debug.fgviewerServer;
            debug.fgviewerServer = nullptr;
        }
    }
#endif

    while (true) {
        if (!execute()) {
            break;
        }
    }

#if FILAMENT_ENABLE_MATDBG
    if(debug.server) {
        delete debug.server;
    }
#endif
#if FILAMENT_ENABLE_FGVIEWER
    if(debug.fgviewerServer) {
        delete debug.fgviewerServer;
    }
#endif

    // terminate() is a synchronous API
    getDriverApi().terminate();
    return 0;
}

/**
 * 刷新命令缓冲区
 * 
 * 刷新命令缓冲区队列并清理驱动。
 * 
 * @param commandBufferQueue 命令缓冲区队列引用
 */
void FEngine::flushCommandBuffer(CommandBufferQueue& commandBufferQueue) const {
    getDriver().purge();
    commandBufferQueue.flush();
}

/**
 * 获取天空盒材质
 * 
 * 返回天空盒材质。如果尚未创建，则延迟创建。
 * 
 * @return 天空盒材质指针
 */
const FMaterial* FEngine::getSkyboxMaterial() const noexcept {
    FMaterial const* material = mSkyboxMaterial;
    if (UTILS_UNLIKELY(material == nullptr)) {
        material = FSkybox::createMaterial(*const_cast<FEngine*>(this));
        mSkyboxMaterial = material;
    }
    return material;
}

// -----------------------------------------------------------------------------------------------
// Resource management
// -----------------------------------------------------------------------------------------------

/*
 * Object created from a Builder
 * 从构建器创建的对象
 */

/**
 * 通用资源创建模板方法
 * 
 * 使用构建器创建资源对象并添加到资源列表。
 * 
 * @tparam T 资源类型
 * @tparam ARGS 额外参数类型
 * @param list 资源列表，用于跟踪创建的资源
 * @param builder 构建器对象
 * @param args 额外参数（转发给构造函数）
 * @return 创建的资源指针，如果创建失败返回 nullptr
 */
template<typename T, typename ... ARGS>
T* FEngine::create(ResourceList<T>& list,
        typename T::Builder const& builder, ARGS&& ... args) noexcept {
    // 使用堆分配器创建对象
    T* p = mHeapAllocator.make<T>(*this, builder, std::forward<ARGS>(args)...);
    if (UTILS_LIKELY(p)) {
        // 创建成功，添加到资源列表
        list.insert(p);
    }
    return p;
}

/**
 * 创建缓冲区对象
 * 
 * @param builder 缓冲区对象构建器
 * @return 创建的缓冲区对象指针
 */
FBufferObject* FEngine::createBufferObject(const BufferObject::Builder& builder) noexcept {
    return create(mBufferObjects, builder);
}

/**
 * 创建顶点缓冲区
 * 
 * @param builder 顶点缓冲区构建器
 * @return 创建的顶点缓冲区指针
 */
FVertexBuffer* FEngine::createVertexBuffer(const VertexBuffer::Builder& builder) noexcept {
    return create(mVertexBuffers, builder);
}

/**
 * 创建索引缓冲区
 * 
 * @param builder 索引缓冲区构建器
 * @return 创建的索引缓冲区指针
 */
FIndexBuffer* FEngine::createIndexBuffer(const IndexBuffer::Builder& builder) noexcept {
    return create(mIndexBuffers, builder);
}

/**
 * 创建蒙皮缓冲区
 * 
 * @param builder 蒙皮缓冲区构建器
 * @return 创建的蒙皮缓冲区指针
 */
FSkinningBuffer* FEngine::createSkinningBuffer(const SkinningBuffer::Builder& builder) noexcept {
    return create(mSkinningBuffers, builder);
}

/**
 * 创建变形目标缓冲区
 * 
 * @param builder 变形目标缓冲区构建器
 * @return 创建的变形目标缓冲区指针
 */
FMorphTargetBuffer* FEngine::createMorphTargetBuffer(const MorphTargetBuffer::Builder& builder) noexcept {
    return create(mMorphTargetBuffers, builder);
}

/**
 * 创建实例缓冲区
 * 
 * @param builder 实例缓冲区构建器
 * @return 创建的实例缓冲区指针
 */
FInstanceBuffer* FEngine::createInstanceBuffer(const InstanceBuffer::Builder& builder) noexcept {
    return create(mInstanceBuffers, builder);
}

/**
 * 创建纹理
 * 
 * @param builder 纹理构建器
 * @return 创建的纹理指针
 */
FTexture* FEngine::createTexture(const Texture::Builder& builder) noexcept {
    return create(mTextures, builder);
}

/**
 * 创建间接光
 * 
 * @param builder 间接光构建器
 * @return 创建的间接光指针
 */
FIndirectLight* FEngine::createIndirectLight(const IndirectLight::Builder& builder) noexcept {
    return create(mIndirectLights, builder);
}

/**
 * 创建材质
 * 
 * @param builder 材质构建器
 * @param definition 材质定义
 * @return 创建的材质指针
 */
FMaterial* FEngine::createMaterial(const Material::Builder& builder,
        MaterialDefinition const& definition) noexcept {
    return create(mMaterials, builder, definition);
}

/**
 * 创建天空盒
 * 
 * @param builder 天空盒构建器
 * @return 创建的天空盒指针
 */
FSkybox* FEngine::createSkybox(const Skybox::Builder& builder) noexcept {
    return create(mSkyboxes, builder);
}

/**
 * 创建颜色分级
 * 
 * @param builder 颜色分级构建器
 * @return 创建的颜色分级指针
 */
FColorGrading* FEngine::createColorGrading(const ColorGrading::Builder& builder) noexcept {
    return create(mColorGradings, builder);
}

/**
 * 创建流
 * 
 * @param builder 流构建器
 * @return 创建的流指针
 */
FStream* FEngine::createStream(const Stream::Builder& builder) noexcept {
    return create(mStreams, builder);
}

/**
 * 创建渲染目标
 * 
 * @param builder 渲染目标构建器
 * @return 创建的渲染目标指针
 */
FRenderTarget* FEngine::createRenderTarget(const RenderTarget::Builder& builder) noexcept {
    return create(mRenderTargets, builder);
}

/*
 * Special cases
 * 特殊情况
 */

/**
 * 创建渲染器
 * 
 * 渲染器不使用构建器，直接创建。
 * 
 * @return 创建的渲染器指针
 */
FRenderer* FEngine::createRenderer() noexcept {
    FRenderer* p = mHeapAllocator.make<FRenderer>(*this);
    if (UTILS_LIKELY(p)) {
        mRenderers.insert(p);
    }
    return p;
}

/**
 * 创建材质实例（从另一个实例复制）
 * 
 * @param material 材质指针
 * @param other 要复制的材质实例
 * @param name 材质实例名称（可选）
 * @return 创建的材质实例指针
 */
FMaterialInstance* FEngine::createMaterialInstance(const FMaterial* material,
        const FMaterialInstance* other, const char* name) noexcept {
    FMaterialInstance* p = mHeapAllocator.make<FMaterialInstance>(*this, other, name);
    if (UTILS_LIKELY(p)) {
        // 将材质实例添加到对应材质的实例列表中
        auto const pos = mMaterialInstances.emplace(material, "MaterialInstance");
        pos.first->second.insert(p);
    }
    return p;
}

/**
 * 创建材质实例（新建）
 * 
 * @param material 材质指针
 * @param name 材质实例名称（可选）
 * @return 创建的材质实例指针
 */
FMaterialInstance* FEngine::createMaterialInstance(const FMaterial* material,
        const char* name) noexcept {
    FMaterialInstance* p = mHeapAllocator.make<FMaterialInstance>(*this, material, name);
    if (UTILS_LIKELY(p)) {
        // 将材质实例添加到对应材质的实例列表中
        auto pos = mMaterialInstances.emplace(material, "MaterialInstance");
        pos.first->second.insert(p);
    }
    return p;
}

/*
 * Objects created without a Builder
 * 不使用构建器创建的对象
 */

/**
 * 创建场景
 * 
 * @return 创建的场景指针
 */
FScene* FEngine::createScene() noexcept {
    FScene* p = mHeapAllocator.make<FScene>(*this);
    if (UTILS_LIKELY(p)) {
        mScenes.insert(p);
    }
    return p;
}

/**
 * 创建视图
 * 
 * @return 创建的视图指针
 */
FView* FEngine::createView() noexcept {
    FView* p = mHeapAllocator.make<FView>(*this);
    if (UTILS_LIKELY(p)) {
        mViews.insert(p);
    }
    return p;
}

/**
 * 创建栅栏
 * 
 * 栅栏用于同步 GPU 命令执行。
 * 
 * @return 创建的栅栏指针
 */
FFence* FEngine::createFence() noexcept {
    FFence* p = mHeapAllocator.make<FFence>(*this);
    if (UTILS_LIKELY(p)) {
        // 栅栏列表需要锁保护（可能从不同线程访问）
        std::lock_guard const guard(mFenceListLock);
        mFences.insert(p);
    }
    return p;
}

/**
 * 创建交换链（从原生窗口）
 * 
 * @param nativeWindow 原生窗口句柄
 * @param flags 交换链标志
 * @return 创建的交换链指针
 */
FSwapChain* FEngine::createSwapChain(void* nativeWindow, uint64_t flags) noexcept {
    // 如果设置了 Apple CVPixelBuffer 标志，设置外部图像
    if (UTILS_UNLIKELY(flags & backend::SWAP_CHAIN_CONFIG_APPLE_CVPIXELBUFFER)) {
        // If this flag is set, then the nativeWindow is a CVPixelBufferRef.
        // The call to setupExternalImage is synchronous, and allows the driver to take ownership of
        // the buffer on this thread.
        // For non-Metal backends, this is a no-op.
        // 如果设置了此标志，则 nativeWindow 是 CVPixelBufferRef。
        // setupExternalImage 调用是同步的，允许驱动在此线程上获取缓冲区的所有权。
        // 对于非 Metal 后端，这是空操作。
        getDriverApi().setupExternalImage(nativeWindow);
    }
    FSwapChain* p = mHeapAllocator.make<FSwapChain>(*this, nativeWindow, flags);
    if (UTILS_LIKELY(p)) {
        mSwapChains.insert(p);
    }
    return p;
}

/**
 * 创建交换链（指定尺寸）
 * 
 * @param width 交换链宽度
 * @param height 交换链高度
 * @param flags 交换链标志
 * @return 创建的交换链指针
 */
FSwapChain* FEngine::createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept {
    FSwapChain* p = mHeapAllocator.make<FSwapChain>(*this, width, height, flags);
    if (UTILS_LIKELY(p)) {
        mSwapChains.insert(p);
    }
    return p;
}

/**
 * 创建同步对象
 * 
 * 同步对象用于跨线程同步。
 * 
 * @return 创建的同步对象指针
 */
FSync* FEngine::createSync() noexcept {
    FSync* p = mHeapAllocator.make<FSync>(*this);
    if (UTILS_LIKELY(p)) {
        // 同步对象列表需要锁保护（可能从不同线程访问）
        std::lock_guard const guard(mSyncListLock);
        mSyncs.insert(p);
    }
    return p;
}

/*
 * Objects created with a component manager
 * 使用组件管理器创建的对象
 */

/**
 * 创建摄像机组件
 * 
 * @param entity 实体 ID
 * @return 创建的摄像机指针
 */
FCamera* FEngine::createCamera(Entity const entity) noexcept {
    return mCameraManager.create(*this, entity);
}

/**
 * 获取摄像机组件
 * 
 * @param entity 实体 ID
 * @return 摄像机指针，如果实体没有摄像机组件返回 nullptr
 */
FCamera* FEngine::getCameraComponent(Entity const entity) noexcept {
    auto const ci = mCameraManager.getInstance(entity);
    return ci ? mCameraManager.getCamera(ci) : nullptr;
}

/**
 * 销毁摄像机组件
 * 
 * @param entity 实体 ID
 */
void FEngine::destroyCameraComponent(Entity const entity) noexcept {
    mCameraManager.destroy(*this, entity);
}

/**
 * 创建可渲染组件
 * 
 * 如果实体没有变换组件，会自动创建一个。
 * 
 * @param builder 可渲染组件构建器
 * @param entity 实体 ID
 */
void FEngine::createRenderable(const RenderableManager::Builder& builder, Entity const entity) {
    mRenderableManager.create(builder, entity);
    auto& tcm = mTransformManager;
    // if this entity doesn't have a transform component, add one.
    // 如果此实体没有变换组件，添加一个。
    if (!tcm.hasComponent(entity)) {
        // 创建默认变换组件（单位矩阵）
        tcm.create(entity, 0, mat4f());
    }
}

/**
 * 创建光源组件
 * 
 * @param builder 光源组件构建器
 * @param entity 实体 ID
 */
void FEngine::createLight(const LightManager::Builder& builder, Entity const entity) {
    mLightManager.create(builder, entity);
}

// -----------------------------------------------------------------------------------------------

/**
 * 清理资源列表
 * 
 * 对列表中的每个资源调用 terminate() 并销毁。
 * 用于清理"泄漏"的资源（用户忘记销毁的资源）。
 * 
 * @tparam T 资源类型
 * @param list 资源列表（移动语义）
 */
template<typename T>
UTILS_NOINLINE
void FEngine::cleanupResourceList(ResourceList<T>&& list) {
    if (UTILS_UNLIKELY(!list.empty())) {
#ifndef NDEBUG
        // 调试模式下记录泄漏的资源数量
        DLOG(INFO) << "cleaning up " << list.size() << " leaked "
                   << CallStack::typeName<T>().c_str();
#endif
        // 遍历列表，终止并销毁每个资源
        list.forEach([this, &allocator = mHeapAllocator](T* item) {
            item->terminate(*this);
            allocator.destroy(item);
        });
        list.clear();
    }
}

/**
 * 清理资源列表（带锁保护）
 * 
 * 在持有锁的情况下复制列表，然后正常清理。
 * 用于需要线程安全的资源列表。
 * 
 * @tparam T 资源类型
 * @tparam Lock 锁类型
 * @param lock 锁对象
 * @param list 资源列表（移动语义）
 */
template<typename T, typename Lock>
UTILS_NOINLINE
void FEngine::cleanupResourceListLocked(Lock& lock, ResourceList<T>&& list) {
    // copy the list with the lock held, then proceed as usual
    // 在持有锁的情况下复制列表，然后正常处理
    lock.lock();
    auto copy(std::move(list));
    lock.unlock();
    cleanupResourceList(std::move(copy));
}

// -----------------------------------------------------------------------------------------------

/**
 * 验证资源指针是否有效
 * 
 * 检查指针是否在资源列表中。
 * 
 * @tparam T 资源类型
 * @param ptr 资源指针
 * @param list 资源列表
 * @return 如果指针有效返回 true，否则返回 false
 */
template<typename T>
UTILS_ALWAYS_INLINE
bool FEngine::isValid(const T* ptr, ResourceList<T> const& list) const {
    auto& l = const_cast<ResourceList<T>&>(list);
    return l.find(ptr) != l.end();
}

/**
 * 终止并销毁资源
 * 
 * 从资源列表中移除资源，调用 terminate()，然后销毁。
 * 
 * @tparam T 资源类型
 * @param p 资源指针
 * @param list 资源列表
 * @return 如果成功销毁返回 true，如果指针无效或资源不存在返回 false
 */
template<typename T>
UTILS_ALWAYS_INLINE
bool FEngine::terminateAndDestroy(const T* p, ResourceList<T>& list) {
    if (p == nullptr) return true;
    // 从列表中移除资源
    bool const success = list.remove(p);

#if UTILS_HAS_RTTI
    // 获取类型名称（用于错误消息）
    auto typeName = CallStack::typeName<T>();
    const char * const typeNameCStr = typeName.c_str();
#else
    const char * const typeNameCStr = "<no-rtti>";
#endif

    // 如果成功移除，终止并销毁资源
    if (ASSERT_PRECONDITION_NON_FATAL(success,
            "Object %s at %p doesn't exist (double free?)", typeNameCStr, p)) {
        const_cast<T*>(p)->terminate(*this);
        mHeapAllocator.destroy(const_cast<T*>(p));
    }
    return success;
}

/**
 * 终止并销毁资源（带锁保护）
 * 
 * 在持有锁的情况下从资源列表中移除资源，调用 terminate()，然后销毁。
 * 
 * @tparam T 资源类型
 * @tparam Lock 锁类型
 * @param lock 锁对象
 * @param p 资源指针
 * @param list 资源列表
 * @return 如果成功销毁返回 true，如果指针无效或资源不存在返回 false
 */
template<typename T, typename Lock>
UTILS_ALWAYS_INLINE
bool FEngine::terminateAndDestroyLocked(Lock& lock, const T* p, ResourceList<T>& list) {
    if (p == nullptr) return true;
    // 在持有锁的情况下从列表中移除资源
    lock.lock();
    bool const success = list.remove(p);
    lock.unlock();

#if UTILS_HAS_RTTI
    // 获取类型名称（用于错误消息）
    auto typeName = CallStack::typeName<T>();
    const char * const typeNameCStr = typeName.c_str();
#else
    const char * const typeNameCStr = "<no-rtti>";
#endif

    // 如果成功移除，终止并销毁资源
    if (ASSERT_PRECONDITION_NON_FATAL(success,
            "Object %s at %p doesn't exist (double free?)", typeNameCStr, p)) {
        const_cast<T*>(p)->terminate(*this);
        mHeapAllocator.destroy(const_cast<T*>(p));
    }
    return success;
}

// -----------------------------------------------------------------------------------------------

UTILS_NOINLINE
bool FEngine::destroy(const FBufferObject* p) {
    return terminateAndDestroy(p, mBufferObjects);
}

UTILS_NOINLINE
bool FEngine::destroy(const FVertexBuffer* p) {
    return terminateAndDestroy(p, mVertexBuffers);
}

UTILS_NOINLINE
bool FEngine::destroy(const FIndexBuffer* p) {
    return terminateAndDestroy(p, mIndexBuffers);
}

UTILS_NOINLINE
bool FEngine::destroy(const FSkinningBuffer* p) {
    return terminateAndDestroy(p, mSkinningBuffers);
}

UTILS_NOINLINE
bool FEngine::destroy(const FMorphTargetBuffer* p) {
    return terminateAndDestroy(p, mMorphTargetBuffers);
}

UTILS_NOINLINE
bool FEngine::destroy(const FRenderer* p) {
    return terminateAndDestroy(p, mRenderers);
}

UTILS_NOINLINE
bool FEngine::destroy(const FScene* p) {
    return terminateAndDestroy(p, mScenes);
}

UTILS_NOINLINE
bool FEngine::destroy(const FSkybox* p) {
    return terminateAndDestroy(p, mSkyboxes);
}

UTILS_NOINLINE
bool FEngine::destroy(const FColorGrading* p) {
    return terminateAndDestroy(p, mColorGradings);
}

UTILS_NOINLINE
bool FEngine::destroy(const FTexture* p) {
    return terminateAndDestroy(p, mTextures);
}

UTILS_NOINLINE
bool FEngine::destroy(const FRenderTarget* p) {
    return terminateAndDestroy(p, mRenderTargets);
}

UTILS_NOINLINE
bool FEngine::destroy(const FView* p) {
    return terminateAndDestroy(p, mViews);
}

UTILS_NOINLINE
bool FEngine::destroy(const FIndirectLight* p) {
    return terminateAndDestroy(p, mIndirectLights);
}

UTILS_NOINLINE
bool FEngine::destroy(const FFence* p) {
    return terminateAndDestroyLocked(mFenceListLock, p, mFences);
}

UTILS_NOINLINE
bool FEngine::destroy(const FSwapChain* p) {
    return terminateAndDestroy(p, mSwapChains);
}

UTILS_NOINLINE
bool FEngine::destroy(const FSync* p) {
    return terminateAndDestroyLocked(mSyncListLock, p, mSyncs);
}

UTILS_NOINLINE
bool FEngine::destroy(const FStream* p) {
    return terminateAndDestroy(p, mStreams);
}

UTILS_NOINLINE
bool FEngine::destroy(const FInstanceBuffer* p){
    return terminateAndDestroy(p, mInstanceBuffers);
}

/**
 * 销毁材质
 * 
 * 销毁材质并清理其所有材质实例。
 * 
 * @param p 材质指针
 * @return 如果成功销毁返回 true，否则返回 false
 */
UTILS_NOINLINE
bool FEngine::destroy(const FMaterial* p) {
    if (p == nullptr) return true;
    // 销毁材质
    bool const success = terminateAndDestroy(p, mMaterials);
    if (UTILS_LIKELY(success)) {
        // 清理该材质的所有材质实例
        auto const pos = mMaterialInstances.find(p);
        if (UTILS_LIKELY(pos != mMaterialInstances.cend())) {
            mMaterialInstances.erase(pos);
        }
    }
    return success;
}

/**
 * 销毁材质实例
 * 
 * 在销毁前检查材质实例是否仍在使用中（被可渲染对象使用）。
 * 如果仍在使用中且启用了断言，会触发断言。
 * 
 * @param p 材质实例指针
 * @return 如果成功销毁返回 true，如果是默认实例或销毁失败返回 false
 */
UTILS_NOINLINE
bool FEngine::destroy(const FMaterialInstance* p) {
    if (p == nullptr) return true;

    // Check that the material instance we're destroying is not in use in the RenderableManager
    // To do this, we currently need to inspect all render primitives in the RenderableManager
    // 检查我们要销毁的材质实例是否仍在 RenderableManager 中使用
    // 为此，我们需要检查 RenderableManager 中的所有渲染图元
    EntityManager const& em = mEntityManager;
    FRenderableManager const& rcm = mRenderableManager;
    Entity const* entities = rcm.getEntities();
    size_t const count = rcm.getComponentCount();
    // 遍历所有可渲染实体
    for (size_t i = 0; i < count; i++) {
        Entity const entity = entities[i];
        if (em.isAlive(entity)) {
            RenderableManager::Instance const ri = rcm.getInstance(entity);
            size_t const primitiveCount = rcm.getPrimitiveCount(ri, 0);
            // 检查每个图元的材质实例
            for (size_t j = 0; j < primitiveCount; j++) {
                auto const* const mi = rcm.getMaterialInstanceAt(ri, 0, j);
                auto const& featureFlags = features.engine.debug;
                // 如果材质实例正在使用中，触发断言（如果启用）
                FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION(
                        mi != p,
                        featureFlags.assert_material_instance_in_use)
                        << "destroying MaterialInstance \"" << mi->getName()
                        << "\" which is still in use by Renderable (entity=" << entity.getId()
                        << ", instance=" << ri.asValue() << ", index=" << j << ")";
            }
        }
    }

    // 不能销毁默认材质实例
    if (p->isDefaultInstance()) return false;
    // 从对应材质的实例列表中移除并销毁
    auto const pos = mMaterialInstances.find(p->getMaterial());
    assert_invariant(pos != mMaterialInstances.cend());
    if (pos != mMaterialInstances.cend()) {
        return terminateAndDestroy(p, pos->second);
    }
    // this shouldn't happen, this would be double-free
    // 这不应该发生，这将是双重释放
    return false;
}

/**
 * 销毁实体
 * 
 * 销毁实体的所有组件（可渲染、光源、变换、摄像机）。
 * 
 * @param e 实体 ID
 */
UTILS_NOINLINE
void FEngine::destroy(Entity const e) {
    mRenderableManager.destroy(e);
    mLightManager.destroy(e);
    mTransformManager.destroy(e);
    mCameraManager.destroy(*this, e);
}

bool FEngine::isValid(const FBufferObject* p) const {
    return isValid(p, mBufferObjects);
}

bool FEngine::isValid(const FVertexBuffer* p) const {
    return isValid(p, mVertexBuffers);
}

bool FEngine::isValid(const FFence* p) const {
    return isValid(p, mFences);
}

bool FEngine::isValid(const FSync* p) const {
    return isValid(p, mSyncs);
}

bool FEngine::isValid(const FIndexBuffer* p) const {
    return isValid(p, mIndexBuffers);
}

bool FEngine::isValid(const FSkinningBuffer* p) const {
    return isValid(p, mSkinningBuffers);
}

bool FEngine::isValid(const FMorphTargetBuffer* p) const {
    return isValid(p, mMorphTargetBuffers);
}

bool FEngine::isValid(const FIndirectLight* p) const {
    return isValid(p, mIndirectLights);
}

bool FEngine::isValid(const FMaterial* p) const {
    return isValid(p, mMaterials);
}

/**
 * 验证材质实例是否有效（给定材质）
 * 
 * 首先验证材质是否有效，然后验证材质实例是否属于该材质。
 * 
 * @param m 材质指针
 * @param p 材质实例指针
 * @return 如果材质和材质实例都有效且材质实例属于该材质返回 true，否则返回 false
 */
bool FEngine::isValid(const FMaterial* m, const FMaterialInstance* p) const {
    // first make sure the material we're given is valid.
    // 首先确保给定的材质有效
    if (!isValid(m)) {
        return false;
    }

    // then find the material instance list for that material
    // 然后查找该材质的材质实例列表
    auto const it = mMaterialInstances.find(m);
    if (it == mMaterialInstances.end()) {
        // this could happen if this material has no material instances at all
        // 如果该材质没有任何材质实例，可能发生这种情况
        return false;
    }

    // finally validate the material instance
    // 最后验证材质实例
    return isValid(p, it->second);
}

/**
 * 验证材质实例是否有效（昂贵版本）
 * 
 * 在所有材质的实例列表中搜索材质实例。
 * 这是一个昂贵的操作，因为需要遍历所有材质。
 * 
 * @param p 材质实例指针
 * @return 如果材质实例有效返回 true，否则返回 false
 */
bool FEngine::isValidExpensive(const FMaterialInstance* p) const {
    // 在所有材质的实例列表中搜索
    return std::any_of(mMaterialInstances.cbegin(), mMaterialInstances.cend(),
            [this, p](auto&& entry) {
        return isValid(p, entry.second);
    });
}

bool FEngine::isValid(const FRenderer* p) const {
    return isValid(p, mRenderers);
}

bool FEngine::isValid(const FScene* p) const {
    return isValid(p, mScenes);
}

bool FEngine::isValid(const FSkybox* p) const {
    return isValid(p, mSkyboxes);
}

bool FEngine::isValid(const FColorGrading* p) const {
    return isValid(p, mColorGradings);
}

bool FEngine::isValid(const FSwapChain* p) const {
    return isValid(p, mSwapChains);
}

bool FEngine::isValid(const FStream* p) const {
    return isValid(p, mStreams);
}

bool FEngine::isValid(const FTexture* p) const {
    return isValid(p, mTextures);
}

bool FEngine::isValid(const FRenderTarget* p) const {
    return isValid(p, mRenderTargets);
}

bool FEngine::isValid(const FView* p) const {
    return isValid(p, mViews);
}

bool FEngine::isValid(const FInstanceBuffer* p) const {
    return isValid(p, mInstanceBuffers);
}

size_t FEngine::getBufferObjectCount() const noexcept { return mBufferObjects.size(); }
size_t FEngine::getViewCount() const noexcept { return mViews.size(); }
size_t FEngine::getSceneCount() const noexcept { return mScenes.size(); }
size_t FEngine::getSwapChainCount() const noexcept { return mSwapChains.size(); }
size_t FEngine::getStreamCount() const noexcept { return mStreams.size(); }
size_t FEngine::getIndexBufferCount() const noexcept { return mIndexBuffers.size(); }
size_t FEngine::getSkinningBufferCount() const noexcept { return mSkinningBuffers.size(); }
size_t FEngine::getMorphTargetBufferCount() const noexcept { return mMorphTargetBuffers.size(); }
size_t FEngine::getInstanceBufferCount() const noexcept { return mInstanceBuffers.size(); }
size_t FEngine::getVertexBufferCount() const noexcept { return mVertexBuffers.size(); }
size_t FEngine::getIndirectLightCount() const noexcept { return mIndirectLights.size(); }
size_t FEngine::getMaterialCount() const noexcept { return mMaterials.size(); }
size_t FEngine::getTextureCount() const noexcept { return mTextures.size(); }
size_t FEngine::getSkyboxeCount() const noexcept { return mSkyboxes.size(); }
size_t FEngine::getColorGradingCount() const noexcept { return mColorGradings.size(); }
size_t FEngine::getRenderTargetCount() const noexcept { return mRenderTargets.size(); }

/**
 * 获取最大阴影贴图数量
 * 
 * 根据是否使用阴影图集返回不同的最大值。
 * 
 * @return 最大阴影贴图数量
 */
size_t FEngine::getMaxShadowMapCount() const noexcept {
    return features.engine.shadows.use_shadow_atlas ?
        CONFIG_MAX_SHADOWMAPS : CONFIG_MAX_SHADOW_LAYERS;
}

/**
 * 流分配内存
 * 
 * 从驱动 API 分配临时内存，用于流操作。
 * 只允许小分配（最大 64KB）。
 * 
 * @param size 分配大小（字节）
 * @param alignment 对齐要求（字节）
 * @return 分配的内存指针，如果分配失败或大小超过限制返回 nullptr
 */
void* FEngine::streamAlloc(size_t const size, size_t const alignment) noexcept {
    // we allow this only for small allocations
    // 我们只允许小分配
    if (size > 65536) {
        return nullptr;
    }
    return getDriverApi().allocate(size, alignment);
}

/**
 * 执行命令缓冲区
 * 
 * 等待并执行命令缓冲区队列中的命令。
 * 在 Driver 线程循环中调用。
 * 
 * @return 如果成功执行返回 true，如果线程退出请求返回 false
 */
bool FEngine::execute() {
    // wait until we get command buffers to be executed (or thread exit requested)
    // 等待直到获取要执行的命令缓冲区（或线程退出请求）
    auto const buffers = mCommandBufferQueue.waitForCommands();
    if (UTILS_UNLIKELY(buffers.empty())) {
        return false;  // 队列为空表示线程退出请求
    }

    // execute all command buffers
    // 执行所有命令缓冲区
    auto& driver = getDriverApi();
    for (auto& item : buffers) {
        if (UTILS_LIKELY(item.begin)) {
            driver.execute(item.begin);  // 执行命令
            mCommandBufferQueue.releaseBuffer(item);  // 释放缓冲区
        }
    }

    return true;  // 成功执行
}

/**
 * 销毁 Engine 实例
 * 
 * 静态方法，用于正确关闭和销毁 Engine。
 * 
 * @param engine Engine 指针
 */
void FEngine::destroy(FEngine* engine) {
    if (engine) {
        engine->shutdown();
        delete engine;
    }
}

/**
 * 检查 Engine 是否暂停
 * 
 * @return 如果 Engine 已暂停返回 true，否则返回 false
 */
bool FEngine::isPaused() const noexcept {
    return mCommandBufferQueue.isPaused();
}

/**
 * 设置 Engine 暂停状态
 * 
 * @param paused 是否暂停
 */
void FEngine::setPaused(bool const paused) {
    mCommandBufferQueue.setPaused(paused);
}

/**
 * 获取支持的特性级别
 * 
 * 返回驱动支持的最高特性级别。
 * 
 * @return 支持的特性级别
 */
Engine::FeatureLevel FEngine::getSupportedFeatureLevel() const noexcept {
    DriverApi& driver = const_cast<FEngine*>(this)->getDriverApi();
    return driver.getFeatureLevel();
}

/**
 * 设置激活的特性级别
 * 
 * 设置 Engine 使用的特性级别。不能超过驱动支持的特性级别，
 * 且不能在运行时从特性级别 0 调整。
 * 
 * @param featureLevel 要设置的特性级别
 * @return 实际设置的特性级别（取请求值和当前值的较大者）
 */
Engine::FeatureLevel FEngine::setActiveFeatureLevel(FeatureLevel featureLevel) {
    FILAMENT_CHECK_PRECONDITION(featureLevel <= getSupportedFeatureLevel())
            << "Feature level " << unsigned(featureLevel) << " not supported";
    FILAMENT_CHECK_PRECONDITION(mActiveFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1)
            << "Cannot adjust feature level beyond 0 at runtime";
    return (mActiveFeatureLevel = std::max(mActiveFeatureLevel, featureLevel));
}

/**
 * 检查是否支持异步操作
 * 
 * @return 如果支持异步操作返回 true，否则返回 false
 */
bool FEngine::isAsynchronousOperationSupported() const noexcept {
    return features.backend.enable_asynchronous_operation &&
        mConfig.asynchronousMode != AsynchronousMode::NONE;
}

#if defined(__EMSCRIPTEN__)
/**
 * 重置后端状态
 * 
 * 仅在 Emscripten 平台可用。
 * 重置后端到初始状态。
 */
void FEngine::resetBackendState() noexcept {
    getDriverApi().resetState();
}
#endif

/**
 * 切换到未保护上下文
 * 
 * 创建或使用虚拟交换链切换到未保护上下文。
 * 用于某些需要未保护上下文的后端操作。
 */
void FEngine::unprotected() noexcept {
    if (UTILS_UNLIKELY(!mUnprotectedDummySwapchain)) {
        // 创建虚拟交换链（1x1 像素）
        mUnprotectedDummySwapchain = createSwapChain(1, 1, 0);
    }
    // 将虚拟交换链设为当前上下文
    mUnprotectedDummySwapchain->makeCurrent(getDriverApi());
}

/**
 * 设置特性标志
 * 
 * 设置指定名称的特性标志值。
 * 
 * @param name 特性标志名称
 * @param value 要设置的值
 * @return 如果成功设置返回 true，如果特性不存在或为常量返回 false
 */
bool FEngine::setFeatureFlag(char const* name, bool const value) const noexcept {
    auto* const p = getFeatureFlagPtr(name);
    if (p) {
        *p = value;
    }
    return p != nullptr;
}

/**
 * 获取特性标志
 * 
 * 获取指定名称的特性标志值。
 * 
 * @param name 特性标志名称
 * @return 如果特性存在返回其值，否则返回 std::nullopt
 */
std::optional<bool> FEngine::getFeatureFlag(char const* name) const noexcept {
    auto* const p = getFeatureFlagPtr(name, true);
    if (p) {
        return *p;
    }
    return std::nullopt;
}

/**
 * 获取特性标志指针
 * 
 * 获取指定名称的特性标志的指针。用于直接访问特性标志值。
 * 
 * @param name 特性标志名称
 * @param allowConstant 是否允许返回常量特性标志的指针
 * @return 如果特性存在返回其指针，否则返回 nullptr
 */
bool* FEngine::getFeatureFlagPtr(std::string_view name, bool const allowConstant) const noexcept {
    auto pos = std::find_if(mFeatures.begin(), mFeatures.end(),
            [name](FeatureFlag const& entry) {
                return name == entry.name;
            });

    return (pos != mFeatures.end() && (!pos->constant || allowConstant)) ?
           const_cast<bool*>(pos->value) : nullptr;
}

// ------------------------------------------------------------------------------------------------

// Engine::Builder 的默认构造函数和析构函数
Engine::Builder::Builder() noexcept = default;
Engine::Builder::~Builder() noexcept = default;
Engine::Builder::Builder(Builder const& rhs) noexcept = default;
Engine::Builder::Builder(Builder&& rhs) noexcept = default;
Engine::Builder& Engine::Builder::operator=(Builder const& rhs) noexcept = default;
Engine::Builder& Engine::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置后端类型
 * 
 * @param backend 后端类型（OpenGL、Vulkan、Metal 等）
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::backend(Backend const backend) noexcept {
    mImpl->mBackend = backend;
    return *this;
}

/**
 * 设置平台对象
 * 
 * @param platform 平台指针，如果为 nullptr 将创建默认平台
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::platform(Platform* platform) noexcept {
    mImpl->mPlatform = platform;
    return *this;
}

/**
 * 设置配置
 * 
 * @param config 配置指针，如果为 nullptr 使用默认配置
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::config(Config const* config) noexcept {
    mImpl->mConfig = config ? *config : Config{};
    return *this;
}

/**
 * 设置特性级别
 * 
 * @param featureLevel 特性级别
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::featureLevel(FeatureLevel const featureLevel) noexcept {
    mImpl->mFeatureLevel = featureLevel;
    return *this;
}

/**
 * 设置共享上下文
 * 
 * 用于多线程渲染场景，允许在不同线程间共享 OpenGL 上下文。
 * 
 * @param sharedContext 共享上下文指针
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::sharedContext(void* sharedContext) noexcept {
    mImpl->mSharedContext = sharedContext;
    return *this;
}

/**
 * 设置暂停状态
 * 
 * 如果设置为 true，Engine 创建后不会立即开始处理命令。
 * 
 * @param paused 是否暂停
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::paused(bool const paused) noexcept {
    mImpl->mPaused = paused;
    return *this;
}

/**
 * 设置特性标志
 * 
 * @param name 特性标志名称
 * @param value 特性标志值
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::feature(char const* name, bool const value) noexcept {
    mImpl->mFeatureFlags.emplace(name, value);
    return *this;
}

/**
 * 批量设置特性标志
 * 
 * 将列表中的所有特性标志设置为 true。
 * 
 * @param list 特性标志名称列表
 * @return Builder 引用（支持链式调用）
 */
Engine::Builder& Engine::Builder::features(std::initializer_list<char const *> const list) noexcept {
    for (auto const name : list) {
        if (name) {
            feature(name, true);
        }
    }
    return *this;
}

#if UTILS_HAS_THREADING

/**
 * 异步构建 Engine
 * 
 * 在单独线程中创建 Engine，并通过回调函数通知创建完成。
 * 
 * @param callback 回调函数，参数是创建的 Engine 指针
 */
void Engine::Builder::build(Invocable<void(void*)>&& callback) const {
    FEngine::create(*this, std::move(callback));
}

#endif

/**
 * 构建 Engine
 * 
 * 根据配置创建 Engine 实例。
 * 
 * @return 创建的 Engine 指针，如果创建失败返回 nullptr
 */
Engine* Engine::Builder::build() const {
    // 验证并修正配置
    mImpl->mConfig = BuilderDetails::validateConfig(mImpl->mConfig);
    return FEngine::create(*this);
}

/**
 * 验证并修正配置
 * 
 * 确保所有配置参数在合理范围内，并应用经验规则。
 * 
 * 验证规则：
 * - 最小命令缓冲区大小：至少为构建系统默认值
 * - 每帧命令大小：至少为构建系统默认值
 * - 每渲染通道内存池大小：至少为构建系统默认值，且至少比每帧命令大小大 1MB
 * - 命令缓冲区大小：至少为最小大小的 3 倍（支持 3 个并发帧）
 * - 立体渲染眼睛数量：限制在 1 到最大眼睛数之间
 * 
 * @param config 配置对象
 * @return 验证后的配置对象
 */
Engine::Config Engine::BuilderDetails::validateConfig(Config config) noexcept {
    // Rule of thumb: perRenderPassArenaMB must be roughly 1 MB larger than perFrameCommandsMB
    // 经验规则：每渲染通道内存池大小必须大约比每帧命令大小大 1 MB
    constexpr uint32_t COMMAND_ARENA_OVERHEAD = 1;
    constexpr uint32_t CONCURRENT_FRAME_COUNT = 3;

    // Use at least the defaults set by the build system
    // 至少使用构建系统设置的默认值
    config.minCommandBufferSizeMB = std::max(
            config.minCommandBufferSizeMB,
            uint32_t(FILAMENT_MIN_COMMAND_BUFFERS_SIZE_IN_MB)); // NOLINT(*-include-cleaner)

    config.perFrameCommandsSizeMB = std::max(
            config.perFrameCommandsSizeMB,
            uint32_t(FILAMENT_PER_FRAME_COMMANDS_SIZE_IN_MB)); // NOLINT(*-include-cleaner)

    config.perRenderPassArenaSizeMB = std::max(
            config.perRenderPassArenaSizeMB,
            uint32_t(FILAMENT_PER_RENDER_PASS_ARENA_SIZE_IN_MB)); // NOLINT(*-include-cleaner)

    // 命令缓冲区大小至少为最小大小的 3 倍（支持 3 个并发帧）
    config.commandBufferSizeMB = std::max(
            config.commandBufferSizeMB,
            config.minCommandBufferSizeMB * CONCURRENT_FRAME_COUNT);

    // Enforce pre-render-pass arena rule-of-thumb
    // 强制执行每渲染通道内存池的经验规则
    config.perRenderPassArenaSizeMB = std::max(
            config.perRenderPassArenaSizeMB,
            config.perFrameCommandsSizeMB + COMMAND_ARENA_OVERHEAD);

    // This value gets validated during driver creation, so pass it through
    // 此值在驱动创建时验证，因此直接传递
    config.driverHandleArenaSizeMB = config.driverHandleArenaSizeMB;

    // 限制立体渲染眼睛数量在合理范围内
    config.stereoscopicEyeCount =
            std::clamp(config.stereoscopicEyeCount, uint8_t(1), CONFIG_MAX_STEREOSCOPIC_EYES);

    return config;
}

} // namespace filament
