/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DRIVER_COMMANDSTREAM_DISPATCHER_H
#define TNT_FILAMENT_DRIVER_COMMANDSTREAM_DISPATCHER_H

#include "private/backend/Driver.h"
#include "private/backend/CommandStream.h"

#include <private/utils/Tracing.h>

#include <utils/compiler.h>

#include <utility>

#include <stddef.h>
#include <stdint.h>

#define DEBUG_LEVEL_NONE       0
#define DEBUG_LEVEL_SYSTRACE   1

// set to the desired debug level
#define DEBUG_LEVEL             DEBUG_LEVEL_NONE


#if DEBUG_LEVEL == DEBUG_LEVEL_NONE
#   define SYSTRACE()
#elif DEBUG_LEVEL == DEBUG_LEVEL_SYSTRACE
#   define SYSTRACE() FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
#else
#   error "invalid debug level"
#endif

namespace filament::backend {

/**
 * ConcreteDispatcher（具体分发器模板）
 * 
 * 为每个具体的 Driver 实现（OpenGLDriver、VulkanDriver 等）生成 Dispatcher。
 * 使用模板特化，在编译时为每个 Driver API 方法生成对应的执行函数。
 * 
 * 工作流程：
 * 1. make() 方法创建 Dispatcher 对象
 * 2. 为每个 Driver API 方法生成静态执行函数
 * 3. 将执行函数的指针赋值给 Dispatcher 的对应成员
 * 4. 返回填充好的 Dispatcher
 * 
 * @tparam ConcreteDriver 具体的 Driver 类型（如 OpenGLDriver、VulkanDriver）
 */
template<typename ConcreteDriver>
class ConcreteDispatcher {
public:
    /**
     * 创建 Dispatcher 对象
     * 
     * 为 ConcreteDriver 生成 Dispatcher，填充所有方法的函数指针。
     * 
     * @return 填充好的 Dispatcher 对象
     */
    static Dispatcher make() noexcept;

private:
    /**
     * 为每个 Driver API 方法生成静态执行函数
     * 
     * 宏展开示例：
     * - DECL_DRIVER_API(beginRenderPass, ...) 
     *   -> static void beginRenderPass(Driver& driver, CommandBase* base, intptr_t* next)
     * 
     * 执行函数的工作：
     * 1. 从命令对象中提取参数
     * 2. 将 Driver 转换为 ConcreteDriver
     * 3. 调用对应的 ConcreteDriver 方法
     */
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)  // 同步方法不生成执行函数
#define DECL_DRIVER_API(methodName, paramsDecl, params)                                         \
    static void methodName(Driver& driver, CommandBase* base, intptr_t* next) {                 \
        SYSTRACE()  /* 性能追踪标记（如果启用）*/                                                \
        using Cmd = COMMAND_TYPE(methodName);  /* 获取命令类型 */                                \
        ConcreteDriver& concreteDriver = static_cast<ConcreteDriver&>(driver);  /* 转换为具体类型 */ \
        Cmd::execute(&ConcreteDriver::methodName, concreteDriver, base, next);  /* 执行命令 */  \
     }
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)                         \
    static void methodName(Driver& driver, CommandBase* base, intptr_t* next) {                 \
        SYSTRACE()                                                                              \
        using Cmd = COMMAND_TYPE(methodName##R);  /* 返回方法使用 methodName##R */             \
        ConcreteDriver& concreteDriver = static_cast<ConcreteDriver&>(driver);                  \
        Cmd::execute(&ConcreteDriver::methodName##R, concreteDriver, base, next);                 \
     }
#include "private/backend/DriverAPI.inc"  // 包含所有 Driver API 定义
};

/**
 * 创建 Dispatcher 对象（模板实现）
 * 
 * 为 ConcreteDriver 生成 Dispatcher，填充所有方法的函数指针。
 * 
 * 实现说明：
 * - 创建空的 Dispatcher 对象
 * - 为每个 Driver API 方法，将对应的执行函数指针赋值给 Dispatcher 的成员
 * - 返回填充好的 Dispatcher
 * 
 * @tparam ConcreteDriver 具体的 Driver 类型
 * @return 填充好的 Dispatcher 对象
 */
template<typename ConcreteDriver>
UTILS_NOINLINE
Dispatcher ConcreteDispatcher<ConcreteDriver>::make() noexcept {
    Dispatcher dispatcher;  // 创建空的 Dispatcher

    /**
     * 填充 Dispatcher 的函数指针
     * 
     * 宏展开示例：
     * - DECL_DRIVER_API(beginRenderPass, ...)
     *   -> dispatcher.beginRenderPass_ = &ConcreteDispatcher::beginRenderPass;
     */
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)  // 同步方法不填充
#define DECL_DRIVER_API(methodName, paramsDecl, params)                 \
                dispatcher.methodName##_ = &ConcreteDispatcher::methodName;  /* 赋值函数指针 */
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
                dispatcher.methodName##_ = &ConcreteDispatcher::methodName;  /* 返回方法同样赋值 */

#include "private/backend/DriverAPI.inc"  // 包含所有 Driver API 定义

    return dispatcher;  // 返回填充好的 Dispatcher
}

} // namespace filament::backend

#endif // TNT_FILAMENT_DRIVER_COMMANDSTREAM_DISPATCHER_H
