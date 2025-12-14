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

#include "details/Material.h"
#include "details/Engine.h"

#include "Froxelizer.h"
#include "MaterialParser.h"

#include "ds/ColorPassDescriptorSet.h"

#include "FilamentAPI-impl.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/PushConstantInfo.h>
#include <private/filament/Variant.h>

#include <filament/Material.h>
#include <filament/MaterialEnums.h>

#if FILAMENT_ENABLE_MATDBG
#include <matdbg/DebugServer.h>
#endif

#include <filaflat/ChunkContainer.h>

#include <backend/DriverEnums.h>
#include <backend/CallbackHandler.h>
#include <backend/Program.h>

#include <utils/BitmaskEnum.h>
#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Hash.h>
#include <utils/Invocable.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/bitset.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <algorithm>
#include <array>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include <stddef.h>
#include <stdint.h>

namespace filament {

namespace {

using namespace backend;
using namespace filaflat;
using namespace utils;
using UboBatchingMode = Material::UboBatchingMode;

/**
 * 检查是否应该启用 UBO 批处理
 * 
 * UBO 批处理允许将多个材质实例的统一数据打包到单个 UBO 中，
 * 减少绘制调用和状态切换。
 * 
 * @param engine 引擎引用
 * @param batchingMode 批处理模式
 * @param domain 材质域
 * @return 如果应该启用批处理返回 true，否则返回 false
 */
bool shouldEnableBatching(FEngine& engine, UboBatchingMode batchingMode, MaterialDomain domain) {
    // 批处理必须：
    // 1. 未明确禁用
    // 2. 引擎支持批处理
    // 3. 材质域为 SURFACE（表面材质）
    return batchingMode != UboBatchingMode::DISABLED && engine.isUboBatchingEnabled() &&
           domain == MaterialDomain::SURFACE;
}

} // anonymous namespace

/**
 * 材质构建器详情结构
 * 
 * 存储材质的构建参数。
 */
struct Material::BuilderDetails {
    const void* mPayload = nullptr;  // 材质包数据指针（包含编译后的材质数据）
    size_t mSize = 0;  // 材质包大小（字节）
    bool mDefaultMaterial = false;  // 是否为默认材质（用于错误情况）
    int32_t mShBandsCount = 3;  // 球谐函数频带数（1-3，默认 3，用于 IBL）
    Builder::ShadowSamplingQuality mShadowSamplingQuality = Builder::ShadowSamplingQuality::LOW;  // 阴影采样质量（默认 LOW）
    UboBatchingMode mUboBatchingMode = UboBatchingMode::DEFAULT;  // UBO 批处理模式（默认使用引擎设置）
    std::unordered_map<
        CString,  // 常量名称
        std::variant<int32_t, float, bool>,  // 常量值（可以是整数、浮点数或布尔值）
        CString::Hasher> mConstantSpecializations;  // 常量特化映射（用于编译时优化）
};

/**
 * 默认材质构建器构造函数
 * 
 * 创建默认材质构建器，用于错误情况下的回退材质。
 */
FMaterial::DefaultMaterialBuilder::DefaultMaterialBuilder() {
    mImpl->mDefaultMaterial = true;  // 标记为默认材质
}

using BuilderType = Material;
BuilderType::Builder::Builder() noexcept = default;
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置材质包数据
 * 
 * 设置包含编译后的材质数据的包。
 * 
 * @param payload 材质包数据指针
 * @param size 材质包大小（字节）
 * @return 构建器引用（支持链式调用）
 */
Material::Builder& Material::Builder::package(const void* payload, size_t const size) {
    mImpl->mPayload = payload;  // 设置数据指针
    mImpl->mSize = size;  // 设置数据大小
    return *this;  // 返回自身引用
}

/**
 * 设置球谐函数频带数
 * 
 * 设置用于间接光照（IBL）的球谐函数频带数。
 * 更高的频带数提供更精确的光照，但会增加计算成本。
 * 
 * @param shBandCount 频带数（1-3，1=4 个系数，2=9 个系数，3=16 个系数）
 * @return 构建器引用（支持链式调用）
 */
Material::Builder& Material::Builder::sphericalHarmonicsBandCount(size_t const shBandCount) noexcept {
    mImpl->mShBandsCount = math::clamp(shBandCount, size_t(1), size_t(3));  // 限制在 1-3 范围内
    return *this;  // 返回自身引用
}

/**
 * 设置阴影采样质量
 * 
 * 设置阴影贴图的采样质量，影响阴影的精度和性能。
 * 
 * @param quality 阴影采样质量（LOW、MEDIUM、HIGH）
 * @return 构建器引用（支持链式调用）
 */
Material::Builder& Material::Builder::shadowSamplingQuality(ShadowSamplingQuality const quality) noexcept {
    mImpl->mShadowSamplingQuality = quality;  // 设置质量
    return *this;  // 返回自身引用
}

/**
 * 设置 UBO 批处理模式
 * 
 * 设置统一缓冲区对象（UBO）的批处理模式。
 * 批处理可以将多个材质实例的数据打包到单个 UBO 中，减少绘制调用。
 * 
 * @param mode 批处理模式（DISABLED、ENABLED、DEFAULT）
 * @return 构建器引用（支持链式调用）
 */
Material::Builder& Material::Builder::uboBatching(UboBatchingMode const mode) noexcept {
    mImpl->mUboBatchingMode = mode;  // 设置批处理模式
    return *this;  // 返回自身引用
}

/**
 * 设置常量特化值
 * 
 * 设置材质中的编译时常量值，用于编译时优化。
 * 这些值会在编译时替换材质中的常量，生成优化的着色器变体。
 * 
 * @tparam T 常量类型（int32_t、float 或 bool）
 * @param name 常量名称
 * @param nameLength 名称长度
 * @param value 常量值
 * @return 构建器引用（支持链式调用）
 */
template<typename T, typename>
Material::Builder& Material::Builder::constant(const char* name, size_t nameLength, T value) {
    FILAMENT_CHECK_PRECONDITION(name != nullptr) << "name cannot be null";  // 验证名称不为空
    mImpl->mConstantSpecializations[{name, nameLength}] = value;  // 存储常量特化值
    return *this;  // 返回自身引用
}

template Material::Builder& Material::Builder::constant<int32_t>(const char*, size_t, int32_t);
template Material::Builder& Material::Builder::constant<float>(const char*, size_t, float);
template Material::Builder& Material::Builder::constant<bool>(const char*, size_t, bool);


/**
 * 着色器模型转字符串
 * 
 * 将着色器模型枚举值转换为字符串表示。
 * 
 * @param model 着色器模型枚举值
 * @return 字符串指针（"mobile" 或 "desktop"）
 */
const char* toString(ShaderModel model) {
    switch (model) {
        case ShaderModel::MOBILE:
            return "mobile";
        case ShaderModel::DESKTOP:
            return "desktop";
    }
}

/**
 * 构建材质
 * 
 * 根据构建器配置创建材质对象。
 * 
 * @param engine 引擎引用
 * @return 材质指针（如果构建失败则返回 nullptr）
 * 
 * 处理流程：
 * 1. 从材质缓存中获取或创建材质定义
 * 2. 根据构建器和材质定义创建材质对象
 */
Material* Material::Builder::build(Engine& engine) const {
    MaterialDefinition* r = downcast(engine).getMaterialCache().acquire(downcast(engine),
            mImpl->mPayload, mImpl->mSize);
    if (r) {
        return downcast(engine).createMaterial(*this, *r);
    }
    return nullptr;
}

/**
 * 材质构造函数
 * 
 * 从构建器和材质定义创建材质对象。
 * 
 * @param engine 引擎引用
 * @param builder 材质构建器
 * @param definition 材质定义（包含解析后的材质数据）
 * 
 * 初始化过程：
 * 1. 设置材质定义、默认材质标志、UBO 批处理标志等
 * 2. 验证 UBO 批处理配置
 * 3. 处理特化常量（编译时常量替换）
 * 4. 预缓存深度变体（用于深度渲染）
 * 5. 注册到 matdbg（如果启用）
 */
FMaterial::FMaterial(FEngine& engine, const Builder& builder, MaterialDefinition const& definition)
        : mDefinition(definition),  // 保存材质定义（包含解析后的材质数据）
          mIsDefaultMaterial(builder->mDefaultMaterial),  // 是否为默认材质
          mUseUboBatching(shouldEnableBatching(engine, builder->mUboBatchingMode,
                  definition.materialDomain)),  // 是否启用 UBO 批处理
          mEngine(engine),  // 保存引擎引用
          mMaterialId(engine.getMaterialId()) {  // 获取唯一的材质 ID
    /**
     * 验证：如果启用了 UBO 批处理，引擎必须支持它
     */
    FILAMENT_CHECK_PRECONDITION(!mUseUboBatching || engine.isUboBatchingEnabled())
            << "UBO batching is not enabled.";
    /**
     * 处理特化常量（包括保留常量和用户定义的常量）
     */
    mSpecializationConstants = processSpecializationConstants(builder);
    /**
     * 预缓存深度变体，优化首次渲染性能
     * - 对于默认材质：预编译所有深度变体
     * - 对于其他材质：从默认材质继承深度变体（如果没有自定义深度着色器）
     */
    precacheDepthVariants(engine);

#if FILAMENT_ENABLE_MATDBG
    /**
     * 注册材质到 matdbg 调试服务器
     */
    matdbg::DebugServer* server = downcast(engine).debug.server;
    if (UTILS_UNLIKELY(server)) {
        auto const details = builder.mImpl;
        mDebuggerId = server->addMaterial(mDefinition.name, details->mPayload, details->mSize, this);
    }
#endif
}

/**
 * 材质析构函数
 */
FMaterial::~FMaterial() noexcept = default;

/**
 * 使着色器程序变体无效
 * 
 * 销毁匹配变体掩码和值的缓存着色器程序。
 * 
 * @param variantMask 变体掩码（指定要检查哪些变体位）
 * @param variantValue 变体值（指定这些位的值）
 * 
 * 注意：此 API 目前不是公开的，所以可以有一些调试日志和额外检查。
 * 
 * 特殊处理：
 * - 对于 SURFACE 材质域且没有自定义深度着色器的情况，
 *   保护深度变体缓存不被破坏
 */
void FMaterial::invalidate(Variant::type_t variantMask, Variant::type_t variantValue) noexcept {
    if (mDefinition.materialDomain == MaterialDomain::SURFACE &&
            !mIsDefaultMaterial &&
            !mDefinition.hasCustomDepthShader) {
        /**
         * 破坏深度变体缓存是不安全的
         * 
         * 验证：variantMask 必须包含 DEP 位，且 variantValue 不能包含 DEP 位
         */
        if (UTILS_UNLIKELY(!((variantMask & Variant::DEP) && !(variantValue & Variant::DEP)))) {
            char variantMaskString[16];
            snprintf(variantMaskString, sizeof(variantMaskString), "%#x", +variantMask);
            char variantValueString[16];
            snprintf(variantValueString, sizeof(variantValueString), "%#x", +variantValue);
            LOG(WARNING) << "FMaterial::invalidate(" << variantMaskString << ", "
                         << variantValueString << ") would corrupt the depth variant cache";
        }
        /**
         * 强制包含 DEP 位到掩码中，并清除 DEP 位的值
         */
        variantMask |= Variant::DEP;
        variantValue &= ~Variant::DEP;
    }
    destroyPrograms(mEngine, variantMask, variantValue);
}

/**
 * 终止材质
 * 
 * 清理材质的所有资源，包括：
 * - 默认材质实例
 * - 所有着色器程序
 * - 材质缓存引用
 * - matdbg 注册
 * 
 * @param engine 引擎引用
 * 
 * 注意：确保在销毁材质之前销毁所有材质实例。
 */
void FMaterial::terminate(FEngine& engine) {
    /**
     * 销毁默认材质实例
     */
    if (mDefaultMaterialInstance) {
        mDefaultMaterialInstance->setDefaultInstance(false);
        engine.destroy(mDefaultMaterialInstance);
        mDefaultMaterialInstance = nullptr;
    }

    /**
     * 确保在销毁材质之前销毁所有材质实例
     */
    auto const& materialInstanceResourceList = engine.getMaterialInstanceResourceList();
    auto pos = materialInstanceResourceList.find(this);
    if (UTILS_LIKELY(pos != materialInstanceResourceList.cend())) {
        auto const& featureFlags = engine.features.engine.debug;
        FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION(pos->second.empty(),
                featureFlags.assert_destroy_material_before_material_instance)
                << "destroying material \"" << this->getName().c_str_safe() << "\" but "
                << pos->second.size() << " instances still alive.";
    }

#if FILAMENT_ENABLE_MATDBG
    /**
     * 从 matdbg 注销材质
     */
    matdbg::DebugServer* server = engine.debug.server;
    if (UTILS_UNLIKELY(server)) {
        server->removeMaterial(mDebuggerId);
    }
#endif

    /**
     * 销毁所有着色器程序
     */
    destroyPrograms(engine);
    /**
     * 释放材质缓存引用
     */
    engine.getMaterialCache().release(engine, mDefinition);
}

/**
 * 获取每视图描述符集布局
 * 
 * 根据变体和 VSM 标志返回相应的描述符集布局。
 * 
 * @param variant 着色器变体（仅对 SURFACE 材质域有意义）
 * @param useVsmDescriptorSetLayout 是否使用 VSM 描述符集布局
 * @return 描述符集布局常量引用
 * 
 * 选择逻辑：
 * - SURFACE 材质域：
 *   - 如果是有效的深度变体，返回深度变体描述符集布局
 *   - 如果是 SSR 变体，返回 SSR 变体描述符集布局
 *   - 否则根据 useVsmDescriptorSetLayout 返回 VSM 或普通描述符集布局
 * - 其他材质域：直接根据 useVsmDescriptorSetLayout 返回
 */
filament::DescriptorSetLayout const& FMaterial::getPerViewDescriptorSetLayout(
        Variant const variant, bool const useVsmDescriptorSetLayout) const noexcept {
    if (mDefinition.materialDomain == MaterialDomain::SURFACE) {
        /**
         * 变体仅对 SURFACE 材质域有意义
         */
        if (Variant::isValidDepthVariant(variant)) {
            return mEngine.getPerViewDescriptorSetLayoutDepthVariant();
        }
        if (Variant::isSSRVariant(variant)) {
            return mEngine.getPerViewDescriptorSetLayoutSsrVariant();
        }
    }
    /**
     * mDefinition.perViewDescriptorSetLayout{Vsm} 已经根据材质域解析
     */
    if (useVsmDescriptorSetLayout) {
        return mDefinition.perViewDescriptorSetLayoutVsm;
    }
    return mDefinition.perViewDescriptorSetLayout;
}

/**
 * 异步编译材质的所有指定变体
 * 
 * 编译材质包中匹配指定变体掩码的所有着色器变体。
 * 如果后端支持并行着色器编译，则并行编译所有变体以提高性能。
 * 
 * @param priority 编译优先级队列（HIGH 或 LOW）
 * @param variantSpec 要编译的变体掩码（UserVariantFilterMask，指定要编译哪些变体）
 * @param handler 回调处理器（用于线程间通信，可以为 nullptr）
 * @param callback 编译完成后的回调函数（在主线程调用，可以为空）
 * 
 * 处理流程：
 * 1. 如果后端不支持立体渲染，则关闭 STE（立体）变体
 * 2. 计算要过滤掉的变体掩码
 * 3. 如果支持并行着色器编译，则遍历并编译所有匹配的变体
 * 4. 如果有回调函数，注册到驱动 API 的编译队列
 */
void FMaterial::compile(CompilerPriorityQueue const priority,
        UserVariantFilterMask variantSpec,
        CallbackHandler* handler,
        Invocable<void(Material*)>&& callback) noexcept {

    // 如果后端不支持立体渲染，则关闭 STE（立体）变体
    if (!mEngine.getDriverApi().isStereoSupported()) {
        variantSpec &= ~UserVariantFilterMask(UserVariantFilterBit::STE);
    }

    // 计算要过滤掉的变体掩码（variantSpec 指定要编译的，variantFilter 是要过滤的）
    UserVariantFilterMask const variantFilter =
            ~variantSpec & UserVariantFilterMask(UserVariantFilterBit::ALL);

    // 如果后端支持并行着色器编译，则并行编译所有变体
    if (UTILS_LIKELY(mEngine.getDriverApi().isParallelShaderCompileSupported())) {
        // 根据材质是 Lit 还是 Unlit 获取对应的变体列表
        auto const& variants = isVariantLit() ?
                VariantUtils::getLitVariants() : VariantUtils::getUnlitVariants();
        for (auto const variant: variants) {
            // 如果 variantFilter 为空（编译所有变体），或者当前变体通过过滤
            if (!variantFilter || variant == Variant::filterUserVariant(variant, variantFilter)) {
                // 检查材质包中是否包含此变体的着色器
                if (hasVariant(variant)) {
                    // 准备并编译此变体的着色器程序（如果尚未缓存）
                    prepareProgram(variant, priority);
                }
            }
        }
    }

    // 如果有回调函数，注册到驱动 API 的编译队列
    if (callback) {
        struct Callback {
            Invocable<void(Material*)> f;
            Material* m;
            static void func(void* user) {
                auto* const c = static_cast<Callback*>(user);
                c->f(c->m);
                delete c;
            }
        };
        // 创建回调包装器，在编译完成后调用用户回调
        auto* const user = new(std::nothrow) Callback{ std::move(callback), this };
        mEngine.getDriverApi().compilePrograms(priority, handler, &Callback::func, user);
    } else {
        // 没有回调，只触发编译（不等待完成）
        mEngine.getDriverApi().compilePrograms(priority, nullptr, nullptr, nullptr);
    }
}

/**
 * 创建材质实例
 * 
 * 创建一个新的材质实例。如果存在默认材质实例，则复制它；否则创建带有默认参数的新实例。
 * 
 * @param name 材质实例名称（可以为 nullptr）
 * @return 材质实例指针
 */
FMaterialInstance* FMaterial::createInstance(const char* name) const noexcept {
    if (mDefaultMaterialInstance) {
        /**
         * 如果已有默认实例，使用它来创建新实例（复制）
         */
        return FMaterialInstance::duplicate(mDefaultMaterialInstance, name);
    } else {
        /**
         * 如果没有默认实例，创建带有所有默认参数的新实例
         */
        return mEngine.createMaterialInstance(this, name);
    }
}

/**
 * 获取默认材质实例
 * 
 * 返回默认材质实例。如果不存在，则创建一个新的默认实例。
 * 
 * @return 默认材质实例指针
 */
FMaterialInstance* FMaterial::getDefaultInstance() noexcept {
    if (UTILS_UNLIKELY(!mDefaultMaterialInstance)) {
        mDefaultMaterialInstance =
                mEngine.createMaterialInstance(this, mDefinition.name.c_str());
        mDefaultMaterialInstance->setDefaultInstance(true);
    }
    return mDefaultMaterialInstance;
}

/**
 * 检查是否有指定名称的参数
 * 
 * 检查材质是否包含指定名称的参数（可以是 uniform、采样器或子通道）。
 * 
 * @param name 参数名称
 * @return 如果存在返回 true，否则返回 false
 */
bool FMaterial::hasParameter(const char* name) const noexcept {
    return mDefinition.uniformInterfaceBlock.hasField(name) ||
           mDefinition.samplerInterfaceBlock.hasSampler(name) ||
            mDefinition.subpassInfo.name == CString(name);
}

/**
 * 检查指定名称的参数是否为采样器
 * 
 * 检查指定名称的参数是否是采样器类型。
 * 
 * @param name 参数名称
 * @return 如果是采样器返回 true，否则返回 false
 */
bool FMaterial::isSampler(const char* name) const noexcept {
    return mDefinition.samplerInterfaceBlock.hasSampler(name);
}

/**
 * 反射 uniform 参数信息
 * 
 * 获取指定名称的 uniform 参数的详细信息（类型、偏移量等）。
 * 
 * @param name 参数名称
 * @return 字段信息指针（如果不存在则返回 nullptr）
 */
BufferInterfaceBlock::FieldInfo const* FMaterial::reflect(
        std::string_view const name) const noexcept {
    return mDefinition.uniformInterfaceBlock.getFieldInfo(name);
}

/**
 * 获取材质解析器
 * 
 * 返回材质解析器，用于访问材质包中的着色器和元数据。
 * 
 * @return 材质解析器常量引用
 * 
 * 注意：如果启用了 matdbg 且材质已被编辑，返回编辑后的解析器。
 */
MaterialParser const& FMaterial::getMaterialParser() const noexcept {
#if FILAMENT_ENABLE_MATDBG
    if (mEditedMaterialParser) {
        return *mEditedMaterialParser;
    }
#endif
    return mDefinition.getMaterialParser();
}

/**
 * 检查是否具有指定的着色器变体
 * 
 * 检查材质包中是否包含指定变体的顶点和片段着色器。
 * 
 * @param variant 着色器变体
 * @return 如果具有变体返回 true，否则返回 false
 * 
 * 处理流程：
 * 1. 根据材质域过滤变体（SURFACE 分别过滤顶点和片段，POST_PROCESS 使用相同变体）
 * 2. 检查材质解析器中是否存在对应着色器阶段的着色器代码
 */
bool FMaterial::hasVariant(Variant const variant) const noexcept {
    Variant vertexVariant, fragmentVariant;
    switch (getMaterialDomain()) {
        case MaterialDomain::SURFACE:
            /**
             * SURFACE 材质域：分别过滤顶点和片段着色器的变体
             */
            vertexVariant = Variant::filterVariantVertex(variant);
            fragmentVariant = Variant::filterVariantFragment(variant);
            break;
        case MaterialDomain::POST_PROCESS:
            /**
             * POST_PROCESS 材质域：顶点和片段使用相同的变体
             */
            vertexVariant = fragmentVariant = variant;
            break;
        case MaterialDomain::COMPUTE:
            /**
             * COMPUTE 材质域：尚未实现
             */
            return false;
    }
    const ShaderModel sm = mEngine.getShaderModel();
    /**
     * 检查顶点着色器是否存在
     */
    if (!mDefinition.getMaterialParser().hasShader(sm, vertexVariant, ShaderStage::VERTEX)) {
        return false;
    }
    /**
     * 检查片段着色器是否存在
     */
    if (!mDefinition.getMaterialParser().hasShader(sm, fragmentVariant, ShaderStage::FRAGMENT)) {
        return false;
    }
    return true;
}

/**
 * 准备着色器程序（慢路径）
 * 
 * 根据材质域调用相应的程序准备函数。
 * 这是实际编译着色器的路径（与缓存查找的快速路径相对）。
 * 
 * @param variant 着色器变体
 * @param priorityQueue 编译优先级队列
 */
void FMaterial::prepareProgramSlow(Variant const variant,
        backend::CompilerPriorityQueue const priorityQueue) const noexcept {
    /**
     * 验证引擎功能级别是否满足材质要求
     */
    assert_invariant(mEngine.hasFeatureLevel(mDefinition.featureLevel));
    /**
     * 根据材质域调用相应的程序准备函数
     */
    switch (getMaterialDomain()) {
        case MaterialDomain::SURFACE:
            /**
             * Surface 材质域：需要分别处理顶点和片段着色器变体
             */
            getSurfaceProgramSlow(variant, priorityQueue);
            break;
        case MaterialDomain::POST_PROCESS:
            /**
             * Post-Process 材质域：顶点和片段使用相同的变体
             */
            getPostProcessProgramSlow(variant, priorityQueue);
            break;
        case MaterialDomain::COMPUTE:
            /**
             * Compute 材质域：尚未实现
             */
            // TODO: implement MaterialDomain::COMPUTE
            break;
    }
}

/**
 * 为 Surface 材质域创建着色器程序（慢路径，实际编译）
 * 
 * 为 Surface 材质域创建着色器程序，包括顶点和片段着色器。
 * 分别过滤顶点和片段着色器需要的变体位，然后编译并缓存程序。
 * 
 * @param variant 完整的变体掩码（包含所有位）
 * @param priorityQueue 编译优先级队列
 * 
 * 处理流程：
 * 1. 验证变体是否已正确过滤
 * 2. 分别过滤顶点和片段着色器的变体
 * 3. 从 MaterialParser 获取着色器代码并构建 Program 对象
 * 4. 设置编译优先级、多视图等选项
 * 5. 创建并缓存着色器程序
 */
void FMaterial::getSurfaceProgramSlow(Variant const variant,
        CompilerPriorityQueue const priorityQueue) const noexcept {
    /**
     * filterVariant() 已经在 generateCommands() 中应用，这里不应该再需要。
     * 如果是 Unlit 材质，不应该有任何对应 Lit 材质的位。
     */
    assert_invariant(variant == Variant::filterVariant(variant, isVariantLit()) );

    /**
     * 确保变体不是保留的（无效的）变体。
     * 保留变体是用于特殊用途的，不应该在这里使用。
     */
    assert_invariant(!Variant::isReserved(variant));

    /**
     * 分别过滤出顶点着色器和片段着色器需要的变体位。
     * 
     * 顶点着色器只关心：立体、蒙皮、阴影接收、动态光源、方向光。
     * 片段着色器只关心：VSM、雾效、阴影接收、动态光源、方向光。
     * 
     * 这是因为顶点和片段着色器需要不同的变体信息。
     */
    Variant const vertexVariant   = Variant::filterVariantVertex(variant);
    Variant const fragmentVariant = Variant::filterVariantFragment(variant);

    /**
     * 从 MaterialParser 获取顶点和片段着色器代码，构建 Program 对象。
     * 这会从材质包中提取指定变体的着色器代码。
     */
    Program pb{ getProgramWithVariants(variant, vertexVariant, fragmentVariant) };
    /**
     * 设置编译优先级队列（HIGH 或 LOW）。
     * 高优先级用于关键路径的着色器，低优先级用于后台编译。
     */
    pb.priorityQueue(priorityQueue);
    /**
     * 如果是多视图立体渲染，启用 multiview 扩展。
     * multiview 允许一次渲染到多个视图，提高立体渲染性能。
     */
    pb.multiview(
            mEngine.getConfig().stereoscopicType == StereoscopicType::MULTIVIEW &&
            Variant::isStereoVariant(variant));
    /**
     * 创建着色器程序并缓存。
     * 如果支持共享变体（如深度变体），可能会从默认材质共享程序。
     */
    createAndCacheProgram(std::move(pb), variant);
}

/**
 * 为 Post-Process 材质域创建着色器程序（慢路径）
 * 
 * 为 Post-Process 材质域创建着色器程序。
 * Post-Process 材质的顶点和片段着色器使用相同的变体。
 * 
 * @param variant 着色器变体
 * @param priorityQueue 编译优先级队列
 */
void FMaterial::getPostProcessProgramSlow(Variant const variant,
        CompilerPriorityQueue const priorityQueue) const noexcept {
    Program pb{ getProgramWithVariants(variant, variant, variant) };
    pb.priorityQueue(priorityQueue);
    createAndCacheProgram(std::move(pb), variant);
}

/**
 * 从 MaterialParser 获取顶点和片段着色器代码，构建 Program 对象
 * 
 * 从材质解析器中提取指定变体的顶点和片段着色器代码，并构建 Program 对象。
 * 设置着色器语言、诊断信息、Uniform 绑定、描述符绑定、特化常量、Push Constants 等。
 * 
 * @param variant 完整的变体掩码（用于缓存 ID）
 * @param vertexVariant 过滤后的顶点着色器变体
 * @param fragmentVariant 过滤后的片段着色器变体
 * @return Program 对象（包含着色器代码和元数据）
 */
Program FMaterial::getProgramWithVariants(
        Variant variant,
        Variant vertexVariant,
        Variant fragmentVariant) const {
    FEngine const& engine = mEngine;
    const ShaderModel sm = engine.getShaderModel();
    const bool isNoop = engine.getBackend() == Backend::NOOP;
    /*
     * Vertex shader - 从材质包中提取顶点着色器代码
     */

    MaterialParser const& parser = getMaterialParser();

    // 获取引擎的顶点着色器内容缓冲区（可重用的临时缓冲区）
    ShaderContent& vsBuilder = engine.getVertexShaderContent();

    // 从 MaterialParser 获取指定变体的顶点着色器代码
    UTILS_UNUSED_IN_RELEASE bool const vsOK = parser.getShader(vsBuilder, sm,
            vertexVariant, ShaderStage::VERTEX);

    // 验证顶点着色器是否成功提取（NOOP 后端除外）
    FILAMENT_CHECK_POSTCONDITION(isNoop || (vsOK && !vsBuilder.empty()))
            << "The material '" << mDefinition.name.c_str()
            << "' has not been compiled to include the required GLSL or SPIR-V chunks for the "
               "vertex shader (variant="
            << +variant.key << ", filtered=" << +vertexVariant.key << ").";

    /*
     * Fragment shader - 从材质包中提取片段着色器代码
     */

    // 获取引擎的片段着色器内容缓冲区（可重用的临时缓冲区）
    ShaderContent& fsBuilder = engine.getFragmentShaderContent();

    // 从 MaterialParser 获取指定变体的片段着色器代码
    UTILS_UNUSED_IN_RELEASE bool const fsOK = parser.getShader(fsBuilder, sm,
            fragmentVariant, ShaderStage::FRAGMENT);

    // 验证片段着色器是否成功提取（NOOP 后端除外）
    FILAMENT_CHECK_POSTCONDITION(isNoop || (fsOK && !fsBuilder.empty()))
            << "The material '" << mDefinition.name.c_str()
            << "' has not been compiled to include the required GLSL or SPIR-V chunks for the "
               "fragment shader (variant="
            << +variant.key << ", filtered=" << +fragmentVariant.key << ").";

    // 构建 Program 对象
    Program program;
    // 设置顶点和片段着色器代码
    program.shader(ShaderStage::VERTEX, vsBuilder.data(), vsBuilder.size())
            .shader(ShaderStage::FRAGMENT, fsBuilder.data(), fsBuilder.size())
            // 设置着色器语言（GLSL、SPIR-V、MSL、WGSL）
            .shaderLanguage(parser.getShaderLanguage())
            // 设置诊断信息（用于错误报告）
            .diagnostics(mDefinition.name,
                    [variant, vertexVariant, fragmentVariant](utils::CString const& name,
                            io::ostream& out) -> io::ostream& {
                        return out << name.c_str_safe() << ", variant=(" << io::hex << +variant.key
                                   << io::dec << "), vertexVariant=(" << io::hex
                                   << +vertexVariant.key << io::dec << "), fragmentVariant=("
                                   << io::hex << +fragmentVariant.key << io::dec << ")";
                    });

    // ESSL1（OpenGL ES 2.0）需要额外的 Uniform 和 Attribute 绑定信息
    if (UTILS_UNLIKELY(parser.getShaderLanguage() == ShaderLanguage::ESSL1)) {
        assert_invariant(!mDefinition.bindingUniformInfo.empty());
        // 设置 Uniform 绑定信息（用于 ESSL1 的 Uniform 位置绑定）
        for (auto const& [index, name, uniforms] : mDefinition.bindingUniformInfo) {
            program.uniforms(uint32_t(index), name, uniforms);
        }
        // 设置 Attribute 信息（用于 ESSL1 的 Attribute 位置绑定）
        program.attributes(mDefinition.attributeInfo);
    }

    // 设置描述符堆绑定信息（用于 Vulkan/Metal 的 Descriptor Set 绑定）
    program.descriptorBindings(+DescriptorSetBindingPoints::PER_VIEW,
            mDefinition.programDescriptorBindings[+DescriptorSetBindingPoints::PER_VIEW]);
    program.descriptorBindings(+DescriptorSetBindingPoints::PER_RENDERABLE,
            mDefinition.programDescriptorBindings[+DescriptorSetBindingPoints::PER_RENDERABLE]);
    program.descriptorBindings(+DescriptorSetBindingPoints::PER_MATERIAL,
            mDefinition.programDescriptorBindings[+DescriptorSetBindingPoints::PER_MATERIAL]);
    // 设置特化常量（用于编译时优化）
    program.specializationConstants(mSpecializationConstants);

    // 设置 Push Constants（用于 Vulkan/Metal 的小数据传递）
    program.pushConstants(ShaderStage::VERTEX,
            mDefinition.pushConstants[uint8_t(ShaderStage::VERTEX)]);
    program.pushConstants(ShaderStage::FRAGMENT,
            mDefinition.pushConstants[uint8_t(ShaderStage::FRAGMENT)]);

    // 设置缓存 ID（用于程序缓存，基于材质缓存 ID 和变体键）
    program.cacheId(hash::combine(size_t(mDefinition.cacheId), variant.key));

    return program;
}

/**
 * 创建着色器程序并缓存
 * 
 * 通过驱动 API 创建着色器程序，并缓存到材质的程序数组中。
 * 对于共享变体（如深度变体），可能会从默认材质共享程序，避免重复编译。
 * 
 * @param p 已构建的 Program 对象（包含着色器代码和元数据）
 * @param variant 变体键（用于索引缓存数组）
 * 
 * 处理流程：
 * 1. 检查是否为共享变体
 * 2. 如果是共享变体，尝试从默认材质获取已缓存的程序
 * 3. 如果没有缓存，通过驱动 API 创建新程序
 * 4. 将程序缓存到当前材质
 * 5. 如果是共享变体且默认材质还没有，也缓存到默认材质
 */
void FMaterial::createAndCacheProgram(Program&& p, Variant const variant) const noexcept {
    FEngine const& engine = mEngine;
    DriverApi& driverApi = mEngine.getDriverApi();

    // 检查是否为共享变体（深度变体，可以从默认材质共享）
    bool const isShared = isSharedVariant(variant);

    // 如果是共享变体，先检查默认材质是否已经缓存了此程序
    if (isShared) {
        FMaterial const* const pDefaultMaterial = engine.getDefaultMaterial();
        if (pDefaultMaterial) {
            auto const program = pDefaultMaterial->mCachedPrograms[variant.key];
            if (program) {
                // 直接使用默认材质的缓存程序，避免重复创建
                mCachedPrograms[variant.key] = program;
                return;
            }
        }
    }

    // 通过驱动 API 创建实际的着色器程序（编译着色器代码）
    auto const program = driverApi.createProgram(std::move(p),
            ImmutableCString{ mDefinition.name.c_str_safe() });
    assert_invariant(program);
    // 缓存到当前材质的程序数组中
    mCachedPrograms[variant.key] = program;

    // 如果是共享变体，且默认材质还没有缓存此程序，则也缓存到默认材质
    // 这样后续创建的材质可以自动继承这些程序，减少重复编译
    if (isShared) {
        FMaterial const* const pDefaultMaterial = engine.getDefaultMaterial();
        if (pDefaultMaterial && !pDefaultMaterial->mCachedPrograms[variant.key]) {
            pDefaultMaterial->mCachedPrograms[variant.key] = program;
        }
    }
}

/**
 * 获取参数信息
 * 
 * 获取材质的所有参数信息（uniform、采样器和子通道），填充到提供的数组中。
 * 
 * @param parameters 参数信息数组（输出）
 * @param count 数组大小（输入/输出）
 * @return 实际填充的参数数量
 * 
 * 参数顺序：
 * 1. Uniform 参数
 * 2. 采样器参数
 * 3. 子通道参数（如果存在）
 */
size_t FMaterial::getParameters(ParameterInfo* parameters, size_t count) const noexcept {
    /**
     * 限制返回的参数数量不超过数组大小和实际参数数量
     */
    count = std::min(count, getParameterCount());

    /**
     * 第一部分：填充 Uniform 参数信息
     */
    const auto& uniforms = mDefinition.uniformInterfaceBlock.getFieldInfoList();
    size_t i = 0;  // 当前填充位置
    size_t const uniformCount = std::min(count, size_t(uniforms.size()));
    for ( ; i < uniformCount; i++) {
        ParameterInfo& info = parameters[i];
        const auto& uniformInfo = uniforms[i];
        info.name = uniformInfo.name.c_str();  // 参数名称
        info.isSampler = false;  // 不是采样器
        info.isSubpass = false;  // 不是子通道
        info.type = uniformInfo.type;  // 参数类型（int、float、vec3 等）
        info.count = std::max(1u, uniformInfo.size);  // 数组元素数量（至少为 1）
        info.precision = uniformInfo.precision;  // 精度（lowp、mediump、highp）
    }

    /**
     * 第二部分：填充采样器参数信息
     */
    const auto& samplers = mDefinition.samplerInterfaceBlock.getSamplerInfoList();
    size_t const samplerCount = samplers.size();
    for (size_t j = 0; i < count && j < samplerCount; i++, j++) {
        ParameterInfo& info = parameters[i];
        const auto& samplerInfo = samplers[j];
        info.name = samplerInfo.name.c_str();  // 采样器名称
        info.isSampler = true;  // 是采样器
        info.isSubpass = false;  // 不是子通道
        info.samplerType = samplerInfo.type;  // 采样器类型（2D、CUBEMAP 等）
        info.count = 1;  // 采样器总是单个（不支持数组）
        info.precision = samplerInfo.precision;  // 精度
    }

    /**
     * 第三部分：填充子通道参数信息（如果存在且还有空间）
     */
    if (mDefinition.subpassInfo.isValid && i < count) {
        ParameterInfo& info = parameters[i];
        info.name = mDefinition.subpassInfo.name.c_str();  // 子通道名称
        info.isSampler = false;  // 不是采样器
        info.isSubpass = true;  // 是子通道
        info.subpassType = mDefinition.subpassInfo.type;  // 子通道类型
        info.count = 1;  // 子通道总是单个
        info.precision = mDefinition.subpassInfo.precision;  // 精度
    }

    return count;  // 返回实际填充的参数数量
}

#if FILAMENT_ENABLE_MATDBG

/**
 * 应用待处理的编辑
 * 
 * 交换入原始材质包的编辑版本。编辑后的包是在响应调试器事件时暂存的。
 * 仅在附加了 Material Debugger 时调用。
 * 材质包中唯一可编辑的特性是着色器源字符串，因此这里触发 HwProgram 对象的重建。
 * 
 * 处理流程：
 * 1. 销毁所有现有着色器程序
 * 2. 锁定待处理编辑并交换到已编辑解析器
 */
void FMaterial::applyPendingEdits() noexcept {
    const char* name = mDefinition.name.c_str();
    DLOG(INFO) << "Applying edits to " << (name ? name : "(untitled)");
    destroyPrograms(mEngine); // FIXME: this will not destroy the shared variants
    latchPendingEdits();
}

/**
 * 设置待处理的编辑
 * 
 * 设置待处理的材质解析器编辑（线程安全）。
 * 
 * @param pendingEdits 待处理的材质解析器（包含编辑后的材质包）
 */
void FMaterial::setPendingEdits(std::unique_ptr<MaterialParser> pendingEdits) noexcept {
    std::lock_guard const lock(mPendingEditsLock);
    std::swap(pendingEdits, mPendingEdits);
}

/**
 * 检查是否有待处理的编辑
 * 
 * 检查是否有待应用的材质编辑（线程安全）。
 * 
 * @return 如果有待处理编辑返回 true，否则返回 false
 */
bool FMaterial::hasPendingEdits() const noexcept {
    std::lock_guard const lock(mPendingEditsLock);
    return bool(mPendingEdits);
}

/**
 * 锁定待处理的编辑
 * 
 * 将待处理的编辑移动到已编辑解析器中（线程安全）。
 * 这会在下次获取材质解析器时使用编辑后的版本。
 */
void FMaterial::latchPendingEdits() noexcept {
    std::lock_guard const lock(mPendingEditsLock);
    mEditedMaterialParser = std::move(mPendingEdits);
}

/**
 * Callback handlers for the debug server, potentially called from any thread. These methods are
 * never called during normal operation and exist for debugging purposes only.
 *
 * @{
 */

/**
 * 编辑回调（调试服务器回调）
 * 
 * 材质调试服务器的编辑回调，可能从任何线程调用。
 * 此方法在正常操作期间从不调用，仅用于调试目的。
 * 
 * 处理流程：
 * 1. 此回调在 Web 服务器线程上调用
 * 2. 延迟清除程序缓存和交换 MaterialParser，直到下次 getProgram 调用
 * 3. 创建新的材质解析器并设置为待处理编辑
 * 
 * @param userdata 材质指针（转换为 FMaterial*）
 * @param packageData 编辑后的材质包数据
 * @param packageSize 材质包大小
 */
void FMaterial::onEditCallback(void* userdata, const CString&, const void* packageData,
        size_t const packageSize) {
    FMaterial* material = downcast(static_cast<Material*>(userdata));
    FEngine const& engine = material->mEngine;

    // This is called on a web server thread, so we defer clearing the program cache
    // and swapping out the MaterialParser until the next getProgram call.
    std::unique_ptr<MaterialParser> pending = MaterialDefinition::createParser(
            engine.getBackend(), engine.getShaderLanguage(), packageData, packageSize);
    material->setPendingEdits(std::move(pending));
}

/**
 * 查询回调（调试服务器回调）
 * 
 * 材质调试服务器的查询回调，可能从任何线程调用。
 * 此方法在正常操作期间从不调用，仅用于调试目的。
 * 
 * 返回当前活跃的着色器变体列表（最近请求的变体），用于调试和可视化。
 * 
 * @param userdata 材质指针（转换为 FMaterial*）
 * @param pActiveVariants 活跃变体列表（输出）
 */
void FMaterial::onQueryCallback(void* userdata, VariantList* pActiveVariants) {
    FMaterial const* material = downcast(static_cast<Material*>(userdata));
    std::lock_guard const lock(material->mActiveProgramsLock);
    *pActiveVariants = material->mActivePrograms;
    material->mActivePrograms.reset();
}

/** @}*/

#endif // FILAMENT_ENABLE_MATDBG

/**
 * 获取着色器程序（带 MATDBG 支持）
 * 
 * 获取指定变体的着色器程序句柄，并记录活跃变体用于调试。
 * 
 * @param variant 着色器变体
 * @return 着色器程序句柄
 * 
 * 处理流程：
 * 1. 如果启用了 MATDBG，记录活跃变体
 * 2. 如果是共享变体，尝试从默认材质获取
 * 3. 否则返回当前材质缓存的程序
 */
[[nodiscard]] Handle<HwProgram> FMaterial::getProgramWithMATDBG(Variant const variant) const noexcept {
#if FILAMENT_ENABLE_MATDBG
    assert_invariant((size_t)variant.key < VARIANT_COUNT);
    std::unique_lock lock(mActiveProgramsLock);
    if (getMaterialDomain() == MaterialDomain::SURFACE) {
        auto vert = Variant::filterVariantVertex(variant);
        auto frag = Variant::filterVariantFragment(variant);
        mActivePrograms.set(vert.key);
        mActivePrograms.set(frag.key);
    } else {
        mActivePrograms.set(variant.key);
    }
    lock.unlock();
    if (isSharedVariant(variant)) {
        FMaterial const* const pDefaultMaterial = mEngine.getDefaultMaterial();
        if (pDefaultMaterial && pDefaultMaterial->mCachedPrograms[variant.key]) {
            return pDefaultMaterial->getProgram(variant);
        }
    }
#endif
    assert_invariant(mCachedPrograms[variant.key]);
    return mCachedPrograms[variant.key];
}

/**
 * 销毁着色器程序
 * 
 * 销毁匹配变体掩码和值的缓存着色器程序。
 * 
 * @param engine 引擎引用
 * @param variantMask 变体掩码（指定要检查哪些变体位）
 * @param variantValue 变体值（指定这些位的值）
 * 
 * 特殊处理：
 * - 对于默认材质或有自定义深度着色器的材质，销毁所有匹配的变体
 * - 对于其他材质，深度变体可能从默认材质共享，不应销毁
 * - 仅在有有效句柄时销毁，避免不必要的命令队列流量
 */
void FMaterial::destroyPrograms(FEngine& engine,
        Variant::type_t const variantMask, Variant::type_t const variantValue) {

    DriverApi& driverApi = engine.getDriverApi();
    auto& cachedPrograms = mCachedPrograms;

    /**
     * 根据材质域采用不同的销毁策略
     */
    switch (mDefinition.materialDomain) {
        case MaterialDomain::SURFACE: {
            /**
             * Surface 材质域的处理
             */
            if (mIsDefaultMaterial || mDefinition.hasCustomDepthShader) {
                /**
                 * 默认材质或有自定义深度着色器的材质：
                 * 销毁所有匹配的变体，因为这些材质拥有所有程序的所有权。
                 */
                for (size_t k = 0, n = VARIANT_COUNT; k < n; ++k) {
                    /**
                     * 检查变体是否匹配掩码和值
                     */
                    if ((k & variantMask) == variantValue) {
                        /**
                         * 仅在有有效句柄时销毁。
                         * 这不是严格必需的，但我们有很多变体，
                         * 这会在命令队列中产生流量。
                         */
                        if (cachedPrograms[k]) {
                            driverApi.destroyProgram(std::move(cachedPrograms[k]));
                        }
                    }
                }
            } else {
                /**
                 * 非默认材质且没有自定义深度着色器：
                 * 深度变体可能与默认材质共享，在这种情况下我们不应该释放它们。
                 * 
                 * 在 Engine::shutdown() 期间，自动清理会首先销毁默认材质，
                 * 所以这可能是 null，但这仅用于调试。
                 */
                UTILS_UNUSED_IN_RELEASE
                auto const UTILS_NULLABLE pDefaultMaterial = engine.getDefaultMaterial();

                for (size_t k = 0, n = VARIANT_COUNT; k < n; ++k) {
                    /**
                     * 检查变体是否匹配掩码和值
                     */
                    if ((k & variantMask) == variantValue) {
                        /**
                         * 仅在有有效句柄时销毁
                         */
                        if (cachedPrograms[k]) {
                            if (Variant::isValidDepthVariant(Variant(k))) {
                                /**
                                 * 这是深度变体，从默认材质共享。
                                 * 根据构造，这应该总是 true，因为此字段仅在材质为此变体创建程序时填充。
                                 * 
                                 * 在 Engine::shutdown 期间，自动清理会首先销毁默认材质。
                                 */
                                assert_invariant(!pDefaultMaterial ||
                                        pDefaultMaterial->mCachedPrograms[k]);
                                /**
                                 * 我们不拥有此变体，跳过销毁，但清空条目。
                                 */
                                cachedPrograms[k].clear();
                                continue;
                            }

                            /**
                             * 非共享变体，正常销毁
                             */
                            driverApi.destroyProgram(std::move(cachedPrograms[k]));
                        }
                    }
                }
            }
            break;
        }
        case MaterialDomain::POST_PROCESS: {
            /**
             * Post-Process 材质域：
             * 遍历所有 Post-Process 变体并销毁匹配的。
             */
            for (size_t k = 0, n = POST_PROCESS_VARIANT_COUNT; k < n; ++k) {
                if ((k & variantMask) == variantValue) {
                    /**
                     * 仅在有有效句柄时销毁
                     */
                    if (cachedPrograms[k]) {
                        driverApi.destroyProgram(std::move(cachedPrograms[k]));
                    }
                }
            }
            break;
        }
        case MaterialDomain::COMPUTE: {
            /**
             * Compute 材质域：
             * Compute 程序没有变体，总是使用索引 0。
             */
            driverApi.destroyProgram(std::move(cachedPrograms[0]));
            break;
        }
    }
}

/**
 * 获取特化常量 ID
 * 
 * 根据名称查找特化常量的 ID。
 * 
 * @param name 常量名称
 * @return 常量 ID（如果找到），否则返回 std::nullopt
 * 
 * 注意：返回的 ID 已加上 CONFIG_MAX_RESERVED_SPEC_CONSTANTS 偏移量。
 */
std::optional<uint32_t> FMaterial::getSpecializationConstantId(std::string_view const name) const noexcept {
    auto const pos = mDefinition.specializationConstantsNameToIndex.find(name);
    if (pos != mDefinition.specializationConstantsNameToIndex.end()) {
        return pos->second + CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
    }
    return std::nullopt;
}

/**
 * 设置特化常量值
 * 
 * 设置指定 ID 的特化常量值。仅在值发生变化时更新。
 * 
 * @tparam T 常量类型（int32_t、float 或 bool）
 * @param id 常量 ID
 * @param value 常量值
 * @return 如果成功设置返回 true，否则返回 false
 * 
 * 验证：
 * - ID 必须在有效范围内
 * - 对于材质常量（非保留常量），类型必须匹配
 */
template<typename T, typename>
bool FMaterial::setConstant(uint32_t id, T value) noexcept {
    /**
     * 检查 ID 是否在有效范围内
     */
    if (UTILS_UNLIKELY(id >= mSpecializationConstants.size())) {
        return false;
    }

    /**
     * 对于材质常量（非保留常量），验证类型匹配
     */
    if (id >= CONFIG_MAX_RESERVED_SPEC_CONSTANTS) {
        /**
         * 这是来自材质本身的常量（相对于保留常量）。
         * 需要验证提供的值类型与材质定义中的类型匹配。
         */
        auto const& constant =
                mDefinition.materialConstants[id - CONFIG_MAX_RESERVED_SPEC_CONSTANTS];
        using ConstantType = ConstantType;
        switch (constant.type) {
            case ConstantType::INT:
                /**
                 * 常量类型是 int，提供的值必须是 int32_t
                 */
                if (!std::is_same_v<T, int32_t>) return false;
                break;
            case ConstantType::FLOAT:
                /**
                 * 常量类型是 float，提供的值必须是 float
                 */
                if (!std::is_same_v<T, float>) return false;
                break;
            case ConstantType::BOOL:
                /**
                 * 常量类型是 bool，提供的值必须是 bool
                 */
                if (!std::is_same_v<T, bool>) return false;
                break;
        }
    }

    /**
     * 仅在值发生变化时更新，避免不必要的重编译
     */
    if (std::get<T>(mSpecializationConstants[id]) != value) {
        mSpecializationConstants[id] = value;
        return true;  // 值已更改
    }
    return false;  // 值未更改
}

/**
 * 处理特化常量
 * 
 * 处理构建器中的特化常量，包括保留常量和用户定义的常量。
 * 
 * @param builder 材质构建器
 * @return 特化常量向量
 * 
 * 处理流程：
 * 1. 从材质定义复制特化常量
 * 2. 设置保留常量（球谐函数频带数、阴影采样方法）
 * 3. 验证并设置用户定义的常量特化
 * 4. 验证常量类型匹配
 */
FixedCapacityVector<Program::SpecializationConstant>
FMaterial::processSpecializationConstants(Builder const& builder) {
    /**
     * 从材质定义复制特化常量（包含保留常量的占位符）
     */
    FixedCapacityVector<Program::SpecializationConstant> specializationConstants =
            mDefinition.specializationConstants;

    /**
     * 设置保留的特化常量（这些是引擎级别的常量，不是用户定义的）
     */
    /**
     * 设置球谐函数频带数（用于 IBL）
     */
    specializationConstants[+ReservedSpecializationConstants::CONFIG_SH_BANDS_COUNT] =
            builder->mShBandsCount;
    /**
     * 设置阴影采样方法（LOW、MEDIUM、HIGH）
     */
    specializationConstants[+ReservedSpecializationConstants::CONFIG_SHADOW_SAMPLING_METHOD] =
            int32_t(builder->mShadowSamplingQuality);

    /**
     * 验证所有常量特化都存在于材质中，并且它们的类型匹配。
     * 这确保用户在构建器中提供的常量值有效。
     */
    for (auto const& [name, value] : builder->mConstantSpecializations) {
        /**
         * 将 CString 转换为 string_view 以便查找
         */
        std::string_view const key{ name.data(), name.size() };
        /**
         * 在材质定义中查找常量索引
         */
        auto pos = mDefinition.specializationConstantsNameToIndex.find(key);
        /**
         * 验证常量存在
         */
        FILAMENT_CHECK_PRECONDITION(pos != mDefinition.specializationConstantsNameToIndex.end())
                << "The material " << mDefinition.name.c_str_safe()
                << " does not have a constant parameter named " << name.c_str() << ".";
        
        /**
         * 用于错误消息的类型名称
         */
        constexpr char const* const types[3] = {"an int", "a float", "a bool"};
        /**
         * 获取材质定义中的常量信息
         */
        auto const& constant = mDefinition.materialConstants[pos->second];
        /**
         * 验证类型匹配
         */
        switch (constant.type) {
            case ConstantType::INT:
                /**
                 * 常量类型是 int，值必须是 int32_t
                 */
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<int32_t>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type int, but "
                        << types[value.index()] << " was provided.";
                break;
            case ConstantType::FLOAT:
                /**
                 * 常量类型是 float，值必须是 float
                 */
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<float>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type float, but "
                        << types[value.index()] << " was provided.";
                break;
            case ConstantType::BOOL:
                /**
                 * 常量类型是 bool，值必须是 bool
                 */
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<bool>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type bool, but "
                        << types[value.index()] << " was provided.";
                break;
        }
        /**
         * 计算最终的常量索引（材质常量索引 + 保留常量偏移量）
         */
        uint32_t const index = pos->second + CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
        /**
         * 设置特化常量值
         */
        specializationConstants[index] = value;
    }
    return specializationConstants;
}

// 预缓存深度变体，优化首次渲染性能
// 对于默认材质：预编译所有深度变体
// 对于其他材质：从默认材质继承深度变体（如果没有自定义深度着色器）
void FMaterial::precacheDepthVariants(FEngine& engine) {

    // 检查是否需要禁用默认材质的深度预缓存（某些驱动的工作around）
    bool const disableDepthPrecacheForDefaultMaterial = engine.getDriverApi().isWorkaroundNeeded(
                               Workaround::DISABLE_DEPTH_PRECACHE_FOR_DEFAULT_MATERIAL);

    // 如果是默认材质，预编译所有深度变体
    // 注意：这是可选的优化；如果移除预缓存，这些变体会在首次需要时通过 createAndCacheProgram() 创建
    // 预缓存的代价：使用更多内存，增加初始化时间
    // 预缓存的收益：减少第一帧的卡顿（避免首次渲染时的编译延迟）
    if (UTILS_UNLIKELY(mIsDefaultMaterial && !disableDepthPrecacheForDefaultMaterial)) {
        const bool stereoSupported = mEngine.getDriverApi().isStereoSupported();
        // 获取所有有效的深度变体列表
        auto const allDepthVariants = VariantUtils::getDepthVariants();
        for (auto const variant: allDepthVariants) {
            // 如果不支持立体渲染，跳过立体变体
            if (!stereoSupported && Variant::isStereoVariant(variant)) {
                continue;
            }
            assert_invariant(Variant::isValidDepthVariant(variant));
            // 如果材质包中包含此变体的着色器，则预编译
            if (hasVariant(variant)) {
                prepareProgram(variant, CompilerPriorityQueue::HIGH);
            }
        }
        return;
    }

    // 对于非默认材质，如果可能，从默认材质继承深度变体
    // 条件：Surface 材质域、非默认材质、没有自定义深度着色器
    if (mDefinition.materialDomain == MaterialDomain::SURFACE &&
            !mIsDefaultMaterial &&
            !mDefinition.hasCustomDepthShader) {
        FMaterial const* const pDefaultMaterial = engine.getDefaultMaterial();
        assert_invariant(pDefaultMaterial);
        // 获取所有深度变体，直接从默认材质复制程序句柄
        auto const allDepthVariants = VariantUtils::getDepthVariants();
        for (auto const variant: allDepthVariants) {
            assert_invariant(Variant::isValidDepthVariant(variant));
            // 直接共享默认材质的程序，避免重复编译
            mCachedPrograms[variant.key] = pDefaultMaterial->mCachedPrograms[variant.key];
        }
    }
}

/**
 * 获取采样器绑定
 * 
 * 获取指定名称的采样器的描述符绑定索引。
 * 
 * @param name 采样器名称
 * @return 描述符绑定索引
 */
descriptor_binding_t FMaterial::getSamplerBinding(
        std::string_view const& name) const {
    return mDefinition.samplerInterfaceBlock.getSamplerInfo(name)->binding;
}

/**
 * 获取参数变换名称
 * 
 * 获取指定采样器参数的变换矩阵名称（用于 UV 变换）。
 * 
 * @param samplerName 采样器名称
 * @return 变换名称（如果存在），否则返回 nullptr
 */
const char* FMaterial::getParameterTransformName(std::string_view samplerName) const noexcept {
    auto const& sib = getSamplerInterfaceBlock();
    SamplerInterfaceBlock::SamplerInfo const* info = sib.getSamplerInfo(samplerName);
    if (!info || info->transformName.empty()) {
        return nullptr;
    }
    return info->transformName.c_str();
}

template bool FMaterial::setConstant<int32_t>(uint32_t id, int32_t value) noexcept;
template bool FMaterial::setConstant<float>(uint32_t id, float value) noexcept;
template bool FMaterial::setConstant<bool>(uint32_t id, bool value) noexcept;

} // namespace filament
