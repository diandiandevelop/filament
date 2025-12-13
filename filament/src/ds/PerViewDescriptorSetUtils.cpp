/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "PerViewDescriptorSetUtils.h"

#include "details/Camera.h"
#include "details/Engine.h"

#include <private/filament/UibStructs.h>

#include <filament/Engine.h>
#include <filament/Viewport.h>

#include <backend/DriverEnums.h>

#include <math/mat4.h>
#include <math/vec4.h>

#include <array>

#include <stdint.h>

namespace filament {

using namespace backend;
using namespace math;

/**
 * 准备相机数据
 * 
 * 更新每视图统一缓冲区中的相机相关矩阵和参数。
 * 
 * @param s 每视图统一缓冲区引用
 * @param engine 引擎常量引用
 * @param camera 相机信息
 */
void PerViewDescriptorSetUtils::prepareCamera(PerViewUib& s,
        FEngine const& engine, const CameraInfo& camera) noexcept {
    mat4f const& viewFromWorld = camera.view;  // 视图矩阵（世界到视图）
    mat4f const& worldFromView = camera.model;  // 模型矩阵（视图到世界）
    mat4f const& clipFromView  = camera.projection;  // 投影矩阵（视图到裁剪）

    const mat4f viewFromClip{ inverse((mat4)camera.projection) };  // 视图到裁剪的逆矩阵（裁剪到视图）
    const mat4f worldFromClip{ highPrecisionMultiply(worldFromView, viewFromClip) };  // 世界到裁剪的逆矩阵（裁剪到世界）

    s.viewFromWorldMatrix = viewFromWorld;    // 视图矩阵（世界到视图）
    s.worldFromViewMatrix = worldFromView;    // 模型矩阵（视图到世界）
    s.clipFromViewMatrix  = clipFromView;     // 投影矩阵（视图到裁剪）
    s.viewFromClipMatrix  = viewFromClip;     // 投影逆矩阵（裁剪到视图）
    s.worldFromClipMatrix = worldFromClip;    // 投影视图逆矩阵（裁剪到世界）
    s.userWorldFromWorldMatrix = mat4f(inverse(camera.worldTransform));  // 用户世界到世界的逆矩阵
    s.clipTransform = camera.clipTransform;  // 裁剪变换
    s.cameraFar = camera.zf;  // 相机远平面
    s.oneOverFarMinusNear = 1.0f / (camera.zf - camera.zn);  // 1 / (远 - 近)
    s.nearOverFarMinusNear = camera.zn / (camera.zf - camera.zn);  // 近 / (远 - 近)

    /**
     * 处理立体渲染的每个眼睛
     */
    mat4f const& headFromWorld = camera.view;  // 头部到世界矩阵（对于单目渲染等于视图矩阵）
    Engine::Config const& config = engine.getConfig();  // 获取引擎配置
    for (int i = 0; i < config.stereoscopicEyeCount; i++) {  // 遍历每个眼睛
        mat4f const& eyeFromHead = camera.eyeFromView[i];   // 眼睛到头部矩阵（对于单目渲染为单位矩阵）
        mat4f const& clipFromEye = camera.eyeProjection[i];  // 裁剪到眼睛矩阵（眼睛投影矩阵）
        s.eyeFromViewMatrix[i] = eyeFromHead;  // 眼睛到视图矩阵
        /**
         * 计算裁剪到世界矩阵：clipFromEye * eyeFromHead * headFromWorld
         */
        // clipFromEye * eyeFromHead * headFromWorld
        s.clipFromWorldMatrix[i] = highPrecisionMultiply(  // 高精度矩阵乘法
                clipFromEye, highPrecisionMultiply(eyeFromHead, headFromWorld));  // 裁剪到世界矩阵
    }

    /**
     * 获取裁剪空间参数
     * 
     * 对于裁剪空间 [-w, w] ==> z' = -z
     * 对于裁剪空间 [0,  w] ==> z' = (w - z)/2
     */
    // with a clip-space of [-w, w] ==> z' = -z
    // with a clip-space of [0,  w] ==> z' = (w - z)/2
    s.clipControl = const_cast<FEngine&>(engine).getDriverApi().getClipSpaceParams();  // 获取裁剪空间参数
}

/**
 * 准备 LOD 偏移
 * 
 * 更新每视图统一缓冲区中的 LOD 偏移和导数缩放。
 * 
 * @param s 每视图统一缓冲区引用
 * @param bias LOD 偏移
 * @param derivativesScale 导数缩放
 */
void PerViewDescriptorSetUtils::prepareLodBias(PerViewUib& s, float bias,
        float2 derivativesScale) noexcept {
    s.lodBias = bias;  // LOD 偏移
    s.derivativesScale = derivativesScale;  // 导数缩放
}

/**
 * 准备视口
 * 
 * 更新每视图统一缓冲区中的视口相关数据。
 * 
 * @param s 每视图统一缓冲区引用
 * @param physicalViewport 物理视口
 * @param logicalViewport 逻辑视口
 */
void PerViewDescriptorSetUtils::prepareViewport(PerViewUib& s,
        backend::Viewport const& physicalViewport,
        backend::Viewport const& logicalViewport) noexcept {
    float4 const physical{ physicalViewport.left, physicalViewport.bottom,  // 物理视口（左、底、宽、高）
                           physicalViewport.width, physicalViewport.height };
    float4 const logical{ logicalViewport.left, logicalViewport.bottom,  // 逻辑视口（左、底、宽、高）
                          logicalViewport.width, logicalViewport.height };
    s.resolution = { physical.zw, 1.0f / physical.zw };  // 分辨率（宽高和倒数）
    s.logicalViewportScale = physical.zw / logical.zw;  // 逻辑视口缩放（物理尺寸 / 逻辑尺寸）
    s.logicalViewportOffset = -logical.xy / logical.zw;  // 逻辑视口偏移（负逻辑位置 / 逻辑尺寸）
}

/**
 * 准备时间
 * 
 * 更新每视图统一缓冲区中的时间数据。
 * 
 * @param s 每视图统一缓冲区引用
 * @param engine 引擎常量引用
 * @param userTime 用户时间（float4）
 */
void PerViewDescriptorSetUtils::prepareTime(PerViewUib& s,
        FEngine const& engine, float4 const& userTime) noexcept {
    const uint64_t oneSecondRemainder = engine.getEngineTime().count() % 1000000000;  // 引擎时间对 1 秒取余（纳秒）
    const float fraction = float(double(oneSecondRemainder) / 1000000000.0);  // 转换为秒的小数部分
    s.time = fraction;  // 时间（秒的小数部分）
    s.userTime = userTime;  // 用户时间
}

/**
 * 准备材质全局变量
 * 
 * 更新每视图统一缓冲区中的材质全局变量。
 * 
 * @param s 每视图统一缓冲区引用
 * @param materialGlobals 材质全局变量数组（4 个 float4）
 */
void PerViewDescriptorSetUtils::prepareMaterialGlobals(PerViewUib& s,
        std::array<float4, 4> const& materialGlobals) noexcept {
    s.custom[0] = materialGlobals[0];  // 自定义变量 0
    s.custom[1] = materialGlobals[1];  // 自定义变量 1
    s.custom[2] = materialGlobals[2];  // 自定义变量 2
    s.custom[3] = materialGlobals[3];  // 自定义变量 3
}

} // namespace filament
