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
 * 类型安全的统一缓冲区包装器。
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
     * @param i 索引
     * @return 元素引用
     */
    T& itemAt(size_t i) noexcept {
        mSomethingDirty = true;
        return mBuffer[i];
    }

    /**
     * 编辑第一个元素
     * 
     * @return 第一个元素的引用
     */
    T& edit() noexcept {
        return itemAt(0);
    }

    /**
     * 获取统一缓冲区大小
     * 
     * @return 大小（字节）
     */
    size_t getSize() const noexcept { return sizeof(T) * N; }

    /**
     * 检查是否有 uniform 被修改
     * 
     * @return true 如果有修改
     */
    bool isDirty() const noexcept { return mSomethingDirty; }

    /**
     * 标记整个缓冲区为"干净"（没有修改的 uniform）
     */
    void clean() const noexcept { mSomethingDirty = false; }

    /**
     * 辅助函数
     */

    /**
     * 转换为缓冲区描述符
     * 
     * @param driver 驱动 API 引用
     * @return 缓冲区描述符
     */
    backend::BufferDescriptor toBufferDescriptor(backend::DriverApi& driver) const noexcept {
        return toBufferDescriptor(driver, 0, getSize());
    }

    /**
     * 转换为缓冲区描述符（指定范围）
     * 
     * 复制 UBO 数据并清理脏位。
     * 
     * @param driver 驱动 API 引用
     * @param offset 偏移量（字节）
     * @param size 大小（字节）
     * @return 缓冲区描述符
     */
    backend::BufferDescriptor toBufferDescriptor(
            backend::DriverApi& driver, size_t offset, size_t size) const noexcept {
        backend::BufferDescriptor p;
        p.size = size;
        p.buffer = driver.allocate(p.size); // TODO: 如果太大，使用离线缓冲区
        memcpy(p.buffer, reinterpret_cast<const char*>(mBuffer) + offset, p.size); // 内联
        clean();
        return p;
    }

private:
    T mBuffer[N];  // 缓冲区数组
    mutable bool mSomethingDirty = false;  // 脏标记
};

} // namespace filament

#endif // TNT_FILAMENT_TYPEDBUFFER_H
