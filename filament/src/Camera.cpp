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

#include <filament/Camera.h>

#include <math/mat4.h>

#include <utils/Panic.h>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <cstddef>
#include <cstdint>

namespace filament {

using namespace math;

/**
 * 设置投影矩阵（基于视场角）
 * 
 * 使用视场角（FOV）和宽高比创建透视投影矩阵。
 * 
 * @param fovInDegrees 视场角（度）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离
 * @param direction 视场角方向（水平或垂直）
 */
void Camera::setProjection(double const fovInDegrees, double const aspect,
        double const near, double const far, Fov const direction) {
    /**
     * 参数验证：确保近平面和远平面值有效
     * 
     * 前置条件：
     * - near 必须大于 0（近平面必须在相机前方）
     * - far 必须大于 near（远平面必须在近平面之后）
     * 
     * 如果条件不满足，会触发断言并输出错误信息。
     */
    FILAMENT_CHECK_PRECONDITION(near > 0 && far > near)
            << "Camera preconditions not met in setProjection(): near <= 0 or far <= near, near="
            << near << ", far=" << far;

    /**
     * 设置自定义投影矩阵
     * 
     * 调用 setCustomProjection 并传入两个投影矩阵：
     * 1. 第一个投影矩阵：用于渲染的投影矩阵（只使用 near 平面）
     * 2. 第二个投影矩阵：用于剔除的投影矩阵（使用 near 和 far 平面）
     * 
     * 这样设计的原因：
     * - 渲染投影矩阵可以无限远（far = infinity），用于某些渲染技术
     * - 剔除投影矩阵需要明确的 far 平面，用于视锥体剔除优化
     */
    setCustomProjection(
            projection(direction, fovInDegrees, aspect, near),      // 渲染投影矩阵
            projection(direction, fovInDegrees, aspect, near, far), // 剔除投影矩阵
            near, far);
}

/**
 * 设置投影矩阵（基于焦距）
 * 
 * 使用物理焦距（毫米）创建透视投影矩阵。
 * 
 * @param focalLengthInMillimeters 焦距（毫米）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离
 */
void Camera::setLensProjection(double const focalLengthInMillimeters,
        double const aspect, double const near, double const far) {
    /**
     * 参数验证：确保近平面和远平面值有效
     * 
     * 前置条件：
     * - near 必须大于 0（近平面必须在相机前方）
     * - far 必须大于 near（远平面必须在近平面之后）
     * 
     * 如果条件不满足，会触发断言并输出错误信息。
     */
    FILAMENT_CHECK_PRECONDITION(near > 0 && far > near)
        << "Camera preconditions not met in setLensProjection(): near <= 0 or far <= near, near="
        << near << ", far=" << far;

    /**
     * 设置基于物理焦距的投影矩阵
     * 
     * 使用物理焦距（毫米）而不是视场角来计算投影矩阵。
     * 这对于模拟真实相机镜头很有用。
     * 
     * 调用 setCustomProjection 并传入两个投影矩阵：
     * 1. 第一个投影矩阵：用于渲染的投影矩阵（只使用 near 平面）
     * 2. 第二个投影矩阵：用于剔除的投影矩阵（使用 near 和 far 平面）
     */
    setCustomProjection(
            projection(focalLengthInMillimeters, aspect, near),      // 渲染投影矩阵
            projection(focalLengthInMillimeters, aspect, near, far), // 剔除投影矩阵
            near, far);
}

/**
 * 计算投影矩阵的逆矩阵（单精度）
 * 
 * @param p 投影矩阵
 * @return 逆投影矩阵
 */
mat4f Camera::inverseProjection(const mat4f& p) noexcept {
    return inverse(p);
}

/**
 * 计算投影矩阵的逆矩阵（双精度）
 * 
 * @param p 投影矩阵
 * @return 逆投影矩阵
 */
mat4 Camera::inverseProjection(const mat4& p) noexcept {
    return inverse(p);
}

/**
 * 设置眼睛模型矩阵（用于立体渲染）
 * 
 * @param eyeId 眼睛ID
 * @param model 模型矩阵
 */
void Camera::setEyeModelMatrix(uint8_t const eyeId, mat4 const& model) {
    /**
     * 设置眼睛模型矩阵（用于立体渲染）
     * 
     * 在立体渲染中，每只眼睛（左眼/右眼）可能有不同的位置和朝向。
     * 此方法为指定的眼睛设置其模型矩阵。
     * 
     * @param eyeId 眼睛ID（0 = 左眼，1 = 右眼，等等）
     * @param model 眼睛的模型矩阵（定义眼睛在世界空间中的位置和方向）
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setEyeModelMatrix(eyeId, model);
}

/**
 * 设置自定义眼睛投影矩阵（用于立体渲染）
 * 
 * @param projection 投影矩阵数组
 * @param count 投影矩阵数量
 * @param projectionForCulling 用于剔除的投影矩阵
 * @param near 近平面距离
 * @param far 远平面距离
 */
void Camera::setCustomEyeProjection(mat4 const* projection, size_t const count,
        mat4 const& projectionForCulling, double const near, double const far) {
    /**
     * 设置自定义眼睛投影矩阵（用于立体渲染）
     * 
     * 在立体渲染中，每只眼睛可能有不同的投影矩阵。
     * 此方法允许为每只眼睛设置自定义投影矩阵。
     * 
     * @param projection 投影矩阵数组指针，每个元素对应一只眼睛的投影矩阵
     * @param count 投影矩阵数量（眼睛数量）
     * @param projectionForCulling 用于视锥体剔除的统一投影矩阵
     *                             所有眼睛共享此剔除投影矩阵以提高性能
     * @param near 近平面距离
     * @param far 远平面距离
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setCustomEyeProjection(projection, count, projectionForCulling, near, far);
}

/**
 * 设置投影矩阵（基于边界）
 * 
 * @param projection 投影类型（透视或正交）
 * @param left 左边界
 * @param right 右边界
 * @param bottom 底边界
 * @param top 顶边界
 * @param near 近平面距离
 * @param far 远平面距离
 */
void Camera::setProjection(Projection const projection, double const left, double const right, double const bottom,
        double const top, double const near, double const far) {
    /**
     * 设置投影矩阵（基于边界）
     * 
     * 使用指定的边界（left, right, bottom, top）和平面距离（near, far）
     * 创建投影矩阵。可以创建透视投影或正交投影。
     * 
     * @param projection 投影类型（PERSPECTIVE 或 ORTHO）
     * @param left 左边界（视图空间坐标）
     * @param right 右边界（视图空间坐标）
     * @param bottom 底边界（视图空间坐标）
     * @param top 顶边界（视图空间坐标）
     * @param near 近平面距离
     * @param far 远平面距离
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setProjection(projection, left, right, bottom, top, near, far);
}

/**
 * 设置自定义投影矩阵
 * 
 * @param projection 投影矩阵
 * @param near 近平面距离
 * @param far 远平面距离
 */
void Camera::setCustomProjection(mat4 const& projection, double const near, double const far) noexcept {
    /**
     * 设置自定义投影矩阵
     * 
     * 允许直接设置投影矩阵，而不是使用预设的投影类型。
     * 这对于特殊效果（如鱼眼镜头、非对称投影等）很有用。
     * 
     * 注意：此方法会使用相同的投影矩阵用于渲染和剔除。
     * 如果需要不同的剔除投影矩阵，请使用重载版本。
     * 
     * @param projection 投影矩阵（用于渲染和剔除）
     * @param near 近平面距离
     * @param far 远平面距离
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setCustomProjection(projection, near, far);
}

/**
 * 设置自定义投影矩阵（带剔除投影）
 * 
 * 允许分别设置用于渲染和剔除的投影矩阵。
 * 这对于优化很有用：可以使用无限远的剔除投影矩阵来减少不必要的剔除，
 * 同时使用有限远的渲染投影矩阵来获得更好的深度精度。
 * 
 * @param projection 投影矩阵（用于渲染）
 * @param projectionForCulling 用于剔除的投影矩阵（可能与渲染投影矩阵不同）
 * @param near 近平面距离
 * @param far 远平面距离
 * 
 * 实现：将调用转发到内部实现类（FCamera）
 */
void Camera::setCustomProjection(mat4 const& projection, mat4 const& projectionForCulling,
        double const near, double const far) noexcept {
    downcast(this)->setCustomProjection(projection, projectionForCulling, near, far);
}

/**
 * 设置投影缩放
 * 
 * @param scaling 缩放因子
 */
void Camera::setScaling(double2 const scaling) noexcept {
    /**
     * 设置投影缩放
     * 
     * 投影缩放用于调整投影矩阵的缩放因子。
     * 这对于实现动态分辨率、缩放效果等很有用。
     * 
     * @param scaling 缩放因子（x, y）
     *                 - x: 水平缩放
     *                 - y: 垂直缩放
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setScaling(scaling);
}

/**
 * 设置投影偏移
 * 
 * 投影偏移用于调整投影矩阵的偏移量。
     * 这对于实现镜头偏移、非对称投影等很有用。
     * 
     * @param shift 偏移量（x, y）
     *               - x: 水平偏移
     *               - y: 垂直偏移
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
void Camera::setShift(double2 const shift) noexcept {
    downcast(this)->setShift(shift);
}

/**
 * 获取投影矩阵
 * 
 * @param eyeId 眼睛ID（用于立体渲染）
 * @return 投影矩阵
 */
mat4 Camera::getProjectionMatrix(uint8_t const eyeId) const {
    /**
     * 获取投影矩阵
     * 
     * 返回指定眼睛的用户投影矩阵（用于渲染的投影矩阵）。
     * 
     * @param eyeId 眼睛ID（用于立体渲染，0 = 左眼，1 = 右眼，等等）
     * @return 投影矩阵（用于渲染）
     * 
     * 实现：从内部实现类（FCamera）获取用户投影矩阵
     */
    return downcast(this)->getUserProjectionMatrix(eyeId);
}

/**
 * 获取用于剔除的投影矩阵
 * 
 * 用于视锥体剔除的投影矩阵，可能与用户投影矩阵不同。
 * 
 * @return 剔除投影矩阵
 */
mat4 Camera::getCullingProjectionMatrix() const noexcept {
    /**
     * 获取用于剔除的投影矩阵
     * 
     * 返回用于视锥体剔除的投影矩阵。
     * 此矩阵可能与用户投影矩阵不同，用于优化剔除性能。
     * 
     * @return 剔除投影矩阵
     * 
     * 实现：从内部实现类（FCamera）获取剔除投影矩阵
     */
    return downcast(this)->getUserCullingProjectionMatrix();
}

/**
 * 获取投影缩放
 * 
 * @return 缩放因子（x, y, z, w）
 */
double4 Camera::getScaling() const noexcept {
    return downcast(this)->getScaling();
}

/**
 * 获取投影偏移
 * 
 * @return 偏移量（x, y）
 */
double2 Camera::getShift() const noexcept {
    return downcast(this)->getShift();
}

/**
 * 获取近平面距离
 * 
 * @return 近平面距离
 */
double Camera::getNear() const noexcept {
    return downcast(this)->getNear();
}

/**
 * 获取用于剔除的远平面距离
 * 
 * @return 远平面距离
 */
double Camera::getCullingFar() const noexcept {
    return downcast(this)->getCullingFar();
}

/**
 * 设置模型矩阵（双精度）
 * 
 * 设置相机在世界空间中的位置和方向。
 * 
 * @param modelMatrix 模型矩阵
 */
void Camera::setModelMatrix(const mat4& modelMatrix) noexcept {
    downcast(this)->setModelMatrix(modelMatrix);
}

/**
 * 设置模型矩阵（单精度）
 * 
 * 设置相机在世界空间中的位置和方向。
 * 
 * @param modelMatrix 模型矩阵
 */
void Camera::setModelMatrix(const mat4f& modelMatrix) noexcept {
    downcast(this)->setModelMatrix(modelMatrix);
}

/**
 * 设置相机朝向
 * 
 * 使用观察点、目标点和上向量计算并设置相机的模型矩阵。
 * 
 * @param eye 相机位置
 * @param center 观察目标点
 * @param up 上向量
 */
void Camera::lookAt(double3 const& eye, double3 const& center, double3 const& up) noexcept {
    /**
     * 设置相机朝向
     * 
     * 使用观察点、目标点和上向量计算并设置相机的模型矩阵。
     * 这是一个便捷方法，用于快速设置相机的位置和朝向。
     * 
     * 算法：
     * 1. 计算前向量：forward = normalize(center - eye)
     * 2. 计算右向量：right = normalize(cross(forward, up))
     * 3. 重新计算上向量：up = cross(right, forward)
     * 4. 构建模型矩阵（视图矩阵的逆矩阵）
     * 
     * @param eye 相机位置（世界空间坐标）
     * @param center 观察目标点（世界空间坐标）
     * @param up 上向量（世界空间坐标，用于确定相机的朝向）
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->lookAt(eye, center, up);
}

/**
 * 获取模型矩阵
 * 
 * @return 模型矩阵
 */
mat4 Camera::getModelMatrix() const noexcept {
    return downcast(this)->getModelMatrix();
}

/**
 * 获取视图矩阵
 * 
 * 视图矩阵是模型矩阵的逆矩阵。
 * 
 * @return 视图矩阵
 */
mat4 Camera::getViewMatrix() const noexcept {
    return downcast(this)->getViewMatrix();
}

/**
 * 获取相机位置
 * 
 * @return 相机在世界空间中的位置
 */
double3 Camera::getPosition() const noexcept {
    return downcast(this)->getPosition();
}

/**
 * 获取左向量
 * 
 * @return 相机的左方向向量（归一化）
 */
float3 Camera::getLeftVector() const noexcept {
    return downcast(this)->getLeftVector();
}

/**
 * 获取上向量
 * 
 * @return 相机的上方向向量（归一化）
 */
float3 Camera::getUpVector() const noexcept {
    return downcast(this)->getUpVector();
}

/**
 * 获取前向量
 * 
 * @return 相机的向前方向向量（归一化）
 */
float3 Camera::getForwardVector() const noexcept {
    return downcast(this)->getForwardVector();
}

/**
 * 获取视场角
 * 
 * @param direction 视场角方向（水平或垂直）
 * @return 视场角（度）
 */
float Camera::getFieldOfViewInDegrees(Fov const direction) const noexcept {
    return downcast(this)->getFieldOfViewInDegrees(direction);
}

/**
 * 获取视锥体
 * 
 * 用于视锥体剔除的视锥体。
 * 
 * @return 视锥体
 */
Frustum Camera::getFrustum() const noexcept {
    return downcast(this)->getCullingFrustum();
}

/**
 * 获取相机实体
 * 
 * @return 与相机关联的实体
 */
utils::Entity Camera::getEntity() const noexcept {
    return downcast(this)->getEntity();
}

/**
 * 设置曝光参数
 * 
 * 设置相机的光圈、快门速度和感光度。
 * 
 * @param aperture 光圈（f-stop）
 * @param shutterSpeed 快门速度（秒）
 * @param sensitivity 感光度（ISO）
 */
void Camera::setExposure(
        float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    /**
     * 设置曝光参数
     * 
     * 设置相机的光圈、快门速度和感光度，用于模拟真实相机的曝光。
     * 这些参数会影响渲染的亮度。
     * 
     * 曝光值（EV）计算公式：
     * EV = log2((aperture²) / (shutterSpeed * sensitivity / 100))
     * 
     * @param aperture 光圈（f-stop），例如：f/2.8, f/4.0, f/5.6 等
     *                 值越小，光圈越大，进光量越多
     * @param shutterSpeed 快门速度（秒），例如：1/60, 1/125, 1/250 等
     *                     值越小，快门越快，进光量越少
     * @param sensitivity 感光度（ISO），例如：100, 200, 400, 800 等
     *                   值越大，感光度越高，对光越敏感
     * 
     * 实现：将调用转发到内部实现类（FCamera）
     */
    downcast(this)->setExposure(aperture, shutterSpeed, sensitivity);
}

/**
 * 获取光圈
 * 
 * @return 光圈值（f-stop）
 */
float Camera::getAperture() const noexcept {
    return downcast(this)->getAperture();
}

/**
 * 获取快门速度
 * 
 * @return 快门速度（秒）
 */
float Camera::getShutterSpeed() const noexcept {
    return downcast(this)->getShutterSpeed();
}

/**
 * 获取感光度
 * 
 * @return 感光度（ISO）
 */
float Camera::getSensitivity() const noexcept {
    return downcast(this)->getSensitivity();
}

/**
 * 设置对焦距离
 * 
 * @param distance 对焦距离
 */
void Camera::setFocusDistance(float const distance) noexcept {
    downcast(this)->setFocusDistance(distance);
}

/**
 * 获取对焦距离
 * 
 * @return 对焦距离
 */
float Camera::getFocusDistance() const noexcept {
    return downcast(this)->getFocusDistance();
}

/**
 * 获取焦距
 * 
 * @return 焦距（毫米）
 */
double Camera::getFocalLength() const noexcept {
    return downcast(this)->getFocalLength();
}

/**
 * 计算有效焦距
 * 
 * 考虑对焦距离后的有效焦距。
 * 
 * @param focalLength 焦距（毫米）
 * @param focusDistance 对焦距离
 * @return 有效焦距（毫米）
 */
double Camera::computeEffectiveFocalLength(
        double const focalLength, double const focusDistance) noexcept {
    /**
     * 计算有效焦距
     * 
     * 当相机对焦到有限距离时，有效焦距会发生变化。
     * 这是因为镜头需要移动来对焦，改变了镜头到传感器的距离。
     * 
     * 公式（薄透镜方程）：
     * 1/f_effective = 1/f + 1/focusDistance
     * 
     * 其中：
     * - f: 原始焦距
     * - focusDistance: 对焦距离
     * - f_effective: 有效焦距
     * 
     * @param focalLength 原始焦距（毫米）
     * @param focusDistance 对焦距离（米）
     * @return 有效焦距（毫米）
     * 
     * 实现：调用静态方法计算有效焦距
     */
    return FCamera::computeEffectiveFocalLength(focalLength, focusDistance);
}

/**
 * 计算有效视场角
 * 
 * 考虑对焦距离后的有效视场角。
 * 
 * @param fovInDegrees 视场角（度）
 * @param focusDistance 对焦距离
 * @return 有效视场角（度）
 */
double Camera::computeEffectiveFov(double const fovInDegrees, double const focusDistance) noexcept {
    /**
     * 计算有效视场角
     * 
     * 当相机对焦到有限距离时，视场角会发生变化。
     * 这是因为对焦改变了有效焦距，从而改变了视场角。
     * 
     * 算法：
     * 1. 从视场角计算焦距
     * 2. 计算有效焦距（考虑对焦距离）
     * 3. 从有效焦距计算有效视场角
     * 
     * @param fovInDegrees 原始视场角（度）
     * @param focusDistance 对焦距离（米）
     * @return 有效视场角（度）
     * 
     * 实现：调用静态方法计算有效视场角
     */
    return FCamera::computeEffectiveFov(fovInDegrees, focusDistance);
}

/**
 * 计算投影矩阵（基于视场角）
 * 
 * @param direction 视场角方向
 * @param fovInDegrees 视场角（度）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离
 * @return 投影矩阵
 */
mat4 Camera::projection(Fov const direction, double const fovInDegrees,
        double const aspect, double const near, double const far) {
    return FCamera::projection(direction, fovInDegrees, aspect, near, far);
}

/**
 * 计算投影矩阵（基于焦距）
 * 
 * @param focalLengthInMillimeters 焦距（毫米）
 * @param aspect 宽高比
 * @param near 近平面距离
 * @param far 远平面距离
 * @return 投影矩阵
 */
mat4 Camera::projection(double const focalLengthInMillimeters,
        double const aspect, double const near, double const far) {
    return FCamera::projection(focalLengthInMillimeters, aspect, near, far);
}

} // namespace filament
