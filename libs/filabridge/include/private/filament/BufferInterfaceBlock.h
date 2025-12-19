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

#ifndef TNT_FILAMENT_DRIVER_BUFFERINTERFACEBLOCK_H
#define TNT_FILAMENT_DRIVER_BUFFERINTERFACEBLOCK_H

#include <backend/DriverEnums.h>

#include <utils/CString.h>
#include <utils/compiler.h>
#include <utils/FixedCapacityVector.h>

#include <math/vec4.h>

#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <assert.h>

namespace filament {

/**
 * 缓冲区接口块类
 * 用于定义和管理着色器中使用的统一缓冲区对象（UBO）或着色器存储缓冲区对象（SSBO）的结构
 */
class BufferInterfaceBlock {
public:
    struct InterfaceBlockEntry {
        std::string_view name;                    // 字段名称
        uint32_t size;                            // 数组大小（如果是数组）
        backend::UniformType type;                // 统一变量类型
        backend::Precision precision;             // 精度
        uint8_t associatedSampler = 0;            // 关联的采样器索引
        backend::FeatureLevel minFeatureLevel;    // 所需的最低功能级别
        std::string_view structName;              // 结构体名称（如果是结构体类型）
        uint32_t stride;                          // 步长（用于数组或结构体）
        std::string_view sizeName;                // 大小参数名称
        
        // 默认构造函数
        InterfaceBlockEntry() = default;
        /**
         * 构造函数
         * @param name 字段名称
         * @param size 数组大小
         * @param type 统一变量类型
         * @param precision 精度（默认值）
         * @param minFeatureLevel 所需的最低功能级别（默认FEATURE_LEVEL_1）
         * @param structName 结构体名称（可选）
         * @param stride 步长（可选）
         * @param sizeName 大小参数名称（可选）
         */
        InterfaceBlockEntry(std::string_view name, uint32_t size, backend::UniformType type,
                backend::Precision precision = {},
                backend::FeatureLevel minFeatureLevel = backend::FeatureLevel::FEATURE_LEVEL_1, std::string_view structName = {},
                uint32_t stride = {}, std::string_view sizeName = {}) noexcept
                : name(name), size(size), type(type), precision(precision),
                associatedSampler(0), minFeatureLevel(minFeatureLevel),
                structName(structName), stride(stride), sizeName(sizeName) {}
        /**
         * 构造函数（带关联采样器）
         * @param name 字段名称
         * @param associatedSampler 关联的采样器索引
         * @param size 数组大小
         * @param type 统一变量类型
         * @param precision 精度（默认值）
         * @param minFeatureLevel 所需的最低功能级别（默认FEATURE_LEVEL_1）
         * @param structName 结构体名称（可选）
         * @param stride 步长（可选）
         * @param sizeName 大小参数名称（可选）
         */
        InterfaceBlockEntry(std::string_view name, uint8_t associatedSampler, uint32_t size, backend::UniformType type,
                backend::Precision precision = {},
                backend::FeatureLevel minFeatureLevel = backend::FeatureLevel::FEATURE_LEVEL_1, std::string_view structName = {},
                uint32_t stride = {}, std::string_view sizeName = {}) noexcept
                : name(name), size(size), type(type), precision(precision),
                associatedSampler(associatedSampler), minFeatureLevel(minFeatureLevel),
                structName(structName), stride(stride), sizeName(sizeName) {}
    };

    // 默认构造函数
    BufferInterfaceBlock();

    // 禁止拷贝构造
    BufferInterfaceBlock(const BufferInterfaceBlock& rhs) = delete;
    // 禁止拷贝赋值
    BufferInterfaceBlock& operator=(const BufferInterfaceBlock& rhs) = delete;

    // 移动构造函数
    BufferInterfaceBlock(BufferInterfaceBlock&& rhs) noexcept;
    // 移动赋值运算符
    BufferInterfaceBlock& operator=(BufferInterfaceBlock&& rhs) noexcept;

    // 析构函数
    ~BufferInterfaceBlock() noexcept;

    using Type = backend::UniformType;
    using Precision = backend::Precision;

    struct FieldInfo {
        utils::CString name;        // name of this field - 字段名称
        uint16_t offset;            // offset in "uint32_t" of this field in the buffer - 字段在缓冲区中的偏移量（以uint32_t为单位）
        uint8_t stride;             // stride in "uint32_t" to the next element - 到下一个元素的步长（以uint32_t为单位）
        Type type;                  // type of this field - 字段类型
        bool isArray;               // true if the field is an array - 如果字段是数组则为true
        uint32_t size;              // size of the array in elements, or 0 if not an array - 数组的元素数量，如果不是数组则为0
        Precision precision;        // precision of this field - 字段精度
        uint8_t associatedSampler;   // sampler associated with this field - 与此字段关联的采样器
        backend::FeatureLevel minFeatureLevel; // below this feature level, this field is not needed - 低于此功能级别时，此字段不需要
        utils::CString structName;  // name of this field structure if type is STRUCT - 如果类型为STRUCT，则为字段结构名称
        utils::CString sizeName;    // name of the size parameter in the shader - 着色器中大小参数的名称
        // returns offset in bytes of this field (at index if an array) - 返回此字段的字节偏移量（如果是数组则为指定索引处）
        inline size_t getBufferOffset(size_t index = 0) const {
            // 检查索引是否在有效范围内
            assert_invariant(index < std::max(1u, size));
            // 计算偏移量：基础偏移 + 步长 * 索引，然后转换为字节数
            return (offset + stride * index) * sizeof(uint32_t);
        }
    };

    /**
     * 对齐方式枚举
     */
    enum class Alignment : uint8_t {
        std140,  // std140对齐方式
        std430   // std430对齐方式
    };

    /**
     * 缓冲区目标类型枚举
     */
    enum class Target : uint8_t  {
        UNIFORM,  // 统一缓冲区（UBO）
        SSBO      // 着色器存储缓冲区（SSBO）
    };

    /**
     * 限定符枚举
     */
    enum class Qualifier : uint8_t {
        COHERENT  = 0x01,
        WRITEONLY = 0x02,
        READONLY  = 0x04,
        VOLATILE  = 0x08,
        RESTRICT  = 0x10
    };

    /**
     * 构建器类
     * 用于构建BufferInterfaceBlock对象
     */
    class Builder {
    public:
        Builder() noexcept;
        ~Builder() noexcept;

        Builder(Builder const& rhs) = default;
        Builder(Builder&& rhs) noexcept = default;
        Builder& operator=(Builder const& rhs) = default;
        Builder& operator=(Builder&& rhs) noexcept = default;

        // Give a name to this buffer interface block - 为此缓冲区接口块命名
        Builder& name(std::string_view interfaceBlockName);

        // Buffer target - 缓冲区目标
        Builder& target(Target target);

        // build and return the BufferInterfaceBlock - 构建并返回BufferInterfaceBlock（设置对齐方式）
        Builder& alignment(Alignment alignment);

        // add a qualifier - 添加限定符
        Builder& qualifier(Qualifier qualifier);

        // a list of this buffer's fields - 添加此缓冲区的字段列表
        Builder& add(std::initializer_list<InterfaceBlockEntry> list);

        // add a variable-sized array. must be the last entry. - 添加可变大小数组，必须是最后一个条目
        Builder& addVariableSizedArray(InterfaceBlockEntry const& item);

        // 构建并返回BufferInterfaceBlock对象
        BufferInterfaceBlock build();

        // 检查是否包含可变大小数组
        bool hasVariableSizeArray() const;

    private:
        friend class BufferInterfaceBlock;
        utils::CString mName;                          // 接口块名称
        std::vector<FieldInfo> mEntries;               // 字段条目列表
        Alignment mAlignment = Alignment::std140;      // 对齐方式（默认std140）
        Target mTarget = Target::UNIFORM;              // 缓冲区目标（默认UNIFORM）
        uint8_t mQualifiers = 0;                       // 限定符标志位
        bool mHasVariableSizeArray = false;            // 是否包含可变大小数组
    };

    // name of this BufferInterfaceBlock interface block - 获取此BufferInterfaceBlock接口块的名称
    std::string_view getName() const noexcept { return { mName.data(), mName.size() }; }

    // size needed for the buffer in bytes - 获取缓冲区所需的字节大小
    size_t getSize() const noexcept { return mSize; }

    // list of information records for each field - 获取每个字段的信息记录列表
    utils::FixedCapacityVector<FieldInfo> const& getFieldInfoList() const noexcept {
        return mFieldInfoList;
    }

    // negative value if name doesn't exist or Panic if exceptions are enabled - 获取字段偏移量，如果名称不存在则返回负值，如果启用异常则抛出异常
    ssize_t getFieldOffset(std::string_view name, size_t index) const;

    // 获取指定名称的字段信息，如果不存在则返回nullptr或抛出异常
    FieldInfo const* getFieldInfo(std::string_view name) const;

    // 检查是否存在指定名称的字段
    bool hasField(std::string_view name) const noexcept {
        return mInfoMap.find(name) != mInfoMap.end();
    }

    // 检查是否为空（没有字段）
    bool isEmpty() const noexcept { return mFieldInfoList.empty(); }

    // 检查在指定功能级别下是否为空（所有字段都不需要）
    bool isEmptyForFeatureLevel(backend::FeatureLevel featureLevel) const noexcept;

    // 获取对齐方式
    Alignment getAlignment() const noexcept { return mAlignment; }

    // 获取缓冲区目标类型
    Target getTarget() const noexcept { return mTarget; }

    // 获取限定符
    uint8_t getQualifier() const noexcept { return mQualifiers; }

private:
    friend class Builder;

    // 从构建器构造（私有构造函数，只能通过Builder构建）
    explicit BufferInterfaceBlock(Builder const& builder) noexcept;

    // 获取指定类型的基础对齐方式（以uint32_t为单位）
    static uint8_t baseAlignmentForType(Type type) noexcept;
    // 获取指定类型的步长（以uint32_t为单位）
    static uint8_t strideForType(Type type, uint32_t stride) noexcept;

    utils::CString mName;                                          // 接口块名称
    utils::FixedCapacityVector<FieldInfo> mFieldInfoList;          // 字段信息列表
    std::unordered_map<std::string_view , uint32_t> mInfoMap;      // 字段名称到索引的映射
    uint32_t mSize = 0; // size in bytes rounded to multiple of 4 - 缓冲区大小（字节数，四舍五入到4的倍数）
    Alignment mAlignment = Alignment::std140;                      // 对齐方式
    Target mTarget = Target::UNIFORM;                              // 缓冲区目标类型
    uint8_t mQualifiers = 0;                                       // 限定符标志位
};

} // namespace filament

#endif // TNT_FILAMENT_DRIVER_BUFFERINTERFACEBLOCK_H
