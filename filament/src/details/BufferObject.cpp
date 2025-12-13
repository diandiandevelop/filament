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

#include "details/BufferObject.h"

#include "details/Engine.h"

#include "FilamentAPI-impl.h"

#include <backend/DriverEnums.h>

#include <filament/BufferObject.h>

#include <utils/CString.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>

#include <utility>

#include <stdint.h>
#include <stddef.h>

namespace filament {

/**
 * 构建器详情结构
 * 
 * 存储缓冲区对象的构建参数。
 */
struct BufferObject::BuilderDetails {
    BindingType mBindingType = BindingType::VERTEX;  // 绑定类型（默认为顶点）
    uint32_t mByteCount = 0;  // 字节数（默认为 0）
};

/**
 * 构建器类型别名
 */
using BuilderType = BufferObject;

/**
 * 构建器默认构造函数
 */
BuilderType::Builder::Builder() noexcept = default;

/**
 * 构建器析构函数
 */
BuilderType::Builder::~Builder() noexcept = default;

/**
 * 构建器拷贝构造函数
 */
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;

/**
 * 构建器移动构造函数
 */
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;

/**
 * 构建器拷贝赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;

/**
 * 构建器移动赋值运算符
 */
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置缓冲区大小
 * 
 * @param byteCount 字节数
 * @return 构建器引用（支持链式调用）
 */
BufferObject::Builder& BufferObject::Builder::size(uint32_t const byteCount) noexcept {
    mImpl->mByteCount = byteCount;  // 设置字节数
    return *this;  // 返回自身引用
}

/**
 * 设置绑定类型
 * 
 * @param bindingType 绑定类型（VERTEX、INDEX、UNIFORM 等）
 * @return 构建器引用（支持链式调用）
 */
BufferObject::Builder& BufferObject::Builder::bindingType(BindingType const bindingType) noexcept {
    mImpl->mBindingType = bindingType;  // 设置绑定类型
    return *this;  // 返回自身引用
}

/**
 * 设置名称（C 字符串版本）
 * 
 * @param name 名称字符串
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
BufferObject::Builder& BufferObject::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 调用名称混入方法
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 名称静态字符串
 * @return 构建器引用（支持链式调用）
 */
BufferObject::Builder& BufferObject::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 调用名称混入方法
}

/**
 * 构建缓冲区对象
 * 
 * 根据构建器配置创建缓冲区对象。
 * 
 * @param engine 引擎引用
 * @return 缓冲区对象指针
 */
BufferObject* BufferObject::Builder::build(Engine& engine) {
    return downcast(engine).createBufferObject(*this);  // 调用引擎的创建方法
}

// ------------------------------------------------------------------------------------------------

/**
 * 缓冲区对象构造函数
 * 
 * 创建缓冲区对象并分配驱动资源。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FBufferObject::FBufferObject(FEngine& engine, const Builder& builder)
        : mByteCount(builder->mByteCount),  // 初始化字节数
          mBindingType(builder->mBindingType) {  // 初始化绑定类型
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    /**
     * 创建缓冲区对象
     * 
     * 参数：
     * - builder->mByteCount: 缓冲区大小（字节）
     * - builder->mBindingType: 绑定类型
     * - backend::BufferUsage::STATIC: 使用方式（静态）
     * - builder.getName(): 缓冲区名称
     */
    mHandle = driver.createBufferObject(builder->mByteCount, builder->mBindingType,
            backend::BufferUsage::STATIC, utils::ImmutableCString{ builder.getName() });
}

/**
 * 终止缓冲区对象
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FBufferObject::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyBufferObject(mHandle);  // 销毁缓冲区对象
}

/**
 * 设置缓冲区数据
 * 
 * 更新缓冲区对象的内容。
 * 
 * @param engine 引擎引用
 * @param buffer 缓冲区描述符（会被移动）
 * @param byteOffset 字节偏移量（必须是 4 的倍数）
 */
void FBufferObject::setBuffer(FEngine& engine, BufferDescriptor&& buffer, uint32_t const byteOffset) {

    /**
     * 验证字节偏移量必须是 4 的倍数
     * 
     * 这是因为大多数 GPU 要求数据对齐到 4 字节边界。
     */
    FILAMENT_CHECK_PRECONDITION((byteOffset & 0x3) == 0)  // 检查低 2 位是否为 0
            << "byteOffset must be a multiple of 4";

    /**
     * 更新缓冲区对象
     * 
     * 将缓冲区描述符移动到驱动 API，更新缓冲区内容。
     */
    engine.getDriverApi().updateBufferObject(mHandle, std::move(buffer), byteOffset);  // 更新缓冲区
}

} // namespace filament
