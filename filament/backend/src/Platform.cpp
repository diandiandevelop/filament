/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <backend/Platform.h>

#include <utils/compiler.h>
#include <utils/ostream.h>

#include <atomic>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament::backend {

/**
 * 增加外部图像引用计数
 * 
 * 使用 relaxed 内存序增加引用计数，因为增加引用计数不需要同步。
 * 
 * @param p 外部图像指针
 */
void Platform::ExternalImageHandle::incref(ExternalImage* p) noexcept {
    if (p) {
        // 增加引用计数不需要获取或释放任何内容
        p->mRefCount.fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * 减少外部图像引用计数
 * 
 * 使用 release 内存序减少引用计数，确保之前的写入对其他线程可见。
 * 如果引用计数降为零，使用 acquire 内存屏障确保看到其他线程的所有写入，然后删除对象。
 * 
 * @param p 外部图像指针
 * 
 * 内存序说明：
 * - fetch_sub 使用 memory_order_release：确保之前的写入对其他线程可见
 * - 如果计数降为零：使用 memory_order_acquire 屏障，确保看到其他线程的所有写入
 */
void Platform::ExternalImageHandle::decref(ExternalImage* p) noexcept {
    if (p) {
        // 当减少引用计数时，除非达到零，否则不需要获取数据；
        // 但我们需要释放所有先前的写入，以便它们对实际删除对象的线程可见。
        if (p->mRefCount.fetch_sub(1, std::memory_order_release) == 1) {
            // 如果达到零，我们即将删除对象，需要获取所有先前的写入
            // 从其他线程（即：其他线程在 decref() 之前的内存需要现在可见）。
            std::atomic_thread_fence(std::memory_order_acquire);
            delete p;
        }
    }
}

/**
 * ExternalImageHandle 默认构造函数
 * 
 * 创建一个空的外部图像句柄。
 */
Platform::ExternalImageHandle::ExternalImageHandle() noexcept = default;

/**
 * ExternalImageHandle 析构函数
 * 
 * 减少目标图像的引用计数。
 */
Platform::ExternalImageHandle::~ExternalImageHandle() noexcept {
    decref(mTarget);
}

/**
 * ExternalImageHandle 构造函数（从指针）
 * 
 * 从外部图像指针创建句柄，并增加引用计数。
 * 
 * @param p 外部图像指针
 */
Platform::ExternalImageHandle::ExternalImageHandle(ExternalImage* p) noexcept
        : mTarget(p) {
    incref(mTarget);
}

/**
 * ExternalImageHandle 拷贝构造函数
 * 
 * 从另一个句柄创建句柄，增加引用计数。
 * 
 * @param rhs 源句柄
 */
Platform::ExternalImageHandle::ExternalImageHandle(ExternalImageHandle const& rhs) noexcept
        : mTarget(rhs.mTarget) {
    incref(mTarget);
}

/**
 * ExternalImageHandle 移动构造函数
 * 
 * 从另一个句柄移动构造，不增加引用计数。
 * 
 * @param rhs 源句柄（会被清空）
 */
Platform::ExternalImageHandle::ExternalImageHandle(ExternalImageHandle&& rhs) noexcept
        : mTarget(rhs.mTarget) {
    rhs.mTarget = nullptr;
}

/**
 * ExternalImageHandle 拷贝赋值操作符
 * 
 * 从另一个句柄赋值，正确处理引用计数。
 * 
 * @param rhs 源句柄
 * @return 当前句柄引用
 */
Platform::ExternalImageHandle& Platform::ExternalImageHandle::operator=(
        ExternalImageHandle const& rhs) noexcept {
    if (UTILS_LIKELY(this != &rhs)) {
        incref(rhs.mTarget);  // 先增加新目标的引用计数
        decref(mTarget);      // 再减少旧目标的引用计数
        mTarget = rhs.mTarget;
    }
    return *this;
}

/**
 * ExternalImageHandle 移动赋值操作符
 * 
 * 从另一个句柄移动赋值，不增加引用计数。
 * 
 * @param rhs 源句柄（会被清空）
 * @return 当前句柄引用
 */
Platform::ExternalImageHandle& Platform::ExternalImageHandle::operator=(
        ExternalImageHandle&& rhs) noexcept {
    if (UTILS_LIKELY(this != &rhs)) {
        decref(mTarget);
        mTarget = rhs.mTarget;
        rhs.mTarget = nullptr;
    }
    return *this;
}

/**
 * 清空句柄
 * 
 * 减少引用计数并将句柄设置为空。
 */
void Platform::ExternalImageHandle::clear() noexcept {
    decref(mTarget);
    mTarget = nullptr;
}

/**
 * 重置句柄
 * 
 * 将句柄重置为新的外部图像指针。
 * 
 * @param p 新的外部图像指针
 */
void Platform::ExternalImageHandle::reset(ExternalImage* p) noexcept {
    incref(p);        // 先增加新目标的引用计数
    decref(mTarget);  // 再减少旧目标的引用计数
    mTarget = p;
}

/**
 * ExternalImageHandle 输出流操作符
 * 
 * 将外部图像句柄输出到流中。
 * 
 * @param out 输出流
 * @param handle 外部图像句柄
 * @return 输出流引用
 */
utils::io::ostream& operator<<(utils::io::ostream& out,
        Platform::ExternalImageHandle const& handle) {
    out << "ExternalImageHandle{" << handle.mTarget << "}";
    return out;
}

// --------------------------------------------------------------------------------------------------------------------

/**
 * ExternalImage 析构函数
 * 
 * 默认实现，具体平台可以重写。
 */
Platform::ExternalImage::~ExternalImage() noexcept = default;

// --------------------------------------------------------------------------------------------------------------------

/**
 * Platform 默认构造函数
 * 
 * 初始化平台对象。
 */
Platform::Platform() noexcept = default;

/**
 * Platform 析构函数
 * 
 * 这在此翻译单元中生成虚函数表。
 */
Platform::~Platform() noexcept = default;

/**
 * 处理事件
 * 
 * 默认实现返回 false（不支持事件处理）。
 * 具体平台可以重写以处理窗口事件等。
 * 
 * @return 如果处理了事件返回 true，否则返回 false
 */
bool Platform::pumpEvents() noexcept {
    return false;
}

/**
 * 检查是否支持合成器时序
 * 
 * 默认实现返回 false（不支持）。
 * 具体平台可以重写以支持合成器时序查询（如 Android）。
 * 
 * @return 如果支持返回 true，否则返回 false
 */
bool Platform::isCompositorTimingSupported() const noexcept {
    return false;
}

/**
 * 查询合成器时序
 * 
 * 默认实现返回 false（不支持）。
 * 
 * @param swapChain 交换链指针
 * @param timing 输出参数，合成器时序信息
 * @return 如果成功返回 true，否则返回 false
 */
bool Platform::queryCompositorTiming(SwapChain const*, CompositorTiming*) const noexcept {
    return false;
}

/**
 * 设置呈现帧 ID
 * 
 * 默认实现返回 false（不支持）。
 * 
 * @param swapChain 交换链指针
 * @param frameId 帧 ID
 * @return 如果成功返回 true，否则返回 false
 */
bool Platform::setPresentFrameId(SwapChain const*, uint64_t) noexcept {
    return false;
}

/**
 * 查询帧时间戳
 * 
 * 默认实现返回 false（不支持）。
 * 
 * @param swapChain 交换链指针
 * @param frameId 帧 ID
 * @param timestamps 输出参数，帧时间戳信息
 * @return 如果成功返回 true，否则返回 false
 */
bool Platform::queryFrameTimestamps(SwapChain const*, uint64_t, FrameTimestamps*) const noexcept {
    return false;
}

/**
 * 设置 Blob 函数
 * 
 * 设置用于存储和检索 Blob（二进制大对象）的回调函数。
 * 通常用于着色器程序二进制缓存。
 * 
 * @param insertBlob 插入 Blob 的函数（存储）
 * @param retrieveBlob 检索 Blob 的函数（加载）
 */
void Platform::setBlobFunc(InsertBlobFunc&& insertBlob, RetrieveBlobFunc&& retrieveBlob) noexcept {
    std::lock_guard<decltype(mMutex)> lock(mMutex);
    mInsertBlob = std::make_shared<InsertBlobFunc>(std::move(insertBlob));
    mRetrieveBlob = std::make_shared<RetrieveBlobFunc>(std::move(retrieveBlob));
}

/**
 * 检查是否有插入 Blob 函数
 * 
 * @return 如果有插入 Blob 函数返回 true，否则返回 false
 */
bool Platform::hasInsertBlobFunc() const noexcept {
    std::lock_guard<decltype(mMutex)> lock(mMutex);
    return mInsertBlob && bool(*mInsertBlob);
}

/**
 * 检查是否有检索 Blob 函数
 * 
 * @return 如果有检索 Blob 函数返回 true，否则返回 false
 */
bool Platform::hasRetrieveBlobFunc() const noexcept {
    std::lock_guard<decltype(mMutex)> lock(mMutex);
    return mRetrieveBlob && bool(*mRetrieveBlob);
}

/**
 * 插入 Blob
 * 
 * 将 Blob 存储到缓存中。
 * 
 * @param key Blob 键指针
 * @param keySize 键大小（字节）
 * @param value Blob 值指针
 * @param valueSize 值大小（字节）
 * 
 * 实现细节：
 * - 使用 shared_ptr 在锁外调用回调，避免长时间持有锁
 */
void Platform::insertBlob(void const* key, size_t keySize, void const* value, size_t valueSize) {
    std::shared_ptr<InsertBlobFunc> callback;
    {
        std::unique_lock<decltype(mMutex)> lock(mMutex);
        callback = mInsertBlob;
    }
    if (callback) {
        (*callback)(key, keySize, value, valueSize);
    }
}

/**
 * 检索 Blob
 * 
 * 从缓存中检索 Blob。
 * 
 * @param key Blob 键指针
 * @param keySize 键大小（字节）
 * @param value 输出缓冲区指针
 * @param valueSize 输出缓冲区大小（字节）
 * @return 实际读取的字节数，如果未找到返回 0
 * 
 * 实现细节：
 * - 使用 shared_ptr 在锁外调用回调，避免长时间持有锁
 */
size_t Platform::retrieveBlob(void const* key, size_t keySize, void* value, size_t valueSize) {
    std::shared_ptr<RetrieveBlobFunc> callback;
    {
        std::unique_lock<decltype(mMutex)> lock(mMutex);
        callback = mRetrieveBlob;
    }
    if (callback) {
        return (*callback)(key, keySize, value, valueSize);
    }
    return 0;
}

/**
 * 设置调试更新统计函数
 * 
 * 设置用于更新调试统计信息的回调函数。
 * 
 * @param debugUpdateStat 调试更新统计函数
 */
void Platform::setDebugUpdateStatFunc(DebugUpdateStatFunc&& debugUpdateStat) noexcept {
    std::lock_guard<decltype(mMutex)> lock(mMutex);
    mDebugUpdateStat = std::make_shared<DebugUpdateStatFunc>(std::move(debugUpdateStat));
}

/**
 * 检查是否有调试更新统计函数
 * 
 * @return 如果有调试更新统计函数返回 true，否则返回 false
 */
bool Platform::hasDebugUpdateStatFunc() const noexcept {
    std::lock_guard<decltype(mMutex)> lock(mMutex);
    return mDebugUpdateStat && bool(*mDebugUpdateStat);
}

/**
 * 更新调试统计（整数值）
 * 
 * 更新调试统计信息，使用整数值。
 * 
 * @param key 统计键
 * @param intValue 整数值
 * 
 * 实现细节：
 * - 使用 shared_ptr 在锁外调用回调，避免长时间持有锁
 */
void Platform::debugUpdateStat(const char* key, uint64_t intValue) {
    std::shared_ptr<DebugUpdateStatFunc> callback;
    {
        std::unique_lock<decltype(mMutex)> lock(mMutex);
        callback = mDebugUpdateStat;
    }
    if (callback) {
        (*callback)(key, intValue, "");
    }
}

/**
 * 更新调试统计（字符串值）
 * 
 * 更新调试统计信息，使用字符串值。
 * 
 * @param key 统计键
 * @param stringValue 字符串值
 * 
 * 实现细节：
 * - 使用 shared_ptr 在锁外调用回调，避免长时间持有锁
 */
void Platform::debugUpdateStat(const char* key, utils::CString stringValue) {
    std::shared_ptr<DebugUpdateStatFunc> callback;
    {
        std::unique_lock<decltype(mMutex)> lock(mMutex);
        callback = mDebugUpdateStat;
    }
    if (callback) {
        (*callback)(key, 0, stringValue);
    }
}

} // namespace filament::backend
