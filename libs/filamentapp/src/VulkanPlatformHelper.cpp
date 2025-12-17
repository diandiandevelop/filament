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

#include <filamentapp/VulkanPlatformHelper.h>

#include <backend/platforms/VulkanPlatform.h>

#include <utils/CString.h>

#include <algorithm>
#include <cstdlib>

namespace filament::filamentapp {

using namespace filament::backend;

/**
 * 解析GPU偏好提示字符串实现
 * 
 * 执行步骤：
 * 1. 检查字符串是否为空，为空则返回默认配置
 * 2. 判断字符串是否为纯数字（GPU索引）
 * 3. 如果是数字，转换为索引
 * 4. 如果不是数字，作为GPU设备名称
 * 5. 返回自定义配置
 * 
 * @param gpuHintCstr GPU偏好提示字符串（可以是索引号或GPU名称）
 * @return Vulkan平台自定义配置
 */
VulkanPlatform::Customization parseGpuHint(char const* gpuHintCstr) {
    utils::CString gpuHint{ gpuHintCstr };
    if (gpuHint.empty()) {
        return {};
    }
    VulkanPlatform::Customization::GPUPreference pref;
    // Check to see if it is an integer, if so turn it into an index.
    // 检查是否为整数，如果是则转换为索引
    if (std::all_of(gpuHint.begin(), gpuHint.end(), ::isdigit)) {
        char* p_end{};
        pref.index = static_cast<int8_t>(std::strtol(gpuHint.c_str(), &p_end, 10));
    } else {
        pref.deviceName = gpuHint;  // 作为GPU设备名称
    }
    return { .gpu = pref };
}

/**
 * 销毁Vulkan平台对象实现
 * 
 * @param platform Vulkan平台对象指针
 */
void destroyVulkanPlatform(VulkanPlatform* platform) {
    delete platform;
}

} // namespace filament::filamentapp
