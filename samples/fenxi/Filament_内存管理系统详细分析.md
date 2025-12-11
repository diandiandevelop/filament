# Filament 内存管理系统详细分析

## 目录
- 概览
- 核心组件
  - Arena 系统（Linear / Heap / RootArenaScope）
  - BufferAllocator（UBO 槽位分配）
  - UboManager（共享 UBO + 延迟增长 + Fence 回收）
  - BufferPoolAllocator / 后端上传策略（示例：Metal）
- 渲染流程中的作用点
- 数据结构与生命周期
- 性能与优化要点

---

## 概览
Filament 的内存管理采用“分层分域”策略：
- **Arena**：面向 CPU 的临时/持久分配（线性或堆），大规模用于每帧临时数据。
- **BufferAllocator + UboManager**：面向 GPU 的 UBO 槽位管理，支持按需扩容、GPU 使用计数、Fence 回收。
- **资源分配器**：纹理/RenderTarget 等 GPU 资源的缓存与回收（ResourceAllocator，已在其他文档覆盖）。
- **后端缓冲池**：平台特定的上传优化（如 Metal bump allocator / pool）。

---

## 核心组件

### 1) Arena 系统
文件：`filament/src/Allocators.h`、`libs/utils/include/utils/Allocator.h`
- **LinearAllocator**：线性分配；只支持整体或回滚式释放，适合 per-frame 临时内存。
- **HeapAllocator**：包装 malloc/free。
- **Arena 模板**：可插拔 LockingPolicy / TrackingPolicy / AreaPolicy。调试版可记录分配水位。
- 关键别名：
  - `LinearAllocatorArena`：无锁线性 Arena（Release 下无追踪）。
  - `HeapAllocatorArena`：堆分配 Arena（Debug 下带锁与追踪）。
  - `RootArenaScope`：作用域型对象，进入作用域分配，退出时整体回收（per-frame）。
- 使用场景：FrameGraph 构建、渲染命令/临时列表、froxel 数据等（均在 per-frame Arena 中分配）。

### 2) BufferAllocator（UBO 槽位分配）
文件：`filament/src/details/BufferAllocator.h/.cpp`
- 目标：在单个大 UBO 内管理固定对齐的“槽位”：
  - slotSize：满足 GPU UBO 对齐（常见 256 字节），totalSize：当前 UBO 总容量。
  - 分配返回 `{AllocationId, offset}`，不足则返回 `REALLOCATION_REQUIRED` 触发上层扩容。
- 数据结构：
  - `mSlotPool`：双向链表风格容器，记录连续块。
  - `mFreeList`：按 size 排序的 multimap（best-fit）。
  - `mOffsetMap`：offset → 节点，用于通过 AllocationId 反查节点。
- 合并回收：
  - `retire()` 标记 CPU 释放，`acquireGpu()/releaseGpu()` 跟踪 GPU 使用计数。
  - `releaseFreeSlots()` 合并相邻空闲块，更新 freeList/offsetMap。
- 非线程安全：调用者需保证单线程或外部锁。

### 3) UboManager（共享 UBO 管理）
文件：`filament/src/details/UboManager.h/.cpp`
- 目标：集中管理所有 MaterialInstance 的 UBO：
  - 使用 BufferAllocator 分配槽位；一个大 UBO（可增长 1.5x）。
  - 生命周期：`beginFrame()` 触发回收与按需扩容，映射 UBO → `commit()` 写入 → `endFrame()` 建立 Fence 跟踪 GPU 使用 → `reclaim`。
- 核心流程：
  1. `beginFrame()`：Fence 回收（releaseGpu），合并 free list，若需扩容则 `reallocate()` 新 UBO 并为所有 MI 重新分配槽位；映射 UBO。
  2. `commit()`（在 FMaterialInstance 内调用）：写脏 Uniform 到映射内存。
  3. `endFrame()`：为本帧使用的 AllocationId 建 Fence，gpuUseCount++。
  4. 下帧 `beginFrame()`：Fence 完成后 gpuUseCount--，释放可合并块。
- 特点：
  - 批量管理：MaterialInstance 通过 `manageMaterialInstance()` 注册，解绑时 `unmanageMaterialInstance()` 释放槽位。
  - 按需扩容：分配失败返回 REALLOCATION_REQUIRED 触发 UBO 增长。
  - 映射写入：只在 beginFrame/finishBeginFrame 之间映射一次，减少 map/unmap。

### 4) BufferPoolAllocator / 后端上传策略（以 Metal 为例）
文件：`filament/src/BufferPoolAllocator.h`，`backend/src/metal/MetalBuffer.mm`
- BufferPoolAllocator：固定池 + 按最大请求放大；用于 CPU 侧临时上传缓冲。
- Metal：UploadStrategy
  - 若 bump allocator 容量可用且是 VERTEX/INDEX，走 BUMP（线性）分配。
  - 否则走 POOL；共享/Managed 依据 SHARED_WRITE_BIT 与架构决定。

---

## 渲染流程中的作用点
- **Per-frame Arena**：Renderer::renderInternal → View::prepare / FrameGraph 构建时分配临时数据，随帧结束释放。
- **UBO 分配**：MaterialInstance setParameter → commit() 时由 UboManager 统一槽位与上传。
- **资源分配器**（另文）：纹理/RT 缓存，与 FrameGraph 资源生命周期绑定。

---

## 数据结构与生命周期
- Arena：作用域结束释放；无析构调用，适合 POD/临时数据。
- BufferAllocator：槽位可合并，gpuUseCount 保护正在使用的块；需要 releaseFreeSlots 合并。
- UboManager：跨帧保留 UBO，动态增长；Fence 确保 GPU 用完后回收。

---

## 性能与优化要点
- 线性 Arena：避免碎片与锁，按帧释放；debug 下可追踪水位。
- UBO 槽位合并：批量 releaseFreeSlots，减少碎片；按需增长（1.5x）平衡抖动与浪费。
- 减少 map/unmap：UboManager 仅在 beginFrame/finishBeginFrame 间映射一次。
- 平台上传优化：Metal bump allocator/池化；可减少小缓冲频繁创建。

---

## 摘要
Filament 的内存管理核心思路：
- **Arena** 提供高效的 per-frame 线性临时分配。
- **BufferAllocator + UboManager** 通过槽位化管理共享 UBO，配合 Fence 安全回收。
- **BufferPool/平台策略** 减少上传缓冲的分配开销。
该组合在保证低碎片和低锁开销的同时，兼顾 GPU 资源的可增长与跨帧安全回收。 

