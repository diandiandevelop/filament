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

#ifndef TNT_FILAMENT_NATIVE_WINDOW_HELPER_H
#define TNT_FILAMENT_NATIVE_WINDOW_HELPER_H

struct SDL_Window;

/**
 * 获取SDL窗口的原生窗口句柄
 * @param sdlWindow SDL窗口指针
 * @return 原生窗口句柄（平台相关：Windows返回HWND，Linux返回Window或Wayland结构）
 */
extern "C" void* getNativeWindow(SDL_Window* sdlWindow);

#if defined(__APPLE__)
// Add a backing CAMetalLayer to the NSView and return the layer.
/**
 * 为NSView设置CAMetalLayer并返回该层
 * @param nativeWindow 原生窗口（NSView*）
 * @return CAMetalLayer指针
 */
extern "C" void* setUpMetalLayer(void* nativeWindow);
// Setup the window the way Filament expects (color space, etc.).
/**
 * 准备原生窗口（设置颜色空间等Filament需要的属性）
 * @param sdlWindow SDL窗口指针
 */
extern "C" void prepareNativeWindow(SDL_Window* sdlWindow);
// Resize the backing CAMetalLayer's drawable to match the new view's size. Returns the layer.
/**
 * 调整CAMetalLayer的可绘制区域大小以匹配新视图大小
 * @param nativeView 原生视图（NSView*）
 * @return CAMetalLayer指针
 */
extern "C" void* resizeMetalLayer(void* nativeView);
#endif

#endif // TNT_FILAMENT_NATIVE_WINDOW_HELPER_H
