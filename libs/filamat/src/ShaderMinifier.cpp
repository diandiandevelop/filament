/*
 * Copyright (C) 2020 The Android Open Source Project
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


#include "ShaderMinifier.h"

#include <utils/Log.h>

namespace filamat {

// 检查字符是否为标识符非数字字符（下划线或字母）
// @param c 字符
// @return 如果是标识符非数字字符返回true
static bool isIdCharNondigit(char c) {
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// 检查字符是否为标识符字符（下划线、字母或数字）
// @param c 字符
// @return 如果是标识符字符返回true
static bool isIdChar(char c) {
    return isIdCharNondigit(c) || (c >= '0' && c <= '9');
};

// Checks if a GLSL identifier lives at the given index in the given codeline.
// If so, returns the identifier and moves the given index to point
// to the first character after the identifier.
// 检查GLSL标识符是否在给定代码行的指定索引处
// 如果是，返回标识符并将索引移动到标识符后的第一个字符
// @param codeline 代码行
// @param pindex 指向索引的指针（输入输出参数）
// @param id 输出参数，标识符字符串视图
// @return 如果找到标识符返回true
// 步骤1：检查索引是否有效
// 步骤2：检查第一个字符是否为标识符非数字字符
// 步骤3：继续读取标识符字符直到遇到非标识符字符
// 步骤4：提取标识符并更新索引
static bool getId(std::string_view codeline, size_t* pindex, std::string_view* id) {
    size_t index = *pindex;
    // 步骤1：检查索引是否有效
    if (index >= codeline.size()) {
        return false;
    }
    // 步骤2：检查第一个字符是否为标识符非数字字符（下划线或字母）
    if (!isIdCharNondigit(codeline[index])) {
        return false;
    }
    ++index;
    // 步骤3：继续读取标识符字符（下划线、字母或数字）直到遇到非标识符字符
    while (index < codeline.size() && isIdChar(codeline[index])) {
        ++index;
    }
    // 步骤4：提取标识符并更新索引
    *id = codeline.substr(*pindex, index - *pindex);
    *pindex = index;
    return true;
}

// Checks if the given string lives anywhere after the given index in the given codeline.
// If so, moves the given index to point to the first character after the string.
// 检查给定字符串是否在代码行的指定索引之后存在
// 如果是，将索引移动到字符串后的第一个字符
// @param codeline 代码行
// @param pindex 指向索引的指针（输入输出参数）
// @param s 要查找的字符串
// @return 如果找到字符串返回true
// 步骤1：从指定索引开始查找字符串
// 步骤2：如果找到，更新索引到字符串后的位置
static bool getString(std::string_view codeline, size_t* pindex, std::string_view s) {
    // 步骤1：从指定索引开始查找字符串
    size_t index = codeline.find(s, *pindex);
    if (index == std::string_view::npos) {
        return false;
    }
    // 步骤2：如果找到，更新索引到字符串后的位置
    *pindex = index + s.size();
    return true;
}

// Checks if the given character is at the last position of the string.
// 检查给定字符是否在字符串的最后位置
// @param codeline 代码行
// @param index 索引位置
// @param c 要检查的字符
// @return 如果字符在最后位置返回true
static bool getLastChar(std::string_view codeline, size_t index, char c) {
    if (index != codeline.size() - 1) {
        return false;
    }
    return codeline[index] == c;
}

// Checks if whitespace lives at the given position of a codeline; if so, updates the given index.
// 检查代码行指定位置是否有空白字符；如果有，更新索引
// @param codeline 代码行
// @param pindex 指向索引的指针（输入输出参数）
// @return 如果找到空白字符返回true
// 步骤1：从指定索引开始跳过所有空白字符（空格或制表符）
// 步骤2：如果索引有变化，更新索引并返回true
static bool getWhitespace(std::string_view codeline, size_t* pindex) {
    size_t index = *pindex;
    // 步骤1：从指定索引开始跳过所有空白字符（空格或制表符）
    while (index < codeline.size() && (codeline[index] == ' ' || codeline[index] == '\t')) {
        ++index;
    }
    // 步骤2：如果索引有变化，更新索引并返回true
    if (index == *pindex) {
        return false;
    }
    *pindex = index;
    return true;
}

// Skips past an optional precision qualifier that is possibly surrounded by whitespace.
// 跳过可选的精度限定符（可能被空白字符包围）
// @param codeline 代码行
// @param pindex 指向索引的指针（输入输出参数）
// 步骤1：保存当前索引位置
// 步骤2：跳过前置空白字符
// 步骤3：检查是否为精度限定符（lowp、mediump、highp）
// 步骤4：如果找到精度限定符，跳过它
// 步骤5：跳过后续空白字符
static void ignorePrecision(std::string_view codeline, size_t* pindex) {
    // 步骤1：保存当前索引位置
    static std::string tokens[3] = {"lowp", "mediump", "highp"};
    const size_t i = *pindex;
    // 步骤2：跳过前置空白字符
    getWhitespace(codeline, pindex);
    // 步骤3：检查是否为精度限定符（lowp、mediump、highp）
    for (const auto& token : tokens) {
        const size_t n = token.size();
        if (codeline.substr(i, i + n) == token) {
            // 步骤4：如果找到精度限定符，跳过它
            *pindex += n;
            break;
        }
    }
    // 步骤5：跳过后续空白字符
    getWhitespace(codeline, pindex);
}

// Checks if the given string lives has an array size at the given codeline.
// If so, moves the given index to point to the first character after the array size.
// Always returns true for convenient daisy-chaining in the parser.
// 检查代码行是否有数组大小；如果有，将索引移动到数组大小后的第一个字符
// 总是返回true以便在解析器中方便地链式调用
// @param codeline 代码行
// @param pindex 指向索引的指针（输入输出参数）
// @return 总是返回true
// 步骤1：检查索引是否超出范围
// 步骤2：检查是否为数组开始符'['
// 步骤3：跳过数组大小内容直到找到']'
// 步骤4：更新索引到']'之后的位置
static bool ignoreArraySize(std::string_view codeline, size_t* pindex) {
    size_t index = *pindex;
    // 步骤1：检查索引是否超出范围
    if (index >= codeline.size()) {
        return true;
    }
    // 步骤2：检查是否为数组开始符'['
    if (codeline[index] != '[') {
        return true;
    }
    ++index;
    // 步骤3：跳过数组大小内容直到找到']'
    while (index < codeline.size() && codeline[index] != ']') {
        ++index;
    }
    // 步骤4：更新索引到']'之后的位置（如果找到']'）或字符串末尾
    *pindex = (index == codeline.size()) ? index : index + 1;
    return true;
}

// 替换字符串中所有匹配的子字符串
// @param result 目标字符串（将被修改）
// @param from 要查找的子字符串
// @param to 替换为的字符串
// 步骤1：从位置0开始查找
// 步骤2：如果找到匹配，替换它
// 步骤3：从替换后的位置继续查找
static void replaceAll(std::string& result, std::string_view from, std::string to) {
    size_t n = 0;
    // 步骤1-3：循环查找并替换所有匹配项
    while ((n = result.find(from, n)) != std::string::npos) {
        // 步骤2：替换找到的匹配项
        result.replace(n, from.size(), to);
        // 步骤3：从替换后的位置继续查找（避免重复替换刚替换的内容）
        n += to.size();
    }
}

namespace {
    enum ParserState {
        OUTSIDE,
        STRUCT_OPEN,
        STRUCT_DEFN,
    };
}

/**
 * Shrinks the specified string and returns a new string as the result.
 * To shrink the string, this method performs the following transforms:
 * - Remove leading white spaces at the beginning of each line
 * - Remove empty lines
 */
// 压缩指定字符串并返回新字符串
// 压缩操作包括：移除每行开头的空白字符、移除空行
// @param s 源字符串
// @param mergeBraces 是否合并大括号（将单独的{或}移到上一行）
// @return 压缩后的字符串
std::string ShaderMinifier::removeWhitespace(const std::string& s, bool mergeBraces) const {
    // 步骤1：初始化当前位置和结果字符串
    size_t cur = 0;

    std::string r;
    r.reserve(s.length());

    // 步骤2：逐行处理字符串
    while (cur < s.length()) {
        // 步骤2.1：记录当前行的起始位置和长度
        size_t pos = cur;
        size_t len = 0;

        // 步骤2.2：找到当前行的结束位置（换行符）
        while (s[cur] != '\n') {
            cur++;
            len++;
        }

        // 步骤2.3：找到行中第一个非空白字符的位置
        size_t newPos = s.find_first_not_of(" \t", pos);
        if (newPos == std::string::npos) newPos = pos;

        // If we have a single { or } on a line, move it to the previous line instead
        // 如果一行中只有一个{或}，将其移到上一行
        // 步骤2.4：计算去除前导空白后的行长度
        size_t subLen = len - (newPos - pos);
        // 步骤2.5：如果启用合并大括号且该行只有一个大括号，将其合并到上一行
        if (mergeBraces && subLen == 1 && (s[newPos] == '{' || s[newPos] == '}')) {
            r.replace(r.size() - 1, 1, 1, s[newPos]);
        } else {
            // 步骤2.6：否则，追加去除前导空白后的行内容
            r.append(s, newPos, subLen);
        }
        r += '\n';

        // 步骤2.7：跳过所有连续的换行符（移除空行）
        while (s[cur] == '\n') {
            cur++;
        }
    }

    return r;
}

/**
 * Uniform block definitions can be quite big so this compresses them as follows.
 * First, the uniform struct definitions are found, new field names are generated, and a mapping
 * table is built. Second, all uses are replaced by applying the mapping table.
 *
 * The struct definition must be a sequence of tokens with the following pattern. This is fairly
 * constrained (e.g. no comments or nesting) but this is designed to operate on generated code.
 *
 *     "uniform" TypeIdentifier
 *     {
 *     OptionalPrecQual TypeIdentifier FieldIdentifier OptionalArraySize ;
 *     OptionalPrecQual TypeIdentifier FieldIdentifier OptionalArraySize ;
 *     OptionalPrecQual TypeIdentifier FieldIdentifier OptionalArraySize ;
 *     } StructIdentifier ;
 */
// 重命名结构体字段（压缩uniform结构体定义）
// uniform块定义可能很大，因此按以下方式压缩：
// 首先，找到uniform结构体定义，生成新字段名，构建映射表
// 然后，通过应用映射表替换所有使用
// 结构体定义必须遵循特定模式（相当受限，例如无注释或嵌套），但这是为生成的代码设计的
// @param source 源字符串
// @return 压缩后的字符串（字段名被重命名为短名称）
std::string ShaderMinifier::renameStructFields(const std::string& source) {
    // 步骤1：将源字符串分割为代码行
    std::string_view sv = source;
    size_t first = 0;
    mCodelines.clear();
    while (first < sv.size()) {
        // 步骤1.1：找到下一个换行符
        const size_t second = sv.find_first_of('\n', first);
        // 步骤1.2：如果不是空行，添加到代码行列表
        if (first != second) {
            mCodelines.emplace_back(sv.substr(first, second - first));
        }
        if (second == std::string_view::npos) {
            break;
        }
        first = second + 1;
    }
    // 步骤2：构建字段映射表（解析结构体定义，生成字段名映射）
    buildFieldMapping();
    // 步骤3：应用字段映射（使用映射表替换所有字段名）
    return applyFieldMapping();
}

// 构建字段映射表（解析结构体定义，生成字段名映射）
// 步骤1：初始化状态和数据结构
// 步骤2：遍历代码行，使用状态机解析结构体定义
// 步骤3：为每个结构体字段生成短名称并建立映射
// 步骤4：对映射表按键长度排序（从长到短，避免替换冲突）
void ShaderMinifier::buildFieldMapping() {
    // 步骤1：初始化状态和数据结构
    mStructFieldMap.clear();
    mStructDefnMap.clear();
    std::string currentStructPrefix;
    std::vector<std::string_view> currentStructFields;
    ParserState state = OUTSIDE;
    
    // 步骤2：遍历代码行，使用状态机解析结构体定义
    for (std::string_view codeline : mCodelines) {
        size_t cursor = 0;
        std::string_view typeId;
        std::string_view fieldName;
        switch (state) {
            case OUTSIDE:
                // 步骤2.1：在OUTSIDE状态，查找"uniform TypeIdentifier"行
                if (getString(codeline, &cursor, "uniform") &&
                        getWhitespace(codeline, &cursor) &&
                        getId(codeline, &cursor, &typeId) &&
                        cursor == codeline.size()) {
                    // 步骤2.1.1：找到uniform声明，保存类型前缀并进入STRUCT_OPEN状态
                    currentStructPrefix = std::string(typeId) + ".";
                    state = STRUCT_OPEN;
                }
                continue;
            case STRUCT_OPEN:
                // 步骤2.2：在STRUCT_OPEN状态，查找'{'行
                state = getLastChar(codeline, 0, '{') ? STRUCT_DEFN : OUTSIDE;
                continue;
            case STRUCT_DEFN: {
                // 步骤2.3：在STRUCT_DEFN状态，处理结构体定义内容
                std::string_view structName;
                // 步骤2.3.1：检查是否为结构体结束行"} StructIdentifier;"
                if (getString(codeline, &cursor, "}")) {
                    if (!getWhitespace(codeline, &cursor) ||
                            !getId(codeline, &cursor, &structName) ||
                            !getLastChar(codeline, cursor, ';')) {
                        break;
                    }
                    // 步骤2.3.2：找到结构体结束，为所有字段生成短名称并建立映射
                    const std::string structNamePrefix = std::string(structName) + ".";
                    std::string generatedFieldName = "a";
                    for (auto field : currentStructFields) {
                        const std::string sField(field);
                        // 步骤2.3.2.1：构建完整字段名（结构体名.字段名）
                        const std::string key = structNamePrefix + sField;
                        const std::string val = structNamePrefix + generatedFieldName;
                        // 步骤2.3.2.2：添加到字段映射表（用于替换使用处）
                        mStructFieldMap.push_back({key, val});
                        // 步骤2.3.2.3：添加到定义映射表（用于替换定义处）
                        mStructDefnMap[currentStructPrefix + sField] = generatedFieldName;
                        // 步骤2.3.2.4：生成下一个字段名（a, b, c, ..., z, aa, ab, ...）
                        if (generatedFieldName[0] == 'z') {
                            generatedFieldName = "a" + generatedFieldName;
                        } else {
                            generatedFieldName[0]++;
                        }
                    }
                    currentStructFields.clear();
                    state = OUTSIDE;
                    break;
                }
                // 步骤2.3.3：解析结构体字段定义行"OptionalPrecQual TypeIdentifier FieldIdentifier OptionalArraySize;"
                ignorePrecision(codeline, &cursor);
                if (!getId(codeline, &cursor, &typeId) ||
                        !getWhitespace(codeline, &cursor) ||
                        !getId(codeline, &cursor, &fieldName) ||
                        !ignoreArraySize(codeline, &cursor) ||
                        !getLastChar(codeline, cursor, ';')) {
                    break;
                }
                // 步骤2.3.4：将字段名添加到当前结构体字段列表
                currentStructFields.push_back(fieldName);
                break;
            }
        }
    }

    // Sort keys from longest to shortest because we want to replace "fogColorFromIbl" before
    // replacing "fogColor".
    // 步骤4：对映射表按键长度排序（从长到短，避免替换冲突）
    // 例如：先替换"fogColorFromIbl"再替换"fogColor"，避免部分匹配问题
    const auto& compare = [](const RenameEntry& a, const RenameEntry& b) {
        return a.first.length() > b.first.length();
    };
    std::sort(mStructFieldMap.begin(), mStructFieldMap.end(), compare);
}

// 应用字段映射（使用映射表替换所有字段名）
// @return 应用映射后的字符串
// 步骤1：初始化状态和结果字符串
// 步骤2：遍历代码行，使用状态机处理
// 步骤3：在OUTSIDE状态，替换所有字段使用处
// 步骤4：在STRUCT_DEFN状态，替换字段定义处的字段名
std::string ShaderMinifier::applyFieldMapping() const {
    // 步骤1：初始化状态和结果字符串
    std::string result;
    ParserState state = OUTSIDE;
    std::string currentStructPrefix;
    
    // 步骤2：遍历代码行，使用状态机处理
    for (std::string_view codeline : mCodelines) {
        std::string modified(codeline);
        std::string_view fieldName;
        std::string_view structName;
        std::string_view typeId;
        size_t cursor = 0;
        switch (state) {
            case OUTSIDE: {
                // 步骤2.1：在OUTSIDE状态，检查是否为uniform声明行
                if (getString(codeline, &cursor, "uniform") &&
                        getWhitespace(codeline, &cursor) &&
                        getId(codeline, &cursor, &typeId) &&
                        cursor == codeline.size()) {
                    // 步骤2.1.1：找到uniform声明，保存类型前缀并进入STRUCT_OPEN状态
                    currentStructPrefix = std::string(typeId) + ".";
                    state = STRUCT_OPEN;
                    break;
                }
                // 步骤3：在OUTSIDE状态，替换所有字段使用处（使用完整字段名映射）
                for (const auto& key : mStructFieldMap) {
                    replaceAll(modified, key.first, key.second);
                }
                break;
            }
            case STRUCT_OPEN:
                // 步骤2.2：在STRUCT_OPEN状态，查找'{'行
                state = getLastChar(codeline, 0, '{') ? STRUCT_DEFN : OUTSIDE;
                break;
            case STRUCT_DEFN: {
                // 步骤2.3：在STRUCT_DEFN状态，处理结构体定义内容
                // 步骤2.3.1：检查是否为结构体结束行"} StructIdentifier;"
                if (getString(codeline, &cursor, "}") &&
                        getWhitespace(codeline, &cursor) &&
                        getId(codeline, &cursor, &structName) &&
                        getLastChar(codeline, cursor, ';')) {
                    state = OUTSIDE;
                    break;
                }
                // 步骤2.3.2：解析结构体字段定义行
                ignorePrecision(codeline, &cursor);
                if (!getId(codeline, &cursor, &typeId) ||
                        !getWhitespace(codeline, &cursor) ||
                        !getId(codeline, &cursor, &fieldName) ||
                        !ignoreArraySize(codeline, &cursor) ||
                        !getLastChar(codeline, cursor, ';')) {
                    break;
                }
                // 步骤4：在STRUCT_DEFN状态，替换字段定义处的字段名
                // 步骤4.1：构建字段键（类型前缀+字段名）
                std::string key = currentStructPrefix + std::string(fieldName);
                // 步骤4.2：查找映射表中的新字段名
                auto iter = mStructDefnMap.find(key);
                if (iter == mStructDefnMap.end()) {
                    utils::slog.e << "ShaderMinifier error: " << key << utils::io::endl;
                    break;
                }
                // 步骤4.3：替换字段定义处的字段名为短名称
                replaceAll(modified, fieldName, iter->second);
                break;
            }
        }
        // 步骤2.4：将处理后的行添加到结果
        result += modified + "\n";
    }
    return result;
}

} // namespace filamat
