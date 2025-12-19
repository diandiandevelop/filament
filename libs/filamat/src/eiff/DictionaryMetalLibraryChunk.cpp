/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "DictionaryMetalLibraryChunk.h"

namespace filamat {

// 构造函数，使用blob字典初始化
DictionaryMetalLibraryChunk::DictionaryMetalLibraryChunk(BlobDictionary&& dictionary)
    : Chunk(ChunkType::DictionaryMetalLibrary), mDictionary(std::move(dictionary)) {}

// 将块扁平化到Flattener中
// @param f Flattener对象，用于写入数据
void DictionaryMetalLibraryChunk::flatten(Flattener& f) {
    // 写入blob数量
    f.writeUint32(mDictionary.getBlobCount());
    // 遍历所有blob，写入blob数据
    for (size_t i = 0 ; i < mDictionary.getBlobCount() ; i++) {
        // 获取blob
        std::string_view blob = mDictionary.getBlob(i);
        // 写入对齐填充
        f.writeAlignmentPadding();
        // 写入blob数据
        f.writeBlob((const char*) blob.data(), blob.size());
    }
}

} // namespace filamat
