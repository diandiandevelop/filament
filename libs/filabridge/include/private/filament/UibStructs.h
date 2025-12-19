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

#ifndef TNT_FILABRIDGE_UIBSTRUCTS_H
#define TNT_FILABRIDGE_UIBSTRUCTS_H

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec4.h>

#include <private/filament/EngineEnums.h>

#include <array>
#include <string_view>

/*
 * Here we define all the UBOs known by filament as C structs. It is used by filament to - 这里我们将Filament已知的所有UBO定义为C结构体。Filament使用它来
 * fill the uniform values and get the interface block names. It is also used by filabridge to - 填充统一变量值并获取接口块名称。filabridge也使用它来
 * get the interface block names. - 获取接口块名称
 */

namespace filament {

/**
 * std140命名空间
 * 定义了符合std140布局规则的类型（用于UBO布局）
 */
namespace std140 {

// std140对齐的vec3类型（16字节对齐）
struct alignas(16) vec3 : public std::array<float, 3> {};
// std140对齐的vec4类型（16字节对齐）
struct alignas(16) vec4 : public std::array<float, 4> {};

/**
 * std140对齐的3x3矩阵类型
 */
struct mat33 : public std::array<vec3, 3> {
    // passing by value informs the compiler that rhs != *this - 按值传递告知编译器rhs != *this
    mat33& operator=(math::mat3f const rhs) noexcept {
        // 逐行复制矩阵元素
        for (int i = 0; i < 3; i++) {
            (*this)[i][0] = rhs[i][0];
            (*this)[i][1] = rhs[i][1];
            (*this)[i][2] = rhs[i][2];
        }
        return *this;
    }
};

/**
 * std140对齐的4x4矩阵类型
 */
struct mat44 : public std::array<vec4, 4> {
    // passing by value informs the compiler that rhs != *this - 按值传递告知编译器rhs != *this
    mat44& operator=(math::mat4f const rhs) noexcept {
        // 逐行复制矩阵元素
        for (int i = 0; i < 4; i++) {
            (*this)[i][0] = rhs[i][0];
            (*this)[i][1] = rhs[i][1];
            (*this)[i][2] = rhs[i][2];
            (*this)[i][3] = rhs[i][3];
        }
        return *this;
    }
};

} // std140
/*
 * IMPORTANT NOTE: Respect std140 layout, don't update without updating UibGenerator::get{*}Uib() - 重要提示：遵守std140布局，不要在不更新UibGenerator::get{*}Uib()的情况下更新
 */

/**
 * 每视图统一缓冲区（UBO）结构
 * 包含所有每视图的统一变量数据（相机矩阵、光照、阴影等）
 */
struct PerViewUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "FrameUniforms" };  // 接口块名称

    // --------------------------------------------------------------------------------------------
    // Values that can be accessed in both surface and post-process materials - 表面材质和后处理材质都可以访问的值
    // --------------------------------------------------------------------------------------------

    math::mat4f viewFromWorldMatrix;    // clip    view <- world    : view matrix - 视图矩阵（世界空间到视图空间）
    math::mat4f worldFromViewMatrix;    // clip    view -> world    : model matrix - 世界矩阵（视图空间到世界空间）
    math::mat4f clipFromViewMatrix;     // clip <- view    world    : projection matrix - 投影矩阵（视图空间到裁剪空间）
    math::mat4f viewFromClipMatrix;     // clip -> view    world    : inverse projection matrix - 逆投影矩阵（裁剪空间到视图空间）
    math::mat4f eyeFromViewMatrix[CONFIG_MAX_STEREOSCOPIC_EYES];   // clip    eye  <- view    world - 眼睛矩阵数组（视图空间到眼睛空间，用于立体渲染）
    math::mat4f clipFromWorldMatrix[CONFIG_MAX_STEREOSCOPIC_EYES]; // clip <- eye  <- view <- world - 裁剪矩阵数组（世界空间到裁剪空间，用于立体渲染）
    math::mat4f worldFromClipMatrix;    // clip -> view -> world - 逆变换矩阵（裁剪空间到世界空间）
    math::mat4f userWorldFromWorldMatrix;   // userWorld <- world - 用户世界空间到世界空间的变换矩阵
    math::float4 clipTransform;             // [sx, sy, tx, ty] only used by VERTEX_DOMAIN_DEVICE - 裁剪变换[sx, sy, tx, ty]，仅用于VERTEX_DOMAIN_DEVICE

    // --------------------------------------------------------------------------------------------

    math::float2 clipControl;       // clip control - 裁剪控制参数
    float time;                     // time in seconds, with a 1-second period - 时间（秒），1秒周期
    float temporalNoise;            // noise [0,1] when TAA is used, 0 otherwise - 时间噪声[0,1]，使用TAA时有效，否则为0
    math::float4 userTime;          // time(s), (double)time - (float)time, 0, 0 - 用户时间(s)，(double)time - (float)time, 0, 0

    // --------------------------------------------------------------------------------------------
    // values below should only be accessed in surface materials - 以下值应仅在表面材质中访问
    // (i.e.: not in the post-processing materials) - （即：不在后处理材质中）
    // --------------------------------------------------------------------------------------------

    math::float4 resolution;        // physical viewport width, height, 1/width, 1/height - 物理视口宽度、高度、1/宽度、1/高度
    math::float2 logicalViewportScale;  // scale-factor to go from physical to logical viewport - 从物理视口到逻辑视口的缩放因子
    math::float2 logicalViewportOffset; // offset to go from physical to logical viewport - 从物理视口到逻辑视口的偏移量

    float lodBias;                  // load bias to apply to user materials - 应用于用户材质的LOD偏差
    float refractionLodOffset;      // 折射LOD偏移
    math::float2 derivativesScale;  // 导数缩放

    // camera position in view space (when camera_at_origin is enabled), i.e. it's (0,0,0). - 视图空间中的相机位置（启用camera_at_origin时），即(0,0,0)
    float oneOverFarMinusNear;      // 1 / (f-n), always positive - 1/(远平面-近平面)，始终为正
    float nearOverFarMinusNear;     // n / (f-n), always positive - 近平面/(远平面-近平面)，始终为正
    float cameraFar;                // camera *culling* far-plane distance, always positive (projection far is at +inf) - 相机剔除远平面距离，始终为正（投影远平面在+inf）
    float exposure;                 // 曝光值
    float ev100;                    // EV100值
    float needsAlphaChannel;        // 是否需要Alpha通道

    // AO - 环境光遮蔽
    float aoSamplingQualityAndEdgeDistance;     // <0: no AO, 0: bilinear, !0: bilateral edge distance - <0:无AO, 0:双线性, !0:双边边缘距离
    float aoBentNormals;                        // 0: no AO bent normal, >0.0 AO bent normals - 0:无AO弯曲法线, >0.0有AO弯曲法线

    // --------------------------------------------------------------------------------------------
    // Dynamic Lighting [variant: DYN] - 动态光照[变体：DYN]
    // --------------------------------------------------------------------------------------------
    math::float4 zParams;                       // froxel Z parameters - Froxel Z参数
    math::uint3 fParams;                        // stride-x, stride-y, stride-z - 步长-x, 步长-y, 步长-z
    int32_t lightChannels;                      // light channel bits - 光源通道位
    math::float2 froxelCountXY;                 // Froxel XY数量
    int enableFroxelViz;                        // 启用Froxel可视化
    int dynReserved0;                           // 动态光照保留字段0
    int dynReserved1;                           // 动态光照保留字段1
    int dynReserved2;                           // 动态光照保留字段2

    // IBL - 基于图像的光照
    float iblLuminance;                         // IBL亮度
    float iblRoughnessOneLevel;                 // level for roughness == 1 - 粗糙度==1时的级别
    math::float4 iblSH[9];                      // actually float3 entries (std140 requires float4 alignment) - 实际为float3条目（std140要求float4对齐）的球谐函数系数

    // --------------------------------------------------------------------------------------------
    // Directional Lighting [variant: DIR] - 方向光[变体：DIR]
    // --------------------------------------------------------------------------------------------
    math::float3 lightDirection;                // directional light direction - 方向光方向
    float padding0;                             // 填充0（对齐用）
    math::float4 lightColorIntensity;           // directional light - 方向光颜色和强度
    math::float4 sun;                           // cos(sunAngle), sin(sunAngle), 1/(sunAngle*HALO_SIZE-sunAngle), HALO_EXP - 太阳参数：cos(太阳角度), sin(太阳角度), 1/(太阳角度*光晕大小-太阳角度), 光晕指数
    math::float2 shadowFarAttenuationParams;    // a, a/far (a=1/pct-of-far) - 阴影远距离衰减参数：a, a/远平面(a=1/远平面百分比)

    // --------------------------------------------------------------------------------------------
    // Directional light shadowing [variant: SRE | DIR] - 方向光阴影[变体：SRE | DIR]
    // --------------------------------------------------------------------------------------------
    // bit 0: directional (sun) shadow enabled - 位0：方向光（太阳）阴影启用
    // bit 1: directional (sun) screen-space contact shadow enabled - 位1：方向光（太阳）屏幕空间接触阴影启用
    // bit 8-15: screen-space contact shadows ray casting steps - 位8-15：屏幕空间接触阴影光线投射步数
    int32_t directionalShadows;                 // 方向光阴影标志
    float ssContactShadowDistance;              // 屏幕空间接触阴影距离

    // position of cascade splits, in world space (not including the near plane) - 级联分割位置，在世界空间中（不包括近平面）
    // -Inf stored in unused components - 未使用的分量存储-Inf
    math::float4 cascadeSplits;                 // 级联分割位置
    // bit 0-3: cascade count - 位0-3：级联数量
    // bit 8-11: cascade has visible shadows - 位8-11：级联有可见阴影
    int32_t cascades;                           // 级联标志
    float shadowPenumbraRatioScale;     // For DPCF or PCSS, scale penumbra ratio for artistic use - 对于DPCF或PCSS，缩放半影比例用于艺术效果
    math::float2 lightFarAttenuationParams;     // a, a/far (a=1/pct-of-far) - 光源远距离衰减参数：a, a/远平面(a=1/远平面百分比)

    // --------------------------------------------------------------------------------------------
    // VSM shadows [variant: VSM] - VSM阴影[变体：VSM]
    // --------------------------------------------------------------------------------------------
    float vsmExponent;                          // VSM指数
    float vsmDepthScale;                        // VSM深度缩放
    float vsmLightBleedReduction;               // VSM光泄漏减少
    uint32_t shadowSamplingType;                // 0: vsm, 1: dpcf - 阴影采样类型：0:VSM, 1:DPCF

    // --------------------------------------------------------------------------------------------
    // Fog [variant: FOG] - 雾效[变体：FOG]
    // --------------------------------------------------------------------------------------------
    math::float3 fogDensity;        // { density, -falloff * yc, density * exp(-fallof * yc) } - 雾密度：{密度, -衰减*yc, 密度*exp(-衰减*yc)}
    float fogStart;                 // 雾开始距离
    float fogMaxOpacity;            // 雾最大不透明度
    uint32_t fogMinMaxMip;          // 雾最小最大Mip级别
    float fogHeightFalloff;         // 雾高度衰减
    float fogCutOffDistance;        // 雾截止距离
    math::float3 fogColor;          // 雾颜色
    float fogColorFromIbl;          // 雾颜色来自IBL
    float fogInscatteringStart;     // 雾内散射开始
    float fogInscatteringSize;      // 雾内散射大小
    float fogOneOverFarMinusNear;   // 雾1/(远-近)
    float fogNearOverFarMinusNear;  // 雾近/(远-近)
    std140::mat33 fogFromWorldMatrix;  // 雾从世界空间的变换矩阵
    math::float2 fogLinearParams; // { 1/(end-start), -start/(end-start) } - 雾线性参数：{1/(结束-开始), -开始/(结束-开始)}
    math::float2 fogReserved0;        // 雾保留字段0

    // --------------------------------------------------------------------------------------------
    // Screen-space reflections [variant: SSR (i.e.: VSM | SRE)] - 屏幕空间反射[变体：SSR（即：VSM | SRE）]
    // --------------------------------------------------------------------------------------------
    math::mat4f ssrReprojection;       // SSR重投影矩阵
    math::mat4f ssrUvFromViewMatrix;   // SSR从视图空间的UV矩阵
    float ssrThickness;                 // ssr thickness, in world units - SSR厚度，世界单位
    float ssrBias;                      // ssr bias, in world units - SSR偏差，世界单位
    float ssrDistance;                  // ssr world raycast distance, 0 when ssr is off - SSR世界光线投射距离，SSR关闭时为0
    float ssrStride;                    // ssr texel stride, >= 1.0 - SSR纹理步长，>= 1.0

    // --------------------------------------------------------------------------------------------
    // user defined global variables - 用户定义的全局变量
    // --------------------------------------------------------------------------------------------
    math::float4 custom[4];             // 自定义变量数组（4个float4）

    // --------------------------------------------------------------------------------------------
    // for feature level 0 / es2 usage - 用于功能级别0/ES2
    // --------------------------------------------------------------------------------------------
    int32_t rec709;                     // Only for ES2, 0 or 1, whether we need to do sRGB conversion - 仅用于ES2，0或1，是否需要sRGB转换
    float es2Reserved0;                 // ES2保留字段0
    float es2Reserved1;                 // ES2保留字段1
    float es2Reserved2;                 // ES2保留字段2

    // bring PerViewUib to 2 KiB - 将PerViewUib填充到2 KiB
    math::float4 reserved[22];          // 保留字段数组（22个float4）
};

// 2 KiB == 128 float4s - 2 KiB == 128个float4
static_assert(sizeof(PerViewUib) == sizeof(math::float4) * 128,
        "PerViewUib should be exactly 2KiB");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * 每个可渲染对象数据
 * 单个可渲染对象的统一变量数据
 */
struct PerRenderableData {
    std140::mat44 worldFromModelMatrix;       // 世界空间到模型空间的矩阵
    std140::mat33 worldFromModelNormalMatrix; // 世界空间到模型空间的法线矩阵
    int32_t morphTargetCount;                 // 变形目标数量
    int32_t flagsChannels;                   // see packFlags() below (0x00000fll) - 标志和通道位（见下面的packFlags()）
    int32_t objectId;                        // used for picking - 用于拾取的对象ID
    // TODO: We need a better solution, this currently holds the average local scale for the renderable - 我们需要更好的解决方案，目前保存可渲染对象的平均局部缩放
    float userData;                           // 用户数据

    math::float4 reserved[8];                 // 保留字段（8个float4）

    /**
     * 打包标志和通道位
     * @param skinning 是否使用蒙皮
     * @param morphing 是否使用变形
     * @param contactShadows 是否有接触阴影
     * @param hasInstanceBuffer 是否有实例缓冲区
     * @param channels 通道位（低8位）
     * @return 打包后的标志和通道值
     */
    static uint32_t packFlagsChannels(
            bool skinning, bool morphing, bool contactShadows, bool hasInstanceBuffer,
            uint8_t channels) noexcept {
        // 将各个标志打包到不同的位：蒙皮(0x100)，变形(0x200)，接触阴影(0x400)，实例缓冲区(0x800)，通道在低8位
        return (skinning              ? 0x100 : 0) |
               (morphing              ? 0x200 : 0) |
               (contactShadows        ? 0x400 : 0) |
               (hasInstanceBuffer     ? 0x800 : 0) |
               channels;
    }
};

#ifndef _MSC_VER
// not sure why this static_assert fails on MSVC - 不确定为什么这个static_assert在MSVC上失败
static_assert(std::is_trivially_default_constructible_v<PerRenderableData>,
        "make sure PerRenderableData stays trivially_default_constructible");
#endif

static_assert(sizeof(PerRenderableData) == 256,
        "sizeof(PerRenderableData) must be 256 bytes");

/**
 * 每个可渲染对象统一缓冲区（UBO）结构
 * 包含多个可渲染对象的数据数组（用于实例化渲染）
 * PerRenderableUib must have an alignment of 256 to be compatible with all versions of GLES. - PerRenderableUib必须有256字节对齐以兼容所有版本的GLES
 */
struct alignas(256) PerRenderableUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "ObjectUniforms" };  // 接口块名称
    PerRenderableData data[CONFIG_MAX_INSTANCES];                 // 可渲染对象数据数组（最大实例数）
};
// PerRenderableUib must have an alignment of 256 to be compatible with all versions of GLES. - PerRenderableUib必须有256字节对齐以兼容所有版本的GLES
static_assert(sizeof(PerRenderableUib) <= CONFIG_MINSPEC_UBO_SIZE,
        "PerRenderableUib exceeds max UBO size");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * 光源统一缓冲区（UBO）结构
 * 包含单个光源的数据（点光源或聚光灯）
 */
struct LightsUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "LightsUniforms" };  // 接口块名称
    math::float4 positionFalloff;     // { float3(pos), 1/falloff^2 } - {位置float3, 1/衰减^2}
    math::float3 direction;           // dir - 方向（用于聚光灯）
    float reserved1;                  // 0 - 保留字段1
    math::half4 colorIES;             // { half3(col),  IES index   } - {颜色half3, IES索引}
    math::float2 spotScaleOffset;     // { scale, offset } - {缩放, 偏移}（用于聚光灯）
    float reserved3;                  // 0 - 保留字段3
    float intensity;                  // float - 强度
    uint32_t typeShadow;              // 0x00.00.ii.ct (t: 0=point, 1=spot, c:contact, ii: index) - 类型和阴影：0x00.00.索引.接触阴影.类型(类型:0=点光源,1=聚光灯,接触阴影,索引)
    uint32_t channels;                // 0x000c00ll (ll: light channels, c: caster) - 通道：0x000c00ll(ll:光源通道, c:投射阴影)

    /**
     * 打包类型和阴影信息
     * @param type 光源类型（0=点光源，1=聚光灯）
     * @param contactShadow 是否有接触阴影
     * @param index 阴影索引
     * @return 打包后的值
     */
    static uint32_t packTypeShadow(uint8_t type, bool contactShadow, uint8_t index) noexcept {
        // 类型在低4位，接触阴影在第4位，索引在8-15位
        return (type & 0xF) | (contactShadow ? 0x10 : 0x00) | (index << 8);
    }
    /**
     * 打包通道和投射阴影标志
     * @param lightChannels 光源通道（低8位）
     * @param castShadows 是否投射阴影
     * @return 打包后的值
     */
    static uint32_t packChannels(uint8_t lightChannels, bool castShadows) noexcept {
        // 通道在低8位，投射阴影标志在第16位
        return lightChannels | (castShadows ? 0x10000 : 0);
    }
};
static_assert(sizeof(LightsUib) == 64,
        "the actual UBO is an array of 256 mat4");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * UBO for punctual (pointlight and spotlight) shadows. - 瞬时光源（点光源和聚光灯）阴影的UBO
 * 包含阴影贴图数据的结构
 */
struct ShadowUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "ShadowUniforms" };  // 接口块名称
    /**
     * 单个阴影数据（16字节对齐）
     */
    struct alignas(16) ShadowData {
        math::mat4f lightFromWorldMatrix;       // 64 - 光源空间到世界空间的矩阵
        math::float4 lightFromWorldZ;           // 16 - 光源空间的Z轴方向（世界空间）
        math::float4 scissorNormalized;         // 16 - 归一化的裁剪区域
        float texelSizeAtOneMeter;              //  4 - 一米处的纹理像素大小
        float bulbRadiusLs;                     //  4 - 光源球半径（光源空间）
        float nearOverFarMinusNear;             //  4 - 近平面/(远平面-近平面)
        float normalBias;                       //  4 - 法线偏差
        bool elvsm;                             //  4 - 是否使用指数方差阴影贴图（ESM/VSM）
        uint32_t layer;                         //  4 - 阴影贴图层索引
        uint32_t reserved1;                     //  4 - 保留字段1
        uint32_t reserved2;                     //  4 - 保留字段2
    };
    ShadowData shadows[CONFIG_MAX_SHADOWMAPS];  // 阴影数据数组（最大阴影贴图数量）
};
static_assert(sizeof(ShadowUib) <= CONFIG_MINSPEC_UBO_SIZE,
        "ShadowUib exceeds max UBO size");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * UBO froxel record buffer. - Froxel记录缓冲区的UBO
 * 用于存储Froxel记录数据
 */
struct FroxelRecordUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "FroxelRecordUniforms" };  // 接口块名称
    math::uint4 records[1024];  // 记录数组（1024个uint4）
};
static_assert(sizeof(FroxelRecordUib) == 16384,
        "FroxelRecordUib should be exactly 16KiB");

/**
 * Froxels统一缓冲区（UBO）结构
 * 用于存储Froxel数据
 */
struct FroxelsUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "FroxelsUniforms" };  // 接口块名称
    math::uint4 records[1024];  // 记录数组（1024个uint4）
};
static_assert(sizeof(FroxelsUib) == 16384,
        "FroxelsUib should be exactly 16KiB");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * This is not the UBO proper, but just an element of a bone array. - 这不是UBO本身，而是骨骼数组的一个元素
 * 每个可渲染对象的骨骼统一缓冲区（UBO）结构
 */
struct PerRenderableBoneUib { // NOLINT(cppcoreguidelines-pro-type-member-init)
    static constexpr std::string_view _name{ "BonesUniforms" };  // 接口块名称
    /**
     * 单个骨骼数据（16字节对齐）
     */
    struct alignas(16) BoneData {
        // bone transform, last row assumed [0,0,0,1] - 骨骼变换矩阵，假设最后一行是[0,0,0,1]
        math::float4 transform[3];     // 变换矩阵（3行，每行float4，最后一行隐含为[0,0,0,1]）
        // 4 first cofactor matrix of transform's upper left - 变换矩阵左上角的前4个余子式矩阵
        math::float3 cof0;             // 余子式矩阵的第一行
        float cof1x;                   // 余子式矩阵的第二行第一个元素（用于法线变换）
    };
    BoneData bones[CONFIG_MAX_BONE_COUNT];  // 骨骼数据数组（最大骨骼数量）
};

static_assert(sizeof(PerRenderableBoneUib) <= CONFIG_MINSPEC_UBO_SIZE,
        "PerRenderableUibBone exceeds max UBO size");

// ------------------------------------------------------------------------------------------------
// MARK: -

/**
 * 每个可渲染对象变形统一缓冲区（UBO）结构
 * 包含变形目标的权重数组
 */
struct alignas(16) PerRenderableMorphingUib {
    static constexpr std::string_view _name{ "MorphingUniforms" };  // 接口块名称
    // The array stride(the bytes between array elements) is always rounded up to the size of a vec4 in std140. - 在std140中，数组步长（数组元素之间的字节数）总是向上舍入到vec4的大小
    math::float4 weights[CONFIG_MAX_MORPH_TARGET_COUNT];  // 变形目标权重数组（最大变形目标数量）
};

static_assert(sizeof(PerRenderableMorphingUib) <= CONFIG_MINSPEC_UBO_SIZE,
        "PerRenderableMorphingUib exceeds max UBO size");

} // namespace filament

#endif // TNT_FILABRIDGE_UIBSTRUCTS_H
