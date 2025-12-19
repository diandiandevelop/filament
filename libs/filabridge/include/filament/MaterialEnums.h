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

//! \file

#ifndef TNT_FILAMENT_MATERIAL_ENUM_H
#define TNT_FILAMENT_MATERIAL_ENUM_H

#include <utils/bitset.h>
#include <utils/BitmaskEnum.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

// update this when a new version of filament wouldn't work with older materials
// 当新版本的Filament不能与旧材质兼容时，需要更新此版本号
static constexpr size_t MATERIAL_VERSION = 68;

/**
 * Supported shading models
 * 支持的着色模型
 */
enum class Shading : uint8_t {
    UNLIT,                  //!< no lighting applied, emissive possible - 不应用光照，可发射光
    LIT,                    //!< default, standard lighting - 默认，标准光照
    SUBSURFACE,             //!< subsurface lighting model - 次表面光照模型
    CLOTH,                  //!< cloth lighting model - 布料光照模型
    SPECULAR_GLOSSINESS,    //!< legacy lighting model - 传统光照模型
};

/**
 * Attribute interpolation types in the fragment shader
 * 片段着色器中的属性插值类型
 */
enum class Interpolation : uint8_t {
    SMOOTH,                 //!< default, smooth interpolation - 默认，平滑插值
    FLAT                    //!< flat interpolation - 平坦插值
};

/**
 * Shader quality, affect some global quality parameters
 * 着色器质量，影响某些全局质量参数
 */
enum class ShaderQuality : int8_t {
    DEFAULT = -1,   // LOW on mobile, HIGH on desktop - 移动设备为LOW，桌面为HIGH
    LOW     = 0,    // enable optimizations that can slightly affect correctness - 启用可能略微影响正确性的优化
    NORMAL  = 1,    // normal quality, correctness honored - 正常质量，保证正确性
    HIGH    = 2     // higher quality (e.g. better upscaling, etc...) - 更高质量（例如更好的上采样等）
};

/**
 * Supported blending modes
 * 支持的混合模式
 */
enum class BlendingMode : uint8_t {
    //! material is opaque - 材质不透明
    OPAQUE,
    //! material is transparent and color is alpha-pre-multiplied, affects diffuse lighting only - 材质透明且颜色为预乘alpha，仅影响漫反射光照
    TRANSPARENT,
    //! material is additive (e.g.: hologram) - 材质为加法混合（例如：全息图）
    ADD,
    //! material is masked (i.e. alpha tested) - 材质为遮罩（即alpha测试）
    MASKED,
    /**
     * material is transparent and color is alpha-pre-multiplied, affects specular lighting - 材质透明且颜色为预乘alpha，影响镜面反射光照
     * when adding more entries, change the size of FRenderer::CommandKey::blending - 添加更多条目时，更改FRenderer::CommandKey::blending的大小
     */
    FADE,
    //! material darkens what's behind it - 材质使后面的内容变暗
    MULTIPLY,
    //! material brightens what's behind it - 材质使后面的内容变亮
    SCREEN,
    //! custom blending function - 自定义混合函数
    CUSTOM,
};

/**
 * How transparent objects are handled
 * 如何处理透明对象
 */
enum class TransparencyMode : uint8_t {
    //! the transparent object is drawn honoring the raster state - 透明对象按照光栅状态绘制
    DEFAULT,
    /**
     * the transparent object is first drawn in the depth buffer, - 透明对象首先在深度缓冲区中绘制
     * then in the color buffer, honoring the culling mode, but ignoring the depth test function - 然后在颜色缓冲区中绘制，遵守剔除模式，但忽略深度测试函数
     */
    TWO_PASSES_ONE_SIDE,

    /**
     * the transparent object is drawn twice in the color buffer, - 透明对象在颜色缓冲区中绘制两次
     * first with back faces only, then with front faces; the culling - 首先仅绘制背面，然后绘制正面；剔除
     * mode is ignored. Can be combined with two-sided lighting - 模式被忽略。可以与双面光照结合使用
     */
    TWO_PASSES_TWO_SIDES
};

/**
 * Supported types of vertex domains.
 * 支持的顶点域类型
 */
enum class VertexDomain : uint8_t {
    OBJECT,                 //!< vertices are in object space, default - 顶点在对象空间中，默认
    WORLD,                  //!< vertices are in world space - 顶点在世界空间中
    VIEW,                   //!< vertices are in view space - 顶点在视图空间中
    DEVICE                  //!< vertices are in normalized device space - 顶点在归一化设备空间中
    // when adding more entries, make sure to update VERTEX_DOMAIN_COUNT - 添加更多条目时，确保更新VERTEX_DOMAIN_COUNT
};

/**
 * Vertex attribute types
 * 顶点属性类型
 */
enum VertexAttribute : uint8_t {
    // Update hasIntegerTarget() in VertexBuffer when adding an attribute that will - 添加将在着色器中作为整数读取的属性时，更新VertexBuffer中的hasIntegerTarget()
    // be read as integers in the shaders - 

    POSITION        = 0, //!< XYZ position (float3) - XYZ位置（float3）
    TANGENTS        = 1, //!< tangent, bitangent and normal, encoded as a quaternion (float4) - 切线、副切线和法线，编码为四元数（float4）
    COLOR           = 2, //!< vertex color (float4) - 顶点颜色（float4）
    UV0             = 3, //!< texture coordinates (float2) - 纹理坐标（float2）
    UV1             = 4, //!< texture coordinates (float2) - 纹理坐标（float2）
    BONE_INDICES    = 5, //!< indices of 4 bones, as unsigned integers (uvec4) - 4个骨骼的索引，作为无符号整数（uvec4）
    BONE_WEIGHTS    = 6, //!< weights of the 4 bones (normalized float4) - 4个骨骼的权重（归一化float4）
    // -- we have 1 unused slot here -- - 这里有一个未使用的槽位
    CUSTOM0         = 8,  // 自定义属性0
    CUSTOM1         = 9,  // 自定义属性1
    CUSTOM2         = 10, // 自定义属性2
    CUSTOM3         = 11, // 自定义属性3
    CUSTOM4         = 12, // 自定义属性4
    CUSTOM5         = 13, // 自定义属性5
    CUSTOM6         = 14, // 自定义属性6
    CUSTOM7         = 15, // 自定义属性7

    // Aliases for legacy vertex morphing. - 传统顶点变形的别名
    // See RenderableManager::Builder::morphing(). - 参见RenderableManager::Builder::morphing()
    MORPH_POSITION_0 = CUSTOM0,  // 变形位置0（传统顶点变形）
    MORPH_POSITION_1 = CUSTOM1,  // 变形位置1（传统顶点变形）
    MORPH_POSITION_2 = CUSTOM2,  // 变形位置2（传统顶点变形）
    MORPH_POSITION_3 = CUSTOM3,  // 变形位置3（传统顶点变形）
    MORPH_TANGENTS_0 = CUSTOM4,  // 变形切线0（传统顶点变形）
    MORPH_TANGENTS_1 = CUSTOM5,  // 变形切线1（传统顶点变形）
    MORPH_TANGENTS_2 = CUSTOM6,  // 变形切线2（传统顶点变形）
    MORPH_TANGENTS_3 = CUSTOM7,  // 变形切线3（传统顶点变形）

    // this is limited by driver::MAX_VERTEX_ATTRIBUTE_COUNT - 这受driver::MAX_VERTEX_ATTRIBUTE_COUNT限制
};

// 传统顶点变形的最大目标数量
static constexpr size_t MAX_LEGACY_MORPH_TARGETS = 4;
// this is limited by filament::CONFIG_MAX_MORPH_TARGET_COUNT - 这受filament::CONFIG_MAX_MORPH_TARGET_COUNT限制
static constexpr size_t MAX_MORPH_TARGETS = 256;
// 最大自定义属性数量
static constexpr size_t MAX_CUSTOM_ATTRIBUTES = 8;

/**
 * Material domains
 * 材质域
 */
enum class MaterialDomain : uint8_t {
    SURFACE         = 0, //!< shaders applied to renderables - 应用于可渲染对象的着色器
    POST_PROCESS    = 1, //!< shaders applied to rendered buffers - 应用于已渲染缓冲区的着色器
    COMPUTE         = 2, //!< compute shader - 计算着色器
};

/**
 * Specular occlusion
 * 镜面反射环境光遮蔽
 */
enum class SpecularAmbientOcclusion : uint8_t {
    NONE            = 0, //!< no specular occlusion - 无镜面反射环境光遮蔽
    SIMPLE          = 1, //!< simple specular occlusion - 简单镜面反射环境光遮蔽
    BENT_NORMALS    = 2, //!< more accurate specular occlusion, requires bent normals - 更准确的镜面反射环境光遮蔽，需要弯曲法线
};

/**
 * Refraction
 * 折射
 */
enum class RefractionMode : uint8_t {
    NONE            = 0, //!< no refraction - 无折射
    CUBEMAP         = 1, //!< refracted rays go to the ibl cubemap - 折射光线进入IBL立方体贴图
    SCREEN_SPACE    = 2, //!< refracted rays go to screen space - 折射光线进入屏幕空间
};

/**
 * Refraction type
 * 折射类型
 */
enum class RefractionType : uint8_t {
    SOLID           = 0, //!< refraction through solid objects (e.g. a sphere) - 通过固体对象的折射（例如球体）
    THIN            = 1, //!< refraction through thin objects (e.g. window) - 通过薄对象的折射（例如窗户）
};

/**
 * Reflection mode
 * 反射模式
 */
enum class ReflectionMode : uint8_t {
    DEFAULT         = 0, //! reflections sample from the scene's IBL only - 反射仅从场景的IBL采样
    SCREEN_SPACE    = 1, //! reflections sample from screen space, and fallback to the scene's IBL - 反射从屏幕空间采样，并回退到场景的IBL
};

// can't really use std::underlying_type<AttributeIndex>::type because the driver takes a uint32_t
// 不能真正使用std::underlying_type<AttributeIndex>::type，因为驱动程序接受uint32_t
// 顶点属性位集合类型，用于表示顶点属性的集合
using AttributeBitset = utils::bitset32;

// 材质属性数量
static constexpr size_t MATERIAL_PROPERTIES_COUNT = 31;
/**
 * Material properties
 * 材质属性
 */
enum class Property : uint8_t {
    BASE_COLOR,              //!< float4, all shading models - float4，所有着色模型
    ROUGHNESS,               //!< float,  lit shading models only - float，仅光照着色模型
    METALLIC,                //!< float,  all shading models, except unlit and cloth - float，所有着色模型（除了无光照和布料）
    REFLECTANCE,             //!< float,  all shading models, except unlit and cloth - float，所有着色模型（除了无光照和布料）
    AMBIENT_OCCLUSION,       //!< float,  lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    CLEAR_COAT,              //!< float,  lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    CLEAR_COAT_ROUGHNESS,    //!< float,  lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    CLEAR_COAT_NORMAL,       //!< float,  lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    ANISOTROPY,              //!< float,  lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    ANISOTROPY_DIRECTION,    //!< float3, lit shading models only, except subsurface and cloth - float3，仅光照着色模型（除了次表面和布料）
    THICKNESS,               //!< float,  subsurface shading model only - float，仅次表面着色模型
    SUBSURFACE_POWER,        //!< float,  subsurface shading model only - float，仅次表面着色模型
    SUBSURFACE_COLOR,        //!< float3, subsurface and cloth shading models only - float3，仅次表面和布料着色模型
    SHEEN_COLOR,             //!< float3, lit shading models only, except subsurface - float3，仅光照着色模型（除了次表面）
    SHEEN_ROUGHNESS,         //!< float3, lit shading models only, except subsurface and cloth - float3，仅光照着色模型（除了次表面和布料）
    SPECULAR_COLOR,          //!< float3, specular-glossiness shading model only - float3，仅镜面-光泽度着色模型
    GLOSSINESS,              //!< float,  specular-glossiness shading model only - float，仅镜面-光泽度着色模型
    EMISSIVE,                //!< float4, all shading models - float4，所有着色模型
    NORMAL,                  //!< float3, all shading models only, except unlit - float3，所有着色模型（除了无光照）
    POST_LIGHTING_COLOR,     //!< float4, all shading models - float4，所有着色模型
    POST_LIGHTING_MIX_FACTOR,//!< float, all shading models - float，所有着色模型
    CLIP_SPACE_TRANSFORM,    //!< mat4,   vertex shader only - mat4，仅顶点着色器
    ABSORPTION,              //!< float3, how much light is absorbed by the material - float3，材质吸收的光量
    TRANSMISSION,            //!< float,  how much light is refracted through the material - float，通过材质折射的光量
    IOR,                     //!< float,  material's index of refraction - float，材质的折射率
    DISPERSION,              //!< float,  material's dispersion - float，材质的色散
    MICRO_THICKNESS,         //!< float, thickness of the thin layer - float，薄层厚度
    BENT_NORMAL,             //!< float3, all shading models only, except unlit - float3，所有着色模型（除了无光照）
    SPECULAR_FACTOR,         //!< float, lit shading models only, except subsurface and cloth - float，仅光照着色模型（除了次表面和布料）
    SPECULAR_COLOR_FACTOR,   //!< float3, lit shading models only, except subsurface and cloth - float3，仅光照着色模型（除了次表面和布料）
    SHADOW_STRENGTH,         //!< float, [0, 1] strength of shadows received by this material - float，[0, 1]此材质接收的阴影强度

    // when adding new Properties, make sure to update MATERIAL_PROPERTIES_COUNT - 添加新属性时，确保更新MATERIAL_PROPERTIES_COUNT
};

// 用户变体过滤器掩码类型，用于指定哪些变体位应该被过滤
using UserVariantFilterMask = uint32_t;

/**
 * User variant filter bits
 * 用户变体过滤器位
 */
enum class UserVariantFilterBit : UserVariantFilterMask {
    DIRECTIONAL_LIGHTING        = 0x01,         //!< Directional lighting - 方向光
    DYNAMIC_LIGHTING            = 0x02,         //!< Dynamic lighting - 动态光源
    SHADOW_RECEIVER             = 0x04,         //!< Shadow receiver - 阴影接收
    SKINNING                    = 0x08,         //!< Skinning - 蒙皮
    FOG                         = 0x10,         //!< Fog - 雾效
    VSM                         = 0x20,         //!< Variance shadow maps - 方差阴影贴图
    SSR                         = 0x40,         //!< Screen-space reflections - 屏幕空间反射
    STE                         = 0x80,         //!< Instanced stereo rendering - 实例化立体渲染
    ALL                         = 0xFF,         // 所有位（0xFF）
};

} // namespace filament

// 为UserVariantFilterBit启用位掩码操作符（如 |, &, ^等）
// Enable bitmask operators (e.g., |, &, ^) for UserVariantFilterBit
template<> struct utils::EnableBitMaskOperators<filament::UserVariantFilterBit>
        : public std::true_type {};

// 为UserVariantFilterBit启用整数操作符
// Enable integer operators for UserVariantFilterBit
template<> struct utils::EnableIntegerOperators<filament::UserVariantFilterBit>
        : public std::true_type {};

#endif
