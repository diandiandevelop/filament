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

#include "fg/FrameGraphTexture.h"

#include "ResourceAllocator.h"

#include <utils/StaticString.h>

#include <algorithm>

namespace filament {

/**
 * 创建帧图纹理资源
 * 
 * 根据描述符创建硬件纹理资源。
 * 
 * @param resourceAllocator 资源分配器接口
 * @param name 资源名称
 * @param descriptor 纹理描述符
 * @param usage 使用方式
 * @param useProtectedMemory 是否使用受保护内存
 */
void FrameGraphTexture::create(ResourceAllocatorInterface& resourceAllocator,
        utils::StaticString const name,
        Descriptor const& descriptor, Usage usage,
        bool const useProtectedMemory) noexcept {
    /**
     * 如果使用受保护内存，添加 PROTECTED 使用标志
     * 
     * FIXME: 我认为应该限制为仅附件和 blit 目标
     */
    if (useProtectedMemory) {
        // FIXME: I think we should restrict this to attachments and blit destinations only
        usage |= Usage::PROTECTED;  // 添加受保护标志
    }
    /**
     * 构建通道重映射数组
     */
    std::array<backend::TextureSwizzle, 4> swizzle = {
            descriptor.swizzle.r,  // R 通道
            descriptor.swizzle.g,  // G 通道
            descriptor.swizzle.b,  // B 通道
            descriptor.swizzle.a };  // A 通道
    /**
     * 创建纹理并获取句柄
     */
    handle = resourceAllocator.createTexture(name,  // 名称
            descriptor.type,  // 采样器类型
            descriptor.levels,  // mip 级别数
            descriptor.format,  // 格式
            descriptor.samples,  // 采样数
            descriptor.width,  // 宽度
            descriptor.height,  // 高度
            descriptor.depth,  // 深度
            swizzle,  // 通道重映射
            usage);  // 使用方式
}

/**
 * 销毁帧图纹理资源
 * 
 * 释放硬件纹理资源。
 * 
 * @param resourceAllocator 资源分配器接口
 */
void FrameGraphTexture::destroy(ResourceAllocatorInterface& resourceAllocator) noexcept {
    if (handle) {
        resourceAllocator.destroyTexture(handle);  // 销毁纹理
        handle.clear();  // 清空句柄
    }
}

/**
 * 生成子资源描述符
 * 
 * 从父资源描述符和子资源描述符生成适合子资源的描述符。
 * 计算子资源的实际尺寸（考虑 mip 级别）。
 * 
 * @param descriptor 父资源描述符
 * @param srd 子资源描述符
 * @return 适合子资源的新描述符
 */
FrameGraphTexture::Descriptor FrameGraphTexture::generateSubResourceDescriptor(
        Descriptor descriptor,
        SubResourceDescriptor const& srd) noexcept {
    descriptor.levels = 1;  // 子资源只有 1 个 mip 级别
    /**
     * 根据 mip 级别计算子资源的尺寸
     * 
     * 每个 mip 级别尺寸减半，但至少为 1。
     */
    descriptor.width  = std::max(1u, descriptor.width >> srd.level);   // 宽度 = 原宽度 / 2^level
    descriptor.height = std::max(1u, descriptor.height >> srd.level);  // 高度 = 原高度 / 2^level
    return descriptor;
}

} // namespace filament
