/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "VulkanSwapChain.h"

#include "VulkanCommands.h"
#include "VulkanTexture.h"

#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Panic.h>

using namespace bluevk;
using namespace utils;

namespace filament::backend {

/**
 * VulkanSwapChain 构造函数实现
 *
 * 执行步骤：
 * 1. 记录平台 / 上下文 / 资源管理器 / 命令系统 / 分配器等依赖；
 * 2. 根据 `extent` 和 `nativeWindow` 判断是否为 headless 模式；
 * 3. 读取自定义配置：
 *    - `flushAndWaitOnResize`：重建交换链前是否先 flush + wait；
 *    - `transitionSwapChainImageLayoutForPresent`：present 前是否转换图像布局；
 * 4. 调用平台层 `createSwapChain` 创建底层交换链对象；
 * 5. 调用 `update()` 从平台查询图像 / 格式并构建 `VulkanTexture` 封装。
 */
VulkanSwapChain::VulkanSwapChain(VulkanPlatform* platform, VulkanContext const& context,
        fvkmemory::ResourceManager* resourceManager, VmaAllocator allocator,
        VulkanCommands* commands, VulkanStagePool& stagePool, void* nativeWindow, uint64_t flags,
        VkExtent2D extent)
    : mPlatform(platform),
      mContext(context),
      mResourceManager(resourceManager),
      mCommands(commands),
      mAllocator(allocator),
      mStagePool(stagePool),
      mHeadless(extent.width != 0 && extent.height != 0 && !nativeWindow),
      mFlushAndWaitOnResize(platform->getCustomization().flushAndWaitOnWindowResize),
      mTransitionSwapChainImageLayoutForPresent(
              platform->getCustomization().transitionSwapChainImageLayoutForPresent),
      mLayerCount(1),
      mCurrentSwapIndex(0),
      mAcquired(false),
      mIsFirstRenderPass(true) {
    // 创建平台层交换链对象
    swapChain = mPlatform->createSwapChain(nativeWindow, flags, extent);
    FILAMENT_CHECK_POSTCONDITION(swapChain) << "Unable to create swapchain";

    // 根据平台返回的 SwapChainBundle 更新 color / depth 纹理等
    update();
}

/**
 * VulkanSwapChain 析构函数实现
 *
 * 注意：在销毁交换链前必须确保所有 in-flight 命令缓冲执行完成，
 * 否则可能访问已经被销毁的交换链图像。
 */
VulkanSwapChain::~VulkanSwapChain() {
    // Must wait for the inflight command buffers to finish since they might contain the images
    // we're about to destroy.
    mCommands->flush();
    mCommands->wait();

    mColors = {};
    mDepth = {};
    for (auto& semaphore : mFinishedDrawing) {
        semaphore = {};
    }
    mFinishedDrawing.clear();
    mPlatform->destroy(swapChain);
}

/**
 * 重新从平台层交换链获取图像 / 格式信息并重建内部资源
 *
 * 执行步骤：
 * 1. 清空颜色附件列表和信号量列表；
 * 2. 向平台层查询 `SwapChainBundle`（颜色图像数组、深度图像、格式、尺寸、图层数等）；
 * 3. 为每张颜色图像构建一个 `VulkanTexture` 封装，设置合适的 `TextureUsage` 标志；
 * 4. 为深度图像构建对应的 `VulkanTexture`；
 * 5. 更新本地记录的 `mExtent` 和 `mLayerCount`。
 */
void VulkanSwapChain::update() {
    mColors.clear();

    auto const bundle = mPlatform->getSwapChainBundle(swapChain);
    size_t const swapChainCount = bundle.colors.size();
    mColors.reserve(bundle.colors.size());
    VkDevice const device = mPlatform->getDevice();

    mFinishedDrawing.clear();
    mFinishedDrawing.reserve(swapChainCount);
    mFinishedDrawing.resize(swapChainCount);
    for (size_t i = 0; i < swapChainCount; ++i) {
        mFinishedDrawing[i] = {};
    }

    TextureUsage depthUsage = TextureUsage::DEPTH_ATTACHMENT;
    TextureUsage colorUsage = TextureUsage::COLOR_ATTACHMENT;
    if (bundle.isProtected) {
        depthUsage |= TextureUsage::PROTECTED;
        colorUsage |= TextureUsage::PROTECTED;
    }
    for (auto const color: bundle.colors) {
        auto colorTexture = fvkmemory::resource_ptr<VulkanTexture>::construct(mResourceManager,
                mContext, device, mAllocator, mResourceManager, mCommands, color, VK_NULL_HANDLE,
                bundle.colorFormat, VK_NULL_HANDLE /*ycrcb */, 1, bundle.extent.width,
                bundle.extent.height, bundle.layerCount, colorUsage, mStagePool);
        mColors.push_back(colorTexture);
    }

    mDepth = fvkmemory::resource_ptr<VulkanTexture>::construct(mResourceManager, mContext, device,
            mAllocator, mResourceManager, mCommands, bundle.depth, VK_NULL_HANDLE,
            bundle.depthFormat, VK_NULL_HANDLE /*ycrcb */, 1, bundle.extent.width,
            bundle.extent.height, bundle.layerCount, depthUsage, mStagePool);

    mExtent = bundle.extent;
    mLayerCount = bundle.layerCount;
}

/**
 * 提交当前帧到交换链实现
 *
 * 执行步骤：
 * 1. 如果上一次 acquire 失败（`mAcquired == false`），直接返回；
 * 2. 如有需要，在非 headless 模式下，将当前 color image 布局转换为 `PRESENT`；
 * 3. 调用 `mCommands->flush()` 提交命令缓冲；
 * 4. 非 headless 模式下：
 *    - 从命令系统获取“渲染完成”信号量；
 *    - 调用平台层 `present`，传入当前 image 索引和完成信号量；
 *    - 检查返回值是否为 `VK_SUCCESS / VK_SUBOPTIMAL_KHR / VK_ERROR_OUT_OF_DATE_KHR`；
 * 5. 重置 `mAcquired` 和 `mIsFirstRenderPass` 标志；
 * 6. 如果注册了 `mFrameScheduled` 回调，通过 Driver 调度执行回调。
 */
void VulkanSwapChain::present(DriverBase& driver) {
    // The last acquire failed, so just skip presenting.
    if (!mAcquired) {
        return;
    }

    if (!mHeadless && mTransitionSwapChainImageLayoutForPresent) {
        VulkanCommandBuffer& commands = mCommands->get();
        VkImageSubresourceRange const subresources{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = mLayerCount,
        };
        mColors[mCurrentSwapIndex]->transitionLayout(&commands, subresources, VulkanLayout::PRESENT);
    }

    mCommands->flush();

    // We only present if it is not headless. No-op for headless.
    if (!mHeadless) {
        auto finishedDrawing = mCommands->acquireFinishedSignal();
        mFinishedDrawing[mCurrentSwapIndex] = finishedDrawing;
        VkResult const result =
                mPlatform->present(swapChain, mCurrentSwapIndex, finishedDrawing->getVkSemaphore());
        FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ||
                result == VK_ERROR_OUT_OF_DATE_KHR)
                << "Cannot present in swapchain. error=" << static_cast<int32_t>(result);
    }

    // We presented the last acquired buffer.
    mAcquired = false;
    mIsFirstRenderPass = true;

    if (mFrameScheduled.callback) {
        driver.scheduleCallback(mFrameScheduled.handler,
                [callback = mFrameScheduled.callback]() {
                    PresentCallable noop = PresentCallable(PresentCallable::noopPresent, nullptr);
                    callback->operator()(noop);
                });
    }
}

/**
 * 从交换链获取下一张可渲染图像实现
 *
 * 执行步骤：
 * 1. 如果本帧已经 acquire 过，直接返回（`Driver::makeCurrent()` 可能多次调用）；
 * 2. 调用平台层 `hasResized` 检查窗口是否改变大小：
 *    - 如果需要重建且 `mFlushAndWaitOnResize` 为真，则先 flush + wait，确保 GPU 空闲；
 *    - 调用平台层 `recreate` 并重新 `update()` 内部纹理；
 * 3. 调用平台层 `acquire` 获取 `ImageSyncData`：
 *    - 如果失败，记录日志并返回（下一次 present 会跳过）；
 *    - 成功则记录 `mCurrentSwapIndex`，清空对应的 finishedDrawing 记录；
 *    - 检查返回码仅允许 `VK_SUCCESS / VK_SUBOPTIMAL_KHR`；
 *    - 如果提供了 `imageReadySemaphore`，通过 `mCommands->injectDependency` 将其注入为后续命令的依赖；
 * 4. 标记 `mAcquired = true`。
 *
 * @param resized 输出：是否在本次调用中检测到并处理了交换链重建
 */
void VulkanSwapChain::acquire(bool& resized) {
    // It's ok to call acquire multiple times due to it being linked to Driver::makeCurrent().
    if (mAcquired) {
        return;
    }

    // Check if the swapchain should be resized.
    if ((resized = mPlatform->hasResized(swapChain))) {
        if (mFlushAndWaitOnResize) {
            mCommands->flush();
            mCommands->wait();
        }
        mPlatform->recreate(swapChain);
        update();
    }

    VulkanPlatform::ImageSyncData imageSyncData;
    VkResult const result = mPlatform->acquire(swapChain, &imageSyncData);

    if (result != VK_SUCCESS) {
        // We just don't set mAcquired here so the next present will just skip.
        FVK_LOGD << "Failed to acquire next image in the swapchain result=" << (int) result;
        return;
    }

    mCurrentSwapIndex = imageSyncData.imageIndex;
    assert_invariant(mCurrentSwapIndex < mFinishedDrawing.size());
    mFinishedDrawing[mCurrentSwapIndex] = {};
    FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
            << "Cannot acquire in swapchain. error=" << static_cast<int32_t>(result);
    if (imageSyncData.imageReadySemaphore != VK_NULL_HANDLE) {
        mCommands->injectDependency(imageSyncData.imageReadySemaphore,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }
    mAcquired = true;
}

}// namespace filament::backend
