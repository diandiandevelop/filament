# Filament 视口与相机系统详细分析

## 目录
1. [概述](#概述)
2. [Camera 架构](#camera-架构)
3. [View 架构](#view-架构)
4. [视口管理](#视口管理)
5. [投影矩阵](#投影矩阵)
6. [立体渲染支持](#立体渲染支持)

---

## 概述

Filament 的视口与相机系统负责管理场景的观察视角和渲染区域。`Camera` 定义观察参数（位置、方向、投影），`View` 管理视口和渲染设置。

### 核心组件

- **Camera**：相机组件，定义观察参数和投影
- **View**：视图，管理视口和渲染设置
- **Viewport**：视口，定义渲染区域
- **Frustum**：视锥体，用于剔除

---

## Camera 架构

### 1. 类结构

```cpp
class FCamera : public Camera {
public:
    // 设置投影矩阵
    void setProjection(Projection projection,
                       double left, double right, 
                       double bottom, double top,
                       double near, double far);
    
    // 设置自定义投影矩阵
    void setCustomProjection(math::mat4 const& projection,
            math::mat4 const& projectionForCulling, 
            double near, double far);
    
    // 设置眼睛投影（立体渲染）
    void setCustomEyeProjection(math::mat4 const* projection, size_t count,
            math::mat4 const& projectionForCulling, 
            double near, double far);
    
    // 设置模型矩阵（相机位置和方向）
    void setModelMatrix(const math::mat4& modelMatrix);
    void lookAt(math::double3 const& eye, 
                math::double3 const& center, 
                math::double3 const& up);
    
    // 获取矩阵
    math::mat4 getProjectionMatrix(uint8_t eye = 0) const;
    math::mat4 getCullingProjectionMatrix() const;
    math::mat4 getViewMatrix() const;
    math::mat4 getModelMatrix() const;
    
    // 曝光设置
    void setExposure(float aperture, float shutterSpeed, float sensitivity);
    
private:
    math::mat4 mEyeProjection[CONFIG_MAX_STEREOSCOPIC_EYES];  // 每眼投影矩阵
    math::mat4 mProjectionForCulling;                          // 剔除投影矩阵
    math::mat4 mEyeFromView[CONFIG_MAX_STEREOSCOPIC_EYES];     // 眼睛变换矩阵
    
    math::double2 mScalingCS = {1.0};  // 投影缩放
    math::double2 mShiftCS = {0.0};    // 投影偏移
    
    double mNear{}, mFar{};            // 近远平面
    float mAperture = 16.0f;           // 光圈（f-stop）
    float mShutterSpeed = 1.0f / 125.0f;  // 快门速度（秒）
    float mSensitivity = 100.0f;        // 感光度（ISO）
    float mFocusDistance = 0.0f;      // 焦点距离
};
```

### 2. 投影类型

**两种投影类型：**

1. **PERSPECTIVE（透视投影）**：模拟人眼视角
2. **ORTHO（正交投影）**：无透视变形

**透视投影：**

```cpp
static math::mat4 FCamera::projection(Fov direction, double fovInDegrees,
        double aspect, double near, double far) {
    double h, w;
    if (direction == Fov::VERTICAL) {
        h = std::tan(fovInDegrees * math::d::DEG_TO_RAD * 0.5) * near;
        w = h * aspect;
    } else {
        w = std::tan(fovInDegrees * math::d::DEG_TO_RAD * 0.5) * near;
        h = w / aspect;
    }
    return projection(PERSPECTIVE, -w, w, -h, h, near, far);
}
```

**正交投影：**

```cpp
static math::mat4 FCamera::projection(Projection projection,
        double left, double right, double bottom, double top,
        double near, double far) {
    if (projection == PERSPECTIVE) {
        return math::mat4::frustum(left, right, bottom, top, near, far);
    } else {
        return math::mat4::ortho(left, right, bottom, top, near, far);
    }
}
```

### 3. 相机变换

**模型矩阵（世界空间）：**

```cpp
void FCamera::setModelMatrix(const math::mat4& modelMatrix) noexcept {
    // 模型矩阵定义相机在世界空间的位置和方向
    mModelMatrix = modelMatrix;
}
```

**视图矩阵（相机空间）：**

```cpp
math::mat4 FCamera::getViewMatrix() const noexcept {
    // 视图矩阵是模型矩阵的逆矩阵（刚体变换）
    return rigidTransformInverse(getModelMatrix());
}
```

**lookAt() 辅助函数：**

```cpp
void FCamera::lookAt(math::double3 const& eye, 
                     math::double3 const& center, 
                     math::double3 const& up) noexcept {
    // 计算朝向目标的视图矩阵
    math::double3 f = normalize(center - eye);
    math::double3 s = normalize(cross(f, up));
    math::double3 u = cross(s, f);
    
    math::mat4 view = {
        s.x, u.x, -f.x, 0,
        s.y, u.y, -f.y, 0,
        s.z, u.z, -f.z, 0,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1
    };
    
    setModelMatrix(inverse(view));
}
```

---

## View 架构

### 1. 类结构

```cpp
class FView : public View {
public:
    // 准备渲染
    void prepare(FEngine& engine, backend::DriverApi& driver,
            RootArenaScope& rootArenaScope,
            Viewport viewport, CameraInfo cameraInfo,
            math::float4 const& userTime, bool needsAlphaChannel);
    
    // 设置场景
    void setScene(FScene* scene);
    
    // 设置相机
    void setCullingCamera(FCamera* camera);
    void setViewingCamera(FCamera* camera);
    
    // 设置视口
    void setViewport(Viewport const& viewport);
    Viewport const& getViewport() const;
    
    // 剔除设置
    void setFrustumCullingEnabled(bool culling);
    bool isFrustumCullingEnabled() const;
    
private:
    FScene* mScene = nullptr;
    FCamera* mCullingCamera = nullptr;   // 剔除相机
    FCamera* mViewingCamera = nullptr;   // 渲染相机
    
    Viewport mViewport;                  // 视口
    bool mCulling = true;                // 是否启用剔除
    
    CameraInfo mCameraInfo;              // 相机信息
};
```

### 2. 相机信息（CameraInfo）

**包含渲染所需的所有相机数据：**

```cpp
struct CameraInfo {
    union {
        math::mat4f projection;                    // 单眼投影
        math::mat4f eyeProjection[CONFIG_MAX_STEREOSCOPIC_EYES];  // 双眼投影
    };
    
    math::mat4f cullingProjection;                 // 剔除投影
    math::mat4f model;                              // 模型矩阵
    math::mat4f view;                                // 视图矩阵
    math::mat4f eyeFromView[CONFIG_MAX_STEREOSCOPIC_EYES];  // 眼睛变换
    
    math::mat4 worldTransform;                      // 世界变换
    math::float4 clipTransform{1, 1, 0, 0};        // 裁剪空间变换
    
    float zn{}, zf{};                               // 近远平面距离
    float ev100{};                                  // 曝光值（EV100）
    float f{}, A{}, d{};                            // 焦距、光圈、焦点距离
};
```

**从 Camera 创建 CameraInfo：**

```cpp
CameraInfo::CameraInfo(FCamera const& camera) noexcept {
    // 1. 复制投影矩阵
    projection = camera.getProjectionMatrix();
    cullingProjection = camera.getCullingProjectionMatrix();
    
    // 2. 复制变换矩阵
    model = camera.getModelMatrix();
    view = camera.getViewMatrix();
    
    // 3. 复制眼睛变换（立体渲染）
    for (size_t i = 0; i < CONFIG_MAX_STEREOSCOPIC_EYES; i++) {
        eyeFromView[i] = camera.getEyeFromViewMatrix(i);
    }
    
    // 4. 计算曝光值
    ev100 = computeEV100(camera.getAperture(), 
                          camera.getShutterSpeed(), 
                          camera.getSensitivity());
    
    // 5. 设置其他参数
    zn = camera.getNear();
    zf = camera.getCullingFar();
    f = camera.getFocalLength();
    A = camera.getAperture();
    d = camera.getFocusDistance();
}
```

---

## 视口管理

### 1. Viewport 结构

```cpp
struct Viewport {
    int32_t left = 0;      // 左边界
    int32_t bottom = 0;    // 下边界
    uint32_t width = 1;    // 宽度
    uint32_t height = 1;   // 高度
};
```

### 2. 视口设置

**设置视口：**

```cpp
void FView::setViewport(Viewport const& viewport) noexcept {
    mViewport = viewport;
    
    // 更新渲染相关的视口设置
    // （例如：动态分辨率、后处理等）
}
```

**视口使用：**

- **渲染目标**：定义渲染到哪个区域
- **后处理**：定义后处理效果的应用区域
- **动态分辨率**：支持动态调整渲染分辨率

---

## 投影矩阵

### 1. 无限远平面

**渲染时使用无限远平面：**

```cpp
math::mat4 getProjectionMatrix(uint8_t eye = 0) const noexcept {
    // 投影矩阵使用无限远平面（提高深度缓冲精度）
    return mEyeProjection[eye];
}
```

**剔除时使用有限远平面：**

```cpp
math::mat4 getCullingProjectionMatrix() const noexcept {
    // 剔除投影矩阵使用有限远平面（用于剔除）
    return mProjectionForCulling;
}
```

### 2. 缩放和偏移

**投影缩放：**

```cpp
void FCamera::setScaling(math::double2 const scaling) noexcept {
    mScalingCS = scaling;
}

math::mat4 getProjectionMatrix(uint8_t eye) const noexcept {
    math::mat4 p = mEyeProjection[eye];
    // 应用缩放
    p[0][0] *= mScalingCS.x;
    p[1][1] *= mScalingCS.y;
    return p;
}
```

**投影偏移：**

```cpp
void FCamera::setShift(math::double2 const shift) noexcept {
    mShiftCS = shift * 2.0;  // 内部存储为 2 倍
}

math::mat4 getProjectionMatrix(uint8_t eye) const noexcept {
    math::mat4 p = mEyeProjection[eye];
    // 应用偏移
    p[2][0] += mShiftCS.x;
    p[2][1] += mShiftCS.y;
    return p;
}
```

---

## 立体渲染支持

### 1. 双眼投影

**每眼独立的投影矩阵：**

```cpp
void FCamera::setCustomEyeProjection(
        math::mat4 const* projection, size_t count,
        math::mat4 const& projectionForCulling, 
        double near, double far) {
    for (size_t i = 0; i < count; i++) {
        mEyeProjection[i] = projection[i];
    }
    mProjectionForCulling = projectionForCulling;
    mNear = near;
    mFar = far;
}
```

### 2. 眼睛变换

**从主视图空间到眼睛视图空间：**

```cpp
void FCamera::setEyeModelMatrix(uint8_t eyeId, math::mat4 const& model) {
    // 设置眼睛相对于主视图的变换
    mEyeFromView[eyeId] = model;
}
```

**在 CameraInfo 中使用：**

```cpp
CameraInfo::CameraInfo(FCamera const& camera) noexcept {
    // 复制眼睛变换
    for (size_t i = 0; i < CONFIG_MAX_STEREOSCOPIC_EYES; i++) {
        eyeFromView[i] = camera.getEyeFromViewMatrix(i);
    }
}
```

---

## 总结

Filament 的视口与相机系统通过以下设计实现了灵活的观察管理：

1. **分离设计**：Camera 定义观察参数，View 管理渲染设置
2. **双投影**：渲染使用无限远平面，剔除使用有限远平面
3. **立体渲染**：支持每眼独立的投影和变换
4. **灵活视口**：支持动态调整渲染区域
5. **曝光控制**：模拟真实相机的曝光参数

这些设计使得 Filament 能够支持从简单单眼渲染到复杂立体渲染的各种场景。

