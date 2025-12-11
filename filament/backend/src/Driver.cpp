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

#include "DriverBase.h"

#include "private/backend/Driver.h"
#include "private/backend/CommandStream.h"

#include <backend/AcquiredImage.h>
#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>

#include <private/utils/Tracing.h>

#include <utils/Logger.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/half.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <functional>
#include <mutex>
#include <utility>

#include <stddef.h>
#include <stdint.h>

using namespace utils;
using namespace filament::math;

namespace filament::backend {

/**
 * DriverBase 构造函数
 * 
 * 初始化 DriverBase，如果启用多线程，创建服务线程用于处理回调。
 * 服务线程负责在后台执行异步回调，避免阻塞渲染线程。
 */
DriverBase::DriverBase() noexcept {
    if constexpr (UTILS_HAS_THREADING) {
        // 创建服务线程，用于处理用户回调
        mServiceThread = std::thread([this]() {
            do {
                auto& serviceThreadCondition = mServiceThreadCondition;
                auto& serviceThreadCallbackQueue = mServiceThreadCallbackQueue;

                // 等待有回调需要处理
                std::unique_lock<std::mutex> lock(mServiceThreadLock);
                while (serviceThreadCallbackQueue.empty() && !mExitRequested) {
                    serviceThreadCondition.wait(lock);  // 等待条件变量通知
                }
                if (mExitRequested) {
                    break;  // 退出标志已设置，退出循环
                }
                // 将回调队列移动到临时向量（避免长时间持有锁）
                auto callbacks(std::move(serviceThreadCallbackQueue));
                lock.unlock();  // 释放锁，确保回调执行时不持有锁
                // 执行所有回调（此时不持有锁，避免死锁）
                for (auto[handler, callback, user]: callbacks) {
                    handler->post(user, callback);  // 通过 handler 执行回调
                }
            } while (true);
        });
    }
}

/**
 * DriverBase 析构函数
 * 
 * 清理所有回调并停止服务线程。
 * 确保所有回调都已执行完毕，服务线程已退出。
 */
DriverBase::~DriverBase() noexcept {
    // 确保所有回调都已处理
    assert_invariant(mCallbacks.empty());
    assert_invariant(mServiceThreadCallbackQueue.empty());
    if constexpr (UTILS_HAS_THREADING) {
        // 停止服务线程
        std::unique_lock<std::mutex> lock(mServiceThreadLock);
        mExitRequested = true;  // 设置退出标志
        mServiceThreadCondition.notify_one();  // 唤醒服务线程
        lock.unlock();
        mServiceThread.join();  // 等待服务线程退出
    }
}

// ------------------------------------------------------------------------------------------------


class DriverBase::CallbackDataDetails : public DriverBase::CallbackData {
    UTILS_UNUSED DriverBase* mAllocator;
public:
    explicit CallbackDataDetails(DriverBase* allocator) : mAllocator(allocator) {}
};

/**
 * 分配 CallbackData 对象
 * 
 * TODO: 使用对象池优化性能
 * 
 * @param allocator DriverBase 实例（当前未使用）
 * @return CallbackData 指针
 */
DriverBase::CallbackData* DriverBase::CallbackData::obtain(DriverBase* allocator) {
    // todo: use a pool
    return new CallbackDataDetails(allocator);
}

/**
 * 释放 CallbackData 对象
 * 
 * TODO: 使用对象池优化性能
 * 
 * @param data 要释放的 CallbackData 指针
 */
void DriverBase::CallbackData::release(CallbackData* data) {
    // todo: use a pool
    delete static_cast<CallbackDataDetails*>(data);
}

/**
 * 调度回调（非模板版本）
 * 
 * 将回调添加到队列中，等待执行。
 * 
 * 执行策略：
 * - 如果有 handler 且启用多线程：添加到服务线程队列，由服务线程执行
 * - 否则：添加到主线程队列，由 purge() 在主线程执行
 * 
 * @param handler 回调处理器（可以为 nullptr）
 * @param user 用户数据指针
 * @param callback 回调函数指针
 */
void DriverBase::scheduleCallback(CallbackHandler* handler, void* user, CallbackHandler::Callback callback) {
    if (handler && UTILS_HAS_THREADING) {
        // 多线程模式：添加到服务线程队列
        std::lock_guard<std::mutex> const lock(mServiceThreadLock);
        mServiceThreadCallbackQueue.emplace_back(handler, callback, user);
        mServiceThreadCondition.notify_one();  // 唤醒服务线程
    } else {
        // 单线程模式：添加到主线程队列
        std::lock_guard<std::mutex> const lock(mPurgeLock);
        mCallbacks.emplace_back(user, callback);
    }
}

/**
 * 清理回调队列
 * 
 * 在主线程调用，执行所有待处理的回调。
 * 这是 Driver::purge() 的最终实现。
 * 
 * 实现细节：
 * - 使用 swap 快速清空队列，避免长时间持有锁
 * - 在锁外执行回调，避免死锁
 */
void DriverBase::purge() noexcept {
    decltype(mCallbacks) callbacks;
    std::unique_lock<std::mutex> lock(mPurgeLock);
    std::swap(callbacks, mCallbacks);  // 快速交换，清空队列
    lock.unlock(); // 不要删除这行！确保回调执行时不持有锁
    // 执行所有回调（此时不持有锁）
    for (auto& item : callbacks) {
        item.second(item.first);  // 调用回调函数
    }
}

// ------------------------------------------------------------------------------------------------

/**
 * 调度缓冲区销毁（慢路径）
 * 
 * 将缓冲区移动到回调中，在回调执行时自动销毁。
 * 这样可以确保缓冲区在使用完毕后才被销毁。
 * 
 * @param buffer 要销毁的缓冲区描述符（会被移动）
 */
void DriverBase::scheduleDestroySlow(BufferDescriptor&& buffer) {
    auto const handler = buffer.getHandler();
    // 将 buffer 移动到 lambda 中，当 lambda 执行时 buffer 会被析构
    // 此时会调用 buffer 的回调（如果有）
    scheduleCallback(handler, [buffer = std::move(buffer)]() {
        // 用户回调在 BufferDescriptor 析构时被调用
    });
}

/**
 * 调度图像释放
 * 
 * 当外部图像不再需要时调用此方法释放。
 * 
 * 注意：这个方法在异步驱动方法中调用（在渲染线程），
 * 但 purge() 在主线程调用。通常每帧调用 0 或 1 次。
 * 
 * @param image 要释放的获取图像
 */
void DriverBase::scheduleRelease(AcquiredImage const& image) {
    // 捕获所有必要的数据，在回调中执行释放
    scheduleCallback(image.handler, [callback = image.callback, image = image.image, userData = image.userData]() {
        callback(image, userData);  // 调用平台特定的释放回调
    });
}

/**
 * 调试：命令开始标记
 * 
 * 在命令执行前调用，用于标记命令执行的开始。
 * 支持日志输出和 Systrace 标记。
 * 
 * @param cmds 命令流指针（用于异步命令的 Systrace 标记）
 * @param synchronous 是否为同步调用
 * @param methodName 方法名称（用于日志和调试）
 */
void DriverBase::debugCommandBegin(CommandStream* cmds, bool synchronous, const char* methodName) noexcept {
    if constexpr (bool(FILAMENT_DEBUG_COMMANDS > FILAMENT_DEBUG_COMMANDS_NONE)) {
        // 日志输出
        if constexpr (bool(FILAMENT_DEBUG_COMMANDS & FILAMENT_DEBUG_COMMANDS_LOG)) {
            DLOG(INFO) << methodName;
        }
        // Systrace 标记
        if constexpr (bool(FILAMENT_DEBUG_COMMANDS & FILAMENT_DEBUG_COMMANDS_SYSTRACE)) {
            FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
            FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, methodName);

            if (!synchronous) {
                // 异步命令：在命令流中插入 Systrace 标记
                cmds->queueCommand([=]() {
                    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
                    FILAMENT_TRACING_NAME_BEGIN(FILAMENT_TRACING_CATEGORY_FILAMENT, methodName);
                });
            }
        }
    }
}

/**
 * 调试：命令结束标记
 * 
 * 在命令执行后调用，与 debugCommandBegin 配对使用。
 * 
 * @param cmds 命令流指针（用于异步命令的 Systrace 标记）
 * @param synchronous 是否为同步调用
 * @param methodName 方法名称（用于日志和调试）
 */
void DriverBase::debugCommandEnd(CommandStream* cmds, bool synchronous,
        const char* methodName) noexcept {
    if constexpr (bool(FILAMENT_DEBUG_COMMANDS > FILAMENT_DEBUG_COMMANDS_NONE)) {
        if constexpr (bool(FILAMENT_DEBUG_COMMANDS & FILAMENT_DEBUG_COMMANDS_SYSTRACE)) {
            if (!synchronous) {
                // 异步命令：在命令流中插入 Systrace 结束标记
                cmds->queueCommand([]() {
                    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
                    FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);
                });
            }
            // 同步命令：直接标记结束
            FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);
            FILAMENT_TRACING_NAME_END(FILAMENT_TRACING_CATEGORY_FILAMENT);
        }
    }
}

/**
 * 获取元素类型的大小（字节数）
 * 
 * 根据元素类型返回对应的字节数。
 * 用于计算顶点缓冲区、索引缓冲区等的大小。
 * 
 * @param type 元素类型（BYTE、FLOAT、HALF 等）
 * @return 元素类型的大小（字节）
 */
size_t Driver::getElementTypeSize(ElementType type) noexcept {
    switch (type) {
        case ElementType::BYTE:     return sizeof(int8_t);
        case ElementType::BYTE2:    return sizeof(byte2);
        case ElementType::BYTE3:    return sizeof(byte3);
        case ElementType::BYTE4:    return sizeof(byte4);
        case ElementType::UBYTE:    return sizeof(uint8_t);
        case ElementType::UBYTE2:   return sizeof(ubyte2);
        case ElementType::UBYTE3:   return sizeof(ubyte3);
        case ElementType::UBYTE4:   return sizeof(ubyte4);
        case ElementType::SHORT:    return sizeof(int16_t);
        case ElementType::SHORT2:   return sizeof(short2);
        case ElementType::SHORT3:   return sizeof(short3);
        case ElementType::SHORT4:   return sizeof(short4);
        case ElementType::USHORT:   return sizeof(uint16_t);
        case ElementType::USHORT2:  return sizeof(ushort2);
        case ElementType::USHORT3:  return sizeof(ushort3);
        case ElementType::USHORT4:  return sizeof(ushort4);
        case ElementType::INT:      return sizeof(int32_t);
        case ElementType::UINT:     return sizeof(uint32_t);
        case ElementType::FLOAT:    return sizeof(float);
        case ElementType::FLOAT2:   return sizeof(float2);
        case ElementType::FLOAT3:   return sizeof(float3);
        case ElementType::FLOAT4:   return sizeof(float4);
        case ElementType::HALF:     return sizeof(half);
        case ElementType::HALF2:    return sizeof(half2);
        case ElementType::HALF3:    return sizeof(half3);
        case ElementType::HALF4:    return sizeof(half4);
    }
}

// ------------------------------------------------------------------------------------------------

/**
 * Driver 析构函数
 * 
 * 默认实现，具体后端可以重写以清理资源。
 */
Driver::~Driver() noexcept = default;

/**
 * 执行一批驱动命令（默认实现）
 * 
 * 默认实现直接调用函数，不做任何包装。
 * 具体后端可以重写以添加调试标记、性能分析等。
 * 
 * @param fn 要执行的函数，包含一批 Driver 命令
 */
void Driver::execute(std::function<void(void)> const& fn) {
    fn();
}

} // namespace filament::backend
