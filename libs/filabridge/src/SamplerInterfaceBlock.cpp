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

#include "private/filament/SamplerInterfaceBlock.h"

#include <private/filament/DescriptorSets.h>

#include <backend/DriverEnums.h>

#include <utils/Panic.h>

#include <initializer_list>
#include <iterator>
#include <string_view>
#include <utility>

#include <stddef.h>
#include <stdint.h>

using namespace utils;

namespace filament {

// Builder默认构造函数
SamplerInterfaceBlock::Builder::Builder() = default;
// Builder析构函数
SamplerInterfaceBlock::Builder::~Builder() noexcept = default;

// 设置接口块名称
SamplerInterfaceBlock::Builder&
SamplerInterfaceBlock::Builder::name(std::string_view interfaceBlockName) {
    mName = { interfaceBlockName.data(), interfaceBlockName.size() };
    return *this;
}

// 设置着色器阶段标志
SamplerInterfaceBlock::Builder&
SamplerInterfaceBlock::Builder::stageFlags(backend::ShaderStageFlags stageFlags) {
    mStageFlags = stageFlags;
    return *this;
}

// 添加一个采样器
SamplerInterfaceBlock::Builder& SamplerInterfaceBlock::Builder::add(std::string_view samplerName,
        Binding binding, Type type, Format format, Precision precision, bool filterable,
        bool multisample, std::string_view transformName, ShaderStageFlags stages) noexcept {
    // 创建采样器信息并添加到条目列表
    mEntries.push_back({
        { samplerName.data(), samplerName.size() }, // name - 采样器名称
        {},                                         // uniform name - uniform名称（稍后生成）
        binding,        // 绑定索引
        type,          // 采样器类型
        format,        // 采样器格式
        precision,     // 精度
        filterable,    // 是否可过滤
        multisample,   // 是否支持多重采样
        stages,        // 着色器阶段标志
        { transformName.data(), transformName.size() },  // 变换矩阵名称
    });
    return *this;
}

// 构建SamplerInterfaceBlock对象
SamplerInterfaceBlock SamplerInterfaceBlock::Builder::build() {
    return SamplerInterfaceBlock(*this);
}

// 添加多个采样器
SamplerInterfaceBlock::Builder& SamplerInterfaceBlock::Builder::add(
        std::initializer_list<ListEntry> list) noexcept {
    // 遍历列表，逐个添加采样器
    for (auto& e: list) {
        add(e.name, e.binding, e.type, e.format, e.precision, e.filterable,
                e.multisample, e.transformName, e.stages);
    }
    return *this;
}

// -------------------------------------------------------------------------------------------------

// 默认构造函数
SamplerInterfaceBlock::SamplerInterfaceBlock() = default;
// 移动构造函数
SamplerInterfaceBlock::SamplerInterfaceBlock(SamplerInterfaceBlock&& rhs) noexcept = default;
// 移动赋值运算符
SamplerInterfaceBlock& SamplerInterfaceBlock::operator=(SamplerInterfaceBlock&& rhs) noexcept = default;
// 析构函数
SamplerInterfaceBlock::~SamplerInterfaceBlock() noexcept = default;

// 从Builder构造SamplerInterfaceBlock（验证格式和可过滤性组合，生成uniform名称）
SamplerInterfaceBlock::SamplerInterfaceBlock(Builder const& builder) noexcept
    : mName(builder.mName), mStageFlags(builder.mStageFlags),
    mSamplersInfoList(builder.mEntries.size())
{
    auto& infoMap = mInfoMap;
    // 预留映射表空间
    infoMap.reserve(builder.mEntries.size());

    auto& samplersInfoList = mSamplersInfoList;

    // 遍历所有条目，验证并构建采样器信息
    for (auto const& e : builder.mEntries) {
        size_t const i = std::distance(builder.mEntries.data(), &e);
        SamplerInfo& info = samplersInfoList[i];

        // We verify the following assumption. - 验证以下假设：
        //   - float sampler can be filterable or not, default to filterable - float采样器可以过滤或不过滤，默认可过滤
        //   - int sampler is not filterable - int采样器不可过滤
        //   - shadow sampler uses comparison operator and should be filterable. - shadow采样器使用比较操作符，应该是可过滤的
        FILAMENT_CHECK_PRECONDITION(
                ((info.format == Format::INT || info.format == Format::UINT) && !info.filterable) ||
                (info.format == Format::SHADOW && info.filterable) ||
                (info.format == Format::FLOAT))
                << "Format and filterable flag combination not allowed. "
                << "format=" << (int) info.format << " filterable=" << info.filterable;

        // 复制条目信息
        info = e;
        // 应用阶段标志过滤（只保留允许的阶段）
        info.stages &= builder.mStageFlags;
        // 生成uniform名称（GLSL/MSL需要）
        info.uniformName = generateUniformName(mName.c_str(), e.name.c_str());
        // 记录名称到索引的映射
        // info.name.c_str() guaranteed constant - info.name.c_str()保证为常量
        infoMap[{ info.name.data(), info.name.size() }] = i;
    }
}

// 获取指定名称的采样器信息
const SamplerInterfaceBlock::SamplerInfo* SamplerInterfaceBlock::getSamplerInfo(
        std::string_view name) const {
    // 在映射表中查找采样器名称
    auto pos = mInfoMap.find(name);
    // 如果未找到，抛出异常或触发断言
    FILAMENT_CHECK_PRECONDITION(pos != mInfoMap.end()) << "sampler named \"" << name << "\" not found";
    // 返回对应的采样器信息
    return &mSamplersInfoList[pos->second];
}

// 生成uniform名称（格式：groupName_samplerName，首字母小写）
CString SamplerInterfaceBlock::generateUniformName(const char* group, const char* sampler) noexcept {
    char uniformName[256];

    // sampler interface block name - 复制采样器接口块名称
    char* const prefix = std::copy_n(group,
            std::min(sizeof(uniformName) / 2, strlen(group)), uniformName);
    // 将首字母转换为小写（简单的方法）
    if (uniformName[0] >= 'A' && uniformName[0] <= 'Z') {
        uniformName[0] |= 0x20; // poor man's tolower() - 简单的小写转换
    }
    // 添加下划线分隔符
    *prefix = '_';

    // 复制采样器名称
    char* last = std::copy_n(sampler,
            std::min(sizeof(uniformName) / 2 - 2, strlen(sampler)),
            prefix + 1);
    *last++ = 0; // null terminator - 添加空终止符
    assert_invariant(last <= std::end(uniformName));

    // 返回CString（不包括空终止符）
    return CString{ uniformName, size_t(last - uniformName) - 1u };
}

// 过滤采样器列表，只保留在描述符集布局中存在的采样器
SamplerInterfaceBlock::SamplerInfoList SamplerInterfaceBlock::filterSamplerList(
        SamplerInfoList list, backend::DescriptorSetLayout const& descriptorSetLayout) {
    // remove all the samplers that are not included in the descriptor-set layout - 移除所有不在描述符集布局中的采样器
    list.erase(
            std::remove_if(list.begin(), list.end(),
                    [&](auto const& entry) {
                        // 在描述符集布局的绑定中查找匹配的绑定索引
                        auto pos = std::find_if(
                                descriptorSetLayout.bindings.begin(),
                                descriptorSetLayout.bindings.end(),
                                [&entry](const auto& item) {
                                    return item.binding == entry.binding;
                                });
                        // 如果未找到，则移除该采样器
                        return pos == descriptorSetLayout.bindings.end();
                    }), list.end());

    return list;
}

} // namespace filament
