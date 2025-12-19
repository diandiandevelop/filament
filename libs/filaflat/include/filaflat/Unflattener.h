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

#ifndef TNT_FILAFLAT_UNFLATTENER_H
#define TNT_FILAFLAT_UNFLATTENER_H

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/CString.h>

#include <private/filament/Variant.h>

#include <type_traits>

#include <stdint.h>

namespace filaflat {

// Allow read operation from an Unflattenable. All READ operation MUST go through the Unflattener
// since it checks boundaries before readind. All read operations return values MUST be verified,
// never assume a read will succeed.
// 允许从可反序列化对象读取操作。所有读取操作必须通过Unflattener，因为它在读取前检查边界。
// 所有读取操作的返回值必须被验证，永远不要假设读取会成功
class UTILS_PUBLIC Unflattener {
public:
    Unflattener() noexcept = default;

    // 构造函数，使用起始和结束指针初始化
    Unflattener(const uint8_t* src, const uint8_t* end)
            : mSrc(src), mCursor(src), mEnd(end) {
        assert_invariant(src && end);
    }

    Unflattener(Unflattener const& rhs) = default;

    ~Unflattener() noexcept = default;

    // 检查是否还有数据可读
    bool hasData() const noexcept {
        return mCursor < mEnd;
    }

    // 检查读取指定大小是否会溢出
    bool willOverflow(size_t size) const noexcept {
        return (mCursor + size) > mEnd;
    }

    // 跳过对齐填充（对齐到8字节边界）
    void skipAlignmentPadding() {
        const uint8_t padSize = (8 - (intptr_t(mCursor) % 8)) % 8;
        mCursor += padSize;
        assert_invariant(0 == (intptr_t(mCursor) % 8));
    }

    // 读取整数类型（小端序）
    template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    bool read(T* const out) noexcept {
        // 检查是否会溢出
        if (UTILS_UNLIKELY(willOverflow(sizeof(T)))) {
            return false;
        }
        // 保存当前游标位置
        auto const* const cursor = mCursor;
        // 移动游标
        mCursor += sizeof(T);
        // 从小端序字节序列重构值
        T v = 0;
        for (size_t i = 0; i < sizeof(T); i++) {
            v |= T(cursor[i]) << (8 * i);
        }
        *out = v;
        return true;
    }

    // 读取float类型（通过uint32_t读取）
    bool read(float* f) noexcept {
        return read(reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(f)));
    }

    // 读取Variant类型（读取其key成员）
    bool read(filament::Variant* v) noexcept {
        return read(&v->key);
    }

    // 读取CString类型（以null结尾的字符串）
    bool read(utils::CString* s) noexcept;

    // 读取blob（二进制数据块），返回指针和大小
    bool read(const char** blob, size_t* size) noexcept;

    // 读取C风格字符串（以null结尾）
    bool read(const char** s) noexcept;

    // 获取当前游标位置
    const uint8_t* getCursor() const noexcept {
        return mCursor;
    }

    // 设置游标位置（带边界检查）
    void setCursor(const uint8_t* cursor) noexcept {
        mCursor = (cursor >= mSrc && cursor < mEnd) ? cursor : mEnd;
    }

private:
    const uint8_t* mSrc = nullptr;      // 数据起始指针
    const uint8_t* mCursor = nullptr;   // 当前读取位置
    const uint8_t* mEnd = nullptr;      // 数据结束指针
};

} //namespace filaflat

#endif // TNT_FILAFLAT_UNFLATTENER_H
