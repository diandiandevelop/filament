/*
* Copyright (C) 2025 The Android Open Source Project
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

#ifndef TNT_COMPRESSEDSTRINGCHUNK_H
#define TNT_COMPRESSEDSTRINGCHUNK_H

#include "Chunk.h"
#include "Flattener.h"

#include <utils/CString.h>

namespace filamat {

// 压缩字符串块类，用于存储使用ZSTD压缩的字符串
class CompressedStringChunk final : public Chunk {
public:
    // 压缩级别枚举
    enum class CompressionLevel { MIN, MAX, DEFAULT };
    // 构造函数，使用块类型、字符串和压缩级别初始化
    // @param type 块类型
    // @param string 要压缩的字符串
    // @param compressionLevel 压缩级别
    CompressedStringChunk(
            ChunkType type, std::string_view string, CompressionLevel compressionLevel)
            : Chunk(type),
              mString(utils::CString(string.data(), string.size())),
              mCompressionLevel(compressionLevel) {}
    ~CompressedStringChunk() override = default;

private:
    // 将块扁平化到Flattener中（使用ZSTD压缩字符串）
    void flatten(Flattener& f) override;
    utils::CString mString;              // 要压缩的字符串
    CompressionLevel mCompressionLevel;  // 压缩级别
};

} // namespace filamat


#endif // TNT_COMPRESSEDSTRINGCHUNK_H
