/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_TIMERQUERY_H
#define TNT_FILAMENT_BACKEND_OPENGL_TIMERQUERY_H

// 后端枚举
#include <backend/DriverEnums.h>

// 驱动基类
#include "DriverBase.h"

// Utils 工具库
#include <utils/AsyncJobQueue.h>  // 异步任务队列

// OpenGL 头文件
#include "gl_headers.h"

// 标准库
#include <atomic>      // 原子操作
#include <chrono>      // 时间库
#include <functional>  // 函数对象
#include <memory>      // 智能指针
#include <thread>      // 线程
#include <vector>      // 向量

#include <stdint.h>    // 标准整数类型

namespace filament::backend {

class OpenGLPlatform;
class OpenGLContext;
class OpenGLDriver;
class TimerQueryFactoryInterface;

/**
 * OpenGL 定时查询结构
 * 
 * 封装 OpenGL 定时查询，用于测量 GPU 执行时间。
 * 
 * 主要功能：
 * 1. 测量 GPU 命令执行时间（时间流逝查询）
 * 2. 支持多种实现策略（原生查询、Fence、回退）
 * 
 * 设计目的：
 * - 提供统一的定时查询接口
 * - 根据硬件支持选择最佳实现
 * - 处理不同 GPU 驱动的准确性问题
 */
struct GLTimerQuery : public HwTimerQuery {
    /**
     * 定时查询状态
     * 
     * 存储定时查询的 OpenGL 状态和结果。
     */
    struct State {
        /**
         * OpenGL 相关状态
         */
        struct {
            GLuint query;  // OpenGL 查询对象 ID
        } gl;
        int64_t then{};  // 开始时间（用于 Fence 和回退实现）
        std::atomic<int64_t> elapsed{};  // 已用时间（纳秒），原子操作保证线程安全
    };
    std::shared_ptr<State> state;  // 共享状态指针，允许多线程访问
};

/*
 * 我们需要定时查询的多种实现（仅时间流逝），因为
 * 在某些 GPU 上，disjoint_timer_query/arb_timer_query 的准确性远低于
 * 使用 Fence。
 *
 * 这些类实现了各种策略...
 */

/**
 * 定时查询工厂类
 * 
 * 负责创建和初始化定时查询工厂实现。
 * 根据硬件支持选择最佳实现策略。
 */
class TimerQueryFactory {
    static bool mGpuTimeSupported;  // 是否支持 GPU 时间查询
public:
    /**
     * 初始化定时查询工厂
     * 
     * 根据平台和上下文支持选择最佳实现。
     * 
     * @param platform OpenGLPlatform 引用
     * @param context OpenGLContext 引用
     * @return 定时查询工厂接口指针
     * 
     * 选择策略：
     * 1. 如果支持 EXT_disjoint_timer_query：
     *    - 如果驱动有 Bug，使用 Fence 实现（如果支持）
     *    - 否则使用原生查询实现
     * 2. 如果不支持查询但支持 Fence，使用 Fence 实现
     * 3. 否则使用回退实现（CPU 时间）
     */
    static TimerQueryFactoryInterface* init(
            OpenGLPlatform& platform, OpenGLContext& context);

    /**
     * 检查是否支持 GPU 时间查询
     * 
     * @return 如果支持 GPU 时间查询返回 true，否则返回 false
     */
    static bool isGpuTimeSupported() noexcept {
        return mGpuTimeSupported;
    }
};

/**
 * 定时查询工厂接口
 * 
 * 定义定时查询操作的抽象接口。
 * 不同的实现类提供不同的策略。
 */
class TimerQueryFactoryInterface {
protected:
    using GLTimerQuery = filament::backend::GLTimerQuery;
    using clock = std::chrono::steady_clock;  // 稳定时钟，用于时间测量

public:
    virtual ~TimerQueryFactoryInterface();
    
    /**
     * 创建定时查询
     * 
     * @param query 定时查询对象指针
     */
    virtual void createTimerQuery(GLTimerQuery* query) = 0;
    
    /**
     * 销毁定时查询
     * 
     * @param query 定时查询对象指针
     */
    virtual void destroyTimerQuery(GLTimerQuery* query) = 0;
    
    /**
     * 开始时间流逝查询
     * 
     * @param query 定时查询对象指针
     */
    virtual void beginTimeElapsedQuery(GLTimerQuery* query) = 0;
    
    /**
     * 结束时间流逝查询
     * 
     * @param driver OpenGLDriver 引用，用于注册回调
     * @param query 定时查询对象指针
     */
    virtual void endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* query) = 0;

    /**
     * 获取定时查询值
     * 
     * 非阻塞地获取定时查询结果。
     * 
     * @param tq 定时查询对象指针
     * @param elapsedTime 输出参数：已用时间（纳秒）
     * @return 查询结果状态（AVAILABLE、NOT_READY、ERROR）
     */
    static TimerQueryResult getTimerQueryValue(GLTimerQuery* tq, uint64_t* elapsedTime) noexcept;
};

#if defined(BACKEND_OPENGL_VERSION_GL) || defined(GL_EXT_disjoint_timer_query)

/**
 * 原生定时查询工厂
 * 
 * 使用 OpenGL 原生定时查询扩展（EXT_disjoint_timer_query 或 ARB_timer_query）。
 * 这是最准确的实现，直接测量 GPU 时间。
 * 
 * 实现方式：
 * - 使用 glBeginQuery(GL_TIME_ELAPSED) 和 glEndQuery()
 * - 使用 glGetQueryObjectui64v() 获取结果
 * - 异步查询结果（通过 runEveryNowAndThen）
 */
class TimerQueryNativeFactory final : public TimerQueryFactoryInterface {
public:
    explicit TimerQueryNativeFactory(OpenGLContext& context);
    ~TimerQueryNativeFactory() override;
private:
    void createTimerQuery(GLTimerQuery* query) override;
    void destroyTimerQuery(GLTimerQuery* query) override;
    void beginTimeElapsedQuery(GLTimerQuery* query) override;
    void endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* query) override;
    OpenGLContext& mContext;  // OpenGL 上下文引用
};

#endif

/**
 * Fence 定时查询工厂
 * 
 * 使用平台 Fence 来测量 GPU 时间。
 * 当原生定时查询不可用或不准确时使用。
 * 
 * 实现方式：
 * - 在开始和结束时创建 Fence
 * - 在后台线程等待 Fence 完成
 * - 使用 CPU 时钟测量 Fence 之间的时间
 * 
 * 注意：
 * - 此实现测量的是 CPU 时间，不是真正的 GPU 时间
 * - 通常不如原生查询准确，但在某些驱动上可能更可靠
 */
class TimerQueryFenceFactory final : public TimerQueryFactoryInterface {
public:
    explicit TimerQueryFenceFactory(OpenGLPlatform& platform);
    ~TimerQueryFenceFactory() override;
private:
    void createTimerQuery(GLTimerQuery* query) override;
    void destroyTimerQuery(GLTimerQuery* query) override;
    void beginTimeElapsedQuery(GLTimerQuery* tq) override;
    void endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* tq) override;

    OpenGLPlatform& mPlatform;  // OpenGL 平台引用
    utils::AsyncJobQueue mJobQueue;  // 异步任务队列（用于后台等待 Fence）
};

/**
 * 回退定时查询工厂
 * 
 * 当既没有定时查询也没有 Fence 支持时使用。
 * 使用 CPU 时钟测量时间，不提供真正的 GPU 时间。
 * 
 * 实现方式：
 * - 在开始和结束时记录 CPU 时间
 * - 计算时间差作为"已用时间"
 * 
 * 限制：
 * - 测量的是 CPU 时间，不是 GPU 时间
 * - 不准确，但至少提供某种时间测量
 */
class TimerQueryFallbackFactory final : public TimerQueryFactoryInterface {
public:
    explicit TimerQueryFallbackFactory();
    ~TimerQueryFallbackFactory() override;
private:
    void createTimerQuery(GLTimerQuery* query) override;
    void destroyTimerQuery(GLTimerQuery* query) override;
    void beginTimeElapsedQuery(GLTimerQuery* query) override;
    void endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* query) override;
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_TIMERQUERY_H
