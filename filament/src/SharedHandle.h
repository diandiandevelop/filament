/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SHARED_HANDLE_H
#define TNT_FILAMENT_SHARED_HANDLE_H

#include <backend/Handle.h>

#include <stdint.h>

namespace filament {

/**
 * 共享句柄
 * 
 * 类似于 shared_ptr<>，但用于 Handle<>。
 * 销毁由提供的 Deleter 函子执行。
 * 目前仅支持强引用。
 * 
 * 注意：当前实现不是线程安全的。
 * 
 * @tparam T 句柄类型
 * @tparam Deleter 删除器类型（函子）
 */
template<typename T, typename Deleter>
struct SharedHandle {
    /**
     * 默认构造函数
     */
    SharedHandle() noexcept = default;

    /**
     * 析构函数
     * 
     * 减少引用计数，如果计数为 0 则销毁控制块。
     */
    ~SharedHandle() noexcept {
        dec(mControlBlockPtr);
    }

    /**
     * 拷贝构造函数
     * 
     * 增加引用计数。
     */
    SharedHandle(SharedHandle const& rhs) noexcept
            : mControlBlockPtr(inc(rhs.mControlBlockPtr)) {
    }

    /**
     * 移动构造函数
     */
    SharedHandle(SharedHandle&& rhs) noexcept {
        std::swap(mControlBlockPtr, rhs.mControlBlockPtr);
    }

    /**
     * 拷贝赋值操作符
     * 
     * 实现细节：
     * 1. 先增加右侧的引用计数（防止自赋值时过早销毁）
     * 2. 减少左侧的引用计数（可能销毁左侧的控制块）
     * 3. 采用右侧的控制块指针
     */
    SharedHandle& operator=(SharedHandle const& rhs) noexcept {
        if (this != &rhs) {  // 防止自赋值
            inc(rhs.mControlBlockPtr);  // 增加其他控制块的引用（先增加，防止自赋值问题）
            dec(mControlBlockPtr);  // 减少我们的引用（可能销毁它）
            mControlBlockPtr = rhs.mControlBlockPtr;  // 采用新的控制块
        }
        return *this;
    }

    /**
     * 移动赋值操作符
     * 
     * 交换控制块指针，不改变引用计数。
     */
    SharedHandle& operator=(SharedHandle&& rhs) noexcept {
        if (this != &rhs) {  // 防止自移动
            std::swap(mControlBlockPtr, rhs.mControlBlockPtr);  // 交换控制块指针
        }
        return *this;
    }

    /**
     * 从 Handle 构造（提供删除器）
     * 
     * @param rhs 句柄
     * @param args 删除器构造参数
     */
    template<typename ... ARGS>
    explicit SharedHandle(backend::Handle<T> const& rhs, ARGS&& ... args) noexcept
            : mControlBlockPtr(new ControlBlock(rhs, std::forward<ARGS>(args)...)) {
    }

    /**
     * 从 Handle 构造（提供删除器，移动版本）
     * 
     * @param rhs 句柄（会被移动）
     * @param args 删除器构造参数
     */
    template<typename ... ARGS>
    explicit SharedHandle(backend::Handle<T>&& rhs, ARGS&& ... args) noexcept
            : mControlBlockPtr(new ControlBlock(rhs, std::forward<ARGS>(args)...)) {
    }

    /**
     * 自动转换为 Handle<T>
     */
    operator backend::Handle<T>() const noexcept { // NOLINT(*-explicit-constructor)
        return mControlBlockPtr ? mControlBlockPtr->handle : backend::Handle<T>{};
    }

    /**
     * 布尔转换
     * 
     * @return true 如果句柄有效
     */
    explicit operator bool() const noexcept {
        return mControlBlockPtr ? (bool)mControlBlockPtr->handle : false;
    }

    /**
     * 清空共享句柄
     * 
     * 减少引用计数，如果计数为 0 则销毁控制块。
     */
    void clear() noexcept { dec(mControlBlockPtr); }

private:
    /**
     * 控制块
     * 
     * 存储引用计数、句柄和删除器。
     * 类似于 shared_ptr 的控制块，但针对 Handle 类型进行了优化。
     */
    struct ControlBlock {
        /**
         * 构造函数
         * 
         * 创建控制块，初始化引用计数为 1。
         * 
         * @param handle 句柄（会被移动）
         * @param args 删除器构造参数（完美转发）
         */
        template<typename ... ARGS>
        explicit ControlBlock(backend::Handle<T> handle, ARGS&& ... args) noexcept
                : deleter(std::forward<ARGS>(args)...),  // 构造删除器（完美转发）
                  handle(std::move(handle)) {  // 移动句柄
            // 引用计数初始化为 1（在成员初始化列表中）
        }
        
        /**
         * 增加引用计数
         * 
         * 当新的 SharedHandle 引用此控制块时调用。
         */
        void inc() noexcept {
            ++count;  // 原子递增（如果支持）
        }
        
        /**
         * 减少引用计数
         * 
         * 当 SharedHandle 不再引用此控制块时调用。
         * 如果计数降为 0，调用删除器销毁句柄，然后销毁控制块本身。
         */
        void dec() noexcept {
            if (--count == 0) {  // 减少引用计数
                deleter(handle);  // 调用删除器销毁句柄
                delete this;  // 销毁控制块（自删除）
            }
        }
        
        Deleter deleter;  // 删除器（用于销毁句柄）
        int32_t count = 1;  // 引用计数（初始化为 1）
        backend::Handle<T> handle;  // 句柄（要管理的资源）
    };

    /**
     * 增加控制块的引用计数
     * 
     * @param ctrlBlk 控制块指针
     * @return 控制块指针
     */
    ControlBlock* inc(ControlBlock* const ctrlBlk) noexcept {
        if (ctrlBlk) {
            ctrlBlk->inc();
        }
        return ctrlBlk;
    }

    /**
     * 减少控制块的引用计数
     * 
     * @param ctrlBlk 控制块指针
     */
    void dec(ControlBlock* const ctrlBlk) noexcept {
        if (ctrlBlk) {
            ctrlBlk->dec();
        }
    }

    ControlBlock* mControlBlockPtr = nullptr;  // 控制块指针
};

} // namespace filament

#endif // TNT_FILAMENT_SHARED_HANDLE_H
