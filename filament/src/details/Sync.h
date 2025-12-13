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

#ifndef TNT_FILAMENT_DETAILS_SYNC_H
#define TNT_FILAMENT_DETAILS_SYNC_H

#include "downcast.h"

#include <filament/Sync.h>

#include <backend/CallbackHandler.h>
#include <backend/Handle.h>
#include <backend/Platform.h>

namespace filament {

class FEngine;

/**
 * 同步对象实现类
 * 
 * 管理 GPU-CPU 同步对象，用于跨平台同步。
 * 同步对象可以用于等待 GPU 操作完成，或导出到外部系统。
 */
class FSync : public Sync {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    FSync(FEngine& engine);

    /**
     * 终止同步对象
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 获取硬件句柄
     * 
     * @return 同步对象硬件句柄
     */
    backend::SyncHandle getHwHandle() const noexcept { return mHwSync; }

    /**
     * 获取外部句柄
     * 
     * 获取此同步对象的外部、平台特定表示的句柄。
     * 这用于将同步对象导出到外部系统（如 Vulkan 信号量）。
     *
     * @param handler 回调处理器指针，将接收句柄
     * @param callback 回调函数，当句柄准备好时将被调用
     * @param userData 传递给回调的用户数据，以便应用程序
     *                 可以识别同步对象相关的帧
     * 
     * 注意：外部句柄在此 Sync 对象上调用 destroy() 之前有效。
     */
    void getExternalHandle(Sync::CallbackHandler* handler, Sync::Callback callback,
            void* userData) noexcept;

private:
    FEngine& mEngine;  // 引擎引用
    backend::SyncHandle mHwSync;  // 硬件同步句柄
};

FILAMENT_DOWNCAST(Sync)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_SYNC_H
