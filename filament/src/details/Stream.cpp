/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "details/Stream.h"

#include "details/Engine.h"
#include "details/Fence.h"

#include "FilamentAPI-impl.h"

#include <backend/PixelBufferDescriptor.h>

#include <utils/CString.h>
#include <utils/StaticString.h>
#include <utils/Panic.h>
#include <filament/Stream.h>

namespace filament {

using namespace backend;

/**
 * 构建器详情结构
 * 
 * 存储流的构建参数。
 */
struct Stream::BuilderDetails {
    void* mStream = nullptr;  // 原生流指针
    uint32_t mWidth = 0;  // 宽度
    uint32_t mHeight = 0;  // 高度
};

/**
 * 构建器类型别名
 */
using BuilderType = Stream;

/**
 * 构建器默认构造函数
 */
BuilderType::Builder::Builder() noexcept = default;

/**
 * 构建器析构函数
 */
BuilderType::Builder::~Builder() noexcept = default;

/**
 * 构建器拷贝构造函数
 */
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;

/**
 * 构建器移动构造函数
 */
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;

/**
 * 构建器拷贝赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;

/**
 * 构建器移动赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;


/**
 * 设置流
 * 
 * 设置原生流指针。
 * 
 * @param stream 原生流指针
 * @return 构建器引用（支持链式调用）
 */
Stream::Builder& Stream::Builder::stream(void* stream) noexcept {
    mImpl->mStream = stream;  // 设置流指针
    return *this;  // 返回自身引用
}

/**
 * 设置宽度
 * 
 * @param width 宽度
 * @return 构建器引用（支持链式调用）
 */
Stream::Builder& Stream::Builder::width(uint32_t const width) noexcept {
    mImpl->mWidth = width;  // 设置宽度
    return *this;  // 返回自身引用
}

/**
 * 设置高度
 * 
 * @param height 高度
 * @return 构建器引用（支持链式调用）
 */
Stream::Builder& Stream::Builder::height(uint32_t const height) noexcept {
    mImpl->mHeight = height;  // 设置高度
    return *this;  // 返回自身引用
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
Stream::Builder& Stream::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 调用名称混入方法
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 名称静态字符串
 * @return 构建器引用（支持链式调用）
 */
Stream::Builder& Stream::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 调用名称混入方法
}

/**
 * 构建流
 * 
 * 根据构建器配置创建流。
 * 
 * @param engine 引擎引用
 * @return 流指针
 */
Stream* Stream::Builder::build(Engine& engine) {
    return downcast(engine).createStream(*this);  // 调用引擎的创建方法
}

// ------------------------------------------------------------------------------------------------

/**
 * 流构造函数
 * 
 * 创建流对象并分配驱动资源。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FStream::FStream(FEngine& engine, const Builder& builder) noexcept
        : mEngine(engine),  // 初始化引擎引用
          mStreamType(builder->mStream ? StreamType::NATIVE : StreamType::ACQUIRED),  // 根据是否有原生流设置类型
          mNativeStream(builder->mStream),  // 初始化原生流指针
          mWidth(builder->mWidth),  // 初始化宽度
          mHeight(builder->mHeight) {  // 初始化高度

    if (mNativeStream) {  // 如果有原生流
        /**
         * 注意：这是一个同步调用。在 Android 上，这会回调到 Java。
         */
        // Note: this is a synchronous call. On Android, this calls back into Java.
        mStreamHandle = engine.getDriverApi().createStreamNative(mNativeStream, builder.getName());  // 创建原生流
    } else {  // 否则
        mStreamHandle = engine.getDriverApi().createStreamAcquired(builder.getName());  // 创建获取流
    }
}

/**
 * 终止流
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FStream::terminate(FEngine& engine) noexcept {
    engine.getDriverApi().destroyStream(mStreamHandle);  // 销毁流
}

/**
 * 设置获取的图像（无回调处理器版本）
 * 
 * 设置从外部源获取的图像。
 * 
 * @param image 图像指针
 * @param callback 回调函数
 * @param userdata 用户数据指针
 * @param transform 变换矩阵
 */
void FStream::setAcquiredImage(void* image,
    Callback const callback, void* userdata, math::mat3f const& transform) noexcept {
    mEngine.getDriverApi().setAcquiredImage(mStreamHandle, image, transform, nullptr, callback, userdata);  // 设置获取的图像
}

/**
 * 设置获取的图像（带回调处理器版本）
 * 
 * 设置从外部源获取的图像，使用指定的回调处理器。
 * 
 * @param image 图像指针
 * @param handler 回调处理器指针
 * @param callback 回调函数
 * @param userdata 用户数据指针
 * @param transform 变换矩阵
 */
void FStream::setAcquiredImage(void* image,
    CallbackHandler* handler, Callback const callback, void* userdata, math::mat3f const& transform) noexcept {
    mEngine.getDriverApi().setAcquiredImage(mStreamHandle, image, transform, handler, callback, userdata);  // 设置获取的图像
}

/**
 * 设置尺寸
 * 
 * 更新流的宽度和高度。
 * 
 * @param width 宽度
 * @param height 高度
 */
void FStream::setDimensions(uint32_t const width, uint32_t const height) noexcept {
    mWidth = width;  // 设置宽度
    mHeight = height;  // 设置高度

    /**
     * 不幸的是，由于此调用是同步的，我们必须确保句柄已经首先创建
     */
    // unfortunately, because this call is synchronous, we must make sure the handle has been
    // created first
    if (UTILS_UNLIKELY(!mStreamHandle)) {  // 如果句柄未创建
        FFence::waitAndDestroy(mEngine.createFence(), Fence::Mode::FLUSH);  // 等待栅栏以确保句柄已创建
    }
    mEngine.getDriverApi().setStreamDimensions(mStreamHandle, mWidth, mHeight);  // 设置流尺寸
}

/**
 * 获取时间戳
 * 
 * 获取当前帧的时间戳。
 * 
 * @return 时间戳（纳秒）
 */
int64_t FStream::getTimestamp() const noexcept {
    FEngine::DriverApi& driver = mEngine.getDriverApi();  // 获取驱动 API
    return driver.getStreamTimestamp(mStreamHandle);  // 获取流时间戳
}

} // namespace filament
