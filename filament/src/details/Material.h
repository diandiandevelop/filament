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

    /**
     * 获取默认实例（常量版本）
     * 
     * @return 默认材质实例常量指针
     */
    FMaterialInstance const* getDefaultInstance() const noexcept {
        return const_cast<FMaterial*>(this)->getDefaultInstance();
    }

    /**
     * 获取默认实例
     * 
     * @return 默认材质实例指针
     * 
     * 功能：返回材质的默认实例（首次访问时创建）。
     */
    FMaterialInstance* getDefaultInstance() noexcept;

    /**
     * 获取引擎
     * 
     * @return 引擎引用
     */
    FEngine& getEngine() const noexcept  { return mEngine; }

    /**
     * 是否已缓存
     * 
     * @param variant 着色器变体
     * @return 如果变体的程序已缓存返回 true，否则返回 false
     * 
     * 功能：检查指定变体的着色器程序是否已编译并缓存。
     */
    bool isCached(Variant const variant) const noexcept {
        return bool(mCachedPrograms[variant.key]);
    }

    /**
     * 使缓存无效
     * 
     * @param variantMask 变体掩码（指定要检查的变体位）
     * @param variantValue 变体值（指定要无效化的变体值）
     * 
     * 功能：使匹配条件的变体程序缓存无效，下次访问时会重新编译。
     */
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

    /**
     * 获取程序（MATDBG 版本）
     * 
     * @param variant 着色器变体
     * @return 程序句柄
     * 
     * 功能：在启用 MATDBG（材质调试）时使用，支持实时编辑。
     */
    [[nodiscard]]
    backend::Handle<backend::HwProgram> getProgramWithMATDBG(Variant variant) const noexcept;

    /**
     * 变体是否为光照变体
     * 
     * @return 如果是光照变体返回 true，否则返回 false
     */
    bool isVariantLit() const noexcept { return mDefinition.isVariantLit; }

    /**
     * 获取材质名称
     * 
     * @return 材质名称常量引用
     */
    const utils::CString& getName() const noexcept { return mDefinition.name; }
    
    /**
     * 获取功能级别
     * 
     * @return 后端功能级别
     */
    backend::FeatureLevel getFeatureLevel() const noexcept { return mDefinition.featureLevel; }
    
    /**
     * 获取光栅状态
     * 
     * @return 光栅状态（包含剔除、深度、混合等状态）
     */
    backend::RasterState getRasterState() const noexcept  { return mDefinition.rasterState; }
    
    /**
     * 获取材质 ID
     * 
     * @return 材质唯一标识符
     */
    uint32_t getId() const noexcept { return mMaterialId; }

    /**
     * 获取支持的变体
     * 
     * @return 支持的变体掩码
     * 
     * 功能：返回材质支持的所有变体（排除过滤掉的变体）。
     */
    UserVariantFilterMask getSupportedVariants() const noexcept {
        return UserVariantFilterMask(UserVariantFilterBit::ALL) & ~mDefinition.variantFilterMask;
    }

    /**
     * 获取着色模型
     * 
     * @return 着色模型（UNLIT、LIT、SUBSURFACE、CLOTH、SPECULAR_GLOSSINESS 等）
     */
    Shading getShading() const noexcept { return mDefinition.shading; }
    
    /**
     * 获取插值模式
     * 
     * @return 插值模式（SMOOTH、FLAT）
     */
    Interpolation getInterpolation() const noexcept { return mDefinition.interpolation; }
    
    /**
     * 获取混合模式
     * 
     * @return 混合模式（OPAQUE、TRANSPARENT、ADD、MULTIPLY、MASKED、FADE 等）
     */
    BlendingMode getBlendingMode() const noexcept { return mDefinition.blendingMode; }
    
    /**
     * 获取顶点域
     * 
     * @return 顶点域（OBJECT、WORLD、VIEW、DEVICE）
     */
    VertexDomain getVertexDomain() const noexcept { return mDefinition.vertexDomain; }
    
    /**
     * 获取材质域
     * 
     * @return 材质域（SURFACE、POST_PROCESS、COMPUTE）
     */
    MaterialDomain getMaterialDomain() const noexcept { return mDefinition.materialDomain; }
    
    /**
     * 获取剔除模式
     * 
     * @return 剔除模式（NONE、FRONT、BACK、FRONT_AND_BACK）
     */
    CullingMode getCullingMode() const noexcept { return mDefinition.cullingMode; }
    
    /**
     * 获取透明度模式
     * 
     * @return 透明度模式（DEFAULT、TWO_PASSES_ONE_SIDE、TWO_PASSES_TWO_SIDES）
     */
    TransparencyMode getTransparencyMode() const noexcept { return mDefinition.transparencyMode; }
    
    /**
     * 颜色写入是否启用
     * 
     * @return 如果颜色缓冲区写入启用返回 true，否则返回 false
     */
    bool isColorWriteEnabled() const noexcept { return mDefinition.rasterState.colorWrite; }
    
    /**
     * 深度写入是否启用
     * 
     * @return 如果深度缓冲区写入启用返回 true，否则返回 false
     */
    bool isDepthWriteEnabled() const noexcept { return mDefinition.rasterState.depthWrite; }
    
    /**
     * 深度剔除是否启用
     * 
     * @return 如果深度测试启用返回 true，否则返回 false
     */
    bool isDepthCullingEnabled() const noexcept {
        return mDefinition.rasterState.depthFunc != backend::RasterState::DepthFunc::A;
    }
    
    /**
     * 是否双面渲染
     * 
     * @return 如果启用双面渲染返回 true，否则返回 false
     */
    bool isDoubleSided() const noexcept { return mDefinition.doubleSided; }
    
    /**
     * 是否有双面渲染能力
     * 
     * @return 如果材质支持双面渲染返回 true，否则返回 false
     */
    bool hasDoubleSidedCapability() const noexcept { return mDefinition.doubleSidedCapability; }
    
    /**
     * Alpha 到覆盖率是否启用
     * 
     * @return 如果 Alpha 到覆盖率启用返回 true，否则返回 false
     */
    bool isAlphaToCoverageEnabled() const noexcept { return mDefinition.rasterState.alphaToCoverage; }
    
    /**
     * 获取遮罩阈值
     * 
     * @return 遮罩阈值（用于 MASKED 混合模式）
     */
    float getMaskThreshold() const noexcept { return mDefinition.maskThreshold; }
    
    /**
     * 是否有阴影倍增器
     * 
     * @return 如果材质有阴影倍增器返回 true，否则返回 false
     */
    bool hasShadowMultiplier() const noexcept { return mDefinition.hasShadowMultiplier; }
    
    /**
     * 获取必需属性
     * 
     * @return 必需的顶点属性位集
     */
    AttributeBitset getRequiredAttributes() const noexcept { return mDefinition.requiredAttributes; }
    
    /**
     * 获取折射模式
     * 
     * @return 折射模式（NONE、CUBEMAP、SCREEN_SPACE）
     */
    RefractionMode getRefractionMode() const noexcept { return mDefinition.refractionMode; }
    
    /**
     * 获取折射类型
     * 
     * @return 折射类型（SOLID、THIN）
     */
    RefractionType getRefractionType() const noexcept { return mDefinition.refractionType; }
    
    /**
     * 获取反射模式
     * 
     * @return 反射模式（DEFAULT、SCREEN_SPACE）
     */
    ReflectionMode getReflectionMode() const noexcept { return mDefinition.reflectionMode; }

    /**
     * 是否有镜面反射抗锯齿
     * 
     * @return 如果启用镜面反射抗锯齿返回 true，否则返回 false
     */
    bool hasSpecularAntiAliasing() const noexcept { return mDefinition.specularAntiAliasing; }
    
    /**
     * 获取镜面反射抗锯齿方差
     * 
     * @return 方差值
     */
    float getSpecularAntiAliasingVariance() const noexcept { return mDefinition.specularAntiAliasingVariance; }
    
    /**
     * 获取镜面反射抗锯齿阈值
     * 
     * @return 阈值
     */
    float getSpecularAntiAliasingThreshold() const noexcept { return mDefinition.specularAntiAliasingThreshold; }

    /**
     * 获取采样器绑定索引
     * 
     * @param name 采样器参数名称
     * @return 描述符绑定索引
     */
    backend::descriptor_binding_t getSamplerBinding(
            std::string_view const& name) const;

    /**
     * 获取参数变换名称
     * 
     * @param samplerName 采样器名称
     * @return 变换参数名称（如果存在），否则返回 nullptr
     * 
     * 功能：某些采样器（如法线贴图）可能有关联的变换参数（如法线贴图缩放的 uniform）。
     */
    const char* getParameterTransformName(std::string_view samplerName) const noexcept;

    /**
     * 是否有材质属性
     * 
     * @param property 材质属性
     * @return 如果材质具有指定属性返回 true，否则返回 false
     * 
     * 功能：检查材质是否具有特定的属性（如 BASE_COLOR、METALLIC、ROUGHNESS 等）。
     */
    bool hasMaterialProperty(Property property) const noexcept {
        return bool(mDefinition.materialProperties & uint64_t(property));
    }

    /**
     * 获取采样器接口块
     * 
     * @return 采样器接口块常量引用
     * 
     * 功能：返回材质的采样器接口块，包含所有采样器参数的定义。
     */
    SamplerInterfaceBlock const& getSamplerInterfaceBlock() const noexcept {
        return mDefinition.samplerInterfaceBlock;
    }

    /**
     * 获取参数数量
     * 
     * @return 材质参数的总数量（uniform + sampler + subpass）
     */
    size_t getParameterCount() const noexcept {
        return mDefinition.uniformInterfaceBlock.getFieldInfoList().size() +
               mDefinition.samplerInterfaceBlock.getSamplerInfoList().size() +
               (mDefinition.subpassInfo.isValid ? 1 : 0);
    }
    
    /**
     * 获取参数列表
     * 
     * @param parameters 输出参数信息数组
     * @param count 数组大小
     * @return 实际填充的参数数量
     * 
     * 功能：获取所有材质参数的反射信息。
     */
    size_t getParameters(ParameterInfo* parameters, size_t count) const noexcept;

    /**
     * 生成材质实例 ID
     * 
     * @return 新的材质实例 ID
     * 
     * 功能：为材质实例生成唯一标识符（用于排序键生成）。
     */
    uint32_t generateMaterialInstanceId() const noexcept { return mMaterialInstanceId++; }

    /**
     * 销毁程序
     * 
     * @param engine 引擎引用
     * @param variantMask 变体掩码（指定要检查的变体位）
     * @param variantValue 变体值（指定要销毁的变体值）
     * 
     * 功能：销毁匹配条件的变体程序，释放 GPU 资源。
     */
    void destroyPrograms(FEngine& engine,
            Variant::type_t variantMask = 0,
            Variant::type_t variantValue = 0);

    /**
     * 获取特化常量 ID
     * 
     * @param name 特化常量名称
     * @return 特化常量 ID（如果找到），否则返回空值
     * 
     * 功能：根据名称查找特化常量的 ID。
     * 特化常量是编译时常量，可以在运行时修改而不需要重新编译着色器。
     */
    // return the id of a specialization constant specified by name for this material
    std::optional<uint32_t> getSpecializationConstantId(std::string_view name) const noexcept ;

    /**
     * 设置特化常量
     * 
     * @tparam T 常量类型（INT、FLOAT、BOOL）
     * @param id 特化常量 ID
     * @param value 常量值
     * @return 如果值被更改返回 true，否则返回 false
     * 
     * 功能：设置特化常量的值。如果 ID 无效，调用无效果。
     */
    // Sets a specialization constant by id. call is no-op if the id is invalid.
    // Return true is the value was changed.
    template<typename T, typename = Builder::is_supported_constant_parameter_t<T>>
    bool setConstant(uint32_t id, T value) noexcept;

    /**
     * 获取每视图布局索引
     * 
     * @return 每视图描述符集布局索引
     * 
     * 功能：用于后处理材质，指定使用哪个每视图描述符集布局。
     */
    uint8_t getPerViewLayoutIndex() const noexcept {
        return mDefinition.perViewLayoutIndex;
    }

    /**
     * 是否使用 UBO 批处理
     * 
     * @return 如果使用 UBO 批处理返回 true，否则返回 false
     * 
     * 功能：UBO 批处理将多个实例的 uniform 数据打包到单个 UBO 中，以提高性能。
     */
    bool useUboBatching() const noexcept {
        return mUseUboBatching;
    }

    /**
     * 获取材质源代码
     * 
     * @return 材质包（Material Package）的源代码字符串视图
     * 
     * 功能：返回材质的原始源代码（用于调试和 MATDBG）。
     */
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
    /**
     * 获取材质解析器
     * 
     * @return 材质解析器常量引用
     * 
     * 功能：返回用于解析材质定义的解析器（支持 MATDBG 编辑）。
     */
    MaterialParser const& getMaterialParser() const noexcept;

    /**
     * 是否有变体
     * 
     * @param variant 着色器变体
     * @return 如果材质支持此变体返回 true，否则返回 false
     */
    bool hasVariant(Variant variant) const noexcept;
    
    /**
     * 准备程序（慢速路径）
     * 
     * @param variant 着色器变体
     * @param priorityQueue 编译优先级队列
     * 
     * 功能：编译并缓存指定变体的着色器程序（如果未缓存）。
     */
    void prepareProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    
    /**
     * 获取表面程序（慢速路径）
     * 
     * @param variant 着色器变体
     * @param priorityQueue 编译优先级队列
     * 
     * 功能：为表面材质编译程序。
     */
    void getSurfaceProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    
    /**
     * 获取后处理程序（慢速路径）
     * 
     * @param variant 着色器变体
     * @param priorityQueue 编译优先级队列
     * 
     * 功能：为后处理材质编译程序。
     */
    void getPostProcessProgramSlow(Variant variant,
            CompilerPriorityQueue priorityQueue) const noexcept;
    
    /**
     * 获取程序（带变体）
     * 
     * @param variant 完整变体
     * @param vertexVariant 顶点着色器变体
     * @param fragmentVariant 片段着色器变体
     * @return 后端程序对象
     * 
     * 功能：根据指定的顶点和片段变体创建程序。
     */
    backend::Program getProgramWithVariants(Variant variant,
            Variant vertexVariant, Variant fragmentVariant) const;

    /**
     * 处理特化常量
     * 
     * @param builder 构建器引用
     * @return 特化常量向量
     * 
     * 功能：从构建器中提取特化常量并转换为后端格式。
     */
    utils::FixedCapacityVector<backend::Program::SpecializationConstant>
            processSpecializationConstants(Builder const& builder);

    /**
     * 预缓存深度变体
     * 
     * @param engine 引擎引用
     * 
     * 功能：预编译深度变体（用于优化深度渲染性能）。
     */
    void precacheDepthVariants(FEngine& engine);

    /**
     * 创建并缓存程序
     * 
     * @param p 后端程序对象（移动语义）
     * @param variant 着色器变体
     * 
     * 功能：将编译后的程序添加到缓存中。
     */
    void createAndCacheProgram(backend::Program&& p, Variant variant) const noexcept;

    /**
     * 是否为共享变体
     * 
     * @param variant 着色器变体
     * @return 如果是共享变体返回 true，否则返回 false
     * 
     * 功能：共享变体使用默认材质的深度着色器，用于优化内存使用。
     * 条件：
     * - 必须是表面材质域
     * - 不是默认材质
     * - 没有自定义深度着色器
     * - 是有效的深度变体
     */
    inline bool isSharedVariant(Variant const variant) const {
        return (mDefinition.materialDomain == MaterialDomain::SURFACE) && !mIsDefaultMaterial &&
               !mDefinition.hasCustomDepthShader && Variant::isValidDepthVariant(variant);
    }

    /**
     * 缓存的程序数组
     * 
     * 存储所有变体的编译后程序句柄。
     * 可变，因为 prepareProgramSlow() 需要修改它。
     */
    mutable std::array<backend::Handle<backend::HwProgram>, VARIANT_COUNT> mCachedPrograms;
    
    /**
     * 材质定义
     * 
     * 包含所有材质属性（着色器、uniform、sampler、渲染状态等）。
     * 由 MaterialBuilder 创建并传递给构造函数。
     */
    MaterialDefinition const& mDefinition;

    /**
     * 是否为默认材质
     * 
     * 默认材质是引擎创建的回退材质，用于错误情况。
     */
    bool mIsDefaultMaterial = false;

    /**
     * 是否使用 UBO 批处理
     * 
     * 如果为 true，材质实例将使用 UBO 批处理模式。
     */
    bool mUseUboBatching = false;

    /**
     * 默认材质实例
     * 
     * 材质的默认实例（首次访问时创建）。
     * 可变，因为 getDefaultInstance() 需要修改它。
     */
    // reserve some space to construct the default material instance
    mutable FMaterialInstance* mDefaultMaterialInstance = nullptr;

    /**
     * 当前特化常量
     * 
     * 存储当前程序使用的特化常量值。
     * 用于在运行时修改特化常量而不重新编译着色器。
     */
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

    /**
     * 引擎引用
     * 
     * 材质的创建和管理都需要引擎。
     */
    FEngine& mEngine;
    
    /**
     * 材质 ID
     * 
     * 材质的唯一标识符（由引擎分配）。
     */
    const uint32_t mMaterialId;
    
    /**
     * 材质实例 ID 计数器
     * 
     * 用于为材质实例生成唯一 ID（用于排序键）。
     * 可变，因为 generateMaterialInstanceId() 需要修改它。
     */
    mutable uint32_t mMaterialInstanceId = 0;
};


FILAMENT_DOWNCAST(Material)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_MATERIAL_H
