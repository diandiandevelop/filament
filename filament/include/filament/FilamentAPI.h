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

#ifndef TNT_FILAMENT_FILAMENTAPI_H
#define TNT_FILAMENT_FILAMENTAPI_H

// 工具库头文件
#include <utils/compiler.h>              // 编译器特定宏（如 UTILS_PUBLIC）
#include <utils/PrivateImplementation.h>  // PIMPL 模式实现
#include <utils/ImmutableCString.h>       // 不可变C字符串类
#include <utils/StaticString.h>           // 静态字符串类

#include <stddef.h>                       // size_t 类型定义

namespace filament {

/**
 * \privatesection
 * FilamentAPI 基类 - Filament 渲染引擎 API 的基类
 * 
 * 设计目的：
 * 1. 确保 API 类只能通过 Engine 创建，不能由调用者直接实例化
 * 2. 防止在栈上创建对象（只能通过 Engine 在堆上创建）
 * 3. 防止拷贝和移动操作，确保资源管理的唯一性
 * 4. 在渲染引擎中，所有资源（纹理、材质、缓冲区等）都由 Engine 统一管理
 * 
 * 使用方式：
 * - 所有 Filament API 类（如 Texture、Material、VertexBuffer 等）都继承自此类
 * - 客户端代码通过 Engine::create*() 方法创建对象
 * - 客户端代码通过 Engine::destroy() 方法销毁对象
 * 
 * 内存管理策略：
 * - 允许 placement-new：用于在预分配的内存中构造对象（Engine 内部使用）
 * - 禁止普通 new：防止客户端在堆上直接创建
 * - 禁止栈分配：构造函数为 protected
 * 
 * 示例：
 *   class Texture : public FilamentAPI { ... };
 *   // 客户端代码：
 *   Texture* tex = engine->createTexture(...);  // 正确
 *   Texture tex;  // 错误：构造函数不可访问
 *   Texture* tex = new Texture();  // 错误：operator new 被删除
 */
class UTILS_PUBLIC FilamentAPI {
protected:
    /**
     * 受保护的默认构造函数
     * 
     * 作用：
     * - 防止客户端代码在栈上直接创建对象
     * - 只允许派生类和友元类（如 Engine）创建实例
     * - 使用 default 实现，允许编译器生成默认构造函数
     * 
     * 注意：使用 noexcept 确保构造函数不抛出异常
     */
    FilamentAPI() noexcept = default;
    
    /**
     * 受保护的默认析构函数
     * 
     * 作用：
     * - 防止客户端代码直接调用 delete
     * - 只允许通过 Engine::destroy() 销毁对象
     * - 确保资源由 Engine 统一管理
     */
    ~FilamentAPI() = default;

public:
    /**
     * 禁止拷贝构造函数
     * 
     * 原因：
     * - Filament 资源对象通常包含底层渲染资源（如 OpenGL/Vulkan 句柄）
     * - 拷贝会导致资源所有权不明确
     * - 防止意外的资源复制和双重释放
     */
    FilamentAPI(FilamentAPI const&) = delete;
    
    /**
     * 禁止移动构造函数
     * 
     * 原因：
     * - 资源对象由 Engine 管理，不应该被移动
     * - 保持资源句柄的稳定性
     */
    FilamentAPI(FilamentAPI&&) = delete;
    
    /**
     * 禁止拷贝赋值运算符
     */
    FilamentAPI& operator=(FilamentAPI const&) = delete;
    
    /**
     * 禁止移动赋值运算符
     */
    FilamentAPI& operator=(FilamentAPI&&) = delete;

    /**
     * Placement-new 运算符重载
     * 
     * 功能：
     * - 允许在预分配的内存地址上构造对象
     * - 这是 Engine 内部创建对象的方式
     * 
     * 实现过程：
     * 1. 接收预分配的内存指针 p
     * 2. 直接返回该指针，不进行内存分配
     * 3. 调用者负责在该地址上调用构造函数
     * 
     * @param size_t 对象大小（未使用，但必须匹配 operator new 签名）
     * @param p 预分配的内存地址
     * @return 返回传入的指针 p
     * 
     * 注意：
     * - 不使用 noexcept，避免编译器插入空指针检查
     * - 这是唯一允许的 new 运算符重载
     * 
     * 使用示例（Engine 内部）：
     *   void* memory = allocate(sizeof(Texture));
     *   Texture* tex = new(memory) Texture(...);
     */
    static void *operator new     (size_t, void* p) { return p; }

    /**
     * 禁止普通堆分配（单个对象）
     * 
     * 作用：
     * - 防止客户端使用 new Texture() 创建对象
     * - 强制使用 Engine::createTexture() 方法
     */
    static void *operator new     (size_t) = delete;
    
    /**
     * 禁止数组堆分配
     * 
     * 作用：
     * - 防止客户端使用 new Texture[] 创建对象数组
     */
    static void *operator new[]   (size_t) = delete;
};

/**
 * BuilderBase 类型别名
 * 
 * 功能：
 * - 为 Builder 模式提供统一的基类实现
 * - 使用 PIMPL 模式隐藏 Builder 的实现细节
 * 
 * 模板参数：
 * @tparam T Builder 的内部实现类型
 * 
 * 使用方式：
 * - 所有 Filament Builder 类（如 Material::Builder、Texture::Builder）都继承自此类
 * - 通过 PIMPL 模式实现，确保 ABI 兼容性
 * 
 * 示例：
 *   class Material::Builder : public BuilderBase<MaterialBuilderImpl> { ... };
 */
template<typename T>
using BuilderBase = utils::PrivateImplementation<T>;

/**
 * 构建器名称设置函数
 * 
 * 功能：
 * - 将 C 字符串转换为 ImmutableCString 并存储
 * - 用于 Builder 模式中设置资源名称
 * 
 * 实现过程：
 * 1. 接收 C 字符串和长度
 * 2. 创建 ImmutableCString 对象
 * 3. 存储到输出参数中
 * 
 * @param outName 输出参数，存储转换后的不可变字符串
 * @param name 输入的 C 字符串
 * @param len 字符串长度
 * 
 * 注意：
 * - 必须是公共函数，因为被模板类 BuilderNameMixin 使用
 * - 使用 noexcept 确保不抛出异常
 */
UTILS_PUBLIC void builderMakeName(utils::ImmutableCString& outName, const char* name, size_t len) noexcept;

/**
 * BuilderNameMixin 模板类 - Builder 名称功能混入类
 * 
 * 功能：
 * - 为 Builder 类提供名称设置和查询功能
 * - 使用 CRTP（Curiously Recurring Template Pattern）模式
 * - 在渲染引擎中，资源名称用于调试、日志和错误追踪
 * 
 * 模板参数：
 * @tparam Builder 派生 Builder 类的类型（CRTP）
 * 
 * 设计模式：
 * - Mixin 模式：通过多重继承为类添加功能
 * - CRTP 模式：派生类作为模板参数，实现静态多态
 * 
 * 使用方式：
 *   class Material::Builder : public BuilderBase<...>, 
 *                             public BuilderNameMixin<Material::Builder> {
 *       // 现在可以使用 name() 和 getName() 方法
 *   };
 * 
 * 示例：
 *   Material::Builder builder;
 *   builder.name("MyMaterial").build(engine);
 *   const auto& name = builder.getName();
 */
template <typename Builder>
class UTILS_PUBLIC BuilderNameMixin {
public:
    /**
     * 设置 Builder 名称（已弃用版本）
     * 
     * 功能：
     * - 使用 C 字符串和长度设置名称
     * - 返回 Builder 引用，支持方法链式调用
     * 
     * 实现过程：
     * 1. 调用 builderMakeName 将 C 字符串转换为 ImmutableCString
     * 2. 存储到成员变量 mName 中
     * 3. 返回当前 Builder 对象的引用（通过 CRTP 转换）
     * 
     * @param name C 字符串指针
     * @param len 字符串长度
     * @return 返回 Builder 引用，支持链式调用
     * 
     * 注意：
     * - 此方法已标记为废弃，建议使用 StaticString 版本
     * - 使用 noexcept 确保不抛出异常
     * 
     * 使用示例：
     *   builder.name("MyTexture", 9).build(engine);
     */
    UTILS_DEPRECATED
    Builder& name(const char* name, size_t len) noexcept {
        builderMakeName(mName, name, len);  // 转换并存储名称
        return static_cast<Builder&>(*this);  // CRTP：返回派生类引用
    }

    /**
     * 设置 Builder 名称（推荐版本）
     * 
     * 功能：
     * - 使用 StaticString 设置名称
     * - 类型安全，编译时检查
     * - 返回 Builder 引用，支持方法链式调用
     * 
     * 实现过程：
     * 1. 从 StaticString 获取数据和大小
     * 2. 调用 builderMakeName 转换为 ImmutableCString
     * 3. 存储到成员变量 mName 中
     * 4. 返回当前 Builder 对象的引用
     * 
     * @param name StaticString 常量引用
     * @return 返回 Builder 引用，支持链式调用
     * 
     * 使用示例：
     *   builder.name("MyTexture"_s).build(engine);
     */
    Builder& name(utils::StaticString const& name) noexcept {
        builderMakeName(mName, name.data(), name.size());  // 从 StaticString 提取数据
        return static_cast<Builder&>(*this);  // CRTP：返回派生类引用
    }

    /**
     * 获取 Builder 名称
     * 
     * 功能：
     * - 返回当前设置的名称
     * - 如果未设置名称，返回空字符串
     * 
     * @return 返回 ImmutableCString 常量引用
     * 
     * 使用场景：
     * - 调试时查看资源名称
     * - 日志记录
     * - 错误信息中显示资源标识
     */
    utils::ImmutableCString const& getName() const noexcept { return mName; }

    /**
     * 获取 Builder 名称或默认值
     * 
     * 功能：
     * - 如果名称已设置，返回该名称
     * - 如果名称为空，返回默认字符串 "(none)"
     * 
     * 实现过程：
     * 1. 调用 getName() 获取当前名称
     * 2. 如果名称不为空，直接返回
     * 3. 如果名称为空，返回静态默认字符串
     * 
     * @return 返回 ImmutableCString 常量引用（非空）
     * 
     * 使用场景：
     * - 在日志或错误信息中显示资源名称
     * - 确保始终有一个可显示的标识符
     * 
     * 性能说明：
     * - 使用静态变量存储默认名称，避免重复创建
     * - 返回值是引用，不涉及拷贝
     */
    utils::ImmutableCString const& getNameOrDefault() const noexcept {
        if (const auto& name = getName(); !name.empty()) {  // 检查名称是否为空
            return name;  // 返回已设置的名称
        }
        // 返回默认名称（静态变量，只创建一次）
        static const utils::ImmutableCString sDefaultName = "(none)";
        return sDefaultName;
    }

private:
    /**
     * 存储 Builder 名称的成员变量
     * 
     * 类型说明：
     * - ImmutableCString：不可变的 C 字符串包装类
     * - 提供字符串的只读访问，避免不必要的拷贝
     * 
     * 生命周期：
     * - 在 name() 方法中设置
     * - 在 Builder 对象生命周期内保持有效
     */
    utils::ImmutableCString mName;
};

} // namespace filament

#endif // TNT_FILAMENT_FILAMENTAPI_H
