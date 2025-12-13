/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SSRPASSDESCRIPTORSET_H
#define TNT_FILAMENT_SSRPASSDESCRIPTORSET_H

#include "DescriptorSet.h"

#include "TypedUniformBuffer.h"

#include <private/filament/UibStructs.h>

#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

namespace filament {

class FEngine;

struct ScreenSpaceReflectionsOptions;

/**
 * SSR 通道描述符集
 * 
 * 管理屏幕空间反射（Screen-Space Reflections，SSR）通道的描述符集。
 * 用于绑定 SSR 通道所需的统一缓冲区和纹理采样器。
 */
class SsrPassDescriptorSet {

    using TextureHandle = backend::Handle<backend::HwTexture>;  // 纹理句柄类型别名

public:
    /**
     * 构造函数
     */
    SsrPassDescriptorSet() noexcept;

    /**
     * 初始化描述符集
     * 
     * 创建描述符集布局和统一缓冲区。
     * 
     * @param engine 引擎引用
     */
    void init(FEngine& engine) noexcept;

    /**
     * 终止描述符集
     * 
     * 释放所有资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver);

    /**
     * 设置帧统一数据
     * 
     * 从每视图统一缓冲区复制帧统一数据。
     * 
     * @param engine 引擎常量引用
     * @param uniforms 每视图统一缓冲区引用
     */
    void setFrameUniforms(FEngine const& engine, TypedUniformBuffer<PerViewUib>& uniforms) noexcept;

    /**
     * 准备结构纹理
     * 
     * 设置结构纹理采样器（用于 SSR 的深度和法线信息）。
     * 
     * @param engine 引擎常量引用
     * @param structure 结构纹理句柄
     */
    void prepareStructure(FEngine const& engine, TextureHandle structure) noexcept;

    /**
     * 准备 SSR 历史纹理
     * 
     * 设置 SSR 历史纹理采样器（用于时间累积）。
     * 
     * @param engine 引擎常量引用
     * @param ssr SSR 历史纹理句柄
     */
    void prepareHistorySSR(FEngine const& engine, TextureHandle ssr) noexcept;

    /**
     * 提交统一数据
     * 
     * 将本地数据更新到 GPU UBO。
     * 
     * @param engine 引擎引用
     */
    // update local data into GPU UBO
    void commit(FEngine& engine) noexcept;

    /**
     * 绑定描述符集
     * 
     * 绑定此描述符集到渲染管线。
     * 
     * @param driver 驱动 API 引用
     */
    // bind this descriptor set
    void bind(backend::DriverApi& driver) noexcept;

private:
    DescriptorSet mDescriptorSet;  // 描述符集
    backend::BufferObjectHandle mShadowUbh;  // 阴影统一缓冲区句柄（虚拟，用于 Vulkan 验证层）
};

} // namespace filament

#endif //TNT_FILAMENT_SSRPASSDESCRIPTORSET_H
