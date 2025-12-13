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

#ifndef TNT_FILAMENT_UNIFORMBUFFER_H
#define TNT_FILAMENT_UNIFORMBUFFER_H

#include "private/backend/DriverApi.h"

#include <backend/BufferDescriptor.h>

#include <utils/compiler.h>
#include <utils/debug.h>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <type_traits>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace filament {

/**
 * 统一缓冲区
 * 
 * 用于管理着色器统一缓冲区（UBO）的数据。
 * 支持小缓冲区的本地存储优化（96 字节），大缓冲区使用堆分配。
 * 
 * 特性：
 * - 脏标记机制（仅更新修改的部分）
 * - 支持 std140 布局对齐
 * - 类型安全的 uniform 设置
 * - 支持数组（考虑 std140 对齐）
 */
class UniformBuffer { // NOLINT(cppcoreguidelines-pro-type-member-init)
public:
    UniformBuffer() noexcept = default;

    /**
     * 构造函数
     * 
     * 创建指定大小的统一缓冲区（字节）。
     * 
     * @param size 缓冲区大小（字节）
     */
    explicit UniformBuffer(size_t size) noexcept;

    /**
     * 禁止拷贝构造（因为很重）
     */
    UniformBuffer(const UniformBuffer& rhs) = delete;

    /**
     * 禁止拷贝赋值（使 UniformBuffer 的分配不可变）
     */
    UniformBuffer& operator=(const UniformBuffer& rhs) = delete;

    /**
     * 移动构造函数
     */
    UniformBuffer(UniformBuffer&& rhs) noexcept;

    /**
     * 移动赋值操作符
     * 
     * 可以从临时对象移动赋值。
     */
    UniformBuffer& operator=(UniformBuffer&& rhs) noexcept;

    ~UniformBuffer() noexcept {
        // inline this because there is no point in jumping into the library, just to
        // immediately jump into libc's free()
        if (mBuffer && !isLocalStorage()) {
            // test not necessary but avoids a call to libc (and this is a common enough case)
            free(mBuffer, mSize);
        }
    }

    /**
     * 设置统一缓冲区（从另一个缓冲区）
     * 
     * @param rhs 源缓冲区
     * @return 自身引用
     */
    UniformBuffer& setUniforms(const UniformBuffer& rhs) noexcept;

    /**
     * 检查范围是否需要失效化
     * 
     * 通过比较当前值来判断。
     * 
     * @param offset 偏移量（字节）
     * @param v 新值指针
     * @return true 如果值不同（需要更新）
     */
    template<size_t Size>
    bool invalidateNeeded(size_t const offset, void const* UTILS_RESTRICT v) const {
        assert_invariant(offset + Size <= mSize);
        void* const UTILS_RESTRICT addr = getUniformAddress(offset);
        return bool(memcmp(addr, v, Size)); // 内联
    }

    /**
     * 失效化统一缓冲区范围
     * 
     * 标记指定范围的 uniform 为脏，offset 和 size 以字节为单位。
     * 
     * @param offset 偏移量（字节）
     * @param size 大小（字节）
     */
    void invalidateUniforms(size_t const offset, size_t const size) const {
        assert_invariant(offset + size <= mSize);
        mSomethingDirty = true;
    }

    /**
     * 失效化整个缓冲区
     */
    void invalidate() const noexcept {
        invalidateUniforms(0, mSize);
    }

    /**
     * 获取统一缓冲区指针
     * 
     * @return 缓冲区指针
     */
    void const* getBuffer() const noexcept { return mBuffer; }

    /**
     * 获取统一缓冲区大小
     * 
     * @return 大小（字节）
     */
    size_t getSize() const noexcept { return mSize; }

    /**
     * 检查是否有 uniform 被修改
     * 
     * @return true 如果有修改
     */
    bool isDirty() const noexcept { return mSomethingDirty; }

    /**
     * 标记整个缓冲区为干净（没有修改的 uniform）
     */
    void clean() const noexcept { mSomethingDirty = false; }

    /**
     * -----------------------------------------------
     * 类型已知情况下的内联辅助函数
     * -----------------------------------------------
     */

    /**
     * 支持的类型检查
     * 
     * 注意：我们故意不包含需要转换的类型（例如 bool 和 bool 向量）。
     */
    template <typename T>
    struct is_supported_type {
        using type = std::enable_if_t<
                std::is_same_v<float, T> ||
                std::is_same_v<int32_t, T> ||
                std::is_same_v<uint32_t, T> ||
                std::is_same_v<math::quatf, T> ||
                std::is_same_v<math::int2, T> ||
                std::is_same_v<math::int3, T> ||
                std::is_same_v<math::int4, T> ||
                std::is_same_v<math::uint2, T> ||
                std::is_same_v<math::uint3, T> ||
                std::is_same_v<math::uint4, T> ||
                std::is_same_v<math::float2, T> ||
                std::is_same_v<math::float3, T> ||
                std::is_same_v<math::float4, T> ||
                std::is_same_v<math::mat3f, T> ||
                std::is_same_v<math::mat4f, T>
        >;
    };

    /**
     * 设置统一缓冲区数组
     * 
     * 失效化统一缓冲区数组并返回指向第一个元素的指针。
     * 
     * offset 以字节为单位，count 是要失效化的元素数量。
     * 注意：Filament 将大小为 1 的数组视为等同于标量。
     * 
     * 为了计算数组占用的空间，我们考虑 std140 对齐，它指定
     * 每个数组元素的开始对齐到 vec4 的大小。
     * 考虑一个包含三个 float 的数组，它在内存中的布局如下，其中每个字母是一个 32 位字：
     * 
     *      a x x x b x x x c
     * 
     * "x" 符号表示虚拟字。
     * 
     * @param offset 偏移量（字节）
     * @param begin 数组起始指针
     * @param count 元素数量
     */
    template<typename T, typename = typename is_supported_type<T>::type>
    UTILS_ALWAYS_INLINE
    void setUniformArray(size_t const offset, T const* UTILS_RESTRICT begin, size_t const count) noexcept {
        static_assert(!std::is_same_v<T, math::mat3f>);
        setUniformArrayUntyped<sizeof(T)>(offset, begin, count);
    }

    /**
     * 设置统一缓冲区值（静态版本）
     * 
     * （参见下面的 mat3f 特化）
     * 
     * @param addr 地址
     * @param v 值
     */
    template<typename T, typename = typename is_supported_type<T>::type>
    UTILS_ALWAYS_INLINE
    static void setUniform(void* addr, const T& v) noexcept {
        static_assert(!std::is_same_v<T, math::mat3f>);
        setUniformUntyped<sizeof(T)>(addr, &v);
    }

    /**
     * 设置统一缓冲区值
     * 
     * @param offset 偏移量（字节，例如：使用 offsetof()）
     * @param v 值
     */
    template<typename T, typename = typename is_supported_type<T>::type>
    UTILS_ALWAYS_INLINE
    void setUniform(size_t const offset, const T& v) noexcept {
        static_assert(!std::is_same_v<T, math::mat3f>);
        setUniformUntyped<sizeof(T)>(offset, &v);
    }

    /**
     * 获取统一缓冲区值
     * 
     * 从适当的偏移量获取已知类型的 uniform（例如：使用 offsetof()）。
     * 
     * @param offset 偏移量（字节）
     * @return uniform 值
     */
    template<typename T, typename = typename is_supported_type<T>::type>
    T getUniform(size_t const offset) const noexcept {
        static_assert(!std::is_same_v<T, math::mat3f>);
        return *reinterpret_cast<T const*>(static_cast<char const*>(mBuffer) + offset);
    }

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
            backend::DriverApi& driver, size_t const offset, size_t const size) const noexcept {
        backend::BufferDescriptor p;
        p.size = size;
        p.buffer = driver.allocate(p.size); // TODO: 如果太大，使用离线缓冲区
        memcpy(p.buffer, static_cast<const char*>(getBuffer()) + offset, p.size); // 内联
        clean();
        return p;
    }

    /**
     * 设置统一缓冲区值（无类型版本）
     * 
     * 将已知类型的 uniform 设置到适当的偏移量（例如：使用 offsetof()）。
     * 
     * @param offset 偏移量（字节）
     * @param v 值指针
     */
    template<size_t Size>
    void setUniformUntyped(size_t offset, void const* UTILS_RESTRICT v) noexcept;

    /**
     * 设置统一缓冲区数组（无类型版本）
     * 
     * @param offset 偏移量（字节）
     * @param begin 数组起始指针
     * @param count 元素数量
     */
    template<size_t Size>
    void setUniformArrayUntyped(size_t offset, void const* UTILS_RESTRICT begin, size_t count) noexcept;

private:

#if !defined(NDEBUG)
    /**
     * 友元：流输出操作符（仅调试版本）
     */
    friend utils::io::ostream& operator<<(utils::io::ostream& out, const UniformBuffer& rhs);
#endif
    
    /**
     * 分配内存
     * 
     * 分配指定大小的内存。小缓冲区使用本地存储，大缓冲区使用堆分配。
     * 
     * @param size 大小（字节）
     * @return 内存指针
     */
    static void* alloc(size_t size) noexcept;
    
    /**
     * 释放内存
     * 
     * 释放之前分配的内存。仅释放堆分配的内存，本地存储不需要释放。
     * 
     * @param addr 内存指针
     * @param size 大小（字节）
     */
    static void free(void* addr, size_t size) noexcept;

    /**
     * 设置统一缓冲区值（无类型版本，优化大小）
     * 
     * 仅对特定大小（4、8、12、16、64 字节）进行特化，以优化性能。
     * 
     * @tparam Size 大小（必须是 4、8、12、16 或 64）
     * @param addr 地址
     * @param v 值指针
     */
    template<size_t Size, std::enable_if_t<
            Size == 4 || Size == 8 || Size == 12 || Size == 16 || Size == 64, bool> = true>
    UTILS_ALWAYS_INLINE
    static void setUniformUntyped(void* UTILS_RESTRICT addr, void const* v) noexcept {
        memcpy(addr, v, Size); // 内联复制
    }

    /**
     * 检查是否使用本地存储
     * 
     * @return 如果使用本地存储返回 true
     */
    bool isLocalStorage() const noexcept { return mBuffer == mStorage; }

    /**
     * 获取统一缓冲区地址
     * 
     * 根据偏移量获取统一缓冲区的地址。
     * 
     * @param offset 偏移量（字节）
     * @return 地址指针
     */
    void* getUniformAddress(size_t const offset) const noexcept {
        return static_cast<char*>(mBuffer) + offset;  // 计算地址
    }

    char mStorage[96];  // 本地存储（6 行 = 6 x vec4 x 4 字节 = 96 字节）
    void *mBuffer = nullptr;  // 缓冲区指针（指向 mStorage 或堆分配的内存）
    uint32_t mSize = 0;  // 缓冲区大小（字节）
    mutable bool mSomethingDirty = false;  // 脏标记（标记是否有 uniform 被修改）
    // 这里有 3 个填充字节（用于对齐）
};

/**
 * mat3f 特化（具有不同的对齐方式，参见 std140 布局规则）
 * 
 * 我们声明它但不定义它，以确保它永远不会被调用。
 */
template<>
void UniformBuffer::setUniform(void* addr, const math::mat3f& v) noexcept;

/**
 * mat3f 特化（具有不同的对齐方式，参见 std140 布局规则）
 * 
 * 作为三个 float3 的数组处理（因此对齐为 16）
 */
template<>
inline void UniformBuffer::setUniform(size_t const offset, const math::mat3f& v) noexcept {
    setUniformArrayUntyped<sizeof(math::float3)>(offset, &v, 3);
}

/**
 * mat3f 数组特化
 * 
 * 将每个 mat3 视为 3 个 float3 的数组。
 */
template<>
inline void UniformBuffer::setUniformArray(
        size_t const offset, math::mat3f const* UTILS_RESTRICT begin, size_t const count) noexcept {
    // 将每个 mat3 视为 3 个 float3 的数组
    setUniformArray(offset, reinterpret_cast<math::float3 const*>(begin), count * 3);
}

/**
 * mat3f 获取特化
 */
template<>
inline math::mat3f UniformBuffer::getUniform(size_t const offset) const noexcept {
    math::float4 const* p = reinterpret_cast<math::float4 const*>(
            static_cast<char const*>(mBuffer) + offset);
    return { p[0].xyz, p[1].xyz, p[2].xyz };
}

} // namespace filament

#endif // TNT_FILAMENT_UNIFORMBUFFER_H
