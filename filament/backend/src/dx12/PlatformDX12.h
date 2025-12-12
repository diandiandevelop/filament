/*
 * DX12 Platform skeleton (WIP).
 * This placeholder lets us incrementally add a DX12 backend without touching
 * existing backends or build scripts. The implementation currently returns
 * null driver and a dummy OS version.
 */

#pragma once

#include <backend/Platform.h>

namespace filament::backend {

class PlatformDX12 final : public Platform {
public:
    PlatformDX12() noexcept = default;
    ~PlatformDX12() noexcept override = default;

    struct SwapChainDX12 : public SwapChain {
        void*   nativeWindow = nullptr; // expected HWND on Win32
        uint32_t width = 1280;
        uint32_t height = 720;
    };

    // Returns Windows build number (0 if unavailable).
    int getOSVersion() const noexcept override;

    // TODO: wire up DX12 device/queue/swapchain and return a concrete Driver.
    Driver* UTILS_NULLABLE createDriver(void* UTILS_NULLABLE sharedContext,
            const DriverConfig& driverConfig) override;

    // SwapChain lifecycle（DX12 专用，非基类虚函数）
    SwapChainDX12* createSwapChain(void* nativeWindow, uint64_t flags);
    void destroySwapChain(SwapChainDX12* swapChain);

    // Helper to initialize driver swapchain from the platform SwapChain (DX12 only).
    void initDriverSwapChain(Driver* driver, SwapChainDX12* swapChain);
};

} // namespace filament::backend

