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

#include <filaflat/ChunkContainer.h>

#include <filaflat/Unflattener.h>

namespace filaflat {

ChunkContainer::~ChunkContainer() noexcept = default;

// 检查是否存在指定类型的块
bool ChunkContainer::hasChunk(Type type) const noexcept {
    auto const& chunks = mChunks;
    auto pos = chunks.find(type);
    return pos != chunks.end();
}

// 检查是否存在指定类型的块，如果存在则填充描述符
bool ChunkContainer::hasChunk(Type type, ChunkDesc* pChunkDesc) const noexcept {
    assert_invariant(pChunkDesc);
    auto const& chunks = mChunks;
    auto pos = chunks.find(type);
    if (UTILS_LIKELY(pos != chunks.end())) {
        *pChunkDesc = pos.value();
        return true;
    }
    return false;
}

// 解析单个块（私有辅助方法）
bool ChunkContainer::parseChunk(Unflattener& unflattener) {
    // 读取块类型
    uint64_t type;
    if (!unflattener.read(&type)) {
        return false;
    }

    // 读取块大小
    uint32_t size;
    if (!unflattener.read(&size)) {
        return false;
    }

    // If size goes beyond the boundaries of the package, this is an invalid chunk. Discard it.
    // All remaining chunks cannot be accessed and will not be mapped.
    // 如果大小超出包的边界，这是一个无效块。丢弃它。所有剩余块无法访问且不会被映射
    auto cursor = unflattener.getCursor();
    if (!(cursor + size >= (uint8_t *)mData &&
          cursor + size <= (uint8_t *)mData + mSize)) {
        return false;
    }

    // 将块信息存储到映射表中
    mChunks[Type(type)] = { cursor, size };
    // 更新读取游标到下一个块的位置
    unflattener.setCursor(cursor + size);
    return true;
}

// 解析所有块，构建块映射表
bool ChunkContainer::parse() noexcept {
    // 创建Unflattener来读取数据
    Unflattener unflattener((uint8_t *)mData, (uint8_t *)mData + mSize);
    // 循环解析所有块，直到没有更多数据
    do {
        if (!parseChunk(unflattener)) {
            return false;
        }
    } while (unflattener.hasData());
    return true;
}

} // namespace filaflat
