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

#include "GLSLPostProcessor.h"

#include <GlslangToSpv.h>
#include <spirv-tools/libspirv.hpp>

#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>

#include "private/filament/DescriptorSets.h"
#include "sca/builtinResource.h"
#include "sca/GLSLTools.h"

#include "shaders/CodeGenerator.h"
#include "shaders/MaterialInfo.h"
#include "shaders/SibGenerator.h"

#include "MetalArgumentBuffer.h"
#include "SpirvFixup.h"

#include <filament/MaterialEnums.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Log.h>
#include <utils/ostream.h>

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stddef.h>
#include <stdint.h>

#ifdef FILAMENT_SUPPORTS_WEBGPU
#include <tint/tint.h>
#endif

using namespace glslang;
using namespace spirv_cross;
using namespace spvtools;
using namespace filament;
using namespace filament::backend;

namespace filamat {

using namespace utils;

namespace msl {  // this is only used for MSL

using BindingIndexMap = std::unordered_map<std::string, uint16_t>;

#ifndef DEBUG_LOG_DESCRIPTOR_SETS
#define DEBUG_LOG_DESCRIPTOR_SETS 0
#endif

// 将着色器阶段标志转换为字符串（用于调试输出）
// @param flags 着色器阶段标志
// @return 阶段名称字符串
static const char* toString(ShaderStageFlags const flags) {
    // 步骤1：收集所有启用的着色器阶段名称
    std::vector<const char*> stages;
    if (any(flags & ShaderStageFlags::VERTEX)) {
        stages.push_back("VERTEX");
    }
    if (any(flags & ShaderStageFlags::FRAGMENT)) {
        stages.push_back("FRAGMENT");
    }
    if (any(flags & ShaderStageFlags::COMPUTE)) {
        stages.push_back("COMPUTE");
    }
    // 步骤2：如果没有阶段，返回"NONE"
    if (stages.empty()) {
        return "NONE";
    }
    // 步骤3：将阶段名称连接为字符串（用" | "分隔）
    static char buffer[64];
    buffer[0] = '\0';
    for (size_t i = 0; i < stages.size(); i++) {
        if (i > 0) {
            strcat(buffer, " | ");
        }
        strcat(buffer, stages[i]);
    }
    return buffer;
}

// 将描述符标志转换为可读字符串（用于调试输出）
// @param flags 描述符标志
// @return 标志名称字符串
static const char* prettyDescriptorFlags(DescriptorFlags const flags) {
    if (flags == DescriptorFlags::DYNAMIC_OFFSET) {
        return "DYNAMIC_OFFSET";
    }
    return "NONE";
}

// 将采样器类型转换为可读字符串（用于调试输出）
// @param type 采样器类型
// @return 类型名称字符串
static const char* prettyPrintSamplerType(SamplerType const type) {
    switch (type) {
        case SamplerType::SAMPLER_2D:
            return "SAMPLER_2D";
        case SamplerType::SAMPLER_2D_ARRAY:
            return "SAMPLER_2D_ARRAY";
        case SamplerType::SAMPLER_CUBEMAP:
            return "SAMPLER_CUBEMAP";
        case SamplerType::SAMPLER_EXTERNAL:
            return "SAMPLER_EXTERNAL";
        case SamplerType::SAMPLER_3D:
            return "SAMPLER_3D";
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return "SAMPLER_CUBEMAP_ARRAY";
    }
}

// 获取每个材质的描述符集布局（根据采样器接口块生成）
// @param sib 采样器接口块
// @return 描述符集布局
DescriptorSetLayout getPerMaterialDescriptorSet(SamplerInterfaceBlock const& sib) noexcept {
    // 步骤1：获取采样器信息列表
    auto const& samplers = sib.getSamplerInfoList();

    // 步骤2：创建描述符集布局并预留空间（1个UBO + 采样器数量）
    DescriptorSetLayout layout;
    layout.bindings.reserve(1 + samplers.size());

    // 步骤3：添加材质参数UBO绑定（MATERIAL_PARAMS，支持动态偏移）
    layout.bindings.push_back(DescriptorSetLayoutBinding{ DescriptorType::UNIFORM_BUFFER,
        ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,
        +PerMaterialBindingPoints::MATERIAL_PARAMS, DescriptorFlags::DYNAMIC_OFFSET, 0 });

    // 步骤4：遍历采样器，为每个采样器添加绑定
    for (auto const& sampler: samplers) {
        // 步骤4.1：创建采样器绑定（默认为SAMPLER_EXTERNAL）
        DescriptorSetLayoutBinding layoutBinding{
            DescriptorType::SAMPLER_EXTERNAL,
            sampler.stages, sampler.binding,
            DescriptorFlags::NONE,
            0
        };
        // 步骤4.2：如果不是外部采样器，根据类型和格式确定描述符类型
        if (sampler.type != SamplerInterfaceBlock::Type::SAMPLER_EXTERNAL) {
            layoutBinding.type = descriptor_sets::getDescriptorType(sampler.type, sampler.format);
        }
        // 步骤4.3：添加绑定到布局
        layout.bindings.push_back(layoutBinding);
    }

    return layout;
}

// 收集指定绑定点的描述符集信息
// @param set 描述符集绑定点
// @param config 后处理器配置
// @param descriptors 输出参数，描述符信息列表
static void collectDescriptorsForSet(DescriptorSetBindingPoints set,
        const GLSLPostProcessor::Config& config, DescriptorSetInfo& descriptors) {
    const MaterialInfo& material = *config.materialInfo;

    // get the descriptor set layout for the given pinding point
    // 步骤1：获取给定绑定点的描述符集布局
    DescriptorSetLayout const descriptorSetLayout = [&] {
        switch (set) {
            case DescriptorSetBindingPoints::PER_VIEW: {
                // 步骤1.1：计算PER_VIEW描述符集布局所需的参数
                bool const isLit = material.isLit || material.hasShadowMultiplier;
                bool const isSSR = material.reflectionMode == ReflectionMode::SCREEN_SPACE ||
                        material.refractionMode == RefractionMode::SCREEN_SPACE;
                bool const hasFog = !(config.variantFilter & UserVariantFilterMask(UserVariantFilterBit::FOG));
                return descriptor_sets::getPerViewDescriptorSetLayoutWithVariant(
                        config.variant, config.domain, isLit, isSSR, hasFog);
            }
            case DescriptorSetBindingPoints::PER_RENDERABLE:
                // 步骤1.2：获取PER_RENDERABLE描述符集布局
                return descriptor_sets::getPerRenderableLayout();
            case DescriptorSetBindingPoints::PER_MATERIAL:
                // 步骤1.3：获取PER_MATERIAL描述符集布局
                return getPerMaterialDescriptorSet(config.materialInfo->sib);
            default:
                return DescriptorSetLayout {};
        }
    }();

    // get the sampler list for this binding point
    // 步骤2：获取此绑定点的采样器列表
    auto samplerList = [&] {
        switch (set) {
            case DescriptorSetBindingPoints::PER_VIEW:
                return SibGenerator::getPerViewSib(config.variant).getSamplerInfoList();
            case DescriptorSetBindingPoints::PER_RENDERABLE:
                return SibGenerator::getPerRenderableSib(config.variant).getSamplerInfoList();
            case DescriptorSetBindingPoints::PER_MATERIAL:
                return config.materialInfo->sib.getSamplerInfoList();
            default:
                return SamplerInterfaceBlock::SamplerInfoList {};
        }
    }();

    // filter the list with the descriptor set layout
    // 步骤3：使用描述符集布局过滤采样器列表
    auto const descriptorSetSamplerList =
            SamplerInterfaceBlock::filterSamplerList(std::move(samplerList), descriptorSetLayout);

    // helper to get the name of a descriptor for this set, given a binding.
    // 步骤4：定义辅助函数，根据绑定获取描述符名称
    auto getDescriptorName = [set, &descriptorSetSamplerList](descriptor_binding_t binding) {
        if (set == DescriptorSetBindingPoints::PER_MATERIAL) {
            // 对于PER_MATERIAL，从采样器列表中查找uniform名称
            auto pos = std::find_if(descriptorSetSamplerList.begin(), descriptorSetSamplerList.end(),
                    [&](const auto& entry) { return entry.binding == binding; });
            if (pos == descriptorSetSamplerList.end()) {
                return descriptor_sets::getDescriptorName(set, binding);
            }
            return pos->uniformName;
        }
        // 对于其他集合，使用标准描述符名称
        return descriptor_sets::getDescriptorName(set, binding);
    };

    // 步骤5：遍历描述符集布局绑定，创建描述符信息
    for (auto const& layoutBinding : descriptorSetLayout.bindings) {
        descriptor_binding_t binding = layoutBinding.binding;
        auto name = getDescriptorName(binding);
        // 步骤5.1：如果是采样器类型，从采样器列表中查找并添加采样器信息
        if (DescriptorSetLayoutBinding::isSampler(layoutBinding.type)) {
            auto const pos = std::find_if(descriptorSetSamplerList.begin(), descriptorSetSamplerList.end(),
                    [&](const auto& entry) { return entry.binding == binding; });
            assert_invariant(pos != descriptorSetSamplerList.end());
            descriptors.emplace_back(name, layoutBinding, *pos);
        } else {
            // 步骤5.2：如果不是采样器类型，添加描述符信息（无采样器信息）
            descriptors.emplace_back(name, layoutBinding, std::nullopt);
        }
    }

    // 步骤6：按绑定索引排序描述符
    std::sort(descriptors.begin(), descriptors.end(), [](const auto& a, const auto& b) {
        return std::get<1>(a).binding < std::get<1>(b).binding;
    });
}

// 美化打印描述符集信息向量（用于调试输出）
// @param sets 描述符集数组
static void prettyPrintDescriptorSetInfoVector(DescriptorSets const& sets) noexcept {
    // 步骤1：定义lambda函数，获取描述符集名称
    auto getName = [](uint8_t const set) {
        switch (set) {
            case +DescriptorSetBindingPoints::PER_VIEW:
                return "perViewDescriptorSetLayout";
            case +DescriptorSetBindingPoints::PER_RENDERABLE:
                return "perRenderableDescriptorSetLayout";
            case +DescriptorSetBindingPoints::PER_MATERIAL:
                return "perMaterialDescriptorSetLayout";
            default:
                return "unknown";
        }
    };
    // 步骤2：遍历所有描述符集
    for (size_t setIndex = 0; setIndex < MAX_DESCRIPTOR_SET_COUNT; setIndex++) {
        auto const& descriptors = sets[setIndex];
        // 步骤2.1：打印描述符集标题
        printf("[DS] info (%s) = [\n", getName(setIndex));
        // 步骤2.2：遍历描述符集中的每个描述符
        for (auto const& descriptor : descriptors) {
            auto const& [name, info, sampler] = descriptor;
            // 步骤2.2.1：如果是采样器类型，打印包含采样器信息的详细信息
            if (DescriptorSetLayoutBinding::isSampler(info.type)) {
                assert_invariant(sampler.has_value());
                printf("    {name = %s, binding = %d, type = %.*s, count = %d, stage = %s, flags = "
                       "%s, samplerType = %s}",
                        name.c_str_safe(), info.binding,
                        int(to_string(info.type).size()),
                        to_string(info.type).data(),
                        info.count,
                        toString(info.stageFlags), prettyDescriptorFlags(info.flags),
                        prettyPrintSamplerType(sampler->type));
            } else {
                // 步骤2.2.2：如果不是采样器类型，打印不包含采样器信息的详细信息
                printf("    {name = %s, binding = %d, type = %.*s, count = %d, stage = %s, flags = "
                       "%s}",
                        name.c_str_safe(), info.binding,
                        int(to_string(info.type).size()),
                        to_string(info.type).data(),
                        info.count,
                        toString(info.stageFlags), prettyDescriptorFlags(info.flags));
            }
            printf(",\n");
        }
        printf("]\n");
    }
}

// 收集所有描述符集信息（PER_VIEW、PER_RENDERABLE、PER_MATERIAL）
// @param config 后处理器配置
// @param sets 输出参数，描述符集数组
static void collectDescriptorSets(const GLSLPostProcessor::Config& config, DescriptorSets& sets) {
    // 步骤1：收集PER_VIEW描述符集信息
    auto perViewDescriptors = DescriptorSetInfo::with_capacity(MAX_DESCRIPTOR_COUNT);
    collectDescriptorsForSet(DescriptorSetBindingPoints::PER_VIEW, config, perViewDescriptors);
    sets[+DescriptorSetBindingPoints::PER_VIEW] = std::move(perViewDescriptors);

    // 步骤2：收集PER_RENDERABLE描述符集信息
    auto perRenderableDescriptors = DescriptorSetInfo::with_capacity(MAX_DESCRIPTOR_COUNT);
    collectDescriptorsForSet(
            DescriptorSetBindingPoints::PER_RENDERABLE, config, perRenderableDescriptors);
    sets[+DescriptorSetBindingPoints::PER_RENDERABLE] = std::move(perRenderableDescriptors);

    // 步骤3：收集PER_MATERIAL描述符集信息
    auto perMaterialDescriptors = DescriptorSetInfo::with_capacity(MAX_DESCRIPTOR_COUNT);
    collectDescriptorsForSet(
            DescriptorSetBindingPoints::PER_MATERIAL, config, perMaterialDescriptors);
    sets[+DescriptorSetBindingPoints::PER_MATERIAL] = std::move(perMaterialDescriptors);
}

} // namespace msl

// 构造函数：初始化GLSL后处理器
// @param optimization 优化级别
// @param workarounds 工作区标志
// @param flags 处理器标志（PRINT_SHADERS、GENERATE_DEBUG_INFO等）
GLSLPostProcessor::GLSLPostProcessor(
        MaterialBuilder::Optimization optimization,
        MaterialBuilder::Workarounds workarounds,
        uint32_t flags)
    : mOptimization(optimization), mWorkarounds(workarounds),
      mPrintShaders(flags & PRINT_SHADERS),
      mGenerateDebugInfo(flags & GENERATE_DEBUG_INFO) {
}

GLSLPostProcessor::~GLSLPostProcessor() = default;

// 过滤SPIR-V优化器消息（在发布版本中只记录错误）
// @param level 消息级别
// @return 如果应该记录消息返回true，否则返回false
static bool filterSpvOptimizerMessage(spv_message_level_t level) {
#ifdef NDEBUG
    // In release builds, only log errors.
    if (level == SPV_MSG_WARNING ||
        level == SPV_MSG_INFO ||
        level == SPV_MSG_DEBUG) {
        return false;
    }
#endif
    return true;
}

// 将SPIR-V优化器消息转换为字符串
// @param level 消息级别
// @param source 源文件名（可选）
// @param position 消息位置（行、列、索引）
// @param message 消息内容
// @return 格式化的消息字符串
static std::string stringifySpvOptimizerMessage(spv_message_level_t level, const char* source,
        const spv_position_t& position, const char* message) {
    const char* levelString = nullptr;
    switch (level) {
        case SPV_MSG_FATAL:
            levelString = "FATAL";
            break;
        case SPV_MSG_INTERNAL_ERROR:
            levelString = "INTERNAL ERROR";
            break;
        case SPV_MSG_ERROR:
            levelString = "ERROR";
            break;
        case SPV_MSG_WARNING:
            levelString = "WARNING";
            break;
        case SPV_MSG_INFO:
            levelString = "INFO";
            break;
        case SPV_MSG_DEBUG:
            levelString = "DEBUG";
            break;
    }

    std::ostringstream oss;
    oss << levelString << ": ";
    if (source) oss << source << ":";
    oss << position.line << ":" << position.column << ":";
    oss << position.index << ": ";
    if (message) oss << message;

    return oss.str();
}

// 将SPIR-V转换为MSL（公开方法，backend_test也可以使用）
// @param spirv SPIR-V二进制数据
// @param outMsl 输出参数，MSL代码
// @param stage 着色器阶段
// @param shaderModel 着色器模型
// @param useFramebufferFetch 是否使用帧缓冲区获取
// @param descriptorSets 描述符集信息
// @param minifier 着色器压缩器（可选，用于调试信息）
void GLSLPostProcessor::spirvToMsl(const SpirvBlob* spirv, std::string* outMsl,
        ShaderStage stage, ShaderModel shaderModel,
        bool useFramebufferFetch, const DescriptorSets& descriptorSets,
        const ShaderMinifier* minifier) {
    using namespace msl;

    // 步骤1：创建MSL编译器并设置通用选项
    CompilerMSL mslCompiler(*spirv);
    CompilerGLSL::Options const options;
    mslCompiler.set_common_options(options);

    // 步骤2：根据着色器模型确定平台（iOS或macOS）
    const CompilerMSL::Options::Platform platform =
        shaderModel == ShaderModel::MOBILE ?
            CompilerMSL::Options::Platform::iOS : CompilerMSL::Options::Platform::macOS;

    // 步骤3：配置MSL选项
    CompilerMSL::Options mslOptions = {};
    mslOptions.platform = platform,
    // 步骤4：设置MSL版本（移动设备使用2.0，桌面使用2.2）
    mslOptions.msl_version = shaderModel == ShaderModel::MOBILE ?
        CompilerMSL::Options::make_msl_version(2, 0) : CompilerMSL::Options::make_msl_version(2, 2);

    // 步骤5：如果使用帧缓冲区获取，启用子通道并升级MSL版本
    if (useFramebufferFetch) {
        mslOptions.use_framebuffer_fetch_subpasses = true;
        // On macOS, framebuffer fetch is only available starting with MSL 2.3. Filament will only
        // use framebuffer fetch materials on devices that support it.
        // 在macOS上，帧缓冲区获取仅在MSL 2.3开始可用。Filament只会在支持它的设备上使用帧缓冲区获取材质
        if (shaderModel == ShaderModel::DESKTOP) {
            mslOptions.msl_version = CompilerMSL::Options::make_msl_version(2, 3);
        }
    }

    // 步骤6：启用参数缓冲区和iOS基础顶点实例支持
    mslOptions.argument_buffers = true;
    mslOptions.ios_support_base_vertex_instance = true;
    mslOptions.dynamic_offsets_buffer_index = 25;

    // 步骤7：应用MSL选项
    mslCompiler.set_msl_options(mslOptions);



    // 步骤8：获取执行模型
    auto executionModel = mslCompiler.get_execution_model();

    // Map each descriptor set (argument buffer) to a [[buffer(n)]] binding.
    // For example, mapDescriptorSet(0, 21) says "map descriptor set 0 to [[buffer(21)]]"
    // 步骤9：定义lambda函数，将每个描述符集（参数缓冲区）映射到[[buffer(n)]]绑定
    // 例如，mapDescriptorSet(0, 21)表示"将描述符集0映射到[[buffer(21)]]"
    auto mapDescriptorSet = [&mslCompiler](uint32_t set, uint32_t buffer) {
        MSLResourceBinding argBufferBinding;
        argBufferBinding.basetype = SPIRType::BaseType::Float;
        argBufferBinding.stage = mslCompiler.get_execution_model();
        argBufferBinding.desc_set = set;
        argBufferBinding.binding = kArgumentBufferBinding;
        argBufferBinding.count = 1;
        argBufferBinding.msl_buffer = buffer;
        mslCompiler.add_msl_resource_binding(argBufferBinding);
    };
    // 步骤10：为所有描述符集设置绑定
    for (int i = 0; i < MAX_DESCRIPTOR_SET_COUNT; i++) {
        mapDescriptorSet(i, CodeGenerator::METAL_DESCRIPTOR_SET_BINDING_START + i);
    }

    // 步骤11：获取着色器资源
    auto resources = mslCompiler.get_shader_resources();

    // We're using argument buffers for descriptor sets, however, we cannot rely on spirv-cross to
    // generate the argument buffer definitions.
    //
    // Consider a shader with 3 textures:
    // layout (set = 0, binding = 0) uniform sampler2D texture1;
    // layout (set = 0, binding = 1) uniform sampler2D texture2;
    // layout (set = 0, binding = 2) uniform sampler2D texture3;
    //
    // If only texture1 and texture2 are used in the material, then texture3 will be optimized away.
    // This results in an argument buffer like the following:
    // struct spvDescriptorSetBuffer0 {
    //     texture2d<float> texture1 [[id(0)]];
    //     sampler texture1Smplr [[id(1)]];
    //     texture2d<float> texture2 [[id(2)]];
    //     sampler texture2Smplr [[id(3)]];
    // };
    // Note that this happens even if "pad_argument_buffer_resources" and
    // "force_active_argument_buffer_resources" are true.
    //
    // This would be fine, except older Apple devices don't like it when the argument buffer in the
    // shader doesn't precisely match the one generated at runtime.
    //
    // So, we use the MetalArgumentBuffer class to replace spirv-cross' argument buffer definitions
    // with our own that contain all the descriptors, even those optimized away.
    // 我们使用参数缓冲区作为描述符集，但不能依赖spirv-cross生成参数缓冲区定义
    // 考虑一个包含3个纹理的着色器，如果只有texture1和texture2被使用，texture3会被优化掉
    // 这会导致参数缓冲区不完整，而较旧的Apple设备不喜欢着色器中的参数缓冲区与运行时生成的不完全匹配
    // 因此，我们使用MetalArgumentBuffer类替换spirv-cross的参数缓冲区定义，包含所有描述符（即使被优化掉）
    // 步骤12：构建自定义参数缓冲区（包含所有描述符，即使被优化掉）
    std::vector<MetalArgumentBuffer*> argumentBuffers;
    size_t dynamicOffsetsBufferIndex = 0;
    for (size_t setIndex = 0; setIndex < MAX_DESCRIPTOR_SET_COUNT; setIndex++) {
        // 步骤12.1：获取当前描述符集的描述符列表
        auto const& descriptors = descriptorSets[setIndex];
        // 步骤12.2：创建参数缓冲区构建器并设置名称
        auto argBufferBuilder = MetalArgumentBuffer::Builder().name(
                "spvDescriptorSetBuffer" + std::to_string(int(setIndex)));
        // 步骤12.3：遍历描述符并添加到参数缓冲区
        for (auto const& descriptor : descriptors) {
            auto const& [name, info, sampler] = descriptor;
            // 步骤12.4：检查描述符是否适用于当前着色器阶段
            if (!hasShaderType(info.stageFlags, stage)) {
                if (any(info.flags & DescriptorFlags::DYNAMIC_OFFSET)) {
                    // We still need to increment the dynamic offset index
                    // 我们仍然需要递增动态偏移索引
                    dynamicOffsetsBufferIndex++;
                }
                continue;
            }
            // 步骤12.5：根据描述符类型添加到参数缓冲区
            switch (info.type) {
                case DescriptorType::INPUT_ATTACHMENT:
                    // TODO: Handle INPUT_ATTACHMENT case
                    // TODO: 处理INPUT_ATTACHMENT情况
                    break;
                case DescriptorType::UNIFORM_BUFFER:
                case DescriptorType::SHADER_STORAGE_BUFFER: {
                    // 步骤12.5.1：处理uniform缓冲区或着色器存储缓冲区
                    // 步骤12.5.1.1：将名称首字母转为小写（用于类型定义）
                    std::string lowercasedName = name.c_str();
                    assert_invariant(!lowercasedName.empty());
                    lowercasedName[0] = std::tolower(lowercasedName[0]);
                    // 步骤12.5.1.2：添加缓冲区参数到参数缓冲区（绑定索引 = 原索引 * 2 + 0）
                    argBufferBuilder
                            .buffer(info.binding * 2 + 0, name.c_str(), lowercasedName);
                    // 步骤12.5.1.3：如果有动态偏移标志，添加到MSL编译器的动态缓冲区列表
                    if (any(info.flags & DescriptorFlags::DYNAMIC_OFFSET)) {
                        // Note: this requires that the sets and descriptors are sorted (at least
                        // the uniforms).
                        // 注意：这要求集合和描述符已排序（至少uniform）
                        mslCompiler.add_dynamic_buffer(
                                setIndex, info.binding * 2 + 0, dynamicOffsetsBufferIndex++);
                    }
                    break;
                }

                // 步骤12.5.2：处理所有采样器类型（2D、2D数组、立方体、立方体数组、3D、多重采样、外部采样器）
                case DescriptorType::SAMPLER_2D_FLOAT:
                case DescriptorType::SAMPLER_2D_INT:
                case DescriptorType::SAMPLER_2D_UINT:
                case DescriptorType::SAMPLER_2D_DEPTH:
                case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_2D_ARRAY_INT:
                case DescriptorType::SAMPLER_2D_ARRAY_UINT:
                case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:
                case DescriptorType::SAMPLER_CUBE_FLOAT:
                case DescriptorType::SAMPLER_CUBE_INT:
                case DescriptorType::SAMPLER_CUBE_UINT:
                case DescriptorType::SAMPLER_CUBE_DEPTH:
                case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_INT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:
                case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:
                case DescriptorType::SAMPLER_3D_FLOAT:
                case DescriptorType::SAMPLER_3D_INT:
                case DescriptorType::SAMPLER_3D_UINT:
                case DescriptorType::SAMPLER_2D_MS_FLOAT:
                case DescriptorType::SAMPLER_2D_MS_INT:
                case DescriptorType::SAMPLER_2D_MS_UINT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
                case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:
                case DescriptorType::SAMPLER_EXTERNAL: {
                    // 步骤12.5.2.1：确保采样器信息存在
                    assert_invariant(sampler.has_value());
                    // 步骤12.5.2.2：生成采样器名称（原名称 + "Smplr"）
                    const std::string samplerName = std::string(name.c_str()) + "Smplr";
                    // 步骤12.5.2.3：添加纹理参数（绑定索引 = 原索引 * 2 + 0）和采样器参数（绑定索引 = 原索引 * 2 + 1）
                    // 在Metal中，纹理和采样器是分离的，需要分别绑定
                    argBufferBuilder
                            .texture(info.binding * 2 + 0, name.c_str(), sampler->type,
                                    sampler->format, sampler->multisample)
                            .sampler(info.binding * 2 + 1, samplerName);
                    break;
                }
            }
        }
        // 步骤12.6：构建参数缓冲区并添加到列表
        argumentBuffers.push_back(argBufferBuilder.build());
    }

    // Bind push constants to [buffer(26)]
    // 步骤13：将推送常量绑定到[buffer(26)]
    MSLResourceBinding pushConstantBinding;
    // the baseType doesn't matter, but can't be UNKNOWN
    // baseType不重要，但不能是UNKNOWN
    pushConstantBinding.basetype = SPIRType::BaseType::Struct;
    pushConstantBinding.stage = executionModel;
    pushConstantBinding.desc_set = kPushConstDescSet;
    pushConstantBinding.binding = kPushConstBinding;
    pushConstantBinding.count = 1;
    pushConstantBinding.msl_buffer = CodeGenerator::METAL_PUSH_CONSTANT_BUFFER_INDEX;
    mslCompiler.add_msl_resource_binding(pushConstantBinding);

    // 步骤14：编译SPIR-V为MSL
    *outMsl = mslCompiler.compile();
    // 步骤15：如果提供了压缩器，移除空白字符
    if (minifier) {
        *outMsl = minifier->removeWhitespace(*outMsl);
    }

    // Replace spirv-cross' generated argument buffers with our own.
    // 步骤16：用我们自己的参数缓冲区替换spirv-cross生成的参数缓冲区
    for (auto* argBuffer : argumentBuffers) {
        auto argBufferMsl = argBuffer->getMsl();
        MetalArgumentBuffer::replaceInShader(*outMsl, argBuffer->getName(), argBufferMsl);
        MetalArgumentBuffer::destroy(&argBuffer);
    }
}

// 为WGSL重新绑定图像采样器（修改SPIR-V以适配WGSL）
// @param spirv SPIR-V二进制数据（将被修改）
void GLSLPostProcessor::rebindImageSamplerForWGSL(std::vector<uint32_t> &spirv) {
    // 步骤1：获取SPIR-V数据
    constexpr size_t HEADER_SIZE = 5;
    size_t const dataSize = spirv.size();
    uint32_t *data = spirv.data();

    // 步骤2：创建采样器目标ID集合
    std::set<uint32_t> samplerTargetIDs;

    // 步骤3：定义lambda函数，遍历SPIR-V指令并执行回调
    auto pass = [&](uint32_t targetOp, std::function<void(uint32_t)> f) {
        for (uint32_t cursor = HEADER_SIZE, cursorEnd = dataSize; cursor < cursorEnd;) {
            uint32_t const firstWord = data[cursor];
            uint32_t const wordCount = firstWord >> 16;  // 指令字计数
            uint32_t const op = firstWord & 0x0000FFFF;  // 操作码
            if (targetOp == op) {
                f(cursor + 1);  // 传递操作数位置
            }
            cursor += wordCount;
        }
    };

    //Parse through debug name info to determine which bindings are samplers and which are not.
    // This is possible because the sampler splitting pass outputs sampler and texture pairs of the form:
    // `uniform sampler2D var_x` => `uniform sampler var_sampler` and `uniform texture2D var_texture`;
    // TODO: This works, but may limit what optimizations can be done and has the potential to collide with user
    // variable names. Ideally, trace usage to determine binding type.
    // 步骤4：解析调试名称信息以确定哪些绑定是采样器
    // 这是可能的，因为采样器分离通道输出采样器和纹理对，形式为：
    // `uniform sampler2D var_x` => `uniform sampler var_sampler` 和 `uniform texture2D var_texture`
    // TODO: 这可以工作，但可能限制可以做的优化，并可能与用户变量名冲突。理想情况下，跟踪使用以确定绑定类型
    pass(spv::Op::OpName, [&](uint32_t pos) {
        auto target = data[pos];
        char *name = (char *) &data[pos + 1];
        std::string_view view(name);
        // 如果名称包含"_sampler"，则将其添加到采样器目标ID集合
        if (view.find("_sampler") != std::string_view::npos) {
            samplerTargetIDs.insert(target);
        }
    });

    // Write out the offset bindings
    // 步骤5：写入偏移绑定（重新映射绑定索引）
    pass(spv::Op::OpDecorate, [&](uint32_t pos) {
        uint32_t const type = data[pos + 1];
        if (type == spv::Decoration::DecorationBinding) {
            uint32_t const targetVar = data[pos];
            // 如果是采样器，绑定索引 = 原索引 * 2 + 1；否则绑定索引 = 原索引 * 2
            if (samplerTargetIDs.find(targetVar) != samplerTargetIDs.end()) {
                data[pos + 2] = data[pos + 2] * 2 + 1;
            } else {
                data[pos + 2] = data[pos + 2] * 2;
            }
        }
    });
}

// 将SPIR-V转换为WGSL
// @param spirv SPIR-V二进制数据（将被修改）
// @param outWsl 输出参数，WGSL代码
// @return 如果转换成功返回true，否则返回false
bool GLSLPostProcessor::spirvToWgsl(SpirvBlob *spirv, std::string *outWsl) {
#if FILAMENT_SUPPORTS_WEBGPU
    //We need to run some opt-passes at all times to transpile to WGSL
    // 步骤1：我们需要始终运行一些优化通道以转换为WGSL
    auto optimizer = createEmptyOptimizer();
    // 步骤2：注册分离组合图像采样器通道
    optimizer->RegisterPass(CreateSplitCombinedImageSamplerPass());
    // 步骤3：优化SPIR-V（分离图像和采样器）
    optimizeSpirv(optimizer, *spirv);

    //After splitting the image samplers, we need to remap the bindings to separate them.
    // 步骤4：分离图像采样器后，需要重新映射绑定以分离它们
    rebindImageSamplerForWGSL(*spirv);

    //Allow non-uniform derivatives due to our nested shaders. See https://github.com/gpuweb/gpuweb/issues/3479
    // 步骤5：允许非均匀导数（由于我们的嵌套着色器）
    // 参见 https://github.com/gpuweb/gpuweb/issues/3479
    const tint::spirv::reader::Options readerOpts{true};

    // 步骤6：使用Tint读取SPIR-V并转换为Tint程序
    tint::Program tintRead = tint::spirv::reader::Read(*spirv, readerOpts);

    // 步骤7：检查Tint读取是否有错误
    if (tintRead.Diagnostics().ContainsErrors()) {
        //We know errors can potentially crop up, and want the ability to ignore them if needed for sample bringup
        // 我们知道错误可能会出现，并希望在示例启动时能够忽略它们
#ifndef FILAMENT_WEBGPU_IGNORE_TNT_READ_ERRORS
        // 步骤7.1：输出Tint读取错误信息
        slog.e << "Tint Reader Error: " << tintRead.Diagnostics().Str() << io::endl;
        // 步骤7.2：创建SPIR-V上下文（用于将二进制转换为文本）
        spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_1_SPIRV_1_4);
        spv_text text = nullptr;
        spv_diagnostic diagnostic = nullptr;
        // 步骤7.3：将SPIR-V二进制转换为文本（用于调试输出）
        spv_result_t result = spvBinaryToText(
            context,
            spirv->data(),
            spirv->size(),
            SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES | SPV_BINARY_TO_TEXT_OPTION_COLOR,
            &text,
            &diagnostic);
        // 步骤7.4：输出SPIR-V文本转储
        slog.e << "Beginning SpirV-output dump with ret " << result << "\n\n" << text->str << "\n\nEndSPIRV\n" <<
                io::endl;
        // 步骤7.5：清理资源
        spvTextDestroy(text);
        // 步骤7.6：再次输出Tint读取错误信息
        slog.e << "Tint Reader Error: " << tintRead.Diagnostics().Str() << io::endl;
        return false;
#endif
    }

    // 步骤8：使用Tint生成WGSL代码
    tint::Result<tint::wgsl::writer::Output> wgslOut = tint::wgsl::writer::Generate(tintRead);
    /// An instance of SuccessType that can be used to check a tint Result.
    // SuccessType的实例，可用于检查tint Result
    tint::SuccessType tintSuccess;

    // 步骤9：检查WGSL生成是否成功
    if (wgslOut != tintSuccess) {
        slog.e << "Tint writer error: " << wgslOut.Failure().reason << io::endl;
        return false;
    }
    // Tint adds annotations that Dawn complains about when consuming. remove for now
    // https://dawn.googlesource.com/dawn/+/efb17b02543fb52c0b2e21d6082c0c9fbc2168a9%5E%21/
    // Tint添加的注释在Dawn消费时会抱怨。现在移除它们
    // 步骤10：移除Tint添加的注释（Dawn不支持）
    // 步骤10.1：定义要移除的注释字符串
    char const* annotationStr = "@stride(16) @internal(disable_validation__ignore_stride)";
    // 步骤10.2：查找并移除所有出现的注释字符串
    size_t pos = wgslOut->wgsl.find(annotationStr);
    while (pos != std::string::npos) {
        wgslOut->wgsl.erase(pos, strlen(annotationStr));
        pos = wgslOut->wgsl.find(annotationStr);
    }
    // 步骤11：输出WGSL代码
    *outWsl = wgslOut->wgsl;
    return true;
#else
    // 如果不支持WebGPU，输出错误信息
    slog.i << "Trying to emit WGSL without including WebGPU dependencies,"
            " please set CMake arg FILAMENT_SUPPORTS_WEBGPU and FILAMENT_SUPPORTS_WEBGPU"
            << io::endl;
    return false;
#endif

}

// 处理着色器（将输入GLSL转换为目标格式）
// @param inputShader 输入GLSL着色器代码
// @param config 配置信息
// @param outputGlsl 输出参数，GLSL代码（可选）
// @param outputSpirv 输出参数，SPIR-V二进制（可选）
// @param outputMsl 输出参数，MSL代码（可选）
// @param outputWgsl 输出参数，WGSL代码（可选）
// @return 如果处理成功返回true，否则返回false
bool GLSLPostProcessor::process(const std::string& inputShader, Config const& config,
                                std::string* outputGlsl, SpirvBlob* outputSpirv, std::string* outputMsl, std::string* outputWgsl) {
    using TargetLanguage = MaterialBuilder::TargetLanguage;

    // 步骤1：如果目标是GLSL且无优化，直接返回输入着色器
    if (config.targetLanguage == TargetLanguage::GLSL &&
            mOptimization == MaterialBuilder::Optimization::NONE) {
        *outputGlsl = inputShader;
        if (mPrintShaders) {
            slog.i << *outputGlsl << io::endl;
        }
        return true;
    }

    // 步骤2：初始化内部配置
    InternalConfig internalConfig{
            .glslOutput = outputGlsl,
            .spirvOutput = outputSpirv,
            .mslOutput = outputMsl,
            .wgslOutput = outputWgsl,
    };

    // 步骤3：根据着色器阶段设置glslang着色器语言
    switch (config.shaderType) {
        case ShaderStage::VERTEX:
            internalConfig.shLang = EShLangVertex;
            break;
        case ShaderStage::FRAGMENT:
            internalConfig.shLang = EShLangFragment;
            break;
        case ShaderStage::COMPUTE:
            internalConfig.shLang = EShLangCompute;
            break;
    }

    // 步骤4：创建glslang程序和着色器对象
    TProgram program;
    TShader tShader(internalConfig.shLang);

    // The cleaner must be declared after the TShader to prevent ASAN failures.
    // 清理器必须在TShader之后声明以防止ASAN失败
    GLSLangCleaner const cleaner;

    // 步骤5：设置着色器源代码
    const char* shaderCString = inputShader.c_str();
    tShader.setStrings(&shaderCString, 1);

    // This allows shaders to query if they will be run through glslang.
    // OpenGL shaders without optimization, for example, won't have this define.
    // 这允许着色器查询它们是否将通过glslang运行
    // 例如，没有优化的OpenGL着色器不会有此定义
    tShader.setPreamble("#define FILAMENT_GLSLANG\n");

    // 步骤6：获取GLSL版本并准备着色器解析器
    internalConfig.langVersion = GLSLTools::getGlslDefaultVersion(config.shaderModel);
    GLSLTools::prepareShaderParser(config.targetApi, config.targetLanguage, tShader,
            internalConfig.shLang, internalConfig.langVersion);

    // 步骤7：获取glslang消息标志
    EShMessages msg = GLSLTools::glslangFlagsFromTargetApi(config.targetApi, config.targetLanguage);
    // 步骤8：如果使用帧缓冲区获取，添加Vulkan规则标志
    if (config.hasFramebufferFetch) {
        // FIXME: subpasses require EShMsgVulkanRules, which I think is a mistake.
        //        SpvRules should be enough.
        //        I think this could cause the compilation to fail on gl_VertexID.
        // FIXME: 子通道需要EShMsgVulkanRules，我认为这是一个错误。SpvRules应该足够了
        // 我认为这可能导致gl_VertexID编译失败
        using Type = std::underlying_type_t<EShMessages>;
        msg = EShMessages(Type(msg) | Type(EShMsgVulkanRules));
    }

    // 步骤9：解析着色器
    bool const ok = tShader.parse(&DefaultTBuiltInResource, internalConfig.langVersion, false, msg);
    if (!ok) {
        slog.e << tShader.getInfoLog() << io::endl;
        return false;
    }

    // add texture lod bias
    // 步骤10：如果是片段着色器且是表面材质，添加纹理LOD偏置
    if (config.shaderType == ShaderStage::FRAGMENT &&
        config.domain == MaterialDomain::SURFACE) {
        GLSLTools::textureLodBias(tShader);
    }

    // 步骤11：将着色器添加到程序并链接（即使只有一个着色器阶段，链接仍然是必要的以完成SPIR-V类型）
    program.addShader(&tShader);
    // Even though we only have a single shader stage, linking is still necessary to finalize
    // SPIR-V types
    bool const linkOk = program.link(msg);
    if (!linkOk) {
        slog.e << tShader.getInfoLog() << io::endl;
        return false;
    }

    // 步骤12：根据优化级别执行不同的优化路径
    switch (mOptimization) {
        case MaterialBuilder::Optimization::NONE:
            // 步骤12.1：无优化模式 - 直接生成SPIR-V和其他目标格式
            if (internalConfig.spirvOutput) {
                // 步骤12.1.1：生成SPIR-V
                SpvOptions options;
                options.generateDebugInfo = mGenerateDebugInfo;
                GlslangToSpv(*program.getIntermediate(internalConfig.shLang),
                        *internalConfig.spirvOutput, &options);
                // 步骤12.1.2：修复裁剪距离
                fixupClipDistance(*internalConfig.spirvOutput, config);
                // 步骤12.1.3：如果需要MSL输出，转换为MSL
                if (internalConfig.mslOutput) {
                    auto sibs = SibVector::with_capacity(CONFIG_SAMPLER_BINDING_COUNT);
                    DescriptorSets descriptors {};
                    msl::collectDescriptorSets(config, descriptors);
#if DEBUG_LOG_DESCRIPTOR_SETS == 1
                    msl::prettyPrintDescriptorSetInfoVector(descriptors);
#endif
                    spirvToMsl(internalConfig.spirvOutput, internalConfig.mslOutput,
                            config.shaderType, config.shaderModel, config.hasFramebufferFetch, descriptors,
                            mGenerateDebugInfo ? &internalConfig.minifier : nullptr);
                }
                // 步骤12.1.4：如果需要WGSL输出，转换为WGSL
                if (internalConfig.wgslOutput) {
                    if (!spirvToWgsl(internalConfig.spirvOutput, internalConfig.wgslOutput)) {
                        return false;
                    }
                }
            } else {
                slog.e << "GLSL post-processor invoked with optimization level NONE"
                        << io::endl;
            }
            break;
        case MaterialBuilder::Optimization::PREPROCESSOR:
            // 步骤12.2：预处理器优化模式 - 仅使用预处理器优化
            if (!preprocessOptimization(tShader, config, internalConfig)) {
                return false;
            }
            break;
        case MaterialBuilder::Optimization::SIZE:
        case MaterialBuilder::Optimization::PERFORMANCE:
            // 步骤12.3：完整优化模式（大小或性能优化）- 使用SPIR-V优化器
            if (!fullOptimization(tShader, config, internalConfig)) {
                return false;
            }
            break;
    }

    // 步骤13：如果输出GLSL，进行后处理（压缩和重命名）
    if (internalConfig.glslOutput) {
        if (!mGenerateDebugInfo) {
            // 步骤13.1：移除空白字符（如果是SIZE优化，合并大括号）
            *internalConfig.glslOutput =
                    internalConfig.minifier.removeWhitespace(
                            *internalConfig.glslOutput,
                            mOptimization == MaterialBuilder::Optimization::SIZE);

            // In theory this should only be enabled for SIZE, but in practice we often use PERFORMANCE.
            // 理论上这应该只为SIZE启用，但实际上我们经常使用PERFORMANCE
            // 步骤13.2：重命名结构体字段（如果不是NONE优化）
            if (mOptimization != MaterialBuilder::Optimization::NONE) {
                *internalConfig.glslOutput =
                        internalConfig.minifier.renameStructFields(*internalConfig.glslOutput);
            }
        }
        // 步骤13.3：如果启用了打印着色器标志，输出GLSL代码
        if (mPrintShaders) {
            slog.i << *internalConfig.glslOutput << io::endl;
        }
    }
    return true;
}

// 预处理器优化（仅使用预处理器进行优化，不生成SPIR-V）
// @param tShader glslang着色器对象
// @param config 配置信息
// @param internalConfig 内部配置
// @return 如果优化成功返回true，否则返回false
bool GLSLPostProcessor::preprocessOptimization(TShader& tShader,
        Config const& config, InternalConfig& internalConfig) const {
    using TargetApi = MaterialBuilder::TargetApi;
    // 步骤1：验证配置（OpenGL不需要SPIR-V输出）
    assert_invariant(bool(internalConfig.spirvOutput) == (config.targetApi != TargetApi::OPENGL));

    // 步骤2：预处理着色器（展开宏、处理条件编译等）
    std::string glsl;
    TShader::ForbidIncluder forbidIncluder;

    const int version = GLSLTools::getGlslDefaultVersion(config.shaderModel);
    EShMessages const msg =
            GLSLTools::glslangFlagsFromTargetApi(config.targetApi, config.targetLanguage);
    bool ok = tShader.preprocess(&DefaultTBuiltInResource, version, ENoProfile, false, false,
            msg, &glsl, forbidIncluder);

    if (!ok) {
        slog.e << tShader.getInfoLog() << io::endl;
        return false;
    }

    // 步骤3：如果需要SPIR-V输出，编译预处理后的GLSL为SPIR-V
    if (internalConfig.spirvOutput) {
        // 步骤3.1：创建新的着色器对象用于SPIR-V生成
        TProgram program;
        TShader spirvShader(internalConfig.shLang);

        // The cleaner must be declared after the TShader/TProgram which are setting the current
        // pool in the tls
        // 清理器必须在TShader/TProgram之后声明，因为它们设置TLS中的当前池
        GLSLangCleaner const cleaner;

        // 步骤3.2：设置预处理后的GLSL代码并解析
        const char* shaderCString = glsl.c_str();
        spirvShader.setStrings(&shaderCString, 1);
        GLSLTools::prepareShaderParser(config.targetApi, config.targetLanguage, spirvShader,
                internalConfig.shLang, internalConfig.langVersion);
        ok = spirvShader.parse(&DefaultTBuiltInResource, internalConfig.langVersion, false, msg);
        // 步骤3.3：将着色器添加到程序并链接
        program.addShader(&spirvShader);
        // Even though we only have a single shader stage, linking is still necessary to finalize
        // SPIR-V types
        // 即使只有一个着色器阶段，链接仍然是必要的以完成SPIR-V类型
        bool const linkOk = program.link(msg);
        if (!ok || !linkOk) {
            slog.e << spirvShader.getInfoLog() << io::endl;
            return false;
        } else {
            // 步骤3.4：生成SPIR-V并修复裁剪距离
            SpvOptions options;
            options.generateDebugInfo = mGenerateDebugInfo;
            GlslangToSpv(*program.getIntermediate(internalConfig.shLang),
                    *internalConfig.spirvOutput, &options);
            fixupClipDistance(*internalConfig.spirvOutput, config);
        }
    }

    // 步骤4：如果需要MSL输出，转换为MSL
    if (internalConfig.mslOutput) {
        DescriptorSets descriptors {};
        msl::collectDescriptorSets(config, descriptors);
#if DEBUG_LOG_DESCRIPTOR_SETS == 1
        msl::prettyPrintDescriptorSetInfoVector(descriptors);
#endif
        spirvToMsl(internalConfig.spirvOutput, internalConfig.mslOutput, config.shaderType,
                config.shaderModel, config.hasFramebufferFetch, descriptors,
                mGenerateDebugInfo ? &internalConfig.minifier : nullptr);
    }
    // 步骤5：如果需要WGSL输出，转换为WGSL
    if (internalConfig.wgslOutput) {
        if (!spirvToWgsl(internalConfig.spirvOutput, internalConfig.wgslOutput)) {
            return false;
        }
    }

    // 步骤6：如果需要GLSL输出，使用预处理后的GLSL
    if (internalConfig.glslOutput) {
        *internalConfig.glslOutput = glsl;
    }
    return true;
}

// 完整优化（使用SPIR-V优化器进行完整优化）
// @param tShader glslang着色器对象
// @param config 配置信息
// @param internalConfig 内部配置
// @return 如果优化成功返回true，否则返回false
bool GLSLPostProcessor::fullOptimization(const TShader& tShader,
        Config const& config, InternalConfig& internalConfig) const {
    SpirvBlob spirv;

    // 步骤1：确定是否针对大小优化
    bool const optimizeForSize = mOptimization == MaterialBuilderBase::Optimization::SIZE;

    // Compile GLSL to to SPIR-V
    // 步骤2：将GLSL编译为SPIR-V
    SpvOptions options;
    options.generateDebugInfo = mGenerateDebugInfo;
    GlslangToSpv(*tShader.getIntermediate(), spirv, &options);

    // 步骤3：如果需要SPIR-V输出或不是大小优化，运行SPIR-V优化器
    if (internalConfig.spirvOutput) {
        // Run the SPIR-V optimizer
        // 运行SPIR-V优化器
        OptimizerPtr const optimizer = createOptimizer(mOptimization, config);
        optimizeSpirv(optimizer, spirv);
    } else {
        // 如果没有SPIR-V输出但需要性能优化，仍然运行优化器
        if (!optimizeForSize) {
            OptimizerPtr const optimizer = createOptimizer(mOptimization, config);
            optimizeSpirv(optimizer, spirv);
        }
    }

    // 步骤4：修复裁剪距离
    fixupClipDistance(spirv, config);

    // 步骤5：如果需要SPIR-V输出，保存优化后的SPIR-V
    if (internalConfig.spirvOutput) {
        *internalConfig.spirvOutput = spirv;
    }

    // 步骤6：如果需要MSL输出，转换为MSL
    if (internalConfig.mslOutput) {
        DescriptorSets descriptors {};
        msl::collectDescriptorSets(config, descriptors);
#if DEBUG_LOG_DESCRIPTOR_SETS == 1
        msl::prettyPrintDescriptorSetInfoVector(descriptors);
#endif
        spirvToMsl(&spirv, internalConfig.mslOutput, config.shaderType, config.shaderModel,
                config.hasFramebufferFetch, descriptors,
                mGenerateDebugInfo ? &internalConfig.minifier : nullptr);
    }
    // 步骤7：如果需要WGSL输出，转换为WGSL
    if (internalConfig.wgslOutput) {
        if (!spirvToWgsl(&spirv, internalConfig.wgslOutput)) {
            return false;
        }
    }

    // Transpile back to GLSL
    // 步骤8：如果需要GLSL输出，将SPIR-V转换回GLSL
    if (internalConfig.glslOutput) {
        // 步骤8.1：配置GLSL编译器选项
        CompilerGLSL::Options glslOptions;
        auto version = GLSLTools::getShadingLanguageVersion(
                config.shaderModel, config.featureLevel);
        glslOptions.es = version.second;  // 是否为ES环境
        glslOptions.version = version.first;  // GLSL版本
        glslOptions.enable_420pack_extension = glslOptions.version >= 420;  // 启用420pack扩展
        // 步骤8.2：设置片段着色器默认精度
        glslOptions.fragment.default_float_precision = glslOptions.es ?
                CompilerGLSL::Options::Precision::Mediump : CompilerGLSL::Options::Precision::Highp;
        glslOptions.fragment.default_int_precision = glslOptions.es ?
                CompilerGLSL::Options::Precision::Mediump : CompilerGLSL::Options::Precision::Highp;

        // TODO: this should be done only on the "feature level 0" variant
        // TODO: 这应该只在"功能级别0"变体上完成
        // 步骤8.3：如果是功能级别0，将UBO转换为普通uniform
        if (config.featureLevel == 0) {
            // convert UBOs to plain uniforms if we're at feature level 0
            glslOptions.emit_uniform_buffer_as_plain_uniforms = true;
        }

        // 步骤8.4：如果变体有立体视觉且是顶点着色器，配置立体视觉选项
        if (config.variant.hasStereo() && config.shaderType == ShaderStage::VERTEX) {
            switch (config.materialInfo->stereoscopicType) {
            case StereoscopicType::MULTIVIEW:
                // For stereo variants using multiview feature, this generates the shader code below.
                //   #extension GL_OVR_multiview2 : require
                //   layout(num_views = 2) in;
                // 对于使用多视图功能的立体变体，生成以下着色器代码
                glslOptions.ovr_multiview_view_count = config.materialInfo->stereoscopicEyeCount;
                break;
            case StereoscopicType::INSTANCED:
            case StereoscopicType::NONE:
                // Nothing to generate
                // 无需生成
                break;
            }
        }

        // 步骤8.5：创建GLSL编译器并设置选项
        CompilerGLSL glslCompiler(std::move(spirv));
        glslCompiler.set_common_options(glslOptions);

        // 步骤8.6：如果不是ES环境，启用GL_ARB_shading_language_packing扩展
        if (!glslOptions.es) {
            // enable GL_ARB_shading_language_packing if available
            glslCompiler.add_header_line("#extension GL_ARB_shading_language_packing : enable");
        }

        // 步骤8.7：如果是片段着色器且是ES环境，重新映射帧缓冲区获取
        if (tShader.getStage() == EShLangFragment && glslOptions.es) {
            for (auto i : config.glsl.subpassInputToColorLocation) {
                glslCompiler.remap_ext_framebuffer_fetch(i.first, i.second, true);
            }
        }

        // 步骤8.8：编译SPIR-V为GLSL（根据是否启用异常处理使用不同方式）
#ifdef SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS
        *internalConfig.glslOutput = glslCompiler.compile();
#else
        try {
            *internalConfig.glslOutput = glslCompiler.compile();
        } catch (CompilerError e) {
            slog.e << "ERROR: " << e.what() << io::endl;
            return false;
        }
#endif

        // spirv-cross automatically redeclares gl_ClipDistance if it's used. Some drivers don't
        // like this, so we simply remove it.
        // According to EXT_clip_cull_distance, gl_ClipDistance can be
        // "implicitly sized by indexing it only with integral constant expressions".
        // spirv-cross如果使用了gl_ClipDistance会自动重新声明它。某些驱动程序不喜欢这样，所以我们简单地移除它
        // 根据EXT_clip_cull_distance，gl_ClipDistance可以通过仅使用整数常量表达式索引来隐式确定大小
        // 步骤8.8：移除spirv-cross自动生成的gl_ClipDistance声明
        std::string& str = *internalConfig.glslOutput;
        const std::string clipDistanceDefinition = "out float gl_ClipDistance[2];";
        size_t const found = str.find(clipDistanceDefinition);
        if (found != std::string::npos) {
            str.replace(found, clipDistanceDefinition.length(), "");
        }
    }
    return true;
}

// 创建空优化器（不注册任何优化通道）
// @return 空优化器指针
std::shared_ptr<Optimizer> GLSLPostProcessor::createEmptyOptimizer() {
    // 步骤1：创建SPIR-V优化器（使用通用1.3环境）
    auto optimizer = std::make_shared<Optimizer>(SPV_ENV_UNIVERSAL_1_3);
    // 步骤2：设置消息消费者（过滤和格式化优化器消息）
    optimizer->SetMessageConsumer([](spv_message_level_t level,
            const char* source, const spv_position_t& position, const char* message) {
        if (!filterSpvOptimizerMessage(level)) {
            return;
        }
        slog.e << stringifySpvOptimizerMessage(level, source, position, message)
                << io::endl;
    });
    return optimizer;
}

// 创建优化器实例（根据优化级别和着色器配置调整）
// @param optimization 优化级别
// @param config 配置信息
// @return 优化器指针
std::shared_ptr<Optimizer> GLSLPostProcessor::createOptimizer(
        MaterialBuilder::Optimization optimization, Config const& config) {
    // 步骤1：创建空优化器
    auto optimizer = createEmptyOptimizer();

    // 步骤2：根据优化级别注册相应的优化通道
    if (optimization == MaterialBuilder::Optimization::SIZE) {
        // When optimizing for size, we don't run the SPIR-V through any size optimization passes
        // when targeting MSL. This results in better line dictionary compression. We do, however,
        // still register the passes necessary (below) to support half precision floating point
        // math.
        // 针对大小优化时，如果目标是MSL，我们不运行任何大小优化通道
        // 这会导致更好的行字典压缩。但是，我们仍然注册必要的通道（如下）以支持半精度浮点数学
        if (config.targetApi != MaterialBuilder::TargetApi::METAL) {
            registerSizePasses(*optimizer, config);
        }
    } else if (optimization == MaterialBuilder::Optimization::PERFORMANCE) {
        registerPerformancePasses(*optimizer, config);
    }

    // Metal doesn't support relaxed precision, but does have support for float16 math operations.
    // Metal不支持宽松精度，但确实支持float16数学运算
    // 步骤3：如果目标是Metal，注册Metal特定的优化通道
    if (config.targetApi == MaterialBuilder::TargetApi::METAL) {
        optimizer->RegisterPass(CreateConvertRelaxedToHalfPass());  // 将宽松精度转换为半精度
        optimizer->RegisterPass(CreateSimplificationPass());  // 简化通道
        optimizer->RegisterPass(CreateRedundancyEliminationPass());  // 冗余消除
        optimizer->RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    }

    return optimizer;
}

// 优化SPIR-V代码
// @param optimizer 优化器
// @param spirv SPIR-V二进制数据（将被修改）
void GLSLPostProcessor::optimizeSpirv(OptimizerPtr optimizer, SpirvBlob& spirv) {

    // Always add the CanonicalizeIds Pass.
    // The CanonicalIds pass replaces the old SPIR-V remapper in Glslang.
    // 总是添加CanonicalizeIds通道
    // CanonicalIds通道替换了Glslang中的旧SPIR-V重映射器
    // 步骤1：注册规范化ID通道（确保ID按顺序分配）
    optimizer->RegisterPass(CreateCanonicalizeIdsPass());

    // run optimizer
    // 步骤2：运行优化器
    if (!optimizer->Run(spirv.data(), spirv.size(), &spirv)) {
        slog.e << "SPIR-V optimizer pass failed" << io::endl;
        return;
    }
}

// 修复裁剪距离（将filament_gl_ClipDistance装饰为gl_ClipDistance）
// @param spirv SPIR-V二进制数据（将被修改）
// @param config 配置信息
void GLSLPostProcessor::fixupClipDistance(
        SpirvBlob& spirv, Config const& config) const {
    // 步骤1：如果未使用裁剪距离，直接返回
    if (!config.usesClipDistance) {
        return;
    }
    // This should match the version of SPIR-V used in GLSLTools::prepareShaderParser.
    // 这应该与GLSLTools::prepareShaderParser中使用的SPIR-V版本匹配
    // 步骤2：创建SPIR-V工具（使用通用1.3环境）
    SpirvTools const tools(SPV_ENV_UNIVERSAL_1_3);
    // 步骤3：将SPIR-V反汇编为文本
    std::string disassembly;
    const bool result = tools.Disassemble(spirv, &disassembly);
    assert_invariant(result);
    // 步骤4：修复裁剪距离装饰
    if (filamat::fixupClipDistance(disassembly)) {
        // 步骤5：如果修复成功，重新汇编SPIR-V并验证
        spirv.clear();
        tools.Assemble(disassembly, &spirv);
        assert_invariant(tools.Validate(spirv));
    }
}

// CreateMergeReturnPass() causes these issues:
// - triggers a segfault with AMD OpenGL drivers on macOS
// - triggers a crash on some Adreno drivers (b/291140208, b/289401984, b/289393290)
// However Metal requires this pass in order to correctly generate half-precision MSL
// CreateMergeReturnPass() also creates issues with Tint conversion related to the
// bitwise "<<" Operator used in shaders/src/surface_light_directional.fs against
// a signed integer.
//
// CreateSimplificationPass() creates a lot of problems:
// - Adreno GPU show artifacts after running simplification passes (Vulkan)
// - spirv-cross fails generating working glsl
//      (https://github.com/KhronosGroup/SPIRV-Cross/issues/2162)
// - generally it makes the code more complicated, e.g.: replacing for loops with
//   while-if-break, unclear if it helps for anything.
// However, the simplification passes below are necessary when targeting Metal, otherwise the
// result is mismatched half / float assignments in MSL.
// CreateMergeReturnPass()会导致以下问题：
// - 在macOS上触发AMD OpenGL驱动程序的段错误
// - 在某些Adreno驱动程序上触发崩溃（b/291140208, b/289401984, b/289393290）
// 但是Metal需要此通道以正确生成半精度MSL
// CreateMergeReturnPass()还会在与Tint转换相关的问题，涉及在shaders/src/surface_light_directional.fs中
// 对带符号整数使用的按位"<<"操作符
//
// CreateSimplificationPass()会产生很多问题：
// - Adreno GPU在运行简化通道后显示伪影（Vulkan）
// - spirv-cross无法生成可工作的glsl（https://github.com/KhronosGroup/SPIRV-Cross/issues/2162）
// - 通常它使代码更复杂，例如：用while-if-break替换for循环，不清楚是否有帮助
// 但是，下面的简化通道在针对Metal时是必要的，否则结果是在MSL中不匹配的半精度/浮点赋值

// 注册性能优化通道（针对性能优化）
// @param optimizer 优化器
// @param config 配置信息
void GLSLPostProcessor::registerPerformancePasses(Optimizer& optimizer, Config const& config) {
    // 定义lambda函数，用于注册优化通道（带API过滤）
    auto RegisterPass = [&](Optimizer::PassToken&& pass,
            MaterialBuilder::TargetApi apiFilter = MaterialBuilder::TargetApi::ALL) {
        // Workaround management is currently very simple, only two values are possible
        // ALL and NONE. If the value is anything but NONE, we apply all workarounds.
        // 工作区管理目前非常简单，只有两个可能的值：ALL和NONE。如果值不是NONE，我们应用所有工作区
        // 步骤1：如果启用了工作区，检查API过滤器
        if (config.workarounds != MaterialBuilderBase::Workarounds::NONE) {
            if (!(config.targetApi & apiFilter)) {
                return;
            }
        }

        // FIXME: Workaround within a workaround!!! We IGNORE config.workarounds for WEBGPU
        //        because Tint doesn't even compile with MergeReturn/Simplification pass
        //        active:
        //            Tint Reader Error: warning: code is unreachable
        //            error: no matching overload for 'operator << (i32, i32)'
        //            2 candidate operators:
        //             • 'operator << (T  ✓ , u32  ✗ ) -> T' where:
        //                  ✓  'T' is 'abstract-int', 'i32' or 'u32'
        //             • 'operator << (vecN<T>  ✗ , vecN<u32>  ✗ ) -> vecN<T>' where:
        //                  ✗  'T' is 'abstract-int', 'i32' or 'u32'
        // FIXME: 工作区中的工作区！！！我们忽略WEBGPU的config.workarounds
        // 因为Tint在使用MergeReturn/Simplification通道时甚至无法编译
        // 步骤2：如果是WEBGPU目标，检查API过滤器（忽略工作区设置）
        if (any(config.targetApi & MaterialBuilder::TargetApi::WEBGPU)) {
            if (!(config.targetApi & apiFilter)) {
                return;
            }
        }

        // 步骤3：注册优化通道
        optimizer.RegisterPass(std::move(pass));
    };

    // 注册性能优化通道序列（按优化顺序）
    RegisterPass(CreateWrapOpKillPass());  // 包装OpKill指令
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateMergeReturnPass(), MaterialBuilder::TargetApi::METAL);  // 合并返回（仅Metal）
    RegisterPass(CreateInlineExhaustivePass());  // 内联函数（穷举）
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreatePrivateToLocalPass());  // 私有变量转局部变量
    RegisterPass(CreateLocalSingleBlockLoadStoreElimPass());  // 消除单块内的加载/存储
    RegisterPass(CreateLocalSingleStoreElimPass());  // 消除局部单次存储
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateScalarReplacementPass());  // 标量替换
    RegisterPass(CreateLocalAccessChainConvertPass());  // 转换局部访问链
    RegisterPass(CreateLocalSingleBlockLoadStoreElimPass());  // 消除单块内的加载/存储
    RegisterPass(CreateLocalSingleStoreElimPass());  // 消除局部单次存储
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateLocalMultiStoreElimPass());  // 消除局部多次存储
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateCCPPass());  // 常量传播和折叠
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateRedundancyEliminationPass());  // 冗余消除
    RegisterPass(CreateCombineAccessChainsPass());  // 合并访问链
    RegisterPass(CreateSimplificationPass(), MaterialBuilder::TargetApi::METAL);  // 简化（仅Metal）
    RegisterPass(CreateVectorDCEPass());  // 向量死代码消除
    RegisterPass(CreateDeadInsertElimPass());  // 消除死插入
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateSimplificationPass(), MaterialBuilder::TargetApi::METAL);  // 简化（仅Metal）
    RegisterPass(CreateIfConversionPass());  // if转换
    RegisterPass(CreateCopyPropagateArraysPass());  // 数组复制传播
    RegisterPass(CreateReduceLoadSizePass());  // 减少加载大小
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateBlockMergePass());  // 合并基本块
    RegisterPass(CreateRedundancyEliminationPass());  // 冗余消除
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateBlockMergePass());  // 合并基本块
    RegisterPass(CreateSimplificationPass(), MaterialBuilder::TargetApi::METAL);  // 简化（仅Metal）
}

// 注册大小优化通道（针对代码大小优化）
// @param optimizer 优化器
// @param config 配置信息
void GLSLPostProcessor::registerSizePasses(Optimizer& optimizer, Config const& config) {
    // 定义lambda函数，用于注册优化通道（带API过滤）
    auto RegisterPass = [&](Optimizer::PassToken&& pass,
            MaterialBuilder::TargetApi apiFilter = MaterialBuilder::TargetApi::ALL) {
        // 步骤1：检查API过滤器
        if (!(config.targetApi & apiFilter)) {
            return;
        }
        // 步骤2：注册优化通道
        optimizer.RegisterPass(std::move(pass));
    };

    // 注册大小优化通道序列（按优化顺序，针对代码大小优化）
    RegisterPass(CreateWrapOpKillPass());  // 包装OpKill指令
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateInlineExhaustivePass());  // 内联函数（穷举）
    RegisterPass(CreateEliminateDeadFunctionsPass());  // 消除死函数
    RegisterPass(CreatePrivateToLocalPass());  // 私有变量转局部变量
    RegisterPass(CreateScalarReplacementPass(0));  // 标量替换（阈值为0）
    RegisterPass(CreateLocalMultiStoreElimPass());  // 消除局部多次存储
    RegisterPass(CreateCCPPass());  // 常量传播和折叠
    RegisterPass(CreateLoopUnrollPass(true));  // 循环展开（完全展开）
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateScalarReplacementPass(0));  // 标量替换（阈值为0）
    RegisterPass(CreateLocalSingleStoreElimPass());  // 消除局部单次存储
    RegisterPass(CreateIfConversionPass());  // if转换
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateDeadBranchElimPass());  // 消除死分支
    RegisterPass(CreateBlockMergePass());  // 合并基本块
    RegisterPass(CreateLocalAccessChainConvertPass());  // 转换局部访问链
    RegisterPass(CreateLocalSingleBlockLoadStoreElimPass());  // 消除单块内的加载/存储
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateCopyPropagateArraysPass());  // 数组复制传播
    RegisterPass(CreateVectorDCEPass());  // 向量死代码消除
    RegisterPass(CreateDeadInsertElimPass());  // 消除死插入
    // this breaks UBO layout
    // 这会破坏UBO布局
    //RegisterPass(CreateEliminateDeadMembersPass());  // 消除死成员（已禁用）
    RegisterPass(CreateLocalSingleStoreElimPass());  // 消除局部单次存储
    RegisterPass(CreateBlockMergePass());  // 合并基本块
    RegisterPass(CreateLocalMultiStoreElimPass());  // 消除局部多次存储
    RegisterPass(CreateRedundancyEliminationPass());  // 冗余消除
    RegisterPass(CreateAggressiveDCEPass());  // 激进死代码消除
    RegisterPass(CreateCFGCleanupPass());  // 控制流图清理
}

} // namespace filamat
