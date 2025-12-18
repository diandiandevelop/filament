/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "details/Skybox.h"

#include "details/Engine.h"
#include "details/IndirectLight.h"
#include "details/Material.h"
#include "details/Texture.h"
#include "details/VertexBuffer.h"

#include "FilamentAPI-impl.h"

#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/Skybox.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>

#include <math/vec4.h>

#include <stdint.h>


#include "generated/resources/materials.h"

using namespace filament::math;
namespace filament {

/**
 * 构建器详情结构
 * 
 * 存储天空盒的构建参数。
 */
struct Skybox::BuilderDetails {
    Texture* mEnvironmentMap = nullptr;  // 环境贴图指针（立方体贴图）
    float4 mColor{0, 0, 0, 1};  // 颜色（RGBA，默认黑色不透明）
    float mIntensity = FIndirectLight::DEFAULT_INTENSITY;  // 强度（默认太阳照度）
    bool mShowSun = false;  // 是否显示太阳（默认不显示）
    uint8_t mPriority = 7;
};

using BuilderType = Skybox;  // 构建器类型别名
BuilderType::Builder::Builder() noexcept = default;  // 默认构造函数
BuilderType::Builder::~Builder() noexcept = default;  // 析构函数
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;  // 拷贝构造函数
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;  // 移动构造函数
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;  // 拷贝赋值运算符
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;  // 移动赋值运算符

/**
 * 设置环境贴图
 * 
 * 设置用于天空盒的立方体贴图。
 * 
 * @param cubemap 立方体贴图指针
 * @return 构建器引用（支持链式调用）
 */
Skybox::Builder& Skybox::Builder::environment(Texture* cubemap) noexcept {
    mImpl->mEnvironmentMap = cubemap;  // 设置环境贴图
    return *this;  // 返回自身引用
}

/**
 * 设置强度
 * 
 * 设置天空盒的强度。
 * 
 * @param envIntensity 环境强度
 * @return 构建器引用（支持链式调用）
 */
Skybox::Builder& Skybox::Builder::intensity(float const envIntensity) noexcept {
    mImpl->mIntensity = envIntensity;  // 设置强度
    return *this;  // 返回自身引用
}

/**
 * 设置颜色
 * 
 * 设置纯色天空盒的颜色。
 * 
 * @param color 颜色（RGBA）
 * @return 构建器引用（支持链式调用）
 */
Skybox::Builder& Skybox::Builder::color(float4 const color) noexcept {
    mImpl->mColor = color;  // 设置颜色
    return *this;  // 返回自身引用
}

Skybox::Builder& Skybox::Builder::priority(uint8_t const priority) noexcept {
    mImpl->mPriority = priority;
    return *this;
}


/**
 * 设置是否显示太阳
 * 
 * 控制是否在天空盒中显示太阳。
 * 
 * @param show 是否显示太阳
 * @return 构建器引用（支持链式调用）
 */
Skybox::Builder& Skybox::Builder::showSun(bool const show) noexcept {
    mImpl->mShowSun = show;  // 设置显示太阳标志
    return *this;  // 返回自身引用
}

/**
 * 构建天空盒
 * 
 * 根据构建器配置创建天空盒。
 * 
 * @param engine 引擎引用
 * @return 天空盒指针
 */
Skybox* Skybox::Builder::build(Engine& engine) {

    FTexture const* cubemap = downcast(mImpl->mEnvironmentMap);// 转换为实现类

    FILAMENT_CHECK_PRECONDITION(!cubemap || cubemap->isCubemap())  // 检查是否为立方体贴图
            << "environment maps must be a cubemap";

    return downcast(engine).createSkybox(*this);  // 调用引擎的创建方法
}

// ------------------------------------------------------------------------------------------------

/**
 * 天空盒构造函数
 * 
 * 创建天空盒对象，包括材质实例和可渲染实体。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FSkybox::FSkybox(FEngine& engine, const Builder& builder) noexcept
        : mSkyboxTexture(downcast(builder->mEnvironmentMap)),  // 转换环境贴图
          mRenderableManager(engine.getRenderableManager()),  // 获取可渲染对象管理器
          mIntensity(builder->mIntensity) {  // 设置强度

    FMaterial const* material = engine.getSkyboxMaterial();  // 获取天空盒材质
    mSkyboxMaterialInstance = material->createInstance("Skybox");  // 创建材质实例

    /**
     * 设置材质参数
     */
    TextureSampler const sampler(TextureSampler::MagFilter::LINEAR, TextureSampler::WrapMode::REPEAT);  // 创建采样器（线性过滤，重复包装）
    auto *pInstance = static_cast<MaterialInstance*>(mSkyboxMaterialInstance);  // 转换为材质实例
    FTexture const* texture = mSkyboxTexture ? mSkyboxTexture : engine.getDummyCubemap();  // 如果有纹理则使用，否则使用虚拟立方体贴图
    pInstance->setParameter("skybox", texture, sampler);  // 设置天空盒纹理参数
    pInstance->setParameter("showSun", builder->mShowSun);  // 设置显示太阳参数
    pInstance->setParameter("constantColor", mSkyboxTexture == nullptr);  // 设置常量颜色标志（如果没有纹理则为 true）
    pInstance->setParameter("color", builder->mColor);  // 设置颜色参数

    /**
     * 创建天空盒实体
     */
    mSkybox = engine.getEntityManager().create();  // 创建实体

    /**
     * 创建可渲染对象
     * 
     * 使用全屏四边形渲染天空盒。
     */
    RenderableManager::Builder(1)  // 创建构建器（1 个图元）
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,  // 设置几何（三角形）
                    engine.getFullScreenVertexBuffer(),  // 全屏顶点缓冲区
                    engine.getFullScreenIndexBuffer())  // 全屏索引缓冲区
            .material(0, mSkyboxMaterialInstance)  // 设置材质实例
            .castShadows(false)  // 不投射阴影
            .receiveShadows(false)  // 不接收阴影
            .priority(builder->mPriority)  // 最低优先级（7）
            .culling(false)  // 禁用剔除
            .build(engine, mSkybox);  // 构建可渲染对象

}

/**
 * 创建天空盒材质
 * 
 * 根据引擎配置创建天空盒材质。
 * 支持不同的特性级别和立体渲染类型。
 * 
 * @param engine 引擎引用
 * @return 材质常量指针
 */
FMaterial const* FSkybox::createMaterial(FEngine& engine) {
    Material::Builder builder;  // 创建材质构建器
#ifdef FILAMENT_ENABLE_FEATURE_LEVEL_0  // 如果启用了特性级别 0
    if (UTILS_UNLIKELY(engine.getActiveFeatureLevel() == Engine::FeatureLevel::FEATURE_LEVEL_0)) {  // 如果当前特性级别为 0
        builder.package(MATERIALS_SKYBOX_FL0_DATA, MATERIALS_SKYBOX_FL0_SIZE);  // 使用特性级别 0 的材质包
    } else  // 否则
#endif
    {
        /**
         * 根据立体渲染类型选择材质包
         */
        switch (engine.getConfig().stereoscopicType) {  // 获取立体渲染类型
            case Engine::StereoscopicType::NONE:  // 无立体渲染
            case Engine::StereoscopicType::INSTANCED:  // 实例化立体渲染
                builder.package(MATERIALS_SKYBOX_DATA, MATERIALS_SKYBOX_SIZE);  // 使用标准材质包
                break;
            case Engine::StereoscopicType::MULTIVIEW:  // 多视图立体渲染
#ifdef FILAMENT_ENABLE_MULTIVIEW  // 如果启用了多视图
                builder.package(MATERIALS_SKYBOX_MULTIVIEW_DATA, MATERIALS_SKYBOX_MULTIVIEW_SIZE);  // 使用多视图材质包
#else  // 否则
                PANIC_POSTCONDITION("Multiview is enabled in the Engine, but this build has not "  // 触发后置条件错误
                                    "been compiled for multiview.");
#endif
                break;
        }
    }
    auto material = builder.build(engine);  // 构建材质
    return downcast(material);  // 转换为实现类并返回
}

/**
 * 终止天空盒
 * 
 * 释放天空盒资源，包括实体和材质实例。
 * 
 * @param engine 引擎引用
 */
void FSkybox::terminate(FEngine& engine) noexcept {
    /**
     * 使用 Engine::destroy 因为 FEngine::destroy 是内联的
     */
    // use Engine::destroy because FEngine::destroy is inlined
    Engine& e = engine;  // 获取引擎引用
    e.destroy(mSkybox);  // 销毁天空盒实体
    e.destroy(mSkyboxMaterialInstance);  // 销毁材质实例

    engine.getEntityManager().destroy(mSkybox);  // 销毁实体

    mSkybox = {};  // 清空实体
    mSkyboxMaterialInstance = nullptr;  // 清空材质实例指针
}

/**
 * 设置层掩码
 * 
 * 控制天空盒在哪些层可见。
 * 
 * @param select 选择掩码（哪些位有效）
 * @param values 值掩码（位的值）
 */
void FSkybox::setLayerMask(uint8_t const select, uint8_t const values) noexcept {
    auto& rcm = mRenderableManager;  // 获取可渲染对象管理器引用
    rcm.setLayerMask(rcm.getInstance(mSkybox), select, values);  // 设置可渲染对象的层掩码
    /**
     * 我们保留一个已检查的版本
     */
    // we keep a checked version
    mLayerMask = (mLayerMask & ~select) | (values & select);  // 更新层掩码（清除选择位，然后设置新值）
}

/**
 * 设置颜色
 * 
 * 设置纯色天空盒的颜色。
 * 
 * @param color 颜色（RGBA）
 */
void FSkybox::setColor(float4 const color) noexcept {
    mSkyboxMaterialInstance->setParameter("color", color);  // 设置材质参数
}

} // namespace filament
