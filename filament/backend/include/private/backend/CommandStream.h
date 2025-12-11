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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H
#define TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H

#include "private/backend/CircularBuffer.h"
#include "private/backend/Dispatcher.h"
#include "private/backend/Driver.h"

#include <backend/BufferDescriptor.h>
#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>
#include <backend/Program.h>
#include <backend/PixelBufferDescriptor.h>
#include <backend/PresentCallable.h>
#include <backend/TargetBufferInfo.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ThreadUtils.h>

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#ifndef NDEBUG
#include <thread>
#endif

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// Set to true to print every command out on log.d. This requires RTTI and DEBUG
#define DEBUG_COMMAND_STREAM false

namespace filament::backend {

/**
 * 命令基类
 * 
 * 所有命令都继承自此基类，提供统一的执行接口。
 * 命令在命令流中连续存储，通过偏移量链接。
 */
class CommandBase {
    static constexpr size_t FILAMENT_OBJECT_ALIGNMENT = alignof(std::max_align_t);  // 命令对齐大小

protected:
    using Execute = Dispatcher::Execute;  // 执行函数类型

    /**
     * 构造函数
     * @param execute 执行函数指针
     */
    constexpr explicit CommandBase(Execute const execute) noexcept : mExecute(execute) {}

public:
    /**
     * 对齐大小
     * 
     * 将所有命令对齐到 FILAMENT_OBJECT_ALIGNMENT 边界。
     * 确保在不同平台上都能正确访问命令数据。
     * 
     * @param v 要对齐的大小
     * @return 对齐后的大小
     */
    static constexpr size_t align(size_t v) {
        return (v + (FILAMENT_OBJECT_ALIGNMENT - 1)) & -FILAMENT_OBJECT_ALIGNMENT;
    }

    /**
     * 执行命令并返回下一个命令
     * 
     * 执行当前命令，然后返回下一个命令的指针。
     * 
     * 实现说明：
     * - 通过输出参数返回下一个命令的偏移量，允许编译器进行尾调用优化
     * - 但这会在每次迭代时读写栈，有一定开销
     * - 最终选择在单一位置支付这个开销
     * 
     * @param driver Driver 实例
     * @return 下一个命令的指针
     */
    CommandBase* execute(Driver& driver) {
        intptr_t next;  // 下一个命令的偏移量
        mExecute(driver, this, &next);  // 调用执行函数
        // 计算下一个命令的地址（当前地址 + 偏移量）
        return reinterpret_cast<CommandBase*>(reinterpret_cast<intptr_t>(this) + next);
    }

    ~CommandBase() noexcept = default;

private:
    Execute mExecute;  // 执行函数指针
};

// ------------------------------------------------------------------------------------------------

template<typename T, typename Type, typename D, typename ... ARGS>
constexpr decltype(auto) invoke(Type T::* m, D&& d, ARGS&& ... args) {
    static_assert(std::is_base_of<T, std::decay_t<D>>::value,
            "member function and object not related");
    return (std::forward<D>(d).*m)(std::forward<ARGS>(args)...);
}

template<typename M, typename D, typename T, std::size_t... I>
constexpr decltype(auto) trampoline(M&& m, D&& d, T&& t, std::index_sequence<I...>) {
    return invoke(std::forward<M>(m), std::forward<D>(d), std::get<I>(std::forward<T>(t))...);
}

template<typename M, typename D, typename T>
constexpr decltype(auto) apply(M&& m, D&& d, T&& t) {
    return trampoline(std::forward<M>(m), std::forward<D>(d), std::forward<T>(t),
            std::make_index_sequence< std::tuple_size<std::remove_reference_t<T>>::value >{});
}

/**
 * CommandType 模板类
 * 
 * 这是一个包装类，用于特化 Driver 的成员函数指针类型。
 * 我们只需要识别方法的参数类型，不会通过这个指针调用方法。
 * 
 * @tparam ARGS 方法参数类型列表
 */
template<typename... ARGS>
struct CommandType;

/**
 * CommandType 特化：针对 Driver 的成员函数指针
 * 
 * @tparam ARGS 方法参数类型列表
 */
template<typename... ARGS>
struct CommandType<void (Driver::*)(ARGS...)> {

    /**
     * Command 类
     * 
     * 针对 Driver 的特定方法进行模板特化。
     * 注意：我们从不直接调用这个方法（这就是为什么它不出现在模板参数中）。
     * 实际的调用通过 Command::execute() 进行。
     * 
     * @tparam METHOD Driver 方法的成员函数指针
     */
    template<void(Driver::*)(ARGS...)>
    class Command : public CommandBase {
        // 使用 std::tuple<> 保存传递给构造函数的参数
        using SavedParameters = std::tuple<std::remove_reference_t<ARGS>...>;
        SavedParameters mArgs;  // 保存的参数

        void log() noexcept;  // 日志输出（调试用）
        template<std::size_t... I> void log(std::index_sequence<I...>) noexcept;  // 日志输出辅助函数

    public:
        /**
         * 执行命令（静态方法）
         * 
         * 从命令流中执行命令，调用对应的 Driver 方法。
         * 
         * @tparam M 方法类型
         * @tparam D Driver 类型
         * @param method 方法指针
         * @param driver Driver 实例
         * @param base 命令基类指针
         * @param next 输出参数：下一个命令的偏移量
         */
        template<typename M, typename D>
        static void execute(M&& method, D&& driver, CommandBase* base, intptr_t* next) {
            Command* self = static_cast<Command*>(base);
            *next = align(sizeof(Command));  // 计算下一个命令的偏移量
#if DEBUG_COMMAND_STREAM
                // 必须在调用方法前调用日志
                self->log();
#endif
            // 使用 apply 将保存的参数展开并调用 Driver 方法
            apply(std::forward<M>(method), std::forward<D>(driver), std::move(self->mArgs));
            self->~Command();  // 析构命令
        }

        // 命令可以移动
        Command(Command&& rhs) noexcept = default;

        /**
         * 构造函数
         * 
         * @tparam A 参数类型列表
         * @param execute 执行函数指针
         * @param args 方法参数（会被保存到 mArgs 中）
         */
        template<typename... A>
        explicit constexpr Command(Execute const execute, A&& ... args) noexcept
                : CommandBase(execute), mArgs(std::forward<A>(args)...) {
        }

        /**
         * Placement new 操作符
         * 
         * 声明为 "throw" 以避免编译器的空指针检查。
         * 
         * @param ptr 内存地址（必须非空）
         * @return 内存地址
         */
        void* operator new(std::size_t, void* ptr) noexcept {
            assert_invariant(ptr);
            return ptr;
        }
    };
};

/**
 * 宏：将 Driver 的方法转换为 Command<> 类型
 * 
 * 用法：COMMAND_TYPE(beginRenderPass) 会展开为对应的 Command 类型
 */
#define COMMAND_TYPE(method) CommandType<decltype(&Driver::method)>::Command<&Driver::method>

// ------------------------------------------------------------------------------------------------

/**
 * 自定义命令
 * 
 * 执行任意函数对象的命令。
 * 用于执行不在 Driver API 中的自定义操作。
 */
class CustomCommand : public CommandBase {
    std::function<void()> mCommand;  // 要执行的函数对象
    /**
     * 执行函数（静态方法）
     * 
     * @param driver Driver 实例（未使用）
     * @param base 命令基类指针
     * @param next 输出参数：下一个命令的偏移量
     */
    static void execute(Driver&, CommandBase* base, intptr_t* next);
public:
    CustomCommand(CustomCommand&& rhs) = default;

    /**
     * 构造函数
     * 
     * @param cmd 要执行的函数对象（会被移动）
     */
    explicit CustomCommand(std::function<void()> cmd)
            : CommandBase(execute), mCommand(std::move(cmd)) { }
};

// ------------------------------------------------------------------------------------------------

/**
 * 空操作命令
 * 
 * 用于命令流中的对齐或占位。
 * 不执行任何操作，只是跳转到下一个命令。
 */
class NoopCommand : public CommandBase {
    intptr_t mNext;  // 下一个命令的偏移量
    /**
     * 执行函数（静态方法）
     * 
     * @param driver Driver 实例（未使用）
     * @param self 命令自身
     * @param next 输出参数：下一个命令的偏移量
     */
    static void execute(Driver&, CommandBase* self, intptr_t* next) noexcept {
        *next = static_cast<NoopCommand*>(self)->mNext;
    }
public:
    /**
     * 构造函数
     * 
     * @param next 下一个命令的指针
     */
    constexpr explicit NoopCommand(void* next) noexcept
            : CommandBase(execute), mNext(intptr_t((char *)next - (char *)this)) { }
};

// ------------------------------------------------------------------------------------------------

#if !defined(NDEBUG) || (FILAMENT_DEBUG_COMMANDS >= FILAMENT_DEBUG_COMMANDS_ENABLE)
    // For now, simply pass the method name down as a string and throw away the parameters.
    // This is good enough for certain debugging needs and we can improve this later.
    #define DEBUG_COMMAND_BEGIN(methodName, sync, ...) mDriver.debugCommandBegin(this, sync, #methodName)
    #define DEBUG_COMMAND_END(methodName, sync) mDriver.debugCommandEnd(this, sync, #methodName)
#else
    #define DEBUG_COMMAND_BEGIN(methodName, sync, ...)
    // For the line "AutoExecute callOnExit([=, this](){",
    // "this" will be unused for release build since DEBUG_COMMAND_END is a no-op. So we add the
    // following workaround.
    #define DEBUG_COMMAND_END(methodName, sync) ((void)(this));
#endif

/**
 * 命令流类
 * 
 * CommandStream 负责命令的序列化和执行。
 * 
 * 工作流程：
 * 1. 主线程调用 DriverApi 方法，CommandStream 将参数序列化到 CircularBuffer
 * 2. flush() 或 submitFrame() 时，命令缓冲区被提交到渲染线程
 * 3. 渲染线程调用 execute() 执行所有命令
 * 
 * 命令类型：
 * - Driver 方法命令：对应 Driver 的每个方法
 * - 自定义命令：执行任意函数
 * - 空操作命令：用于对齐
 */
class CommandStream {
    /**
     * 自动执行辅助类
     * 
     * 在作用域结束时自动执行闭包。
     * 用于同步命令的调试标记。
     * 
     * @tparam T 闭包类型
     */
    template<typename T>
    struct AutoExecute {
        T closure;  // 要执行的闭包
        explicit AutoExecute(T&& closure) : closure(std::forward<T>(closure)) {}
        ~AutoExecute() { closure(); }  // 析构时执行
    };

public:
    /**
     * 构造函数
     * 
     * @param driver Driver 实例
     * @param buffer 循环缓冲区引用
     */
    CommandStream(Driver& driver, CircularBuffer& buffer) noexcept;

    CommandStream(CommandStream const& rhs) noexcept = delete;
    CommandStream& operator=(CommandStream const& rhs) noexcept = delete;

    /**
     * 获取循环缓冲区
     * 
     * @return 循环缓冲区的常量引用
     */
    CircularBuffer const& getCircularBuffer() const noexcept { return mCurrentBuffer; }

public:
#define DECL_DRIVER_API(methodName, paramsDecl, params)                                         \
    inline void methodName(paramsDecl) noexcept {                                               \
        DEBUG_COMMAND_BEGIN(methodName, false, params);                                         \
        using Cmd = COMMAND_TYPE(methodName);                                                   \
        void* const p = allocateCommand(CommandBase::align(sizeof(Cmd)));                       \
        new(p) Cmd(mDispatcher.methodName##_, APPLY(std::move, params));                        \
        DEBUG_COMMAND_END(methodName, false);                                                   \
    }

#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)                    \
    inline RetType methodName(paramsDecl) noexcept {                                            \
        DEBUG_COMMAND_BEGIN(methodName, true, params);                                          \
        AutoExecute callOnExit([=, this](){                                                     \
            DEBUG_COMMAND_END(methodName, true);                                                \
        });                                                                                     \
        return apply(&Driver::methodName, mDriver, std::forward_as_tuple(params));              \
    }

#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)                         \
    inline RetType methodName(paramsDecl) noexcept {                                            \
        DEBUG_COMMAND_BEGIN(methodName, false, params);                                         \
        RetType result = mDriver.methodName##S();                                               \
        using Cmd = COMMAND_TYPE(methodName##R);                                                \
        void* const p = allocateCommand(CommandBase::align(sizeof(Cmd)));                       \
        new(p) Cmd(mDispatcher.methodName##_, RetType(result), APPLY(std::move, params));       \
        DEBUG_COMMAND_END(methodName, false);                                                   \
        return result;                                                                          \
    }

#include "DriverAPI.inc"

public:
    /**
     * 调试：线程检查
     * 
     * 仅用于调试。当前 CircularBuffer 只能从单个线程写入。
     * 在调试构建中会断言这个条件。
     * 在渲染循环开始时调用此方法。
     */
    void debugThreading() noexcept {
#ifndef NDEBUG
        mThreadId = utils::ThreadUtils::getThreadId();
#endif
    }

    /**
     * 执行命令流
     * 
     * 在渲染线程调用，执行缓冲区中的所有命令。
     * 
     * @param buffer 命令缓冲区起始地址
     */
    void execute(void* buffer);

    /**
     * 队列自定义命令
     * 
     * 允许将 lambda 函数作为命令队列。
     * 这比使用 Driver API 效率低得多，应谨慎使用。
     * 
     * @param command 要执行的函数对象
     */
    void queueCommand(std::function<void()> command);

    /**
     * 分配内存
     * 
     * 分配与当前 CommandStreamBuffer 关联的内存。
     * 此内存在命令缓冲区处理完后自动释放。
     * 
     * 重要：析构函数不会被调用！
     * 
     * @param size 要分配的大小（字节）
     * @param alignment 对齐要求（默认 8 字节）
     * @return 分配的内存指针
     */
    inline void* allocate(size_t size, size_t alignment = 8) noexcept;

    /**
     * 分配 POD 类型数组
     * 
     * 辅助函数，用于分配平凡可析构对象的数组。
     * 
     * @tparam PodType POD 类型（必须是平凡可析构的）
     * @param count 元素数量（默认 1）
     * @param alignment 对齐要求（默认使用类型的自然对齐）
     * @return 分配的数组指针
     */
    template<typename PodType,
            typename = typename std::enable_if<std::is_trivially_destructible<PodType>::value>::type>
    PodType* allocatePod(
            size_t count = 1, size_t alignment = alignof(PodType)) noexcept;

private:
    /**
     * 分配命令内存（私有方法）
     * 
     * 在命令流中分配内存，用于存储命令对象。
     * 
     * @param size 要分配的大小（字节）
     * @return 分配的内存指针
     */
    void* allocateCommand(size_t const size) noexcept {
        assert_invariant(utils::ThreadUtils::isThisThread(mThreadId));  // 确保在正确的线程
        return mCurrentBuffer.allocate(size);
    }

    // 我们使用 Dispatcher 的副本（而不是指针），因为这样可以减少执行驱动命令时的一次解引用
    Driver& UTILS_RESTRICT mDriver;  // Driver 实例引用
    CircularBuffer& UTILS_RESTRICT mCurrentBuffer;  // 当前循环缓冲区引用
    Dispatcher mDispatcher;  // 命令分发器（存储方法指针映射）

#ifndef NDEBUG
    // 仅用于调试：记录线程 ID
    std::thread::id mThreadId{};
#endif

    bool mUsePerformanceCounter = false;  // 是否使用性能计数器（Android 调试用）
};

/**
 * 分配内存（内联实现）
 * 
 * 在命令流中分配内存，用于存储命令相关的数据。
 * 
 * 实现细节：
 * - 使用 NoopCommand 作为占位符，确保命令流的连续性
 * - 内存会在命令缓冲区处理完后自动释放
 * - 重要：析构函数不会被调用，只能用于 POD 类型
 * 
 * @param size 要分配的大小（字节）
 * @param alignment 对齐要求（必须是 2 的幂）
 * @return 分配的内存指针（已对齐）
 */
void* CommandStream::allocate(size_t size, size_t alignment) noexcept {
    // 确保对齐是 2 的幂
    assert_invariant(alignment && !(alignment & alignment-1));

    // 填充请求的大小以容纳 NoopCommand 和对齐
    const size_t s = CustomCommand::align(sizeof(NoopCommand) + size + alignment - 1);

    // 在命令流中分配空间并插入 NoopCommand
    char* const p = (char *)allocateCommand(s);
    new(p) NoopCommand(p + s);  // 在分配的内存开头构造 NoopCommand

    // 计算"用户"数据指针（跳过 NoopCommand 并对齐）
    void* data = (void *)((uintptr_t(p) + sizeof(NoopCommand) + alignment - 1) & ~(alignment - 1));
    assert_invariant(data >= p + sizeof(NoopCommand));
    return data;
}

/**
 * 分配 POD 类型数组（模板实现）
 * 
 * @tparam PodType POD 类型（必须是平凡可析构的）
 * @param count 元素数量
 * @param alignment 对齐要求
 * @return 分配的数组指针
 */
template<typename PodType, typename>
PodType* CommandStream::allocatePod(size_t count, size_t alignment) noexcept {
    return static_cast<PodType*>(allocate(count * sizeof(PodType), alignment));
}

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H
