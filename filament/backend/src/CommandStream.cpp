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

#include "private/backend/CommandStream.h"

#include <private/utils/Tracing.h>

#if DEBUG_COMMAND_STREAM
#include <utils/CallStack.h>
#endif

#include <private/utils/Tracing.h>

#include <utils/Logger.h>
#include <utils/Profiler.h>
#include <utils/compiler.h>
#include <utils/ostream.h>
#include <utils/sstream.h>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif

using namespace utils;

namespace filament::backend {

// ------------------------------------------------------------------------------------------------
/**
 * 调试辅助函数
 * 
 * 用于在 DEBUG_COMMAND_STREAM 模式下打印命令参数。
 */

/**
 * 打印参数包（递归终止）
 * 
 * 当没有参数时调用此函数。
 */
inline void printParameterPack(io::ostream&) { }

/**
 * 打印参数包（最后一个参数）
 * 
 * @tparam LAST 最后一个参数类型
 * @param out 输出流
 * @param t 最后一个参数
 */
template<typename LAST>
static void printParameterPack(io::ostream& out, const LAST& t) { out << t; }

/**
 * 打印参数包（递归版本）
 * 
 * 递归打印所有参数，用逗号分隔。
 * 
 * @tparam FIRST 第一个参数类型
 * @tparam REMAINING 剩余参数类型
 * @param out 输出流
 * @param first 第一个参数
 * @param rest 剩余参数
 */
template<typename FIRST, typename... REMAINING>
static void printParameterPack(io::ostream& out, const FIRST& first, const REMAINING& ... rest) {
    out << first << ", ";
    printParameterPack(out, rest...);
}

/**
 * 从命令类型名称中提取方法名称
 * 
 * 从类似 "::Command<&filament::backend::Driver::methodName>" 的字符串中
 * 提取 "methodName" 部分。
 * 
 * @param command 命令类型名称
 * @return 方法名称的字符串视图
 */
static UTILS_NOINLINE UTILS_UNUSED std::string_view extractMethodName(std::string_view command) noexcept { // NOLINT(*-exception-escape)
    constexpr char startPattern[] = "::Command<&filament::backend::Driver::";
    auto pos = command.rfind(startPattern);
    auto end = command.rfind('(');
    pos += sizeof(startPattern) - 1;
    if (pos > command.size()) {
        return { command.data(), command.size() };
    }
    return command.substr(pos, end-pos); // 根据构造，这不会抛出异常
}

// ------------------------------------------------------------------------------------------------

/**
 * CommandStream 构造函数
 * 
 * 初始化命令流，设置 Driver 和缓冲区，获取 Dispatcher。
 * 
 * @param driver Driver 实例引用
 * @param buffer 循环缓冲区引用
 */
CommandStream::CommandStream(Driver& driver, CircularBuffer& buffer) noexcept
        : mDriver(driver),
          mCurrentBuffer(buffer),
          mDispatcher(driver.getDispatcher())  // 获取命令分发器
#ifndef NDEBUG
          , mThreadId(ThreadUtils::getThreadId())  // 记录当前线程 ID（调试用）
#endif
{
#ifdef __ANDROID__
    // Android 特定：检查是否启用性能计数器
    char property[PROP_VALUE_MAX];
    __system_property_get("debug.filament.perfcounters", property);
    mUsePerformanceCounter = bool(atoi(property));
#endif
}

/**
 * 执行命令流
 * 
 * 在渲染线程调用，执行缓冲区中的所有命令。
 * 
 * 执行流程：
 * 1. 启动性能分析器（如果启用）
 * 2. 通过 Driver::execute() 包装执行（允许后端添加调试标记等）
 * 3. 循环执行所有命令，直到遇到结束标记
 * 4. 停止性能分析器并记录指标
 * 
 * @param buffer 命令缓冲区起始地址
 * 
 * 注意：不能使用 FILAMENT_TRACING_CALL()，因为 execute() 内部也使用 systrace，
 * 而 END 不保证在这个作用域内发生。
 */
void CommandStream::execute(void* buffer) {
    Profiler profiler;  // 性能分析器

    // 如果启用性能计数器，启动分析
    if constexpr (FILAMENT_TRACING_ENABLED) {
        if (UTILS_UNLIKELY(mUsePerformanceCounter)) {
            // 当追踪完全禁用时，我们希望移除所有这些代码
            profiler.resetEvents(Profiler::EV_CPU_CYCLES | Profiler::EV_BPU_MISSES);
            profiler.start();
        }
    }

    Driver& UTILS_RESTRICT driver = mDriver;
    CommandBase* UTILS_RESTRICT base = static_cast<CommandBase*>(buffer);
    // 通过 Driver::execute() 执行，允许后端包装执行上下文
    mDriver.execute([&driver, base] {
        auto& d = driver;
        auto p = base;
        // 循环执行所有命令，直到遇到结束标记（p == nullptr）
        while (UTILS_LIKELY(p)) {
            p = p->execute(d);  // 执行命令并获取下一个命令
        }
    });

    // 如果启用性能计数器，停止分析并记录指标
    if constexpr (FILAMENT_TRACING_ENABLED) {
        if (UTILS_UNLIKELY(mUsePerformanceCounter)) {
            profiler.stop();
            UTILS_UNUSED Profiler::Counters const counters = profiler.readCounters();
            FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
            FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "GLThread (I)", counters.getInstructions());
            FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "GLThread (C)", counters.getCpuCycles());
            FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "GLThread (CPI x10)", counters.getCPI() * 10);
            FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "GLThread (BPU miss)", counters.getBranchMisses());
            FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "GLThread (I / BPU miss)",
                    counters.getInstructions() / counters.getBranchMisses());
        }
    }
}

/**
 * 队列自定义命令
 * 
 * 将 lambda 函数作为命令添加到命令流中。
 * 这比使用 Driver API 效率低得多，应谨慎使用。
 * 
 * @param command 要执行的函数对象（会被移动）
 */
void CommandStream::queueCommand(std::function<void()> command) {
    // 分配 CustomCommand 的内存并构造
    new(allocateCommand(CustomCommand::align(sizeof(CustomCommand)))) CustomCommand(std::move(command));
}

/**
 * 记录命令（带索引序列）
 * 
 * 在 DEBUG_COMMAND_STREAM 模式下，记录命令的方法名称、大小和参数。
 * 
 * @tparam ARGS 命令参数类型
 * @tparam METHOD Driver 方法指针
 * @tparam I 索引序列
 * 
 * 注意：需要 RTTI 支持才能使用 DEBUG_COMMAND_STREAM。
 */
template<typename... ARGS>
template<void (Driver::*METHOD)(ARGS...)>
template<std::size_t... I>
void CommandType<void (Driver::*)(ARGS...)>::Command<METHOD>::log(std::index_sequence<I...>) noexcept  {
#if DEBUG_COMMAND_STREAM
    static_assert(UTILS_HAS_RTTI, "DEBUG_COMMAND_STREAM can only be used with RTTI");
    /**
     * 获取命令类型名称并提取方法名称
     */
    CString command = CallStack::demangleTypeName(typeid(Command).name());
    DLOG(INFO) << extractMethodName({command.data(), command.size()}) << " : size=" << sizeof(Command);
    
    /**
     * 打印所有参数
     */
    io::sstream parameterPack;
    printParameterPack(parameterPack, std::get<I>(mArgs)...);
    DLOG(INFO) << "\t" << parameterPack.c_str();
#endif
}

/**
 * 记录命令（无参数版本）
 * 
 * 创建索引序列并调用带索引序列的 log() 方法。
 */
template<typename... ARGS>
template<void (Driver::*METHOD)(ARGS...)>
void CommandType<void (Driver::*)(ARGS...)>::Command<METHOD>::log() noexcept  {
    log(std::make_index_sequence<std::tuple_size<SavedParameters>::value>{});
}

/**
 * 显式实例化 log() 方法
 * 
 * 当 DEBUG_COMMAND_STREAM 激活时，需要显式实例化 log() 方法
 * （因为我们不想在头文件中包含它）。
 * 
 * 这通过包含 DriverAPI.inc 并展开宏来为所有 Driver API 方法生成 log() 实例化。
 */
#if DEBUG_COMMAND_STREAM
#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)
#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    template void CommandType<decltype(&Driver::methodName)>::Command<&Driver::methodName>::log() noexcept;
#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    template void CommandType<decltype(&Driver::methodName##R)>::Command<&Driver::methodName##R>::log() noexcept;
#include "private/backend/DriverAPI.inc"
#endif

// ------------------------------------------------------------------------------------------------

/**
 * 执行自定义命令
 * 
 * 执行通过 queueCommand() 添加的 lambda 函数。
 * 
 * @param driver Driver 引用（未使用）
 * @param base 命令基类指针
 * @param next 输出参数，下一个命令的偏移量
 */
void CustomCommand::execute(Driver&, CommandBase* base, intptr_t* next) {
    *next = align(sizeof(CustomCommand));  // 计算下一个命令的位置
    static_cast<CustomCommand*>(base)->mCommand();  // 执行 lambda
    static_cast<CustomCommand*>(base)->~CustomCommand();  // 析构 CustomCommand
}

} // namespace filament::backend
