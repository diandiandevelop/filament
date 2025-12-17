/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_VULKANBUFFER_H
#define TNT_FILAMENT_BACKEND_VULKANBUFFER_H

#include "VulkanMemory.h"
#include "memory/Resource.h"

#include <functional>

namespace filament::backend {

/**
 * VulkanBuffer - 高层 GPU 缓冲区封装
 *
 * 该类持有一个底层的 `VulkanGpuBuffer` 指针，并在析构时通过回调将其归还给对应的池（Pool），
 * 便于实现缓冲区的重用（避免频繁创建 / 销毁 VkBuffer）。
 */
class VulkanBuffer : public fvkmemory::Resource {
public:
    // Because we need to recycle the unused `VulkanGpuBuffer`, we allow for a callback that the
    // "Pool" can use to acquire the buffer back.
    // 由于需要回收未使用的 `VulkanGpuBuffer`，这里允许传入一个回调供池（Pool）将缓冲区取回。
    using OnRecycle = std::function<void(VulkanGpuBuffer const*)>;

    /**
     * 构造函数
     *
     * @param gpuBuffer   底层 GPU 缓冲区指针（由缓存 / 池创建并管理）
     * @param onRecycleFn 在本对象析构时调用的回调，用于将 gpuBuffer 归还给池
     */
    VulkanBuffer(VulkanGpuBuffer const* gpuBuffer, OnRecycle&& onRecycleFn)
        : mGpuBuffer(gpuBuffer),
          mOnRecycleFn(onRecycleFn) {}

    /**
     * 析构函数
     *
     * 如果提供了回调函数，则在析构时调用回调，将底层 `VulkanGpuBuffer` 交还给池，
     * 从而实现缓冲区的重用。
     */
    ~VulkanBuffer() {
        if (mOnRecycleFn) {
            mOnRecycleFn(mGpuBuffer);
        }
    }

    /**
     * 获取底层 GPU 缓冲区指针
     *
     * @return 指向 `VulkanGpuBuffer` 的常量指针
     */
    VulkanGpuBuffer const* getGpuBuffer() const { return mGpuBuffer; }

private:
    VulkanGpuBuffer const* mGpuBuffer;  // 实际持有的 GPU 缓冲区（VkBuffer + 分配信息）
    OnRecycle mOnRecycleFn;             // 回调：在析构时用于回收 GPU 缓冲区
};

}// namespace filament::backend

#endif// TNT_FILAMENT_BACKEND_VULKANBUFFER_H
