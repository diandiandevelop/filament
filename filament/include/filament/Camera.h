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

//! \file

#ifndef TNT_FILAMENT_CAMERA_H
#define TNT_FILAMENT_CAMERA_H

#include <filament/FilamentAPI.h>

#include <utils/compiler.h>

#include <math/mathfwd.h>
#include <math/vec2.h>
#include <math/vec4.h>
#include <math/mat4.h>

#include <math.h>

#include <stdint.h>
#include <stddef.h>

namespace utils {
class Entity;
} // namespace utils

namespace filament {

/**
 * Camera represents the eye(s) through which the scene is viewed.
 *
 * A Camera has a position and orientation and controls the projection and exposure parameters.
 *
 * For stereoscopic rendering, a Camera maintains two separate "eyes": Eye 0 and Eye 1. These are
 * arbitrary and don't necessarily need to correspond to "left" and "right".
 *
 * Creation and destruction
 * ========================
 *
 * In Filament, Camera is a component that must be associated with an entity. To do so,
 * use Engine::createCamera(Entity). A Camera component is destroyed using
 * Engine::destroyCameraComponent(Entity).
 *
 * ~~~~~~~~~~~{.cpp}
 *  filament::Engine* engine = filament::Engine::create();
 *
 *  utils::Entity myCameraEntity = utils::EntityManager::get().create();
 *  filament::Camera* myCamera = engine->createCamera(myCameraEntity);
 *  myCamera->setProjection(45, 16.0/9.0, 0.1, 1.0);
 *  myCamera->lookAt({0, 1.60, 1}, {0, 0, 0});
 *  engine->destroyCameraComponent(myCameraEntity);
 * ~~~~~~~~~~~
 *
 *
 * Coordinate system
 * =================
 *
 * The camera coordinate system defines the *view space*. The camera points towards its -z axis
 * and is oriented such that its top side is in the direction of +y, and its right side in the
 * direction of +x.
 *
 * @note
 * Since the *near* and *far* planes are defined by the distance from the camera,
 * their respective coordinates are -\p distance(near) and -\p distance(far).
 *
 * Clipping planes
 * ===============
 *
 * The camera defines six *clipping planes* which together create a *clipping volume*. The
 * geometry outside this volume is clipped.
 *
 * The clipping volume can either be a box or a frustum depending on which projection is used,
 * respectively Projection.ORTHO or Projection.PERSPECTIVE. The six planes are specified either
 * directly or indirectly using setProjection().
 *
 * The six planes are:
 * - left
 * - right
 * - bottom
 * - top
 * - near
 * - far
 *
 * @note
 * To increase the depth-buffer precision, the *far* clipping plane is always assumed to be at
 * infinity for rendering. That is, it is not used to clip geometry during rendering.
 * However, it is used during the culling phase (objects entirely behind the *far*
 * plane are culled).
 *
 *
 * Choosing the *near* plane distance
 * ==================================
 *
 * The *near* plane distance greatly affects the depth-buffer resolution.
 *
 * Example: Precision at 1m, 10m, 100m and 1Km for various near distances assuming a 32-bit float
 * depth-buffer:
 *
 *    near (m)  |   1 m  |   10 m  |  100 m   |  1 Km
 *  -----------:|:------:|:-------:|:--------:|:--------:
 *      0.001   | 7.2e-5 |  0.0043 |  0.4624  |  48.58
 *      0.01    | 6.9e-6 |  0.0001 |  0.0430  |   4.62
 *      0.1     | 3.6e-7 |  7.0e-5 |  0.0072  |   0.43
 *      1.0     |    0   |  3.8e-6 |  0.0007  |   0.07
 *
 *  As can be seen in the table above, the depth-buffer precision drops rapidly with the
 *  distance to the camera.
 *
 * Make sure to pick the highest *near* plane distance possible.
 *
 * On Vulkan and Metal platforms (or OpenGL platforms supporting either EXT_clip_control or
 * ARB_clip_control extensions), the depth-buffer precision is much less dependent on the *near*
 * plane value:
 *
 *    near (m)  |   1 m  |   10 m  |  100 m   |  1 Km
 *  -----------:|:------:|:-------:|:--------:|:--------:
 *      0.001   | 1.2e-7 |  9.5e-7 |  7.6e-6  |  6.1e-5
 *      0.01    | 1.2e-7 |  9.5e-7 |  7.6e-6  |  6.1e-5
 *      0.1     | 5.9e-8 |  9.5e-7 |  1.5e-5  |  1.2e-4
 *      1.0     |    0   |  9.5e-7 |  7.6e-6  |  1.8e-4
 *
 *
 * Choosing the *far* plane distance
 * =================================
 *
 * The far plane distance is always set internally to infinity for rendering, however it is used for
 * culling and shadowing calculations. It is important to keep a reasonable ratio between
 * the near and far plane distances. Typically a ratio in the range 1:100 to 1:100000 is
 * commanded. Larger values may causes rendering artifacts or trigger assertions in debug builds.
 *
 *
 * Exposure
 * ========
 *
 * The Camera is also used to set the scene's exposure, just like with a real camera. The lights
 * intensity and the Camera exposure interact to produce the final scene's brightness.
 *
 * Stereoscopic rendering
 * ======================
 *
 * The Camera's transform (as set by setModelMatrix or via TransformManager) defines a "head" space,
 * which typically corresponds to the location of the viewer's head. Each eye's transform is set
 * relative to this head space by setEyeModelMatrix.
 *
 * Each eye also maintains its own projection matrix. These can be set with setCustomEyeProjection.
 * Care must be taken to correctly set the projectionForCulling matrix, as well as its corresponding
 * near and far values. The projectionForCulling matrix must define a frustum (in head space) that
 * bounds the frustums of both eyes. Alternatively, culling may be disabled with
 * View::setFrustumCullingEnabled.
 *
 * \see Frustum, View
 */
/**
 * Camera 表示观察场景的眼睛（或双眼）
 *
 * Camera 具有位置和方向，并控制投影和曝光参数。
 *
 * 对于立体渲染，Camera 维护两个独立的"眼睛"：Eye 0 和 Eye 1。这些是
 * 任意的，不一定需要对应"左"和"右"。
 *
 * 创建和销毁
 * ========================
 *
 * 在 Filament 中，Camera 是一个组件，必须与实体关联。为此，
 * 使用 Engine::createCamera(Entity)。Camera 组件使用
 * Engine::destroyCameraComponent(Entity) 销毁。
 *
 * 坐标系
 * =================
 *
 * 相机坐标系定义了*视图空间*。相机指向其 -z 轴
 * 并定向为其顶部在 +y 方向，其右侧在
 * +x 方向。
 *
 * @note
 * 由于*近*和*远*平面由距相机的距离定义，
 * 它们各自的坐标是 -\p distance(near) 和 -\p distance(far)。
 *
 * 裁剪平面
 * ===============
 *
 * 相机定义了六个*裁剪平面*，它们共同创建一个*裁剪体积*。
 * 此体积外的几何体被裁剪。
 *
 * 裁剪体积可以是盒子或截头体，取决于使用的投影类型，
 * 分别是 Projection.ORTHO 或 Projection.PERSPECTIVE。六个平面通过
 * 直接指定或使用 setProjection() 间接指定。
 *
 * 六个平面是：
 * - left（左）
 * - right（右）
 * - bottom（底）
 * - top（顶）
 * - near（近）
 * - far（远）
 *
 * @note
 * 为了提高深度缓冲区精度，*远*裁剪平面在渲染时总是被假设为
 * 无穷远。也就是说，它在渲染期间不用于裁剪几何体。
 * 但是，它在剔除阶段使用（完全在*远*平面后面的对象被剔除）。
 *
 *
 * 选择*近*平面距离
 * ==================================
 *
 * *近*平面距离极大地影响深度缓冲区的分辨率。
 *
 * 示例：假设 32 位浮点深度缓冲区，在不同近距离下在 1m、10m、100m 和 1Km 处的精度：
 *
 * 如上表所示，深度缓冲区精度随
 * 距相机的距离而快速下降。
 *
 * 确保选择尽可能高的*近*平面距离。
 *
 * 在 Vulkan 和 Metal 平台上（或支持 EXT_clip_control 或
 * ARB_clip_control 扩展的 OpenGL 平台），深度缓冲区精度对*近*
 * 平面值的依赖要小得多。
 *
 *
 * 选择*远*平面距离
 * =================================
 *
 * 远平面距离在渲染时总是在内部设置为无穷大，但用于
 * 剔除和阴影计算。保持
 * 近平面和远平面距离之间的合理比例很重要。通常建议比例在 1:100 到 1:100000 范围内。
 * 更大的值可能导致渲染伪影或在调试构建中触发断言。
 *
 *
 * 曝光
 * ========
 *
 * Camera 也用于设置场景的曝光，就像真实相机一样。光源
 * 强度和 Camera 曝光相互作用以产生最终场景的亮度。
 *
 * \see Frustum, View
 */
class UTILS_PUBLIC Camera : public FilamentAPI {
public:
    //! Denotes the projection type used by this camera. \see setProjection
    /**
     * 表示此相机使用的投影类型
     */
    enum class Projection : int {
        PERSPECTIVE,    //!< perspective projection, objects get smaller as they are farther
        /** 透视投影，对象越远越小 */
        ORTHO           //!< orthonormal projection, preserves distances
        /** 正交投影，保持距离 */
    };

    //! Denotes a field-of-view direction. \see setProjection
    /**
     * 表示视场方向
     */
    enum class Fov : int {
        VERTICAL,       //!< the field-of-view angle is defined on the vertical axis
        /** 视场角在垂直轴上定义 */
        HORIZONTAL      //!< the field-of-view angle is defined on the horizontal axis
        /** 视场角在水平轴上定义 */
    };

    /** Returns the projection matrix from the field-of-view.
     *
     * @param fovInDegrees full field-of-view in degrees. 0 < \p fov < 180.
     * @param aspect       aspect ratio \f$ \frac{width}{height} \f$. \p aspect > 0.
     * @param near         distance in world units from the camera to the near plane. \p near > 0.
     * @param far          distance in world units from the camera to the far plane. \p far > \p near.
     * @param direction    direction of the \p fovInDegrees parameter.
     *
     * @see Fov.
     */
    /**
     * 从视场返回投影矩阵
     *
     * @param fovInDegrees 完整视场（度）。0 < \p fov < 180
     * @param aspect       宽高比 \f$ \frac{width}{height} \f$。\p aspect > 0
     * @param near         从相机到近平面的距离（世界单位）。\p near > 0
     * @param far          从相机到远平面的距离（世界单位）。\p far > \p near
     * @param direction    \p fovInDegrees 参数的方向
     *
     * @see Fov
     */
    static math::mat4 projection(Fov direction, double fovInDegrees,
            double aspect, double near, double far = INFINITY);

    /** Returns the projection matrix from the focal length.
     *
     * @param focalLengthInMillimeters lens's focal length in millimeters. \p focalLength > 0.
     * @param aspect      aspect ratio \f$ \frac{width}{height} \f$. \p aspect > 0.
     * @param near        distance in world units from the camera to the near plane. \p near > 0.
     * @param far         distance in world units from the camera to the far plane. \p far > \p near.
     */
    /**
     * 从焦距返回投影矩阵
     *
     * @param focalLengthInMillimeters 镜头焦距（毫米）。\p focalLength > 0
     * @param aspect      宽高比 \f$ \frac{width}{height} \f$。\p aspect > 0
     * @param near        从相机到近平面的距离（世界单位）。\p near > 0
     * @param far         从相机到远平面的距离（世界单位）。\p far > \p near
     */
    static math::mat4 projection(double focalLengthInMillimeters,
            double aspect, double near, double far = INFINITY);


    /** Sets the projection matrix from a frustum defined by six planes.
     *
     * @param projection    type of #Projection to use.
     *
     * @param left      distance in world units from the camera to the left plane,
     *                  at the near plane.
     *                  Precondition: \p left != \p right.
     *
     * @param right     distance in world units from the camera to the right plane,
     *                  at the near plane.
     *                  Precondition: \p left != \p right.
     *
     * @param bottom    distance in world units from the camera to the bottom plane,
     *                  at the near plane.
     *                  Precondition: \p bottom != \p top.
     *
     * @param top       distance in world units from the camera to the top plane,
     *                  at the near plane.
     *                  Precondition: \p left != \p right.
     *
     * @param near      distance in world units from the camera to the near plane. The near plane's
     *                  position in view space is z = -\p near.
     *                  Precondition: \p near > 0 for PROJECTION::PERSPECTIVE or
     *                                \p near != far for PROJECTION::ORTHO
     *
     * @param far       distance in world units from the camera to the far plane. The far plane's
     *                  position in view space is z = -\p far.
     *                  Precondition: \p far > near for PROJECTION::PERSPECTIVE or
     *                                \p far != near for PROJECTION::ORTHO
     *
     * @see Projection, Frustum
     */
    /**
     * 从由六个平面定义的截头体设置投影矩阵
     *
     * @param projection    要使用的 #Projection 类型
     *
     * @param left      从相机到左平面的距离（世界单位），
     *                  在近平面处。
     *                  前置条件：\p left != \p right
     *
     * @param right     从相机到右平面的距离（世界单位），
     *                  在近平面处。
     *                  前置条件：\p left != \p right
     *
     * @param bottom    从相机到底平面的距离（世界单位），
     *                  在近平面处。
     *                  前置条件：\p bottom != \p top
     *
     * @param top       从相机到顶平面的距离（世界单位），
     *                  在近平面处。
     *                  前置条件：\p left != \p right
     *
     * @param near      从相机到近平面的距离（世界单位）。近平面的
     *                  位置在视图空间中为 z = -\p near。
     *                  前置条件：对于 PROJECTION::PERSPECTIVE，\p near > 0 或
     *                            对于 PROJECTION::ORTHO，\p near != far
     *
     * @param far       从相机到远平面的距离（世界单位）。远平面的
     *                  位置在视图空间中为 z = -\p far。
     *                  前置条件：对于 PROJECTION::PERSPECTIVE，\p far > near 或
     *                            对于 PROJECTION::ORTHO，\p far != near
     *
     * @see Projection, Frustum
     */
    void setProjection(Projection projection,
            double left, double right,
            double bottom, double top,
            double near, double far);


    /** Utility to set the projection matrix from the field-of-view.
     *
     * @param fovInDegrees full field-of-view in degrees. 0 < \p fov < 180.
     * @param aspect    aspect ratio \f$ \frac{width}{height} \f$. \p aspect > 0.
     * @param near      distance in world units from the camera to the near plane. \p near > 0.
     * @param far       distance in world units from the camera to the far plane. \p far > \p near.
     * @param direction direction of the \p fovInDegrees parameter.
     *
     * @see Fov.
     */
    /**
     * 从视场设置投影矩阵的实用方法
     *
     * @param fovInDegrees 完整视场（度）。0 < \p fov < 180
     * @param aspect    宽高比 \f$ \frac{width}{height} \f$。\p aspect > 0
     * @param near      从相机到近平面的距离（世界单位）。\p near > 0
     * @param far       从相机到远平面的距离（世界单位）。\p far > \p near
     * @param direction \p fovInDegrees 参数的方向
     *
     * @see Fov
     */
    void setProjection(double fovInDegrees, double aspect, double near, double far,
                       Fov direction = Fov::VERTICAL);

    /** Utility to set the projection matrix from the focal length.
     *
     * @param focalLengthInMillimeters lens's focal length in millimeters. \p focalLength > 0.
     * @param aspect    aspect ratio \f$ \frac{width}{height} \f$. \p aspect > 0.
     * @param near      distance in world units from the camera to the near plane. \p near > 0.
     * @param far       distance in world units from the camera to the far plane. \p far > \p near.
     */
    /**
     * 从焦距设置投影矩阵的实用方法
     *
     * @param focalLengthInMillimeters 镜头焦距（毫米）。\p focalLength > 0
     * @param aspect    宽高比 \f$ \frac{width}{height} \f$。\p aspect > 0
     * @param near      从相机到近平面的距离（世界单位）。\p near > 0
     * @param far       从相机到远平面的距离（世界单位）。\p far > \p near
     */
    void setLensProjection(double focalLengthInMillimeters,
            double aspect, double near, double far);


    /** Sets a custom projection matrix.
     *
     * The projection matrix must define an NDC system that must match the OpenGL convention,
     * that is all 3 axis are mapped to [-1, 1].
     *
     * @param projection  custom projection matrix used for rendering and culling
     * @param near      distance in world units from the camera to the near plane.
     * @param far       distance in world units from the camera to the far plane. \p far != \p near.
     */
    /**
     * 设置自定义投影矩阵
     *
     * 投影矩阵必须定义一个符合 OpenGL 约定的 NDC 系统，
     * 即所有 3 个轴都映射到 [-1, 1]。
     *
     * @param projection  用于渲染和剔除的自定义投影矩阵
     * @param near      从相机到近平面的距离（世界单位）
     * @param far       从相机到远平面的距离（世界单位）。\p far != \p near
     */
    void setCustomProjection(math::mat4 const& projection, double near, double far) noexcept;

    /** Sets the projection matrix.
     *
     * The projection matrices must define an NDC system that must match the OpenGL convention,
     * that is all 3 axis are mapped to [-1, 1].
     *
     * @param projection  custom projection matrix used for rendering
     * @param projectionForCulling  custom projection matrix used for culling
     * @param near      distance in world units from the camera to the near plane.
     * @param far       distance in world units from the camera to the far plane. \p far != \p near.
     */
    /**
     * 设置投影矩阵
     *
     * 投影矩阵必须定义一个符合 OpenGL 约定的 NDC 系统，
     * 即所有 3 个轴都映射到 [-1, 1]。
     *
     * @param projection          用于渲染的自定义投影矩阵
     * @param projectionForCulling  用于剔除的自定义投影矩阵
     * @param near                从相机到近平面的距离（世界单位）
     * @param far                 从相机到远平面的距离（世界单位）。\p far != \p near
     */
    void setCustomProjection(math::mat4 const& projection, math::mat4 const& projectionForCulling,
            double near, double far) noexcept;

    /** Sets a custom projection matrix for each eye.
     *
     * The projectionForCulling, near, and far parameters establish a "culling frustum" which must
     * encompass anything any eye can see. All projection matrices must be set simultaneously. The
     * number of stereoscopic eyes is controlled by the stereoscopicEyeCount setting inside of
     * Engine::Config.
     *
     * @param projection an array of projection matrices, only the first config.stereoscopicEyeCount
     *                   are read
     * @param count size of the projection matrix array to set, must be
     *              >= config.stereoscopicEyeCount
     * @param projectionForCulling custom projection matrix for culling, must encompass both eyes
     * @param near distance in world units from the camera to the culling near plane. \p near > 0.
     * @param far distance in world units from the camera to the culling far plane. \p far > \p
     * near.
     * @see setCustomProjection
     * @see Engine::Config::stereoscopicEyeCount
     */
    /**
     * 为每个眼睛设置自定义投影矩阵
     *
     * projectionForCulling、near 和 far 参数建立一个"剔除截头体"，必须
     * 包含任何眼睛都能看到的任何内容。所有投影矩阵必须同时设置。
     * 立体眼睛的数量由 Engine::Config 中的 stereoscopicEyeCount 设置控制。
     *
     * @param projection          投影矩阵数组，只读取前 config.stereoscopicEyeCount 个
     * @param count              要设置的投影矩阵数组的大小，必须
     *                          >= config.stereoscopicEyeCount
     * @param projectionForCulling  用于剔除的自定义投影矩阵，必须包含两个眼睛
     * @param near               从相机到剔除近平面的距离（世界单位）。\p near > 0
     * @param far                从相机到剔除远平面的距离（世界单位）。\p far > \p near
     * @see setCustomProjection
     * @see Engine::Config::stereoscopicEyeCount
     */
    void setCustomEyeProjection(math::mat4 const* UTILS_NONNULL projection, size_t count,
            math::mat4 const& projectionForCulling, double near, double far);

    /** Sets an additional matrix that scales the projection matrix.
     *
     * This is useful to adjust the aspect ratio of the camera independent from its projection.
     * First, pass an aspect of 1.0 to setProjection. Then set the scaling with the desired aspect
     * ratio:
     *
     *     const double aspect = width / height;
     *
     *     // with Fov::HORIZONTAL passed to setProjection:
     *     camera->setScaling(double4 {1.0, aspect});
     *
     *     // with Fov::VERTICAL passed to setProjection:
     *     camera->setScaling(double4 {1.0 / aspect, 1.0});
     *
     *
     * By default, this is an identity matrix.
     *
     * @param scaling     diagonal of the 2x2 scaling matrix to be applied after the projection matrix.
     *
     * @see setProjection, setLensProjection, setCustomProjection
     */
    /**
     * 设置用于缩放投影矩阵的附加矩阵
     *
     * 这对于独立于投影调整相机的宽高比很有用。
     * 首先，将 aspect 设置为 1.0 传递给 setProjection。然后用所需的宽高比
     * 设置缩放：
     *
     * 默认情况下，这是一个单位矩阵。
     *
     * @param scaling     要在投影矩阵之后应用的 2x2 缩放矩阵的对角线
     *
     * @see setProjection, setLensProjection, setCustomProjection
     */
    void setScaling(math::double2 scaling) noexcept;

    /**
     * Sets an additional matrix that shifts the projection matrix.
     * By default, this is an identity matrix.
     *
     * @param shift     x and y translation added to the projection matrix, specified in NDC
     *                  coordinates, that is, if the translation must be specified in pixels,
     *                  shift must be scaled by 1.0 / { viewport.width, viewport.height }.
     *
     * @see setProjection, setLensProjection, setCustomProjection
     */
    /**
     * 设置用于平移投影矩阵的附加矩阵
     * 默认情况下，这是一个单位矩阵。
     *
     * @param shift     添加到投影矩阵的 x 和 y 平移，以 NDC
     *                  坐标指定，也就是说，如果必须以像素指定平移，
     *                  shift 必须按 1.0 / { viewport.width, viewport.height } 缩放。
     *
     * @see setProjection, setLensProjection, setCustomProjection
     */
    void setShift(math::double2 shift) noexcept;

    /** Returns the scaling amount used to scale the projection matrix.
     *
     * @return the diagonal of the scaling matrix applied after the projection matrix.
     *
     * @see setScaling
     */
    /**
     * 返回用于缩放投影矩阵的缩放量
     *
     * @return 在投影矩阵之后应用的缩放矩阵的对角线
     *
     * @see setScaling
     */
    math::double4 getScaling() const noexcept;

    /** Returns the shift amount used to translate the projection matrix.
     *
     * @return the 2D translation x and y offsets applied after the projection matrix.
     *
     * @see setShift
     */
    /**
     * 返回用于平移投影矩阵的平移量
     *
     * @return 在投影矩阵之后应用的 2D 平移 x 和 y 偏移
     *
     * @see setShift
     */
    math::double2 getShift() const noexcept;

    /** Returns the projection matrix used for rendering.
     *
     * The projection matrix used for rendering always has its far plane set to infinity. This
     * is why it may differ from the matrix set through setProjection() or setLensProjection().
     *
     * @param eyeId the index of the eye to return the projection matrix for, must be
     *              < config.stereoscopicEyeCount
     * @return The projection matrix used for rendering
     *
     * @see setProjection, setLensProjection, setCustomProjection, getCullingProjectionMatrix,
     * setCustomEyeProjection
     */
    /**
     * 返回用于渲染的投影矩阵
     *
     * 用于渲染的投影矩阵总是将其远平面设置为无穷大。这就是
     * 为什么它可能不同于通过 setProjection() 或 setLensProjection() 设置的矩阵。
     *
     * @param eyeId 要返回投影矩阵的眼睛索引，必须
     *              < config.stereoscopicEyeCount
     * @return 用于渲染的投影矩阵
     *
     * @see setProjection, setLensProjection, setCustomProjection, getCullingProjectionMatrix,
     * setCustomEyeProjection
     */
    math::mat4 getProjectionMatrix(uint8_t eyeId = 0) const;


    /** Returns the projection matrix used for culling (far plane is finite).
     *
     * @return The projection matrix set by setProjection or setLensProjection.
     *
     * @see setProjection, setLensProjection, getProjectionMatrix
     */
    /**
     * 返回用于剔除的投影矩阵（远平面是有限的）
     *
     * @return 由 setProjection 或 setLensProjection 设置的投影矩阵
     *
     * @see setProjection, setLensProjection, getProjectionMatrix
     */
    math::mat4 getCullingProjectionMatrix() const noexcept;


    //! Returns the frustum's near plane
    /**
     * 返回截头体的近平面
     */
    double getNear() const noexcept;

    //! Returns the frustum's far plane used for culling
    /**
     * 返回用于剔除的截头体远平面
     */
    double getCullingFar() const noexcept;

    /** Sets the camera's model matrix.
     *
     * Helper method to set the camera's entity transform component.
     * It has the same effect as calling:
     *
     * ~~~~~~~~~~~{.cpp}
     *  engine.getTransformManager().setTransform(
     *          engine.getTransformManager().getInstance(camera->getEntity()), model);
     * ~~~~~~~~~~~
     *
     * @param modelMatrix The camera position and orientation provided as a rigid transform matrix.
     *
     * @note The Camera "looks" towards its -z axis
     *
     * @warning \p model must be a rigid transform
     */
    /**
     * 设置相机的模型矩阵
     *
     * 设置相机实体变换组件的辅助方法。
     * 它与调用以下代码效果相同：
     *
     * @param modelMatrix 作为刚体变换矩阵提供的相机位置和方向
     *
     * @note 相机"看向"其 -z 轴
     *
     * @warning \p model 必须是刚体变换
     */
    void setModelMatrix(const math::mat4& modelMatrix) noexcept;
    void setModelMatrix(const math::mat4f& modelMatrix) noexcept; //!< @overload

    /** Set the position of an eye relative to this Camera (head).
     *
     * By default, both eyes' model matrices are identity matrices.
     *
     * For example, to position Eye 0 3cm leftwards and Eye 1 3cm rightwards:
     * ~~~~~~~~~~~{.cpp}
     * const mat4 leftEye  = mat4::translation(double3{-0.03, 0.0, 0.0});
     * const mat4 rightEye = mat4::translation(double3{ 0.03, 0.0, 0.0});
     * camera.setEyeModelMatrix(0, leftEye);
     * camera.setEyeModelMatrix(1, rightEye);
     * ~~~~~~~~~~~
     *
     * This method is not intended to be called every frame. Instead, to update the position of the
     * head, use Camera::setModelMatrix.
     *
     * @param eyeId the index of the eye to set, must be < config.stereoscopicEyeCount
     * @param model the model matrix for an individual eye
     */
    /**
     * 设置眼睛相对于此 Camera（头部）的位置
     *
     * 默认情况下，两个眼睛的模型矩阵都是单位矩阵。
     *
     * 此方法不打算每帧调用。相反，要更新
     * 头部的位置，请使用 Camera::setModelMatrix。
     *
     * @param eyeId 要设置的眼睛的索引，必须 < config.stereoscopicEyeCount
     * @param model 单个眼睛的模型矩阵
     */
    void setEyeModelMatrix(uint8_t eyeId, math::mat4 const& model);

    /** Sets the camera's model matrix
     *
     * @param eye       The position of the camera in world space.
     * @param center    The point in world space the camera is looking at.
     * @param up        A unit vector denoting the camera's "up" direction.
     */
    /**
     * 设置相机的模型矩阵（通过观察点和目标点）
     *
     * @param eye       相机在世界空间中的位置
     * @param center    相机在世界空间中观察的点
     * @param up        表示相机"向上"方向的单位向量
     */
    void lookAt(math::double3 const& eye,
                math::double3 const& center,
                math::double3 const& up = math::double3{0, 1, 0}) noexcept;

    /** Returns the camera's model matrix
     *
     * Helper method to return the camera's entity transform component.
     * It has the same effect as calling:
     *
     * ~~~~~~~~~~~{.cpp}
     *  engine.getTransformManager().getWorldTransform(
     *          engine.getTransformManager().getInstance(camera->getEntity()));
     * ~~~~~~~~~~~
     *
     * @return The camera's pose in world space as a rigid transform. Parent transforms, if any,
     * are taken into account.
     */
    /**
     * 返回相机的模型矩阵
     *
     * 返回相机实体变换组件的辅助方法。
     * 它与调用以下代码效果相同：
     *
     * ~~~~~~~~~~~{.cpp}
     *  engine.getTransformManager().getWorldTransform(
     *          engine.getTransformManager().getInstance(camera->getEntity()));
     * ~~~~~~~~~~~
     *
     * @return 相机在世界空间中的姿态，作为刚体变换。如果存在父变换，
     * 则会考虑在内。
     */
    math::mat4 getModelMatrix() const noexcept;

    //! Returns the camera's view matrix (inverse of the model matrix)
    /**
     * 返回相机的视图矩阵（模型矩阵的逆）
     */
    math::mat4 getViewMatrix() const noexcept;

    //! Returns the camera's position in world space
    /**
     * 返回相机在世界空间中的位置
     */
    math::double3 getPosition() const noexcept;

    //! Returns the camera's normalized left vector
    /**
     * 返回相机的归一化左向量
     */
    math::float3 getLeftVector() const noexcept;

    //! Returns the camera's normalized up vector
    /**
     * 返回相机的归一化上向量
     */
    math::float3 getUpVector() const noexcept;

    //! Returns the camera's forward vector
    /**
     * 返回相机的前向量
     */
    math::float3 getForwardVector() const noexcept;

    //! Returns the camera's field of view in degrees
    /**
     * 返回相机的视场（度）
     */
    float getFieldOfViewInDegrees(Fov direction) const noexcept;

    //! Returns the camera's culling Frustum in world space
    /**
     * 返回相机在世界空间中的剔除截头体
     */
    class Frustum getFrustum() const noexcept;

    //! Returns the entity representing this camera
    /**
     * 返回表示此相机的实体
     */
    utils::Entity getEntity() const noexcept;

    /** Sets this camera's exposure (default is f/16, 1/125s, 100 ISO)
     *
     * The exposure ultimately controls the scene's brightness, just like with a real camera.
     * The default values provide adequate exposure for a camera placed outdoors on a sunny day
     * with the sun at the zenith.
     *
     * @param aperture      Aperture in f-stops, clamped between 0.5 and 64.
     *                      A lower \p aperture value *increases* the exposure, leading to
     *                      a brighter scene. Realistic values are between 0.95 and 32.
     *
     * @param shutterSpeed  Shutter speed in seconds, clamped between 1/25,000 and 60.
     *                      A lower shutter speed increases the exposure. Realistic values are
     *                      between 1/8000 and 30.
     *
     * @param sensitivity   Sensitivity in ISO, clamped between 10 and 204,800.
     *                      A higher \p sensitivity increases the exposure. Realistic values are
     *                      between 50 and 25600.
     *
     * @note
     * With the default parameters, the scene must contain at least one Light of intensity
     * similar to the sun (e.g.: a 100,000 lux directional light).
     *
     * @see LightManager, Exposure
     */
    /**
     * 设置此相机的曝光（默认值为 f/16、1/125s、100 ISO）
     *
     * 曝光最终控制场景的亮度，就像真实相机一样。
     * 默认值为在晴朗天气、太阳位于天顶时放置在户外的相机提供
     * 充足的曝光。
     *
     * @param aperture      光圈（f 值），钳制在 0.5 到 64 之间。
     *                      较低的 \p aperture 值*增加*曝光，导致
     *                      场景更亮。实际值在 0.95 到 32 之间。
     *
     * @param shutterSpeed  快门速度（秒），钳制在 1/25,000 到 60 之间。
     *                      较低的快门速度会增加曝光。实际值
     *                      在 1/8000 到 30 之间。
     *
     * @param sensitivity   感光度（ISO），钳制在 10 到 204,800 之间。
     *                      较高的 \p sensitivity 会增加曝光。实际值
     *                      在 50 到 25600 之间。
     *
     * @note
     * 使用默认参数时，场景必须包含至少一个强度
     * 类似于太阳的光源（例如：100,000 lux 的方向光）。
     *
     * @see LightManager, Exposure
     */
    void setExposure(float aperture, float shutterSpeed, float sensitivity) noexcept;

    /** Sets this camera's exposure directly. Calling this method will set the aperture
     * to 1.0, the shutter speed to 1.2 and the sensitivity will be computed to match
     * the requested exposure (for a desired exposure of 1.0, the sensitivity will be
     * set to 100 ISO).
     *
     * This method is useful when trying to match the lighting of other engines or tools.
     * Many engines/tools use unit-less light intensities, which can be matched by setting
     * the exposure manually. This can be typically achieved by setting the exposure to
     * 1.0.
     */
    /**
     * 直接设置此相机的曝光。调用此方法将设置光圈
     * 为 1.0，快门速度为 1.2，感光度将计算为匹配
     * 请求的曝光（对于期望的曝光 1.0，感光度将
     * 设置为 100 ISO）。
     *
     * 此方法在尝试匹配其他引擎或工具的照明时很有用。
     * 许多引擎/工具使用无单位的光强度，可以通过手动设置
     * 曝光来匹配。这通常可以通过将曝光设置为
     * 1.0 来实现。
     *
     * @param exposure 曝光值
     */
    void setExposure(float exposure) noexcept {
        setExposure(1.0f, 1.2f, 100.0f * (1.0f / exposure));
    }

    //! returns this camera's aperture in f-stops
    /**
     * 返回此相机的光圈（f 值）
     */
    float getAperture() const noexcept;

    //! returns this camera's shutter speed in seconds
    /**
     * 返回此相机的快门速度（秒）
     */
    float getShutterSpeed() const noexcept;

    //! returns this camera's sensitivity in ISO
    /**
     * 返回此相机的感光度（ISO）
     */
    float getSensitivity() const noexcept;

    /** Returns the focal length in meters [m] for a 35mm camera.
     * Eye 0's projection matrix is used to compute the focal length.
     */
    /**
     * 返回 35mm 相机的焦距（米 [m]）
     * 使用 Eye 0 的投影矩阵计算焦距。
     */
    double getFocalLength() const noexcept;

    /**
     * Sets the camera focus distance. This is used by the Depth-of-field PostProcessing effect.
     * @param distance Distance from the camera to the plane of focus in world units.
     *                 Must be positive and larger than the near clipping plane.
     */
    /**
     * 设置相机对焦距离。这用于景深后处理效果。
     * @param distance 从相机到对焦平面的距离（世界单位）。
     *                 必须为正且大于近裁剪平面。
     */
    void setFocusDistance(float distance) noexcept;

    //! Returns the focus distance in world units
    /**
     * 返回对焦距离（世界单位）
     */
    float getFocusDistance() const noexcept;

    /**
     * Returns the inverse of a projection matrix.
     *
     * \param p the projection matrix to inverse
     * \returns the inverse of the projection matrix \p p
     */
    /**
     * 返回投影矩阵的逆矩阵。
     *
     * \param p 要求逆的投影矩阵
     * \returns 投影矩阵 \p p 的逆矩阵
     */
    static math::mat4 inverseProjection(const math::mat4& p) noexcept;

    /**
     * Returns the inverse of a projection matrix.
     * @see inverseProjection(const math::mat4&)
     */
    /**
     * 返回投影矩阵的逆矩阵（float 版本）。
     * @see inverseProjection(const math::mat4&)
     */
    static math::mat4f inverseProjection(const math::mat4f& p) noexcept;

    /**
     * Helper to compute the effective focal length taking into account the focus distance
     *
     * @param focalLength       focal length in any unit (e.g. [m] or [mm])
     * @param focusDistance     focus distance in same unit as focalLength
     * @return                  the effective focal length in same unit as focalLength
     */
    /**
     * 辅助函数：计算考虑对焦距离的有效焦距
     *
     * @param focalLength       焦距（任何单位，例如 [m] 或 [mm]）
     * @param focusDistance     对焦距离（与 focalLength 相同的单位）
     * @return                  有效焦距（与 focalLength 相同的单位）
     */
    static double computeEffectiveFocalLength(double focalLength, double focusDistance) noexcept;

    /**
     * Helper to compute the effective field-of-view taking into account the focus distance
     *
     * @param fovInDegrees      full field of view in degrees
     * @param focusDistance     focus distance in meters [m]
     * @return                  effective full field of view in degrees
     */
    /**
     * 辅助函数：计算考虑对焦距离的有效视场角
     *
     * @param fovInDegrees      全视场角（度）
     * @param focusDistance     对焦距离（米 [m]）
     * @return                  有效全视场角（度）
     */
    static double computeEffectiveFov(double fovInDegrees, double focusDistance) noexcept;

protected:
    // prevent heap allocation
    ~Camera() = default;
};

} // namespace filament

#endif // TNT_FILAMENT_CAMERA_H
