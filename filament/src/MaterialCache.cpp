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

#include "MaterialCache.h"
#include "MaterialParser.h"

#include <backend/DriverEnums.h>

#include <details/Engine.h>
#include <details/Material.h>

#include <utils/Logger.h>

namespace filament {

/**
 * 键哈希函数
 * 
 * 计算材质缓存键的哈希值。
 * 优先使用预计算的 CRC32，否则计算 CRC32。
 * 
 * @param key 材质缓存键
 * @return 哈希值
 */
size_t MaterialCache::Key::Hash::operator()(
        filament::MaterialCache::Key const& key) const noexcept {
    /**
     * 尝试获取预计算的 CRC32
     */
    uint32_t crc;
    if (key.parser->getMaterialCrc32(&crc)) {
        /**
         * 如果存在预计算的 CRC32，使用它
         */
        return size_t(crc);
    }
    /**
     * 否则计算 CRC32
     */
    return size_t(key.parser->computeCrc32());
}

/**
 * 键相等比较操作符
 * 
 * 比较两个材质缓存键是否相等。
 * 
 * @param rhs 右侧键
 * @return 如果键相等则返回 true
 */
bool MaterialCache::Key::operator==(Key const& rhs) const noexcept {
    /**
     * 通过比较解析器指针判断是否相等
     */
    return parser == rhs.parser;
}

MaterialCache::~MaterialCache() {
    if (!mDefinitions.empty()) {
        LOG(WARNING) << "MaterialCache was destroyed but wasn't empty";
    }
}

/**
 * 获取材质定义
 * 
 * 从缓存中获取或创建材质定义。
 * 如果缓存中已存在相同的材质，则返回缓存的版本；否则创建新的并缓存。
 * 
 * @param engine 引擎引用
 * @param data 材质数据指针
 * @param size 材质数据大小
 * @return 材质定义指针，如果失败则返回 nullptr
 */
MaterialDefinition* UTILS_NULLABLE MaterialCache::acquire(FEngine& engine,
        const void* UTILS_NONNULL data, size_t size) noexcept {
    /**
     * 创建材质解析器
     */
    std::unique_ptr<MaterialParser> parser = MaterialDefinition::createParser(engine.getBackend(),
            engine.getShaderLanguage(), data, size);
    /**
     * 确保解析器创建成功
     */
    assert_invariant(parser);

    // The `key` must be constructed using parser.get() before parser is moved into the lambda
    // function. This prevents a potential crash or undefined behavior, as accessing a moved-from
    // object is unsafe. The validity of the generated key is guaranteed because the
    // MaterialDefinition object (which owns the same parser object) created within the lambda is
    // subsequently used as the associated value in the map.
    /**
     * 在将 parser 移动到 lambda 之前构造 key
     * 这防止了潜在的崩溃或未定义行为，因为访问已移动的对象是不安全的
     */
    const Key key{ parser.get() };

    /**
     * 从缓存中获取或创建材质定义
     * 如果缓存中不存在，则使用 lambda 创建新的材质定义
     */
    return mDefinitions.acquire(key, [&engine, parser = std::move(parser)]() mutable {
        return MaterialDefinition::create(engine, std::move(parser));
    });
}

/**
 * 释放材质定义
 * 
 * 减少材质定义的引用计数。
 * 如果引用计数为 0，则终止材质定义。
 * 
 * @param engine 引擎引用
 * @param definition 要释放的材质定义引用
 */
void MaterialCache::release(FEngine& engine, MaterialDefinition const& definition) noexcept {
    /**
     * 从缓存中释放材质定义
     * 如果引用计数为 0，则调用终止函数
     */
    mDefinitions.release(Key{ &definition.getMaterialParser() },
            [&engine](MaterialDefinition& definition) {
                /**
                 * 终止材质定义（清理资源）
                 */
                definition.terminate(engine);
            });
}

} // namespace filament
