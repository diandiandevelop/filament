/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "HwRenderPrimitiveFactory.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/backend/DriverApi.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Hash.h>

#include <stdlib.h>

namespace filament {

using namespace utils;
using namespace backend;

/**
 * 计算参数哈希值
 * 
 * 组合顶点缓冲区句柄、索引缓冲区句柄和图元类型的哈希值。
 * 
 * @return 哈希值
 */
size_t HwRenderPrimitiveFactory::Parameters::hash() const noexcept {
    /**
     * 组合哈希值：
     * 1. 索引缓冲区句柄 ID 和图元类型
     * 2. 再与顶点缓冲区句柄 ID 组合
     */
    return hash::combine(vbh.getId(),
            hash::combine(ibh.getId(),
                    (size_t)type));
}

bool operator==(HwRenderPrimitiveFactory::Parameters const& lhs,
        HwRenderPrimitiveFactory::Parameters const& rhs) noexcept {
    return lhs.vbh == rhs.vbh &&
           lhs.ibh == rhs.ibh &&
           lhs.type == rhs.type;
}

// ------------------------------------------------------------------------------------------------

/**
 * HwRenderPrimitiveFactory 构造函数
 * 
 * 初始化渲染图元工厂，创建内存池和双向映射。
 */
HwRenderPrimitiveFactory::HwRenderPrimitiveFactory()
        : mArena("HwRenderPrimitiveFactory::mArena", SET_ARENA_SIZE),  // 创建内存池
          mBimap(mArena) {  // 创建双向映射（使用内存池）
    /**
     * 预分配双向映射的容量（256 个条目）
     */
    mBimap.reserve(256);
}

HwRenderPrimitiveFactory::~HwRenderPrimitiveFactory() noexcept = default;

void HwRenderPrimitiveFactory::terminate(DriverApi&) noexcept {
    assert_invariant(mBimap.empty());
}

/**
 * 创建渲染图元
 * 
 * 创建或重用渲染图元。如果相同的图元已存在，则增加引用计数并返回现有句柄。
 * 
 * @param driver 驱动 API 引用
 * @param vbh 顶点缓冲区句柄
 * @param ibh 索引缓冲区句柄
 * @param type 图元类型
 * @return 渲染图元句柄
 */
auto HwRenderPrimitiveFactory::create(DriverApi& driver,
        VertexBufferHandle vbh,
        IndexBufferHandle ibh,
        PrimitiveType const type) noexcept -> Handle {

    // see if we already have seen this RenderPrimitive
    /**
     * 创建查找键（包含顶点缓冲区、索引缓冲区和图元类型）
     */
    Key const key({ vbh, ibh, type });
    /**
     * 在双向映射中查找是否已存在相同的图元
     */
    auto pos = mBimap.find(key);

    // the common case is that we've never seen it (i.e.: no reuse)
    /**
     * 如果未找到（常见情况：无重用）
     */
    if (UTILS_LIKELY(pos == mBimap.end())) {
        /**
         * 创建新的渲染图元
         */
        auto handle = driver.createRenderPrimitive(vbh, ibh, type);
        /**
         * 将新图元插入双向映射
         */
        mBimap.insert(key, { handle });
        return handle;
    }

    /**
     * 找到现有图元，增加引用计数
     */
    ++(pos->first.pKey->refs);
    /**
     * 返回现有句柄
     */
    return pos->second.handle;
}

void HwRenderPrimitiveFactory::destroy(DriverApi& driver, Handle handle) noexcept {
    // look for this handle in our map
    auto pos = mBimap.findValue(Value{ handle });
    if (--pos->second.pKey->refs == 0) {
        mBimap.erase(pos);
        driver.destroyRenderPrimitive(handle);
    }
}

} // namespace filament
