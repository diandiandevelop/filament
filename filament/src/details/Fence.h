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

#ifndef TNT_FILAMENT_DETAILS_FENCE_H
#define TNT_FILAMENT_DETAILS_FENCE_H

#include "downcast.h"

#include <filament/Fence.h>

#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/Condition.h>
#include <utils/Mutex.h>

namespace filament {

class FEngine;

/**
 * 栅栏实现类
 * 
 * 提供 GPU-CPU 同步机制。
 * 栅栏用于确保 GPU 命令在某个点完成执行，然后 CPU 可以安全地访问相关资源。
 * 
 * 实现细节：
 * - 使用共享的互斥锁和条件变量来同步所有栅栏对象
 * - 栅栏信号通过命令流发出
 * - 支持阻塞和非阻塞等待
 */
class FFence : public Fence {
public:
    /**
     * 构造函数
     * 
     * 创建栅栏对象并排队命令以在命令流中发出信号。
     * 
     * @param engine 引擎引用
     */
    FFence(FEngine& engine);

    /**
     * 终止栅栏
     * 
     * 标记栅栏为已销毁状态。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 等待栅栏
     * 
     * 等待栅栏满足条件。
     * 
     * @param mode 等待模式（FLUSH 或 DONT_FLUSH）
     * @param timeout 超时时间（纳秒），FENCE_WAIT_FOR_EVER 表示无限等待
     * @return 栅栏状态
     */
    FenceStatus wait(Mode mode, uint64_t timeout);

    /**
     * 等待并销毁栅栏
     * 
     * 等待栅栏满足条件，然后销毁栅栏对象。
     * 
     * @param fence 栅栏指针
     * @param mode 等待模式
     * @return 栅栏状态
     */
    static FenceStatus waitAndDestroy(FFence* fence, Mode mode) noexcept;

private:
    /**
     * 静态互斥锁
     * 
     * 我们假设栅栏的竞争不多，让它们都共享一个锁/条件变量。
     */
    // We assume we don't have a lot of contention of fence and have all of them
    // share a single lock/condition
    static utils::Mutex sLock;  // 静态互斥锁
    static utils::Condition sCondition;  // 静态条件变量

    /**
     * 栅栏信号结构
     * 
     * 管理栅栏的状态和信号。
     */
    struct FenceSignal {
        /**
         * 默认构造函数
         */
        explicit FenceSignal() noexcept = default;
        
        /**
         * 状态枚举
         */
        enum State : uint8_t { 
            UNSIGNALED,  // 未发出信号
            SIGNALED,    // 已发出信号
            DESTROYED    // 已销毁
        };
        
        State mState = UNSIGNALED;  // 当前状态（默认为未发出信号）
        
        /**
         * 发出信号
         * 
         * 设置栅栏状态并通知所有等待的线程。
         * 
         * @param s 状态（默认为 SIGNALED）
         */
        void signal(State s = SIGNALED) noexcept;
        
        /**
         * 等待栅栏信号
         * 
         * 等待栅栏被发出信号。
         * 
         * @param timeout 超时时间（纳秒），FENCE_WAIT_FOR_EVER 表示无限等待，0 表示立即返回
         * @return 栅栏状态
         */
        FenceStatus wait(uint64_t timeout) noexcept;
    };

    FEngine& mEngine;  // 引擎引用
    /**
     * TODO: 为这些小对象使用自定义分配器
     */
    // TODO: use custom allocator for these small objects
    std::shared_ptr<FenceSignal> mFenceSignal;  // 栅栏信号共享指针
};

FILAMENT_DOWNCAST(Fence)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_FENCE_H
