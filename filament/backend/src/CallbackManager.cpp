/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "CallbackManager.h"

#include "DriverBase.h"

namespace filament::backend {

/**
 * CallbackManager 构造函数
 * 
 * 初始化回调管理器，创建一个初始的 Callback 槽位。
 * 
 * @param driver DriverBase 引用，用于调度回调
 */
CallbackManager::CallbackManager(DriverBase& driver)
    : mDriver(driver), mCallbacks(1) {  // 初始化为 1 个槽位
}

/**
 * CallbackManager 析构函数
 */
CallbackManager::~CallbackManager() noexcept = default;

/**
 * 终止回调管理器
 * 
 * 执行所有待处理的回调，无论是否满足条件。
 * 这用于避免资源泄漏，例如在关闭时。
 * 
 * 注意：如果条件未满足也没关系，因为我们正在关闭。
 */
void CallbackManager::terminate() noexcept {
    for (auto&& item: mCallbacks) {
        if (item.func) {
            mDriver.scheduleCallback(
                    item.handler, item.user, item.func);
        }
    }
}

/**
 * 创建条件并获取句柄
 * 
 * 创建一个新的条件（通过增加引用计数），并返回句柄。
 * 这个句柄必须通过 put() 来满足条件。
 * 
 * @return 条件句柄
 */
CallbackManager::Handle CallbackManager::get() const noexcept {
    Container::const_iterator const curr = getCurrent();
    curr->count.fetch_add(1);  // 增加引用计数（创建新条件）
    return curr;
}

/**
 * 满足指定条件
 * 
 * 当条件满足时（通过减少引用计数），如果所有条件都已满足且设置了回调，
 * 则调度回调。
 * 
 * @param curr 条件句柄（会被清空）
 */
void CallbackManager::put(Handle& curr) noexcept {
    /**
     * 减少引用计数，如果降为 0，说明所有条件都已满足
     */
    if (curr->count.fetch_sub(1) == 1) {
        /**
         * 如果设置了回调，调度它
         */
        if (curr->func) {
            mDriver.scheduleCallback(
                    curr->handler, curr->user, curr->func);
            destroySlot(curr);
        }
    }
    curr = {};  // 清空句柄
}

/**
 * 设置回调
 * 
 * 设置一个回调，当所有之前创建的条件（通过 get()）都满足时（通过 put()），
 * 回调会被调度。
 * 
 * 如果没有创建条件，或者所有条件都已满足，回调会立即被调度。
 * 
 * @param handler 回调处理器
 * @param func 回调函数
 * @param user 用户数据指针
 */
void CallbackManager::setCallback(
        CallbackHandler* handler, CallbackHandler::Callback func, void* user) {
    assert_invariant(func);
    Container::iterator const curr = allocateNewSlot();
    curr->handler = handler;
    curr->func = func;
    curr->user = user;
    
    /**
     * 如果当前槽位的引用计数为 0，说明所有条件都已满足，立即调度回调
     */
    if (curr->count == 0) {
        mDriver.scheduleCallback(
                curr->handler, curr->user, curr->func);
        destroySlot(curr);
    }
}

} // namespace filament::backend
