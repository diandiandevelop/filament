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

#include "private/backend/HandleAllocator.h"

#include <backend/Handle.h>

#include <utils/Allocator.h>
#include <utils/CString.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <mutex>
#include <utility>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace filament::backend {

using namespace utils;

/**
 * Allocator 构造函数
 * 
 * 初始化句柄分配器，设置三个不同大小的内存池。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param area 堆内存区域
 * @param disableUseAfterFreeCheck 是否禁用释放后使用检查
 * 
 * 设计说明：
 * - 此分配器可以生成的最大句柄取决于架构的最小对齐（通常是 8 或 16 字节）
 * - 例如：在 Android armv8 上，对齐是 16 字节，对于 1 MiB 堆，最大句柄索引是 65536
 * - 注意：这不是句柄数量（句柄数量总是更少）
 * - 由于当前最大可表示句柄是 0x07FFFFFF，最大合理的堆大小是 2 GiB，
 *   在 GL 情况下每个池大约 760 万个句柄
 */
template <size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
HandleAllocator<P0, P1, P2>::Allocator::Allocator(AreaPolicy::HeapArea const& area,
        bool disableUseAfterFreeCheck)
        : mArea(area),
          mUseAfterFreeCheckDisabled(disableUseAfterFreeCheck) {

    // 此分配器可以生成的最大句柄取决于架构的最小对齐，通常是 8 或 16 字节。
    // 例如：在 Android armv8 上，对齐是 16 字节，对于 1 MiB 堆，最大句柄索引是 65536。
    // 注意：这不是句柄数量（句柄数量总是更少）。
    // 由于当前最大可表示句柄是 0x07FFFFFF，最大合理的堆大小是 2 GiB，
    // 在 GL 情况下每个池大约 760 万个句柄。
    size_t const maxHeapSize = std::min(area.size(), HANDLE_INDEX_MASK * getAlignment());

    if (UTILS_UNLIKELY(maxHeapSize != area.size())) {
        LOG(WARNING) << "HandleAllocator heap size reduced to " << maxHeapSize << " from "
                     << area.size();
    }

    // 确保我们从干净的内存区域开始。这需要确保所有块都以 age 为 0 开始。
    memset(area.data(), 0, maxHeapSize);

    // 调整不同池的大小，使它们都能包含相同数量的句柄
    size_t const count = maxHeapSize / (P0 + P1 + P2);
    char* const p0 = static_cast<char*>(area.begin());
    char* const p1 = p0 + count * P0;
    char* const p2 = p1 + count * P1;

    mPool0 = Pool<P0>(p0, count * P0);
    mPool1 = Pool<P1>(p1, count * P1);
    mPool2 = Pool<P2>(p2, count * P2);
}

// ------------------------------------------------------------------------------------------------

/**
 * HandleAllocator 构造函数（完整参数）
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param name 分配器名称（用于调试）
 * @param size 堆大小（字节）
 * @param disableUseAfterFreeCheck 是否禁用释放后使用检查
 * @param disableHeapHandleTags 是否禁用堆句柄标签
 */
template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::HandleAllocator(const char* name, size_t size,
        bool disableUseAfterFreeCheck,
        bool disableHeapHandleTags)
    : mHandleArena(name, size, disableUseAfterFreeCheck),
      mUseAfterFreeCheckDisabled(disableUseAfterFreeCheck),
      mHeapHandleTagsDisabled(disableHeapHandleTags) {
}

/**
 * HandleAllocator 构造函数（简化版本）
 * 
 * 使用默认设置（启用释放后使用检查和堆句柄标签）。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param name 分配器名称（用于调试）
 * @param size 堆大小（字节）
 */
template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::HandleAllocator(const char* name, size_t size)
    : HandleAllocator(name, size, false, false) {
}

/**
 * HandleAllocator 析构函数
 * 
 * 检查是否有未释放的句柄（溢出映射中的句柄），如果有则记录错误并释放内存。
 */
template <size_t P0, size_t P1, size_t P2>
HandleAllocator<P0, P1, P2>::~HandleAllocator() noexcept {
    auto& overflowMap = mOverflowMap;
    if (!overflowMap.empty()) {
        PANIC_LOG("Not all handles have been freed. Probably leaking memory.");
        // 释放剩余的句柄内存
        for (auto& entry : overflowMap) {
            ::free(entry.second);
        }
    }
}

/**
 * 将句柄转换为指针（慢路径）
 * 
 * 当句柄不在池中时（在溢出映射中），使用此方法查找。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param id 句柄 ID
 * @return 资源指针，如果未找到返回 nullptr
 */
template <size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
void* HandleAllocator<P0, P1, P2>::handleToPointerSlow(HandleBase::HandleId id) const noexcept {
    auto& overflowMap = mOverflowMap;
    std::lock_guard const lock(mLock);
    auto pos = overflowMap.find(id);
    if (pos != overflowMap.end()) {
        return pos.value();
    }
    return nullptr;
}

/**
 * 分配句柄（慢路径）
 * 
 * 当池已满时，使用系统堆分配内存。
 * 这会设置 HANDLE_HEAP_FLAG 标志，表示句柄在堆中而不是池中。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param size 要分配的大小（字节）
 * @return 句柄 ID（设置了 HANDLE_HEAP_FLAG）
 * 
 * 注意：如果这是第一次使用堆分配（id == HANDLE_HEAP_FLAG | 1），
 * 会记录警告，建议增加 FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB。
 */
template <size_t P0, size_t P1, size_t P2>
HandleBase::HandleId HandleAllocator<P0, P1, P2>::allocateHandleSlow(size_t size) {
    void* p = ::malloc(size);

    auto const nextId = mId.fetch_add(1, std::memory_order_relaxed) + 1;
    FILAMENT_CHECK_POSTCONDITION(nextId < HANDLE_HEAP_FLAG) <<
            "No more Handle ids available! This can happen if HandleAllocator arena has been full"
            " for a while. Please increase FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB";

    HandleBase::HandleId id = nextId | HANDLE_HEAP_FLAG;

    std::unique_lock lock(mLock);
    mOverflowMap.emplace(id, p);
    lock.unlock();

    if (UTILS_UNLIKELY(id == (HANDLE_HEAP_FLAG | 1u))) { // 意味着 id 为零
        PANIC_LOG("HandleAllocator arena is full, using slower system heap. Please increase "
                  "the appropriate constant (e.g. FILAMENT_OPENGL_HANDLE_ARENA_SIZE_IN_MB).");
    }
    return id;
}

/**
 * 释放句柄（慢路径）
 * 
 * 释放堆分配的句柄（设置了 HANDLE_HEAP_FLAG 的句柄）。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param id 句柄 ID（必须设置了 HANDLE_HEAP_FLAG）
 * @param size 大小（未使用，保留以匹配接口）
 */
template <size_t P0, size_t P1, size_t P2>
void HandleAllocator<P0, P1, P2>::deallocateHandleSlow(HandleBase::HandleId id, size_t) noexcept {
    assert_invariant(id & HANDLE_HEAP_FLAG);
    void* p = nullptr;
    auto& overflowMap = mOverflowMap;

    std::unique_lock lock(mLock);
    auto pos = overflowMap.find(id);
    if (pos != overflowMap.end()) {
        p = pos.value();
        overflowMap.erase(pos);
    }
    lock.unlock();

    ::free(p);
}

/**
 * 获取句柄标签
 * 
 * 返回与句柄关联的调试标签。
 * 
 * @tparam P0 第一个池的对象大小（字节）
 * @tparam P1 第二个池的对象大小（字节）
 * @tparam P2 第三个池的对象大小（字节）
 * @param id 句柄 ID
 * @return 调试标签字符串，如果没有标签返回 "(no tag)"
 * 
 * 对于池句柄，会截断 age 以获取调试标签。
 */
template<size_t P0, size_t P1, size_t P2>
UTILS_NOINLINE
ImmutableCString HandleAllocator<P0, P1, P2>::getHandleTag(HandleBase::HandleId id) const noexcept {
    uint32_t key = id;
    if (UTILS_LIKELY(isPoolHandle(id))) {
        // 截断 age 以获取调试标签
        key &= ~(HANDLE_DEBUG_TAG_MASK ^ HANDLE_AGE_MASK);
    }
    return findHandleTag(key);
}

/**
 * DebugTag 构造函数
 * 
 * 为调试标签预留初始空间，防止在设置前几个标签时过度调用 malloc。
 */
DebugTag::DebugTag() {
    // 为调试标签预留初始空间。这可以防止在设置前几个标签时过度调用 malloc。
    mDebugTags.reserve(512);
}

/**
 * 查找句柄标签
 * 
 * 在调试标签映射中查找指定键的标签。
 * 
 * @param key 句柄键（可能是截断后的 ID）
 * @return 调试标签字符串，如果未找到返回 "(no tag)"
 */
UTILS_NOINLINE
ImmutableCString DebugTag::findHandleTag(HandleBase::HandleId key) const noexcept {
    std::unique_lock const lock(mDebugTagLock);
    if (auto pos = mDebugTags.find(key); pos != mDebugTags.end()) {
        return pos->second;
    }
    return "(no tag)";
}

/**
 * 写入池句柄标签
 * 
 * 为池句柄设置调试标签。
 * 
 * @param key 句柄键（截断后的 ID）
 * @param tag 标签字符串（会被移动）
 * 
 * 注意：基于池的标签在达到一定 age 后会被回收。
 */
UTILS_NOINLINE
void DebugTag::writePoolHandleTag(HandleBase::HandleId key, ImmutableCString&& tag) noexcept {
    // 这一行是昂贵的部分。将来，我们可以使用自定义分配器。
    std::unique_lock const lock(mDebugTagLock);
    // 基于池的标签在达到一定 age 后会被回收。
    mDebugTags[key] = std::move(tag);
}

/**
 * 写入堆句柄标签
 * 
 * 为堆句柄设置调试标签。
 * 
 * @param key 句柄键（完整的 ID）
 * @param tag 标签字符串（会被移动）
 * 
 * 注意：基于堆的标签永远不会被回收，因此一旦进入慢速模式，这可能会无限增长。
 * FIXME: 需要实现堆标签的回收机制。
 */
UTILS_NOINLINE
void DebugTag::writeHeapHandleTag(HandleBase::HandleId key, ImmutableCString&& tag) noexcept {
    // 这一行是昂贵的部分。将来，我们可以使用自定义分配器。
    std::unique_lock const lock(mDebugTagLock);
    // FIXME: 基于堆的标签永远不会被回收，因此一旦进入慢速模式，这可能会无限增长。
    mDebugTags[key] = std::move(tag);
}

/**
 * 显式模板实例化
 * 
 * 为各个后端显式实例化 HandleAllocator 模板。
 * 这确保模板代码被编译到相应的后端模块中。
 */
#if defined (FILAMENT_SUPPORTS_OPENGL)
template class HandleAllocatorGL;  // OpenGL 后端句柄分配器
#endif

#if defined (FILAMENT_DRIVER_SUPPORTS_VULKAN)
template class HandleAllocatorVK;  // Vulkan 后端句柄分配器
#endif

#if defined (FILAMENT_SUPPORTS_METAL)
template class HandleAllocatorMTL;  // Metal 后端句柄分配器
#endif

#if defined (FILAMENT_SUPPORTS_WEBGPU)
template class HandleAllocatorWGPU;  // WebGPU 后端句柄分配器
#endif

} // namespace filament::backend
