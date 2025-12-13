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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPHTEXTURE_H
#define TNT_FILAMENT_FG_FRAMEGRAPHTEXTURE_H

#include "fg/FrameGraphId.h"

#include <utils/StaticString.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

namespace filament {
class ResourceAllocatorInterface;
} // namespace::filament

namespace filament {

/**
 * 帧图纹理资源结构
 * 
 * 帧图资源必须至少声明：
 * - struct Descriptor;  // 描述符结构
 * - struct SubResourceDescriptor;  // 子资源描述符结构
 * - Usage 位掩码类型  // 使用方式位掩码
 * 
 * 并且声明和定义：
 * - void create(ResourceAllocatorInterface&, const char* name, Descriptor const&, Usage,
 *               bool useProtectedMemory) noexcept;  // 创建资源
 * - void destroy(ResourceAllocatorInterface&) noexcept;  // 销毁资源
 */
struct FrameGraphTexture {
    backend::Handle<backend::HwTexture> handle;  // 硬件纹理句柄

    /**
     * 帧图纹理资源描述符
     * 
     * 描述纹理的尺寸、格式、类型等属性。
     */
    /** describes a FrameGraphTexture resource */
    struct Descriptor {
        uint32_t width = 1;     // 资源宽度（像素）
        uint32_t height = 1;    // 资源高度（像素）
        uint32_t depth = 1;     // 3D 纹理的图像数量
        uint8_t levels = 1;     // 纹理的 mip 级别数量
        uint8_t samples = 0;    // 采样数：0=自动，1=请求非多重采样，>1 仅用于不可采样纹理
        backend::SamplerType type = backend::SamplerType::SAMPLER_2D;     // 纹理目标类型
        backend::TextureFormat format = backend::TextureFormat::RGBA8;    // 资源内部格式
        /**
         * 纹理通道重映射（Swizzle）
         * 
         * 允许重新映射纹理通道（R、G、B、A）。
         */
        struct {
            using TS = backend::TextureSwizzle;
            union {
                backend::TextureSwizzle channels[4] = {
                        TS::CHANNEL_0, TS::CHANNEL_1, TS::CHANNEL_2, TS::CHANNEL_3 };  // 通道数组
                struct {
                    backend::TextureSwizzle r, g, b, a;  // 命名通道
                };
            };
        } swizzle;
    };

    /**
     * 帧图纹理子资源描述符
     * 
     * 描述纹理的子资源（mip 级别或层）。
     */
    /** Describes a FrameGraphTexture sub-resource */
    struct SubResourceDescriptor {
        uint8_t level = 0;      // 资源的 mip 级别
        uint8_t layer = 0;      // 资源的层或面（用于数组纹理或立方体贴图）
    };

    /**
     * 使用方式类型别名
     * 
     * 用于读取和写入的使用方式。
     */
    /** Usage for read and write */
    using Usage = backend::TextureUsage;
    static constexpr Usage DEFAULT_R_USAGE = Usage::SAMPLEABLE;  // 默认读取使用方式：可采样
    static constexpr Usage DEFAULT_W_USAGE = Usage::COLOR_ATTACHMENT;  // 默认写入使用方式：颜色附件

    /**
     * 创建具体资源
     * 
     * 根据描述符创建硬件纹理资源。
     * 
     * @param resourceAllocator 资源分配器（用于纹理等）
     * @param name 资源名称
     * @param descriptor 资源描述符
     * @param usage 使用方式
     * @param useProtectedMemory 是否使用受保护内存
     */
    void create(ResourceAllocatorInterface& resourceAllocator, utils::StaticString name,
            Descriptor const& descriptor, Usage usage, bool useProtectedMemory) noexcept;

    /**
     * 销毁具体资源
     * 
     * 释放硬件纹理资源。
     * 
     * @param resourceAllocator 资源分配器
     */
    void destroy(ResourceAllocatorInterface& resourceAllocator) noexcept;

    /**
     * 生成子资源描述符
     * 
     * 从父资源描述符和子资源描述符生成适合子资源的描述符。
     * 这包括计算子资源的实际尺寸（考虑 mip 级别）。
     * 
     * @param descriptor 父资源的描述符
     * @param srd 子资源的 SubResourceDescriptor
     * @return 适合此子资源的新描述符
     */
    static Descriptor generateSubResourceDescriptor(Descriptor descriptor,
            SubResourceDescriptor const& srd) noexcept;
};

} // namespace filament

#endif // TNT_FILAMENT_FG_FRAMEGRAPHTEXTURE_H
