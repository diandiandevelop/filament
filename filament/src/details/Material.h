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

#ifndef TNT_FILAMENT_DETAILS_MATERIAL_H
#define TNT_FILAMENT_DETAILS_MATERIAL_H

#include "downcast.h"

#include "details/MaterialInstance.h"

#include "ds/DescriptorSetLayout.h"

#include <filament/Material.h>
#include <filament/MaterialEnums.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>
#include <private/filament/Variant.h>
#include <private/filament/ConstantInfo.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/Program.h>

#include <utils/compiler.h>
#include <utils/CString.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Invocable.h>
#include <utils/Mutex.h>

#include <array>
#include <optional>
#include <string_view>

#include <stddef.h>
#include <stdint.h>

#if FILAMENT_ENABLE_MATDBG
#include <matdbg/DebugServer.h>
#endif

namespace filament {

class MaterialParser;

class  FEngine;

/**
 * 材质实现类
 * 
 * 材质定义了渲染表面的外观和行为。
 * 包含着色器程序、uniform 接口块、sampler 接口块、渲染状态等。
 * 
 * 实现细节：
 * - 支持多种着色器变体（基于光照、阴影等条件）
 * - 缓存编译后的着色器程序
 * - 支持共享变体（使用默认材质的变体）
 * - 支持 UBO 批处理
 */
class FMaterial : public Material {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     * @param definition 材质定义（包含所有材质属性）
     */
    FMaterial(FEngine& engine, const Builder& builder,
            MaterialDefinition const& definition);
    
    /**
     * 析构函数
     */
    ~FMaterial() noexcept;

    /**
     * 默认材质构建器
     * 
     * 用于创建默认材质（错误情况下的回退材质）。
     */
    class DefaultMaterialBuilder : public Builder {
    public:
        DefaultMaterialBuilder();
    };

    /**
     * 终止
     * 
     * 释放资源，包括所有缓存的着色器程序。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);

    /**
     * 获取统一接口块
     * 
     * 返回材质的 uniform 接口块，包含所有 uniform 参数的定义。
     * 
     * @return uniform 接口块常量引用
     */
    const BufferInterfaceBlock& getUniformInterfaceBlock() const noexcept {
        return mDefinition.uniformInterfaceBlock;
    }

    /**
     * 获取每视图描述符集布局
     * 
     * 仅用于后处理材质。
     * 
     * @return 每视图描述符集布局常量引用
     */
    DescriptorSetLayout const& getPerViewDescriptorSetLayout() const noexcept {
        assert_invariant(mDefinition.materialDomain == MaterialDomain::POST_PROCESS);
        return mDefinition.perViewDescriptorSetLayout;
    }

    /**
     * 获取每视图描述符集布局（带变体和 VSM 选项）
     * 
     * @param variant 着色器变体
     * @param useVsmDescriptorSetLayout 是否使用 VSM 描述符集布局
     * @return 每视图描述符集布局常量引用
     */
    DescriptorSetLayout const& getPerViewDescriptorSetLayout(
            Variant const variant, bool const useVsmDescriptorSetLayout) const noexcept;

    /**
     * 获取描述符集布局
     * 
     * 返回在将材质绑定到管线时应使用的布局。
     * 共享变体使用引擎的默认材质的变体，因此也应使用默认材质的布局。
     * 
     * @param variant 着色器变体（默认为空，使用默认变体）
     * @return 描述符集布局常量引用
     */
    DescriptorSetLayout const& getDescriptorSetLayout(Variant variant = {}) const noexcept {
        if (!isSharedVariant(variant)) {  // 如果不是共享变体
            return mDefinition.descriptorSetLayout;  // 返回材质自己的布局
        }
        FMaterial const* const pDefaultMaterial = mEngine.getDefaultMaterial();  // 获取默认材质
        if (UTILS_UNLIKELY(!pDefaultMaterial)) {  // 如果默认材质不存在
            return mDefinition.descriptorSetLayout;  // 返回材质自己的布局
        }
        return pDefaultMaterial->getDescriptorSetLayout();  // 返回默认材质的布局
    }

    /**
     * 编译材质
     * 
     * 异步编译材质的着色器程序。
     * 
     * @param priority 编译优先级队列
     * @param variantSpec 变体规范（指定要编译的变体）
     * @param handler 回调处理器
     * @param callback 完成回调函数
     */
    void compile(CompilerPriorityQueue priority,
            UserVariantFilterMask variantSpec,
            backend::CallbackHandler* handler,
            utils::Invocable<void(Material*)>&& callback) noexcept;

    /**
     * 创建实例
     * 
     * 创建此材质的实例，指定批处理模式。
     * 
     * @param name 实例名称
     * @return 材质实例指针
     */
    FMaterialInstance* createInstance(const char* name) const noexcept;

    /**
     * 是否有参数
     * 
     * 检查材质是否有指定名称的参数。
     * 
     * @param name 参数名称
     * @return 如果参数存在返回 true，否则返回 false
     */
    bool hasParameter(const char* name) const noexcept;

    /**
     * 是否为采样器
     * 
     * 检查指定名称的参数是否为采样器。
     * 
     * @param name 参数名称
     * @return 如果是采样器返回 true，否则返回 false
     */
    bool isSampler(const char* name) const noexcept;

    /**
     * 反射
     * 
     * 获取参数的反射信息。
     * 
     * @param name 参数名称
     * @return 字段信息指针，如果未找到返回 nullptr
     */
    BufferInterfaceBlock::FieldInfo const* reflect(std::string_view name) const noexcept;

    FMaterialInstance const* getDefaultInstance() const noexcept {
        return const_cast<FMaterial*>(this)->getDefaultInstance();
    }

    FMaterialInstance* getDefaultInstance() noexcept;

    FEngine& getEngine() const noexcept  { return mEngine; }

    bool isCached(Variant const variant) const noexcept {
        return bool(mCachedPrograms[variant.key]);
    }

    void invalidate(Variant::type_t variantMask = 0, Variant::type_t variantValue = 0) noexcept;

    /**
     * 准备程序
     * 
     * 在后端级别为材质的给定变体创建程序。
     * 必须在后端渲染通道外调用。
     * 必须在 getProgram() 之前调用。
     * 
     * 注意：prepareProgram() 为场景中的每个 RenderPrimitive 调用，因此必须高效。
     * 
     * @param variant 着色器变体
     * @param priorityQueue 编译优先级队列
     */
    void prepareProgram(Variant const variant,
            backend::CompilerPriorityQueue const priorityQueue) const noexcept {
        // prepareProgram() 为场景中的每个 RenderPrimitive 调用，因此必须高效
        if (UTILS_UNLIKELY(!isCached(variant))) {  // 如果未缓存（不常见情况）
            prepareProgramSlow(variant, priorityQueue);  // 调用慢速路径
        }
    }

    /**
     * 获取程序
     * 
     * 返回材质的给定变体的后端程序。
     * 必须在 prepareProgram() 之后调用。
     * 
     * @param variant 着色器变体
     * @return 程序句柄
     */
    [[nodiscard]]
    backend::Handle<backend::HwProgram> getProgram(Variant const variant) const noexcept {
#if FILAMENT_ENABLE_MATDBG
        return getProgramWithMATDBG(variant);
#endif
        assert_invariant(mCachedPrograms[variant.key]);
        return mCachedPrograms[variant.key];
    }

    /**
     * 使用共享
     * 
     * MaterialInstance::use() 在绘制前绑定描述符集。
     * 对于共享变体，材质实例将调用 useShared() 来绑定默认材质的集合。
     * 
     * @param driver 驱动 API 引用
     * @param variant 着色器变体
     * @return 如果这是共享变体返回 true，否则返回 false
     */
    bool useShared(backend::DriverApi& driver, Variant variant) const noexcept {
        if (!isSharedVariant(variant)) {  // 如果不是共享变体
            return false;  // 返回 false
        }
        FMaterial const* const pDefaultMaterial = mEngine.getDefaultMaterial();  // 获取默认材质
        if (UTILS_UNLIKELY(!pDefaultMaterial)) {  // 如果默认材质不存在
            return false;  // 返回 false
        }
        FMaterialInstance const* const pDefaultInstance = pDefaultMaterial->getDefaultInstance();  // 获取默认实例
        pDefaultInstance->use(driver, variant);  // 使用默认实例
        return true;  // 返回 true
    }

    [[nodiscard]]
    backend::Handle<backend::HwProgram> getProgramWithMATDBG(Variant variant) const noexcept;

    bool isVariantLit() const noexcept { return mDefinition.isVariantLit; }

    const utils::CString& getName() const noexcept { return mDefinition.name; }
    backend::FeatureLevel getFeatureLevel() const noexcept { return mDefinition.featureLevel; }
    backend::RasterState getRasterState() const noexcept  { return mDefinition.rasterState; }
    uint32_t getId() const noexcept { return mMaterialId; }

    UserVariantFilterMask getSupportedVariants() const noexcept {
        return UserVariantFilterMask(UserVariantFilterBit::ALL) & ~mDefinition.variantFilterMask;
    }

    Shading getShading() const noexcept { return mDefinition.shading; }
    Interpolation getInterpolation() const noexcept { return mDefinition.interpolation; }
    BlendingMode getBlendingMode() const noexcept { return mDefinition.blendingMode; }
    VertexDomain getVertexDomain() const noexcept { return mDefinition.vertexDomain; }
    MaterialDomain getMaterialDomain() const noexcept { return mDefinition.materialDomain; }
    CullingMode getCullingMode() const noexcept { return mDefinition.cullingMode; }
    TransparencyMode getTransparencyMode() const noexcept { return mDefinition.transparencyMode; }
    bool isColorWriteEnabled() const noexcept { return mDefinition.rasterState.colorWrite; }
    bool isDepthWriteEnabled() const noexcept { return mDefinition.rasterState.depthWrite; }
    bool isDepthCullingEnabled() const noexcept {
        return mDefinition.rasterState.depthFunc != backend::RasterState::DepthFunc::A;
    }
    bool isDoubleSided() const noexcept { return mDefinition.doubleSided; }
    bool hasDoubleSidedCapability() const noexcept { return mDefinition.doubleSidedCapability; }
    bool isAlphaToCoverageEnabled() const noexcept { return mDefinition.rasterState.alphaToCoverage; }
    float getMaskThreshold() const noexcept { return mDefinition.maskThreshold; }
    bool hasShadowMultiplier() const noexcept { return mDefinition.hasShadowMultiplier; }
    AttributeBitset getRequiredAttributes() const noexcept { return mDefinition.requiredAttributes; }
    RefractionMode getRefractionMode() const noexcept { return mDefinition.refractionMode; }
    RefractionType getRefractionType() const noexcept { return mDefinition.refractionType; }
    ReflectionMode getReflectionMode() const noexcept { return mDefinition.reflectionMode; }

    bool hasSpecularAntiAliasing() const noexcept { return mDefinition.specularAntiAliasing; }
    float getSpecularAntiAliasingVariance() const noexcept { return mDefinition.specularAntiAliasingVariance; }
    float getSpecularAntiAliasingThreshold() const noexcept { return mDefinition.specularAntiAliasingThreshold; }

    backend::descriptor_binding_t getSamplerBinding(
            std::string_view const& name) const;

    const char* getParameterTransformName(std::string_view samplerName) const noexcept;

    bool hasMaterialProperty(Property property) const noexcept {
        return bool(mDefinition.materialProperties & uint64_t(property));
    }

    SamplerInterfaceBlock const& getSamplerInterfaceBlock() const noexcept {
        return mDefinition.samplerInterfaceBlock;
    }

    size_t getParameterCount() const noexcept {
        return mDefinition.uniformInterfaceBlock.getFieldInfoList().size() +
               mDefinition.samplerInterfaceBlock.getSamplerInfoList().size() +
               (mDefinition.subpassInfo.isValid ? 1 : 0);
    }
    size_t getParameters(ParameterInfo* parameters, size_t count) const noexcept;

    uint32_t generateMaterialInstanceId() const noexcept { return mMaterialInstanceId++; }

    void destroyPrograms(FEngine& engine,
            Variant::type_t variantMask = 0,
            Variant::type_t variantValue = 0);

    // return the id of a specialization constant specified by name for this material
    std::optional<uint32_t> getSpecializationConstantId(std::string_view name) const noexcept ;

    // Sets a specialization constant by id. call is no-op if the id is invalid.
    // Return true is the value was changed.
    template<typename T, typename = Builder::is_supported_constant_parameter_t<T>>
    bool setConstant(uint32_t id, T value) noexcept;

    uint8_t getPerViewLayoutIndex() const noexcept {
        return mDefinition.perViewLayoutIndex;
    }

    bool useUboBatching() const noexcept {
        return mUseUboBatching;
    }

    std::string_view getSource() const noexcept {
        return mDefinition.source.c_str_safe();
    }

#if FILAMENT_ENABLE_MATDBG
    void applyPendingEdits() noexcept;

    /**
     * Callback handlers for the debug server, potentially called from any thread. The userdata
     * argument has the same value that was passed to DebugServer::addMaterial(), which should
     * be an instance of the public-facing Material.
     * @{
     */

    /** Replaces the material package. */
    static void onEditCallback(void* userdata, const utils::CString& name, const void* packageData,
            size_t packageSize);

    /**
     * Returns a list of "active" variants.
     *
     * This works by checking which variants have been accessed since the previous call, then
     * clearing out the internal list.  Note that the active vs inactive status is merely a visual
     * indicator in the matdbg UI, and that it gets updated about every second.
     */
    static void onQueryCallback(void* userdata, VariantList* pActiveVariants);

    void checkProgramEdits() noexcept {
        if (UTILS_UNLIKELY(hasPendingEdits())) {
            applyPendingEdits();
        }
    }

    /** @}*/
#endif

private:
    MaterialParser const& getMaterialParser() const noexcept;

    bool hasVariant(Variant variant) const noexcept;
    void prepareProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    void getSurfaceProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    void getPostProcessProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    backend::Program getProgramWithVariants(Variant variant,
            Variant vertexVariant, Variant fragmentVariant) const;

    utils::FixedCapacityVector<backend::Program::SpecializationConstant>
            processSpecializationConstants(Builder const& builder);

    void precacheDepthVariants(FEngine& engine);

    void createAndCacheProgram(backend::Program&& p, Variant variant) const noexcept;

    inline bool isSharedVariant(Variant const variant) const {
        return (mDefinition.materialDomain == MaterialDomain::SURFACE) && !mIsDefaultMaterial &&
               !mDefinition.hasCustomDepthShader && Variant::isValidDepthVariant(variant);
    }

    mutable std::array<backend::Handle<backend::HwProgram>, VARIANT_COUNT> mCachedPrograms;
    MaterialDefinition const& mDefinition;

    bool mIsDefaultMaterial = false;

    bool mUseUboBatching = false;

    // reserve some space to construct the default material instance
    mutable FMaterialInstance* mDefaultMaterialInstance = nullptr;

    // current specialization constants for the HwProgram
    utils::FixedCapacityVector<backend::Program::SpecializationConstant> mSpecializationConstants;

#if FILAMENT_ENABLE_MATDBG
    matdbg::MaterialKey mDebuggerId;
    mutable utils::Mutex mActiveProgramsLock;
    mutable VariantList mActivePrograms;
    mutable utils::Mutex mPendingEditsLock;
    std::unique_ptr<MaterialParser> mPendingEdits;
    std::unique_ptr<MaterialParser> mEditedMaterialParser;
    void setPendingEdits(std::unique_ptr<MaterialParser> pendingEdits) noexcept;
    bool hasPendingEdits() const noexcept;
    void latchPendingEdits() noexcept;
#endif

    FEngine& mEngine;
    const uint32_t mMaterialId;
    mutable uint32_t mMaterialInstanceId = 0;
};


FILAMENT_DOWNCAST(Material)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_MATERIAL_H
