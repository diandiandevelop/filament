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

//! \file

#ifndef TNT_FILAMENT_BUFFEROBJECT_H
#define TNT_FILAMENT_BUFFEROBJECT_H

#include <filament/FilamentAPI.h>

#include <backend/DriverEnums.h>
#include <backend/BufferDescriptor.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <stdint.h>
#include <stddef.h>

namespace filament {

class FBufferObject;

class Engine;

/**
 * A generic GPU buffer containing data.
 *
 * Usage of this BufferObject is optional. For simple use cases it is not necessary. It is useful
 * only when you need to share data between multiple VertexBuffer instances. It also allows you to
 * efficiently swap-out the buffers in VertexBuffer.
 *
 * NOTE: For now this is only used for vertex data, but in the future we may use it for other things
 * (e.g. compute).
 *
 * @see VertexBuffer
 */
/**
 * 包含数据的通用 GPU 缓冲区。
 *
 * BufferObject 的使用是可选的。对于简单的用例，它不是必需的。它仅在
 * 需要在多个 VertexBuffer 实例之间共享数据时有用。它还允许你
 * 高效地交换 VertexBuffer 中的缓冲区。
 *
 * 注意：目前这仅用于顶点数据，但将来我们可能将其用于其他用途
 * （例如计算）。
 *
 * @see VertexBuffer
 */
class UTILS_PUBLIC BufferObject : public FilamentAPI {
    struct BuilderDetails;

public:
    using BufferDescriptor = backend::BufferDescriptor;  //!< 缓冲区描述符类型
    using BindingType = backend::BufferObjectBinding;     //!< 绑定类型

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
         * Size of the buffer in bytes.
         * @param byteCount Maximum number of bytes the BufferObject can hold.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 缓冲区的大小（字节）。
         * @param byteCount BufferObject 可以容纳的最大字节数。
         * @return 此 Builder 的引用，用于链接调用。
         */
        Builder& size(uint32_t byteCount) noexcept;

        /**
         * The binding type for this buffer object. (defaults to VERTEX)
         * @param bindingType Distinguishes between SSBO, VBO, etc. For now this must be VERTEX.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 此缓冲区对象的绑定类型。（默认为 VERTEX）
         * @param bindingType 区分 SSBO、VBO 等。目前这必须是 VERTEX。
         * @return 此 Builder 的引用，用于链接调用。
         */
        Builder& bindingType(BindingType bindingType) noexcept;

        /**
         * Associate an optional name with this BufferObject for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this BufferObject
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 为此 BufferObject 关联一个可选名称，用于调试目的。
         *
         * 名称将显示在错误消息中，应尽可能简短。名称
         * 最多截断为 128 个字符。
         *
         * 名称字符串在此方法中复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 BufferObject 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用。
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this BufferObject for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this BufferObject
         * @return This Builder, for chaining calls.
         */
        /**
         * 为此 BufferObject 关联一个可选名称，用于调试目的。
         *
         * 名称将显示在错误消息中，应尽可能简短。
         *
         * @param name 用于标识此 BufferObject 的字符串字面量
         * @return 此 Builder，用于链接调用。
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the BufferObject and returns a pointer to it. After creation, the buffer
         * object is uninitialized. Use BufferObject::setBuffer() to initialize it.
         *
         * @param engine Reference to the filament::Engine to associate this BufferObject with.
         *
         * @return pointer to the newly created object
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         *
         * @see IndexBuffer::setBuffer
         */
        /**
         * 创建 BufferObject 并返回指向它的指针。创建后，缓冲区
         * 对象未初始化。使用 BufferObject::setBuffer() 来初始化它。
         *
         * @param engine 要与此 BufferObject 关联的 filament::Engine 的引用。
         *
         * @return 指向新创建对象的指针
         *
         * @exception utils::PostConditionPanic 如果发生运行时错误，例如用完
         *           内存或其他资源。
         * @exception utils::PreConditionPanic 如果构建器函数的参数无效。
         *
         * @see IndexBuffer::setBuffer
         */
        BufferObject* UTILS_NONNULL build(Engine& engine);
    private:
        friend class FBufferObject;
    };

    /**
     * Asynchronously copy-initializes a region of this BufferObject from the data provided.
     *
     * @param engine Reference to the filament::Engine associated with this BufferObject.
     * @param buffer A BufferDescriptor representing the data used to initialize the BufferObject.
     * @param byteOffset Offset in bytes into the BufferObject. Must be multiple of 4.
     */
    /**
     * 从提供的数据异步复制初始化此 BufferObject 的一个区域。
     *
     * @param engine 与此 BufferObject 关联的 filament::Engine 的引用。
     * @param buffer 表示用于初始化 BufferObject 的数据的 BufferDescriptor。
     * @param byteOffset BufferObject 中的字节偏移量。必须是 4 的倍数。
     */
    void setBuffer(Engine& engine, BufferDescriptor&& buffer, uint32_t byteOffset = 0);

    /**
     * Returns the size of this BufferObject in elements.
     * @return The maximum capacity of the BufferObject.
     */
    /**
     * 返回此 BufferObject 的大小（字节）。
     * @return BufferObject 的最大容量。
     */
    size_t getByteCount() const noexcept;

protected:
    // prevent heap allocation
    ~BufferObject() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_BUFFEROBJECT_H
