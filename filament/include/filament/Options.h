/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_OPTIONS_H
#define TNT_FILAMENT_OPTIONS_H

#include <filament/Color.h>

#include <math/vec2.h>
#include <math/vec3.h>

#include <math.h>

#include <stdint.h>

namespace filament {

class Texture;

/**
 * Generic quality level.
 */
/**
 * 通用质量级别。
 */
enum class QualityLevel : uint8_t {
    LOW,     //!< 低质量
    MEDIUM,  //!< 中等质量
    HIGH,    //!< 高质量
    ULTRA    //!< 超高质量
};

/**
 * 混合模式。
 */
enum class BlendMode : uint8_t {
    OPAQUE,      //!< 不透明
    TRANSLUCENT  //!< 半透明
};

/**
 * Dynamic resolution can be used to either reach a desired target frame rate
 * by lowering the resolution of a View, or to increase the quality when the
 * rendering is faster than the target frame rate.
 *
 * This structure can be used to specify the minimum scale factor used when
 * lowering the resolution of a View, and the maximum scale factor used when
 * increasing the resolution for higher quality rendering. The scale factors
 * can be controlled on each X and Y axis independently. By default, all scale
 * factors are set to 1.0.
 *
 * enabled:   enable or disables dynamic resolution on a View
 *
 * homogeneousScaling: by default the system scales the major axis first. Set this to true
 *                     to force homogeneous scaling.
 *
 * minScale:  the minimum scale in X and Y this View should use
 *
 * maxScale:  the maximum scale in X and Y this View should use
 *
 * quality:   upscaling quality.
 *            LOW: 1 bilinear tap, Medium: 4 bilinear taps, High: 9 bilinear taps (tent)
 *
 * \note
 * Dynamic resolution is only supported on platforms where the time to render
 * a frame can be measured accurately. On platforms where this is not supported,
 * Dynamic Resolution can't be enabled unless minScale == maxScale.
 *
 * @see Renderer::FrameRateOptions
 *
 */
struct DynamicResolutionOptions {
    math::float2 minScale = {0.5f, 0.5f};           //!< minimum scale factors in x and y %codegen_java_float%
    /**
     * x 和 y 方向的最小缩放因子
     */
    math::float2 maxScale = {1.0f, 1.0f};           //!< maximum scale factors in x and y %codegen_java_float%
    /**
     * x 和 y 方向的最大缩放因子
     */
    float sharpness = 0.9f;                         //!< sharpness when QualityLevel::MEDIUM or higher is used [0 (disabled), 1 (sharpest)]
    /**
     * 当使用 QualityLevel::MEDIUM 或更高时，锐度 [0（禁用），1（最锐）]
     */
    bool enabled = false;                           //!< enable or disable dynamic resolution
    /**
     * 启用或禁用动态分辨率
     */
    bool homogeneousScaling = false;                //!< set to true to force homogeneous scaling
    /**
     * 设置为 true 以强制均匀缩放
     */

    /**
     * Upscaling quality
     * LOW:    bilinear filtered blit. Fastest, poor quality
     * MEDIUM: Qualcomm Snapdragon Game Super Resolution (SGSR) 1.0
     * HIGH:   AMD FidelityFX FSR1 w/ mobile optimizations
     * ULTRA:  AMD FidelityFX FSR1
     *      FSR1 and SGSR require a well anti-aliased (MSAA or TAA), noise free scene.
     *      Avoid FXAA and dithering.
     *
     * The default upscaling quality is set to LOW.
     *
     * caveat: currently, 'quality' is always set to LOW if the View is TRANSLUCENT.
     */
    /**
     * 上采样质量
     * LOW:    双线性过滤的位块传输。最快，质量较差
     * MEDIUM: 高通骁龙游戏超级分辨率 (SGSR) 1.0
     * HIGH:   AMD FidelityFX FSR1，带移动优化
     * ULTRA:  AMD FidelityFX FSR1
     *         FSR1 和 SGSR 需要良好的抗锯齿（MSAA 或 TAA）、无噪点的场景。
     *         避免使用 FXAA 和抖动。
     *
     * 默认上采样质量设置为 LOW。
     *
     * 注意：当前，如果 View 是 TRANSLUCENT（半透明）的，'quality' 总是设置为 LOW。
     */
    QualityLevel quality = QualityLevel::LOW;
};

/**
 * Options to control the bloom effect
 *
 * enabled:     Enable or disable the bloom post-processing effect. Disabled by default.
 *
 * levels:      Number of successive blurs to achieve the blur effect, the minimum is 3 and the
 *              maximum is 12. This value together with resolution influences the spread of the
 *              blur effect. This value can be silently reduced to accommodate the original
 *              image size.
 *
 * resolution:  Resolution of bloom's minor axis. The minimum value is 2^levels and the
 *              the maximum is lower of the original resolution and 4096. This parameter is
 *              silently clamped to the minimum and maximum.
 *              It is highly recommended that this value be smaller than the target resolution
 *              after dynamic resolution is applied (horizontally and vertically).
 *
 * strength:    how much of the bloom is added to the original image. Between 0 and 1.
 *
 * blendMode:   Whether the bloom effect is purely additive (false) or mixed with the original
 *              image (true).
 *
 * threshold:   When enabled, a threshold at 1.0 is applied on the source image, this is
 *              useful for artistic reasons and is usually needed when a dirt texture is used.
 *
 * dirt:        A dirt/scratch/smudges texture (that can be RGB), which gets added to the
 *              bloom effect. Smudges are visible where bloom occurs. Threshold must be
 *              enabled for the dirt effect to work properly.
 *
 * dirtStrength: Strength of the dirt texture.
 */
struct BloomOptions {
    enum class BlendMode : uint8_t {
        ADD,           //!< Bloom is modulated by the strength parameter and added to the scene
        /**
         * 泛光通过 strength 参数调制并添加到场景
         */
        INTERPOLATE    //!< Bloom is interpolated with the scene using the strength parameter
        /**
         * 泛光使用 strength 参数与场景插值
         */
    };
    Texture* dirt = nullptr;                //!< user provided dirt texture %codegen_skip_json% %codegen_skip_javascript%
    /**
     * 用户提供的污垢纹理
     */
    float dirtStrength = 0.2f;              //!< strength of the dirt texture %codegen_skip_json% %codegen_skip_javascript%
    /**
     * 污垢纹理的强度
     */
    float strength = 0.10f;                 //!< bloom's strength between 0.0 and 1.0
    /**
     * 泛光强度，在 0.0 和 1.0 之间
     */
    uint32_t resolution = 384;              //!< resolution of vertical axis (2^levels to 2048)
    /**
     * 垂直轴的分辨率（2^levels 到 2048）
     */
    uint8_t levels = 6;                     //!< number of blur levels (1 to 11)
    /**
     * 模糊级别数（1 到 11）
     */
    BlendMode blendMode = BlendMode::ADD;   //!< how the bloom effect is applied
    /**
     * 泛光效果的应用方式
     */
    bool threshold = true;                  //!< whether to threshold the source
    /**
     * 是否对源图像应用阈值
     */
    bool enabled = false;                   //!< enable or disable bloom
    /**
     * 启用或禁用泛光
     */
    float highlight = 1000.0f;              //!< limit highlights to this value before bloom [10, +inf]
    /**
     * 泛光前限制高光到此值 [10, +无穷]
     */

    /**
     * Bloom quality level.
     * LOW (default): use a more optimized down-sampling filter, however there can be artifacts
     *      with dynamic resolution, this can be alleviated by using the homogenous mode.
     * MEDIUM: Good balance between quality and performance.
     * HIGH: In this mode the bloom resolution is automatically increased to avoid artifacts.
     *      This mode can be significantly slower on mobile, especially at high resolution.
     *      This mode greatly improves the anamorphic bloom.
     */
    /**
     * 泛光质量级别。
     * LOW（默认）：使用更优化的下采样过滤器，但在动态分辨率下可能出现伪影，
     *      可以通过使用均匀模式来缓解。
     * MEDIUM: 质量和性能之间的良好平衡。
     * HIGH: 在此模式下，泛光分辨率会自动增加以避免伪影。
     *       此模式在移动设备上可能明显较慢，特别是在高分辨率下。
     *       此模式大大改善了变形泛光。
     */
    QualityLevel quality = QualityLevel::LOW;

    bool lensFlare = false;                 //!< enable screen-space lens flare
    /**
     * 启用屏幕空间镜头光晕
     */
    bool starburst = true;                  //!< enable starburst effect on lens flare
    /**
     * 在镜头光晕上启用星爆效果
     */
    float chromaticAberration = 0.005f;     //!< amount of chromatic aberration
    /**
     * 色差量
     */
    uint8_t ghostCount = 4;                 //!< number of flare "ghosts"
    /**
     * 光晕"鬼影"的数量
     */
    float ghostSpacing = 0.6f;              //!< spacing of the ghost in screen units [0, 1[
    /**
     * 鬼影的间距（屏幕单位）[0, 1[
     */
    float ghostThreshold = 10.0f;           //!< hdr threshold for the ghosts
    /**
     * 鬼影的 HDR 阈值
     */
    float haloThickness = 0.1f;             //!< thickness of halo in vertical screen units, 0 to disable
    /**
     * 光晕的厚度（垂直屏幕单位），0 表示禁用
     */
    float haloRadius = 0.4f;                //!< radius of halo in vertical screen units [0, 0.5]
    /**
     * 光晕的半径（垂直屏幕单位）[0, 0.5]
     */
    float haloThreshold = 10.0f;            //!< hdr threshold for the halo
    /**
     * 光晕的 HDR 阈值
     */
};

/**
 * Options to control large-scale fog in the scene. Materials can enable the `linearFog` property,
 * which uses a simplified, linear equation for fog calculation; in this mode, the heightFalloff
 * is ignored as well as the mipmap selection in IBL or skyColor mode.
 */
/**
 * 控制场景中大范围雾的选项。材质可以启用 `linearFog` 属性，
 * 它使用简化的线性方程进行雾计算；在此模式下，heightFalloff
 * 以及在 IBL 或 skyColor 模式下的 mipmap 选择都会被忽略。
 */
struct FogOptions {
    /**
     * Distance in world units [m] from the camera to where the fog starts ( >= 0.0 )
     */
    /**
     * 从相机到雾开始处的距离（世界单位 [m]）( >= 0.0 )
     */
    float distance = 0.0f;

    /**
     * Distance in world units [m] after which the fog calculation is disabled.
     * This can be used to exclude the skybox, which is desirable if it already contains clouds or
     * fog. The default value is +infinity which applies the fog to everything.
     *
     * Note: The SkyBox is typically at a distance of 1e19 in world space (depending on the near
     * plane distance and projection used though).
     */
    /**
     * 在此距离（世界单位 [m]）之后禁用雾计算。
     * 这可用于排除天空盒，如果天空盒已包含云或
     * 雾，这很有用。默认值是 +无穷，表示对所有物体应用雾。
     *
     * 注意：SkyBox 在世界空间中通常位于 1e19 的距离处（取决于近
     * 平面距离和使用的投影）。
     */
    float cutOffDistance = INFINITY;

    /**
     * fog's maximum opacity between 0 and 1. Ignored in `linearFog` mode.
     */
    /**
     * 雾的最大不透明度，在 0 和 1 之间。在 `linearFog` 模式下忽略。
     */
    float maximumOpacity = 1.0f;

    /**
     * Fog's floor in world units [m]. This sets the "sea level".
     */
    /**
     * 雾的地面高度（世界单位 [m]）。这设置了"海平面"。
     */
    float height = 0.0f;

    /**
     * How fast the fog dissipates with the altitude. heightFalloff has a unit of [1/m].
     * It can be expressed as 1/H, where H is the altitude change in world units [m] that causes a
     * factor 2.78 (e) change in fog density.
     *
     * A falloff of 0 means the fog density is constant everywhere and may result is slightly
     * faster computations.
     *
     * In `linearFog` mode, only use to compute the slope of the linear equation. Completely
     * ignored if set to 0.
     */
    /**
     * 雾随高度消散的速度。heightFalloff 的单位为 [1/m]。
     * 可以表示为 1/H，其中 H 是世界单位 [m] 中的高度变化，导致
     * 雾密度变化因子 2.78 (e)。
     *
     * 衰减为 0 意味着雾密度在各处都是恒定的，可能会导致稍微
     * 更快的计算。
     *
     * 在 `linearFog` 模式下，仅用于计算线性方程的斜率。如果
     * 设置为 0 则完全忽略。
     */
    float heightFalloff = 1.0f;

    /**
     *  Fog's color is used for ambient light in-scattering, a good value is
     *  to use the average of the ambient light, possibly tinted towards blue
     *  for outdoors environments. Color component's values should be between 0 and 1, values
     *  above one are allowed but could create a non energy-conservative fog (this is dependant
     *  on the IBL's intensity as well).
     *
     *  We assume that our fog has no absorption and therefore all the light it scatters out
     *  becomes ambient light in-scattering and has lost all directionality, i.e.: scattering is
     *  isotropic. This somewhat simulates Rayleigh scattering.
     *
     *  This value is used as a tint instead, when fogColorFromIbl is enabled.
     *
     *  @see fogColorFromIbl
     */
    /**
     *  雾的颜色用于环境光内散射，一个好的值是
     *  使用环境光的平均值，可能偏向蓝色
     *  用于户外环境。颜色分量的值应在 0 和 1 之间，允许
     * 大于 1 的值，但可能产生非能量守恒的雾（这也取决于
     *  IBL 的强度）。
     *
     *  我们假设我们的雾没有吸收，因此它散射出的所有光
     *  都变成环境光内散射并失去了所有方向性，即：散射是
     *  各向同性的。这在某种程度上模拟了瑞利散射。
     *
     *  启用 fogColorFromIbl 时，此值用作着色。
     *
     *  @see fogColorFromIbl
     */
    LinearColor color = { 1.0f, 1.0f, 1.0f };

    /**
     * Extinction factor in [1/m] at an altitude 'height'. The extinction factor controls how much
     * light is absorbed and out-scattered per unit of distance. Each unit of extinction reduces
     * the incoming light to 37% of its original value.
     *
     * Note: The extinction factor is related to the fog density, it's usually some constant K times
     * the density at sea level (more specifically at fog height). The constant K depends on
     * the composition of the fog/atmosphere.
     *
     * For historical reason this parameter is called `density`.
     *
     * In `linearFog` mode this is the slope of the linear equation if heightFalloff is set to 0.
     * Otherwise, heightFalloff affects the slope calculation such that it matches the slope of
     * the standard equation at the camera height.
     */
    /**
     * 在高度 'height' 处的消光系数 [1/m]。消光系数控制每单位距离
     * 有多少光被吸收和外散射。每个单位的消光将
     * 入射光降低到其原始值的 37%。
     *
     * 注意：消光系数与雾密度相关，通常是某个常数 K 乘以
     * 海平面（更具体地说是雾高度）的密度。常数 K 取决于
     * 雾/大气的组成。
     *
     * 由于历史原因，此参数称为 `density`。
     *
     * 在 `linearFog` 模式下，如果 heightFalloff 设置为 0，这是线性方程的斜率。
     * 否则，heightFalloff 会影响斜率计算，使其与
     * 相机高度处的标准方程的斜率匹配。
     */
    float density = 0.1f;

    /**
     * Distance in world units [m] from the camera where the Sun in-scattering starts.
     * Ignored in `linearFog` mode.
     */
    /**
     * 从相机开始太阳内散射的距离（世界单位 [m]）。
     * 在 `linearFog` 模式下忽略。
     */
    float inScatteringStart = 0.0f;

    /**
     * Very inaccurately simulates the Sun's in-scattering. That is, the light from the sun that
     * is scattered (by the fog) towards the camera.
     * Size of the Sun in-scattering (>0 to activate). Good values are >> 1 (e.g. ~10 - 100).
     * Smaller values result is a larger scattering size.
     * Ignored in `linearFog` mode.
     */
    /**
     * 非常不准确地模拟太阳的内散射。即来自太阳的光
     * 被（雾）散射向相机。
     * 太阳内散射的大小（>0 以激活）。良好的值远大于 1（例如 ~10 - 100）。
     * 较小的值导致较大的散射尺寸。
     * 在 `linearFog` 模式下忽略。
     */
    float inScatteringSize = -1.0f;

    /**
     * The fog color will be sampled from the IBL in the view direction and tinted by `color`.
     * Depending on the scene this can produce very convincing results.
     *
     * This simulates a more anisotropic phase-function.
     *
     * `fogColorFromIbl` is ignored when skyTexture is specified.
     *
     * @see skyColor
     */
    /**
     * 雾颜色将从 IBL 沿视图方向采样，并通过 `color` 着色。
     * 根据场景，这可以产生非常令人信服的结果。
     *
     * 这模拟了更各向异性的相位函数。
     *
     * 指定 skyTexture 时，`fogColorFromIbl` 被忽略。
     *
     * @see skyColor
     */
    bool fogColorFromIbl = false;

    /**
     * skyTexture must be a mipmapped cubemap. When provided, the fog color will be sampled from
     * this texture, higher resolution mip levels will be used for objects at the far clip plane,
     * and lower resolution mip levels for objects closer to the camera. The skyTexture should
     * typically be heavily blurred; a typical way to produce this texture is to blur the base
     * level with a strong gaussian filter or even an irradiance filter and then generate mip
     * levels as usual. How blurred the base level is somewhat of an artistic decision.
     *
     * This simulates a more anisotropic phase-function.
     *
     * `fogColorFromIbl` is ignored when skyTexture is specified.
     *
     * In `linearFog` mode mipmap level 0 is always used.
     *
     * @see Texture
     * @see fogColorFromIbl
     */
    Texture* skyColor = nullptr;    //!< %codegen_skip_json% %codegen_skip_javascript%
    /**
     * 天空颜色立方体贴图（必须带 mipmap）
     */

    /**
     * Enable or disable large-scale fog
     */
    /**
     * 启用或禁用大范围雾
     */
    bool enabled = false;
};

/**
 * Options to control Depth of Field (DoF) effect in the scene.
 *
 * cocScale can be used to set the depth of field blur independently of the camera
 * aperture, e.g. for artistic reasons. This can be achieved by setting:
 *      cocScale = cameraAperture / desiredDoFAperture
 *
 * @see Camera
 */
/**
 * 控制场景中景深 (DoF) 效果的选项。
 *
 * cocScale 可用于独立于相机
 * 光圈设置景深模糊，例如出于艺术原因。可以通过设置以下值来实现：
 *      cocScale = cameraAperture / desiredDoFAperture
 *
 * @see Camera
 */
struct DepthOfFieldOptions {
    enum class Filter : uint8_t {
        NONE,    //!< 无过滤器
        UNUSED,  //!< 未使用
        MEDIAN   //!< 中值过滤器
    };
    float cocScale = 1.0f;              //!< circle of confusion scale factor (amount of blur)
    /**
     * 弥散圆缩放因子（模糊量）
     */
    float cocAspectRatio = 1.0f;        //!< width/height aspect ratio of the circle of confusion (simulate anamorphic lenses)
    /**
     * 弥散圆的宽度/高度纵横比（模拟变形镜头）
     */
    float maxApertureDiameter = 0.01f;  //!< maximum aperture diameter in meters (zero to disable rotation)
    /**
     * 最大光圈直径（米）（零表示禁用旋转）
     */
    bool enabled = false;               //!< enable or disable depth of field effect
    /**
     * 启用或禁用景深效果
     */
    Filter filter = Filter::MEDIAN;     //!< filter to use for filling gaps in the kernel
    /**
     * 用于填充内核间隙的过滤器
     */
    bool nativeResolution = false;      //!< perform DoF processing at native resolution
    /**
     * 在本机分辨率下执行 DoF 处理
     */
    /**
     * Number of of rings used by the gather kernels. The number of rings affects quality
     * and performance. The actual number of sample per pixel is defined
     * as (ringCount * 2 - 1)^2. Here are a few commonly used values:
     *       3 rings :   25 ( 5x 5 grid)
     *       4 rings :   49 ( 7x 7 grid)
     *       5 rings :   81 ( 9x 9 grid)
     *      17 rings : 1089 (33x33 grid)
     *
     * With a maximum circle-of-confusion of 32, it is never necessary to use more than 17 rings.
     *
     * Usually all three settings below are set to the same value, however, it is often
     * acceptable to use a lower ring count for the "fast tiles", which improves performance.
     * Fast tiles are regions of the screen where every pixels have a similar
     * circle-of-confusion radius.
     *
     * A value of 0 means default, which is 5 on desktop and 3 on mobile.
     *
     * @{
     */
    /**
     * 聚集内核使用的环数。环数影响质量和
     * 性能。每个像素的实际采样数定义为
     * (ringCount * 2 - 1)^2。以下是一些常用值：
     *       3 环 :   25 (5x5 网格)
     *       4 环 :   49 (7x7 网格)
     *       5 环 :   81 (9x9 网格)
     *      17 环 : 1089 (33x33 网格)
     *
     * 最大弥散圆为 32 时，永远不需要使用超过 17 个环。
     *
     * 通常下面的三个设置都设置为相同的值，但通常
     * 可以为"快速瓦片"使用较低的环数，这可以提高性能。
     * 快速瓦片是屏幕上每个像素都有相似
     * 弥散圆半径的区域。
     *
     * 值为 0 表示默认值，桌面为 5，移动为 3。
     *
     * @{
     */
    uint8_t foregroundRingCount = 0; //!< number of kernel rings for foreground tiles
    /**
     * 前景瓦片的内核环数
     */
    uint8_t backgroundRingCount = 0; //!< number of kernel rings for background tiles
    /**
     * 背景瓦片的内核环数
     */
    uint8_t fastGatherRingCount = 0; //!< number of kernel rings for fast tiles
    /**
     * 快速瓦片的内核环数
     */
    /** @}*/

    /**
     * maximum circle-of-confusion in pixels for the foreground, must be in [0, 32] range.
     * A value of 0 means default, which is 32 on desktop and 24 on mobile.
     */
    /**
     * 前景的最大弥散圆（像素），必须在 [0, 32] 范围内。
     * 值为 0 表示默认值，桌面为 32，移动为 24。
     */
    uint16_t maxForegroundCOC = 0;

    /**
     * maximum circle-of-confusion in pixels for the background, must be in [0, 32] range.
     * A value of 0 means default, which is 32 on desktop and 24 on mobile.
     */
    /**
     * 背景的最大弥散圆（像素），必须在 [0, 32] 范围内。
     * 值为 0 表示默认值，桌面为 32，移动为 24。
     */
    uint16_t maxBackgroundCOC = 0;
};

/**
 * Options to control the vignetting effect.
 */
/**
 * 控制暗角效果的选项。
 */
struct VignetteOptions {
    float midPoint = 0.5f;                      //!< high values restrict the vignette closer to the corners, between 0 and 1
    /**
     * 高值将暗角限制在更靠近角落的位置，在 0 和 1 之间
     */
    float roundness = 0.5f;                     //!< controls the shape of the vignette, from a rounded rectangle (0.0), to an oval (0.5), to a circle (1.0)
    /**
     * 控制暗角的形状，从圆角矩形 (0.0) 到椭圆 (0.5) 到圆形 (1.0)
     */
    float feather = 0.5f;                       //!< softening amount of the vignette effect, between 0 and 1
    /**
     * 暗角效果的柔化量，在 0 和 1 之间
     */
    LinearColorA color = {0.0f, 0.0f, 0.0f, 1.0f}; //!< color of the vignette effect, alpha is currently ignored
    /**
     * 暗角效果的颜色，alpha 当前被忽略
     */
    bool enabled = false;                       //!< enables or disables the vignette effect
    /**
     * 启用或禁用暗角效果
     */
};

/**
 * Structure used to set the precision of the color buffer and related quality settings.
 *
 * @see setRenderQuality, getRenderQuality
 */
/**
 * 用于设置颜色缓冲区精度和相关质量设置的结构体。
 *
 * @see setRenderQuality, getRenderQuality
 */
struct RenderQuality {
    /**
     * Sets the quality of the HDR color buffer.
     *
     * A quality of HIGH or ULTRA means using an RGB16F or RGBA16F color buffer. This means
     * colors in the LDR range (0..1) have a 10 bit precision. A quality of LOW or MEDIUM means
     * using an R11G11B10F opaque color buffer or an RGBA16F transparent color buffer. With
     * R11G11B10F colors in the LDR range have a precision of either 6 bits (red and green
     * channels) or 5 bits (blue channel).
     */
    /**
     * 设置 HDR 颜色缓冲区的质量。
     *
     * HIGH 或 ULTRA 质量意味着使用 RGB16F 或 RGBA16F 颜色缓冲区。这意味着
     * LDR 范围 (0..1) 中的颜色具有 10 位精度。LOW 或 MEDIUM 质量意味着
     * 使用 R11G11B10F 不透明颜色缓冲区或 RGBA16F 透明颜色缓冲区。使用
     * R11G11B10F 时，LDR 范围中的颜色精度为 6 位（红色和绿色
     * 通道）或 5 位（蓝色通道）。
     */
    QualityLevel hdrColorBuffer = QualityLevel::HIGH;
};

/**
 * Options for screen space Ambient Occlusion (SSAO) and Screen Space Cone Tracing (SSCT)
 * @see setAmbientOcclusionOptions()
 */
/**
 * 屏幕空间环境光遮蔽 (SSAO) 和屏幕空间圆锥追踪 (SSCT) 的选项
 * @see setAmbientOcclusionOptions()
 */
struct AmbientOcclusionOptions {
    enum class AmbientOcclusionType : uint8_t {
        SAO,        //!< use Scalable Ambient Occlusion
        /**
         * 使用可扩展环境光遮蔽
         */
        GTAO,       //!< use Ground Truth-Based Ambient Occlusion
        /**
         * 使用基于真实值的环境光遮蔽
         */
    };

    AmbientOcclusionType aoType = AmbientOcclusionType::SAO;//!< Type of ambient occlusion algorithm.
    /**
     * 环境光遮蔽算法类型
     */
    float radius = 0.3f;    //!< Ambient Occlusion radius in meters, between 0 and ~10.
    /**
     * 环境光遮蔽半径（米），在 0 和 ~10 之间
     */
    float power = 1.0f;     //!< Controls ambient occlusion's contrast. Must be positive.
    /**
     * 控制环境光遮蔽的对比度。必须为正数
     */

    /**
     * Self-occlusion bias in meters. Use to avoid self-occlusion.
     * Between 0 and a few mm. No effect when aoType set to GTAO
     */
    /**
     * 自遮蔽偏差（米）。用于避免自遮蔽。
     * 在 0 和几毫米之间。当 aoType 设置为 GTAO 时无效
     */
    float bias = 0.0005f;

    float resolution = 0.5f;//!< How each dimension of the AO buffer is scaled. Must be either 0.5 or 1.0.
    /**
     * AO 缓冲区的每个维度如何缩放。必须是 0.5 或 1.0
     */
    float intensity = 1.0f; //!< Strength of the Ambient Occlusion effect.
    /**
     * 环境光遮蔽效果的强度
     */
    float bilateralThreshold = 0.05f; //!< depth distance that constitute an edge for filtering
    /**
     * 构成过滤边缘的深度距离
     */
    QualityLevel quality = QualityLevel::LOW; //!< affects # of samples used for AO and params for filtering
    /**
     * 影响用于 AO 的采样数和过滤参数
     */
    QualityLevel lowPassFilter = QualityLevel::MEDIUM; //!< affects AO smoothness. Recommend setting to HIGH when aoType set to GTAO.
    /**
     * 影响 AO 平滑度。当 aoType 设置为 GTAO 时，建议设置为 HIGH
     */
    QualityLevel upsampling = QualityLevel::LOW; //!< affects AO buffer upsampling quality
    /**
     * 影响 AO 缓冲区上采样质量
     */
    bool enabled = false;    //!< enables or disables screen-space ambient occlusion
    /**
     * 启用或禁用屏幕空间环境光遮蔽
     */
    bool bentNormals = false; //!< enables bent normals computation from AO, and specular AO
    /**
     * 启用从 AO 计算弯曲法线，以及镜面 AO
     */
    float minHorizonAngleRad = 0.0f;  //!< min angle in radian to consider. No effect when aoType set to GTAO.
    /**
     * 考虑的最小角度（弧度）。当 aoType 设置为 GTAO 时无效
     */
    /**
     * Screen Space Cone Tracing (SSCT) options
     * Ambient shadows from dominant light
     */
    /**
     * 屏幕空间圆锥追踪 (SSCT) 选项
     * 来自主导光的环境阴影
     */
    struct Ssct {
        float lightConeRad = 1.0f;       //!< full cone angle in radian, between 0 and pi/2
        /**
         * 完整圆锥角度（弧度），在 0 和 pi/2 之间
         */
        float shadowDistance = 0.3f;     //!< how far shadows can be cast
        /**
         * 阴影可以投射的距离
         */
        float contactDistanceMax = 1.0f; //!< max distance for contact
        /**
         * 接触的最大距离
         */
        float intensity = 0.8f;          //!< intensity
        /**
         * 强度
         */
        math::float3 lightDirection = { 0, -1, 0 };    //!< light direction
        /**
         * 光方向
         */
        float depthBias = 0.01f;         //!< depth bias in world units (mitigate self shadowing)
        /**
         * 深度偏差（世界单位）（减轻自阴影）
         */
        float depthSlopeBias = 0.01f;    //!< depth slope bias (mitigate self shadowing)
        /**
         * 深度斜率偏差（减轻自阴影）
         */
        uint8_t sampleCount = 4;         //!< tracing sample count, between 1 and 255
        /**
         * 追踪采样数，在 1 和 255 之间
         */
        uint8_t rayCount = 1;            //!< # of rays to trace, between 1 and 255
        /**
         * 要追踪的光线数，在 1 和 255 之间
         */
        bool enabled = false;            //!< enables or disables SSCT
        /**
         * 启用或禁用 SSCT
         */
    };
    Ssct ssct;                           // %codegen_skip_javascript% %codegen_java_flatten%

    /**
     * Ground Truth-base Ambient Occlusion (GTAO) options
     */
    /**
     * 基于真实值的环境光遮蔽 (GTAO) 选项
     */
    struct Gtao {
        uint8_t sampleSliceCount = 4;     //!< # of slices. Higher value makes less noise.
        /**
         * 切片数。较高的值产生较少的噪点。
         */
        uint8_t sampleStepsPerSlice = 3;  //!< # of steps the radius is divided into for integration. Higher value makes less bias.
        /**
         * 半径被分成用于积分的步数。较高的值产生较少的偏差。
         */
        float thicknessHeuristic = 0.004f; //!< thickness heuristic, should be closed to 0. No effect when useVisibilityBitmasks sets to true.
        /**
         * 厚度启发式，应该接近 0。当 useVisibilityBitmasks 设置为 true 时无效。
         */

        /**
         * Enables or disables visibility bitmasks mode. Notes that bent normal doesn't work under this mode.
         * Caution: Changing this option at runtime is very expensive as it may trigger a shader re-compilation.
         */
        /**
         * 启用或禁用可见性位掩码模式。注意弯曲法线在此模式下不起作用。
         * 警告：在运行时更改此选项非常昂贵，因为它可能触发着色器重新编译。
         */
        bool useVisibilityBitmasks = false;
        float constThickness = 0.5f; //!< constant thickness value of objects on the screen in world space. Only take effect when useVisibilityBitmasks is set to true.
        /**
         * 屏幕上物体在世界空间中的恒定厚度值。仅在 useVisibilityBitmasks 设置为 true 时生效。
         */

        /**
         * Increase thickness with distance to maintain detail on distant surfaces.
         * Caution: Changing this option at runtime is very expensive as it may trigger a shader re-compilation.
         */
        /**
         * 随距离增加厚度以在远距离表面上保持细节。
         * 警告：在运行时更改此选项非常昂贵，因为它可能触发着色器重新编译。
         */
        bool linearThickness = false;
    };
    Gtao gtao;                           // %codegen_skip_javascript% %codegen_java_flatten%
};

/**
 * Options for Multi-Sample Anti-aliasing (MSAA)
 * @see setMultiSampleAntiAliasingOptions()
 */
/**
 * 多重采样抗锯齿 (MSAA) 的选项
 * @see setMultiSampleAntiAliasingOptions()
 */
struct MultiSampleAntiAliasingOptions {
    bool enabled = false;           //!< enables or disables msaa
    /**
     * 启用或禁用 MSAA
     */

    /**
     * sampleCount number of samples to use for multi-sampled anti-aliasing.\n
     *              0: treated as 1
     *              1: no anti-aliasing
     *              n: sample count. Effective sample could be different depending on the
     *                 GPU capabilities.
     */
    /**
     * 用于多重采样抗锯齿的采样数。
     *              0: 视为 1
     *              1: 无抗锯齿
     *              n: 采样数。有效采样可能因
     *                 GPU 功能而异。
     */
    uint8_t sampleCount = 4;

    /**
     * custom resolve improves quality for HDR scenes, but may impact performance.
     */
    /**
     * 自定义解析提高 HDR 场景的质量，但可能影响性能。
     */
    bool customResolve = false;
};

/**
 * Options for Temporal Anti-aliasing (TAA)
 * Most TAA parameters are extremely costly to change, as they will trigger the TAA post-process
 * shaders to be recompiled. These options should be changed or set during initialization.
 * `filterWidth`, `feedback` and `jitterPattern`, however, can be changed at any time.
 *
 * `feedback` of 0.1 effectively accumulates a maximum of 19 samples in steady state.
 * see "A Survey of Temporal Antialiasing Techniques" by Lei Yang and all for more information.
 *
 * @see setTemporalAntiAliasingOptions()
 */
/**
 * 时间抗锯齿 (TAA) 的选项
 * 大多数 TAA 参数的更改成本极高，因为它们会触发 TAA 后处理
 * 着色器重新编译。这些选项应在初始化期间更改或设置。
 * 但是，`filterWidth`、`feedback` 和 `jitterPattern` 可以随时更改。
 *
 * 0.1 的 `feedback` 在稳态下有效地累积最多 19 个采样。
 * 有关更多信息，请参阅 Lei Yang 等人的"时间抗锯齿技术调查"。
 *
 * @see setTemporalAntiAliasingOptions()
 */
struct TemporalAntiAliasingOptions {
    float filterWidth = 1.0f;   //!< reconstruction filter width typically between 1 (sharper) and 2 (smoother)
    /**
     * 重建过滤器宽度，通常在 1（更锐）和 2（更平滑）之间
     */
    float feedback = 0.12f;     //!< history feedback, between 0 (maximum temporal AA) and 1 (no temporal AA).
    /**
     * 历史反馈，在 0（最大时间抗锯齿）和 1（无时间抗锯齿）之间
     */
    float lodBias = -1.0f;      //!< texturing lod bias (typically -1 or -2)
    /**
     * 纹理 LOD 偏差（通常为 -1 或 -2）
     */
    float sharpness = 0.0f;     //!< post-TAA sharpen, especially useful when upscaling is true.
    /**
     * TAA 后锐化，在 upscaling 为 true 时特别有用
     */
    bool enabled = false;       //!< enables or disables temporal anti-aliasing
    /**
     * 启用或禁用时间抗锯齿
     */
    bool upscaling = false;     //!< 4x TAA upscaling. Disables Dynamic Resolution. [BETA]
    /**
     * 4x TAA 上采样。禁用动态分辨率。[BETA]
     */

    enum class BoxType : uint8_t {
        AABB,           //!< use an AABB neighborhood
        /**
         * 使用 AABB 邻域
         */
        VARIANCE,       //!< use the variance of the neighborhood (not recommended)
        /**
         * 使用邻域的方差（不推荐）
         */
        AABB_VARIANCE   //!< use both AABB and variance
        /**
         * 同时使用 AABB 和方差
         */
    };

    enum class BoxClipping : uint8_t {
        ACCURATE,       //!< Accurate box clipping
        /**
         * 精确的包围盒裁剪
         */
        CLAMP,          //!< clamping
        /**
         * 钳制
         */
        NONE            //!< no rejections (use for debugging)
        /**
         * 无拒绝（用于调试）
         */
    };

    enum class JitterPattern : uint8_t {
        RGSS_X4,             //!  4-samples, rotated grid sampling
        /**
         * 4 采样，旋转网格采样
         */
        UNIFORM_HELIX_X4,    //!  4-samples, uniform grid in helix sequence
        /**
         * 4 采样，螺旋序列中的均匀网格
         */
        HALTON_23_X8,        //!  8-samples of halton 2,3
        /**
         * 8 个 halton 2,3 采样
         */
        HALTON_23_X16,       //! 16-samples of halton 2,3
        /**
         * 16 个 halton 2,3 采样
         */
        HALTON_23_X32        //! 32-samples of halton 2,3
        /**
         * 32 个 halton 2,3 采样
         */
    };

    bool filterHistory = true;      //!< whether to filter the history buffer
    /**
     * 是否过滤历史缓冲区
     */
    bool filterInput = true;        //!< whether to apply the reconstruction filter to the input
    /**
     * 是否将重建过滤器应用于输入
     */
    bool useYCoCg = false;          //!< whether to use the YcoCg color-space for history rejection
    /**
     * 是否使用 YcoCg 色彩空间进行历史拒绝
     */
    BoxType boxType = BoxType::AABB;            //!< type of color gamut box
    /**
     * 色域包围盒类型
     */
    BoxClipping boxClipping = BoxClipping::ACCURATE;     //!< clipping algorithm
    /**
     * 裁剪算法
     */
    JitterPattern jitterPattern = JitterPattern::HALTON_23_X16; //! Jitter Pattern
    /**
     * 抖动模式
     */
    float varianceGamma = 1.0f; //! High values increases ghosting artefact, lower values increases jittering, range [0.75, 1.25]
    /**
     * 高值增加拖尾伪影，低值增加抖动，范围 [0.75, 1.25]
     */

    bool preventFlickering = false;     //!< adjust the feedback dynamically to reduce flickering
    /**
     * 动态调整反馈以减少闪烁
     */
    bool historyReprojection = true;    //!< whether to apply history reprojection (debug option)
    /**
     * 是否应用历史重投影（调试选项）
     */
};

/**
 * Options for Screen-space Reflections.
 * @see setScreenSpaceReflectionsOptions()
 */
/**
 * 屏幕空间反射的选项。
 * @see setScreenSpaceReflectionsOptions()
 */
struct ScreenSpaceReflectionsOptions {
    float thickness = 0.1f;     //!< ray thickness, in world units
    /**
     * 光线厚度（世界单位）
     */
    float bias = 0.01f;         //!< bias, in world units, to prevent self-intersections
    /**
     * 偏差（世界单位），用于防止自相交
     */
    float maxDistance = 3.0f;   //!< maximum distance, in world units, to raycast
    /**
     * 光线投射的最大距离（世界单位）
     */
    float stride = 2.0f;        //!< stride, in texels, for samples along the ray.
    /**
     * 沿光线的采样步长（纹素）
     */
    bool enabled = false;
    /**
     * 启用或禁用屏幕空间反射
     */
};

/**
 * Options for the  screen-space guard band.
 * A guard band can be enabled to avoid some artifacts towards the edge of the screen when
 * using screen-space effects such as SSAO. Enabling the guard band reduces performance slightly.
 * Currently the guard band can only be enabled or disabled.
 */
/**
 * 屏幕空间保护带的选项。
 * 可以在使用屏幕空间效果（如 SSAO）时启用保护带以避免屏幕边缘的某些伪影。
 * 启用保护带会略微降低性能。
 * 目前保护带只能启用或禁用。
 */
struct GuardBandOptions {
    bool enabled = false;
    /**
     * 启用或禁用屏幕空间保护带
     */
};

/**
 * List of available post-processing anti-aliasing techniques.
 * @see setAntiAliasing, getAntiAliasing, setSampleCount
 */
/**
 * 可用的后处理抗锯齿技术列表。
 * @see setAntiAliasing, getAntiAliasing, setSampleCount
 */
enum class AntiAliasing : uint8_t {
    NONE,   //!< no anti aliasing performed as part of post-processing
    /**
     * 作为后处理的一部分不执行抗锯齿
     */
    FXAA    //!< FXAA is a low-quality but very efficient type of anti-aliasing. (default).
    /**
     * FXAA 是一种低质量但非常高效的抗锯齿类型。（默认）
     */
};

/**
 * List of available post-processing dithering techniques.
 */
/**
 * 可用的后处理抖动技术列表。
 */
enum class Dithering : uint8_t {
    NONE,       //!< No dithering
    /**
     * 无抖动
     */
    TEMPORAL    //!< Temporal dithering (default)
    /**
     * 时间抖动（默认）
     */
};

/**
 * List of available shadow mapping techniques.
 * @see setShadowType
 */
/**
 * 可用的阴影映射技术列表。
 * @see setShadowType
 */
enum class ShadowType : uint8_t {
    PCF,        //!< percentage-closer filtered shadows (default)
    /**
     * 百分比接近过滤阴影（默认）
     */
    VSM,        //!< variance shadows
    /**
     * 方差阴影
     */
    DPCF,       //!< PCF with contact hardening simulation
    /**
     * 带接触硬化模拟的 PCF
     */
    PCSS,       //!< PCF with soft shadows and contact hardening
    /**
     * 带软阴影和接触硬化的 PCF
     */
    PCFd,       // for debugging only, don't use.
    /**
     * 仅用于调试，不要使用
     */
};

/**
 * View-level options for VSM Shadowing.
 * @see setVsmShadowOptions()
 * @warning This API is still experimental and subject to change.
 */
/**
 * VSM 阴影的视图级别选项。
 * @see setVsmShadowOptions()
 * @warning 此 API 仍处于实验阶段，可能会更改。
 */
struct VsmShadowOptions {
    /**
     * Sets the number of anisotropic samples to use when sampling a VSM shadow map. If greater
     * than 0, mipmaps will automatically be generated each frame for all lights.
     *
     * The number of anisotropic samples = 2 ^ vsmAnisotropy.
     */
    /**
     * 设置采样 VSM 阴影贴图时使用的各向异性采样数。如果大于
     * 0，将为所有光源每帧自动生成 mipmap。
     *
     * 各向异性采样数 = 2 ^ vsmAnisotropy。
     */
    uint8_t anisotropy = 0;

    /**
     * Whether to generate mipmaps for all VSM shadow maps.
     */
    /**
     * 是否为所有 VSM 阴影贴图生成 mipmap。
     */
    bool mipmapping = false;

    /**
     * The number of MSAA samples to use when rendering VSM shadow maps.
     * Must be a power-of-two and greater than or equal to 1. A value of 1 effectively turns
     * off MSAA.
     * Higher values may not be available depending on the underlying hardware.
     */
    /**
     * 渲染 VSM 阴影贴图时使用的 MSAA 采样数。
     * 必须是 2 的幂且大于或等于 1。值为 1 实际上会关闭
     * MSAA。
     * 根据底层硬件，可能不支持更高的值。
     */
    uint8_t msaaSamples = 1;

    /**
     * Whether to use a 32-bits or 16-bits texture format for VSM shadow maps. 32-bits
     * precision is rarely needed, but it does reduces light leaks as well as "fading"
     * of the shadows in some situations. Setting highPrecision to true for a single
     * shadow map will double the memory usage of all shadow maps.
     */
    /**
     * 是否为 VSM 阴影贴图使用 32 位或 16 位纹理格式。32 位
     * 精度很少需要，但它确实可以减少某些情况下的光泄漏和阴影的"褪色"。
     * 为单个阴影贴图将 highPrecision 设置为 true 将使所有阴影贴图的内存使用量翻倍。
     */
    bool highPrecision = false;

    /**
     * VSM minimum variance scale, must be positive.
     */
    /**
     * VSM 最小方差缩放，必须为正数。
     */
    float minVarianceScale = 0.5f;

    /**
     * VSM light bleeding reduction amount, between 0 and 1.
     */
    /**
     * VSM 光泄漏减少量，在 0 和 1 之间。
     */
    float lightBleedReduction = 0.15f;
};

/**
 * View-level options for DPCF and PCSS Shadowing.
 * @see setSoftShadowOptions()
 * @warning This API is still experimental and subject to change.
 */
/**
 * DPCF 和 PCSS 阴影的视图级别选项。
 * @see setSoftShadowOptions()
 * @warning 此 API 仍处于实验阶段，可能会更改。
 */
struct SoftShadowOptions {
    /**
     * Globally scales the penumbra of all DPCF and PCSS shadows
     * Acceptable values are greater than 0
     */
    /**
     * 全局缩放所有 DPCF 和 PCSS 阴影的半影
     * 可接受的值大于 0
     */
    float penumbraScale = 1.0f;

    /**
     * Globally scales the computed penumbra ratio of all DPCF and PCSS shadows.
     * This effectively controls the strength of contact hardening effect and is useful for
     * artistic purposes. Higher values make the shadows become softer faster.
     * Acceptable values are equal to or greater than 1.
     */
    /**
     * 全局缩放所有 DPCF 和 PCSS 阴影的计算半影比率。
     * 这有效地控制了接触硬化效果的强度，对于
     * 艺术目的很有用。较高的值使阴影更快地变软。
     * 可接受的值等于或大于 1。
     */
    float penumbraRatioScale = 1.0f;
};

/**
 * Options for stereoscopic (multi-eye) rendering.
 */
/**
 * 立体（多眼）渲染的选项。
 */
struct StereoscopicOptions {
    bool enabled = false;
    /**
     * 启用或禁用立体渲染
     */
};

} // namespace filament

#endif //TNT_FILAMENT_OPTIONS_H
