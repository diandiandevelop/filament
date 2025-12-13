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

#ifndef TNT_FILAMENT_DETAILS_SCENE_H
#define TNT_FILAMENT_DETAILS_SCENE_H

#include "downcast.h"

#include "Allocators.h"
#include "Culler.h"

#include "ds/DescriptorSet.h"

#include "components/LightManager.h"
#include "components/RenderableManager.h"

#include <filament/Scene.h>

#include <utils/Entity.h>
#include <utils/Slice.h>
#include <utils/StructureOfArrays.h>
#include <utils/Range.h>

#include <stddef.h>

#include <tsl/robin_set.h>

namespace filament {

struct CameraInfo;
class FEngine;
class FIndirectLight;
class FRenderer;
class FSkybox;

/**
 * 场景实现类
 * 
 * 管理场景中的所有实体、光源、天空盒和间接光。
 * 场景是渲染的容器，包含所有需要渲染的对象。
 * 
 * 实现细节：
 * - 使用 StructureOfArrays (SoA) 存储可渲染对象和光源数据
 * - 支持每帧准备可见对象和光源数据
 * - 管理实体的添加和移除
 */
class FScene : public Scene {
public:
    /*
     * Filament-scope Public API
     */

    /**
     * 获取天空盒
     * 
     * @return 天空盒指针（如果没有则返回 nullptr）
     */
    FSkybox* getSkybox() const noexcept { return mSkybox; }

    /**
     * 获取间接光
     * 
     * @return 间接光指针（如果没有则返回 nullptr）
     */
    FIndirectLight* getIndirectLight() const noexcept { return mIndirectLight; }

    /**
     * 定向光源数量
     * 
     * 定向光源总是存储在 LightSoA 的第一位，所以我们需要在几个地方考虑这一点。
     */
    // the directional light is always stored first in the LightSoA, so we need to account
    // for that in a few places.
    static constexpr size_t DIRECTIONAL_LIGHTS_COUNT = 1;  // 定向光源数量

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FScene(FEngine& engine);
    
    /**
     * 析构函数
     */
    ~FScene() noexcept;
    
    /**
     * 终止场景
     * 
     * 清理资源。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 准备场景
     * 
     * 准备场景数据用于渲染，包括：
     * - 计算世界变换
     * - 准备可见对象
     * - 准备光源数据
     * 
     * @param js 作业系统引用
     * @param rootArenaScope 根内存池作用域
     * @param worldTransform 世界变换矩阵
     * @param shadowReceiversAreCasters 阴影接收者是否也是投射者
     */
    void prepare(utils::JobSystem& js, RootArenaScope& rootArenaScope,
            math::mat4 const& worldTransform, bool shadowReceiversAreCasters) noexcept;

    /**
     * 准备可见可渲染对象
     * 
     * 为可见的可渲染对象准备数据。
     * 
     * @param visibleRenderables 可见可渲染对象的范围
     */
    void prepareVisibleRenderables(utils::Range<uint32_t> visibleRenderables) noexcept;

    /**
     * 准备动态光源
     * 
     * 准备动态光源数据并更新 Uniform 缓冲区。
     * 
     * @param camera 相机信息
     * @param lightUbh 光源 Uniform 缓冲区句柄
     */
    void prepareDynamicLights(const CameraInfo& camera,
            backend::Handle<backend::HwBufferObject> lightUbh) noexcept;

    /*
     * 每帧可渲染对象数据存储
     */
    /**
     * Storage for per-frame renderable data
     */

    using VisibleMaskType = Culler::result_type;  // 可见掩码类型别名

    /**
     * 可渲染对象 SoA 字段枚举
     * 
     * 定义 StructureOfArrays 中每个字段的索引和大小。
     */
    enum {
        RENDERABLE_INSTANCE,    //   4 | 可渲染对象组件的实例
        WORLD_TRANSFORM,        //  16 | 变换组件的实例（世界变换矩阵）
        VISIBILITY_STATE,       //   2 | 组件的可见性数据
        SKINNING_BUFFER,        //   8 | 骨骼 uniform 缓冲区句柄、偏移量、索引和权重
        MORPHING_BUFFER,        //  16 | 权重 uniform 缓冲区句柄、数量、变形目标
        INSTANCES,              //  16 | 此可渲染对象的实例化信息
        WORLD_AABB_CENTER,      //  12 | 可渲染对象的世界空间包围盒中心
        VISIBLE_MASK,           //   2 | 每个位表示一个通道中的可见性
        CHANNELS,               //   1 | 当前仅光源通道

        /**
         * 这些在剔除后不再需要
         */
        // These are not needed anymore after culling
        LAYERS,                 //   1 | 层
        WORLD_AABB_EXTENT,      //  12 | 可渲染对象的世界空间包围盒半范围

        /**
         * 这些是临时数据，应该存储在线外
         */
        // These are temporaries and should be stored out of line
        PRIMITIVES,             //   8 | 细节级别化的图元
        SUMMED_PRIMITIVE_COUNT, //   4 | 累加的可见图元数量
        UBO,                    // 128 | Uniform 缓冲区对象数据
        DESCRIPTOR_SET_HANDLE,  // 描述符堆句柄

        /**
         * FIXME: 我们需要更好的方式来处理这个
         */
        // FIXME: We need a better way to handle this
        USER_DATA,              //   4 | 用户数据（当前用于存储缩放）
    };

    using RenderableSoa = utils::StructureOfArrays<
            utils::EntityInstance<RenderableManager>,   // RENDERABLE_INSTANCE
            math::mat4f,                                // WORLD_TRANSFORM
            FRenderableManager::Visibility,             // VISIBILITY_STATE
            FRenderableManager::SkinningBindingInfo,    // SKINNING_BUFFER
            FRenderableManager::MorphingBindingInfo,    // MORPHING_BUFFER
            FRenderableManager::InstancesInfo,          // INSTANCES
            math::float3,                               // WORLD_AABB_CENTER
            VisibleMaskType,                            // VISIBLE_MASK
            uint8_t,                                    // CHANNELS
            uint8_t,                                    // LAYERS
            math::float3,                               // WORLD_AABB_EXTENT
            utils::Slice<const FRenderPrimitive>,       // PRIMITIVES
            uint32_t,                                   // SUMMED_PRIMITIVE_COUNT
            PerRenderableData,                          // UBO
            backend::DescriptorSetHandle,               // DESCRIPTOR_SET_HANDLE
            // FIXME: We need a better way to handle this
            float                                       // USER_DATA
    >;

    RenderableSoa const& getRenderableData() const noexcept { return mRenderableData; }
    RenderableSoa& getRenderableData() noexcept { return mRenderableData; }

    static uint32_t getPrimitiveCount(RenderableSoa const& soa,
            uint32_t const first, uint32_t const last) noexcept {
        // the caller must guarantee that last is dereferenceable
        return soa.elementAt<SUMMED_PRIMITIVE_COUNT>(last) -
                soa.elementAt<SUMMED_PRIMITIVE_COUNT>(first);
    }

    static uint32_t getPrimitiveCount(RenderableSoa const& soa, uint32_t const last) noexcept {
        // the caller must guarantee that last is dereferenceable
        return soa.elementAt<SUMMED_PRIMITIVE_COUNT>(last);
    }

    /*
     * 每帧光源数据存储
     */
    /**
     * Storage for per-frame light data
     */

    /**
     * 阴影信息结构
     * 
     * 存储每个光源的阴影相关信息。
     * 这些值被打包成 32 位并存储在 Lights uniform 缓冲区中。
     * 它们在片段着色器中解包并用于计算点光源阴影。
     */
    struct ShadowInfo {
        // These are per-light values.
        // They're packed into 32 bits and stored in the Lights uniform buffer.
        // They're unpacked in the fragment shader and used to calculate punctual shadows.
        bool castsShadows = false;      // 此光源是否投射阴影
        bool contactShadows = false;    // 此光源是否投射接触阴影
        uint8_t index = 0;              // Shadows uniform 缓冲区中数组的索引
    };

    /**
     * 光源 SoA 字段枚举
     * 
     * 定义 StructureOfArrays 中每个字段的索引。
     */
    enum {
        POSITION_RADIUS,        // 位置和半径（float4）
        DIRECTION,              // 方向（float3）
        SHADOW_DIRECTION,       // 阴影方向（float3）
        SHADOW_REF,             // 阴影参考（double2）
        LIGHT_INSTANCE,         // 光源实例
        VISIBILITY,             // 可见性
        SCREEN_SPACE_Z_RANGE,   // 屏幕空间 Z 范围（float2）
        SHADOW_INFO             // 阴影信息
    };

    using LightSoa = utils::StructureOfArrays<
            math::float4,
            math::float3,
            math::float3,
            math::double2,
            FLightManager::Instance,
            Culler::result_type,
            math::float2,
            ShadowInfo
    >;

    LightSoa const& getLightData() const noexcept { return mLightData; }
    LightSoa& getLightData() noexcept { return mLightData; }

    /**
     * 检查是否有接触阴影
     * 
     * @return 如果有接触阴影返回 true，否则返回 false
     */
    bool hasContactShadows() const noexcept;

private:
    friend class Scene;  // 允许 Scene 访问私有成员
    
    /**
     * 设置天空盒
     * 
     * @param skybox 天空盒指针
     */
    void setSkybox(FSkybox* skybox) noexcept;
    
    /**
     * 设置间接光
     * 
     * @param ibl 间接光指针
     */
    void setIndirectLight(FIndirectLight* ibl) noexcept { mIndirectLight = ibl; }
    
    /**
     * 添加实体
     * 
     * @param entity 实体
     */
    void addEntity(utils::Entity entity);
    
    /**
     * 添加多个实体
     * 
     * @param entities 实体数组指针
     * @param count 实体数量
     */
    void addEntities(const utils::Entity* entities, size_t count);
    
    /**
     * 移除实体
     * 
     * @param entity 实体
     */
    void remove(utils::Entity entity);
    
    /**
     * 移除多个实体
     * 
     * @param entities 实体数组指针
     * @param count 实体数量
     */
    void removeEntities(const utils::Entity* entities, size_t count);
    
    /**
     * 移除所有实体
     */
    void removeAllEntities() noexcept;
    
    /**
     * 获取实体数量
     * 
     * @return 实体数量
     */
    size_t getEntityCount() const noexcept { return mEntities.size(); }
    
    /**
     * 获取可渲染对象数量
     * 
     * @return 可渲染对象数量
     */
    size_t getRenderableCount() const noexcept;
    
    /**
     * 获取光源数量
     * 
     * @return 光源数量
     */
    size_t getLightCount() const noexcept;
    
    /**
     * 检查是否有实体
     * 
     * @param entity 实体
     * @return 如果有实体返回 true，否则返回 false
     */
    bool hasEntity(utils::Entity entity) const noexcept;
    
    /**
     * 遍历所有实体
     * 
     * @param functor 函数对象
     */
    void forEach(utils::Invocable<void(utils::Entity)>&& functor) const noexcept;

    /**
     * 计算光源范围
     * 
     * 计算光源的屏幕空间 Z 范围。
     * 
     * @param zrange 输出 Z 范围数组
     * @param camera 相机信息
     * @param spheres 球体数组（位置和半径）
     * @param count 数量
     */
    static inline void computeLightRanges(math::float2* zrange,
            CameraInfo const& camera, const math::float4* spheres, size_t count) noexcept;

    FEngine& mEngine;  // 引擎引用
    FSkybox* mSkybox = nullptr;  // 天空盒指针
    FIndirectLight* mIndirectLight = nullptr;  // 间接光指针

    /**
     * 场景中的实体列表
     * 
     * 我们使用 robin_set<> 以便进行高效的移除操作
     * （vector<> 也可以工作，但移除操作是 O(n)）。
     * robin_set<> 的迭代几乎和 vector<> 一样好，这是一个很好的折衷。
     */
    /*
     * list of Entities in the scene. We use a robin_set<> so we can do efficient removes
     * (a vector<> could work, but removes would be O(n)). robin_set<> iterates almost as
     * nicely as vector<>, which is a good compromise.
     */
    tsl::robin_set<utils::Entity, utils::Entity::Hasher> mEntities;  // 实体集合


    /**
     * 下面的数据仅在视图通道期间有效。
     * 即，如果一个场景在多个视图中使用，下面的数据会为每个视图更新。
     * 本质上，这些数据应该由 View 拥有，但由于它们非常特定于场景，
     * 目前我们在这里存储它们。
     */
    /*
     * The data below is valid only during a view pass. i.e. if a scene is used in multiple
     * views, the data below is updated for each view.
     * In essence, this data should be owned by View, but it's so scene-specific, that for now
     * we store it here.
     */
    RenderableSoa mRenderableData;  // 可渲染对象数据（SoA）
    LightSoa mLightData;  // 光源数据（SoA）
    bool mHasContactShadows = false;  // 是否有接触阴影
};

FILAMENT_DOWNCAST(Scene)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_SCENE_H
