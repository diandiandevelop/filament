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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_DRIVER_H
#define TNT_FILAMENT_BACKEND_PRIVATE_DRIVER_H

#include <backend/CallbackHandler.h>
#include <backend/DescriptorSetOffsetArray.h>
#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>
#include <backend/TargetBufferInfo.h>
#include <backend/AcquiredImage.h>

#include <utils/CString.h>
#include <utils/ImmutableCString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/compiler.h>

#include <functional>

#include <stddef.h>
#include <stdint.h>

// Command debugging off. debugging virtuals are not called.
// This is automatically enabled in DEBUG builds.
#define FILAMENT_DEBUG_COMMANDS_NONE         0x0
// Command debugging enabled. No logging by default.
#define FILAMENT_DEBUG_COMMANDS_ENABLE       0x1
// Command debugging enabled. Every command logged to DLOG(INFO)
#define FILAMENT_DEBUG_COMMANDS_LOG          0x2
// Command debugging enabled. Every command logged to systrace
#define FILAMENT_DEBUG_COMMANDS_SYSTRACE     0x4

#define FILAMENT_DEBUG_COMMANDS              FILAMENT_DEBUG_COMMANDS_NONE

namespace filament::backend {

class BufferDescriptor;
class CallbackHandler;
class PixelBufferDescriptor;
class Program;

template<typename T>
class ConcreteDispatcher;
class Dispatcher;
class CommandStream;

/**
 * Driver 抽象基类
 * 
 * Driver 是所有后端实现的基类，定义了统一的渲染 API 接口。
 * 具体后端（OpenGL、Vulkan、Metal、WebGPU）需要实现这些虚函数。
 * 
 * 架构说明：
 * - 主线程通过 DriverApi 调用 Driver 方法，命令被序列化到命令流
 * - 渲染线程从命令流读取命令并执行对应的 Driver 方法
 * - 异步方法通过命令流执行，同步方法直接调用
 */
class Driver {
public:
    virtual ~Driver() noexcept;

    /**
     * 获取元素类型的大小（字节数）
     * @param type 元素类型（BYTE、FLOAT、HALF 等）
     * @return 元素类型的大小（字节）
     */
    static size_t getElementTypeSize(ElementType type) noexcept;

    /**
     * 清理回调队列
     * 
     * 在主线程（非渲染线程）定期调用，用于执行用户回调。
     * 这是 Driver 执行用户回调的唯一入口点。
     * 
     * 实现说明：
     * - 从回调队列中取出所有待处理的回调
     * - 在主线程执行这些回调
     * - 确保回调在正确的线程上下文中执行
     */
    virtual void purge() noexcept = 0;

    /**
     * 获取着色器模型版本
     * @return 着色器模型（如 GLSL 330、GLSL ES 300 等）
     */
    virtual ShaderModel getShaderModel() const noexcept = 0;

    /**
     * 获取支持的着色器语言列表（按优先级排序）
     * 
     * 用于 matdbg（材质调试工具）显示正确的着色器代码。
     * 
     * @param preferredLanguage 首选语言（仅作为提示）
     * @return 支持的着色器语言列表，如果支持首选语言，它会在列表最前面
     * 
     * 说明：
     * - OpenGL: 区分 ESSL1 和 ESSL3
     * - Metal: 可以是 MSL 或 Metal 库，但 matdbg 目前只支持 MSL
     */
    virtual utils::FixedCapacityVector<ShaderLanguage> getShaderLanguages(
            ShaderLanguage preferredLanguage) const noexcept = 0;

    /**
     * 获取命令分发器
     * 
     * 返回 Dispatcher 对象，用于将命令分发到对应的 Driver 方法。
     * 只在 CommandStream 初始化时调用一次，所以虚函数调用开销可以接受。
     * 
     * @return Dispatcher 对象，包含所有 Driver 方法的函数指针映射
     */
    virtual Dispatcher getDispatcher() const noexcept = 0;

    /**
     * 执行一批驱动命令
     * 
     * 在渲染线程的 CommandStream::execute() 中调用。
     * 给 Driver 一个机会包装命令执行的上下文（如设置调试标记、性能分析等）。
     * 
     * 默认实现直接调用 fn，但具体后端可以重写以添加：
     * - 调试标记（pushGroupMarker/popGroupMarker）
     * - 性能分析（Profiler）
     * - 错误检查
     * 
     * @param fn 要执行的函数，包含一批 Driver 命令
     */
    virtual void execute(std::function<void(void)> const& fn);

    /**
     * 调试：命令开始标记
     * 
     * 在调试构建或手动启用时，在命令执行前调用。
     * 用于标记命令执行的开始，便于调试和性能分析。
     * 
     * @param cmds 命令流指针
     * @param synchronous 是否为同步调用
     * @param methodName 方法名称（用于日志和调试）
     */
    virtual void debugCommandBegin(CommandStream* cmds,
            bool synchronous, const char* methodName) noexcept = 0;

    /**
     * 调试：命令结束标记
     * 
     * 在命令执行后调用，与 debugCommandBegin 配对使用。
     * 
     * @param cmds 命令流指针
     * @param synchronous 是否为同步调用
     * @param methodName 方法名称（用于日志和调试）
     */
    virtual void debugCommandEnd(CommandStream* cmds,
            bool synchronous, const char* methodName) noexcept = 0;

    /*
     * Asynchronous calls here only to provide a type to CommandStream. They must be non-virtual
     * so that calling the concrete implementation won't go through a vtable.
     *
     * Synchronous calls are virtual and are called directly by CommandStream.
     */

#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    void methodName(paramsDecl) {}

#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    virtual RetType methodName(paramsDecl) = 0;

#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    virtual RetType methodName##S() noexcept = 0; \
    void methodName##R(RetType, paramsDecl) {}

#include "private/backend/DriverAPI.inc"
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_DRIVER_H
