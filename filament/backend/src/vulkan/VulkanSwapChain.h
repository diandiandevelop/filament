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

#ifndef TNT_FILAMENT_BACKEND_VULKANSWAPCHAIN_H
#define TNT_FILAMENT_BACKEND_VULKANSWAPCHAIN_H

#include "DriverBase.h"

#include "VulkanConstants.h"
#include "VulkanContext.h"
#include "VulkanTexture.h"
#include "vulkan/memory/Resource.h"

#include <backend/platforms/VulkanPlatform.h>

#include <bluevk/BlueVK.h>
#include <utils/FixedCapacityVector.h>

#include <memory>

using namespace bluevk;

namespace filament::backend {

class VulkanCommands;

/**
 * VulkanSwapChain - Vulkan 后端的交换链包装
 *
 * 负责将 Filament 的 `HwSwapChain` 抽象与平台层 `VulkanPlatform` 的交换链实现对接：
 * - 持有平台层的 swapChain 句柄与 `SwapChainBundle` 中的图像（封装成 `VulkanTexture`）；
 * - 提供 `acquire / present` 接口，用于获取下一张可绘制图像并提交到显示；
 * - 处理窗口尺寸变化、首帧渲染标记、受保护内容标志等；
 * - 支持 headless 模式（无窗口，仅离屏渲染）。
 */
struct VulkanSwapChain : public HwSwapChain, fvkmemory::Resource {
    /**
     * 构造函数
     *
     * @param platform        平台抽象（负责创建 / 销毁底层 Vulkan 交换链）
     * @param context         Vulkan 上下文
     * @param resourceManager Vulkan 资源管理器
     * @param allocator       VMA 分配器
     * @param commands        命令管理器
     * @param stagePool       staging buffer 池（用于纹理上传）
     * @param nativeWindow    原生窗口句柄（为 null 且 extent!=0 时为 headless 模式）
     * @param flags           平台 / 后端配置标志（受保护内容、立体等）
     * @param extent          交换链尺寸（{0,0} 表示由平台决定）
     */
    VulkanSwapChain(VulkanPlatform* platform, VulkanContext const& context,
            fvkmemory::ResourceManager* resourceManager, VmaAllocator allocator,
            VulkanCommands* commands, VulkanStagePool& stagePool, void* nativeWindow,
            uint64_t flags, VkExtent2D extent = {0, 0});

    ~VulkanSwapChain();

    /**
     * 提交当前帧到交换链
     *
     * 流程：
     * 1. 如有需要，将当前 color image 的布局转换为 PRESENT；
     * 2. flush 命令缓冲；
     * 3. 调用平台层的 `present` 提交到显示（非 headless 模式才执行）；
     * 4. 触发 frame scheduled 回调（如果已注册）。
     */
    void present(DriverBase& driver);

    /**
     * 从交换链中获取下一张图像
     *
     * 流程：
     * 1. 如检测到窗口尺寸变化，则可选地 flush+wait 并调用平台层重建交换链；
     * 2. 调用平台层 `acquire` 获取下一张图像及其同步信号量；
     * 3. 记录当前 swap index，清空对应的 finishedDrawing 信号量；
     * 4. 将获取到的 imageReadySemaphore 注入到命令队列依赖中。
     *
     * @param resized  输出标志：如果本次调用触发或检测到重建交换链，则为 true
     */
    void acquire(bool& resized);

    // 获取当前帧对应的颜色附件纹理
    fvkmemory::resource_ptr<VulkanTexture> getCurrentColor() const noexcept {
        uint32_t const imageIndex = mCurrentSwapIndex;
        FILAMENT_CHECK_PRECONDITION(
            imageIndex != VulkanPlatform::ImageSyncData::INVALID_IMAGE_INDEX);
        return mColors[imageIndex];
    }

    // 获取深度附件纹理
    inline fvkmemory::resource_ptr<VulkanTexture> getDepth() const noexcept {
        return mDepth;
    }

    // 返回是否为本 swapchain 第一次渲染（首个 render pass）
    inline bool isFirstRenderPass() const noexcept {
        return mIsFirstRenderPass;
    }

    // 标记已经完成第一次渲染
    inline void markFirstRenderPass() noexcept {
        mIsFirstRenderPass = false;
    }

    // 获取当前交换链的尺寸
    inline VkExtent2D getExtent() noexcept {
        return mExtent;
    }

    // 查询交换链是否为受保护内容（如 DRM / Widevine）
    inline bool isProtected() noexcept {
        return mPlatform->isProtected(swapChain);
    }

    // 设置“帧调度完成”回调（在 present 之后由 Driver 调度）
    inline void setFrameScheduledCallback(CallbackHandler* handler,
            FrameScheduledCallback&& callback) noexcept {
        if (!callback) {
            mFrameScheduled.handler = nullptr;
            mFrameScheduled.callback.reset();
            return;
        }
        mFrameScheduled.handler = handler;
        mFrameScheduled.callback = std::make_shared<FrameScheduledCallback>(std::move(callback));
    }

private:
    // 同一时间可并发使用的“图像就绪”信号量数量（与命令缓冲数量一致）
    static constexpr int IMAGE_READY_SEMAPHORE_COUNT = FVK_MAX_COMMAND_BUFFERS;

    // 重新从平台层交换链查询图像 / 格式等信息，并重建 mColors / mDepth 等资源
    void update();

    VulkanPlatform* mPlatform;                     // 平台层接口（创建 / 销毁 / acquire / present）
    VulkanContext const& mContext;                 // Vulkan 上下文
    fvkmemory::ResourceManager* mResourceManager;  // 资源管理器
    VulkanCommands* mCommands;                     // 命令管理器
    VmaAllocator mAllocator;                       // VMA 分配器
    VulkanStagePool& mStagePool;                   // staging buffer 池
    bool const mHeadless;                          // 是否为 headless 模式（无窗口）
    bool const mFlushAndWaitOnResize;              // 窗口重建前是否先 flush+wait GPU
    bool const mTransitionSwapChainImageLayoutForPresent; // 是否在 present 前转换布局为 PRESENT

    // These fields store a callback to notify the client that Filament is commiting a frame.
    struct {
        CallbackHandler* handler = nullptr;                         // 回调处理器
        std::shared_ptr<FrameScheduledCallback> callback;           // 帧调度回调
    } mFrameScheduled;

    // We create VulkanTextures based on VkImages. VulkanTexture has facilities for doing layout
    // transitions, which are useful here.
    // 基于 VkImage 创建的颜色附件纹理列表，每个元素对应 swapchain 的一张 image
    utils::FixedCapacityVector<fvkmemory::resource_ptr<VulkanTexture>> mColors;
    // 每张 swap image 对应的“渲染完成”信号量（由命令系统提供）
    utils::FixedCapacityVector<fvkmemory::resource_ptr<VulkanSemaphore>> mFinishedDrawing;
    // 深度附件纹理
    fvkmemory::resource_ptr<VulkanTexture> mDepth;
    VkExtent2D mExtent;          // 当前交换链的尺寸
    uint32_t mLayerCount;        // 图层数量（立方体 / VR 等场景下可>1）
    uint32_t mCurrentSwapIndex;  // 当前使用的 swap image 索引
    bool mAcquired;              // 当前帧是否已成功 acquire image
    bool mIsFirstRenderPass;     // 是否为本 swapchain 的首次 render pass
};


}// namespace filament::backend

#endif// TNT_FILAMENT_BACKEND_VULKANSWAPCHAIN_H
