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

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

namespace filament {

using namespace math;

/**
 * 检查属性是否存在
 * 
 * @param name 属性名称
 * @return 如果属性存在则返回 true
 */
bool DebugRegistry::hasProperty(const char* name) const noexcept {
    return downcast(this)->hasProperty(name);
}

/**
 * 设置布尔属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, bool const v) noexcept {
    return downcast(this)->setProperty(name, v);
}

/**
 * 设置整数属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, int const v) noexcept {
    return downcast(this)->setProperty(name, v);
}

/**
 * 设置浮点数属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, float const v) noexcept {
    return downcast(this)->setProperty(name, v);
}

/**
 * 设置 float2 属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, float2 const v) noexcept {
    return downcast(this)->setProperty(name, v);
}

/**
 * 设置 float3 属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, float3 const v) noexcept {
    return downcast(this)->setProperty(name, v);
}

/**
 * 设置 float4 属性
 * 
 * @param name 属性名称
 * @param v 属性值
 * @return 如果设置成功则返回 true
 */
bool DebugRegistry::setProperty(const char* name, float4 const v) noexcept {
    return downcast(this)->setProperty(name, v);
}


/**
 * 获取布尔属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, bool* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取整数属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, int* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取浮点数属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, float* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取 float2 属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, float2* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取 float3 属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, float3* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取 float4 属性
 * 
 * @param name 属性名称
 * @param v 输出值指针
 * @return 如果获取成功则返回 true
 */
bool DebugRegistry::getProperty(const char* name, float4* v) const noexcept {
    return downcast(this)->getProperty(name, v);
}

/**
 * 获取属性地址（非 const 版本）
 * 
 * @param name 属性名称
 * @return 属性地址指针，如果不存在则返回 nullptr
 */
void *DebugRegistry::getPropertyAddress(const char *name) {
    return  downcast(this)->getPropertyAddress(name);
}

/**
 * 获取属性地址（const 版本）
 * 
 * @param name 属性名称
 * @return 属性地址指针，如果不存在则返回 nullptr
 */
void const *DebugRegistry::getPropertyAddress(const char *name) const noexcept {
    return  downcast(this)->getPropertyAddress(name);
}

/**
 * 获取属性数据源
 * 
 * @param name 属性名称
 * @return 数据源类型
 */
DebugRegistry::DataSource DebugRegistry::getDataSource(const char* name) const noexcept {
    return  downcast(this)->getDataSource(name);
}


} // namespace filament

