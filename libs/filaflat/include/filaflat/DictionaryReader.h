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

#ifndef TNT_FILAFLAT_DICTIONARY_READER_H
#define TNT_FILAFLAT_DICTIONARY_READER_H

#include <filaflat/ChunkContainer.h>

namespace filaflat {

// 字典读取器结构，用于从ChunkContainer中读取字典数据
struct DictionaryReader {
    // 从容器中反序列化字典数据
    // @param container 块容器
    // @param dictionaryTag 字典标签（字典块类型）
    // @param dictionary 输出字典，将填充字典数据
    // @return 成功返回true，失败返回false
    static bool unflatten(ChunkContainer const& container,
            ChunkContainer::Type dictionaryTag,
            BlobDictionary& dictionary);
};

} // namespace filaflat

#endif // TNT_FILAFLAT_DICTIONARY_READER_H
