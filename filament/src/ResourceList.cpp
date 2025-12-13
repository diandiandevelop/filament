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

#include "ResourceList.h"

#include <utils/Logger.h>

#include <algorithm>

namespace filament {

/**
 * ResourceListBase 构造函数
 * 
 * 初始化资源列表基类。
 * 
 * @param typeName 资源类型名称（仅在调试模式下使用，用于泄漏检测）
 */
ResourceListBase::ResourceListBase(const char* typeName)
#ifndef NDEBUG
        : mTypeName(typeName)  // 仅在调试模式下存储类型名称
#endif
{
}

/**
 * ResourceListBase 析构函数
 * 
 * 在调试模式下检查资源泄漏。
 * 如果列表不为空，输出泄漏警告。
 */
ResourceListBase::~ResourceListBase() noexcept {
#ifndef NDEBUG
    /**
     * 如果列表不为空，说明有资源泄漏
     */
    if (!mList.empty()) {
        /**
         * 输出泄漏的资源数量和类型名称
         */
        DLOG(INFO) << "leaked " << mList.size() << " " << mTypeName;
    }
#endif
}

/**
 * 插入资源项
 * 
 * 将资源项添加到列表中。
 * 
 * @param item 要插入的资源项指针
 */
void ResourceListBase::insert(void* item) {
    /**
     * 将项插入到集合中
     */
    mList.insert(item);
}

/**
 * 移除资源项
 * 
 * 从列表中移除指定的资源项。
 * 
 * @param item 要移除的资源项指针
 * @return 如果成功移除则返回 true，否则返回 false
 */
bool ResourceListBase::remove(void const* item) {
    /**
     * 从集合中移除项，返回移除的元素数量（0 或 1）
     */
    return mList.erase(const_cast<void*>(item)) > 0;
}

/**
 * 查找资源项
 * 
 * 在列表中查找指定的资源项。
 * 
 * @param item 要查找的资源项指针
 * @return 指向找到的项的迭代器，如果未找到则返回 end()
 */
auto ResourceListBase::find(void const* item) -> iterator {
    /**
     * 在集合中查找项
     */
    return mList.find(const_cast<void*>(item));
}

/**
 * 清空资源列表
 * 
 * 移除列表中的所有资源项。
 */
void ResourceListBase::clear() noexcept {
    /**
     * 清空集合
     */
    mList.clear();
}

/**
 * 遍历资源列表
 * 
 * 对列表中的每个资源项调用指定的函数。
 * 此函数未内联，以避免增加代码大小。
 * 
 * @param f 要调用的函数指针，函数签名为 void f(void* user, void* item)
 * @param user 传递给函数的用户数据指针
 */
// this is not inlined, so we don't pay the code-size cost of iterating the list
void ResourceListBase::forEach(void (* f)(void*, void*), void* user) const noexcept {
    /**
     * 使用 std::for_each 遍历列表，对每个项调用函数 f
     */
    std::for_each(mList.begin(), mList.end(), [=](void* p) {
        /**
         * 调用用户提供的函数，传递用户数据和当前项
         */
        f(user, p);
    });
}

} // namespace filament
