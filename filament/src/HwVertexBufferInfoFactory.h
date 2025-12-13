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

#ifndef TNT_FILAMENT_HWVERTEXBUFFERINFOFACTORY_H
#define TNT_FILAMENT_HWVERTEXBUFFERINFOFACTORY_H

#include "Bimap.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/Allocator.h>

#include <functional>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FEngine;

/**
 * 硬件顶点缓冲区信息工厂
 * 
 * 使用 Bimap 管理顶点缓冲区信息，避免重复创建相同的资源。
 * 使用引用计数来跟踪资源使用情况。
 */
class HwVertexBufferInfoFactory {
public:
    using Handle = backend::VertexBufferInfoHandle;

    HwVertexBufferInfoFactory();
    ~HwVertexBufferInfoFactory() noexcept;

    // 禁止拷贝和移动
    HwVertexBufferInfoFactory(HwVertexBufferInfoFactory const& rhs) = delete;
    HwVertexBufferInfoFactory(HwVertexBufferInfoFactory&& rhs) noexcept = delete;
    HwVertexBufferInfoFactory& operator=(HwVertexBufferInfoFactory const& rhs) = delete;
    HwVertexBufferInfoFactory& operator=(HwVertexBufferInfoFactory&& rhs) noexcept = delete;

    /**
     * 终止工厂
     * 
     * 清理所有资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver) noexcept;

    /**
     * 参数结构（132 字节）
     * 
     * 包含创建顶点缓冲区信息所需的所有参数。
     */
    struct Parameters {
        uint8_t bufferCount;  // 缓冲区数量
        uint8_t attributeCount;  // 属性数量
        uint8_t padding[2] = {};  // 填充（用于对齐）
        backend::AttributeArray attributes;  // 属性数组（包含所有顶点属性定义）
        
        /**
         * 计算哈希值
         * 
         * 用于在 Bimap 中快速查找。
         * 
         * @return 哈希值
         */
        size_t hash() const noexcept;  // 哈希函数
    };

    /**
     * 相等比较运算符
     * 
     * 比较两个参数结构是否相等。
     * 
     * @param lhs 左侧参数
     * @param rhs 右侧参数
     * @return 如果相等则返回 true
     */
    friend bool operator==(Parameters const& lhs, Parameters const& rhs) noexcept;

    /**
     * 创建顶点缓冲区信息
     * 
     * 如果已存在相同参数的资源，返回现有句柄并增加引用计数。
     * 
     * @param driver 驱动 API 引用
     * @param bufferCount 缓冲区数量
     * @param attributeCount 属性数量
     * @param attributes 属性数组
     * @return 顶点缓冲区信息句柄
     */
    Handle create(backend::DriverApi& driver,
            uint8_t bufferCount,
            uint8_t attributeCount,
            backend::AttributeArray attributes) noexcept;

    /**
     * 销毁顶点缓冲区信息
     * 
     * 减少引用计数，如果计数为 0 则销毁资源。
     * 
     * @param driver 驱动 API 引用
     * @param handle 句柄
     */
    void destroy(backend::DriverApi& driver, Handle handle) noexcept;

private:
    /**
     * 键结构（136 字节）
     * 
     * 键不应该是可拷贝的，但由于 Bimap 的工作方式，我们必须拷贝构造一次。
     */
    struct Key {
        /**
         * 拷贝构造函数
         */
        Key(Key const&) = default;
        
        /**
         * 禁止拷贝赋值
         */
        Key& operator=(Key const&) = delete;
        
        /**
         * 禁止移动赋值
         */
        Key& operator=(Key&&) noexcept = delete;
        
        /**
         * 构造函数
         * 
         * @param params 参数结构
         */
        explicit Key(Parameters const& params) : params(params), refs(1) { }  // 初始引用计数为 1
        
        Parameters params;  // 参数结构
        mutable uint32_t refs;  // 引用计数（4 字节）
        
        /**
         * 相等比较运算符
         * 
         * @param rhs 右侧键
         * @return 如果相等则返回 true
         */
        bool operator==(Key const& rhs) const noexcept {
            return params == rhs.params;  // 比较参数
        }
    };

    /**
     * 键哈希器
     * 
     * 为键提供哈希函数。
     */
    struct KeyHasher {
        /**
         * 计算键的哈希值
         * 
         * @param p 键引用
         * @return 哈希值
         */
        size_t operator()(Key const& p) const noexcept {
            return p.params.hash();  // 委托给参数的哈希函数
        }
    };

    /**
     * 值结构（4 字节）
     * 
     * 存储顶点缓冲区信息句柄。
     */
    struct Value {
        Handle handle;  // 顶点缓冲区信息句柄
    };

    /**
     * 值哈希器
     * 
     * 为值提供哈希函数。
     */
    struct ValueHasher {
        /**
         * 计算值的哈希值
         * 
         * @param v 值
         * @return 哈希值
         */
        size_t operator()(Value const v) const noexcept {
            return std::hash<Handle::HandleId>()(v.handle.getId());  // 使用句柄 ID 的哈希
        }
    };

    /**
     * 值相等比较运算符
     * 
     * @param lhs 左侧值
     * @param rhs 右侧值
     * @return 如果相等则返回 true
     */
    friend bool operator==(Value const lhs, Value const rhs) noexcept {
        return lhs.handle == rhs.handle;  // 比较句柄
    }

    /**
     * 用于 Bimap 的"set"部分的竞技场大小
     * 
     * 约 ~15K 条目后回退到堆。
     */
    static constexpr size_t SET_ARENA_SIZE = 2 * 1024 * 1024;  // 2 MB

    /**
     * 池分配器竞技场
     * 
     * 用于 set<> 的竞技场，使用堆区域内的池分配器。
     * - 池分配器：每个对象大小固定（Key 的大小）
     * - 无锁策略：单线程使用
     * - 无跟踪策略：不跟踪分配
     * - 堆区域策略：在堆上分配
     */
    /**
     * 用于 set<> 的竞技场，在堆区域中使用池分配器
     */
    using PoolAllocatorArena = utils::Arena<
            utils::PoolAllocatorWithFallback<sizeof(Key)>,  // 池分配器（Key 大小）
            utils::LockingPolicy::NoLock,  // 无锁策略
            utils::TrackingPolicy::Untracked,  // 无跟踪策略
            utils::AreaPolicy::HeapArea>;  // 堆区域策略

    /**
     * 分配 set 内存的竞技场
     */
    PoolAllocatorArena mArena;  // 内存分配器竞技场

    /**
     * 特殊的 Bimap
     * 
     * 双向映射，允许从键查找值，也可以从值查找键。
     * 使用自定义分配器以使用池分配器竞技场。
     */
    /**
     * 特殊的 Bimap
     * 
     * 双向映射：Key <-> Value
     */
    Bimap<Key, Value, KeyHasher, ValueHasher,
            utils::STLAllocator<Key, PoolAllocatorArena>> mBimap;  // 双向映射
};

} // namespace filament

#endif // TNT_FILAMENT_HWVERTEXBUFFERINFOFACTORY_H
