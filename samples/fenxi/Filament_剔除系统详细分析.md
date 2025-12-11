# Filament 剔除系统详细分析

## 目录
1. [概述](#概述)
2. [Culler 架构](#culler-架构)
3. [视锥剔除算法](#视锥剔除算法)
4. [性能优化](#性能优化)
5. [使用场景](#使用场景)

---

## 概述

Filament 的剔除系统（Culling System）负责在渲染前剔除不可见的对象，减少 GPU 渲染负担。核心组件是 `Culler` 类，实现了高效的视锥剔除（Frustum Culling）算法。

### 核心组件

- **Culler**：剔除器，实现视锥剔除算法
- **Frustum**：视锥体，定义 6 个裁剪平面
- **Box**：轴对齐包围盒（AABB）
- **Sphere**：包围球

---

## Culler 架构

### 1. 类结构

```cpp
class Culler {
public:
    // Culler 只能处理大小为 MODULO 倍数的缓冲区
    static constexpr size_t MODULO = 8u;
    
    // 将数量向上舍入到 MODULO 的倍数
    static inline size_t round(size_t const count) noexcept {
        return (count + (MODULO - 1)) & ~(MODULO - 1);
    }
    
    using result_type = uint8_t;  // 结果类型（8 位掩码）
    
    // 批量剔除：AABB 数组
    static void intersects(result_type* results,
            Frustum const& frustum,
            math::float3 const* center,      // AABB 中心
            math::float3 const* extent,      // AABB 半边长
            size_t count, 
            size_t bit);                     // 结果掩码位
    
    // 批量剔除：球体数组
    static void intersects(result_type* results,
            Frustum const& frustum,
            math::float4 const* spheres,     // 球体（xyz=中心，w=半径）
            size_t count);
    
    // 单个对象剔除
    static bool intersects(Frustum const& frustum, Box const& box);
    static bool intersects(Frustum const& frustum, math::float4 const& sphere);
};
```

### 2. MODULO 设计

**为什么需要 MODULO = 8？**

- **SIMD 优化**：允许编译器生成 SIMD 指令，一次处理 8 个对象
- **内存对齐**：确保数据对齐，提高缓存效率
- **向量化提示**：通过 `#pragma clang loop vectorize_width(4)` 提示编译器向量化

**向上舍入：**

```cpp
static inline size_t round(size_t const count) noexcept {
    return (count + (MODULO - 1)) & ~(MODULO - 1);
}
```

例如：`count = 10` → `round(10) = 16`（向上舍入到 8 的倍数）

---

## 视锥剔除算法

### 1. 视锥体定义

视锥体由 6 个平面定义：

```cpp
struct Frustum {
    math::float4 mPlanes[6];  // 6 个平面（左、右、下、上、近、远）
};
```

每个平面使用 `ax + by + cz + d = 0` 的形式，其中 `(a, b, c)` 是法向量，`d` 是距离。

### 2. AABB 剔除算法

**算法原理：**

对于每个 AABB，检查它是否与视锥体的所有 6 个平面相交：

```cpp
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float3 const* UTILS_RESTRICT center,      // AABB 中心
        float3 const* UTILS_RESTRICT extent,      // AABB 半边长
        size_t count, 
        size_t const bit) noexcept {
    
    float4 const * UTILS_RESTRICT const planes = frustum.mPlanes;
    
    // 向上舍入到 MODULO 的倍数
    count = round(count);
    
    // 向量化循环（提示编译器）
    #pragma clang loop vectorize_width(4)
    for (size_t i = 0; i < count; i++) {
        int visible = ~0;  // 初始化为全 1（可见）
        
        // 展开循环（提示编译器）
        #pragma clang loop unroll(full)
        for (size_t j = 0; j < 6; j++) {
            // 计算 AABB 到平面的距离
            // 使用分离轴定理（Separating Axis Theorem）
            const float dot =
                    planes[j].x * center[i].x - std::abs(planes[j].x) * extent[i].x +
                    planes[j].y * center[i].y - std::abs(planes[j].y) * extent[i].y +
                    planes[j].z * center[i].z - std::abs(planes[j].z) * extent[i].z +
                    planes[j].w;
            
            // 如果 dot < 0，AABB 在平面外侧（不可见）
            visible &= fast::signbit(dot) << bit;
        }
        
        // 更新结果掩码
        auto r = results[i];
        r &= ~result_type(1u << bit);  // 清除对应位
        r |= result_type(visible);      // 设置可见性
        results[i] = r;
    }
}
```

**分离轴定理（SAT）：**

对于每个平面，计算 AABB 到平面的最近距离：

```
distance = plane.x * center.x - |plane.x| * extent.x +
           plane.y * center.y - |plane.y| * extent.y +
           plane.z * center.z - |plane.z| * extent.z +
           plane.w
```

如果 `distance < 0`，AABB 在平面外侧，不可见。

### 3. 球体剔除算法

**算法原理：**

对于每个球体，检查它是否与视锥体的所有 6 个平面相交：

```cpp
void Culler::intersects(
        result_type* UTILS_RESTRICT results,
        Frustum const& UTILS_RESTRICT frustum,
        float4 const* UTILS_RESTRICT spheres,    // 球体（xyz=中心，w=半径）
        size_t count) noexcept {
    
    float4 const * const UTILS_RESTRICT planes = frustum.mPlanes;
    
    count = round(count);
    
    #pragma clang loop vectorize_width(4)
    for (size_t i = 0; i < count; i++) {
        int visible = ~0;  // 初始化为全 1（可见）
        float4 const sphere(spheres[i]);
        
        #pragma clang loop unroll(full)
        for (size_t j = 0; j < 6; j++) {
            // 计算球心到平面的距离
            const float dot = planes[j].x * sphere.x +
                              planes[j].y * sphere.y +
                              planes[j].z * sphere.z +
                              planes[j].w - sphere.w;  // 减去半径
            
            // 如果 dot < 0，球体在平面外侧（不可见）
            visible &= fast::signbit(dot);
        }
        results[i] = result_type(visible);
    }
}
```

**球体到平面的距离：**

```
distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w - radius
```

如果 `distance < 0`，球体在平面外侧，不可见。

### 4. 单个对象剔除

**AABB 剔除：**

```cpp
bool Culler::intersects(Frustum const& frustum, Box const& box) noexcept {
    // 主剔除例程假设处理 MODULO 的倍数
    float3 centers[MODULO];
    float3 extents[MODULO];
    result_type results[MODULO];
    centers[0] = box.center;
    extents[0] = box.halfExtent;
    intersects(results, frustum, centers, extents, MODULO, 0);
    return bool(results[0] & 1);
}
```

**球体剔除：**

```cpp
bool Culler::intersects(Frustum const& frustum, float4 const& sphere) noexcept {
    float4 spheres[MODULO];
    result_type results[MODULO];
    spheres[0] = sphere;
    intersects(results, frustum, spheres, MODULO);
    return bool(results[0] & 1);
}
```

---

## 性能优化

### 1. SIMD 向量化

**编译器提示：**

```cpp
#pragma clang loop vectorize_width(4)
for (size_t i = 0; i < count; i++) {
    // ...
}
```

提示编译器使用 SIMD 指令（如 NEON、SSE），一次处理多个对象。

### 2. 循环展开

**完全展开内层循环：**

```cpp
#pragma clang loop unroll(full)
for (size_t j = 0; j < 6; j++) {
    // 处理 6 个平面
}
```

减少循环开销，提高指令级并行性。

### 3. 分支消除

**使用位运算代替分支：**

```cpp
visible &= fast::signbit(dot) << bit;
```

`fast::signbit()` 返回符号位（0 或 1），避免分支预测失败。

### 4. 内存访问优化

**使用 `UTILS_RESTRICT`：**

```cpp
float4 const * UTILS_RESTRICT const planes = frustum.mPlanes;
float3 const* UTILS_RESTRICT center = ...;
```

告诉编译器这些指针不会重叠，允许更激进的优化。

### 5. MODULO 对齐

**确保数据对齐：**

- 向上舍入到 8 的倍数
- 确保 SIMD 指令可以安全使用
- 提高缓存效率

---

## 使用场景

### 1. 主视口剔除

**在 `FView::prepare()` 中：**

```cpp
void FView::cullRenderables(JobSystem&,
        FScene::RenderableSoa& renderableData, 
        Frustum const& frustum, 
        size_t bit) noexcept {
    
    float3 const* worldAABBCenter = renderableData.data<FScene::WORLD_AABB_CENTER>();
    float3 const* worldAABBExtent = renderableData.data<FScene::WORLD_AABB_EXTENT>();
    FScene::VisibleMaskType* visibleArray = renderableData.data<FScene::VISIBLE_MASK>();
    
    // 剔除任务（在多线程上运行）
    auto functor = [&frustum, worldAABBCenter, worldAABBExtent, visibleArray, bit]
            (uint32_t const index, uint32_t const c) {
        Culler::intersects(
                visibleArray + index,
                frustum,
                worldAABBCenter + index,
                worldAABBExtent + index, 
                c, 
                bit);
    };
    
    // 注意：不能使用 jobs::parallel_for()，因为 Culler::intersects() 
    // 必须处理 8 的倍数个图元
    functor(0, renderableData.size());
}
```

### 2. 阴影剔除

**在 `ShadowMapManager` 中：**

```cpp
void ShadowMapManager::cullPointShadowMap(ShadowMap const& shadowMap, 
        FView const& view,
        FScene::RenderableSoa& renderableData, 
        utils::Range<uint32_t> const range,
        FScene::LightSoa const& lightData) noexcept {
    
    // 计算点光源的视锥体
    mat4f const Mv = ShadowMap::getPointLightViewMatrix(TextureCubemapFace(face), position);
    mat4f const Mp = mat4f::perspective(90.0f, 1.0f, 0.01f, radius);
    Frustum const frustum{ highPrecisionMultiply(Mp, Mv) };
    
    // 剔除阴影投射者
    Culler::intersects(
            visibleArray + range.first,
            frustum,
            worldAABBCenter + range.first,
            worldAABBExtent + range.first,
            range.size(),
            VISIBLE_DYN_SHADOW_RENDERABLE_BIT);
}
```

### 3. 可见性掩码

**多级可见性：**

```cpp
// 使用位掩码存储多个可见性状态
using VisibleMaskType = uint8_t;

// 不同的可见性位
constexpr size_t VISIBLE_RENDERABLE_BIT = 0;
constexpr size_t VISIBLE_DYN_SHADOW_RENDERABLE_BIT = 1;
constexpr size_t VISIBLE_STATIC_SHADOW_RENDERABLE_BIT = 2;
```

每个对象可以有多个可见性状态（主视口、阴影等）。

---

## 算法复杂度

### 时间复杂度

- **单个对象**：O(1)（6 个平面检查）
- **N 个对象**：O(N)（线性时间）

### 空间复杂度

- **O(1)**（原地操作，不分配额外内存）

### 实际性能

根据代码注释，在 Pixel 4 上：
- **4000 个图元**：约 **100 微秒**
- **单线程性能**：即使大量图元，JobSystem 开销也太大

---

## 总结

Filament 的剔除系统通过以下设计实现了高效的视锥剔除：

1. **SIMD 优化**：使用 MODULO=8 对齐，允许向量化
2. **分支消除**：使用位运算代替分支
3. **循环展开**：完全展开内层循环
4. **内存优化**：使用 `RESTRICT` 提示，优化内存访问
5. **批量处理**：批量处理多个对象，减少函数调用开销

这些优化使得 Filament 能够在保持高精度的同时，实现高效的剔除性能。

