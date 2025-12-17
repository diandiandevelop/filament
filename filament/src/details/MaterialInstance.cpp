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

#include <filament/MaterialInstance.h>

#include "RenderPass.h"

#include "ds/DescriptorSetLayout.h"

#include "details/Engine.h"
#include "details/Material.h"
#include "details/MaterialInstance.h"
#include "details/Texture.h"
#include "details/Stream.h"

#include "private/filament/EngineEnums.h"

#include <filament/MaterialEnums.h>
#include <filament/TextureSampler.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/BitmaskEnum.h>
#include <utils/CString.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#include <math/scalar.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string_view>
#include <utility>

#include <stddef.h>

using namespace filament::math;
using namespace utils;

namespace filament {

using namespace backend;

// MaterialInstance 构造函数：创建材质实例
// engine: Filament 引擎引用
// material: 关联的 Material（材质定义）
// name: 实例名称（可选，如果为 nullptr 则使用 Material 的名称）
FMaterialInstance::FMaterialInstance(FEngine& engine, FMaterial const* material, const char* name) noexcept
        : mMaterial(material),
          // 创建描述符堆，使用 Material 的描述符堆布局
          mDescriptorSet("MaterialInstance", material->getDescriptorSetLayout()),
          mCulling(CullingMode::BACK),
          mShadowCulling(CullingMode::BACK),
          mDepthFunc(RasterState::DepthFunc::LE),
          mColorWrite(false),
          mDepthWrite(false),
          mHasScissor(false),
          mIsDoubleSided(false),
          mIsDefaultInstance(false),
          // 是否使用 UBO 批处理（从 Material 继承）
          mUseUboBatching(material->useUboBatching()),
          mTransparencyMode(TransparencyMode::DEFAULT),
          mName(name ? CString(name) : material->getName()) {
    FEngine::DriverApi& driver = engine.getDriverApi();

    // 即使材质没有任何参数，我们也分配一个小的 UBO（最小 16 字节）
    // 因为 per-material 描述符堆布局期望有一个 UBO
    size_t const uboSize = std::max(size_t(16), material->getUniformInterfaceBlock().getSize());
    mUniforms = UniformBuffer(uboSize);

    // 根据是否使用 UBO 批处理选择不同的 UBO 分配策略
    if (mUseUboBatching) {
        // UBO 批处理模式：不立即分配，由 UboManager 统一管理
        mUboData = BufferAllocator::UNALLOCATED;
        engine.getUboManager()->manageMaterialInstance(this);
    } else {
        // 独立 UBO 模式：立即创建独立的 BufferObject
        mUboData = driver.createBufferObject(mUniforms.getSize(), BufferObjectBinding::UNIFORM,
                BufferUsage::STATIC, ImmutableCString{ material->getName().c_str_safe() });
        // 设置 UBO 到描述符堆，总是使用 descriptor 0
        mDescriptorSet.setBuffer(material->getDescriptorSetLayout(),
                0, std::get<Handle<HwBufferObject>>(mUboData), 0, mUniforms.getSize());
    }

    // 从 Material 的 RasterState 继承渲染状态
    const RasterState& rasterState = material->getRasterState();
    // 注意：目前只有 MaterialInstance 有 stencil state，但未来应该可以直接在 Material 上设置
    // TODO: 这里应该从 Material 继承 stencil state
    // mStencilState = material->getStencilState();

    // 我们继承已解析的 culling mode，而不是 builder 设置的 culling mode
    // 这样可以保持 double-sidedness 自动禁用 culling 的特性
    mCulling = rasterState.culling;
    mShadowCulling = rasterState.culling;
    mColorWrite = rasterState.colorWrite;
    mDepthWrite = rasterState.depthWrite;
    mDepthFunc = rasterState.depthFunc;

    // 生成材质排序键（用于渲染排序，基于 Material ID 和 Instance ID）
    mMaterialSortingKey = RenderPass::makeMaterialSortingKey(
            material->getId(), material->generateMaterialInstanceId());

    // 如果材质使用 MASKED 混合模式，设置遮罩阈值
    if (material->getBlendingMode() == BlendingMode::MASKED) {
        setMaskThreshold(material->getMaskThreshold());
    }

    // 如果材质支持双面渲染，设置双面状态
    if (material->hasDoubleSidedCapability()) {
        setDoubleSided(material->isDoubleSided());
    }

    // 如果材质启用镜面反射抗锯齿，设置相关参数
    if (material->hasSpecularAntiAliasing()) {
        setSpecularAntiAliasingVariance(material->getSpecularAntiAliasingVariance());
        setSpecularAntiAliasingThreshold(material->getSpecularAntiAliasingThreshold());
    }

    // 设置透明度模式
    setTransparencyMode(material->getTransparencyMode());
}

/**
 * 复制构造函数：从另一个实例创建材质实例
 * 
 * @param engine 引擎引用
 * @param other 要复制的实例指针
 * @param name 新实例名称（可选）
 * 
 * 功能：
 * - 复制所有材质参数（uniform 和 sampler）
 * - 复制渲染状态（剔除、深度、模板等）
 * - 创建新的描述符堆（复制描述符堆布局）
 * - 分配新的 UBO（独立模式）或注册到 UBO 管理器（批处理模式）
 */
FMaterialInstance::FMaterialInstance(FEngine& engine,
        FMaterialInstance const* other, const char* name)
        : mMaterial(other->mMaterial),
          // 复制纹理参数映射（从绑定点到纹理和采样器参数）
          mTextureParameters(other->mTextureParameters),
          // 复制描述符堆（创建新的描述符堆，但使用相同的布局）
          mDescriptorSet(other->mDescriptorSet.duplicate(
                "MaterialInstance", mMaterial->getDescriptorSetLayout())),
          mPolygonOffset(other->mPolygonOffset),
          mStencilState(other->mStencilState),
          mMaskThreshold(other->mMaskThreshold),
          mSpecularAntiAliasingVariance(other->mSpecularAntiAliasingVariance),
          mSpecularAntiAliasingThreshold(other->mSpecularAntiAliasingThreshold),
          mCulling(other->mCulling),
          mShadowCulling(other->mShadowCulling),
          mDepthFunc(other->mDepthFunc),
          mColorWrite(other->mColorWrite),
          mDepthWrite(other->mDepthWrite),
          // 裁剪矩形不复制（新实例默认无裁剪）
          mHasScissor(false),
          mIsDoubleSided(other->mIsDoubleSided),
          // 新实例不是默认实例
          mIsDefaultInstance(false),
          // 继承 UBO 批处理模式
          mUseUboBatching(other->mUseUboBatching),
          mScissorRect(other->mScissorRect),
          mName(name ? CString(name) : other->mName) {
    FEngine::DriverApi& driver = engine.getDriverApi();
    FMaterial const* const material = other->getMaterial();

    // 复制 uniform 数据
    mUniforms.setUniforms(other->getUniformBuffer());

    // 根据 UBO 批处理模式选择不同的分配策略
    if (mUseUboBatching) {
        // UBO 批处理模式：注册到 UBO 管理器，稍后分配
        mUboData = BufferAllocator::UNALLOCATED;
        engine.getUboManager()->manageMaterialInstance(this);
    } else {
        // 独立 UBO 模式：立即创建动态 BufferObject（因为可能需要修改）
        mUboData = driver.createBufferObject(mUniforms.getSize(), BufferObjectBinding::UNIFORM,
                BufferUsage::DYNAMIC, ImmutableCString{ material->getName().c_str_safe() });
        // 设置 UBO 到描述符堆，总是使用 descriptor 0
        mDescriptorSet.setBuffer(material->getDescriptorSetLayout(),
                0, std::get<Handle<HwBufferObject>>(mUboData), 0, mUniforms.getSize());
    }

    // 应用材质特定的设置（这些方法会更新 uniform 参数）
    if (material->hasDoubleSidedCapability()) {
        setDoubleSided(mIsDoubleSided);
    }

    if (material->getBlendingMode() == BlendingMode::MASKED) {
        setMaskThreshold(mMaskThreshold);
    }

    if (material->hasSpecularAntiAliasing()) {
        setSpecularAntiAliasingThreshold(mSpecularAntiAliasingThreshold);
        setSpecularAntiAliasingVariance(mSpecularAntiAliasingVariance);
    }

    setTransparencyMode(material->getTransparencyMode());

    // 生成新的排序键（使用新的实例 ID）
    mMaterialSortingKey = RenderPass::makeMaterialSortingKey(
            material->getId(), material->generateMaterialInstanceId());

    // 如果原始描述符堆已经提交，副本也需要提交
    // 这确保纹理参数等已正确设置
    if (!mUseUboBatching && other->mDescriptorSet.getHandle()) {
        mDescriptorSet.commitSlow(mMaterial->getDescriptorSetLayout(), driver);
    }
}

/**
 * 复制材质实例（静态工厂方法）
 * 
 * @param other 要复制的实例指针
 * @param name 新实例名称（可选）
 * @return 新创建的材质实例指针
 * 
 * 功能：通过引擎创建材质实例的副本。
 */
FMaterialInstance* FMaterialInstance::duplicate(FMaterialInstance const* other,
        const char* name) noexcept {
    FMaterial const* const material = other->getMaterial();
    FEngine& engine = material->getEngine();
    return engine.createMaterialInstance(material, other, name);
}

/**
 * 析构函数
 * 
 * 注意：实际的资源清理在 terminate() 中完成。
 */
FMaterialInstance::~FMaterialInstance() noexcept = default;

/**
 * 终止材质实例
 * 
 * @param engine 引擎引用
 * 
 * 功能：
 * - 销毁描述符堆
 * - 从 UBO 管理器中注销（如果使用批处理）
 * - 销毁独立的 UBO（如果使用独立模式）
 */
void FMaterialInstance::terminate(FEngine& engine) {
    FEngine::DriverApi& driver = engine.getDriverApi();
    // 销毁描述符堆（释放所有绑定的资源）
    mDescriptorSet.terminate(driver);
    if (mUseUboBatching) {
        // 从 UBO 管理器中注销，释放分配的槽位
        engine.getUboManager()->unmanageMaterialInstance(this);
    }

    // 如果使用独立 UBO 模式，销毁 BufferObject
    auto* ubHandle = std::get_if<Handle<HwBufferObject>>(&mUboData);
    if (ubHandle){
        driver.destroyBufferObject(*ubHandle);
    }
}

/**
 * 提交材质实例（使用引擎）
 * 
 * @param engine 引擎引用
 * 
 * 功能：
 * - 对于非表面材质域（POST_PROCESS、COMPUTE），立即提交
 * - 对于表面材质域，延迟提交（在渲染时提交）
 */
void FMaterialInstance::commit(FEngine& engine) const {
    if (UTILS_LIKELY(mMaterial->getMaterialDomain() != MaterialDomain::SURFACE)) {
        commit(engine.getDriverApi(), engine.getUboManager());
    }
}

// 提交 MaterialInstance 的参数到 GPU
// driver: 驱动 API 引用
// uboManager: UBO 管理器（用于 UBO 批处理模式）
// 此方法将 Uniform Buffer 和纹理参数更新到 GPU，并提交 DescriptorSet
void FMaterialInstance::commit(FEngine::DriverApi& driver, UboManager* uboManager) const {
    // 1. 更新 Uniform Buffer（如果已修改）
    if (mUniforms.isDirty()) {
        mUniforms.clean();  // 清除脏标记
        if (isUsingUboBatching()) {
            // UBO 批处理模式：更新批处理 Buffer 中的槽位
            if (!BufferAllocator::isValid(getAllocationId())) {
                // 分配尚未发生，返回（等待分配完成）
                return;
            }
            // 更新批处理 Buffer 中的指定槽位
            uboManager->updateSlot(driver, getAllocationId(), mUniforms.toBufferDescriptor(driver));
        }
        else {
            // 独立 UBO 模式：更新独立的 BufferObject
            auto* ubHandle = std::get_if<Handle<HwBufferObject>>(&mUboData);
            assert_invariant(ubHandle != nullptr);
            driver.updateBufferObject(*ubHandle, mUniforms.toBufferDescriptor(driver), 0);
        }
    }
    
    // 2. 更新纹理参数（如果有）
    if (!mTextureParameters.empty()) {
        for (auto const& [binding, p]: mTextureParameters) {
            assert_invariant(p.texture);
            // TODO: 找到更高效的方法（isValid() 是哈希表查找）
            FEngine const& engine = mMaterial->getEngine();
            // 验证纹理仍然有效
            FILAMENT_CHECK_PRECONDITION(engine.isValid(p.texture))
                    << "Invalid texture still bound to MaterialInstance: '" << getName() << "'\n";
            // 获取纹理的采样句柄
            Handle<HwTexture> const handle = p.texture->getHwHandleForSampling();
            assert_invariant(handle);
            // 设置采样器到描述符堆
            mDescriptorSet.setSampler(mMaterial->getDescriptorSetLayout(),
                binding, handle, p.params);
        }
    }

    // 3. 修复缺失的采样器（为未设置的采样器设置占位符纹理）
    // TODO: 最终应该在 RELEASE 构建中移除此检查
    fixMissingSamplers();

    // 4. 如果使用 UBO 批处理但分配尚未完成，返回
    if (isUsingUboBatching() && !BufferAllocator::isValid(getAllocationId())) {
        return;
    }

    // 5. 提交描述符堆（例如当纹理更新时，或首次提交时）
    mDescriptorSet.commit(mMaterial->getDescriptorSetLayout(), driver);
}

// ------------------------------------------------------------------------------------------------

/**
 * 设置纹理参数（内部方法）
 * 
 * @param name 参数名称
 * @param texture 纹理句柄（硬件句柄）
 * @param params 采样器参数
 * 
 * 功能：直接设置纹理和采样器到描述符堆，用于内部调用。
 */
void FMaterialInstance::setParameter(std::string_view const name,
        Handle<HwTexture> texture, SamplerParams const params) {
    // 获取采样器的绑定索引
    auto const binding = mMaterial->getSamplerBinding(name);

    // 设置采样器到描述符堆
    mDescriptorSet.setSampler(mMaterial->getDescriptorSetLayout(),
        binding, texture, params);
}

/**
 * 设置纹理参数（实现方法）
 * 
 * @param name 参数名称
 * @param texture 纹理指针
 * @param sampler 纹理采样器
 * 
 * 功能：
 * - 验证纹理与描述符类型的兼容性
 * - 检查深度纹理的过滤模式（深度纹理只能在比较模式下使用线性过滤）
 * - 处理可变纹理句柄（延迟绑定）和固定纹理句柄（立即绑定）
 */
void FMaterialInstance::setParameterImpl(std::string_view const name,
        FTexture const* texture, TextureSampler const& sampler) {

#ifndef NDEBUG
    // 根据 GLES3.x 规范，深度纹理不能在比较模式之外使用过滤
    // Per GLES3.x specification, depth texture can't be filtered unless in compare mode.
    if (texture && isDepthFormat(texture->getFormat())) {
        if (sampler.getCompareMode() == SamplerCompareMode::NONE) {
            SamplerMinFilter const minFilter = sampler.getMinFilter();
            SamplerMagFilter const magFilter = sampler.getMagFilter();
            if (magFilter == SamplerMagFilter::LINEAR ||
                minFilter == SamplerMinFilter::LINEAR ||
                minFilter == SamplerMinFilter::LINEAR_MIPMAP_LINEAR ||
                minFilter == SamplerMinFilter::LINEAR_MIPMAP_NEAREST ||
                minFilter == SamplerMinFilter::NEAREST_MIPMAP_LINEAR) {
                PANIC_LOG("Depth textures can't be sampled with a linear filter "
                          "unless the comparison mode is set to COMPARE_TO_TEXTURE. "
                          "(material: \"%s\", parameter: \"%.*s\")",
                        getMaterial()->getName().c_str(), name.size(), name.data());
            }
        }
    }
#endif

    // 获取采样器的绑定索引
    auto const binding = mMaterial->getSamplerBinding(name);

    // 验证纹理与描述符类型的兼容性（仅在调试模式下检查）
    if (texture) {
        auto const& descriptorSetLayout = mMaterial->getDescriptorSetLayout();
        DescriptorType const descriptorType = descriptorSetLayout.getDescriptorType(binding);
        TextureType const textureType = texture->getTextureType();
        SamplerType const samplerType = texture->getTarget();
        auto const& featureFlags = mMaterial->getEngine().features.engine.debug;
        // 检查纹理类型、采样器类型和描述符类型是否兼容
        FILAMENT_FLAG_GUARDED_CHECK_PRECONDITION(
                DescriptorSet::isTextureCompatibleWithDescriptor(
                        textureType, samplerType, descriptorType),
                featureFlags.assert_material_instance_texture_descriptor_set_compatible)
                << "Texture format " << int(texture->getFormat())
                << " of type " << to_string(textureType)
                << " with sampler type " << to_string(samplerType)
                << " is not compatible with material \"" << getMaterial()->getName().c_str() << "\""
                << " parameter \"" << name << "\""
                << " of type " << to_string(descriptorType);
    }

    // 根据纹理句柄是否可变选择不同的绑定策略
    if (texture && texture->textureHandleCanMutate()) {
        // 可变纹理句柄：存储纹理指针和采样器参数，延迟绑定（在 commit() 时绑定）
        // 这允许纹理在提交之前更新其句柄（例如外部纹理、流式纹理）
        mTextureParameters[binding] = { texture, sampler.getSamplerParams() };
    } else {
        // 固定纹理句柄：立即绑定到描述符堆
        // 确保从 mTextureParameters 中删除此绑定，因为它不会在 commit() 时更新
        mTextureParameters.erase(binding);

        Handle<HwTexture> handle{};
        if (texture) {
            // 获取采样句柄（可能与渲染句柄不同）
            handle = texture->getHwHandleForSampling();
            assert_invariant(handle == texture->getHwHandle());
        }
        // 立即设置采样器到描述符堆
        mDescriptorSet.setSampler(mMaterial->getDescriptorSetLayout(),
            binding, handle, sampler.getSamplerParams());
    }
}

/**
 * 设置遮罩阈值
 * 
 * @param threshold 阈值（0.0-1.0），会被限制在 [0, 1] 范围内
 * 
 * 功能：用于 MASKED 混合模式，当片段 alpha < 阈值时丢弃片段。
 */
void FMaterialInstance::setMaskThreshold(float const threshold) noexcept {
    setParameter("_maskThreshold", saturate(threshold));
    mMaskThreshold = saturate(threshold);
}

float FMaterialInstance::getMaskThreshold() const noexcept {
    return mMaskThreshold;
}

/**
 * 设置镜面反射抗锯齿方差
 * 
 * @param variance 方差值（0.0-1.0），会被限制在 [0, 1] 范围内
 * 
 * 功能：用于几何镜面反射抗锯齿（GSAA），控制粗糙度的变化。
 */
void FMaterialInstance::setSpecularAntiAliasingVariance(float const variance) noexcept {
    setParameter("_specularAntiAliasingVariance", saturate(variance));
    mSpecularAntiAliasingVariance = saturate(variance);
}

float FMaterialInstance::getSpecularAntiAliasingVariance() const noexcept {
    return mSpecularAntiAliasingVariance;
}

/**
 * 设置镜面反射抗锯齿阈值
 * 
 * @param threshold 阈值，内部会计算平方并开方（优化计算）
 * 
 * 功能：用于几何镜面反射抗锯齿（GSAA），控制过滤的强度。
 */
void FMaterialInstance::setSpecularAntiAliasingThreshold(float const threshold) noexcept {
    setParameter("_specularAntiAliasingThreshold", saturate(threshold * threshold));
    mSpecularAntiAliasingThreshold = std::sqrt(saturate(threshold * threshold));
}

float FMaterialInstance::getSpecularAntiAliasingThreshold() const noexcept {
    return mSpecularAntiAliasingThreshold;
}

/**
 * 设置双面渲染
 * 
 * @param doubleSided 如果为 true，启用双面渲染（禁用背面剔除）
 * 
 * 功能：
 * - 检查材质是否支持双面渲染
 * - 如果启用双面，自动禁用剔除（设置为 NONE）
 * - 更新 uniform 参数 "_doubleSided"
 */
void FMaterialInstance::setDoubleSided(bool const doubleSided) noexcept {
    if (UTILS_UNLIKELY(!mMaterial->hasDoubleSidedCapability())) {
        LOG(WARNING) << "Parent material does not have double-sided capability.";
        return;
    }
    setParameter("_doubleSided", doubleSided);
    if (doubleSided) {
        // 双面渲染时自动禁用剔除
        setCullingMode(CullingMode::NONE);
    }
    mIsDoubleSided = doubleSided;
}

bool FMaterialInstance::isDoubleSided() const noexcept {
    return mIsDoubleSided;
}

/**
 * 设置透明度模式
 * 
 * @param mode 透明度模式（DEFAULT、TWO_PASSES_ONE_SIDE、TWO_PASSES_TWO_SIDES）
 * 
 * 功能：控制透明对象的渲染方式。
 */
void FMaterialInstance::setTransparencyMode(TransparencyMode const mode) noexcept {
    mTransparencyMode = mode;
}

/**
 * 设置深度剔除（深度测试）
 * 
 * @param enable 如果为 true，启用深度测试（使用 GE 比较）；如果为 false，禁用深度测试（使用 A 始终通过）
 * 
 * 功能：控制是否进行深度测试。禁用深度测试意味着所有片段都会通过深度测试。
 */
void FMaterialInstance::setDepthCulling(bool const enable) noexcept {
    mDepthFunc = enable ? RasterState::DepthFunc::GE : RasterState::DepthFunc::A;
}

/**
 * 深度剔除是否启用
 * 
 * @return 如果深度测试启用返回 true，否则返回 false
 * 
 * 实现：通过检查深度函数是否为 A（始终通过）来判断。
 */
bool FMaterialInstance::isDepthCullingEnabled() const noexcept {
    return mDepthFunc != RasterState::DepthFunc::A;
}

/**
 * 获取材质实例名称
 * 
 * @return 实例名称的 C 字符串，如果未设置则返回材质名称
 * 
 * 实现说明：
 * - 为了决定是否使用父材质名称作为后备，我们检查实例 CString 的空指针
 *   而不是调用 empty()。这允许实例用空字符串覆盖父材质的名称。
 */
const char* FMaterialInstance::getName() const noexcept {
    // To decide whether to use the parent material name as a fallback, we check for the nullness of
    // the instance's CString rather than calling empty(). This allows instances to override the
    // parent material's name with a blank string.
    if (mName.data() == nullptr) {
        return mMaterial->getName().c_str_safe();
    }
    return mName.c_str();
}

// ------------------------------------------------------------------------------------------------

// 绑定 MaterialInstance 的描述符堆到渲染管线
// driver: 驱动 API 引用
// variant: 当前使用的变体（用于判断是否为共享变体）
// 此方法在渲染时调用，将 MaterialInstance 的 DescriptorSet 绑定到指定的绑定点
void FMaterialInstance::use(FEngine::DriverApi& driver, Variant variant) const {
    // 如果描述符堆尚未创建（未提交），直接返回
    if (!mDescriptorSet.getHandle()) {
        return;
    }

    // 如果使用 UBO 批处理但分配尚未完成，返回
    if (isUsingUboBatching() && !BufferAllocator::isValid(getAllocationId())) {
        return;
    }

    // 检查是否有缺失的采样器参数（仅警告一次）
    if (UTILS_UNLIKELY(mMissingSamplerDescriptors.any())) {
        std::call_once(mMissingSamplersFlag, [this] {
            auto const& list = mMaterial->getSamplerInterfaceBlock().getSamplerInfoList();
            LOG(WARNING) << "sampler parameters not set in MaterialInstance \""
                         << mName.c_str_safe() << "\" or Material \""
                         << mMaterial->getName().c_str_safe() << "\":";
            // 遍历所有缺失的采样器，输出警告信息
            mMissingSamplerDescriptors.forEachSetBit([&list](descriptor_binding_t binding) {
                auto const pos = std::find_if(list.begin(), list.end(), [binding](const auto& item) {
                    return item.binding == binding;
                });
                // 安全检查，应该永远不会失败
                if (UTILS_LIKELY(pos != list.end())) {
                    LOG(WARNING) << "[" << +binding << "] " << pos->name.c_str();
                }
            });
        });
        mMissingSamplerDescriptors.clear();
    }

    // 检查此变体是否为共享变体（深度变体）
    // 如果是共享变体，FMaterial 负责绑定描述符堆（使用默认材质的 DescriptorSet）
    if (mMaterial->useShared(driver, variant)) {
        return;
    }

    // 绑定 MaterialInstance 的描述符堆到 PER_MATERIAL 绑定点
    // 如果使用 UBO 批处理，mUboOffset 是动态偏移；否则为 0
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_MATERIAL,
            { { mUboOffset }, driver });
}

/**
 * 分配 UBO 分配槽位（由 UBO 管理器调用）
 * 
 * @param ubHandle 共享 UBO 句柄
 * @param id 分配 ID
 * @param offset 在共享 UBO 中的偏移量（字节）
 * 
 * 功能：
 * - 由 UboManager 调用，为材质实例分配 UBO 批处理槽位
 * - 设置描述符堆的 UBO 绑定
 * - 偏移量存储在 mUboOffset 中，用于动态偏移绑定
 */
void FMaterialInstance::assignUboAllocation(
        const Handle<HwBufferObject>& ubHandle,
        BufferAllocator::AllocationId id,
        BufferAllocator::allocation_size_t offset) {
    assert_invariant(isUsingUboBatching());

    mUboData = id;
    mUboOffset = offset;
    if (BufferAllocator::isValid(id)) {
        // 在绑定时使用动态偏移，所以这里的偏移量始终为零
        // Use dynamic offset during binding, so the offset here is always zero.
        mDescriptorSet.setBuffer(mMaterial->getDescriptorSetLayout(), 0, ubHandle, 0,
                mUniforms.getSize());
    }
}

/**
 * 获取分配 ID
 * 
 * @return 分配 ID，如果未分配返回 UNALLOCATED
 * 
 * 功能：用于 UBO 批处理模式，查询是否已分配槽位。
 */
BufferAllocator::AllocationId FMaterialInstance::getAllocationId() const noexcept {
    auto const* allocationId = std::get_if<BufferAllocator::AllocationId>(&mUboData);
    return allocationId ? *allocationId : BufferAllocator::UNALLOCATED;
}

/**
 * 修复缺失的采样器
 * 
 * 功能：
 * - 检查所有声明的采样器参数是否已设置（Vulkan 和 Metal 要求所有采样器必须设置）
 * - 如果采样器未设置，使用占位符纹理（零纹理、虚拟立方体贴图等）填充
 * - 记录缺失的采样器状态（用于 use() 时的警告）
 * 
 * 实现：
 * - 通过位集运算找出缺失的采样器描述符
 * - 仅为 FLOAT 格式的采样器设置占位符（INT/UINT 暂不支持）
 * - 占位符选择：2D -> 零纹理，2D_ARRAY -> 零纹理数组，CUBEMAP -> 虚拟立方体贴图
 */
void FMaterialInstance::fixMissingSamplers() const {
    // 这里我们检查所有声明的采样器参数是否已设置，这由 Vulkan 和 Metal 要求
    // GL 更宽松。如果采样器参数未设置，我们将在系统日志中为每个 MaterialInstance 记录一次警告
    // 并补入虚拟纹理。
    // Here we check that all declared sampler parameters are set, this is required by
    // Vulkan and Metal; GL is more permissive. If a sampler parameter is not set, we will
    // log a warning once per MaterialInstance in the system log and patch-in a dummy
    // texture.
    auto const& layout = mMaterial->getDescriptorSetLayout();
    // 获取所有采样器描述符的位集
    auto const samplersDescriptors = layout.getSamplerDescriptors();
    // 获取已设置的有效描述符位集
    auto const validDescriptors = mDescriptorSet.getValidDescriptors();
    // 通过 XOR 运算找出缺失的采样器描述符
    auto const missingSamplerDescriptors =
            (validDescriptors & samplersDescriptors) ^ samplersDescriptors;

    // 始终在 commit() 时记录缺失采样器的状态（用于 use() 时的警告）
    // always record the missing samplers state at commit() time
    mMissingSamplerDescriptors = missingSamplerDescriptors;

    if (UTILS_UNLIKELY(missingSamplerDescriptors.any())) {
        // 需要设置缺失的采样器
        // here we need to set the samplers that are missing
        auto const& list = mMaterial->getSamplerInterfaceBlock().getSamplerInfoList();
        // 遍历所有缺失的采样器描述符
        missingSamplerDescriptors.forEachSetBit([this, &list](descriptor_binding_t binding) {
            // 在采样器信息列表中查找对应的绑定
            auto const pos = std::find_if(list.begin(), list.end(), [binding](const auto& item) {
                return item.binding == binding;
            });
            // 安全检查，应该永远不会失败
            // just safety-check, should never fail
            if (UTILS_LIKELY(pos != list.end())) {
                FEngine const& engine = mMaterial->getEngine();
                filament::DescriptorSetLayout const& layout = mMaterial->getDescriptorSetLayout();

                // 目前只处理 FLOAT 格式的缺失采样器
                if (pos->format == SamplerFormat::FLOAT) {
                    // TODO: we only handle missing samplers that are FLOAT
                    switch (pos->type) {
                        case SamplerType::SAMPLER_2D:
                            // 2D 采样器使用零纹理占位符
                            mDescriptorSet.setSampler(layout,
                                    binding, engine.getZeroTexture(), {});
                            break;
                        case SamplerType::SAMPLER_2D_ARRAY:
                            // 2D 数组采样器使用零纹理数组占位符
                            mDescriptorSet.setSampler(layout,
                                    binding, engine.getZeroTextureArray(), {});
                            break;
                        case SamplerType::SAMPLER_CUBEMAP:
                            // 立方体贴图采样器使用虚拟立方体贴图占位符
                            mDescriptorSet.setSampler(layout,
                                    binding, engine.getDummyCubemap()->getHwHandle(), {});
                            break;
                        default:
                            // 目前无法修复其他采样器类型
                            // we're currently not able to fix-up other sampler types
                            break;
                    }
                }
            }
        });
    }
}

} // namespace filament
