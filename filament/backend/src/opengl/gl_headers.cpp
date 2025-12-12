/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "gl_headers.h"

#if defined(FILAMENT_IMPORT_ENTRY_POINTS)

#include <EGL/egl.h>
#include <mutex>

/**
 * 获取函数指针
 * 
 * 使用 EGL 获取扩展函数的入口点。
 * 对于非 EGL 平台，我们需要以不同的方式实现此功能。目前，这不是问题。
 * 
 * @param pfn 函数指针引用（输出）
 * @param name 函数名称
 */
template<typename T>
static void getProcAddress(T& pfn, const char* name) noexcept {
    pfn = (T)eglGetProcAddress(name);
}

/**
 * GLES 扩展命名空间
 * 
 * 包含所有扩展函数的函数指针声明和初始化。
 */
namespace glext {
#ifndef __EMSCRIPTEN__
#ifdef GL_OES_EGL_image
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
#endif
#if GL_EXT_debug_marker
PFNGLINSERTEVENTMARKEREXTPROC glInsertEventMarkerEXT;
PFNGLPUSHGROUPMARKEREXTPROC glPushGroupMarkerEXT;
PFNGLPOPGROUPMARKEREXTPROC glPopGroupMarkerEXT;
#endif
#if GL_EXT_multisampled_render_to_texture
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT;
PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT;
#endif
#ifdef GL_KHR_debug
PFNGLDEBUGMESSAGECALLBACKKHRPROC glDebugMessageCallbackKHR;
PFNGLGETDEBUGMESSAGELOGKHRPROC glGetDebugMessageLogKHR;
#endif
#ifdef GL_EXT_disjoint_timer_query
PFNGLGENQUERIESEXTPROC glGenQueriesEXT;
PFNGLDELETEQUERIESEXTPROC glDeleteQueriesEXT;
PFNGLBEGINQUERYEXTPROC glBeginQueryEXT;
PFNGLENDQUERYEXTPROC glEndQueryEXT;
PFNGLGETQUERYOBJECTUIVEXTPROC glGetQueryObjectuivEXT;
PFNGLGETQUERYOBJECTUI64VEXTPROC glGetQueryObjectui64vEXT;
#endif
#ifdef GL_OES_vertex_array_object
PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
#endif
#ifdef GL_EXT_clip_control
PFNGLCLIPCONTROLEXTPROC glClipControlEXT;
#endif
#ifdef GL_EXT_discard_framebuffer
PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
#endif
#ifdef GL_KHR_parallel_shader_compile
PFNGLMAXSHADERCOMPILERTHREADSKHRPROC glMaxShaderCompilerThreadsKHR;
#endif
#ifdef GL_OVR_multiview
PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR;
#endif
#ifdef GL_OVR_multiview_multisampled_render_to_texture
PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR;
#endif

#if defined(__ANDROID__) && !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
// On Android, If we want to support a build system less than ANDROID_API 21, we need to
// use getProcAddress for ES3.1 and above entry points.
PFNGLDISPATCHCOMPUTEPROC glDispatchCompute;
#endif
/**
 * 扩展初始化标志
 * 
 * 确保扩展入口点只初始化一次（线程安全）。
 */
static std::once_flag sGlExtInitialized;
#endif // __EMSCRIPTEN__

/**
 * 导入 GLES 扩展入口点
 * 
 * 使用 eglGetProcAddress 获取所有扩展函数的入口点。
 * 此函数是线程安全的，可以多次调用（使用 std::call_once 确保只初始化一次）。
 * 
 * 执行流程：
 * 1. 检查是否已初始化（使用 std::call_once）
 * 2. 为每个扩展调用 getProcAddress 获取函数指针
 * 3. 存储函数指针供后续使用
 */
void importGLESExtensionsEntryPoints() {
#ifndef __EMSCRIPTEN__
    // 使用 std::call_once 确保只初始化一次（线程安全）
    std::call_once(sGlExtInitialized, +[]() {
#ifdef GL_OES_EGL_image
    getProcAddress(glEGLImageTargetTexture2DOES, "glEGLImageTargetTexture2DOES");
#endif
#if GL_EXT_debug_marker
    getProcAddress(glInsertEventMarkerEXT, "glInsertEventMarkerEXT");
    getProcAddress(glPushGroupMarkerEXT, "glPushGroupMarkerEXT");
    getProcAddress(glPopGroupMarkerEXT, "glPopGroupMarkerEXT");
#endif
#if GL_EXT_multisampled_render_to_texture
    getProcAddress(glFramebufferTexture2DMultisampleEXT, "glFramebufferTexture2DMultisampleEXT");
    getProcAddress(glRenderbufferStorageMultisampleEXT, "glRenderbufferStorageMultisampleEXT");
#endif
#ifdef GL_KHR_debug
    getProcAddress(glDebugMessageCallbackKHR, "glDebugMessageCallbackKHR");
    getProcAddress(glGetDebugMessageLogKHR, "glGetDebugMessageLogKHR");
#endif
#ifdef GL_EXT_disjoint_timer_query
    getProcAddress(glGenQueriesEXT, "glGenQueriesEXT");
    getProcAddress(glDeleteQueriesEXT, "glDeleteQueriesEXT");
    getProcAddress(glBeginQueryEXT, "glBeginQueryEXT");
    getProcAddress(glEndQueryEXT, "glEndQueryEXT");
    getProcAddress(glGetQueryObjectuivEXT, "glGetQueryObjectuivEXT");
    getProcAddress(glGetQueryObjectui64vEXT, "glGetQueryObjectui64vEXT");
#endif
#if defined(GL_OES_vertex_array_object)
    getProcAddress(glBindVertexArrayOES, "glBindVertexArrayOES");
    getProcAddress(glDeleteVertexArraysOES, "glDeleteVertexArraysOES");
    getProcAddress(glGenVertexArraysOES, "glGenVertexArraysOES");
#endif
#ifdef GL_EXT_clip_control
    getProcAddress(glClipControlEXT, "glClipControlEXT");
#endif
#ifdef GL_EXT_discard_framebuffer
        getProcAddress(glDiscardFramebufferEXT, "glDiscardFramebufferEXT");
#endif
#ifdef GL_KHR_parallel_shader_compile
        getProcAddress(glMaxShaderCompilerThreadsKHR, "glMaxShaderCompilerThreadsKHR");
#endif
#ifdef GL_OVR_multiview
        getProcAddress(glFramebufferTextureMultiviewOVR, "glFramebufferTextureMultiviewOVR");
#endif
#ifdef GL_OVR_multiview_multisampled_render_to_texture
        getProcAddress(glFramebufferTextureMultisampleMultiviewOVR, "glFramebufferTextureMultisampleMultiviewOVR");
#endif
#if defined(__ANDROID__) && !defined(FILAMENT_SILENCE_NOT_SUPPORTED_BY_ES2)
        getProcAddress(glDispatchCompute, "glDispatchCompute");
#endif
    });
#endif // __EMSCRIPTEN__
}

} // namespace glext

#endif // defined(FILAMENT_IMPORT_ENTRY_POINTS)
