/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "HwVertexBufferInfoFactory.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/backend/DriverApi.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Hash.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

namespace filament {

using namespace utils;
using namespace backend;

/**
 * 计算参数哈希值
 * 
 * 使用 MurmurHash3 算法计算顶点缓冲区信息参数的哈希值。
 * 将整个 Parameters 结构体作为字节数组进行哈希。
 * 
 * @return 哈希值
 */
size_t HwVertexBufferInfoFactory::Parameters::hash() const noexcept {
    /**
     * 确保结构体大小是 uint32_t 的倍数（用于对齐）
     */
    static_assert((sizeof(*this) % sizeof(uint32_t)) == 0);
    /**
     * 使用 MurmurHash3 计算哈希
     * - 数据：将结构体转换为 uint32_t 数组
     * - 大小：结构体大小 / uint32_t 大小（uint32_t 数量）
     * - 种子：0
     */
    return hash::murmur3(
            reinterpret_cast<uint32_t const*>(this), sizeof(Parameters) / sizeof(uint32_t), 0);
}

bool operator==(HwVertexBufferInfoFactory::Parameters const& lhs,
        HwVertexBufferInfoFactory::Parameters const& rhs) noexcept {
    return !memcmp(&lhs, &rhs, sizeof(HwVertexBufferInfoFactory::Parameters));
}

// ------------------------------------------------------------------------------------------------

/**
 * HwVertexBufferInfoFactory 构造函数
 * 
 * 初始化顶点缓冲区信息工厂，创建内存池和双向映射。
 */
HwVertexBufferInfoFactory::HwVertexBufferInfoFactory()
        : mArena("HwVertexBufferInfoFactory::mArena", SET_ARENA_SIZE),  // 创建内存池
          mBimap(mArena) {  // 创建双向映射（使用内存池）
    /**
     * 预分配双向映射的容量（256 个条目）
     */
    mBimap.reserve(256);
}

HwVertexBufferInfoFactory::~HwVertexBufferInfoFactory() noexcept = default;

void HwVertexBufferInfoFactory::terminate(DriverApi&) noexcept {
    assert_invariant(mBimap.empty());
}

/**
 * 创建顶点缓冲区信息
 * 
 * 创建或重用顶点缓冲区信息。如果相同的信息已存在，则增加引用计数并返回现有句柄。
 * 
 * @param driver 驱动 API 引用
 * @param bufferCount 缓冲区数量
 * @param attributeCount 属性数量
 * @param attributes 属性数组
 * @return 顶点缓冲区信息句柄
 */
auto HwVertexBufferInfoFactory::create(DriverApi& driver,
        uint8_t const bufferCount,
        uint8_t const attributeCount,
        AttributeArray attributes) noexcept -> Handle {

    /**
     * 创建查找键（包含缓冲区数量、属性数量和属性数组）
     */
    Key const key({ bufferCount, attributeCount, {}, attributes });
    /**
     * 在双向映射中查找是否已存在相同的信息
     */
    auto pos = mBimap.find(key);

    // the common case is that we've never seen it (i.e.: no reuse)
    /**
     * 如果未找到（常见情况：无重用）
     */
    if (UTILS_LIKELY(pos == mBimap.end())) {
        /**
         * 创建新的顶点缓冲区信息
         */
        auto handle = driver.createVertexBufferInfo(
                bufferCount, attributeCount,
                attributes);
        /**
         * 将新信息插入双向映射
         */
        mBimap.insert(key, { handle });
        return handle;
    }

    /**
     * 找到现有信息，增加引用计数
     */
    ++(pos->first.pKey->refs);
    /**
     * 返回现有句柄
     */
    return pos->second.handle;
}

void HwVertexBufferInfoFactory::destroy(DriverApi& driver, Handle handle) noexcept {
    // look for this handle in our map
    auto pos = mBimap.findValue(Value{ handle });
    if (--(pos->second.pKey->refs) == 0) {
        mBimap.erase(pos);
        driver.destroyVertexBufferInfo(handle);
    }
}

} // namespace filament
