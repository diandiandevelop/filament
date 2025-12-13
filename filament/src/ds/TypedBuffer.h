/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_TYPEDBUFFER_H
#define TNT_FILAMENT_TYPEDBUFFER_H

#include <private/backend/DriverApi.h>

#include <backend/BufferDescriptor.h>

#include <stddef.h>
#include <string.h>

namespace filament {

/**
 * 类型化缓冲区
 * 
 * 类型安全的统一缓冲区包装器，提供脏标记跟踪和缓冲区描述符转换功能。
 * 
 * @tparam T 元素类型
 * @tparam N 元素数量（默认 1）
 */
template<typename T, size_t N = 1>
class TypedBuffer { // NOLINT(cppcoreguidelines-pro-type-member-init)
public:

    /**
     * 获取指定索引的元素（可编辑）
     * 
     * 访问元素时自动标记缓冲区为脏。
     * 
     * @param i 索引
     * @return 元素引用
     */
    T& itemAt(size_t i) noexcept {
        mSomethingDirty = true;  // 标记为脏
        return mBuffer[i];  // 返回元素引用
    }

    /**
     * 编辑第一个元素
     * 
     * 便捷方法，用于编辑单个元素的缓冲区。
     * 
     * @return 第一个元素的引用
     */
    T& edit() noexcept {
        return itemAt(0);  // 返回第一个元素
    }

    /**
     * 获取统一缓冲区大小
     * 
     * 计算缓冲区总大小（字节）。
     * 
     * @return 大小（字节）
     */
    // size of the uniform buffer in bytes
    size_t getSize() const noexcept { return sizeof(T) * N; }

    /**
     * 检查是否有 uniform 被修改
     * 
     * 返回缓冲区是否被标记为脏（有修改）。
     * 
     * @return 如果有修改返回 true，否则返回 false
     */
    // return if any uniform has been changed
    bool isDirty() const noexcept { return mSomethingDirty; }

    /**
     * 标记整个缓冲区为"干净"（没有修改的 uniform）
     * 
     * 清除脏标记，通常在数据已提交到 GPU 后调用。
     */
    // mark the whole buffer as "clean" (no modified uniforms)
    void clean() const noexcept { mSomethingDirty = false; }

    /**
     * 辅助函数
     */
    // helper functions

    /**
     * 转换为缓冲区描述符
     * 
     * 将整个缓冲区转换为缓冲区描述符。
     * 
     * @param driver 驱动 API 引用
     * @return 缓冲区描述符
     */
    backend::BufferDescriptor toBufferDescriptor(backend::DriverApi& driver) const noexcept {
        return toBufferDescriptor(driver, 0, getSize());  // 转换整个缓冲区
    }

    /**
     * 转换为缓冲区描述符（指定范围）
     * 
     * 复制 UBO 数据并清理脏位。
     * 从驱动分配内存并复制缓冲区数据到该内存。
     * 
     * @param driver 驱动 API 引用
     * @param offset 偏移量（字节）
     * @param size 大小（字节）
     * @return 缓冲区描述符（包含分配的内存和大小）
     */
    // copy the UBO data and cleans the dirty bits
    backend::BufferDescriptor toBufferDescriptor(
            backend::DriverApi& driver, size_t const offset, size_t const size) const noexcept {
        backend::BufferDescriptor p;  // 创建缓冲区描述符
        p.size = size;  // 设置大小
        p.buffer = driver.allocate(p.size);  // 从驱动分配内存
        // TODO: use out-of-line buffer if too large
        memcpy(p.buffer, reinterpret_cast<const char*>(mBuffer) + offset, p.size);  // 复制数据（内联）
        // inlined
        clean();  // 清理脏标记
        return p;  // 返回描述符
    }

private:
    T mBuffer[N];  // 缓冲区数组（存储 N 个类型为 T 的元素）
    mutable bool mSomethingDirty = false;  // 脏标记（mutable 因为 clean 是 const）
};

} // namespace filament

#endif // TNT_FILAMENT_TYPEDBUFFER_H
