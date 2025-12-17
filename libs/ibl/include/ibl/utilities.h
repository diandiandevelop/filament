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

#ifndef IBL_UTILITIES_H
#define IBL_UTILITIES_H

#include <math.h>

#include <math/vec2.h>
#include <math/vec3.h>

namespace filament {
namespace ibl {

/**
 * 计算平方（模板函数）
 * 
 * @param x 输入值
 * @return x的平方
 */
template<typename T>
static inline constexpr T sq(T x) {
    return x * x;
}

/**
 * 计算以4为底的对数（模板函数）
 * 
 * 计算log4(x) = log2(x) / log2(4) = log2(x) / 2
 * 
 * @param x 输入值
 * @return log4(x)
 */
template<typename T>
static inline constexpr T log4(T x) {
    // log2(x)/log2(4)
    // log2(x)/2
    return std::log2(x) * T(0.5);
}

/**
 * 检查是否为2的幂（Power Of Two）
 * 
 * 执行步骤：
 * 1. 使用位运算技巧：x & (x - 1) == 0 当且仅当x是2的幂
 * 
 * @param x 要检查的值
 * @return true如果是2的幂，false否则
 */
inline bool isPOT(size_t x) {
    return !(x & (x - 1));
}

/**
 * 生成Hammersley序列点
 * 
 * Hammersley序列是一种低差异序列（Low-Discrepancy Sequence），
 * 用于在单位正方形内生成均匀分布的采样点，常用于蒙特卡洛积分。
 * 
 * 执行步骤：
 * 1. 第一个坐标：i * iN（均匀分布）
 * 2. 第二个坐标：通过位反转（bit reversal）生成
 * 
 * @param i 序列索引
 * @param iN 1/N（N为总采样数）
 * @return Hammersley点（float2，范围[0, 1]）
 */
inline filament::math::float2 hammersley(uint32_t i, float iN) {
    constexpr float tof = 0.5f / 0x80000000U;  // 转换为浮点数的因子
    uint32_t bits = i;
    // 位反转操作：通过多次交换位来反转32位整数的位序
    bits = (bits << 16u) | (bits >> 16u);  // 交换高16位和低16位
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);  // 交换相邻位对
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);  // 交换相邻2位组
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);  // 交换相邻4位组
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);  // 交换相邻8位组
    return { i * iN, bits * tof };  // 返回Hammersley点
}

} // namespace ibl
} // namespace filament
#endif /* IBL_UTILITIES_H */
