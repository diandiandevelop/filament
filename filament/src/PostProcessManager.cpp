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

#ifdef FILAMENT_TARGET_MOBILE
#   define DOF_DEFAULT_RING_COUNT 3
#   define DOF_DEFAULT_MAX_COC    24
#else
#   define DOF_DEFAULT_RING_COUNT 5
#   define DOF_DEFAULT_MAX_COC    32
#endif

#include "PostProcessManager.h"

#include "materials/antiAliasing/fxaa/fxaa.h"
#include "materials/antiAliasing/taa/taa.h"
#include "materials/bloom/bloom.h"
#include "materials/colorGrading/colorGrading.h"
#include "materials/dof/dof.h"
#include "materials/flare/flare.h"
#include "materials/fsr/fsr.h"
#include "materials/sgsr/sgsr.h"
#include "materials/ssao/ssao.h"

#include "details/Engine.h"

#include "ds/DescriptorSet.h"
#include "ds/SsrPassDescriptorSet.h"
#include "ds/TypedUniformBuffer.h"

#include "fg/FrameGraph.h"
#include "fg/FrameGraphId.h"
#include "fg/FrameGraphResources.h"
#include "fg/FrameGraphTexture.h"

#include "fsr.h"
#include "FrameHistory.h"
#include "RenderPass.h"

#include "details/Camera.h"
#include "details/ColorGrading.h"
#include "details/Material.h"
#include "details/MaterialInstance.h"
#include "details/Texture.h"
#include "details/VertexBuffer.h"

#include "generated/resources/materials.h"

#include <filament/Material.h>
#include <filament/MaterialEnums.h>
#include <filament/Options.h>
#include <filament/Viewport.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverEnums.h>
#include <backend/DriverApiForward.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>
#include <backend/PixelBufferDescriptor.h>

#include <private/backend/BackendUtils.h>

#include <math/half.h>
#include <math/mat2.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/scalar.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/algorithm.h>
#include <utils/BitmaskEnum.h>
#include <utils/debug.h>
#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>
#include <variant>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

using namespace utils;
using namespace math;
using namespace backend;

static constexpr uint8_t kMaxBloomLevels = 12u;
static_assert(kMaxBloomLevels >= 3, "We require at least 3 bloom levels");

namespace {

/**
 * Halton 序列生成器
 * 
 * 生成 Halton 低差异序列的值。
 * 跳过一些条目使序列的平均值更接近 0.5。
 * 
 * @param i 序列索引
 * @param b 基数
 * @return Halton 序列值（0-1 范围）
 */
constexpr float halton(unsigned int i, unsigned int const b) noexcept {
    // skipping a bunch of entries makes the average of the sequence closer to 0.5
    /**
     * 跳过 409 个条目，使平均值更接近 0.5
     */
    i += 409;
    /**
     * 分数因子（初始为 1）
     */
    float f = 1.0f;
    /**
     * 结果值
     */
    float r = 0.0f;
    /**
     * 计算 Halton 序列值
     */
    while (i > 0u) {
        /**
         * 除以基数得到下一个分数因子
         */
        f /= float(b);
        /**
         * 累加当前位的贡献
         */
        r += f * float(i % b);
        /**
         * 移动到下一位
         */
        i /= b;
    }
    return r;
}

/**
 * 设置常量参数
 * 
 * 设置材质的特化常量参数。
 * 
 * @tparam ValueType 值类型
 * @param material 材质指针
 * @param name 常量名称
 * @param value 常量值
 * @param dirty 输出标志，如果值改变则设置为 true
 */
template <typename ValueType>
void setConstantParameter(FMaterial* const material, std::string_view const name,
        ValueType value, bool& dirty) noexcept {
    /**
     * 获取特化常量 ID
     */
    auto id = material->getSpecializationConstantId(name);
    if (id.has_value()) {
        /**
         * 如果设置成功（值改变），标记为脏
         */
        if (material->setConstant(id.value(), value)) {
            dirty = true;
        }
    }
}

} // anonymous

// ------------------------------------------------------------------------------------------------

/**
 * PostProcessMaterial 构造函数（静态材质信息版本）
 * 
 * 从静态材质信息创建后处理材质。
 * 
 * @param info 静态材质信息
 */
PostProcessManager::PostProcessMaterial::PostProcessMaterial(StaticMaterialInfo const& info) noexcept
    : mConstants(info.constants.begin(), info.constants.size()) {  // 复制常量列表
    /**
     * 保存材质数据指针（别名到 mMaterial）
     */
    mData = info.data; // aliased to mMaterial
    /**
     * 保存材质数据大小
     */
    mSize = info.size;
}

/**
 * PostProcessMaterial 移动构造函数
 * 
 * 移动构造后处理材质对象，交换资源所有权。
 * 
 * @param rhs 要移动的源对象（右值引用）
 */
PostProcessManager::PostProcessMaterial::PostProcessMaterial(
        PostProcessMaterial&& rhs) noexcept
        : mData(nullptr), mSize(0), mConstants(rhs.mConstants) {
    using namespace std;
    /**
     * 交换数据指针和大小
     * 
     * mData 是 mMaterial 的别名（当 mSize == 0 时）。
     */
    swap(mData, rhs.mData);  // aliased to mMaterial
    swap(mSize, rhs.mSize);
}

/**
 * PostProcessMaterial 移动赋值操作符
 * 
 * 移动赋值后处理材质对象，交换资源所有权。
 * 
 * @param rhs 要移动的源对象（右值引用）
 * @return 当前对象引用
 */
PostProcessManager::PostProcessMaterial& PostProcessManager::PostProcessMaterial::operator=(
        PostProcessMaterial&& rhs) noexcept {
    if (this != &rhs) {
        using namespace std;
        /**
         * 交换所有成员变量
         */
        swap(mData, rhs.mData);      // aliased to mMaterial
        swap(mSize, rhs.mSize);
        swap(mConstants, rhs.mConstants);
    }
    return *this;
}

/**
 * PostProcessMaterial 析构函数
 * 
 * 验证对象状态一致性：
 * - 如果 mSize == 0，材质已加载（mMaterial 应存在）
 * - 如果 mSize != 0，材质未加载（mData 应存在）
 */
PostProcessManager::PostProcessMaterial::~PostProcessMaterial() noexcept {
    assert_invariant((!mSize && !mMaterial) || (mSize && mData));
}

/**
 * 终止后处理材质
 * 
 * 释放材质对象。如果材质已加载（mSize == 0），销毁材质对象。
 * 
 * @param engine 引擎引用
 */
void PostProcessManager::PostProcessMaterial::terminate(FEngine& engine) noexcept {
    if (!mSize) {
        /**
         * 材质已加载，需要销毁
         */
        engine.destroy(mMaterial);
        mMaterial = nullptr;
    }
    /**
     * 如果 mSize != 0，材质未加载，使用静态数据，无需销毁
     */
}

/**
 * 加载材质
 * 
 * 从材质数据加载材质对象。
 * 
 * @param engine 引擎引用
 */
UTILS_NOINLINE
void PostProcessManager::PostProcessMaterial::loadMaterial(FEngine& engine) const noexcept {
    // TODO: After all materials using this class have been converted to the post-process material
    //       domain, load both OPAQUE and TRANSPARENT variants here.
    /**
     * 创建材质构建器
     */
    auto builder = Material::Builder();
    /**
     * 设置材质包数据
     */
    builder.package(mData, mSize);
    /**
     * 设置所有特化常量
     */
    for (auto const& constant: mConstants) {
        /**
         * 使用 std::visit 处理不同类型的常量值
         */
        std::visit([&](auto&& arg) {
            builder.constant(constant.name.data(), constant.name.size(), arg);
        }, constant.value);
    }
    /**
     * 构建材质并转换为内部类型
     */
    mMaterial = downcast(builder.build(engine));
    /**
     * 标记材质已加载（mSize = 0）
     */
    mSize = 0; // material loaded
}

/**
 * 获取后处理材质
 * 
 * 获取或加载后处理材质对象，并准备指定变体的着色器程序。
 * 
 * @param engine 引擎引用
 * @param variant 后处理变体（OPAQUE 或 TRANSPARENT）
 * @return 材质指针
 * 
 * 处理流程：
 * 1. 如果材质未加载（mSize != 0），先加载材质
 * 2. 准备指定变体的着色器程序（CRITICAL 优先级）
 * 3. 返回材质指针
 */
UTILS_NOINLINE
FMaterial* PostProcessManager::PostProcessMaterial::getMaterial(FEngine& engine,
        PostProcessVariant variant) const noexcept {
    /**
     * 如果材质未加载，先加载它
     * 
     * mSize != 0 表示材质尚未从静态数据加载。
     */
    if (UTILS_UNLIKELY(mSize)) {
        loadMaterial(engine);
    }
    /**
     * 准备指定变体的着色器程序
     * 
     * 使用 CRITICAL 优先级，确保程序尽快编译完成。
     */
    mMaterial->prepareProgram(Variant{ Variant::type_t(variant) },
            CompilerPriorityQueue::CRITICAL);
    return mMaterial;
}

// ------------------------------------------------------------------------------------------------

/**
 * RGSS（Rotated Grid Super Sampling）4 点抖动序列
 * 
 * 用于抗锯齿的 4 点抖动模式。
 * 这个序列定义了 4 个采样偏移，用于在 TAA（时间抗锯齿）等后处理效果中提供亚像素抖动。
 */
const PostProcessManager::JitterSequence<4> PostProcessManager::sRGSS4 = {{{
        { 0.625f, 0.125f },  // 采样点 0
        { 0.125f, 0.375f },  // 采样点 1
        { 0.875f, 0.625f },  // 采样点 2
        { 0.375f, 0.875f }   // 采样点 3
}}};

/**
 * 均匀螺旋 4 点抖动序列
 * 
 * 用于抗锯齿的 4 点抖动模式，使用均匀螺旋分布。
 * 这个序列提供了另一种抖动模式选择。
 */
const PostProcessManager::JitterSequence<4> PostProcessManager::sUniformHelix4 = {{{
        { 0.25f, 0.25f },  // 采样点 0
        { 0.75f, 0.75f },  // 采样点 1
        { 0.25f, 0.75f },  // 采样点 2
        { 0.75f, 0.25f }   // 采样点 3
}}};

/**
 * 生成 Halton 序列（模板函数）
 * 
 * 生成指定数量的 Halton 低差异序列点。
 * 使用基数 2 和 3 生成二维 Halton 序列。
 * 
 * @tparam COUNT 序列点数
 * @return Halton 序列数组
 */
template<size_t COUNT>
static constexpr auto halton() {
    std::array<float2, COUNT> h;
    for (size_t i = 0; i < COUNT; i++) {
        /**
         * 使用基数 2 生成 X 坐标，基数 3 生成 Y 坐标
         */
        h[i] = {
                halton(i, 2),  // X 坐标（基数 2）
                halton(i, 3)   // Y 坐标（基数 3）
        };
    }
    return h;
}

/**
 * Halton 32 点采样序列
 * 
 * 使用 Halton 低差异序列生成的 32 个采样点。
 * 这提供了比 RGSS4 更多的采样点，用于更高质量的抗锯齿。
 */
const PostProcessManager::JitterSequence<32>
        PostProcessManager::sHaltonSamples = { halton<32>() };

/**
 * PostProcessManager 构造函数
 * 
 * 初始化后处理管理器。注意：此时 Engine 尚未完全初始化，
 * 因此不能在此处使用 Engine。
 * 
 * @param engine 引擎引用
 */
PostProcessManager::PostProcessManager(FEngine& engine) noexcept
        : mEngine(engine),                          // 保存引擎引用
          mFixedMaterialInstanceIndex {},            // 固定材质实例索引（初始为空）
          mWorkaroundSplitEasu(false),              // EASU 分割工作区标志（初始为 false）
          mWorkaroundAllowReadOnlyAncillaryFeedbackLoop(false) {  // 允许只读辅助反馈循环工作区标志（初始为 false）
    /**
     * 注意：不要在这里使用 Engine，因为它还没有完全初始化
     */
}

/**
 * PostProcessManager 析构函数
 */
PostProcessManager::~PostProcessManager() noexcept = default;

/**
 * 设置帧统一缓冲区
 * 
 * 更新后处理描述符堆和 SSR 通道描述符堆的帧统一缓冲区。
 * 
 * @param driver 驱动 API 引用
 * @param uniforms 每视图统一缓冲区引用
 */
void PostProcessManager::setFrameUniforms(DriverApi& driver,
        TypedUniformBuffer<PerViewUib>& uniforms) noexcept {
    mPostProcessDescriptorSet.setFrameUniforms(driver, uniforms);      // 设置后处理描述符堆的帧统一缓冲区
    mSsrPassDescriptorSet.setFrameUniforms(mEngine, uniforms);         // 设置 SSR 通道描述符堆的帧统一缓冲区
}

/**
 * 绑定后处理描述符堆
 * 
 * 将后处理描述符堆绑定到当前渲染状态。
 * 
 * @param driver 驱动 API 引用
 */
void PostProcessManager::bindPostProcessDescriptorSet(DriverApi& driver) const noexcept {
    mPostProcessDescriptorSet.bind(driver);
}

/**
 * 绑定每渲染对象描述符堆
 * 
 * 绑定虚拟的每渲染对象描述符堆。这用于后处理通道，
 * 因为后处理通常不需要实际的每渲染对象数据。
 * 
 * @param driver 驱动 API 引用
 */
void PostProcessManager::bindPerRenderableDescriptorSet(DriverApi& driver) const noexcept {
    driver.bindDescriptorSet(mDummyPerRenderableDsh, +DescriptorSetBindingPoints::PER_RENDERABLE,
            { { 0, 0 }, driver });
}

/**
 * 获取 UBO 管理器
 * 
 * @return UBO 管理器指针
 */
UboManager* PostProcessManager::getUboManager() const noexcept {
    return mEngine.getUboManager();
}

/**
 * 注册后处理材质
 * 
 * 将后处理材质注册到材质注册表中。
 * 
 * @param name 材质名称
 * @param info 静态材质信息
 * 
 * 注意：如果材质已存在，不会覆盖（使用 try_emplace）。
 */
UTILS_NOINLINE
void PostProcessManager::registerPostProcessMaterial(std::string_view const name,
        StaticMaterialInfo const& info) {
    mMaterialRegistry.try_emplace(name, info);
}

/**
 * 获取后处理材质
 * 
 * 从材质注册表中获取指定名称的后处理材质。
 * 
 * @param name 材质名称
 * @return 后处理材质常量引用
 * 
 * 注意：如果材质不存在，会触发断言。
 */
UTILS_NOINLINE
PostProcessManager::PostProcessMaterial const& PostProcessManager::getPostProcessMaterial(
        std::string_view const name) const noexcept {
    auto pos = mMaterialRegistry.find(name);
    assert_invariant(pos != mMaterialRegistry.end());
    return pos.value();
}

/**
 * 静态断言：ConstantInfo 必须是可平凡析构的
 * 
 * ConstantInfo 的析构函数在关闭时调用，为避免副作用，
 * 确保它是可平凡析构的。
 */
static_assert(std::is_trivially_destructible_v<PostProcessManager::StaticMaterialInfo::ConstantInfo>);

/**
 * 材质数据宏
 * 
 * 用于简化材质数据的声明，将前缀和名称组合成数据指针和大小。
 * 
 * 示例：MATERIAL(MATERIALS, BLITLOW) 展开为 MATERIALS_BLITLOW_DATA, size_t(MATERIALS_BLITLOW_SIZE)
 */
#define MATERIAL(p, n) p ## _ ## n ## _DATA, size_t(p ## _ ## n ## _SIZE)

/**
 * 特性级别 0 的材质列表
 * 
 * 包含特性级别 0 支持的后处理材质。
 * 这些材质功能受限，适用于低端设备。
 */
static const PostProcessManager::StaticMaterialInfo sMaterialListFeatureLevel0[] = {
        { "blitLow",                    MATERIAL(MATERIALS, BLITLOW) },  // 低质量位块传输
};

/**
 * 特性级别 1+ 的材质列表
 * 
 * 包含特性级别 1 及以上支持的后处理材质。
 * 这些材质提供完整的功能。
 * 
 * 材质说明：
 * - blitArray: 数组位块传输
 * - blitDepth: 深度位块传输
 * - clearDepth: 清除深度
 * - separableGaussianBlur: 可分离高斯模糊（支持 1-4 组件，数组和非数组采样器）
 * - vsmMipmap: VSM（方差阴影贴图）Mipmap
 * - debugShadowCascades: 调试阴影级联
 * - resolveDepth: 解析深度
 * - shadowmap: 阴影贴图
 */
static const PostProcessManager::StaticMaterialInfo sMaterialList[] = {
        { "blitArray",                  MATERIAL(MATERIALS, BLITARRAY) },
        { "blitDepth",                  MATERIAL(MATERIALS, BLITDEPTH) },
        { "clearDepth",                 MATERIAL(MATERIALS, CLEARDEPTH) },
        /**
         * 可分离高斯模糊（1 组件，非数组采样器）
         */
        { "separableGaussianBlur1",     MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", false}, {"componentCount", 1} } },
        /**
         * 可分离高斯模糊（1 组件，数组采样器）
         */
        { "separableGaussianBlur1L",    MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", true }, {"componentCount", 1} } },
        /**
         * 可分离高斯模糊（2 组件，非数组采样器）
         */
        { "separableGaussianBlur2",     MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", false}, {"componentCount", 2} } },
        /**
         * 可分离高斯模糊（2 组件，数组采样器）
         */
        { "separableGaussianBlur2L",    MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", true }, {"componentCount", 2} } },
        /**
         * 可分离高斯模糊（3 组件，非数组采样器）
         */
        { "separableGaussianBlur3",     MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", false}, {"componentCount", 3} } },
        /**
         * 可分离高斯模糊（3 组件，数组采样器）
         */
        { "separableGaussianBlur3L",    MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", true }, {"componentCount", 3} } },
        /**
         * 可分离高斯模糊（4 组件，非数组采样器）
         */
        { "separableGaussianBlur4",     MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", false}, {"componentCount", 4} } },
        /**
         * 可分离高斯模糊（4 组件，数组采样器）
         */
        { "separableGaussianBlur4L",    MATERIAL(MATERIALS, SEPARABLEGAUSSIANBLUR),
                { {"arraySampler", true }, {"componentCount", 4} } },
        { "vsmMipmap",                  MATERIAL(MATERIALS, VSMMIPMAP) },              // VSM Mipmap
        { "debugShadowCascades",        MATERIAL(MATERIALS, DEBUGSHADOWCASCADES) },    // 调试阴影级联
        { "resolveDepth",               MATERIAL(MATERIALS, RESOLVEDEPTH) },           // 解析深度
        { "shadowmap",                  MATERIAL(MATERIALS, SHADOWMAP) },              // 阴影贴图
};

/**
 * 初始化后处理管理器
 * 
 * 初始化后处理管理器，包括：
 * 1. 获取全屏四边形资源
 * 2. 创建虚拟每渲染对象描述符堆
 * 3. 初始化描述符堆
 * 4. 检测驱动工作区
 * 5. 注册所有后处理材质
 * 6. 创建星爆纹理（特性级别 1+）
 */
void PostProcessManager::init() noexcept {
    auto& engine = mEngine;
    DriverApi& driver = engine.getDriverApi();

    /**
     * 注册调试属性（已注释，可根据需要启用）
     */
    //FDebugRegistry& debugRegistry = engine.getDebugRegistry();
    //debugRegistry.registerProperty("d.ssao.sampleCount", &engine.debug.ssao.sampleCount);
    //debugRegistry.registerProperty("d.ssao.spiralTurns", &engine.debug.ssao.spiralTurns);
    //debugRegistry.registerProperty("d.ssao.kernelSize", &engine.debug.ssao.kernelSize);
    //debugRegistry.registerProperty("d.ssao.stddev", &engine.debug.ssao.stddev);

    /**
     * 获取全屏四边形资源
     * 
     * 全屏四边形用于后处理效果的全屏渲染。
     */
    mFullScreenQuadRph = engine.getFullScreenRenderPrimitive();              // 全屏四边形渲染图元句柄
    mFullScreenQuadVbih = engine.getFullScreenVertexBuffer()->getVertexBufferInfoHandle();  // 全屏四边形顶点缓冲区信息句柄
    mPerRenderableDslh = engine.getPerRenderableDescriptorSetLayout().getHandle();  // 每渲染对象描述符堆布局句柄

    /**
     * 创建虚拟每渲染对象描述符堆
     * 
     * 后处理通道不需要实际的每渲染对象数据，使用虚拟描述符堆。
     */
    mDummyPerRenderableDsh = driver.createDescriptorSet(mPerRenderableDslh);

    /**
     * 初始化虚拟描述符堆的绑定
     * 
     * 使用虚拟统一缓冲区和纹理填充所有必需的绑定点。
     */
    driver.updateDescriptorSetBuffer(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::OBJECT_UNIFORMS, engine.getDummyUniformBuffer(), 0,
            sizeof(PerRenderableUib));  // 对象统一缓冲区（虚拟）

    driver.updateDescriptorSetBuffer(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::BONES_UNIFORMS, engine.getDummyUniformBuffer(), 0,
            sizeof(PerRenderableBoneUib));  // 骨骼统一缓冲区（虚拟）

    driver.updateDescriptorSetBuffer(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::MORPHING_UNIFORMS, engine.getDummyUniformBuffer(), 0,
            sizeof(PerRenderableMorphingUib));  // 变形统一缓冲区（虚拟）

    driver.updateDescriptorSetTexture(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,
            engine.getDummyMorphTargetBuffer()->getPositionsHandle(), {});  // 变形目标位置纹理（虚拟）

    driver.updateDescriptorSetTexture(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,
            engine.getDummyMorphTargetBuffer()->getTangentsHandle(), {});  // 变形目标切线纹理（虚拟）

    driver.updateDescriptorSetTexture(mDummyPerRenderableDsh,
            +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS, engine.getZeroTexture(),
            {});  // 骨骼索引和权重纹理（虚拟，使用零纹理）

    /**
     * 初始化描述符堆
     * 
     * 初始化后处理、SSR 通道和结构描述符堆。
     */
    mSsrPassDescriptorSet.init(engine);           // 初始化 SSR 通道描述符堆
    mPostProcessDescriptorSet.init(engine);       // 初始化后处理描述符堆
    mStructureDescriptorSet.init(engine);         // 初始化结构描述符堆

    /**
     * 检测驱动工作区
     * 
     * 检查是否需要应用特定驱动的工作区。
     */
    mWorkaroundSplitEasu =
            driver.isWorkaroundNeeded(Workaround::SPLIT_EASU);  // 检测是否需要分割 EASU 工作区
    mWorkaroundAllowReadOnlyAncillaryFeedbackLoop =
            driver.isWorkaroundNeeded(Workaround::ALLOW_READ_ONLY_ANCILLARY_FEEDBACK_LOOP);  // 检测是否需要允许只读辅助反馈循环工作区

    /**
     * 注册特性级别 0 的材质
     * 
     * 特性级别 0 仅支持基本材质。
     */
    UTILS_NOUNROLL
    for (auto const& info: sMaterialListFeatureLevel0) {
        registerPostProcessMaterial(info.name, info);
    }

    /**
     * 注册特性级别 1+ 的材质
     * 
     * 特性级别 1+ 支持完整的功能集。
     */
    if (mEngine.getActiveFeatureLevel() >= FeatureLevel::FEATURE_LEVEL_1) {
        /**
         * 注册基本材质列表
         */
        UTILS_NOUNROLL
        for (auto const& info: sMaterialList) {
            registerPostProcessMaterial(info.name, info);
        }
        /**
         * 注册各个后处理效果的材质
         */
        for (auto const& info: getBloomMaterialList()) {           // 泛光材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getFlareMaterialList()) {           // 光晕材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getDofMaterialList()) {             // 景深材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getColorGradingMaterialList()) {    // 颜色分级材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getFsrMaterialList()) {             // FSR 材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getSgsrMaterialList()) {            // SGSR 材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getFxaaMaterialList()) {            // FXAA 材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getTaaMaterialList()) {             // TAA 材质
            registerPostProcessMaterial(info.name, info);
        }
        for (auto const& info: getSsaoMaterialList()) {            // SSAO 材质
            registerPostProcessMaterial(info.name, info);
        }
    }

    /**
     * 创建星爆纹理（特性级别 1+）
     * 
     * 星爆纹理用于镜头光晕效果，包含随机噪声数据。
     */
    if (engine.hasFeatureLevel(FeatureLevel::FEATURE_LEVEL_1)) {
        /**
         * 创建星爆纹理（256x1，单通道 R8 格式）
         */
        mStarburstTexture = driver.createTexture(SamplerType::SAMPLER_2D, 1,
                TextureFormat::R8, 1, 256, 1, 1, TextureUsage::DEFAULT);

        /**
         * 分配纹理数据缓冲区
         */
        PixelBufferDescriptor dataStarburst(driver.allocate(256), 256,
                PixelDataFormat::R, PixelDataType::UBYTE);
        
        /**
         * 生成随机噪声数据
         * 
         * 使用均匀分布生成 0.5-1.0 范围的随机值，然后转换为 8 位整数。
         * 这创建了一个包含随机噪声的纹理，用于镜头光晕效果。
         */
        std::generate_n((uint8_t*)dataStarburst.buffer, 256,
                [&dist = mUniformDistribution, &gen = mEngine.getRandomEngine()]() {
                    float const r = 0.5f + 0.5f * dist(gen);  // 生成 0.5-1.0 范围的随机值
                    return uint8_t(r * 255.0f);                // 转换为 8 位整数（128-255）
                });

        /**
         * 更新纹理数据
         */
        driver.update3DImage(mStarburstTexture,
                0, 0, 0, 0, 256, 1, 1,
                std::move(dataStarburst));
    }
}

/**
 * 终止后处理管理器
 * 
 * 清理后处理管理器分配的所有资源。
 * 
 * @param driver 驱动 API 引用
 * 
 * 清理顺序：
 * 1. 销毁星爆纹理
 * 2. 销毁虚拟每渲染对象描述符堆
 * 3. 终止材质实例管理器（必须在材质之前）
 * 4. 终止所有注册的材质
 * 5. 终止描述符堆
 */
void PostProcessManager::terminate(DriverApi& driver) noexcept {
    FEngine& engine = mEngine;
    
    /**
     * 销毁星爆纹理
     */
    driver.destroyTexture(mStarburstTexture);

    /**
     * 销毁虚拟每渲染对象描述符堆
     */
    driver.destroyDescriptorSet(mDummyPerRenderableDsh);

    /**
     * 终止材质实例管理器
     * 
     * 必须在材质之前销毁，因为材质实例可能引用材质。
     */
    mMaterialInstanceManager.terminate(engine);

    /**
     * 终止所有注册的材质
     * 
     * 遍历材质注册表，终止每个材质。
     */
    auto first = mMaterialRegistry.begin();
    auto const last = mMaterialRegistry.end();
    while (first != last) {
        first.value().terminate(engine);
        ++first;
    }

    /**
     * 终止描述符堆
     */
    mPostProcessDescriptorSet.terminate(engine.getDescriptorSetLayoutFactory(), driver);  // 终止后处理描述符堆
    mSsrPassDescriptorSet.terminate(driver);        // 终止 SSR 通道描述符堆
    mStructureDescriptorSet.terminate(driver);      // 终止结构描述符堆
}

/**
 * 获取全一纹理句柄
 * 
 * @return 全一纹理句柄（全白纹理，用于默认值）
 */
Handle<HwTexture> PostProcessManager::getOneTexture() const {
    return mEngine.getOneTexture();
}

/**
 * 获取全零纹理句柄
 * 
 * @return 全零纹理句柄（全黑纹理，用于默认值）
 */
Handle<HwTexture> PostProcessManager::getZeroTexture() const {
    return mEngine.getZeroTexture();
}

/**
 * 获取全一纹理数组句柄
 * 
 * @return 全一纹理数组句柄（全白纹理数组，用于默认值）
 */
Handle<HwTexture> PostProcessManager::getOneTextureArray() const {
    return mEngine.getOneTextureArray();
}

/**
 * 获取全零纹理数组句柄
 * 
 * @return 全零纹理数组句柄（全黑纹理数组，用于默认值）
 */
Handle<HwTexture> PostProcessManager::getZeroTextureArray() const {
    return mEngine.getZeroTextureArray();
}

/**
 * 重置渲染状态
 * 
 * 在每帧渲染开始时调用，重置材质实例管理器状态。
 * 这确保材质实例在每帧都能正确分配和重用。
 */
void PostProcessManager::resetForRender() {
    mMaterialInstanceManager.reset();    // 重置材质实例管理器
    mFixedMaterialInstanceIndex = {};    // 清空固定材质实例索引
}

/**
 * 解绑所有描述符堆
 * 
 * 解绑所有绑定的描述符堆，清理渲染状态。
 * 
 * @param driver 驱动 API 引用
 */
void PostProcessManager::unbindAllDescriptorSets(DriverApi& driver) noexcept {
    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_VIEW);        // 解绑每视图描述符堆
    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_RENDERABLE);  // 解绑每渲染对象描述符堆
    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);    // 解绑每材质描述符堆
}

/**
 * 获取管道状态
 * 
 * 根据材质和变体创建管道状态对象。
 * 
 * @param ma 材质指针
 * @param variant 后处理变体（OPAQUE 或 TRANSPARENT）
 * @return 管道状态对象
 * 
 * 管道状态包含：
 * - 着色器程序
 * - 顶点缓冲区信息（全屏四边形）
 * - 描述符堆布局（每视图、每渲染对象、每材质）
 * - 光栅化状态
 */
UTILS_NOINLINE
PipelineState PostProcessManager::getPipelineState(
        FMaterial const* const ma, PostProcessVariant variant) const noexcept {
    return {
            .program = ma->getProgram(Variant{ Variant::type_t(variant) }),  // 获取着色器程序
            .vertexBufferInfo = mFullScreenQuadVbih,                         // 全屏四边形顶点缓冲区信息
            .pipelineLayout = {
                    .setLayout = {
                            ma->getPerViewDescriptorSetLayout().getHandle(),  // 每视图描述符堆布局
                            mPerRenderableDslh,                               // 每渲染对象描述符堆布局
                            ma->getDescriptorSetLayout().getHandle()          // 每材质描述符堆布局
                    }},
            .rasterState = ma->getRasterState()  // 光栅化状态
    };
}

/**
 * 渲染全屏四边形
 * 
 * 使用指定的管道状态渲染全屏四边形。
 * 
 * @param out 渲染通道信息（包含目标和参数）
 * @param pipeline 管道状态
 * @param driver 驱动 API 引用
 * 
 * 注意：如果深度缓冲区是只读的，管道状态必须禁用深度写入。
 */
UTILS_NOINLINE
void PostProcessManager::renderFullScreenQuad(
        FrameGraphResources::RenderPassInfo const& out,
        PipelineState const& pipeline,
        DriverApi& driver) const noexcept {
    /**
     * 验证深度写入与只读深度缓冲区的一致性
     * 
     * 如果深度缓冲区是只读的，管道状态必须禁用深度写入。
     */
    assert_invariant(
            ((out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH)
                    && !pipeline.rasterState.depthWrite)
            || !(out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH));
    
    /**
     * 开始渲染通道
     */
    driver.beginRenderPass(out.target, out.params);
    /**
     * 绘制全屏四边形（3 个顶点，1 个实例）
     */
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
    /**
     * 结束渲染通道
     */
    driver.endRenderPass();
}

/**
 * 使用剪刀矩形渲染全屏四边形
 * 
 * 使用指定的管道状态和剪刀矩形渲染全屏四边形。
 * 
 * @param out 渲染通道信息（包含目标和参数）
 * @param pipeline 管道状态
 * @param scissor 剪刀矩形（视口）
 * @param driver 驱动 API 引用
 * 
 * 注意：如果深度缓冲区是只读的，管道状态必须禁用深度写入。
 */
UTILS_NOINLINE
void PostProcessManager::renderFullScreenQuadWithScissor(
        FrameGraphResources::RenderPassInfo const& out,
        PipelineState const& pipeline,
        backend::Viewport const scissor,
        DriverApi& driver) const noexcept {
    /**
     * 验证深度写入与只读深度缓冲区的一致性
     */
    assert_invariant(
            ((out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH)
                    && !pipeline.rasterState.depthWrite)
            || !(out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH));
    
    /**
     * 开始渲染通道
     */
    driver.beginRenderPass(out.target, out.params);
    /**
     * 设置剪刀矩形
     */
    driver.scissor(scissor);
    /**
     * 绘制全屏四边形（3 个顶点，1 个实例）
     */
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
    /**
     * 结束渲染通道
     */
    driver.endRenderPass();
}

/**
 * 提交并渲染全屏四边形
 * 
 * 提交材质实例的统一缓冲区，然后渲染全屏四边形。
 * 
 * @param driver 驱动 API 引用
 * @param out 渲染通道信息（包含目标和参数）
 * @param mi 材质实例指针
 * @param variant 后处理变体（OPAQUE 或 TRANSPARENT）
 * 
 * 处理流程：
 * 1. 提交材质实例的统一缓冲区
 * 2. 使用材质实例（绑定到管道）
 * 3. 获取管道状态
 * 4. 开始渲染通道
 * 5. 绘制全屏四边形
 * 6. 结束渲染通道
 * 7. 解绑每材质描述符堆
 */
UTILS_NOINLINE
void PostProcessManager::commitAndRenderFullScreenQuad(DriverApi& driver,
        FrameGraphResources::RenderPassInfo const& out, FMaterialInstance const* mi,
        PostProcessVariant const variant) const noexcept {
    /**
     * 提交材质实例的统一缓冲区
     */
    mi->commit(driver, getUboManager());
    /**
     * 使用材质实例（绑定到管道）
     */
    mi->use(driver);
    
    /**
     * 获取材质和管道状态
     */
    FMaterial const* const ma = mi->getMaterial();
    PipelineState const pipeline = getPipelineState(ma, variant);

    /**
     * 验证深度写入与只读深度缓冲区的一致性
     */
    assert_invariant(
            ((out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH)
             && !pipeline.rasterState.depthWrite)
            || !(out.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH));

    /**
     * 开始渲染通道
     */
    driver.beginRenderPass(out.target, out.params);
    /**
     * 绘制全屏四边形（3 个顶点，1 个实例）
     */
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
    /**
     * 结束渲染通道
     */
    driver.endRenderPass();
    /**
     * 解绑每材质描述符堆
     */
    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);
}

// ------------------------------------------------------------------------------------------------

/**
 * 结构通道
 * 
 * 生成结构信息通道，包括深度缓冲区和可选的拾取缓冲区。
 * 结构通道用于 SSAO 和接触阴影等效果。
 * 
 * @param fg 帧图引用
 * @param passBuilder 渲染通道构建器
 * @param structureRenderFlags 结构渲染标志
 * @param width 宽度（像素）
 * @param height 高度（像素）
 * @param config 结构通道配置（包含缩放因子和拾取选项）
 * @return 结构通道输出（深度纹理和拾取纹理）
 * 
 * 处理流程：
 * 1. 生成深度通道（包含 Mipmap 链，针对 SSAO 优化）
 * 2. 如果启用拾取，生成拾取缓冲区
 * 3. 生成深度 Mipmap 链
 * 
 * 注意：如果结构通道的输出未被使用，会自动被剔除。
 * 当前使用结构通道的效果：
 * - SSAO（屏幕空间环境光遮蔽）
 * - Contact Shadows（接触阴影）
 */
PostProcessManager::StructurePassOutput PostProcessManager::structure(FrameGraph& fg,
        RenderPassBuilder const& passBuilder, uint8_t const structureRenderFlags,
        uint32_t width, uint32_t height,
        StructurePassConfig const& config) noexcept {

    const float scale = config.scale;

    /**
     * 结构通道数据
     * 
     * 包含深度纹理和可选的拾取纹理。
     */
    struct StructurePassData {
        FrameGraphId<FrameGraphTexture> depth;    // 深度纹理
        FrameGraphId<FrameGraphTexture> picking;  // 拾取纹理（可选）
    };

    /**
     * 修正用户提供的缩放因子
     * 
     * 确保宽度和高度至少为 32 像素，满足最小尺寸要求。
     */
    width  = std::max(32u, uint32_t(std::ceil(float(width) * scale)));
    height = std::max(32u, uint32_t(std::ceil(float(height) * scale)));

    /**
     * 计算 Mipmap 级别数
     * 
     * 我们将最低 LOD 大小限制为 32 像素（-5 来自于此）。
     * 最多生成 8 个级别。
     */
    const size_t levelCount = std::min(8, FTexture::maxLevelCount(width, height) - 5);
    assert_invariant(levelCount >= 1);

    /**
     * 生成深度通道
     * 
     * 在请求的分辨率下生成深度通道。
     */
    auto const& structurePass = fg.addPass<StructurePassData>("Structure Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                bool const isES2 = mEngine.getDriverApi().getFeatureLevel() == FeatureLevel::FEATURE_LEVEL_0;
                data.depth = builder.createTexture("Structure Buffer", {
                        .width = width, .height = height,
                        .levels = uint8_t(levelCount),
                        .format = isES2 ? TextureFormat::DEPTH24 : TextureFormat::DEPTH32F });

                data.depth = builder.write(data.depth,
                        FrameGraphTexture::Usage::DEPTH_ATTACHMENT);

                if (config.picking) {
                    // FIXME: the DescriptorSetLayout must specify SAMPLER_FLOAT
                    data.picking = builder.createTexture("Picking Buffer", {
                            .width = width, .height = height,
                            .format = isES2 ? TextureFormat::RGBA8 : TextureFormat::RG32UI });

                    data.picking = builder.write(data.picking,
                            FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                }

                builder.declareRenderPass("Structure Target", {
                        .attachments = { .color = { data.picking }, .depth = data.depth },
                        .clearFlags = TargetBufferFlags::COLOR0 | TargetBufferFlags::DEPTH
                });
            },
            [=, this, passBuilder = passBuilder](FrameGraphResources const& resources,
                    auto const&, DriverApi& driver) mutable {
                /**
                 * 创建结构变体
                 * 
                 * 使用深度变体，并根据配置启用拾取。
                 */
                Variant structureVariant(Variant::DEPTH_VARIANT);
                structureVariant.setPicking(config.picking);

                /**
                 * 绑定描述符堆
                 * 
                 * 绑定结构通道使用的每视图和每渲染对象描述符堆。
                 */
                getStructureDescriptorSet().bind(driver);        // 绑定结构描述符堆
                bindPerRenderableDescriptorSet(driver);          // 绑定每渲染对象描述符堆

                /**
                 * 配置渲染通道构建器
                 */
                passBuilder.renderFlags(structureRenderFlags);                          // 设置渲染标志
                passBuilder.variant(structureVariant);                                  // 设置变体
                passBuilder.commandTypeFlags(RenderPass::CommandTypeFlags::SSAO);       // 设置命令类型标志（SSAO）

                /**
                 * 构建并执行渲染通道
                 */
                RenderPass const pass{ passBuilder.build(mEngine, driver) };
                auto const out = resources.getRenderPassInfo();
                driver.beginRenderPass(out.target, out.params);
                pass.getExecutor().execute(mEngine, driver);     // 执行渲染通道
                driver.endRenderPass();
                
                /**
                 * 解绑所有描述符堆
                 */
                unbindAllDescriptorSets(driver);
            });

    auto const depth = structurePass->depth;

    /**
     * 创建深度 Mipmap 链
     * 
     * 为深度纹理生成 Mipmap 链，用于 SSAO 的多级采样。
     */
    struct StructureMipmapData {
        FrameGraphId<FrameGraphTexture> depth;  // 深度纹理
    };

    /**
     * 添加深度 Mipmap 生成通道
     * 
     * 为深度纹理生成 Mipmap 链。
     */
    fg.addPass<StructureMipmapData>("StructureMipmap",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 采样深度纹理
                 */
                data.depth = builder.sample(depth);
                
                /**
                 * 为每个 Mipmap 级别创建子资源
                 * 
                 * 级别 0 已经存在（来自结构通道），所以我们从级别 1 开始。
                 */
                for (size_t i = 1; i < levelCount; i++) {
                    /**
                     * 创建 Mipmap 子资源
                     */
                    auto out = builder.createSubresource(data.depth, "Structure mip", {
                            .level = uint8_t(i)  // Mipmap 级别
                    });
                    /**
                     * 将子资源声明为深度附件
                     */
                    out = builder.write(out, FrameGraphTexture::Usage::DEPTH_ATTACHMENT);
                    /**
                     * 声明渲染通道（仅深度附件）
                     */
                    builder.declareRenderPass("Structure mip target", {
                            .attachments = { .depth = out }
                    });
                }
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                getStructureDescriptorSet().bind(driver);        // 绑定结构描述符堆
                bindPerRenderableDescriptorSet(driver);          // 绑定每渲染对象描述符堆

                /**
                 * 获取深度纹理和材质
                 */
                auto in = resources.getTexture(data.depth);
                auto& material = getPostProcessMaterial("mipmapDepth");  // 获取深度 Mipmap 材质
                FMaterial const* const ma = material.getMaterial(mEngine);
                FMaterialInstance* const mi = getMaterialInstance(ma);
                
                /**
                 * 注意：只有深度纹理在材质实例中改变（没有 UBO 更新），
                 * 因此我们不将 getMaterialInstance() 移到循环内部。
                 */

                /**
                 * 获取管道状态
                 */
                auto pipeline = getPipelineState(ma);

                /**
                 * 生成 Mipmap 链
                 * 
                 * 第一个 Mip（级别 0）已经存在，所以我们处理 n-1 个级别。
                 * 对于每个级别，我们从上一级别生成当前级别的 Mipmap。
                 */
                for (size_t level = 0; level < levelCount - 1; level++) {
                    /**
                     * 获取渲染通道信息（对应级别）
                     */
                    auto out = resources.getRenderPassInfo(level);
                    
                    /**
                     * 创建纹理视图（当前级别，1 个级别）
                     */
                    auto th = driver.createTextureView(in, level, 1);
                    
                    /**
                     * 设置深度纹理参数
                     * 
                     * 使用 NEAREST_MIPMAP_NEAREST 过滤，因为我们正在生成 Mipmap。
                     */
                    mi->setParameter("depth", th, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                    
                    /**
                     * 提交材质实例并渲染
                     */
                    mi->commit(driver, getUboManager());
                    mi->use(driver);
                    renderFullScreenQuad(out, pipeline, driver);
                    
                    /**
                     * 解绑每材质描述符堆
                     */
                    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);
                    
                    /**
                     * 销毁纹理视图
                     */
                    driver.destroyTexture(th);
                }
                
                /**
                 * 解绑所有描述符堆
                 */
                unbindAllDescriptorSets(driver);
            });

    return { depth, structurePass->picking };
}

/**
 * 透明拾取通道
 * 
 * 生成透明对象的拾取缓冲区，用于对象选择和拾取。
 * 
 * @param fg 帧图引用
 * @param passBuilder 渲染通道构建器
 * @param structureRenderFlags 结构渲染标志
 * @param width 宽度（像素）
 * @param height 高度（像素）
 * @param scale 缩放因子
 * @return 拾取纹理 ID
 * 
 * 处理流程：
 * 1. 创建深度缓冲区和拾取缓冲区
 * 2. 使用拾取变体渲染透明对象
 * 3. 返回拾取纹理
 * 
 * 注意：拾取通道使用 DEPTH 命令类型标志，仅渲染深度通道。
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::transparentPicking(FrameGraph& fg,
        RenderPassBuilder const& passBuilder, uint8_t const structureRenderFlags,
        uint32_t width, uint32_t height, float const scale) noexcept {

    /**
     * 拾取渲染通道数据
     */
    struct PickingRenderPassData {
        FrameGraphId<FrameGraphTexture> depth;    // 深度缓冲区
        FrameGraphId<FrameGraphTexture> picking;  // 拾取缓冲区
    };
    
    /**
     * 添加拾取渲染通道
     */
    auto const& pickingRenderPass = fg.addPass<PickingRenderPassData>("Picking Render Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 检测是否为特性级别 0
                 */
                bool const isFL0 = mEngine.getDriverApi().getFeatureLevel() ==
                    FeatureLevel::FEATURE_LEVEL_0;

                /**
                 * 修正用户提供的缩放因子
                 * 
                 * 确保宽度和高度至少为 32 像素。
                 */
                width  = std::max(32u, uint32_t(std::ceil(float(width) * scale)));
                height = std::max(32u, uint32_t(std::ceil(float(height) * scale)));
                
                /**
                 * 创建深度缓冲区
                 * 
                 * 特性级别 0 使用 DEPTH24，其他级别使用 DEPTH32F。
                 */
                data.depth = builder.createTexture("Depth Buffer", {
                        .width = width, .height = height,
                        .format = isFL0 ? TextureFormat::DEPTH24 : TextureFormat::DEPTH32F });

                /**
                 * 将深度缓冲区声明为深度附件
                 */
                data.depth = builder.write(data.depth,
                        FrameGraphTexture::Usage::DEPTH_ATTACHMENT);

                /**
                 * 创建拾取缓冲区
                 * 
                 * 特性级别 0 使用 RGBA8，其他级别使用 RG32UI（2 通道无符号整数）。
                 * 
                 * TODO: 指定拾取通道的精度
                 */
                data.picking = builder.createTexture("Picking Buffer", {
                        .width = width, .height = height,
                        .format = isFL0 ? TextureFormat::RGBA8 : TextureFormat::RG32UI });

                /**
                 * 将拾取缓冲区声明为颜色附件
                 */
                data.picking = builder.write(data.picking,
                        FrameGraphTexture::Usage::COLOR_ATTACHMENT);

                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("Picking Render Target", {
                        .attachments = {.color = { data.picking }, .depth = data.depth },
                        .clearFlags = TargetBufferFlags::COLOR0 | TargetBufferFlags::DEPTH  // 清除颜色和深度
                    });
            },
            [=, this, passBuilder = passBuilder](FrameGraphResources const& resources,
                    auto const&, DriverApi& driver) mutable {
                        /**
                         * 创建拾取变体
                         * 
                         * 使用深度变体，并启用拾取。
                         */
                        Variant pickingVariant(Variant::DEPTH_VARIANT);
                        pickingVariant.setPicking(true);

                        /**
                         * 绑定描述符堆
                         * 
                         * 绑定结构通道使用的每视图和每渲染对象描述符堆。
                         */
                        getStructureDescriptorSet().bind(driver);        // 绑定结构描述符堆
                        bindPerRenderableDescriptorSet(driver);          // 绑定每渲染对象描述符堆

                        /**
                         * 获取渲染通道信息
                         */
                        auto [target, params] = resources.getRenderPassInfo();
                        
                        /**
                         * 配置渲染通道构建器
                         */
                        passBuilder.renderFlags(structureRenderFlags);                      // 设置渲染标志
                        passBuilder.variant(pickingVariant);                                // 设置变体（启用拾取）
                        passBuilder.commandTypeFlags(RenderPass::CommandTypeFlags::DEPTH); // 设置命令类型标志（仅深度）

                        /**
                         * 构建并执行渲染通道
                         */
                        RenderPass const pass{ passBuilder.build(mEngine, driver) };
                        driver.beginRenderPass(target, params);
                        pass.getExecutor().execute(mEngine, driver);     // 执行渲染通道
                        driver.endRenderPass();
                        
                        /**
                         * 解绑所有描述符堆
                         */
                        unbindAllDescriptorSets(driver);
                });

    /**
     * 返回拾取纹理 ID
     */
    return pickingRenderPass->picking;
}

// ------------------------------------------------------------------------------------------------

/**
 * 屏幕空间反射（SSR）通道
 * 
 * 生成屏幕空间反射效果，用于渲染表面反射。
 * 
 * @param fg 帧图引用
 * @param passBuilder 渲染通道构建器
 * @param frameHistory 帧历史（用于时间累积）
 * @param structure 结构缓冲区（用于光线步进）
 * @param desc 反射纹理描述符
 * @return 反射纹理 ID
 * 
 * 处理流程：
 * 1. 检查上一帧的历史数据是否存在
 * 2. 导入历史反射纹理
 * 3. 创建反射缓冲区和深度缓冲区
 * 4. 使用 SSR 特殊变体渲染反射对象
 * 5. 返回反射纹理
 * 
 * 注意：
 * - 仅适用于具有 SCREEN_SPACE 反射模式的对象
 * - 移除阴影标志，因为反射渲染不需要阴影
 * - 历史缓冲区用于时间累积，提高反射质量
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::ssr(FrameGraph& fg,
        RenderPassBuilder const& passBuilder,
        FrameHistory const& frameHistory,
        FrameGraphId<FrameGraphTexture> const structure,
        FrameGraphTexture::Descriptor const& desc) noexcept {

    /**
     * SSR 通道数据
     * 
     * 包含反射纹理、深度缓冲区和结构缓冲区。
     */
    struct SSRPassData {
        /**
         * 反射贴图（输出）
         * 
         * our output, the reflection map
         */
        FrameGraphId<FrameGraphTexture> reflections;
        /**
         * 深度缓冲区（用于剔除）
         * 
         * we need a depth buffer for culling
         */
        FrameGraphId<FrameGraphTexture> depth;
        /**
         * 结构缓冲区（用于光线步进）
         * 
         * we also need the structure buffer for ray-marching
         */
        FrameGraphId<FrameGraphTexture> structure;
        /**
         * 历史缓冲区（用于获取上一帧的反射）
         * 
         * and the history buffer for fetching the reflections
         */
        FrameGraphId<FrameGraphTexture> history;
    };

    /**
     * 获取上一帧的 SSR 历史数据
     * 
     * 如果历史数据不存在，返回空纹理 ID。
     */
    auto const& previous = frameHistory.getPrevious().ssr;
    if (!previous.color.handle) {
        return {};
    }

    /**
     * 导入 SSR 历史纹理
     * 
     * 从上一帧导入反射纹理，用于时间累积。
     */
    FrameGraphId<FrameGraphTexture> const history = fg.import("SSR history", previous.desc,
            FrameGraphTexture::Usage::SAMPLEABLE, previous.color);

    /**
     * 添加 SSR 渲染通道
     */
    auto const& ssrPass = fg.addPass<SSRPassData>("SSR Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 创建反射缓冲区
                 * 
                 * 我们需要 alpha 通道，因此必须使用 RGBA16F 格式。
                 * 
                 * Create our reflection buffer. We need an alpha channel, so we have to use RGBA16F
                 */
                data.reflections = builder.createTexture("Reflections Texture", {
                        .width = desc.width, .height = desc.height,
                        .format = TextureFormat::RGBA16F });

                /**
                 * 创建深度缓冲区
                 * 
                 * 深度缓冲区永远不会写入内存（仅用于剔除）。
                 * 
                 * create our depth buffer, the depth buffer is never written to memory
                 */
                data.depth = builder.createTexture("Reflections Texture Depth", {
                        .width = desc.width, .height = desc.height,
                        .format = TextureFormat::DEPTH32F });

                /**
                 * 将两个缓冲区声明为写入目标
                 * 
                 * we're writing to both these buffers
                 */
                data.reflections = builder.write(data.reflections,
                        FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.depth = builder.write(data.depth,
                        FrameGraphTexture::Usage::DEPTH_ATTACHMENT);

                /**
                 * 声明渲染目标
                 * 
                 * finally declare our render target
                 */
                builder.declareRenderPass("Reflections Target", {
                        .attachments = { .color = { data.reflections }, .depth = data.depth },
                        .clearFlags = TargetBufferFlags::COLOR0 | TargetBufferFlags::DEPTH });

                /**
                 * 获取结构缓冲区（用于光线步进）
                 * 
                 * get the structure buffer
                 */
                assert_invariant(structure);
                data.structure = builder.sample(structure);

                /**
                 * 如果历史纹理存在，采样它
                 */
                if (history) {
                    data.history = builder.sample(history);
                }
            },
            [this, passBuilder = passBuilder](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) mutable {
                /**
                 * 准备结构采样器
                 * 
                 * set structure sampler
                 */
                mSsrPassDescriptorSet.prepareStructure(mEngine, data.structure ?
                        resources.getTexture(data.structure) : getOneTexture());

                /**
                 * 准备历史采样器（常规 texture2D）
                 * 
                 * the history sampler is a regular texture2D
                 */
                TextureHandle const history = data.history ?
                        resources.getTexture(data.history) : getZeroTexture();
                mSsrPassDescriptorSet.prepareHistorySSR(mEngine, history);

                /**
                 * 提交并绑定描述符堆
                 */
                mSsrPassDescriptorSet.commit(mEngine);
                mSsrPassDescriptorSet.bind(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息
                 */
                auto const out = resources.getRenderPassInfo();

                /**
                 * 移除 HAS_SHADOWING 渲染标志
                 * 
                 * 因为在渲染反射时阴影是无关的。
                 * 
                 * Remove the HAS_SHADOWING RenderFlags, since it's irrelevant when rendering reflections
                 */
                passBuilder.renderFlags(RenderPass::HAS_SHADOWING, 0);

                /**
                 * 使用特殊的 SSR 变体
                 * 
                 * 只能应用于具有 SCREEN_SPACE 反射模式的对象。
                 * 
                 * use our special SSR variant, it can only be applied to object that have
                 * the SCREEN_SPACE ReflectionMode.
                 */
                passBuilder.variant(Variant{ Variant::SPECIAL_SSR });

                /**
                 * 生成所有绘制命令（除了混合对象）
                 * 
                 * generate all our drawing commands, except blended objects.
                 */
                passBuilder.commandTypeFlags(RenderPass::CommandTypeFlags::SCREEN_SPACE_REFLECTIONS);

                /**
                 * 构建并执行渲染通道
                 */
                RenderPass const pass{ passBuilder.build(mEngine, driver) };
                driver.beginRenderPass(out.target, out.params);
                pass.getExecutor().execute(mEngine, driver);
                driver.endRenderPass();
                unbindAllDescriptorSets(driver);
            });

    return ssrPass->reflections;
}

/**
 * 屏幕空间环境光遮蔽（SSAO）通道
 * 
 * 生成屏幕空间环境光遮蔽效果，用于增强场景的深度感和细节。
 * 支持 SAO（Scalable Ambient Occlusion）和 GTAO（Ground Truth Ambient Occlusion）两种算法。
 * 
 * @param fg 帧图引用
 * @param viewport 视口（未使用）
 * @param cameraInfo 相机信息
 * @param depth 深度纹理 ID（必须包含 Mipmap 链）
 * @param options SSAO 选项（包含质量级别、采样数、半径等）
 * @return SSAO 纹理 ID
 * 
 * 处理流程：
 * 1. 根据质量级别设置采样数、螺旋圈数和标准差
 * 2. 配置双边滤波参数
 * 3. 如果需要，复制深度纹理（避免反馈循环）
 * 4. 执行 SSAO 主通道（支持 SAO/GTAO）
 * 5. 执行可分离双边模糊通道（如果启用）
 * 6. 返回最终的 SSAO 纹理
 * 
 * 注意：
 * - 深度纹理必须包含 Mipmap 链
 * - 支持 Bent Normals（弯曲法线）计算
 * - 支持高质量上采样
 * - 支持接触阴影（SSCT）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::screenSpaceAmbientOcclusion(FrameGraph& fg,
        filament::Viewport const&, const CameraInfo& cameraInfo,
        FrameGraphId<FrameGraphTexture> const depth,
        AmbientOcclusionOptions const& options) noexcept {
    assert_invariant(depth);

    /**
     * 获取深度纹理的 Mipmap 级别数
     */
    const size_t levelCount = fg.getDescriptor(depth).levels;

    /**
     * 高斯滤波器说明
     * 
     * 设 q 为标准差，高斯滤波器需要 6q-1 个值来保持其高斯性质
     * （参见 en.wikipedia.org/wiki/Gaussian_filter）
     * 更直观地说，2q 是滤波器的宽度（以像素为单位）。
     * 
     * With q the standard deviation,
     * A gaussian filter requires 6q-1 values to keep its gaussian nature
     * (see en.wikipedia.org/wiki/Gaussian_filter)
     * More intuitively, 2q is the width of the filter in pixels.
     */
    /**
     * 双边滤波配置
     */
    BilateralPassConfig config = {
            .bentNormals = options.bentNormals,
            .bilateralThreshold = options.bilateralThreshold,
    };

    /**
     * 根据质量级别设置采样参数
     */
    float sampleCount{};  // 采样数
    float spiralTurns{};  // 螺旋圈数
    float standardDeviation{};  // 标准差
    switch (options.quality) {
        default:
        case QualityLevel::LOW:
            sampleCount = 7.0f;
            spiralTurns = 3.0f;
            standardDeviation = 8.0;
            break;
        case QualityLevel::MEDIUM:
            sampleCount = 11.0f;
            spiralTurns = 6.0f;
            standardDeviation = 8.0;
            break;
        case QualityLevel::HIGH:
            sampleCount = 16.0f;
            spiralTurns = 7.0f;
            standardDeviation = 6.0;
            break;
        case QualityLevel::ULTRA:
            sampleCount = 32.0f;
            spiralTurns = 14.0f;
            standardDeviation = 4.0;
            break;
    }

    /**
     * 根据低通滤波器质量级别配置双边滤波参数
     */
    switch (options.lowPassFilter) {
        default:
        case QualityLevel::LOW:
            /**
             * 无滤波，值无关紧要
             * 
             * no filtering, values don't matter
             */
            config.kernelSize = 1;
            config.standardDeviation = 1.0f;
            config.scale = 1.0f;
            break;
        case QualityLevel::MEDIUM:
            config.kernelSize = 11;
            config.standardDeviation = standardDeviation * 0.5f;
            config.scale = 2.0f;
            break;
        case QualityLevel::HIGH:
        case QualityLevel::ULTRA:
            config.kernelSize = 23;
            config.standardDeviation = standardDeviation;
            config.scale = 1.0f;
            break;
    }

    /**
     * 调试选项（已注释）
     * 
     * for debugging
     */
    //config.kernelSize = engine.debug.ssao.kernelSize;
    //config.standardDeviation = engine.debug.ssao.stddev;
    //sampleCount = engine.debug.ssao.sampleCount;
    //spiralTurns = engine.debug.ssao.spiralTurns;

    /**
     * SSAO 主通道
     * 
     * Our main SSAO pass
     */

    /**
     * SSAO 通道数据
     */
    struct SSAOPassData {
        FrameGraphId<FrameGraphTexture> depth;  // 深度纹理
        FrameGraphId<FrameGraphTexture> ssao;   // SSAO 纹理（数组或单个）
        FrameGraphId<FrameGraphTexture> ao;     // 环境光遮蔽纹理
        FrameGraphId<FrameGraphTexture> bn;     // 弯曲法线纹理
    };

    /**
     * 是否计算弯曲法线
     */
    const bool computeBentNormals = options.bentNormals;

    /**
     * 是否启用高质量上采样
     * 
     * 当上采样质量 >= HIGH 且分辨率 < 1.0 时启用。
     */
    const bool highQualityUpsampling =
            options.upsampling >= QualityLevel::HIGH && options.resolution < 1.0f;

    /**
     * 是否启用低通滤波
     */
    const bool lowPassFilterEnabled = options.lowPassFilter != QualityLevel::LOW;

    /**
     * 深度纹理反馈循环处理
     * 
     * GLES 认为当缓冲区既作为纹理又作为附件使用时存在反馈循环，
     * 即使未启用写入。此限制在桌面 GL 和 Vulkan 上已取消。Metal 的情况尚不清楚。
     * 在这种情况下，我们需要复制深度纹理以用作附件。
     * 
     * 由于类似的原因，Vulkan 也需要这样做。
     * 
     * GLES considers there is a feedback loop when a buffer is used as both a texture and
     * attachment, even if writes are not enabled. This restriction is lifted on desktop GL and
     * Vulkan. The Metal situation is unclear.
     * In this case, we need to duplicate the depth texture to use it as an attachment.
     *
     * This is also needed in Vulkan for a similar reason.
     */
    FrameGraphId<FrameGraphTexture> duplicateDepthOutput = {};
    if (!mWorkaroundAllowReadOnlyAncillaryFeedbackLoop) {
        duplicateDepthOutput = blitDepth(fg, depth);
    }

    /**
     * 添加 SSAO 渲染通道
     */
    auto const& SSAOPass = fg.addPass<SSAOPassData>(
            "SSAO Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取深度纹理描述符
                 */
                auto const& desc = builder.getDescriptor(depth);

                /**
                 * 采样深度纹理（用于 AO 计算）
                 */
                data.depth = builder.sample(depth);
                
                /**
                 * 创建 SSAO 缓冲区
                 * 
                 * - 如果计算弯曲法线，使用 2 层数组（AO 和 Bent Normals）
                 * - 根据是否启用低通滤波/高质量上采样/弯曲法线选择格式：
                 *   - 需要：RGB8（3 通道）
                 *   - 否则：R8（单通道）
                 */
                data.ssao = builder.createTexture("SSAO Buffer", {
                        .width = desc.width,
                        .height = desc.height,
                        .depth = computeBentNormals ? 2u : 1u,
                        .type = Texture::Sampler::SAMPLER_2D_ARRAY,
                        .format = (lowPassFilterEnabled || highQualityUpsampling || computeBentNormals) ?
                                TextureFormat::RGB8 : TextureFormat::R8
                });

                /**
                 * 如果计算弯曲法线，创建两个子资源（AO 层和 Bent Normals 层）
                 */
                if (computeBentNormals) {
                    data.ao = builder.createSubresource(data.ssao, "SSAO attachment", { .layer = 0 });
                    data.bn = builder.createSubresource(data.ssao, "Bent Normals attachment", { .layer = 1 });
                    data.ao = builder.write(data.ao, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    data.bn = builder.write(data.bn, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                } else {
                    /**
                     * 否则直接使用 SSAO 纹理作为 AO 输出
                     */
                    data.ao = data.ssao;
                    data.ao = builder.write(data.ao, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                }

                /**
                 * 深度测试说明
                 * 
                 * 我们使用深度测试来跳过无穷远处的像素（例如天空盒）。
                 * 注意：我们必须清除 SAO 缓冲区，因为混合对象最终会读取它，
                 * 即使它们没有写入深度缓冲区。
                 * 模糊通道中的双边滤波器将忽略无穷远处的像素。
                 * 
                 * Here we use the depth test to skip pixels at infinity (i.e. the skybox)
                 * Note that we have to clear the SAO buffer because blended objects will end-up
                 * reading into it even though they were not written in the depth buffer.
                 * The bilateral filter in the blur pass will ignore pixels at infinity.
                 */

                /**
                 * 选择深度附件（如果已复制深度纹理，使用副本）
                 */
                auto depthAttachment = duplicateDepthOutput ? duplicateDepthOutput : data.depth;

                /**
                 * 将深度纹理声明为只读深度附件
                 */
                depthAttachment = builder.read(depthAttachment,
                        FrameGraphTexture::Usage::DEPTH_ATTACHMENT);
                
                /**
                 * 声明渲染目标
                 * 
                 * 清除颜色缓冲区为 1.0（全白，表示无遮挡）
                 */
                builder.declareRenderPass("SSAO Target", {
                        .attachments = { .color = { data.ao, data.bn }, .depth = depthAttachment },
                        .clearColor = { 1.0f },
                        .clearFlags = TargetBufferFlags::COLOR0 | TargetBufferFlags::COLOR1
                });
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定每视图描述符堆（用于结构通道）
                 * 
                 * bind the per-view descriptorSet that is used for the structure pass
                 */
                getStructureDescriptorSet().bind(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理和渲染通道信息
                 */
                auto depth = resources.getTexture(data.depth);
                auto ssao = resources.getRenderPassInfo();
                auto const& desc = resources.getDescriptor(data.depth);

                /**
                 * 投影尺度估计
                 * 
                 * 估算在 z=-1 处（即距离 1 米）观看的 1 米高/宽对象在像素单位中的大小。
                 * 
                 * Estimate of the size in pixel units of a 1m tall/wide object viewed from 1m away (i.e. at z=-1)
                 */
                const float projectionScale = std::min(
                        0.5f * cameraInfo.projection[0].x * desc.width,
                        0.5f * cameraInfo.projection[1].y * desc.height);

                /**
                 * 计算逆投影矩阵和螺旋角度增量
                 */
                const auto invProjection = inverse(cameraInfo.projection);
                const float inc = (1.0f / (sampleCount - 0.5f)) * spiralTurns * f::TAU;

                /**
                 * 屏幕空间到裁剪空间的转换矩阵
                 * 
                 * 将裁剪空间坐标（[-1, 1]）转换为屏幕空间坐标（[0, width/height]）。
                 */
                const mat4 screenFromClipMatrix{ mat4::row_major_init{
                        0.5 * desc.width, 0.0, 0.0, 0.5 * desc.width,
                        0.0, 0.5 * desc.height, 0.0, 0.5 * desc.height,
                        0.0, 0.0, 0.5, 0.5,
                        0.0, 0.0, 0.0, 1.0
                }};

                /**
                 * 选择材质名称（根据 AO 类型和是否计算弯曲法线）
                 */
                std::string_view materialName;
                auto aoType = options.aoType;
#ifdef FILAMENT_DISABLE_GTAO
                /**
                 * 如果禁用 GTAO，强制使用 SAO
                 */
                materialName = computeBentNormals ? "saoBentNormals" : "sao";
                aoType = AmbientOcclusionOptions::AmbientOcclusionType::SAO;
#else
                if (aoType ==
                        AmbientOcclusionOptions::AmbientOcclusionType::GTAO) {
                    materialName = computeBentNormals ? "gtaoBentNormals" : "gtao";
                } else {
                    materialName = computeBentNormals ? "saoBentNormals" : "sao";
                }
#endif
                auto& material = getPostProcessMaterial(materialName);

                /**
                 * 设置 GTAO 特化常量参数（如果使用 GTAO）
                 */
                FMaterial* ma = material.getMaterial(mEngine);
                bool dirty = false;
                setConstantParameter(ma, "useVisibilityBitmasks", options.gtao.useVisibilityBitmasks, dirty);
                setConstantParameter(ma, "linearThickness", options.gtao.linearThickness, dirty);
                if (dirty) {
                   /**
                    * 如果参数改变，使材质无效
                    * 
                    * TODO: 调用 Material::compile()，但目前无法这样做，
                    *       因为它只适用于表面材质。
                    */
                   ma->invalidate();
                   // TODO: call Material::compile(), we can't do that now because it works only
                   //       with surface materials
                }

                /**
                 * 获取材质实例并设置 AO 类型特定的参数
                 */
                ma = material.getMaterial(mEngine);
                FMaterialInstance* const mi = getMaterialInstance(ma);

                /**
                 * 设置 AO 类型特定的材质参数
                 * 
                 * Set AO type specific material parameters
                 */
                switch (aoType) {
                    case AmbientOcclusionOptions::AmbientOcclusionType::SAO: {
                        /**
                         * SAO 参数设置
                         */
                        /**
                         * 衰减函数峰值位置
                         * 
                         * Where the falloff function peaks
                         */
                        const float peak = 0.1f * options.radius;
                        const float intensity = (f::TAU * peak) * options.intensity;

                        /**
                         * 总是对 AO 结果求平方，看起来更好
                         * 
                         * always square AO result, as it looks much better
                         */
                        const float power = options.power * 2.0f;

                        mi->setParameter("minHorizonAngleSineSquared",
                                std::pow(std::sin(options.minHorizonAngleRad), 2.0f));
                        mi->setParameter("intensity", intensity / sampleCount);
                        mi->setParameter("power", power);
                        mi->setParameter("peak2", peak * peak);
                        mi->setParameter("bias", options.bias);
                        mi->setParameter("sampleCount",
                                float2{ sampleCount, 1.0f / (sampleCount - 0.5f) });
                        mi->setParameter("spiralTurns", spiralTurns);
                        mi->setParameter("angleIncCosSin", float2{ std::cos(inc), std::sin(inc) });
                        break;
                    }
                    case AmbientOcclusionOptions::AmbientOcclusionType::GTAO: {
                        /**
                         * GTAO 参数设置
                         */
                        const auto sliceCount = static_cast<float>(options.gtao.sampleSliceCount);
                        mi->setParameter("stepsPerSlice",
                                static_cast<float>(options.gtao.sampleStepsPerSlice));
                        mi->setParameter("sliceCount",
                                float2{ sliceCount, 1.0f / sliceCount });
                        mi->setParameter("power", options.power);
                        mi->setParameter("radius", options.radius);
                        mi->setParameter("intensity", options.intensity);
                        mi->setParameter("thicknessHeuristic", options.gtao.thicknessHeuristic);
                        mi->setParameter("constThickness", options.gtao.constThickness);

                        break;
                    }
                }

                /**
                 * 设置通用材质参数
                 * 
                 * Set common material parameters
                 */
                mi->setParameter("invRadiusSquared", 1.0f / (options.radius * options.radius));
                mi->setParameter("depth", depth, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                mi->setParameter("screenFromViewMatrix",
                        mat4f(screenFromClipMatrix * cameraInfo.projection));
                mi->setParameter("resolution",
                        float4{ desc.width, desc.height, 1.0f / desc.width, 1.0f / desc.height });
                mi->setParameter("projectionScale", projectionScale);
                mi->setParameter("projectionScaleRadius", projectionScale * options.radius);
                mi->setParameter("positionParams",
                        float2{ invProjection[0][0], invProjection[1][1] } * 2.0f);
                mi->setParameter("maxLevel", uint32_t(levelCount - 1));
                mi->setParameter("invFarPlane", 1.0f / -cameraInfo.zf);
                
                /**
                 * 设置接触阴影（SSCT）参数
                 */
                mi->setParameter("ssctShadowDistance", options.ssct.shadowDistance);
                mi->setParameter("ssctConeAngleTangeant",
                        std::tan(options.ssct.lightConeRad * 0.5f));
                mi->setParameter("ssctContactDistanceMaxInv",
                        1.0f / options.ssct.contactDistanceMax);
                
                /**
                 * 计算视图空间中的光线方向
                 * 
                 * light direction in view space
                 */
                const mat4f view{ cameraInfo.getUserViewMatrix() };
                const float3 l = normalize(
                        mat3f::getTransformForNormals(view.upperLeft())
                                * options.ssct.lightDirection);
                mi->setParameter("ssctIntensity",
                        options.ssct.enabled ? options.ssct.intensity : 0.0f);
                mi->setParameter("ssctVsLightDirection", -l);
                mi->setParameter("ssctDepthBias",
                        float2{ options.ssct.depthBias, options.ssct.depthSlopeBias });
                mi->setParameter("ssctSampleCount", uint32_t(options.ssct.sampleCount));
                mi->setParameter("ssctRayCount",
                        float2{ options.ssct.rayCount, 1.0f / float(options.ssct.rayCount) });

                /**
                 * 提交材质实例并渲染
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取管道状态并设置深度函数为小于（L）
                 * 
                 * 这确保只有深度值小于深度缓冲区值的像素才会被渲染。
                 */
                auto pipeline = getPipelineState(ma);
                pipeline.rasterState.depthFunc = RasterState::DepthFunc::L;
                assert_invariant(ssao.params.readOnlyDepthStencil & RenderPassParams::READONLY_DEPTH);
                renderFullScreenQuad(ssao, pipeline, driver);
                unbindAllDescriptorSets(driver);
            });

    /**
     * 获取 SSAO 输出纹理
     */
    FrameGraphId<FrameGraphTexture> ssao = SSAOPass->ssao;

    /**
     * 最终的可分离双边模糊通道
     * 
     * Final separable bilateral blur pass
     */

    /**
     * 如果启用低通滤波，执行两次可分离双边模糊（水平和垂直）
     */
    if (lowPassFilterEnabled) {
        /**
         * 水平模糊
         */
        ssao = bilateralBlurPass(fg, ssao, depth, { config.scale, 0 },
                cameraInfo.zf,
                TextureFormat::RGB8,
                config);

        /**
         * 垂直模糊
         * 
         * 根据是否启用高质量上采样或计算弯曲法线选择格式。
         */
        ssao = bilateralBlurPass(fg, ssao, depth, { 0, config.scale },
                cameraInfo.zf,
                (highQualityUpsampling || computeBentNormals) ? TextureFormat::RGB8
                                                              : TextureFormat::R8,
                config);
    }

    return ssao;
}

/**
 * 双边模糊通道
 * 
 * 对 SSAO 纹理执行可分离的双边模糊，保持深度边缘的锐度。
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID（要模糊的 SSAO 纹理）
 * @param depth 深度纹理 ID（用于双边滤波）
 * @param axis 模糊轴（{scale, 0} 为水平，{0, scale} 为垂直）
 * @param zf 远平面 Z 值（用于深度计算）
 * @param format 输出纹理格式
 * @param config 双边滤波配置
 * @return 模糊后的纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理
 * 2. 采样输入和深度纹理
 * 3. 设置双边模糊材质参数
 * 4. 执行全屏四边形渲染
 * 
 * 注意：双边模糊使用深度信息来避免模糊跨越深度不连续性，
 *       从而保持边缘的锐度。
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::bilateralBlurPass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphId<FrameGraphTexture> depth,
        int2 const axis, float const zf, TextureFormat const format,
        BilateralPassConfig const& config) noexcept {
    assert_invariant(depth);

    /**
     * 模糊通道数据
     */
    struct BlurPassData {
        FrameGraphId<FrameGraphTexture> input;  // 输入纹理
        FrameGraphId<FrameGraphTexture> blurred; // 模糊输出纹理
        FrameGraphId<FrameGraphTexture> ao;     // AO 附件
        FrameGraphId<FrameGraphTexture> bn;     // Bent Normals 附件
    };

    /**
     * 添加可分离模糊通道
     */
    auto const& blurPass = fg.addPass<BlurPassData>("Separable Blur Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取输入纹理描述符
                 */
                auto const& desc = builder.getDescriptor(input);

                /**
                 * 采样输入纹理
                 */
                data.input = builder.sample(input);

                /**
                 * 创建模糊输出纹理
                 */
                data.blurred = builder.createTexture("Blurred output", {
                        .width = desc.width,
                        .height = desc.height,
                        .depth = desc.depth,
                        .type = desc.type,
                        .format = format });

                /**
                 * 读取深度纹理作为深度附件（用于双边滤波）
                 */
                depth = builder.read(depth, FrameGraphTexture::Usage::DEPTH_ATTACHMENT);

                /**
                 * 如果计算弯曲法线，创建两个子资源（AO 层和 Bent Normals 层）
                 */
                if (config.bentNormals) {
                    data.ao = builder.createSubresource(data.blurred, "SSAO attachment", { .layer = 0 });
                    data.bn = builder.createSubresource(data.blurred, "Bent Normals attachment", { .layer = 1 });
                    data.ao = builder.write(data.ao, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    data.bn = builder.write(data.bn, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                } else {
                    /**
                     * 否则直接使用模糊纹理作为 AO 输出
                     */
                    data.ao = data.blurred;
                    data.ao = builder.write(data.ao, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                }

                /**
                 * 深度测试说明
                 * 
                 * 我们使用深度测试来跳过无穷远处的像素（例如天空盒）。
                 * 我们需要清除缓冲区，因为我们要跳过无穷远处的像素（天空盒）。
                 * 
                 * Here we use the depth test to skip pixels at infinity (i.e. the skybox)
                 * We need to clear the buffers because we are skipping pixels at infinity (skybox)
                 */
                data.blurred = builder.write(data.blurred, FrameGraphTexture::Usage::COLOR_ATTACHMENT);

                /**
                 * 声明渲染目标
                 * 
                 * 清除颜色缓冲区为 1.0（全白，表示无遮挡）
                 */
                builder.declareRenderPass("Blurred target", {
                        .attachments = { .color = { data.ao, data.bn }, .depth = depth },
                        .clearColor = { 1.0f },
                        .clearFlags = TargetBufferFlags::COLOR0 | TargetBufferFlags::COLOR1
                });
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 * 
                 * TODO: 结构描述符堆可能不是最佳选择。
                 * 
                 * TODO: the structure descriptor set might not be the best fit.
                 */
                getStructureDescriptorSet().bind(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理和渲染通道信息
                 */
                auto ssao = resources.getTexture(data.input);
                auto blurred = resources.getRenderPassInfo();
                auto const& desc = resources.getDescriptor(data.blurred);

                /**
                 * 高斯半核函数（未归一化）
                 * 
                 * 给定标准差的未归一化高斯半核。
                 * 返回存储在数组中的采样数（最多 16 个）。
                 * 
                 * unnormalized gaussian half-kernel of a given standard deviation
                 * returns number of samples stored in the array (max 16)
                 */
                constexpr size_t kernelArraySize = 16; // limited by bilateralBlur.mat
                auto gaussianKernel =
                        [kernelArraySize](float* outKernel, size_t const gaussianWidth, float const stdDev) -> uint32_t {
                    /**
                     * 计算高斯采样数（取最小值以避免溢出）
                     */
                    const size_t gaussianSampleCount = std::min(kernelArraySize, (gaussianWidth + 1u) / 2u);
                    /**
                     * 生成高斯核值
                     */
                    for (size_t i = 0; i < gaussianSampleCount; i++) {
                        float const x = float(i);
                        float const g = std::exp(-(x * x) / (2.0f * stdDev * stdDev));
                        outKernel[i] = g;
                    }
                    return uint32_t(gaussianSampleCount);
                };

                /**
                 * 生成高斯核
                 */
                float kGaussianSamples[kernelArraySize];
                uint32_t const kGaussianCount = gaussianKernel(kGaussianSamples,
                        config.kernelSize, config.standardDeviation);

                /**
                 * 根据是否计算弯曲法线选择材质
                 */
                auto& material = config.bentNormals ?
                        getPostProcessMaterial("bilateralBlurBentNormals") :
                        getPostProcessMaterial("bilateralBlur");
                FMaterial const* const ma = material.getMaterial(mEngine);
                FMaterialInstance* const mi = getMaterialInstance(ma);
                
                /**
                 * 设置材质参数
                 */
                mi->setParameter("ssao", ssao, { /* only reads level 0 */ });  // 只读取级别 0
                mi->setParameter("axis", axis / float2{desc.width, desc.height});  // 模糊轴（归一化）
                mi->setParameter("kernel", kGaussianSamples, kGaussianCount);  // 高斯核
                mi->setParameter("sampleCount", kGaussianCount);  // 采样数
                mi->setParameter("farPlaneOverEdgeDistance", -zf / config.bilateralThreshold);  // 远平面/边缘距离比

                /**
                 * 提交材质实例并渲染
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取管道状态并设置深度函数为小于（L）
                 */
                auto pipeline = getPipelineState(ma);
                pipeline.rasterState.depthFunc = RasterState::DepthFunc::L;
                renderFullScreenQuad(blurred, pipeline, driver);
                unbindAllDescriptorSets(driver);
            });

    return blurPass->blurred;
}

/**
 * 生成高斯 Mipmap 链
 * 
 * 为输入纹理生成高斯模糊的 Mipmap 链。
 * 每个 Mipmap 级别都是通过高斯模糊上一级别生成的。
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID
 * @param levels 要生成的 Mipmap 级别数
 * @param reinhard 是否在第一个级别应用 Reinhard 色调映射
 * @param kernelWidth 高斯核宽度
 * @param sigma 高斯核标准差
 * @return 输入纹理 ID（我们只写入子资源）
 * 
 * 处理流程：
 * 1. 为每个要生成的级别创建子资源
 * 2. 对每个级别执行高斯模糊（使用上一级别作为源）
 * 3. 返回原始输入纹理（因为我们只写入子资源）
 * 
 * 注意：
 * - Reinhard 色调映射只应用于第一个级别
 * - 后续级别使用前一级别的结果作为源
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::generateGaussianMipmap(FrameGraph& fg,
        const FrameGraphId<FrameGraphTexture> input, size_t const levels,
        bool reinhard, size_t const kernelWidth, float const sigma) noexcept {

    /**
     * 获取输入纹理的子资源描述符
     */
    auto const subResourceDesc = fg.getSubResourceDescriptor(input);

    /**
     * 为每个要生成的级别创建一个子资源。这些将是我们的目标。
     * 
     * Create one subresource per level to be generated from the input. These will be our
     * destinations.
     */
    struct MipmapPassData {
        FixedCapacityVector<FrameGraphId<FrameGraphTexture>> out;  // 输出子资源列表
    };
    auto const& mipmapPass = fg.addPass<MipmapPassData>("Mipmap Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 预留空间并创建每个级别的子资源
                 */
                data.out.reserve(levels - 1);
                for (size_t i = 1; i < levels; i++) {
                    data.out.push_back(builder.createSubresource(input,
                            "Mipmap output", {
                                    .level = uint8_t(subResourceDesc.level + i),
                                    .layer = subResourceDesc.layer }));
                }
            });


    /**
     * 然后为每个级别生成模糊通道，使用上一级别作为源
     * 
     * Then generate a blur pass for each level, using the previous level as source
     */
    auto from = input;
    for (size_t i = 0; i < levels - 1; i++) {
        auto const output = mipmapPass->out[i];
        from = gaussianBlurPass(fg, from, output, reinhard, kernelWidth, sigma);
        reinhard = false; // only do the reinhard filtering on the first level
    }

    /**
     * 返回原始输入（我们只写入子资源）
     * 
     * return our original input (we only wrote into sub resources)
     */
    return input;
}

/**
 * 高斯模糊通道（可分离）
 * 
 * 对输入纹理执行可分离的高斯模糊，生成输出纹理。
 * 使用水平和垂直两次通道实现二维高斯模糊。
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID
 * @param output 输出纹理 ID（如果为空，将创建新纹理）
 * @param reinhard 是否应用 Reinhard 色调映射
 * @param kernelWidth 高斯核宽度
 * @param sigma 高斯核标准差
 * @return 输出纹理 ID
 * 
 * 处理流程：
 * 1. 计算高斯系数（利用线性采样优化）
 * 2. 创建临时缓冲区（水平模糊结果）
 * 3. 执行水平模糊通道
 * 4. 执行垂直模糊通道
 * 5. 返回输出纹理
 * 
 * 注意：
 * - 这是可分离的模糊，通过两次一维模糊实现二维效果
 * - 利用线性采样减少采样次数
 * - 有效核大小是 (kMaxPositiveKernelSize - 1) * 4 + 1
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::gaussianBlurPass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphId<FrameGraphTexture> output,
        bool const reinhard, size_t kernelWidth, const float sigma) noexcept {

    /**
     * 计算高斯系数
     * 
     * 利用线性采样优化，返回存储的系数数量。
     * 
     * @param kernel 输出核数组（存储系数和偏移）
     * @param size 数组大小
     * @return 存储的系数数量
     */
    /**
     * 计算高斯系数 lambda 函数
     * 
     * 利用线性采样优化，将高斯核系数存储为权重和偏移对。
     * 这允许使用双线性采样来模拟多个采样点，大大减少纹理采样次数。
     * 
     * @param kernel 输出核数组（存储系数和偏移，x=权重，y=偏移）
     * @param size 数组大小
     * @return 存储的系数数量
     * 
     * 优化原理：
     * - 使用双线性采样可以在一次采样中获取两个相邻像素的加权平均
     * - 通过调整采样偏移，可以模拟多个采样点的效果
     * - 这减少了所需的纹理采样次数
     * 
     * 核存储格式：
     * 原始高斯系数（右侧）：| 0 | 1 | 2 | 3 | 4 | 5 | 6 |
     * 存储的系数（右侧）：  | 0 |   1   |   2   |   3   |
     * 
     * 每个存储的系数包含：
     * - x: 组合权重（k0 + k1）
     * - y: 采样偏移（用于双线性插值）
     */
    auto computeGaussianCoefficients =
            [kernelWidth, sigma](float2* kernel, size_t const size) -> size_t {
        /**
         * 高斯函数参数：alpha = 1 / (2 * sigma^2)
         * 
         * 用于计算高斯权重：exp(-alpha * x^2)
         */
        const float alpha = 1.0f / (2.0f * sigma * sigma);

        // number of positive-side samples needed, using linear sampling
        /**
         * 计算所需的正侧采样数（使用线性采样优化）
         * 
         * 由于使用双线性采样，每个存储的系数可以覆盖 2 个原始采样点
         * 所以需要的采样数 = (kernelWidth - 1) / 4 + 1
         */
        size_t m = (kernelWidth - 1) / 4 + 1;
        // clamp to what we have
        /**
         * 限制到可用数组大小
         */
        m = std::min(size, m);

        // How the kernel samples are stored:
        //  *===*---+---+---+---+---+---+
        //  | 0 | 1 | 2 | 3 | 4 | 5 | 6 |       Gaussian coefficients (right size)
        //  *===*-------+-------+-------+
        //  | 0 |   1   |   2   |   3   |       stored coefficients (right side)
        /**
         * 核采样存储方式说明：
         * 
         * 原始高斯系数（右侧）：| 0 | 1 | 2 | 3 | 4 | 5 | 6 |
         * 存储的系数（右侧）：  | 0 |   1   |   2   |   3   |
         * 
         * 每个存储的系数覆盖 2 个原始采样点
         */

        /**
         * 设置中心采样点（索引 0）
         * 
         * x = 1.0（权重），y = 0.0（无偏移，直接采样中心）
         */
        kernel[0].x = 1.0;
        kernel[0].y = 0.0;
        /**
         * 初始化总权重（用于归一化）
         */
        float totalWeight = kernel[0].x;

        /**
         * 计算其他采样点的系数
         */
        for (size_t i = 1; i < m; i++) {
            /**
             * 计算两个相邻采样点的位置
             * 
             * x0 和 x1 是相对于中心的距离
             */
            float const x0 = float(i * 2 - 1);
            float const x1 = float(i * 2);
            /**
             * 计算两个采样点的高斯权重
             */
            float const k0 = std::exp(-alpha * x0 * x0);
            float const k1 = std::exp(-alpha * x1 * x1);

            // k * textureLod(..., o) with bilinear sampling is equivalent to:
            //      k * (s[0] * (1 - o) + s[1] * o)
            // solve:
            //      k0 = k * (1 - o)
            //      k1 = k * o
            /**
             * 使用双线性采样优化
             * 
             * 双线性采样 textureLod(..., o) 等价于：
             *   k * (s[0] * (1 - o) + s[1] * o)
             * 
             * 求解：
             *   k0 = k * (1 - o)  =>  k = k0 + k1
             *   k1 = k * o        =>  o = k1 / k
             */

            /**
             * 计算组合权重（两个采样点的权重之和）
             */
            float const k = k0 + k1;
            /**
             * 计算采样偏移（用于双线性插值）
             */
            float const o = k1 / k;
            /**
             * 存储权重和偏移
             */
            kernel[i].x = k;
            kernel[i].y = o;
            /**
             * 累加总权重（乘以 2 因为对称）
             */
            totalWeight += (k0 + k1) * 2.0f;
        }
        /**
         * 归一化所有权重（确保总和为 1）
         */
        for (size_t i = 0; i < m; i++) {
            kernel[i].x *= 1.0f / totalWeight;
        }
        return m;
    };

    struct BlurPassData {
        FrameGraphId<FrameGraphTexture> in;
        FrameGraphId<FrameGraphTexture> out;
        FrameGraphId<FrameGraphTexture> temp;
    };

    // The effective kernel size is (kMaxPositiveKernelSize - 1) * 4 + 1.
    // e.g.: 5 positive-side samples, give 4+1+4=9 samples both sides
    // taking advantage of linear filtering produces an effective kernel of 8+1+8=17 samples
    // and because it's a separable filter, the effective 2D filter kernel size is 17*17
    // The total number of samples needed over the two passes is 18.

    /**
     * 可分离高斯模糊通道
     * 
     * 使用两通道方法：先水平模糊，再垂直模糊，这样可以大大减少采样次数。
     * 有效核大小 = (kMaxPositiveKernelSize - 1) * 4 + 1
     * 例如：5 个正侧采样，得到 4+1+4=9 个采样（两侧）
     * 利用线性过滤产生有效核大小 8+1+8=17 个采样
     * 因为是可分离滤波器，有效的 2D 滤波器核大小为 17*17
     * 两个通道总共需要的采样数为 18
     */
    auto const& blurPass = fg.addPass<BlurPassData>("Gaussian Blur Pass (separable)",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取输入纹理描述符
                 */
                auto const inDesc = builder.getDescriptor(input);

                /**
                 * 如果未提供输出，创建输出纹理
                 */
                if (!output) {
                    output = builder.createTexture("Blurred texture", inDesc);
                }

                /**
                 * 获取输出描述符
                 */
                auto const outDesc = builder.getDescriptor(output);
                /**
                 * 创建临时缓冲区描述符（用于水平模糊）
                 * 
                 * 宽度为目标级别的宽度（因为我们先进行水平模糊）
                 */
                auto tempDesc = inDesc;
                tempDesc.width = outDesc.width; // width of the destination level (b/c we're blurring horizontally)
                tempDesc.levels = 1;
                tempDesc.depth = 1;
                // note: we don't systematically use a Sampler2D for the temp buffer because
                // this could force us to use two different programs below
                /**
                 * 注意：我们不系统地使用 Sampler2D 作为临时缓冲区，
                 * 因为这可能迫使我们使用两个不同的程序
                 */

                /**
                 * 声明输入资源
                 */
                data.in = builder.sample(input);
                /**
                 * 创建水平临时缓冲区
                 */
                data.temp = builder.createTexture("Horizontal temporary buffer", tempDesc);
                /**
                 * 声明临时缓冲区为可采样和渲染目标
                 */
                data.temp = builder.sample(data.temp);
                data.temp = builder.declareRenderPass(data.temp);
                /**
                 * 声明输出为渲染目标
                 */
                data.out = builder.declareRenderPass(output);
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                // don't use auto for those, b/c the ide can't resolve them
                /**
                 * 类型别名（不使用 auto，因为 IDE 无法解析）
                 */
                using FGTD = FrameGraphTexture::Descriptor;
                using FGTSD = FrameGraphTexture::SubResourceDescriptor;

                /**
                 * 获取渲染通道信息和纹理资源
                 */
                auto hwTempRT = resources.getRenderPassInfo(0);  // 临时缓冲区渲染通道
                auto hwOutRT = resources.getRenderPassInfo(1);   // 输出渲染通道
                auto hwTemp = resources.getTexture(data.temp);   // 临时缓冲区纹理
                auto hwIn = resources.getTexture(data.in);       // 输入纹理
                /**
                 * 获取纹理描述符和子资源描述符
                 */
                FGTD const& inDesc = resources.getDescriptor(data.in);
                FGTSD const& inSubDesc = resources.getSubResourceDescriptor(data.in);
                FGTD const& outDesc = resources.getDescriptor(data.out);
                FGTD const& tempDesc = resources.getDescriptor(data.temp);

                /**
                 * 根据输出格式的通道数和纹理类型选择材质
                 */
                using namespace std::literals;
                std::string_view materialName;
                const bool is2dArray = inDesc.type == SamplerType::SAMPLER_2D_ARRAY;
                switch (getFormatComponentCount(outDesc.format)) {
                    case 1: materialName  = is2dArray ?
                            "separableGaussianBlur1L"sv : "separableGaussianBlur1"sv;   break;
                    case 2: materialName  = is2dArray ?
                            "separableGaussianBlur2L"sv : "separableGaussianBlur2"sv;   break;
                    case 3: materialName  = is2dArray ?
                            "separableGaussianBlur3L"sv : "separableGaussianBlur3"sv;   break;
                    default: materialName = is2dArray ?
                            "separableGaussianBlur4L"sv : "separableGaussianBlur4"sv;   break;
                }
                /**
                 * 获取可分离高斯模糊材质
                 */
                auto const& separableGaussianBlur = getPostProcessMaterial(materialName);
                auto ma = separableGaussianBlur.getMaterial(mEngine);

                /**
                 * 获取材质中 kernel 参数的存储大小
                 */
                const size_t kernelStorageSize = ma->reflect("kernel")->size;
                /**
                 * 高斯核数组（最多 64 个系数）
                 */
                float2 kernel[64];
                /**
                 * 计算高斯系数，返回实际存储的系数数量
                 */
                size_t const m = computeGaussianCoefficients(kernel,
                        std::min(std::size(kernel), kernelStorageSize));
                /**
                 * 根据纹理类型选择源参数名称（2D 数组或普通 2D 纹理）
                 */
                std::string_view const sourceParameterName = is2dArray ? "sourceArray"sv : "source"sv;

                /**
                 * 设置材质实例的公共参数
                 * 
                 * 用于设置水平模糊和垂直模糊共用的材质参数。
                 * 
                 * @param mi 材质实例指针
                 */
                auto setCommonParams = [&](FMaterialInstance* const mi) {
                    // Initialize the samplers with dummy textures because vulkan requires a sampler to
                    // be bound to a texture even if sampler might be unused.
                    /**
                     * 初始化采样器（使用虚拟纹理），因为 Vulkan 要求即使采样器可能未使用也必须绑定纹理
                     */
                    mi->setParameter("sourceArray"sv, getZeroTextureArray(), SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST
                    });
                    mi->setParameter("source"sv, getZeroTexture(), SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST
                    });
                    /**
                     * 设置 Reinhard 色调映射标志
                     */
                    mi->setParameter("reinhard", reinhard ? uint32_t(1) : uint32_t(0));
                    /**
                     * 设置高斯核系数数量
                     */
                    mi->setParameter("count", int32_t(m));
                    /**
                     * 设置高斯核系数数组
                     */
                    mi->setParameter("kernel", kernel, m);
                };

                {
                    // horizontal pass
                    /**
                     * 水平模糊通道
                     * 
                     * 对输入纹理进行水平方向的高斯模糊，结果写入临时缓冲区
                     */
                    auto mi = getMaterialInstance(mEngine, separableGaussianBlur);
                    setCommonParams(mi);
                    /**
                     * 设置输入纹理（使用水平方向采样）
                     */
                    mi->setParameter(sourceParameterName, hwIn, SamplerParams{
                                .filterMag = SamplerMagFilter::LINEAR,
                                .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST,
                            });
                    /**
                     * 设置 Mipmap 级别
                     */
                    mi->setParameter("level", float(inSubDesc.level));
                    /**
                     * 设置纹理数组层索引
                     */
                    mi->setParameter("layer", float(inSubDesc.layer));
                    /**
                     * 设置模糊轴方向（水平方向：X 轴）
                     * X 分量 = 1 / 宽度，Y 分量 = 0
                     */
                    mi->setParameter("axis", float2{ 1.0f / inDesc.width, 0 });

                    // The framegraph only computes discard flags at FrameGraphPass boundaries
                    /**
                     * FrameGraph 只在通道边界计算丢弃标志，这里设置不丢弃临时缓冲区内容
                     */
                    hwTempRT.params.flags.discardEnd = TargetBufferFlags::NONE;

                    /**
                     * 提交材质参数并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, hwTempRT, mi);
                }

                {
                    // vertical pass
                    /**
                     * 垂直模糊通道
                     * 
                     * 对水平模糊的结果进行垂直方向的高斯模糊，得到最终的二维高斯模糊结果
                     */
                    auto mi = getMaterialInstance(mEngine, separableGaussianBlur);
                    setCommonParams(mi);
                    UTILS_UNUSED_IN_RELEASE auto width = outDesc.width;
                    UTILS_UNUSED_IN_RELEASE auto height = outDesc.height;
                    assert_invariant(width == hwOutRT.params.viewport.width);
                    assert_invariant(height == hwOutRT.params.viewport.height);

                    /**
                     * 设置输入纹理（临时缓冲区，使用垂直方向采样）
                     */
                    mi->setParameter(sourceParameterName, hwTemp, SamplerParams{
                                .filterMag = SamplerMagFilter::LINEAR,
                                .filterMin = SamplerMinFilter::LINEAR, /* level is always 0 */
                            });
                    /**
                     * 临时缓冲区始终是第 0 级
                     */
                    mi->setParameter("level", 0.0f);
                    mi->setParameter("layer", 0.0f);
                    /**
                     * 设置模糊轴方向（垂直方向：Y 轴）
                     * X 分量 = 0，Y 分量 = 1 / 高度
                     */
                    mi->setParameter("axis", float2{ 0, 1.0f / tempDesc.height });

                    /**
                     * 提交材质参数并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, hwOutRT, mi);
                }
                unbindAllDescriptorSets(driver);
            });

    return blurPass->out;
}

/**
 * 准备屏幕空间反射（SSR）的 Mipmap 配置
 * 
 * 为屏幕空间反射和折射效果准备多级 Mipmap 链的配置参数。
 * 该方法计算了将粗糙度映射到 LOD 级别的关键参数，以及高斯模糊核的参数。
 * 
 * @param fg 帧图引用
 * @param width 反射缓冲区宽度（像素）
 * @param height 反射缓冲区高度（像素）
 * @param format 纹理格式
 * @param verticalFieldOfView 垂直视场角（弧度）
 * @param scale 动态分辨率缩放因子（x, y）
 * @return ScreenSpaceRefConfig 配置结构，包含 SSR 纹理引用和计算参数
 * 
 * 处理流程：
 * 1. 确定高斯模糊核大小和标准差
 * 2. 计算单位距离下的像素大小
 * 3. 计算粗糙度到 LOD 的映射偏移量
 * 4. 计算 Mipmap 链的级别数量
 * 5. 处理非均匀动态分辨率的宽高比
 * 6. 创建 SSR 2D 数组纹理（包含折射和反射两个层）
 */
PostProcessManager::ScreenSpaceRefConfig PostProcessManager::prepareMipmapSSR(FrameGraph& fg,
        uint32_t const width, uint32_t const height, TextureFormat const format,
        float const verticalFieldOfView, float2 const scale) noexcept {

    // The kernel-size was determined empirically so that we don't get too many artifacts
    // due to the down-sampling with a box filter (which happens implicitly).
    // Requires only 6 stored coefficients and 11 tap/pass
    // e.g.: size of 13 (4 stored coefficients)
    //      +-------+-------+-------*===*-------+-------+-------+
    //  ... | 6 | 5 | 4 | 3 | 2 | 1 | 0 | 1 | 2 | 3 | 4 | 5 | 6 | ...
    //      +-------+-------+-------*===*-------+-------+-------+
    /**
     * 高斯模糊核大小
     * 
     * 通过经验确定，以避免由于隐式盒式滤波下采样造成的过多伪影。
     * 只需要存储 6 个系数，每个通道 11 次采样
     */
    constexpr size_t kernelSize = 21u;

    // The relation between the kernel size N and sigma (standard deviation) is 6*sigma - 1 = N
    // and is designed so the filter keeps its "gaussian-ness".
    // The standard deviation sigma0 is expressed in texels.
    /**
     * 高斯核标准差
     * 
     * 核大小 N 与标准差 sigma 的关系：6*sigma - 1 = N
     * 这个设计确保滤波器保持其高斯特性。
     * sigma0 以纹素为单位表示
     */
    constexpr float sigma0 = (kernelSize + 1u) / 6.0f;

    static_assert(kernelSize & 1u,
            "kernel size must be odd");

    static_assert((((kernelSize - 1u) / 2u) & 1u) == 0,
            "kernel positive side size must be even");

    // texel size of the reflection buffer in world units at 1 meter
    /**
     * 计算距离相机 1 米处反射缓冲区的像素大小（世界单位）
     * 
     * 用于将纹理空间的标准差转换为世界单位
     */
    constexpr float d = 1.0f; // 1m
    const float texelSizeAtOneMeter = d * std::tan(verticalFieldOfView) / float(height);

    /*
     * 1. Relation between standard deviation and LOD
     * ----------------------------------------------
     *
     * The standard deviation doubles at each level (i.e. variance quadruples), however,
     * the mip-chain is constructed by successively blurring each level, which causes the
     * variance of a given level to increase by the variance of the previous level (i.e. variances
     * add under convolution). This results in a scaling of 2.23 (instead of 2) of the standard
     * deviation for each level: sqrt( 1^2 + 2^2 ) = sqrt(5) = 2.23;
     *
     *  The standard deviation is scaled by 2.23 each time we go one mip down,
     *  and our mipmap chain is built such that lod 0 is not blurred and lod 1 is blurred with
     *  sigma0 * 2 (because of the smaller resolution of lod 1). To simplify things a bit, we
     *  replace this factor by 2.23 (i.e. we pretend that lod 0 is blurred by sigma0).
     *  We then get:
     *      sigma = sigma0 * 2.23^lod
     *      lod   = log2(sigma / sigma0) / log2(2.23)
     *
     *      +------------------------------------------------+
     *      |  lod = [ log2(sigma) - log2(sigma0) ] * 0.8614 |
     *      +------------------------------------------------+
     *
     * 2. Relation between standard deviation and roughness
     * ----------------------------------------------------
     *
     *  The spherical gaussian approximation of the GGX distribution is given by:
     *
     *           1         2(cos(theta)-1)
     *         ------ exp(  --------------- )
     *         pi*a^2           a^2
     *
     *
     *  Which is equivalent to:
     *
     *      sqrt(2)
     *      ------- Gaussian(2 * sqrt(1 - cos(theta)), a)
     *       pi*a
     *
     *  But when we filter a frame, we're actually calculating:
     *
     *      Gaussian(d * tan(theta), sigma)
     *
     *  With d the distance from the eye to the center sample, theta the angle, and it turns out
     *  that sqrt(2) * tan(theta) is very close to 2 * sqrt(1 - cos(theta)) for small angles, we
     *  can make that assumption because our filter is not wide.
     *  The above can be rewritten as:
     *
     *      Gaussian(d * tan(theta), a * d / sqrt(2))
     *    = Gaussian(    tan(theta), a     / sqrt(2))
     *
     *  Which now matches the SG approximation (we don't mind the scale factor because it's
     *  calculated automatically in the shader).
     *
     *  We finally get that:
     *
     *      +---------------------+
     *      | sigma = a / sqrt(2) |
     *      +---------------------+
     *
     *
     * 3. Taking the resolution into account
     * -------------------------------------
     *
     *  sigma0 above is expressed in texels, but we're interested in world units. The texel
     *  size in world unit is given by:
     *
     *      +--------------------------------+
     *      |  s = d * tan(fov) / resolution |      with d distance to camera plane
     *      +--------------------------------+
     *
     * 4. Roughness to lod mapping
     * ---------------------------
     *
     *  Putting it all together we get:
     *
     *      lod   = [ log2(sigma)       - log2(           sigma0 * s ) ] * 0.8614
     *      lod   = [ log2(a / sqrt(2)) - log2(           sigma0 * s ) ] * 0.8614
     *      lod   = [ log2(a)           - log2( sqrt(2) * sigma0 * s ) ] * 0.8614
     *
     *   +-------------------------------------------------------------------------------------+
     *   | lod   = [ log2(a / d) - log2(sqrt(2) * sigma0 * (tan(fov) / resolution)) ] * 0.8614 |
     *   +-------------------------------------------------------------------------------------+
     */
    /**
     * 1. 标准差与 LOD 的关系
     * ----------------------------------------------
     * 
     * 理论上，标准差在每个级别应该翻倍（即方差翻四倍），然而，
     * Mipmap 链是通过逐级模糊每个级别构建的，这导致给定级别的方差
     * 会增加前一级别的方差（即在卷积下方差相加）。这导致标准差的缩放因子
     * 为 2.23（而不是 2）：sqrt(1^2 + 2^2) = sqrt(5) = 2.23
     * 
     * 标准差每次下降一个 Mip 级别时缩放 2.23 倍，
     * 我们的 Mipmap 链构建方式为：lod 0 不模糊，lod 1 用 sigma0 * 2 模糊
     * （因为 lod 1 的分辨率更小）。为了简化，我们用 2.23 替换这个因子
     * （即我们假设 lod 0 用 sigma0 模糊）。
     * 然后我们得到：
     *     sigma = sigma0 * 2.23^lod
     *     lod   = log2(sigma / sigma0) / log2(2.23)
     * 
     *     +------------------------------------------------+
     *     |  lod = [ log2(sigma) - log2(sigma0) ] * 0.8614 |
     *     +------------------------------------------------+
     * 
     * 2. 标准差与粗糙度的关系
     * ----------------------------------------------------
     * 
     * GGX 分布的球面高斯近似由下式给出：
     * 
     *           1         2(cos(theta)-1)
     *         ------ exp(  --------------- )
     *         pi*a^2           a^2
     * 
     * 这等价于：
     * 
     *      sqrt(2)
     *      ------- Gaussian(2 * sqrt(1 - cos(theta)), a)
     *       pi*a
     * 
     * 但是当我们过滤一帧时，我们实际上计算的是：
     * 
     *      Gaussian(d * tan(theta), sigma)
     * 
     * 其中 d 是从眼睛到中心采样点的距离，theta 是角度。事实证明，
     * 对于小角度，sqrt(2) * tan(theta) 非常接近 2 * sqrt(1 - cos(theta))，
     * 我们可以做这个假设，因为我们的滤波器不宽。
     * 上面的可以重写为：
     * 
     *      Gaussian(d * tan(theta), a * d / sqrt(2))
     *    = Gaussian(    tan(theta), a     / sqrt(2))
     * 
     * 现在与 SG 近似匹配（我们不关心缩放因子，因为它会在着色器中自动计算）。
     * 
     * 我们最终得到：
     * 
     *      +---------------------+
     *      | sigma = a / sqrt(2) |
     *      +---------------------+
     * 
     * 3. 考虑分辨率
     * -------------------------------------
     * 
     * 上面的 sigma0 以纹素表示，但我们感兴趣的是世界单位。纹素
     * 在世界单位中的大小由下式给出：
     * 
     *      +--------------------------------+
     *      |  s = d * tan(fov) / resolution |      其中 d 是到相机平面的距离
     *      +--------------------------------+
     * 
     * 4. 粗糙度到 LOD 的映射
     * ---------------------------
     * 
     * 将所有内容放在一起，我们得到：
     * 
     *      lod   = [ log2(sigma)       - log2(           sigma0 * s ) ] * 0.8614
     *      lod   = [ log2(a / sqrt(2)) - log2(           sigma0 * s ) ] * 0.8614
     *      lod   = [ log2(a)           - log2( sqrt(2) * sigma0 * s ) ] * 0.8614
     * 
     *   +-------------------------------------------------------------------------------------+
     *   | lod   = [ log2(a / d) - log2(sqrt(2) * sigma0 * (tan(fov) / resolution)) ] * 0.8614 |
     *   +-------------------------------------------------------------------------------------+
     */

    /**
     * 计算折射 LOD 偏移量
     * 
     * 这是将粗糙度映射到 LOD 级别的关键参数。根据前面的数学推导，
     * LOD = [log2(a/d) - log2(sqrt(2) * sigma0 * (tan(fov) / resolution))] * 0.8614
     * 这里计算的是偏移项：log2(sqrt(2) * sigma0 * texelSizeAtOneMeter)
     */
    const float refractionLodOffset =
            -std::log2(f::SQRT2 * sigma0 * texelSizeAtOneMeter);

    // LOD count, we don't go lower than 16 texel in one dimension
    /**
     * 计算粗糙度 Mipmap 链的级别数量
     * 
     * 我们不希望任何一个维度的最低级别低于 16 纹素，以保证模糊质量
     */
    uint8_t roughnessLodCount = FTexture::maxLevelCount(width, height);
    roughnessLodCount = std::max(std::min(4, +roughnessLodCount), +roughnessLodCount - 4);

    // Make sure we keep the original buffer aspect ratio (this is because dynamic-resolution is
    // not necessarily homogenous).
    /**
     * 确保保持原始缓冲区的宽高比
     * 
     * 因为动态分辨率可能不是均匀的（X 和 Y 方向缩放不同），
     * 这会影响模糊效果。我们需要创建一个中间缓冲区，保持与原始相同的宽高比
     */
    uint32_t w = width;
    uint32_t h = height;
    if (scale.x != scale.y) {
        // dynamic resolution wasn't homogenous, which would affect the blur, so make sure to
        // keep an intermediary buffer that has the same aspect-ratio as the original.
        /**
         * 计算均匀缩放因子（几何平均值）
         */
        const float homogenousScale = std::sqrt(scale.x * scale.y);
        /**
         * 根据均匀缩放因子调整宽度和高度
         */
        w = uint32_t((homogenousScale / scale.x) * float(width));
        h = uint32_t((homogenousScale / scale.y) * float(height));
    }

    /**
     * 创建 SSR 纹理描述符
     * 
     * 使用 2D 数组纹理，深度为 2（一层用于折射，一层用于反射），
     * 包含多个 Mipmap 级别用于粗糙度映射
     */
    const FrameGraphTexture::Descriptor outDesc{
            .width = w, .height = h, .depth = 2,
            .levels = roughnessLodCount,
            .type = SamplerType::SAMPLER_2D_ARRAY,
            .format = format,
    };

    struct PrepareMipmapSSRPassData {
        FrameGraphId<FrameGraphTexture> ssr;
        FrameGraphId<FrameGraphTexture> refraction;
        FrameGraphId<FrameGraphTexture> reflection;
    };
    /**
     * 创建 SSR Mipmap 准备通道
     * 
     * 在 FrameGraph 中创建 SSR 2D 数组纹理，并为其创建子资源引用
     */
    auto const& pass = fg.addPass<PrepareMipmapSSRPassData>("Prepare MipmapSSR Pass",
            [&](FrameGraph::Builder& builder, auto& data){
                // create the SSR 2D array
                /**
                 * 创建 SSR 2D 数组纹理
                 */
                data.ssr = builder.createTexture("ssr", outDesc);
                // create the refraction subresource at layer 0
                /**
                 * 创建折射子资源（第 0 层）
                 */
                data.refraction = builder.createSubresource(data.ssr, "refraction", {.layer = 0 });
                // create the reflection subresource at layer 1
                /**
                 * 创建反射子资源（第 1 层）
                 */
                data.reflection = builder.createSubresource(data.ssr, "reflection", {.layer = 1 });
            });

    return {
            .ssr = pass->ssr,
            .refraction = pass->refraction,
            .reflection = pass->reflection,
            .lodOffset = refractionLodOffset,
            .roughnessLodCount = roughnessLodCount,
            .kernelSize = kernelSize,
            .sigma0 = sigma0
    };
}

/**
 * 生成屏幕空间反射（SSR）的 Mipmap 链
 * 
 * 将输入纹理（反射或折射缓冲区）复制到 SSR 纹理的第一个 Mipmap 级别，
 * 然后生成高斯模糊的 Mipmap 链。该链用于根据材质粗糙度进行预滤波。
 * 
 * @param ppm 后处理管理器引用
 * @param fg 帧图引用
 * @param input 输入纹理 ID（反射缓冲区或折射帧缓冲区）
 * @param output 输出纹理 ID（SSR 2D 数组的子资源，即一个层）
 * @param needInputDuplication 是否需要复制输入（当 SSR 缓冲区必须与颜色缓冲区分离时）
 * @param config SSR 配置参数（包含 Mipmap 级别数、核大小等）
 * @return 输出纹理 ID（与输入相同，但已包含完整的 Mipmap 链）
 * 
 * 处理流程：
 * 1. 获取输入和输出的纹理描述符
 * 2. 如果需要，解析多采样纹理并复制到输出（第一个 LOD）
 * 3. 使用 forwardResource 将输入重定向到输出
 * 4. 生成高斯模糊的 Mipmap 链
 * 
 * 注意：
 * - 在某些情况下无法使用 FrameGraph 的 forwardResource() 优化，
 *   因为 SSR 缓冲区必须与颜色缓冲区分离（例如折射时不能读写同一缓冲区）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::generateMipmapSSR(
        PostProcessManager& ppm, FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> input,
        FrameGraphId<FrameGraphTexture> output,
        bool const needInputDuplication, ScreenSpaceRefConfig const& config) noexcept {

    // Descriptor of our actual input image (e.g. reflection buffer or refraction framebuffer)
    /**
     * 获取实际输入图像的描述符（例如反射缓冲区或折射帧缓冲区）
     */
    auto const& desc = fg.getDescriptor(input);

    // Descriptor of the destination. `output` is a subresource (i.e. a layer of a 2D array)
    /**
     * 获取目标描述符。output 是一个子资源（即 2D 数组的一个层）
     */
    auto const& outDesc = fg.getDescriptor(output);

    /*
     * Resolve if needed + copy the image into first LOD
     */

    // needInputDuplication:
    // In some situations it's not possible to use the FrameGraph's forwardResource(),
    // as an optimization because the SSR buffer must be distinct from the color buffer
    // (input here), because we can't read and write into the same buffer (e.g. for refraction).

    /**
     * 如果需要输入复制或尺寸不匹配，则解析并复制到第一个 LOD
     * 
     * needInputDuplication：在某些情况下无法使用 FrameGraph 的 forwardResource() 优化，
     * 因为 SSR 缓冲区必须与颜色缓冲区分离（输入），因为我们不能读写同一缓冲区（例如折射）
     */
    if (needInputDuplication || outDesc.width != desc.width || outDesc.height != desc.height) {
        if (desc.samples > 1 &&
                outDesc.width == desc.width && outDesc.height == desc.height &&
                desc.format == outDesc.format) {
            // Resolve directly into the destination. This guarantees a blit/resolve will be
            // performed (i.e.: the source is copied) and we also guarantee that format/scaling
            // is the same after the forwardResource call below.
            /**
             * 多采样纹理且尺寸和格式匹配：直接解析到目标
             * 
             * 这保证了会执行 blit/resolve 操作（即源被复制），
             * 并且我们保证在下面的 forwardResource 调用后格式/缩放保持不变
             */
            input = ppm.resolve(fg, "ssr", input, outDesc);
        } else {
            // First resolve (if needed), may be a no-op. Guarantees that format/size is unchanged
            // by construction.
            /**
             * 首先解析多采样纹理（如果需要），可能是空操作。
             * 保证格式/尺寸不变
             */
            input = ppm.resolve(fg, "ssr", input, { .levels = 1 });
            // Then blit into an appropriate texture, this handles scaling and format conversion.
            // The input/output sizes may differ when non-homogenous DSR is enabled.
            /**
             * 然后 blit 到适当的纹理，处理缩放和格式转换
             * 
             * 当启用非均匀动态分辨率（DSR）时，输入/输出尺寸可能不同
             */
            input = ppm.blit(fg, false, input, { 0, 0, desc.width, desc.height }, outDesc,
                    SamplerMagFilter::LINEAR, SamplerMinFilter::LINEAR);
        }
    }

    // A lot of magic happens right here. This forward call replaces 'input' (which is either
    // the actual input we received when entering this function, or, a resolved version of it) by
    // our output. Effectively, forcing the methods *above* to render into our output.
    /**
     * FrameGraph 资源转发
     * 
     * 这个 forward 调用将 'input'（我们进入函数时接收的实际输入，或其解析版本）
     * 替换为我们的 'output'。实际上，强制上层方法渲染到我们的输出中。
     * 这样可以将输入数据直接复制到输出的第一个 Mipmap 级别。
     */
    output = fg.forwardResource(output, input);

    /*
     * Generate mipmap chain
     */

    /**
     * 生成高斯模糊的 Mipmap 链
     * 
     * 从第一个级别开始，为每个后续级别生成高斯模糊，
     * 用于根据材质粗糙度进行预滤波
     */
    return ppm.generateGaussianMipmap(fg, output, config.roughnessLodCount,
            true, config.kernelSize, config.sigma0);
}

/**
 * 景深（Depth of Field，DoF）后处理效果
 * 
 * 实现基于物理的景深效果，模拟真实相机的焦点模糊。
 * 该实现使用可分离的高斯模糊和基于圆形散焦（Circle of Confusion）的计算。
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID
 * @param depth 深度纹理 ID（用于计算散焦）
 * @param cameraInfo 相机信息（包含焦距、光圈、对焦距离等）
 * @param translucent 是否为半透明渲染（影响输出格式和变体）
 * @param bokehScale 散景缩放因子（X, Y）
 * @param dofOptions DoF 选项（分辨率、模糊参数等）
 * @return 应用景深效果后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 计算散焦（Circle of Confusion）参数
 * 2. 下采样颜色和深度缓冲区，生成散焦图
 * 3. 生成 Mipmap 链
 * 4. 生成散焦图块（tiles），用于优化模糊计算
 * 5. 执行景深模糊
 * 6. 应用中值滤波（可选）
 * 7. 与原始颜色混合
 * 
 * 注意：
 * - 支持全分辨率和四分之一分辨率两种模式
 * - 使用可分离的高斯模糊以优化性能
 * - 通过图块化减少不必要的模糊计算
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::dof(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphId<FrameGraphTexture> const depth,
        const CameraInfo& cameraInfo,
        bool const translucent,
        float2 bokehScale,
        const DepthOfFieldOptions& dofOptions) noexcept {

    assert_invariant(depth);

    /**
     * 根据是否为半透明选择后处理变体
     */
    PostProcessVariant const variant =
            translucent ? PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE;

    /**
     * 根据是否为半透明选择输出格式
     * - 半透明：RGBA16F（需要高精度 Alpha）
     * - 不透明：R11F_G11F_B10F（RGB 浮点压缩格式）
     */
    const TextureFormat format = translucent ? TextureFormat::RGBA16F
                                             : TextureFormat::R11F_G11F_B10F;

    // Rotate the bokeh based on the aperture diameter (i.e. angle of the blades)
    /**
     * 根据光圈直径（即光圈叶片的角度）旋转散景
     * 
     * 基础角度为 30 度（PI/6），然后根据当前光圈与最大光圈的比例进行调整
     */
    float bokehAngle = f::PI / 6.0f;
    if (dofOptions.maxApertureDiameter > 0.0f) {
        bokehAngle += f::PI_2 * saturate(cameraInfo.A / dofOptions.maxApertureDiameter);
    }

    /*
     * Circle-of-confusion
     * -------------------
     *
     * (see https://en.wikipedia.org/wiki/Circle_of_confusion)
     *
     * Ap: aperture [m]
     * f: focal length [m]
     * S: focus distance [m]
     * d: distance to the focal plane [m]
     *
     *            f      f     |      S  |
     * coc(d) =  --- . ----- . | 1 - --- |      in meters (m)
     *           Ap    S - f   |      d  |
     *
     *  This can be rewritten as:
     *
     *  coc(z) = Kc . Ks . (1 - S / d)          in pixels [px]
     *
     *                A.f
     *          Kc = -----          with: A = f / Ap
     *               S - f
     *
     *          Ks = height [px] / SensorSize [m]        pixel conversion
     *
     *
     *  We also introduce a "cocScale" factor for artistic reasons (see code below).
     *
     *
     *  Object distance computation (d)
     *  -------------------------------
     *
     *  1/d is computed from the depth buffer value as:
     *  (note: our Z clip space is 1 to 0 (inverted DirectX NDC))
     *
     *          screen-space -> clip-space -> view-space -> distance (*-1)
     *
     *   v_s = { x, y, z, 1 }                     // screen space (reversed-z)
     *   v_c = v_s                                // clip space (matches screen space)
     *   v   = inverse(projection) * v_c          // view space
     *   d   = -v.z / v.w                         // view space distance to camera
     *   1/d = -v.w / v.z
     *
     * Assuming a generic projection matrix of the form:
     *
     *    a 0 x 0
     *    0 b y 0
     *    0 0 A B
     *    0 0 C 0
     *
     * It comes that:
     *
     *           C          A
     *    1/d = --- . z  - ---
     *           B          B
     *
     * note: Here the result doesn't depend on {x, y}. This wouldn't be the case with a
     *       tilt-shift lens.
     *
     * Mathematica code:
     *      p = {{a, 0, b, 0}, {0, c, d, 0}, {0, 0, m22, m32}, {0, 0, m23, 0}};
     *      v = {x, y, z, 1};
     *      f = Inverse[p].v;
     *      Simplify[f[[4]]/f[[3]]]
     *
     * Plugging this back into the expression of: coc(z) = Kc . Ks . (1 - S / d)
     * We get that:  coc(z) = C0 * z + C1
     * With: C0 = - Kc * Ks * S * -C / B
     *       C1 =   Kc * Ks * (1 + S * A / B)
     *
     * It's just a madd!
     */
    /**
     * 散焦（Circle of Confusion，CoC）计算
     * -------------------
     * 
     * 散焦是景深效果的核心概念，表示一个点光源在焦点外成像时形成的模糊圆的大小。
     * （参见：https://en.wikipedia.org/wiki/Circle_of_confusion）
     * 
     * 符号说明：
     * - Ap: 光圈直径 [米]
     * - f: 焦距 [米]
     * - S: 对焦距离 [米]（焦点所在的距离）
     * - d: 物体到相机的距离 [米]（焦平面距离）
     * 
     * 基本公式（以米为单位）：
     * 
     *            f      f     |      S  |
     * coc(d) =  --- . ----- . | 1 - --- |      [米]
     *           Ap    S - f   |      d  |
     * 
     * 这可以重写为（以像素为单位）：
     * 
     *  coc(z) = Kc . Ks . (1 - S / d)          [像素]
     * 
     *                A.f
     *          Kc = -----          其中：A = f / Ap（F 值，光圈数）
     *               S - f
     * 
     *          Ks = height [px] / SensorSize [m]        像素转换常数
     * 
     * 我们还引入一个 "cocScale" 因子用于艺术调整（见下面的代码）。
     * 
     * 
     * 物体距离计算 (d)
     * -------------------------------
     * 
     * 从深度缓冲区值计算 1/d：
     * （注意：我们的 Z 裁剪空间是 1 到 0（反转的 DirectX NDC））
     * 
     *  变换流程：屏幕空间 -> 裁剪空间 -> 视图空间 -> 距离 (*-1)
     * 
     *   v_s = { x, y, z, 1 }                      // 屏幕空间（反转 Z）
     *   v_c = v_s                                 // 裁剪空间（与屏幕空间匹配）
     *   v   = inverse(projection) * v_c           // 视图空间
     *   d   = -v.z / v.w                          // 视图空间中到相机的距离
     *   1/d = -v.w / v.z
     * 
     * 假设投影矩阵的形式为：
     * 
     *    a 0 x 0
     *    0 b y 0
     *    0 0 A B
     *    0 0 C 0
     * 
     * 推导得到：
     * 
     *           C          A
     *    1/d = --- . z  - ---
     *           B          B
     * 
     * 注意：这里结果不依赖于 {x, y}。对于移轴镜头（tilt-shift lens）则不是这样。
     * 
     * Mathematica 验证代码：
     *      p = {{a, 0, b, 0}, {0, c, d, 0}, {0, 0, m22, m32}, {0, 0, m23, 0}};
     *      v = {x, y, z, 1};
     *      f = Inverse[p].v;
     *      Simplify[f[[4]]/f[[3]]]
     * 
     * 将上述结果代入表达式：coc(z) = Kc . Ks . (1 - S / d)
     * 我们得到：coc(z) = C0 * z + C1
     * 其中：C0 = - Kc * Ks * S * -C / B
     *       C1 =   Kc * Ks * (1 + S * A / B)
     * 
     * 这只是一个乘加运算（madd）！
     */
    /**
     * 计算散焦（Circle of Confusion，CoC）参数
     * 
     * 基于前面注释中的数学推导，计算将深度值转换为散焦大小的参数
     */
    const float focusDistance = cameraInfo.d;
    auto const& desc = fg.getDescriptor<FrameGraphTexture>(input);
    /**
     * Kc：光学常数（基于焦距、光圈和对焦距离）
     */
    const float Kc = (cameraInfo.A * cameraInfo.f) / (focusDistance - cameraInfo.f);
    /**
     * Ks：像素转换常数（将世界单位转换为像素）
     */
    const float Ks = float(desc.height) / FCamera::SENSOR_SIZE;
    /**
     * K：组合常数（包含艺术调整的散焦缩放因子）
     */
    const float K  = dofOptions.cocScale * Ks * Kc;

    /**
     * 从投影矩阵计算散焦参数（C0 和 C1）
     * 
     * 根据推导：coc(z) = C0 * z + C1
     * 其中 C0 和 C1 是从投影矩阵提取的常数
     */
    auto const& p = cameraInfo.projection;
    const float2 cocParams = {
              K * focusDistance * p[2][3] / p[3][2],
              K * (1.0 + focusDistance * p[2][2] / p[3][2])
    };

    /*
     * dofResolution is used to chose between full- or quarter-resolution
     * for the DoF calculations. Set to [1] for full resolution or [2] for quarter-resolution.
     */
    /**
     * 确定 DoF 计算的分辨率
     * 
     * 1 = 全分辨率，2 = 四分之一分辨率（性能优化）
     */
    const uint32_t dofResolution = dofOptions.nativeResolution ? 1u : 2u;

    /**
     * 获取输入纹理描述符并计算 DoF 处理分辨率
     */
    auto const& colorDesc = fg.getDescriptor(input);
    const uint32_t width  = colorDesc.width  / dofResolution;
    const uint32_t height = colorDesc.height / dofResolution;

    // at full resolution, 4 "safe" levels are guaranteed
    /**
     * 最大 Mipmap 级别数
     * 
     * 在全分辨率下，保证有 4 个"安全"级别
     */
    constexpr uint32_t maxMipLevels = 4u;

    // compute numbers of "safe" levels (should be 4, but can be 3 at half res)
    /**
     * 计算"安全"的 Mipmap 级别数量
     * 
     * 应该是 4，但在半分辨率下可能是 3
     * 使用 ctz（计算尾随零）来确保尺寸是 2 的幂
     */
    const uint8_t mipmapCount = std::min(maxMipLevels, ctz(width | height));
    assert_invariant(mipmapCount == maxMipLevels || mipmapCount == maxMipLevels-1);

    /*
     * Setup:
     *      - Downsample of color buffer
     *      - Separate near & far field
     *      - Generate Circle Of Confusion buffer
     */

    struct PostProcessDofDownsample {
        FrameGraphId<FrameGraphTexture> color;
        FrameGraphId<FrameGraphTexture> depth;
        FrameGraphId<FrameGraphTexture> outColor;
        FrameGraphId<FrameGraphTexture> outCoc;
    };

    /**
     * DoF 下采样通道
     * 
     * 对颜色和深度缓冲区进行下采样，同时计算散焦（Circle of Confusion）图
     */
    auto const& ppDoFDownsample = fg.addPass<PostProcessDofDownsample>("DoF Downsample",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（颜色和深度）
                 */
                data.color = builder.sample(input);
                data.depth = builder.sample(depth);

                /**
                 * 创建输出颜色纹理（包含 Mipmap 链）
                 */
                data.outColor = builder.createTexture("dof downsample output", {
                        .width  = width, .height = height, .levels = mipmapCount, .format = format
                });
                /**
                 * 创建散焦图纹理
                 * 
                 * 使用 R16F 格式存储散焦值，swizzle 设置为
                 * 下一阶段期望红色和绿色通道包含最小/最大散焦值
                 */
                data.outCoc = builder.createTexture("dof CoC output", {
                        .width  = width, .height = height, .levels = mipmapCount,
                        .format = TextureFormat::R16F,
                        .swizzle = {
                                // the next stage expects min/max CoC in the red/green channel
                                .r = TextureSwizzle::CHANNEL_0,
                                .g = TextureSwizzle::CHANNEL_0 },
                });
                /**
                 * 将输出声明为颜色附件
                 */
                data.outColor = builder.write(data.outColor, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.outCoc   = builder.write(data.outCoc,   FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("DoF Target", { .attachments = {
                                .color = { data.outColor, data.outCoc }
                        }
                });
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息和纹理资源
                 */
                auto const& out = resources.getRenderPassInfo();
                auto color = resources.getTexture(data.color);
                auto depth = resources.getTexture(data.depth);
                /**
                 * 根据分辨率选择材质（全分辨率使用 dofCoc，四分之一分辨率使用 dofDownsample）
                 */
                auto const& material = (dofResolution == 1) ?
                        getPostProcessMaterial("dofCoc") :
                        getPostProcessMaterial("dofDownsample");
                FMaterialInstance* const mi = getMaterialInstance(mEngine, material);

                /**
                 * 设置材质参数
                 */
                mi->setParameter("color", color, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                mi->setParameter("depth", depth, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                /**
                 * 设置散焦计算参数
                 */
                mi->setParameter("cocParams", cocParams);
                /**
                 * 设置散焦值钳制范围（前景和背景的最大散焦值）
                 */
                mi->setParameter("cocClamp", float2{
                    -(dofOptions.maxForegroundCOC ? dofOptions.maxForegroundCOC : DOF_DEFAULT_MAX_COC),
                      dofOptions.maxBackgroundCOC ? dofOptions.maxBackgroundCOC : DOF_DEFAULT_MAX_COC});
                /**
                 * 设置像素大小（用于散焦计算）
                 */
                mi->setParameter("texelSize", float2{
                        1.0f / float(colorDesc.width),
                        1.0f / float(colorDesc.height) });
                /**
                 * 提交材质参数并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    /*
     * Setup (Continued)
     *      - Generate mipmaps
     */

    /**
     * DoF Mipmap 生成通道
     * 
     * 为颜色和散焦图生成 Mipmap 链，用于多级模糊计算
     */
    struct PostProcessDofMipmap {
        FrameGraphId<FrameGraphTexture> inOutColor;
        FrameGraphId<FrameGraphTexture> inOutCoc;
        uint32_t rp[maxMipLevels];
    };

    assert_invariant(mipmapCount - 1 <= sizeof(PostProcessDofMipmap::rp) / sizeof(uint32_t));

    auto const& ppDoFMipmap = fg.addPass<PostProcessDofMipmap>("DoF Mipmap",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（从下采样通道输出）
                 */
                data.inOutColor = builder.sample(ppDoFDownsample->outColor);
                data.inOutCoc   = builder.sample(ppDoFDownsample->outCoc);
                /**
                 * 为每个 Mipmap 级别创建子资源和渲染通道
                 */
                for (size_t i = 0; i < mipmapCount - 1u; i++) {
                    // make sure inputs are always multiple of two (should be true by construction)
                    // (this is so that we can compute clean mip levels)
                    /**
                     * 确保输入尺寸始终是 2 的倍数（构造时应该为真）
                     * 这样才能正确计算 Mipmap 级别
                     */
                    assert_invariant((FTexture::valueForLevel(uint8_t(i), fg.getDescriptor(data.inOutColor).width ) & 0x1u) == 0);
                    assert_invariant((FTexture::valueForLevel(uint8_t(i), fg.getDescriptor(data.inOutColor).height) & 0x1u) == 0);

                    /**
                     * 创建下一级 Mipmap 的子资源
                     */
                    auto inOutColor = builder.createSubresource(data.inOutColor, "Color mip", { .level = uint8_t(i + 1) });
                    auto inOutCoc   = builder.createSubresource(data.inOutCoc, "Coc mip", { .level = uint8_t(i + 1) });

                    /**
                     * 声明为颜色附件
                     */
                    inOutColor = builder.write(inOutColor, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    inOutCoc   = builder.write(inOutCoc,   FrameGraphTexture::Usage::COLOR_ATTACHMENT);

                    /**
                     * 声明渲染通道
                     */
                    data.rp[i] = builder.declareRenderPass("DoF Target", { .attachments = {
                                .color = { inOutColor, inOutCoc  }
                        }
                    });
                }
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理描述符和硬件纹理句柄
                 */
                auto desc       = resources.getDescriptor(data.inOutColor);
                auto inOutColor = resources.getTexture(data.inOutColor);
                auto inOutCoc   = resources.getTexture(data.inOutCoc);

                /**
                 * 获取 Mipmap 生成材质和管道状态
                 */
                auto const& material = getPostProcessMaterial("dofMipmap");
                FMaterial const* const ma = material.getMaterial(mEngine);

                auto const pipeline = getPipelineState(ma, variant);

                /**
                 * 为每个 Mipmap 级别生成模糊
                 */
                for (size_t level = 0 ; level < mipmapCount - 1u ; level++) {
                    /**
                     * 计算当前级别的尺寸
                     */
                    const float w = FTexture::valueForLevel(level, desc.width);
                    const float h = FTexture::valueForLevel(level, desc.height);
                    /**
                     * 获取渲染通道信息
                     */
                    auto const& out = resources.getRenderPassInfo(data.rp[level]);
                    /**
                     * 创建当前级别的纹理视图
                     */
                    auto inColor = driver.createTextureView(inOutColor, level, 1);
                    auto inCoc = driver.createTextureView(inOutCoc, level, 1);
                    FMaterialInstance* const mi = getMaterialInstance(ma);

                    /**
                     * 设置材质参数
                     */
                    mi->setParameter("color", inColor, SamplerParams{
                            .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                    mi->setParameter("coc",   inCoc,   SamplerParams{
                            .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                    /**
                     * 设置权重缩放（随级别递减）
                     */
                    mi->setParameter("weightScale", 0.5f / float(1u << level));   // FIXME: halfres?
                    /**
                     * 设置像素大小
                     */
                    mi->setParameter("texelSize", float2{ 1.0f / w, 1.0f / h });
                    /**
                     * 提交材质参数并使用
                     */
                    mi->commit(driver, getUboManager());
                    mi->use(driver);

                    /**
                     * 渲染全屏四边形生成 Mipmap
                     */
                    renderFullScreenQuad(out, pipeline, driver);
                    /**
                     * 解绑材质描述符堆
                     */
                    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);

                    /**
                     * 销毁临时纹理视图
                     */
                    driver.destroyTexture(inColor);
                    driver.destroyTexture(inCoc);
                }
                unbindAllDescriptorSets(driver);
            });

    /*
     * Setup (Continued)
     *      - Generate min/max tiles for far/near fields (continued)
     */

    /**
     * DoF 图块化处理
     * 
     * 将散焦图分成图块（tiles），每个图块存储最小和最大散焦值。
     * 这用于优化模糊计算，只对需要模糊的区域进行处理。
     */
    auto inTilesCocMinMax = ppDoFDownsample->outCoc;

    // TODO: Should the tile size be in real pixels? i.e. always 16px instead of being dependant on
    //       the DoF effect resolution?
    // Size of a tile in full-resolution pixels -- must match TILE_SIZE in dofDilate.mat
    /**
     * 图块大小（全分辨率像素）
     * 
     * 必须与 dofDilate.mat 中的 TILE_SIZE 匹配
     */
    constexpr size_t tileSize = 16;

    // we assume the width/height is already multiple of 16
    /**
     * 假设宽度/高度已经是 16 的倍数
     */
    assert_invariant(!(colorDesc.width  & 0xF) && !(colorDesc.height & 0xF));
    /**
     * 图块缓冲区尺寸（与 DoF 处理分辨率相同）
     */
    const uint32_t tileBufferWidth  = width;
    const uint32_t tileBufferHeight = height;
    /**
     * 图块归约次数（计算需要多少级图块归约）
     */
    const size_t tileReductionCount = ctz(tileSize / dofResolution);

    struct PostProcessDofTiling1 {
        FrameGraphId<FrameGraphTexture> inCocMinMax;
        FrameGraphId<FrameGraphTexture> outTilesCocMinMax;
    };

    /**
     * 检查纹理 swizzle 是否受支持
     */
    const bool textureSwizzleSupported = Texture::isTextureSwizzleSupported(mEngine);
    /**
     * 执行多级图块归约，生成层次化的图块结构
     */
    for (size_t i = 0; i < tileReductionCount; i++) {
        auto const& ppDoFTiling = fg.addPass<PostProcessDofTiling1>("DoF Tiling",
                [&](FrameGraph::Builder& builder, auto& data) {

                    // this must be true by construction
                    /**
                     * 通过构造保证尺寸是 2 的倍数
                     */
                    assert_invariant(((tileBufferWidth  >> i) & 1u) == 0);
                    assert_invariant(((tileBufferHeight >> i) & 1u) == 0);

                    /**
                     * 声明输入图块散焦图
                     */
                    data.inCocMinMax = builder.sample(inTilesCocMinMax);
                    /**
                     * 创建输出图块（尺寸减半）
                     * 
                     * 使用 RG16F 格式存储每个图块的最小和最大散焦值
                     */
                    data.outTilesCocMinMax = builder.createTexture("dof tiles output", {
                            .width  = tileBufferWidth  >> (i + 1u),
                            .height = tileBufferHeight >> (i + 1u),
                            .format = TextureFormat::RG16F
                    });
                    data.outTilesCocMinMax = builder.declareRenderPass(data.outTilesCocMinMax);
                },
                [=, this](FrameGraphResources const& resources,
                        auto const& data, DriverApi& driver) {
                    /**
                     * 绑定描述符堆
                     */
                    bindPostProcessDescriptorSet(driver);
                    bindPerRenderableDescriptorSet(driver);
                    /**
                     * 获取输入描述符和纹理资源
                     */
                    auto const& inputDesc = resources.getDescriptor(data.inCocMinMax);
                    auto const& out = resources.getRenderPassInfo();
                    auto inCocMinMax = resources.getTexture(data.inCocMinMax);
                    /**
                     * 选择材质（如果不支持 swizzle 且是第一级，使用特殊材质）
                     */
                    auto const& material = (!textureSwizzleSupported && (i == 0)) ?
                            getPostProcessMaterial("dofTilesSwizzle") :
                            getPostProcessMaterial("dofTiles");
                    FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                    /**
                     * 设置材质参数
                     */
                    mi->setParameter("cocMinMax", inCocMinMax, SamplerParams{
                            .filterMin = SamplerMinFilter::NEAREST });
                    mi->setParameter("texelSize", float2{ 1.0f / inputDesc.width, 1.0f / inputDesc.height });
                    /**
                     * 提交并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, out, mi);
                    unbindAllDescriptorSets(driver);
                });
        /**
         * 更新输入为当前输出，用于下一级归约
         */
        inTilesCocMinMax = ppDoFTiling->outTilesCocMinMax;
    }

    /*
     * Dilate tiles
     */

    // This is a small helper that does one round of dilate
    /**
     * 图块膨胀（Dilate）辅助函数
     * 
     * 对图块进行膨胀操作，扩展每个图块的散焦范围。
     * 这确保模糊计算能够覆盖到图块边界外的区域。
     * 
     * @param input 输入图块纹理 ID
     * @return 膨胀后的图块纹理 ID
     */
    auto dilate = [&](FrameGraphId<FrameGraphTexture> const input) -> FrameGraphId<FrameGraphTexture> {

        struct PostProcessDofDilate {
            FrameGraphId<FrameGraphTexture> inTilesCocMinMax;
            FrameGraphId<FrameGraphTexture> outTilesCocMinMax;
        };

        auto const& ppDoFDilate = fg.addPass<PostProcessDofDilate>("DoF Dilate",
                [&](FrameGraph::Builder& builder, auto& data) {
                    /**
                     * 获取输入描述符并声明输入资源
                     */
                    auto const& inputDesc = fg.getDescriptor(input);
                    data.inTilesCocMinMax = builder.sample(input);
                    /**
                     * 创建输出纹理（与输入相同尺寸）
                     */
                    data.outTilesCocMinMax = builder.createTexture("dof dilated tiles output", inputDesc);
                    data.outTilesCocMinMax = builder.declareRenderPass(data.outTilesCocMinMax );
                },
                [=, this](FrameGraphResources const& resources,
                        auto const& data, DriverApi& driver) {
                    /**
                     * 绑定描述符堆
                     */
                    bindPostProcessDescriptorSet(driver);
                    bindPerRenderableDescriptorSet(driver);

                    /**
                     * 获取渲染通道信息和纹理资源
                     */
                    auto const& out = resources.getRenderPassInfo();
                    auto inTilesCocMinMax = resources.getTexture(data.inTilesCocMinMax);
                    /**
                     * 获取膨胀材质并设置参数
                     */
                    auto const& material = getPostProcessMaterial("dofDilate");
                    FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                    mi->setParameter("tiles", inTilesCocMinMax, SamplerParams{
                            .filterMin = SamplerMinFilter::NEAREST });
                    /**
                     * 提交并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, out, mi);
                    unbindAllDescriptorSets(driver);
                });
        return ppDoFDilate->outTilesCocMinMax;
    };

    // Tiles of 16 full-resolution pixels requires two dilate rounds to accommodate our max Coc of 32 pixels
    // (note: when running at half-res, the tiles are 8 half-resolution pixels, and still need two
    //  dilate rounds to accommodate the mac CoC pf 16 half-resolution pixels)
    /**
     * 执行两次膨胀操作
     * 
     * 16 全分辨率像素的图块需要两次膨胀来容纳最大 32 像素的散焦
     * （注意：在半分辨率下，图块是 8 半分辨率像素，仍然需要两次膨胀
     *  来容纳最大 16 半分辨率像素的散焦）
     */
    auto dilated = dilate(inTilesCocMinMax);
    dilated = dilate(dilated);

    /*
     * DoF blur pass
     */

    /**
     * DoF 模糊通道
     * 
     * 执行实际的景深模糊计算。根据散焦值和图块信息，
     * 对颜色纹理进行自适应模糊，生成模糊后的颜色和 Alpha 通道。
     */
    struct PostProcessDof {
        FrameGraphId<FrameGraphTexture> color;
        FrameGraphId<FrameGraphTexture> coc;
        FrameGraphId<FrameGraphTexture> tilesCocMinMax;
        FrameGraphId<FrameGraphTexture> outColor;
        FrameGraphId<FrameGraphTexture> outAlpha;
    };

    auto const& ppDoF = fg.addPass<PostProcessDof>("DoF",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（颜色、散焦图、膨胀后的图块）
                 */
                data.color          = builder.sample(ppDoFMipmap->inOutColor);
                data.coc            = builder.sample(ppDoFMipmap->inOutCoc);
                data.tilesCocMinMax = builder.sample(dilated);

                /**
                 * 创建输出颜色纹理
                 */
                data.outColor = builder.createTexture("dof color output", {
                        .width  = colorDesc.width  / dofResolution,
                        .height = colorDesc.height / dofResolution,
                        .format = fg.getDescriptor(data.color).format
                });
                /**
                 * 创建输出 Alpha 纹理（R8 格式，用于混合）
                 */
                data.outAlpha = builder.createTexture("dof alpha output", {
                        .width  = colorDesc.width  / dofResolution,
                        .height = colorDesc.height / dofResolution,
                        .format = TextureFormat::R8
                });
                /**
                 * 声明为颜色附件
                 */
                data.outColor  = builder.write(data.outColor, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.outAlpha  = builder.write(data.outAlpha, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("DoF Target", {
                        .attachments = { .color = { data.outColor, data.outAlpha }}
                });
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息和纹理资源
                 */
                auto const& out = resources.getRenderPassInfo();

                auto color          = resources.getTexture(data.color);
                auto coc            = resources.getTexture(data.coc);
                auto tilesCocMinMax = resources.getTexture(data.tilesCocMinMax);

                auto const& inputDesc = resources.getDescriptor(data.coc);

                /**
                 * 获取 DoF 材质并设置参数
                 */
                auto const& material = getPostProcessMaterial("dof");
                FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                // it's not safe to use bilinear filtering in the general case (causes artifacts around edges)
                /**
                 * 设置颜色采样器（最近邻，避免边缘伪影）
                 */
                mi->setParameter("color", color, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                /**
                 * 设置线性颜色采样器（用于特定区域）
                 */
                mi->setParameter("colorLinear", color, SamplerParams{
                        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST });
                /**
                 * 设置散焦图采样器
                 */
                mi->setParameter("coc", coc, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                /**
                 * 设置图块采样器
                 */
                mi->setParameter("tiles", tilesCocMinMax, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                /**
                 * 设置散焦到纹素的缩放因子（用于散景缩放）
                 */
                mi->setParameter("cocToTexelScale", float2{
                        bokehScale.x / (inputDesc.width * dofResolution),
                        bokehScale.y / (inputDesc.height * dofResolution)
                });
                /**
                 * 设置散焦到像素的缩放因子
                 */
                mi->setParameter("cocToPixelScale", (1.0f / float(dofResolution)));
                /**
                 * 设置模糊环数量（前景、背景、快速收集）
                 */
                mi->setParameter("ringCounts", float4{
                    dofOptions.foregroundRingCount ? dofOptions.foregroundRingCount : DOF_DEFAULT_RING_COUNT,
                    dofOptions.backgroundRingCount ? dofOptions.backgroundRingCount : DOF_DEFAULT_RING_COUNT,
                    dofOptions.fastGatherRingCount ? dofOptions.fastGatherRingCount : DOF_DEFAULT_RING_COUNT,
                    0.0 // unused for now
                });
                /**
                 * 设置散景旋转角度
                 */
                mi->setParameter("bokehAngle",  bokehAngle);
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    /*
     * DoF median
     */

    /**
     * DoF 中值滤波通道（可选）
     * 
     * 对模糊后的结果应用中值滤波，减少噪点和伪影。
     * 这是一个可选的后处理步骤，根据选项决定是否启用。
     */
    struct PostProcessDofMedian {
        FrameGraphId<FrameGraphTexture> inColor;
        FrameGraphId<FrameGraphTexture> inAlpha;
        FrameGraphId<FrameGraphTexture> tilesCocMinMax;
        FrameGraphId<FrameGraphTexture> outColor;
        FrameGraphId<FrameGraphTexture> outAlpha;
    };

    auto const& ppDoFMedian = fg.addPass<PostProcessDofMedian>("DoF Median",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（从 DoF 模糊通道输出）
                 */
                data.inColor        = builder.sample(ppDoF->outColor);
                data.inAlpha        = builder.sample(ppDoF->outAlpha);
                data.tilesCocMinMax = builder.sample(dilated);

                /**
                 * 创建输出纹理（与输入相同尺寸和格式）
                 */
                data.outColor = builder.createTexture("dof color output", fg.getDescriptor(data.inColor));
                data.outAlpha = builder.createTexture("dof alpha output", fg.getDescriptor(data.inAlpha));
                data.outColor = builder.write(data.outColor, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                data.outAlpha = builder.write(data.outAlpha, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("DoF Target", {
                        .attachments = { .color = { data.outColor, data.outAlpha }}
                });
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息和纹理资源
                 */
                auto const& out = resources.getRenderPassInfo();

                auto inColor        = resources.getTexture(data.inColor);
                auto inAlpha        = resources.getTexture(data.inAlpha);
                auto tilesCocMinMax = resources.getTexture(data.tilesCocMinMax);

                /**
                 * 获取中值滤波材质并设置参数
                 */
                auto const& material = getPostProcessMaterial("dofMedian");
                FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                /**
                 * 设置 DoF 颜色采样器
                 */
                mi->setParameter("dof",   inColor,        SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                /**
                 * 设置 Alpha 采样器
                 */
                mi->setParameter("alpha", inAlpha,        SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                /**
                 * 设置图块采样器
                 */
                mi->setParameter("tiles", tilesCocMinMax, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });


    /*
     * DoF recombine
     */

    /**
     * 根据选项选择使用中值滤波后的结果还是直接使用模糊结果
     */
    auto outColor = ppDoFMedian->outColor;
    auto outAlpha = ppDoFMedian->outAlpha;
    if (dofOptions.filter == DepthOfFieldOptions::Filter::NONE) {
        /**
         * 如果禁用中值滤波，使用模糊通道的直接输出
         */
        outColor = ppDoF->outColor;
        outAlpha = ppDoF->outAlpha;
    }

    /**
     * DoF 组合通道
     * 
     * 将原始颜色与模糊后的 DoF 结果混合，根据 Alpha 通道和散焦图块
     * 进行智能混合，得到最终的景深效果。
     */
    struct PostProcessDofCombine {
        FrameGraphId<FrameGraphTexture> color;
        FrameGraphId<FrameGraphTexture> dof;
        FrameGraphId<FrameGraphTexture> alpha;
        FrameGraphId<FrameGraphTexture> tilesCocMinMax;
        FrameGraphId<FrameGraphTexture> output;
    };

    auto const& ppDoFCombine = fg.addPass<PostProcessDofCombine>("DoF combine",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（原始颜色、DoF 结果、Alpha、图块）
                 */
                data.color      = builder.sample(input);
                data.dof        = builder.sample(outColor);
                data.alpha      = builder.sample(outAlpha);
                data.tilesCocMinMax = builder.sample(dilated);
                /**
                 * 创建输出纹理（与原始输入相同尺寸和格式）
                 */
                auto const& inputDesc = fg.getDescriptor(data.color);
                data.output = builder.createTexture("DoF output", inputDesc);
                data.output = builder.declareRenderPass(data.output);
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息和纹理资源
                 */
                auto const& out = resources.getRenderPassInfo();

                auto color      = resources.getTexture(data.color);
                auto dof        = resources.getTexture(data.dof);
                auto alpha      = resources.getTexture(data.alpha);
                auto tilesCocMinMax = resources.getTexture(data.tilesCocMinMax);

                /**
                 * 获取组合材质并设置参数
                 */
                auto const& material = getPostProcessMaterial("dofCombine");
                FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                /**
                 * 设置原始颜色采样器
                 */
                mi->setParameter("color", color, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                /**
                 * 设置 DoF 结果采样器
                 */
                mi->setParameter("dof",   dof,   SamplerParams{
                        .filterMag = SamplerMagFilter::NEAREST });
                /**
                 * 设置 Alpha 采样器
                 */
                mi->setParameter("alpha", alpha, SamplerParams{
                        .filterMag = SamplerMagFilter::NEAREST });
                /**
                 * 设置图块采样器
                 */
                mi->setParameter("tiles", tilesCocMinMax, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST });
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    return ppDoFCombine->output;
}

/**
 * 下采样通道
 * 
 * 对输入纹理进行 2x 下采样，用于泛光（Bloom）效果的前处理。
 * 支持阈值处理、高光抑制和萤火虫（fireflies）减少。
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID
 * @param outDesc 输出纹理描述符（指定目标尺寸和格式）
 * @param threshold 是否启用阈值处理（提取高亮区域）
 * @param highlight 高光抑制参数（用于色调映射）
 * @param fireflies 是否启用萤火虫减少（减少高亮噪点）
 * @return 下采样后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理（按描述符指定的尺寸）
 * 2. 使用 bloomDownsample2x 材质进行 2x 下采样
 * 3. 应用阈值、萤火虫减少和高光抑制（如果启用）
 * 4. 渲染全屏四边形
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::downscalePass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphTexture::Descriptor const& outDesc,
        bool const threshold, float const highlight, bool const fireflies) noexcept {
    struct DownsampleData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };
    auto const& downsamplePass = fg.addPass<DownsampleData>("Downsample",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理（按描述符指定的尺寸）
                 */
                data.output = builder.createTexture("Downsample-output", outDesc);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(data.output);
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取渲染通道信息
                 */
                auto const& out = resources.getRenderPassInfo();
                /**
                 * 获取 2x 下采样材质
                 */
                auto const& material = getPostProcessMaterial("bloomDownsample2x");
                FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                /**
                 * 设置源纹理采样器（线性过滤）
                 */
                mi->setParameter("source", resources.getTexture(data.input), SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR
                });
                /**
                 * 设置 Mipmap 级别（0 = 基础级别）
                 */
                mi->setParameter("level", 0);
                /**
                 * 设置阈值标志（用于提取高亮区域）
                 */
                mi->setParameter("threshold", threshold ? 1.0f : 0.0f);
                /**
                 * 设置萤火虫减少标志（减少高亮噪点）
                 */
                mi->setParameter("fireflies", fireflies ? 1.0f : 0.0f);
                /**
                 * 设置高光抑制参数（如果 highlight 为无穷大，则设为 0）
                 */
                mi->setParameter("invHighlight", std::isinf(highlight) ? 0.0f : 1.0f / highlight);
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });
    return downsamplePass->output;
}

/**
 * 泛光（Bloom）后处理效果
 * 
 * 实现泛光效果，模拟明亮光源产生的光晕。该实现包括：
 * 1. 下采样阶段：提取高亮区域并生成多级 Mipmap
 * 2. 光晕（Flare）阶段：生成镜头光晕效果（可选）
 * 3. 上采样阶段：将模糊后的高亮区域混合回原始图像
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID
 * @param outFormat 输出纹理格式
 * @param inoutBloomOptions 泛光选项（输入/输出，可能被修改以调整级别数）
 * @param taaOptions 时间抗锯齿选项（用于决定是否需要萤火虫减少）
 * @param scale 动态分辨率缩放因子（X, Y）
 * @return BloomPassOutput 包含泛光纹理和光晕纹理的结构
 * 
 * 处理流程：
 * 1. 计算泛光缓冲区尺寸（固定尺寸，不受动态分辨率影响）
 * 2. 根据质量级别调整缓冲区和级别数
 * 3. 执行下采样（提取高亮区域）
 * 4. 生成泛光 Mipmap 链
 * 5. 执行光晕通道（可选）
 * 6. 执行上采样（混合回原始图像）
 * 
 * 注意：
 * - 使用固定缓冲区尺寸确保泛光效果不受动态分辨率影响
 * - 支持多种质量级别（LOW, MEDIUM, HIGH, ULTRA）
 * - 在低质量模式下使用 9 采样滤波器，高质量模式下使用 13 采样滤波器
 */
PostProcessManager::BloomPassOutput PostProcessManager::bloom(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> input, TextureFormat const outFormat,
        BloomOptions& inoutBloomOptions,
        TemporalAntiAliasingOptions const& taaOptions,
        float2 const scale) noexcept {

    // Figure out a good size for the bloom buffer. We must use a fixed bloom buffer size so
    // that the size/strength of the bloom doesn't vary much with the resolution, otherwise
    // dynamic resolution would affect the bloom effect too much.
    /**
     * 计算泛光缓冲区的合适尺寸
     * 
     * 必须使用固定的泛光缓冲区尺寸，这样泛光的大小/强度不会随分辨率变化太大，
     * 否则动态分辨率会过度影响泛光效果。
     */
    auto desc = fg.getDescriptor(input);

    // width and height after dynamic resolution upscaling
    /**
     * 计算动态分辨率上采样后的宽高比
     */
    const float aspect = (float(desc.width) * scale.y) / (float(desc.height) * scale.x);

    // FIXME: don't allow inoutBloomOptions.resolution to be larger than input's resolution
    //        (avoid upscale) but how does this affect dynamic resolution
    // FIXME: check what happens on WebGL and intel's processors

    // compute the desired bloom buffer size
    /**
     * 计算期望的泛光缓冲区尺寸
     * 
     * 基于选项中的分辨率设置，保持宽高比
     */
    float bloomHeight = float(inoutBloomOptions.resolution);
    float bloomWidth  = bloomHeight * aspect;

    // we might need to adjust the max # of levels
    /**
     * 根据缓冲区尺寸调整最大级别数
     * 
     * 计算可以生成的最大 Mipmap 级别数，并限制选项中的级别数
     */
    const uint32_t major = uint32_t(std::max(bloomWidth,  bloomHeight));
    const uint8_t maxLevels = FTexture::maxLevelCount(major);
    inoutBloomOptions.levels = std::min(inoutBloomOptions.levels, maxLevels);
    inoutBloomOptions.levels = std::min(inoutBloomOptions.levels, kMaxBloomLevels);

    if (inoutBloomOptions.quality == QualityLevel::LOW) {
        // In low quality mode, we adjust the bloom buffer size so that both dimensions
        // have enough exact mip levels. This can slightly affect the aspect ratio causing
        // some artifacts:
        // - add some anamorphism (experimentally not visible)
        // - visible bloom size changes with dynamic resolution in non-homogenous mode
        // This allows us to use the 9 sample downsampling filter (instead of 13)
        // for at least 4 levels.
        /**
         * 低质量模式：调整泛光缓冲区尺寸
         * 
         * 确保两个维度都有足够的精确 Mipmap 级别。这可能会轻微影响宽高比，导致一些伪影：
         * - 添加一些变形（实验上不可见）
         * - 在非均匀模式下，泛光大小随动态分辨率变化可见
         * 
         * 这允许我们使用 9 采样下采样滤波器（而不是 13 采样）至少 4 个级别
         */
        uint32_t width  = std::max(16u, uint32_t(std::floor(bloomWidth)));
        uint32_t height = std::max(16u, uint32_t(std::floor(bloomHeight)));
        width  &= ~((1 << 4) - 1);  // at least 4 levels
        height &= ~((1 << 4) - 1);
        bloomWidth  = float(width);
        bloomHeight = float(height);
    }

    /**
     * 阈值处理标志（提取高亮区域）
     */
    bool threshold = inoutBloomOptions.threshold;

    // we don't need to do the fireflies reduction if we have TAA (it already does it)
    /**
     * 萤火虫减少标志
     * 
     * 如果启用了 TAA（时间抗锯齿），则不需要萤火虫减少，因为 TAA 已经处理了
     */
    bool fireflies = threshold && !taaOptions.enabled;

    assert_invariant(bloomWidth && bloomHeight);

    /**
     * 预下采样阶段
     * 
     * 如果输入尺寸远大于泛光缓冲区尺寸，先进行预下采样。
     * 这确保最终的泛光缓冲区尺寸合适。
     */
    while (2 * bloomWidth < float(desc.width) || 2 * bloomHeight < float(desc.height)) {
        if (inoutBloomOptions.quality == QualityLevel::LOW ||
            inoutBloomOptions.quality == QualityLevel::MEDIUM) {
            /**
             * 低/中等质量：直接下采样输入
             */
            input = downscalePass(fg, input, {
                            .width  = (desc.width  = std::max(1u, desc.width  / 2)),
                            .height = (desc.height = std::max(1u, desc.height / 2)),
                            .format = outFormat
                    },
                    threshold, inoutBloomOptions.highlight, fireflies);
            /**
             * 阈值和萤火虫减少只在第一次下采样时执行
             */
            threshold = false; // we do the thresholding only once during down sampling
            fireflies = false; // we do the fireflies reduction only once during down sampling
        } else if (inoutBloomOptions.quality == QualityLevel::HIGH ||
                   inoutBloomOptions.quality == QualityLevel::ULTRA) {
            // In high quality mode, we increase the size of the bloom buffer such that the
            // first scaling is less than 2x, and we increase the number of levels accordingly.
            /**
             * 高/超高质量：增加泛光缓冲区尺寸
             * 
             * 使第一次缩放小于 2x，并相应增加级别数。
             * 但不能超过硬件保证的最小规格（2048）
             */
            if (bloomWidth * 2.0f > 2048.0f || bloomHeight * 2.0f > 2048.0f) {
                // but we can't scale above the h/w guaranteed minspec
                break;
            }
            bloomWidth  *= 2.0f;
            bloomHeight *= 2.0f;
            inoutBloomOptions.levels++;
        }
    }

    // convert back to integer width/height
    /**
     * 将浮点尺寸转换回整数
     */
    uint32_t const width  = std::max(1u, uint32_t(std::floor(bloomWidth)));
    uint32_t const height = std::max(1u, uint32_t(std::floor(bloomHeight)));

    /**
     * 执行最终下采样到泛光缓冲区尺寸
     */
    input = downscalePass(fg, input,
            { .width = width, .height = height, .format = outFormat },
            threshold, inoutBloomOptions.highlight, fireflies);

    struct BloomPassData {
        FrameGraphId<FrameGraphTexture> out;
        uint32_t outRT[kMaxBloomLevels];
    };

    // Creating a mip-chain poses a "feedback" loop problem on some GPU. We will disable
    // Bloom on these.
    // See: https://github.com/google/filament/issues/2338

    /**
     * 泛光下采样通道
     * 
     * 生成泛光 Mipmap 链。每个级别都是通过下采样上一级别生成的。
     * 
     * 注意：在某些 GPU 上创建 Mipmap 链会导致"反馈循环"问题，这些 GPU 上会禁用泛光。
     * 参见：https://github.com/google/filament/issues/2338
     */
    auto const& bloomDownsamplePass = fg.addPass<BloomPassData>("Bloom Downsample",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 创建泛光输出纹理（包含多个 Mipmap 级别）
                 */
                data.out = builder.createTexture("Bloom Out Texture", {
                        .width = width,
                        .height = height,
                        .levels = inoutBloomOptions.levels,
                        .format = outFormat
                });
                data.out = builder.sample(data.out);

                /**
                 * 为每个 Mipmap 级别创建子资源和渲染通道
                 */
                for (size_t i = 0; i < inoutBloomOptions.levels; i++) {
                    auto out = builder.createSubresource(data.out, "Bloom Out Texture mip",
                            { .level = uint8_t(i) });
                    if (i == 0) {
                        // this causes the last blit above to render into this mip
                        /**
                         * 第 0 级：使用 forwardResource 将输入重定向到输出
                         * 
                         * 这使上面的最后一次 blit 渲染到这个 Mipmap 级别
                         */
                       fg.forwardResource(out, input);
                    }
                    /**
                     * 声明渲染通道
                     */
                    builder.declareRenderPass(out, &data.outRT[i]);
                }
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                // TODO: if downsampling is not exactly a multiple of two, use the 13 samples
                //       filter. This is generally the accepted solution, however, the 13 samples
                //       filter is not correct either when we don't sample at integer coordinates,
                //       but it seems ot create less artifacts.
                //       A better solution might be to use the filter described in
                //       Castaño, 2013, "Shadow Mapping Summary Part 1", which is 5x5 filter with
                //       9 samples, but works at all coordinates.

                /**
                 * 获取硬件纹理句柄
                 */
                auto hwOut = resources.getTexture(data.out);

                /**
                 * 获取两种下采样材质（9 采样和 13 采样）
                 * 
                 * 9 采样滤波器：当尺寸是 2 的倍数时使用（更高效）
                 * 13 采样滤波器：当尺寸不是 2 的倍数时使用（更准确）
                 */
                auto const& material9 = getPostProcessMaterial("bloomDownsample9");
                auto* mi9 = getMaterialInstance(mEngine, material9);

                auto const& material13 = getPostProcessMaterial("bloomDownsample");
                auto* mi13 = getMaterialInstance(mEngine, material13);
                // These material instances have no UBO updates in the loop, so we do not move
                // getMaterialInstance() inside the loop.

                /**
                 * 为每个 Mipmap 级别生成下采样
                 * 
                 * 从第 1 级开始（第 0 级已由 forwardResource 填充）
                 */
                for (size_t i = 1; i < inoutBloomOptions.levels; i++) {
                    /**
                     * 获取目标渲染通道信息
                     */
                    auto hwDstRT = resources.getRenderPassInfo(data.outRT[i]);
                    /**
                     * 设置丢弃标志（开始时丢弃颜色，结束时保留）
                     */
                    hwDstRT.params.flags.discardStart = TargetBufferFlags::COLOR;
                    hwDstRT.params.flags.discardEnd = TargetBufferFlags::NONE;

                    // if downsampling is a multiple of 2 in each dimension we can use the
                    // 9 samples filter.
                    /**
                     * 根据上一级的视口尺寸选择滤波器
                     * 
                     * 如果尺寸是 2 的倍数，使用 9 采样滤波器（更高效）
                     * 否则使用 13 采样滤波器（更准确）
                     */
                    auto vp = resources.getRenderPassInfo(data.outRT[i-1]).params.viewport;
                    auto* const mi = (vp.width & 1 || vp.height & 1) ? mi13 : mi9;
                    /**
                     * 创建上一级的纹理视图作为源
                     */
                    auto hwOutView = driver.createTextureView(hwOut, i - 1, 1);
                    /**
                     * 设置源纹理采样器
                     */
                    mi->setParameter("source", hwOutView, SamplerParams{
                            .filterMag = SamplerMagFilter::LINEAR,
                            .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST });
                    /**
                     * 提交并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, hwDstRT, mi);
                    /**
                     * 销毁临时纹理视图
                     */
                    driver.destroyTexture(hwOutView);
                }
                unbindAllDescriptorSets(driver);
            });

    // output of bloom downsample pass becomes input of next (flare) pass
    /**
     * 泛光下采样通道的输出成为下一个（光晕）通道的输入
     */
    input = bloomDownsamplePass->out;

    // flare pass
    /**
     * 光晕通道
     * 
     * 生成镜头光晕效果（可选）
     */
    auto const flare = flarePass(fg, input, width, height, outFormat, inoutBloomOptions);

    /**
     * 泛光上采样通道
     * 
     * 将模糊后的高亮区域从最细级别开始，逐级上采样并混合，
     * 最终混合回原始图像尺寸。
     */
    auto const& bloomUpsamplePass = fg.addPass<BloomPassData>("Bloom Upsample",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源（从下采样通道输出）
                 */
                data.out = builder.sample(input);
                /**
                 * 为每个 Mipmap 级别创建子资源和渲染通道
                 */
                for (size_t i = 0; i < inoutBloomOptions.levels; i++) {
                    auto out = builder.createSubresource(data.out, "Bloom Out Texture mip",
                            { .level = uint8_t(i) });
                    builder.declareRenderPass(out, &data.outRT[i]);
                }
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取硬件纹理句柄和描述符
                 */
                auto hwOut = resources.getTexture(data.out);
                auto const& outDesc = resources.getDescriptor(data.out);

                /**
                 * 获取上采样材质和管道状态
                 */
                auto const& material = getPostProcessMaterial("bloomUpsample");
                FMaterial const* const ma = material.getMaterial(mEngine);

                /**
                 * 设置混合函数（加法混合：ONE + ONE）
                 * 
                 * 用于将不同级别的模糊结果叠加
                 */
                auto pipeline = getPipelineState(ma);
                pipeline.rasterState.blendFunctionSrcRGB = BlendFunction::ONE;
                pipeline.rasterState.blendFunctionDstRGB = BlendFunction::ONE;

                /**
                 * 从最细级别开始，逐级上采样并混合
                 * 
                 * 注意：我们不想为每个通道使用相同的实例，因为那意味着使用相同的 UBO，
                 * 这会导致通道之间的同步。
                 */
                for (size_t j = inoutBloomOptions.levels, i = j - 1; i >= 1; i--, j++) {
                    // Note that we wouldn't want to use the same instance for each pass since that
                    // would imply using the same UBOs, which implies synchronization across the
                    // passes.
                    /**
                     * 为每个通道创建新的材质实例
                     */
                    FMaterialInstance* mi = getMaterialInstance(ma);
                    /**
                     * 获取目标渲染通道信息（上一级）
                     */
                    auto hwDstRT = resources.getRenderPassInfo(data.outRT[i - 1]);
                    /**
                     * 设置丢弃标志（不丢弃，因为我们要混合）
                     */
                    hwDstRT.params.flags.discardStart = TargetBufferFlags::NONE; // b/c we'll blend
                    hwDstRT.params.flags.discardEnd = TargetBufferFlags::NONE;
                    /**
                     * 计算目标级别的尺寸
                     */
                    auto w = FTexture::valueForLevel(i - 1, outDesc.width);
                    auto h = FTexture::valueForLevel(i - 1, outDesc.height);
                    /**
                     * 创建当前级别的纹理视图作为源
                     */
                    auto hwOutView = driver.createTextureView(hwOut, i, 1);
                    /**
                     * 设置分辨率参数（宽度、高度、倒数）
                     */
                    mi->setParameter("resolution", float4{ w, h, 1.0f / w, 1.0f / h });
                    /**
                     * 设置源纹理采样器
                     */
                    mi->setParameter("source", hwOutView, SamplerParams{
                            .filterMag = SamplerMagFilter::LINEAR,
                            .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST});
                    /**
                     * 提交材质参数并使用
                     */
                    mi->commit(driver, getUboManager());
                    mi->use(driver);
                    /**
                     * 渲染全屏四边形（混合到上一级）
                     */
                    renderFullScreenQuad(hwDstRT, pipeline, driver);
                    /**
                     * 解绑材质描述符堆
                     */
                    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);
                    /**
                     * 销毁临时纹理视图
                     */
                    driver.destroyTexture(hwOutView);
                }
                unbindAllDescriptorSets(driver);
            });

    return { bloomUpsamplePass->out, flare };
}

/**
 * 光晕（Flare）通道
 * 
 * 生成镜头光晕效果，模拟明亮光源在相机镜头中产生的光晕和鬼影。
 * 包括：
 * - 鬼影（Ghosts）：光源的多个反射
 * - 光晕（Halo）：光源周围的光环
 * - 色差（Chromatic Aberration）：不同颜色通道的偏移
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID（泛光下采样结果）
 * @param width 输入宽度（像素）
 * @param height 输入高度（像素）
 * @param outFormat 输出纹理格式
 * @param bloomOptions 泛光选项（包含光晕参数）
 * @return 光晕效果输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理（尺寸为输入的一半）
 * 2. 使用 flare 材质生成光晕效果
 * 3. 应用高斯模糊（后续处理）
 * 
 * 注意：
 * - 输出尺寸为输入的一半，以优化性能
 * - 支持可配置的鬼影数量、间距、光晕半径等参数
 */
UTILS_NOINLINE
FrameGraphId<FrameGraphTexture> PostProcessManager::flarePass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        uint32_t const width, uint32_t const height,
        TextureFormat const outFormat,
        BloomOptions const& bloomOptions) noexcept {

    struct FlarePassData {
        FrameGraphId<FrameGraphTexture> in;
        FrameGraphId<FrameGraphTexture> out;
    };
    auto const& flarePass = fg.addPass<FlarePassData>("Flare",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.in = builder.sample(input);
                /**
                 * 创建输出纹理（尺寸为输入的一半）
                 */
                data.out = builder.createTexture("Flare Texture", {
                        .width  = std::max(1u, width  / 2),
                        .height = std::max(1u, height / 2),
                        .format = outFormat
                });
                data.out = builder.declareRenderPass(data.out);
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取纹理资源和渲染通道信息
                 */
                auto in = resources.getTexture(data.in);
                auto const out = resources.getRenderPassInfo(0);
                /**
                 * 计算宽高比
                 */
                const float aspectRatio = float(width) / float(height);

                /**
                 * 获取光晕材质并设置参数
                 */
                auto const& material = getPostProcessMaterial("flare");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material);

                /**
                 * 设置输入颜色采样器
                 */
                mi->setParameter("color", in, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST
                });

                /**
                 * 设置 Mipmap 级别（根据分辨率调整）
                 */
                mi->setParameter("level", 0.0f);    // adjust with resolution
                /**
                 * 设置宽高比（X 和倒数）
                 */
                mi->setParameter("aspectRatio", float2{ aspectRatio, 1.0f / aspectRatio });
                /**
                 * 设置阈值（鬼影阈值和光晕阈值）
                 */
                mi->setParameter("threshold",
                        float2{ bloomOptions.ghostThreshold, bloomOptions.haloThreshold });
                /**
                 * 设置色差强度
                 */
                mi->setParameter("chromaticAberration", bloomOptions.chromaticAberration);
                /**
                 * 设置鬼影数量
                 */
                mi->setParameter("ghostCount", float(bloomOptions.ghostCount));
                /**
                 * 设置鬼影间距
                 */
                mi->setParameter("ghostSpacing", bloomOptions.ghostSpacing);
                /**
                 * 设置光晕半径
                 */
                mi->setParameter("haloRadius", bloomOptions.haloRadius);
                /**
                 * 设置光晕厚度
                 */
                mi->setParameter("haloThickness", bloomOptions.haloThickness);

                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    constexpr float kernelWidth = 9;
    constexpr float sigma = (kernelWidth + 1.0f) / 6.0f;
    auto const flare = gaussianBlurPass(fg, flarePass->out, {}, false, kernelWidth, sigma);
    return flare;
}

/**
 * 获取渐晕（Vignette）参数
 * 
 * 计算渐晕效果所需的参数。渐晕用于模拟相机镜头的边缘暗化效果。
 * 
 * @param options 渐晕选项（包含圆度、中点、羽化等参数）
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 * @return float4 渐晕参数向量（midPoint, radius, aspect, feather）
 *                如果渐晕未启用，返回 half 最大值表示禁用
 * 
 * 参数说明：
 * - roundness: 0.0 到 0.5 时，渐晕从圆角矩形转变为椭圆形
 *              0.5 到 1.0 时，渐晕从椭圆形转变为圆形
 * - midPoint: 渐晕的中间点位置
 * - feather: 渐晕边缘的羽化程度
 * 
 * 返回值：
 * - x (midPoint): 渐晕中点
 * - y (radius): 圆角半径（用于 pow() 函数）
 * - z (aspect): 将椭圆转换为圆形的宽高比因子
 * - w (feather): 羽化参数
 */
UTILS_NOINLINE
static float4 getVignetteParameters(VignetteOptions const& options,
        uint32_t const width, uint32_t const height) noexcept {
    if (options.enabled) {
        // Vignette params
        // From 0.0 to 0.5 the vignette is a rounded rect that turns into an oval
        // From 0.5 to 1.0 the vignette turns from oval to circle
        /**
         * 计算椭圆和圆形参数
         * 
         * oval: 椭圆阶段参数（0.0 到 0.5 范围映射到 0.0 到 1.0）
         * circle: 圆形阶段参数（0.5 到 1.0 范围映射到 0.0 到 1.0）
         */
        float const oval = min(options.roundness, 0.5f) * 2.0f;
        float const circle = (max(options.roundness, 0.5f) - 0.5f) * 2.0f;
        /**
         * 计算圆度参数（用于控制圆角矩形到椭圆的转换）
         */
        float const roundness = (1.0f - oval) * 6.0f + oval;

        // Mid point varies during the oval/rounded section of roundness
        // We also modify it to emphasize feathering
        /**
         * 计算渐晕中点
         * 
         * 在椭圆/圆角区域，中点会变化。我们还修改它以强调羽化效果。
         */
        float const midPoint = (1.0f - options.midPoint) * mix(2.2f, 3.0f, oval)
                         * (1.0f - 0.1f * options.feather);

        // Radius of the rounded corners as a param to pow()
        /**
         * 计算圆角半径（作为 pow() 函数的参数）
         */
        float const radius = roundness *
                mix(1.0f + 4.0f * (1.0f - options.feather), 1.0f, std::sqrt(oval));

        // Factor to transform oval into circle
        /**
         * 计算将椭圆转换为圆形的宽高比因子
         */
        float const aspect = mix(1.0f, float(width) / float(height), circle);

        return float4{ midPoint, radius, aspect, options.feather };
    }

    // Set half-max to show disabled
    /**
     * 如果渐晕未启用，返回 half 最大值表示禁用
     */
    return float4{ std::numeric_limits<half>::max() };
}

/**
 * 颜色分级准备子通道
 * 
 * 在渲染通道的子通道执行颜色分级之前，准备材质实例并提交参数。
 * 这是一个两阶段过程的第一阶段，用于优化性能。
 * 
 * @param driver 驱动 API 引用
 * @param colorGrading 颜色分级对象指针
 * @param colorGradingConfig 颜色分级配置
 * @param vignetteOptions 渐晕选项
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 * 
 * 注意：
 * - 此函数只准备和提交材质参数，不执行实际渲染
 * - 实际渲染在 colorGradingSubpass() 中执行
 */
void PostProcessManager::colorGradingPrepareSubpass(DriverApi& driver,
        const FColorGrading* colorGrading, ColorGradingConfig const& colorGradingConfig,
        VignetteOptions const& vignetteOptions, uint32_t const width, uint32_t const height) noexcept {

    /**
     * 获取颜色分级子通道材质并配置
     */
    auto& material = getPostProcessMaterial("colorGradingAsSubpass");
    FMaterialInstance* const mi =
            configureColorGradingMaterial(material, colorGrading, colorGradingConfig,
                    vignetteOptions, width, height);
    /**
     * 提交材质参数到驱动
     */
    mi->commit(driver, getUboManager());
}

/**
 * 颜色分级子通道
 * 
 * 在渲染通道的子通道中执行颜色分级渲染。
 * 这是两阶段过程的第二阶段，使用 prepareSubpass 中准备的材料实例。
 * 
 * @param driver 驱动 API 引用
 * @param colorGradingConfig 颜色分级配置（用于确定变体）
 * 
 * 处理流程：
 * 1. 绑定描述符堆
 * 2. 根据配置选择变体（透明或不透明）
 * 3. 使用已准备的材质实例
 * 4. 进入下一个子通道并绘制全屏四边形
 * 
 * 注意：
 * - UBO 已在 colorGradingPrepareSubpass() 中设置和提交
 * - 使用固定索引的材质实例以提高性能
 */
void PostProcessManager::colorGradingSubpass(DriverApi& driver,
        ColorGradingConfig const& colorGradingConfig) noexcept {

    /**
     * 绑定描述符堆
     */
    bindPostProcessDescriptorSet(driver);
    bindPerRenderableDescriptorSet(driver);

    /**
     * 根据配置选择变体
     */
    PostProcessVariant const variant = colorGradingConfig.translucent ?
            PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE;

    /**
     * 获取颜色分级子通道材质
     */
    auto const& material = getPostProcessMaterial("colorGradingAsSubpass");
    FMaterial const* const ma = material.getMaterial(mEngine, variant);
    // the UBO has been set and committed in colorGradingPrepareSubpass()
    /**
     * 获取固定材质实例索引（根据透明性选择）
     */
    int32_t const fixedIndex = colorGradingConfig.translucent
                                       ? mFixedMaterialInstanceIndex.colorGradingTranslucent
                                       : mFixedMaterialInstanceIndex.colorGradingOpaque;

    /**
     * 获取材质实例并使用
     */
    FMaterialInstance const* mi = mMaterialInstanceManager.getMaterialInstance(ma, fixedIndex);
    mi->use(driver);
    /**
     * 获取管道状态并进入下一个子通道
     */
    auto const pipeline = getPipelineState(ma, variant);
    driver.nextSubpass();
    /**
     * 设置裁剪并绘制全屏四边形
     */
    driver.scissor(mi->getScissor());
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
}

/**
 * 自定义解析准备子通道
 * 
 * 准备自定义解析（压缩/解压）操作的材质实例。
 * 这是一个两阶段过程的第一阶段，用于优化性能。
 * 
 * @param driver 驱动 API 引用
 * @param op 自定义解析操作（COMPRESS 压缩或 UNCOMPRESS 解压）
 * 
 * 处理流程：
 * 1. 获取自定义解析子通道材质
 * 2. 获取固定材质实例
 * 3. 设置方向参数（1.0 = 压缩，-1.0 = 解压）
 * 4. 提交材质参数
 * 
 * 注意：
 * - 此函数只准备和提交材质参数，不执行实际渲染
 * - 实际渲染在 customResolveSubpass() 中执行
 */
void PostProcessManager::customResolvePrepareSubpass(DriverApi& driver, CustomResolveOp const op) noexcept {
    /**
     * 获取自定义解析子通道材质
     */
    auto const& material = getPostProcessMaterial("customResolveAsSubpass");
    auto ma = material.getMaterial(mEngine, PostProcessVariant::OPAQUE);
    /**
     * 获取固定材质实例
     */
    auto [mi, fixedIndex] = mMaterialInstanceManager.getFixedMaterialInstance(ma);
    mFixedMaterialInstanceIndex.customResolve = fixedIndex;
    /**
     * 设置方向参数（压缩 = 1.0，解压 = -1.0）
     */
    mi->setParameter("direction", op == CustomResolveOp::COMPRESS ? 1.0f : -1.0f),
    /**
     * 提交材质参数
     */
    mi->commit(driver, getUboManager());
    material.getMaterial(mEngine);
}

/**
 * 自定义解析子通道
 * 
 * 在渲染通道的子通道中执行自定义解析渲染（压缩或解压）。
 * 这是两阶段过程的第二阶段，使用 prepareSubpass 中准备的材质实例。
 * 
 * @param driver 驱动 API 引用
 * 
 * 处理流程：
 * 1. 绑定描述符堆
 * 2. 获取已准备的材质实例
 * 3. 进入下一个子通道
 * 4. 绘制全屏四边形执行解析
 * 
 * 注意：
 * - UBO 已在 customResolvePrepareSubpass() 中设置和提交
 * - 使用固定索引的材质实例以提高性能
 */
void PostProcessManager::customResolveSubpass(DriverApi& driver) noexcept {
    /**
     * 绑定描述符堆
     */
    bindPostProcessDescriptorSet(driver);
    bindPerRenderableDescriptorSet(driver);

    /**
     * 获取自定义解析材质和实例
     */
    auto const& material = getPostProcessMaterial("customResolveAsSubpass");
    FMaterial const* const ma = material.getMaterial(mEngine);
    // the UBO has been set and committed in customResolvePrepareSubpass()
    /**
     * 获取固定材质实例
     */
    FMaterialInstance const* mi = mMaterialInstanceManager.getMaterialInstance(ma,
            mFixedMaterialInstanceIndex.customResolve);
    mi->use(driver);

    /**
     * 获取管道状态并进入下一个子通道
     */
    auto const pipeline = getPipelineState(ma);
    driver.nextSubpass();
    /**
     * 设置裁剪并绘制全屏四边形
     */
    driver.scissor(mi->getScissor());
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
}

/**
 * 自定义解析解压通道
 * 
 * 使用子通道执行自定义解压操作。这通常用于将压缩的纹理解压回原始格式。
 * 
 * @param fg 帧图引用
 * @param inout 输入/输出纹理 ID（同一纹理，先读取后写入）
 * @return 解压后的纹理 ID（与输入相同）
 * 
 * 处理流程：
 * 1. 声明纹理为子通道输入和颜色附件
 * 2. 准备自定义解析子通道（UNCOMPRESS 模式）
 * 3. 开始渲染通道
 * 4. 执行子通道解压
 * 5. 结束渲染通道
 * 
 * 注意：
 * - 使用子通道可以避免额外的纹理拷贝
 * - 输入和输出是同一个纹理（就地操作）
 * - 子通道掩码设置为 1 表示这是第一个子通道
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::customResolveUncompressPass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const inout) noexcept {
    struct UncompressData {
        FrameGraphId<FrameGraphTexture> inout;
    };
    auto const& detonemapPass = fg.addPass<UncompressData>("Uncompress Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明为子通道输入（读取）
                 */
                data.inout = builder.read(inout, FrameGraphTexture::Usage::SUBPASS_INPUT);
                /**
                 * 声明为颜色附件（写入）
                 */
                data.inout = builder.write(data.inout, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("Uncompress target", {
                        .attachments = { .color = { data.inout }}
                });
            },
            [=, this](FrameGraphResources const& resources, auto const&, DriverApi& driver) {
                /**
                 * 准备自定义解析子通道（解压模式）
                 */
                customResolvePrepareSubpass(driver, CustomResolveOp::UNCOMPRESS);
                /**
                 * 获取渲染通道信息并设置子通道掩码
                 */
                auto out = resources.getRenderPassInfo();
                out.params.subpassMask = 1;
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 开始渲染通道
                 */
                driver.beginRenderPass(out.target, out.params);
                /**
                 * 执行子通道解压
                 */
                customResolveSubpass(driver);
                /**
                 * 结束渲染通道
                 */
                driver.endRenderPass();
            });
    return detonemapPass->inout;
}


/**
 * 清除辅助缓冲区准备
 * 
 * 准备清除辅助缓冲区（深度、模板等）操作的材质实例。
 * 这是一个两阶段过程的第一阶段，用于优化性能。
 * 
 * @param driver 驱动 API 引用
 * 
 * 处理流程：
 * 1. 获取清除深度材质
 * 2. 获取固定材质实例
 * 3. 提交材质参数
 * 
 * 注意：
 * - 此函数只准备和提交材质参数，不执行实际渲染
 * - 实际渲染在 clearAncillaryBuffers() 中执行
 * - 未来可能支持清除模板缓冲区
 */
void PostProcessManager::clearAncillaryBuffersPrepare(DriverApi& driver) noexcept {
    /**
     * 获取清除深度材质
     */
    auto const& material = getPostProcessMaterial("clearDepth");
    auto ma = material.getMaterial(mEngine, PostProcessVariant::OPAQUE);
    /**
     * 获取固定材质实例
     */
    auto [mi, fixedIndex] = mMaterialInstanceManager.getFixedMaterialInstance(ma);
    mFixedMaterialInstanceIndex.clearDepth = fixedIndex;
    /**
     * 提交材质参数
     */
    mi->commit(driver, getUboManager());
    material.getMaterial(mEngine);
}

/**
 * 清除辅助缓冲区
 * 
 * 清除辅助缓冲区（深度、模板等）。使用全屏四边形和特殊的深度函数
 * 来实现高效的缓冲区清除。
 * 
 * @param driver 驱动 API 引用
 * @param attachments 要清除的附件标志（目前只支持 DEPTH）
 * 
 * 处理流程：
 * 1. 检查是否需要清除深度缓冲区
 * 2. 绑定描述符堆
 * 3. 获取已准备的材质实例
 * 4. 设置深度函数为 ALWAYS（始终通过）
 * 5. 绘制全屏四边形清除缓冲区
 * 
 * 注意：
 * - 目前只支持清除深度缓冲区
 * - 使用 ALWAYS 深度函数确保所有像素都写入
 * - UBO 已在 clearAncillaryBuffersPrepare() 中设置和提交
 */
void PostProcessManager::clearAncillaryBuffers(DriverApi& driver,
        TargetBufferFlags attachments) const noexcept {
    // in the future we might allow STENCIL as well
    /**
     * 目前只支持清除深度缓冲区（未来可能支持模板缓冲区）
     */
    attachments &= TargetBufferFlags::DEPTH;
    /**
     * 如果不需要清除深度，直接返回
     */
    if (none(attachments & TargetBufferFlags::DEPTH)) {
        return;
    }

    /**
     * 绑定描述符堆
     */
    bindPostProcessDescriptorSet(driver);
    bindPerRenderableDescriptorSet(driver);

    /**
     * 获取清除深度材质
     */
    auto const& material = getPostProcessMaterial("clearDepth");
    FMaterial const* const ma = material.getMaterial(mEngine);

    // the UBO has been set and committed in clearAncillaryBuffersPrepare()
    /**
     * 获取固定材质实例
     */
    FMaterialInstance const* const mi = mMaterialInstanceManager.getMaterialInstance(ma,
            mFixedMaterialInstanceIndex.clearDepth);

    mi->use(driver);

    /**
     * 获取管道状态并设置深度函数为 ALWAYS
     * 
     * 使用 ALWAYS 确保所有像素都通过深度测试并写入深度缓冲区
     */
    auto pipeline = getPipelineState(ma);
    pipeline.rasterState.depthFunc = RasterState::DepthFunc::A;

    /**
     * 设置裁剪并绘制全屏四边形
     */
    driver.scissor(mi->getScissor());
    driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
}

/**
 * 颜色分级后处理效果
 * 
 * 应用颜色分级（Color Grading）效果，包括：
 * 1. 颜色查找表（LUT）应用
 * 2. 泛光（Bloom）混合
 * 3. 光晕（Flare）混合
 * 4. 污垢纹理（Dirt）叠加
 * 5. 星爆（Starburst）效果
 * 6. 渐晕（Vignette）效果
 * 7. 抖动（Dithering）处理
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID
 * @param vp 视口信息
 * @param bloom 泛光纹理 ID（可选）
 * @param flare 光晕纹理 ID（可选）
 * @param colorGrading 颜色分级对象指针
 * @param colorGradingConfig 颜色分级配置
 * @param bloomOptions 泛光选项
 * @param vignetteOptions 渐晕选项
 * @return 应用颜色分级后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 导入辅助纹理（污垢、星爆等）
 * 2. 创建颜色分级通道
 * 3. 配置材质参数（LUT、泛光参数、渐晕参数等）
 * 4. 混合所有效果并渲染
 * 
 * 注意：
 * - 支持 LDR 和 HDR 颜色分级
 * - 支持 1D 和 3D LUT
 * - 可选的抖动用于减少色带伪影
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::colorGrading(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input, filament::Viewport const& vp,
        FrameGraphId<FrameGraphTexture> const bloom,
        FrameGraphId<FrameGraphTexture> const flare,
        FColorGrading const* colorGrading,
        ColorGradingConfig const& colorGradingConfig,
        BloomOptions const& bloomOptions,
        VignetteOptions const& vignetteOptions) noexcept
{
    /**
     * 泛光污垢纹理和星爆纹理（可选）
     */
    FrameGraphId<FrameGraphTexture> bloomDirt;
    FrameGraphId<FrameGraphTexture> starburst;

    /**
     * 计算泛光强度
     */
    float bloomStrength = 0.0f;
    if (bloomOptions.enabled) {
        bloomStrength = clamp(bloomOptions.strength, 0.0f, 1.0f);
        /**
         * 如果启用了污垢纹理，导入到 FrameGraph
         */
        if (bloomOptions.dirt) {
            FTexture const* fdirt = downcast(bloomOptions.dirt);
            FrameGraphTexture const frameGraphTexture{ .handle = fdirt->getHwHandleForSampling() };
            bloomDirt = fg.import("dirt", {
                    .width = (uint32_t)fdirt->getWidth(0u),
                    .height = (uint32_t)fdirt->getHeight(0u),
                    .format = fdirt->getFormat()
            }, FrameGraphTexture::Usage::SAMPLEABLE, frameGraphTexture);
        }

        /**
         * 如果启用了镜头光晕和星爆，导入星爆纹理
         */
        if (bloomOptions.lensFlare && bloomOptions.starburst) {
            starburst = fg.import("starburst", {
                    .width = 256, .height = 1, .format = TextureFormat::R8
            }, FrameGraphTexture::Usage::SAMPLEABLE,
                    FrameGraphTexture{ .handle = mStarburstTexture });
        }
    }

    /**
     * 颜色分级通道数据结构
     */
    struct PostProcessColorGrading {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
        FrameGraphId<FrameGraphTexture> bloom;
        FrameGraphId<FrameGraphTexture> flare;
        FrameGraphId<FrameGraphTexture> dirt;
        FrameGraphId<FrameGraphTexture> starburst;
    };

    /**
     * 颜色分级通道
     * 
     * 在 FrameGraph 中创建颜色分级通道，设置所有输入纹理和输出纹理。
     */
    auto const& ppColorGrading = fg.addPass<PostProcessColorGrading>("colorGrading",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入颜色纹理
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理（按视口尺寸和 LDR 格式）
                 */
                data.output = builder.createTexture("colorGrading output", {
                        .width = vp.width,
                        .height = vp.height,
                        .format = colorGradingConfig.ldrFormat
                });
                /**
                 * 声明为渲染通道
                 */
                data.output = builder.declareRenderPass(data.output);

                /**
                 * 如果存在泛光，声明泛光纹理
                 */
                if (bloom) {
                    data.bloom = builder.sample(bloom);
                }
                /**
                 * 如果存在污垢纹理，声明污垢纹理
                 */
                if (bloomDirt) {
                    data.dirt = builder.sample(bloomDirt);
                }
                /**
                 * 如果启用镜头光晕且存在光晕纹理，声明光晕和星爆纹理
                 */
                if (bloomOptions.lensFlare && flare) {
                    data.flare = builder.sample(flare);
                    if (starburst) {
                        data.starburst = builder.sample(starburst);
                    }
                }
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取输入颜色纹理
                 */
                auto colorTexture = resources.getTexture(data.input);

                /**
                 * 获取泛光纹理（如果存在，否则使用零纹理）
                 */
                auto bloomTexture =
                        data.bloom ? resources.getTexture(data.bloom) : getZeroTexture();

                /**
                 * 获取光晕纹理（如果存在，否则使用零纹理）
                 */
                auto flareTexture =
                        data.flare ? resources.getTexture(data.flare) : getZeroTexture();

                /**
                 * 获取污垢纹理（如果存在，否则使用单位纹理）
                 */
                auto dirtTexture =
                        data.dirt ? resources.getTexture(data.dirt) : getOneTexture();

                /**
                 * 获取星爆纹理（如果存在，否则使用单位纹理）
                 */
                auto starburstTexture =
                        data.starburst ? resources.getTexture(data.starburst) : getOneTexture();

                /**
                 * 获取渲染通道信息和描述符
                 */
                auto const& out = resources.getRenderPassInfo();

                auto const& input = resources.getDescriptor(data.input);
                auto const& output = resources.getDescriptor(data.output);

                /**
                 * 获取颜色分级材质并配置
                 */
                auto& material = getPostProcessMaterial("colorGrading");
                FMaterialInstance* const mi =
                        configureColorGradingMaterial(material, colorGrading, colorGradingConfig,
                                vignetteOptions, output.width, output.height);

                /**
                 * 设置颜色缓冲区采样器（着色器使用 texelFetch，不需要采样参数）
                 */
                mi->setParameter("colorBuffer", colorTexture, { /* shader uses texelFetch */ });
                /**
                 * 设置泛光缓冲区采样器（线性过滤，始终读取基础级别）
                 */
                mi->setParameter("bloomBuffer", bloomTexture, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR /* always read base level in shader */
                });
                /**
                 * 设置光晕缓冲区采样器
                 */
                mi->setParameter("flareBuffer", flareTexture, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR
                });
                /**
                 * 设置污垢缓冲区采样器
                 */
                mi->setParameter("dirtBuffer", dirtTexture, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR
                });
                /**
                 * 设置星爆缓冲区采样器（重复模式）
                 */
                mi->setParameter("starburstBuffer", starburstTexture, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR,
                        .wrapS = SamplerWrapMode::REPEAT,
                        .wrapT = SamplerWrapMode::REPEAT
                });

                // Bloom params
                /**
                 * 计算泛光参数
                 * 
                 * x: 泛光强度（按级别数归一化）
                 * y: 混合权重（根据混合模式调整）
                 * z: 污垢强度（如果启用）
                 * w: 光晕强度（如果启用镜头光晕）
                 */
                float4 bloomParameters{
                    bloomStrength / float(bloomOptions.levels),
                    1.0f,
                    (bloomOptions.enabled && bloomOptions.dirt) ? bloomOptions.dirtStrength : 0.0f,
                    bloomOptions.lensFlare ? bloomStrength : 0.0f
                };
                /**
                 * 如果是插值混合模式，调整混合权重
                 */
                if (bloomOptions.blendMode == BloomOptions::BlendMode::INTERPOLATE) {
                    bloomParameters.y = 1.0f - bloomParameters.x;
                }

                /**
                 * 设置泛光参数
                 */
                mi->setParameter("bloom", bloomParameters);
                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left)   / input.width,
                        float(vp.bottom) / input.height,
                        float(vp.width)  / input.width,
                        float(vp.height) / input.height
                });

                /**
                 * 提交并渲染全屏四边形（根据配置选择变体）
                 */
                commitAndRenderFullScreenQuad(driver, out, mi,
                        colorGradingConfig.translucent
                        ? PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE);

                unbindAllDescriptorSets(driver);
            }
    );

    return ppColorGrading->output;
}

/**
 * FXAA（Fast Approximate Anti-Aliasing）后处理抗锯齿
 * 
 * 应用 FXAA 抗锯齿算法，这是一种快速、低开销的后期处理抗锯齿技术。
 * FXAA 通过检测和柔化边缘来减少锯齿，无需多重采样。
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID
 * @param vp 视口信息
 * @param outFormat 输出纹理格式
 * @param preserveAlphaChannel 是否保留 Alpha 通道（影响使用的变体）
 * @return 应用 FXAA 后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理（按视口尺寸）
 * 2. 设置材质参数（颜色缓冲区、视口、像素大小）
 * 3. 渲染全屏四边形应用 FXAA
 * 
 * 注意：
 * - FXAA 是一种后处理技术，不需要深度信息
 * - 性能开销较低，适合移动设备
 * - 可以选择保留 Alpha 通道（用于透明渲染）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::fxaa(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input, filament::Viewport const& vp,
        TextureFormat const outFormat, bool const preserveAlphaChannel) noexcept {

    struct PostProcessFXAA {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    auto const& ppFXAA = fg.addPass<PostProcessFXAA>("fxaa",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理（按视口尺寸）
                 */
                data.output = builder.createTexture("fxaa output", {
                        .width = vp.width,
                        .height = vp.height,
                        .format = outFormat
                });
                data.output = builder.declareRenderPass(data.output);
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取输入描述符和纹理资源
                 */
                auto const& inDesc = resources.getDescriptor(data.input);
                auto const& texture = resources.getTexture(data.input);
                auto const& out = resources.getRenderPassInfo();

                /**
                 * 获取 FXAA 材质
                 */
                auto const& material = getPostProcessMaterial("fxaa");

                /**
                 * 根据是否保留 Alpha 通道选择变体
                 */
                PostProcessVariant const variant = preserveAlphaChannel ?
                        PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE;

                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material, variant);

                /**
                 * 设置颜色缓冲区采样器
                 */
                mi->setParameter("colorBuffer", texture, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR
                });
                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left)   / inDesc.width,
                        float(vp.bottom) / inDesc.height,
                        float(vp.width)  / inDesc.width,
                        float(vp.height) / inDesc.height
                });
                /**
                 * 设置像素大小（用于边缘检测）
                 */
                mi->setParameter("texelSize", 1.0f / float2{ inDesc.width, inDesc.height });

                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi, variant);
                unbindAllDescriptorSets(driver);
            });

    return ppFXAA->output;
}

/**
 * TAA 抖动相机
 * 
 * 为时间抗锯齿（TAA）应用亚像素抖动，通过在不同帧之间移动投影矩阵
 * 来采样不同的亚像素位置，然后通过时间混合来减少锯齿。
 * 
 * @param svp 屏幕视口信息
 * @param taaOptions TAA 选项（包含抖动模式）
 * @param frameHistory 帧历史记录
 * @param pTaa 指向帧历史中 TAA 数据的成员指针
 * @param inoutCameraInfo 输入/输出的相机信息（投影矩阵会被修改）
 * 
 * 处理流程：
 * 1. 更新帧历史和投影矩阵
 * 2. 根据抖动模式计算抖动位置
 * 3. 根据后端调整 Y 轴方向（Metal/Vulkan/WebGPU 需要反转）
 * 4. 将抖动转换为裁剪空间并更新投影矩阵
 * 5. 更新裁剪变换（用于 VERTEX_DOMAIN_DEVICE）
 * 
 * 注意：
 * - 抖动位置在 [-0.5, 0.5] 像素范围内
 * - 不同后端对 Y 轴的处理不同（OpenGL vs Metal/Vulkan）
 * - VERTEX_DOMAIN_DEVICE 需要单独的裁剪变换
 */
void PostProcessManager::TaaJitterCamera(
        filament::Viewport const& svp,
        TemporalAntiAliasingOptions const& taaOptions,
        FrameHistory& frameHistory,
        FrameHistoryEntry::TemporalAA FrameHistoryEntry::*pTaa,
        CameraInfo* inoutCameraInfo) const noexcept {
    /**
     * 获取上一帧和当前帧的 TAA 数据
     */
    auto const& previous = frameHistory.getPrevious().*pTaa;
    auto& current = frameHistory.getCurrent().*pTaa;

    // compute projection
    /**
     * 计算投影矩阵（包含用户视图矩阵）
     */
    current.projection = inoutCameraInfo->projection * inoutCameraInfo->getUserViewMatrix();
    /**
     * 更新帧 ID（递增）
     */
    current.frameId = previous.frameId + 1;

    /**
     * 抖动位置计算 lambda
     * 
     * 根据抖动模式返回对应的抖动序列位置
     */
    auto jitterPosition = [pattern = taaOptions.jitterPattern](size_t const frameIndex) -> float2 {
        using JitterPattern = TemporalAntiAliasingOptions::JitterPattern;
        switch (pattern) {
            case JitterPattern::RGSS_X4:
                return sRGSS4(frameIndex);
            case JitterPattern::UNIFORM_HELIX_X4:
                return sUniformHelix4(frameIndex);
            case JitterPattern::HALTON_23_X8:
                return sHaltonSamples(frameIndex % 8);
            case JitterPattern::HALTON_23_X16:
                return sHaltonSamples(frameIndex % 16);
            case JitterPattern::HALTON_23_X32:
                return sHaltonSamples(frameIndex);
        }
        return { 0.0f, 0.0f };
    };

    // sample position within a pixel [-0.5, 0.5]
    // for metal/vulkan we need to reverse the y-offset
    /**
     * 计算当前帧的抖动位置（像素空间，范围 [-0.5, 0.5]）
     * 
     * 对于 Metal/Vulkan/WebGPU，需要反转 Y 偏移（因为这些后端的坐标系不同）
     */
    current.jitter = jitterPosition(previous.frameId);
    float2 jitter = current.jitter;
    switch (mEngine.getBackend()) {
        case Backend::METAL:
        case Backend::VULKAN:
        case Backend::WEBGPU:
            /**
             * Metal/Vulkan/WebGPU 需要反转 Y 轴
             */
            jitter.y = -jitter.y;
            UTILS_FALLTHROUGH;
        case Backend::OPENGL:
        default:
            break;
    }

    /**
     * 将抖动从像素空间转换为裁剪空间
     * 
     * 裁剪空间的坐标范围是 [-1, 1]，所以需要乘以 2/尺寸
     */
    float2 const jitterInClipSpace = jitter * (2.0f / float2{ svp.width, svp.height });

    // update projection matrix
    /**
     * 更新投影矩阵
     * 
     * 通过修改投影矩阵的第 3 行前两个元素来应用抖动
     */
    inoutCameraInfo->projection[2].xy -= jitterInClipSpace;
    // VERTEX_DOMAIN_DEVICE doesn't apply the projection, but it still needs this
    // clip transform, so we apply it separately (see surface_main.vs)
    /**
     * 更新裁剪变换
     * 
     * VERTEX_DOMAIN_DEVICE 不应用投影矩阵，但仍需要裁剪变换，
     * 所以单独应用（参见 surface_main.vs）
     */
    inoutCameraInfo->clipTransform.zw -= jitterInClipSpace;
}

/**
 * 配置时间抗锯齿（TAA）材质
 * 
 * 设置 TAA 材质的特化常量参数。这些参数控制 TAA 算法的行为，
 * 包括上采样、历史重投影、滤波、颜色空间等选项。
 * 
 * @param taaOptions TAA 选项配置
 * 
 * 配置的参数：
 * - upscaling: 是否启用上采样（用于动态分辨率）
 * - historyReprojection: 历史重投影方法
 * - filterHistory: 是否对历史进行滤波
 * - filterInput: 是否对输入进行滤波
 * - useYCoCg: 是否使用 YCoCg 颜色空间（更好的色度精度）
 * - preventFlickering: 是否防止闪烁
 * - boxType: 裁剪框类型
 * - boxClipping: 裁剪框裁剪方法
 * - varianceGamma: 方差伽马值（用于自适应滤波）
 * 
 * 注意：
 * - 如果任何参数改变，会标记材质为脏并使其失效
 * - 这会导致着色器重新编译（在下次使用时）
 */
void PostProcessManager::configureTemporalAntiAliasingMaterial(
        TemporalAntiAliasingOptions const& taaOptions) noexcept {

    /**
     * 获取 TAA 材质
     */
    FMaterial* const ma = getPostProcessMaterial("taa").getMaterial(mEngine);
    /**
     * 脏标志（如果任何参数改变，设为 true）
     */
    bool dirty = false;

    /**
     * 设置所有 TAA 相关的特化常量参数
     */
    setConstantParameter(ma, "upscaling", taaOptions.upscaling, dirty);
    setConstantParameter(ma, "historyReprojection", taaOptions.historyReprojection, dirty);
    setConstantParameter(ma, "filterHistory", taaOptions.filterHistory, dirty);
    setConstantParameter(ma, "filterInput", taaOptions.filterInput, dirty);
    setConstantParameter(ma, "useYCoCg", taaOptions.useYCoCg, dirty);
    setConstantParameter(ma, "preventFlickering", taaOptions.preventFlickering, dirty);
    setConstantParameter(ma, "boxType", int32_t(taaOptions.boxType), dirty);
    setConstantParameter(ma, "boxClipping", int32_t(taaOptions.boxClipping), dirty);
    setConstantParameter(ma, "varianceGamma", taaOptions.varianceGamma, dirty);
    /**
     * 如果参数改变，使材质失效（会导致重新编译）
     */
    if (dirty) {
        ma->invalidate();
        // TODO: call Material::compile(), we can't do that now because it works only
        //       with surface materials
    }
}

/**
 * 配置颜色分级材质
 * 
 * 设置颜色分级材质的所有参数，包括 LUT（查找表）、渐晕、抖动等。
 * 返回配置好的材质实例，可直接用于渲染。
 * 
 * @param material 颜色分级材质引用
 * @param colorGrading 颜色分级对象指针
 * @param colorGradingConfig 颜色分级配置
 * @param vignetteOptions 渐晕选项
 * @param width 图像宽度（像素）
 * @param height 图像高度（像素）
 * @return 配置好的材质实例指针
 * 
 * 处理流程：
 * 1. 设置特化常量（LUT 维度、LDR/HDR 模式）
 * 2. 根据透明性选择变体并获取材质实例
 * 3. 设置 LUT 采样器参数
 * 4. 设置 LUT 尺寸参数
 * 5. 设置渐晕参数
 * 6. 设置抖动和其他参数
 * 
 * 注意：
 * - 使用固定索引的材质实例以提高性能
 * - LUT 使用线性过滤和边缘夹紧模式
 * - 生成时间噪声用于抖动（减少色带）
 */
FMaterialInstance* PostProcessManager::configureColorGradingMaterial(
        PostProcessMaterial const& material, FColorGrading const* colorGrading,
        ColorGradingConfig const& colorGradingConfig, VignetteOptions const& vignetteOptions,
        uint32_t const width, uint32_t const height) noexcept {
    /**
     * 获取基础材质并设置特化常量
     */
    FMaterial* ma = material.getMaterial(mEngine);
    bool dirty = false;

    /**
     * 设置 LUT 维度（1D 或 3D）
     */
    setConstantParameter(ma, "isOneDimensional", colorGrading->isOneDimensional(), dirty);
    /**
     * 设置 LUT 模式（LDR 或 HDR）
     */
    setConstantParameter(ma, "isLDR", colorGrading->isLDR(), dirty);

    /**
     * 如果参数改变，使材质失效
     */
    if (dirty) {
        ma->invalidate();
        // TODO: call Material::compile(), we can't do that now because it works only
        //       with surface materials
    }

    /**
     * 根据配置选择变体（透明或不透明）
     */
    PostProcessVariant const variant = colorGradingConfig.translucent
                                               ? PostProcessVariant::TRANSLUCENT
                                               : PostProcessVariant::OPAQUE;
    /**
     * 获取对应变体的材质
     */
    ma = material.getMaterial(mEngine, variant);
    /**
     * 获取固定材质实例（使用固定索引以提高性能）
     */
    FMaterialInstance* mi = nullptr;
    int32_t& fixedIndex = colorGradingConfig.translucent
                                  ? mFixedMaterialInstanceIndex.colorGradingTranslucent
                                  : mFixedMaterialInstanceIndex.colorGradingOpaque;
    std::tie(mi, fixedIndex) = mMaterialInstanceManager.getFixedMaterialInstance(ma);

    /**
     * 设置 LUT 采样器参数（线性过滤，边缘夹紧）
     */
    const SamplerParams params = SamplerParams{
            .filterMag = SamplerMagFilter::LINEAR,
            .filterMin = SamplerMinFilter::LINEAR,
            .wrapS = SamplerWrapMode::CLAMP_TO_EDGE,
            .wrapT = SamplerWrapMode::CLAMP_TO_EDGE,
            .wrapR = SamplerWrapMode::CLAMP_TO_EDGE,
            .anisotropyLog2 = 0
    };

    /**
     * 设置 LUT 纹理句柄
     */
    mi->setParameter("lut", colorGrading->getHwHandle(), params);

    /**
     * 计算并设置 LUT 尺寸参数
     * 
     * x: 0.5 / 维度（用于采样偏移）
     * y: (维度 - 1) / 维度（用于正确索引）
     */
    const float lutDimension = float(colorGrading->getDimension());
    mi->setParameter("lutSize", float2{
        0.5f / lutDimension, (lutDimension - 1.0f) / lutDimension,
    });

    /**
     * 生成时间噪声（用于抖动，减少色带）
     */
    const float temporalNoise = mUniformDistribution(mEngine.getRandomEngine());

    /**
     * 计算并设置渐晕参数
     */
    float4 const vignetteParameters = getVignetteParameters(vignetteOptions, width, height);
    mi->setParameter("vignette", vignetteParameters);
    /**
     * 设置渐晕颜色
     */
    mi->setParameter("vignetteColor", vignetteOptions.color);
    /**
     * 设置抖动强度
     */
    mi->setParameter("dithering", colorGradingConfig.dithering);
    /**
     * 设置输出亮度（用于 HDR 到 LDR 的色调映射）
     */
    mi->setParameter("outputLuminance", colorGradingConfig.outputLuminance);
    /**
     * 设置时间噪声
     */
    mi->setParameter("temporalNoise", temporalNoise);

    return mi;
}

/**
 * 时间抗锯齿（TAA，Temporal Anti-Aliasing）
 * 
 * 通过累积多帧历史数据来减少锯齿和闪烁。TAA 使用上一帧的信息
 * 与当前帧混合，利用时间域的信息提高图像质量。
 * 
 * @param fg 帧图引用
 * @param input 当前帧颜色纹理 ID
 * @param depth 深度纹理 ID
 * @param frameHistory 帧历史记录
 * @param pTaa 指向帧历史中 TAA 数据的成员指针
 * @param taaOptions TAA 选项（包含反馈系数、滤波宽度等）
 * @param colorGradingConfig 颜色分级配置（用于确定是否使用子通道）
 * @return 应用 TAA 后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 获取历史帧数据并导入历史纹理
 * 2. 创建 TAA 通道（包含输出和可选的色调映射输出）
 * 3. 计算 Lanczos 滤波权重
 * 4. 执行时间混合（当前帧 + 历史帧）
 * 5. 可选：应用 RCAS 锐化
 * 6. 导出历史数据供下一帧使用
 * 
 * 注意：
 * - 支持上采样模式（2x 分辨率）
 * - 使用重投影将历史帧映射到当前帧的屏幕空间
 * - 可选的颜色分级子通道集成
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::taa(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> input,
        FrameGraphId<FrameGraphTexture> const depth,
        FrameHistory& frameHistory,
        FrameHistoryEntry::TemporalAA FrameHistoryEntry::*pTaa,
        TemporalAntiAliasingOptions const& taaOptions,
        ColorGradingConfig const& colorGradingConfig) noexcept {
    assert_invariant(depth);

    /**
     * 获取上一帧和当前帧的 TAA 数据
     */
    auto const& previous = frameHistory.getPrevious().*pTaa;
    auto& current = frameHistory.getCurrent().*pTaa;

    // if we don't have a history yet, just use the current color buffer as history
    /**
     * 如果还没有历史数据，使用当前颜色缓冲区作为历史
     * 
     * 这样可以避免第一帧的特殊处理
     */
    FrameGraphId<FrameGraphTexture> colorHistory = input;
    if (UTILS_LIKELY(previous.color.handle)) {
        /**
         * 导入历史纹理到 FrameGraph
         */
        colorHistory = fg.import("TAA history", previous.desc,
                FrameGraphTexture::Usage::SAMPLEABLE, previous.color);
    }

    /**
     * 选择历史投影矩阵
     * 
     * 如果历史数据存在，使用历史投影；否则使用当前投影
     */
    mat4 const& historyProjection = previous.color.handle ?
            previous.projection : current.projection;

    /**
     * TAA 通道数据结构
     */
    struct TAAData {
        FrameGraphId<FrameGraphTexture> color;
        FrameGraphId<FrameGraphTexture> depth;
        FrameGraphId<FrameGraphTexture> history;
        FrameGraphId<FrameGraphTexture> output;
        FrameGraphId<FrameGraphTexture> tonemappedOutput;
    };
    /**
     * TAA 通道
     * 
     * 时间抗锯齿通道，通过多帧历史数据减少锯齿和闪烁
     */
    auto const& taaPass = fg.addPass<TAAData>("TAA",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取输入纹理描述符
                 */
                auto desc = fg.getDescriptor(input);
                /**
                 * 如果启用了 TAA 上采样，输出尺寸翻倍
                 */
                if (taaOptions.upscaling) {
                    desc.width *= 2;
                    desc.height *= 2;
                }
                /**
                 * 声明输入资源（只读）
                 */
                data.color = builder.sample(input);      // 当前帧颜色
                data.depth = builder.sample(depth);      // 深度缓冲
                data.history = builder.sample(colorHistory);  // 历史帧颜色
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("TAA output", desc);
                data.output = builder.write(data.output);
                /**
                 * 如果颜色分级作为子通道，创建色调映射输出缓冲
                 */
                if (colorGradingConfig.asSubpass) {
                    data.tonemappedOutput = builder.createTexture("Tonemapped Buffer", {
                            .width = desc.width,
                            .height = desc.height,
                            .format = colorGradingConfig.ldrFormat
                    });
                    data.tonemappedOutput = builder.write(data.tonemappedOutput, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    /**
                     * TAA 输出作为子通道输入
                     */
                    data.output = builder.read(data.output, FrameGraphTexture::Usage::SUBPASS_INPUT);
                }
                data.output = builder.write(data.output, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass("TAA target", {
                        .attachments = { .color = { data.output, data.tonemappedOutput }}
                });
            },
            [=, this, &current](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 归一化坐标到裁剪空间的变换矩阵
                 * 
                 * 将 [0,1] 范围的归一化坐标转换为 [-1,1] 的裁剪空间坐标
                 */
                constexpr mat4f normalizedToClip{mat4f::row_major_init{
                        2, 0, 0, -1,
                        0, 2, 0, -1,
                        0, 0, 1,  0,
                        0, 0, 0,  1
                }};

                /**
                 * 3x3 采样偏移（9 个采样点）
                 * 
                 * 用于输入滤波的采样模式
                 */
                constexpr float2 sampleOffsets[9] = {
                        { -1.0f, -1.0f }, {  0.0f, -1.0f }, {  1.0f, -1.0f }, { -1.0f,  0.0f },
                        {  0.0f,  0.0f },
                        {  1.0f,  0.0f }, { -1.0f,  1.0f }, {  0.0f,  1.0f }, {  1.0f,  1.0f },
                };

                /**
                 * 子像素偏移（用于上采样模式，4 个子像素）
                 * 
                 * 在 2x 上采样模式下，每个像素被分成 4 个子像素
                 */
                constexpr float2 subSampleOffsets[4] = {
                        { -0.25f,  0.25f },
                        {  0.25f,  0.25f },
                        {  0.25f, -0.25f },
                        { -0.25f, -0.25f }
                };

                /**
                 * Lanczos 滤波函数
                 * 
                 * 用于计算采样权重，减少重投影错误。
                 * Lanczos 滤波器提供良好的频率响应和较低的振铃伪影。
                 */
                UTILS_UNUSED
                auto const lanczos = [](float const x, float const a) -> float {
                    if (x <= std::numeric_limits<float>::epsilon()) {
                        return 1.0f;
                    }
                    if (std::abs(x) <= a) {
                        return (a * std::sin(f::PI * x) * std::sin(f::PI * x / a))
                               / ((f::PI * f::PI) * (x * x));
                    }
                    return 0.0f;
                };

                /**
                 * 计算滤波权重
                 * 
                 * 根据抖动位置和采样偏移计算每个采样点的权重
                 */
                float const filterWidth = std::clamp(taaOptions.filterWidth, 1.0f, 2.0f);
                float4 sum = 0.0;
                float4 weights[9];

                /**
                 * 计算每个采样点的权重（使用 Lanczos 滤波）
                 * 
                 * 注意：此循环不会被向量化（可能是因为三角函数），所以不需要展开
                 */
                UTILS_NOUNROLL
                for (size_t i = 0; i < 9; i++) {
                    float2 const o = sampleOffsets[i];
                    /**
                     * 如果启用上采样，为每个子像素计算权重
                     */
                    for (size_t j = 0; j < 4; j++) {
                        float2 const subPixelOffset = taaOptions.upscaling ? subSampleOffsets[j] : float2{ 0 };
                        /**
                         * 计算采样偏移相对于抖动的距离
                         */
                        float2 const d = (o - (current.jitter - subPixelOffset)) / filterWidth;
                        /**
                         * 使用 Lanczos 滤波计算权重
                         */
                        weights[i][j] = lanczos(length(d), filterWidth);
                    }
                    sum += weights[i];
                }
                /**
                 * 归一化权重（确保所有权重之和为 1）
                 */
                for (auto& w : weights) {
                    w /= sum;
                }

                /**
                 * 获取渲染资源
                 */
                auto out = resources.getRenderPassInfo();
                auto color = resources.getTexture(data.color);      // 当前帧颜色
                auto depth = resources.getTexture(data.depth);      // 深度缓冲
                auto history = resources.getTexture(data.history);  // 历史帧颜色
                auto const& material = getPostProcessMaterial("taa");

                /**
                 * 选择材质变体（透明或不透明）
                 */
                PostProcessVariant const variant = colorGradingConfig.translucent ?
                        PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE;

                FMaterial const* const ma = material.getMaterial(mEngine, variant);

                /**
                 * 获取材质实例并设置参数
                 */
                FMaterialInstance* mi = getMaterialInstance(ma);
                /**
                 * 设置当前帧颜色（最近邻采样，保持清晰度）
                 */
                mi->setParameter("color",  color, SamplerParams{});  // 当前帧颜色（最近邻采样）
                /**
                 * 设置深度缓冲（最近邻采样）
                 */
                mi->setParameter("depth",  depth, SamplerParams{});  // 深度缓冲（最近邻采样）
                /**
                 * 设置反馈系数（混合权重，控制历史帧的贡献）
                 */
                mi->setParameter("alpha", taaOptions.feedback);     // 反馈系数（混合权重）
                /**
                 * 设置历史帧纹理（线性滤波，平滑时间累积）
                 */
                mi->setParameter("history", history, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,      // 历史帧使用线性滤波
                        .filterMin = SamplerMinFilter::LINEAR
                });
                /**
                 * 设置 Lanczos 滤波权重
                 */
                mi->setParameter("filterWeights",  weights, 9);      // Lanczos 滤波权重
                /**
                 * 设置当前帧的抖动偏移
                 */
                mi->setParameter("jitter",  current.jitter);        // 当前帧的抖动偏移
                /**
                 * 设置重投影矩阵
                 * 
                 * 将历史帧的屏幕空间坐标重投影到当前帧的屏幕空间。
                 * 通过投影矩阵的逆变换实现。
                 */
                mi->setParameter("reprojection",
                        mat4f{ historyProjection * inverse(current.projection) } *
                        normalizedToClip);

                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 如果使用颜色分级子通道，设置子通道掩码
                 */
                if (colorGradingConfig.asSubpass) {
                    out.params.subpassMask = 1;
                }
                /**
                 * 获取管道状态并开始渲染
                 */
                auto const pipeline = getPipelineState(ma, variant);

                driver.beginRenderPass(out.target, out.params);
                /**
                 * 绘制全屏四边形
                 */
                driver.draw(pipeline, mFullScreenQuadRph, 0, 3, 1);
                /**
                 * 如果使用颜色分级子通道，执行子通道
                 */
                if (colorGradingConfig.asSubpass) {
                    colorGradingSubpass(driver, colorGradingConfig);
                }
                driver.endRenderPass();
                unbindAllDescriptorSets(driver);
            });

    /**
     * 选择输出（如果使用颜色分级子通道，使用色调映射输出）
     */
    input = colorGradingConfig.asSubpass ? taaPass->tonemappedOutput : taaPass->output;
    auto const history = input;

    // optional sharpen pass from FSR1
    /**
     * 可选的锐化通道（来自 FSR1 的 RCAS）
     * 
     * 如果锐度 > 0，应用 RCAS 锐化以提高图像清晰度
     */
    if (taaOptions.sharpness > 0.0f) {
        input = rcas(fg, taaOptions.sharpness,
                input, fg.getDescriptor(input),
                colorGradingConfig.translucent ? RcasMode::ALPHA_PASSTHROUGH : RcasMode::OPAQUE);
    }

    /**
     * 导出 TAA 历史数据
     * 
     * 将当前帧的输出保存为下一帧的历史数据。
     * 使用 sideEffect 确保此通道不会被优化掉。
     */
    struct ExportColorHistoryData {
        FrameGraphId<FrameGraphTexture> color;
    };
    fg.addPass<ExportColorHistoryData>("Export TAA history",
            [&](FrameGraph::Builder& builder, auto& data) {
                // We need to use sideEffect here to ensure this pass won't be culled.
                // The "output" of this pass is going to be used during the next frame as
                // an "import".
                /**
                 * 使用 sideEffect 确保此通道不会被优化掉
                 * 
                 * 此通道的"输出"将在下一帧作为"导入"使用
                 */
                builder.sideEffect();
                data.color = builder.sample(history); // FIXME: an access must be declared for detach(), why?
            }, [&current](FrameGraphResources const& resources, auto const& data, auto&) {
                /**
                 * 分离资源并保存到当前帧历史
                 */
                resources.detach(data.color, &current.color, &current.desc);
            });

    return input;
}

/**
 * RCAS（Robust Contrast Adaptive Sharpening）锐化通道
 * 
 * 应用 FSR1 的 RCAS 锐化算法，这是一种自适应锐化技术，能够增强图像边缘
 * 的对比度而不过度锐化平坦区域。
 * 
 * @param fg 帧图引用
 * @param sharpness 锐化强度（0.0 到 2.0，2.0 为最强）
 * @param input 输入纹理 ID
 * @param outDesc 输出纹理描述符
 * @param mode RCAS 模式（OPAQUE、TRANSLUCENT 或 BLENDED）
 * @return 锐化后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理
 * 2. 设置 FSR 锐化参数
 * 3. 如果使用 BLENDED 模式，配置混合状态
 * 4. 渲染全屏四边形应用锐化
 * 
 * 注意：
 * - RCAS 使用 texelFetch 进行精确采样，不依赖采样器过滤
 * - 支持透明和混合模式，用于不同的渲染场景
 * - 锐度参数会被转换为 FSR 内部格式（2.0 - 2.0 * sharpness）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::rcas(
        FrameGraph& fg,
        float const sharpness,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphTexture::Descriptor const& outDesc,
        RcasMode const mode) {

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    auto const& ppFsrRcas = fg.addPass<QuadBlitData>("FidelityFX FSR1 Rcas",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("FFX FSR1 Rcas output", outDesc);
                data.output = builder.declareRenderPass(data.output);
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理资源和渲染通道信息
                 */
                auto const input = resources.getTexture(data.input);
                auto const out = resources.getRenderPassInfo();
                auto const& outputDesc = resources.getDescriptor(data.input);

                /**
                 * 根据模式选择变体
                 */
                PostProcessVariant const variant = mode == RcasMode::OPAQUE ?
                        PostProcessVariant::OPAQUE : PostProcessVariant::TRANSLUCENT;

                /**
                 * 获取 RCAS 材质
                 */
                auto const& material = getPostProcessMaterial("fsr_rcas");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material, variant);

                /**
                 * 设置 FSR 锐化参数
                 * 
                 * FSR 内部格式：2.0 - 2.0 * sharpness
                 * sharpness 范围 [0, 2]，映射到 FSR 的 [2, -2]
                 */
                FSRUniforms uniforms;
                FSR_SharpeningSetup(&uniforms, { .sharpness = 2.0f - 2.0f * sharpness });
                mi->setParameter("RcasCon", uniforms.RcasCon);
                /**
                 * 设置颜色纹理（使用 texelFetch，不需要采样参数）
                 */
                mi->setParameter("color", input, SamplerParams{}); // uses texelFetch
                /**
                 * 设置分辨率参数（宽度、高度、倒数）
                 */
                mi->setParameter("resolution", float4{
                        outputDesc.width, outputDesc.height,
                        1.0f / outputDesc.width, 1.0f / outputDesc.height });
                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取管道状态
                 */
                auto pipeline = getPipelineState(material.getMaterial(mEngine), variant);
                /**
                 * 如果使用混合模式，配置混合函数
                 */
                if (mode == RcasMode::BLENDED) {
                    pipeline.rasterState.blendFunctionSrcRGB = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionDstRGB = BlendFunction::ONE_MINUS_SRC_ALPHA;
                    pipeline.rasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_ALPHA;
                }
                /**
                 * 渲染全屏四边形
                 */
                renderFullScreenQuad(out, pipeline, driver);
                unbindAllDescriptorSets(driver);
            });

    return ppFsrRcas->output;
}

/**
 * 上采样分发函数
 * 
 * 根据质量级别选择合适的上采样算法。支持多种上采样技术：
 * - LOW: 双线性上采样（简单快速）
 * - MEDIUM: SGSR1（Samsung 的空间上采样）
 * - HIGH/ULTRA: FSR1（AMD FidelityFX 空间放大）
 * 
 * @param fg 帧图引用
 * @param translucent 是否为半透明渲染
 * @param sourceHasLuminance 源纹理是否包含亮度信息
 * @param dsrOptions 动态分辨率选项（包含质量级别、锐度等）
 * @param input 输入纹理 ID
 * @param vp 视口信息
 * @param outDesc 输出纹理描述符
 * @param filter 采样器放大过滤模式
 * @return 上采样后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 检查输入是否为子资源（不支持）
 * 2. 如果是半透明，降级到低质量（FSR 和 SGSR 不支持 Alpha 通道）
 * 3. 根据质量级别调用相应的上采样函数
 * 
 * 注意：
 * - 不支持子资源输入（必须是基础纹理）
 * - 半透明渲染会自动降级到双线性上采样
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::upscale(FrameGraph& fg, bool const translucent,
    bool sourceHasLuminance, DynamicResolutionOptions dsrOptions,
    FrameGraphId<FrameGraphTexture> const input, filament::Viewport const& vp,
    FrameGraphTexture::Descriptor const& outDesc, SamplerMagFilter filter) noexcept {
    // The code below cannot handle sub-resources
    /**
     * 检查输入是否为子资源（不支持）
     */
    assert_invariant(fg.getSubResourceDescriptor(input).layer == 0);
    assert_invariant(fg.getSubResourceDescriptor(input).level == 0);

    /**
     * 如果是半透明，降级到低质量
     * 
     * FSR 和 SGSR 目前不支持源 Alpha 通道
     */
    if (const bool lowQualityFallback = translucent; lowQualityFallback) {
        // FidelityFX-FSR nor SGSR support the source alpha channel currently
        dsrOptions.quality = QualityLevel::LOW;
    }

    /**
     * 根据质量级别选择上采样算法
     */
    if (dsrOptions.quality == QualityLevel::LOW) {
        return upscaleBilinear(fg, translucent, dsrOptions, input, vp, outDesc, filter);
    }
    if (dsrOptions.quality == QualityLevel::MEDIUM) {
        return upscaleSGSR1(fg, sourceHasLuminance, dsrOptions, input, vp, outDesc);
    }
    return upscaleFSR1(fg, dsrOptions, input, vp, outDesc);
}

/**
 * 双线性上采样通道
 * 
 * 使用双线性插值进行简单的上采样。这是最快但质量最低的上采样方法。
 * 还负责混合结果（如果是半透明），避免额外的混合通道。
 * 
 * @param fg 帧图引用
 * @param translucent 是否为半透明渲染
 * @param dsrOptions 动态分辨率选项
 * @param input 输入纹理 ID
 * @param vp 视口信息
 * @param outDesc 输出纹理描述符
 * @param filter 采样器放大过滤模式
 * @return 上采样后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理
 * 2. 如果使用锐化，先执行双线性上采样
 * 3. 如果锐度 > 0，应用 RCAS 锐化
 * 
 * 注意：
 * - upscaleBilinear 负责上采样和混合（如果是半透明且不使用锐化）
 * - 混合必须在上采样或 RCAS 通道中进行（取最后执行的）
 * - 如果使用锐化，混合在 RCAS 通道中进行
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::upscaleBilinear(FrameGraph& fg,
        bool const translucent,
        DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> const input,
        filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
        SamplerMagFilter filter) noexcept {

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    // upscaleBilinear is responsible for upscaling AND blending the result (so we don't
    // have to do it with an extra pass). Blending must then happen either during the upsacling
    // or during the RCAS pass, whichever is last
    /**
     * 确定是否在此通道中混合
     * 
     * 如果是半透明且不使用锐化，在此通道中混合
     * 如果使用锐化，混合在 RCAS 通道中进行
     */
    bool const blended = translucent && (dsrOptions.sharpness == 0.0f);

    /**
     * 双线性上采样通道
     */
    auto const& ppQuadBlit = fg.addPass<QuadBlitData>(dsrOptions.enabled ? "upscaling" : "compositing",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("upscaled output", outDesc);
                data.output = builder.write(data.output, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(builder.getName(data.output), {
                        .attachments = { .color = { data.output } },
                        .clearFlags = TargetBufferFlags::DEPTH });
            },
            [this, blended, vp, filter](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理资源
                 */
                auto color = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);

                // --------------------------------------------------------------------------------
                // set uniforms

                /**
                 * 获取低质量 blit 材质
                 */
                auto& material = getPostProcessMaterial("blitLow");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material);
                /**
                 * 设置颜色纹理和过滤模式
                 */
                mi->setParameter("color", color, SamplerParams{
                    .filterMag = filter
                });

                /**
                 * 设置细节级别（LOD）
                 */
                mi->setParameter("levelOfDetail", 0.0f);

                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left)   / inputDesc.width,
                        float(vp.bottom) / inputDesc.height,
                        float(vp.width)  / inputDesc.width,
                        float(vp.height) / inputDesc.height
                });
                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                auto out = resources.getRenderPassInfo();

                /**
                 * 获取管道状态
                 */
                auto pipeline = getPipelineState(material.getMaterial(mEngine));
                /**
                 * 如果使用混合模式，配置混合函数
                 */
                if (blended) {
                    pipeline.rasterState.blendFunctionSrcRGB = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionDstRGB = BlendFunction::ONE_MINUS_SRC_ALPHA;
                    pipeline.rasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_ALPHA;
                }

                /**
                 * 渲染全屏四边形
                 */
                renderFullScreenQuad(out, pipeline, driver);
                unbindAllDescriptorSets(driver);
            });

    auto output = ppQuadBlit->output;

    // if we had to take the low quality fallback, we still do the "sharpen pass"
    /**
     * 如果使用了低质量降级，仍然执行锐化通道
     * 
     * 即使降级到双线性上采样，如果锐度 > 0，仍然应用 RCAS 锐化
     */
    if (dsrOptions.sharpness > 0.0f) {
        output = rcas(fg, dsrOptions.sharpness, output, outDesc,
                translucent ? RcasMode::BLENDED : RcasMode::OPAQUE);
    }

    // we rely on automatic culling of unused render passes
    /**
     * 返回输出纹理（依赖自动剔除未使用的渲染通道）
     */
    return output;
}

/**
 * SGSR1（Samsung GPU Spatial Reconstruction）上采样通道
 * 
 * 使用 Samsung 的 SGSR1 算法进行空间上采样。这是一种中等质量的上采样方法，
 * 适合中等质量级别。
 * 
 * @param fg 帧图引用
 * @param sourceHasLuminance 源纹理是否包含亮度信息（影响使用的变体）
 * @param dsrOptions 动态分辨率选项
 * @param input 输入纹理 ID
 * @param vp 视口信息
 * @param outDesc 输出纹理描述符
 * @return 上采样后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理
 * 2. 根据源纹理是否包含亮度选择变体
 * 3. 设置视口和分辨率参数
 * 4. 渲染全屏四边形应用 SGSR1 上采样
 * 
 * 注意：
 * - SGSR 文档未明确说明应使用 LINEAR 还是 NEAREST 过滤
 * - 示例代码使用 NEAREST，但这似乎不正确，因为 LERP 模式将不是真正的 LERP
 * - 这里使用 LINEAR 过滤，以获得更好的插值效果
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::upscaleSGSR1(FrameGraph& fg, bool sourceHasLuminance,
    DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> const input,
    filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc) noexcept {

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * SGSR1 上采样通道
     */
    auto const& ppQuadBlit = fg.addPass<QuadBlitData>(dsrOptions.enabled ? "upscaling" : "compositing",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("upscaled output", outDesc);
                data.output = builder.write(data.output, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(builder.getName(data.output), {
                        .attachments = { .color = { data.output } },
                        .clearFlags = TargetBufferFlags::DEPTH });
            },
            [this, vp, sourceHasLuminance](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 获取纹理资源
                 */
                auto color = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);

                // --------------------------------------------------------------------------------
                // set uniforms

                /**
                 * 获取 SGSR1 材质
                 */
                auto const& material = getPostProcessMaterial("sgsr1");

                /**
                 * 根据源纹理是否包含亮度选择变体
                 */
                PostProcessVariant const variant = sourceHasLuminance ?
                        PostProcessVariant::TRANSLUCENT : PostProcessVariant::OPAQUE;

                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material, variant);

                /**
                 * 设置颜色纹理
                 * 
                 * SGSR 文档未明确说明应使用 LINEAR 还是 NEAREST 过滤。
                 * 示例代码使用 NEAREST，但这似乎不正确，因为这意味着 LERP 模式
                 * 将不是真正的 LERP，非边缘区域将使用 NEAREST 采样。
                 * 这里使用 LINEAR 过滤以获得更好的插值效果。
                 */
                mi->setParameter("color", color, SamplerParams{
                    // The SGSR documentation doesn't clarify if LINEAR or NEAREST should be used. The
                    // sample code uses NEAREST, but that doesn't seem right, since it would mean the
                    // LERP mode would not be a LERP, and the non-edges would be sampled as NEAREST.
                    .filterMag = SamplerMagFilter::LINEAR
                });

                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left)   / inputDesc.width,
                        float(vp.bottom) / inputDesc.height,
                        float(vp.width)  / inputDesc.width,
                        float(vp.height) / inputDesc.height
                });

                /**
                 * 设置视口信息（像素大小和分辨率）
                 */
                mi->setParameter("viewportInfo", float4{
                        1.0f / inputDesc.width,
                        1.0f / inputDesc.height,
                        float(inputDesc.width),
                        float(inputDesc.height)
                });

                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取渲染通道信息并渲染
                 */
                auto const out = resources.getRenderPassInfo();
                commitAndRenderFullScreenQuad(driver, out, mi, variant);
                unbindAllDescriptorSets(driver);
            });

    auto output = ppQuadBlit->output;

    // we rely on automatic culling of unused render passes
    /**
     * 返回输出纹理（依赖自动剔除未使用的渲染通道）
     */
    return output;
}

/**
 * FSR1（FidelityFX Super Resolution 1）上采样通道
 * 
 * 使用 AMD FidelityFX FSR1 的 EASU（Edge Adaptive Spatial Upsampling）算法
 * 进行高质量上采样。这是最高质量的上采样方法，适合 HIGH 和 ULTRA 质量级别。
 * 
 * @param fg 帧图引用
 * @param dsrOptions 动态分辨率选项
 * @param input 输入纹理 ID
 * @param vp 视口信息
 * @param outDesc 输出纹理描述符
 * @return 上采样后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 确定是否使用两通道 EASU（某些后端/质量级别的变通方案）
 * 2. 创建输出纹理和可选的深度缓冲区
 * 3. 设置 FSR EASU 参数（根据输入/输出尺寸）
 * 4. 执行 EASU 上采样（单通道或两通道）
 * 5. 可选：应用 RCAS 锐化
 * 
 * 注意：
 * - 支持单通道和两通道 EASU（根据后端和变通方案）
 * - 两通道模式使用深度缓冲区来分离通道
 * - 使用 FSR_ScalingSetup 计算 EASU 常量
 * - 支持移动端优化版本（fsr_easu_mobile）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::upscaleFSR1(FrameGraph& fg,
    DynamicResolutionOptions dsrOptions, FrameGraphId<FrameGraphTexture> const input,
    filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc) noexcept {

    /**
     * 确定是否使用两通道 EASU
     * 
     * 某些后端在中等/高质量级别需要两通道 EASU 作为变通方案
     */
    const bool twoPassesEASU = mWorkaroundSplitEasu &&
            (dsrOptions.quality == QualityLevel::MEDIUM
                || dsrOptions.quality == QualityLevel::HIGH);

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
        FrameGraphId<FrameGraphTexture> depth;
    };

    /**
     * FSR1 EASU 上采样通道
     */
    auto const& ppQuadBlit = fg.addPass<QuadBlitData>(dsrOptions.enabled ? "upscaling" : "compositing",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("upscaled output", outDesc);

                /**
                 * 如果使用两通道 EASU，创建深度缓冲区
                 * 
                 * FIXME: 使用模板缓冲区会更好（带宽更少）
                 */
                if (twoPassesEASU) {
                    // FIXME: it would be better to use the stencil buffer in this case (less bandwidth)
                    data.depth = builder.createTexture("upscaled output depth", {
                        .width = outDesc.width,
                        .height = outDesc.height,
                        .format = TextureFormat::DEPTH16
                    });
                    data.depth = builder.write(data.depth, FrameGraphTexture::Usage::DEPTH_ATTACHMENT);
                }

                /**
                 * 声明输出为颜色附件
                 */
                data.output = builder.write(data.output, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(builder.getName(data.output), {
                        .attachments = { .color = { data.output }, .depth = { data.depth }},
                        .clearFlags = TargetBufferFlags::DEPTH });
            },
            [this, twoPassesEASU, dsrOptions, vp](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * EASU 参数设置辅助函数
                 * 
                 * 使用 FSR_ScalingSetup 计算 EASU 常量（EasuCon0-3），
                 * 这些常量用于 EASU 算法的边缘自适应上采样
                 */
                auto setEasuUniforms = [vp, backend = mEngine.getBackend()](FMaterialInstance* mi,
                        FrameGraphTexture::Descriptor const& inputDesc,
                        FrameGraphTexture::Descriptor const& outputDesc) {
                    FSRUniforms uniforms{};
                    /**
                     * 计算 FSR 缩放参数
                     */
                    FSR_ScalingSetup(&uniforms, {
                            .backend = backend,
                            .input = vp,
                            .inputWidth = inputDesc.width,
                            .inputHeight = inputDesc.height,
                            .outputWidth = outputDesc.width,
                            .outputHeight = outputDesc.height,
                    });
                    /**
                     * 设置 EASU 常量（四个 float4）
                     */
                    mi->setParameter("EasuCon0", uniforms.EasuCon0);
                    mi->setParameter("EasuCon1", uniforms.EasuCon1);
                    mi->setParameter("EasuCon2", uniforms.EasuCon2);
                    mi->setParameter("EasuCon3", uniforms.EasuCon3);
                    /**
                     * 设置纹理尺寸
                     */
                    mi->setParameter("textureSize",
                            float2{ inputDesc.width, inputDesc.height });
                };

                /**
                 * 获取纹理资源和描述符
                 */
                auto color = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);
                auto const& outputDesc = resources.getDescriptor(data.output);

                // --------------------------------------------------------------------------------
                // set uniforms

                /**
                 * 材质指针（两通道和单通道 EASU）
                 */
                PostProcessMaterial const* splitEasuMaterial = nullptr;
                PostProcessMaterial const* easuMaterial = nullptr;

                /**
                 * 如果使用两通道 EASU，设置第一个通道的材质
                 */
                if (twoPassesEASU) {
                    splitEasuMaterial = &getPostProcessMaterial("fsr_easu_mobileF");
                    FMaterialInstance* const mi =
                            getMaterialInstance(mEngine, *splitEasuMaterial);
                    /**
                     * 设置 EASU 参数
                     */
                    setEasuUniforms(mi, inputDesc, outputDesc);
                    /**
                     * 设置颜色纹理（线性过滤）
                     */
                    mi->setParameter("color", color, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR
                    });
                    /**
                     * 设置分辨率参数
                     */
                    mi->setParameter("resolution",
                            float4{ outputDesc.width, outputDesc.height,
                                    1.0f / outputDesc.width, 1.0f / outputDesc.height });
                    /**
                     * 提交并使用材质
                     */
                    mi->commit(driver, getUboManager());
                    mi->use(driver);
                }

                /**
                 * 设置主要 EASU 材质
                 * 
                 * 根据质量级别选择移动端或桌面端版本：
                 * - MEDIUM: fsr_easu_mobile
                 * - HIGH/ULTRA: fsr_easu
                 */
                { // just a scope to not leak local variables
                    const std::string_view blitterNames[2] = { "fsr_easu_mobile", "fsr_easu" };
                    unsigned const index = std::min(1u, unsigned(dsrOptions.quality) - 2);
                    easuMaterial = &getPostProcessMaterial(blitterNames[index]);
                    FMaterialInstance* const mi =
                            getMaterialInstance(mEngine, *easuMaterial);

                    /**
                     * 设置 EASU 参数
                     */
                    setEasuUniforms(mi, inputDesc, outputDesc);

                    /**
                     * 设置颜色纹理（线性过滤）
                     */
                    mi->setParameter("color", color, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR
                    });

                    /**
                     * 设置分辨率参数
                     */
                    mi->setParameter("resolution",
                            float4{outputDesc.width, outputDesc.height, 1.0f / outputDesc.width,
                                1.0f / outputDesc.height});

                    /**
                     * 设置视口参数（归一化坐标）
                     */
                    mi->setParameter("viewport", float4{
                            float(vp.left)   / inputDesc.width,
                            float(vp.bottom) / inputDesc.height,
                            float(vp.width)  / inputDesc.width,
                            float(vp.height) / inputDesc.height
                    });
                    /**
                     * 提交并使用材质
                     */
                    mi->commit(driver, getUboManager());
                    mi->use(driver);
                }

                // --------------------------------------------------------------------------------
                // render pass with draw calls

                /**
                 * 获取渲染通道信息
                 */
                auto out = resources.getRenderPassInfo();

                /**
                 * 如果使用两通道 EASU，执行两次绘制
                 * 
                 * 第二个通道使用深度测试（NE - 不等于）来跳过已处理的区域
                 */
                if (UTILS_UNLIKELY(twoPassesEASU)) {
                    auto pipeline0 = getPipelineState(splitEasuMaterial->getMaterial(mEngine));
                    auto pipeline1 = getPipelineState(easuMaterial->getMaterial(mEngine));
                    /**
                     * 第二个通道使用深度测试来分离区域
                     */
                    pipeline1.rasterState.depthFunc = SamplerCompareFunc::NE;
                    driver.beginRenderPass(out.target, out.params);
                    /**
                     * 绘制第一个通道
                     */
                    driver.draw(pipeline0, mFullScreenQuadRph, 0, 3, 1);
                    /**
                     * 绘制第二个通道
                     */
                    driver.draw(pipeline1, mFullScreenQuadRph, 0, 3, 1);
                    driver.endRenderPass();
                } else {
                    /**
                     * 单通道 EASU，直接渲染
                     */
                    auto pipeline = getPipelineState(easuMaterial->getMaterial(mEngine));
                    renderFullScreenQuad(out, pipeline, driver);
                }
                unbindAllDescriptorSets(driver);
            });

    auto output = ppQuadBlit->output;
    /**
     * 如果锐度 > 0，应用 RCAS 锐化
     */
    if (dsrOptions.sharpness > 0.0f) {
        output = rcas(fg, dsrOptions.sharpness, output, outDesc, RcasMode::OPAQUE);
    }

    // we rely on automatic culling of unused render passes
    /**
     * 返回输出纹理（依赖自动剔除未使用的渲染通道）
     */
    return output;
}

/**
 * 位块传输（Blit）通道
 * 
 * 将输入纹理复制到输出纹理，支持缩放、裁剪、Mipmap 级别选择和数组层选择。
 * 这是一个通用的纹理复制/缩放通道，用于后处理管线中的各种场景。
 * 
 * @param fg 帧图引用
 * @param translucent 是否为半透明渲染（影响混合模式）
 * @param input 输入纹理 ID
 * @param vp 视口信息（用于裁剪和缩放）
 * @param outDesc 输出纹理描述符
 * @param filterMag 放大过滤模式
 * @param filterMin 缩小过滤模式
 * @return 输出纹理 ID
 * 
 * 处理流程：
 * 1. 获取输入的子资源信息（层和 Mipmap 级别）
 * 2. 根据是否为数组纹理选择材质（blitArray 或 blitLow）
 * 3. 设置采样器参数和视口
 * 4. 如果使用半透明，配置混合函数
 * 5. 渲染全屏四边形执行位块传输
 * 
 * 注意：
 * - 支持纹理数组（通过 layer 参数）
 * - 支持 Mipmap 级别选择（通过 levelOfDetail 参数）
 * - 支持自定义采样过滤模式
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::blit(FrameGraph& fg, bool const translucent,
        FrameGraphId<FrameGraphTexture> const input,
        filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
        SamplerMagFilter filterMag,
        SamplerMinFilter filterMin) noexcept {

    /**
     * 获取输入的子资源信息（层和 Mipmap 级别）
     */
    uint32_t const layer = fg.getSubResourceDescriptor(input).layer;
    float const levelOfDetail = fg.getSubResourceDescriptor(input).level;

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * 位块传输通道
     */
    auto const& ppQuadBlit = fg.addPass<QuadBlitData>("blitting",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("blit output", outDesc);
                /**
                 * 声明输出为颜色附件
                 */
                data.output = builder.write(data.output,
                        FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(builder.getName(data.output), {
                        .attachments = { .color = { data.output }},
                        .clearFlags = TargetBufferFlags::DEPTH });
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取纹理资源和描述符
                 */
                auto color = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);
                auto out = resources.getRenderPassInfo();

                // --------------------------------------------------------------------------------
                // set uniforms

                /**
                 * 根据是否为数组纹理选择材质
                 */
                PostProcessMaterial const& material =
                        getPostProcessMaterial(layer ? "blitArray" : "blitLow");
                FMaterial const* const ma = material.getMaterial(mEngine);
                auto* mi = getMaterialInstance(ma);
                /**
                 * 设置颜色纹理和过滤模式
                 */
                mi->setParameter("color", color, SamplerParams{
                        .filterMag = filterMag,
                        .filterMin = filterMin
                });
                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left)   / inputDesc.width,
                        float(vp.bottom) / inputDesc.height,
                        float(vp.width)  / inputDesc.width,
                        float(vp.height) / inputDesc.height
                });
                /**
                 * 设置细节级别（LOD）
                 */
                mi->setParameter("levelOfDetail", levelOfDetail);
                /**
                 * 如果是数组纹理，设置层索引
                 */
                if (layer) {
                    mi->setParameter("layerIndex", layer);
                }
                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取管道状态
                 */
                auto pipeline = getPipelineState(ma);
                /**
                 * 如果使用半透明，配置混合函数
                 */
                if (translucent) {
                    pipeline.rasterState.blendFunctionSrcRGB = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionDstRGB = BlendFunction::ONE_MINUS_SRC_ALPHA;
                    pipeline.rasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_ALPHA;
                }
                /**
                 * 渲染全屏四边形
                 */
                renderFullScreenQuad(out, pipeline, driver);
                unbindAllDescriptorSets(driver);
            });

    return ppQuadBlit->output;
}

/**
 * 深度位块传输（Depth Blit）通道
 * 
 * 复制深度纹理。优先使用硬件位块传输（如果支持），否则使用基于着色器的复制。
 * 这用于避免深度缓冲区的反馈循环问题（某些后端不允许同时作为纹理和附件使用）。
 * 
 * @param fg 帧图引用
 * @param input 输入深度纹理 ID
 * @return 复制的深度纹理 ID
 * 
 * 处理流程：
 * 1. 检查硬件是否支持深度/模板位块传输
 * 2. 如果支持，使用硬件位块传输（仅复制基础级别）
 * 3. 如果不支持，使用基于着色器的深度复制
 * 
 * 注意：
 * - 硬件位块传输只复制基础 Mipmap 级别
 * - 源和目标必须具有相同的格式和尺寸（通过构造保证）
 * - 用于避免某些后端的反馈循环限制
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::blitDepth(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input) noexcept {
    /**
     * 获取输入描述符并创建全屏视口
     */
    auto const& inputDesc = fg.getDescriptor(input);
    filament::Viewport const vp = {0, 0, inputDesc.width, inputDesc.height};
    /**
     * 检查硬件是否支持深度/模板位块传输
     */
    bool const hardwareBlitSupported =
            mEngine.getDriverApi().isDepthStencilBlitSupported(inputDesc.format);

    struct BlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * 如果硬件支持，使用硬件位块传输
     */
    if (hardwareBlitSupported) {
        auto const& depthPass = fg.addPass<BlitData>(
                "Depth Blit",
                [&](FrameGraph::Builder& builder, auto& data) {
                    /**
                     * 声明输入为位块传输源
                     */
                    data.input = builder.read(input, FrameGraphTexture::Usage::BLIT_SRC);

                    /**
                     * 获取输入描述符并仅复制基础级别
                     */
                    auto desc = builder.getDescriptor(data.input);
                    desc.levels = 1;// only copy the base level

                    // create a new buffer for the copy
                    /**
                     * 创建输出纹理用于复制
                     */
                    data.output = builder.createTexture("depth blit output", desc);

                    // output is an attachment
                    /**
                     * 声明输出为位块传输目标
                     */
                    data.output = builder.write(data.output, FrameGraphTexture::Usage::BLIT_DST);
                },
                [=](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                    /**
                     * 获取源和目标纹理及子资源描述符
                     */
                    auto const& src = resources.getTexture(data.input);
                    auto const& dst = resources.getTexture(data.output);
                    auto const& srcSubDesc = resources.getSubResourceDescriptor(data.input);
                    auto const& dstSubDesc = resources.getSubResourceDescriptor(data.output);
                    auto const& desc = resources.getDescriptor(data.output);
                    /**
                     * 验证采样数匹配
                     */
                    assert_invariant(desc.samples == resources.getDescriptor(data.input).samples);
                    // here we can guarantee that src and dst format and size match, by
                    // construction.
                    /**
                     * 执行硬件位块传输
                     * 
                     * 从源纹理的子资源复制到目标纹理的子资源
                     */
                    driver.blit(
                            dst, dstSubDesc.level, dstSubDesc.layer, { 0, 0 },
                            src, srcSubDesc.level, srcSubDesc.layer, { 0, 0 },
                            { desc.width, desc.height });
                });
        return depthPass->output;
    }
    // Otherwise, we would do a shader-based blit.
    /**
     * 如果不支持硬件位块传输，使用基于着色器的深度复制
     */
    auto const& ppQuadBlit = fg.addPass<BlitData>(
            "Depth Blit (Shader)",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                // Note that this is a same size/format blit.
                /**
                 * 注意：这是相同尺寸/格式的位块传输
                 * 
                 * 输出描述符与输入相同
                 */
                auto const& outputDesc = inputDesc;
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("depth blit output", outputDesc);
                /**
                 * 声明输出为深度附件
                 */
                data.output =
                        builder.write(data.output, FrameGraphTexture::Usage::DEPTH_ATTACHMENT);
                /**
                 * 声明渲染通道（仅深度附件）
                 */
                builder.declareRenderPass(builder.getName(data.output),
                        {.attachments = {.depth = {data.output}}});
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定结构描述符堆（用于深度采样）
                 */
                getStructureDescriptorSet().bind(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取深度纹理和描述符
                 */
                auto depth = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);
                auto const out = resources.getRenderPassInfo();

                // --------------------------------------------------------------------------------
                // set uniforms
                /**
                 * 获取深度位块传输材质
                 */
                PostProcessMaterial const& material = getPostProcessMaterial("blitDepth");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material);
                /**
                 * 设置深度纹理（最近邻过滤，保持精确的深度值）
                 */
                mi->setParameter("depth", depth, SamplerParams {
                            .filterMag = SamplerMagFilter::NEAREST,
                            .filterMin = SamplerMinFilter::NEAREST,
                        });
                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport",
                        float4{float(vp.left) / inputDesc.width,
                            float(vp.bottom) / inputDesc.height, float(vp.width) / inputDesc.width,
                            float(vp.height) / inputDesc.height});
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    return ppQuadBlit->output;
}

/**
 * 解析（Resolve）通道
 * 
 * 将多采样（MSAA）纹理解析为单采样纹理。如果输入不是 MSAA 纹理，直接返回输入。
 * 对于深度/模板格式，如果后端不支持硬件解析，则使用基于着色器的解析。
 * 
 * @param fg 帧图引用
 * @param outputBufferName 输出缓冲区名称
 * @param input 输入 MSAA 纹理 ID
 * @param outDesc 输出纹理描述符（会被修改以匹配输入）
 * @return 解析后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 检查输入是否为 MSAA 纹理（samples > 1）
 * 2. 如果是深度格式且后端不支持硬件解析，使用基于着色器的解析
 * 3. 创建输出纹理（单采样，格式和尺寸与输入相同）
 * 4. 使用硬件解析将多采样纹理解析为单采样
 * 
 * 注意：
 * - 目前不支持模板解析
 * - 源和目标必须具有相同的格式和尺寸
 * - 源必须是多采样（samples > 1），目标必须是单采样（samples <= 1）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::resolve(FrameGraph& fg,
        utils::StaticString outputBufferName, FrameGraphId<FrameGraphTexture> const input,
        FrameGraphTexture::Descriptor outDesc) noexcept {

    // Don't do anything if we're not a MSAA buffer
    /**
     * 如果不是 MSAA 缓冲区，直接返回输入
     */
    auto const& inDesc = fg.getDescriptor(input);
    if (inDesc.samples <= 1) {
        return input;
    }

    // The Metal / Vulkan backends currently don't support depth/stencil resolve.
    /**
     * Metal/Vulkan 后端目前不支持深度/模板解析
     * 
     * 如果是深度格式且后端不支持硬件解析，使用基于着色器的解析
     */
    if (isDepthFormat(inDesc.format) && (!mEngine.getDriverApi().isDepthStencilResolveSupported())) {
        return resolveDepth(fg, outputBufferName, input, outDesc);
    }

    /**
     * 设置输出描述符（与输入相同，但为单采样）
     */
    outDesc.width = inDesc.width;
    outDesc.height = inDesc.height;
    outDesc.format = inDesc.format;
    outDesc.samples = 0;

    struct ResolveData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * 解析通道
     */
    auto const& ppResolve = fg.addPass<ResolveData>("resolve",
            [&](FrameGraph::Builder& builder, auto& data) {
                // we currently don't support stencil resolve.
                /**
                 * 目前不支持模板解析
                 */
                assert_invariant(!isStencilFormat(inDesc.format));

                /**
                 * 声明输入为位块传输源
                 */
                data.input = builder.read(input, FrameGraphTexture::Usage::BLIT_SRC);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture(outputBufferName, outDesc);
                /**
                 * 声明输出为位块传输目标
                 */
                data.output = builder.write(data.output, FrameGraphTexture::Usage::BLIT_DST);
            },
            [](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 获取源和目标纹理及子资源描述符
                 */
                auto const& src = resources.getTexture(data.input);
                auto const& dst = resources.getTexture(data.output);
                auto const& srcSubDesc = resources.getSubResourceDescriptor(data.input);
                auto const& dstSubDesc = resources.getSubResourceDescriptor(data.output);
                UTILS_UNUSED_IN_RELEASE auto const& srcDesc = resources.getDescriptor(data.input);
                UTILS_UNUSED_IN_RELEASE auto const& dstDesc = resources.getDescriptor(data.output);
                /**
                 * 验证源和目标有效性
                 */
                assert_invariant(src);
                assert_invariant(dst);
                /**
                 * 验证格式和尺寸匹配
                 */
                assert_invariant(srcDesc.format == dstDesc.format);
                assert_invariant(srcDesc.width == dstDesc.width && srcDesc.height == dstDesc.height);
                /**
                 * 验证采样数（源必须是多采样，目标必须是单采样）
                 */
                assert_invariant(srcDesc.samples > 1 && dstDesc.samples <= 1);
                /**
                 * 执行硬件解析
                 */
                driver.resolve(
                        dst, dstSubDesc.level, dstSubDesc.layer,
                        src, srcSubDesc.level, srcSubDesc.layer);
            });

    return ppResolve->output;
}

/**
 * 深度解析（Depth Resolve）通道
 * 
 * 使用基于着色器的方法将多采样深度纹理解析为单采样纹理。
 * 这用于不支持硬件深度解析的后端（如 Metal/Vulkan）。
 * 
 * @param fg 帧图引用
 * @param outputBufferName 输出缓冲区名称
 * @param input 输入 MSAA 深度纹理 ID
 * @param outDesc 输出纹理描述符（会被修改以匹配输入）
 * @return 解析后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 检查输入是否为 MSAA 纹理
 * 2. 验证输入是深度格式且为基础级别（layer=0, level=0）
 * 3. 创建输出纹理（单采样，格式和尺寸与输入相同）
 * 4. 使用基于着色器的解析材质进行解析
 * 
 * 注意：
 * - 目前不支持模板解析
 * - 只支持基础级别的深度纹理（不支持子资源）
 * - 使用最近邻采样保持深度值的精确性
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::resolveDepth(FrameGraph& fg,
        utils::StaticString outputBufferName, FrameGraphId<FrameGraphTexture> const input,
        FrameGraphTexture::Descriptor outDesc) noexcept {

    // Don't do anything if we're not a MSAA buffer
    /**
     * 如果不是 MSAA 缓冲区，直接返回输入
     */
    auto const& inDesc = fg.getDescriptor(input);
    if (inDesc.samples <= 1) {
        return input;
    }

    /**
     * 验证输入是深度格式且为基础级别
     */
    UTILS_UNUSED_IN_RELEASE auto const& inSubDesc = fg.getSubResourceDescriptor(input);
    assert_invariant(isDepthFormat(inDesc.format));
    assert_invariant(inSubDesc.layer == 0);
    assert_invariant(inSubDesc.level == 0);

    /**
     * 设置输出描述符（与输入相同，但为单采样）
     */
    outDesc.width = inDesc.width;
    outDesc.height = inDesc.height;
    outDesc.format = inDesc.format;
    outDesc.samples = 0;

    struct ResolveData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * 深度解析通道（基于着色器）
     */
    auto const& ppResolve = fg.addPass<ResolveData>("resolveDepth",
            [&](FrameGraph::Builder& builder, auto& data) {
                // we currently don't support stencil resolve
                /**
                 * 目前不支持模板解析
                 */
                assert_invariant(!isStencilFormat(inDesc.format));

                /**
                 * 声明输入资源
                 */
                data.input = builder.sample(input);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture(outputBufferName, outDesc);
                /**
                 * 声明输出为深度附件
                 */
                data.output = builder.write(data.output, FrameGraphTexture::Usage::DEPTH_ATTACHMENT);
                /**
                 * 声明渲染通道（仅深度附件）
                 */
                builder.declareRenderPass(builder.getName(data.output), {
                        .attachments = { .depth = { data.output }},
                        .clearFlags = TargetBufferFlags::DEPTH });
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取输入纹理和解析材质
                 */
                auto const& input = resources.getTexture(data.input);
                auto const& material = getPostProcessMaterial("resolveDepth");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material);
                /**
                 * 设置深度纹理（最近邻采样，保持精确性）
                 */
                mi->setParameter("depth", input, SamplerParams{}); // NEAREST
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, resources.getRenderPassInfo(), mi);
                unbindAllDescriptorSets(driver);
            });

    return ppResolve->output;
}

/**
 * VSM（Variance Shadow Maps）Mipmap 生成通道
 * 
 * 为方差阴影贴图生成 Mipmap 级别。在生成阴影贴图 Mipmap 时，
 * 需要保留 1 纹素的边界，以确保阴影过滤的正确性。
 * 
 * @param fg 帧图引用
 * @param input 输入纹理 ID（VSM 阴影贴图）
 * @param layer 纹理数组层索引
 * @param level 源 Mipmap 级别（将生成 level+1 级别）
 * @param clearColor 清除颜色
 * @return 输入纹理 ID（我们只写入子资源）
 * 
 * 处理流程：
 * 1. 创建目标 Mipmap 级别的子资源
 * 2. 创建源级别的纹理视图
 * 3. 使用剪刀矩形保留 1 纹素边界
 * 4. 渲染生成 Mipmap
 * 
 * 注意：
 * - 清除操作不遵循剪刀矩形（Filament 的限制）
 * - 使用线性过滤生成 Mipmap
 * - 假设输入是正方形纹理（width == height）
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::vsmMipmapPass(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input, uint8_t layer, size_t const level,
        float4 clearColor) noexcept {

    struct VsmMipData {
        FrameGraphId<FrameGraphTexture> in;
    };

    /**
     * VSM Mipmap 生成通道
     */
    auto const& depthMipmapPass = fg.addPass<VsmMipData>("VSM Generate Mipmap Pass",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取输入纹理名称
                 */
                utils::StaticString name = builder.getName(input);
                /**
                 * 声明输入资源
                 */
                data.in = builder.sample(input);

                /**
                 * 创建目标 Mipmap 级别的子资源
                 */
                auto out = builder.createSubresource(data.in, "Mip level", {
                        .level = uint8_t(level + 1), .layer = layer });

                /**
                 * 声明为颜色附件
                 */
                out = builder.write(out, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(name, {
                    .attachments = { .color = { out }},
                    .clearColor = clearColor,
                    .clearFlags = TargetBufferFlags::COLOR
                });
            },
            [=, this](FrameGraphResources const& resources,
                    auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);

                /**
                 * 创建源级别的纹理视图
                 */
                auto in = driver.createTextureView(resources.getTexture(data.in), level, 1);
                auto out = resources.getRenderPassInfo();

                /**
                 * 获取输入描述符并计算目标尺寸
                 */
                auto const& inDesc = resources.getDescriptor(data.in);
                auto width = inDesc.width;
                /**
                 * 验证输入是正方形纹理
                 */
                assert_invariant(width == inDesc.height);
                /**
                 * 计算目标 Mipmap 级别的尺寸
                 */
                int const dim = width >> (level + 1);

                /**
                 * 获取 VSM Mipmap 材质
                 */
                auto& material = getPostProcessMaterial("vsmMipmap");
                FMaterial const* const ma = material.getMaterial(mEngine);

                // When generating shadow map mip levels, we want to preserve the 1 texel border.
                // (note clearing never respects the scissor in Filament)
                /**
                 * 当生成阴影贴图 Mipmap 级别时，我们希望保留 1 纹素边界
                 * 
                 * 注意：清除操作在 Filament 中不遵循剪刀矩形
                 */
                auto const pipeline = getPipelineState(ma);
                /**
                 * 设置剪刀矩形（保留 1 纹素边界）
                 */
                backend::Viewport const scissor = { 1u, 1u, dim - 2u, dim - 2u };

                /**
                 * 获取材质实例并设置参数
                 */
                FMaterialInstance* const mi = getMaterialInstance(ma);
                /**
                 * 设置颜色纹理（线性过滤）
                 */
                mi->setParameter("color", in, SamplerParams{
                        .filterMag = SamplerMagFilter::LINEAR,
                        .filterMin = SamplerMinFilter::LINEAR_MIPMAP_NEAREST
                });
                /**
                 * 设置层索引
                 */
                mi->setParameter("layer", uint32_t(layer));
                /**
                 * 设置 UV 缩放（1 / 目标尺寸）
                 */
                mi->setParameter("uvscale", 1.0f / float(dim));
                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 使用剪刀矩形渲染全屏四边形
                 */
                renderFullScreenQuadWithScissor(out, pipeline, scissor, driver);
                unbindAllDescriptorSets(driver);

                /**
                 * 销毁临时纹理视图
                 * 
                 * `in` 只是 `data.in` 的一个视图
                 */
                driver.destroyTexture(in); // `in` is just a view on `data.in`
            });

    return depthMipmapPass->in;
}

/**
 * 调试阴影级联（Debug Shadow Cascades）通道
 * 
 * 用于调试目的，可视化阴影级联（cascades）的边界。
 * 这对于调试级联阴影贴图（CSM）非常有用。
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID
 * @param depth 深度纹理 ID
 * @return 调试输出纹理 ID
 * 
 * 处理流程：
 * 1. 创建输出纹理（与输入相同尺寸）
 * 2. 采样颜色和深度纹理
 * 3. 使用调试材质渲染级联边界
 * 
 * 注意：
 * - 这是一个调试功能，不应在生产代码中使用
 * - 使用最近邻采样保持清晰度
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::debugShadowCascades(FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> const input,
        FrameGraphId<FrameGraphTexture> const depth) noexcept {

    // new pass for showing the cascades
    /**
     * 用于显示级联的新通道
     */
    struct DebugShadowCascadesData {
        FrameGraphId<FrameGraphTexture> color;
        FrameGraphId<FrameGraphTexture> depth;
        FrameGraphId<FrameGraphTexture> output;
    };
    /**
     * 阴影级联调试通道
     */
    auto const& debugShadowCascadePass = fg.addPass<DebugShadowCascadesData>("ShadowCascades",
            [&](FrameGraph::Builder& builder, auto& data) {
                /**
                 * 获取输入描述符
                 */
                auto const desc = builder.getDescriptor(input);
                /**
                 * 声明输入资源
                 */
                data.color = builder.sample(input);
                data.depth = builder.sample(depth);
                /**
                 * 创建输出纹理
                 */
                data.output = builder.createTexture("Shadow Cascade Debug", desc);
                /**
                 * 声明渲染通道
                 */
                builder.declareRenderPass(data.output);
            },
            [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取纹理资源和渲染通道信息
                 */
                auto color = resources.getTexture(data.color);
                auto depth = resources.getTexture(data.depth);
                auto const out = resources.getRenderPassInfo();
                /**
                 * 获取调试阴影级联材质
                 */
                auto const& material = getPostProcessMaterial("debugShadowCascades");
                FMaterialInstance* const mi =
                        getMaterialInstance(mEngine, material);
                /**
                 * 设置颜色纹理（最近邻采样）
                 */
                mi->setParameter("color",  color, SamplerParams{});  // nearest
                /**
                 * 设置深度纹理（最近邻采样）
                 */
                mi->setParameter("depth",  depth, SamplerParams{});  // nearest
                /**
                 * 提交并渲染全屏四边形
                 */
                commitAndRenderFullScreenQuad(driver, out, mi);
                unbindAllDescriptorSets(driver);
            });

    return debugShadowCascadePass->output;
}

/**
 * 调试组合数组纹理（Debug Combine Array Texture）通道
 * 
 * 用于调试目的，将纹理数组的所有层并排渲染到屏幕上。
 * 这对于可视化纹理数组的内容非常有用。
 * 
 * @param fg 帧图引用
 * @param translucent 是否为半透明渲染
 * @param input 输入纹理数组 ID
 * @param vp 视口信息
 * @param outDesc 输出纹理描述符
 * @param filterMag 放大过滤模式
 * @param filterMin 缩小过滤模式
 * @return 组合后的输出纹理 ID
 * 
 * 处理流程：
 * 1. 验证输入是纹理数组（depth > 1）
 * 2. 创建输出纹理
 * 3. 为每个层设置视口（每个层占据屏幕宽度的 1/depth）
 * 4. 依次渲染所有层（并排显示）
 * 
 * 注意：
 * - 这是一个调试功能，不应在生产代码中使用
 * - 目前不支持子资源（TODO）
 * - 每个层的宽度 = 屏幕宽度 / 层数
 * - 从第二次绘制开始不清除目标缓冲区
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::debugCombineArrayTexture(FrameGraph& fg,
    bool const translucent, FrameGraphId<FrameGraphTexture> const input,
    filament::Viewport const& vp, FrameGraphTexture::Descriptor const& outDesc,
    SamplerMagFilter filterMag,
    SamplerMinFilter filterMin) noexcept {

    /**
     * 获取输入纹理描述符并验证
     */
    auto& inputTextureDesc = fg.getDescriptor(input);
    /**
     * 验证输入是纹理数组（深度 > 1）
     */
    assert_invariant(inputTextureDesc.depth > 1);
    assert_invariant(inputTextureDesc.type == SamplerType::SAMPLER_2D_ARRAY);

    // TODO: add support for sub-resources
    /**
     * 验证输入是基础级别（TODO: 添加子资源支持）
     */
    assert_invariant(fg.getSubResourceDescriptor(input).layer == 0);
    assert_invariant(fg.getSubResourceDescriptor(input).level == 0);

    struct QuadBlitData {
        FrameGraphId<FrameGraphTexture> input;
        FrameGraphId<FrameGraphTexture> output;
    };

    /**
     * 组合数组纹理通道
     */
    auto const& ppQuadBlit = fg.addPass<QuadBlitData>("combining array tex",
        [&](FrameGraph::Builder& builder, auto& data) {
            /**
             * 声明输入资源
             */
            data.input = builder.sample(input);
            /**
             * 创建输出纹理
             */
            data.output = builder.createTexture("upscaled output", outDesc);
            /**
             * 声明输出为颜色附件
             */
            data.output = builder.write(data.output,
                FrameGraphTexture::Usage::COLOR_ATTACHMENT);
            /**
             * 声明渲染通道
             */
            builder.declareRenderPass(builder.getName(data.output), {
                    .attachments = {.color = { data.output }},
                    .clearFlags = TargetBufferFlags::DEPTH });
        },
        [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                /**
                 * 绑定描述符堆
                 */
                bindPostProcessDescriptorSet(driver);
                bindPerRenderableDescriptorSet(driver);
                /**
                 * 获取纹理资源和描述符
                 */
                auto color = resources.getTexture(data.input);
                auto const& inputDesc = resources.getDescriptor(data.input);
                auto out = resources.getRenderPassInfo();

                // --------------------------------------------------------------------------------
                // set uniforms

                /**
                 * 获取数组位块传输材质
                 */
                PostProcessMaterial const& material = getPostProcessMaterial("blitArray");
                FMaterial const* const ma = material.getMaterial(mEngine);
                // It should be ok to not move this getMaterialInstance to inside the loop, since
                // this is a pass meant for debug.
                /**
                 * 获取材质实例（不需要移到循环内，因为这是调试通道）
                 */
                auto* mi = getMaterialInstance(ma);
                /**
                 * 设置颜色纹理和过滤模式
                 */
                mi->setParameter("color", color, SamplerParams{
                        .filterMag = filterMag,
                        .filterMin = filterMin
                    });
                /**
                 * 设置视口参数（归一化坐标）
                 */
                mi->setParameter("viewport", float4{
                        float(vp.left) / inputDesc.width,
                        float(vp.bottom) / inputDesc.height,
                        float(vp.width) / inputDesc.width,
                        float(vp.height) / inputDesc.height
                    });
                /**
                 * 提交材质参数并使用
                 */
                mi->commit(driver, getUboManager());
                mi->use(driver);

                /**
                 * 获取管道状态
                 */
                auto pipeline = getPipelineState(ma);
                /**
                 * 如果使用半透明，配置混合函数
                 */
                if (translucent) {
                    pipeline.rasterState.blendFunctionSrcRGB = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionSrcAlpha = BlendFunction::ONE;
                    pipeline.rasterState.blendFunctionDstRGB = BlendFunction::ONE_MINUS_SRC_ALPHA;
                    pipeline.rasterState.blendFunctionDstAlpha = BlendFunction::ONE_MINUS_SRC_ALPHA;
                }

                // The width of each view takes up 1/depth of the screen width.
                /**
                 * 每个视图的宽度占据屏幕宽度的 1/depth
                 */
                out.params.viewport.width /= inputTextureDesc.depth;

                // Render all layers of the texture to the screen side-by-side.
                /**
                 * 将所有纹理层并排渲染到屏幕上
                 */
                for (uint32_t i = 0; i < inputTextureDesc.depth; ++i) {
                    /**
                     * 设置当前层索引
                     */
                    mi->setParameter("layerIndex", i);
                    /**
                     * 提交材质参数
                     */
                    mi->commit(driver, getUboManager());
                    /**
                     * 渲染全屏四边形
                     */
                    renderFullScreenQuad(out, pipeline, driver);
                    /**
                     * 解绑材质描述符堆
                     */
                    DescriptorSet::unbind(driver, DescriptorSetBindingPoints::PER_MATERIAL);
                    // From the second draw, don't clear the targetbuffer.
                    /**
                     * 从第二次绘制开始，不清除目标缓冲区
                     */
                    out.params.flags.clear = TargetBufferFlags::NONE;
                    out.params.flags.discardStart = TargetBufferFlags::NONE;
                    /**
                     * 移动到下一个视图位置
                     */
                    out.params.viewport.left += out.params.viewport.width;
                }
                unbindAllDescriptorSets(driver);
        });

    return ppQuadBlit->output;
}

/**
 * 调试显示阴影贴图（Debug Display Shadow Texture）通道
 * 
 * 用于调试目的，在屏幕上显示阴影贴图的内容。
 * 可以显示指定的层、级别和通道，并应用缩放和幂次调整。
 * 
 * @param fg 帧图引用
 * @param input 输入颜色纹理 ID（将被修改以包含阴影贴图可视化）
 * @param shadowmap 阴影贴图纹理 ID（可选）
 * @param scale 缩放因子
 * @param layer 要显示的层索引
 * @param level 要显示的 Mipmap 级别
 * @param channel 要显示的通道（0=R, 1=G, 2=B, 3=A）
 * @param power 幂次调整（用于亮度调整）
 * @return 输出纹理 ID（如果 shadowmap 为空，返回原始输入）
 * 
 * 处理流程：
 * 1. 如果阴影贴图存在，计算缩放参数
 * 2. 创建调试通道，将阴影贴图叠加到输入颜色上
 * 3. 使用调试材质渲染阴影贴图可视化
 * 
 * 注意：
 * - 这是一个调试功能，不应在生产代码中使用
 * - 使用最近邻 Mipmap 过滤保持清晰度
 * - 可以显示阴影贴图的特定层、级别和通道
 */
FrameGraphId<FrameGraphTexture> PostProcessManager::debugDisplayShadowTexture(
        FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> input,
        FrameGraphId<FrameGraphTexture> const shadowmap, float const scale,
        uint8_t const layer, uint8_t const level, uint8_t const channel, float const power) noexcept {
    /**
     * 如果阴影贴图存在，创建调试通道
     */
    if (shadowmap) {
        struct ShadowMapData {
            FrameGraphId<FrameGraphTexture> color;
            FrameGraphId<FrameGraphTexture> depth;
        };

        /**
         * 计算缩放参数
         */
        auto const& desc = fg.getDescriptor(input);
        /**
         * 计算宽高比
         */
        float const ratio = float(desc.height) / float(desc.width);
        /**
         * 计算屏幕缩放（阴影贴图高度 / 输入高度）
         */
        float const screenScale = float(fg.getDescriptor(shadowmap).height) / float(desc.height);
        /**
         * 计算最终缩放（考虑宽高比和用户缩放）
         */
        float2 const s = { screenScale * scale * ratio, screenScale * scale };

        /**
         * 阴影贴图调试通道
         */
        auto const& shadomapDebugPass = fg.addPass<ShadowMapData>("shadowmap debug pass",
                [&](FrameGraph::Builder& builder, auto& data) {
                    /**
                     * 声明输入颜色为可读写（用于叠加）
                     */
                    data.color = builder.read(input,
                            FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    data.color = builder.write(data.color,
                            FrameGraphTexture::Usage::COLOR_ATTACHMENT);
                    /**
                     * 声明阴影贴图为可采样
                     */
                    data.depth = builder.sample(shadowmap);
                    /**
                     * 声明渲染通道
                     */
                    builder.declareRenderPass("color target", {
                            .attachments = { .color = { data.color }}
                    });
                },
                [=, this](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
                    /**
                     * 绑定描述符堆
                     */
                    bindPostProcessDescriptorSet(driver);
                    bindPerRenderableDescriptorSet(driver);
                    /**
                     * 获取渲染通道信息和阴影贴图纹理
                     */
                    auto const out = resources.getRenderPassInfo();
                    auto in = resources.getTexture(data.depth);
                    /**
                     * 获取阴影贴图调试材质
                     */
                    auto const& material = getPostProcessMaterial("shadowmap");
                    FMaterialInstance* const mi = getMaterialInstance(mEngine, material);
                    /**
                     * 设置阴影贴图纹理（最近邻 Mipmap 过滤）
                     */
                    mi->setParameter("shadowmap", in, SamplerParams{
                        .filterMin = SamplerMinFilter::NEAREST_MIPMAP_NEAREST });
                    /**
                     * 设置缩放参数
                     */
                    mi->setParameter("scale", s);
                    /**
                     * 设置层索引
                     */
                    mi->setParameter("layer", (uint32_t)layer);
                    /**
                     * 设置 Mipmap 级别
                     */
                    mi->setParameter("level", (uint32_t)level);
                    /**
                     * 设置通道索引
                     */
                    mi->setParameter("channel", (uint32_t)channel);
                    /**
                     * 设置幂次调整
                     */
                    mi->setParameter("power", power);
                    /**
                     * 提交并渲染全屏四边形
                     */
                    commitAndRenderFullScreenQuad(driver, out, mi);
                    unbindAllDescriptorSets(driver);
                });
        /**
         * 更新输入为调试输出
         */
        input = shadomapDebugPass->color;
    }
    return input;
}

} // namespace filament
