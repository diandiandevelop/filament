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

#ifndef TNT_FILAMENT_MATERIAL_H
#define TNT_FILAMENT_MATERIAL_H

#include <filament/Color.h>
#include <filament/FilamentAPI.h>
#include <filament/MaterialEnums.h>
#include <filament/MaterialInstance.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/Invocable.h>

#include <math/mathfwd.h>

#include <type_traits>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace utils {
    class CString;
} // namespace utils

namespace filament {

class Texture;
class TextureSampler;

class FEngine;
class FMaterial;

class Engine;

/**
 * 材质类
 * 
 * Material 表示一个可重用的材质定义，包含着色器代码和材质参数。
 * 
 * 功能：
 * - 定义渲染表面的外观和行为
 * - 管理着色器程序（顶点、片段着色器）
 * - 定义材质参数（颜色、纹理、常量等）
 * - 创建材质实例（MaterialInstance）
 * 
 * 生命周期：
 * - 通过 Builder 创建
 * - 由 Engine 管理
 * - 使用 Engine::destroy() 销毁
 * 
 * 使用流程：
 * 1. 使用 Material::Builder 创建材质
 * 2. 调用 build() 创建 Material 对象
 * 3. 调用 createInstance() 创建材质实例
 * 4. 在材质实例上设置参数值
 * 5. 将材质实例应用到渲染对象
 */
class UTILS_PUBLIC Material : public FilamentAPI {
    struct BuilderDetails;

public:
    /**
     * 类型别名
     * 
     * 从 MaterialEnums.h 和 backend 命名空间导入的类型。
     */
    using BlendingMode = filament::BlendingMode;           // 混合模式
    using Shading = filament::Shading;                     // 着色模型
    using Interpolation = filament::Interpolation;          // 插值模式
    using VertexDomain = filament::VertexDomain;            // 顶点域
    using TransparencyMode = filament::TransparencyMode;    // 透明度模式

    using ParameterType = backend::UniformType;             // 参数类型
    using Precision = backend::Precision;                    // 精度
    using SamplerType = backend::SamplerType;               // 采样器类型
    using SamplerFormat = backend::SamplerFormat;           // 采样器格式
    using CullingMode = backend::CullingMode;              // 面剔除模式
    using ShaderModel = backend::ShaderModel;               // 着色器模型
    using SubpassType = backend::SubpassType;               // 子通道类型

    /**
     * Defines whether a material instance should use UBO batching or not.
     */
    /**
     * UBO 批处理模式枚举
     * 
     * 定义材质实例是否应该使用 UBO（Uniform Buffer Object）批处理。
     * 
     * UBO 批处理：
     * - 将多个材质实例的 uniform 数据打包到单个 UBO 中
     * - 减少绘制调用和状态切换
     * - 提高渲染性能
     * - 仅适用于 SURFACE 域的材质
     */
    enum class UboBatchingMode {
        /**
         * For default, it follows the engine settings.
         * If UBO batching is enabled on the engine and the material domain is SURFACE, it
         * turns on the UBO batching. Otherwise, it turns off the UBO batching.
        */
        /**
         * 默认模式
         * 
         * 遵循引擎设置。
         * - 如果引擎启用了 UBO 批处理且材质域为 SURFACE，则启用批处理
         * - 否则禁用批处理
         */
        DEFAULT,
        
        /**
         * 禁用 UBO 批处理
         * 
         * 明确禁用此材质的 UBO 批处理。
         * 即使引擎启用了批处理，此材质也不会使用批处理。
         */
        //! Disable the Ubo Batching for this material
        DISABLED,
    };

    /**
     * Holds information about a material parameter.
     */
    /**
     * 材质参数信息结构
     * 
     * 包含材质参数的所有元数据信息。
     * 
     * 用途：
     * - 查询材质支持的参数
     * - 获取参数的类型和属性
     * - 用于材质编辑器或工具
     */
    struct ParameterInfo {
        /**
         * Name of the parameter.
         * 
         * 参数名称
         * - 在着色器中定义的参数名称
         * - 用于 setParameter() 等方法
         * - 必须以 null 结尾的 C 字符串
         */
        //! Name of the parameter.
        const char* UTILS_NONNULL name;
        
        /**
         * Whether the parameter is a sampler (texture).
         * 
         * 是否为采样器（纹理）参数
         * - true: 参数是纹理采样器
         * - false: 参数是普通 uniform 变量
         */
        //! Whether the parameter is a sampler (texture).
        bool isSampler;
        
        /**
         * Whether the parameter is a subpass type.
         * 
         * 是否为子通道类型参数
         * - true: 参数是子通道输入（用于多通道渲染）
         * - false: 参数不是子通道类型
         */
        //! Whether the parameter is a subpass type.
        bool isSubpass;
        
        /**
         * 参数类型联合体
         * 
         * 根据参数类型（普通、采样器、子通道）使用不同的字段。
         */
        union {
            /**
             * Type of the parameter if the parameter is not a sampler.
             * 
             * 参数类型（如果参数不是采样器）
             * - 用于普通 uniform 变量
             * - 如 FLOAT、FLOAT3、MAT4 等
             */
            //! Type of the parameter if the parameter is not a sampler.
            ParameterType type;
            
            /**
             * Type of the parameter if the parameter is a sampler.
             * 
             * 采样器类型（如果参数是采样器）
             * - 用于纹理采样器
             * - 如 SAMPLER_2D、SAMPLER_CUBEMAP 等
             */
            //! Type of the parameter if the parameter is a sampler.
            SamplerType samplerType;
            
            /**
             * Type of the parameter if the parameter is a subpass.
             * 
             * 子通道类型（如果参数是子通道）
             * - 用于子通道输入
             */
            //! Type of the parameter if the parameter is a subpass.
            SubpassType subpassType;
        };
        
        /**
         * Size of the parameter when the parameter is an array.
         * 
         * 数组大小
         * - 如果参数是数组，表示数组元素个数
         * - 如果参数不是数组，值为 1
         */
        //! Size of the parameter when the parameter is an array.
        uint32_t count;
        
        /**
         * Requested precision of the parameter.
         * 
         * 请求的精度
         * - LOW: 低精度（通常用于移动设备）
         * - MEDIUM: 中等精度
         * - HIGH: 高精度
         * - DEFAULT: 使用默认精度
         */
        //! Requested precision of the parameter.
        Precision precision;
    };

    /**
     * 材质构建器类
     * 
     * 用于构建 Material 对象的构建器类，使用构建器模式。
     * 
     * 使用方式：
     * ```cpp
     * Material* material = Material::Builder()
     *     .package(payload, size)
     *     .constant("myConstant", 42)
     *     .build(engine);
     * ```
     */
    class Builder : public BuilderBase<BuilderDetails> {
        friend struct BuilderDetails;
    public:
        /**
         * 默认构造函数
         * 
         * 创建一个空的材质构建器。
         */
        Builder() noexcept;
        
        /**
         * 拷贝构造函数
         */
        Builder(Builder const& rhs) noexcept;
        
        /**
         * 移动构造函数
         */
        Builder(Builder&& rhs) noexcept;
        
        /**
         * 析构函数
         */
        ~Builder() noexcept;
        
        /**
         * 拷贝赋值操作符
         */
        Builder& operator=(Builder const& rhs) noexcept;
        
        /**
         * 移动赋值操作符
         */
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * 阴影采样质量枚举
         * 
         * 定义阴影采样的质量级别，影响阴影的软硬程度和性能。
         * 
         * 注意：仅对已启用光照且域为 SURFACE 的材质有效。
         */
        enum class ShadowSamplingQuality : uint8_t {
            /**
             * 硬阴影
             * 
             * 使用 2x2 PCF（Percentage Closer Filtering）采样。
             * - 性能较高
             * - 阴影边缘较硬
             */
            HARD,   // 2x2 PCF
            
            /**
             * 软阴影
             * 
             * 使用 3x3 高斯滤波器采样。
             * - 性能较低
             * - 阴影边缘较软，更平滑
             */
            LOW     // 3x3 gaussian filter
        };

        /**
         * Specifies the material data. The material data is a binary blob produced by
         * libfilamat or by matc.
         *
         * @param payload Pointer to the material data, must stay valid until build() is called.
         * @param size Size of the material data pointed to by "payload" in bytes.
         */
        /**
         * 指定材质数据
         * 
         * 设置材质数据包，包含编译后的着色器代码和材质元数据。
         * 
         * @param payload 指向材质数据的指针
         *                 - 必须保持有效，直到 build() 被调用
         *                 - 材质数据是二进制 blob，由 libfilamat 或 matc 工具生成
         *                 - 包含编译后的着色器代码、参数定义等
         * 
         * @param size    材质数据的大小（字节）
         * 
         * @return 当前构建器的引用（支持链式调用）
         * 
         * 材质数据来源：
         * - libfilamat: Filament 的材质编译库
         * - matc: Filament 的材质编译命令行工具
         * - 从 .mat 文件（JSON 格式）编译生成
         * 
         * 注意：
         * - 这是构建材质的必需步骤
         * - payload 指针在 build() 调用前必须保持有效
         * - build() 调用后，数据会被复制，payload 可以释放
         */
        Builder& package(const void* UTILS_NONNULL payload, size_t size);

        /**
         * 支持的常量参数类型检查
         * 
         * 类型特征，用于编译时检查常量参数类型是否支持。
         * 仅支持 int32_t、float 和 bool。
         */
        template<typename T>
        using is_supported_constant_parameter_t = std::enable_if_t<
                std::is_same_v<int32_t, T> ||
                std::is_same_v<float, T> ||
                std::is_same_v<bool, T>>;

        /**
         * Specialize a constant parameter specified in the material definition with a concrete
         * value for this material. Once build() is called, this constant cannot be changed.
         * Will throw an exception if the name does not match a constant specified in the
         * material definition or if the type provided does not match.
         *
         * @tparam T The type of constant parameter, either int32_t, float, or bool.
         * @param name The name of the constant parameter specified in the material definition, such
         *             as "myConstant".
         * @param nameLength Length in `char` of the name parameter.
         * @param value The value to use for the constant parameter, must match the type specified
         *              in the material definition.
         */
        /**
         * 特化常量参数
         * 
         * 为材质定义中指定的常量参数设置具体值。
         * 
         * 模板参数：
         * - T: 常量参数类型，必须是 int32_t、float 或 bool 之一
         * 
         * @param name       材质定义中指定的常量参数名称（如 "myConstant"）
         * @param nameLength 名称的长度（字符数）
         * @param value      常量参数的值，必须与材质定义中指定的类型匹配
         * 
         * @return 当前构建器的引用（支持链式调用）
         * 
         * 重要说明：
         * - 一旦 build() 被调用，此常量就不能再更改
         * - 如果名称不匹配或类型不匹配，会抛出异常
         * - 常量在编译时优化到着色器中，提高性能
         * 
         * 使用场景：
         * - 根据运行时条件生成不同的材质变体
         * - 优化着色器性能（编译时已知的值）
         * - 减少运行时分支
         * 
         * 示例：
         * ```cpp
         * Builder()
         *     .package(payload, size)
         *     .constant("enableNormalMapping", true)
         *     .constant("roughness", 0.5f)
         *     .build(engine);
         * ```
         */
        template<typename T, typename = is_supported_constant_parameter_t<T>>
        Builder& constant(const char* UTILS_NONNULL name, size_t nameLength, T value);

        /**
         * 内联辅助函数：使用 null 终止的 C 字符串提供常量名称
         * 
         * 便捷版本，自动计算字符串长度。
         * 
         * @param name  常量参数名称（null 终止的 C 字符串）
         * @param value 常量参数的值
         * 
         * @return 当前构建器的引用
         * 
         * 实现：
         * - 使用 strlen() 计算名称长度
         * - 调用 constant(name, strlen(name), value)
         */
        /** inline helper to provide the constant name as a null-terminated C string */
        template<typename T, typename = is_supported_constant_parameter_t<T>>
        inline Builder& constant(const char* UTILS_NONNULL name, T value) {
            return constant(name, strlen(name), value);
        }

        /**
         * Sets the quality of the indirect lights computations. This is only taken into account
         * if this material is lit and in the surface domain. This setting will affect the
         * IndirectLight computation if one is specified on the Scene and Spherical Harmonics
         * are used for the irradiance.
         *
         * @param shBandCount Number of spherical harmonic bands. Must be 1, 2 or 3 (default).
         * @return Reference to this Builder for chaining calls.
         * @see IndirectLight
         */
        /**
         * 设置间接光照计算的质量
         * 
         * 设置球谐函数（Spherical Harmonics）的频带数量，影响间接光照的计算质量和性能。
         * 
         * @param shBandCount 球谐函数频带数量
         *                    - 必须是 1、2 或 3（默认）
         *                    - 1: 最低质量，最快性能
         *                    - 2: 中等质量
         *                    - 3: 最高质量，最慢性能
         * 
         * @return 当前构建器的引用（支持链式调用）
         * 
         * 限制条件：
         * - 仅对已启用光照（lit）且域为 SURFACE 的材质有效
         * 
         * 影响：
         * - 如果场景中指定了 IndirectLight 且使用球谐函数计算辐照度，此设置会影响计算
         * - 频带数越多，间接光照质量越高，但计算开销也越大
         * 
         * @see IndirectLight
         */
        Builder& sphericalHarmonicsBandCount(size_t shBandCount) noexcept;

        /**
         * Set the quality of shadow sampling. This is only taken into account
         * if this material is lit and in the surface domain.
         * @param quality
         * @return
         */
        /**
         * 设置阴影采样质量
         * 
         * 设置阴影采样的质量级别，影响阴影的软硬程度和性能。
         * 
         * @param quality 阴影采样质量
         *                 - HARD: 硬阴影（2x2 PCF），性能较高
         *                 - LOW: 软阴影（3x3 高斯滤波），性能较低但更平滑
         * 
         * @return 当前构建器的引用（支持链式调用）
         * 
         * 限制条件：
         * - 仅对已启用光照（lit）且域为 SURFACE 的材质有效
         * 
         * 影响：
         * - 影响阴影边缘的软硬程度
         * - 影响阴影渲染的性能
         */
        Builder& shadowSamplingQuality(ShadowSamplingQuality quality) noexcept;

        /**
         * Set the batching mode of the instances created from this material.
         * @param uboBatchingMode
         * @return
         */
        /**
         * 设置从此材质创建的实例的批处理模式
         * 
         * 设置材质实例是否使用 UBO 批处理。
         * 
         * @param uboBatchingMode UBO 批处理模式
         *                        - DEFAULT: 遵循引擎设置
         *                        - DISABLED: 禁用批处理
         * 
         * @return 当前构建器的引用（支持链式调用）
         * 
         * 用途：
         * - 控制材质实例是否参与 UBO 批处理
         * - 批处理可以提高渲染性能，但需要材质域为 SURFACE
         * - 某些特殊材质可能需要禁用批处理
         */
        Builder& uboBatching(UboBatchingMode uboBatchingMode) noexcept;

        /**
         * Creates the Material object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this Material with.
         *
         * @return pointer to the newly created object or nullptr if exceptions are disabled and
         *         an error occurred.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 创建 Material 对象
         * 
         * 根据构建器的配置创建 Material 对象。
         * 
         * @param engine 要与此材质关联的 Engine 引用
         *               - 材质由 Engine 管理
         *               - 使用 Engine::destroy() 销毁材质
         * 
         * @return 指向新创建对象的指针
         *         - 成功时返回有效指针
         *         - 如果禁用了异常且发生错误，返回 nullptr
         * 
         * 异常：
         * - utils::PostConditionPanic: 运行时错误（如内存不足、资源耗尽等）
         * - utils::PreConditionPanic: 构建器函数参数无效
         * 
         * 实现步骤：
         * 1. 验证构建器配置（材质数据、常量参数等）
         * 2. 解析材质数据包
         * 3. 创建 Material 对象
         * 4. 初始化材质状态（着色器、参数等）
         * 5. 创建默认材质实例
         * 
         * 注意：
         * - 调用此方法后，构建器可以销毁
         * - 材质数据会被复制，payload 指针可以释放
         * - 常量参数在此之后不能再更改
         */
        Material* UTILS_NULLABLE build(Engine& engine) const;
    private:
        friend class FMaterial;
    };

    /**
     * 编译器优先级队列类型别名
     * 
     * 用于控制着色器编译的优先级。
     */
    using CompilerPriorityQueue = backend:: CompilerPriorityQueue;

    /**
     * Asynchronously ensures that a subset of this Material's variants are compiled. After issuing
     * several Material::compile() calls in a row, it is recommended to call Engine::flush()
     * such that the backend can start the compilation work as soon as possible.
     * The provided callback is guaranteed to be called on the main thread after all specified
     * variants of the material are compiled. This can take hundreds of milliseconds.
     *
     * If all the material's variants are already compiled, the callback will be scheduled as
     * soon as possible, but this might take a few dozen millisecond, corresponding to how
     * many previous frames are enqueued in the backend. This also varies by backend. Therefore,
     * it is recommended to only call this method once per material shortly after creation.
     *
     * If the same variant is scheduled for compilation multiple times, the first scheduling
     * takes precedence; later scheduling are ignored.
     *
     * caveat: A consequence is that if a variant is scheduled on the low priority queue and later
     * scheduled again on the high priority queue, the later scheduling is ignored.
     * Therefore, the second callback could be called before the variant is compiled.
     * However, the first callback, if specified, will trigger as expected.
     *
     * The callback is guaranteed to be called. If the engine is destroyed while some material
     * variants are still compiling or in the queue, these will be discarded and the corresponding
     * callback will be called. In that case however the Material pointer passed to the callback
     * is guaranteed to be invalid (either because it's been destroyed by the user already, or,
     * because it's been cleaned-up by the Engine).
     *
     * UserVariantFilterMask::ALL should be used with caution. Only variants that an application
     * needs should be included in the variants argument. For example, the STE variant is only used
     * for stereoscopic rendering. If an application is not planning to render in stereo, this bit
     * should be turned off to avoid unnecessary material compilations.
     *
     * @param priority      Which priority queue to use, LOW or HIGH.
     * @param variants      Variants to include to the compile command.
     * @param handler       Handler to dispatch the callback or nullptr for the default handler
     * @param callback      callback called on the main thread when the compilation is done on
     *                      by backend.
     */
    /**
     * 异步编译材质的变体子集
     * 
     * 确保此材质的指定变体子集被编译。着色器编译是异步进行的，可能需要数百毫秒。
     * 
     * @param priority  要使用的优先级队列
     *                  - CRITICAL: 立即需要，如果平台不支持并行编译，会同步编译
     *                  - HIGH: 很快需要
     *                  - LOW: 最终需要
     * 
     * @param variants  要包含在编译命令中的变体
     *                  - 使用 UserVariantFilterMask 指定要编译的变体
     *                  - UserVariantFilterMask::ALL 应谨慎使用
     *                  - 只包含应用程序需要的变体（如不使用立体渲染，不要包含 STE 位）
     * 
     * @param handler   用于分发回调的处理器（可选）
     *                  - nullptr 表示使用默认处理器
     *                  - 用于控制回调的执行线程
     * 
     * @param callback  编译完成时在主线程上调用的回调函数（可选）
     *                  - 签名：void(Material* material)
     *                  - 在所有指定变体编译完成后调用
     *                  - 如果材质已编译，回调会尽快调度（可能需要几十毫秒）
     * 
     * 重要说明：
     * - 连续调用多个 Material::compile() 后，建议调用 Engine::flush() 以便后端尽快开始编译
     * - 如果同一变体被多次调度编译，第一次调度优先，后续调度被忽略
     * - 回调保证会被调用（即使引擎在编译过程中被销毁）
     * - 如果引擎被销毁，传递给回调的 Material 指针可能无效
     * - 建议在材质创建后立即调用一次此方法
     * 
     * 注意事项：
     * - 如果变体在低优先级队列中调度，然后在高优先级队列中再次调度，后续调度会被忽略
     * - 因此第二个回调可能在变体编译完成前被调用
     * - 但第一个回调（如果指定）会按预期触发
     * 
     * 使用示例：
     * ```cpp
     * material->compile(CompilerPriorityQueue::HIGH, 
     *                   UserVariantFilterBit::ALL,
     *                   nullptr,
     *                   [](Material* mat) {
     *                       // 编译完成
     *                   });
     * ```
     */
    void compile(CompilerPriorityQueue priority,
            UserVariantFilterMask variants,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr,
            utils::Invocable<void(Material* UTILS_NONNULL)>&& callback = {}) noexcept;

    /**
     * 编译变体的便捷版本（使用 UserVariantFilterBit）
     * 
     * 内联辅助函数，接受单个变体位而不是变体掩码。
     * 
     * @param priority  优先级队列
     * @param variants  单个变体位（会被转换为掩码）
     * @param handler   回调处理器（可选）
     * @param callback  回调函数（可选）
     * 
     * 实现：将 variants 转换为 UserVariantFilterMask 并调用主版本。
     */
    inline void compile(CompilerPriorityQueue priority,
            UserVariantFilterBit variants,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr,
            utils::Invocable<void(Material* UTILS_NONNULL)>&& callback = {}) noexcept {
        compile(priority, UserVariantFilterMask(variants), handler,
                std::forward<utils::Invocable<void(Material* UTILS_NONNULL)>>(callback));
    }

    /**
     * 编译所有变体的便捷版本
     * 
     * 内联辅助函数，编译所有支持的变体。
     * 
     * @param priority  优先级队列
     * @param handler   回调处理器（可选）
     * @param callback  回调函数（可选）
     * 
     * 实现：使用 UserVariantFilterBit::ALL 调用主版本。
     * 
     * 注意：编译所有变体可能很耗时，建议只编译需要的变体。
     */
    inline void compile(CompilerPriorityQueue priority,
            backend::CallbackHandler* UTILS_NULLABLE handler = nullptr,
            utils::Invocable<void(Material* UTILS_NONNULL)>&& callback = {}) noexcept {
        compile(priority, UserVariantFilterBit::ALL, handler,
                std::forward<utils::Invocable<void(Material* UTILS_NONNULL)>>(callback));
    }

    /**
     * Creates a new instance of this material. Material instances should be freed using
     * Engine::destroy(const MaterialInstance*).
     *
     * @param name Optional name to associate with the given material instance. If this is null,
     * then the instance inherits the material's name.
     *
     * @return A pointer to the new instance.
     */
    /**
     * 创建材质实例
     * 
     * 从此材质创建一个新的材质实例。材质实例用于设置材质参数的实际值。
     * 
     * @param name 要与材质实例关联的可选名称
     *             - 如果为 nullptr，实例继承材质的名称
     *             - 用于调试和日志记录
     * 
     * @return 指向新实例的指针
     *         - 总是返回有效指针（不会返回 nullptr）
     * 
     * 生命周期：
     * - 材质实例由 Engine 管理
     * - 使用 Engine::destroy(const MaterialInstance*) 销毁
     * - 材质被销毁时，所有实例也会被销毁
     * 
     * 使用方式：
     * ```cpp
     * MaterialInstance* instance = material->createInstance("myInstance");
     * instance->setParameter("baseColor", RgbType::sRGB, {1.0f, 0.0f, 0.0f});
     * // 使用 instance 渲染对象
     * ```
     * 
     * 注意：
     * - 每个材质都有一个默认实例（getDefaultInstance()）
     * - 可以创建多个实例，每个实例有独立的参数值
     * - 实例共享相同的着色器代码和参数定义
     */
    MaterialInstance* UTILS_NONNULL createInstance(const char* UTILS_NULLABLE name = nullptr) const noexcept;

    /**
     * 返回材质的名称
     * 
     * @return 以 null 结尾的字符串，表示材质的名称
     */
    //! Returns the name of this material as a null-terminated string.
    const char* UTILS_NONNULL getName() const noexcept;

    /**
     * 返回材质的着色模型
     * 
     * @return 着色模型（如 UNLIT、LIT 等）
     */
    //! Returns the shading model of this material.
    Shading getShading() const noexcept;

    /**
     * 返回材质的插值模式
     * 
     * 影响变量在顶点和片段着色器之间的插值方式。
     * 
     * @return 插值模式（SMOOTH、FLAT 等）
     */
    //! Returns the interpolation mode of this material. This affects how variables are interpolated.
    Interpolation getInterpolation() const noexcept;

    /**
     * 返回材质的混合模式
     * 
     * @return 混合模式（OPAQUE、TRANSPARENT、FADE、MASKED 等）
     */
    //! Returns the blending mode of this material.
    BlendingMode getBlendingMode() const noexcept;

    /**
     * 返回材质的顶点域
     * 
     * @return 顶点域（OBJECT、WORLD、VIEW、CLIP 等）
     */
    //! Returns the vertex domain of this material.
    VertexDomain getVertexDomain() const noexcept;

    /**
     * 返回材质支持的变体
     * 
     * @return 支持的变体掩码
     */
    //! Returns the material's supported variants
    UserVariantFilterMask getSupportedVariants() const noexcept;

    /**
     * 返回材质的域
     * 
     * 材质域决定材质的使用方式（SURFACE、POST_PROCESS 等）。
     * 
     * @return 材质域
     */
    //! Returns the material domain of this material.
    //! The material domain determines how the material is used.
    MaterialDomain getMaterialDomain() const noexcept;

    /**
     * 返回材质的默认面剔除模式
     * 
     * @return 面剔除模式（NONE、FRONT、BACK、FRONT_AND_BACK）
     */
    //! Returns the default culling mode of this material.
    CullingMode getCullingMode() const noexcept;

    /**
     * 返回材质的透明度模式
     * 
     * 此值仅在混合模式为透明（TRANSPARENT）或淡入淡出（FADE）时才有意义。
     * 
     * @return 透明度模式（DEFAULT、TWO_PASSES_ONE_SIDE、TWO_PASSES_TWO_SIDES）
     */
    //! Returns the transparency mode of this material.
    //! This value only makes sense when the blending mode is transparent or fade.
    TransparencyMode getTransparencyMode() const noexcept;

    /**
     * 指示此材质的实例是否默认写入颜色缓冲区
     * 
     * @return 如果启用颜色写入返回 true，否则返回 false
     */
    //! Indicates whether instances of this material will, by default, write to the color buffer.
    bool isColorWriteEnabled() const noexcept;

    /**
     * 指示此材质的实例是否默认写入深度缓冲区
     * 
     * @return 如果启用深度写入返回 true，否则返回 false
     */
    //! Indicates whether instances of this material will, by default, write to the depth buffer.
    bool isDepthWriteEnabled() const noexcept;

    /**
     * 指示此材质的实例是否默认使用深度测试
     * 
     * @return 如果启用深度测试返回 true，否则返回 false
     */
    //! Indicates whether instances of this material will, by default, use depth testing.
    bool isDepthCullingEnabled() const noexcept;

    /**
     * 指示此材质是否为双面
     * 
     * 双面材质会渲染正面和背面，不受面剔除影响。
     * 
     * @return 如果是双面材质返回 true，否则返回 false
     */
    //! Indicates whether this material is double-sided.
    bool isDoubleSided() const noexcept;

    /**
     * 指示此材质是否使用 alpha to coverage
     * 
     * Alpha to coverage 将 alpha 值转换为覆盖掩码，用于多重采样抗锯齿。
     * 
     * @return 如果启用 alpha to coverage 返回 true，否则返回 false
     */
    //! Indicates whether this material uses alpha to coverage.
    bool isAlphaToCoverageEnabled() const noexcept;

    /**
     * 返回遮罩阈值
     * 
     * 当混合模式设置为 MASKED 时使用的 alpha 遮罩阈值。
     * 
     * @return alpha 遮罩阈值（0.0 到 1.0）
     */
    //! Returns the alpha mask threshold used when the blending mode is set to masked.
    float getMaskThreshold() const noexcept;

    /**
     * 指示此材质是否将阴影因子用作颜色乘数
     * 
     * 此值仅在着色模式为 UNLIT 时才有意义。
     * 
     * @return 如果使用阴影乘数返回 true，否则返回 false
     */
    //! Indicates whether this material uses the shadowing factor as a color multiplier.
    //! This values only makes sense when the shading mode is unlit.
    bool hasShadowMultiplier() const noexcept;

    /**
     * 指示此材质是否启用了镜面反射抗锯齿
     * 
     * @return 如果启用镜面反射抗锯齿返回 true，否则返回 false
     */
    //! Indicates whether this material has specular anti-aliasing enabled
    bool hasSpecularAntiAliasing() const noexcept;

    /**
     * 返回镜面反射抗锯齿的屏幕空间方差
     * 
     * @return 方差值（0.0 到 1.0）
     */
    //! Returns the screen-space variance for specular-antialiasing, this value is between 0 and 1.
    float getSpecularAntiAliasingVariance() const noexcept;

    /**
     * 返回镜面反射抗锯齿的钳制阈值
     * 
     * @return 阈值（0.0 到 1.0）
     */
    //! Returns the clamping threshold for specular-antialiasing, this value is between 0 and 1.
    float getSpecularAntiAliasingThreshold() const noexcept;

    /**
     * 返回此材质所需的顶点属性列表
     * 
     * @return 顶点属性位集
     */
    //! Returns the list of vertex attributes required by this material.
    AttributeBitset getRequiredAttributes() const noexcept;

    /**
     * 返回此材质使用的折射模式
     * 
     * @return 折射模式（NONE、CUBEMAP、SCREEN_SPACE 等）
     */
    //! Returns the refraction mode used by this material.
    RefractionMode getRefractionMode() const noexcept;

    /**
     * 返回此材质使用的折射类型
     * 
     * @return 折射类型（SOLID、THIN）
     */
    //! Return the refraction type used by this material.
    RefractionType getRefractionType() const noexcept;

    /**
     * 返回此材质使用的反射模式
     * 
     * @return 反射模式（DEFAULT、SCREEN_SPACE 等）
     */
    //! Returns the reflection mode used by this material.
    ReflectionMode getReflectionMode() const noexcept;

    /**
     * 返回此材质所需的最小功能级别
     * 
     * @return 功能级别（FEATURE_LEVEL_0、FEATURE_LEVEL_1 等）
     */
    //! Returns the minimum required feature level for this material.
    backend::FeatureLevel getFeatureLevel() const noexcept;

    /**
     * Returns the number of parameters declared by this material.
     * The returned value can be 0.
     */
    /**
     * 返回此材质声明的参数数量
     * 
     * @return 参数数量（可能为 0）
     * 
     * 用途：
     * - 查询材质支持的参数数量
     * - 用于分配参数信息数组
     */
    size_t getParameterCount() const noexcept;

    /**
     * Gets information about this material's parameters.
     *
     * @param parameters A pointer to a list of ParameterInfo.
     *                   The list must be at least "count" large
     * @param count The number of parameters to retrieve. Must be >= 0 and can be > count.
     *
     * @return The number of parameters written to the parameters pointer.
     */
    /**
     * 获取此材质参数的信息
     * 
     * 获取材质参数的元数据信息（名称、类型、大小等）。
     * 
     * @param parameters 指向 ParameterInfo 列表的指针
     *                   - 列表必须至少为 "count" 大小
     *                   - 用于接收参数信息
     * 
     * @param count      要检索的参数数量
     *                   - 必须 >= 0
     *                   - 可以 > 实际参数数量（只会写入实际存在的参数）
     * 
     * @return 写入 parameters 指针的参数数量
     *         - 可能小于 count（如果材质参数少于 count）
     *         - 可能等于 count（如果材质参数等于或多于 count）
     * 
     * 使用示例：
     * ```cpp
     * size_t count = material->getParameterCount();
     * ParameterInfo* infos = new ParameterInfo[count];
     * material->getParameters(infos, count);
     * // 使用 infos...
     * ```
     */
    size_t getParameters(ParameterInfo* UTILS_NONNULL parameters, size_t count) const noexcept;

    /**
     * 指示给定名称的参数是否存在于此材质上
     * 
     * @param name 参数名称（null 终止的 C 字符串）
     * @return 如果参数存在返回 true，否则返回 false
     */
    //! Indicates whether a parameter of the given name exists on this material.
    bool hasParameter(const char* UTILS_NONNULL name) const noexcept;

    /**
     * 指示现有参数是否为采样器
     * 
     * @param name 参数名称（null 终止的 C 字符串）
     * @return 如果参数是采样器返回 true，否则返回 false
     * 
     * 注意：如果参数不存在，行为未定义。
     * 建议先调用 hasParameter() 检查参数是否存在。
     */
    //! Indicates whether an existing parameter is a sampler or not.
    bool isSampler(const char* UTILS_NONNULL name) const noexcept;

    /**
     * Returns a view of the material source (.mat which is a JSON-ish file) string,
     * if it has been set. Otherwise, it returns a view of an empty string.
     * The lifetime of the string_view is tied to the lifetime of the Material.
     */
    /**
     * 返回材质源字符串的视图
     * 
     * 返回材质源文件（.mat，类似 JSON 的文件）字符串的视图。
     * 
     * @return 材质源字符串的视图
     *         - 如果已设置源字符串，返回其视图
     *         - 否则返回空字符串的视图
     * 
     * 生命周期：
     * - string_view 的生命周期与 Material 绑定
     * - Material 被销毁后，string_view 无效
     * 
     * 用途：
     * - 调试和日志记录
     * - 材质编辑器
     * - 材质序列化
     */
    std::string_view getSource() const noexcept;
    
    /**
     *
     * Gets the name of the transform field associated for the given sampler parameter.
     * In the case where the parameter does not have a transform name field, it will return nullptr.
     *
     * @param samplerName the name of the sampler parameter to query.
     *
     * @return If exists, the transform name value otherwise returns a nullptr.
     */
    /**
     * 获取给定采样器参数关联的变换字段名称
     * 
     * 获取与给定采样器参数关联的变换字段名称。
     * 变换字段用于指定纹理的 UV 变换（平移、旋转、缩放等）。
     * 
     * @param samplerName 要查询的采样器参数名称
     *                    - 必须是有效的采样器参数名称
     * 
     * @return 如果存在，返回变换名称值；否则返回 nullptr
     * 
     * 用途：
     * - 查询纹理的 UV 变换参数名称
     * - 用于设置纹理变换
     * - 用于材质工具和编辑器
     * 
     * 示例：
     * ```cpp
     * const char* transformName = material->getParameterTransformName("baseColorMap");
     * if (transformName) {
     *     // 使用 transformName 设置变换参数
     * }
     * ```
     */
    const char* UTILS_NULLABLE getParameterTransformName(
            const char* UTILS_NONNULL samplerName) const noexcept;

    /**
     * Sets the value of the given parameter on this material's default instance.
     *
     * @param name The name of the material parameter
     * @param value The value of the material parameter
     *
     * @see getDefaultInstance()
     */
    /**
     * 设置默认实例的参数值（模板版本）
     * 
     * 在材质的默认实例上设置给定参数的值。
     * 
     * 模板参数：
     * - T: 参数值类型（支持的类型由 MaterialInstance::setParameter 决定）
     * 
     * @param name  材质参数的名称
     * @param value 材质参数的值
     * 
     * 实现：
     * - 调用 getDefaultInstance()->setParameter(name, value)
     * 
     * 用途：
     * - 快速设置默认实例的参数
     * - 避免手动获取默认实例
     * 
     * @see getDefaultInstance()
     */
    template <typename T>
    void setDefaultParameter(const char* UTILS_NONNULL name, T value) noexcept {
        getDefaultInstance()->setParameter(name, value);
    }

    /**
     * Sets a texture and sampler parameters on this material's default instance.
     *
     * @param name The name of the material texture parameter
     * @param texture The texture to set as parameter
     * @param sampler The sampler to be used with this texture
     *
     * @see getDefaultInstance()
     */
    /**
     * 设置默认实例的纹理和采样器参数
     * 
     * 在材质的默认实例上设置纹理和采样器参数。
     * 
     * @param name    材质纹理参数的名称
     * @param texture 要设置为参数的纹理（可以为 nullptr）
     * @param sampler 要与此纹理一起使用的采样器
     *                - 包含过滤、包装、各向异性等设置
     * 
     * 实现：
     * - 调用 getDefaultInstance()->setParameter(name, texture, sampler)
     * 
     * @see getDefaultInstance()
     */
    void setDefaultParameter(const char* UTILS_NONNULL name,
            Texture const* UTILS_NULLABLE texture, TextureSampler const& sampler) noexcept {
        getDefaultInstance()->setParameter(name, texture, sampler);
    }

    /**
     * Sets the color of the given parameter on this material's default instance.
     *
     * @param name The name of the material color parameter
     * @param type Whether the color is specified in the linear or sRGB space
     * @param color The color as a floating point red, green, blue tuple
     *
     * @see getDefaultInstance()
     */
    /**
     * 设置默认实例的 RGB 颜色参数
     * 
     * 在材质的默认实例上设置给定参数的 RGB 颜色值。
     * 
     * @param name  材质颜色参数的名称
     * @param type  颜色空间类型
     *              - LINEAR: 线性颜色空间
     *              - sRGB: sRGB 颜色空间
     * @param color 颜色值（浮点 RGB 三元组）
     *              - 范围通常为 [0.0, 1.0]
     * 
     * 实现：
     * - 调用 getDefaultInstance()->setParameter(name, type, color)
     * 
     * @see getDefaultInstance()
     */
    void setDefaultParameter(const char* UTILS_NONNULL name, RgbType type, math::float3 color) noexcept {
        getDefaultInstance()->setParameter(name, type, color);
    }

    /**
     * Sets the color of the given parameter on this material's default instance.
     *
     * @param name The name of the material color parameter
     * @param type Whether the color is specified in the linear or sRGB space
     * @param color The color as a floating point red, green, blue, alpha tuple
     *
     * @see getDefaultInstance()
     */
    /**
     * 设置默认实例的 RGBA 颜色参数
     * 
     * 在材质的默认实例上设置给定参数的 RGBA 颜色值。
     * 
     * @param name  材质颜色参数的名称
     * @param type  颜色空间类型
     *              - LINEAR: 线性颜色空间
     *              - sRGB: sRGB 颜色空间
     * @param color 颜色值（浮点 RGBA 四元组）
     *              - 范围通常为 [0.0, 1.0]
     * 
     * 实现：
     * - 调用 getDefaultInstance()->setParameter(name, type, color)
     * 
     * @see getDefaultInstance()
     */
    void setDefaultParameter(const char* UTILS_NONNULL name, RgbaType type, math::float4 color) noexcept {
        getDefaultInstance()->setParameter(name, type, color);
    }

    /**
     * 返回此材质的默认实例（非 const）
     * 
     * 每个材质都有一个默认实例，用于设置默认参数值。
     * 
     * @return 指向默认材质实例的指针
     * 
     * 用途：
     * - 设置材质的默认参数值
     * - 所有新创建的实例会继承默认实例的参数值
     * 
     * 注意：
     * - 默认实例由材质管理，不需要手动销毁
     * - 修改默认实例会影响所有使用默认值的实例
     */
    //! Returns this material's default instance.
    MaterialInstance* UTILS_NONNULL getDefaultInstance() noexcept;

    /**
     * 返回此材质的默认实例（const）
     * 
     * @return 指向默认材质实例的常量指针
     */
    //! Returns this material's default instance.
    MaterialInstance const* UTILS_NONNULL getDefaultInstance() const noexcept;

protected:
    // prevent heap allocation
    ~Material() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_MATERIAL_H
