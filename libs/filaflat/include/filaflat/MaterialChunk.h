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

#ifndef TNT_FILAMAT_MATERIAL_CHUNK_H
#define TNT_FILAMAT_MATERIAL_CHUNK_H

#include <filament/MaterialChunkType.h>

#include <filaflat/ChunkContainer.h>
#include <filaflat/Unflattener.h>

#include <private/filament/Variant.h>

#include <tsl/robin_map.h>

#include <backend/DriverEnums.h>

#include <utils/Invocable.h>
#include <utils/FixedCapacityVector.h>

namespace filaflat {

// 材质块类，用于从序列化的材质数据中读取着色器
class MaterialChunk {
public:
    using ShaderModel = filament::backend::ShaderModel;  // 着色器模型类型
    using ShaderStage = filament::backend::ShaderStage;  // 着色器阶段类型
    using Variant = filament::Variant;                   // 变体类型

    // 构造函数，使用块容器初始化
    explicit MaterialChunk(ChunkContainer const& container);
    ~MaterialChunk() noexcept;

    // call this once after container.parse() has been called
    // 在调用container.parse()之后调用一次此方法
    // @param materialTag 材质块类型标签
    // @return 成功返回true，失败返回false
    bool initialize(filamat::ChunkType materialTag);

    // call this as many times as needed
    // populates "shaderContent" with the requested shader, or returns false on failure.
    // 可以根据需要多次调用此方法
    // 用请求的着色器填充"shaderContent"，失败时返回false
    // @param shaderContent 输出着色器内容
    // @param dictionary 字典（用于文本着色器的字典压缩）
    // @param shaderModel 着色器模型
    // @param variant 着色器变体
    // @param stage 着色器阶段
    // @return 成功返回true，失败返回false
    bool getShader(ShaderContent& shaderContent, BlobDictionary const& dictionary,
            ShaderModel shaderModel, filament::Variant variant, ShaderStage stage) const;

    // 获取着色器数量
    uint32_t getShaderCount() const noexcept;

    // 访问所有着色器，对每个着色器调用visitor函数
    void visitShaders(utils::Invocable<void(ShaderModel, Variant, ShaderStage)>&& visitor) const;

    // 检查是否存在指定参数的着色器
    bool hasShader(ShaderModel model, Variant variant, ShaderStage stage) const noexcept;

    // These methods are for debugging purposes only (matdbg)
    // 这些方法仅用于调试目的（matdbg）
    // @{
    // 解码着色器键，提取着色器模型、变体和阶段
    static void decodeKey(uint32_t key,
            ShaderModel* outModel, Variant* outVariant, ShaderStage* outStage);
    // 获取偏移量映射表（调试用）
    const tsl::robin_map<uint32_t, uint32_t>& getOffsets() const { return mOffsets; }
    // @}

private:
    ChunkContainer const& mContainer;                    // 块容器引用
    filamat::ChunkType mMaterialTag = filamat::ChunkType::Unknown;  // 材质块类型标签
    Unflattener mUnflattener;                            // 反序列化器
    const uint8_t* mBase = nullptr;                      // 数据基址指针
    tsl::robin_map<uint32_t, uint32_t> mOffsets;        // 着色器键到偏移量的映射表

    // 获取文本着色器（通过字典解压缩）
    bool getTextShader(Unflattener unflattener,
            BlobDictionary const& dictionary, ShaderContent& shaderContent,
            ShaderModel shaderModel, filament::Variant variant, ShaderStage shaderStage) const;

    // 获取二进制着色器（直接从字典中获取）
    bool getBinaryShader(
            BlobDictionary const& dictionary, ShaderContent& shaderContent,
            ShaderModel shaderModel, filament::Variant variant, ShaderStage shaderStage) const;
};

} // namespace filaflat

#endif // TNT_FILAMAT_MATERIAL_CHUNK_H
