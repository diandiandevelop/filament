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

#ifndef TNT_FILAMENT_DETAILS_ENGINE_H
#define TNT_FILAMENT_DETAILS_ENGINE_H

#include "downcast.h"

#include "Allocators.h"
#include "DFG.h"
#include "HwDescriptorSetLayoutFactory.h"
#include "HwVertexBufferInfoFactory.h"
#include "MaterialCache.h"
#include "PostProcessManager.h"
#include "ResourceList.h"
#include "UboManager.h"

#include "components/CameraManager.h"
#include "components/LightManager.h"
#include "components/RenderableManager.h"
#include "components/TransformManager.h"

#include "ds/DescriptorSetLayout.h"

#include "details/BufferObject.h"
#include "details/Camera.h"
#include "details/ColorGrading.h"
#include "details/DebugRegistry.h"
#include "details/Fence.h"
#include "details/IndexBuffer.h"
#include "details/InstanceBuffer.h"
#include "details/MorphTargetBuffer.h"
#include "details/RenderTarget.h"
#include "details/SkinningBuffer.h"
#include "details/Skybox.h"
#include "details/Sync.h"

#include "private/backend/CommandBufferQueue.h"
#include "private/backend/CommandStream.h"
#include "private/backend/DriverApi.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/BufferInterfaceBlock.h>

#include <filament/ColorGrading.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/Material.h>
#include <filament/Skybox.h>
#include <filament/Stream.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>

#include <backend/DriverEnums.h>

#include <utils/Allocator.h>
#include <utils/compiler.h>
#include <utils/CountDownLatch.h>
#include <utils/FixedCapacityVector.h>
#include <utils/JobSystem.h>
#include <utils/Slice.h>

#include <array>
#include <chrono>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <random>
#include <thread>
#include <type_traits>
#include <unordered_map>

#if FILAMENT_ENABLE_MATDBG
#include <matdbg/DebugServer.h>
#else
namespace filament::matdbg {
class DebugServer;
using MaterialKey = uint32_t;
} // namespace filament::matdbg
#endif

#if FILAMENT_ENABLE_FGVIEWER
#include <fgviewer/DebugServer.h>
#else
namespace filament::fgviewer {
    class DebugServer;
} // namespace filament::fgviewer
#endif

// We have added correctness assertions that breaks clients' projects. We add this define to allow
// for the client's to address these assertions at a more gradual pace.
#if defined(FILAMENT_RELAXED_CORRECTNESS_ASSERTIONS)
#define CORRECTNESS_ASSERTION_DEFAULT false
#else
#define CORRECTNESS_ASSERTION_DEFAULT true
#endif

namespace filament {

class Renderer;
class MaterialParser;
class ResourceAllocatorDisposer;

namespace backend {
class Driver;
class Program;
} // namespace driver

class FFence;
class FMaterialInstance;
class FRenderer;
class FScene;
class FSwapChain;
class FSync;
class FView;

class ResourceAllocator;

/**
 * Engine 实现类
 * 
 * Engine 接口的具体实现。跟踪给定上下文的所有硬件资源。
 * 
 * Engine 是 Filament 的核心，管理所有渲染资源（材质、纹理、缓冲区等）和组件管理器
 * （RenderableManager、LightManager、TransformManager 等）。
 * 
 * 实现细节：
 * - 使用对齐分配（因为可能包含需要对齐的成员）
 * - 管理驱动 API（后端抽象层）
 * - 管理材质缓存、后处理管理器等子系统
 * - 支持多线程和单线程模式
 */
class FEngine : public Engine {
public:
    /**
     * 对齐分配运算符
     * 
     * 使用对齐分配，因为 FEngine 可能包含需要对齐的成员。
     * 
     * @param size 大小
     * @return 对齐分配的内存指针
     */
    void* operator new(std::size_t const size) noexcept {
        return utils::aligned_alloc(size, alignof(FEngine));
    }

    /**
     * 对齐释放运算符
     * 
     * @param p 内存指针
     */
    void operator delete(void* p) noexcept {
        utils::aligned_free(p);
    }

    using DriverApi = backend::DriverApi;  // 驱动 API 类型别名
    using clock = std::chrono::steady_clock;  // 时钟类型别名
    using Epoch = clock::time_point;  // 纪元类型别名（时间点）
    using duration = clock::duration;  // 持续时间类型别名

public:
    static Engine* create(Builder const& builder);

#if UTILS_HAS_THREADING
    static void create(Builder const& builder, utils::Invocable<void(void* token)>&& callback);
    static FEngine* getEngine(void* token);
#endif

    static void destroy(FEngine* engine);

    ~FEngine() noexcept;

    /**
     * 获取着色器模型
     * 
     * @return 着色器模型（MOBILE 或 DESKTOP）
     */
    backend::ShaderModel getShaderModel() const noexcept { return getDriver().getShaderModel(); }

    /**
     * 获取驱动 API
     * 
     * 使用 std::launder 来避免未定义行为（因为 DriverApi 存储在字节数组中）。
     * 
     * @return 驱动 API 引用
     */
    DriverApi& getDriverApi() noexcept {
        return *std::launder(reinterpret_cast<DriverApi*>(&mDriverApiStorage));
    }

    /**
     * 获取 DFG（分布-菲涅耳-几何）查找表
     * 
     * DFG LUT 用于 IBL（基于图像的光照）计算。
     * 
     * @return DFG 常量引用
     */
    DFG const& getDFG() const noexcept { return mDFG; }

    /**
     * 获取每渲染通道内存池
     * 
     * 每帧的内存池由所有 Renderer 使用，因此它们必须按顺序运行，
     * 并在完成时释放所有分配的内存。如果将来需要更改，只需使用单独的内存池。
     * 
     * @return 每渲染通道内存池引用
     */
    LinearAllocatorArena& getPerRenderPassArena() noexcept { return mPerRenderPassArena; }

    /**
     * 获取材质 ID
     * 
     * 返回唯一的材质 ID（递增）。
     * 
     * @return 材质 ID
     */
    uint32_t getMaterialId() const noexcept { return mMaterialId++; }

    /**
     * 获取默认材质
     * 
     * 返回 Engine 的默认材质实例。默认材质用于未指定材质的渲染对象。
     * 
     * @return 默认材质指针
     */
    const FMaterial* getDefaultMaterial() const noexcept { return mDefaultMaterial; }
    
    /**
     * 获取天空盒材质
     * 
     * 返回用于渲染天空盒的材质实例。
     * 
     * @return 天空盒材质指针
     */
    const FMaterial* getSkyboxMaterial() const noexcept;
    
    /**
     * 获取默认间接光
     * 
     * 返回默认的基于图像的光照（IBL）实例。
     * 
     * @return 默认间接光指针
     */
    const FIndirectLight* getDefaultIndirectLight() const noexcept { return mDefaultIbl; }
    
    /**
     * 获取虚拟立方体贴图
     * 
     * 返回默认的虚拟立方体贴图纹理，用于未指定环境贴图的情况。
     * 
     * @return 虚拟立方体贴图纹理指针
     */
    const FTexture* getDummyCubemap() const noexcept { return mDefaultIblTexture; }
    
    /**
     * 获取默认颜色分级
     * 
     * 返回默认的颜色分级实例。
     * 
     * @return 默认颜色分级指针
     */
    const FColorGrading* getDefaultColorGrading() const noexcept { return mDefaultColorGrading; }
    
    /**
     * 获取虚拟变形目标缓冲区
     * 
     * 返回默认的虚拟变形目标缓冲区，用于未指定变形目标的情况。
     * 
     * @return 虚拟变形目标缓冲区指针
     */
    FMorphTargetBuffer* getDummyMorphTargetBuffer() const { return mDummyMorphTargetBuffer; }

    /**
     * 获取全屏渲染图元
     * 
     * 返回用于全屏后处理的全屏三角形渲染图元句柄。
     * 
     * @return 全屏渲染图元句柄
     */
    backend::Handle<backend::HwRenderPrimitive> getFullScreenRenderPrimitive() const noexcept {
        return mFullScreenTriangleRph;
    }

    /**
     * 获取全屏顶点缓冲区
     * 
     * 返回用于全屏后处理的全屏三角形顶点缓冲区。
     * 
     * @return 全屏顶点缓冲区指针
     */
    FVertexBuffer* getFullScreenVertexBuffer() const noexcept {
        return mFullScreenTriangleVb;
    }

    /**
     * 获取全屏索引缓冲区
     * 
     * 返回用于全屏后处理的全屏三角形索引缓冲区。
     * 
     * @return 全屏索引缓冲区指针
     */
    FIndexBuffer* getFullScreenIndexBuffer() const noexcept {
        return mFullScreenTriangleIb;
    }

    /**
     * 获取从裁剪空间到纹理空间的变换矩阵
     * 
     * 返回将裁剪坐标 [-1, 1] 转换为纹理坐标 [0, 1] 的变换矩阵。
     * 用于后处理效果中需要从裁剪空间采样纹理的情况。
     * 
     * @return 从裁剪空间到纹理空间的变换矩阵
     */
    math::mat4f getUvFromClipMatrix() const noexcept {
        return mUvFromClipMatrix;
    }

    /**
     * 获取支持的特性级别
     * 
     * 返回后端驱动支持的最高特性级别。
     * 
     * @return 支持的特性级别
     */
    FeatureLevel getSupportedFeatureLevel() const noexcept;

    /**
     * 设置活动特性级别
     * 
     * 设置 Engine 使用的特性级别。级别不能超过支持的特性级别。
     * 
     * @param featureLevel 要设置的特性级别
     * @return 实际设置的特性级别（可能被限制为支持的最高级别）
     */
    FeatureLevel setActiveFeatureLevel(FeatureLevel featureLevel);

    /**
     * 获取活动特性级别
     * 
     * 返回当前使用的特性级别。
     * 
     * @return 活动特性级别
     */
    FeatureLevel getActiveFeatureLevel() const noexcept {
        return mActiveFeatureLevel;
    }

    /**
     * 获取最大自动实例数量
     * 
     * 返回系统支持的最大自动实例数量（用于实例化渲染）。
     * 
     * @return 最大自动实例数量
     */
    size_t getMaxAutomaticInstances() const noexcept {
        return CONFIG_MAX_INSTANCES;
    }

    /**
     * 是否支持立体渲染
     * 
     * 检查后端驱动是否支持立体渲染（VR/AR）。
     * 
     * @return 如果支持返回 true，否则返回 false
     */
    bool isStereoSupported() const noexcept {
        return getDriver().isStereoSupported();
    }

    /**
     * 是否支持异步操作
     * 
     * 检查是否支持异步操作模式。
     * 
     * @return 如果支持返回 true，否则返回 false
     */
    bool isAsynchronousOperationSupported() const noexcept;

    /**
     * 获取最大立体眼睛数量
     * 
     * 返回系统支持的最大立体眼睛数量（通常为 2）。
     * 
     * @return 最大立体眼睛数量
     */
    static size_t getMaxStereoscopicEyes() noexcept {
        return CONFIG_MAX_STEREOSCOPIC_EYES;
    }

    /**
     * 获取后处理管理器（常量版本）
     * 
     * @return 后处理管理器常量引用
     */
    PostProcessManager const& getPostProcessManager() const noexcept {
        return mPostProcessManager;
    }

    /**
     * 获取后处理管理器
     * 
     * @return 后处理管理器引用
     */
    PostProcessManager& getPostProcessManager() noexcept {
        return mPostProcessManager;
    }

    /**
     * 获取可渲染对象管理器
     * 
     * @return 可渲染对象管理器引用
     */
    FRenderableManager& getRenderableManager() noexcept {
        return mRenderableManager;
    }

    /**
     * 获取可渲染对象管理器（常量版本）
     * 
     * @return 可渲染对象管理器常量引用
     */
    FRenderableManager const& getRenderableManager() const noexcept {
        return mRenderableManager;
    }

    /**
     * 获取光源管理器
     * 
     * @return 光源管理器引用
     */
    FLightManager& getLightManager() noexcept {
        return mLightManager;
    }

    /**
     * 获取光源管理器（常量版本）
     * 
     * @return 光源管理器常量引用
     */
    FLightManager const& getLightManager() const noexcept {
        return mLightManager;
    }

    /**
     * 获取相机管理器
     * 
     * @return 相机管理器引用
     */
    FCameraManager& getCameraManager() noexcept {
        return mCameraManager;
    }

    /**
     * 获取变换管理器
     * 
     * @return 变换管理器引用
     */
    FTransformManager& getTransformManager() noexcept {
        return mTransformManager;
    }

    /**
     * 获取实体管理器
     * 
     * @return 实体管理器引用
     */
    utils::EntityManager& getEntityManager() noexcept {
        return mEntityManager;
    }

    /**
     * 获取堆分配器
     * 
     * @return 堆分配器引用
     */
    HeapAllocatorArena& getHeapAllocator() noexcept {
        return mHeapAllocator;
    }

    /**
     * 获取后端类型
     * 
     * @return 后端类型（OpenGL、Vulkan、Metal 等）
     */
    Backend getBackend() const noexcept {
        return mBackend;
    }

    /**
     * UBO 批处理是否启用
     * 
     * @return 如果启用返回 true，否则返回 false
     */
    bool isUboBatchingEnabled() const noexcept {
        return mUboManager != nullptr;  // UBO 管理器存在即表示启用
    }

    /**
     * 获取 UBO 管理器
     * 
     * @return UBO 管理器指针
     */
    UboManager* getUboManager() noexcept {
        return mUboManager;
    }

    /**
     * 获取平台指针
     * 
     * @return 平台指针
     */
    Platform* getPlatform() const noexcept {
        return mPlatform;
    }

    /**
     * 获取最大阴影贴图数量
     * 
     * @return 最大阴影贴图数量
     */
    size_t getMaxShadowMapCount() const noexcept;

    /**
     * 获取着色器语言
     * 
     * 返回按优先级排序的着色器语言向量。
     * 
     * @return 着色器语言向量
     */
    utils::FixedCapacityVector<backend::ShaderLanguage> getShaderLanguage() const noexcept {
        backend::ShaderLanguage preferredLanguage;

        switch (mConfig.preferredShaderLanguage) {
            case Config::ShaderLanguage::DEFAULT:
                preferredLanguage = backend::ShaderLanguage::UNSPECIFIED;
                break;
            case Config::ShaderLanguage::MSL:
                preferredLanguage = backend::ShaderLanguage::MSL;
                break;
            case Config::ShaderLanguage::METAL_LIBRARY:
                preferredLanguage = backend::ShaderLanguage::METAL_LIBRARY;
                break;
        }


        return getDriver().getShaderLanguages(preferredLanguage);
    }

    /**
     * 获取资源分配器处置器
     * 
     * 返回用于管理资源分配器生命周期的处置器引用。
     * 
     * @return 资源分配器处置器引用
     */
    ResourceAllocatorDisposer& getResourceAllocatorDisposer() noexcept {
        assert_invariant(mResourceAllocatorDisposer);
        return *mResourceAllocatorDisposer;
    }

    /**
     * 获取共享的资源分配器处置器
     * 
     * 返回资源分配器处置器的共享指针（用于多线程共享）。
     * 
     * @return 资源分配器处置器的共享指针
     */
    std::shared_ptr<ResourceAllocatorDisposer> const& getSharedResourceAllocatorDisposer() noexcept {
        return mResourceAllocatorDisposer;
    }

    /**
     * 获取材质缓存
     * 
     * 返回用于缓存已编译材质的材质缓存引用。
     * 
     * @return 材质缓存引用
     */
    MaterialCache& getMaterialCache() const noexcept {
        return mMaterialCache;
    }

    /**
     * 流式分配内存
     * 
     * 从流式内存分配器中分配指定大小和对齐的内存。
     * 用于临时分配，会在帧结束时自动释放。
     * 
     * @param size 要分配的内存大小（字节）
     * @param alignment 内存对齐要求（字节）
     * @return 分配的内存指针，如果分配失败返回 nullptr
     */
    void* streamAlloc(size_t size, size_t alignment) noexcept;

    /**
     * 获取 Engine 纪元时间点
     * 
     * 返回 Engine 创建时的时间点，用于计算相对时间。
     * 
     * @return Engine 纪元时间点
     */
    Epoch getEngineEpoch() const { return mEngineEpoch; }
    
    /**
     * 获取 Engine 运行时间
     * 
     * 返回从 Engine 创建到现在的持续时间。
     * 
     * @return Engine 运行时间
     */
    duration getEngineTime() const noexcept {
        return clock::now() - getEngineEpoch();
    }

    /**
     * 获取默认渲染目标
     * 
     * 返回默认的渲染目标句柄（通常是交换链的后台缓冲区）。
     * 
     * @return 默认渲染目标句柄
     */
    backend::Handle<backend::HwRenderTarget> getDefaultRenderTarget() const noexcept {
        return mDefaultRenderTarget;
    }

    /**
     * 创建资源（模板方法）
     * 
     * 通用资源创建方法，将资源添加到资源列表中。
     * 
     * @tparam T 资源类型
     * @tparam ARGS 额外参数类型
     * @param list 资源列表
     * @param builder 构建器
     * @param args 额外参数
     * @return 资源指针
     */
    template <typename T, typename ... ARGS>
    T* create(ResourceList<T>& list, typename T::Builder const& builder, ARGS&& ... args) noexcept;

    /**
     * 创建缓冲区对象
     * 
     * @param builder 构建器
     * @return 缓冲区对象指针
     */
    FBufferObject* createBufferObject(const BufferObject::Builder& builder) noexcept;
    
    /**
     * 创建顶点缓冲区
     * 
     * @param builder 构建器
     * @return 顶点缓冲区指针
     */
    FVertexBuffer* createVertexBuffer(const VertexBuffer::Builder& builder) noexcept;
    
    /**
     * 创建索引缓冲区
     * 
     * @param builder 构建器
     * @return 索引缓冲区指针
     */
    FIndexBuffer* createIndexBuffer(const IndexBuffer::Builder& builder) noexcept;
    
    /**
     * 创建蒙皮缓冲区
     * 
     * @param builder 构建器
     * @return 蒙皮缓冲区指针
     */
    FSkinningBuffer* createSkinningBuffer(const SkinningBuffer::Builder& builder) noexcept;
    
    /**
     * 创建变形目标缓冲区
     * 
     * @param builder 构建器
     * @return 变形目标缓冲区指针
     */
    FMorphTargetBuffer* createMorphTargetBuffer(const MorphTargetBuffer::Builder& builder) noexcept;
    
    /**
     * 创建实例缓冲区
     * 
     * @param builder 构建器
     * @return 实例缓冲区指针
     */
    FInstanceBuffer* createInstanceBuffer(const InstanceBuffer::Builder& builder) noexcept;
    
    /**
     * 创建间接光照
     * 
     * @param builder 构建器
     * @return 间接光照指针
     */
    FIndirectLight* createIndirectLight(const IndirectLight::Builder& builder) noexcept;
    
    /**
     * 创建材质
     * 
     * @param builder 构建器
     * @param definition 材质定义
     * @return 材质指针
     */
    FMaterial* createMaterial(const Material::Builder& builder,
            MaterialDefinition const& definition) noexcept;
    
    /**
     * 创建纹理
     * 
     * @param builder 构建器
     * @return 纹理指针
     */
    FTexture* createTexture(const Texture::Builder& builder) noexcept;
    
    /**
     * 创建天空盒
     * 
     * @param builder 构建器
     * @return 天空盒指针
     */
    FSkybox* createSkybox(const Skybox::Builder& builder) noexcept;
    
    /**
     * 创建颜色分级
     * 
     * @param builder 构建器
     * @return 颜色分级指针
     */
    FColorGrading* createColorGrading(const ColorGrading::Builder& builder) noexcept;
    
    /**
     * 创建流
     * 
     * @param builder 构建器
     * @return 流指针
     */
    FStream* createStream(const Stream::Builder& builder) noexcept;
    
    /**
     * 创建渲染目标
     * 
     * @param builder 构建器
     * @return 渲染目标指针
     */
    FRenderTarget* createRenderTarget(const RenderTarget::Builder& builder) noexcept;

    /**
     * 创建可渲染对象
     * 
     * @param builder 构建器
     * @param entity 实体
     */
    void createRenderable(const RenderableManager::Builder& builder, utils::Entity entity);
    
    /**
     * 创建光源
     * 
     * @param builder 构建器
     * @param entity 实体
     */
    void createLight(const LightManager::Builder& builder, utils::Entity entity);

    /**
     * 创建渲染器
     * 
     * @return 渲染器指针
     */
    FRenderer* createRenderer() noexcept;

    /**
     * 创建材质实例（从其他实例复制）
     * 
     * @param material 材质指针
     * @param other 要复制的实例指针
     * @param name 实例名称
     * @return 材质实例指针
     */
    FMaterialInstance* createMaterialInstance(const FMaterial* material,
            const FMaterialInstance* other, const char* name) noexcept;

    /**
     * 创建材质实例（从材质创建）
     * 
     * @param material 材质指针
     * @param name 实例名称
     * @return 材质实例指针
     */
    FMaterialInstance* createMaterialInstance(const FMaterial* material, const char* name) noexcept;

    /**
     * 创建场景
     * 
     * @return 场景指针
     */
    FScene* createScene() noexcept;
    
    /**
     * 创建视图
     * 
     * @return 视图指针
     */
    FView* createView() noexcept;
    
    /**
     * 创建栅栏
     * 
     * @return 栅栏指针
     */
    FFence* createFence() noexcept;
    
    /**
     * 创建同步对象
     * 
     * @return 同步对象指针
     */
    FSync* createSync() noexcept;
    
    /**
     * 创建交换链（从原生窗口）
     * 
     * @param nativeWindow 原生窗口指针
     * @param flags 标志
     * @return 交换链指针
     */
    FSwapChain* createSwapChain(void* nativeWindow, uint64_t flags) noexcept;
    
    /**
     * 创建交换链（从尺寸）
     * 
     * @param width 宽度
     * @param height 高度
     * @param flags 标志
     * @return 交换链指针
     */
    FSwapChain* createSwapChain(uint32_t width, uint32_t height, uint64_t flags) noexcept;

    /**
     * 创建相机
     * 
     * @param entity 实体
     * @return 相机指针
     */
    FCamera* createCamera(utils::Entity entity) noexcept;
    
    /**
     * 获取相机组件
     * 
     * @param entity 实体
     * @return 相机指针
     */
    FCamera* getCameraComponent(utils::Entity entity) noexcept;
    
    /**
     * 销毁相机组件
     * 
     * @param entity 实体
     */
    void destroyCameraComponent(utils::Entity entity) noexcept;


    /**
     * 销毁缓冲区对象
     * 
     * @param p 缓冲区对象指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FBufferObject* p);
    
    /**
     * 销毁顶点缓冲区
     * 
     * @param p 顶点缓冲区指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FVertexBuffer* p);
    
    /**
     * 销毁栅栏
     * 
     * @param p 栅栏指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FFence* p);
    
    /**
     * 销毁同步对象
     * 
     * @param p 同步对象指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FSync* p);
    
    /**
     * 销毁索引缓冲区
     * 
     * @param p 索引缓冲区指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FIndexBuffer* p);
    
    /**
     * 销毁蒙皮缓冲区
     * 
     * @param p 蒙皮缓冲区指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FSkinningBuffer* p);
    
    /**
     * 销毁变形目标缓冲区
     * 
     * @param p 变形目标缓冲区指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FMorphTargetBuffer* p);
    
    /**
     * 销毁间接光照
     * 
     * @param p 间接光照指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FIndirectLight* p);
    
    /**
     * 销毁材质
     * 
     * @param p 材质指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FMaterial* p);
    
    /**
     * 销毁材质实例
     * 
     * @param p 材质实例指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FMaterialInstance* p);
    
    /**
     * 销毁渲染器
     * 
     * @param p 渲染器指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FRenderer* p);
    
    /**
     * 销毁场景
     * 
     * @param p 场景指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FScene* p);
    
    /**
     * 销毁天空盒
     * 
     * @param p 天空盒指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FSkybox* p);
    
    /**
     * 销毁颜色分级
     * 
     * @param p 颜色分级指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FColorGrading* p);
    
    /**
     * 销毁流
     * 
     * @param p 流指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FStream* p);
    
    /**
     * 销毁纹理
     * 
     * @param p 纹理指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FTexture* p);
    
    /**
     * 销毁渲染目标
     * 
     * @param p 渲染目标指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FRenderTarget* p);
    
    /**
     * 销毁交换链
     * 
     * @param p 交换链指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FSwapChain* p);
    
    /**
     * 销毁视图
     * 
     * @param p 视图指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FView* p);
    
    /**
     * 销毁实例缓冲区
     * 
     * @param p 实例缓冲区指针
     * @return 如果销毁成功返回 true，否则返回 false
     */
    bool destroy(const FInstanceBuffer* p);

    /**
     * 检查缓冲区对象是否有效
     * 
     * @param p 缓冲区对象指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FBufferObject* p) const;
    
    /**
     * 检查顶点缓冲区是否有效
     * 
     * @param p 顶点缓冲区指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FVertexBuffer* p) const;
    
    /**
     * 检查栅栏是否有效
     * 
     * @param p 栅栏指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FFence* p) const;
    
    /**
     * 检查同步对象是否有效
     * 
     * @param p 同步对象指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FSync* p) const;
    
    /**
     * 检查索引缓冲区是否有效
     * 
     * @param p 索引缓冲区指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FIndexBuffer* p) const;
    
    /**
     * 检查蒙皮缓冲区是否有效
     * 
     * @param p 蒙皮缓冲区指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FSkinningBuffer* p) const;
    
    /**
     * 检查变形目标缓冲区是否有效
     * 
     * @param p 变形目标缓冲区指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FMorphTargetBuffer* p) const;
    
    /**
     * 检查间接光照是否有效
     * 
     * @param p 间接光照指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FIndirectLight* p) const;
    
    /**
     * 检查材质是否有效
     * 
     * @param p 材质指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FMaterial* p) const;
    
    /**
     * 检查材质实例是否有效（关联到指定材质）
     * 
     * @param m 材质指针
     * @param p 材质实例指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FMaterial* m, const FMaterialInstance* p) const;
    
    /**
     * 检查材质实例是否有效（昂贵版本，进行完整验证）
     * 
     * @param p 材质实例指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValidExpensive(const FMaterialInstance* p) const;
    
    /**
     * 检查渲染器是否有效
     * 
     * @param p 渲染器指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FRenderer* p) const;
    
    /**
     * 检查场景是否有效
     * 
     * @param p 场景指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FScene* p) const;
    
    /**
     * 检查天空盒是否有效
     * 
     * @param p 天空盒指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FSkybox* p) const;
    
    /**
     * 检查颜色分级是否有效
     * 
     * @param p 颜色分级指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FColorGrading* p) const;
    
    /**
     * 检查交换链是否有效
     * 
     * @param p 交换链指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FSwapChain* p) const;
    
    /**
     * 检查流是否有效
     * 
     * @param p 流指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FStream* p) const;
    
    /**
     * 检查纹理是否有效
     * 
     * @param p 纹理指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FTexture* p) const;
    
    /**
     * 检查渲染目标是否有效
     * 
     * @param p 渲染目标指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FRenderTarget* p) const;
    
    /**
     * 检查视图是否有效
     * 
     * @param p 视图指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FView* p) const;
    
    /**
     * 检查实例缓冲区是否有效
     * 
     * @param p 实例缓冲区指针
     * @return 如果有效返回 true，否则返回 false
     */
    bool isValid(const FInstanceBuffer* p) const;

    /**
     * 获取缓冲区对象数量
     * 
     * @return 当前存在的缓冲区对象数量
     */
    size_t getBufferObjectCount() const noexcept;
    
    /**
     * 获取视图数量
     * 
     * @return 当前存在的视图数量
     */
    size_t getViewCount() const noexcept;
    
    /**
     * 获取场景数量
     * 
     * @return 当前存在的场景数量
     */
    size_t getSceneCount() const noexcept;
    
    /**
     * 获取交换链数量
     * 
     * @return 当前存在的交换链数量
     */
    size_t getSwapChainCount() const noexcept;
    
    /**
     * 获取流数量
     * 
     * @return 当前存在的流数量
     */
    size_t getStreamCount() const noexcept;
    
    /**
     * 获取索引缓冲区数量
     * 
     * @return 当前存在的索引缓冲区数量
     */
    size_t getIndexBufferCount() const noexcept;
    
    /**
     * 获取蒙皮缓冲区数量
     * 
     * @return 当前存在的蒙皮缓冲区数量
     */
    size_t getSkinningBufferCount() const noexcept;
    
    /**
     * 获取变形目标缓冲区数量
     * 
     * @return 当前存在的变形目标缓冲区数量
     */
    size_t getMorphTargetBufferCount() const noexcept;
    
    /**
     * 获取实例缓冲区数量
     * 
     * @return 当前存在的实例缓冲区数量
     */
    size_t getInstanceBufferCount() const noexcept;
    
    /**
     * 获取顶点缓冲区数量
     * 
     * @return 当前存在的顶点缓冲区数量
     */
    size_t getVertexBufferCount() const noexcept;
    
    /**
     * 获取间接光照数量
     * 
     * @return 当前存在的间接光照数量
     */
    size_t getIndirectLightCount() const noexcept;
    
    /**
     * 获取材质数量
     * 
     * @return 当前存在的材质数量
     */
    size_t getMaterialCount() const noexcept;
    
    /**
     * 获取纹理数量
     * 
     * @return 当前存在的纹理数量
     */
    size_t getTextureCount() const noexcept;
    
    /**
     * 获取天空盒数量
     * 
     * @return 当前存在的天空盒数量
     */
    size_t getSkyboxeCount() const noexcept;
    
    /**
     * 获取颜色分级数量
     * 
     * @return 当前存在的颜色分级数量
     */
    size_t getColorGradingCount() const noexcept;
    
    /**
     * 获取渲染目标数量
     * 
     * @return 当前存在的渲染目标数量
     */
    size_t getRenderTargetCount() const noexcept;

    /**
     * 销毁实体
     * 
     * 销毁指定实体的所有组件。
     * 
     * @param e 实体
     */
    void destroy(utils::Entity e);

    /**
     * 检查 Engine 是否暂停
     * 
     * @return 如果暂停返回 true，否则返回 false
     */
    bool isPaused() const noexcept;
    
    /**
     * 设置 Engine 暂停状态
     * 
     * 暂停时，Engine 不会处理命令队列。
     * 
     * @param paused 是否暂停
     */
    void setPaused(bool paused);

    /**
     * 刷新并等待
     * 
     * 刷新当前缓冲区并等待所有命令完成。
     */
    void flushAndWait();
    
    /**
     * 刷新并等待（带超时）
     * 
     * 刷新当前缓冲区并等待所有命令完成，带超时限制。
     * 
     * @param timeout 超时时间（纳秒）
     * @return 如果成功返回 true，如果超时返回 false
     */
    bool flushAndWait(uint64_t timeout);

    /**
     * 刷新当前缓冲区
     * 
     * 将当前命令缓冲区提交到驱动。
     */
    void flush();

    /**
     * 如果需要则刷新
     * 
     * 基于一些启发式规则刷新当前缓冲区。
     * 每 128 次调用刷新一次。
     */
    void flushIfNeeded() {
        auto counter = mFlushCounter + 1;  // 递增计数器
        if (UTILS_LIKELY(counter < 128)) {  // 如果计数器小于 128（常见情况）
            mFlushCounter = counter;  // 更新计数器
        } else {  // 如果计数器达到 128（不常见情况）
            mFlushCounter = 0;  // 重置计数器
            flush();  // 刷新缓冲区
        }
    }

    /**
     * Processes the platform's event queue when called from the platform's event-handling thread.
     * Returns false when called from any other thread.
     */
    /**
     * 处理平台事件队列
     * 
     * 当从平台的事件处理线程调用时处理平台的事件队列。
     * 从任何其他线程调用时返回 false。
     * 
     * @return 如果成功处理返回 true，否则返回 false
     */
    bool pumpPlatformEvents() {
        return mPlatform->pumpEvents();
    }

    /**
     * 准备
     * 
     * 准备引擎进行渲染（清理资源、更新状态等）。
     */
    void prepare();
    
    /**
     * 垃圾回收
     * 
     * 清理不再使用的资源。
     */
    void gc();
    
    /**
     * 提交帧
     * 
     * 提交当前帧到驱动。
     */
    void submitFrame();

    /**
     * 着色器内容类型别名
     */
    using ShaderContent = utils::FixedCapacityVector<uint8_t>;

    /**
     * 获取顶点着色器内容
     * 
     * @return 顶点着色器内容引用
     */
    ShaderContent& getVertexShaderContent() const noexcept {
        return mVertexShaderContent;
    }

    /**
     * 获取片段着色器内容
     * 
     * @return 片段着色器内容引用
     */
    ShaderContent& getFragmentShaderContent() const noexcept {
        return mFragmentShaderContent;
    }

    /**
     * 获取调试注册表
     * 
     * @return 调试注册表引用
     */
    FDebugRegistry& getDebugRegistry() noexcept {
        return mDebugRegistry;
    }

    /**
     * 执行
     * 
     * 执行驱动命令队列。
     * 
     * @return 如果成功返回 true，否则返回 false
     */
    bool execute();

    /**
     * 获取作业系统
     * 
     * JobSystem 是线程安全的，返回非常量版本总是安全的，
     * 这在概念上等同于持有非常量引用，而不是按值类属性。
     * 
     * @return 作业系统引用
     */
    utils::JobSystem& getJobSystem() const noexcept {
        // JobSystem 是线程安全的，返回非常量版本总是安全的
        return const_cast<utils::JobSystem&>(mJobSystem);
    }

    /**
     * 获取随机数引擎
     * 
     * @return 随机数引擎引用
     */
    std::default_random_engine& getRandomEngine() {
        return mRandomEngine;
    }

    /**
     * 泵送消息队列
     * 
     * 处理驱动消息队列。
     */
    void pumpMessageQueues() const {
        getDriver().purge();  // 清理驱动消息队列
    }

    /**
     * 取消保护
     * 
     * 取消对受保护资源的保护（用于调试）。
     */
    void unprotected() noexcept;

    void setAutomaticInstancingEnabled(bool const enable) noexcept {
        // instancing is not allowed at feature level 0
        if (hasFeatureLevel(FeatureLevel::FEATURE_LEVEL_1)) {
            mAutomaticInstancingEnabled = enable;
        }
    }

    bool isAutomaticInstancingEnabled() const noexcept {
        return mAutomaticInstancingEnabled;
    }

    HwVertexBufferInfoFactory& getVertexBufferInfoFactory() noexcept {
        return mHwVertexBufferInfoFactory;
    }

    HwDescriptorSetLayoutFactory& getDescriptorSetLayoutFactory() noexcept {
        return mHwDescriptorSetLayoutFactory;
    }

    DescriptorSetLayout const& getPerViewDescriptorSetLayoutDepthVariant() const noexcept {
        return mPerViewDescriptorSetLayoutDepthVariant;
    }

    DescriptorSetLayout const& getPerViewDescriptorSetLayoutSsrVariant() const noexcept {
        return mPerViewDescriptorSetLayoutSsrVariant;
    }

    DescriptorSetLayout const& getPerRenderableDescriptorSetLayout() const noexcept {
        return mPerRenderableDescriptorSetLayout;
    }

    backend::Handle<backend::HwTexture> getOneTexture() const {
        return mDummyOneTexture;
    }

    backend::Handle<backend::HwTexture> getOneTextureArray() const {
        return mDummyOneTextureArray;
    }

    backend::Handle<backend::HwTexture> getOneTextureArrayDepth() const {
        return mDummyOneTextureArrayDepth;
    }

    backend::Handle<backend::HwTexture> getZeroTexture() const {
        return mDummyZeroTexture;
    }

    backend::Handle<backend::HwTexture> getZeroTextureArray() const {
        return mDummyZeroTextureArray;
    }

    backend::Handle<backend::HwBufferObject> getDummyUniformBuffer() const {
        return mDummyUniformBuffer;
    }

    static constexpr size_t MiB = 1024u * 1024u;
    size_t getMinCommandBufferSize() const noexcept { return mConfig.minCommandBufferSizeMB * MiB; }
    size_t getCommandBufferSize() const noexcept { return mConfig.commandBufferSizeMB * MiB; }
    size_t getPerFrameCommandsSize() const noexcept { return mConfig.perFrameCommandsSizeMB * MiB; }
    size_t getPerRenderPassArenaSize() const noexcept { return mConfig.perRenderPassArenaSizeMB * MiB; }
    size_t getRequestedDriverHandleArenaSize() const noexcept { return mConfig.driverHandleArenaSizeMB * MiB; }
    Config const& getConfig() const noexcept { return mConfig; }

    /**
     * 是否有特性级别
     * 
     * 检查当前活动特性级别是否满足所需特性级别。
     * 
     * @param neededFeatureLevel 所需特性级别
     * @return 如果满足返回 true，否则返回 false
     */
    bool hasFeatureLevel(backend::FeatureLevel const neededFeatureLevel) const noexcept {
        return getActiveFeatureLevel() >= neededFeatureLevel;
    }

    /**
     * 获取材质实例资源列表
     * 
     * @return 材质实例资源列表常量引用
     */
    auto const& getMaterialInstanceResourceList() const noexcept {
        return mMaterialInstances;
    }

#if defined(__EMSCRIPTEN__)
    /**
     * 重置后端状态
     * 
     * 仅用于 Emscripten（WebAssembly）平台。
     */
    void resetBackendState() noexcept;
#endif

    /**
     * 获取驱动
     * 
     * @return 驱动引用
     */
    backend::Driver& getDriver() const noexcept { return *mDriver; }

private:
    /**
     * 私有构造函数
     * 
     * @param builder 构建器引用
     */
    explicit FEngine(Builder const& builder);
    
    /**
     * 初始化
     * 
     * 初始化引擎的所有子系统。
     */
    void init();
    
    /**
     * 关闭
     * 
     * 关闭引擎并清理资源。
     */
    void shutdown();

    /**
     * 驱动循环
     * 
     * 驱动线程的主循环（多线程模式下）。
     * 
     * @return 退出码
     */
    int loop();
    
    /**
     * 刷新命令缓冲区
     * 
     * 将命令缓冲区提交到驱动。
     * 
     * @param commandBufferQueue 命令缓冲区队列引用
     */
    void flushCommandBuffer(backend::CommandBufferQueue& commandBufferQueue) const;

    /**
     * 检查资源是否有效（模板方法）
     * 
     * @tparam T 资源类型
     * @param ptr 资源指针
     * @param list 资源列表常量引用
     * @return 如果资源有效返回 true，否则返回 false
     */
    template<typename T>
    bool isValid(const T* ptr, ResourceList<T> const& list) const;

    /**
     * 终止并销毁资源（模板方法）
     * 
     * @tparam T 资源类型
     * @param p 资源指针
     * @param list 资源列表引用
     * @return 如果销毁成功返回 true，否则返回 false
     */
    template<typename T>
    bool terminateAndDestroy(const T* p, ResourceList<T>& list);

    /**
     * 终止并销毁资源（带锁，模板方法）
     * 
     * @tparam T 资源类型
     * @tparam Lock 锁类型
     * @param lock 锁引用
     * @param p 资源指针
     * @param list 资源列表引用
     * @return 如果销毁成功返回 true，否则返回 false
     */
    template<typename T, typename Lock>
    bool terminateAndDestroyLocked(Lock& lock, const T* p, ResourceList<T>& list);

    /**
     * 清理资源列表（模板方法）
     * 
     * @tparam T 资源类型
     * @param list 资源列表（右值引用，会被移动）
     */
    template<typename T>
    void cleanupResourceList(ResourceList<T>&& list);

    /**
     * 清理资源列表（带锁，模板方法）
     * 
     * @tparam T 资源类型
     * @tparam Lock 锁类型
     * @param lock 锁引用
     * @param list 资源列表（右值引用，会被移动）
     */
    template<typename T, typename Lock>
    void cleanupResourceListLocked(Lock& lock, ResourceList<T>&& list);

    /**
     * 驱动指针
     */
    backend::Driver* mDriver = nullptr;
    
    /**
     * 默认渲染目标句柄
     */
    backend::Handle<backend::HwRenderTarget> mDefaultRenderTarget;

    /**
     * 后端类型
     */
    Backend mBackend;
    
    /**
     * 活动特性级别
     */
    FeatureLevel mActiveFeatureLevel = FeatureLevel::FEATURE_LEVEL_1;
    
    /**
     * 平台指针
     */
    Platform* mPlatform = nullptr;
    
    /**
     * 是否拥有平台（析构时释放）
     */
    bool mOwnPlatform = false;
    
    /**
     * 是否启用自动实例化
     */
    bool mAutomaticInstancingEnabled = false;
    
    /**
     * 共享 GL 上下文（用于多线程渲染）
     */
    void* mSharedGLContext = nullptr;
    
    /**
     * 全屏三角形渲染图元句柄
     */
    backend::Handle<backend::HwRenderPrimitive> mFullScreenTriangleRph;
    
    /**
     * 全屏三角形顶点缓冲区指针
     */
    FVertexBuffer* mFullScreenTriangleVb = nullptr;
    
    /**
     * 全屏三角形索引缓冲区指针
     */
    FIndexBuffer* mFullScreenTriangleIb = nullptr;
    
    /**
     * UV 从裁剪空间矩阵（用于后处理）
     */
    math::mat4f mUvFromClipMatrix;

    /**
     * 后处理管理器
     */
    PostProcessManager mPostProcessManager;

    /**
     * 实体管理器引用
     */
    utils::EntityManager& mEntityManager;
    
    /**
     * 可渲染对象管理器
     */
    FRenderableManager mRenderableManager;
    
    /**
     * 变换管理器
     */
    FTransformManager mTransformManager;
    
    /**
     * 光源管理器
     */
    FLightManager mLightManager;
    
    /**
     * 相机管理器
     */
    FCameraManager mCameraManager;
    
    /**
     * 资源分配器删除器（共享指针）
     */
    std::shared_ptr<ResourceAllocatorDisposer> mResourceAllocatorDisposer;
    
    /**
     * 材质缓存（可变，因为可能被修改）
     */
    mutable MaterialCache mMaterialCache;
    
    /**
     * 硬件顶点缓冲区信息工厂
     */
    HwVertexBufferInfoFactory mHwVertexBufferInfoFactory;
    
    /**
     * 硬件描述符堆布局工厂
     */
    HwDescriptorSetLayoutFactory mHwDescriptorSetLayoutFactory;
    
    /**
     * 每视图描述符堆布局（深度变体）
     */
    DescriptorSetLayout mPerViewDescriptorSetLayoutDepthVariant;
    
    /**
     * 每视图描述符堆布局（SSR 变体）
     */
    DescriptorSetLayout mPerViewDescriptorSetLayoutSsrVariant;
    
    /**
     * 每可渲染对象描述符堆布局
     */
    DescriptorSetLayout mPerRenderableDescriptorSetLayout;

    ResourceList<FBufferObject> mBufferObjects{ "BufferObject" };
    ResourceList<FRenderer> mRenderers{ "Renderer" };
    ResourceList<FView> mViews{ "View" };
    ResourceList<FScene> mScenes{ "Scene" };
    ResourceList<FSwapChain> mSwapChains{ "SwapChain" };
    ResourceList<FStream> mStreams{ "Stream" };
    ResourceList<FIndexBuffer> mIndexBuffers{ "IndexBuffer" };
    ResourceList<FSkinningBuffer> mSkinningBuffers{ "SkinningBuffer" };
    ResourceList<FMorphTargetBuffer> mMorphTargetBuffers{ "MorphTargetBuffer" };
    ResourceList<FInstanceBuffer> mInstanceBuffers{ "InstanceBuffer" };
    ResourceList<FVertexBuffer> mVertexBuffers{ "VertexBuffer" };
    ResourceList<FIndirectLight> mIndirectLights{ "IndirectLight" };
    ResourceList<FMaterial> mMaterials{ "Material" };
    ResourceList<FTexture> mTextures{ "Texture" };
    ResourceList<FSkybox> mSkyboxes{ "Skybox" };
    ResourceList<FColorGrading> mColorGradings{ "ColorGrading" };
    ResourceList<FRenderTarget> mRenderTargets{ "RenderTarget" };

    /**
     * 栅栏列表锁
     * 
     * 栅栏列表从多个线程访问，需要锁保护。
     */
    utils::Mutex mFenceListLock;
    
    /**
     * 栅栏资源列表
     */
    ResourceList<FFence> mFences{"Fence"};

    /**
     * 同步对象列表锁
     * 
     * 同步对象列表从多个线程访问，因为它们是同步对象。
     */
    utils::Mutex mSyncListLock;
    
    /**
     * 同步对象资源列表
     */
    ResourceList<FSync> mSyncs{ "Sync" };

    /**
     * 材质 ID（可变，因为会被修改）
     */
    mutable uint32_t mMaterialId = 0;

    /**
     * 材质实例映射
     * 
     * FMaterialInstance 由 FMaterial 直接管理。
     * 键为材质指针，值为该材质的实例列表。
     */
    std::unordered_map<const FMaterial*, ResourceList<FMaterialInstance>> mMaterialInstances;

    /**
     * DFG（分布-菲涅耳-几何）查找表
     */
    DFG mDFG;

    /**
     * 驱动线程（多线程模式下）
     */
    std::thread mDriverThread;
    
    /**
     * 命令缓冲区队列
     */
    backend::CommandBufferQueue mCommandBufferQueue;
    
    /**
     * 驱动 API 存储（对齐存储）
     * 
     * 使用对齐存储来避免未定义行为。
     */
    std::aligned_storage<sizeof(DriverApi), alignof(DriverApi)>::type mDriverApiStorage;
    static_assert( sizeof(mDriverApiStorage) >= sizeof(DriverApi) );  // 确保大小足够

    /**
     * 刷新计数器
     * 
     * 用于启发式刷新（每 128 次调用刷新一次）。
     */
    uint32_t mFlushCounter = 0;

    /**
     * UBO 管理器指针
     */
    UboManager* mUboManager = nullptr;
    
    /**
     * 每渲染通道内存池
     */
    RootArenaScope::Arena mPerRenderPassArena;
    
    /**
     * 堆分配器
     */
    HeapAllocatorArena mHeapAllocator;

    /**
     * 作业系统
     */
    utils::JobSystem mJobSystem;
    
    /**
     * 获取作业系统线程池大小（静态方法）
     * 
     * @param config 配置常量引用
     * @return 线程池大小
     */
    static uint32_t getJobSystemThreadPoolSize(Config const& config) noexcept;

    /**
     * 随机数引擎
     */
    std::default_random_engine mRandomEngine;

    /**
     * 引擎纪元（时间点）
     */
    Epoch mEngineEpoch;

    /**
     * 默认材质指针（可变，因为可能被延迟初始化）
     */
    mutable FMaterial const* mDefaultMaterial = nullptr;
    
    /**
     * 天空盒材质指针（可变，因为可能被延迟初始化）
     */
    mutable FMaterial const* mSkyboxMaterial = nullptr;
    
    /**
     * 未保护虚拟交换链指针（可变，因为可能被延迟初始化）
     */
    mutable FSwapChain* mUnprotectedDummySwapchain = nullptr;

    /**
     * 默认 IBL 纹理指针（可变，因为可能被延迟初始化）
     */
    mutable FTexture* mDefaultIblTexture = nullptr;
    
    /**
     * 默认间接光照指针（可变，因为可能被延迟初始化）
     */
    mutable FIndirectLight* mDefaultIbl = nullptr;

    /**
     * 默认颜色分级指针（可变，因为可能被延迟初始化）
     */
    mutable FColorGrading* mDefaultColorGrading = nullptr;
    
    /**
     * 虚拟变形目标缓冲区指针
     */
    FMorphTargetBuffer* mDummyMorphTargetBuffer = nullptr;

    /**
     * 驱动栅栏（可变，因为可能被修改）
     * 
     * 用于同步驱动线程初始化。
     */
    mutable utils::CountDownLatch mDriverBarrier;

    /**
     * 顶点着色器内容（可变，因为可能被修改）
     */
    mutable ShaderContent mVertexShaderContent;
    
    /**
     * 片段着色器内容（可变，因为可能被修改）
     */
    mutable ShaderContent mFragmentShaderContent;
    
    /**
     * 调试注册表
     */
    FDebugRegistry mDebugRegistry;

    /**
     * 虚拟纹理句柄（全 1）
     */
    backend::Handle<backend::HwTexture> mDummyOneTexture;
    
    /**
     * 虚拟纹理数组句柄（全 1）
     */
    backend::Handle<backend::HwTexture> mDummyOneTextureArray;
    
    /**
     * 虚拟深度纹理数组句柄（全 1）
     */
    backend::Handle<backend::HwTexture> mDummyOneTextureArrayDepth;
    
    /**
     * 虚拟纹理数组句柄（全 0）
     */
    backend::Handle<backend::HwTexture> mDummyZeroTextureArray;
    
    /**
     * 虚拟纹理句柄（全 0）
     */
    backend::Handle<backend::HwTexture> mDummyZeroTexture;
    
    /**
     * 虚拟统一缓冲区句柄
     */
    backend::Handle<backend::HwBufferObject> mDummyUniformBuffer;

    /**
     * 主线程 ID
     */
    std::thread::id mMainThreadId{};

    /**
     * 是否已初始化
     */
    bool mInitialized = false;

    /**
     * 创建参数
     */
    Config mConfig;

public:
    // These are the debug properties used by FDebug.
    // They're accessed directly by modules who need them.
    struct {
        struct {
            bool debug_directional_shadowmap = false;
            bool display_shadow_texture = false;
            bool far_uses_shadowcasters = true;
            bool focus_shadowcasters = true;
            bool visualize_cascades = false;
            bool disable_light_frustum_align = false;
            bool depth_clamp = true;
            float dzn = -1.0f;
            float dzf =  1.0f;
            float display_shadow_texture_scale = 0.25f;
            int display_shadow_texture_layer = 0;
            int display_shadow_texture_level = 0;
            int display_shadow_texture_channel = 0;
            int display_shadow_texture_layer_count = 0;
            int display_shadow_texture_level_count = 0;
            float display_shadow_texture_power = 1;
        } shadowmap;
        struct {
            bool camera_at_origin = true;
            struct {
                float kp = 0.0f;
                float ki = 0.0f;
                float kd = 0.0f;
            } pid;
        } view;
        struct {
            // When set to true, the backend will attempt to capture the next frame and write the
            // capture to file. At the moment, only supported by the Metal backend.
            bool doFrameCapture = false;
            bool disable_buffer_padding = false;
            bool disable_subpasses = false;
        } renderer;
        struct {
            bool debug_froxel_visualization = false;
        } lighting;
        struct {
            bool combine_multiview_images = false;
        } stereo;
        matdbg::DebugServer* server = nullptr;
        fgviewer::DebugServer* fgviewerServer = nullptr;
    } debug;

    struct {
        struct {
            struct {
                bool use_1d_lut = false;
            } color_grading;
            struct {
                bool use_shadow_atlas = false;
            } shadows;
            struct {
                // TODO: clean-up the following flags (equivalent to setting them to true) when
                // clients have addressed their usages.
                bool assert_material_instance_in_use = CORRECTNESS_ASSERTION_DEFAULT;
                bool assert_destroy_material_before_material_instance =
                        CORRECTNESS_ASSERTION_DEFAULT;
                bool assert_vertex_buffer_count_exceeds_8 = CORRECTNESS_ASSERTION_DEFAULT;
                bool assert_vertex_buffer_attribute_stride_mult_of_4 =
                        CORRECTNESS_ASSERTION_DEFAULT;
                bool assert_material_instance_texture_descriptor_set_compatible =
                        CORRECTNESS_ASSERTION_DEFAULT;
                bool assert_texture_can_generate_mipmap = CORRECTNESS_ASSERTION_DEFAULT;
            } debug;
            struct {
                bool disable_gpu_frame_complete_metric = true;
            } frame_info;
        } engine;
        struct {
            struct {
                bool assert_native_window_is_valid = false;
            } opengl;
            struct {
                // On Unified Memory Architecture device, it is possible to bypass using the staging
                // buffer. This is an experimental feature that still needs to be implemented fully
                // before it can be fully enabled.
                bool enable_staging_buffer_bypass = false;
            } vulkan;
            bool disable_parallel_shader_compile = false;
            bool disable_amortized_shader_compile = true;
            bool disable_handle_use_after_free_check = false;
            bool disable_heap_handle_tags = true; // FIXME: this should be false
            bool enable_asynchronous_operation = false;
        } backend;
        struct {
            bool check_crc32_after_loading = false;
            bool enable_material_instance_uniform_batching = false;
        } material;
    } features;

    std::array<FeatureFlag, sizeof(features)> const mFeatures{{
            { "backend.disable_parallel_shader_compile",
              "Disable parallel shader compilation in GL and Metal backends.",
              &features.backend.disable_parallel_shader_compile, true },
            { "backend.disable_amortized_shader_compile",
              "Disable amortized shader compilation in GL backend.",
              &features.backend.disable_amortized_shader_compile, true },
            { "backend.disable_handle_use_after_free_check",
              "Disable Handle<> use-after-free checks.",
              &features.backend.disable_handle_use_after_free_check, true },
            { "backend.disable_heap_handle_tags",
              "Disable Handle<> tags for heap-allocated handles.",
              &features.backend.disable_heap_handle_tags, true },
            { "backend.enable_asynchronous_operation",
              "Enable asynchronous operation for resource management.",
              &features.backend.enable_asynchronous_operation, true },
            { "backend.opengl.assert_native_window_is_valid",
              "Asserts that the ANativeWindow is valid when rendering starts.",
              &features.backend.opengl.assert_native_window_is_valid, true },
            { "engine.color_grading.use_1d_lut",
              "Uses a 1D LUT for color grading.",
              &features.engine.color_grading.use_1d_lut, false },
            { "engine.shadows.use_shadow_atlas",
              "Uses an array of atlases to store shadow maps.",
              &features.engine.shadows.use_shadow_atlas, false },
            { "engine.debug.assert_material_instance_in_use",
              "Assert when a MaterialInstance is destroyed while it is in use by RenderableManager.",
              &features.engine.debug.assert_material_instance_in_use, false },
            { "engine.debug.assert_destroy_material_before_material_instance",
              "Assert when a Material is destroyed but its instances are still alive.",
              &features.engine.debug.assert_destroy_material_before_material_instance, false },
            { "engine.debug.assert_vertex_buffer_count_exceeds_8",
              "Assert when a client's number of buffers for a VertexBuffer exceeds 8.",
              &features.engine.debug.assert_vertex_buffer_count_exceeds_8, false },
            { "engine.debug.assert_vertex_buffer_attribute_stride_mult_of_4",
              "Assert that the attribute stride of a vertex buffer is a multiple of 4.",
              &features.engine.debug.assert_vertex_buffer_attribute_stride_mult_of_4, false },
            { "backend.vulkan.enable_staging_buffer_bypass",
              "vulkan: enable a staging bypass logic for unified memory architecture.",
              &features.backend.vulkan.enable_staging_buffer_bypass, false },
            { "engine.debug.assert_material_instance_texture_descriptor_set_compatible",
              "Assert that the textures in a material instance are compatible with descriptor set.",
              &features.engine.debug.assert_material_instance_texture_descriptor_set_compatible, false },
            { "engine.debug.assert_texture_can_generate_mipmap",
              "Assert if a texture has the correct usage set for generating mipmaps.",
              &features.engine.debug.assert_texture_can_generate_mipmap, false },
            { "material.check_crc32_after_loading",
              "Verify the checksum of package data when a material is loaded.",
              &features.material.check_crc32_after_loading, false },
            { "material.enable_material_instance_uniform_batching",
              "Make all MaterialInstances share a common large uniform buffer and use sub-allocations within it.",
              &features.material.enable_material_instance_uniform_batching, false },
            { "engine.frame_info.disable_gpu_complete_metric",
              "Disable Renderer::FrameInfo::gpuFrameComplete reporting",
              &features.engine.frame_info.disable_gpu_frame_complete_metric, false },
    }};

    utils::Slice<const FeatureFlag> getFeatureFlags() const noexcept {
        return { mFeatures.data(), mFeatures.size() };
    }

    bool setFeatureFlag(char const* name, bool value) const noexcept;
    std::optional<bool> getFeatureFlag(char const* name) const noexcept;
    bool* getFeatureFlagPtr(std::string_view name, bool allowConstant = false) const noexcept;
};

FILAMENT_DOWNCAST(Engine)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_ENGINE_H
