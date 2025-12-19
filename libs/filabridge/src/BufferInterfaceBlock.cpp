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

#include "private/filament/BufferInterfaceBlock.h"

#include <utils/Panic.h>
#include <utils/compiler.h>

#include <utility>

using namespace utils;

namespace filament {

// Builder默认构造函数
BufferInterfaceBlock::Builder::Builder() noexcept = default;
// Builder析构函数
BufferInterfaceBlock::Builder::~Builder() noexcept = default;

// 设置接口块名称
BufferInterfaceBlock::Builder&
BufferInterfaceBlock::Builder::name(std::string_view interfaceBlockName) {
    mName = { interfaceBlockName.data(), interfaceBlockName.size() };
    return *this;
}

// 设置对齐方式
BufferInterfaceBlock::Builder& BufferInterfaceBlock::Builder::alignment(
        BufferInterfaceBlock::Alignment alignment) {
    mAlignment = alignment;
    return *this;
}

// 设置缓冲区目标类型
BufferInterfaceBlock::Builder& BufferInterfaceBlock::Builder::target(
        BufferInterfaceBlock::Target target) {
    mTarget = target;
    return *this;
}

// 添加限定符（使用位或运算组合多个限定符）
BufferInterfaceBlock::Builder& BufferInterfaceBlock::Builder::qualifier(
        BufferInterfaceBlock::Qualifier qualifier) {
    mQualifiers |= uint8_t(qualifier);
    return *this;
}

// 添加字段列表
BufferInterfaceBlock::Builder& BufferInterfaceBlock::Builder::add(
        std::initializer_list<InterfaceBlockEntry> list) {
    // 预留足够的空间以容纳新条目
    mEntries.reserve(mEntries.size() + list.size());
    // 遍历列表，将每个条目转换为FieldInfo并添加到条目列表
    for (auto const& item : list) {
        mEntries.push_back({
                { item.name.data(), item.name.size() },
                0, uint8_t(item.stride), item.type, item.size > 0, item.size, item.precision, item.associatedSampler, item.minFeatureLevel,
                { item.structName.data(), item.structName.size() },
                { item.sizeName.data(), item.sizeName.size() }
        });
    }
    return *this;
}

// 添加可变大小数组（必须是最后一个条目）
BufferInterfaceBlock::Builder& BufferInterfaceBlock::Builder::addVariableSizedArray(
        BufferInterfaceBlock::InterfaceBlockEntry const& item) {
    // 标记包含可变大小数组
    mHasVariableSizeArray = true;
    // 添加条目（isArray=true, size=0表示可变大小）
    mEntries.push_back({
            { item.name.data(), item.name.size() },
            0, uint8_t(item.stride), item.type, true, 0, item.precision, item.associatedSampler, item.minFeatureLevel,
            { item.structName.data(), item.structName.size() },
            { item.sizeName.data(), item.sizeName.size() }
    });
    return *this;
}

// 构建BufferInterfaceBlock对象
BufferInterfaceBlock BufferInterfaceBlock::Builder::build() {
    // 查找第一个可变大小数组
    // look for the first variable-size array
    auto pos = std::find_if(mEntries.begin(), mEntries.end(),
            [](FieldInfo const& item) -> bool {
        return item.isArray && !item.size;
    });

    // 如果存在可变大小数组，检查它是否是最后一个条目
    // if there is one, check it's the last entry
    FILAMENT_CHECK_PRECONDITION(pos == mEntries.end() || pos == mEntries.end() - 1)
            << "the variable-size array must be the last entry";

    // 如果有可变大小数组，不能是UBO
    // if we have a variable size array, we can't be a UBO
    FILAMENT_CHECK_PRECONDITION(pos == mEntries.end() || mTarget == Target::SSBO)
            << "variable size arrays not supported for UBOs";

    // UBO必须使用std140对齐方式
    // std430 not available for UBOs
    FILAMENT_CHECK_PRECONDITION(mAlignment == Alignment::std140 || mTarget == Target::SSBO)
            << "UBOs must use std140";

    return BufferInterfaceBlock(*this);
}

// 检查是否包含可变大小数组
bool BufferInterfaceBlock::Builder::hasVariableSizeArray() const {
    return mHasVariableSizeArray;
}

// --------------------------------------------------------------------------------------------------------------------

// 默认构造函数
BufferInterfaceBlock::BufferInterfaceBlock() = default;
// 移动构造函数
BufferInterfaceBlock::BufferInterfaceBlock(BufferInterfaceBlock&& rhs) noexcept = default;
// 移动赋值运算符
BufferInterfaceBlock& BufferInterfaceBlock::operator=(BufferInterfaceBlock&& rhs) noexcept = default;
// 析构函数
BufferInterfaceBlock::~BufferInterfaceBlock() noexcept = default;

// 从Builder构造BufferInterfaceBlock（计算字段偏移量和对齐）
BufferInterfaceBlock::BufferInterfaceBlock(Builder const& builder) noexcept
    : mName(builder.mName),
      mFieldInfoList(builder.mEntries.size()),
      mAlignment(builder.mAlignment),
      mTarget(builder.mTarget)
{
    auto& infoMap = mInfoMap;
    // 预留映射表空间
    infoMap.reserve(builder.mEntries.size());

    auto& uniformsInfoList = mFieldInfoList;

    uint32_t i = 0;          // 当前字段索引
    uint16_t offset = 0;     // 当前偏移量（以uint32_t为单位）
    // 遍历所有字段，计算每个字段的偏移量
    for (auto const& e : builder.mEntries) {
        // 获取该类型的基础对齐方式
        size_t alignment = baseAlignmentForType(e.type);
        // 获取该类型的步长
        size_t stride = strideForType(e.type, e.stride);

        // 如果是数组，需要特殊处理对齐
        if (e.isArray) { // this is an array
            if (builder.mAlignment == Alignment::std140) {
                // 在std140中，数组对齐到float4
                // in std140 arrays are aligned to float4
                alignment = 4;
            }
            // 数组的步长总是向上舍入到对齐方式（对齐方式是2的幂）
            // the stride of an array is always rounded to its alignment (which is POT)
            stride = (stride + alignment - 1) & ~(alignment - 1);
        }

        // 计算该字段所需的填充和对齐
        // calculate the offset for this uniform
        size_t padding = (alignment - (offset % alignment)) % alignment;
        offset += padding;

        // 创建字段信息并设置偏移量
        FieldInfo& info = uniformsInfoList[i];
        info = { e.name, offset, uint8_t(stride), e.type, e.isArray, e.size,
                 e.precision, e.associatedSampler, e.minFeatureLevel, e.structName, e.sizeName };

        // 记录该字段名称到索引的映射
        // record this uniform info
        infoMap[{ info.name.data(), info.name.size() }] = i;

        // 将偏移量推进到下一个位置
        // advance offset to next slot
        offset += stride * std::max(1u, e.size);
        ++i;
    }

    // 将大小向上舍入到4的倍数并转换为字节数
    // round size to the next multiple of 4 and convert to bytes
    mSize = sizeof(uint32_t) * ((offset + 3) & ~3);
}

// 获取字段的字节偏移量
ssize_t BufferInterfaceBlock::getFieldOffset(std::string_view name, size_t index) const {
    auto const* info = getFieldInfo(name);
    assert_invariant(info);
    // 调用FieldInfo的方法获取缓冲区的字节偏移量
    return (ssize_t)info->getBufferOffset(index);
}

// 获取指定名称的字段信息
BufferInterfaceBlock::FieldInfo const* BufferInterfaceBlock::getFieldInfo(
        std::string_view name) const {
    // 在映射表中查找字段名称
    auto pos = mInfoMap.find(name);
    // 如果未找到，抛出异常或触发断言
    FILAMENT_CHECK_PRECONDITION(pos != mInfoMap.end()) << "uniform named \""
            << name << "\" not found";
    // 返回对应的字段信息
    return &mFieldInfoList[pos->second];
}

// 检查在指定功能级别下是否为空（所有字段都不需要）
bool BufferInterfaceBlock::isEmptyForFeatureLevel(
        backend::FeatureLevel featureLevel) const noexcept {
    // 检查所有字段的最低功能级别是否都高于指定级别
    return std::all_of(mFieldInfoList.begin(), mFieldInfoList.end(),
                       [featureLevel](auto const &info) {
                           return featureLevel < info.minFeatureLevel;
                       });
}

// 获取指定类型的基础对齐方式（以uint32_t为单位）
uint8_t UTILS_NOINLINE BufferInterfaceBlock::baseAlignmentForType(BufferInterfaceBlock::Type type) noexcept {
    switch (type) {
        case Type::BOOL:
        case Type::FLOAT:
        case Type::INT:
        case Type::UINT:
            return 1;
        case Type::BOOL2:
        case Type::FLOAT2:
        case Type::INT2:
        case Type::UINT2:
            return 2;
        case Type::BOOL3:
        case Type::BOOL4:
        case Type::FLOAT3:
        case Type::FLOAT4:
        case Type::INT3:
        case Type::INT4:
        case Type::UINT3:
        case Type::UINT4:
        case Type::MAT3:
        case Type::MAT4:
        case Type::STRUCT:
            return 4;
    }
}

// 获取指定类型的步长（以uint32_t为单位）
uint8_t UTILS_NOINLINE BufferInterfaceBlock::strideForType(BufferInterfaceBlock::Type type, uint32_t stride) noexcept {
    switch (type) {
        case Type::BOOL:
        case Type::INT:
        case Type::UINT:
        case Type::FLOAT:
            return 1;
        case Type::BOOL2:
        case Type::INT2:
        case Type::UINT2:
        case Type::FLOAT2:
            return 2;
        case Type::BOOL3:
        case Type::INT3:
        case Type::UINT3:
        case Type::FLOAT3:
            return 3;
        case Type::BOOL4:
        case Type::INT4:
        case Type::UINT4:
        case Type::FLOAT4:
            return 4;
        case Type::MAT3:
            return 12;
        case Type::MAT4:
            return 16;
        case Type::STRUCT:
            return stride;
    }
}

} // namespace filament
