# Filament 后处理系统详细分析

## 目录
1. [概述](#概述)
2. [PostProcessManager 架构](#postprocessmanager-架构)
3. [后处理管线流程](#后处理管线流程)
4. [各后处理效果详解](#各后处理效果详解)
5. [FrameGraph 集成](#framegraph-集成)
6. [性能优化](#性能优化)

---

## 概述

Filament 的后处理系统（Post-Processing System）负责在渲染管线末尾应用各种视觉效果，包括色调映射、泛光、抗锯齿、景深、屏幕空间反射等。所有后处理效果都通过 `PostProcessManager` 统一管理，并集成到 FrameGraph 中。

### 核心组件

- **PostProcessManager**：后处理管理器，管理所有后处理材质和效果
- **PostProcessMaterial**：后处理材质封装（延迟加载）
- **FrameGraph**：后处理 Pass 的依赖管理和资源分配
- **FrameHistory**：帧历史数据（用于 TAA、SSR 等时间性效果）

---

## PostProcessManager 架构

### 1. 类结构

```cpp
class PostProcessManager {
    // 后处理材质管理
    struct PostProcessMaterial {
        StaticMaterialInfo mConstants;  // 特化常量
        void* mData;                    // 材质包数据
        size_t mSize;                   // 材质包大小
        FMaterial* mMaterial;           // 延迟加载的材质
    };
    
    // 材质实例管理器
    MaterialInstanceManager mMaterialInstanceManager;
    
    // 后处理描述符集
    PostProcessDescriptorSet mPostProcessDescriptorSet;
    StructureDescriptorSet mStructureDescriptorSet;
    SsrPassDescriptorSet mSsrPassDescriptorSet;
};
```

### 2. 后处理材质加载

**延迟加载机制：**

```cpp
FMaterial* PostProcessMaterial::getMaterial(FEngine& engine, 
                                            PostProcessVariant variant) const {
    if (UTILS_UNLIKELY(mSize)) {
        // 首次使用时才加载材质
        loadMaterial(engine);
    }
    return mMaterial->getMaterial(engine, variant);
}
```

**加载流程：**

1. 从静态材质包数据创建 Material::Builder
2. 设置特化常量（Specialization Constants）
3. 调用 `Material::Builder::build()` 创建 Material
4. 缓存 Material 对象，后续直接使用

### 3. 材质实例管理

`MaterialInstanceManager` 负责复用 MaterialInstance，避免频繁创建/销毁：

```cpp
FMaterialInstance* getMaterialInstance(FMaterial* material) {
    // 查找或创建 MaterialInstance
    auto it = mInstances.find(material);
    if (it != mInstances.end()) {
        return it->second;
    }
    auto* mi = material->createInstance();
    mInstances[material] = mi;
    return mi;
}
```

---

## 后处理管线流程

### 1. 渲染管线中的位置

后处理在渲染管线中的位置（`Renderer::renderInternal`）：

```
1. Shadow Pass（阴影渲染）
2. Structure Pass（结构/深度 Pass）
3. SSAO Pass（屏幕空间环境光遮蔽）
4. SSR Pass（屏幕空间反射）
5. Color Pass（主渲染）
6. [后处理开始]
   - Resolve Depth（解析深度）
   - TAA（时间抗锯齿）
   - DoF（景深）
   - Bloom（泛光）
   - Color Grading（色调映射/颜色分级）
   - FXAA（快速近似抗锯齿，可选）
   - Dithering（抖动）
   - Vignette（暗角）
   - Final Blit（最终输出）
```

### 2. 后处理 Pass 顺序

**在 `Renderer::renderInternal` 中：**

```cpp
// 1. 解析深度（如果需要）
auto depth = ppm.resolve(fg, "Resolved Depth Buffer", 
                         colorPassOutput.depth, { .levels = 1 });

// 2. TAA（时间抗锯齿）
if (taaOptions.enabled) {
    input = ppm.taa(fg, input, depth, view.getFrameHistory(), 
                    &FrameHistoryEntry::taa, taaOptions, colorGradingConfig);
}

// 3. DoF（景深，可选）
if (dofOptions.enabled) {
    input = ppm.dof(fg, input, depth, cameraInfo, 
                    translucent, bokehScale, dofOptions);
}

// 4. Bloom（泛光）
BloomPassOutput bloomOutput = ppm.bloom(fg, input, outFormat, 
                                        bloomOptions, taaOptions, scale);

// 5. Color Grading（色调映射 + 颜色分级）
input = ppm.colorGrading(fg, input, vp, bloomOutput.bloom, 
                         bloomOutput.flare, colorGrading, 
                         colorGradingConfig, bloomOptions, vignetteOptions);

// 6. FXAA（可选）
if (fxaaOptions.enabled) {
    input = ppm.fxaa(fg, input, fxaaOptions);
}
```

---

## 各后处理效果详解

### 1. Structure Pass（结构/深度 Pass）

**目的：** 生成深度缓冲和结构信息，供后续 Pass 使用（SSAO、SSR、TAA）

**实现：**

```cpp
StructurePassOutput PostProcessManager::structure(FrameGraph& fg,
        RenderPassBuilder const& passBuilder, 
        uint8_t structureRenderFlags,
        uint32_t width, uint32_t height, 
        StructurePassConfig const& config) {
    // 创建结构纹理（RGBA8，包含深度、法线、粗糙度等）
    auto structure = builder.createTexture("Structure", {
        .width = width * config.scale,
        .height = height * config.scale,
        .format = TextureFormat::RGBA8
    });
    
    // 创建深度纹理
    auto depth = builder.createTexture("Depth", {
        .width = width,
        .height = height,
        .format = TextureFormat::DEPTH24
    });
    
    // 渲染结构 Pass
    // 使用特殊的材质变体，输出深度、法线、粗糙度等信息
}
```

**输出：**
- **Structure Texture**：RGBA8，包含法线、粗糙度等信息
- **Depth Texture**：深度缓冲

### 2. SSAO（屏幕空间环境光遮蔽）

**目的：** 在屏幕空间计算环境光遮蔽，增强场景深度感

**实现：**

```cpp
FrameGraphId<FrameGraphTexture> PostProcessManager::screenSpaceAmbientOcclusion(
        FrameGraph& fg, Viewport const& svp, 
        const CameraInfo& cameraInfo,
        FrameGraphId<FrameGraphTexture> depth,
        AmbientOcclusionOptions const& options) {
    
    // 1. 创建 AO 纹理（单通道）
    auto ao = builder.createTexture("SSAO", {
        .width = svp.width * options.resolution,
        .height = svp.height * options.resolution,
        .format = TextureFormat::R8
    });
    
    // 2. 设置采样参数
    mi->setParameter("depth", depth, SamplerParams{...});
    mi->setParameter("radius", options.radius);
    mi->setParameter("power", options.power);
    mi->setParameter("bias", options.bias);
    
    // 3. 执行 AO Pass
    // 使用半球采样，计算遮挡因子
}
```

**算法：**
- 在深度缓冲上进行半球采样
- 计算采样点的遮挡程度
- 使用双边滤波平滑结果

### 3. SSR（屏幕空间反射）

**目的：** 在屏幕空间计算反射，无需预计算环境贴图

**实现：**

```cpp
FrameGraphId<FrameGraphTexture> PostProcessManager::ssr(
        FrameGraph& fg, RenderPassBuilder const& passBuilder,
        FrameHistory const& frameHistory,
        FrameGraphId<FrameGraphTexture> structure,
        FrameGraphTexture::Descriptor const& desc) {
    
    // 1. 准备 SSR Mipmap 配置
    auto ssrConfig = prepareMipmapSSR(fg, desc.width, desc.height, 
                                      format, verticalFov, scale);
    
    // 2. 生成反射/折射纹理（2D Array，包含多个 LOD）
    // 3. 使用高斯模糊生成 Mipmap
    generateMipmapSSR(ppm, fg, input, ssrConfig.ssr, 
                      needDuplication, ssrConfig);
}
```

**特点：**
- 使用 2D 纹理数组存储不同粗糙度级别的反射
- 通过高斯模糊生成 Mipmap
- 支持折射和反射两种模式

### 4. TAA（时间抗锯齿）

**目的：** 通过多帧历史数据减少锯齿和闪烁

**实现：**

```cpp
FrameGraphId<FrameGraphTexture> PostProcessManager::taa(
        FrameGraph& fg, FrameGraphId<FrameGraphTexture> input,
        FrameGraphId<FrameGraphTexture> depth,
        FrameHistory const& frameHistory,
        FrameHistoryEntry::TaaHistory* history,
        TemporalAntiAliasingOptions const& taaOptions,
        ColorGradingConfig const& colorGradingConfig) {
    
    // 1. 应用相机抖动（Jitter）
    TaaJitterCamera(svp, taaOptions, frameHistory, 
                    &FrameHistoryEntry::taa, &cameraInfo);
    
    // 2. 计算重投影矩阵
    mat4f reprojection = historyProjection * 
                        inverse(current.projection) * 
                        normalizedToClip;
    
    // 3. 计算滤波权重（Lanczos 滤波）
    float2 weights[9];
    for (size_t i = 0; i < 9; i++) {
        float2 d = (sampleOffsets[i] - current.jitter) / filterWidth;
        weights[i] = lanczos(length(d), filterWidth);
    }
    
    // 4. 执行 TAA Pass
    mi->setParameter("color", currentColor, ...);
    mi->setParameter("depth", depth, ...);
    mi->setParameter("history", historyColor, ...);
    mi->setParameter("reprojection", reprojection);
    mi->setParameter("jitter", current.jitter);
    mi->setParameter("filterWeights", weights, 9);
}
```

**算法：**
- **Jitter**：每帧对相机投影矩阵应用微小偏移（Halton 序列）
- **Reprojection**：将上一帧的颜色重投影到当前帧
- **Blending**：混合当前帧和重投影的历史帧
- **Filtering**：使用 Lanczos 滤波减少重投影错误

### 5. Bloom（泛光）

**目的：** 模拟明亮光源的泛光效果

**实现：**

```cpp
BloomPassOutput PostProcessManager::bloom(
        FrameGraph& fg, FrameGraphId<FrameGraphTexture> input,
        backend::TextureFormat outFormat,
        BloomOptions& inoutBloomOptions,
        TemporalAntiAliasingOptions const& taaOptions,
        math::float2 scale) {
    
    // 1. 提取亮部（Threshold）
    auto bright = builder.createTexture("Bright", {...});
    // 使用阈值提取亮部区域
    
    // 2. 下采样（Downsample）
    FrameGraphId<FrameGraphTexture> bloomTextures[kMaxBloomLevels];
    for (size_t i = 0; i < bloomLevels; i++) {
        bloomTextures[i] = downsample(fg, input, i);
    }
    
    // 3. 上采样并混合（Upsample）
    auto bloom = bloomTextures[bloomLevels - 1];
    for (size_t i = bloomLevels - 1; i > 0; i--) {
        bloom = upsample(fg, bloom, bloomTextures[i - 1]);
    }
    
    // 4. 泛光强度调整
    mi->setParameter("bloomStrength", bloomOptions.strength);
}
```

**算法：**
- **Threshold**：提取超过阈值的亮部
- **Downsample**：多级下采样，生成不同尺寸的泛光
- **Upsample**：上采样并混合，产生平滑的泛光效果

### 6. DoF（景深）

**目的：** 模拟相机焦点效果，模糊前景和背景

**实现：**

```cpp
FrameGraphId<FrameGraphTexture> PostProcessManager::dof(
        FrameGraph& fg,
        FrameGraphId<FrameGraphTexture> input,
        FrameGraphId<FrameGraphTexture> depth,
        const CameraInfo& cameraInfo,
        bool translucent,
        math::float2 bokehScale,
        const DepthOfFieldOptions& dofOptions) {
    
    // 1. 计算 CoC（Circle of Confusion）
    float cocScale = computeCoCScale(cameraInfo, dofOptions);
    
    // 2. 预过滤（Prefilter）
    // 分离前景和背景
    
    // 3. 散景（Bokeh）Pass
    // 使用环形采样模式
    
    // 4. 后处理（Post-filter）
    // 平滑散景结果
}
```

**算法：**
- **CoC 计算**：根据深度和焦点距离计算模糊半径
- **Bokeh**：使用环形采样模式模拟散景效果
- **分层处理**：分别处理前景和背景

### 7. Color Grading（色调映射 + 颜色分级）

**目的：** 将 HDR 颜色映射到 LDR，并应用颜色校正

**实现：**

```cpp
FrameGraphId<FrameGraphTexture> PostProcessManager::colorGrading(
        FrameGraph& fg, FrameGraphId<FrameGraphTexture> input,
        Viewport const& vp,
        FrameGraphId<FrameGraphTexture> bloom,
        FrameGraphId<FrameGraphTexture> flare,
        const FColorGrading* colorGrading,
        ColorGradingConfig const& colorGradingConfig,
        BloomOptions const& bloomOptions,
        VignetteOptions const& vignetteOptions) {
    
    // 1. 色调映射（Tone Mapping）
    // ACES、Reinhard、Uchimura 等算法
    
    // 2. 颜色分级（Color Grading）
    // 使用 3D LUT（查找表）
    
    // 3. 抖动（Dithering）
    // 减少颜色带
    
    // 4. 暗角（Vignette）
    // 边缘变暗效果
}
```

**色调映射算法：**
- **ACES**：Academy Color Encoding System
- **Reinhard**：简单的色调映射
- **Uchimura**：针对移动设备优化

---

## FrameGraph 集成

### 1. 资源声明

每个后处理 Pass 在 FrameGraph 中声明输入/输出资源：

```cpp
auto& taaPass = fg.addPass<TAAData>("TAA",
    [&](FrameGraph::Builder& builder, auto& data) {
        // 声明输入资源
        data.color = builder.sample(input);
        data.depth = builder.sample(depth);
        data.history = builder.sample(colorHistory);
        
        // 创建输出资源
        data.output = builder.createTexture("TAA output", desc);
        data.output = builder.write(data.output);
        
        // 声明 Render Pass
        builder.declareRenderPass("TAA target", {
            .attachments = { .color = { data.output }}
        });
    },
    [=](FrameGraphResources const& resources, auto const& data, DriverApi& driver) {
        // 执行 Pass
        auto rt = resources.getRenderPassInfo();
        driver.beginRenderPass(rt.target, rt.params);
        // ... 渲染
        driver.endRenderPass();
    });
```

### 2. 资源依赖

FrameGraph 自动处理资源依赖：

- **sample()**：声明只读依赖
- **write()**：声明写入依赖
- **read()**：声明子通道输入依赖

### 3. Pass 排序

FrameGraph 根据资源依赖自动排序 Pass：

```
input → TAA → DoF → Bloom → ColorGrading → output
```

---

## 性能优化

### 1. 分辨率缩放

许多后处理效果使用降低的分辨率：

- **SSAO**：`options.resolution`（默认 0.5）
- **Structure Pass**：`config.scale`（默认 0.5）
- **Bloom**：多级下采样

### 2. 材质实例复用

`MaterialInstanceManager` 复用 MaterialInstance，避免每帧创建：

```cpp
FMaterialInstance* getMaterialInstance(FMaterial* material) {
    // 查找缓存
    auto it = mInstances.find(material);
    if (it != mInstances.end()) {
        return it->second;
    }
    // 创建新实例并缓存
    auto* mi = material->createInstance();
    mInstances[material] = mi;
    return mi;
}
```

### 3. 延迟材质加载

后处理材质首次使用时才加载：

```cpp
FMaterial* getMaterial(FEngine& engine, PostProcessVariant variant) const {
    if (UTILS_UNLIKELY(mSize)) {
        loadMaterial(engine);  // 延迟加载
    }
    return mMaterial->getMaterial(engine, variant);
}
```

### 4. FrameGraph 剔除

未使用的后处理 Pass 会被 FrameGraph 自动剔除：

```cpp
// 如果 TAA 未启用，整个 TAA Pass 会被剔除
if (taaOptions.enabled) {
    input = ppm.taa(...);
}
```

### 5. Subpass 优化

某些后处理可以合并到同一个 Render Pass：

```cpp
// TAA 和 Color Grading 可以合并为 Subpass
if (colorGradingConfig.asSubpass) {
    data.tonemappedOutput = builder.createTexture("Tonemapped Buffer", {...});
    data.output = builder.read(data.output, FrameGraphTexture::Usage::SUBPASS_INPUT);
}
```

---

## 总结

Filament 的后处理系统通过以下设计实现了高效的视觉效果：

1. **统一管理**：PostProcessManager 统一管理所有后处理效果
2. **FrameGraph 集成**：自动处理资源依赖和 Pass 排序
3. **延迟加载**：材质和实例按需创建和复用
4. **分辨率优化**：降低分辨率减少计算量
5. **Subpass 优化**：合并多个效果到同一 Render Pass

这些设计使得 Filament 能够在保持高质量视觉效果的同时，维持良好的性能。

