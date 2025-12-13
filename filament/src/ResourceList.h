/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef TNT_FILAMENT_RESOURCELIST_H
#define TNT_FILAMENT_RESOURCELIST_H

#include <utils/compiler.h>

#include <tsl/robin_set.h>

#include <stdint.h>

namespace filament {

/**
 * 资源列表基类
 * 
 * 使用 robin_set（哈希集合）存储资源指针。
 * 分离 ResourceListBase 和 ResourceList 允许我们通过将通用代码
 * （操作 void*）分开来减少代码大小。
 */
class ResourceListBase {
public:
    using iterator = typename tsl::robin_set<void*>::iterator;
    using const_iterator = typename tsl::robin_set<void*>::const_iterator;

    /**
     * 构造函数
     * 
     * @param typeName 类型名称（用于调试）
     */
    explicit ResourceListBase(const char* typeName);
    ResourceListBase(ResourceListBase&& rhs) noexcept = default;

    ~ResourceListBase() noexcept;

    /**
     * 插入资源
     * 
     * @param item 资源指针
     */
    void insert(void* item);

    /**
     * 移除资源
     * 
     * @param item 资源指针
     * @return true 如果成功移除
     */
    bool remove(void const* item);

    /**
     * 查找资源
     * 
     * @param item 资源指针
     * @return 迭代器
     */
    iterator find(void const* item);

    /**
     * 清空列表
     */
    void clear() noexcept;

    /**
     * 检查是否为空
     */
    bool empty() const noexcept {
        return mList.empty();
    }

    /**
     * 获取大小
     */
    size_t size() const noexcept {
        return mList.size();
    }

    /**
     * 迭代器
     */
    iterator begin() noexcept {
        return mList.begin();
    }

    iterator end() noexcept {
        return mList.end();
    }

    const_iterator begin() const noexcept {
        return mList.begin();
    }

    const_iterator end() const noexcept {
        return mList.end();
    }

protected:
    /**
     * 遍历所有资源
     * 
     * @param f 回调函数
     * @param user 用户数据
     */
    void forEach(void(*f)(void* user, void *p), void* user) const noexcept;
    
    tsl::robin_set<void*> mList;  // 资源列表（哈希集合）
#ifndef NDEBUG
private:
    /**
     * 移除这个可以节省 8 字节，因为派生类的填充
     */
    const char* const mTypeName;  // 类型名称（仅调试版本）
#endif
};

/**
 * 资源列表模板类
 * 
 * 分离 ResourceListBase / ResourceList 允许我们通过将通用代码
 * （操作 void*）分开来减少代码大小。
 * 
 * @tparam T 资源类型
 */
template<typename T>
class ResourceList : private ResourceListBase {
public:
    using ResourceListBase::ResourceListBase;
    using ResourceListBase::forEach;
    using ResourceListBase::insert;
    using ResourceListBase::remove;
    using ResourceListBase::find;
    using ResourceListBase::empty;
    using ResourceListBase::size;
    using ResourceListBase::clear;
    using ResourceListBase::begin;
    using ResourceListBase::end;

    /**
     * 构造函数
     * 
     * @param name 类型名称（用于调试）
     */
    explicit ResourceList(const char* name) noexcept: ResourceListBase(name) {}

    ResourceList(ResourceList&& rhs) noexcept = default;

    ~ResourceList() noexcept = default;

    /**
     * 遍历所有资源（类型安全版本）
     * 
     * 将闭包转换为函数指针调用，我们这样做是为了减少代码大小。
     * 
     * @param func 回调函数（接受 T* 参数）
     */
    template<typename F>
    inline void forEach(F func) const noexcept {
        // 将闭包转换为函数指针调用，我们这样做是为了减少代码大小。
        this->forEach(+[](void* user, void* p) {
            ((F*)user)->operator()((T*)p);
        }, &func);
    }
};

} // namespace filament

#endif // TNT_FILAMENT_RESOURCELIST_H
