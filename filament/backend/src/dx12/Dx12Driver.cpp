/*
 * DX12 Driver skeleton (WIP).
 * Guarded by FILAMENT_SUPPORTS_DX12; not built by default.
 */

#include "Dx12Driver.h"
#include "CommandStreamDispatcher.h"

#include <cstring>
#include <unordered_map>
#include <algorithm>

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
#include <windows.h>
#include <d3dcompiler.h>
#endif

namespace filament::backend {

Dx12Driver::Dx12Driver(DriverConfig const& config) noexcept
    : DriverBase(config) {
    // TODO: initialize DX12 device/queues/resources here.
    initialize();
}

Dx12Driver::~Dx12Driver() noexcept {
    shutdown();
}

Dispatcher Dx12Driver::getDispatcher() const noexcept {
    return ConcreteDispatcher<Dx12Driver>::make();
}

ShaderModel Dx12Driver::getShaderModel() const noexcept {
#if defined(__ANDROID__) || defined(FILAMENT_IOS) || defined(__EMSCRIPTEN__)
    return ShaderModel::MOBILE;
#else
    return ShaderModel::DESKTOP;
#endif
}

utils::FixedCapacityVector<ShaderLanguage> Dx12Driver::getShaderLanguages(
        ShaderLanguage /*preferredLanguage*/) const noexcept {
    // DX12 路径暂用 SPIRV 占位，后续可改为专用 HLSL/DXIL 选择。
    return { ShaderLanguage::SPIRV };
}

void Dx12Driver::beginFrame(int64_t monotonic_clock_ns, int64_t /*refreshIntervalNs*/, uint32_t frameId) {
    (void)monotonic_clock_ns;
    (void)frameId;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!mSwapchainReady) return;

    if (mWidth == 0 || mHeight == 0) return;

    auto allocator = mAllocators[mFrameIndex].Get();
    auto cmd = mCmdList.Get();
    auto rtvHeap = mRtvHeap.Get();
    if (!allocator || !cmd || !rtvHeap) return;

    // Reset allocator & command list
    if (FAILED(allocator->Reset())) return;
    if (FAILED(cmd->Reset(allocator, nullptr))) return;

    // Transition Present -> RenderTarget
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = mBackbuffers[mFrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // Viewport / Scissor cover the swapchain.
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width  = static_cast<float>(mWidth);
    vp.Height = static_cast<float>(mHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    D3D12_RECT sc{};
    sc.left = 0;
    sc.top = 0;
    sc.right = static_cast<LONG>(mWidth);
    sc.bottom = static_cast<LONG>(mHeight);
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    // RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += mFrameIndex * mRtvDescriptorSize;
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // Clear
    FLOAT clearColor[4] = {0.1f, 0.2f, 0.4f, 1.0f};
    cmd->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // 简单三角形示例：确保 root signature / PSO / VB 已创建。
    if (!ensureBasicRootSignature() || !ensureBasicPipelineState() || !ensureBasicVertexBuffer()) {
        return;
    }
    cmd->SetGraphicsRootSignature(mRootSignature.Get());
    cmd->SetPipelineState(mPipelineState.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &mVbView);
    cmd->DrawInstanced(3, 1, 0, 0);
#else
    (void)monotonic_clock_ns;
    (void)frameId;
#endif
}

void Dx12Driver::endFrame(uint32_t frameId) {
    (void)frameId;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!mSwapchainReady) return;

    auto cmd = mCmdList.Get();
    if (!cmd) return;

    // Transition RenderTarget -> Present
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = mBackbuffers[mFrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // Close & execute
    if (FAILED(cmd->Close())) return;
    ID3D12CommandList* lists[] = { cmd };
    mQueue->ExecuteCommandLists(1, lists);

    // Present
    mSwapchain->Present(1, 0);

    // Signal fence and wait for completion of this frame
    const UINT64 fenceToWait = ++mFenceValue;
    if (FAILED(mQueue->Signal(mFence.Get(), fenceToWait))) return;
    waitForGpu(fenceToWait);

    mFrameIndex = mSwapchain->GetCurrentBackBufferIndex();
#endif
}

void Dx12Driver::flush(int dummy) {
    (void)dummy;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!mQueue || !mFence) return;
    const UINT64 fenceToWait = ++mFenceValue;
    if (FAILED(mQueue->Signal(mFence.Get(), fenceToWait))) return;
    waitForGpu(fenceToWait);
#endif
}

void Dx12Driver::initialize() noexcept {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    UINT flags = 0;
#if defined(_DEBUG)
    // Enable debug layer if available.
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
            flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    // Factory
    if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&mFactory)))) {
        return;
    }

    // Choose hardware adapter
    for (UINT i = 0; mFactory->EnumAdapters1(i, &mAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        mAdapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(mAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                _uuidof(ID3D12Device), nullptr))) {
            break;
        }
        mAdapter.Reset();
    }
    if (!mAdapter) {
        // fallback: WARP
        if (FAILED(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&mAdapter)))) {
            return;
        }
    }

    if (FAILED(D3D12CreateDevice(mAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&mDevice)))) {
        return;
    }

    // Command queue
    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    qdesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(mDevice->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&mQueue)))) {
        return;
    }

    // 全局 SRV/CBV/UAV 与 Sampler 描述符堆（简化版）。
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.NumDescriptors = kMaxDescriptors;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(mDevice->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&mSrvHeap)))) {
        return;
    }
    mSrvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc{};
    samplerDesc.NumDescriptors = kMaxDescriptors;
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(mDevice->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&mSamplerHeap)))) {
        return;
    }
    mSamplerDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // SwapChain/RTV/cmd list need a native window; call initSwapChain() later when a swapchain is provided.
#else
    // Non-DX12 build: no-op.
#endif
}

void Dx12Driver::shutdown() noexcept {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    mSwapchainReady = false;
    // Make sure GPU work is complete before releasing resources.
    if (mQueue && mFence) {
        const UINT64 fenceToWait = ++mFenceValue;
        if (SUCCEEDED(mQueue->Signal(mFence.Get(), fenceToWait))) {
            waitForGpu(fenceToWait);
        }
    }
    if (mFenceEvent) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
    mFence.Reset();
    mCmdList.Reset();
    for (auto& a : mAllocators) a.Reset();
    for (auto& b : mBackbuffers) b.Reset();
    mRtvHeap.Reset();
    mSwapchain.Reset();
    mQueue.Reset();
    mDevice.Reset();
    mAdapter.Reset();
    mFactory.Reset();
    mRootSignature.Reset();
    mPipelineState.Reset();
    mVertexBuffer.Reset();
#endif
}

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
void Dx12Driver::initSwapChain(void* hwnd, uint32_t width, uint32_t height) noexcept {
    if (!mDevice || !mQueue || !mFactory) return;
    if (mSwapchain) return; // already created
    mWidth = width;
    mHeight = height;
    mHwnd = static_cast<HWND>(hwnd);
    createSwapChainResources(mHwnd, width, height, /*createSwapchain*/ true);
}

void Dx12Driver::waitForGpu(uint64_t fenceValue) noexcept {
    if (!mFence || !mFenceEvent) return;
    if (mFence->GetCompletedValue() < fenceValue) {
        mFence->SetEventOnCompletion(fenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
}

void Dx12Driver::releaseSwapChainResources(bool keepSwapchain) noexcept {
    mSwapchainReady = false;
    mCmdList.Reset();
    for (auto& a : mAllocators) a.Reset();
    for (auto& b : mBackbuffers) b.Reset();
    mRtvHeap.Reset();
    if (!keepSwapchain) {
        mSwapchain.Reset();
    }
}

bool Dx12Driver::createSwapChainResources(HWND hwnd, uint32_t width, uint32_t height, bool createSwapchain) noexcept {
    if (createSwapchain) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.BufferCount = kBackBufferCount;
        desc.Width  = width ? width : 1;
        desc.Height = height ? height : 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.SampleDesc.Count = 1;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap1;
        if (FAILED(mFactory->CreateSwapChainForHwnd(
                mQueue.Get(), hwnd, &desc,
                nullptr, nullptr, &swap1))) {
            return false;
        }
        if (FAILED(swap1.As(&mSwapchain))) {
            return false;
        }
    } else {
        if (!mSwapchain) return false;
    }

    // Refresh width/height from the swapchain in case the requested size was adjusted by DXGI.
    DXGI_SWAP_CHAIN_DESC1 realDesc{};
    if (SUCCEEDED(mSwapchain->GetDesc1(&realDesc))) {
        mWidth = realDesc.Width;
        mHeight = realDesc.Height;
    }

    mFrameIndex = mSwapchain->GetCurrentBackBufferIndex();

    // RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = kBackBufferCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvHeap)))) {
        return false;
    }
    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Backbuffers + RTV + allocators
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        if (FAILED(mSwapchain->GetBuffer(i, IID_PPV_ARGS(&mBackbuffers[i])))) return false;
        mDevice->CreateRenderTargetView(mBackbuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += mRtvDescriptorSize;
        if (FAILED(mDevice->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mAllocators[i])))) return false;
    }

    // Command list
    if (FAILED(mDevice->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, mAllocators[mFrameIndex].Get(),
            nullptr, IID_PPV_ARGS(&mCmdList)))) {
        return false;
    }
    mCmdList->Close(); // will be reset in beginFrame

    // Fence + event
    if (!mFence) {
        if (FAILED(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)))) {
            return false;
        }
        mFenceValue = 0;
    }
    if (!mFenceEvent) {
        mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!mFenceEvent) return false;
    }

    mSwapchainReady = true;
    return true;
}

void Dx12Driver::resizeSwapChain(uint32_t width, uint32_t height) noexcept {
    if (!mSwapchain || !mQueue || !mFence) return;

    // Finish pending GPU work before resizing.
    const UINT64 fenceToWait = ++mFenceValue;
    if (FAILED(mQueue->Signal(mFence.Get(), fenceToWait))) return;
    waitForGpu(fenceToWait);

    // Release resources and resize swapchain buffers.
    releaseSwapChainResources(/*keepSwapchain*/ true);
    mWidth = width;
    mHeight = height;

    // Resize on the existing swapchain object if possible; otherwise recreate.
    if (FAILED(mSwapchain->ResizeBuffers(kBackBufferCount, width, height,
            DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
        // If ResizeBuffers failed, drop and recreate from scratch.
        mSwapchain.Reset();
    }

    if (!mSwapchain) {
        // Recreate from scratch using stored hwnd.
        if (!mHwnd) return;
        createSwapChainResources(mHwnd, width, height, /*createSwapchain*/ true);
        return;
    }

    // Recreate backbuffer-dependent resources using existing swapchain.
    createSwapChainResources(nullptr, width, height, /*createSwapchain*/ false);
}
#endif

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
bool Dx12Driver::ensureBasicRootSignature() noexcept {
    if (mRootSignature) return true;
    // 简化 root signature：slot0 SRV/CBV/UAV 表，slot1 Sampler 表。
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = UINT_MAX;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[1].NumDescriptors = UINT_MAX;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = UINT_MAX;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &samplerRange;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> sigBlob;
    Microsoft::WRL::ComPtr<ID3DBlob> errBlob;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob, &errBlob))) {
        return false;
    }
    if (FAILED(mDevice->CreateRootSignature(
            0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&mRootSignature)))) {
        return false;
    }
    return true;
}

bool Dx12Driver::ensureBasicPipelineState() noexcept {
    // 保留旧接口以兼容已有调用，内部使用默认 shader + swapchain RTV 格式。
    Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
    if (!getDefaultShaders(vs, ps)) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    pso.pRootSignature = mRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.SampleMask = UINT_MAX;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    pso.RasterizerState = {
        D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE,
        D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    };
    pso.BlendState.AlphaToCoverageEnable = FALSE;
    pso.BlendState.IndependentBlendEnable = FALSE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    if (FAILED(mDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&mPipelineState)))) {
        return false;
    }
    return true;
}

bool Dx12Driver::getDefaultShaders(Microsoft::WRL::ComPtr<ID3DBlob>& vs, Microsoft::WRL::ComPtr<ID3DBlob>& ps) noexcept {
    static Microsoft::WRL::ComPtr<ID3DBlob> sVS;
    static Microsoft::WRL::ComPtr<ID3DBlob> sPS;
    if (sVS && sPS) { vs = sVS; ps = sPS; return true; }

    // 简单 VS/PS：插值颜色输出。
    static const char* kVS = R"(struct VSIn { float3 pos : POSITION; float3 col : COLOR; };
struct VSOut { float4 pos : SV_Position; float3 col : COLOR; };
VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; })";

    static const char* kPS = R"(struct PSIn { float4 pos : SV_Position; float3 col : COLOR; };
float4 main(PSIn i) : SV_Target { return float4(i.col, 1.0); })";

    auto compile = [](const char* src, const char* entry, const char* target,
            Microsoft::WRL::ComPtr<ID3DBlob>& out) -> bool {
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if defined(_DEBUG)
        flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
                entry, target, flags, 0, &out, &errors);
        return SUCCEEDED(hr);
    };

    if (!compile(kVS, "main", "vs_5_0", sVS)) return false;
    if (!compile(kPS, "main", "ps_5_0", sPS)) return false;
    vs = sVS; ps = sPS;
    return true;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> Dx12Driver::getOrCreatePso(
        const PipelineState& pipelineState,
        DXGI_FORMAT rtvFormat,
        DXGI_FORMAT dsvFormat,
        Microsoft::WRL::ComPtr<ID3DBlob> const& vs,
        Microsoft::WRL::ComPtr<ID3DBlob> const& ps,
        UINT sampleCount) noexcept {
    const size_t key = hashPsoKey(pipelineState, rtvFormat, dsvFormat, sampleCount);
    auto it = mPsoCache.find(key);
    if (it != mPsoCache.end()) return it->second;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    pso.pRootSignature = mRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.InputLayout = { layout, static_cast<UINT>(std::size(layout)) };
    pso.PrimitiveTopologyType = toDxTopologyType(pipelineState.primitiveType);
    pso.SampleMask = UINT_MAX;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = rtvFormat;
    pso.DSVFormat = dsvFormat;
    pso.SampleDesc.Count = sampleCount ? sampleCount : 1;

    // Raster state
    // Raster
    const auto& rs = pipelineState.rasterState;
    pso.RasterizerState = {
        D3D12_FILL_MODE_SOLID,
        toDxCull(rs.culling),
        rs.inverseFrontFaces ? TRUE : FALSE,
        D3D12_DEFAULT_DEPTH_BIAS, D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        TRUE, FALSE, rs.depthClamp, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    };

    // Depth/stencil
    pso.DepthStencilState.DepthEnable = rs.depthWrite || rs.depthFunc != SamplerCompareFunc::A;
    pso.DepthStencilState.DepthFunc = toDxCompare(rs.depthFunc);
    pso.DepthStencilState.StencilEnable = FALSE;

    // Blend
    D3D12_RENDER_TARGET_BLEND_DESC rtBlend{};
    rtBlend.BlendEnable = rs.hasBlending();
    rtBlend.RenderTargetWriteMask = rs.colorWrite ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
    rtBlend.SrcBlend = toDxBlend(rs.blendFunctionSrcRGB);
    rtBlend.DestBlend = toDxBlend(rs.blendFunctionDstRGB);
    rtBlend.BlendOp = toDxBlendOp(rs.blendEquationRGB);
    rtBlend.SrcBlendAlpha = toDxBlend(rs.blendFunctionSrcAlpha);
    rtBlend.DestBlendAlpha = toDxBlend(rs.blendFunctionDstAlpha);
    rtBlend.BlendOpAlpha = toDxBlendOp(rs.blendEquationAlpha);
    pso.BlendState.AlphaToCoverageEnable = rs.alphaToCoverage;
    pso.BlendState.IndependentBlendEnable = FALSE;
    pso.BlendState.RenderTarget[0] = rtBlend;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoObj;
    if (SUCCEEDED(mDevice->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&psoObj)))) {
        mPsoCache[key] = psoObj;
        return psoObj;
    }
    return nullptr;
}

size_t Dx12Driver::hashPsoKey(const PipelineState& ps, DXGI_FORMAT rtvFmt, DXGI_FORMAT dsvFmt, UINT sampleCount) const noexcept {
    size_t h = 1469598103934665603ull;
    auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h<<6) + (h>>2); };
    mix(static_cast<size_t>(rtvFmt));
    mix(static_cast<size_t>(dsvFmt));
    mix(static_cast<size_t>(sampleCount));
    mix(static_cast<size_t>(ps.rasterState.u));
    mix(static_cast<size_t>(ps.stencilState.front.readMask));
    mix(static_cast<size_t>(ps.stencilState.front.writeMask));
    mix(static_cast<size_t>(ps.primitiveType));
    return h;
}

D3D12_BLEND Dx12Driver::toDxBlend(BlendFunction f) const noexcept {
    switch (f) {
        case BlendFunction::ZERO: return D3D12_BLEND_ZERO;
        case BlendFunction::ONE: return D3D12_BLEND_ONE;
        case BlendFunction::SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
        case BlendFunction::ONE_MINUS_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFunction::DST_COLOR: return D3D12_BLEND_DEST_COLOR;
        case BlendFunction::ONE_MINUS_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFunction::SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
        case BlendFunction::ONE_MINUS_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFunction::DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
        case BlendFunction::ONE_MINUS_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP Dx12Driver::toDxBlendOp(BlendEquation e) const noexcept {
    switch (e) {
        case BlendEquation::ADD: return D3D12_BLEND_OP_ADD;
        case BlendEquation::SUBTRACT: return D3D12_BLEND_OP_SUBTRACT;
        case BlendEquation::REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendEquation::MIN: return D3D12_BLEND_OP_MIN;
        case BlendEquation::MAX: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_ADD;
    }
}

D3D12_COMPARISON_FUNC Dx12Driver::toDxCompare(SamplerCompareFunc f) const noexcept {
    switch (f) {
        case SamplerCompareFunc::A: return D3D12_COMPARISON_FUNC_ALWAYS;
        case SamplerCompareFunc::E: return D3D12_COMPARISON_FUNC_EQUAL;
        case SamplerCompareFunc::GE: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case SamplerCompareFunc::G: return D3D12_COMPARISON_FUNC_GREATER;
        case SamplerCompareFunc::LE: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case SamplerCompareFunc::L: return D3D12_COMPARISON_FUNC_LESS;
        case SamplerCompareFunc::NE: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        default: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

D3D12_CULL_MODE Dx12Driver::toDxCull(CullingMode c) const noexcept {
    switch (c) {
        case CullingMode::NONE: return D3D12_CULL_MODE_NONE;
        case CullingMode::FRONT: return D3D12_CULL_MODE_FRONT;
        case CullingMode::BACK: return D3D12_CULL_MODE_BACK;
        default: return D3D12_CULL_MODE_BACK;
    }
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE Dx12Driver::toDxTopologyType(PrimitiveType p) const noexcept {
    switch (p) {
        case PrimitiveType::POINTS:         return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        case PrimitiveType::LINES:
        case PrimitiveType::LINE_STRIP:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case PrimitiveType::TRIANGLES:
        case PrimitiveType::TRIANGLE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}
#endif

bool Dx12Driver::ensureBasicVertexBuffer() noexcept {
    if (mVertexBuffer) return true;
    struct Vertex {
        float pos[3];
        float col[3];
    };
    const Vertex vertices[] = {
        { { 0.0f,  0.25f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.25f, -0.25f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
        { { -0.25f, -0.25f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
    const UINT vbSize = static_cast<UINT>(sizeof(vertices));

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resDesc.Alignment = 0;
    resDesc.Width = vbSize;
    resDesc.Height = 1;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_UNKNOWN;
    resDesc.SampleDesc.Count = 1;
    resDesc.SampleDesc.Quality = 0;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(mDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&mVertexBuffer)))) {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE range{0, 0};
    if (FAILED(mVertexBuffer->Map(0, &range, &mapped))) {
        return false;
    }
    std::memcpy(mapped, vertices, vbSize);
    mVertexBuffer->Unmap(0, nullptr);

    mVbView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVbView.SizeInBytes = vbSize;
    mVbView.StrideInBytes = sizeof(Vertex);
    return true;
}
#endif

// ===== Driver API 其余接口暂以占位实现，便于框架对齐 =====

void Dx12Driver::terminate() {
    // TODO(zh-cn): DX12 资源终止清理，可调用 shutdown。
}

void Dx12Driver::tick(int) {
}

void Dx12Driver::setFrameScheduledCallback(Handle<HwSwapChain> sch,
        CallbackHandler* handler, FrameScheduledCallback&& callback, uint64_t flags) {
}

void Dx12Driver::setFrameCompletedCallback(Handle<HwSwapChain> sch,
        CallbackHandler* handler, utils::Invocable<void(void)>&& callback) {
}

void Dx12Driver::setPresentationTime(int64_t monotonic_clock_ns) {
}

void Dx12Driver::finish(int) {
}

void Dx12Driver::destroyRenderPrimitive(Handle<HwRenderPrimitive> rph) {
}

void Dx12Driver::destroyVertexBufferInfo(Handle<HwVertexBufferInfo> vbih) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (vbih) mVbInfos.erase(vbih.getId());
#else
    (void)vbih;
#endif
}

void Dx12Driver::destroyVertexBuffer(Handle<HwVertexBuffer> vbh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (vbh) mVertexBuffers.erase(vbh.getId());
#else
    (void)vbh;
#endif
}

void Dx12Driver::destroyIndexBuffer(Handle<HwIndexBuffer> ibh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (ibh) mIndexBuffers.erase(ibh.getId());
#else
    (void)ibh;
#endif
}

void Dx12Driver::destroyBufferObject(Handle<HwBufferObject> boh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (boh) mBufferObjects.erase(boh.getId());
#else
    (void)boh;
#endif
}

void Dx12Driver::destroyTexture(Handle<HwTexture> th) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (th) {
        mTextures.erase(th.getId());
    }
#else
    (void)th;
#endif
}

void Dx12Driver::destroyProgram(Handle<HwProgram> ph) {
}

void Dx12Driver::destroyRenderTarget(Handle<HwRenderTarget> rth) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (rth) {
        mRenderTargets.erase(rth.getId());
    }
#else
    (void)rth;
#endif
}

void Dx12Driver::destroySwapChain(Handle<HwSwapChain> sch) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!sch) return;
    auto id = sch.getId();
    auto it = mSwapChains.find(id);
    if (it != mSwapChains.end()) {
        flush(0);
        releaseSwapChainResources(/*keepSwapchain*/ false);
        mSwapChains.erase(it);
    }
#else
    (void)sch;
#endif
}

void Dx12Driver::destroyStream(Handle<HwStream> sh) {
}

void Dx12Driver::destroySync(Handle<HwSync> sh) {
}

void Dx12Driver::destroyTimerQuery(Handle<HwTimerQuery> tqh) {
}

void Dx12Driver::destroyDescriptorSetLayout(Handle<HwDescriptorSetLayout> tqh) {
}

void Dx12Driver::destroyDescriptorSet(Handle<HwDescriptorSet> tqh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    (void)tqh;
#endif
}

Handle<HwStream> Dx12Driver::createStreamNative(void* nativeStream, utils::ImmutableCString tag) {
    return {};
}

Handle<HwStream> Dx12Driver::createStreamAcquired(utils::ImmutableCString tag) {
    return {};
}

void Dx12Driver::setAcquiredImage(Handle<HwStream> sh, void* image, const math::mat3f& transform,
        CallbackHandler* handler, StreamCallback cb, void* userData) {
}

void Dx12Driver::setStreamDimensions(Handle<HwStream> sh, uint32_t width, uint32_t height) {
}

int64_t Dx12Driver::getStreamTimestamp(Handle<HwStream> sh) {
    return 0;
}

void Dx12Driver::updateStreams(CommandStream* driver) {
}

void Dx12Driver::getPlatformSync(Handle<HwSync> sh, CallbackHandler* handler,
        Platform::SyncCallback cb, void* userData) {
}

void Dx12Driver::destroyFence(Handle<HwFence> fh) {
}

void Dx12Driver::fenceCancel(FenceHandle fh) {
}

FenceStatus Dx12Driver::getFenceStatus(Handle<HwFence> fh) {
    return FenceStatus::CONDITION_SATISFIED;
}

FenceStatus Dx12Driver::fenceWait(Handle<HwFence> fh, uint64_t timeout) {
    return FenceStatus::ERROR;
}

bool Dx12Driver::isTextureFormatSupported(TextureFormat format) {
    return true;
}

bool Dx12Driver::isTextureSwizzleSupported() {
    return true;
}

bool Dx12Driver::isTextureFormatMipmappable(TextureFormat format) {
    return true;
}

bool Dx12Driver::isRenderTargetFormatSupported(TextureFormat format) {
    return true;
}

bool Dx12Driver::isFrameBufferFetchSupported() {
    return false;
}

bool Dx12Driver::isFrameBufferFetchMultiSampleSupported() {
    return false;
}

bool Dx12Driver::isFrameTimeSupported() {
    return true;
}

bool Dx12Driver::isAutoDepthResolveSupported() {
    return true;
}

bool Dx12Driver::isSRGBSwapChainSupported() {
    return false;
}

bool Dx12Driver::isMSAASwapChainSupported(uint32_t) {
    return false;
}

bool Dx12Driver::isProtectedContentSupported() {
    return false;
}

bool Dx12Driver::isStereoSupported() {
    return false;
}

bool Dx12Driver::isParallelShaderCompileSupported() {
    return false;
}

bool Dx12Driver::isDepthStencilResolveSupported() {
    return true;
}

bool Dx12Driver::isDepthStencilBlitSupported(TextureFormat format) {
    return true;
}

bool Dx12Driver::isProtectedTexturesSupported() {
    return true;
}

bool Dx12Driver::isDepthClampSupported() {
    return false;
}

bool Dx12Driver::isWorkaroundNeeded(Workaround) {
    return false;
}

FeatureLevel Dx12Driver::getFeatureLevel() {
    return FeatureLevel::FEATURE_LEVEL_1;
}

math::float2 Dx12Driver::getClipSpaceParams() {
    return math::float2{ 1.0f, 0.0f };
}

uint8_t Dx12Driver::getMaxDrawBuffers() {
    return MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT;
}

size_t Dx12Driver::getMaxUniformBufferSize() {
    return 16384u;
}

size_t Dx12Driver::getMaxTextureSize(SamplerType target) {
    return 16384u;
}

size_t Dx12Driver::getMaxArrayTextureLayers() {
    return 256u;
}

size_t Dx12Driver::getUniformBufferOffsetAlignment() {
    return 256u;
}

void Dx12Driver::updateIndexBuffer(Handle<HwIndexBuffer> ibh, BufferDescriptor&& p,
        uint32_t byteOffset) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    auto it = mIndexBuffers.find(ibh.getId());
    if (it != mIndexBuffers.end() && it->second.resource && p.size + byteOffset <= it->second.view.SizeInBytes) {
        void* mapped = nullptr;
        D3D12_RANGE range{0, 0};
        if (SUCCEEDED(it->second.resource->Map(0, &range, &mapped))) {
            std::memcpy(static_cast<uint8_t*>(mapped) + byteOffset, p.buffer, p.size);
            it->second.resource->Unmap(0, nullptr);
        }
    }
#endif
    scheduleDestroy(std::move(p));
}

void Dx12Driver::updateBufferObject(Handle<HwBufferObject> ibh, BufferDescriptor&& p,
        uint32_t byteOffset) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    auto it = mBufferObjects.find(ibh.getId());
    if (it != mBufferObjects.end() && it->second.resource && p.size + byteOffset <= it->second.byteCount) {
        void* mapped = nullptr;
        D3D12_RANGE range{0, 0};
        if (SUCCEEDED(it->second.resource->Map(0, &range, &mapped))) {
            std::memcpy(static_cast<uint8_t*>(mapped) + byteOffset, p.buffer, p.size);
            it->second.resource->Unmap(0, nullptr);
        }
    }
#endif
    scheduleDestroy(std::move(p));
}

void Dx12Driver::updateBufferObjectUnsynchronized(Handle<HwBufferObject> ibh, BufferDescriptor&& p,
        uint32_t byteOffset) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    updateBufferObject(ibh, std::move(p), byteOffset);
#else
    scheduleDestroy(std::move(p));
#endif
}

void Dx12Driver::resetBufferObject(Handle<HwBufferObject> boh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    // 重新创建上传堆，避免遗留数据；若后续需要 Default 堆可扩展。
    auto it = mBufferObjects.find(boh.getId());
    if (it == mBufferObjects.end() || !mDevice) return;
    auto byteCount = it->second.byteCount;
    DxBufferObject bo = it->second;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteCount;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    bo.resource.Reset();
    if (SUCCEEDED(mDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&bo.resource)))) {
        mBufferObjects[boh.getId()] = bo;
    }
#else
    (void)boh;
#endif
}

void Dx12Driver::setVertexBufferObject(Handle<HwVertexBuffer> vbh, uint32_t index,
        Handle<HwBufferObject> boh) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    auto vit = mVertexBuffers.find(vbh.getId());
    auto bit = mBufferObjects.find(boh.getId());
    if (vit == mVertexBuffers.end() || bit == mBufferObjects.end()) return;
    DxVertexBuffer& vb = vit->second;
    if (index >= backend::MAX_VERTEX_ATTRIBUTE_COUNT) return;
    vb.buffers[index] = boh;

    // 基于关联的 VertexBufferInfo 计算 stride / offset。
    auto infoIt = mVbInfos.find(vb.vbih.getId());
    if (infoIt == mVbInfos.end()) return;
    const DxVertexBufferInfo& info = infoIt->second;

    // 找到该 buffer index 的最大 (offset + size) 作为 stride。
    uint32_t stride = 0;
    for (uint8_t i = 0; i < info.attributeCount; ++i) {
        const auto& attr = info.attributes[i];
        if (attr.buffer == index) {
            const uint32_t sz = Driver::getElementTypeSize(attr.type);
            stride = std::max<uint32_t>(stride, attr.offset + sz);
        }
    }
    DxBufferObject const& bo = bit->second;
    vb.views[index].BufferLocation = bo.resource ? bo.resource->GetGPUVirtualAddress() : 0;
    vb.views[index].SizeInBytes = bo.byteCount;
    vb.views[index].StrideInBytes = stride;
#else
    (void)vbh; (void)index; (void)boh;
#endif
}

void Dx12Driver::update3DImage(Handle<HwTexture> th,
        uint32_t level, uint32_t xoffset, uint32_t yoffset, uint32_t zoffset,
        uint32_t width, uint32_t height, uint32_t depth,
        PixelBufferDescriptor&& data) {
    scheduleDestroy(std::move(data));
}

void Dx12Driver::setupExternalImage2(Platform::ExternalImageHandleRef image) {
}

void Dx12Driver::setupExternalImage(void* image) {
}

TimerQueryResult Dx12Driver::getTimerQueryValue(Handle<HwTimerQuery> tqh, uint64_t* elapsedTime) {
    return TimerQueryResult::ERROR;
}

void Dx12Driver::setExternalStream(Handle<HwTexture> th, Handle<HwStream> sh) {
}

void Dx12Driver::generateMipmaps(Handle<HwTexture> th) {
}

void Dx12Driver::compilePrograms(CompilerPriorityQueue priority,
        CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {
    if (callback) {
        scheduleCallback(handler, user, callback);
    }
}

void Dx12Driver::beginRenderPass(Handle<HwRenderTarget> rth, const RenderPassParams& params) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    auto cmd = mCmdList.Get();
    if (!cmd) return;

    mCurrentRenderTarget = rth;

    // 选择 RenderTarget：若未提供则使用 swapchain。
    DxRenderTarget* rt = nullptr;
    if (rth) {
        auto it = mRenderTargets.find(rth.getId());
        if (it != mRenderTargets.end()) rt = &it->second;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    bool hasRTV = false, hasDSV = false;
    uint32_t width = mWidth;
    uint32_t height = mHeight;

    if (rt) {
        width = rt->width;
        height = rt->height;
        // 仅使用第一个颜色附件
        if (rt->color[0]) {
            auto texIt = mTextures.find(rt->color[0].getId());
            if (texIt != mTextures.end() && texIt->second.rtvHeap) {
                rtv = texIt->second.rtvHeap->GetCPUDescriptorHandleForHeapStart();
                hasRTV = true;
            }
        }
        if (rt->depth) {
            auto texIt = mTextures.find(rt->depth.getId());
            if (texIt != mTextures.end() && texIt->second.dsvHeap) {
                dsv = texIt->second.dsvHeap->GetCPUDescriptorHandleForHeapStart();
                hasDSV = true;
            }
        }
    } else {
        // 使用 swapchain backbuffer RTV
        auto rtvHeap = mRtvHeap.Get();
        if (rtvHeap) {
            rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += mFrameIndex * mRtvDescriptorSize;
            hasRTV = true;
        }
    }

    // 设置 viewport/scissor
    Viewport vp = params.viewport;
    if (vp.width == 0 || vp.height == 0) {
        vp.left = 0;
        vp.bottom = 0;
        vp.width = width;
        vp.height = height;
    }
    D3D12_VIEWPORT d3dVp{};
    d3dVp.TopLeftX = static_cast<float>(vp.left);
    d3dVp.TopLeftY = static_cast<float>(vp.bottom);
    d3dVp.Width  = static_cast<float>(vp.width);
    d3dVp.Height = static_cast<float>(vp.height);
    d3dVp.MinDepth = static_cast<float>(params.depthRange.near);
    d3dVp.MaxDepth = static_cast<float>(params.depthRange.far);
    D3D12_RECT sc{};
    sc.left = static_cast<LONG>(vp.left);
    sc.top = static_cast<LONG>(vp.bottom);
    sc.right = static_cast<LONG>(vp.left + vp.width);
    sc.bottom = static_cast<LONG>(vp.bottom + vp.height);
    cmd->RSSetViewports(1, &d3dVp);
    cmd->RSSetScissorRects(1, &sc);

    // 绑定 RT/DS，并进行必要的状态转换
    if (hasRTV) {
        auto it = rt ? mTextures.find(rt->color[0].getId()) : mTextures.end();
        if (it != mTextures.end() && it->second.resource) {
            if (it->second.state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = it->second.resource.Get();
                barrier.Transition.StateBefore = it->second.state;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmd->ResourceBarrier(1, &barrier);
                it->second.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            }
        }
    }
    if (hasDSV) {
        auto it = rt ? mTextures.find(rt->depth.getId()) : mTextures.end();
        if (it != mTextures.end() && it->second.resource) {
            if (it->second.state != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = it->second.resource.Get();
                barrier.Transition.StateBefore = it->second.state;
                barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_DEPTH_WRITE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmd->ResourceBarrier(1, &barrier);
                it->second.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            }
        }
    }

    // 绑定 RT/DS
    if (hasRTV || hasDSV) {
        cmd->OMSetRenderTargets(hasRTV ? 1 : 0, hasRTV ? &rtv : nullptr, FALSE, hasDSV ? &dsv : nullptr);
    }

    // 清除
    if (hasRTV && (params.flags.clear & TargetBufferFlags::COLOR)) {
        FLOAT c[4] = { params.clearColor.r, params.clearColor.g, params.clearColor.b, params.clearColor.a };
        cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    }
    if (hasDSV && ((params.flags.clear & TargetBufferFlags::DEPTH) || (params.flags.clear & TargetBufferFlags::STENCIL))) {
        UINT clearFlags = 0;
        if (params.flags.clear & TargetBufferFlags::DEPTH)   clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
        if (params.flags.clear & TargetBufferFlags::STENCIL) clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
        cmd->ClearDepthStencilView(dsv, clearFlags,
                static_cast<float>(params.clearDepth), params.clearStencil, 0, nullptr);
    }
#else
    (void)rth; (void)params;
#endif
}

void Dx12Driver::endRenderPass(int) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    auto cmd = mCmdList.Get();
    if (!cmd) return;

    // 将当前 RenderTarget 资源回迁至 COMMON，便于后续使用。
    if (mCurrentRenderTarget) {
        auto rtIt = mRenderTargets.find(mCurrentRenderTarget.getId());
        if (rtIt != mRenderTargets.end()) {
            DxRenderTarget& rt = rtIt->second;
            if (rt.color[0]) {
                auto it = mTextures.find(rt.color[0].getId());
                if (it != mTextures.end() && it->second.resource &&
                        it->second.state != D3D12_RESOURCE_STATE_COMMON) {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = it->second.resource.Get();
                    barrier.Transition.StateBefore = it->second.state;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    cmd->ResourceBarrier(1, &barrier);
                    it->second.state = D3D12_RESOURCE_STATE_COMMON;
                }
            }
            if (rt.depth) {
                auto it = mTextures.find(rt.depth.getId());
                if (it != mTextures.end() && it->second.resource &&
                        it->second.state != D3D12_RESOURCE_STATE_COMMON) {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition.pResource = it->second.resource.Get();
                    barrier.Transition.StateBefore = it->second.state;
                    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
                    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    cmd->ResourceBarrier(1, &barrier);
                    it->second.state = D3D12_RESOURCE_STATE_COMMON;
                }
            }
        }
    }
#endif
}

void Dx12Driver::nextSubpass(int) {
}

void Dx12Driver::makeCurrent(Handle<HwSwapChain> drawSch, Handle<HwSwapChain> readSch) {
}

void Dx12Driver::commit(Handle<HwSwapChain> sch) {
}

void Dx12Driver::setPushConstant(ShaderStage stage, uint8_t index,
        PushConstantVariant value) {
}

void Dx12Driver::insertEventMarker(char const* string) {
}

void Dx12Driver::pushGroupMarker(char const* string) {
}

void Dx12Driver::popGroupMarker(int) {
}

void Dx12Driver::startCapture(int) {
}

void Dx12Driver::stopCapture(int) {
}

void Dx12Driver::readPixels(Handle<HwRenderTarget> src,
        uint32_t x, uint32_t y, uint32_t width, uint32_t height,
        PixelBufferDescriptor&& p) {
    scheduleDestroy(std::move(p));
}

void Dx12Driver::readBufferSubData(BufferObjectHandle boh,
        uint32_t offset, uint32_t size, BufferDescriptor&& p) {
    scheduleDestroy(std::move(p));
}

void Dx12Driver::blitDEPRECATED(TargetBufferFlags buffers,
        Handle<HwRenderTarget> dst, Viewport dstRect,
        Handle<HwRenderTarget> src, Viewport srcRect,
        SamplerMagFilter filter) {
}

void Dx12Driver::resolve(
        Handle<HwTexture> dst, uint8_t srcLevel, uint8_t srcLayer,
        Handle<HwTexture> src, uint8_t dstLevel, uint8_t dstLayer) {
}

void Dx12Driver::blit(
        Handle<HwTexture> dst, uint8_t srcLevel, uint8_t srcLayer, math::uint2 dstOrigin,
        Handle<HwTexture> src, uint8_t dstLevel, uint8_t dstLayer, math::uint2 srcOrigin,
        math::uint2 size) {
}

void Dx12Driver::bindPipeline(PipelineState const& pipelineState) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    ensureBasicRootSignature();

    // 着色器：优先使用 Program 中的 VS/PS；否则 fallback 默认。
    Microsoft::WRL::ComPtr<ID3DBlob> vs;
    Microsoft::WRL::ComPtr<ID3DBlob> ps;
    auto progIt = mPrograms.find(pipelineState.program.getId());
    if (progIt != mPrograms.end()) {
        vs = progIt->second.vs;
        ps = progIt->second.ps;
    }
    if (!vs || !ps) {
        if (!getDefaultShaders(vs, ps)) return;
    }

    // 选择 RTV/DSV 格式与样本数：默认使用 swapchain 格式。
    DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
    UINT sampleCount = 1;
    if (mCurrentRenderTarget) {
        auto rtIt = mRenderTargets.find(mCurrentRenderTarget.getId());
        if (rtIt != mRenderTargets.end()) {
            const DxRenderTarget& rt = rtIt->second;
            sampleCount = rt.samples ? rt.samples : 1;
            if (rt.color[0]) {
                auto it = mTextures.find(rt.color[0].getId());
                if (it != mTextures.end()) rtvFormat = toDxgiFormat(it->second.format);
            }
            if (rt.depth) {
                auto it = mTextures.find(rt.depth.getId());
                if (it != mTextures.end()) dsvFormat = toDxgiFormat(it->second.format);
            }
        }
    }

    auto pso = getOrCreatePso(pipelineState, rtvFormat, dsvFormat, vs, ps, sampleCount);
    if (pso) {
        mCmdList->SetPipelineState(pso.Get());
        mCmdList->SetGraphicsRootSignature(mRootSignature.Get());
    }
#else
    (void)pipelineState;
#endif
}

void Dx12Driver::bindRenderPrimitive(Handle<HwRenderPrimitive> rph) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    // TODO(zh-cn): 后续可缓存当前绑定以减少 IA 设置；此处直接存储以便 draw 使用。
    mCurrentRenderPrimitive = rph;
    // 设置当前拓扑，后续 draw 使用。
    auto it = mRenderPrimitives.find(rph.getId());
    if (it != mRenderPrimitives.end()) {
        switch (it->second.type) {
            case PrimitiveType::POINTS:        mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
            case PrimitiveType::LINES:         mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
            case PrimitiveType::LINE_STRIP:    mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
            case PrimitiveType::TRIANGLES:     mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
            case PrimitiveType::TRIANGLE_STRIP:mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
            default:                           mCurrentTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
        }
    }
#else
    (void)rph;
#endif
}

void Dx12Driver::draw2(uint32_t indexOffset, uint32_t indexCount, uint32_t instanceCount) {
    // draw2 直接复用 draw 的路径：使用当前绑定的 RenderPrimitive。
    draw({}, mCurrentRenderPrimitive, indexOffset, indexCount, instanceCount);
}

void Dx12Driver::draw(PipelineState pipelineState, Handle<HwRenderPrimitive> rph,
        uint32_t indexOffset, uint32_t indexCount, uint32_t instanceCount) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    // 简单绑定：IA 设置 VB/IB；其余状态待 bindPipeline 完善。
    auto cmd = mCmdList.Get();
    if (!cmd) return;

    auto rpIt = mRenderPrimitives.find(rph.getId());
    if (rpIt == mRenderPrimitives.end()) return;
    const DxRenderPrimitive& rp = rpIt->second;

    auto vbIt = mVertexBuffers.find(rp.vbh.getId());
    if (vbIt == mVertexBuffers.end()) return;
    const DxVertexBuffer& vb = vbIt->second;

    // 设置 VB views
    cmd->IASetPrimitiveTopology(mCurrentTopology);
    cmd->IASetVertexBuffers(0, 1, &vb.views[0]);

    if (rp.ibh) {
        auto ibIt = mIndexBuffers.find(rp.ibh.getId());
        if (ibIt == mIndexBuffers.end()) return;
        const DxIndexBuffer& ib = ibIt->second;
        cmd->IASetIndexBuffer(&ib.view);
        cmd->DrawIndexedInstanced(indexCount ? indexCount : ib.count, instanceCount, indexOffset, 0, 0);
    } else {
        cmd->DrawInstanced(indexCount, instanceCount, indexOffset, 0);
    }
#else
    (void)pipelineState; (void)rph; (void)indexOffset; (void)indexCount; (void)instanceCount;
#endif
}

void Dx12Driver::dispatchCompute(Handle<HwProgram> program, math::uint3 workGroupCount) {
}

void Dx12Driver::scissor(Viewport scissor) {
}

void Dx12Driver::beginTimerQuery(Handle<HwTimerQuery> tqh) {
}

void Dx12Driver::endTimerQuery(Handle<HwTimerQuery> tqh) {
}

void Dx12Driver::resetState(int) {
}

void Dx12Driver::updateDescriptorSetBuffer(
        DescriptorSetHandle dsh,
        descriptor_binding_t binding,
        BufferObjectHandle boh,
        uint32_t offset,
        uint32_t size) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    (void)dsh; (void)binding; (void)boh; (void)offset; (void)size;
    // TODO(zh-cn): 完整的 descriptor set 绑定逻辑，当前仅占位。
#endif
}

void Dx12Driver::updateDescriptorSetTexture(
        DescriptorSetHandle dsh,
        descriptor_binding_t binding,
        TextureHandle th,
        SamplerParams params) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    (void)dsh; (void)binding; (void)params;
    auto it = mTextures.find(th.getId());
    if (it == mTextures.end()) return;
    // 分配并写入 SRV
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = allocSrvCpu();
    if (cpu.ptr == 0) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = toDxgiFormat(it->second.format);
    srv.ViewDimension = (it->second.samples > 1) ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = it->second.levels;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    mDevice->CreateShaderResourceView(it->second.resource.Get(), &srv, cpu);
    // Sampler 占位：默认线性 wrap
    D3D12_CPU_DESCRIPTOR_HANDLE samplerCpu = allocSamplerCpu();
    if (samplerCpu.ptr != 0) {
        D3D12_SAMPLER_DESC sd{};
        sd.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sd.MaxLOD = D3D12_FLOAT32_MAX;
        mDevice->CreateSampler(&sd, samplerCpu);
    }
#endif
}

void Dx12Driver::bindDescriptorSet(
        DescriptorSetHandle dsh,
        descriptor_set_t set,
        DescriptorSetOffsetArray&& offsets) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    (void)dsh; (void)set; (void)offsets;
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get(), mSamplerHeap.Get() };
    mCmdList->SetDescriptorHeaps(2, heaps);
    // TODO(zh-cn): 根参数绑定表，目前为占位，需与 descriptor set/layout 对齐。
#endif
}

void Dx12Driver::unmapBuffer(MemoryMappedBufferHandle mmbh) {
}

void Dx12Driver::copyToMemoryMappedBuffer(MemoryMappedBufferHandle mmbh, size_t offset,
        BufferDescriptor&& data) {
}

bool Dx12Driver::isCompositorTimingSupported() {
    return false;
}

bool Dx12Driver::queryCompositorTiming(backend::SwapChainHandle swapChain,
        backend::CompositorTiming* outCompositorTiming) {
    return false;
}

bool Dx12Driver::queryFrameTimestamps(SwapChainHandle swapChain, uint64_t frameId,
        FrameTimestamps* outFrameTimestamps) {
    return false;
}

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
DXGI_FORMAT Dx12Driver::toDxgiFormat(TextureFormat fmt) const noexcept {
    switch (fmt) {
        case TextureFormat::R8:             return DXGI_FORMAT_R8_UNORM;
        case TextureFormat::R8G8B8A8:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::SRGB8_A8:       return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::R11F_G11F_B10F: return DXGI_FORMAT_R11G11B10_FLOAT;
        case TextureFormat::R16F:           return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::R16G16F:        return DXGI_FORMAT_R16G16_FLOAT;
        case TextureFormat::R16G16B16A16F:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R32F:           return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::DEPTH24:        return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::DEPTH32F:       return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::DEPTH24_STENCIL8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::DEPTH32F_STENCIL8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:                            return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

bool Dx12Driver::isDepthFormat(TextureFormat fmt) const noexcept {
    switch (fmt) {
        case TextureFormat::DEPTH24:
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            return true;
        default:
            return false;
    }
}

void Dx12Driver::ensureSrv(DxTexture& tex) noexcept {
    if (tex.srvHeap || !mDevice || !tex.resource) return;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&tex.srvHeap)))) return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = toDxgiFormat(tex.format);
    srv.ViewDimension = (tex.samples > 1) ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = tex.levels;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    mDevice->CreateShaderResourceView(tex.resource.Get(), &srv,
            tex.srvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Dx12Driver::ensureRtv(DxTexture& tex) noexcept {
    if (tex.rtvHeap || !mDevice || !tex.resource) return;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&tex.rtvHeap)))) return;

    D3D12_RENDER_TARGET_VIEW_DESC rtv{};
    rtv.Format = toDxgiFormat(tex.format);
    rtv.ViewDimension = (tex.samples > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
    mDevice->CreateRenderTargetView(tex.resource.Get(), &rtv,
            tex.rtvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Dx12Driver::ensureDsv(DxTexture& tex) noexcept {
    if (tex.dsvHeap || !mDevice || !tex.resource) return;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(mDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&tex.dsvHeap)))) return;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = toDxgiFormat(tex.format);
    dsv.ViewDimension = (tex.samples > 1) ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    mDevice->CreateDepthStencilView(tex.resource.Get(), &dsv,
            tex.dsvHeap->GetCPUDescriptorHandleForHeapStart());
}
#endif

// 句柄返回型 API（DriverAPI.inc 中生成的 *_S / *_R）占位实现
Handle<HwRenderPrimitive> Dx12Driver::createRenderPrimitiveS() noexcept {
    auto id = mNextHandle++;
    return Handle<HwRenderPrimitive>(id);
}
void Dx12Driver::createRenderPrimitiveR(Handle<HwRenderPrimitive> rph, int dummy) {
    (void)dummy;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!rph) return;
    DxRenderPrimitive rp{};
    rp.type = PrimitiveType::TRIANGLES;
    mRenderPrimitives[rph.getId()] = rp;
#else
    (void)rph;
#endif
}
Handle<HwVertexBufferInfo> Dx12Driver::createVertexBufferInfoS(
        uint8_t bufferCount, uint8_t attributeCount, AttributeArray attributes) noexcept {
    auto id = mNextHandle++;
    return Handle<HwVertexBufferInfo>(id);
}
void Dx12Driver::createVertexBufferInfoR(Handle<HwVertexBufferInfo> vbih, uint8_t bufferCount, uint8_t attributeCount, AttributeArray attributes) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!vbih) return;
    DxVertexBufferInfo info{};
    info.bufferCount = bufferCount;
    info.attributeCount = attributeCount;
    info.attributes = attributes;
    mVbInfos[vbih.getId()] = info;
#else
    (void)vbih; (void)bufferCount; (void)attributeCount; (void)attributes;
#endif
}
Handle<HwVertexBuffer> Dx12Driver::createVertexBufferS(
        uint8_t bufferCount, uint8_t attributeCount, uint32_t vertexCount,
        AttributeArray attributes, AttributeBitset attributeBitset) noexcept {
    (void)bufferCount; (void)attributeCount; (void)vertexCount; (void)attributes; (void)attributeBitset;
    auto id = mNextHandle++;
    return Handle<HwVertexBuffer>(id);
}
void Dx12Driver::createVertexBufferR(Handle<HwVertexBuffer> vbh, uint8_t bufferCount, uint8_t attributeCount, uint32_t vertexCount,
        AttributeArray attributes, AttributeBitset attributeBitset) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!vbh) return;
    DxVertexBuffer vb{};
    vb.vertexCount = vertexCount;
    // 为 VB 创建一个专属的 VertexBufferInfo 以便后续布局查询
    Handle<HwVertexBufferInfo> vbih(mNextHandle++);
    DxVertexBufferInfo info{};
    info.bufferCount = bufferCount;
    info.attributeCount = attributeCount;
    info.attributes = attributes;
    mVbInfos[vbih.getId()] = info;
    vb.vbih = vbih;
    mVertexBuffers[vbh.getId()] = vb;
#else
    (void)vbh; (void)bufferCount; (void)attributeCount; (void)vertexCount; (void)attributes; (void)attributeBitset;
#endif
}
Handle<HwIndexBuffer> Dx12Driver::createIndexBufferS(ElementType elementType, uint32_t indexCount) noexcept { return {}; }
void Dx12Driver::createIndexBufferR(Handle<HwIndexBuffer> ibh, ElementType elementType, uint32_t indexCount) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!ibh || !mDevice) return;
    DxIndexBuffer ib{};
    ib.count = indexCount;
    ib.format = (elementType == ElementType::UINT) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    const UINT stride = (elementType == ElementType::UINT) ? 4u : 2u;
    const UINT bufferSize = indexCount * stride;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bufferSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(mDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&ib.resource)))) {
        return;
    }
    ib.view.BufferLocation = ib.resource->GetGPUVirtualAddress();
    ib.view.Format = ib.format;
    ib.view.SizeInBytes = bufferSize;
    mIndexBuffers[ibh.getId()] = ib;
#else
    (void)ibh; (void)elementType; (void)indexCount;
#endif
}
Handle<HwBufferObject> Dx12Driver::createBufferObjectS(uint32_t byteCount, BufferObjectBinding binding, BufferUsage usage) noexcept {
    auto id = mNextHandle++;
    return Handle<HwBufferObject>(id);
}
void Dx12Driver::createBufferObjectR(Handle<HwBufferObject> boh, uint32_t byteCount, BufferObjectBinding binding, BufferUsage usage) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!boh || !mDevice) return;
    DxBufferObject bo{};
    bo.byteCount = byteCount;
    bo.binding = binding;
    bo.usage = usage;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteCount;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(mDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&bo.resource)))) {
        return;
    }
    mBufferObjects[boh.getId()] = bo;
#else
    (void)boh; (void)byteCount; (void)binding; (void)usage;
#endif
}
Handle<HwMemoryMappedBuffer> Dx12Driver::createMemoryMappedBufferS(uint32_t size, uint32_t alignment) noexcept { return {}; }
void Dx12Driver::createMemoryMappedBufferR(Handle<HwMemoryMappedBuffer>, uint32_t, uint32_t) {}
Handle<HwTimerQuery> Dx12Driver::createTimerQueryS() noexcept { return {}; }
void Dx12Driver::createTimerQueryR(Handle<HwTimerQuery>, int dummy) { (void)dummy; }
Handle<HwDescriptorSetLayout> Dx12Driver::createDescriptorSetLayoutS(
        DescriptorSetLayoutBinding const* bindings, size_t size) noexcept { return {}; }
void Dx12Driver::createDescriptorSetLayoutR(Handle<HwDescriptorSetLayout> dslh, DescriptorSetLayoutBinding const* bindings, size_t size) {
    (void)dslh; (void)bindings; (void)size;
}
Handle<HwDescriptorSet> Dx12Driver::createDescriptorSetS(
        Handle<HwDescriptorSetLayout> dslh) noexcept { return {}; }
void Dx12Driver::createDescriptorSetR(Handle<HwDescriptorSet> dsh, Handle<HwDescriptorSetLayout> dslh) {
    (void)dslh;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!dsh) return;
    DxDescriptorSet ds{};
    ds.srvGpu.ptr = 0;
    ds.samplerGpu.ptr = 0;
    mDescriptorSets[dsh.getId()] = ds;
#else
    (void)dsh;
#endif
}
Handle<HwTexture> Dx12Driver::createTextureS(SamplerType target, uint8_t levels, TextureFormat format,
        uint8_t samples, uint32_t width, uint32_t height, uint32_t depth,
        TextureUsage usage, TextureSwizzle swizzle, utils::ImmutableCString&& name) noexcept {
    (void)target; (void)levels; (void)format; (void)samples;
    (void)width; (void)height; (void)depth; (void)usage; (void)swizzle; (void)name;
    auto id = mNextHandle++;
    return Handle<HwTexture>(id);
}
void Dx12Driver::createTextureR(Handle<HwTexture> th, SamplerType target, uint8_t levels, TextureFormat format,
        uint8_t samples, uint32_t width, uint32_t height, uint32_t depth, TextureUsage usage,
        TextureSwizzle swizzle, utils::ImmutableCString&& name) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!th || !mDevice) return;
    DxTexture tex{};
    tex.type = target;
    tex.levels = levels;
    tex.format = format;
    tex.samples = samples ? samples : 1;
    tex.width = width;
    tex.height = height;
    tex.depth = depth ? depth : 1;
    tex.usage = usage;
    tex.state = D3D12_RESOURCE_STATE_COMMON;
    (void)swizzle;
    (void)name;

    DXGI_FORMAT dxFormat = toDxgiFormat(format);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(tex.depth);
    desc.MipLevels = levels ? levels : 1;
    desc.Format = dxFormat;
    desc.SampleDesc.Count = tex.samples;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (usage & TextureUsage::COLOR_ATTACHMENT) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (usage & TextureUsage::DEPTH_ATTACHMENT) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    if (FAILED(mDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tex.resource)))) {
        return;
    }

    // 创建必要的 SRV/RTV/DSV 描述符。
    ensureSrv(tex);
    if (usage & TextureUsage::COLOR_ATTACHMENT) {
        ensureRtv(tex);
    }
    if (usage & TextureUsage::DEPTH_ATTACHMENT) {
        ensureDsv(tex);
    }

    mTextures[th.getId()] = std::move(tex);
#else
    (void)th; (void)target; (void)levels; (void)format; (void)samples;
    (void)width; (void)height; (void)depth; (void)usage; (void)swizzle; (void)name;
#endif
}
Handle<HwProgram> Dx12Driver::createProgramS(Program&& program) noexcept { return {}; }
void Dx12Driver::createProgramR(Handle<HwProgram> ph, Program&& program) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!ph) return;
    DxProgram p{};
    auto const& sources = program.getShadersSource();
    // 约定：VERTEX 存在于 sources[VERTEX]，FRAGMENT 存在于 sources[FRAGMENT]，内容为 DXIL/HLSL 字节。
    auto copyBlob = [](auto const& src, Microsoft::WRL::ComPtr<ID3DBlob>& out) {
        if (src.empty()) return;
        if (SUCCEEDED(D3DCreateBlob(src.size(), &out))) {
            std::memcpy(out->GetBufferPointer(), src.data(), src.size());
        }
    };
    copyBlob(sources[size_t(ShaderStage::VERTEX)], p.vs);
    copyBlob(sources[size_t(ShaderStage::FRAGMENT)], p.ps);
    mPrograms[ph.getId()] = std::move(p);
#else
    (void)ph; (void)program;
#endif
}
Handle<HwRenderTarget> Dx12Driver::createRenderTargetS(
        TargetBufferFlags targets, uint32_t width, uint32_t height, uint8_t samples,
        MRT color, TargetBufferInfo depth, TargetBufferInfo stencil) noexcept {
    (void)targets; (void)width; (void)height; (void)samples; (void)color; (void)depth; (void)stencil;
    auto id = mNextHandle++;
    return Handle<HwRenderTarget>(id);
}
void Dx12Driver::createRenderTargetR(Handle<HwRenderTarget> rth, TargetBufferFlags targets, uint32_t width, uint32_t height, uint8_t samples,
        MRT color, TargetBufferInfo depth, TargetBufferInfo stencil) {
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!rth) return;
    DxRenderTarget rt{};
    rt.samples = samples ? samples : 1;
    rt.width = width;
    rt.height = height;
    for (uint8_t i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; ++i) {
        if (targets & (1u << i)) {
            rt.color[i] = color[i].handle;
        }
    }
    if (targets & TargetBufferFlags::DEPTH) {
        rt.depth = depth.handle;
    }
    if (targets & TargetBufferFlags::STENCIL) {
        rt.stencil = stencil.handle;
    }
    mRenderTargets[rth.getId()] = rt;
#else
    (void)rth; (void)targets; (void)width; (void)height; (void)samples; (void)color; (void)depth; (void)stencil;
#endif
}
Handle<HwSwapChain> Dx12Driver::createSwapChainS(void* nativeWindow, uint64_t flags) noexcept {
    (void)flags;
    auto id = mNextHandle++;
    return Handle<HwSwapChain>(id);
}

void Dx12Driver::createSwapChainR(Handle<HwSwapChain> sch, void* nativeWindow, uint64_t flags) {
    (void)flags;
#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
    if (!sch || !nativeWindow) return;
    auto hwnd = static_cast<HWND>(nativeWindow);
    RECT rc{};
    uint32_t w = 1280, h = 720;
    if (::GetClientRect(hwnd, &rc)) {
        w = std::max<uint32_t>(1, static_cast<uint32_t>(rc.right - rc.left));
        h = std::max<uint32_t>(1, static_cast<uint32_t>(rc.bottom - rc.top));
    }
    mSwapChains[sch.getId()] = DxSwapChain{ hwnd, w, h };
    initSwapChain(hwnd, w, h);
#else
    (void)sch;
    (void)nativeWindow;
#endif
}

Handle<HwSwapChain> Dx12Driver::createSwapChainHeadlessS(uint32_t width, uint32_t height, uint64_t flags) noexcept {
    (void)width; (void)height; (void)flags;
    auto id = mNextHandle++;
    return Handle<HwSwapChain>(id);
}

void Dx12Driver::createSwapChainHeadlessR(Handle<HwSwapChain> sch, uint32_t width, uint32_t height, uint64_t flags) {
    (void)sch; (void)width; (void)height; (void)flags;
}
Handle<HwStream> Dx12Driver::createStreamFromTextureIdS(uint32_t texId, uint64_t streamFlags, utils::ImmutableCString tag) noexcept { return {}; }
void Dx12Driver::createStreamFromTextureIdR(Handle<HwStream>, uint32_t, uint64_t, utils::ImmutableCString) {}
Handle<HwSync> Dx12Driver::createSyncS() noexcept { return {}; }
void Dx12Driver::createSyncR(Handle<HwSync>, int dummy) { (void)dummy; }
Handle<HwFence> Dx12Driver::createFenceS() noexcept { return {}; }
void Dx12Driver::createFenceR(Handle<HwFence>, int dummy) { (void)dummy; }

#if defined(FILAMENT_SUPPORTS_DX12) && defined(WIN32)
// 显式实例化命令分发器
template class ConcreteDispatcher<Dx12Driver>;
#endif

} // namespace filament::backend

