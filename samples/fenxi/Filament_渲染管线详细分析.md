# Filament 渲染管线主流程详细分析

## 概述

Filament 的渲染管线采用 FrameGraph 架构，将渲染过程分解为多个 Pass，每个 Pass 声明其资源依赖关系，FrameGraph 自动管理资源分配和执行顺序。

## 渲染步骤总览（图式）

- 帧入口：`beginFrame()` → 设置 swapchain、时间戳、driver.beginFrame。
- View 渲染：`render(view)` → `renderInternal()` → `renderJob()`。
- 视图准备（CPU）：
  - 相机/视口/动态分辨率、TAA 抖动。
  - 剔除：视锥剔除、阴影剔除；光源筛选/排序；Froxel/Tile 光分配。
  - 阴影准备：CSM / Spot / Point cube atlas。
  - UBO/材质：`UboManager` 分配槽位，`MaterialInstance::commit()`。
- FrameGraph 构建：
  - Pass 声明：Shadow → Structure/Depth → SSAO/SSR → Color → PostProcess。
  - 资源依赖：read/write 声明；虚拟资源。
- FrameGraph 编译：
  - 拓扑排序、未用 Pass 剔除、资源生命周期(first/last use)→ create/destroy。
- 执行阶段：
  - Shadow Pass：渲染阴影贴图。
  - Structure/Depth Pass：深度/GBuffer 子集。
  - 屏幕空间：SSAO / SSR 等。
  - Color Pass：主色彩，PBR + 直射光/IBL + 阴影 + 透明。
  - 后处理：TAA → DoF → Bloom → Color Grading/ToneMap → FXAA/锐化 → Final Blit。
- 提交与同步：`driver.flush()` → CommandStream → 后端；Fence/Sync 回收，帧时间记录。

### 可编程图形管线速览（类似示意图风格）
```
顶点数据 → 顶点着色器 → 图元装配 → 细分着色器(可选) → 几何着色器(可选)
          ↓                                         ↑
      片段着色器 ← 光栅化 ← 插值/生成片段 ← 深度/模板/混合
```
- 顶点着色器：变换顶点、输出裁剪空间/自定义属性。
- 图元装配：组装三角形/线段/点。
- 细分着色器（可选）：曲面细分前后将控制点/细分为更多图元。
- 几何着色器（可选）：对整条图元再生成/过滤。
- 光栅化：将图元转为片段，插值顶点属性。
- 片段着色器：逐片段计算材质/光照（PBR）、写颜色/深度。
- 深度/模板/混合：裁剪不可见片段，按混合方式写入目标。

## 主流程入口

### 1. Renderer::beginFrame() - 帧开始

**位置**: `filament/src/details/Renderer.cpp:308`

```cpp
bool FRenderer::beginFrame(FSwapChain* swapChain, uint64_t vsyncSteadyClockTimeNano)
```

**功能**:
- 初始化帧渲染环境
- 设置 SwapChain 为当前渲染目标
- 更新帧时间戳
- 启动帧捕获（如果启用）
- 调用 `driver.beginFrame()` 通知后端开始新帧

**关键 API**:
- `swapChain->makeCurrent(driver)` - 设置当前交换链
- `driver.beginFrame()` - 后端帧开始
- `driver.tick()` - 执行后端周期性任务
- `engine.prepare()` - 准备引擎资源（光照缓冲区、材质等）

### 2. Renderer::render() - 渲染主入口

**位置**: `filament/src/details/Renderer.cpp:591`

```cpp
void FRenderer::render(FView const* view)
```

**功能**:
- 检查前置条件（View、Scene、Camera）
- 调用 `renderInternal()` 执行实际渲染

**调用链**:
```
render() 
  -> renderInternal() 
    -> renderJob()
```

### 3. FRenderer::renderInternal() - 内部渲染逻辑

**位置**: `filament/src/details/Renderer.cpp:609`

```cpp
void FRenderer::renderInternal(FView const* view, bool flush)
```

**功能**:
- 创建每帧渲染的 Arena 内存分配器
- 创建根 Job 用于多线程同步
- 调用 `renderJob()` 执行渲染作业
- 刷新命令缓冲区

**关键 API**:
- `engine.getPerRenderPassArena()` - 获取每帧渲染内存池
- `engine.getJobSystem()` - 获取任务系统
- `js.setRootJob()` - 设置根任务
- `engine.flush()` - 刷新命令流

### 4. FRenderer::renderJob() - 核心渲染作业

**位置**: `filament/src/details/Renderer.cpp:637`

这是整个渲染管线的核心函数，包含完整的渲染流程。

#### 4.1 初始化阶段

```cpp
void FRenderer::renderJob(RootArenaScope& rootArenaScope, FView& view)
```

**步骤**:
1. **获取引擎组件引用**
   - `FEngine& engine` - 引擎实例
   - `JobSystem& js` - 任务系统
   - `DriverApi& driver` - 后端驱动 API
   - `PostProcessManager& ppm` - 后处理管理器

2. **重置后处理管理器**
   ```cpp
   ppm.resetForRender();
   ppm.setFrameUniforms(driver, view.getFrameUniforms());
   ```

3. **收集渲染选项**
   - 后处理选项（TAA、FXAA、Bloom、DoF 等）
   - 动态分辨率选项
   - MSAA 选项
   - SSR 选项
   - 立体渲染选项

#### 4.2 View 准备阶段

**位置**: `filament/src/details/Renderer.cpp:845`

```cpp
view.prepare(engine, driver, rootArenaScope, svp, cameraInfo, getShaderUserTime(), needsAlphaChannel);
```

**功能**: 准备视图相关的所有数据
- **视锥剔除**: 多线程剔除不可见物体
- **光照网格化**: 将动态光源分配到网格中
- **阴影准备**: 计算阴影贴图
- **Uniform 缓冲区准备**: 更新所有 UBO

**关键 API**:
- `view.prepare()` - 准备视图（内部调用 `FView::prepare()`）
- `view.prepareLodBias()` - 准备 LOD 偏移
- `view.prepareSSAO()` - 准备 SSAO
- `view.prepareSSR()` - 准备 SSR
- `view.prepareShadowMapping()` - 准备阴影映射
- `view.prepareViewport()` - 准备视口
- `view.commitUniforms()` - 提交 Uniform 数据

#### 4.3 FrameGraph 构建阶段

**位置**: `filament/src/details/Renderer.cpp:830`

```cpp
FrameGraph fg(*mResourceAllocator,
    isProtectedContent ? FrameGraph::Mode::PROTECTED : FrameGraph::Mode::UNPROTECTED);
```

**FrameGraph 架构**:
- **虚拟资源**: 在构建阶段，资源是虚拟的，只有描述符
- **资源分配**: 在 `compile()` 阶段分配实际资源
- **Pass 依赖**: 通过 `read()` 和 `write()` 声明资源依赖
- **自动剔除**: 未使用的 Pass 会被自动剔除

#### 4.4 Shadow Pass - 阴影贴图渲染

**位置**: `filament/src/details/Renderer.cpp:915`

```cpp
if (view.needsShadowMap()) {
    Variant shadowVariant(Variant::DEPTH_VARIANT);
    shadowVariant.setVsm(view.getShadowType() == ShadowType::VSM);
    
    auto shadows = view.renderShadowMaps(engine, fg, cameraInfo, mShaderUserTime,
            RenderPassBuilder{ commandArena }
                .renderFlags(renderFlags)
                .variant(shadowVariant));
    blackboard["shadows"] = shadows;
}
```

**功能**:
- 为每个阴影光源创建阴影贴图
- 使用深度变体（DEPTH_VARIANT）渲染
- 支持级联阴影（CSM）和点光源阴影

**关键 API**:
- `view.renderShadowMaps()` - 渲染阴影贴图
- `FrameGraph::Builder::read()` - 声明资源读取
- `FrameGraph::Builder::write()` - 声明资源写入
- `FrameGraph::Builder::declareRenderPass()` - 声明渲染通道

#### 4.5 Structure Pass - 结构通道（深度预渲染）

**位置**: `filament/src/details/Renderer.cpp:1020`

```cpp
const auto [structure, picking_] = ppm.structure(fg,
        passBuilder, renderFlags, svp.width, svp.height, {
        .scale = aoOptions.resolution,
        .picking = view.hasPicking() && !view.isTransparentPickingEnabled()
});
```

**功能**:
- 渲染深度缓冲区（用于 SSAO 和接触阴影）
- 可选：渲染拾取缓冲区
- 通常以较低分辨率渲染（根据 SSAO 选项）

**关键 API**:
- `ppm.structure()` - 创建结构通道
- `RenderPassBuilder::renderFlags()` - 设置渲染标志
- `RenderPassBuilder::camera()` - 设置相机
- `RenderPassBuilder::geometry()` - 设置几何体

#### 4.6 SSAO Pass - 屏幕空间环境光遮蔽

**位置**: `filament/src/details/Renderer.cpp:1074`

```cpp
if (aoOptions.enabled) {
    auto ssao = ppm.screenSpaceAmbientOcclusion(fg, svp, cameraInfo, structure, aoOptions);
    blackboard["ssao"] = ssao;
}
```

**功能**:
- 使用结构通道的深度信息计算环境光遮蔽
- 输出 AO 纹理到 Blackboard

#### 4.7 SSR Pass - 屏幕空间反射

**位置**: `filament/src/details/Renderer.cpp:1083`

```cpp
if (ssReflectionsOptions.enabled) {
    auto reflections = ppm.ssr(fg, passBuilder,
            view.getFrameHistory(), structure,
            { .width = svp.width, .height = svp.height });
    
    if (UTILS_LIKELY(reflections)) {
        PostProcessManager::generateMipmapSSR(ppm, fg,
                reflections, ssrConfig.reflection, false, ssrConfig);
    }
}
```

**功能**:
- 计算屏幕空间反射
- 生成反射纹理的 Mipmap 链

#### 4.8 Color Pass - 颜色通道（主渲染）

**位置**: `filament/src/details/Renderer.cpp:1096`

这是最复杂的通道，负责渲染场景的主要颜色。

**4.8.1 RenderPass 构建**

```cpp
RenderPassBuilder passBuilder(commandArena);
passBuilder.renderFlags(renderFlags);
passBuilder.camera(cameraInfo.getPosition(), cameraInfo.getForwardVector());
passBuilder.geometry(scene.getRenderableData(), view.getVisibleRenderables());
passBuilder.variant(variant);
passBuilder.colorPassDescriptorSet(&view.getColorPassDescriptorSet());
passBuilder.commandTypeFlags(RenderPass::CommandTypeFlags::COLOR);

RenderPass const pass{ passBuilder.build(engine, driver) };
```

**功能**:
- 构建渲染命令列表
- 根据材质、变体、距离等排序命令
- 支持自动实例化

**关键 API**:
- `RenderPassBuilder::build()` - 构建 RenderPass（内部调用 `RenderPass::RenderPass()`）
- `RenderPass::appendCommands()` - 生成渲染命令
- `RenderPass::sortCommands()` - 排序命令
- `RenderPass::instanceify()` - 自动实例化

**4.8.2 命令生成流程**

**位置**: `filament/src/RenderPass.cpp:184`

```cpp
void RenderPass::appendCommands(FEngine const& engine,
        Slice<Command> commands,
        Range<uint32_t> const visibleRenderables,
        CommandTypeFlags const commandTypeFlags,
        ...)
```

**流程**:
1. **多线程生成命令**: 使用 JobSystem 并行处理可见物体
2. **命令生成**: 为每个图元生成渲染命令
   - 计算排序键（材质、距离、混合模式等）
   - 设置渲染状态（光栅化、混合、深度测试等）
   - 设置几何信息（顶点缓冲区、索引缓冲区等）
3. **准备程序**: 确保着色器程序已编译

**4.8.3 命令排序**

**位置**: `filament/src/RenderPass.cpp:271`

```cpp
RenderPass::Command* RenderPass::sortCommands(
        Command* const begin, Command* const end) noexcept
```

**排序策略**:
- **不透明物体**: 按 Z-Bucket 和材质排序（前到后）
- **透明物体**: 按距离排序（后到前）
- **混合模式**: 按混合顺序排序

**4.8.4 自动实例化**

**位置**: `filament/src/RenderPass.cpp:286`

```cpp
RenderPass::Command* RenderPass::instanceify(DriverApi& driver,
        DescriptorSetLayoutHandle perRenderableDescriptorSetLayoutHandle,
        Command* curr, Command* const last,
        int32_t const eyeCount) const noexcept
```

**功能**:
- 检测使用相同材质和几何体的重复绘制
- 合并为实例化绘制调用
- 创建实例化 UBO 存储每个实例的数据

**4.8.5 Color Pass 执行**

**位置**: `filament/src/details/Renderer.cpp:1216`

```cpp
auto colorPassOutput = RendererUtils::colorPass(fg, "Color Pass", mEngine, view, {
            .shadows = blackboard.get<FrameGraphTexture>("shadows"),
            .ssao = blackboard.get<FrameGraphTexture>("ssao"),
            .ssr = ssrConfig.ssr,
            .structure = structure
        },
        colorBufferDesc, config, colorGradingConfigForColor, pass.getExecutor());
```

**功能**:
- 创建颜色通道的 FrameGraph Pass
- 绑定所有输入资源（阴影、SSAO、SSR、结构）
- 执行 RenderPass 的命令列表

**关键 API**:
- `RendererUtils::colorPass()` - 创建颜色通道
- `RenderPass::Executor::execute()` - 执行渲染命令

**4.8.6 命令执行流程**

**位置**: `filament/src/RenderPass.cpp:920`

```cpp
void RenderPass::Executor::execute(FEngine const& engine, DriverApi& driver,
        Command const* first, Command const* last) const noexcept
```

**执行流程**:
1. **批次处理**: 将命令分批处理，避免命令缓冲区溢出
2. **状态管理**: 跟踪当前管线状态，只在变化时更新
   - Pipeline State（程序、光栅化状态、混合状态等）
   - Render Primitive（几何体）
   - Descriptor Sets（Uniform 缓冲区、纹理等）
3. **绘制调用**: 调用 `driver.draw2()` 执行实际绘制

**关键 API**:
- `driver.bindPipeline()` - 绑定管线状态
- `driver.bindRenderPrimitive()` - 绑定几何体
- `driver.bindDescriptorSet()` - 绑定描述符集
- `driver.draw2()` - 执行绘制

#### 4.9 Post-Processing Passes - 后处理通道

**位置**: `filament/src/details/Renderer.cpp:1317`

后处理按顺序执行：

**4.9.1 TAA (Temporal Anti-Aliasing)**

```cpp
if (taaOptions.enabled) {
    input = ppm.taa(fg, input, depth, view.getFrameHistory(), 
            &FrameHistoryEntry::taa, taaOptions, colorGradingConfig);
}
```

**功能**: 时间抗锯齿，使用历史帧信息减少锯齿

**4.9.2 DoF (Depth of Field)**

```cpp
if (dofOptions.enabled) {
    input = ppm.dof(fg, input, depth, cameraInfo, needsAlphaChannel,
            bokehScale, dofOptions);
}
```

**功能**: 景深效果

**4.9.3 Bloom**

```cpp
if (bloomOptions.enabled) {
    auto [bloom_, flare_] = ppm.bloom(fg, input, TextureFormat::R11F_G11F_B10F,
            bloomOptions, taaOptions, scale);
    bloom = bloom_;
    flare = flare_;
}
```

**功能**: 泛光和光晕效果

**4.9.4 Color Grading**

```cpp
if (hasColorGrading) {
    if (!colorGradingConfig.asSubpass) {
        input = ppm.colorGrading(fg, input, xvp,
                bloom, flare,
                colorGrading, colorGradingConfig,
                bloomOptions, vignetteOptions);
    }
}
```

**功能**: 颜色分级和色调映射

**4.9.5 FXAA**

```cpp
if (hasFXAA) {
    input = ppm.fxaa(fg, input, xvp, colorGradingConfig.ldrFormat, preserveAlphaChannel);
}
```

**功能**: 快速近似抗锯齿

**4.9.6 Upscaling**

```cpp
if (scaled) {
    input = ppm.upscale(fg, needsAlphaChannel, sourceHasLuminance, dsrOptions, 
            input, xvp, {
                .width = viewport.width, .height = viewport.height,
                .format = colorGradingConfig.ldrFormat }, 
            SamplerMagFilter::LINEAR);
}
```

**功能**: 动态分辨率上采样

#### 4.10 FrameGraph 编译和执行

**位置**: `filament/src/details/Renderer.cpp:1475`

```cpp
fg.forwardResource(fgViewRenderTarget, input);
fg.present(fgViewRenderTarget);
fg.compile();
fg.execute(driver);
```

**4.10.1 compile() - 编译 FrameGraph**

**位置**: `filament/src/fg/FrameGraph.cpp:122`

```cpp
FrameGraph& FrameGraph::compile() noexcept
```

**功能**:
1. **剔除未使用的 Pass**: 通过依赖图分析，移除未被引用的 Pass
2. **计算资源生命周期**: 确定每个资源的首次使用和最后使用
3. **分配资源**: 为虚拟资源分配实际的 GPU 资源
4. **解析使用标志**: 确定每个资源的使用方式（采样、附件等）

**关键 API**:
- `dependencyGraph.cull()` - 剔除未使用的节点
- `resource->devirtualize()` - 将虚拟资源转换为实际资源
- `resource->destroy()` - 标记资源销毁时机

**4.10.2 execute() - 执行 FrameGraph**

**位置**: `filament/src/fg/FrameGraph.cpp:199`

```cpp
void FrameGraph::execute(backend::DriverApi& driver) noexcept
```

**执行流程**:
1. **遍历活动 Pass**: 按依赖顺序执行每个 Pass
2. **资源去虚拟化**: 在 Pass 首次使用资源时分配
3. **执行 Pass**: 调用 Pass 的 execute 函数
4. **资源销毁**: 在 Pass 最后使用资源后销毁

**关键 API**:
- `driver.pushGroupMarker()` - 开始调试组
- `resource->devirtualize()` - 分配资源
- `node->execute()` - 执行 Pass
- `resource->destroy()` - 销毁资源
- `driver.popGroupMarker()` - 结束调试组

### 5. FRenderer::endFrame() - 帧结束

**位置**: `filament/src/details/Renderer.cpp:423`

```cpp
void FRenderer::endFrame()
```

**功能**:
- 提交 SwapChain（呈现帧）
- 更新帧信息管理器
- 提交帧到后端
- 执行垃圾回收
- 停止帧捕获（如果启用）

**关键 API**:
- `mSwapChain->commit(driver)` - 提交交换链
- `engine.submitFrame()` - 提交帧
- `driver.endFrame()` - 后端帧结束
- `driver.tick()` - 执行后端周期性任务
- `mResourceAllocator->gc()` - 资源垃圾回收
- `engine.flush()` - 刷新命令流

## FrameGraph 资源管理

### 资源声明

```cpp
// 创建虚拟纹理资源
auto color = fg.createTexture("Color Buffer", {
    .width = 1920,
    .height = 1080,
    .format = TextureFormat::RGBA16F
});

// 声明读取
color = builder.read(color, FrameGraphTexture::Usage::SAMPLEABLE);

// 声明写入
color = builder.write(color, FrameGraphTexture::Usage::COLOR_ATTACHMENT);

// 声明渲染通道
builder.declareRenderPass("Color Pass", {
    .attachments = { .color = { color }}
});
```

### 资源转发

```cpp
// 将中间资源转发到最终目标
fg.forwardResource(fgViewRenderTarget, input);
```

**功能**: 将中间缓冲区的资源直接转发到最终渲染目标，避免不必要的拷贝。

### 资源导入

```cpp
// 导入外部资源（如 SwapChain）
FrameGraphId<FrameGraphTexture> fgViewRenderTarget = fg.import("viewRenderTarget", {
        .attachments = attachmentMask,
        .viewport = svp,
        .clearColor = clearColor,
        .samples = 0,
        .clearFlags = clearFlags,
        .keepOverrideStart = keepOverrideStartFlags,
        .keepOverrideEnd = keepOverrideEndFlags
    }, viewRenderTarget);
```

## RenderPass 命令生成

### 命令结构

```cpp
struct Command {
    uint64_t key;           // 排序键
    PrimitiveInfo info;     // 图元信息
};
```

**排序键组成**:
- Pass 类型（COLOR、DEPTH、BLENDED 等）
- 通道（Channel）
- 优先级（Priority）
- Z-Bucket（深度桶）
- 材质变体（Material Variant）
- 混合顺序（Blend Order）
- 距离（Distance）

### 命令生成流程

1. **遍历可见物体**: 多线程处理可见渲染对象
2. **生成命令**: 为每个图元创建命令
   - 设置排序键
   - 设置渲染状态
   - 设置几何信息
3. **排序**: 按排序键排序命令
4. **实例化**: 合并相同绘制调用

## 多线程架构

### JobSystem 使用

```cpp
JobSystem& js = engine.getJobSystem();
auto* rootJob = js.setRootJob(js.createJob());

// 并行生成命令
auto* jobCommandsParallel = parallel_for(js, nullptr,
        visibleRenderables.first, uint32_t(visibleRenderables.size()),
        std::cref(work), jobs::CountSplitter<JOBS_PARALLEL_FOR_COMMANDS_COUNT>());
js.runAndWait(jobCommandsParallel);
```

**并行化点**:
- 视锥剔除
- 光照剔除
- 命令生成
- 光照网格化（Froxelization）

## 性能优化技术

### 1. 自动实例化

将使用相同材质和几何体的多个绘制调用合并为实例化绘制。

### 2. 命令排序

- 不透明物体：前到后排序（利用深度测试早期剔除）
- 透明物体：后到前排序（正确混合）

### 3. 状态缓存

跟踪当前管线状态，只在变化时更新，减少状态切换开销。

### 4. 资源生命周期管理

FrameGraph 自动管理资源分配和释放，避免不必要的资源创建。

### 5. 动态分辨率

根据帧时间动态调整渲染分辨率，保持目标帧率。

## 关键数据结构

### FrameGraph

- `mPassNodes` - Pass 节点列表
- `mResourceNodes` - 资源节点列表
- `mResources` - 虚拟资源列表
- `mGraph` - 依赖图

### RenderPass

- `mCommandBegin` - 命令列表开始
- `mCommandEnd` - 命令列表结束
- `mCustomCommands` - 自定义命令列表

### View

- `mVisibleRenderables` - 可见渲染对象范围
- `mColorPassDescriptorSet` - 颜色通道描述符集
- `mUniforms` - Uniform 缓冲区

## 总结

Filament 的渲染管线采用声明式 FrameGraph 架构，通过资源依赖关系自动管理渲染顺序和资源生命周期。主要流程包括：

1. **准备阶段**: 视锥剔除、光照准备、阴影准备
2. **FrameGraph 构建**: 声明所有 Pass 和资源依赖
3. **FrameGraph 编译**: 分配资源、剔除未使用 Pass
4. **FrameGraph 执行**: 按顺序执行所有 Pass
5. **后处理**: 应用各种后处理效果
6. **呈现**: 提交到 SwapChain

这种架构的优势：
- **自动优化**: 自动剔除未使用的 Pass
- **资源管理**: 自动管理资源生命周期
- **可扩展性**: 易于添加新的渲染 Pass
- **可调试性**: 清晰的依赖关系便于调试

