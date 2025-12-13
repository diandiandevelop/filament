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

#include "FilamentAPI-impl.h"

#include "components/LightManager.h"

#include "details/Engine.h"
#include "utils/ostream.h"

#include <filament/LightManager.h>

#include <utils/Logger.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/fast.h>
#include <math/scalar.h>
#include <math/vec2.h>
#include <math/vec3.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace filament::math;
using namespace utils;

namespace filament {

// ------------------------------------------------------------------------------------------------

/**
 * 光源构建器详情结构
 * 
 * 存储光源构建器的所有配置参数。
 */
struct LightManager::BuilderDetails {
    Type mType = Type::DIRECTIONAL;  // 光源类型（方向光、点光源、聚光灯）
    bool mCastShadows = false;  // 是否投射阴影
    bool mCastLight = true;  // 是否投射光线
    uint8_t mChannels = 1u;  // 光源通道（用于光源分组）
    float3 mPosition = {};  // 位置（点光源和聚光灯）
    float mFalloff = 1.0f;  // 衰减半径
    LinearColor mColor = LinearColor{ 1.0f };  // 颜色（线性空间）
    float mIntensity = 100000.0f;  // 强度值
    FLightManager::IntensityUnit mIntensityUnit = FLightManager::IntensityUnit::LUMEN_LUX;  // 强度单位
    float3 mDirection = { 0.0f, -1.0f, 0.0f };  // 方向（方向光和聚光灯）
    float2 mSpotInnerOuter = { f::PI_4 * 0.75f, f::PI_4 };  // 聚光灯内外角（弧度）
    float mSunAngle = 0.00951f; // 太阳角度（弧度，0.545°）
    float mSunHaloSize = 10.0f;  // 太阳光晕大小
    float mSunHaloFalloff = 80.0f;  // 太阳光晕衰减
    ShadowOptions mShadowOptions;  // 阴影选项

    /**
     * 构造函数
     * 
     * @param type 光源类型
     */
    explicit BuilderDetails(Type const type) noexcept : mType(type) { }
    
    /**
     * 默认构造函数
     * 
     * 仅用于下面的显式实例化。
     */
    // this is only needed for the explicit instantiation below
    BuilderDetails() = default;
};

using BuilderType = LightManager;

/**
 * 光源构建器构造函数
 * 
 * @param type 光源类型
 */
BuilderType::Builder::Builder(Type type) noexcept: BuilderBase<BuilderDetails>(type) {}
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置是否投射阴影
 * 
 * @param enable 是否启用阴影
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::castShadows(bool const enable) noexcept {
    mImpl->mCastShadows = enable;  // 设置阴影标志
    return *this;
}

/**
 * 设置阴影选项
 * 
 * @param options 阴影选项
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::shadowOptions(const ShadowOptions& options) noexcept {
    mImpl->mShadowOptions = options;  // 设置阴影选项
    return *this;
}

/**
 * 设置是否投射光线
 * 
 * @param enable 是否启用光线
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::castLight(bool const enable) noexcept {
    mImpl->mCastLight = enable;  // 设置光线标志
    return *this;
}

/**
 * 设置光源位置
 * 
 * 用于点光源和聚光灯。
 * 
 * @param position 位置向量
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::position(const float3& position) noexcept {
    mImpl->mPosition = position;  // 设置位置
    return *this;
}

/**
 * 设置光源方向
 * 
 * 用于方向光和聚光灯。
 * 
 * @param direction 方向向量（会被归一化）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::direction(const float3& direction) noexcept {
    mImpl->mDirection = direction;  // 设置方向
    return *this;
}

/**
 * 设置光源颜色
 * 
 * @param color 颜色（线性空间）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::color(const LinearColor& color) noexcept {
    mImpl->mColor = color;  // 设置颜色
    return *this;
}

/**
 * 设置光源强度（流明/勒克斯）
 * 
 * 方向光使用勒克斯，点光源和聚光灯使用流明。
 * 
 * @param intensity 强度值
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::intensity(float const intensity) noexcept {
    mImpl->mIntensity = intensity;  // 设置强度
    mImpl->mIntensityUnit = FLightManager::IntensityUnit::LUMEN_LUX;  // 设置单位为流明/勒克斯
    return *this;
}

/**
 * 设置光源强度（坎德拉）
 * 
 * 仅适用于点光源和聚光灯。
 * 
 * @param intensity 强度值（坎德拉）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::intensityCandela(float const intensity) noexcept {
    mImpl->mIntensity = intensity;  // 设置强度
    mImpl->mIntensityUnit = FLightManager::IntensityUnit::CANDELA;  // 设置单位为坎德拉
    return *this;
}

/**
 * 设置光源强度（瓦特）
 * 
 * 根据功率（瓦特）和效率计算流明。
 * 公式：流明 = 效率 × 683 × 瓦特
 * 
 * @param watts 功率（瓦特）
 * @param efficiency 效率（0-1）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::intensity(float const watts, float const efficiency) noexcept {
    mImpl->mIntensity = efficiency * 683.0f * watts;  // 计算流明（683 是流明/瓦特的转换系数）
    mImpl->mIntensityUnit = FLightManager::IntensityUnit::LUMEN_LUX;  // 设置单位为流明/勒克斯
    return *this;
}

/**
 * 设置衰减半径
 * 
 * 点光源和聚光灯的衰减半径。
 * 
 * @param radius 衰减半径
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::falloff(float const radius) noexcept {
    mImpl->mFalloff = radius;  // 设置衰减半径
    return *this;
}

/**
 * 设置聚光灯锥角
 * 
 * @param inner 内角（弧度）
 * @param outer 外角（弧度）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::spotLightCone(float inner, float outer) noexcept {
    mImpl->mSpotInnerOuter = { inner, outer };  // 设置内外角
    return *this;
}

/**
 * 设置太阳角度半径
 * 
 * 用于方向光（太阳光）。
 * 
 * @param sunAngle 角度半径（弧度）
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::sunAngularRadius(float const sunAngle) noexcept {
    mImpl->mSunAngle = sunAngle;  // 设置太阳角度
    return *this;
}

/**
 * 设置太阳光晕大小
 * 
 * @param haloSize 光晕大小
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::sunHaloSize(float const haloSize) noexcept {
    mImpl->mSunHaloSize = haloSize;  // 设置光晕大小
    return *this;
}

/**
 * 设置太阳光晕衰减
 * 
 * @param haloFalloff 光晕衰减值
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::sunHaloFalloff(float const haloFalloff) noexcept {
    mImpl->mSunHaloFalloff = haloFalloff;  // 设置光晕衰减
    return *this;
}

/**
 * 设置光源通道
 * 
 * 控制光源影响哪些光源通道。
 * 
 * @param channel 通道索引（0-7）
 * @param enable 是否启用此通道
 * @return 构建器引用（支持链式调用）
 */
LightManager::Builder& LightManager::Builder::lightChannel(unsigned int const channel, bool const enable) noexcept {
    if (channel < 8) {  // 如果通道索引有效
        const uint8_t mask = 1u << channel;  // 创建位掩码
        mImpl->mChannels &= ~mask;  // 清除该位
        mImpl->mChannels |= enable ? mask : 0u;  // 根据 enable 设置该位
    }
    return *this;
}

/**
 * 构建光源组件
 * 
 * 根据构建器配置创建光源组件。
 * 
 * @param engine 引擎引用
 * @param entity 实体
 * @return 构建结果
 */
LightManager::Builder::Result LightManager::Builder::build(Engine& engine, Entity const entity) {
    downcast(engine).createLight(*this, entity);  // 创建光源组件
    return Success;  // 返回成功
}

// ------------------------------------------------------------------------------------------------

/**
 * 光源管理器构造函数
 * 
 * @param engine 引擎引用
 * 
 * 注意：不要在构造函数中使用 engine，因为它还没有完全构造完成。
 */
FLightManager::FLightManager(FEngine& engine) noexcept : mEngine(engine) {
    // DON'T use engine here in the ctor, because it's not fully constructed yet.
}

/**
 * 光源管理器析构函数
 * 
 * 所有组件应该在此处之前已被销毁
 * （terminate 应该已经从 Engine 的 shutdown() 调用）
 */
FLightManager::~FLightManager() {
    // all components should have been destroyed when we get here
    // (terminate should have been called from Engine's shutdown())
    assert_invariant(mManager.getComponentCount() == 0);  // 断言组件数量为 0
}

/**
 * 初始化光源管理器
 * 
 * @param engine 引擎引用（未使用）
 */
void FLightManager::init(FEngine&) noexcept {
}

/**
 * 创建光源组件
 * 
 * 根据构建器配置创建光源组件。
 * 
 * @param builder 构建器引用
 * @param entity 实体
 */
void FLightManager::create(const Builder& builder, Entity const entity) {
    auto& manager = mManager;

    /**
     * 如果实体已有光源组件，先销毁它
     */
    if (UTILS_UNLIKELY(manager.hasComponent(entity))) {
        destroy(entity);  // 销毁现有组件
    }
    /**
     * 添加光源组件
     */
    Instance const i = manager.addComponent(entity);  // 添加组件并获取实例
    assert_invariant(i);  // 断言实例有效

    if (i) {
        /**
         * 设置光源类型
         * 
         * 这需要在调用下面的 set() 方法之前完成。
         * 类型必须首先设置（下面的一些调用依赖于它）。
         */
        // This needs to happen before we call the set() methods below
        // Type must be set first (some calls depend on it below)
        LightType& lightType = manager[i].lightType;  // 获取光源类型引用
        lightType.type = builder->mType;  // 设置光源类型
        lightType.shadowCaster = builder->mCastShadows;  // 设置阴影投射标志
        lightType.lightCaster = builder->mCastLight;  // 设置光线投射标志

        mManager[i].channels = builder->mChannels;  // 设置光源通道

        /**
         * 通过调用 setter 设置默认值
         */
        // set default values by calling the setters
        setShadowOptions(i, builder->mShadowOptions);  // 设置阴影选项
        setLocalPosition(i, builder->mPosition);  // 设置局部位置
        setLocalDirection(i, builder->mDirection);  // 设置局部方向
        setColor(i, builder->mColor);  // 设置颜色

        /**
         * 这必须在设置强度之前设置
         */
        // this must be set before intensity
        setSpotLightCone(i, builder->mSpotInnerOuter.x, builder->mSpotInnerOuter.y);  // 设置聚光灯锥角
        setIntensity(i, builder->mIntensity, builder->mIntensityUnit);  // 设置强度

        setFalloff(i, builder->mCastLight ? builder->mFalloff : 0);  // 设置衰减（如果不投射光线则为 0）
        setSunAngularRadius(i, builder->mSunAngle);  // 设置太阳角度半径
        setSunHaloSize(i, builder->mSunHaloSize);  // 设置太阳光晕大小
        setSunHaloFalloff(i, builder->mSunHaloFalloff);  // 设置太阳光晕衰减
    }
}

/**
 * 准备光源数据
 * 
 * 在渲染前准备光源数据（当前为空实现）。
 * 
 * @param driver 驱动 API 引用（未使用）
 */
void FLightManager::prepare(backend::DriverApi&) const noexcept {
}

/**
 * 销毁光源组件
 * 
 * 销毁指定实体的光源组件。
 * 
 * @param e 实体
 */
void FLightManager::destroy(Entity const e) noexcept {
    Instance const i = getInstance(e);  // 获取组件实例
    if (i) {  // 如果实例有效
        auto& manager = mManager;
        manager.removeComponent(e);  // 移除组件
    }
}

void FLightManager::terminate() noexcept {
    auto& manager = mManager;
    if (!manager.empty()) {
#ifndef NDEBUG
        DLOG(INFO) << "cleaning up " << manager.getComponentCount() << " leaked Light components";
#endif
        while (!manager.empty()) {
            Instance const ci = manager.end() - 1;
            manager.removeComponent(manager.getEntity(ci));
        }
    }
}
void FLightManager::gc(EntityManager& em) noexcept {
    mManager.gc(em, [this](Entity const e) {
        destroy(e);
    });
}

void FLightManager::setShadowOptions(Instance const i, ShadowOptions const& options) noexcept {
    ShadowParams& params = mManager[i].shadowParams;
    params.options = options;
    params.options.mapSize = clamp(options.mapSize, 8u, 2048u);
    params.options.shadowCascades = clamp<uint8_t>(options.shadowCascades, 1, CONFIG_MAX_SHADOW_CASCADES);
    params.options.constantBias = clamp(options.constantBias, 0.0f, 2.0f);
    params.options.normalBias = clamp(options.normalBias, 0.0f, 3.0f);
    params.options.shadowFar = std::max(options.shadowFar, 0.0f);
    params.options.shadowNearHint = std::max(options.shadowNearHint, 0.0f);
    params.options.shadowFarHint = std::max(options.shadowFarHint, 0.0f);
    params.options.vsm.blurWidth = std::max(0.0f, options.vsm.blurWidth);
}

void FLightManager::setLightChannel(Instance const i, unsigned int const channel, bool const enable) noexcept {
    if (i) {
        if (channel < 8) {
            auto& manager = mManager;
            const uint8_t mask = 1u << channel;
            manager[i].channels &= ~mask;
            manager[i].channels |= enable ? mask : 0u;
        }
    }
}

bool FLightManager::getLightChannel(Instance const i, unsigned int const channel) const noexcept {
    if (i) {
        if (channel < 8) {
            auto& manager = mManager;
            const uint8_t mask = 1u << channel;
            return bool(manager[i].channels & mask);
        }
    }
    return false;
}

void FLightManager::setLocalPosition(Instance const i, const float3& position) noexcept {
    if (i) {
        auto& manager = mManager;
        manager[i].position = position;
    }
}

void FLightManager::setLocalDirection(Instance const i, float3 const direction) noexcept {
    if (i) {
        auto& manager = mManager;
        manager[i].direction = direction;
    }
}

void FLightManager::setColor(Instance const i, const LinearColor& color) noexcept {
    if (i) {
        auto& manager = mManager;
        manager[i].color = color;
    }
}

void FLightManager::setIntensity(Instance const i, float const intensity, IntensityUnit const unit) noexcept {
    auto& manager = mManager;
    if (i) {
        Type const type = getLightType(i).type;
        float luminousPower = intensity;
        float luminousIntensity = 0.0f;
        switch (type) {
            case Type::SUN:
            case Type::DIRECTIONAL:
                // luminousPower is in lux, nothing to do.
                luminousIntensity = luminousPower;
                break;

            case Type::POINT:
                if (unit == IntensityUnit::LUMEN_LUX) {
                    // li = lp / (4 * pi)
                    luminousIntensity = luminousPower * f::ONE_OVER_PI * 0.25f;
                } else {
                    assert_invariant(unit == IntensityUnit::CANDELA);
                    // intensity specified directly in candela, no conversion needed
                    luminousIntensity = luminousPower;
                }
                break;

            case Type::FOCUSED_SPOT: {
                SpotParams& spotParams = manager[i].spotParams;
                float const cosOuter = std::sqrt(spotParams.cosOuterSquared);
                if (unit == IntensityUnit::LUMEN_LUX) {
                    // li = lp / (2 * pi * (1 - cos(cone_outer / 2)))
                    luminousIntensity = luminousPower / (f::TAU * (1.0f - cosOuter));
                } else {
                    assert_invariant(unit == IntensityUnit::CANDELA);
                    // intensity specified directly in candela, no conversion needed
                    luminousIntensity = luminousPower;
                    // lp = li * (2 * pi * (1 - cos(cone_outer / 2)))
                    luminousPower = luminousIntensity * (f::TAU * (1.0f - cosOuter));
                }
                spotParams.luminousPower = luminousPower;
                break;
            }
            case Type::SPOT:
                if (unit == IntensityUnit::LUMEN_LUX) {
                    // li = lp / pi
                    luminousIntensity = luminousPower * f::ONE_OVER_PI;
                } else {
                    assert_invariant(unit == IntensityUnit::CANDELA);
                    // intensity specified directly in Candela, no conversion needed
                    luminousIntensity = luminousPower;
                }
                break;
        }
        manager[i].intensity = luminousIntensity;
    }
}

void FLightManager::setFalloff(Instance const i, float const falloff) noexcept {
    auto& manager = mManager;
    if (i && !isDirectionalLight(i)) {
        float const sqFalloff = falloff * falloff;
        SpotParams& spotParams = manager[i].spotParams;
        manager[i].squaredFallOffInv = sqFalloff > 0.0f ? (1 / sqFalloff) : 0;
        spotParams.radius = falloff;
    }
}

void FLightManager::setSpotLightCone(Instance const i, float const inner, float const outer) noexcept {
    auto& manager = mManager;
    if (i && isSpotLight(i)) {
        // clamp the inner/outer angles to [0.5 degrees, 90 degrees]
        float innerClamped = std::clamp(std::abs(inner), 0.5f * f::DEG_TO_RAD, f::PI_2);
        float const outerClamped = std::clamp(std::abs(outer), 0.5f * f::DEG_TO_RAD, f::PI_2);

        // inner must always be smaller than outer
        innerClamped = std::min(innerClamped, outerClamped);

        float const cosOuter = fast::cos(outerClamped);
        float const cosInner = fast::cos(innerClamped);
        float const cosOuterSquared = cosOuter * cosOuter;
        float const scale = 1.0f / std::max(1.0f / 1024.0f, cosInner - cosOuter);
        float const offset = -cosOuter * scale;

        SpotParams& spotParams = manager[i].spotParams;
        spotParams.outerClamped = outerClamped;
        spotParams.cosOuterSquared = cosOuterSquared;
        spotParams.sinInverse = 1.0f / std::sin(outerClamped);
        spotParams.scaleOffset = { scale, offset };

        // we need to recompute the luminous intensity
        Type const type = getLightType(i).type;
        if (type == Type::FOCUSED_SPOT) {
            // li = lp / (2 * pi * (1 - cos(cone_outer / 2)))
            float const luminousPower = spotParams.luminousPower;
            float const luminousIntensity = luminousPower / (f::TAU * (1.0f - cosOuter));
            manager[i].intensity = luminousIntensity;
        }
    }
}

void FLightManager::setSunAngularRadius(Instance const i, float angularRadius) noexcept {
    if (i && isSunLight(i)) {
        angularRadius = clamp(angularRadius, 0.25f, 20.0f);
        mManager[i].sunAngularRadius = angularRadius * f::DEG_TO_RAD;
    }
}

void FLightManager::setSunHaloSize(Instance const i, float const haloSize) noexcept {
    if (i && isSunLight(i)) {
        mManager[i].sunHaloSize = haloSize;
    }
}

void FLightManager::setSunHaloFalloff(Instance const i, float const haloFalloff) noexcept {
    if (i && isSunLight(i)) {
        mManager[i].sunHaloFalloff = haloFalloff;
    }
}

void FLightManager::setShadowCaster(Instance const i, bool const shadowCaster) noexcept {
    if (i) {
        LightType& lightType = mManager[i].lightType;
        lightType.shadowCaster = shadowCaster;
    }
}

float FLightManager::getSpotLightInnerCone(Instance const i) const noexcept {
    const auto& spotParams = getSpotParams(i);
    float const cosOuter = std::cos(spotParams.outerClamped);
    float const scale = spotParams.scaleOffset.x;
    float const inner = std::acos((1.0f / scale) + cosOuter);
    return inner;
}

// ------------------------------------------------------------------------------------------------
// ShadowCascades utility methods
// ------------------------------------------------------------------------------------------------

void LightManager::ShadowCascades::computeUniformSplits(float splitPositions[3], uint8_t cascades) {
    size_t s = 0;
    cascades = min(cascades, (uint8_t) 4u);
    for (size_t c = 1; c < cascades; c++) {
        splitPositions[s++] = float(c) / float(cascades);
    }
}

void LightManager::ShadowCascades::computeLogSplits(float splitPositions[3], uint8_t cascades,
        float const near, float const far) {
    size_t s = 0;
    cascades = min(cascades, (uint8_t) 4u);
    for (size_t c = 1; c < cascades; c++) {
        splitPositions[s++] =
            (near * std::pow(far / near, float(c) / float(cascades)) - near) / (far - near);
    }
}

void LightManager::ShadowCascades::computePracticalSplits(float splitPositions[3], uint8_t cascades,
        float const near, float const far, float const lambda) {
    float uniformSplits[3];
    float logSplits[3];
    cascades = min(cascades, (uint8_t) 4u);
    computeUniformSplits(uniformSplits, cascades);
    computeLogSplits(logSplits, cascades, near, far);
    size_t s = 0;
    for (size_t c = 1; c < cascades; c++) {
        splitPositions[s] = lambda * logSplits[s] + (1.0f - lambda) * uniformSplits[s];
        s++;
    }
}

} // namespace filament
