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

#ifndef TNT_FILAMENT_CODEGENERATOR_H
#define TNT_FILAMENT_CODEGENERATOR_H


#include "MaterialInfo.h"
#include "UibGenerator.h"

#include <filamat/MaterialBuilder.h>

#include <filament/MaterialEnums.h>

#include <private/filament/EngineEnums.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>
#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Log.h>
#include <utils/sstream.h>

#include <exception>
#include <iosfwd>
#include <string>
#include <variant>

#include <stdint.h>

namespace filamat {

// 代码生成器类，用于生成GLSL着色器代码的各个部分
class UTILS_PRIVATE CodeGenerator {
    using ShaderModel = filament::backend::ShaderModel;      // 着色器模型类型别名
    using ShaderStage = filament::backend::ShaderStage;      // 着色器阶段类型别名
    using FeatureLevel = filament::backend::FeatureLevel;    // 功能级别类型别名
    using TargetApi = MaterialBuilder::TargetApi;            // 目标API类型别名
    using TargetLanguage = MaterialBuilder::TargetLanguage;  // 目标语言类型别名
    using ShaderQuality = MaterialBuilder::ShaderQuality;    // 着色器质量类型别名
public:
    // 构造函数，初始化代码生成器
    CodeGenerator(ShaderModel shaderModel,
            TargetApi targetApi,
            TargetLanguage targetLanguage,
            FeatureLevel featureLevel) noexcept
            : mShaderModel(shaderModel),
              mTargetApi(targetApi),
              mTargetLanguage(targetLanguage),
              mFeatureLevel(featureLevel) {
        // 目标API必须是已解析的单个API，不能是ALL
        if (targetApi == TargetApi::ALL) {
            utils::slog.e << "Must resolve target API before codegen." << utils::io::endl;
            std::terminate();
        }
    }

    // 获取着色器模型
    filament::backend::ShaderModel getShaderModel() const noexcept { return mShaderModel; }

    // insert a separator (can be a new line)
    // 插入分隔符（可以是换行符）
    static utils::io::sstream& generateSeparator(utils::io::sstream& out);

    // generate prolog for the given shader
    // 为给定的着色器生成序言（版本、扩展等）
    utils::io::sstream& generateCommonProlog(utils::io::sstream& out, ShaderStage stage,
            MaterialInfo const& material, filament::Variant v) const;

    // 生成通用结尾
    static utils::io::sstream& generateCommonEpilog(utils::io::sstream& out);

    // 生成表面着色器类型定义
    static utils::io::sstream& generateSurfaceTypes(utils::io::sstream& out, ShaderStage stage);

    // generate common functions for the given shader
    // 为给定的着色器生成通用函数
    static utils::io::sstream& generateSurfaceCommon(utils::io::sstream& out, ShaderStage stage);
    static utils::io::sstream& generatePostProcessCommon(utils::io::sstream& out, ShaderStage stage);
    static utils::io::sstream& generateSurfaceMaterial(utils::io::sstream& out, ShaderStage stage);

    // 生成表面雾效代码
    static utils::io::sstream& generateSurfaceFog(utils::io::sstream& out, ShaderStage stage);

    // generate the shader's main()
    // 生成着色器的main()函数
    static utils::io::sstream& generateSurfaceMain(utils::io::sstream& out, ShaderStage stage);
    static utils::io::sstream& generatePostProcessMain(utils::io::sstream& out, ShaderStage stage);

    // generate the shader's code for the lit shading model
    // 为光照着色模型生成着色器代码
    static utils::io::sstream& generateSurfaceLit(utils::io::sstream& out, ShaderStage stage,
            filament::Variant variant, filament::Shading shading, bool customSurfaceShading);

    // generate the shader's code for the unlit shading model
    // 为无光照着色模型生成着色器代码
    static utils::io::sstream& generateSurfaceUnlit(utils::io::sstream& out, ShaderStage stage,
            filament::Variant variant, bool hasShadowMultiplier);

    // generate the shader's code for the screen-space reflections
    // 为屏幕空间反射生成着色器代码
    static utils::io::sstream& generateSurfaceReflections(utils::io::sstream& out, ShaderStage stage);

    // generate declarations for custom interpolants
    // 为自定义插值变量生成声明
    static utils::io::sstream& generateCommonVariable(utils::io::sstream& out, ShaderStage stage,
            const MaterialBuilder::CustomVariable& variable, size_t index);

    // generate declarations for non-custom "in" variables
    // 为非自定义"in"变量生成声明
    utils::io::sstream& generateSurfaceShaderInputs(utils::io::sstream& out, ShaderStage stage,
            const filament::AttributeBitset& attributes, filament::Interpolation interpolation,
            MaterialBuilder::PushConstantList const& pushConstants) const;
    static utils::io::sstream& generatePostProcessInputs(utils::io::sstream& out, ShaderStage stage);

    // generate declarations for custom output variables
    // 为自定义输出变量生成声明
    utils::io::sstream& generateOutput(utils::io::sstream& out, ShaderStage stage,
            const utils::CString& name, size_t index,
            MaterialBuilder::VariableQualifier qualifier,
            MaterialBuilder::Precision precision,
            MaterialBuilder::OutputType outputType) const;

    // generate no-op shader for depth prepass
    // 为深度预通道生成空操作着色器
    static utils::io::sstream& generateSurfaceDepthMain(utils::io::sstream& out, ShaderStage stage);

    // generate samplers
    // 生成采样器声明
    utils::io::sstream& generateCommonSamplers(utils::io::sstream& out,
            filament::DescriptorSetBindingPoints set,
            filament::SamplerInterfaceBlock::SamplerInfoList const& list) const;

    utils::io::sstream& generateCommonSamplers(utils::io::sstream& out,
            filament::DescriptorSetBindingPoints set,
            const filament::SamplerInterfaceBlock& sib) const {
        return generateCommonSamplers(out, set, sib.getSamplerInfoList());
    }

    // generate subpass
    static utils::io::sstream& generatePostProcessSubpass(utils::io::sstream& out,
            filament::SubpassInfo subpass);

    // generate uniforms
    // 生成统一变量声明
    utils::io::sstream& generateUniforms(utils::io::sstream& out, ShaderStage stage,
            filament::DescriptorSetBindingPoints set,
            filament::backend::descriptor_binding_t binding,
            const filament::BufferInterfaceBlock& uib) const;

    // generate buffers
    // 生成缓冲区声明（SSBO）
    utils::io::sstream& generateBuffers(utils::io::sstream& out,
            MaterialInfo::BufferContainer const& buffers) const;

    // generate an interface block
    // 生成接口块（UBO）
    utils::io::sstream& generateBufferInterfaceBlock(utils::io::sstream& out, ShaderStage stage,
            filament::DescriptorSetBindingPoints set,
            filament::backend::descriptor_binding_t binding,
            const filament::BufferInterfaceBlock& uib) const;

    // generate material properties getters
    // 生成材质属性getter函数
    static utils::io::sstream& generateMaterialProperty(utils::io::sstream& out,
            MaterialBuilder::Property property, bool isSet);

    // 生成质量相关的预处理器定义
    utils::io::sstream& generateQualityDefine(utils::io::sstream& out, ShaderQuality quality) const;

    // 生成预处理器定义（bool值）
    static utils::io::sstream& generateDefine(utils::io::sstream& out, const char* name, bool value);
    // 生成预处理器定义（uint32_t值）
    static utils::io::sstream& generateDefine(utils::io::sstream& out, const char* name, uint32_t value);
    // 生成预处理器定义（字符串值）
    static utils::io::sstream& generateDefine(utils::io::sstream& out, const char* name, const char* string);
    // 生成带索引的预处理器定义
    static utils::io::sstream& generateIndexedDefine(utils::io::sstream& out, const char* name,
            uint32_t index, uint32_t value);

    // 生成特殊化常量
    utils::io::sstream& generateSpecializationConstant(utils::io::sstream& out,
            const char* name, uint32_t id, std::variant<int, float, bool> value) const;

    // 生成推送常量
    utils::io::sstream& generatePushConstants(utils::io::sstream& out,
            MaterialBuilder::PushConstantList const& pushConstants,
            size_t const layoutLocation) const;

    // 生成后处理getter函数
    static utils::io::sstream& generatePostProcessGetters(utils::io::sstream& out, ShaderStage stage);
    // 生成表面getter函数
    static utils::io::sstream& generateSurfaceGetters(utils::io::sstream& out, ShaderStage stage);
    // 生成表面参数声明
    static utils::io::sstream& generateSurfaceParameters(utils::io::sstream& out, ShaderStage stage);

    static void fixupExternalSamplers(
            std::string& shader, filament::SamplerInterfaceBlock const& sib,
            FeatureLevel featureLevel) noexcept;

    // These constants must match the equivalent in MetalState.h.
    // These values represent the starting index for uniform, ssbo, and sampler group [[buffer(n)]]
    // bindings. See the chart at the top of MetalState.h.
    // 这些常量必须与MetalState.h中的等效常量匹配。
    // 这些值表示统一变量、SSBO和采样器组[[buffer(n)]]绑定的起始索引。参见MetalState.h顶部的图表。
    static constexpr uint32_t METAL_PUSH_CONSTANT_BUFFER_INDEX = 20u;      // Metal推送常量缓冲区索引
    static constexpr uint32_t METAL_DESCRIPTOR_SET_BINDING_START = 21u;   // Metal描述符集绑定起始索引
    static constexpr uint32_t METAL_DYNAMIC_OFFSET_BINDING = 25u;         // Metal动态偏移绑定索引

    // 获取唯一的采样器绑定点
    uint32_t getUniqueSamplerBindingPoint() const noexcept {
        return mUniqueSamplerBindingPoint++;
    }

    // 获取唯一的UBO绑定点
    uint32_t getUniqueUboBindingPoint() const noexcept {
        return mUniqueUboBindingPoint++;
    }

    // 获取唯一的SSBO绑定点
    uint32_t getUniqueSsboBindingPoint() const noexcept {
        return mUniqueSsboBindingPoint++;
    }

private:
    // 获取默认精度（根据着色器阶段）
    filament::backend::Precision getDefaultPrecision(ShaderStage stage) const;
    // 获取默认统一变量精度
    filament::backend::Precision getDefaultUniformPrecision() const;

    utils::io::sstream& generateInterfaceFields(utils::io::sstream& out,
            utils::FixedCapacityVector<filament::BufferInterfaceBlock::FieldInfo> const& infos,
            filament::backend::Precision defaultPrecision) const;

    utils::io::sstream& generateUboAsPlainUniforms(utils::io::sstream& out, ShaderStage stage,
            const filament::BufferInterfaceBlock& uib) const;

    static const char* getUniformPrecisionQualifier(filament::backend::UniformType type,
            filament::backend::Precision precision,
            filament::backend::Precision uniformPrecision,
            filament::backend::Precision defaultPrecision) noexcept;

    // return type name of sampler  (e.g.: "sampler2D")
    char const* getSamplerTypeName(filament::backend::SamplerType type,
            filament::backend::SamplerFormat format, bool multisample) const noexcept;

    // return name of the material property (e.g.: "ROUGHNESS")
    static char const* getConstantName(MaterialBuilder::Property property) noexcept;

    static char const* getPrecisionQualifier(filament::backend::Precision precision) noexcept;

    // return type (e.g.: "vec3", "vec4", "float")
    static char const* getTypeName(UniformType type) noexcept;

    // return type name of uniform Field (e.g.: "vec3", "vec4", "float")
    static char const* getUniformTypeName(filament::BufferInterfaceBlock::FieldInfo const& info) noexcept;

    // return type name of output  (e.g.: "vec3", "vec4", "float")
    static char const* getOutputTypeName(MaterialBuilder::OutputType type) noexcept;

    // return qualifier for the specified interpolation mode
    static char const* getInterpolationQualifier(filament::Interpolation interpolation) noexcept;

    static bool hasPrecision(filament::BufferInterfaceBlock::Type type) noexcept;

    ShaderModel mShaderModel;                        // 着色器模型
    TargetApi mTargetApi;                            // 目标API
    TargetLanguage mTargetLanguage;                  // 目标语言
    FeatureLevel mFeatureLevel;                      // 功能级别
    mutable uint32_t mUniqueSamplerBindingPoint = 0; // 唯一采样器绑定点（递增计数）
    mutable uint32_t mUniqueUboBindingPoint = 0;     // 唯一UBO绑定点（递增计数）
    mutable uint32_t mUniqueSsboBindingPoint = 0;    // 唯一SSBO绑定点（递增计数）
};

} // namespace filamat

#endif // TNT_FILAMENT_CODEGENERATOR_H
