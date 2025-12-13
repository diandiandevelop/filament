# Filament 光照与阴影系统详细分析

## 目录
- 概览
- 核心组件
  - LightManager（光源管理）
  - Froxelizer（分块光照）
  - ShadowMapManager（阴影图管理）
  - View 准备阶段（光照 & 阴影 UBO/Descriptor）
- 渲染流程中的位置
- 关键数据流与资源
- 性能与优化要点

---

## 概览
Filament 的光照与阴影系统围绕三类核心：**光源管理（LightManager）**、**分块光照（Froxelizer）**、**阴影图管理（ShadowMapManager）**。View 在 `prepare*` 阶段串起光照/阴影数据更新，并把结果提交到 per-view/per-renderable 的 UBO 与描述符堆中，为后续 FrameGraph 渲染阶段提供输入。

---

## 核心组件

### LightManager：光源组件管理
- 类型：Directional / Point / Spot（含 Focused Spot），方向光仅支持 1 盏。
- 组件数据：位置/方向、强度、衰减、聚光锥、是否投影阴影、阴影参数（分辨率、层级、VSM 设置等）。
- 生命周期：Builder 创建 → attach 到 Entity → destroy；支持遍历实例、批量 prepare。
- `FScene::prepareDynamicLights()` 会在 View 准备阶段裁剪光源并填充 UBO。

### Froxelizer：屏幕空间分块光照
文件：`filament/src/Froxelizer.h/.cpp`
- 目标：为前向渲染构建 **Froxels**（view 空间分块）并为每个 froxel 写入活跃光源列表，以便片段着色器快速查询。
- 布局计算：`computeFroxelLayout()` 根据 viewport 与缓冲预算（4096 froxel entries 默认）求出 X/Y/Z 切片数，保证方形且 8 对齐。
- 数据：
  - Froxel buffer：每个 froxel 的光列表起始 offset + 计数。
  - Record buffer：紧凑的光索引列表，uint8/uint4 打包。
  - 绑定到 GPU：`commit()` 把 froxel/record buffer 上传；`updateUniforms()` 写 zParams、froxelCountXY 等到 PerViewUib。
- 算法：
  - 根据 near/far 或用户 zLightNear/zLightFar 生成对数分布的 Z 切片。
  - 计算切片平面 → 计算 bounding sphere → 用光源包围球与 froxel 相交测试填表。
  - 对光源构建 z-range 二叉树（light tree）以加速裁剪。
- 在 View 中：`prepare()` 分配 per-frame 缓存，`froxelizeLights()` 在 JobSystem 并行执行，输出写入 Frame UBO + SSBO。

### ShadowMapManager：阴影图管理
文件：`filament/src/ShadowMapManager.h/.cpp`
- 负责：阴影图列表构建、剔除、级联/点光/聚光的相机与视口计算，阴影图 atlas 尺寸计算，生成 FrameGraph 阴影 pass。
- Builder：
  - `directionalShadowMap(light, options)`：基于 cascades 生成多个 ShadowMap（CSM）。
  - `shadowMap(light, spotlight)`：spotlight 生成 1 张；point 生成 6 张立方体面。
- `update()`：
  - 延迟初始化 shadowmap 缓存和 UBO。
  - 计算 atlas 需求（size/layers/levels/MSAA/format）。
  - `updateCascadeShadowMaps()`：方向光 CSM，写入 split、可见性、矩阵。
  - `updateSpotShadowMaps()`：聚光/点光阴影，写可见遮挡/层分配。
- `render()`：
  - FrameGraph pass，创建阴影图 atlas 纹理（2D array）。
  - 构造每个 ShadowPass 的 RenderPass::Executor；VSM 需要清除 moment 纹理并可选模糊。
  - 按层排序（可选 FeatureShadowAllocator）以批处理同层渲染。
  - execute 阶段绑定 shadowMap、覆盖 scissor，执行绘制，最后解绑描述符堆避免依赖串联。
- 输出：
  - Shadow atlas 纹理 FrameGraph 句柄。
  - ShadowMappingUniforms：cascade splits、屏幕空间接触阴影距离、阴影数量等。

### View 准备阶段（光照 & 阴影）
文件：`filament/src/details/View.cpp`
- `prepareShadowing()`：
  - 构建 ShadowMapManager::Builder（根据可见光源/阴影选项）。
  - 触发 ShadowMapManager::update()（剔除、atlas 需求、CSM/spot/point 阴影相机）。
  - 记录是否需要 shadow map / directional shadow。
- `prepareLighting()`：
  - `prepareDynamicLights()`：把可见动态光写入 UBO。
  - 曝光：根据 ev100 写曝光参数。
  - IBL：准备环境光（无 IBL 时用 1x1 black IBL + skybox intensity）。
  - Directional：索引 0 的方向光数据写入 DescriptorSet/UBO。
- 后续 `prepareShadowMapping()`、`prepareSSAO/SSR` 等调用会把阴影/屏幕空间效果绑定到描述符堆。

---

## 渲染流程中的位置
1) Renderer::renderInternal()
2) View::prepare()
   - scene.prepare (剔除) + froxelize job
   - prepareShadowing() → ShadowMapManager::update()
   - prepareLighting() → 动态光/IBL/方向光 UBO
3) FrameGraph 构建
   - ShadowMapManager::render() 生成阴影 atlas pass
   - SSAO/SSR/PostProcess 等
4) 颜色通道渲染
   - 片段着色器使用 froxel 光列表 + 阴影 atlas 采样实现逐片段光照与阴影

---

## 关键数据流与资源
- PerView UBO：zParams（froxel Z 切片映射）、froxelCountXY、shadow/cascade splits、光照常量。
- Froxel buffers：froxel entries（offset/count）、record buffer（光索引）。
- Shadow atlas：2D array，包含 CSM/spot/point 阴影；VSM 可选模糊。
- DescriptorSets：
  - PER_VIEW：froxel/record buffer、shadow atlas、IBL、方向光、曝光等。
  - PER_RENDERABLE：网格/材质数据（用于阴影绘制）。

---

## 性能与优化要点
- Froxel 预算：默认 4096 entry；分辨率下降/上升会自动调整 XY 切片尺寸，保持 8 对齐加速 shader。
- 深度变体共享：默认材质深度变体共享减少编译与内存（同 ShadowMapManager 的 atlas 共享层分配）。
- 阴影图层排序：可选 FeatureShadowAllocator 按 layer 排序降低 render pass 次数。
- 剔除：
  - 光源裁剪到 CONFIG_MAX_LIGHT_COUNT。
  - Froxel 仅记录影响的光源；Spot/Point 阴影做可见性剔除。
  - CSM 依据可见层与 split 范围裁剪 renderables。
- VSM 清除/模糊：VSM 需要特定 clear 值，模糊 pass 仅在需要时启用。

---

## 摘要
Filament 的光照/阴影系统通过：
- LightManager 管理光源参数与阴影设置。
- Froxelizer 把屏幕分块，生成每块活跃光列表以支撑高并发前向光照。
- ShadowMapManager 统一管理 CSM/Spot/Point 阴影图的构建、剔除、atlas 分配与渲染。
- View 准备阶段把上述数据写入 UBO/描述符，FrameGraph 组织阴影/颜色 pass。
这一组合在保证可扩展的多光源前向渲染同时，兼顾了阴影质量与运行时性能。 

