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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_HANDLEALLOCATOR_H
#define TNT_FILAMENT_BACKEND_PRIVATE_HANDLEALLOCATOR_H

#include <backend/Handle.h>

#include <utils/Allocator.h>
#include <utils/CString.h>
#include <utils/ImmutableCString.h>
#include <utils/Log.h>
#include <utils/Panic.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <tsl/robin_map.h>

#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <stddef.h>
#include <stdint.h>

/**
 * 不同后端的 HandleAllocator 类型别名
 * 
 * 模板参数说明（P0, P1, P2）：
 * - P0: 小对象池大小（字节）
 * - P1: 中对象池大小（字节）
 * - P2: 大对象池大小（字节）
 * 
 * 每个后端根据其 Handle 对象的大小分布选择不同的池大小。
 */
#define HandleAllocatorGL   HandleAllocator<32,  96, 184>    // ~4520 / pool / MiB
// OpenGL 后端：小对象 32 字节，中对象 96 字节，大对象 184 字节
#define HandleAllocatorVK   HandleAllocator<64, 160, 312>    // ~1820 / pool / MiB
// Vulkan 后端：小对象 64 字节，中对象 160 字节，大对象 312 字节
#define HandleAllocatorMTL  HandleAllocator<32,  64, 552>    // ~1660 / pool / MiB
// Metal 后端：小对象 32 字节，中对象 64 字节，大对象 552 字节
// TODO WebGPU examine right size of handles
#define HandleAllocatorWGPU HandleAllocator<64, 160, 552>    // ~1820 / pool / MiB
// WebGPU 后端：小对象 64 字节，中对象 160 字节，大对象 552 字节

namespace filament::backend {

// This is used to not duplicate the code for the Tags management
/**
 * DebugTag（调试标签）
 * 
 * 用于管理 Handle 的调试标签，避免代码重复。
 * 
 * 功能：
 * - 为 Handle 关联调试标签（用于调试和错误报告）
 * - 支持池 Handle 和堆 Handle 的标签管理
 * - 线程安全：标签写入只在主驱动线程，但可以从任何线程读取
 */
class DebugTag {
public:
    DebugTag();
    
    /**
     * 写入池 Handle 的标签
     * 
     * @param key Handle ID（已截断年龄位）
     * @param tag 调试标签（移动语义）
     */
    void writePoolHandleTag(HandleBase::HandleId key, utils::ImmutableCString&& tag) noexcept;
    
    /**
     * 写入堆 Handle 的标签
     * 
     * @param key Handle ID
     * @param tag 调试标签（移动语义）
     */
    void writeHeapHandleTag(HandleBase::HandleId key, utils::ImmutableCString&& tag) noexcept;
    
    /**
     * 查找 Handle 的标签
     * 
     * @param key Handle ID
     * @return 调试标签，如果不存在返回空字符串
     */
    utils::ImmutableCString findHandleTag(HandleBase::HandleId key) const noexcept;

private:
    // This is used to associate a tag to a handle. mDebugTags is only written the in the main
    // driver thread, but it can be accessed from any thread, because it's called from handle_cast<>
    // which is used by synchronous calls.
    // 用于将标签关联到 Handle。mDebugTags 只在主驱动线程写入，但可以从任何线程访问，
    // 因为它从 handle_cast<> 调用，而 handle_cast<> 用于同步调用。
    mutable utils::Mutex mDebugTagLock;
    // 调试标签锁：保护调试标签映射表的访问
    tsl::robin_map<HandleBase::HandleId, utils::ImmutableCString> mDebugTags;
    // 调试标签映射表：从 Handle ID 到调试标签的映射
};

/*
 * A utility class to efficiently allocate and manage Handle<>
 */
/**
 * HandleAllocator（Handle 分配器）
 * 
 * 高效分配和管理 Handle<> 对象的工具类。
 * 
 * 设计特点：
 * - 三级池分配器：根据对象大小选择不同的内存池（P0, P1, P2）
 * - 使用后释放检测：通过年龄（age）机制检测 use-after-free
 * - 调试标签支持：为每个 Handle 关联调试标签
 * - 线程安全：使用互斥锁保护并发访问
 * 
 * Handle ID 编码：
 * - 低 27 位：索引（在池中的偏移）
 * - 第 27-30 位：年龄（age，用于检测 use-after-free）
 * - 第 31 位：堆标志（0=池 Handle，1=堆 Handle）
 * 
 * @tparam P0 小对象池大小（字节）
 * @tparam P1 中对象池大小（字节）
 * @tparam P2 大对象池大小（字节）
 */
template<size_t P0, size_t P1, size_t P2>
class HandleAllocator : public DebugTag {
public:
    /**
     * 构造函数
     * 
     * @param name 分配器名称（用于调试）
     * @param size 分配器大小（字节）
     */
    HandleAllocator(const char* name, size_t size);
    
    /**
     * 构造函数（带选项）
     * 
     * @param name 分配器名称（用于调试）
     * @param size 分配器大小（字节）
     * @param disableUseAfterFreeCheck 是否禁用使用后释放检测
     * @param disableHeapHandleTags 是否禁用堆 Handle 标签
     */
    HandleAllocator(const char* name, size_t size,
            bool disableUseAfterFreeCheck, bool disableHeapHandleTags);
    
    HandleAllocator(HandleAllocator const& rhs) = delete;
    HandleAllocator& operator=(HandleAllocator const& rhs) = delete;
    
    /**
     * 析构函数
     */
    ~HandleAllocator() noexcept;

    /*
     * Constructs a D object and returns a Handle<D>
     *
     * e.g.:
     *  struct ConcreteTexture : public HwTexture {
     *      ConcreteTexture(int w, int h);
     *  };
     *  Handle<ConcreteTexture> h = allocateAndConstruct(w, h);
     *
     */
    /**
     * 分配并构造对象
     * 
     * 分配内存并就地构造对象，返回 Handle。
     * 
     * @tparam D 对象类型（必须是 Handle 管理的类型）
     * @tparam ARGS 构造参数类型
     * @param args 构造参数（转发）
     * @return 对象的 Handle
     * 
     * 示例：
     * ```cpp
     * struct ConcreteTexture : public HwTexture {
     *     ConcreteTexture(int w, int h);
     * };
     * Handle<ConcreteTexture> h = allocateAndConstruct(w, h);
     * ```
     */
    template<typename D, typename ... ARGS>
    Handle<D> allocateAndConstruct(ARGS&& ... args) {
        Handle<D> h{ allocateHandle<D>() };
        D* addr = handle_cast<D*>(h);
        new(addr) D(std::forward<ARGS>(args)...);
        return h;
    }

    /*
     * Allocates (without constructing) a D object and returns a Handle<D>
     *
     * e.g.:
     *  struct ConcreteTexture : public HwTexture {
     *      ConcreteTexture(int w, int h);
     *  };
     *  Handle<ConcreteTexture> h = allocate();
     *
     */
    /**
     * 分配对象（不构造）
     * 
     * 仅分配内存，不构造对象。需要后续调用 construct() 构造对象。
     * 
     * @tparam D 对象类型
     * @return 对象的 Handle（对象未构造）
     * 
     * 示例：
     * ```cpp
     * struct ConcreteTexture : public HwTexture {
     *     ConcreteTexture(int w, int h);
     * };
     * Handle<ConcreteTexture> h = allocate();
     * // 后续需要调用 construct(h, w, h) 构造对象
     * ```
     */
    template<typename D>
    Handle<D> allocate() {
        Handle<D> h{ allocateHandle<D>() };
        return h;
    }


    /*
     * Destroys the object D at Handle<B> and construct a new D in its place
     * e.g.:
     *  Handle<ConcreteTexture> h = allocateAndConstruct(w, h);
     *  ConcreteTexture* p = destroyAndConstruct(h, w, h);
     */
    /**
     * 销毁并重新构造对象
     * 
     * 在 Handle 指向的位置销毁旧对象并构造新对象。
     * 
     * @tparam D 新对象类型（必须是 B 的派生类）
     * @tparam B 基类类型
     * @tparam ARGS 构造参数类型
     * @param handle 对象的 Handle
     * @param args 构造参数（转发）
     * @return 新对象的指针
     * 
     * 注意：当前实现使用析构+构造，也可以使用 operator=，但所有析构函数都是平凡的，
     * ~D() 实际上是空操作。
     * 
     * 示例：
     * ```cpp
     * Handle<ConcreteTexture> h = allocateAndConstruct(w, h);
     * ConcreteTexture* p = destroyAndConstruct(h, w, h);
     * ```
     */
    template<typename D, typename B, typename ... ARGS>
    std::enable_if_t<std::is_base_of_v<B, D>, D>*
    destroyAndConstruct(Handle<B> const& handle, ARGS&& ... args) {
        assert_invariant(handle);
        D* addr = handle_cast<D*>(const_cast<Handle<B>&>(handle));
        assert_invariant(addr);
        // currently we implement construct<> with dtor+ctor, we could use operator= also
        // but all our dtors are trivial, ~D() is actually a noop.
        // 当前我们使用析构+构造实现 construct<>，也可以使用 operator=，
        // 但所有析构函数都是平凡的，~D() 实际上是空操作。
        addr->~D();
        new(addr) D(std::forward<ARGS>(args)...);
        return addr;
    }

    /*
     * Construct a new D at Handle<B>
     * e.g.:
     *  Handle<ConcreteTexture> h = allocate();
     *  ConcreteTexture* p = construct(h, w, h);
     */
    /**
     * 在 Handle 指向的位置构造对象
     * 
     * 在已分配的内存上构造对象（不销毁旧对象）。
     * 
     * @tparam D 对象类型（必须是 B 的派生类）
     * @tparam B 基类类型
     * @tparam ARGS 构造参数类型
     * @param handle 对象的 Handle（必须已分配但未构造）
     * @param args 构造参数（转发）
     * @return 对象的指针
     * 
     * 示例：
     * ```cpp
     * Handle<ConcreteTexture> h = allocate();
     * ConcreteTexture* p = construct(h, w, h);
     * ```
     */
    template<typename D, typename B, typename ... ARGS>
    std::enable_if_t<std::is_base_of_v<B, D>, D>*
    construct(Handle<B> const& handle, ARGS&& ... args) noexcept {
        assert_invariant(handle);
        D* addr = handle_cast<D*>(const_cast<Handle<B>&>(handle));
        assert_invariant(addr);
        new(addr) D(std::forward<ARGS>(args)...);
        return addr;
    }

    /*
     * Destroy the object D at Handle<B> and frees Handle<B>
     * e.g.:
     *      Handle<HwTexture> h = ...;
     *      ConcreteTexture* p = handle_cast<ConcreteTexture*>(h);
     *      deallocate(h, p);
     */
    /**
     * 释放对象（带指针参数）
     * 
     * 销毁对象并释放 Handle。
     * 
     * @tparam B 基类类型
     * @tparam D 对象类型（必须是 B 的派生类）
     * @param handle 对象的 Handle（调用后会被置为无效）
     * @param p 对象指针（允许为 nullptr，类似 operator delete）
     * 
     * 示例：
     * ```cpp
     * Handle<HwTexture> h = ...;
     * ConcreteTexture* p = handle_cast<ConcreteTexture*>(h);
     * deallocate(h, p);
     * ```
     */
    template <typename B, typename D,
            typename = std::enable_if_t<std::is_base_of_v<B, D>, D>>
    void deallocate(Handle<B>& handle, D const* p) noexcept {
        // allow to destroy the nullptr, similarly to operator delete
        // 允许销毁 nullptr，类似 operator delete
        if (p) {
            p->~D();
            deallocateHandle<D>(handle.getId());
        }
    }

    /*
     * Destroy the object D at Handle<B> and frees Handle<B>
     * e.g.:
     *      Handle<HwTexture> h = ...;
     *      deallocate<GLTexture>(h);
     */
    /**
     * 释放对象（自动获取指针）
     * 
     * 从 Handle 获取对象指针，然后销毁对象并释放 Handle。
     * 
     * @tparam D 对象类型（必须是 B 的派生类）
     * @tparam B 基类类型
     * @param handle 对象的 Handle（调用后会被置为无效）
     * 
     * 示例：
     * ```cpp
     * Handle<HwTexture> h = ...;
     * deallocate<GLTexture>(h);
     * ```
     */
    template<typename D, typename B,
            typename = std::enable_if_t<std::is_base_of_v<B, D>, D>>
    void deallocate(Handle<B>& handle) noexcept {
        D const* d = handle_cast<const D*>(handle);
        deallocate(handle, d);
    }

    /*
     * returns a D* from a Handle<B>. B must be a base of D.
     * e.g.:
     *      Handle<HwTexture> h = ...;
     *      ConcreteTexture* p = handle_cast<ConcreteTexture*>(h);
     */
    /**
     * 将 Handle 转换为对象指针
     * 
     * 从 Handle 获取对象指针，并进行使用后释放检测。
     * 
     * @tparam Dp 指针类型（必须是 B 的派生类指针）
     * @tparam B 基类类型
     * @param handle 对象的 Handle
     * @return 对象指针
     * 
     * 使用后释放检测：
     * - 池 Handle：检查年龄（age）是否匹配
     * - 堆 Handle：检查指针是否有效，以及索引是否已分配
     * 
     * 示例：
     * ```cpp
     * Handle<HwTexture> h = ...;
     * ConcreteTexture* p = handle_cast<ConcreteTexture*>(h);
     * ```
     */
    template<typename Dp, typename B>
    inline std::enable_if_t<
            std::is_pointer_v<Dp> &&
            std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp>
    handle_cast(Handle<B>& handle) {
        assert_invariant(handle);
        auto [p, tag] = handleToPointer(handle.getId());

        if (isPoolHandle(handle.getId())) {
            // check for pool handle use-after-free
            // 检查池 Handle 的使用后释放
            if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled)) {
                // 从 tag 中提取年龄
                uint8_t const age = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
                auto const pNode = static_cast<typename Allocator::Node*>(p);
                // 从节点中获取期望的年龄（存储在分配前的内存中）
                uint8_t const expectedAge = pNode[-1].age;
                // getHandleTag() is only called if the check fails.
                // getHandleTag() 只在检查失败时调用。
                FILAMENT_CHECK_POSTCONDITION(expectedAge == age)
                        << "use-after-free of Handle with id=" << handle.getId()
                        << ", tag=" << getHandleTag(handle.getId()).c_str_safe();
            }
        } else {
            // check for heap handle use-after-free
            // 检查堆 Handle 的使用后释放
            if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled)) {
                HandleBase::HandleId const index = (handle.getId() & HANDLE_INDEX_MASK);
                // if we've already handed out this handle index before, it's definitely a
                // use-after-free, otherwise it's probably just a corrupted handle
                // 如果我们之前已经分配过这个 handle 索引，那肯定是使用后释放，
                // 否则可能只是损坏的 handle
                if (index < mId.load(std::memory_order_relaxed)) {
                    FILAMENT_CHECK_POSTCONDITION(p != nullptr)
                            << "use-after-free of heap Handle with id=" << handle.getId()
                            << ", tag=" << getHandleTag(handle.getId()).c_str_safe();
                } else {
                    FILAMENT_CHECK_POSTCONDITION(p != nullptr)
                            << "corrupted heap Handle with id=" << handle.getId()
                            << ", tag=" << getHandleTag(handle.getId()).c_str_safe();
                }
            }
        }

        return static_cast<Dp>(p);
    }

    /**
     * 获取 Handle 的调试标签
     * 
     * @param key Handle ID
     * @return 调试标签，如果不存在返回空字符串
     */
    utils::ImmutableCString getHandleTag(HandleBase::HandleId key) const noexcept;

    /**
     * 检查 Handle 是否有效
     * 
     * 验证 Handle 是否指向有效的对象。
     * 
     * @tparam B 基类类型
     * @param handle 要检查的 Handle
     * @return 如果 Handle 有效返回 true，否则返回 false
     * 
     * 检查逻辑：
     * - null Handle 无效
     * - 池 Handle：检查年龄是否匹配
     * - 堆 Handle：检查指针是否非空
     */
    template<typename B>
    bool is_valid(Handle<B>& handle) {
        if (!handle) {
            // null handles are invalid
            // null Handle 无效
            return false;
        }
        auto [p, tag] = handleToPointer(handle.getId());
        if (isPoolHandle(handle.getId())) {
            // 池 Handle：检查年龄是否匹配
            uint8_t const age = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
            auto const pNode = static_cast<typename Allocator::Node*>(p);
            uint8_t const expectedAge = pNode[-1].age;
            return expectedAge == age;
        }
        // 堆 Handle：检查指针是否非空
        return p != nullptr;
    }

    /**
     * 将 Handle 转换为对象指针（const 版本）
     * 
     * @tparam Dp 指针类型
     * @tparam B 基类类型
     * @param handle 对象的 Handle（const）
     * @return 对象指针
     */
    template<typename Dp, typename B>
    inline std::enable_if_t<
            std::is_pointer_v<Dp> &&
            std::is_base_of_v<B, std::remove_pointer_t<Dp>>, Dp>
    handle_cast(Handle<B> const& handle) {
        return handle_cast<Dp>(const_cast<Handle<B>&>(handle));
    }

    /**
     * 将标签关联到 Handle
     * 
     * 为 Handle 设置调试标签，用于调试和错误报告。
     * 
     * @param id Handle ID
     * @param tag 调试标签（移动语义）
     * 
     * 处理逻辑：
     * - 空标签会被忽略
     * - 池 Handle：截断年龄位以获取调试标签键
     * - 堆 Handle：如果未禁用堆 Handle 标签，则写入标签
     */
    void associateTagToHandle(HandleBase::HandleId id, utils::ImmutableCString&& tag) noexcept {
        if (tag.empty()) {
            return;
        }
        uint32_t key = id;
        if (UTILS_LIKELY(isPoolHandle(id))) {
            // Truncate the age to get the debug tag
            // 截断年龄位以获取调试标签
            key &= ~(HANDLE_DEBUG_TAG_MASK ^ HANDLE_AGE_MASK);
            writePoolHandleTag(key, std::move(tag));
        } else {
            if (!mHeapHandleTagsDisabled) {
                writeHeapHandleTag(key, std::move(tag));
            }
        }
    }

private:

    /**
     * 获取对象所属的桶大小
     * 
     * 根据对象大小选择合适的内存池。
     * 
     * @tparam D 对象类型
     * @return 桶大小（P0, P1 或 P2）
     */
    template<typename D>
    static constexpr size_t getBucketSize() noexcept {
        if constexpr (sizeof(D) <= P0) { return P0; }
        if constexpr (sizeof(D) <= P1) { return P1; }
        static_assert(sizeof(D) <= P2);
        return P2;
    }

    /**
     * Allocator（分配器）
     * 
     * 三级池分配器，根据对象大小选择不同的内存池。
     * 
     * 设计特点：
     * - 三个固定大小的内存池（P0, P1, P2）
     * - 每个分配前存储年龄（age）用于检测 use-after-free
     * - 支持 double-free 检测
     */
    class Allocator {
        friend class HandleAllocator;
        static constexpr size_t MIN_ALIGNMENT = alignof(std::max_align_t);
        // 最小对齐：最大对齐要求
        struct Node { uint8_t age; };
        // 节点：存储年龄（用于 use-after-free 检测）
        // Note: using the `extra` parameter of PoolAllocator<>, even with a 1-byte structure,
        // generally increases all pool allocations by 8-bytes because of alignment restrictions.
        // 注意：使用 PoolAllocator<> 的 `extra` 参数，即使只有 1 字节结构，
        // 由于对齐限制，通常也会使所有池分配增加 8 字节。
        template<size_t SIZE>
        using Pool = utils::PoolAllocator<SIZE, MIN_ALIGNMENT, sizeof(Node)>;
        // 池类型：使用 PoolAllocator，额外存储 Node
        Pool<P0> mPool0;  // 小对象池
        Pool<P1> mPool1;  // 中对象池
        Pool<P2> mPool2;  // 大对象池
        UTILS_UNUSED_IN_RELEASE const utils::AreaPolicy::HeapArea& mArea;
        // 堆区域（仅在调试模式下使用）
        bool mUseAfterFreeCheckDisabled;
        // 是否禁用使用后释放检测
    public:
        /**
         * 构造函数
         * 
         * @param area 堆区域
         * @param disableUseAfterFreeCheck 是否禁用使用后释放检测
         */
        explicit Allocator(const utils::AreaPolicy::HeapArea& area, bool disableUseAfterFreeCheck);

        /**
         * 获取对齐要求
         * 
         * @return 最小对齐大小
         */
        static constexpr size_t getAlignment() noexcept { return MIN_ALIGNMENT; }

        // this is in fact always called with a constexpr size argument
        /**
         * 分配内存
         * 
         * 根据大小选择合适的内存池进行分配。
         * 
         * @param size 分配大小
         * @param outAge 输出参数：返回分配的内存中存储的年龄
         * @return 分配的内存指针，如果分配失败返回 nullptr
         * 
         * 注意：实际上总是用 constexpr 大小参数调用。
         */
        [[nodiscard]] inline void* alloc(size_t size, size_t, size_t, uint8_t* outAge) noexcept {
            void* p = nullptr;
            // 根据大小选择合适的内存池
            if      (size <= mPool0.getSize()) p = mPool0.alloc(size);
            else if (size <= mPool1.getSize()) p = mPool1.alloc(size);
            else if (size <= mPool2.getSize()) p = mPool2.alloc(size);
            if (UTILS_LIKELY(p)) {
                Node const* const pNode = static_cast<Node const*>(p);
                // we are guaranteed to have at least sizeof<Node> bytes of extra storage before
                // the allocation address.
                // 我们保证在分配地址之前至少有 sizeof<Node> 字节的额外存储。
                // 从分配前的内存中读取年龄
                *outAge = pNode[-1].age;
            }
            return p;
        }

        // this is in fact always called with a constexpr size argument
        /**
         * 释放内存
         * 
         * 根据大小选择合适的内存池进行释放，并检测 double-free。
         * 
         * @param p 要释放的内存指针
         * @param size 释放大小
         * @param age 期望的年龄（用于 double-free 检测）
         * 
         * 注意：实际上总是用 constexpr 大小参数调用。
         */
        inline void free(void* p, size_t size, uint8_t age) noexcept {
            assert_invariant(p >= mArea.begin() && (char*)p + size <= (char*)mArea.end());

            // check for double-free
            // 检查 double-free
            Node* const pNode = static_cast<Node*>(p);
            uint8_t& expectedAge = pNode[-1].age;
            if (UTILS_UNLIKELY(!mUseAfterFreeCheckDisabled)) {
                // 检查年龄是否匹配（如果不匹配，说明是 double-free）
                FILAMENT_CHECK_POSTCONDITION(expectedAge == age) <<
                        "double-free of Handle of size " << size << " at " << p;
            }
            // 增加年龄（循环，0-15）
            expectedAge = (expectedAge + 1) & 0xF; // fixme

            // 根据大小选择合适的内存池进行释放
            if (size <= mPool0.getSize()) { mPool0.free(p); return; }
            if (size <= mPool1.getSize()) { mPool1.free(p); return; }
            if (size <= mPool2.getSize()) { mPool2.free(p); return; }
        }
    };

// FIXME: We should be using a Spinlock here, at least on platforms where mutexes are not
//        efficient (i.e. non-Linux). However, we've seen some hangs on that spinlock, which
//        we don't understand well (b/308029108).
// 修复：我们应该在这里使用 Spinlock，至少在互斥锁效率不高的平台上（即非 Linux）。
// 但是，我们在那个自旋锁上看到了一些挂起，我们不太理解（b/308029108）。
#ifndef NDEBUG
    using HandleArena = utils::Arena<Allocator,
            utils::LockingPolicy::Mutex,
            utils::TrackingPolicy::DebugAndHighWatermark>;
    // Handle 内存区域（调试模式）：使用互斥锁和调试跟踪策略
#else
    using HandleArena = utils::Arena<Allocator,
            utils::LockingPolicy::Mutex>;
    // Handle 内存区域（发布模式）：使用互斥锁
#endif

    // allocateHandle()/deallocateHandle() selects the pool to use at compile-time based on the
    // allocation size this is always inlined, because all these do is to call
    // allocateHandleInPool()/deallocateHandleFromPool() with the right pool size.
    /**
     * 分配 Handle
     * 
     * 根据对象大小在编译时选择合适的内存池。
     * 总是内联，因为它只是用正确的池大小调用 allocateHandleInPool()。
     * 
     * @tparam D 对象类型
     * @return Handle ID
     */
    template<typename D>
    HandleBase::HandleId allocateHandle() {
        constexpr size_t BUCKET_SIZE = getBucketSize<D>();
        return allocateHandleInPool<BUCKET_SIZE>();
    }

    /**
     * 释放 Handle
     * 
     * 根据对象大小在编译时选择合适的内存池。
     * 总是内联，因为它只是用正确的池大小调用 deallocateHandleFromPool()。
     * 
     * @tparam D 对象类型
     * @param id Handle ID
     */
    template<typename D>
    void deallocateHandle(HandleBase::HandleId id) noexcept {
        constexpr size_t BUCKET_SIZE = getBucketSize<D>();
        deallocateHandleFromPool<BUCKET_SIZE>(id);
    }

    // allocateHandleInPool()/deallocateHandleFromPool() is NOT inlined, which will cause three
    // versions to be generated, one for each pool. Because the arena is synchronized,
    // the code generated is not trivial (even if it's not insane either).
    /**
     * 从指定大小的池中分配 Handle
     * 
     * 不从内联，这会导致生成三个版本，每个池一个。
     * 因为 arena 是同步的，生成的代码不是平凡的（虽然也不是疯狂的）。
     * 
     * @tparam SIZE 池大小
     * @return Handle ID，如果分配失败则调用 allocateHandleSlow()
     */
    template<size_t SIZE>
    UTILS_NOINLINE
    HandleBase::HandleId allocateHandleInPool() {
        uint8_t age;
        void* p = mHandleArena.alloc(SIZE, alignof(std::max_align_t), 0, &age);
        if (UTILS_LIKELY(p)) {
            // 将年龄编码到 tag 中
            uint32_t const tag = (uint32_t(age) << HANDLE_AGE_SHIFT) & HANDLE_AGE_MASK;
            return arenaPointerToHandle(p, tag);
        }
        // 池分配失败，使用慢路径（堆分配）
        return allocateHandleSlow(SIZE);
    }

    /**
     * 从指定大小的池中释放 Handle
     * 
     * 不从内联，这会导致生成三个版本，每个池一个。
     * 
     * @tparam SIZE 池大小
     * @param id Handle ID
     */
    template<size_t SIZE>
    UTILS_NOINLINE
    void deallocateHandleFromPool(HandleBase::HandleId id) noexcept {
        if (UTILS_LIKELY(isPoolHandle(id))) {
            // 池 Handle：从池中释放
            auto [p, tag] = handleToPointer(id);
            uint8_t const age = (tag & HANDLE_AGE_MASK) >> HANDLE_AGE_SHIFT;
            mHandleArena.free(p, SIZE, age);
        } else {
            // 堆 Handle：使用慢路径释放
            deallocateHandleSlow(id, SIZE);
        }
    }

    // number if bits allotted to the handle's age (currently 4 max)
    // Handle 年龄的位数（当前最多 4 位）
    static constexpr uint32_t HANDLE_AGE_BIT_COUNT = 4;
    // number if bits allotted to the handle's debug tag (HANDLE_AGE_BIT_COUNT max)
    // Handle 调试标签的位数（最多 HANDLE_AGE_BIT_COUNT）
    static constexpr uint32_t HANDLE_DEBUG_TAG_BIT_COUNT = 2;
    // bit shift for both the age and debug tag
    // 年龄和调试标签的位偏移
    static constexpr uint32_t HANDLE_AGE_SHIFT = 27;
    // mask for the heap (vs pool) flag
    // 堆（vs 池）标志的掩码
    static constexpr uint32_t HANDLE_HEAP_FLAG = 0x80000000u;
    // mask for the age
    // 年龄的掩码
    static constexpr uint32_t HANDLE_AGE_MASK =
            ((1 << HANDLE_AGE_BIT_COUNT) - 1) << HANDLE_AGE_SHIFT;
    // mask for the debug tag
    // 调试标签的掩码
    static constexpr uint32_t HANDLE_DEBUG_TAG_MASK =
            ((1 << HANDLE_DEBUG_TAG_BIT_COUNT) - 1) << HANDLE_AGE_SHIFT;
    // mask for the index
    // 索引的掩码
    static constexpr uint32_t HANDLE_INDEX_MASK = 0x07FFFFFFu;

    static_assert(HANDLE_DEBUG_TAG_BIT_COUNT <= HANDLE_AGE_BIT_COUNT);

    /**
     * 检查 Handle 是否是池 Handle
     * 
     * @param id Handle ID
     * @return 如果是池 Handle 返回 true，否则返回 false
     */
    static bool isPoolHandle(HandleBase::HandleId id) noexcept {
        return (id & HANDLE_HEAP_FLAG) == 0u;
    }

    /**
     * 慢路径：分配 Handle（堆分配）
     * 
     * 当池分配失败时使用堆分配。
     * 
     * @param size 分配大小
     * @return Handle ID
     */
    HandleBase::HandleId allocateHandleSlow(size_t size);
    
    /**
     * 慢路径：释放 Handle（堆释放）
     * 
     * @param id Handle ID
     * @param size 释放大小
     */
    void deallocateHandleSlow(HandleBase::HandleId id, size_t size) noexcept;

    // We inline this because it's just 4 instructions in the fast case
    /**
     * 将 Handle ID 转换为指针和标签
     * 
     * 内联，因为在快速情况下只有 4 条指令。
     * 
     * @param id Handle ID
     * @return 指针和标签的 pair
     * 
     * 注意：null Handle 最终会返回 nullptr，因为它会被处理为非池 Handle。
     */
   std::pair<void*, uint32_t> handleToPointer(HandleBase::HandleId id) const noexcept {
        // note: the null handle will end-up returning nullptr b/c it'll be handled as
        // a non-pool handle.
        // 注意：null Handle 最终会返回 nullptr，因为它会被处理为非池 Handle。
        if (UTILS_LIKELY(isPoolHandle(id))) {
            // 池 Handle：快速路径
            char* const base = (char*)mHandleArena.getArea().begin();
            uint32_t const tag = id & HANDLE_AGE_MASK;
            // 计算偏移（索引 × 对齐大小）
            size_t const offset = (id & HANDLE_INDEX_MASK) * Allocator::getAlignment();
            return { static_cast<void*>(base + offset), tag };
        }
        // 堆 Handle：慢路径
        return { handleToPointerSlow(id), 0 };
    }

    /**
     * 慢路径：将 Handle ID 转换为指针（堆 Handle）
     * 
     * @param id Handle ID
     * @return 对象指针
     */
    void* handleToPointerSlow(HandleBase::HandleId id) const noexcept;

    // We inline this because it's just 3 instructions
    /**
     * 将 arena 指针转换为 Handle ID
     * 
     * 内联，因为只有 3 条指令。
     * 
     * @param p 对象指针
     * @param tag 标签（包含年龄）
     * @return Handle ID
     */
   HandleBase::HandleId arenaPointerToHandle(void* p, uint32_t tag) const noexcept {
        char* const base = (char*)mHandleArena.getArea().begin();
        size_t const offset = (char*)p - base;
        assert_invariant((offset % Allocator::getAlignment()) == 0);
        // 计算索引（偏移 / 对齐大小）
        auto id = HandleBase::HandleId(offset / Allocator::getAlignment());
        // 添加标签（年龄）
        id |= tag & HANDLE_AGE_MASK;
        // 确保不是堆 Handle
        assert_invariant((id & HANDLE_HEAP_FLAG) == 0);
        return id;
    }

    HandleArena mHandleArena;
    // Handle 内存区域：主要的分配器

    // Below is only used when running out of space in the HandleArena
    // 下面仅在 HandleArena 空间不足时使用
    mutable utils::Mutex mLock;
    // 锁：保护溢出映射表的访问
    tsl::robin_map<HandleBase::HandleId, void*> mOverflowMap;
    // 溢出映射表：当池空间不足时，使用堆分配的 Handle 映射
    std::atomic<HandleBase::HandleId> mId = 0;
    // 原子计数器：用于生成堆 Handle 的唯一 ID

    // constants
    // 常量
    const bool mUseAfterFreeCheckDisabled;
    // 是否禁用使用后释放检测
    const bool mHeapHandleTagsDisabled;
    // 是否禁用堆 Handle 标签
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_HANDLEALLOCATOR_H
