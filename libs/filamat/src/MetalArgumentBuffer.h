/*
* Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_METALARGUMENTBUFFER_H
#define TNT_METALARGUMENTBUFFER_H

#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include <backend/DriverEnums.h>

namespace filamat {

// Metal参数缓冲区类：用于生成Metal着色器语言（MSL）的参数缓冲区结构定义
class MetalArgumentBuffer {
public:

    // 构建器类：用于构建Metal参数缓冲区
    class Builder {
    public:

        /**
         * Set the name of the argument buffer structure.
         */
        // 设置参数缓冲区结构的名称
        // @param name 结构名称
        // @return 构建器引用（支持链式调用）
        Builder& name(const std::string& name) noexcept;

        /**
         * Add a texture argument to the argument buffer structure.
         * All combinations of type/format are supported, except for SAMPLER_3D/SHADOW.
         *
         * @param index the [[id(n)]] index of the texture argument
         * @param name the name of the texture argument
         * @param type controls the texture data type, e.g., texture2d, texturecube, etc
         * @param format controls the data format of the texture, e.g., int, float, etc
         */
        // 添加纹理参数到参数缓冲区结构
        // 支持所有type/format组合，除了SAMPLER_3D/SHADOW
        // @param index 纹理参数的[[id(n)]]索引
        // @param name 纹理参数名称
        // @param type 纹理数据类型（如texture2d、texturecube等）
        // @param format 纹理数据格式（如int、float等）
        // @param multisample 是否为多重采样纹理
        // @return 构建器引用（支持链式调用）
        Builder& texture(size_t index, const std::string& name,
                filament::backend::SamplerType type,
                filament::backend::SamplerFormat format,
                bool multisample) noexcept;

        /**
         * Add a sampler argument to the argument buffer structure.
         * @param index the [[id(n)]] index of the texture argument
         * @param name the name of the texture argument
         */
        // 添加采样器参数到参数缓冲区结构
        // @param index 采样器参数的[[id(n)]]索引
        // @param name 采样器参数名称
        // @return 构建器引用（支持链式调用）
        Builder& sampler(size_t index, const std::string& name) noexcept;

        /**
         * Add a buffer argument to the argument buffer structure.
         * @param index the [[id(n)]] index of the buffer argument
         * @param type the type of data the buffer points to
         * @param name the name of the buffer argument
         */
        // 添加缓冲区参数到参数缓冲区结构
        // @param index 缓冲区参数的[[id(n)]]索引
        // @param type 缓冲区指向的数据类型
        // @param name 缓冲区参数名称
        // @return 构建器引用（支持链式调用）
        Builder& buffer(size_t index, const std::string& type, const std::string& name) noexcept;

        // 构建MetalArgumentBuffer对象
        // @return 新创建的MetalArgumentBuffer指针
        MetalArgumentBuffer* build();

        friend class MetalArgumentBuffer;

    private:
        std::string mName;

        struct TextureArgument {
            std::string name;
            size_t index;
            filament::backend::SamplerType type;
            filament::backend::SamplerFormat format;
            bool multisample;

            std::ostream& write(std::ostream& os) const;
        };

        struct SamplerArgument {
            std::string name;
            size_t index;

            std::ostream& write(std::ostream& os) const;
        };

        struct BufferArgument {
            std::string name;
            size_t index;
            std::string type;

            std::ostream& write(std::ostream& os) const;
        };

        using ArgumentType = std::variant<TextureArgument, SamplerArgument, BufferArgument>;
        std::vector<ArgumentType> mArguments;
    };

    // 销毁MetalArgumentBuffer对象
    // @param argumentBuffer 指向MetalArgumentBuffer指针的指针（将被设置为nullptr）
    static void destroy(MetalArgumentBuffer** argumentBuffer);

    // 获取参数缓冲区名称
    // @return 参数缓冲区名称
    const std::string& getName() const noexcept { return mName; }

    /**
     * Returns the generated MSL argument buffer definition.
     */
    // 返回生成的MSL参数缓冲区定义
    // @return MSL参数缓冲区定义字符串
    const std::string& getMsl() const noexcept { return mShaderText; }

    /**
     * Searches shader for the target argument buffer, and replaces it with the replacement string.
     *
     * @param shader the source MSL shader that contains the target argument buffer definition
     * @param targetArgBufferName the name of the argument buffer definition to replace
     * @param replacement the replacement argument buffer definition
     * @return true if the target was found, false otherwise
     */
    // 在着色器中搜索目标参数缓冲区，并用替换字符串替换它
    // @param shader 包含目标参数缓冲区定义的源MSL着色器（将被修改）
    // @param targetArgBufferName 要替换的参数缓冲区定义名称
    // @param replacement 替换的参数缓冲区定义
    // @return 如果找到目标返回true，否则返回false
    static bool replaceInShader(std::string& shader, const std::string& targetArgBufferName,
            const std::string& replacement) noexcept;
private:

    MetalArgumentBuffer(Builder& builder);
    ~MetalArgumentBuffer() = default;

    std::string mName;
    std::string mShaderText;
};

} // namespace filamat

#endif  // TNT_METALARGUMENTBUFFER_H
