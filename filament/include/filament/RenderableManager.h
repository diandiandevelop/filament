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

#ifndef TNT_FILAMENT_RENDERABLEMANAGER_H
#define TNT_FILAMENT_RENDERABLEMANAGER_H

#include <filament/Box.h>
#include <filament/FilamentAPI.h>
#include <filament/MaterialEnums.h>
#include <filament/MorphTargetBuffer.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/EntityInstance.h>
#include <utils/FixedCapacityVector.h>

#include <math/mathfwd.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <type_traits>

#include <float.h>
#include <stddef.h>
#include <stdint.h>

namespace utils {
    class Entity;
} // namespace utils

namespace filament {

class BufferObject;
class Engine;
class IndexBuffer;
class MaterialInstance;
class Renderer;
class SkinningBuffer;
class VertexBuffer;
class Texture;
class InstanceBuffer;

class FEngine;
class FRenderPrimitive;
class FRenderableManager;

/**
 * Factory and manager for \em renderables, which are entities that can be drawn.
 *
 * Renderables are bundles of \em primitives, each of which has its own geometry and material. All
 * primitives in a particular renderable share a set of rendering attributes, such as whether they
 * cast shadows or use vertex skinning.
 *
 * Usage example:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * auto renderable = utils::EntityManager::get().create();
 *
 * RenderableManager::Builder(1)
 *         .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
 *         .material(0, matInstance)
 *         .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vertBuffer, indBuffer, 0, 3)
 *         .receiveShadows(false)
 *         .build(engine, renderable);
 *
 * scene->addEntity(renderable);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To modify the state of an existing renderable, clients should first use RenderableManager
 * to get a temporary handle called an \em instance. The instance can then be used to get or set
 * the renderable's state. Please note that instances are ephemeral; clients should store entities,
 * not instances.
 *
 * - For details about constructing renderables, see RenderableManager::Builder.
 * - To associate a 4x4 transform with an entity, see TransformManager.
 * - To associate a human-readable label with an entity, see utils::NameComponentManager.
 */
/**
 * 可渲染对象的工厂和管理器，可渲染对象是可以绘制的实体。
 *
 * 可渲染对象是图元的集合，每个图元都有自己的几何体和材质。特定可渲染对象中的
 * 所有图元共享一组渲染属性，例如它们是否投射阴影或使用顶点蒙皮。
 *
 * 使用示例：
 *
 * 要修改现有可渲染对象的状态，客户端应首先使用 RenderableManager
 * 获取一个称为实例的临时句柄。然后可以使用该实例来获取或设置
 * 可渲染对象的状态。请注意，实例是临时的；客户端应存储实体，
 * 而不是实例。
 *
 * - 有关构造可渲染对象的详细信息，请参见 RenderableManager::Builder。
 * - 要将 4x4 变换与实体关联，请参见 TransformManager。
 * - 要将人类可读的标签与实体关联，请参见 utils::NameComponentManager。
 */
class UTILS_PUBLIC RenderableManager : public FilamentAPI {
    struct BuilderDetails;

public:
    using Instance = utils::EntityInstance<RenderableManager>;
    using PrimitiveType = backend::PrimitiveType;

    /**
     * Checks if the given entity already has a renderable component.
     */
    /**
     * 检查给定实体是否已有可渲染组件。
     */
    bool hasComponent(utils::Entity e) const noexcept;

    /**
     * Gets a temporary handle that can be used to access the renderable state.
     *
     * @return Non-zero handle if the entity has a renderable component, 0 otherwise.
     */
    /**
     * 获取可用于访问可渲染对象状态的临时句柄。
     *
     * @return 如果实体有可渲染组件则返回非零句柄，否则返回 0
     */
    Instance getInstance(utils::Entity e) const noexcept;

    /**
     * @return the number of Components
     */
    /**
     * @return 组件数量
     */
    size_t getComponentCount() const noexcept;

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
     * @return 实体
     */
    utils::Entity getEntity(Instance i) const noexcept;

    /**
     * Retrieve the Entities of all the components of this manager.
     * @return A list, in no particular order, of all the entities managed by this manager.
     */
    /**
     * 检索此管理器所有组件的实体。
     * @return 此管理器管理的所有实体的列表（无特定顺序）
     */
    utils::Entity const* UTILS_NONNULL getEntities() const noexcept;

    /**
     * The transformation associated with a skinning joint.
     *
     * Clients can specify bones either using this quat-vec3 pair, or by using 4x4 matrices.
     */
    /**
     * 与蒙皮关节关联的变换。
     *
     * 客户端可以使用此四元数-vec3 对或使用 4x4 矩阵来指定骨骼。
     */
    struct Bone {
        math::quatf unitQuaternion = { 1.f, 0.f, 0.f, 0.f };  //!< 单位四元数
        math::float3 translation = { 0.f, 0.f, 0.f };          //!< 平移
        float reserved = 0;                                     //!< 保留字段
    };

    /**
     * Adds renderable components to entities using a builder pattern.
     */
    /**
     * 使用构建器模式向实体添加可渲染组件。
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
    public:
        enum Result { Error = -1, Success = 0  };  //!< 构建结果：错误或成功

        /**
         * Default render channel
         * @see Builder::channel()
         */
        /**
         * 默认渲染通道
         * @see Builder::channel()
         */
        static constexpr uint8_t DEFAULT_CHANNEL = 2u;

        /**
         * Type of geometry for a Renderable
         */
        /**
         * 可渲染对象的几何类型
         */
        enum class GeometryType : uint8_t {
            DYNAMIC,        //!< dynamic gemoetry has no restriction
            /**
             * 动态几何体没有限制
             */
            STATIC_BOUNDS,  //!< bounds and world space transform are immutable
            /**
             * 边界和世界空间变换是不可变的
             */
            STATIC          //!< skinning/morphing not allowed and Vertex/IndexBuffer immutables
            /**
             * 不允许蒙皮/变形，且 Vertex/IndexBuffer 不可变
             */
        };

        /**
         * Creates a builder for renderable components.
         *
         * @param count the number of primitives that will be supplied to the builder
         *
         * Note that builders typically do not have a long lifetime since clients should discard
         * them after calling build(). For a usage example, see RenderableManager.
         */
        /**
         * 创建可渲染组件的构建器。
         *
         * @param count 将提供给构建器的图元数量
         *
         * 请注意，构建器通常没有很长的生命周期，因为客户端应在调用 build() 后丢弃它们。
         * 有关使用示例，请参见 RenderableManager。
         */
        explicit Builder(size_t count) noexcept;

        /*! \cond PRIVATE */
        Builder(Builder const& rhs) = delete;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder& rhs) = delete;
        Builder& operator=(Builder&& rhs) noexcept;
        /*! \endcond */

        /**
         * Specifies the geometry data for a primitive.
         *
         * Filament primitives must have an associated VertexBuffer and IndexBuffer. Typically, each
         * primitive is specified with a pair of daisy-chained calls: \c geometry(...) and \c
         * material(...).
         *
         * @param index zero-based index of the primitive, must be less than the count passed to Builder constructor
         * @param type specifies the topology of the primitive (e.g., \c RenderableManager::PrimitiveType::TRIANGLES)
         * @param vertices specifies the vertex buffer, which in turn specifies a set of attributes
         * @param indices specifies the index buffer (either u16 or u32)
         * @param offset specifies where in the index buffer to start reading (expressed as a number of indices)
         * @param minIndex specifies the minimum index contained in the index buffer
         * @param maxIndex specifies the maximum index contained in the index buffer
         * @param count number of indices to read (for triangles, this should be a multiple of 3)
         */
        /**
         * 指定图元的几何数据。
         *
         * Filament 图元必须有关联的 VertexBuffer 和 IndexBuffer。通常，每个
         * 图元通过一对链式调用来指定：\c geometry(...) 和 \c material(...)。
         *
         * @param index 图元的从零开始的索引，必须小于传递给 Builder 构造函数的 count
         * @param type 指定图元的拓扑（例如，\c RenderableManager::PrimitiveType::TRIANGLES）
         * @param vertices 指定顶点缓冲区，它又指定一组属性
         * @param indices 指定索引缓冲区（u16 或 u32）
         * @param offset 指定在索引缓冲区中开始读取的位置（以索引数量表示）
         * @param minIndex 指定索引缓冲区中包含的最小索引
         * @param maxIndex 指定索引缓冲区中包含的最大索引
         * @param count 要读取的索引数量（对于三角形，这应该是 3 的倍数）
         */
        Builder& geometry(size_t index, PrimitiveType type,
                VertexBuffer* UTILS_NONNULL vertices,
                IndexBuffer* UTILS_NONNULL indices,
                size_t offset, size_t minIndex, size_t maxIndex, size_t count) noexcept;

        Builder& geometry(size_t index, PrimitiveType type,
                VertexBuffer* UTILS_NONNULL vertices,
                IndexBuffer* UTILS_NONNULL indices,
                size_t offset, size_t count) noexcept; //!< \overload
        /**
         * 重载版本：不指定 minIndex 和 maxIndex
         */

        Builder& geometry(size_t index, PrimitiveType type,
                VertexBuffer* UTILS_NONNULL vertices,
                IndexBuffer* UTILS_NONNULL indices) noexcept; //!< \overload
        /**
         * 重载版本：从索引缓冲区开头读取所有索引
         */


        /**
         * Specify the type of geometry for this renderable. DYNAMIC geometry has no restriction,
         * STATIC_BOUNDS geometry means that both the bounds and the world-space transform of the
         * the renderable are immutable.
         * STATIC geometry has the same restrictions as STATIC_BOUNDS, but in addition disallows
         * skinning, morphing and changing the VertexBuffer or IndexBuffer in any way.
         * @param type type of geometry.
         */
        /**
         * 指定此可渲染对象的几何类型。DYNAMIC 几何体没有限制，
         * STATIC_BOUNDS 几何体意味着可渲染对象的边界和世界空间变换都是不可变的。
         * STATIC 几何体具有与 STATIC_BOUNDS 相同的限制，但此外还禁止
         * 蒙皮、变形和以任何方式更改 VertexBuffer 或 IndexBuffer。
         * @param type 几何类型
         */
        Builder& geometryType(GeometryType type) noexcept;

        /**
         * Binds a material instance to the specified primitive.
         *
         * If no material is specified for a given primitive, Filament will fall back to a basic
         * default material.
         *
         * The MaterialInstance's material must have a feature level equal or lower to the engine's
         * selected feature level.
         *
         * @param index zero-based index of the primitive, must be less than the count passed to
         * Builder constructor
         * @param materialInstance the material to bind
         *
         * @see Engine::setActiveFeatureLevel
         */
        /**
         * 将材质实例绑定到指定的图元。
         *
         * 如果未为给定图元指定材质，Filament 将回退到基本默认材质。
         *
         * MaterialInstance 的材质必须具有等于或低于引擎所选功能级别的功能级别。
         *
         * @param index 图元的从零开始的索引，必须小于传递给 Builder 构造函数的 count
         * @param materialInstance 要绑定的材质
         *
         * @see Engine::setActiveFeatureLevel
         */
        Builder& material(size_t index,
                MaterialInstance const* UTILS_NONNULL materialInstance) noexcept;

        /**
         * The axis-aligned bounding box of the renderable.
         *
         * This is an object-space AABB used for frustum culling. For skinning and morphing, this
         * should encompass all possible vertex positions. It is mandatory unless culling is
         * disabled for the renderable.
         *
         * \see computeAABB()
         */
        /**
         * 可渲染对象的轴对齐包围盒。
         *
         * 这是用于视锥剔除的对象空间 AABB。对于蒙皮和变形，这
         * 应该包含所有可能的顶点位置。除非为可渲染对象禁用剔除，否则这是必需的。
         *
         * \see computeAABB()
         */
        Builder& boundingBox(const Box& axisAlignedBoundingBox) noexcept;

        /**
         * Sets bits in a visibility mask. By default, this is 0x1.
         *
         * This feature provides a simple mechanism for hiding and showing groups of renderables
         * in a Scene. See View::setVisibleLayers().
         *
         * For example, to set bit 1 and reset bits 0 and 2 while leaving all other bits unaffected,
         * do: `builder.layerMask(7, 2)`.
         *
         * To change this at run time, see RenderableManager::setLayerMask.
         *
         * @param select the set of bits to affect
         * @param values the replacement values for the affected bits
         */
        /**
         * 设置可见性掩码中的位。默认情况下，这是 0x1。
         *
         * 此功能提供了一种简单的机制来隐藏和显示场景中的可渲染对象组。
         * 请参见 View::setVisibleLayers()。
         *
         * 例如，要设置位 1 并重置位 0 和 2，同时保持所有其他位不变，
         * 执行：`builder.layerMask(7, 2)`。
         *
         * 要在运行时更改此设置，请参见 RenderableManager::setLayerMask。
         *
         * @param select 要影响的位集合
         * @param values 受影响位的替换值
         */
        Builder& layerMask(uint8_t select, uint8_t values) noexcept;

        /**
         * Provides coarse-grained control over draw order.
         *
         * In general Filament reserves the right to re-order renderables to allow for efficient
         * rendering. However clients can control ordering at a coarse level using \em priority.
         * The priority is applied separately for opaque and translucent objects, that is, opaque
         * objects are always drawn before translucent objects regardless of the priority.
         *
         * For example, this could be used to draw a semitransparent HUD on top of everything,
         * without using a separate View. Note that priority is completely orthogonal to
         * Builder::layerMask, which merely controls visibility.
         *
         * The Skybox always using the lowest priority, so it's drawn last, which may improve
         * performance.
         *
         * @param priority clamped to the range [0..7], defaults to 4; 7 is lowest priority
         *                 (rendered last).
         *
         * @return Builder reference for chaining calls.
         *
         * @see Builder::blendOrder()
         * @see Builder::channel()
         * @see RenderableManager::setPriority()
         * @see RenderableManager::setBlendOrderAt()
         */
        /**
         * 提供对绘制顺序的粗粒度控制。
         *
         * 通常，Filament 保留重新排序可渲染对象的权利，以允许高效
         * 渲染。但是，客户端可以使用 \em priority 在粗粒度级别控制排序。
         * 优先级分别应用于不透明和半透明对象，即不透明
         * 对象总是在半透明对象之前绘制，无论优先级如何。
         *
         * 例如，这可用于在所有内容之上绘制半透明 HUD，
         * 而无需使用单独的 View。请注意，优先级与
         * Builder::layerMask 完全正交，后者仅控制可见性。
         *
         * Skybox 始终使用最低优先级，因此最后绘制，这可能会提高性能。
         *
         * @param priority 限制在范围 [0..7] 内，默认为 4；7 是最低优先级（最后渲染）
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see Builder::blendOrder()
         * @see Builder::channel()
         * @see RenderableManager::setPriority()
         * @see RenderableManager::setBlendOrderAt()
         */
        Builder& priority(uint8_t priority) noexcept;

        /**
         * Set the channel this renderable is associated to. There can be 8 channels.
         * All renderables in a given channel are rendered together, regardless of anything else.
         * They are sorted as usual within a channel.
         * Channels work similarly to priorities, except that they enforce the strongest ordering.
         *
         * Channels 0 and 1 may not have render primitives using a material with `refractionType`
         * set to `screenspace`.
         *
         * @param channel clamped to the range [0..7], defaults to 2.
         *
         * @return Builder reference for chaining calls.
         *
         * @see Builder::blendOrder()
         * @see Builder::priority()
         * @see RenderableManager::setBlendOrderAt()
         */
        /**
         * 设置此可渲染对象关联的通道。可以有 8 个通道。
         * 给定通道中的所有可渲染对象一起渲染，无论其他任何因素。
         * 它们在通道内按常规排序。
         * 通道的工作方式类似于优先级，但它们强制执行最强的排序。
         *
         * 通道 0 和 1 可能没有使用 `refractionType` 设置为 `screenspace` 的材质的渲染图元。
         *
         * @param channel 限制在范围 [0..7] 内，默认为 2
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see Builder::blendOrder()
         * @see Builder::priority()
         * @see RenderableManager::setBlendOrderAt()
         */
        Builder& channel(uint8_t channel) noexcept;

        /**
         * Controls frustum culling, true by default.
         *
         * \note Do not confuse frustum culling with backface culling. The latter is controlled via
         * the material.
         */
        /**
         * 控制视锥剔除，默认为 true。
         *
         * \note 不要将视锥剔除与背面剔除混淆。后者通过材质控制。
         */
        Builder& culling(bool enable) noexcept;

        /**
         * Enables or disables a light channel. Light channel 0 is enabled by default.
         *
         * @param channel Light channel to enable or disable, between 0 and 7.
         * @param enable Whether to enable or disable the light channel.
         */
        /**
         * 启用或禁用光源通道。默认情况下启用光源通道 0。
         *
         * @param channel 要启用或禁用的光源通道，在 0 和 7 之间
         * @param enable 是否启用或禁用光源通道
         */
        Builder& lightChannel(unsigned int channel, bool enable = true) noexcept;

        /**
         * Controls if this renderable casts shadows, false by default.
         *
         * If the View's shadow type is set to ShadowType::VSM, castShadows should only be disabled
         * if either is true:
         *   - receiveShadows is also disabled
         *   - the object is guaranteed to not cast shadows on itself or other objects (for example,
         *     a ground plane)
         */
        /**
         * 控制此可渲染对象是否投射阴影，默认为 false。
         *
         * 如果 View 的阴影类型设置为 ShadowType::VSM，只有在以下任一条件为真时才应禁用 castShadows：
         *   - receiveShadows 也被禁用
         *   - 保证对象不会在自己或其他对象上投射阴影（例如，地面平面）
         */
        Builder& castShadows(bool enable) noexcept;

        /**
         * Controls if this renderable receives shadows, true by default.
         */
        /**
         * 控制此可渲染对象是否接收阴影，默认为 true。
         */
        Builder& receiveShadows(bool enable) noexcept;

        /**
         * Controls if this renderable uses screen-space contact shadows. This is more
         * expensive but can improve the quality of shadows, especially in large scenes.
         * (off by default).
         */
        /**
         * 控制此可渲染对象是否使用屏幕空间接触阴影。这更昂贵，
         * 但可以提高阴影质量，尤其是在大场景中。
         * （默认关闭）。
         */
        Builder& screenSpaceContactShadows(bool enable) noexcept;

        /**
         * Allows bones to be swapped out and shared using SkinningBuffer.
         *
         * If skinning buffer mode is enabled, clients must call setSkinningBuffer() rather than
         * setBones(). This allows sharing of data between renderables.
         *
         * @param enabled If true, enables buffer object mode.  False by default.
         */
        /**
         * 允许使用 SkinningBuffer 交换和共享骨骼。
         *
         * 如果启用了蒙皮缓冲区模式，客户端必须调用 setSkinningBuffer() 而不是
         * setBones()。这允许在可渲染对象之间共享数据。
         *
         * @param enabled 如果为 true，则启用缓冲区对象模式。默认为 false。
         */
        Builder& enableSkinningBuffers(bool enabled = true) noexcept;

        /**
         * Controls if this renderable is affected by the large-scale fog.
         * @param enabled If true, enables large-scale fog on this object. Disables it otherwise.
         *                True by default.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 控制此可渲染对象是否受大范围雾影响。
         * @param enabled 如果为 true，则在此对象上启用大范围雾。否则禁用它。
         *                默认为 true。
         * @return 用于链接调用的 Builder 引用
         */
        Builder& fog(bool enabled = true) noexcept;

        /**
         * Enables GPU vertex skinning for up to 255 bones, 0 by default.
         *
         * Skinning Buffer mode must be enabled.
         *
         * Each vertex can be affected by up to 4 bones simultaneously. The attached
         * VertexBuffer must provide data in the \c BONE_INDICES slot (uvec4) and the
         * \c BONE_WEIGHTS slot (float4).
         *
         * See also RenderableManager::setSkinningBuffer() or SkinningBuffer::setBones(),
         * which can be called on a per-frame basis to advance the animation.
         *
         * @param skinningBuffer nullptr to disable, otherwise the SkinningBuffer to use
         * @param count 0 to disable, otherwise the number of bone transforms (up to 255)
         * @param offset offset in the SkinningBuffer
         */
        /**
         * 启用最多 255 个骨骼的 GPU 顶点蒙皮，默认为 0。
         *
         * 必须启用蒙皮缓冲区模式。
         *
         * 每个顶点最多可同时受 4 个骨骼影响。附加的
         * VertexBuffer 必须在 \c BONE_INDICES 槽（uvec4）和
         * \c BONE_WEIGHTS 槽（float4）中提供数据。
         *
         * 另请参见 RenderableManager::setSkinningBuffer() 或 SkinningBuffer::setBones()，
         * 可以每帧调用它们来推进动画。
         *
         * @param skinningBuffer nullptr 表示禁用，否则为要使用的 SkinningBuffer
         * @param count 0 表示禁用，否则为骨骼变换数量（最多 255）
         * @param offset SkinningBuffer 中的偏移量
         */
        Builder& skinning(SkinningBuffer* UTILS_NONNULL skinningBuffer,
                size_t count, size_t offset) noexcept;


        /**
         * Enables GPU vertex skinning for up to 255 bones, 0 by default.
         *
         * Skinning Buffer mode must be disabled.
         *
         * Each vertex can be affected by up to 4 bones simultaneously. The attached
         * VertexBuffer must provide data in the \c BONE_INDICES slot (uvec4) and the
         * \c BONE_WEIGHTS slot (float4).
         *
         * See also RenderableManager::setBones(), which can be called on a per-frame basis
         * to advance the animation.
         *
         * @param boneCount 0 to disable, otherwise the number of bone transforms (up to 255)
         * @param transforms the initial set of transforms (one for each bone)
         */
        /**
         * 启用最多 255 个骨骼的 GPU 顶点蒙皮，默认为 0。
         *
         * 必须禁用蒙皮缓冲区模式。
         *
         * 每个顶点最多可同时受 4 个骨骼影响。附加的
         * VertexBuffer 必须在 \c BONE_INDICES 槽（uvec4）和
         * \c BONE_WEIGHTS 槽（float4）中提供数据。
         *
         * 另请参见 RenderableManager::setBones()，可以每帧调用它来推进动画。
         *
         * @param boneCount 0 表示禁用，否则为骨骼变换数量（最多 255）
         * @param transforms 初始变换集合（每个骨骼一个）
         */
        Builder& skinning(size_t boneCount, math::mat4f const* UTILS_NONNULL transforms) noexcept;
        Builder& skinning(size_t boneCount, Bone const* UTILS_NONNULL bones) noexcept; //!< \overload
        /**
         * 重载版本：使用 Bone 结构体数组
         */
        Builder& skinning(size_t boneCount) noexcept; //!< \overload
        /**
         * 重载版本：仅指定骨骼数量，稍后通过 setBones() 设置变换
         */

        /**
         * Define bone indices and weights "pairs" for vertex skinning as a float2.
         * The unsigned int(pair.x) defines index of the bone and pair.y is the bone weight.
         * The pairs substitute \c BONE_INDICES and the \c BONE_WEIGHTS defined in the VertexBuffer.
         * Both ways of indices and weights definition must not be combined in one primitive.
         * Number of pairs per vertex bonesPerVertex is not limited to 4 bones.
         * Vertex buffer used for \c primitiveIndex must be set for advance skinning.
         * All bone weights of one vertex should sum to one. Otherwise they will be normalized.
         * Data must be rectangular and number of bone pairs must be same for all vertices of this
         * primitive.
         * The data is arranged sequentially, all bone pairs for the first vertex, then for the
         * second vertex, and so on.
         *
         * @param primitiveIndex zero-based index of the primitive, must be less than the primitive
         *                       count passed to Builder constructor
         * @param indicesAndWeights pairs of bone index and bone weight for all vertices
         *                          sequentially
         * @param count number of all pairs, must be a multiple of vertexCount of the primitive
         *                          count = vertexCount * bonesPerVertex
         * @param bonesPerVertex number of bone pairs, same for all vertices of the primitive
         *
         * @return Builder reference for chaining calls.
         *
         * @see VertexBuffer:Builder:advancedSkinning
         */
        /**
         * 将顶点蒙皮的骨骼索引和权重"对"定义为 float2。
         * unsigned int(pair.x) 定义骨骼索引，pair.y 是骨骼权重。
         * 这些对替代 VertexBuffer 中定义的 \c BONE_INDICES 和 \c BONE_WEIGHTS。
         * 索引和权重的两种定义方式不得在一个图元中组合使用。
         * 每个顶点的对数量 bonesPerVertex 不受限于 4 个骨骼。
         * 用于 \c primitiveIndex 的顶点缓冲区必须设置为高级蒙皮。
         * 一个顶点的所有骨骼权重应总和为一。否则它们将被归一化。
         * 数据必须是矩形的，并且此图元所有顶点的骨骼对数量必须相同。
         * 数据按顺序排列：第一个顶点的所有骨骼对，然后是第二个顶点，依此类推。
         *
         * @param primitiveIndex 图元的从零开始的索引，必须小于传递给 Builder 构造函数的图元 count
         * @param indicesAndWeights 所有顶点的骨骼索引和权重对，按顺序
         * @param count 所有对的数量，必须是图元 vertexCount 的倍数
         *                          count = vertexCount * bonesPerVertex
         * @param bonesPerVertex 骨骼对数量，图元所有顶点相同
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see VertexBuffer:Builder:advancedSkinning
         */
        Builder& boneIndicesAndWeights(size_t primitiveIndex,
                math::float2 const* UTILS_NONNULL indicesAndWeights,
                size_t count, size_t bonesPerVertex) noexcept;

        /**
         * Define bone indices and weights "pairs" for vertex skinning as a float2.
         * The unsigned int(pair.x) defines index of the bone and pair.y is the bone weight.
         * The pairs substitute \c BONE_INDICES and the \c BONE_WEIGHTS defined in the VertexBuffer.
         * Both ways of indices and weights definition must not be combined in one primitive.
         * Number of pairs is not limited to 4 bones per vertex.
         * Vertex buffer used for \c primitiveIndex must be set for advance skinning.
         * All bone weights of one vertex should sum to one. Otherwise they will be normalized.
         * Data doesn't have to be rectangular and number of pairs per vertices of primitive can be
         * variable.
         * The vector of the vertices contains the vectors of the pairs
         *
         * @param primitiveIndex zero-based index of the primitive, must be less than the primitive
         *                       count passed to Builder constructor
         * @param indicesAndWeightsVector pairs of bone index and bone weight for all vertices of
         *                                 the primitive sequentially
         *
         * @return Builder reference for chaining calls.
         *
         * @see VertexBuffer:Builder:advancedSkinning
         */
        /**
         * 将顶点蒙皮的骨骼索引和权重"对"定义为 float2（变长版本）。
         * unsigned int(pair.x) 定义骨骼索引，pair.y 是骨骼权重。
         * 这些对替代 VertexBuffer 中定义的 \c BONE_INDICES 和 \c BONE_WEIGHTS。
         * 索引和权重的两种定义方式不得在一个图元中组合使用。
         * 每顶点的对数量不受限于 4 个骨骼。
         * 用于 \c primitiveIndex 的顶点缓冲区必须设置为高级蒙皮。
         * 一个顶点的所有骨骼权重应总和为一。否则它们将被归一化。
         * 数据不必是矩形的，图元每个顶点的对数量可以是可变的。
         * 顶点的向量包含对的向量
         *
         * @param primitiveIndex 图元的从零开始的索引，必须小于传递给 Builder 构造函数的图元 count
         * @param indicesAndWeightsVector 图元所有顶点的骨骼索引和权重对，按顺序
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see VertexBuffer:Builder:advancedSkinning
         */
        Builder& boneIndicesAndWeights(size_t primitiveIndex,
                utils::FixedCapacityVector<
                    utils::FixedCapacityVector<math::float2>> indicesAndWeightsVector) noexcept;

        /**
         * Controls if the renderable has legacy vertex morphing targets, zero by default. This is
         * required to enable GPU morphing.
         *
         * For legacy morphing, the attached VertexBuffer must provide data in the
         * appropriate VertexAttribute slots (\c MORPH_POSITION_0 etc). Legacy morphing only
         * supports up to 4 morph targets and will be deprecated in the future. Legacy morphing must
         * be enabled on the material definition: either via the legacyMorphing material attribute
         * or by calling filamat::MaterialBuilder::useLegacyMorphing().
         *
         * See also RenderableManager::setMorphWeights(), which can be called on a per-frame basis
         * to advance the animation.
         */
        /**
         * 控制可渲染对象是否具有传统顶点变形目标，默认为零。这是
         * 启用 GPU 变形所必需的。
         *
         * 对于传统变形，附加的 VertexBuffer 必须在
         * 适当的 VertexAttribute 槽（\c MORPH_POSITION_0 等）中提供数据。传统变形仅
         * 支持最多 4 个变形目标，将来会被弃用。传统变形必须在
         * 材质定义上启用：通过 legacyMorphing 材质属性
         * 或调用 filamat::MaterialBuilder::useLegacyMorphing()。
         *
         * 另请参见 RenderableManager::setMorphWeights()，可以每帧调用它来推进动画。
         */
        Builder& morphing(size_t targetCount) noexcept;

        /**
         * Controls if the renderable has vertex morphing targets, zero by default. This is
         * required to enable GPU morphing.
         *
         * Filament supports two morphing modes: standard (default) and legacy.
         *
         * For standard morphing, A MorphTargetBuffer must be provided.
         * Standard morphing supports up to \c CONFIG_MAX_MORPH_TARGET_COUNT morph targets.
         *
         * See also RenderableManager::setMorphWeights(), which can be called on a per-frame basis
         * to advance the animation.
         */
        /**
         * 控制可渲染对象是否具有顶点变形目标，默认为零。这是
         * 启用 GPU 变形所必需的。
         *
         * Filament 支持两种变形模式：标准（默认）和传统。
         *
         * 对于标准变形，必须提供 MorphTargetBuffer。
         * 标准变形支持最多 \c CONFIG_MAX_MORPH_TARGET_COUNT 个变形目标。
         *
         * 另请参见 RenderableManager::setMorphWeights()，可以每帧调用它来推进动画。
         */
        Builder& morphing(MorphTargetBuffer* UTILS_NONNULL morphTargetBuffer) noexcept;

        /**
         * Specifies the the range of the MorphTargetBuffer to use with this primitive.
         *
         * @param level the level of detail (lod), only 0 can be specified
         * @param primitiveIndex zero-based index of the primitive, must be less than the count passed to Builder constructor
         * @param offset specifies where in the morph target buffer to start reading (expressed as a number of vertices)
         */
        /**
         * 指定要与此图元一起使用的 MorphTargetBuffer 范围。
         *
         * @param level 细节级别（lod），只能指定 0
         * @param primitiveIndex 图元的从零开始的索引，必须小于传递给 Builder 构造函数的 count
         * @param offset 指定在变形目标缓冲区中开始读取的位置（以顶点数表示）
         */
        Builder& morphing(uint8_t level,
                size_t primitiveIndex, size_t offset) noexcept;


        /**
         * Sets the drawing order for blended primitives. The drawing order is either global or
         * local (default) to this Renderable. In either case, the Renderable priority takes
         * precedence.
         *
         * @param primitiveIndex the primitive of interest
         * @param order draw order number (0 by default). Only the lowest 15 bits are used.
         *
         * @return Builder reference for chaining calls.
         *
         * @see globalBlendOrderEnabled
         */
        /**
         * 设置混合图元的绘制顺序。绘制顺序可以是全局的或
         * 相对于此可渲染对象是局部的（默认）。无论哪种情况，可渲染对象的优先级都优先。
         *
         * @param primitiveIndex 感兴趣的图元
         * @param order 绘制顺序号（默认为 0）。仅使用最低 15 位。
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see globalBlendOrderEnabled
         */
        Builder& blendOrder(size_t primitiveIndex, uint16_t order) noexcept;

        /**
         * Sets whether the blend order is global or local to this Renderable (by default).
         *
         * @param primitiveIndex the primitive of interest
         * @param enabled true for global, false for local blend ordering.
         *
         * @return Builder reference for chaining calls.
         *
         * @see blendOrder
         */
        /**
         * 设置混合顺序是全局的还是相对于此可渲染对象是局部的（默认）。
         *
         * @param primitiveIndex 感兴趣的图元
         * @param enabled true 表示全局，false 表示局部混合排序
         *
         * @return 用于链接调用的 Builder 引用
         *
         * @see blendOrder
         */
        Builder& globalBlendOrderEnabled(size_t primitiveIndex, bool enabled) noexcept;

        /**
         * Specifies the number of draw instances of this renderable. The default is 1 instance and
         * the maximum number of instances allowed is 32767. 0 is invalid.
         *
         * All instances are culled using the same bounding box, so care must be taken to make
         * sure all instances render inside the specified bounding box.
         *
         * The material must set its `instanced` parameter to `true` in order to use
         * getInstanceIndex() in the vertex or fragment shader to get the instance index and
         * possibly adjust the position or transform.
         *
         * @param instanceCount the number of instances silently clamped between 1 and 32767.
         */
        /**
         * 指定此可渲染对象的绘制实例数量。默认为 1 个实例，
         * 允许的最大实例数为 32767。0 无效。
         *
         * 所有实例使用相同的包围盒进行剔除，因此必须注意确保
         * 所有实例都在指定的包围盒内渲染。
         *
         * 材质必须将其 `instanced` 参数设置为 `true` 才能使用
         * 顶点或片段着色器中的 getInstanceIndex() 来获取实例索引并
         * 可能调整位置或变换。
         *
         * @param instanceCount 实例数量，静默限制在 1 和 32767 之间
         */
        Builder& instances(size_t instanceCount) noexcept;

        /**
         * Specifies the number of draw instances of this renderable and an \c InstanceBuffer
         * containing their local transforms. The default is 1 instance and the maximum number of
         * instances allowed when supplying transforms is given by
         * \c Engine::getMaxAutomaticInstances (64 on most platforms). 0 is invalid. The
         * \c InstanceBuffer must not be destroyed before this renderable.
         *
         * All instances are culled using the same bounding box, so care must be taken to make
         * sure all instances render inside the specified bounding box.
         *
         * The material must set its `instanced` parameter to `true` in order to use
         * \c getInstanceIndex() in the vertex or fragment shader to get the instance index.
         *
         * Only the \c VERTEX_DOMAIN_OBJECT vertex domain is supported.
         *
         * The local transforms of each instance can be updated with
         * \c InstanceBuffer::setLocalTransforms.
         *
         * \see InstanceBuffer
         * \see instances(size_t, * math::mat4f const*)
         * @param instanceCount the number of instances, silently clamped between 1 and
         *                      the result of Engine::getMaxAutomaticInstances().
         * @param instanceBuffer an InstanceBuffer containing at least instanceCount transforms
         */
        /**
         * 指定此可渲染对象的绘制实例数量以及包含其局部变换的 \c InstanceBuffer。
         * 默认为 1 个实例，提供变换时允许的最大实例数由
         * \c Engine::getMaxAutomaticInstances（在大多数平台上为 64）给出。0 无效。
         * \c InstanceBuffer 在此可渲染对象之前不得销毁。
         *
         * 所有实例使用相同的包围盒进行剔除，因此必须注意确保
         * 所有实例都在指定的包围盒内渲染。
         *
         * 材质必须将其 `instanced` 参数设置为 `true` 才能使用
         * 顶点或片段着色器中的 \c getInstanceIndex() 来获取实例索引。
         *
         * 仅支持 \c VERTEX_DOMAIN_OBJECT 顶点域。
         *
         * 每个实例的局部变换可以使用
         * \c InstanceBuffer::setLocalTransforms 更新。
         *
         * \see InstanceBuffer
         * \see instances(size_t)
         * @param instanceCount 实例数量，静默限制在 1 和
         *                      Engine::getMaxAutomaticInstances() 的结果之间
         * @param instanceBuffer 包含至少 instanceCount 个变换的 InstanceBuffer
         */
        Builder& instances(size_t instanceCount,
                InstanceBuffer* UTILS_NONNULL instanceBuffer) noexcept;

        /**
         * Adds the Renderable component to an entity.
         *
         * @param engine Reference to the filament::Engine to associate this Renderable with.
         * @param entity Entity to add the Renderable component to.
         * @return Success if the component was created successfully, Error otherwise.
         *
         * If exceptions are disabled and an error occurs, this function is a no-op.
         *        Success can be checked by looking at the return value.
         *
         * If this component already exists on the given entity and the construction is successful,
         * it is first destroyed as if destroy(utils::Entity e) was called. In case of error,
         * the existing component is unmodified.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 将可渲染组件添加到实体。
         *
         * @param engine 要与此可渲染对象关联的 filament::Engine 的引用
         * @param entity 要添加可渲染组件的实体
         * @return 如果组件创建成功则返回 Success，否则返回 Error
         *
         * 如果禁用了异常并且发生错误，此函数是无操作。
         *        可以通过查看返回值来检查成功。
         *
         * 如果给定实体上已存在此组件且构造成功，
         * 则首先销毁它，就像调用了 destroy(utils::Entity e) 一样。如果出错，
         * 现有组件不会被修改。
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         */
        Result build(Engine& engine, utils::Entity entity);

    private:
        friend class FEngine;
        friend class FRenderPrimitive;
        friend class FRenderableManager;
    };

    /**
     * Destroys the renderable component in the given entity.
     */
    /**
     * 销毁给定实体中的可渲染组件。
     */
    void destroy(utils::Entity e) noexcept;

    /**
     * Changes the bounding box used for frustum culling.
     * The renderable must not have staticGeometry enabled.
     *
     * \see Builder::boundingBox()
     * \see RenderableManager::getAxisAlignedBoundingBox()
     */
    /**
     * 更改用于视锥剔除的包围盒。
     * 可渲染对象不得启用 staticGeometry。
     *
     * \see Builder::boundingBox()
     * \see RenderableManager::getAxisAlignedBoundingBox()
     */
    void setAxisAlignedBoundingBox(Instance instance, const Box& aabb);

    /**
     * Changes the visibility bits.
     *
     * \see Builder::layerMask()
     * \see View::setVisibleLayers().
     * \see RenderableManager::getLayerMask()
     */
    /**
     * 更改可见性位。
     *
     * \see Builder::layerMask()
     * \see View::setVisibleLayers().
     * \see RenderableManager::getLayerMask()
     */
    void setLayerMask(Instance instance, uint8_t select, uint8_t values) noexcept;

    /**
     * Changes the coarse-level draw ordering.
     *
     * \see Builder::priority().
     */
    /**
     * 更改粗粒度绘制顺序。
     *
     * \see Builder::priority().
     */
    void setPriority(Instance instance, uint8_t priority) noexcept;

    /**
     * Changes the channel a renderable is associated to.
     *
     * \see Builder::channel().
     */
    /**
     * 更改可渲染对象关联的通道。
     *
     * \see Builder::channel().
     */
    void setChannel(Instance instance, uint8_t channel) noexcept;

    /**
     * Changes whether or not frustum culling is on.
     *
     * \see Builder::culling()
     */
    /**
     * 更改是否启用视锥剔除。
     *
     * \see Builder::culling()
     */
    void setCulling(Instance instance, bool enable) noexcept;

    /**
     * Changes whether or not the large-scale fog is applied to this renderable
     * @see Builder::fog()
     */
    /**
     * 更改是否将大范围雾应用于此可渲染对象
     * @see Builder::fog()
     */
    void setFogEnabled(Instance instance, bool enable) noexcept;

    /**
     * Returns whether large-scale fog is enabled for this renderable.
     * @return True if fog is enabled for this renderable.
     * @see Builder::fog()
     */
    /**
     * 返回是否为此可渲染对象启用大范围雾。
     * @return 如果为此可渲染对象启用雾则返回 true
     * @see Builder::fog()
     */
    bool getFogEnabled(Instance instance) const noexcept;

    /**
     * Enables or disables a light channel.
     * Light channel 0 is enabled by default.
     *
     * \see Builder::lightChannel()
     */
    /**
     * 启用或禁用光源通道。
     * 默认情况下启用光源通道 0。
     *
     * \see Builder::lightChannel()
     */
    void setLightChannel(Instance instance, unsigned int channel, bool enable) noexcept;

    /**
     * Returns whether a light channel is enabled on a specified renderable.
     * @param instance Instance of the component obtained from getInstance().
     * @param channel  Light channel to query
     * @return         true if the light channel is enabled, false otherwise
     */
    /**
     * 返回指定可渲染对象上是否启用了光源通道。
     * @param instance 从 getInstance() 获取的组件实例
     * @param channel  要查询的光源通道
     * @return         如果启用了光源通道则返回 true，否则返回 false
     */
    bool getLightChannel(Instance instance, unsigned int channel) const noexcept;

    /**
     * Changes whether or not the renderable casts shadows.
     *
     * \see Builder::castShadows()
     */
    /**
     * 更改可渲染对象是否投射阴影。
     *
     * \see Builder::castShadows()
     */
    void setCastShadows(Instance instance, bool enable) noexcept;

    /**
     * Changes whether or not the renderable can receive shadows.
     *
     * \see Builder::receiveShadows()
     */
    /**
     * 更改可渲染对象是否可以接收阴影。
     *
     * \see Builder::receiveShadows()
     */
    void setReceiveShadows(Instance instance, bool enable) noexcept;

    /**
     * Changes whether or not the renderable can use screen-space contact shadows.
     *
     * \see Builder::screenSpaceContactShadows()
     */
    /**
     * 更改可渲染对象是否可以使用屏幕空间接触阴影。
     *
     * \see Builder::screenSpaceContactShadows()
     */
    void setScreenSpaceContactShadows(Instance instance, bool enable) noexcept;

    /**
     * Checks if the renderable can cast shadows.
     *
     * \see Builder::castShadows().
     */
    /**
     * 检查可渲染对象是否可以投射阴影。
     *
     * \see Builder::castShadows().
     */
    bool isShadowCaster(Instance instance) const noexcept;

    /**
     * Checks if the renderable can receive shadows.
     *
     * \see Builder::receiveShadows().
     */
    /**
     * 检查可渲染对象是否可以接收阴影。
     *
     * \see Builder::receiveShadows().
     */
    bool isShadowReceiver(Instance instance) const noexcept;

    /**
     * Updates the bone transforms in the range [offset, offset + boneCount).
     * The bones must be pre-allocated using Builder::skinning().
     */
    /**
     * 更新范围 [offset, offset + boneCount) 中的骨骼变换。
     * 必须使用 Builder::skinning() 预分配骨骼。
     */
    void setBones(Instance instance, Bone const* UTILS_NONNULL transforms,
            size_t boneCount = 1, size_t offset = 0);

    void setBones(Instance instance, math::mat4f const* UTILS_NONNULL transforms,
            size_t boneCount = 1, size_t offset = 0); //!< \overload
    /**
     * 重载版本：使用 4x4 矩阵数组
     */

    /**
     * Associates a region of a SkinningBuffer to a renderable instance
     *
     * Note: due to hardware limitations offset + 256 must be smaller or equal to
     *       skinningBuffer->getBoneCount()
     *
     * @param instance          Instance of the component obtained from getInstance().
     * @param skinningBuffer    skinning buffer to associate to the instance
     * @param count             Size of the region in bones, must be smaller or equal to 256.
     * @param offset            Start offset of the region in bones
     */
    /**
     * 将 SkinningBuffer 的区域关联到可渲染对象实例
     *
     * 注意：由于硬件限制，offset + 256 必须小于或等于
     *       skinningBuffer->getBoneCount()
     *
     * @param instance          从 getInstance() 获取的组件实例
     * @param skinningBuffer    要关联到实例的蒙皮缓冲区
     * @param count             区域大小（以骨骼为单位），必须小于或等于 256
     * @param offset            区域在骨骼中的起始偏移量
     */
    void setSkinningBuffer(Instance instance, SkinningBuffer* UTILS_NONNULL skinningBuffer,
            size_t count, size_t offset);

    /**
     * Updates the vertex morphing weights on a renderable, all zeroes by default.
     *
     * The renderable must be built with morphing enabled, see Builder::morphing(). In legacy
     * morphing mode, only the first 4 weights are considered.
     *
     * @param instance Instance of the component obtained from getInstance().
     * @param weights Pointer to morph target weights to be update.
     * @param count Number of morph target weights.
     * @param offset Index of the first morph target weight to set at instance.
     */
    /**
     * 更新可渲染对象上的顶点变形权重，默认为全零。
     *
     * 可渲染对象必须启用变形构建，请参见 Builder::morphing()。在传统
     * 变形模式下，仅考虑前 4 个权重。
     *
     * @param instance 从 getInstance() 获取的组件实例
     * @param weights  指向要更新的变形目标权重的指针
     * @param count    变形目标权重数量
     * @param offset   要在实例处设置的第一个变形目标权重的索引
     */
    void setMorphWeights(Instance instance,
            float const* UTILS_NONNULL weights, size_t count, size_t offset = 0);

    /**
     * Associates a MorphTargetBuffer to the given primitive.
     */
    /**
     * 将 MorphTargetBuffer 关联到给定图元。
     */
    void setMorphTargetBufferOffsetAt(Instance instance, uint8_t level, size_t primitiveIndex,
            size_t offset);

    /**
     * Get a MorphTargetBuffer to the given renderable or null if it doesn't exist.
     */
    /**
     * 获取给定可渲染对象的 MorphTargetBuffer，如果不存在则返回 null。
     */
    MorphTargetBuffer* UTILS_NULLABLE getMorphTargetBuffer(Instance instance) const noexcept;

    /**
     * Gets the number of morphing in the given entity.
     */
    /**
     * 获取给定实体中的变形数量。
     */
    size_t getMorphTargetCount(Instance instance) const noexcept;

    /**
     * Gets the bounding box used for frustum culling.
     *
     * \see Builder::boundingBox()
     * \see RenderableManager::setAxisAlignedBoundingBox()
     */
    /**
     * 获取用于视锥剔除的包围盒。
     *
     * \see Builder::boundingBox()
     * \see RenderableManager::setAxisAlignedBoundingBox()
     */
    const Box& getAxisAlignedBoundingBox(Instance instance) const noexcept;

    /**
     * Get the visibility bits.
     *
     * \see Builder::layerMask()
     * \see View::setVisibleLayers().
     * \see RenderableManager::getLayerMask()
     */
    /**
     * 获取可见性位。
     *
     * \see Builder::layerMask()
     * \see View::setVisibleLayers().
     * \see RenderableManager::getLayerMask()
     */
    uint8_t getLayerMask(Instance instance) const noexcept;

    /**
     * Gets the immutable number of primitives in the given renderable.
     */
    /**
     * 获取给定可渲染对象中不可变的图元数量。
     */
    size_t getPrimitiveCount(Instance instance) const noexcept;

    /**
     * Returns the number of instances for this renderable.
     * @param instance Instance of the component obtained from getInstance().
     * @return The number of instances.
     */
    /**
     * 返回此可渲染对象的实例数量。
     * @param instance 从 getInstance() 获取的组件实例
     * @return 实例数量
     */
    size_t getInstanceCount(Instance instance) const noexcept;

    /**
     * Changes the material instance binding for the given primitive.
     *
     * The MaterialInstance's material must have a feature level equal or lower to the engine's
     * selected feature level.
     *
     * @exception utils::PreConditionPanic if the engine doesn't support the material's
     *                                     feature level.
     *
     * @see Builder::material()
     * @see Engine::setActiveFeatureLevel
     */
    /**
     * 更改给定图元的材质实例绑定。
     *
     * MaterialInstance 的材质必须具有等于或低于引擎所选功能级别的功能级别。
     *
     * @exception 如果引擎不支持材质的
     *                                     功能级别，则抛出 utils::PreConditionPanic。
     *
     * @see Builder::material()
     * @see Engine::setActiveFeatureLevel
     */
    void setMaterialInstanceAt(Instance instance,
            size_t primitiveIndex, MaterialInstance const* UTILS_NONNULL materialInstance);

    /**
     * Clear the MaterialInstance for the given primitive.
     * @param instance Renderable's instance
     * @param primitiveIndex Primitive index
     */
    /**
     * 清除给定图元的 MaterialInstance。
     * @param instance 可渲染对象的实例
     * @param primitiveIndex 图元索引
     */
    void clearMaterialInstanceAt(Instance instance, size_t primitiveIndex);

    /**
     * Retrieves the material instance that is bound to the given primitive.
     */
    /**
     * 检索绑定到给定图元的材质实例。
     */
    MaterialInstance* UTILS_NULLABLE getMaterialInstanceAt(
            Instance instance, size_t primitiveIndex) const noexcept;

    /**
     * Changes the geometry for the given primitive.
     *
     * \see Builder::geometry()
     */
    /**
     * 更改给定图元的几何体。
     *
     * \see Builder::geometry()
     */
    void setGeometryAt(Instance instance, size_t primitiveIndex, PrimitiveType type,
            VertexBuffer* UTILS_NONNULL vertices,
            IndexBuffer* UTILS_NONNULL indices,
            size_t offset, size_t count) noexcept;

    /**
     * Changes the drawing order for blended primitives. The drawing order is either global or
     * local (default) to this Renderable. In either case, the Renderable priority takes precedence.
     *
     * @param instance the renderable of interest
     * @param primitiveIndex the primitive of interest
     * @param order draw order number (0 by default). Only the lowest 15 bits are used.
     *
     * @see Builder::blendOrder(), setGlobalBlendOrderEnabledAt()
     */
    /**
     * 更改混合图元的绘制顺序。绘制顺序可以是全局的或
     * 相对于此可渲染对象是局部的（默认）。无论哪种情况，可渲染对象的优先级都优先。
     *
     * @param instance 感兴趣的可渲染对象
     * @param primitiveIndex 感兴趣的图元
     * @param order 绘制顺序号（默认为 0）。仅使用最低 15 位。
     *
     * @see Builder::blendOrder(), setGlobalBlendOrderEnabledAt()
     */
    void setBlendOrderAt(Instance instance, size_t primitiveIndex, uint16_t order) noexcept;

    /**
     * Changes whether the blend order is global or local to this Renderable (by default).
     *
     * @param instance the renderable of interest
     * @param primitiveIndex the primitive of interest
     * @param enabled true for global, false for local blend ordering.
     *
     * @see Builder::globalBlendOrderEnabled(), setBlendOrderAt()
     */
    /**
     * 更改混合顺序是全局的还是相对于此可渲染对象是局部的（默认）。
     *
     * @param instance 感兴趣的可渲染对象
     * @param primitiveIndex 感兴趣的图元
     * @param enabled true 表示全局，false 表示局部混合排序。
     *
     * @see Builder::globalBlendOrderEnabled(), setBlendOrderAt()
     */
    void setGlobalBlendOrderEnabledAt(Instance instance, size_t primitiveIndex, bool enabled) noexcept;

    /**
     * Retrieves the set of enabled attribute slots in the given primitive's VertexBuffer.
     */
    /**
     * 检索给定图元的 VertexBuffer 中启用的属性槽集合。
     */
    AttributeBitset getEnabledAttributesAt(Instance instance, size_t primitiveIndex) const noexcept;

    /*! \cond PRIVATE */
    template<typename T>
    struct is_supported_vector_type {
        using type = std::enable_if_t<
                std::is_same_v<math::float4, T> ||
                std::is_same_v<math::half4,  T> ||
                std::is_same_v<math::float3, T> ||
                std::is_same_v<math::half3,  T>
        >;
    };

    template<typename T>
    struct is_supported_index_type {
        using type = std::enable_if_t<
                std::is_same_v<uint16_t, T> ||
                std::is_same_v<uint32_t, T>
        >;
    };
    /*! \endcond */

    /**
     * Utility method that computes the axis-aligned bounding box from a set of vertices.
     *
     * - The index type must be \c uint16_t or \c uint32_t.
     * - The vertex type must be \c float4, \c half4, \c float3, or \c half3.
     * - For 4-component vertices, the w component is ignored (implicitly replaced with 1.0).
     */
    /**
     * 从一组顶点计算轴对齐包围盒的实用方法。
     *
     * - 索引类型必须是 \c uint16_t 或 \c uint32_t。
     * - 顶点类型必须是 \c float4、\c half4、\c float3 或 \c half3。
     * - 对于 4 分量顶点，w 分量被忽略（隐式替换为 1.0）。
     *
     * @param vertices 顶点数组
     * @param indices 索引数组
     * @param count 索引数量
     * @param stride 顶点之间的步长（字节），默认为 VECTOR 的大小
     * @return 计算得到的轴对齐包围盒
     */
    template<typename VECTOR, typename INDEX,
            typename = typename is_supported_vector_type<VECTOR>::type,
            typename = typename is_supported_index_type<INDEX>::type>
    static Box computeAABB(
            VECTOR const* UTILS_NONNULL vertices,
            INDEX const* UTILS_NONNULL indices, size_t count,
            size_t stride = sizeof(VECTOR)) noexcept;

protected:
    // prevent heap allocation
    ~RenderableManager() = default;
};

template<typename VECTOR, typename INDEX, typename, typename>
Box RenderableManager::computeAABB(
        VECTOR const* UTILS_NONNULL vertices,
        INDEX const* UTILS_NONNULL indices,
        size_t count, size_t stride) noexcept {
    math::float3 bmin(FLT_MAX);
    math::float3 bmax(-FLT_MAX);
    for (size_t i = 0; i < count; ++i) {
        VECTOR const* p = reinterpret_cast<VECTOR const*>(
                (char const*)vertices + indices[i] * stride);
        const math::float3 v(p->x, p->y, p->z);
        bmin = min(bmin, v);
        bmax = max(bmax, v);
    }
    return Box().set(bmin, bmax);
}

} // namespace filament

#endif // TNT_FILAMENT_RENDERABLEMANAGER_H
