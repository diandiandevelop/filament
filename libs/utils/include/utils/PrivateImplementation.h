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

#ifndef UTILS_PRIVATEIMPLEMENTATION_H
#define UTILS_PRIVATEIMPLEMENTATION_H

#include <stddef.h>

namespace utils {

/**
 * \privatesection
 * PrivateImplementation 是一个模板类，用于实现 PIMPL（Pointer to Implementation）设计模式。
 * 
 * 主要作用：
 * 1. 隐藏类的实现细节，将实现与接口分离
 * 2. 提高二进制兼容性（ABI兼容性），当实现改变时不影响客户端代码的重新编译
 * 3. 减少编译依赖，加快编译速度
 * 4. 在渲染引擎中常用于封装底层渲染资源（如纹理、缓冲区、着色器等）的实现细节
 * 
 * 模板参数：
 * @tparam T 实际实现类的类型，通常是一个结构体或类，包含所有私有成员变量和方法
 * 
 * 实现细节：
 * 实际实现位于 src/PrivateImplementation-impl.h 中，通过显式实例化避免在头文件中暴露实现
 */
template<typename T>
class PrivateImplementation {
public:
    /**
     * 可变参数构造函数
     * 使用完美转发（perfect forwarding）构造内部实现对象
     * 
     * @tparam ARGS 可变参数类型包，支持任意数量和类型的参数
     * @param args 转发给 T 类型构造函数的参数（使用右值引用和完美转发）
     * 
     * 注意：此方法必须非内联实现，以隐藏实现细节
     */
    template<typename ... ARGS>
    explicit PrivateImplementation(ARGS&& ...) noexcept;
    
    /**
     * 默认构造函数
     * 使用 T 类型的默认构造函数创建内部实现对象
     * 
     * 注意：此方法必须非内联实现，以隐藏实现细节
     */
    PrivateImplementation() noexcept;
    
    /**
     * 析构函数
     * 释放内部实现对象的内存
     * 
     * 注意：此方法必须非内联实现，以隐藏实现细节
     */
    ~PrivateImplementation() noexcept;
    
    /**
     * 拷贝构造函数
     * 深拷贝另一个 PrivateImplementation 对象的内部实现
     * 
     * @param rhs 要拷贝的源对象（常量引用）
     * 
     * 注意：此方法必须非内联实现，以隐藏实现细节
     */
    PrivateImplementation(PrivateImplementation const& rhs) noexcept;
    
    /**
     * 拷贝赋值运算符
     * 将另一个 PrivateImplementation 对象的内容深拷贝到当前对象
     * 
     * @param rhs 要拷贝的源对象（常量引用）
     * @return 返回当前对象的引用，支持链式赋值
     * 
     * 注意：此方法必须非内联实现，以隐藏实现细节
     */
    PrivateImplementation& operator = (PrivateImplementation const& rhs) noexcept;

    /**
     * 移动构造函数
     * 将另一个对象的内部实现指针转移过来，原对象置空
     * 
     * 实现过程：
     * 1. 将源对象的 mImpl 指针复制到当前对象
     * 2. 将源对象的 mImpl 置为 nullptr，避免双重释放
     * 
     * @param rhs 要移动的源对象（右值引用）
     * 
     * 注意：可以内联实现，因为只是指针操作，不涉及实现细节
     */
    PrivateImplementation(PrivateImplementation&& rhs) noexcept : mImpl(rhs.mImpl) { rhs.mImpl = nullptr; }
    
    /**
     * 移动赋值运算符
     * 交换两个对象的内部实现指针
     * 
     * 实现过程：
     * 1. 保存当前对象的 mImpl 指针到临时变量
     * 2. 将源对象的 mImpl 赋值给当前对象
     * 3. 将临时变量赋值给源对象的 mImpl（实现交换）
     * 4. 返回当前对象引用
     * 
     * @param rhs 要移动的源对象（右值引用）
     * @return 返回当前对象的引用，支持链式赋值
     * 
     * 注意：可以内联实现，因为只是指针操作，不涉及实现细节
     */
    PrivateImplementation& operator = (PrivateImplementation&& rhs) noexcept {
        auto temp = mImpl;      // 保存当前对象的实现指针
        mImpl = rhs.mImpl;      // 获取源对象的实现指针
        rhs.mImpl = temp;       // 将当前对象的实现指针交给源对象（实现交换）
        return *this;
    }

protected:
    /**
     * 指向实际实现对象的指针
     * 
     * 作用：
     * - 存储实际实现对象的地址
     * - 通过指针间接访问实现，实现接口与实现的分离
     * - 初始化为 nullptr，表示未初始化状态
     */
    T* mImpl = nullptr;
    
    /**
     * 指针解引用运算符（非const版本）
     * 提供便捷的方式访问内部实现对象的成员
     * 
     * @return 返回内部实现对象的指针（非const）
     * 
     * 使用示例：p->method() 等价于 p.mImpl->method()
     */
    inline T* operator->() noexcept { return mImpl; }
    
    /**
     * 指针解引用运算符（const版本）
     * 提供便捷的方式访问内部实现对象的成员（只读访问）
     * 
     * @return 返回内部实现对象的常量指针（const）
     * 
     * 使用示例：const_p->method() 等价于 const_p.mImpl->method()
     */
    inline T const* operator->() const noexcept { return mImpl; }
};

} // namespace utils

#endif // UTILS_PRIVATEIMPLEMENTATION_H
