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

#ifndef TNT_FILAMENT_BACKEND_COMMANDSTREAMVECTOR_H
#define TNT_FILAMENT_BACKEND_COMMANDSTREAMVECTOR_H

#include <backend/DriverApiForward.h>

#include <initializer_list>
#include <memory>

#include <stddef.h>
#include <stdint.h>


namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

/**
 * 从命令流分配内存
 * 
 * 在命令流中分配对齐的内存块，用于存储命令数据。
 * 
 * @param driver 驱动程序 API 引用
 * @param size 要分配的字节数
 * @param alignment 内存对齐要求
 * @return 指向分配内存的指针，失败返回 nullptr
 * 
 * 实现说明：
 * - 内存从命令流的缓冲区中分配
 * - 内存生命周期与命令流绑定
 * - 在命令流执行完成后自动释放
 */
void* allocateFromCommandStream(DriverApi& driver, size_t size, size_t alignment) noexcept;

/**
 * 描述符集偏移数组
 * 
 * 用于存储描述符集中动态偏移量的数组。这些偏移量用于动态 uniform 缓冲区绑定。
 * 
 * 特性：
 * - 内存从命令流分配，生命周期与命令流绑定
 * - 支持移动语义，不支持拷贝
 * - 提供类似 std::vector 的接口
 * 
 * 用途：
 * - Vulkan 后端：存储 VkDescriptorSet 的动态偏移量
 * - 其他后端：可能用于类似目的
 * 
 * 内存管理：
 * - 内存由命令流管理，不需要手动释放
 * - 移动后原对象变为空
 */
class DescriptorSetOffsetArray {
public:
    using value_type = uint32_t;           // 元素类型：32 位无符号整数
    using reference = value_type&;         // 引用类型
    using const_reference = value_type const&;  // 常量引用类型
    using size_type = uint32_t;            // 大小类型
    using difference_type = int32_t;       // 差值类型
    using pointer = value_type*;           // 指针类型
    using const_pointer = value_type const*;    // 常量指针类型
    using iterator = pointer;               // 迭代器类型
    using const_iterator = const_pointer;  // 常量迭代器类型

    /**
     * 默认构造函数
     * 
     * 创建一个空的偏移数组。
     */
    DescriptorSetOffsetArray() noexcept = default;

    /**
     * 析构函数
     * 
     * 不需要手动释放内存，内存由命令流管理。
     */
    ~DescriptorSetOffsetArray() noexcept = default;

    /**
     * 从大小构造
     * 
     * 分配指定大小的偏移数组，所有元素初始化为 0。
     * 
     * @param size 数组大小（元素个数）
     * @param driver 驱动程序 API 引用，用于分配内存
     * 
     * 实现步骤：
     * 1. 从命令流分配对齐的内存（size * sizeof(uint32_t)）
     * 2. 使用 std::uninitialized_fill_n 将所有元素初始化为 0
     */
    DescriptorSetOffsetArray(size_type size, DriverApi& driver) noexcept {
        mOffsets = (value_type *)allocateFromCommandStream(driver,
                size * sizeof(value_type), alignof(value_type));
        std::uninitialized_fill_n(mOffsets, size, 0);
    }

    /**
     * 从初始化列表构造
     * 
     * 从初始化列表创建偏移数组，并复制所有值。
     * 
     * @param list 初始化列表，包含要复制的偏移值
     * @param driver 驱动程序 API 引用，用于分配内存
     * 
     * 实现步骤：
     * 1. 从命令流分配对齐的内存（list.size() * sizeof(uint32_t)）
     * 2. 使用 std::uninitialized_copy 复制初始化列表中的值
     */
    DescriptorSetOffsetArray(std::initializer_list<uint32_t> list, DriverApi& driver) noexcept {
        mOffsets = (value_type *)allocateFromCommandStream(driver,
                list.size() * sizeof(value_type), alignof(value_type));
        std::uninitialized_copy(list.begin(), list.end(), mOffsets);
    }

    /**
     * 禁用拷贝构造
     * 
     * 因为内存由命令流管理，不支持拷贝。
     */
    DescriptorSetOffsetArray(DescriptorSetOffsetArray const&) = delete;
    
    /**
     * 禁用拷贝赋值
     */
    DescriptorSetOffsetArray& operator=(DescriptorSetOffsetArray const&) = delete;

    /**
     * 移动构造函数
     * 
     * 转移所有权，原对象变为空。
     * 
     * @param rhs 要移动的源对象
     * 
     * 实现步骤：
     * 1. 复制指针
     * 2. 将源对象的指针设置为 nullptr
     */
    DescriptorSetOffsetArray(DescriptorSetOffsetArray&& rhs) noexcept
            : mOffsets(rhs.mOffsets) {
        rhs.mOffsets = nullptr;
    }

    /**
     * 移动赋值操作符
     * 
     * 转移所有权，原对象变为空。
     * 
     * @param rhs 要移动的源对象
     * @return 当前对象的引用
     * 
     * 实现步骤：
     * 1. 检查自赋值
     * 2. 复制指针
     * 3. 将源对象的指针设置为 nullptr
     */
    DescriptorSetOffsetArray& operator=(DescriptorSetOffsetArray&& rhs) noexcept {
        if (this != &rhs) {
            mOffsets = rhs.mOffsets;
            rhs.mOffsets = nullptr;
        }
        return *this;
    }

    /**
     * 检查数组是否为空
     * 
     * @return 如果数组为空返回 true，否则返回 false
     */
    bool empty() const noexcept { return mOffsets == nullptr; }

    /**
     * 获取数据指针（非 const）
     * 
     * @return 指向数组数据的指针
     */
    value_type* data() noexcept { return mOffsets; }
    
    /**
     * 获取数据指针（const）
     * 
     * @return 指向数组数据的常量指针
     */
    const value_type* data() const noexcept { return mOffsets; }

    /**
     * 下标操作符（非 const）
     * 
     * 访问指定索引的元素。
     * 
     * @param n 元素索引
     * @return 元素的引用
     * 
     * 实现：返回 data() + n 处的元素引用
     */
    reference operator[](size_type n) noexcept {
        return *(data() + n);
    }

    /**
     * 下标操作符（const）
     * 
     * 访问指定索引的元素（只读）。
     * 
     * @param n 元素索引
     * @return 元素的常量引用
     */
    const_reference operator[](size_type n) const noexcept {
        return *(data() + n);
    }

    /**
     * 清空数组
     * 
     * 将数组设置为空状态。注意：不会释放内存，内存由命令流管理。
     */
    void clear() noexcept {
        mOffsets = nullptr;
    }

private:
    /**
     * 偏移数组数据指针
     * 
     * 指向从命令流分配的内存，存储偏移值。
     * nullptr 表示空数组。
     */
    value_type *mOffsets = nullptr;
};

} // namespace filament::backend

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::DescriptorSetOffsetArray& rhs);
#endif

#endif //TNT_FILAMENT_BACKEND_COMMANDSTREAMVECTOR_H
