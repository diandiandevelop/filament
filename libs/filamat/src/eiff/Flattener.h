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

#ifndef TNT_FILAMAT_FLATENNER_H
#define TNT_FILAMAT_FLATENNER_H

#include <utility>
#include <utils/Log.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <map>
#include <string_view>
#include <vector>

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

using namespace utils;

namespace {
constexpr uint8_t FAKE_DRY_RUNNER_START_ADDR = 0x1;
} // anonymous

namespace filamat {

// 扁平化器类，用于将数据序列化到内存缓冲区
class Flattener {
public:
    // 构造函数，使用目标缓冲区初始化
    explicit Flattener(uint8_t* dst) : mCursor(dst), mStart(dst){}

    // DryRunner is used to compute the size of the flattened output but not actually carry out
    // flattening. If we set mStart = nullptr and mEnd=nullptr, we would hit an error about
    // offsetting on null when ubsan is enabled. Instead we point mStart to a fake address, and
    // mCursor is offset from that.
    // DryRunner用于计算扁平化输出的大小，但不实际执行扁平化。
    // 如果我们将mStart = nullptr和mEnd=nullptr，当启用ubsan时会出现关于在null上偏移的错误。
    // 相反，我们将mStart指向一个假地址，mCursor从中偏移。
    static Flattener& getDryRunner() {
        static Flattener dryRunner = Flattener(nullptr);
        dryRunner.mStart = (uint8_t*) FAKE_DRY_RUNNER_START_ADDR;
        dryRunner.mCursor =  (uint8_t*) FAKE_DRY_RUNNER_START_ADDR;
        dryRunner.mOffsetPlaceholders.clear();
        dryRunner.mSizePlaceholders.clear();
        dryRunner.mValuePlaceholders.clear();
        return dryRunner;
    }

    // 检查是否是DryRunner（仅计算大小，不实际写入）
    bool isDryRunner() {
        return mStart == (uint8_t*) FAKE_DRY_RUNNER_START_ADDR;
    }

    // 获取起始指针
    uint8_t* getStartPtr() {
        return mStart;
    }
    // 获取已写入的字节数
    size_t getBytesWritten() {
        return mCursor - mStart;
    }

    // 写入bool值（小端序）
    void writeBool(bool b) {
        if (!isDryRunner()) {
            mCursor[0] = static_cast<uint8_t>(b);
        }
        mCursor += 1;
    }

    // 写入uint8_t值
    void writeUint8(uint8_t i) {
        if (!isDryRunner()) {
            mCursor[0] = i;
        }
        mCursor += 1;
    }

    // 写入uint16_t值（小端序）
    void writeUint16(uint16_t i) {
        if (!isDryRunner()) {
            mCursor[0] = static_cast<uint8_t>( i        & 0xff);
            mCursor[1] = static_cast<uint8_t>((i >> 8)  & 0xff);
        }
        mCursor += 2;
    }

    // 写入uint32_t值（小端序）
    void writeUint32(uint32_t i) {
        if (!isDryRunner()) {
            mCursor[0] = static_cast<uint8_t>( i        & 0xff);
            mCursor[1] = static_cast<uint8_t>((i >> 8)  & 0xff);
            mCursor[2] = static_cast<uint8_t>((i >> 16) & 0xff);
            mCursor[3] = static_cast<uint8_t>( i >> 24);
        }
        mCursor += 4;
    }

    // 写入uint64_t值（小端序）
    void writeUint64(uint64_t i) {
        if (!isDryRunner()) {
            mCursor[0] = static_cast<uint8_t>( i        & 0xff);
            mCursor[1] = static_cast<uint8_t>((i >> 8)  & 0xff);
            mCursor[2] = static_cast<uint8_t>((i >> 16) & 0xff);
            mCursor[3] = static_cast<uint8_t>((i >> 24) & 0xff);
            mCursor[4] = static_cast<uint8_t>((i >> 32) & 0xff);
            mCursor[5] = static_cast<uint8_t>((i >> 40) & 0xff);
            mCursor[6] = static_cast<uint8_t>((i >> 48) & 0xff);
            mCursor[7] = static_cast<uint8_t>((i >> 56) & 0xff);
        }
        mCursor += 8;
    }

    // 写入C风格字符串（以null结尾）
    void writeString(const char* str) {
        size_t const len = strlen(str);
        if (!isDryRunner()) {
            strcpy(reinterpret_cast<char*>(mCursor), str);
        }
        mCursor += len + 1;
    }

    // 写入string_view字符串（以null结尾）
    void writeString(std::string_view str) {
        size_t const len = str.length();
        if (!isDryRunner()) {
            memcpy(reinterpret_cast<char*>(mCursor), str.data(), len);
            mCursor[len] = 0;
        }
        mCursor += len + 1;
    }

    // 写入blob（二进制数据块），先写入大小，再写入数据
    void writeBlob(const char* blob, size_t nbytes) {
        writeUint64(nbytes);
        if (!isDryRunner()) {
            memcpy(reinterpret_cast<char*>(mCursor), blob, nbytes);
        }
        mCursor += nbytes;
    }

    // 写入原始数据（直接复制，不包含大小）
    void writeRaw(const char* raw, size_t nbytes) {
        if (!isDryRunner()) {
            memcpy(reinterpret_cast<char*>(mCursor), raw, nbytes);
        }
        mCursor += nbytes;
    }

    // 写入大小占位符（稍后可用writeSize填充）
    void writeSizePlaceholder() {
        mSizePlaceholders.push_back(mCursor);
        if (!isDryRunner()) {
            mCursor[0] = 0;
            mCursor[1] = 0;
            mCursor[2] = 0;
            mCursor[3] = 0;
        }
        mCursor += 4;
    }

    // This writes 0 to 7 (inclusive) zeroes, and the subsequent write is guaranteed to be on a
    // 8-byte boundary. Note that the reader must perform a similar calculation to figure out
    // how many bytes to skip.
    // 写入0到7个（包含）零字节，后续写入保证在8字节边界上。
    // 注意读取器必须执行类似的计算以确定要跳过多少字节。
    void writeAlignmentPadding() {
        const intptr_t offset = mCursor - mStart;
        const uint8_t padSize = (8 - (offset % 8)) % 8;
        for (uint8_t i = 0; i < padSize; i++) {
            writeUint8(0);
        }
        assert_invariant(0 == ((mCursor - mStart) % 8));
    }

    // 填充之前写入的大小占位符，返回写入的大小
    uint32_t writeSize() {
        assert(!mSizePlaceholders.empty());

        uint8_t* dst = mSizePlaceholders.back();
        mSizePlaceholders.pop_back();
        // -4 to account for the 4 bytes we are about to write.
        // -4以考虑我们要写入的4个字节
        uint32_t const size = static_cast<uint32_t>(mCursor - dst - 4);
        if (!isDryRunner()) {
            dst[0] = static_cast<uint8_t>( size        & 0xff);
            dst[1] = static_cast<uint8_t>((size >>  8) & 0xff);
            dst[2] = static_cast<uint8_t>((size >> 16) & 0xff);
            dst[3] = static_cast<uint8_t>((size >> 24));
        }
        return size;
    }

    // 写入偏移量占位符（稍后可用writeOffsets填充）
    void writeOffsetPlaceholder(size_t index) {
        mOffsetPlaceholders.insert(std::pair<size_t, uint8_t*>(index, mCursor));
        if (!isDryRunner()) {
            mCursor[0] = 0x0;
            mCursor[1] = 0x0;
            mCursor[2] = 0x0;
            mCursor[3] = 0x0;
        }
        mCursor += 4;
    }

    // 填充指定索引的偏移量占位符
    void writeOffsets(uint32_t forIndex) {
        if (isDryRunner()) {
            return;
        }

        for(auto pair : mOffsetPlaceholders) {
            size_t const index = pair.first;
            if (index != forIndex) {
                continue;
            }
            uint8_t* dst = pair.second;
            // 计算从偏移基址到当前游标的偏移量
            size_t const offset = mCursor - mOffsetsBase;
            if (offset > UINT32_MAX) {
                slog.e << "Unable to write offset greater than UINT32_MAX." << io::endl;
                exit(0);
            }
            dst[0] = static_cast<uint8_t>( offset & 0xff);
            dst[1] = static_cast<uint8_t>((offset >> 8) & 0xff);
            dst[2] = static_cast<uint8_t>((offset >> 16) & 0xff);
            dst[3] = static_cast<uint8_t>((offset >> 24));
        }
    }

    // 写入值占位符（稍后可用writeValue填充）
    void writeValuePlaceholder() {
        mValuePlaceholders.push_back(mCursor);
        if (!isDryRunner()) {
            mCursor[0] = 0;
            mCursor[1] = 0;
            mCursor[2] = 0;
            mCursor[3] = 0;
        }
        mCursor += 4;
    }

    // 填充之前写入的值占位符
    void writeValue(size_t v) {
        assert(!mValuePlaceholders.empty());

        if (v > UINT32_MAX) {
            slog.e << "Unable to write value greater than UINT32_MAX." << io::endl;
            exit(0);
        }

        uint8_t* dst = mValuePlaceholders.back();
        mValuePlaceholders.pop_back();
        if (!isDryRunner()) {
            dst[0] = static_cast<uint8_t>( v        & 0xff);
            dst[1] = static_cast<uint8_t>((v >>  8) & 0xff);
            dst[2] = static_cast<uint8_t>((v >> 16) & 0xff);
            dst[3] = static_cast<uint8_t>((v >> 24));
        }
    }

    // 重置偏移量占位符列表
    void resetOffsets() {
        mOffsetPlaceholders.clear();
    }

    // 标记偏移量基址（用于后续计算相对偏移量）
    void markOffsetBase() {
        mOffsetsBase = mCursor;
    }

private:
    uint8_t* mCursor;
    uint8_t* mStart;
    std::vector<uint8_t*> mSizePlaceholders;
    std::multimap<size_t, uint8_t*> mOffsetPlaceholders;
    std::vector<uint8_t*> mValuePlaceholders;
    uint8_t* mOffsetsBase = nullptr;
};

} // namespace filamat
#endif // TNT_FILAMAT_FLATENNER_H
