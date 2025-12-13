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

#include "ZstdHelper.h"

#include <zstd.h>

#include <cstddef>
#include <cstdint>

namespace filament {

/**
 * 检查数据是否被 Zstd 压缩
 * 
 * 通过检查 Zstd 魔数（magic number）来判断。
 * 
 * @param src 源数据指针
 * @param src_size 源数据大小
 * @return 如果数据被 Zstd 压缩则返回 true
 */
bool ZstdHelper::isCompressed(const void* src, size_t src_size) noexcept {
    if (src_size < 4) {
        return false;
    }

    /**
     * `src` 可能不对齐到 4 字节，这违反了
     * `UndefinedBehaviorSanitizer: misaligned-pointer-use` 的对齐要求。
     * 因此从小端字节序的字节重建 32 位整数，因为预期的字节序列是 28 B5 2F FD，
     * ZSTD_MAGICNUMBER 指的是 0xFD2FB528。
     * 这应该在小端和大端系统上都能正确工作。
     */
    const auto* p = static_cast<const uint8_t*>(src);
    const uint32_t magic =
            (uint32_t)p[0] |
            (uint32_t)(p[1] << 8) |
            (uint32_t)(p[2] << 16) |
            (uint32_t)(p[3] << 24);

    return magic == ZSTD_MAGICNUMBER;
}

/**
 * 获取解压后的大小
 * 
 * @param src 源数据指针
 * @param src_size 源数据大小
 * @return 解压后的大小
 */
size_t ZstdHelper::getDecodedSize(const void* src, size_t src_size) noexcept {
    return ZSTD_getFrameContentSize(src, src_size);
}

/**
 * 解压数据
 * 
 * @param dst 目标缓冲区指针
 * @param dst_size 目标缓冲区大小
 * @param src 源数据指针
 * @param src_size 源数据大小
 * @return 解压的字节数
 */
size_t ZstdHelper::decompress(void* dst, size_t dst_size, const void* src, size_t src_size) noexcept {
    return ZSTD_decompress(dst, dst_size, src, src_size);
}

} // namespace filament
