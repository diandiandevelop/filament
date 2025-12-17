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

#ifndef TNT_IBL_PREFILTER_IBLPREFILTER_H
#define TNT_IBL_PREFILTER_IBLPREFILTER_H

#include <utils/compiler.h>
#include <utils/Entity.h>

#include <filament/Texture.h>

namespace filament {
class Engine;
class View;
class Scene;
class Renderer;
class Material;
class MaterialInstance;
class VertexBuffer;
class IndexBuffer;
class Camera;
class Texture;
} // namespace filament

/**
 * IBLPrefilterContext - IBL预过滤上下文类
 * 
 * 创建并初始化所有支持的环境贴图过滤器共用的GPU状态。
 * 通常每个Filament引擎只需要一个此对象的实例。
 * 
 * 使用示例：
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * #include <filament/Engine.h>
 * using namespace filament;
 *
 * Engine* engine = Engine::create();
 *
 * IBLPrefilterContext context(engine);
 * IBLPrefilterContext::SpecularFilter filter(context);
 * Texture* texture = filter(environment_cubemap);
 *
 * IndirectLight* indirectLight = IndirectLight::Builder()
 *     .reflections(texture)
 *     .build(engine);
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
class UTILS_PUBLIC IBLPrefilterContext {
public:

    /**
     * 过滤器核函数枚举
     * 
     * 定义用于预过滤的分布函数类型。
     */
    enum class Kernel : uint8_t {
        D_GGX,        // Trowbridge-Reitz分布（GGX/Trowbridge-Reitz分布，用于PBR渲染）
    };

    /**
     * 构造函数
     * 
     * 创建IBL预过滤上下文，初始化所有过滤器共用的GPU资源。
     * 
     * @param engine Filament引擎引用
     */
    explicit IBLPrefilterContext(filament::Engine& engine);

    /**
     * 析构函数
     * 
     * 销毁初始化期间创建的所有GPU资源。
     */
    ~IBLPrefilterContext() noexcept;

    // not copyable
    // 不可拷贝（删除拷贝构造函数和拷贝赋值运算符）
    IBLPrefilterContext(IBLPrefilterContext const&) = delete;
    IBLPrefilterContext& operator=(IBLPrefilterContext const&) = delete;

    // movable
    // 可移动（支持移动构造函数和移动赋值运算符）
    IBLPrefilterContext(IBLPrefilterContext&& rhs) noexcept;
    IBLPrefilterContext& operator=(IBLPrefilterContext&& rhs) noexcept;

    // -------------------------------------------------------------------------------------------

    /**
     * EquirectangularToCubemap - 等距圆柱投影到立方体贴图转换器
     * 
     * 用于将等距圆柱投影（Equirectangular）图像转换为立方体贴图。
     * 等距圆柱投影是一种常见的环境贴图格式，宽高比为2:1。
     */
    class EquirectangularToCubemap {
    public:

        /**
         * 配置结构体
         */
        struct Config {
            bool mirror = true;  //!< 是否在水平方向镜像源图像
        };

        /**
         * 构造函数（使用默认配置）
         * 
         * 创建等距圆柱投影到立方体贴图转换器，使用默认配置。
         * 
         * @param context IBL预过滤上下文
         */
        explicit EquirectangularToCubemap(IBLPrefilterContext& context);

        /**
         * 构造函数（使用指定配置）
         * 
         * 创建等距圆柱投影到立方体贴图转换器，使用提供的配置。
         * 
         * @param context IBL预过滤上下文
         * @param config 配置参数
         */
       EquirectangularToCubemap(IBLPrefilterContext& context, Config const& config);

        /**
         * 析构函数
         * 
         * 销毁初始化期间创建的所有GPU资源。
         */
        ~EquirectangularToCubemap() noexcept;

        /**
         * 删除拷贝构造函数和拷贝赋值运算符（不可拷贝）
         */
        EquirectangularToCubemap(EquirectangularToCubemap const&) = delete;
        EquirectangularToCubemap& operator=(EquirectangularToCubemap const&) = delete;
        
        /**
         * 移动构造函数和移动赋值运算符（可移动）
         */
        EquirectangularToCubemap(EquirectangularToCubemap&& rhs) noexcept;
        EquirectangularToCubemap& operator=(EquirectangularToCubemap&& rhs) noexcept;

        /**
         * 将等距圆柱投影图像转换为立方体贴图
         * 
         * 执行步骤：
         * 1. 验证输入纹理的有效性
         * 2. 如果输出纹理为null，自动创建默认立方体贴图
         * 3. 使用GPU着色器将等距圆柱投影图像投影到立方体贴图的6个面
         * 4. 返回生成的立方体贴图
         * 
         * @param equirectangular 要转换的等距圆柱投影纹理
         *                          - 不能为null
         *                          - 必须是2D纹理
         *                          - 必须具有等距圆柱投影几何形状，即宽度 == 2*高度
         *                          - 必须分配所有Mipmap级别
         *                          - 必须具有SAMPLEABLE用途
         * @param outCubemap 输出立方体贴图。如果为null，将使用默认参数自动创建纹理
         *                    （尺寸256，9个Mipmap级别）
         *                          - 必须是立方体贴图
         *                          - 必须具有SAMPLEABLE和COLOR_ATTACHMENT用途位
         * @return 返回outCubemap
         */
        filament::Texture* operator()(
                filament::Texture const* equirectangular,
                filament::Texture* outCubemap = nullptr);

    private:
        IBLPrefilterContext& mContext;              // IBL预过滤上下文引用
        filament::Material* mEquirectMaterial = nullptr;  // 等距圆柱投影材质
        Config mConfig{};                           // 配置参数
    };

    /**
     * IrradianceFilter - 辐照度过滤器类
     * 
     * 基于GPU的漫反射探针预积分过滤器实现。
     * 每个过滤器配置需要一个IrradianceFilter实例。
     * 过滤器配置包含过滤器的核函数和采样数。
     * 
     * 用于生成环境贴图的漫反射辐照度，这是PBR渲染中用于计算漫反射光照的预过滤环境贴图。
     */
    class IrradianceFilter {
    public:
        using Kernel = Kernel;  // 使用父类的Kernel类型

        /**
         * 过滤器配置结构体
         */
        struct Config {
            uint16_t sampleCount = 1024u;   //!< 过滤器采样数（最大2048）
            Kernel kernel = Kernel::D_GGX;  //!< 过滤器核函数
        };

        /**
         * 当前环境的过滤选项结构体
         */
        struct Options {
            float hdrLinear = 1024.0f;   //!< 在此值以下不进行HDR压缩
            float hdrMax = 16384.0f;     //!< 在hdrLinear和hdrMax之间进行HDR压缩
            float lodOffset = 2.0f;      //!< 良好的值是2.0或3.0。更高的值有助于处理高动态范围输入
            bool generateMipmap = true;  //!< 如果输入环境贴图已经有Mipmap，设置为false
        };

        /**
         * 构造函数（使用指定配置）
         * 
         * 创建辐照度过滤器处理器。
         * 
         * @param context IBL预过滤上下文
         * @param config 过滤器配置
         */
        IrradianceFilter(IBLPrefilterContext& context, Config config);

        /**
         * 构造函数（使用默认配置）
         * 
         * 使用默认配置创建过滤器。
         * 
         * @param context IBL预过滤上下文
         */
        explicit IrradianceFilter(IBLPrefilterContext& context);

        /**
         * 析构函数
         * 
         * 销毁初始化期间创建的所有GPU资源。
         */
        ~IrradianceFilter() noexcept;

        /**
         * 删除拷贝构造函数和拷贝赋值运算符（不可拷贝）
         */
        IrradianceFilter(IrradianceFilter const&) = delete;
        IrradianceFilter& operator=(IrradianceFilter const&) = delete;
        
        /**
         * 移动构造函数和移动赋值运算符（可移动）
         */
        IrradianceFilter(IrradianceFilter&& rhs) noexcept;
        IrradianceFilter& operator=(IrradianceFilter&& rhs) noexcept;

        /**
         * 生成辐照度立方体贴图
         * 
         * 执行步骤：
         * 1. 验证输入环境贴图的有效性
         * 2. 如果输出纹理为null，自动创建默认辐照度纹理
         * 3. 使用重要性采样和GPU着色器计算漫反射辐照度
         * 4. 将结果写入输出立方体贴图
         * 
         * 注意：即使存在Mipmap也不会生成Mipmap。
         * 
         * @param options 环境选项
         * @param environmentCubemap 环境立方体贴图（输入）。不能为null。
         *                            此立方体贴图必须具有SAMPLEABLE用途，并且必须分配所有级别。
         *                            如果Options.generateMipmap为true，Mipmap级别将被覆盖，
         *                            否则假设所有级别都已正确初始化。
         * @param outIrradianceTexture 输出辐照度纹理，如果为null，将使用默认参数自动创建。
         *                              outIrradianceTexture必须是立方体贴图，必须至少具有
         *                              COLOR_ATTACHMENT和SAMPLEABLE用途。
         * @return 返回outIrradianceTexture
         */
        filament::Texture* operator()(Options options,
                filament::Texture const* environmentCubemap,
                filament::Texture* outIrradianceTexture = nullptr);

        /**
         * 生成预过滤立方体贴图（使用默认选项）
         * 
         * 执行步骤：
         * 1. 使用默认选项调用完整版本的operator()
         * 2. 生成辐照度立方体贴图
         * 
         * @param environmentCubemap 环境立方体贴图（输入）。不能为null。
         *                            此立方体贴图必须具有SAMPLEABLE用途，并且必须分配所有级别。
         *                            如果Options.generateMipmap为true，Mipmap级别将被覆盖，
         *                            否则假设所有级别都已正确初始化。
         * @param outIrradianceTexture 输出辐照度纹理，如果为null，将使用默认参数自动创建。
         *                              outIrradianceTexture必须是立方体贴图，必须至少具有
         *                              COLOR_ATTACHMENT和SAMPLEABLE用途。
         * @return 返回outIrradianceTexture
         */
        filament::Texture* operator()(
                filament::Texture const* environmentCubemap,
                filament::Texture* outIrradianceTexture = nullptr);

    private:
        IBLPrefilterContext& mContext;              // IBL预过滤上下文引用
        filament::Material* mKernelMaterial = nullptr;  // 核函数材质
        filament::Texture* mKernelTexture = nullptr;     // 核函数纹理（用于重要性采样）
        uint32_t mSampleCount = 0u;                 // 采样数
    };

    /**
     * SpecularFilter - 镜面反射过滤器类
     * 
     * 基于GPU的镜面反射探针预积分过滤器实现。
     * 每个过滤器配置需要一个SpecularFilter实例。
     * 过滤器配置包含过滤器的核函数、采样数和粗糙度级别数。
     * 
     * 用于生成环境贴图的预过滤镜面反射贴图，这是PBR渲染中用于计算镜面反射光照的预过滤环境贴图。
     * 每个Mipmap级别对应不同的粗糙度值。
     */
    class SpecularFilter {
    public:
        using Kernel = Kernel;  // 使用父类的Kernel类型

        /**
         * 过滤器配置结构体
         */
        struct Config {
            uint16_t sampleCount = 1024u;   //!< 过滤器采样数（最大2048）
            uint8_t levelCount = 5u;        //!< 粗糙度级别数（对应Mipmap级别数）
            Kernel kernel = Kernel::D_GGX;  //!< 过滤器核函数
        };

        /**
         * 当前环境的过滤选项结构体
         */
        struct Options {
            float hdrLinear = 1024.0f;   //!< 在此值以下不进行HDR压缩
            float hdrMax = 16384.0f;     //!< 在hdrLinear和hdrMax之间进行HDR压缩
            float lodOffset = 1.0f;      //!< 良好的值是1.0或2.0。更高的值有助于处理高动态范围输入
            bool generateMipmap = true;  //!< 如果输入环境贴图已经有Mipmap，设置为false
        };

        /**
         * 构造函数（使用指定配置）
         * 
         * 创建镜面反射过滤器处理器。
         * 
         * @param context IBL预过滤上下文
         * @param config 过滤器配置
         */
        SpecularFilter(IBLPrefilterContext& context, Config config);

        /**
         * 构造函数（使用默认配置）
         * 
         * 使用默认配置创建过滤器。
         * 
         * @param context IBL预过滤上下文
         */
        explicit SpecularFilter(IBLPrefilterContext& context);

        /**
         * 析构函数
         * 
         * 销毁初始化期间创建的所有GPU资源。
         */
        ~SpecularFilter() noexcept;

        /**
         * 删除拷贝构造函数和拷贝赋值运算符（不可拷贝）
         */
        SpecularFilter(SpecularFilter const&) = delete;
        SpecularFilter& operator=(SpecularFilter const&) = delete;
        
        /**
         * 移动构造函数和移动赋值运算符（可移动）
         */
        SpecularFilter(SpecularFilter&& rhs) noexcept;
        SpecularFilter& operator=(SpecularFilter&& rhs) noexcept;

        /**
         * 生成预过滤立方体贴图
         * 
         * 执行步骤：
         * 1. 验证输入环境贴图的有效性
         * 2. 如果输出纹理为null，自动创建默认预过滤纹理
         * 3. 对每个粗糙度级别（Mipmap级别）：
         *    - 使用重要性采样和GGX分布函数计算该粗糙度下的镜面反射
         *    - 使用GPU着色器进行预过滤
         *    - 将结果写入对应的Mipmap级别
         * 4. 返回生成的预过滤立方体贴图
         * 
         * @param options 环境选项
         * @param environmentCubemap 环境立方体贴图（输入）。不能为null。
         *                            此立方体贴图必须具有SAMPLEABLE用途，并且必须分配所有级别。
         *                            如果Options.generateMipmap为true，Mipmap级别将被覆盖，
         *                            否则假设所有级别都已正确初始化。
         * @param outReflectionsTexture 输出预过滤纹理，如果为null，将使用默认参数自动创建。
         *                               outReflectionsTexture必须是立方体贴图，必须至少具有
         *                               COLOR_ATTACHMENT和SAMPLEABLE用途，并且至少具有Config请求的级别数。
         * @return 返回outReflectionsTexture
         */
        filament::Texture* operator()(Options options,
                filament::Texture const* environmentCubemap,
                filament::Texture* outReflectionsTexture = nullptr);

        /**
         * 生成预过滤立方体贴图（使用默认选项）
         * 
         * 执行步骤：
         * 1. 使用默认选项调用完整版本的operator()
         * 2. 生成预过滤立方体贴图
         * 
         * @param environmentCubemap 环境立方体贴图（输入）。不能为null。
         *                            此立方体贴图必须具有SAMPLEABLE用途，并且必须分配所有级别。
         *                            所有Mipmap级别将被覆盖。
         * @param outReflectionsTexture 输出预过滤纹理，如果为null，将使用默认参数自动创建。
         *                               outReflectionsTexture必须是立方体贴图，必须至少具有
         *                               COLOR_ATTACHMENT和SAMPLEABLE用途，并且至少具有Config请求的级别数。
         * @return 返回outReflectionsTexture
         */
        filament::Texture* operator()(
                filament::Texture const* environmentCubemap,
                filament::Texture* outReflectionsTexture = nullptr);

        // TODO: option for progressive filtering
        // TODO: 渐进式过滤选项

        // TODO: add a callback for when the processing is done?
        // TODO: 添加处理完成时的回调？

    private:
        IBLPrefilterContext& mContext;              // IBL预过滤上下文引用
        filament::Material* mKernelMaterial = nullptr;  // 核函数材质
        filament::Texture* mKernelTexture = nullptr;     // 核函数纹理（用于重要性采样）
        uint32_t mSampleCount = 0u;                 // 采样数
        uint8_t mLevelCount = 1u;                    // 粗糙度级别数（Mipmap级别数）
    };

private:
    friend class Filter;  // 允许Filter类访问私有成员
    
    // GPU资源（所有过滤器共用）
    filament::Engine& mEngine;                      // Filament引擎引用
    filament::Renderer* mRenderer{};                // 渲染器
    filament::Scene* mScene{};                      // 场景
    filament::VertexBuffer* mVertexBuffer{};        // 顶点缓冲区（全屏四边形）
    filament::IndexBuffer* mIndexBuffer{};          // 索引缓冲区
    filament::Camera* mCamera{};                    // 相机
    utils::Entity mFullScreenQuadEntity{};          // 全屏四边形实体
    utils::Entity mCameraEntity{};                  // 相机实体
    filament::View* mView{};                        // 视图
    filament::Material* mIntegrationMaterial{};     // 积分材质（用于镜面反射过滤）
    filament::Material* mIrradianceIntegrationMaterial{};  // 辐照度积分材质（用于漫反射过滤）
};

#endif //TNT_IBL_PREFILTER_IBLPREFILTER_H
