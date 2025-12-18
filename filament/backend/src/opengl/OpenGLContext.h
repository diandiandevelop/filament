/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGLCONTEXT_H
#define TNT_FILAMENT_BACKEND_OPENGLCONTEXT_H

// OpenGL 定时查询接口
#include "OpenGLTimerQuery.h"

// 平台接口
#include <backend/platforms/OpenGLPlatform.h>

// 后端枚举和句柄
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

// OpenGL 头文件
#include "gl_headers.h"

// Utils 工具库
#include <utils/compiler.h>          // 编译器工具
#include <utils/bitset.h>            // 位集合
#include <utils/debug.h>             // 调试工具

// 数学库
#include <math/vec2.h>               // 2D 向量
#include <math/vec4.h>               // 4D 向量

// 第三方库
#include <tsl/robin_map.h>           // Robin Hood 哈希映射

// 标准库
#include <array>                     // 数组
#include <functional>               // 函数对象
#include <optional>                  // 可选值
#include <tuple>                     // 元组
#include <vector>                    // 向量

#include <stddef.h>                  // 标准定义
#include <stdint.h>                  // 标准整数类型

namespace filament::backend {

class OpenGLPlatform;

/**
 * OpenGL 上下文类
 * 
 * 管理 OpenGL 状态缓存，减少冗余的 OpenGL API 调用。
 * 通过跟踪当前 OpenGL 状态，只在状态改变时才调用相应的 OpenGL 函数。
 * 
 * 主要功能：
 * 1. 状态缓存：缓存所有 OpenGL 状态，避免冗余调用
 * 2. 版本检测：检测 OpenGL/GLES 版本和扩展
 * 3. Bug 检测：检测并应用特定驱动的工作区
 * 4. 功能级别：根据硬件能力确定功能级别
 * 5. 定时查询：管理 GPU 定时查询
 * 
 * 设计原则：
 * - 所有状态改变都通过此类的方法进行，确保状态同步
 * - 使用 update_state 模板函数只在状态改变时调用 OpenGL API
 * - 支持多上下文（常规和保护上下文）
 */
class OpenGLContext final : public TimerQueryFactoryInterface {
public:
    static constexpr const size_t MAX_TEXTURE_UNIT_COUNT = MAX_SAMPLER_COUNT;  // 最大纹理单元数
    static constexpr const size_t DUMMY_TEXTURE_BINDING = 7;  // 虚拟纹理绑定（ES2 保证可用的最高绑定）
    static constexpr const size_t MAX_BUFFER_BINDINGS = 32;    // 最大缓冲区绑定数
    
    typedef math::details::TVec4<GLint> vec4gli;      // GLint 4D 向量类型（用于视口、裁剪等）
    typedef math::details::TVec2<GLclampf> vec2glf;   // GLclampf 2D 向量类型（用于深度范围等）

    /**
     * 渲染图元结构
     * 
     * 存储与渲染图元相关的 OpenGL 状态，包括 VAO、索引缓冲区等。
     * 支持多上下文（常规和保护上下文），每个上下文有独立的 VAO 名称。
     */
    struct RenderPrimitive {
        static_assert(MAX_VERTEX_ATTRIBUTE_COUNT <= 16);  // 确保顶点属性数量不超过 16

        GLuint vao[2] = {};                                     // VAO 对象 ID（每个上下文一个）：8 字节
        GLuint elementArray = 0;                                // 元素数组缓冲区 ID：4 字节
        GLenum indicesType = 0;                                 // 索引类型（GL_UNSIGNED_BYTE/SHORT/INT）：4 字节

        // 可选的 32 位句柄，指向 GLVertexBuffer。
        // 仅当引用的 VertexBuffer 支持缓冲区对象时需要。
        // 如果为零，则 VBO 句柄数组是不可变的。
        Handle<HwVertexBuffer> vertexBufferWithObjects;         // 4 字节

        mutable utils::bitset<uint16_t> vertexAttribArray;      // 启用的顶点属性数组位集合：2 字节

        uint8_t reserved[2] = {};                               // 保留字段：2 字节

        // 如果此值与 vertexBufferWithObjects->bufferObjectsVersion 不同，
        // 则此 VAO 需要更新（参见 OpenGLDriver::updateVertexArrayObject()）
        uint8_t vertexBufferVersion = 0;                        // 1 字节

        // 如果此值与 OpenGLContext::state.age 不同，
        // 则此 VAO 需要更新（参见 OpenGLDriver::updateVertexArrayObject()）
        uint8_t stateVersion = 0;                               // 1 字节

        // 如果此值与 OpenGLContext::state.age 不同，
        // 则此 VAO 的名称需要更新。
        // 参见 OpenGLContext::bindVertexArray()
        uint8_t nameVersion = 0;                                // 1 字节

        // 索引缓冲区中索引的大小（字节）（1 或 2）
        uint8_t indicesShift = 0;                                // 1 字节

        /**
         * 获取索引类型
         * 
         * @return 索引类型枚举值
         */
        GLenum getIndicesType() const noexcept {
            return indicesType;
        }
    } gl;

    /**
     * 查询 OpenGL 版本
     * 
     * 查询当前 OpenGL/GLES 上下文的主版本号和次版本号。
     * 
     * @param major 输出参数：主版本号
     * @param minor 输出参数：次版本号
     * @return 成功返回 true，失败返回 false
     */
    static bool queryOpenGLVersion(GLint* major, GLint* minor) noexcept;

    /**
     * 构造函数
     * 
     * 初始化 OpenGL 上下文，检测版本、扩展、功能和 Bug。
     * 
     * @param platform OpenGL 平台引用
     * @param driverConfig 驱动配置参数
     */
    explicit OpenGLContext(OpenGLPlatform& platform,
            Platform::DriverConfig const& driverConfig) noexcept;

    /**
     * 析构函数
     * 
     * 清理 OpenGL 上下文资源。
     */
    ~OpenGLContext() noexcept final;

    /**
     * 终止上下文
     * 
     * 清理上下文相关的资源（如采样器对象）。
     */
    void terminate() noexcept;

    // --------------------------------------------------------------------------------------------
    // TimerQueryInterface 定时查询接口

    // 注意：OpenGLContext 是 final 类，确保（clang）这些方法不会通过虚函数表调用

    /**
     * 创建定时查询
     * 
     * @param query 定时查询对象指针
     */
    void createTimerQuery(GLTimerQuery* query) override;

    /**
     * 销毁定时查询
     * 
     * @param query 定时查询对象指针
     */
    void destroyTimerQuery(GLTimerQuery* query) override;

    /**
     * 开始时间流逝查询
     * 
     * @param query 定时查询对象指针
     */
    void beginTimeElapsedQuery(GLTimerQuery* query) override;

    /**
     * 结束时间流逝查询
     * 
     * @param driver OpenGLDriver 引用
     * @param query 定时查询对象指针
     */
    void endTimeElapsedQuery(OpenGLDriver& driver, GLTimerQuery* query) override;

    // --------------------------------------------------------------------------------------------

    /**
     * 检查是否为至少指定版本的 OpenGL
     * 
     * @tparam MAJOR 主版本号
     * @tparam MINOR 次版本号
     * @return 如果是至少指定版本的 OpenGL 返回 true，否则返回 false
     */
    template<int MAJOR, int MINOR>
    inline bool isAtLeastGL() const noexcept {
#ifdef BACKEND_OPENGL_VERSION_GL
        return state.major > MAJOR || (state.major == MAJOR && state.minor >= MINOR);
#else
        return false;
#endif
    }

    /**
     * 检查是否为至少指定版本的 OpenGL ES
     * 
     * @tparam MAJOR 主版本号
     * @tparam MINOR 次版本号
     * @return 如果是至少指定版本的 OpenGL ES 返回 true，否则返回 false
     */
    template<int MAJOR, int MINOR>
    inline bool isAtLeastGLES() const noexcept {
#ifdef BACKEND_OPENGL_VERSION_GLES
        return state.major > MAJOR || (state.major == MAJOR && state.minor >= MINOR);
#else
        return false;
#endif
    }

    /**
     * 检查是否为 ES2
     * 
     * @return 如果是 ES2 返回 true，否则返回 false
     */
    inline bool isES2() const noexcept {
#if defined(BACKEND_OPENGL_VERSION_GLES) && !defined(FILAMENT_IOS)
#   ifndef BACKEND_OPENGL_LEVEL_GLES30
            return true;
#   else
            return mFeatureLevel == FeatureLevel::FEATURE_LEVEL_0;
#   endif
#else
        return false;
#endif
    }

    bool hasFences() const noexcept {
    #if defined(BACKEND_OPENGL_VERSION_GLES) && !defined(FILAMENT_IOS) && !defined(__EMSCRIPTEN__)
    #   ifndef BACKEND_OPENGL_LEVEL_GLES30
            return false;
    #   else
            return mFeatureLevel > FeatureLevel::FEATURE_LEVEL_0;
    #   endif
    #else
            return true;
    #endif
    }

    /**
     * 获取功能标志的索引
     * 
     * @param cap OpenGL 功能标志（如 GL_BLEND、GL_DEPTH_TEST 等）
     * @return 功能标志在状态数组中的索引
     */
    constexpr        inline size_t getIndexForCap(GLenum cap) noexcept;

    /**
     * 获取缓冲区目标的索引
     * 
     * @param target 缓冲区目标（如 GL_ARRAY_BUFFER、GL_UNIFORM_BUFFER 等）
     * @return 缓冲区目标在状态数组中的索引
     */
    constexpr static inline size_t getIndexForBufferTarget(GLenum target) noexcept;

    /**
     * 获取着色器模型
     * 
     * @return 着色器模型（MOBILE 或 DESKTOP）
     */
    ShaderModel getShaderModel() const noexcept { return mShaderModel; }

    /**
     * 重置状态
     * 
     * 强制 OpenGL 状态与 Filament 状态匹配。
     * 增加状态版本号，使其他部分知道需要重置。
     */
    void resetState() noexcept;

    /**
     * 使用着色器程序
     * 
     * @param program 着色器程序对象 ID
     */
    inline void useProgram(GLuint program) noexcept;

    /**
     * 设置像素存储参数
     * 
     * @param pname 参数名（如 GL_PACK_ALIGNMENT、GL_UNPACK_ALIGNMENT 等）
     * @param param 参数值
     */
          void pixelStore(GLenum, GLint) noexcept;

    /**
     * 激活纹理单元
     * 
     * @param unit 纹理单元索引（从 0 开始）
     */
    inline void activeTexture(GLuint unit) noexcept;

    /**
     * 绑定纹理
     * 
     * @param unit 纹理单元索引
     * @param target 纹理目标（GL_TEXTURE_2D、GL_TEXTURE_CUBE_MAP 等）
     * @param texId 纹理对象 ID
     * @param external 是否为外部纹理（强制更新）
     */
    inline void bindTexture(GLuint unit, GLuint target, GLuint texId, bool external) noexcept;

    /**
     * 解绑纹理
     * 
     * @param target 纹理目标
     * @param id 纹理对象 ID
     */
           void unbindTexture(GLenum target, GLuint id) noexcept;

    /**
     * 解绑纹理单元
     * 
     * @param unit 纹理单元索引
     */
           void unbindTextureUnit(GLuint unit) noexcept;

    /**
     * 绑定顶点数组对象
     * 
     * @param p 渲染图元指针（nullptr 表示使用默认 VAO）
     */
    inline void bindVertexArray(RenderPrimitive const* p) noexcept;

    /**
     * 绑定采样器对象
     * 
     * @param unit 纹理单元索引
     * @param sampler 采样器对象 ID
     */
    inline void bindSampler(GLuint unit, GLuint sampler) noexcept;

    /**
     * 解绑采样器对象
     * 
     * @param sampler 采样器对象 ID
     */
           void unbindSampler(GLuint sampler) noexcept;

    /**
     * 绑定缓冲区
     * 
     * @param target 缓冲区目标（GL_ARRAY_BUFFER、GL_ELEMENT_ARRAY_BUFFER 等）
     * @param buffer 缓冲区对象 ID
     */
           void bindBuffer(GLenum target, GLuint buffer) noexcept;

    /**
     * 绑定缓冲区范围
     * 
     * @param target 缓冲区目标（GL_UNIFORM_BUFFER、GL_TRANSFORM_FEEDBACK_BUFFER 等）
     * @param index 绑定点索引
     * @param buffer 缓冲区对象 ID
     * @param offset 偏移量（字节）
     * @param size 大小（字节）
     */
    inline void bindBufferRange(GLenum target, GLuint index, GLuint buffer,
            GLintptr offset, GLsizeiptr size) noexcept;

    /**
     * 绑定帧缓冲区
     * 
     * @param target 帧缓冲区目标（GL_FRAMEBUFFER、GL_DRAW_FRAMEBUFFER、GL_READ_FRAMEBUFFER）
     * @param buffer 帧缓冲区对象 ID（0 表示默认 FBO）
     * @return 解析后的帧缓冲区 ID
     */
    GLuint bindFramebuffer(GLenum target, GLuint buffer) noexcept;

    /**
     * 解绑帧缓冲区
     * 
     * @param target 帧缓冲区目标
     */
    void unbindFramebuffer(GLenum target) noexcept;

    /**
     * 启用顶点属性数组
     * 
     * @param rp 渲染图元指针
     * @param index 顶点属性索引
     */
    inline void enableVertexAttribArray(RenderPrimitive const* rp, GLuint index) noexcept;

    /**
     * 禁用顶点属性数组
     * 
     * @param rp 渲染图元指针
     * @param index 顶点属性索引
     */
    inline void disableVertexAttribArray(RenderPrimitive const* rp, GLuint index) noexcept;

    /**
     * 启用功能
     * 
     * @param cap 功能标志（GL_BLEND、GL_DEPTH_TEST 等）
     */
    inline void enable(GLenum cap) noexcept;

    /**
     * 禁用功能
     * 
     * @param cap 功能标志
     */
    inline void disable(GLenum cap) noexcept;

    /**
     * 设置正面方向
     * 
     * @param mode 正面方向（GL_CW、GL_CCW）
     */
    inline void frontFace(GLenum mode) noexcept;

    /**
     * 设置剔除面
     * 
     * @param mode 剔除面（GL_FRONT、GL_BACK、GL_FRONT_AND_BACK）
     */
    inline void cullFace(GLenum mode) noexcept;

    /**
     * 设置混合方程
     * 
     * @param modeRGB RGB 混合方程
     * @param modeA Alpha 混合方程
     */
    inline void blendEquation(GLenum modeRGB, GLenum modeA) noexcept;

    /**
     * 设置混合函数
     * 
     * @param srcRGB RGB 源因子
     * @param srcA Alpha 源因子
     * @param dstRGB RGB 目标因子
     * @param dstA Alpha 目标因子
     */
    inline void blendFunction(GLenum srcRGB, GLenum srcA, GLenum dstRGB, GLenum dstA) noexcept;

    /**
     * 设置颜色写入掩码
     * 
     * @param flag 是否允许写入（GL_TRUE/GL_FALSE）
     */
    inline void colorMask(GLboolean flag) noexcept;

    /**
     * 设置深度写入掩码
     * 
     * @param flag 是否允许写入（GL_TRUE/GL_FALSE）
     */
    inline void depthMask(GLboolean flag) noexcept;

    /**
     * 设置深度测试函数
     * 
     * @param func 深度测试函数（GL_LESS、GL_LEQUAL 等）
     */
    inline void depthFunc(GLenum func) noexcept;

    /**
     * 设置模板测试函数（分离前后）
     * 
     * @param funcFront 正面模板测试函数
     * @param refFront 正面参考值
     * @param maskFront 正面掩码
     * @param funcBack 背面模板测试函数
     * @param refBack 背面参考值
     * @param maskBack 背面掩码
     */
    inline void stencilFuncSeparate(GLenum funcFront, GLint refFront, GLuint maskFront,
            GLenum funcBack, GLint refBack, GLuint maskBack) noexcept;

    /**
     * 设置模板操作（分离前后）
     * 
     * @param sfailFront 正面模板测试失败操作
     * @param dpfailFront 正面深度测试失败操作
     * @param dppassFront 正面深度测试通过操作
     * @param sfailBack 背面模板测试失败操作
     * @param dpfailBack 背面深度测试失败操作
     * @param dppassBack 背面深度测试通过操作
     */
    inline void stencilOpSeparate(GLenum sfailFront, GLenum dpfailFront, GLenum dppassFront,
            GLenum sfailBack, GLenum dpfailBack, GLenum dppassBack) noexcept;

    /**
     * 设置模板写入掩码（分离前后）
     * 
     * @param maskFront 正面掩码
     * @param maskBack 背面掩码
     */
    inline void stencilMaskSeparate(GLuint maskFront, GLuint maskBack) noexcept;

    /**
     * 设置多边形偏移
     * 
     * @param factor 偏移因子
     * @param units 偏移单位
     */
    inline void polygonOffset(GLfloat factor, GLfloat units) noexcept;

    /**
     * 设置裁剪矩形
     * 
     * @param left 左边界
     * @param bottom 底边界
     * @param width 宽度
     * @param height 高度
     */
    inline void setScissor(GLint left, GLint bottom, GLsizei width, GLsizei height) noexcept;

    /**
     * 设置视口
     * 
     * @param left 左边界
     * @param bottom 底边界
     * @param width 宽度
     * @param height 高度
     */
    inline void viewport(GLint left, GLint bottom, GLsizei width, GLsizei height) noexcept;

    /**
     * 设置深度范围
     * 
     * @param near 近平面深度值
     * @param far 远平面深度值
     */
    inline void depthRange(GLclampf near, GLclampf far) noexcept;

    /**
     * 删除缓冲区
     * 
     * @param buffer 缓冲区对象 ID
     * @param target 缓冲区目标
     */
    void deleteBuffer(GLuint buffer, GLenum target) noexcept;

    /**
     * 删除顶点数组对象
     * 
     * @param vao VAO 对象 ID
     */
    void deleteVertexArray(GLuint vao) noexcept;

    /**
     * 在指定上下文中销毁对象
     * 
     * 延迟对象的销毁，直到切换到指定上下文。
     * 
     * @param index 上下文索引（0=常规，1=保护）
     * @param closure 销毁闭包
     */
    void destroyWithContext(size_t index, std::function<void(OpenGLContext&)> const& closure);

    /**
     * OpenGL 查询值结构
     * 
     * 存储通过 glGet*() 查询得到的 OpenGL 限制值。
     */
    struct Gets {
        GLfloat max_anisotropy;                          // 最大各向异性过滤级别
        GLint max_combined_texture_image_units;          // 最大组合纹理图像单元数
        GLint max_draw_buffers;                          // 最大绘制缓冲区数
        GLint max_renderbuffer_size;                     // 最大渲染缓冲区大小
        GLint max_samples;                                // 最大采样数
        GLint max_texture_image_units;                   // 最大纹理图像单元数（片段着色器）
        GLint max_texture_size;                          // 最大纹理大小
        GLint max_cubemap_texture_size;                  // 最大立方体贴图纹理大小
        GLint max_3d_texture_size;                       // 最大 3D 纹理大小
        GLint max_array_texture_layers;                  // 最大数组纹理层数
        GLint max_transform_feedback_separate_attribs;   // 最大变换反馈分离属性数
        GLint max_uniform_block_size;                    // 最大 Uniform 块大小
        GLint max_uniform_buffer_bindings;               // 最大 Uniform 缓冲区绑定数
        GLint num_program_binary_formats;                // 程序二进制格式数量
        GLint uniform_buffer_offset_alignment;           // Uniform 缓冲区偏移对齐
    } gets = {};

    /**
     * 功能支持结构
     * 
     * 存储此版本 GL 或 GLES 支持的功能。
     */
    struct {
        bool multisample_texture;  // 是否支持多重采样纹理
    } features = {};

    /**
     * 扩展支持结构
     * 
     * 存储运行时检测到的 OpenGL 扩展支持情况。
     */
    struct Extensions {
        bool APPLE_color_buffer_packed_float;
        bool ARB_shading_language_packing;
        bool EXT_clip_control;
        bool EXT_clip_cull_distance;
        bool EXT_color_buffer_float;
        bool EXT_color_buffer_half_float;
        bool EXT_debug_marker;
        bool EXT_depth_clamp;
        bool EXT_discard_framebuffer;
        bool EXT_disjoint_timer_query;
        bool EXT_multisampled_render_to_texture2;
        bool EXT_multisampled_render_to_texture;
        bool EXT_protected_textures;
        bool EXT_shader_framebuffer_fetch;
        bool EXT_texture_compression_bptc;
        bool EXT_texture_compression_etc2;
        bool EXT_texture_compression_rgtc;
        bool EXT_texture_compression_s3tc;
        bool EXT_texture_compression_s3tc_srgb;
        bool EXT_texture_cube_map_array;
        bool EXT_texture_filter_anisotropic;
        bool EXT_texture_sRGB;
        bool GOOGLE_cpp_style_line_directive;
        bool KHR_debug;
        bool KHR_parallel_shader_compile;
        bool KHR_texture_compression_astc_hdr;
        bool KHR_texture_compression_astc_ldr;
        bool OES_EGL_image_external_essl3;
        bool OES_depth24;
        bool OES_depth_texture;
        bool OES_packed_depth_stencil;
        bool OES_rgb8_rgba8;
        bool OES_standard_derivatives;
        bool OES_texture_npot;
        bool OES_vertex_array_object;
        bool OVR_multiview2;
        bool WEBGL_compressed_texture_etc;
        bool WEBGL_compressed_texture_s3tc;
        bool WEBGL_compressed_texture_s3tc_srgb;
    } ext = {};

    /**
     * Bug 工作区结构
     * 
     * 存储特定 GPU 驱动的已知 Bug 和工作区。
     * 这些标志用于在运行时应用相应的修复。
     */
    struct Bugs {
        // 某些驱动在绘制调用之间调用 glFlush() 时，
        // 片段着色器中的 UBO 会出现问题。
        bool disable_glFlush;

        // Some drivers seem to not store the GL_ELEMENT_ARRAY_BUFFER binding
        // in the VAO state.
        bool vao_doesnt_store_element_array_buffer_binding;

        // Some drivers have gl state issues when drawing from shared contexts
        bool disable_shared_context_draws;

        // Some web browsers seem to immediately clear the default framebuffer when calling
        // glInvalidateFramebuffer with WebGL 2.0
        bool disable_invalidate_framebuffer;

        // Some drivers declare GL_EXT_texture_filter_anisotropic but don't support
        // calling glSamplerParameter() with GL_TEXTURE_MAX_ANISOTROPY_EXT
        bool texture_filter_anisotropic_broken_on_sampler;

        // Some drivers have issues when reading from a mip while writing to a different mip.
        // In the OpenGL ES 3.0 specification this is covered in section 4.4.3,
        // "Feedback Loops Between Textures and the Framebuffer".
        bool disable_feedback_loops;

        // Some drivers don't implement timer queries correctly
        bool dont_use_timer_query;

        // Some drivers can't blit from a sidecar renderbuffer into a layer of a texture array.
        // This technique is used for VSM with MSAA turned on.
        bool disable_blit_into_texture_array;

        // Some drivers incorrectly flatten the early exit condition in the EASU code, in which
        // case we need an alternative algorithm
        bool split_easu;

        // As of Android R some qualcomm drivers invalidate buffers for the whole render pass
        // even if glInvalidateFramebuffer() is called at the end of it.
        bool invalidate_end_only_if_invalidate_start;

        // GLES doesn't allow feedback loops even if writes are disabled. So take we the point of
        // view that this is generally forbidden. However, this restriction is lifted on desktop
        // GL and Vulkan and probably Metal.
        bool allow_read_only_ancillary_feedback_loop;

        // Some Adreno drivers crash in glDrawXXX() when there's an uninitialized uniform block,
        // even when the shader doesn't access it.
        bool enable_initialize_non_used_uniform_array;

        // Workarounds specific to PowerVR GPUs affecting shaders (currently, we lump them all
        // under one specialization constant).
        // - gl_InstanceID is invalid when used first in the vertex shader
        bool powervr_shader_workarounds;

        // On PowerVR destroying the destination of a glBlitFramebuffer operation is equivalent to
        // a glFinish. So we must delay the destruction until we know the GPU is finished.
        bool delay_fbo_destruction;

        // Mesa and Mozilla(web) sometimes clear the generic buffer binding when *another* buffer
        // is destroyed, if that other buffer is bound on an *indexed* buffer binding.
        bool rebind_buffer_after_deletion;

        // Force feature level 0. Typically used for low end ES3 devices with significant driver
        // bugs or performance issues.
        bool force_feature_level0;

        // Some browsers, such as Firefox on Mac, struggle with slow shader compile/link times when
        // creating programs for the default material, leading to startup stutters. This workaround
        // prevents these stutters by not precaching depth variants of the default material for
        // those particular browsers.
        bool disable_depth_precache_for_default_material;

        // On llvmpipe (mesa), enabling framebuffer fetch causes a crash in draw2
        //   'OpenGL error 0x502 (GL_INVALID_OPERATION) in "draw2" at line 4389'
        // This coincides with the use of framebuffer fetch (ColorGradingAsSubpass). We disable
        // framebuffer fetch in the case of llvmpipe.
        // Some Mali drivers also have problems with this (b/445721121)
        bool disable_framebuffer_fetch_extension;

    } bugs = {};

    // state getters -- as needed.
    vec4gli const& getViewport() const { return state.window.viewport; }

    // function to handle state changes we don't control
    void updateTexImage(GLenum target, GLuint id) noexcept {
        assert_invariant(target == GL_TEXTURE_EXTERNAL_OES);
        // if another target is bound to this texture unit, unbind that texture
        if (UTILS_UNLIKELY(state.textures.units[state.textures.active].target != target)) {
            glBindTexture(state.textures.units[state.textures.active].target, 0);
            state.textures.units[state.textures.active].target = GL_TEXTURE_EXTERNAL_OES;
        }
        // the texture is already bound to `target`, we just update our internal state
        state.textures.units[state.textures.active].id = id;
    }
    void resetProgram() noexcept { state.program.use = 0; }

    FeatureLevel getFeatureLevel() const noexcept { return mFeatureLevel; }

    // This is the index of the context in use. Must be 0 or 1. This is used to manange the
    // OpenGL name of ContainerObjects within each context.
    uint32_t contextIndex = 0;

    /**
     * OpenGL 状态结构
     * 
     * 存储所有 OpenGL 状态的缓存。
     * 尝试按数据访问模式排序，以提高缓存性能。
     * 
     * 注意：此结构不可复制或移动，以防止意外状态复制。
     */
    struct State {
        State() noexcept = default;
        // 确保不会意外复制此状态
        State(State const& rhs) = delete;
        State(State&& rhs) noexcept = delete;
        State& operator=(State const& rhs) = delete;
        State& operator=(State&& rhs) noexcept = delete;

        GLint major = 0;  // OpenGL 主版本号
        GLint minor = 0;  // OpenGL 次版本号

        char const* vendor = nullptr;    // GL_VENDOR 字符串
        char const* renderer = nullptr;  // GL_RENDERER 字符串
        char const* version = nullptr;   // GL_VERSION 字符串
        char const* shader = nullptr;     // GL_SHADING_LANGUAGE_VERSION 字符串

        GLuint draw_fbo = 0;   // 绘制帧缓冲区对象 ID
        GLuint read_fbo = 0;   // 读取帧缓冲区对象 ID

        /**
         * 着色器程序状态
         */
        struct {
            GLuint use = 0;  // 当前使用的着色器程序对象 ID
        } program;

        /**
         * 顶点数组对象状态
         */
        struct {
            RenderPrimitive* p = nullptr;  // 当前绑定的渲染图元指针
        } vao;

        /**
         * 光栅化状态
         */
        struct {
            GLenum frontFace            = GL_CCW;      // 正面方向
            GLenum cullFace             = GL_BACK;     // 剔除面
            GLenum blendEquationRGB     = GL_FUNC_ADD; // RGB 混合方程
            GLenum blendEquationA       = GL_FUNC_ADD; // Alpha 混合方程
            GLenum blendFunctionSrcRGB  = GL_ONE;      // RGB 源混合因子
            GLenum blendFunctionSrcA    = GL_ONE;      // Alpha 源混合因子
            GLenum blendFunctionDstRGB  = GL_ZERO;     // RGB 目标混合因子
            GLenum blendFunctionDstA    = GL_ZERO;     // Alpha 目标混合因子
            GLboolean colorMask         = GL_TRUE;     // 颜色写入掩码
            GLboolean depthMask         = GL_TRUE;     // 深度写入掩码
            GLenum depthFunc            = GL_LESS;     // 深度测试函数
        } raster;

        /**
         * 模板测试状态
         */
        struct {
            /**
             * 模板测试函数结构
             */
            struct StencilFunc {
                GLenum func             = GL_ALWAYS;  // 模板测试函数
                GLint ref               = 0;          // 参考值
                GLuint mask             = ~GLuint(0); // 掩码
                bool operator != (StencilFunc const& rhs) const noexcept {
                    return func != rhs.func || ref != rhs.ref || mask != rhs.mask;
                }
            };
            /**
             * 模板操作结构
             */
            struct StencilOp {
                GLenum sfail            = GL_KEEP;  // 模板测试失败操作
                GLenum dpfail           = GL_KEEP;  // 深度测试失败操作
                GLenum dppass           = GL_KEEP;  // 深度测试通过操作
                bool operator != (StencilOp const& rhs) const noexcept {
                    return sfail != rhs.sfail || dpfail != rhs.dpfail || dppass != rhs.dppass;
                }
            };
            /**
             * 模板状态（前后两面）
             */
            struct {
                StencilFunc func;       // 模板测试函数
                StencilOp op;           // 模板操作
                GLuint stencilMask      = ~GLuint(0);  // 模板写入掩码
            } front, back;  // 正面和背面
        } stencil;

        /**
         * 多边形偏移状态
         */
        struct PolygonOffset {
            GLfloat factor = 0;  // 偏移因子
            GLfloat units = 0;   // 偏移单位
            bool operator != (PolygonOffset const& rhs) const noexcept {
                return factor != rhs.factor || units != rhs.units;
            }
        } polygonOffset;

        /**
         * 功能启用状态
         */
        struct {
            utils::bitset32 caps;  // 功能标志位集合
        } enables;

        /**
         * 缓冲区绑定状态
         */
        struct {
            /**
             * 索引缓冲区目标（每个目标一个）
             */
            struct {
                /**
                 * 缓冲区绑定信息
                 */
                struct {
                    GLuint name = 0;        // 缓冲区对象 ID
                    GLintptr offset = 0;    // 偏移量（字节）
                    GLsizeiptr size = 0;    // 大小（字节）
                } buffers[MAX_BUFFER_BINDINGS];
            } targets[3];   // 只有 3 个索引缓冲区目标（UBO、TFB、SSBO）
            GLuint genericBinding[7] = {};  // 通用缓冲区绑定（非索引）
        } buffers;

        /**
         * 纹理状态
         */
        struct {
            GLuint active = 0;      // 当前活动的纹理单元（从 0 开始）
            /**
             * 纹理单元状态
             */
            struct {
                GLuint sampler = 0;  // 采样器对象 ID
                GLuint target = 0;    // 纹理目标
                GLuint id = 0;        // 纹理对象 ID
            } units[MAX_TEXTURE_UNIT_COUNT];
        } textures;

        /**
         * 解包状态（用于 glTexImage*）
         */
        struct {
            GLint row_length = 0;   // 行长度
            GLint alignment = 4;     // 对齐方式
        } unpack;

        /**
         * 打包状态（用于 glReadPixels）
         */
        struct {
            GLint alignment = 4;     // 对齐方式
        } pack;

        /**
         * 窗口状态
         */
        struct {
            vec4gli scissor { 0 };      // 裁剪矩形 (x, y, width, height)
            vec4gli viewport { 0 };     // 视口 (x, y, width, height)
            vec2glf depthRange { 0.0f, 1.0f };  // 深度范围 (near, far)
        } window;
        uint8_t age = 0;  // 状态版本号（用于检测状态失效）
    } state;

    /**
     * OpenGL 函数指针结构
     * 
     * 存储可能通过扩展提供的 OpenGL 函数指针。
     * 允许在运行时选择正确的函数（核心或扩展版本）。
     */
    struct Procs {
        void (* bindVertexArray)(GLuint array);                    // 绑定顶点数组对象
        void (* deleteVertexArrays)(GLsizei n, const GLuint* arrays);  // 删除顶点数组对象
        void (* genVertexArrays)(GLsizei n, GLuint* arrays);       // 生成顶点数组对象

        void (* genQueries)(GLsizei n, GLuint* ids);               // 生成查询对象
        void (* deleteQueries)(GLsizei n, const GLuint* ids);      // 删除查询对象
        void (* beginQuery)(GLenum target, GLuint id);             // 开始查询
        void (* endQuery)(GLenum target);                          // 结束查询
        void (* getQueryObjectuiv)(GLuint id, GLenum pname, GLuint* params);  // 获取查询对象（uint）
        void (* getQueryObjectui64v)(GLuint id, GLenum pname, GLuint64* params);  // 获取查询对象（uint64）

        void (* invalidateFramebuffer)(GLenum target, GLsizei numAttachments, const GLenum *attachments);  // 使帧缓冲区无效

        void (* maxShaderCompilerThreadsKHR)(GLuint count);      // 设置最大着色器编译线程数
    } procs{};

    /**
     * 解绑所有对象
     * 
     * 解绑所有绑定的对象，以便资源不会在销毁时卡在此上下文中。
     */
    void unbindEverything() noexcept;

    /**
     * 同步状态和缓存
     * 
     * 在切换到新上下文时同步状态和缓存。
     * 
     * @param index 上下文索引（0=常规，1=保护）
     */
    void synchronizeStateAndCache(size_t index);

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    /**
     * 获取采样器对象（慢路径）
     * 
     * 如果采样器不存在，创建新的采样器对象。
     * 
     * @param sp 采样器参数
     * @return 采样器对象 ID
     */
    GLuint getSamplerSlow(SamplerParams sp) const noexcept;

    /**
     * 获取采样器对象（快速路径）
     * 
     * 从缓存中查找采样器，如果不存在则调用慢路径创建。
     * 
     * @param sp 采样器参数
     * @return 采样器对象 ID
     */
    inline GLuint getSampler(SamplerParams sp) const noexcept {
        assert_invariant(!sp.padding0);
        assert_invariant(!sp.padding1);
        assert_invariant(!sp.padding2);
        auto& samplerMap = mSamplerMap;
        auto pos = samplerMap.find(sp);
        if (UTILS_UNLIKELY(pos == samplerMap.end())) {
            return getSamplerSlow(sp);
        }
        return pos->second;
    }
#endif


private:
    OpenGLPlatform& mPlatform;
    ShaderModel mShaderModel = ShaderModel::MOBILE;
    FeatureLevel mFeatureLevel = FeatureLevel::FEATURE_LEVEL_1;
    TimerQueryFactoryInterface* mTimerQueryFactory = nullptr;
    std::vector<std::function<void(OpenGLContext&)>> mDestroyWithNormalContext;
    RenderPrimitive mDefaultVAO;
    std::optional<GLuint> mDefaultFbo[2];
    mutable tsl::robin_map<SamplerParams, GLuint,
            SamplerParams::Hasher, SamplerParams::EqualTo> mSamplerMap;

    Platform::DriverConfig const mDriverConfig;

    void bindFramebufferResolved(GLenum target, GLuint buffer) noexcept;

    const std::array<std::tuple<bool const&, char const*, char const*>, sizeof(bugs)> mBugDatabase{{
            {   bugs.disable_glFlush,
                    "disable_glFlush",
                    ""},
            {   bugs.vao_doesnt_store_element_array_buffer_binding,
                    "vao_doesnt_store_element_array_buffer_binding",
                    ""},
            {   bugs.disable_shared_context_draws,
                    "disable_shared_context_draws",
                    ""},
            {   bugs.disable_invalidate_framebuffer,
                    "disable_invalidate_framebuffer",
                    ""},
            {   bugs.texture_filter_anisotropic_broken_on_sampler,
                    "texture_filter_anisotropic_broken_on_sampler",
                    ""},
            {   bugs.disable_feedback_loops,
                    "disable_feedback_loops",
                    ""},
            {   bugs.dont_use_timer_query,
                    "dont_use_timer_query",
                    ""},
            {   bugs.disable_blit_into_texture_array,
                    "disable_blit_into_texture_array",
                    ""},
            {   bugs.split_easu,
                    "split_easu",
                    ""},
            {   bugs.invalidate_end_only_if_invalidate_start,
                    "invalidate_end_only_if_invalidate_start",
                    ""},
            {   bugs.allow_read_only_ancillary_feedback_loop,
                    "allow_read_only_ancillary_feedback_loop",
                    ""},
            {   bugs.enable_initialize_non_used_uniform_array,
                    "enable_initialize_non_used_uniform_array",
                    ""},
            {   bugs.powervr_shader_workarounds,
                    "powervr_shader_workarounds",
                    ""},
            {   bugs.delay_fbo_destruction,
                    "delay_fbo_destruction",
                    ""},
            {   bugs.rebind_buffer_after_deletion,
                    "rebind_buffer_after_deletion",
                    ""},
            {   bugs.force_feature_level0,
                    "force_feature_level0",
                    ""},
            {   bugs.disable_depth_precache_for_default_material,
                    "disable_depth_precache_for_default_material",
                    ""},
            {   bugs.disable_framebuffer_fetch_extension,
                    "disable_framebuffer_fetch_extension",
                    ""},
    }};

    // this is chosen to minimize code size
#if defined(BACKEND_OPENGL_VERSION_GLES)
    static void initExtensionsGLES(Extensions* ext, GLint major, GLint minor) noexcept;
#endif
#if defined(BACKEND_OPENGL_VERSION_GL)
    static void initExtensionsGL(Extensions* ext, GLint major, GLint minor) noexcept;
#endif

    static void initExtensions(Extensions* ext, GLint major, GLint minor) noexcept {
#if defined(BACKEND_OPENGL_VERSION_GLES)
        initExtensionsGLES(ext, major, minor);
#endif
#if defined(BACKEND_OPENGL_VERSION_GL)
        initExtensionsGL(ext, major, minor);
#endif
    }

    static void initBugs(Bugs* bugs, Extensions const& exts,
            GLint major, GLint minor,
            char const* vendor,
            char const* renderer,
            char const* version,
            char const* shader
    );

    static void initProcs(Procs* procs,
            Extensions const& exts, GLint major, GLint minor) noexcept;

    static void initWorkarounds(Bugs const& bugs, Extensions* ext);

    static FeatureLevel resolveFeatureLevel(GLint major, GLint minor,
            Extensions const& exts,
            Gets const& gets,
            Bugs const& bugs) noexcept;

    template <typename T, typename F>
    static inline void update_state(T& state, T const& expected, F functor, bool force = false) noexcept {
        if (UTILS_UNLIKELY(force || state != expected)) {
            state = expected;
            functor();
        }
    }

    void setDefaultState() noexcept;
};

// ------------------------------------------------------------------------------------------------

constexpr size_t OpenGLContext::getIndexForCap(GLenum cap) noexcept { //NOLINT
    size_t index = 0;
    switch (cap) {
        case GL_BLEND:                          index =  0; break;
        case GL_CULL_FACE:                      index =  1; break;
        case GL_SCISSOR_TEST:                   index =  2; break;
        case GL_DEPTH_TEST:                     index =  3; break;
        case GL_STENCIL_TEST:                   index =  4; break;
        case GL_DITHER:                         index =  5; break;
        case GL_SAMPLE_ALPHA_TO_COVERAGE:       index =  6; break;
        case GL_SAMPLE_COVERAGE:                index =  7; break;
        case GL_POLYGON_OFFSET_FILL:            index =  8; break;
#ifdef GL_ARB_seamless_cube_map
        case GL_TEXTURE_CUBE_MAP_SEAMLESS:      index =  9; break;
#endif
#ifdef BACKEND_OPENGL_VERSION_GL
        case GL_PROGRAM_POINT_SIZE:             index = 10; break;
#endif
        case GL_DEPTH_CLAMP:                    index = 11; break;
        default: break;
    }
    assert_invariant(index < state.enables.caps.size());
    return index;
}

constexpr size_t OpenGLContext::getIndexForBufferTarget(GLenum target) noexcept {
    size_t index = 0;
    switch (target) {
        // The indexed buffers MUST be first in this list (those usable with bindBufferRange)
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_UNIFORM_BUFFER:             index = 0; break;
        case GL_TRANSFORM_FEEDBACK_BUFFER:  index = 1; break;
#if defined(BACKEND_OPENGL_LEVEL_GLES31)
        case GL_SHADER_STORAGE_BUFFER:      index = 2; break;
#endif
#endif
        case GL_ARRAY_BUFFER:               index = 3; break;
        case GL_ELEMENT_ARRAY_BUFFER:       index = 4; break;
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
        case GL_PIXEL_PACK_BUFFER:          index = 5; break;
        case GL_PIXEL_UNPACK_BUFFER:        index = 6; break;
#endif
        default: break;
    }
    assert_invariant(index < sizeof(state.buffers.genericBinding)/sizeof(state.buffers.genericBinding[0])); // NOLINT(misc-redundant-expression)
    return index;
}

// ------------------------------------------------------------------------------------------------

void OpenGLContext::activeTexture(GLuint unit) noexcept {
    assert_invariant(unit < MAX_TEXTURE_UNIT_COUNT);
    update_state(state.textures.active, unit, [&]() {
        glActiveTexture(GL_TEXTURE0 + unit);
    });
}

void OpenGLContext::bindSampler(GLuint unit, GLuint sampler) noexcept {
    assert_invariant(unit < MAX_TEXTURE_UNIT_COUNT);
    assert_invariant(mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1);
#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    update_state(state.textures.units[unit].sampler, sampler, [&]() {
        glBindSampler(unit, sampler);
    });
#endif
}

void OpenGLContext::setScissor(GLint left, GLint bottom, GLsizei width, GLsizei height) noexcept {
    vec4gli const scissor(left, bottom, width, height);
    update_state(state.window.scissor, scissor, [&]() {
        glScissor(left, bottom, width, height);
    });
}

void OpenGLContext::viewport(GLint left, GLint bottom, GLsizei width, GLsizei height) noexcept {
    vec4gli const viewport(left, bottom, width, height);
    update_state(state.window.viewport, viewport, [&]() {
        glViewport(left, bottom, width, height);
    });
}

void OpenGLContext::depthRange(GLclampf near, GLclampf far) noexcept {
    vec2glf const depthRange(near, far);
    update_state(state.window.depthRange, depthRange, [&]() {
        glDepthRangef(near, far);
    });
}

void OpenGLContext::bindVertexArray(RenderPrimitive const* p) noexcept {
    RenderPrimitive* vao = p ? const_cast<RenderPrimitive *>(p) : &mDefaultVAO;
    update_state(state.vao.p, vao, [&]() {

        // See if we need to create a name for this VAO on the fly, this would happen if:
        // - we're not the default VAO, because its name is always 0
        // - our name is 0, this could happen if this VAO was created in the "other" context
        // - the nameVersion is out of date *and* we're on the protected context, in this case:
        //      - the name must be stale from a previous use of this context because we always
        //        destroy the protected context when we're done with it.
        bool const recreateVaoName = vao != &mDefaultVAO &&
                ((vao->vao[contextIndex] == 0) ||
                        (vao->nameVersion != state.age && contextIndex == 1));
        if (UTILS_UNLIKELY(recreateVaoName)) {
            vao->nameVersion = state.age;
            procs.genVertexArrays(1, &vao->vao[contextIndex]);
        }

        procs.bindVertexArray(vao->vao[contextIndex]);
        // update GL_ELEMENT_ARRAY_BUFFER, which is updated by glBindVertexArray
        size_t const targetIndex = getIndexForBufferTarget(GL_ELEMENT_ARRAY_BUFFER);
        state.buffers.genericBinding[targetIndex] = vao->elementArray;
        if (UTILS_UNLIKELY(bugs.vao_doesnt_store_element_array_buffer_binding || recreateVaoName)) {
            // This shouldn't be needed, but it looks like some drivers don't do the implicit
            // glBindBuffer().
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vao->elementArray);
        }
    });
}

void OpenGLContext::bindBufferRange(GLenum target, GLuint index, GLuint buffer,
        GLintptr offset, GLsizeiptr size) noexcept {
    assert_invariant(mFeatureLevel >= FeatureLevel::FEATURE_LEVEL_1);

#ifndef FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
#   ifdef BACKEND_OPENGL_LEVEL_GLES31
        assert_invariant(false
                 || target == GL_UNIFORM_BUFFER
                 || target == GL_TRANSFORM_FEEDBACK_BUFFER
                 || target == GL_SHADER_STORAGE_BUFFER);
#   else
        assert_invariant(false
                || target == GL_UNIFORM_BUFFER
                || target == GL_TRANSFORM_FEEDBACK_BUFFER);
#   endif
    size_t const targetIndex = getIndexForBufferTarget(target);
    // this ALSO sets the generic binding
    assert_invariant(targetIndex < sizeof(state.buffers.targets) / sizeof(*state.buffers.targets));
    if (   state.buffers.targets[targetIndex].buffers[index].name != buffer
           || state.buffers.targets[targetIndex].buffers[index].offset != offset
           || state.buffers.targets[targetIndex].buffers[index].size != size) {
        state.buffers.targets[targetIndex].buffers[index].name = buffer;
        state.buffers.targets[targetIndex].buffers[index].offset = offset;
        state.buffers.targets[targetIndex].buffers[index].size = size;
        state.buffers.genericBinding[targetIndex] = buffer;
        glBindBufferRange(target, index, buffer, offset, size);
    }
#endif
}

void OpenGLContext::bindTexture(GLuint unit, GLuint target, GLuint texId, bool external) noexcept {
    //  another texture is bound to the same unit with a different target,
    //  unbind the texture from the current target
    update_state(state.textures.units[unit].target, target, [&]() {
        activeTexture(unit);
        glBindTexture(state.textures.units[unit].target, 0);
    });
    update_state(state.textures.units[unit].id, texId, [&]() {
        activeTexture(unit);
        glBindTexture(target, texId);
    }, external);
}

void OpenGLContext::useProgram(GLuint program) noexcept {
    update_state(state.program.use, program, [&]() {
        glUseProgram(program);
    });
}

void OpenGLContext::enableVertexAttribArray(RenderPrimitive const* rp, GLuint index) noexcept {
    assert_invariant(rp);
    assert_invariant(index < rp->vertexAttribArray.size());
    bool const force = rp->stateVersion != state.age;
    if (UTILS_UNLIKELY(force || !rp->vertexAttribArray[index])) {
        rp->vertexAttribArray.set(index);
        glEnableVertexAttribArray(index);
    }
}

void OpenGLContext::disableVertexAttribArray(RenderPrimitive const* rp, GLuint index) noexcept {
    assert_invariant(rp);
    assert_invariant(index < rp->vertexAttribArray.size());
    bool const force = rp->stateVersion != state.age;
    if (UTILS_UNLIKELY(force || rp->vertexAttribArray[index])) {
        rp->vertexAttribArray.unset(index);
        glDisableVertexAttribArray(index);
    }
}

void OpenGLContext::enable(GLenum cap) noexcept {
    size_t const index = getIndexForCap(cap);
    if (UTILS_UNLIKELY(!state.enables.caps[index])) {
        state.enables.caps.set(index);
        glEnable(cap);
    }
}

void OpenGLContext::disable(GLenum cap) noexcept {
    size_t const index = getIndexForCap(cap);
    if (UTILS_UNLIKELY(state.enables.caps[index])) {
        state.enables.caps.unset(index);
        glDisable(cap);
    }
}

void OpenGLContext::frontFace(GLenum mode) noexcept {
    update_state(state.raster.frontFace, mode, [&]() {
        glFrontFace(mode);
    });
}

void OpenGLContext::cullFace(GLenum mode) noexcept {
    update_state(state.raster.cullFace, mode, [&]() {
        glCullFace(mode);
    });
}

void OpenGLContext::blendEquation(GLenum modeRGB, GLenum modeA) noexcept {
    if (UTILS_UNLIKELY(
            state.raster.blendEquationRGB != modeRGB || state.raster.blendEquationA != modeA)) {
        state.raster.blendEquationRGB = modeRGB;
        state.raster.blendEquationA   = modeA;
        glBlendEquationSeparate(modeRGB, modeA);
    }
}

void OpenGLContext::blendFunction(GLenum srcRGB, GLenum srcA, GLenum dstRGB, GLenum dstA) noexcept {
    if (UTILS_UNLIKELY(
            state.raster.blendFunctionSrcRGB != srcRGB ||
            state.raster.blendFunctionSrcA != srcA ||
            state.raster.blendFunctionDstRGB != dstRGB ||
            state.raster.blendFunctionDstA != dstA)) {
        state.raster.blendFunctionSrcRGB = srcRGB;
        state.raster.blendFunctionSrcA = srcA;
        state.raster.blendFunctionDstRGB = dstRGB;
        state.raster.blendFunctionDstA = dstA;
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
    }
}

void OpenGLContext::colorMask(GLboolean flag) noexcept {
    update_state(state.raster.colorMask, flag, [&]() {
        glColorMask(flag, flag, flag, flag);
    });
}
void OpenGLContext::depthMask(GLboolean flag) noexcept {
    update_state(state.raster.depthMask, flag, [&]() {
        glDepthMask(flag);
    });
}

void OpenGLContext::depthFunc(GLenum func) noexcept {
    update_state(state.raster.depthFunc, func, [&]() {
        glDepthFunc(func);
    });
}

void OpenGLContext::stencilFuncSeparate(GLenum funcFront, GLint refFront, GLuint maskFront,
        GLenum funcBack, GLint refBack, GLuint maskBack) noexcept {
    update_state(state.stencil.front.func, {funcFront, refFront, maskFront}, [&]() {
        glStencilFuncSeparate(GL_FRONT, funcFront, refFront, maskFront);
    });
    update_state(state.stencil.back.func, {funcBack, refBack, maskBack}, [&]() {
        glStencilFuncSeparate(GL_BACK, funcBack, refBack, maskBack);
    });
}

void OpenGLContext::stencilOpSeparate(GLenum sfailFront, GLenum dpfailFront, GLenum dppassFront,
        GLenum sfailBack, GLenum dpfailBack, GLenum dppassBack) noexcept {
    update_state(state.stencil.front.op, {sfailFront, dpfailFront, dppassFront}, [&]() {
        glStencilOpSeparate(GL_FRONT, sfailFront, dpfailFront, dppassFront);
    });
    update_state(state.stencil.back.op, {sfailBack, dpfailBack, dppassBack}, [&]() {
        glStencilOpSeparate(GL_BACK, sfailBack, dpfailBack, dppassBack);
    });
}

void OpenGLContext::stencilMaskSeparate(GLuint maskFront, GLuint maskBack) noexcept {
    update_state(state.stencil.front.stencilMask, maskFront, [&]() {
        glStencilMaskSeparate(GL_FRONT, maskFront);
    });
    update_state(state.stencil.back.stencilMask, maskBack, [&]() {
        glStencilMaskSeparate(GL_BACK, maskBack);
    });
}

void OpenGLContext::polygonOffset(GLfloat factor, GLfloat units) noexcept {
    update_state(state.polygonOffset, { factor, units }, [&]() {
        if (factor != 0 || units != 0) {
            glPolygonOffset(factor, units);
            enable(GL_POLYGON_OFFSET_FILL);
        } else {
            disable(GL_POLYGON_OFFSET_FILL);
        }
    });
}

} // namespace filament

#endif //TNT_FILAMENT_BACKEND_OPENGLCONTEXT_H
