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

#include <SDL_syswm.h>

/**
 * 获取SDL窗口的原生窗口句柄实现（Windows平台）
 * 
 * 执行步骤：
 * 1. 初始化SDL系统窗口管理器信息结构
 * 2. 获取SDL窗口的系统窗口管理器信息
 * 3. 从Windows特定信息中提取HWND句柄
 * 4. 返回HWND句柄
 * 
 * @param sdlWindow SDL窗口指针
 * @return Windows原生窗口句柄（HWND）
 */
void* getNativeWindow(SDL_Window* sdlWindow) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi))
            << "SDL version unsupported!";
    HWND win = (HWND) wmi.info.win.window;
    return (void*) win;
}
