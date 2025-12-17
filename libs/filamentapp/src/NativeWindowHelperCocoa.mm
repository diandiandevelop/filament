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

#include <filamentapp/NativeWindowHelper.h>

#include <utils/Panic.h>

#include <Cocoa/Cocoa.h>
#include <QuartzCore/QuartzCore.h>

#include <SDL_syswm.h>

/**
 * 获取SDL窗口的原生窗口句柄实现（macOS平台）
 * 
 * 执行步骤：
 * 1. 初始化SDL系统窗口管理器信息结构
 * 2. 获取SDL窗口的系统窗口管理器信息
 * 3. 从Cocoa信息中提取NSWindow
 * 4. 获取窗口的内容视图（NSView）
 * 5. 返回NSView指针
 * 
 * @param sdlWindow SDL窗口指针
 * @return macOS原生视图（NSView*）
 */
void* getNativeWindow(SDL_Window* sdlWindow) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi))
            << "SDL version unsupported!";
    NSWindow* win = wmi.info.cocoa.window;
    NSView* view = [win contentView];
    return view;
}

/**
 * 准备原生窗口实现（macOS平台）
 * 
 * 执行步骤：
 * 1. 获取SDL窗口的系统窗口管理器信息
 * 2. 从Cocoa信息中提取NSWindow
 * 3. 设置窗口颜色空间为sRGB（确保颜色正确显示）
 * 
 * @param sdlWindow SDL窗口指针
 */
void prepareNativeWindow(SDL_Window* sdlWindow) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi))
            << "SDL version unsupported!";
    NSWindow* win = wmi.info.cocoa.window;
    [win setColorSpace:[NSColorSpace sRGBColorSpace]];
}

/**
 * 为NSView设置CAMetalLayer并返回该层实现
 * 
 * 执行步骤：
 * 1. 启用视图的图层支持
 * 2. 创建CAMetalLayer对象
 * 3. 设置图层边界为视图边界
 * 4. 设置可绘制区域大小为实际像素大小（重要：全屏渲染时可跳过macOS合成器）
 * 5. 设置内容缩放因子（用于高DPI显示，MoltenVK会考虑此值）
 * 6. 设置不透明（重要：全屏模式时可绕过合成器，实现"Direct to Display"）
 * 7. 将图层设置到视图
 * 8. 返回CAMetalLayer指针
 * 
 * @param nativeView 原生视图（NSView*）
 * @return CAMetalLayer指针
 */
void* setUpMetalLayer(void* nativeView) {
    NSView* view = (NSView*) nativeView;
    [view setWantsLayer:YES];
    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    metalLayer.bounds = view.bounds;

    // It's important to set the drawableSize to the actual backing pixels. When rendering
    // full-screen, we can skip the macOS compositor if the size matches the display size.
    // 设置可绘制区域大小为实际像素大小很重要。全屏渲染时，如果大小匹配显示大小，可以跳过macOS合成器。
    metalLayer.drawableSize = [view convertSizeToBacking:view.bounds.size];

    // In its implementation of vkGetPhysicalDeviceSurfaceCapabilitiesKHR, MoltenVK takes into
    // consideration both the size (in points) of the bounds, and the contentsScale of the
    // CAMetalLayer from which the Vulkan surface was created.
    // See also https://github.com/KhronosGroup/MoltenVK/issues/428
    // 在vkGetPhysicalDeviceSurfaceCapabilitiesKHR的实现中，MoltenVK会考虑边界的尺寸（以点为单位）
    // 以及创建Vulkan表面的CAMetalLayer的contentsScale。
    metalLayer.contentsScale = view.window.backingScaleFactor;

    // This is set to NO by default, but is also important to ensure we can bypass the compositor
    // in full-screen mode
    // See "Direct to Display" http://metalkit.org/2017/06/30/introducing-metal-2.html.
    // 默认设置为NO，但也很重要，确保全屏模式时可以绕过合成器
    // 参见"Direct to Display" http://metalkit.org/2017/06/30/introducing-metal-2.html
    metalLayer.opaque = YES;

    [view setLayer:metalLayer];

    return metalLayer;
}

/**
 * 调整CAMetalLayer的可绘制区域大小以匹配新视图大小实现
 * 
 * 执行步骤：
 * 1. 获取视图和现有的CAMetalLayer
 * 2. 计算新的可绘制区域大小（考虑高DPI缩放）
 * 3. 更新图层的可绘制区域大小
 * 4. 更新内容缩放因子
 * 5. 返回CAMetalLayer指针
 * 
 * @param nativeView 原生视图（NSView*）
 * @return CAMetalLayer指针
 */
void* resizeMetalLayer(void* nativeView) {
    NSView* view = (NSView*) nativeView;
    CAMetalLayer* metalLayer = (CAMetalLayer*) view.layer;
    CGSize viewSize = view.bounds.size;
    // 将视图大小转换为实际像素大小（考虑高DPI）
    NSSize newDrawableSize = [view convertSizeToBacking:view.bounds.size];
    metalLayer.drawableSize = newDrawableSize;
    metalLayer.contentsScale = view.window.backingScaleFactor;
    return metalLayer;
}
