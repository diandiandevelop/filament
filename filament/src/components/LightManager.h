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

#ifndef TNT_FILAMENT_COMPONENTS_LIGHTMANAGER_H
#define TNT_FILAMENT_COMPONENTS_LIGHTMANAGER_H

#include "downcast.h"

#include "backend/DriverApiForward.h"

#include <filament/LightManager.h>

#include <utils/Entity.h>
#include <utils/SingleInstanceComponentManager.h>

#include <math/mat4.h>

namespace filament {

class FEngine;
class FScene;

/**
 * 光源管理器实现类
 * 
 * 管理场景中的光源组件，包括点光源、聚光灯、方向光等。
 * 使用结构体数组（SoA）存储光源数据以提高缓存效率。
 * 
 * 实现细节：
 * - 使用 SingleInstanceComponentManager 管理组件
 * - 支持多种光源类型（点光源、聚光灯、方向光、太阳光等）
 * - 支持阴影投射和光照投射
 * - 支持光源通道（用于选择性光照）
 * - 支持聚光灯锥角和衰减
 * - 支持太阳光参数（角半径、光晕大小、光晕衰减）
 */
class FLightManager : public LightManager {
public:
    using Instance = Instance;  // 实例类型别名

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit FLightManager(FEngine& engine) noexcept;
    
    /**
     * 析构函数
     */
    ~FLightManager();

    /**
     * 初始化
     * 
     * 初始化光源管理器。
     * 
     * @param engine 引擎引用
     */
    void init(FEngine& engine) noexcept;

    /**
     * 终止
     * 
     * 清理资源。
     */
    void terminate() noexcept;

    /**
     * 垃圾回收
     * 
     * 清理已销毁实体的组件数据。
     * 
     * @param em 实体管理器引用
     */
    void gc(utils::EntityManager& em) noexcept;

    /*
     * Component Manager APIs
     */

    /**
     * 检查实体是否有光源组件
     * 
     * @param e 实体
     * @return 如果有组件返回 true，否则返回 false
     */
    bool hasComponent(utils::Entity const e) const noexcept {
        return mManager.hasComponent(e);  // 检查是否有组件
    }

    /**
     * 获取实体的光源实例
     * 
     * @param e 实体
     * @return 光源实例
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
     * 检查是否为空
     * 
     * @return 如果为空返回 true，否则返回 false
     */
    bool empty() const noexcept {
        return mManager.empty();  // 检查是否为空
    }

    /**
     * 获取实例对应的实体
     * 
     * @param i 实例
     * @return 实体
     */
    utils::Entity getEntity(Instance const i) const noexcept {
        return mManager.getEntity(i);  // 获取实体
    }

    /**
     * 获取所有实体数组
     * 
     * @return 实体数组常量指针
     */
    utils::Entity const* getEntities() const noexcept {
        return mManager.getEntities();  // 返回实体数组
    }

    /**
     * 创建光源组件
     * 
     * 根据构建器配置创建光源组件。
     * 
     * @param builder 构建器引用
     * @param entity 实体
     */
    void create(const Builder& builder, utils::Entity entity);

    /**
     * 销毁光源组件
     * 
     * @param e 实体
     */
    void destroy(utils::Entity e) noexcept;

    /**
     * 准备渲染
     * 
     * 在渲染前准备光源数据。
     * 
     * @param driver 驱动 API 引用
     */
    void prepare(backend::DriverApi& driver) const noexcept;

    /**
     * 光源类型结构
     * 
     * 使用位域存储光源类型和标志。
     */
    struct LightType {
        Type type : 3;  // 光源类型（3 位，支持最多 8 种类型）
        bool shadowCaster : 1;  // 是否投射阴影（1 位）
        bool lightCaster : 1;  // 是否投射光照（1 位）
    };

    /**
     * 聚光灯参数结构
     * 
     * 存储聚光灯的几何和光照参数。
     */
    struct SpotParams {
        float radius = 0;  // 半径（衰减距离）
        float outerClamped = 0;  // 外锥角（已限制）
        float cosOuterSquared = 1;  // 外锥角余弦的平方（用于快速点积测试）
        float sinInverse = std::numeric_limits<float>::infinity();  // 正弦的倒数（用于计算衰减）
        float luminousPower = 0;  // 光通量（流明）
        math::float2 scaleOffset = {};  // 缩放和偏移（用于光照计算）
    };

    /**
     * 强度单位枚举
     */
    enum class IntensityUnit {
        LUMEN_LUX,  // 强度以流明（点光源）或勒克斯（方向光）指定
        CANDELA     // 强度以坎德拉指定（仅适用于点光源）
    };

    /**
     * 阴影参数结构
     * 
     * TODO: 移除此结构
     */
    struct ShadowParams { // TODO: get rid of this struct
        ShadowOptions options;  // 阴影选项
    };

    /**
     * 设置局部位置
     * 
     * 设置光源在局部空间中的位置。
     * 
     * @param i 实例
     * @param position 位置
     */
    UTILS_NOINLINE void setLocalPosition(Instance i, const math::float3& position) noexcept;
    
    /**
     * 设置局部方向
     * 
     * 设置光源在局部空间中的方向。
     * 
     * @param i 实例
     * @param direction 方向
     */
    UTILS_NOINLINE void setLocalDirection(Instance i, math::float3 direction) noexcept;
    
    /**
     * 设置光源通道
     * 
     * 启用或禁用光源的指定通道。
     * 
     * @param i 实例
     * @param channel 通道索引（0-7）
     * @param enable 是否启用
     */
    UTILS_NOINLINE void setLightChannel(Instance i, unsigned int channel, bool enable) noexcept;
    
    /**
     * 设置颜色
     * 
     * 设置光源的颜色（线性空间）。
     * 
     * @param i 实例
     * @param color 颜色
     */
    UTILS_NOINLINE void setColor(Instance i, const LinearColor& color) noexcept;
    
    /**
     * 设置聚光灯锥角
     * 
     * 设置聚光灯的内锥角和外锥角。
     * 
     * @param i 实例
     * @param inner 内锥角（弧度）
     * @param outer 外锥角（弧度）
     */
    UTILS_NOINLINE void setSpotLightCone(Instance i, float inner, float outer) noexcept;
    
    /**
     * 设置强度
     * 
     * 设置光源的强度。
     * 
     * @param i 实例
     * @param intensity 强度值
     * @param unit 强度单位
     */
    UTILS_NOINLINE void setIntensity(Instance i, float intensity, IntensityUnit unit) noexcept;
    
    /**
     * 设置衰减
     * 
     * 设置光源的衰减半径。
     * 
     * @param i 实例
     * @param radius 衰减半径
     */
    UTILS_NOINLINE void setFalloff(Instance i, float radius) noexcept;
    
    /**
     * 设置阴影投射
     * 
     * 设置光源是否投射阴影。
     * 
     * @param i 实例
     * @param shadowCaster 是否投射阴影
     */
    UTILS_NOINLINE void setShadowCaster(Instance i, bool shadowCaster) noexcept;
    
    /**
     * 设置太阳角半径
     * 
     * 设置太阳光的角半径（用于软阴影）。
     * 
     * @param i 实例
     * @param angularRadius 角半径（弧度）
     */
    UTILS_NOINLINE void setSunAngularRadius(Instance i, float angularRadius) noexcept;
    
    /**
     * 设置太阳光晕大小
     * 
     * 设置太阳光的光晕大小。
     * 
     * @param i 实例
     * @param haloSize 光晕大小
     */
    UTILS_NOINLINE void setSunHaloSize(Instance i, float haloSize) noexcept;
    
    /**
     * 设置太阳光晕衰减
     * 
     * 设置太阳光的光晕衰减。
     * 
     * @param i 实例
     * @param haloFalloff 光晕衰减
     */
    UTILS_NOINLINE void setSunHaloFalloff(Instance i, float haloFalloff) noexcept;

    /**
     * 获取光源通道状态
     * 
     * 检查光源的指定通道是否启用。
     * 
     * @param i 实例
     * @param channel 通道索引（0-7）
     * @return 如果通道启用返回 true，否则返回 false
     */
    UTILS_NOINLINE bool getLightChannel(Instance i, unsigned int channel) const noexcept;

    /**
     * 获取光源类型
     * 
     * @param i 实例
     * @return 光源类型常量引用
     */
    LightType const& getLightType(Instance const i) const noexcept {
        return mManager[i].lightType;  // 返回光源类型
    }

    /**
     * 获取类型
     * 
     * @param i 实例
     * @return 光源类型
     */
    Type getType(Instance const i) const noexcept {
        return getLightType(i).type;  // 返回类型
    }

    /**
     * 检查是否投射阴影
     * 
     * @param i 实例
     * @return 如果投射阴影返回 true，否则返回 false
     */
    bool isShadowCaster(Instance const i) const noexcept {
        return getLightType(i).shadowCaster;  // 返回阴影投射标志
    }

    /**
     * 检查是否投射光照
     * 
     * @param i 实例
     * @return 如果投射光照返回 true，否则返回 false
     */
    bool isLightCaster(Instance const i) const noexcept {
        return getLightType(i).lightCaster;  // 返回光照投射标志
    }

    /**
     * 检查是否是点光源
     * 
     * @param i 实例
     * @return 如果是点光源返回 true，否则返回 false
     */
    bool isPointLight(Instance const i) const noexcept {
        return getType(i) == Type::POINT;  // 检查类型
    }

    /**
     * 检查是否是聚光灯
     * 
     * @param i 实例
     * @return 如果是聚光灯返回 true，否则返回 false
     */
    bool isSpotLight(Instance const i) const noexcept {
        Type const type = getType(i);  // 获取类型
        return type == Type::FOCUSED_SPOT || type == Type::SPOT;  // 检查是否是聚光灯类型
    }

    /**
     * 检查是否是方向光
     * 
     * @param i 实例
     * @return 如果是方向光返回 true，否则返回 false
     */
    bool isDirectionalLight(Instance const i) const noexcept {
        Type const type = getType(i);  // 获取类型
        return type == Type::DIRECTIONAL || type == Type::SUN;  // 检查是否是方向光类型
    }

    /**
     * 检查是否是 IES 光源
     * 
     * @param i 实例
     * @return 如果是 IES 光源返回 true，否则返回 false
     */
    bool isIESLight(Instance i) const noexcept {
        return false;   // TODO: 当我们支持 IES 光源时更改此方法
    }

    /**
     * 检查是否是太阳光
     * 
     * @param i 实例
     * @return 如果是太阳光返回 true，否则返回 false
     */
    bool isSunLight(Instance const i) const noexcept {
        return getType(i) == Type::SUN;  // 检查类型
    }

    /**
     * 获取阴影贴图大小
     * 
     * @param i 实例
     * @return 阴影贴图大小
     */
    uint32_t getShadowMapSize(Instance const i) const noexcept {
        return getShadowParams(i).options.mapSize;  // 返回阴影贴图大小
    }

    /**
     * 获取阴影参数
     * 
     * @param i 实例
     * @return 阴影参数常量引用
     */
    ShadowParams const& getShadowParams(Instance const i) const noexcept {
        return mManager[i].shadowParams;  // 返回阴影参数
    }

    /**
     * 获取阴影常量偏移
     * 
     * @param i 实例
     * @return 阴影常量偏移
     */
    float getShadowConstantBias(Instance const i) const noexcept {
        return getShadowParams(i).options.constantBias;  // 返回常量偏移
    }

    /**
     * 获取阴影法线偏移
     * 
     * @param i 实例
     * @return 阴影法线偏移
     */
    float getShadowNormalBias(Instance const i) const noexcept {
        return getShadowParams(i).options.normalBias;  // 返回法线偏移
    }

    /**
     * 获取阴影远平面
     * 
     * @param i 实例
     * @return 阴影远平面距离
     */
    float getShadowFar(Instance const i) const noexcept {
        return getShadowParams(i).options.shadowFar;  // 返回阴影远平面
    }

    /**
     * 获取颜色
     * 
     * @param i 实例
     * @return 颜色常量引用
     */
    const math::float3& getColor(Instance const i) const noexcept {
        return mManager[i].color;  // 返回颜色
    }

    /**
     * 获取强度
     * 
     * @param i 实例
     * @return 强度值
     */
    float getIntensity(Instance const i) const noexcept {
        return mManager[i].intensity;  // 返回强度
    }

    /**
     * 获取太阳角半径
     * 
     * @param i 实例
     * @return 太阳角半径
     */
    float getSunAngularRadius(Instance const i) const noexcept {
        return mManager[i].sunAngularRadius;  // 返回太阳角半径
    }

    /**
     * 获取太阳光晕大小
     * 
     * @param i 实例
     * @return 太阳光晕大小
     */
    float getSunHaloSize(Instance const i) const noexcept {
        return mManager[i].sunHaloSize;  // 返回太阳光晕大小
    }

    /**
     * 获取太阳光晕衰减
     * 
     * @param i 实例
     * @return 太阳光晕衰减
     */
    float getSunHaloFalloff(Instance const i) const noexcept {
        return mManager[i].sunHaloFalloff;  // 返回太阳光晕衰减
    }

    /**
     * 获取平方衰减倒数
     * 
     * 用于快速计算衰减。
     * 
     * @param i 实例
     * @return 平方衰减倒数
     */
    float getSquaredFalloffInv(Instance const i) const noexcept {
        return mManager[i].squaredFallOffInv;  // 返回平方衰减倒数
    }

    /**
     * 获取衰减
     * 
     * @param i 实例
     * @return 衰减半径
     */
    float getFalloff(Instance const i) const noexcept {
        return getRadius(i);  // 返回半径
    }

    /**
     * 获取聚光灯参数
     * 
     * @param i 实例
     * @return 聚光灯参数常量引用
     */
    SpotParams const& getSpotParams(Instance const i) const noexcept {
        return mManager[i].spotParams;  // 返回聚光灯参数
    }

    /**
     * 获取聚光灯内锥角
     * 
     * @param i 实例
     * @return 内锥角（弧度）
     */
    float getSpotLightInnerCone(Instance i) const noexcept;

    /**
     * 获取外锥角余弦的平方
     * 
     * @param i 实例
     * @return 外锥角余弦的平方
     */
    float getCosOuterSquared(Instance const i) const noexcept {
        return getSpotParams(i).cosOuterSquared;  // 返回外锥角余弦的平方
    }

    /**
     * 获取正弦的倒数
     * 
     * @param i 实例
     * @return 正弦的倒数
     */
    float getSinInverse(Instance const i) const noexcept {
        return getSpotParams(i).sinInverse;  // 返回正弦的倒数
    }

    /**
     * 获取半径
     * 
     * @param i 实例
     * @return 半径
     */
    float getRadius(Instance const i) const noexcept {
        return getSpotParams(i).radius;  // 返回半径
    }

    /**
     * 获取光源通道掩码
     * 
     * @param i 实例
     * @return 光源通道掩码（8 位，每位代表一个通道）
     */
    uint8_t getLightChannels(Instance const i) const noexcept {
        return mManager[i].channels;  // 返回通道掩码
    }

    /**
     * 获取局部位置
     * 
     * @param i 实例
     * @return 局部位置常量引用
     */
    const math::float3& getLocalPosition(Instance const i) const noexcept {
        return mManager[i].position;  // 返回位置
    }

    /**
     * 获取局部方向
     * 
     * @param i 实例
     * @return 局部方向常量引用
     */
    const math::float3& getLocalDirection(Instance const i) const noexcept {
        return mManager[i].direction;  // 返回方向
    }

    /**
     * 获取阴影选项
     * 
     * @param i 实例
     * @return 阴影选项常量引用
     */
    const ShadowOptions& getShadowOptions(Instance const i) const noexcept {
        return getShadowParams(i).options;  // 返回阴影选项
    }

    /**
     * 设置阴影选项
     * 
     * @param i 实例
     * @param options 阴影选项
     */
    void setShadowOptions(Instance i, ShadowOptions const& options) noexcept;

private:
    friend class FScene;  // 允许 FScene 访问私有成员

    /**
     * 字段索引枚举
     * 
     * 用于访问结构体数组中的不同字段。
     */
    enum {
        LIGHT_TYPE,         // 光源类型
        POSITION,           // 局部空间中的位置（即变换前）
        DIRECTION,          // 局部空间中的方向（即变换前）
        COLOR,              // 颜色
        SHADOW_PARAMS,      // 阴影所需的状态
        SPOT_PARAMS,        // 聚光灯所需的状态
        SUN_ANGULAR_RADIUS, // 方向光太阳的状态
        SUN_HALO_SIZE,      // 方向光太阳的状态
        SUN_HALO_FALLOFF,   // 方向光太阳的状态
        INTENSITY,          // 强度
        FALLOFF,            // 衰减
        CHANNELS,           // 通道
    };

    /**
     * 基础组件管理器类型
     * 
     * 使用结构体数组（SoA）存储组件数据，总大小约 120 字节。
     */
    using Base = utils::SingleInstanceComponentManager<  // 120 bytes
            LightType,      //  1 字节 - 光源类型
            math::float3,   // 12 字节 - 位置
            math::float3,   // 12 字节 - 方向
            math::float3,   // 12 字节 - 颜色
            ShadowParams,   // 12 字节 - 阴影参数
            SpotParams,     // 24 字节 - 聚光灯参数
            float,          //  4 字节 - 太阳角半径
            float,          //  4 字节 - 太阳光晕大小
            float,          //  4 字节 - 太阳光晕衰减
            float,          //  4 字节 - 强度
            float,          //  4 字节 - 平方衰减倒数
            uint8_t         //  1 字节 - 通道掩码
    >;

    /**
     * 组件管理器结构
     * 
     * 继承自 Base，提供代理访问接口。
     */
    struct Sim : public Base {
        using Base::gc;  // 垃圾回收方法
        using Base::swap;  // 交换方法

        /**
         * 代理结构
         * 
         * 提供对组件字段的统一访问接口。
         * 所有方法都会被内联。
         */
        struct Proxy {
            /**
             * 构造函数
             * 
             * 所有内容都会被内联。
             */
            // all of this gets inlined
            UTILS_ALWAYS_INLINE
            Proxy(Base& sim, utils::EntityInstanceBase::Type i) noexcept
                    : lightType{ sim, i } { }  // 初始化第一个字段

            /**
             * 联合体
             * 
             * 此联合体的特定用法是允许的。所有字段都是相同的类型（Field）。
             */
            union {
                // this specific usage of union is permitted. All fields are identical
                Field<LIGHT_TYPE>           lightType;  // 光源类型字段
                Field<POSITION>             position;  // 位置字段
                Field<DIRECTION>            direction;  // 方向字段
                Field<COLOR>                color;  // 颜色字段
                Field<SHADOW_PARAMS>        shadowParams;  // 阴影参数字段
                Field<SPOT_PARAMS>          spotParams;  // 聚光灯参数字段
                Field<SUN_ANGULAR_RADIUS>   sunAngularRadius;  // 太阳角半径字段
                Field<SUN_HALO_SIZE>        sunHaloSize;  // 太阳光晕大小字段
                Field<SUN_HALO_FALLOFF>     sunHaloFalloff;  // 太阳光晕衰减字段
                Field<INTENSITY>            intensity;  // 强度字段
                Field<FALLOFF>              squaredFallOffInv;  // 平方衰减倒数字段
                Field<CHANNELS>             channels;  // 通道字段
            };
        };

        /**
         * 下标运算符（非常量版本）
         * 
         * @param i 实例
         * @return 代理对象
         */
        UTILS_ALWAYS_INLINE Proxy operator[](Instance i) noexcept {
            return { *this, i };  // 返回代理对象
        }
        
        /**
         * 下标运算符（常量版本）
         * 
         * @param i 实例
         * @return 代理对象
         */
        UTILS_ALWAYS_INLINE const Proxy operator[](Instance i) const noexcept {
            return { const_cast<Sim&>(*this), i };  // 返回代理对象（需要 const_cast 因为 Proxy 构造函数接受非常量引用）
        }
    };

    Sim mManager;  // 组件管理器
    FEngine& mEngine;  // 引擎引用
};

FILAMENT_DOWNCAST(LightManager)


} // namespace filament

#endif // TNT_FILAMENT_COMPONENTS_LIGHTMANAGER_H
