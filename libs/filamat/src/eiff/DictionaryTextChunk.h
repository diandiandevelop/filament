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

#ifndef TNT_FILAMAT_DIC_TEXT_CHUNK_H
#define TNT_FILAMAT_DIC_TEXT_CHUNK_H

#include <stdint.h>
#include <vector>

#include "Chunk.h"
#include "Flattener.h"
#include "LineDictionary.h"

namespace filamat {

// 文本字典块类，用于存储文本字典（用于文本着色器的字典压缩）
class DictionaryTextChunk final : public Chunk {
public:
    // 构造函数，使用行字典和块类型初始化
    DictionaryTextChunk(LineDictionary&& dictionary, ChunkType chunkType);
    ~DictionaryTextChunk() = default;

    // 获取字典引用
    const LineDictionary& getDictionary() const noexcept { return mDictionary; }

private:
    // 将块扁平化到Flattener中
    void flatten(Flattener& f) override;

    const LineDictionary mDictionary;  // 行字典
};

} // namespace filamat

#endif // TNT_FILAMAT_DIC_TEXT_CHUNK_H
