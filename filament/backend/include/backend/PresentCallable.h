/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PRESENTCALLABLE
#define TNT_FILAMENT_BACKEND_PRESENTCALLABLE

#include <utils/compiler.h>

namespace filament::backend {

/**
 * A PresentCallable is a callable object that, when called, schedules a frame for presentation on
 * a SwapChain.
 *
 * Typically, Filament's backend is responsible for scheduling a frame's presentation. However,
 * there are certain cases where the application might want to control when a frame is scheduled for
 * presentation.
 *
 * For example, on iOS, UIKit elements can be synchronized to 3D content by scheduling a present
 * within a CATransaction:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * void myFrameScheduledCallback(PresentCallable presentCallable, void* user) {
 *     [CATransaction begin];
 *     // Update other UI elements...
 *     presentCallable();
 *     [CATransaction commit];
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To obtain a PresentCallable, set a SwapChain::FrameScheduledCallback on a SwapChain with the
 * SwapChain::setFrameScheduledCallback method. The callback is called with a PresentCallable object
 * and optional user data:
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * swapChain->setFrameScheduledCallback(nullptr, myFrameScheduledCallback);
 * if (renderer->beginFrame(swapChain)) {
 *     renderer->render(view);
 *     renderer->endFrame();
 * }
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * @remark The PresentCallable mechanism for user-controlled presentation is only supported by
 * Filament's Metal backend. On other backends, the FrameScheduledCallback is still invoked, but the
 * PresentCallable passed to it is a no-op and calling it has no effect.
 *
 * When using the Metal backend, applications *must* call each PresentCallable they receive. Each
 * PresentCallable represents a frame that is waiting to be presented, and failing to call it
 * will result in a memory leak. To "cancel" the presentation of a frame, pass false to the
 * PresentCallable, which will cancel the presentation of the frame and release associated memory.
 *
 * @see Renderer, SwapChain::setFrameScheduledCallback
 */
/**
 * 可调用的呈现对象
 * 
 * PresentCallable 是一个可调用对象，当被调用时，会在 SwapChain 上调度一帧进行呈现。
 * 
 * 典型用法：
 * - 通常，Filament 的后端负责调度帧的呈现
 * - 但在某些情况下，应用程序可能希望控制何时调度帧的呈现
 * 
 * 使用场景示例（iOS）：
 * - 在 CATransaction 中同步 UIKit 元素和 3D 内容
 * - 确保 UI 更新和帧呈现在同一事务中
 * 
 * 获取 PresentCallable：
 * - 在 SwapChain 上设置 FrameScheduledCallback
 * - 回调在帧准备好呈现时被调用，并传递 PresentCallable 对象
 * 
 * 后端支持：
 * - 用户控制的呈现机制仅在 Metal 后端支持
 * - 其他后端：FrameScheduledCallback 仍会被调用，但 PresentCallable 是空操作
 * 
 * Metal 后端重要提示：
 * - 应用程序必须调用接收到的每个 PresentCallable
 * - 每个 PresentCallable 代表一个等待呈现的帧
 * - 不调用会导致内存泄漏
 * - 要"取消"帧的呈现，传递 false 给 PresentCallable
 */
class UTILS_PUBLIC PresentCallable {
public:

    /**
     * 呈现函数类型
     * 
     * 签名：void(*)(bool presentFrame, void* user)
     * - presentFrame: 如果为 true，呈现帧；如果为 false，取消呈现但释放内存
     * - user: 用户数据指针
     */
    using PresentFn = void(*)(bool presentFrame, void* user);
    
    /**
     * 空操作呈现函数
     * 
     * 用于不支持用户控制呈现的后端。
     * 调用此函数不会有任何效果。
     */
    static void noopPresent(bool, void*) {}

    /**
     * 构造函数
     * 
     * @param fn   呈现函数指针
     * @param user 用户数据指针
     */
    PresentCallable(PresentFn fn, void* user) noexcept;
    
    /**
     * 析构函数
     * 
     * 使用默认实现，不执行任何操作。
     */
    ~PresentCallable() noexcept = default;
    
    /**
     * 拷贝构造函数
     * 
     * 使用默认实现，支持拷贝。
     */
    PresentCallable(const PresentCallable& rhs) = default;
    
    /**
     * 拷贝赋值操作符
     * 
     * 使用默认实现，支持拷贝赋值。
     */
    PresentCallable& operator=(const PresentCallable& rhs) = default;

    /**
     * Call this PresentCallable, scheduling the associated frame for presentation. Pass false for
     * presentFrame to effectively "cancel" the presentation of the frame.
     *
     * @param presentFrame if false, will not present the frame but releases associated memory
     */
    /**
     * 调用操作符：调度关联的帧进行呈现
     * 
     * 调用此 PresentCallable，调度关联的帧进行呈现。
     * 
     * @param presentFrame 如果为 true，呈现帧；如果为 false，取消呈现但释放关联的内存
     * 
     * 实现说明：
     * - 调用内部存储的呈现函数
     * - 传递 presentFrame 参数和用户数据
     * - 在 Metal 后端，这会实际调度帧呈现
     * - 在其他后端，这是空操作
     * 
     * 使用示例：
     * ```cpp
     * PresentCallable callable = ...;
     * callable();           // 呈现帧
     * callable(false);      // 取消呈现但释放内存
     * ```
     */
    void operator()(bool presentFrame = true) noexcept;

private:

    PresentFn mPresentFn;
    void* mUser = nullptr;

};

/**
 * @deprecated, FrameFinishedCallback has been renamed to SwapChain::FrameScheduledCallback.
 */
using FrameFinishedCallback UTILS_DEPRECATED = void(*)(PresentCallable callable, void* user);

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRESENTCALLABLE
