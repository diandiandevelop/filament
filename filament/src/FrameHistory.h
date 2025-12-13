/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef TNT_FILAMENT_FRAMEHISTORY_H
#define TNT_FILAMENT_FRAMEHISTORY_H

#include <fg/FrameGraphId.h>
#include <fg/FrameGraphTexture.h>

#include <math/mat4.h>
#include <math/vec2.h>

#include <stdint.h>

namespace filament {

/**
 * 帧历史条目
 * 
 * 这是存储帧的所有历史的地方。
 * 当在此处添加内容时，请更新：
 *      FView::commitFrameHistory()
 */
struct FrameHistoryEntry {
    /**
     * 时间抗锯齿（TAA）数据
     */
    struct TemporalAA{
        FrameGraphTexture color;  // 颜色纹理
        FrameGraphTexture::Descriptor desc;  // 纹理描述符
        math::mat4 projection;  // 世界空间到裁剪空间的投影矩阵
        math::float2 jitter{};  // 抖动（用于 Halton 序列）
        uint32_t frameId = 0;  // 帧 ID（用于 Halton 序列）
    } taa;
    
    /**
     * 屏幕空间反射（SSR）数据
     */
    struct {
        FrameGraphTexture color;  // 颜色纹理
        FrameGraphTexture::Descriptor desc;  // 纹理描述符
        math::mat4 projection;  // 投影矩阵
    } ssr;
};

/**
 * 帧历史模板类
 * 
 * 这是一个非常简单的 FIFO（先进先出）队列，用于存储先前帧的历史。
 * 
 * @tparam T 条目类型
 * @tparam SIZE 历史大小
 */
template<typename T, size_t SIZE>
class TFrameHistory {
public:
    /**
     * 历史大小
     * 
     * @return 历史容器的大小
     */
    constexpr size_t size() const noexcept { return mContainer.size(); }

    /**
     * 获取最新的帧历史条目
     */
    T const& front() const noexcept { return mContainer.front(); }
    T& front() noexcept { return mContainer.front(); }

    /**
     * 获取最旧的帧历史条目
     */
    T const& back() const noexcept { return mContainer.back(); }
    T& back() noexcept { return mContainer.back(); }

    /**
     * 按索引访问
     */
    T const& operator[](size_t n) const noexcept { return mContainer[n]; }
    T& operator[](size_t n) noexcept { return mContainer[n]; }

    /**
     * 获取当前帧信息
     * 
     * 这是存储当前帧信息的地方。
     */
    T& getCurrent() noexcept {
        return mCurrentEntry;
    }

    const T& getCurrent() const noexcept {
        return mCurrentEntry;
    }

    /**
     * 获取前一帧信息
     */
    T& getPrevious() noexcept {
        return mContainer[0];
    }

    const T& getPrevious() const noexcept {
        return mContainer[0];
    }

    /**
     * 提交当前帧
     * 
     * 将当前帧信息推入 FIFO，有效地销毁最旧的状态。
     * 
     * 实现细节：
     * 1. 如果历史大小 > 1，将所有条目向后移动（最旧的被覆盖）
     * 2. 将当前条目移动到历史容器的前端
     * 3. 重置当前条目为空
     * 
     * 注意：只有结构被销毁，存储在其中的句柄可能需要在调用此函数之前销毁。
     */
    void commit() noexcept {
        auto& container = mContainer;  // 获取历史容器引用
        if (SIZE > 1u) {  // 如果历史大小 > 1
            // 将所有条目向后移动，最旧的条目被覆盖
            std::move_backward(container.begin(), container.end() - 1, container.end());
        }
        container.front() = std::move(mCurrentEntry);  // 将当前条目移动到历史前端
        mCurrentEntry = {};  // 重置当前条目为空
    }

private:
    T mCurrentEntry{};  // 当前帧条目
    std::array<T, SIZE> mContainer;  // 历史容器（FIFO）
};

/**
 * 帧历史类型别名
 * 
 * 使用大小为 1 的帧历史（只保留前一帧）。
 */
using FrameHistory = TFrameHistory<FrameHistoryEntry, 1u>;

} // namespace filament

#endif // TNT_FILAMENT_FRAMEHISTORY_H
