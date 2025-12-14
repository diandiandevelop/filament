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

#ifndef TNT_FILAMENT_MATERIALINSTANCE_H
#define TNT_FILAMENT_MATERIALINSTANCE_H

#include <filament/FilamentAPI.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/MaterialEnums.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>

#include <math/mathfwd.h>

#include <type_traits>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace filament {

class Material;
class Texture;
class TextureSampler;
class UniformBuffer;
class BufferInterfaceBlock;

class UTILS_PUBLIC MaterialInstance : public FilamentAPI {
    template<size_t N>
    using StringLiteralHelper = const char[N];

    struct StringLiteral {
        const char* UTILS_NONNULL data;
        size_t size;
        template<size_t N>
        StringLiteral(StringLiteralHelper<N> const& s) noexcept // NOLINT(google-explicit-constructor)
                : data(s), size(N - 1) {
        }
    };

public:
    using CullingMode = backend::CullingMode;
    /**
     * 剔除模式类型
     */
    // ReSharper disable once CppRedundantQualifier
    using TransparencyMode = filament::TransparencyMode;
    /**
     * 透明度模式类型
     */
    using DepthFunc = backend::SamplerCompareFunc;
    /**
     * 深度函数类型
     */
    using StencilCompareFunc = backend::SamplerCompareFunc;
    /**
     * 模板比较函数类型
     */
    using StencilOperation = backend::StencilOperation;
    /**
     * 模板操作类型
     */
    using StencilFace = backend::StencilFace;
    /**
     * 模板面类型
     */

    template<typename T>
    using is_supported_parameter_t = std::enable_if_t<
            std::is_same_v<float, T> ||
            std::is_same_v<int32_t, T> ||
            std::is_same_v<uint32_t, T> ||
            std::is_same_v<math::int2, T> ||
            std::is_same_v<math::int3, T> ||
            std::is_same_v<math::int4, T> ||
            std::is_same_v<math::uint2, T> ||
            std::is_same_v<math::uint3, T> ||
            std::is_same_v<math::uint4, T> ||
            std::is_same_v<math::float2, T> ||
            std::is_same_v<math::float3, T> ||
            std::is_same_v<math::float4, T> ||
            std::is_same_v<math::mat4f, T> ||
            // these types are slower as they need a layout conversion
            std::is_same_v<bool, T> ||
            std::is_same_v<math::bool2, T> ||
            std::is_same_v<math::bool3, T> ||
            std::is_same_v<math::bool4, T> ||
            std::is_same_v<math::mat3f, T>
    >;

    /**
     * Creates a new MaterialInstance using another MaterialInstance as a template for initialization.
     * The new MaterialInstance is an instance of the same Material of the template instance and
     * must be destroyed just like any other MaterialInstance.
     *
     * @param other A MaterialInstance to use as a template for initializing a new instance
     * @param name  A name for the new MaterialInstance or nullptr to use the template's name
     * @return      A new MaterialInstance
     */
    /**
     * 使用另一个 MaterialInstance 作为模板创建新的 MaterialInstance
     * 新的 MaterialInstance 是模板实例的同一 Material 的实例，
     * 必须像任何其他 MaterialInstance 一样被销毁。
     *
     * @param other 用作初始化新实例的模板的 MaterialInstance
     * @param name  新 MaterialInstance 的名称，或 nullptr 以使用模板的名称
     * @return      新的 MaterialInstance
     */
    static MaterialInstance* UTILS_NONNULL duplicate(MaterialInstance const* UTILS_NONNULL other,
            const char* UTILS_NULLABLE name = nullptr) noexcept;

    /**
     * @return the Material associated with this instance
     */
    /**
     * @return 与此实例关联的 Material
     */
    Material const* UTILS_NONNULL getMaterial() const noexcept;

    /**
     * @return the name associated with this instance
     */
    /**
     * @return 与此实例关联的名称
     */
    const char* UTILS_NONNULL getName() const noexcept;

    /**
     * Set a uniform by name
     *
     * @param name          Name of the parameter as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @param value         Value of the parameter to set.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     */
    /**
     * 按名称设置 uniform 参数
     *
     * @param name          Material 定义的参数名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @param value         要设置的参数值
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(const char* UTILS_NONNULL name, size_t nameLength, T const& value);

    /** inline helper to provide the name as a null-terminated string literal */
    /**
     * 内联辅助函数，以空终止字符串字面量提供名称
     */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(StringLiteral const name, T const& value) {
        setParameter<T>(name.data, name.size, value);
    }

    /** inline helper to provide the name as a null-terminated C string */
    /**
     * 内联辅助函数，以空终止 C 字符串提供名称
     */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(const char* UTILS_NONNULL name, T const& value) {
        setParameter<T>(name, strlen(name), value);
    }


    /**
     * Set a uniform array by name
     *
     * @param name          Name of the parameter array as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @param values        Array of values to set to the named parameter array.
     * @param count         Size of the array to set.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     * @see Material::hasParameter
     */
    /**
     * 按名称设置 uniform 数组
     *
     * @param name          Material 定义的参数数组名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @param values        要设置到命名参数数组的值数组
     * @param count         要设置的数组大小
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     * @see Material::hasParameter
     */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(const char* UTILS_NONNULL name, size_t nameLength,
            const T* UTILS_NONNULL values, size_t count);

    /** inline helper to provide the name as a null-terminated string literal */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(StringLiteral const name, const T* UTILS_NONNULL values, size_t const count) {
        setParameter<T>(name.data, name.size, values, count);
    }

    /** inline helper to provide the name as a null-terminated C string */
    template<typename T, typename = is_supported_parameter_t<T>>
    void setParameter(const char* UTILS_NONNULL name,
                      const T* UTILS_NONNULL values, size_t const count) {
        setParameter<T>(name, strlen(name), values, count);
    }


    /**
     * Set a texture as the named parameter
     *
     * Note: Depth textures can't be sampled with a linear filter unless the comparison mode is set
     *       to COMPARE_TO_TEXTURE.
     *
     * @param name          Name of the parameter as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @param texture       Non nullptr Texture object pointer.
     * @param sampler       Sampler parameters.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     */
    /**
     * 将纹理设置为命名参数
     *
     * 注意：深度纹理不能使用线性过滤器进行采样，除非比较模式设置为
     *       COMPARE_TO_TEXTURE。
     *
     * @param name          Material 定义的参数名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @param texture       非 nullptr 的 Texture 对象指针
     * @param sampler       采样器参数
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     */
    void setParameter(const char* UTILS_NONNULL name, size_t nameLength,
            Texture const* UTILS_NULLABLE texture, TextureSampler const& sampler);

    /** inline helper to provide the name as a null-terminated string literal */
    /**
     * 内联辅助函数，以空终止字符串字面量提供名称
     */
    void setParameter(StringLiteral const name,
                      Texture const* UTILS_NULLABLE texture, TextureSampler const& sampler) {
        setParameter(name.data, name.size, texture, sampler);
    }

    /** inline helper to provide the name as a null-terminated C string */
    /**
     * 内联辅助函数，以空终止 C 字符串提供名称
     */
    void setParameter(const char* UTILS_NONNULL name,
                      Texture const* UTILS_NULLABLE texture, TextureSampler const& sampler) {
        setParameter(name, strlen(name), texture, sampler);
    }


    /**
     * Set an RGB color as the named parameter.
     * A conversion might occur depending on the specified type
     *
     * @param name          Name of the parameter as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @param type          Whether the color value is encoded as Linear or sRGB.
     * @param color         Array of read, green, blue channels values.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     */
    /**
     * 将 RGB 颜色设置为命名参数
     * 根据指定的类型可能会发生转换
     *
     * @param name          Material 定义的参数名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @param type          颜色值是否编码为 Linear 或 sRGB
     * @param color         红色、绿色、蓝色通道值的数组
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     */
    void setParameter(const char* UTILS_NONNULL name, size_t nameLength,
            RgbType type, math::float3 color);

    /** inline helper to provide the name as a null-terminated string literal */
    void setParameter(StringLiteral const name, RgbType const type, math::float3 const color) {
        setParameter(name.data, name.size, type, color);
    }

    /** inline helper to provide the name as a null-terminated C string */
    void setParameter(const char* UTILS_NONNULL name, RgbType const type, math::float3 const color) {
        setParameter(name, strlen(name), type, color);
    }


    /**
     * Set an RGBA color as the named parameter.
     * A conversion might occur depending on the specified type
     *
     * @param name          Name of the parameter as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @param type          Whether the color value is encoded as Linear or sRGB/A.
     * @param color         Array of read, green, blue and alpha channels values.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     */
    /**
     * 将 RGBA 颜色设置为命名参数
     * 根据指定的类型可能会发生转换
     *
     * @param name          Material 定义的参数名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @param type          颜色值是否编码为 Linear 或 sRGB/A
     * @param color         红色、绿色、蓝色和 alpha 通道值的数组
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     */
    void setParameter(const char* UTILS_NONNULL name, size_t nameLength,
            RgbaType type, math::float4 color);

    /** inline helper to provide the name as a null-terminated string literal */
    void setParameter(StringLiteral const name, RgbaType const type, math::float4 const color) {
        setParameter(name.data, name.size, type, color);
    }

    /** inline helper to provide the name as a null-terminated C string */
    void setParameter(const char* UTILS_NONNULL name, RgbaType const type, math::float4 const color) {
        setParameter(name, strlen(name), type, color);
    }

    /**
     * Gets the value of a parameter by name.
     * 
     * Note: Only supports non-texture parameters such as numeric and math types.
     * 
     * @param name          Name of the parameter as defined by Material. Cannot be nullptr.
     * @param nameLength    Length in `char` of the name parameter.
     * @throws utils::PreConditionPanic if name doesn't exist or no-op if exceptions are disabled.
     * 
     * @see Material::hasParameter
     */
    /**
     * 按名称获取参数值
     * 
     * 注意：仅支持非纹理参数，例如数值和数学类型。
     * 
     * @param name          Material 定义的参数名称。不能为 nullptr
     * @param nameLength    名称参数的长度（以 `char` 为单位）
     * @throws 如果名称不存在，则抛出 utils::PreConditionPanic，或如果禁用了异常，则为无操作
     * 
     * @see Material::hasParameter
     */
    template<typename T>
    T getParameter(const char* UTILS_NONNULL name, size_t nameLength) const;

    /** inline helper to provide the name as a null-terminated C string */
    template<typename T, typename = is_supported_parameter_t<T>>
    T getParameter(StringLiteral const name) const {
        return getParameter<T>(name.data, name.size);
    }

    /** inline helper to provide the name as a null-terminated C string */
    template<typename T, typename = is_supported_parameter_t<T>>
    T getParameter(const char* UTILS_NONNULL name) const {
        return getParameter<T>(name, strlen(name));
    }

    /**
     * Set-up a custom scissor rectangle; by default it is disabled.
     *
     * The scissor rectangle gets clipped by the View's viewport, in other words, the scissor
     * cannot affect fragments outside of the View's Viewport.
     *
     * Currently the scissor is not compatible with dynamic resolution and should always be
     * disabled when dynamic resolution is used.
     *
     * @param left      left coordinate of the scissor box relative to the viewport
     * @param bottom    bottom coordinate of the scissor box relative to the viewport
     * @param width     width of the scissor box
     * @param height    height of the scissor box
     *
     * @see unsetScissor
     * @see View::setViewport
     * @see View::setDynamicResolutionOptions
     */
    /**
     * 设置自定义裁剪矩形；默认情况下禁用
     *
     * 裁剪矩形会被 View 的视口裁剪，换句话说，裁剪
     * 不能影响 View 视口外部的片段。
     *
     * 目前裁剪与动态分辨率不兼容，当使用动态分辨率时应始终
     * 禁用。
     *
     * @param left      裁剪框相对于视口的左坐标
     * @param bottom    裁剪框相对于视口的底坐标
     * @param width     裁剪框的宽度
     * @param height    裁剪框的高度
     *
     * @see unsetScissor
     * @see View::setViewport
     * @see View::setDynamicResolutionOptions
     */
    void setScissor(uint32_t left, uint32_t bottom, uint32_t width, uint32_t height) noexcept;

    /**
     * Returns the scissor rectangle to its default disabled setting.
     *
     * Currently the scissor is not compatible with dynamic resolution and should always be
     * disabled when dynamic resolution is used.
     *
     * @see View::setDynamicResolutionOptions
     */
    /**
     * 将裁剪矩形返回到其默认的禁用设置
     *
     * 目前裁剪与动态分辨率不兼容，当使用动态分辨率时应始终
     * 禁用。
     *
     * @see View::setDynamicResolutionOptions
     */
    void unsetScissor() noexcept;

    /**
     * Sets a polygon offset that will be applied to all renderables drawn with this material
     * instance.
     *
     *  The value of the offset is scale * dz + r * constant, where dz is the change in depth
     *  relative to the screen area of the triangle, and r is the smallest value that is guaranteed
     *  to produce a resolvable offset for a given implementation. This offset is added before the
     *  depth test.
     *
     *  @warning using a polygon offset other than zero has a significant negative performance
     *  impact, as most implementations have to disable early depth culling. DO NOT USE unless
     *  absolutely necessary.
     *
     * @param scale scale factor used to create a variable depth offset for each triangle
     * @param constant scale factor used to create a constant depth offset for each triangle
     */
    /**
     * 设置多边形偏移，该偏移将应用于使用此材质
     * 实例绘制的所有可渲染对象。
     *
     *  偏移值为 scale * dz + r * constant，其中 dz 是相对于
     *  三角形屏幕区域的深度变化，r 是保证
     *  为给定实现产生可解析偏移的最小值。此偏移在
     *  深度测试之前添加。
     *
     *  @warning 使用非零的多边形偏移会对性能产生显著的负面影响，
     *  因为大多数实现必须禁用早期深度剔除。除非
     *  绝对必要，否则不要使用。
     *
     * @param scale 用于为每个三角形创建可变深度偏移的缩放因子
     * @param constant 用于为每个三角形创建恒定深度偏移的缩放因子
     */
    void setPolygonOffset(float scale, float constant) noexcept;

    /**
     * Overrides the minimum alpha value a fragment must have to not be discarded when the blend
     * mode is MASKED. Defaults to 0.4 if it has not been set in the parent Material. The specified
     * value should be between 0 and 1 and will be clamped if necessary.
     */
    /**
     * 覆盖片段在混合模式为 MASKED 时不被丢弃必须具有的最小 alpha 值。
     * 如果未在父 Material 中设置，则默认为 0.4。指定的
     * 值应在 0 和 1 之间，必要时会被钳制。
     */
    void setMaskThreshold(float threshold) noexcept;

    /**
     * Gets the minimum alpha value a fragment must have to not be discarded when the blend
     * mode is MASKED
     */
    /**
     * 获取片段在混合模式为 MASKED 时不被丢弃必须具有的最小 alpha 值
     */
    float getMaskThreshold() const noexcept;

    /**
     * Sets the screen space variance of the filter kernel used when applying specular
     * anti-aliasing. The default value is set to 0.15. The specified value should be between
     * 0 and 1 and will be clamped if necessary.
     */
    /**
     * 设置应用镜面反射抗锯齿时使用的滤波器内核的屏幕空间方差。
     * 默认值设置为 0.15。指定的值应在
     * 0 和 1 之间，必要时会被钳制。
     */
    void setSpecularAntiAliasingVariance(float variance) noexcept;

    /**
     * Gets the screen space variance of the filter kernel used when applying specular
     * anti-aliasing.
     */
    /**
     * 获取应用镜面反射抗锯齿时使用的滤波器内核的屏幕空间方差
     */
    float getSpecularAntiAliasingVariance() const noexcept;

    /**
     * Sets the clamping threshold used to suppress estimation errors when applying specular
     * anti-aliasing. The default value is set to 0.2. The specified value should be between 0
     * and 1 and will be clamped if necessary.
     */
    /**
     * 设置应用镜面反射抗锯齿时用于抑制估计误差的钳制阈值。
     * 默认值设置为 0.2。指定的值应在 0
     * 和 1 之间，必要时会被钳制。
     */
    void setSpecularAntiAliasingThreshold(float threshold) noexcept;

    /**
     * Gets the clamping threshold used to suppress estimation errors when applying specular
     * anti-aliasing.
     */
    /**
     * 获取应用镜面反射抗锯齿时用于抑制估计误差的钳制阈值
     */
    float getSpecularAntiAliasingThreshold() const noexcept;

    /**
     * Enables or disables double-sided lighting if the parent Material has double-sided capability,
     * otherwise prints a warning. If double-sided lighting is enabled, backface culling is
     * automatically disabled.
     */
    /**
     * 如果父 Material 具有双面能力，则启用或禁用双面光照，
     * 否则打印警告。如果启用了双面光照，背面剔除会
     * 自动禁用。
     */
    void setDoubleSided(bool doubleSided) noexcept;

    /**
     * Returns whether double-sided lighting is enabled when the parent Material has double-sided
     * capability.
     */
    /**
     * 当父 Material 具有双面能力时，返回是否启用了双面光照
     */
    bool isDoubleSided() const noexcept;

    /**
     * Specifies how transparent objects should be rendered (default is DEFAULT).
     */
    /**
     * 指定如何渲染透明对象（默认为 DEFAULT）
     */
    void setTransparencyMode(TransparencyMode mode) noexcept;

    /**
     * Returns the transparency mode.
     */
    /**
     * 返回透明度模式
     */
    TransparencyMode getTransparencyMode() const noexcept;

    /**
     * Overrides the default triangle culling state that was set on the material.
     */
    /**
     * 覆盖材质上设置的默认三角形剔除状态
     */
    void setCullingMode(CullingMode culling) noexcept;

    /**
     * Overrides the default triangle culling state that was set on the material separately for the
     * color and shadow passes
     */
    /**
     * 覆盖材质上为颜色通道和阴影通道分别设置的默认三角形剔除状态
     */
    void setCullingMode(CullingMode colorPassCullingMode, CullingMode shadowPassCullingMode) noexcept;

    /**
     * Returns the face culling mode.
     */
    /**
     * 返回面剔除模式
     */
    CullingMode getCullingMode() const noexcept;

    /**
     * Returns the face culling mode for the shadow passes.
     */
    /**
     * 返回阴影通道的面剔除模式
     */
    CullingMode getShadowCullingMode() const noexcept;

    /**
     * Overrides the default color-buffer write state that was set on the material.
     */
    /**
     * 覆盖材质上设置的颜色缓冲区写入状态
     */
    void setColorWrite(bool enable) noexcept;

    /**
     * Returns whether color write is enabled.
     */
    /**
     * 返回是否启用了颜色写入
     */
    bool isColorWriteEnabled() const noexcept;

    /**
     * Overrides the default depth-buffer write state that was set on the material.
     */
    /**
     * 覆盖材质上设置的深度缓冲区写入状态
     */
    void setDepthWrite(bool enable) noexcept;

    /**
     * Returns whether depth write is enabled.
     */
    /**
     * 返回是否启用了深度写入
     */
    bool isDepthWriteEnabled() const noexcept;

    /**
     * Overrides the default depth testing state that was set on the material.
     */
    /**
     * 覆盖材质上设置的深度测试状态
     */
    void setDepthCulling(bool enable) noexcept;

    /**
     * Overrides the default depth function state that was set on the material.
     */
    /**
     * 覆盖材质上设置的深度函数状态
     */
    void setDepthFunc(DepthFunc depthFunc) noexcept;

    /**
     * Returns the depth function state.
     */
    /**
     * 返回深度函数状态
     */
    DepthFunc getDepthFunc() const noexcept;

    /**
     * Returns whether depth culling is enabled.
     */
    /**
     * 返回是否启用了深度剔除
     */
    bool isDepthCullingEnabled() const noexcept;

    /**
     * Overrides the default stencil-buffer write state that was set on the material.
     */
    /**
     * 覆盖材质上设置的模板缓冲区写入状态
     */
    void setStencilWrite(bool enable) noexcept;

    /**
     * Returns whether stencil write is enabled.
     */
    /**
     * 返回是否启用了模板写入
     */
    bool isStencilWriteEnabled() const noexcept;

    /**
     * Sets the stencil comparison function (default is StencilCompareFunc::A).
     *
     * It's possible to set separate stencil comparison functions; one for front-facing polygons,
     * and one for back-facing polygons. The face parameter determines the comparison function(s)
     * updated by this call.
     */
    /**
     * 设置模板比较函数（默认为 StencilCompareFunc::A）
     *
     * 可以设置单独的模板比较函数；一个用于正面多边形，
     * 一个用于背面多边形。face 参数确定此调用更新的比较函数。
     */
    void setStencilCompareFunction(StencilCompareFunc func,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the stencil fail operation (default is StencilOperation::KEEP).
     *
     * The stencil fail operation is performed to update values in the stencil buffer when the
     * stencil test fails.
     *
     * It's possible to set separate stencil fail operations; one for front-facing polygons, and one
     * for back-facing polygons. The face parameter determines the stencil fail operation(s) updated
     * by this call.
     */
    /**
     * 设置模板失败操作（默认为 StencilOperation::KEEP）
     *
     * 当模板测试失败时，执行模板失败操作以更新模板缓冲区中的值。
     *
     * 可以设置单独的模板失败操作；一个用于正面多边形，一个
     * 用于背面多边形。face 参数确定此调用更新的模板失败操作。
     */
    void setStencilOpStencilFail(StencilOperation op,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the depth fail operation (default is StencilOperation::KEEP).
     *
     * The depth fail operation is performed to update values in the stencil buffer when the depth
     * test fails.
     *
     * It's possible to set separate depth fail operations; one for front-facing polygons, and one
     * for back-facing polygons. The face parameter determines the depth fail operation(s) updated
     * by this call.
     */
    /**
     * 设置深度失败操作（默认为 StencilOperation::KEEP）
     *
     * 当深度测试失败时，执行深度失败操作以更新模板缓冲区中的值。
     *
     * 可以设置单独的深度失败操作；一个用于正面多边形，一个
     * 用于背面多边形。face 参数确定此调用更新的深度失败操作。
     */
    void setStencilOpDepthFail(StencilOperation op,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the depth-stencil pass operation (default is StencilOperation::KEEP).
     *
     * The depth-stencil pass operation is performed to update values in the stencil buffer when
     * both the stencil test and depth test pass.
     *
     * It's possible to set separate depth-stencil pass operations; one for front-facing polygons,
     * and one for back-facing polygons. The face parameter determines the depth-stencil pass
     * operation(s) updated by this call.
     */
    /**
     * 设置深度-模板通过操作（默认为 StencilOperation::KEEP）
     *
     * 当模板测试和深度测试都通过时，执行深度-模板通过操作以更新模板缓冲区中的值。
     *
     * 可以设置单独的深度-模板通过操作；一个用于正面多边形，
     * 一个用于背面多边形。face 参数确定此调用更新的深度-模板通过
     * 操作。
     */
    void setStencilOpDepthStencilPass(StencilOperation op,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the stencil reference value (default is 0).
     *
     * The stencil reference value is the left-hand side for stencil comparison tests. It's also
     * used as the replacement stencil value when StencilOperation is REPLACE.
     *
     * It's possible to set separate stencil reference values; one for front-facing polygons, and
     * one for back-facing polygons. The face parameter determines the reference value(s) updated by
     * this call.
     */
    /**
     * 设置模板参考值（默认为 0）
     *
     * 模板参考值是模板比较测试的左侧值。当
     * StencilOperation 为 REPLACE 时，它也被用作替换模板值。
     *
     * 可以设置单独的模板参考值；一个用于正面多边形，
     * 一个用于背面多边形。face 参数确定此调用更新的参考值。
     */
    void setStencilReferenceValue(uint8_t value,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the stencil read mask (default is 0xFF).
     *
     * The stencil read mask masks the bits of the values participating in the stencil comparison
     * test- both the value read from the stencil buffer and the reference value.
     *
     * It's possible to set separate stencil read masks; one for front-facing polygons, and one for
     * back-facing polygons. The face parameter determines the stencil read mask(s) updated by this
     * call.
     */
    /**
     * 设置模板读取掩码（默认为 0xFF）
     *
     * 模板读取掩码屏蔽参与模板比较
     * 测试的值的位——既包括从模板缓冲区读取的值，也包括参考值。
     *
     * 可以设置单独的模板读取掩码；一个用于正面多边形，一个
     * 用于背面多边形。face 参数确定此调用更新的模板读取掩码。
     */
    void setStencilReadMask(uint8_t readMask,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * Sets the stencil write mask (default is 0xFF).
     *
     * The stencil write mask masks the bits in the stencil buffer updated by stencil operations.
     *
     * It's possible to set separate stencil write masks; one for front-facing polygons, and one for
     * back-facing polygons. The face parameter determines the stencil write mask(s) updated by this
     * call.
     */
    /**
     * 设置模板写入掩码（默认为 0xFF）
     *
     * 模板写入掩码屏蔽模板缓冲区中由模板操作更新的位。
     *
     * 可以设置单独的模板写入掩码；一个用于正面多边形，一个
     * 用于背面多边形。face 参数确定此调用更新的模板写入掩码。
     */
    void setStencilWriteMask(uint8_t writeMask,
            StencilFace face = StencilFace::FRONT_AND_BACK) noexcept;

    /**
     * PostProcess and compute domain material instance must be commited manually. This call has
     * no effect on surface domain materials.
     * @param engine Filament engine
     */
    /**
     * PostProcess 和 compute 域的材质实例必须手动提交。此调用对
     * surface 域材质无效。
     * @param engine Filament 引擎
     */
    void commit(Engine& engine) const;

protected:
    // prevent heap allocation
    ~MaterialInstance() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_MATERIALINSTANCE_H
