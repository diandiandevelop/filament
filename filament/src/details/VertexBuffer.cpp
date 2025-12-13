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

#include "details/VertexBuffer.h"

#include "details/BufferObject.h"
#include "details/Engine.h"

#include "FilamentAPI-impl.h"

#include <filament/MaterialEnums.h>
#include <filament/VertexBuffer.h>

#include <backend/DriverEnums.h>
#include <backend/BufferDescriptor.h>

#include <utils/CString.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/StaticString.h>
#include <utils/bitset.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

namespace {

/**
 * TODO: 将此值（在 VertexBuffer.h 中定义）与 DriverEnums 的 MAX_VERTEX_BUFFER_COUNT 协调一致
 */
// TODO: reconcile this value (defined in VertexBuffer.h) with DriverEnums' MAX_VERTEX_BUFFER_COUNT
constexpr size_t DOCUMENTED_MAX_VERTEX_BUFFER_COUNT = 8;  // 文档化的最大顶点缓冲区数量

} // anonymous

using namespace backend;
using namespace filament::math;

/**
 * 构建器详情结构
 * 
 * 存储顶点缓冲区的构建参数。
 */
struct VertexBuffer::BuilderDetails {
    /**
     * 属性数据结构
     * 
     * 继承自 Attribute，提供默认值。
     */
    struct AttributeData : Attribute {
        /**
         * 构造函数
         * 
         * 默认类型为 FLOAT4。
         */
        AttributeData() : Attribute{ .type = ElementType::FLOAT4 } {
            static_assert(sizeof(Attribute) == sizeof(AttributeData),  // 静态断言大小匹配
                    "Attribute and Builder::Attribute must match");
        }
    };
    std::array<AttributeData, MAX_VERTEX_ATTRIBUTE_COUNT> mAttributes{};  // 属性数组
    AttributeBitset mDeclaredAttributes;  // 已声明的属性位集
    uint32_t mVertexCount = 0;  // 顶点数量
    uint8_t mBufferCount = 0;  // 缓冲区数量
    bool mBufferObjectsEnabled = false;  // 是否启用缓冲区对象
    bool mAdvancedSkinningEnabled = false;  // 是否启用高级蒙皮
    // TODO: use bits to save memory  // TODO: 使用位以节省内存
};

/**
 * 构建器类型别名
 */
using BuilderType = VertexBuffer;

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
 * 设置顶点数量
 * 
 * @param vertexCount 顶点数量
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::vertexCount(uint32_t const vertexCount) noexcept {
    mImpl->mVertexCount = vertexCount;  // 设置顶点数量
    return *this;  // 返回自身引用
}

/**
 * 启用缓冲区对象
 * 
 * 启用后，可以使用 BufferObject 来管理顶点数据。
 * 
 * @param enabled 是否启用
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::enableBufferObjects(bool const enabled) noexcept {
    mImpl->mBufferObjectsEnabled = enabled;  // 设置启用标志
    return *this;  // 返回自身引用
}

/**
 * 设置缓冲区数量
 * 
 * @param bufferCount 缓冲区数量
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::bufferCount(uint8_t const bufferCount) noexcept {
    mImpl->mBufferCount = bufferCount;  // 设置缓冲区数量
    return *this;  // 返回自身引用
}

/**
 * 设置属性
 * 
 * 配置顶点属性的缓冲区、类型、偏移和步长。
 * 
 * @param attribute 顶点属性
 * @param bufferIndex 缓冲区索引
 * @param attributeType 属性类型
 * @param byteOffset 字节偏移
 * @param byteStride 字节步长（0 表示自动计算）
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::attribute(VertexAttribute const attribute,
        uint8_t const bufferIndex,  // 缓冲区索引
        AttributeType const attributeType,  // 属性类型
        uint32_t const byteOffset,  // 字节偏移
        uint8_t byteStride) noexcept {  // 字节步长

    size_t const attributeSize = Driver::getElementTypeSize(attributeType);  // 获取属性类型大小
    if (byteStride == 0) {  // 如果步长为 0
        byteStride = uint8_t(attributeSize);  // 自动设置为属性大小
    }

    if (size_t(attribute) < MAX_VERTEX_ATTRIBUTE_COUNT &&  // 如果属性索引有效
            size_t(bufferIndex) < MAX_VERTEX_ATTRIBUTE_COUNT) {  // 如果缓冲区索引有效
        auto& entry = mImpl->mAttributes[attribute];  // 获取属性条目
        entry.buffer = bufferIndex;  // 设置缓冲区索引
        entry.offset = byteOffset;  // 设置字节偏移
        entry.stride = byteStride;  // 设置字节步长
        entry.type = attributeType;  // 设置属性类型
        if (attribute == BONE_INDICES) {  // 如果是骨骼索引属性
            /**
             * BONE_INDICES 必须始终是整数类型
             */
            // BONE_INDICES must always be an integer type
            entry.flags |= Attribute::FLAG_INTEGER_TARGET;  // 设置整数目标标志
        }

        mImpl->mDeclaredAttributes.set(attribute);  // 标记属性已声明
    } else {  // 如果索引超出范围
        LOG(WARNING) << "Ignoring VertexBuffer attribute, the limit of "  // 记录警告
                     << MAX_VERTEX_ATTRIBUTE_COUNT << " attributes has been exceeded";
    }
    return *this;  // 返回自身引用
}

/**
 * 设置属性归一化
 * 
 * 设置顶点属性是否归一化。
 * 
 * @param attribute 顶点属性
 * @param normalized 是否归一化
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::normalized(VertexAttribute const attribute,
        bool const normalized) noexcept {
    if (size_t(attribute) < MAX_VERTEX_ATTRIBUTE_COUNT) {  // 如果属性索引有效
        auto& entry = mImpl->mAttributes[attribute];  // 获取属性条目
        if (normalized) {  // 如果归一化
            entry.flags |= Attribute::FLAG_NORMALIZED;  // 设置归一化标志
        } else {  // 否则
            entry.flags &= ~Attribute::FLAG_NORMALIZED;  // 清除归一化标志
        }
    }
    return *this;  // 返回自身引用
}

/**
 * 启用高级蒙皮
 * 
 * 启用高级蒙皮功能。
 * 
 * @param enabled 是否启用
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::advancedSkinning(bool const enabled) noexcept {
    mImpl->mAdvancedSkinningEnabled = enabled;  // 设置高级蒙皮标志
    return *this;  // 返回自身引用
}

/**
 * 设置名称
 * 
 * @param name 名称字符串
 * @param len 名称长度
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::name(const char* name, size_t const len) noexcept {
    return BuilderNameMixin::name(name, len);  // 调用名称混入方法
}

/**
 * 设置名称（StaticString 版本）
 * 
 * @param name 名称静态字符串
 * @return 构建器引用（支持链式调用）
 */
VertexBuffer::Builder& VertexBuffer::Builder::name(utils::StaticString const& name) noexcept {
    return BuilderNameMixin::name(name);  // 调用名称混入方法
}

/**
 * 构建顶点缓冲区
 * 
 * 根据构建器配置创建顶点缓冲区对象。
 * 
 * @param engine 引擎引用
 * @return 顶点缓冲区指针
 */
VertexBuffer* VertexBuffer::Builder::build(Engine& engine) {
    /**
     * 验证顶点数量和缓冲区数量
     */
    FILAMENT_CHECK_PRECONDITION(mImpl->mVertexCount > 0) << "vertexCount cannot be 0";  // 检查顶点数量
    FILAMENT_CHECK_PRECONDITION(mImpl->mBufferCount > 0) << "bufferCount cannot be 0";  // 检查缓冲区数量

    /**
     * 静态断言：文档化的最大值不应超过实际最大值
     */
    static_assert(DOCUMENTED_MAX_VERTEX_BUFFER_COUNT <= MAX_VERTEX_BUFFER_COUNT);  // 静态断言

    /**
     * 验证缓冲区数量不超过文档化的最大值
     */
    auto const& featureFlags = static_cast<FEngine*>(&engine)->features.engine.debug;  // 获取特性标志
    FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION(
            mImpl->mBufferCount <= DOCUMENTED_MAX_VERTEX_BUFFER_COUNT,  // 检查缓冲区数量
            featureFlags.assert_vertex_buffer_count_exceeds_8)  // 特性标志
            << "bufferCount cannot be more than " << DOCUMENTED_MAX_VERTEX_BUFFER_COUNT;

    /**
     * 检查是否有未使用的缓冲区槽
     * 
     * 这有助于防止错误，因为上传到未使用的槽可能会在后端触发未定义行为。
     */
    // Next we check if any unused buffer slots have been allocated. This helps prevent errors
    // because uploading to an unused slot can trigger undefined behavior in the backend.
    auto const& declaredAttributes = mImpl->mDeclaredAttributes;  // 获取已声明属性
    auto const& attributes = mImpl->mAttributes;  // 获取属性数组
    utils::bitset32 attributedBuffers;  // 已使用缓冲区位集

    /**
     * 遍历所有已声明的属性
     */
    declaredAttributes.forEachSetBit([&](size_t const j) {  // 遍历已设置的位

        /**
         * 验证属性偏移必须是 4 的倍数
         */
        FILAMENT_CHECK_PRECONDITION((attributes[j].offset & 0x3u) == 0)  // 检查偏移对齐
                << "attribute " << j << " offset=" << attributes[j].offset
                << " is not multiple of 4";

        /**
         * 验证属性步长必须是 4 的倍数（受特性标志保护）
         */
        FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION((attributes[j].stride & 0x3u) == 0,  // 检查步长对齐
                featureFlags.assert_vertex_buffer_attribute_stride_mult_of_4)  // 特性标志
                << "attribute " << j << " stride=" << +attributes[j].stride
                << " is not multiple of 4";

        /**
         * 在特性级别 0 下，不支持整数目标标志
         */
        if (engine.getActiveFeatureLevel() == FeatureLevel::FEATURE_LEVEL_0) {  // 如果是特性级别 0
            FILAMENT_CHECK_PRECONDITION(!(attributes[j].flags & Attribute::FLAG_INTEGER_TARGET))  // 检查整数目标标志
                    << "Attribute::FLAG_INTEGER_TARGET not supported at FEATURE_LEVEL_0";
        }

        /**
         * 检查整数属性是否使用了无效的类型
         */
        // also checks that we don't use an invalid type with integer attributes
        if (attributes[j].flags & Attribute::FLAG_INTEGER_TARGET) {  // 如果是整数目标
            using ET = ElementType;  // 元素类型别名
            /**
             * 无效的整数类型位掩码
             */
            constexpr uint32_t invalidIntegerTypes =
                    (1 << int(ET::FLOAT)) |  // FLOAT
                    (1 << int(ET::FLOAT2)) |  // FLOAT2
                    (1 << int(ET::FLOAT3)) |  // FLOAT3
                    (1 << int(ET::FLOAT4)) |  // FLOAT4
                    (1 << int(ET::HALF)) |  // HALF
                    (1 << int(ET::HALF2)) |  // HALF2
                    (1 << int(ET::HALF3)) |  // HALF3
                    (1 << int(ET::HALF4));  // HALF4

            FILAMENT_CHECK_PRECONDITION(!(invalidIntegerTypes & (1 << int(attributes[j].type))))  // 检查类型
                    << "invalid integer vertex attribute type " << int(attributes[j].type);
        }

        /**
         * 更新已使用缓冲区集合
         */
        // update set of used buffers
        attributedBuffers.set(attributes[j].buffer);  // 设置缓冲区位
    });

    /**
     * 验证所有缓冲区槽都已分配给属性
     */
    FILAMENT_CHECK_PRECONDITION(attributedBuffers.count() == mImpl->mBufferCount)  // 检查缓冲区数量
            << "At least one buffer slot was never assigned to an attribute.";

    /**
     * 验证高级蒙皮配置
     */
    if (mImpl->mAdvancedSkinningEnabled) {  // 如果启用高级蒙皮
        FILAMENT_CHECK_PRECONDITION(!mImpl->mDeclaredAttributes[VertexAttribute::BONE_INDICES])  // 检查骨骼索引
                << "Vertex buffer attribute BONE_INDICES is already defined, "
                   "no advanced skinning is allowed";
        FILAMENT_CHECK_PRECONDITION(!mImpl->mDeclaredAttributes[VertexAttribute::BONE_WEIGHTS])  // 检查骨骼权重
                << "Vertex buffer attribute BONE_WEIGHTS is already defined, "
                   "no advanced skinning is allowed";
        FILAMENT_CHECK_PRECONDITION(mImpl->mBufferCount < (MAX_VERTEX_BUFFER_COUNT - 2))  // 检查缓冲区数量
                << "Vertex buffer uses to many buffers (" << mImpl->mBufferCount << ")";
    }

    return downcast(engine).createVertexBuffer(*this);  // 创建顶点缓冲区
}

// ------------------------------------------------------------------------------------------------

/**
 * 顶点缓冲区构造函数
 * 
 * 创建顶点缓冲区对象并分配驱动资源。
 * 
 * @param engine 引擎引用
 * @param builder 构建器引用
 */
FVertexBuffer::FVertexBuffer(FEngine& engine, const Builder& builder)
        : mVertexCount(builder->mVertexCount),  // 初始化顶点数量
          mBufferCount(builder->mBufferCount),  // 初始化缓冲区数量
          mBufferObjectsEnabled(builder->mBufferObjectsEnabled),  // 初始化缓冲区对象启用标志
          mAdvancedSkinningEnabled(builder->mAdvancedSkinningEnabled){  // 初始化高级蒙皮启用标志
    /**
     * 复制属性数组和已声明属性
     */
    std::copy(std::begin(builder->mAttributes), std::end(builder->mAttributes), mAttributes.begin());  // 复制属性
    mDeclaredAttributes = builder->mDeclaredAttributes;  // 复制已声明属性

    /**
     * 如果启用高级蒙皮，自动添加骨骼索引和权重属性
     */
    if (mAdvancedSkinningEnabled) {  // 如果启用高级蒙皮
        /**
         * 设置骨骼索引属性
         */
        mAttributes[BONE_INDICES] = {
                .offset = 0,  // 偏移为 0
                .stride = 8,  // 步长为 8 字节（USHORT4）
                .buffer = mBufferCount,  // 使用下一个缓冲区槽
                .type = AttributeType::USHORT4,  // 类型为无符号短整型 4 元组
                .flags = Attribute::FLAG_INTEGER_TARGET,  // 整数目标标志
        };
        mDeclaredAttributes.set(BONE_INDICES);  // 标记已声明
        mBufferCount++;  // 递增缓冲区数量

        /**
         * 设置骨骼权重属性
         */
        mAttributes[BONE_WEIGHTS] = {
                .offset = 0,  // 偏移为 0
                .stride = 16,  // 步长为 16 字节（FLOAT4）
                .buffer = mBufferCount,  // 使用下一个缓冲区槽
                .type = AttributeType::FLOAT4,  // 类型为浮点型 4 元组
                .flags = 0,  // 无标志
        };
        mDeclaredAttributes.set(BONE_WEIGHTS);  // 标记已声明
        mBufferCount++;  // 递增缓冲区数量
    } else {  // 否则
        /**
         * 因为材质的 SKN 变体同时支持蒙皮和变形，它期望
         * 与两者相关的所有属性都存在。反过来，这意味着用于
         * 蒙皮和/或变形的 VertexBuffer 需要提供所有相关属性。
         * 目前，后端必须处理在 VertexBuffer 中禁用但在着色器中
         * 声明的数组。在 GL 中这是自动的，在 Vulkan/Metal 中，后端必须
         * 使用虚拟缓冲区。
         * - 一个复杂之处是后端需要知道属性在着色器中是声明为浮点还是
         * 整数，无论属性在 VertexBuffer 中是否启用（例如，变形属性可能
         * 被禁用，因为我们只使用蒙皮）。
         * - 另一个复杂之处是 SKN 变体由可渲染对象选择
         * （而不是 RenderPrimitive），所以一个既没有蒙皮也没有变形的图元
         * 使用 SKN 变体渲染是可能且有效的（变形/蒙皮将动态禁用）。
         *
         * 因此，我们需要在所有我们知道在着色器中是整数的属性上设置
         * FLAG_INTEGER_TARGET，底线是 BONE_INDICES 始终需要设置为
         * FLAG_INTEGER_TARGET。
         */
        // Because the Material's SKN variant supports both skinning and morphing, it expects
        // all attributes related to *both* to be present. In turn, this means that a VertexBuffer
        // used for skinning and/or morphing, needs to provide all related attributes.
        // Currently, the backend must handle disabled arrays in the VertexBuffer that are declared
        // in the shader. In GL this happens automatically, in vulkan/metal, the backends have to
        // use dummy buffers.
        // - A complication is that backends need to know if an attribute is declared as float or
        // integer in the shader, regardless of if the attribute is enabled or not in the
        // VertexBuffer (e.g. the morphing attributes could be disabled because we're only using
        // skinning).
        // - Another complication is that the SKN variant is selected by the renderable
        // (as opposed to the RenderPrimitive), so it's possible and valid for a primitive
        // that isn't skinned nor morphed to be rendered with the SKN variant (morphing/skinning
        // will then be disabled dynamically).
        //
        // Because of that we need to set FLAG_INTEGER_TARGET on all attributes that we know are
        // integer in the shader and the bottom line is that BONE_INDICES always needs to be set to
        // FLAG_INTEGER_TARGET.
        mAttributes[BONE_INDICES].flags |= Attribute::FLAG_INTEGER_TARGET;  // 设置整数目标标志
    }

    /**
     * 创建顶点缓冲区信息和句柄
     */
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API

    mVertexBufferInfoHandle = engine.getVertexBufferInfoFactory().create(driver,  // 创建顶点缓冲区信息
            mBufferCount, mDeclaredAttributes.count(), mAttributes);  // 缓冲区数量、属性数量、属性数组

    mHandle = driver.createVertexBuffer(mVertexCount, mVertexBufferInfoHandle,  // 创建顶点缓冲区
            utils::ImmutableCString{ builder.getName() });  // 名称

    /**
     * 计算缓冲区大小
     */
    // calculate buffer sizes
    size_t bufferSizes[MAX_VERTEX_BUFFER_COUNT] = {};  // 缓冲区大小数组

    /**
     * Lambda 函数：判断是否应该创建缓冲区
     */
    auto shouldCreateBuffer = [this](size_t attributeIndex) {  // Lambda 函数
        const uint8_t slot = mAttributes[attributeIndex].buffer;  // 获取缓冲区槽
        return mDeclaredAttributes[attributeIndex] && slot != Attribute::BUFFER_UNUSED &&  // 属性已声明且槽未使用
                !mBufferObjects[slot];  // 且缓冲区对象不存在
    };
    
    /**
     * Lambda 函数：更新缓冲区大小
     */
    auto updateBufferSize = [&bufferSizes, this](size_t attributeIndex) {  // Lambda 函数
        const uint32_t offset = mAttributes[attributeIndex].offset;  // 获取偏移
        const uint8_t stride = mAttributes[attributeIndex].stride;  // 获取步长
        const uint8_t slot = mAttributes[attributeIndex].buffer;  // 获取缓冲区槽
        const size_t end = offset + mVertexCount * stride;  // 计算结束位置
        assert_invariant(slot < MAX_VERTEX_BUFFER_COUNT);  // 断言槽有效
        bufferSizes[slot] = std::max(bufferSizes[slot], end);  // 更新最大大小
    };

    /**
     * 计算缓冲区大小
     */
    if (!mBufferObjectsEnabled) {  // 如果未启用缓冲区对象
        #pragma nounroll  // 不展开循环
        for (size_t i = 0, n = mAttributes.size(); i < n; ++i) {  // 遍历所有属性
            if (shouldCreateBuffer(i)) {  // 如果应该创建缓冲区
                updateBufferSize(i);  // 更新缓冲区大小
            }
        }
    } else if (mAdvancedSkinningEnabled) {  // 否则如果启用高级蒙皮
        /**
         * 对于高级蒙皮模式，只创建相关缓冲区（BONE_INDICES 和 BONE_WEIGHTS）。
         * 我们上面已经手动填充了这些缓冲区的相关属性。
         */
        // For advanced skinning mode, only relevant buffers (BONE_INDICES & BONE_WEIGHTS) are
        // created. We manually populated the relevant attributes for those buffers above.
        updateBufferSize(BONE_INDICES);  // 更新骨骼索引缓冲区大小
        updateBufferSize(BONE_WEIGHTS);  // 更新骨骼权重缓冲区大小
    }

    /**
     * 创建缓冲区
     */
    // create buffers
    for (size_t i = 0; i < MAX_VERTEX_BUFFER_COUNT; ++i) {  // 遍历所有缓冲区槽
        if (bufferSizes[i] == 0 || mBufferObjects[i]) {  // 如果大小为 0 或已存在缓冲区对象
            continue;  // 跳过
        }
        BufferObjectHandle const bo = driver.createBufferObject(bufferSizes[i],  // 创建缓冲区对象
                BufferObjectBinding::VERTEX, BufferUsage::STATIC,  // 绑定类型、使用方式
                utils::ImmutableCString{ builder.getName() });  // 名称
        driver.setVertexBufferObject(mHandle, i, bo);  // 设置顶点缓冲区对象
        mBufferObjects[i] = bo;  // 存储句柄
    }
}

/**
 * 终止顶点缓冲区
 * 
 * 释放驱动资源，对象变为无效。
 * 
 * @param engine 引擎引用
 */
void FVertexBuffer::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();  // 获取驱动 API
    if (!mBufferObjectsEnabled) {  // 如果未启用缓冲区对象
        for (BufferObjectHandle const& bo : mBufferObjects) {  // 遍历所有缓冲区对象
            driver.destroyBufferObject(bo);  // 销毁缓冲区对象
        }
    }
    driver.destroyVertexBuffer(mHandle);  // 销毁顶点缓冲区
    engine.getVertexBufferInfoFactory().destroy(driver, mVertexBufferInfoHandle);  // 销毁顶点缓冲区信息
}

/**
 * 获取顶点数量
 * 
 * @return 顶点数量
 */
size_t FVertexBuffer::getVertexCount() const noexcept {
    return mVertexCount;  // 返回顶点数量
}

/**
 * 设置缓冲区数据（非缓冲区对象模式）
 * 
 * 将数据上传到指定缓冲区槽。
 * 
 * @param engine 引擎引用
 * @param bufferIndex 缓冲区索引
 * @param buffer 缓冲区描述符（会被移动）
 * @param byteOffset 字节偏移
 */
void FVertexBuffer::setBufferAt(FEngine& engine, uint8_t const bufferIndex,
        backend::BufferDescriptor&& buffer, uint32_t const byteOffset) {

    FILAMENT_CHECK_PRECONDITION(!mBufferObjectsEnabled)  // 检查未启用缓冲区对象
            << "buffer objects enabled, use setBufferObjectAt() instead";

    FILAMENT_CHECK_PRECONDITION(bufferIndex < mBufferCount)  // 检查缓冲区索引
            << "bufferIndex must be < bufferCount";

    FILAMENT_CHECK_PRECONDITION((byteOffset & 0x3) == 0)  // 检查偏移对齐
        << "byteOffset must be a multiple of 4";

    engine.getDriverApi().updateBufferObject(mBufferObjects[bufferIndex],  // 更新缓冲区对象
            std::move(buffer), byteOffset);  // 移动缓冲区描述符和偏移
}

/**
 * 设置缓冲区对象（缓冲区对象模式）
 * 
 * 将外部缓冲区对象绑定到指定缓冲区槽。
 * 
 * @param engine 引擎引用
 * @param bufferIndex 缓冲区索引
 * @param bufferObject 缓冲区对象常量指针
 */
void FVertexBuffer::setBufferObjectAt(FEngine& engine, uint8_t const bufferIndex,
        FBufferObject const * bufferObject) {

    FILAMENT_CHECK_PRECONDITION(mBufferObjectsEnabled)  // 检查已启用缓冲区对象
            << "buffer objects disabled, use setBufferAt() instead";

    FILAMENT_CHECK_PRECONDITION(bufferObject->getBindingType() == BufferObject::BindingType::VERTEX)  // 检查绑定类型
            << "bufferObject binding type must be VERTEX but is "
            << to_string(bufferObject->getBindingType());

    FILAMENT_CHECK_PRECONDITION(bufferIndex < mBufferCount)  // 检查缓冲区索引
            << "bufferIndex must be < bufferCount";

    auto const hwBufferObject = bufferObject->getHwHandle();  // 获取硬件句柄
    engine.getDriverApi().setVertexBufferObject(mHandle, bufferIndex, hwBufferObject);  // 设置顶点缓冲区对象
    /**
     * 存储句柄以便在额外骨骼索引和权重定义的情况下重新创建 VertexBuffer
     * 仅在缓冲区对象模式下使用
     */
    // store handle to recreate VertexBuffer in the case extra bone indices and weights definition
    // used only in buffer object mode
    mBufferObjects[bufferIndex] = hwBufferObject;  // 存储句柄
}

/**
 * 更新骨骼索引和权重
 * 
 * 更新高级蒙皮模式的骨骼索引和权重数据。
 * 
 * @param engine 引擎引用
 * @param skinJoints 骨骼关节数据（唯一指针，会被释放）
 * @param skinWeights 骨骼权重数据（唯一指针，会被释放）
 */
void FVertexBuffer::updateBoneIndicesAndWeights(FEngine& engine,
        std::unique_ptr<uint16_t[]> skinJoints,  // 骨骼关节数据
        std::unique_ptr<float[]> skinWeights) {  // 骨骼权重数据
    FILAMENT_CHECK_PRECONDITION(mAdvancedSkinningEnabled) << "No advanced skinning enabled";  // 检查高级蒙皮启用
    auto jointsData = skinJoints.release();  // 释放骨骼关节数据所有权
    uint8_t const indicesIndex = mAttributes[BONE_INDICES].buffer;  // 获取骨骼索引缓冲区索引
    engine.getDriverApi().updateBufferObject(mBufferObjects[indicesIndex],  // 更新缓冲区对象
            { jointsData, mVertexCount * 8,  // 数据指针、大小（USHORT4 = 8 字节）
              [](void* buffer, size_t, void*) { delete[] static_cast<uint16_t*>(buffer); } },  // 删除回调
            0);  // 偏移为 0

    auto weightsData = skinWeights.release();  // 释放骨骼权重数据所有权
    uint8_t const weightsIndex = mAttributes[BONE_WEIGHTS].buffer;  // 获取骨骼权重缓冲区索引
    engine.getDriverApi().updateBufferObject(mBufferObjects[weightsIndex],  // 更新缓冲区对象
            { weightsData, mVertexCount * 16,  // 数据指针、大小（FLOAT4 = 16 字节）
              [](void* buffer, size_t, void*) { delete[] static_cast<float*>(buffer); } },  // 删除回调
            0);  // 偏移为 0
}

} // namespace filament
