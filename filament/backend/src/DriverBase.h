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

#ifndef TNT_FILAMENT_DRIVER_DRIVERBASE_H
#define TNT_FILAMENT_DRIVER_DRIVERBASE_H

#include <utils/compiler.h>
#include <utils/CString.h>

#include <backend/Platform.h>

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/CallbackHandler.h>

#include "private/backend/Dispatcher.h"
#include "private/backend/Driver.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <stdint.h>

namespace filament::backend {

struct AcquiredImage;

/*
 * 硬件资源句柄（Hardware Handles）
 * 
 * 这些结构体是后端特定资源的包装器，存储资源的元数据。
 * 实际的后端资源 ID 存储在 Handle<T> 中，这些结构体提供类型安全的访问。
 */

/**
 * 硬件句柄基类
 * 所有硬件资源句柄都继承自此类，用于类型系统识别
 */
struct HwBase {
};

/**
 * 顶点缓冲区信息句柄
 * 描述顶点缓冲区的布局信息
 */
struct HwVertexBufferInfo : public HwBase {
    uint8_t bufferCount{};                // 缓冲区对象数量（最多 16 个）
    uint8_t attributeCount{};             // 顶点属性数量（最多 16 个）
    bool padding[2]{};                    // 填充字节，保证结构体大小
    HwVertexBufferInfo() noexcept = default;
    /**
     * 构造函数
     * @param bufferCount 缓冲区对象数量
     * @param attributeCount 顶点属性数量
     */
    HwVertexBufferInfo(uint8_t bufferCount, uint8_t attributeCount) noexcept
            : bufferCount(bufferCount),
              attributeCount(attributeCount) {
    }
};

/**
 * 顶点缓冲区句柄
 * 存储顶点数据
 */
struct HwVertexBuffer : public HwBase {
    uint32_t vertexCount{};               // 顶点数量
    uint8_t bufferObjectsVersion{0xff};    // 缓冲区对象版本号（用于检测缓冲区更新）
    bool padding[3]{};                    // 填充字节
    HwVertexBuffer() noexcept = default;
    /**
     * 构造函数
     * @param vertextCount 顶点数量
     */
    explicit HwVertexBuffer(uint32_t vertextCount) noexcept
            : vertexCount(vertextCount) {
    }
};

/**
 * 缓冲区对象句柄
 * 通用的 GPU 缓冲区（Uniform Buffer、Storage Buffer 等）
 */
struct HwBufferObject : public HwBase {
    uint32_t byteCount{};  // 缓冲区大小（字节）

    HwBufferObject() noexcept = default;
    /**
     * 构造函数
     * @param byteCount 缓冲区大小（字节）
     */
    explicit HwBufferObject(uint32_t byteCount) noexcept : byteCount(byteCount) {}
};

/**
 * 内存映射缓冲区句柄
 * CPU 可直接访问的 GPU 缓冲区
 */
struct HwMemoryMappedBuffer : public HwBase {
};

/**
 * 索引缓冲区句柄
 * 存储索引数据
 */
struct HwIndexBuffer : public HwBase {
    uint32_t count : 27;      // 索引数量（最多 2^27 = 134,217,728 个索引）
    uint32_t elementSize : 5; // 每个索引的大小（字节）：2（USHORT）或 4（UINT）

    HwIndexBuffer() noexcept : count{}, elementSize{} { }
    /**
     * 构造函数
     * @param elementSize 每个索引的大小（字节），必须是 2 或 4
     * @param indexCount 索引数量
     */
    HwIndexBuffer(uint8_t elementSize, uint32_t indexCount) noexcept :
            count(indexCount), elementSize(elementSize) {
        // elementSize 永远不会 > 16 且永远不会为 0，所以可以用 4 位存储
        assert_invariant(elementSize > 0 && elementSize <= 16);
        assert_invariant(indexCount < (1u << 27));
    }
};

/**
 * 渲染图元句柄
 * 组合顶点缓冲区和索引缓冲区，定义如何绘制几何体
 */
struct HwRenderPrimitive : public HwBase {
    PrimitiveType type = PrimitiveType::TRIANGLES;  // 图元类型（三角形、线条等）
};

/**
 * 着色器程序句柄
 * 编译后的着色器程序
 */
struct HwProgram : public HwBase {
    utils::CString name;  // 程序名称（用于调试）
    explicit HwProgram(utils::CString name) noexcept : name(std::move(name)) { }
    HwProgram() noexcept = default;
};

/**
 * 描述符集布局句柄
 * 描述描述符集的布局（哪些绑定点有哪些资源）
 */
struct HwDescriptorSetLayout : public HwBase {
    HwDescriptorSetLayout() noexcept = default;
};

/**
 * 描述符集句柄
 * 绑定到着色器的资源集合（纹理、Uniform Buffer 等）
 */
struct HwDescriptorSet : public HwBase {
    HwDescriptorSet() noexcept = default;
};

/**
 * 纹理句柄
 * GPU 纹理资源
 */
struct HwTexture : public HwBase {
    uint32_t width{};        // 纹理宽度
    uint32_t height{};       // 纹理高度
    uint32_t depth{};        // 纹理深度（3D 纹理或数组纹理的层数）
    SamplerType target{};    // 采样器类型（2D、3D、CUBEMAP、2D_ARRAY 等）
    uint8_t levels : 4;      // Mipmap 级别数（最多 15 级，对应最大纹理尺寸 32768x32768）
    uint8_t samples : 4;     // 多重采样数（每个像素的采样点数，必须是 2 的幂）
    TextureFormat format{};  // 纹理格式（RGBA8、RGB16F 等）
    uint8_t reserved0 = 0;   // 保留字段
    TextureUsage usage{};    // 使用标志（SAMPLEABLE、COLOR_ATTACHMENT 等）
    uint16_t reserved1 = 0;   // 保留字段
    HwStream* hwStream = nullptr;  // 关联的外部流（用于视频纹理）

    HwTexture() noexcept : levels{}, samples{} {}
    /**
     * 构造函数
     * @param target 采样器类型
     * @param levels Mipmap 级别数
     * @param samples 多重采样数
     * @param width 宽度
     * @param height 高度
     * @param depth 深度
     * @param fmt 纹理格式
     * @param usage 使用标志
     */
    HwTexture(backend::SamplerType target, uint8_t levels, uint8_t samples,
              uint32_t width, uint32_t height, uint32_t depth, TextureFormat fmt, TextureUsage usage) noexcept
            : width(width), height(height), depth(depth),
              target(target), levels(levels), samples(samples), format(fmt), usage(usage) { }
};

/**
 * 渲染目标句柄
 * 渲染输出的目标（帧缓冲区）
 */
struct HwRenderTarget : public HwBase {
    uint32_t width{};   // 渲染目标宽度
    uint32_t height{};  // 渲染目标高度
    HwRenderTarget() noexcept = default;
    /**
     * 构造函数
     * @param w 宽度
     * @param h 高度
     */
    HwRenderTarget(uint32_t w, uint32_t h) : width(w), height(h) { }
};

/**
 * 栅栏句柄
 * GPU-CPU 同步对象，用于等待 GPU 完成操作
 */
struct HwFence : public HwBase {
    Platform::Fence* fence = nullptr;  // 平台特定的栅栏对象
};

/**
 * 同步对象句柄
 * 更通用的同步对象（Vulkan 的 VkSemaphore 等）
 */
struct HwSync : public HwBase {
    Platform::Sync* sync = nullptr;  // 平台特定的同步对象
};

/**
 * 交换链句柄
 * 窗口的渲染表面
 */
struct HwSwapChain : public HwBase {
    Platform::SwapChain* swapChain = nullptr;  // 平台特定的交换链对象
};

/**
 * 流句柄
 * 外部视频流（用于视频纹理）
 */
struct HwStream : public HwBase {
    Platform::Stream* stream = nullptr;  // 平台特定的流对象
    StreamType streamType = StreamType::ACQUIRED;  // 流类型（ACQUIRED 或 NATIVE）
    uint32_t width{};   // 流宽度
    uint32_t height{};  // 流高度

    HwStream() noexcept = default;
    /**
     * 构造函数
     * @param stream 平台特定的流对象
     */
    explicit HwStream(Platform::Stream* stream) noexcept
            : stream(stream), streamType(StreamType::NATIVE) {
    }
};

/**
 * 计时查询句柄
 * GPU 性能计时查询对象
 */
struct HwTimerQuery : public HwBase {
};

/*
 * Driver 实现基类
 * 
 * DriverBase 是所有 Driver 实现的基类，提供所有后端共用的功能：
 * - 回调管理：异步回调的执行和调度
 * - 调试支持：命令调试标记
 * - 资源销毁：带回调的资源销毁
 */
class DriverBase : public Driver {
public:
    DriverBase() noexcept;
    ~DriverBase() noexcept override;

    /**
     * 清理回调队列
     * 
     * 在主线程调用，执行所有待处理的回调。
     * 这是 Driver::purge() 的最终实现。
     */
    void purge() noexcept final;

    /**
     * 回调数据容器
     * 
     * 用于存储回调函数和用户数据。
     * 使用 placement new 在 storage 中存储任意类型的回调函数。
     */
    struct CallbackData {
        CallbackData(CallbackData const &) = delete;
        CallbackData(CallbackData&&) = delete;
        CallbackData& operator=(CallbackData const &) = delete;
        CallbackData& operator=(CallbackData&&) = delete;
        void* storage[8] = {};  // 存储回调函数的缓冲区（64 字节）
        /**
         * 分配 CallbackData 对象
         * @param allocator DriverBase 实例
         * @return CallbackData 指针
         */
        static CallbackData* obtain(DriverBase* allocator);
        /**
         * 释放 CallbackData 对象
         * @param data 要释放的 CallbackData 指针
         */
        static void release(CallbackData* data);
    protected:
        CallbackData() = default;
    };

    /**
     * 调度回调（模板版本）
     * 
     * 接受任意可调用对象（lambda、函数对象等）作为回调。
     * 回调会被存储到 CallbackData 中，并在适当的时机执行。
     * 
     * @tparam T 回调类型（必须是可调用对象）
     * @param handler 回调处理器（可以为 nullptr）
     * @param functor 回调函数对象（会被移动到 storage 中）
     * 
     * 限制：functor 的大小不能超过 64 字节（storage 的大小）
     */
    template<typename T>
    void scheduleCallback(CallbackHandler* handler, T&& functor) {
        CallbackData* data = CallbackData::obtain(this);
        static_assert(sizeof(T) <= sizeof(data->storage), "functor too large");
        // 使用 placement new 在 storage 中构造 functor
        new(data->storage) T(std::forward<T>(functor));
        // 调度回调，回调执行时会调用 functor
        scheduleCallback(handler, data, (CallbackHandler::Callback)[](void* data) {
            CallbackData* details = static_cast<CallbackData*>(data);
            void* user = details->storage;
            T& functor = *static_cast<T*>(user);
            functor();  // 执行回调
            functor.~T();  // 析构 functor
            CallbackData::release(details);  // 释放 CallbackData
        });
    }

    /**
     * 调度回调（非模板版本）
     * 
     * @param handler 回调处理器（可以为 nullptr）
     * @param user 用户数据指针
     * @param callback 回调函数指针
     */
    void scheduleCallback(CallbackHandler* handler, void* user, CallbackHandler::Callback callback);

    // --------------------------------------------------------------------------------------------
    // Privates
    // --------------------------------------------------------------------------------------------

protected:
    class CallbackDataDetails;  // CallbackData 的实现细节类

    /**
     * 调度缓冲区销毁（带回调）
     * 
     * 如果缓冲区有回调，延迟到回调执行时再销毁。
     * 
     * @param buffer 要销毁的缓冲区描述符
     */
    void scheduleDestroy(BufferDescriptor&& buffer) {
        if (buffer.hasCallback()) {
            scheduleDestroySlow(std::move(buffer));
        }
    }

    /**
     * 调度缓冲区销毁（慢路径）
     * 
     * 将缓冲区移动到回调中，在回调执行时自动销毁。
     * 
     * @param buffer 要销毁的缓冲区描述符
     */
    void scheduleDestroySlow(BufferDescriptor&& buffer);

    /**
     * 调度图像释放
     * 
     * 当外部图像不再需要时，调用此方法释放。
     * 
     * @param image 要释放的获取图像
     */
    void scheduleRelease(AcquiredImage const& image);

    /**
     * 调试：命令开始标记
     * 
     * @param cmds 命令流指针
     * @param synchronous 是否为同步调用
     * @param methodName 方法名称
     */
    void debugCommandBegin(CommandStream* cmds, bool synchronous, const char* methodName) noexcept override;
    
    /**
     * 调试：命令结束标记
     * 
     * @param cmds 命令流指针
     * @param synchronous 是否为同步调用
     * @param methodName 方法名称
     */
    void debugCommandEnd(CommandStream* cmds, bool synchronous, const char* methodName) noexcept override;

private:
    std::mutex mPurgeLock;  // purge() 的互斥锁
    std::vector<std::pair<void*, CallbackHandler::Callback>> mCallbacks;  // 主线程回调队列

    std::thread mServiceThread;  // 服务线程（用于执行回调）
    std::mutex mServiceThreadLock;  // 服务线程互斥锁
    std::condition_variable mServiceThreadCondition;  // 服务线程条件变量
    std::vector<std::tuple<CallbackHandler*, CallbackHandler::Callback, void*>> mServiceThreadCallbackQueue;  // 服务线程回调队列
    bool mExitRequested = false;  // 退出标志
};


} // namespace backend::filament

#endif // TNT_FILAMENT_DRIVER_DRIVERBASE_H
