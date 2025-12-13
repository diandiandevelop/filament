/*
 * Copyright (C) 2021 The Android Open Source Project
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

#ifndef TNT_FILAMENT_PIDCONTROLLER_H
#define TNT_FILAMENT_PIDCONTROLLER_H

#include <math/scalar.h>

#include <limits>

namespace filament {

/**
 * PID 控制器
 * 
 * 实现比例-积分-微分（PID）控制器，用于自动控制系统。
 * 
 * PID 控制器输出公式：
 * output = Kp * error + Ki * integral + Kd * derivative
 * 
 * 特性：
 * - 支持标准形式和并行形式的增益设置
 * - 积分限幅以防止积分饱和（windup）
 * - 输出限幅
 * - 死区（dead band）
 * - 可选的积分抑制
 */
class PIDController {
public:
    PIDController() noexcept = default;

    /**
     * 设置标准形式的增益
     * 
     * 标准形式：Ki = Kp / Ti, Kd = Kp * Td
     * 
     * @param Kp 比例增益
     * @param Ti 积分时间常数
     * @param Td 微分时间常数
     */
    void setStandardGains(float const Kp, float const Ti, float const Td) noexcept {
        mKp = Kp;
        mKi = Kp / Ti;
        mKd = Kp * Td;
    }

    /**
     * 设置并行形式的增益
     * 
     * 并行形式：直接设置 Kp, Ki, Kd
     * 
     * @param Kp 比例增益
     * @param Ki 积分增益
     * @param Kd 微分增益
     */
    void setParallelGains(float const Kp, float const Ki, float const Kd) noexcept {
        mKp = Kp;
        mKi = Ki;
        mKd = Kd;
    }

    /**
     * 设置输出死区
     * 
     * 在死区内，输出保持稳定（设为 0）。
     * 
     * @param low 死区下限
     * @param high 死区上限
     */
    void setOutputDeadBand(float const low, float const high) noexcept {
        mDeadBandLow = low;
        mDeadBandHigh = high;
    }

    /**
     * 设置积分限幅
     * 
     * 积分限幅用于防止积分饱和（windup）。
     * 
     * @param low 积分下限
     * @param high 积分上限
     */
    void setIntegralLimits(float const low, float const high) noexcept {
        mIntegralLimitLow = low;
        mIntegralLimitHigh = high;
    }

    /**
     * 设置输出限幅
     * 
     * @param low 输出下限
     * @param high 输出上限
     */
    void setOutputLimits(float const low, float const high) noexcept {
        mOutputLimitLow = low;
        mOutputLimitHigh = high;
    }

    /**
     * 启用/禁用积分项
     * 
     * 禁用积分项以防止积分饱和。
     * 
     * @param enabled true 禁用积分项，false 启用积分项
     */
    void setIntegralInhibitionEnabled(bool const enabled) noexcept {
        mIntegralInhibition = enabled ? 0.0f : 1.0f;
    }

    /**
     * 更新 PID 输出
     * 
     * 计算 PID 控制器的输出值。
     * 
     * @param measure 测量值（当前值）
     * @param target 目标值（期望值）
     * @param dt 时间步长（秒）
     * @return PID 控制器输出
     */
    float update(float const measure, float const target, float const dt) const noexcept {
        /**
         * 计算误差
         */
        const float error = target - measure;

        /**
         * 计算误差积分
         */
        float integral = mIntegral + error * mIntegralInhibition * dt;

        /**
         * 计算微分（误差变化率）
         */
        const float derivative = (error - mLastError) / dt;

        /**
         * 防止积分饱和
         */
        integral = math::clamp(integral, mIntegralLimitLow, mIntegralLimitHigh);

        /**
         * PID 控制器输出
         */
        float out = mKp * error + mKi * integral + mKd * derivative;

        /**
         * 应用死区
         */
        if (out > mDeadBandLow && out < mDeadBandHigh) {
            out = 0.0f;
        }

        /**
         * 应用输出限幅
         */
        out = math::clamp(out, mOutputLimitLow, mOutputLimitHigh);

        /**
         * 保存状态供下一轮使用
         */
        mIntegral = integral;
        mLastError = error;
        mDerivative = derivative;

        return out;
    }

    /**
     * 获取当前误差
     * 
     * @return 当前误差值
     */
    float getError() const noexcept {
        return mLastError;
    }

    /**
     * 获取当前积分值
     * 
     * @return 当前积分值
     */
    float getIntegral() const noexcept {
        return mIntegral;
    }

    /**
     * 获取当前微分值
     * 
     * @return 当前微分值
     */
    float getDerivative() const noexcept {
        return mDerivative;
    }

private:
    float mKp = 0.1f;  // 比例增益
    float mKi = 0.0f;  // 积分增益
    float mKd = 0.0f;  // 微分增益
    float mIntegralInhibition = 1.0f;  // 积分抑制因子（0 = 禁用，1 = 启用）
    float mIntegralLimitLow = -std::numeric_limits<float>::infinity();  // 积分下限
    float mIntegralLimitHigh = std::numeric_limits<float>::infinity();  // 积分上限
    float mOutputLimitLow = -std::numeric_limits<float>::infinity();  // 输出下限
    float mOutputLimitHigh = std::numeric_limits<float>::infinity();  // 输出上限
    float mDeadBandLow = 0.0f;  // 死区下限
    float mDeadBandHigh = 0.0f;  // 死区上限
    mutable float mLastError = 0.0f;  // 上次误差（用于计算微分）
    mutable float mIntegral = 0.0f;  // 积分累积值
    mutable float mDerivative = 0.0f;  // 微分值
};

} // namespace filament

#endif // TNT_FILAMENT_PIDCONTROLLER_H
