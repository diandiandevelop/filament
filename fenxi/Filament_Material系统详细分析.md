# Filament Material 系统详细分析

## 目录
1. [概述](#概述)
2. [Material 系统架构](#material-系统架构)
3. [Material 定义与编译](#material-定义与编译)
4. [Material 实例化](#material-实例化)
5. [Variant 系统](#variant-系统)
6. [着色器编译与缓存](#着色器编译与缓存)
7. [参数管理](#参数管理)
8. [UBO 批处理](#ubo-批处理)
9. [性能优化](#性能优化)

---

## 概述

Filament 的 Material 系统是渲染管线的核心组件之一，负责管理材质定义、着色器变体、参数绑定和渲染状态。Material 系统采用**材质定义（Material Definition）**和**材质实例（Material Instance）**的分离设计，实现了高效的材质管理和参数定制。

### 核心概念

- **Material（材质）**：不可变的材质定义，包含着色器代码、参数接口、渲染状态等
- **MaterialInstance（材质实例）**：Material 的可变实例，包含具体的参数值、纹理绑定等
- **Variant（变体）**：根据渲染条件（光照、阴影、雾效等）生成的着色器变体
- **MaterialBuilder**：材质构建器，用于从 `.mat` 文件或代码创建材质包
- **MaterialParser**：材质包解析器，从二进制包中提取着色器和元数据

---

## Material 系统架构

### 1. 类层次结构

```
Material (公共 API)
  └── FMaterial (实现)
      ├── MaterialDefinition (材质定义数据)
      ├── MaterialParser (材质包解析器)
      └── mCachedPrograms[256] (缓存的着色器程序)

MaterialInstance (公共 API)
  └── FMaterialInstance (实现)
      ├── mMaterial (关联的 Material)
      ├── mUniforms (Uniform Buffer)
      ├── mTextureParameters (纹理参数)
      └── mDescriptorSet (描述符堆)
```

### 2. 材质包格式

材质包（Material Package）是一个二进制格式，包含：

- **Material Chunk**：材质元数据（名称、混合模式、着色模型等）
- **Shader Chunks**：各种变体的着色器代码（GLSL、SPIR-V、MSL、WGSL）
- **Dictionary Chunks**：字符串字典（用于压缩）
- **Interface Block Chunks**：Uniform 和 Sampler 接口定义
- **Specialization Constants**：特化常量定义

### 3. 材质创建流程

```
1. MaterialBuilder::build()
   └── MaterialCache::acquire()  // 从缓存获取或创建 MaterialDefinition
       └── MaterialDefinition::createParser()  // 解析材质包
           └── MaterialParser::parse()  // 解析二进制包

2. Engine::createMaterial()
   └── FMaterial::FMaterial()  // 创建 Material 对象
       ├── processSpecializationConstants()  // 处理特化常量
       └── precacheDepthVariants()  // 预缓存深度变体

3. Material::createInstance()
   └── FMaterialInstance::FMaterialInstance()  // 创建实例
       ├── 分配 Uniform Buffer
       ├── 创建 DescriptorSet
       └── 初始化默认参数
```

---

## Material 定义与编译

### 1. MaterialBuilder

`MaterialBuilder` 是材质构建器，负责从 `.mat` 文件或代码构建材质包。

**关键方法：**

```cpp
MaterialBuilder& name(const char* name);  // 设置材质名称
MaterialBuilder& material(const char* code);  // 设置材质代码
MaterialBuilder& vertex(const char* code);  // 设置顶点着色器
MaterialBuilder& fragment(const char* code);  // 设置片段着色器
MaterialBuilder& shading(Shading shading);  // 设置着色模型
MaterialBuilder& blending(BlendingMode mode);  // 设置混合模式
Package build();  // 构建材质包
```

**构建流程：**

1. **解析材质定义**：解析 `.mat` 文件（JSONish 格式）
2. **生成着色器变体**：根据 Variant 位掩码生成所有需要的着色器变体
3. **编译着色器**：使用 glslang 编译 GLSL 到 SPIR-V，然后转换为目标后端格式
4. **打包**：将所有数据打包成二进制格式

### 2. MaterialDefinition

`MaterialDefinition` 存储解析后的材质定义数据，是一个**只读**的数据结构。

**关键字段：**

```cpp
struct MaterialDefinition {
    // 渲染状态
    RasterState rasterState;  // 光栅化状态（深度测试、面剔除等）
    BlendingMode blendingMode;  // 混合模式
    Shading shading;  // 着色模型（LIT/UNLIT）
    
    // 接口块
    BufferInterfaceBlock uniformInterfaceBlock;  // Uniform 参数接口
    SamplerInterfaceBlock samplerInterfaceBlock;  // Sampler 参数接口
    
    // 描述符堆布局
    DescriptorSetLayout descriptorSetLayout;  // 材质描述符堆布局
    DescriptorSetLayout perViewDescriptorSetLayout;  // Per-View 描述符堆布局
    
    // 变体过滤
    UserVariantFilterMask variantFilterMask;  // 变体过滤掩码
    
    // 材质解析器
    std::unique_ptr<MaterialParser> mMaterialParser;  // 材质包解析器
};
```

### 3. MaterialParser

`MaterialParser` 负责从材质包中提取着色器代码和元数据。

**关键方法：**

```cpp
bool getShader(ShaderContent& content, ShaderModel model, 
               Variant variant, ShaderStage stage);  // 获取着色器代码
bool getName(utils::CString* name);  // 获取材质名称
bool getUIB(BufferInterfaceBlock* uib);  // 获取 Uniform 接口块
bool getSIB(SamplerInterfaceBlock* sib);  // 获取 Sampler 接口块
```

---

## Material 实例化

### 1. FMaterialInstance 创建

**构造函数流程：**

```cpp
FMaterialInstance::FMaterialInstance(FEngine& engine, 
                                     FMaterial const* material, 
                                     const char* name)
{
    // 1. 分配 Uniform Buffer
    size_t uboSize = std::max(size_t(16), 
                              material->getUniformInterfaceBlock().getSize());
    mUniforms = UniformBuffer(uboSize);
    
    // 2. 创建 UBO 或使用 UBO 批处理
    if (mUseUboBatching) {
        mUboData = BufferAllocator::UNALLOCATED;
        engine.getUboManager()->manageMaterialInstance(this);
    } else {
        mUboData = driver.createBufferObject(...);
        mDescriptorSet.setBuffer(..., mUboData, ...);
    }
    
    // 3. 继承 Material 的渲染状态
    mCulling = rasterState.culling;
    mColorWrite = rasterState.colorWrite;
    mDepthWrite = rasterState.depthWrite;
    
    // 4. 初始化默认参数
    if (material->getBlendingMode() == BlendingMode::MASKED) {
        setMaskThreshold(material->getMaskThreshold());
    }
}
```

### 2. 参数设置

**Uniform 参数：**

```cpp
template<typename T>
void setParameter(const char* name, T const& value) {
    // 1. 查找参数定义
    auto* field = mMaterial->reflect(name);
    
    // 2. 设置 Uniform Buffer 中的值
    mUniforms.setUniform(field->offset, value);
    
    // 3. 标记为脏
    mUniforms.markDirty();
}
```

**纹理参数：**

```cpp
void setParameter(const char* name, 
                  Texture const* texture, 
                  TextureSampler const& sampler) {
    // 1. 获取 Sampler 绑定点
    auto binding = mMaterial->getSamplerBinding(name);
    
    // 2. 设置纹理和采样器参数
    mTextureParameters[binding] = { texture, sampler.getSamplerParams() };
    
    // 3. 更新 DescriptorSet
    mDescriptorSet.setSampler(..., binding, texture->getHandle(), ...);
}
```

### 3. Commit 机制

`commit()` 方法将 MaterialInstance 的参数提交到 GPU：

```cpp
void FMaterialInstance::commit(DriverApi& driver, UboManager* uboManager) {
    // 1. 更新 Uniform Buffer
    if (mUniforms.isDirty()) {
        if (isUsingUboBatching()) {
            uboManager->updateSlot(driver, getAllocationId(), 
                                   mUniforms.toBufferDescriptor(driver));
        } else {
            driver.updateBufferObject(mUboData, 
                                     mUniforms.toBufferDescriptor(driver), 0);
        }
    }
    
    // 2. 更新纹理参数
    for (auto const& [binding, param] : mTextureParameters) {
        mDescriptorSet.setSampler(..., binding, 
                                 param.texture->getHandle(), param.params);
    }
    
    // 3. 提交 DescriptorSet
    mDescriptorSet.commit(..., driver);
}
```

---

## Variant 系统

### 1. Variant 位掩码

Variant 使用 8 位掩码表示不同的渲染条件：

```
位 7   6   5   4   3   2   1   0
+---+---+---+---+---+---+---+---+
|STE|VSM|FOG|DEP|SKN|SRE|DYN|DIR|
+---+---+---+---+---+---+---+---+
```

- **DIR (0x01)**：方向光（Directional Light）
- **DYN (0x02)**：动态光源（Point/Spot/Area Light）
- **SRE (0x04)**：阴影接收（Shadow Receiver）
- **SKN (0x08)**：蒙皮/变形（Skinning/Morphing）
- **DEP (0x10)**：仅深度（Depth Only）
- **FOG (0x20)**：雾效（Fog）
- **VSM (0x40)**：方差阴影贴图（Variance Shadow Maps）
- **STE (0x80)**：立体渲染（Stereo）

### 2. Variant 过滤

**Vertex Variant 过滤：**

```cpp
static constexpr Variant filterVariantVertex(Variant variant) {
    if (isStandardVariant(variant)) {
        // 顶点着色器只关心：立体、蒙皮、阴影接收、动态光源、方向光
        return variant & (STE | SKN | SRE | DYN | DIR);
    }
    if (isDepthVariant(variant)) {
        // 深度变体只关心：立体、VSM、蒙皮
        return variant & (STE | VSM | SKN | DEP);
    }
    return {};
}
```

**Fragment Variant 过滤：**

```cpp
static constexpr Variant filterVariantFragment(Variant variant) {
    if (isStandardVariant(variant)) {
        // 片段着色器只关心：VSM、雾效、阴影接收、动态光源、方向光
        return variant & (VSM | FOG | SRE | DYN | DIR);
    }
    if (isDepthVariant(variant)) {
        // 深度变体只关心：VSM、拾取
        return variant & (VSM | PCK | DEP);
    }
    return {};
}
```

### 3. Variant 生成

在 `MaterialBuilder::build()` 中，根据材质属性生成所有需要的变体：

```cpp
std::vector<Variant> determineSurfaceVariants(
        UserVariantFilterMask filter, bool isLit, bool shadowMultiplier) {
    std::vector<Variant> variants;
    for (size_t k = 0; k < VARIANT_COUNT; k++) {
        Variant variant(k);
        if (Variant::isReserved(variant)) continue;
        
        // 应用用户过滤
        Variant filtered = Variant::filterUserVariant(variant, filter);
        
        // 移除未光照材质的变体
        filtered = Variant::filterVariant(filtered, isLit || shadowMultiplier);
        
        // 分别生成顶点和片段变体
        auto vertexVariant = Variant::filterVariantVertex(filtered);
        if (vertexVariant == variant) {
            variants.emplace_back(variant, ShaderStage::VERTEX);
        }
        
        auto fragmentVariant = Variant::filterVariantFragment(filtered);
        if (fragmentVariant == variant) {
            variants.emplace_back(variant, ShaderStage::FRAGMENT);
        }
    }
    return variants;
}
```

---

## 着色器编译与缓存

### 1. 程序准备流程

**prepareProgram()：**

```cpp
void FMaterial::prepareProgram(Variant variant, 
                               CompilerPriorityQueue priority) const {
    // 快速路径：检查缓存
    if (UTILS_UNLIKELY(!isCached(variant))) {
        prepareProgramSlow(variant, priority);
    }
}
```

**prepareProgramSlow()：**

```cpp
void FMaterial::getSurfaceProgramSlow(Variant variant, 
                                      CompilerPriorityQueue priority) const {
    // 1. 过滤变体
    Variant vertexVariant = Variant::filterVariantVertex(variant);
    Variant fragmentVariant = Variant::filterVariantFragment(variant);
    
    // 2. 从 MaterialParser 获取着色器代码
    Program program = getProgramWithVariants(variant, 
                                            vertexVariant, 
                                            fragmentVariant);
    
    // 3. 设置优先级队列
    program.priorityQueue(priority);
    
    // 4. 创建并缓存程序
    createAndCacheProgram(std::move(program), variant);
}
```

**getProgramWithVariants()：**

```cpp
Program FMaterial::getProgramWithVariants(
        Variant variant,
        Variant vertexVariant,
        Variant fragmentVariant) const {
    MaterialParser const& parser = getMaterialParser();
    
    // 1. 获取顶点着色器
    ShaderContent& vsBuilder = engine.getVertexShaderContent();
    parser.getShader(vsBuilder, sm, vertexVariant, ShaderStage::VERTEX);
    
    // 2. 获取片段着色器
    ShaderContent& fsBuilder = engine.getFragmentShaderContent();
    parser.getShader(fsBuilder, sm, fragmentVariant, ShaderStage::FRAGMENT);
    
    // 3. 构建 Program 对象
    Program program;
    program.shader(ShaderStage::VERTEX, vsBuilder.data(), vsBuilder.size())
           .shader(ShaderStage::FRAGMENT, fsBuilder.data(), fsBuilder.size())
           .shaderLanguage(parser.getShaderLanguage())
           .descriptorBindings(...)
           .specializationConstants(mSpecializationConstants);
    
    return program;
}
```

### 2. 程序缓存

**createAndCacheProgram()：**

```cpp
void FMaterial::createAndCacheProgram(Program&& p, Variant variant) const {
    // 1. 检查是否为共享变体（深度变体）
    bool isShared = isSharedVariant(variant);
    
    if (isShared) {
        // 2. 尝试从默认材质获取
        FMaterial const* defaultMaterial = engine.getDefaultMaterial();
        if (defaultMaterial && defaultMaterial->mCachedPrograms[variant.key]) {
            mCachedPrograms[variant.key] = defaultMaterial->mCachedPrograms[variant.key];
            return;
        }
    }
    
    // 3. 创建新程序
    auto program = driverApi.createProgram(std::move(p), ...);
    mCachedPrograms[variant.key] = program;
    
    // 4. 如果是共享变体，也缓存到默认材质
    if (isShared && defaultMaterial) {
        defaultMaterial->mCachedPrograms[variant.key] = program;
    }
}
```

### 3. 共享变体优化

对于**深度变体**（Depth Variants），Filament 使用共享机制：

- 如果材质没有自定义深度着色器，深度变体从**默认材质**共享
- 这样可以减少重复编译和内存占用

**isSharedVariant()：**

```cpp
bool isSharedVariant(Variant variant) const {
    return (mDefinition.materialDomain == MaterialDomain::SURFACE) &&
           !mIsDefaultMaterial &&
           !mDefinition.hasCustomDepthShader &&
           Variant::isValidDepthVariant(variant);
}
```

### 4. 预缓存深度变体

在 Material 创建时，预缓存所有深度变体：

```cpp
void FMaterial::precacheDepthVariants(FEngine& engine) {
    // 1. 默认材质预缓存所有深度变体
    if (mIsDefaultMaterial) {
        auto allDepthVariants = VariantUtils::getDepthVariants();
        for (auto variant : allDepthVariants) {
            if (hasVariant(variant)) {
                prepareProgram(variant, CompilerPriorityQueue::HIGH);
            }
        }
        return;
    }
    
    // 2. 其他材质从默认材质继承深度变体
    if (mDefinition.materialDomain == MaterialDomain::SURFACE &&
        !mDefinition.hasCustomDepthShader) {
        FMaterial const* defaultMaterial = engine.getDefaultMaterial();
        auto allDepthVariants = VariantUtils::getDepthVariants();
        for (auto variant : allDepthVariants) {
            mCachedPrograms[variant.key] = 
                defaultMaterial->mCachedPrograms[variant.key];
        }
    }
}
```

---

## 参数管理

### 1. Uniform 参数

**UniformInterfaceBlock：**

```cpp
struct BufferInterfaceBlock {
    struct FieldInfo {
        utils::CString name;  // 参数名称
        uint32_t size;  // 数组大小
        uint32_t offset;  // 在 UBO 中的偏移
        UniformType type;  // 参数类型
        Precision precision;  // 精度
    };
    
    std::vector<FieldInfo> mFieldInfoList;  // 字段列表
    size_t mSize;  // UBO 总大小
};
```

**设置参数：**

```cpp
template<typename T>
void FMaterialInstance::setParameterImpl(std::string_view name, T const& value) {
    // 1. 查找参数定义
    auto* field = mMaterial->reflect(name);
    FILAMENT_CHECK_PRECONDITION(field) << "Parameter not found: " << name;
    
    // 2. 验证类型
    FILAMENT_CHECK_PRECONDITION(field->type == getUniformType<T>())
        << "Type mismatch for parameter: " << name;
    
    // 3. 设置 Uniform Buffer 中的值
    mUniforms.setUniform(field->offset, value);
}
```

### 2. Sampler 参数

**SamplerInterfaceBlock：**

```cpp
struct SamplerInterfaceBlock {
    struct SamplerInfo {
        utils::CString name;  // 采样器名称
        descriptor_binding_t binding;  // 绑定点
        SamplerType type;  // 采样器类型（2D、CUBEMAP 等）
        SamplerFormat format;  // 格式（FLOAT、INT、UINT）
        Precision precision;  // 精度
    };
    
    std::vector<SamplerInfo> mSamplerInfoList;  // 采样器列表
};
```

**设置纹理：**

```cpp
void FMaterialInstance::setParameterImpl(std::string_view name,
                                          FTexture const* texture,
                                          TextureSampler const& sampler) {
    // 1. 获取绑定点
    auto binding = mMaterial->getSamplerBinding(name);
    
    // 2. 验证纹理类型兼容性
    DescriptorType descriptorType = layout.getDescriptorType(binding);
    TextureType textureType = texture->getTextureType();
    SamplerType samplerType = texture->getTarget();
    assert(DescriptorSet::isTextureCompatibleWithDescriptor(
        textureType, samplerType, descriptorType));
    
    // 3. 设置纹理参数
    if (texture && texture->textureHandleCanMutate()) {
        // 可变纹理：延迟绑定
        mTextureParameters[binding] = { texture, sampler.getSamplerParams() };
    } else {
        // 不可变纹理：立即绑定
        Handle<HwTexture> handle = texture->getHwHandleForSampling();
        mDescriptorSet.setSampler(..., binding, handle, sampler.getSamplerParams());
    }
}
```

### 3. 参数反射

**反射接口：**

```cpp
// 获取参数信息
size_t Material::getParameterCount() const;
size_t Material::getParameters(ParameterInfo* parameters, size_t count) const;

// 查询参数
bool Material::hasParameter(const char* name) const;
bool Material::isSampler(const char* name) const;
```

---

## UBO 批处理

### 1. UBO Batching 机制

UBO Batching 是一种优化技术，将多个 MaterialInstance 的 Uniform Buffer 合并到一个大的 Buffer 中，减少 Draw Call 和状态切换。

**启用条件：**

```cpp
bool shouldEnableBatching(FEngine& engine, 
                          UboBatchingMode mode, 
                          MaterialDomain domain) {
    return mode != UboBatchingMode::DISABLED &&
           engine.isUboBatchingEnabled() &&
           domain == MaterialDomain::SURFACE;
}
```

### 2. UBO 分配

**非批处理模式：**

```cpp
// 每个 MaterialInstance 创建独立的 UBO
mUboData = driver.createBufferObject(
    mUniforms.getSize(), 
    BufferObjectBinding::UNIFORM,
    BufferUsage::DYNAMIC,
    ...);
mDescriptorSet.setBuffer(..., 0, mUboData, 0, mUniforms.getSize());
```

**批处理模式：**

```cpp
// MaterialInstance 注册到 UboManager
mUboData = BufferAllocator::UNALLOCATED;
engine.getUboManager()->manageMaterialInstance(this);

// 在渲染时分配
void assignUboAllocation(const Handle<HwBufferObject>& ubHandle,
                         BufferAllocator::AllocationId id,
                         BufferAllocator::allocation_size_t offset) {
    mUboData = id;
    mUboOffset = offset;
    mDescriptorSet.setBuffer(..., 0, ubHandle, 0, mUniforms.getSize());
}
```

### 3. UBO 更新

**批处理模式下的更新：**

```cpp
void FMaterialInstance::commit(DriverApi& driver, UboManager* uboManager) {
    if (mUniforms.isDirty()) {
        if (isUsingUboBatching()) {
            // 更新批处理 Buffer 中的槽位
            uboManager->updateSlot(driver, getAllocationId(), 
                                   mUniforms.toBufferDescriptor(driver));
        } else {
            // 更新独立 UBO
            driver.updateBufferObject(mUboData, 
                                     mUniforms.toBufferDescriptor(driver), 0);
        }
    }
}
```

### 4. 绑定时的动态偏移

**use() 方法：**

```cpp
void FMaterialInstance::use(DriverApi& driver, Variant variant) const {
    // 批处理模式下使用动态偏移
    mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_MATERIAL,
                       { { mUboOffset }, driver });
}
```

---

## 性能优化

### 1. 程序缓存

- **按 Variant 缓存**：每个 Variant 对应一个缓存的 Program
- **共享变体**：深度变体在材质间共享，减少重复编译
- **延迟编译**：程序在首次使用时才编译

### 2. 变体过滤

- **用户过滤**：通过 `UserVariantFilterMask` 过滤不需要的变体
- **材质过滤**：根据材质属性（Lit/Unlit）过滤变体
- **运行时过滤**：根据渲染条件动态选择变体

### 3. UBO 批处理

- **减少 Draw Call**：合并多个 MaterialInstance 的 UBO
- **减少状态切换**：共享 UBO 绑定
- **内存优化**：减少小 Buffer 的分配开销

### 4. 并行编译

```cpp
void FMaterial::compile(CompilerPriorityQueue priority,
                        UserVariantFilterMask variants,
                        CallbackHandler* handler,
                        Invocable<void(Material*)>&& callback) {
    // 并行编译所有变体
    if (engine.getDriverApi().isParallelShaderCompileSupported()) {
        for (auto variant : variants) {
            if (hasVariant(variant)) {
                prepareProgram(variant, priority);
            }
        }
    }
    
    // 注册回调
    engine.getDriverApi().compilePrograms(priority, handler, 
                                         &Callback::func, user);
}
```

### 5. 特化常量

特化常量（Specialization Constants）允许在编译时优化着色器：

```cpp
// 设置特化常量
material->setConstant("myConstant", 42);

// 在着色器中使用
#if defined(SPECIALIZATION_CONSTANT_MY_CONSTANT)
    const int myConstant = SPECIALIZATION_CONSTANT_MY_CONSTANT;
#else
    const int myConstant = 0;  // 默认值
#endif
```

---

## 总结

Filament 的 Material 系统通过以下设计实现了高效的材质管理：

1. **分离设计**：Material（定义）和 MaterialInstance（实例）分离
2. **变体系统**：8 位 Variant 掩码支持 256 种组合
3. **程序缓存**：按 Variant 缓存，共享深度变体
4. **UBO 批处理**：合并多个实例的 Uniform Buffer
5. **延迟编译**：按需编译着色器程序
6. **并行编译**：支持多线程编译变体

这些设计使得 Filament 能够高效地管理大量材质和变体，同时保持良好的性能。

