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

#include "ChunkContainer.h"
#include "Flattener.h"

namespace filamat {

// This call is relatively expensive since it performs a dry run of the flattering process,
// using a flattener that will calculate offets but will not write. It should be used only once
// when the container is about to be flattened.
// 此调用相对昂贵，因为它执行扁平化过程的dry run，使用一个只计算偏移量但不写入的flattener。
// 它应该在容器即将被扁平化时只使用一次。

// 获取容器的总大小（通过dry run计算）
// @return 容器的总字节数
size_t ChunkContainer::getSize() const {
    return flatten(Flattener::getDryRunner());
}

// 将容器扁平化到Flattener中
// @param f Flattener对象，用于写入数据
// @return 写入的总字节数
size_t ChunkContainer::flatten(Flattener& f) const {
    // 遍历所有子块
    for (const auto& chunk: mChildren) {
        // 写入块类型
        f.writeUint64(static_cast<uint64_t>(chunk->getType()));
        // 写入大小占位符（稍后填充）
        f.writeSizePlaceholder();
        // 扁平化子块
        chunk->flatten(f);
        // 填充大小占位符
        f.writeSize();
    }
    // 返回写入的总字节数
    return f.getBytesWritten();
}

}
