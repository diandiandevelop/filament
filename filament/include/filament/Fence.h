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

//! \file

#ifndef TNT_FILAMENT_FENCE_H
#define TNT_FILAMENT_FENCE_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>

#include <stdint.h>

namespace filament {

/**
 * Fence is used to synchronize the application main thread with filament's rendering thread.
 */
/**
 * Fence 用于同步应用程序主线程与 Filament 的渲染线程
 */
class UTILS_PUBLIC Fence : public FilamentAPI {
public:
    //! Special \p timeout value to disable wait()'s timeout.
    /**
     * 特殊的 \p timeout 值，用于禁用 wait() 的超时
     */
    static constexpr uint64_t FENCE_WAIT_FOR_EVER = backend::FENCE_WAIT_FOR_EVER;

    //! Error codes for Fence::wait()
    /**
     * Fence::wait() 的错误代码
     */
    using FenceStatus = backend::FenceStatus;

    /** Mode controls the behavior of the command stream when calling wait()
     *
     * @attention
     * It would be unwise to call `wait(..., Mode::DONT_FLUSH)` from the same thread
     * the Fence was created, as it would most certainly create a dead-lock.
     */
    /**
     * Mode 控制在调用 wait() 时命令流的行为
     *
     * @attention
     * 从创建 Fence 的同一线程调用 `wait(..., Mode::DONT_FLUSH)` 是不明智的，
     * 因为这几乎肯定会造成死锁。
     */
    enum class Mode : uint8_t {
        FLUSH,          //!< The command stream is flushed
        /**
         * 命令流被刷新
         */
        DONT_FLUSH      //!< The command stream is not flushed
        /**
         * 命令流不被刷新
         */
    };

    /**
     * Client-side wait on the Fence.
     *
     * Blocks the current thread until the Fence signals.
     *
     * @param mode      Whether the command stream is flushed before waiting or not.
     * @param timeout   Wait time out. Using a \p timeout of 0 is a way to query the state of the fence.
     *                  A \p timeout value of FENCE_WAIT_FOR_EVER is used to disable the timeout.
     * @return          FenceStatus::CONDITION_SATISFIED on success,
     *                  FenceStatus::TIMEOUT_EXPIRED if the time out expired or
     *                  FenceStatus::ERROR in other cases.
     * @see #Mode
     */
    /**
     * 客户端等待 Fence
     *
     * 阻塞当前线程，直到 Fence 发出信号。
     *
     * @param mode      在等待之前是否刷新命令流
     * @param timeout   等待超时。使用 \p timeout 为 0 是查询栅栏状态的一种方式。
     *                  使用 FENCE_WAIT_FOR_EVER 的 \p timeout 值可禁用超时。
     * @return          成功时返回 FenceStatus::CONDITION_SATISFIED，
     *                  超时过期时返回 FenceStatus::TIMEOUT_EXPIRED，或
     *                  其他情况返回 FenceStatus::ERROR
     * @see #Mode
     */
    FenceStatus wait(Mode mode = Mode::FLUSH, uint64_t timeout = FENCE_WAIT_FOR_EVER);

    /**
     * Client-side wait on a Fence and destroy the Fence.
     *
     * @param fence Fence object to wait on.
     *
     * @param mode  Whether the command stream is flushed before waiting or not.
     *
     * @return  FenceStatus::CONDITION_SATISFIED on success,
     *          FenceStatus::ERROR otherwise.
     */
    /**
     * 客户端等待 Fence 并销毁 Fence
     *
     * @param fence 要等待的 Fence 对象
     *
     * @param mode  在等待之前是否刷新命令流
     *
     * @return  成功时返回 FenceStatus::CONDITION_SATISFIED，
     *          否则返回 FenceStatus::ERROR
     */
    static FenceStatus waitAndDestroy(Fence* UTILS_NONNULL fence, Mode mode = Mode::FLUSH);

protected:
    // prevent heap allocation
    ~Fence() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_FENCE_H
