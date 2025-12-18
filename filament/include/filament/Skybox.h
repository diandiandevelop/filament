/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SKYBOX_H
#define TNT_FILAMENT_SKYBOX_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>

#include <math/mathfwd.h>

#include <stdint.h>

namespace filament {

class FSkybox;

class Engine;
class Texture;

/**
 * Skybox
 *
 * When added to a Scene, the Skybox fills all untouched pixels.
 *
 * Creation and destruction
 * ========================
 *
 * A Skybox object is created using the Skybox::Builder and destroyed by calling
 * Engine::destroy(const Skybox*).
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *
 *  filament::IndirectLight* skybox = filament::Skybox::Builder()
 *              .environment(cubemap)
 *              .build(*engine);
 *
 *  engine->destroy(skybox);
 * ~~~~~~~~~~~
 *
 *
 * @note
 * Currently only Texture based sky boxes are supported.
 *
 * @see Scene, IndirectLight
 */
/**
 * Skybox（天空盒）
 *
 * 当添加到 Scene 时，Skybox 填充所有未触及的像素。
 *
 * 创建和销毁
 * ========================
 *
 * Skybox 对象使用 Skybox::Builder 创建，通过调用
 * Engine::destroy(const Skybox*) 销毁。
 *
 * @note
 * 目前仅支持基于 Texture 的天空盒。
 *
 * @see Scene, IndirectLight
 */
class UTILS_PUBLIC Skybox : public FilamentAPI {
    struct BuilderDetails;

public:
    //! Use Builder to construct an Skybox object instance
    /**
     * 使用 Builder 构造 Skybox 对象实例
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Set the environment map (i.e. the skybox content).
         *
         * The Skybox is rendered as though it were an infinitely large cube with the camera
         * inside it. This means that the cubemap which is mapped onto the cube's exterior
         * will appear mirrored. This follows the OpenGL conventions.
         *
         * The cmgen tool generates reflection maps by default which are therefore ideal to use
         * as skyboxes.
         *
         * @param cubemap This Texture must be a cube map.
         *
         * @return This Builder, for chaining calls.
         *
         * @see Texture
         */
        /**
         * 设置环境贴图（即天空盒内容）
         *
         * Skybox 被渲染为一个无限大的立方体，相机位于其内部。
         * 这意味着映射到立方体外部的立方体贴图将显示为镜像。
         * 这遵循 OpenGL 约定。
         *
         * cmgen 工具默认生成反射贴图，因此非常适合用作天空盒。
         *
         * @param cubemap 此 Texture 必须是立方体贴图
         *
         * @return 此 Builder，用于链接调用
         *
         * @see Texture
         */
        Builder& environment(Texture* UTILS_NONNULL cubemap) noexcept;

        /**
         * Indicates whether the sun should be rendered. The sun can only be
         * rendered if there is at least one light of type SUN in the scene.
         * The default value is false.
         *
         * @param show True if the sun should be rendered, false otherwise
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 指示是否应渲染太阳。只有当场景中至少有一个
         * SUN 类型的光源时才能渲染太阳。
         * 默认值为 false。
         *
         * @param show 如果要渲染太阳则为 true，否则为 false
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& showSun(bool show) noexcept;

        /**
         * Skybox intensity when no IndirectLight is set on the Scene.
         *
         * This call is ignored when an IndirectLight is set on the Scene, and the intensity
         * of the IndirectLight is used instead.
         *
         * @param envIntensity  Scale factor applied to the skybox texel values such that
         *                      the result is in lux, or lumen/m^2 (default = 30000)
         *
         * @return This Builder, for chaining calls.
         *
         * @see IndirectLight::Builder::intensity
         */
        /**
         * 当 Scene 上未设置 IndirectLight 时，天空盒的强度
         *
         * 当在 Scene 上设置 IndirectLight 时，此调用将被忽略，而是使用
         * IndirectLight 的强度。
         *
         * @param envIntensity  应用于天空盒纹素值的缩放因子，使得
         *                      结果以勒克斯或流明/平方米为单位（默认值 = 30000）
         *
         * @return 此 Builder，用于链接调用
         *
         * @see IndirectLight::Builder::intensity
         */
        Builder& intensity(float envIntensity) noexcept;

        /**
         * Sets the skybox to a constant color. Default is opaque black.
         *
         * Ignored if an environment is set.
         *
         * @param color the constant color
         *
         * @return This Builder, for chaining calls.
         */
        /**
         * 将天空盒设置为恒定颜色。默认为不透明黑色。
         *
         * 如果设置了环境贴图，则会被忽略。
         *
         * @param color 恒定颜色
         *
         * @return 此 Builder，用于链接调用
         */
        Builder& color(math::float4 color) noexcept;

        /**
         * Set the rendering priority of the Skybox. By default, it is set to the lowest
         * priority (7) such that the Skybox is always rendered after the opaque objects,
         * to reduce overdraw when depth culling is enabled.
         *
         * @param priority clamped to the range [0..7], defaults to 4; 7 is lowest priority
         *                 (rendered last).
         *
         * @return Builder reference for chaining calls.
         *
         * @see RenderableManager::Builder::priority()
         */
        Builder& priority(uint8_t priority) noexcept;

        /**
         * Creates the Skybox object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this Skybox with.
         *
         * @return pointer to the newly created object.
         */
        /**
         * 创建 Skybox 对象并返回指向它的指针
         *
         * @param engine 要与此 Skybox 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         */
        Skybox* UTILS_NONNULL build(Engine& engine);

    private:
        friend class FSkybox;
    };

    void setColor(math::float4 color) noexcept;
    /**
     * 设置天空盒的恒定颜色
     *
     * @param color 恒定颜色
     */

    /**
     * Sets bits in a visibility mask. By default, this is 0x1.
     *
     * This provides a simple mechanism for hiding or showing this Skybox in a Scene.
     *
     * @see View::setVisibleLayers().
     *
     * For example, to set bit 1 and reset bits 0 and 2 while leaving all other bits unaffected,
     * call: `setLayerMask(7, 2)`.
     *
     * @param select the set of bits to affect
     * @param values the replacement values for the affected bits
     */
    /**
     * 设置可见性掩码中的位。默认情况下，这是 0x1。
     *
     * 这提供了在 Scene 中隐藏或显示此 Skybox 的简单机制。
     *
     * @see View::setVisibleLayers()
     *
     * 例如，要设置位 1 并重置位 0 和 2，同时不影响所有其他位，
     * 调用：`setLayerMask(7, 2)`。
     *
     * @param select 要影响的位集合
     * @param values 受影响位的替换值
     */
    void setLayerMask(uint8_t select, uint8_t values) noexcept;

    /**
     * @return the visibility mask bits
     */
    /**
     * @return 可见性掩码位
     */
    uint8_t getLayerMask() const noexcept;

    /**
     * Returns the skybox's intensity in lux, or lumen/m^2.
     */
    /**
     * 返回天空盒的强度（以勒克斯或流明/平方米为单位）
     */
    float getIntensity() const noexcept;

    /**
     * @return the associated texture
     */
    /**
     * @return 关联的纹理
     */
    Texture const* UTILS_NONNULL getTexture() const noexcept;

protected:
    // prevent heap allocation
    ~Skybox() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_SKYBOX_H
