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

#ifndef TNT_FILAMENT_BACKEND_VULKANBLITTER_H
#define TNT_FILAMENT_BACKEND_VULKANBLITTER_H

#include "VulkanCommands.h"
#include "VulkanContext.h"

#include <utils/compiler.h>

namespace filament::backend {

/**
 * VulkanBlitter - Vulkan 纹理 Blit / Resolve 工具类
 *
 * 封装了 Vulkan 中常用的图像复制、缩放（blit）以及多重采样解析（resolve）操作，
 * 统一通过 VulkanCommands 录制到命令缓冲中。
 */
class VulkanBlitter {
public:
    /**
     * 构造函数
     *
     * @param physicalDevice Vulkan 物理设备句柄（用于能力查询等）
     * @param commands       命令管理器，用于录制和提交 Vulkan 命令
     */
    VulkanBlitter(VkPhysicalDevice physicalDevice, VulkanCommands* commands) noexcept;

    /**
     * 执行图像 Blit 操作
     *
     * 支持在不同尺寸 / 不同区域之间复制纹理，并可选择插值过滤方式（线性 / 最近邻）。
     *
     * @param filter       采样过滤方式（VkFilter::VK_FILTER_LINEAR / VK_FILTER_NEAREST）
     * @param dst          目标附件（图像 + 子资源范围）
     * @param dstRectPair  目标矩形（起点 + 终点，数组长度为2）
     * @param src          源附件
     * @param srcRectPair  源矩形（起点 + 终点，数组长度为2）
     */
    void blit(VkFilter filter,
            VulkanAttachment dst, const VkOffset3D* dstRectPair,
            VulkanAttachment src, const VkOffset3D* srcRectPair);

    /**
     * 执行多重采样解析（Resolve）
     *
     * 将多重采样颜色附件解析为单样本颜色附件（MSAA -> non-MSAA）。
     *
     * @param dst 目标附件（单样本）
     * @param src 源附件（多重采样）
     */
    void resolve(VulkanAttachment dst, VulkanAttachment src);

    /**
     * 终止并释放与 Blitter 相关的临时资源
     */
    void terminate() noexcept;

private:
    UTILS_UNUSED VkPhysicalDevice mPhysicalDevice; // Vulkan 物理设备（当前主要用于能力查询）
    VulkanCommands* mCommands;                     // 命令管理器（用于录制 Blit / Resolve 命令）
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_VULKANBLITTER_H
