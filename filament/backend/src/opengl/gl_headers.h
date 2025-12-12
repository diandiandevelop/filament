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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_GL_HEADERS_H
#define TNT_FILAMENT_BACKEND_OPENGL_GL_HEADERS_H

/**
 * OpenGL 头文件配置
 * 
 * 此文件统一处理不同平台的 OpenGL 头文件包含和扩展函数入口点。
 * 
 * 我们旨在支持的配置：
 *
 * GL 4.5 头文件
 *      - GL 4.1 运行时（用于 macOS）
 *      - GL 4.5 运行时
 *
 * GLES 2.0 头文件
 *      - GLES 2.0 运行时（仅 Android）
 *
 * GLES 3.0 头文件
 *      - GLES 3.0 运行时（仅 iOS 和 WebGL2）
 *
 * GLES 3.1 头文件
 *      - GLES 2.0 运行时
 *      - GLES 3.0 运行时
 *      - GLES 3.1 运行时
 */


#if defined(__ANDROID__) || defined(FILAMENT_USE_EXTERNAL_GLES3) || defined(__EMSCRIPTEN__) || defined(FILAMENT_SUPPORTS_EGL_ON_LINUX)

    #if defined(__EMSCRIPTEN__)
    #   include <GLES3/gl3.h>
    #else
//    #   include <GLES2/gl2.h>
    #   include <GLES3/gl31.h>
    #endif
    #include <GLES2/gl2ext.h>

    // 仅用于开发和调试目的，我们希望支持使用仅 ES2 头文件编译此后端。
    // 在这种情况下（即我们有 VERSION_2 但没有 VERSION_3+），
    // 我们定义 FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2，目的是编译掉
    // 无法使用 ES2 头文件编译的代码。
    // 在生产中，此代码被编译进去，但由于运行时检查或断言，永远不会执行。
    #if defined(GL_ES_VERSION_2_0) && !defined(GL_ES_VERSION_3_0)
    #   define FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2
    #endif

#elif defined(FILAMENT_IOS)

    #define GLES_SILENCE_DEPRECATION

    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>

#else

    // bluegl 暴露带有 bluegl_ 前缀的符号，以避免与也链接 GL 的客户端冲突。
    // 此头文件使用 bluegl_ 前缀重新定义 GL 函数名称。
    // 例如：
    //   #define glFunction bluegl_glFunction
    // 此头文件必须在 <bluegl/BlueGL.h> 之前包含。
    #include <bluegl/BlueGLDefines.h>
    #include <bluegl/BlueGL.h>

#endif

/**
 * 验证我们旨在支持的头文件配置
 * 
 * 确保包含的头文件版本与平台兼容。
 */
#if defined(GL_VERSION_4_5)
    // OpenGL 4.5 头文件（桌面平台）
#elif defined(GL_ES_VERSION_3_1)
    // GLES 3.1 头文件
#elif defined(GL_ES_VERSION_3_0)
    // GLES 3.0 头文件（仅 iOS 和 WebGL2 支持）
    #if !defined(FILAMENT_IOS) && !defined(__EMSCRIPTEN__)
        #error "GLES 3.0 headers only supported on iOS and WebGL2"
    #endif
#elif defined(GL_ES_VERSION_2_0)
    // GLES 2.0 头文件（仅 Android 支持）
    #if !defined(__ANDROID__)
        #error "GLES 2.0 headers only supported on Android"
    #endif
#else
    // 未定义任何支持的版本
    #error "Minimum header version must be OpenGL ES 2.0 or OpenGL 4.5"
#endif

/**
 * GLES 扩展
 * 
 * 处理 GLES 扩展函数的入口点导入。
 * 在 Android 上，NDK 不暴露扩展函数，需要使用 eglGetProcAddress 获取。
 */

#if defined(GL_ES_VERSION_2_0)  // 这基本上意味着所有版本的 GLES

#if defined(FILAMENT_IOS)

// iOS 头文件只提供原型，无需处理。

#else

// 定义此宏以启用扩展入口点导入
#define FILAMENT_IMPORT_ENTRY_POINTS

/**
 * GLES 扩展命名空间
 * 
 * Android NDK 不暴露扩展函数，使用 eglGetProcAddress 模拟。
 */
namespace glext {
/**
 * 导入 GLES 扩展入口点
 * 
 * 此函数是线程安全的，可以多次调用。
 * 目前从 PlatformEGL 调用。
 */
void importGLESExtensionsEntryPoints();

#ifndef __EMSCRIPTEN__
    // OES_EGL_image 扩展：EGL 图像目标纹理
    #ifdef GL_OES_EGL_image
    extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
    #endif
    
    // EXT_debug_marker 扩展：调试标记函数
    #ifdef GL_EXT_debug_marker
    extern PFNGLINSERTEVENTMARKEREXTPROC glInsertEventMarkerEXT;
    extern PFNGLPUSHGROUPMARKEREXTPROC glPushGroupMarkerEXT;
    extern PFNGLPOPGROUPMARKEREXTPROC glPopGroupMarkerEXT;
    #endif
    
    // EXT_multisampled_render_to_texture 扩展：多重采样渲染到纹理
    #ifdef GL_EXT_multisampled_render_to_texture
    extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT;
    extern PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT;
    #endif
    
    // KHR_debug 扩展：调试回调函数
    #ifdef GL_KHR_debug
    extern PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
    extern PFNGLGETDEBUGMESSAGELOGKHRPROC glGetDebugMessageLogKHR;
    #endif
    
    // EXT_clip_control 扩展：裁剪控制
    #ifdef GL_EXT_clip_control
    extern PFNGLCLIPCONTROLEXTPROC glClipControlEXT;
    #endif
    
    // EXT_disjoint_timer_query 扩展：定时查询函数
    #ifdef GL_EXT_disjoint_timer_query
    extern PFNGLGENQUERIESEXTPROC glGenQueriesEXT;
    extern PFNGLDELETEQUERIESEXTPROC glDeleteQueriesEXT;
    extern PFNGLBEGINQUERYEXTPROC glBeginQueryEXT;
    extern PFNGLENDQUERYEXTPROC glEndQueryEXT;
    extern PFNGLGETQUERYOBJECTUIVEXTPROC glGetQueryObjectuivEXT;
    extern PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT;
    #endif
    
    // OES_vertex_array_object 扩展：顶点数组对象
    #ifdef GL_OES_vertex_array_object
    extern PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
    extern PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
    extern PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
    #endif
    
    // EXT_discard_framebuffer 扩展：丢弃帧缓冲区
    #ifdef GL_EXT_discard_framebuffer
    extern PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
    #endif
    
    // KHR_parallel_shader_compile 扩展：并行着色器编译
    #ifdef GL_KHR_parallel_shader_compile
    extern PFNGLMAXSHADERCOMPILERTHREADSKHRPROC glMaxShaderCompilerThreadsKHR;
    #endif
    
    // OVR_multiview 扩展：多视图渲染
    #ifdef GL_OVR_multiview
    extern PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR;
    #endif
    
    // OVR_multiview_multisampled_render_to_texture 扩展：多重采样多视图渲染到纹理
    #ifdef GL_OVR_multiview_multisampled_render_to_texture
    extern PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR;
    #endif
    
    // 计算着色器（Android，ES 3.1+）
    // 在 Android 上，如果我们想支持低于 ANDROID_API 21 的构建系统，
    // 我们需要使用 getProcAddress 获取 ES 3.1 及以上的入口点。
    #if defined(__ANDROID__) && !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
    extern PFNGLDISPATCHCOMPUTEPROC glDispatchCompute;
    #endif
#endif // __EMSCRIPTEN__
} // namespace glext

using namespace glext;

#endif

/**
 * 统一桌面和移动平台的常量定义
 * 
 * 防止桌面和移动平台之间的大量 #ifdef。
 * 将扩展特定的常量统一为标准名称。
 */

// EXT_disjoint_timer_query 扩展常量统一
#ifdef GL_EXT_disjoint_timer_query
#   define GL_TIME_ELAPSED                          GL_TIME_ELAPSED_EXT
#   ifndef GL_ES_VERSION_3_0
#       define GL_QUERY_RESULT_AVAILABLE            GL_QUERY_RESULT_AVAILABLE_EXT
#       define GL_QUERY_RESULT                      GL_QUERY_RESULT_EXT
#   endif
#endif

// EXT_clip_control 扩展常量统一
#ifdef GL_EXT_clip_control
#   define GL_LOWER_LEFT                            GL_LOWER_LEFT_EXT
#   define GL_ZERO_TO_ONE                           GL_ZERO_TO_ONE_EXT
#endif

// KHR_parallel_shader_compile 扩展常量统一
#ifdef GL_KHR_parallel_shader_compile
#   define GL_COMPLETION_STATUS                     GL_COMPLETION_STATUS_KHR
#else
#   define GL_COMPLETION_STATUS                     0x91B1  // 如果扩展不可用，使用硬编码值
#endif

// 我们需要 GL_TEXTURE_CUBE_MAP_ARRAY 定义，但如果扩展/功能不可用，我们不会使用它。
#if defined(GL_EXT_texture_cube_map_array)
#   define GL_TEXTURE_CUBE_MAP_ARRAY                GL_TEXTURE_CUBE_MAP_ARRAY_EXT
#else
#   define GL_TEXTURE_CUBE_MAP_ARRAY                0x9009  // 硬编码值
#endif

// EXT_clip_cull_distance 扩展常量统一
#if defined(GL_EXT_clip_cull_distance)
#   define GL_CLIP_DISTANCE0                        GL_CLIP_DISTANCE0_EXT
#   define GL_CLIP_DISTANCE1                        GL_CLIP_DISTANCE1_EXT
#else
#   define GL_CLIP_DISTANCE0                        0x3000  // 硬编码值
#   define GL_CLIP_DISTANCE1                        0x3001  // 硬编码值
#endif

// EXT_depth_clamp 扩展常量统一
#if defined(GL_EXT_depth_clamp)
#   define GL_DEPTH_CLAMP                           GL_DEPTH_CLAMP_EXT
#else
#   define GL_DEPTH_CLAMP                           0x864F  // 硬编码值
#endif

#if defined(GL_KHR_debug)
#   define GL_DEBUG_OUTPUT                          GL_DEBUG_OUTPUT_KHR
#   define GL_DEBUG_OUTPUT_SYNCHRONOUS              GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR
#   define GL_DEBUG_SEVERITY_HIGH                   GL_DEBUG_SEVERITY_HIGH_KHR
#   define GL_DEBUG_SEVERITY_MEDIUM                 GL_DEBUG_SEVERITY_MEDIUM_KHR
#   define GL_DEBUG_SEVERITY_LOW                    GL_DEBUG_SEVERITY_LOW_KHR
#   define GL_DEBUG_SEVERITY_NOTIFICATION           GL_DEBUG_SEVERITY_NOTIFICATION_KHR
#   define GL_DEBUG_TYPE_MARKER                     GL_DEBUG_TYPE_MARKER_KHR
#   define GL_DEBUG_TYPE_ERROR                      GL_DEBUG_TYPE_ERROR_KHR
#   define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR        GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_KHR
#   define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR         GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_KHR
#   define GL_DEBUG_TYPE_PORTABILITY                GL_DEBUG_TYPE_PORTABILITY_KHR
#   define GL_DEBUG_TYPE_PERFORMANCE                GL_DEBUG_TYPE_PERFORMANCE_KHR
#   define GL_DEBUG_TYPE_OTHER                      GL_DEBUG_TYPE_OTHER_KHR
#   define glDebugMessageCallback                   glDebugMessageCallbackKHR
#endif

/**
 * ES2 扩展常量统一
 * 
 * 在 ES3 核心中存在但在 ES2 中是扩展（强制或非强制）的标记。
 * 统一这些常量以便在 ES2 和 ES3 之间使用相同的代码。
 */
#ifndef GL_ES_VERSION_3_0
    // OES_vertex_array_object 扩展
    #ifdef GL_OES_vertex_array_object
    #   define GL_VERTEX_ARRAY_BINDING             GL_VERTEX_ARRAY_BINDING_OES
    #endif
    
    // OES_rgb8_rgba8 扩展
    #ifdef GL_OES_rgb8_rgba8
    #   define GL_RGB8                             0x8051
    #   define GL_RGBA8                            0x8058
    #endif
    
    // OES_depth24 扩展
    #ifdef GL_OES_depth24
    #   define GL_DEPTH_COMPONENT24                GL_DEPTH_COMPONENT24_OES
    #endif
    
    // EXT_discard_framebuffer 扩展
    #ifdef GL_EXT_discard_framebuffer
    #   define GL_COLOR                             GL_COLOR_EXT
    #   define GL_DEPTH                             GL_DEPTH_EXT
    #   define GL_STENCIL                           GL_STENCIL_EXT
    #endif
    
    // OES_packed_depth_stencil 扩展
    #ifdef GL_OES_packed_depth_stencil
    #   define GL_DEPTH_STENCIL                     GL_DEPTH_STENCIL_OES
    #   define GL_UNSIGNED_INT_24_8                 GL_UNSIGNED_INT_24_8_OES
    #   define GL_DEPTH24_STENCIL8                  GL_DEPTH24_STENCIL8_OES
    #endif
#endif

#else // 以下为所有版本的 OpenGL（桌面平台）

// ARB_parallel_shader_compile 扩展常量统一（桌面 OpenGL）
#ifdef GL_ARB_parallel_shader_compile
#   define GL_COMPLETION_STATUS                     GL_COMPLETION_STATUS_ARB
#else
#   define GL_COMPLETION_STATUS                     0x91B1  // 硬编码值
#endif

#endif // GL_ES_VERSION_2_0

/**
 * 外部纹理常量定义
 * 
 * 这只是为了简化实现（即我们不必到处都有 #ifdef）。
 * 如果头文件中没有定义，我们手动定义它。
 */
#ifndef GL_OES_EGL_image_external
#define GL_TEXTURE_EXTERNAL_OES           0x8D65
#endif

/**
 * WebGL 2.0 特定函数
 * 
 * 这是一个奇怪的函数，存在于 WebGL 2.0 但不存在于 OpenGL ES 中。
 */
#if defined(__EMSCRIPTEN__)
extern "C" {
void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data);
}
#endif

/**
 * 验证必需的扩展头文件
 * 
 * 在非 iOS 平台上，某些扩展是必需的。
 */
#ifdef GL_ES_VERSION_2_0
#   ifndef FILAMENT_IOS
#      ifndef GL_OES_vertex_array_object
#          error "Headers with GL_OES_vertex_array_object are mandatory unless on iOS"
#      endif
#      ifndef GL_EXT_disjoint_timer_query
#          error "Headers with GL_EXT_disjoint_timer_query are mandatory unless on iOS"
#      endif
#      ifndef GL_OES_rgb8_rgba8
#          error "Headers with GL_OES_rgb8_rgba8 are mandatory unless on iOS"
#      endif
#   endif
#endif

/**
 * 定义后端版本宏
 * 
 * 根据包含的头文件定义后端版本。
 */
#if defined(GL_ES_VERSION_2_0)
#   define BACKEND_OPENGL_VERSION_GLES  // GLES 版本
#elif defined(GL_VERSION_4_5)
#   define BACKEND_OPENGL_VERSION_GL     // 桌面 OpenGL 版本
#else
#   error "Unsupported header version"
#endif

/**
 * 定义功能级别宏
 * 
 * 根据包含的头文件定义支持的功能级别。
 */
#if defined(GL_VERSION_4_5) || defined(GL_ES_VERSION_3_1)
#   define BACKEND_OPENGL_LEVEL_GLES31  // GLES 3.1 / GL 4.5 功能级别
#   ifdef __EMSCRIPTEN__
#       error "__EMSCRIPTEN__ shouldn't be defined with GLES 3.1 headers"
#   endif
#endif
#if defined(GL_VERSION_4_5) || defined(GL_ES_VERSION_3_0)
#   define BACKEND_OPENGL_LEVEL_GLES30  // GLES 3.0 / GL 4.5 功能级别
#endif
#if defined(GL_VERSION_4_5) || defined(GL_ES_VERSION_2_0)
#   define BACKEND_OPENGL_LEVEL_GLES20  // GLES 2.0 / GL 4.5 功能级别
#endif

#include "NullGLES.h"

#endif // TNT_FILAMENT_BACKEND_OPENGL_GL_HEADERS_H
