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

#include "details/Scene.h"

#include "Allocators.h"

#include "components/LightManager.h"
#include "components/RenderableManager.h"
#include "components/TransformManager.h"

#include "details/Engine.h"
#include "details/Skybox.h"

#include <backend/Handle.h>

#include <private/filament/UibStructs.h>

#include <private/utils/Tracing.h>

#include <filament/Box.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <filament/LightManager.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/Allocator.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/EntityManager.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Invocable.h>
#include <utils/JobSystem.h>
#include <utils/Range.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <functional>
#include <memory>
#include <utility>
#include <new>

#include <cstddef>

using namespace filament::backend;
using namespace filament::math;
using namespace utils;

namespace filament {

// ------------------------------------------------------------------------------------------------

/**
 * FScene 构造函数
 * 
 * 创建场景对象。场景是渲染对象的容器，包含可渲染对象、光源、天空盒等。
 * 
 * @param engine Engine 引用（用于访问组件管理器）
 */
FScene::FScene(FEngine& engine) :
        mEngine(engine) {  // 保存引擎引用
}

/**
 * FScene 析构函数
 * 
 * 场景析构是默认的，因为场景不拥有 Entity，Entity 由 EntityManager 管理。
 */
FScene::~FScene() noexcept = default;

/**
 * 准备场景渲染数据
 * 
 * 这是 Scene 的核心方法，在每帧渲染前调用。
 * 负责收集所有 Entity 的渲染数据，填充到 SoA（Structure of Arrays）结构中。
 * 
 * 执行流程：
 * 1. 收集所有 Entity 的 Renderable 和 Light 实例
 * 2. 查找主方向光（强度最大的）
 * 3. 调整 SoA 容量（对齐到 16 字节，便于 SIMD）
 * 4. 并行填充 RenderableSoA（世界变换、包围盒、可见性等）
 * 5. 并行填充 LightSoA（位置、方向、阴影信息等）
 * 
 * @param js JobSystem，用于并行处理
 * @param rootArenaScope 根内存池作用域
 * @param worldTransform 世界变换矩阵（用于变换所有对象）
 * @param shadowReceiversAreCasters 阴影接收者是否也是投射者（优化用）
 * 
 * TODO: 我们能否在大多数情况下跳过此操作？由于我们依赖索引保持不变，
 *       只有在 RCM 中没有任何变化时才能跳过。
 */
void FScene::prepare(JobSystem& js,
        RootArenaScope& rootArenaScope,
        mat4 const& worldTransform,
        bool shadowReceiversAreCasters) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);

    // 这将在退出时重置分配器
    ArenaScope localArenaScope(rootArenaScope.getArena());

    FEngine& engine = mEngine;
    EntityManager const& em = engine.getEntityManager();           // Entity 管理器
    FRenderableManager const& rcm = engine.getRenderableManager(); // Renderable 管理器
    FTransformManager const& tcm = engine.getTransformManager();  // Transform 管理器
    FLightManager const& lcm = engine.getLightManager();          // Light 管理器
    
    // 遍历 Entity 列表，收集那些是 Renderable 的数据
    auto& sceneData = mRenderableData;  // Renderable 数据 SoA
    auto& lightData = mLightData;       // Light 数据 SoA
    auto const& entities = mEntities;   // 场景中的所有 Entity

    /**
     * 定义临时容器类型
     * 
     * RenderableContainerData: Renderable 实例和 Transform 实例的配对
     * LightContainerData: Light 实例和 Transform 实例的配对
     * 
     * 这些容器使用线性分配器，在 prepare() 结束时自动释放。
     */
    using RenderableContainerData = std::pair<RenderableManager::Instance, TransformManager::Instance>;
    using RenderableInstanceContainer = FixedCapacityVector<RenderableContainerData,
            STLAllocator< RenderableContainerData, LinearAllocatorArena >, false>;

    using LightContainerData = std::pair<LightManager::Instance, TransformManager::Instance>;
    using LightInstanceContainer = FixedCapacityVector<LightContainerData,
            STLAllocator< LightContainerData, LinearAllocatorArena >, false>;

    // 预分配容器（容量等于 Entity 数量）
    RenderableInstanceContainer renderableInstances{
            RenderableInstanceContainer::with_capacity(entities.size(), localArenaScope.getArena()) };

    LightInstanceContainer lightInstances{
            LightInstanceContainer::with_capacity(entities.size(), localArenaScope.getArena()) };

    FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, "InstanceLoop");

    // 查找强度最大的方向光（场景中只使用一个主方向光）
    float maxIntensity = 0.0f;
    std::pair<LightManager::Instance, TransformManager::Instance> directionalLightInstances{};

    /**
     * 第一步：计算场景中确切的 Renderable 和 Light 数量
     * 同时查找主方向光
     * 
     * 遍历所有 Entity，收集：
     * - Renderable 实例（及其 Transform）
     * - Light 实例（及其 Transform）
     * - 方向光（单独处理，因为场景中只有一个主方向光）
     */
    for (Entity const e: entities) {
        if (UTILS_LIKELY(em.isAlive(e))) {  // Entity 必须存活
            auto ti = tcm.getInstance(e);  // Transform 实例
            auto li = lcm.getInstance(e);  // Light 实例（可能为空）
            auto ri = rcm.getInstance(e);  // Renderable 实例（可能为空）
            
            if (li) {
                // 我们在这里处理方向光，因为它会阻止下面的多线程处理
                if (UTILS_UNLIKELY(lcm.isDirectionalLight(li))) {
                    // 我们不存储所有方向光，因为只有一个主方向光
                    // 选择强度最大的方向光作为主方向光
                    if (lcm.getIntensity(li) >= maxIntensity) {
                        maxIntensity = lcm.getIntensity(li);
                        directionalLightInstances = { li, ti };
                    }
                } else {
                    // 非方向光：添加到 Light 实例列表
                    lightInstances.emplace_back(li, ti);
                }
            }
            if (ri) {
                // Renderable：添加到 Renderable 实例列表
                renderableInstances.emplace_back(ri, ti);
            }
        }
    }

    FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 第二步：评估 Renderable 和 Light SoA 所需的容量
     * 
     * 容量需要是 16 的倍数，以便使用 SIMD 指令优化循环。
     */

    // Renderable 数据容量：
    // - 需要是 16 的倍数（SIMD 优化）
    // - 末尾需要 1 个额外条目用于累积图元计数
    size_t renderableDataCapacity = entities.size();
    renderableDataCapacity = (renderableDataCapacity + 0xFu) & ~0xFu;  // 对齐到 16
    renderableDataCapacity = renderableDataCapacity + 1;  // 额外条目

    // Light 数据容量：
    // Light 数据列表将始终包含至少一个条目（主方向光），即使没有其他 Entity。
    // 需要是 16 的倍数（SIMD 优化）
    size_t lightDataCapacity = std::max<size_t>(DIRECTIONAL_LIGHTS_COUNT, entities.size());
    lightDataCapacity = (lightDataCapacity + 0xFu) & ~0xFu;  // 对齐到 16

    /**
     * 第三步：如果需要，调整 SoA 大小
     * 
     * TODO: 下面的调整大小操作可以在 Job 中执行
     */

    // 调整 RenderableSoA 大小
    if (!sceneData.capacity() || sceneData.size() != renderableInstances.size()) {
        sceneData.clear();  // 清空现有数据
        if (sceneData.capacity() < renderableDataCapacity) {
            sceneData.setCapacity(renderableDataCapacity);  // 扩展容量
        }
        assert_invariant(renderableInstances.size() <= sceneData.capacity());
        sceneData.resize(renderableInstances.size());  // 调整到实际大小
    }

    // 调整 LightSoA 大小
    // LightSoA 总是包含主方向光（DIRECTIONAL_LIGHTS_COUNT = 1）
    if (lightData.size() != lightInstances.size() + DIRECTIONAL_LIGHTS_COUNT) {
        lightData.clear();  // 清空现有数据
        if (lightData.capacity() < lightDataCapacity) {
            lightData.setCapacity(lightDataCapacity);  // 扩展容量
        }
        assert_invariant(lightInstances.size() + DIRECTIONAL_LIGHTS_COUNT <= lightData.capacity());
        lightData.resize(lightInstances.size() + DIRECTIONAL_LIGHTS_COUNT);  // 调整到实际大小（包含方向光）
    }

    /**
     * 第四步：使用 JobSystem 并行填充 SoA
     * 
     * 定义两个工作函数：
     * - renderableWork: 填充 RenderableSoA
     * - lightWork: 填充 LightSoA
     */

    /**
     * Renderable 工作函数
     * 
     * 并行处理一批 Renderable 实例，填充 RenderableSoA。
     * 
     * @param p 当前批次的起始指针
     * @param c 当前批次的数量
     */
    auto renderableWork = [first = renderableInstances.data(), &rcm, &tcm, &worldTransform,
                 &sceneData, shadowReceiversAreCasters](auto* p, auto c) {
        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "renderableWork");

        for (size_t i = 0; i < c; i++) {
            auto [ri, ti] = p[i];  // 解构：Renderable 实例和 Transform 实例

            // 这里我们将双精度变换转换为单精度（用于着色器）
            // 组合世界变换和 Entity 的世界变换
            const mat4f shaderWorldTransform{
                    worldTransform * tcm.getWorldTransformAccurate(ti) };
            
            // 检测是否反转了绕序（负行列式表示镜像变换）
            const bool reversedWindingOrder = det(shaderWorldTransform.upperLeft()) < 0;

            // 计算世界空间包围盒（用于视锥剔除）
            const Box worldAABB = rigidTransform(rcm.getAABB(ri), shaderWorldTransform);

            // 获取可见性状态并更新
            auto visibility = rcm.getVisibility(ri);
            visibility.reversedWindingOrder = reversedWindingOrder;  // 设置绕序反转标志
            // 如果阴影接收者也是投射者，自动启用阴影投射
            if (shadowReceiversAreCasters && visibility.receiveShadows) {
                visibility.castShadows = true;
            }

            // FIXME: 我们计算并存储局部缩放，因为 glTF 需要它
            //        但我们需要更好的方式来处理这个问题
            const mat4f& transform = tcm.getTransform(ti);
            // 计算平均缩放（三个轴的平均值）
            float const scale = (length(transform[0].xyz) + length(transform[1].xyz) +
                                 length(transform[2].xyz)) / 3.0f;

            // 计算在 SoA 中的索引
            size_t const index = std::distance(first, p) + i;
            assert_invariant(index < sceneData.size());

            // 填充 RenderableSoA 的各个字段
            sceneData.elementAt<RENDERABLE_INSTANCE>(index) = ri;                    // Renderable 实例
            sceneData.elementAt<WORLD_TRANSFORM>(index)     = shaderWorldTransform; // 世界变换矩阵
            sceneData.elementAt<VISIBILITY_STATE>(index)    = visibility;           // 可见性状态
            sceneData.elementAt<SKINNING_BUFFER>(index)     = rcm.getSkinningBufferInfo(ri);  // 骨骼动画缓冲区信息
            sceneData.elementAt<MORPHING_BUFFER>(index)     = rcm.getMorphingBufferInfo(ri);  // 变形动画缓冲区信息
            sceneData.elementAt<INSTANCES>(index)           = rcm.getInstancesInfo(ri);       // 实例化信息
            sceneData.elementAt<WORLD_AABB_CENTER>(index)   = worldAABB.center;              // 世界空间包围盒中心
            sceneData.elementAt<VISIBLE_MASK>(index)        = 0;                              // 可见性掩码（初始化为 0）
            sceneData.elementAt<CHANNELS>(index)            = rcm.getChannels(ri);            // 渲染通道
            sceneData.elementAt<LAYERS>(index)              = rcm.getLayerMask(ri);           // 层掩码
            sceneData.elementAt<WORLD_AABB_EXTENT>(index)   = worldAABB.halfExtent;          // 世界空间包围盒半尺寸
            //sceneData.elementAt<PRIMITIVES>(index)          = {}; // 已初始化，Slice<>
            sceneData.elementAt<SUMMED_PRIMITIVE_COUNT>(index) = 0;  // 累积图元计数（初始化为 0）
            //sceneData.elementAt<UBO>(index)                 = {}; // 这里不需要
            sceneData.elementAt<USER_DATA>(index)           = scale;  // 用户数据（存储缩放）
        }
    };

    /**
     * Light 工作函数
     * 
     * 并行处理一批 Light 实例，填充 LightSoA。
     * 
     * @param p 当前批次的起始指针
     * @param c 当前批次的数量
     */
    auto lightWork = [first = lightInstances.data(), &lcm, &tcm, &worldTransform,
            &lightData](auto* p, auto c) {
        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "lightWork");
        for (size_t i = 0; i < c; i++) {
            auto [li, ti] = p[i];  // 解构：Light 实例和 Transform 实例
            
            // 这里我们将双精度变换转换为单精度（用于着色器）
            mat4f const shaderWorldTransform{
                    worldTransform * tcm.getWorldTransformAccurate(ti) };
            
            // 计算世界空间位置
            float4 const position = shaderWorldTransform * float4{ lcm.getLocalPosition(li), 1 };
            
            // 计算方向（点光源和 IES 光源没有方向）
            float3 d = 0;
            if (!lcm.isPointLight(li) || lcm.isIESLight(li)) {
                d = lcm.getLocalDirection(li);
                // 使用 mat3f::getTransformForNormals 处理非均匀缩放
                d = normalize(mat3f::getTransformForNormals(shaderWorldTransform.upperLeft()) * d);
            }
            
            // 计算在 LightSoA 中的索引（跳过方向光的位置）
            size_t const index = DIRECTIONAL_LIGHTS_COUNT + std::distance(first, p) + i;
            assert_invariant(index < lightData.size());
            
            // 填充 LightSoA 的各个字段
            lightData.elementAt<POSITION_RADIUS>(index) = float4{ position.xyz, lcm.getRadius(li) };  // 位置和半径
            lightData.elementAt<DIRECTION>(index) = d;                                                // 方向
            lightData.elementAt<LIGHT_INSTANCE>(index) = li;                                          // Light 实例
        }
    };


    FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, "Renderable and Light jobs");

    /**
     * 第五步：并行执行工作函数
     * 
     * 创建并行 Job 来填充 RenderableSoA 和 LightSoA。
     * 这些 Job 可以并行执行，因为它们操作不同的数据。
     */
    
    // 创建根 Job（所有并行 Job 的父节点）
    JobSystem::Job* rootJob = js.createJob();

    // 创建 Renderable 并行 Job
    // CountSplitter<64>: 每批处理 64 个 Renderable
    auto* renderableJob = parallel_for(js, rootJob,
            renderableInstances.data(), renderableInstances.size(),
            std::cref(renderableWork), jobs::CountSplitter<64>());

    // 创建 Light 并行 Job
    // CountSplitter<32, 5>: 每批处理 32 个 Light，最多 5 个批次并行
    auto* lightJob = parallel_for(js, rootJob,
            lightInstances.data(), lightInstances.size(),
            std::cref(lightWork), jobs::CountSplitter<32, 5>());

    // 启动并行 Job（它们会并行执行）
    js.run(renderableJob);
    js.run(lightJob);

    // 下面的所有操作都可以并行执行。

    /**
     * 第六步：单独处理方向光
     * 
     * 方向光总是存储在 LightSoA 的第一个位置（索引 0）。
     * 方向光需要特殊处理，因为：
     * - 它没有位置（或位置在无穷远）
     * - 需要计算阴影贴图的参考点
     * - 需要处理阴影方向变换
     */
    if (auto [li, ti] = directionalLightInstances ; li) {
        // 在下面的代码中，我们只变换方向，所以世界变换的平移部分无关紧要，
        // 不需要使用 getWorldTransformAccurate()
        
        // 计算方向变换矩阵（处理非均匀缩放）
        mat3 const worldDirectionTransform =
                mat3::getTransformForNormals(tcm.getWorldTransformAccurate(ti).upperLeft());
        FLightManager::ShadowParams const params = lcm.getShadowParams(li);
        float3 const localDirection = worldDirectionTransform * lcm.getLocalDirection(li);
        double3 const shadowLocalDirection = params.options.transform * localDirection;

        // 使用 mat3::getTransformForNormals 处理非均匀缩放
        // 注意：在常见的刚体变换情况下，getTransformForNormals() 返回单位矩阵
        mat3 const worlTransformNormals = mat3::getTransformForNormals(worldTransform.upperLeft());
        double3 const d = worlTransformNormals * localDirection;        // 世界空间方向
        double3 const s = worlTransformNormals * shadowLocalDirection; // 阴影方向

        // 我们计算阴影贴图捕捉的参考点，而不在两边应用 `worldOriginTransform` 的旋转，
        // 这样就不会因为"光空间"矩阵的有限精度（即使在双精度下）而产生不稳定性

        // getMv() 返回世界空间到光空间的变换。参见 ShadowMap.cpp。
        auto getMv = [](double3 direction) -> mat3 {
            // 我们使用 x 轴作为"上"参考，这样当光指向下方时数学是稳定的，
            // 这是光源的常见情况。参见 ShadowMap.cpp。
            return transpose(mat3::lookTo(direction, double3{ 1, 0, 0 }));
        };
        double3 const worldOrigin = transpose(worldTransform.upperLeft()) * worldTransform[3].xyz;
        mat3 const Mv = getMv(shadowLocalDirection);
        double2 const lsReferencePoint = (Mv * worldOrigin).xy;  // 光空间参考点

        // 填充方向光数据到 LightSoA 的第一个位置（索引 0）
        constexpr float inf = std::numeric_limits<float>::infinity();
        lightData.elementAt<POSITION_RADIUS>(0) = float4{ 0, 0, 0, inf };  // 位置在无穷远
        lightData.elementAt<DIRECTION>(0) = normalize(d);                  // 方向
        lightData.elementAt<SHADOW_DIRECTION>(0) = normalize(s);            // 阴影方向
        lightData.elementAt<SHADOW_REF>(0) = lsReferencePoint;             // 阴影参考点
        lightData.elementAt<LIGHT_INSTANCE>(0) = li;                       // Light 实例
    } else {
        // 没有方向光：将第一个位置标记为空
        lightData.elementAt<LIGHT_INSTANCE>(0) = 0;
    }

    /**
     * 第七步：初始化 SoA 的未使用部分
     * 
     * 数组末尾的一些元素会被 SIMD 代码访问，我们需要确保数据足够有效，
     * 以避免产生错误（如除零，例如在 computeLightRanges() 中）。
     */
    for (size_t i = lightData.size(), e = lightData.capacity(); i < e; i++) {
        new(lightData.data<POSITION_RADIUS>() + i) float4{ 0, 0, 0, 1 };
    }

    // 纯粹为了 MSAN（内存清理器）的好处，我们可以通过清零数组末尾和
    // 向上取整计数之间的未使用场景元素来避免未初始化读取。
    if constexpr (UTILS_HAS_SANITIZE_MEMORY) {
        for (size_t i = sceneData.size(), e = sceneData.capacity(); i < e; i++) {
            sceneData.data<LAYERS>()[i] = 0;
            sceneData.data<VISIBLE_MASK>()[i] = 0;
            sceneData.data<VISIBILITY_STATE>()[i] = {};
        }
    }

    /**
     * 第八步：等待所有并行 Job 完成
     * 
     * 此时 RenderableSoA 和 LightSoA 已完全填充，可以用于渲染。
     */
    js.runAndWait(rootJob);

    FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);
}

/**
 * 准备可见 Renderable 的 UBO 数据
 * 
 * 在视锥剔除后调用，为可见的 Renderable 准备 Uniform Buffer Object 数据。
 * 这些数据会被上传到 GPU，供着色器使用。
 * 
 * @param visibleRenderables 可见 Renderable 的索引范围
 */
void FScene::prepareVisibleRenderables(Range<uint32_t> visibleRenderables) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    RenderableSoa& sceneData = mRenderableData;
    FRenderableManager const& rcm = mEngine.getRenderableManager();

    mHasContactShadows = false;  // 重置接触阴影标志
    for (uint32_t const i : visibleRenderables) {
        PerRenderableData& uboData = sceneData.elementAt<UBO>(i);  // UBO 数据引用

        // 从 SoA 中获取数据
        auto const visibility = sceneData.elementAt<VISIBILITY_STATE>(i);  // 可见性状态
        auto const& model = sceneData.elementAt<WORLD_TRANSFORM>(i);       // 世界变换矩阵
        auto const ri = sceneData.elementAt<RENDERABLE_INSTANCE>(i);       // Renderable 实例

        /**
         * 计算法线变换矩阵
         * 
         * 使用 mat3f::getTransformForNormals 处理非均匀缩放，但不保证
         * 变换后的法线具有单位长度，因此需要在着色器中归一化
         * （无论如何都需要归一化，因为插值后需要归一化）。
         * 
         * 我们预先按最大缩放因子的倒数缩放法线，以避免着色器中
         * 变换后的大幅度值，特别是在片段着色器中，我们使用中等精度。
         * 
         * 注意：如果已知模型矩阵是刚体变换，我们可以直接使用它。
         */
        mat3f m = mat3f::getTransformForNormals(model.upperLeft());
        m = prescaleForNormals(m);  // 预缩放法线

        /**
         * 镜像变换的法线翻转
         * 
         * 对于镜像变换，着色法线必须翻转。
         * 基本上我们在多边形的另一侧着色，因此需要取反法线，
         * 类似于我们为支持双面光照所做的操作。
         */
        if (visibility.reversedWindingOrder) {
            m = -m;
        }

        // 填充 UBO 数据
        uboData.worldFromModelMatrix = model;        // 世界到模型矩阵
        uboData.worldFromModelNormalMatrix = m;      // 法线变换矩阵

        // 打包标志和通道（用于着色器分支优化）
        uboData.flagsChannels = PerRenderableData::packFlagsChannels(
                visibility.skinning,                                    // 是否使用骨骼动画
                visibility.morphing,                                    // 是否使用变形动画
                visibility.screenSpaceContactShadows,                   // 是否使用屏幕空间接触阴影
                sceneData.elementAt<INSTANCES>(i).buffer != nullptr,    // 是否使用实例化
                sceneData.elementAt<CHANNELS>(i));                     // 渲染通道

        uboData.morphTargetCount = sceneData.elementAt<MORPHING_BUFFER>(i).count;  // 变形目标数量
        uboData.objectId = rcm.getEntity(ri).getId();                              // 对象 ID（用于拾取）
        
        // TODO: 我们需要找到更好的方式来提供每个对象的缩放信息
        uboData.userData = sceneData.elementAt<USER_DATA>(i);  // 用户数据（当前存储缩放）

        // 更新接触阴影标志
        mHasContactShadows = mHasContactShadows || visibility.screenSpaceContactShadows;
    }
}

void FScene::terminate(FEngine&) {
}

/**
 * 准备动态光照数据
 * 
 * 将 LightSoA 中的数据复制到 GPU 缓冲区，供着色器使用。
 * 只处理点光源和聚光灯（方向光单独处理）。
 * 
 * @param camera 相机信息（用于计算光照的屏幕空间 Z 范围）
 * @param lightUbh 光照 Uniform Buffer Handle
 */
void FScene::prepareDynamicLights(const CameraInfo& camera,
        Handle<HwBufferObject> lightUbh) noexcept {
    FEngine::DriverApi& driver = mEngine.getDriverApi();
    FLightManager const& lcm = mEngine.getLightManager();
    LightSoa& lightData = getLightData();

    /**
     * 将光照数据复制到 GPU 缓冲区
     */

    size_t const size = lightData.size();
    // 点光源/聚光灯的数量（排除方向光）
    size_t const positionalLightCount = size - DIRECTIONAL_LIGHTS_COUNT;
    assert_invariant(positionalLightCount);

    float4 const* const UTILS_RESTRICT spheres = lightData.data<POSITION_RADIUS>();

    // compute the light ranges (needed when building light trees)
    float2* const zrange = lightData.data<SCREEN_SPACE_Z_RANGE>();
    computeLightRanges(zrange, camera, spheres + DIRECTIONAL_LIGHTS_COUNT, positionalLightCount);

    LightsUib* const lp = driver.allocatePod<LightsUib>(positionalLightCount);

    auto const* UTILS_RESTRICT directions       = lightData.data<DIRECTION>();
    auto const* UTILS_RESTRICT instances        = lightData.data<LIGHT_INSTANCE>();
    auto const* UTILS_RESTRICT shadowInfo       = lightData.data<SHADOW_INFO>();
    for (size_t i = DIRECTIONAL_LIGHTS_COUNT, c = size; i < c; ++i) {
        const size_t gpuIndex = i - DIRECTIONAL_LIGHTS_COUNT;
        auto const li = instances[i];
        lp[gpuIndex].positionFalloff      = { spheres[i].xyz, lcm.getSquaredFalloffInv(li) };
        lp[gpuIndex].direction            = directions[i];
        lp[gpuIndex].reserved1            = {};
        lp[gpuIndex].colorIES             = { lcm.getColor(li), 0.0f };
        lp[gpuIndex].spotScaleOffset      = lcm.getSpotParams(li).scaleOffset;
        lp[gpuIndex].reserved3            = {};
        lp[gpuIndex].intensity            = lcm.getIntensity(li);
        lp[gpuIndex].typeShadow           = LightsUib::packTypeShadow(
                lcm.isPointLight(li) ? 0u : 1u,
                shadowInfo[i].contactShadows,
                shadowInfo[i].index);
        lp[gpuIndex].channels             = LightsUib::packChannels(
                lcm.getLightChannels(li),
                shadowInfo[i].castsShadows);
    }

    driver.updateBufferObject(lightUbh, { lp, positionalLightCount * sizeof(LightsUib) }, 0);
}

// These methods need to exist so clang honors the __restrict__ keyword, which in turn
// produces much better vectorization. The ALWAYS_INLINE keyword makes sure we actually don't
// pay the price of the call!
UTILS_ALWAYS_INLINE
inline void FScene::computeLightRanges(
        float2* UTILS_RESTRICT const zrange,
        CameraInfo const& UTILS_RESTRICT camera,
        float4 const* UTILS_RESTRICT const spheres, size_t count) noexcept {

    // without this clang seems to assume the src and dst might overlap even if they're
    // restricted.
    // we're guaranteed to have a multiple of 4 lights (at least)
    count = uint32_t(count + 3u) & ~3u;

    for (size_t i = 0 ; i < count; i++) {
        // this loop gets vectorized x4
        const float4 sphere = spheres[i];
        const float4 center = camera.view * sphere.xyz; // camera points towards the -z axis
        float4 n = center + float4{ 0, 0, sphere.w, 0 };
        float4 f = center - float4{ 0, 0, sphere.w, 0 };
        // project to clip space
        n = camera.projection * n;
        f = camera.projection * f;
        // convert to NDC
        const float min = (n.w > camera.zn) ? (n.z / n.w) : -1.0f;
        const float max = (f.w < camera.zf) ? (f.z / f.w) :  1.0f;
        // convert to screen space
        zrange[i].x = (min + 1.0f) * 0.5f;
        zrange[i].y = (max + 1.0f) * 0.5f;
    }
}

/**
 * 添加 Entity 到场景
 * 
 * @param entity 要添加的 Entity
 * 
 * 注意：Entity 必须具有 Renderable 或 Light 组件才能被渲染。
 * 同一个 Entity 只能添加一次。
 */
UTILS_NOINLINE
void FScene::addEntity(Entity const entity) {
    mEntities.insert(entity);  // 使用 robin_set 插入（O(1) 平均时间复杂度）
}

/**
 * 批量添加 Entity 到场景
 * 
 * @param entities Entity 数组
 * @param count Entity 数量
 */
UTILS_NOINLINE
void FScene::addEntities(const Entity* entities, size_t const count) {
    mEntities.insert(entities, entities + count);  // 批量插入
}

/**
 * 从场景移除 Entity
 * 
 * @param entity 要移除的 Entity
 * 
 * 如果 Entity 不存在，此调用会被忽略。
 */
UTILS_NOINLINE
void FScene::remove(Entity const entity) {
    mEntities.erase(entity);  // 使用 robin_set 删除（O(1) 平均时间复杂度）
}

/**
 * 批量从场景移除 Entity
 * 
 * @param entities Entity 数组
 * @param count Entity 数量
 * 
 * 这等价于循环调用 remove()。
 * 如果任何指定的 Entity 不在场景中，它们会被跳过。
 */
UTILS_NOINLINE
void FScene::removeEntities(const Entity* entities, size_t const count) {
    for (size_t i = 0; i < count; ++i, ++entities) {
        remove(*entities);
    }
}

/**
 * 移除场景中的所有 Entity
 */
UTILS_NOINLINE
void FScene::removeAllEntities() noexcept {
    mEntities.clear();  // 清空 Entity 集合
}

UTILS_NOINLINE
size_t FScene::getRenderableCount() const noexcept {
    FEngine& engine = mEngine;
    EntityManager const& em = engine.getEntityManager();
    FRenderableManager const& rcm = engine.getRenderableManager();
    size_t count = 0;
    auto const& entities = mEntities;
    for (Entity const e : entities) {
        count += em.isAlive(e) && rcm.getInstance(e) ? 1 : 0;
    }
    return count;
}

UTILS_NOINLINE
size_t FScene::getLightCount() const noexcept {
    FEngine& engine = mEngine;
    EntityManager const& em = engine.getEntityManager();
    FLightManager const& lcm = engine.getLightManager();
    size_t count = 0;
    auto const& entities = mEntities;
    for (Entity const e : entities) {
        count += em.isAlive(e) && lcm.getInstance(e) ? 1 : 0;
    }
    return count;
}

UTILS_NOINLINE
bool FScene::hasEntity(Entity const entity) const noexcept {
    return mEntities.find(entity) != mEntities.end();
}

UTILS_NOINLINE
void FScene::setSkybox(FSkybox* skybox) noexcept {
    std::swap(mSkybox, skybox);
    if (skybox) {
        remove(skybox->getEntity());
    }
    if (mSkybox) {
        addEntity(mSkybox->getEntity());
    }
}

bool FScene::hasContactShadows() const noexcept {
    // at least some renderables in the scene must have contact-shadows enabled
    // TODO: we should refine this with only the visible ones
    if (!mHasContactShadows) {
        return false;
    }

    // find out if at least one light has contact-shadow enabled
    // TODO: we could cache the result of this Loop in the LightManager
    auto const& lcm = mEngine.getLightManager();
    const auto *pFirst = mLightData.begin<LIGHT_INSTANCE>();
    const auto *pLast = mLightData.end<LIGHT_INSTANCE>();
    while (pFirst != pLast) {
        if (pFirst->isValid()) {
            auto const& shadowOptions = lcm.getShadowOptions(*pFirst);
            if (shadowOptions.screenSpaceContactShadows) {
                return true;
            }
        }
        ++pFirst;
    }
    return false;
}

UTILS_NOINLINE
void FScene::forEach(Invocable<void(Entity)>&& functor) const noexcept {
    std::for_each(mEntities.begin(), mEntities.end(), std::move(functor));
}

} // namespace filament
