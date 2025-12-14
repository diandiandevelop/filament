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

#ifndef TNT_FILAMENT_DESCRIPTORSETLAYOUT_H
#define TNT_FILAMENT_DESCRIPTORSETLAYOUT_H

#include <backend/DriverEnums.h>

#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

#include <utils/bitset.h>
#include <utils/FixedCapacityVector.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class HwDescriptorSetLayoutFactory;

/**
 * 描述符堆布局类
 * 
 * 管理描述符堆的布局定义，描述描述符堆中每个绑定点（binding point）的类型和属性。
 * 
 * 功能：
 * - 定义描述符堆的结构（哪些绑定点是采样器，哪些是 Uniform 缓冲区）
 * - 管理硬件描述符堆布局句柄
 * - 提供查询接口（检查绑定点类型、有效性等）
 * 
 * 实现细节：
 * - 使用位集（bitset）跟踪采样器和 Uniform 缓冲区的绑定
 * - 通过工厂类管理硬件资源的生命周期
 */
class DescriptorSetLayout {
public:
    /**
     * 默认构造函数
     * 
     * 创建空的描述符堆布局。
     */
    DescriptorSetLayout() noexcept;
    
    /**
     * 构造函数
     * 
     * 从后端描述符堆布局创建描述符堆布局对象。
     * 
     * @param factory 硬件描述符堆布局工厂
     * @param driver 驱动 API 引用
     * @param descriptorSetLayout 后端描述符堆布局
     */
    DescriptorSetLayout(
            HwDescriptorSetLayoutFactory& factory,
            backend::DriverApi& driver,
            backend::DescriptorSetLayout descriptorSetLayout) noexcept;

    /**
     * 禁止拷贝构造
     */
    DescriptorSetLayout(DescriptorSetLayout const&) = delete;
    
    /**
     * 移动构造函数
     */
    DescriptorSetLayout(DescriptorSetLayout&& rhs) noexcept;
    
    /**
     * 禁止拷贝赋值
     */
    DescriptorSetLayout& operator=(DescriptorSetLayout const&) = delete;
    
    /**
     * 移动赋值操作符
     */
    DescriptorSetLayout& operator=(DescriptorSetLayout&& rhs) noexcept;

    /**
     * 终止描述符堆布局
     * 
     * 释放硬件资源。
     * 
     * @param factory 硬件描述符堆布局工厂
     * @param driver 驱动 API 引用
     */
    void terminate(
            HwDescriptorSetLayoutFactory& factory,
            backend::DriverApi& driver) noexcept;

    /**
     * 获取硬件句柄
     * 
     * @return 描述符堆布局句柄
     */
    backend::DescriptorSetLayoutHandle getHandle() const noexcept {
        return mDescriptorSetLayoutHandle;
    }

    /**
     * 获取最大描述符绑定索引
     * 
     * @return 最大绑定索引
     */
    size_t getMaxDescriptorBinding() const noexcept {
        return mMaxDescriptorBinding;
    }

    /**
     * 检查绑定点是否有效
     * 
     * 绑定点有效当且仅当它是采样器或 Uniform 缓冲区。
     * 
     * @param binding 绑定索引
     * @return 如果绑定点有效返回 true，否则返回 false
     */
    bool isValid(backend::descriptor_binding_t const binding) const noexcept {
        return mSamplers[binding] || mUniformBuffers[binding];
    }

    /**
     * 检查绑定点是否为采样器
     * 
     * @param binding 绑定索引
     * @return 如果是采样器返回 true，否则返回 false
     */
    bool isSampler(backend::descriptor_binding_t const binding) const noexcept {
        return mSamplers[binding];
    }

    /**
     * 获取所有有效描述符的位集
     * 
     * @return 有效描述符位集（采样器和 Uniform 缓冲区的并集）
     */
    utils::bitset64 getValidDescriptors() const noexcept {
        return mSamplers | mUniformBuffers;
    }

    /**
     * 获取采样器描述符位集
     * 
     * @return 采样器描述符位集
     */
    utils::bitset64 getSamplerDescriptors() const noexcept {
        return mSamplers;
    }

    /**
     * 获取 Uniform 缓冲区描述符位集
     * 
     * @return Uniform 缓冲区描述符位集
     */
    utils::bitset64 getUniformBufferDescriptors() const noexcept {
        return mUniformBuffers;
    }

    /**
     * 获取描述符类型
     * 
     * 返回指定绑定点的描述符类型（如 UNIFORM_BUFFER、SAMPLER_2D 等）。
     * 
     * @param binding 绑定索引
     * @return 描述符类型
     */
    backend::DescriptorType getDescriptorType(
        backend::descriptor_binding_t const binding) const noexcept {
        return mDescriptorTypes[binding];  // 返回描述符类型
    }

private:
    backend::DescriptorSetLayoutHandle mDescriptorSetLayoutHandle;  // 硬件描述符堆布局句柄
    utils::bitset64 mSamplers;  // 采样器描述符位集（标记哪些绑定点是采样器）
    utils::bitset64 mUniformBuffers;  // Uniform 缓冲区描述符位集（标记哪些绑定点是 UBO）
    uint8_t mMaxDescriptorBinding = 0;  // 最大描述符绑定索引
    utils::FixedCapacityVector<backend::DescriptorType> mDescriptorTypes;  // 描述符类型数组（每个绑定点的类型）
};


} // namespace filament

#endif //TNT_FILAMENT_DESCRIPTORSETLAYOUT_H
