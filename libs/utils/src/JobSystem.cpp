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

/**
 * 增加任务的引用计数
 * 
 * 使用 memory_order_relaxed 是安全的，因为增加引用计数时不执行任何操作。
 * 
 * @param job 任务指针
 */
inline void JobSystem::incRef(Job const* job) noexcept {
    // no action is taken when incrementing the reference counter, therefore we can safely use
    // memory_order_relaxed.
    // 增加引用计数时不执行任何操作，因此可以安全地使用 memory_order_relaxed。
    job->refCount.fetch_add(1, std::memory_order_relaxed);
}

/**
 * 减少任务的引用计数
 * 
 * 如果引用计数归零，销毁任务。
 * 
 * 内存序说明：
 * - 必须确保其他线程的访问在删除 Job 之前发生
 * - 需要保证在 dec-ref 之后没有读/写被重排序，因为另一个线程可能持有最后一个引用
 * - 使用 memory_order_release 确保释放语义
 * - 使用 memory_order_acquire 确保获取语义
 * - 因此使用 memory_order_acq_rel
 * 
 * @param job 任务指针
 */
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
    // 我们必须确保其他线程的访问在删除 Job 之前发生。
    // 为此，我们需要保证在 dec-ref 之后没有读/写被重排序，因为另一个线程可能持有最后一个引用（在我们之后），
    // 该线程需要看到所有访问在删除对象之前完成。这通过 memory_order_release 完成。
    // 同样，我们需要保证在最后一次 decref 之前没有读/写被重排序，否则其他线程可能在引用计数为 0 之前看到已销毁的对象。
    // 这通过 memory_order_acquire 完成。
    auto const c = job->refCount.fetch_sub(1, std::memory_order_acq_rel);
    assert(c > 0);
    if (c == 1) {
        // This was the last reference, it's safe to destroy the job.
        // 这是最后一个引用，可以安全地销毁任务。
        mJobPool.destroy(job);
    }
}

/**
 * 请求所有线程退出
 * 
 * 设置退出标志并唤醒所有等待的线程。
 */
void JobSystem::requestExit() noexcept {
    mExitRequested.store(true);
    std::lock_guard const lock(mWaiterLock);
    mWaiterCondition.notify_all();
}

/**
 * 检查是否已请求退出
 * 
 * 使用 memory_order_relaxed 是安全的，因为唯一采取的操作是退出线程。
 * 
 * @return 如果已请求退出返回 true，否则返回 false
 */
inline bool JobSystem::exitRequested() const noexcept {
    // memory_order_relaxed is safe because the only action taken is to exit the thread
    // memory_order_relaxed 是安全的，因为唯一采取的操作是退出线程
    return mExitRequested.load(std::memory_order_relaxed);
}

/**
 * 检查是否有活跃任务
 * 
 * @return 如果有活跃任务返回 true，否则返回 false
 */
inline bool JobSystem::hasActiveJobs() const noexcept {
    return mActiveJobs.load(std::memory_order_relaxed) > 0;
}

/**
 * 检查任务是否已完成
 * 
 * 通过检查 runningJobCount 的低 24 位是否为 0 来判断。
 * 
 * @param job 任务指针
 * @return 如果任务已完成返回 true，否则返回 false
 */
inline bool JobSystem::hasJobCompleted(Job const* job) noexcept {
    return (job->runningJobCount.load(std::memory_order_acquire) & JOB_COUNT_MASK) == 0;
}

/**
 * 等待条件变量
 * 
 * @param lock 互斥锁（已锁定）
 */
inline void JobSystem::wait(std::unique_lock<Mutex>& lock) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterCondition.wait(lock);
}

/**
 * 等待任务完成
 * 
 * 在任务的 runningJobCount 中记录等待者数量，然后等待条件变量。
 * 
 * 等待机制：
 * 1. 如果有活跃任务或退出请求，立即返回（不等待）
 * 2. 否则，在 runningJobCount 的高 8 位增加等待者计数
 * 3. 如果任务仍在运行，等待条件变量
 * 4. 唤醒后，减少等待者计数并返回
 * 
 * @param lock 互斥锁（已锁定）
 * @param job 要等待的任务
 * @return runningJobCount 的值
 */
inline uint32_t JobSystem::wait(std::unique_lock<Mutex>& lock, Job* const job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    // signal we are waiting
    // 发出我们正在等待的信号

    // 如果有活跃任务或退出请求，立即返回（不等待）
    if (hasActiveJobs() || exitRequested()) {
        return job->runningJobCount.load(std::memory_order_acquire);
    }

    // 在 runningJobCount 的高 8 位增加等待者计数
    uint32_t runningJobCount =
            job->runningJobCount.fetch_add(1 << WAITER_COUNT_SHIFT, std::memory_order_relaxed);

    // 如果任务仍在运行（低 24 位不为 0），等待条件变量
    if (runningJobCount & JOB_COUNT_MASK) {
        mWaiterCondition.wait(lock);
    }

    // 唤醒后，减少等待者计数
    runningJobCount =
            job->runningJobCount.fetch_sub(1 << WAITER_COUNT_SHIFT, std::memory_order_acquire);

    // 确保等待者计数 >= 1（我们刚刚减少了一个）
    assert_invariant((runningJobCount >> WAITER_COUNT_SHIFT) >= 1);

    return runningJobCount;
}

/**
 * 唤醒所有等待的线程
 * 
 * 在任务完成时调用，唤醒可能正在等待该任务的任何线程。
 * 
 * 注意：空的临界区是必需的，它保证 notify_all() 在条件检查之前或条件变量睡眠之后发生。
 * notify_all() 可能很慢，不需要在锁内。
 */
UTILS_NOINLINE
void JobSystem::wakeAll() noexcept {
    // wakeAll() is called when a job finishes (to wake up any thread that might be waiting on it)
    // wakeAll() 在任务完成时调用（唤醒可能正在等待该任务的任何线程）
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterLock.lock();
    // this empty critical section is needed -- it guarantees that notify_all() happens
    // either before the condition is checked, or after the condition variable sleeps.
    // 这个空的临界区是必需的 - 它保证 notify_all() 在条件检查之前或条件变量睡眠之后发生。
    mWaiterLock.unlock();
    // notify_all() can be pretty slow, and it doesn't need to be inside the lock.
    // notify_all() 可能很慢，不需要在锁内。
    mWaiterCondition.notify_all();
}

/**
 * 唤醒一个等待的线程
 * 
 * 在向队列添加新任务时调用。
 * 
 * 注意：空的临界区是必需的，它保证 notify_one() 在条件检查之前或条件变量睡眠之后发生。
 * notify_one() 可能很慢，不需要在锁内。
 */
void JobSystem::wakeOne() noexcept {
    // wakeOne() is called when a new job is added to a queue
    // wakeOne() 在向队列添加新任务时调用
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    mWaiterLock.lock();
    // this empty critical section is needed -- it guarantees that notify_one() happens
    // either before the condition is checked, or after the condition variable sleeps.
    // 这个空的临界区是必需的 - 它保证 notify_one() 在条件检查之前或条件变量睡眠之后发生。
    mWaiterLock.unlock();
    // notify_one() can be pretty slow, and it doesn't need to be inside the lock.
    // notify_one() 可能很慢，不需要在锁内。
    mWaiterCondition.notify_one();
}

/**
 * 获取当前线程的状态
 * 
 * 从线程映射表中查找当前线程对应的 ThreadState。
 * 
 * @return 当前线程的 ThreadState 引用
 */
inline JobSystem::ThreadState& JobSystem::getState() {
    std::lock_guard const lock(mThreadMapLock);
    auto const iter = mThreadMap.find(std::this_thread::get_id());
    FILAMENT_CHECK_PRECONDITION(iter != mThreadMap.end()) << "This thread has not been adopted.";
    return *iter->second;
}

/**
 * 分配任务对象
 * 
 * 从对象池中分配一个 Job 对象。
 * 
 * @return 分配的任务指针，如果分配失败返回 nullptr
 */
JobSystem::Job* JobSystem::allocateJob() noexcept {
    return mJobPool.make<Job>();
}

/**
 * 将任务放入队列
 * 
 * 将任务添加到工作队列并增加活跃任务计数。
 * 
 * 实现细节：
 * - 将任务指针转换为索引（+1，因为 0 表示空）
 * - 将索引推入队列
 * - 增加活跃任务计数
 * - 唤醒一个等待的线程
 * 
 * 注意：mActiveJobs 可能在这里为 0，因为任务可能已经被积极的工作线程处理了。
 * 在这种情况下可以避免调用 wakeOne()，但这不是常见情况。
 * 
 * @param workQueue 工作队列
 * @param job 任务指针
 */
void JobSystem::put(WorkQueue& workQueue, Job const* job) noexcept {
    assert(job);
    assert(job >= mJobStorageBase && job < mJobStorageBase + MAX_JOB_COUNT);

    // 将任务指针转换为索引（+1，因为 0 表示空）
    size_t const index = job - mJobStorageBase;

    // put the job into the queue
    // 将任务放入队列
    workQueue.push(uint16_t(index + 1));

    // increase our active job count (the order in which we're doing this must not matter
    // because we're not using std::memory_order_seq_cst (here or in WorkQueue::push()).
    // 增加活跃任务计数（我们这样做的顺序无关紧要，因为我们不使用 std::memory_order_seq_cst）
    mActiveJobs.fetch_add(1, std::memory_order_relaxed);

    // Note: it's absolutely possible for mActiveJobs to be 0 here, because the job could have
    // been handled by a zealous worker already. In that case we could avoid calling wakeOne(),
    // but that is not the common case.
    // 注意：mActiveJobs 在这里可能为 0，因为任务可能已经被积极的工作线程处理了。
    // 在这种情况下可以避免调用 wakeOne()，但这不是常见情况。

    wakeOne();
}

/**
 * 从队列中弹出任务
 * 
 * 从队列底部弹出任务（LIFO，后进先出）。
 * 这有助于提高缓存局部性（最近添加的任务更可能在缓存中）。
 * 
 * @param workQueue 工作队列
 * @return 任务指针，如果队列为空返回 nullptr
 */
JobSystem::Job* JobSystem::pop(WorkQueue& workQueue) noexcept {
    // 从队列底部弹出索引
    size_t const index = workQueue.pop();
    assert(index <= MAX_JOB_COUNT);
    // 将索引转换回任务指针（-1，因为存储时 +1）
    Job* const job = !index ? nullptr : &mJobStorageBase[index - 1];
    if (UTILS_LIKELY(job)) {
        // 减少活跃任务计数
        mActiveJobs.fetch_sub(1, std::memory_order_relaxed);
    }
    return job;
}

/**
 * 从队列中窃取任务
 * 
 * 从队列顶部窃取任务（FIFO，先进先出）。
 * 这是工作窃取的核心操作：工作线程从其他线程的队列顶部窃取任务。
 * 
 * 设计原因：
 * - 主线程从底部操作（push/pop），工作线程从顶部窃取
 * - 减少竞争：主线程和工作线程操作队列的不同端
 * - 提高缓存局部性：主线程操作最近添加的任务
 * 
 * @param workQueue 工作队列
 * @return 任务指针，如果队列为空返回 nullptr
 */
JobSystem::Job* JobSystem::steal(WorkQueue& workQueue) noexcept {
    // 从队列顶部窃取索引
    size_t const index = workQueue.steal();
    assert_invariant(index <= MAX_JOB_COUNT);
    // 将索引转换回任务指针（-1，因为存储时 +1）
    Job* const job = !index ? nullptr : &mJobStorageBase[index - 1];
    if (UTILS_LIKELY(job)) {
        // 减少活跃任务计数
        mActiveJobs.fetch_sub(1, std::memory_order_relaxed);
    }
    return job;
}

/**
 * 获取要窃取的目标线程状态
 * 
 * 随机选择一个线程作为工作窃取的目标。
 * 
 * 选择策略：
 * - 如果只有一个线程，返回 nullptr（避免无限循环）
 * - 否则，使用随机数生成器随机选择一个线程
 * - 如果选中的是自己，重新选择
 * 
 * 注意：使用 memory_order_relaxed 是安全的，因为我们不对此值采取任何有数据依赖的操作
 * （特别是 mThreadStates 始终正确初始化）。
 * 
 * @param state 当前线程状态
 * @return 目标线程状态指针，如果只有一个线程返回 nullptr
 */
inline JobSystem::ThreadState* JobSystem::getStateToStealFrom(ThreadState& state) noexcept {
    auto& threadStates = mThreadStates;
    // memory_order_relaxed is okay because we don't take any action that has data dependency
    // on this value (in particular mThreadStates, is always initialized properly).
    // memory_order_relaxed 是可以的，因为我们不对此值采取任何有数据依赖的操作
    // （特别是 mThreadStates 始终正确初始化）。
    uint16_t const adopted = mAdoptedThreads.load(std::memory_order_relaxed);
    uint16_t const threadCount = mThreadCount + adopted;

    ThreadState* stateToStealFrom = nullptr;

    // don't try to steal from someone else if we're the only thread (infinite loop)
    // 如果我们是唯一的线程，不要尝试从其他线程窃取（无限循环）
    if (threadCount >= 2) {
        do {
            // This is biased, but frankly, we don't care. It's fast.
            // 这是有偏的，但坦率地说，我们不在乎。它很快。
            // 使用随机数生成器随机选择线程索引
            uint16_t const index = uint16_t(state.rndGen() % threadCount);
            assert(index < threadStates.size());
            stateToStealFrom = &threadStates[index];
            // don't steal from our own queue
            // 不要从我们自己的队列窃取
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

/**
 * 完成任务
 * 
 * 减少任务的运行计数，如果任务完成则通知父任务并唤醒等待的线程。
 * 
 * 处理流程：
 * 1. 减少 runningJobCount（原子操作）
 * 2. 如果 runningJobCount 归零（任务完成）：
 *    - 检查是否有等待者，如果有则标记需要通知
 *    - 获取父任务
 *    - 释放当前任务的引用
 *    - 继续处理父任务（递归）
 * 3. 如果仍有工作（如子任务），停止处理
 * 4. 如果有等待者，唤醒所有等待的线程
 * 
 * 内存序说明：
 * - 使用 memory_order_acq_rel 确保与 wait() 同步
 * - wait() 需要"看到"任务终止前发生的所有更改
 * 
 * @param job 任务指针
 */
UTILS_NOINLINE
void JobSystem::finish(Job* job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    bool notify = false;

    // terminate this job and notify its parent
    // 终止此任务并通知其父任务
    Job* const storage = mJobStorageBase;
    do {
        // std::memory_order_release here is needed to synchronize with JobSystem::wait()
        // which needs to "see" all changes that happened before the job terminated.
        // 这里需要 std::memory_order_release 来与 JobSystem::wait() 同步
        // wait() 需要"看到"任务终止前发生的所有更改。
        uint32_t const v = job->runningJobCount.fetch_sub(1, std::memory_order_acq_rel);
        uint32_t const runningJobCount = v & JOB_COUNT_MASK;
        assert(runningJobCount > 0);

        if (runningJobCount == 1) {
            // no more work, destroy this job and notify its parent
            // 没有更多工作，销毁此任务并通知其父任务
            // 提取等待者数量（高 8 位）
            uint32_t const waiters = v >> WAITER_COUNT_SHIFT;
            if (waiters) {
                notify = true;
            }
            // 获取父任务（0x7FFF 表示无父任务）
            Job* const parent = job->parent == 0x7FFF ? nullptr : &storage[job->parent];
            // 释放当前任务的引用
            decRef(job);
            // 继续处理父任务
            job = parent;
        } else {
            // there is still work (e.g.: children), we're done.
            // 仍有工作（例如：子任务），我们完成了。
            break;
        }
    } while (job);

    // wake-up all threads that could potentially be waiting on this job finishing
    // 唤醒可能正在等待此任务完成的所有线程
    if (UTILS_UNLIKELY(notify)) {
        // but avoid calling notify_all() at all cost, because it's always expensive
        // 但尽量避免调用 notify_all()，因为它总是很昂贵
        wakeAll();
    }
}

// -----------------------------------------------------------------------------------------------
// public API...
// 公共 API...

/**
 * 创建任务（内部方法）
 * 
 * 分配任务对象并设置父任务关系。
 * 
 * 处理流程：
 * 1. 如果未指定父任务，使用根任务
 * 2. 分配任务对象
 * 3. 如果有父任务：
 *    - 增加父任务的 runningJobCount（防止父任务被终止）
 *    - 计算父任务的索引
 * 4. 设置任务函数和父任务索引
 * 
 * @param parent 父任务（nullptr 表示使用根任务）
 * @param func 任务函数指针
 * @return 创建的任务指针，如果分配失败返回 nullptr
 */
JobSystem::Job* JobSystem::create(Job* parent, JobFunc func) noexcept {
    // 如果未指定父任务，使用根任务
    parent = (parent == nullptr) ? mRootJob : parent;
    // 分配任务对象
    Job* const job = allocateJob();
    if (UTILS_LIKELY(job)) {
        size_t index = 0x7FFF;  // 0x7FFF 表示无父任务
        if (parent) {
            // add a reference to the parent to make sure it can't be terminated.
            // memory_order_relaxed is safe because no action is taken at this point
            // (the job is not started yet).
            // 增加父任务的引用，确保它不能被终止。
            // memory_order_relaxed 是安全的，因为此时不执行任何操作（任务尚未启动）。
            UTILS_UNUSED_IN_RELEASE auto const parentJobCount =
                    parent->runningJobCount.fetch_add(1, std::memory_order_relaxed);

            // can't create a child job of a terminated parent
            // 不能创建已终止父任务的子任务
            assert((parentJobCount & JOB_COUNT_MASK) > 0);

            // 计算父任务的索引
            index = parent - mJobStorageBase;
            assert(index < MAX_JOB_COUNT);
        }
        // 设置任务函数和父任务索引
        job->function = func;
        job->parent = uint16_t(index);
    }
    return job;
}

/**
 * 取消任务
 * 
 * 完成任务并清空指针。
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 */
void JobSystem::cancel(Job*& job) noexcept {
    finish(job);
    job = nullptr;
}

/**
 * 增加任务的引用计数
 * 
 * @param job 任务指针
 * @return 增加引用后的任务指针
 */
JobSystem::Job* JobSystem::retain(Job* job) noexcept {
    Job* retained = job;
    incRef(retained);
    return retained;
}

/**
 * 释放任务的引用
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 */
void JobSystem::release(Job*& job) noexcept {
    decRef(job);
    job = nullptr;
}

/**
 * 将任务添加到当前线程的执行队列
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 */
void JobSystem::run(Job*& job) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    // 获取当前线程的状态
    ThreadState& state(getState());

    // 将任务放入队列
    put(state.workQueue, job);

    // after run() returns, the job is virtually invalid (it'll die on its own)
    // run() 返回后，任务实际上已无效（它会自行销毁）
    job = nullptr;
}

/**
 * 将任务添加到指定线程的执行队列
 * 
 * 更高效的版本，直接使用线程 ID 而不是查找线程映射表。
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 * @param id 线程 ID（必须是当前线程的 ID）
 */
void JobSystem::run(Job*& job, uint8_t id) noexcept {
    HEAVY_FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    // 直接通过索引获取线程状态（避免查找映射表）
    ThreadState& state = mThreadStates[id];
    // 验证确实是当前线程的状态
    assert_invariant(&state == &getState());

    // 将任务放入队列
    put(state.workQueue, job);

    // after run() returns, the job is virtually invalid (it'll die on its own)
    // run() 返回后，任务实际上已无效（它会自行销毁）
    job = nullptr;
}

/**
 * 将任务添加到当前线程的执行队列并保留引用
 * 
 * @param job 任务指针
 * @return 保留引用后的任务指针
 */
JobSystem::Job* JobSystem::runAndRetain(Job* job) noexcept {
    Job* retained = retain(job);
    run(job);
    return retained;
}

/**
 * 等待任务完成并释放
 * 
 * 在等待期间，当前线程会继续执行其他任务（避免浪费 CPU）。
 * 
 * 等待策略：
 * 1. 尝试执行任务（execute）
 * 2. 如果执行失败（队列为空）：
 *    - 检查任务是否已完成，如果完成则退出
 *    - 否则，等待条件变量（任务可能在其他线程执行）
 *    - 唤醒后检查任务状态，如果完成或退出请求则退出
 *    - 否则继续执行其他任务
 * 3. 重复直到任务完成或退出请求
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 */
void JobSystem::waitAndRelease(Job*& job) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);

    assert(job);
    assert(job->refCount.load(std::memory_order_relaxed) >= 1);

    ThreadState& state(getState());
    do {
        // 尝试执行一个任务（可能是其他任务，也可能是我们等待的任务）
        if (UTILS_UNLIKELY(!execute(state))) {
            // test if job has completed first, to possibly avoid taking the lock
            // 首先测试任务是否已完成，可能避免获取锁
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
            // 我们能在这里的唯一方式是，我们等待的任务正在被另一个线程处理：
            //    - 我们从 execute() 返回，这意味着所有队列都为空
            //    - 但我们的任务尚未完成
            //    因此，它正在另一个线程中运行
            //
            // 这可能需要时间，所以我们将等待条件变量，并在添加更多任务时继续处理它们。

            std::unique_lock lock(mWaiterLock);
            uint32_t const runningJobCount = wait(lock, job);
            // we could be waking up because either:
            // - the job we're waiting on has completed
            // - more jobs where added to the JobSystem
            // - we're asked to exit
            // 我们可能因为以下原因被唤醒：
            // - 我们等待的任务已完成
            // - 更多任务被添加到 JobSystem
            // - 我们被要求退出
            if ((runningJobCount & JOB_COUNT_MASK) == 0 || exitRequested()) {
                break;
            }

            // if we get here, it means that
            // - the job we're waiting on is still running, and
            // - we're not asked to exit, and
            // - there were some active jobs
            // So we try to handle one.
            // 如果我们到达这里，这意味着：
            // - 我们等待的任务仍在运行，并且
            // - 我们没有收到退出请求，并且
            // - 有一些活跃任务
            // 所以我们尝试处理一个。
            continue;
        }
    } while (UTILS_LIKELY(!hasJobCompleted(job) && !exitRequested()));

    // 如果等待的是根任务，重置根任务指针
    if (job == mRootJob) {
        mRootJob = nullptr;
    }

    // 释放任务引用
    release(job);
}

/**
 * 运行并等待任务完成
 * 
 * 等价于：
 *   runAndRetain(job);
 *   waitAndRelease(job);
 * 
 * @param job 任务指针（调用后会被置为 nullptr）
 */
void JobSystem::runAndWait(Job*& job) noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
    runAndRetain(job);
    waitAndRelease(job);
}

/**
 * 将当前线程加入线程池
 * 
 * 使当前线程成为 JobSystem 的一部分，可以执行和创建任务。
 * 
 * 处理流程：
 * 1. 检查线程是否已经属于某个 JobSystem
 * 2. 如果已属于，验证是否是当前 JobSystem
 * 3. 否则，分配一个 adoptable 线程槽
 * 4. 设置线程优先级
 * 5. 将线程注册到线程映射表
 * 
 * 注意：
 * - 此线程的队列会立即可选择（即：在我们设置 TLS 之前）
 * - 这不是问题，因为 mThreadState 已预初始化且有效（例如：队列为空）
 */
void JobSystem::adopt() {
    const auto tid = std::this_thread::get_id();

    // 检查线程是否已经属于某个 JobSystem
    std::unique_lock lock(mThreadMapLock);
    auto const iter = mThreadMap.find(tid);
    ThreadState const* const state = iter ==  mThreadMap.end() ? nullptr : iter->second;
    lock.unlock();

    if (state) {
        // we're already part of a JobSystem, do nothing.
        // 我们已经属于某个 JobSystem，不做任何事。
        FILAMENT_CHECK_PRECONDITION(this == state->js)
                << "Called adopt on a thread owned by another JobSystem (" << state->js
                << "), this=" << this << "!";
        return;
    }

    // memory_order_relaxed is safe because we don't take action on this value.
    // memory_order_relaxed 是安全的，因为我们不对此值采取操作。
    // 增加已 adopt 的线程计数
    uint16_t const adopted = mAdoptedThreads.fetch_add(1, std::memory_order_relaxed);
    // 计算线程索引（工作线程数 + 已 adopt 的线程数）
    size_t const index = mThreadCount + adopted;

    FILAMENT_CHECK_POSTCONDITION(index < mThreadStates.size())
            << "Too many calls to adopt(). No more adoptable threads!";

    // all threads adopted by the JobSystem need to run at the same priority
    // 所有被 JobSystem adopt 的线程需要以相同的优先级运行
    setThreadPriority(Priority::DISPLAY);

    // This thread's queue will be selectable immediately (i.e.: before we set its TLS)
    // however, it's not a problem since mThreadState is pre-initialized and valid
    // (e.g.: the queue is empty).
    // 此线程的队列会立即可选择（即：在我们设置 TLS 之前）
    // 但这不是问题，因为 mThreadState 已预初始化且有效（例如：队列为空）

    // 将线程注册到线程映射表
    lock.lock();
    mThreadMap[tid] = &mThreadStates[index];
}

/**
 * 将当前线程从线程池中移除
 * 
 * 用于关闭 JobSystem。调用后，该线程不能再执行或创建任务。
 * 
 * @param tid 当前线程 ID
 */
void JobSystem::emancipate() {
    const auto tid = std::this_thread::get_id();
    std::unique_lock const lock(mThreadMapLock);
    auto const iter = mThreadMap.find(tid);
    ThreadState const* const state = iter ==  mThreadMap.end() ? nullptr : iter->second;
    FILAMENT_CHECK_PRECONDITION(state) << "this thread is not an adopted thread";
    FILAMENT_CHECK_PRECONDITION(state->js == this) << "this thread is not adopted by us";
    // 从线程映射表中移除
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
