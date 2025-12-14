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

#ifndef TNT_FILAMENT_SYNC_H
#define TNT_FILAMENT_SYNC_H

#include <filament/FilamentAPI.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Platform.h>

namespace filament {

/**
 * Sync 同步对象
 * 
 * 用于在渲染管道中同步操作的同步原语。可以获取平台特定的外部句柄。
 */
class UTILS_PUBLIC Sync : public FilamentAPI {
public:
    using CallbackHandler = backend::CallbackHandler;
    /**
     * 回调处理器类型
     */
    using Callback = backend::Platform::SyncCallback;
    /**
     * 同步回调函数类型
     */

    /**
     * Fetches a handle to the external, platform-specific representation of
     * this sync object.
     *
     * @param handler A handler for the callback that will receive the handle
     * @param callback A callback that will receive the handle when ready
     * @param userData Data to be passed to the callback so that the application
     *                 can identify what frame the sync is relevant to.
     * @return The external handle for the Sync. This is valid destroy() is
     *         called on this Sync object.
     */
    /**
     * 获取此同步对象的外部、平台特定表示的句柄
     *
     * @param handler 将接收句柄的回调处理器
     * @param callback 当句柄准备好时将接收句柄的回调函数
     * @param userData 传递给回调的数据，以便应用程序
     *                 可以识别同步相关的帧
     * @return Sync 的外部句柄。这在对此 Sync 对象调用 destroy() 之前有效
     */
    void getExternalHandle(CallbackHandler* handler, Callback callback, void* userData) noexcept;

protected:
    // prevent heap allocation
    ~Sync() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_SYNC_H
