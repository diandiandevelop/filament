/*
 * DX12 handle placeholders (WIP).
 * Define minimal RAII wrappers later; currently only opaque forward declarations
 * to unblock compilation when we start wiring Dx12Driver.
 */

#pragma once

namespace filament::backend {

struct Dx12Device;
struct Dx12CommandQueue;
struct Dx12CommandAllocator;
struct Dx12CommandList;
struct Dx12SwapChain;
struct Dx12DescriptorHeap;
struct Dx12Fence;

} // namespace filament::backend

