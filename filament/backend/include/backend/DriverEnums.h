/*
 * Copyright (C) 2015 The Android Open Source Project
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

//! \file

#ifndef TNT_FILAMENT_BACKEND_DRIVERENUMS_H
#define TNT_FILAMENT_BACKEND_DRIVERENUMS_H

#include <utils/unwindows.h> // Because we define ERROR in the FenceStatus enum.

#include <backend/Platform.h>
#include <backend/PresentCallable.h>

#include <utils/BitmaskEnum.h>
#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Invocable.h>
#include <utils/StaticString.h>
#include <utils/debug.h>

#include <math/vec4.h>

#include <array>
#include <type_traits>
#include <variant>
#include <string_view>

#include <stddef.h>
#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

/**
 * Types and enums used by filament's driver.
 *
 * Effectively these types are public but should not be used directly. Instead use public classes
 * internal redeclaration of these types.
 * For e.g. Use Texture::Sampler instead of filament::SamplerType.
 */
/**
 * Filament 驱动程序使用的类型和枚举
 * 
 * 这些类型实际上是公开的，但不应该直接使用。
 * 应该使用公共类内部重新声明的这些类型。
 * 例如：使用 Texture::Sampler 而不是 filament::SamplerType。
 * 
 * 设计目的：
 * - 提供后端抽象层
 * - 支持多种图形 API（OpenGL、Vulkan、Metal、WebGPU）
 * - 类型安全的枚举和常量
 */
namespace filament::backend {

/**
 * Requests a SwapChain with an alpha channel.
 */
/**
 * 交换链配置：请求带 alpha 通道的交换链
 * 
 * 值：0x1
 * 
 * 用途：
 * - 创建支持透明度的交换链
 * - 用于合成透明内容（如 UI 叠加层）
 * - 需要与窗口系统支持透明背景配合使用
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_TRANSPARENT         = 0x1;

/**
 * This flag indicates that the swap chain may be used as a source surface
 * for reading back render results.  This config flag must be set when creating
 * any SwapChain that will be used as the source for a blit operation.
 */
/**
 * 交换链配置：可读标志
 * 
 * 值：0x2
 * 
 * 用途：
 * - 指示交换链可以用作读取渲染结果的源表面
 * - 在创建任何将用作 blit 操作源的 SwapChain 时必须设置此标志
 * - 用于屏幕截图、后处理等需要读取交换链内容的场景
 * 
 * 注意：
 * - 设置此标志可能会影响性能
 * - 某些平台可能不支持可读交换链
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_READABLE            = 0x2;

/**
 * Indicates that the native X11 window is an XCB window rather than an XLIB window.
 * This is ignored on non-Linux platforms and in builds that support only one X11 API.
 */
/**
 * 交换链配置：启用 XCB
 * 
 * 值：0x4
 * 
 * 用途：
 * - 指示原生 X11 窗口是 XCB 窗口而不是 XLIB 窗口
 * - XCB 是 X11 的 C 绑定，提供更好的性能和线程安全
 * 
 * 限制：
 * - 仅在 Linux 平台上有效
 * - 在仅支持一个 X11 API 的构建中被忽略
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_ENABLE_XCB          = 0x4;

/**
 * Indicates that the native window is a CVPixelBufferRef.
 *
 * This is only supported by the Metal backend. The CVPixelBuffer must be in the
 * kCVPixelFormatType_32BGRA format.
 *
 * It is not necessary to add an additional retain call before passing the pixel buffer to
 * Filament. Filament will call CVPixelBufferRetain during Engine::createSwapChain, and
 * CVPixelBufferRelease when the swap chain is destroyed.
 */
/**
 * 交换链配置：Apple CVPixelBuffer
 * 
 * 值：0x8
 * 
 * 用途：
 * - 指示原生窗口是 CVPixelBufferRef（Core Video 像素缓冲区）
 * - 用于 iOS/macOS 上的视频纹理和相机预览
 * 
 * 限制：
 * - 仅由 Metal 后端支持
 * - CVPixelBuffer 必须为 kCVPixelFormatType_32BGRA 格式
 * 
 * 内存管理：
 * - 不需要在传递给 Filament 之前调用额外的 retain
 * - Filament 会在 Engine::createSwapChain 时调用 CVPixelBufferRetain
 * - Filament 会在交换链销毁时调用 CVPixelBufferRelease
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_APPLE_CVPIXELBUFFER = 0x8;

/**
 * Indicates that the SwapChain must automatically perform linear to srgb encoding.
 */
/**
 * 交换链配置：sRGB 颜色空间
 * 
 * 值：0x10
 * 
 * 用途：
 * - 指示交换链必须自动执行线性到 sRGB 编码
 * - 用于在显示时正确进行颜色空间转换
 * - 确保颜色在显示器上正确显示
 * 
 * 注意：
 * - 如果交换链支持原生 sRGB，可能不需要此标志
 * - 某些平台会自动处理颜色空间转换
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_SRGB_COLORSPACE     = 0x10;

/**
 * Indicates that the SwapChain should also contain a stencil component.
 */
/**
 * 交换链配置：包含模板缓冲区
 * 
 * 值：0x20
 * 
 * 用途：
 * - 指示交换链应该包含模板组件
 * - 用于需要模板测试的渲染（如 UI 遮罩、轮廓等）
 * 
 * 注意：
 * - 会增加内存使用
 * - 某些平台可能不支持模板缓冲区
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_HAS_STENCIL_BUFFER  = 0x20;

/**
 * 交换链配置：包含模板缓冲区（向后兼容别名）
 * 
 * 与 SWAP_CHAIN_CONFIG_HAS_STENCIL_BUFFER 相同。
 */
static constexpr uint64_t SWAP_CHAIN_HAS_STENCIL_BUFFER         = SWAP_CHAIN_CONFIG_HAS_STENCIL_BUFFER;

/**
 * The SwapChain contains protected content. Currently only supported by OpenGLPlatform and
 * only when OpenGLPlatform::isProtectedContextSupported() is true.
 */
/**
 * 交换链配置：受保护内容
 * 
 * 值：0x40
 * 
 * 用途：
 * - 指示交换链包含受保护内容（如 DRM 保护的内容）
 * - 用于安全视频播放等场景
 * 
 * 限制：
 * - 目前仅由 OpenGLPlatform 支持
 * - 仅当 OpenGLPlatform::isProtectedContextSupported() 返回 true 时有效
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_PROTECTED_CONTENT   = 0x40;

/**
 * Indicates that the SwapChain is configured to use Multi-Sample Anti-Aliasing (MSAA) with the
 * given sample points within each pixel. Only supported when isMSAASwapChainSupported(4) is
 * true.
 *
 * This is only supported by EGL(Android). Other GL platforms (GLX, WGL, etc) don't support it
 * because the swapchain MSAA settings must be configured before window creation.
 */
/**
 * 交换链配置：4x MSAA（多重采样抗锯齿）
 * 
 * 值：0x80
 * 
 * 用途：
 * - 指示交换链配置为使用 4x 多重采样抗锯齿
 * - 每个像素内使用 4 个采样点
 * - 用于减少锯齿，提高渲染质量
 * 
 * 限制：
 * - 仅当 isMSAASwapChainSupported(4) 返回 true 时支持
 * - 仅由 EGL（Android）支持
 * - 其他 GL 平台（GLX、WGL 等）不支持，因为交换链 MSAA 设置必须在窗口创建前配置
 */
static constexpr uint64_t SWAP_CHAIN_CONFIG_MSAA_4_SAMPLES      = 0x80;

/**
 * 最大顶点属性数量
 * 
 * OpenGL ES 保证的最小值。
 * 所有后端必须至少支持此数量的顶点属性。
 */
static constexpr size_t MAX_VERTEX_ATTRIBUTE_COUNT  = 16;   // This is guaranteed by OpenGL ES.

/**
 * 最大采样器数量
 * 
 * 功能级别 3 所需的最大值。
 * 用于顶点和片段着色器的总采样器数量。
 */
static constexpr size_t MAX_SAMPLER_COUNT           = 62;   // Maximum needed at feature level 3.

/**
 * 最大顶点缓冲区数量
 * 
 * 可以绑定到顶点缓冲区的缓冲区对象的最大数量。
 * 必须 <= MAX_VERTEX_ATTRIBUTE_COUNT。
 */
static constexpr size_t MAX_VERTEX_BUFFER_COUNT     = 16;   // Max number of bound buffer objects.

/**
 * 最大着色器存储缓冲区（SSBO）数量
 * 
 * OpenGL ES 保证的最小值。
 * 用于计算着色器中的存储缓冲区。
 */
static constexpr size_t MAX_SSBO_COUNT              = 4;    // This is guaranteed by OpenGL ES.

/**
 * 最大描述符集数量
 * 
 * Vulkan 保证的最小值。
 * 用于资源绑定的描述符集数量。
 */
static constexpr size_t MAX_DESCRIPTOR_SET_COUNT    = 4;    // This is guaranteed by Vulkan.

/**
 * 每个描述符集的最大描述符数量
 * 
 * 单个描述符集中可以绑定的资源数量。
 */
static constexpr size_t MAX_DESCRIPTOR_COUNT        = 64;   // per set

/**
 * 最大推送常量数量
 * 
 * Vulkan 1.1 规范允许 128 字节的推送常量。
 * 假设每个类型为 4 字节，因此最多 32 个推送常量。
 */
static constexpr size_t MAX_PUSH_CONSTANT_COUNT     = 32;   // Vulkan 1.1 spec allows for 128-byte
                                                            // of push constant (we assume 4-byte
                                                            // types).

/**
 * 每个功能级别的能力限制
 * 
 * 根据功能级别定义的最大采样器数量。
 * 使用 (int)FeatureLevel 作为数组索引。
 * 
 * 数组索引：
 * - [0]: 不使用（占位符）
 * - [1]: FEATURE_LEVEL_0 - 顶点采样器：0，片段采样器：0
 * - [2]: FEATURE_LEVEL_1 - 顶点采样器：16，片段采样器：16（OpenGL ES、Vulkan、Metal、WebGPU 保证）
 * - [3]: FEATURE_LEVEL_2 - 顶点采样器：16，片段采样器：16（OpenGL ES、Vulkan、Metal、WebGPU 保证）
 * - [4]: FEATURE_LEVEL_3 - 顶点采样器：31，片段采样器：31（Metal 保证）
 */
// Per feature level caps
// Use (int)FeatureLevel to index this array
static constexpr struct {
    const size_t MAX_VERTEX_SAMPLER_COUNT;      // 顶点着色器最大采样器数量
    const size_t MAX_FRAGMENT_SAMPLER_COUNT;   // 片段着色器最大采样器数量
} FEATURE_LEVEL_CAPS[4] = {
        {  0,  0 }, // do not use
        { 16, 16 }, // guaranteed by OpenGL ES, Vulkan, Metal And WebGPU
        { 16, 16 }, // guaranteed by OpenGL ES, Vulkan, Metal And WebGPU
        { 31, 31 }, // guaranteed by Metal
};

/**
 * 静态断言：验证顶点缓冲区数量限制
 * 
 * 确保可以附加到 VertexBuffer 的缓冲区对象数量
 * 小于或等于最大顶点属性数量。
 */
static_assert(MAX_VERTEX_BUFFER_COUNT <= MAX_VERTEX_ATTRIBUTE_COUNT,
        "The number of buffer objects that can be attached to a VertexBuffer must be "
        "less than or equal to the maximum number of vertex attributes.");

/**
 * 配置的 Uniform 绑定数量
 * 
 * OpenGL ES 保证的最小值。
 * 用于 ES2 兼容模式的 uniform 缓冲区绑定。
 */
static constexpr size_t CONFIG_UNIFORM_BINDING_COUNT = 9;   // This is guaranteed by OpenGL ES.

/**
 * 配置的采样器绑定数量
 * 
 * OpenGL ES 保证的最小值。
 * 用于 ES2 兼容模式的采样器绑定。
 */
static constexpr size_t CONFIG_SAMPLER_BINDING_COUNT = 4;   // This is guaranteed by OpenGL ES.

/**
 * 外部采样器数据索引：未使用
 * 
 * 值：uint8_t(-1) = 255
 * 
 * 用途：
 * - 表示描述符集绑定未使用任何外部采样器状态
 * - 因此没有有效条目
 * - 用于标记未使用的绑定槽
 */
static constexpr uint8_t EXTERNAL_SAMPLER_DATA_INDEX_UNUSED =
        uint8_t(-1);// Case where the descriptor set binding isnt using any external sampler state
                     // and therefore doesn't have a valid entry.

/**
 * Defines the backend's feature levels.
 */
/**
 * 后端功能级别枚举
 * 
 * 定义后端支持的功能级别，从低到高递增。
 * 功能级别决定了可用的图形 API 功能和性能特性。
 */
enum class FeatureLevel : uint8_t {
    /**
     * OpenGL ES 2.0 功能级别
     * 
     * 最低功能级别，支持基本的渲染功能。
     * - 基本的顶点和片段着色器
     * - 有限的纹理支持
     * - 基本的混合和深度测试
     */
    FEATURE_LEVEL_0 = 0,  //!< OpenGL ES 2.0 features
    
    /**
     * OpenGL ES 3.0 功能级别（默认）
     * 
     * 标准功能级别，大多数现代设备支持。
     * - 完整的 OpenGL ES 3.0 功能
     * - 更好的纹理格式支持
     * - 改进的着色器功能
     */
    FEATURE_LEVEL_1,      //!< OpenGL ES 3.0 features (default)
    
    /**
     * OpenGL ES 3.1 功能级别 + 扩展
     * 
     * 高级功能级别，支持更多特性。
     * - OpenGL ES 3.1 功能
     * - 16 个纹理单元
     * - 立方体贴图数组支持
     */
    FEATURE_LEVEL_2,      //!< OpenGL ES 3.1 features + 16 textures units + cubemap arrays
    
    /**
     * OpenGL ES 3.1 功能级别 + 最大扩展
     * 
     * 最高功能级别，支持最多特性。
     * - OpenGL ES 3.1 功能
     * - 31 个纹理单元（Metal 保证）
     * - 立方体贴图数组支持
     */
    FEATURE_LEVEL_3       //!< OpenGL ES 3.1 features + 31 textures units + cubemap arrays
};

/**
 * Selects which driver a particular Engine should use.
 */
/**
 * 后端驱动选择枚举
 * 
 * 选择特定 Engine 应该使用的驱动程序。
 * 
 * 用途：
 * - 在创建 Engine 时指定后端
 * - 控制使用哪个底层图形 API
 * - 用于测试和调试
 */
enum class Backend : uint8_t {
    /**
     * 自动选择
     * 
     * 根据平台自动选择适当的驱动程序。
     * - 这是推荐的默认值
     * - Filament 会选择最适合平台的后端
     */
    DEFAULT = 0,  //!< Automatically selects an appropriate driver for the platform.
    
    /**
     * OpenGL/ES 驱动
     * 
     * 选择 OpenGL 或 OpenGL ES 驱动程序。
     * - Android 上的默认后端
     * - 跨平台支持最好
     * - 兼容性最高
     */
    OPENGL = 1,   //!< Selects the OpenGL/ES driver (default on Android)
    
    /**
     * Vulkan 驱动
     * 
     * 选择 Vulkan 驱动程序（如果平台支持）。
     * - Linux/Windows 上的默认后端
     * - 性能通常最好
     * - 需要 Vulkan 1.0+ 支持
     */
    VULKAN = 2,   //!< Selects the Vulkan driver if the platform supports it (default on Linux/Windows)
    
    /**
     * Metal 驱动
     * 
     * 选择 Metal 驱动程序（如果平台支持）。
     * - macOS/iOS 上的默认后端
     * - Apple 平台的推荐后端
     * - 需要 macOS 10.13+ 或 iOS 11+
     */
    METAL = 3,    //!< Selects the Metal driver if the platform supports it (default on MacOS/iOS).
    
    /**
     * WebGPU 驱动
     * 
     * 选择 WebGPU 驱动程序（如果平台支持 WebGPU）。
     * - 用于 Web 平台
     * - 跨浏览器图形 API
     * - 需要浏览器支持 WebGPU
     */
    WEBGPU = 4,   //!< Selects the Webgpu driver if the platform supports webgpu.
    
    /**
     * 空操作驱动
     * 
     * 选择空操作驱动程序，用于测试目的。
     * - 不执行任何实际的渲染操作
     * - 用于单元测试和性能分析
     * - 不会创建实际的图形上下文
     */
    NOOP = 5,     //!< Selects the no-op driver for testing purposes.
};

/**
 * 计时查询结果枚举
 * 
 * 表示计时查询操作的结果状态。
 * 
 * 用途：
 * - 用于 GPU 性能测量
 * - 查询渲染命令的执行时间
 * - 用于性能分析和优化
 */
enum class TimerQueryResult : int8_t {
    /**
     * 错误
     * 
     * 发生错误，结果不可用。
     * - 查询可能失败
     * - 结果数据无效
     */
    ERROR = -1,     // an error occurred, result won't be available
    
    /**
     * 未就绪
     * 
     * 结果尚未就绪。
     * - 查询仍在进行中
     * - 需要稍后再次查询
     */
    NOT_READY = 0,  // result to ready yet
    
    /**
     * 可用
     * 
     * 结果可用。
     * - 查询已完成
     * - 可以安全读取结果
     */
    AVAILABLE = 1,  // result is available
};

/**
 * 将 Backend 枚举转换为字符串
 * 
 * @param backend 后端枚举值
 * @return 后端名称的字符串视图
 * 
 * 支持的枚举值：
 * - NOOP: "Noop"
 * - OPENGL: "OpenGL"
 * - VULKAN: "Vulkan"
 * - METAL: "Metal"
 * - WEBGPU: "WebGPU"
 * - DEFAULT: "Default"
 * - 其他: "Unknown"
 */
constexpr std::string_view to_string(Backend const backend) noexcept {
    switch (backend) {
        case Backend::NOOP:
            return "Noop";
        case Backend::OPENGL:
            return "OpenGL";
        case Backend::VULKAN:
            return "Vulkan";
        case Backend::METAL:
            return "Metal";
        case Backend::WEBGPU:
            return "WebGPU";
        case Backend::DEFAULT:
            return "Default";
    }
    return "Unknown";
}

/**
 * Defines the shader language. Similar to the above backend enum, with some differences:
 * - The OpenGL backend can select between two shader languages: ESSL 1.0 and ESSL 3.0.
 * - The Metal backend can prefer precompiled Metal libraries, while falling back to MSL.
 */
/**
 * 着色器语言枚举
 * 
 * 定义着色器语言类型。与后端枚举类似，但有一些区别：
 * - OpenGL 后端可以在两种着色器语言之间选择：ESSL 1.0 和 ESSL 3.0
 * - Metal 后端可以优先使用预编译的 Metal 库，否则回退到 MSL
 * 
 * 用途：
 * - 指定材质使用的着色器语言
 * - 控制着色器编译流程
 * - 支持跨后端着色器兼容性
 */
enum class ShaderLanguage {
    /**
     * 未指定
     * 
     * 着色器语言未指定，由后端自动选择。
     */
    UNSPECIFIED = -1,
    
    /**
     * ESSL 1.0
     * 
     * OpenGL ES Shading Language 1.0。
     * - OpenGL ES 2.0 的着色器语言
     * - 功能有限，兼容性最好
     */
    ESSL1 = 0,
    
    /**
     * ESSL 3.0
     * 
     * OpenGL ES Shading Language 3.0。
     * - OpenGL ES 3.0+ 的着色器语言
     * - 功能更丰富，推荐使用
     */
    ESSL3 = 1,
    
    /**
     * SPIR-V
     * 
     * Standard Portable Intermediate Representation - Vulkan。
     * - Vulkan 的着色器中间表示
     * - 二进制格式，跨平台
     */
    SPIRV = 2,
    
    /**
     * MSL
     * 
     * Metal Shading Language。
     * - Metal 的着色器语言
     * - 源代码格式
     */
    MSL = 3,
    
    /**
     * Metal 预编译库
     * 
     * Precompiled Metal Library。
     * - 预编译的 Metal 着色器库
     * - 二进制格式，性能更好
     * - Metal 后端优先使用
     */
    METAL_LIBRARY = 4,
    
    /**
     * WGSL
     * 
     * WebGPU Shading Language。
     * - WebGPU 的着色器语言
     * - 用于 Web 平台
     */
    WGSL = 5,
};

/**
 * 将 ShaderLanguage 枚举转换为字符串
 * 
 * @param shaderLanguage 着色器语言枚举值
 * @return 着色器语言名称的 C 字符串
 * 
 * 支持的枚举值：
 * - ESSL1: "ESSL 1.0"
 * - ESSL3: "ESSL 3.0"
 * - SPIRV: "SPIR-V"
 * - MSL: "MSL"
 * - METAL_LIBRARY: "Metal precompiled library"
 * - WGSL: "WGSL"
 * - UNSPECIFIED: "Unspecified"
 * - 其他: "UNKNOWN"
 */
constexpr const char* shaderLanguageToString(ShaderLanguage shaderLanguage) noexcept {
    switch (shaderLanguage) {
        case ShaderLanguage::ESSL1:
            return "ESSL 1.0";
        case ShaderLanguage::ESSL3:
            return "ESSL 3.0";
        case ShaderLanguage::SPIRV:
            return "SPIR-V";
        case ShaderLanguage::MSL:
            return "MSL";
        case ShaderLanguage::METAL_LIBRARY:
            return "Metal precompiled library";
        case ShaderLanguage::WGSL:
            return "WGSL";
        case ShaderLanguage::UNSPECIFIED:
            return "Unspecified";
    }
    return "UNKNOWN";
}

/**
 * 着色器阶段枚举
 * 
 * 定义着色器程序的阶段类型。
 * 
 * 用途：
 * - 指定着色器代码所属的阶段
 * - 在创建着色器程序时使用
 * - 用于资源绑定和管线状态管理
 */
enum class ShaderStage : uint8_t {
    VERTEX = 0,    // 顶点着色器阶段
    FRAGMENT = 1,  // 片段着色器阶段
    COMPUTE = 2    // 计算着色器阶段
};

/**
 * 管线阶段数量
 * 
 * 用于传统渲染管线的阶段数量（顶点和片段）。
 * 注意：不包括计算着色器。
 */
static constexpr size_t PIPELINE_STAGE_COUNT = 2;

/**
 * 着色器阶段标志枚举
 * 
 * 位掩码枚举，用于组合多个着色器阶段。
 * 支持位运算（OR、AND 等）。
 */
enum class ShaderStageFlags : uint8_t {
    NONE        =    0,   // 无阶段
    VERTEX      =    0x1, // 顶点着色器阶段
    FRAGMENT    =    0x2, // 片段着色器阶段
    COMPUTE     =    0x4, // 计算着色器阶段
    ALL_SHADER_STAGE_FLAGS = VERTEX | FRAGMENT | COMPUTE  // 所有着色器阶段
};

/**
 * 检查着色器阶段标志是否包含指定类型
 * 
 * @param flags 着色器阶段标志（位掩码）
 * @param type  要检查的着色器阶段类型
 * @return 如果标志包含指定类型返回 true，否则返回 false
 * 
     * 实现：
 * - 使用位运算检查标志中是否设置了对应的位
 * - 例如：hasShaderType(VERTEX | FRAGMENT, VERTEX) 返回 true
 */
constexpr bool hasShaderType(ShaderStageFlags flags, ShaderStage type) noexcept {
    switch (type) {
        case ShaderStage::VERTEX:
            return bool(uint8_t(flags) & uint8_t(ShaderStageFlags::VERTEX));
        case ShaderStage::FRAGMENT:
            return bool(uint8_t(flags) & uint8_t(ShaderStageFlags::FRAGMENT));
        case ShaderStage::COMPUTE:
            return bool(uint8_t(flags) & uint8_t(ShaderStageFlags::COMPUTE));
    }
}

/**
 * 纹理类型枚举
 * 
 * 定义纹理的数据类型，影响纹理的存储格式和用途。
 */
enum class TextureType : uint8_t {
    FLOAT,          // 浮点纹理（用于颜色、法线等）
    INT,            // 有符号整数纹理（用于整数数据）
    UINT,           // 无符号整数纹理（用于整数数据）
    DEPTH,          // 深度纹理（用于深度缓冲）
    STENCIL,        // 模板纹理（用于模板缓冲）
    DEPTH_STENCIL   // 深度-模板纹理（组合格式）
};

/**
 * 将 TextureType 枚举转换为字符串
 * 
 * @param type 纹理类型枚举值
 * @return 纹理类型名称的字符串视图
 */
constexpr std::string_view to_string(TextureType type) noexcept {
    switch (type) {
        case TextureType::FLOAT:            return "FLOAT";
        case TextureType::INT:              return "INT";
        case TextureType::UINT:             return "UINT";
        case TextureType::DEPTH:            return "DEPTH";
        case TextureType::STENCIL:          return "STENCIL";
        case TextureType::DEPTH_STENCIL:    return "DEPTH_STENCIL";
    }
    return "UNKNOWN";
}

/**
 * 描述符类型枚举
 * 
 * 定义可在描述符集中绑定的资源类型。
 * 描述符用于在着色器中访问纹理、采样器、缓冲区等资源。
 * 
 * 分类：
 * - 采样器类型（SAMPLER_*）：纹理和采样器组合
 * - 缓冲区类型（UNIFORM_BUFFER, SHADER_STORAGE_BUFFER）：用于存储 uniform 数据
 * - 输入附件（INPUT_ATTACHMENT）：用于子通道输入
 */
enum class DescriptorType : uint8_t {
    SAMPLER_2D_FLOAT,           // 2D 浮点采样器
    SAMPLER_2D_INT,             // 2D 有符号整数采样器
    SAMPLER_2D_UINT,            // 2D 无符号整数采样器
    SAMPLER_2D_DEPTH,           // 2D 深度采样器（用于阴影贴图）

    SAMPLER_2D_ARRAY_FLOAT,     // 2D 数组浮点采样器
    SAMPLER_2D_ARRAY_INT,       // 2D 数组有符号整数采样器
    SAMPLER_2D_ARRAY_UINT,      // 2D 数组无符号整数采样器
    SAMPLER_2D_ARRAY_DEPTH,     // 2D 数组深度采样器

    SAMPLER_CUBE_FLOAT,         // 立方体贴图浮点采样器
    SAMPLER_CUBE_INT,           // 立方体贴图有符号整数采样器
    SAMPLER_CUBE_UINT,          // 立方体贴图无符号整数采样器
    SAMPLER_CUBE_DEPTH,         // 立方体贴图深度采样器

    SAMPLER_CUBE_ARRAY_FLOAT,   // 立方体贴图数组浮点采样器（功能级别 2+）
    SAMPLER_CUBE_ARRAY_INT,     // 立方体贴图数组有符号整数采样器
    SAMPLER_CUBE_ARRAY_UINT,    // 立方体贴图数组无符号整数采样器
    SAMPLER_CUBE_ARRAY_DEPTH,   // 立方体贴图数组深度采样器

    SAMPLER_3D_FLOAT,           // 3D 浮点采样器
    SAMPLER_3D_INT,             // 3D 有符号整数采样器
    SAMPLER_3D_UINT,            // 3D 无符号整数采样器

    SAMPLER_2D_MS_FLOAT,        // 2D 多重采样浮点采样器（MSAA）
    SAMPLER_2D_MS_INT,          // 2D 多重采样有符号整数采样器
    SAMPLER_2D_MS_UINT,         // 2D 多重采样无符号整数采样器

    SAMPLER_2D_MS_ARRAY_FLOAT,  // 2D 多重采样数组浮点采样器
    SAMPLER_2D_MS_ARRAY_INT,    // 2D 多重采样数组有符号整数采样器
    SAMPLER_2D_MS_ARRAY_UINT,   // 2D 多重采样数组无符号整数采样器

    SAMPLER_EXTERNAL,           // 外部采样器（如相机预览、视频流）
    UNIFORM_BUFFER,             // Uniform 缓冲区（用于常量数据）
    SHADER_STORAGE_BUFFER,      // 着色器存储缓冲区（SSBO，用于计算着色器）
    INPUT_ATTACHMENT,           // 输入附件（用于子通道中读取之前的渲染结果）
};

/**
 * 检查描述符类型是否为深度采样器
 * 
 * @param type 描述符类型
 * @return 如果是深度采样器类型返回 true，否则返回 false
 * 
 * 支持的深度采样器类型：
 * - SAMPLER_2D_DEPTH
 * - SAMPLER_2D_ARRAY_DEPTH
 * - SAMPLER_CUBE_DEPTH
 * - SAMPLER_CUBE_ARRAY_DEPTH
 */
constexpr bool isDepthDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_DEPTH:
        case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:
        case DescriptorType::SAMPLER_CUBE_DEPTH:
        case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为浮点采样器
 * 
 * @param type 描述符类型
 * @return 如果是浮点采样器类型返回 true，否则返回 false
 * 
 * 支持的浮点采样器类型：
 * - 2D、2D 数组、立方体、立方体数组、3D、多重采样等浮点采样器
 */
constexpr bool isFloatDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_FLOAT:
        case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_CUBE_FLOAT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_3D_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为有符号整数采样器
 * 
 * @param type 描述符类型
 * @return 如果是有符号整数采样器类型返回 true，否则返回 false
 * 
 * 用途：用于存储和采样整数值的纹理（如 ID 缓冲区）
 */
constexpr bool isIntDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_INT:
        case DescriptorType::SAMPLER_2D_ARRAY_INT:
        case DescriptorType::SAMPLER_CUBE_INT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_INT:
        case DescriptorType::SAMPLER_3D_INT:
        case DescriptorType::SAMPLER_2D_MS_INT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为无符号整数采样器
 * 
 * @param type 描述符类型
 * @return 如果是无符号整数采样器类型返回 true，否则返回 false
 * 
 * 用途：用于存储和采样无符号整数值的纹理（如索引缓冲区）
 */
constexpr bool isUnsignedIntDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_UINT:
        case DescriptorType::SAMPLER_2D_ARRAY_UINT:
        case DescriptorType::SAMPLER_CUBE_UINT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:
        case DescriptorType::SAMPLER_3D_UINT:
        case DescriptorType::SAMPLER_2D_MS_UINT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为 3D 纹理采样器
 * 
 * @param type 描述符类型
 * @return 如果是 3D 纹理采样器类型返回 true，否则返回 false
 * 
 * 用途：用于体积纹理（如体积光照贴图、3D 噪声纹理等）
 */
constexpr bool is3dTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_3D_FLOAT:
        case DescriptorType::SAMPLER_3D_INT:
        case DescriptorType::SAMPLER_3D_UINT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为 2D 纹理采样器（非数组）
 * 
 * @param type 描述符类型
 * @return 如果是 2D 纹理采样器类型返回 true，否则返回 false
 * 
 * 包括普通 2D 和多重采样 2D 纹理，但不包括 2D 数组
 */
constexpr bool is2dTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_FLOAT:
        case DescriptorType::SAMPLER_2D_INT:
        case DescriptorType::SAMPLER_2D_UINT:
        case DescriptorType::SAMPLER_2D_DEPTH:
        case DescriptorType::SAMPLER_2D_MS_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_INT:
        case DescriptorType::SAMPLER_2D_MS_UINT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为 2D 数组纹理采样器
 * 
 * @param type 描述符类型
 * @return 如果是 2D 数组纹理采样器类型返回 true，否则返回 false
 * 
 * 用途：用于存储多个 2D 纹理的数组（如纹理图集、层叠渲染目标）
 */
constexpr bool is2dArrayTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_2D_ARRAY_INT:
        case DescriptorType::SAMPLER_2D_ARRAY_UINT:
        case DescriptorType::SAMPLER_2D_ARRAY_DEPTH:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为立方体贴图采样器（非数组）
 * 
 * @param type 描述符类型
 * @return 如果是立方体贴图采样器类型返回 true，否则返回 false
 * 
 * 用途：用于环境贴图、天空盒、反射贴图等
 */
constexpr bool isCubeTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_CUBE_FLOAT:
        case DescriptorType::SAMPLER_CUBE_INT:
        case DescriptorType::SAMPLER_CUBE_UINT:
        case DescriptorType::SAMPLER_CUBE_DEPTH:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为立方体贴图数组采样器
 * 
 * @param type 描述符类型
 * @return 如果是立方体贴图数组采样器类型返回 true，否则返回 false
 * 
 * 用途：用于存储多个立方体贴图（功能级别 2+）
 * 例如：点光源的阴影贴图立方体数组
 */
constexpr bool isCubeArrayTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_CUBE_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_INT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_UINT:
        case DescriptorType::SAMPLER_CUBE_ARRAY_DEPTH:
            return true;
        default: ;
    }
    return false;
}

/**
 * 检查描述符类型是否为多重采样纹理采样器
 * 
 * @param type 描述符类型
 * @return 如果是多重采样纹理采样器类型返回 true，否则返回 false
 * 
 * 用途：用于多重采样抗锯齿（MSAA）纹理
 * 注意：多重采样纹理不能直接采样，需要先解析（resolve）为普通纹理
 */
constexpr bool isMultiSampledTypeDescriptor(DescriptorType const type) noexcept {
    switch (type) {
        case DescriptorType::SAMPLER_2D_MS_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_INT:
        case DescriptorType::SAMPLER_2D_MS_UINT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_FLOAT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_INT:
        case DescriptorType::SAMPLER_2D_MS_ARRAY_UINT:
            return true;
        default: ;
    }
    return false;
}

/**
 * 将 DescriptorType 枚举转换为字符串
 * 
 * @param type 描述符类型枚举值
 * @return 描述符类型名称的字符串视图
 * 
 * 用途：用于调试输出和日志记录
 */
constexpr std::string_view to_string(DescriptorType type) noexcept {
    #define DESCRIPTOR_TYPE_CASE(TYPE)  case DescriptorType::TYPE: return #TYPE;
    switch (type) {
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_DEPTH)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_ARRAY_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_ARRAY_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_ARRAY_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_ARRAY_DEPTH)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_DEPTH)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_ARRAY_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_ARRAY_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_ARRAY_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_CUBE_ARRAY_DEPTH)
        DESCRIPTOR_TYPE_CASE(SAMPLER_3D_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_3D_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_3D_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_ARRAY_FLOAT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_ARRAY_INT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_2D_MS_ARRAY_UINT)
        DESCRIPTOR_TYPE_CASE(SAMPLER_EXTERNAL)
        DESCRIPTOR_TYPE_CASE(UNIFORM_BUFFER)
        DESCRIPTOR_TYPE_CASE(SHADER_STORAGE_BUFFER)
        DESCRIPTOR_TYPE_CASE(INPUT_ATTACHMENT)
    }
    return "UNKNOWN";
    #undef DESCRIPTOR_TYPE_CASE
}

/**
 * 描述符标志枚举
 * 
 * 位掩码枚举，用于描述符的额外属性。
 * 支持位运算（OR、AND 等）。
 */
enum class DescriptorFlags : uint8_t {
    NONE = 0x00,                    // 无标志

    // Indicate a UNIFORM_BUFFER will have dynamic offsets.
    /**
     * 动态偏移标志
     * 
     * 指示 UNIFORM_BUFFER 将使用动态偏移。
     * 用于在同一个 uniform 缓冲区中绑定不同的偏移范围。
     * 允许更高效地使用 uniform 缓冲区内存。
     */
    DYNAMIC_OFFSET = 0x01,

    // To indicate a texture/sampler type should be unfiltered.
    /**
     * 不可过滤标志
     * 
     * 指示纹理/采样器类型应该是未过滤的（仅最近邻采样）。
     * 用于整数纹理或需要精确像素值的场景。
     */
    UNFILTERABLE = 0x02,
};

/**
 * 描述符集索引类型
 * 
 * 用于标识描述符集的索引（0-255）。
 * 在 Vulkan 中，描述符集是资源绑定的集合。
 */
using descriptor_set_t = uint8_t;

/**
 * 描述符绑定索引类型
 * 
 * 用于标识描述符在描述符集中的绑定位置（0-255）。
 * 绑定索引在着色器中对应 binding 布局限定符。
 */
using descriptor_binding_t = uint8_t;

/**
 * 描述符集布局绑定结构体
 * 
 * 定义描述符集中一个绑定的配置。
 * 用于创建描述符集布局，指定资源的类型、绑定位置和使用阶段。
 */
struct DescriptorSetLayoutBinding {
    /**
     * 检查描述符类型是否为采样器类型
     * 
     * @param type 描述符类型
     * @return 如果是采样器类型返回 true，否则返回 false
     * 
     * 实现：所有采样器类型的枚举值都 <= SAMPLER_EXTERNAL
     */
    static bool isSampler(DescriptorType type) noexcept {
        return int(type) <= int(DescriptorType::SAMPLER_EXTERNAL);
    }
    
    /**
     * 检查描述符类型是否为缓冲区类型
     * 
     * @param type 描述符类型
     * @return 如果是缓冲区类型返回 true，否则返回 false
     */
    static bool isBuffer(DescriptorType type) noexcept {
        return type == DescriptorType::UNIFORM_BUFFER ||
               type == DescriptorType::SHADER_STORAGE_BUFFER;
    }
    
    DescriptorType type;              //!< 描述符类型（采样器、缓冲区等）
    ShaderStageFlags stageFlags;      //!< 使用此绑定的着色器阶段标志（位掩码）
    descriptor_binding_t binding;     //!< 绑定索引（对应着色器中的 binding 布局限定符）
    DescriptorFlags flags = DescriptorFlags::NONE;  //!< 描述符标志（动态偏移、不可过滤等）
    uint16_t count = 0;               //!< 数组元素数量（用于数组绑定，0 表示单个资源）

    /**
     * 比较两个描述符集布局绑定是否相等
     * 
     * @param lhs 左侧绑定
     * @param rhs 右侧绑定
     * @return 如果所有字段相等返回 true，否则返回 false
     * 
     * 比较字段：
     * - type: 描述符类型
     * - flags: 描述符标志
     * - count: 数组元素数量
     * - stageFlags: 着色器阶段标志
     */
    friend bool operator==(DescriptorSetLayoutBinding const& lhs,
            DescriptorSetLayoutBinding const& rhs) noexcept {
        return lhs.type == rhs.type &&
               lhs.flags == rhs.flags &&
               lhs.count == rhs.count &&
               lhs.stageFlags == rhs.stageFlags;
    }
};

/**
 * Bitmask for selecting render buffers
 */
/**
 * 目标缓冲区标志枚举
 * 
 * 位掩码枚举，用于选择渲染目标缓冲区。
 * 可以组合多个标志来选择多个缓冲区（如 COLOR0 | DEPTH）。
 * 
 * 用途：
 * - 指定哪些缓冲区需要清除
 * - 指定哪些缓冲区可以在渲染通道开始时丢弃
 * - 指定哪些缓冲区可以在渲染通道结束时丢弃
 */
enum class TargetBufferFlags : uint32_t {
    NONE = 0x0u,                            //!< No buffer selected.
    COLOR0 = 0x00000001u,                   //!< Color buffer selected.
    COLOR1 = 0x00000002u,                   //!< Color buffer selected.
    COLOR2 = 0x00000004u,                   //!< Color buffer selected.
    COLOR3 = 0x00000008u,                   //!< Color buffer selected.
    COLOR4 = 0x00000010u,                   //!< Color buffer selected.
    COLOR5 = 0x00000020u,                   //!< Color buffer selected.
    COLOR6 = 0x00000040u,                   //!< Color buffer selected.
    COLOR7 = 0x00000080u,                   //!< Color buffer selected.

    COLOR = COLOR0,                         //!< \deprecated
    COLOR_ALL = COLOR0 | COLOR1 | COLOR2 | COLOR3 | COLOR4 | COLOR5 | COLOR6 | COLOR7,
    DEPTH   = 0x10000000u,                  //!< Depth buffer selected.
    STENCIL = 0x20000000u,                  //!< Stencil buffer selected.
    DEPTH_AND_STENCIL = DEPTH | STENCIL,    //!< depth and stencil buffer selected.
    ALL = COLOR_ALL | DEPTH | STENCIL       //!< Color, depth and stencil buffer selected.
};

/**
 * 根据索引获取目标缓冲区标志
 * 
 * @param index 缓冲区索引
 *              0-7: 颜色缓冲区（COLOR0-COLOR7）
 *              8: 深度缓冲区
 *              9: 模板缓冲区
 * @return 对应的目标缓冲区标志，如果索引无效返回 NONE
 * 
 * 用途：用于循环处理多个渲染目标缓冲区
 */
constexpr TargetBufferFlags getTargetBufferFlagsAt(size_t index) noexcept {
    if (index == 0u) return TargetBufferFlags::COLOR0;
    if (index == 1u) return TargetBufferFlags::COLOR1;
    if (index == 2u) return TargetBufferFlags::COLOR2;
    if (index == 3u) return TargetBufferFlags::COLOR3;
    if (index == 4u) return TargetBufferFlags::COLOR4;
    if (index == 5u) return TargetBufferFlags::COLOR5;
    if (index == 6u) return TargetBufferFlags::COLOR6;
    if (index == 7u) return TargetBufferFlags::COLOR7;
    if (index == 8u) return TargetBufferFlags::DEPTH;
    if (index == 9u) return TargetBufferFlags::STENCIL;
    return TargetBufferFlags::NONE;
}

/**
 * How the buffer will be used.
 */
/**
 * 缓冲区使用方式枚举
 * 
 * 位掩码枚举，用于指定缓冲区的使用模式。
 * 帮助驱动优化缓冲区的内存分配和访问模式。
 * 
 * 注意：STATIC 和 DYNAMIC 是遗留值，应使用 DYNAMIC_BIT
 */
enum class BufferUsage : uint8_t {
    STATIC              = 0,    //!< (legacy) content modified once, used many times
                                //!< （遗留）内容修改一次，使用多次
    DYNAMIC             = 1,    //!< (legacy) content modified frequently, used many times
                                //!< （遗留）内容频繁修改，使用多次
    DYNAMIC_BIT         = 0x1,  //!< buffer can be modified frequently, used many times
                                //!< 缓冲区可以频繁修改，使用多次（用于顶点、索引缓冲区）
    SHARED_WRITE_BIT    = 0x04, //!< buffer can be memory mapped for write operations
                                //!< 缓冲区可以内存映射用于写入操作（允许 CPU 直接写入）
};

/**
 * How the buffer will be mapped.
 */
/**
 * 缓冲区映射访问标志枚举
 * 
 * 位掩码枚举，用于指定缓冲区内存映射的访问模式。
 * 用于 mapBuffer() 操作，控制 CPU 如何访问 GPU 缓冲区内存。
 */
enum class MapBufferAccessFlags : uint8_t {
    WRITE_BIT               = 0x2,  //!< buffer is mapped from writing
                                    //!< 缓冲区映射用于写入（CPU 可以写入数据）
    INVALIDATE_RANGE_BIT    = 0x4,  //!< the mapped range content is lost
                                    //!< 映射范围的内容将被丢弃（告诉驱动可以丢弃旧数据以优化性能）
};

/**
 * Defines a viewport, which is the origin and extent of the clip-space.
 * All drawing is clipped to the viewport.
 */
/**
 * 视口结构体
 * 
 * 定义视口，即裁剪空间的原点和范围。
 * 所有绘制都会被裁剪到视口范围内。
 * 
 * 坐标系统：
 * - 左下角为原点（left, bottom）
 * - X 轴向右为正，Y 轴向上为正
 * - 单位：像素
 * 
 * 用途：
 * - 控制渲染输出的区域
 * - 用于分屏渲染、画中画等场景
 */
struct Viewport {
    int32_t left;       //!< left coordinate in window space.
                        //!< 窗口空间中左边界坐标
    int32_t bottom;     //!< bottom coordinate in window space.
                        //!< 窗口空间中下边界坐标
    uint32_t width;     //!< width in pixels
                        //!< 宽度（像素）
    uint32_t height;    //!< height in pixels
                        //!< 高度（像素）
    
    /**
     * 获取视口右边界坐标
     * 
     * @return 窗口空间中右边界坐标
     * 
     * 计算：left + width
     */
    //! get the right coordinate in window space of the viewport
    int32_t right() const noexcept { return left + int32_t(width); }
    
    /**
     * 获取视口上边界坐标
     * 
     * @return 窗口空间中上边界坐标
     * 
     * 计算：bottom + height
     */
    //! get the top coordinate in window space of the viewport
    int32_t top() const noexcept { return bottom + int32_t(height); }

    /**
     * 比较两个视口是否相等
     * 
     * @param lhs 左侧视口
     * @param rhs 右侧视口
     * @return 如果所有字段相等返回 true，否则返回 false
     * 
     * 注意：clang 可以将其优化为无分支的 XOR/OR 操作
     */
    friend bool operator==(Viewport const& lhs, Viewport const& rhs) noexcept {
        // clang can do this branchless with xor/or
        return lhs.left == rhs.left && lhs.bottom == rhs.bottom &&
               lhs.width == rhs.width && lhs.height == rhs.height;
    }

    /**
     * 比较两个视口是否不相等
     * 
     * @param lhs 左侧视口
     * @param rhs 右侧视口
     * @return 如果有任何字段不相等返回 true，否则返回 false
     * 
     * 实现：使用 XOR 和 OR 操作进行位运算比较
     */
    friend bool operator!=(Viewport const& lhs, Viewport const& rhs) noexcept {
        // clang is being dumb and uses branches
        return bool(((lhs.left ^ rhs.left) | (lhs.bottom ^ rhs.bottom)) |
                    ((lhs.width ^ rhs.width) | (lhs.height ^ rhs.height)));
    }
};

/**
 * Specifies the mapping of the near and far clipping plane to window coordinates.
 */
/**
 * 深度范围结构体
 * 
 * 指定近裁剪平面和远裁剪平面到窗口坐标的映射。
 * 控制深度缓冲区值的范围。
 * 
 * 默认值：
 * - near = 0.0（近裁剪平面映射到深度值 0.0）
 * - far = 1.0（远裁剪平面映射到深度值 1.0）
 * 
 * 注意：
 * - 可以使用反向深度（near = 1.0, far = 0.0）以获得更好的浮点精度
 * - OpenGL 默认使用 [0, 1]，Vulkan 默认使用 [0, 1] 但可以配置
 */
struct DepthRange {
    float near = 0.0f;    //!< mapping of the near plane to window coordinates.
                          //!< 近裁剪平面到窗口坐标的映射（通常为 0.0）
    float far = 1.0f;     //!< mapping of the far plane to window coordinates.
                          //!< 远裁剪平面到窗口坐标的映射（通常为 1.0）
};

/**
 * Error codes for Fence::wait()
 * @see Fence, Fence::wait()
 */
/**
 * 栅栏状态枚举
 * 
 * 表示 Fence::wait() 操作的返回状态。
 * 用于同步 GPU 操作，确保操作完成后再继续。
 * 
 * @see Fence, Fence::wait()
 */
enum class FenceStatus : int8_t {
    ERROR = -1,                 //!< An error occurred. The Fence condition is not satisfied.
                                //!< 发生错误。栅栏条件未满足。
    CONDITION_SATISFIED = 0,    //!< The Fence condition is satisfied.
                                //!< 栅栏条件已满足（操作已完成）。
    TIMEOUT_EXPIRED = 1,        //!< wait()'s timeout expired. The Fence condition is not satisfied.
                                //!< wait() 超时。栅栏条件未满足（操作可能仍在进行中）。
};

/**
 * 栅栏等待超时值：永远等待
 * 
 * 值：uint64_t(-1) = 0xFFFFFFFFFFFFFFFF
 * 
 * 用途：传递给 Fence::wait() 表示无限期等待直到条件满足
 */
static constexpr uint64_t FENCE_WAIT_FOR_EVER = uint64_t(-1);

/**
 * Shader model.
 *
 * These enumerants are used across all backends and refer to a level of functionality and quality.
 *
 * For example, the OpenGL backend returns `MOBILE` if it supports OpenGL ES, or `DESKTOP` if it
 * supports Desktop OpenGL, this is later used to select the proper shader.
 *
 * Shader quality vs. performance is also affected by ShaderModel.
 */
/**
 * 着色器模型枚举
 * 
 * 在所有后端中使用，表示功能级别和质量级别。
 * 
 * 用途：
 * - OpenGL 后端：如果支持 OpenGL ES 返回 MOBILE，如果支持桌面 OpenGL 返回 DESKTOP
 * - 用于选择适当的着色器变体
 * - 影响着色器的质量和性能平衡
 * 
 * 示例：
 * - MOBILE：针对移动设备优化的着色器（性能优先）
 * - DESKTOP：针对桌面设备优化的着色器（质量优先）
 */
enum class ShaderModel : uint8_t {
    MOBILE  = 1,    //!< Mobile level functionality
                    //!< 移动设备级别功能（OpenGL ES）
    DESKTOP = 2,    //!< Desktop level functionality
                    //!< 桌面设备级别功能（桌面 OpenGL）
};
/**
 * 着色器模型数量
 * 
 * 用于数组大小和循环限制。
 */
static constexpr size_t SHADER_MODEL_COUNT = 2;

/**
 * 将 ShaderModel 枚举转换为字符串
 * 
 * @param model 着色器模型枚举值
 * @return 着色器模型名称的字符串视图
 * 
 * 返回值：
 * - MOBILE: "mobile"
 * - DESKTOP: "desktop"
 */
constexpr std::string_view to_string(ShaderModel model) noexcept {
    switch (model) {
        case ShaderModel::MOBILE:
            return "mobile";
        case ShaderModel::DESKTOP:
            return "desktop";
    }
}

/**
 * Primitive types
 */
/**
 * 图元类型枚举
 * 
 * 定义几何图元的类型，用于绘制调用。
 * 
 * 注意：不要更改枚举值（与 OpenGL 匹配）
 */
enum class PrimitiveType : uint8_t {
    // don't change the enums values (made to match GL)
    POINTS         = 0,    //!< points
                            //!< 点图元（每个顶点渲染为一个点）
    LINES          = 1,    //!< lines
                            //!< 线段图元（每两个顶点渲染为一条线段）
    LINE_STRIP     = 3,    //!< line strip
                            //!< 线带图元（顶点连接成连续的线段）
    TRIANGLES      = 4,    //!< triangles
                            //!< 三角形图元（每三个顶点渲染为一个三角形）
    TRIANGLE_STRIP = 5     //!< triangle strip
                            //!< 三角带图元（顶点连接成连续的三角形带）
};

/**
 * 检查图元类型是否为条带类型
 * 
 * @param type 图元类型
 * @return 如果是条带类型（LINE_STRIP 或 TRIANGLE_STRIP）返回 true，否则返回 false
 * 
 * 条带类型：
 * - LINE_STRIP：线带
 * - TRIANGLE_STRIP：三角带
 * 
 * 非条带类型：
 * - POINTS：点
 * - LINES：线段
 * - TRIANGLES：三角形
 */
[[nodiscard]] constexpr bool isStripPrimitiveType(const PrimitiveType type) {
    switch (type) {
        case PrimitiveType::POINTS:
        case PrimitiveType::LINES:
        case PrimitiveType::TRIANGLES:
            return false;
        case PrimitiveType::LINE_STRIP:
        case PrimitiveType::TRIANGLE_STRIP:
            return true;
    }
}

/**
 * Supported uniform types
 */
/**
 * Uniform 类型枚举
 * 
 * 定义着色器中支持的 uniform 变量类型。
 * 用于材质参数和着色器常量。
 */
enum class UniformType : uint8_t {
    BOOL,       // 布尔值
    BOOL2,      // 2 分量布尔向量
    BOOL3,      // 3 分量布尔向量
    BOOL4,      // 4 分量布尔向量
    FLOAT,      // 浮点数
    FLOAT2,     // 2 分量浮点向量（vec2）
    FLOAT3,     // 3 分量浮点向量（vec3）
    FLOAT4,     // 4 分量浮点向量（vec4）
    INT,        // 有符号整数
    INT2,       // 2 分量整数向量（ivec2）
    INT3,       // 3 分量整数向量（ivec3）
    INT4,       // 4 分量整数向量（ivec4）
    UINT,       // 无符号整数
    UINT2,      // 2 分量无符号整数向量（uvec2）
    UINT3,      // 3 分量无符号整数向量（uvec3）
    UINT4,      // 4 分量无符号整数向量（uvec4）
    MAT3,       //!< a 3x3 float matrix
                //!< 3x3 浮点矩阵
    MAT4,       //!< a 4x4 float matrix
                //!< 4x4 浮点矩阵
    STRUCT      // 结构体类型（用于复杂的 uniform 数据）
};

/**
 * Supported constant parameter types
 */
/**
 * 常量参数类型枚举
 * 
 * 定义着色器常量参数的类型。
 * 用于编译时常量，在着色器编译时确定值。
 */
enum class ConstantType : uint8_t {
  INT,    // 整数常量
  FLOAT,  // 浮点数常量
  BOOL    // 布尔常量
};

/**
 * 精度级别枚举
 * 
 * 定义着色器中数值类型的精度级别。
 * 用于控制浮点数计算的精度和性能。
 * 
 * 用途：
 * - LOW：低精度（移动设备，性能优先）
 * - MEDIUM：中等精度（平衡质量和性能）
 * - HIGH：高精度（桌面设备，质量优先）
 * - DEFAULT：使用默认精度（由着色器语言决定）
 */
enum class Precision : uint8_t {
    LOW,        // 低精度（适用于移动设备）
    MEDIUM,     // 中等精度（默认选择）
    HIGH,       // 高精度（适用于桌面设备）
    DEFAULT     // 默认精度（使用着色器语言默认值）
};

/**
 * 常量值联合体
 * 
 * 用于存储不同类型的常量值。
 * 根据 ConstantType 使用相应的成员。
 */
union ConstantValue {
    int32_t i;  // 整数常量值
    float f;    // 浮点数常量值
    bool b;     // 布尔常量值
};

/**
 * Shader compiler priority queue
 *
 * On platforms which support parallel shader compilation, compilation requests will be processed in
 * order of priority, then insertion order. See Material::compile().
 */
/**
 * 着色器编译器优先级队列枚举
 * 
 * 在支持并行着色器编译的平台上，编译请求将按优先级然后按插入顺序处理。
 * 
 * @see Material::compile()
 * 
 * 用途：
 * - 控制着色器编译的优先级
 * - 优化编译性能和用户体验
 * - 优先编译立即需要的着色器
 */
enum class CompilerPriorityQueue : uint8_t {
    /** We need this program NOW.
     *
     * When passed as an argument to Material::compile(), if the platform doesn't support parallel
     * compilation, but does support amortized shader compilation, the given shader program will be
     * synchronously compiled.
     */
    /**
     * 关键优先级
     * 
     * 立即需要此着色器程序。
     * 
     * 如果平台不支持并行编译但支持分摊着色器编译，
     * 传递此优先级时，给定的着色器程序将同步编译。
     */
    CRITICAL,
    /** We will need this program soon. */
    /**
     * 高优先级
     * 
     * 很快将需要此着色器程序。
     */
    HIGH,
    /** We will need this program eventually. */
    /**
     * 低优先级
     * 
     * 最终将需要此着色器程序（可以在后台编译）。
     */
    LOW
};

//! Texture sampler type
enum class SamplerType : uint8_t {
    SAMPLER_2D,             //!< 2D texture
    SAMPLER_2D_ARRAY,       //!< 2D array texture
    SAMPLER_CUBEMAP,        //!< Cube map texture
    SAMPLER_EXTERNAL,       //!< External texture
    SAMPLER_3D,             //!< 3D texture
    SAMPLER_CUBEMAP_ARRAY,  //!< Cube map array texture (feature level 2)
};

constexpr std::string_view to_string(SamplerType const type) noexcept {
    switch (type) {
        case SamplerType::SAMPLER_2D:
            return "SAMPLER_2D";
        case SamplerType::SAMPLER_2D_ARRAY:
            return "SAMPLER_2D_ARRAY";
        case SamplerType::SAMPLER_CUBEMAP:
            return "SAMPLER_CUBEMAP";
        case SamplerType::SAMPLER_EXTERNAL:
            return "SAMPLER_EXTERNAL";
        case SamplerType::SAMPLER_3D:
            return "SAMPLER_3D";
        case SamplerType::SAMPLER_CUBEMAP_ARRAY:
            return "SAMPLER_CUBEMAP_ARRAY";
    }
    return "Unknown";
}

//! Subpass type
/**
 * 子通道类型枚举
 * 
 * 定义渲染子通道的类型。
 * 用于 Vulkan 风格的子通道渲染，允许在单个渲染通道中进行多次渲染。
 */
enum class SubpassType : uint8_t {
    SUBPASS_INPUT   // 子通道输入（用于在后续子通道中读取之前的渲染结果）
};

//! Texture sampler format
enum class SamplerFormat : uint8_t {
    INT = 0,        //!< signed integer sampler
    UINT = 1,       //!< unsigned integer sampler
    FLOAT = 2,      //!< float sampler
    SHADOW = 3      //!< shadow sampler (PCF)
};

constexpr std::string_view to_string(SamplerFormat const format) noexcept {
    switch (format) {
        case SamplerFormat::INT:
            return "INT";
        case SamplerFormat::UINT:
            return "UINT";
        case SamplerFormat::FLOAT:
            return "FLOAT";
        case SamplerFormat::SHADOW:
            return "SHADOW";
    }
    return "Unknown";
}

/**
 * Supported element types
 */
/**
 * 元素类型枚举
 * 
 * 定义顶点属性缓冲区中的数据类型。
 * 用于指定顶点数据的格式和大小。
 * 
 * 命名规则：
 * - 类型：BYTE（8位有符号）、UBYTE（8位无符号）、SHORT（16位有符号）、
 *         USHORT（16位无符号）、INT（32位有符号）、UINT（32位无符号）、
 *         FLOAT（32位浮点）、HALF（16位半精度浮点）
 * - 数字后缀：表示向量分量数量（1、2、3、4）
 */
enum class ElementType : uint8_t {
    BYTE,       // 8 位有符号整数
    BYTE2,      // 2 分量 8 位有符号整数向量
    BYTE3,      // 3 分量 8 位有符号整数向量
    BYTE4,      // 4 分量 8 位有符号整数向量
    UBYTE,      // 8 位无符号整数
    UBYTE2,     // 2 分量 8 位无符号整数向量
    UBYTE3,     // 3 分量 8 位无符号整数向量
    UBYTE4,     // 4 分量 8 位无符号整数向量
    SHORT,      // 16 位有符号整数
    SHORT2,     // 2 分量 16 位有符号整数向量
    SHORT3,     // 3 分量 16 位有符号整数向量
    SHORT4,     // 4 分量 16 位有符号整数向量
    USHORT,     // 16 位无符号整数
    USHORT2,    // 2 分量 16 位无符号整数向量
    USHORT3,    // 3 分量 16 位无符号整数向量
    USHORT4,    // 4 分量 16 位无符号整数向量
    INT,        // 32 位有符号整数
    UINT,       // 32 位无符号整数
    FLOAT,      // 32 位浮点数
    FLOAT2,     // 2 分量 32 位浮点向量
    FLOAT3,     // 3 分量 32 位浮点向量
    FLOAT4,     // 4 分量 32 位浮点向量
    HALF,       // 16 位半精度浮点数
    HALF2,      // 2 分量 16 位半精度浮点向量
    HALF3,      // 3 分量 16 位半精度浮点向量
    HALF4,      // 4 分量 16 位半精度浮点向量
};

//! Buffer object binding type
/**
 * 缓冲区对象绑定类型枚举
 * 
 * 定义缓冲区的绑定目标类型。
 * 用于指定缓冲区在 GPU 中的用途。
 */
enum class BufferObjectBinding : uint8_t {
    VERTEX,         // 顶点缓冲区（用于存储顶点数据）
    UNIFORM,        // Uniform 缓冲区（用于存储常量数据）
    SHADER_STORAGE  // 着色器存储缓冲区（SSBO，用于计算着色器的读写访问）
};

/**
 * 将 BufferObjectBinding 枚举转换为字符串
 * 
 * @param type 缓冲区对象绑定类型枚举值
 * @return 绑定类型名称的字符串视图
 * 
 * 返回值：
 * - VERTEX: "VERTEX"
 * - UNIFORM: "UNIFORM"
 * - SHADER_STORAGE: "SHADER_STORAGE"
 * - 其他: "UNKNOWN"
 */
constexpr std::string_view to_string(BufferObjectBinding type) noexcept {
    switch (type) {
        case BufferObjectBinding::VERTEX:           return "VERTEX";
        case BufferObjectBinding::UNIFORM:          return "UNIFORM";
        case BufferObjectBinding::SHADER_STORAGE:   return "SHADER_STORAGE";
    }
    return "UNKNOWN";
}

//! Face culling Mode
enum class CullingMode : uint8_t {
    NONE,               //!< No culling, front and back faces are visible
    FRONT,              //!< Front face culling, only back faces are visible
    BACK,               //!< Back face culling, only front faces are visible
    FRONT_AND_BACK      //!< Front and Back, geometry is not visible
};

//! Pixel Data Format
enum class PixelDataFormat : uint8_t {
    R,                  //!< One Red channel, float
    R_INTEGER,          //!< One Red channel, integer
    RG,                 //!< Two Red and Green channels, float
    RG_INTEGER,         //!< Two Red and Green channels, integer
    RGB,                //!< Three Red, Green and Blue channels, float
    RGB_INTEGER,        //!< Three Red, Green and Blue channels, integer
    RGBA,               //!< Four Red, Green, Blue and Alpha channels, float
    RGBA_INTEGER,       //!< Four Red, Green, Blue and Alpha channels, integer
    UNUSED,             // used to be rgbm
    DEPTH_COMPONENT,    //!< Depth, 16-bit or 24-bits usually
    DEPTH_STENCIL,      //!< Two Depth (24-bits) + Stencil (8-bits) channels
    ALPHA               //! One Alpha channel, float
};

//! Pixel Data Type
enum class PixelDataType : uint8_t {
    UBYTE,                //!< unsigned byte
    BYTE,                 //!< signed byte
    USHORT,               //!< unsigned short (16-bit)
    SHORT,                //!< signed short (16-bit)
    UINT,                 //!< unsigned int (32-bit)
    INT,                  //!< signed int (32-bit)
    HALF,                 //!< half-float (16-bit float)
    FLOAT,                //!< float (32-bits float)
    COMPRESSED,           //!< compressed pixels, @see CompressedPixelDataType
    UINT_10F_11F_11F_REV, //!< three low precision floating-point numbers
    USHORT_565,           //!< unsigned int (16-bit), encodes 3 RGB channels
    UINT_2_10_10_10_REV,  //!< unsigned normalized 10 bits RGB, 2 bits alpha
};

//! Compressed pixel data types
enum class CompressedPixelDataType : uint16_t {
    // Mandatory in GLES 3.0 and GL 4.3
    EAC_R11, EAC_R11_SIGNED, EAC_RG11, EAC_RG11_SIGNED,
    ETC2_RGB8, ETC2_SRGB8,
    ETC2_RGB8_A1, ETC2_SRGB8_A1,
    ETC2_EAC_RGBA8, ETC2_EAC_SRGBA8,

    // Available everywhere except Android/iOS
    DXT1_RGB, DXT1_RGBA, DXT3_RGBA, DXT5_RGBA,
    DXT1_SRGB, DXT1_SRGBA, DXT3_SRGBA, DXT5_SRGBA,

    // ASTC formats are available with a GLES extension
    RGBA_ASTC_4x4,
    RGBA_ASTC_5x4,
    RGBA_ASTC_5x5,
    RGBA_ASTC_6x5,
    RGBA_ASTC_6x6,
    RGBA_ASTC_8x5,
    RGBA_ASTC_8x6,
    RGBA_ASTC_8x8,
    RGBA_ASTC_10x5,
    RGBA_ASTC_10x6,
    RGBA_ASTC_10x8,
    RGBA_ASTC_10x10,
    RGBA_ASTC_12x10,
    RGBA_ASTC_12x12,
    SRGB8_ALPHA8_ASTC_4x4,
    SRGB8_ALPHA8_ASTC_5x4,
    SRGB8_ALPHA8_ASTC_5x5,
    SRGB8_ALPHA8_ASTC_6x5,
    SRGB8_ALPHA8_ASTC_6x6,
    SRGB8_ALPHA8_ASTC_8x5,
    SRGB8_ALPHA8_ASTC_8x6,
    SRGB8_ALPHA8_ASTC_8x8,
    SRGB8_ALPHA8_ASTC_10x5,
    SRGB8_ALPHA8_ASTC_10x6,
    SRGB8_ALPHA8_ASTC_10x8,
    SRGB8_ALPHA8_ASTC_10x10,
    SRGB8_ALPHA8_ASTC_12x10,
    SRGB8_ALPHA8_ASTC_12x12,

    // RGTC formats available with a GLES extension
    RED_RGTC1,              // BC4 unsigned
    SIGNED_RED_RGTC1,       // BC4 signed
    RED_GREEN_RGTC2,        // BC5 unsigned
    SIGNED_RED_GREEN_RGTC2, // BC5 signed

    // BPTC formats available with a GLES extension
    RGB_BPTC_SIGNED_FLOAT,  // BC6H signed
    RGB_BPTC_UNSIGNED_FLOAT,// BC6H unsigned
    RGBA_BPTC_UNORM,        // BC7
    SRGB_ALPHA_BPTC_UNORM,  // BC7 sRGB
};

/** Supported texel formats
 * These formats are typically used to specify a texture's internal storage format.
 *
 * Enumerants syntax format
 * ========================
 *
 * `[components][size][type]`
 *
 * `components` : List of stored components by this format.\n
 * `size`       : Size in bit of each component.\n
 * `type`       : Type this format is stored as.\n
 *
 *
 * Name     | Component
 * :--------|:-------------------------------
 * R        | Linear Red
 * RG       | Linear Red, Green
 * RGB      | Linear Red, Green, Blue
 * RGBA     | Linear Red, Green Blue, Alpha
 * SRGB     | sRGB encoded Red, Green, Blue
 * DEPTH    | Depth
 * STENCIL  | Stencil
 *
 * \n
 * Name     | Type
 * :--------|:---------------------------------------------------
 * (none)   | Unsigned Normalized Integer [0, 1]
 * _SNORM   | Signed Normalized Integer [-1, 1]
 * UI       | Unsigned Integer @f$ [0, 2^{size}] @f$
 * I        | Signed Integer @f$ [-2^{size-1}, 2^{size-1}-1] @f$
 * F        | Floating-point
 *
 *
 * Special color formats
 * ---------------------
 *
 * There are a few special color formats that don't follow the convention above:
 *
 * Name             | Format
 * :----------------|:--------------------------------------------------------------------------
 * RGB565           |  5-bits for R and B, 6-bits for G.
 * RGB5_A1          |  5-bits for R, G and B, 1-bit for A.
 * RGB10_A2         | 10-bits for R, G and B, 2-bits for A.
 * RGB9_E5          | **Unsigned** floating point. 9-bits mantissa for RGB, 5-bits shared exponent
 * R11F_G11F_B10F   | **Unsigned** floating point. 6-bits mantissa, for R and G, 5-bits for B. 5-bits exponent.
 * SRGB8_A8         | sRGB 8-bits with linear 8-bits alpha.
 * DEPTH24_STENCIL8 | 24-bits unsigned normalized integer depth, 8-bits stencil.
 * DEPTH32F_STENCIL8| 32-bits floating-point depth, 8-bits stencil.
 *
 *
 * Compressed texture formats
 * --------------------------
 *
 * Many compressed texture formats are supported as well, which include (but are not limited to)
 * the following list:
 *
 * Name             | Format
 * :----------------|:--------------------------------------------------------------------------
 * EAC_R11          | Compresses R11UI
 * EAC_R11_SIGNED   | Compresses R11I
 * EAC_RG11         | Compresses RG11UI
 * EAC_RG11_SIGNED  | Compresses RG11I
 * ETC2_RGB8        | Compresses RGB8
 * ETC2_SRGB8       | compresses SRGB8
 * ETC2_EAC_RGBA8   | Compresses RGBA8
 * ETC2_EAC_SRGBA8  | Compresses SRGB8_A8
 * ETC2_RGB8_A1     | Compresses RGB8 with 1-bit alpha
 * ETC2_SRGB8_A1    | Compresses sRGB8 with 1-bit alpha
 *
 *
 * @see Texture
 */
enum class TextureFormat : uint16_t {
    // 8-bits per element
    R8, R8_SNORM, R8UI, R8I, STENCIL8,

    // 16-bits per element
    R16F, R16UI, R16I,
    RG8, RG8_SNORM, RG8UI, RG8I,
    RGB565,
    RGB9_E5, // 9995 is actually 32 bpp but it's here for historical reasons.
    RGB5_A1,
    RGBA4,
    DEPTH16,

    // 24-bits per element
    RGB8, SRGB8, RGB8_SNORM, RGB8UI, RGB8I,
    DEPTH24,

    // 32-bits per element
    R32F, R32UI, R32I,
    RG16F, RG16UI, RG16I,
    R11F_G11F_B10F,
    RGBA8, SRGB8_A8,RGBA8_SNORM,
    UNUSED, // used to be rgbm
    RGB10_A2, RGBA8UI, RGBA8I,
    DEPTH32F, DEPTH24_STENCIL8, DEPTH32F_STENCIL8,

    // 48-bits per element
    RGB16F, RGB16UI, RGB16I,

    // 64-bits per element
    RG32F, RG32UI, RG32I,
    RGBA16F, RGBA16UI, RGBA16I,

    // 96-bits per element
    RGB32F, RGB32UI, RGB32I,

    // 128-bits per element
    RGBA32F, RGBA32UI, RGBA32I,

    // compressed formats

    // Mandatory in GLES 3.0 and GL 4.3
    EAC_R11, EAC_R11_SIGNED, EAC_RG11, EAC_RG11_SIGNED,
    ETC2_RGB8, ETC2_SRGB8,
    ETC2_RGB8_A1, ETC2_SRGB8_A1,
    ETC2_EAC_RGBA8, ETC2_EAC_SRGBA8,

    // Available everywhere except Android/iOS
    DXT1_RGB, DXT1_RGBA, DXT3_RGBA, DXT5_RGBA,
    DXT1_SRGB, DXT1_SRGBA, DXT3_SRGBA, DXT5_SRGBA,

    // ASTC formats are available with a GLES extension
    RGBA_ASTC_4x4,
    RGBA_ASTC_5x4,
    RGBA_ASTC_5x5,
    RGBA_ASTC_6x5,
    RGBA_ASTC_6x6,
    RGBA_ASTC_8x5,
    RGBA_ASTC_8x6,
    RGBA_ASTC_8x8,
    RGBA_ASTC_10x5,
    RGBA_ASTC_10x6,
    RGBA_ASTC_10x8,
    RGBA_ASTC_10x10,
    RGBA_ASTC_12x10,
    RGBA_ASTC_12x12,
    SRGB8_ALPHA8_ASTC_4x4,
    SRGB8_ALPHA8_ASTC_5x4,
    SRGB8_ALPHA8_ASTC_5x5,
    SRGB8_ALPHA8_ASTC_6x5,
    SRGB8_ALPHA8_ASTC_6x6,
    SRGB8_ALPHA8_ASTC_8x5,
    SRGB8_ALPHA8_ASTC_8x6,
    SRGB8_ALPHA8_ASTC_8x8,
    SRGB8_ALPHA8_ASTC_10x5,
    SRGB8_ALPHA8_ASTC_10x6,
    SRGB8_ALPHA8_ASTC_10x8,
    SRGB8_ALPHA8_ASTC_10x10,
    SRGB8_ALPHA8_ASTC_12x10,
    SRGB8_ALPHA8_ASTC_12x12,

    // RGTC formats available with a GLES extension
    RED_RGTC1,              // BC4 unsigned
    SIGNED_RED_RGTC1,       // BC4 signed
    RED_GREEN_RGTC2,        // BC5 unsigned
    SIGNED_RED_GREEN_RGTC2, // BC5 signed

    // BPTC formats available with a GLES extension
    RGB_BPTC_SIGNED_FLOAT,  // BC6H signed
    RGB_BPTC_UNSIGNED_FLOAT,// BC6H unsigned
    RGBA_BPTC_UNORM,        // BC7
    SRGB_ALPHA_BPTC_UNORM,  // BC7 sRGB
};

TextureType getTextureType(TextureFormat format) noexcept;

//! Bitmask describing the intended Texture Usage
enum class TextureUsage : uint16_t {
    NONE                = 0x0000,
    COLOR_ATTACHMENT    = 0x0001,            //!< Texture can be used as a color attachment
    DEPTH_ATTACHMENT    = 0x0002,            //!< Texture can be used as a depth attachment
    STENCIL_ATTACHMENT  = 0x0004,            //!< Texture can be used as a stencil attachment
    UPLOADABLE          = 0x0008,            //!< Data can be uploaded into this texture (default)
    SAMPLEABLE          = 0x0010,            //!< Texture can be sampled (default)
    SUBPASS_INPUT       = 0x0020,            //!< Texture can be used as a subpass input
    BLIT_SRC            = 0x0040,            //!< Texture can be used the source of a blit()
    BLIT_DST            = 0x0080,            //!< Texture can be used the destination of a blit()
    PROTECTED           = 0x0100,            //!< Texture can be used for protected content
    GEN_MIPMAPPABLE     = 0x0200,            //!< Texture can be used with generateMipmaps()
    DEFAULT             = UPLOADABLE | SAMPLEABLE,   //!< Default texture usage
    ALL_ATTACHMENTS     = COLOR_ATTACHMENT | DEPTH_ATTACHMENT | STENCIL_ATTACHMENT | SUBPASS_INPUT,   //!< Mask of all attachments
};

//! Texture swizzle
enum class TextureSwizzle : uint8_t {
    SUBSTITUTE_ZERO,
    SUBSTITUTE_ONE,
    CHANNEL_0,
    CHANNEL_1,
    CHANNEL_2,
    CHANNEL_3
};

//! returns whether this format a depth format
/**
 * 检查纹理格式是否为深度格式
 * 
 * @param format 纹理格式
 * @return 如果是深度格式返回 true，否则返回 false
 * 
 * 深度格式包括：
 * - DEPTH16、DEPTH24、DEPTH32F
 * - DEPTH24_STENCIL8、DEPTH32F_STENCIL8
 */
constexpr bool isDepthFormat(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::DEPTH32F:
        case TextureFormat::DEPTH24:
        case TextureFormat::DEPTH16:
        case TextureFormat::DEPTH32F_STENCIL8:
        case TextureFormat::DEPTH24_STENCIL8:
            return true;
        default:
            return false;
    }
}

/**
 * 检查纹理格式是否为模板格式
 * 
 * @param format 纹理格式
 * @return 如果是模板格式返回 true，否则返回 false
 * 
 * 模板格式包括：
 * - STENCIL8
 * - DEPTH24_STENCIL8（深度+模板组合）
 * - DEPTH32F_STENCIL8（深度+模板组合）
 */
constexpr bool isStencilFormat(TextureFormat format) noexcept {
    switch (format) {
        case TextureFormat::STENCIL8:
        case TextureFormat::DEPTH24_STENCIL8:
        case TextureFormat::DEPTH32F_STENCIL8:
            return true;
        default:
            return false;
    }
}

/**
 * 检查纹理格式是否为颜色格式
 * 
 * @param format 纹理格式
 * @return 如果是颜色格式返回 true，否则返回 false
 * 
 * 颜色格式包括：
 * - 标准颜色格式（R、RG、RGB、RGBA）
 * - 浮点格式（R16F、RG16F、RGBA16F、R32F、RG32F、RGBA32F）
 * - 特殊格式（RGB10_A2、R11F_G11F_B10F、SRGB8、SRGB8_A8、RGB565、RGB5_A1、RGBA4）
 */
constexpr bool isColorFormat(TextureFormat format) noexcept {
    switch (format) {
        // Standard color formats
        case TextureFormat::R8:
        case TextureFormat::RG8:
        case TextureFormat::RGBA8:
        case TextureFormat::R16F:
        case TextureFormat::RG16F:
        case TextureFormat::RGBA16F:
        case TextureFormat::R32F:
        case TextureFormat::RG32F:
        case TextureFormat::RGBA32F:
        case TextureFormat::RGB10_A2:
        case TextureFormat::R11F_G11F_B10F:
        case TextureFormat::SRGB8:
        case TextureFormat::SRGB8_A8:
        case TextureFormat::RGB8:
        case TextureFormat::RGB565:
        case TextureFormat::RGB5_A1:
        case TextureFormat::RGBA4:
            return true;
        default:
            break;
    }
    return false;
}

/**
 * 检查纹理格式是否为无符号整数格式
 * 
 * @param format 纹理格式
 * @return 如果是无符号整数格式返回 true，否则返回 false
 * 
 * 无符号整数格式：
 * - R8UI、R16UI、R32UI
 * - RG8UI、RG16UI、RG32UI
 * - RGB8UI、RGB16UI、RGB32UI
 * - RGBA8UI、RGBA16UI、RGBA32UI
 */
constexpr bool isUnsignedIntFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8UI:
        case TextureFormat::R16UI:
        case TextureFormat::R32UI:
        case TextureFormat::RG8UI:
        case TextureFormat::RG16UI:
        case TextureFormat::RG32UI:
        case TextureFormat::RGB8UI:
        case TextureFormat::RGB16UI:
        case TextureFormat::RGB32UI:
        case TextureFormat::RGBA8UI:
        case TextureFormat::RGBA16UI:
        case TextureFormat::RGBA32UI:
            return true;

        default:
            return false;
    }
}

/**
 * 检查纹理格式是否为有符号整数格式
 * 
 * @param format 纹理格式
 * @return 如果是有符号整数格式返回 true，否则返回 false
 * 
 * 有符号整数格式：
 * - R8I、R16I、R32I
 * - RG8I、RG16I、RG32I
 * - RGB8I、RGB16I、RGB32I
 * - RGBA8I、RGBA16I、RGBA32I
 */
constexpr bool isSignedIntFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8I:
        case TextureFormat::R16I:
        case TextureFormat::R32I:
        case TextureFormat::RG8I:
        case TextureFormat::RG16I:
        case TextureFormat::RG32I:
        case TextureFormat::RGB8I:
        case TextureFormat::RGB16I:
        case TextureFormat::RGB32I:
        case TextureFormat::RGBA8I:
        case TextureFormat::RGBA16I:
        case TextureFormat::RGBA32I:
            return true;

        default:
            return false;
    }
}

//! returns whether this format is a compressed format
/**
 * 检查纹理格式是否为压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是压缩格式返回 true，否则返回 false
 * 
 * 实现：所有压缩格式的枚举值都 >= EAC_R11
 * 包括：ETC2、S3TC/DXT、ASTC、RGTC、BPTC 等
 */
constexpr bool isCompressedFormat(TextureFormat format) noexcept {
    return format >= TextureFormat::EAC_R11;
}

//! returns whether this format is an ETC2 compressed format
/**
 * 检查纹理格式是否为 ETC2 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 ETC2 压缩格式返回 true，否则返回 false
 * 
 * ETC2 格式包括：
 * - EAC_R11、EAC_R11_SIGNED、EAC_RG11、EAC_RG11_SIGNED
 * - ETC2_RGB8、ETC2_SRGB8
 * - ETC2_RGB8_A1、ETC2_SRGB8_A1
 * - ETC2_EAC_RGBA8、ETC2_EAC_SRGBA8
 * 
 * 注意：ETC2 是 OpenGL ES 3.0 和 OpenGL 4.3 的强制要求格式
 */
constexpr bool isETC2Compression(TextureFormat format) noexcept {
    return format >= TextureFormat::EAC_R11 && format <= TextureFormat::ETC2_EAC_SRGBA8;
}

//! returns whether this format is an S3TC compressed format
/**
 * 检查纹理格式是否为 S3TC/DXT 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 S3TC 压缩格式返回 true，否则返回 false
 * 
 * S3TC/DXT 格式包括：
 * - DXT1_RGB、DXT1_RGBA、DXT3_RGBA、DXT5_RGBA
 * - DXT1_SRGB、DXT1_SRGBA、DXT3_SRGBA、DXT5_SRGBA
 * 
 * 注意：S3TC 在 Android/iOS 上不可用，在桌面平台上可用
 */
constexpr bool isS3TCCompression(TextureFormat format) noexcept {
    return format >= TextureFormat::DXT1_RGB && format <= TextureFormat::DXT5_SRGBA;
}

/**
 * 检查纹理格式是否为 S3TC/DXT sRGB 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 S3TC sRGB 压缩格式返回 true，否则返回 false
 * 
 * S3TC sRGB 格式包括：
 * - DXT1_SRGB、DXT1_SRGBA、DXT3_SRGBA、DXT5_SRGBA
 */
constexpr bool isS3TCSRGBCompression(TextureFormat format) noexcept {
    return format >= TextureFormat::DXT1_SRGB && format <= TextureFormat::DXT5_SRGBA;
}

//! returns whether this format is an RGTC compressed format
/**
 * 检查纹理格式是否为 RGTC/BC4/BC5 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 RGTC 压缩格式返回 true，否则返回 false
 * 
 * RGTC 格式包括：
 * - RED_RGTC1（BC4 无符号）
 * - SIGNED_RED_RGTC1（BC4 有符号）
 * - RED_GREEN_RGTC2（BC5 无符号）
 * - SIGNED_RED_GREEN_RGTC2（BC5 有符号）
 * 
 * 注意：RGTC 需要通过 GLES 扩展提供
 */
constexpr bool isRGTCCompression(TextureFormat format) noexcept {
    return format >= TextureFormat::RED_RGTC1 && format <= TextureFormat::SIGNED_RED_GREEN_RGTC2;
}

//! returns whether this format is an BPTC compressed format
/**
 * 检查纹理格式是否为 BPTC/BC6H/BC7 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 BPTC 压缩格式返回 true，否则返回 false
 * 
 * BPTC 格式包括：
 * - RGB_BPTC_SIGNED_FLOAT（BC6H 有符号浮点）
 * - RGB_BPTC_UNSIGNED_FLOAT（BC6H 无符号浮点）
 * - RGBA_BPTC_UNORM（BC7）
 * - SRGB_ALPHA_BPTC_UNORM（BC7 sRGB）
 * 
 * 注意：BPTC 需要通过 GLES 扩展提供
 */
constexpr bool isBPTCCompression(TextureFormat format) noexcept {
    return format >= TextureFormat::RGB_BPTC_SIGNED_FLOAT && format <= TextureFormat::SRGB_ALPHA_BPTC_UNORM;
}

/**
 * 检查纹理格式是否为 ASTC 压缩格式
 * 
 * @param format 纹理格式
 * @return 如果是 ASTC 压缩格式返回 true，否则返回 false
 * 
 * ASTC 格式包括：
 * - RGBA_ASTC_4x4 到 RGBA_ASTC_12x12（线性）
 * - SRGB8_ALPHA8_ASTC_4x4 到 SRGB8_ALPHA8_ASTC_12x12（sRGB）
 * 
 * 注意：ASTC 需要通过 GLES 扩展提供，广泛用于移动设备
 */
constexpr bool isASTCCompression(TextureFormat format) noexcept {
    return format >= TextureFormat::RGBA_ASTC_4x4 && format <= TextureFormat::SRGB8_ALPHA8_ASTC_12x12;
}

//! Texture Cubemap Face
/**
 * 立方体贴图面枚举
 * 
 * 定义立方体贴图的六个面。
 * 
 * 注意：不要更改枚举值（与 OpenGL 匹配）
 * 
 * 立方体贴图坐标系统：
 * - 每个面是一个 2D 纹理
 * - 六个面构成一个完整的立方体环境贴图
 * - 用于环境映射、天空盒、反射等
 */
enum class TextureCubemapFace : uint8_t {
    // don't change the enums values
    POSITIVE_X = 0, //!< +x face
                    //!< +X 面（右面）
    NEGATIVE_X = 1, //!< -x face
                    //!< -X 面（左面）
    POSITIVE_Y = 2, //!< +y face
                    //!< +Y 面（上面）
    NEGATIVE_Y = 3, //!< -y face
                    //!< -Y 面（下面）
    POSITIVE_Z = 4, //!< +z face
                    //!< +Z 面（前面）
    NEGATIVE_Z = 5, //!< -z face
                    //!< -Z 面（后面）
};

//! Sampler Wrap mode
enum class SamplerWrapMode : uint8_t {
    CLAMP_TO_EDGE,      //!< clamp-to-edge. The edge of the texture extends to infinity.
    REPEAT,             //!< repeat. The texture infinitely repeats in the wrap direction.
    MIRRORED_REPEAT,    //!< mirrored-repeat. The texture infinitely repeats and mirrors in the wrap direction.
};

//! Sampler minification filter
enum class SamplerMinFilter : uint8_t {
    // don't change the enums values
    NEAREST = 0,                //!< No filtering. Nearest neighbor is used.
    LINEAR = 1,                 //!< Box filtering. Weighted average of 4 neighbors is used.
    NEAREST_MIPMAP_NEAREST = 2, //!< Mip-mapping is activated. But no filtering occurs.
    LINEAR_MIPMAP_NEAREST = 3,  //!< Box filtering within a mip-map level.
    NEAREST_MIPMAP_LINEAR = 4,  //!< Mip-map levels are interpolated, but no other filtering occurs.
    LINEAR_MIPMAP_LINEAR = 5    //!< Both interpolated Mip-mapping and linear filtering are used.
};

//! Sampler magnification filter
enum class SamplerMagFilter : uint8_t {
    // don't change the enums values
    NEAREST = 0,                //!< No filtering. Nearest neighbor is used.
    LINEAR = 1,                 //!< Box filtering. Weighted average of 4 neighbors is used.
};

//! Sampler compare mode
enum class SamplerCompareMode : uint8_t {
    // don't change the enums values
    NONE = 0,
    COMPARE_TO_TEXTURE = 1
};

//! comparison function for the depth / stencil sampler
enum class SamplerCompareFunc : uint8_t {
    // don't change the enums values
    LE = 0,     //!< Less or equal
    GE,         //!< Greater or equal
    L,          //!< Strictly less than
    G,          //!< Strictly greater than
    E,          //!< Equal
    NE,         //!< Not equal
    A,          //!< Always. Depth / stencil testing is deactivated.
    N           //!< Never. The depth / stencil test always fails.
};

//! Sampler parameters
struct SamplerParams {             // NOLINT
    SamplerMagFilter filterMag      : 1;    //!< magnification filter (NEAREST)
    SamplerMinFilter filterMin      : 3;    //!< minification filter  (NEAREST)
    SamplerWrapMode wrapS           : 2;    //!< s-coordinate wrap mode (CLAMP_TO_EDGE)
    SamplerWrapMode wrapT           : 2;    //!< t-coordinate wrap mode (CLAMP_TO_EDGE)

    SamplerWrapMode wrapR           : 2;    //!< r-coordinate wrap mode (CLAMP_TO_EDGE)
    uint8_t anisotropyLog2          : 3;    //!< anisotropy level (0)
    SamplerCompareMode compareMode  : 1;    //!< sampler compare mode (NONE)
    uint8_t padding0                : 2;    //!< reserved. must be 0.

    SamplerCompareFunc compareFunc  : 3;    //!< sampler comparison function (LE)
    uint8_t padding1                : 5;    //!< reserved. must be 0.
    uint8_t padding2                : 8;    //!< reserved. must be 0.

    struct Hasher {
        size_t operator()(SamplerParams p) const noexcept {
            // we don't use std::hash<> here, so we don't have to include <functional>
            return *reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&p));
        }
    };

    struct EqualTo {
        bool operator()(SamplerParams lhs, SamplerParams rhs) const noexcept {
            assert_invariant(lhs.padding0 == 0);
            assert_invariant(lhs.padding1 == 0);
            assert_invariant(lhs.padding2 == 0);
            auto* pLhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&lhs));
            auto* pRhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&rhs));
            return *pLhs == *pRhs;
        }
    };

    struct LessThan {
        bool operator()(SamplerParams lhs, SamplerParams rhs) const noexcept {
            assert_invariant(lhs.padding0 == 0);
            assert_invariant(lhs.padding1 == 0);
            assert_invariant(lhs.padding2 == 0);
            auto* pLhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&lhs));
            auto* pRhs = reinterpret_cast<uint32_t const*>(reinterpret_cast<char const*>(&rhs));
            return *pLhs < *pRhs;
        }
    };

    bool isFiltered() const noexcept {
        return filterMag != SamplerMagFilter::NEAREST || filterMin != SamplerMinFilter::NEAREST;
    }

private:
    friend bool operator == (SamplerParams lhs, SamplerParams rhs) noexcept {
        return EqualTo{}(lhs, rhs);
    }
    friend bool operator != (SamplerParams lhs, SamplerParams rhs) noexcept {
        return  !EqualTo{}(lhs, rhs);
    }
    friend bool operator < (SamplerParams lhs, SamplerParams rhs) noexcept {
        return LessThan{}(lhs, rhs);
    }
};

static_assert(sizeof(SamplerParams) == 4);

// The limitation to 64-bits max comes from how we store a SamplerParams in our JNI code
// see android/.../TextureSampler.cpp
static_assert(sizeof(SamplerParams) <= sizeof(uint64_t),
        "SamplerParams must be no more than 64 bits");

struct DescriptorSetLayout {
    std::variant<utils::StaticString, utils::CString, std::monostate> label;
    utils::FixedCapacityVector<DescriptorSetLayoutBinding> bindings;
};

//! blending equation function
enum class BlendEquation : uint8_t {
    ADD,                    //!< the fragment is added to the color buffer
    SUBTRACT,               //!< the fragment is subtracted from the color buffer
    REVERSE_SUBTRACT,       //!< the color buffer is subtracted from the fragment
    MIN,                    //!< the min between the fragment and color buffer
    MAX                     //!< the max between the fragment and color buffer
};

//! blending function
enum class BlendFunction : uint8_t {
    ZERO,                   //!< f(src, dst) = 0
    ONE,                    //!< f(src, dst) = 1
    SRC_COLOR,              //!< f(src, dst) = src
    ONE_MINUS_SRC_COLOR,    //!< f(src, dst) = 1-src
    DST_COLOR,              //!< f(src, dst) = dst
    ONE_MINUS_DST_COLOR,    //!< f(src, dst) = 1-dst
    SRC_ALPHA,              //!< f(src, dst) = src.a
    ONE_MINUS_SRC_ALPHA,    //!< f(src, dst) = 1-src.a
    DST_ALPHA,              //!< f(src, dst) = dst.a
    ONE_MINUS_DST_ALPHA,    //!< f(src, dst) = 1-dst.a
    SRC_ALPHA_SATURATE      //!< f(src, dst) = (1,1,1) * min(src.a, 1 - dst.a), 1
};

//! stencil operation
enum class StencilOperation : uint8_t {
    KEEP,                   //!< Keeps the current value.
    ZERO,                   //!< Sets the value to 0.
    REPLACE,                //!< Sets the value to the stencil reference value.
    INCR,                   //!< Increments the current value. Clamps to the maximum representable unsigned value.
    INCR_WRAP,              //!< Increments the current value. Wraps value to zero when incrementing the maximum representable unsigned value.
    DECR,                   //!< Decrements the current value. Clamps to 0.
    DECR_WRAP,              //!< Decrements the current value. Wraps value to the maximum representable unsigned value when decrementing a value of zero.
    INVERT,                 //!< Bitwise inverts the current value.
};

//! stencil faces
enum class StencilFace : uint8_t {
    FRONT               = 0x1,              //!< Update stencil state for front-facing polygons.
    BACK                = 0x2,              //!< Update stencil state for back-facing polygons.
    FRONT_AND_BACK      = FRONT | BACK,     //!< Update stencil state for all polygons.
};

//! Stream for external textures
enum class StreamType {
    NATIVE,     //!< Not synchronized but copy-free. Good for video.
    ACQUIRED,   //!< Synchronized, copy-free, and take a release callback. Good for AR but requires API 26+.
};

//! Releases an ACQUIRED external texture, guaranteed to be called on the application thread.
using StreamCallback = void(*)(void* image, void* user);

//! Vertex attribute descriptor
struct Attribute {
    //! attribute is normalized (remapped between 0 and 1)
    static constexpr uint8_t FLAG_NORMALIZED     = 0x1;
    //! attribute is an integer
    static constexpr uint8_t FLAG_INTEGER_TARGET = 0x2;
    static constexpr uint8_t BUFFER_UNUSED = 0xFF;
    uint32_t offset = 0;                    //!< attribute offset in bytes
    uint8_t stride = 0;                     //!< attribute stride in bytes
    uint8_t buffer = BUFFER_UNUSED;         //!< attribute buffer index
    ElementType type = ElementType::BYTE;   //!< attribute element type
    uint8_t flags = 0x0;                    //!< attribute flags
};

using AttributeArray = std::array<Attribute, MAX_VERTEX_ATTRIBUTE_COUNT>;

//! Raster state descriptor
struct RasterState {

    using CullingMode = backend::CullingMode;
    using DepthFunc = backend::SamplerCompareFunc;
    using BlendEquation = backend::BlendEquation;
    using BlendFunction = backend::BlendFunction;

    RasterState() noexcept { // NOLINT
        static_assert(sizeof(RasterState) == sizeof(uint32_t),
                "RasterState size not what was intended");
        culling = CullingMode::BACK;
        blendEquationRGB = BlendEquation::ADD;
        blendEquationAlpha = BlendEquation::ADD;
        blendFunctionSrcRGB = BlendFunction::ONE;
        blendFunctionSrcAlpha = BlendFunction::ONE;
        blendFunctionDstRGB = BlendFunction::ZERO;
        blendFunctionDstAlpha = BlendFunction::ZERO;
    }

    bool operator == (RasterState rhs) const noexcept { return u == rhs.u; }
    bool operator != (RasterState rhs) const noexcept { return u != rhs.u; }

    void disableBlending() noexcept {
        blendEquationRGB = BlendEquation::ADD;
        blendEquationAlpha = BlendEquation::ADD;
        blendFunctionSrcRGB = BlendFunction::ONE;
        blendFunctionSrcAlpha = BlendFunction::ONE;
        blendFunctionDstRGB = BlendFunction::ZERO;
        blendFunctionDstAlpha = BlendFunction::ZERO;
    }

    // note: clang reduces this entire function to a simple load/mask/compare
    bool hasBlending() const noexcept {
        // This is used to decide if blending needs to be enabled in the h/w
        return !(blendEquationRGB == BlendEquation::ADD &&
                 blendEquationAlpha == BlendEquation::ADD &&
                 blendFunctionSrcRGB == BlendFunction::ONE &&
                 blendFunctionSrcAlpha == BlendFunction::ONE &&
                 blendFunctionDstRGB == BlendFunction::ZERO &&
                 blendFunctionDstAlpha == BlendFunction::ZERO);
    }

    union {
        struct {
            //! culling mode
            CullingMode culling                         : 2;        //  2

            //! blend equation for the red, green and blue components
            BlendEquation blendEquationRGB              : 3;        //  5
            //! blend equation for the alpha component
            BlendEquation blendEquationAlpha            : 3;        //  8

            //! blending function for the source color
            BlendFunction blendFunctionSrcRGB           : 4;        // 12
            //! blending function for the source alpha
            BlendFunction blendFunctionSrcAlpha         : 4;        // 16
            //! blending function for the destination color
            BlendFunction blendFunctionDstRGB           : 4;        // 20
            //! blending function for the destination alpha
            BlendFunction blendFunctionDstAlpha         : 4;        // 24

            //! Whether depth-buffer writes are enabled
            bool depthWrite                             : 1;        // 25
            //! Depth test function
            DepthFunc depthFunc                         : 3;        // 28

            //! Whether color-buffer writes are enabled
            bool colorWrite                             : 1;        // 29

            //! use alpha-channel as coverage mask for anti-aliasing
            bool alphaToCoverage                        : 1;        // 30

            //! whether front face winding direction must be inverted
            bool inverseFrontFaces                      : 1;        // 31

            //! padding, must be 0
            bool depthClamp                             : 1;        // 32
        };
        uint32_t u = 0;
    };
};

/**
 **********************************************************************************************
 * \privatesection
 */

/**
 * Selects which buffers to clear at the beginning of the render pass, as well as which buffers
 * can be discarded at the beginning and end of the render pass.
 *
 */
struct RenderPassFlags {
    /**
     * bitmask indicating which buffers to clear at the beginning of a render pass.
     * This implies discard.
     */
    TargetBufferFlags clear;

    /**
     * bitmask indicating which buffers to discard at the beginning of a render pass.
     * Discarded buffers have uninitialized content, they must be entirely drawn over or cleared.
     */
    TargetBufferFlags discardStart;

    /**
     * bitmask indicating which buffers to discard at the end of a render pass.
     * Discarded buffers' content becomes invalid, they must not be read from again.
     */
    TargetBufferFlags discardEnd;
};

/**
 * Parameters of a render pass.
 */
struct RenderPassParams {
    RenderPassFlags flags{};    //!< operations performed on the buffers for this pass

    Viewport viewport{};        //!< viewport for this pass
    DepthRange depthRange{};    //!< depth range for this pass

    //! Color to use to clear the COLOR buffer. RenderPassFlags::clear must be set.
    math::float4 clearColor = {};

    //! Depth value to clear the depth buffer with
    double clearDepth = 0.0;

    //! Stencil value to clear the stencil buffer with
    uint32_t clearStencil = 0;

    /**
     * The subpass mask specifies which color attachments are designated for read-back in the second
     * subpass. If this is zero, the render pass has only one subpass. The least significant bit
     * specifies that the first color attachment in the render target is a subpass input.
     *
     * For now only 2 subpasses are supported, so only the lower 8 bits are used, one for each color
     * attachment (see MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT).
     */
    uint16_t subpassMask = 0;

    /**
     * This mask makes a promise to the backend about read-only usage of the depth attachment (bit
     * 0) and the stencil attachment (bit 1). Some backends need to know if writes are disabled in
     * order to allow sampling from the depth attachment.
     */
    uint16_t readOnlyDepthStencil = 0;

    static constexpr uint16_t READONLY_DEPTH = 1 << 0;
    static constexpr uint16_t READONLY_STENCIL = 1 << 1;
};

struct PolygonOffset {
    float slope = 0;        // factor in GL-speak
    float constant = 0;     // units in GL-speak
};

struct StencilState {
    using StencilFunction = SamplerCompareFunc;

    struct StencilOperations {
        //! Stencil test function
        StencilFunction stencilFunc                     : 3;                    // 3

        //! Stencil operation when stencil test fails
        StencilOperation stencilOpStencilFail           : 3;                    // 6

        uint8_t padding0                                : 2;                    // 8

        //! Stencil operation when stencil test passes but depth test fails
        StencilOperation stencilOpDepthFail             : 3;                    // 11

        //! Stencil operation when both stencil and depth test pass
        StencilOperation stencilOpDepthStencilPass      : 3;                    // 14

        uint8_t padding1                                : 2;                    // 16

        //! Reference value for stencil comparison tests and updates
        uint8_t ref;                                                            // 24

        //! Masks the bits of the stencil values participating in the stencil comparison test.
        uint8_t readMask;                                                       // 32

        //! Masks the bits of the stencil values updated by the stencil test.
        uint8_t writeMask;                                                      // 40
    };

    //! Stencil operations for front-facing polygons
    StencilOperations front = {
            .stencilFunc = StencilFunction::A,
            .stencilOpStencilFail = StencilOperation::KEEP,
            .padding0 = 0,
            .stencilOpDepthFail = StencilOperation::KEEP,
            .stencilOpDepthStencilPass = StencilOperation::KEEP,
            .padding1 = 0,
            .ref = 0,
            .readMask = 0xff,
            .writeMask = 0xff };

    //! Stencil operations for back-facing polygons
    StencilOperations back  = {
            .stencilFunc = StencilFunction::A,
            .stencilOpStencilFail = StencilOperation::KEEP,
            .padding0 = 0,
            .stencilOpDepthFail = StencilOperation::KEEP,
            .stencilOpDepthStencilPass = StencilOperation::KEEP,
            .padding1 = 0,
            .ref = 0,
            .readMask = 0xff,
            .writeMask = 0xff };

    //! Whether stencil-buffer writes are enabled
    bool stencilWrite = false;

    uint8_t padding = 0;
};

using PushConstantVariant = std::variant<int32_t, float, bool>;

static_assert(sizeof(StencilState::StencilOperations) == 5u,
        "StencilOperations size not what was intended");

static_assert(sizeof(StencilState) == 12u,
        "StencilState size not what was intended");

using FrameScheduledCallback = utils::Invocable<void(PresentCallable)>;

enum class Workaround : uint16_t {
    // The EASU pass must split because shader compiler flattens early-exit branch
    SPLIT_EASU,
    // Backend allows feedback loop with ancillary buffers (depth/stencil) as long as they're
    // read-only for the whole render pass.
    ALLOW_READ_ONLY_ANCILLARY_FEEDBACK_LOOP,
    // for some uniform arrays, it's needed to do an initialization to avoid crash on adreno gpu
    ADRENO_UNIFORM_ARRAY_CRASH,
    // Workaround a Metal pipeline compilation error with the message:
    // "Could not statically determine the target of a texture". See surface_light_indirect.fs
    METAL_STATIC_TEXTURE_TARGET_ERROR,
    // Adreno drivers sometimes aren't able to blit into a layer of a texture array.
    DISABLE_BLIT_INTO_TEXTURE_ARRAY,
    // Multiple workarounds needed for PowerVR GPUs
    POWER_VR_SHADER_WORKAROUNDS,
    // Some browsers, such as Firefox on Mac, struggle with slow shader compile/link times when
    // creating programs for the default material, leading to startup stutters. This workaround
    // prevents these stutters by not precaching depth variants of the default material for those
    // particular browsers.
    DISABLE_DEPTH_PRECACHE_FOR_DEFAULT_MATERIAL,
    // Emulate an sRGB swapchain in shader code.
    EMULATE_SRGB_SWAPCHAIN,
};

using StereoscopicType = Platform::StereoscopicType;

using FrameTimestamps = Platform::FrameTimestamps;

using CompositorTiming = Platform::CompositorTiming;

using AsynchronousMode = Platform::AsynchronousMode;

} // namespace filament::backend

template<> struct utils::EnableBitMaskOperators<filament::backend::ShaderStageFlags>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::TargetBufferFlags>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::DescriptorFlags>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::TextureUsage>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::StencilFace>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::BufferUsage>
        : public std::true_type {};
template<> struct utils::EnableBitMaskOperators<filament::backend::MapBufferAccessFlags>
        : public std::true_type {};

template<> struct utils::EnableIntegerOperators<filament::backend::TextureCubemapFace>
        : public std::true_type {};
template<> struct utils::EnableIntegerOperators<filament::backend::FeatureLevel>
        : public std::true_type {};

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::BufferUsage usage);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::CullingMode mode);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::ElementType type);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::PixelDataFormat format);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::PixelDataType type);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::Precision precision);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::PrimitiveType type);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::TargetBufferFlags f);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerCompareFunc func);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerCompareMode mode);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerFormat format);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerMagFilter filter);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerMinFilter filter);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerParams params);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerType type);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::SamplerWrapMode wrap);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::ShaderModel model);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::TextureCubemapFace face);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::TextureFormat format);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::TextureUsage usage);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::BufferObjectBinding binding);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::TextureSwizzle swizzle);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::ShaderStage shaderStage);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::ShaderStageFlags stageFlags);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::CompilerPriorityQueue compilerPriorityQueue);
utils::io::ostream& operator<<(utils::io::ostream& out, filament::backend::PushConstantVariant pushConstantVariant);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::AttributeArray& type);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::DescriptorSetLayout& dsl);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::PolygonOffset& po);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::RasterState& rs);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::RenderPassParams& b);
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::Viewport& v);
#endif

#endif // TNT_FILAMENT_BACKEND_DRIVERENUMS_H
