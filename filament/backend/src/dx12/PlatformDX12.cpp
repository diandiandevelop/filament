/*
 * DX12 Platform skeleton (WIP).
 * Not yet compiled by default; build-system gating must be added later
 * (e.g., FILAMENT_ENABLE_BACKEND_DX12).
 */

#include "PlatformDX12.h"

#if defined(FILAMENT_SUPPORTS_DX12)
#include "Dx12Driver.h"
#endif

#if defined(WIN32)
#   include <windows.h>
#endif

namespace filament::backend {

Driver* UTILS_NULLABLE PlatformDX12::createDriver(
        void* UTILS_NULLABLE sharedContext, const DriverConfig& driverConfig) {
    (void)sharedContext;
#if defined(FILAMENT_SUPPORTS_DX12)
    // NOTE: swapchain/window hookup will be handled in Driver later.
    return new Dx12Driver(driverConfig);
#else
    return nullptr;
#endif
}

PlatformDX12::SwapChainDX12* PlatformDX12::createSwapChain(void* nativeWindow, uint64_t flags) {
    (void)flags;
    auto* sc = new SwapChainDX12();
    sc->nativeWindow = nativeWindow; // expected HWND
#if defined(WIN32)
    if (nativeWindow) {
        RECT rc{};
        if (::GetClientRect(static_cast<HWND>(nativeWindow), &rc)) {
            UINT w = rc.right - rc.left;
            UINT h = rc.bottom - rc.top;
            // Clamp to minimal sane size to avoid zero-dimension swapchains.
            sc->width = w > 0 ? w : 1;
            sc->height = h > 0 ? h : 1;
        }
    }
#else
    (void)nativeWindow;
#endif
    // Fallback defaults if window query failed.
    if (sc->width == 0 || sc->height == 0) {
        sc->width = 1280;
        sc->height = 720;
    }
    return sc;
}

void PlatformDX12::destroySwapChain(SwapChainDX12* swapChain) {
    delete swapChain;
}

void PlatformDX12::initDriverSwapChain(Driver* driver, SwapChainDX12* swapChain) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!driver || !swapChain) return;
    auto* dx12 = static_cast<Dx12Driver*>(driver);
    dx12->initSwapChain(swapChain->nativeWindow, swapChain->width, swapChain->height);
#else
    (void)driver;
    (void)swapChain;
#endif
}

int PlatformDX12::getOSVersion() const noexcept {
#if defined(WIN32)
    // Prefer RtlGetVersion (works regardless of manifest); fallback to GetVersionEx.
    using RtlGetVersionPtr = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE hNt = ::GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        auto* rtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
                ::GetProcAddress(hNt, "RtlGetVersion"));
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (rtlGetVersion(&info) == 0) {
                return static_cast<int>(info.dwBuildNumber);
            }
        }
    }

    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
#pragma warning(push)
#pragma warning(disable : 4996) // GetVersionExW is legacy but fine for a build hint
    if (::GetVersionExW(&v)) {
        return static_cast<int>(v.dwBuildNumber);
    }
#pragma warning(pop)
    return 0;
#else
    return 0;
#endif
}

} // namespace filament::backend

