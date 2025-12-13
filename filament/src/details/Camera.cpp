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

#include "details/Camera.h"

#include "components/TransformManager.h"

#include "details/Engine.h"

#include <filament/Exposure.h>
#include <filament/Camera.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>

#include <math/scalar.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace filament::math;
using namespace utils;

namespace filament {

// 曝光参数的有效范围
static constexpr float MIN_APERTURE = 0.5f;  // 最小光圈值（f-stop）
static constexpr float MAX_APERTURE = 64.0f;  // 最大光圈值（f-stop）
static constexpr float MIN_SHUTTER_SPEED = 1.0f / 25000.0f;  // 最小快门速度（秒，1/25000）
static constexpr float MAX_SHUTTER_SPEED = 60.0f;  // 最大快门速度（秒，60秒）
static constexpr float MIN_SENSITIVITY = 10.0f;  // 最小感光度（ISO）
static constexpr float MAX_SENSITIVITY = 204800.0f;  // 最大感光度（ISO）

/**
 * 构造函数
 * 
 * 创建相机并初始化默认投影（透视投影，单位视锥体）。
 * 
 * @param engine 引擎引用
 * @param e 实体（用于在 TransformManager 中查找变换）
 */
FCamera::FCamera(FEngine& engine, Entity const e)
        : mEngine(engine),
          mEntity(e) {
    // 设置默认透视投影（单位视锥体：-1 到 1，近平面 0.1，远平面 1.0）
    setProjection(Projection::PERSPECTIVE, -1.0, 1.0, -1.0, 1.0, 0.1, 1.0);
}

/**
 * 计算透视投影矩阵（基于视场角）
 * 
 * 根据视场角（FOV）计算透视投影矩阵。
 * 
 * 实现细节：
 * 1. 根据 FOV 方向（垂直或水平）计算视锥体的宽高
 * 2. 使用 frustum 函数创建投影矩阵
 * 3. 如果远平面为无穷大，调整矩阵以支持无限远平面
 * 
 * @param direction FOV 方向（VERTICAL 或 HORIZONTAL）
 * @param fovInDegrees 视场角（度）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离（可以是无穷大）
 * @return 投影矩阵
 */
mat4 FCamera::projection(Fov const direction, double const fovInDegrees,
        double const aspect, double const near, double const far) {
    double w;  // 视锥体宽度的一半
    double h;  // 视锥体高度的一半
    // 计算视锥体尺寸：s = tan(fov/2) * near
    double const s = std::tan(fovInDegrees * d::DEG_TO_RAD / 2.0) * near;
    if (direction == Fov::VERTICAL) {
        // 垂直 FOV：高度由 FOV 决定，宽度由宽高比决定
        w = s * aspect;
        h = s;
    } else {
        // 水平 FOV：宽度由 FOV 决定，高度由宽高比决定
        w = s;
        h = s / aspect;
    }
    // 创建透视投影矩阵
    mat4 p = mat4::frustum(-w, w, -h, h, near, far);
    if (far == std::numeric_limits<double>::infinity()) {
        // 无限远平面：调整矩阵以支持无限远平面
        // 当 far -> inf 时，p[2][2] -> -1, p[3][2] -> -2*near
        p[2][2] = -1.0f;           // lim(far->inf) = -1
        p[3][2] = -2.0f * near;    // lim(far->inf) = -2*near
    }
    return p;
}

/**
 * 计算透视投影矩阵（基于焦距）
 * 
 * 根据焦距（毫米）计算透视投影矩阵，模拟真实相机。
 * 
 * 实现细节：
 * 1. 使用 35mm 相机传感器尺寸（36x24mm）计算视锥体尺寸
 * 2. 根据焦距计算视锥体的高度和宽度
 * 3. 如果远平面为无穷大，调整矩阵以支持无限远平面
 * 
 * @param focalLengthInMillimeters 焦距（毫米）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离（可以是无穷大）
 * @return 投影矩阵
 */
mat4 FCamera::projection(double const focalLengthInMillimeters,
        double const aspect, double const near, double const far) {
    // 35mm 相机有 36x24mm 的宽画幅尺寸
    // 计算视锥体高度：h = (0.5 * near) * (传感器尺寸 / 焦距)
    double const h = (0.5 * near) * ((SENSOR_SIZE * 1000.0) / focalLengthInMillimeters);
    double const w = h * aspect;  // 宽度由宽高比决定
    // 创建透视投影矩阵
    mat4 p = mat4::frustum(-w, w, -h, h, near, far);
    if (far == std::numeric_limits<double>::infinity()) {
        // 无限远平面：调整矩阵以支持无限远平面
        p[2][2] = -1.0f;           // lim(far->inf) = -1
        p[3][2] = -2.0f * near;    // lim(far->inf) = -2*near
    }
    return p;
}

/**
 * 设置自定义投影矩阵（所有设置投影的方法都通过这里）
 * 
 * 设置自定义投影矩阵，同时设置用于渲染和剔除的投影矩阵。
 * 
 * 实现细节：
 * 1. 验证近平面和远平面不相等
 * 2. 为所有眼设置相同的投影矩阵（单眼渲染时所有眼使用相同矩阵）
 * 3. 设置用于剔除的投影矩阵
 * 4. 保存近平面和远平面距离
 * 
 * @param projection 用于渲染的投影矩阵（无限远平面）
 * @param projectionForCulling 用于剔除的投影矩阵（有限远平面）
 * @param near 近平面距离
 * @param far 远平面距离
 */
void UTILS_NOINLINE FCamera::setCustomProjection(mat4 const& projection,
        mat4 const& projectionForCulling, double const near, double const far) noexcept {

    FILAMENT_CHECK_PRECONDITION(near != far)
            << "Camera preconditions not met in setCustomProjection(): near = far = " << near;

    // 为所有眼设置相同的投影矩阵（单眼渲染时所有眼使用相同矩阵）
    for (auto& eyeProjection: mEyeProjection) {
        eyeProjection = projection;
    }
    mProjectionForCulling = projectionForCulling;  // 用于剔除的投影矩阵
    mNear = near;  // 近平面距离
    mFar = far;  // 远平面距离
}

/**
 * 设置自定义眼投影矩阵
 * 
 * 为立体渲染设置多个眼的投影矩阵。
 * 
 * 实现细节：
 * 1. 验证近平面和远平面不相等
 * 2. 验证提供的投影矩阵数量 >= 立体眼数量
 * 3. 为每个眼设置投影矩阵
 * 4. 设置用于剔除的投影矩阵
 * 5. 保存近平面和远平面距离
 * 
 * @param projection 投影矩阵数组（每个眼一个）
 * @param count 投影矩阵数量（必须 >= 立体眼数量）
 * @param projectionForCulling 用于剔除的投影矩阵
 * @param near 近平面距离
 * @param far 远平面距离
 */
void UTILS_NOINLINE FCamera::setCustomEyeProjection(mat4 const* projection, size_t const count,
        mat4 const& projectionForCulling, double const near, double const far) {
    const Engine::Config& config = mEngine.getConfig();

    FILAMENT_CHECK_PRECONDITION(near != far)
            << "Camera preconditions not met in setCustomEyeProjection(): near = far = " << near;

    FILAMENT_CHECK_PRECONDITION(count >= config.stereoscopicEyeCount)
            << "All eye projections must be supplied together, count must be >= "
               "config.stereoscopicEyeCount ("
            << config.stereoscopicEyeCount << ")";

    // 为每个眼设置投影矩阵
    for (int i = 0; i < config.stereoscopicEyeCount; i++) {
        mEyeProjection[i] = projection[i];
    }
    mProjectionForCulling = projectionForCulling;  // 用于剔除的投影矩阵
    mNear = near;  // 近平面距离
    mFar = far;  // 远平面距离
}

/**
 * 设置投影矩阵
 * 
 * 根据投影类型（透视或正交）设置投影矩阵。
 * 
 * 实现细节：
 * 1. 验证输入参数的有效性
 * 2. 根据投影类型创建相应的投影矩阵
 * 3. 对于透视投影，使用无限远平面（调整矩阵）
 * 4. 对于正交投影，使用标准正交投影矩阵
 * 5. 调用 setCustomProjection 设置最终投影矩阵
 * 
 * @param projection 投影类型（PERSPECTIVE 或 ORTHO）
 * @param left 左边界
 * @param right 右边界
 * @param bottom 底边界
 * @param top 顶边界
 * @param near 近平面距离
 * @param far 远平面距离
 */
void UTILS_NOINLINE FCamera::setProjection(Projection const projection,
        double const left, double const right,
        double const bottom, double const top,
        double const near, double const far) {

    FILAMENT_CHECK_PRECONDITION(!(left == right || bottom == top ||
            (projection == Projection::PERSPECTIVE && (near <= 0 || far <= near)) ||
            (projection == Projection::ORTHO && (near == far))))
            << "Camera preconditions not met in setProjection("
            << (projection == Projection::PERSPECTIVE ? "PERSPECTIVE" : "ORTHO") << ", "
            << left << ", " << right << ", " << bottom << ", " << top << ", " << near << ", " << far
            << ")";

    mat4 c, p;  // c = 用于剔除的投影矩阵, p = 用于渲染的投影矩阵
    switch (projection) {
        case Projection::PERSPECTIVE:
            /*
             * GL 约定中的一般透视投影矩阵：
             *
             * P =  2N/r-l    0      r+l/r-l        0
             *       0      2N/t-b   t+b/t-b        0
             *       0        0      F+N/N-F   2*F*N/N-F
             *       0        0        -1           0
             */
            c = mat4::frustum(left, right, bottom, top, near, far);  // 用于剔除的投影矩阵
            p = c;  // 初始时两者相同

            /*
             * 但我们使用无限远平面：
             *
             * P =  2N/r-l      0    r+l/r-l        0
             *       0      2N/t-b   t+b/t-b        0
             *       0       0         -1        -2*N    <-- 无限远平面
             *       0       0         -1           0
             */
            p[2][2] = -1.0f;           // lim(far->inf) = -1
            p[3][2] = -2.0f * near;    // lim(far->inf) = -2*near
            break;

        case Projection::ORTHO:
            /*
             * GL 约定中的一般正交投影矩阵：
             *
             * P =  2/r-l    0         0       - r+l/r-l
             *       0      2/t-b      0       - t+b/t-b
             *       0       0       -2/F-N    - F+N/F-N
             *       0       0         0            1
             */
            c = mat4::ortho(left, right, bottom, top, near, far);  // 用于剔除的正交投影矩阵
            p = c;  // 正交投影不需要无限远平面调整
            break;
    }
    setCustomProjection(p, c, near, far);  // 设置最终投影矩阵
}

/**
 * 获取投影矩阵（用于渲染）
 * 
 * 返回用于渲染的投影矩阵，包含缩放/偏移以及从 GL 约定到反转 DX 约定的变换。
 * 
 * 实现细节：
 * 1. 验证眼索引有效
 * 2. 创建变换矩阵，将用户裁剪空间（GL 约定）转换为虚拟裁剪空间（反转 DX 约定）
 * 3. 应用缩放和偏移
 * 4. 注意：这个变换将投影矩阵的 p33 设置为 0，从而在深度缓冲区中恢复大量精度
 * 
 * @param eye 眼索引（默认为 0）
 * @return 用于渲染的投影矩阵
 */
mat4 FCamera::getProjectionMatrix(uint8_t const eye) const noexcept {
    UTILS_UNUSED_IN_RELEASE const Engine::Config& config = mEngine.getConfig();
    assert_invariant(eye < config.stereoscopicEyeCount);
    // 将用户裁剪空间（GL 约定）转换为虚拟裁剪空间（反转 DX 约定）
    // 注意：这个数学运算最终将投影矩阵的 p33 设置为 0，这是我们在深度缓冲区中恢复大量精度的地方
    const mat4 m{ mat4::row_major_init{
            mScalingCS.x, 0.0, 0.0, mShiftCS.x,  // X 缩放和偏移
            0.0, mScalingCS.y, 0.0, mShiftCS.y,  // Y 缩放和偏移
            0.0, 0.0, -0.5, 0.5,    // GL 到反转 DX 约定（Z 变换）
            0.0, 0.0, 0.0, 1.0
    }};
    return m * mEyeProjection[eye];  // 应用变换到眼投影矩阵
}

/**
 * 获取剔除投影矩阵
 * 
 * 返回用于剔除的投影矩阵，包含缩放/偏移。
 * 剔除投影矩阵保持在 GL 约定中（不进行 Z 变换）。
 * 
 * @return 用于剔除的投影矩阵
 */
mat4 FCamera::getCullingProjectionMatrix() const noexcept {
    // 剔除投影矩阵保持在 GL 约定中（不进行 Z 变换）
    const mat4 m{ mat4::row_major_init{
            mScalingCS.x, 0.0, 0.0, mShiftCS.x,  // X 缩放和偏移
            0.0, mScalingCS.y, 0.0, mShiftCS.y,  // Y 缩放和偏移
            0.0, 0.0, 1.0, 0.0,    // Z 保持不变（GL 约定）
            0.0, 0.0, 0.0, 1.0
    }};
    return m * mProjectionForCulling;  // 应用变换到剔除投影矩阵
}

/**
 * 获取用户投影矩阵
 * 
 * 返回用户设置的用于视图的投影矩阵（未应用缩放/偏移变换）。
 * 
 * @param eyeId 眼 ID（必须 < 立体眼数量）
 * @return 用户投影矩阵常量引用
 */
const mat4& FCamera::getUserProjectionMatrix(uint8_t const eyeId) const {
    const Engine::Config& config = mEngine.getConfig();
    FILAMENT_CHECK_PRECONDITION(eyeId < config.stereoscopicEyeCount)
            << "eyeId must be < config.stereoscopicEyeCount (" << config.stereoscopicEyeCount
            << ")";
    return mEyeProjection[eyeId];  // 返回用户设置的投影矩阵（未应用变换）
}

/**
 * 设置相机的模型矩阵（单精度版本）
 * 
 * 设置相机的模型矩阵，必须是刚体变换（只包含旋转和平移，不包含缩放）。
 * 模型矩阵定义了相机在世界空间中的位置和方向。
 * 
 * @param modelMatrix 模型矩阵（单精度）
 */
void UTILS_NOINLINE FCamera::setModelMatrix(const mat4f& modelMatrix) noexcept {
    FTransformManager& transformManager = mEngine.getTransformManager();
    // 通过 TransformManager 设置实体的变换
    transformManager.setTransform(transformManager.getInstance(mEntity), modelMatrix);
}

/**
 * 设置相机的模型矩阵（双精度版本）
 * 
 * 设置相机的模型矩阵，必须是刚体变换（只包含旋转和平移，不包含缩放）。
 * 模型矩阵定义了相机在世界空间中的位置和方向。
 * 
 * @param modelMatrix 模型矩阵（双精度）
 */
void UTILS_NOINLINE FCamera::setModelMatrix(const mat4& modelMatrix) noexcept {
    FTransformManager& transformManager = mEngine.getTransformManager();
    // 通过 TransformManager 设置实体的变换
    transformManager.setTransform(transformManager.getInstance(mEntity), modelMatrix);
}

/**
 * 设置眼的模型矩阵
 * 
 * 为立体渲染设置特定眼的模型矩阵。
 * 同时计算并存储眼到视图的变换矩阵。
 * 
 * @param eyeId 眼 ID（必须 < 立体眼数量）
 * @param model 模型矩阵
 */
void UTILS_NOINLINE FCamera::setEyeModelMatrix(uint8_t const eyeId, mat4 const& model) {
    const Engine::Config& config = mEngine.getConfig();
    FILAMENT_CHECK_PRECONDITION(eyeId < config.stereoscopicEyeCount)
            << "eyeId must be < config.stereoscopicEyeCount (" << config.stereoscopicEyeCount
            << ")";
    // 计算并存储眼到视图的变换矩阵（模型矩阵的逆）
    mEyeFromView[eyeId] = inverse(model);
}

/**
 * 设置相机朝向
 * 
 * 使用 lookAt 函数设置相机的模型矩阵。
 * 相机将朝向指定中心点，使用指定的上方向向量。
 * 
 * @param eye 相机位置
 * @param center 朝向的中心点
 * @param up 上方向向量（通常为 (0, 1, 0)）
 */
void FCamera::lookAt(double3 const& eye, double3 const& center, double3 const& up) noexcept {
    FTransformManager& transformManager = mEngine.getTransformManager();
    // 使用 lookAt 函数计算模型矩阵并设置
    transformManager.setTransform(transformManager.getInstance(mEntity),
            mat4::lookAt(eye, center, up));
}

/**
 * 获取模型矩阵
 * 
 * 返回相机在世界空间中的模型矩阵（位置和方向）。
 * 
 * @return 模型矩阵
 */
mat4 FCamera::getModelMatrix() const noexcept {
    FTransformManager const& transformManager = mEngine.getTransformManager();
    // 从 TransformManager 获取实体的世界变换矩阵（高精度版本）
    return transformManager.getWorldTransformAccurate(transformManager.getInstance(mEntity));
}

/**
 * 获取视图矩阵
 * 
 * 返回视图矩阵（模型矩阵的逆）。
 * 视图矩阵将世界空间坐标转换为视图空间坐标。
 * 
 * @return 视图矩阵
 */
mat4 UTILS_NOINLINE FCamera::getViewMatrix() const noexcept {
    return inverse(getModelMatrix());  // 视图矩阵 = 模型矩阵的逆
}

/**
 * 获取剔除视锥体
 * 
 * 返回用于剔除的视锥体（世界空间）。
 * 剔除视锥体使用剔除投影矩阵（保持远平面），而不是渲染投影矩阵（无限远平面）。
 * 
 * @return 剔除视锥体
 */
Frustum FCamera::getCullingFrustum() const noexcept {
    // 为了剔除目的，我们保持远平面在原位置（使用剔除投影矩阵）
    // 视锥体 = 剔除投影矩阵 * 视图矩阵
    return Frustum(mat4f{ getCullingProjectionMatrix() * getViewMatrix() });
}

/**
 * 设置曝光参数
 * 
 * 设置相机的曝光参数（光圈、快门速度、感光度）。
 * 所有参数都会被限制在有效范围内。
 * 
 * @param aperture 光圈值（f-stop，例如 16.0 表示 f/16）
 * @param shutterSpeed 快门速度（秒，例如 1/125 表示 0.008）
 * @param sensitivity 感光度（ISO，例如 100）
 */
void FCamera::setExposure(float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    mAperture = clamp(aperture, MIN_APERTURE, MAX_APERTURE);  // 限制光圈值
    mShutterSpeed = clamp(shutterSpeed, MIN_SHUTTER_SPEED, MAX_SHUTTER_SPEED);  // 限制快门速度
    mSensitivity = clamp(sensitivity, MIN_SENSITIVITY, MAX_SENSITIVITY);  // 限制感光度
}

/**
 * 获取焦距
 * 
 * 从投影矩阵计算焦距（毫米）。
 * 
 * 公式：f = (传感器尺寸 * 投影矩阵[1][1]) * 0.5
 * 
 * @return 焦距（毫米）
 */
double FCamera::getFocalLength() const noexcept {
    auto const& monoscopicEyeProjection = mEyeProjection[0];  // 使用单眼投影矩阵
    // 从投影矩阵的 [1][1] 元素计算焦距
    return (SENSOR_SIZE * monoscopicEyeProjection[1][1]) * 0.5;
}

/**
 * 计算有效焦距
 * 
 * 根据对焦距离计算有效焦距（考虑对焦效果）。
 * 
 * 公式：f_eff = (focusDistance * focalLength) / (focusDistance - focalLength)
 * 
 * 注意：对焦距离必须大于等于焦距。
 * 
 * @param focalLength 焦距（毫米）
 * @param focusDistance 对焦距离（米）
 * @return 有效焦距（毫米）
 */
double FCamera::computeEffectiveFocalLength(double const focalLength, double focusDistance) noexcept {
    focusDistance = std::max(focalLength, focusDistance);  // 确保对焦距离 >= 焦距
    // 计算有效焦距（考虑对焦效果）
    return (focusDistance * focalLength) / (focusDistance - focalLength);
}

/**
 * 计算有效视场角
 * 
 * 根据对焦距离计算有效视场角（考虑对焦效果）。
 * 
 * 实现细节：
 * 1. 从 FOV 计算焦距
 * 2. 确保对焦距离 >= 焦距
 * 3. 根据对焦距离计算有效 FOV
 * 
 * @param fovInDegrees 视场角（度）
 * @param focusDistance 对焦距离（米）
 * @return 有效视场角（度）
 */
double FCamera::computeEffectiveFov(double const fovInDegrees, double focusDistance) noexcept {
    // 从 FOV 计算焦距：f = 0.5 * SENSOR_SIZE / tan(fov/2)
    double const f = 0.5 * SENSOR_SIZE / std::tan(fovInDegrees * d::DEG_TO_RAD * 0.5);
    focusDistance = std::max(f, focusDistance);  // 确保对焦距离 >= 焦距
    // 计算有效 FOV（考虑对焦效果）
    double const fov = 2.0 * std::atan(SENSOR_SIZE * (focusDistance - f) / (2.0 * focusDistance * f));
    return fov * d::RAD_TO_DEG;  // 转换为度
}

uint8_t FCamera::getStereoscopicEyeCount() const noexcept {
    const Engine::Config& config = mEngine.getConfig();
    return config.stereoscopicEyeCount;
}

// ------------------------------------------------------------------------------------------------

/**
 * 构造函数（从相机创建，无世界变换）
 * 
 * 创建相机信息，使用相机的模型矩阵作为世界变换。
 */
CameraInfo::CameraInfo(FCamera const& camera) noexcept
        : CameraInfo(camera, {}, camera.getModelMatrix()) {
}

/**
 * 构造函数（从相机创建，带世界变换）
 * 
 * 创建相机信息，相对于给定的世界变换。
 * 这通常用于颜色通道相机。
 * 
 * @param camera 相机
 * @param inWorldTransform 世界变换矩阵
 */
CameraInfo::CameraInfo(FCamera const& camera, mat4 const& inWorldTransform) noexcept
        : CameraInfo(camera, inWorldTransform, inWorldTransform * camera.getModelMatrix()) {
}

/**
 * 构造函数（从相机创建，相对于主相机信息）
 * 
 * 创建相机信息，相对于主相机信息。
 * 这通常用于阴影通道相机。
 * 
 * @param camera 相机
 * @param mainCameraInfo 主相机信息
 */
CameraInfo::CameraInfo(FCamera const& camera, CameraInfo const& mainCameraInfo) noexcept
        : CameraInfo(camera, mainCameraInfo.worldTransform, camera.getModelMatrix()) {
}

/**
 * 构造函数（私有，实际实现）
 * 
 * 从相机创建相机信息，包含所有必要的矩阵和参数。
 * 
 * 实现细节：
 * 1. 为每个眼设置投影矩阵和眼到视图矩阵
 * 2. 设置剔除投影矩阵
 * 3. 计算模型矩阵和视图矩阵
 * 4. 设置世界变换
 * 5. 设置近平面、远平面、曝光、焦距、光圈、对焦距离等参数
 * 
 * @param camera 相机
 * @param inWorldTransform 世界变换矩阵
 * @param modelMatrix 模型矩阵
 */
CameraInfo::CameraInfo(FCamera const& camera,
        mat4 const& inWorldTransform,
        mat4 const& modelMatrix) noexcept {
    // 为每个眼设置投影矩阵和眼到视图矩阵
    for (size_t i = 0; i < camera.getStereoscopicEyeCount(); i++) {
        eyeProjection[i]   = mat4f{ camera.getProjectionMatrix(i) };  // 眼投影矩阵
        eyeFromView[i]     = mat4f{ camera.getEyeFromViewMatrix(i) };  // 眼到视图矩阵
    }
    cullingProjection  = mat4f{ camera.getCullingProjectionMatrix() };  // 剔除投影矩阵
    model              = mat4f{ modelMatrix };  // 模型矩阵
    view               = mat4f{ inverse(modelMatrix) };  // 视图矩阵（模型矩阵的逆）
    worldTransform     = inWorldTransform;  // 世界变换
    zn                 = float(camera.getNear());  // 近平面距离
    zf                 = float(camera.getCullingFar());  // 剔除远平面距离
    ev100              = Exposure::ev100(camera);  // 曝光值（EV100）
    f                  = float(camera.getFocalLength());  // 焦距（米）
    A                  = f / camera.getAperture();  // f-number（焦距/光圈直径）
    d                  = std::max(zn, camera.getFocusDistance());  // 对焦距离（至少为近平面）
}

} // namespace filament
