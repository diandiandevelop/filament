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

#ifndef TNT_FILAMENT_FG_BLACKBOARD_H
#define TNT_FILAMENT_FG_BLACKBOARD_H

#include <fg/FrameGraphId.h>

#include <string_view>
#include <unordered_map>

namespace filament {

/**
 * 黑板（Blackboard）类
 * 
 * 提供命名资源句柄的存储和检索机制。
 * 黑板允许通过名称访问帧图中的资源，而不需要直接传递句柄。
 * 
 * 用途：
 * - 在帧图构建过程中，通过名称共享资源句柄
 * - 简化资源传递，避免复杂的依赖关系
 * - 支持动态资源查找
 * 
 * 实现细节：
 * - 使用 unordered_map 存储名称到句柄的映射
 * - 使用 string_view 避免字符串拷贝
 * - 支持类型安全的句柄获取（通过模板）
 */
class Blackboard {
    /**
     * 容器类型
     * 
     * 使用 unordered_map 存储名称到句柄的映射。
     */
    using Container = std::unordered_map<
            std::string_view,
            FrameGraphHandle>;

public:
    /**
     * 构造函数
     */
    Blackboard() noexcept;
    
    /**
     * 析构函数
     */
    ~Blackboard() noexcept;

    /**
     * 下标操作符
     * 
     * 获取或创建指定名称的句柄引用。
     * 如果名称不存在，会创建一个新的条目。
     * 
     * @param name 资源名称
     * @return 句柄引用
     */
    FrameGraphHandle& operator [](std::string_view name) noexcept;

    /**
     * 放置句柄
     * 
     * 将句柄存储到指定名称下。
     * 
     * @param name 资源名称
     * @param handle 帧图句柄
     */
    void put(std::string_view name, FrameGraphHandle handle) noexcept;

    /**
     * 获取类型化的句柄
     * 
     * 获取指定名称的句柄，并转换为指定类型。
     * 
     * @tparam T 资源类型
     * @param name 资源名称（右值引用，会被转发）
     * @return 类型化的帧图 ID
     */
    template<typename T>
    FrameGraphId<T> get(std::string_view&& name) const noexcept {
        return static_cast<FrameGraphId<T>>(getHandle(std::forward<std::string_view>(name)));
    }

    /**
     * 移除句柄
     * 
     * 从黑板中移除指定名称的句柄。
     * 
     * @param name 资源名称
     */
    void remove(std::string_view name) noexcept;

private:
    /**
     * 获取句柄（内部方法）
     * 
     * 查找指定名称的句柄，如果不存在则返回空句柄。
     * 
     * @param name 资源名称
     * @return 帧图句柄
     */
    FrameGraphHandle getHandle(std::string_view name) const noexcept;
    Container mMap;  // 名称到句柄的映射
};

} // namespace filament


#endif //TNT_FILAMENT_FG_BLACKBOARD_H
