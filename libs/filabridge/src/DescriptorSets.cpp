/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "private/filament/DescriptorSets.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/Variant.h>

#include <filament/MaterialEnums.h>

#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>
#include <utils/debug.h>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace filament::descriptor_sets {

using namespace backend;

// used to generate shadow-maps, structure and postfx passes - 用于生成阴影贴图、结构和后处理效果的描述符集布局列表
static constexpr std::initializer_list<DescriptorSetLayoutBinding> depthVariantDescriptorSetLayoutList = {
    { DescriptorType::UNIFORM_BUFFER, ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::FRAME_UNIFORMS },
};

/**
 * ssrVariantDescriptorSetLayout must match perViewDescriptorSetLayout's vertex stage. This is - SSR变体描述符集布局必须匹配perViewDescriptorSetLayout的顶点阶段，这是因为
 * because the SSR variant is always using the "standard" vertex shader (i.e. there is no - SSR变体总是使用"标准"顶点着色器（即没有专用的SSR顶点着色器），它使用perViewDescriptorSetLayout
 * dedicated SSR vertex shader), which uses perViewDescriptorSetLayout. - 这意味着即使SSR变体不使用，PerViewBindingPoints::SHADOWS也必须在布局中
 * This means that PerViewBindingPoints::SHADOWS must be in the layout even though it's not used - 
 * by the SSR variant. - 
 */
static constexpr std::initializer_list<DescriptorSetLayoutBinding> ssrVariantDescriptorSetLayoutList = {
    { DescriptorType::UNIFORM_BUFFER, ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::FRAME_UNIFORMS },
    { DescriptorType::UNIFORM_BUFFER, ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SHADOWS        },
    { DescriptorType::SAMPLER_2D_FLOAT,                          ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::STRUCTURE, DescriptorFlags::UNFILTERABLE },
    { DescriptorType::SAMPLER_2D_FLOAT,                          ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SSR_HISTORY    },
};

/**
 * Used for generating the color pass (i.e. the main pass). This is in fact a template that gets - 用于生成颜色通道（即主通道）的描述符集布局列表。这实际上是一个模板，根据变体展开为8种不同的布局
 * expanded to 8 different layouts, based on variants. - 
 * 
 * Note about the SHADOW_MAP binding points: - 关于SHADOW_MAP绑定点的说明：
 * This descriptor can either be a SAMPLER_FLOAT or a SAMPLER_DEPTH, - 此描述符可以是SAMPLER_FLOAT或SAMPLER_DEPTH
 * and there are 3 cases to consider: - 有3种情况需要考虑：
 * 
 *          | TextureType | CompareMode | Filtered | SamplerType | Variant |
 * ---------+-------------+-------------+----------+-------------+---------+
 *  PCF     |    DEPTH    |    COMPARE  |   Yes    |    DEPTH    |    -    |
 *  VSM     |    FLOAT    |     NONE    |   Yes    |    FLOAT    |   VSM   |
 *  OTHER   |    DEPTH    |     NONE    |   No     |    FLOAT    |   VSM   |
 * 
 * The SamplerType to use depends on the Variant. Variant::VSM is set for all cases except PCM. - 要使用的SamplerType取决于Variant。除了PCM之外，所有情况都设置Variant::VSM
 */
static constexpr std::initializer_list<DescriptorSetLayoutBinding> perViewDescriptorSetLayoutList = {
    { DescriptorType::UNIFORM_BUFFER, ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::FRAME_UNIFORMS },
    { DescriptorType::UNIFORM_BUFFER, ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SHADOWS        },
    { DescriptorType::UNIFORM_BUFFER,                            ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::LIGHTS         },
    { DescriptorType::UNIFORM_BUFFER,                            ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::RECORD_BUFFER  },
    { DescriptorType::UNIFORM_BUFFER,                            ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::FROXEL_BUFFER  },
    { DescriptorType::SAMPLER_2D_FLOAT,                          ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::STRUCTURE, DescriptorFlags::UNFILTERABLE },
    { DescriptorType::SAMPLER_2D_ARRAY_DEPTH,                    ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SHADOW_MAP     },
    { DescriptorType::SAMPLER_2D_FLOAT,                          ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::IBL_DFG_LUT    },
    { DescriptorType::SAMPLER_CUBE_FLOAT,                        ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::IBL_SPECULAR   },
    { DescriptorType::SAMPLER_2D_ARRAY_FLOAT,                    ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SSAO           },
    { DescriptorType::SAMPLER_2D_ARRAY_FLOAT,                    ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::SSR            },
    { DescriptorType::SAMPLER_CUBE_FLOAT,                        ShaderStageFlags::FRAGMENT,  +PerViewBindingPoints::FOG            },
};

static constexpr std::initializer_list<DescriptorSetLayoutBinding> perRenderableDescriptorSetLayoutList = {
    { DescriptorType::UNIFORM_BUFFER,           ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerRenderableBindingPoints::OBJECT_UNIFORMS, DescriptorFlags::DYNAMIC_OFFSET },
    { DescriptorType::UNIFORM_BUFFER,           ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerRenderableBindingPoints::BONES_UNIFORMS,  DescriptorFlags::DYNAMIC_OFFSET },
    { DescriptorType::UNIFORM_BUFFER,           ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT,  +PerRenderableBindingPoints::MORPHING_UNIFORMS         },
    { DescriptorType::SAMPLER_2D_ARRAY_FLOAT,   ShaderStageFlags::VERTEX                             ,  +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS, DescriptorFlags::UNFILTERABLE },
    { DescriptorType::SAMPLER_2D_ARRAY_INT,     ShaderStageFlags::VERTEX                             ,  +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS     },
    { DescriptorType::SAMPLER_2D_FLOAT,   ShaderStageFlags::VERTEX                                   ,  +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS, DescriptorFlags::UNFILTERABLE },
};

/**
 * 采样器类型和格式对的哈希函数
 * 用于unordered_map的哈希计算
 */
struct PairSamplerTypeFormatHasher {
    std::size_t operator()(const std::pair<SamplerType, SamplerFormat>& p) const {
        using UnderlyingSamplerType = std::underlying_type_t<SamplerType>;      // 采样器类型的底层类型
        using UnderlyingSamplerFormat = std::underlying_type_t<SamplerFormat>;  // 采样器格式的底层类型
        std::size_t seed = 0;
        // 计算第一个元素的哈希值
        std::size_t const hash1 = std::hash<UnderlyingSamplerType>{}(
            static_cast<UnderlyingSamplerType>(p.first)
        );
        // 计算第二个元素的哈希值
        std::size_t const hash2 = std::hash<UnderlyingSamplerFormat>{}(
            static_cast<UnderlyingSamplerFormat>(p.second)
        );
        // 使用boost::hash_combine风格的哈希组合算法（0x9e3779b9是黄金比例的倒数）
        seed ^= hash1 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// 采样器类型和格式到描述符类型的映射表
static const std::unordered_map<
    std::pair<SamplerType, SamplerFormat>, DescriptorType, PairSamplerTypeFormatHasher> sDescriptorTypeMap{
        {{ SamplerType::SAMPLER_2D, SamplerFormat::INT }, DescriptorType::SAMPLER_2D_INT },
        {{ SamplerType::SAMPLER_2D, SamplerFormat::UINT }, DescriptorType::SAMPLER_2D_UINT },
        {{ SamplerType::SAMPLER_2D, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_2D_FLOAT },
        {{ SamplerType::SAMPLER_2D, SamplerFormat::SHADOW }, DescriptorType::SAMPLER_2D_DEPTH },
        {{ SamplerType::SAMPLER_2D_ARRAY, SamplerFormat::INT }, DescriptorType::SAMPLER_2D_ARRAY_INT },
        {{ SamplerType::SAMPLER_2D_ARRAY, SamplerFormat::UINT }, DescriptorType::SAMPLER_2D_ARRAY_UINT },
        {{ SamplerType::SAMPLER_2D_ARRAY, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_2D_ARRAY_FLOAT },
        {{ SamplerType::SAMPLER_2D_ARRAY, SamplerFormat::SHADOW }, DescriptorType::SAMPLER_2D_ARRAY_DEPTH },
        {{ SamplerType::SAMPLER_CUBEMAP, SamplerFormat::INT }, DescriptorType::SAMPLER_CUBE_INT },
        {{ SamplerType::SAMPLER_CUBEMAP, SamplerFormat::UINT }, DescriptorType::SAMPLER_CUBE_UINT },
        {{ SamplerType::SAMPLER_CUBEMAP, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_CUBE_FLOAT },
        {{ SamplerType::SAMPLER_CUBEMAP, SamplerFormat::SHADOW }, DescriptorType::SAMPLER_CUBE_DEPTH },
        {{ SamplerType::SAMPLER_CUBEMAP_ARRAY, SamplerFormat::INT }, DescriptorType::SAMPLER_CUBE_ARRAY_INT },
        {{ SamplerType::SAMPLER_CUBEMAP_ARRAY, SamplerFormat::UINT }, DescriptorType::SAMPLER_CUBE_ARRAY_UINT },
        {{ SamplerType::SAMPLER_CUBEMAP_ARRAY, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT },
        {{ SamplerType::SAMPLER_CUBEMAP_ARRAY, SamplerFormat::SHADOW }, DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH },
        {{ SamplerType::SAMPLER_3D, SamplerFormat::INT }, DescriptorType::SAMPLER_3D_INT },
        {{ SamplerType::SAMPLER_3D, SamplerFormat::UINT }, DescriptorType::SAMPLER_3D_UINT },
        {{ SamplerType::SAMPLER_3D, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_3D_FLOAT },
        {{ SamplerType::SAMPLER_EXTERNAL, SamplerFormat::FLOAT }, DescriptorType::SAMPLER_EXTERNAL }
};

// used to generate shadow-maps - 用于生成阴影贴图的描述符集布局
static DescriptorSetLayout const depthVariantDescriptorSetLayout{
    utils::StaticString("depthVariant"), depthVariantDescriptorSetLayoutList
};

// SSR变体的描述符集布局
static DescriptorSetLayout const ssrVariantDescriptorSetLayout{ utils::StaticString("ssrVariant"),
    ssrVariantDescriptorSetLayoutList };

/**
 * Used for generating the color pass (i.e. the main pass). This is in fact a template that gets - 用于生成颜色通道（即主通道）的描述符集布局。这实际上是一个模板，根据变体展开为8种不同的布局
 * declined into 8 different layouts, based on variants. - 
 */
static DescriptorSetLayout perViewDescriptorSetLayout = { utils::StaticString("perView"),
    perViewDescriptorSetLayoutList };

// 每个可渲染对象的描述符集布局
static DescriptorSetLayout perRenderableDescriptorSetLayout = {
    utils::StaticString("perRenderable"), perRenderableDescriptorSetLayoutList
};

// 获取深度变体的描述符集布局
DescriptorSetLayout const& getDepthVariantLayout() noexcept {
    return depthVariantDescriptorSetLayout;
}

// 获取SSR变体的描述符集布局
DescriptorSetLayout const& getSsrVariantLayout() noexcept {
    return ssrVariantDescriptorSetLayout;
}

// 获取每个可渲染对象的描述符集布局
DescriptorSetLayout const& getPerRenderableLayout() noexcept {
    return perRenderableDescriptorSetLayout;
}

// 获取描述符名称（根据描述符集和绑定索引）
utils::CString getDescriptorName(DescriptorSetBindingPoints const set,
        descriptor_binding_t const binding) noexcept {
    using namespace std::literals;

    // 每视图描述符集的绑定名称映射表
    static std::unordered_map<descriptor_binding_t, std::string_view> const set0{{
        { +PerViewBindingPoints::FRAME_UNIFORMS, "FrameUniforms"sv },
        { +PerViewBindingPoints::SHADOWS,        "ShadowUniforms"sv },
        { +PerViewBindingPoints::LIGHTS,         "LightsUniforms"sv },
        { +PerViewBindingPoints::RECORD_BUFFER,  "FroxelRecordUniforms"sv },
        { +PerViewBindingPoints::FROXEL_BUFFER,  "FroxelsUniforms"sv },
        { +PerViewBindingPoints::STRUCTURE,      "sampler0_structure"sv },
        { +PerViewBindingPoints::SHADOW_MAP,     "sampler0_shadowMap"sv },
        { +PerViewBindingPoints::IBL_DFG_LUT,    "sampler0_iblDFG"sv },
        { +PerViewBindingPoints::IBL_SPECULAR,   "sampler0_iblSpecular"sv },
        { +PerViewBindingPoints::SSAO,           "sampler0_ssao"sv },
        { +PerViewBindingPoints::SSR,            "sampler0_ssr"sv },
        { +PerViewBindingPoints::FOG,            "sampler0_fog"sv },
    }};

    // 每个可渲染对象描述符集的绑定名称映射表
    static std::unordered_map<descriptor_binding_t, std::string_view> const set1{{
        { +PerRenderableBindingPoints::OBJECT_UNIFORMS,             "ObjectUniforms"sv },
        { +PerRenderableBindingPoints::BONES_UNIFORMS,              "BonesUniforms"sv },
        { +PerRenderableBindingPoints::MORPHING_UNIFORMS,           "MorphingUniforms"sv },
        { +PerRenderableBindingPoints::MORPH_TARGET_POSITIONS,      "sampler1_positions"sv },
        { +PerRenderableBindingPoints::MORPH_TARGET_TANGENTS,       "sampler1_tangents"sv },
        { +PerRenderableBindingPoints::BONES_INDICES_AND_WEIGHTS,   "sampler1_indicesAndWeights"sv },
    }};

    // 根据描述符集类型查找对应的绑定名称
    switch (set) {
        case DescriptorSetBindingPoints::PER_VIEW: {
            // 在每视图描述符集中查找
            auto const pos = set0.find(binding);
            assert_invariant(pos != set0.end());
            return { pos->second.data(), pos->second.size() };
        }
        case DescriptorSetBindingPoints::PER_RENDERABLE: {
            // 在每个可渲染对象描述符集中查找
            auto const pos = set1.find(binding);
            assert_invariant(pos != set1.end());
            return { pos->second.data(), pos->second.size() };
        }
        case DescriptorSetBindingPoints::PER_MATERIAL: {
            // 每个材质描述符集只有一个绑定（MaterialParams）
            assert_invariant(binding < 1);
            return "MaterialParams";
        }
    }
    return "Unknown";  // 未知描述符集类型
}

// 获取每视图描述符集布局（根据材质域、光照、SSR、雾效、VSM等条件过滤）
DescriptorSetLayout getPerViewDescriptorSetLayout(
        MaterialDomain const domain,
        bool const isLit, bool const isSSR, bool const hasFog,
        bool const isVSM) noexcept {

    switch (domain) {
        case MaterialDomain::SURFACE: {
            //
            // CAVEAT: The logic here must match MaterialBuilder::checkMaterialLevelFeatures() - 警告：此处的逻辑必须与MaterialBuilder::checkMaterialLevelFeatures()匹配
            //
            // 从基础布局开始
            auto layout = perViewDescriptorSetLayout;
            // remove descriptors not needed for unlit materials - 移除无光照材质不需要的描述符
            if (!isLit) {
                // 移除IBL相关的描述符（DFG LUT和镜面反射）
                layout.bindings.erase(
                        std::remove_if(layout.bindings.begin(), layout.bindings.end(),
                                [](auto const& entry) {
                                    return  entry.binding == PerViewBindingPoints::IBL_DFG_LUT ||
                                            entry.binding == PerViewBindingPoints::IBL_SPECULAR;
                                }),
                        layout.bindings.end());
            }
            // remove descriptors not needed for SSRs - 移除SSR不需要的描述符
            if (!isSSR) {
                // 移除SSR描述符
                layout.bindings.erase(
                        std::remove_if(layout.bindings.begin(), layout.bindings.end(),
                                [](auto const& entry) {
                                    return entry.binding == PerViewBindingPoints::SSR;
                                }),
                        layout.bindings.end());

            }
            // remove fog descriptor if filtered out - 如果过滤掉雾效，移除雾效描述符
            if (!hasFog) {
                // 移除雾效描述符
                layout.bindings.erase(
                        std::remove_if(layout.bindings.begin(), layout.bindings.end(),
                                [](auto const& entry) {
                                    return entry.binding == PerViewBindingPoints::FOG;
                                }),
                        layout.bindings.end());
            }

            // change the SHADOW_MAP descriptor type for VSM - 为VSM更改SHADOW_MAP描述符类型
            if (isVSM) {
                // 查找SHADOW_MAP绑定并更改其类型为FLOAT（VSM使用FLOAT纹理）
                auto const pos = std::find_if(layout.bindings.begin(), layout.bindings.end(),
                        [](auto const& v) {
                            return v.binding == PerViewBindingPoints::SHADOW_MAP;
                        });
                if (pos != layout.bindings.end()) {
                    pos->type = DescriptorType::SAMPLER_2D_ARRAY_FLOAT;
                }
            }
            return layout;
        }
        case MaterialDomain::POST_PROCESS:
            // 后处理使用深度变体布局
            return depthVariantDescriptorSetLayout;
        case MaterialDomain::COMPUTE:
            // TODO: what's the layout for compute? - 计算着色器的布局是什么？
            return depthVariantDescriptorSetLayout;
    }
}

// 根据变体获取每视图描述符集布局
DescriptorSetLayout getPerViewDescriptorSetLayoutWithVariant(
        Variant const variant,
        MaterialDomain const domain,
        bool const isLit, bool const isSSR, bool const hasFog) noexcept {
    // 如果是深度变体，返回深度变体布局
    if (Variant::isValidDepthVariant(variant)) {
        return depthVariantDescriptorSetLayout;
    }
    // 如果是SSR变体，返回SSR变体布局
    if (Variant::isSSRVariant(variant)) {
        return ssrVariantDescriptorSetLayout;
    }
    // We need to filter out all the descriptors not included in the "resolved" layout below - 我们需要过滤掉下面"解析"布局中不包含的所有描述符
    // 根据变体是否为VSM变体来获取布局
    return getPerViewDescriptorSetLayout(domain, isLit, isSSR, hasFog,
            Variant::isVSMVariant(variant));
}

// 根据采样器类型和格式获取描述符类型
DescriptorType getDescriptorType(SamplerType const type, SamplerFormat const format) {
    // 在映射表中查找对应的描述符类型
    auto const pos = sDescriptorTypeMap.find({ type, format });
    // 如果未找到，抛出异常（类型和格式不兼容）
    FILAMENT_CHECK_PRECONDITION(pos != sDescriptorTypeMap.end())
            << "Incompatible Sampler Format " << to_string(format)
            << " and Type " << to_string(type);
    return pos->second;
}

/**
 * 简单的find_if实现（用于编译时检查）
 * @tparam ITERATOR 迭代器类型
 * @tparam PREDICATE 谓词类型
 * @param first 起始迭代器
 * @param last 结束迭代器
 * @param pred 谓词函数
 * @return 找到的迭代器，如果未找到则返回last
 */
template<class ITERATOR, class PREDICATE>
constexpr static ITERATOR find_if(ITERATOR first, ITERATOR last, PREDICATE pred) {
    for (; first != last; ++first)
        if (pred(*first)) break;
    return first;
}

/**
 * 编译时一致性检查
 * 检查perViewDescriptorSetLayout中所有应用于顶点阶段的描述符
 * 是否都出现在ssrVariantDescriptorSetLayout中，这意味着后者与前者兼容
 */
constexpr static bool checkConsistency() noexcept {
    // check that all descriptors that apply to the vertex stage in perViewDescriptorSetLayout - 检查perViewDescriptorSetLayout中所有应用于顶点阶段的描述符
    // are present in ssrVariantDescriptorSetLayout; meaning that the latter is compatible - 是否都出现在ssrVariantDescriptorSetLayout中，这意味着后者兼容
    // with the former. - 前者
    for (auto const& r: perViewDescriptorSetLayoutList) {
        if (hasShaderType(r.stageFlags, ShaderStage::VERTEX)) {
            auto const pos = find_if(
                    ssrVariantDescriptorSetLayoutList.begin(),
                    ssrVariantDescriptorSetLayoutList.end(),
                    [r](auto const& l) {
                        return l.count == r.count &&
                               l.type == r.type &&
                               l.binding == r.binding &&
                               l.flags == r.flags &&
                               l.stageFlags == r.stageFlags;
                    });
            if (pos == ssrVariantDescriptorSetLayoutList.end()) {
                return false;
            }
        }
    }
    return true;
}

static_assert(checkConsistency(), "ssrVariantDescriptorSetLayout is not compatible with "
        "perViewDescriptorSetLayout");

} // namespace filament::descriptor_sets
