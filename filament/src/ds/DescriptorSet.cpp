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

#include "DescriptorSet.h"

#include "DescriptorSetLayout.h"

#include "details/Engine.h"

#include <private/filament/EngineEnums.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <utility>
#include <limits>

#include <stdint.h>

namespace filament {

using namespace utils;

/**
 * 默认构造函数
 * 
 * 创建一个空的描述符堆。
 */
DescriptorSet::DescriptorSet() noexcept = default;

/**
 * 析构函数
 * 
 * 确保描述符堆句柄已被释放，避免资源泄漏。
 */
DescriptorSet::~DescriptorSet() noexcept {
    /**
     * 确保我们没有泄漏描述符堆句柄
     */
    // make sure we're not leaking the descriptor set handle
    assert_invariant(!mDescriptorSetHandle);  // 断言句柄为空
}

/**
 * 构造函数
 * 
 * 根据描述符堆布局创建描述符堆。
 * 
 * @param name 描述符堆名称
 * @param descriptorSetLayout 描述符堆布局常量引用
 */
DescriptorSet::DescriptorSet(StaticString const name, DescriptorSetLayout const& descriptorSetLayout) noexcept
        : mDescriptors(descriptorSetLayout.getMaxDescriptorBinding() + 1),  // 初始化描述符数组（大小 = 最大绑定点 + 1）
          mDirty(std::numeric_limits<uint64_t>::max()),  // 初始化脏标志（所有位都设置为 1，表示所有描述符都需要更新）
          mSetAfterCommitWarning(false),  // 初始化提交后设置警告标志
          mName(name) {  // 初始化名称
}

/**
 * 移动构造函数
 * 
 * @param rhs 右值引用
 */
DescriptorSet::DescriptorSet(DescriptorSet&& rhs) noexcept = default;

/**
 * 移动赋值运算符
 * 
 * @param rhs 右值引用
 * @return 自身引用
 */
DescriptorSet& DescriptorSet::operator=(DescriptorSet&& rhs) noexcept {
    if (this != &rhs) {  // 如果不是自赋值
        /**
         * 确保我们没有泄漏描述符堆句柄
         */
        // make sure we're not leaking the descriptor set handle
        assert_invariant(!mDescriptorSetHandle);  // 断言当前句柄为空
        mDescriptors = std::move(rhs.mDescriptors);  // 移动描述符数组
        mDescriptorSetHandle = std::move(rhs.mDescriptorSetHandle);  // 移动描述符堆句柄
        mDirty = rhs.mDirty;  // 复制脏标志
        mValid = rhs.mValid;  // 复制有效标志
        mSetAfterCommitWarning = rhs.mSetAfterCommitWarning;  // 复制提交后设置警告标志
        mSetUndefinedParameterWarning = rhs.mSetUndefinedParameterWarning;  // 复制未定义参数警告标志
        mName = rhs.mName;  // 复制名称
    }
    return *this;  // 返回自身引用
}

/**
 * 终止描述符堆
 * 
 * 释放描述符堆的硬件资源。
 * 
 * @param driver 驱动 API 引用
 */
void DescriptorSet::terminate(FEngine::DriverApi& driver) noexcept {
    if (mDescriptorSetHandle) {  // 如果句柄有效
        driver.destroyDescriptorSet(std::move(mDescriptorSetHandle));  // 销毁描述符堆
    }
}

/**
 * 提交描述符堆（慢速路径）
 * 
 * 当描述符堆有脏数据时，创建新的描述符堆并更新所有描述符。
 * 
 * @param layout 描述符堆布局常量引用
 * @param driver 驱动 API 引用
 */
void DescriptorSet::commitSlow(DescriptorSetLayout const& layout,
        FEngine::DriverApi& driver) noexcept {
    mDirty.clear();  // 清除脏标志
    /**
     * 如果我们有脏的描述符堆，
     * 需要分配一个新的并重置所有描述符
     */
    // if we have a dirty descriptor set,
    // we need to allocate a new one and reset all the descriptors
    if (UTILS_LIKELY(mDescriptorSetHandle)) {  // 如果句柄存在（常见情况）
        /**
         * 注意：如果描述符堆已绑定，这样做会使其悬空。
         * 如果新的描述符堆在稍后某个时刻没有绑定，这可能导致驱动中的释放后使用。
         */
        // note: if the descriptor-set is bound, doing this will essentially make it dangling.
        // This can result in a use-after-free in the driver if the new one isn't bound at some
        // point later.
        driver.destroyDescriptorSet(mDescriptorSetHandle);  // 销毁旧的描述符堆
    }
    mDescriptorSetHandle = driver.createDescriptorSet(layout.getHandle(), mName);  // 创建新的描述符堆
    /**
     * 更新所有有效的描述符
     */
    mValid.forEachSetBit([&layout, &driver,  // Lambda 捕获
            dsh = mDescriptorSetHandle, descriptors = mDescriptors.data()]  // 局部变量
            (backend::descriptor_binding_t const binding) {  // Lambda 参数
        assert_invariant(layout.isValid(binding));  // 断言绑定点有效
        if (layout.isSampler(binding)) {  // 如果是采样器
            driver.updateDescriptorSetTexture(dsh, binding,  // 更新纹理描述符
                    descriptors[binding].texture.th,  // 纹理句柄
                    descriptors[binding].texture.params);  // 采样器参数
        } else {  // 如果是缓冲区
            driver.updateDescriptorSetBuffer(dsh, binding,  // 更新缓冲区描述符
                    descriptors[binding].buffer.boh,  // 缓冲区对象句柄
                    descriptors[binding].buffer.offset,  // 偏移量
                    descriptors[binding].buffer.size);  // 大小
        }
    });

    /**
     * 检查是否有未设置的有效描述符
     */
    auto const unsetValidDescriptors = layout.getValidDescriptors() & ~mValid;  // 计算未设置的有效描述符
    

    // FIXME: see [b/468072646]
    //  We only validate empty descriptors at FEATURE_LEVEL_1 and above.
    //  This is because at FL0 it's expected that some descriptor won't be set. In theory, the
    //  corresponding layouts should not even contain those descriptors. However, making that change
    //  is difficult and risky, and will be done at a later time.
    //  Note: that the correct fix is actually needed to properly support FL3 once we want
    //  to take advantage of having more samplers.
    if (UTILS_LIKELY(driver.getFeatureLevel() > backend::FeatureLevel::FEATURE_LEVEL_0)) {
        auto const unsetValidDescriptors = layout.getValidDescriptors() & ~mValid;
        if (UTILS_VERY_UNLIKELY(!unsetValidDescriptors.empty() && !mSetUndefinedParameterWarning)) {
            unsetValidDescriptors.forEachSetBit([&](auto i) {
                LOG(WARNING) << (layout.isSampler(i) ? "Sampler" : "Buffer") << " descriptor " << i
                        << " of " << mName.c_str() << " is not set. Please report this issue.";
            });
            mSetUndefinedParameterWarning = true;
        }
    }
}

/**
 * 绑定描述符堆
 * 
 * 将描述符堆绑定到指定的绑定点（无动态偏移）。
 * 
 * @param driver 驱动 API 引用
 * @param set 描述符堆绑定点
 */
void DescriptorSet::bind(FEngine::DriverApi& driver, DescriptorSetBindingPoints const set) const noexcept {
    bind(driver, set, {});  // 调用带动态偏移的版本（空偏移）
}

/**
 * 绑定描述符堆
 * 
 * 将描述符堆绑定到指定的绑定点，并设置动态偏移。
 * 
 * @param driver 驱动 API 引用
 * @param set 描述符堆绑定点
 * @param dynamicOffsets 动态偏移数组
 */
void DescriptorSet::bind(FEngine::DriverApi& driver, DescriptorSetBindingPoints const set,
        backend::DescriptorSetOffsetArray dynamicOffsets) const noexcept {
    // TODO: on debug check that dynamicOffsets is large enough

    assert_invariant(mDescriptorSetHandle);  // 断言句柄有效

    /**
     * TODO: 确保客户端做正确的事情，不要在渲染通道内更改材质实例参数。
     * 我们必须注释掉断言，因为它导致客户端调试构建崩溃。
     */
    // TODO: Make sure clients do the right thing and not change material instance parameters
    // within the renderpass. We have to comment the assert out since it crashed a client's debug
    // build.
    // assert_invariant(mDirty.none());
    if (UTILS_VERY_UNLIKELY(mDirty.any() && !mSetAfterCommitWarning)) {  // 如果有脏数据且未警告过
        mDirty.forEachSetBit([&](uint8_t const binding) {  // 遍历脏绑定点
            LOG(WARNING) << "Descriptor set (handle=" << mDescriptorSetHandle.getId()  // 输出警告
                         << ") binding=" << (int) binding << " was set between begin/endRenderPass";
        });
        mSetAfterCommitWarning = true;  // 设置警告标志
    }
    driver.bindDescriptorSet(mDescriptorSetHandle, +set, std::move(dynamicOffsets));  // 绑定描述符堆
}

/**
 * 解绑描述符堆
 * 
 * 从指定的绑定点解绑描述符堆。
 * 
 * @param driver 驱动 API 引用
 * @param set 描述符堆绑定点
 */
void DescriptorSet::unbind(backend::DriverApi& driver,
        DescriptorSetBindingPoints set) noexcept {
    driver.bindDescriptorSet({}, +set, {});  // 绑定空描述符堆（即解绑）
}

/**
 * 设置缓冲区描述符
 * 
 * 将缓冲区对象绑定到指定的绑定点。
 * 
 * @param layout 描述符堆布局常量引用
 * @param binding 绑定点
 * @param boh 缓冲区对象句柄
 * @param offset 偏移量
 * @param size 大小
 */
void DescriptorSet::setBuffer(DescriptorSetLayout const& layout,
        backend::descriptor_binding_t const binding,
        backend::Handle<backend::HwBufferObject> boh, uint32_t const offset, uint32_t const size) {

    /**
     * 验证它是正确类型的描述符
     */
    // Validate it's the right kind of descriptor
    using DSLB = backend::DescriptorSetLayoutBinding;
    FILAMENT_CHECK_PRECONDITION(DSLB::isBuffer(layout.getDescriptorType(binding)))  // 检查是否为缓冲区类型
            << "descriptor " << +binding << "is not a buffer";

    auto& buffer = mDescriptors[binding].buffer;  // 获取缓冲区描述符
    if (buffer.boh != boh ||  // 如果句柄改变
        buffer.offset != offset ||  // 或偏移量改变
        buffer.size != size) {  // 或大小改变
        mDirty.set(binding);  // 标记为脏
    }
    buffer = { boh, offset, size };  // 更新缓冲区描述符
    mValid.set(binding, bool(boh));  // 设置有效标志（如果句柄有效则为 true）
}

/**
 * 设置采样器描述符
 * 
 * 将纹理和采样器参数绑定到指定的绑定点。
 * 
 * @param layout 描述符堆布局常量引用
 * @param binding 绑定点
 * @param th 纹理句柄
 * @param params 采样器参数
 */
void DescriptorSet::setSampler(
        DescriptorSetLayout const& layout,
        backend::descriptor_binding_t const binding,
        backend::Handle<backend::HwTexture> th, backend::SamplerParams const params) {

    using namespace backend;
    using DSLB = DescriptorSetLayoutBinding;

    /**
     * 验证它是正确类型的描述符
     */
    // Validate it's the right kind of descriptor
    auto type = layout.getDescriptorType(binding);  // 获取描述符类型
    FILAMENT_CHECK_PRECONDITION(DSLB::isSampler(type))  // 检查是否为采样器类型
            << "descriptor " << +binding << " is not a sampler";

    /**
     * 验证深度描述符与比较模式的一致性
     */
    FILAMENT_CHECK_PRECONDITION(
            !(params.compareMode == SamplerCompareMode::COMPARE_TO_TEXTURE && !isDepthDescriptor(type)))  // 如果比较模式为 COMPARE_TO_TEXTURE，必须是深度描述符
            << "descriptor " << +binding
            << " is not of type DEPTH, but sampler is in COMPARE_TO_TEXTURE mode";

    /**
     * 验证过滤深度描述符与比较模式的一致性
     */
    FILAMENT_CHECK_PRECONDITION(
            !(params.isFiltered() &&  // 如果已过滤
            isDepthDescriptor(type) &&  // 且是深度描述符
            params.compareMode != SamplerCompareMode::COMPARE_TO_TEXTURE))  // 但比较模式不是 COMPARE_TO_TEXTURE
            << "descriptor " << +binding
            << " is of type filtered DEPTH, but sampler not in COMPARE_TO_TEXTURE mode";

    if (mDescriptors[binding].texture.th != th || mDescriptors[binding].texture.params != params) {  // 如果纹理或参数改变
        mDirty.set(binding);  // 标记为脏
    }
    mDescriptors[binding].texture = { th, params };  // 更新纹理描述符
    mValid.set(binding, bool(th));  // 设置有效标志（如果句柄有效则为 true）
}

/**
 * 复制描述符堆
 * 
 * 创建一个新的描述符堆，复制当前描述符堆的所有描述符。
 * 
 * @param name 新描述符堆名称
 * @param layout 描述符堆布局常量引用
 * @return 新的描述符堆
 */
DescriptorSet DescriptorSet::duplicate(
        StaticString const name, DescriptorSetLayout const& layout) const noexcept {
    DescriptorSet set{ name, layout };  // 创建新的描述符堆
    set.mDescriptors = mDescriptors;  // 使用向量的赋值运算符复制描述符
    set.mDirty = mValid | mDirty;  // 将所有有效描述符标记为脏，以便在提交时更新
    set.mValid = mValid;  // 复制有效标志
    return set;  // 返回新描述符堆
}

/**
 * 检查纹理是否与描述符兼容
 * 
 * 验证纹理类型、采样器类型和描述符类型是否兼容。
 * 
 * @param t 纹理类型
 * @param s 采样器类型
 * @param d 描述符类型
 * @return 如果兼容返回 true，否则返回 false
 */
bool DescriptorSet::isTextureCompatibleWithDescriptor(
    backend::TextureType t, backend::SamplerType s, backend::DescriptorType d) noexcept {
    using namespace backend;

    /**
     * 检查采样器类型与描述符类型的维度兼容性
     */
    switch (s) {
        case SamplerType::SAMPLER_2D:  // 2D 采样器
            if (!is2dTypeDescriptor(d)) {  // 如果不是 2D 类型描述符
                return false;  // 不兼容
            }
            break;
        case SamplerType::SAMPLER_2D_ARRAY:  // 2D 数组采样器
            if (!is2dArrayTypeDescriptor(d)) {  // 如果不是 2D 数组类型描述符
                return false;  // 不兼容
            }
            break;
        case SamplerType::SAMPLER_CUBEMAP:  // 立方体贴图采样器
            if (!isCubeTypeDescriptor(d)) {  // 如果不是立方体类型描述符
                return false;  // 不兼容
            }
            break;
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:  // 立方体数组采样器
            if (!isCubeArrayTypeDescriptor(d)) {  // 如果不是立方体数组类型描述符
                return false;  // 不兼容
            }
            break;
        case SamplerType::SAMPLER_3D:  // 3D 采样器
            if (!is3dTypeDescriptor(d)) {  // 如果不是 3D 类型描述符
                return false;  // 不兼容
            }
            break;
        case SamplerType::SAMPLER_EXTERNAL:  // 外部采样器
            break;  // 外部采样器与所有描述符类型兼容
    }

    /**
     * 检查描述符类型与纹理格式类型的兼容性
     */
    // check that the descriptor type is compatible with the texture format type
    switch (d) {
        case DescriptorType::SAMPLER_2D_FLOAT:  // 2D 浮点采样器
        case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:  // 2D 数组浮点采样器
        case DescriptorType::SAMPLER_CUBE_FLOAT:  // 立方体浮点采样器
        case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:  // 立方体数组浮点采样器
        case DescriptorType::SAMPLER_3D_FLOAT:  // 3D 浮点采样器
        case DescriptorType::SAMPLER_2D_MS_FLOAT:  // 2D 多重采样浮点采样器
        case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:  // 2D 多重采样数组浮点采样器
            /**
             * DEPTH_STENCIL 被视为访问深度分量。OpenGL 4.3
             * 允许指定访问哪个分量，但 Filament 不支持。
             * 深度纹理可以用作未过滤的浮点采样器
             */
            // DEPTH_STENCIL is treated as accessing the depth component. OpenGL 4.3
            // allows to specify which one, but not filament.
            // Depth textures can be used as an unfiltered float sampler
            return t == TextureType::FLOAT || t == TextureType::DEPTH || t == TextureType::DEPTH_STENCIL;  // 浮点、深度或深度模板纹理

        case DescriptorType::SAMPLER_2D_INT:  // 2D 整数采样器
        case DescriptorType::SAMPLER_2D_ARRAY_INT:  // 2D 数组整数采样器
        case DescriptorType::SAMPLER_CUBE_INT:  // 立方体整数采样器
        case DescriptorType::SAMPLER_CUBE_ARRAY_INT:  // 立方体数组整数采样器
        case DescriptorType::SAMPLER_3D_INT:  // 3D 整数采样器
        case DescriptorType::SAMPLER_2D_MS_INT:  // 2D 多重采样整数采样器
        case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:  // 2D 多重采样数组整数采样器
            return t == TextureType::INT;  // 整数纹理

        case DescriptorType::SAMPLER_2D_UINT:  // 2D 无符号整数采样器
        case DescriptorType::SAMPLER_2D_ARRAY_UINT:  // 2D 数组无符号整数采样器
        case DescriptorType::SAMPLER_CUBE_UINT:  // 立方体无符号整数采样器
        case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:  // 立方体数组无符号整数采样器
        case DescriptorType::SAMPLER_3D_UINT:  // 3D 无符号整数采样器
        case DescriptorType::SAMPLER_2D_MS_UINT:  // 2D 多重采样无符号整数采样器
        case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:  // 2D 多重采样数组无符号整数采样器
            return t == TextureType::UINT;  // 无符号整数纹理

        case DescriptorType::SAMPLER_2D_DEPTH:  // 2D 深度采样器
        case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:  // 2D 数组深度采样器
        case DescriptorType::SAMPLER_CUBE_DEPTH:  // 立方体深度采样器
        case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:  // 立方体数组深度采样器
            /**
             * DEPTH_STENCIL 被视为访问深度分量。OpenGL 4.3
             * 允许指定访问哪个分量，但 Filament 不支持。
             */
            // DEPTH_STENCIL is treated as accessing the depth component. OpenGL 4.3
            // allows to specify which one, but not filament.
            return t == TextureType::DEPTH || t == TextureType::DEPTH_STENCIL;  // 深度或深度模板纹理

        case DescriptorType::SAMPLER_EXTERNAL:  // 外部采样器
            return true;  // 外部采样器与所有纹理类型兼容

        case DescriptorType::UNIFORM_BUFFER:  // 统一缓冲区
        case DescriptorType::SHADER_STORAGE_BUFFER:  // 着色器存储缓冲区
        case DescriptorType::INPUT_ATTACHMENT:  // 输入附件
            return false;  // 这些不是采样器类型，不兼容
    }

    /**
     * 不应该到达这里
     */
    // should never happen
    return false;  // 默认返回不兼容
}


} // namespace filament
