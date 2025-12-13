/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_BUFFEROBJECT_H
#define TNT_FILAMENT_DETAILS_BUFFEROBJECT_H

#include "downcast.h"

#include <backend/Handle.h>

#include <filament/BufferObject.h>

#include <utils/compiler.h>

namespace filament {

class FEngine;

/**
 * 缓冲区对象实现类
 * 
 * 管理通用 GPU 缓冲区对象。
 * 缓冲区对象可以用于存储各种数据，如 Uniform 数据、顶点数据等。
 * 
 * 实现细节：
 * - 支持不同的绑定类型（Uniform、Vertex、Index 等）
 * - 可以更新缓冲区内容
 * - 维护缓冲区大小和绑定类型
 */
class FBufferObject : public BufferObject {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FBufferObject(FEngine& engine, const Builder& builder);

    /**
     * 终止缓冲区对象
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 获取硬件句柄
     * 
     * @return 缓冲区对象句柄
     */
    backend::Handle<backend::HwBufferObject> getHwHandle() const noexcept { return mHandle; }

    /**
     * 获取字节数
     * 
     * @return 缓冲区大小（字节）
     */
    size_t getByteCount() const noexcept { return mByteCount; }

    /**
     * 获取绑定类型
     * 
     * @return 绑定类型
     */
    BindingType getBindingType() const noexcept { return mBindingType; }

private:
    friend class BufferObject;
    /**
     * 设置缓冲区数据
     * 
     * 更新缓冲区对象的内容。
     * 
     * @param engine 引擎引用
     * @param buffer 缓冲区描述符（会被移动）
     * @param byteOffset 字节偏移量
     */
    void setBuffer(FEngine& engine, BufferDescriptor&& buffer, uint32_t byteOffset = 0);
    backend::Handle<backend::HwBufferObject> mHandle;  // 缓冲区对象句柄
    uint32_t mByteCount;  // 缓冲区大小（字节）
    BindingType mBindingType;  // 绑定类型
};

FILAMENT_DOWNCAST(BufferObject)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_BUFFEROBJECT_H
