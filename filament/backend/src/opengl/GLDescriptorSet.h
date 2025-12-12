/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSET_H
#define TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSET_H

// 驱动基类
#include "DriverBase.h"

// OpenGL 头文件
#include "gl_headers.h"

// Handle 分配器
#include <private/backend/HandleAllocator.h>

// 后端枚举和句柄
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

// Utils 工具库
#include <utils/bitset.h>                    // 位集合
#include <utils/FixedCapacityVector.h>      // 固定容量向量

// 数学库
#include <math/half.h>                      // 半精度浮点数

// 标准库
#include <array>                            // 数组
#include <variant>                          // 变体类型

#include <stddef.h>                         // 标准定义
#include <stdint.h>                         // 标准整数类型

namespace filament::backend {

struct GLBufferObject;
struct GLTexture;
struct GLTextureRef;
struct GLDescriptorSetLayout;
class OpenGLProgram;
class OpenGLContext;
class OpenGLDriver;

/**
 * OpenGL 描述符集结构
 * 
 * 封装 OpenGL 描述符集，管理着色器资源的绑定（Uniform 缓冲区、存储缓冲区、采样器等）。
 * 
 * 主要功能：
 * 1. 存储描述符绑定信息（缓冲区、纹理、采样器等）
 * 2. 更新描述符绑定（update 方法）
 * 3. 绑定描述符集到命令缓冲区（bind 方法）
 * 4. 验证描述符集与程序布局的匹配（validate 方法）
 * 
 * 设计特点：
 * - 使用 std::variant 存储不同类型的描述符（Buffer、DynamicBuffer、Sampler 等）
 * - 支持动态偏移缓冲区（DynamicBuffer）
 * - ES2 特殊处理（BufferGLES2、SamplerGLES2）
 * - 支持各向异性过滤工作区（SamplerWithAnisotropyWorkaround）
 * 
 * 性能优化：
 * - 只绑定程序实际使用的描述符（通过 activeDescriptorBindings）
 * - 支持仅更新动态偏移（offsetsOnly 模式）
 * - 使用位集合跟踪动态缓冲区
 */
struct GLDescriptorSet : public HwDescriptorSet {

    using HwDescriptorSet::HwDescriptorSet;

    /**
     * 构造函数
     * 
     * 从描述符集布局创建描述符集，初始化所有描述符。
     * 
     * @param gl OpenGLContext 引用，用于检查 ES2
     * @param dslh 描述符集布局句柄
     * @param layout 描述符集布局指针
     * 
     * 执行流程：
     * 1. 根据布局分配描述符数组
     * 2. 为每个绑定初始化对应的描述符类型
     * 3. 根据描述符类型和 ES2 支持选择适当的变体类型
     */
    GLDescriptorSet(OpenGLContext& gl, DescriptorSetLayoutHandle dslh,
            GLDescriptorSetLayout const* layout);

    /**
     * 更新缓冲区描述符
     * 
     * 更新描述符集中的缓冲区绑定（Uniform 缓冲区或存储缓冲区）。
     * 
     * @param gl OpenGLContext 引用（当前未使用）
     * @param binding 绑定索引
     * @param bo 缓冲区对象指针（可为 nullptr）
     * @param offset 缓冲区偏移量（字节）
     * @param size 缓冲区大小（字节）
     */
    void update(OpenGLContext& gl,
            descriptor_binding_t binding, GLBufferObject* bo, size_t offset, size_t size) noexcept;

    /**
     * 更新采样器描述符
     * 
     * 更新描述符集中的纹理和采样器绑定。
     * 
     * @param gl OpenGLContext 引用，用于获取采样器对象
     * @param handleAllocator Handle 分配器，用于获取纹理对象
     * @param binding 绑定索引
     * @param th 纹理句柄（可为空句柄）
     * @param params 采样器参数
     * 
     * 特殊处理：
     * - 外部纹理：强制使用 CLAMP_TO_EDGE 环绕模式
     * - 深度纹理：限制过滤模式（不能使用线性过滤）
     * - 各向异性过滤工作区：某些驱动需要在纹理上设置各向异性
     */
    void update(OpenGLContext& gl, HandleAllocatorGL& handleAllocator,
            descriptor_binding_t binding, TextureHandle th, SamplerParams params) noexcept;

    /**
     * 绑定描述符集
     * 
     * 概念上将描述符集绑定到命令缓冲区。
     * 实际执行所有描述符的 OpenGL 绑定操作。
     * 
     * @param gl OpenGLContext 引用，用于绑定缓冲区和纹理
     * @param handleAllocator Handle 分配器，用于获取资源对象
     * @param p OpenGLProgram 引用，用于获取绑定点和纹理单元
     * @param set 描述符集索引
     * @param offsets 动态偏移数组（用于动态缓冲区）
     * @param offsetsOnly 是否仅更新动态偏移（true 时只绑定动态缓冲区）
     * 
     * 执行流程：
     * 1. 获取程序的活动描述符绑定
     * 2. 如果 offsetsOnly，只处理动态缓冲区
     * 3. 遍历活动绑定，根据描述符类型执行绑定：
     *    - Buffer：使用静态偏移绑定缓冲区范围
     *    - DynamicBuffer：使用静态偏移 + 动态偏移绑定缓冲区范围
     *    - BufferGLES2：更新 Uniform（ES2 模拟 UBO）
     *    - Sampler：绑定纹理和采样器对象
     *    - SamplerWithAnisotropyWorkaround：绑定纹理和采样器，并在纹理上设置各向异性
     *    - SamplerGLES2：绑定纹理并在纹理上设置采样器参数（ES2）
     * 4. 处理纹理视图（baseLevel、maxLevel、swizzle）
     */
    void bind(
            OpenGLContext& gl,
            HandleAllocatorGL& handleAllocator,
            OpenGLProgram const& p,
            descriptor_set_t set, uint32_t const* offsets, bool offsetsOnly) const noexcept;

    /**
     * 获取动态缓冲区数量
     * 
     * 返回描述符集中动态偏移缓冲区的数量。
     * 
     * @return 动态缓冲区数量
     */
    uint32_t getDynamicBufferCount() const noexcept {
        return dynamicBufferCount;
    }

    /**
     * 验证描述符集
     * 
     * 验证描述符集布局是否与管线布局匹配。
     * 
     * @param allocator Handle 分配器，用于获取布局对象
     * @param pipelineLayout 管线布局句柄
     * 
     * 验证内容：
     * - 绑定类型（type）
     * - 阶段标志（stageFlags）
     * - 绑定索引（binding）
     * - 标志（flags）
     * - 数量（count）
     */
    void validate(HandleAllocatorGL& allocator, DescriptorSetLayoutHandle pipelineLayout) const;

private:
    /**
     * 缓冲区描述符（静态偏移）
     * 
     * 用于 SSBO 或 UBO，具有静态偏移。
     * 偏移量在创建时确定，不会在每次绘制时改变。
     */
    struct Buffer {
        // 工作区：我们不能将以下定义为 Buffer() = default，因为某些客户端的编译器
        // 会将此声明（可能与 explicit 结合）视为已删除的构造函数。
        Buffer() {}

        explicit Buffer(GLenum target) noexcept : target(target) {}
        GLenum target;                          // 缓冲区目标（GL_UNIFORM_BUFFER、GL_SHADER_STORAGE_BUFFER）：4 字节
        GLuint id = 0;                          // OpenGL 缓冲区对象 ID：4 字节
        uint32_t offset = 0;                    // 缓冲区偏移量（字节）：4 字节
        uint32_t size = 0;                      // 缓冲区大小（字节）：4 字节
    };

    /**
     * 缓冲区描述符（动态偏移）
     * 
     * 用于 SSBO 或 UBO，具有动态偏移。
     * 偏移量在每次绘制时通过 offsets 数组提供。
     */
    struct DynamicBuffer {
        DynamicBuffer() = default;
        explicit DynamicBuffer(GLenum target) noexcept : target(target) { }
        GLenum target;                          // 缓冲区目标：4 字节
        GLuint id = 0;                          // OpenGL 缓冲区对象 ID：4 字节
        uint32_t offset = 0;                    // 静态偏移量（字节）：4 字节
        uint32_t size = 0;                      // 缓冲区大小（字节）：4 字节
    };

    /**
     * UBO 描述符（ES2）
     * 
     * 用于 ES2，模拟 UBO 功能。
     * ES2 不支持 UBO，需要手动更新每个 Uniform。
     */
    struct BufferGLES2 {
        BufferGLES2() = default;
        explicit BufferGLES2(bool dynamicOffset) noexcept : dynamicOffset(dynamicOffset) { }
        GLBufferObject const* bo = nullptr;     // 缓冲区对象指针：8 字节
        uint32_t offset = 0;                    // 偏移量（字节）：4 字节
        bool dynamicOffset = false;             // 是否为动态偏移：4 字节
    };

    /**
     * 采样器描述符
     * 
     * 用于纹理和采样器绑定（ES 3.0+）。
     */
    struct Sampler {
        TextureHandle handle;                   // 纹理句柄：4 字节
        GLuint sampler = 0;                     // 采样器对象 ID：4 字节
    };

    /**
     * 采样器描述符（各向异性过滤工作区）
     * 
     * 用于需要各向异性过滤工作区的驱动。
     * 某些驱动在采样器上设置各向异性过滤会失败，需要在纹理上设置。
     */
    struct SamplerWithAnisotropyWorkaround {
        TextureHandle handle;                   // 纹理句柄：4 字节
        GLuint sampler = 0;                     // 采样器对象 ID：4 字节
        math::half anisotropy = 1.0f;           // 各向异性过滤级别：2 字节
    };

    /**
     * 采样器描述符（ES2）
     * 
     * 用于 ES2，ES2 不支持采样器对象，需要在纹理上设置采样器参数。
     */
    struct SamplerGLES2 {
        TextureHandle handle;                   // 纹理句柄：4 字节
        SamplerParams params{};                 // 采样器参数：4 字节
        float anisotropy = 1.0f;                // 各向异性过滤级别：4 字节
    };
    
    /**
     * 描述符变体
     * 
     * 使用 std::variant 存储不同类型的描述符。
     * 允许在运行时根据描述符类型选择适当的处理方式。
     */
    struct Descriptor {
        std::variant<
                Buffer,                         // 静态偏移缓冲区
                DynamicBuffer,                  // 动态偏移缓冲区
                BufferGLES2,                    // ES2 缓冲区
                Sampler,                        // 采样器
                SamplerWithAnisotropyWorkaround,// 采样器（各向异性工作区）
                SamplerGLES2> desc;             // ES2 采样器
    };
    static_assert(sizeof(Descriptor) <= 32);  // 确保描述符大小不超过 32 字节

    /**
     * 更新纹理视图
     * 
     * 更新纹理的视图参数（baseLevel、maxLevel、swizzle）。
     * 当纹理有视图（View）时，需要同步视图参数。
     * 
     * @param gl OpenGLContext 引用
     * @param handleAllocator Handle 分配器
     * @param unit 纹理单元索引
     * @param t 纹理对象指针
     */
    static void updateTextureView(OpenGLContext& gl,
            HandleAllocatorGL& handleAllocator, GLuint unit, GLTexture const* t) noexcept;

    utils::FixedCapacityVector<Descriptor> descriptors;     // 描述符数组：16 字节
    utils::bitset64 dynamicBuffers;                         // 动态缓冲区位集合：8 字节
    DescriptorSetLayoutHandle dslh;                         // 描述符集布局句柄：4 字节
    uint8_t dynamicBufferCount = 0;                         // 动态缓冲区数量：1 字节
};
static_assert(sizeof(GLDescriptorSet) <= 32);  // 确保描述符集大小不超过 32 字节

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_GLDESCRIPTORSET_H
