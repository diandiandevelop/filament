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

#ifndef TNT_FILAMENT_MORPHTARGETBUFFER_H
#define TNT_FILAMENT_MORPHTARGETBUFFER_H

#include <filament/FilamentAPI.h>

#include <filament/Engine.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <math/mathfwd.h>

#include <stddef.h>

namespace filament {

/**
 * MorphTargetBuffer is used to hold morphing data (positions and tangents).
 *
 * Both positions and tangents are required.
 *
 */
/**
 * MorphTargetBuffer 用于保存变形目标数据（位置和切线）
 *
 * 位置和切线都是必需的。
 *
 */
class UTILS_PUBLIC MorphTargetBuffer : public FilamentAPI {
    struct BuilderDetails;

public:
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
         * Size of the morph targets in vertex counts.
         * @param vertexCount Number of vertex counts the morph targets can hold.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 变形目标的顶点数量大小
         * @param vertexCount 变形目标可以容纳的顶点数量
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& vertexCount(size_t vertexCount) noexcept;

        /**
         * Size of the morph targets in targets.
         * @param count Number of targets the morph targets can hold.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 变形目标的目标数量大小
         * @param count 变形目标可以容纳的目标数量
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& count(size_t count) noexcept;

        /**
         * Associate an optional name with this MorphTargetBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this MorphTargetBuffer
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 MorphTargetBuffer 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。名称
         * 会被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 MorphTargetBuffer 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this MorphTargetBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this MorphTargetBuffer
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 MorphTargetBuffer 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。
         *
         * @param name 用于标识此 MorphTargetBuffer 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the MorphTargetBuffer object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this MorphTargetBuffer with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         */
        /**
         * 创建 MorphTargetBuffer 对象并返回指向它的指针
         *
         * @param engine 要与此 MorphTargetBuffer 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），
         *           则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         */
        MorphTargetBuffer* UTILS_NONNULL build(Engine& engine);
    private:
        friend class FMorphTargetBuffer;
    };

    /**
     * Updates positions for the given morph target.
     *
     * This is equivalent to the float4 method, but uses 1.0 for the 4th component.
     *
     * Both positions and tangents must be provided.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param positions pointer to at least "count" positions
     * @param count number of float3 vectors in positions
     * @param offset offset into the target buffer, expressed as a number of float4 vectors
     * @see setTangentsAt
     */
    /**
     * 更新给定变形目标的位置
     *
     * 这等效于 float4 方法，但使用 1.0 作为第 4 个分量。
     *
     * 必须提供位置和切线。
     *
     * @param engine 与此 MorphTargetBuffer 关联的 filament::Engine 的引用
     * @param targetIndex 要更新的变形目标的索引
     * @param positions 指向至少 "count" 个位置的指针
     * @param count positions 中 float3 向量的数量
     * @param offset 目标缓冲区中的偏移量，以 float4 向量的数量表示
     * @see setTangentsAt
     */
    void setPositionsAt(Engine& engine, size_t targetIndex,
            math::float3 const* UTILS_NONNULL positions, size_t count, size_t offset = 0);

    /**
     * Updates positions for the given morph target.
     *
     * Both positions and tangents must be provided.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param positions pointer to at least "count" positions
     * @param count number of float4 vectors in positions
     * @param offset offset into the target buffer, expressed as a number of float4 vectors
     * @see setTangentsAt
     */
    /**
     * 更新给定变形目标的位置
     *
     * 必须提供位置和切线。
     *
     * @param engine 与此 MorphTargetBuffer 关联的 filament::Engine 的引用
     * @param targetIndex 要更新的变形目标的索引
     * @param positions 指向至少 "count" 个位置的指针
     * @param count positions 中 float4 向量的数量
     * @param offset 目标缓冲区中的偏移量，以 float4 向量的数量表示
     * @see setTangentsAt
     */
    void setPositionsAt(Engine& engine, size_t targetIndex,
            math::float4 const* UTILS_NONNULL positions, size_t count, size_t offset = 0);

    /**
     * Updates tangents for the given morph target.
     *
     * These quaternions must be represented as signed shorts, where real numbers in the [-1,+1]
     * range multiplied by 32767.
     *
     * @param engine Reference to the filament::Engine associated with this MorphTargetBuffer.
     * @param targetIndex the index of morph target to be updated.
     * @param tangents pointer to at least "count" tangents
     * @param count number of short4 quaternions in tangents
     * @param offset offset into the target buffer, expressed as a number of short4 vectors
     * @see setPositionsAt
     */
    /**
     * 更新给定变形目标的切线
     *
     * 这些四元数必须表示为有符号短整型，其中 [-1,+1] 范围内的
     * 实数乘以 32767。
     *
     * @param engine 与此 MorphTargetBuffer 关联的 filament::Engine 的引用
     * @param targetIndex 要更新的变形目标的索引
     * @param tangents 指向至少 "count" 个切线的指针
     * @param count tangents 中 short4 四元数的数量
     * @param offset 目标缓冲区中的偏移量，以 short4 向量的数量表示
     * @see setPositionsAt
     */
    void setTangentsAt(Engine& engine, size_t targetIndex,
            math::short4 const* UTILS_NONNULL tangents, size_t count, size_t offset = 0);

    /**
     * Returns the vertex count of this MorphTargetBuffer.
     * @return The number of vertices the MorphTargetBuffer holds.
     */
    /**
     * 返回此 MorphTargetBuffer 的顶点数量
     * @return MorphTargetBuffer 容纳的顶点数量
     */
    size_t getVertexCount() const noexcept;

    /**
     * Returns the target count of this MorphTargetBuffer.
     * @return The number of targets the MorphTargetBuffer holds.
     */
    /**
     * 返回此 MorphTargetBuffer 的目标数量
     * @return MorphTargetBuffer 容纳的目标数量
     */
    size_t getCount() const noexcept;

protected:
    // prevent heap allocation
    ~MorphTargetBuffer() = default;
};

} // namespace filament

#endif //TNT_FILAMENT_MORPHTARGETBUFFER_H
