/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERBASE_H
#define TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERBASE_H

// 驱动基类
#include "DriverBase.h"

// Utils 工具库
#include <utils/CString.h>  // C 字符串

namespace filament::backend {

/**
 * OpenGL 驱动基类
 * 
 * OpenGL 驱动的抽象基类，提供 OpenGL 特定的接口。
 * 所有 OpenGL 驱动实现都应继承此类。
 * 
 * 主要功能：
 * 1. 提供 OpenGL 供应商和渲染器信息查询接口
 * 2. 作为 OpenGLPlatform 和具体驱动实现之间的桥梁
 * 
 * 设计目的：
 * - 允许 OpenGLPlatform 查询驱动信息，而无需知道具体实现类型
 * - 提供类型安全的接口，确保只有 OpenGL 驱动可以使用这些方法
 */
class OpenGLDriverBase : public DriverBase {
protected:
    /**
     * 析构函数
     * 
     * 受保护的析构函数，防止直接删除基类指针。
     * 派生类必须正确实现析构函数。
     */
    ~OpenGLDriverBase() override;

public:
    /**
     * 获取 OpenGL 供应商字符串
     * 
     * 返回 OpenGL 供应商字符串（GL_VENDOR）。
     * 例如："NVIDIA Corporation"、"Intel Inc."、"ARM" 等。
     * 
     * @return OpenGL 供应商字符串
     */
    virtual utils::CString getVendorString() const noexcept = 0;

    /**
     * 获取 OpenGL 渲染器字符串
     * 
     * 返回 OpenGL 渲染器字符串（GL_RENDERER）。
     * 例如："NVIDIA GeForce RTX 3080/PCIe/SSE2"、"Mali-G78" 等。
     * 
     * @return OpenGL 渲染器字符串
     */
    virtual utils::CString getRendererString() const noexcept = 0;
};

} // filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_OPENGLDRIVERBASE_H
