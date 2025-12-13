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

#ifndef TNT_FILAMENT_DETAILS_DEBUGREGISTRY_H
#define TNT_FILAMENT_DETAILS_DEBUGREGISTRY_H

#include "downcast.h"

#include <filament/DebugRegistry.h>

#include <utils/compiler.h>
#include <utils/Invocable.h>

#include <math/mathfwd.h>

#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <stddef.h>

namespace filament {

class FEngine;

/**
 * 调试注册表实现类
 * 
 * 提供调试属性的接口，允许运行时检查和修改。
 * 用于调试和性能分析。
 * 
 * 实现细节：
 * - 支持多种类型（bool、int、float、float2、float3、float4）
 * - 支持属性变更回调
 * - 支持数据源注册（直接和延迟）
 */
class FDebugRegistry : public DebugRegistry {
public:
    /**
     * 属性类型枚举
     */
    enum Type {
        BOOL,    // 布尔类型
        INT,     // 整数类型
        FLOAT,   // 浮点类型
        FLOAT2,  // 二维浮点向量
        FLOAT3,  // 三维浮点向量
        FLOAT4   // 四维浮点向量
    };

    /**
     * 构造函数
     */
    FDebugRegistry() noexcept;

    /**
     * 注册属性（bool 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, bool* p) noexcept {
        registerProperty(name, p, BOOL);  // 注册为布尔类型
    }

    /**
     * 注册属性（int 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, int* p) noexcept {
        registerProperty(name, p, INT);  // 注册为整数类型
    }

    /**
     * 注册属性（float 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, float* p) noexcept {
        registerProperty(name, p, FLOAT);  // 注册为浮点类型
    }

    /**
     * 注册属性（float2 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, math::float2* p) noexcept {
        registerProperty(name, p, FLOAT2);  // 注册为二维浮点向量类型
    }

    /**
     * 注册属性（float3 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, math::float3* p) noexcept {
        registerProperty(name, p, FLOAT3);  // 注册为三维浮点向量类型
    }

    /**
     * 注册属性（float4 版本）
     * 
     * @param name 属性名称
     * @param p 属性指针
     */
    void registerProperty(std::string_view const name, math::float4* p) noexcept {
        registerProperty(name, p, FLOAT4);  // 注册为四维浮点向量类型
    }


    /**
     * 注册属性（bool 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, bool* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, BOOL, std::move(fn));  // 注册为布尔类型，带回调
    }

    /**
     * 注册属性（int 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, int* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, INT, std::move(fn));  // 注册为整数类型，带回调
    }

    /**
     * 注册属性（float 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, float* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, FLOAT, std::move(fn));  // 注册为浮点类型，带回调
    }

    /**
     * 注册属性（float2 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, math::float2* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, FLOAT2, std::move(fn));  // 注册为二维浮点向量类型，带回调
    }

    /**
     * 注册属性（float3 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, math::float3* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, FLOAT3, std::move(fn));  // 注册为三维浮点向量类型，带回调
    }

    /**
     * 注册属性（float4 版本，带回调）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param fn 属性变更回调函数
     */
    void registerProperty(std::string_view const name, math::float4* p,
            std::function<void()> fn) noexcept {
        registerProperty(name, p, FLOAT4, std::move(fn));  // 注册为四维浮点向量类型，带回调
    }

    /**
     * 注册数据源（直接）
     * 
     * 直接注册数据源，数据必须立即可用。
     * 
     * @param name 数据源名称
     * @param data 数据指针
     * @param count 数据元素数量
     * @return 如果注册成功返回 true，否则返回 false
     */
    // registers a DataSource directly
    bool registerDataSource(std::string_view name, void const* data, size_t count) noexcept;

    /**
     * 注册数据源（延迟）
     * 
     * 延迟注册数据源，数据在需要时通过创建器函数获取。
     * 
     * @param name 数据源名称
     * @param creator 数据源创建器函数
     * @return 如果注册成功返回 true，否则返回 false
     */
    // registers a DataSource lazily
    bool registerDataSource(std::string_view name,
            utils::Invocable<DataSource()>&& creator) noexcept;

    /**
     * 取消注册数据源
     * 
     * @param name 数据源名称
     */
    void unregisterDataSource(std::string_view name) noexcept;

#if !defined(_MSC_VER)
private:
#endif
    /**
     * 获取属性（模板方法）
     * 
     * @tparam T 属性类型
     * @param name 属性名称
     * @param p 输出指针
     * @return 如果属性存在返回 true，否则返回 false
     */
    template<typename T> bool getProperty(const char* name, T* p) const noexcept;
    
    /**
     * 设置属性（模板方法）
     * 
     * @tparam T 属性类型
     * @param name 属性名称
     * @param v 属性值
     * @return 如果属性存在并设置成功返回 true，否则返回 false
     */
    template<typename T> bool setProperty(const char* name, T v) noexcept;

private:
    /**
     * 属性信息类型
     * 
     * 包含属性指针和变更回调函数。
     */
    using PropertyInfo = std::pair<void*, std::function<void()>>;
    
    friend class DebugRegistry;  // 允许 DebugRegistry 访问私有成员
    
    /**
     * 注册属性（内部方法）
     * 
     * @param name 属性名称
     * @param p 属性指针
     * @param type 属性类型
     * @param fn 属性变更回调函数（可选）
     */
    void registerProperty(std::string_view name, void* p, Type type, std::function<void()> fn = {}) noexcept;
    
    /**
     * 检查是否有属性
     * 
     * @param name 属性名称
     * @return 如果属性存在返回 true，否则返回 false
     */
    bool hasProperty(const char* name) const noexcept;
    
    /**
     * 获取属性信息
     * 
     * @param name 属性名称
     * @return 属性信息
     */
    PropertyInfo getPropertyInfo(const char* name) noexcept;
    
    /**
     * 获取属性地址（非常量版本）
     * 
     * @param name 属性名称
     * @return 属性地址指针
     */
    void* getPropertyAddress(const char* name);
    
    /**
     * 获取属性地址（常量版本）
     * 
     * @param name 属性名称
     * @return 属性地址常量指针
     */
    void const* getPropertyAddress(const char* name) const noexcept;
    
    /**
     * 获取数据源
     * 
     * @param name 数据源名称
     * @return 数据源
     */
    DataSource getDataSource(const char* name) const noexcept;
    
    /**
     * 属性映射表
     * 
     * 存储属性名称到属性信息的映射。
     */
    std::unordered_map<std::string_view, PropertyInfo> mPropertyMap;  // 属性映射表
    
    /**
     * 数据源映射表（可变）
     * 
     * 存储数据源名称到数据源的映射。
     */
    mutable std::unordered_map<std::string_view, DataSource> mDataSourceMap;  // 数据源映射表
    
    /**
     * 数据源创建器映射表（可变）
     * 
     * 存储数据源名称到创建器函数的映射。
     */
    mutable std::unordered_map<std::string_view, utils::Invocable<DataSource()>> mDataSourceCreatorMap;  // 数据源创建器映射表
};

FILAMENT_DOWNCAST(DebugRegistry)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_DEBUGREGISTRY_H
