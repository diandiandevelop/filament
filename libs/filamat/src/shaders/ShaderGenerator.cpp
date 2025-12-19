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

#include "ShaderGenerator.h"

#include "CodeGenerator.h"
#include "SibGenerator.h"
#include "UibGenerator.h"

#include <filament/MaterialEnums.h>

#include <private/filament/DescriptorSets.h>
#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <filamat/MaterialBuilder.h>

#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/debug.h>
#include <utils/sstream.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filamat {

using namespace filament;
using namespace filament::backend;
using namespace utils;

// 生成表面材质变体相关的预处理器定义
void ShaderGenerator::generateSurfaceMaterialVariantDefines(io::sstream& out,
        ShaderStage const stage, MaterialBuilder::FeatureLevel featureLevel,
        MaterialInfo const& material, filament::Variant const variant) noexcept {

    // 判断是否有光照相关的变体
    bool const litVariants = material.isLit || material.hasShadowMultiplier;

    // 生成变体相关的预处理器定义
    CodeGenerator::generateDefine(out, "VARIANT_HAS_DIRECTIONAL_LIGHTING",
            litVariants && variant.hasDirectionalLighting());  // 是否有方向光
    CodeGenerator::generateDefine(out, "VARIANT_HAS_DYNAMIC_LIGHTING",
            litVariants && variant.hasDynamicLighting());  // 是否有动态光源
    CodeGenerator::generateDefine(out, "VARIANT_HAS_SHADOWING",
            litVariants && filament::Variant::isShadowReceiverVariant(variant));  // 是否有阴影
    CodeGenerator::generateDefine(out, "VARIANT_HAS_VSM",
            filament::Variant::isVSMVariant(variant));  // 是否使用方差阴影贴图
    CodeGenerator::generateDefine(out, "VARIANT_HAS_STEREO",
            hasStereo(variant, featureLevel));  // 是否有立体渲染
    CodeGenerator::generateDefine(out, "VARIANT_DEPTH",
            filament::Variant::isValidDepthVariant(variant));  // 是否是深度变体

    // 根据着色器阶段生成特定的定义
    switch (stage) {
        case ShaderStage::VERTEX:
        // 顶点着色器：检查是否有蒙皮或变形
        CodeGenerator::generateDefine(out, "VARIANT_HAS_SKINNING_OR_MORPHING",
                hasSkinningOrMorphing(variant, featureLevel));
            break;
        case ShaderStage::FRAGMENT:
            // 片段着色器：检查是否有雾效、拾取、屏幕空间反射
            CodeGenerator::generateDefine(out, "VARIANT_HAS_FOG",
                    filament::Variant::isFogVariant(variant));
            CodeGenerator::generateDefine(out, "VARIANT_HAS_PICKING",
                    filament::Variant::isPickingVariant(variant));
            CodeGenerator::generateDefine(out, "VARIANT_HAS_SSR",
                    filament::Variant::isSSRVariant(variant));
            break;
        case ShaderStage::COMPUTE:
            break;
    }

    out << '\n';
    // 生成材质相关的预处理器定义
    CodeGenerator::generateDefine(out, "MATERIAL_FEATURE_LEVEL", uint32_t(featureLevel));  // 功能级别

    CodeGenerator::generateDefine(out, "MATERIAL_HAS_SHADOW_MULTIPLIER",
            material.hasShadowMultiplier);  // 是否有阴影倍增器

    CodeGenerator::generateDefine(out, "MATERIAL_HAS_INSTANCES", material.instanced);  // 是否使用实例化

    CodeGenerator::generateDefine(out, "MATERIAL_HAS_VERTEX_DOMAIN_DEVICE_JITTERED",
            material.vertexDomainDeviceJittered);  // 顶点域设备抖动

    CodeGenerator::generateDefine(out, "MATERIAL_HAS_TRANSPARENT_SHADOW",
            material.hasTransparentShadow);  // 是否有透明阴影

    if (stage == ShaderStage::FRAGMENT) {
        // We only support both screen-space refractions and reflections at the same time.
        // And the MATERIAL_HAS_REFRACTION/MATERIAL_HAS_REFLECTIONS defines signify if
        // refraction/reflections are supported by the material.
        // 我们只同时支持屏幕空间折射和反射。
        // MATERIAL_HAS_REFRACTION/MATERIAL_HAS_REFLECTIONS定义表示材质是否支持折射/反射。

        CodeGenerator::generateDefine(out, "MATERIAL_HAS_REFRACTION",
                material.refractionMode != RefractionMode::NONE);
        if (material.refractionMode != RefractionMode::NONE) {
            CodeGenerator::generateDefine(out, "REFRACTION_MODE_CUBEMAP",
                    uint32_t(RefractionMode::CUBEMAP));
            CodeGenerator::generateDefine(out, "REFRACTION_MODE_SCREEN_SPACE",
                    uint32_t(RefractionMode::SCREEN_SPACE));
            switch (material.refractionMode) {
                case RefractionMode::NONE:
                    // can't be here
                    break;
                case RefractionMode::CUBEMAP:
                    CodeGenerator::generateDefine(out, "REFRACTION_MODE",
                            "REFRACTION_MODE_CUBEMAP");
                    break;
                case RefractionMode::SCREEN_SPACE:
                    CodeGenerator::generateDefine(out, "REFRACTION_MODE",
                            "REFRACTION_MODE_SCREEN_SPACE");
                    break;
            }
            CodeGenerator::generateDefine(out, "REFRACTION_TYPE_SOLID",
                    uint32_t(RefractionType::SOLID));
            CodeGenerator::generateDefine(out, "REFRACTION_TYPE_THIN",
                    uint32_t(RefractionType::THIN));
            switch (material.refractionType) {
                case RefractionType::SOLID:
                    CodeGenerator::generateDefine(out, "REFRACTION_TYPE", "REFRACTION_TYPE_SOLID");
                    break;
                case RefractionType::THIN:
                    CodeGenerator::generateDefine(out, "REFRACTION_TYPE", "REFRACTION_TYPE_THIN");
                    break;
            }
        }
        CodeGenerator::generateDefine(out, "MATERIAL_HAS_REFLECTIONS",
                material.reflectionMode == ReflectionMode::SCREEN_SPACE);

        CodeGenerator::generateDefine(out, "MATERIAL_HAS_DOUBLE_SIDED_CAPABILITY",
                material.hasDoubleSidedCapability);

        CodeGenerator::generateDefine(out, "MATERIAL_HAS_CUSTOM_SURFACE_SHADING",
                material.hasCustomSurfaceShading);

        out << '\n';
        switch (material.blendingMode) {
            case BlendingMode::OPAQUE:
                CodeGenerator::generateDefine(out, "BLEND_MODE_OPAQUE", true);
                break;
            case BlendingMode::TRANSPARENT:
                CodeGenerator::generateDefine(out, "BLEND_MODE_TRANSPARENT", true);
                break;
            case BlendingMode::ADD:
                CodeGenerator::generateDefine(out, "BLEND_MODE_ADD", true);
                break;
            case BlendingMode::MASKED:
                CodeGenerator::generateDefine(out, "BLEND_MODE_MASKED", true);
                break;
            case BlendingMode::FADE:
                // Fade is a special case of transparent
                CodeGenerator::generateDefine(out, "BLEND_MODE_TRANSPARENT", true);
                CodeGenerator::generateDefine(out, "BLEND_MODE_FADE", true);
                break;
            case BlendingMode::MULTIPLY:
                CodeGenerator::generateDefine(out, "BLEND_MODE_MULTIPLY", true);
                break;
            case BlendingMode::SCREEN:
                CodeGenerator::generateDefine(out, "BLEND_MODE_SCREEN", true);
                break;
            case BlendingMode::CUSTOM:
                CodeGenerator::generateDefine(out, "BLEND_MODE_CUSTOM", true);
                break;
        }

        switch (material.postLightingBlendingMode) {
            case BlendingMode::OPAQUE:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_OPAQUE", true);
                break;
            case BlendingMode::TRANSPARENT:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_TRANSPARENT", true);
                break;
            case BlendingMode::ADD:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_ADD", true);
                break;
            case BlendingMode::MULTIPLY:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_MULTIPLY", true);
                break;
            case BlendingMode::SCREEN:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_SCREEN", true);
                break;
            case BlendingMode::CUSTOM:
                CodeGenerator::generateDefine(out, "POST_LIGHTING_BLEND_MODE_CUSTOM", true);
                break;
            default:
                break;
        }

        out << '\n';
        CodeGenerator::generateDefine(out, "GEOMETRIC_SPECULAR_AA",
                material.specularAntiAliasing && material.isLit);

        CodeGenerator::generateDefine(out, "CLEAR_COAT_IOR_CHANGE",
                material.clearCoatIorChange);
    }
}

// 生成表面材质变体属性相关的预处理器定义
// @param out 输出流
// @param properties 材质属性列表
// @param defines 用户提供的预处理器定义列表
void ShaderGenerator::generateSurfaceMaterialVariantProperties(io::sstream& out,
        MaterialBuilder::PropertyList const properties,
        const MaterialBuilder::PreprocessorDefineList& defines) noexcept {

    // 遍历所有材质属性，生成属性定义
    UTILS_NOUNROLL
    for (size_t i = 0; i < MaterialBuilder::MATERIAL_PROPERTIES_COUNT; i++) {
        CodeGenerator::generateMaterialProperty(out,
                static_cast<MaterialBuilder::Property>(i), properties[i]);
    }

    // 合成定义：检查是否需要TBN（切线-副法线-法线）矩阵
    bool const hasTBN =
            properties[static_cast<int>(MaterialBuilder::Property::ANISOTROPY)]         ||
            properties[static_cast<int>(MaterialBuilder::Property::NORMAL)]             ||
            properties[static_cast<int>(MaterialBuilder::Property::BENT_NORMAL)]        ||
            properties[static_cast<int>(MaterialBuilder::Property::CLEAR_COAT_NORMAL)];

    CodeGenerator::generateDefine(out, "MATERIAL_NEEDS_TBN", hasTBN);

    // 添加用户提供的额外定义
    for (const auto& define : defines) {
        CodeGenerator::generateDefine(out, define.name.c_str(), define.value.c_str());
    }
}

// 生成顶点域相关的预处理器定义
// @param out 输出流
// @param domain 顶点域类型
void ShaderGenerator::generateVertexDomainDefines(io::sstream& out, VertexDomain const domain) noexcept {
    switch (domain) {
        case VertexDomain::OBJECT:
            CodeGenerator::generateDefine(out, "VERTEX_DOMAIN_OBJECT", true);
            break;
        case VertexDomain::WORLD:
            CodeGenerator::generateDefine(out, "VERTEX_DOMAIN_WORLD", true);
            break;
        case VertexDomain::VIEW:
            CodeGenerator::generateDefine(out, "VERTEX_DOMAIN_VIEW", true);
            break;
        case VertexDomain::DEVICE:
            CodeGenerator::generateDefine(out, "VERTEX_DOMAIN_DEVICE", true);
            break;
    }
}

// 生成后处理材质变体相关的预处理器定义
// @param out 输出流
// @param variant 后处理变体类型
void ShaderGenerator::generatePostProcessMaterialVariantDefines(io::sstream& out,
        PostProcessVariant const variant) noexcept {
    switch (variant) {
        case PostProcessVariant::OPAQUE:
            CodeGenerator::generateDefine(out, "POST_PROCESS_OPAQUE", 1u);
            break;
        case PostProcessVariant::TRANSLUCENT:
            CodeGenerator::generateDefine(out, "POST_PROCESS_OPAQUE", 0u);
            break;
    }
}

// 追加着色器代码到流中（处理行偏移，用于正确的错误报告）
// @param ss 输出流
// @param shader 要追加的着色器代码
// @param lineOffset 行偏移量（用于#line指令）
void ShaderGenerator::appendShader(io::sstream& ss,
        const CString& shader, size_t const lineOffset) noexcept {

    // Lambda函数：计算字符串中的行数
    auto countLines = [](const char* s) -> size_t {
        size_t lines = 0;
        size_t i = 0;
        while (s[i]) {
            if (s[i++] == '\n') lines++;
        }
        return lines;
    };

    if (!shader.empty()) {
        // 计算当前流中的行数
        size_t lines = countLines(ss.c_str());
        // 添加#line指令，设置行偏移
        ss << "#line " << lineOffset + 1 << '\n';
        // 追加着色器代码
        ss << shader.c_str();
        // 如果着色器代码不以换行符结尾，添加换行符
        if (shader[shader.size() - 1] != '\n') {
            ss << "\n";
            lines++;
        }
        // 添加#line指令，恢复行号（+2以考虑我们刚添加的#line指令）
        ss << "#line " << lines + countLines(shader.c_str()) + 2 << "\n";
    }
}

// 生成用户指定的特殊化常量（specialization constants）
// @param cg 代码生成器
// @param fs 片段着色器输出流
// @param constants 常量列表
// Constants 0 to CONFIG_MAX_RESERVED_SPEC_CONSTANTS - 1 are reserved by Filament.
void ShaderGenerator::generateUserSpecConstants(
        const CodeGenerator& cg, io::sstream& fs, MaterialBuilder::ConstantList const& constants) {
    // 从保留常量之后的索引开始
    size_t index = CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
    // 遍历所有用户常量，生成特殊化常量定义
    for (const auto& constant : constants) {
        // 生成完整的常量名称（添加前缀）
        std::string const fullName = std::string("materialConstants_") + constant.name.c_str();
        // 根据常量类型生成相应的特殊化常量
        switch (constant.type) {
            case ConstantType::INT:
                cg.generateSpecializationConstant(
                        fs, fullName.c_str(), index++, constant.defaultValue.i);
                break;
            case ConstantType::FLOAT:
                cg.generateSpecializationConstant(
                        fs, fullName.c_str(), index++, constant.defaultValue.f);
                break;
            case ConstantType::BOOL:
                cg.generateSpecializationConstant(
                        fs, fullName.c_str(), index++, constant.defaultValue.b);
                break;
        }
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，初始化着色器生成器
// @param properties 材质属性列表
// @param variables 变量列表
// @param outputs 输出列表
// @param defines 预处理器定义列表
// @param constants 常量列表
// @param pushConstants 推送常量列表
// @param materialCode 材质片段/计算着色器代码
// @param lineOffset 材质代码行偏移
// @param materialVertexCode 材质顶点着色器代码
// @param vertexLineOffset 顶点着色器代码行偏移
// @param materialDomain 材质域
ShaderGenerator::ShaderGenerator(
        MaterialBuilder::PropertyList const& properties,
        MaterialBuilder::VariableList const& variables,
        MaterialBuilder::OutputList const& outputs,
        MaterialBuilder::PreprocessorDefineList const& defines,
        MaterialBuilder::ConstantList const& constants,
        MaterialBuilder::PushConstantList const& pushConstants,
        CString const& materialCode, size_t const lineOffset,
        CString const& materialVertexCode, size_t const vertexLineOffset,
        MaterialBuilder::MaterialDomain const materialDomain) noexcept {

    // 计算材质不应该有顶点着色器
    if (materialDomain == MaterialBuilder::MaterialDomain::COMPUTE) {
        // we shouldn't have a vertex shader in a compute material
        assert_invariant(materialVertexCode.empty());
    }

    // 复制属性、变量和输出列表
    std::copy(std::begin(properties), std::end(properties), std::begin(mProperties));
    std::copy(std::begin(variables), std::end(variables), std::begin(mVariables));
    std::copy(std::begin(outputs), std::end(outputs), std::back_inserter(mOutputs));

    // 保存材质代码和行偏移
    mMaterialFragmentCode = materialCode;
    mMaterialVertexCode = materialVertexCode;
    mIsMaterialVertexShaderEmpty = materialVertexCode.empty();
    mMaterialLineOffset = lineOffset;
    mMaterialVertexLineOffset = vertexLineOffset;
    mMaterialDomain = materialDomain;
    mDefines = defines;
    mConstants = constants;
    mPushConstants = pushConstants;

    // 如果片段/计算着色器代码为空，生成默认代码
    if (mMaterialFragmentCode.empty()) {
        if (mMaterialDomain == MaterialBuilder::MaterialDomain::SURFACE) {
            // 表面材质：生成默认的material函数
            mMaterialFragmentCode =
                    CString("void material(inout MaterialInputs m) {\n    prepareMaterial(m);\n}\n");
        } else if (mMaterialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
            // 后处理材质：生成默认的postProcess函数
            mMaterialFragmentCode =
                    CString("void postProcess(inout PostProcessInputs p) {\n}\n");
        } else if (mMaterialDomain == MaterialBuilder::MaterialDomain::COMPUTE) {
            // 计算材质：生成默认的compute函数
            mMaterialFragmentCode =
                    CString("void compute() {\n}\n");
        }
    }
    // 如果顶点着色器代码为空，生成默认代码
    if (mMaterialVertexCode.empty()) {
        if (mMaterialDomain == MaterialBuilder::MaterialDomain::SURFACE) {
            // 表面材质：生成默认的materialVertex函数
            mMaterialVertexCode =
                    CString("void materialVertex(inout MaterialVertexInputs m) {\n}\n");
        } else if (mMaterialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
            // 后处理材质：生成默认的postProcessVertex函数
            mMaterialVertexCode =
                    CString("void postProcessVertex(inout PostProcessVertexInputs m) {\n}\n");
        }
    }
}

// 修复外部采样器（将外部采样器转换回常规采样器，用于优化后的着色器）
// @param sm 着色器模型
// @param shader 着色器代码（会被修改）
// @param featureLevel 功能级别
// @param material 材质信息
// External samplers are only supported on GL ES at the moment, we must
// skip the fixup on desktop targets
void ShaderGenerator::fixupExternalSamplers(ShaderModel const sm, std::string& shader,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material) noexcept {
    // 外部采样器目前仅在GL ES上支持，必须在桌面目标上跳过修复
    if (material.hasExternalSamplers && sm == ShaderModel::MOBILE) {
        CodeGenerator::fixupExternalSamplers(shader, material.sib, featureLevel);
    }
}

// 创建表面顶点着色器程序
std::string ShaderGenerator::createSurfaceVertexProgram(ShaderModel const shaderModel,
        MaterialBuilder::TargetApi const targetApi, MaterialBuilder::TargetLanguage const targetLanguage,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material, const filament::Variant variant, Interpolation const interpolation,
        VertexDomain const vertexDomain) const noexcept {

    assert_invariant(filament::Variant::isValid(variant));
    assert_invariant(mMaterialDomain != MaterialBuilder::MaterialDomain::COMPUTE);

    if (mMaterialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
        return createPostProcessVertexProgram(
                shaderModel, targetApi,
                targetLanguage, featureLevel, material, variant.key);
    }

    io::sstream vs;

    const CodeGenerator cg(shaderModel, targetApi, targetLanguage, featureLevel);

    cg.generateCommonProlog(vs, ShaderStage::VERTEX, material, variant);

    generateUserSpecConstants(cg, vs, mConstants);

    // note: even if the user vertex shader is empty, we can't use the "optimized" version if
    // we're in masked mode because fragment shader needs the color varyings
    const bool useOptimizedDepthVertexShader =
            // must be a depth variant
            filament::Variant::isValidDepthVariant(variant) &&
            // must have an empty vertex shader
            mIsMaterialVertexShaderEmpty &&
            // but must not be MASKED mode
            material.blendingMode != BlendingMode::MASKED &&
            // and must not have transparent shadows
            !(material.hasTransparentShadow  &&
                    (material.blendingMode == BlendingMode::TRANSPARENT ||
                     material.blendingMode == BlendingMode::FADE));

    CodeGenerator::generateDefine(vs, "USE_OPTIMIZED_DEPTH_VERTEX_SHADER",
            useOptimizedDepthVertexShader);

    generateSurfaceMaterialVariantDefines(vs, ShaderStage::VERTEX, featureLevel, material, variant);

    generateSurfaceMaterialVariantProperties(vs, mProperties, mDefines);

    AttributeBitset attributes = material.requiredAttributes;
    if (hasSkinningOrMorphing(variant, featureLevel)) {
        attributes.set(BONE_INDICES);
        attributes.set(BONE_WEIGHTS);
        if (material.useLegacyMorphing) {
            attributes.set(MORPH_POSITION_0);
            attributes.set(MORPH_POSITION_1);
            attributes.set(MORPH_POSITION_2);
            attributes.set(MORPH_POSITION_3);
            attributes.set(MORPH_TANGENTS_0);
            attributes.set(MORPH_TANGENTS_1);
            attributes.set(MORPH_TANGENTS_2);
            attributes.set(MORPH_TANGENTS_3);
        }
    }

    MaterialBuilder::PushConstantList vertexPushConstants;
    std::copy_if(mPushConstants.begin(), mPushConstants.end(),
            std::back_insert_iterator(vertexPushConstants),
            [](MaterialBuilder::PushConstant const& constant) {
                return constant.stage == ShaderStage::VERTEX;
            });
    cg.generateSurfaceShaderInputs(vs, ShaderStage::VERTEX, attributes, interpolation,
            vertexPushConstants);

    CodeGenerator::generateSurfaceTypes(vs, ShaderStage::VERTEX);

    // custom material variables
    size_t variableIndex = 0;
    for (const auto& variable : mVariables) {
        CodeGenerator::generateCommonVariable(vs, ShaderStage::VERTEX, variable, variableIndex++);
    }

    // materials defines
    generateVertexDomainDefines(vs, vertexDomain);

    // uniforms
    cg.generateUniforms(vs, ShaderStage::VERTEX,
            DescriptorSetBindingPoints::PER_VIEW,
            +PerViewBindingPoints::FRAME_UNIFORMS,
            UibGenerator::getPerViewUib());

    cg.generateUniforms(vs, ShaderStage::VERTEX,
            DescriptorSetBindingPoints::PER_RENDERABLE,
            +PerRenderableBindingPoints::OBJECT_UNIFORMS,
            UibGenerator::getPerRenderableUib());

    const bool litVariants = material.isLit || material.hasShadowMultiplier;
    if (litVariants && filament::Variant::isShadowReceiverVariant(variant)) {
        cg.generateUniforms(vs, ShaderStage::FRAGMENT,
                DescriptorSetBindingPoints::PER_VIEW,
                +PerViewBindingPoints::SHADOWS,
                UibGenerator::getShadowUib());
    }

    if (hasSkinningOrMorphing(variant, featureLevel)) {
        cg.generateUniforms(vs, ShaderStage::VERTEX,
                DescriptorSetBindingPoints::PER_RENDERABLE,
                +PerRenderableBindingPoints::BONES_UNIFORMS,
                UibGenerator::getPerRenderableBonesUib());
        cg.generateUniforms(vs, ShaderStage::VERTEX,
                DescriptorSetBindingPoints::PER_RENDERABLE,
                +PerRenderableBindingPoints::MORPHING_UNIFORMS,
                UibGenerator::getPerRenderableMorphingUib());
        cg.generateCommonSamplers(vs, DescriptorSetBindingPoints::PER_RENDERABLE,
                SibGenerator::getPerRenderableSib(variant));
    }

    cg.generateUniforms(vs, ShaderStage::VERTEX,
            DescriptorSetBindingPoints::PER_MATERIAL,
            +PerMaterialBindingPoints::MATERIAL_PARAMS,
            material.uib);

    CodeGenerator::generateSeparator(vs);

    cg.generateCommonSamplers(vs, DescriptorSetBindingPoints::PER_MATERIAL, material.sib);

    // shader code
    CodeGenerator::generateSurfaceCommon(vs, ShaderStage::VERTEX);
    CodeGenerator::generateSurfaceGetters(vs, ShaderStage::VERTEX);
    CodeGenerator::generateSurfaceMaterial(vs, ShaderStage::VERTEX);

    // main entry point
    appendShader(vs, mMaterialVertexCode, mMaterialVertexLineOffset);
    CodeGenerator::generateSurfaceMain(vs, ShaderStage::VERTEX);

    CodeGenerator::generateCommonEpilog(vs);

    return vs.c_str();
}

// 创建表面片段着色器程序
std::string ShaderGenerator::createSurfaceFragmentProgram(ShaderModel const shaderModel,
        MaterialBuilder::TargetApi const targetApi, MaterialBuilder::TargetLanguage const targetLanguage,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material, const filament::Variant variant,
        Interpolation const interpolation, UserVariantFilterMask const variantFilter) const noexcept {

    assert_invariant(filament::Variant::isValid(variant));
    assert_invariant(mMaterialDomain != MaterialBuilder::MaterialDomain::COMPUTE);

    if (mMaterialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
        return createPostProcessFragmentProgram(shaderModel, targetApi, targetLanguage,
                                                featureLevel, material, variant.key);
    }

    const CodeGenerator cg(shaderModel, targetApi, targetLanguage, featureLevel);

    io::sstream fs;
    cg.generateCommonProlog(fs, ShaderStage::FRAGMENT, material, variant);

    generateUserSpecConstants(cg, fs, mConstants);

    generateSurfaceMaterialVariantDefines(
            fs, ShaderStage::FRAGMENT, featureLevel, material, variant);

    auto const defaultSpecularAO = shaderModel == ShaderModel::MOBILE ?
            SpecularAmbientOcclusion::NONE : SpecularAmbientOcclusion::SIMPLE;
    auto specularAO = material.specularAOSet ? material.specularAO : defaultSpecularAO;
    CodeGenerator::generateDefine(fs, "SPECULAR_AMBIENT_OCCLUSION", uint32_t(specularAO));

    bool const multiBounceAO = material.multiBounceAOSet ?
                               material.multiBounceAO : shaderModel == ShaderModel::DESKTOP;
    CodeGenerator::generateDefine(fs, "MULTI_BOUNCE_AMBIENT_OCCLUSION", multiBounceAO ? 1u : 0u);

    generateSurfaceMaterialVariantProperties(fs, mProperties, mDefines);

    MaterialBuilder::PushConstantList fragmentPushConstants;
    std::copy_if(mPushConstants.begin(), mPushConstants.end(),
            std::back_insert_iterator(fragmentPushConstants),
            [](MaterialBuilder::PushConstant const& constant) {
                return constant.stage == ShaderStage::FRAGMENT;
            });
    cg.generateSurfaceShaderInputs(fs, ShaderStage::FRAGMENT, material.requiredAttributes,
            interpolation, fragmentPushConstants);

    CodeGenerator::generateSurfaceTypes(fs, ShaderStage::FRAGMENT);

    // custom material variables
    size_t variableIndex = 0;
    for (const auto& variable : mVariables) {
        CodeGenerator::generateCommonVariable(fs, ShaderStage::FRAGMENT, variable, variableIndex++);
    }

    // uniforms and samplers
    cg.generateUniforms(fs, ShaderStage::FRAGMENT,
            DescriptorSetBindingPoints::PER_VIEW,
            +PerViewBindingPoints::FRAME_UNIFORMS,
            UibGenerator::getPerViewUib());

    cg.generateUniforms(fs, ShaderStage::FRAGMENT,
            DescriptorSetBindingPoints::PER_RENDERABLE,
            +PerRenderableBindingPoints::OBJECT_UNIFORMS,
            UibGenerator::getPerRenderableUib());

    if (variant.hasDynamicLighting()) {
        cg.generateUniforms(fs, ShaderStage::FRAGMENT,
                DescriptorSetBindingPoints::PER_VIEW,
                +PerViewBindingPoints::LIGHTS,
                UibGenerator::getLightsUib());
    }

    bool const litVariants = material.isLit || material.hasShadowMultiplier;
    if (litVariants && filament::Variant::isShadowReceiverVariant(variant)) {
        cg.generateUniforms(fs, ShaderStage::FRAGMENT,
                DescriptorSetBindingPoints::PER_VIEW,
                +PerViewBindingPoints::SHADOWS,
                UibGenerator::getShadowUib());
    }

    if (variant.hasDynamicLighting()) {
        cg.generateUniforms(fs, ShaderStage::FRAGMENT,
                DescriptorSetBindingPoints::PER_VIEW,
                +PerViewBindingPoints::RECORD_BUFFER,
                UibGenerator::getFroxelRecordUib());

        cg.generateUniforms(fs, ShaderStage::FRAGMENT,
                DescriptorSetBindingPoints::PER_VIEW,
                +PerViewBindingPoints::FROXEL_BUFFER,
                UibGenerator::getFroxelsUib());
    }

    cg.generateUniforms(fs, ShaderStage::FRAGMENT,
            DescriptorSetBindingPoints::PER_MATERIAL,
            +PerMaterialBindingPoints::MATERIAL_PARAMS,
            material.uib);

    CodeGenerator::generateSeparator(fs);

    if (featureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
        assert_invariant(mMaterialDomain == MaterialDomain::SURFACE);
        // this is the list of samplers we need to filter

        bool const isLit = material.isLit || material.hasShadowMultiplier;
        bool const isSSR = material.reflectionMode == ReflectionMode::SCREEN_SPACE ||
                material.refractionMode == RefractionMode::SCREEN_SPACE;
        bool const hasFog = !(variantFilter & UserVariantFilterMask(UserVariantFilterBit::FOG));

        auto const list = SamplerInterfaceBlock::filterSamplerList(
                SibGenerator::getPerViewSib(variant).getSamplerInfoList(),
                descriptor_sets::getPerViewDescriptorSetLayoutWithVariant(
                        variant, mMaterialDomain, isLit, isSSR, hasFog));

        cg.generateCommonSamplers(fs, DescriptorSetBindingPoints::PER_VIEW, list);
    }

    cg.generateCommonSamplers(fs, DescriptorSetBindingPoints::PER_MATERIAL, material.sib);

    fs << "float filament_lodBias;\n";

    // shading code
    CodeGenerator::generateSurfaceCommon(fs, ShaderStage::FRAGMENT);
    CodeGenerator::generateSurfaceGetters(fs, ShaderStage::FRAGMENT);
    CodeGenerator::generateSurfaceMaterial(fs, ShaderStage::FRAGMENT);
    CodeGenerator::generateSurfaceParameters(fs, ShaderStage::FRAGMENT);

    if (filament::Variant::isFogVariant(variant)) {
        CodeGenerator::generateSurfaceFog(fs, ShaderStage::FRAGMENT);
    }

    // shading model
    if (filament::Variant::isValidDepthVariant(variant)) {
        // In MASKED mode or with transparent shadows, we need the alpha channel computed by
        // the material (user code), so we append it here.
        if (material.userMaterialHasCustomDepth ||
            material.blendingMode == BlendingMode::MASKED || ((
                     material.blendingMode == BlendingMode::TRANSPARENT ||
                     material.blendingMode == BlendingMode::FADE)
             && material.hasTransparentShadow)) {
            appendShader(fs, mMaterialFragmentCode, mMaterialLineOffset);
        }
        // These variants are special and are treated as DEPTH variants. Filament will never
        // request that variant for the color pass.
        CodeGenerator::generateSurfaceDepthMain(fs, ShaderStage::FRAGMENT);
    } else {
        appendShader(fs, mMaterialFragmentCode, mMaterialLineOffset);
        if (material.isLit) {
            if (filament::Variant::isSSRVariant(variant)) {
                CodeGenerator::generateSurfaceReflections(fs, ShaderStage::FRAGMENT);
            } else {
                CodeGenerator::generateSurfaceLit(fs, ShaderStage::FRAGMENT, variant,
                        material.shading,material.hasCustomSurfaceShading);
            }
        } else {
            CodeGenerator::generateSurfaceUnlit(fs, ShaderStage::FRAGMENT, variant,
                    material.hasShadowMultiplier);
        }
        // entry point
        CodeGenerator::generateSurfaceMain(fs, ShaderStage::FRAGMENT);
    }

    CodeGenerator::generateCommonEpilog(fs);

    return fs.c_str();
}

// 创建表面计算着色器程序
std::string ShaderGenerator::createSurfaceComputeProgram(ShaderModel const shaderModel,
        MaterialBuilder::TargetApi const targetApi, MaterialBuilder::TargetLanguage const targetLanguage,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material) const noexcept {
    assert_invariant(mMaterialDomain == MaterialBuilder::MaterialDomain::COMPUTE);
    assert_invariant(featureLevel >= FeatureLevel::FEATURE_LEVEL_2);
    const CodeGenerator cg(shaderModel, targetApi, targetLanguage, featureLevel);
    io::sstream s;

    cg.generateCommonProlog(s, ShaderStage::COMPUTE, material, {});

    generateUserSpecConstants(cg, s, mConstants);

    CodeGenerator::generateSurfaceTypes(s, ShaderStage::COMPUTE);

    cg.generateUniforms(s, ShaderStage::COMPUTE,
            DescriptorSetBindingPoints::PER_VIEW,
            +PerViewBindingPoints::FRAME_UNIFORMS,
            UibGenerator::getPerViewUib());

    cg.generateUniforms(s, ShaderStage::COMPUTE,
            DescriptorSetBindingPoints::PER_MATERIAL,
            +PerMaterialBindingPoints::MATERIAL_PARAMS,
            material.uib);

    cg.generateCommonSamplers(s, DescriptorSetBindingPoints::PER_MATERIAL, material.sib);

    // generate SSBO
    cg.generateBuffers(s, material.buffers);

    // TODO: generate images

    CodeGenerator::generateSurfaceCommon(s, ShaderStage::COMPUTE);
    CodeGenerator::generateSurfaceGetters(s, ShaderStage::COMPUTE);

    appendShader(s, mMaterialFragmentCode, mMaterialLineOffset);

    CodeGenerator::generateSurfaceMain(s, ShaderStage::COMPUTE);

    CodeGenerator::generateCommonEpilog(s);
    return s.c_str();
}

// 创建后处理顶点着色器程序
std::string ShaderGenerator::createPostProcessVertexProgram(ShaderModel const sm,
        MaterialBuilder::TargetApi const targetApi, MaterialBuilder::TargetLanguage const targetLanguage,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material, const filament::Variant::type_t variantKey) const noexcept {
    const CodeGenerator cg(sm, targetApi, targetLanguage, featureLevel);
    io::sstream vs;
    cg.generateCommonProlog(vs, ShaderStage::VERTEX, material, {});

    generateUserSpecConstants(cg, vs, mConstants);

    CodeGenerator::generateDefine(vs, "LOCATION_POSITION", uint32_t(POSITION));

    // custom material variables
    size_t variableIndex = 0;
    for (const auto& variable : mVariables) {
        CodeGenerator::generateCommonVariable(vs, ShaderStage::VERTEX, variable, variableIndex++);
    }

    CodeGenerator::generatePostProcessInputs(vs, ShaderStage::VERTEX);
    generatePostProcessMaterialVariantDefines(vs, PostProcessVariant(variantKey));

    cg.generateUniforms(vs, ShaderStage::VERTEX,
            DescriptorSetBindingPoints::PER_VIEW,
            +PerViewBindingPoints::FRAME_UNIFORMS,
            UibGenerator::getPerViewUib());

    cg.generateUniforms(vs, ShaderStage::VERTEX,
            DescriptorSetBindingPoints::PER_MATERIAL,
            +PerMaterialBindingPoints::MATERIAL_PARAMS,
            material.uib);

    cg.generateCommonSamplers(vs, DescriptorSetBindingPoints::PER_MATERIAL, material.sib);

    CodeGenerator::generatePostProcessCommon(vs, ShaderStage::VERTEX);
    CodeGenerator::generatePostProcessGetters(vs, ShaderStage::VERTEX);

    appendShader(vs, mMaterialVertexCode, mMaterialVertexLineOffset);

    CodeGenerator::generatePostProcessMain(vs, ShaderStage::VERTEX);

    CodeGenerator::generateCommonEpilog(vs);
    return vs.c_str();
}

// 创建后处理片段着色器程序
std::string ShaderGenerator::createPostProcessFragmentProgram(ShaderModel const sm,
        MaterialBuilder::TargetApi const targetApi, MaterialBuilder::TargetLanguage const targetLanguage,
        MaterialBuilder::FeatureLevel const featureLevel,
        MaterialInfo const& material, uint8_t variant) const noexcept {
    const CodeGenerator cg(sm, targetApi, targetLanguage, featureLevel);
    io::sstream fs;
    cg.generateCommonProlog(fs, ShaderStage::FRAGMENT, material, {});

    generateUserSpecConstants(cg, fs, mConstants);

    generatePostProcessMaterialVariantDefines(fs, PostProcessVariant(variant));

    // custom material variables
    size_t variableIndex = 0;
    for (const auto& variable : mVariables) {
        CodeGenerator::generateCommonVariable(fs, ShaderStage::FRAGMENT, variable, variableIndex++);
    }

    cg.generateUniforms(fs, ShaderStage::FRAGMENT,
            DescriptorSetBindingPoints::PER_VIEW,
            +PerViewBindingPoints::FRAME_UNIFORMS,
            UibGenerator::getPerViewUib());

    cg.generateUniforms(fs, ShaderStage::FRAGMENT,
            DescriptorSetBindingPoints::PER_MATERIAL,
            +PerMaterialBindingPoints::MATERIAL_PARAMS,
            material.uib);

    cg.generateCommonSamplers(fs, DescriptorSetBindingPoints::PER_MATERIAL, material.sib);

    // Subpasses are not yet in WebGPU, https://github.com/gpuweb/gpuweb/issues/435
    CodeGenerator::generatePostProcessSubpass(fs, targetApi == MaterialBuilderBase::TargetApi::WEBGPU
                                                      ? SubpassInfo{}
                                                      : material.subpass);

    CodeGenerator::generatePostProcessCommon(fs, ShaderStage::FRAGMENT);
    CodeGenerator::generatePostProcessGetters(fs, ShaderStage::FRAGMENT);

    // Generate post-process outputs.
    for (const auto& output : mOutputs) {
        if (output.target == MaterialBuilder::OutputTarget::COLOR) {
            cg.generateOutput(fs, ShaderStage::FRAGMENT, output.name, output.location,
                    output.qualifier, output.precision, output.type);
        }
        if (output.target == MaterialBuilder::OutputTarget::DEPTH) {
            CodeGenerator::generateDefine(fs, "FRAG_OUTPUT_DEPTH", 1u);
        }
    }

    CodeGenerator::generatePostProcessInputs(fs, ShaderStage::FRAGMENT);

    appendShader(fs, mMaterialFragmentCode, mMaterialLineOffset);

    CodeGenerator::generatePostProcessMain(fs, ShaderStage::FRAGMENT);
    CodeGenerator::generateCommonEpilog(fs);
    return fs.c_str();
}

// 检查变体是否具有蒙皮或变形功能
// @param variant 着色器变体
// @param featureLevel 功能级别
// @return 如果变体具有蒙皮或变形且功能级别支持，返回true
// HACK(exv): Ignore skinning/morphing variant when targeting ESSL 1.0. We should
// either properly support skinning on FL0 or build a system in matc which allows
// the set of included variants to differ per-feature level.
bool ShaderGenerator::hasSkinningOrMorphing(
        filament::Variant const variant, MaterialBuilder::FeatureLevel const featureLevel) noexcept {
    return variant.hasSkinningOrMorphing()
            // 当目标是ESSL 1.0时忽略蒙皮/变形变体（功能级别0不支持）
            && featureLevel > MaterialBuilder::FeatureLevel::FEATURE_LEVEL_0;
}

// 检查变体是否具有立体渲染功能
// @param variant 着色器变体
// @param featureLevel 功能级别
// @return 如果变体具有立体渲染且功能级别支持，返回true
// HACK(exv): Ignore stereo variant when targeting ESSL 1.0. We should properly build a
// system in matc which allows the set of included variants to differ per-feature level.
bool ShaderGenerator::hasStereo(
        filament::Variant const variant, MaterialBuilder::FeatureLevel const featureLevel) noexcept {
    return variant.hasStereo()
            // 当目标是ESSL 1.0时忽略立体渲染变体（功能级别0不支持）
            && featureLevel > MaterialBuilder::FeatureLevel::FEATURE_LEVEL_0;
}

} // namespace filament
