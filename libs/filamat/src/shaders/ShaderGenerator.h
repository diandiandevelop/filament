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

#ifndef TNT_FILAMENT_DETAILS_SHADERGENERATOR_H
#define TNT_FILAMENT_DETAILS_SHADERGENERATOR_H


#include "MaterialInfo.h"

#include "UibGenerator.h"

#include <filament/MaterialEnums.h>

#include <filamat/MaterialBuilder.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/sstream.h>

#include <string>

#include <stdint.h>
#include <stddef.h>

namespace filamat {

class CodeGenerator;

// 着色器生成器类，用于生成顶点、片段和计算着色器程序
class ShaderGenerator {
public:
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
    ShaderGenerator(
            MaterialBuilder::PropertyList const& properties,
            MaterialBuilder::VariableList const& variables,
            MaterialBuilder::OutputList const& outputs,
            MaterialBuilder::PreprocessorDefineList const& defines,
            MaterialBuilder::ConstantList const& constants,
            MaterialBuilder::PushConstantList const& pushConstants,
            utils::CString const& materialCode,
            size_t lineOffset,
            utils::CString const& materialVertexCode,
            size_t vertexLineOffset,
            MaterialBuilder::MaterialDomain materialDomain) noexcept;

    // 创建表面顶点着色器程序
    std::string createSurfaceVertexProgram(filament::backend::ShaderModel shaderModel,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material, filament::Variant variant,
            filament::Interpolation interpolation,
            filament::VertexDomain vertexDomain) const noexcept;

    // 创建表面片段着色器程序
    std::string createSurfaceFragmentProgram(filament::backend::ShaderModel shaderModel,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material, filament::Variant variant,
            filament::Interpolation interpolation,
            filament::UserVariantFilterMask variantFilter) const noexcept;

    // 创建表面计算着色器程序
    std::string createSurfaceComputeProgram(filament::backend::ShaderModel shaderModel,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material) const noexcept;

    /**
     * When a GLSL shader is optimized we run it through an intermediate SPIR-V
     * representation. Unfortunately external samplers cannot be used with SPIR-V
     * at this time, so we must transform them into regular 2D samplers. This
     * fixup step can be used to turn the samplers back into external samplers after
     * the optimizations have been applied.
     * 当GLSL着色器被优化时，我们通过中间SPIR-V表示运行它。不幸的是，目前外部采样器不能与SPIR-V一起使用，
     * 所以我们必须将它们转换为常规的2D采样器。此修复步骤可用于在应用优化后将采样器转换回外部采样器。
     */
    static void fixupExternalSamplers(filament::backend::ShaderModel sm, std::string& shader,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material) noexcept;

private:
    // 生成顶点域相关的预处理器定义
    static void generateVertexDomainDefines(utils::io::sstream& out,
            filament::VertexDomain domain) noexcept;

    // 生成表面材质变体属性
    static void generateSurfaceMaterialVariantProperties(utils::io::sstream& out,
            MaterialBuilder::PropertyList const properties,
            const MaterialBuilder::PreprocessorDefineList& defines) noexcept;

    // 生成表面材质变体相关的预处理器定义
    static void generateSurfaceMaterialVariantDefines(utils::io::sstream& out,
            filament::backend::ShaderStage stage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material, filament::Variant variant) noexcept;

    // 生成后处理材质变体相关的预处理器定义
    static void generatePostProcessMaterialVariantDefines(utils::io::sstream& out,
            filament::PostProcessVariant variant) noexcept;

    // 生成用户指定的常量（特殊化常量）
    static void generateUserSpecConstants(
            const CodeGenerator& cg, utils::io::sstream& fs,
            MaterialBuilder::ConstantList const& constants);

    // 创建后处理顶点着色器程序
    std::string createPostProcessVertexProgram(filament::backend::ShaderModel sm,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material, filament::Variant::type_t variantKey) const noexcept;

    // 创建后处理片段着色器程序
    std::string createPostProcessFragmentProgram(filament::backend::ShaderModel sm,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            MaterialBuilder::FeatureLevel featureLevel,
            MaterialInfo const& material, uint8_t variant) const noexcept;

    // 追加着色器代码到流中（处理行偏移）
    static void appendShader(utils::io::sstream& ss,
            const utils::CString& shader, size_t lineOffset) noexcept;

    // 检查变体是否具有蒙皮或变形
    static bool hasSkinningOrMorphing(
            filament::Variant variant,
            MaterialBuilder::FeatureLevel featureLevel) noexcept;

    // 检查变体是否具有立体渲染
    static bool hasStereo(
            filament::Variant variant,
            MaterialBuilder::FeatureLevel featureLevel) noexcept;

    MaterialBuilder::PropertyList mProperties;              // 材质属性列表
    MaterialBuilder::VariableList mVariables;                // 变量列表
    MaterialBuilder::OutputList mOutputs;                    // 输出列表
    MaterialBuilder::MaterialDomain mMaterialDomain;         // 材质域
    MaterialBuilder::PreprocessorDefineList mDefines;        // 预处理器定义列表
    MaterialBuilder::ConstantList mConstants;                // 常量列表
    MaterialBuilder::PushConstantList mPushConstants;        // 推送常量列表
    utils::CString mMaterialFragmentCode;   // fragment or compute code - 片段或计算着色器代码
    utils::CString mMaterialVertexCode;     // 顶点着色器代码
    size_t mMaterialLineOffset;             // 材质代码行偏移
    size_t mMaterialVertexLineOffset;       // 顶点着色器代码行偏移
    bool mIsMaterialVertexShaderEmpty;      // 材质顶点着色器是否为空
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_SHADERGENERATOR_H
