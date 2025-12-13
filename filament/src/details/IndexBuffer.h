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

#ifndef TNT_FILAMENT_DETAILS_INDEXBUFFER_H
#define TNT_FILAMENT_DETAILS_INDEXBUFFER_H

#include "downcast.h"

#include <backend/Handle.h>

#include <filament/IndexBuffer.h>

#include <utils/compiler.h>

namespace filament {

class FEngine;

/**
 * 索引缓冲区实现类
 * 
 * 管理索引数据的 GPU 缓冲区。
 * 索引缓冲区存储顶点索引，用于定义图元的连接关系。
 * 
 * 实现细节：
 * - 支持 16 位或 32 位索引
 * - 可以更新缓冲区数据
 * - 维护索引数量
 */
class FIndexBuffer : public IndexBuffer {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FIndexBuffer(FEngine& engine, const Builder& builder);

    /**
     * 终止索引缓冲区
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
     * @return 索引缓冲区句柄
     */
    backend::Handle<backend::HwIndexBuffer> getHwHandle() const noexcept { return mHandle; }

    /**
     * 获取索引数量
     * 
     * @return 索引数量
     */
    size_t getIndexCount() const noexcept { return mIndexCount; }

    /**
     * 设置缓冲区数据
     * 
     * 更新索引缓冲区的内容。
     * 
     * @param engine 引擎引用
     * @param buffer 缓冲区描述符（会被移动）
     * @param byteOffset 字节偏移量
     */
    void setBuffer(FEngine& engine, BufferDescriptor&& buffer, uint32_t byteOffset = 0);

private:
    friend class IndexBuffer;
    backend::Handle<backend::HwIndexBuffer> mHandle;  // 索引缓冲区句柄
    uint32_t mIndexCount;  // 索引数量
};

FILAMENT_DOWNCAST(IndexBuffer)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_INDEXBUFFER_H
