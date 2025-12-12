/*
 * DX12 Driver skeleton (WIP).
 * Guarded by FILAMENT_SUPPORTS_DX12 (default OFF) to avoid impacting other builds.
 */

#pragma once

#include "DriverBase.h"
#include <backend/DriverEnums.h>

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
#   include <wrl/client.h>
#   include <d3d12.h>
#   include <dxgi1_6.h>
#   include <unordered_map>
#endif

namespace filament::backend {

class Dx12Driver final : public DriverBase {
public:
    explicit Dx12Driver(DriverConfig const& config) noexcept;
    ~Dx12Driver() noexcept override;

    Dispatcher getDispatcher() const noexcept override;

    // --- Driver interface（使用命令分发宏声明）
    template<typename T>
    friend class ConcreteDispatcher;

#define DECL_DRIVER_API(methodName, paramsDecl, params) \
    UTILS_ALWAYS_INLINE inline void methodName(paramsDecl);

#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params) \
    RetType methodName(paramsDecl) override;

#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params) \
    RetType methodName##S() noexcept override; \
    UTILS_ALWAYS_INLINE inline void methodName##R(RetType, paramsDecl);

#include "private/backend/DriverAPI.inc"

#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_SYNCHRONOUS
#undef DECL_DRIVER_API_RETURN

    // TODO: add resource creation/destroy, render pass, draw, sync, etc.

private:
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    // Minimal DX12 core objects (ComPtr helps lifetime).
    Microsoft::WRL::ComPtr<IDXGIFactory6>          mFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1>          mAdapter;
    Microsoft::WRL::ComPtr<ID3D12Device>           mDevice;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>     mQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3>        mSwapchain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>   mRtvHeap;
    static constexpr UINT                          kBackBufferCount = 2;
    Microsoft::WRL::ComPtr<ID3D12Resource>         mBackbuffers[kBackBufferCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mAllocators[kBackBufferCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCmdList;
    Microsoft::WRL::ComPtr<ID3D12Fence>            mFence;
    HANDLE                                         mFenceEvent = nullptr;
    UINT64                                         mFenceValue = 0;
    UINT                                           mFrameIndex = 0;
    UINT                                           mRtvDescriptorSize = 0;
    HWND                                           mHwnd = nullptr;
    UINT                                           mWidth = 0;
    UINT                                           mHeight = 0;
    bool                                           mSwapchainReady = false;

    // 基础绘制/资源占位（后续补齐）
    Microsoft::WRL::ComPtr<ID3D12RootSignature>    mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>    mPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource>         mVertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW                       mVbView{};

    // 句柄管理（简化实现）
    HandleBase::HandleId                           mNextHandle = 1;
    struct DxSwapChain {
        HWND hwnd = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    std::unordered_map<HandleBase::HandleId, DxSwapChain> mSwapChains;

    struct DxVertexBufferInfo {
        uint8_t bufferCount = 0;
        uint8_t attributeCount = 0;
        AttributeArray attributes{};
    };
    struct DxVertexBuffer {
        uint32_t vertexCount = 0;
        Handle<HwVertexBufferInfo> vbih{};
        Handle<HwBufferObject> buffers[backend::MAX_VERTEX_ATTRIBUTE_COUNT]{};
        D3D12_VERTEX_BUFFER_VIEW views[backend::MAX_VERTEX_ATTRIBUTE_COUNT]{};
    };
    struct DxIndexBuffer {
        uint32_t count = 0;
        DXGI_FORMAT format = DXGI_FORMAT_R16_UINT;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_INDEX_BUFFER_VIEW view{};
    };
    struct DxBufferObject {
        uint32_t byteCount = 0;
        BufferObjectBinding binding = BufferObjectBinding::VERTEX;
        BufferUsage usage = BufferUsage::STATIC;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };
    struct DxRenderPrimitive {
        PrimitiveType type = PrimitiveType::TRIANGLES;
        Handle<HwVertexBuffer> vbh{};
        Handle<HwIndexBuffer> ibh{};
    };

    struct DxRenderTarget {
        Handle<HwTexture> color[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT]{};
        Handle<HwTexture> depth{};
        Handle<HwTexture> stencil{};
        uint8_t samples = 1;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct DxTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap;
        D3D12_RESOURCE_STATES                      state = D3D12_RESOURCE_STATE_COMMON;
        TextureFormat format{};
        TextureUsage usage{};
        SamplerType type{};
        uint8_t levels = 1;
        uint8_t samples = 1;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
    };

    struct DxProgram {
        Microsoft::WRL::ComPtr<ID3DBlob> vs;
        Microsoft::WRL::ComPtr<ID3DBlob> ps;
        Program::DescriptorSetInfo descriptorInfo;
    };

    struct DxDescriptorSet {
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE samplerGpu{};
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvsCpu;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> samplersCpu;
    };

    std::unordered_map<HandleBase::HandleId, DxVertexBufferInfo> mVbInfos;
    std::unordered_map<HandleBase::HandleId, DxVertexBuffer> mVertexBuffers;
    std::unordered_map<HandleBase::HandleId, DxIndexBuffer> mIndexBuffers;
    std::unordered_map<HandleBase::HandleId, DxBufferObject> mBufferObjects;
    std::unordered_map<HandleBase::HandleId, DxRenderPrimitive> mRenderPrimitives;
    std::unordered_map<HandleBase::HandleId, DxRenderTarget> mRenderTargets;
    std::unordered_map<HandleBase::HandleId, DxTexture> mTextures;
    std::unordered_map<HandleBase::HandleId, DxProgram> mPrograms;
    std::unordered_map<HandleBase::HandleId, DxDescriptorSet> mDescriptorSets;
    std::unordered_map<size_t, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPsoCache;
    Handle<HwRenderPrimitive> mCurrentRenderPrimitive{};
    Handle<HwRenderTarget>    mCurrentRenderTarget{};

    // Descriptor heaps（简化全局堆）
    static constexpr UINT kMaxDescriptors = 1024;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
    UINT mSrvDescriptorSize = 0;
    UINT mSamplerDescriptorSize = 0;
    UINT mSrvAllocCursor = 0;
    UINT mSamplerAllocCursor = 0;

    // 简单拓扑映射缓存
    D3D12_PRIMITIVE_TOPOLOGY mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
#else
    struct DeviceStub {};
    DeviceStub* mDevice = nullptr;
#endif

    void initialize() noexcept;  // create device/queue (swapchain needs window)
    void shutdown() noexcept;    // release all resources

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
public:
    // Initialize swapchain/RTV/cmd allocators/list with a native window + size.
    void initSwapChain(void* hwnd, uint32_t width, uint32_t height) noexcept;
    // Recreate swapchain and dependent resources on resize.
    void resizeSwapChain(uint32_t width, uint32_t height) noexcept;
private:
    void releaseSwapChainResources(bool keepSwapchain) noexcept;
    bool createSwapChainResources(HWND hwnd, uint32_t width, uint32_t height, bool createSwapchain) noexcept;
    void waitForGpu(uint64_t fenceValue) noexcept;

    // 资源 / PSO 占位：当前仅声明，后续实现真实上传与编译。
    bool ensureBasicRootSignature() noexcept;
    bool ensureBasicPipelineState() noexcept;
    bool ensureBasicVertexBuffer() noexcept;

    // 格式映射与描述符创建
    DXGI_FORMAT toDxgiFormat(TextureFormat fmt) const noexcept;
    bool isDepthFormat(TextureFormat fmt) const noexcept;
    void ensureSrv(DxTexture& tex) noexcept;
    void ensureRtv(DxTexture& tex) noexcept;
    void ensureDsv(DxTexture& tex) noexcept;

    bool getDefaultShaders(Microsoft::WRL::ComPtr<ID3DBlob>& vs, Microsoft::WRL::ComPtr<ID3DBlob>& ps) noexcept;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> getOrCreatePso(
            const PipelineState& pipelineState,
            DXGI_FORMAT rtvFormat,
            DXGI_FORMAT dsvFormat,
            Microsoft::WRL::ComPtr<ID3DBlob> const& vs,
            Microsoft::WRL::ComPtr<ID3DBlob> const& ps,
            UINT sampleCount) noexcept;
    size_t hashPsoKey(const PipelineState& ps, DXGI_FORMAT rtvFmt, DXGI_FORMAT dsvFmt, UINT sampleCount) const noexcept;
    D3D12_BLEND toDxBlend(BlendFunction f) const noexcept;
    D3D12_BLEND_OP toDxBlendOp(BlendEquation e) const noexcept;
    D3D12_COMPARISON_FUNC toDxCompare(SamplerCompareFunc f) const noexcept;
    D3D12_CULL_MODE toDxCull(CullingMode c) const noexcept;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE toDxTopologyType(PrimitiveType p) const noexcept;

    // 简化的 descriptor 写入
    D3D12_CPU_DESCRIPTOR_HANDLE allocSrvCpu();
    D3D12_GPU_DESCRIPTOR_HANDLE allocSrvGpu(D3D12_CPU_DESCRIPTOR_HANDLE cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE allocSamplerCpu();
    D3D12_GPU_DESCRIPTOR_HANDLE allocSamplerGpu(D3D12_CPU_DESCRIPTOR_HANDLE cpu);
#endif
};

} // namespace filament::backend

