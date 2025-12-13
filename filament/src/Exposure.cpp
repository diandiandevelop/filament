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

#include <filament/Exposure.h>

#include "details/Camera.h"

#include <cmath>

namespace filament {
namespace Exposure {

/**
 * 从相机计算 EV100
 * 
 * 从相机的曝光参数（光圈、快门速度、感光度）计算 EV100 值。
 * 
 * @param c 相机引用
 * @return EV100 值
 * 
 * 实现：
 * 1. 将相机对象转换为内部实现类
 * 2. 获取相机的光圈、快门速度和感光度
 * 3. 调用 ev100() 函数计算 EV100
 */
float ev100(const Camera& c) noexcept {
    const FCamera& camera = downcast(c);  // 转换为内部实现类
    return ev100(camera.getAperture(), camera.getShutterSpeed(), camera.getSensitivity());
}

/**
 * 计算 EV100（曝光值，ISO 100）
 * 
 * 使用光圈、快门速度和感光度计算 EV100。
 * 
 * 公式推导：
 * 设 N = 光圈，t = 快门速度，S = 感光度
 * 
 * EVs = log2(N^2 / t)
 * EVs = EV100 + log2(S / 100)
 * 
 * 因此：
 * EV100 = EVs - log2(S / 100)
 * EV100 = log2(N^2 / t) - log2(S / 100)
 * EV100 = log2((N^2 / t) * (100 / S))
 * 
 * 参考：https://en.wikipedia.org/wiki/Exposure_value
 * 
 * @param aperture 光圈（f-stop）
 * @param shutterSpeed 快门速度（秒）
 * @param sensitivity 感光度（ISO）
 * @return EV100 值
 */
float ev100(float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    /**
     * 计算 EV100（曝光值，ISO 100）
     * 
     * 使用光圈、快门速度和感光度计算 EV100。
     * 
     * 公式推导：
     * 设 N = 光圈，t = 快门速度，S = 感光度
     * 
     * EVs = log2(N^2 / t)
     * EVs = EV100 + log2(S / 100)
     * 
     * 因此：
     * EV100 = EVs - log2(S / 100)
     * EV100 = log2(N^2 / t) - log2(S / 100)
     * EV100 = log2((N^2 / t) * (100 / S))
     * 
     * 变量说明：
     * - aperture: 光圈值（f-stop），例如 f/2.8, f/4.0
     * - shutterSpeed: 快门速度（秒），例如 1/60, 1/125
     * - sensitivity: 感光度（ISO），例如 100, 200, 400
     * 
     * 计算步骤：
     * 1. aperture * aperture: 计算 N^2
     * 2. / shutterSpeed: 除以快门速度
     * 3. * 100.0f / sensitivity: 转换为 ISO 100 基准
     * 4. std::log2(...): 计算以 2 为底的对数
     * 
     * 参考：https://en.wikipedia.org/wiki/Exposure_value
     */
    return std::log2((aperture * aperture) / shutterSpeed * 100.0f / sensitivity);
}

/**
 * 从亮度计算 EV100
 * 
 * 设 L 为平均场景亮度，S 为感光度，K 为反射光表校准常数：
 * 
 * EV = log2(L * S / K)
 * 
 * 使用常用值 K = 12.5（以匹配标准相机制造商设置），计算 EV100：
 * 
 * EV100 = log2(L * 100 / 12.5)
 * 
 * 参考：https://en.wikipedia.org/wiki/Exposure_value
 * 
 * @param luminance 亮度（cd/m²）
 * @return EV100 值
 */
float ev100FromLuminance(float const luminance) noexcept {
    /**
     * 从亮度计算 EV100
     * 
     * 设 L 为平均场景亮度，S 为感光度，K 为反射光表校准常数：
     * 
     * EV = log2(L * S / K)
     * 
     * 使用常用值 K = 12.5（以匹配标准相机制造商设置），计算 EV100：
     * 
     * EV100 = log2(L * 100 / 12.5)
     * EV100 = log2(L * 8)
     * 
     * 变量说明：
     * - luminance: 平均场景亮度（cd/m²，坎德拉每平方米）
     * - 100.0f / 12.5f = 8.0f: 将 ISO 100 和 K=12.5 合并为常数
     * 
     * 计算步骤：
     * 1. luminance * (100.0f / 12.5f): 计算 L * 100 / K
     * 2. std::log2(...): 计算以 2 为底的对数
     * 
     * 参考：https://en.wikipedia.org/wiki/Exposure_value
     */
    return std::log2(luminance * (100.0f / 12.5f));
}

/**
 * 从照度计算 EV100
 * 
 * 设 E 为照度，S 为感光度，C 为入射光表校准常数：
 * 
 * EV = log2(E * S / C)
 * 或
 * EV100 = log2(E * 100 / C)
 * 
 * 使用 C = 250（平面传感器的典型值），EV100 和照度之间的关系为：
 * 
 * EV100 = log2(E * 100 / 250)
 * 
 * 参考：https://en.wikipedia.org/wiki/Exposure_value
 * 
 * @param illuminance 照度（lux）
 * @return EV100 值
 */
float ev100FromIlluminance(float const illuminance) noexcept {
    /**
     * 从照度计算 EV100
     * 
     * 设 E 为照度，S 为感光度，C 为入射光表校准常数：
     * 
     * EV = log2(E * S / C)
     * 或
     * EV100 = log2(E * 100 / C)
     * 
     * 使用 C = 250（平面传感器的典型值），EV100 和照度之间的关系为：
     * 
     * EV100 = log2(E * 100 / 250)
     * EV100 = log2(E * 0.4)
     * 
     * 变量说明：
     * - illuminance: 照度（lux，勒克斯）
     * - 100.0f / 250.0f = 0.4f: 将 ISO 100 和 C=250 合并为常数
     * 
     * 计算步骤：
     * 1. illuminance * (100.0f / 250.0f): 计算 E * 100 / C
     * 2. std::log2(...): 计算以 2 为底的对数
     * 
     * 参考：https://en.wikipedia.org/wiki/Exposure_value
     */
    return std::log2(illuminance * (100.0f / 250.0f));
}

/**
 * 从相机计算曝光值
 * 
 * @param c 相机引用
 * @return 曝光值（用于着色器）
 */
float exposure(const Camera& c) noexcept {
    const FCamera& camera = downcast(c);
    return exposure(camera.getAperture(), camera.getShutterSpeed(), camera.getSensitivity());
}

/**
 * 计算曝光值（从相机参数）
 * 
 * 这等价于调用 exposure(ev100(N, t, S))。
 * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用。
 * 
 * @param aperture 光圈
 * @param shutterSpeed 快门速度
 * @param sensitivity 感光度
 * @return 曝光值
 */
float exposure(float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    /**
     * 计算曝光值（从相机参数）
     * 
     * 这等价于调用 exposure(ev100(N, t, S))。
     * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用，提高性能。
     * 
     * 变量说明：
     * - aperture: 光圈值（f-stop）
     * - shutterSpeed: 快门速度（秒）
     * - sensitivity: 感光度（ISO）
     * 
     * 计算步骤：
     * 1. (aperture * aperture) / shutterSpeed * 100.0f / sensitivity: 
     *    计算 (N^2 / t) * (100 / S)，这是 EV100 公式的内部部分
     * 2. 1.0f / (1.2f * e): 
     *    计算曝光值，其中 1.2 是传感器饱和常数
     * 
     * 曝光值公式：
     * exposure = 1.0 / (1.2 * 2^EV100)
     * 由于 e = 2^EV100（从 EV100 公式推导），所以：
     * exposure = 1.0 / (1.2 * e)
     */
    const float e = (aperture * aperture) / shutterSpeed * 100.0f / sensitivity;
    return 1.0f / (1.2f * e);
}

/**
 * 计算曝光值（从 EV100）
 * 
 * 光度曝光 H 定义为：
 * 
 * H = (q * t / (N^2)) * L
 * 
 * 其中 t 是快门速度，N 是光圈，L 是入射亮度，q 是镜头和渐晕衰减。
 * q 的典型值是 0.65（参见下面的参考链接）。
 * 
 * 传感器记录的 H 值取决于传感器的感光度。
 * 找到该值的简单方法是使用基于饱和的感光度方法：
 * 
 * S_sat = 78 / H_sat
 * 
 * 此方法定义了不会导致裁剪或光晕的最大可能曝光。
 * 
 * 选择因子 78 是为了使基于标准测光表和 18% 反射表面的曝光设置
 * 将产生灰度为 18% * sqrt(2) = 12.7% 饱和度的图像。
 * sqrt(2) 因子用于考虑额外的半档余量以处理镜面反射。
 * 
 * 使用 H 和 S_sat 的定义，我们可以推导出计算使传感器饱和的最大亮度的公式：
 * 
 * H_sat = 78 / S_sat
 * (q * t / (N^2)) * Lmax = 78 / S
 * Lmax = (78 / S) * (N^2 / (q * t))
 * Lmax = (78 / (S * q)) * (N^2 / t)
 * 
 * 当 q = 0.65，S = 100 且 EVs = log2(N^2 / t)（在这种情况下 EVs = EV100）时：
 * 
 * Lmax = (78 / (100 * 0.65)) * 2^EV100
 * Lmax = 1.2 * 2^EV100
 * 
 * 片段着色器中像素的值可以通过将像素位置的入射亮度 L
 * 与最大亮度 Lmax 归一化来计算。
 * 
 * 参考：https://en.wikipedia.org/wiki/Film_speed
 * 
 * @param ev100 EV100 值
 * @return 曝光值（用于着色器）
 */
float exposure(float const ev100) noexcept {
    /**
     * 计算曝光值（从 EV100）
     * 
     * 光度曝光 H 定义为：
     * 
     * H = (q * t / (N^2)) * L
     * 
     * 其中 t 是快门速度，N 是光圈，L 是入射亮度，q 是镜头和渐晕衰减。
     * q 的典型值是 0.65（参见下面的参考链接）。
     * 
     * 传感器记录的 H 值取决于传感器的感光度。
     * 找到该值的简单方法是使用基于饱和的感光度方法：
     * 
     * S_sat = 78 / H_sat
     * 
     * 此方法定义了不会导致裁剪或光晕的最大可能曝光。
     * 
     * 选择因子 78 是为了使基于标准测光表和 18% 反射表面的曝光设置
     * 将产生灰度为 18% * sqrt(2) = 12.7% 饱和度的图像。
     * sqrt(2) 因子用于考虑额外的半档余量以处理镜面反射。
     * 
     * 使用 H 和 S_sat 的定义，我们可以推导出计算使传感器饱和的最大亮度的公式：
     * 
     * H_sat = 78 / S_sat
     * (q * t / (N^2)) * Lmax = 78 / S
     * Lmax = (78 / S) * (N^2 / (q * t))
     * Lmax = (78 / (S * q)) * (N^2 / t)
     * 
     * 当 q = 0.65，S = 100 且 EVs = log2(N^2 / t)（在这种情况下 EVs = EV100）时：
     * 
     * Lmax = (78 / (100 * 0.65)) * 2^EV100
     * Lmax = 1.2 * 2^EV100
     * 
     * 片段着色器中像素的值可以通过将像素位置的入射亮度 L
     * 与最大亮度 Lmax 归一化来计算。
     * 
     * 曝光值 = 1.0 / Lmax = 1.0 / (1.2 * 2^EV100)
     * 
     * 变量说明：
     * - ev100: EV100 值（曝光值，ISO 100 基准）
     * 
     * 计算步骤：
     * 1. std::pow(2.0f, ev100): 计算 2^EV100
     * 2. 1.2f * ...: 乘以传感器饱和常数
     * 3. 1.0f / ...: 计算倒数得到曝光值
     * 
     * 参考：https://en.wikipedia.org/wiki/Film_speed
     */
    return 1.0f / (1.2f * std::pow(2.0f, ev100));
}

/**
 * 从相机计算亮度
 * 
 * @param c 相机引用
 * @return 亮度（cd/m²）
 */
float luminance(const Camera& c) noexcept {
    const FCamera& camera = downcast(c);
    return luminance(camera.getAperture(), camera.getShutterSpeed(), camera.getSensitivity());
}

/**
 * 计算亮度（从相机参数）
 * 
 * 这等价于调用 luminance(ev100(N, t, S))。
 * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用。
 * 
 * @param aperture 光圈
 * @param shutterSpeed 快门速度
 * @param sensitivity 感光度
 * @return 亮度（cd/m²）
 */
float luminance(float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    /**
     * 计算亮度（从相机参数）
     * 
     * 这等价于调用 luminance(ev100(N, t, S))。
     * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用，提高性能。
     * 
     * 变量说明：
     * - aperture: 光圈值（f-stop）
     * - shutterSpeed: 快门速度（秒）
     * - sensitivity: 感光度（ISO）
     * 
     * 计算步骤：
     * 1. (aperture * aperture) / shutterSpeed * 100.0f / sensitivity: 
     *    计算 (N^2 / t) * (100 / S)，这是 EV100 公式的内部部分
     * 2. e * 0.125f: 
     *    将 EV100 的内部值转换为亮度
     *    0.125 = 12.5 / 100，其中 12.5 是反射光表校准常数 K
     * 
     * 亮度公式：
     * L = 2^EV100 * K / 100 = 2^EV100 * 0.125
     * 由于 e = 2^EV100（从 EV100 公式推导），所以：
     * L = e * 0.125
     */
    const float e = (aperture * aperture) / shutterSpeed * 100.0f / sensitivity;
    return e * 0.125f;
}

/**
 * 计算亮度（从 EV100）
 * 
 * 设 L 为平均场景亮度，S 为感光度，K 为反射光表校准常数：
 * 
 * EV = log2(L * S / K)
 * L = 2^EV100 * K / 100
 * 
 * 与 ev100FromLuminance(luminance) 一样，我们使用 K = 12.5 以匹配常见相机制造商
 * （佳能、尼康和世光）：
 * 
 * L = 2^EV100 * 12.5 / 100 = 2^EV100 * 0.125
 * 
 * 由于 log2(0.125) = -3，我们有：
 * 
 * L = 2^(EV100 - 3)
 * 
 * 参考：https://en.wikipedia.org/wiki/Exposure_value
 * 
 * @param ev100 EV100 值
 * @return 亮度（cd/m²）
 */
float luminance(float const ev100) noexcept {
    /**
     * 计算亮度（从 EV100）
     * 
     * 设 L 为平均场景亮度，S 为感光度，K 为反射光表校准常数：
     * 
     * EV = log2(L * S / K)
     * L = 2^EV100 * K / 100
     * 
     * 与 ev100FromLuminance(luminance) 一样，我们使用 K = 12.5 以匹配常见相机制造商
     * （佳能、尼康和世光）：
     * 
     * L = 2^EV100 * 12.5 / 100 = 2^EV100 * 0.125
     * 
     * 由于 log2(0.125) = -3，我们有：
     * 
     * L = 2^(EV100 - 3)
     * 
     * 变量说明：
     * - ev100: EV100 值（曝光值，ISO 100 基准）
     * 
     * 计算步骤：
     * 1. ev100 - 3.0f: 减去 log2(0.125) = -3
     * 2. std::pow(2.0f, ...): 计算 2^(EV100 - 3)
     * 
     * 参考：https://en.wikipedia.org/wiki/Exposure_value
     */
    return std::pow(2.0f, ev100 - 3.0f);
}

/**
 * 从相机计算照度
 * 
 * @param c 相机引用
 * @return 照度（lux）
 */
float illuminance(const Camera& c) noexcept {
    const FCamera& camera = downcast(c);
    return illuminance(camera.getAperture(), camera.getShutterSpeed(), camera.getSensitivity());
}

/**
 * 计算照度（从相机参数）
 * 
 * 这等价于调用 illuminance(ev100(N, t, S))。
 * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用。
 * 
 * @param aperture 光圈
 * @param shutterSpeed 快门速度
 * @param sensitivity 感光度
 * @return 照度（lux）
 */
float illuminance(float const aperture, float const shutterSpeed, float const sensitivity) noexcept {
    /**
     * 计算照度（从相机参数）
     * 
     * 这等价于调用 illuminance(ev100(N, t, S))。
     * 通过合并两个调用，我们可以移除额外的 pow()/log2() 调用，提高性能。
     * 
     * 变量说明：
     * - aperture: 光圈值（f-stop）
     * - shutterSpeed: 快门速度（秒）
     * - sensitivity: 感光度（ISO）
     * 
     * 计算步骤：
     * 1. (aperture * aperture) / shutterSpeed * 100.0f / sensitivity: 
     *    计算 (N^2 / t) * (100 / S)，这是 EV100 公式的内部部分
     * 2. 2.5f * e: 
     *    将 EV100 的内部值转换为照度
     *    2.5 = 250 / 100，其中 250 是入射光表校准常数 C
     * 
     * 照度公式：
     * E = 2^EV100 * C / 100 = 2^EV100 * 2.5
     * 由于 e = 2^EV100（从 EV100 公式推导），所以：
     * E = e * 2.5
     */
    const float e = (aperture * aperture) / shutterSpeed * 100.0f / sensitivity;
    return 2.5f * e;
}

/**
 * 计算照度（从 EV100）
 * 
 * 设 E 为照度，S 为感光度，C 为入射光表校准常数，曝光值可以这样计算：
 * 
 * EV = log2(E * S / C)
 * 或
 * EV100 = log2(E * 100 / C)
 * 
 * 使用 C = 250（平面传感器的典型值），EV100 和照度之间的关系为：
 * 
 * EV100 = log2(E * 100 / 250)
 * E = 2^EV100 / (100 / 250)
 * E = 2.5 * 2^EV100
 * 
 * 参考：https://en.wikipedia.org/wiki/Exposure_value
 * 
 * @param ev100 EV100 值
 * @return 照度（lux）
 */
float illuminance(float const ev100) noexcept {
    /**
     * 计算照度（从 EV100）
     * 
     * 设 E 为照度，S 为感光度，C 为入射光表校准常数，曝光值可以这样计算：
     * 
     * EV = log2(E * S / C)
     * 或
     * EV100 = log2(E * 100 / C)
     * 
     * 使用 C = 250（平面传感器的典型值），EV100 和照度之间的关系为：
     * 
     * EV100 = log2(E * 100 / 250)
     * E = 2^EV100 / (100 / 250)
     * E = 2.5 * 2^EV100
     * 
     * 变量说明：
     * - ev100: EV100 值（曝光值，ISO 100 基准）
     * 
     * 计算步骤：
     * 1. std::pow(2.0f, ev100): 计算 2^EV100
     * 2. 2.5f * ...: 乘以入射光表校准常数（250 / 100 = 2.5）
     * 
     * 参考：https://en.wikipedia.org/wiki/Exposure_value
     */
    return 2.5f * std::pow(2.0f, ev100);
}

} // namespace Exposure
} // namespace filament
