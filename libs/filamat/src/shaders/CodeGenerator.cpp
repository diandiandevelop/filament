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

#include "CodeGenerator.h"

#include "MaterialInfo.h"
#include "../PushConstantDefinitions.h"

#include "generated/shaders.h"

#include <backend/DriverEnums.h>

#include <utils/sstream.h>

#include <cctype>
#include <iomanip>

#include <assert.h>

namespace filamat {

// From driverEnum namespace
using namespace filament;
using namespace backend;
using namespace utils;

// 生成分隔符（换行符）
// @param out 输出流
// @return 输出流引用
io::sstream& CodeGenerator::generateSeparator(io::sstream& out) {
    out << '\n';
    return out;
}

// 生成着色器的公共序言（版本号、扩展、定义等）
// @param out 输出流
// @param stage 着色器阶段
// @param material 材质信息
// @param v 着色器变体
// @return 输出流引用
utils::io::sstream& CodeGenerator::generateCommonProlog(utils::io::sstream& out, ShaderStage stage,
        MaterialInfo const& material, filament::Variant v) const {
    switch (mShaderModel) {
        case ShaderModel::MOBILE:
            // Vulkan requires version 310 or higher
            if (mTargetLanguage == TargetLanguage::SPIRV ||
                mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_2) {
                // Vulkan requires layout locations on ins and outs, which were not supported
                // in ESSL 300
                out << "#version 310 es\n\n";
            } else {
                if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
                    out << "#version 300 es\n\n";
                } else {
                    out << "#version 100\n\n";
                }
            }
            if (material.hasExternalSamplers) {
                if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
                    out << "#extension GL_OES_EGL_image_external_essl3 : require\n\n";
                } else {
                    out << "#extension GL_OES_EGL_image_external : require\n\n";
                }
            }
            if (v.hasStereo() && stage == ShaderStage::VERTEX) {
                switch (material.stereoscopicType) {
                case StereoscopicType::INSTANCED:
                    // If we're not processing the shader through glslang (in the case of unoptimized
                    // OpenGL shaders), then we need to add the #extension string ourselves.
                    // If we ARE running the shader through glslang, then we must not include it,
                    // otherwise glslang will complain.
                    out << "#ifndef FILAMENT_GLSLANG\n";
                    out << "#extension GL_EXT_clip_cull_distance : require\n";
                    out << "#endif\n\n";
                    break;
                case StereoscopicType::MULTIVIEW:
                    if (mTargetApi == TargetApi::VULKAN) {
                        out << "#extension GL_EXT_multiview : enable\n";
                    } else {
                        out << "#extension GL_OVR_multiview2 : require\n";
                    }
                    break;
                case StereoscopicType::NONE:
                    break;
                }
            }
            break;
        case ShaderModel::DESKTOP:
            if (mTargetLanguage == TargetLanguage::SPIRV ||
                mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_2) {
                // Vulkan requires binding specifiers on uniforms and samplers, which were not
                // supported in the OpenGL 4.1 GLSL profile.
                out << "#version 450 core\n\n";
            } else {
                out << "#version 410 core\n\n";
                out << "#extension GL_ARB_shading_language_packing : enable\n\n";
            }
            if (v.hasStereo() && stage == ShaderStage::VERTEX) {
                switch (material.stereoscopicType) {
                case StereoscopicType::INSTANCED:
                    // Nothing to generate
                    break;
                case StereoscopicType::MULTIVIEW:
                    if (mTargetApi == TargetApi::VULKAN) {
                        out << "#extension GL_EXT_multiview : enable\n";
                    } else {
                        out << "#extension GL_OVR_multiview2 : require\n";
                    }
                    break;
                case StereoscopicType::NONE:
                    break;
                }
            }
            break;
    }

    if (mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        out << "#extension GL_OES_standard_derivatives : require\n\n";
    }

    // This allows our includer system to use the #line directive to denote the source file for
    // #included code. This way, glslang reports errors more accurately.
    out << "#extension GL_GOOGLE_cpp_style_line_directive : enable\n\n";

    if (v.hasStereo() && stage == ShaderStage::VERTEX) {
        switch (material.stereoscopicType) {
        case StereoscopicType::INSTANCED:
            // Nothing to generate
            break;
        case StereoscopicType::MULTIVIEW:
            if (mTargetApi != TargetApi::VULKAN) {
                out << "layout(num_views = " << material.stereoscopicEyeCount << ") in;\n";
            }
            break;
        case StereoscopicType::NONE:
            break;
        }
    }

    if (stage == ShaderStage::COMPUTE) {
        out << "layout(local_size_x = " << material.groupSize.x
            << ", local_size_y = " <<  material.groupSize.y
            << ", local_size_z = " <<  material.groupSize.z
            << ") in;\n\n";
    }

    switch (mShaderModel) {
        case ShaderModel::MOBILE:
            out << "#define TARGET_MOBILE\n";
            break;
        case ShaderModel::DESKTOP:
            break;
    }

    switch (mTargetApi) {
        case TargetApi::OPENGL:
            switch (mShaderModel) {
                case ShaderModel::MOBILE:
                    out << "#define TARGET_GLES_ENVIRONMENT\n";
                    break;
                case ShaderModel::DESKTOP:
                    out << "#define TARGET_GL_ENVIRONMENT\n";
                    break;
            }
            break;
        case TargetApi::VULKAN:
            out << "#define TARGET_VULKAN_ENVIRONMENT\n";
            break;
        case TargetApi::METAL:
            out << "#define TARGET_METAL_ENVIRONMENT\n";
            break;
        case TargetApi::WEBGPU:
            out << "#define TARGET_WEBGPU_ENVIRONMENT\n";
            break;
        case TargetApi::ALL:
            // invalid should never happen
            break;
    }

    switch (mTargetLanguage) {
        case TargetLanguage::GLSL:
            out << "#define FILAMENT_OPENGL_SEMANTICS\n";
            break;
        case TargetLanguage::SPIRV:
            out << "#define FILAMENT_VULKAN_SEMANTICS\n";
            break;
    }

    if (mTargetApi == TargetApi::VULKAN ||
        mTargetApi == TargetApi::WEBGPU ||
        mTargetApi == TargetApi::METAL ||
        (mTargetApi == TargetApi::OPENGL && mShaderModel == ShaderModel::DESKTOP) ||
        mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_2) {
        out << "#define FILAMENT_HAS_FEATURE_TEXTURE_GATHER\n";
    }

    if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
        out << "#define FILAMENT_HAS_FEATURE_INSTANCING\n";
    }

    // During compilation and optimization, __VERSION__ reflects the shader language version of the
    // intermediate code, not the version of the final code. spirv-cross automatically adapts
    // certain language features (e.g. fragment output) but leaves others untouched (e.g. sampler
    // functions, bit shift operations). Client code may have to make decisions based on this
    // information, so define a FILAMENT_EFFECTIVE_VERSION constant.
    const char *effective_version;
    if (mTargetLanguage == TargetLanguage::GLSL) {
        effective_version = "__VERSION__";
    } else {
        switch (mShaderModel) {
            case ShaderModel::MOBILE:
                if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
                    effective_version = "300";
                } else {
                    effective_version = "100";
                }
                break;
            case ShaderModel::DESKTOP:
                if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_2) {
                    effective_version = "450";
                } else {
                    effective_version = "410";
                }
                break;
            default:
                assert(false);
        }
    }
    generateDefine(out, "FILAMENT_EFFECTIVE_VERSION", effective_version);

    switch (material.stereoscopicType) {
    case StereoscopicType::INSTANCED:
        generateDefine(out, "FILAMENT_STEREO_INSTANCED", true);
        break;
    case StereoscopicType::MULTIVIEW:
        generateDefine(out, "FILAMENT_STEREO_MULTIVIEW", true);
        break;
    case StereoscopicType::NONE:
        break;
    }

    if (stage == ShaderStage::VERTEX) {
        generateDefine(out, "FLIP_UV_ATTRIBUTE", material.flipUV);
        generateDefine(out, "LEGACY_MORPHING", material.useLegacyMorphing);
    }
    if (stage == ShaderStage::FRAGMENT) {
        generateDefine(out, "FILAMENT_LINEAR_FOG", material.linearFog);
        generateDefine(out, "FILAMENT_SHADOW_FAR_ATTENUATION", material.shadowFarAttenuation);
        generateDefine(out, "MATERIAL_HAS_CUSTOM_DEPTH", material.userMaterialHasCustomDepth);
    }

    if (mTargetLanguage == TargetLanguage::SPIRV ||
        mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
        if (stage == ShaderStage::VERTEX) {
            generateDefine(out, "VARYING", "out");
            generateDefine(out, "ATTRIBUTE", "in");
        } else if (stage == ShaderStage::FRAGMENT) {
            generateDefine(out, "VARYING", "in");
        }
    } else {
        generateDefine(out, "VARYING", "varying");
        generateDefine(out, "ATTRIBUTE", "attribute");
    }

    auto getShadingDefine = [](Shading shading) -> const char* {
        switch (shading) {
            case Shading::LIT:                 return "SHADING_MODEL_LIT";
            case Shading::UNLIT:               return "SHADING_MODEL_UNLIT";
            case Shading::SUBSURFACE:          return "SHADING_MODEL_SUBSURFACE";
            case Shading::CLOTH:               return "SHADING_MODEL_CLOTH";
            case Shading::SPECULAR_GLOSSINESS: return "SHADING_MODEL_SPECULAR_GLOSSINESS";
        }
    };

    generateDefine(out, getShadingDefine(material.shading), true);

    generateQualityDefine(out, material.quality);

    // precision qualifiers
    // 精度限定符
    out << '\n';
    Precision const defaultPrecision = getDefaultPrecision(stage);
    const char* precision = getPrecisionQualifier(defaultPrecision);
    out << "precision " << precision << " float;\n";
    out << "precision " << precision << " int;\n";
    if (mShaderModel == ShaderModel::MOBILE) {
        if (mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
            out << "precision lowp sampler2DArray;\n";
        }
        if (material.has3dSamplers) {
            out << "precision lowp sampler3D;\n";
        }
    }

    // Filament-reserved specification constants (limited by CONFIG_MAX_RESERVED_SPEC_CONSTANTS)
    // Filament保留的特化常量（受CONFIG_MAX_RESERVED_SPEC_CONSTANTS限制）
    out << '\n';
    generateSpecializationConstant(out, "BACKEND_FEATURE_LEVEL",
            +ReservedSpecializationConstants::BACKEND_FEATURE_LEVEL, 1);

    if (mTargetApi == TargetApi::WEBGPU) {
        // Note: This is a revived hack for a hack.
        // 注意：这是一个变通方法的变通方法。
        //
        // WGSL doesn't support specialization constants as an array length
        // CONFIG_MAX_INSTANCES is only needed for WebGL, so we can replace it with a constant.
        // More information at https://github.com/gpuweb/gpuweb/issues/572#issuecomment-649760005
        // WGSL不支持将特化常量作为数组长度
        // CONFIG_MAX_INSTANCES仅用于WebGL，因此我们可以用常量替换它。
        // 更多信息请参见 https://github.com/gpuweb/gpuweb/issues/572#issuecomment-649760005
        out << "const int CONFIG_MAX_INSTANCES = " << (int)CONFIG_MAX_INSTANCES << ";\n";
        out << "const int CONFIG_FROXEL_BUFFER_HEIGHT = 2048;\n";
        out << "const int CONFIG_FROXEL_RECORD_BUFFER_HEIGHT = 16384;\n";
    } else {
        generateSpecializationConstant(out, "CONFIG_MAX_INSTANCES",
                +ReservedSpecializationConstants::CONFIG_MAX_INSTANCES, (int)CONFIG_MAX_INSTANCES);

        // the default of 1024 (16KiB) is needed for 32% of Android devices
        // 默认值1024（16KiB）是32%的Android设备所需的
        generateSpecializationConstant(out, "CONFIG_FROXEL_BUFFER_HEIGHT",
                +ReservedSpecializationConstants::CONFIG_FROXEL_BUFFER_HEIGHT, 1024);

        generateSpecializationConstant(out, "CONFIG_FROXEL_RECORD_BUFFER_HEIGHT",
                +ReservedSpecializationConstants::CONFIG_FROXEL_RECORD_BUFFER_HEIGHT, 16384);
    }

    // directional shadowmap visualization
    // 方向光阴影贴图可视化
    generateSpecializationConstant(out, "CONFIG_DEBUG_DIRECTIONAL_SHADOWMAP",
            +ReservedSpecializationConstants::CONFIG_DEBUG_DIRECTIONAL_SHADOWMAP, false);

    // froxel visualization
    // Froxel可视化
    generateSpecializationConstant(out, "CONFIG_DEBUG_FROXEL_VISUALIZATION",
            +ReservedSpecializationConstants::CONFIG_DEBUG_FROXEL_VISUALIZATION, false);

    // Workaround a Metal pipeline compilation error with the message:
    // "Could not statically determine the target of a texture". See surface_light_indirect.fs
    // 变通方法：解决Metal管道编译错误，错误消息为：
    // "无法静态确定纹理的目标"。参见 surface_light_indirect.fs
    generateSpecializationConstant(out, "CONFIG_STATIC_TEXTURE_TARGET_WORKAROUND",
            +ReservedSpecializationConstants::CONFIG_STATIC_TEXTURE_TARGET_WORKAROUND, false);

    generateSpecializationConstant(out, "CONFIG_POWER_VR_SHADER_WORKAROUNDS",
            +ReservedSpecializationConstants::CONFIG_POWER_VR_SHADER_WORKAROUNDS, false);

    generateSpecializationConstant(out, "CONFIG_STEREO_EYE_COUNT",
            +ReservedSpecializationConstants::CONFIG_STEREO_EYE_COUNT, material.stereoscopicEyeCount);

    generateSpecializationConstant(out, "CONFIG_SH_BANDS_COUNT",
            +ReservedSpecializationConstants::CONFIG_SH_BANDS_COUNT, 3);

    generateSpecializationConstant(out, "CONFIG_SHADOW_SAMPLING_METHOD",
            +ReservedSpecializationConstants::CONFIG_SHADOW_SAMPLING_METHOD, 1);

    // CONFIG_MAX_STEREOSCOPIC_EYES is used to size arrays and on Adreno GPUs + vulkan, this has to
    // be explicitly, statically defined (as in #define). Otherwise (using const int for
    // example), we'd run into a GPU crash.
    // CONFIG_MAX_STEREOSCOPIC_EYES用于确定数组大小，在Adreno GPU + Vulkan上，这必须
    // 显式、静态定义（如#define）。否则（例如使用const int），我们会遇到GPU崩溃。
    out << "#define CONFIG_MAX_STEREOSCOPIC_EYES " << (int) CONFIG_MAX_STEREOSCOPIC_EYES << "\n";

    if (mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        // On ES2 since we don't have post-processing, we need to emulate EGL_GL_COLORSPACE_KHR,
        // when it's not supported.
        // 在ES2上，由于我们没有后处理，当不支持EGL_GL_COLORSPACE_KHR时，我们需要模拟它。
        generateSpecializationConstant(out, "CONFIG_SRGB_SWAPCHAIN_EMULATION",
                +ReservedSpecializationConstants::CONFIG_SRGB_SWAPCHAIN_EMULATION, false);
    }

    out << '\n';
    out << SHADERS_COMMON_DEFINES_GLSL_DATA;

    if (material.featureLevel == FeatureLevel::FEATURE_LEVEL_0 &&
            (mFeatureLevel > FeatureLevel::FEATURE_LEVEL_0
                    || mTargetLanguage == TargetLanguage::SPIRV)) {
        // Insert compatibility definitions for ESSL 1.0 functions which were removed in ESSL 3.0.
        // 插入ESSL 1.0函数的兼容性定义，这些函数在ESSL 3.0中被移除。

        // This is the minimum required value according to the OpenGL ES Shading Language Version
        // 1.00 document. glslang forbids defining symbols beginning with gl_ as const, hence the
        // #define.
        // 这是根据OpenGL ES着色语言版本1.00文档所需的最小值。
        // glslang禁止将以gl_开头的符号定义为const，因此使用#define。
        generateDefine(out, "gl_MaxVaryingVectors", "8");

        generateDefine(out, "texture2D", "texture");
        generateDefine(out, "texture2DProj", "textureProj");
        generateDefine(out, "texture3D", "texture");
        generateDefine(out, "texture3DProj", "textureProj");
        generateDefine(out, "textureCube", "texture");

        if (stage == ShaderStage::VERTEX) {
            generateDefine(out, "texture2DLod", "textureLod");
            generateDefine(out, "texture2DProjLod", "textureProjLod");
            generateDefine(out, "texture3DLod", "textureLod");
            generateDefine(out, "texture3DProjLod", "textureProjLod");
            generateDefine(out, "textureCubeLod", "textureLod");
        }
    }

    out << "\n";
    return out;
}

// 获取默认精度（根据着色器阶段和着色器模型）
// @param stage 着色器阶段
// @return 默认精度值
Precision CodeGenerator::getDefaultPrecision(ShaderStage stage) const {
    switch (stage) {
        case ShaderStage::VERTEX:
            return Precision::HIGH;  // 顶点着色器使用高精度
        case ShaderStage::FRAGMENT:
            switch (mShaderModel) {
                case ShaderModel::MOBILE:
                    return Precision::MEDIUM;  // 移动端片段着色器使用中等精度
                case ShaderModel::DESKTOP:
                    return Precision::HIGH;   // 桌面端片段着色器使用高精度
            }
        case ShaderStage::COMPUTE:
            return Precision::HIGH;  // 计算着色器使用高精度
    }
}

// 获取统一变量的默认精度（根据着色器模型）
// @return 默认精度值
Precision CodeGenerator::getDefaultUniformPrecision() const {
    switch (mShaderModel) {
        case ShaderModel::MOBILE:
            return Precision::MEDIUM;  // 移动端使用中等精度
        case ShaderModel::DESKTOP:
            return Precision::HIGH;    // 桌面端使用高精度
    }
}

// 生成着色器的公共结尾（换行符，用于行压缩）
// @param out 输出流
// @return 输出流引用
// For line compression all shaders finish with a newline character.
io::sstream& CodeGenerator::generateCommonEpilog(io::sstream& out) {
    out << "\n";
    return out;
}

// 生成表面材质类型定义
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceTypes(io::sstream& out, ShaderStage stage) {
    out << '\n';
    switch (stage) {
        case ShaderStage::VERTEX:
            out << '\n';
            out << SHADERS_SURFACE_TYPES_GLSL_DATA;  // 顶点着色器类型定义
            break;
        case ShaderStage::FRAGMENT:
            out << '\n';
            out << SHADERS_SURFACE_TYPES_GLSL_DATA;  // 片段着色器类型定义
            break;
        case ShaderStage::COMPUTE:
            break;
    }
    return out;
}

// 生成表面材质主函数
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceMain(io::sstream& out, ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX:
            out << SHADERS_SURFACE_MAIN_VS_DATA;  // 顶点着色器主函数
            break;
        case ShaderStage::FRAGMENT:
            out << SHADERS_SURFACE_MAIN_FS_DATA;  // 片段着色器主函数
            break;
        case ShaderStage::COMPUTE:
            out << SHADERS_SURFACE_MAIN_CS_DATA;
            break;
    }
    return out;
}

// 生成后处理材质主函数
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generatePostProcessMain(io::sstream& out, ShaderStage stage) {
    if (stage == ShaderStage::VERTEX) {
        out << SHADERS_POST_PROCESS_MAIN_VS_DATA;  // 后处理顶点着色器主函数
    } else if (stage == ShaderStage::FRAGMENT) {
        out << SHADERS_POST_PROCESS_MAIN_FS_DATA;  // 后处理片段着色器主函数
    }
    return out;
}

// 生成自定义插值变量声明
// @param out 输出流
// @param stage 着色器阶段
// @param variable 自定义变量信息
// @param index 变量索引
// @return 输出流引用
io::sstream& CodeGenerator::generateCommonVariable(io::sstream& out, ShaderStage stage,
        const MaterialBuilder::CustomVariable& variable, size_t index) {
    auto const& name = variable.name;
    const char* precisionString = getPrecisionQualifier(variable.precision);
    if (!name.empty()) {
        if (stage == ShaderStage::VERTEX) {
            // 生成顶点着色器中的自定义变量定义和声明
            out << "\n#define VARIABLE_CUSTOM" << index << " " << name.c_str() << "\n";
            out << "\n#define VARIABLE_CUSTOM_AT" << index << " variable_" << name.c_str() << "\n";
            out << "LAYOUT_LOCATION(" << index << ") VARYING " << precisionString << " vec4 variable_" << name.c_str() << ";\n";
        } else if (stage == ShaderStage::FRAGMENT) {
            // 生成片段着色器中的自定义变量声明
            if (!variable.hasPrecision && variable.precision == Precision::DEFAULT) {
                // for backward compatibility
                precisionString = "highp";
            }
            out << "\nLAYOUT_LOCATION(" << index << ") VARYING " << precisionString << " vec4 variable_" << name.c_str() << ";\n";
        }
    }
    return out;
}

// 生成表面着色器输入声明（属性、插值模式、推送常量等）
// @param out 输出流
// @param stage 着色器阶段
// @param attributes 属性位集
// @param interpolation 插值模式
// @param pushConstants 推送常量列表
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceShaderInputs(io::sstream& out, ShaderStage stage,
        const AttributeBitset& attributes, Interpolation interpolation,
        MaterialBuilder::PushConstantList const& pushConstants) const {
    auto const& attributeDatabase = MaterialBuilder::getAttributeDatabase();

    // 生成插值模式定义
    const char* shading = getInterpolationQualifier(interpolation);
    out << "#define SHADING_INTERPOLATION " << shading << "\n";

    // 生成属性相关的预处理器定义
    out << "\n";
    attributes.forEachSetBit([&out, &attributeDatabase](size_t i) {
        generateDefine(out, attributeDatabase[i].getDefineName().c_str(), true);
    });

    // 在顶点着色器中生成属性输入声明
    if (stage == ShaderStage::VERTEX) {
        out << "\n";
        attributes.forEachSetBit([&out, &attributeDatabase, this](size_t i) {
            auto const& attribute = attributeDatabase[i];
            assert_invariant( i == attribute.location );
            // 根据目标语言和功能级别选择布局语法
            if (mTargetLanguage == TargetLanguage::SPIRV ||
                    mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1) {
                out << "layout(location = " << size_t(attribute.location) << ") in ";
            } else {
                out << "attribute ";
            }
            out << getTypeName(attribute.type) << " " << attribute.getAttributeName() << ";\n";
        });

        // 生成推送常量
        out << "\n";
        generatePushConstants(out, pushConstants, attributes.size());
    }

    // 生成表面着色器的varying变量
    out << "\n";
    out << SHADERS_SURFACE_VARYINGS_GLSL_DATA;
    return out;
}

// 生成自定义输出变量声明
// @param out 输出流
// @param stage 着色器阶段
// @param name 输出变量名称
// @param index 输出索引
// @param qualifier 变量限定符
// @param precision 精度
// @param outputType 输出类型
// @return 输出流引用
io::sstream& CodeGenerator::generateOutput(io::sstream& out, ShaderStage stage,
        const CString& name, size_t index,
        MaterialBuilder::VariableQualifier qualifier,
        MaterialBuilder::Precision precision,
        MaterialBuilder::OutputType outputType) const {
    // 顶点着色器不支持输出，或名称为空则直接返回
    if (name.empty() || stage == ShaderStage::VERTEX) {
        return out;
    }

    // Feature level 0 only supports one output.
    // 功能级别0只支持一个输出
    if (index > 0 && mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        slog.w << "Discarding an output in the generated ESSL 1.0 shader: index = " << index
               << ", name = " << name.c_str() << io::endl;
        return out;
    }

    // TODO: add and support additional variable qualifiers
    (void) qualifier;
    assert(qualifier == MaterialBuilder::VariableQualifier::OUT);

    // The material output type is the type the shader writes to from the material.
    // 材质输出类型是着色器从材质写入的类型
    const MaterialBuilder::OutputType materialOutputType = outputType;

    const char* swizzleString = "";

    // Metal and WebGPU don't support some 3-component texture formats, so the backend uses 4-component
    // formats behind the scenes. It's an error to output fewer components than the attachment
    // needs, so we always output a float4 instead of a float3. It's never an error to output extra
    // components.
    //
    // Meanwhile, ESSL 1.0 must always write to gl_FragColor, a vec4.
    // Metal和WebGPU不支持某些3分量纹理格式，因此后端在幕后使用4分量格式。
    // 输出少于附件需要的分量是错误的，因此我们总是输出float4而不是float3。
    // 输出额外的分量永远不会出错。
    // 同时，ESSL 1.0必须始终写入gl_FragColor（一个vec4）。
    if (mTargetApi == TargetApi::METAL || mTargetApi == TargetApi::WEBGPU ||
            mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        if (outputType == MaterialBuilder::OutputType::FLOAT3) {
            outputType = MaterialBuilder::OutputType::FLOAT4;
            swizzleString = ".rgb";  // 使用swizzle来提取RGB分量
        }
    }

    const char* precisionString = getPrecisionQualifier(precision);
    const char* materialTypeString = getOutputTypeName(materialOutputType);
    const char* typeString = getOutputTypeName(outputType);

    // 判断是否生成ESSL 3.0代码
    bool generate_essl3_code = mTargetLanguage == TargetLanguage::SPIRV
            || mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1;

    // 生成输出相关的预处理器定义
    out << "\n#define FRAG_OUTPUT"               << index << " " << name.c_str();
    if (generate_essl3_code) {
        out << "\n#define FRAG_OUTPUT_AT"        << index << " output_" << name.c_str();
    } else {
        out << "\n#define FRAG_OUTPUT_AT"        << index << " gl_FragColor";
    }
    out << "\n#define FRAG_OUTPUT_MATERIAL_TYPE" << index << " " << materialTypeString;
    out << "\n#define FRAG_OUTPUT_PRECISION"     << index << " " << precisionString;
    out << "\n#define FRAG_OUTPUT_TYPE"          << index << " " << typeString;
    out << "\n#define FRAG_OUTPUT_SWIZZLE"       << index << " " << swizzleString;
    out << "\n";

    // 生成输出变量声明（仅ESSL 3.0+）
    if (generate_essl3_code) {
        out << "\nlayout(location=" << index << ") out " << precisionString << " "
            << typeString << " output_" << name.c_str() << ";\n";
    }

    return out;
}


// 生成表面深度主函数（用于深度预通道）
// @param out 输出流
// @param stage 着色器阶段（必须是FRAGMENT或COMPUTE）
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceDepthMain(io::sstream& out, ShaderStage stage) {
    assert(stage != ShaderStage::VERTEX);
    if (stage == ShaderStage::FRAGMENT) {
        out << SHADERS_SURFACE_DEPTH_MAIN_FS_DATA;
    }
    return out;
}

// 获取统一变量的精度限定符
// @param type 统一变量类型
// @param precision 指定的精度
// @param uniformPrecision 统一变量的默认精度
// @param defaultPrecision 着色器阶段的默认精度
// @return 精度限定符字符串，如果不需要则返回空字符串
const char* CodeGenerator::getUniformPrecisionQualifier(UniformType type, Precision precision,
        Precision uniformPrecision, Precision defaultPrecision) noexcept {
    // 某些类型（如bool）不能有精度限定符
    if (!hasPrecision(type)) {
        // some types like bool can't have a precision qualifier
        return "";
    }
    // 如果精度字段指定为DEFAULT，将其转换为统一变量的默认精度（桌面端和移动端可能不同）
    if (precision == Precision::DEFAULT) {
        // if precision field is specified as default, turn it into the default precision for
        // uniforms (which might be different on desktop vs mobile)
        precision = uniformPrecision;
    }
    // 如果精度与着色器阶段的默认精度匹配，省略精度限定符
    // 这意味着有效精度可能在不同阶段不同
    if (precision == defaultPrecision) {
        // finally if the precision match the default precision of this stage, don't omit
        // the precision qualifier -- which mean the effective precision might be different
        // in different stages.
        return "";
    }
    return getPrecisionQualifier(precision);
}

// 生成缓冲区声明（SSBO）
// @param out 输出流
// @param buffers 缓冲区容器
// @return 输出流引用
utils::io::sstream& CodeGenerator::generateBuffers(utils::io::sstream& out,
        MaterialInfo::BufferContainer const& buffers) const {

    // 遍历所有缓冲区，生成缓冲区接口块
    for (auto const* buffer : buffers) {

        // FIXME: we need to get the bindings for the SSBOs and that will depend on the samplers
        // FIXME: 我们需要获取SSBO的绑定，这将取决于采样器
        backend::descriptor_binding_t binding = 0;

        if (mTargetApi == TargetApi::OPENGL) {
            // For OpenGL, the set is not used bug the binding must be unique.
            // 对于OpenGL，不使用set，但绑定必须是唯一的
            binding = getUniqueSsboBindingPoint();
        }
        // 生成缓冲区接口块（用于计算着色器）
        generateBufferInterfaceBlock(out, ShaderStage::COMPUTE,
                DescriptorSetBindingPoints::PER_MATERIAL, binding, *buffer);
    }
    return out;
}

// 生成统一变量声明（UBO）
// @param out 输出流
// @param stage 着色器阶段
// @param set 描述符集绑定点
// @param binding 绑定点
// @param uib 缓冲区接口块
// @return 输出流引用
io::sstream& CodeGenerator::generateUniforms(io::sstream& out, ShaderStage stage,
        filament::DescriptorSetBindingPoints set,
        filament::backend::descriptor_binding_t binding,
        const BufferInterfaceBlock& uib) const {

    if (mTargetApi == TargetApi::OPENGL) {
        // For OpenGL, the set is not used bug the binding must be unique.
        // 对于OpenGL，不使用set，但绑定必须是唯一的
        binding = getUniqueUboBindingPoint();
    }

    // 生成缓冲区接口块
    return generateBufferInterfaceBlock(out, stage, set, binding, uib);
}

// 生成接口字段声明（UBO或SSBO的字段）
// @param out 输出流
// @param infos 字段信息列表
// @param defaultPrecision 默认精度
// @return 输出流引用
io::sstream& CodeGenerator::generateInterfaceFields(io::sstream& out,
        FixedCapacityVector<BufferInterfaceBlock::FieldInfo> const& infos,
        Precision defaultPrecision) const {
    Precision const uniformPrecision = getDefaultUniformPrecision();

    // 遍历所有字段信息，生成字段声明
    for (auto const& info : infos) {
        // 跳过不支持的功能级别字段
        if (mFeatureLevel < info.minFeatureLevel) {
            continue;
        }
        // 获取类型和精度限定符
        char const* const type = getUniformTypeName(info);
        char const* const precision = getUniformPrecisionQualifier(info.type, info.precision,
                uniformPrecision, defaultPrecision);
        // 生成字段声明：精度 类型 名称[数组大小];
        out << "    " << precision;
        if (precision[0] != '\0') out << " ";
        out << type << " " << info.name.c_str();
        // 如果是数组，添加数组大小
        if (info.isArray) {
            if (info.sizeName.empty()) {
                if (info.size) {
                    out << "[" << info.size << "]";
                } else {
                    out << "[]";
                }
            } else {
                out << "[" << info.sizeName.c_str() << "]";
            }
        }
        out << ";\n";
    }
    return out;
}

// 将UBO生成为普通统一变量（用于ES2/功能级别0，不支持UBO）
// @param out 输出流
// @param stage 着色器阶段
// @param uib 缓冲区接口块
// @return 输出流引用
io::sstream& CodeGenerator::generateUboAsPlainUniforms(io::sstream& out, ShaderStage stage,
        const BufferInterfaceBlock& uib) const {

    auto const& infos = uib.getFieldInfoList();

    // 生成结构体名称（首字母大写）和实例名称（首字母小写）
    std::string blockName{ uib.getName() };
    std::string instanceName{ uib.getName() };
    blockName.front() = char(std::toupper((unsigned char)blockName.front()));
    instanceName.front() = char(std::tolower((unsigned char)instanceName.front()));

    // 生成结构体定义
    out << "\nstruct " << blockName << " {\n";

    // 生成结构体字段
    generateInterfaceFields(out, infos, Precision::DEFAULT);

    // 生成结构体结束和统一变量声明
    out << "};\n";
    out << "uniform " << blockName << " " << instanceName << ";\n";

    return out;
}

// 生成缓冲区接口块声明（UBO或SSBO）
// @param out 输出流
// @param stage 着色器阶段
// @param set 描述符集绑定点
// @param binding 绑定点
// @param uib 缓冲区接口块
// @return 输出流引用
io::sstream& CodeGenerator::generateBufferInterfaceBlock(io::sstream& out, ShaderStage stage,
        filament::DescriptorSetBindingPoints set,
        filament::backend::descriptor_binding_t binding,
        const BufferInterfaceBlock& uib) const {
    // 如果对于当前功能级别为空，直接返回
    if (uib.isEmptyForFeatureLevel(mFeatureLevel)) {
        return out;
    }

    auto const& infos = uib.getFieldInfoList();

    // 对于ES2/功能级别0，需要生成结构体而不是UBO
    if (mTargetLanguage == TargetLanguage::GLSL &&
            mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0) {
        // we need to generate a structure instead
        assert_invariant(mTargetApi == TargetApi::OPENGL);
        assert_invariant(uib.getTarget() == BufferInterfaceBlock::Target::UNIFORM);
        return generateUboAsPlainUniforms(out, stage, uib);
    }

    // 生成结构体名称（首字母大写）和实例名称（首字母小写）
    std::string blockName{ uib.getName() };
    std::string instanceName{ uib.getName() };
    blockName.front() = char(std::toupper((unsigned char)blockName.front()));
    instanceName.front() = char(std::tolower((unsigned char)instanceName.front()));

    // 生成layout限定符
    out << "\nlayout(";
    if (mTargetLanguage == TargetLanguage::SPIRV ||
        mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_2) {
        // 根据目标API生成不同的布局限定符
        switch (mTargetApi) {
            case TargetApi::METAL:
            case TargetApi::VULKAN:
                // Metal和Vulkan需要set和binding
                out << "set = " << +set << ", binding = " << +binding << ", ";
                break;

            case TargetApi::OPENGL:
                // GLSL 4.5 / ESSL 3.1 require the 'binding' layout qualifier
                // in the GLSL 4.5 / ESSL 3.1 case, the set is not used and binding is unique
                // GLSL 4.5 / ESSL 3.1需要'binding'布局限定符
                // 在GLSL 4.5 / ESSL 3.1情况下，不使用set，binding是唯一的
                out << "binding = " << +binding << ", ";
                break;
            case TargetApi::WEBGPU:
                // WebGPU需要set和binding
                out << "set = " << +set << ", binding = " << +binding << ", ";
            break;
            case TargetApi::ALL:
                // nonsensical, shouldn't happen.
                break;
        }
    }
    // 生成对齐方式（std140或std430）
    switch (uib.getAlignment()) {
        case BufferInterfaceBlock::Alignment::std140:
            out << "std140";
            break;
        case BufferInterfaceBlock::Alignment::std430:
            out << "std430";
            break;
    }

    out << ") ";

    // 生成目标类型（uniform或buffer）
    switch (uib.getTarget()) {
        case BufferInterfaceBlock::Target::UNIFORM:
            out << "uniform ";
            break;
        case BufferInterfaceBlock::Target::SSBO:
            out << "buffer ";
            break;
    }

    out << blockName << " ";

    // 如果是SSBO，生成限定符（coherent、writeonly、readonly等）
    if (uib.getTarget() == BufferInterfaceBlock::Target::SSBO) {
        uint8_t qualifiers = uib.getQualifier();
        while (qualifiers) {
            uint8_t const mask = 1u << utils::ctz(unsigned(qualifiers));
            switch (BufferInterfaceBlock::Qualifier(qualifiers & mask)) {
                case BufferInterfaceBlock::Qualifier::COHERENT:  out << "coherent "; break;
                case BufferInterfaceBlock::Qualifier::WRITEONLY: out << "writeonly "; break;
                case BufferInterfaceBlock::Qualifier::READONLY:  out << "readonly "; break;
                case BufferInterfaceBlock::Qualifier::VOLATILE:  out << "volatile "; break;
                case BufferInterfaceBlock::Qualifier::RESTRICT:  out << "restrict "; break;
            }
            qualifiers &= ~mask;
        }
    }

    // 生成接口块开始
    out << "{\n";

    // 生成接口字段
    generateInterfaceFields(out, infos, getDefaultPrecision(stage));

    // 生成接口块结束和实例名称
    out << "} " << instanceName << ";\n";

    return out;
}

// 生成公共采样器声明
// @param out 输出流
// @param set 描述符集绑定点
// @param list 采样器信息列表
// @return 输出流引用
io::sstream& CodeGenerator::generateCommonSamplers(utils::io::sstream& out,
        filament::DescriptorSetBindingPoints set,
        filament::SamplerInterfaceBlock::SamplerInfoList const& list) const {
    // 如果列表为空，直接返回
    if (list.empty()) {
        return out;
    }

    // 遍历所有采样器信息，生成采样器声明
    for (auto const& info : list) {
        auto type = info.type;
        // 如果是外部采样器且不是移动端，转换为普通2D采样器
        if (type == SamplerType::SAMPLER_EXTERNAL && mShaderModel != ShaderModel::MOBILE) {
            // we're generating the shader for the desktop, where we assume external textures
            // are not supported, in which case we revert to texture2d
            // 我们正在为桌面端生成着色器，假设不支持外部纹理，在这种情况下我们回退到texture2d
            type = SamplerType::SAMPLER_2D;
        }
        char const* const typeName = getSamplerTypeName(type, info.format, info.multisample);
        char const* const precision = getPrecisionQualifier(info.precision);
        // 如果是SPIR-V目标语言，生成布局限定符
        if (mTargetLanguage == TargetLanguage::SPIRV) {
            switch (mTargetApi) {
                // Note that the set specifier is not covered by the desktop GLSL spec, including
                // recent versions. It is only documented in the GL_KHR_vulkan_glsl extension.
                // 注意，set说明符不在桌面GLSL规范中，包括最新版本。它仅在GL_KHR_vulkan_glsl扩展中记录。
                case TargetApi::VULKAN:
                    // Vulkan：需要binding和set
                    out << "layout(binding = " << +info.binding << ", set = " << +set << ") ";
                    break;

                // For Metal, each sampler group gets its own descriptor set, each of which will
                // become an argument buffer. The first descriptor set is reserved for uniforms,
                // hence the +1 here.
                // 对于Metal，每个采样器组都有自己的描述符集，每个描述符集将成为参数缓冲区。
                // 第一个描述符集保留给统一变量，因此这里+1。
                case TargetApi::METAL:
                    // Metal：需要binding和set
                    out << "layout(binding = " << +info.binding << ", set = " << +set << ") ";
                    break;

                case TargetApi::OPENGL:
                    // GLSL 4.5 / ESSL 3.1 require the 'binding' layout qualifier
                    // GLSL 4.5 / ESSL 3.1需要'binding'布局限定符
                    out << "layout(binding = " << getUniqueSamplerBindingPoint() << ") ";
                    break;
                case TargetApi::WEBGPU:
                    // WebGPU：需要binding和set
                    out << "layout(binding = " << +info.binding << ", set = " << +set << ") ";
                break;
                case TargetApi::ALL:
                    // should not happen
                    // 不应该发生
                    break;
            }
        }
        // 生成采样器统一变量声明
        out << "uniform " << precision << " " << typeName << " " << info.uniformName.c_str();
        out << ";\n";
    }
    out << "\n";

    return out;
}

// 生成后处理子通道输入声明（用于Vulkan子通道输入）
// @param out 输出流
// @param subpass 子通道信息
// @return 输出流引用
io::sstream& CodeGenerator::generatePostProcessSubpass(io::sstream& out, SubpassInfo subpass) {
    // 如果子通道信息无效，直接返回
    if (!subpass.isValid) {
        return out;
    }

    // 生成子通道输入的统一变量名称
    CString subpassName =
            SamplerInterfaceBlock::generateUniformName(subpass.block.c_str(), subpass.name.c_str());

    char const* const typeName = "subpassInput";
    // In our Vulkan backend, subpass inputs always live in descriptor set 2. (ignored for GLES)
    // 在我们的Vulkan后端中，子通道输入始终位于描述符集2中（GLES忽略）
    char const* const precision = getPrecisionQualifier(subpass.precision);
    // 生成子通道输入的布局限定符和统一变量声明
    out << "layout(input_attachment_index = " << (int) subpass.attachmentIndex
        << ", set = 2, binding = " << (int) subpass.binding
        << ") ";
    out << "uniform " << precision << " " << typeName << " " << subpassName.c_str();
    out << ";\n";

    out << "\n";

    return out;
}

// 修复外部采样器声明（将sampler2D替换为samplerExternalOES并添加扩展指令）
// @param shader 着色器代码字符串（将被修改）
// @param sib 采样器接口块
// @param featureLevel 功能级别
void CodeGenerator::fixupExternalSamplers(
        std::string& shader, SamplerInterfaceBlock const& sib, FeatureLevel featureLevel) noexcept {
    auto const& infos = sib.getSamplerInfoList();
    // 如果采样器信息列表为空，直接返回
    if (infos.empty()) {
        return;
    }

    bool hasExternalSampler = false;

    // Replace sampler2D declarations by samplerExternal declarations as they may have
    // been swapped during a previous optimization step
    // 将sampler2D声明替换为samplerExternal声明，因为它们可能在之前的优化步骤中被交换
    for (auto const& info : infos) {
        if (info.type == SamplerType::SAMPLER_EXTERNAL) {
            // 查找sampler2D声明
            auto name = std::string("sampler2D ") + info.uniformName.c_str();
            size_t const index = shader.find(name);

            if (index != std::string::npos) {
                hasExternalSampler = true;
                // 替换为samplerExternalOES
                auto newName =
                        std::string("samplerExternalOES ") + info.uniformName.c_str();
                shader.replace(index, name.size(), newName);
            }
        }
    }

    // This method should only be called on shaders that have external samplers but since
    // they may have been removed by previous optimization steps, we check again here
    // 此方法应该只在具有外部采样器的着色器上调用，但由于它们可能在之前的优化步骤中被移除，我们在这里再次检查
    if (hasExternalSampler) {
        // Find the #version line, so we can insert the #extension directive
        // 查找#version行，以便我们可以插入#extension指令
        size_t index = shader.find("#version");
        index += 8;

        // Find the end of the line and skip the line return
        // 查找行尾并跳过换行符
        while (shader[index] != '\n') index++;
        index++;

        // 根据功能级别选择扩展指令
        const char *extensionLine = (featureLevel >= FeatureLevel::FEATURE_LEVEL_1)
                ? "#extension GL_OES_EGL_image_external_essl3 : require\n\n"
                : "#extension GL_OES_EGL_image_external : require\n\n";
        shader.insert(index, extensionLine);
    }
}


// 生成预处理器定义（bool值）
// @param out 输出流
// @param name 定义名称
// @param value 定义值（如果为true则生成定义）
// @return 输出流引用
io::sstream& CodeGenerator::generateDefine(io::sstream& out, const char* name, bool value) {
    if (value) {
        out << "#define " << name << "\n";
    }
    return out;
}

// 生成预处理器定义（uint32_t值）
// @param out 输出流
// @param name 定义名称
// @param value 定义值
// @return 输出流引用
io::sstream& CodeGenerator::generateDefine(io::sstream& out, const char* name, uint32_t value) {
    out << "#define " << name << " " << value << "\n";
    return out;
}

// 生成预处理器定义（字符串值）
// @param out 输出流
// @param name 定义名称
// @param string 定义值（字符串）
// @return 输出流引用
io::sstream& CodeGenerator::generateDefine(io::sstream& out, const char* name, const char* string) {
    out << "#define " << name << " " << string << "\n";
    return out;
}

// 生成带索引的预处理器定义
// @param out 输出流
// @param name 定义名称前缀
// @param index 索引值
// @param value 定义值
// @return 输出流引用
io::sstream& CodeGenerator::generateIndexedDefine(io::sstream& out, const char* name,
        uint32_t index, uint32_t value) {
    out << "#define " << name << index << " " << value << "\n";
    return out;
}

struct SpecializationConstantFormatter {
    std::string operator()(int value) noexcept { return std::to_string(value); }
    std::string operator()(float value) noexcept { return std::to_string(value); }
    std::string operator()(bool value) noexcept { return value ? "true" : "false"; }
};

// 生成特化常量声明（用于SPIR-V特化常量）
// @param out 输出流
// @param name 常量名称
// @param id 常量ID
// @param value 常量值（int、float或bool）
// @return 输出流引用
utils::io::sstream& CodeGenerator::generateSpecializationConstant(utils::io::sstream& out,
        const char* name, uint32_t id, std::variant<int, float, bool> value) const {

    // 格式化常量值为字符串
    std::string const constantString = std::visit(SpecializationConstantFormatter(), value);

    static const char* types[] = { "int", "float", "bool" };

    // Spec constants aren't fully supported in Tint,
    // workaround until https://issues.chromium.org/issues/42250586 is resolved
    // 特化常量在Tint中不完全支持，变通方法直到https://issues.chromium.org/issues/42250586解决
    if (mTargetApi == TargetApi::WEBGPU) {
        // WebGPU：使用常量变量作为变通方法
        std::string const variableName = "FILAMENT_SPEC_CONST_" + std::to_string(id) + "_" + name;
        out << " const " << types[value.index()] << " " << variableName << " = " << constantString << ";\n";
        out << types[value.index()] << " " << name << " =  " << variableName << ";\n";
        return out;
    }
    // SPIR-V目标：使用layout(constant_id)声明特化常量
    if (mTargetLanguage == MaterialBuilderBase::TargetLanguage::SPIRV) {
        out << "layout (constant_id = " << id << ") const "
            << types[value.index()] << " " << name << " = " << constantString << ";\n";
    } else {
        // GLSL目标：使用预处理器定义和常量变量
        out << "#ifndef SPIRV_CROSS_CONSTANT_ID_" << id << '\n'
            << "#define SPIRV_CROSS_CONSTANT_ID_" << id << " " << constantString << '\n'
            << "#endif" << '\n'
            << "const " << types[value.index()] << " " << name << " = SPIRV_CROSS_CONSTANT_ID_" << id
            << ";\n\n";
    }
    return out;
}

// 生成推送常量声明
// @param out 输出流
// @param pushConstants 推送常量列表
// @param layoutLocation 布局位置
// @return 输出流引用
utils::io::sstream& CodeGenerator::generatePushConstants(utils::io::sstream& out,
        MaterialBuilder::PushConstantList const& pushConstants, size_t const layoutLocation) const {
    // 如果推送常量列表为空，直接返回
    if (UTILS_UNLIKELY(pushConstants.empty())) {
        return out;
    }
    static constexpr char const* STRUCT_NAME = "Constants";

    // Lambda函数：根据常量类型返回类型字符串
    auto const getType = [](ConstantType const& type) {
        switch (type) {
            case ConstantType::BOOL:
                return "bool";
            case ConstantType::INT:
                return "int";
            case ConstantType::FLOAT:
                return "float";
        }
    };
    // This is a workaround for WebGPU not supporting push constants for skinning.
    // We replace the push constant with a regular constant struct initialized to 0.
    // 这是WebGPU不支持蒙皮推送常量的变通方法。
    // 我们用初始化为0的常规常量结构体替换推送常量。
    if (mTargetApi == TargetApi::WEBGPU) {
        assert_invariant(
                pushConstants.size() == 1 &&
                "The current workaround for WebGPU push constants assumes for now that only 1");
        assert_invariant(pushConstants[0].name == CString("morphingBufferOffset") &&
                         "The current workaround for WebGPU push constants assumes only the "
                         "morphingBufferOffset constant is present.");
        assert_invariant(pushConstants[0].type == ConstantType::INT &&
                         "The current workaround for WebGPU push constants assumes "
                         "morphingBufferOffset is an integer type.");
        out << "struct " << STRUCT_NAME << " {\n";
        for (auto const& constant: pushConstants) {
            out << "    " << getType(constant.type) << " " << constant.name.c_str() << ";\n";
        }
        out << "};\n";
        out << "const " << STRUCT_NAME << " " << PUSH_CONSTANT_STRUCT_VAR_NAME << " = "
            << STRUCT_NAME << "(0);\n";
        return out;
    }

    // 判断是否输出SPIR-V格式的推送常量
    bool const outputSpirv =
            mTargetLanguage == TargetLanguage::SPIRV && mTargetApi != TargetApi::OPENGL;
    if (outputSpirv) {
        // SPIR-V格式：使用layout(push_constant) uniform
        out << "layout(push_constant) uniform " << STRUCT_NAME << " {\n ";
    } else {
        // GLSL格式：使用struct定义
        out << "struct " << STRUCT_NAME << " {\n";
    }

    // 生成所有推送常量字段
    for (auto const& constant: pushConstants) {
        out << getType(constant.type) << " " << constant.name.c_str() << ";\n";
    }

    if (outputSpirv) {
        // SPIR-V格式：直接声明变量
        out << "} " << PUSH_CONSTANT_STRUCT_VAR_NAME << ";\n";
    } else {
        // GLSL格式：先结束结构体，再声明统一变量
        out << "};\n";
        out << "LAYOUT_LOCATION(" << static_cast<int>(layoutLocation) << ") uniform " << STRUCT_NAME
            << " " << PUSH_CONSTANT_STRUCT_VAR_NAME << ";\n";
    }
    return out;
}

// 生成材质属性定义（如果属性已设置）
// @param out 输出流
// @param property 材质属性
// @param isSet 属性是否已设置
// @return 输出流引用
io::sstream& CodeGenerator::generateMaterialProperty(io::sstream& out,
        MaterialBuilder::Property property, bool isSet) {
    // 如果属性已设置，生成预处理器定义
    if (isSet) {
        out << "#define " << "MATERIAL_HAS_" << getConstantName(property) << "\n";
    }
    return out;
}

// 生成着色器质量定义
// @param out 输出流
// @param quality 着色器质量
// @return 输出流引用
io::sstream& CodeGenerator::generateQualityDefine(io::sstream& out, ShaderQuality quality) const {
    // 定义质量常量
    out << "#define FILAMENT_QUALITY_LOW    0\n";
    out << "#define FILAMENT_QUALITY_NORMAL 1\n";
    out << "#define FILAMENT_QUALITY_HIGH   2\n";

    // 根据质量设置或着色器模型选择质量级别
    switch (quality) {
        case ShaderQuality::DEFAULT:
            // 默认质量：根据着色器模型选择（桌面端=高，移动端=低）
            switch (mShaderModel) {
                default:                   goto quality_normal;
                case ShaderModel::DESKTOP: goto quality_high;
                case ShaderModel::MOBILE:  goto quality_low;
            }
        case ShaderQuality::LOW:
        quality_low:
            out << "#define FILAMENT_QUALITY FILAMENT_QUALITY_LOW\n";
            break;
        case ShaderQuality::NORMAL:
        default:
        quality_normal:
            out << "#define FILAMENT_QUALITY FILAMENT_QUALITY_NORMAL\n";
            break;
        case ShaderQuality::HIGH:
        quality_high:
            out << "#define FILAMENT_QUALITY FILAMENT_QUALITY_HIGH\n";
            break;
    }

    return out;
}

// 生成表面着色器的公共代码（数学函数、实例化、阴影等）
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceCommon(io::sstream& out, ShaderStage stage) {
    // 生成公共数学函数
    out << SHADERS_COMMON_MATH_GLSL_DATA;
    switch (stage) {
        case ShaderStage::VERTEX:
            // 顶点着色器：实例化和阴影相关代码
            out << SHADERS_SURFACE_INSTANCING_GLSL_DATA;
            out << SHADERS_SURFACE_SHADOWING_GLSL_DATA;
            break;
        case ShaderStage::FRAGMENT:
            // 片段着色器：实例化、阴影、着色、图形和材质相关代码
            out << SHADERS_SURFACE_INSTANCING_GLSL_DATA;
            out << SHADERS_SURFACE_SHADOWING_GLSL_DATA;
            out << SHADERS_COMMON_SHADING_FS_DATA;
            out << SHADERS_COMMON_GRAPHICS_FS_DATA;
            out << SHADERS_SURFACE_MATERIAL_FS_DATA;
            break;
        case ShaderStage::COMPUTE:
            out << '\n';
            // TODO: figure out if we need some common files here
            // TODO: 确定是否需要一些公共文件
            break;
    }
    return out;
}

// 生成后处理着色器的公共代码（数学函数等）
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generatePostProcessCommon(io::sstream& out, ShaderStage stage) {
    // 生成公共数学函数
    out << SHADERS_COMMON_MATH_GLSL_DATA;
    if (stage == ShaderStage::VERTEX) {
    } else if (stage == ShaderStage::FRAGMENT) {
        out << SHADERS_COMMON_SHADING_FS_DATA;
        out << SHADERS_COMMON_GRAPHICS_FS_DATA;
    }
    return out;
}

// 生成表面着色器的雾效代码
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceFog(io::sstream& out, ShaderStage stage) {
    if (stage == ShaderStage::VERTEX) {
        // 顶点着色器不需要雾效代码
    } else if (stage == ShaderStage::FRAGMENT) {
        // 片段着色器：生成雾效相关代码
        out << SHADERS_SURFACE_FOG_FS_DATA;
    }
    return out;
}

// 生成表面着色器的材质代码
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceMaterial(io::sstream& out, ShaderStage stage) {
    if (stage == ShaderStage::VERTEX) {
        // 顶点着色器：生成材质输入相关代码
        out << SHADERS_SURFACE_MATERIAL_INPUTS_VS_DATA;
    } else if (stage == ShaderStage::FRAGMENT) {
        out << SHADERS_SURFACE_MATERIAL_INPUTS_FS_DATA;
    }
    return out;
}

// 生成后处理着色器的输入声明
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generatePostProcessInputs(io::sstream& out, ShaderStage stage) {
    if (stage == ShaderStage::VERTEX) {
        // 顶点着色器：生成后处理输入相关代码
        out << SHADERS_POST_PROCESS_INPUTS_VS_DATA;
    } else if (stage == ShaderStage::FRAGMENT) {
        // 片段着色器：生成后处理输入相关代码
        out << SHADERS_POST_PROCESS_INPUTS_FS_DATA;
    }
    return out;
}

// 生成后处理着色器的getter函数
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generatePostProcessGetters(io::sstream& out, ShaderStage stage) {
    // 生成公共getter函数
    out << SHADERS_COMMON_GETTERS_GLSL_DATA;
    if (stage == ShaderStage::VERTEX) {
        // 顶点着色器：生成后处理getter函数
        out << SHADERS_POST_PROCESS_GETTERS_VS_DATA;
    } else if (stage == ShaderStage::FRAGMENT) {
        // 片段着色器不需要额外的getter函数
    }
    return out;
}

// 生成表面着色器的getter函数
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceGetters(io::sstream& out, ShaderStage stage) {
    // 生成公共getter函数
    out << SHADERS_COMMON_GETTERS_GLSL_DATA;
    switch (stage) {
        case ShaderStage::VERTEX:
            // 顶点着色器：生成表面getter函数
            out << SHADERS_SURFACE_GETTERS_VS_DATA;
            break;
        case ShaderStage::FRAGMENT:
            // 片段着色器：生成表面getter函数
            out << SHADERS_SURFACE_GETTERS_FS_DATA;
            break;
        case ShaderStage::COMPUTE:
            // 计算着色器：生成表面getter函数
            out << SHADERS_SURFACE_GETTERS_CS_DATA;
            break;
    }
    return out;
}

// 生成表面着色器的着色参数代码
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceParameters(io::sstream& out, ShaderStage stage) {
    if (stage == ShaderStage::FRAGMENT) {
        // 片段着色器：生成着色参数相关代码
        out << SHADERS_SURFACE_SHADING_PARAMETERS_FS_DATA;
    }
    return out;
}

// 生成表面着色器的光照代码（有光照）
// @param out 输出流
// @param stage 着色器阶段
// @param variant 着色器变体
// @param shading 着色模型
// @param customSurfaceShading 是否使用自定义表面着色
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceLit(io::sstream& out, ShaderStage stage,
        filament::Variant variant, Shading shading, bool customSurfaceShading) {
    if (stage == ShaderStage::FRAGMENT) {
        // 生成基础光照代码
        out << SHADERS_SURFACE_LIGHTING_FS_DATA;
        // 如果是阴影接收变体，生成阴影相关代码
        if (filament::Variant::isShadowReceiverVariant(variant)) {
            out << SHADERS_SURFACE_SHADOWING_FS_DATA;
        }

        // the only reason we have this assert here is that we used to have a check,
        // which seemed unnecessary.
        // 我们在这里有这个断言的原因是，我们曾经有一个检查，这似乎是不必要的。
        assert_invariant(shading != Shading::UNLIT);

        // 生成BRDF相关代码
        out << SHADERS_SURFACE_BRDF_FS_DATA;
        // 根据着色模型生成相应的着色代码
        switch (shading) {
            case Shading::UNLIT:
                // can't happen
                // 不会发生
                break;
            case Shading::SPECULAR_GLOSSINESS:
            case Shading::LIT:
                // 标准光照或镜面光泽度：根据是否自定义选择着色代码
                if (customSurfaceShading) {
                    out << SHADERS_SURFACE_SHADING_LIT_CUSTOM_FS_DATA;
                } else {
                    out << SHADERS_SURFACE_SHADING_MODEL_STANDARD_FS_DATA;
                }
                break;
            case Shading::SUBSURFACE:
                // 次表面散射着色模型
                out << SHADERS_SURFACE_SHADING_MODEL_SUBSURFACE_FS_DATA;
                break;
            case Shading::CLOTH:
                // 布料着色模型
                out << SHADERS_SURFACE_SHADING_MODEL_CLOTH_FS_DATA;
                break;
        }

        // 生成环境光遮蔽和间接光照代码
        out << SHADERS_SURFACE_AMBIENT_OCCLUSION_FS_DATA;
        out << SHADERS_SURFACE_LIGHT_INDIRECT_FS_DATA;

        // 如果有方向光，生成方向光相关代码
        if (variant.hasDirectionalLighting()) {
            out << SHADERS_SURFACE_LIGHT_DIRECTIONAL_FS_DATA;
        }
        if (variant.hasDynamicLighting()) {
            out << SHADERS_SURFACE_LIGHT_PUNCTUAL_FS_DATA;
        }

        out << SHADERS_SURFACE_SHADING_LIT_FS_DATA;
    }
    return out;
}

// 生成表面着色器的无光照代码（无光照）
// @param out 输出流
// @param stage 着色器阶段
// @param variant 着色器变体
// @param hasShadowMultiplier 是否有阴影乘数
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceUnlit(io::sstream& out, ShaderStage stage,
        filament::Variant variant, bool hasShadowMultiplier) {
    if (stage == ShaderStage::FRAGMENT) {
        // 如果有阴影乘数且是阴影接收变体，生成阴影相关代码
        if (hasShadowMultiplier) {
            if (filament::Variant::isShadowReceiverVariant(variant)) {
                out << SHADERS_SURFACE_SHADOWING_FS_DATA;
            }
        }
        // 生成无光照着色代码
        out << SHADERS_SURFACE_SHADING_UNLIT_FS_DATA;
    }
    return out;
}

// 生成表面着色器的反射代码（屏幕空间反射）
// @param out 输出流
// @param stage 着色器阶段
// @return 输出流引用
io::sstream& CodeGenerator::generateSurfaceReflections(utils::io::sstream& out,
        ShaderStage stage) {
    if (stage == ShaderStage::FRAGMENT) {
        // 片段着色器：生成光照、反射和着色反射相关代码
        out << SHADERS_SURFACE_LIGHTING_FS_DATA;
        out << SHADERS_SURFACE_LIGHT_REFLECTIONS_FS_DATA;
        out << SHADERS_SURFACE_SHADING_REFLECTIONS_FS_DATA;
    }
    return out;
}

/* static */
// 获取材质属性的常量名称（用于预处理器定义）
// @param property 材质属性枚举值
// @return 属性名称字符串（如"BASE_COLOR"、"ROUGHNESS"等）
char const* CodeGenerator::getConstantName(MaterialBuilder::Property property) noexcept {
    using Property = MaterialBuilder::Property;
    // 根据属性类型返回对应的常量名称
    switch (property) {
        case Property::BASE_COLOR:                  return "BASE_COLOR";
        case Property::ROUGHNESS:                   return "ROUGHNESS";
        case Property::METALLIC:                    return "METALLIC";
        case Property::REFLECTANCE:                 return "REFLECTANCE";
        case Property::AMBIENT_OCCLUSION:           return "AMBIENT_OCCLUSION";
        case Property::CLEAR_COAT:                  return "CLEAR_COAT";
        case Property::CLEAR_COAT_ROUGHNESS:        return "CLEAR_COAT_ROUGHNESS";
        case Property::CLEAR_COAT_NORMAL:           return "CLEAR_COAT_NORMAL";
        case Property::ANISOTROPY:                  return "ANISOTROPY";
        case Property::ANISOTROPY_DIRECTION:        return "ANISOTROPY_DIRECTION";
        case Property::THICKNESS:                   return "THICKNESS";
        case Property::SUBSURFACE_POWER:            return "SUBSURFACE_POWER";
        case Property::SUBSURFACE_COLOR:            return "SUBSURFACE_COLOR";
        case Property::SHEEN_COLOR:                 return "SHEEN_COLOR";
        case Property::SHEEN_ROUGHNESS:             return "SHEEN_ROUGHNESS";
        case Property::GLOSSINESS:                  return "GLOSSINESS";
        case Property::SPECULAR_COLOR:              return "SPECULAR_COLOR";
        case Property::EMISSIVE:                    return "EMISSIVE";
        case Property::NORMAL:                      return "NORMAL";
        case Property::POST_LIGHTING_COLOR:         return "POST_LIGHTING_COLOR";
        case Property::POST_LIGHTING_MIX_FACTOR:    return "POST_LIGHTING_MIX_FACTOR";
        case Property::CLIP_SPACE_TRANSFORM:        return "CLIP_SPACE_TRANSFORM";
        case Property::ABSORPTION:                  return "ABSORPTION";
        case Property::TRANSMISSION:                return "TRANSMISSION";
        case Property::IOR:                         return "IOR";
        case Property::DISPERSION:                  return "DISPERSION";
        case Property::MICRO_THICKNESS:             return "MICRO_THICKNESS";
        case Property::BENT_NORMAL:                 return "BENT_NORMAL";
        case Property::SPECULAR_FACTOR:             return "SPECULAR_FACTOR";
        case Property::SPECULAR_COLOR_FACTOR:       return "SPECULAR_COLOR_FACTOR";
        case Property::SHADOW_STRENGTH:             return "SHADOW_STRENGTH";
    }
}

// 获取统一变量类型的GLSL类型名称
// @param type 统一变量类型枚举值
// @return GLSL类型名称字符串（如"float"、"vec3"、"mat4"等）
char const* CodeGenerator::getTypeName(UniformType type) noexcept {
    // 根据类型返回对应的GLSL类型名称
    switch (type) {
        case UniformType::BOOL:   return "bool";
        case UniformType::BOOL2:  return "bvec2";
        case UniformType::BOOL3:  return "bvec3";
        case UniformType::BOOL4:  return "bvec4";
        case UniformType::FLOAT:  return "float";
        case UniformType::FLOAT2: return "vec2";
        case UniformType::FLOAT3: return "vec3";
        case UniformType::FLOAT4: return "vec4";
        case UniformType::INT:    return "int";
        case UniformType::INT2:   return "ivec2";
        case UniformType::INT3:   return "ivec3";
        case UniformType::INT4:   return "ivec4";
        case UniformType::UINT:   return "uint";
        case UniformType::UINT2:  return "uvec2";
        case UniformType::UINT3:  return "uvec3";
        case UniformType::UINT4:  return "uvec4";
        case UniformType::MAT3:   return "mat3";
        case UniformType::MAT4:   return "mat4";
        case UniformType::STRUCT: return "";
    }
}

// 获取统一变量字段的类型名称（如果是结构体则返回结构体名称，否则返回GLSL类型名称）
// @param info 缓冲区接口块字段信息
// @return 类型名称字符串
char const* CodeGenerator::getUniformTypeName(BufferInterfaceBlock::FieldInfo const& info) noexcept {
    using Type = BufferInterfaceBlock::Type;
    switch (info.type) {
        case Type::STRUCT: 
            // 结构体类型：返回结构体名称
            return info.structName.c_str();
        default:            
            // 其他类型：返回GLSL类型名称
            return getTypeName(info.type);
    }
}

// 获取输出类型的GLSL类型名称
// @param type 输出类型枚举值
// @return GLSL类型名称字符串（如"float"、"vec2"、"vec3"、"vec4"）
char const* CodeGenerator::getOutputTypeName(MaterialBuilder::OutputType type) noexcept {
    // 根据输出类型返回对应的GLSL类型名称
    switch (type) {
        case MaterialBuilder::OutputType::FLOAT:  return "float";
        case MaterialBuilder::OutputType::FLOAT2: return "vec2";
        case MaterialBuilder::OutputType::FLOAT3: return "vec3";
        case MaterialBuilder::OutputType::FLOAT4: return "vec4";
    }
}

// 获取采样器类型的GLSL类型名称（根据采样器类型、格式和多重采样标志）
// @param type 采样器类型（2D、3D、立方体贴图等）
// @param format 采样器格式（FLOAT、INT、UINT、SHADOW）
// @param multisample 是否多重采样
// @return GLSL采样器类型名称字符串（如"sampler2D"、"isampler2DMS"、"sampler2DShadow"等）
char const* CodeGenerator::getSamplerTypeName(SamplerType type, SamplerFormat format,
        bool multisample) const noexcept {
    // 根据采样器类型、格式和多重采样标志组合生成GLSL类型名称
    switch (type) {
        case SamplerType::SAMPLER_2D:
            switch (format) {
                case SamplerFormat::INT:    return multisample ? "isampler2DMS" : "isampler2D";
                case SamplerFormat::UINT:   return multisample ? "usampler2DMS" : "usampler2D";
                case SamplerFormat::FLOAT:  return multisample ? "sampler2DMS" : "sampler2D";
                case SamplerFormat::SHADOW: return "sampler2DShadow";
            }
        case SamplerType::SAMPLER_3D:
            assert(format != SamplerFormat::SHADOW);
            switch (format) {
                case SamplerFormat::INT:    return "isampler3D";
                case SamplerFormat::UINT:   return "usampler3D";
                case SamplerFormat::FLOAT:  return "sampler3D";
                case SamplerFormat::SHADOW: return nullptr;
            }
        case SamplerType::SAMPLER_2D_ARRAY:
            switch (format) {
                case SamplerFormat::INT:    return multisample ? "isampler2DMSArray": "isampler2DArray";
                case SamplerFormat::UINT:   return multisample ? "usampler2DMSArray": "usampler2DArray";
                case SamplerFormat::FLOAT:  return multisample ? "sampler2DMSArray": "sampler2DArray";
                case SamplerFormat::SHADOW: return "sampler2DArrayShadow";
            }
        case SamplerType::SAMPLER_CUBEMAP:
            switch (format) {
                case SamplerFormat::INT:    return "isamplerCube";
                case SamplerFormat::UINT:   return "usamplerCube";
                case SamplerFormat::FLOAT:  return "samplerCube";
                case SamplerFormat::SHADOW: return "samplerCubeShadow";
            }
        case SamplerType::SAMPLER_EXTERNAL:
            assert(format != SamplerFormat::SHADOW);
            // Vulkan doesn't have external textures in the sense as GL. Vulkan external textures
            // are created via VK_ANDROID_external_memory_android_hardware_buffer, but they are
            // backed by VkImage just like a normal texture, and sampled from normally.
            // Vulkan没有像GL那样的外部纹理。Vulkan外部纹理通过VK_ANDROID_external_memory_android_hardware_buffer创建，
            // 但它们像普通纹理一样由VkImage支持，并正常采样。
            // SPIR-V目标使用sampler2D，其他使用samplerExternalOES
            return (mTargetLanguage == TargetLanguage::SPIRV) ? "sampler2D" : "samplerExternalOES";
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            switch (format) {
                case SamplerFormat::INT:    return "isamplerCubeArray";
                case SamplerFormat::UINT:   return "usamplerCubeArray";
                case SamplerFormat::FLOAT:  return "samplerCubeArray";
                case SamplerFormat::SHADOW: return "samplerCubeArrayShadow";
            }
    }
}

// 获取插值限定符字符串
// @param interpolation 插值模式枚举值
// @return 插值限定符字符串（SMOOTH返回空字符串，FLAT返回"flat "）
char const* CodeGenerator::getInterpolationQualifier(Interpolation interpolation) noexcept {
    // 根据插值模式返回对应的限定符
    switch (interpolation) {
        case Interpolation::SMOOTH: 
            // 平滑插值（默认），不需要限定符
            return "";
        case Interpolation::FLAT:   
            // 平面插值，需要"flat"限定符
            return "flat ";
    }
}

/* static */
// 获取精度限定符字符串
// @param precision 精度枚举值
// @return 精度限定符字符串（LOW="lowp", MEDIUM="mediump", HIGH="highp", DEFAULT=""）
char const* CodeGenerator::getPrecisionQualifier(Precision precision) noexcept {
    // 根据精度返回对应的限定符
    switch (precision) {
        case Precision::LOW:     
            return "lowp";
        case Precision::MEDIUM:  
            return "mediump";
        case Precision::HIGH:    
            return "highp";
        case Precision::DEFAULT: 
            // 默认精度不需要限定符
            return "";
    }
}

/* static */
// 判断类型是否支持精度限定符
// @param type 缓冲区接口块类型
// @return 如果类型支持精度限定符返回true，否则返回false
// @note bool类型和结构体类型不支持精度限定符
bool CodeGenerator::hasPrecision(BufferInterfaceBlock::Type type) noexcept {
    // 判断类型是否支持精度限定符
    switch (type) {
        case UniformType::BOOL:
        case UniformType::BOOL2:
        case UniformType::BOOL3:
        case UniformType::BOOL4:
        case UniformType::STRUCT:
            // bool类型和结构体类型不支持精度限定符
            return false;
        default:
            // 其他类型（float、int、uint、mat等）支持精度限定符
            return true;
    }
}

} // namespace filamat
