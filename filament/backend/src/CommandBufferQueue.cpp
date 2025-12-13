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

#include "private/backend/CommandBufferQueue.h"
#include "private/backend/CircularBuffer.h"
#include "private/backend/CommandStream.h"

#include <private/utils/Tracing.h>

#include <utils/Logger.h>
#include <utils/Mutex.h>
#include <utils/Panic.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <algorithm>
#include <mutex>
#include <iterator>
#include <utility>
#include <vector>

#include <stddef.h>
#include <stdint.h>

using namespace utils;

namespace filament::backend {

/**
 * CommandBufferQueue 构造函数
 * 
 * 创建命令缓冲区队列，初始化循环缓冲区和空闲空间。
 * 
 * @param requiredSize 所需的最小缓冲区大小（字节）
 * @param bufferSize 实际缓冲区大小（字节），必须 >= requiredSize
 * @param paused 是否暂停（初始状态）
 * 
 * 注意：requiredSize 会被对齐到页面大小。
 */
CommandBufferQueue::CommandBufferQueue(size_t requiredSize, size_t bufferSize, bool paused)
        : mRequiredSize((requiredSize + (CircularBuffer::getBlockSize() - 1u)) & ~(CircularBuffer::getBlockSize() -1u)),  // 对齐到页面大小
          mCircularBuffer(std::max(mRequiredSize, bufferSize)),  // 使用较大的值
          mFreeSpace(mCircularBuffer.size()),  // 初始空闲空间等于缓冲区大小
          mPaused(paused) {
    assert_invariant(mCircularBuffer.size() >= mRequiredSize);
}

/**
 * CommandBufferQueue 析构函数
 * 
 * 确保所有命令缓冲区都已执行完毕。
 */
CommandBufferQueue::~CommandBufferQueue() {
    assert_invariant(mCommandBuffersToExecute.empty());
}

/**
 * 请求退出
 * 
 * 设置退出标志并通知等待的线程。
 * 用于优雅地关闭渲染线程。
 */
void CommandBufferQueue::requestExit() {
    std::lock_guard const lock(mLock);
    mExitRequested = EXIT_REQUESTED;
    mCondition.notify_one();
}

/**
 * 检查是否暂停
 * 
 * @return 如果暂停返回 true，否则返回 false
 */
bool CommandBufferQueue::isPaused() const noexcept {
    std::lock_guard const lock(mLock);
    return mPaused;
}

/**
 * 设置暂停状态
 * 
 * @param paused 是否暂停
 * 
 * 如果从暂停状态恢复，通知等待的线程。
 */
void CommandBufferQueue::setPaused(bool paused) {
    std::lock_guard const lock(mLock);
    if (paused) {
        mPaused = true;
    } else {
        mPaused = false;
        mCondition.notify_one();  // 恢复时通知等待的线程
    }
}

/**
 * 检查是否请求退出
 * 
 * @return 如果请求退出返回 true，否则返回 false
 */
bool CommandBufferQueue::isExitRequested() const {
    std::lock_guard const lock(mLock);
    return bool(mExitRequested);
}


/**
 * 刷新命令缓冲区
 * 
 * 将当前缓冲区中的命令提交到执行队列，并等待有足够的空间继续。
 * 
 * 流程：
 * 1. 如果缓冲区为空，直接返回
 * 2. 添加终止命令（NoopCommand）
 * 3. 获取当前缓冲区范围
 * 4. 检查缓冲区溢出
 * 5. 将缓冲区添加到执行队列
 * 6. 如果空闲空间不足，等待直到有足够空间
 * 
 * 注意：
 * - 如果缓冲区溢出，命令已损坏且无法恢复
 * - 如果渲染线程暂停且缓冲区已满，会死锁，因此直接中止
 */
void CommandBufferQueue::flush() {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    CircularBuffer& circularBuffer = mCircularBuffer;
    if (circularBuffer.empty()) {
        return;
    }

    /**
     * 添加终止命令
     * 
     * 总是保证有足够的空间用于 NoopCommand。
     */
    new(circularBuffer.allocate(sizeof(NoopCommand))) NoopCommand(nullptr);

    const size_t requiredSize = mRequiredSize;

    /**
     * 获取当前缓冲区范围
     */
    auto const [begin, end] = circularBuffer.getBuffer();

    assert_invariant(circularBuffer.empty());

    /**
     * 计算当前缓冲区使用的空间
     */
    size_t const used = std::distance(
            static_cast<char const*>(begin), static_cast<char const*>(end));


    std::unique_lock lock(mLock);

    /**
     * 检查缓冲区溢出
     * 
     * 如果使用的空间超过空闲空间，说明缓冲区太小，命令已损坏。
     */
    FILAMENT_CHECK_POSTCONDITION(used <= mFreeSpace) <<
            "Backend CommandStream overflow. Commands are corrupted and unrecoverable.\n"
            "Please increase minCommandBufferSizeMB inside the Config passed to Engine::create.\n"
            "Space used at this time: " << used <<
            " bytes, overflow: " << used - mFreeSpace << " bytes";

    /**
     * 更新空闲空间并添加到执行队列
     */
    mFreeSpace -= used;
    mCommandBuffersToExecute.push_back({ begin, end });
    mCondition.notify_one();  // 通知渲染线程有新的命令

    /**
     * 如果空闲空间不足，等待直到有足够空间
     */
    if (UTILS_UNLIKELY(mFreeSpace < requiredSize)) {

#ifndef NDEBUG
        size_t const totalUsed = circularBuffer.size() - mFreeSpace;
        DLOG(INFO) << "CommandStream used too much space (will block): "
                   << "needed space " << requiredSize << " out of " << mFreeSpace
                   << ", totalUsed=" << totalUsed << ", current=" << used
                   << ", queue size=" << mCommandBuffersToExecute.size() << " buffers";

        mHighWatermark = std::max(mHighWatermark, totalUsed);
#endif

        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "waiting: CircularBuffer::flush()");

        /**
         * 检查是否暂停
         * 
         * 如果渲染线程暂停且缓冲区已满，会死锁，因此直接中止。
         */
        FILAMENT_CHECK_POSTCONDITION(!mPaused) <<
                "CommandStream is full, but since the rendering thread is paused, "
                "the buffer cannot flush and we will deadlock. Instead, abort.";

        /**
         * 等待直到有足够的空间
         * 
         * TODO: 在 macOS 上，需要定期调用 pumpEvents
         */
        mCondition.wait(lock, [this, requiredSize]() -> bool {
            return mFreeSpace >= requiredSize;
        });
    }
}

/**
 * 等待命令
 * 
 * 在渲染线程调用，等待直到有命令可执行或请求退出。
 * 
 * 如果禁用多线程，直接返回待执行的命令缓冲区。
 * 
 * @return 待执行的命令缓冲区向量
 */
std::vector<CommandBufferQueue::Range> CommandBufferQueue::waitForCommands() const {
    if (!UTILS_HAS_THREADING) {
        return std::move(mCommandBuffersToExecute);
    }
    std::unique_lock lock(mLock);
    /**
     * 等待条件：
     * - 有命令可执行（!mCommandBuffersToExecute.empty()）
     * - 且未暂停（!mPaused）
     * - 或请求退出（mExitRequested）
     */
    while ((mCommandBuffersToExecute.empty() || mPaused) && !mExitRequested) {
        mCondition.wait(lock);
    }
    return std::move(mCommandBuffersToExecute);
}

/**
 * 释放缓冲区
 * 
 * 在渲染线程执行完命令后调用，释放缓冲区并更新空闲空间。
 * 
 * @param buffer 要释放的缓冲区范围
 */
void CommandBufferQueue::releaseBuffer(CommandBufferQueue::Range const& buffer) {
    size_t const used = std::distance(
            static_cast<char const*>(buffer.begin), static_cast<char const*>(buffer.end));
    std::lock_guard const lock(mLock);
    mFreeSpace += used;  // 增加空闲空间
    mCondition.notify_one();  // 通知等待的线程（如果有）
}

} // namespace filament::backend
