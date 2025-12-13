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

#include "details/BufferObject.h"

#include "details/Engine.h"

#include "FilamentAPI-impl.h"

namespace filament {

/**
 * 设置缓冲区数据
 * 
 * 将缓冲区描述符设置到缓冲区对象中。
 * 
 * @param engine 引擎引用
 * @param buffer 缓冲区描述符（会被移动）
 * @param byteOffset 字节偏移量
 */
void BufferObject::setBuffer(Engine& engine,
        BufferDescriptor&& buffer, uint32_t const byteOffset) {
    /**
     * 设置缓冲区数据
     * 
     * 将缓冲区描述符中的数据上传到 GPU 缓冲区对象。
     * 
     * @param engine 引擎引用，用于访问底层驱动
     * @param buffer 缓冲区描述符（会被移动，调用后不再有效）
     *               包含：
     *               - 数据指针
     *               - 数据大小
     *               - 回调函数（可选，用于数据上传完成后的清理）
     * @param byteOffset 字节偏移量，指定数据在缓冲区中的起始位置
     *                   0 表示从缓冲区开头开始
     * 
     * 实现：将调用转发到内部实现类（FBufferObject）
     * 
     * 注意：
     * - buffer 使用移动语义，调用后原对象不再有效
     * - 数据上传是异步的，可能需要多帧才能完成
     */
    downcast(this)->setBuffer(downcast(engine), std::move(buffer), byteOffset);
}

/**
 * 获取字节数
 * 
 * 返回缓冲区对象的大小（字节数）。
 * 
 * @return 缓冲区大小（字节）
 *         如果缓冲区未初始化，可能返回 0
 * 
 * 实现：从内部实现类（FBufferObject）获取缓冲区大小
 */
size_t BufferObject::getByteCount() const noexcept {
    return downcast(this)->getByteCount();
}

} // namespace filament
