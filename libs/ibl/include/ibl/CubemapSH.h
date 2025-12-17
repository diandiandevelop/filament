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

#ifndef IBL_CUBEMAPSH_H
#define IBL_CUBEMAPSH_H


#include <utils/compiler.h>

#include <math/mat3.h>
#include <math/vec3.h>

#include <memory>
#include <vector>

namespace utils {
class JobSystem;
} // namespace utils

namespace filament {
namespace ibl {

class Cubemap;

/**
 * CubemapSH - 立方体贴图球谐函数类
 * 
 * 用于计算和渲染球谐函数（Spherical Harmonics，SH）。
 * 球谐函数是一种用于表示球面上函数的数学方法，常用于环境光照的压缩表示。
 */
class UTILS_PUBLIC CubemapSH {
public:
    /**
     * 计算给定立方体贴图的球谐函数分解
     * 
     * 将立方体贴图分解为球谐函数系数。可选地通过与截断余弦函数卷积来计算辐照度。
     * 
     * @param js 作业系统（用于并行处理）
     * @param cm 源立方体贴图
     * @param numBands 球谐函数的阶数（通常为3，对应9个系数）
     * @param irradiance 是否计算辐照度（与截断余弦函数卷积）
     * @return 球谐函数系数数组（每个系数为float3，RGB格式）
     */
    static std::unique_ptr<math::float3[]> computeSH(
            utils::JobSystem& js, const Cubemap& cm, size_t numBands, bool irradiance);

    /**
     * 将给定的球谐函数渲染到立方体贴图
     * 
     * 从球谐函数系数重建立方体贴图，用于可视化或验证。
     * 
     * @param js 作业系统
     * @param cm 目标立方体贴图
     * @param sh 球谐函数系数数组
     * @param numBands 球谐函数的阶数
     */
    static void renderSH(utils::JobSystem& js, Cubemap& cm,
            const std::unique_ptr<math::float3[]>& sh, size_t numBands);

    /**
     * 对球谐函数系数应用窗口函数
     * 
     * 使用窗口函数平滑球谐函数系数，减少高频噪声。
     * 
     * @param sh 球谐函数系数数组（会被修改）
     * @param numBands 球谐函数的阶数
     * @param cutoff 截止频率
     */
    static void windowSH(std::unique_ptr<math::float3[]>& sh, size_t numBands, float cutoff);

    /**
     * 预处理球谐函数以供着色器使用
     * 
     * 计算给定立方体贴图的辐照度球谐函数。
     * SH基函数被预缩放以便着色器更容易渲染。结果系数不是标准球谐函数
     * （因为它们被各种因子缩放）。特别是它们不能用上面的renderSH()渲染。
     * 应该使用renderPreScaledSH3Bands()，这正是我们着色器运行的代码。
     * 
     * @param sh 球谐函数系数数组（会被修改，预缩放）
     */
    static void preprocessSHForShader(std::unique_ptr<math::float3[]>& sh);

    /**
     * 渲染预缩放的辐照度球谐函数
     * 
     * 将预缩放的球谐函数系数渲染到立方体贴图。
     * 这是着色器中使用的确切代码。
     * 
     * @param js 作业系统
     * @param cm 目标立方体贴图
     * @param sh 预缩放的球谐函数系数数组
     */
    static void renderPreScaledSH3Bands(utils::JobSystem& js, Cubemap& cm,
            const std::unique_ptr<math::float3[]>& sh);

    /**
     * 获取球谐函数索引
     * 
     * 根据球谐函数的阶数l和次数m计算一维索引。
     * 
     * @param m 次数（-l到l）
     * @param l 阶数
     * @return 一维索引
     */
    static constexpr size_t getShIndex(ssize_t m, size_t l) {
        return SHindex(m, l);
    }

private:
    /**
     * float5 - 5维浮点向量类
     * 
     * 用于球谐函数计算中的5维向量操作。
     */
    class float5 {
        float v[5];  // 5个浮点数值
    public:
        /**
         * 默认构造函数
         */
        float5() = default;
        
        /**
         * 构造函数
         * 
         * @param a 第一个分量
         * @param b 第二个分量
         * @param c 第三个分量
         * @param d 第四个分量
         * @param e 第五个分量
         */
        constexpr float5(float a, float b, float c, float d, float e) : v{ a, b, c, d, e } {}
        
        /**
         * 常量索引运算符
         * 
         * @param i 索引（0-4）
         * @return 对应分量的常量引用
         */
        constexpr float operator[](size_t i) const { return v[i]; }
        
        /**
         * 索引运算符
         * 
         * @param i 索引（0-4）
         * @return 对应分量的引用
         */
        float& operator[](size_t i) { return v[i]; }
    };

    /**
     * 矩阵向量乘法（内联函数）
     * 
     * 计算5x5矩阵与5维向量的乘积。
     * 
     * @param M 5x5矩阵（数组形式）
     * @param x 5维向量
     * @return 矩阵向量乘积结果
     */
    static inline const float5 multiply(const float5 M[5], float5 x) noexcept {
        return float5{
                M[0][0] * x[0] + M[1][0] * x[1] + M[2][0] * x[2] + M[3][0] * x[3] + M[4][0] * x[4],
                M[0][1] * x[0] + M[1][1] * x[1] + M[2][1] * x[2] + M[3][1] * x[3] + M[4][1] * x[4],
                M[0][2] * x[0] + M[1][2] * x[1] + M[2][2] * x[2] + M[3][2] * x[3] + M[4][2] * x[4],
                M[0][3] * x[0] + M[1][3] * x[1] + M[2][3] * x[2] + M[3][3] * x[3] + M[4][3] * x[4],
                M[0][4] * x[0] + M[1][4] * x[1] + M[2][4] * x[2] + M[3][4] * x[3] + M[4][4] * x[4]
        };
    };

    /**
     * 计算球谐函数索引（内联函数）
     * 
     * 根据球谐函数的阶数l和次数m计算一维索引。
     * 索引公式：l * (l + 1) + m
     * 
     * @param m 次数（-l到l）
     * @param l 阶数
     * @return 一维索引
     */
    static inline constexpr size_t SHindex(ssize_t m, size_t l) {
        return l * (l + 1) + m;
    }

    static void computeShBasis(float* SHb, size_t numBands, const math::float3& s);

    static float Kml(ssize_t m, size_t l);

    static std::vector<float> Ki(size_t numBands);

    static constexpr float computeTruncatedCosSh(size_t l);

    static float sincWindow(size_t l, float w);

    static math::float3 rotateShericalHarmonicBand1(math::float3 band1, math::mat3f const& M);

    static float5 rotateShericalHarmonicBand2(float5 const& band2, math::mat3f const& M);

        // debugging only...
    static float Legendre(ssize_t l, ssize_t m, float x);
    static float TSH(int l, int m, const math::float3& d);
    static void printShBase(std::ostream& out, int l, int m);
};

} // namespace ibl
} // namespace filament

#endif /* IBL_CUBEMAPSH_H */
