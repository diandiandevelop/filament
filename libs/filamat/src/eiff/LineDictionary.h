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

#ifndef TNT_FILAMAT_LINEDICTIONARY_H
#define TNT_FILAMAT_LINEDICTIONARY_H

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace utils::io {
class ostream;
}

namespace filamat {

// 行字典类，用于存储唯一行的字典（用于文本着色器的字典压缩）
class LineDictionary {
public:
    using index_t = uint32_t;  // 索引类型

    LineDictionary();
    ~LineDictionary() noexcept;

    // Due to the presence of unique_ptr, disallow copy construction but allow move construction.
    // 由于存在unique_ptr，禁止复制构造但允许移动构造
    LineDictionary(LineDictionary const&) = delete;
    LineDictionary(LineDictionary&&) = default;

    // Adds text to the dictionary, parsing it into lines.
    // 将文本添加到字典中，将其解析为行
    void addText(std::string_view text) noexcept;

    // Returns the total number of unique lines stored in the dictionary.
    // 返回字典中存储的唯一行的总数
    size_t getDictionaryLineCount() const {
        return mStrings.size();
    }

    // Checks if the dictionary is empty.
    // 检查字典是否为空
    bool isEmpty() const noexcept {
        return mStrings.empty();
    }

    // Retrieves a string by its index.
    // 根据索引获取字符串
    std::string const& getString(index_t index) const noexcept;

    // Retrieves the indices of lines that match the given string view.
    // 获取与给定string_view匹配的行的索引
    std::vector<index_t> getIndices(std::string_view const& line) const noexcept;

    // Prints statistics about the dictionary to the given output stream.
    // 将字典的统计信息打印到给定的输出流
    void printStatistics(utils::io::ostream& stream) const noexcept;

    // conveniences...
    // 便利方法...
    size_t size() const {
        return getDictionaryLineCount();
    }

    bool empty() const noexcept {
        return isEmpty();
    }

    std::string const& operator[](index_t const index) const noexcept {
        return getString(index);
    }

private:
    // Adds a single line to the dictionary.
    // 将单行添加到字典中
    void addLine(std::string_view line) noexcept;

    // Trims leading whitespace from a string view.
    // 从string_view中修剪前导空白字符
    static std::string_view ltrim(std::string_view s);

    // Splits a string view into a vector of string views based on delimiters.
    // 根据分隔符将string_view分割成string_view向量
    static std::vector<std::string_view> splitString(std::string_view line);

    // Finds a pattern within a string view starting from an offset.
    // 从偏移量开始在string_view中查找模式
    static std::pair<size_t, size_t> findPattern(std::string_view line, size_t offset);

    // 行信息结构
    struct LineInfo {
        index_t index;    // 行索引
        uint32_t count;   // 出现次数
    };

    std::unordered_map<std::string_view, LineInfo> mLineIndices;    // 行到行信息的映射表
    std::vector<std::unique_ptr<std::string>> mStrings;              // 字符串存储列表
};

} // namespace filamat
#endif // TNT_FILAMAT_LINEDICTIONARY_H
