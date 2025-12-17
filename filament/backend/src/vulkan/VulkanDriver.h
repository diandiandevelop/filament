/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_BACKEND_VULKANDRIVER_H
#define TNT_FILAMENT_BACKEND_VULKANDRIVER_H

#include "VulkanBlitter.h"
#include "VulkanBufferCache.h"
#include "VulkanConstants.h"
#include "VulkanContext.h"
#include "VulkanFboCache.h"
#include "VulkanHandles.h"
#include "VulkanMemory.h"
#include "VulkanPipelineCache.h"
#include "VulkanQueryManager.h"
#include "VulkanReadPixels.h"
#include "VulkanSamplerCache.h"
#include "VulkanSemaphoreManager.h"
#include "VulkanStagePool.h"
#include "VulkanYcbcrConversionCache.h"
#include "vulkan/VulkanDescriptorSetCache.h"
#include "vulkan/VulkanDescriptorSetLayoutCache.h"
#include "vulkan/VulkanExternalImageManager.h"
#include "vulkan/VulkanStreamedImageManager.h"
#include "vulkan/VulkanPipelineLayoutCache.h"
#include "vulkan/memory/ResourceManager.h"
#include "vulkan/memory/ResourcePointer.h"
#include "vulkan/utils/Definitions.h"

#include "backend/DriverEnums.h"

#include "DriverBase.h"
#include "private/backend/Driver.h"

#include <utils/FixedCapacityVector.h>
#include <utils/Allocator.h>
#include <utils/compiler.h>

namespace filament::backend {

class VulkanPlatform;

// The maximum number of attachments for any renderpass (color + resolve + depth)
// 任意渲染通道中附件（颜色 + resolve + 深度）的最大数量
constexpr uint8_t MAX_RENDERTARGET_ATTACHMENT_TEXTURES =
        MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT * 2 + 1;

/**
 * VulkanDriver - Vulkan 后端驱动实现类
 *
 * 负责将 Filament 抽象的后端接口（Driver / DriverAPI）映射到 Vulkan API 调用，
 * 管理 Vulkan 上下文、资源分配、命令缓冲、管线缓存、同步对象等。
 *
 * 主要职责：
 * - 资源管理：缓冲区、纹理、采样器、渲染目标、交换链等的创建与销毁
 * - 命令录制：构建和提交 Vulkan 命令缓冲（图形 / 计算）
 * - 管线管理：图形管线 / 管线布局 / 描述符堆布局与缓存
 * - 同步管理：信号量、fence、查询对象等 GPU 同步原语
 * - 调试支持：DebugUtils 扩展（对象命名、调试回调）
 */
class VulkanDriver final : public DriverBase {
public:
    static Driver* create(VulkanPlatform* platform, VulkanContext& context,
            Platform::DriverConfig const& driverConfig);

#if FVK_ENABLED(FVK_DEBUG_DEBUG_UTILS)
    // Encapsulates the VK_EXT_debug_utils extension.  In particular, we use
    // vkSetDebugUtilsObjectNameEXT and vkCreateDebugUtilsMessengerEXT
    /**
     * DebugUtils - VK_EXT_debug_utils 扩展封装类
     *
     * 用于：
     * - 为 Vulkan 对象设置调试名称（vkSetDebugUtilsObjectNameEXT）
     * - 创建 / 销毁调试消息回调（vkCreateDebugUtilsMessengerEXT）
     */
    class DebugUtils {
    public:
        /**
         * 为 Vulkan 对象设置调试名称
         *
         * @param type   Vulkan 对象类型（VkObjectType）
         * @param handle Vulkan 对象句柄（按 uint64_t 传入）
         * @param name   调试名称（以 null 结尾的字符串）
         */
        static void setName(VkObjectType type, uint64_t handle, char const* name);

    private:
        static DebugUtils* get();

        DebugUtils(VkInstance instance, VkDevice device, VulkanContext const& context);
        ~DebugUtils();

        VkInstance const mInstance;                 // Vulkan 实例
        VkDevice const mDevice;                     // Vulkan 设备
        bool const mEnabled;                        // 是否启用 debug_utils 扩展
        VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE; // 调试消息回调句柄

        static DebugUtils* mSingleton;              // 全局单例指针

        friend class VulkanDriver;
    };
#endif // FVK_ENABLED(FVK_DEBUG_DEBUG_UTILS)

private:
    template<typename D>
    using resource_ptr = fvkmemory::resource_ptr<D>;   // Vulkan 资源智能指针包装

    // 支持的最大采样器绑定数量（与 Program 中的绑定计数保持一致）
    static constexpr uint8_t MAX_SAMPLER_BINDING_COUNT = Program::SAMPLER_BINDING_COUNT;

    // 调试：在命令提交前记录调试信息 / 标记范围
    void debugCommandBegin(CommandStream* cmds, bool synchronous,
            const char* methodName) noexcept override;

    // 构造函数：初始化 VulkanDriver，绑定平台和上下文
    VulkanDriver(VulkanPlatform* platform, VulkanContext& context,
            Platform::DriverConfig const& driverConfig);

    // 析构函数：释放所有 Vulkan 相关资源
    ~VulkanDriver() noexcept override;

    // 返回 Dispatcher，用于将 DriverAPI 调用分发到具体实现
    Dispatcher getDispatcher() const noexcept final;

    // 返回当前后端支持的 ShaderModel
    ShaderModel getShaderModel() const noexcept final;

    // 返回当前后端支持的着色语言集合（GLSL、SPIR-V 等）
    utils::FixedCapacityVector<ShaderLanguage> getShaderLanguages(
            ShaderLanguage preferredLanguage) const noexcept final;

    template<typename T>
    friend class ConcreteDispatcher;

#define DECL_DRIVER_API(methodName, paramsDecl, params)                                            \
    UTILS_ALWAYS_INLINE inline void methodName(paramsDecl);

#define DECL_DRIVER_API_SYNCHRONOUS(RetType, methodName, paramsDecl, params)                       \
    RetType methodName(paramsDecl) override;

#define DECL_DRIVER_API_RETURN(RetType, methodName, paramsDecl, params)                            \
    RetType methodName##S() noexcept override;                                                     \
    UTILS_ALWAYS_INLINE inline void methodName##R(RetType, paramsDecl);

#include "private/backend/DriverAPI.inc"

    VulkanDriver(VulkanDriver const&) = delete;
    VulkanDriver& operator=(VulkanDriver const&) = delete;

private:
    // 收集和销毁延迟释放的 Vulkan 资源
    void collectGarbage();

    // 绑定图形管线的内部实现：根据 PipelineState 和管线布局绑定管线和描述符堆
    void bindPipelineImpl(PipelineState const& pipelineState, VkPipelineLayout pipelineLayout,
            fvkutils::DescriptorSetMask descriptorSetMask);

    // Flush the current command buffer and reset the pipeline state.
    // 刷新当前命令缓冲并重置管线状态
    void endCommandRecording();

    VulkanPlatform* mPlatform = nullptr;                // 平台抽象（窗口系统 / 表面等）
    fvkmemory::ResourceManager mResourceManager;        // Vulkan 内存与资源管理器

    resource_ptr<VulkanSwapChain> mCurrentSwapChain;    // 当前交换链
    resource_ptr<VulkanRenderTarget> mDefaultRenderTarget; // 默认渲染目标
    VulkanRenderPass mCurrentRenderPass = {};           // 当前渲染通道状态
    VmaAllocator mAllocator = VK_NULL_HANDLE;           // VMA 分配器（Vulkan Memory Allocator）
    VkDebugReportCallbackEXT mDebugCallback = VK_NULL_HANDLE; // Debug 回调（旧扩展）

    VulkanContext& mContext;                            // Vulkan 上下文（设备 / 队列等）

    // 各种缓存与管理器（按职责分类）
    VulkanSemaphoreManager mSemaphoreManager;           // 信号量管理器
    VulkanCommands mCommands;                           // 命令缓冲与队列管理
    VulkanPipelineLayoutCache mPipelineLayoutCache;     // 管线布局缓存
    VulkanPipelineCache mPipelineCache;                 // 管线对象缓存
    VulkanStagePool mStagePool;                         // staging buffer 池
    VulkanBufferCache mBufferCache;                     // 缓冲区缓存
    VulkanFboCache mFramebufferCache;                   // Framebuffer 缓存
    VulkanYcbcrConversionCache mYcbcrConversionCache;   // YCbCr 转换缓存
    VulkanSamplerCache mSamplerCache;                   // 采样器缓存
    VulkanBlitter mBlitter;                             // 纹理拷贝 / blit 工具
    VulkanReadPixels mReadPixels;                       // 读回像素辅助类
    VulkanDescriptorSetLayoutCache mDescriptorSetLayoutCache; // 描述符堆布局缓存
    VulkanDescriptorSetCache mDescriptorSetCache;       // 描述符堆缓存
    VulkanQueryManager mQueryManager;                   // 查询对象管理器（时间戳 / occlusion）
    VulkanExternalImageManager mExternalImageManager;   // 外部图像（AHB / 外部内存）管理
    VulkanStreamedImageManager mStreamedImageManager;   // 流式图像（上传 / streaming）管理


    // This maps a VulkanSwapchain to a native swapchain. VulkanSwapchain should have a copy of the
    // Platform::Swapchain pointer, but queryFrameTimestamps() and queryCompositorTiming() are
    // synchronous calls, making access to VulkanSwapchain unsafe (this difference vs other backends
    // is due to the ref-counting of vulkan resources).
    struct {
        std::mutex lock;
        std::unordered_map<HandleId, Platform::SwapChain*> nativeSwapchains;
    } mTiming;  // 交换链句柄到平台交换链的映射（用于同步查询接口）

    // This is necessary for us to write to push constants after binding a pipeline.
    using DescriptorSetLayoutHandleList = std::array<resource_ptr<VulkanDescriptorSetLayout>,
            VulkanDescriptorSetLayout::UNIQUE_DESCRIPTOR_SET_COUNT>;

    // 在 draw() 过程中绑定管线时需要缓存的一组状态
    struct BindInDrawBundle {
        PipelineState pipelineState = {};               // 即将绑定的管线状态
        DescriptorSetLayoutHandleList dsLayoutHandles = {}; // 描述符堆布局句柄列表
        fvkutils::DescriptorSetMask descriptorSetMask = {}; // 需要绑定的描述符堆掩码
        resource_ptr<VulkanProgram> program = {};       // 着色程序
    };

    // 当前管线相关的状态缓存（push constant / 动态 UBO 提交等）
    struct {
        // For push constant
        // 当前激活的着色程序（用于 push constant）
        resource_ptr<VulkanProgram> program = {};
        // For push commiting dynamic ubos in draw()
        // 当前绑定的管线布局（用于提交动态 UBO）
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        fvkutils::DescriptorSetMask descriptorSetMask = {};

        // 在 draw 调用中延迟绑定管线的一组状态
        std::pair<bool, BindInDrawBundle> bindInDraw = {false, {}};
    } mPipelineState = {};

    // 应用层相关状态（与外部采样器 / 外部图像相关）
    struct {
        // This tracks whether the app has seen external samplers bound to a the descriptor set.
        // This will force bindPipeline to take a slow path.
        // 是否存在绑定到描述符堆的 external sampler / external image
        bool hasExternalSamplerLayouts = false;
        bool hasBoundExternalImages = false;

        bool hasExternalSamplers() const noexcept {
            return hasExternalSamplerLayouts && hasBoundExternalImages;
        }
    } mAppState;

    bool const mIsSRGBSwapChainSupported;               // 是否支持 sRGB 交换链
    bool const mIsMSAASwapChainSupported;               // 是否支持 MSAA 交换链
    backend::StereoscopicType const mStereoscopicType;  // 立体渲染类型（单眼 / 双眼等）

    // setAcquiredImage is a DECL_DRIVER_API_SYNCHRONOUS_N which means we don't necessarily have the
    // data to process it at call time. So we store it and process it during updateStreams.
    // setAcquiredImage 是 DECL_DRIVER_API_SYNCHRONOUS_N，意味着在调用时可能无法立即处理
    // 因此我们将其记录下来，并在 updateStreams 阶段统一处理。
    std::vector<resource_ptr<VulkanStream>> mStreamsWithPendingAcquiredImage;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_VULKANDRIVER_H
