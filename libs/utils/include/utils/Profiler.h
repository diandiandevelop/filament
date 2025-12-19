/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_UTILS_PROFILER_H
#define TNT_UTILS_PROFILER_H

// 标准库头文件
#include <ratio>                    // 用于时间单位比例（std::nano等）
#include <chrono>                   // 时间库（注意：仅内联使用，安全）

// C标准库头文件
#include <assert.h>                 // 断言
#include <stddef.h>                 // 标准定义（size_t等）
#include <stdint.h>                 // 固定宽度整数类型
#include <string.h>                 // 字符串操作

// Linux平台特定的性能计数器支持
#if defined(__linux__)
#   include <unistd.h>              // POSIX操作系统API
#   include <sys/ioctl.h>           // I/O控制操作
#   include <linux/perf_event.h>    // Linux性能事件接口（perf_event）
#endif

#include <utils/compiler.h>         // 编译器特定宏

namespace utils {

/**
 * Profiler 类 - CPU性能分析器
 * 
 * 功能概述：
 * 使用 Linux perf_event 接口访问硬件性能计数器，测量 CPU 执行性能指标。
 * 在渲染引擎中用于性能分析和优化，可以测量：
 * - CPU指令执行数量
 * - CPU周期数
 * - 缓存命中/未命中率（L1数据缓存、L1指令缓存）
 * - 分支预测命中/未命中率
 * 
 * 工作原理：
 * 1. 通过 perf_event_open 系统调用打开性能计数器文件描述符
 * 2. 使用 ioctl 控制计数器的启动、停止和重置
 * 3. 读取计数器值计算各种性能指标（IPC、CPI、缓存命中率等）
 * 
 * 平台支持：
 * - Linux: 完整支持，使用 perf_event 接口
 * - 其他平台: 空实现，方法不执行任何操作
 * 
 * 使用示例：
 *   Profiler profiler(EV_CPU_CYCLES | EV_L1D_RATES);
 *   profiler.reset();
 *   profiler.start();
 *   // ... 执行要测量的代码 ...
 *   profiler.stop();
 *   Counters counters = profiler.readCounters();
 *   double ipc = counters.getIPC();
 */
class Profiler {
public:
    /**
     * 性能事件类型枚举
     * 定义可测量的硬件性能计数器类型
     */
    enum {
        INSTRUCTIONS    = 0,   // 已执行的指令数量（必须为0，作为基准事件）
        CPU_CYCLES      = 1,   // CPU周期数
        DCACHE_REFS     = 2,   // L1数据缓存引用次数（访问次数）
        DCACHE_MISSES   = 3,   // L1数据缓存未命中次数
        BRANCHES        = 4,   // 分支指令数量
        BRANCH_MISSES   = 5,   // 分支预测未命中次数
        ICACHE_REFS     = 6,   // L1指令缓存引用次数
        ICACHE_MISSES   = 7,   // L1指令缓存未命中次数

        // 必须放在最后，用于确定事件总数
        EVENT_COUNT
    };
	
    /**
     * 性能事件掩码枚举
     * 使用位标志组合多个事件，方便同时启用多个性能计数器
     * 
     * 位操作说明：
     * - 每个事件对应一个位（bit）
     * - 可以使用位或（|）操作组合多个事件
     * - 例如：EV_CPU_CYCLES | EV_L1D_RATES 同时启用CPU周期和L1数据缓存统计
     */
    enum {
        EV_CPU_CYCLES = 1u << CPU_CYCLES,        // CPU周期事件掩码（位1）
        EV_L1D_REFS   = 1u << DCACHE_REFS,        // L1数据缓存引用事件掩码（位2）
        EV_L1D_MISSES = 1u << DCACHE_MISSES,      // L1数据缓存未命中事件掩码（位3）
        EV_BPU_REFS   = 1u << BRANCHES,           // 分支预测单元引用事件掩码（位4）
        EV_BPU_MISSES = 1u << BRANCH_MISSES,      // 分支预测未命中事件掩码（位5）
        EV_L1I_REFS   = 1u << ICACHE_REFS,        // L1指令缓存引用事件掩码（位6）
        EV_L1I_MISSES = 1u << ICACHE_MISSES,      // L1指令缓存未命中事件掩码（位7）
        
        // 辅助组合掩码：同时启用引用和未命中事件，用于计算命中率
        EV_L1D_RATES = EV_L1D_REFS | EV_L1D_MISSES,  // L1数据缓存完整统计
        EV_L1I_RATES = EV_L1I_REFS | EV_L1I_MISSES,  // L1指令缓存完整统计
        EV_BPU_RATES = EV_BPU_REFS | EV_BPU_MISSES,  // 分支预测完整统计
    };

    /**
     * 默认构造函数
     * 
     * 注意：构造后必须调用 resetEvents() 来设置要启用的事件
     * 否则性能计数器将不会工作
     */
    Profiler() noexcept;
    
    /**
     * 带事件掩码的构造函数
     * 
     * @param eventMask 要启用的性能事件掩码，使用 EV_* 枚举值组合
     *                  例如：EV_CPU_CYCLES | EV_L1D_RATES
     * 
     * 实现过程：
     * 1. 根据 eventMask 打开对应的性能计数器文件描述符
     * 2. 初始化内部状态
     * 3. 在 Linux 平台上，使用 perf_event_open 系统调用
     */
    explicit Profiler(uint32_t eventMask) noexcept;
    
    /**
     * 析构函数
     * 
     * 实现过程：
     * 1. 关闭所有打开的性能计数器文件描述符
     * 2. 释放相关资源
     */
    ~Profiler() noexcept;

    // 禁止拷贝和移动操作（性能分析器不应该被复制）
    Profiler(const Profiler& rhs) = delete;
    Profiler(Profiler&& rhs) = delete;
    Profiler& operator=(const Profiler& rhs) = delete;
    Profiler& operator=(Profiler&& rhs) = delete;

    /**
     * 重置并设置要启用的性能事件
     * 
     * 实现过程：
     * 1. 关闭之前打开的所有性能计数器
     * 2. 根据新的 eventMask 重新打开对应的性能计数器
     * 3. 更新内部状态
     * 
     * @param eventMask 要启用的性能事件掩码，使用 EV_* 枚举值组合
     * @return 返回实际启用的事件掩码（可能与输入不同，如果某些事件不支持）
     * 
     * 使用场景：
     * - 在运行时动态改变要测量的性能指标
     * - 重新初始化性能分析器
     */
    uint32_t resetEvents(uint32_t eventMask) noexcept;

    /**
     * 获取当前启用的事件掩码
     * 
     * @return 返回当前启用的事件掩码位标志
     */
    uint32_t getEnabledEvents() const noexcept { return mEnabledEvents; }

    /**
     * 检查性能分析器是否有效
     * 
     * 返回值说明：
     * - true: 性能计数器已成功初始化，可以使用
     * - false: 性能计数器未初始化或不支持（可能的原因：
     *           1. 系统不支持 perf_event
     *           2. 权限不足（需要 CAP_PERFMON 或 CAP_SYS_ADMIN）
     *           3. 某些事件在当前硬件上不可用）
     * 
     * @return 如果性能计数器文件描述符有效返回 true，否则返回 false
     */
    bool isValid() const { return mCountersFd[0] >= 0; }

    /**
     * Counters 类 - 性能计数器数据容器
     * 
     * 功能：
     * 存储从硬件性能计数器读取的原始数据，并提供便捷的方法计算各种性能指标。
     * 
     * 使用方式：
     * 1. 调用 readCounters() 读取当前计数器值
     * 2. 使用两个 Counters 对象相减计算时间段的增量
     * 3. 调用各种 get*() 方法获取性能指标
     */
    class Counters {
        friend class Profiler;  // 允许 Profiler 访问私有成员

        /**
         * 性能计数器组中的事件数量
         * 通常等于启用的事件数量
         */
        uint64_t nr;
        
        /**
         * 性能计数器启用的总时间（纳秒）
         * 表示从计数器启用到读取时经过的墙上时钟时间
         */
        uint64_t time_enabled;
        
        /**
         * 性能计数器实际运行的总时间（纳秒）
         * 由于多路复用，可能小于 time_enabled
         * 如果多个事件共享硬件计数器，会进行时间分片
         */
        uint64_t time_running;
        
        /**
         * 各个性能事件的计数器值数组
         * 每个元素包含：
         * - value: 计数器的原始值
         * - id: 计数器的事件ID（用于多路复用时的关联）
         */
        struct {
            uint64_t value;  // 计数器的原始计数值
            uint64_t id;     // 事件ID（perf_event 使用）
        } counters[EVENT_COUNT];

        /**
         * 减法运算符重载
         * 
         * 功能：计算两个计数器之间的差值，用于测量时间段内的性能指标增量
         * 
         * 实现过程：
         * 1. 对每个字段执行减法操作
         * 2. 遍历所有事件计数器，计算每个事件的增量
         * 3. 返回包含差值的新 Counters 对象
         * 
         * @param lhs 左操作数（被减数），会被修改
         * @param rhs 右操作数（减数），保持不变
         * @return 返回包含差值的 Counters 对象
         * 
         * 使用示例：
         *   Counters start = profiler.readCounters();
         *   // ... 执行代码 ...
         *   Counters end = profiler.readCounters();
         *   Counters delta = end - start;  // 计算增量
         */
        friend Counters operator-(Counters lhs, const Counters& rhs) noexcept {
            lhs.nr -= rhs.nr;                          // 事件数量差值
            lhs.time_enabled -= rhs.time_enabled;      // 启用时间差值
            lhs.time_running -= rhs.time_running;      // 运行时间差值
            // 计算每个性能事件的增量
            for (size_t i = 0; i < EVENT_COUNT; ++i) {
                lhs.counters[i].value -= rhs.counters[i].value;
            }
            return lhs;
        }

    public:
        /**
         * 获取已执行的指令数量
         * @return 返回指令计数的原始值
         */
        uint64_t getInstructions() const        { return counters[INSTRUCTIONS].value; }
        
        /**
         * 获取CPU周期数
         * @return 返回CPU周期计数的原始值
         */
        uint64_t getCpuCycles() const           { return counters[CPU_CYCLES].value; }
        
        /**
         * 获取L1数据缓存引用次数
         * @return 返回L1数据缓存访问次数的原始值
         */
        uint64_t getL1DReferences() const       { return counters[DCACHE_REFS].value; }
        
        /**
         * 获取L1数据缓存未命中次数
         * @return 返回L1数据缓存未命中次数的原始值
         */
        uint64_t getL1DMisses() const           { return counters[DCACHE_MISSES].value; }
        
        /**
         * 获取L1指令缓存引用次数
         * @return 返回L1指令缓存访问次数的原始值
         */
        uint64_t getL1IReferences() const       { return counters[ICACHE_REFS].value; }
        
        /**
         * 获取L1指令缓存未命中次数
         * @return 返回L1指令缓存未命中次数的原始值
         */
        uint64_t getL1IMisses() const           { return counters[ICACHE_MISSES].value; }
        
        /**
         * 获取分支指令数量
         * @return 返回分支指令计数的原始值
         */
        uint64_t getBranchInstructions() const  { return counters[BRANCHES].value; }
        
        /**
         * 获取分支预测未命中次数
         * @return 返回分支预测未命中次数的原始值
         */
        uint64_t getBranchMisses() const        { return counters[BRANCH_MISSES].value; }

        /**
         * 获取墙上时钟时间（Wall Time）
         * 
         * 墙上时钟时间：从性能计数器启用到读取时经过的实际时间
         * 
         * @return 返回时间间隔，单位为纳秒
         */
        std::chrono::duration<uint64_t, std::nano> getWallTime() const {
            return std::chrono::duration<uint64_t, std::nano>(time_enabled);
        }

        /**
         * 获取实际运行时间（Running Time）
         * 
         * 实际运行时间：性能计数器实际计数的累计时间
         * 由于硬件计数器数量有限，多个事件可能共享计数器，导致时间分片
         * 
         * @return 返回时间间隔，单位为纳秒
         */
        std::chrono::duration<uint64_t, std::nano> getRunningTime() const {
            return std::chrono::duration<uint64_t, std::nano>(time_running);
        }

        /**
         * 计算 IPC（Instructions Per Cycle）- 每周期指令数
         * 
         * IPC 是衡量 CPU 效率的重要指标：
         * - IPC > 1: CPU 每个周期执行超过1条指令（超标量架构的优势）
         * - IPC = 1: 每个周期执行1条指令（理想情况）
         * - IPC < 1: 由于流水线停顿、缓存未命中等原因，效率较低
         * 
         * 计算公式：IPC = 指令数 / CPU周期数
         * 
         * @return 返回 IPC 值（浮点数）
         * 
         * 性能分析应用：
         * - 在渲染引擎中，高 IPC 表示代码执行效率高
         * - 低 IPC 可能表示存在缓存未命中、分支预测失败等问题
         */
        double getIPC() const noexcept {
            uint64_t cpuCycles = getCpuCycles();
            uint64_t instructions = getInstructions();
            return double(instructions) / double(cpuCycles);
        }

        /**
         * 计算 CPI（Cycles Per Instruction）- 每指令周期数
         * 
         * CPI 是 IPC 的倒数，表示执行一条指令平均需要的周期数：
         * - CPI < 1: 每个周期执行超过1条指令（超标量）
         * - CPI = 1: 每个周期执行1条指令
         * - CPI > 1: 需要多个周期才能执行一条指令
         * 
         * 计算公式：CPI = CPU周期数 / 指令数
         * 
         * @return 返回 CPI 值（浮点数）
         */
        double getCPI() const noexcept {
            uint64_t cpuCycles = getCpuCycles();
            uint64_t instructions = getInstructions();
            return double(cpuCycles) / double(instructions);
        }

        /**
         * 计算L1数据缓存未命中率
         * 
         * 未命中率 = 未命中次数 / 总访问次数
         * 
         * 性能影响：
         * - 高未命中率会导致频繁访问L2/L3缓存或内存，严重影响性能
         * - 在渲染引擎中，数据访问模式对缓存性能至关重要
         * 
         * @return 返回未命中率（0.0 - 1.0之间的浮点数）
         */
        double getL1DMissRate() const noexcept {
            uint64_t cacheReferences = getL1DReferences();
            uint64_t cacheMisses = getL1DMisses();
            return double(cacheMisses) / double(cacheReferences);
        }

        /**
         * 计算L1数据缓存命中率
         * 
         * 命中率 = 1 - 未命中率
         * 
         * @return 返回命中率（0.0 - 1.0之间的浮点数）
         */
        double getL1DHitRate() const noexcept {
            return 1.0 - getL1DMissRate();
        }

        /**
         * 计算L1指令缓存未命中率
         * 
         * 指令缓存未命中会导致指令获取延迟，影响流水线效率
         * 
         * @return 返回未命中率（0.0 - 1.0之间的浮点数）
         */
        double getL1IMissRate() const noexcept {
            uint64_t cacheReferences = getL1IReferences();
            uint64_t cacheMisses = getL1IMisses();
            return double(cacheMisses) / double(cacheReferences);
        }

        /**
         * 计算L1指令缓存命中率
         * 
         * @return 返回命中率（0.0 - 1.0之间的浮点数）
         */
        double getL1IHitRate() const noexcept {
            return 1.0 - getL1IMissRate();
        }

        /**
         * 计算分支预测未命中率
         * 
         * 分支预测失败会导致流水线清空，造成性能损失
         * 现代CPU的分支预测器通常准确率很高（>95%）
         * 
         * @return 返回未命中率（0.0 - 1.0之间的浮点数）
         */
        double getBranchMissRate() const noexcept {
            uint64_t branchReferences = getBranchInstructions();
            uint64_t branchMisses = getBranchMisses();
            return double(branchMisses) / double(branchReferences);
        }

        /**
         * 计算分支预测命中率
         * 
         * @return 返回命中率（0.0 - 1.0之间的浮点数）
         */
        double getBranchHitRate() const noexcept {
            return 1.0 - getBranchMissRate();
        }

        /**
         * 计算 MPKI（Misses Per Kilo Instructions）- 每千条指令的未命中次数
         * 
         * MPKI 是另一种衡量缓存性能的指标，特别适用于比较不同工作负载：
         * - 较低的 MPKI 表示更好的缓存性能
         * - 可以用于比较不同算法或数据结构的缓存友好性
         * 
         * 计算公式：MPKI = (未命中次数 × 1000) / 指令数
         * 
         * @param misses 未命中次数（可以是任何类型的未命中，如缓存未命中、分支未命中）
         * @return 返回 MPKI 值（浮点数）
         * 
         * 使用示例：
         *   double l1d_mpki = counters.getMPKI(counters.getL1DMisses());
         *   double branch_mpki = counters.getMPKI(counters.getBranchMisses());
         */
        double getMPKI(uint64_t misses) const noexcept {
            return (misses * 1000.0) / getInstructions();
        }
    };

#if defined(__linux__)

    /**
     * 重置所有性能计数器
     * 
     * 实现过程：
     * 1. 获取主计数器文件描述符（mCountersFd[0]）
     * 2. 使用 ioctl 系统调用发送 PERF_EVENT_IOC_RESET 命令
     * 3. PERF_IOC_FLAG_GROUP 标志表示重置整个事件组
     * 
     * 作用：
     * - 将所有计数器的值清零
     * - 准备开始新的性能测量
     * 
     * 使用场景：
     * - 在开始测量前调用，确保计数器从0开始
     * - 在多次测量之间调用，清除之前的计数
     */
    void reset() noexcept {
        int fd = mCountersFd[0];  // 获取主计数器文件描述符
        ioctl(fd, PERF_EVENT_IOC_RESET,  PERF_IOC_FLAG_GROUP);  // 重置整个事件组
    }

    /**
     * 启动性能计数器
     * 
     * 实现过程：
     * 1. 获取主计数器文件描述符
     * 2. 使用 ioctl 系统调用发送 PERF_EVENT_IOC_ENABLE 命令
     * 3. 启用整个事件组中的所有计数器
     * 
     * 作用：
     * - 开始收集性能数据
     * - 计数器开始递增
     * 
     * 使用流程：
     * 1. reset() - 重置计数器
     * 2. start() - 启动计数
     * 3. ... 执行要测量的代码 ...
     * 4. stop() - 停止计数
     * 5. readCounters() - 读取结果
     */
    void start() noexcept {
        int fd = mCountersFd[0];  // 获取主计数器文件描述符
        ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);  // 启用整个事件组
    }

    /**
     * 停止性能计数器
     * 
     * 实现过程：
     * 1. 获取主计数器文件描述符
     * 2. 使用 ioctl 系统调用发送 PERF_EVENT_IOC_DISABLE 命令
     * 3. 停止整个事件组中的所有计数器
     * 
     * 作用：
     * - 停止收集性能数据
     * - 计数器停止递增，但保留当前值
     * 
     * 注意：
     * - 停止后可以调用 readCounters() 读取当前计数值
     * - 停止后可以再次调用 start() 继续计数（累加）
     */
    void stop() noexcept {
        int fd = mCountersFd[0];  // 获取主计数器文件描述符
        ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);  // 禁用整个事件组
    }

    /**
     * 读取当前所有性能计数器的值
     * 
     * 实现过程（在实现文件中）：
     * 1. 使用 read() 系统调用从每个计数器文件描述符读取数据
     * 2. 读取 perf_event 结构体，包含计数器值和时间信息
     * 3. 填充 Counters 对象并返回
     * 
     * @return 返回包含所有计数器值的 Counters 对象
     * 
     * 使用示例：
     *   profiler.start();
     *   // ... 执行代码 ...
     *   profiler.stop();
     *   Counters counters = profiler.readCounters();
     *   double ipc = counters.getIPC();
     */
    Counters readCounters() noexcept;

#else // !__linux__

    /**
     * 非Linux平台的空实现
     * 这些方法不执行任何操作，返回空结果
     */
    void reset() noexcept { }
    void start() noexcept { }
    void stop() noexcept { }
    Counters readCounters() noexcept { return {}; }

#endif // __linux__

    /**
     * 检查是否支持分支预测统计
     * 
     * 实现过程：
     * 检查分支指令和分支未命中两个计数器是否都已成功打开
     * 
     * @return 如果两个计数器都有效（文件描述符 >= 0）返回 true，否则返回 false
     * 
     * 使用场景：
     * - 在调用分支相关方法前检查是否支持
     * - 某些CPU或系统可能不支持分支预测统计
     */
    bool hasBranchRates() const noexcept {
        return (mCountersFd[BRANCHES] >= 0) && (mCountersFd[BRANCH_MISSES] >= 0);
    }

    /**
     * 检查是否支持指令缓存统计
     * 
     * 实现过程：
     * 检查指令缓存引用和指令缓存未命中两个计数器是否都已成功打开
     * 
     * @return 如果两个计数器都有效（文件描述符 >= 0）返回 true，否则返回 false
     * 
     * 使用场景：
     * - 在调用指令缓存相关方法前检查是否支持
     * - 某些CPU或系统可能不支持指令缓存统计
     */
    bool hasICacheRates() const noexcept {
        return (mCountersFd[ICACHE_REFS] >= 0) && (mCountersFd[ICACHE_MISSES] >= 0);
    }

private:
    /**
     * 性能事件ID数组（未使用）
     * 保留用于未来扩展或调试目的
     */
    UTILS_UNUSED uint8_t mIds[EVENT_COUNT] = {};
    
    /**
     * 性能计数器文件描述符数组
     * 
     * 作用：
     * - 每个元素对应一个性能事件的Linux文件描述符
     * - 通过文件描述符与内核的perf_event子系统通信
     * - 值 >= 0 表示文件描述符有效，-1 表示未打开或打开失败
     * 
     * 索引对应关系：
     * - mCountersFd[INSTRUCTIONS] = 指令计数器文件描述符
     * - mCountersFd[CPU_CYCLES] = CPU周期计数器文件描述符
     * - ... 以此类推
     * 
     * 生命周期：
     * - 在构造函数中通过 perf_event_open 打开
     * - 在析构函数中通过 close 关闭
     */
    int mCountersFd[EVENT_COUNT];
    
    /**
     * 当前启用的事件掩码
     * 
     * 作用：
     * - 存储通过 resetEvents() 或构造函数设置的事件掩码
     * - 使用位标志表示哪些事件已启用
     * - 可以通过 getEnabledEvents() 查询
     * 
     * 位标志说明：
     * - 每个位对应一个事件类型
     * - 例如：如果 EV_CPU_CYCLES 位被设置，则CPU周期计数器已启用
     */
    uint32_t mEnabledEvents = 0;
};

} // namespace utils

#endif // TNT_UTILS_PROFILER_H
