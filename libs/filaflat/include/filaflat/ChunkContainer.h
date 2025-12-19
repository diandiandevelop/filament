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

#ifndef TNT_FILAFLAT_CHUNK_CONTAINER_H
#define TNT_FILAFLAT_CHUNK_CONTAINER_H


#include <utils/compiler.h>

#include <filament/MaterialChunkType.h>

#include <utils/FixedCapacityVector.h>

#include <tsl/robin_map.h>

namespace filaflat {

// 着色器内容类型，存储着色器的二进制数据
using ShaderContent = utils::FixedCapacityVector<uint8_t>;
// Blob字典类型，存储多个着色器内容（用于字典压缩）
using BlobDictionary = utils::FixedCapacityVector<ShaderContent>;

class Unflattener;

// Allows to build a map of chunks in a Package and get direct individual access based on chunk ID.
// 允许在Package中构建块映射，并根据块ID获取直接的单独访问
class UTILS_PUBLIC ChunkContainer {
public:
    // 块类型，使用MaterialChunkType中定义的ChunkType
    using Type = filamat::ChunkType;

    // 构造函数，使用原始数据和大小初始化容器
    ChunkContainer(void const* data, size_t size) : mData(data), mSize(size) {}

    ~ChunkContainer() noexcept;

    // Must be called before trying to access any of the chunk. Fails and return false ONLY if
    // an incomplete chunk is found or if a chunk with bogus size is found.
    // 在尝试访问任何块之前必须调用此方法。只有在发现不完整的块或块大小错误时才会失败并返回false
    bool parse() noexcept;

    // 块描述符结构，描述块的位置和大小
    typedef struct {
        const uint8_t* start;  // 块的起始地址
        size_t size;            // 块的大小（字节数）
    } ChunkDesc;

    // 块结构，包含块类型和描述符
    typedef struct {
        Type type;      // 块类型
        ChunkDesc desc; // 块描述符
    } Chunk;

    // 获取块的数量
    size_t getChunkCount() const noexcept {
        return mChunks.size();
    }

    // 根据索引获取块
    Chunk getChunk(size_t index) const noexcept {
        auto it = mChunks.begin();
        std::advance(it, index);
        return { it->first, it->second };
    }

    // 获取指定类型块的数据范围（起始和结束指针）
    std::pair<uint8_t const*, uint8_t const*> getChunkRange(Type type) const noexcept {
        ChunkDesc chunkDesc;
        bool const success = hasChunk(type, &chunkDesc);
        if (success) {
            return { chunkDesc.start, chunkDesc.start + chunkDesc.size };
        }
        return { nullptr, nullptr };
    }

    // 检查是否存在指定类型的块
    bool hasChunk(Type type) const noexcept;
    // 检查是否存在指定类型的块，如果存在则填充描述符
    bool hasChunk(Type type, ChunkDesc* pChunkDesc) const noexcept;

    // 获取原始数据指针
    void const* getData() const { return mData; }

    // 获取数据大小
    size_t getSize() const { return mSize; }

private:
    // 解析单个块（私有辅助方法）
    bool parseChunk(Unflattener& unflattener);

    void const* mData;      // 原始数据指针
    size_t mSize;            // 数据大小
    tsl::robin_map<Type, ChunkContainer::ChunkDesc> mChunks;  // 块映射表（类型 -> 描述符）
};

} // namespace filaflat
#endif
