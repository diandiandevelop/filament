/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_DESCRIPTORSET_H
#define TNT_FILAMENT_DETAILS_DESCRIPTORSET_H

#include "DescriptorSetLayout.h"

#include <private/filament/EngineEnums.h>

#include <backend/DescriptorSetOffsetArray.h>
#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/bitset.h>
#include <utils/FixedCapacityVector.h>
#include <utils/StaticString.h>

#include <stdint.h>

namespace filament {

/**
 * 描述符堆类
 * 
 * 管理描述符堆的创建、更新和绑定。
 * 描述符堆是 GPU 资源的集合，包含 Uniform 缓冲区、采样器等。
 * 
 * 功能：
 * - 设置和更新描述符（缓冲区、采样器）
 * - 提交描述符到 GPU（延迟更新，仅在脏标记时更新）
 * - 绑定描述符堆到渲染管线
 * - 管理硬件描述符堆句柄
 * 
 * 实现细节：
 * - 使用脏标记（dirty bitset）跟踪需要更新的描述符
 * - 延迟提交：仅在调用 commit() 且存在脏标记时更新
 * - 支持动态偏移量（用于动态 Uniform 缓冲区）
 */
class DescriptorSet {
public:
    /**
     * 默认构造函数
     * 
     * 创建空的描述符堆。
     */
    DescriptorSet() noexcept;
    
    /**
     * 构造函数
     * 
     * 创建描述符堆并分配硬件资源。
     * 
     * @param name 描述符堆名称
     * @param descriptorSetLayout 描述符堆布局引用
     */
    explicit DescriptorSet(utils::StaticString name, DescriptorSetLayout const& descriptorSetLayout) noexcept;
    
    /**
     * 禁止拷贝构造
     */
    DescriptorSet(DescriptorSet const&) = delete;
    
    /**
     * 移动构造函数
     */
    DescriptorSet(DescriptorSet&& rhs) noexcept;
    
    /**
     * 禁止拷贝赋值
     */
    DescriptorSet& operator=(DescriptorSet const&) = delete;
    
    /**
     * 移动赋值操作符
     */
    DescriptorSet& operator=(DescriptorSet&& rhs) noexcept;
    
    /**
     * 析构函数
     */
    ~DescriptorSet() noexcept;

    /**
     * 终止描述符堆
     * 
     * 释放硬件资源。
     * 
     * @param driver 驱动 API 引用
     */
    void terminate(backend::DriverApi& driver) noexcept;

    /**
     * 提交描述符（快速路径）
     * 
     * 如果存在脏标记，则调用慢速路径提交。
     * 
     * @param layout 描述符堆布局引用
     * @param driver 驱动 API 引用
     */
    // update the descriptors if needed
    void commit(DescriptorSetLayout const& layout, backend::DriverApi& driver) noexcept {
        if (UTILS_UNLIKELY(mDirty.any())) {  // 如果有脏标记
            commitSlow(layout, driver);  // 调用慢速路径
        }
    }

    /**
     * 提交描述符（慢速路径）
     * 
     * 实际执行描述符的更新操作。
     * 
     * @param layout 描述符堆布局引用
     * @param driver 驱动 API 引用
     */
    void commitSlow(DescriptorSetLayout const& layout, backend::DriverApi& driver) noexcept;

    /**
     * 绑定描述符堆
     * 
     * 将描述符堆绑定到指定的绑定点。
     * 
     * @param driver 驱动 API 引用
     * @param set 绑定点
     */
    // bind the descriptor set
    void bind(backend::DriverApi& driver, DescriptorSetBindingPoints set) const noexcept;

    /**
     * 绑定描述符堆（带动态偏移量）
     * 
     * 将描述符堆绑定到指定的绑定点，并指定动态 Uniform 缓冲区的偏移量。
     * 
     * @param driver 驱动 API 引用
     * @param set 绑定点
     * @param dynamicOffsets 动态偏移量数组
     */
    void bind(backend::DriverApi& driver, DescriptorSetBindingPoints set,
            backend::DescriptorSetOffsetArray dynamicOffsets) const noexcept;

    /**
     * 解绑描述符堆
     * 
     * 从指定的绑定点解绑描述符堆。
     * 
     * @param driver 驱动 API 引用
     * @param set 绑定点
     */
    // unbind the descriptor set
    static void unbind(backend::DriverApi& driver, DescriptorSetBindingPoints set) noexcept;

    /**
     * 设置缓冲区描述符
     * 
     * 将 Uniform 缓冲区或存储缓冲区绑定到指定绑定点。
     * 
     * @param layout 描述符堆布局引用
     * @param binding 绑定索引
     * @param boh 缓冲区对象句柄
     * @param offset 字节偏移量
     * @param size 大小（字节）
     */
    // sets a ubo/ssbo descriptor
    void setBuffer(DescriptorSetLayout const& layout,
            backend::descriptor_binding_t binding,
            backend::Handle<backend::HwBufferObject> boh,
            uint32_t offset, uint32_t size);

    /**
     * 设置采样器描述符
     * 
     * 将纹理和采样器参数绑定到指定绑定点。
     * 
     * @param layout 描述符堆布局引用
     * @param binding 绑定索引
     * @param th 纹理句柄
     * @param params 采样器参数
     */
    // sets a sampler descriptor
    void setSampler(DescriptorSetLayout const& layout,
            backend::descriptor_binding_t binding,
            backend::Handle<backend::HwTexture> th,
            backend::SamplerParams params);

    /**
     * 复制描述符堆
     * 
     * 用于复制材质时创建描述符堆的副本。
     * 
     * @param name 新描述符堆名称
     * @param layout 描述符堆布局引用
     * @return 新的描述符堆对象
     */
    // Used for duplicating material
    DescriptorSet duplicate(utils::StaticString name, DescriptorSetLayout const& layout) const noexcept;

    /**
     * 获取硬件句柄
     * 
     * @return 描述符堆句柄
     */
    backend::DescriptorSetHandle getHandle() const noexcept {
        return mDescriptorSetHandle;  // 返回硬件句柄
    }

    /**
     * 获取有效描述符位集
     * 
     * 返回所有已设置的有效描述符的位集。
     * 
     * @return 有效描述符位集
     */
    utils::bitset64 getValidDescriptors() const noexcept {
        return mValid;  // 返回有效描述符位集
    }

    /**
     * 检查纹理是否与描述符兼容
     * 
     * 验证给定的纹理类型、采样器类型和描述符类型是否兼容。
     * 
     * @param t 纹理类型
     * @param s 采样器类型
     * @param d 描述符类型
     * @return 如果兼容返回 true，否则返回 false
     */
    static bool isTextureCompatibleWithDescriptor(
            backend::TextureType t, backend::SamplerType s,
            backend::DescriptorType d) noexcept;

private:
    /**
     * 描述符结构
     * 
     * 使用联合体存储缓冲区或纹理描述符数据。
     */
    struct Desc {
        Desc() noexcept { }  // 默认构造函数
        union {  // 联合体：缓冲区或纹理描述符
            struct {
                backend::Handle<backend::HwBufferObject> boh;  // 缓冲区对象句柄
                uint32_t offset;  // 字节偏移量
                uint32_t size;  // 大小（字节）
            } buffer{};  // 缓冲区描述符
            struct {
                backend::Handle<backend::HwTexture> th;  // 纹理句柄
                backend::SamplerParams params;  // 采样器参数
                uint32_t padding;  // 填充（确保结构大小一致）
            } texture;  // 纹理描述符
        };
    };

    utils::FixedCapacityVector<Desc> mDescriptors;          // 描述符数组（16 字节）
    mutable utils::bitset64 mDirty;                         // 脏标记位集（8 字节，标记需要更新的描述符）
    mutable utils::bitset64 mValid;                         // 有效描述符位集（8 字节，标记已设置的描述符）
    backend::DescriptorSetHandle mDescriptorSetHandle;      // 硬件描述符堆句柄（4 字节）
    mutable bool mSetAfterCommitWarning = false;            // 提交后设置警告标志（1 字节）
    mutable bool mSetUndefinedParameterWarning = false;     // 设置未定义参数警告标志（1 字节）
    utils::StaticString mName;                              // 描述符堆名称（16 字节）
};

} // namespace filament

#endif //TNT_FILAMENT_DETAILS_DESCRIPTORSET_H
