# Filament DX12 后端最小骨架方案

目标：最小可运行的 DX12 后端（清屏 + 三角形），按里程碑逐步扩展，避免一次性引入大规模未验证代码。此文档仅规划，不改动构建与现有后端。

## 目录规划（建议）
```
filament/backend/src/dx12/
  PlatformDX12.h / .cpp        // 设备/队列/交换链创建，窗口集成（DXGI）
  Dx12Driver.h / .cpp          // 实现 Driver 抽象，封装命令录制与资源创建
  Dx12Handles.h                // 句柄/RAII 封装（资源/视图/heap/命令对象/fence）
  Dx12SamplerCache.h / .cpp    // 采样器描述符缓存
  Dx12Blitter.h / .cpp         // Blit/Copy 封装（可选，后续）
```

## 构建开关
- 建议新增 CMake 选项 `FILAMENT_ENABLE_BACKEND_DX12`，默认 `OFF`，隔离风险。
- 注册平台：`backend/src/PlatformFactory.cpp` 添加 DX12 分支；`backend/include/backend/Platform.h` 增加枚举/工厂入口。
- Dispatcher：`backend/src/CommandStreamDispatcher.h` 为 DX12 生成专用 Dispatcher。
- 构建隔离：当 `FILAMENT_ENABLE_BACKEND_DX12=ON` 时才编译 `backend/src/dx12/*`，其余后端不受影响。

## 里程碑拆解
### 里程碑 1：清屏
- Device/Queue/CommandAllocator/CommandList/SwapChain/RTV 创建。
- 录制：资源状态转 RT → Clear RTV → 转 Present → Present。
- Fence 同步，控制 in-flight 帧数（2~3 帧）。
> 当前状态：骨架文件已放置于 `filament/backend/src/dx12/`，未接入 CMake/Factory，未实现清屏。

### 里程碑 2：三角形
- RootSignature（最小常量/无纹理）。
- PSO（简单 VS/PS，硬编码 HLSL/DXIL）。
- VBO/IBO 上传（Upload Heap → Default Heap）。
- Viewport/Scissor 设置，Draw 调用。

### 里程碑 3：描述符与上传
- CBV/SRV/Sampler Heap 分配器（线性或分页 + free list）。
- 常量缓冲 256B 对齐；纹理 SRV；采样器缓存。
- 简单上传缓冲池（环形或帧内重置）。

### 里程碑 4：Driver API 覆盖（基础）
- Buffer/Texture/RenderTarget/Program/Pipeline/Sampler 创建与销毁。
- RenderPass：RTV/DSV 绑定，Clear/Resolve（MSAA→非 MSAA）。
- Draw/DrawIndexed，Blit/Copy（可后移）。
- Fence/Sync：CPU-GPU 同步，队列 flush。

### 里程碑 5：扩展到 PBR 路径
- 纹理格式映射（sRGB、深度/模板、MSAA）。
- 材质编译链：增加 DX12 目标（HLSL/DXIL），保持现有变体/宏一致。
- MRT 支持，混合/深度/模板状态完整 PSO。
- IBL/后处理：HLSL 版本 Shader 与 PSO，Compute/Graphics 兼容。

## 关键实现要点
- 资源状态与 Barrier：封装 Transition/UAV/alias Barrier；RenderPass 前后状态切换。
- Descriptor Heap 管理：全局 Sampler Heap；CBV/SRV/UAV 可增长或分页；回收策略。
- SwapChain：DXGI；sRGB/HDR；tearing/vsync；resize。
- 同步：Fence/Timeline；present 后 BackBuffer 状态恢复；in-flight 帧保护。
- 调试：PIX 事件标记；DRED/GPU Validation 开关；设备丢失处理。

## 风险与测试
- Shader 管线：需要可靠 HLSL/DXIL 编译链（dxc/fxc）；与材质系统的变体匹配。
- Barrier 正确性：错误的资源状态会导致 GPU hang，需要单元/场景回归。
- 兼容性：多厂商/驱动，HDR/多显示器，MSAA/格式支持矩阵。

## 建议推进顺序
1) 完成里程碑 1（清屏）并加入构建开关，默认关闭。
2) 完成里程碑 2（三角形）验证命令录制/提交/同步正确性。
3) 开启描述符/上传与基本 Driver 映射，跑简单位图采样。
4) 补齐 PSO/材质变体，接通主渲染管线与后处理。
5) 做性能与稳定性调优（Barrier、Heap 回收、Fence 控制）。

