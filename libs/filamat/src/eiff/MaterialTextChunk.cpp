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

#include "MaterialTextChunk.h"
#include "Flattener.h"
#include "LineDictionary.h"
#include "ShaderEntry.h"

#include <exception>
#include <utils/Log.h>
#include <utils/ostream.h>

#include <cstdint>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <string>

namespace filamat {

// 写入条目属性（着色器模型、变体、阶段等）
// @param entryIndex 条目索引
// @param f Flattener对象，用于写入数据
void MaterialTextChunk::writeEntryAttributes(size_t const entryIndex, Flattener& f) const noexcept {
    const TextEntry& entry = mEntries[entryIndex];
    f.writeUint8(uint8_t(entry.shaderModel));  // 写入着色器模型
    f.writeUint8(entry.variant.key);          // 写入变体键
    f.writeUint8(uint8_t(entry.stage));        // 写入着色器阶段
}

// 压缩着色器文本（使用行字典进行压缩）
// @param src 源着色器文本
// @param f Flattener对象，用于写入压缩后的数据
// @param dictionary 行字典，用于查找行的索引
void compressShader(std::string_view const src, Flattener &f, const LineDictionary& dictionary) {
    // 检查字典大小（最多65536个条目，因为使用uint16_t索引）
    if (dictionary.getDictionaryLineCount() > 65536) {
        slog.e << "Dictionary is too large!" << io::endl;
        std::terminate();
    }

    // 写入着色器大小（包含null终止符）
    f.writeUint32(static_cast<uint32_t>(src.size() + 1));
    // 写入行数占位符（稍后填充）
    f.writeValuePlaceholder();

    size_t numLines = 0;

    // 逐行处理着色器文本
    size_t cur = 0;
    size_t const len = src.length();
    const char* s = src.data();
    while (cur < len) {
        // Start of the current line
        // 当前行的起始位置
        size_t const pos = cur;
        // Find the end of the current line or end of text
        // 查找当前行的结束位置或文本的结束位置
        while (cur < len && s[cur] != '\n') {
            cur++;
        }
        // If we found a newline, advance past it for the next iteration, ensuring '\n' is included
        // 如果找到换行符，为下一次迭代推进过去，确保包含'\n'
        if (cur < len) {
            cur++;
        }
        std::string_view const newLine{ s + pos, cur - pos };

        // 从字典中获取行的索引
        auto const indices = dictionary.getIndices(newLine);
        if (indices.empty()) {
            slog.e << "Line not found in dictionary!" << io::endl;
            std::terminate();
        }

        // 累计行数并写入索引
        numLines += indices.size();
        for (auto const index : indices) {
            f.writeUint16(static_cast<uint16_t>(index));
        }
    }
    // 填充行数占位符
    f.writeValue(numLines);
}

// 将块扁平化到Flattener中
// @param f Flattener对象，用于写入数据
void MaterialTextChunk::flatten(Flattener& f) {
    // 重置偏移量占位符列表
    f.resetOffsets();

    // Avoid detecting duplicate twice (once for dry run and once for actual flattening).
    // 避免两次检测重复项（一次用于dry run，一次用于实际扁平化）
    if (mDuplicateMap.empty()) {
        // 初始化重复项映射表
        mDuplicateMap.resize(mEntries.size());

        // Detect duplicate;
        // 检测重复项：查找具有相同着色器代码的条目
        std::unordered_map<std::string_view, size_t> stringToIndex;
        for (size_t i = 0; i < mEntries.size(); i++) {
            const std::string& text = mEntries[i].shader;
            // 检查是否已存在相同的着色器代码
            if (auto iter = stringToIndex.find(text); iter != stringToIndex.end()) {
                // 标记为重复项，记录原始索引
                mDuplicateMap[i] = { true, iter->second };
            } else {
                // 首次出现，记录索引
                stringToIndex.emplace(text, i);
                mDuplicateMap[i].isDup = false;
            }
        }
    }

    // All offsets expressed later will start at the current flattener cursor position
    // 标记偏移量基址（后续所有偏移量都从当前flattener游标位置开始计算）
    f.markOffsetBase();

    // Write how many shaders we have
    // 写入着色器数量
    f.writeUint64(mEntries.size());

    // Write all indexes.
    // 写入所有条目的索引和属性
    for (size_t i = 0; i < mEntries.size(); i++) {
        // 写入条目属性（着色器模型、变体、阶段）
        writeEntryAttributes(i, f);
        // 写入偏移量占位符（如果是重复项，指向原始条目的索引）
        const ShaderMapping& mapping = mDuplicateMap[i];
        f.writeOffsetPlaceholder(mapping.isDup ? mapping.dupOfIndex : i);
    }

    // Write all strings
    // 写入所有唯一的着色器字符串（跳过重复项）
    for (size_t i = 0; i < mEntries.size(); i++) {
        // 如果是重复项，跳过（使用原始条目的数据）
        if (mDuplicateMap[i].isDup) {
            continue;
        }
        // 填充偏移量占位符
        f.writeOffsets(i);
        // 压缩并写入着色器文本
        compressShader(mEntries.at(i).shader, f, mDictionary);
    }
}

} // namespace filamat
