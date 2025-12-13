/*
 * Copyright (C) 2025 The Android Open Source Project
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
#ifndef TNT_FILAMENT_MATERIALDEFINITION_H
#define TNT_FILAMENT_MATERIALDEFINITION_H

#include <private/filament/Variant.h>
#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>
#include <private/filament/ConstantInfo.h>

#include <ds/DescriptorSetLayout.h>

#include <backend/Program.h>

namespace filament {

class FEngine;
class MaterialParser;

/**
 * MaterialDefinition 结构
 * 
 * 一个已解析、未编组的材质文件，不包含状态。
 * 由于这是一个纯只读类，几乎所有成员都是公共的，没有 getter。
 */
/** A MaterialDefinition is a parsed, unmarshalled material file, containing no state.
 *
 * Given that this is a pure read-only class, nearly all members are public without getters.
 */
struct MaterialDefinition {
    /**
     * 类型别名
     */
    using BlendingMode = filament::BlendingMode;  // 混合模式
    using Shading = filament::Shading;  // 着色模式
    using Interpolation = filament::Interpolation;  // 插值模式
    using VertexDomain = filament::VertexDomain;  // 顶点域
    using TransparencyMode = filament::TransparencyMode;  // 透明度模式
    using CullingMode = backend::CullingMode;  // 剔除模式

    /**
     * 属性信息容器类型
     * 存储属性名称和位置的配对
     */
    using AttributeInfoContainer = utils::FixedCapacityVector<std::pair<utils::CString, uint8_t>>;

    /**
     * 绑定 Uniform 信息容器类型
     * 存储绑定索引、名称和 Uniform 信息的元组
     */
    using BindingUniformInfoContainer = utils::FixedCapacityVector<
        std::tuple<uint8_t, utils::CString, backend::Program::UniformInfo>>;

    /**
     * 构造函数
     * 
     * 仅因为 std::make_unique() 而公开。
     * 
     * @param engine 引擎引用
     * @param parser 材质解析器唯一指针
     */
    // public only due to std::make_unique().
    MaterialDefinition(FEngine& engine, std::unique_ptr<MaterialParser> parser);

    /**
     * 终止 MaterialDefinition
     * 
     * 释放此 MaterialDefinition 拥有的 GPU 资源。
     * 
     * @param engine 引擎引用
     */
    // Free GPU resources owned by this MaterialDefinition.
    void terminate(FEngine& engine);

    /**
     * 获取材质解析器
     * 
     * @return 材质解析器常量引用
     */
    MaterialParser const& getMaterialParser() const noexcept { return *mMaterialParser; }

    /**
     * 描述符堆布局（按使用频率排序）
     */
    // try to order by frequency of use
    DescriptorSetLayout perViewDescriptorSetLayout;  // 每视图描述符堆布局
    DescriptorSetLayout perViewDescriptorSetLayoutVsm;  // 每视图描述符堆布局（VSM）
    DescriptorSetLayout descriptorSetLayout;  // 描述符堆布局
    backend::Program::DescriptorSetInfo programDescriptorBindings;  // 程序描述符绑定信息

    /**
     * 渲染状态和材质属性
     */
    backend::RasterState rasterState;  // 光栅化状态
    TransparencyMode transparencyMode = TransparencyMode::DEFAULT;  // 透明度模式
    bool isVariantLit = false;  // 是否为变体光照
    backend::FeatureLevel featureLevel = backend::FeatureLevel::FEATURE_LEVEL_1;  // 功能级别
    Shading shading = Shading::UNLIT;  // 着色模式

    BlendingMode blendingMode = BlendingMode::OPAQUE;  // 混合模式
    std::array<backend::BlendFunction, 4> customBlendFunctions = {};  // 自定义混合函数
    Interpolation interpolation = Interpolation::SMOOTH;  // 插值模式
    VertexDomain vertexDomain = VertexDomain::OBJECT;  // 顶点域
    MaterialDomain materialDomain = MaterialDomain::SURFACE;  // 材质域
    CullingMode cullingMode = CullingMode::NONE;  // 剔除模式
    AttributeBitset requiredAttributes;  // 必需属性位集
    UserVariantFilterMask variantFilterMask = 0;  // 变体过滤掩码
    RefractionMode refractionMode = RefractionMode::NONE;  // 折射模式
    RefractionType refractionType = RefractionType::SOLID;  // 折射类型
    ReflectionMode reflectionMode = ReflectionMode::DEFAULT;  // 反射模式
    uint64_t materialProperties = 0;  // 材质属性
    uint8_t perViewLayoutIndex = 0;  // 每视图布局索引

    /**
     * 阈值和抗锯齿参数
     */
    float maskThreshold = 0.4f;  // 遮罩阈值
    float specularAntiAliasingVariance = 0.0f;  // 镜面抗锯齿方差
    float specularAntiAliasingThreshold = 0.0f;  // 镜面抗锯齿阈值

    /**
     * 布尔标志
     */
    bool doubleSided = false;  // 是否双面
    bool doubleSidedCapability = false;  // 双面能力
    bool hasShadowMultiplier = false;  // 是否有阴影乘数
    bool hasCustomDepthShader = false;  // 是否有自定义深度着色器
    bool specularAntiAliasing = false;  // 是否启用镜面抗锯齿

    /**
     * 接口块和子通道信息
     */
    SamplerInterfaceBlock samplerInterfaceBlock;  // 采样器接口块
    BufferInterfaceBlock uniformInterfaceBlock;  // Uniform 接口块
    SubpassInfo subpassInfo;  // 子通道信息

    /**
     * 绑定和属性信息
     */
    BindingUniformInfoContainer bindingUniformInfo;  // 绑定 Uniform 信息
    AttributeInfoContainer attributeInfo;  // 属性信息

    /**
     * 常量和特化常量
     */
    // Constants defined by this material. Does not include reserved constants.
    utils::FixedCapacityVector<MaterialConstant> materialConstants;  // 材质常量（不包括保留常量）
    // A map from the Constant name to the materialConstants index.
    std::unordered_map<std::string_view, uint32_t> specializationConstantsNameToIndex;  // 特化常量名称到索引的映射
    // A list of default values for spec constants. Includes reserved constants.
    utils::FixedCapacityVector<backend::Program::SpecializationConstant> specializationConstants;  // 特化常量默认值列表（包括保留常量）

    /**
     * Push 常量
     */
    // current push constants for the HwProgram
    std::array<utils::FixedCapacityVector<backend::Program::PushConstant>,
            backend::Program::SHADER_TYPE_COUNT>
            pushConstants;  // 当前 HwProgram 的 push 常量

    /**
     * 名称和标识符
     */
    utils::CString name;  // 材质名称
    uint64_t cacheId = 0;  // 缓存 ID
    utils::CString source;  // 源字符串

private:
    friend class MaterialCache;  // 友元：材质缓存类
    friend class FMaterial;  // 友元：材质类（用于 onEditCallback）

    /**
     * 创建材质解析器
     * 
     * 根据后端和首选语言创建材质解析器。
     * 
     * @param backend 后端类型
     * @param languages 首选着色器语言列表
     * @param data 材质数据指针（非空）
     * @param size 材质数据大小
     * @return 材质解析器唯一指针
     */
    static std::unique_ptr<MaterialParser> createParser(backend::Backend const backend,
            utils::FixedCapacityVector<backend::ShaderLanguage> languages,
            const void* UTILS_NONNULL data, size_t size);

    /**
     * 创建材质定义
     * 
     * 从材质解析器创建材质定义。
     * 
     * @param engine 引擎引用
     * @param parser 材质解析器唯一指针
     * @return 材质定义唯一指针
     */
    static std::unique_ptr<MaterialDefinition> create(FEngine& engine,
            std::unique_ptr<MaterialParser> parser);

    /**
     * 处理主块
     * 
     * 从材质解析器中提取主要材质属性。
     */
    void processMain();
    
    /**
     * 处理混合模式
     * 
     * 从材质解析器中提取混合模式相关属性。
     */
    void processBlendingMode();
    
    /**
     * 处理特化常量
     * 
     * 从材质解析器中提取特化常量信息。
     * 
     * @param engine 引擎引用
     */
    void processSpecializationConstants(FEngine& engine);
    
    /**
     * 处理 Push 常量
     * 
     * 从材质解析器中提取 Push 常量信息。
     */
    void processPushConstants();
    
    /**
     * 处理描述符集
     * 
     * 从材质解析器中提取描述符集布局信息。
     * 
     * @param engine 引擎引用
     */
    void processDescriptorSets(FEngine& engine);

    /**
     * 材质解析器
     * 
     * 用于解析材质包的解析器对象。
     */
    std::unique_ptr<MaterialParser> mMaterialParser;  // 材质解析器唯一指针
};

} // namespace filament

#endif  // TNT_FILAMENT_MATERIALDEFINITION_H
