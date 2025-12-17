# Filament View 架构设计分析

## 目录
1. [概述](#概述)
2. [View 的职责与定位](#view-的职责与定位)
3. [View 架构组件](#view-架构组件)
4. [View 核心数据结构](#view-核心数据结构)
5. [View 渲染流程](#view-渲染流程)
6. [View 与其他组件的关系](#view-与其他组件的关系)
7. [View::prepare() 详解](#viewprepare-详解)
8. [View 的高级特性](#view-的高级特性)

---

## 概述

**View** 是 Filament 渲染系统的核心组件之一，它封装了渲染一个场景所需的所有状态和配置。View 是连接应用程序和渲染管线的桥梁，负责准备渲染数据、管理渲染资源、协调渲染通道。

### 核心特点

- **重量级对象**：View 实例内部缓存大量渲染所需的数据，不建议创建过多实例
- **独立配置**：每个 View 可以独立配置渲染参数（抗锯齿、动态分辨率、阴影等）
- **多用途**：可用于主场景渲染、UI 渲染、特殊效果渲染等
- **状态管理**：管理相机、场景、视口、渲染选项等所有渲染状态

---

## View 的职责与定位

### 在渲染管线中的位置

```mermaid
graph TB
    subgraph "应用程序层"
        A[应用程序]
    end
    
    subgraph "Filament 核心层"
        B[Engine<br/>引擎]
        C[Renderer<br/>渲染器]
        D[View<br/>视图]
        E[Scene<br/>场景]
        F[Camera<br/>相机]
    end
    
    subgraph "渲染管线层"
        G[FrameGraph<br/>帧图]
        H[Shadow Pass<br/>阴影通道]
        I[Color Pass<br/>颜色通道]
        J[Post-Process<br/>后处理]
    end
    
    A --> B
    B --> C
    C --> D
    D --> E
    D --> F
    D --> G
    G --> H
    G --> I
    G --> J
```

### View 的核心职责

1. **渲染状态管理**
   - 管理相机（Culling Camera 和 Viewing Camera）
   - 管理场景引用
   - 管理视口（Viewport）
   - 管理渲染目标（RenderTarget）

2. **渲染数据准备**
   - 视锥剔除（Frustum Culling）
   - 光源筛选和排序
   - 阴影投射者收集
   - Uniform 缓冲区准备

3. **渲染选项配置**
   - 抗锯齿（FXAA、TAA、MSAA）
   - 动态分辨率
   - 阴影类型（PCF、VSM、DPCF、PCSS）
   - 后处理效果（Bloom、DOF、SSAO、SSR等）

4. **资源管理**
   - 描述符集（Descriptor Set）管理
   - Uniform 缓冲区管理
   - 帧历史管理（用于 TAA、SSR）

---

## View 架构组件

### View 类层次结构

```mermaid
graph TB
    subgraph "公共接口层"
        A[View<br/>公共接口]
    end
    
    subgraph "实现层"
        B[FView<br/>实现类]
    end
    
    subgraph "核心组件"
        C[Froxelizer<br/>光照网格化器]
        D[ShadowMapManager<br/>阴影贴图管理器]
        E[ColorPassDescriptorSet<br/>颜色通道描述符集]
        F[FrameHistory<br/>帧历史]
        G[PIDController<br/>动态分辨率控制器]
    end
    
    A --> B
    B --> C
    B --> D
    B --> E
    B --> F
    B --> G
```

### View 内部组件架构

```mermaid
graph TB
    subgraph "FView 核心组件"
        V1[Scene*<br/>场景引用]
        V2[Camera*<br/>相机引用]
        V3[Viewport<br/>视口]
        V4[RenderTarget*<br/>渲染目标]
        
        V5[Froxelizer<br/>光照网格化]
        V6[ShadowMapManager<br/>阴影管理器]
        V7[ColorPassDescriptorSet<br/>描述符集]
        V8[FrameHistory<br/>帧历史]
        V9[TypedUniformBuffer<br/>Uniform缓冲区]
        V10[PIDController<br/>动态分辨率控制]
        
        V11[Visible Ranges<br/>可见对象范围]
        V12[Render Options<br/>渲染选项]
    end
    
    V1 --> V5
    V2 --> V5
    V3 --> V5
    V5 --> V6
    V6 --> V7
    V7 --> V9
    V8 --> V7
    V10 --> V3
    V11 --> V7
    V12 --> V7
```

### 组件详细说明

#### 1. Froxelizer（光照网格化器）

**职责**：将3D空间划分为Froxel网格，用于高效的光照计算

```mermaid
graph LR
    A[光源数据] --> B[Froxelizer]
    B --> C[3D网格划分]
    C --> D[光源分配到Froxel]
    D --> E[生成Light Records]
    E --> F[提交到GPU]
```

**关键特性**：
- 使用3D网格（通常16x16x16或32x32x16）划分视锥空间
- 每个Froxel包含影响该区域的光源列表
- 支持动态光源的高效查询

#### 2. ShadowMapManager（阴影贴图管理器）

**职责**：管理所有阴影贴图的生成和更新

```mermaid
graph TB
    A[ShadowMapManager] --> B[Directional Shadows<br/>方向光阴影]
    A --> C[Spot Shadows<br/>聚光灯阴影]
    A --> D[Point Shadows<br/>点光源阴影]
    
    B --> E[CSM<br/>级联阴影贴图]
    C --> F[Spot Shadow Maps]
    D --> G[Cube Shadow Maps]
```

**支持的阴影类型**：
- **PCF**：Percentage Closer Filtering（默认）
- **VSM**：Variance Shadow Maps
- **DPCF**：Denoised Percentage Closer Filtering
- **PCSS**：Percentage Closer Soft Shadows

#### 3. ColorPassDescriptorSet（颜色通道描述符集）

**职责**：管理颜色通道所需的所有GPU资源绑定

**包含的资源**：
- 相机Uniform（投影矩阵、视图矩阵等）
- 光源Uniform（方向光、动态光源）
- 阴影Uniform（阴影贴图、级联参数）
- 环境光Uniform（IBL、曝光）
- 纹理资源（阴影贴图、SSAO、SSR等）

#### 4. FrameHistory（帧历史）

**职责**：存储前一帧的渲染结果，用于时间性效果

**用途**：
- **TAA**（Temporal Anti-Aliasing）：时间抗锯齿
- **SSR**（Screen Space Reflections）：屏幕空间反射
- **动态分辨率**：历史帧用于上采样

---

## View 核心数据结构

### FView 主要成员变量

```cpp
class FView : public View {
private:
    // 场景和相机
    FScene* mScene = nullptr;
    FCamera* mCullingCamera = nullptr;      // 用于剔除的相机
    FCamera* mViewingCamera = nullptr;       // 用于渲染的相机（可选）
    
    // 视口和渲染目标
    Viewport mViewport;
    FRenderTarget* mRenderTarget = nullptr;
    
    // 核心组件
    Froxelizer mFroxelizer;                  // 光照网格化器
    std::unique_ptr<ShadowMapManager> mShadowMapManager;  // 阴影管理器
    TypedUniformBuffer<PerViewUib> mUniforms;  // Uniform缓冲区
    ColorPassDescriptorSet mColorPassDescriptorSet[2];  // 描述符集（两个变体）
    FrameHistory mFrameHistory;              // 帧历史
    
    // 可见性数据（由prepare()设置）
    Range mVisibleRenderables;               // 可见可渲染对象范围
    Range mVisibleDirectionalShadowCasters;  // 可见方向光阴影投射者
    Range mSpotLightShadowCasters;           // 聚光灯阴影投射者
    
    // 渲染选项
    AntiAliasing mAntiAliasing = AntiAliasing::FXAA;
    ShadowType mShadowType = ShadowType::PCF;
    BloomOptions mBloomOptions;
    FogOptions mFogOptions;
    AmbientOcclusionOptions mAmbientOcclusionOptions;
    TemporalAntiAliasingOptions mTemporalAntiAliasingOptions;
    // ... 更多选项
    
    // 动态分辨率
    PIDController mPidController;           // PID控制器
    DynamicResolutionOptions mDynamicResolution;
    math::float2 mScale = 1.0f;              // 当前缩放比例
    
    // 状态标志
    bool mHasDirectionalLighting = false;
    bool mHasDynamicLighting = false;
    bool mHasShadowing = false;
    bool mNeedsShadowMap = false;
    bool mHasPostProcessPass = true;
};
```

### 数据结构关系图

```mermaid
graph TB
    subgraph "FView 数据结构"
        A[FView]
        
        subgraph "引用数据"
            B[Scene*]
            C[Camera*]
            D[RenderTarget*]
        end
        
        subgraph "核心组件"
            E[Froxelizer]
            F[ShadowMapManager]
            G[ColorPassDescriptorSet]
            H[FrameHistory]
        end
        
        subgraph "可见性数据"
            I[VisibleRenderables Range]
            J[ShadowCasters Ranges]
        end
        
        subgraph "渲染选项"
            K[AntiAliasing]
            L[ShadowType]
            M[PostProcessOptions]
        end
        
        A --> B
        A --> C
        A --> D
        A --> E
        A --> F
        A --> G
        A --> H
        A --> I
        A --> J
        A --> K
        A --> L
        A --> M
    end
```

---

## View 渲染流程

### 完整渲染流程图

```mermaid
sequenceDiagram
    participant App as 应用程序
    participant Renderer as Renderer
    participant View as View
    participant Scene as Scene
    participant FG as FrameGraph
    
    App->>Renderer: beginFrame(swapChain)
    Renderer->>Renderer: 准备渲染上下文
    
    App->>Renderer: render(view)
    Renderer->>View: prepare()
    
    View->>Scene: prepare()
    Scene->>Scene: 收集Entity数据
    Scene->>Scene: 填充SoA结构
    
    View->>View: 计算相机信息
    View->>View: 视锥剔除（多线程）
    View->>View: 光源筛选和排序
    View->>View: Froxelize光源
    View->>View: 准备阴影
    View->>View: 准备光照
    View->>View: 更新Uniform缓冲区
    
    Renderer->>FG: 构建FrameGraph
    FG->>FG: Shadow Pass
    FG->>FG: Depth Pre-Pass
    FG->>FG: Color Pass
    FG->>FG: Post-Process Pass
    FG->>FG: 编译和执行
    
    Renderer->>Renderer: endFrame()
    Renderer->>App: 完成渲染
```

### View::prepare() 详细流程

```mermaid
graph TD
    A[View::prepare开始] --> B[计算相机信息<br/>computeCameraInfo]
    B --> C[准备场景<br/>Scene::prepare]
    C --> D[计算剔除视锥<br/>getFrustum]
    
    D --> E{有位置光源?}
    E -->|是| F[并行: 准备可见光源<br/>prepareVisibleLights]
    E -->|否| G[跳过光源准备]
    
    F --> H[并行: 视锥剔除<br/>prepareVisibleRenderables]
    G --> H
    
    H --> I[等待光源准备完成]
    I --> J{有动态光照?}
    J -->|是| K[Froxelize光源<br/>froxelizeLights]
    J -->|否| L[跳过Froxelization]
    
    K --> M[准备阴影<br/>prepareShadowing]
    L --> M
    
    M --> N[分区可渲染对象<br/>Partition Renderables]
    N --> O[更新UBO<br/>updateUBOs]
    O --> P[准备光照<br/>prepareLighting]
    P --> Q[准备相机Uniform<br/>prepareCamera]
    Q --> R[准备其他Uniform<br/>prepareTime/Fog/SSAO等]
    R --> S[提交Uniform和描述符集]
    S --> T[prepare完成]
```

---

## View 与其他组件的关系

### View 与核心组件的关系图

```mermaid
graph TB
    subgraph "View 依赖关系"
        V[View]
        
        subgraph "必需组件"
            S[Scene<br/>场景]
            C[Camera<br/>相机]
        end
        
        subgraph "可选组件"
            RT[RenderTarget<br/>渲染目标]
            CG[ColorGrading<br/>色彩分级]
        end
        
        subgraph "内部管理组件"
            FZ[Froxelizer<br/>光照网格化]
            SM[ShadowMapManager<br/>阴影管理]
            DS[DescriptorSet<br/>描述符集]
            FH[FrameHistory<br/>帧历史]
        end
        
        subgraph "外部系统"
            E[Engine<br/>引擎]
            R[Renderer<br/>渲染器]
            FG[FrameGraph<br/>帧图]
        end
    end
    
    V --> S
    V --> C
    V -.-> RT
    V -.-> CG
    V --> FZ
    V --> SM
    V --> DS
    V --> FH
    V --> E
    R --> V
    V --> FG
```

### View 与 Scene 的交互

```mermaid
sequenceDiagram
    participant View
    participant Scene
    participant RenderableData as RenderableSoA
    participant LightData as LightSoA
    
    View->>Scene: prepare(worldTransform)
    Scene->>RenderableData: 填充可渲染对象数据
    Scene->>LightData: 填充光源数据
    
    View->>RenderableData: 视锥剔除
    View->>RenderableData: 分区（可见/阴影投射者）
    
    View->>LightData: 光源筛选和排序
    View->>LightData: Froxelize分配
```

### View 与 Renderer 的交互

```mermaid
sequenceDiagram
    participant Renderer
    participant View
    participant FrameGraph
    
    Renderer->>View: prepare(viewport, cameraInfo)
    View->>View: 准备所有渲染数据
    
    Renderer->>FrameGraph: 构建渲染通道
    FrameGraph->>View: 查询可见对象
    FrameGraph->>View: 查询阴影数据
    FrameGraph->>View: 查询光照数据
    
    FrameGraph->>FrameGraph: 执行渲染
    FrameGraph->>Renderer: 完成渲染
```

---

## View::prepare() 详解

### prepare() 方法签名

```cpp
void FView::prepare(FEngine& engine, backend::DriverApi& driver, 
                    RootArenaScope& rootArenaScope,
                    Viewport viewport, CameraInfo cameraInfo,
                    math::float4 const& userTime, 
                    bool needsAlphaChannel) noexcept
```

### prepare() 执行步骤详解

#### 1. 计算相机信息

```cpp
CameraInfo cameraInfo = computeCameraInfo(engine);
```

**功能**：
- 计算投影矩阵和视图矩阵
- 应用世界原点变换（提高大场景浮点精度）
- 处理IBL旋转

#### 2. 准备场景

```cpp
scene->prepare(js, rootArenaScope, cameraInfo.worldTransform, hasVSM());
```

**功能**：
- 收集所有Entity的渲染数据
- 填充RenderableSoA和LightSoA
- 应用世界变换到所有对象

#### 3. 视锥剔除

```mermaid
graph LR
    A[场景对象] --> B[视锥剔除器]
    B --> C{在视锥内?}
    C -->|是| D[标记为可见]
    C -->|否| E[标记为不可见]
    D --> F[添加到可见列表]
```

**并行执行**：
- 可渲染对象剔除
- 光源剔除
- 使用JobSystem多线程加速

#### 4. 光源处理

```mermaid
graph TB
    A[可见光源列表] --> B[按距离排序]
    B --> C[限制到CONFIG_MAX_LIGHT_COUNT]
    C --> D{有动态光源?}
    D -->|是| E[Froxelize分配]
    D -->|否| F[跳过]
    E --> G[生成Light Records]
    G --> H[提交到GPU]
```

#### 5. 阴影准备

```mermaid
graph TB
    A[准备阴影] --> B{阴影启用?}
    B -->|否| C[跳过]
    B -->|是| D[收集阴影投射光源]
    D --> E[方向光阴影<br/>CSM]
    D --> F[聚光灯阴影]
    D --> G[点光源阴影]
    E --> H[ShadowMapManager]
    F --> H
    G --> H
    H --> I[更新阴影贴图]
```

#### 6. 可渲染对象分区

```mermaid
graph TB
    A[所有可渲染对象] --> B[分区操作]
    B --> C[组1: 可见主相机]
    B --> D[组2: 可见+方向光阴影]
    B --> E[组3: 仅方向光阴影]
    B --> F[组4: 潜在点光源阴影]
    B --> G[组5: 完全不可见]
```

**分区目的**：
- 优化渲染顺序
- 减少不必要的渲染调用
- 提高缓存局部性

#### 7. Uniform缓冲区更新

```mermaid
graph LR
    A[可见对象数据] --> B[计算PerRenderableUib]
    B --> C[更新RenderableUBO]
    C --> D[提交到GPU]
    
    E[相机数据] --> F[计算PerViewUib]
    F --> G[更新ViewUBO]
    G --> D
    
    H[光源数据] --> I[计算LightsUib]
    I --> J[更新LightUBO]
    J --> D
```

---

## View 的高级特性

### 1. 动态分辨率（Dynamic Resolution）

```mermaid
graph TB
    A[帧时间测量] --> B[PID控制器]
    B --> C[计算缩放比例]
    C --> D{缩放类型}
    D -->|均匀缩放| E[uniform scale]
    D -->|非均匀缩放| F[主轴优先缩放]
    E --> G[应用缩放]
    F --> G
    G --> H[渲染到缩放视口]
```

**特性**：
- 使用PID控制器根据帧时间自动调整分辨率
- 支持均匀和非均匀缩放
- 限制在minScale和maxScale之间

### 2. 时间抗锯齿（TAA）

```mermaid
graph LR
    A[当前帧] --> B[与历史帧混合]
    B --> C[TAA输出]
    C --> D[保存到FrameHistory]
    D --> E[下一帧使用]
```

**流程**：
1. 当前帧渲染
2. 与前一帧混合（使用运动向量）
3. 输出结果
4. 保存到FrameHistory供下一帧使用

### 3. 屏幕空间反射（SSR）

```mermaid
graph TB
    A[深度缓冲区] --> B[结构纹理]
    B --> C[SSR计算]
    C --> D[反射结果]
    D --> E[与颜色混合]
    E --> F[保存到FrameHistory]
```

### 4. 立体渲染（Stereoscopic Rendering）

```mermaid
graph TB
    A[立体渲染启用] --> B[分割视口]
    B --> C[左眼渲染]
    B --> D[右眼渲染]
    C --> E[合并输出]
    D --> E
```

**限制**：
- 不支持后处理
- 不支持阴影
- 不支持点光源

### 5. 拾取查询（Picking Query）

```mermaid
sequenceDiagram
    participant App
    participant View
    participant Driver
    participant GPU
    
    App->>View: pick(x, y, callback)
    View->>View: 添加到查询列表
    
    View->>Driver: 渲染时执行查询
    Driver->>GPU: 读取像素数据
    GPU->>Driver: 返回Entity和深度
    Driver->>App: 调用callback
```

---

## View 使用示例

### 基本使用

```cpp
// 创建View
View* view = engine->createView();

// 设置场景和相机
view->setScene(scene);
view->setCamera(camera);

// 设置视口
view->setViewport({0, 0, width, height});

// 配置渲染选项
view->setAntiAliasing(View::AntiAliasing::FXAA);
view->setShadowingEnabled(true);
view->setBloomOptions({...});

// 渲染
renderer->render(view);
```

### 多View使用场景

```mermaid
graph TB
    A[应用程序] --> B[主场景View]
    A --> C[UI View]
    A --> D[反射View]
    
    B --> E[完整渲染管线]
    C --> F[简化渲染管线]
    D --> G[离屏渲染]
```

**典型场景**：
- **主场景View**：完整渲染，包含所有效果
- **UI View**：简化渲染，禁用后处理
- **反射View**：离屏渲染，用于镜面反射

---

## 总结

### View 的核心价值

1. **状态封装**：将渲染所需的所有状态集中管理
2. **性能优化**：通过剔除、分区、并行处理提高性能
3. **灵活配置**：支持丰富的渲染选项和效果
4. **资源管理**：统一管理GPU资源（UBO、描述符集等）

### View 的设计原则

1. **重量级对象**：内部缓存大量数据，避免频繁创建
2. **独立配置**：每个View独立配置，互不影响
3. **多线程友好**：prepare()内部大量使用并行处理
4. **资源复用**：描述符集、UBO等资源在帧间复用

### View 的性能考虑

1. **剔除优化**：视锥剔除、光源剔除减少渲染负担
2. **数据局部性**：SoA布局、分区操作提高缓存命中率
3. **并行处理**：多线程剔除、Froxelization提高CPU利用率
4. **动态分辨率**：根据性能自动调整分辨率保持帧率

---

## 参考资料

- `filament/include/filament/View.h` - View公共接口
- `filament/src/details/View.h` - View实现类
- `filament/src/details/View.cpp` - View实现代码
- Filament官方文档：https://google.github.io/filament/

---

**文档版本**：1.0  
**最后更新**：2024年


