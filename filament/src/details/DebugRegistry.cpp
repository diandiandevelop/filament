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

#include "details/DebugRegistry.h"

#include <utils/compiler.h>
#include <utils/Invocable.h>
#include <utils/Panic.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <functional>
#include <string_view>
#include <utility>

using namespace filament::math;
using namespace utils;

namespace filament {

/**
 * 构造函数
 * 
 * 创建调试注册表，初始化为空。
 */
FDebugRegistry::FDebugRegistry() noexcept = default;

/**
 * 获取属性信息
 * 
 * 根据属性名称查找属性信息（指针和回调函数）。
 * 
 * @param name 属性名称
 * @return 属性信息对（指针和回调函数），如果未找到返回 {nullptr, {}}）
 */
auto FDebugRegistry::getPropertyInfo(const char* name) noexcept -> PropertyInfo {
    std::string_view const key{ name };  // 创建字符串视图
    auto& propertyMap = mPropertyMap;  // 获取属性映射表引用
    if (propertyMap.find(key) == propertyMap.end()) {  // 如果未找到
        return { nullptr, {} };  // 返回空信息
    }
    return propertyMap[key];  // 返回属性信息
}

/**
 * 获取属性地址（非常量版本）
 * 
 * 获取属性的内存地址。如果属性有回调函数，不能使用此方法。
 * 
 * @param name 属性名称
 * @return 属性地址指针，如果未找到或设置了回调返回 nullptr
 */
UTILS_NOINLINE
void* FDebugRegistry::getPropertyAddress(const char* name) {
    auto info = getPropertyInfo(name);  // 获取属性信息
    ASSERT_PRECONDITION_NON_FATAL(!info.second,  // 断言没有回调函数
            "don't use DebugRegistry::getPropertyAddress() when a callback is set. "
            "Use setProperty() instead.");
    return info.first;  // 返回属性指针
}

/**
 * 获取属性地址（常量版本）
 * 
 * 获取属性的内存地址（只读）。
 * 
 * @param name 属性名称
 * @return 属性地址常量指针，如果未找到返回 nullptr
 */
UTILS_NOINLINE
void const* FDebugRegistry::getPropertyAddress(const char* name) const noexcept {
    auto info = const_cast<FDebugRegistry*>(this)->getPropertyInfo(name);  // 获取属性信息（需要非常量版本）
    return info.first;  // 返回属性指针
}

/**
 * 注册属性（内部方法）
 * 
 * 将属性注册到调试注册表中。
 * 
 * @param name 属性名称
 * @param p 属性指针
 * @param type 属性类型（未使用，保留用于类型检查）
 * @param fn 属性变更回调函数（可选）
 */
void FDebugRegistry::registerProperty(std::string_view const name, void* p, Type,
        std::function<void()> fn) noexcept {
    auto& propertyMap = mPropertyMap;  // 获取属性映射表引用
    if (propertyMap.find(name) == propertyMap.end()) {  // 如果属性未注册
        propertyMap[name] = { p, std::move(fn) };  // 注册属性（移动回调函数）
    }
}

/**
 * 检查是否有属性
 * 
 * @param name 属性名称
 * @return 如果属性存在返回 true，否则返回 false
 */
bool FDebugRegistry::hasProperty(const char* name) const noexcept {
    return getPropertyAddress(name) != nullptr;  // 检查属性地址是否有效
}

/**
 * 设置属性值（模板方法）
 * 
 * 设置指定属性的值。如果属性有回调函数且值发生变化，会调用回调函数。
 * 
 * @tparam T 属性类型（bool、int、float、float2、float3、float4）
 * @param name 属性名称
 * @param v 属性值
 * @return 如果属性存在并设置成功返回 true，否则返回 false
 */
template<typename T>
bool FDebugRegistry::setProperty(const char* name, T v) noexcept {
    auto info = getPropertyInfo(name);  // 获取属性信息
    T* const addr = static_cast<T*>(info.first);  // 转换为类型化指针
    if (addr) {  // 如果属性存在
        auto old = *addr;  // 保存旧值
        *addr = v;  // 设置新值
        if (info.second && old != v) {  // 如果有回调函数且值发生变化
            info.second();  // 调用回调函数
        }
        return true;  // 设置成功
    }
    return false;  // 属性不存在
}

template bool FDebugRegistry::setProperty<bool>(const char* name, bool v) noexcept;
template bool FDebugRegistry::setProperty<int>(const char* name, int v) noexcept;
template bool FDebugRegistry::setProperty<float>(const char* name, float v) noexcept;
template bool FDebugRegistry::setProperty<float2>(const char* name, float2 v) noexcept;
template bool FDebugRegistry::setProperty<float3>(const char* name, float3 v) noexcept;
template bool FDebugRegistry::setProperty<float4>(const char* name, float4 v) noexcept;

/**
 * 获取属性值（模板方法）
 * 
 * 获取指定属性的值。
 * 
 * @tparam T 属性类型（bool、int、float、float2、float3、float4）
 * @param name 属性名称
 * @param p 输出指针
 * @return 如果属性存在并获取成功返回 true，否则返回 false
 */
template<typename T>
bool FDebugRegistry::getProperty(const char* name, T* p) const noexcept {
    T const* const addr = static_cast<T const*>(getPropertyAddress(name));  // 获取属性地址并转换为类型化指针
    if (addr) {  // 如果属性存在
        *p = *addr;  // 复制属性值
        return true;  // 获取成功
    }
    return false;  // 属性不存在
}

template bool FDebugRegistry::getProperty<bool>(const char* name, bool* v) const noexcept;
template bool FDebugRegistry::getProperty<int>(const char* name, int* v) const noexcept;
template bool FDebugRegistry::getProperty<float>(const char* name, float* v) const noexcept;
template bool FDebugRegistry::getProperty<float2>(const char* name, float2* v) const noexcept;
template bool FDebugRegistry::getProperty<float3>(const char* name, float3* v) const noexcept;
template bool FDebugRegistry::getProperty<float4>(const char* name, float4* v) const noexcept;

/**
 * 注册数据源（直接）
 * 
 * 直接注册数据源，数据必须立即可用。
 * 
 * @param name 数据源名称
 * @param data 数据指针
 * @param count 数据元素数量
 * @return 如果注册成功返回 true（名称未被占用），否则返回 false
 */
bool FDebugRegistry::registerDataSource(std::string_view const name,
        void const* data, size_t const count) noexcept {
    auto& dataSourceMap = mDataSourceMap;  // 获取数据源映射表引用
    bool const found = dataSourceMap.find(name) == dataSourceMap.end();  // 检查名称是否已被占用
    if (found) {  // 如果名称未被占用
        dataSourceMap[name] = { data, count };  // 注册数据源
    }
    return found;  // 返回是否注册成功
}

/**
 * 注册数据源（延迟）
 * 
 * 延迟注册数据源，数据在需要时通过创建器函数获取。
 * 第一次访问时会调用创建器函数，之后数据会被缓存。
 * 
 * @param name 数据源名称
 * @param creator 数据源创建器函数（返回 DataSource）
 * @return 如果注册成功返回 true（名称未被占用），否则返回 false
 */
bool FDebugRegistry::registerDataSource(std::string_view const name,
        Invocable<DataSource()>&& creator) noexcept {
    auto& dataSourceCreatorMap = mDataSourceCreatorMap;  // 获取数据源创建器映射表引用
    bool const found = dataSourceCreatorMap.find(name) == dataSourceCreatorMap.end();  // 检查名称是否已被占用
    if (found) {  // 如果名称未被占用
        dataSourceCreatorMap[name] = std::move(creator);  // 注册创建器函数（移动）
    }
    return found;  // 返回是否注册成功
}

/**
 * 取消注册数据源
 * 
 * 从两个映射表中移除数据源。
 * 
 * @param name 数据源名称
 */
void FDebugRegistry::unregisterDataSource(std::string_view const name) noexcept {
    mDataSourceCreatorMap.erase(name);  // 从创建器映射表中移除
    mDataSourceMap.erase(name);  // 从数据源映射表中移除
}

/**
 * 获取数据源
 * 
 * 获取已注册的数据源。如果是延迟注册的数据源，第一次访问时会调用创建器函数。
 * 
 * 实现细节：
 * 1. 首先在直接数据源映射表中查找
 * 2. 如果未找到，在创建器映射表中查找
 * 3. 如果找到创建器，调用它创建数据源并缓存
 * 4. 从创建器映射表中移除（因为已经缓存）
 * 
 * @param name 数据源名称
 * @return 数据源（包含数据指针和数量），如果未找到返回 {nullptr, 0}
 */
DebugRegistry::DataSource FDebugRegistry::getDataSource(const char* name) const noexcept {
    std::string_view const key{ name };  // 创建字符串视图
    auto& dataSourceMap = mDataSourceMap;  // 获取数据源映射表引用
    auto const& it = dataSourceMap.find(key);  // 在直接数据源映射表中查找
    if (UTILS_UNLIKELY(it == dataSourceMap.end())) {  // 如果未找到（不常见情况）
        auto& dataSourceCreatorMap = mDataSourceCreatorMap;  // 获取创建器映射表引用
        auto const& pos = dataSourceCreatorMap.find(key);  // 在创建器映射表中查找
        if (pos == dataSourceCreatorMap.end()) {  // 如果也未找到
            return { nullptr, 0u };  // 返回空数据源
        }
        /**
         * 调用创建器函数创建数据源并缓存
         */
        DataSource dataSource{ pos->second() };  // 调用创建器函数
        dataSourceMap[key] = dataSource;  // 缓存到直接数据源映射表
        dataSourceCreatorMap.erase(pos);  // 从创建器映射表中移除（因为已经缓存）
        return dataSource;  // 返回创建的数据源
    }
    return it->second;  // 返回找到的数据源
}

} // namespace filament
