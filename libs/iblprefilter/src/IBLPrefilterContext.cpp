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

#include "filament-iblprefilter/IBLPrefilterContext.h"

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialEnums.h>
#include <filament/RenderTarget.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <backend/DriverEnums.h>

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/EntityManager.h>
#include <utils/Panic.h>

#include <math/scalar.h>
#include <math/vec4.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <stddef.h>
#include <stdint.h>

#include "generated/resources/iblprefilter_materials.h"

namespace {

using namespace filament::math;
using namespace filament;

/**
 * 全屏三角形顶点数据
 * 
 * 使用一个覆盖整个屏幕的大三角形，通过裁剪产生两个三角形。
 * 这种方法比使用两个三角形更高效。
 */
constexpr float4 sFullScreenTriangleVertices[3] = {
    { -1.0f, -1.0f, 1.0f, 1.0f },  // 左下角
    { 3.0f,  -1.0f, 1.0f, 1.0f },  // 右下角（超出屏幕）
    { -1.0f, 3.0f,  1.0f, 1.0f }   // 左上角（超出屏幕）
};

/**
 * 全屏三角形索引数据
 */
constexpr uint16_t sFullScreenTriangleIndices[3] = { 0, 1, 2 };

/**
 * 将LOD级别转换为感知粗糙度实现
 * 
 * 这是感知粗糙度到LOD映射的逆函数。
 * LOD到感知粗糙度的映射是对 log2(perceptualRoughness)+iblMaxMipLevel 的二次拟合，
 * 当iblMaxMipLevel为4时。
 * 经验发现，这个映射对于256大小的立方体贴图使用5个级别效果很好，
 * 同时也适用于其他iblMaxMipLevel值。
 * 
 * @param lod LOD级别
 * @return 感知粗糙度 [0, 1]
 */
static float lodToPerceptualRoughness(float lod) noexcept {
    // Inverse perceptualRoughness-to-LOD mapping:
    // The LOD-to-perceptualRoughness mapping is a quadratic fit for
    // log2(perceptualRoughness)+iblMaxMipLevel when iblMaxMipLevel is 4.
    // We found empirically that this mapping works very well for a 256 cubemap with 5 levels used,
    // but also scales well for other iblMaxMipLevel values.
    const float a = 2.0f;
    const float b = -1.0f;
    // 求解二次方程：a*lod + b*lod^2 = perceptualRoughness
    return (lod != 0)
           ? saturate((std::sqrt(a * a + 4.0f * b * lod) - a) / (2.0f * b))
           : 0.0f;
}

/**
 * 计算以4为底的对数实现
 * 
 * log4(x) = log2(x) / 2
 * 
 * @param x 输入值
 * @return log4(x)
 */
template<typename T>
constexpr T log4(T x) {
    return std::log2(x) * T(0.5);
}

/**
 * 清理材质实例实现
 * 
 * 从可渲染对象中移除材质实例并销毁它。
 * 
 * @param mi 材质实例指针（可以为null）
 * @param engine Filament引擎
 * @param rcm 可渲染管理器
 * @param ci 可渲染实例
 */
static void cleanupMaterialInstance(MaterialInstance const* mi, Engine& engine, RenderableManager& rcm,
    RenderableManager::Instance const& ci) {
    // mi is already nullptr, there is no need to clean up again.
    // mi已经是nullptr，不需要再次清理
    if (mi == nullptr)
        return;

    rcm.clearMaterialInstanceAt(ci, 0);  // 从可渲染对象中移除材质实例
    engine.destroy(mi);                  // 销毁材质实例
}

constexpr Texture::Usage COMMON_USAGE =
        Texture::Usage::COLOR_ATTACHMENT | Texture::Usage::SAMPLEABLE;
constexpr Texture::Usage MIPMAP_USAGE = Texture::Usage::GEN_MIPMAPPABLE;

} // namespace


/**
 * IBLPrefilterContext构造函数实现
 * 
 * 初始化IBL预过滤上下文，创建所有过滤器共用的GPU资源。
 * 
 * 执行步骤：
 * 1. 创建实体（相机实体和全屏四边形实体）
 * 2. 创建材质（积分材质和辐照度积分材质）
 * 3. 创建顶点缓冲区和索引缓冲区（全屏三角形）
 * 4. 创建可渲染对象（全屏四边形）
 * 5. 创建视图、场景、渲染器和相机
 * 6. 配置视图（禁用不需要的功能以提高性能）
 * 
 * @param engine Filament引擎引用
 */
IBLPrefilterContext::IBLPrefilterContext(Engine& engine)
        : mEngine(engine) {
    utils::EntityManager& em = utils::EntityManager::get();
    mCameraEntity = em.create();              // 创建相机实体
    mFullScreenQuadEntity = em.create();      // 创建全屏四边形实体

    // 创建积分材质（用于镜面反射过滤）
    mIntegrationMaterial = Material::Builder().package(
            IBLPREFILTER_MATERIALS_IBLPREFILTER_DATA,
            IBLPREFILTER_MATERIALS_IBLPREFILTER_SIZE).build(engine);

    // 创建辐照度积分材质（用于漫反射过滤）
    mIrradianceIntegrationMaterial = Material::Builder().package(
            IBLPREFILTER_MATERIALS_IBLPREFILTER_DATA,
            IBLPREFILTER_MATERIALS_IBLPREFILTER_SIZE)
                    .constant("irradiance", true)  // 设置辐照度常量
                    .build(engine);

    // 创建顶点缓冲区（全屏三角形）
    mVertexBuffer = VertexBuffer::Builder()
            .vertexCount(3)
            .bufferCount(1)
            .attribute(POSITION, 0,
                    VertexBuffer::AttributeType::FLOAT4, 0)
            .build(engine);

    // 创建索引缓冲区
    mIndexBuffer = IndexBuffer::Builder()
            .indexCount(3)
            .bufferType(IndexBuffer::IndexType::USHORT)
            .build(engine);

    // 设置顶点数据
    mVertexBuffer->setBufferAt(engine, 0,
            { sFullScreenTriangleVertices, sizeof(sFullScreenTriangleVertices) });

    // 设置索引数据
    mIndexBuffer->setBuffer(engine,
            { sFullScreenTriangleIndices, sizeof(sFullScreenTriangleIndices) });

    // 创建可渲染对象（全屏四边形）
    RenderableManager::Builder(1)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, mVertexBuffer, mIndexBuffer)
            .culling(false)           // 禁用背面剔除
            .castShadows(false)       // 不投射阴影
            .receiveShadows(false)    // 不接收阴影
            .build(engine, mFullScreenQuadEntity);

    // 创建视图、场景、渲染器和相机
    mView = engine.createView();
    mScene = engine.createScene();
    mRenderer = engine.createRenderer();
    mCamera = engine.createCamera(mCameraEntity);

    // 将全屏四边形添加到场景
    mScene->addEntity(mFullScreenQuadEntity);

    // 配置视图（禁用不需要的功能以提高性能）
    View* const view = mView;
    view->setCamera(mCamera);
    view->setScene(mScene);
    view->setScreenSpaceRefractionEnabled(false);  // 禁用屏幕空间折射
    view->setShadowingEnabled(false);              // 禁用阴影
    view->setPostProcessingEnabled(false);         // 禁用后处理
    view->setFrustumCullingEnabled(false);         // 禁用视锥剔除
}

/**
 * IBLPrefilterContext析构函数实现
 * 
 * 销毁所有初始化期间创建的GPU资源。
 * 
 * 执行步骤：
 * 1. 销毁视图、场景、渲染器
 * 2. 销毁顶点缓冲区和索引缓冲区
 * 3. 销毁材质
 * 4. 销毁实体和相机组件
 */
IBLPrefilterContext::~IBLPrefilterContext() noexcept {
    utils::EntityManager& em = utils::EntityManager::get();
    auto& engine = mEngine;
    engine.destroy(mView);                        // 销毁视图
    engine.destroy(mScene);                       // 销毁场景
    engine.destroy(mRenderer);                    // 销毁渲染器
    engine.destroy(mVertexBuffer);                // 销毁顶点缓冲区
    engine.destroy(mIndexBuffer);                // 销毁索引缓冲区
    engine.destroy(mIntegrationMaterial);         // 销毁积分材质
    engine.destroy(mIrradianceIntegrationMaterial);  // 销毁辐照度积分材质
    engine.destroy(mFullScreenQuadEntity);        // 销毁全屏四边形实体
    engine.destroyCameraComponent(mCameraEntity); // 销毁相机组件
    em.destroy(mFullScreenQuadEntity);            // 销毁实体
}


/**
 * IBLPrefilterContext移动构造函数实现
 * 
 * 从另一个IBLPrefilterContext对象移动资源。
 * 
 * @param rhs 源对象（将被移动）
 */
IBLPrefilterContext::IBLPrefilterContext(IBLPrefilterContext&& rhs) noexcept
        : mEngine(rhs.mEngine) {
    this->operator=(std::move(rhs));
}

/**
 * IBLPrefilterContext移动赋值运算符实现
 * 
 * 从另一个IBLPrefilterContext对象移动资源。
 * 
 * @param rhs 源对象（将被移动）
 * @return 当前对象的引用
 */
IBLPrefilterContext& IBLPrefilterContext::operator=(IBLPrefilterContext&& rhs) noexcept {
    using std::swap;
    if (this != & rhs) {
        // 交换所有资源
        swap(mRenderer, rhs.mRenderer);
        swap(mScene, rhs.mScene);
        swap(mVertexBuffer, rhs.mVertexBuffer);
        swap(mIndexBuffer, rhs.mIndexBuffer);
        swap(mCamera, rhs.mCamera);
        swap(mFullScreenQuadEntity, rhs.mFullScreenQuadEntity);
        swap(mCameraEntity, rhs.mCameraEntity);
        swap(mView, rhs.mView);
        swap(mIntegrationMaterial, rhs.mIntegrationMaterial);
        swap(mIrradianceIntegrationMaterial, rhs.mIrradianceIntegrationMaterial);
    }
    return *this;
}

// ------------------------------------------------------------------------------------------------

/**
 * EquirectangularToCubemap构造函数实现（使用指定配置）
 * 
 * 创建等距圆柱投影到立方体贴图转换器。
 * 
 * 执行步骤：
 * 1. 保存上下文和配置
 * 2. 创建等距圆柱投影材质
 * 
 * @param context IBL预过滤上下文
 * @param config 配置参数
 */
IBLPrefilterContext::EquirectangularToCubemap::EquirectangularToCubemap(
        IBLPrefilterContext& context,
        Config const& config)
        : mContext(context), mConfig(config) {
    Engine& engine = mContext.mEngine;
    // 创建等距圆柱投影材质
    mEquirectMaterial = Material::Builder().package(
            IBLPREFILTER_MATERIALS_EQUIRECTTOCUBE_DATA,
            IBLPREFILTER_MATERIALS_EQUIRECTTOCUBE_SIZE).build(engine);
}

/**
 * EquirectangularToCubemap构造函数实现（使用默认配置）
 * 
 * 使用默认配置创建等距圆柱投影到立方体贴图转换器。
 * 
 * @param context IBL预过滤上下文
 */
IBLPrefilterContext::EquirectangularToCubemap::EquirectangularToCubemap(
        IBLPrefilterContext& context) : EquirectangularToCubemap(context, {}) {
}

/**
 * EquirectangularToCubemap析构函数实现
 * 
 * 销毁等距圆柱投影材质。
 */
IBLPrefilterContext::EquirectangularToCubemap::~EquirectangularToCubemap() noexcept {
    Engine& engine = mContext.mEngine;
    engine.destroy(mEquirectMaterial);
}

/**
 * EquirectangularToCubemap移动构造函数实现
 * 
 * @param rhs 源对象（将被移动）
 */
IBLPrefilterContext::EquirectangularToCubemap::EquirectangularToCubemap(
        EquirectangularToCubemap&& rhs) noexcept
        : mContext(rhs.mContext) {
    using std::swap;
    swap(mEquirectMaterial, rhs.mEquirectMaterial);
}

/**
 * EquirectangularToCubemap移动赋值运算符实现
 * 
 * @param rhs 源对象（将被移动）
 * @return 当前对象的引用
 */
IBLPrefilterContext::EquirectangularToCubemap&
IBLPrefilterContext::EquirectangularToCubemap::operator=(
        EquirectangularToCubemap&& rhs) noexcept {
    using std::swap;
    if (this != &rhs) {
        swap(mEquirectMaterial, rhs.mEquirectMaterial);
    }
    return *this;
}

/**
 * 将等距圆柱投影图像转换为立方体贴图实现
 * 
 * 执行步骤：
 * 1. 验证输入纹理的有效性
 * 2. 如果输出纹理为null，自动创建默认立方体贴图
 * 3. 设置材质参数（等距圆柱投影纹理、镜像选项）
 * 4. 生成Mipmap（用于下采样）
 * 5. 对每个面组（3个面）：
 *    - 设置渲染目标（3个面同时渲染）
 *    - 渲染到立方体贴图
 * 6. 清理资源并返回结果
 * 
 * @param equirect 等距圆柱投影纹理（输入）
 * @param outCube 输出立方体贴图（如果为null则自动创建）
 * @return 输出立方体贴图
 */
Texture* IBLPrefilterContext::EquirectangularToCubemap::operator()(
        Texture const* equirect, Texture* outCube) {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    using namespace backend;

    const TextureCubemapFace faces[2][3] = {
            { TextureCubemapFace::POSITIVE_X, TextureCubemapFace::POSITIVE_Y, TextureCubemapFace::POSITIVE_Z },
            { TextureCubemapFace::NEGATIVE_X, TextureCubemapFace::NEGATIVE_Y, TextureCubemapFace::NEGATIVE_Z }
    };

    Engine& engine = mContext.mEngine;
    View* const view = mContext.mView;
    Renderer* const renderer = mContext.mRenderer;
    MaterialInstance* const mi = mEquirectMaterial->createInstance();

    FILAMENT_CHECK_PRECONDITION(equirect != nullptr) << "equirect is null!";

    FILAMENT_CHECK_PRECONDITION(equirect->getTarget() == Texture::Sampler::SAMPLER_2D)
            << "equirect must be a 2D texture.";

    UTILS_UNUSED_IN_RELEASE
    const uint8_t maxLevelCount = std::max(1, std::ilogbf(float(equirect->getWidth())) + 1);

    FILAMENT_CHECK_PRECONDITION(equirect->getLevels() == maxLevelCount)
            << "equirect must have " << +maxLevelCount << " mipmap levels allocated.";

    if (outCube == nullptr) {
        outCube = Texture::Builder()
                .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                .format(Texture::InternalFormat::R11F_G11F_B10F)
                .usage(COMMON_USAGE | MIPMAP_USAGE)
                .width(256).height(256).levels(0xFF)
                .build(engine);
    }

    FILAMENT_CHECK_PRECONDITION(outCube->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
            << "outCube must be a Cubemap texture.";

    const uint32_t dim = outCube->getWidth();

    RenderableManager& rcm = engine.getRenderableManager();
    auto const ci = rcm.getInstance(mContext.mFullScreenQuadEntity);

    TextureSampler environmentSampler;
    environmentSampler.setMagFilter(SamplerMagFilter::LINEAR);
    environmentSampler.setMinFilter(SamplerMinFilter::LINEAR_MIPMAP_LINEAR);
    environmentSampler.setAnisotropy(16.0f); // maybe make this an option

    mi->setParameter("equirect", equirect, environmentSampler);

    // We need mipmaps because we're sampling down
    equirect->generateMipmaps(engine);

    view->setViewport({ 0, 0, dim, dim });

    RenderTarget::Builder builder;
    builder.texture(RenderTarget::AttachmentPoint::COLOR0, outCube)
           .texture(RenderTarget::AttachmentPoint::COLOR1, outCube)
           .texture(RenderTarget::AttachmentPoint::COLOR2, outCube);

    mi->setParameter("mirror", mConfig.mirror ? -1.0f : 1.0f);

    for (size_t i = 0; i < 2; i++) {
        // This is a workaround for internal bug b/419664914 to duplicate same material for each draw.
        // TODO: properly address the bug and remove this workaround.
#if defined(__EMSCRIPTEN__)
        MaterialInstance *const tempMi = MaterialInstance::duplicate(mi);
#else
        MaterialInstance *const tempMi = mi;
#endif
        rcm.setMaterialInstanceAt(ci, 0, tempMi);

        tempMi->setParameter("side", i == 0 ? 1.0f : -1.0f);
        tempMi->commit(engine);

        builder.face(RenderTarget::AttachmentPoint::COLOR0, faces[i][0])
               .face(RenderTarget::AttachmentPoint::COLOR1, faces[i][1])
               .face(RenderTarget::AttachmentPoint::COLOR2, faces[i][2]);

        RenderTarget* const rt = builder.build(engine);
        view->setRenderTarget(rt);
        renderer->renderStandaloneView(view);
        engine.destroy(rt);

#if defined(__EMSCRIPTEN__)
        cleanupMaterialInstance(tempMi, engine, rcm, ci);
#endif
    }

    rcm.clearMaterialInstanceAt(ci, 0);
    engine.destroy(mi);

    return outCube;
}

// ------------------------------------------------------------------------------------------------

/**
 * IrradianceFilter构造函数实现（使用指定配置）
 * 
 * 创建辐照度过滤器，预计算重要性采样核函数。
 * 
 * 执行步骤：
 * 1. 创建核函数材质（用于生成采样方向）
 * 2. 创建核函数纹理（存储采样方向和LOD）
 * 3. 使用GPU生成核函数纹理（重要性采样方向）
 * 
 * @param context IBL预过滤上下文
 * @param config 过滤器配置
 */
IBLPrefilterContext::IrradianceFilter::IrradianceFilter(IBLPrefilterContext& context,
        Config config)
        : mContext(context),
         mSampleCount(std::min(config.sampleCount, uint16_t(2048))) {  // 限制最大采样数为2048

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    using namespace backend;

    Engine& engine = mContext.mEngine;
    View* const view = mContext.mView;
    Renderer* const renderer = mContext.mRenderer;

    // 创建核函数材质（用于生成重要性采样方向）
    mKernelMaterial = Material::Builder().package(
            IBLPREFILTER_MATERIALS_GENERATEKERNEL_DATA,
            IBLPREFILTER_MATERIALS_GENERATEKERNEL_SIZE)
                    .constant("irradiance", true)  // 设置为辐照度模式
                    .build(engine);

    // { L.x, L.y, L.z, lod }
    // 核函数纹理：存储采样方向（L.x, L.y, L.z）和LOD
    mKernelTexture = Texture::Builder()
            .sampler(Texture::Sampler::SAMPLER_2D)
            .format(Texture::InternalFormat::RGBA16F)
            .usage(COMMON_USAGE)
            .width(1)
            .height(mSampleCount)  // 每个采样一行
            .build(engine);

    // 创建材质实例并设置参数
    MaterialInstance* const mi = mKernelMaterial->createInstance();
    mi->setParameter("size", uint2{ 1, mSampleCount });
    mi->setParameter("sampleCount", float(mSampleCount));
    mi->commit(engine);

    // 设置可渲染对象
    RenderableManager& rcm = engine.getRenderableManager();
    auto const ci = rcm.getInstance(mContext.mFullScreenQuadEntity);
    rcm.setMaterialInstanceAt(ci, 0, mi);

    // 创建渲染目标
    RenderTarget* const rt = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR0, mKernelTexture)
            .build(engine);

    // 设置视口并渲染核函数纹理
    view->setRenderTarget(rt);
    view->setViewport({ 0, 0, 1, mSampleCount });

    renderer->renderStandaloneView(view);

    // 清理资源
    cleanupMaterialInstance(mi, engine, rcm, ci);
    engine.destroy(rt);
}

/**
 * IrradianceFilter构造函数实现（使用默认配置）
 * 
 * 使用默认配置创建辐照度过滤器。
 * 
 * @param context IBL预过滤上下文
 */
UTILS_NOINLINE
IBLPrefilterContext::IrradianceFilter::IrradianceFilter(IBLPrefilterContext& context)
        : IrradianceFilter(context, {}) {
}

/**
 * IrradianceFilter析构函数实现
 * 
 * 销毁核函数纹理和材质。
 */
IBLPrefilterContext::IrradianceFilter::~IrradianceFilter() noexcept {
    Engine& engine = mContext.mEngine;
    engine.destroy(mKernelTexture);
    engine.destroy(mKernelMaterial);
}

/**
 * IrradianceFilter移动构造函数实现
 * 
 * @param rhs 源对象（将被移动）
 */
IBLPrefilterContext::IrradianceFilter::IrradianceFilter(
        IrradianceFilter&& rhs) noexcept
        : mContext(rhs.mContext) {
    this->operator=(std::move(rhs));
}

/**
 * IrradianceFilter移动赋值运算符实现
 * 
 * @param rhs 源对象（将被移动）
 * @return 当前对象的引用
 */
IBLPrefilterContext::IrradianceFilter& IBLPrefilterContext::IrradianceFilter::operator=(
        IrradianceFilter&& rhs) noexcept {
    using std::swap;
    if (this != & rhs) {
        swap(mKernelMaterial, rhs.mKernelMaterial);
        swap(mKernelTexture, rhs.mKernelTexture);
        mSampleCount = rhs.mSampleCount;
    }
    return *this;
}

/**
 * 生成辐照度立方体贴图实现
 * 
 * 执行步骤：
 * 1. 验证输入环境贴图的有效性
 * 2. 如果输出纹理为null，自动创建默认辐照度纹理
 * 3. 设置材质参数（环境贴图、核函数纹理、HDR压缩、LOD偏移）
 * 4. 如果需要，生成Mipmap
 * 5. 对每个面组（3个面）：
 *    - 设置渲染目标（3个面同时渲染）
 *    - 使用重要性采样计算漫反射辐照度
 *    - 渲染到辐照度立方体贴图
 * 6. 清理资源并返回结果
 * 
 * @param options 环境选项（HDR压缩、LOD偏移、Mipmap生成）
 * @param environmentCubemap 环境立方体贴图（输入）
 * @param outIrradianceTexture 输出辐照度纹理（如果为null则自动创建）
 * @return 输出辐照度纹理
 */
Texture* IBLPrefilterContext::IrradianceFilter::operator()(Options options,
        Texture const* environmentCubemap, Texture* outIrradianceTexture) {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    using namespace backend;

    FILAMENT_CHECK_PRECONDITION(environmentCubemap != nullptr) << "environmentCubemap is null!";

    FILAMENT_CHECK_PRECONDITION(
            environmentCubemap->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
            << "environmentCubemap must be a cubemap.";

    UTILS_UNUSED_IN_RELEASE
    const uint8_t maxLevelCount = uint8_t(std::log2(environmentCubemap->getWidth()) + 0.5f) + 1u;

    FILAMENT_CHECK_PRECONDITION(environmentCubemap->getLevels() == maxLevelCount)
            << "environmentCubemap must have " << +maxLevelCount << " mipmap levels allocated.";

    if (outIrradianceTexture == nullptr) {
        outIrradianceTexture =
                Texture::Builder()
                        .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                        .format(Texture::InternalFormat::R11F_G11F_B10F)
                        .usage(COMMON_USAGE |
                                (options.generateMipmap ? MIPMAP_USAGE : Texture::Usage::NONE))
                        .width(256)
                        .height(256)
                        .levels(0xff)
                        .build(mContext.mEngine);
    }

    FILAMENT_CHECK_PRECONDITION(
            outIrradianceTexture->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
            << "outReflectionsTexture must be a cubemap.";

    const TextureCubemapFace faces[2][3] = {
            { TextureCubemapFace::POSITIVE_X, TextureCubemapFace::POSITIVE_Y, TextureCubemapFace::POSITIVE_Z },
            { TextureCubemapFace::NEGATIVE_X, TextureCubemapFace::NEGATIVE_Y, TextureCubemapFace::NEGATIVE_Z }
    };

    Engine& engine = mContext.mEngine;
    View* const view = mContext.mView;
    Renderer* const renderer = mContext.mRenderer;
    MaterialInstance* const mi = mContext.mIrradianceIntegrationMaterial->createInstance();

    RenderableManager& rcm = engine.getRenderableManager();
    auto const ci = rcm.getInstance(mContext.mFullScreenQuadEntity);

    const uint32_t sampleCount = mSampleCount;
    const float linear = options.hdrLinear;
    const float compress = options.hdrMax;
    const uint32_t dim = outIrradianceTexture->getWidth();
    const float omegaP = (4.0f * f::PI) / float(6 * dim * dim);

    TextureSampler environmentSampler;
    environmentSampler.setMagFilter(SamplerMagFilter::LINEAR);
    environmentSampler.setMinFilter(SamplerMinFilter::LINEAR_MIPMAP_LINEAR);

    mi->setParameter("environment", environmentCubemap, environmentSampler);
    mi->setParameter("kernel", mKernelTexture, TextureSampler{ SamplerMagFilter::NEAREST });
    mi->setParameter("compress", float2{ linear, compress });
    mi->setParameter("lodOffset", options.lodOffset - log4(omegaP));
    mi->setParameter("sampleCount", sampleCount);

    if (options.generateMipmap) {
        // We need mipmaps for prefiltering
        environmentCubemap->generateMipmaps(engine);
    }

    RenderTarget::Builder builder;
    builder.texture(RenderTarget::AttachmentPoint::COLOR0, outIrradianceTexture)
           .texture(RenderTarget::AttachmentPoint::COLOR1, outIrradianceTexture)
           .texture(RenderTarget::AttachmentPoint::COLOR2, outIrradianceTexture);


    view->setViewport({ 0, 0, dim, dim });

    for (size_t i = 0; i < 2; i++) {
        // This is a workaround for internal bug b/419664914 to duplicate same material for each draw.
        // TODO: properly address the bug and remove this workaround.
#if defined(__EMSCRIPTEN__)
        MaterialInstance *const tempMi = MaterialInstance::duplicate(mi);
#else
        MaterialInstance *const tempMi = mi;
#endif
        rcm.setMaterialInstanceAt(ci, 0, tempMi);

        tempMi->setParameter("side", i == 0 ? 1.0f : -1.0f);
        tempMi->commit(engine);

        builder.face(RenderTarget::AttachmentPoint::COLOR0, faces[i][0])
               .face(RenderTarget::AttachmentPoint::COLOR1, faces[i][1])
               .face(RenderTarget::AttachmentPoint::COLOR2, faces[i][2]);

        RenderTarget* const rt = builder.build(engine);
        view->setRenderTarget(rt);
        renderer->renderStandaloneView(view);
        engine.destroy(rt);

#if defined(__EMSCRIPTEN__)
        cleanupMaterialInstance(tempMi, engine, rcm, ci);
#endif
    }

    rcm.clearMaterialInstanceAt(ci, 0);
    engine.destroy(mi);

    return outIrradianceTexture;
}

/**
 * 生成辐照度立方体贴图实现（使用默认选项）
 * 
 * 使用默认选项调用完整版本的operator()。
 * 
 * @param environmentCubemap 环境立方体贴图（输入）
 * @param outIrradianceTexture 输出辐照度纹理（如果为null则自动创建）
 * @return 输出辐照度纹理
 */
UTILS_NOINLINE
Texture* IBLPrefilterContext::IrradianceFilter::operator()(
        Texture const* environmentCubemap, Texture* outIrradianceTexture) {
    return operator()({}, environmentCubemap, outIrradianceTexture);
}

// ------------------------------------------------------------------------------------------------

/**
 * SpecularFilter构造函数实现（使用指定配置）
 * 
 * 创建镜面反射过滤器，预计算重要性采样核函数。
 * 
 * 执行步骤：
 * 1. 创建核函数材质（用于生成采样方向）
 * 2. 创建核函数纹理（存储每个粗糙度级别的采样方向）
 * 3. 计算每个粗糙度级别的粗糙度值
 * 4. 使用GPU生成核函数纹理（重要性采样方向）
 * 
 * @param context IBL预过滤上下文
 * @param config 过滤器配置
 */
IBLPrefilterContext::SpecularFilter::SpecularFilter(IBLPrefilterContext& context, Config config)
    : mContext(context) {

    /**
     * Lambda函数：将LOD级别转换为感知粗糙度
     * 
     * 这是感知粗糙度到LOD映射的逆函数。
     * LOD到感知粗糙度的映射是对 log2(perceptualRoughness)+iblMaxMipLevel 的二次拟合，
     * 当iblMaxMipLevel为4时。
     * 经验发现，这个映射对于256大小的立方体贴图使用5个级别效果很好，
     * 同时也适用于其他iblMaxMipLevel值。
     */
    auto lodToPerceptualRoughness = [](const float lod) -> float {
        // Inverse perceptualRoughness-to-LOD mapping:
        // The LOD-to-perceptualRoughness mapping is a quadratic fit for
        // log2(perceptualRoughness)+iblMaxMipLevel when iblMaxMipLevel is 4.
        // We found empirically that this mapping works very well for a 256 cubemap with 5 levels used,
        // but also scales well for other iblMaxMipLevel values.
        const float a = 2.0f;
        const float b = -1.0f;
        return (lod != 0.0f) ? saturate((sqrt(a * a + 4.0f * b * lod) - a) / (2.0f * b)) : 0.0f;
    };

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    using namespace backend;

    Engine& engine = mContext.mEngine;
    View* const view = mContext.mView;
    Renderer* const renderer = mContext.mRenderer;

    // 限制采样数和级别数
    mSampleCount = std::min(config.sampleCount, uint16_t(2048));  // 最大2048个采样
    mLevelCount = std::max(config.levelCount, uint8_t(1u));       // 至少1个级别

    // 创建核函数材质（用于生成重要性采样方向）
    mKernelMaterial = Material::Builder().package(
            IBLPREFILTER_MATERIALS_GENERATEKERNEL_DATA,
            IBLPREFILTER_MATERIALS_GENERATEKERNEL_SIZE).build(engine);

    // { L.x, L.y, L.z, lod }
    // 核函数纹理：存储每个粗糙度级别的采样方向（L.x, L.y, L.z）和LOD
    // 宽度 = 粗糙度级别数，高度 = 采样数
    mKernelTexture = Texture::Builder()
            .sampler(Texture::Sampler::SAMPLER_2D)
            .format(Texture::InternalFormat::RGBA16F)
            .usage(COMMON_USAGE)
            .width(mLevelCount)
            .height(mSampleCount)
            .build(engine);

    // 计算每个粗糙度级别的粗糙度值
    float roughnessArray[16] = {};
    for (size_t i = 0, c = mLevelCount; i < c; i++) {
        // 将LOD级别转换为感知粗糙度
        float const perceptualRoughness = lodToPerceptualRoughness(
                saturate(float(i) * (1.0f / (float(mLevelCount) - 1.0f))));
        // 感知粗糙度转换为线性粗糙度（平方）
        float const roughness = perceptualRoughness * perceptualRoughness;
        roughnessArray[i] = roughness;
    }

    // 创建材质实例并设置参数
    MaterialInstance* const mi = mKernelMaterial->createInstance();
    mi->setParameter("size", uint2{ mLevelCount, mSampleCount });
    mi->setParameter("sampleCount", float(mSampleCount));
    mi->setParameter("roughness", roughnessArray, 16);  // 传递粗糙度数组
    mi->commit(engine);

    // 设置可渲染对象
    RenderableManager& rcm = engine.getRenderableManager();
    auto const ci = rcm.getInstance(mContext.mFullScreenQuadEntity);
    rcm.setMaterialInstanceAt(ci, 0, mi);

    // 创建渲染目标
    RenderTarget* const rt = RenderTarget::Builder()
            .texture(RenderTarget::AttachmentPoint::COLOR0, mKernelTexture)
            .build(engine);

    // 设置视口并渲染核函数纹理
    view->setRenderTarget(rt);
    view->setViewport({ 0, 0, mLevelCount, mSampleCount });

    renderer->renderStandaloneView(view);

    // 清理资源
    cleanupMaterialInstance(mi, engine, rcm, ci);
    engine.destroy(rt);
}

/**
 * SpecularFilter构造函数实现（使用默认配置）
 * 
 * 使用默认配置创建镜面反射过滤器。
 * 
 * @param context IBL预过滤上下文
 */
UTILS_NOINLINE
IBLPrefilterContext::SpecularFilter::SpecularFilter(IBLPrefilterContext& context)
    : SpecularFilter(context, {}) {
}

/**
 * SpecularFilter析构函数实现
 * 
 * 销毁核函数纹理和材质。
 */
IBLPrefilterContext::SpecularFilter::~SpecularFilter() noexcept {
    Engine& engine = mContext.mEngine;
    engine.destroy(mKernelTexture);
    engine.destroy(mKernelMaterial);
}

/**
 * SpecularFilter移动构造函数实现
 * 
 * @param rhs 源对象（将被移动）
 */
IBLPrefilterContext::SpecularFilter::SpecularFilter(SpecularFilter&& rhs) noexcept
        : mContext(rhs.mContext) {
    this->operator=(std::move(rhs));
}

/**
 * SpecularFilter移动赋值运算符实现
 * 
 * @param rhs 源对象（将被移动）
 * @return 当前对象的引用
 */
IBLPrefilterContext::SpecularFilter&
IBLPrefilterContext::SpecularFilter::operator=(SpecularFilter&& rhs) noexcept {
    using std::swap;
    if (this != & rhs) {
        swap(mKernelMaterial, rhs.mKernelMaterial);
        swap(mKernelTexture, rhs.mKernelTexture);
        mSampleCount = rhs.mSampleCount;
        mLevelCount = rhs.mLevelCount;
    }
    return *this;
}

/**
 * 生成预过滤立方体贴图实现（使用默认选项）
 * 
 * 使用默认选项调用完整版本的operator()。
 * 
 * @param environmentCubemap 环境立方体贴图（输入）
 * @param outReflectionsTexture 输出预过滤纹理（如果为null则自动创建）
 * @return 输出预过滤纹理
 */
UTILS_NOINLINE
Texture* IBLPrefilterContext::SpecularFilter::operator()(
        Texture const* environmentCubemap, Texture* outReflectionsTexture) {
    return operator()({}, environmentCubemap, outReflectionsTexture);
}

/**
 * 生成预过滤立方体贴图实现
 * 
 * 执行步骤：
 * 1. 验证输入环境贴图的有效性
 * 2. 如果输出纹理为null，自动创建默认预过滤纹理
 * 3. 设置材质参数（环境贴图、核函数纹理、HDR压缩、LOD偏移）
 * 4. 如果需要，生成Mipmap
 * 5. 对每个Mipmap级别（粗糙度级别）：
 *    - 设置材质参数（采样数、附件级别、LOD偏移）
 *    - 对每个面组（3个面）：
 *      - 设置渲染目标（3个面同时渲染）
 *      - 使用重要性采样计算镜面反射
 *      - 渲染到预过滤立方体贴图
 * 6. 清理资源并返回结果
 * 
 * @param options 环境选项（HDR压缩、LOD偏移、Mipmap生成）
 * @param environmentCubemap 环境立方体贴图（输入）
 * @param outReflectionsTexture 输出预过滤纹理（如果为null则自动创建）
 * @return 输出预过滤纹理
 */
Texture* IBLPrefilterContext::SpecularFilter::operator()(
        Options options,
        Texture const* environmentCubemap, Texture* outReflectionsTexture) {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    using namespace backend;

    FILAMENT_CHECK_PRECONDITION(environmentCubemap != nullptr) << "environmentCubemap is null!";

    FILAMENT_CHECK_PRECONDITION(
            environmentCubemap->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
            << "environmentCubemap must be a cubemap.";

    UTILS_UNUSED_IN_RELEASE
    const uint8_t maxLevelCount = uint8_t(std::log2(environmentCubemap->getWidth()) + 0.5f) + 1u;

    FILAMENT_CHECK_PRECONDITION(environmentCubemap->getLevels() == maxLevelCount)
            << "environmentCubemap must have " << +maxLevelCount << " mipmap levels allocated.";

    if (outReflectionsTexture == nullptr) {
        const uint8_t levels = mLevelCount;

        // default texture is 256 or larger to accommodate the level count requested
        const uint32_t dim = std::max(256u, 1u << (levels - 1u));

        outReflectionsTexture =
                Texture::Builder()
                        .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                        .format(Texture::InternalFormat::R11F_G11F_B10F)
                        .usage(COMMON_USAGE |
                                (options.generateMipmap ? MIPMAP_USAGE : Texture::Usage::NONE))
                        .width(dim)
                        .height(dim)
                        .levels(levels)
                        .build(mContext.mEngine);
    }

    FILAMENT_CHECK_PRECONDITION(
            outReflectionsTexture->getTarget() == Texture::Sampler::SAMPLER_CUBEMAP)
            << "outReflectionsTexture must be a cubemap.";

    FILAMENT_CHECK_PRECONDITION(mLevelCount <= outReflectionsTexture->getLevels())
            << "outReflectionsTexture has " << +outReflectionsTexture->getLevels() << " levels but "
            << +mLevelCount << " are requested.";

    const TextureCubemapFace faces[2][3] = {
            { TextureCubemapFace::POSITIVE_X, TextureCubemapFace::POSITIVE_Y, TextureCubemapFace::POSITIVE_Z },
            { TextureCubemapFace::NEGATIVE_X, TextureCubemapFace::NEGATIVE_Y, TextureCubemapFace::NEGATIVE_Z }
    };

    Engine& engine = mContext.mEngine;
    View* const view = mContext.mView;
    Renderer* const renderer = mContext.mRenderer;
    MaterialInstance* const mi = mContext.mIntegrationMaterial->createInstance();

    RenderableManager& rcm = engine.getRenderableManager();
    auto const ci = rcm.getInstance(mContext.mFullScreenQuadEntity);

    const uint32_t sampleCount = mSampleCount;
    const float linear = options.hdrLinear;
    const float compress = options.hdrMax;
    const uint8_t levels = outReflectionsTexture->getLevels();
    uint32_t dim = outReflectionsTexture->getWidth();
    const float omegaP = (4.0f * f::PI) / float(6 * dim * dim);

    TextureSampler environmentSampler;
    environmentSampler.setMagFilter(SamplerMagFilter::LINEAR);
    environmentSampler.setMinFilter(SamplerMinFilter::LINEAR_MIPMAP_LINEAR);

    mi->setParameter("environment", environmentCubemap, environmentSampler);
    mi->setParameter("kernel", mKernelTexture, TextureSampler{ SamplerMagFilter::NEAREST });
    mi->setParameter("compress", float2{ linear, compress });
    mi->setParameter("lodOffset", options.lodOffset - log4(omegaP));

    if (options.generateMipmap) {
        // We need mipmaps for prefiltering
        environmentCubemap->generateMipmaps(engine);
    }

    RenderTarget::Builder builder;
    builder.texture(RenderTarget::AttachmentPoint::COLOR0, outReflectionsTexture)
           .texture(RenderTarget::AttachmentPoint::COLOR1, outReflectionsTexture)
           .texture(RenderTarget::AttachmentPoint::COLOR2, outReflectionsTexture);

    for (size_t lod = 0; lod < levels; lod++) {
        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "executeFilterLOD");

        mi->setParameter("sampleCount", uint32_t(lod == 0 ? 1u : sampleCount));
        mi->setParameter("attachmentLevel", uint32_t(lod));

        if (lod == levels - 1) {
            // this is the last lod, use a more aggressive filtering because this level is also
            // used for the diffuse brdf by filament, and we need it to be very smooth.
            // So we set the lod offset to at least 2.
            mi->setParameter("lodOffset", std::max(2.0f, options.lodOffset) - log4(omegaP));
        }

        builder.mipLevel(RenderTarget::AttachmentPoint::COLOR0, lod)
               .mipLevel(RenderTarget::AttachmentPoint::COLOR1, lod)
               .mipLevel(RenderTarget::AttachmentPoint::COLOR2, lod);

        view->setViewport({ 0, 0, dim, dim });

        for (size_t i = 0; i < 2; i++) {
            // This is a workaround for internal bug b/419664914 to duplicate same material for each draw.
            // TODO: properly address the bug and remove this workaround.
#if defined(__EMSCRIPTEN__)
            MaterialInstance *const tempMi = MaterialInstance::duplicate(mi);
#else
            MaterialInstance *const tempMi = mi;
#endif
            rcm.setMaterialInstanceAt(ci, 0, tempMi);

            tempMi->setParameter("side", i == 0 ? 1.0f : -1.0f);
            tempMi->commit(engine);

            builder.face(RenderTarget::AttachmentPoint::COLOR0, faces[i][0])
                   .face(RenderTarget::AttachmentPoint::COLOR1, faces[i][1])
                   .face(RenderTarget::AttachmentPoint::COLOR2, faces[i][2]);

            RenderTarget* const rt = builder.build(engine);
            view->setRenderTarget(rt);
            renderer->renderStandaloneView(view);
            engine.destroy(rt);

#if defined(__EMSCRIPTEN__)
            cleanupMaterialInstance(tempMi, engine, rcm, ci);
#endif
        }

        dim >>= 1;
    }

    rcm.clearMaterialInstanceAt(ci, 0);
    engine.destroy(mi);

    return outReflectionsTexture;
}
