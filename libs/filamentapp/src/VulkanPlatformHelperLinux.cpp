/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <backend/platforms/VulkanPlatformLinux.h>

#include "VulkanPlatformHelperCommon.h"

namespace filament::filamentapp {

using namespace filament::backend;

/**
 * FilamentAppVulkanPlatform - Linux平台的Vulkan平台实现
 * 
 * 继承自VulkanPlatformLinux，提供GPU偏好配置功能。
 */
class FilamentAppVulkanPlatform : public VulkanPlatformLinux {
public:
    /**
     * 构造函数
     * @param gpuHintCstr GPU偏好提示字符串
     */
    FilamentAppVulkanPlatform(char const* gpuHintCstr) : mCustomization(parseGpuHint(gpuHintCstr)) {}

    /**
     * 获取自定义配置实现
     * @return Vulkan平台自定义配置
     */
    VulkanPlatform::Customization getCustomization() const noexcept override {
        return mCustomization;
    }

private:
    VulkanPlatform::Customization mCustomization; // 自定义配置
};

/**
 * 创建Vulkan平台对象实现（Linux平台）
 * 
 * @param gpuHintCstr GPU偏好提示字符串
 * @return Vulkan平台对象指针
 */
VulkanPlatform* createVulkanPlatform(char const* gpuHintCstr) {
    return new FilamentAppVulkanPlatform(gpuHintCstr);
}

} // namespace filament::filamentapp
