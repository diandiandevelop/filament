/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_TYPEDUNIFORMBUFFER_H
#define TNT_FILAMENT_TYPEDUNIFORMBUFFER_H

#include "TypedBuffer.h"

#include <backend/BufferDescriptor.h>
#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <utils/debug.h>

#include <utility>

#include <stddef.h>

namespace filament {

/**
 * 类型化统一缓冲区
 * 
 * 管理 GPU 统一缓冲区对象（UBO）的类型安全包装器。
 * 封装了 TypedBuffer 和 GPU 缓冲区句柄，提供完整的统一缓冲区生命周期管理。
 * 
 * @tparam T 元素类型
 * @tparam N 元素数量（默认 1）
 */
template<typename T, size_t N = 1>
class TypedUniformBuffer {
public:

    /**
     * 默认构造函数
     * 
     * 创建一个未初始化的统一缓冲区。需要调用 init() 来初始化。
     */
    TypedUniformBuffer() noexcept = default;

    /**
     * 构造函数（带驱动）
     * 
     * 创建并初始化统一缓冲区。
     * 
     * @param driver 驱动 API 引用
     */
    explicit TypedUniformBuffer(backend::DriverApi& driver) noexcept {
        init(driver);  // 初始化
    }

    /**
     * 初始化统一缓冲区
     * 
     * 创建 GPU 缓冲区对象。
     * 
     * @param driver 驱动 API 引用
     */
    void init(backend::DriverApi& driver) noexcept {
        assert_invariant(!mUboHandle);  // 断言句柄未初始化
        mUboHandle = driver.createBufferObject(  // 创建缓冲区对象
                mTypedBuffer.getSize(),  // 缓冲区大小
                backend::BufferObjectBinding::UNIFORM,  // 绑定类型：统一缓冲区
                backend::BufferUsage::DYNAMIC);  // 使用方式：动态
    }

    /**
     * 终止统一缓冲区
     * 
     * 销毁 GPU 缓冲区对象。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver) noexcept {
        assert_invariant(mUboHandle);  // 断言句柄已初始化
        driver.destroyBufferObject(std::move(mUboHandle));  // 销毁缓冲区对象
    }

    /**
     * 析构函数
     * 
     * 断言缓冲区对象已被正确销毁。
     */
    ~TypedUniformBuffer() noexcept {
        assert_invariant(!mUboHandle);  // 断言句柄已销毁
    }

    /**
     * 获取类型化缓冲区引用
     * 
     * 返回内部类型化缓冲区的引用，用于直接访问和修改数据。
     * 
     * @return 类型化缓冲区引用
     */
    TypedBuffer<T,N>& getTypedBuffer() noexcept {
        return mTypedBuffer;  // 返回类型化缓冲区引用
    }

    /**
     * 获取 UBO 句柄
     * 
     * 返回 GPU 缓冲区对象的句柄。
     * 
     * @return 缓冲区对象句柄
     */
    backend::BufferObjectHandle getUboHandle() const noexcept {
        return mUboHandle;  // 返回句柄
    }

    /**
     * 获取指定索引的元素（可编辑）
     * 
     * @param i 索引
     * @return 元素引用
     */
    T& itemAt(size_t i) noexcept {
        return mTypedBuffer.itemAt(i);  // 委托给类型化缓冲区
    }

    /**
     * 编辑第一个元素
     * 
     * @return 第一个元素的引用
     */
    T& edit() noexcept {
        return mTypedBuffer.itemAt(0);  // 委托给类型化缓冲区
    }

    /**
     * 获取统一缓冲区大小
     * 
     * @return 大小（字节）
     */
    // size of the uniform buffer in bytes
    size_t getSize() const noexcept { return mTypedBuffer.getSize(); }

    /**
     * 检查是否有 uniform 被修改
     * 
     * @return 如果有修改返回 true，否则返回 false
     */
    // return if any uniform has been changed
    bool isDirty() const noexcept { return mTypedBuffer.isDirty(); }

    /**
     * 标记整个缓冲区为"干净"（没有修改的 uniform）
     */
    // mark the whole buffer as "clean" (no modified uniforms)
    void clean() const noexcept { mTypedBuffer.clean(); }

    /**
     * 辅助函数
     */
    // helper functions
    
    /**
     * 转换为缓冲区描述符
     * 
     * @param driver 驱动 API 引用
     * @return 缓冲区描述符
     */
    backend::BufferDescriptor toBufferDescriptor(backend::DriverApi& driver) const noexcept {
        return mTypedBuffer.toBufferDescriptor(driver);  // 委托给类型化缓冲区
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
    // copy the UBO data and cleans the dirty bits
    backend::BufferDescriptor toBufferDescriptor(
            backend::DriverApi& driver, size_t offset, size_t size) const noexcept {
        return mTypedBuffer.toBufferDescriptor(driver, offset, size);  // 委托给类型化缓冲区
    }

private:
    TypedBuffer<T,N> mTypedBuffer;  // 类型化缓冲区（存储数据）
    backend::BufferObjectHandle mUboHandle;  // GPU 缓冲区对象句柄
};

} // namespace filament


#endif //TNT_FILAMENT_TYPEDUNIFORMBUFFER_H
