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

#ifndef TNT_FILAMENT_BIMAP_H
#define TNT_FILAMENT_BIMAP_H

#include <utils/debug.h>

#include <tsl/robin_map.h>

#include <functional>
#include <memory>
#include <utility>

#include <stddef.h>

namespace filament {

/**
 * 双向映射（Bimap）
 * 
 * 一个半通用的自定义双向映射。此 bimap 存储键/值对，可以从键检索值，也可以从值检索键。
 * 
 * 优化特性：
 * - 针对大键和小值进行了优化
 * - 键存储在外部（out-of-line），永远不会被移动
 * - 使用两个哈希表实现 O(1) 的双向查找
 * 
 * @tparam Key 键类型
 * @tparam Value 值类型
 * @tparam KeyHash 键的哈希函数类型
 * @tparam ValueHash 值的哈希函数类型
 * @tparam Allocator 分配器类型（用于分配键的内存）
 */
template<typename Key, typename Value,
        typename KeyHash = std::hash<Key>,
        typename ValueHash = std::hash<Value>,
        typename Allocator = std::allocator<Key>>
class Bimap {

    /**
     * 键委托结构
     * 
     * 用于在哈希表中存储键的指针，而不是键本身。
     * 这样可以避免移动大键，提高性能。
     */
    struct KeyDelegate {
        Key const* pKey = nullptr;  // 指向实际键的指针
        
        /**
         * 相等比较操作符
         * 
         * 比较两个 KeyDelegate 指向的键是否相等。
         */
        bool operator==(KeyDelegate const& rhs) const noexcept {
            return *pKey == *rhs.pKey;
        }
    };

    /**
     * 键哈希委托
     * 
     * 将哈希计算委托给 KeyHash，对 KeyDelegate 指向的键进行哈希。
     */
    struct KeyHasherDelegate {
        size_t operator()(KeyDelegate const& p) const noexcept {
            KeyHash const h;
            return h(*p.pKey);
        }
    };

    /**
     * 前向映射：从键到值
     * 
     * 使用 tsl::robin_map（高性能哈希表）存储键到值的映射。
     */
    using ForwardMap = tsl::robin_map<
            KeyDelegate, Value,
            KeyHasherDelegate,
            std::equal_to<KeyDelegate>,
            std::allocator<std::pair<KeyDelegate, Value>>,
            true>;
    
    /**
     * 后向映射：从值到键
     * 
     * 使用 tsl::robin_map 存储值到键的映射。
     */
    using BackwardMap = tsl::robin_map<Value, KeyDelegate, ValueHash>;

    Allocator mAllocator;      // 分配器（用于分配键的内存，键存储在外部）
    ForwardMap mForwardMap;     // 前向映射（键 -> 值），使用 KeyDelegate 存储键指针
    BackwardMap mBackwardMap;   // 后向映射（值 -> 键），使用 KeyDelegate 存储键指针

public:
    /**
     * 默认构造函数
     */
    Bimap() = default;

    /**
     * 构造函数（带分配器）
     * 
     * @param allocator 分配器（会被移动）
     */
    explicit Bimap(Allocator&& allocator)
            : mAllocator(std::forward<Allocator>(allocator)) {
    }

    /**
     * 析构函数
     * 
     * 清理所有分配的键并清空映射。
     */
    ~Bimap() noexcept {
        clear();
    }

    // 禁止拷贝和移动（因为键存储在外部，移动语义复杂）
    Bimap(Bimap const&) = delete;
    Bimap& operator=(Bimap const&) = delete;
    Bimap(Bimap&&) = delete;
    Bimap& operator=(Bimap&&) = delete;

    /**
     * 清空所有映射
     * 
     * 手动析构并释放所有键的内存，然后清空两个映射表。
     * 
     * 注意：只需要遍历一个映射表，因为它们都指向相同的键。
     */
    void clear() noexcept {
        // 我们只需要遍历一个映射表，因为它们都指向相同的键。
        for (auto& pair : mForwardMap) {
            // 手动调用键的析构函数...
            pair.first.pKey->~Key();
            // ...然后释放内存。
            mAllocator.deallocate(const_cast<Key*>(pair.first.pKey), 1);
        }
        mForwardMap.clear();
        mBackwardMap.clear();
    }

    /**
     * 预留容量
     * 
     * 为两个映射表预留空间，减少重新哈希。
     * 
     * @param capacity 容量
     */
    void reserve(size_t capacity) {
        mForwardMap.reserve(capacity);
        mBackwardMap.reserve(capacity);
    }

    /**
     * 检查是否为空
     * 
     * @return 如果两个映射表都为空返回 true，否则返回 false
     */
    bool empty() const noexcept {
        return mForwardMap.empty() && mBackwardMap.empty();
    }

    /**
     * 插入新的键/值对
     * 
     * 不允许重复的键或值。
     * 
     * 实现细节：
     * 1. 为键分配外部存储空间
     * 2. 在分配的空间中复制构造键
     * 3. 将键指针和值插入前向映射
     * 4. 将值和键指针插入后向映射
     * 
     * @param key 键（会被复制到外部存储）
     * @param value 值
     * 
     * 注意：如果插入失败，可能会泄漏已分配的键（TODO：修复）。
     */
    void insert(Key const& key, Value const& value) noexcept {
        assert_invariant(find(key) == end() && findValue(value) == mBackwardMap.end());  // 断言键和值都不存在
        Key* pKey = mAllocator.allocate(1);  // 为键分配存储空间（外部存储）
        new(static_cast<void*>(pKey)) Key{ key };  // 在分配的空间中复制构造键
        // TODO: 如果下面的调用抛出异常，可能会泄漏键
        mForwardMap.insert({{ pKey }, value });  // 插入前向映射（键指针 -> 值）
        mBackwardMap.insert({ value, { pKey }});  // 插入后向映射（值 -> 键指针）
    }

    /**
     * 迭代器访问（前向映射）
     */
    typename ForwardMap::iterator begin() { return mForwardMap.begin(); }
    typename ForwardMap::const_iterator begin() const { return mForwardMap.begin(); }
    typename ForwardMap::const_iterator cbegin() const { return mForwardMap.cbegin(); }

    typename ForwardMap::iterator end() { return mForwardMap.end(); }
    typename ForwardMap::const_iterator end() const { return mForwardMap.end(); }
    typename ForwardMap::const_iterator cend() const { return mForwardMap.cend(); }

    /**
     * 值迭代器结束标记（后向映射）
     */
    typename BackwardMap::iterator endValue() { return mBackwardMap.end(); }
    typename BackwardMap::const_iterator endValue() const { return mBackwardMap.end(); }

    /**
     * 通过键查找值（O(1)）
     * 
     * @param key 键
     * @return 前向映射的迭代器，如果未找到返回 end()
     */
    typename ForwardMap::const_iterator find(Key const& key) const {
        return mForwardMap.find(KeyDelegate{ .pKey = &key });
    }
    typename ForwardMap::iterator find(Key const& key) {
        return mForwardMap.find(KeyDelegate{ .pKey = &key });
    }

    /**
     * 通过值查找键（O(1)）
     * 
     * 前置条件：值必须存在。
     * 
     * @param value 值
     * @return 后向映射的迭代器，如果未找到返回 endValue()
     */
    typename BackwardMap::const_iterator findValue(Value const& value) const {
        return mBackwardMap.find(value);
    }
    typename BackwardMap::iterator findValue(Value& value) {
        return mBackwardMap.find(value);
    }

    /**
     * 通过键删除
     * 
     * @param key 键
     * @return 如果找到并删除返回 true，否则返回 false
     */
    bool erase(Key const& key) {
        auto forward_it = find(key);
        if (forward_it != end()) {
            erase(forward_it);
            return true;
        }
        return false;
    }

    /**
     * 通过前向映射迭代器删除
     * 
     * @param it 前向映射的迭代器
     * 
     * 实现细节：
     * 1. 在删除前获取键的稳定指针
     * 2. 在后向映射中找到对应的条目
     * 3. 从两个映射中删除（此时键仍然有效）
     * 4. 安全地析构并释放键的内存
     */
    void erase(typename ForwardMap::const_iterator it) {
        // 在删除前获取键对象的稳定指针。
        Key const* const pKey = const_cast<Key*>(it->first.pKey);

        // 在后向映射中找到对应的条目。
        auto backward_it = findValue(it->second);
        assert_invariant(backward_it != mBackwardMap.end());

        // 在键仍然有效时从两个映射中删除。
        mBackwardMap.erase(backward_it);
        mForwardMap.erase(it);

        // 现在没有映射引用该键，可以安全地析构并释放它。
        pKey->~Key();
        mAllocator.deallocate(const_cast<Key*>(pKey), 1);
    }

    /**
     * 通过后向映射迭代器删除
     * 
     * @param it 后向映射的迭代器
     * 
     * 实现细节：
     * 1. 获取键对象的稳定指针
     * 2. 在前向映射中找到对应的迭代器
     * 3. 从两个映射中删除（此时键对象仍然有效）
     * 4. 安全地析构并释放键的内存
     */
    void erase(typename BackwardMap::const_iterator it) {
        // 获取键对象的稳定指针。
        Key const* const pKey = it->second.pKey;

        // 在删除前在前向映射中找到对应的迭代器。
        auto forward_it = find(*pKey);
        assert_invariant(forward_it != end());

        // 在键对象仍然有效时从两个映射中删除。
        mForwardMap.erase(forward_it);
        mBackwardMap.erase(it);

        // 现在没有映射引用该键，可以安全地析构并释放它。
        pKey->~Key();
        mAllocator.deallocate(const_cast<Key*>(pKey), 1);
    }
};

} // namespace filament


#endif // TNT_FILAMENT_BIMAP_H
