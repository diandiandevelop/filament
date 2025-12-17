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

#ifndef IBL_CUBEMAP_UTILS_H
#define IBL_CUBEMAP_UTILS_H

#include <ibl/Cubemap.h>
#include <ibl/Image.h>

#include <utils/compiler.h>

#include <functional>

namespace utils {
class JobSystem;
} // namespace utils

namespace filament {
namespace ibl {

class CubemapIBL;

/**
 * CubemapUtils - 立方体贴图工具类
 * 
 * 用于创建和转换立方体贴图格式。
 * 提供各种立方体贴图处理功能，包括格式转换、下采样、处理等。
 */
class UTILS_PUBLIC CubemapUtils {
public:
    /**
     * 创建立方体贴图对象及其底层图像
     * 
     * @param image 图像对象（将被用于立方体贴图）
     * @param dim 立方体贴图尺寸
     * @param horizontal 是否为水平交叉布局（true）或垂直交叉布局（false）
     * @return 创建的立方体贴图对象
     */
    static Cubemap create(Image& image, size_t dim, bool horizontal = true);

    /**
     * 空状态结构体
     * 
     * 用于不需要状态的处理函数。
     */
    struct EmptyState {
    };

    /**
     * 扫描线处理函数类型（模板）
     * 
     * 用于处理立方体贴图的每一行像素。
     * 
     * @param state 状态对象（会被修改）
     * @param y 当前行索引
     * @param f 当前面
     * @param data 当前行的像素数据指针
     * @param width 行宽度
     */
    template<typename STATE>
    using ScanlineProc = std::function<
            void(STATE& state, size_t y, Cubemap::Face f, Cubemap::Texel* data, size_t width)>;

    /**
     * 归约处理函数类型（模板）
     * 
     * 在所有扫描线处理完成后调用，用于合并状态。
     * 
     * @param state 状态对象（会被修改）
     */
    template<typename STATE>
    using ReduceProc = std::function<void(STATE& state)>;

    /**
     * 使用多线程处理立方体贴图
     * 
     * 并行处理立方体贴图的所有面，每个面使用多线程处理。
     * 
     * @param cm 立方体贴图对象
     * @param js 作业系统（用于并行处理）
     * @param proc 扫描线处理函数
     * @param reduce 归约处理函数（可选）
     * @param prototype 状态原型（用于初始化每个线程的状态）
     */
    template<typename STATE>
    static void process(Cubemap& cm,
            utils::JobSystem& js,
            ScanlineProc<STATE> proc,
            ReduceProc<STATE> reduce = [](STATE&) {},
            const STATE& prototype = STATE());

    /**
     * 单线程处理立方体贴图
     * 
     * 顺序处理立方体贴图的所有面。
     * 
     * @param cm 立方体贴图对象
     * @param js 作业系统（未使用，但保留接口一致性）
     * @param proc 扫描线处理函数
     * @param reduce 归约处理函数（可选）
     * @param prototype 状态原型
     */
    template<typename STATE>
    static void processSingleThreaded(Cubemap& cm,
            utils::JobSystem& js,
            ScanlineProc<STATE> proc,
            ReduceProc<STATE> reduce = [](STATE&) {},
            const STATE& prototype = STATE());

    /**
     * 将图像值限制在可接受范围内
     * 
     * 将图像像素值限制在合理范围内，防止溢出或无效值。
     * 
     * @param src 源图像（会被修改）
     */
    static void clamp(Image& src);

    /**
     * 高亮显示图像
     * 
     * 增强图像的对比度和亮度，用于可视化。
     * 
     * @param src 源图像（会被修改）
     */
    static void highlight(Image& src);

    /**
     * 使用盒式过滤器将立方体贴图在X和Y方向上各下采样一半
     * 
     * 用于生成Mipmap级别。
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图（下采样后的结果）
     * @param src 源立方体贴图
     */
    static void downsampleCubemapLevelBoxFilter(utils::JobSystem& js, Cubemap& dst, const Cubemap& src);

    /**
     * 返回面的名称（适合用作文件名）
     * 
     * @param face 立方体贴图面
     * @return 面的名称字符串（如"px", "nx", "py"等）
     */
    static const char* getFaceName(Cubemap::Face face);

    /**
     * 计算立方体贴图面上像素的立体角
     * 
     * 立体角用于重要性采样和球谐函数计算中的权重。
     * 
     * @param dim 立方体贴图尺寸
     * @param u 像素U坐标
     * @param v 像素V坐标
     * @return 立体角（球面度）
     */
    static float solidAngle(size_t dim, size_t u, size_t v);

    /**
     * 从交叉图像设置立方体贴图的所有面
     * 
     * 从水平或垂直交叉布局的图像中提取6个面。
     * 
     * @param cm 目标立方体贴图
     * @param image 源交叉图像
     */
    static void setAllFacesFromCross(Cubemap& cm, const Image& image);

private:

    /**
     * 从交叉图像设置立方体贴图的指定面
     * 
     * @param cm 目标立方体贴图
     * @param face 要设置的面
     * @param image 源交叉图像
     */
    //move these into cmgen?
    static void setFaceFromCross(Cubemap& cm, Cubemap::Face face, const Image& image);
    
    /**
     * 创建立方体贴图图像
     * 
     * 创建一个足够大的图像来容纳立方体贴图的所有面。
     * 
     * @param dim 立方体贴图尺寸
     * @param horizontal 是否为水平交叉布局
     * @return 创建的图像对象
     */
    static Image createCubemapImage(size_t dim, bool horizontal = true);

#ifndef FILAMENT_IBL_LITE

public:

    /**
     * 将水平或垂直交叉图像转换为立方体贴图
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图
     * @param src 源交叉图像
     */
    static void crossToCubemap(utils::JobSystem& js, Cubemap& dst, const Image& src);

    /**
     * 将等距圆柱投影图像转换为立方体贴图
     * 
     * 等距圆柱投影（Equirectangular）是一种常见的环境贴图格式。
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图
     * @param src 源等距圆柱投影图像
     */
    static void equirectangularToCubemap(utils::JobSystem& js, Cubemap& dst, const Image& src);

    /**
     * 将立方体贴图转换为等距圆柱投影图像
     * 
     * @param js 作业系统
     * @param dst 目标等距圆柱投影图像
     * @param src 源立方体贴图
     */
    static void cubemapToEquirectangular(utils::JobSystem& js, Image& dst, const Cubemap& src);

    /**
     * 将立方体贴图转换为八面体投影图像
     * 
     * 八面体投影是一种替代的球面投影方法。
     * 
     * @param js 作业系统
     * @param dst 目标八面体投影图像
     * @param src 源立方体贴图
     */
    static void cubemapToOctahedron(utils::JobSystem& js, Image& dst, const Cubemap& src);

    /**
     * 在水平方向镜像立方体贴图
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图（镜像后的结果）
     * @param src 源立方体贴图
     */
    static void mirrorCubemap(utils::JobSystem& js, Cubemap& dst, const Cubemap& src);

    /**
     * 在立方体贴图中生成UV网格（用于调试）
     * 
     * 在立方体贴图上绘制UV网格，便于可视化纹理坐标。
     * 
     * @param js 作业系统
     * @param cml 目标立方体贴图
     * @param gridFrequencyX X方向网格频率
     * @param gridFrequencyY Y方向网格频率
     */
    static void generateUVGrid(utils::JobSystem& js, Cubemap& cml, size_t gridFrequencyX, size_t gridFrequencyY);

#endif

    friend class CubemapIBL;
};


} // namespace ibl
} // namespace filament

#endif /* IBL_CUBEMAP_UTILS_H */
