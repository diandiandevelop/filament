/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "DictionarySpirvChunk.h"

#include <smolv.h>

namespace filamat {

// 构造函数，使用blob字典和是否剥离调试信息标志初始化
DictionarySpirvChunk::DictionarySpirvChunk(BlobDictionary&& dictionary, bool stripDebugInfo) :
        Chunk(ChunkType::DictionarySpirv), mDictionary(std::move(dictionary)), mStripDebugInfo(stripDebugInfo) {
}

// 将块扁平化到Flattener中
// @param f Flattener对象，用于写入数据
void DictionarySpirvChunk::flatten(Flattener& f) {
    // 写入压缩方案（目前只有1是可接受的压缩方案）
    f.writeUint32(1);

    // 设置压缩标志
    uint32_t flags = 0;
    if (mStripDebugInfo) {
        flags |= smolv::kEncodeFlagStripDebugInfo;
    }

    // 写入blob数量
    f.writeUint32(mDictionary.getBlobCount());
    // 遍历所有blob，压缩并写入
    for (size_t i = 0 ; i < mDictionary.getBlobCount() ; i++) {
        // 获取SPIR-V blob
        std::string_view spirv = mDictionary.getBlob(i);
        // 压缩SPIR-V
        smolv::ByteArray compressed;
        if (!smolv::Encode(spirv.data(), spirv.size(), compressed, flags)) {
            utils::slog.e << "Error with SPIRV compression" << utils::io::endl;
        }

        // 写入对齐填充
        f.writeAlignmentPadding();
        // 写入压缩后的blob
        f.writeBlob((const char*) compressed.data(), compressed.size());
    }
}

} // namespace filamat
