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

#ifndef TNT_ENUMMANAGER_H
#define TNT_ENUMMANAGER_H

#include <algorithm>
#include <string>
#include <unordered_map>

#include <filamat/MaterialBuilder.h>

namespace filamat {

// 类型别名，简化枚举类型的引用
using Property = MaterialBuilder::Property;                    // 材质属性类型
using UniformType = MaterialBuilder::UniformType;              // 统一变量类型
using SamplerType = MaterialBuilder::SamplerType;              // 采样器类型
using SubpassType = MaterialBuilder::SubpassType;              // 子通道类型
using SamplerFormat = MaterialBuilder::SamplerFormat;          // 采样器格式
using ParameterPrecision = MaterialBuilder::ParameterPrecision; // 参数精度
using OutputTarget = MaterialBuilder::OutputTarget;            // 输出目标
using OutputQualifier = MaterialBuilder::VariableQualifier;    // 输出限定符
using OutputType = MaterialBuilder::OutputType;                // 输出类型
using ConstantType = MaterialBuilder::ConstantType;            // 常量类型
using ShaderStageType = MaterialBuilder::ShaderStageFlags;     // 着色器阶段类型

// Convenience methods to convert std::string to Enum and also iterate over Enum values.
// 用于在std::string和枚举之间转换以及遍历枚举值的便利方法
class Enums {
public:

    // Returns true if string "s" is a valid string representation of an element of enum T.
    // 如果字符串"s"是枚举T的有效字符串表示，则返回true
    template<typename T>
    static bool isValid(const std::string& s) noexcept {
        std::unordered_map<std::string, T>& map = getMap<T>();
        return map.find(s) != map.end();
    }

    // Return enum matching its string representation. Returns undefined if s is not a valid enum T
    // value. You should always call isValid() first to validate a string before calling toEnum().
    // 返回与其字符串表示匹配的枚举。如果s不是有效的枚举T值，则返回未定义值。
    // 在调用toEnum()之前，应始终先调用isValid()来验证字符串
    template<typename T>
    static T toEnum(const std::string& s) noexcept {
        std::unordered_map<std::string, T>& map = getMap<T>();
        return map.at(s);
    }

    // 将枚举值转换为字符串表示
    template<typename T>
    static std::string toString(T t) noexcept;

    // Return a map of all values in an enum with their string representation.
    // 返回枚举中所有值及其字符串表示的映射
    template<typename T>
    static std::unordered_map<std::string, T>& map() noexcept {
        std::unordered_map<std::string, T>& map = getMap<T>();
        return map;
    };

private:
    // 获取指定枚举类型的字符串到枚举值映射表
    template<typename T>
    static std::unordered_map<std::string, T>& getMap() noexcept;

    // 各种枚举类型的字符串到枚举值映射表
    static std::unordered_map<std::string, Property> mStringToProperty;                    // 属性映射
    static std::unordered_map<std::string, UniformType> mStringToUniformType;              // 统一变量类型映射
    static std::unordered_map<std::string, SamplerType> mStringToSamplerType;              // 采样器类型映射
    static std::unordered_map<std::string, SubpassType> mStringToSubpassType;              // 子通道类型映射
    static std::unordered_map<std::string, SamplerFormat> mStringToSamplerFormat;          // 采样器格式映射
    static std::unordered_map<std::string, ParameterPrecision> mStringToSamplerPrecision;  // 采样器精度映射
    static std::unordered_map<std::string, OutputTarget> mStringToOutputTarget;            // 输出目标映射
    static std::unordered_map<std::string, OutputQualifier> mStringToOutputQualifier;      // 输出限定符映射
    static std::unordered_map<std::string, OutputType> mStringToOutputType;                // 输出类型映射
    static std::unordered_map<std::string, ConstantType> mStringToConstantType;            // 常量类型映射
    static std::unordered_map<std::string, ShaderStageType> mStringToShaderStageType;      // 着色器阶段类型映射
};

// 将枚举值转换为字符串表示（模板实现）
template<typename T>
std::string Enums::toString(T t) noexcept {
    std::unordered_map<std::string, T>& map = getMap<T>();
    // 查找匹配的枚举值
    auto result = std::find_if(map.begin(), map.end(), [t](auto& pair) {
        return pair.second == t;
    });
    if (result != map.end()) {
        return result->first;  // 返回对应的字符串
    }
    return "";  // 未找到时返回空字符串
}

} // namespace filamat

#endif //TNT_ENUMMANAGER_H
