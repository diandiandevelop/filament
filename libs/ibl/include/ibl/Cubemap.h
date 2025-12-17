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

#ifndef IBL_CUBEMAP_H
#define IBL_CUBEMAP_H

#include <ibl/Image.h>

#include <utils/compiler.h>

#include <math/vec4.h>
#include <math/vec3.h>
#include <math/vec2.h>

#include <algorithm>

namespace filament {
namespace ibl {

/**
 * Cubemap - 立方体贴图类
 * 
 * 通用的立方体贴图类，处理立方体贴图6个面的读写操作。
 * 支持无缝三线性过滤。
 * 
 * 注意：此类不拥有面的数据，它只是6个图像的"视图"。
 * 
 * @see CubemapUtils
 */
class UTILS_PUBLIC Cubemap {
public:

    /**
     * 构造函数
     * 
     * 使用给定尺寸初始化立方体贴图，但不设置任何面，也不分配内存。
     * 
     * 通常使用CubemapUtils创建Cubemap。
     * 
     * @param dim 立方体贴图的尺寸（每个面的宽度和高度）
     * @see CubemapUtils
     */
    explicit Cubemap(size_t dim);

    /**
     * 移动构造函数（默认）
     */
    Cubemap(Cubemap&&) = default;
    
    /**
     * 移动赋值运算符（默认）
     */
    Cubemap& operator=(Cubemap&&) = default;

    /**
     * 析构函数
     */
    ~Cubemap();

    /**
     * 立方体贴图面枚举
     * 
     * 立方体贴图的6个面，按照标准OpenGL立方体贴图布局：
     * 
     *            +----+
     *            | PY |  (上/正Y)
     *     +----+----+----+----+
     *     | NX | PZ | PX | NZ |  (左/负X, 后/正Z, 右/正X, 前/负Z)
     *     +----+----+----+----+
     *            | NY |  (下/负Y)
     *            +----+
     */
    enum class Face : uint8_t {
        PX = 0,     // 右面（正X）            +----+
        NX,         // 左面（负X）            | PY |
        PY,         // 上面（正Y）      +----+----+----+----+
        NY,         // 下面（负Y）      | NX | PZ | PX | NZ |
        PZ,         // 后面（正Z）      +----+----+----+----+
        NZ          // 前面（负Z）            | NY |
                    //                        +----+
    };

    /**
     * 纹理元素类型（每个像素为float3，RGB格式）
     */
    using Texel = filament::math::float3;


    /**
     * 重置立方体贴图尺寸
     * 
     * 释放所有图像并重置立方体贴图尺寸。
     * 
     * @param dim 新的立方体贴图尺寸
     */
    void resetDimensions(size_t dim);

    /**
     * 为指定面分配图像
     * 
     * 将图像分配给立方体贴图的指定面（不复制数据，只是引用）。
     * 
     * @param face 立方体贴图面
     * @param image 图像对象
     */
    void setImageForFace(Face face, const Image& image);

    /**
     * 获取指定面的图像（常量版本）
     * 
     * @param face 立方体贴图面
     * @return 图像对象的常量引用
     */
    inline const Image& getImageForFace(Face face) const;

    /**
     * 获取指定面的图像（非常量版本）
     * 
     * @param face 立方体贴图面
     * @return 图像对象的引用
     */
    inline Image& getImageForFace(Face face);

    /**
     * 计算像素的中心坐标
     * 
     * 将像素坐标转换为像素中心坐标（添加0.5偏移）。
     * 
     * @param x 像素X坐标
     * @param y 像素Y坐标
     * @return 像素中心坐标（float2）
     */
    static inline filament::math::float2 center(size_t x, size_t y);

    /**
     * 从面和像素中心位置计算方向向量
     * 
     * @param face 立方体贴图面
     * @param x 像素X坐标
     * @param y 像素Y坐标
     * @return 归一化的方向向量（float3）
     */
    inline filament::math::float3 getDirectionFor(Face face, size_t x, size_t y) const;

    /**
     * 从面和像素位置计算方向向量
     * 
     * @param face 立方体贴图面
     * @param x 像素X坐标（浮点数）
     * @param y 像素Y坐标（浮点数）
     * @return 归一化的方向向量（float3）
     */
    inline filament::math::float3 getDirectionFor(Face face, float x, float y) const;

    /**
     * 使用最近邻过滤在给定方向采样立方体贴图
     * 
     * @param direction 归一化的方向向量
     * @return 采样到的纹理元素（常量引用）
     */
    inline Texel const& sampleAt(const filament::math::float3& direction) const;

    /**
     * 使用双线性过滤在给定方向采样立方体贴图
     * 
     * @param direction 归一化的方向向量
     * @return 采样到的纹理元素
     */
    inline Texel filterAt(const filament::math::float3& direction) const;

    /**
     * 使用双线性过滤在图像指定位置采样
     * 
     * @param image 图像对象
     * @param x 采样位置X坐标（浮点数）
     * @param y 采样位置Y坐标（浮点数）
     * @return 采样到的纹理元素
     */
    static Texel filterAt(const Image& image, float x, float y);
    
    /**
     * 在像素中心位置使用双线性过滤采样图像
     * 
     * @param image 图像对象
     * @param x0 像素X坐标
     * @param y0 像素Y坐标
     * @return 采样到的纹理元素（4个相邻像素的平均值）
     */
    static Texel filterAtCenter(const Image& image, size_t x0, size_t y0);

    /**
     * 在两个立方体贴图的给定方向采样并线性插值
     * 
     * 执行三线性过滤：在两个Mipmap级别之间进行线性插值。
     * 
     * @param c0 第一个立方体贴图（较低Mipmap级别）
     * @param c1 第二个立方体贴图（较高Mipmap级别）
     * @param lerp 插值因子（0.0 = c0, 1.0 = c1）
     * @param direction 归一化的方向向量
     * @return 插值后的纹理元素
     */
    static Texel trilinearFilterAt(const Cubemap& c0, const Cubemap& c1, float lerp,
            const filament::math::float3& direction);

    /**
     * 从给定地址读取纹理元素（内联函数）
     * 
     * @param data 数据指针
     * @return 纹理元素的常量引用
     */
    inline static const Texel& sampleAt(void const* data) {
        return *static_cast<Texel const*>(data);
    }

    /**
     * 在给定地址写入纹理元素（内联函数）
     * 
     * @param data 数据指针
     * @param texel 要写入的纹理元素
     */
    inline static void writeAt(void* data, const Texel& texel) {
        *static_cast<Texel*>(data) = texel;
    }

    /**
     * 获取立方体贴图的尺寸
     * 
     * @return 立方体贴图的尺寸（像素数）
     */
    size_t getDimensions() const;

    /**
     * 准备立方体贴图以实现无缝访问
     * 
     * 在面的边缘复制相邻面的数据，使立方体贴图可以在边界处无缝采样。
     * 
     * @warning 立方体贴图的所有面必须由同一个Image支持，并且必须已经间隔2行/列。
     */
    void makeSeamless();

    /**
     * 立方体贴图地址结构体
     * 
     * 包含方向向量对应的面和纹理坐标。
     */
    struct Address {
        Face face;      // 立方体贴图面
        float s = 0;    // 纹理坐标S（U坐标，范围[0,1]）
        float t = 0;    // 纹理坐标T（V坐标，范围[0,1]）
    };

    /**
     * 获取给定方向对应的面和纹理坐标
     * 
     * 将3D方向向量转换为立方体贴图的面和2D纹理坐标。
     * 
     * @param direction 归一化的方向向量
     * @return 立方体贴图地址（包含面和纹理坐标）
     */
    static Address getAddressFor(const filament::math::float3& direction);

private:
    size_t mDimensions = 0;
    float mScale = 1;
    float mUpperBound = 0;
    Image mFaces[6];
};

// ------------------------------------------------------------------------------------------------

inline const Image& Cubemap::getImageForFace(Face face) const {
    return mFaces[int(face)];
}

inline Image& Cubemap::getImageForFace(Face face) {
    return mFaces[int(face)];
}

inline filament::math::float2 Cubemap::center(size_t x, size_t y) {
    return { x + 0.5f, y + 0.5f };
}

inline filament::math::float3 Cubemap::getDirectionFor(Face face, size_t x, size_t y) const {
    return getDirectionFor(face, x + 0.5f, y + 0.5f);
}

inline filament::math::float3 Cubemap::getDirectionFor(Face face, float x, float y) const {
    // map [0, dim] to [-1,1] with (-1,-1) at bottom left
    float cx = (x * mScale) - 1;
    float cy = 1 - (y * mScale);

    filament::math::float3 dir;
    const float l = std::sqrt(cx * cx + cy * cy + 1);
    switch (face) {
        case Face::PX:  dir = {   1, cy, -cx }; break;
        case Face::NX:  dir = {  -1, cy,  cx }; break;
        case Face::PY:  dir = {  cx,  1, -cy }; break;
        case Face::NY:  dir = {  cx, -1,  cy }; break;
        case Face::PZ:  dir = {  cx, cy,   1 }; break;
        case Face::NZ:  dir = { -cx, cy,  -1 }; break;
    }
    return dir * (1 / l);
}

inline Cubemap::Texel const& Cubemap::sampleAt(const filament::math::float3& direction) const {
    Cubemap::Address addr(getAddressFor(direction));
    const size_t x = std::min(size_t(addr.s * mDimensions), mDimensions - 1);
    const size_t y = std::min(size_t(addr.t * mDimensions), mDimensions - 1);
    return sampleAt(getImageForFace(addr.face).getPixelRef(x, y));
}

inline Cubemap::Texel Cubemap::filterAt(const filament::math::float3& direction) const {
    Cubemap::Address addr(getAddressFor(direction));
    addr.s = std::min(addr.s * mDimensions, mUpperBound);
    addr.t = std::min(addr.t * mDimensions, mUpperBound);
    return filterAt(getImageForFace(addr.face), addr.s, addr.t);
}

} // namespace ibl
} // namespace filament

#endif /* IBL_CUBEMAP_H */
