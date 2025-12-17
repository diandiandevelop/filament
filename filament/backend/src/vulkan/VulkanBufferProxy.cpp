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

#include "VulkanBufferProxy.h"

#include "VulkanBufferCache.h"
#include "VulkanCommands.h"
#include "VulkanContext.h"
#include "VulkanHandles.h"
#include "VulkanMemory.h"

#include <backend/DriverEnums.h>

using namespace bluevk;

namespace filament::backend {

/**
 * VulkanBufferProxy 构造函数
 *
 * 作用：
 * - 从 `VulkanBufferCache` 中获取一个满足大小和绑定类型的 `VulkanBuffer`；
 * - 记录是否启用 staging buffer bypass（在 UMA + 特定用法下允许直接 memcpy 到 GPU）；
 * - 持有对 `VulkanStagePool` / `VulkanBufferCache` / VMA 分配器的引用，便于后续上传。
 *
 * @param context       Vulkan 上下文（用于判断是否启用 stagingBufferBypass）
 * @param allocator     VMA 分配器句柄
 * @param stagePool     staging buffer 池（用于上传路径中的中转缓冲区）
 * @param bufferCache   缓冲区缓存 / 池
 * @param binding       缓冲区绑定类型（UNIFORM / VERTEX / INDEX / STORAGE 等）
 * @param usage         BufferUsage 标志（STATIC / DYNAMIC / SHARED_WRITE_BIT 等）
 * @param numBytes      需要的缓冲区大小
 */
VulkanBufferProxy::VulkanBufferProxy(VulkanContext const& context, VmaAllocator allocator,
        VulkanStagePool& stagePool, VulkanBufferCache& bufferCache, VulkanBufferBinding binding,
        BufferUsage usage, uint32_t numBytes)
    : mStagingBufferBypassEnabled(context.stagingBufferBypassEnabled()),
      mAllocator(allocator),
      mStagePool(stagePool),
      mBufferCache(bufferCache),
      mBuffer(mBufferCache.acquire(binding, numBytes)),
      mLastReadAge(0),
      mUsage(usage) {}

/**
 * 将 CPU 数据写入 GPU 缓冲区实现
 *
 * 根据当前硬件架构（是否 UMA）、缓冲区绑定类型 / 用法以及是否存在“前一帧读依赖”，
 * 在以下两种路径之间选择：
 *
 * 1. 直接 memcpy 路径（无 staging buffer）：
 *    - 条件：
 *      * 缓冲区内存是 host visible（有 pMappedData）；
 *      * (UNIFORM + 没有读依赖 + 启用 stagingBufferBypass) 或 标记为 STATIC / SHARED_WRITE_BIT；
 *    - 步骤：
 *      * 直接 memcpy 到映射内存；
 *      * 调用 vmaFlushAllocation 刷新对应范围；
 *      * 不需要额外的 pipeline barrier。
 *
 * 2. 通过 staging buffer 上传：
 *    - 步骤：
 *      * 从 `VulkanStagePool` 获取一个 staging 段，将数据 memcpy 进去并 flush；
 *      * 如果存在读依赖（前一次命令在读同一缓冲区），根据绑定类型插入合适的 buffer memory barrier；
 *      * 使用 vkCmdCopyBuffer 将数据从 staging 复制到目标缓冲区；
 *      * 再插入一次 buffer memory barrier，确保写入在后续 draw / dispatch 前可见。
 *
 * @param commands   当前录制中的命令缓冲包装
 * @param cpuData    CPU 侧源数据指针
 * @param byteOffset 目标缓冲区内的字节偏移
 * @param numBytes   写入的字节数
 */
void VulkanBufferProxy::loadFromCpu(VulkanCommandBuffer& commands, const void* cpuData,
        uint32_t byteOffset, uint32_t numBytes) {

    // This means that we're recording a write into a command buffer without a previous read, so it
    // should be safe to
    //   1) Do a direct memcpy in UMA mode
    //   2) Skip adding a barrier (to protect the write from writing over a read).
    bool const isAvailable = commands.age() != mLastReadAge;

    // Keep track of the VulkanBuffer usage
    // 跟踪 VulkanBuffer 的使用，防止在命令执行前被销毁
    commands.acquire(mBuffer);

    // Check if we can just memcpy directly to the GPU memory.
    // 检查是否可以直接 memcpy 到 GPU 内存（host visible）
    bool const isMemcopyable = mBuffer->getGpuBuffer()->allocationInfo.pMappedData != nullptr;

    // In the case of UNIFORMS, check that is available to see to know if a memcpy is possible.
    // This works regardless if it's a full or partial update of the buffer.
    // 对于 uniform buffer，如果当前命令缓冲没有读依赖，则可以直接 memcpy（全量或部分更新都成立）
    bool const isUniformAndAvailable = getBinding() == VulkanBufferBinding::UNIFORM && isAvailable;

    // In the case the content is marked as memory mapped or static, is guaranteed to be safe to do
    // a memcpy if its available.
    bool const isStaticOrShared =
            any(mUsage & (BufferUsage::STATIC | BufferUsage::SHARED_WRITE_BIT));
    bool const useMemcpy =
            ((isUniformAndAvailable && mStagingBufferBypassEnabled) || isStaticOrShared) &&
            isMemcopyable;
    if (useMemcpy) {
        char* dest = static_cast<char*>(mBuffer->getGpuBuffer()->allocationInfo.pMappedData) +
                     byteOffset;
        memcpy(dest, cpuData, numBytes);
        vmaFlushAllocation(mAllocator, mBuffer->getGpuBuffer()->vmaAllocation, byteOffset,
                numBytes);
        return;

        // TODO: to properly bypass staging buffer, we'd need to be able to swap out a VulkanBuffer,
        // which represents a VkBuffer. This means that the corresponding descriptor sets also have
        // to be updated.
        // TODO：要彻底绕过 staging buffer，需要能够在不修改上层对象的情况下替换 VkBuffer，
        //       这意味着还要同步更新所有引用到该缓冲区的描述符堆。
    }

    // Note: this should be stored within the command buffer before going out of
    // scope, so that the command buffer can manage its lifecycle.
    // 注意：stage 对象会被命令缓冲持有，确保在 GPU 执行完复制命令前不会被销毁
    fvkmemory::resource_ptr<VulkanStage::Segment> stage = mStagePool.acquireStage(numBytes);
    assert_invariant(stage->memory());
    commands.acquire(stage);
    memcpy(stage->mapping(), cpuData, numBytes);
    vmaFlushAllocation(mAllocator, stage->memory(), stage->offset(), numBytes);

    // If there was a previous read, then we need to make sure the following write is properly
    // synced with the previous read.
    // 如果之前存在对该缓冲区的读取，需要在写入前插入 barrier，保证读写顺序正确
    if (!isAvailable) {
        VkAccessFlags srcAccess = 0;
        VkPipelineStageFlags srcStage = 0;
        if (getBinding() == VulkanBufferBinding::UNIFORM) {
            srcAccess = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else if (getBinding() == VulkanBufferBinding::VERTEX) {
            srcAccess = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        } else if (getBinding() == VulkanBufferBinding::INDEX) {
            srcAccess = VK_ACCESS_INDEX_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        }

        VkBufferMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = srcAccess,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = getVkBuffer(),
            .offset = byteOffset,
            .size = numBytes,
        };
        vkCmdPipelineBarrier(commands.buffer(), srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                nullptr, 1, &barrier, 0, nullptr);
    }

    VkBufferCopy region = {
        .srcOffset = stage->offset(),
        .dstOffset = byteOffset,
        .size = numBytes,
    };
    vkCmdCopyBuffer(commands.buffer(), stage->buffer(), getVkBuffer(), 1, &region);

    // Firstly, ensure that the copy finishes before the next draw call.
    // Secondly, in case the user decides to upload another chunk (without ever using the first one)
    // we need to ensure that this upload completes first (hence
    // dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT).
    // 1）确保本次拷贝在后续绘制调用前完成；
    // 2）如果用户连续上传多个片段（第一个从未被使用），也要确保上传顺序正确，
    //    因此目标阶段包含 TRANSFER_BIT。
    VkAccessFlags dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (getBinding() == VulkanBufferBinding::VERTEX) {
        dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        dstStageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    } else if (getBinding() == VulkanBufferBinding::INDEX) {
        dstAccessMask |= VK_ACCESS_INDEX_READ_BIT;
        dstStageMask |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    } else if (getBinding() == VulkanBufferBinding::UNIFORM) {
        dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
        dstStageMask |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    } else if (getBinding() == VulkanBufferBinding::SHADER_STORAGE) {
        // TODO: implement me
    }

    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = dstAccessMask,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = getVkBuffer(),
        .offset = byteOffset,
        .size = numBytes,
    };

    vkCmdPipelineBarrier(commands.buffer(), VK_PIPELINE_STAGE_TRANSFER_BIT, dstStageMask, 0, 0,
            nullptr, 1, &barrier, 0, nullptr);
}

VkBuffer VulkanBufferProxy::getVkBuffer() const noexcept {
    return mBuffer->getGpuBuffer()->vkbuffer;
}

VulkanBufferBinding VulkanBufferProxy::getBinding() const noexcept {
    return mBuffer->getGpuBuffer()->binding;
}

void VulkanBufferProxy::referencedBy(VulkanCommandBuffer& commands) {
    commands.acquire(mBuffer);
    mLastReadAge = commands.age();
}

} // namespace filament::backend
