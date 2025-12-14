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

#ifndef TNT_FILAMENT_POSTPROCESSINGDESCRIPTORSET_H
#define TNT_FILAMENT_POSTPROCESSINGDESCRIPTORSET_H

#include "DescriptorSet.h"

#include "DescriptorSetLayout.h"

#include "TypedUniformBuffer.h"

#include <private/filament/UibStructs.h>

#include <backend/DriverApiForward.h>

namespace filament {

class FEngine;
class HwDescriptorSetLayoutFactory;

/**
 * 后处理描述符堆类
 * 
 * 管理后处理通道使用的描述符堆。
 * 后处理通道需要访问每视图 Uniform 数据（如投影矩阵、时间等）。
 * 
 * 功能：
 * - 初始化描述符堆布局和描述符堆
 * - 设置帧 Uniform 数据
 * - 绑定描述符堆到渲染管线
 */
class PostProcessDescriptorSet {
public:
    /**
     * 构造函数
     */
    explicit PostProcessDescriptorSet() noexcept;

    /**
     * 初始化后处理描述符堆
     * 
     * 创建描述符堆布局和描述符堆对象。
     * 
     * @param engine 引擎引用
     */
    void init(FEngine& engine) noexcept;

    /**
     * 终止后处理描述符堆
     * 
     * 释放硬件资源。
     * 
     * @param factory 硬件描述符堆布局工厂
     * @param driver 驱动 API 引用
     */
    void terminate(HwDescriptorSetLayoutFactory& factory, backend::DriverApi& driver);

    /**
     * 设置帧 Uniform 数据
     * 
     * 将每视图 Uniform 缓冲区绑定到描述符堆。
     * 
     * @param driver 驱动 API 引用
     * @param uniforms 每视图 Uniform 缓冲区引用
     */
    void setFrameUniforms(backend::DriverApi& driver,
            TypedUniformBuffer<PerViewUib>& uniforms) noexcept;

    /**
     * 绑定后处理描述符堆
     * 
     * 将描述符堆绑定到 PER_VIEW 绑定点。
     * 
     * @param driver 驱动 API 引用
     */
    void bind(backend::DriverApi& driver) noexcept;

    /**
     * 获取描述符堆布局
     * 
     * @return 描述符堆布局常量引用
     */
    filament::DescriptorSetLayout const& getLayout() const noexcept {
        return mDescriptorSetLayout;
    }

private:
    filament::DescriptorSetLayout mDescriptorSetLayout;  // 描述符堆布局（明确指定 filament 命名空间以避免与 backend::DescriptorSetLayout 冲突）
    DescriptorSet mDescriptorSet;  // 描述符堆
};

} // namespace filament

#endif //TNT_FILAMENT_POSTPROCESSINGDESCRIPTORSET_H
