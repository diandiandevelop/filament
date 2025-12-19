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

#ifndef TNT_FILAMENT_SAMPLERINTERFACEBLOCK_H
#define TNT_FILAMENT_SAMPLERINTERFACEBLOCK_H


#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/ImmutableCString.h>

#include <private/filament/DescriptorSets.h>

#include <initializer_list>
#include <unordered_map>
#include <string_view>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace filament {

/**
 * 采样器接口块类
 * 用于定义和管理着色器中使用的采样器（纹理采样器）的结构
 */
class SamplerInterfaceBlock {
public:
    // 默认构造函数
    SamplerInterfaceBlock();

    // 禁止拷贝构造
    SamplerInterfaceBlock(const SamplerInterfaceBlock& rhs) = delete;
    // 移动构造函数
    SamplerInterfaceBlock(SamplerInterfaceBlock&& rhs) noexcept;

    // 禁止拷贝赋值
    SamplerInterfaceBlock& operator=(const SamplerInterfaceBlock& rhs) = delete;
    // 移动赋值运算符
    SamplerInterfaceBlock& operator=(SamplerInterfaceBlock&& rhs) noexcept;

    // 析构函数
    ~SamplerInterfaceBlock() noexcept;

    using Type = backend::SamplerType;
    using Format = backend::SamplerFormat;
    using Precision = backend::Precision;
    using SamplerParams = backend::SamplerParams;
    using Binding = backend::descriptor_binding_t;
    using ShaderStageFlags = backend::ShaderStageFlags;

    /**
     * 采样器信息结构
     * 描述一个采样器的详细信息
     */
    struct SamplerInfo { // NOLINT(cppcoreguidelines-pro-type-member-init)
        utils::CString name;                    // name of this sampler - 采样器名称
        utils::CString uniformName;             // name of the uniform holding this - 保存此采样器的uniform名称（GLSL/MSL需要）
                                                // sampler (needed for glsl/MSL)
        Binding binding;                        // binding in the descriptor set - 在描述符集中的绑定索引
        Type type;                              // type of this sampler - 采样器类型
        Format format;                          // format of this sampler - 采样器格式
        Precision precision;                    // precision of this sampler - 采样器精度
        bool filterable;                        // whether the sampling should be filterable. - 采样是否可过滤
        bool multisample;                       // multisample capable - 是否支持多重采样
        ShaderStageFlags stages;                // stages the sampler can be accessed from - 可以访问此采样器的着色器阶段
        utils::ImmutableCString transformName;  // name of the uniform holding the transform - 保存此采样器变换矩阵的uniform名称
                                                // matrix for this sampler
    };

    using SamplerInfoList = utils::FixedCapacityVector<SamplerInfo>;

    /**
     * 构建器类
     * 用于构建SamplerInterfaceBlock对象
     */
    class Builder {
    public:
        Builder();
        ~Builder() noexcept;

        Builder(Builder const& rhs) = default;
        Builder(Builder&& rhs) noexcept = default;
        Builder& operator=(Builder const& rhs) = default;
        Builder& operator=(Builder&& rhs) noexcept = default;

        /**
         * 列表条目结构
         * 用于批量添加采样器的临时结构
         */
        struct ListEntry { // NOLINT(cppcoreguidelines-pro-type-member-init)
            std::string_view name;          // name of this sampler - 采样器名称
            Binding binding;                // binding in the descriptor set - 在描述符集中的绑定索引
            Type type;                      // type of this sampler - 采样器类型
            Format format;                  // format of this sampler - 采样器格式
            Precision precision;            // precision of this sampler - 采样器精度
            bool filterable;                // whether the sampling should be filterable. - 采样是否可过滤
            bool multisample;               // multisample capable - 是否支持多重采样
            ShaderStageFlags stages;        // shader stages using this sampler - 使用此采样器的着色器阶段
            std::string_view transformName; // name of the uniform holding the transform matrix for - 保存此采样器变换矩阵的uniform名称
                                            // this sampler
        };

        // Give a name to this sampler interface block - 为此采样器接口块命名
        Builder& name(std::string_view interfaceBlockName);

        // 设置着色器阶段标志（指定哪些着色器阶段可以使用这些采样器）
        Builder& stageFlags(backend::ShaderStageFlags stageFlags);

        /**
         * Add a sampler - 添加一个采样器
         * @param samplerName 采样器名称
         * @param binding 绑定索引
         * @param type 采样器类型
         * @param format 采样器格式
         * @param precision 精度（默认MEDIUM）
         * @param filterable 是否可过滤（默认true）
         * @param multisample 是否支持多重采样（默认false）
         * @param transformName 变换矩阵uniform名称（可选）
         * @param stages 着色器阶段标志（默认所有阶段）
         */
        Builder& add(std::string_view samplerName, Binding binding, Type type, Format format,
                Precision precision = Precision::MEDIUM, bool filterable = true,
                bool multisample = false, std::string_view transformName = "",
                ShaderStageFlags stages = ShaderStageFlags::ALL_SHADER_STAGE_FLAGS) noexcept;

        // Add multiple samplers - 添加多个采样器
        Builder& add(std::initializer_list<ListEntry> list) noexcept;

        // build and return the SamplerInterfaceBlock - 构建并返回SamplerInterfaceBlock对象
        SamplerInterfaceBlock build();
    private:
        friend class SamplerInterfaceBlock;
        utils::CString mName;                          // 接口块名称
        backend::ShaderStageFlags mStageFlags = backend::ShaderStageFlags::ALL_SHADER_STAGE_FLAGS;  // 着色器阶段标志（默认所有阶段）
        std::vector<SamplerInfo> mEntries;             // 采样器条目列表
    };

    // name of this sampler interface block - 获取此采样器接口块的名称
    const utils::CString& getName() const noexcept { return mName; }

    // 获取着色器阶段标志
    backend::ShaderStageFlags getStageFlags() const noexcept { return mStageFlags; }

    // size needed to store the samplers described by this interface block in a SamplerGroup - 获取在SamplerGroup中存储此接口块描述的采样器所需的大小
    size_t getSize() const noexcept { return mSamplersInfoList.size(); }

    // list of information records for each sampler - 获取每个采样器的信息记录列表
    SamplerInfoList const& getSamplerInfoList() const noexcept {
        return mSamplersInfoList;
    }

    // information record for sampler of the given name - 获取指定名称的采样器信息记录
    SamplerInfo const* getSamplerInfo(std::string_view name) const;

    // 检查是否存在指定名称的采样器
    bool hasSampler(std::string_view name) const noexcept {
        return mInfoMap.find(name) != mInfoMap.end();
    }

    // 检查是否为空（没有采样器）
    bool isEmpty() const noexcept { return mSamplersInfoList.empty(); }

    // 生成uniform名称（用于GLSL/MSL）
    static utils::CString generateUniformName(const char* group, const char* sampler) noexcept;

    // 过滤采样器列表，只保留在描述符集布局中存在的采样器
    static SamplerInfoList filterSamplerList(SamplerInfoList list,
            backend::DescriptorSetLayout const& descriptorSetLayout);

private:
    friend class Builder;

    // 从构建器构造（私有构造函数，只能通过Builder构建）
    explicit SamplerInterfaceBlock(Builder const& builder) noexcept;

    utils::CString mName;                                  // 接口块名称
    // It's needed to check if MAX_SAMPLER_COUNT is exceeded. - 需要检查是否超过MAX_SAMPLER_COUNT
    backend::ShaderStageFlags mStageFlags{};
    utils::FixedCapacityVector<SamplerInfo> mSamplersInfoList;  // 采样器信息列表
    std::unordered_map<std::string_view, uint32_t> mInfoMap;    // 采样器名称到索引的映射
};

} // namespace filament

#endif // TNT_FILAMENT_SAMPLERINTERFACEBLOCK_H
