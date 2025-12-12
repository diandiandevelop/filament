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

#include "OpenGLTimerQuery.h"

#include "GLUtils.h"
#include "OpenGLDriver.h"

#include <backend/Platform.h>
#include <backend/platforms/OpenGLPlatform.h>
#include <backend/DriverEnums.h>

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/JobSystem.h>
#include <utils/Log.h>
#include <utils/Mutex.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include <stdint.h>

namespace filament::backend {

using namespace backend;
using namespace GLUtils;

class OpenGLDriver;

// ------------------------------------------------------------------------------------------------

/**
 * GPU 时间支持标志
 * 
 * 指示当前平台是否支持真正的 GPU 时间查询。
 */
bool TimerQueryFactory::mGpuTimeSupported = false;

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
 *    - 如果驱动有 Bug（dont_use_timer_query）且支持 Fence，使用 Fence 实现
 *    - 否则使用原生查询实现
 * 2. 如果不支持查询但支持 Fence，使用 Fence 实现
 * 3. 否则使用回退实现（CPU 时间）
 */
TimerQueryFactoryInterface* TimerQueryFactory::init(
        OpenGLPlatform& platform, OpenGLContext& context) {
    (void)context;

    TimerQueryFactoryInterface* impl = nullptr;

#if defined(BACKEND_OPENGL_VERSION_GL) || defined(GL_EXT_disjoint_timer_query)
    if (context.ext.EXT_disjoint_timer_query) {
        // 定时查询可用
        if (context.bugs.dont_use_timer_query && platform.canCreateFence()) {
            // 但是它们工作不好，如果可以的话，回退到使用 Fence
            impl = new(std::nothrow) TimerQueryFenceFactory(platform);
        } else {
            // 使用原生查询实现
            impl = new(std::nothrow) TimerQueryNativeFactory(context);
        }
        mGpuTimeSupported = true;
    } else
#endif
    if (platform.canCreateFence()) {
        // 没有定时查询，但可以使用 Fence
        impl = new(std::nothrow) TimerQueryFenceFactory(platform);
        mGpuTimeSupported = true;
    } else {
        // 没有查询，没有 Fence -- 这是个问题
        impl = new(std::nothrow) TimerQueryFallbackFactory();
        mGpuTimeSupported = false;
    }
    assert_invariant(impl);
    return impl;
}

// ------------------------------------------------------------------------------------------------

/**
 * 析构函数
 * 
 * 默认析构函数。
 */
TimerQueryFactoryInterface::~TimerQueryFactoryInterface() = default;

/**
 * 获取定时查询值
 * 
 * 这是后端同步调用，非阻塞地获取定时查询结果。
 * 
 * @param tq 定时查询对象指针
 * @param elapsedTime 输出参数：已用时间（纳秒）
 * @return 查询结果状态
 * 
 * 返回值说明：
 * - AVAILABLE：结果可用，已用时间已写入 elapsedTime
 * - NOT_READY：结果尚未准备好（elapsed < 0）
 * - ERROR：查询对象无效（state 为空）
 */
TimerQueryResult TimerQueryFactoryInterface::getTimerQueryValue(
        GLTimerQuery* tq, uint64_t* elapsedTime) noexcept {
    if (UTILS_LIKELY(tq->state)) {
        // 使用 relaxed 内存顺序，因为我们只关心值本身
        int64_t const elapsed = tq->state->elapsed.load(std::memory_order_relaxed);
        if (elapsed > 0) {
            // 结果可用
            *elapsedTime = elapsed;
            return TimerQueryResult::AVAILABLE;
        }
        // 结果尚未准备好（elapsed < 0 表示 NOT_READY）
        return TimerQueryResult(elapsed);
    }
    // 查询对象无效
    return TimerQueryResult::ERROR;
}

// ------------------------------------------------------------------------------------------------

#if defined(BACKEND_OPENGL_VERSION_GL) || defined(GL_EXT_disjoint_timer_query)

/**
 * 构造函数
 * 
 * @param context OpenGLContext 引用
 */
TimerQueryNativeFactory::TimerQueryNativeFactory(OpenGLContext& context)
        : mContext(context) {
}

/**
 * 析构函数
 * 
 * 默认析构函数。
 */
TimerQueryNativeFactory::~TimerQueryNativeFactory() = default;

/**
 * 创建定时查询
 * 
 * 创建 OpenGL 查询对象。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryNativeFactory::createTimerQuery(GLTimerQuery* tq) {
    assert_invariant(!tq->state);

    // 创建状态对象并生成查询对象
    tq->state = std::make_shared<GLTimerQuery::State>();
    mContext.procs.genQueries(1u, &tq->state->gl.query);
    CHECK_GL_ERROR()
}

/**
 * 销毁定时查询
 * 
 * 删除 OpenGL 查询对象。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryNativeFactory::destroyTimerQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);

    // 删除查询对象
    mContext.procs.deleteQueries(1u, &tq->state->gl.query);
    CHECK_GL_ERROR()

    // 重置状态指针
    tq->state.reset();
}

/**
 * 开始时间流逝查询
 * 
 * 开始测量 GPU 时间流逝。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryNativeFactory::beginTimeElapsedQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);

    // 将已用时间设置为 NOT_READY
    tq->state->elapsed.store(int64_t(TimerQueryResult::NOT_READY), std::memory_order_relaxed);
    // 开始查询
    mContext.procs.beginQuery(GL_TIME_ELAPSED, tq->state->gl.query);
    CHECK_GL_ERROR()
}

/**
 * 结束时间流逝查询
 * 
 * 结束测量并注册回调以异步获取结果。
 * 
 * @param driver OpenGLDriver 引用，用于注册回调
 * @param tq 定时查询对象指针
 * 
 * 执行流程：
 * 1. 结束查询
 * 2. 注册周期性回调以检查结果是否可用
 * 3. 当结果可用时，读取并存储结果
 */
void TimerQueryNativeFactory::endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* tq) {
    assert_invariant(tq->state);

    // 结束查询
    mContext.procs.endQuery(GL_TIME_ELAPSED);
    CHECK_GL_ERROR()

    // 使用弱指针，避免循环引用
    std::weak_ptr<GLTimerQuery::State> const weak = tq->state;

    // 注册周期性回调以检查结果
    driver.runEveryNowAndThen([&context = mContext, weak]() -> bool {
        auto state = weak.lock();
        if (!state) {
            // 定时查询状态已被销毁，很可能是由于 IBL 预过滤上下文销毁。
            // 我们仍然返回 true 以将此元素从查询列表中移除。
            return true;
        }

        // 检查结果是否可用
        GLuint available = 0;
        context.procs.getQueryObjectuiv(state->gl.query, GL_QUERY_RESULT_AVAILABLE, &available);
        CHECK_GL_ERROR()
        if (!available) {
            // 结果尚未可用，需要稍后再试
            return false;
        }
        
        // 结果可用，读取已用时间
        GLuint64 elapsedTime = 0;
        // 如果我们不在 ES 上或没有 GL_EXT_disjoint_timer_query，不会到达这里
        context.procs.getQueryObjectui64v(state->gl.query, GL_QUERY_RESULT, &elapsedTime);
        state->elapsed.store((int64_t)elapsedTime, std::memory_order_relaxed);

        // 结果已读取，可以移除此回调
        return true;
    });
}

#endif

// ------------------------------------------------------------------------------------------------

/**
 * 构造函数
 * 
 * @param platform OpenGLPlatform 引用
 */
TimerQueryFenceFactory::TimerQueryFenceFactory(OpenGLPlatform& platform)
        : mPlatform(platform),
          mJobQueue("OpenGLTimerQueryFence", utils::AsyncJobQueue::Priority::URGENT_DISPLAY) {
}

/**
 * 析构函数
 * 
 * 排空并退出任务队列。
 */
TimerQueryFenceFactory::~TimerQueryFenceFactory() {
    mJobQueue.drainAndExit();
}

/**
 * 创建定时查询
 * 
 * 创建状态对象（不需要 OpenGL 查询对象）。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryFenceFactory::createTimerQuery(GLTimerQuery* tq) {
    assert_invariant(!tq->state);
    tq->state = std::make_shared<GLTimerQuery::State>();
}

/**
 * 销毁定时查询
 * 
 * 重置状态指针。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryFenceFactory::destroyTimerQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);
    tq->state.reset();
}

/**
 * 开始时间流逝查询
 * 
 * 创建 Fence 并在后台线程等待，记录开始时间。
 * 
 * @param tq 定时查询对象指针
 * 
 * FIXME: 此 beginTimeElapsedQuery 实现通常是不正确的；
 *    它最终测量的是当前 CPU 时间，因为 Fence 立即发出信号
 *    （通常此时 GPU 上没有工作）。我们可以通过在虚拟目标上
 *    发送小的 glClear 来解决这个问题，或者在下一个渲染通道
 *    开始时锁定开始时间。
 */
void TimerQueryFenceFactory::beginTimeElapsedQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);
    tq->state->elapsed.store(int64_t(TimerQueryResult::NOT_READY), std::memory_order_relaxed);

    std::weak_ptr<GLTimerQuery::State> const weak = tq->state;

    // FIXME: 此 beginTimeElapsedQuery 实现通常是不正确的；
    //    它最终测量的是当前 CPU 时间，因为 Fence 立即发出信号
    //    （通常此时 GPU 上没有工作）。我们可以通过在虚拟目标上
    //    发送小的 glClear 来解决这个问题，或者在下一个渲染通道
    //    开始时锁定开始时间。

    // 在后台线程中等待 Fence 并记录开始时间
    mJobQueue.push([&platform = mPlatform, fence = mPlatform.createFence(), weak]() {
        auto state = weak.lock();
        if (state) {
            // 等待 Fence 完成（这表示 GPU 已到达此点）
            platform.waitFence(fence, FENCE_WAIT_FOR_EVER);
            // 记录开始时间
            state->then = clock::now().time_since_epoch().count();
            FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
            FILAMENT_TRACING_ASYNC_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, "OpenGLTimerQueryFence", intptr_t(state.get()));
        }
        platform.destroyFence(fence);
    });
}

/**
 * 结束时间流逝查询
 * 
 * 创建 Fence 并在后台线程等待，计算已用时间。
 * 
 * @param driver OpenGLDriver 引用（未使用）
 * @param tq 定时查询对象指针
 */
void TimerQueryFenceFactory::endTimeElapsedQuery(OpenGLDriver&, GLTimerQuery* tq) {
    assert_invariant(tq->state);
    std::weak_ptr<GLTimerQuery::State> const weak = tq->state;

    // 在后台线程中等待 Fence 并计算已用时间
    mJobQueue.push([&platform = mPlatform, fence = mPlatform.createFence(), weak]() {
        auto state = weak.lock();
        if (state) {
            // 等待 Fence 完成（这表示 GPU 已到达此点）
            platform.waitFence(fence, FENCE_WAIT_FOR_EVER);
            // 计算已用时间
            int64_t const now = clock::now().time_since_epoch().count();
            state->elapsed.store(now - state->then, std::memory_order_relaxed);
            FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
            FILAMENT_TRACING_ASYNC_END(FILAMENT_TRACING_CATEGORY_FILAMENT, "OpenGLTimerQueryFence", intptr_t(state.get()));
        }
        platform.destroyFence(fence);
    });
}

// ------------------------------------------------------------------------------------------------

/**
 * 构造函数
 * 
 * 默认构造函数。
 */
TimerQueryFallbackFactory::TimerQueryFallbackFactory() = default;

/**
 * 析构函数
 * 
 * 默认析构函数。
 */
TimerQueryFallbackFactory::~TimerQueryFallbackFactory() = default;

/**
 * 创建定时查询
 * 
 * 创建状态对象（不需要 OpenGL 查询对象或 Fence）。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryFallbackFactory::createTimerQuery(GLTimerQuery* tq) {
    assert_invariant(!tq->state);
    tq->state = std::make_shared<GLTimerQuery::State>();
}

/**
 * 销毁定时查询
 * 
 * 重置状态指针。
 * 
 * @param tq 定时查询对象指针
 */
void TimerQueryFallbackFactory::destroyTimerQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);
    tq->state.reset();
}

/**
 * 开始时间流逝查询
 * 
 * 记录开始时间（CPU 时间）。
 * 
 * @param tq 定时查询对象指针
 * 
 * 注意：此实现测量的是 CPU 时间，但我们没有硬件支持。
 */
void TimerQueryFallbackFactory::beginTimeElapsedQuery(GLTimerQuery* tq) {
    assert_invariant(tq->state);
    // 此实现测量的是 CPU 时间，但我们没有硬件支持
    tq->state->then = clock::now().time_since_epoch().count();
    tq->state->elapsed.store(int64_t(TimerQueryResult::NOT_READY), std::memory_order_relaxed);
}

/**
 * 结束时间流逝查询
 * 
 * 计算已用时间（CPU 时间）。
 * 
 * @param driver OpenGLDriver 引用（未使用）
 * @param tq 定时查询对象指针
 * 
 * 注意：此实现测量的是 CPU 时间，但我们没有硬件支持。
 */
void TimerQueryFallbackFactory::endTimeElapsedQuery(OpenGLDriver&, GLTimerQuery* tq) {
    assert_invariant(tq->state);
    // 此实现测量的是 CPU 时间，但我们没有硬件支持
    int64_t const now = clock::now().time_since_epoch().count();
    tq->state->elapsed.store(now - tq->state->then, std::memory_order_relaxed);
}

} // namespace filament::backend
