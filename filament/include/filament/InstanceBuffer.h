/*
* Copyright (C) 2023 The Android Open Source Project
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

#ifndef TNT_FILAMENT_INSTANCEBUFFER_H
#define TNT_FILAMENT_INSTANCEBUFFER_H

#include <filament/FilamentAPI.h>
#include <filament/Engine.h>

#include <utils/compiler.h>
#include <utils/StaticString.h>

#include <math/mathfwd.h>

#include <stddef.h>

namespace filament {

/**
 * InstanceBuffer holds draw (GPU) instance transforms. These can be provided to a renderable to
 * "offset" each draw instance.
 *
 * @see RenderableManager::Builder::instances(size_t, InstanceBuffer*)
 */
/**
 * InstanceBuffer 保存绘制（GPU）实例变换。可以将其提供给可渲染对象以
 * "偏移"每个绘制实例。
 *
 * @see RenderableManager::Builder::instances(size_t, InstanceBuffer*)
 */
class UTILS_PUBLIC InstanceBuffer : public FilamentAPI {
    struct BuilderDetails;

public:
    class Builder : public BuilderBase<BuilderDetails>, public BuilderNameMixin<Builder> {
        friend struct BuilderDetails;

    public:

        /**
         * @param instanceCount The number of instances this InstanceBuffer will support, must be
         *                      >= 1 and <= \c Engine::getMaxAutomaticInstances()
         * @see Engine::getMaxAutomaticInstances
         */
        /**
         * @param instanceCount 此 InstanceBuffer 将支持的实例数，必须
         *                      >= 1 且 <= \c Engine::getMaxAutomaticInstances()
         * @see Engine::getMaxAutomaticInstances
         */
        explicit Builder(size_t instanceCount) noexcept;

        Builder(Builder const& rhs) noexcept;
        Builder(Builder&& rhs) noexcept;
        ~Builder() noexcept;
        Builder& operator=(Builder const& rhs) noexcept;
        Builder& operator=(Builder&& rhs) noexcept;

        /**
         * Provide an initial local transform for each instance. Each local transform is relative to
         * the transform of the associated renderable. This forms a parent-child relationship
         * between the renderable and its instances, so adjusting the renderable's transform will
         * affect all instances.
         *
         * The array of math::mat4f must have length instanceCount, provided when constructing this
         * Builder.
         *
         * @param localTransforms an array of math::mat4f with length instanceCount, must remain
         *                        valid until after build() is called
         */
        /**
         * 为每个实例提供初始局部变换。每个局部变换相对于
         * 关联可渲染对象的变换。这在可渲染对象及其实例之间形成了父子关系，
         * 因此调整可渲染对象的变换将
         * 影响所有实例。
         *
         * math::mat4f 数组的长度必须为 instanceCount，在构造此
         * Builder 时提供。
         *
         * @param localTransforms 长度为 instanceCount 的 math::mat4f 数组，必须在
         *                        build() 调用之后保持有效
         */
        Builder& localTransforms(math::mat4f const* UTILS_NULLABLE localTransforms) noexcept;

        /**
         * Associate an optional name with this InstanceBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible. The name is
         * truncated to a maximum of 128 characters.
         *
         * The name string is copied during this method so clients may free its memory after
         * the function returns.
         *
         * @param name A string to identify this InstanceBuffer
         * @param len Length of name, should be less than or equal to 128
         * @return This Builder, for chaining calls.
         * @deprecated Use name(utils::StaticString const&) instead.
         */
        /**
         * 将可选名称与此 InstanceBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。名称
         * 被截断为最多 128 个字符。
         *
         * 名称字符串在此方法期间被复制，因此客户端可以在
         * 函数返回后释放其内存。
         *
         * @param name 用于标识此 InstanceBuffer 的字符串
         * @param len 名称长度，应小于或等于 128
         * @return 此 Builder，用于链接调用
         * @deprecated 改用 name(utils::StaticString const&)
         */
        UTILS_DEPRECATED
        Builder& name(const char* UTILS_NONNULL name, size_t len) noexcept;

        /**
         * Associate an optional name with this InstanceBuffer for debugging purposes.
         *
         * name will show in error messages and should be kept as short as possible.
         *
         * @param name A string literal to identify this InstanceBuffer
         * @return This Builder, for chaining calls.
         */
        /**
         * 将可选名称与此 InstanceBuffer 关联以用于调试目的
         *
         * 名称将显示在错误消息中，应尽可能短。
         *
         * @param name 用于标识此 InstanceBuffer 的字符串字面量
         * @return 此 Builder，用于链接调用
         */
        Builder& name(utils::StaticString const& name) noexcept;

        /**
         * Creates the InstanceBuffer object and returns a pointer to it.
         */
        /**
         * 创建 InstanceBuffer 对象并返回指向它的指针
         */
        InstanceBuffer* UTILS_NONNULL build(Engine& engine) const;

    private:
        friend class FInstanceBuffer;
    };

    /**
     * Returns the instance count specified when building this InstanceBuffer.
     */
    /**
     * 返回构建此 InstanceBuffer 时指定的实例数
     */
    size_t getInstanceCount() const noexcept;

    /**
     * Sets the local transform for each instance. Each local transform is relative to the transform
     * of the associated renderable. This forms a parent-child relationship between the renderable
     * and its instances, so adjusting the renderable's transform will affect all instances.
     *
     * @param localTransforms an array of math::mat4f with length count, need not outlive this call
     * @param count the number of local transforms
     * @param offset index of the first instance to set local transforms
     */
    /**
     * 为每个实例设置局部变换。每个局部变换相对于
     * 关联可渲染对象的变换。这在可渲染对象与其实例之间形成父子关系，
     * 因此调整可渲染对象的变换将影响所有实例。
     *
     * @param localTransforms 长度为 count 的 math::mat4f 数组，不需要在此调用之后保持有效
     * @param count 局部变换的数量
     * @param offset 要设置局部变换的第一个实例的索引
     */
    void setLocalTransforms(math::mat4f const* UTILS_NONNULL localTransforms,
            size_t count, size_t offset = 0);

    /**
     * Returns the local transform for a given instance.
     * @param index The index of the instance.
     * @return The local transform of the instance.
     */
    /**
     * 返回给定实例的局部变换
     * @param index 实例的索引
     * @return 实例的局部变换
     */
    math::mat4f const& getLocalTransform(size_t index);

protected:
    // prevent heap allocation
    ~InstanceBuffer() = default;
};

} // namespace filament

#endif //TNT_FILAMENT_INSTANCEBUFFER_H
