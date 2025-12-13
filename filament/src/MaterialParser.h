/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef TNT_FILAMENT_MATERIALPARSER_H
#define TNT_FILAMENT_MATERIALPARSER_H

#include <filaflat/ChunkContainer.h>
#include <filaflat/MaterialChunk.h>

#include <filament/MaterialEnums.h>
#include <filament/MaterialChunkType.h>

#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>
#include <backend/Program.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>

#include <array>
#include <optional>
#include <tuple>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filaflat {
class ChunkContainer;
class Unflattener;
}

namespace filament {

class BufferInterfaceBlock;
class SamplerInterfaceBlock;
struct SubpassInfo;
struct MaterialConstant;
struct MaterialPushConstant;

/**
 * MaterialParser 类
 * 
 * 解析材质包，提取着色器代码、接口块和其他材质属性。
 */
class MaterialParser {
public:
    /**
     * 构造函数
     * 
     * @param preferredLanguages 首选着色器语言列表
     * @param data 材质包数据指针
     * @param size 材质包数据大小
     */
    MaterialParser(utils::FixedCapacityVector<backend::ShaderLanguage> preferredLanguages,
            const void* data, size_t size);

    /**
     * 禁止拷贝构造
     */
    MaterialParser(MaterialParser const& rhs) noexcept = delete;
    /**
     * 禁止拷贝赋值
     */
    MaterialParser& operator=(MaterialParser const& rhs) noexcept = delete;

    /**
     * 解析结果枚举
     */
    enum class ParseResult {
        SUCCESS,  // 成功
        ERROR_MISSING_BACKEND,  // 错误：缺少后端
        ERROR_OTHER  // 其他错误
    };

    /**
     * 相等比较运算符
     * 
     * @param rhs 右侧 MaterialParser
     * @return 如果相等则返回 true
     */
    bool operator==(MaterialParser const& rhs) const noexcept;

    /**
     * 解析材质包
     * 
     * @return 解析结果
     */
    ParseResult parse() noexcept;

    /**
     * 计算材质 CRC32
     * 
     * 计算材质的 CRC32 或返回缓存值。
     * 
     * @return CRC32 值
     */
    // Compute the CRC32 of the material or return the cached value.
    uint32_t computeCrc32() const noexcept;
    /**
     * 获取预计算的 CRC32
     * 
     * 返回缓存的 CRC32 或材质文件中内置的 CRC32（如果存在）。
     * 
     * @return CRC32 值（可选）
     */
    // Return the cached computed CRC32 or the CRC32 built into the material file if one exists.
    std::optional<uint32_t> getPrecomputedCrc32() const noexcept;

    /**
     * 获取着色器语言
     * 
     * @return 着色器语言
     */
    backend::ShaderLanguage getShaderLanguage() const noexcept;

    /**
     * 访问器方法
     * 
     * 以下方法用于从材质包中提取各种属性。
     * 所有方法返回 bool 表示是否成功提取值。
     */
    // Accessors
    /**
     * 获取材质版本
     * 
     * @param value 输出：材质版本
     * @return 如果成功则返回 true
     */
    bool getMaterialVersion(uint32_t* value) const noexcept;
    /**
     * 获取功能级别
     * 
     * @param value 输出：功能级别
     * @return 如果成功则返回 true
     */
    bool getFeatureLevel(uint8_t* value) const noexcept;
    /**
     * 获取材质名称
     * 
     * @param value 输出：材质名称
     * @return 如果成功则返回 true
     */
    bool getName(utils::CString*) const noexcept;
    /**
     * 获取缓存 ID
     * 
     * @param cacheId 输出：缓存 ID
     * @return 如果成功则返回 true
     */
    bool getCacheId(uint64_t* cacheId) const noexcept;
    /**
     * 获取 Uniform 接口块（UIB）
     * 
     * @param uib 输出：Uniform 接口块
     * @return 如果成功则返回 true
     */
    bool getUIB(BufferInterfaceBlock* uib) const noexcept;
    /**
     * 获取采样器接口块（SIB）
     * 
     * @param sib 输出：采样器接口块
     * @return 如果成功则返回 true
     */
    bool getSIB(SamplerInterfaceBlock* sib) const noexcept;
    /**
     * 获取子通道信息
     * 
     * @param subpass 输出：子通道信息
     * @return 如果成功则返回 true
     */
    bool getSubpasses(SubpassInfo* subpass) const noexcept;
    /**
     * 获取着色器模型
     * 
     * @param value 输出：着色器模型位掩码
     * @return 如果成功则返回 true
     */
    bool getShaderModels(uint32_t* value) const noexcept;
    /**
     * 获取材质属性
     * 
     * @param value 输出：材质属性位掩码
     * @return 如果成功则返回 true
     */
    bool getMaterialProperties(uint64_t* value) const noexcept;
    /**
     * 获取常量
     * 
     * @param value 输出：常量向量
     * @return 如果成功则返回 true
     */
    bool getConstants(utils::FixedCapacityVector<MaterialConstant>* value) const noexcept;
    /**
     * 获取 Push 常量
     * 
     * @param structVarName 输出：结构变量名称
     * @param value 输出：Push 常量向量
     * @return 如果成功则返回 true
     */
    bool getPushConstants(utils::CString* structVarName,
            utils::FixedCapacityVector<MaterialPushConstant>* value) const noexcept;

    /**
     * 绑定 Uniform 信息容器类型
     */
    using BindingUniformInfoContainer = utils::FixedCapacityVector<std::tuple<
            uint8_t, utils::CString, backend::Program::UniformInfo>>;
    /**
     * 获取绑定 Uniform 信息
     * 
     * @param container 输出：绑定 Uniform 信息容器
     * @return 如果成功则返回 true
     */
    bool getBindingUniformInfo(BindingUniformInfoContainer* container) const noexcept;

    /**
     * 属性信息容器类型
     */
    using AttributeInfoContainer = utils::FixedCapacityVector<
            std::pair<utils::CString, uint8_t>>;
    /**
     * 获取属性信息
     * 
     * @param container 输出：属性信息容器
     * @return 如果成功则返回 true
     */
    bool getAttributeInfo(AttributeInfoContainer* container) const noexcept;

    /**
     * 描述符绑定容器类型
     */
    using DescriptorBindingsContainer = backend::Program::DescriptorSetInfo;
    /**
     * 获取描述符绑定
     * 
     * @param container 输出：描述符绑定容器
     * @return 如果成功则返回 true
     */
    bool getDescriptorBindings(DescriptorBindingsContainer* container) const noexcept;

    /**
     * 描述符堆布局容器类型
     */
    using DescriptorSetLayoutContainer = backend::DescriptorSetLayout;
    /**
     * 获取描述符堆布局
     * 
     * @param container 输出：描述符堆布局容器
     * @return 如果成功则返回 true
     */
    bool getDescriptorSetLayout(DescriptorSetLayoutContainer* container) const noexcept;

    /**
     * 获取深度写入是否已设置
     * 
     * @param value 输出：是否已设置
     * @return 如果成功则返回 true
     */
    bool getDepthWriteSet(bool* value) const noexcept;
    
    /**
     * 获取深度写入
     * 
     * @param value 输出：是否启用深度写入
     * @return 如果成功则返回 true
     */
    bool getDepthWrite(bool* value) const noexcept;
    
    /**
     * 获取双面是否已设置
     * 
     * @param value 输出：是否已设置
     * @return 如果成功则返回 true
     */
    bool getDoubleSidedSet(bool* value) const noexcept;
    
    /**
     * 获取双面
     * 
     * @param value 输出：是否双面渲染
     * @return 如果成功则返回 true
     */
    bool getDoubleSided(bool* value) const noexcept;
    
    /**
     * 获取剔除模式
     * 
     * @param value 输出：剔除模式
     * @return 如果成功则返回 true
     */
    bool getCullingMode(backend::CullingMode* value) const noexcept;
    
    /**
     * 获取透明度模式
     * 
     * @param value 输出：透明度模式
     * @return 如果成功则返回 true
     */
    bool getTransparencyMode(TransparencyMode* value) const noexcept;
    
    /**
     * 获取颜色写入
     * 
     * @param value 输出：是否启用颜色写入
     * @return 如果成功则返回 true
     */
    bool getColorWrite(bool* value) const noexcept;
    
    /**
     * 获取深度测试
     * 
     * @param value 输出：是否启用深度测试
     * @return 如果成功则返回 true
     */
    bool getDepthTest(bool* value) const noexcept;
    
    /**
     * 获取实例化
     * 
     * @param value 输出：是否启用实例化
     * @return 如果成功则返回 true
     */
    bool getInstanced(bool* value) const noexcept;
    
    /**
     * 获取插值模式
     * 
     * @param value 输出：插值模式
     * @return 如果成功则返回 true
     */
    bool getInterpolation(Interpolation* value) const noexcept;
    
    /**
     * 获取顶点域
     * 
     * @param value 输出：顶点域
     * @return 如果成功则返回 true
     */
    bool getVertexDomain(VertexDomain* value) const noexcept;
    
    /**
     * 获取材质域
     * 
     * @param domain 输出：材质域
     * @return 如果成功则返回 true
     */
    bool getMaterialDomain(MaterialDomain* domain) const noexcept;
    
    /**
     * 获取材质变体过滤掩码
     * 
     * @param userVariantFilterMask 输出：用户变体过滤掩码
     * @return 如果成功则返回 true
     */
    bool getMaterialVariantFilterMask(UserVariantFilterMask* userVariantFilterMask) const noexcept;

    /**
     * 获取着色模型
     * 
     * @param value 输出：着色模型
     * @return 如果成功则返回 true
     */
    bool getShading(Shading*) const noexcept;
    
    /**
     * 获取混合模式
     * 
     * @param value 输出：混合模式
     * @return 如果成功则返回 true
     */
    bool getBlendingMode(BlendingMode*) const noexcept;
    
    /**
     * 获取自定义混合函数
     * 
     * @param value 输出：混合函数数组（RGBA 四个通道）
     * @return 如果成功则返回 true
     */
    bool getCustomBlendFunction(std::array<backend::BlendFunction, 4>*) const noexcept;
    
    /**
     * 获取遮罩阈值
     * 
     * @param value 输出：遮罩阈值
     * @return 如果成功则返回 true
     */
    bool getMaskThreshold(float*) const noexcept;
    
    /**
     * 获取 Alpha 到覆盖是否已设置
     * 
     * @param value 输出：是否已设置
     * @return 如果成功则返回 true
     */
    bool getAlphaToCoverageSet(bool*) const noexcept;
    
    /**
     * 获取 Alpha 到覆盖
     * 
     * @param value 输出：是否启用 Alpha 到覆盖
     * @return 如果成功则返回 true
     */
    bool getAlphaToCoverage(bool*) const noexcept;
    
    /**
     * 检查是否有阴影乘数
     * 
     * @param value 输出：是否有阴影乘数
     * @return 如果成功则返回 true
     */
    bool hasShadowMultiplier(bool*) const noexcept;
    
    /**
     * 获取必需属性
     * 
     * @param value 输出：必需属性位集
     * @return 如果成功则返回 true
     */
    bool getRequiredAttributes(AttributeBitset*) const noexcept;
    
    /**
     * 获取折射模式
     * 
     * @param value 输出：折射模式
     * @return 如果成功则返回 true
     */
    bool getRefractionMode(RefractionMode* value) const noexcept;
    
    /**
     * 获取折射类型
     * 
     * @param value 输出：折射类型
     * @return 如果成功则返回 true
     */
    bool getRefractionType(RefractionType* value) const noexcept;
    
    /**
     * 获取反射模式
     * 
     * @param value 输出：反射模式
     * @return 如果成功则返回 true
     */
    bool getReflectionMode(ReflectionMode* value) const noexcept;
    
    /**
     * 检查是否有自定义深度着色器
     * 
     * @param value 输出：是否有自定义深度着色器
     * @return 如果成功则返回 true
     */
    bool hasCustomDepthShader(bool* value) const noexcept;
    
    /**
     * 检查是否有镜面反射抗锯齿
     * 
     * @param value 输出：是否有镜面反射抗锯齿
     * @return 如果成功则返回 true
     */
    bool hasSpecularAntiAliasing(bool* value) const noexcept;
    
    /**
     * 获取镜面反射抗锯齿方差
     * 
     * @param value 输出：方差值
     * @return 如果成功则返回 true
     */
    bool getSpecularAntiAliasingVariance(float* value) const noexcept;
    
    /**
     * 获取镜面反射抗锯齿阈值
     * 
     * @param value 输出：阈值
     * @return 如果成功则返回 true
     */
    bool getSpecularAntiAliasingThreshold(float* value) const noexcept;
    
    /**
     * 获取立体类型
     * 
     * @param value 输出：立体类型
     * @return 如果成功则返回 true
     */
    bool getStereoscopicType(backend::StereoscopicType*) const noexcept;
    
    /**
     * 获取材质 CRC32
     * 
     * @param value 输出：CRC32 值
     * @return 如果成功则返回 true
     */
    bool getMaterialCrc32(uint32_t* value) const noexcept;

    /**
     * 获取着色器
     * 
     * 获取指定着色器模型、变体和阶段的着色器内容。
     * 
     * @param shader 输出：着色器内容
     * @param shaderModel 着色器模型
     * @param variant 变体
     * @param stage 着色器阶段
     * @return 如果成功则返回 true
     */
    bool getShader(filaflat::ShaderContent& shader, backend::ShaderModel shaderModel,
            Variant variant, backend::ShaderStage stage) const noexcept;

    /**
     * 检查是否有着色器
     * 
     * 检查是否存在指定着色器模型、变体和阶段的着色器。
     * 
     * @param model 着色器模型
     * @param variant 变体
     * @param stage 着色器阶段
     * @return 如果存在则返回 true
     */
    bool hasShader(backend::ShaderModel const model,
            Variant const variant, backend::ShaderStage const stage) const noexcept {
        return getMaterialChunk().hasShader(model, variant, stage);
    }

    /**
     * 获取源着色器
     * 
     * 获取原始着色器源代码（如果可用）。
     * 
     * @param cstring 输出：源着色器字符串
     * @return 如果成功则返回 true
     */
    bool getSourceShader(utils::CString* cstring) const noexcept;

    /**
     * 获取材质块
     * 
     * @return 材质块常量引用
     */
    filaflat::MaterialChunk const& getMaterialChunk() const noexcept {
        return mImpl.mMaterialChunk;
    }

private:

    /**
     * 获取块数据（模板辅助方法）
     * 
     * 从材质包中提取指定类型的数据块。
     * 
     * @tparam T 块类型（必须定义 Container 类型和 tag 常量）
     * @param container 输出：容器指针
     * @return 如果成功则返回 true
     */
    template<typename T>
    bool get(typename T::Container* container) const noexcept;

    /**
     * MaterialParserDetails 结构
     * 
     * MaterialParser 的内部实现细节。
     */
    struct MaterialParserDetails {
        /**
         * 构造函数
         * 
         * @param preferredLanguages 首选着色器语言列表
         * @param data 材质包数据指针
         * @param size 材质包数据大小
         */
        MaterialParserDetails(
                utils::FixedCapacityVector<backend::ShaderLanguage> preferredLanguages,
                const void* data, size_t size);

        /**
         * 从简单块获取值
         * 
         * @tparam T 值类型
         * @param type 块类型
         * @param value 输出：值指针
         * @return 如果成功则返回 true
         */
        template<typename T>
        bool getFromSimpleChunk(filamat::ChunkType type, T* value) const noexcept;

    private:
        friend class MaterialParser;

        /**
         * ManagedBuffer 类
         * 
         * 管理材质包数据的缓冲区。
         */
        class ManagedBuffer {
            void* mStart = nullptr;  // 缓冲区起始指针
            size_t mSize = 0;  // 缓冲区大小
        public:
            /**
             * 构造函数
             * 
             * @param start 起始指针
             * @param size 大小
             */
            explicit ManagedBuffer(const void* start, size_t size);
            /**
             * 析构函数
             */
            ~ManagedBuffer() noexcept;
            /**
             * 禁止拷贝构造
             */
            ManagedBuffer(ManagedBuffer const& rhs) = delete;
            /**
             * 禁止拷贝赋值
             */
            ManagedBuffer& operator=(ManagedBuffer const& rhs) = delete;
            /**
             * 获取数据指针
             */
            void* data() const noexcept { return mStart; }
            /**
             * 获取起始指针
             */
            void* begin() const noexcept { return mStart; }
            /**
             * 获取结束指针
             */
            void* end() const noexcept { return (uint8_t*)mStart + mSize; }
            /**
             * 获取大小
             */
            size_t size() const noexcept { return mSize; }
        };

        ManagedBuffer mManagedBuffer;  // 管理的缓冲区
        filaflat::ChunkContainer mChunkContainer;  // 块容器
        utils::FixedCapacityVector<backend::ShaderLanguage> mPreferredLanguages;  // 首选语言列表
        backend::ShaderLanguage mChosenLanguage;  // 选择的语言

        /**
         * 保持 MaterialChunk 在 getShader 调用之间存活，以避免重新加载着色器索引。
         */
        // Keep MaterialChunk alive between calls to getShader to avoid reload the shader index.
        filaflat::MaterialChunk mMaterialChunk;  // 材质块
        filaflat::BlobDictionary mBlobDictionary;  // Blob 字典
    };

    /**
     * 获取块容器（非 const）
     * 
     * @return 块容器引用
     */
    filaflat::ChunkContainer& getChunkContainer() noexcept;
    
    /**
     * 获取块容器（const）
     * 
     * @return 块容器常量引用
     */
    filaflat::ChunkContainer const& getChunkContainer() const noexcept;
    
    MaterialParserDetails mImpl;  // 内部实现细节
    
    /**
     * CRC32 缓存
     * 
     * 0 == 未缓存。从技术上讲，这意味着 CRC32 为 0 的文件永远不会被缓存，
     * 但这不太可能，并且保持它为 32 位值可以保证它是无锁的。
     */
    // 0 == not cached. This technically means that a file with a CRC32 of 0 will never be cached,
    // but this is unlikely, and keeping it a 32-bit value guarantees that it will be lockless.
    mutable std::atomic<uint32_t> mCrc32 = 0;  // CRC32 缓存（原子操作，线程安全）
};

/**
 * Uniform 接口块块结构
 * 
 * 用于从材质包中提取 Uniform 接口块。
 */
struct ChunkUniformInterfaceBlock {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复 Uniform 接口块。
     * 
     * @param unflattener 解扁平化器引用
     * @param uib 输出：Uniform 接口块指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener, BufferInterfaceBlock* uib);
    using Container = BufferInterfaceBlock;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialUib;  // 块类型标签
};

/**
 * 采样器接口块块结构
 * 
 * 用于从材质包中提取采样器接口块。
 */
struct ChunkSamplerInterfaceBlock {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复采样器接口块。
     * 
     * @param unflattener 解扁平化器引用
     * @param sib 输出：采样器接口块指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener, SamplerInterfaceBlock* sib);
    using Container = SamplerInterfaceBlock;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialSib;  // 块类型标签
};

/**
 * 子通道接口块块结构
 * 
 * 用于从材质包中提取子通道信息。
 */
struct ChunkSubpassInterfaceBlock {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复子通道信息。
     * 
     * @param unflattener 解扁平化器引用
     * @param sib 输出：子通道信息指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener, SubpassInfo* sib);
    using Container = SubpassInfo;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialSubpass;  // 块类型标签
};

/**
 * 绑定 Uniform 信息块结构
 * 
 * 用于从材质包中提取绑定 Uniform 信息。
 */
struct ChunkBindingUniformInfo {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复绑定 Uniform 信息。
     * 
     * @param unflattener 解扁平化器引用
     * @param bindingUniformInfo 输出：绑定 Uniform 信息容器指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener,
            MaterialParser::BindingUniformInfoContainer* bindingUniformInfo);
    using Container = MaterialParser::BindingUniformInfoContainer;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialBindingUniformInfo;  // 块类型标签
};

/**
 * 属性信息块结构
 * 
 * 用于从材质包中提取属性信息。
 */
struct ChunkAttributeInfo {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复属性信息。
     * 
     * @param unflattener 解扁平化器引用
     * @param attributeInfoContainer 输出：属性信息容器指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener,
            MaterialParser::AttributeInfoContainer* attributeInfoContainer);
    using Container = MaterialParser::AttributeInfoContainer;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialAttributeInfo;  // 块类型标签
};

/**
 * 描述符绑定信息块结构
 * 
 * 用于从材质包中提取描述符绑定信息。
 */
struct ChunkDescriptorBindingsInfo {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复描述符绑定信息。
     * 
     * @param unflattener 解扁平化器引用
     * @param container 输出：描述符绑定容器指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener,
            MaterialParser::DescriptorBindingsContainer* container);
    using Container = MaterialParser::DescriptorBindingsContainer;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialDescriptorBindingsInfo;  // 块类型标签
};

/**
 * 描述符堆布局信息块结构
 * 
 * 用于从材质包中提取描述符堆布局信息。
 */
struct ChunkDescriptorSetLayoutInfo {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复描述符堆布局信息。
     * 
     * @param unflattener 解扁平化器引用
     * @param container 输出：描述符堆布局容器指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener,
            MaterialParser::DescriptorSetLayoutContainer* container);
    using Container = MaterialParser::DescriptorSetLayoutContainer;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialDescriptorSetLayoutInfo;  // 块类型标签
};

/**
 * 材质常量块结构
 * 
 * 用于从材质包中提取材质常量。
 */
struct ChunkMaterialConstants {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复材质常量。
     * 
     * @param unflattener 解扁平化器引用
     * @param materialConstants 输出：材质常量向量指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener,
            utils::FixedCapacityVector<MaterialConstant>* materialConstants);
    using Container = utils::FixedCapacityVector<MaterialConstant>;  // 容器类型
    static filamat::ChunkType const tag = filamat::MaterialConstants;  // 块类型标签
};

/**
 * 材质 Push 常量块结构
 * 
 * 用于从材质包中提取 Push 常量。
 */
struct ChunkMaterialPushConstants {
    /**
     * 解扁平化
     * 
     * 从扁平化数据中恢复 Push 常量。
     * 
     * @param unflattener 解扁平化器引用
     * @param structVarName 输出：结构变量名称
     * @param materialPushConstants 输出：Push 常量向量指针
     * @return 如果成功则返回 true
     */
    static bool unflatten(filaflat::Unflattener& unflattener, utils::CString* structVarName,
            utils::FixedCapacityVector<MaterialPushConstant>* materialPushConstants);
};

} // namespace filament

#endif // TNT_FILAMENT_MATERIALPARSER_H
