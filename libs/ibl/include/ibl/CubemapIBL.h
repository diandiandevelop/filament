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

#ifndef IBL_CUBEMAPIBL_H
#define IBL_CUBEMAPIBL_H

#include <math/vec3.h>

#include <utils/Slice.h>
#include <utils/compiler.h>

#include <vector>

#include <stdint.h>
#include <stddef.h>

namespace utils {
class JobSystem;
} // namespace utils

namespace filament {
namespace ibl {

class Cubemap;
class Image;

/**
 * CubemapIBL - 立方体贴图IBL生成类
 * 
 * 用于生成基于图像的光照（IBL）所需的立方体贴图。
 * 包括粗糙度过滤、漫反射辐照度计算、DFG项计算等功能。
 */
class UTILS_PUBLIC CubemapIBL {
public:
    /**
     * 进度回调函数类型
     * 
     * @param level 当前处理的Mipmap级别
     * @param progress 进度（0.0到1.0）
     * @param userdata 用户数据指针
     */
    typedef void (*Progress)(size_t, float, void*);

    /**
     * 计算粗糙度LOD（使用预过滤重要性采样GGX）
     * 
     * 使用重要性采样和GGX分布函数计算指定粗糙度的预过滤环境贴图。
     * 这是PBR渲染中用于镜面反射的环境贴图预过滤。
     * 
     * @param js 作业系统（用于并行处理）
     * @param dst 目标立方体贴图
     * @param levels 源环境的预过滤LOD列表
     * @param linearRoughness 线性粗糙度值（0.0到1.0）
     * @param maxNumSamples 重要性采样的最大采样数
     * @param mirror 镜像向量（用于反射方向）
     * @param prefilter 是否进行预过滤
     * @param updater 进度回调函数
     * @param userdata 用户数据指针
     */
    static void roughnessFilter(
            utils::JobSystem& js, Cubemap& dst, utils::Slice<const Cubemap> levels,
            float linearRoughness, size_t maxNumSamples, math::float3 mirror, bool prefilter,
            Progress updater = nullptr, void* userdata = nullptr);

    /**
     * 计算粗糙度LOD（重载版本，使用vector）
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图
     * @param levels 源环境的预过滤LOD列表（vector版本）
     * @param linearRoughness 线性粗糙度值
     * @param maxNumSamples 重要性采样的最大采样数
     * @param mirror 镜像向量
     * @param prefilter 是否进行预过滤
     * @param updater 进度回调函数
     * @param userdata 用户数据指针
     */
    static void roughnessFilter(
            utils::JobSystem& js, Cubemap& dst, const std::vector<Cubemap>& levels,
            float linearRoughness, size_t maxNumSamples, math::float3 mirror, bool prefilter,
            Progress updater = nullptr, void* userdata = nullptr);

    /**
     * 计算"分割求和"近似中的"DFG"项并存储到2D图像中
     * 
     * DFG项是PBR渲染中用于计算镜面反射BRDF的项，包括：
     * - D：法线分布函数（GGX）
     * - F：菲涅尔项
     * - G：几何遮蔽项
     * 
     * @param js 作业系统
     * @param dst 目标2D图像（存储DFG查找表）
     * @param multiscatter 是否使用多次散射模型
     * @param cloth 是否使用布料材质模型
     */
    static void DFG(utils::JobSystem& js, Image& dst, bool multiscatter, bool cloth);

    /**
     * 使用预过滤重要性采样GGX计算漫反射辐照度
     * 
     * 计算环境贴图的漫反射辐照度，用于PBR渲染中的漫反射光照。
     * 
     * @note 通常使用球谐函数（Spherical Harmonics）来实现，而不是这个方法。
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图
     * @param levels 源环境的预过滤LOD列表
     * @param maxNumSamples 重要性采样的最大采样数（默认1024）
     * @param updater 进度回调函数
     * @param userdata 用户数据指针
     * 
     * @see CubemapSH
     */
    static void diffuseIrradiance(utils::JobSystem& js, Cubemap& dst, const std::vector<Cubemap>& levels,
            size_t maxNumSamples = 1024, Progress updater = nullptr, void* userdata = nullptr);

    /**
     * 计算BRDF（用于调试，可忽略）
     * 
     * @param js 作业系统
     * @param dst 目标立方体贴图
     * @param linearRoughness 线性粗糙度值
     */
    static void brdf(utils::JobSystem& js, Cubemap& dst, float linearRoughness);
};

} // namespace ibl
} // namespace filament

#endif /* IBL_CUBEMAPIBL_H */
