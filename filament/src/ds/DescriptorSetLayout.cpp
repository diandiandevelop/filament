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

#include "DescriptorSetLayout.h"

#include "HwDescriptorSetLayoutFactory.h"

#include "details/Engine.h"

#include <utils/debug.h>
#include <utils/FixedCapacityVector.h>

#include <backend/DriverEnums.h>

#include <algorithm>
#include <utility>

namespace filament {

/**
 * 默认构造函数
 * 
 * 创建一个空的描述符堆布局。
 */
DescriptorSetLayout::DescriptorSetLayout() noexcept = default;

/**
 * 构造函数
 * 
 * 从后端描述符堆布局创建描述符堆布局。
 * 
 * @param factory 硬件描述符堆布局工厂引用
 * @param driver 驱动 API 引用
 * @param descriptorSetLayout 后端描述符堆布局（将被移动）
 */
DescriptorSetLayout::DescriptorSetLayout(
        HwDescriptorSetLayoutFactory& factory,
        backend::DriverApi& driver,
        backend::DescriptorSetLayout descriptorSetLayout) noexcept  {
    /**
     * 遍历所有绑定，计算最大绑定点并设置标志位
     */
    for (auto&& desc : descriptorSetLayout.bindings) {  // 遍历绑定
        mMaxDescriptorBinding = std::max(mMaxDescriptorBinding, desc.binding);  // 更新最大绑定点
        mSamplers.set(desc.binding, backend::DescriptorSetLayoutBinding::isSampler(desc.type));  // 设置采样器标志
        mUniformBuffers.set(desc.binding, desc.type == backend::DescriptorType::UNIFORM_BUFFER);  // 设置统一缓冲区标志
    }

    assert_invariant(mMaxDescriptorBinding < utils::bitset64::BIT_COUNT);  // 断言最大绑定点在有效范围内

    /**
     * 创建描述符类型向量并填充
     */
    mDescriptorTypes = utils::FixedCapacityVector<backend::DescriptorType>(mMaxDescriptorBinding + 1);  // 创建类型向量
    for (auto&& desc : descriptorSetLayout.bindings) {  // 遍历绑定
        mDescriptorTypes[desc.binding] = desc.type;  // 存储描述符类型
    }

    /**
     * 通过工厂创建硬件描述符堆布局
     */
    mDescriptorSetLayoutHandle = factory.create(driver,
            std::move(descriptorSetLayout));  // 创建硬件布局
}

/**
 * 终止描述符堆布局
 * 
 * 释放描述符堆布局的硬件资源。
 * 
 * @param factory 硬件描述符堆布局工厂引用
 * @param driver 驱动 API 引用
 */
void DescriptorSetLayout::terminate(
        HwDescriptorSetLayoutFactory& factory,
        backend::DriverApi& driver) noexcept {
    if (mDescriptorSetLayoutHandle) {  // 如果句柄有效
        factory.destroy(driver, mDescriptorSetLayoutHandle);  // 销毁硬件布局
    }
}

/**
 * 移动构造函数
 * 
 * @param rhs 右值引用
 */
DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& rhs) noexcept = default;

/**
 * 移动赋值运算符
 * 
 * @param rhs 右值引用
 * @return 自身引用
 */
DescriptorSetLayout& DescriptorSetLayout::operator=(DescriptorSetLayout&& rhs) noexcept = default;

} // namespace filament
