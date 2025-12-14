/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_TARGETBUFFERINFO_H
#define TNT_FILAMENT_BACKEND_TARGETBUFFERINFO_H

#include <backend/Handle.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

//! \privatesection

/**
 * 渲染目标缓冲区信息
 * 
 * 描述用作渲染目标的纹理及其相关参数。
 * 
 * 用途：
 * - 在 RenderTarget 中指定颜色、深度、模板附件
 * - 支持多渲染目标（MRT）
 * - 支持纹理数组、立方体贴图等复杂纹理类型
 * 
 * 组成：
 * - handle: 纹理句柄
 * - level: Mip 级别
 * - layer: 层索引（用于数组纹理、立方体贴图、多视图等）
 */
struct TargetBufferInfo {
    /**
     * 构造函数：指定纹理、Mip 级别和层
     * 
     * 注意：参数顺序与结构体字段顺序不同。
     * 
     * @param handle 纹理句柄，使用移动语义
     * @param level  Mip 级别（0 表示最高分辨率）
     * @param layer  层索引
     *               - 立方体贴图：表示面（参见 TextureCubemapFace）
     *               - 2D 数组/立方体数组/3D 纹理：表示单个层的索引
     *               - 多视图纹理：表示起始层索引
     */
    // note: the parameters of this constructor are not in the order of this structure's fields
    TargetBufferInfo(Handle<HwTexture> handle, uint8_t const level, uint16_t const layer) noexcept
            : handle(std::move(handle)), level(level), layer(layer) {
    }

    /**
     * 构造函数：指定纹理和 Mip 级别
     * 
     * @param handle 纹理句柄
     * @param level  Mip 级别（0 表示最高分辨率）
     * 
     * layer 使用默认值 0
     */
    TargetBufferInfo(Handle<HwTexture> handle, uint8_t const level) noexcept
            : handle(handle), level(level) {
    }

    /**
     * 构造函数：仅指定纹理
     * 
     * @param handle 纹理句柄
     * 
     * level 和 layer 使用默认值 0
     */
    TargetBufferInfo(Handle<HwTexture> handle) noexcept // NOLINT(*-explicit-constructor)
            : handle(handle) {
    }

    /**
     * 默认构造函数
     * 
     * 创建一个空的 TargetBufferInfo，所有成员使用默认值。
     */
    TargetBufferInfo() noexcept = default;

    /**
     * Texture handle to be used as render target
     * 
     * 用作渲染目标的纹理句柄
     * - 必须是有效的纹理句柄
     * - 纹理必须支持作为渲染目标（TextureUsage::COLOR_ATTACHMENT 等）
     */
    // texture to be used as render target
    Handle<HwTexture> handle;

    /**
     * Mip level to be used
     * 
     * 要使用的 Mip 级别
     * - 0 表示最高分辨率（原始大小）
     * - 更高的值表示更低的分辨率
     * - 用于渲染到纹理的特定 Mip 级别
     */
    // level to be used
    uint8_t level = 0;

    /**
     * Layer index
     * 
     * 层索引，含义取决于纹理类型：
     * 
     * - 立方体贴图：表示立方体的面（参见 TextureCubemapFace 的面->层映射）
     * - 2D 数组、立方体数组、3D 纹理：表示单个层的索引
     * - 多视图纹理（RenderTarget 的 layerCount > 1）：表示当前 2D 数组纹理用于多视图的起始层索引
     */
    // - For cubemap textures, this indicates the face of the cubemap. See TextureCubemapFace for
    //   the face->layer mapping)
    // - For 2d array, cubemap array, and 3d textures, this indicates an index of a single layer of
    //   them.
    // - For multiview textures (i.e., layerCount for the RenderTarget is greater than 1), this
    //   indicates a starting layer index of the current 2d array texture for multiview.
    uint16_t layer = 0;
};

/**
 * 多渲染目标（MRT - Multiple Render Targets）
 * 
 * 用于管理多个颜色附件的容器类，支持同时渲染到多个纹理。
 * 
 * 特性：
 * - 支持最多 MAX_SUPPORTED_RENDER_TARGET_COUNT 个渲染目标
 * - 提供类似数组的访问接口
 * - 支持从 1 到 4 个目标的便捷构造函数
 * 
 * 用途：
 * - 延迟渲染（G-Buffer）
 * - 多输出着色器
 * - 同时渲染到多个纹理
 * 
 * 注意：
 * - 更新 MAX_SUPPORTED_RENDER_TARGET_COUNT 时，也要更新 RenderTarget.java
 */
class MRT {
public:
    /**
     * 最小支持的渲染目标数量
     * 
     * 所有后端必须至少支持这个数量的渲染目标。
     */
    static constexpr uint8_t MIN_SUPPORTED_RENDER_TARGET_COUNT = 4u;

    /**
     * 最大支持的渲染目标数量
     * 
     * 当前实现支持的最大渲染目标数量。
     * 
     * 注意：更新此值时，也要更新 RenderTarget.java
     */
    // When updating this, make sure to also take care of RenderTarget.java
    static constexpr uint8_t MAX_SUPPORTED_RENDER_TARGET_COUNT = 8u;

private:
    /**
     * 渲染目标信息数组
     * 
     * 存储最多 MAX_SUPPORTED_RENDER_TARGET_COUNT 个 TargetBufferInfo。
     */
    TargetBufferInfo mInfos[MAX_SUPPORTED_RENDER_TARGET_COUNT];

public:
    /**
     * 下标操作符（const）
     * 
     * 访问指定索引的渲染目标信息。
     * 
     * @param i 索引（0 到 MAX_SUPPORTED_RENDER_TARGET_COUNT - 1）
     * @return 渲染目标信息的常量引用
     */
    TargetBufferInfo const& operator[](size_t const i) const noexcept {
        return mInfos[i];
    }

    /**
     * 下标操作符（非 const）
     * 
     * 访问指定索引的渲染目标信息。
     * 
     * @param i 索引（0 到 MAX_SUPPORTED_RENDER_TARGET_COUNT - 1）
     * @return 渲染目标信息的引用
     */
    TargetBufferInfo& operator[](size_t const i) noexcept {
        return mInfos[i];
    }

    /**
     * 默认构造函数
     * 
     * 创建一个空的 MRT，所有元素使用默认值。
     */
    MRT() noexcept = default;

    /**
     * 构造函数：单个颜色附件
     * 
     * @param color 颜色附件信息
     */
    MRT(TargetBufferInfo const& color) noexcept // NOLINT(hicpp-explicit-conversions, *-explicit-constructor)
            : mInfos{ color } {
    }

    /**
     * 构造函数：两个颜色附件
     * 
     * @param color0 第一个颜色附件
     * @param color1 第二个颜色附件
     */
    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1) noexcept
            : mInfos{ color0, color1 } {
    }

    /**
     * 构造函数：三个颜色附件
     * 
     * @param color0 第一个颜色附件
     * @param color1 第二个颜色附件
     * @param color2 第三个颜色附件
     */
    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1,
        TargetBufferInfo const& color2) noexcept
            : mInfos{ color0, color1, color2 } {
    }

    /**
     * 构造函数：四个颜色附件
     * 
     * @param color0 第一个颜色附件
     * @param color1 第二个颜色附件
     * @param color2 第三个颜色附件
     * @param color3 第四个颜色附件
     */
    MRT(TargetBufferInfo const& color0, TargetBufferInfo const& color1,
        TargetBufferInfo const& color2, TargetBufferInfo const& color3) noexcept
            : mInfos{ color0, color1, color2, color3 } {
    }

    /**
     * 构造函数：向后兼容（已弃用）
     * 
     * 从纹理句柄、Mip 级别和层直接构造。
     * 保留此构造函数以保持向后兼容性。
     * 
     * @param handle 纹理句柄
     * @param level  Mip 级别
     * @param layer  层索引
     */
    // this is here for backward compatibility
    MRT(Handle<HwTexture> handle, uint8_t level, uint16_t layer) noexcept
            : mInfos{{ handle, level, layer }} {
    }
};

} // namespace filament::backend

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::TargetBufferInfo& tbi);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::MRT& mrt);
#endif

#endif //TNT_FILAMENT_BACKEND_TARGETBUFFERINFO_H
