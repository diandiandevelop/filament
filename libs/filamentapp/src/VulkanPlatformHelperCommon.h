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

#ifndef TNT_FILAMENTAPP_VULKAN_PLATFORM_HELPER_COMMON_H
#define TNT_FILAMENTAPP_VULKAN_PLATFORM_HELPER_COMMON_H

#include <backend/platforms/VulkanPlatform.h>

namespace filament::filamentapp {

/**
 * 解析GPU偏好提示字符串
 * 
 * 将GPU偏好提示字符串解析为Vulkan平台自定义配置。
 * 支持两种格式：
 * - 纯数字：作为GPU索引
 * - 字符串：作为GPU设备名称
 * 
 * @param gpuHintCstr GPU偏好提示字符串（可以是索引号或GPU名称）
 * @return Vulkan平台自定义配置
 */
filament::backend::VulkanPlatform::Customization parseGpuHint(char const* gpuHintCstr);

} // namespace filament::filamentapp

#endif // TNT_FILAMENTAPP_VULKAN_PLATFORM_HELPER_COMMON_H
