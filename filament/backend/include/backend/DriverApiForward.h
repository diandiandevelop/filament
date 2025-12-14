/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_DRIVERAPIFORWARD_H
#define TNT_FILAMENT_BACKEND_PRIVATE_DRIVERAPIFORWARD_H

namespace filament::backend {

class CommandStream;

/**
 * DriverApi 类型别名
 * 
 * DriverApi 是 CommandStream 的类型别名，用于表示驱动程序的 API 接口。
 * 
 * 设计目的：
 * - 提供统一的 API 接口名称
 * - 简化代码中的类型引用
 * - 隐藏实现细节（CommandStream 是内部实现）
 * 
 * 使用场景：
 * - 在命令流中记录渲染命令
 * - 作为 Driver 的公共接口
 * - 用于类型安全的命令传递
 * 
 * 注意：
 * - DriverApi 和 CommandStream 是同一个类型
 * - 使用 DriverApi 作为公共接口名称
 * - CommandStream 是内部实现类
 */
using DriverApi = CommandStream;

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_DRIVERAPIFORWARD_H
