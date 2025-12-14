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

//! \file

#ifndef TNT_FILAMENT_INDEXBUFFER_H
#define TNT_FILAMENT_INDEXBUFFER_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>

#include <backend/BufferDescriptor.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <stdint.h>
#include <stddef.h>

namespace filament {

class FIndexBuffer;

class Engine;

/**
 * A buffer containing vertex indices into a VertexBuffer. Indices can be 16 or 32 bit.
 * The buffer itself is a GPU resource, therefore mutating the data can be relatively slow.
 * Typically these buffers are constant.
 *
 * It is possible, and even encouraged, to use a single index buffer for several Renderables.
 *
 * @see VertexBuffer, RenderableManager
 */
/**
 * 包含指向 VertexBuffer 的顶点索引的缓冲区。索引可以是 16 位或 32 位。
 * 缓冲区本身是 GPU 资源，因此修改数据可能相对较慢。
 * 通常这些缓冲区是常量。
 *
 * 可以为多个 Renderable 使用单个索引缓冲区，这是可能的，甚至是推荐的。
 *
 * @see VertexBuffer, RenderableManager
 */
class UTILS_PUBLIC IndexBuffer : public FilamentAPI {
    struct BuilderDetails;

public:
    using BufferDescriptor = backend::BufferDescriptor;
    /**
     * 缓冲区描述符类型
     */

    /**
     * Type of the index buffer
     */
    /**
     * 索引缓冲区的类型
     */
    enum class IndexType : uint8_t {
        USHORT = uint8_t(backend::ElementType::USHORT),  //!< 16-bit indices
        /** 16 位索引 */
        UINT = uint8_t(backend::ElementType::UINT),      //!< 32-bit indices
        /** 32 位索引 */
    };

    class Builder : public BuilderBase<BuilderDetails>, public BuilderNameMixin<Builder> {
        friend struct BuilderDetails;
    public:
        Builder() noexcept;
        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Size of the index buffer in elements.
         * @param indexCount Number of indices the IndexBuffer can hold.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 索引缓冲区的大小（以元素为单位）
         * @param indexCount IndexBuffer 可以容纳的索引数
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& indexCount(uint32_t indexCount) noexcept;

        /**
         * Type of the index buffer, 16-bit or 32-bit.
         * @param indexType Type of indices stored in the IndexBuffer.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 索引缓冲区的类型，16 位或 32 位
         * @param indexType 存储在 IndexBuffer 中的索引类型
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& bufferType(IndexType indexType) noexcept;

        /**
         * Associate an optional name with this IndexBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this IndexBuffer
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 IndexBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。名称
         * 被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 IndexBuffer 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this IndexBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this IndexBuffer
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 IndexBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。
         *
         * @param name 用于标识此 IndexBuffer 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the IndexBuffer object and returns a pointer to it. After creation, the index
         * buffer is uninitialized. Use IndexBuffer::setBuffer() to initialize the IndexBuffer.
         *
         * @param engine Reference to the filament::Engine to associate this IndexBuffer with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         *
         * @see IndexBuffer::setBuffer
         */
        /**
         * 创建 IndexBuffer 对象并返回指向它的指针。创建后，索引
         * 缓冲区未初始化。使用 IndexBuffer::setBuffer() 初始化 IndexBuffer。
         *
         * @param engine 要与此 IndexBuffer 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），
         *           则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         *
         * @see IndexBuffer::setBuffer
         */
        IndexBuffer* UTILS_NONNULL build(Engine& engine);
    private:
        friend class FIndexBuffer;
    };

    /**
     * Asynchronously copy-initializes a region of this IndexBuffer from the data provided.
     *
     * @param engine Reference to the filament::Engine to associate this IndexBuffer with.
     * @param buffer A BufferDescriptor representing the data used to initialize the IndexBuffer.
     *               BufferDescriptor points to raw, untyped data that will be interpreted as
     *               either 16-bit or 32-bits indices based on the Type of this IndexBuffer.
     * @param byteOffset Offset in *bytes* into the IndexBuffer. Must be multiple of 4.
     */
    /**
     * 从提供的数据异步复制初始化此 IndexBuffer 的区域
     *
     * @param engine 要与此 IndexBuffer 关联的 filament::Engine 的引用
     * @param buffer 表示用于初始化 IndexBuffer 的数据的 BufferDescriptor。
     *               BufferDescriptor 指向原始、未类型化的数据，将根据
     *               此 IndexBuffer 的类型解释为 16 位或 32 位索引
     * @param byteOffset 到 IndexBuffer 的偏移量（*字节*）。必须是 4 的倍数
     */
    void setBuffer(Engine& engine, BufferDescriptor&& buffer, uint32_t byteOffset = 0);

    /**
     * Returns the size of this IndexBuffer in elements.
     * @return The number of indices the IndexBuffer holds.
     */
    /**
     * 返回此 IndexBuffer 的大小（以元素为单位）
     * @return IndexBuffer 容纳的索引数
     */
    size_t getIndexCount() const noexcept;

protected:
    // prevent heap allocation
    ~IndexBuffer() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_INDEXBUFFER_H
