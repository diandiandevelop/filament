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

#include "VulkanBlitter.h"
#include "VulkanCommands.h"
#include "VulkanContext.h"
#include "VulkanTexture.h"
#include "vulkan/utils/Image.h"

#include <utils/FixedCapacityVector.h>
#include <utils/Panic.h>

#include <smolv.h>

using namespace bluevk;
using namespace utils;

namespace filament::backend {

namespace {

// 快速路径的 Blit 实现：
// - 负责处理同一设备上、兼容格式的图像之间的区域复制 / 缩放；
// - 自动处理源 / 目标图像布局的转换与还原；
// - 在开启调试宏时输出详细的调试日志。
inline void blitFast(VulkanCommandBuffer* commands, VkImageAspectFlags aspect, VkFilter filter,
        VulkanAttachment src, VulkanAttachment dst,
        const VkOffset3D srcRect[2], const VkOffset3D dstRect[2]) {
    VkCommandBuffer const cmdbuf = commands->buffer();
    if constexpr (FVK_ENABLED(FVK_DEBUG_BLITTER)) {
        FVK_LOGE << "Fast blit from=" << src.texture->getVkImage() << ", level=" << (int) src.level
                      << ", layer=" << (int) src.layer
                      << ", layout=" << src.getLayout()
                      << ", src-rect=(" << srcRect[0].x << "," << srcRect[0].y << "," << srcRect[0].z << ")"
                      << "->(" << srcRect[1].x << "," << srcRect[1].y << "," << srcRect[1].z << ")"
                      << " to=" << dst.texture->getVkImage() << ", level=" << (int) dst.level
                      << ", layer=" << (int) dst.layer
                      << ", layout=" << dst.getLayout()
                      << ", dst-rect=(" << dstRect[0].x << "," << dstRect[0].y << "," << dstRect[0].z << ")"
                      << "->(" << dstRect[1].x << "," << dstRect[1].y << "," << dstRect[1].z << ")";
    }

    VkImageSubresourceRange const srcRange = src.getSubresourceRange();
    VkImageSubresourceRange const dstRange = dst.getSubresourceRange();

    VulkanLayout oldSrcLayout = src.getLayout();
    VulkanLayout oldDstLayout = dst.getLayout();

    src.texture->transitionLayout(commands, srcRange, VulkanLayout::TRANSFER_SRC);
    dst.texture->transitionLayout(commands, dstRange, VulkanLayout::TRANSFER_DST);

    const VkImageBlit blitRegions[1] = {{
            .srcSubresource = { aspect, src.level, src.layer, 1 },
            .srcOffsets = { srcRect[0], srcRect[1] },
            .dstSubresource = { aspect, dst.level, dst.layer, 1 },
            .dstOffsets = { dstRect[0], dstRect[1] },
    }};
    vkCmdBlitImage(cmdbuf,
            src.getImage(), fvkutils::getVkLayout(VulkanLayout::TRANSFER_SRC),
            dst.getImage(), fvkutils::getVkLayout(VulkanLayout::TRANSFER_DST),
            1, blitRegions, filter);

    if (oldSrcLayout == VulkanLayout::UNDEFINED) {
        oldSrcLayout = src.texture->getDefaultLayout();
    }
    if (oldDstLayout == VulkanLayout::UNDEFINED) {
        oldDstLayout = dst.texture->getDefaultLayout();
    }

    src.texture->transitionLayout(commands, srcRange, oldSrcLayout);
    dst.texture->transitionLayout(commands, dstRange, oldDstLayout);
}

// 快速路径的多重采样解析（resolve）实现：
// - 当前仅支持颜色附件（不支持深度解析）；
// - 自动处理目标图像布局的转换与还原；
// - 在开启调试宏时输出详细的调试日志。
inline void resolveFast(VulkanCommandBuffer* commands, VkImageAspectFlags aspect,
        VulkanAttachment src, VulkanAttachment dst) {
    VkCommandBuffer const cmdbuffer = commands->buffer();
    if constexpr (FVK_ENABLED(FVK_DEBUG_BLITTER)) {
        FVK_LOGD << "Fast blit from=" << src.texture->getVkImage() << ",level=" << (int) src.level
                      << " layout=" << src.getLayout()
                      << " to=" << dst.texture->getVkImage() << ",level=" << (int) dst.level
                      << " layout=" << dst.getLayout();
    }

    VkImageSubresourceRange const srcRange = src.getSubresourceRange();
    VkImageSubresourceRange const dstRange = dst.getSubresourceRange();

    VulkanLayout oldSrcLayout = src.getLayout();
    VulkanLayout oldDstLayout = dst.getLayout();

    dst.texture->transitionLayout(commands, dstRange, VulkanLayout::TRANSFER_DST);

    assert_invariant(
            aspect != VK_IMAGE_ASPECT_DEPTH_BIT && "Resolve with depth is not yet supported.");
    const VkImageResolve resolveRegions[1] = {{
            .srcSubresource = { aspect, src.level, src.layer, 1 },
            .srcOffset = { 0, 0 },
            .dstSubresource = { aspect, dst.level, dst.layer, 1 },
            .dstOffset = { 0, 0 },
            .extent = { src.getExtent2D().width, src.getExtent2D().height, 1 },
    }};
    vkCmdResolveImage(cmdbuffer,
            src.getImage(), fvkutils::getVkLayout(src.getLayout()),
            dst.getImage(), fvkutils::getVkLayout(VulkanLayout::TRANSFER_DST),
            1, resolveRegions);
    if (oldSrcLayout == VulkanLayout::UNDEFINED) {
        oldSrcLayout = src.texture->getDefaultLayout();
    }
    if (oldDstLayout == VulkanLayout::UNDEFINED) {
        oldDstLayout = dst.texture->getDefaultLayout();
    }
    src.texture->transitionLayout(commands, srcRange, oldSrcLayout);
    dst.texture->transitionLayout(commands, dstRange, oldDstLayout);
}

struct BlitterUniforms {
    int sampleCount;
    float inverseSampleCount;
};

}// anonymous namespace

VulkanBlitter::VulkanBlitter(VkPhysicalDevice physicalDevice, VulkanCommands* commands) noexcept
        : mPhysicalDevice(physicalDevice), mCommands(commands) {}

/**
 * 多重采样解析（Resolve）实现
 *
 * 步骤：
 * 1. 检查源 / 目标格式是否支持作为 blit / resolve 源和目标（在调试模式下）；
 * 2. 根据目标纹理是否受保护选择对应的命令缓冲（普通 / 受保护）；
 * 3. 获取并保留源 / 目标纹理的引用（防止在命令执行前被销毁）；
 * 4. 调用 resolveFast 录制并执行实际的 vkCmdResolveImage 调用。
 *
 * @param dst 目标附件（单样本）
 * @param src 源附件（多重采样）
 */
void VulkanBlitter::resolve(VulkanAttachment dst, VulkanAttachment src) {

    // src and dst should have the same aspect here
    VkImageAspectFlags const aspect = src.texture->getImageAspect();

    assert_invariant(!(aspect & VK_IMAGE_ASPECT_DEPTH_BIT));

#if FVK_ENABLED(FVK_DEBUG_BLIT_FORMAT)
    VkPhysicalDevice const gpu = mPhysicalDevice;
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(gpu, src.getFormat(), &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT,
                "Depth src format is not blittable %d", src.getFormat())) {
        return;
    }
    vkGetPhysicalDeviceFormatProperties(gpu, dst.getFormat(), &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT,
                "Depth dst format is not blittable %d", dst.getFormat())) {
        return;
    }
#endif

    VulkanCommandBuffer& commands = dst.texture->getIsProtected() ?
            mCommands->getProtected() : mCommands->get();
    commands.acquire(src.texture);
    commands.acquire(dst.texture);
    resolveFast(&commands, aspect, src, dst);
}

/**
 * 图像 Blit 实现
 *
 * 步骤：
 * 1. 在调试模式下检查源 / 目标格式是否支持 blit 操作；
 * 2. 根据目标纹理是否受保护选择对应的命令缓冲；
 * 3. 获取并保留源 / 目标纹理的引用；
 * 4. 调用 blitFast 录制并执行实际的 vkCmdBlitImage 调用。
 *
 * @param filter       采样过滤方式（线性 / 最近邻）
 * @param dst          目标附件
 * @param dstRectPair  目标矩形（起点 + 终点）
 * @param src          源附件
 * @param srcRectPair  源矩形（起点 + 终点）
 */
void VulkanBlitter::blit(VkFilter filter,
        VulkanAttachment dst, const VkOffset3D* dstRectPair,
        VulkanAttachment src, const VkOffset3D* srcRectPair) {
#if FVK_ENABLED(FVK_DEBUG_BLIT_FORMAT)
    VkPhysicalDevice const gpu = mPhysicalDevice;
    VkFormatProperties info;
    vkGetPhysicalDeviceFormatProperties(gpu, src.getFormat(), &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT,
                "Depth src format is not blittable %d", src.getFormat())) {
        return;
    }
    vkGetPhysicalDeviceFormatProperties(gpu, dst.getFormat(), &info);
    if (!ASSERT_POSTCONDITION_NON_FATAL(info.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT,
                "Depth dst format is not blittable %d", dst.getFormat())) {
        return;
    }
#endif
    // src and dst should have the same aspect here
    VkImageAspectFlags const aspect = src.texture->getImageAspect();
    VulkanCommandBuffer& commands = dst.texture->getIsProtected() ?
            mCommands->getProtected() : mCommands->get();
    commands.acquire(src.texture);
    commands.acquire(dst.texture);
    blitFast(&commands, aspect, filter, src, dst, srcRectPair, dstRectPair);
}

// 当前 Blitter 无需显式持有额外 GPU 资源，因此 terminate 为空实现。
void VulkanBlitter::terminate() noexcept {
}

} // namespace filament::backend
