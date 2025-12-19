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

#ifndef TNT_FILAMENT_ENGINE_ENUM_H
#define TNT_FILAMENT_ENGINE_ENUM_H

#include <backend/DriverEnums.h>

#include <utils/BitmaskEnum.h>
#include <utils/FixedCapacityVector.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

// 后处理变体位数（1位）
static constexpr size_t POST_PROCESS_VARIANT_BITS = 1;
// 后处理变体数量（2的1次方=2）
static constexpr size_t POST_PROCESS_VARIANT_COUNT = (1u << POST_PROCESS_VARIANT_BITS);
// 后处理变体掩码
static constexpr size_t POST_PROCESS_VARIANT_MASK = POST_PROCESS_VARIANT_COUNT - 1;

/**
 * 后处理变体枚举
 */
enum class PostProcessVariant : uint8_t {
    OPAQUE,         // 不透明
    TRANSLUCENT     // 半透明
};

/**
 * 描述符集绑定点枚举
 * 定义不同类型的描述符集
 */
enum class DescriptorSetBindingPoints : uint8_t {
    PER_VIEW        = 0,    // 每视图描述符集
    PER_RENDERABLE  = 1,    // 每个可渲染对象描述符集
    PER_MATERIAL    = 2,    // 每个材质描述符集
};

/**
 * binding point for the "per-view" descriptor set - "每视图"描述符集的绑定点
 * 定义每视图描述符集中各个资源的绑定索引
 */
enum class PerViewBindingPoints : uint8_t  {
    FRAME_UNIFORMS  =  0,   // uniforms updated per view
    SHADOWS         =  1,   // punctual shadow data
    LIGHTS          =  2,   // lights data array
    RECORD_BUFFER   =  3,   // froxel record buffer
    FROXEL_BUFFER   =  4,   // froxel buffer
    STRUCTURE       =  5,   // variable, DEPTH
    SHADOW_MAP      =  6,   // user defined (1024x1024) DEPTH, array
    IBL_DFG_LUT     =  7,   // user defined (128x128), RGB16F
    IBL_SPECULAR    =  8,   // user defined, user defined, CUBEMAP
    SSAO            =  9,   // variable, RGB8 {AO, [depth]}
    SSR             = 10,   // variable, 2d array, RGB_11_11_10, mipmapped
    SSR_HISTORY     = 10,   // variable, 2d texture, RGB_11_11_10
    FOG             = 11    // variable, user defined, CUBEMAP
};

/**
 * 每个可渲染对象的绑定点枚举
 * 定义每个可渲染对象描述符集中各个资源的绑定索引
 */
enum class PerRenderableBindingPoints : uint8_t  {
    OBJECT_UNIFORMS             =  0,   // uniforms updated per renderable - 每个可渲染对象更新的统一变量
    BONES_UNIFORMS              =  1,   // 骨骼统一变量
    MORPHING_UNIFORMS           =  2,   // 变形统一变量
    MORPH_TARGET_POSITIONS      =  3,   // 变形目标位置纹理
    MORPH_TARGET_TANGENTS       =  4,   // 变形目标切线纹理
    BONES_INDICES_AND_WEIGHTS   =  5,   // 骨骼索引和权重纹理
};

/**
 * 每个材质的绑定点枚举
 * 定义每个材质描述符集中各个资源的绑定索引
 */
enum class PerMaterialBindingPoints : uint8_t  {
    MATERIAL_PARAMS             =  0,   // uniforms - 材质参数统一变量
};

/**
 * 保留的特殊化常量枚举
 * Filament为自己使用保留的特殊化常量
 */
enum class ReservedSpecializationConstants : uint8_t {
    BACKEND_FEATURE_LEVEL = 0,
    CONFIG_MAX_INSTANCES = 1,
    CONFIG_STATIC_TEXTURE_TARGET_WORKAROUND = 2,
    CONFIG_SRGB_SWAPCHAIN_EMULATION = 3, // don't change (hardcoded in OpenGLDriver.cpp)
    CONFIG_FROXEL_BUFFER_HEIGHT = 4,
    CONFIG_POWER_VR_SHADER_WORKAROUNDS = 5,
    CONFIG_DEBUG_DIRECTIONAL_SHADOWMAP = 6,
    CONFIG_DEBUG_FROXEL_VISUALIZATION = 7,
    CONFIG_STEREO_EYE_COUNT = 8, // don't change (hardcoded in ShaderCompilerService.cpp)
    CONFIG_SH_BANDS_COUNT = 9,
    CONFIG_SHADOW_SAMPLING_METHOD = 10,
    CONFIG_FROXEL_RECORD_BUFFER_HEIGHT = 11,
    // check CONFIG_NEXT_RESERVED_SPEC_CONSTANT and CONFIG_MAX_RESERVED_SPEC_CONSTANTS below
};

/**
 * 推送常量ID枚举
 * 定义推送常量的标识符
 */
enum class PushConstantIds : uint8_t  {
    MORPHING_BUFFER_OFFSET = 0,  // 变形缓冲区偏移量
};

// number of renderpass channels - 渲染通道通道数
constexpr size_t CONFIG_RENDERPASS_CHANNEL_COUNT = 8;

/**
 * This value is limited by UBO size, ES3.0 only guarantees 16 KiB. - 此值受UBO大小限制，ES3.0仅保证16 KiB
 * It's also limited by the Froxelizer's record buffer data type (uint8_t). - 还受Froxelizer记录缓冲区数据类型(uint8_t)限制
 * And it's limited by the Froxelizer's Froxel data structure, which stores - 还受Froxelizer的Froxel数据结构限制，它使用
 * a light count in a uint8_t (so the count is limited to 255) - uint8_t存储光源数量（因此数量限制为255）
 */
constexpr size_t CONFIG_MAX_LIGHT_COUNT = 255;      // 最大光源数量
constexpr size_t CONFIG_MAX_LIGHT_INDEX = CONFIG_MAX_LIGHT_COUNT - 1;  // 最大光源索引

/**
 * The number of specialization constants that Filament reserves for its own use. These are always - Filament为自己使用保留的特殊化常量数量，这些总是
 * the first constants (from 0 to CONFIG_MAX_RESERVED_SPEC_CONSTANTS - 1). - 前几个常量（从0到CONFIG_MAX_RESERVED_SPEC_CONSTANTS - 1）
 * Updating this value necessitates a material version bump. - 更新此值需要增加材质版本号
 */
constexpr size_t CONFIG_MAX_RESERVED_SPEC_CONSTANTS = 16;
// The number of the next unassigned reserved spec constant. - 下一个未分配的保留特殊化常量编号
constexpr size_t CONFIG_NEXT_RESERVED_SPEC_CONSTANT = 12;

/**
 * The maximum number of shadow maps possible. - 可能的最大阴影贴图数量
 * There is currently a maximum limit of 128 shadow maps. - 当前最大限制为128个阴影贴图
 * Factors contributing to this limit: - 导致此限制的因素：
 * - minspec for UBOs is 16KiB, which currently can hold a maximum of 128 entries - UBO的最小规范是16KiB，当前最多可容纳128个条目
 */
constexpr size_t CONFIG_MAX_SHADOWMAPS = 128;

/**
 * The maximum number of shadow layers. - 最大阴影层数
 * There is currently a maximum limit of 255 layers. - 当前最大限制为255层
 * Several factors are contributing to this limit: - 导致此限制的几个因素：
 * - minspec for 2d texture arrays layer is 256 - 2D纹理数组层的最小规范是256
 * - we're using uint8_t to store the number of layers (255 max) - 我们使用uint8_t存储层数（最大255）
 * - nonsensical to be larger than the number of shadowmaps - 大于阴影贴图数量没有意义
 * - AtlasAllocator depth limits it to 64 - AtlasAllocator深度限制为64
 */
constexpr size_t CONFIG_MAX_SHADOW_LAYERS = 64;

/**
 * The maximum number of shadow cascades that can be used for directional lights. - 可用于方向光的最大阴影级联数量
 */
constexpr size_t CONFIG_MAX_SHADOW_CASCADES = 4;

/**
 * The maximum UBO size, in bytes. This value is set to 16 KiB due to the ES3.0 spec. - 最大UBO大小（字节）。由于ES3.0规范，此值设置为16 KiB
 * Note that this value constrains the maximum number of skinning bones, morph targets, - 注意，此值限制了最大骨骼数量、变形目标、
 * instances, and shadow casting spotlights. - 实例数和投射阴影的聚光灯数量
 */
constexpr size_t CONFIG_MINSPEC_UBO_SIZE = 16384;

/**
 * The maximum number of instances that Filament automatically creates as an optimization. - Filament作为优化自动创建的最大实例数
 * Use a much smaller number for WebGL as a workaround for the following Chrome issues: - 对WebGL使用更小的数字，作为以下Chrome问题的解决方法：
 *     https://crbug.com/1348017 Compiling GLSL is very slow with struct arrays - 使用结构数组编译GLSL非常慢
 *     https://crbug.com/1348363 Lighting looks wrong with D3D11 but not OpenGL - 使用D3D11时光照看起来不对，但OpenGL正常
 * Note that __EMSCRIPTEN__ is not defined when running matc, but that's okay because we're - 注意，运行matc时__EMSCRIPTEN__未定义，但这没关系，因为我们
 * actually using a specification constant. - 实际上使用规范常量
 */
#if defined(__EMSCRIPTEN__)
constexpr size_t CONFIG_MAX_INSTANCES = 8;
#else
constexpr size_t CONFIG_MAX_INSTANCES = 64;
#endif

/**
 * The maximum number of bones that can be associated with a single renderable. - 可以与单个可渲染对象关联的最大骨骼数
 * We store 32 bytes per bone. Must be a power-of-two, and must fit within CONFIG_MINSPEC_UBO_SIZE. - 每个骨骼存储32字节。必须是2的幂，并且必须适合CONFIG_MINSPEC_UBO_SIZE
 */
constexpr size_t CONFIG_MAX_BONE_COUNT = 256;

/**
 * The maximum number of morph targets associated with a single renderable. - 与单个可渲染对象关联的最大变形目标数
 * Note that ES3.0 only guarantees 256 layers in an array texture. - 注意，ES3.0仅保证数组纹理中有256层
 * Furthermore, this is constrained by CONFIG_MINSPEC_UBO_SIZE (16 bytes per morph target). - 此外，这受CONFIG_MINSPEC_UBO_SIZE限制（每个变形目标16字节）
 */
constexpr size_t CONFIG_MAX_MORPH_TARGET_COUNT = 256;

/**
 * The max number of eyes supported in stereoscopic mode. - 立体模式支持的最大眼睛数
 * The number of eyes actually rendered is set at Engine creation time, see - 实际渲染的眼睛数在Engine创建时设置，参见
 * Engine::Config::stereoscopicEyeCount. - Engine::Config::stereoscopicEyeCount
 */
constexpr uint8_t CONFIG_MAX_STEREOSCOPIC_EYES = 4;

} // namespace filament

template<>
struct utils::EnableIntegerOperators<filament::DescriptorSetBindingPoints> : public std::true_type {};
template<>
struct utils::EnableIntegerOperators<filament::PerViewBindingPoints> : public std::true_type {};
template<>
struct utils::EnableIntegerOperators<filament::PerRenderableBindingPoints> : public std::true_type {};
template<>
struct utils::EnableIntegerOperators<filament::PerMaterialBindingPoints> : public std::true_type {};

template<>
struct utils::EnableIntegerOperators<filament::ReservedSpecializationConstants> : public std::true_type {};
template<>
struct utils::EnableIntegerOperators<filament::PushConstantIds> : public std::true_type {};
template<>
struct utils::EnableIntegerOperators<filament::PostProcessVariant> : public std::true_type {};

template<>
inline constexpr size_t utils::Enum::count<filament::PostProcessVariant>() { return filament::POST_PROCESS_VARIANT_COUNT; }

#endif // TNT_FILAMENT_ENGINE_ENUM_H
