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

#ifndef TNT_FILAMENT_ZSTD_HELPER_H
#define TNT_FILAMENT_ZSTD_HELPER_H

#include <stddef.h>

namespace filament {

/**
 * Zstd 辅助类
 * 
 * 提供 Zstd 压缩/解压缩的辅助功能。
 * Zstd 是 Facebook 开发的高性能压缩算法。
 */
class ZstdHelper {
public:
    /**
     * 检查给定的二进制数据是否被 Zstd 压缩
     * 
     * @param src 源数据指针
     * @param src_size 源数据大小
     * @return 如果数据被 Zstd 压缩则返回 true，否则返回 false
     */
    static bool isCompressed(const void* src, size_t src_size) noexcept;

    /**
     * 返回 Zstd 压缩数据的解压后大小
     * 
     * @param src 源数据指针
     * @param src_size 源数据大小
     * @return 解压后的大小，如果发生错误则返回 0
     */
    static size_t getDecodedSize(const void* src, size_t src_size) noexcept;

    /**
     * 将 Zstd 压缩的数据解压到预分配的缓冲区
     * 
     * @param dst 目标缓冲区指针
     * @param dst_size 目标缓冲区大小
     * @param src 源数据指针
     * @param src_size 源数据大小
     * @return 解压的字节数，如果发生错误则返回 0
     */
    static size_t decompress(void* dst, size_t dst_size, const void* src, size_t src_size) noexcept;
};

} // namespace filament

#endif // TNT_FILAMENT_ZSTD_HELPER_H
