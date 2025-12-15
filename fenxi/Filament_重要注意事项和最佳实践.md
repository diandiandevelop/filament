# Filament 重要注意事项和最佳实践

## 目录
1. [线程安全](#线程安全)
2. [资源生命周期管理](#资源生命周期管理)
3. [错误处理和调试](#错误处理和调试)
4. [性能优化](#性能优化)
5. [FrameGraph 使用](#framegraph-使用)
6. [平台特定注意事项](#平台特定注意事项)
7. [常见陷阱](#常见陷阱)
8. [最佳实践](#最佳实践)

---

## 线程安全

### 1.1 Engine 不是线程安全的

**重要**：`Engine` 实例**不是线程安全的**。所有对 `Engine` 方法的调用必须在同一线程进行，或使用外部同步机制。

```cpp
// ❌ 错误：多线程不安全
std::thread t1([&engine]() {
    engine->createTexture(...);  // 危险！
});

std::thread t2([&engine]() {
    engine->createBuffer(...);  // 危险！
});

// ✅ 正确：单线程使用
void renderThread(Engine* engine) {
    engine->createTexture(...);
    engine->createBuffer(...);
}

// ✅ 正确：使用互斥锁保护
std::mutex engineMutex;
std::thread t1([&engine, &engineMutex]() {
    std::lock_guard<std::mutex> lock(engineMutex);
    engine->createTexture(...);
});
```

### 1.2 渲染线程 vs 主线程

Filament 使用多线程架构：

- **主线程**：创建资源、更新场景
- **渲染线程**：执行 GPU 命令
- **工作线程**：并行处理（JobSystem）

**注意**：
- 资源创建在主线程，但实际 GPU 资源创建在渲染线程
- 不要在渲染线程中创建资源（除非使用同步机制）
- 使用 `Fence` 进行 CPU-GPU 同步

### 1.3 线程安全的组件

以下组件是**线程安全的**（或设计为线程安全）：
- `Fence`：可以在不同线程等待
- `TimerQuery`：可以在不同线程查询结果
- 某些回调处理器（`CallbackHandler`）

### 1.4 命令流传递

句柄（`Handle<T>`）可以在命令流中安全传递，因为：
- 句柄只是 ID（`uint32_t`），不是指针
- 命令流会自动序列化句柄
- 渲染线程会正确反序列化

---

## 资源生命周期管理

### 2.1 资源创建和销毁

**规则**：所有通过 `Engine` 创建的资源必须通过 `Engine` 销毁。

```cpp
// ✅ 正确
Engine* engine = Engine::create();
Texture* texture = engine->createTexture(...);
// ... 使用 texture
engine->destroy(texture);  // 必须通过 engine 销毁
Engine::destroy(&engine);

// ❌ 错误
delete texture;  // 不要直接删除！
```

### 2.2 资源泄漏检测

Filament 会在 `Engine` 销毁时检测资源泄漏：

```cpp
// 如果忘记销毁资源，Engine 析构时会输出警告：
// "Leaked resources detected: Texture(1), Buffer(2), ..."
```

### 2.3 延迟销毁

资源销毁是**延迟的**，实际销毁发生在：
- 帧结束时（`Renderer::endFrame()`）
- 或显式调用 `Engine::gc()` 时

**注意**：销毁后立即使用资源会导致未定义行为。

```cpp
engine->destroy(texture);
// ❌ 错误：texture 可能还未真正销毁，但不应再使用
texture->setImage(...);  // 未定义行为！

// ✅ 正确：等待帧结束
engine->destroy(texture);
renderer->endFrame();  // 等待资源真正销毁
```

### 2.4 资源依赖

销毁资源时注意依赖关系：

```cpp
// MaterialInstance 依赖 Material
Material* material = engine->createMaterial(...);
MaterialInstance* instance = material->createInstance();

// 必须先销毁实例，再销毁材质
engine->destroy(instance);
engine->destroy(material);  // 如果先销毁 material，instance 会失效
```

---

## 错误处理和调试

### 3.1 Panic 机制

Filament 使用 Panic 机制处理严重错误：

```cpp
// 前置条件检查
FILAMENT_CHECK_PRECONDITION(condition) 
    << "Error message";

// 后置条件检查
FILAMENT_CHECK_POSTCONDITION(condition) 
    << "Error message";

// 算术错误检查
FILAMENT_CHECK_ARITHMETIC(condition) 
    << "Error message";
```

**注意**：
- Panic 默认会终止程序（`std::terminate()`）
- 在测试中可以设置为抛出异常（`Panic::setMode(Panic::Mode::THROW)`）
- 不要在析构函数中使用 Panic（使用 `ASSERT_DESTRUCTOR`）

### 3.2 OpenGL 错误检查

OpenGL 后端会检查错误：

```cpp
// 自动检查（调试模式）
GLenum error = glGetError();
if (error != GL_NO_ERROR) {
    slog.e << "OpenGL error: " << getGLErrorString(error) << io::endl;
}
```

### 3.3 Vulkan 验证层

Vulkan 后端支持验证层：

```cpp
// 启用验证层（调试模式）
VkInstanceCreateInfo createInfo{
    .enabledLayerCount = validationLayerCount,
    .ppEnabledLayerNames = validationLayers,
};
```

### 3.4 资源标签

使用资源标签便于调试：

```cpp
// 为资源添加标签（调试用）
Handle<HwTexture> handle = ...;
mHandleAllocator.associateTag(handle.getId(), "MainTexture");
```

---

## 性能优化

### 4.1 命令缓冲区大小

**重要**：根据应用需求配置命令缓冲区大小。

```cpp
Engine::Config config;
config.minCommandBufferSizeMB = 4;  // 默认值，根据应用调整

Engine* engine = Engine::create(config);
```

**注意**：
- 太小：会导致命令缓冲区溢出，阻塞渲染线程
- 太大：浪费内存
- 建议：从默认值开始，根据日志调整

### 4.2 批量操作

尽量批量操作减少开销：

```cpp
// ❌ 低效：多次单独更新
for (auto& texture : textures) {
    texture->setImage(engine, 0, data);
}

// ✅ 高效：批量更新（如果可能）
// 或使用 CommandStream 批量提交
```

### 4.3 资源复用

使用 `ResourceAllocator` 复用资源：

```cpp
// ResourceAllocator 会自动缓存和复用纹理
// 相同参数的纹理会被复用，而不是重新创建
```

### 4.4 避免频繁同步

避免频繁的 CPU-GPU 同步：

```cpp
// ❌ 低效：每帧都同步
for (int i = 0; i < 100; i++) {
    renderer->render(view);
    renderer->endFrame();
    glFinish();  // 阻塞等待
}

// ✅ 高效：批量提交，最后同步
for (int i = 0; i < 100; i++) {
    renderer->render(view);
    renderer->endFrame();
}
Fence::waitAndDestroy(fence);  // 只在需要时同步
```

### 4.5 UBO 分配优化

Uniform Buffer 使用共享 UBO 和槽位分配：

```cpp
// UboManager 会自动管理 UBO 分配
// 尽量重用 MaterialInstance，避免频繁创建/销毁
```

---

## FrameGraph 使用

### 5.1 资源依赖声明

**重要**：正确声明资源依赖，否则会导致资源生命周期错误。

```cpp
FrameGraph& fg = renderer->getFrameGraph();

// 声明资源
auto shadowMap = fg.createTexture("ShadowMap", ...);

// 声明 Pass 依赖
fg.addPass("ShadowPass", [&](FrameGraphPassBuilder& builder, ...) {
    builder.read(shadowMap);  // 读取依赖
    builder.write(shadowMap); // 写入依赖
});
```

### 5.2 资源生命周期

FrameGraph 自动管理资源生命周期：

```cpp
// 资源在最后一次使用后自动销毁
// 不需要手动管理生命周期
```

### 5.3 资源别名

使用资源别名避免不必要的分配：

```cpp
// 如果两个 Pass 使用相同的资源但不同时使用，
// FrameGraph 可以重用内存（别名）
fg.alias(shadowMap, colorBuffer);  // 重用内存
```

### 5.4 循环检测

FrameGraph 会自动检测循环依赖：

```cpp
// ❌ 错误：循环依赖
Pass A → Resource X → Pass B → Resource X → Pass A
// FrameGraph 会检测并报错
```

---

## 平台特定注意事项

### 6.1 OpenGL ES 2.0 限制

OpenGL ES 2.0 有特殊限制：

- 不支持 Uniform Buffer（使用 CPU 模拟）
- 不支持多渲染目标（MRT）
- 某些纹理格式不支持

**注意**：Filament 会自动处理这些限制，但性能可能受影响。

### 6.2 Vulkan 同步

Vulkan 后端需要显式同步：

```cpp
// Vulkan 使用 Semaphore 和 Fence 同步
// Filament 自动管理，但需要注意：
// - 命令缓冲区提交是异步的
// - 资源访问需要正确的屏障
```

### 6.3 Metal 资源管理

Metal 后端使用引用计数：

```cpp
// Metal 对象使用 ARC（自动引用计数）
// Filament 使用 resource_ptr 管理生命周期
```

### 6.4 移动平台

移动平台特殊注意事项：

- **内存限制**：注意纹理大小和数量
- **热节流**：避免长时间高负载
- **电池优化**：合理使用 VSYNC

---

## 常见陷阱

### 7.1 使用已销毁的资源

```cpp
// ❌ 错误
engine->destroy(texture);
texture->setImage(...);  // 未定义行为！

// ✅ 正确
engine->destroy(texture);
texture = nullptr;  // 清空指针
```

### 7.2 跨线程访问 Engine

```cpp
// ❌ 错误
std::thread t([&engine]() {
    engine->createTexture(...);  // 线程不安全！
});

// ✅ 正确
std::mutex mtx;
std::thread t([&engine, &mtx]() {
    std::lock_guard<std::mutex> lock(mtx);
    engine->createTexture(...);
});
```

### 7.3 命令缓冲区溢出

```cpp
// 如果命令缓冲区太小，会看到错误：
// "Backend CommandStream overflow. Commands are corrupted..."

// 解决：增加命令缓冲区大小
Engine::Config config;
config.minCommandBufferSizeMB = 8;  // 增加大小
```

### 7.4 资源泄漏

```cpp
// ❌ 错误：忘记销毁
Texture* texture = engine->createTexture(...);
// 忘记调用 engine->destroy(texture)

// ✅ 正确：使用 RAII 或确保销毁
class TextureRAII {
    Engine* mEngine;
    Texture* mTexture;
public:
    ~TextureRAII() {
        if (mTexture) {
            mEngine->destroy(mTexture);
        }
    }
};
```

### 7.5 材质变体缓存

```cpp
// 材质变体会被缓存
// 如果频繁创建相同变体，会浪费内存
// 尽量重用 MaterialInstance
```

---

## 最佳实践

### 8.1 资源创建模式

```cpp
// ✅ 推荐：集中创建
void initResources(Engine* engine) {
    mTexture = engine->createTexture(...);
    mBuffer = engine->createBuffer(...);
    mMaterial = engine->createMaterial(...);
}

// ✅ 推荐：使用 RAII
class ResourceManager {
    Engine* mEngine;
    std::vector<Texture*> mTextures;
public:
    ~ResourceManager() {
        for (auto* tex : mTextures) {
            mEngine->destroy(tex);
        }
    }
};
```

### 8.2 渲染循环模式

```cpp
// ✅ 推荐：标准渲染循环
while (running) {
    // 1. 更新场景
    updateScene();
    
    // 2. 开始帧
    if (renderer->beginFrame(swapChain)) {
        // 3. 渲染
        renderer->render(view);
        
        // 4. 结束帧
        renderer->endFrame();
    }
    
    // 5. 等待 VSYNC（由平台处理）
}
```

### 8.3 错误处理模式

```cpp
// ✅ 推荐：检查返回值
Texture* texture = engine->createTexture(...);
if (!texture) {
    // 处理错误
    return;
}

// ✅ 推荐：使用异常（如果启用）
try {
    Texture* texture = engine->createTexture(...);
} catch (const utils::Panic& e) {
    // 处理错误
}
```

### 8.4 性能优化模式

```cpp
// ✅ 推荐：批量提交
renderer->beginFrame(swapChain);
for (auto* view : views) {
    renderer->render(view);  // 批量渲染多个视图
}
renderer->endFrame();

// ✅ 推荐：避免频繁同步
// 只在必要时使用 Fence
if (needSync) {
    Fence* fence = engine->createFence();
    Fence::waitAndDestroy(fence);
}
```

### 8.5 调试模式

```cpp
// ✅ 推荐：启用调试功能
Engine::Config config;
config.debugLevel = Engine::DebugLevel::DEBUG;  // 启用调试
config.enableValidation = true;  // 启用验证（Vulkan）

Engine* engine = Engine::create(config);

// ✅ 推荐：使用资源标签
mHandleAllocator.associateTag(handle.getId(), "MyTexture");
```

### 8.6 内存管理

```cpp
// ✅ 推荐：监控内存使用
size_t textureMemory = getTextureMemoryUsage();
size_t bufferMemory = getBufferMemoryUsage();

// ✅ 推荐：及时释放不需要的资源
if (textureMemory > threshold) {
    // 释放旧的纹理
    releaseOldTextures();
}
```

---

## 总结

### 关键要点

1. **线程安全**：Engine 不是线程安全的，需要外部同步
2. **资源管理**：所有资源必须通过 Engine 创建和销毁
3. **生命周期**：注意资源依赖和销毁顺序
4. **性能**：合理配置命令缓冲区大小，避免频繁同步
5. **错误处理**：使用 Panic 机制，启用调试功能
6. **平台差异**：注意不同后端的限制和特性

### 检查清单

在使用 Filament 时，确保：

- [ ] 所有 Engine 调用在同一线程或使用同步
- [ ] 所有资源通过 Engine 销毁
- [ ] 命令缓冲区大小配置合理
- [ ] 正确声明 FrameGraph 资源依赖
- [ ] 避免使用已销毁的资源
- [ ] 启用调试功能（开发时）
- [ ] 监控内存使用
- [ ] 处理错误情况

---

**文档版本**：1.0  
**最后更新**：2024  
**作者**：Filament 分析文档

