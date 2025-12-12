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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_ANDROID_EXTERNAL_STREAM_MANAGER_ANDROID_H
#define TNT_FILAMENT_BACKEND_OPENGL_ANDROID_EXTERNAL_STREAM_MANAGER_ANDROID_H

#include "private/backend/VirtualMachineEnv.h"

#include <backend/Platform.h>

#include <utils/compiler.h>

#if __has_include(<android/surface_texture.h>)
#   include <android/surface_texture.h>
#else
struct ASurfaceTexture;
typedef struct ASurfaceTexture ASurfaceTexture;
#endif

#include <jni.h>

#include <stdint.h>
#include <math/mat3.h>

namespace filament::backend {

/**
 * Android 外部流管理器
 * 
 * ExternalStreamManagerAndroid::Stream 基本上是 SurfaceTexture 的包装器。
 * 
 * 此类确实依赖于 GLES 上下文，因为 SurfaceTexture 就是这样工作的。
 * 
 * 主要功能：
 * 1. 管理 Android SurfaceTexture 对象
 * 2. 将 SurfaceTexture 附加/分离到 GLES 上下文
 * 3. 更新纹理图像
 * 4. 获取变换矩阵
 * 
 * 使用场景：
 * - 从相机或视频流获取图像
 * - 将 Android SurfaceTexture 集成到 Filament 渲染管线
 */
class ExternalStreamManagerAndroid {
public:
    using Stream = Platform::Stream;

    /**
     * 创建外部流管理器
     * 
     * 必须在 GLES 线程上调用。
     * 
     * @return 管理器引用
     */
    static ExternalStreamManagerAndroid& create() noexcept;

    /**
     * 销毁外部流管理器
     * 
     * 必须在 GLES 线程上调用。
     * 
     * @param pExternalStreamManagerAndroid 管理器指针
     */
    static void destroy(ExternalStreamManagerAndroid* pExternalStreamManagerAndroid) noexcept;

    /**
     * 获取流
     * 
     * 从 SurfaceTexture 对象创建流。
     * 
     * @param surfaceTexture Java SurfaceTexture 对象
     * @return 流指针，失败返回 nullptr
     */
    Stream* acquire(jobject surfaceTexture) noexcept;
    
    /**
     * 释放流
     * 
     * 释放流资源。
     * 
     * @param stream 流指针
     */
    void release(Stream* stream) noexcept;

    /**
     * 将流附加到当前 GLES 上下文
     * 
     * @param stream 流指针
     * @param tname 纹理名称
     */
    void attach(Stream* stream, intptr_t tname) noexcept;

    /**
     * 从当前 GLES 上下文分离流
     * 
     * @param stream 流指针
     */
    void detach(Stream* stream) noexcept;

    /**
     * 更新纹理图像
     * 
     * 必须在 GLES 上下文线程上调用，更新流内容。
     * 
     * @param stream 流指针
     * @param timestamp 输出参数：时间戳
     */
    void updateTexImage(Stream* stream, int64_t* timestamp) noexcept;

    /**
     * 获取变换矩阵
     * 
     * 必须在 GLES 上下文线程上调用，返回变换矩阵。
     * 
     * @param stream 流指针
     * @return 变换矩阵
     */
    math::mat3f getTransformMatrix(Stream* stream) noexcept;

private:
    /**
     * 构造函数
     * 
     * 私有构造函数，只能通过 create() 创建。
     */
    ExternalStreamManagerAndroid() noexcept;
    
    /**
     * 析构函数
     * 
     * 清理资源。
     */
    ~ExternalStreamManagerAndroid() noexcept;

    VirtualMachineEnv& mVm;  // Java 虚拟机环境
    JNIEnv* mJniEnv = nullptr;  // JNI 环境（缓存）

    /**
     * EGL 流结构
     * 
     * 扩展 Platform::Stream，存储 SurfaceTexture 对象。
     */
    struct EGLStream : public Stream {
        jobject             jSurfaceTexture = nullptr;  // Java SurfaceTexture 对象
        ASurfaceTexture*    nSurfaceTexture = nullptr;  // 原生 SurfaceTexture 对象（Android 28+）
    };

    /**
     * 获取 JNI 环境
     * 
     * 必须仅从后端线程调用。
     * 快速路径：返回缓存的 JNI 环境。
     * 慢速路径：如果缓存为空，调用 getEnvironmentSlow()。
     * 
     * @return JNI 环境指针
     */
    JNIEnv* getEnvironment() noexcept {
        JNIEnv* env = mJniEnv;
        if (UTILS_UNLIKELY(!env)) {
            return getEnvironmentSlow();
        }
        return env;
    }

    /**
     * 获取 JNI 环境（慢速路径）
     * 
     * 从虚拟机获取 JNI 环境并缓存方法 ID。
     * 
     * @return JNI 环境指针
     */
    JNIEnv* getEnvironmentSlow() noexcept;

    // SurfaceTexture 类的方法 ID（缓存）
    jmethodID mSurfaceTextureClass_updateTexImage{};        // updateTexImage 方法
    jmethodID mSurfaceTextureClass_getTimestamp{};          // getTimestamp 方法
    jmethodID mSurfaceTextureClass_getTransformMatrix{};    // getTransformMatrix 方法
    jmethodID mSurfaceTextureClass_attachToGLContext{};     // attachToGLContext 方法
    jmethodID mSurfaceTextureClass_detachFromGLContext{};   // detachFromGLContext 方法
};
} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_ANDROID_EXTERNAL_STREAM_MANAGER_ANDROID_H
