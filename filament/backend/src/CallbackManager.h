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
 *
 */

#ifndef TNT_FILAMENT_BACKEND_CALLBACKMANAGER_H
#define TNT_FILAMENT_BACKEND_CALLBACKMANAGER_H

#include <backend/CallbackHandler.h>

#include <utils/Mutex.h>

#include <atomic>
#include <mutex>
#include <list>

namespace filament::backend {

class DriverBase;
class CallbackHandler;

/**
 * 回调管理器
 * 
 * CallbackManager 在所有先前的条件都满足时调度用户回调。
 * 
 * 工作流程：
 * 1. 通过调用 get() 创建"条件"，返回一个句柄
 * 2. 通过调用 put() 满足条件（通常从不同线程调用）
 * 3. 通过 setCallback() 设置回调，当所有条件都满足时回调会被调度
 * 
 * 使用场景：
 * - 等待多个异步操作完成
 * - 资源释放的延迟回调
 * - GPU 命令完成的回调
 * 
 * 线程安全：
 * - get() 和 put() 可以从不同线程调用
 * - setCallback() 会原子地创建新的条件集
 */
class CallbackManager {
    /**
     * 回调结构
     * 
     * 存储回调信息和引用计数（条件计数）。
     */
    struct Callback {
        mutable std::atomic_int count{};  // 引用计数（条件计数）
        CallbackHandler* handler = nullptr;  // 回调处理器
        CallbackHandler::Callback func = {};  // 回调函数
        void* user = nullptr;  // 用户数据指针
    };

    using Container = std::list<Callback>;  // 回调容器（双向链表）

public:
    using Handle = Container::const_iterator;

    explicit CallbackManager(DriverBase& driver);

    ~CallbackManager() noexcept;

    /**
     * 终止回调管理器
     * 
     * 执行所有待处理的回调，无论是否满足条件。
     * 这用于避免资源泄漏，例如在关闭时。
     * 如果条件未满足也没关系，因为我们正在关闭。
     */
    void terminate() noexcept;

    /**
     * 创建条件并获取句柄
     * 
     * 创建一个新的条件（通过增加引用计数），并返回句柄。
     * 这个句柄必须通过 put() 来满足条件。
     * 
     * @return 条件句柄
     */
    Handle get() const noexcept;

    /**
     * 满足指定条件
     * 
     * 当条件满足时（通过减少引用计数），如果所有条件都已满足且设置了回调，
     * 则调度回调。
     * 
     * @param curr 条件句柄（会被清空）
     */
    void put(Handle& curr) noexcept;

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
    void setCallback(CallbackHandler* handler, CallbackHandler::Callback func, void* user);

private:
    /**
     * 获取当前槽位（最后一个槽位）
     * 
     * @return 当前槽位的迭代器
     */
    Container::const_iterator getCurrent() const noexcept {
        std::lock_guard const lock(mLock);
        return --mCallbacks.end();
    }

    /**
     * 分配新槽位
     * 
     * 在列表末尾添加新槽位，并返回前一个槽位的迭代器。
     * 
     * @return 新分配槽位的前一个槽位的迭代器
     */
    Container::iterator allocateNewSlot() {
        std::lock_guard const lock(mLock);
        auto curr = --mCallbacks.end();
        mCallbacks.emplace_back();  // 在末尾添加新槽位
        return curr;
    }
    
    /**
     * 销毁槽位
     * 
     * 从列表中移除指定的槽位。
     * 
     * @param curr 要销毁的槽位迭代器
     */
    void destroySlot(Container::const_iterator curr) noexcept {
        std::lock_guard const lock(mLock);
        mCallbacks.erase(curr);
    }

    DriverBase& mDriver;
    mutable utils::Mutex mLock;
    Container mCallbacks;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_CALLBACKMANAGER_H
