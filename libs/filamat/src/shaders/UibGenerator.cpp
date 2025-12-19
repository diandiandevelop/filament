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

#include "UibGenerator.h"
#include "private/filament/UibStructs.h"

#include "private/filament/BufferInterfaceBlock.h"

#include <private/filament/EngineEnums.h>
#include <backend/DriverEnums.h>

#include <utils/debug.h>

#include <stdlib.h>

namespace filament {

using namespace backend;

// 返回给定UBO标签的BufferInterfaceBlock
// @param ubo UBO标签枚举值
// @return 对应的BufferInterfaceBlock常量引用
// @note MaterialParams不应该通过此方法获取，应使用材质特定的方法
BufferInterfaceBlock const& UibGenerator::get(UibGenerator::Ubo ubo) noexcept {
    // MaterialParams不应该通过此方法获取
    assert_invariant(ubo != Ubo::MaterialParams);
    // 根据UBO标签返回相应的BufferInterfaceBlock
    switch (ubo) {
        case Ubo::FrameUniforms:
            return getPerViewUib();              // 每视图UBO
        case Ubo::ObjectUniforms:
            return getPerRenderableUib();        // 每个可渲染对象UBO
        case Ubo::BonesUniforms:
            return getPerRenderableBonesUib();   // 骨骼UBO
        case Ubo::MorphingUniforms:
            return getPerRenderableMorphingUib(); // 变形UBO
        case Ubo::LightsUniforms:
            return getLightsUib();               // 光源UBO
        case Ubo::ShadowUniforms:
            return getShadowUib();               // 阴影UBO
        case Ubo::FroxelRecordUniforms:
            return getFroxelRecordUib();         // Froxel记录UBO
        case Ubo::FroxelsUniforms:
            return getFroxelsUib();              // Froxel UBO
        case Ubo::MaterialParams:
            abort();  // MaterialParams不通过此方法获取
    }
}

// 返回给定UBO标签的绑定信息（描述符集和绑定点）
// @param ubo UBO标签枚举值
// @return 绑定信息结构，包含描述符集和绑定点
UibGenerator::Binding UibGenerator::getBinding(UibGenerator::Ubo ubo) noexcept {
    // 根据UBO标签返回相应的绑定信息
    switch (ubo) {
        case Ubo::FrameUniforms:
            return { +DescriptorSetBindingPoints::PER_VIEW,
                     +PerViewBindingPoints::FRAME_UNIFORMS };
        case Ubo::ObjectUniforms:
            return { +DescriptorSetBindingPoints::PER_RENDERABLE,
                     +PerRenderableBindingPoints::OBJECT_UNIFORMS };
        case Ubo::BonesUniforms:
            return { +DescriptorSetBindingPoints::PER_RENDERABLE,
                     +PerRenderableBindingPoints::BONES_UNIFORMS };
        case Ubo::MorphingUniforms:
            return { +DescriptorSetBindingPoints::PER_RENDERABLE,
                     +PerRenderableBindingPoints::MORPHING_UNIFORMS };
        case Ubo::LightsUniforms:
            return { +DescriptorSetBindingPoints::PER_VIEW,
                     +PerViewBindingPoints::LIGHTS };
        case Ubo::ShadowUniforms:
            return { +DescriptorSetBindingPoints::PER_VIEW,
                     +PerViewBindingPoints::SHADOWS };
        case Ubo::FroxelRecordUniforms:
            return { +DescriptorSetBindingPoints::PER_VIEW,
                     +PerViewBindingPoints::RECORD_BUFFER };
        case Ubo::FroxelsUniforms:
            return { +DescriptorSetBindingPoints::PER_VIEW,
                     +PerViewBindingPoints::FROXEL_BUFFER };
        case Ubo::MaterialParams:
            return { +DescriptorSetBindingPoints::PER_MATERIAL,
                     +PerMaterialBindingPoints::MATERIAL_PARAMS };
    }
}

// 静态断言：确保CONFIG_MAX_SHADOW_CASCADES为4，因为改变它会影响PerView大小并破坏材质
static_assert(CONFIG_MAX_SHADOW_CASCADES == 4,
        "Changing CONFIG_MAX_SHADOW_CASCADES affects PerView size and breaks materials.");

// 获取每视图UBO（包含视图相关的统一变量）
// @return 每视图BufferInterfaceBlock常量引用
// 包含：变换矩阵、时间、分辨率、光照参数、阴影参数、雾效参数、SSR参数等
BufferInterfaceBlock const& UibGenerator::getPerViewUib() noexcept  {
    using Type = BufferInterfaceBlock::Type;

    // 构建每视图UBO，包含所有视图相关的统一变量
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(PerViewUib::_name)
            .add({
            // 变换矩阵（视图、世界、裁剪空间之间的变换）
            { "viewFromWorldMatrix",    0, Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "worldFromViewMatrix",    0, Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "clipFromViewMatrix",     0, Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "viewFromClipMatrix",     0, Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "eyeFromViewMatrix",      CONFIG_MAX_STEREOSCOPIC_EYES,
                                           Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "clipFromWorldMatrix",    CONFIG_MAX_STEREOSCOPIC_EYES,
                                           Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "worldFromClipMatrix",    0, Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "userWorldFromWorldMatrix",0,Type::MAT4,   Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "clipTransform",          0, Type::FLOAT4, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },

            // 裁剪控制和时间相关参数
            { "clipControl",            0, Type::FLOAT2, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "time",                   0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "temporalNoise",          0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "userTime",               0, Type::FLOAT4, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },

            // ------------------------------------------------------------------------------------
            // values below should only be accessed in surface materials
            // 以下值应仅在表面材质中访问
            // ------------------------------------------------------------------------------------

            // 分辨率和视口相关参数
            { "resolution",             0, Type::FLOAT4, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "logicalViewportScale",   0, Type::FLOAT2, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "logicalViewportOffset",  0, Type::FLOAT2, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },

            // LOD和导数相关参数
            { "lodBias",                0, Type::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "refractionLodOffset",    0, Type::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "derivativesScale",       0, Type::FLOAT2                  },

            // 相机和曝光相关参数
            { "oneOverFarMinusNear",    0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "nearOverFarMinusNear",   0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "cameraFar",              0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "exposure",               0, Type::FLOAT,  Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 }, // high precision to work around #3602 (qualcom),
            { "ev100",                  0, Type::FLOAT,  Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "needsAlphaChannel",      0, Type::FLOAT,  Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },

            // AO（环境光遮蔽）相关参数
            { "aoSamplingQualityAndEdgeDistance", 0, Type::FLOAT         },
            { "aoBentNormals",          0, Type::FLOAT                   },

            // ------------------------------------------------------------------------------------
            // Dynamic Lighting [variant: DYN]
            // 动态光照相关参数（变体：DYN）
            // ------------------------------------------------------------------------------------
            // Froxel相关参数
            { "zParams",                0, Type::FLOAT4                  },
            { "fParams",                0, Type::UINT3                   },
            { "lightChannels",          0, Type::INT                     },
            { "froxelCountXY",          0, Type::FLOAT2                  },
            { "enableFroxelViz",        0, Type::INT                     },
            { "dynReserved0",           0, Type::INT                     },
            { "dynReserved1",           0, Type::INT                     },
            { "dynReserved2",           0, Type::INT                     },

            // IBL（基于图像的光照）相关参数
            { "iblLuminance",           0, Type::FLOAT,  Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "iblRoughnessOneLevel",   0, Type::FLOAT,  Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "iblSH",                  9, Type::FLOAT3                  },

            // ------------------------------------------------------------------------------------
            // Directional Lighting [variant: DIR]
            // 方向光相关参数（变体：DIR）
            // ------------------------------------------------------------------------------------
            { "lightDirection",         0, Type::FLOAT3, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "padding0",               0, Type::FLOAT                   },
            { "lightColorIntensity",    0, Type::FLOAT4, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "sun",                    0, Type::FLOAT4, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "shadowFarAttenuationParams", 0, Type::FLOAT2, Precision::HIGH },

            // ------------------------------------------------------------------------------------
            // Directional light shadowing [variant: SRE | DIR]
            // 方向光阴影相关参数（变体：SRE | DIR）
            // ------------------------------------------------------------------------------------
            { "directionalShadows",       0, Type::INT                      },
            { "ssContactShadowDistance",  0, Type::FLOAT                    },

            // 级联阴影相关参数
            { "cascadeSplits",             0, Type::FLOAT4, Precision::HIGH },
            { "cascades",                  0, Type::INT                     },
            { "shadowPenumbraRatioScale",  0, Type::FLOAT                   },
            { "lightFarAttenuationParams", 0, Type::FLOAT2, Precision::HIGH },

            // ------------------------------------------------------------------------------------
            // VSM shadows [variant: VSM]
            // VSM（方差阴影贴图）阴影相关参数（变体：VSM）
            // ------------------------------------------------------------------------------------
            { "vsmExponent",             0, Type::FLOAT                  },
            { "vsmDepthScale",           0, Type::FLOAT                  },
            { "vsmLightBleedReduction",  0, Type::FLOAT                  },
            { "shadowSamplingType",      0, Type::UINT                   },

            // ------------------------------------------------------------------------------------
            // Fog [variant: FOG]
            // 雾效相关参数（变体：FOG）
            // ------------------------------------------------------------------------------------
            { "fogDensity",              0, Type::FLOAT3,Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogStart",                0, Type::FLOAT, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogMaxOpacity",           0, Type::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogMinMaxMip",            0, Type::UINT,  Precision::HIGH },
            { "fogHeightFalloff",        0, Type::FLOAT, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogCutOffDistance",       0, Type::FLOAT, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogColor",                0, Type::FLOAT3, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogColorFromIbl",         0, Type::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogInscatteringStart",    0, Type::FLOAT, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogInscatteringSize",     0, Type::FLOAT, Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogOneOverFarMinusNear",  0, Type::FLOAT, Precision::HIGH },
            { "fogNearOverFarMinusNear", 0, Type::FLOAT, Precision::HIGH },
            { "fogFromWorldMatrix",      0, Type::MAT3, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogLinearParams",         0, Type::FLOAT2, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },
            { "fogReserved0",            0, Type::FLOAT2, Precision::HIGH },

            // ------------------------------------------------------------------------------------
            // Screen-space reflections [variant: SSR (i.e.: VSM | SRE)]
            // 屏幕空间反射相关参数（变体：SSR，即VSM | SRE）
            // ------------------------------------------------------------------------------------
            { "ssrReprojection",         0, Type::MAT4,  Precision::HIGH },
            { "ssrUvFromViewMatrix",     0, Type::MAT4,  Precision::HIGH },
            { "ssrThickness",            0, Type::FLOAT                  },
            { "ssrBias",                 0, Type::FLOAT                  },
            { "ssrDistance",             0, Type::FLOAT                  },
            { "ssrStride",               0, Type::FLOAT                  },

            // --------------------------------------------------------------------------------------------
            // user defined global variables
            // 用户定义的全局变量
            // --------------------------------------------------------------------------------------------
            { "custom",                  4, Type::FLOAT4, Precision::HIGH, FeatureLevel::FEATURE_LEVEL_0 },

            // --------------------------------------------------------------------------------------------
            // for feature level 0 / es2 usage
            // 用于功能级别0 / ES2使用
            // --------------------------------------------------------------------------------------------
            { "rec709",                  0, Type::INT,  Precision::DEFAULT, FeatureLevel::FEATURE_LEVEL_0 },
            { "es2Reserved0",            0, Type::FLOAT                  },
            { "es2Reserved1",            0, Type::FLOAT                  },
            { "es2Reserved2",            0, Type::FLOAT                  },

            // bring PerViewUib to 2 KiB
            // 将PerViewUib填充到2 KiB
            { "reserved", sizeof(PerViewUib::reserved)/16, Type::FLOAT4 }
            })
            .build();

    return uib;
}

// 获取每个可渲染对象的UBO（包含每个可渲染对象的统一变量，如模型矩阵等）
// @return 每个可渲染对象的BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getPerRenderableUib() noexcept {
    static BufferInterfaceBlock const uib =  BufferInterfaceBlock::Builder()
            .name(PerRenderableUib::_name)
            // 添加结构体字段，支持多个实例
            .add({{ "data", CONFIG_MAX_INSTANCES, BufferInterfaceBlock::Type::STRUCT, {}, {},
                    "PerRenderableData", sizeof(PerRenderableData), "CONFIG_MAX_INSTANCES" }})
            .build();
    return uib;
}

// 获取光源UBO（包含光源数据数组）
// @return 光源BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getLightsUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(LightsUib::_name)
            // 添加光源数组（每个光源是一个MAT4）
            .add({{ "lights", CONFIG_MAX_LIGHT_COUNT,
                    BufferInterfaceBlock::Type::MAT4, Precision::HIGH }})
            .build();
    return uib;
}

// 获取阴影UBO（包含点光源阴影数据）
// @return 阴影BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getShadowUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(ShadowUib::_name)
            // 添加阴影数据数组（每个阴影是一个ShadowData结构体）
            .add({{ "shadows", CONFIG_MAX_SHADOWMAPS,
                    BufferInterfaceBlock::Type::STRUCT, {}, {},
                    "ShadowData", sizeof(ShadowUib::ShadowData) }})
            .build();
    return uib;
}

// 获取每个可渲染对象的骨骼UBO（包含骨骼数据，每个可渲染对象）
// @return 骨骼BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getPerRenderableBonesUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(PerRenderableBoneUib::_name)
            // 添加骨骼数据数组（每个骨骼是一个BoneData结构体）
            .add({{ "bones", CONFIG_MAX_BONE_COUNT,
                    BufferInterfaceBlock::Type::STRUCT, {}, {},
                    "BoneData", sizeof(PerRenderableBoneUib::BoneData) }})
            .build();
    return uib;
}

// 获取每个可渲染对象的变形UBO（包含变形权重，每个渲染基元更新）
// @return 变形BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getPerRenderableMorphingUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(PerRenderableMorphingUib::_name)
            // 添加变形权重数组（每个权重是一个FLOAT4）
            .add({{ "weights", CONFIG_MAX_MORPH_TARGET_COUNT,
                    BufferInterfaceBlock::Type::FLOAT4 }})
            .build();
    return uib;
}

// 获取Froxel记录UBO（包含Froxel记录数据）
// @return Froxel记录BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getFroxelRecordUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(FroxelRecordUib::_name)
            // 添加Froxel记录数组（每个记录是一个UINT4）
            .add({{ "records", 1024, BufferInterfaceBlock::Type::UINT4, Precision::HIGH, {},
                    {}, {}, "CONFIG_FROXEL_RECORD_BUFFER_HEIGHT"}})
            .build();
    return uib;
}

// 获取Froxel UBO（包含Froxel数据）
// @return Froxel BufferInterfaceBlock引用
BufferInterfaceBlock const& UibGenerator::getFroxelsUib() noexcept {
    static BufferInterfaceBlock const uib = BufferInterfaceBlock::Builder()
            .name(FroxelsUib::_name)
            // 添加Froxel记录数组（每个记录是一个UINT4）
            .add({{ "records", 1024, BufferInterfaceBlock::Type::UINT4, Precision::HIGH, {},
                    {}, {}, "CONFIG_FROXEL_BUFFER_HEIGHT"}})
            .build();
    return uib;
}

} // namespace filament
