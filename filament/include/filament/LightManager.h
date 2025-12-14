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

#ifndef TNT_FILAMENT_LIGHTMANAGER_H
#define TNT_FILAMENT_LIGHTMANAGER_H

#include <filament/FilamentAPI.h>
#include <filament/Color.h>

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/EntityInstance.h>

#include <math/mathfwd.h>
#include <math/quat.h>

#include <stdint.h>
#include <stddef.h>

namespace utils {
    class Entity;
} // namespace utils

namespace filament {

class Engine;
class FEngine;
class FLightManager;

/**
 * LightManager allows to create a light source in the scene, such as a sun or street lights.
 *
 * At least one light must be added to a scene in order to see anything
 * (unless the Material.Shading.UNLIT is used).
 *
 *
 * Creation and destruction
 * ========================
 *
 * A Light component is created using the LightManager::Builder and destroyed by calling
 * LightManager::destroy(utils::Entity).
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *  utils::Entity sun = utils::EntityManager.get().create();
 *
 *  filament::LightManager::Builder(Type::SUN)
 *              .castShadows(true)
 *              .build(*engine, sun);
 *
 *  engine->getLightManager().destroy(sun);
 * ~~~~~~~~~~~
 *
 *
 * Light types
 * ===========
 *
 * Lights come in three flavors:
 * - directional lights
 * - point lights
 * - spot lights
 *
 *
 * Directional lights
 * ------------------
 *
 * Directional lights have a direction, but don't have a position. All light rays are
 * parallel and come from infinitely far away and from everywhere. Typically a directional light
 * is used to simulate the sun.
 *
 * Directional lights and spot lights are able to cast shadows.
 *
 * To create a directional light use Type.DIRECTIONAL or Type.SUN, both are similar, but the later
 * also draws a sun's disk in the sky and its reflection on glossy objects.
 *
 * @warning Currently, only a single directional light is supported. If several directional lights
 * are added to the scene, the dominant one will be used.
 *
 * @see Builder.direction(), Builder.sunAngularRadius()
 *
 * Point lights
 * ------------
 *
 * Unlike directional lights, point lights have a position but emit light in all directions.
 * The intensity of the light diminishes with the inverse square of the distance to the light.
 * Builder.falloff() controls distance beyond which the light has no more influence.
 *
 * A scene can have multiple point lights.
 *
 * @see Builder.position(), Builder.falloff()
 *
 * Spot lights
 * -----------
 *
 * Spot lights are similar to point lights but the light it emits is limited to a cone defined by
 * Builder.spotLightCone() and the light's direction.
 *
 * A spot light is therefore defined by a position, a direction and inner and outer cones. The
 * spot light's influence is limited to inside the outer cone. The inner cone defines the light's
 * falloff attenuation.
 *
 * A physically correct spot light is a little difficult to use because changing the outer angle
 * of the cone changes the illumination levels, as the same amount of light is spread over a
 * changing volume. The coupling of illumination and the outer cone means that an artist cannot
 * tweak the influence cone of a spot light without also changing the perceived illumination.
 * It therefore makes sense to provide artists with a parameter to disable this coupling. This
 * is the difference between Type.FOCUSED_SPOT and Type.SPOT.
 *
 * @see Builder.position(), Builder.direction(), Builder.falloff(), Builder.spotLightCone()
 *
 * Performance considerations
 * ==========================
 *
 * Generally, adding lights to the scene hurts performance, however filament is designed to be
 * able to handle hundreds of lights in a scene under certain conditions. Here are some tips
 * to keep performances high.
 *
 * 1. Prefer spot lights to point lights and use the smallest outer cone angle possible.
 *
 * 2. Use the smallest possible falloff distance for point and spot lights.
 *    Performance is very sensitive to overlapping lights. The falloff distance essentially
 *    defines a sphere of influence for the light, so try to position point and spot lights
 *    such that they don't overlap too much.
 *
 *    On the other hand, a scene can contain hundreds of non overlapping lights without
 *    incurring a significant overhead.
 *
 */
/**
 * LightManager 允许在场景中创建光源，例如太阳或街灯。
 *
 * 必须向场景添加至少一个光源才能看到任何内容
 *（除非使用 Material.Shading.UNLIT）。
 *
 *
 * 创建和销毁
 * ========================
 *
 * 使用 LightManager::Builder 创建光源组件，并通过调用
 * LightManager::destroy(utils::Entity) 销毁。
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *  utils::Entity sun = utils::EntityManager.get().create();
 *
 *  filament::LightManager::Builder(Type::SUN)
 *              .castShadows(true)
 *              .build(*engine, sun);
 *
 *  engine->getLightManager().destroy(sun);
 * ~~~~~~~~~~~
 *
 *
 * 光源类型
 * ===========
 *
 * 光源分为三种类型：
 * - 方向光
 * - 点光源
 * - 聚光灯
 *
 *
 * 方向光
 * ------------------
 *
 * 方向光具有方向，但没有位置。所有光线都是
 * 平行的，来自无限远处和所有方向。通常方向光
 * 用于模拟太阳。
 *
 * 方向光和聚光灯能够投射阴影。
 *
 * 要创建方向光，请使用 Type.DIRECTIONAL 或 Type.SUN，两者相似，但后者
 * 还会在天空中绘制太阳圆盘并在光泽物体上反射。
 *
 * @warning 目前仅支持单个方向光。如果向场景添加多个方向光，
 * 将使用占主导地位的那个。
 *
 * @see Builder.direction(), Builder.sunAngularRadius()
 *
 * 点光源
 * ------------
 *
 * 与方向光不同，点光源具有位置但在所有方向上发光。
 * 光的强度随距离的平方反比衰减。
 * Builder.falloff() 控制光不再有影响的距离。
 *
 * 场景可以有多个点光源。
 *
 * @see Builder.position(), Builder.falloff()
 *
 * 聚光灯
 * -----------
 *
 * 聚光灯类似于点光源，但它发出的光被限制在由
 * Builder.spotLightCone() 和光的方向定义的圆锥内。
 *
 * 因此，聚光灯由位置、方向以及内圆锥和外圆锥定义。
 * 聚光灯的影响限制在外圆锥内。内圆锥定义光的
 * 衰减。
 *
 * 物理正确的聚光灯有点难以使用，因为改变圆锥的
 * 外角会改变光照水平，因为相同的光量分布在变化的
 * 体积上。光照和外圆锥的耦合意味着艺术家无法
 * 在不改变感知光照的情况下调整聚光灯的影响圆锥。
 * 因此，为艺术家提供禁用此耦合的参数是有意义的。这
 * 就是 Type.FOCUSED_SPOT 和 Type.SPOT 之间的区别。
 *
 * @see Builder.position(), Builder.direction(), Builder.falloff(), Builder.spotLightCone()
 *
 * 性能注意事项
 * ==========================
 *
 * 通常，向场景添加光源会降低性能，但 filament 设计为能够在
 * 某些条件下处理场景中的数百个光源。以下是一些保持
 * 高性能的提示。
 *
 * 1. 优先使用聚光灯而不是点光源，并使用尽可能小的外圆锥角。
 *
 * 2. 对点光源和聚光灯使用尽可能小的衰减距离。
 *    性能对重叠的光源非常敏感。衰减距离本质上
 *    定义了光的影响球体，因此尝试定位点光源和聚光灯
 *    使它们不会过度重叠。
 *
 *    另一方面，场景可以包含数百个不重叠的光源而不会
 *    产生显著的开销。
 *
 */
class UTILS_PUBLIC LightManager : public FilamentAPI {
    struct BuilderDetails;

public:
    using Instance = utils::EntityInstance<LightManager>;

    /**
     * Returns the number of component in the LightManager, note that component are not
     * guaranteed to be active. Use the EntityManager::isAlive() before use if needed.
     *
     * @return number of component in the LightManager
     */
    /**
     * 返回 LightManager 中的组件数量，注意组件不
     * 保证是活动的。如果需要，在使用前使用 EntityManager::isAlive()。
     *
     * @return LightManager 中的组件数量
     */
    size_t getComponentCount() const noexcept;

    /**
     * Returns whether a particular Entity is associated with a component of this LightManager
     * @param e An Entity.
     * @return true if this Entity has a component associated with this manager.
     */
    /**
     * 返回特定 Entity 是否与此 LightManager 的组件关联
     * @param e 一个 Entity
     * @return 如果此 Entity 具有与此管理器关联的组件则返回 true
     */
    bool hasComponent(utils::Entity e) const noexcept;

    /**
     * @return true if the this manager has no components
     */
    /**
     * @return 如果此管理器没有组件则返回 true
     */
    bool empty() const noexcept;

    /**
     * Retrieve the `Entity` of the component from its `Instance`.
     * @param i Instance of the component obtained from getInstance()
     * @return
     */
    /**
     * 从其 `Instance` 检索组件的 `Entity`。
     * @param i 从 getInstance() 获取的组件实例
     * @return 组件的 Entity
     */
    utils::Entity getEntity(Instance i) const noexcept;

    /**
     * Retrieve the Entities of all the components of this manager.
     * @return A list, in no particular order, of all the entities managed by this manager.
     */
    /**
     * 检索此管理器所有组件的 Entities。
     * @return 此管理器管理的所有实体的列表（无特定顺序）
     */
    utils::Entity const* UTILS_NONNULL getEntities() const noexcept;

    /**
     * Gets an Instance representing the Light component associated with the given Entity.
     * @param e An Entity.
     * @return An Instance object, which represents the Light component associated with the Entity e.
     * @note Use Instance::isValid() to make sure the component exists.
     * @see hasComponent()
     */
    /**
     * 获取表示与给定 Entity 关联的光源组件实例。
     * @param e 一个 Entity
     * @return 表示与 Entity e 关联的光源组件的 Instance 对象
     * @note 使用 Instance::isValid() 确保组件存在
     * @see hasComponent()
     */
    Instance getInstance(utils::Entity e) const noexcept;

    // destroys this component from the given entity
    /**
     * 从给定实体销毁此组件
     */
    void destroy(utils::Entity e) noexcept;


    //! Denotes the type of the light being created.
    /**
     * 表示正在创建的光源类型。
     */
    enum class Type : uint8_t {
        SUN,            //!< Directional light that also draws a sun's disk in the sky.
        /** 方向光，还会在天空中绘制太阳圆盘。 */
        DIRECTIONAL,    //!< Directional light, emits light in a given direction.
        /** 方向光，在给定方向发光。 */
        POINT,          //!< Point light, emits light from a position, in all directions.
        /** 点光源，从位置向所有方向发光。 */
        FOCUSED_SPOT,   //!< Physically correct spot light.
        /** 物理正确的聚光灯。 */
        SPOT,           //!< Spot light with coupling of outer cone and illumination disabled.
        /** 禁用外圆锥和光照耦合的聚光灯。 */
    };

    /**
     * Control the quality / performance of the shadow map associated to this light
     */
    /**
     * 控制与此光源关联的阴影贴图的质量/性能
     */
    struct ShadowOptions {
        /** Size of the shadow map in texels. Must be a power-of-two and larger or equal to 8. */
        /** 阴影贴图的大小（以纹素为单位）。必须是 2 的幂且大于或等于 8。 */
        uint32_t mapSize = 1024;

        /**
         * Number of shadow cascades to use for this light. Must be between 1 and 4 (inclusive).
         * A value greater than 1 turns on cascaded shadow mapping (CSM).
         * Only applicable to Type.SUN or Type.DIRECTIONAL lights.
         *
         * When using shadow cascades, cascadeSplitPositions must also be set.
         *
         * @see ShadowOptions::cascadeSplitPositions
         */
        /**
         * 用于此光源的阴影级联数量。必须在 1 到 4 之间（含）。
         * 大于 1 的值会启用级联阴影映射（CSM）。
         * 仅适用于 Type.SUN 或 Type.DIRECTIONAL 光源。
         *
         * 使用阴影级联时，还必须设置 cascadeSplitPositions。
         *
         * @see ShadowOptions::cascadeSplitPositions
         */
        uint8_t shadowCascades = 1;

        /**
         * The split positions for shadow cascades.
         *
         * Cascaded shadow mapping (CSM) partitions the camera frustum into cascades. These values
         * determine the planes along the camera's Z axis to split the frustum. The camera near
         * plane is represented by 0.0f and the far plane represented by 1.0f.
         *
         * For example, if using 4 cascades, these values would set a uniform split scheme:
         * { 0.25f, 0.50f, 0.75f }
         *
         * For N cascades, N - 1 split positions will be read from this array.
         *
         * Filament provides utility methods inside LightManager::ShadowCascades to help set these
         * values. For example, to use a uniform split scheme:
         *
         * ~~~~~~~~~~~{.cpp}
         *   LightManager::ShadowCascades::computeUniformSplits(options.splitPositions, 4);
         * ~~~~~~~~~~~
         *
         * @see ShadowCascades::computeUniformSplits
         * @see ShadowCascades::computeLogSplits
         * @see ShadowCascades::computePracticalSplits
         */
        /**
         * 阴影级联的分割位置。
         *
         * 级联阴影映射（CSM）将相机视锥体分割为级联。这些值
         * 确定沿相机 Z 轴分割视锥体的平面。相机近
         * 平面用 0.0f 表示，远平面用 1.0f 表示。
         *
         * 例如，如果使用 4 个级联，这些值将设置统一分割方案：
         * { 0.25f, 0.50f, 0.75f }
         *
         * 对于 N 个级联，将从该数组读取 N - 1 个分割位置。
         *
         * Filament 在 LightManager::ShadowCascades 内提供实用方法来帮助设置这些
         * 值。例如，要使用统一分割方案：
         *
         * ~~~~~~~~~~~{.cpp}
         *   LightManager::ShadowCascades::computeUniformSplits(options.splitPositions, 4);
         * ~~~~~~~~~~~
         *
         * @see ShadowCascades::computeUniformSplits
         * @see ShadowCascades::computeLogSplits
         * @see ShadowCascades::computePracticalSplits
         */
        float cascadeSplitPositions[3] = { 0.125f, 0.25f, 0.50f };

        /** Constant bias in world units (e.g. meters) by which shadows are moved away from the
         * light. 1mm by default.
         * This is ignored when the View's ShadowType is set to VSM.
         */
        /** 以世界单位（例如米）表示的常量偏移，用于将阴影移离光源。默认为 1mm。
         * 当 View 的 ShadowType 设置为 VSM 时忽略此值。
         */
        float constantBias = 0.001f;

        /** Amount by which the maximum sampling error is scaled. The resulting value is used
         * to move the shadow away from the fragment normal. Should be 1.0.
         * This is ignored when the View's ShadowType is set to VSM.
         */
        /** 最大采样误差的缩放量。结果值用于
         * 将阴影移离片段法线。应为 1.0。
         * 当 View 的 ShadowType 设置为 VSM 时忽略此值。
         */
        float normalBias = 1.0f;

        /** Distance from the camera after which shadows are clipped. This is used to clip
         * shadows that are too far and wouldn't contribute to the scene much, improving
         * performance and quality. This value is always positive.
         * Use 0.0f to use the camera far distance.
         * This only affect directional lights.
         */
        /** 距离相机的距离，超过此距离后阴影被裁剪。用于裁剪
         * 太远且对场景贡献不大的阴影，从而改善
         * 性能和质量。此值始终为正。
         * 使用 0.0f 以使用相机远距离。
         * 这仅影响方向光。
         */
        float shadowFar = 0.0f;

        /** Optimize the quality of shadows from this distance from the camera. Shadows will
         * be rendered in front of this distance, but the quality may not be optimal.
         * This value is always positive. Use 0.0f to use the camera near distance.
         * The default of 1m works well with many scenes. The quality of shadows may drop
         * rapidly when this value decreases.
         */
        /** 优化从相机此距离开始的阴影质量。阴影将在
         * 此距离之前渲染，但质量可能不是最优的。
         * 此值始终为正。使用 0.0f 以使用相机近距离。
         * 默认值 1m 适用于许多场景。当此值减小时，阴影质量可能会
         * 急剧下降。
         */
        float shadowNearHint = 1.0f;

        /** Optimize the quality of shadows in front of this distance from the camera. Shadows
         * will be rendered behind this distance, but the quality may not be optimal.
         * This value is always positive. Use std::numerical_limits<float>::infinity() to
         * use the camera far distance.
         */
        /** 优化相机此距离之前的阴影质量。阴影将
         * 在此距离之后渲染，但质量可能不是最优的。
         * 此值始终为正。使用 std::numerical_limits<float>::infinity() 以
         * 使用相机远距离。
         */
        float shadowFarHint = 100.0f;

        /**
         * Controls whether the shadow map should be optimized for resolution or stability.
         * When set to true, all resolution enhancing features that can affect stability are
         * disabling, resulting in significantly lower resolution shadows, albeit stable ones.
         *
         * Setting this flag to true always disables LiSPSM (see below).
         *
         * @see lispsm
         */
        /**
         * 控制阴影贴图是否应针对分辨率或稳定性进行优化。
         * 设置为 true 时，所有可能影响稳定性的分辨率增强功能都会
         * 被禁用，导致阴影分辨率显著降低，尽管是稳定的。
         *
         * 将此标志设置为 true 总是会禁用 LiSPSM（见下文）。
         *
         * @see lispsm
         */
        bool stable = false;

        /**
         * LiSPSM, or light-space perspective shadow-mapping is a technique allowing to better
         * optimize the use of the shadow-map texture. When enabled the effective resolution of
         * shadows is greatly improved and yields result similar to using cascades without the
         * extra cost. LiSPSM comes with some drawbacks however, in particular it is incompatible
         * with blurring because it effectively affects the blur kernel size.
         *
         * Blurring is only an issue when using ShadowType::VSM with a large blur or with
         * ShadowType::PCSS however.
         *
         * If these blurring artifacts become problematic, this flag can be used to disable LiSPSM.
         *
         * @see stable
         */
        /**
         * LiSPSM，即光空间透视阴影映射，是一种允许更好地
         * 优化阴影贴图纹理使用的技术。启用时，阴影的有效分辨率
         * 会大大提高，并产生类似于使用级联的结果，但没有
         * 额外成本。但是，LiSPSM 有一些缺点，特别是它与
         * 模糊不兼容，因为它会有效地影响模糊核大小。
         *
         * 但是，模糊问题仅在将 ShadowType::VSM 与大面积模糊或
         * ShadowType::PCSS 一起使用时才存在。
         *
         * 如果这些模糊伪影变得有问题，可以使用此标志禁用 LiSPSM。
         *
         * @see stable
         */
        bool lispsm = true;

        /**
         * Constant bias in depth-resolution units by which shadows are moved away from the
         * light. The default value of 0.5 is used to round depth values up.
         * Generally this value shouldn't be changed or at least be small and positive.
         * This is ignored when the View's ShadowType is set to VSM.
         */
        /**
         * 以深度分辨率单位表示的常量偏移，用于将阴影移离
         * 光源。默认值 0.5 用于向上舍入深度值。
         * 通常不应更改此值，或至少应为小的正值。
         * 当 View 的 ShadowType 设置为 VSM 时忽略此值。
         */
        float polygonOffsetConstant = 0.5f;

        /**
         * Bias based on the change in depth in depth-resolution units by which shadows are moved
         * away from the light. The default value of 2.0 works well with SHADOW_SAMPLING_PCF_LOW.
         * Generally this value is between 0.5 and the size in texel of the PCF filter.
         * Setting this value correctly is essential for LISPSM shadow-maps.
         * This is ignored when the View's ShadowType is set to VSM.
         */
        /**
         * 基于深度变化（以深度分辨率单位表示）的偏移，用于将阴影移离
         * 光源。默认值 2.0 与 SHADOW_SAMPLING_PCF_LOW 配合良好。
         * 通常此值在 0.5 和 PCF 滤波器的纹素大小之间。
         * 正确设置此值对于 LISPSM 阴影贴图至关重要。
         * 当 View 的 ShadowType 设置为 VSM 时忽略此值。
         */
        float polygonOffsetSlope = 2.0f;

        /**
         * Whether screen-space contact shadows are used. This applies regardless of whether a
         * Renderable is a shadow caster.
         * Screen-space contact shadows are typically useful in large scenes.
         * (off by default)
         */
        /**
         * 是否使用屏幕空间接触阴影。无论
         * Renderable 是否为阴影投射者，这都适用。
         * 屏幕空间接触阴影通常在大场景中很有用。
         * （默认关闭）
         */
        bool screenSpaceContactShadows = false;

        /**
         * Number of ray-marching steps for screen-space contact shadows (8 by default).
         *
         * CAUTION: this parameter is ignored for all lights except the directional/sun light,
         *          all other lights use the same value set for the directional/sun light.
         *
         */
        /**
         * 屏幕空间接触阴影的光线步进步数（默认为 8）。
         *
         * 注意：此参数对所有光源都被忽略，除了方向光/太阳光，
         *         所有其他光源使用为方向光/太阳光设置的相同值。
         *
         */
        uint8_t stepCount = 8;

        /**
         * Maximum shadow-occluder distance for screen-space contact shadows (world units).
         * (30 cm by default)
         *
         * CAUTION: this parameter is ignored for all lights except the directional/sun light,
         *          all other lights use the same value set for the directional/sun light.
         *
         */
        /**
         * 屏幕空间接触阴影的最大阴影遮挡物距离（世界单位）。
         * （默认 30 cm）
         *
         * 注意：此参数对所有光源都被忽略，除了方向光/太阳光，
         *         所有其他光源使用为方向光/太阳光设置的相同值。
         *
         */
        float maxShadowDistance = 0.3f;

        /**
         * Options available when the View's ShadowType is set to VSM.
         *
         * @warning This API is still experimental and subject to change.
         * @see View::setShadowType
         */
        /**
         * 当 View 的 ShadowType 设置为 VSM 时可用的选项。
         *
         * @warning 此 API 仍处于实验阶段，可能会更改。
         * @see View::setShadowType
         */
        struct Vsm {
            /**
             * When elvsm is set to true, "Exponential Layered VSM without Layers" are used. It is
             * an improvement to the default EVSM which suffers important light leaks. Enabling
             * ELVSM for a single shadowmap doubles the memory usage of all shadow maps.
             * ELVSM is mostly useful when large blurs are used.
             */
            /**
             * 当 elvsm 设置为 true 时，使用"无层的指数分层 VSM"。它是
             * 对默认 EVSM 的改进，默认 EVSM 存在重要的漏光问题。为
             * 单个阴影贴图启用 ELVSM 会使所有阴影贴图的内存使用量加倍。
             * ELVSM 在大型模糊时最有用。
             */
            bool elvsm = false;

            /**
             * Blur width for the VSM blur. Zero do disable.
             * The maximum value is 125.
             */
            /**
             * VSM 模糊的模糊宽度。零表示禁用。
             * 最大值为 125。
             */
            float blurWidth = 0.0f;
        } vsm;

        /**
         * Light bulb radius used for soft shadows. Currently this is only used when DPCF or PCSS is
         * enabled. (2cm by default).
         */
        /**
         * 用于柔和阴影的光源半径。目前仅在启用 DPCF 或 PCSS 时使用。
         * （默认 2cm）。
         */
        float shadowBulbRadius = 0.02f;

        /**
         * Transforms the shadow direction. Must be a unit quaternion.
         * The default is identity.
         * Ignored if the light type isn't directional. For artistic use. Use with caution.
         */
        /**
         * 变换阴影方向。必须是单位四元数。
         * 默认为单位变换。
         * 如果光源类型不是方向光则忽略。用于艺术效果。请谨慎使用。
         */
        math::quatf transform{ 1.0f };
    };

    /**
     * 阴影级联实用工具。
     */
    struct ShadowCascades {
        /**
         * Utility method to compute ShadowOptions::cascadeSplitPositions according to a uniform
         * split scheme.
         *
         * @param splitPositions    a float array of at least size (cascades - 1) to write the split
         *                          positions into
         * @param cascades          the number of shadow cascades, at most 4
         */
        /**
         * 根据统一分割方案计算 ShadowOptions::cascadeSplitPositions 的实用方法。
         *
         * @param splitPositions    至少大小为 (cascades - 1) 的 float 数组，用于写入分割位置
         * @param cascades          阴影级联数量，最多 4
         */
        static void computeUniformSplits(float* UTILS_NONNULL splitPositions, uint8_t cascades);

        /**
         * Utility method to compute ShadowOptions::cascadeSplitPositions according to a logarithmic
         * split scheme.
         *
         * @param splitPositions    a float array of at least size (cascades - 1) to write the split
         *                          positions into
         * @param cascades          the number of shadow cascades, at most 4
         * @param near              the camera near plane
         * @param far               the camera far plane
         */
        /**
         * 根据对数分割方案计算 ShadowOptions::cascadeSplitPositions 的实用方法。
         *
         * @param splitPositions    至少大小为 (cascades - 1) 的 float 数组，用于写入分割位置
         * @param cascades          阴影级联数量，最多 4
         * @param near              相机近平面
         * @param far               相机远平面
         */
        static void computeLogSplits(float* UTILS_NONNULL splitPositions, uint8_t cascades,
                float near, float far);

        /**
         * Utility method to compute ShadowOptions::cascadeSplitPositions according to a practical
         * split scheme.
         *
         * The practical split scheme uses uses a lambda value to interpolate between the logarithmic
         * and uniform split schemes. Start with a lambda value of 0.5f and adjust for your scene.
         *
         * See: Zhang et al 2006, "Parallel-split shadow maps for large-scale virtual environments"
         *
         * @param splitPositions    a float array of at least size (cascades - 1) to write the split
         *                          positions into
         * @param cascades          the number of shadow cascades, at most 4
         * @param near              the camera near plane
         * @param far               the camera far plane
         * @param lambda            a float in the range [0, 1] that interpolates between log and
         *                          uniform split schemes
         */
        /**
         * 根据实用分割方案计算 ShadowOptions::cascadeSplitPositions 的实用方法。
         *
         * 实用分割方案使用 lambda 值在对数
         * 和统一分割方案之间进行插值。从 lambda 值 0.5f 开始，并根据场景进行调整。
         *
         * 参见：Zhang et al 2006, "Parallel-split shadow maps for large-scale virtual environments"
         *
         * @param splitPositions    至少大小为 (cascades - 1) 的 float 数组，用于写入分割位置
         * @param cascades          阴影级联数量，最多 4
         * @param near              相机近平面
         * @param far               相机远平面
         * @param lambda            范围 [0, 1] 内的 float，在对数和
         *                          统一分割方案之间插值
         */
        static void computePracticalSplits(float* UTILS_NONNULL splitPositions, uint8_t cascades,
                float near, float far, float lambda);
    };

    //! Use Builder to construct a Light object instance
    /**
     * 使用 Builder 构造光源对象实例
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
    public:
        /**
         * Creates a light builder and set the light's #Type.
         *
         * @param type #Type of Light object to create.
         */
        /**
         * 创建光源构建器并设置光源的 #Type。
         *
         * @param type 要创建的光源对象的 #Type。
         */
        explicit Builder(Type type) noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Enables or disables a light channel. Light channel 0 is enabled by default.
         *
         * @param channel Light channel to enable or disable, between 0 and 7.
         * @param enable Whether to enable or disable the light channel.
         * @return This Builder, for chaining calls.
         */
        /**
         * 启用或禁用光源通道。默认情况下启用光源通道 0。
         *
         * @param channel 要启用或禁用的光源通道，在 0 到 7 之间
         * @param enable 是否启用或禁用光源通道
         * @return 此 Builder，用于链接调用
         */
        Builder& lightChannel(unsigned int channel, bool enable = true) noexcept;

        /**
         * Whether this Light casts shadows (disabled by default)
         *
         * @param enable Enables or disables casting shadows from this Light.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 此光源是否投射阴影（默认禁用）
         *
         * @param enable 启用或禁用从此光源投射阴影
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& castShadows(bool enable) noexcept;

        /**
         * Sets the shadow-map options for this light.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 设置此光源的阴影贴图选项。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& shadowOptions(const ShadowOptions& options) noexcept;

        /**
         * Whether this light casts light (enabled by default)
         *
         * @param enable Enables or disables lighting from this Light.
         *
         * @return This Builder, for chaining calls.
         *
         * @note
         * In some situations it can be useful to have a light in the scene that doesn't
         * actually emit light, but does cast shadows.
         */
        /**
         * 此光源是否发光（默认启用）
         *
         * @param enable 启用或禁用从此光源发出的光照
         *
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 在某些情况下，在场景中有一个实际上不
         * 发光但确实投射阴影的光源可能很有用。
         */
        Builder& castLight(bool enable) noexcept;

        /**
         * Sets the initial position of the light in world space.
         *
         * @param position Light's position in world space. The default is at the origin.
         *
         * @return This Builder, for chaining calls.
         *
         * @note
         * The Light's position is ignored for directional lights (Type.DIRECTIONAL or Type.SUN)
         */
        /**
         * 设置光源在世界空间中的初始位置。
         *
         * @param position 光源在世界空间中的位置。默认在原点。
         *
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 对于方向光（Type.DIRECTIONAL 或 Type.SUN），光源的位置被忽略
         */
        Builder& position(const math::float3& position) noexcept;

        /**
         * Sets the initial direction of a light in world space.
         *
         * @param direction Light's direction in world space. Should be a unit vector.
         *                  The default is {0,-1,0}.
         *
         * @return This Builder, for chaining calls.
         *
         * @note
         * The Light's direction is ignored for Type.POINT lights.
         */
        /**
         * 设置光源在世界空间中的初始方向。
         *
         * @param direction 光源在世界空间中的方向。应为单位向量。
         *                  默认值为 {0,-1,0}。
         *
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 对于 Type.POINT 光源，光源的方向被忽略
         */
        Builder& direction(const math::float3& direction) noexcept;

        /**
         * Sets the initial color of a light.
         *
         * @param color Color of the light specified in the linear sRGB color-space.
         *              The default is white {1,1,1}.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 设置光源的初始颜色。
         *
         * @param color 在线性 sRGB 色彩空间中指定的光源颜色。
         *              默认值为白色 {1,1,1}。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& color(const LinearColor& color) noexcept;

        /**
         * Sets the initial intensity of a light.
         * @param intensity This parameter depends on the Light.Type:
         *                  - For directional lights, it specifies the illuminance in *lux*
         *                  (or *lumen/m^2*).
         *                  - For point lights and spot lights, it specifies the luminous power
         *                  in *lumen*.
         *
         * @return This Builder, for chaining calls.
         *
         * For example, the sun's illuminance is about 100,000 lux.
         *
         * This method overrides any prior calls to intensity or intensityCandela.
         *
         */
        /**
         * 设置光源的初始强度。
         * @param intensity 此参数取决于 Light.Type：
         *                  - 对于方向光，它指定照度（以 *lux* 为单位
         *                  （或 *lumen/m^2*））。
         *                  - 对于点光源和聚光灯，它指定光通量
         *                  （以 *lumen* 为单位）。
         *
         * @return 此 Builder，用于链接调用
         *
         * 例如，太阳的照度约为 100,000 lux。
         *
         * 此方法会覆盖之前对 intensity 或 intensityCandela 的任何调用。
         *
         */
        Builder& intensity(float intensity) noexcept;

        /**
         * Sets the initial intensity of a spot or point light in candela.
         *
         * @param intensity Luminous intensity in *candela*.
         *
         * @return This Builder, for chaining calls.
         *
         * @note
         * This method is equivalent to calling intensity(float intensity) for directional lights
         * (Type.DIRECTIONAL or Type.SUN).
         *
         * This method overrides any prior calls to intensity or intensityCandela.
         */
        /**
         * 设置聚光灯或点光源的初始强度（以坎德拉为单位）。
         *
         * @param intensity 光强度（以 *candela* 为单位）。
         *
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 对于方向光（Type.DIRECTIONAL 或 Type.SUN），此方法等效于调用 intensity(float intensity)
         *
         * 此方法会覆盖之前对 intensity 或 intensityCandela 的任何调用。
         */
        Builder& intensityCandela(float intensity) noexcept;

        /**
         * Sets the initial intensity of a light in watts.
         *
         * @param watts         Energy consumed by a lightbulb. It is related to the energy produced
         *                      and ultimately the brightness by the \p efficiency parameter.
         *                      This value is often available on the packaging of commercial
         *                      lightbulbs.
         *
         * @param efficiency    Efficiency in percent. This depends on the type of lightbulb used.
         *
         *  Lightbulb type  | Efficiency
         * ----------------:|-----------:
         *     Incandescent |  2.2%
         *         Halogen  |  7.0%
         *             LED  |  8.7%
         *     Fluorescent  | 10.7%
         *
         * @return This Builder, for chaining calls.
         *
         *
         * @note
         * This call is equivalent to `Builder::intensity(efficiency * 683 * watts);`
         *
         * This method overrides any prior calls to intensity or intensityCandela.
         */
        /**
         * 以瓦特为单位设置光源的初始强度。
         *
         * @param watts         灯泡消耗的能量。它与产生的能量相关，
         *                      并最终通过 \p efficiency 参数影响亮度。
         *                      此值通常在商业
         *                      灯泡的包装上提供。
         *
         * @param efficiency    效率（百分比）。这取决于使用的灯泡类型。
         *
         *  灯泡类型      | 效率
         * ----------------:|-----------:
         *     白炽灯     |  2.2%
         *     卤素灯     |  7.0%
         *     LED        |  8.7%
         *     荧光灯     | 10.7%
         *
         * @return 此 Builder，用于链接调用
         *
         *
         * @note
         * 此调用等效于 `Builder::intensity(efficiency * 683 * watts);`
         *
         * 此方法会覆盖之前对 intensity 或 intensityCandela 的任何调用。
         */
        Builder& intensity(float watts, float efficiency) noexcept;

        /**
         * Set the falloff distance for point lights and spot lights.
         *
         * At the falloff distance, the light has no more effect on objects.
         *
         * The falloff distance essentially defines a *sphere of influence* around the light, and
         * therefore has an impact on performance. Larger falloffs might reduce performance
         * significantly, especially when many lights are used.
         *
         * Try to avoid having a large number of light's spheres of influence overlap.
         *
         * @param radius Falloff distance in world units. Default is 1 meter.
         *
         * @return This Builder, for chaining calls.
         *
         * @note
         * The Light's falloff is ignored for directional lights (Type.DIRECTIONAL or Type.SUN)
         */
        /**
         * 设置点光源和聚光灯的衰减距离。
         *
         * 在衰减距离处，光对物体不再有影响。
         *
         * 衰减距离本质上定义了光源周围的*影响球体*，因此
         * 会影响性能。较大的衰减可能会显著降低性能，
         * 尤其是在使用多个光源时。
         *
         * 尽量避免大量光源的影响球体重叠。
         *
         * @param radius 世界单位中的衰减距离。默认值为 1 米。
         *
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 对于方向光（Type.DIRECTIONAL 或 Type.SUN），光源的衰减被忽略
         */
        Builder& falloff(float radius) noexcept;

        /**
         * Defines a spot light'st angular falloff attenuation.
         *
         * A spot light is defined by a position, a direction and two cones, \p inner and \p outer.
         * These two cones are used to define the angular falloff attenuation of the spot light
         * and are defined by the angle from the center axis to where the falloff begins (i.e.
         * cones are defined by their half-angle).
         *
         * Both inner and outer are silently clamped to a minimum value of 0.5 degrees
         * (~0.00873 radians) to avoid floating-point precision issues during rendering.
         *
         * @param inner inner cone angle in *radians* between 0.00873 and \p outer
         * @param outer outer cone angle in *radians* between 0.00873 inner and @f$ \pi/2 @f$
         * @return This Builder, for chaining calls.
         *
         * @note
         * The spot light cone is ignored for directional and point lights.
         *
         * @see Type.SPOT, Type.FOCUSED_SPOT
         */
        /**
         * 定义聚光灯的角度衰减。
         *
         * 聚光灯由位置、方向和两个圆锥（\p inner 和 \p outer）定义。
         * 这两个圆锥用于定义聚光灯的角度衰减，
         * 并由从中心轴到衰减开始位置的角度定义（即
         * 圆锥由其半角定义）。
         *
         * inner 和 outer 都会静默限制为最小值 0.5 度
         * （~0.00873 弧度）以避免渲染期间的浮点精度问题。
         *
         * @param inner 内圆锥角（以 *radians* 为单位），在 0.00873 和 \p outer 之间
         * @param outer 外圆锥角（以 *radians* 为单位），在 0.00873 inner 和 @f$ \pi/2 @f$ 之间
         * @return 此 Builder，用于链接调用
         *
         * @note
         * 对于方向光和点光源，聚光灯圆锥被忽略。
         *
         * @see Type.SPOT, Type.FOCUSED_SPOT
         */
        Builder& spotLightCone(float inner, float outer) noexcept;

        /**
         * Defines the angular radius of the sun, in degrees, between 0.25° and 20.0°
         *
         * The Sun as seen from Earth has an angular size of 0.526° to 0.545°
         *
         * @param angularRadius sun's radius in degree. Default is 0.545°.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 定义太阳的角半径（以度为单位），在 0.25° 和 20.0° 之间
         *
         * 从地球看到的太阳的角大小约为 0.526° 到 0.545°
         *
         * @param angularRadius 太阳的半径（以度为单位）。默认值为 0.545°。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& sunAngularRadius(float angularRadius) noexcept;

        /**
         * Defines the halo radius of the sun. The radius of the halo is defined as a
         * multiplier of the sun angular radius.
         *
         * @param haloSize radius multiplier. Default is 10.0.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 定义太阳的光晕半径。光晕的半径定义为
         * 太阳角半径的倍数。
         *
         * @param haloSize 半径倍数。默认值为 10.0。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& sunHaloSize(float haloSize) noexcept;

        /**
         * Defines the halo falloff of the sun. The falloff is a dimensionless number
         * used as an exponent.
         *
         * @param haloFalloff halo falloff. Default is 80.0.
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 定义太阳的光晕衰减。衰减是一个无量纲数字，
         * 用作指数。
         *
         * @param haloFalloff 光晕衰减。默认值为 80.0。
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& sunHaloFalloff(float haloFalloff) noexcept;

        /**
         * 构建结果枚举。
         */
        enum Result { Error = -1, Success = 0  };

        /**
         * Adds the Light component to an entity.
         *
         * @param engine Reference to the filament::Engine to associate this light with.
         * @param entity Entity to add the light component to.
         * @return Success if the component was created successfully, Error otherwise.
         *
         * If exceptions are disabled and an error occurs, this function is a no-op.
         *        Success can be checked by looking at the return value.
         *
         * If this component already exists on the given entity, it is first destroyed as if
         * destroy(utils::Entity e) was called.
         *
         * @warning
         * Currently, only 2048 lights can be created on a given Engine.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 将光源组件添加到实体。
         *
         * @param engine 要与此光源关联的 filament::Engine 的引用
         * @param entity 要添加光源组件的实体
         * @return 如果组件创建成功则返回 Success，否则返回 Error
         *
         * 如果禁用了异常并且发生错误，此函数是无操作。
         *        可以通过查看返回值来检查成功。
         *
         * 如果给定实体上已存在此组件，则首先销毁它，就像调用了
         * destroy(utils::Entity e) 一样。
         *
         * @warning
         * 目前，在给定的 Engine 上只能创建 2048 个光源。
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         */
        Result build(Engine& engine, utils::Entity entity);

    private:
        friend class FEngine;
        friend class FLightManager;
    };

    static constexpr float EFFICIENCY_INCANDESCENT = 0.0220f;   //!< Typical efficiency of an incandescent light bulb (2.2%)
    /** 白炽灯泡的典型效率（2.2%） */
    static constexpr float EFFICIENCY_HALOGEN      = 0.0707f;   //!< Typical efficiency of an halogen light bulb (7.0%)
    /** 卤素灯泡的典型效率（7.0%） */
    static constexpr float EFFICIENCY_FLUORESCENT  = 0.0878f;   //!< Typical efficiency of a fluorescent light bulb (8.7%)
    /** 荧光灯泡的典型效率（8.7%） */
    static constexpr float EFFICIENCY_LED          = 0.1171f;   //!< Typical efficiency of a LED light bulb (11.7%)
    /** LED 灯泡的典型效率（11.7%） */

    /**
     * 返回光源的类型。
     * @param i 从 getInstance() 获取的组件实例
     * @return 光源类型
     */
    Type getType(Instance i) const noexcept;

    /**
     * Helper function that returns if a light is a directional light
     *
     * @param i     Instance of the component obtained from getInstance().
     * @return      true is this light is a type of directional light
     */
    /**
     * 辅助函数，返回光源是否为方向光
     *
     * @param i     从 getInstance() 获取的组件实例
     * @return      如果此光源是方向光类型则返回 true
     */
    inline bool isDirectional(Instance i) const noexcept {
        Type const type = getType(i);
        return type == Type::DIRECTIONAL || type == Type::SUN;
    }

    /**
     * Helper function that returns if a light is a point light
     *
     * @param i     Instance of the component obtained from getInstance().
     * @return      true is this light is a type of point light
     */
    /**
     * 辅助函数，返回光源是否为点光源
     *
     * @param i     从 getInstance() 获取的组件实例
     * @return      如果此光源是点光源类型则返回 true
     */
    inline bool isPointLight(Instance i) const noexcept {
        return getType(i) == Type::POINT;
    }

    /**
     * Helper function that returns if a light is a spot light
     *
     * @param i     Instance of the component obtained from getInstance().
     * @return      true is this light is a type of spot light
     */
    /**
     * 辅助函数，返回光源是否为聚光灯
     *
     * @param i     从 getInstance() 获取的组件实例
     * @return      如果此光源是聚光灯类型则返回 true
     */
    inline bool isSpotLight(Instance i) const noexcept {
        Type const type = getType(i);
        return type == Type::SPOT || type == Type::FOCUSED_SPOT;
    }

    /**
     * Enables or disables a light channel. Light channel 0 is enabled by default.
     * @param channel light channel to enable or disable, between 0 and 7.
     * @param enable whether to enable (true) or disable (false) the specified light channel.
     */
    /**
     * 启用或禁用光源通道。默认情况下启用光源通道 0。
     * @param channel 要启用或禁用的光源通道，在 0 到 7 之间
     * @param enable 是否启用（true）或禁用（false）指定的光源通道
     */
    void setLightChannel(Instance i, unsigned int channel, bool enable = true) noexcept;

    /**
     * Returns whether a light channel is enabled on a specified light.
     * @param i        Instance of the component obtained from getInstance().
     * @param channel  Light channel to query
     * @return         true if the light channel is enabled, false otherwise
     */
    /**
     * 返回指定光源上是否启用了光源通道。
     * @param i        从 getInstance() 获取的组件实例
     * @param channel  要查询的光源通道
     * @return         如果启用了光源通道则返回 true，否则返回 false
     */
    bool getLightChannel(Instance i, unsigned int channel) const noexcept;

    /**
     * Dynamically updates the light's position.
     *
     * @param i        Instance of the component obtained from getInstance().
     * @param position Light's position in world space. The default is at the origin.
     *
     * @see Builder.position()
     */
    void setPosition(Instance i, const math::float3& position) noexcept;

    //! returns the light's position in world space
    /**
     * 返回光源在世界空间中的位置
     * @param i 从 getInstance() 获取的组件实例
     * @return 光源在世界空间中的位置
     */
    const math::float3& getPosition(Instance i) const noexcept;

    /**
     * Dynamically updates the light's direction
     *
     * @param i         Instance of the component obtained from getInstance().
     * @param direction Light's direction in world space. Should be a unit vector.
     *                  The default is {0,-1,0}.
     *
     * @see Builder.direction()
     */
    /**
     * 动态更新光源的方向
     *
     * @param i         从 getInstance() 获取的组件实例
     * @param direction 光源在世界空间中的方向。应为单位向量。
     *                  默认值为 {0,-1,0}。
     *
     * @see Builder.direction()
     */
    void setDirection(Instance i, const math::float3& direction) noexcept;

    //! returns the light's direction in world space
    /**
     * 返回光源在世界空间中的方向
     * @param i 从 getInstance() 获取的组件实例
     * @return 光源在世界空间中的方向
     */
    const math::float3& getDirection(Instance i) const noexcept;

    /**
     * Dynamically updates the light's hue as linear sRGB
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param color Color of the light specified in the linear sRGB color-space.
     *              The default is white {1,1,1}.
     *
     * @see Builder.color(), getInstance()
     */
    /**
     * 动态更新光源的颜色（以线性 sRGB 表示）
     *
     * @param i     从 getInstance() 获取的组件实例
     * @param color 在线性 sRGB 色彩空间中指定的光源颜色。
     *              默认值为白色 {1,1,1}。
     *
     * @see Builder.color(), getInstance()
     */
    void setColor(Instance i, const LinearColor& color) noexcept;

    /**
     * @param i     Instance of the component obtained from getInstance().
     * @return the light's color in linear sRGB
     */
    /**
     * @param i     从 getInstance() 获取的组件实例
     * @return 光源的颜色（以线性 sRGB 表示）
     */
    const math::float3& getColor(Instance i) const noexcept;

    /**
     * Dynamically updates the light's intensity. The intensity can be negative.
     *
     * @param i         Instance of the component obtained from getInstance().
     * @param intensity This parameter depends on the Light.Type:
     *                  - For directional lights, it specifies the illuminance in *lux*
     *                  (or *lumen/m^2*).
     *                  - For point lights and spot lights, it specifies the luminous power
     *                  in *lumen*.
     *
     * @see Builder.intensity()
     */
    /**
     * 动态更新光源的强度。强度可以为负。
     *
     * @param i         从 getInstance() 获取的组件实例
     * @param intensity 此参数取决于 Light.Type：
     *                  - 对于方向光，它指定照度（以 *lux* 为单位
     *                  （或 *lumen/m^2*））。
     *                  - 对于点光源和聚光灯，它指定光通量
     *                  （以 *lumen* 为单位）。
     *
     * @see Builder.intensity()
     */
    void setIntensity(Instance i, float intensity) noexcept;

    /**
     * Dynamically updates the light's intensity. The intensity can be negative.
     *
     * @param i             Instance of the component obtained from getInstance().
     * @param watts         Energy consumed by a lightbulb. It is related to the energy produced
     *                      and ultimately the brightness by the \p efficiency parameter.
     *                      This value is often available on the packaging of commercial
     *                      lightbulbs.
     * @param efficiency    Efficiency in percent. This depends on the type of lightbulb used.
     *
     *  Lightbulb type  | Efficiency
     * ----------------:|-----------:
     *     Incandescent |  2.2%
     *         Halogen  |  7.0%
     *             LED  |  8.7%
     *     Fluorescent  | 10.7%
     *
     * @see Builder.intensity(float watts, float efficiency)
     */
    /**
     * 动态更新光源的强度（以瓦特为单位）。强度可以为负。
     *
     * @param i             从 getInstance() 获取的组件实例
     * @param watts         灯泡消耗的能量。它与产生的能量相关，
     *                      并最终通过 \p efficiency 参数影响亮度。
     *                      此值通常在商业灯泡的包装上提供。
     * @param efficiency    效率（百分比）。这取决于使用的灯泡类型。
     *
     *  灯泡类型      | 效率
     * ----------------:|-----------:
     *     白炽灯     |  2.2%
     *     卤素灯     |  7.0%
     *     LED        |  8.7%
     *     荧光灯     | 10.7%
     *
     * @see Builder.intensity(float watts, float efficiency)
     */
    void setIntensity(Instance i, float watts, float efficiency) noexcept {
        setIntensity(i, watts * 683.0f * efficiency);
    }

    /**
     * Dynamically updates the light's intensity in candela. The intensity can be negative.
     *
     * @param i         Instance of the component obtained from getInstance().
     * @param intensity Luminous intensity in *candela*.
     *
     * @note
     * This method is equivalent to calling setIntensity(float intensity) for directional lights
     * (Type.DIRECTIONAL or Type.SUN).
     *
     * @see Builder.intensityCandela(float intensity)
     */
    /**
     * 动态更新光源的强度（以坎德拉为单位）。强度可以为负。
     *
     * @param i         从 getInstance() 获取的组件实例
     * @param intensity 光强度（以 *candela* 为单位）。
     *
     * @note
     * 对于方向光（Type.DIRECTIONAL 或 Type.SUN），此方法等效于调用 setIntensity(float intensity)
     *
     * @see Builder.intensityCandela(float intensity)
     */
    void setIntensityCandela(Instance i, float intensity) noexcept;

    /**
     * returns the light's luminous intensity in candela.
     *
     * @param i     Instance of the component obtained from getInstance().
     *
     * @note for Type.FOCUSED_SPOT lights, the returned value depends on the \p outer cone angle.
     *
     * @return luminous intensity in candela.
     */
    /**
     * 返回光源的光强度（以坎德拉为单位）。
     *
     * @param i     从 getInstance() 获取的组件实例
     *
     * @note 对于 Type.FOCUSED_SPOT 光源，返回值取决于 \p outer 圆锥角。
     *
     * @return 光强度（以坎德拉为单位）
     */
    float getIntensity(Instance i) const noexcept;

    /**
     * Set the falloff distance for point lights and spot lights.
     *
     * @param i      Instance of the component obtained from getInstance().
     * @param radius falloff distance in world units. Default is 1 meter.
     *
     * @see Builder.falloff()
     */
    /**
     * 设置点光源和聚光灯的衰减距离。
     *
     * @param i      从 getInstance() 获取的组件实例
     * @param radius 世界单位中的衰减距离。默认值为 1 米。
     *
     * @see Builder.falloff()
     */
    void setFalloff(Instance i, float radius) noexcept;

    /**
     * returns the falloff distance of this light.
     * @param i     Instance of the component obtained from getInstance().
     * @return the falloff distance of this light.
     */
    /**
     * 返回此光源的衰减距离。
     * @param i     从 getInstance() 获取的组件实例
     * @return 此光源的衰减距离
     */
    float getFalloff(Instance i) const noexcept;

    /**
     * Dynamically updates a spot light's cone as angles
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param inner inner cone angle in *radians* between 0.00873 and outer
     * @param outer outer cone angle in *radians* between 0.00873 and pi/2
     *
     * @see Builder.spotLightCone()
     */
    /**
     * 动态更新聚光灯的圆锥（以角度表示）
     *
     * @param i     从 getInstance() 获取的组件实例
     * @param inner 内圆锥角（以 *radians* 为单位），在 0.00873 和 outer 之间
     * @param outer 外圆锥角（以 *radians* 为单位），在 0.00873 和 pi/2 之间
     *
     * @see Builder.spotLightCone()
     */
    void setSpotLightCone(Instance i, float inner, float outer) noexcept;

    /**
     * returns the outer cone angle in *radians* between inner and pi/2.
     * @param i     Instance of the component obtained from getInstance().
     * @return the outer cone angle of this light.
     */
    /**
     * 返回外圆锥角（以 *radians* 为单位），在 inner 和 pi/2 之间。
     * @param i     从 getInstance() 获取的组件实例
     * @return 此光源的外圆锥角
     */
    float getSpotLightOuterCone(Instance i) const noexcept;

    /**
     * returns the inner cone angle in *radians* between 0 and pi/2.
     * 
     * The value is recomputed from the initial values, thus is not precisely
     * the same as the one passed to setSpotLightCone() or Builder.spotLightCone().
     * 
     * @param i     Instance of the component obtained from getInstance().
     * @return the inner cone angle of this light.
     */
    /**
     * 返回内圆锥角（以 *radians* 为单位），在 0 和 pi/2 之间。
     * 
     * 该值是从初始值重新计算的，因此与传递给
     * setSpotLightCone() 或 Builder.spotLightCone() 的值不完全相同。
     * 
     * @param i     从 getInstance() 获取的组件实例
     * @return 此光源的内圆锥角
     */
    float getSpotLightInnerCone(Instance i) const noexcept;

    /**
     * Dynamically updates the angular radius of a Type.SUN light
     *
     * The Sun as seen from Earth has an angular size of 0.526° to 0.545°
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param angularRadius sun's radius in degrees. Default is 0.545°.
     */
    /**
     * 动态更新 Type.SUN 光源的角半径
     *
     * 从地球看到的太阳的角大小约为 0.526° 到 0.545°
     *
     * @param i             从 getInstance() 获取的组件实例
     * @param angularRadius 太阳的半径（以度为单位）。默认值为 0.545°。
     */
    void setSunAngularRadius(Instance i, float angularRadius) noexcept;

    /**
     * returns the angular radius if the sun in degrees.
     * @param i     Instance of the component obtained from getInstance().
     * @return the angular radius if the sun in degrees.
     */
    /**
     * 返回太阳的角半径（以度为单位）。
     * @param i     从 getInstance() 获取的组件实例
     * @return 太阳的角半径（以度为单位）
     */
    float getSunAngularRadius(Instance i) const noexcept;

    /**
     * Dynamically updates the halo radius of a Type.SUN light. The radius
     * of the halo is defined as a multiplier of the sun angular radius.
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param haloSize radius multiplier. Default is 10.0.
     */
    /**
     * 动态更新 Type.SUN 光源的光晕半径。光晕的半径
     * 定义为太阳角半径的倍数。
     *
     * @param i        从 getInstance() 获取的组件实例
     * @param haloSize 半径倍数。默认值为 10.0。
     */
    void setSunHaloSize(Instance i, float haloSize) noexcept;

    /**
     * returns the halo size of a Type.SUN light as a multiplier of the
     * sun angular radius.
     * @param i     Instance of the component obtained from getInstance().
     * @return the halo size
     */
    /**
     * 返回 Type.SUN 光源的光晕大小，作为太阳角半径的倍数。
     * @param i     从 getInstance() 获取的组件实例
     * @return 光晕大小
     */
    float getSunHaloSize(Instance i) const noexcept;

    /**
     * Dynamically updates the halo falloff of a Type.SUN light. The falloff
     * is a dimensionless number used as an exponent.
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param haloFalloff halo falloff. Default is 80.0.
     */
    /**
     * 动态更新 Type.SUN 光源的光晕衰减。衰减
     * 是一个无量纲数字，用作指数。
     *
     * @param i          从 getInstance() 获取的组件实例
     * @param haloFalloff 光晕衰减。默认值为 80.0。
     */
    void setSunHaloFalloff(Instance i, float haloFalloff) noexcept;

    /**
     * returns the halo falloff of a Type.SUN light as a dimensionless value.
     * @param i     Instance of the component obtained from getInstance().
     * @return the halo falloff
     */
    /**
     * 返回 Type.SUN 光源的光晕衰减（作为无量纲值）。
     * @param i     从 getInstance() 获取的组件实例
     * @return 光晕衰减
     */
    float getSunHaloFalloff(Instance i) const noexcept;

    /**
     * returns the shadow-map options for a given light
     * @param i     Instance of the component obtained from getInstance().
     * @return      A ShadowOption structure
     */
    /**
     * 返回给定光源的阴影贴图选项
     * @param i     从 getInstance() 获取的组件实例
     * @return      阴影选项结构体
     */
    ShadowOptions const& getShadowOptions(Instance i) const noexcept;

    /**
     * sets the shadow-map options for a given light
     * @param i     Instance of the component obtained from getInstance().
     * @param options  A ShadowOption structure
     */
    /**
     * 设置给定光源的阴影贴图选项
     * @param i       从 getInstance() 获取的组件实例
     * @param options 阴影选项结构体
     */
    void setShadowOptions(Instance i, ShadowOptions const& options) noexcept;

    /**
     * Whether this Light casts shadows (disabled by default)
     *
     * @param i     Instance of the component obtained from getInstance().
     * @param shadowCaster Enables or disables casting shadows from this Light.
     *
     * @warning
     * - Only a Type.DIRECTIONAL, Type.SUN, Type.SPOT, or Type.FOCUSED_SPOT light can cast shadows
     */
    /**
     * 此光源是否投射阴影（默认禁用）
     *
     * @param i           从 getInstance() 获取的组件实例
     * @param shadowCaster 启用或禁用从此光源投射阴影
     *
     * @warning
     * - 只有 Type.DIRECTIONAL、Type.SUN、Type.SPOT 或 Type.FOCUSED_SPOT 光源可以投射阴影
     */
    void setShadowCaster(Instance i, bool shadowCaster) noexcept;

    /**
     * returns whether this light casts shadows.
     * @param i     Instance of the component obtained from getInstance().
     */
    /**
     * 返回此光源是否投射阴影。
     * @param i     从 getInstance() 获取的组件实例
     * @return 如果此光源投射阴影则返回 true，否则返回 false
     */
    bool isShadowCaster(Instance i) const noexcept;

protected:
    // prevent heap allocation
    ~LightManager() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_LIGHTMANAGER_H
