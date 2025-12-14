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

//! \file

#ifndef TNT_FILAMENT_BACKEND_BUFFERDESCRIPTOR_H
#define TNT_FILAMENT_BACKEND_BUFFERDESCRIPTOR_H

#include <utils/compiler.h>

#include <utility>

#include <stddef.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

class CallbackHandler;

/**
 * A CPU memory-buffer descriptor, typically used to transfer data from the CPU to the GPU.
 *
 * A BufferDescriptor owns the memory buffer it references, therefore BufferDescriptor cannot
 * be copied, but can be moved.
 *
 * BufferDescriptor releases ownership of the memory-buffer when it's destroyed.
 */
/**
 * CPU 内存缓冲区描述符
 * 
 * 用于将数据从 CPU 传输到 GPU 的缓冲区描述符。
 * 
 * 特性：
 * - BufferDescriptor 拥有其引用的内存缓冲区
 * - 不支持拷贝，但支持移动语义
 * - 析构时通过回调释放缓冲区所有权
 * 
 * 用途：
 * - 上传顶点数据到 GPU
 * - 上传纹理数据到 GPU
 * - 上传 uniform 数据到 GPU
 * 
 * 生命周期：
 * - 创建时获得缓冲区所有权
 * - 数据上传完成后，通过回调通知可以释放缓冲区
 * - 析构时调用回调释放所有权
 * 
 * 线程安全：
 * - 回调保证在主 Filament 线程上调用
 * - 回调必须是轻量级的
 * - 回调中不能调用 Filament API
 */
class UTILS_PUBLIC BufferDescriptor {
public:
    /**
     * Callback used to destroy the buffer data.
     * Guarantees:
     *      Called on the main filament thread.
     *
     * Limitations:
     *      Must be lightweight.
     *      Must not call filament APIs.
     */
    /**
     * 用于释放缓冲区数据的回调函数类型
     * 
     * 回调函数签名：void(*)(void* buffer, size_t size, void* user)
     * 
     * 参数：
     * - buffer: 要释放的缓冲区指针
     * - size: 缓冲区大小（字节）
     * - user: 用户数据指针（在创建 BufferDescriptor 时提供）
     * 
     * 保证：
     * - 在主 Filament 线程上调用
     * 
     * 限制：
     * - 必须是轻量级的（快速执行）
     * - 不能调用 Filament API
     * 
     * 使用场景：
     * - 释放临时分配的缓冲区
     * - 通知外部系统可以重用缓冲区
     * - 执行自定义清理逻辑
     */
    using Callback = void(*)(void* buffer, size_t size, void* user);

    /**
     * 创建空描述符
     * 
     * 创建一个不引用任何缓冲区的空描述符。
     * 所有成员变量使用默认值（nullptr 或 0）。
     */
    //! creates an empty descriptor
    BufferDescriptor() noexcept = default;

    /**
     * 析构函数
     * 
     * 如果设置了回调函数，则调用回调通知缓冲区不再被 BufferDescriptor 拥有。
     * 
     * 实现步骤：
     * 1. 检查是否有回调函数（mCallback != nullptr）
     * 2. 如果有，调用回调函数，传递缓冲区指针、大小和用户数据
     * 3. 回调函数负责释放或重用缓冲区
     * 
     * 注意：
     * - 回调在析构时调用，此时 BufferDescriptor 即将被销毁
     * - 回调保证在主 Filament 线程上调用
     */
    //! calls the callback to advertise BufferDescriptor no-longer owns the buffer
    ~BufferDescriptor() noexcept {
        if (mCallback) {
            mCallback(buffer, size, mUser);
        }
    }

    /**
     * 禁用拷贝构造
     * 
     * BufferDescriptor 拥有缓冲区所有权，不支持拷贝以避免重复释放。
     */
    BufferDescriptor(const BufferDescriptor& rhs) = delete;
    
    /**
     * 禁用拷贝赋值
     */
    BufferDescriptor& operator=(const BufferDescriptor& rhs) = delete;

    /**
     * 移动构造函数
     * 
     * 转移缓冲区所有权到新对象，原对象变为空状态。
     * 
     * @param rhs 要移动的源对象
     * 
     * 实现步骤：
     * 1. 复制所有成员变量（buffer, size, mCallback, mUser, mHandler）
     * 2. 将源对象的 buffer 和 mCallback 设置为 nullptr
     * 3. 源对象变为空状态，不会在析构时调用回调
     */
    BufferDescriptor(BufferDescriptor&& rhs) noexcept
        : buffer(rhs.buffer), size(rhs.size),
          mCallback(rhs.mCallback), mUser(rhs.mUser), mHandler(rhs.mHandler) {
            rhs.buffer = nullptr;
            rhs.mCallback = nullptr;
    }

    /**
     * 移动赋值操作符
     * 
     * 转移缓冲区所有权，原对象变为空状态。
     * 
     * @param rhs 要移动的源对象
     * @return 当前对象的引用
     * 
     * 实现步骤：
     * 1. 检查自赋值（this != &rhs）
     * 2. 复制所有成员变量
     * 3. 将源对象的 buffer 和 mCallback 设置为 nullptr
     * 4. 返回当前对象的引用
     */
    BufferDescriptor& operator=(BufferDescriptor&& rhs) noexcept {
        if (this != &rhs) {
            buffer = rhs.buffer;
            size = rhs.size;
            mCallback = rhs.mCallback;
            mUser = rhs.mUser;
            mHandler = rhs.mHandler;
            rhs.buffer = nullptr;
            rhs.mCallback = nullptr;
        }
        return *this;
    }

    /**
     * Creates a BufferDescriptor that references a CPU memory-buffer
     * @param buffer    Memory address of the CPU buffer to reference
     * @param size      Size of the CPU buffer in bytes
     * @param callback  A callback used to release the CPU buffer from this BufferDescriptor
     * @param user      An opaque user pointer passed to the callback function when it's called
     */
    /**
     * 构造函数：创建引用 CPU 内存缓冲区的描述符
     * 
     * 创建一个 BufferDescriptor，引用指定的 CPU 内存缓冲区。
     * 
     * @param buffer   要引用的 CPU 缓冲区内存地址
     *                 - 必须是有效的内存地址
     *                 - 缓冲区在回调调用前必须保持有效
     * 
     * @param size     CPU 缓冲区的大小（字节）
     *                 - 必须与实际缓冲区大小匹配
     * 
     * @param callback 用于释放 CPU 缓冲区的回调函数（可选）
     *                 - 如果为 nullptr，则不会调用回调
     *                 - 在 BufferDescriptor 析构时调用
     * 
     * @param user     传递给回调函数的不透明用户指针（可选）
     *                 - 可以是任意用户数据
     *                 - 在回调时原样传递
     * 
     * 实现说明：
     * - 使用 const_cast 移除 const 限定（因为内部需要非 const 指针）
     * - 不复制缓冲区数据，只保存指针
     * - 缓冲区所有权由回调函数管理
     */
    BufferDescriptor(void const* buffer, size_t const size,
            Callback const callback = nullptr, void* user = nullptr) noexcept
                : buffer(const_cast<void*>(buffer)), size(size), mCallback(callback), mUser(user) {
    }

    /**
     * Creates a BufferDescriptor that references a CPU memory-buffer
     * @param buffer    Memory address of the CPU buffer to reference
     * @param size      Size of the CPU buffer in bytes
     * @param handler   A custom handler for the callback
     * @param callback  A callback used to release the CPU buffer from this BufferDescriptor
     * @param user      An opaque user pointer passed to the callback function when it's called
     */
    /**
     * 构造函数：创建引用 CPU 内存缓冲区的描述符（带自定义处理器）
     * 
     * 创建一个 BufferDescriptor，引用指定的 CPU 内存缓冲区，并使用自定义回调处理器。
     * 
     * @param buffer   要引用的 CPU 缓冲区内存地址
     * 
     * @param size     CPU 缓冲区的大小（字节）
     * 
     * @param handler  自定义回调处理器（可选）
     *                 - 如果为 nullptr，使用默认处理器
     *                 - 用于控制回调的执行线程
     * 
     * @param callback 用于释放 CPU 缓冲区的回调函数
     * 
     * @param user     传递给回调函数的不透明用户指针（可选）
     * 
     * 使用场景：
     * - 需要在特定线程上执行回调
     * - 需要与平台的消息系统集成
     * - 需要自定义回调调度逻辑
     */
    BufferDescriptor(void const* buffer, size_t const size,
            CallbackHandler* handler, Callback const callback, void* user = nullptr) noexcept
                : buffer(const_cast<void*>(buffer)), size(size),
                mCallback(callback), mUser(user), mHandler(handler) {
    }

    // --------------------------------------------------------------------------------------------

    /**
     * Helper to create a BufferDescriptor that uses a KNOWN method pointer w/ object passed
     * by pointer as the callback. e.g.:
     *     auto bd = BufferDescriptor::make<Foo, &Foo::method>(buffer, size, foo);
     *
     * @param buffer    Memory address of the CPU buffer to reference
     * @param size      Size of the CPU buffer in bytes
     * @param data      A pointer to the data
     * @param handler   Handler to use to dispatch the callback, or nullptr for the default handler
     * @return          A new BufferDescriptor
     */
    /**
     * 辅助函数：使用已知方法指针创建 BufferDescriptor
     * 
     * 创建一个 BufferDescriptor，使用对象的成员函数作为回调。
     * 
     * 模板参数：
     * - T: 对象类型
     * - method: 成员函数指针，签名必须是 void(T::*)(void const*, size_t)
     * 
     * @param buffer   要引用的 CPU 缓冲区内存地址
     * @param size     CPU 缓冲区的大小（字节）
     * @param data     指向对象的指针，回调时调用其成员函数
     * @param handler  回调处理器（可选），nullptr 表示使用默认处理器
     * @return         新创建的 BufferDescriptor
     * 
     * 实现说明：
     * - 创建一个 lambda 函数作为回调
     * - Lambda 将 user 指针转换为 T* 并调用指定的成员函数
     * - 使用静态转换确保类型安全
     */
    template<typename T, void(T::*method)(void const*, size_t)>
    static BufferDescriptor make(void const* buffer, size_t size, T* data,
            CallbackHandler* handler = nullptr) noexcept {
        return {
                buffer, size,
                handler, [](void* b, size_t s, void* u) {
                    (static_cast<T*>(u)->*method)(b, s);
                }, data
        };
    }

    /**
     * Helper to create a BufferDescriptor that uses a functor as the callback.
     *
     * Caveats:
     *      - DO NOT CALL setCallback() when using this helper.
     *      - This make a heap allocation
     *
     * @param buffer    Memory address of the CPU buffer to reference
     * @param size      Size of the CPU buffer in bytes
     * @param functor   functor of type f(void const* buffer, size_t size)
     * @param handler   Handler to use to dispatch the callback, or nullptr for the default handler
     * @return          a new BufferDescriptor
     */
    /**
     * 辅助函数：使用函数对象（functor）创建 BufferDescriptor
     * 
     * 创建一个 BufferDescriptor，使用函数对象（lambda、函数对象等）作为回调。
     * 
     * 模板参数：
     * - T: 函数对象类型，必须可调用，签名：void operator()(void const* buffer, size_t size)
     * 
     * @param buffer   要引用的 CPU 缓冲区内存地址
     * @param size     CPU 缓冲区的大小（字节）
     * @param functor  函数对象，在回调时调用
     *                 - 可以是 lambda 表达式
     *                 - 可以是函数对象类
     * @param handler  回调处理器（可选），nullptr 表示使用默认处理器
     * @return         新创建的 BufferDescriptor
     * 
     * 注意事项：
     * - 不要在使用此辅助函数后调用 setCallback()
     * - 会在堆上分配内存（用于存储函数对象的副本）
     * - 函数对象在回调执行后自动删除
     * 
     * 使用示例：
     * ```cpp
     * auto bd = BufferDescriptor::make(buffer, size,
     *     [](void const* buf, size_t sz) {
     *         // 释放缓冲区
     *         delete[] static_cast<char const*>(buf);
     *     });
     * ```
     * 
     * 实现说明：
     * - 使用 new 在堆上创建函数对象的副本
     * - 创建 lambda 作为回调，调用函数对象并删除它
     * - 使用完美转发（std::forward）保持函数对象的值类别
     */
    template<typename T>
    static BufferDescriptor make(void const* buffer, size_t size, T&& functor,
            CallbackHandler* handler = nullptr) noexcept {
        return {
                buffer, size,
                handler, [](void* b, size_t s, void* u) {
                    T* const that = static_cast<T*>(u);
                    that->operator()(b, s);
                    delete that;
                },
                new T(std::forward<T>(functor))
        };
    }

    // --------------------------------------------------------------------------------------------

    /**
     * Set or replace the release callback function
     * @param callback  The new callback function
     * @param user      An opaque user pointer passed to the callbeck function when it's called
     */
    /**
     * 设置或替换释放回调函数
     * 
     * 设置新的回调函数和用户数据，使用默认处理器。
     * 
     * @param callback 新的回调函数
     *                 - 如果为 nullptr，则不会调用回调
     * 
     * @param user     传递给回调函数的不透明用户指针（可选）
     * 
     * 实现说明：
     * - 设置回调函数和用户数据
     * - 将处理器设置为 nullptr（使用默认处理器）
     */
    void setCallback(Callback const callback, void* user = nullptr) noexcept {
        this->mCallback = callback;
        this->mUser = user;
        this->mHandler = nullptr;
    }

    /**
     * Set or replace the release callback function
     * @param handler   The Handler to use to dispatch the callback
     * @param callback  The new callback function
     * @param user      An opaque user pointer passed to the callbeck function when it's called
     */
    /**
     * 设置或替换释放回调函数（带自定义处理器）
     * 
     * 设置新的回调函数、用户数据和自定义处理器。
     * 
     * @param handler  用于分发回调的处理器
     *                 - 如果为 nullptr，使用默认处理器
     * 
     * @param callback 新的回调函数
     * 
     * @param user     传递给回调函数的不透明用户指针（可选）
     */
    void setCallback(CallbackHandler* handler, Callback const callback, void* user = nullptr) noexcept {
        mCallback = callback;
        mUser = user;
        mHandler = handler;
    }

    /**
     * 检查是否设置了释放回调
     * 
     * @return 如果设置了回调返回 true，否则返回 false
     */
    //! Returns whether a release callback is set
    bool hasCallback() const noexcept { return mCallback != nullptr; }

    /**
     * 获取当前设置的释放回调函数
     * 
     * @return 当前的回调函数，如果未设置返回 nullptr
     */
    //! Returns the currently set release callback function
    Callback getCallback() const noexcept {
        return mCallback;
    }

    /**
     * 获取回调处理器
     * 
     * @return 回调处理器指针，如果使用默认处理器返回 nullptr
     */
    //! Returns the handler for this callback or nullptr if the default handler is to be used.
    CallbackHandler* getHandler() const noexcept {
        return mHandler;
    }

    /**
     * 获取用户数据指针
     * 
     * @return 关联的用户数据指针
     */
    //! Returns the user opaque pointer associated to this BufferDescriptor
    void* getUser() const noexcept {
        return mUser;
    }

    /**
     * CPU 内存缓冲区虚拟地址
     * 
     * 指向要传输到 GPU 的 CPU 内存缓冲区。
     * nullptr 表示空描述符。
     */
    //! CPU memory-buffer virtual address
    void* buffer = nullptr;

    /**
     * CPU 内存缓冲区大小（字节）
     * 
     * 缓冲区的大小，必须与实际缓冲区大小匹配。
     */
    //! CPU memory-buffer size in bytes
    size_t size = 0;

private:
    // callback when the buffer is consumed.
    Callback mCallback = nullptr;
    void* mUser = nullptr;
    CallbackHandler* mHandler = nullptr;
};

} // namespace filament::backend

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::BufferDescriptor& b);
#endif

#endif // TNT_FILAMENT_BACKEND_BUFFERDESCRIPTOR_H
