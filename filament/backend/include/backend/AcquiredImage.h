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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_ACQUIREDIMAGE_H
#define TNT_FILAMENT_BACKEND_PRIVATE_ACQUIREDIMAGE_H

#include <backend/DriverEnums.h>
#include <math/mat3.h>

namespace filament::backend {

class CallbackHandler;

/**
 * This lightweight POD allows us to bundle the state required to process an ACQUIRED stream.
 * Since these types of external images need to be moved around and queued up, an encapsulation is
 * very useful.
 */
/**
 * 获取的外部图像结构体
 * 
 * 这是一个轻量级的 POD（Plain Old Data）结构，用于封装处理 ACQUIRED 类型流所需的状态。
 * 由于这些外部图像需要在系统中移动和排队，封装非常有用。
 * 
 * 用途：
 * - 用于外部纹理流（如 Android 的 SurfaceTexture）
 * - 封装图像数据和相关的回调信息
 * - 支持异步图像获取和释放
 * 
 * 生命周期：
 * - 由后端创建和管理
 * - 在图像获取时填充
 * - 在图像使用完成后通过回调释放
 */
struct AcquiredImage {
    /**
     * External image handle (platform-specific)
     * 
     * 外部图像句柄（平台特定）
     * - Android: AHardwareBuffer* 或 ANativeWindowBuffer*
     * - iOS: CVPixelBufferRef
     * - 其他平台: 平台特定的图像句柄
     */
    void* image = nullptr;
    
    /**
     * Callback function to release the image
     * 
     * 释放图像的回调函数
     * - 当图像不再需要时调用
     * - 用于通知外部系统可以重用图像缓冲区
     * - 签名：void(*)(void* image, void* user)
     */
    backend::StreamCallback callback = nullptr;
    
    /**
     * User data passed to the callback
     * 
     * 传递给回调函数的用户数据
     * - 不透明指针，由调用者提供
     * - 在回调时原样传递
     */
    void* userData = nullptr;
    
    /**
     * Handler to dispatch the callback
     * 
     * 用于分发回调的处理器
     * - 如果为 nullptr，使用默认处理器
     * - 自定义处理器可以控制回调的执行线程
     */
    CallbackHandler* handler = nullptr;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_ACQUIREDIMAGE_H
