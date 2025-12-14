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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_PROGRAM_H
#define TNT_FILAMENT_BACKEND_PRIVATE_PROGRAM_H

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Invocable.h>

#include <backend/DriverEnums.h>

#include <array>
#include <tuple>
#include <utility>
#include <variant>

#include <stddef.h>
#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

/**
 * 着色器程序类
 * 
 * 用于构建和管理着色器程序，包含顶点、片段、计算着色器的源代码和元数据。
 * 
 * 功能：
 * - 管理着色器源代码（顶点、片段、计算）
 * - 定义描述符绑定（纹理、uniform 缓冲区等）
 * - 支持特化常量（编译时常量）
 * - 支持推送常量（运行时常量）
 * - 支持 ES2 兼容模式（legacy uniform 绑定）
 * 
 * 使用方式：
 * - 使用构建器模式设置各种参数
 * - 通过 Driver 创建实际的着色器程序对象
 * - 支持移动语义，不支持拷贝
 */
class Program {
public:

    /**
     * 着色器类型数量
     * 
     * 支持的着色器阶段数量：顶点、片段、计算。
     */
    static constexpr size_t SHADER_TYPE_COUNT = 3;
    
    /**
     * Uniform 绑定数量
     * 
     * 支持的 uniform 缓冲区绑定数量（由配置定义）。
     */
    static constexpr size_t UNIFORM_BINDING_COUNT = CONFIG_UNIFORM_BINDING_COUNT;
    
    /**
     * 采样器绑定数量
     * 
     * 支持的纹理采样器绑定数量（由配置定义）。
     */
    static constexpr size_t SAMPLER_BINDING_COUNT = CONFIG_SAMPLER_BINDING_COUNT;

    /**
     * 描述符结构
     * 
     * 描述着色器中资源（纹理、缓冲区）的绑定信息。
     * 
     * 组成：
     * - name: 着色器中的资源名称
     * - type: 描述符类型（纹理类型、缓冲区类型等）
     * - binding: 绑定索引（在描述符集中的位置）
     */
    struct Descriptor {
        utils::CString name;           // 着色器中的资源名称
        DescriptorType type;           // 描述符类型
        descriptor_binding_t binding;  // 绑定索引
    };

    /**
     * 特化常量类型
     * 
     * 编译时常量，可以是整数、浮点数或布尔值。
     * 用于在编译时优化着色器代码。
     */
    using SpecializationConstant = std::variant<int32_t, float, bool>;

    /**
     * Uniform 结构（用于 ES2 支持）
     * 
     * 描述 OpenGL ES 2.0 兼容模式下的 uniform 变量信息。
     * 
     * 组成：
     * - name: uniform 字段的完全限定名称
     * - offset: 在 uniform 缓冲区中的偏移量（以 uint32_t 为单位）
     * - size: 数组大小（>1 表示数组）
     * - type: uniform 类型
     */
    struct Uniform { // For ES2 support
        utils::CString name;    // full qualified name of the uniform field
        uint16_t offset;        // offset in 'uint32_t' into the uniform buffer
        uint8_t size;           // >1 for arrays
        UniformType type;       // uniform type
    };

    /**
     * 描述符绑定信息类型
     * 
     * 固定容量的描述符向量，用于存储一个描述符集的绑定信息。
     */
    using DescriptorBindingsInfo = utils::FixedCapacityVector<Descriptor>;
    
    /**
     * 描述符集信息类型
     * 
     * 描述符集数组，最多支持 MAX_DESCRIPTOR_SET_COUNT 个描述符集。
     */
    using DescriptorSetInfo = std::array<DescriptorBindingsInfo, MAX_DESCRIPTOR_SET_COUNT>;
    
    /**
     * 特化常量信息类型
     * 
     * 固定容量的特化常量向量。
     */
    using SpecializationConstantsInfo = utils::FixedCapacityVector<SpecializationConstant>;
    
    /**
     * 着色器二进制数据类型
     * 
     * 固定容量的字节向量，用于存储编译后的着色器二进制数据。
     */
    using ShaderBlob = utils::FixedCapacityVector<uint8_t>;
    
    /**
     * 着色器源代码类型
     * 
     * 着色器二进制数据数组，每个元素对应一个着色器阶段。
     */
    using ShaderSource = std::array<ShaderBlob, SHADER_TYPE_COUNT>;

    /**
     * 顶点属性信息类型
     * 
     * 固定容量的属性名称和位置对向量。
     * 用于 ES2 兼容模式。
     */
    using AttributesInfo = utils::FixedCapacityVector<std::pair<utils::CString, uint8_t>>;
    
    /**
     * Uniform 信息类型
     * 
     * 固定容量的 Uniform 向量。
     * 用于 ES2 兼容模式。
     */
    using UniformInfo = utils::FixedCapacityVector<Uniform>;
    
    /**
     * 绑定 Uniform 信息类型
     * 
     * 固定容量的元组向量，每个元组包含：
     * - uint8_t: 绑定索引
     * - CString: 绑定名称
     * - UniformInfo: Uniform 信息列表
     */
    using BindingUniformsInfo = utils::FixedCapacityVector<
            std::tuple<uint8_t, utils::CString, Program::UniformInfo>>;

    /**
     * 默认构造函数
     * 
     * 创建一个空的 Program 对象。
     */
    Program() noexcept;

    /**
     * 禁用拷贝构造
     * 
     * Program 包含大量数据，不支持拷贝以避免性能问题。
     */
    Program(const Program& rhs) = delete;
    
    /**
     * 禁用拷贝赋值
     */
    Program& operator=(const Program& rhs) = delete;

    /**
     * 移动构造函数
     * 
     * 转移所有权到新对象。
     * 
     * @param rhs 要移动的源对象
     */
    Program(Program&& rhs) noexcept;
    Program& operator=(Program&& rhs) noexcept;

    /**
     * 析构函数
     * 
     * 清理所有资源。
     */
    ~Program() noexcept;

    /**
     * 设置编译优先级队列
     * 
     * 设置着色器编译的优先级，用于并行编译时的调度。
     * 
     * @param priorityQueue 优先级队列（CRITICAL、HIGH、LOW）
     * @return 当前对象的引用（支持链式调用）
     */
    Program& priorityQueue(CompilerPriorityQueue priorityQueue) noexcept;

    /**
     * 设置诊断信息
     * 
     * 设置材质名称和变体，仅用于诊断目的（日志、调试等）。
     * 
     * @param name   材质名称
     * @param logger 日志记录器函数对象
     *               - 签名：ostream&(CString const& name, ostream& out)
     *               - 用于输出诊断信息
     * @return 当前对象的引用
     */
    // sets the material name and variant for diagnostic purposes only
    Program& diagnostics(utils::CString const& name,
            utils::Invocable<utils::io::ostream&(utils::CString const& name,
                    utils::io::ostream& out)>&& logger);

    /**
     * 设置着色器源代码
     * 
     * 设置程序的某个着色器阶段（顶点、片段、计算）的源代码。
     * 
     * @param shader 着色器阶段（VERTEX、FRAGMENT、COMPUTE）
     * @param data   着色器源代码数据指针
     *               - 可以是字符串（GLSL、MSL 等）
     *               - 可以是二进制数据（SPIR-V、Metal 库等）
     * @param size   数据大小（字节）
     *               - 对于字符串：必须包含 null 终止符
     *               - 对于二进制：实际数据大小
     * @return 当前对象的引用
     * 
     * 注意：
     * - 字符串着色器必须以 null 结尾，size 必须包含 null 终止符
     * - 二进制着色器（如 SPIR-V）不需要 null 终止符
     */
    // Sets one of the program's shader (e.g. vertex, fragment)
    // string-based shaders are null terminated, consequently the size parameter must include the
    // null terminating character.
    Program& shader(ShaderStage shader, void const* data, size_t size);

    /**
     * 设置着色器语言
     * 
     * 设置通过 shader() 提供的着色器源代码的语言类型。
     * 
     * @param shaderLanguage 着色器语言（默认 ESSL3）
     *                       - ESSL1: OpenGL ES Shading Language 1.0
     *                       - ESSL3: OpenGL ES Shading Language 3.0
     *                       - SPIRV: SPIR-V 二进制
     *                       - MSL: Metal Shading Language
     *                       - METAL_LIBRARY: 预编译的 Metal 库
     *                       - WGSL: WebGPU Shading Language
     * @return 当前对象的引用
     */
    // Sets the language of the shader sources provided with shader() (defaults to ESSL3)
    Program& shaderLanguage(ShaderLanguage shaderLanguage);

    /**
     * 设置描述符绑定信息
     * 
     * 设置描述符集的绑定信息，描述着色器如何访问资源。
     * 
     * @param set               描述符集索引（0 到 MAX_DESCRIPTOR_SET_COUNT - 1）
     * @param descriptorBindings 描述符绑定信息列表
     *                          - 每个描述符包含：名称、类型、绑定索引
     *                          - 用于将着色器中的资源名称映射到绑定索引
     * @return 当前对象的引用
     * 
     * 用途：
     * - 定义纹理、采样器、uniform 缓冲区的绑定
     * - 在 Vulkan 后端中用于创建描述符集布局
     * - 在 OpenGL 后端中用于验证资源绑定
     */
    // Descriptor binding (set, binding, type -> shader name) info
    Program& descriptorBindings(backend::descriptor_set_t set,
            DescriptorBindingsInfo descriptorBindings) noexcept;

    /**
     * 设置特化常量
     * 
     * 设置编译时常量，用于在编译时优化着色器代码。
     * 
     * @param specConstants 特化常量列表
     *                      - 每个常量可以是 int32_t、float 或 bool
     *                      - 在编译时替换着色器中的常量
     * @return 当前对象的引用
     * 
     * 用途：
     * - 根据运行时条件生成不同的着色器变体
     * - 优化着色器性能（编译时已知的值）
     * - 减少运行时分支
     */
    Program& specializationConstants(SpecializationConstantsInfo specConstants) noexcept;

    /**
     * 推送常量结构
     * 
     * 描述推送常量的信息（运行时常量，直接推送到着色器）。
     * 
     * 组成：
     * - name: 常量名称（在着色器中的名称）
     * - type: 常量类型（INT、FLOAT、BOOL）
     */
    struct PushConstant {
        utils::CString name;    // 常量名称
        ConstantType type;      // 常量类型
    };

    /**
     * 设置推送常量
     * 
     * 设置指定着色器阶段的推送常量列表。
     * 
     * @param stage    着色器阶段（VERTEX、FRAGMENT、COMPUTE）
     * @param constants 推送常量列表
     *                 - 每个常量包含名称和类型
     *                 - 用于运行时直接推送常量值到着色器
     * @return 当前对象的引用
     * 
     * 用途：
     * - 传递频繁变化的小量数据
     * - 避免更新 uniform 缓冲区的开销
     * - 在 Vulkan 后端中使用推送常量
     */
    Program& pushConstants(ShaderStage stage,
            utils::FixedCapacityVector<PushConstant> constants) noexcept;

    /**
     * 设置缓存 ID
     * 
     * 设置着色器程序的缓存 ID，用于着色器编译缓存。
     * 
     * @param cacheId 缓存 ID（64 位整数）
     *                - 用于标识和查找缓存的着色器
     *                - 相同 cacheId 的程序可以重用编译结果
     * @return 当前对象的引用
     * 
     * 用途：
     * - 加速着色器编译（重用已编译的着色器）
     * - 减少重复编译的开销
     */
    Program& cacheId(uint64_t cacheId) noexcept;

    /**
     * 设置多视图支持
     * 
     * 启用或禁用多视图（multiview）渲染支持。
     * 
     * @param multiview 如果为 true，启用多视图支持
     *                 - 用于立体渲染（VR/AR）
     *                 - 允许一次渲染到多个视图
     * @return 当前对象的引用
     * 
     * 注意：
     * - 仅在引擎初始化时启用了多视图时才有效
     * - 需要后端支持多视图扩展
     */
    Program& multiview(bool multiview) noexcept;

    /**
     * 设置 Uniform 信息（仅用于 ES2 支持）
     * 
     * 设置 OpenGL ES 2.0 兼容模式下的 uniform 变量信息。
     * 
     * @param index   绑定索引
     * @param name    绑定名称
     * @param uniforms Uniform 信息列表
     * @return 当前对象的引用
     * 
     * 注意：
     * - 仅用于 OpenGL ES 2.0 兼容模式
     * - 现代后端（ES3+、Vulkan、Metal）不需要此方法
     */
    // For ES2 support only...
    Program& uniforms(uint32_t index, utils::CString name, UniformInfo uniforms);
    
    /**
     * 设置顶点属性信息（仅用于 ES2 支持）
     * 
     * 设置 OpenGL ES 2.0 兼容模式下的顶点属性信息。
     * 
     * @param attributes 属性信息列表（名称和位置对）
     * @return 当前对象的引用
     * 
     * 注意：
     * - 仅用于 OpenGL ES 2.0 兼容模式
     * - 现代后端使用描述符绑定
     */
    Program& attributes(AttributesInfo attributes) noexcept;

    //
    // Getters for program construction...
    //

    ShaderSource const& getShadersSource() const noexcept { return mShadersSource; }
    ShaderSource& getShadersSource() noexcept { return mShadersSource; }

    utils::CString const& getName() const noexcept { return mName; }
    utils::CString& getName() noexcept { return mName; }

    auto const& getShaderLanguage() const { return mShaderLanguage; }

    uint64_t getCacheId() const noexcept { return mCacheId; }

    bool isMultiview() const noexcept { return mMultiview; }

    CompilerPriorityQueue getPriorityQueue() const noexcept { return mPriorityQueue; }

    SpecializationConstantsInfo const& getSpecializationConstants() const noexcept {
        return mSpecializationConstants;
    }

    SpecializationConstantsInfo& getSpecializationConstants() noexcept {
        return mSpecializationConstants;
    }

    DescriptorSetInfo& getDescriptorBindings() noexcept {
        return mDescriptorBindings;
    }

    utils::FixedCapacityVector<PushConstant> const& getPushConstants(
            ShaderStage stage) const noexcept {
        return mPushConstants[static_cast<uint8_t>(stage)];
    }

    utils::FixedCapacityVector<PushConstant>& getPushConstants(ShaderStage stage) noexcept {
        return mPushConstants[static_cast<uint8_t>(stage)];
    }

    auto const& getBindingUniformInfo() const { return mBindingUniformsInfo; }
    auto& getBindingUniformInfo() { return mBindingUniformsInfo; }

    auto const& getAttributes() const { return mAttributes; }
    auto& getAttributes() { return mAttributes; }

private:
    friend utils::io::ostream& operator<<(utils::io::ostream& out, const Program& builder);

    ShaderSource mShadersSource;
    ShaderLanguage mShaderLanguage = ShaderLanguage::ESSL3;
    utils::CString mName;
    uint64_t mCacheId{};
    CompilerPriorityQueue mPriorityQueue = CompilerPriorityQueue::HIGH;
    utils::Invocable<utils::io::ostream&(utils::CString const& name, utils::io::ostream& out)>
            mLogger;
    SpecializationConstantsInfo mSpecializationConstants;
    std::array<utils::FixedCapacityVector<PushConstant>, SHADER_TYPE_COUNT> mPushConstants;
    DescriptorSetInfo mDescriptorBindings;

    // For ES2 support only
    AttributesInfo mAttributes;
    BindingUniformsInfo mBindingUniformsInfo;

    // Indicates the current engine was initialized with multiview stereo, and the variant for this
    // program contains STE flag. This will be referred later for the OpenGL shader compiler to
    // determine whether shader code replacement for the num_views should be performed.
    // This variable could be promoted as a more generic variable later if other similar needs occur.
    bool mMultiview = false;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_PROGRAM_H
