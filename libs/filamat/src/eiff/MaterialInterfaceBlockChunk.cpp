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
#include "MaterialInterfaceBlockChunk.h"

#include "filament/MaterialChunkType.h"

#include <private/filament/BufferInterfaceBlock.h>
#include <private/filament/ConstantInfo.h>
#include <private/filament/DescriptorSets.h>
#include <private/filament/EngineEnums.h>
#include <private/filament/PushConstantInfo.h>
#include <private/filament/SamplerInterfaceBlock.h>
#include <private/filament/SubpassInfo.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/CString.h>
#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>

#include <utility>

#include <stdint.h>

using namespace filament;

namespace filamat {

// 构造函数，使用统一接口块初始化
MaterialUniformInterfaceBlockChunk::MaterialUniformInterfaceBlockChunk(
        BufferInterfaceBlock const& uib) :
        Chunk(MaterialUib),
        mUib(uib) {
}

// 将块扁平化到Flattener中
// @param f Flattener对象，用于写入数据
void MaterialUniformInterfaceBlockChunk::flatten(Flattener& f) {
    // 写入UBO名称
    f.writeString(mUib.getName());
    // 获取字段信息列表
    auto uibFields = mUib.getFieldInfoList();
    // 写入字段数量
    f.writeUint64(uibFields.size());
    // 遍历所有字段，写入字段信息
    for (auto uInfo: uibFields) {
        f.writeString(uInfo.name.c_str());                          // 字段名称
        f.writeUint64(uInfo.size);                                    // 字段大小
        f.writeUint8(static_cast<uint8_t>(uInfo.type));             // 字段类型
        f.writeUint8(static_cast<uint8_t>(uInfo.precision));        // 精度
        f.writeUint8(static_cast<uint8_t>(uInfo.associatedSampler)); // 关联的采样器
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用采样器接口块初始化
MaterialSamplerInterfaceBlockChunk::MaterialSamplerInterfaceBlockChunk(
        SamplerInterfaceBlock const& sib) :
        Chunk(MaterialSib),
        mSib(sib) {
}

// 将块扁平化到Flattener中
void MaterialSamplerInterfaceBlockChunk::flatten(Flattener& f) {
    // 写入SIB名称
    f.writeString(mSib.getName().c_str());
    // 获取采样器信息列表
    auto sibFields = mSib.getSamplerInfoList();
    // 写入采样器数量
    f.writeUint64(sibFields.size());
    // 遍历所有采样器，写入采样器信息
    for (auto sInfo: sibFields) {
        f.writeString(sInfo.name.c_str());                          // 采样器名称
        f.writeUint8(static_cast<uint8_t>(sInfo.binding));          // 绑定点
        f.writeUint8(static_cast<uint8_t>(sInfo.type));            // 采样器类型
        f.writeUint8(static_cast<uint8_t>(sInfo.format));          // 采样器格式
        f.writeUint8(static_cast<uint8_t>(sInfo.precision));       // 精度
        f.writeBool(sInfo.filterable);                              // 是否可过滤
        f.writeBool(sInfo.multisample);                             // 是否多重采样
        f.writeString(sInfo.transformName.c_str_safe());            // 变换名称
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用子通道信息初始化
MaterialSubpassInterfaceBlockChunk::MaterialSubpassInterfaceBlockChunk(SubpassInfo const& subpass) :
        Chunk(MaterialSubpass),
        mSubpass(subpass) {
}

// 将块扁平化到Flattener中
void MaterialSubpassInterfaceBlockChunk::flatten(Flattener& f) {
    // 写入子通道块名称
    f.writeString(mSubpass.block.c_str());
    // only ever a single subpass for now
    // 目前只支持单个子通道
    f.writeUint64(mSubpass.isValid ? 1 : 0);
    if (mSubpass.isValid) {
        f.writeString(mSubpass.name.c_str());                       // 子通道名称
        f.writeUint8(static_cast<uint8_t>(mSubpass.type));         // 子通道类型
        f.writeUint8(static_cast<uint8_t>(mSubpass.format));       // 格式
        f.writeUint8(static_cast<uint8_t>(mSubpass.precision));    // 精度
        f.writeUint8(static_cast<uint8_t>(mSubpass.attachmentIndex));
        f.writeUint8(static_cast<uint8_t>(mSubpass.binding));
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用常量列表初始化
MaterialConstantParametersChunk::MaterialConstantParametersChunk(
        FixedCapacityVector<MaterialConstant> constants)
    : Chunk(MaterialConstants), mConstants(std::move(constants)) {}

// 将块扁平化到Flattener中
void MaterialConstantParametersChunk::flatten(Flattener& f) {
    // 写入常量数量
    f.writeUint64(mConstants.size());
    // 遍历所有常量，写入常量信息
    for (const auto& constant : mConstants) {
        f.writeString(constant.name.c_str());                              // 常量名称
        f.writeUint8(static_cast<uint8_t>(constant.type));                // 常量类型
        f.writeUint32(static_cast<uint32_t>(constant.defaultValue.i));    // 默认值
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用结构体变量名称和推送常量列表初始化
MaterialPushConstantParametersChunk::MaterialPushConstantParametersChunk(
        CString const& structVarName, FixedCapacityVector<MaterialPushConstant> constants)
    : Chunk(MaterialPushConstants),
      mStructVarName(structVarName),
      mConstants(std::move(constants)) {}

// 将块扁平化到Flattener中
void MaterialPushConstantParametersChunk::flatten(Flattener& f) {
    // 写入结构体变量名称
    f.writeString(mStructVarName.c_str());
    // 写入推送常量数量
    f.writeUint64(mConstants.size());
    // 遍历所有推送常量，写入推送常量信息
    for (const auto& constant: mConstants) {
        f.writeString(constant.name.c_str());                         // 常量名称
        f.writeUint8(static_cast<uint8_t>(constant.type));           // 常量类型
        f.writeUint8(static_cast<uint8_t>(constant.stage));          // 着色器阶段
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用绑定统一变量信息列表初始化
MaterialBindingUniformInfoChunk::MaterialBindingUniformInfoChunk(Container list) noexcept
        : Chunk(MaterialBindingUniformInfo),
          mBindingUniformInfo(std::move(list)) {
}

// 将块扁平化到Flattener中
void MaterialBindingUniformInfoChunk::flatten(Flattener& f) {
    // 写入绑定数量
    f.writeUint8(mBindingUniformInfo.size());
    // 遍历所有绑定，写入绑定信息
    for (auto const& [index, name, uniforms] : mBindingUniformInfo) {
        f.writeUint8(uint8_t(index));                                 // 绑定索引
        f.writeString({ name.data(), name.size() });                  // 绑定名称
        f.writeUint8(uint8_t(uniforms.size()));                       // 统一变量数量
        // 遍历该绑定的所有统一变量
        for (auto const& uniform: uniforms) {
            f.writeString({ uniform.name.data(), uniform.name.size() }); // 统一变量名称
            f.writeUint16(uniform.offset);                            // 偏移量
            f.writeUint8(uniform.size);                               // 大小
            f.writeUint8(uint8_t(uniform.type));                      // 类型
        }
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用属性信息列表初始化
MaterialAttributesInfoChunk::MaterialAttributesInfoChunk(Container list) noexcept
        : Chunk(MaterialAttributeInfo),
          mAttributeInfo(std::move(list))
{
}

// 将块扁平化到Flattener中
void MaterialAttributesInfoChunk::flatten(Flattener& f) {
    // 写入属性数量
    f.writeUint8(mAttributeInfo.size());
    // 遍历所有属性，写入属性信息
    for (auto const& [attribute, location]: mAttributeInfo) {
        f.writeString({ attribute.data(), attribute.size() });  // 属性名称
        f.writeUint8(location);                                  // 属性位置
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用采样器接口块初始化
MaterialDescriptorBindingsChuck::MaterialDescriptorBindingsChuck(Container const& sib) noexcept
        : Chunk(MaterialDescriptorBindingsInfo),
          mSamplerInterfaceBlock(sib) {
}

// 将块扁平化到Flattener中
void MaterialDescriptorBindingsChuck::flatten(Flattener& f) {
    // 确保描述符集和绑定点的大小为uint8_t
    assert_invariant(sizeof(backend::descriptor_set_t) == sizeof(uint8_t));
    assert_invariant(sizeof(backend::descriptor_binding_t) == sizeof(uint8_t));

    using namespace backend;

    // samplers + 1 descriptor for the UBO
    // 采样器数量 + 1个UBO描述符
    f.writeUint8(mSamplerInterfaceBlock.getSize() + 1);

    // our UBO descriptor is always at binding 0
    // 我们的UBO描述符始终在绑定0
    CString const uboName =
            descriptor_sets::getDescriptorName(DescriptorSetBindingPoints::PER_MATERIAL, 0);
    f.writeString({ uboName.data(), uboName.size() });          // UBO名称
    f.writeUint8(uint8_t(DescriptorType::UNIFORM_BUFFER));     // 描述符类型（统一缓冲区）
    f.writeUint8(0);                                            // 绑定点（0）

    // all the material's sampler descriptors
    // 所有材质的采样器描述符
    for (auto const& entry: mSamplerInterfaceBlock.getSamplerInfoList()) {
        f.writeString({ entry.uniformName.data(), entry.uniformName.size() });  // 采样器名称
        f.writeUint8(uint8_t(descriptor_sets::getDescriptorType(entry.type, entry.format))); // 描述符类型
        f.writeUint8(entry.binding);                             // 绑定点
    }
}

// ------------------------------------------------------------------------------------------------

// 构造函数，使用采样器接口块初始化
MaterialDescriptorSetLayoutChunk::MaterialDescriptorSetLayoutChunk(Container const& sib) noexcept
        : Chunk(MaterialDescriptorSetLayoutInfo),
          mSamplerInterfaceBlock(sib) {
}

// 将块扁平化到Flattener中
void MaterialDescriptorSetLayoutChunk::flatten(Flattener& f) {
    // 确保描述符集和绑定点的大小为uint8_t
    assert_invariant(sizeof(backend::descriptor_set_t) == sizeof(uint8_t));
    assert_invariant(sizeof(backend::descriptor_binding_t) == sizeof(uint8_t));

    using namespace backend;

    // samplers + 1 descriptor for the UBO
    // 采样器数量 + 1个UBO描述符
    f.writeUint8(mSamplerInterfaceBlock.getSize() + 1);

    // our UBO descriptor is always at binding 0
    // 我们的UBO描述符始终在绑定0
    f.writeUint8(uint8_t(DescriptorType::UNIFORM_BUFFER));                           // 描述符类型（统一缓冲区）
    f.writeUint8(uint8_t(ShaderStageFlags::VERTEX | ShaderStageFlags::FRAGMENT));   // 着色器阶段标志
    f.writeUint8(0);                                                                 // 绑定点（0）
    f.writeUint8(uint8_t(DescriptorFlags::DYNAMIC_OFFSET));                         // 描述符标志（动态偏移）
    f.writeUint16(0);                                                                // 预留字段

    // all the material's sampler descriptors
    // 所有材质的采样器描述符
    for (auto const& entry: mSamplerInterfaceBlock.getSamplerInfoList()) {
        f.writeUint8(uint8_t(descriptor_sets::getDescriptorType(entry.type, entry.format)));  // 描述符类型
        f.writeUint8(uint8_t(entry.stages));                                                    // 着色器阶段标志
        f.writeUint8(entry.binding);                                                            // 绑定点
        // 根据是否可过滤设置描述符标志
        if (!entry.filterable) {
            f.writeUint8(uint8_t(DescriptorFlags::UNFILTERABLE));  // 不可过滤标志
        } else {
            f.writeUint8(uint8_t(DescriptorFlags::NONE));          // 无标志
        }
        f.writeUint16(0);  // 预留字段
    }
}

} // namespace filamat
