/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SAMPLE_CONFIG_H
#define TNT_FILAMENT_SAMPLE_CONFIG_H

#include <string>

#include <filament/Engine.h>

#include <camutils/Manipulator.h>

/**
 * Config - 应用程序配置结构体
 * 
 * 包含FilamentApp运行所需的所有配置参数，如窗口设置、
 * 渲染后端、相机模式、IBL路径等。
 */
struct Config {
    std::string title;                                    // 窗口标题
    std::string iblDirectory;                             // IBL资源目录路径
    std::string dirt;                                     // 污垢纹理文件路径
    float scale = 1.0f;                                   // 场景缩放比例
    bool splitView = false;                              // 是否启用分屏视图（用于调试）
    mutable filament::Engine::Backend backend = filament::Engine::Backend::DEFAULT; // 渲染后端类型
    mutable filament::backend::FeatureLevel featureLevel = filament::backend::FeatureLevel::FEATURE_LEVEL_3; // 功能级别
    filament::camutils::Mode cameraMode = filament::camutils::Mode::ORBIT; // 相机操作模式
    bool resizeable = true;                               // 窗口是否可调整大小
    bool headless = false;                                // 是否为无头模式（无窗口）
    int stereoscopicEyeCount = 2;                        // 立体渲染眼睛数量
    uint8_t samples = 1;                                  // 多重采样抗锯齿（MSAA）采样数

    // Indicate GPU preference for vulkan
    /** Vulkan后端GPU偏好提示（可以是GPU名称或索引） */
    std::string vulkanGPUHint;


    // Note that WebGPU has its own enums for backends, but to avoid leaking webgpu headers to
    // consumers of FilamentApp, we just overload the Engine::Backend enum.
    /** WebGPU后端类型别名 */
    using WebGPUBackend = filament::Engine::Backend;
    /** 强制使用的WebGPU后端 */
    WebGPUBackend forcedWebGPUBackend = WebGPUBackend::DEFAULT;
};

#endif // TNT_FILAMENT_SAMPLE_CONFIG_H
