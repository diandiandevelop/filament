/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "UniformBuffer.h"

#include <utils/Allocator.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/mat3.h>

#include <utility>

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

using namespace filament::math;

namespace filament {

/**
 * UniformBuffer 构造函数
 * 
 * @param size 缓冲区大小（字节）
 */
UniformBuffer::UniformBuffer(size_t const size) noexcept
        : mBuffer(mStorage),
          mSize(uint32_t(size)),
          mSomethingDirty(true) {
    /**
     * 如果大小超过本地存储，使用堆分配
     */
    if (UTILS_LIKELY(size > sizeof(mStorage))) {
        mBuffer = alloc(size);
    }
    /**
     * 初始化缓冲区为零
     */
    memset(mBuffer, 0, size);
}

/**
 * UniformBuffer 移动构造函数
 * 
 * @param rhs 源对象
 */
UniformBuffer::UniformBuffer(UniformBuffer&& rhs) noexcept
        : mBuffer(rhs.mBuffer),
          mSize(rhs.mSize),
          mSomethingDirty(rhs.mSomethingDirty) {
    /**
     * 如果源使用本地存储，需要复制数据
     */
    if (UTILS_LIKELY(rhs.isLocalStorage())) {
        mBuffer = mStorage;
        memcpy(mBuffer, rhs.mBuffer, mSize);
    }
    /**
     * 清空源对象
     */
    rhs.mBuffer = nullptr;
    rhs.mSize = 0;
}

/**
 * UniformBuffer 移动赋值操作符
 * 
 * @param rhs 源对象
 * @return 自身引用
 */
UniformBuffer& UniformBuffer::operator=(UniformBuffer&& rhs) noexcept {
    if (this != &rhs) {
        mSomethingDirty = rhs.mSomethingDirty;
        if (UTILS_LIKELY(rhs.isLocalStorage())) {
            /**
             * 如果源使用本地存储，复制数据
             */
            mBuffer = mStorage;
            mSize = rhs.mSize;
            memcpy(mBuffer, rhs.mBuffer, rhs.mSize);
        } else {
            /**
             * 否则交换指针和大小
             */
            std::swap(mBuffer, rhs.mBuffer);
            std::swap(mSize, rhs.mSize);
        }
    }
    return *this;
}

/**
 * 设置统一缓冲区（从另一个缓冲区）
 * 
 * @param rhs 源缓冲区
 * @return 自身引用
 */
UniformBuffer& UniformBuffer::setUniforms(const UniformBuffer& rhs) noexcept {
    if (this != &rhs) {
        if (UTILS_UNLIKELY(mSize != rhs.mSize)) {
            /**
             * 如果大小不同，先释放我们的存储（如果有）
             */
            if (mBuffer && !isLocalStorage()) {
                free(mBuffer, mSize);
            }
            /**
             * 然后分配新存储
             */
            mBuffer = mStorage;
            mSize = rhs.mSize;
            if (mSize > sizeof(mStorage)) {
                mBuffer = alloc(mSize);
            }
        }
        /**
         * 复制数据
         */
        memcpy(mBuffer, rhs.mBuffer, rhs.mSize);
        /**
         * 始终失效化自己
         */
        invalidate();
    }
    return *this;
}

/**
 * 分配内存
 * 
 * 这些分配具有较长的生命周期。
 * 
 * @param size 大小（字节）
 * @return 分配的内存指针
 */
void* UniformBuffer::alloc(size_t const size) noexcept {
    return malloc(size);
}

/**
 * 释放内存
 * 
 * @param addr 内存地址
 * @param size 大小（未使用）
 */
void UniformBuffer::free(void* addr, size_t) noexcept {
    ::free(addr);
}

// ------------------------------------------------------------------------------------------------

/**
 * 设置统一缓冲区值（无类型版本，模板实现）
 * 
 * @param offset 偏移量（字节）
 * @param v 值指针
 */
template<size_t Size>
void UniformBuffer::setUniformUntyped(size_t const offset, void const* UTILS_RESTRICT v) noexcept{
    /**
     * 仅在值实际改变时更新（优化）
     */
    if (UTILS_LIKELY(invalidateNeeded<Size>(offset, v))) {
        void* const addr = getUniformAddress(offset);
        setUniformUntyped<Size>(addr, v);
        invalidateUniforms(offset, Size);
    }
}

/**
 * 显式模板实例化（常见大小）
 */
template
void UniformBuffer::setUniformUntyped<4ul>(size_t offset, void const* UTILS_RESTRICT v) noexcept;
template
void UniformBuffer::setUniformUntyped<8ul>(size_t offset, void const* UTILS_RESTRICT v) noexcept;
template
void UniformBuffer::setUniformUntyped<12ul>(size_t offset, void const* UTILS_RESTRICT v) noexcept;
template
void UniformBuffer::setUniformUntyped<16ul>(size_t offset, void const* UTILS_RESTRICT v) noexcept;
template
void UniformBuffer::setUniformUntyped<64ul>(size_t offset, void const* UTILS_RESTRICT v) noexcept;

/**
 * 设置统一缓冲区数组（无类型版本，模板实现）
 * 
 * 考虑 std140 对齐：每个数组元素对齐到 vec4（16 字节）边界。
 * 
 * @param offset 偏移量（字节）
 * @param begin 数组起始指针
 * @param count 元素数量
 */
template<size_t Size>
void UniformBuffer::setUniformArrayUntyped(size_t const offset, void const* UTILS_RESTRICT begin, size_t const count) noexcept {
    /**
     * 计算步长（对齐到 16 字节）
     */
    constexpr size_t stride = (Size + 0xFu) & ~0xFu;
    /**
     * 逐个设置数组元素
     */
    for (size_t i = 0; i < count; i++) {
        setUniformUntyped<Size>(offset + i * stride, static_cast<const char *>(begin) + i * Size);
    }
}

/**
 * 显式模板实例化（常见大小）
 */
template
void UniformBuffer::setUniformArrayUntyped<4ul>(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;
template
void UniformBuffer::setUniformArrayUntyped<8ul>(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;
template
void UniformBuffer::setUniformArrayUntyped<12ul>(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;
template
void UniformBuffer::setUniformArrayUntyped<16ul>(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;
template
void UniformBuffer::setUniformArrayUntyped<64ul>(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;

#if !defined(NDEBUG)

utils::io::ostream& operator<<(utils::io::ostream& out, const UniformBuffer& rhs) {
    return out << "UniformBuffer(data=" << rhs.getBuffer() << ", size=" << rhs.getSize() << ")";
}

#endif
} // namespace filament
