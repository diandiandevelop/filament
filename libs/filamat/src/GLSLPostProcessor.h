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

#ifndef TNT_GLSLPOSTPROCESSOR_H
#define TNT_GLSLPOSTPROCESSOR_H

#include <filamat/MaterialBuilder.h>    // for MaterialBuilder:: enums

#include <private/filament/Variant.h>
#include <private/filament/SamplerInterfaceBlock.h>

#include "ShaderMinifier.h"

#include <spirv-tools/optimizer.hpp>

#include <ShaderLang.h>

#include <backend/DriverEnums.h>

#include <utils/FixedCapacityVector.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace filamat {

using SpirvBlob = std::vector<uint32_t>;
using BindingPointAndSib = std::pair<uint8_t, const filament::SamplerInterfaceBlock*>;
using SibVector = utils::FixedCapacityVector<BindingPointAndSib>;

using DescriptorInfo = std::tuple<
        utils::CString,
        filament::backend::DescriptorSetLayoutBinding,
        std::optional<filament::SamplerInterfaceBlock::SamplerInfo>>;
using DescriptorSetInfo = utils::FixedCapacityVector<DescriptorInfo>;
using DescriptorSets = std::array<DescriptorSetInfo, filament::backend::MAX_DESCRIPTOR_SET_COUNT>;

class GLSLPostProcessor {
public:
    enum Flags : uint32_t {
        PRINT_SHADERS = 1 << 0,
        GENERATE_DEBUG_INFO = 1 << 1,
    };

    // 构造函数：初始化GLSL后处理器
    // @param optimization 优化级别
    // @param workarounds 工作区标志
    // @param flags 处理器标志（PRINT_SHADERS、GENERATE_DEBUG_INFO等）
    GLSLPostProcessor(
            MaterialBuilder::Optimization optimization,
            MaterialBuilder::Workarounds workarounds,
            uint32_t flags);

    // 析构函数
    ~GLSLPostProcessor();

    // 配置结构：包含后处理器所需的所有配置信息
    struct Config {
        filament::Variant variant;
        filament::UserVariantFilterMask variantFilter;
        MaterialBuilder::TargetApi targetApi;
        MaterialBuilder::TargetLanguage targetLanguage;
        MaterialBuilder::Workarounds workarounds;
        filament::backend::ShaderStage shaderType;
        filament::backend::ShaderModel shaderModel;
        filament::backend::FeatureLevel featureLevel;
        filament::MaterialDomain domain;
        const filamat::MaterialInfo* materialInfo;
        bool hasFramebufferFetch;
        bool usesClipDistance;
        struct {
            std::vector<std::pair<uint32_t, uint32_t>> subpassInputToColorLocation;
        } glsl;
    };

    // 处理着色器（将输入GLSL转换为目标格式）
    // @param inputShader 输入GLSL着色器代码
    // @param config 配置信息
    // @param outputGlsl 输出参数，GLSL代码（可选）
    // @param outputSpirv 输出参数，SPIR-V二进制（可选）
    // @param outputMsl 输出参数，MSL代码（可选）
    // @param outputWgsl 输出参数，WGSL代码（可选）
    // @return 如果处理成功返回true，否则返回false
    bool process(const std::string& inputShader, Config const& config,
            std::string* outputGlsl,
            SpirvBlob* outputSpirv,
            std::string* outputMsl,
            std::string* outputWgsl);

    // public so backend_test can also use it
    // 将SPIR-V转换为MSL（公开方法，backend_test也可以使用）
    // @param spirv SPIR-V二进制数据
    // @param outMsl 输出参数，MSL代码
    // @param stage 着色器阶段
    // @param shaderModel 着色器模型
    // @param useFramebufferFetch 是否使用帧缓冲区获取
    // @param descriptorSets 描述符集信息
    // @param minifier 着色器压缩器（可选，用于调试信息）
    static void spirvToMsl(const SpirvBlob* spirv, std::string* outMsl,
            filament::backend::ShaderStage stage, filament::backend::ShaderModel shaderModel,
            bool useFramebufferFetch, const DescriptorSets& descriptorSets,
            const ShaderMinifier* minifier);

    // 将SPIR-V转换为WGSL
    // @param spirv SPIR-V二进制数据（将被修改）
    // @param outWsl 输出参数，WGSL代码
    // @return 如果转换成功返回true，否则返回false
    static bool spirvToWgsl(SpirvBlob* spirv, std::string* outWsl);

private:
    struct InternalConfig {
        std::string* glslOutput = nullptr;
        SpirvBlob* spirvOutput = nullptr;
        std::string* mslOutput = nullptr;
        std::string* wgslOutput = nullptr;
        EShLanguage shLang = EShLangFragment;
        // use 100 for ES environment, 110 for desktop
         int langVersion = 0;
        ShaderMinifier minifier;
    };

    // 完整优化（使用SPIR-V优化器进行完整优化）
    // @param tShader glslang着色器对象
    // @param config 配置信息
    // @param internalConfig 内部配置
    // @return 如果优化成功返回true，否则返回false
    bool fullOptimization(const glslang::TShader& tShader,
            GLSLPostProcessor::Config const& config, InternalConfig& internalConfig) const;

    // 预处理器优化（仅使用预处理器进行优化，不生成SPIR-V）
    // @param tShader glslang着色器对象
    // @param config 配置信息
    // @param internalConfig 内部配置
    // @return 如果优化成功返回true，否则返回false
    bool preprocessOptimization(glslang::TShader& tShader,
            GLSLPostProcessor::Config const& config, InternalConfig& internalConfig) const;

    /**
     * Retrieve an optimizer instance tuned for the given optimization level and shader configuration.
     */
    // 优化器指针类型
    using OptimizerPtr = std::shared_ptr<spvtools::Optimizer>;

    // 创建优化器实例（根据优化级别和着色器配置调整）
    // @param optimization 优化级别
    // @param config 配置信息
    // @return 优化器指针
    static OptimizerPtr createOptimizer(
            MaterialBuilder::Optimization optimization,
            Config const& config);

    // 创建空优化器（不注册任何优化通道）
    // @return 空优化器指针
    static OptimizerPtr createEmptyOptimizer();

    // 注册大小优化通道（针对代码大小优化）
    // @param optimizer 优化器
    // @param config 配置信息
    static void registerSizePasses(spvtools::Optimizer& optimizer, Config const& config);

    // 注册性能优化通道（针对性能优化）
    // @param optimizer 优化器
    // @param config 配置信息
    static void registerPerformancePasses(spvtools::Optimizer& optimizer, Config const& config);

    // 优化SPIR-V代码
    // @param optimizer 优化器
    // @param spirv SPIR-V二进制数据（将被修改）
    static void optimizeSpirv(OptimizerPtr optimizer, SpirvBlob &spirv);

    // 为WGSL重新绑定图像采样器（修改SPIR-V以适配WGSL）
    // @param spirv SPIR-V二进制数据（将被修改）
    static void rebindImageSamplerForWGSL(std::vector<uint32_t>& spirv);

    // 修复裁剪距离（将filament_gl_ClipDistance装饰为gl_ClipDistance）
    // @param spirv SPIR-V二进制数据（将被修改）
    // @param config 配置信息
    void fixupClipDistance(SpirvBlob& spirv, GLSLPostProcessor::Config const& config) const;

    const MaterialBuilder::Optimization mOptimization;
    const MaterialBuilder::Workarounds mWorkarounds;
    const bool mPrintShaders;
    const bool mGenerateDebugInfo;
};

} // namespace filamat

#endif //TNT_GLSLPOSTPROCESSOR_H
