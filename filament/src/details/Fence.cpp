/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "details/Fence.h"

#include "details/Engine.h"

#include <filament/Fence.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/Panic.h>
#include <utils/debug.h>

#include <condition_variable>
#include <chrono>
#include <memory>
#include <mutex>

#include <stdint.h>

namespace filament {

using namespace backend;

/**
 * 静态互斥锁
 * 
 * 用于同步所有栅栏对象的信号。
 */
utils::Mutex FFence::sLock;

/**
 * 静态条件变量
 * 
 * 用于等待栅栏信号。
 */
utils::Condition FFence::sCondition;

/**
 * 轮询间隔（毫秒）
 * 
 * 在需要轮询平台事件时，每次等待的时间间隔。
 */
static constexpr uint64_t PUMP_INTERVAL_MILLISECONDS = 1;

using ms = std::chrono::milliseconds;  // 毫秒类型别名
using ns = std::chrono::nanoseconds;   // 纳秒类型别名

/**
 * 栅栏构造函数
 * 
 * 创建栅栏对象并排队命令以在命令流中发出信号。
 * 
 * @param engine 引擎引用
 */
FFence::FFence(FEngine& engine)
    : mEngine(engine),  // 保存引擎引用
      mFenceSignal(std::make_shared<FenceSignal>()) {  // 创建栅栏信号对象
    DriverApi& driverApi = engine.getDriverApi();  // 获取驱动 API

    /**
     * 我们必须首先等待栅栏被命令流发出信号
     */
    // we have to first wait for the fence to be signaled by the command stream
    auto& fs = mFenceSignal;  // 获取信号引用
    driverApi.queueCommand([fs]() {  // 排队命令
        fs->signal();  // 发出信号
    });
}

/**
 * 终止栅栏
 * 
 * 标记栅栏为已销毁状态。
 * 
 * @param engine 引擎引用（未使用）
 */
void FFence::terminate(FEngine&) noexcept {
    FenceSignal * const fs = mFenceSignal.get();  // 获取信号指针
    fs->signal(FenceSignal::DESTROYED);  // 发出销毁信号
}

/**
 * 等待并销毁栅栏
 * 
 * 等待栅栏满足条件，然后销毁栅栏对象。
 * 
 * @param fence 栅栏指针
 * @param mode 等待模式
 * @return 栅栏状态
 */
UTILS_NOINLINE
FenceStatus FFence::waitAndDestroy(FFence* fence, Mode const mode) noexcept {
    assert_invariant(fence);  // 断言栅栏指针有效
    FenceStatus const status = fence->wait(mode, FENCE_WAIT_FOR_EVER);  // 等待栅栏（无限等待）
    fence->mEngine.destroy(fence);  // 销毁栅栏对象
    return status;  // 返回状态
}

/**
 * 等待栅栏
 * 
 * 等待栅栏满足条件。
 * 
 * @param mode 等待模式（FLUSH 或 DONT_FLUSH）
 * @param timeout 超时时间（纳秒），FENCE_WAIT_FOR_EVER 表示无限等待
 * @return 栅栏状态
 */
UTILS_NOINLINE
FenceStatus FFence::wait(Mode const mode, uint64_t const timeout) {
    /**
     * 验证：非零超时需要线程支持
     */
    FILAMENT_CHECK_PRECONDITION(UTILS_HAS_THREADING || timeout == 0)
            << "Non-zero timeout requires threads.";

    FEngine& engine = mEngine;  // 获取引擎引用

    /**
     * 如果模式为 FLUSH，刷新命令流
     */
    if (mode == Mode::FLUSH) {
        engine.flush();  // 刷新命令流
    }

    FenceSignal * const fs = mFenceSignal.get();  // 获取信号指针

    FenceStatus status;  // 状态变量

    /**
     * 如果不需要轮询平台事件，直接等待
     */
    if (UTILS_LIKELY(!engine.pumpPlatformEvents())) {
        status = fs->wait(timeout);  // 等待信号
    } else {
        /**
         * 不幸的是，某些平台可能强制我们在 GL 线程和用户线程之间设置同步点。
         * 为了防止这些平台上的死锁，我们将等待时间分成轮询和泵送平台事件队列。
         */
        // Unfortunately, some platforms might force us to have sync points between the GL thread
        // and user thread. To prevent deadlock on these platforms, we chop up the waiting time into
        // polling and pumping the platform's event queue.
        const auto startTime = std::chrono::system_clock::now();  // 记录开始时间
        while (true) {
            status = fs->wait(ns(ms(PUMP_INTERVAL_MILLISECONDS)).count());  // 等待一小段时间
            if (status != FenceStatus::TIMEOUT_EXPIRED) {  // 如果状态不是超时
                break;  // 退出循环
            }
            engine.pumpPlatformEvents();  // 泵送平台事件
            const auto elapsed = std::chrono::system_clock::now() - startTime;  // 计算已用时间
            if (timeout != FENCE_WAIT_FOR_EVER && elapsed >= ns(timeout)) {  // 如果超过总超时时间
                break;  // 退出循环
            }
        }
    }

    /**
     * 如果状态不是条件满足，返回状态
     */
    if (status != FenceStatus::CONDITION_SATISFIED) {
        return status;  // 返回状态（可能是超时或错误）
    }

    return status;  // 返回条件满足状态
}

/**
 * 发出栅栏信号
 * 
 * 设置栅栏状态并通知所有等待的线程。
 * 
 * @param s 状态（UNSIGNALED、SIGNALED 或 DESTROYED）
 */
UTILS_NOINLINE
void FFence::FenceSignal::signal(State const s) noexcept {
    std::lock_guard const lock(sLock);  // 获取锁
    mState = s;  // 设置状态
    sCondition.notify_all();  // 通知所有等待的线程
}

/**
 * 等待栅栏信号
 * 
 * 等待栅栏被发出信号。
 * 
 * @param timeout 超时时间（纳秒），FENCE_WAIT_FOR_EVER 表示无限等待，0 表示立即返回
 * @return 栅栏状态
 */
UTILS_NOINLINE
Fence::FenceStatus FFence::FenceSignal::wait(uint64_t const timeout) noexcept {
    std::unique_lock lock(sLock);  // 获取唯一锁
    while (mState == UNSIGNALED) {  // 当状态为未发出信号时
        if (timeout == FENCE_WAIT_FOR_EVER) {  // 如果无限等待
            sCondition.wait(lock);  // 等待条件变量
        } else {
            /**
             * 如果超时为 0 或等待超时，返回超时状态
             */
            if (timeout == 0 ||  // 如果超时为 0（立即返回）
                    sCondition.wait_for(lock, ns(timeout)) == std::cv_status::timeout) {  // 或等待超时
                return FenceStatus::TIMEOUT_EXPIRED;  // 返回超时状态
            }
        }
    }
    /**
     * 如果状态为已销毁，返回错误状态
     */
    if (mState == DESTROYED) {
        return FenceStatus::ERROR;  // 返回错误状态
    }
    /**
     * 状态为已发出信号，返回条件满足状态
     */
    return FenceStatus::CONDITION_SATISFIED;  // 返回条件满足状态
}

} // namespace filament
