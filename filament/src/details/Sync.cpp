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

#include "details/Sync.h"

#include "details/Engine.h"

#include <backend/Platform.h>
#include <filament/Sync.h>

namespace filament {

using DriverApi = backend::DriverApi;  // 驱动 API 类型别名

/**
 * 同步对象构造函数
 * 
 * 创建同步对象并分配驱动资源。
 * 
 * @param engine 引擎引用
 */
FSync::FSync(FEngine& engine)
    : mEngine(engine) {  // 初始化引擎引用
    DriverApi& driverApi = engine.getDriverApi();  // 获取驱动 API
    mHwSync = driverApi.createSync();  // 创建硬件同步对象
}

/**
 * 终止同步对象
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FSync::terminate(FEngine& engine) noexcept {
    engine.getDriverApi().destroySync(mHwSync);  // 销毁硬件同步对象
}

/**
 * 获取外部句柄
 * 
 * 获取平台特定的同步对象句柄，用于跨进程或跨 API 同步。
 * 
 * @param handler 回调处理器指针
 * @param callback 回调函数
 * @param userData 用户数据指针
 */
void FSync::getExternalHandle(Sync::CallbackHandler* handler, Sync::Callback callback,
        void* userData) noexcept {
    mEngine.getDriverApi().getPlatformSync(mHwSync, handler, callback, userData);  // 获取平台同步对象
}

} // namespace filament
