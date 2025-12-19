/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "MaterialBinaryChunk.h"

namespace filamat {

// 构造函数，使用二进制条目列表和块类型初始化
MaterialBinaryChunk::MaterialBinaryChunk(
        const std::vector<BinaryEntry>&& entries, ChunkType chunkType)
    : Chunk(chunkType), mEntries(entries) {}

// 将块扁平化到Flattener中
void MaterialBinaryChunk::flatten(Flattener &f) {
    // 写入条目数量
    f.writeUint64(mEntries.size());
    // 遍历所有条目，写入着色器信息
    for (const BinaryEntry& entry : mEntries) {
        f.writeUint8(uint8_t(entry.shaderModel));     // 写入着色器模型
        f.writeUint8(entry.variant.key);              // 写入变体键
        f.writeUint8(uint8_t(entry.stage));           // 写入着色器阶段
        f.writeUint32(entry.dictionaryIndex);         // 写入字典索引
    }
}

}  // namespace filamat
