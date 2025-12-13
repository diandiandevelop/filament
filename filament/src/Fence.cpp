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

namespace filament {

using namespace backend;

/**
 * 等待围栏并销毁
 * 
 * 等待围栏信号到达指定状态，然后销毁围栏对象。
 * 这是一个便捷方法，用于一次性完成等待和清理操作。
 * 
 * @param fence 围栏对象指针（将被销毁）
 * @param mode 等待模式
 *             - FLUSH: 刷新命令队列后等待
 *             - DONT_FLUSH: 不刷新命令队列，直接等待
 * @return 围栏状态
 *         - CONDITION_SATISFIED: 条件已满足
 *         - TIMEOUT_EXPIRED: 超时
 *         - ERROR: 错误
 * 
 * 实现：将调用转发到内部实现类（FFence）
 */
FenceStatus Fence::waitAndDestroy(Fence* fence, Mode const mode) {
    return FFence::waitAndDestroy(downcast(fence), mode);
}

/**
 * 等待围栏
 * 
 * 等待围栏信号到达指定状态。
 * 围栏用于同步 GPU 和 CPU 之间的操作。
 * 
 * @param mode 等待模式
 *             - FLUSH: 刷新命令队列后等待（确保所有命令已提交）
 *             - DONT_FLUSH: 不刷新命令队列，直接等待
 * @param timeout 超时时间（纳秒）
 *                0 表示无限等待
 * @return 围栏状态
 *         - CONDITION_SATISFIED: 条件已满足（围栏已触发）
 *         - TIMEOUT_EXPIRED: 超时（在指定时间内未触发）
 *         - ERROR: 错误（围栏无效或其他错误）
 * 
 * 实现：将调用转发到内部实现类（FFence）
 */
FenceStatus Fence::wait(Mode const mode, uint64_t const timeout) {
    return downcast(this)->wait(mode, timeout);
}

} // namespace filament
