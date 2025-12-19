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

#include <filaflat/Unflattener.h>

namespace filaflat {

// 读取blob（二进制数据块），返回指针和大小
bool Unflattener::read(const char** blob, size_t* size) noexcept {
    // 首先读取blob的大小
    uint64_t nbytes;
    if (!read(&nbytes)) {
        return false;
    }
    // 保存起始位置
    const uint8_t* start = mCursor;
    // 移动游标
    mCursor += nbytes;
    // 检查是否溢出
    bool const overflowed = mCursor > mEnd;
    if (!overflowed) {
        *blob = (const char*)start;
        *size = nbytes;
    }
    return !overflowed;
}

// 读取CString类型（以null结尾的字符串）
bool Unflattener::read(utils::CString* const s) noexcept {
    const uint8_t* const start = mCursor;
    const uint8_t* const last = mEnd;
    const uint8_t* curr = start;
    // 查找字符串结束符（null字符）
    while (curr < last && *curr != '\0') {
        curr++;
    }
    bool const overflowed = start >= last;
    if (UTILS_LIKELY(!overflowed)) {
        // 创建CString对象（不包含null字符）
        *s = utils::CString{ (const char*)start, utils::CString::size_type(curr - start) };
        curr++;  // 跳过null字符
    }
    mCursor = curr;
    return !overflowed;
}

// 读取C风格字符串（以null结尾）
bool Unflattener::read(const char** const s) noexcept {
    const uint8_t* const start = mCursor;
    const uint8_t* const last = mEnd;
    const uint8_t* curr = start;
    // 查找字符串结束符（null字符）
    while (curr < last && *curr != '\0') {
        curr++;
    }
    bool const overflowed = start >= last;
    if (UTILS_LIKELY(!overflowed)) {
        // 返回指向字符串的指针
        *s = (char const*)start;
        curr++;  // 跳过null字符
    }
    mCursor = curr;
    return !overflowed;
}

}
