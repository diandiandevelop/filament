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
#ifndef TNT_FILAMENT_MATERIALCACHE_H
#define TNT_FILAMENT_MATERIALCACHE_H

#include "MaterialDefinition.h"

#include <private/filament/Variant.h>

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/Program.h>

#include <utils/Invocable.h>
#include <utils/RefCountedMap.h>

namespace filament {

class Material;

/**
 * MaterialCache 类
 * 
 * 管理 MaterialDefinition 对象的缓存，使用 MaterialParser 作为键。
 * 通过引用计数管理材质定义的生存期。
 */
class MaterialCache {
    /**
     * 键结构
     * 
     * 围绕 MaterialParser 的新类型，用作材质缓存的键。
     * 使用材质文件的 CRC32 作为哈希函数。
     */
    // A newtype around a material parser used as a key for the material cache. The material file's
    // CRC32 is used as the hash function.
    struct Key {
        /**
         * 哈希函数
         */
        struct Hash {
            /**
             * 计算键的哈希值
             * 
             * @param x 键引用
             * @return 哈希值
             */
            size_t operator()(Key const& x) const noexcept;
        };
        /**
         * 相等比较运算符
         * 
         * @param rhs 右侧键
         * @return 如果相等则返回 true
         */
        bool operator==(Key const& rhs) const noexcept;

        /**
         * 材质解析器指针（非空）
         * 
         * 用作缓存键的材质解析器。通过比较解析器的相等性
         * 来确定材质是否已缓存。
         */
        MaterialParser const* UTILS_NONNULL parser;  // 材质解析器指针
    };

public:
    /**
     * 析构函数
     */
    ~MaterialCache();

    /**
     * 获取或创建缓存条目
     * 
     * 为给定的材质数据获取或创建缓存中的新条目。
     * 如果材质已存在，增加引用计数；否则创建新条目。
     * 
     * @param engine 引擎引用
     * @param data 材质数据指针（非空）
     * @param size 材质数据大小
     * @return 材质定义指针（如果失败则返回 nullptr）
     */
    // Acquire or create a new entry in the cache for the given material data.
    MaterialDefinition* UTILS_NULLABLE acquire(FEngine& engine, const void* UTILS_NONNULL data,
            size_t size) noexcept;

    /**
     * 释放缓存条目
     * 
     * 释放缓存中的条目，可能释放其 GPU 资源。
     * 当引用计数降为 0 时，材质定义将被销毁。
     * 
     * @param engine 引擎引用
     * @param definition 材质定义引用
     */
    // Release an entry in the cache, potentially freeing its GPU resources.
    void release(FEngine& engine, MaterialDefinition const& definition) noexcept;

private:
    /**
     * 材质定义映射
     * 
     * 使用 unique_ptr 因为需要这些指针保持稳定。
     * TODO: 考虑使用自定义分配器？
     */
    // We use unique_ptr here because we need these pointers to be stable.
    // TODO: investigate using a custom allocator here?
    utils::RefCountedMap<Key, std::unique_ptr<MaterialDefinition>, Key::Hash> mDefinitions;
};

} // namespace filament

#endif  // TNT_FILAMENT_MATERIALCACHE_H
