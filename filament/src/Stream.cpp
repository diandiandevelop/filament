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

namespace filament {

using namespace backend;

/**
 * 获取流类型
 * 
 * @return 流类型枚举值
 */
StreamType Stream::getStreamType() const noexcept {
    return downcast(this)->getStreamType();
}

/**
 * 设置获取的图像（无处理器版本）
 * 
 * @param image 图像指针
 * @param callback 回调函数
 * @param userdata 用户数据指针
 * @param transform 变换矩阵
 */
void Stream::setAcquiredImage(void* image, Callback const callback, void* userdata, math::mat3f const& transform) noexcept {
    downcast(this)->setAcquiredImage(image, callback, userdata, transform);
}

/**
 * 设置获取的图像（带处理器版本）
 * 
 * @param image 图像指针
 * @param handler 回调处理器
 * @param callback 回调函数
 * @param userdata 用户数据指针
 * @param transform 变换矩阵
 */
void Stream::setAcquiredImage(void* image,
        CallbackHandler* handler, Callback const callback, void* userdata, math::mat3f const& transform) noexcept {
    downcast(this)->setAcquiredImage(image, handler, callback, userdata, transform);
}

/**
 * 设置流尺寸
 * 
 * @param width 宽度
 * @param height 高度
 */
void Stream::setDimensions(uint32_t const width, uint32_t const height) noexcept {
    downcast(this)->setDimensions(width, height);
}

/**
 * 获取时间戳
 * 
 * @return 时间戳（纳秒）
 */
int64_t Stream::getTimestamp() const noexcept {
    return downcast(this)->getTimestamp();
}

} // namespace filament
