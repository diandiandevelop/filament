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

#ifndef TNT_FILAMENT_COMPONENTS_RENDERABLEMANAGER_H
#define TNT_FILAMENT_COMPONENTS_RENDERABLEMANAGER_H

#include "downcast.h"

#include "HwRenderPrimitiveFactory.h"

#include "ds/DescriptorSet.h"

#include <details/InstanceBuffer.h>

#include <filament/Box.h>
#include <filament/MaterialEnums.h>
#include <filament/RenderableManager.h>

#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/EntityInstance.h>
#include <utils/Panic.h>
#include <utils/SingleInstanceComponentManager.h>
#include <utils/Slice.h>

#include <math/mat4.h>

#include <algorithm>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FBufferObject;
class FIndexBuffer;
class FMaterialInstance;
class FMorphTargetBuffer;
class FRenderPrimitive;
class FSkinningBuffer;
class FVertexBuffer;
class FTexture;

class MorphTargetBuffer;

/**
 * 可渲染对象管理器实现类
 * 
 * 管理实体上的可渲染对象组件。
 * 可渲染对象定义了如何渲染一个实体，包括几何、材质、动画等。
 * 
 * 功能：
 * - 创建和销毁可渲染对象组件
 * - 管理几何数据（顶点、索引）
 * - 管理材质实例
 * - 支持骨骼动画和变形动画
 * - 支持实例化渲染
 * - 管理可见性、层掩码、优先级等属性
 */
class FRenderableManager : public RenderableManager {
public:
    using Instance = Instance;  // 实例类型别名
    using GeometryType = Builder::GeometryType;  // 几何类型别名

    /**
     * Visibility 结构体
     * 
     * 存储 Renderable 的可见性相关属性。
     * 使用位域压缩到 16 位，以提高内存效率。
     * 
     * TODO: 考虑重命名，这实际上与材质变体相关，而不仅仅是可见性。
     */
    struct Visibility {
        uint8_t priority                : 3;  // 绘制优先级（0-7，7 是最低优先级）
        uint8_t channel                 : 3;  // 渲染通道（0-7）
        bool castShadows                : 1;  // 是否投射阴影
        bool receiveShadows             : 1;  // 是否接收阴影

        bool culling                    : 1;  // 是否启用视锥剔除
        bool skinning                   : 1;  // 是否使用骨骼动画
        bool morphing                   : 1;  // 是否使用变形动画
        bool screenSpaceContactShadows  : 1;  // 是否使用屏幕空间接触阴影
        bool reversedWindingOrder       : 1;  // 是否反转绕序（镜像变换）
        bool fog                        : 1;  // 是否受雾影响
        GeometryType geometryType       : 2;  // 几何类型（DYNAMIC、STATIC_BOUNDS、STATIC）
    };

    static_assert(sizeof(Visibility) == sizeof(uint16_t), "Visibility should be 16 bits");  // 断言大小为 16 位

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FRenderableManager(FEngine& engine) noexcept;
    
    /**
     * 析构函数
     */
    ~FRenderableManager();

    /**
     * 终止可渲染对象管理器
     * 
     * 释放所有资源。
     */
    // free-up all resources
    void terminate() noexcept;

    /**
     * 垃圾回收
     * 
     * 清理已删除实体的可渲染对象组件。
     * 
     * @param em 实体管理器引用
     */
    void gc(utils::EntityManager& em) noexcept;

    /*
     * Component Manager APIs
     */

    /**
     * 检查实体是否有可渲染对象组件
     * 
     * @param e 实体
     * @return 如果有组件返回 true，否则返回 false
     */
    bool hasComponent(utils::Entity const e) const noexcept {
        return mManager.hasComponent(e);  // 检查组件是否存在
    }

    /**
     * 获取实体的组件实例
     * 
     * @param e 实体
     * @return 组件实例
     */
    Instance getInstance(utils::Entity const e) const noexcept {
        return { mManager.getInstance(e) };  // 获取实例
    }

    /**
     * 获取组件数量
     * 
     * @return 组件数量
     */
    size_t getComponentCount() const noexcept {
        return mManager.getComponentCount();  // 返回组件数量
    }

    /**
     * 检查管理器是否为空
     * 
     * @return 如果为空返回 true，否则返回 false
     */
    bool empty() const noexcept {
        return mManager.empty();  // 检查是否为空
    }

    /**
     * 获取实例对应的实体
     * 
     * @param i 组件实例
     * @return 实体
     */
    utils::Entity getEntity(Instance const i) const noexcept {
        return mManager.getEntity(i);  // 获取实体
    }

    /**
     * 获取所有实体的数组
     * 
     * @return 实体数组指针
     */
    utils::Entity const* getEntities() const noexcept {
        return mManager.getEntities();  // 返回实体数组
    }

    /**
     * 创建可渲染对象组件
     * 
     * @param builder 构建器引用
     * @param entity 实体
     */
    void create(const Builder& builder, utils::Entity entity);

    /**
     * 销毁可渲染对象组件
     * 
     * @param e 实体
     */
    void destroy(utils::Entity e) noexcept;

    /**
     * 设置轴对齐包围盒
     * 
     * 仅对动态几何类型有效。
     * 
     * @param instance 组件实例
     * @param aabb 轴对齐包围盒
     */
    inline void setAxisAlignedBoundingBox(Instance instance, const Box& aabb);

    /**
     * 设置层掩码（位操作版本）
     * 
     * 使用位操作更新层掩码的特定位。
     * 
     * @param instance 组件实例
     * @param select 选择掩码（哪些位有效）
     * @param values 值掩码（位的值）
     */
    inline void setLayerMask(Instance instance, uint8_t select, uint8_t values) noexcept;

    /**
     * 设置绘制优先级
     * 
     * 优先级被限制在范围 [0..7] 内。
     * 
     * @param instance 组件实例
     * @param priority 优先级（0-7，7 是最低优先级）
     */
    // The priority is clamped to the range [0..7]
    inline void setPriority(Instance instance, uint8_t priority) noexcept;

    /**
     * 设置渲染通道
     * 
     * 通道被限制在范围 [0..7] 内。
     * 
     * @param instance 组件实例
     * @param channel 渲染通道（0-7）
     */
    // The channel is clamped to the range [0..7]
    inline void setChannel(Instance instance, uint8_t channel) noexcept;

    /**
     * 设置是否投射阴影
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setCastShadows(Instance instance, bool enable) noexcept;

    /**
     * 设置层掩码（直接设置版本）
     * 
     * 直接设置层掩码值。
     * 
     * @param instance 组件实例
     * @param layerMask 层掩码
     */
    inline void setLayerMask(Instance instance, uint8_t layerMask) noexcept;
    
    /**
     * 设置是否接收阴影
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setReceiveShadows(Instance instance, bool enable) noexcept;
    
    /**
     * 设置是否使用屏幕空间接触阴影
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setScreenSpaceContactShadows(Instance instance, bool enable) noexcept;
    
    /**
     * 设置是否启用视锥剔除
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setCulling(Instance instance, bool enable) noexcept;
    
    /**
     * 设置是否受雾影响
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setFogEnabled(Instance instance, bool enable) noexcept;
    
    /**
     * 获取是否受雾影响
     * 
     * @param instance 组件实例
     * @return 如果受雾影响返回 true，否则返回 false
     */
    inline bool getFogEnabled(Instance instance) const noexcept;

    /**
     * 设置图元列表
     * 
     * @param instance 组件实例
     * @param primitives 图元切片
     */
    inline void setPrimitives(Instance instance,
            utils::Slice<FRenderPrimitive> primitives) noexcept;

    /**
     * 设置是否使用骨骼动画
     * 
     * 不能与静态几何类型一起使用。
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setSkinning(Instance instance, bool enable);
    
    /**
     * 设置骨骼变换（Bone 数组版本）
     * 
     * @param instance 组件实例
     * @param transforms 骨骼变换数组
     * @param boneCount 骨骼数量
     * @param offset 偏移量（默认 0）
     */
    void setBones(Instance instance, Bone const* transforms, size_t boneCount, size_t offset = 0);
    
    /**
     * 设置骨骼变换（矩阵数组版本）
     * 
     * @param instance 组件实例
     * @param transforms 骨骼变换矩阵数组
     * @param boneCount 骨骼数量
     * @param offset 偏移量（默认 0）
     */
    void setBones(Instance instance, math::mat4f const* transforms, size_t boneCount, size_t offset = 0);
    
    /**
     * 设置蒙皮缓冲区
     * 
     * @param instance 组件实例
     * @param skinningBuffer 蒙皮缓冲区指针
     * @param count 骨骼数量
     * @param offset 偏移量
     */
    void setSkinningBuffer(Instance instance, FSkinningBuffer* skinningBuffer,
            size_t count, size_t offset);

    /**
     * 设置是否使用变形动画
     * 
     * 不能与静态几何类型一起使用。
     * 
     * @param instance 组件实例
     * @param enable 是否启用
     */
    inline void setMorphing(Instance instance, bool enable);
    
    /**
     * 设置变形权重
     * 
     * @param instance 组件实例
     * @param weights 权重数组
     * @param count 权重数量
     * @param offset 偏移量
     */
    void setMorphWeights(Instance instance, float const* weights, size_t count, size_t offset);
    
    /**
     * 设置变形目标缓冲区偏移
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @param offset 偏移量
     */
    void setMorphTargetBufferOffsetAt(Instance instance, uint8_t level, size_t primitiveIndex,
            size_t offset);
    
    /**
     * 获取变形目标缓冲区
     * 
     * @param instance 组件实例
     * @return 变形目标缓冲区指针（如果没有则返回 nullptr）
     */
    MorphTargetBuffer* getMorphTargetBuffer(Instance instance) const noexcept;
    
    /**
     * 获取变形目标数量
     * 
     * @param instance 组件实例
     * @return 变形目标数量
     */
    size_t getMorphTargetCount(Instance instance) const noexcept;

    /**
     * 设置光源通道
     * 
     * 控制可渲染对象受哪些光源影响。
     * 
     * @param instance 组件实例
     * @param channel 光源通道（0-7）
     * @param enable 是否启用
     */
    void setLightChannel(Instance instance, unsigned int channel, bool enable) noexcept;
    
    /**
     * 获取光源通道状态
     * 
     * @param instance 组件实例
     * @param channel 光源通道（0-7）
     * @return 如果通道启用返回 true，否则返回 false
     */
    bool getLightChannel(Instance instance, unsigned int channel) const noexcept;

    /**
     * 检查是否投射阴影
     * 
     * @param instance 组件实例
     * @return 如果投射阴影返回 true，否则返回 false
     */
    inline bool isShadowCaster(Instance instance) const noexcept;
    
    /**
     * 检查是否接收阴影
     * 
     * @param instance 组件实例
     * @return 如果接收阴影返回 true，否则返回 false
     */
    inline bool isShadowReceiver(Instance instance) const noexcept;
    
    /**
     * 检查是否启用视锥剔除
     * 
     * @param instance 组件实例
     * @return 如果启用剔除返回 true，否则返回 false
     */
    inline bool isCullingEnabled(Instance instance) const noexcept;

    /**
     * 获取轴对齐包围盒
     * 
     * @param instance 组件实例
     * @return 轴对齐包围盒常量引用
     */
    inline Box const& getAABB(Instance instance) const noexcept;
    
    /**
     * 获取轴对齐包围盒（别名方法）
     * 
     * @param instance 组件实例
     * @return 轴对齐包围盒常量引用
     */
    Box const& getAxisAlignedBoundingBox(Instance const instance) const noexcept { return getAABB(instance); }
    
    /**
     * 获取可见性
     * 
     * @param instance 组件实例
     * @return 可见性结构
     */
    inline Visibility getVisibility(Instance instance) const noexcept;
    
    /**
     * 获取层掩码
     * 
     * @param instance 组件实例
     * @return 层掩码
     */
    inline uint8_t getLayerMask(Instance instance) const noexcept;
    
    /**
     * 获取绘制优先级
     * 
     * @param instance 组件实例
     * @return 优先级（0-7）
     */
    inline uint8_t getPriority(Instance instance) const noexcept;
    
    /**
     * 获取渲染通道
     * 
     * @param instance 组件实例
     * @return 渲染通道（0-7）
     */
    inline uint8_t getChannels(Instance instance) const noexcept;
    
    /**
     * 获取描述符集
     * 
     * @param instance 组件实例
     * @return 描述符集引用
     */
    inline DescriptorSet& getDescriptorSet(Instance instance) noexcept;

    /**
     * 蒙皮绑定信息结构
     * 
     * 存储骨骼动画的绑定信息。
     */
    struct SkinningBindingInfo {
        backend::Handle<backend::HwBufferObject> handle;  // 骨骼缓冲区对象句柄
        uint32_t offset;  // 缓冲区偏移量
        backend::Handle<backend::HwTexture> boneIndicesAndWeightHandle;  // 骨骼索引和权重纹理句柄
    };

    /**
     * 获取蒙皮缓冲区信息
     * 
     * @param instance 组件实例
     * @return 蒙皮绑定信息
     */
    inline SkinningBindingInfo getSkinningBufferInfo(Instance instance) const noexcept;
    
    /**
     * 获取骨骼数量
     * 
     * @param instance 组件实例
     * @return 骨骼数量
     */
    inline uint32_t getBoneCount(Instance instance) const noexcept;

    /**
     * 变形绑定信息结构
     * 
     * 存储变形动画的绑定信息。
     */
    struct MorphingBindingInfo {
        backend::Handle<backend::HwBufferObject> handle;  // 变形权重缓冲区对象句柄
        uint32_t count;  // 变形目标数量
        FMorphTargetBuffer const* morphTargetBuffer;  // 变形目标缓冲区指针
    };
    
    /**
     * 获取变形缓冲区信息
     * 
     * @param instance 组件实例
     * @return 变形绑定信息
     */
    inline MorphingBindingInfo getMorphingBufferInfo(Instance instance) const noexcept;

    /**
     * 实例信息结构
     * 
     * 存储实例化渲染的信息。
     */
    struct InstancesInfo {
        FInstanceBuffer* buffer = nullptr;  // 实例缓冲区指针
        alignas(8)  // 确保指针在所有架构上都是 64 位
        // ensures the pointer is 64 bits on all archs
        uint16_t count = 0;  // 实例数量
        char padding0[6] = {};  // 填充（确保结构大小为 16 字节）
    };
    static_assert(sizeof(InstancesInfo) == 16);  // 断言结构大小为 16 字节
    
    /**
     * 获取实例信息
     * 
     * @param instance 组件实例
     * @return 实例信息
     */
    inline InstancesInfo getInstancesInfo(Instance instance) const noexcept;

    /**
     * 获取级别数量
     * 
     * 目前总是返回 1。
     * 
     * @param instance 组件实例（未使用）
     * @return 级别数量（1）
     */
    size_t getLevelCount(Instance) const noexcept { return 1u; }
    
    /**
     * 获取图元数量
     * 
     * @param instance 组件实例
     * @param level 级别（目前未使用）
     * @return 图元数量
     */
    size_t getPrimitiveCount(Instance instance, uint8_t level) const noexcept;
    
    /**
     * 获取实例数量
     * 
     * @param instance 组件实例
     * @return 实例数量
     */
    size_t getInstanceCount(Instance instance) const noexcept;
    
    /**
     * 设置材质实例
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @param materialInstance 材质实例指针
     */
    void setMaterialInstanceAt(Instance instance, uint8_t level,
            size_t primitiveIndex, FMaterialInstance const* materialInstance);
    
    /**
     * 清除材质实例
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     */
    void clearMaterialInstanceAt(Instance instance, uint8_t level, size_t primitiveIndex);
    
    /**
     * 获取材质实例
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @return 材质实例指针（如果没有则返回 nullptr）
     */
    MaterialInstance* getMaterialInstanceAt(Instance instance, uint8_t level, size_t primitiveIndex) const noexcept;
    
    /**
     * 设置几何数据
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @param type 图元类型
     * @param vertices 顶点缓冲区指针
     * @param indices 索引缓冲区指针
     * @param offset 索引偏移量
     * @param count 索引数量
     */
    void setGeometryAt(Instance instance, uint8_t level, size_t primitiveIndex,
            PrimitiveType type, FVertexBuffer* vertices, FIndexBuffer* indices,
            size_t offset, size_t count) noexcept;
    
    /**
     * 设置混合顺序
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @param blendOrder 混合顺序
     */
    void setBlendOrderAt(Instance instance, uint8_t level, size_t primitiveIndex, uint16_t blendOrder) noexcept;
    
    /**
     * 设置是否启用全局混合顺序
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @param enabled 是否启用
     */
    void setGlobalBlendOrderEnabledAt(Instance instance, uint8_t level, size_t primitiveIndex, bool enabled) noexcept;
    
    /**
     * 获取启用的属性
     * 
     * @param instance 组件实例
     * @param level 级别
     * @param primitiveIndex 图元索引
     * @return 启用的属性位集
     */
    AttributeBitset getEnabledAttributesAt(Instance instance, uint8_t level, size_t primitiveIndex) const noexcept;
    
    /**
     * 获取渲染图元列表（常量版本）
     * 
     * @param instance 组件实例
     * @param level 级别（未使用）
     * @return 渲染图元常量切片
     */
    inline utils::Slice<const FRenderPrimitive> getRenderPrimitives(Instance instance, uint8_t level) const noexcept;
    
    /**
     * 获取渲染图元列表（非常量版本）
     * 
     * @param instance 组件实例
     * @param level 级别（未使用）
     * @return 渲染图元切片
     */
    inline utils::Slice<FRenderPrimitive> getRenderPrimitives(Instance instance, uint8_t level) noexcept;

    /**
     * 条目结构
     * 
     * 存储构建器中的图元条目信息。
     */
    struct Entry {
        VertexBuffer* vertices = nullptr;  // 顶点缓冲区指针
        IndexBuffer* indices = nullptr;  // 索引缓冲区指针
        uint32_t offset = 0;  // 索引偏移量
        uint32_t count = 0;  // 索引数量
        MaterialInstance const* materialInstance = nullptr;  // 材质实例指针
        PrimitiveType type = PrimitiveType::TRIANGLES;  // 图元类型（默认三角形）
        uint16_t blendOrder = 0;  // 混合顺序
        bool globalBlendOrderEnabled = false;  // 是否启用全局混合顺序
        struct {
            uint32_t offset = 0;  // 变形目标缓冲区偏移量
        } morphing;  // 变形相关数据
    };

private:
    /**
     * 销毁组件
     * 
     * 内部方法，用于销毁可渲染对象组件。
     * 
     * @param ci 组件实例
     */
    void destroyComponent(Instance ci) noexcept;
    
    /**
     * 销毁组件图元
     * 
     * 静态方法，用于销毁组件的渲染图元。
     * 
     * @param factory 硬件渲染图元工厂引用
     * @param driver 驱动 API 引用
     * @param primitives 图元切片
     */
    static void destroyComponentPrimitives(
            HwRenderPrimitiveFactory& factory, backend::DriverApi& driver,
            utils::Slice<FRenderPrimitive> primitives) noexcept;

    /**
     * 骨骼结构
     * 
     * 存储骨骼动画的数据。
     */
    struct Bones {
        backend::Handle<backend::HwBufferObject> handle;  // 骨骼缓冲区对象句柄
        backend::Handle<backend::HwTexture> handleTexture;  // 骨骼索引和权重纹理句柄
        uint16_t count = 0;  // 骨骼数量
        uint16_t offset = 0;  // 缓冲区偏移量
        bool skinningBufferMode = false;  // 是否使用蒙皮缓冲区模式（false: 我们拥有句柄，true: 不拥有句柄）
    };
    static_assert(sizeof(Bones) == 16);  // 断言结构大小为 16 字节

    /**
     * 变形权重结构
     * 
     * 存储变形动画的权重数据。
     */
    struct MorphWeights {
        backend::Handle<backend::HwBufferObject> handle;  // 变形权重缓冲区对象句柄
        uint32_t count = 0;  // 变形目标数量
    };
    static_assert(sizeof(MorphWeights) == 8);  // 断言结构大小为 8 字节

    /**
     * 组件数据索引枚举
     * 
     * 用于访问 SoA（Structure of Arrays）布局中的不同数据字段。
     */
    enum {
        AABB,                   // 用户数据：轴对齐包围盒
        LAYERS,                 // 用户数据：层掩码
        MORPH_WEIGHTS,          // Filament 数据：UBO 存储指向变形权重信息的指针
        CHANNELS,               // 用户数据：渲染通道
        INSTANCES,              // 用户数据：实例信息
        VISIBILITY,             // 用户数据：可见性
        PRIMITIVES,             // 用户数据：图元列表
        BONES,                  // Filament 数据：UBO 存储指向骨骼信息的指针
        MORPHTARGET_BUFFER,     // 组件的变形目标缓冲区
        DESCRIPTOR_SET          // 每个可渲染对象的描述符集
    };

    /**
     * 基类类型别名
     * 
     * 单实例组件管理器，使用 SoA（Structure of Arrays）布局存储组件数据。
     */
    using Base = utils::SingleInstanceComponentManager<
            Box,                             // AABB（轴对齐包围盒）
            uint8_t,                         // LAYERS（层掩码）
            MorphWeights,                    // MORPH_WEIGHTS（变形权重）
            uint8_t,                         // CHANNELS（渲染通道）
            InstancesInfo,                   // INSTANCES（实例信息）
            Visibility,                      // VISIBILITY（可见性）
            utils::Slice<FRenderPrimitive>,  // PRIMITIVES（图元列表）
            Bones,                           // BONES（骨骼数据）
            FMorphTargetBuffer*,            // MORPHTARGET_BUFFER（变形目标缓冲区）
            DescriptorSet                    // DESCRIPTOR_SET（描述符集）
    >;

    /**
     * 模拟结构
     * 
     * 继承自基类，提供 SoA 数据访问。
     */
    struct Sim : public Base {
        using Base::gc;  // 垃圾回收
        using Base::swap;  // 交换

        /**
         * 代理结构
         * 
         * 提供对 SoA 布局中单个组件数据的访问。
         * 所有方法都会被内联。
         */
        struct Proxy {
            /**
             * 所有这些都会被内联
             */
            // all of this gets inlined
            UTILS_ALWAYS_INLINE
            Proxy(Base& sim, utils::EntityInstanceBase::Type i) noexcept
                    : aabb{ sim, i } { }  // 初始化第一个字段（用于初始化联合体）

            /**
             * 联合体
             * 
             * 这种特定的联合体用法是允许的。所有字段都是相同的类型（Field）。
             */
            union {
                // this specific usage of union is permitted. All fields are identical
                Field<AABB>                 aabb;  // 轴对齐包围盒字段
                Field<LAYERS>               layers;  // 层掩码字段
                Field<MORPH_WEIGHTS>        morphWeights;  // 变形权重组字段
                Field<CHANNELS>             channels;  // 渲染通道字段
                Field<INSTANCES>            instances;  // 实例信息字段
                Field<VISIBILITY>           visibility;  // 可见性字段
                Field<PRIMITIVES>           primitives;  // 图元列表字段
                Field<BONES>                bones;  // 骨骼数据字段
                Field<MORPHTARGET_BUFFER>   morphTargetBuffer;  // 变形目标缓冲区字段
                Field<DESCRIPTOR_SET>       descriptorSet;  // 描述符集字段
            };
        };

        /**
         * 下标运算符（非常量版本）
         * 
         * @param i 组件实例
         * @return 代理引用
         */
        UTILS_ALWAYS_INLINE Proxy operator[](Instance i) noexcept {
            return { *this, i };  // 返回代理
        }
        
        /**
         * 下标运算符（常量版本）
         * 
         * @param i 组件实例
         * @return 代理常量引用
         */
        UTILS_ALWAYS_INLINE const Proxy operator[](Instance i) const noexcept {
            return { const_cast<Sim&>(*this), i };  // 返回代理（需要 const_cast 因为 Proxy 构造函数接受非常量引用）
        }
    };

    Sim mManager;  // 管理器实例（SoA 数据存储）
    FEngine& mEngine;  // 引擎引用
    HwRenderPrimitiveFactory mHwRenderPrimitiveFactory;  // 硬件渲染图元工厂
};

FILAMENT_DOWNCAST(RenderableManager)

/**
 * 设置轴对齐包围盒
 * 
 * 仅对动态几何类型有效。
 * 
 * @param instance 组件实例
 * @param aabb 轴对齐包围盒
 */
void FRenderableManager::setAxisAlignedBoundingBox(Instance const instance, const Box& aabb) {
    if (instance) {  // 如果实例有效
        FILAMENT_CHECK_PRECONDITION(  // 检查是否为动态几何类型
                static_cast<Visibility const&>(mManager[instance].visibility).geometryType ==
                GeometryType::DYNAMIC)
                << "This renderable has staticBounds enabled; its AABB cannot change.";  // 错误消息
        mManager[instance].aabb = aabb;  // 设置包围盒
    }
}

/**
 * 设置层掩码（位操作版本）
 * 
 * 使用位操作更新层掩码的特定位。
 * 
 * @param instance 组件实例
 * @param select 选择掩码（哪些位有效）
 * @param values 值掩码（位的值）
 */
void FRenderableManager::setLayerMask(Instance const instance,
        uint8_t const select, uint8_t const values) noexcept {
    if (instance) {  // 如果实例有效
        uint8_t& layers = mManager[instance].layers;  // 获取层掩码引用
        layers = (layers & ~select) | (values & select);  // 清除选择位，然后设置新值
    }
}

/**
 * 设置层掩码（直接设置版本）
 * 
 * 直接设置层掩码值。
 * 
 * @param instance 组件实例
 * @param layerMask 层掩码
 */
void FRenderableManager::setLayerMask(Instance const instance, uint8_t const layerMask) noexcept {
    if (instance) {  // 如果实例有效
        mManager[instance].layers = layerMask;  // 设置层掩码
    }
}

/**
 * 设置绘制优先级
 * 
 * 优先级被限制在范围 [0..7] 内。
 * 
 * @param instance 组件实例
 * @param priority 优先级（0-7，7 是最低优先级）
 */
void FRenderableManager::setPriority(Instance const instance, uint8_t const priority) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.priority = std::min(priority, uint8_t(0x7));  // 限制优先级在 0-7 之间
    }
}

/**
 * 设置渲染通道
 * 
 * 通道被限制在范围 [0..CONFIG_RENDERPASS_CHANNEL_COUNT-1] 内。
 * 
 * @param instance 组件实例
 * @param channel 渲染通道（0-7）
 */
void FRenderableManager::setChannel(Instance const instance, uint8_t const channel) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.channel = std::min(channel, uint8_t(CONFIG_RENDERPASS_CHANNEL_COUNT - 1));  // 限制通道在有效范围内
    }
}

/**
 * 设置是否投射阴影
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setCastShadows(Instance const instance, bool const enable) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.castShadows = enable;  // 设置投射阴影标志
    }
}

/**
 * 设置是否接收阴影
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setReceiveShadows(Instance const instance, bool const enable) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.receiveShadows = enable;  // 设置接收阴影标志
    }
}

/**
 * 设置是否使用屏幕空间接触阴影
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setScreenSpaceContactShadows(Instance const instance, bool const enable) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.screenSpaceContactShadows = enable;  // 设置屏幕空间接触阴影标志
    }
}

/**
 * 设置是否启用视锥剔除
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setCulling(Instance const instance, bool const enable) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.culling = enable;  // 设置剔除标志
    }
}

/**
 * 设置是否受雾影响
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setFogEnabled(Instance const instance, bool const enable) noexcept {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用
        visibility.fog = enable;  // 设置雾标志
    }
}

/**
 * 获取是否受雾影响
 * 
 * @param instance 组件实例
 * @return 如果受雾影响返回 true，否则返回 false
 */
bool FRenderableManager::getFogEnabled(RenderableManager::Instance const instance) const noexcept {
    return getVisibility(instance).fog;  // 返回雾标志
}

/**
 * 设置是否使用骨骼动画
 * 
 * 不能与静态几何类型一起使用。
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setSkinning(Instance const instance, bool const enable) {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用

        FILAMENT_CHECK_PRECONDITION(visibility.geometryType != GeometryType::STATIC || !enable)  // 检查不能与静态几何一起使用
                << "Skinning can't be used with STATIC geometry";  // 错误消息

        visibility.skinning = enable;  // 设置骨骼动画标志
    }
}

/**
 * 设置是否使用变形动画
 * 
 * 不能与静态几何类型一起使用。
 * 
 * @param instance 组件实例
 * @param enable 是否启用
 */
void FRenderableManager::setMorphing(Instance const instance, bool const enable) {
    if (instance) {  // 如果实例有效
        Visibility& visibility = mManager[instance].visibility;  // 获取可见性引用

        FILAMENT_CHECK_PRECONDITION(visibility.geometryType != GeometryType::STATIC || !enable)  // 检查不能与静态几何一起使用
                << "Morphing can't be used with STATIC geometry";  // 错误消息

        visibility.morphing = enable;  // 设置变形动画标志
    }
}

/**
 * 设置图元列表
 * 
 * @param instance 组件实例
 * @param primitives 图元切片
 */
void FRenderableManager::setPrimitives(Instance const instance,
        utils::Slice<FRenderPrimitive> primitives) noexcept {
    if (instance) {  // 如果实例有效
        mManager[instance].primitives = primitives;  // 设置图元列表
    }
}

/**
 * 获取可见性
 * 
 * @param instance 组件实例
 * @return 可见性结构
 */
FRenderableManager::Visibility
FRenderableManager::getVisibility(Instance const instance) const noexcept {
    return mManager[instance].visibility;  // 返回可见性
}

/**
 * 检查是否投射阴影
 * 
 * @param instance 组件实例
 * @return 如果投射阴影返回 true，否则返回 false
 */
bool FRenderableManager::isShadowCaster(Instance const instance) const noexcept {
    return getVisibility(instance).castShadows;  // 返回投射阴影标志
}

/**
 * 检查是否接收阴影
 * 
 * @param instance 组件实例
 * @return 如果接收阴影返回 true，否则返回 false
 */
bool FRenderableManager::isShadowReceiver(Instance const instance) const noexcept {
    return getVisibility(instance).receiveShadows;  // 返回接收阴影标志
}

/**
 * 检查是否启用视锥剔除
 * 
 * @param instance 组件实例
 * @return 如果启用剔除返回 true，否则返回 false
 */
bool FRenderableManager::isCullingEnabled(Instance const instance) const noexcept {
    return getVisibility(instance).culling;  // 返回剔除标志
}

/**
 * 获取层掩码
 * 
 * @param instance 组件实例
 * @return 层掩码
 */
uint8_t FRenderableManager::getLayerMask(Instance const instance) const noexcept {
    return mManager[instance].layers;  // 返回层掩码
}

/**
 * 获取绘制优先级
 * 
 * @param instance 组件实例
 * @return 优先级（0-7）
 */
uint8_t FRenderableManager::getPriority(Instance const instance) const noexcept {
    return getVisibility(instance).priority;  // 返回优先级
}

/**
 * 获取渲染通道
 * 
 * @param instance 组件实例
 * @return 渲染通道（0-7）
 */
uint8_t FRenderableManager::getChannels(Instance const instance) const noexcept {
    return mManager[instance].channels;  // 返回渲染通道
}

/**
 * 获取轴对齐包围盒
 * 
 * @param instance 组件实例
 * @return 轴对齐包围盒常量引用
 */
Box const& FRenderableManager::getAABB(Instance const instance) const noexcept {
    return mManager[instance].aabb;  // 返回包围盒
}

/**
 * 获取蒙皮缓冲区信息
 * 
 * @param instance 组件实例
 * @return 蒙皮绑定信息
 */
FRenderableManager::SkinningBindingInfo
FRenderableManager::getSkinningBufferInfo(Instance const instance) const noexcept {
    Bones const& bones = mManager[instance].bones;  // 获取骨骼数据
    return { bones.handle, bones.offset, bones.handleTexture };  // 返回绑定信息
}

/**
 * 获取骨骼数量
 * 
 * @param instance 组件实例
 * @return 骨骼数量
 */
inline uint32_t FRenderableManager::getBoneCount(Instance const instance) const noexcept {
    Bones const& bones = mManager[instance].bones;  // 获取骨骼数据
    return bones.count;  // 返回骨骼数量
}

/**
 * 获取变形缓冲区信息
 * 
 * @param instance 组件实例
 * @return 变形绑定信息
 */
FRenderableManager::MorphingBindingInfo
FRenderableManager::getMorphingBufferInfo(Instance const instance) const noexcept {
    MorphWeights const& morphWeights = mManager[instance].morphWeights;  // 获取变形权重数据
    FMorphTargetBuffer const* const buffer = mManager[instance].morphTargetBuffer;  // 获取变形目标缓冲区
    return { morphWeights.handle, morphWeights.count, buffer  };  // 返回绑定信息
}

/**
 * 获取实例信息
 * 
 * @param instance 组件实例
 * @return 实例信息
 */
FRenderableManager::InstancesInfo
FRenderableManager::getInstancesInfo(Instance const instance) const noexcept {
    return mManager[instance].instances;  // 返回实例信息
}

/**
 * 获取渲染图元列表（常量版本）
 * 
 * @param instance 组件实例
 * @param level 级别（未使用）
 * @return 渲染图元常量切片
 */
utils::Slice<const FRenderPrimitive> FRenderableManager::getRenderPrimitives(
        Instance const instance, UTILS_UNUSED uint8_t level) const noexcept {
    return utils::Slice<const FRenderPrimitive>(mManager[instance].primitives);  // 返回常量切片
}

/**
 * 获取渲染图元列表（非常量版本）
 * 
 * @param instance 组件实例
 * @param level 级别（未使用）
 * @return 渲染图元切片
 */
utils::Slice<FRenderPrimitive> FRenderableManager::getRenderPrimitives(
        Instance const instance, UTILS_UNUSED uint8_t level) noexcept {
    return mManager[instance].primitives;  // 返回切片
}

/**
 * 获取描述符集
 * 
 * @param instance 组件实例
 * @return 描述符集引用
 */
DescriptorSet& FRenderableManager::getDescriptorSet(Instance const instance) noexcept {
    return mManager[instance].descriptorSet;  // 返回描述符集引用
}

} // namespace filament

#endif // TNT_FILAMENT_COMPONENTS_RENDERABLEMANAGER_H
