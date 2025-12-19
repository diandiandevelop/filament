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

#include "SibGenerator.h"

#include "private/filament/Variant.h"
#include "private/filament/EngineEnums.h"
#include "private/filament/SamplerInterfaceBlock.h"
#include "private/filament/SibStructs.h"

#include <backend/DriverEnums.h>

#include <utils/debug.h>

namespace filament {

namespace {
constexpr bool FILTERABLE = true;   // 可过滤标志
constexpr bool MULTISAMPLE = true;  // 多重采样标志
constexpr backend::ShaderStageFlags ALL_STAGES = backend::ShaderStageFlags::ALL_SHADER_STAGE_FLAGS;  // 所有着色器阶段标志
} // namespace

// 获取每视图的采样器接口块（根据变体返回不同的SIB）
SamplerInterfaceBlock const& SibGenerator::getPerViewSib(Variant variant) noexcept {
    using Type = SamplerInterfaceBlock::Type;
    using Format = SamplerInterfaceBlock::Format;
    using Precision = SamplerInterfaceBlock::Precision;

    // What is happening here is that depending on the variant, some samplers' type or format
    // can change (e.g.: when VSM is used the shadowmap sampler is a regular float sampler),
    // so we return a different SamplerInterfaceBlock based on the variant.
    //
    // The samplers' name and binding (i.e. ordering) must match in all SamplerInterfaceBlocks
    // because this information is stored per-material and not per-shader.
    //
    // For the SSR (reflections) SamplerInterfaceBlock, only two samplers are ever used, for this
    // reason we name them "unused*" to ensure we're not using them by mistake (type/format don't
    // matter).
    // 这里发生的情况是，根据变体，某些采样器的类型或格式可能会改变（例如：当使用VSM时，阴影贴图采样器是常规的float采样器），
    // 因此我们根据变体返回不同的SamplerInterfaceBlock。
    //
    // 采样器的名称和绑定（即排序）必须在所有SamplerInterfaceBlocks中匹配，
    // 因为此信息是按材质存储的，而不是按着色器存储的。
    //
    // 对于SSR（反射）SamplerInterfaceBlock，只使用两个采样器，因此我们将它们命名为"unused*"，
    // 以确保我们不会错误地使用它们（类型/格式无关紧要）。

    static SamplerInterfaceBlock const sibPcf{ SamplerInterfaceBlock::Builder()
            .name("sampler0")
            .stageFlags(backend::ShaderStageFlags::FRAGMENT)
            .add(  {{ "shadowMap",   +PerViewBindingPoints::SHADOW_MAP,     Type::SAMPLER_2D_ARRAY, Format::SHADOW, Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "iblDFG",      +PerViewBindingPoints::IBL_DFG_LUT,    Type::SAMPLER_2D,       Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "iblSpecular", +PerViewBindingPoints::IBL_SPECULAR,   Type::SAMPLER_CUBEMAP,  Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "ssao",        +PerViewBindingPoints::SSAO,           Type::SAMPLER_2D_ARRAY, Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "ssr",         +PerViewBindingPoints::SSR,            Type::SAMPLER_2D_ARRAY, Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "structure",   +PerViewBindingPoints::STRUCTURE,      Type::SAMPLER_2D,       Format::FLOAT,  Precision::HIGH  , FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "fog",         +PerViewBindingPoints::FOG,            Type::SAMPLER_CUBEMAP,  Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES }}
            )
            .build() };

    static SamplerInterfaceBlock const sibVsm{ SamplerInterfaceBlock::Builder()
            .name("sampler0")
            .stageFlags(backend::ShaderStageFlags::FRAGMENT)
            .add(  {{ "shadowMap",   +PerViewBindingPoints::SHADOW_MAP,     Type::SAMPLER_2D_ARRAY, Format::FLOAT,  Precision::HIGH,   FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "iblDFG",      +PerViewBindingPoints::IBL_DFG_LUT,    Type::SAMPLER_2D,       Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "iblSpecular", +PerViewBindingPoints::IBL_SPECULAR,   Type::SAMPLER_CUBEMAP,  Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "ssao",        +PerViewBindingPoints::SSAO,           Type::SAMPLER_2D_ARRAY, Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "ssr",         +PerViewBindingPoints::SSR,            Type::SAMPLER_2D_ARRAY, Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "structure",   +PerViewBindingPoints::STRUCTURE,      Type::SAMPLER_2D,       Format::FLOAT,  Precision::HIGH  , FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "fog",         +PerViewBindingPoints::FOG,            Type::SAMPLER_CUBEMAP,  Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES }}
            )
            .build() };

    static SamplerInterfaceBlock const sibSsr{ SamplerInterfaceBlock::Builder()
            .name("sampler0")
            .stageFlags(backend::ShaderStageFlags::FRAGMENT)
            .add(  {{ "ssr",         +PerViewBindingPoints::SSR,            Type::SAMPLER_2D,       Format::FLOAT,  Precision::MEDIUM, FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    { "structure",   +PerViewBindingPoints::STRUCTURE,      Type::SAMPLER_2D,       Format::FLOAT,  Precision::HIGH,   FILTERABLE, !MULTISAMPLE, ALL_STAGES }}
            )
            .build() };

    // 根据变体类型返回相应的采样器接口块
    if (Variant::isSSRVariant(variant)) {
        return sibSsr;  // SSR变体
    } else if (Variant::isVSMVariant(variant)) {
        return sibVsm;  // VSM变体
    } else {
        return sibPcf;  // PCF变体（默认）
    }
}

// 获取每个可渲染对象的采样器接口块
SamplerInterfaceBlock const& SibGenerator::getPerRenderableSib(Variant) noexcept {
    using Type = SamplerInterfaceBlock::Type;
    using Format = SamplerInterfaceBlock::Format;
    using Precision = SamplerInterfaceBlock::Precision;

    static SamplerInterfaceBlock const sib = SamplerInterfaceBlock::Builder()
            .name("sampler1")
            .stageFlags(backend::ShaderStageFlags::VERTEX)
            .add({  {"positions",          +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,    Type::SAMPLER_2D_ARRAY, Format::FLOAT, Precision::HIGH, FILTERABLE,  !MULTISAMPLE, ALL_STAGES },
                    {"tangents",           +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,     Type::SAMPLER_2D_ARRAY, Format::INT,   Precision::HIGH, !FILTERABLE, !MULTISAMPLE, ALL_STAGES },
                    {"indicesAndWeights",  +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS, Type::SAMPLER_2D,       Format::FLOAT, Precision::HIGH, !FILTERABLE, !MULTISAMPLE, ALL_STAGES }}
            )
            .build();

    return sib;
}

// 根据绑定点和变体获取采样器接口块
SamplerInterfaceBlock const* SibGenerator::getSib(DescriptorSetBindingPoints set, Variant variant) noexcept {
    switch (set) {
        case DescriptorSetBindingPoints::PER_VIEW:
            return &getPerViewSib(variant);      // 每视图SIB
        case DescriptorSetBindingPoints::PER_RENDERABLE:
            return &getPerRenderableSib(variant); // 每个可渲染对象的SIB
        default:
            return nullptr;  // 未知绑定点，返回nullptr
    }
}

} // namespace filament
