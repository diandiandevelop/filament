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

#ifndef TNT_FILAMENT_DETAILS_MATERIALINSTANCE_H
#define TNT_FILAMENT_DETAILS_MATERIALINSTANCE_H

#include "downcast.h"

#include "UniformBuffer.h"

#include "ds/DescriptorSet.h"

#include "details/BufferAllocator.h"
#include "details/Engine.h"

#include "private/backend/DriverApi.h"

#include <filament/MaterialInstance.h>

#include <private/filament/Variant.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/BitmaskEnum.h>
#include <utils/bitset.h>
#include <utils/CString.h>

#include <tsl/robin_map.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <string_view>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class FMaterial;
class FTexture;

/**
 * 材质实例实现类
 * 
 * 材质实例是材质的运行时实例，包含材质的参数值（uniform 和 sampler）。
 * 每个材质实例可以有不同的参数值，但共享相同的材质定义（着色器、变体等）。
 * 
 * 实现细节：
 * - 支持 UBO 批处理（将多个实例的 uniform 数据打包到单个 UBO 中）
 * - 管理描述符集（用于绑定 uniform 和 sampler）
 * - 支持裁剪矩形、模板状态、深度状态等渲染状态
 */
class FMaterialInstance : public MaterialInstance {
public:
    /**
     * 构造函数（从材质创建）
     * 
     * @param engine 引擎引用
     * @param material 材质指针
     * @param name 实例名称
     */
    FMaterialInstance(FEngine& engine, FMaterial const* material, const char* name) noexcept;
    
    /**
     * 构造函数（从其他实例复制）
     * 
     * @param engine 引擎引用
     * @param other 要复制的实例指针
     * @param name 新实例名称
     */
    FMaterialInstance(FEngine& engine, FMaterialInstance const* other, const char* name);
    
    FMaterialInstance(const FMaterialInstance& rhs) = delete;  // 禁止拷贝构造
    FMaterialInstance& operator=(const FMaterialInstance& rhs) = delete;  // 禁止拷贝赋值

    /**
     * 复制实例
     * 
     * 创建材质实例的副本。
     * 
     * @param other 要复制的实例指针
     * @param name 新实例名称
     * @return 新实例指针
     */
    static FMaterialInstance* duplicate(FMaterialInstance const* other, const char* name) noexcept;

    /**
     * 析构函数
     */
    ~FMaterialInstance() noexcept;

    /**
     * 终止
     * 
     * 释放资源。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine);
    
    /**
     * 提交（使用引擎）
     * 
     * 将 uniform 数据提交到 GPU。
     * 
     * @param engine 引擎引用
     */
    void commit(FEngine& engine) const;

    /**
     * 提交（使用驱动 API 和 UBO 管理器）
     * 
     * 将 uniform 数据提交到 GPU，支持 UBO 批处理。
     * 
     * @param driver 驱动 API 引用
     * @param uboManager UBO 管理器指针（可选）
     */
    void commit(FEngine::DriverApi& driver, UboManager* uboManager) const;

    /**
     * 使用
     * 
     * 绑定描述符集到渲染管线。
     * 
     * @param driver 驱动 API 引用
     * @param variant 着色器变体（默认为空，使用默认变体）
     */
    void use(FEngine::DriverApi& driver, Variant variant = {}) const;

    /**
     * 分配 UBO 分配
     * 
     * 为材质实例分配 UBO 槽位（用于 UBO 批处理）。
     * 
     * @param ubHandle UBO 句柄
     * @param id 分配 ID
     * @param offset 偏移量（字节）
     */
    void assignUboAllocation(const backend::Handle<backend::HwBufferObject>& ubHandle,
            BufferAllocator::AllocationId id,
            BufferAllocator::allocation_size_t offset);

    /**
     * 获取分配 ID
     * 
     * @return 分配 ID，如果未分配返回无效 ID
     */
    BufferAllocator::AllocationId getAllocationId() const noexcept;

    /**
     * 获取材质
     * 
     * @return 材质指针
     */
    FMaterial const* getMaterial() const noexcept { return mMaterial; }

    /**
     * 获取排序键
     * 
     * 用于渲染排序，优化绘制调用。
     * 
     * @return 排序键
     */
    uint64_t getSortingKey() const noexcept { return mMaterialSortingKey; }

    /**
     * 获取统一缓冲区
     * 
     * @return 统一缓冲区常量引用
     */
    UniformBuffer const& getUniformBuffer() const noexcept { return mUniforms; }

    /**
     * 设置裁剪矩形
     * 
     * 设置渲染裁剪矩形（左、底、宽、高）。
     * 
     * @param left 左边界
     * @param bottom 底边界
     * @param width 宽度
     * @param height 高度
     */
    void setScissor(uint32_t const left, uint32_t const bottom, uint32_t const width, uint32_t const height) noexcept {
        constexpr uint32_t maxvalu = std::numeric_limits<int32_t>::max();  // 最大值
        mScissorRect = { int32_t(left), int32_t(bottom),
                std::min(width, maxvalu), std::min(height, maxvalu) };  // 限制在有效范围内
        mHasScissor = true;  // 标记已设置裁剪矩形
    }

    /**
     * 取消设置裁剪矩形
     * 
     * 将裁剪矩形重置为全屏。
     */
    void unsetScissor() noexcept {
        constexpr uint32_t maxvalu = std::numeric_limits<int32_t>::max();  // 最大值
        mScissorRect = { 0, 0, maxvalu, maxvalu };  // 设置为全屏
        mHasScissor = false;  // 标记未设置裁剪矩形
    }

    /**
     * 获取裁剪矩形
     * 
     * @return 裁剪矩形常量引用
     */
    backend::Viewport const& getScissor() const noexcept { return mScissorRect; }

    /**
     * 是否有裁剪矩形
     * 
     * @return 如果设置了裁剪矩形返回 true，否则返回 false
     */
    bool hasScissor() const noexcept { return mHasScissor; }

    backend::CullingMode getCullingMode() const noexcept { return mCulling; }

    backend::CullingMode getShadowCullingMode() const noexcept { return mShadowCulling; }

    bool isColorWriteEnabled() const noexcept { return mColorWrite; }

    bool isDepthWriteEnabled() const noexcept { return mDepthWrite; }

    bool isStencilWriteEnabled() const noexcept { return mStencilState.stencilWrite; }

    backend::StencilState getStencilState() const noexcept { return mStencilState; }

    TransparencyMode getTransparencyMode() const noexcept { return mTransparencyMode; }

    backend::RasterState::DepthFunc getDepthFunc() const noexcept { return mDepthFunc; }

    void setDepthFunc(backend::RasterState::DepthFunc const depthFunc) noexcept {
        mDepthFunc = depthFunc;
    }

    void setPolygonOffset(float const scale, float const constant) noexcept {
        // handle reversed Z
        mPolygonOffset = { -scale, -constant };
    }

    backend::PolygonOffset getPolygonOffset() const noexcept { return mPolygonOffset; }

    void setMaskThreshold(float threshold) noexcept;

    float getMaskThreshold() const noexcept;

    void setSpecularAntiAliasingVariance(float variance) noexcept;

    float getSpecularAntiAliasingVariance() const noexcept;

    void setSpecularAntiAliasingThreshold(float threshold) noexcept;

    float getSpecularAntiAliasingThreshold() const noexcept;

    void setDoubleSided(bool doubleSided) noexcept;

    bool isDoubleSided() const noexcept;

    void setTransparencyMode(TransparencyMode mode) noexcept;

    void setCullingMode(CullingMode const culling) noexcept {
        mCulling = culling;
        mShadowCulling = culling;
    }

    void setCullingMode(CullingMode const color, CullingMode const shadow) noexcept {
        mCulling = color;
        mShadowCulling = shadow;
    }

    void setColorWrite(bool const enable) noexcept { mColorWrite = enable; }

    void setDepthWrite(bool const enable) noexcept { mDepthWrite = enable; }

    void setStencilWrite(bool const enable) noexcept { mStencilState.stencilWrite = enable; }

    void setDepthCulling(bool enable) noexcept;

    bool isDepthCullingEnabled() const noexcept;

    void setStencilCompareFunction(StencilCompareFunc const func, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.stencilFunc = func;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.stencilFunc = func;
        }
    }

    void setStencilOpStencilFail(StencilOperation const op, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.stencilOpStencilFail = op;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.stencilOpStencilFail = op;
        }
    }

    void setStencilOpDepthFail(StencilOperation const op, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.stencilOpDepthFail = op;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.stencilOpDepthFail = op;
        }
    }

    void setStencilOpDepthStencilPass(StencilOperation const op, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.stencilOpDepthStencilPass = op;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.stencilOpDepthStencilPass = op;
        }
    }

    void setStencilReferenceValue(uint8_t const value, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.ref = value;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.ref = value;
        }
    }

    void setStencilReadMask(uint8_t const readMask, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.readMask = readMask;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.readMask = readMask;
        }
    }

    void setStencilWriteMask(uint8_t const writeMask, StencilFace const face) noexcept {
        if (any(face & StencilFace::FRONT)) {
            mStencilState.front.writeMask = writeMask;
        }
        if (any(face & StencilFace::BACK)) {
            mStencilState.back.writeMask = writeMask;
        }
    }

    void setDefaultInstance(bool const value) noexcept {
        mIsDefaultInstance = value;
    }

    bool isDefaultInstance() const noexcept {
        return mIsDefaultInstance;
    }

    bool isUsingUboBatching() const noexcept { return mUseUboBatching; }

    /**
     * 修复缺失的采样器
     * 
     * 由引擎调用，确保未设置的采样器使用占位符初始化。
     */
    void fixMissingSamplers() const;

    /**
     * 获取名称
     * 
     * @return 实例名称（C 字符串）
     */
    const char* getName() const noexcept;

    /**
     * 设置参数（纹理）
     * 
     * 设置纹理采样器参数。
     * 
     * @param name 参数名称
     * @param texture 纹理句柄
     * @param params 采样器参数
     */
    void setParameter(std::string_view name,
            backend::Handle<backend::HwTexture> texture, backend::SamplerParams params);

    using MaterialInstance::setParameter;

private:
    friend class FMaterial;
    friend class MaterialInstance;

    template<size_t Size>
    void setParameterUntypedImpl(std::string_view name, const void* value);

    template<size_t Size>
    void setParameterUntypedImpl(std::string_view name, const void* value, size_t count);

    template<typename T>
    void setParameterImpl(std::string_view name, T const& value);

    template<typename T>
    void setParameterImpl(std::string_view name, const T* value, size_t count);

    void setParameterImpl(std::string_view name,
            FTexture const* texture, TextureSampler const& sampler);

    template<typename T>
    T getParameterImpl(std::string_view name) const;

    /**
     * 材质指针
     * 
     * 保持这些分组，它们在渲染循环中一起访问。
     */
    FMaterial const* mMaterial = nullptr;

    /**
     * 纹理参数结构
     * 
     * 存储纹理和采样器参数。
     */
    struct TextureParameter {
        FTexture const* texture;  // 纹理指针
        backend::SamplerParams params;  // 采样器参数
    };

    /**
     * UBO 数据
     * 
     * 可以是分配 ID（用于 UBO 批处理）或 UBO 句柄（用于独立 UBO）。
     */
    std::variant<BufferAllocator::AllocationId, backend::Handle<backend::HwBufferObject>> mUboData;
    
    /**
     * UBO 偏移量
     * 
     * 在共享 UBO 中的偏移量（字节）。
     */
    BufferAllocator::allocation_size_t mUboOffset = 0;
    
    /**
     * 纹理参数映射
     * 
     * 从绑定点到纹理参数的映射。
     */
    tsl::robin_map<backend::descriptor_binding_t, TextureParameter> mTextureParameters;
    
    /**
     * 描述符集
     * 
     * 用于绑定 uniform 和 sampler 的描述符集。
     */
    mutable DescriptorSet mDescriptorSet;
    
    /**
     * 统一缓冲区
     * 
     * 存储 uniform 数据。
     */
    UniformBuffer mUniforms;

    backend::PolygonOffset mPolygonOffset{};
    backend::StencilState mStencilState{};

    float mMaskThreshold = 0.0f;
    float mSpecularAntiAliasingVariance = 0.0f;
    float mSpecularAntiAliasingThreshold = 0.0f;

    backend::CullingMode mCulling : 2;
    backend::CullingMode mShadowCulling : 2;
    backend::RasterState::DepthFunc mDepthFunc : 3;

    bool mColorWrite : 1;
    bool mDepthWrite : 1;
    bool mHasScissor : 1;
    bool mIsDoubleSided : 1;
    bool mIsDefaultInstance : 1;
    const bool mUseUboBatching : 1;
    TransparencyMode mTransparencyMode : 2;

    uint64_t mMaterialSortingKey = 0;

    // Scissor rectangle is specified as: Left Bottom Width Height.
    backend::Viewport mScissorRect = { 0, 0,
            uint32_t(std::numeric_limits<int32_t>::max()),
            uint32_t(std::numeric_limits<int32_t>::max())
    };

    utils::CString mName;
    mutable utils::bitset64 mMissingSamplerDescriptors{};
    mutable std::once_flag mMissingSamplersFlag;
};

FILAMENT_DOWNCAST(MaterialInstance)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_MATERIALINSTANCE_H
