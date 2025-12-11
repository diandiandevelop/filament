# Filament 资源分配器详细分析

## 目录
1. [概述](#概述)
2. [ResourceAllocator 架构](#resourceallocator-架构)
3. [纹理缓存机制](#纹理缓存机制)
4. [垃圾回收策略](#垃圾回收策略)
5. [性能优化](#性能优化)

---

## 概述

Filament 的资源分配器（Resource Allocator）负责管理纹理和渲染目标的创建和销毁，通过缓存机制减少重复分配，提高性能。

### 核心组件

- **ResourceAllocator**：资源分配器，管理纹理和渲染目标
- **ResourceAllocatorDisposer**：资源销毁器，延迟销毁资源
- **TextureKey**：纹理键，用于缓存查找
- **TextureCache**：纹理缓存容器

---

## ResourceAllocator 架构

### 1. 类结构

```cpp
class ResourceAllocator : public ResourceAllocatorInterface {
public:
    // 创建渲染目标
    RenderTargetHandle createRenderTarget(...);
    
    // 销毁渲染目标
    void destroyRenderTarget(RenderTargetHandle h);
    
    // 创建纹理（带缓存）
    TextureHandle createTexture(...);
    
    // 销毁纹理（加入缓存）
    void destroyTexture(TextureHandle h);
    
    // 垃圾回收
    void gc(bool skippedFrame = false);
    
private:
    struct TextureKey {
        utils::StaticString name;
        SamplerType target;
        uint8_t levels;
        TextureFormat format;
        uint8_t samples;
        uint32_t width, height, depth;
        TextureUsage usage;
        std::array<TextureSwizzle, 4> swizzle;
        
        size_t getSize() const noexcept;  // 计算纹理大小
    };
    
    struct TextureCachePayload {
        TextureHandle handle;
        size_t age;      // 缓存年龄
        uint32_t size;   // 纹理大小（字节）
    };
    
    size_t const mCacheMaxAge;           // 缓存最大年龄
    backend::DriverApi& mBackend;
    std::shared_ptr<ResourceAllocatorDisposer> mDisposer;
    CacheContainer mTextureCache;         // 纹理缓存
    size_t mAge = 0;                      // 当前年龄
    uint32_t mCacheSize = 0;              // 缓存总大小
    uint32_t mCacheSizeHiWaterMark = 0;   // 缓存高水位标记
};
```

### 2. 纹理键（TextureKey）

**用于缓存查找的键：**

```cpp
struct TextureKey {
    utils::StaticString name;              // 纹理名称
    SamplerType target;                    // 采样器类型（2D、3D、CUBEMAP 等）
    uint8_t levels;                       // Mipmap 级别数
    TextureFormat format;                  // 纹理格式
    uint8_t samples;                       // MSAA 采样数
    uint32_t width, height, depth;         // 尺寸
    TextureUsage usage;                    // 使用标志
    std::array<TextureSwizzle, 4> swizzle; // Swizzle 配置
};
```

**计算纹理大小：**

```cpp
size_t TextureKey::getSize() const noexcept {
    // 1. 计算像素数
    size_t pixelCount = width * height * depth;
    
    // 2. 计算基础大小
    size_t size = pixelCount * FTexture::getFormatSize(format);
    
    // 3. MSAA 倍数
    if (samples > 1) {
        size *= samples;
    }
    
    // 4. Mipmap 金字塔（约增加 1/3）
    if (levels > 1) {
        size += size / 3;
    }
    
    return size;
}
```

---

## 纹理缓存机制

### 1. 创建纹理

**`createTexture()` 流程：**

```cpp
TextureHandle ResourceAllocator::createTexture(
        StaticString name,
        SamplerType target, uint8_t levels, TextureFormat format,
        uint8_t samples, uint32_t width, uint32_t height, uint32_t depth,
        std::array<TextureSwizzle, 4> swizzle,
        TextureUsage usage) noexcept {
    
    // 1. 构建纹理键
    TextureKey const key{ name, target, levels, format, samples, 
                          width, height, depth, usage, swizzle };
    
    TextureHandle handle;
    
    if constexpr (mEnabled) {
        // 2. 查找缓存
        auto it = mTextureCache.find(key);
        if (UTILS_LIKELY(it != mTextureCache.end())) {
            // 3. 缓存命中：从缓存移除，加入使用列表
            handle = it->second.handle;
            mCacheSize -= it->second.size;
            mTextureCache.erase(it);
        } else {
            // 4. 缓存未命中：创建新纹理
            handle = mBackend.createTexture(target, levels, format, samples,
                                          width, height, depth, usage, name);
            
            // 5. 处理 Swizzle（如果需要）
            if (swizzle != defaultSwizzle) {
                TextureHandle swizzledHandle = mBackend.createTextureViewSwizzle(
                        handle, swizzle[0], swizzle[1], swizzle[2], swizzle[3], name);
                mBackend.destroyTexture(handle);
                handle = swizzledHandle;
            }
        }
    } else {
        // 缓存禁用：直接创建
        handle = mBackend.createTexture(...);
    }
    
    // 6. 加入使用列表
    mDisposer->checkout(handle, key);
    return handle;
}
```

### 2. 销毁纹理

**`destroyTexture()` 流程：**

```cpp
void ResourceAllocator::destroyTexture(TextureHandle h) noexcept {
    // 1. 从使用列表移除，获取纹理键
    auto key = mDisposer->checkin(h);
    
    if constexpr (mEnabled) {
        if (UTILS_LIKELY(key.has_value())) {
            // 2. 加入缓存（而不是立即销毁）
            uint32_t size = key.value().getSize();
            mTextureCache.emplace(key.value(), TextureCachePayload{ h, mAge, size });
            mCacheSize += size;
            mCacheSizeHiWaterMark = std::max(mCacheSizeHiWaterMark, mCacheSize);
        }
    } else {
        // 缓存禁用：立即销毁
        mBackend.destroyTexture(h);
    }
}
```

### 3. ResourceAllocatorDisposer

**延迟销毁机制：**

```cpp
class ResourceAllocatorDisposer : public ResourceAllocatorDisposerInterface {
public:
    // 纹理开始使用
    void checkout(TextureHandle handle, TextureKey key);
    
    // 纹理停止使用
    std::optional<TextureKey> checkin(TextureHandle handle);
    
    // 实际销毁纹理
    void destroy(TextureHandle handle) noexcept override;
    
private:
    backend::DriverApi& mBackend;
    InUseContainer mInUseTextures;  // 使用中的纹理
};
```

**工作流程：**

1. **checkout()**：纹理开始使用时，从缓存移除，加入使用列表
2. **checkin()**：纹理停止使用时，从使用列表移除，返回纹理键
3. **destroy()**：实际销毁纹理（由 ResourceAllocator 调用）

---

## 垃圾回收策略

### 1. 回收机制

**`gc()` 实现：**

```cpp
void ResourceAllocator::gc(bool skippedFrame) noexcept {
    // 1. 更新年龄
    const size_t age = mAge;
    if (!skippedFrame) {
        mAge++;
    }
    
    // 2. 回收策略：
    //    - 跳帧时：移除所有年龄 > MAX_AGE_SKIPPED_FRAME 的条目
    //    - 正常帧：移除年龄 > mCacheMaxAge 的条目（每帧最多 MAX_EVICTION_COUNT 个）
    //    - 唯一年龄桶：移除超过 MAX_UNIQUE_AGE_COUNT 个桶的条目
    
    auto& textureCache = mTextureCache;
    
    // 3. 跳帧时的最大年龄
    constexpr size_t MAX_AGE_SKIPPED_FRAME = 1;
    
    if (skippedFrame) {
        // 移除所有年龄 > MAX_AGE_SKIPPED_FRAME 的条目
        for (auto it = textureCache.begin(); it != textureCache.end();) {
            if (age - it->second.age > MAX_AGE_SKIPPED_FRAME) {
                mBackend.destroyTexture(it->second.handle);
                mCacheSize -= it->second.size;
                it = textureCache.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    
    // 4. 正常帧：计算唯一年龄桶
    std::set<size_t> uniqueAges;
    for (auto const& [key, payload] : textureCache) {
        uniqueAges.insert(payload.age);
    }
    
    // 5. 移除超过 MAX_UNIQUE_AGE_COUNT 个桶的条目
    constexpr size_t MAX_UNIQUE_AGE_COUNT = 3;
    if (uniqueAges.size() > MAX_UNIQUE_AGE_COUNT) {
        size_t thresholdAge = *std::next(uniqueAges.begin(), 
                                         uniqueAges.size() - MAX_UNIQUE_AGE_COUNT);
        for (auto it = textureCache.begin(); it != textureCache.end();) {
            if (it->second.age < thresholdAge) {
                mBackend.destroyTexture(it->second.handle);
                mCacheSize -= it->second.size;
                it = textureCache.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 6. 每帧最多移除 MAX_EVICTION_COUNT 个条目
    constexpr size_t MAX_EVICTION_COUNT = 4;
    size_t evictionCount = 0;
    for (auto it = textureCache.begin(); 
         it != textureCache.end() && evictionCount < MAX_EVICTION_COUNT;) {
        if (age - it->second.age > mCacheMaxAge) {
            mBackend.destroyTexture(it->second.handle);
            mCacheSize -= it->second.size;
            it = textureCache.erase(it);
            evictionCount++;
        } else {
            ++it;
        }
    }
}
```

### 2. 回收策略详解

**三级回收策略：**

1. **跳帧回收**：跳帧时，移除所有年龄 > 1 的条目
2. **年龄回收**：正常帧时，每帧最多移除 4 个年龄 > `mCacheMaxAge` 的条目
3. **桶回收**：如果唯一年龄桶数 > 3，移除最旧的桶

**优势：**

- **平滑回收**：避免单帧大量销毁导致的卡顿
- **内存控制**：限制缓存大小，避免内存泄漏
- **快速响应**：跳帧时快速清理，释放内存

---

## 性能优化

### 1. 缓存查找

**使用线性容器 + 哈希：**

```cpp
template<typename K, typename V, typename H>
class AssociativeContainer {
    std::vector<std::pair<K, V>> mContainer;
    
    iterator find(key_type const& key) {
        return std::find_if(mContainer.begin(), mContainer.end(),
                           [&key](auto const& v) { return v.first == key; });
    }
};
```

**优势：**

- 缓存友好的线性布局
- 小数据集时性能优于 `std::unordered_map`
- 预分配容量，减少重新分配

### 2. 延迟销毁

**纹理不立即销毁，而是加入缓存：**

- 减少 GPU 资源分配/释放开销
- 提高纹理复用率
- 避免频繁的驱动调用

### 3. 年龄跟踪

**每个缓存条目记录年龄：**

```cpp
struct TextureCachePayload {
    TextureHandle handle;
    size_t age;      // 加入缓存时的年龄
    uint32_t size;   // 纹理大小
};
```

**用途：**

- 实现 LRU（Least Recently Used）策略
- 优先回收旧纹理
- 跟踪缓存使用模式

---

## 总结

Filament 的资源分配器通过以下设计实现了高效的资源管理：

1. **纹理缓存**：避免重复创建相同纹理
2. **延迟销毁**：纹理不立即销毁，加入缓存
3. **三级回收**：跳帧、年龄、桶三级回收策略
4. **内存控制**：限制缓存大小，避免内存泄漏
5. **性能优化**：缓存友好的数据结构，减少分配开销

这些设计使得 Filament 能够在保持内存效率的同时，实现高效的资源管理性能。

