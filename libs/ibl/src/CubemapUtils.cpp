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

#include <ibl/CubemapUtils.h>

#include "CubemapUtilsImpl.h"

#include <ibl/utilities.h>

#include <utils/JobSystem.h>

#include <math/mat4.h>

#include <algorithm>

#include <math.h>
#include <string.h>

using namespace filament::math;
using namespace utils;

namespace filament {
namespace ibl {

/**
 * 将图像值限制在可接受范围内实现
 * 
 * 使用色调映射算法压缩高动态范围值，防止溢出。
 * 参考：http://graphicrants.blogspot.com/2013/12/tone-mapping.html (Brian Karis)
 * 
 * 执行步骤：
 * 1. 遍历所有像素
 * 2. 计算像素亮度（使用REC 709权重）
 * 3. 如果亮度超过线性阈值，应用压缩函数
 * 4. 保持颜色比例不变
 * 
 * @param src 源图像（会被修改）
 */
void CubemapUtils::clamp(Image& src) {
    // See: http://graphicrants.blogspot.com/2013/12/tone-mapping.html
    // By Brian Karis
    // Lambda函数：压缩高动态范围颜色
    auto compress = [](float3 color, float linear, float compressed) {
        float luma = dot(color, float3{ 0.2126, 0.7152, 0.0722 }); // REC 709亮度权重
        // 如果亮度在线性范围内，直接返回；否则应用压缩函数
        return luma <= linear ? color :
               (color / luma) * ((linear * linear - compressed * luma)
                                 / (2 * linear - compressed - luma));
    };
    const size_t width = src.getWidth();
    const size_t height = src.getHeight();
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            float3& c = *static_cast<float3*>(src.getPixelRef(x, y));
            // these values are chosen arbitrarily and seem to produce good result with
            // 4096 samples
            // 这些值是任意选择的，似乎在使用4096个采样时产生良好结果
            c = compress(c, 4096.0f, 16384.0f);
        }
    }
}

/**
 * 高亮显示图像实现
 * 
 * 用于调试和可视化，将超出范围的值标记为特定颜色：
 * - 负值标记为蓝色
 * - 超过最大值（10位浮点数可编码的最大值）标记为红色
 * 
 * 执行步骤：
 * 1. 遍历所有像素
 * 2. 检查像素值是否在有效范围内
 * 3. 如果超出范围，设置为标记颜色
 * 
 * @param src 源图像（会被修改）
 */
void CubemapUtils::highlight(Image& src) {
    const size_t width = src.getWidth();
    const size_t height = src.getHeight();
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            float3& c = *static_cast<float3*>(src.getPixelRef(x, y));
            // 负值标记为蓝色
            if (min(c) < 0.0f) {
                c = { 0, 0, 1 };
            } else if (max(c) > 64512.0f) { // maximum encodable by 10-bits float (RGB_11_11_10)
                // 超过最大值（10位浮点数可编码的最大值，RGB_11_11_10格式）标记为红色
                c = { 1, 0, 0 };
            }
        }
    }
}

/**
 * 使用盒式过滤器将立方体贴图下采样实现
 * 
 * 执行步骤：
 * 1. 计算缩放因子（源尺寸 / 目标尺寸）
 * 2. 对目标立方体贴图的每个像素：
 *    - 在源图像中对应的位置使用双线性过滤采样
 *    - 将结果写入目标像素
 * 
 * 用于生成Mipmap级别。
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图（下采样后的结果）
 * @param src 源立方体贴图
 */
void CubemapUtils::downsampleCubemapLevelBoxFilter(JobSystem& js, Cubemap& dst, const Cubemap& src) {
    size_t scale = src.getDimensions() / dst.getDimensions();  // 计算缩放因子
    // 使用单线程处理，对每个像素进行双线性过滤采样
    processSingleThreaded<EmptyState>(dst, js,
            [&](EmptyState&, size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
                const Image& image(src.getImageForFace(f));
                for (size_t x = 0; x < dim; ++x, ++data) {
                    // 在源图像中对应的位置使用双线性过滤采样（filterAtCenter使用4个相邻像素的平均值）
                    Cubemap::writeAt(data, Cubemap::filterAtCenter(image, x * scale, y * scale));
                }
            });
}


/**
 * 计算立方体贴图面的象限投影到球面上的面积
 * 
 * 计算从(-1,1)到(x,y)的象限投影到单位球面上的面积。
 * 用于计算立体角。
 * 
 * 坐标系统：
 *  1 +---+----------+
 *    |   |          |
 *    |---+----------|
 *    |   |(x,y)     |
 *    |   |          |
 *    |   |          |
 * -1 +---+----------+
 *   -1              1
 * 
 * @param x X坐标
 * @param y Y坐标
 * @return 投影面积（球面度）
 */
static inline float sphereQuadrantArea(float x, float y) {
    return std::atan2(x*y, std::sqrt(x*x + y*y + 1));
}

/**
 * 计算立方体贴图面上像素的立体角实现
 * 
 * 执行步骤：
 * 1. 将像素坐标转换为归一化坐标（[-1, 1]范围）
 * 2. 计算像素的4个角点坐标
 * 3. 使用球面象限面积函数计算立体角
 * 4. 使用包含-排除原理计算像素的立体角
 * 
 * 立体角用于重要性采样和球谐函数计算中的权重。
 * 
 * @param dim 立方体贴图尺寸
 * @param u 像素U坐标
 * @param v 像素V坐标
 * @return 立体角（球面度）
 */
float CubemapUtils::solidAngle(size_t dim, size_t u, size_t v) {
    const float iDim = 1.0f / dim;  // 像素尺寸的倒数
    // 将像素中心坐标转换为归一化坐标（[-1, 1]范围）
    float s = ((u + 0.5f) * 2 * iDim) - 1;
    float t = ((v + 0.5f) * 2 * iDim) - 1;
    // 计算像素的4个角点坐标
    const float x0 = s - iDim;  // 左下角X
    const float y0 = t - iDim;  // 左下角Y
    const float x1 = s + iDim;  // 右上角X
    const float y1 = t + iDim;  // 右上角Y
    // 使用包含-排除原理计算像素的立体角
    // 立体角 = 从原点到(x1,y1)的象限面积 - 从原点到(x0,y1)的象限面积
    //        - 从原点到(x1,y0)的象限面积 + 从原点到(x0,y0)的象限面积
    float solidAngle = sphereQuadrantArea(x0, y0) -
                        sphereQuadrantArea(x0, y1) -
                        sphereQuadrantArea(x1, y0) +
                        sphereQuadrantArea(x1, y1);
    return solidAngle;
}

/**
 * 从交叉布局图像创建立方体贴图实现
 * 
 * 执行步骤：
 * 1. 创建临时图像（交叉布局）
 * 2. 从交叉布局图像设置所有面
 * 3. 交换图像（将结果返回给调用者）
 * 
 * @param image 输出图像（交叉布局）
 * @param dim 立方体贴图尺寸
 * @param horizontal 是否为水平布局（true=水平，false=垂直）
 * @return 创建的立方体贴图
 */
Cubemap CubemapUtils::create(Image& image, size_t dim, bool horizontal) {
    Cubemap cm(dim);
    Image temp(CubemapUtils::createCubemapImage(dim, horizontal));
    CubemapUtils::setAllFacesFromCross(cm, temp);
    std::swap(image, temp);
    return cm;
}

/**
 * 从交叉布局图像设置立方体贴图的单个面实现
 * 
 * 执行步骤：
 * 1. 根据面类型确定在交叉布局图像中的位置
 * 2. 提取子图像（跳过边界像素，用于无缝拼接）
 * 3. 设置到立方体贴图对应面
 * 
 * 交叉布局格式：
 *    [PY]
 * [NX][PZ][PX][NZ]
 *    [NY]
 * 
 * @param cm 立方体贴图
 * @param face 要设置的面
 * @param image 交叉布局源图像
 */
void CubemapUtils::setFaceFromCross(Cubemap& cm, Cubemap::Face face, const Image& image) {
    size_t dim = cm.getDimensions() + 2; // 2 extra per image, for seamlessness
    // 每个面额外分配2个像素，用于无缝拼接
    size_t x = 0;
    size_t y = 0;
    // 根据面类型确定在交叉布局图像中的位置
    switch (face) {
        case Cubemap::Face::NX:  // 负X面（左）
            x = 0, y = dim;
            break;
        case Cubemap::Face::PX:  // 正X面（右）
            x = 2 * dim, y = dim;
            break;
        case Cubemap::Face::NY:  // 负Y面（下）
            x = dim, y = 2 * dim;
            break;
        case Cubemap::Face::PY:  // 正Y面（上）
            x = dim, y = 0;
            break;
        case Cubemap::Face::NZ:  // 负Z面（后）
            x = 3 * dim, y = dim;
            break;
        case Cubemap::Face::PZ:  // 正Z面（前）
            x = dim, y = dim;
            break;
    }
    // 提取子图像（跳过边界像素）
    Image subImage;
    subImage.subset(image, x + 1, y + 1, dim - 2, dim - 2);
    cm.setImageForFace(face, subImage);
}

/**
 * 从交叉布局图像设置立方体贴图的所有面实现
 * 
 * 执行步骤：
 * 1. 依次设置6个面（NX, PX, NY, PY, NZ, PZ）
 * 
 * @param cm 立方体贴图
 * @param image 交叉布局源图像
 */
void CubemapUtils::setAllFacesFromCross(Cubemap& cm, const Image& image) {
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::NX, image);
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::PX, image);
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::NY, image);
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::PY, image);
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::NZ, image);
    CubemapUtils::setFaceFromCross(cm, Cubemap::Face::PZ, image);
}

/**
 * 创建交叉布局图像实现
 * 
 * 执行步骤：
 * 1. 计算图像尺寸（4个面宽，3个面高，每个面额外2个像素用于无缝拼接）
 * 2. 根据horizontal参数决定是水平还是垂直布局
 * 3. 创建并清零图像
 * 
 * 水平布局：
 *    [PY]
 * [NX][PZ][PX][NZ]
 *    [NY]
 * 
 * @param dim 立方体贴图尺寸
 * @param horizontal 是否为水平布局
 * @return 创建的交叉布局图像
 */
Image CubemapUtils::createCubemapImage(size_t dim, bool horizontal) {
    // always allocate 2 extra column and row / face, to allow the cubemap to be "seamless"
    // 总是为每个面额外分配2列和2行，以允许立方体贴图"无缝"
    size_t width = 4 * (dim + 2);   // 4个面宽
    size_t height = 3 * (dim + 2);  // 3个面高
    if (!horizontal) {
        std::swap(width, height);  // 垂直布局：交换宽高
    }

    Image image(width, height);
    memset(image.getData(), 0, image.getBytesPerRow() * height);  // 清零图像
    return image;
}

#ifndef FILAMENT_IBL_LITE

/**
 * 将等距圆柱投影图像转换为立方体贴图实现
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 计算像素4个角点在等距圆柱投影图像中的位置
 *    - 根据覆盖面积计算需要的采样数
 *    - 使用Hammersley序列进行重要性采样
 *    - 在等距圆柱投影图像中采样并平均
 *    - 写入立方体贴图
 * 
 * 使用自适应采样：根据立方体贴图像素在等距圆柱投影中覆盖的面积
 * 动态调整采样数，确保质量。
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param src 源等距圆柱投影图像
 */
void CubemapUtils::equirectangularToCubemap(JobSystem& js, Cubemap& dst, const Image& src) {
    const size_t width = src.getWidth();
    const size_t height = src.getHeight();

    // Lambda函数：将3D方向向量转换为等距圆柱投影坐标
    auto toRectilinear = [width, height](float3 s) -> float2 {
        float xf = std::atan2(s.x, s.z) * F_1_PI;   // range [-1.0, 1.0] 经度
        float yf = std::asin(s.y) * (2 * F_1_PI);   // range [-1.0, 1.0] 纬度
        xf = (xf + 1.0f) * 0.5f * (width  - 1);        // range [0, width [
        yf = (1.0f - yf) * 0.5f * (height - 1);        // range [0, height[
        return float2(xf, yf);
    };

    process<EmptyState>(dst, js,
            [&](EmptyState&, size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
        for (size_t x=0 ; x<dim ; ++x, ++data) {
            // calculate how many samples we need based on dx, dy in the source
            // 根据源图像中的dx、dy计算需要的采样数
            // x = cos(phi) sin(theta)
            // y = sin(phi)
            // z = cos(phi) cos(theta)

            // here we try to figure out how many samples we need, by evaluating the surface
            // (in pixels) in the equirectangular -- we take the bounding box of the
            // projection of the cubemap texel's corners.
            // 这里我们尝试通过评估等距圆柱投影中的表面（以像素为单位）来确定需要的采样数
            // ——我们取立方体贴图像素角点投影的边界框

            // 计算像素4个角点在等距圆柱投影图像中的位置
            auto pos0 = toRectilinear(dst.getDirectionFor(f, x + 0.0f, y + 0.0f)); // make sure to use the float version
            auto pos1 = toRectilinear(dst.getDirectionFor(f, x + 1.0f, y + 0.0f)); // make sure to use the float version
            auto pos2 = toRectilinear(dst.getDirectionFor(f, x + 0.0f, y + 1.0f)); // make sure to use the float version
            auto pos3 = toRectilinear(dst.getDirectionFor(f, x + 1.0f, y + 1.0f)); // make sure to use the float version
            // 计算边界框
            const float minx = std::min(pos0.x, std::min(pos1.x, std::min(pos2.x, pos3.x)));
            const float maxx = std::max(pos0.x, std::max(pos1.x, std::max(pos2.x, pos3.x)));
            const float miny = std::min(pos0.y, std::min(pos1.y, std::min(pos2.y, pos3.y)));
            const float maxy = std::max(pos0.y, std::max(pos1.y, std::max(pos2.y, pos3.y)));
            // 计算覆盖面积（像素数）
            const float dx = std::max(1.0f, maxx - minx);
            const float dy = std::max(1.0f, maxy - miny);
            const size_t numSamples = size_t(dx * dy);  // 根据覆盖面积确定采样数

            // 使用Hammersley序列进行重要性采样
            const float iNumSamples = 1.0f / numSamples;
            float3 c = 0;
            for (size_t sample = 0; sample < numSamples; sample++) {
                // Generate numSamples in our destination pixels and map them to input pixels
                // 在目标像素中生成numSamples个采样点，并将它们映射到输入像素
                const float2 h = hammersley(uint32_t(sample), iNumSamples);  // Hammersley低差异序列
                const float3 s(dst.getDirectionFor(f, x + h.x, y + h.y));  // 采样方向
                auto pos = toRectilinear(s);  // 转换为等距圆柱投影坐标

                // we can't use filterAt() here because it reads past the width/height
                // which is okay for cubmaps but not for square images
                // 这里不能使用filterAt()，因为它会读取超出宽度/高度的内容
                // 这对立方体贴图是可以的，但对方形图像不行

                // TODO: the sample should be weighed by the area it covers in the cubemap texel
                // TODO: 采样应该按其覆盖的立方体贴图像素面积加权

                c += Cubemap::sampleAt(src.getPixelRef((uint32_t)pos.x, (uint32_t)pos.y));
            }
            c *= iNumSamples;  // 平均采样结果

            Cubemap::writeAt(data, c);
        }
    });
}

/**
 * 将立方体贴图转换为等距圆柱投影图像实现
 * 
 * 执行步骤：
 * 1. 对等距圆柱投影图像的每个像素：
 *    - 将像素坐标转换为球面坐标（theta, phi）
 *    - 将球面坐标转换为3D方向向量
 *    - 使用Hammersley序列进行重要性采样
 *    - 在立方体贴图中采样并平均
 *    - 写入等距圆柱投影图像
 * 
 * 使用多线程并行处理以提高性能。
 * 
 * @param js 作业系统
 * @param dst 目标等距圆柱投影图像
 * @param src 源立方体贴图
 */
void CubemapUtils::cubemapToEquirectangular(JobSystem& js, Image& dst, const Cubemap& src) {
    const float w = dst.getWidth();
    const float h = dst.getHeight();
    // Lambda函数：并行处理任务
    auto parallelJobTask = [&](size_t j0, size_t count) {
        for (size_t j = j0; j < j0 + count; j++) {
            for (size_t i = 0; i < w; i++) {
                float3 c = 0;
                const size_t numSamples = 64; // TODO: how to chose numsamples
                // 使用Hammersley序列进行重要性采样
                for (size_t sample = 0; sample < numSamples; sample++) {
                    const float2 u = hammersley(uint32_t(sample), 1.0f / numSamples);
                    // 将像素坐标转换为归一化坐标（[-1, 1]范围）
                    float x = 2.0f * (i + u.x) / w - 1.0f;
                    float y = 1.0f - 2.0f * (j + u.y) / h;
                    // 转换为球面坐标
                    float theta = x * F_PI;      // 经度 [-π, π]
                    float phi = y * F_PI * 0.5;   // 纬度 [-π/2, π/2]
                    // 转换为3D方向向量
                    float3 s = {
                            std::cos(phi) * std::sin(theta),
                            std::sin(phi),
                            std::cos(phi) * std::cos(theta) };
                    c += src.filterAt(s);  // 在立方体贴图中采样
                }
                Cubemap::writeAt(dst.getPixelRef(i, j), c * (1.0f / numSamples));  // 平均采样结果
            }
        }
    };

    // 创建并行作业并等待完成
    auto job = jobs::parallel_for(js, nullptr, 0, uint32_t(h),
            std::ref(parallelJobTask), jobs::CountSplitter<1, 8>());
    js.runAndWait(job);
}

/**
 * 将立方体贴图转换为八面体投影图像实现
 * 
 * 执行步骤：
 * 1. 对八面体投影图像的每个像素：
 *    - 将像素坐标转换为归一化坐标
 *    - 使用八面体映射将2D坐标转换为3D方向向量
 *    - 使用Hammersley序列进行重要性采样
 *    - 在立方体贴图中采样并平均
 *    - 写入八面体投影图像
 * 
 * 八面体投影是一种将球面映射到正方形的投影方法，比等距圆柱投影
 * 在极点处失真更小。
 * 
 * @param js 作业系统
 * @param dst 目标八面体投影图像
 * @param src 源立方体贴图
 */
void CubemapUtils::cubemapToOctahedron(JobSystem& js, Image& dst, const Cubemap& src) {
    const float w = dst.getWidth();
    const float h = dst.getHeight();
    // Lambda函数：并行处理任务
    auto parallelJobTask = [&](size_t j0, size_t count) {
        for (size_t j = j0; j < j0 + count; j++) {
            for (size_t i = 0; i < w; i++) {
                float3 c = 0;
                const size_t numSamples = 64; // TODO: how to chose numsamples
                // 使用Hammersley序列进行重要性采样
                for (size_t sample = 0; sample < numSamples; sample++) {
                    const float2 u = hammersley(uint32_t(sample), 1.0f / numSamples);
                    // 将像素坐标转换为归一化坐标（[-1, 1]范围）
                    float x = 2.0f * (i + u.x) / w - 1.0f;
                    float z = 2.0f * (j + u.y) / h - 1.0f;
                    float y;
                    // 八面体映射：将2D坐标转换为3D方向向量
                    if (std::abs(z) > (1.0f - std::abs(x))) {
                        // 下半球面：需要翻转坐标
                        float u = x < 0 ? std::abs(z) - 1 : 1 - std::abs(z);
                        float v = z < 0 ? std::abs(x) - 1 : 1 - std::abs(x);
                        x = u;
                        z = v;
                        y = (std::abs(x) + std::abs(z)) - 1.0f;
                    } else {
                        // 上半球面
                        y = 1.0f - (std::abs(x) + std::abs(z));
                    }
                    c += src.filterAt({x, y, z});  // 在立方体贴图中采样
                }
                Cubemap::writeAt(dst.getPixelRef(i, j), c * (1.0f / numSamples));  // 平均采样结果
            }
        }
    };

    // 创建并行作业并等待完成
    auto job = jobs::parallel_for(js, nullptr, 0, uint32_t(h),
            std::ref(parallelJobTask), jobs::CountSplitter<1, 8>());
    js.runAndWait(job);
}

/**
 * 从交叉布局图像转换为立方体贴图实现
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 根据面类型确定在交叉布局图像中的偏移量
 *    - 计算需要的采样数（根据源图像和目标图像尺寸比）
 *    - 使用Hammersley序列进行重要性采样
 *    - 在交叉布局图像中采样并平均
 *    - 写入立方体贴图
 * 
 * 支持水平和垂直交叉布局。
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param src 源交叉布局图像
 */
void CubemapUtils::crossToCubemap(JobSystem& js, Cubemap& dst, const Image& src) {
    process<EmptyState>(dst, js,
            [&](EmptyState&, size_t iy, Cubemap::Face f, Cubemap::Texel* data, size_t dimension) {
                for (size_t ix = 0; ix < dimension; ++ix, ++data) {
                    // find offsets from face
                    // 查找面的偏移量
                    size_t x = ix;
                    size_t y = iy;
                    size_t dx = 0;  // X方向偏移
                    size_t dy = 0;  // Y方向偏移
                    size_t dim = std::max(src.getHeight(), src.getWidth()) / 4;  // 每个面的尺寸

                    // 根据面类型确定在交叉布局图像中的位置
                    switch (f) {
                        case Cubemap::Face::NX:  // 负X面（左）
                            dx = 0, dy = dim;
                            break;
                        case Cubemap::Face::PX:  // 正X面（右）
                            dx = 2 * dim, dy = dim;
                            break;
                        case Cubemap::Face::NY:  // 负Y面（下）
                            dx = dim, dy = 2 * dim;
                            break;
                        case Cubemap::Face::PY:  // 正Y面（上）
                            dx = dim, dy = 0;
                            break;
                        case Cubemap::Face::NZ:  // 负Z面（后）
                            if (src.getHeight() > src.getWidth()) {
                                // 垂直布局：需要翻转坐标
                                dx = dim, dy = 3 * dim;
                                x = dimension - 1 - ix;
                                y = dimension - 1 - iy;
                            } else {
                                // 水平布局
                                dx = 3 * dim, dy = dim;
                            }
                            break;
                        case Cubemap::Face::PZ:  // 正Z面（前）
                            dx = dim, dy = dim;
                            break;
                    }

                    // 根据源图像和目标图像尺寸比计算采样数
                    size_t sampleCount = std::max(size_t(1), dim / dimension);
                    sampleCount = std::min(size_t(256), sampleCount * sampleCount);
                    // 使用Hammersley序列进行重要性采样
                    for (size_t i = 0; i < sampleCount; i++) {
                        const float2 h = hammersley(uint32_t(i), 1.0f / sampleCount);
                        size_t u = dx + size_t((x + h.x) * dim / dimension);
                        size_t v = dy + size_t((y + h.y) * dim / dimension);
                        Cubemap::writeAt(data, Cubemap::sampleAt(src.getPixelRef(u, v)));
                    }
                }
            });
}

/**
 * 获取立方体贴图面的名称实现
 * 
 * @param face 立方体贴图面
 * @return 面的名称字符串（"nx", "px", "ny", "py", "nz", "pz"）
 */
const char* CubemapUtils::getFaceName(Cubemap::Face face) {
    switch (face) {
        case Cubemap::Face::NX: return "nx";
        case Cubemap::Face::PX: return "px";
        case Cubemap::Face::NY: return "ny";
        case Cubemap::Face::PY: return "py";
        case Cubemap::Face::NZ: return "nz";
        case Cubemap::Face::PZ: return "pz";
    }
}

/**
 * 镜像立方体贴图实现
 * 
 * 执行步骤：
 * 1. 对立方体贴图的每个像素：
 *    - 获取像素对应的3D方向向量
 *    - 镜像X坐标（翻转X方向）
 *    - 在源立方体贴图中采样
 *    - 写入目标立方体贴图
 * 
 * 用于创建镜像环境贴图。
 * 
 * @param js 作业系统
 * @param dst 目标立方体贴图
 * @param src 源立方体贴图
 */
void CubemapUtils::mirrorCubemap(JobSystem& js, Cubemap& dst, const Cubemap& src) {
    processSingleThreaded<EmptyState>(dst, js,
            [&](EmptyState&, size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
        for (size_t x=0 ; x<dim ; ++x, ++data) {
            const float3 N(dst.getDirectionFor(f, x, y));  // 获取3D方向向量
            // 镜像X坐标：翻转X方向，保持Y和Z不变
            Cubemap::writeAt(data, src.sampleAt(float3{ -N.x, N.y, N.z }));
        }
    });
}

/**
 * 生成UV网格图案立方体贴图实现
 * 
 * 执行步骤：
 * 1. 为每个面定义不同的颜色
 * 2. 计算网格尺寸（根据频率）
 * 3. 对每个像素：
 *    - 根据网格位置确定是否绘制网格线
 *    - 如果在网格线上，使用对应面的颜色
 *    - 否则使用黑色
 *    - 应用HDR强度倍增
 * 
 * 用于调试和可视化，帮助识别立方体贴图的各个面。
 * 
 * @param js 作业系统
 * @param cml 立方体贴图（会被修改）
 * @param gridFrequencyX X方向网格频率（网格数量）
 * @param gridFrequencyY Y方向网格频率（网格数量）
 */
void CubemapUtils::generateUVGrid(JobSystem& js, Cubemap& cml, size_t gridFrequencyX, size_t gridFrequencyY) {
    // 每个面的颜色（用于区分不同的面）
    Cubemap::Texel const colors[6] = {
            { 1, 1, 1 }, // +X /  r  - white  (正X面/右 - 白色)
            { 1, 0, 0 }, // -X /  l  - red    (负X面/左 - 红色)
            { 0, 0, 1 }, // +Y /  t  - blue   (正Y面/上 - 蓝色)
            { 0, 1, 0 }, // -Y /  b  - green  (负Y面/下 - 绿色)
            { 1, 1, 0 }, // +z / fr - yellow  (正Z面/前 - 黄色)
            { 1, 0, 1 }, // -Z / bk - magenta (负Z面/后 - 品红色)
    };
    const float uvGridHDRIntensity = 5.0f;  // UV网格HDR强度
    // 计算网格尺寸（每个网格的像素数）
    size_t gridSizeX = cml.getDimensions() / gridFrequencyX;
    size_t gridSizeY = cml.getDimensions() / gridFrequencyY;
    CubemapUtils::process<EmptyState>(cml, js,
            [ & ](EmptyState&,
                    size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t dim) {
                for (size_t x = 0; x < dim; ++x, ++data) {
                    // 使用XOR操作创建棋盘格图案
                    // 如果(x/gridSizeX)和(y/gridSizeY)的奇偶性不同，则绘制网格
                    bool grid = bool(((x / gridSizeX) ^ (y / gridSizeY)) & 1);
                    // 如果在网格线上，使用对应面的颜色并应用HDR强度；否则使用黑色
                    Cubemap::Texel t = grid ? colors[(int)f] * uvGridHDRIntensity : 0;
                    Cubemap::writeAt(data, t);
                }
            });
}
#endif

} // namespace ibl
} // namespace filament
