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

#include <filaflat/MaterialChunk.h>
#include <filaflat/ChunkContainer.h>

#include <backend/DriverEnums.h>

#include <utils/Log.h>

namespace filaflat {

// 创建着色器键（将着色器模型、变体和阶段组合成一个32位键）
// 键格式：位16-23：着色器模型，位8-15：着色器阶段，位0-7：变体键
static inline uint32_t makeKey(
        MaterialChunk::ShaderModel shaderModel,
        MaterialChunk::Variant variant,
        MaterialChunk::ShaderStage stage) noexcept {
    static_assert(sizeof(variant.key) * 8 <= 8);
    return (uint32_t(shaderModel) << 16) | (uint32_t(stage) << 8) | variant.key;
}

// 解码着色器键，提取着色器模型、变体和阶段（调试用）
void MaterialChunk::decodeKey(uint32_t key,
        MaterialChunk::ShaderModel* model,
        MaterialChunk::Variant* variant,
        MaterialChunk::ShaderStage* stage) {
    variant->key = key & 0xff;  // 提取低8位（变体键）
    *model = MaterialChunk::ShaderModel((key >> 16) & 0xff);  // 提取位16-23（着色器模型）
    *stage = MaterialChunk::ShaderStage((key >> 8) & 0xff);   // 提取位8-15（着色器阶段）
}

// 构造函数，使用块容器初始化
MaterialChunk::MaterialChunk(ChunkContainer const& container)
        : mContainer(container) {
}

MaterialChunk::~MaterialChunk() noexcept = default;

// 初始化材质块，读取着色器索引
bool MaterialChunk::initialize(filamat::ChunkType materialTag) {

    if (mBase != nullptr) {
        // initialize() should be called only once.
        // initialize()应该只调用一次
        return true;
    }

    // 获取材质块的数据范围
    auto [start, end] = mContainer.getChunkRange(materialTag);
    if (start == end) {
        return false;
    }

    Unflattener unflattener(start, end);

    mUnflattener = unflattener;
    mMaterialTag = materialTag;
    mBase = unflattener.getCursor();

    // Read how many shaders we have in the chunk.
    // 读取块中有多少个着色器
    uint64_t numShaders;
    if (!unflattener.read(&numShaders) || numShaders == 0) {
        return false;
    }

    // Read all index entries.
    // 读取所有索引条目
    for (uint64_t i = 0 ; i < numShaders; i++) {
        uint8_t model;
        Variant variant;
        uint8_t stage;
        uint32_t offsetValue;

        // 读取着色器模型
        if (!unflattener.read(&model)) {
            return false;
        }

        // 读取变体
        if (!unflattener.read(&variant)) {
            return false;
        }

        // 读取着色器阶段
        if (!unflattener.read(&stage)) {
            return false;
        }

        // 读取偏移值
        if (!unflattener.read(&offsetValue)) {
            return false;
        }

        // 创建键并存储偏移量
        uint32_t key = makeKey(ShaderModel(model), variant, ShaderStage(stage));
        mOffsets[key] = offsetValue;
    }
    return true;
}

// 获取文本着色器（通过字典解压缩）
bool MaterialChunk::getTextShader(Unflattener unflattener,
        BlobDictionary const& dictionary, ShaderContent& shaderContent,
        ShaderModel shaderModel, Variant variant, ShaderStage shaderStage) const {
    if (mBase == nullptr) {
        return false;
    }

    // Jump and read
    // 跳转并读取
    // 查找着色器的偏移量
    uint32_t key = makeKey(shaderModel, variant, shaderStage);
    auto pos = mOffsets.find(key);
    if (pos == mOffsets.end()) {
        return false;
    }

    size_t offset = pos->second;
    if (offset == 0) {
        // This shader was not found.
        // 未找到此着色器
        return false;
    }
    // 设置游标到着色器数据位置
    unflattener.setCursor(mBase + offset);

    // Read how big the shader is.
    // 读取着色器的大小
    uint32_t shaderSize = 0;
    if (!unflattener.read(&shaderSize)){
        return false;
    }

    // Read how many lines there are.
    // 读取有多少行
    uint32_t lineCount = 0;
    if (!unflattener.read(&lineCount)){
        return false;
    }

    // 预留空间并调整大小
    shaderContent.reserve(shaderSize);
    shaderContent.resize(shaderSize);
    size_t cursor = 0;

    // Read all lines.
    // 读取所有行
    for(int32_t i = 0 ; i < lineCount; i++) {
        // 读取行索引
        uint16_t lineIndex;
        if (!unflattener.read(&lineIndex)) {
            return false;
        }
        // 从字典中获取行内容
        const auto& content = dictionary[lineIndex];

        // remove the terminating null character.
        // 移除终止空字符
        memcpy(&shaderContent[cursor], content.data(), content.size() - 1);
        cursor += content.size() - 1;
    }

    // Write the terminating null character.
    // 写入终止空字符
    shaderContent[cursor++] = 0;
    assert_invariant(cursor == shaderSize);

    return true;
}

// 获取二进制着色器（直接从字典中获取）
bool MaterialChunk::getBinaryShader(BlobDictionary const& dictionary,
        ShaderContent& shaderContent, ShaderModel shaderModel, filament::Variant variant, ShaderStage shaderStage) const {

    if (mBase == nullptr) {
        return false;
    }

    // 查找着色器在字典中的索引
    uint32_t key = makeKey(shaderModel, variant, shaderStage);
    auto pos = mOffsets.find(key);
    if (pos == mOffsets.end()) {
        return false;
    }

    // 直接从字典中获取着色器内容（偏移值即为字典索引）
    shaderContent = dictionary[pos->second];
    return true;
}

// 检查是否存在指定参数的着色器
bool MaterialChunk::hasShader(ShaderModel model, Variant variant, ShaderStage stage) const noexcept {
    if (mBase == nullptr) {
        return false;
    }
    // 查找是否存在对应的键
    auto pos = mOffsets.find(makeKey(model, variant, stage));
    return pos != mOffsets.end();
}

// 获取着色器（根据材质块类型选择文本或二进制着色器）
bool MaterialChunk::getShader(ShaderContent& shaderContent, BlobDictionary const& dictionary,
        ShaderModel shaderModel, filament::Variant variant, ShaderStage stage) const {
    switch (mMaterialTag) {
        // 文本着色器（使用字典压缩）
        case filamat::ChunkType::MaterialGlsl:
        case filamat::ChunkType::MaterialEssl1:
        case filamat::ChunkType::MaterialWgsl:
        case filamat::ChunkType::MaterialMetal:
            return getTextShader(mUnflattener, dictionary, shaderContent, shaderModel, variant, stage);
        // 二进制着色器（直接从字典获取）
        case filamat::ChunkType::MaterialSpirv:
        case filamat::ChunkType::MaterialMetalLibrary:
            return getBinaryShader(dictionary, shaderContent, shaderModel, variant, stage);
        default:
            return false;
    }
}

// 获取着色器数量
uint32_t MaterialChunk::getShaderCount() const noexcept {
    Unflattener unflattener{ mUnflattener }; // make a copy - 复制一份
    uint64_t numShaders;
    unflattener.read(&numShaders);
    return uint32_t(numShaders);
}

// 访问所有着色器，对每个着色器调用visitor函数
void MaterialChunk::visitShaders(
        utils::Invocable<void(ShaderModel, Variant, ShaderStage)>&& visitor) const {

    Unflattener unflattener{ mUnflattener }; // make a copy - 复制一份

    // read() calls below cannot fail by construction, because we've already run through them
    // in the constructor.
    // 下面的read()调用不会失败，因为我们在构造函数中已经遍历过它们了

    // Read how many shaders we have in the chunk.
    // 读取块中有多少个着色器
    uint64_t numShaders;
    unflattener.read(&numShaders);

    // Read all index entries.
    // 读取所有索引条目
    for (uint64_t i = 0; i < numShaders; i++) {
        uint8_t shaderModelValue;
        filament::Variant variant;
        uint8_t pipelineStageValue;
        uint32_t offsetValue;

        // 读取着色器信息
        unflattener.read(&shaderModelValue);
        unflattener.read(&variant);
        unflattener.read(&pipelineStageValue);
        unflattener.read(&offsetValue);

        // 调用visitor函数
        visitor(ShaderModel(shaderModelValue), variant, ShaderStage(pipelineStageValue));
    }
}

} // namespace filaflat
