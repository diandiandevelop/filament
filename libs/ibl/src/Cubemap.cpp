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

#include <ibl/Cubemap.h>

using namespace filament::math;

namespace filament {
namespace ibl {

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 调用resetDimensions初始化尺寸
 * 
 * @param dim 立方体贴图尺寸
 */
Cubemap::Cubemap(size_t dim) {
    resetDimensions(dim);
}

/**
 * 析构函数实现（默认）
 */
Cubemap::~Cubemap() = default;

/**
 * 获取立方体贴图尺寸实现
 * 
 * @return 立方体贴图尺寸
 */
size_t Cubemap::getDimensions() const {
    return mDimensions;
}

/**
 * 重置立方体贴图尺寸实现
 * 
 * 执行步骤：
 * 1. 设置尺寸
 * 2. 计算缩放因子（将[0, dim]映射到[-1, 1]）
 * 3. 计算上界（用于边界检查）
 * 4. 重置所有面的图像
 * 
 * @param dim 新的立方体贴图尺寸
 */
void Cubemap::resetDimensions(size_t dim) {
    mDimensions = dim;
    mScale = 2.0f / dim;  // 缩放因子：将[0, dim]映射到[-1, 1]
    mUpperBound = std::nextafter((float) mDimensions, 0.0f);  // 上界（略小于dimensions，用于边界检查）
    // 重置所有面的图像
    for (auto& mFace : mFaces) {
        mFace.reset();
    }
}

/**
 * 为指定面分配图像实现
 * 
 * 执行步骤：
 * 1. 将图像分配给指定面（不复制数据，只是引用）
 * 
 * @param face 立方体贴图面
 * @param image 图像对象
 */
void Cubemap::setImageForFace(Face face, const Image& image) {
    mFaces[size_t(face)].set(image);
}

/**
 * 获取给定方向对应的面和纹理坐标实现
 * 
 * 执行步骤：
 * 1. 计算方向向量各分量的绝对值
 * 2. 找到绝对值最大的分量，确定对应的面
 * 3. 根据面的方向计算纹理坐标（s, t）
 * 4. 将纹理坐标从[-1, 1]范围映射到[0, 1]范围
 * 
 * 算法说明：
 * - 使用立方体贴图的标准投影方法
 * - 选择绝对值最大的分量来确定主要方向
 * - 使用其他两个分量计算纹理坐标
 * 
 * @param r 归一化的方向向量
 * @return 立方体贴图地址（包含面和纹理坐标）
 */
Cubemap::Address Cubemap::getAddressFor(const float3& r) {
    Cubemap::Address addr;
    float sc, tc, ma;  // sc和tc是纹理坐标的中间值，ma是最大分量的倒数
    const float rx = std::abs(r.x);
    const float ry = std::abs(r.y);
    const float rz = std::abs(r.z);
    
    // 根据绝对值最大的分量确定面
    if (rx >= ry && rx >= rz) {
        // X分量最大，使用PX或NX面
        ma = 1.0f / rx;
        if (r.x >= 0) {
            addr.face = Face::PX;  // 正X面（右面）
            sc = -r.z;
            tc = -r.y;
        } else {
            addr.face = Face::NX;  // 负X面（左面）
            sc =  r.z;
            tc = -r.y;
        }
    } else if (ry >= rx && ry >= rz) {
        // Y分量最大，使用PY或NY面
        ma = 1.0f / ry;
        if (r.y >= 0) {
            addr.face = Face::PY;  // 正Y面（上面）
            sc =  r.x;
            tc =  r.z;
        } else {
            addr.face = Face::NY;  // 负Y面（下面）
            sc =  r.x;
            tc = -r.z;
        }
    } else {
        // Z分量最大，使用PZ或NZ面
        ma = 1.0f / rz;
        if (r.z >= 0) {
            addr.face = Face::PZ;  // 正Z面（后面）
            sc =  r.x;
            tc = -r.y;
        } else {
            addr.face = Face::NZ;  // 负Z面（前面）
            sc = -r.x;
            tc = -r.y;
        }
    }
    // ma保证 >= sc和tc（因为ma是最大分量的倒数）
    // 将纹理坐标从[-1, 1]映射到[0, 1]
    addr.s = (sc * ma + 1.0f) * 0.5f;
    addr.t = (tc * ma + 1.0f) * 0.5f;
    return addr;
}

/**
 * 准备立方体贴图以实现无缝访问实现
 * 
 * 执行步骤：
 * 1. 为每个面的边缘复制相邻面的数据
 * 2. 处理面的4个角（使用3个相邻像素的平均值）
 * 
 * 算法说明：
 * - 在面的边缘（-1和D位置）复制相邻面的对应边缘数据
 * - 在面的4个角使用3个相邻像素的平均值
 * - 这样可以在边界处实现无缝采样
 * 
 * @warning 所有面必须由同一个Image支持，并且必须已经间隔2行/列
 */
void Cubemap::makeSeamless() {
    size_t dim = getDimensions();
    size_t D = dim;

    // here we assume that all faces share the same underlying image
    // 这里我们假设所有面共享同一个底层图像
    const size_t bpr = getImageForFace(Face::NX).getBytesPerRow();  // 每行字节数
    const size_t bpp = getImageForFace(Face::NX).getBytesPerPixel();  // 每像素字节数

    // Lambda函数：获取指定位置的纹理元素指针
    auto getTexel = [](Image& image, ssize_t x, ssize_t y) -> Texel* {
        return (Texel*)((uint8_t*)image.getData() + x * image.getBytesPerPixel() + y * image.getBytesPerRow());
    };

    // Lambda函数：缝合两个面的边缘
    // 从源面的指定位置复制数据到目标面的指定位置
    auto stitch = [ & ](
            Face faceDst, ssize_t xdst, ssize_t ydst, size_t incDst,  // 目标面和位置
            Face faceSrc, size_t xsrc, size_t ysrc, ssize_t incSrc) {  // 源面和位置
        Image& imageDst = getImageForFace(faceDst);
        Image& imageSrc = getImageForFace(faceSrc);
        Texel* dst = getTexel(imageDst, xdst, ydst);
        Texel* src = getTexel(imageSrc, xsrc, ysrc);
        // 复制整行或整列
        for (size_t i = 0; i < dim; ++i) {
            *dst = *src;
            dst = (Texel*)((uint8_t*)dst + incDst);  // 移动到下一个目标位置
            src = (Texel*)((uint8_t*)src + incSrc);  // 移动到下一个源位置
        }
    };

    // Lambda函数：处理面的4个角
    // 使用3个相邻像素的平均值填充角位置
    auto corners = [ & ](Face face) {
        size_t L = D - 1;
        Image& image = getImageForFace(face);
        // 左上角：使用(0,0)、(-1,0)、(0,-1)的平均值
        *getTexel(image,  -1,  -1) = (*getTexel(image, 0, 0) + *getTexel(image,  -1,  0) + *getTexel(image, 0,    -1)) / 3;
        // 右上角：使用(L,0)、(L,-1)、(L+1,0)的平均值
        *getTexel(image, L+1,  -1) = (*getTexel(image, L, 0) + *getTexel(image,   L, -1) + *getTexel(image, L+1,   0)) / 3;
        // 左下角：使用(0,L)、(-1,L)、(0,L+1)的平均值
        *getTexel(image,  -1, L+1) = (*getTexel(image, 0, L) + *getTexel(image,  -1,  L) + *getTexel(image, 0,   L+1)) / 3;
        // 右下角：使用(L,L)、(L+1,L)、(L+1,L)的平均值
        *getTexel(image, L+1, L+1) = (*getTexel(image, L, L) + *getTexel(image, L+1,  L) + *getTexel(image, L+1,   L)) / 3;
    };

    // +Y / Top
    stitch( Face::PY, -1,  0,  bpr, Face::NX,  0,    0,    bpp);      // left
    stitch( Face::PY,  0, -1,  bpp, Face::NZ,  D-1,  0,   -bpp);      // top
    stitch( Face::PY,  D,  0,  bpr, Face::PX,  D-1,  0,   -bpp);      // right
    stitch( Face::PY,  0,  D,  bpp, Face::PZ,  0,    0,    bpp);      // bottom
    corners(Face::PY);

    // -X / Left
    stitch( Face::NX, -1,  0,  bpr, Face::NZ,  D-1,  0,    bpr);      // left
    stitch( Face::NX,  0, -1,  bpp, Face::PY,  0,    0,    bpr);      // top
    stitch( Face::NX,  D,  0,  bpr, Face::PZ,  0,    0,    bpr);      // right
    stitch( Face::NX,  0,  D,  bpp, Face::NY,  0,    D-1, -bpr);      // bottom
    corners(Face::NX);

    // +Z / Front
    stitch( Face::PZ, -1,  0,  bpr, Face::NX,  D-1,  0,    bpr);      // left
    stitch( Face::PZ,  0, -1,  bpp, Face::PY,  0,    D-1,  bpp);      // top
    stitch( Face::PZ,  D,  0,  bpr, Face::PX,  0,    0,    bpr);      // right
    stitch( Face::PZ,  0,  D,  bpp, Face::NY,  0,    0,    bpp);      // bottom
    corners(Face::PZ);

    // +X / Right
    stitch( Face::PX, -1,  0,  bpr, Face::PZ,  D-1,  0,    bpr);      // left
    stitch( Face::PX,  0, -1,  bpp, Face::PY,  D-1,  D-1, -bpr);      // top
    stitch( Face::PX,  D,  0,  bpr, Face::NZ,  0,    0,    bpr);      // right
    stitch( Face::PX,  0,  D,  bpp, Face::NY,  D-1,  0,    bpr);      // bottom
    corners(Face::PX);

    // -Z / Back
    stitch( Face::NZ, -1,  0,  bpr, Face::PX,  D-1,  0,    bpr);      // left
    stitch( Face::NZ,  0, -1,  bpp, Face::PY,  D-1,  0,   -bpp);      // top
    stitch( Face::NZ,  D,  0,  bpr, Face::NX,  0,    0,    bpr);      // right
    stitch( Face::NZ,  0,  D,  bpp, Face::NY,  D-1,  D-1, -bpp);      // bottom
    corners(Face::NZ);

    // -Y / Bottom
    stitch( Face::NY, -1,  0,  bpr, Face::NX,  D-1,  D-1, -bpp);      // left
    stitch( Face::NY,  0, -1,  bpp, Face::PZ,  0,    D-1,  bpp);      // top
    stitch( Face::NY,  D,  0,  bpr, Face::PX,  0,    D-1,  bpp);      // right
    stitch( Face::NY,  0,  D,  bpp, Face::NZ,  D-1,  D-1, -bpp);      // bottom
    corners(Face::NY);
}

/**
 * 使用双线性过滤在图像指定位置采样实现
 * 
 * 执行步骤：
 * 1. 计算4个相邻像素的坐标
 * 2. 计算插值权重（u, v）
 * 3. 读取4个像素的值
 * 4. 使用双线性插值计算最终值
 * 
 * 注意：允许读取超出图像宽度/高度的数据，因为数据有效且包含"无缝"数据。
 * 
 * @param image 图像对象
 * @param x 采样位置X坐标（浮点数）
 * @param y 采样位置Y坐标（浮点数）
 * @return 采样到的纹理元素
 */
Cubemap::Texel Cubemap::filterAt(const Image& image, float x, float y) {
    const size_t x0 = size_t(x);  // 左下角像素X坐标
    const size_t y0 = size_t(y);  // 左下角像素Y坐标
    // we allow ourselves to read past the width/height of the Image because the data is valid
    // and contain the "seamless" data.
    // 我们允许读取超出图像宽度/高度的数据，因为数据有效且包含"无缝"数据
    size_t x1 = x0 + 1;  // 右上角像素X坐标
    size_t y1 = y0 + 1;  // 右上角像素Y坐标

    // 计算插值权重
    const float u = float(x - x0);  // X方向的插值权重
    const float v = float(y - y0);  // Y方向的插值权重
    const float one_minus_u = 1 - u;
    const float one_minus_v = 1 - v;
    
    // 读取4个相邻像素的值
    const Texel& c0 = sampleAt(image.getPixelRef(x0, y0));  // 左下角
    const Texel& c1 = sampleAt(image.getPixelRef(x1, y0));  // 右下角
    const Texel& c2 = sampleAt(image.getPixelRef(x0, y1));  // 左上角
    const Texel& c3 = sampleAt(image.getPixelRef(x1, y1));  // 右上角
    
    // 双线性插值：先沿X方向插值，再沿Y方向插值
    return (one_minus_u*one_minus_v)*c0 + (u*one_minus_v)*c1 + (one_minus_u*v)*c2 + (u*v)*c3;
}

/**
 * 在像素中心位置使用双线性过滤采样图像实现
 * 
 * 执行步骤：
 * 1. 计算4个相邻像素的坐标
 * 2. 读取4个像素的值
 * 3. 计算平均值（相当于在像素中心采样）
 * 
 * 注意：允许读取超出图像宽度/高度的数据，因为数据有效且包含"无缝"数据。
 * 
 * @param image 图像对象
 * @param x0 像素X坐标
 * @param y0 像素Y坐标
 * @return 采样到的纹理元素（4个相邻像素的平均值）
 */
Cubemap::Texel Cubemap::filterAtCenter(const Image& image, size_t x0, size_t y0) {
    // we allow ourselves to read past the width/height of the Image because the data is valid
    // and contain the "seamless" data.
    // 我们允许读取超出图像宽度/高度的数据，因为数据有效且包含"无缝"数据
    size_t x1 = x0 + 1;
    size_t y1 = y0 + 1;
    // 读取4个相邻像素的值
    const Texel& c0 = sampleAt(image.getPixelRef(x0, y0));
    const Texel& c1 = sampleAt(image.getPixelRef(x1, y0));
    const Texel& c2 = sampleAt(image.getPixelRef(x0, y1));
    const Texel& c3 = sampleAt(image.getPixelRef(x1, y1));
    // 返回平均值（相当于在像素中心采样）
    return (c0 + c1 + c2 + c3) * 0.25f;
}

/**
 * 三线性过滤实现
 * 
 * 执行步骤：
 * 1. 获取方向对应的面和纹理坐标
 * 2. 在两个Mipmap级别上分别进行双线性过滤
 * 3. 使用lerp因子在两个结果之间进行线性插值
 * 
 * @param l0 第一个立方体贴图（较低Mipmap级别）
 * @param l1 第二个立方体贴图（较高Mipmap级别）
 * @param lerp 插值因子（0.0 = l0, 1.0 = l1）
 * @param L 归一化的方向向量
 * @return 插值后的纹理元素
 */
Cubemap::Texel Cubemap::trilinearFilterAt(const Cubemap& l0, const Cubemap& l1, float lerp,
        const float3& L)
{
    // 获取方向对应的面和纹理坐标
    Cubemap::Address addr(getAddressFor(L));
    const Image& i0 = l0.getImageForFace(addr.face);
    const Image& i1 = l1.getImageForFace(addr.face);
    
    // 计算两个Mipmap级别的采样坐标
    float x0 = std::min(addr.s * l0.mDimensions, l0.mUpperBound);
    float y0 = std::min(addr.t * l0.mDimensions, l0.mUpperBound);
    float x1 = std::min(addr.s * l1.mDimensions, l1.mUpperBound);
    float y1 = std::min(addr.t * l1.mDimensions, l1.mUpperBound);
    
    // 在两个Mipmap级别上分别进行双线性过滤
    float3 c0 = filterAt(i0, x0, y0);
    // 在两个结果之间进行线性插值
    c0 += lerp * (filterAt(i1, x1, y1) - c0);
    return c0;
}

} // namespace ibl
} // namespace filament
