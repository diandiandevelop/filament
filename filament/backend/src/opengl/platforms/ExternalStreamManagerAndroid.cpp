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

#include "ExternalStreamManagerAndroid.h"

#include <private/backend/VirtualMachineEnv.h>

#include <backend/Platform.h>

#include <utils/Logger.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#if __has_include(<android/surface_texture.h>)
#   include <android/surface_texture.h>
#   include <android/surface_texture_jni.h>
#else
struct ASurfaceTexture;
typedef struct ASurfaceTexture ASurfaceTexture;
#endif

#include <jni.h>

#include <stdint.h>

#include <new>
#include <math/mat4.h>

using namespace utils;

namespace filament::backend {

using namespace backend;
using Stream = Platform::Stream;

/**
 * 创建外部流管理器
 * 
 * 必须在 GLES 线程上调用。
 * 
 * @return 管理器引用
 */
ExternalStreamManagerAndroid& ExternalStreamManagerAndroid::create() noexcept {
    return *(new(std::nothrow) ExternalStreamManagerAndroid{});
}

/**
 * 销毁外部流管理器
 * 
 * 必须在 GLES 线程上调用。
 * 
 * @param pExternalStreamManagerAndroid 管理器指针
 */
void ExternalStreamManagerAndroid::destroy(ExternalStreamManagerAndroid* pExternalStreamManagerAndroid) noexcept {
    delete pExternalStreamManagerAndroid;
}

/**
 * 构造函数
 * 
 * 初始化 Java 虚拟机环境。
 * 在 Android 28+ 上，使用 ASurfaceTexture（原生 API）。
 */
ExternalStreamManagerAndroid::ExternalStreamManagerAndroid() noexcept
        : mVm(VirtualMachineEnv::get()) {
    if (__builtin_available(android 28, *)) {
        DLOG(INFO) << "Using ASurfaceTexture";
    }
}

/**
 * 析构函数
 * 
 * 默认析构函数。
 */
ExternalStreamManagerAndroid::~ExternalStreamManagerAndroid() noexcept = default;

/**
 * 获取 JNI 环境（慢速路径）
 * 
 * 从虚拟机获取 JNI 环境并缓存 SurfaceTexture 类的方法 ID。
 * 
 * @return JNI 环境指针
 */
UTILS_NOINLINE
JNIEnv* ExternalStreamManagerAndroid::getEnvironmentSlow() noexcept {
    JNIEnv* const env = mVm.getEnvironment();
    mJniEnv = env;  // 缓存 JNI 环境
    
    // 查找 SurfaceTexture 类
    jclass SurfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
    
    // 获取并缓存方法 ID
    mSurfaceTextureClass_updateTexImage = env->GetMethodID(SurfaceTextureClass, "updateTexImage", "()V");
    mSurfaceTextureClass_attachToGLContext = env->GetMethodID(SurfaceTextureClass, "attachToGLContext", "(I)V");
    mSurfaceTextureClass_detachFromGLContext = env->GetMethodID(SurfaceTextureClass, "detachFromGLContext", "()V");
    mSurfaceTextureClass_getTimestamp = env->GetMethodID(SurfaceTextureClass, "getTimestamp", "()J");
    mSurfaceTextureClass_getTransformMatrix = env->GetMethodID(SurfaceTextureClass, "getTransformMatrix", "([F)V");
    
    return env;
}

/**
 * 获取流
 * 
 * 从 SurfaceTexture 对象创建流。
 * 
 * 注意：此函数在应用程序线程上调用（不是 GL 线程）。
 * 
 * @param surfaceTexture Java SurfaceTexture 对象
 * @return 流指针，失败返回 nullptr
 */
Stream* ExternalStreamManagerAndroid::acquire(jobject surfaceTexture) noexcept {
    // 注意：此函数在应用程序线程上调用（不是 GL 线程）
    JNIEnv* env = VirtualMachineEnv::getThreadEnvironment();
    if (!env) {
        return nullptr; // 这不应该发生
    }
    
    // 创建流对象
    EGLStream* stream = new(std::nothrow) EGLStream();
    
    // 创建全局引用（防止被垃圾回收）
    stream->jSurfaceTexture = env->NewGlobalRef(surfaceTexture);
    
    // 在 Android 28+ 上，使用原生 ASurfaceTexture API
    if (__builtin_available(android 28, *)) {
        stream->nSurfaceTexture = ASurfaceTexture_fromSurfaceTexture(env, surfaceTexture);
    }
    
    return stream;
}

void ExternalStreamManagerAndroid::release(Stream* handle) noexcept {
    EGLStream const* stream = static_cast<EGLStream*>(handle);
    if (__builtin_available(android 28, *)) {
        ASurfaceTexture_release(stream->nSurfaceTexture);
    }
    // use VirtualMachineEnv::getEnvironment() directly because we don't need to cache JNI methods here
    JNIEnv* const env = mVm.getEnvironment();
    assert_invariant(env); // we should have called attach() by now
    env->DeleteGlobalRef(stream->jSurfaceTexture);
    delete stream;
}

void ExternalStreamManagerAndroid::attach(Stream* handle, intptr_t tname) noexcept {
    EGLStream const* stream = static_cast<EGLStream*>(handle);
    if (__builtin_available(android 28, *)) {
        // associate our GL texture to the SurfaceTexture
        ASurfaceTexture* const aSurfaceTexture = stream->nSurfaceTexture;
        if (UTILS_UNLIKELY(ASurfaceTexture_attachToGLContext(aSurfaceTexture, uint32_t(tname)))) {
            // Unfortunately, before API 26 SurfaceTexture was always created in attached mode,
            // so attachToGLContext can fail. We consider this the unlikely case, because
            // this is how it should work.
            // So now we have to detach the surfacetexture from its texture
            ASurfaceTexture_detachFromGLContext(aSurfaceTexture);
            // and finally, try attaching again
            ASurfaceTexture_attachToGLContext(aSurfaceTexture, uint32_t(tname));
        }
    } else {
        JNIEnv* const env = getEnvironment();
        assert_invariant(env); // we should have called attach() by now

        // associate our GL texture to the SurfaceTexture
        jobject jSurfaceTexture = stream->jSurfaceTexture;
        env->CallVoidMethod(jSurfaceTexture, mSurfaceTextureClass_attachToGLContext, jint(tname));
        if (UTILS_UNLIKELY(env->ExceptionCheck())) {
            // Unfortunately, before API 26 SurfaceTexture was always created in attached mode,
            // so attachToGLContext can fail. We consider this the unlikely case, because
            // this is how it should work.
            env->ExceptionClear();

            // so now we have to detach the SurfaceTexture from its texture
            env->CallVoidMethod(jSurfaceTexture, mSurfaceTextureClass_detachFromGLContext);
            VirtualMachineEnv::handleException(env);

            // and finally, try attaching again
            env->CallVoidMethod(jSurfaceTexture, mSurfaceTextureClass_attachToGLContext, jint(tname));
            VirtualMachineEnv::handleException(env);
        }
    }
}

void ExternalStreamManagerAndroid::detach(Stream* handle) noexcept {
    EGLStream* stream = static_cast<EGLStream*>(handle);
    if (__builtin_available(android 28, *)) {
        ASurfaceTexture_detachFromGLContext(stream->nSurfaceTexture);
    } else {
        JNIEnv* const env = getEnvironment();
        assert_invariant(env); // we should have called attach() by now
        env->CallVoidMethod(stream->jSurfaceTexture, mSurfaceTextureClass_detachFromGLContext);
        VirtualMachineEnv::handleException(env);
    }
}

void ExternalStreamManagerAndroid::updateTexImage(Stream* handle, int64_t* timestamp) noexcept {
    EGLStream const* stream = static_cast<EGLStream*>(handle);
    if (__builtin_available(android 28, *)) {
        ASurfaceTexture_updateTexImage(stream->nSurfaceTexture);
        *timestamp = ASurfaceTexture_getTimestamp(stream->nSurfaceTexture);
    } else {
        JNIEnv* const env = getEnvironment();
        assert_invariant(env); // we should have called attach() by now
        env->CallVoidMethod(stream->jSurfaceTexture, mSurfaceTextureClass_updateTexImage);
        VirtualMachineEnv::handleException(env);
        *timestamp = env->CallLongMethod(stream->jSurfaceTexture, mSurfaceTextureClass_getTimestamp);
        VirtualMachineEnv::handleException(env);
    }
}

math::mat3f ExternalStreamManagerAndroid::getTransformMatrix(Stream* handle) noexcept {
    EGLStream const* stream = static_cast<EGLStream*>(handle);
    math::mat4f out = math::mat4f();
    if (__builtin_available(android 28, *)) {
        ASurfaceTexture_getTransformMatrix(stream->nSurfaceTexture, &out[0][0]);
    } else {
        JNIEnv* const env = getEnvironment();
        assert_invariant(env); // we should have called attach() by now
        auto jout = env->NewFloatArray(16);
        env->CallVoidMethod(stream->jSurfaceTexture, mSurfaceTextureClass_getTransformMatrix, jout);
        env->GetFloatArrayRegion(jout, 0, 16, &out[0][0]);
        env->DeleteLocalRef(jout);
        VirtualMachineEnv::handleException(env);
    }
    return math::mat3f {
        out[0][0], out[0][1], out[0][3],
        out[1][0], out[1][1], out[1][3],
        out[3][0], out[3][1], out[3][3],
    };
}

} // namespace filament::backend
