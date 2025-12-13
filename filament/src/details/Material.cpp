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


const char* toString(ShaderModel model) {
    switch (model) {
        case ShaderModel::MOBILE:
            return "mobile";
        case ShaderModel::DESKTOP:
            return "desktop";
    }
}

Material* Material::Builder::build(Engine& engine) const {
    MaterialDefinition* r = downcast(engine).getMaterialCache().acquire(downcast(engine),
            mImpl->mPayload, mImpl->mSize);
    if (r) {
        return downcast(engine).createMaterial(*this, *r);
    }
    return nullptr;
}

FMaterial::FMaterial(FEngine& engine, const Builder& builder, MaterialDefinition const& definition)
        : mDefinition(definition),
          mIsDefaultMaterial(builder->mDefaultMaterial),
          mUseUboBatching(shouldEnableBatching(engine, builder->mUboBatchingMode,
                  definition.materialDomain)),
          mEngine(engine),
          mMaterialId(engine.getMaterialId()) {
    FILAMENT_CHECK_PRECONDITION(!mUseUboBatching || engine.isUboBatchingEnabled())
            << "UBO batching is not enabled.";
    mSpecializationConstants = processSpecializationConstants(builder);
    precacheDepthVariants(engine);

#if FILAMENT_ENABLE_MATDBG
    // Register the material with matdbg.
    matdbg::DebugServer* server = downcast(engine).debug.server;
    if (UTILS_UNLIKELY(server)) {
        auto const details = builder.mImpl;
        mDebuggerId = server->addMaterial(mDefinition.name, details->mPayload, details->mSize, this);
    }
#endif
}

FMaterial::~FMaterial() noexcept = default;

void FMaterial::invalidate(Variant::type_t variantMask, Variant::type_t variantValue) noexcept {
    // Note: This API is not public at the moment, so it's okay to have some debugging logs
    // and extra checks.
    if (mDefinition.materialDomain == MaterialDomain::SURFACE &&
            !mIsDefaultMaterial &&
            !mDefinition.hasCustomDepthShader) {
        // it would be unsafe to invalidate any of the cached depth variant
        if (UTILS_UNLIKELY(!((variantMask & Variant::DEP) && !(variantValue & Variant::DEP)))) {
            char variantMaskString[16];
            snprintf(variantMaskString, sizeof(variantMaskString), "%#x", +variantMask);
            char variantValueString[16];
            snprintf(variantValueString, sizeof(variantValueString), "%#x", +variantValue);
            LOG(WARNING) << "FMaterial::invalidate(" << variantMaskString << ", "
                         << variantValueString << ") would corrupt the depth variant cache";
        }
        variantMask |= Variant::DEP;
        variantValue &= ~Variant::DEP;
    }
    destroyPrograms(mEngine, variantMask, variantValue);
}

void FMaterial::terminate(FEngine& engine) {
    if (mDefaultMaterialInstance) {
        mDefaultMaterialInstance->setDefaultInstance(false);
        engine.destroy(mDefaultMaterialInstance);
        mDefaultMaterialInstance = nullptr;
    }

    // ensure we've destroyed all instances before destroying the material
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
    // Unregister the material with matdbg.
    matdbg::DebugServer* server = engine.debug.server;
    if (UTILS_UNLIKELY(server)) {
        server->removeMaterial(mDebuggerId);
    }
#endif

    destroyPrograms(engine);
    engine.getMaterialCache().release(engine, mDefinition);
}

filament::DescriptorSetLayout const& FMaterial::getPerViewDescriptorSetLayout(
        Variant const variant, bool const useVsmDescriptorSetLayout) const noexcept {
    if (mDefinition.materialDomain == MaterialDomain::SURFACE) {
        // `variant` is only sensical for MaterialDomain::SURFACE
        if (Variant::isValidDepthVariant(variant)) {
            return mEngine.getPerViewDescriptorSetLayoutDepthVariant();
        }
        if (Variant::isSSRVariant(variant)) {
            return mEngine.getPerViewDescriptorSetLayoutSsrVariant();
        }
    }
    // mDefinition.perViewDescriptorSetLayout{Vsm} is already resolved for MaterialDomain
    if (useVsmDescriptorSetLayout) {
        return mDefinition.perViewDescriptorSetLayoutVsm;
    }
    return mDefinition.perViewDescriptorSetLayout;
}

// 异步编译材质的所有指定变体
// priority: 编译优先级队列（HIGH 或 LOW）
// variantSpec: 要编译的变体掩码（UserVariantFilterMask）
// handler: 回调处理器（用于线程间通信）
// callback: 编译完成后的回调函数（在主线程调用）
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

FMaterialInstance* FMaterial::createInstance(const char* name) const noexcept {
    if (mDefaultMaterialInstance) {
        // if we have a default instance, use it to create a new one
        return FMaterialInstance::duplicate(mDefaultMaterialInstance, name);
    } else {
        // but if we don't, just create an instance with all the default parameters
        return mEngine.createMaterialInstance(this, name);
    }
}

FMaterialInstance* FMaterial::getDefaultInstance() noexcept {
    if (UTILS_UNLIKELY(!mDefaultMaterialInstance)) {
        mDefaultMaterialInstance =
                mEngine.createMaterialInstance(this, mDefinition.name.c_str());
        mDefaultMaterialInstance->setDefaultInstance(true);
    }
    return mDefaultMaterialInstance;
}

bool FMaterial::hasParameter(const char* name) const noexcept {
    return mDefinition.uniformInterfaceBlock.hasField(name) ||
           mDefinition.samplerInterfaceBlock.hasSampler(name) ||
            mDefinition.subpassInfo.name == CString(name);
}

bool FMaterial::isSampler(const char* name) const noexcept {
    return mDefinition.samplerInterfaceBlock.hasSampler(name);
}

BufferInterfaceBlock::FieldInfo const* FMaterial::reflect(
        std::string_view const name) const noexcept {
    return mDefinition.uniformInterfaceBlock.getFieldInfo(name);
}

MaterialParser const& FMaterial::getMaterialParser() const noexcept {
#if FILAMENT_ENABLE_MATDBG
    if (mEditedMaterialParser) {
        return *mEditedMaterialParser;
    }
#endif
    return mDefinition.getMaterialParser();
}

bool FMaterial::hasVariant(Variant const variant) const noexcept {
    Variant vertexVariant, fragmentVariant;
    switch (getMaterialDomain()) {
        case MaterialDomain::SURFACE:
            vertexVariant = Variant::filterVariantVertex(variant);
            fragmentVariant = Variant::filterVariantFragment(variant);
            break;
        case MaterialDomain::POST_PROCESS:
            vertexVariant = fragmentVariant = variant;
            break;
        case MaterialDomain::COMPUTE:
            // TODO: implement MaterialDomain::COMPUTE
            return false;
    }
    const ShaderModel sm = mEngine.getShaderModel();
    if (!mDefinition.getMaterialParser().hasShader(sm, vertexVariant, ShaderStage::VERTEX)) {
        return false;
    }
    if (!mDefinition.getMaterialParser().hasShader(sm, fragmentVariant, ShaderStage::FRAGMENT)) {
        return false;
    }
    return true;
}

void FMaterial::prepareProgramSlow(Variant const variant,
        backend::CompilerPriorityQueue const priorityQueue) const noexcept {
    assert_invariant(mEngine.hasFeatureLevel(mDefinition.featureLevel));
    switch (getMaterialDomain()) {
        case MaterialDomain::SURFACE:
            getSurfaceProgramSlow(variant, priorityQueue);
            break;
        case MaterialDomain::POST_PROCESS:
            getPostProcessProgramSlow(variant, priorityQueue);
            break;
        case MaterialDomain::COMPUTE:
            // TODO: implement MaterialDomain::COMPUTE
            break;
    }
}

// 为 Surface 材质域创建着色器程序（慢路径，实际编译）
// variant: 完整的变体掩码（包含所有位）
// priorityQueue: 编译优先级队列
void FMaterial::getSurfaceProgramSlow(Variant const variant,
        CompilerPriorityQueue const priorityQueue) const noexcept {
    // filterVariant() 已经在 generateCommands() 中应用，这里不应该再需要
    // 如果是 Unlit 材质，不应该有任何对应 Lit 材质的位
    assert_invariant(variant == Variant::filterVariant(variant, isVariantLit()) );

    // 确保变体不是保留的（无效的）变体
    assert_invariant(!Variant::isReserved(variant));

    // 分别过滤出顶点着色器和片段着色器需要的变体位
    // 顶点着色器只关心：立体、蒙皮、阴影接收、动态光源、方向光
    Variant const vertexVariant   = Variant::filterVariantVertex(variant);
    // 片段着色器只关心：VSM、雾效、阴影接收、动态光源、方向光
    Variant const fragmentVariant = Variant::filterVariantFragment(variant);

    // 从 MaterialParser 获取顶点和片段着色器代码，构建 Program 对象
    Program pb{ getProgramWithVariants(variant, vertexVariant, fragmentVariant) };
    // 设置编译优先级队列
    pb.priorityQueue(priorityQueue);
    // 如果是多视图立体渲染，启用 multiview 扩展
    pb.multiview(
            mEngine.getConfig().stereoscopicType == StereoscopicType::MULTIVIEW &&
            Variant::isStereoVariant(variant));
    // 创建着色器程序并缓存
    createAndCacheProgram(std::move(pb), variant);
}

void FMaterial::getPostProcessProgramSlow(Variant const variant,
        CompilerPriorityQueue const priorityQueue) const noexcept {
    Program pb{ getProgramWithVariants(variant, variant, variant) };
    pb.priorityQueue(priorityQueue);
    createAndCacheProgram(std::move(pb), variant);
}

// 从 MaterialParser 获取顶点和片段着色器代码，构建 Program 对象
// variant: 完整的变体掩码
// vertexVariant: 过滤后的顶点着色器变体
// fragmentVariant: 过滤后的片段着色器变体
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

// 创建着色器程序并缓存到 Material 的 mCachedPrograms 数组中
// p: 已构建的 Program 对象（包含着色器代码和元数据）
// variant: 变体键（用于索引缓存数组）
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

size_t FMaterial::getParameters(ParameterInfo* parameters, size_t count) const noexcept {
    count = std::min(count, getParameterCount());

    const auto& uniforms = mDefinition.uniformInterfaceBlock.getFieldInfoList();
    size_t i = 0;
    size_t const uniformCount = std::min(count, size_t(uniforms.size()));
    for ( ; i < uniformCount; i++) {
        ParameterInfo& info = parameters[i];
        const auto& uniformInfo = uniforms[i];
        info.name = uniformInfo.name.c_str();
        info.isSampler = false;
        info.isSubpass = false;
        info.type = uniformInfo.type;
        info.count = std::max(1u, uniformInfo.size);
        info.precision = uniformInfo.precision;
    }

    const auto& samplers = mDefinition.samplerInterfaceBlock.getSamplerInfoList();
    size_t const samplerCount = samplers.size();
    for (size_t j = 0; i < count && j < samplerCount; i++, j++) {
        ParameterInfo& info = parameters[i];
        const auto& samplerInfo = samplers[j];
        info.name = samplerInfo.name.c_str();
        info.isSampler = true;
        info.isSubpass = false;
        info.samplerType = samplerInfo.type;
        info.count = 1;
        info.precision = samplerInfo.precision;
    }

    if (mDefinition.subpassInfo.isValid && i < count) {
        ParameterInfo& info = parameters[i];
        info.name = mDefinition.subpassInfo.name.c_str();
        info.isSampler = false;
        info.isSubpass = true;
        info.subpassType = mDefinition.subpassInfo.type;
        info.count = 1;
        info.precision = mDefinition.subpassInfo.precision;
    }

    return count;
}

#if FILAMENT_ENABLE_MATDBG

// Swaps in an edited version of the original package that was used to create the material. The
// edited package was stashed in response to a debugger event. This is invoked only when the
// Material Debugger is attached. The only editable features of a material package are the shader
// source strings, so here we trigger a rebuild of the HwProgram objects.
void FMaterial::applyPendingEdits() noexcept {
    const char* name = mDefinition.name.c_str();
    DLOG(INFO) << "Applying edits to " << (name ? name : "(untitled)");
    destroyPrograms(mEngine); // FIXME: this will not destroy the shared variants
    latchPendingEdits();
}

void FMaterial::setPendingEdits(std::unique_ptr<MaterialParser> pendingEdits) noexcept {
    std::lock_guard const lock(mPendingEditsLock);
    std::swap(pendingEdits, mPendingEdits);
}

bool FMaterial::hasPendingEdits() const noexcept {
    std::lock_guard const lock(mPendingEditsLock);
    return bool(mPendingEdits);
}

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

void FMaterial::onQueryCallback(void* userdata, VariantList* pActiveVariants) {
    FMaterial const* material = downcast(static_cast<Material*>(userdata));
    std::lock_guard const lock(material->mActiveProgramsLock);
    *pActiveVariants = material->mActivePrograms;
    material->mActivePrograms.reset();
}

/** @}*/

#endif // FILAMENT_ENABLE_MATDBG

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

void FMaterial::destroyPrograms(FEngine& engine,
        Variant::type_t const variantMask, Variant::type_t const variantValue) {

    DriverApi& driverApi = engine.getDriverApi();
    auto& cachedPrograms = mCachedPrograms;

    switch (mDefinition.materialDomain) {
        case MaterialDomain::SURFACE: {
            if (mIsDefaultMaterial || mDefinition.hasCustomDepthShader) {
                // default material, or we have custom depth shaders, we destroy all variants
                for (size_t k = 0, n = VARIANT_COUNT; k < n; ++k) {
                    if ((k & variantMask) == variantValue) {
                        // Only destroy if the handle is valid. Not strictly needed, but we have a lot
                        // of variants, and this generates traffic in the command queue.
                        if (cachedPrograms[k]) {
                            driverApi.destroyProgram(std::move(cachedPrograms[k]));
                        }
                    }
                }
            } else {
                // The depth variants may be shared with the default material, in which case
                // we should not free them now.

                // During Engine::shutdown(), auto-cleanup destroys the default material first,
                // so this can be null, but this is only used for debugging.
                UTILS_UNUSED_IN_RELEASE
                auto const UTILS_NULLABLE pDefaultMaterial = engine.getDefaultMaterial();

                for (size_t k = 0, n = VARIANT_COUNT; k < n; ++k) {
                    if ((k & variantMask) == variantValue) {
                        // Only destroy if the handle is valid. Not strictly needed, but we have a lot
                        // of variant, and this generates traffic in the command queue.
                        if (cachedPrograms[k]) {
                            if (Variant::isValidDepthVariant(Variant(k))) {
                                // By construction this should always be true, because this
                                // field is populated only when a material creates the program
                                // for this variant.
                                // During Engine::shutdown, auto-cleanup destroys the
                                // default material first
                                assert_invariant(!pDefaultMaterial ||
                                        pDefaultMaterial->mCachedPrograms[k]);
                                // we don't own this variant, skip, but clear the entry.
                                cachedPrograms[k].clear();
                                continue;
                            }

                            driverApi.destroyProgram(std::move(cachedPrograms[k]));
                        }
                    }
                }
            }
            break;
        }
        case MaterialDomain::POST_PROCESS: {
            for (size_t k = 0, n = POST_PROCESS_VARIANT_COUNT; k < n; ++k) {
                if ((k & variantMask) == variantValue) {
                    // Only destroy if the handle is valid. Not strictly needed, but we have a lot
                    // of variant, and this generates traffic in the command queue.
                    if (cachedPrograms[k]) {
                        driverApi.destroyProgram(std::move(cachedPrograms[k]));
                    }
                }
            }
            break;
        }
        case MaterialDomain::COMPUTE: {
            // Compute programs don't have variants
            driverApi.destroyProgram(std::move(cachedPrograms[0]));
            break;
        }
    }
}

std::optional<uint32_t> FMaterial::getSpecializationConstantId(std::string_view const name) const noexcept {
    auto const pos = mDefinition.specializationConstantsNameToIndex.find(name);
    if (pos != mDefinition.specializationConstantsNameToIndex.end()) {
        return pos->second + CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
    }
    return std::nullopt;
}

template<typename T, typename>
bool FMaterial::setConstant(uint32_t id, T value) noexcept {
    if (UTILS_UNLIKELY(id >= mSpecializationConstants.size())) {
        return false;
    }

    if (id >= CONFIG_MAX_RESERVED_SPEC_CONSTANTS) {
        // Constant from the material itself (as opposed to the reserved ones)
        auto const& constant =
                mDefinition.materialConstants[id - CONFIG_MAX_RESERVED_SPEC_CONSTANTS];
        using ConstantType = ConstantType;
        switch (constant.type) {
            case ConstantType::INT:
                if (!std::is_same_v<T, int32_t>) return false;
                break;
            case ConstantType::FLOAT:
                if (!std::is_same_v<T, float>) return false;
                break;
            case ConstantType::BOOL:
                if (!std::is_same_v<T, bool>) return false;
                break;
        }
    }

    if (std::get<T>(mSpecializationConstants[id]) != value) {
        mSpecializationConstants[id] = value;
        return true;
    }
    return false;
}

FixedCapacityVector<Program::SpecializationConstant>
FMaterial::processSpecializationConstants(Builder const& builder) {
    FixedCapacityVector<Program::SpecializationConstant> specializationConstants =
            mDefinition.specializationConstants;

    specializationConstants[+ReservedSpecializationConstants::CONFIG_SH_BANDS_COUNT] =
            builder->mShBandsCount;
    specializationConstants[+ReservedSpecializationConstants::CONFIG_SHADOW_SAMPLING_METHOD] =
            int32_t(builder->mShadowSamplingQuality);

    // Verify that all the constant specializations exist in the material and that their types
    // match.
    for (auto const& [name, value] : builder->mConstantSpecializations) {
        std::string_view const key{ name.data(), name.size() };
        auto pos = mDefinition.specializationConstantsNameToIndex.find(key);
        FILAMENT_CHECK_PRECONDITION(pos != mDefinition.specializationConstantsNameToIndex.end())
                << "The material " << mDefinition.name.c_str_safe()
                << " does not have a constant parameter named " << name.c_str() << ".";
        constexpr char const* const types[3] = {"an int", "a float", "a bool"};
        auto const& constant = mDefinition.materialConstants[pos->second];
        switch (constant.type) {
            case ConstantType::INT:
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<int32_t>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type int, but "
                        << types[value.index()] << " was provided.";
                break;
            case ConstantType::FLOAT:
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<float>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type float, but "
                        << types[value.index()] << " was provided.";
                break;
            case ConstantType::BOOL:
                FILAMENT_CHECK_PRECONDITION(std::holds_alternative<bool>(value))
                        << "The constant parameter " << name.c_str() << " on material "
                        << mDefinition.name.c_str_safe() << " is of type bool, but "
                        << types[value.index()] << " was provided.";
                break;
        }
        uint32_t const index = pos->second + CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
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

descriptor_binding_t FMaterial::getSamplerBinding(
        std::string_view const& name) const {
    return mDefinition.samplerInterfaceBlock.getSamplerInfo(name)->binding;
}

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
