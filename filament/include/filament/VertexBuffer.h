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

#ifndef TNT_FILAMENT_VERTEXBUFFER_H
#define TNT_FILAMENT_VERTEXBUFFER_H

#include <filament/FilamentAPI.h>
#include <filament/MaterialEnums.h>

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FVertexBuffer;

class BufferObject;
class Engine;

/**
 * Holds a set of buffers that define the geometry of a Renderable.
 *
 * The geometry of the Renderable itself is defined by a set of vertex attributes such as
 * position, color, normals, tangents, etc...
 *
 * There is no need to have a 1-to-1 mapping between attributes and buffer. A buffer can hold the
 * data of several attributes -- attributes are then referred as being "interleaved".
 *
 * The buffers themselves are GPU resources, therefore mutating their data can be relatively slow.
 * For this reason, it is best to separate the constant data from the dynamic data into multiple
 * buffers.
 *
 * It is possible, and even encouraged, to use a single vertex buffer for several Renderables.
 *
 * @see IndexBuffer, RenderableManager
 */
/**
 * 保存一组定义 Renderable 几何体的缓冲区
 *
 * Renderable 本身的几何体由一组顶点属性定义，例如
 * 位置、颜色、法线、切线等...
 *
 * 不需要在属性和缓冲区之间有一对一的映射。一个缓冲区可以保存
 * 多个属性的数据——然后属性被称为"交错"存储。
 *
 * 缓冲区本身是 GPU 资源，因此修改其数据可能相对较慢。
 * 因此，最好将常量数据与动态数据分离到多个
 * 缓冲区中。
 *
 * 可以为多个 Renderable 使用单个顶点缓冲区，这是可能的，甚至是推荐的。
 *
 * @see IndexBuffer, RenderableManager
 */
class UTILS_PUBLIC VertexBuffer : public FilamentAPI {
    struct BuilderDetails;

public:
    using AttributeType = backend::ElementType;
    /**
     * 属性类型
     */
    using BufferDescriptor = backend::BufferDescriptor;
    /**
     * 缓冲区描述符类型
     */

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
         * Defines how many buffers will be created in this vertex buffer set. These buffers are
         * later referenced by index from 0 to \p bufferCount - 1.
         *
         * This call is mandatory. The default is 0.
         *
         * @param bufferCount Number of buffers in this vertex buffer set. The maximum value is 8.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 定义在此顶点缓冲区集中将创建多少个缓冲区。这些缓冲区
         * 稍后通过索引从 0 到 \p bufferCount - 1 引用。
         *
         * 此调用是必需的。默认值为 0。
         *
         * @param bufferCount 此顶点缓冲区集中的缓冲区数量。最大值为 8。
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& bufferCount(uint8_t bufferCount) noexcept;

        /**
         * Size of each buffer in the set in vertex.
         *
         * @param vertexCount Number of vertices in each buffer in this set.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 集合中每个缓冲区的顶点大小
         *
         * @param vertexCount 此集合中每个缓冲区中的顶点数
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& vertexCount(uint32_t vertexCount) noexcept;

        /**
         * Allows buffers to be swapped out and shared using BufferObject.
         *
         * If buffer objects mode is enabled, clients must call setBufferObjectAt rather than
         * setBufferAt. This allows sharing of data between VertexBuffer objects, but it may
         * slightly increase the memory footprint of Filament's internal bookkeeping.
         *
         * @param enabled If true, enables buffer object mode.  False by default.
         */
        /**
         * 允许使用 BufferObject 交换和共享缓冲区
         *
         * 如果启用了缓冲区对象模式，客户端必须调用 setBufferObjectAt 而不是
         * setBufferAt。这允许在 VertexBuffer 对象之间共享数据，但可能
         * 略微增加 Filament 内部簿记的内存占用。
         *
         * @param enabled 如果为 true，启用缓冲区对象模式。默认值为 false
         */
        Builder& enableBufferObjects(bool enabled = true) noexcept;

        /**
         * Sets up an attribute for this vertex buffer set.
         *
         * Using \p byteOffset and \p byteStride, attributes can be interleaved in the same buffer.
         *
         * @param attribute The attribute to set up.
         * @param bufferIndex  The index of the buffer containing the data for this attribute. Must
         *                     be between 0 and bufferCount() - 1.
         * @param attributeType The type of the attribute data (e.g. byte, float3, etc...)
         * @param byteOffset Offset in *bytes* into the buffer \p bufferIndex
         * @param byteStride Stride in *bytes* to the next element of this attribute. When set to
         *                   zero the attribute size, as defined by \p attributeType is used.
         *
         * @return A reference to this Builder for chaining calls.
         *
         * @warning VertexAttribute::TANGENTS must be specified as a quaternion and is how normals
         *          are specified.
         *
         * @warning Not all backends support 3-component attributes that are not floats. For help
         *          with conversion, see geometry::Transcoder.
         *
         * @see VertexAttribute
         *
         * This is a no-op if the \p attribute is an invalid enum.
         * This is a no-op if the \p bufferIndex is out of bounds.
         *
         */
        /**
         * 为此顶点缓冲区集设置属性
         *
         * 使用 \p byteOffset 和 \p byteStride，属性可以在同一缓冲区中交错存储。
         *
         * @param attribute 要设置的属性
         * @param bufferIndex  包含此属性数据的缓冲区索引。必须
         *                     在 0 和 bufferCount() - 1 之间
         * @param attributeType 属性数据的类型（例如 byte、float3 等...）
         * @param byteOffset 到缓冲区 \p bufferIndex 的偏移量（*字节*）
         * @param byteStride 到此属性下一个元素的步长（*字节*）。当设置为
         *                   零时，使用由 \p attributeType 定义的属性大小
         *
         * @return 对此 Builder 的引用，用于链接调用
         *
         * @warning VertexAttribute::TANGENTS 必须指定为四元数，这就是指定
         *         法线的方式。
         *
         * @warning 并非所有后端都支持非浮点数的 3 分量属性。有关转换帮助，
         *         请参见 geometry::Transcoder。
         *
         * @see VertexAttribute
         *
         * 如果 \p attribute 是无效枚举，则为无操作。
         * 如果 \p bufferIndex 超出范围，则为无操作。
         */
        Builder& attribute(VertexAttribute attribute, uint8_t bufferIndex,
                AttributeType attributeType,
                uint32_t byteOffset = 0, uint8_t byteStride = 0) noexcept;

        /**
         * Sets whether a given attribute should be normalized. By default attributes are not
         * normalized. A normalized attribute is mapped between 0 and 1 in the shader. This applies
         * only to integer types.
         *
         * @param attribute Enum of the attribute to set the normalization flag to.
         * @param normalize true to automatically normalize the given attribute.
         * @return A reference to this Builder for chaining calls.
         *
         * This is a no-op if the \p attribute is an invalid enum.
         */
        /**
         * 设置给定属性是否应被归一化。默认情况下，属性不会被
         * 归一化。归一化的属性在着色器中映射到 0 和 1 之间。这仅适用于
         * 整数类型。
         *
         * @param attribute 要设置归一化标志的属性枚举
         * @param normalize true 以自动归一化给定属性
         * @return 对此 Builder 的引用，用于链接调用
         *
         * 如果 \p attribute 是无效枚举，则为无操作。
         */
        Builder& normalized(VertexAttribute attribute, bool normalize = true) noexcept;

        /**
         * Sets advanced skinning mode. Bone data, indices and weights will be
         * set in RenderableManager:Builder:boneIndicesAndWeights methods.
         * Works with or without buffer objects.
         *
         * @param enabled If true, enables advanced skinning mode. False by default.
         *
         * @return A reference to this Builder for chaining calls.
         *
         * @see RenderableManager:Builder:boneIndicesAndWeights
         */
        /**
         * 设置高级蒙皮模式。骨骼数据、索引和权重将在
         * RenderableManager:Builder:boneIndicesAndWeights 方法中设置。
         * 无论是否使用缓冲区对象都可以工作。
         *
         * @param enabled 如果为 true，启用高级蒙皮模式。默认为 false
         *
         * @return 对此 Builder 的引用，用于链接调用
         *
         * @see RenderableManager:Builder:boneIndicesAndWeights
         */
        Builder& advancedSkinning(bool enabled) noexcept;

        /**
         * Associate an optional name with this VertexBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this VertexBuffer
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 VertexBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。名称
         * 被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 VertexBuffer 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this VertexBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this VertexBuffer
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 VertexBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。
         *
         * @param name 用于标识此 VertexBuffer 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the VertexBuffer object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this VertexBuffer with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 创建 VertexBuffer 对象并返回指向它的指针
         *
         * @param engine 要与此 VertexBuffer 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），
         *           则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         */
        VertexBuffer* UTILS_NONNULL build(Engine& engine);

    private:
        friend class FVertexBuffer;
    };

    /**
     * Returns the vertex count.
     * @return Number of vertices in this vertex buffer set.
     */
    /**
     * 返回顶点数量
     * @return 此顶点缓冲区集中的顶点数
     */
    size_t getVertexCount() const noexcept;

    /**
     * Asynchronously copy-initializes the specified buffer from the given buffer data.
     *
     * Do not use this if you called enableBufferObjects() on the Builder.
     *
     * @param engine Reference to the filament::Engine to associate this VertexBuffer with.
     * @param bufferIndex Index of the buffer to initialize. Must be between 0
     *                    and Builder::bufferCount() - 1.
     * @param buffer A BufferDescriptor representing the data used to initialize the buffer at
     *               index \p bufferIndex. BufferDescriptor points to raw, untyped data that will
     *               be copied as-is into the buffer.
     * @param byteOffset Offset in *bytes* into the buffer at index \p bufferIndex of this vertex
     *                   buffer set.  Must be multiple of 4.
     */
    /**
     * 从给定缓冲区数据异步复制初始化指定的缓冲区
     *
     * 如果您在 Builder 上调用了 enableBufferObjects()，请不要使用此方法。
     *
     * @param engine 要与此 VertexBuffer 关联的 filament::Engine 的引用
     * @param bufferIndex 要初始化的缓冲区的索引。必须在 0
     *                    和 Builder::bufferCount() - 1 之间
     * @param buffer 表示用于初始化索引 \p bufferIndex 处的缓冲区的数据的 BufferDescriptor。
     *               BufferDescriptor 指向原始、未类型化的数据，将
     *               按原样复制到缓冲区中
     * @param byteOffset 到此顶点缓冲区集中索引 \p bufferIndex 处的缓冲区的偏移量（*字节*）。
     *                   必须是 4 的倍数
     */
    void setBufferAt(Engine& engine, uint8_t bufferIndex, BufferDescriptor&& buffer,
            uint32_t byteOffset = 0);

    /**
     * Swaps in the given buffer object.
     *
     * To use this, you must first call enableBufferObjects() on the Builder.
     *
     * @param engine Reference to the filament::Engine to associate this VertexBuffer with.
     * @param bufferIndex Index of the buffer to initialize. Must be between 0
     *                    and Builder::bufferCount() - 1.
     * @param bufferObject The handle to the GPU data that will be used in this buffer slot.
     */
    /**
     * 交换给定的缓冲区对象
     *
     * 要使用此方法，必须先在 Builder 上调用 enableBufferObjects()。
     *
     * @param engine 要与此 VertexBuffer 关联的 filament::Engine 的引用
     * @param bufferIndex 要初始化的缓冲区的索引。必须在 0
     *                    和 Builder::bufferCount() - 1 之间
     * @param bufferObject 将在此缓冲区槽中使用的 GPU 数据的句柄
     */
    void setBufferObjectAt(Engine& engine, uint8_t bufferIndex,
            BufferObject const*  UTILS_NONNULL bufferObject);

protected:
    // prevent heap allocation
    ~VertexBuffer() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_VERTEXBUFFER_H
