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
 * 获取SDL窗口的原生窗口句柄实现（Linux平台）
 * 
 * 执行步骤：
 * 1. 初始化SDL系统窗口管理器信息结构
 * 2. 获取SDL窗口的系统窗口管理器信息
 * 3. 根据窗口子系统类型（X11或Wayland）提取相应的窗口句柄
 * 4. 对于Wayland，创建包含display、surface和尺寸的结构体
 * 5. 返回原生窗口句柄
 * 
 * @param sdlWindow SDL窗口指针
 * @return 原生窗口句柄（X11返回Window，Wayland返回结构体指针，失败返回nullptr）
 */
void* getNativeWindow(SDL_Window* sdlWindow) {
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    FILAMENT_CHECK_POSTCONDITION(SDL_GetWindowWMInfo(sdlWindow, &wmi))
            << "SDL version unsupported!";
    if (wmi.subsystem == SDL_SYSWM_X11) {
#if defined(FILAMENT_SUPPORTS_X11)
        // X11窗口系统：返回X11 Window ID
        Window win = (Window) wmi.info.x11.window;
        return (void *) win;
#endif
    }
    else if (wmi.subsystem == SDL_SYSWM_WAYLAND) {
#if defined(FILAMENT_SUPPORTS_WAYLAND)
        // Wayland窗口系统：需要返回包含display、surface和尺寸的结构体
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(sdlWindow, &width, &height);

        // Static is used here to allocate the struct pointer for the lifetime of the program.
        // Without static the valid struct quickly goes out of scope, and ends with seemingly
        // random segfaults. We must update the values on each call.
        // 使用静态变量确保结构体在程序生命周期内有效
        // 不使用静态变量会导致结构体快速超出作用域，造成段错误
        // 必须在每次调用时更新值
        static struct {
            struct wl_display *display;  // Wayland显示对象
            struct wl_surface *surface;   // Wayland表面对象
            uint32_t width;               // 窗口宽度
            uint32_t height;              // 窗口高度
        } wayland;
        wayland.display = wmi.info.wl.display;
        wayland.surface = wmi.info.wl.surface;
        wayland.width = static_cast<uint32_t>(width);
        wayland.height = static_cast<uint32_t>(height);
        return (void *) &wayland;
#endif
    }
    return nullptr;
}
