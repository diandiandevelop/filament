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

// TODO: Clean-up. We shouldn't need this #ifndef here, but a client has requested that perfetto be
// disabled due to size increase.  In their case, this flag would be defined across targets. Hence
// we guard below with an #ifndef.
#ifndef FILAMENT_TRACING_ENABLED
// Note: The overhead of TRACING is not negligible especially with parallel_for().
#define FILAMENT_TRACING_ENABLED false
#endif

// when FILAMENT_TRACING_ENABLED is true, enables even heavier tracing
#define HEAVY_TRACING  0

#include <utils/JobSystem.h>

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Log.h>
#include <utils/ostream.h>
#include <utils/Panic.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <random>
#include <thread>

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>


#if defined(WIN32)
#    define NOMINMAX
#    include <windows.h>
#    include <string>
# else
#    include <pthread.h>
#endif

#ifdef __ANDROID__
    // see https://developer.android.com/topic/performance/threads#priority
#    include <sys/time.h>
#    include <sys/resource.h>
#    include <unistd.h>
#    ifndef ANDROID_PRIORITY_URGENT_DISPLAY
#        define ANDROID_PRIORITY_URGENT_DISPLAY (-8)
#    endif
#    ifndef ANDROID_PRIORITY_DISPLAY
#        define ANDROID_PRIORITY_DISPLAY (-4)
#    endif
#    ifndef ANDROID_PRIORITY_NORMAL
#        define ANDROID_PRIORITY_NORMAL (0)
#    endif
#    ifndef ANDROID_PRIORITY_BACKGROUND
#        define ANDROID_PRIORITY_BACKGROUND (10)
#    endif
#elif defined(__linux__)
// There is no glibc wrapper for gettid on linux so we need to syscall it.
#    include <unistd.h>
#    include <sys/syscall.h>
#    define gettid() syscall(SYS_gettid)
#endif

#if HEAVY_TRACING
#   define HEAVY_FILAMENT_TRACING_CALL(tag)              FILAMENT_TRACING_CALL(tag)
#   define HEAVY_FILAMENT_TRACING_NAME(tag, name)        FILAMENT_TRACING_NAME(tag, name)
#   define HEAVY_FILAMENT_TRACING_VALUE(tag, name, v)    FILAMENT_TRACING_VALUE(tag, name, v)
#else
#   define HEAVY_FILAMENT_TRACING_CALL(tag)
#   define HEAVY_FILAMENT_TRACING_NAME(tag, name)
#   define HEAVY_FILAMENT_TRACING_VALUE(tag, name, v)
#endif

namespace utils {

void JobSystem::setThreadName(const char* name) noexcept {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(WIN32)
    std::string_view u8name(name);
    size_t size = MultiByteToWideChar(CP_UTF8, 0, u8name.data(), u8name.size(), nullptr, 0);

    std::wstring u16name;
    u16name.resize(size);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8name.data(), u8name.size(), u16name.data(), u16name.size());
    
    SetThreadDescription(GetCurrentThread(), u16name.data());
#endif
}

void JobSystem::setThreadPriority(Priority priority) noexcept {
#ifdef __ANDROID__
    int androidPriority = 0;
    switch (priority) {
        case Priority::BACKGROUND:
            androidPriority = ANDROID_PRIORITY_BACKGROUND;
            break;
        case Priority::NORMAL:
            androidPriority = ANDROID_PRIORITY_NORMAL;
            break;
        case Priority::DISPLAY:
            androidPriority = ANDROID_PRIORITY_DISPLAY;
            break;
        case Priority::URGENT_DISPLAY:
            androidPriority = ANDROID_PRIORITY_URGENT_DISPLAY;
            break;
    }
    errno = 0;
    UTILS_UNUSED_IN_RELEASE int error;
    error = setpriority(PRIO_PROCESS, 0, androidPriority);
#ifndef NDEBUG
    if (UTILS_UNLIKELY(error)) {
        slog.w << "setpriority failed: " << strerror(errno) << io::endl;
    }
#endif
#elif defined(__APPLE__)
    qos_class_t qosClass = QOS_CLASS_DEFAULT;
    switch (priority) {
        case Priority::BACKGROUND:
            qosClass = QOS_CLASS_BACKGROUND;
            break;
        case Priority::NORMAL:
            qosClass = QOS_CLASS_DEFAULT;
            break;
        case Priority::DISPLAY:
            qosClass = QOS_CLASS_USER_INTERACTIVE;
            break;
        case Priority::URGENT_DISPLAY:
            qosClass = QOS_CLASS_USER_INTERACTIVE;
            break;
    }
    errno = 0;
    UTILS_UNUSED_IN_RELEASE int error;
    error = pthread_set_qos_class_self_np(qosClass, 0);
#ifndef NDEBUG
    if (UTILS_UNLIKELY(error)) {
        slog.w << "pthread_set_qos_class_self_np failed: " << strerror(errno) << io::endl;
    }
#endif
#endif
}

void JobSystem::setThreadAffinityById(size_t id) noexcept {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(id, &set);
    sched_setaffinity(gettid(), sizeof(set), &set);
#endif
}

/**
 * JobSystem 构造函数
 * 
 * 初始化 JobSystem，创建线程池和工作队列。
 * 
 * @param userThreadCount 用户指定的线程数（0 表示自动检测）
 * @param adoptableThreadsCount 可被 adopt 的线程槽数量（用于外部线程加入）
 * 
 * 线程数计算策略：
 * 1. 如果用户指定了线程数，使用用户指定的值
 * 2. 否则，根据 CPU 核心数自动计算：
 *    - 获取硬件线程数（CPU 核心数）
 *    - 如果支持超线程，减半（避免使用超线程，简化性能分析）
 *    - 减去 1（主线程占用一个核心）
 * 3. 限制范围：至少 1 个线程，最多 32 个线程
 * 
 * 并行分割次数计算：
 * - mParallelSplitCount = log2(总线程数)
 * - 用于 parallel_for 的最大分割深度
 */
JobSystem::JobSystem(const size_t userThreadCount, const size_t adoptableThreadsCount) noexcept
    : mJobPool("JobSystem Job pool", MAX_JOB_COUNT * sizeof(Job)),  // Job 对象池
      mJobStorageBase(static_cast<Job *>(mJobPool.getAllocator().getCurrent()))  // Job 存储基址
{
    FILAMENT_TRACING_ENABLE(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    /**
     * 计算线程池大小
     */
    unsigned int threadPoolCount = userThreadCount;
    if (threadPoolCount == 0) {
        /**
         * 自动检测线程数
         * 
         * 默认策略：使用 CPU 核心数 - 1（主线程占用一个核心）
         */
        unsigned int hwThreads = std::thread::hardware_concurrency();
        if (UTILS_HAS_HYPER_THREADING) {
            /**
             * 超线程处理
             * 
             * 目前避免使用超线程，简化性能分析。
             * 如果检测到超线程，将线程数减半（向上取整）。
             */
            hwThreads = (hwThreads + 1) / 2;
        }
        // 主线程占用一个核心，所以工作线程数 = 总核心数 - 1
        threadPoolCount = hwThreads - 1;
    }
    // 确保至少有一个工作线程
    threadPoolCount = std::max(1u, threadPoolCount);
    // 限制最大线程数为 32（避免过度并行化）
    threadPoolCount = std::min(UTILS_HAS_THREADING ? 32u : 0u, threadPoolCount);

    /**
     * 初始化线程状态
     * 
     * 总线程数 = 工作线程数 + 可 adopt 的线程槽数
     */
    mThreadStates = aligned_vector<ThreadState>(threadPoolCount + adoptableThreadsCount);
    mThreadCount = uint16_t(threadPoolCount);
    
    /**
     * 计算并行分割次数
     * 
     * parallel_for 的最大分割深度 = log2(总线程数)
     * 这确保分割后的任务数不超过线程数
     */
    mParallelSplitCount = (uint8_t)std::ceil((std::log2f(threadPoolCount + adoptableThreadsCount)));

    /**
     * 静态断言：确保原子类型是无锁的
     * 
     * 这对于性能至关重要，如果原子类型需要锁，性能会显著下降。
     */
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<uint16_t>::is_always_lock_free);

    /**
     * 初始化每个线程的状态
     */
    std::random_device rd;  // 随机数生成器（用于工作窃取时的随机选择）
    const size_t hardwareThreadCount = mThreadCount;
    auto& states = mThreadStates;

    #pragma nounroll
    for (size_t i = 0, n = states.size(); i < n; i++) {
        auto& state = states[i];
        state.rndGen = default_random_engine(rd());  // 为每个线程初始化随机数生成器
        state.js = this;  // 设置 JobSystem 指针
        if (i < hardwareThreadCount) {
            /**
             * 创建工作线程
             * 
             * 只为工作线程创建 std::thread，adoptable 线程槽不创建线程
             * （它们会被外部线程通过 adopt() 加入）
             */
            state.thread = std::thread(&JobSystem::loop, this, &state);
        }
    }
}

JobSystem::~JobSystem() {
    requestExit();

    #pragma nounroll
    for (auto &state : mThreadStates) {
        // adopted threads are not joinable
        if (state.thread.joinable()) {
            state.thread.join();
        }
    }
}

inline void JobSystem::incRef(Job const* job) noexcept {
    // no action is taken when incrementing the reference counter, therefore we can safely use
    // memory_order_relaxed.
    job->refCount.fetch_add(1, std::memory_order_relaxed);
}

UTILS_NOINLINE
void JobSystem::decRef(Job const* job) noexcept {

    // We must ensure that accesses from other threads happen before deleting the Job.
    // To accomplish this, we need to guarantee that no read/writes are reordered after the
    // dec-ref, because ANOTHER thread could hold the last reference (after us) and that thread
    // needs to see all accesses completed before it deletes the object. This is done
    // with memory_order_release.
    // Similarly, we need to guarantee that no read/write are reordered before the last decref,
    // or some other thread could see a destroyed object before the ref-count is 0. This is done
    // with memory_order_acquire.
    auto const c = job->refCount.fetch_sub(1, std::memory_order_acq_rel);
    assert(c > 0);
    if (c == 1) {
        // This was the last reference, it's safe to destroy the job.
        mJobPool.destroy(job);
    }
}

void JobSystem::requestExit() noexcept {
    mExitRequested.store(true);
    std::lock_guard const lock(mWaiterLock);
    mWaiterCondition.notify_all();
}

inline bool JobSystem::exitRequested() const noexcept {
    // memory_order_relaxed is safe because the only action taken is to exit the thread
    return mExitRequested.load(std::memory_order_relaxed);
}

inline bool JobSystem::hasActiveJobs() const noexcept {
    return mActiveJobs.load(std::memory_order_relaxed) > 0;
}

inline bool JobSystem::hasJobCompleted(Job const* job) noexcept {
    return (job->runningJobCount.load(std::memory_order_acquire) & JOB_COUNT_MASK) == 0;
}

inline void JobSystem::wait(std::unique_lock<Mutex>& lock) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterCondition.wait(lock);
}

inline uint32_t JobSystem::wait(std::unique_lock<Mutex>& lock, Job* const job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    // signal we are waiting

    if (hasActiveJobs() || exitRequested()) {
        return job->runningJobCount.load(std::memory_order_acquire);
    }

    uint32_t runningJobCount =
            job->runningJobCount.fetch_add(1 << WAITER_COUNT_SHIFT, std::memory_order_relaxed);

    if (runningJobCount & JOB_COUNT_MASK) {
        mWaiterCondition.wait(lock);
    }

    runningJobCount =
            job->runningJobCount.fetch_sub(1 << WAITER_COUNT_SHIFT, std::memory_order_acquire);

    assert_invariant((runningJobCount >> WAITER_COUNT_SHIFT) >= 1);

    return runningJobCount;
}

UTILS_NOINLINE
void JobSystem::wakeAll() noexcept {
    // wakeAll() is called when a job finishes (to wake up any thread that might be waiting on it)
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterLock.lock();
    // this empty critical section is needed -- it guarantees that notify_all() happens
    // either before the condition is checked, or after the condition variable sleeps.
    mWaiterLock.unlock();
    // notify_all() can be pretty slow, and it doesn't need to be inside the lock.
    mWaiterCondition.notify_all();
}

void JobSystem::wakeOne() noexcept {
    // wakeOne() is called when a new job is added to a queue
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterLock.lock();
    // this empty critical section is needed -- it guarantees that notify_one() happens
    // either before the condition is checked, or after the condition variable sleeps.
    mWaiterLock.unlock();
    // notify_one() can be pretty slow, and it doesn't need to be inside the lock.
    mWaiterCondition.notify_one();
}

inline JobSystem::ThreadState& JobSystem::getState() {
    std::lock_guard const lock(mThreadMapLock);
    auto const iter = mThreadMap.find(std::this_thread::get_id());
    FILAMENT_CHECK_PRECONDITION(iter != mThreadMap.end()) << "This thread has not been adopted.";
    return *iter->second;
}

JobSystem::Job* JobSystem::allocateJob() noexcept {
    return mJobPool.make<Job>();
}

void JobSystem::put(WorkQueue& workQueue, Job const* job) noexcept {
    assert(job);
    assert(job >= mJobStorageBase && job < mJobStorageBase + MAX_JOB_COUNT);

    size_t const index = job - mJobStorageBase;

    // put the job into the queue
    workQueue.push(uint16_t(index + 1));

    // increase our active job count (the order in which we're doing this must not matter
    // because we're not using std::memory_order_seq_cst (here or in WorkQueue::push()).
    mActiveJobs.fetch_add(1, std::memory_order_relaxed);

    // Note: it's absolutely possible for mActiveJobs to be 0 here, because the job could have
    // been handled by a zealous worker already. In that case we could avoid calling wakeOne(),
    // but that is not the common case.

    wakeOne();
}

JobSystem::Job* JobSystem::pop(WorkQueue& workQueue) noexcept {
    size_t const index = workQueue.pop();
    assert(index <= MAX_JOB_COUNT);
    Job* const job = !index ? nullptr : &mJobStorageBase[index - 1];
    if (UTILS_LIKELY(job)) {
        mActiveJobs.fetch_sub(1, std::memory_order_relaxed);
    }
    return job;
}

JobSystem::Job* JobSystem::steal(WorkQueue& workQueue) noexcept {
    size_t const index = workQueue.steal();
    assert_invariant(index <= MAX_JOB_COUNT);
    Job* const job = !index ? nullptr : &mJobStorageBase[index - 1];
    if (UTILS_LIKELY(job)) {
        mActiveJobs.fetch_sub(1, std::memory_order_relaxed);
    }
    return job;
}

inline JobSystem::ThreadState* JobSystem::getStateToStealFrom(ThreadState& state) noexcept {
    auto& threadStates = mThreadStates;
    // memory_order_relaxed is okay because we don't take any action that has data dependency
    // on this value (in particular mThreadStates, is always initialized properly).
    uint16_t const adopted = mAdoptedThreads.load(std::memory_order_relaxed);
    uint16_t const threadCount = mThreadCount + adopted;

    ThreadState* stateToStealFrom = nullptr;

    // don't try to steal from someone else if we're the only thread (infinite loop)
    if (threadCount >= 2) {
        do {
            // This is biased, but frankly, we don't care. It's fast.
            uint16_t const index = uint16_t(state.rndGen() % threadCount);
            assert(index < threadStates.size());
            stateToStealFrom = &threadStates[index];
            // don't steal from our own queue
        } while (stateToStealFrom == &state);
    }
    return stateToStealFrom;
}

/**
 * 工作窃取
 * 
 * 从其他线程的队列中窃取 Job。
 * 这是负载均衡的关键机制：当线程自己的队列为空时，主动从其他线程窃取工作。
 * 
 * 窃取策略：
 * 1. 随机选择一个目标线程（避免所有线程都窃取同一个线程）
 * 2. 从目标线程的队列顶部窃取（FIFO，先进先出）
 * 3. 如果失败，继续尝试其他线程
 * 4. 如果所有线程都没有可窃取的 Job，返回 nullptr
 * 
 * @param state 当前线程的状态
 * @return 窃取的 Job，如果没有可窃取的 Job 返回 nullptr
 */
JobSystem::Job* JobSystem::steal(ThreadState& state) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    Job* job = nullptr;
    do {
        /**
         * 随机选择一个目标线程
         * 
         * getStateToStealFrom() 使用随机数生成器选择目标线程，
         * 避免所有线程都窃取同一个线程的队列。
         */
        ThreadState* const stateToStealFrom = getStateToStealFrom(state);
        if (stateToStealFrom) {
            /**
             * 从目标线程的队列顶部窃取 Job
             * 
             * steal() 从队列顶部移除 Job（FIFO，先进先出）
             * 这与 pop() 形成对比（pop 从底部移除，LIFO）
             * 
             * 设计原因：
             * - 主线程从底部操作（push/pop），工作线程从顶部窃取
             * - 减少竞争：主线程和工作线程操作队列的不同端
             * - 提高缓存局部性：主线程操作最近添加的 Job
             */
            job = steal(stateToStealFrom->workQueue);
        }
        /**
         * 如果窃取失败（返回 nullptr），且仍有活跃 Job，
         * 继续尝试窃取（可能有其他线程正在添加 Job）
         */
        // nullptr -> nothing to steal in that queue either, if there are active jobs,
        // continue to try stealing one.
    } while (!job && hasActiveJobs());
    return job;
}

/**
 * 执行一个 Job
 * 
 * 这是工作线程的核心执行函数，负责：
 * 1. 从自己的队列中获取 Job
 * 2. 如果队列为空，尝试从其他线程窃取 Job
 * 3. 执行 Job
 * 4. 完成 Job（通知父 Job、释放资源等）
 * 
 * @param state 当前线程的状态
 * @return true 如果执行了 Job，false 如果队列为空且没有可窃取的 Job
 */
bool JobSystem::execute(ThreadState& state) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    /**
     * 步骤 1：从自己的队列中获取 Job
     * 
     * pop() 从队列底部移除 Job（LIFO，后进先出）
     * 这有助于提高缓存局部性（最近添加的 Job 更可能在缓存中）
     */
    Job* job = pop(state.workQueue);

    /**
     * 步骤 2：如果队列为空，尝试工作窃取
     * 
     * 工作窃取策略：
     * - 从其他线程的队列顶部窃取（FIFO，先进先出）
     * - 这有助于负载均衡
     * - 尝试次数有限，避免过度轮询
     * 
     * 注意：对于某些基准测试，轮询 steal() 一段时间是有益的，
     * 因为进入睡眠和唤醒的开销很大。但在实际应用中，对于较大的 Job
     * 或使用 parallel_for 时，效果不明显。
     */
    constexpr size_t const STEAL_TRY_COUNT = 1;
    for (size_t i = 0; UTILS_UNLIKELY(!job && i < STEAL_TRY_COUNT); i++) {
        // 队列为空，尝试从其他线程窃取 Job
        job = steal(state);
    }

    /**
     * 步骤 3：执行 Job
     */
    if (UTILS_LIKELY(job)) {
        // 确保 Job 的 runningJobCount >= 1（Job 应该处于运行状态）
        assert((job->runningJobCount.load(std::memory_order_relaxed) & JOB_COUNT_MASK) >= 1);
        
        if (UTILS_LIKELY(job->function)) {
            /**
             * 执行 Job 函数
             * 
             * 1. 设置 Job 的线程 ID（用于调试和性能分析）
             * 2. 调用 Job 函数
             * 3. 清除线程 ID
             */
            HEAVY_FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_JOBSYSTEM, "job->function");
            job->id = std::distance(mThreadStates.data(), &state);  // 记录执行线程 ID
            job->function(job->storage, *this, job);  // 执行 Job 函数
            job->id = invalidThreadId;  // 清除线程 ID
        }
        
        /**
         * 步骤 4：完成 Job
         * 
         * finish() 负责：
         * - 减少 runningJobCount
         * - 如果 Job 完成，通知父 Job
         * - 释放 Job 资源
         * - 唤醒等待的线程
         */
        finish(job);
    }
    return job != nullptr;  // 返回是否执行了 Job
}

/**
 * 工作线程主循环
 * 
 * 这是每个工作线程的主函数，负责：
 * 1. 设置线程名称和优先级
 * 2. 注册线程到线程映射表
 * 3. 循环执行 Job，直到退出请求
 * 
 * @param state 线程状态指针
 */
void JobSystem::loop(ThreadState* state) {
    /**
     * 设置线程属性
     * 
     * - 线程名称：用于调试和性能分析工具识别
     * - 线程优先级：DISPLAY 优先级，确保渲染相关任务优先执行
     */
    setThreadName("JobSystem::loop");
    setThreadPriority(Priority::DISPLAY);

    /**
     * 注册线程到线程映射表
     * 
     * 线程映射表用于：
     * - 从线程 ID 查找对应的 ThreadState
     * - 支持 adopt() 机制（外部线程加入 JobSystem）
     * - 线程安全：使用互斥锁保护
     */
    std::unique_lock lock(mThreadMapLock);
    bool const inserted = mThreadMap.emplace(std::this_thread::get_id(), state).second;
    lock.unlock();

    FILAMENT_CHECK_PRECONDITION(inserted) << "This thread is already in a loop.";

    /**
     * 主循环
     * 
     * 执行流程：
     * 1. 尝试执行一个 Job（execute）
     * 2. 如果成功，继续循环
     * 3. 如果失败（队列为空且没有可窃取的 Job）：
     *    - 检查退出请求和活跃 Job 数
     *    - 如果没有退出请求且有活跃 Job，继续循环（可能有其他线程添加了 Job）
     *    - 否则，等待条件变量通知
     * 4. 重复直到退出请求
     */
    do {
        if (!execute(*state)) {
            /**
             * 队列为空，进入等待状态
             * 
             * 等待条件：
             * - 退出请求（exitRequested()）
             * - 有活跃 Job（hasActiveJobs()）
             * 
             * 如果两个条件都不满足，等待条件变量通知。
             * 当有新 Job 添加或退出请求时，会唤醒等待的线程。
             */
            std::unique_lock lock(mWaiterLock);
            while (!exitRequested() && !hasActiveJobs()) {
                wait(lock);  // 等待条件变量通知
            }
        }
    } while (!exitRequested());  // 继续循环直到退出请求
}

UTILS_NOINLINE
void JobSystem::finish(Job* job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    bool notify = false;

    // terminate this job and notify its parent
    Job* const storage = mJobStorageBase;
    do {
        // std::memory_order_release here is needed to synchronize with JobSystem::wait()
        // which needs to "see" all changes that happened before the job terminated.
        uint32_t const v = job->runningJobCount.fetch_sub(1, std::memory_order_acq_rel);
        uint32_t const runningJobCount = v & JOB_COUNT_MASK;
        assert(runningJobCount > 0);

        if (runningJobCount == 1) {
            // no more work, destroy this job and notify its parent
            uint32_t const waiters = v >> WAITER_COUNT_SHIFT;
            if (waiters) {
                notify = true;
            }
            Job* const parent = job->parent == 0x7FFF ? nullptr : &storage[job->parent];
            decRef(job);
            job = parent;
        } else {
            // there is still work (e.g.: children), we're done.
            break;
        }
    } while (job);

    // wake-up all threads that could potentially be waiting on this job finishing
    if (UTILS_UNLIKELY(notify)) {
        // but avoid calling notify_all() at all cost, because it's always expensive
        wakeAll();
    }
}

// -----------------------------------------------------------------------------------------------
// public API...


JobSystem::Job* JobSystem::create(Job* parent, JobFunc func) noexcept {
    parent = (parent == nullptr) ? mRootJob : parent;
    Job* const job = allocateJob();
    if (UTILS_LIKELY(job)) {
        size_t index = 0x7FFF;
        if (parent) {
            // add a reference to the parent to make sure it can't be terminated.
            // memory_order_relaxed is safe because no action is taken at this point
            // (the job is not started yet).
            UTILS_UNUSED_IN_RELEASE auto const parentJobCount =
                    parent->runningJobCount.fetch_add(1, std::memory_order_relaxed);

            // can't create a child job of a terminated parent
            assert((parentJobCount & JOB_COUNT_MASK) > 0);

            index = parent - mJobStorageBase;
            assert(index < MAX_JOB_COUNT);
        }
        job->function = func;
        job->parent = uint16_t(index);
    }
    return job;
}

void JobSystem::cancel(Job*& job) noexcept {
    finish(job);
    job = nullptr;
}

JobSystem::Job* JobSystem::retain(Job* job) noexcept {
    Job* retained = job;
    incRef(retained);
    return retained;
}

void JobSystem::release(Job*& job) noexcept {
    decRef(job);
    job = nullptr;
}

void JobSystem::run(Job*& job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    ThreadState& state(getState());

    put(state.workQueue, job);

    // after run() returns, the job is virtually invalid (it'll die on its own)
    job = nullptr;
}

void JobSystem::run(Job*& job, uint8_t id) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    ThreadState& state = mThreadStates[id];
    assert_invariant(&state == &getState());

    put(state.workQueue, job);

    // after run() returns, the job is virtually invalid (it'll die on its own)
    job = nullptr;
}

JobSystem::Job* JobSystem::runAndRetain(Job* job) noexcept {
    Job* retained = retain(job);
    run(job);
    return retained;
}

void JobSystem::waitAndRelease(Job*& job) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    assert(job);
    assert(job->refCount.load(std::memory_order_relaxed) >= 1);

    ThreadState& state(getState());
    do {
        if (UTILS_UNLIKELY(!execute(state))) {
            // test if job has completed first, to possibly avoid taking the lock
            if (hasJobCompleted(job)) {
                break;
            }

            // the only way we can be here is if the job we're waiting on it being handled
            // by another thread:
            //    - we returned from execute() which means all queues are empty
            //    - yet our job hasn't completed yet
            //    ergo, it's being run in another thread
            //
            // this could take time however, so we will wait with a condition, and
            // continue to handle more jobs, as they get added.

            std::unique_lock lock(mWaiterLock);
            uint32_t const runningJobCount = wait(lock, job);
            // we could be waking up because either:
            // - the job we're waiting on has completed
            // - more jobs where added to the JobSystem
            // - we're asked to exit
            if ((runningJobCount & JOB_COUNT_MASK) == 0 || exitRequested()) {
                break;
            }

            // if we get here, it means that
            // - the job we're waiting on is still running, and
            // - we're not asked to exit, and
            // - there were some active jobs
            // So we try to handle one.
            continue;
        }
    } while (UTILS_LIKELY(!hasJobCompleted(job) && !exitRequested()));

    if (job == mRootJob) {
        mRootJob = nullptr;
    }

    release(job);
}

void JobSystem::runAndWait(Job*& job) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    runAndRetain(job);
    waitAndRelease(job);
}

void JobSystem::adopt() {
    const auto tid = std::this_thread::get_id();

    std::unique_lock lock(mThreadMapLock);
    auto const iter = mThreadMap.find(tid);
    ThreadState const* const state = iter ==  mThreadMap.end() ? nullptr : iter->second;
    lock.unlock();

    if (state) {
        // we're already part of a JobSystem, do nothing.
        FILAMENT_CHECK_PRECONDITION(this == state->js)
                << "Called adopt on a thread owned by another JobSystem (" << state->js
                << "), this=" << this << "!";
        return;
    }

    // memory_order_relaxed is safe because we don't take action on this value.
    uint16_t const adopted = mAdoptedThreads.fetch_add(1, std::memory_order_relaxed);
    size_t const index = mThreadCount + adopted;

    FILAMENT_CHECK_POSTCONDITION(index < mThreadStates.size())
            << "Too many calls to adopt(). No more adoptable threads!";

    // all threads adopted by the JobSystem need to run at the same priority
    setThreadPriority(Priority::DISPLAY);

    // This thread's queue will be selectable immediately (i.e.: before we set its TLS)
    // however, it's not a problem since mThreadState is pre-initialized and valid
    // (e.g.: the queue is empty).

    lock.lock();
    mThreadMap[tid] = &mThreadStates[index];
}

void JobSystem::emancipate() {
    const auto tid = std::this_thread::get_id();
    std::unique_lock const lock(mThreadMapLock);
    auto const iter = mThreadMap.find(tid);
    ThreadState const* const state = iter ==  mThreadMap.end() ? nullptr : iter->second;
    FILAMENT_CHECK_PRECONDITION(state) << "this thread is not an adopted thread";
    FILAMENT_CHECK_PRECONDITION(state->js == this) << "this thread is not adopted by us";
    mThreadMap.erase(iter);
}

io::ostream& operator<<(io::ostream& out, JobSystem const& js) {
    for (auto const& item : js.mThreadStates) {
        size_t const id = std::distance(js.mThreadStates.data(), &item);
        out << id << ": " << item.workQueue.getCount() << io::endl;
    }
    return out;
}

} // namespace utils
