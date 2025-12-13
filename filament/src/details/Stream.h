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

#ifndef TNT_FILAMENT_DETAILS_STREAM_H
#define TNT_FILAMENT_DETAILS_STREAM_H

#include "downcast.h"

#include <filament/Stream.h>

#include <backend/Handle.h>

#include <utils/compiler.h>

#include <math/mat3.h>

namespace filament {

class FEngine;

/**
 * 流实现类
 * 
 * 管理外部图像流（如相机预览流）。
 * 流用于将外部图像源（如相机或视频）集成到渲染管线中。
 */
class FStream : public Stream {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FStream(FEngine& engine, const Builder& builder) noexcept;
    
    /**
     * 终止流
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 获取硬件句柄
     * 
     * @return 流硬件句柄
     */
    backend::Handle<backend::HwStream> getHandle() const noexcept { return mStreamHandle; }

    /**
     * 设置获取的图像（无回调处理器版本）
     * 
     * 设置从外部源获取的图像。
     * 
     * @param image 图像指针
     * @param callback 回调函数
     * @param userdata 用户数据指针
     * @param transform 变换矩阵（用于图像坐标转换）
     */
    void setAcquiredImage(void* image, Callback callback, void* userdata, math::mat3f const& transform) noexcept;
    
    /**
     * 设置获取的图像（带回调处理器版本）
     * 
     * 设置从外部源获取的图像，使用指定的回调处理器。
     * 
     * @param image 图像指针
     * @param handler 回调处理器指针
     * @param callback 回调函数
     * @param userdata 用户数据指针
     * @param transform 变换矩阵（用于图像坐标转换）
     */
    void setAcquiredImage(void* image, backend::CallbackHandler* handler, Callback callback, void* userdata, math::mat3f const& transform) noexcept;

    /**
     * 设置尺寸
     * 
     * 更新流的宽度和高度。
     * 
     * @param width 宽度
     * @param height 高度
     */
    void setDimensions(uint32_t width, uint32_t height) noexcept;

    /**
     * 获取流类型
     * 
     * @return 流类型
     */
    StreamType getStreamType() const noexcept { return mStreamType; }

    /**
     * 获取宽度
     * 
     * @return 宽度（像素）
     */
    uint32_t getWidth() const noexcept { return mWidth; }

    /**
     * 获取高度
     * 
     * @return 高度（像素）
     */
    uint32_t getHeight() const noexcept { return mHeight; }

    /**
     * 获取时间戳
     * 
     * 获取当前帧的时间戳。
     * 
     * @return 时间戳（纳秒）
     */
    int64_t getTimestamp() const noexcept;

private:
    FEngine& mEngine;  // 引擎引用
    const StreamType mStreamType;  // 流类型（常量）
    backend::Handle<backend::HwStream> mStreamHandle;  // 硬件流句柄
    void* mNativeStream = nullptr;  // 原生流指针
    uint32_t mWidth;  // 宽度
    uint32_t mHeight;  // 高度
};

FILAMENT_DOWNCAST(Stream)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_STREAM_H
