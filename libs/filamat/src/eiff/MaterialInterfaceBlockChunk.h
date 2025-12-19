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

#ifndef TNT_FILAMAT_MAT_INTEFFACE_BLOCK_CHUNK_H
#define TNT_FILAMAT_MAT_INTEFFACE_BLOCK_CHUNK_H

#include "Chunk.h"

#include <backend/Program.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>

#include <tuple>

#include <stdint.h>

namespace filament {
class SamplerInterfaceBlock;
class BufferInterfaceBlock;
struct SubpassInfo;
struct MaterialConstant;
struct MaterialPushConstant;
} // namespace filament

namespace filamat {

// 材质统一接口块块类，用于存储材质统一接口块（UBO）信息
class MaterialUniformInterfaceBlockChunk final : public Chunk {
public:
    explicit MaterialUniformInterfaceBlockChunk(filament::BufferInterfaceBlock const& uib);
    ~MaterialUniformInterfaceBlockChunk() override = default;

private:
    void flatten(Flattener&) override;

    filament::BufferInterfaceBlock const& mUib;  // 统一接口块引用
};

// ------------------------------------------------------------------------------------------------

// 材质采样器接口块块类，用于存储材质采样器接口块（SIB）信息
class MaterialSamplerInterfaceBlockChunk final : public Chunk {
public:
    explicit MaterialSamplerInterfaceBlockChunk(filament::SamplerInterfaceBlock const& sib);
    ~MaterialSamplerInterfaceBlockChunk() override = default;

private:
    void flatten(Flattener&) override;

    filament::SamplerInterfaceBlock const& mSib;  // 采样器接口块引用
};

// ------------------------------------------------------------------------------------------------

// 材质子通道接口块块类，用于存储子通道信息
class MaterialSubpassInterfaceBlockChunk final : public Chunk {
public:
    explicit MaterialSubpassInterfaceBlockChunk(filament::SubpassInfo const& subpass);
    ~MaterialSubpassInterfaceBlockChunk() override = default;

private:
    void flatten(Flattener&) override;

    filament::SubpassInfo const& mSubpass;  // 子通道信息引用
};

// ------------------------------------------------------------------------------------------------

// 材质常量参数块类，用于存储材质常量参数信息
class MaterialConstantParametersChunk final : public Chunk {
public:
    explicit MaterialConstantParametersChunk(
            FixedCapacityVector<filament::MaterialConstant> constants);
    ~MaterialConstantParametersChunk() override = default;

private:
    void flatten(Flattener&) override;

    FixedCapacityVector<filament::MaterialConstant> mConstants;  // 常量列表
};

// ------------------------------------------------------------------------------------------------

// 材质推送常量参数块类，用于存储推送常量参数信息
class MaterialPushConstantParametersChunk final : public Chunk {
public:
    explicit MaterialPushConstantParametersChunk(CString const& structVarName,
            FixedCapacityVector<filament::MaterialPushConstant> constants);
    ~MaterialPushConstantParametersChunk() override = default;

private:
    void flatten(Flattener&) override;

    CString mStructVarName;  // 结构体变量名称
    FixedCapacityVector<filament::MaterialPushConstant> mConstants;  // 推送常量列表
};

// ------------------------------------------------------------------------------------------------

// 材质绑定统一变量信息块类，用于存储绑定统一变量信息
class MaterialBindingUniformInfoChunk final : public Chunk {
    using Container = FixedCapacityVector<std::tuple<
            uint8_t, CString, filament::backend::Program::UniformInfo>>;
public:
    explicit MaterialBindingUniformInfoChunk(Container list) noexcept;
    ~MaterialBindingUniformInfoChunk() override = default;

private:
    void flatten(Flattener &) override;

    Container mBindingUniformInfo;  // 绑定统一变量信息列表（绑定点、名称、信息）
};

// ------------------------------------------------------------------------------------------------

// 材质属性信息块类，用于存储材质属性信息
class MaterialAttributesInfoChunk final : public Chunk {
    using Container = FixedCapacityVector<std::pair<CString, uint8_t>>;
public:
    explicit MaterialAttributesInfoChunk(Container list) noexcept;
    ~MaterialAttributesInfoChunk() override = default;

private:
    void flatten(Flattener &) override;

    Container mAttributeInfo;  // 属性信息列表（属性名称、位置）
};

// ------------------------------------------------------------------------------------------------

// 材质描述符绑定块类，用于存储描述符绑定信息
class MaterialDescriptorBindingsChuck final : public Chunk {
    using Container = filament::SamplerInterfaceBlock;
public:
    explicit MaterialDescriptorBindingsChuck(Container const& sib) noexcept;
    ~MaterialDescriptorBindingsChuck() override = default;

private:
    void flatten(Flattener&) override;

    Container const& mSamplerInterfaceBlock;  // 采样器接口块引用
};

// ------------------------------------------------------------------------------------------------

// 材质描述符集布局块类，用于存储描述符集布局信息
class MaterialDescriptorSetLayoutChunk final : public Chunk {
    using Container = filament::SamplerInterfaceBlock;
public:
    explicit MaterialDescriptorSetLayoutChunk(Container const& sib) noexcept;
    ~MaterialDescriptorSetLayoutChunk() override = default;

private:
    void flatten(Flattener&) override;

    Container const& mSamplerInterfaceBlock;  // 采样器接口块引用
};

} // namespace filamat

#endif // TNT_FILAMAT_MAT_INTEFFACE_BLOCK_CHUNK_H
