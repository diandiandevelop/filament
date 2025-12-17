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

#ifndef TNT_FILAMENT_BACKEND_HANDLE_H
#define TNT_FILAMENT_BACKEND_HANDLE_H

#include <utils/debug.h>

#include <type_traits> // FIXME: STL headers are not allowed in public headers
#include <utility>

#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

/**
 * 硬件资源类型前向声明
 * 
 * 这些是不透明类型，仅用于类型安全的句柄系统。
 * 实际定义在后端实现中。
 */
struct HwBufferObject;          // 缓冲区对象（用于存储任意数据）
struct HwFence;                 // 栅栏（用于同步 GPU 操作）
struct HwIndexBuffer;           // 索引缓冲区（用于存储索引数据）
struct HwProgram;               // 着色器程序（编译后的着色器）
struct HwRenderPrimitive;       // 渲染图元（顶点和索引的组合）
struct HwRenderTarget;          // 渲染目标（帧缓冲区）
struct HwStream;                // 流（外部纹理流）
struct HwSwapChain;             // 交换链（用于呈现到屏幕）
struct HwSync;                  // 同步对象（用于同步）
struct HwTexture;               // 纹理（图像数据）
struct HwTimerQuery;            // 计时查询（用于性能测量）
struct HwVertexBufferInfo;      // 顶点缓冲区信息（布局信息）
struct HwVertexBuffer;          // 顶点缓冲区（用于存储顶点数据）
struct HwDescriptorSetLayout;   // 描述符堆布局（资源绑定布局）
struct HwDescriptorSet;         // 描述符堆（资源绑定集合）
struct HwMemoryMappedBuffer;    // 内存映射缓冲区（可映射的缓冲区）

/**
 * 后端资源句柄基类
 * 
 * HandleBase 是后端资源的句柄基类，仅用于内部使用。
 * 
 * 重要约束：
 * - HandleBase 必须是平凡类型（trivial），即不能有用户定义的拷贝或移动构造函数
 * - 这是为了确保句柄可以在命令流中安全传递
 * - 句柄只是一个 ID，不包含实际的资源指针
 * 
 * 设计理念：
 * - 句柄是类型安全的资源标识符
 * - 句柄不拥有资源，只是引用
 * - 句柄可以安全地复制和传递
 */
//! \privatesection

class HandleBase {
public:
    using HandleId = uint32_t;  // 句柄 ID 类型
    static constexpr const HandleId nullid = HandleId{ UINT32_MAX };  // 空句柄 ID（最大值）

    /**
     * 默认构造函数
     * 
     * 创建一个空句柄（未初始化）。
     */
    constexpr HandleBase() noexcept: object(nullid) {}

    /**
     * 检查句柄是否已初始化
     * 
     * @return 如果句柄已初始化返回 true，否则返回 false
     */
    explicit operator bool() const noexcept { return object != nullid; }

    /**
     * 清空句柄
     * 
     * 将句柄设置为空，但不释放关联的资源。
     * 资源释放由 Driver 负责。
     */
    void clear() noexcept { object = nullid; }

    /**
     * 获取句柄 ID
     * 
     * @return 句柄 ID
     */
    HandleId getId() const noexcept { return object; }

    /**
     * 从 ID 初始化句柄（仅内部使用）
     * 
     * @param id 句柄 ID
     * 
     * 注意：id 不能为 nullid，否则断言失败
     */
    explicit HandleBase(HandleId id) noexcept : object(id) {
        assert_invariant(object != nullid); // 通常意味着使用了未初始化的句柄
    }

protected:
    /**
     * 受保护的拷贝构造函数
     * 
     * 允许派生类（Handle<T>）进行拷贝，但不允许外部直接拷贝 HandleBase。
     * 使用默认实现，简单复制句柄 ID。
     */
    HandleBase(HandleBase const& rhs) noexcept = default;
    
    /**
     * 受保护的拷贝赋值操作符
     * 
     * 允许派生类进行拷贝赋值。
     */
    HandleBase& operator=(HandleBase const& rhs) noexcept = default;

    /**
     * 受保护的移动构造函数
     * 
     * 转移句柄所有权，原对象变为空。
     * 
     * @param rhs 要移动的源对象
     * 
     * 实现步骤：
     * 1. 复制句柄 ID
     * 2. 将源对象的 ID 设置为 nullid
     */
    HandleBase(HandleBase&& rhs) noexcept
            : object(rhs.object) {
        rhs.object = nullid;
    }

    /**
     * 受保护的移动赋值操作符
     * 
     * 转移句柄所有权，原对象变为空。
     * 
     * @param rhs 要移动的源对象
     * @return 当前对象的引用
     * 
     * 实现步骤：
     * 1. 检查自赋值
     * 2. 复制句柄 ID
     * 3. 将源对象的 ID 设置为 nullid
     */
    HandleBase& operator=(HandleBase&& rhs) noexcept {
        if (this != &rhs) {
            object = rhs.object;
            rhs.object = nullid;
        }
        return *this;
    }

private:
    /**
     * 句柄 ID
     * 
     * 存储实际的句柄标识符。
     * - nullid (UINT32_MAX) 表示空句柄
     * - 其他值表示有效的资源 ID
     */
    HandleId object;
};

/**
 * 类型安全的后端资源句柄
 * 
 * Handle 是类型安全的资源句柄，提供编译时类型检查。
 * 
 * @tparam T 资源类型（必须是 HwBase 的派生类）
 * 
 * 特性：
 * - 类型安全：不同资源类型的句柄不能混用
 * - 可比较：支持所有比较操作符
 * - 可转换：可以从基类句柄转换（如果 T 是 B 的基类）
 * 
 * 使用示例：
 * ```cpp
 * Handle<HwTexture> texture = ...;
 * if (texture) {  // 检查是否有效
 *     // 使用 texture
 * }
 * ```
 */
template<typename T>
struct Handle : public HandleBase {

    /**
     * 默认构造函数
     * 
     * 创建一个空句柄。
     */
    Handle() noexcept = default;

    /**
     * 拷贝构造函数
     */
    Handle(Handle const& rhs) noexcept = default;
    
    /**
     * 移动构造函数
     */
    Handle(Handle&& rhs) noexcept = default;

    /**
     * 拷贝赋值操作符
     * 
     * 显式重定义而不是使用默认实现，因为在某些编译器（如 NDK 25.1.8937393 及以下）
     * 中，std::move 函数调用时不会自动调用父类的方法。
     * 参见：https://en.cppreference.com/w/cpp/algorithm/move 和 b/371980551
     */
    Handle& operator=(Handle const& rhs) noexcept {
        HandleBase::operator=(rhs);
        return *this;
    }
    
    /**
     * 移动赋值操作符
     * 
     * 显式重定义而不是使用默认实现（原因同上）。
     */
    Handle& operator=(Handle&& rhs) noexcept {
        HandleBase::operator=(std::move(rhs));
        return *this;
    }

    /**
     * 从 ID 构造句柄
     * 
     * @param id 句柄 ID
     */
    explicit Handle(HandleId id) noexcept : HandleBase(id) { }

    /**
     * 比较操作符
     * 
     * 比较相同类型句柄的 ID。
     */
    bool operator==(const Handle& rhs) const noexcept { return getId() == rhs.getId(); }
    bool operator!=(const Handle& rhs) const noexcept { return getId() != rhs.getId(); }
    bool operator<(const Handle& rhs) const noexcept { return getId() < rhs.getId(); }
    bool operator<=(const Handle& rhs) const noexcept { return getId() <= rhs.getId(); }
    bool operator>(const Handle& rhs) const noexcept { return getId() > rhs.getId(); }
    bool operator>=(const Handle& rhs) const noexcept { return getId() >= rhs.getId(); }

    /**
     * 类型安全的句柄转换
     * 
     * 允许从基类句柄转换为派生类句柄（如果 T 是 B 的基类）。
     * 
     * @tparam B 基类类型
     */
    template<typename B, typename = std::enable_if_t<std::is_base_of_v<T, B>> >
    Handle(Handle<B> const& base) noexcept : HandleBase(base) { } // NOLINT(hicpp-explicit-conversions,google-explicit-constructor)

private:
#if !defined(NDEBUG)
    template <typename U>
    friend utils::io::ostream& operator<<(utils::io::ostream& out, const Handle<U>& h) noexcept;
#endif
};

/**
 * 命令流使用的类型别名
 * 
 * 为所有硬件资源类型定义类型别名，用于命令流。
 * 使用类型别名是因为宏系统不能很好地处理 "<" 和 ">"。
 */
using BufferObjectHandle        = Handle<HwBufferObject>;         // 缓冲区对象句柄
using FenceHandle               = Handle<HwFence>;                // 栅栏句柄
using IndexBufferHandle         = Handle<HwIndexBuffer>;          // 索引缓冲区句柄
using ProgramHandle             = Handle<HwProgram>;              // 着色器程序句柄
using RenderPrimitiveHandle     = Handle<HwRenderPrimitive>;      // 渲染图元句柄
using RenderTargetHandle        = Handle<HwRenderTarget>;         // 渲染目标句柄
using StreamHandle              = Handle<HwStream>;                // 流句柄
using SwapChainHandle           = Handle<HwSwapChain>;            // 交换链句柄
using SyncHandle                = Handle<HwSync>;                 // 同步对象句柄
using TextureHandle             = Handle<HwTexture>;              // 纹理句柄
using TimerQueryHandle          = Handle<HwTimerQuery>;          // 计时查询句柄
using VertexBufferHandle        = Handle<HwVertexBuffer>;         // 顶点缓冲区句柄
using VertexBufferInfoHandle    = Handle<HwVertexBufferInfo>;     // 顶点缓冲区信息句柄
using DescriptorSetLayoutHandle = Handle<HwDescriptorSetLayout>; // 描述符堆布局句柄
using DescriptorSetHandle       = Handle<HwDescriptorSet>;        // 描述符堆句柄
using MemoryMappedBufferHandle  = Handle<HwMemoryMappedBuffer>;   // 内存映射缓冲区句柄

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_HANDLE_H
