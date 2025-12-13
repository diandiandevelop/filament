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

#include "details/IndexBuffer.h"
#include "details/Engine.h"

#include "FilamentAPI-impl.h"
#include "filament/FilamentAPI.h"

#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>

#include <utility>

#include <stdint.h>
#include <stddef.h>

namespace filament {

/**
 * 索引缓冲区构建器详情结构
 * 
 * 存储索引缓冲区的构建参数。
 */
struct IndexBuffer::BuilderDetails {
    uint32_t mIndexCount = 0;  // 索引数量（默认 0）
    IndexType mIndexType = IndexType::UINT;  // 索引类型（默认 UINT，32 位无符号整数）
};

using BuilderType = IndexBuffer;
BuilderType::Builder::Builder() noexcept = default;
BuilderType::Builder::~Builder() noexcept = default;
BuilderType::Builder::Builder(Builder const& rhs) noexcept = default;
BuilderType::Builder::Builder(Builder&& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder const& rhs) noexcept = default;
BuilderType::Builder& BuilderType::Builder::operator=(Builder&& rhs) noexcept = default;

/**
 * 设置索引数量
 * 
 * @param indexCount 索引数量
 * @return 构建器引用（支持链式调用）
 */
IndexBuffer::Builder& IndexBuffer::Builder::indexCount(uint32_t const indexCount) noexcept {
    mImpl->mIndexCount = indexCount;  // 设置索引数量
    return *this;  // 返回自身引用
}

/**
 * 设置索引类型
 * 
 * @param indexType 索引类型（UINT 或 USHORT）
 * @return 构建器引用（支持链式调用）
 */
IndexBuffer::Builder& IndexBuffer::Builder::bufferType(IndexType const indexType) noexcept {
    mImpl->mIndexType = indexType;  // 设置索引类型
    return *this;  // 返回自身引用
}

IndexBuffer::Builder& IndexBuffer::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);
}

IndexBuffer::Builder& IndexBuffer::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);
}

IndexBuffer* IndexBuffer::Builder::build(Engine& engine) {
    return downcast(engine).createIndexBuffer(*this);
}

// ------------------------------------------------------------------------------------------------

/**
 * 索引缓冲区构造函数
 * 
 * 创建索引缓冲区并分配驱动资源。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FIndexBuffer::FIndexBuffer(FEngine& engine, const Builder& builder)
        : mIndexCount(builder->mIndexCount) {  // 初始化索引数量
    auto name = builder.getName();  // 获取名称
    const char* const tag = name.empty() ? "(no tag)" : name.c_str_safe();  // 获取标签（用于错误消息）

    /**
     * 验证索引类型
     * 
     * 只支持 UINT（32 位）和 USHORT（16 位）两种类型。
     */
    FILAMENT_CHECK_PRECONDITION(
            builder->mIndexType == IndexType::UINT || builder->mIndexType == IndexType::USHORT)
            << "Invalid index type " << static_cast<int>(builder->mIndexType) << ", tag=" << tag;

    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    /**
     * 创建索引缓冲区
     * 
     * 在驱动层创建索引缓冲区对象。
     */
    mHandle = driver.createIndexBuffer(
            backend::ElementType(builder->mIndexType),  // 元素类型（UINT 或 USHORT）
            uint32_t(builder->mIndexCount),  // 索引数量
            backend::BufferUsage::STATIC,  // 使用方式（静态，不会频繁更新）
            std::move(name));  // 名称（移动）
}

/**
 * 终止索引缓冲区
 * 
 * 释放驱动资源。
 * 
 * @param engine 引擎引用
 */
void FIndexBuffer::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    driver.destroyIndexBuffer(mHandle);  // 销毁索引缓冲区
}

/**
 * 设置索引缓冲区数据
 * 
 * 更新索引缓冲区的数据。
 * 
 * @param engine 引擎引用
 * @param buffer 缓冲区描述符（包含数据和大小，会被移动）
 * @param byteOffset 字节偏移量（必须是 4 的倍数，满足对齐要求）
 */
void FIndexBuffer::setBuffer(FEngine& engine, BufferDescriptor&& buffer, uint32_t const byteOffset) {

    /**
     * 验证字节偏移量对齐
     * 
     * 偏移量必须是 4 的倍数，以满足 GPU 对齐要求。
     */
    FILAMENT_CHECK_PRECONDITION((byteOffset & 0x3) == 0)
            << "byteOffset must be a multiple of 4";

    /**
     * 更新索引缓冲区数据
     * 
     * 将数据上传到 GPU。
     */
    engine.getDriverApi().updateIndexBuffer(mHandle, std::move(buffer), byteOffset);
}

} // namespace filament
