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

#ifndef TNT_FILAMENT_DETAILS_CAMERA_H
#define TNT_FILAMENT_DETAILS_CAMERA_H

#include <filament/Camera.h>

#include "downcast.h"

#include <filament/Frustum.h>

#include <private/filament/EngineEnums.h>

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/Panic.h>

#include <math/mat4.h>
#include <math/scalar.h>

namespace filament {

class FEngine;

/**
 * 相机实现类
 * 
 * 用于轻松计算投影矩阵和视图矩阵。
 * 相机定义了观察者的位置、方向和视野。
 * 
 * 实现细节：
 * - 支持多种投影类型（透视、正交等）
 * - 支持立体渲染（多眼）
 * - 支持缩放和偏移
 * - 维护用于渲染和剔除的不同投影矩阵
 */
/*
 * FCamera is used to easily compute the projection and view matrices
 */
class FCamera : public Camera {
public:
    /**
     * 传感器大小
     * 
     * 35mm 相机有 36x24mm 的宽画幅尺寸。
     */
    // a 35mm camera has a 36x24mm wide frame size
    static constexpr const float SENSOR_SIZE = 0.024f;    // 24mm

    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param e 实体
     */
    FCamera(FEngine& engine, utils::Entity e);

    /**
     * 终止相机
     * 
     * 清理资源（当前为空操作）。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine&) noexcept { }


    /**
     * 设置投影矩阵
     * 
     * 设置投影矩阵（视图和剔除）。
     * 视图矩阵具有无限远平面。
     * 
     * @param projection 投影类型
     * @param left 左边界
     * @param right 右边界
     * @param bottom 底边界
     * @param top 顶边界
     * @param near 近平面
     * @param far 远平面
     */
    // Sets the projection matrices (viewing and culling). The viewing matrice has infinite far.
    void setProjection(Projection projection,
                       double left, double right, double bottom, double top,
                       double near, double far);

    /**
     * 设置自定义投影矩阵
     * 
     * 设置自定义投影矩阵（同时设置视图和剔除投影）。
     * 
     * @param projection 投影矩阵
     * @param projectionForCulling 用于剔除的投影矩阵
     * @param near 近平面
     * @param far 远平面
     */
    // Sets custom projection matrices (sets both the viewing and culling projections).
    void setCustomProjection(math::mat4 const& projection,
            math::mat4 const& projectionForCulling, double near, double far) noexcept;

    /**
     * 设置自定义投影矩阵（重载）
     * 
     * 使用相同的矩阵作为视图和剔除投影。
     * 
     * @param projection 投影矩阵
     * @param near 近平面
     * @param far 远平面
     */
    inline void setCustomProjection(math::mat4 const& projection,
            double const near, double const far) noexcept {
        setCustomProjection(projection, projection, near, far);  // 使用相同矩阵
    }

    /**
     * 设置自定义眼投影矩阵
     * 
     * 为立体渲染设置多个眼的投影矩阵。
     * 
     * @param projection 投影矩阵数组
     * @param count 眼数量
     * @param projectionForCulling 用于剔除的投影矩阵
     * @param near 近平面
     * @param far 远平面
     */
    void setCustomEyeProjection(math::mat4 const* projection, size_t count,
            math::mat4 const& projectionForCulling, double near, double far);


    /**
     * 设置缩放
     * 
     * @param scaling 缩放值（双精度）
     */
    void setScaling(math::double2 const scaling) noexcept { mScalingCS = scaling; }

    /**
     * 获取缩放
     * 
     * @return 缩放值（四元组，后两个分量为 1.0）
     */
    math::double4 getScaling() const noexcept { return math::double4{ mScalingCS, 1.0, 1.0 }; }

    /**
     * 设置偏移
     * 
     * @param shift 偏移值（双精度，内部会乘以 2.0）
     */
    void setShift(math::double2 const shift) noexcept { mShiftCS = shift * 2.0; }

    /**
     * 获取偏移
     * 
     * @return 偏移值（双精度，内部会除以 2.0）
     */
    math::double2 getShift() const noexcept { return mShiftCS * 0.5; }

    /**
     * 获取投影矩阵
     * 
     * 用于渲染的投影矩阵，包含缩放/偏移以及着色器可能需要的其他变换。
     * 
     * @param eye 眼索引（默认为 0）
     * @return 投影矩阵
     */
    // viewing the projection matrix to be used for rendering, contains scaling/shift and possibly
    // other transforms needed by the shaders
    math::mat4 getProjectionMatrix(uint8_t eye = 0) const noexcept;

    /**
     * 获取剔除投影矩阵
     * 
     * 用于剔除的投影矩阵，包含缩放/偏移。
     * 
     * @return 剔除投影矩阵
     */
    // culling the projection matrix to be used for culling, contains scaling/shift
    math::mat4 getCullingProjectionMatrix() const noexcept;

    /**
     * 获取眼到视图矩阵
     * 
     * @param eye 眼索引
     * @return 眼到视图矩阵
     */
    math::mat4 getEyeFromViewMatrix(uint8_t const eye) const noexcept { return mEyeFromView[eye]; }

    /**
     * 获取用户投影矩阵
     * 
     * 用户设置的用于视图的投影矩阵。
     * 
     * @param eyeId 眼 ID
     * @return 用户投影矩阵常量引用
     */
    // viewing projection matrix set by the user
    const math::mat4& getUserProjectionMatrix(uint8_t eyeId) const;

    /**
     * 获取用户剔除投影矩阵
     * 
     * 用户设置的用于剔除的投影矩阵。
     * 
     * @return 用户剔除投影矩阵
     */
    // culling projection matrix set by the user
    math::mat4 getUserCullingProjectionMatrix() const noexcept { return mProjectionForCulling; }

    /**
     * 获取近平面
     * 
     * @return 近平面距离
     */
    double getNear() const noexcept { return mNear; }

    /**
     * 获取剔除远平面
     * 
     * @return 剔除远平面距离
     */
    double getCullingFar() const noexcept { return mFar; }

    /**
     * 设置相机的模型矩阵（必须是刚体变换）
     * 
     * @param modelMatrix 模型矩阵（双精度）
     */
    // sets the camera's model matrix (must be a rigid transform)
    void setModelMatrix(const math::mat4& modelMatrix) noexcept;
    
    /**
     * 设置相机的模型矩阵（单精度版本）
     * 
     * @param modelMatrix 模型矩阵（单精度）
     */
    void setModelMatrix(const math::mat4f& modelMatrix) noexcept;
    
    /**
     * 设置眼的模型矩阵
     * 
     * @param eyeId 眼 ID
     * @param model 模型矩阵
     */
    void setEyeModelMatrix(uint8_t eyeId, math::mat4 const& model);

    /**
     * 设置相机的模型矩阵（通过观察点）
     * 
     * 根据观察位置、目标位置和上向量计算并设置相机的模型矩阵。
     * 
     * @param eye 观察位置（世界空间）
     * @param center 目标位置（世界空间）
     * @param up 上向量（世界空间，通常为 (0, 1, 0)）
     */
    // sets the camera's model matrix
    void lookAt(math::double3 const& eye, math::double3 const& center, math::double3 const& up) noexcept;

    /**
     * 获取模型矩阵
     * 
     * 返回相机的模型矩阵（从世界空间到相机空间）。
     * 
     * @return 模型矩阵（双精度）
     */
    // returns the model matrix
    math::mat4 getModelMatrix() const noexcept;

    /**
     * 获取视图矩阵
     * 
     * 返回相机的视图矩阵（模型矩阵的逆矩阵，从相机空间到世界空间）。
     * 
     * @return 视图矩阵（双精度）
     */
    // returns the view matrix (inverse of the model matrix)
    math::mat4 getViewMatrix() const noexcept;

    /**
     * 刚体变换的逆矩阵
     * 
     * 计算刚体变换矩阵的逆矩阵。
     * 刚体变换的逆可以通过转置旋转部分并调整平移部分来计算：
     *  | R T |^-1    | Rt -Rt*T |
     *  | 0 1 |     = |  0   1   |
     * 
     * @tparam T 矩阵元素类型
     * @param v 刚体变换矩阵
     * @return 逆矩阵
     */
    template<typename T>
    static math::details::TMat44<T> rigidTransformInverse(math::details::TMat44<T> const& v) noexcept {
        /**
         * 刚体变换的逆可以通过转置来计算
         *  | R T |^-1    | Rt -Rt*T |
         *  | 0 1 |     = |  0   1   |
         */
        const auto rt(transpose(v.upperLeft()));  // 转置旋转部分
        const auto t(rt * v[3].xyz);  // 计算新的平移部分
        return { rt, -t };  // 返回逆矩阵
    }

    /**
     * 获取相机位置
     * 
     * 返回相机在世界空间中的位置。
     * 
     * @return 相机位置（双精度向量）
     */
    math::double3 getPosition() const noexcept {
        return getModelMatrix()[3].xyz;  // 模型矩阵的第 4 列包含位置
    }

    /**
     * 获取左向量
     * 
     * 返回相机坐标系中的左方向向量（归一化）。
     * 
     * @return 左向量（单精度向量）
     */
    math::float3 getLeftVector() const noexcept {
        return normalize(getModelMatrix()[0].xyz);  // 模型矩阵的第 1 列
    }

    /**
     * 获取上向量
     * 
     * 返回相机坐标系中的上方向向量（归一化）。
     * 
     * @return 上向量（单精度向量）
     */
    math::float3 getUpVector() const noexcept {
        return normalize(getModelMatrix()[1].xyz);  // 模型矩阵的第 2 列
    }

    /**
     * 获取前向量
     * 
     * 返回相机坐标系中的前方向向量（归一化）。
     * 相机朝向 -z 方向。
     * 
     * @return 前向量（单精度向量）
     */
    math::float3 getForwardVector() const noexcept {
        /**
         * 相机朝向 -z 方向
         */
        return normalize(-getModelMatrix()[2].xyz);  // 模型矩阵的第 3 列（取反）
    }

    float getFieldOfView(Fov const direction) const noexcept {
        // note: this is meaningless for an orthographic projection
        auto const& p = getProjectionMatrix();
        switch (direction) {
            case Fov::VERTICAL:
                return std::abs(2.0f * std::atan(1.0f / float(p[1][1])));
            case Fov::HORIZONTAL:
                return std::abs(2.0f * std::atan(1.0f / float(p[0][0])));
        }
    }

    float getFieldOfViewInDegrees(Fov const direction) const noexcept {
        return getFieldOfView(direction) * math::f::RAD_TO_DEG;
    }

    // Returns the camera's culling Frustum in world space
    Frustum getCullingFrustum() const noexcept;

    // sets this camera's exposure (default is f/16, 1/125s, 100 ISO)
    void setExposure(float aperture, float shutterSpeed, float sensitivity) noexcept;

    // returns this camera's aperture in f-stops
    float getAperture() const noexcept {
        return mAperture;
    }

    // returns this camera's shutter speed in seconds
    float getShutterSpeed() const noexcept {
        return mShutterSpeed;
    }

    // returns this camera's sensitivity in ISO
    float getSensitivity() const noexcept {
        return mSensitivity;
    }

    void setFocusDistance(float const distance) noexcept {
        mFocusDistance = distance;
    }

    float getFocusDistance() const noexcept {
        return mFocusDistance;
    }

    double getFocalLength() const noexcept;

    static double computeEffectiveFocalLength(double focalLength, double focusDistance) noexcept;

    static double computeEffectiveFov(double fovInDegrees, double focusDistance) noexcept;

    uint8_t getStereoscopicEyeCount() const noexcept;

    utils::Entity getEntity() const noexcept {
        return mEntity;
    }

    static math::mat4 projection(Fov direction, double fovInDegrees,
            double aspect, double near, double far);

    static math::mat4 projection(double focalLengthInMillimeters,
            double aspect, double near, double far);

private:
    FEngine& mEngine;
    utils::Entity mEntity;

    // For monoscopic cameras, mEyeProjection[0] == mEyeProjection[1].
    math::mat4 mEyeProjection[CONFIG_MAX_STEREOSCOPIC_EYES]; // projection matrix per eye (infinite far)
    math::mat4 mProjectionForCulling;                        // projection matrix (with far plane)
    math::mat4 mEyeFromView[CONFIG_MAX_STEREOSCOPIC_EYES];   // transforms from the main view (head)
                                                             // space to each eye's unique view space
    math::double2 mScalingCS = {1.0};  // additional scaling applied to projection
    math::double2 mShiftCS = {0.0};    // additional translation applied to projection

    double mNear{};
    double mFar{};
    // exposure settings
    float mAperture = 16.0f;
    float mShutterSpeed = 1.0f / 125.0f;
    float mSensitivity = 100.0f;
    float mFocusDistance = 0.0f;
};

struct CameraInfo {
    CameraInfo() noexcept {}

    // Creates a CameraInfo relative to inWorldTransform (i.e. it's model matrix is
    // transformed by inWorldTransform and inWorldTransform is recorded).
    // This is typically used for the color pass camera.
    CameraInfo(FCamera const& camera, math::mat4 const& inWorldTransform) noexcept;

    // Creates a CameraInfo from a camera that is relative to mainCameraInfo.
    // This is typically used for the shadow pass cameras.
    CameraInfo(FCamera const& camera, CameraInfo const& mainCameraInfo) noexcept;

    // Creates a CameraInfo from the FCamera
    explicit CameraInfo(FCamera const& camera) noexcept;

    union {
        // projection matrix for drawing (infinite zfar)
        // for monoscopic rendering
        // equivalent to eyeProjection[0], but aliased here for convenience
        math::mat4f projection;

        // for stereo rendering, one matrix per eye
        math::mat4f eyeProjection[CONFIG_MAX_STEREOSCOPIC_EYES] = {};
    };

    math::mat4f cullingProjection;                          // projection matrix for culling
    math::mat4f model;                                      // camera model matrix
    math::mat4f view;                                       // camera view matrix (inverse(model))
    math::mat4f eyeFromView[CONFIG_MAX_STEREOSCOPIC_EYES];  // eye view matrix (only for stereoscopic)
    math::mat4 worldTransform;                              // world transform (already applied
                                                            // to model and view)
    math::float4 clipTransform{1, 1, 0, 0};  // clip-space transform, only for VERTEX_DOMAIN_DEVICE
    float zn{};                              // distance (positive) to the near plane
    float zf{};                              // distance (positive) to the far plane
    float ev100{};                           // exposure
    float f{};                               // focal length [m]
    float A{};                               // f-number or f / aperture diameter [m]
    float d{};                               // focus distance [m]
    math::float3 const& getPosition() const noexcept { return model[3].xyz; }
    math::float3 getForwardVector() const noexcept { return normalize(-model[2].xyz); }
    math::mat4 getUserViewMatrix() const noexcept { return view * worldTransform; }

private:
    CameraInfo(FCamera const& camera,
            math::mat4 const& inWorldTransform,
            math::mat4 const& modelMatrix) noexcept;
};

FILAMENT_DOWNCAST(Camera)

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_CAMERA_H
