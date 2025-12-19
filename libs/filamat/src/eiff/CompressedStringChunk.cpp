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

#include "CompressedStringChunk.h"

#include <zstd.h>

namespace filamat {

namespace {
// 将压缩级别枚举转换为ZSTD压缩级别
// @param compressionLevel 压缩级别枚举
// @return ZSTD压缩级别值
int toZstdCompressionLevel(CompressedStringChunk::CompressionLevel compressionLevel) {
    switch (compressionLevel) {
        case CompressedStringChunk::CompressionLevel::MIN:
            return ZSTD_minCLevel();
        case CompressedStringChunk::CompressionLevel::MAX:
            return ZSTD_maxCLevel();
        case CompressedStringChunk::CompressionLevel::DEFAULT:
            return ZSTD_defaultCLevel();
    }
}
} // namespace

// 将块扁平化到Flattener中（使用ZSTD压缩字符串）
// @param f Flattener对象，用于写入数据
void CompressedStringChunk::flatten(filamat::Flattener& f) {
    // 计算压缩缓冲区大小
    const size_t bufferBound = ZSTD_compressBound(mString.size());
    std::vector<std::uint8_t> compressed(bufferBound);

    // 压缩字符串
    const size_t compressedSize = ZSTD_compress(
            compressed.data(), compressed.size(),
            mString.data(), mString.size(),
            toZstdCompressionLevel(mCompressionLevel));

    // 检查压缩是否成功
    if (ZSTD_isError(compressedSize)) {
        utils::slog.e << "Error compressing the input string." << utils::io::endl;
        return;
    }

    // 写入压缩后的blob
    f.writeBlob((const char*) compressed.data(), compressedSize);
}

} // namespace filamat