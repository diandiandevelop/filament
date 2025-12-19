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

#include <filaflat/DictionaryReader.h>

#include <filaflat/ChunkContainer.h>
#include <filaflat/Unflattener.h>

#if defined (FILAMENT_DRIVER_SUPPORTS_VULKAN)
#include <utils/Log.h>
#include <smolv.h>
#endif

#include <assert.h>

using namespace filamat;

namespace filaflat {

// 从容器中反序列化字典数据
bool DictionaryReader::unflatten(ChunkContainer const& container,
        ChunkContainer::Type dictionaryTag,
        BlobDictionary& dictionary) {

    // 获取字典块的数据范围
    auto [start, end] = container.getChunkRange(dictionaryTag);
    Unflattener unflattener(start, end);

    // 处理SPIR-V字典（压缩格式）
    if (dictionaryTag == ChunkType::DictionarySpirv) {
        // 读取压缩方案
        uint32_t compressionScheme;
        if (!unflattener.read(&compressionScheme)) {
            return false;
        }
        // For now, 1 is the only acceptable compression scheme.
        // 目前，1是唯一可接受的压缩方案
        assert(compressionScheme == 1);

        // 读取blob数量
        uint32_t blobCount;
        if (!unflattener.read(&blobCount)) {
            return false;
        }

        dictionary.reserve(blobCount);
        // 遍历所有blob，解压缩并存储
        for (uint32_t i = 0; i < blobCount; i++) {
            // 跳过对齐填充
            unflattener.skipAlignmentPadding();

            // 读取压缩数据
            const char* compressed;
            size_t compressedSize;
            if (!unflattener.read(&compressed, &compressedSize)) {
                return false;
            }

            assert_invariant((intptr_t(compressed) % 8) == 0);

#if defined (FILAMENT_DRIVER_SUPPORTS_VULKAN)
            // 获取解码后的SPIR-V大小
            size_t spirvSize = smolv::GetDecodedBufferSize(compressed, compressedSize);
            if (spirvSize == 0) {
                return false;
            }
            // 解码SPIR-V数据
            ShaderContent spirv(spirvSize);
            if (!smolv::Decode(compressed, compressedSize, spirv.data(), spirvSize)) {
                return false;
            }
            dictionary.emplace_back(std::move(spirv));
#else
            return false;
#endif

        }
        return true;
    } else if (dictionaryTag == ChunkType::DictionaryMetalLibrary) {
        // 处理Metal库字典
        // 读取blob数量
        uint32_t blobCount;
        if (!unflattener.read(&blobCount)) {
            return false;
        }

        dictionary.reserve(blobCount);
        // 遍历所有blob，直接复制数据
        for (uint32_t i = 0; i < blobCount; i++) {
            // 跳过对齐填充
            unflattener.skipAlignmentPadding();

            // 读取数据
            const char* data;
            size_t dataSize;
            if (!unflattener.read(&data, &dataSize)) {
                return false;
            }
            // 复制数据到字典中
            dictionary.emplace_back(dataSize);
            memcpy(dictionary.back().data(), data, dictionary.back().size());
        }
        return true;
    } else if (dictionaryTag == ChunkType::DictionaryText) {
        // 处理文本字典
        // 读取字符串数量
        uint32_t stringCount = 0;
        if (!unflattener.read(&stringCount)) {
            return false;
        }

        dictionary.reserve(stringCount);
        // 遍历所有字符串
        for (uint32_t i = 0; i < stringCount; i++) {
            const char* str;
            if (!unflattener.read(&str)) {
                return false;
            }
            // BlobDictionary hold binary chunks and does not care if the data holds text, it is
            // therefore crucial to include the trailing null.
            // BlobDictionary保存二进制块，不关心数据是否包含文本，因此包含尾随空字符至关重要
            dictionary.emplace_back(strlen(str) + 1);
            memcpy(dictionary.back().data(), str, dictionary.back().size());
        }
        return true;
    }

    return false;
}

} // namespace filaflat
