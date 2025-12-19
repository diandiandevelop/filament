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

#ifndef TNT_FILAMAT_MATERIAL_BINARY_CHUNK_H
#define TNT_FILAMAT_MATERIAL_BINARY_CHUNK_H

#include "Chunk.h"
#include "ShaderEntry.h"

#include <vector>

namespace filamat {

// 材质二进制块类，用于存储二进制着色器（如SPIR-V、Metal库）的条目
class MaterialBinaryChunk final : public Chunk {
public:
    // 构造函数，使用二进制条目列表和块类型初始化
    explicit MaterialBinaryChunk(const std::vector<BinaryEntry>&& entries, ChunkType type);
    ~MaterialBinaryChunk() = default;

private:
    // 将块扁平化到Flattener中
    void flatten(Flattener& f) override;

    const std::vector<BinaryEntry> mEntries;  // 二进制条目列表
};

} // namespace filamat

#endif // TNT_FILAMAT_MATERIAL_BINARY_CHUNK_H
