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

#include "HwDescriptorSetLayoutFactory.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/backend/DriverApi.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Hash.h>
#include <utils/Log.h>

#include <algorithm>
#include <utility>

#include <stdint.h>
#include <stdlib.h>

namespace filament {

using namespace utils;
using namespace backend;

/**
 * 计算参数哈希值
 * 
 * 使用 MurmurHash 算法计算描述符堆布局参数的哈希值。
 * 哈希基于绑定数组的内容。
 * 
 * @return 哈希值
 */
size_t HwDescriptorSetLayoutFactory::Parameters::hash() const noexcept {
    /**
     * 使用 MurmurHash 慢速版本计算哈希
     * - 数据：绑定数组的字节数据
     * - 大小：绑定数量 * 每个绑定的大小
     * - 种子：42
     */
    return hash::murmurSlow(
            reinterpret_cast<uint8_t const *>(dsl.bindings.data()),
            dsl.bindings.size() * sizeof(DescriptorSetLayoutBinding),
            42);
}

/**
 * 参数相等比较操作符
 * 
 * 比较两个描述符堆布局参数是否相等。
 * 
 * @param lhs 左侧参数
 * @param rhs 右侧参数
 * @return 如果参数相等则返回 true
 */
bool operator==(HwDescriptorSetLayoutFactory::Parameters const& lhs,
        HwDescriptorSetLayoutFactory::Parameters const& rhs) noexcept {
    /**
     * 首先比较绑定数组的大小
     */
    return (lhs.dsl.bindings.size() == rhs.dsl.bindings.size()) &&
           /**
            * 然后使用 std::equal 比较绑定数组的内容
            */
           std::equal(
                   lhs.dsl.bindings.begin(), lhs.dsl.bindings.end(),
                   rhs.dsl.bindings.begin());
}

// ------------------------------------------------------------------------------------------------

/**
 * HwDescriptorSetLayoutFactory 构造函数
 * 
 * 初始化描述符堆布局工厂，创建内存池和双向映射。
 */
HwDescriptorSetLayoutFactory::HwDescriptorSetLayoutFactory()
        : mArena("HwDescriptorSetLayoutFactory::mArena", SET_ARENA_SIZE),  // 创建内存池
          mBimap(mArena) {  // 创建双向映射（使用内存池）
    /**
     * 预分配双向映射的容量（256 个条目）
     */
    mBimap.reserve(256);
}

HwDescriptorSetLayoutFactory::~HwDescriptorSetLayoutFactory() noexcept = default;

void HwDescriptorSetLayoutFactory::terminate(DriverApi&) noexcept {
    assert_invariant(mBimap.empty());
}

/**
 * 创建描述符堆布局
 * 
 * 创建或重用描述符堆布局。如果相同的布局已存在，则增加引用计数并返回现有句柄。
 * 
 * @param driver 驱动 API 引用
 * @param dsl 描述符堆布局描述符
 * @return 描述符堆布局句柄
 */
auto HwDescriptorSetLayoutFactory::create(DriverApi& driver,
        DescriptorSetLayout dsl) noexcept -> Handle {

    /**
     * 对绑定数组按 binding 索引排序
     * 这确保相同的布局（即使绑定顺序不同）会被识别为相同
     */
    std::sort(dsl.bindings.begin(), dsl.bindings.end(),
            [](auto&& lhs, auto&& rhs) {
        return lhs.binding < rhs.binding;
    });

    // see if we already have seen this RenderPrimitive
    /**
     * 创建查找键
     */
    Key const key({ dsl });
    /**
     * 在双向映射中查找是否已存在相同的布局
     */
    auto pos = mBimap.find(key);

    // the common case is that we've never seen it (i.e.: no reuse)
    /**
     * 如果未找到（常见情况：无重用）
     */
    if (UTILS_LIKELY(pos == mBimap.end())) {
        /**
         * 创建新的描述符堆布局
         */
        auto handle = driver.createDescriptorSetLayout(std::move(dsl));
        /**
         * 将新布局插入双向映射
         */
        mBimap.insert(key, { handle });
        return handle;
    }

    /**
     * 找到现有布局，增加引用计数
     */
    ++(pos->first.pKey->refs);

    /**
     * 返回现有句柄
     */
    return pos->second.handle;
}

/**
 * 销毁描述符堆布局
 * 
 * 减少引用计数，如果引用计数为 0，则从映射中移除并销毁布局。
 * 
 * @param driver 驱动 API 引用
 * @param handle 要销毁的描述符堆布局句柄
 */
void HwDescriptorSetLayoutFactory::destroy(DriverApi& driver, Handle handle) noexcept {
    // look for this handle in our map
    /**
     * 通过句柄值在双向映射中查找
     */
    auto pos = mBimap.findValue(Value{ handle });
    /**
     * 减少引用计数，如果为 0，则销毁
     */
    if (--pos->second.pKey->refs == 0) {
        /**
         * 从映射中移除
         */
        mBimap.erase(pos);
        /**
         * 销毁描述符堆布局
         */
        driver.destroyDescriptorSetLayout(handle);
    }
}

} // namespace filament
