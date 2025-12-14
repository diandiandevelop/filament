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

#ifndef TNT_FILAMENT_EXPOSURE_H
#define TNT_FILAMENT_EXPOSURE_H

#include <utils/compiler.h>

namespace filament {

class Camera;

/**
 * A series of utilities to compute exposure, exposure value at ISO 100 (EV100),
 * luminance and illuminance using a physically-based camera model.
 */
/**
 * 一系列实用函数，用于使用基于物理的相机模型计算曝光、ISO 100 的曝光值 (EV100)、
 * 亮度和照度
 */
namespace Exposure {

/**
 * Returns the exposure value (EV at ISO 100) of the specified camera.
 */
/**
 * 返回指定相机的曝光值（ISO 100 下的 EV）
 */
UTILS_PUBLIC
float ev100(const Camera& camera) noexcept;

/**
 * Returns the exposure value (EV at ISO 100) of the specified exposure parameters.
 */
/**
 * 返回指定曝光参数的曝光值（ISO 100 下的 EV）
 */
UTILS_PUBLIC
float ev100(float aperture, float shutterSpeed, float sensitivity) noexcept;

/**
 * Returns the exposure value (EV at ISO 100) for the given average luminance (in @f$ \frac{cd}{m^2} @f$).
 */
/**
 * 返回给定平均亮度（单位：@f$ \frac{cd}{m^2} @f$）的曝光值（ISO 100 下的 EV）
 */
UTILS_PUBLIC
float ev100FromLuminance(float luminance) noexcept;

/**
* Returns the exposure value (EV at ISO 100) for the given illuminance (in lux).
*/
/**
 * 返回给定照度（单位：勒克斯）的曝光值（ISO 100 下的 EV）
 */
UTILS_PUBLIC
float ev100FromIlluminance(float illuminance) noexcept;

/**
 * Returns the photometric exposure for the specified camera.
 */
/**
 * 返回指定相机的光度曝光
 */
UTILS_PUBLIC
float exposure(const Camera& camera) noexcept;

/**
 * Returns the photometric exposure for the specified exposure parameters.
 * This function is equivalent to calling `exposure(ev100(aperture, shutterSpeed, sensitivity))`
 * but is slightly faster and offers higher precision.
 */
/**
 * 返回指定曝光参数的光度曝光
 * 此函数等效于调用 `exposure(ev100(aperture, shutterSpeed, sensitivity))`，
 * 但速度稍快且精度更高。
 */
UTILS_PUBLIC
float exposure(float aperture, float shutterSpeed, float sensitivity) noexcept;

/**
 * Returns the photometric exposure for the given EV100.
 */
/**
 * 返回给定 EV100 的光度曝光
 */
UTILS_PUBLIC
float exposure(float ev100) noexcept;

/**
 * Returns the incident luminance in @f$ \frac{cd}{m^2} @f$ for the specified camera acting as a spot meter.
 */
/**
 * 返回作为点测光表的指定相机的入射亮度（单位：@f$ \frac{cd}{m^2} @f$）
 */
UTILS_PUBLIC
float luminance(const Camera& camera) noexcept;

/**
 * Returns the incident luminance in @f$ \frac{cd}{m^2} @f$ for the specified exposure parameters of
 * a camera acting as a spot meter.
 * This function is equivalent to calling `luminance(ev100(aperture, shutterSpeed, sensitivity))`
 * but is slightly faster and offers higher precision.
 */
/**
 * 返回作为点测光表的相机在指定曝光参数下的入射亮度（单位：@f$ \frac{cd}{m^2} @f$）
 * 此函数等效于调用 `luminance(ev100(aperture, shutterSpeed, sensitivity))`，
 * 但速度稍快且精度更高。
 */
UTILS_PUBLIC
float luminance(float aperture, float shutterSpeed, float sensitivity) noexcept;

/**
 * Converts the specified EV100 to luminance in @f$ \frac{cd}{m^2} @f$.
 * EV100 is not a measure of luminance, but an EV100 can be used to denote a
 * luminance for which a camera would use said EV100 to obtain the nominally
 * correct exposure
 */
/**
 * 将指定的 EV100 转换为亮度（单位：@f$ \frac{cd}{m^2} @f$）
 * EV100 不是亮度的度量，但 EV100 可用于表示
 * 相机将使用该 EV100 以获得标称
 * 正确曝光的亮度
 */
UTILS_PUBLIC
float luminance(float ev100) noexcept;

/**
 * Returns the illuminance in lux for the specified camera acting as an incident light meter.
 */
/**
 * 返回作为入射光表的指定相机的照度（单位：勒克斯）
 */
UTILS_PUBLIC
float illuminance(const Camera& camera) noexcept;

/**
 * Returns the illuminance in lux for the specified exposure parameters of
 * a camera acting as an incident light meter.
 * This function is equivalent to calling `illuminance(ev100(aperture, shutterSpeed, sensitivity))`
 * but is slightly faster and offers higher precision.
 */
/**
 * 返回作为入射光表的相机在指定曝光参数下的照度（单位：勒克斯）
 * 此函数等效于调用 `illuminance(ev100(aperture, shutterSpeed, sensitivity))`，
 * 但速度稍快且精度更高。
 */
UTILS_PUBLIC
float illuminance(float aperture, float shutterSpeed, float sensitivity) noexcept;

/**
 * Converts the specified EV100 to illuminance in lux.
 * EV100 is not a measure of illuminance, but an EV100 can be used to denote an
 * illuminance for which a camera would use said EV100 to obtain the nominally
 * correct exposure.
 */
/**
 * 将指定的 EV100 转换为照度（单位：勒克斯）
 * EV100 不是照度的度量，但 EV100 可用于表示
 * 相机将使用该 EV100 以获得标称
 * 正确曝光的照度
 */
UTILS_PUBLIC
float illuminance(float ev100) noexcept;

} // namespace exposure
} // namespace filament

#endif // TNT_FILAMENT_EXPOSURE_H
