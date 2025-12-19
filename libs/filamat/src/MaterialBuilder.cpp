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

#include "filamat/MaterialBuilder.h"

#include <filamat/Enums.h>
#include <filamat/Package.h>

#include "GLSLPostProcessor.h"
#include "MaterialVariants.h"
#include "PushConstantDefinitions.h"

#include "sca/GLSLTools.h"

#include "shaders/MaterialInfo.h"
#include "shaders/ShaderGenerator.h"
#include "shaders/UibGenerator.h"

#include "eiff/BlobDictionary.h"
#include "eiff/ChunkContainer.h"
#include "eiff/CompressedStringChunk.h"
#include "eiff/DictionarySpirvChunk.h"
#include "eiff/DictionaryTextChunk.h"
#include "eiff/LineDictionary.h"
#include "eiff/MaterialBinaryChunk.h"
#include "eiff/MaterialInterfaceBlockChunk.h"
#include "eiff/MaterialTextChunk.h"
#include "eiff/ShaderEntry.h"

#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/ConstantInfo.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/UibStructs.h>
#include <private/filament/Variant.h>

#include <filament/MaterialChunkType.h>
#include <filament/MaterialEnums.h>

#include <backend/DriverEnums.h>
#include <backend/Program.h>

#include <utils/BitmaskEnum.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Hash.h>
#include <utils/JobSystem.h>
#include <utils/Log.h>
#include <utils/Mutex.h>
#include <utils/Panic.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/vec3.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace filamat {

using namespace utils;
using namespace filament;

// Note: the VertexAttribute enum value must match the index in the array
const MaterialBuilder::AttributeDatabase MaterialBuilder::sAttributeDatabase = {{
        { "position",      AttributeType::FLOAT4, POSITION     },
        { "tangents",      AttributeType::FLOAT4, TANGENTS     },
        { "color",         AttributeType::FLOAT4, COLOR        },
        { "uv0",           AttributeType::FLOAT2, UV0          },
        { "uv1",           AttributeType::FLOAT2, UV1          },
        { "bone_indices",  AttributeType::UINT4,  BONE_INDICES },
        { "bone_weights",  AttributeType::FLOAT4, BONE_WEIGHTS },
        { },
        { "custom0",       AttributeType::FLOAT4, CUSTOM0      },
        { "custom1",       AttributeType::FLOAT4, CUSTOM1      },
        { "custom2",       AttributeType::FLOAT4, CUSTOM2      },
        { "custom3",       AttributeType::FLOAT4, CUSTOM3      },
        { "custom4",       AttributeType::FLOAT4, CUSTOM4      },
        { "custom5",       AttributeType::FLOAT4, CUSTOM5      },
        { "custom6",       AttributeType::FLOAT4, CUSTOM6      },
        { "custom7",       AttributeType::FLOAT4, CUSTOM7      },
}};

std::atomic<int> MaterialBuilderBase::materialBuilderClients(0);

// 断言目标API只设置了单个位
// @param api 目标API位掩码
static void assertSingleTargetApi(MaterialBuilderBase::TargetApi api) {
    // Assert that a single bit is set.
    // 断言只设置了单个位
    UTILS_UNUSED uint8_t const bits = uint8_t(api);
    assert_invariant(bits && !(bits & bits - 1u));
}

// 准备代码生成排列（为构建材质包做准备）
// @param vulkanSemantics 是否使用Vulkan语义
// @param featureLevel 功能级别
// 步骤1：清空代码生成排列和着色器模型
// 步骤2：根据平台设置着色器模型（移动端、桌面端或全部）
// 步骤3：确定OpenGL目标语言（如果优化级别高于预处理器，使用SPIR-V；否则使用GLSL）
// 步骤4：如果使用Vulkan语义，激活性能优化并使用SPIR-V
// 步骤5：如果未指定目标API，默认选择OpenGL
// 步骤6：计算有效功能级别（至少为级别1）
// 步骤7：为每个请求的着色器模型和目标API构建代码生成排列
void MaterialBuilderBase::prepare(bool const vulkanSemantics,
        backend::FeatureLevel const featureLevel) {
    // 步骤1：清空代码生成排列和着色器模型
    mCodeGenPermutations.clear();
    mShaderModels.reset();

    // 步骤2：根据平台设置着色器模型
    if (mPlatform == Platform::MOBILE) {
        mShaderModels.set(static_cast<size_t>(ShaderModel::MOBILE));
    } else if (mPlatform == Platform::DESKTOP) {
        mShaderModels.set(static_cast<size_t>(ShaderModel::DESKTOP));
    } else if (mPlatform == Platform::ALL) {
        mShaderModels.set(static_cast<size_t>(ShaderModel::MOBILE));
        mShaderModels.set(static_cast<size_t>(ShaderModel::DESKTOP));
    }

    // OpenGL is a special case. If we're doing any optimization, then we need to go to Spir-V.
    // 步骤3：确定OpenGL目标语言（OpenGL是特殊情况。如果进行任何优化，则需要使用SPIR-V）
    TargetLanguage glTargetLanguage = mOptimization > Optimization::PREPROCESSOR ?
                                      TargetLanguage::SPIRV : TargetLanguage::GLSL;
    if (vulkanSemantics) {
        // Currently GLSLPostProcessor.cpp is incapable of compiling SPIRV to GLSL without
        // running the optimizer. For now we just activate the optimizer in that case.
        // 步骤4：如果使用Vulkan语义，激活性能优化并使用SPIR-V
        // 目前GLSLPostProcessor.cpp无法在不运行优化器的情况下将SPIR-V编译为GLSL。在这种情况下我们激活优化器
        mOptimization = Optimization::PERFORMANCE;
        glTargetLanguage = TargetLanguage::SPIRV;
    }

    // Select OpenGL as the default TargetApi if none was specified.
    // 步骤5：如果未指定目标API，默认选择OpenGL
    if (none(mTargetApi)) {
        mTargetApi = TargetApi::OPENGL;
    }

    // Generally build for a minimum of feature level 1. If feature level 0 is specified, an extra
    // permutation is specifically included for the OpenGL/mobile target.
    // 步骤6：计算有效功能级别（通常至少为级别1。如果指定级别0，会为OpenGL/移动端目标额外包含一个排列）
    MaterialBuilder::FeatureLevel const effectiveFeatureLevel =
            std::max(featureLevel, backend::FeatureLevel::FEATURE_LEVEL_1);

    // Build a list of codegen permutations, which is useful across all types of material builders.
    // 步骤7：构建代码生成排列列表（对所有类型的材质构建器都很有用）
    static_assert(backend::SHADER_MODEL_COUNT == 2);
    for (const auto shaderModel: { ShaderModel::MOBILE, ShaderModel::DESKTOP }) {
        const auto i = static_cast<uint8_t>(shaderModel);
        if (!mShaderModels.test(i)) {
            continue; // skip this shader model since it was not requested.
            // 跳过未请求的着色器模型
        }

        // 步骤7.1：为OpenGL目标添加排列
        if (any(mTargetApi & TargetApi::OPENGL)) {
            mCodeGenPermutations.push_back({
                shaderModel,
                TargetApi::OPENGL,
                glTargetLanguage,
                effectiveFeatureLevel,
            });
            // 如果包含ESSL1且功能级别为0且着色器模型为移动端，添加额外的排列
            if (mIncludeEssl1
                    && featureLevel == backend::FeatureLevel::FEATURE_LEVEL_0
                    && shaderModel == ShaderModel::MOBILE) {
                mCodeGenPermutations.push_back({
                    shaderModel,
                    TargetApi::OPENGL,
                    glTargetLanguage,
                    backend::FeatureLevel::FEATURE_LEVEL_0
                });
            }
        }
        // 步骤7.2：为Vulkan目标添加排列
        if (any(mTargetApi & TargetApi::VULKAN)) {
            mCodeGenPermutations.push_back({
                shaderModel,
                TargetApi::VULKAN,
                TargetLanguage::SPIRV,
                effectiveFeatureLevel,
            });
        }
        // 步骤7.3：为Metal目标添加排列
        if (any(mTargetApi & TargetApi::METAL)) {
            mCodeGenPermutations.push_back({
                shaderModel,
                TargetApi::METAL,
                TargetLanguage::SPIRV,
                effectiveFeatureLevel,
            });
        }
        // 步骤7.4：为WebGPU目标添加排列
        if (any(mTargetApi & TargetApi::WEBGPU)) {
            mCodeGenPermutations.push_back({
                shaderModel,
                TargetApi::WEBGPU,
                TargetLanguage::SPIRV,
                effectiveFeatureLevel,
            });
        }
    }
}

// 构造函数：初始化材质构建器
// 步骤1：设置默认材质名称为"Unnamed"
// 步骤2：将所有材质属性初始化为false
// 步骤3：重置着色器模型
// 步骤4：初始化推送常量
MaterialBuilder::MaterialBuilder() : mMaterialName("Unnamed") {
    // 步骤2：将所有材质属性初始化为false
    std::fill_n(mProperties, MATERIAL_PROPERTIES_COUNT, false);
    // 步骤3：重置着色器模型
    mShaderModels.reset();

    // 步骤4：初始化推送常量
    initPushConstants();
}

MaterialBuilder::~MaterialBuilder() = default;

// 初始化材质构建器系统
// 步骤1：增加材质构建器客户端计数
// 步骤2：初始化GLSL工具
void MaterialBuilderBase::init() {
    // 步骤1：增加材质构建器客户端计数
    ++materialBuilderClients;
    // 步骤2：初始化GLSL工具
    GLSLTools::init();
}

// 关闭材质构建器系统
// 步骤1：减少材质构建器客户端计数
// 步骤2：关闭GLSL工具
void MaterialBuilderBase::shutdown() {
    // 步骤1：减少材质构建器客户端计数
    --materialBuilderClients;
    // 步骤2：关闭GLSL工具
    GLSLTools::shutdown();
}

// 设置材质名称
// @param name 材质名称
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::name(const char* name) noexcept {
    mMaterialName = CString(name);
    return *this;
}

// 设置编译参数
// @param params 编译参数字符串
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::compilationParameters(const char* params) noexcept {
    mCompilationParameters = CString(params);
    return *this;
}

// 设置材质片段着色器代码
// @param code 片段着色器代码
// @param line 行号偏移（用于错误报告）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::material(const char* code, size_t const line) noexcept {
    mMaterialFragmentCode.setCode(CString(code));
    mMaterialFragmentCode.setLineOffset(line);
    return *this;
}

// 设置材质顶点着色器代码
// @param code 顶点着色器代码
// @param line 行号偏移（用于错误报告）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::materialVertex(const char* code, size_t const line) noexcept {
    mMaterialVertexCode.setCode(CString(code));
    mMaterialVertexCode.setLineOffset(line);
    return *this;
}

// 设置着色模型
// @param shading 着色模型（如LIT、UNLIT等）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::shading(Shading const shading) noexcept {
    mShading = shading;
    return *this;
}

// 设置插值模式
// @param interpolation 插值模式（如SMOOTH、FLAT等）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::interpolation(Interpolation const interpolation) noexcept {
    mInterpolation = interpolation;
    return *this;
}

// 设置自定义变量（使用默认精度）
// @param v 变量类型（CUSTOM0-CUSTOM4）
// @param name 变量名称
// @return 返回自身引用以支持链式调用
// 步骤1：检查变量类型是否为自定义变量
// 步骤2：设置变量名称和默认精度
MaterialBuilder& MaterialBuilder::variable(Variable v, const char* name) noexcept {
    switch (v) {
        case Variable::CUSTOM0:
        case Variable::CUSTOM1:
        case Variable::CUSTOM2:
        case Variable::CUSTOM3:
        case Variable::CUSTOM4:
            // 步骤1：检查变量类型是否为自定义变量
            assert_invariant(size_t(v) < MATERIAL_VARIABLES_COUNT);
            // 步骤2：设置变量名称和默认精度
            mVariables[size_t(v)] = { CString(name), Precision::DEFAULT, false };
            break;
    }
    return *this;
}

// 设置自定义变量（指定精度）
// @param v 变量类型（CUSTOM0-CUSTOM4）
// @param name 变量名称
// @param precision 精度限定符
// @return 返回自身引用以支持链式调用
// 步骤1：检查变量类型是否为自定义变量
// 步骤2：设置变量名称和指定精度
MaterialBuilder& MaterialBuilder::variable(Variable v,
        const char* name, ParameterPrecision const precision) noexcept {
    switch (v) {
        case Variable::CUSTOM0:
        case Variable::CUSTOM1:
        case Variable::CUSTOM2:
        case Variable::CUSTOM3:
        case Variable::CUSTOM4:
            // 步骤1：检查变量类型是否为自定义变量
            assert_invariant(size_t(v) < MATERIAL_VARIABLES_COUNT);
            // 步骤2：设置变量名称和指定精度
            mVariables[size_t(v)] = { CString(name), precision, true };
            break;
    }
    return *this;
}

// 添加uniform参数（数组类型）
// @param name 参数名称
// @param size 数组大小
// @param type uniform类型
// @param precision 精度限定符
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::parameter(const char* name, size_t size, UniformType type,
        ParameterPrecision precision) {
    mParameters.emplace_back(name, type, size, precision );
    return *this;
}

// 添加uniform参数（单个值）
// @param name 参数名称
// @param type uniform类型
// @param precision 精度限定符
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::parameter(const char* name, UniformType const type,
        ParameterPrecision const precision) noexcept {
    return parameter(name, 1, type, precision);
}

// 添加采样器参数
// @param name 参数名称
// @param samplerType 采样器类型
// @param format 采样器格式
// @param precision 精度限定符
// @param filterable 是否可过滤
// @param multisample 是否多采样
// @param transformName 变换矩阵名称（可选）
// @param stages 着色器阶段标志（可选）
// @return 返回自身引用以支持链式调用
// 步骤1：验证多采样采样器的约束条件
// 步骤2：添加采样器参数到参数列表
MaterialBuilder& MaterialBuilder::parameter(const char* name, SamplerType samplerType,
        SamplerFormat format, ParameterPrecision precision, bool filterable, bool multisample,
        const char* transformName, std::optional<ShaderStageFlags> stages) {
    // 步骤1：验证多采样采样器的约束条件（多采样采样器只能用于SAMPLER_2D或SAMPLER_2D_ARRAY，且格式不能是SHADOW）
    FILAMENT_CHECK_PRECONDITION(
            !multisample || (format != SamplerFormat::SHADOW &&
                                    (samplerType == SamplerType::SAMPLER_2D ||
                                            samplerType == SamplerType::SAMPLER_2D_ARRAY)))
            << "multisample samplers only possible with SAMPLER_2D or SAMPLER_2D_ARRAY,"
               " as long as type is not SHADOW";

    // 步骤2：添加采样器参数到参数列表
    mParameters.emplace_back( name, samplerType, format, precision, filterable,
        multisample, transformName, stages );
    return *this;
}

// 添加常量参数（模板方法，支持int32_t、float、bool类型）
// @param name 常量名称
// @param type 常量类型
// @param defaultValue 默认值
// @return 返回自身引用以支持链式调用
// 步骤1：检查是否已存在同名常量
// 步骤2：创建常量对象并设置名称和类型
// 步骤3：根据模板类型验证类型匹配并设置默认值
// 步骤4：将常量添加到常量列表
template<typename T, typename>
MaterialBuilder& MaterialBuilder::constant(const char* name, ConstantType const type, T defaultValue) {
    // 步骤1：检查是否已存在同名常量
    auto result = std::find_if(mConstants.begin(), mConstants.end(), [name](const Constant& c) {
        return !strcmp(c.name.c_str(), name);
    });
    FILAMENT_CHECK_POSTCONDITION(result == mConstants.end())
            << "There is already a constant parameter present with the name " << name << ".";
    // 步骤2：创建常量对象并设置名称和类型
    Constant constant {
            .name = CString(name),
            .type = type,
    };
    auto toString = [](ConstantType const t) {
        switch (t) {
            case ConstantType::INT: return "INT";
            case ConstantType::FLOAT: return "FLOAT";
            case ConstantType::BOOL: return "BOOL";
        }
    };

    // 步骤3：根据模板类型验证类型匹配并设置默认值
    if constexpr (std::is_same_v<T, int32_t>) {
        FILAMENT_CHECK_POSTCONDITION(type == ConstantType::INT)
                << "Constant " << name << " was declared with type " << toString(type)
                << " but given an int default value.";
        constant.defaultValue.i = defaultValue;
    } else if constexpr (std::is_same_v<T, float>) {
        FILAMENT_CHECK_POSTCONDITION(type == ConstantType::FLOAT)
                << "Constant " << name << " was declared with type " << toString(type)
                << " but given a float default value.";
        constant.defaultValue.f = defaultValue;
    } else if constexpr (std::is_same_v<T, bool>) {
        FILAMENT_CHECK_POSTCONDITION(type == ConstantType::BOOL)
                << "Constant " << name << " was declared with type " << toString(type)
                << " but given a bool default value.";
        constant.defaultValue.b = defaultValue;
    } else {
        assert_invariant(false);
    }

    // 步骤4：将常量添加到常量列表
    mConstants.push_back(std::move(constant));
    return *this;
}
template MaterialBuilder& MaterialBuilder::constant<int32_t>(
        const char* name, ConstantType type, int32_t defaultValue);
template MaterialBuilder& MaterialBuilder::constant<float>(
        const char* name, ConstantType type, float defaultValue);
template MaterialBuilder& MaterialBuilder::constant<bool>(
        const char* name, ConstantType type, bool defaultValue);

// 添加缓冲区接口块
// @param bib 缓冲区接口块
// @return 返回自身引用以支持链式调用
// 步骤1：检查缓冲区数量是否超过限制
// 步骤2：将缓冲区添加到缓冲区列表
MaterialBuilder& MaterialBuilder::buffer(BufferInterfaceBlock bib) {
    // 步骤1：检查缓冲区数量是否超过限制
    FILAMENT_CHECK_POSTCONDITION(mBuffers.size() < MAX_BUFFERS_COUNT) << "Too many buffers";
    // 步骤2：将缓冲区添加到缓冲区列表
    mBuffers.emplace_back(std::make_unique<BufferInterfaceBlock>(std::move(bib)));
    return *this;
}

// 添加子通道输入（完整参数版本）
// @param subpassType 子通道类型
// @param format 采样器格式（必须是FLOAT）
// @param precision 精度限定符
// @param name 子通道名称
// @return 返回自身引用以支持链式调用
// 步骤1：验证格式必须为FLOAT
// 步骤2：检查子通道数量是否超过限制
// 步骤3：添加子通道到子通道列表
MaterialBuilder& MaterialBuilder::subpass(SubpassType subpassType, SamplerFormat format,
        ParameterPrecision precision, const char* name) {
    // 步骤1：验证格式必须为FLOAT
    FILAMENT_CHECK_PRECONDITION(format == SamplerFormat::FLOAT)
            << "Subpass parameters must have FLOAT format.";

    // 步骤2：检查子通道数量是否超过限制
    FILAMENT_CHECK_POSTCONDITION(mSubpassCount < MAX_SUBPASS_COUNT) << "Too many subpasses";
    // 步骤3：添加子通道到子通道列表
    mSubpasses[mSubpassCount++] = { name, subpassType, format, precision };
    return *this;
}

// 添加子通道输入（使用默认精度）
// @param subpassType 子通道类型
// @param format 采样器格式
// @param name 子通道名称
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::subpass(SubpassType const subpassType, SamplerFormat const format,
        const char* name) {
    return subpass(subpassType, format, ParameterPrecision::DEFAULT, name);
}

// 添加子通道输入（使用FLOAT格式）
// @param subpassType 子通道类型
// @param precision 精度限定符
// @param name 子通道名称
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::subpass(SubpassType const subpassType, ParameterPrecision const precision,
        const char* name) {
    return subpass(subpassType, SamplerFormat::FLOAT, precision, name);
}

// 添加子通道输入（使用默认格式和精度）
// @param subpassType 子通道类型
// @param name 子通道名称
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::subpass(SubpassType const subpassType, const char* name) {
    return subpass(subpassType, SamplerFormat::FLOAT, ParameterPrecision::DEFAULT, name);
}

// 要求顶点属性
// @param attribute 顶点属性
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::require(VertexAttribute const attribute) noexcept {
    mRequiredAttributes.set(attribute);
    return *this;
}

// 设置计算着色器的组大小
// @param groupSize 组大小（x, y, z）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::groupSize(math::uint3 const groupSize) noexcept {
    mGroupSize = groupSize;
    return *this;
}

// 使用默认深度变体
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::useDefaultDepthVariant() noexcept {
    mUseDefaultDepthVariant = true;
    return *this;
}

// 设置材质域
// @param materialDomain 材质域（SURFACE、POST_PROCESS、COMPUTE）
// @return 返回自身引用以支持链式调用
// 步骤1：设置材质域
// 步骤2：如果是计算着色器，确保功能级别至少为级别2
MaterialBuilder& MaterialBuilder::materialDomain(
        MaterialDomain const materialDomain) noexcept {
    // 步骤1：设置材质域
    mMaterialDomain = materialDomain;
    // 步骤2：如果是计算着色器，确保功能级别至少为级别2（计算着色器需要功能级别2）
    if (mMaterialDomain == MaterialDomain::COMPUTE) {
        // compute implies feature level 2
        if (mFeatureLevel < FeatureLevel::FEATURE_LEVEL_2) {
            mFeatureLevel = FeatureLevel::FEATURE_LEVEL_2;
        }
    }
    return *this;
}

// 设置折射模式
// @param refraction 折射模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::refractionMode(RefractionMode const refraction) noexcept {
    mRefractionMode = refraction;
    return *this;
}

// 设置折射类型
// @param refractionType 折射类型
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::refractionType(RefractionType const refractionType) noexcept {
    mRefractionType = refractionType;
    return *this;
}

// 设置着色器质量
// @param quality 着色器质量
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::quality(ShaderQuality const quality) noexcept {
    mShaderQuality = quality;
    return *this;
}

// 设置功能级别
// @param featureLevel 功能级别
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::featureLevel(FeatureLevel const featureLevel) noexcept {
    mFeatureLevel = featureLevel;
    return *this;
}

// 设置混合模式
// @param blending 混合模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::blending(BlendingMode const blending) noexcept {
    mBlendingMode = blending;
    return *this;
}

// 设置自定义混合函数
// @param srcRGB 源RGB混合函数
// @param srcA 源Alpha混合函数
// @param dstRGB 目标RGB混合函数
// @param dstA 目标Alpha混合函数
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::customBlendFunctions(
        BlendFunction const srcRGB, BlendFunction const srcA,
        BlendFunction const dstRGB, BlendFunction const dstA) noexcept {
    mCustomBlendFunctions[0] = srcRGB;
    mCustomBlendFunctions[1] = srcA;
    mCustomBlendFunctions[2] = dstRGB;
    mCustomBlendFunctions[3] = dstA;
    return *this;
}

// 设置光照后混合模式
// @param blending 混合模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::postLightingBlending(BlendingMode const blending) noexcept {
    mPostLightingBlendingMode = blending;
    return *this;
}

// 设置顶点域
// @param domain 顶点域
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::vertexDomain(VertexDomain const domain) noexcept {
    mVertexDomain = domain;
    return *this;
}

// 设置剔除模式
// @param culling 剔除模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::culling(CullingMode const culling) noexcept {
    mCullingMode = culling;
    return *this;
}

// 设置颜色写入
// @param enable 是否启用颜色写入
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::colorWrite(bool const enable) noexcept {
    mColorWrite = enable;
    return *this;
}

// 设置深度写入
// @param enable 是否启用深度写入
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::depthWrite(bool const enable) noexcept {
    mDepthWrite = enable;
    mDepthWriteSet = true;
    return *this;
}

// 设置深度测试
// @param enable 是否启用深度测试
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::depthCulling(bool const enable) noexcept {
    mDepthTest = enable;
    return *this;
}

// 设置实例化
// @param enable 是否启用实例化
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::instanced(bool const enable) noexcept {
    mInstanced = enable;
    return *this;
}

// 设置双面渲染
// @param doubleSided 是否启用双面渲染
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::doubleSided(bool const doubleSided) noexcept {
    mDoubleSided = doubleSided;
    mDoubleSidedCapability = true;
    return *this;
}

// 设置遮罩阈值
// @param threshold 遮罩阈值
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::maskThreshold(float const threshold) noexcept {
    mMaskThreshold = threshold;
    return *this;
}

// 设置Alpha到覆盖
// @param enable 是否启用Alpha到覆盖
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::alphaToCoverage(bool const enable) noexcept {
    mAlphaToCoverage = enable;
    mAlphaToCoverageSet = true;
    return *this;
}

// 设置阴影倍增器
// @param shadowMultiplier 是否启用阴影倍增器
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::shadowMultiplier(bool const shadowMultiplier) noexcept {
    mShadowMultiplier = shadowMultiplier;
    return *this;
}

// 设置透明阴影
// @param transparentShadow 是否启用透明阴影
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::transparentShadow(bool const transparentShadow) noexcept {
    mTransparentShadow = transparentShadow;
    return *this;
}

// 设置镜面反射抗锯齿
// @param specularAntiAliasing 是否启用镜面反射抗锯齿
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::specularAntiAliasing(bool const specularAntiAliasing) noexcept {
    mSpecularAntiAliasing = specularAntiAliasing;
    return *this;
}

// 设置镜面反射抗锯齿方差
// @param screenSpaceVariance 屏幕空间方差
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::specularAntiAliasingVariance(float const screenSpaceVariance) noexcept {
    mSpecularAntiAliasingVariance = screenSpaceVariance;
    return *this;
}

// 设置镜面反射抗锯齿阈值
// @param threshold 阈值
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::specularAntiAliasingThreshold(float const threshold) noexcept {
    mSpecularAntiAliasingThreshold = threshold;
    return *this;
}

// 设置清漆IOR变化
// @param clearCoatIorChange 是否启用清漆IOR变化
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::clearCoatIorChange(bool const clearCoatIorChange) noexcept {
    mClearCoatIorChange = clearCoatIorChange;
    return *this;
}

// 设置UV翻转
// @param flipUV 是否翻转UV
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::flipUV(bool const flipUV) noexcept {
    mFlipUV = flipUV;
    return *this;
}

// 设置线性雾
// @param enabled 是否启用线性雾
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::linearFog(bool const enabled) noexcept {
    mLinearFog = enabled;
    return *this;
}

// 设置阴影远距离衰减
// @param enabled 是否启用阴影远距离衰减
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::shadowFarAttenuation(bool const enabled) noexcept {
    mShadowFarAttenuation = enabled;
    return *this;
}

// 设置自定义表面着色
// @param customSurfaceShading 是否启用自定义表面着色
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::customSurfaceShading(bool const customSurfaceShading) noexcept {
    mCustomSurfaceShading = customSurfaceShading;
    return *this;
}

// 设置多反弹环境光遮蔽
// @param multiBounceAO 是否启用多反弹环境光遮蔽
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::multiBounceAmbientOcclusion(bool const multiBounceAO) noexcept {
    mMultiBounceAO = multiBounceAO;
    mMultiBounceAOSet = true;
    return *this;
}

// 设置镜面环境光遮蔽
// @param specularAO 镜面环境光遮蔽类型
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::specularAmbientOcclusion(SpecularAmbientOcclusion const specularAO) noexcept {
    mSpecularAO = specularAO;
    mSpecularAOSet = true;
    return *this;
}

// 设置透明度模式
// @param mode 透明度模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::transparencyMode(TransparencyMode const mode) noexcept {
    mTransparencyMode = mode;
    return *this;
}

// 设置立体类型
// @param stereoscopicType 立体类型
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::stereoscopicType(StereoscopicType const stereoscopicType) noexcept {
    mStereoscopicType = stereoscopicType;
    return *this;
}

// 设置立体眼数
// @param eyeCount 眼数
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::stereoscopicEyeCount(uint8_t const eyeCount) noexcept {
    mStereoscopicEyeCount = eyeCount;
    return *this;
}

// 设置反射模式
// @param mode 反射模式
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::reflectionMode(ReflectionMode const mode) noexcept {
    mReflectionMode = mode;
    return *this;
}

// 设置平台
// @param platform 平台（MOBILE、DESKTOP、ALL）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::platform(Platform const platform) noexcept {
    mPlatform = platform;
    return *this;
}

// 设置目标API（可以累加多个API）
// @param targetApi 目标API（OPENGL、VULKAN、METAL、WEBGPU）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::targetApi(TargetApi const targetApi) noexcept {
    mTargetApi |= targetApi;
    return *this;
}

// 设置优化级别
// @param optimization 优化级别（NONE、PREPROCESSOR、SIZE、PERFORMANCE）
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::optimization(Optimization const optimization) noexcept {
    mOptimization = optimization;
    return *this;
}

// 设置工作区
// @param workarounds 工作区标志
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::workarounds(Workarounds const workarounds) noexcept {
    mWorkarounds = workarounds;
    return *this;
}

// 设置是否打印着色器代码
// @param printShaders 是否打印着色器代码
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::printShaders(bool const printShaders) noexcept {
    mPrintShaders = printShaders;
    return *this;
}

// 设置是否保存原始变体
// @param saveRawVariants 是否保存原始变体
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::saveRawVariants(bool const saveRawVariants) noexcept {
    mSaveRawVariants = saveRawVariants;
    return *this;
}

// 设置是否生成调试信息
// @param generateDebugInfo 是否生成调试信息
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::generateDebugInfo(bool const generateDebugInfo) noexcept {
    mGenerateDebugInfo = generateDebugInfo;
    return *this;
}

// 设置变体过滤器
// @param variantFilter 变体过滤器掩码
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::variantFilter(UserVariantFilterMask const variantFilter) noexcept {
    mVariantFilter = variantFilter;
    return *this;
}

// 添加着色器定义
// @param name 定义名称
// @param value 定义值
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::shaderDefine(const char* name, const char* value) noexcept {
    mDefines.emplace_back(name, value);
    return *this;
}

// 检查是否有指定类型的采样器
// @param samplerType 采样器类型
// @return 如果存在指定类型的采样器返回true，否则返回false
// 步骤1：遍历所有参数
// 步骤2：检查参数是否为采样器且类型匹配
bool MaterialBuilder::hasSamplerType(SamplerType const samplerType) const noexcept {
    // 步骤1：遍历所有参数
    for (size_t i = 0, c = mParameters.size(); i < c; i++) {
        auto const& param = mParameters[i];
        // 步骤2：检查参数是否为采样器且类型匹配
        if (param.isSampler() && param.samplerType == samplerType) {
            return  true;
        }
    }
    return false;
}

// 准备构建材质包（构建材质信息）
// @param info 输出参数，材质信息
// 步骤1：准备代码生成排列
// 步骤2：确定默认着色器阶段
// 步骤3：构建采样器接口块和uniform接口块
// 步骤4：处理子通道输入
// 步骤5：添加缓冲区
// 步骤6：添加特殊uniform（镜面反射抗锯齿、遮罩阈值、双面渲染）
// 步骤7：设置必需的顶点属性
// 步骤8：构建接口块并填充材质信息
void MaterialBuilder::prepareToBuild(MaterialInfo& info) noexcept {
    // 步骤1：准备代码生成排列
    prepare(mEnableFramebufferFetch, mFeatureLevel);

    // 步骤2：确定默认着色器阶段
    const bool hasEmptyVertexCode = mMaterialVertexCode.getCode().empty();
    const bool isPostProcessMaterial = mMaterialDomain == MaterialDomain::POST_PROCESS;
    // TODO: Currently, for surface materials, we rely on the presence of a custom vertex shader to
    // infer the default shader stages. We could do better by analyzing the AST of the vertex shader
    // to see if the sampler is actually used.
    // 目前，对于表面材质，我们依赖自定义顶点着色器的存在来推断默认着色器阶段。我们可以通过分析顶点着色器的AST来做得更好，以查看采样器是否实际使用
    const ShaderStageFlags defaultShaderStages =
            isPostProcessMaterial || hasEmptyVertexCode
                    ? (ShaderStageFlags::FRAGMENT)
                    : (ShaderStageFlags::FRAGMENT | ShaderStageFlags::VERTEX);

    // Build the per-material sampler block and uniform block.
    // 步骤3：构建采样器接口块和uniform接口块
    SamplerInterfaceBlock::Builder sbb;
    BufferInterfaceBlock::Builder ibb;
    // sampler bindings start at 1, 0 is the ubo
    // 采样器绑定从1开始，0是UBO
    uint16_t binding = 1;
    for (size_t i = 0, c = mParameters.size(); i < c; i++) {
        auto const& param = mParameters[i];
        assert_invariant(!param.isSubpass());
        // 步骤3.1：处理采样器参数
        if (param.isSampler()) {
            ShaderStageFlags stages = param.stages.value_or(defaultShaderStages);
            sbb.add({ param.name.data(), param.name.size() }, binding, param.samplerType,
                    param.format, param.precision, param.filterable, param.multisample,
                    { param.transformName.data(), param.transformName.size() }, stages);
            // 如果采样器有变换矩阵，添加到uniform块
            if (!param.transformName.empty()) {
                ibb.add({ { { param.transformName.data(), param.transformName.size() },
                    uint8_t(binding), 0, UniformType::MAT3, Precision::DEFAULT,
                    FeatureLevel::FEATURE_LEVEL_0 } });
            }
            binding++;
        // 步骤3.2：处理uniform参数
        } else if (param.isUniform()) {
            ibb.add({{{ param.name.data(), param.name.size() },
                      uint32_t(param.size == 1u ? 0u : param.size), param.uniformType,
                      param.precision, FeatureLevel::FEATURE_LEVEL_0 }});
        }
    }

    // 步骤4：处理子通道输入
    for (size_t i = 0, c = mSubpassCount; i < c; i++) {
        auto const& param = mSubpasses[i];
        assert_invariant(param.isSubpass());
        // For now, we only support a single subpass for attachment 0.
        // Subpasses belong to the "MaterialParams" block.
        // 目前，我们只支持附件0的单个子通道。子通道属于"MaterialParams"块
        info.subpass = { CString("MaterialParams"), param.name, param.subpassType,
                         param.format, param.precision, 0, 0 };
    }

    // 步骤5：添加缓冲区
    for (auto const& buffer : mBuffers) {
        info.buffers.emplace_back(buffer.get());
    }

    // 步骤6：添加特殊uniform
    // 步骤6.1：如果启用镜面反射抗锯齿，添加相关uniform
    if (mSpecularAntiAliasing) {
        ibb.add({
                { "_specularAntiAliasingVariance",  0, UniformType::FLOAT },
                { "_specularAntiAliasingThreshold", 0, UniformType::FLOAT },
        });
    }

    // 步骤6.2：如果使用遮罩混合模式，添加遮罩阈值uniform
    if (mBlendingMode == BlendingMode::MASKED) {
        ibb.add({{ "_maskThreshold", 0, UniformType::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 }});
    }

    // 步骤6.3：如果启用双面渲染能力，添加双面渲染uniform
    if (mDoubleSidedCapability) {
        ibb.add({{ "_doubleSided", 0, UniformType::BOOL, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 }});
    }

    // 步骤7：设置必需的顶点属性
    mRequiredAttributes.set(POSITION);
    if (mShading != Shading::UNLIT || mShadowMultiplier) {
        mRequiredAttributes.set(TANGENTS);
    }

    // 步骤8：构建接口块并填充材质信息
    info.sib = sbb.name("MaterialParams").build();
    info.uib = ibb.name("MaterialParams").build();

    info.isLit = isLit();
    info.hasDoubleSidedCapability = mDoubleSidedCapability;
    info.hasExternalSamplers = hasSamplerType(SamplerType::SAMPLER_EXTERNAL);
    info.has3dSamplers = hasSamplerType(SamplerType::SAMPLER_3D);
    info.specularAntiAliasing = mSpecularAntiAliasing;
    info.clearCoatIorChange = mClearCoatIorChange;
    info.flipUV = mFlipUV;
    info.linearFog = mLinearFog;
    info.shadowFarAttenuation = mShadowFarAttenuation;
    info.requiredAttributes = mRequiredAttributes;
    info.blendingMode = mBlendingMode;
    info.postLightingBlendingMode = mPostLightingBlendingMode;
    info.shading = mShading;
    info.hasShadowMultiplier = mShadowMultiplier;
    info.hasTransparentShadow = mTransparentShadow;
    info.multiBounceAO = mMultiBounceAO;
    info.multiBounceAOSet = mMultiBounceAOSet;
    info.specularAO = mSpecularAO;
    info.specularAOSet = mSpecularAOSet;
    info.refractionMode = mRefractionMode;
    info.refractionType = mRefractionType;
    info.reflectionMode = mReflectionMode;
    info.quality = mShaderQuality;
    info.hasCustomSurfaceShading = mCustomSurfaceShading;
    info.useLegacyMorphing = mUseLegacyMorphing;
    info.instanced = mInstanced;
    info.vertexDomainDeviceJittered = mVertexDomainDeviceJittered;
    info.featureLevel = mFeatureLevel;
    info.groupSize = mGroupSize;
    info.stereoscopicType = mStereoscopicType;
    info.stereoscopicEyeCount = mStereoscopicEyeCount;

    // This is determined via static analysis of the glsl after prepareToBuild().
    info.userMaterialHasCustomDepth = false;
}

// 初始化推送常量
// 步骤1：预留推送常量空间
// 步骤2：从全局推送常量定义转换并填充推送常量列表
void MaterialBuilder::initPushConstants() noexcept {
    // 步骤1：预留推送常量空间
    mPushConstants.reserve(PUSH_CONSTANTS.size());
    mPushConstants.resize(PUSH_CONSTANTS.size());
    // 步骤2：从全局推送常量定义转换并填充推送常量列表
    std::transform(PUSH_CONSTANTS.cbegin(), PUSH_CONSTANTS.cend(), mPushConstants.begin(),
            [](MaterialPushConstant const& inConstant) -> PushConstant {
                return {
                    .name = inConstant.name,
                    .type = inConstant.type,
                    .stage = inConstant.stage,
                };
            });
}

// 查找着色器属性（通过静态代码分析）
// @param type 着色器阶段
// @param allProperties 所有属性列表
// @param semanticCodeGenParams 代码生成参数
// @return 如果成功找到属性返回true，否则返回false
// 步骤1：生成包含所有属性的着色器代码
// 步骤2：使用GLSL工具查找属性
// 步骤3：如果失败且启用打印着色器，输出着色器代码
bool MaterialBuilder::findProperties(backend::ShaderStage const type,
        PropertyList const& allProperties,
        CodeGenParams const& semanticCodeGenParams) noexcept {
    GLSLTools const glslTools;
    // 步骤1：生成包含所有属性的着色器代码
    std::string const shaderCodeAllProperties = peek(type, semanticCodeGenParams, allProperties);
    // Populate mProperties with the properties set in the shader.
    // 步骤2：使用GLSL工具查找属性（填充mProperties，包含着色器中设置的属性）
    if (!glslTools.findProperties(type, shaderCodeAllProperties, mProperties,
            semanticCodeGenParams.targetApi,
            semanticCodeGenParams.targetLanguage,
            semanticCodeGenParams.shaderModel)) {
        // 步骤3：如果失败且启用打印着色器，输出着色器代码
        if (mPrintShaders) {
            slog.e << shaderCodeAllProperties << io::endl;
        }
        return false;
    }
    return true;
}

// 查找所有属性（仅用于表面材质）
// @param semanticCodeGenParams 代码生成参数
// @return 如果成功找到所有属性返回true，否则返回false
// 步骤1：如果不是表面材质，直接返回成功
// 步骤2：创建包含所有属性的属性列表
// 步骤3：查找片段着色器属性
// 步骤4：查找顶点着色器属性
bool MaterialBuilder::findAllProperties(CodeGenParams const& semanticCodeGenParams) noexcept {
    // 步骤1：如果不是表面材质，直接返回成功
    if (mMaterialDomain != MaterialDomain::SURFACE) {
        return true;
    }

    using namespace backend;

    // Some fields in MaterialInputs only exist if the property is set (e.g: normal, subsurface
    // for cloth shading model). Give our shader all properties. This will enable us to parse and
    // static code analyse the AST.
    // MaterialInputs中的某些字段仅在设置属性时存在（例如：布料着色模型的normal、subsurface）。
    // 为着色器提供所有属性。这将使我们能够解析和静态代码分析AST
    // 步骤2：创建包含所有属性的属性列表
    PropertyList allProperties;
    std::fill_n(allProperties, MATERIAL_PROPERTIES_COUNT, true);
    // 步骤3：查找片段着色器属性
    if (!findProperties(ShaderStage::FRAGMENT, allProperties, semanticCodeGenParams)) {
        return false;
    }
    // 步骤4：查找顶点着色器属性
    if (!findProperties(ShaderStage::VERTEX, allProperties, semanticCodeGenParams)) {
        return false;
    }
    return true;
}

// 运行语义分析（验证着色器代码）
// @param inOutInfo 输入输出参数，材质信息
// @param semanticCodeGenParams 代码生成参数
// @return 如果语义分析成功返回true，否则返回false
// 步骤1：确定目标API（如果启用帧缓冲获取，强制使用Vulkan）
// 步骤2：根据材质域分析着色器（计算着色器或顶点/片段着色器）
// 步骤3：如果失败且启用打印着色器，输出着色器代码
bool MaterialBuilder::runSemanticAnalysis(MaterialInfo* inOutInfo,
        CodeGenParams const& semanticCodeGenParams) noexcept {
    using namespace backend;

    // 步骤1：确定目标API
    TargetApi targetApi = semanticCodeGenParams.targetApi;
    TargetLanguage const targetLanguage = semanticCodeGenParams.targetLanguage;
    assertSingleTargetApi(targetApi);

    // 如果启用帧缓冲获取，强制使用Vulkan（帧缓冲获取仅在Vulkan语义下可用）
    if (mEnableFramebufferFetch) {
        // framebuffer fetch is only available with vulkan semantics
        targetApi = TargetApi::VULKAN;
    }

    // 步骤2：根据材质域分析着色器
    bool success = false;
    std::string shaderCode;
    ShaderModel const model = semanticCodeGenParams.shaderModel;
    if (mMaterialDomain == MaterialDomain::COMPUTE) {
        // 步骤2.1：分析计算着色器
        shaderCode = peek(ShaderStage::COMPUTE, semanticCodeGenParams, mProperties);
        success = GLSLTools::analyzeComputeShader(shaderCode, model,
                targetApi, targetLanguage);
    } else {
        // 步骤2.2：分析顶点着色器
        shaderCode = peek(ShaderStage::VERTEX, semanticCodeGenParams, mProperties);
        success = GLSLTools::analyzeVertexShader(shaderCode, model, mMaterialDomain,
                targetApi, targetLanguage);
        if (success) {
            // 步骤2.3：分析片段着色器
            shaderCode = peek(ShaderStage::FRAGMENT, semanticCodeGenParams, mProperties);
            auto const result = GLSLTools::analyzeFragmentShader(shaderCode, model, mMaterialDomain,
                    targetApi, targetLanguage, mCustomSurfaceShading);
            success = result.has_value();
            if (success) {
                // 步骤2.4：更新自定义深度信息
                inOutInfo->userMaterialHasCustomDepth = result->userMaterialHasCustomDepth;
            }
        }
    }
    // 步骤3：如果失败且启用打印着色器，输出着色器代码
    if (!success && mPrintShaders) {
        slog.e << shaderCode << io::endl;
    }
    return success;
}

// 显示错误消息（静态辅助函数）
// @param materialName 材质名称
// @param variant 着色器变体
// @param targetApi 目标API
// @param shaderType 着色器阶段
// @param featureLevel 功能级别
// @param shaderCode 着色器代码
// 步骤1：根据目标API确定API字符串
// 步骤2：根据着色器阶段确定阶段字符串
// 步骤3：输出错误消息和着色器代码
static void showErrorMessage(const char* materialName, filament::Variant const variant,
        MaterialBuilder::TargetApi const targetApi, backend::ShaderStage const shaderType,
        MaterialBuilder::FeatureLevel const featureLevel,
        const std::string& shaderCode) {
    using ShaderStage = backend::ShaderStage;
    using TargetApi = MaterialBuilder::TargetApi;

    // 步骤1：根据目标API确定API字符串
    const char* targetApiString = "unknown";
    switch (targetApi) {
        case TargetApi::OPENGL:
            targetApiString = (featureLevel == MaterialBuilder::FeatureLevel::FEATURE_LEVEL_0)
                              ? "GLES 2.0.\n" : "OpenGL.\n";
            break;
        case TargetApi::VULKAN:
            targetApiString = "Vulkan.\n";
            break;
        case TargetApi::METAL:
            targetApiString = "Metal.\n";
            break;
        case TargetApi::WEBGPU:
            targetApiString = "WebGPU.\n";
            break;
        case TargetApi::ALL:
            assert_invariant(false); // Unreachable.
            break;
    }

    // 步骤2：根据着色器阶段确定阶段字符串
    const char* shaderStageString = "unknown";
    switch (shaderType) {
        case ShaderStage::VERTEX:
            shaderStageString = "Vertex Shader\n";
            break;
        case ShaderStage::FRAGMENT:
            shaderStageString = "Fragment Shader\n";
            break;
        case ShaderStage::COMPUTE:
            shaderStageString = "Compute Shader\n";
            break;
    }

    // 步骤3：输出错误消息和着色器代码
    slog.e
            << "Error in \"" << materialName << "\""
            << ", Variant 0x" << io::hex << +variant.key
            << ", " << targetApiString
            << "=========================\n"
            << "Generated " << shaderStageString
            << "=========================\n"
            << shaderCode;
}

// 生成着色器（为所有变体和代码生成排列生成着色器）
// @param jobSystem 作业系统（用于并行处理）
// @param variants 着色器变体列表
// @param container 块容器（用于存储生成的着色器）
// @param info 材质信息
// @return 如果成功生成所有着色器返回true，否则返回false
// 步骤1：创建GLSL后处理器
// 步骤2：初始化共享数据结构（需要锁保护）
// 步骤3：创建着色器生成器
// 步骤4：设置自定义深度着色器标志
// 步骤5：初始化作业控制变量
bool MaterialBuilder::generateShaders(JobSystem& jobSystem, const std::vector<Variant>& variants,
        ChunkContainer& container, const MaterialInfo& info) const noexcept {
    // Create a postprocessor to optimize / compile to Spir-V if necessary.
    // 步骤1：创建GLSL后处理器（用于优化/编译为SPIR-V）
    uint32_t flags = 0;
    flags |= mPrintShaders ? GLSLPostProcessor::PRINT_SHADERS : 0;
    flags |= mGenerateDebugInfo ? GLSLPostProcessor::GENERATE_DEBUG_INFO : 0;
    GLSLPostProcessor postProcessor(mOptimization, mWorkarounds, flags);

    // Start: must be protected by lock
    // 步骤2：初始化共享数据结构（需要锁保护）
    Mutex entriesLock;
    std::vector<TextEntry> glslEntries;
    std::vector<TextEntry> essl1Entries;
    std::vector<BinaryEntry> spirvEntries;
    std::vector<TextEntry> metalEntries;
    std::vector<TextEntry> wgslEntries;
    LineDictionary textDictionary;
    BlobDictionary spirvDictionary;
    // End: must be protected by lock

    // 步骤3：创建着色器生成器
    ShaderGenerator sg(mProperties, mVariables, mOutputs, mDefines, mConstants, mPushConstants,
            mMaterialFragmentCode.getCode(), mMaterialFragmentCode.getLineOffset(),
            mMaterialVertexCode.getCode(), mMaterialVertexCode.getLineOffset(),
            mMaterialDomain);

    // 步骤4：设置自定义深度着色器标志
    container.emplace<bool>(MaterialHasCustomDepthShader,
            needsStandardDepthProgram() && !mUseDefaultDepthVariant);

    // 步骤5：初始化作业控制变量
    std::atomic_bool cancelJobs(false);
    bool firstJob = true;

    // 步骤6：遍历所有代码生成排列（为每个排列生成着色器）
    for (const auto& params : mCodeGenPermutations) {
        // 步骤6.1：检查是否有作业被取消
        if (cancelJobs.load()) {
            return false;
        }

        // 步骤6.2：提取代码生成参数
        const ShaderModel shaderModel = ShaderModel(params.shaderModel);
        const TargetApi targetApi = params.targetApi;
        const TargetLanguage targetLanguage = params.targetLanguage;
        const FeatureLevel featureLevel = params.featureLevel;

        assertSingleTargetApi(targetApi);

        // Metal Shading Language is cross-compiled from Vulkan.
        // Metal着色语言是从Vulkan交叉编译的
        // 步骤6.3：确定目标API需要的着色器格式
        const bool targetApiNeedsSpirv =
                (targetApi == TargetApi::VULKAN || targetApi == TargetApi::METAL || targetApi == TargetApi::WEBGPU);
        const bool targetApiNeedsMsl = targetApi == TargetApi::METAL;
        const bool targetApiNeedsWgsl = targetApi == TargetApi::WEBGPU;
        const bool targetApiNeedsGlsl = targetApi == TargetApi::OPENGL;

        // Set when a job fails
        // 步骤6.4：创建父作业（用于管理所有变体作业）
        JobSystem::Job* parent = jobSystem.createJob();

        // 步骤6.5：为每个变体创建作业
        for (const auto& v : variants) {
            JobSystem::Job* job = jobs::createJob(jobSystem, parent, [&]() {
                // 步骤6.5.1：检查是否有作业被取消
                if (cancelJobs.load()) {
                    return;
                }

                // TODO: avoid allocations when not required
                // 步骤6.5.2：分配着色器输出缓冲区（SPIR-V、MSL、WGSL）
                std::vector<uint32_t> spirv;
                std::string msl;
                std::string wgsl;

                // 步骤6.5.3：根据目标API设置输出指针
                std::vector<uint32_t>* pSpirv = targetApiNeedsSpirv ? &spirv : nullptr;
                std::string* pMsl = targetApiNeedsMsl ? &msl : nullptr;
                std::string* pWgsl = targetApiNeedsWgsl ? &wgsl : nullptr;

                // 步骤6.5.4：初始化着色器条目（用于存储生成的着色器）
                TextEntry glslEntry{};
                BinaryEntry spirvEntry{};
                TextEntry metalEntry{};
                TextEntry wgslEntry{};

                // 步骤6.5.5：设置着色器条目的着色器模型和变体
                glslEntry.shaderModel  = params.shaderModel;
                spirvEntry.shaderModel = params.shaderModel;
                metalEntry.shaderModel = params.shaderModel;
                wgslEntry.shaderModel = params.shaderModel;

                glslEntry.variant  = v.variant;
                spirvEntry.variant = v.variant;
                metalEntry.variant = v.variant;
                wgslEntry.variant = v.variant;

                // Generate raw shader code.
                // The quotes in Google-style line directives cause problems with certain drivers. These
                // directives are optimized away when using the full filamat, so down below we
                // explicitly remove them when using filamat lite.
                // 生成原始着色器代码
                // Google风格的行指令中的引号会导致某些驱动程序出现问题。
                // 使用完整filamat时这些指令会被优化掉，所以下面在使用filamat lite时我们显式移除它们
                // 步骤6.5.6：根据着色器阶段生成原始着色器代码
                std::string shader;
                if (v.stage == ShaderStage::VERTEX) {
                    shader = sg.createSurfaceVertexProgram(
                            shaderModel, targetApi, targetLanguage, featureLevel,
                            info, v.variant, mInterpolation, mVertexDomain);
                } else if (v.stage == ShaderStage::FRAGMENT) {
                    shader = sg.createSurfaceFragmentProgram(
                            shaderModel, targetApi, targetLanguage, featureLevel,
                            info, v.variant, mInterpolation, mVariantFilter);
                } else if (v.stage == ShaderStage::COMPUTE) {
                    shader = sg.createSurfaceComputeProgram(
                            shaderModel, targetApi, targetLanguage, featureLevel,
                            info);
                }

                // Write the variant to a file.
                // 步骤6.5.7：如果启用保存原始变体，将变体写入文件
                if (UTILS_UNLIKELY(mSaveRawVariants)) {
                    int const variantKey = v.variant.key;
                    auto getExtension = [](backend::ShaderStage const stage) {
                        switch (stage) {
                            case ShaderStage::VERTEX:
                                return "vert";
                            case ShaderStage::FRAGMENT:
                                return "frag";
                            case ShaderStage::COMPUTE:
                                return "comp";
                        }
                        return "unknown";
                    };
                    char filename[256];
                    snprintf(filename, sizeof(filename), "%s_0x%02x.%s", mMaterialName.c_str_safe(),
                            variantKey, getExtension(v.stage));
                    printf("Writing variant 0x%02x to %s\n", variantKey, filename);
                    std::ofstream file(filename);
                    if (file.is_open()) {
                        file << shader;
                        file.close();
                    }
                }

                // 步骤6.5.8：设置GLSL输出指针（如果目标API需要GLSL）
                std::string* pGlsl = nullptr;
                if (targetApiNeedsGlsl) {
                    pGlsl = &shader;
                }

                // 步骤6.5.9：配置GLSL后处理器
                GLSLPostProcessor::Config config{
                        .variant = v.variant,
                        .variantFilter = mVariantFilter,
                        .targetApi = targetApi,
                        .targetLanguage = targetLanguage,
                        .workarounds = mWorkarounds,
                        .shaderType = v.stage,
                        .shaderModel = shaderModel,
                        .featureLevel = featureLevel,
                        .domain = mMaterialDomain,
                        .materialInfo = &info,
                        .hasFramebufferFetch = mEnableFramebufferFetch,
                        .usesClipDistance = v.variant.hasStereo() && info.stereoscopicType == StereoscopicType::INSTANCED,
                        .glsl = {},
                };

                // 步骤6.5.10：如果启用帧缓冲获取，配置子通道输入到颜色位置映射
                if (mEnableFramebufferFetch) {
                    config.glsl.subpassInputToColorLocation.emplace_back(0, 0);
                }

                // 步骤6.5.11：处理着色器（编译、优化、转换）
                bool const ok = postProcessor.process(shader, config, pGlsl, pSpirv, pMsl, pWgsl);
                if (!ok) {
                    // 步骤6.5.12：如果处理失败，显示错误消息并取消所有作业
                    showErrorMessage(mMaterialName.c_str_safe(), v.variant, targetApi, v.stage,
                                     featureLevel, shader);
                    cancelJobs = true;
                    if (mPrintShaders) {
                        slog.e << shader << io::endl;
                    }
                    return;
                }

                // 步骤6.5.13：如果是OpenGL且使用SPIR-V，修复外部采样器
                if (targetApi == TargetApi::OPENGL) {
                    if (targetLanguage == TargetLanguage::SPIRV) {
                        ShaderGenerator::fixupExternalSamplers(shaderModel, shader, featureLevel,
                                info);
                    }
                }

                // NOTE: Everything below touches shared structures protected by a lock
                // NOTE: do not execute expensive work from here on!
                // 注意：下面所有内容都会触及受锁保护的共享结构
                // 注意：从这里开始不要执行昂贵的工作！
                // 步骤6.5.14：获取锁以访问共享数据结构
                std::unique_lock const lock(entriesLock);

                // below we rely on casting ShaderStage to uint8_t
                // 下面我们依赖将ShaderStage转换为uint8_t
                static_assert(sizeof(backend::ShaderStage) == 1);

                // 步骤6.5.15：根据目标API将生成的着色器添加到相应的条目列表
                switch (targetApi) {
                    case TargetApi::WEBGPU:
                        // WebGPU需要SPIR-V和WGSL
                        assert_invariant(!spirv.empty());
                        assert_invariant(!wgsl.empty());
                        wgslEntry.stage = v.stage;
                        wgslEntry.shader = wgsl;
                        wgslEntries.push_back(wgslEntry);
                        break;
                    case TargetApi::ALL:
                        // should never happen
                        // 不应该发生
                        break;
                    case TargetApi::OPENGL:
                        // OpenGL使用GLSL，根据功能级别分类
                        glslEntry.stage = v.stage;
                        glslEntry.shader = shader;
                        if (featureLevel == FeatureLevel::FEATURE_LEVEL_0) {
                            essl1Entries.push_back(glslEntry);
                        } else {
                            glslEntries.push_back(glslEntry);
                        }
                        break;
                    case TargetApi::VULKAN: {
                        // Vulkan使用SPIR-V二进制
                        assert_invariant(!spirv.empty());
                        std::vector d(reinterpret_cast<uint8_t*>(spirv.data()),
                                reinterpret_cast<uint8_t*>(spirv.data() + spirv.size()));
                        spirvEntry.stage = v.stage;
                        spirvEntry.data = std::move(d);
                        spirvEntries.push_back(spirvEntry);
                        break;
                    }
                    case TargetApi::METAL:
                        // Metal需要SPIR-V和MSL
                        assert_invariant(!spirv.empty());
                        assert_invariant(!msl.empty());
                        metalEntry.stage = v.stage;
                        metalEntry.shader = msl;
                        metalEntries.push_back(metalEntry);
                        break;
                }
            });

            // NOTE: We run the first job separately to work the lack of thread safety
            //       guarantees in glslang. This library performs unguarded global
            //       operations on first use.
            // 注意：我们单独运行第一个作业，以解决glslang缺乏线程安全保证的问题。
            // 该库在首次使用时执行未受保护的全局操作
            if (firstJob) {
                jobSystem.runAndWait(job);
                firstJob = false;
            } else {
                jobSystem.run(job);
            }
        }

        jobSystem.runAndWait(parent);
    }

    // 步骤6：检查是否有作业被取消
    if (cancelJobs.load()) {
        return false;
    }

    // Sort the variants.
    // 步骤7：对变体进行排序（按着色器模型、变体键、着色器阶段排序）
    auto compare = [](const auto& a, const auto& b) {
        static_assert(sizeof(decltype(a.variant.key)) == 1);
        static_assert(sizeof(decltype(b.variant.key)) == 1);
        const uint32_t akey = (uint32_t(a.shaderModel) << 16) | (uint32_t(a.variant.key) << 8) | uint32_t(a.stage);
        const uint32_t bkey = (uint32_t(b.shaderModel) << 16) | (uint32_t(b.variant.key) << 8) | uint32_t(b.stage);
        return akey < bkey;
    };
    std::sort(glslEntries.begin(), glslEntries.end(), compare);
    std::sort(essl1Entries.begin(), essl1Entries.end(), compare);
    std::sort(spirvEntries.begin(), spirvEntries.end(), compare);
    std::sort(metalEntries.begin(), metalEntries.end(), compare);
    std::sort(wgslEntries.begin(), wgslEntries.end(), compare);

    // Generate the dictionaries.
    // 步骤8：生成字典（文本字典和SPIR-V字典）
    for (const auto& s : glslEntries) {
        textDictionary.addText(s.shader);
    }
    for (const auto& s : essl1Entries) {
        textDictionary.addText(s.shader);
    }
    for (auto& s : spirvEntries) {
        std::vector const spirv{ std::move(s.data) };
        s.dictionaryIndex = spirvDictionary.addBlob(spirv);
    }
    for (const auto& s : metalEntries) {
        textDictionary.addText(s.shader);
    }
    for (const auto& s : wgslEntries) {
        textDictionary.addText(s.shader);
    }

    // Emit dictionary chunk (TextDictionaryReader and DictionaryTextChunk)
    // 步骤9：发出字典块（文本字典读取器和字典文本块）
    const auto& dictionaryChunk = container.push<DictionaryTextChunk>(
            std::move(textDictionary), DictionaryText);

    // Emit GLSL chunk (MaterialTextChunk).
    // 步骤10：发出GLSL块（材质文本块）
    if (!glslEntries.empty()) {
        container.push<MaterialTextChunk>(std::move(glslEntries),
                dictionaryChunk.getDictionary(), MaterialGlsl);
    }

    // Emit ESSL1 chunk (MaterialTextChunk).
    // 步骤11：发出ESSL1块（材质文本块）
    if (!essl1Entries.empty()) {
        container.push<MaterialTextChunk>(std::move(essl1Entries),
                dictionaryChunk.getDictionary(), MaterialEssl1);
    }

    // Emit SPIRV chunks (SpirvDictionaryReader and MaterialBinaryChunk).
    // 步骤12：发出SPIR-V块（SPIR-V字典读取器和材质二进制块）
    if (!spirvEntries.empty()) {
        const bool stripInfo = !mGenerateDebugInfo;
        container.push<DictionarySpirvChunk>(std::move(spirvDictionary), stripInfo);
        container.push<MaterialBinaryChunk>(std::move(spirvEntries), MaterialSpirv);
    }

    // Emit Metal chunk (MaterialTextChunk).
    // 步骤13：发出Metal块（材质文本块）
    if (!metalEntries.empty()) {
        container.push<MaterialTextChunk>(std::move(metalEntries),
                dictionaryChunk.getDictionary(), MaterialMetal);
    }

    // Emit WGSL chunk (MaterialTextChunk).
    // 步骤14：发出WGSL块（材质文本块）
    if (!wgslEntries.empty()) {
        container.push<MaterialTextChunk>(std::move(wgslEntries),
                dictionaryChunk.getDictionary(), MaterialWgsl);
    }

    return true;
}

// 添加输出变量
// @param qualifier 变量限定符（IN/OUT）
// @param target 输出目标（COLOR/DEPTH）
// @param precision 精度限定符
// @param type 输出类型
// @param name 输出名称
// @param location 输出位置（-1表示使用默认位置）
// @return 返回自身引用以支持链式调用
// 步骤1：验证深度输出的约束条件
// 步骤2：确定输出位置（如果为-1，使用默认位置）
// 步骤3：添加输出到输出列表
// 步骤4：检查输出数量限制
MaterialBuilder& MaterialBuilder::output(VariableQualifier qualifier, OutputTarget target,
        Precision precision, OutputType type, const char* name, int location) {
    // 步骤1：验证深度输出的约束条件
    FILAMENT_CHECK_PRECONDITION(target != OutputTarget::DEPTH || type == OutputType::FLOAT)
            << "Depth outputs must be of type FLOAT.";
    FILAMENT_CHECK_PRECONDITION(
            target != OutputTarget::DEPTH || qualifier == VariableQualifier::OUT)
            << "Depth outputs must use OUT qualifier.";

    FILAMENT_CHECK_PRECONDITION(location >= -1)
            << "Output location must be >= 0 (or use -1 for default location).";

    // A location value of -1 signals using the default location. We'll simply take the previous
    // output's location and add 1.
    // 步骤2：确定输出位置（如果为-1，使用默认位置：取前一个输出的位置加1）
    if (location == -1) {
        location = mOutputs.empty() ? 0 : mOutputs.back().location + 1;
    }

    // Unconditionally add this output, then we'll check if we've maxed on on any particular target.
    // 步骤3：添加输出到输出列表（无条件添加，然后检查是否超过任何特定目标的最大值）
    mOutputs.emplace_back(name, qualifier, target, precision, type, location);

    // 步骤4：检查输出数量限制
    uint8_t colorOutputCount = 0;
    uint8_t depthOutputCount = 0;
    for (const auto& output : mOutputs) {
        if (output.target == OutputTarget::COLOR) {
            colorOutputCount++;
        }
        if (output.target == OutputTarget::DEPTH) {
            depthOutputCount++;
        }
    }

    FILAMENT_CHECK_PRECONDITION(colorOutputCount <= MAX_COLOR_OUTPUT)
            << "A maximum of " << MAX_COLOR_OUTPUT << " COLOR outputs is allowed.";
    FILAMENT_CHECK_PRECONDITION(depthOutputCount <= MAX_DEPTH_OUTPUT)
            << "A maximum of " << MAX_DEPTH_OUTPUT << " DEPTH output is allowed.";

    assert_invariant(mOutputs.size() <= MAX_COLOR_OUTPUT + MAX_DEPTH_OUTPUT);

    return *this;
}

// 启用帧缓冲获取（临时API，用于为GLSL着色器启用EXT_framebuffer_fetch）
// @return 返回自身引用以支持链式调用
// 注意：此API是临时的，用于为GLSL着色器启用EXT_framebuffer_fetch，Filament的后处理阶段很少使用
MaterialBuilder& MaterialBuilder::enableFramebufferFetch() noexcept {
    // This API is temporary, it is used to enable EXT_framebuffer_fetch for GLSL shaders,
    // this is used sparingly by filament's post-processing stage.
    mEnableFramebufferFetch = true;
    return *this;
}

// 设置顶点域设备抖动
// @param enabled 是否启用顶点域设备抖动
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::vertexDomainDeviceJittered(bool const enabled) noexcept {
    mVertexDomainDeviceJittered = enabled;
    return *this;
}

// 使用传统变形
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::useLegacyMorphing() noexcept {
    mUseLegacyMorphing = true;
    return *this;
}

// 设置材质源
// @param source 材质源字符串视图
// @return 返回自身引用以支持链式调用
MaterialBuilder& MaterialBuilder::materialSource(std::string_view source) noexcept {
    mMaterialSource = source;
    return *this;
}

// 构建材质包（主要构建方法）
// @param jobSystem 作业系统（用于并行处理）
// @return 返回材质包，如果构建失败返回无效包
// 步骤1：检查MaterialBuilder是否已初始化
// 步骤2：验证和调整材质配置
// 步骤3：准备构建材质信息
// 步骤4：检查功能级别特性
// 步骤5：查找所有属性
// 步骤6：运行语义分析
Package MaterialBuilder::build(JobSystem& jobSystem) {
    // 步骤1：检查MaterialBuilder是否已初始化
    if (materialBuilderClients == 0) {
        slog.e << "Error: MaterialBuilder::init() must be called before build()." << io::endl;
        // Return an empty package to signal a failure to build the material.
        // 返回空包以表示构建材质失败
error:
        return Package::invalidPackage();
    }

    // Force post process materials to be unlit. This prevents imposing a lot of extraneous
    // data, code, and expectations for materials which do not need them.
    // 步骤2：验证和调整材质配置
    // 步骤2.1：强制后处理材质为未光照（这可以避免为不需要它们的材质强加大量无关数据、代码和期望）
    if (mMaterialDomain == MaterialDomain::POST_PROCESS) {
        mShading = Shading::UNLIT;
    }

    // Add a default color output.
    // 步骤2.2：为后处理材质添加默认颜色输出
    if (mMaterialDomain == MaterialDomain::POST_PROCESS && mOutputs.empty()) {
        output(VariableQualifier::OUT,
                OutputTarget::COLOR, Precision::DEFAULT, OutputType::FLOAT4, "color");
    }

    // 步骤2.3：检查表面材质的约束条件
    if (mMaterialDomain == MaterialDomain::SURFACE) {
        if (mRequiredAttributes[COLOR] &&
            !mVariables[int(Variable::CUSTOM4)].name.empty()) {
            // both the color attribute and the custom4 variable are present, that's not supported
            // 颜色属性和custom4变量同时存在，这是不支持的
            slog.e << "Error: when the 'color' attribute is required 'Variable::CUSTOM4' is not supported." << io::endl;
            goto error;
        }
    }

    // TODO: maybe check MaterialDomain::COMPUTE has outputs

    // 步骤2.4：检查自定义表面着色的约束条件
    if (mCustomSurfaceShading && mShading != Shading::LIT) {
        slog.e << "Error: customSurfaceShading can only be used with lit materials." << io::endl;
        goto error;
    }

    // prepareToBuild must be called first, to populate mCodeGenPermutations.
    // 步骤3：准备构建材质信息（必须先调用prepareToBuild，以填充mCodeGenPermutations）
    MaterialInfo info{};
    prepareToBuild(info);

    // check level features
    // 步骤4：检查功能级别特性
    if (!checkMaterialLevelFeatures(info)) {
        goto error;
    }

    // Run checks, in order.
    // The call to findProperties populates mProperties and must come before runSemanticAnalysis.
    // Return an empty package to signal a failure to build the material.
    // 按顺序运行检查。findProperties调用会填充mProperties，必须在runSemanticAnalysis之前调用

    // For finding properties and running semantic analysis, we always use the same code gen
    // permutation. This is the first permutation generated with default arguments passed to matc.
    // 对于查找属性和运行语义分析，我们总是使用相同的代码生成排列。这是使用传递给matc的默认参数生成的第一个排列
    CodeGenParams const semanticCodeGenParams = {
            .shaderModel = ShaderModel::MOBILE,
            .targetApi = TargetApi::OPENGL,
            .targetLanguage = (info.featureLevel == FeatureLevel::FEATURE_LEVEL_0) ?
                              TargetLanguage::GLSL : TargetLanguage::SPIRV,
            .featureLevel = info.featureLevel,
    };

    // 步骤5：查找所有属性
    if (!findAllProperties(semanticCodeGenParams)) {
        goto error;
    }

    // 步骤6：运行语义分析
    if (!runSemanticAnalysis(&info, semanticCodeGenParams)) {
        goto error;
    }

    // adjust variant-filter for feature level *before* we start writing into the container
    // 步骤7：在开始写入容器之前，根据功能级别调整变体过滤器
    // 在功能级别0，许多变体不受支持
    if (mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        // at feature level 0, many variants are not supported
        mVariantFilter |= uint32_t(UserVariantFilterBit::DIRECTIONAL_LIGHTING);
        mVariantFilter |= uint32_t(UserVariantFilterBit::DYNAMIC_LIGHTING);
        mVariantFilter |= uint32_t(UserVariantFilterBit::SHADOW_RECEIVER);
        mVariantFilter |= uint32_t(UserVariantFilterBit::VSM);
        mVariantFilter |= uint32_t(UserVariantFilterBit::SSR);
    }

    // Create chunk tree.
    // 步骤8：创建块树
    ChunkContainer container;
    writeCommonChunks(container, info);
    if (mMaterialDomain == MaterialDomain::SURFACE) {
        writeSurfaceChunks(container);
    }

    info.useLegacyMorphing = mUseLegacyMorphing;

    // Generate all shaders and write the shader chunks.
    // 步骤9：生成所有着色器并写入着色器块

    std::vector<Variant> variants;
    switch (mMaterialDomain) {
        case MaterialDomain::SURFACE:
            variants = determineSurfaceVariants(mVariantFilter, isLit(), mShadowMultiplier);
            break;
        case MaterialDomain::POST_PROCESS:
            variants = determinePostProcessVariants();
            break;
        case MaterialDomain::COMPUTE:
            variants = determineComputeVariants();
            break;
    }

    bool const success = generateShaders(jobSystem, variants, container, info);
    if (!success) {
        // Return an empty package to signal a failure to build the material.
        // 返回空包以表示构建材质失败
        goto error;
    }

    // Flatten all container chunks into a single package and compute its CRC32 value, storing it as
    // a separate chunk.
    // 步骤10：将所有容器块展平为单个包，计算CRC32值，并将其存储为单独的块
    constexpr size_t crc32ChunkSize = sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t);
    const size_t originalContainerSize = container.getSize();
    const size_t signedContainerSize = originalContainerSize + crc32ChunkSize;

    Package package(signedContainerSize);
    Flattener f{ package.getData() };
    size_t flattenSize = container.flatten(f);

    // 步骤10.1：生成CRC32表并计算CRC32值
    std::vector<uint32_t> crc32Table;
    hash::crc32GenerateTable(crc32Table);
    uint32_t crc = hash::crc32Update(0, f.getStartPtr(), flattenSize, crc32Table);
    // 步骤10.2：写入CRC32块（块类型、大小、CRC32值）
    f.writeUint64(static_cast<uint64_t>(MaterialCrc32));
    f.writeUint32(static_cast<uint32_t>(sizeof(crc)));
    f.writeUint32(static_cast<uint32_t>(crc));

    assert_invariant(flattenSize == originalContainerSize);
    assert_invariant(signedContainerSize == f.getBytesWritten());

    return package;
}

using namespace backend;
// 将着色器阶段标志转换为字符串（静态辅助函数）
// @param stageFlags 着色器阶段标志
// @return 返回字符串表示，如果无效返回nullptr
static const char* to_string(ShaderStageFlags const stageFlags) noexcept {
    switch (stageFlags) {
        case ShaderStageFlags::NONE:                    return "{ }";
        case ShaderStageFlags::VERTEX:                  return "{ vertex }";
        case ShaderStageFlags::FRAGMENT:                return "{ fragment }";
        case ShaderStageFlags::COMPUTE:                 return "{ compute }";
        case ShaderStageFlags::ALL_SHADER_STAGE_FLAGS:  return "{ vertex | fragment | COMPUTE }";
    }
    return nullptr;
}

// 检查材质功能级别特性（验证材质是否符合功能级别限制）
// @param info 材质信息
// @return 如果材质符合功能级别限制返回true，否则返回false
// 步骤1：定义采样器溢出日志函数
// 步骤2：计算用户采样器数量
// 步骤3：根据功能级别检查限制
bool MaterialBuilder::checkMaterialLevelFeatures(MaterialInfo const& info) const noexcept {

    // 步骤1：定义采样器溢出日志函数（用于输出采样器信息）
    auto logSamplerOverflow = [](SamplerInterfaceBlock const& sib) {
        auto const& samplers = sib.getSamplerInfoList();
        auto const* stage = to_string(sib.getStageFlags());
        for (auto const& sampler: samplers) {
            slog.e << "\"" << sampler.name.c_str() << "\" "
                   << Enums::toString(sampler.type).c_str() << " " << stage << '\n';
        }
        flush(slog.e);
    };

    // 步骤2：计算用户采样器数量（外部采样器需要额外计数）
    auto userSamplerCount = info.sib.getSize();
    for (auto const& sampler: info.sib.getSamplerInfoList()) {
        if (sampler.type == SamplerInterfaceBlock::Type::SAMPLER_EXTERNAL) {
            userSamplerCount += 1;
        }
    }

    // 步骤3：根据功能级别检查限制
    switch (info.featureLevel) {
        case FeatureLevel::FEATURE_LEVEL_0:
            // TODO: check FEATURE_LEVEL_0 features (e.g. unlit only, no texture arrays, etc...)
            // 步骤3.1：功能级别0只能使用未光照材质
            if (info.isLit) {
                slog.e << "Error: material \"" << mMaterialName.c_str()
                       << "\" has feature level " << +info.featureLevel
                       << " and is not 'unlit'." << io::endl;
                return false;
            }
            return true;
        case FeatureLevel::FEATURE_LEVEL_1:
        case FeatureLevel::FEATURE_LEVEL_2: {
            // 步骤3.2：功能级别1和2的检查
            if (mNoSamplerValidation) {
                break;
            }

            constexpr auto maxTextureCount = FEATURE_LEVEL_CAPS[1].MAX_FRAGMENT_SAMPLER_COUNT;

            // count how many samplers filament uses based on the material properties
            // note: currently SSAO is not used with unlit, but we want to keep that possibility.
            // 根据材质属性计算Filament使用的采样器数量
            // 注意：目前SSAO不与未光照材质一起使用，但我们想保留这种可能性
            // 步骤3.2.1：计算Filament使用的采样器数量
            uint32_t textureUsedByFilamentCount = 4;    // shadowMap, structure, ssao, fog texture
            if (info.isLit) {
                textureUsedByFilamentCount += 3;        // froxels, dfg, specular
            }
            if (info.reflectionMode == ReflectionMode::SCREEN_SPACE ||
                info.refractionMode == RefractionMode::SCREEN_SPACE) {
                textureUsedByFilamentCount += 1;        // ssr
            }
            if (mVariantFilter & uint32_t(UserVariantFilterBit::FOG)) {
                textureUsedByFilamentCount -= 1;        // fog texture
            }

            // 步骤3.2.2：检查用户采样器数量是否超过限制
            if (userSamplerCount > maxTextureCount - textureUsedByFilamentCount) {
                slog.e << "Error: material \"" << mMaterialName.c_str()
                       << "\" has feature level " << +info.featureLevel
                       << " and is using more than " << maxTextureCount - textureUsedByFilamentCount
                       << " samplers." << io::endl;
                logSamplerOverflow(info.sib);
                return false;
            }
            // 步骤3.2.3：检查是否使用了立方体贴图数组采样器（功能级别1/2不支持）
            auto const& samplerList = info.sib.getSamplerInfoList();
            using SamplerInfo = SamplerInterfaceBlock::SamplerInfo;
            if (std::any_of(samplerList.begin(), samplerList.end(),
                    [](const SamplerInfo& sampler) {
                        return sampler.type == SamplerType::SAMPLER_CUBEMAP_ARRAY;
                    })) {
                slog.e << "Error: material \"" << mMaterialName.c_str()
                       << "\" has feature level " << +info.featureLevel
                       << " and uses a samplerCubemapArray." << io::endl;
                logSamplerOverflow(info.sib);
                return false;
            }
            break;
        }
        case FeatureLevel::FEATURE_LEVEL_3: {
            // 步骤3.3：功能级别3的检查
            // TODO: we need constants somewhere for these values
            // TODO: 16 is artificially low for now, until we have a better idea of what we want
            // 我们需要在某个地方为这些值定义常量
            // 16目前是人为设置的低值，直到我们对想要的值有更好的想法
            if (userSamplerCount > 16) {
                slog.e << "Error: material \"" << mMaterialName.c_str()
                       << "\" has feature level " << +info.featureLevel
                       << " and is using more than 16 samplers" << io::endl;
                logSamplerOverflow(info.sib);
                return false;
            }
            break;
        }
    }
    return true;
}

// 检查是否有自定义varying变量
// @return 如果有自定义varying变量返回true，否则返回false
// 步骤1：遍历所有变量
// 步骤2：如果找到非空变量名称，返回true
bool MaterialBuilder::hasCustomVaryings() const noexcept {
    // 步骤1：遍历所有变量
    for (const auto& variable : mVariables) {
        // 步骤2：如果找到非空变量名称，返回true
        if (!variable.name.empty()) {
            return true;
        }
    }
    return false;
}

// 检查是否需要标准深度程序
// @return 如果需要标准深度程序返回true，否则返回false
// 需要标准深度程序的情况：
// 1. 有自定义顶点着色器代码
// 2. 有自定义varying变量
// 3. 使用遮罩混合模式
// 4. 启用透明阴影且使用透明或淡入淡出混合模式
bool MaterialBuilder::needsStandardDepthProgram() const noexcept {
    const bool hasEmptyVertexCode = mMaterialVertexCode.getCode().empty();
    return !hasEmptyVertexCode ||
           hasCustomVaryings() ||
           mBlendingMode == BlendingMode::MASKED ||
           (mTransparentShadow &&
            (mBlendingMode == BlendingMode::TRANSPARENT ||
             mBlendingMode == BlendingMode::FADE));
}

// 预览着色器代码（生成着色器代码用于预览或分析）
// @param stage 着色器阶段
// @param params 代码生成参数
// @param properties 属性列表
// @return 返回生成的着色器代码字符串
// 步骤1：创建着色器生成器
// 步骤2：准备材质信息
// 步骤3：根据着色器阶段生成相应的着色器代码
std::string MaterialBuilder::peek(backend::ShaderStage const stage,
        const CodeGenParams& params, const PropertyList& properties) noexcept {

    // 步骤1：创建着色器生成器
    ShaderGenerator const sg(properties, mVariables, mOutputs, mDefines, mConstants, mPushConstants,
            mMaterialFragmentCode.getCode(), mMaterialFragmentCode.getLineOffset(),
            mMaterialVertexCode.getCode(), mMaterialVertexCode.getLineOffset(),
            mMaterialDomain);

    // 步骤2：准备材质信息
    MaterialInfo info;
    prepareToBuild(info);

    // 步骤3：根据着色器阶段生成相应的着色器代码
    switch (stage) {
        case ShaderStage::VERTEX:
            return sg.createSurfaceVertexProgram(
                    params.shaderModel, params.targetApi, params.targetLanguage,
                    params.featureLevel, info, {}, mInterpolation, mVertexDomain);
        case ShaderStage::FRAGMENT:
            return sg.createSurfaceFragmentProgram(
                    params.shaderModel, params.targetApi, params.targetLanguage,
                    params.featureLevel, info, {}, mInterpolation, mVariantFilter);
        case ShaderStage::COMPUTE:
            return sg.createSurfaceComputeProgram(
                    params.shaderModel, params.targetApi, params.targetLanguage,
                    params.featureLevel, info);
    }
    return {};
}

// 从缓冲区接口块提取uniform信息（静态辅助函数）
// @param uib 缓冲区接口块
// @return 返回uniform信息列表
// 步骤1：获取字段信息列表并预留空间
// 步骤2：提取接口块名称的首字母和剩余部分
// 步骤3：为每个字段构建完全限定名称并添加到uniform列表
static Program::UniformInfo extractUniforms(BufferInterfaceBlock const& uib) noexcept {
    // 步骤1：获取字段信息列表并预留空间
    auto list = uib.getFieldInfoList();
    Program::UniformInfo uniforms = Program::UniformInfo::with_capacity(list.size());

    // 步骤2：提取接口块名称的首字母和剩余部分（用于构建完全限定名称）
    char const firstLetter = std::tolower( uib.getName().at(0) );
    std::string_view const nameAfterFirstLetter{
        uib.getName().data() + 1, uib.getName().size() - 1 };

    // 步骤3：为每个字段构建完全限定名称并添加到uniform列表
    for (auto const& item : list) {
        // construct the fully qualified name
        // 构建完全限定名称（例如：frameUniforms.fieldName）
        std::string qualified;
        qualified.reserve(uib.getName().size() + item.name.size() + 1u);
        qualified.append({ &firstLetter, 1u });
        qualified.append(nameAfterFirstLetter);
        qualified.append(".");
        qualified.append({ item.name.data(), item.name.size() });

        uniforms.push_back({
            { qualified.data(), qualified.size() },
            item.offset,
            uint8_t(item.size < 1u ? 1u : item.size),
            item.type
        });
    }
    return uniforms;
}

// 写入通用块（所有材质类型共用的块）
// @param container 块容器
// @param info 材质信息
// 步骤1：写入基本材质信息（版本、功能级别、名称等）
// 步骤2：写入功能级别0的特殊信息（uniform绑定、属性信息）
// 步骤3：写入用户参数（UBO、采样器、描述符布局等）
// 步骤4：写入常量参数和推送常量
// 步骤5：写入非计算材质的特殊信息
// 步骤6：创建并写入材质ID和源
void MaterialBuilder::writeCommonChunks(ChunkContainer& container, MaterialInfo& info) const noexcept {
    // 步骤1：写入基本材质信息（版本、功能级别、名称等）
    container.emplace<uint32_t>(MaterialVersion, MATERIAL_VERSION);
    container.emplace<uint8_t>(MaterialFeatureLevel, (uint8_t)info.featureLevel);
    container.emplace<const char*>(MaterialName, mMaterialName.c_str_safe());
    container.emplace<const char*>(MaterialCompilationParameters,
            mCompilationParameters.c_str_safe());
    container.emplace<uint32_t>(MaterialShaderModels, mShaderModels.getValue());
    container.emplace<uint8_t>(ChunkType::MaterialDomain, static_cast<uint8_t>(mMaterialDomain));

    // if that ever needed to change, this would require a material version bump
    // 如果这需要更改，将需要材质版本升级
    static_assert(sizeof(uint32_t) >= sizeof(UserVariantFilterMask));

    container.emplace<uint32_t>(MaterialVariantFilterMask, mVariantFilter);

    using namespace filament;

    // 步骤2：写入功能级别0的特殊信息（uniform绑定、属性信息）
    if (info.featureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        // FIXME: don't hardcode this
        // 步骤2.1：提取并写入uniform绑定信息（帧uniform、对象uniform、材质参数）
        FixedCapacityVector<std::tuple<uint8_t, CString, Program::UniformInfo>> list({
                { 0, "FrameUniforms",  extractUniforms(UibGenerator::getPerViewUib()) },
                { 1, "ObjectUniforms", extractUniforms(UibGenerator::getPerRenderableUib()) },
                { 2, "MaterialParams", extractUniforms(info.uib) },
        });
        // 步骤2.2：为对象uniform手动添加字段（功能级别0的特殊处理）
        auto& uniforms = std::get<2>(list[1]);
        uniforms.clear();
        uniforms.reserve(6);
        uniforms.push_back({
                "objectUniforms.data[0].worldFromModelMatrix",
                offsetof(PerRenderableUib, data[0].worldFromModelMatrix), 1,
                UniformType::MAT4 });
        uniforms.push_back({
                "objectUniforms.data[0].worldFromModelNormalMatrix",
                offsetof(PerRenderableUib, data[0].worldFromModelNormalMatrix), 1,
                UniformType::MAT3 });
        uniforms.push_back({
                "objectUniforms.data[0].morphTargetCount",
                offsetof(PerRenderableUib, data[0].morphTargetCount), 1,
                UniformType::INT });
        uniforms.push_back({
                "objectUniforms.data[0].flagsChannels",
                offsetof(PerRenderableUib, data[0].flagsChannels), 1,
                UniformType::INT });
        uniforms.push_back({
                "objectUniforms.data[0].objectId",
                offsetof(PerRenderableUib, data[0].objectId), 1,
                UniformType::INT });
        uniforms.push_back({
                "objectUniforms.data[0].userData",
                offsetof(PerRenderableUib, data[0].userData), 1,
                UniformType::FLOAT });

        container.push<MaterialBindingUniformInfoChunk>(std::move(list));

        // 步骤2.3：写入属性信息（功能级别0需要属性位置信息）
        using Container = FixedCapacityVector<std::pair<CString, uint8_t>>;
        auto attributes = Container::with_capacity(sAttributeDatabase.size());
        for (auto const& attribute: sAttributeDatabase) {
            std::string name("mesh_");
            name.append(attribute.name);
            attributes.emplace_back(CString{ name.data(), name.size() }, attribute.location);
        }
        container.push<MaterialAttributesInfoChunk>(std::move(attributes));
    }

    // User parameters (UBO)
    // 步骤3：写入用户参数（UBO、采样器、描述符布局等）
    // 步骤3.1：写入用户uniform参数（UBO）
    container.push<MaterialUniformInterfaceBlockChunk>(info.uib);

    // User texture parameters
    // 步骤3.2：写入用户纹理参数（采样器接口块）
    container.push<MaterialSamplerInterfaceBlockChunk>(info.sib);

    // Descriptor layout and descriptor name/binding mapping
    // 步骤3.3：写入描述符布局和描述符名称/绑定映射
    container.push<MaterialDescriptorBindingsChuck>(info.sib);
    container.push<MaterialDescriptorSetLayoutChunk>(info.sib);

    // User constant parameters
    // 步骤4：写入常量参数和推送常量
    // 步骤4.1：写入用户常量参数
    FixedCapacityVector<MaterialConstant> constantsEntry(mConstants.size());
    std::transform(mConstants.begin(), mConstants.end(), constantsEntry.begin(),
            [](Constant const& c) { return MaterialConstant(c.name, c.type, c.defaultValue); });
    container.push<MaterialConstantParametersChunk>(std::move(constantsEntry));

    // 步骤4.2：写入推送常量参数
    FixedCapacityVector<MaterialPushConstant> pushConstantsEntry(mPushConstants.size());
    std::transform(mPushConstants.begin(), mPushConstants.end(), pushConstantsEntry.begin(),
            [](PushConstant const& c) {
                return MaterialPushConstant(c.name.c_str(), c.type, c.stage);
            });
    container.push<MaterialPushConstantParametersChunk>(
            CString(PUSH_CONSTANT_STRUCT_VAR_NAME), std::move(pushConstantsEntry));

    // TODO: should we write the SSBO info? this would only be needed if we wanted to provide
    //       an interface to set [get?] values in the buffer. But we can do that easily
    //       with a c-struct (what about kotlin/java?). tbd.
    // 是否应该写入SSBO信息？这只有在我们要提供在缓冲区中设置[获取？]值的接口时才需要。
    // 但我们可以很容易地用c结构体做到这一点（kotlin/java呢？）待定

    // 步骤5：写入非计算材质的特殊信息
    if (mMaterialDomain != MaterialDomain::COMPUTE) {
        // User Subpass
        // 步骤5.1：写入用户子通道
        container.push<MaterialSubpassInterfaceBlockChunk>(info.subpass);

        // 步骤5.2：写入渲染状态（双面、混合、透明度等）
        container.emplace<bool>(MaterialDoubleSidedSet, mDoubleSidedCapability);
        container.emplace<bool>(MaterialDoubleSided, mDoubleSided);
        container.emplace<uint8_t>(MaterialBlendingMode,
                static_cast<uint8_t>(mBlendingMode));

        // 步骤5.3：如果使用自定义混合，写入混合函数
        if (mBlendingMode == BlendingMode::CUSTOM) {
            uint32_t const blendFunctions =
                    (uint32_t(mCustomBlendFunctions[0]) << 24) |
                    (uint32_t(mCustomBlendFunctions[1]) << 16) |
                    (uint32_t(mCustomBlendFunctions[2]) <<  8) |
                    (uint32_t(mCustomBlendFunctions[3]) <<  0);
            container.emplace< uint32_t >(MaterialBlendFunction, blendFunctions);
        }

        container.emplace<uint8_t>(MaterialTransparencyMode,
                static_cast<uint8_t>(mTransparencyMode));
        container.emplace<uint8_t>(MaterialReflectionMode,
                static_cast<uint8_t>(mReflectionMode));
        container.emplace<bool>(MaterialColorWrite, mColorWrite);
        container.emplace<bool>(MaterialDepthWriteSet, mDepthWriteSet);
        container.emplace<bool>(MaterialDepthWrite, mDepthWrite);
        container.emplace<bool>(MaterialDepthTest, mDepthTest);
        container.emplace<bool>(MaterialInstanced, mInstanced);
        container.emplace<bool>(MaterialAlphaToCoverageSet, mAlphaToCoverageSet);
        container.emplace<bool>(MaterialAlphaToCoverage, mAlphaToCoverage);
        container.emplace<uint8_t>(MaterialCullingMode,
                static_cast<uint8_t>(mCullingMode));

        // 步骤5.4：写入材质属性位掩码
        uint64_t properties = 0;
        UTILS_NOUNROLL
        for (size_t i = 0; i < MATERIAL_PROPERTIES_COUNT; i++) {
            if (mProperties[i]) {
                properties |= uint64_t(1u) << i;
            }
        }
        container.emplace<uint64_t>(MaterialProperties, properties);
        container.emplace<uint8_t>(MaterialStereoscopicType, static_cast<uint8_t>(mStereoscopicType));
    }

    // create a unique material id
    // 步骤6：创建并写入材质ID和源
    // 步骤6.1：基于顶点和片段着色器代码创建唯一材质ID
    auto const& vert = mMaterialVertexCode.getCode();
    auto const& frag = mMaterialFragmentCode.getCode();
    std::hash<std::string_view> const hasher;
    size_t const materialId = hash::combine(
            MATERIAL_VERSION,
            hash::combine(
                    hasher({ vert.data(), vert.size() }),
                    hasher({ frag.data(), frag.size() })));

    container.emplace<uint64_t>(MaterialCacheId, materialId);
    // 步骤6.2：如果提供了材质源，写入压缩的材质源
    if (!mMaterialSource.empty()) {
        container.push<CompressedStringChunk>(
                MaterialSource, mMaterialSource,
                CompressedStringChunk::CompressionLevel::MAX);
    }
}

// 写入表面材质专用块
// @param container 块容器
// 步骤1：写入遮罩阈值（如果使用遮罩混合）
// 步骤2：写入着色模型和阴影倍增器
// 步骤3：写入折射相关参数
// 步骤4：写入其他表面材质参数（属性、镜面反射抗锯齿等）
void MaterialBuilder::writeSurfaceChunks(ChunkContainer& container) const noexcept {
    if (mBlendingMode == BlendingMode::MASKED) {
        container.emplace<float>(MaterialMaskThreshold, mMaskThreshold);
    }

    container.emplace<uint8_t>(MaterialShading, static_cast<uint8_t>(mShading));

    if (mShading == Shading::UNLIT) {
        container.emplace<bool>(MaterialShadowMultiplier, mShadowMultiplier);
    }

    container.emplace<uint8_t>(MaterialRefraction, static_cast<uint8_t>(mRefractionMode));
    container.emplace<uint8_t>(MaterialRefractionType,
            static_cast<uint8_t>(mRefractionType));
    container.emplace<bool>(MaterialClearCoatIorChange, mClearCoatIorChange);
    container.emplace<uint32_t>(MaterialRequiredAttributes,
            mRequiredAttributes.getValue());
    container.emplace<bool>(MaterialSpecularAntiAliasing, mSpecularAntiAliasing);
    container.emplace<float>(MaterialSpecularAntiAliasingVariance,
            mSpecularAntiAliasingVariance);
    container.emplace<float>(MaterialSpecularAntiAliasingThreshold,
            mSpecularAntiAliasingThreshold);
    container.emplace<uint8_t>(MaterialVertexDomain, static_cast<uint8_t>(mVertexDomain));
    container.emplace<uint8_t>(MaterialInterpolation,
            static_cast<uint8_t>(mInterpolation));
}

MaterialBuilder& MaterialBuilder::noSamplerValidation(bool const enabled) noexcept {
    mNoSamplerValidation = enabled;
    return *this;
}

MaterialBuilder& MaterialBuilder::includeEssl1(bool const enabled) noexcept {
    mIncludeEssl1 = enabled;
    return *this;
}

} // namespace filamat
