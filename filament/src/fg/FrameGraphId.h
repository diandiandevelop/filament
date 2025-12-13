/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPHID_H
#define TNT_FILAMENT_FG_FRAMEGRAPHID_H

#include <stdint.h>
#include <limits>
#include <utility>

namespace filament {

template<typename T>
class FrameGraphId;

class Blackboard;
class FrameGraph;
class FrameGraphResources;
class PassNode;
class ResourceNode;

/**
 * 帧图资源句柄
 * 
 * 指向帧图中虚拟资源的句柄。
 * 句柄包含索引和版本号，用于标识和验证资源。
 * 
 * 实现细节：
 * - 索引指向资源槽位（ResourceSlot）
 * - 版本号用于检测资源是否已过期
 * - 私有构造函数，只能由 FrameGraph 创建
 */
/** A handle on a resource */
class FrameGraphHandle {
public:
    using Index = uint16_t;  // 索引类型
    using Version = uint16_t;  // 版本类型

private:
    template<typename T>
    friend class FrameGraphId;

    friend class Blackboard;
    friend class FrameGraph;
    friend class FrameGraphResources;
    friend class PassNode;
    friend class ResourceNode;

    /**
     * 私有构造函数
     * 
     * 用户无法直接构造，只能由 FrameGraph 创建。
     */
    // private ctor -- this cannot be constructed by users
    FrameGraphHandle() noexcept = default;
    
    /**
     * 构造函数（从索引）
     * 
     * @param index 资源索引
     */
    explicit FrameGraphHandle(Index const index) noexcept : index(index) {}

    /**
     * 未初始化值
     * 
     * 使用索引的最大值表示未初始化的句柄。
     */
    // index to the resource handle
    static constexpr uint16_t UNINITIALIZED = std::numeric_limits<Index>::max();
    uint16_t index = UNINITIALIZED;     // 资源槽位的索引
    Version version = 0;  // 版本号

public:
    /**
     * 拷贝构造函数
     */
    FrameGraphHandle(FrameGraphHandle const& rhs) noexcept = default;

    /**
     * 拷贝赋值操作符
     */
    FrameGraphHandle& operator=(FrameGraphHandle const& rhs) noexcept = default;

    /**
     * 检查句柄是否已初始化
     * 
     * @return 如果已初始化返回 true，否则返回 false
     */
    bool isInitialized() const noexcept { return index != UNINITIALIZED; }

    /**
     * 布尔转换操作符
     * 
     * @return 如果句柄已初始化返回 true，否则返回 false
     */
    operator bool() const noexcept { return isInitialized(); }

    /**
     * 清空句柄
     * 
     * 将句柄重置为未初始化状态。
     */
    void clear() noexcept { index = UNINITIALIZED; version = 0; }

    /**
     * 小于比较操作符
     * 
     * @param rhs 右侧句柄
     * @return 如果当前索引小于右侧索引返回 true
     */
    bool operator < (const FrameGraphHandle& rhs) const noexcept {
        return index < rhs.index;
    }

    /**
     * 相等比较操作符
     * 
     * @param rhs 右侧句柄
     * @return 如果索引相等返回 true
     */
    bool operator == (const FrameGraphHandle& rhs) const noexcept {
        return (index == rhs.index);
    }

    /**
     * 不等比较操作符
     * 
     * @param rhs 右侧句柄
     * @return 如果索引不等返回 true
     */
    bool operator != (const FrameGraphHandle& rhs) const noexcept {
        return !operator==(rhs);
    }
};

/**
 * 类型化的帧图资源句柄
 * 
 * 提供类型安全的资源句柄。
 * 
 * @tparam RESOURCE 资源类型
 */
/** A typed handle on a resource */
template<typename RESOURCE>
class FrameGraphId : public FrameGraphHandle {
public:
    using FrameGraphHandle::FrameGraphHandle;  // 继承构造函数
    FrameGraphId() noexcept = default;  // 默认构造函数
    
    /**
     * 从 FrameGraphHandle 构造
     * 
     * @param r 帧图句柄
     */
    explicit FrameGraphId(FrameGraphHandle const r) : FrameGraphHandle(r) { }
};

} // namespace filament

#endif //TNT_FILAMENT_FG_FRAMEGRAPHID_H
