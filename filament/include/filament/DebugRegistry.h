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

//! \file

#ifndef TNT_FILAMENT_DEBUGREGISTRY_H
#define TNT_FILAMENT_DEBUGREGISTRY_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>

#include <math/mathfwd.h>

#include <stddef.h>

namespace filament {

/**
 * A registry of runtime properties used exclusively for debugging
 *
 * Filament exposes a few properties that can be queried and set, which control certain debugging
 * features of the engine. These properties can be set at runtime at anytime.
 *
 */
/**
 * 用于调试的运行时属性注册表
 *
 * Filament 公开了一些可以查询和设置的属性，这些属性控制引擎的某些调试
 * 功能。这些属性可以在运行时随时设置。
 */
class UTILS_PUBLIC DebugRegistry : public FilamentAPI {
public:

    /**
     * Queries whether a property exists
     * @param name The name of the property to query
     * @return true if the property exists, false otherwise
     */
    /**
     * 查询属性是否存在
     * @param name 要查询的属性名称
     * @return 如果属性存在则返回 true，否则返回 false
     */
    bool hasProperty(const char* UTILS_NONNULL name) const noexcept;

    /**
     * Queries the address of a property's data from its name
     * @param name Name of the property we want the data address of
     * @return Address of the data of the \p name property
     * @{
     */
    /**
     * 从属性名称查询属性数据的地址
     * @param name 要获取数据地址的属性名称
     * @return \p name 属性的数据地址
     * @{
     */
    void* UTILS_NULLABLE getPropertyAddress(const char* UTILS_NONNULL name);

    void const* UTILS_NULLABLE getPropertyAddress(const char* UTILS_NONNULL name) const noexcept;

    template<typename T>
    inline T* UTILS_NULLABLE getPropertyAddress(const char* UTILS_NONNULL name) {
        return static_cast<T*>(getPropertyAddress(name));
    }
    /**
     * 模板版本：返回指定类型的属性数据地址
     */

    template<typename T>
    inline T const* UTILS_NULLABLE getPropertyAddress(const char* UTILS_NONNULL name) const noexcept {
        return static_cast<T*>(getPropertyAddress(name));
    }
    /**
     * 模板版本（const）：返回指定类型的属性数据地址
     */

    template<typename T>
    inline bool getPropertyAddress(const char* UTILS_NONNULL name,
            T* UTILS_NULLABLE* UTILS_NONNULL p) {
        *p = getPropertyAddress<T>(name);
        return *p != nullptr;
    }
    /**
     * 模板版本：通过输出参数返回属性数据地址
     * @param name 属性名称
     * @param p 指向指针的指针，用于接收属性数据地址
     * @return 如果成功则返回 true，否则返回 false
     */

    template<typename T>
    inline bool getPropertyAddress(const char* UTILS_NONNULL name,
            T* const UTILS_NULLABLE* UTILS_NONNULL p) const noexcept {
        *p = getPropertyAddress<T>(name);
        return *p != nullptr;
    }
    /**
     * 模板版本（const）：通过输出参数返回属性数据地址
     */
    /** @}*/

    /**
     * Set the value of a property
     * @param name Name of the property to set the value of
     * @param v Value to set
     * @return true if the operation was successful, false otherwise.
     * @{
     */
    /**
     * 设置属性的值
     * @param name 要设置值的属性名称
     * @param v 要设置的值
     * @return 如果操作成功则返回 true，否则返回 false
     * @{
     */
    bool setProperty(const char* UTILS_NONNULL name, bool v) noexcept;        //!< 设置 bool 类型属性
    bool setProperty(const char* UTILS_NONNULL name, int v) noexcept;          //!< 设置 int 类型属性
    bool setProperty(const char* UTILS_NONNULL name, float v) noexcept;        //!< 设置 float 类型属性
    bool setProperty(const char* UTILS_NONNULL name, math::float2 v) noexcept; //!< 设置 float2 类型属性
    bool setProperty(const char* UTILS_NONNULL name, math::float3 v) noexcept; //!< 设置 float3 类型属性
    bool setProperty(const char* UTILS_NONNULL name, math::float4 v) noexcept; //!< 设置 float4 类型属性
    /** @}*/

    /**
     * Get the value of a property
     * @param name Name of the property to get the value of
     * @param v A pointer to a variable which will hold the result
     * @return true if the call was successful and \p v was updated
     * @{
     */
    /**
     * 获取属性的值
     * @param name 要获取值的属性名称
     * @param v 指向将保存结果的变量的指针
     * @return 如果调用成功且 \p v 已更新则返回 true
     * @{
     */
    bool getProperty(const char* UTILS_NONNULL name, bool* UTILS_NONNULL v) const noexcept;        //!< 获取 bool 类型属性
    bool getProperty(const char* UTILS_NONNULL name, int* UTILS_NONNULL v) const noexcept;          //!< 获取 int 类型属性
    bool getProperty(const char* UTILS_NONNULL name, float* UTILS_NONNULL v) const noexcept;        //!< 获取 float 类型属性
    bool getProperty(const char* UTILS_NONNULL name, math::float2* UTILS_NONNULL v) const noexcept; //!< 获取 float2 类型属性
    bool getProperty(const char* UTILS_NONNULL name, math::float3* UTILS_NONNULL v) const noexcept; //!< 获取 float3 类型属性
    bool getProperty(const char* UTILS_NONNULL name, math::float4* UTILS_NONNULL v) const noexcept; //!< 获取 float4 类型属性
    /** @}*/

    /**
     * 数据源结构，包含数据指针和数量
     */
    struct DataSource {
        void const* UTILS_NULLABLE data;  //!< 数据指针
        size_t count;                      //!< 数据数量
    };

    /**
     * 获取指定属性的数据源
     * @param name 属性名称
     * @return 数据源结构
     */
    DataSource getDataSource(const char* UTILS_NONNULL name) const noexcept;

    /**
     * 帧历史记录结构，用于存储帧时间相关信息
     */
    struct FrameHistory {
        using duration_ms = float;        //!< 持续时间类型（毫秒）
        duration_ms target{};             //!< 目标帧时间
        duration_ms targetWithHeadroom{}; //!< 带余量的目标帧时间
        duration_ms frameTime{};          //!< 实际帧时间
        duration_ms frameTimeDenoised{};  //!< 去噪后的帧时间
        float scale = 1.0f;               //!< 缩放因子
        float pid_e = 0.0f;               //!< PID 控制器的误差项
        float pid_i = 0.0f;               //!< PID 控制器的积分项
        float pid_d = 0.0f;               //!< PID 控制器的微分项
    };

protected:
    // prevent heap allocation
    ~DebugRegistry() = default;
};


} // namespace filament

#endif /* TNT_FILAMENT_DEBUGREGISTRY_H */
