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

#include "LineDictionary.h"

#include <utils/debug.h>
#include <utils/Log.h>
#include <utils/ostream.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace filamat {

namespace {
    // 检查字符是否为单词字符（字母、数字或下划线）
    // @param c 要检查的字符
    // @return 如果是单词字符返回true，否则返回false
    // Note: isalnum is locale-dependent, which can be problematic.
    // For our purpose, we define word characters as ASCII alphanumeric characters plus underscore.
    // This is safe for UTF-8 strings, as any byte of a multi-byte character will not be
    // in these ranges.
    bool isWordChar(char const c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
} // anonymous namespace

LineDictionary::LineDictionary() = default;

LineDictionary::~LineDictionary() noexcept {
    //printStatistics(utils::slog.d);
}

// 根据索引获取字符串
// @param index 字符串索引
// @return 字符串的常量引用
std::string const& LineDictionary::getString(index_t const index) const noexcept {
    return *mStrings[index];
}

// 获取行的索引列表（将行分割后查找每个子行的索引）
// @param line 要查找的行
// @return 索引列表，如果任何子行不在字典中则返回空列表
std::vector<LineDictionary::index_t> LineDictionary::getIndices(
        std::string_view const& line) const noexcept {
    std::vector<index_t> result;
    // 将行分割成子行
    std::vector<std::string_view> const sublines = splitString(line);
    // 遍历每个子行，查找其索引
    for (std::string_view const& subline : sublines) {
        if (auto iter = mLineIndices.find(subline); iter != mLineIndices.end()) {
            // 找到索引，添加到结果中
            result.push_back(iter->second.index);
        } else {
            // 任何子行不在字典中，返回空列表
            return {};
        }
    }
    return result;
}

// 添加文本到字典中（按行分割并添加）
// @param text 要添加的文本
void LineDictionary::addText(std::string_view const text) noexcept {
    size_t cur = 0;
    size_t const len = text.length();
    const char* s = text.data();
    // 逐行处理文本
    while (cur < len) {
        // 当前行的起始位置
        size_t const pos = cur;
        // 查找当前行的结束位置或文本的结束位置
        while (cur < len && s[cur] != '\n') {
            cur++;
        }
        // 如果找到换行符，为下一次迭代推进过去，确保包含'\n'
        if (cur < len) {
            cur++;
        }
        // 添加当前行到字典
        addLine({ s + pos, cur - pos });
    }
}

// 添加行到字典中（如果行包含模式，会先分割）
// @param line 要添加的行
void LineDictionary::addLine(std::string_view const line) noexcept {
    // 将行分割成子行（如果包含模式）
    auto const lines = splitString(line);
    // 遍历每个子行
    for (std::string_view const& subline : lines) {
        // 检查子行是否已存在于字典中
        auto pos = mLineIndices.find(subline);
        if (pos != mLineIndices.end()) {
            // 已存在，增加计数
            pos->second.count++;
            continue;
        }
        // 不存在，创建新字符串并添加到字典
        mStrings.emplace_back(std::make_unique<std::string>(subline));
        // 添加到索引映射表
        mLineIndices.emplace(*mStrings.back(),
                LineInfo{
                    .index = index_t(mStrings.size() - 1),
                    .count = 1 });
    }
}

// 去除字符串左侧的空白字符
// @param s 要处理的字符串视图
// @return 去除左侧空白后的字符串视图
std::string_view LineDictionary::ltrim(std::string_view s) {
    s.remove_prefix(std::distance(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char const c) { return !std::isspace(c); })));
    return { s.data(), s.size() };
}

// 在行中查找模式（前缀+数字序列）
// @param line 要搜索的行
// @param offset 搜索起始偏移量
// @return 匹配的位置和长度，如果未找到则返回{npos, 0}
// Patterns are ordered from longest to shortest to ensure correct prefix matching.
std::pair<size_t, size_t> LineDictionary::findPattern(
        std::string_view const line, size_t const offset) {
    // 模式按从长到短排序，以确保正确的前缀匹配
    static constexpr std::string_view kPatterns[] = { "hp_copy_", "mp_copy_", "_" };

    const size_t line_len = line.length();
    // 从偏移量开始搜索
    for (size_t i = offset; i < line_len; ++i) {
        // 模式必须是完整单词（或在字符串开头）
        if (i > 0 && isWordChar(line[i - 1])) {
            continue;
        }

        // 尝试匹配每个模式前缀
        for (const auto& prefix : kPatterns) {
            if (line.size() - i >= prefix.size() && line.substr(i, prefix.size()) == prefix) {
                // 匹配到已知前缀，检查后面是否有数字序列
                size_t const startOfDigits = i + prefix.size();
                if (startOfDigits < line_len && std::isdigit(line[startOfDigits])) {
                    // 查找数字序列的结束位置（最多6位数字）
                    size_t j = startOfDigits;
                    while (j < line_len && (j < startOfDigits + 6) && std::isdigit(line[j])) {
                        j++;
                    }
                    // 检查匹配后的字符是否也是单词字符
                    if (j < line_len && isWordChar(line[j])) {
                        // 是单词字符，这不是有效边界，继续搜索
                        break; // 跳出内层循环（kPatterns）
                    }
                    // 找到完整的模式匹配（前缀+数字）
                    return { i, j - i };
                }
                // 如果前缀匹配但后面没有数字，不是有效模式
                // 跳出内层循环，从下一个字符继续搜索
                break;
            }
        }
    }
    return { std::string_view::npos, 0 }; // 未找到模式
}

// 将字符串分割成多个子字符串视图（根据模式分割）
// @param line 要分割的字符串
// @return 分割后的字符串视图列表
std::vector<std::string_view> LineDictionary::splitString(std::string_view const line) {
    std::vector<std::string_view> result;
    size_t current_pos = 0;

    // 如果字符串为空，返回包含空字符串视图的列表
    if (line.empty()) {
        result.push_back({});
        return result;
    }

    // 查找模式并分割字符串
    while (current_pos < line.length()) {
        // 从当前位置查找模式
        auto const [match_pos, match_len] = findPattern(line, current_pos);

        if (match_pos == std::string_view::npos) {
            // 未找到更多模式，添加字符串的剩余部分
            result.push_back(line.substr(current_pos));
            break;
        }

        // 添加匹配前的部分
        if (match_pos > current_pos) {
            result.push_back(line.substr(current_pos, match_pos - current_pos));
        }

        // 添加匹配本身
        result.push_back(line.substr(match_pos, match_len));

        // 将光标移动到匹配之后
        current_pos = match_pos + match_len;
    }

    return result;
}

// 打印字典统计信息到输出流
// @param stream 输出流
void LineDictionary::printStatistics(utils::io::ostream& stream) const noexcept {
    std::vector<std::pair<std::string_view, LineInfo>> info;
    for (auto const& pair : mLineIndices) {
        info.push_back(pair);
    }

    // Sort by count, then by index.
    std::sort(info.begin(), info.end(),
            [](auto const& lhs, auto const& rhs) {
        if (lhs.second.count != rhs.second.count) {
            return lhs.second.count > rhs.second.count;
        }
        return lhs.second.index < rhs.second.index;
    });

    size_t total_size = 0;
    size_t compressed_size = 0;
    size_t total_lines = 0;
    size_t indices_size = 0;
    size_t indices_size_if_varlen = 0;
    size_t indices_size_if_varlen_sorted = 0;
    size_t i = 0;
    using namespace utils;
    // Print the dictionary.
    stream << "Line dictionary:" << io::endl;
    for (auto const& pair : info) {
        compressed_size += pair.first.length();
        total_size += pair.first.length() * pair.second.count;
        total_lines += pair.second.count;
        indices_size += sizeof(uint16_t) * pair.second.count;
        if (pair.second.index <= 127) {
            indices_size_if_varlen += sizeof(uint8_t) * pair.second.count;
        } else {
            indices_size_if_varlen += sizeof(uint16_t) * pair.second.count;
        }
        if (i <= 128) {
            indices_size_if_varlen_sorted += sizeof(uint8_t) * pair.second.count;
        } else {
            indices_size_if_varlen_sorted += sizeof(uint16_t) * pair.second.count;
        }
        i++;
        stream << "  " << pair.second.count << ": " << pair.first << io::endl;
    }
    stream << "Total size: " << total_size << ", compressed size: " << compressed_size << io::endl;
    stream << "Saved size: " << total_size - compressed_size << io::endl;
    stream << "Unique lines: " << mLineIndices.size() << io::endl;
    stream << "Total lines: " << total_lines << io::endl;
    stream << "Compression ratio: " << double(total_size) / compressed_size << io::endl;
    stream << "Average line length (total): " << double(total_size) / total_lines << io::endl;
    stream << "Average line length (compressed): " << double(compressed_size) / mLineIndices.size() << io::endl;
    stream << "Indices size: " << indices_size << io::endl;
    stream << "Indices size (if varlen): " << indices_size_if_varlen << io::endl;
    stream << "Indices size (if varlen, sorted): " << indices_size_if_varlen_sorted << io::endl;

    // some data we gathered

    // Total size: 751161, compressed size: 59818
    // Saved size: 691343
    // Unique lines: 3659
    // Total lines: 61686
    // Compression ratio: 12.557440904075696
    // Average line length (total): 12.177171481373406
    // Average line length (compressed): 16.34818256354195


    // Total size: 751161, compressed size: 263215
    // Saved size: 487946
    // Unique lines: 4672
    // Total lines: 23258
    // Compression ratio: 2.8537925270216364
    // Average line length (total): 32.296887092613296
    // Average line length (compressed): 56.338827054794521
}

} // namespace filamat
