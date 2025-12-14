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

#ifndef TNT_FILAMENT_SKINNINGBUFFER_H
#define TNT_FILAMENT_SKINNINGBUFFER_H

#include <filament/FilamentAPI.h>

#include <filament/RenderableManager.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <math/mathfwd.h>

#include <stddef.h>
#include <stdint.h>

namespace filament {

/**
 * SkinningBuffer is used to hold skinning data (bones). It is a simple wraper around
 * a structured UBO.
 * @see RenderableManager::setSkinningBuffer
 */
/**
 * SkinningBuffer 用于保存蒙皮数据（骨骼）。它是结构化 UBO 的简单包装器
 * @see RenderableManager::setSkinningBuffer
 */
class UTILS_PUBLIC SkinningBuffer : public FilamentAPI {
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
         * Size of the skinning buffer in bones.
         *
         * Due to limitation in the GLSL, the SkinningBuffer must always by a multiple of
         * 256, this adjustment is done automatically, but can cause
         * some memory overhead. This memory overhead can be mitigated by using the same
         * SkinningBuffer to store the bone information for multiple RenderPrimitives.
         *
         * @param boneCount Number of bones the skinning buffer can hold.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 蒙皮缓冲区的大小（以骨骼为单位）
         *
         * 由于 GLSL 的限制，SkinningBuffer 必须始终是 256 的倍数，
         * 此调整会自动完成，但可能导致
         * 一些内存开销。可以通过使用相同的
         * SkinningBuffer 来存储多个 RenderPrimitive 的骨骼信息来减轻此内存开销。
         *
         * @param boneCount 蒙皮缓冲区可以容纳的骨骼数量
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& boneCount(uint32_t boneCount) noexcept;

        /**
         * The new buffer is created with identity bones
         * @param initialize true to initializing the buffer, false to not.
         * @return A reference to this Builder for chaining calls.
         */
        /**
         * 使用单位矩阵骨骼创建新缓冲区
         * @param initialize true 以初始化缓冲区，false 则不初始化
         * @return 对此 Builder 的引用，用于链接调用
         */
        Builder& initialize(bool initialize = true) noexcept;

        /**
         * Associate an optional name with this SkinningBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this SkinningBuffer
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 SkinningBuffer 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。名称
         * 会被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 SkinningBuffer 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this SkinningBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this SkinningBuffer
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 SkinningBuffer 关联，用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能简短。
         *
         * @param name 用于标识此 SkinningBuffer 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the SkinningBuffer object and returns a pointer to it.
         *
         * @param engine Reference to the filament::Engine to associate this SkinningBuffer with.
         *
         * @return pointer to the newly created object.
         *
         * @exception utils::PostConditionPanic if a runtime error occurred, such as running out of
         *            memory or other resources.
         * @exception utils::PreConditionPanic if a parameter to a builder function was invalid.
         *
         * @see SkinningBuffer::setBones
         */
        /**
         * 创建 SkinningBuffer 对象并返回指向它的指针
         *
         * @param engine 要与此 SkinningBuffer 关联的 filament::Engine 的引用
         *
         * @return 指向新创建对象的指针
         *
         * @exception 如果发生运行时错误（例如内存或其他资源耗尽），
         *           则抛出 utils::PostConditionPanic
         * @exception 如果构建器函数的参数无效，则抛出 utils::PreConditionPanic
         *
         * @see SkinningBuffer::setBones
         */
        SkinningBuffer* UTILS_NONNULL build(Engine& engine);
    private:
        friend class FSkinningBuffer;
    };

    /**
     * Updates the bone transforms in the range [offset, offset + count).
     * @param engine Reference to the filament::Engine to associate this SkinningBuffer with.
     * @param transforms pointer to at least count Bone
     * @param count number of Bone elements in transforms
     * @param offset offset in elements (not bytes) in the SkinningBuffer (not in transforms)
     * @see RenderableManager::setSkinningBuffer
     */
    /**
     * 更新 [offset, offset + count) 范围内的骨骼变换
     * @param engine 要与此 SkinningBuffer 关联的 filament::Engine 的引用
     * @param transforms 指向至少 count 个 Bone 的指针
     * @param count transforms 中 Bone 元素的数量
     * @param offset SkinningBuffer（不是 transforms）中的偏移量（以元素为单位，不是字节）
     * @see RenderableManager::setSkinningBuffer
     */
    void setBones(Engine& engine, RenderableManager::Bone const* UTILS_NONNULL transforms,
            size_t count, size_t offset = 0);

    /**
     * Updates the bone transforms in the range [offset, offset + count).
     * @param engine Reference to the filament::Engine to associate this SkinningBuffer with.
     * @param transforms pointer to at least count mat4f
     * @param count number of mat4f elements in transforms
     * @param offset offset in elements (not bytes) in the SkinningBuffer (not in transforms)
     * @see RenderableManager::setSkinningBuffer
     */
    /**
     * 更新 [offset, offset + count) 范围内的骨骼变换
     * @param engine 要与此 SkinningBuffer 关联的 filament::Engine 的引用
     * @param transforms 指向至少 count 个 mat4f 的指针
     * @param count transforms 中 mat4f 元素的数量
     * @param offset SkinningBuffer（不是 transforms）中的偏移量（以元素为单位，不是字节）
     * @see RenderableManager::setSkinningBuffer
     */
    void setBones(Engine& engine, math::mat4f const* UTILS_NONNULL transforms,
            size_t count, size_t offset = 0);

    /**
     * Returns the size of this SkinningBuffer in elements.
     * @return The number of bones the SkinningBuffer holds.
     */
    /**
     * 返回此 SkinningBuffer 的大小（以元素为单位）
     * @return SkinningBuffer 容纳的骨骼数量
     */
    size_t getBoneCount() const noexcept;

protected:
    // prevent heap allocation
    ~SkinningBuffer() = default;
};

} // namespace filament

#endif //TNT_FILAMENT_SKINNINGBUFFER_H
