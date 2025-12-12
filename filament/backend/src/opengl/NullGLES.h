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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_NULLGLES_H
#define TNT_FILAMENT_BACKEND_OPENGL_NULLGLES_H

/**
 * NullGLES - OpenGL ES 空实现
 * 
 * 此文件用于调试目的（将 GLES 调用替换为空操作）。
 * 
 * 使用方法：
 * 只需取消注释下面的行：
 *   using namespace nullgles;
 * 
 * 这将使所有 OpenGL ES 调用变成空操作，可以用于：
 * - 测试 Filament 的逻辑流程而不实际执行 OpenGL 调用
 * - 调试时隔离 OpenGL 相关的问题
 * - 在没有 OpenGL 上下文的环境中测试代码
 */

namespace filament::backend {
namespace nullgles {

/**
 * 视口和裁剪相关函数（空实现）
 */

/**
 * 设置裁剪矩形（空实现）
 * 
 * 对应 OpenGL ES: glScissor(x, y, width, height)
 */
inline void glScissor(GLint, GLint, GLsizei, GLsizei) { }

/**
 * 设置视口（空实现）
 * 
 * 对应 OpenGL ES: glViewport(x, y, width, height)
 */
inline void glViewport(GLint, GLint, GLsizei, GLsizei) { }

/**
 * 设置深度范围（空实现）
 * 
 * 对应 OpenGL ES: glDepthRangef(near, far)
 */
inline void glDepthRangef(GLfloat, GLfloat) { }

/**
 * 清除相关函数（空实现）
 */

/**
 * 设置清除颜色（空实现）
 * 
 * 对应 OpenGL ES: glClearColor(red, green, blue, alpha)
 */
inline void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) { }

/**
 * 设置清除模板值（空实现）
 * 
 * 对应 OpenGL ES: glClearStencil(s)
 */
inline void glClearStencil(GLint) { }

/**
 * 设置清除深度值（空实现）
 * 
 * 对应 OpenGL ES: glClearDepthf(depth)
 */
inline void glClearDepthf(GLfloat) { }

/**
 * 绑定相关函数（空实现）
 */

/**
 * 绑定缓冲区到索引（空实现）
 * 
 * 对应 OpenGL ES: glBindBufferBase(target, index, buffer)
 */
inline void glBindBufferBase(GLenum, GLuint, GLuint) { }

/**
 * 绑定顶点数组对象（空实现）
 * 
 * 对应 OpenGL ES: glBindVertexArray(array)
 */
inline void glBindVertexArray (GLuint) { }

/**
 * 绑定纹理（空实现）
 * 
 * 对应 OpenGL ES: glBindTexture(target, texture)
 */
inline void glBindTexture (GLenum, GLuint)   { }

/**
 * 绑定缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glBindBuffer(target, buffer)
 */
inline void glBindBuffer (GLenum, GLuint) { }

/**
 * 绑定帧缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glBindFramebuffer(target, framebuffer)
 */
inline void glBindFramebuffer (GLenum, GLuint)   { }

/**
 * 绑定渲染缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glBindRenderbuffer(target, renderbuffer)
 */
inline void glBindRenderbuffer (GLenum, GLuint) { }

/**
 * 使用着色器程序（空实现）
 * 
 * 对应 OpenGL ES: glUseProgram(program)
 */
inline void glUseProgram (GLuint)   { }

/**
 * 缓冲区操作函数（空实现）
 */

/**
 * 创建并初始化缓冲区数据（空实现）
 * 
 * 对应 OpenGL ES: glBufferData(target, size, data, usage)
 */
inline void glBufferData(GLenum, GLsizeiptr, const void *, GLenum) { }

/**
 * 更新缓冲区子数据（空实现）
 * 
 * 对应 OpenGL ES: glBufferSubData(target, offset, size, data)
 */
inline void glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void *) { }

/**
 * 纹理操作函数（空实现）
 */

/**
 * 指定压缩纹理子图像（空实现）
 * 
 * 对应 OpenGL ES: glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data)
 */
inline void glCompressedTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *) { }

/**
 * 指定纹理子图像（空实现）
 * 
 * 对应 OpenGL ES: glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels)
 */
inline void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) { }

/**
 * 生成 mipmap（空实现）
 * 
 * 对应 OpenGL ES: glGenerateMipmap(target)
 */
inline void glGenerateMipmap(GLenum) { }

/**
 * 顶点属性相关函数（空实现）
 */

/**
 * 指定顶点属性数据（空实现）
 * 
 * 对应 OpenGL ES: glVertexAttribPointer(index, size, type, normalized, stride, pointer)
 */
inline void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) { }

/**
 * 禁用顶点属性数组（空实现）
 * 
 * 对应 OpenGL ES: glDisableVertexAttribArray(index)
 */
inline void glDisableVertexAttribArray (GLuint) { }

/**
 * 启用顶点属性数组（空实现）
 * 
 * 对应 OpenGL ES: glEnableVertexAttribArray(index)
 */
inline void glEnableVertexAttribArray (GLuint) { }

/**
 * 状态管理函数（空实现）
 */

/**
 * 禁用 OpenGL 功能（空实现）
 * 
 * 对应 OpenGL ES: glDisable(cap)
 */
inline void glDisable (GLenum cap) { }

/**
 * 启用 OpenGL 功能（空实现）
 * 
 * 对应 OpenGL ES: glEnable(cap)
 */
inline void glEnable (GLenum cap) { }

/**
 * 设置面剔除模式（空实现）
 * 
 * 对应 OpenGL ES: glCullFace(mode)
 */
inline void glCullFace (GLenum mode) { }

/**
 * 设置混合函数（空实现）
 * 
 * 对应 OpenGL ES: glBlendFunc(sfactor, dfactor)
 */
inline void glBlendFunc (GLenum, GLenum) { }

/**
 * 帧缓冲区失效函数（空实现）
 */

/**
 * 使帧缓冲区附件失效（空实现）
 * 
 * 对应 OpenGL ES: glInvalidateFramebuffer(target, numAttachments, attachments)
 */
inline void glInvalidateFramebuffer (GLenum, GLsizei, const GLenum *) { }

/**
 * 使帧缓冲区子区域失效（空实现）
 * 
 * 对应 OpenGL ES: glInvalidateSubFramebuffer(target, numAttachments, attachments, x, y, width, height)
 */
inline void glInvalidateSubFramebuffer (GLenum, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei);

/**
 * 采样器参数函数（空实现）
 */

/**
 * 设置采样器整数参数（空实现）
 * 
 * 对应 OpenGL ES: glSamplerParameteri(sampler, pname, param)
 */
inline void glSamplerParameteri (GLuint, GLenum, GLint) { }

/**
 * 设置采样器浮点参数（空实现）
 * 
 * 对应 OpenGL ES: glSamplerParameterf(sampler, pname, param)
 */
inline void glSamplerParameterf (GLuint, GLenum, GLfloat) { }

/**
 * 渲染缓冲区相关函数（空实现）
 */

/**
 * 将渲染缓冲区附加到帧缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer)
 */
inline void glFramebufferRenderbuffer (GLenum, GLenum, GLenum, GLuint) { }

/**
 * 为多重采样渲染缓冲区建立数据存储（空实现）
 * 
 * 对应 OpenGL ES: glRenderbufferStorageMultisample(target, samples, internalformat, width, height)
 */
inline void glRenderbufferStorageMultisample (GLenum, GLsizei, GLenum, GLsizei, GLsizei) { }

/**
 * 为渲染缓冲区建立数据存储（空实现）
 * 
 * 对应 OpenGL ES: glRenderbufferStorage(target, internalformat, width, height)
 */
inline void glRenderbufferStorage (GLenum, GLenum, GLsizei, GLsizei) { }

/**
 * 绘制和读取函数（空实现）
 */

/**
 * 清除缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glClear(mask)
 */
inline void glClear(GLbitfield) { }

/**
 * 绘制元素（空实现）
 * 
 * 对应 OpenGL ES: glDrawRangeElements(mode, start, end, count, type, indices)
 */
inline void glDrawRangeElements(GLenum, GLuint, GLuint, GLsizei, GLenum, const void *)  { }

/**
 * 复制帧缓冲区（空实现）
 * 
 * 对应 OpenGL ES: glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter)
 */
inline void glBlitFramebuffer (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) { }

/**
 * 读取像素（空实现）
 * 
 * 对应 OpenGL ES: glReadPixels(x, y, width, height, format, type, pixels)
 */
inline void glReadPixels (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) { }

/**
 * 错误查询函数（空实现）
 */

/**
 * 返回错误信息（空实现）
 * 
 * 对应 OpenGL ES: glGetError()
 * 
 * @return 总是返回 GL_NO_ERROR
 */
inline GLenum glGetError() { return GL_NO_ERROR; }

} // namespace nullgles

/**
 * 将上面定义的 GLES 调用转换为空操作
 * 
 * 取消注释下面的行以启用空 GLES 实现：
 */
//using namespace nullgles;

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_OPENGL_NULLGLES_H
