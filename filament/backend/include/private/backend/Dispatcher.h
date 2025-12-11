/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_DISPATCHER_H
#define TNT_FILAMENT_BACKEND_PRIVATE_DISPATCHER_H

#include <stdint.h>

namespace filament::backend {

class Driver;
class CommandBase;

/**
 * Dispatcher（命令分发器）
 * 
 * Dispatcher 是一个只包含函数指针的数据结构。
 * 每个函数指针指向一段代码，该代码负责：
 * 1. 从命令对象中解包参数
 * 2. 调用对应的 Driver 方法
 * 
 * 设计说明：
 * - Dispatcher 的函数指针在初始化时填充（通过 Driver::getDispatcher()）
 * - 在 Dispatcher 填充之前不能进行任何 CommandStream 调用
 * - 当命令插入到流中时，对应的函数指针直接从 Dispatcher 复制到 CommandBase
 * 
 * 性能考虑：
 * - 使用函数指针而不是虚函数，避免虚函数调用开销
 * - 函数指针在编译时确定，编译器可以更好地优化
 * - 命令执行时直接调用函数指针，无需查找
 */
class Dispatcher {
public:
    /**
     * 执行函数类型
     * 
     * @param driver Driver 实例
     * @param self 命令对象指针
     * @param next 输出参数：下一个命令的偏移量
     */
    using Execute = void (*)(Driver& driver, CommandBase* self, intptr_t* next);
    
    /**
     * 为每个 Driver API 方法生成函数指针成员
     * 
     * 宏展开示例：
     * - DECL_DRIVER_API(beginRenderPass, ...) -> Execute beginRenderPass_;
     * - DECL_DRIVER_API_RETURN(Handle<HwTexture>, createTexture, ...) -> Execute createTexture_;
     */
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)  // 同步方法不生成函数指针
#define DECL_DRIVER_API(methodName, paramsDecl, params)                     Execute methodName##_;  // 异步方法
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)     Execute methodName##_;  // 返回方法

#include "DriverAPI.inc"  // 包含所有 Driver API 定义
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_PRIVATE_DISPATCHER_H
