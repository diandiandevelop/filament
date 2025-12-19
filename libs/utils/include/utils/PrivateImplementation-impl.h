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

#ifndef UTILS_PRIVATEIMPLEMENTATION_IMPL_H
#define UTILS_PRIVATEIMPLEMENTATION_IMPL_H

/*
 * 此文件虽然看起来是头文件，但实际上充当 .cpp 文件的作用。
 * 
 * 原因：
 * 1. 用于显式实例化 PrivateImplementation<> 模板的方法
 * 2. 将实现细节从公共头文件中分离出来，提高编译效率和二进制兼容性
 * 3. 在渲染引擎中，这种方式可以避免客户端代码依赖底层实现细节
 */

#include <utils/PrivateImplementation.h>

#include <utility>  // 用于 std::forward 完美转发

namespace utils {

/**
 * 默认构造函数实现
 * 
 * 实现过程：
 * 1. 使用 new 运算符在堆上分配 T 类型的对象
 * 2. 调用 T 类型的默认构造函数
 * 3. 将返回的指针赋值给成员变量 mImpl
 * 
 * @tparam T 实际实现类的类型
 * 
 * 内存管理：
 * - 使用 new 分配的内存会在析构函数中通过 delete 释放
 * - 如果 T 的构造函数抛出异常，new 会返回 nullptr 或抛出异常
 */
template<typename T>
PrivateImplementation<T>::PrivateImplementation() noexcept
        : mImpl(new T) {  // 使用默认构造函数创建 T 类型的实例
}

/**
 * 可变参数构造函数实现
 * 
 * 实现过程：
 * 1. 使用 std::forward 进行完美转发，保持参数的值类别（左值/右值）
 * 2. 将转发后的参数传递给 T 类型的构造函数
 * 3. 使用 new 运算符在堆上分配并构造 T 类型的对象
 * 4. 将返回的指针赋值给成员变量 mImpl
 * 
 * @tparam T 实际实现类的类型
 * @tparam ARGS 可变参数类型包，可以是任意数量和类型的参数
 * @param args 要转发给 T 构造函数的参数（使用右值引用）
 * 
 * 完美转发说明：
 * - std::forward<ARGS>(args)... 会保持参数的原始值类别
 * - 如果传入的是右值，则转发为右值；如果是左值，则转发为左值引用
 * - 这样可以避免不必要的拷贝，提高性能
 * 
 * 使用示例：
 * PrivateImplementation<MyClass> p(1, 2.0f, "hello");
 * // 等价于：new MyClass(1, 2.0f, "hello")
 */
template<typename T>
template<typename ... ARGS>
PrivateImplementation<T>::PrivateImplementation(ARGS&& ... args) noexcept
        : mImpl(new T(std::forward<ARGS>(args)...)) {  // 完美转发参数并构造 T 实例
}

/**
 * 析构函数实现
 * 
 * 实现过程：
 * 1. 检查 mImpl 是否为 nullptr（虽然理论上不应该为 nullptr）
 * 2. 调用 delete 运算符释放 mImpl 指向的内存
 * 3. delete 会自动调用 T 类型的析构函数
 * 4. 然后释放内存
 * 
 * @tparam T 实际实现类的类型
 * 
 * 内存安全：
 * - delete nullptr 是安全的（C++标准保证）
 * - 如果 T 的析构函数抛出异常，可能导致未定义行为
 * - 使用 noexcept 保证不抛出异常，符合 RAII 原则
 * 
 * 注意：
 * - 析构函数必须是 noexcept，确保异常安全
 * - 在渲染引擎中，资源释放失败可能导致资源泄漏
 */
template<typename T>
PrivateImplementation<T>::~PrivateImplementation() noexcept {
    delete mImpl;  // 释放内部实现对象，自动调用 T 的析构函数
}

#ifndef UTILS_PRIVATE_IMPLEMENTATION_NON_COPYABLE

/**
 * 拷贝构造函数实现
 * 
 * 实现过程：
 * 1. 使用 new 运算符在堆上分配新的 T 类型对象
 * 2. 使用 T 类型的拷贝构造函数，将 rhs.mImpl 指向的对象拷贝到新对象
 * 3. 将新对象的指针赋值给当前对象的 mImpl
 * 
 * @tparam T 实际实现类的类型
 * @param rhs 要拷贝的源对象（常量引用）
 * 
 * 深拷贝说明：
 * - 创建了 rhs.mImpl 指向对象的完整副本
 * - 两个对象拥有独立的实现，互不影响
 * - 如果 T 的拷贝构造函数抛出异常，new 会返回 nullptr 或抛出异常
 * 
 * 性能考虑：
 * - 深拷贝可能涉及大量数据复制（在渲染引擎中，可能包括纹理数据、缓冲区等）
 * - 对于大型对象，考虑使用移动语义或引用计数
 */
template<typename T>
PrivateImplementation<T>::PrivateImplementation(PrivateImplementation const& rhs) noexcept
        : mImpl(new T(*rhs.mImpl)) {  // 深拷贝：创建 rhs.mImpl 指向对象的副本
}

/**
 * 拷贝赋值运算符实现
 * 
 * 实现过程：
 * 1. 检查自赋值（this != &rhs），避免不必要的操作和潜在问题
 * 2. 如果非自赋值，使用 T 类型的拷贝赋值运算符
 * 3. 将 rhs.mImpl 指向的对象内容拷贝到当前对象的 mImpl 指向的对象
 * 4. 返回当前对象的引用，支持链式赋值
 * 
 * @tparam T 实际实现类的类型
 * @param rhs 要拷贝的源对象（常量引用）
 * @return 返回当前对象的引用，支持链式赋值（如 a = b = c）
 * 
 * 自赋值检查：
 * - 防止对象赋值给自己时出现问题
 * - 如果 T 的拷贝赋值运算符有特殊处理，自赋值检查可以避免不必要的操作
 * 
 * 与拷贝构造的区别：
 * - 拷贝构造创建新对象，拷贝赋值更新现有对象
 * - 拷贝赋值需要处理已存在的对象，可能需要先清理再赋值
 * - 这里直接使用 T 的拷贝赋值运算符，假设 T 已经正确处理
 * 
 * 异常安全：
 * - 如果 T 的拷贝赋值运算符抛出异常，当前对象可能处于不一致状态
 * - 使用 noexcept 保证不抛出异常，但需要 T 的拷贝赋值也是 noexcept
 */
template<typename T>
PrivateImplementation<T>& PrivateImplementation<T>::operator=(PrivateImplementation<T> const& rhs) noexcept {
    if (this != &rhs) {           // 自赋值检查：避免对象赋值给自己
        *mImpl = *rhs.mImpl;      // 深拷贝：将源对象的内容拷贝到当前对象
    }
    return *this;                  // 返回当前对象引用，支持链式赋值
}

#endif  // UTILS_PRIVATE_IMPLEMENTATION_NON_COPYABLE

} // namespace utils

#endif // UTILS_PRIVATEIMPLEMENTATION_IMPL_H
