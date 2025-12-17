/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_VULKANBUFFERCACHE_H
#define TNT_FILAMENT_BACKEND_VULKANBUFFERCACHE_H

#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanMemory.h"
#include "memory/Resource.h"
#include "memory/ResourceManager.h"

#include <map>

namespace filament::backend {

/**
 * VulkanBufferCache - Vulkan 缓冲区缓存 / 池
 *
 * 负责统一管理不同用途（UNIFORM / VERTEX / INDEX / STORAGE 等）的 `VulkanGpuBuffer`：
 * - 按大小和用途分池（BufferPool），实现重用和减少频繁的 VkBuffer / VkDeviceMemory 分配；
 * - 使用简单的“帧计数 + LRU”策略按帧逐步回收长期未使用的缓冲区；
 * - 通过 `VulkanBuffer` 封装，将回收到池的逻辑封装在 OnRecycle 回调中。
 */
class VulkanBufferCache {
public:
    /**
     * 构造函数
     *
     * @param context         Vulkan 上下文（用于获取设备 / 队列等信息）
     * @param resourceManager 资源管理器（负责跟踪 / 销毁底层 Vulkan 资源）
     * @param allocator       VMA 分配器句柄（Vulkan Memory Allocator）
     */
    VulkanBufferCache(VulkanContext const& context, fvkmemory::ResourceManager& resourceManager,
            VmaAllocator allocator);

    // `VulkanBufferCache` is not copyable.
    VulkanBufferCache(const VulkanBufferCache&) = delete;
    VulkanBufferCache& operator=(const VulkanBufferCache&) = delete;

    // Allocates or reuse a new VkBuffer that is device local.
    // In the case of Unified memory architecture, uniform buffers are also host visible.
    /**
     * 获取一个 `VulkanBuffer`
     *
     * 会优先尝试从对应用途的池中重用一个大小足够的 `VulkanGpuBuffer`；
     * 如果没有合适的可用缓冲区，则通过 VMA 分配一个新的 VkBuffer。
     *
     * 在统一内存架构（UMA）下，uniform buffer 也可以是 host visible，从而支持直接 memcpy。
     *
     * @param binding  缓冲区绑定类型（UNIFORM / VERTEX / INDEX / STORAGE 等）
     * @param numBytes 需求字节数
     * @return 指向高层封装 `VulkanBuffer` 的智能指针（resource_ptr）
     */
    fvkmemory::resource_ptr<VulkanBuffer> acquire(VulkanBufferBinding binding,
            uint32_t numBytes) noexcept;

    // Evicts old unused `VulkanGpuBuffer` and bumps the current frame number
    /**
     * 垃圾回收（逐帧调用）
     *
     * 驱逐长时间未使用的 `VulkanGpuBuffer`，并增加当前帧计数。
     * 具体策略由实现（内存 / 时间阈值）决定。
     */
    void gc() noexcept;

    // Destroys all unused `VulkanGpuBuffer`.
    // This should be called while the context's VkDevice is still alive.
    /**
     * 销毁所有未使用的 `VulkanGpuBuffer`
     *
     * 在关闭设备前调用，用于彻底释放池中所有未使用的底层 VkBuffer / VkDeviceMemory。
     *
     * 注意：必须在 VkDevice 仍然有效时调用。
     */
    void terminate() noexcept;

private:
    // 表示一个当前未使用、可被重用的 GPU 缓冲区
    struct UnusedGpuBuffer {
        uint64_t lastAccessed;              // 上次访问（被使用）的“时间戳”（帧计数）
        VulkanGpuBuffer const* gpuBuffer;   // 底层 GPU 缓冲区
    };

    using BufferPool = std::multimap<uint32_t, UnusedGpuBuffer>; // key: 大小，value: 未使用缓冲区

    // Return a `VulkanGpuBuffer` back to its corresponding pool
    // 将一个 `VulkanGpuBuffer` 归还到对应用途的池中（供后续重用）
    void release(VulkanGpuBuffer const* gpuBuffer) noexcept;

    // Allocate a new VkBuffer from the VMA pool of the corresponding `numBytes` and `usage`.
    // 使用 VMA 分配一个新的 VkBuffer / VkDeviceMemory，并封装为 `VulkanGpuBuffer`
    VulkanGpuBuffer const* allocate(VulkanBufferBinding binding, uint32_t numBytes) noexcept;

    // Destroy the corresponding VkBuffer and return the VkDeviceMemory to the VMA pool.
    // 销毁给定的 `VulkanGpuBuffer`，并将其内存归还给 VMA 池
    void destroy(VulkanGpuBuffer const* gpuBuffer) noexcept;

    // 根据绑定类型获取对应的 BufferPool
    BufferPool& getPool(VulkanBufferBinding binding) noexcept;

    VulkanContext const& mContext;              // Vulkan 上下文（设备 / 队列等）
    fvkmemory::ResourceManager& mResourceManager; // 资源管理器，用于生命周期管理
    VmaAllocator mAllocator;                    // VMA 分配器句柄

    // Buffers can be recycled, after they are released. Each type of buffer have its own pool
    // 缓冲区在释放后可以被回收重用；每种绑定类型有自己的池
    static constexpr int MAX_POOL_COUNT = 4;
    BufferPool mGpuBufferPools[MAX_POOL_COUNT];

    // Store the current "time" (really just a frame count) and LRU eviction parameters.
    // 当前“时间”（实际上是帧计数），用于LRU策略的回收决策
    uint64_t mCurrentFrame = 0;
};

}// namespace filament::backend

#endif// TNT_FILAMENT_BACKEND_VULKANBUFFERCACHE_H
