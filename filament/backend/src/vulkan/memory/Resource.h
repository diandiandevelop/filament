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

#ifndef TNT_FILAMENT_BACKEND_VULKAN_MEMORY_RESOURCE_H
#define TNT_FILAMENT_BACKEND_VULKAN_MEMORY_RESOURCE_H

#include <private/backend/HandleAllocator.h>
#include <utils/Mutex.h>

#include <atomic>
#include <cstdint>

namespace filament::backend::fvkmemory {

using CounterIndex = int32_t;
using HandleId = HandleBase::HandleId;

class ResourceManager;

template <typename D>
struct resource_ptr;

// Subclasses of VulkanResource must provide this enum in their construction.
// VulkanResource（及其子类）在构造时必须提供对应的资源类型，用于统一管理生命周期和调试。
enum class ResourceType : uint8_t {
    BUFFER_OBJECT = 0,
    INDEX_BUFFER = 1,
    PROGRAM = 2,
    RENDER_TARGET = 3,
    SWAP_CHAIN = 4,
    RENDER_PRIMITIVE = 5,
    TEXTURE = 6,
    TEXTURE_STATE = 7,
    TIMER_QUERY = 8,
    VERTEX_BUFFER = 9,
    VERTEX_BUFFER_INFO = 10,
    DESCRIPTOR_SET_LAYOUT = 11,
    DESCRIPTOR_SET = 12,
    FENCE = 13,
    VULKAN_BUFFER = 14,
    STAGE_SEGMENT = 15,
    STAGE_IMAGE = 16,
    SYNC = 17,
    MEMORY_MAPPED_BUFFER = 18,
    SEMAPHORE = 19,
    STREAM = 20,
    UNDEFINED_TYPE = 21,    // Must be the last enum because we use it for iterating over the enums.
};

template<typename D>
ResourceType getTypeEnum() noexcept;

std::string_view getTypeStr(ResourceType type);

inline bool isThreadSafeType(ResourceType type) {
    return type == ResourceType::FENCE || type == ResourceType::TIMER_QUERY;
}

// 非线程安全的资源基类
// 适用于只在单线程环境中访问的 Vulkan 资源（例如大部分图形对象）。
struct Resource {
    Resource()
        : resManager(nullptr),
          id(HandleBase::nullid),
          mCount(0),
          restype(ResourceType::UNDEFINED_TYPE),
          mHandleConsideredDestroyed(false) {}

    // 检查当前资源是否为指定类型 D
    template<typename D>
    bool isType() const {
        return getTypeEnum<D>() == restype;
    }

private:
    // 增加引用计数
    inline void inc() noexcept {
        mCount++;
    }

    // 减少引用计数，当计数归零时通过 ResourceManager 延迟销毁对应句柄
    inline void dec() noexcept {
        assert_invariant(mCount > 0);
        if (--mCount == 0) {
            destroy(restype, id);
        }
    }

    // To be able to detect use-after-free, we need a bit to signify if the handle should be
    // consider destroyed (from Filament's perspective).
    // 为了检测 use-after-free，需要一个标记位表示（从 Filament 视角）句柄是否已被视为销毁。
    inline void setHandleConsiderDestroyed() noexcept {
        mHandleConsideredDestroyed = true;
    }

    inline bool isHandleConsideredDestroyed() const {
        return mHandleConsideredDestroyed;
    }

    // 初始化资源：设置句柄 id、所属 ResourceManager 和资源类型
    template <typename T>
    inline void init(HandleId id, ResourceManager* resManager) {
        this->id = id;
        this->resManager = resManager;
        this->restype = getTypeEnum<T>();
    }

    // 实际销毁逻辑委托给 ResourceManager（延迟销毁）
    void destroy(ResourceType type, HandleId id);

    ResourceManager* resManager; // 8 资源管理器指针
    HandleId id;                 // 4 句柄id（用于在管理器中索引资源）
    uint32_t mCount      : 24;   // 24 位引用计数
    ResourceType restype : 7;    // 7 位资源类型
    bool mHandleConsideredDestroyed : 1;  // 是否已将句柄视为销毁
                                          // restype + mCount + mHandleConsideredDestroyed 共4字节

    friend class ResourceManager;

    template <typename D>
    friend struct resource_ptr;
};

// 线程安全的资源基类
// 适用于可能被多个线程并发访问的 Vulkan 资源（如 Fence / TimerQuery 等）。
struct ThreadSafeResource {
    ThreadSafeResource()
        : resManager(nullptr),
          id(HandleBase::nullid),
          mCount(0),
          restype(ResourceType::UNDEFINED_TYPE),
          mHandleConsideredDestroyed(false) {}

private:
    // 原子地增加引用计数
    inline void inc() noexcept {
        mCount.fetch_add(1, std::memory_order_relaxed);
    }

    // 原子地减少引用计数，当计数归零时通过 ResourceManager 延迟销毁对应句柄
    inline void dec() noexcept {
        if (mCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroy(restype, id);
        }
    }

    // To be able to detect use-after-free, we need a bit to signify if the handle should be
    // consider destroyed (from Filament's perspective).
    // 为了检测 use-after-free，需要一个标记位表示（从 Filament 视角）句柄是否已被视为销毁。
    inline void setHandleConsiderDestroyed() noexcept {
        mHandleConsideredDestroyed = true;
    }

    inline bool isHandleConsideredDestroyed() const {
        return mHandleConsideredDestroyed;
    }

    // 初始化资源：设置句柄 id、所属 ResourceManager 和资源类型
    template <typename T>
    inline void init(HandleId id, ResourceManager* resManager) {
        this->id = id;
        this->resManager = resManager;
        this->restype = getTypeEnum<T>();
    }

    // 实际销毁逻辑委托给 ResourceManager（延迟销毁）
    void destroy(ResourceType type, HandleId id);

    ResourceManager* resManager;  // 8 资源管理器指针
    HandleId id;                  // 4 句柄id
    std::atomic<uint32_t> mCount; // 4 原子引用计数
    ResourceType restype : 7;     // 7 位资源类型
    bool mHandleConsideredDestroyed : 1;  // 是否已将句柄视为销毁（与 restype 共用1字节）

    friend class ResourceManager;

    template <typename D>
    friend struct resource_ptr;
};

} // namespace filament::backend::fvkmemory

#endif // TNT_FILAMENT_BACKEND_VULKAN_MEMORY_RESOURCE_H
