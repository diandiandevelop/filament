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

#ifndef TNT_UTILS_JOBSYSTEM_H
#define TNT_UTILS_JOBSYSTEM_H

#include <utility>
#include <utils/Allocator.h>
#include <utils/architecture.h>
#include <utils/compiler.h>
#include <utils/Condition.h>
#include <utils/memalign.h>
#include <utils/Mutex.h>
#include <utils/Slice.h>
#include <utils/ostream.h>
#include <utils/WorkStealingDequeue.h>

#include <tsl/robin_map.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <type_traits>
#include <thread>
#include <vector>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

namespace utils {

class JobSystem {
    static constexpr size_t MAX_JOB_COUNT = 1 << 14; // 16384
    static constexpr uint32_t JOB_COUNT_MASK = MAX_JOB_COUNT - 1;
    static constexpr uint32_t WAITER_COUNT_SHIFT = 24;
    static_assert(MAX_JOB_COUNT <= 0x7FFE, "MAX_JOB_COUNT must be <= 0x7FFE");
    using WorkQueue = WorkStealingDequeue<uint16_t, MAX_JOB_COUNT>;
    using Mutex = utils::Mutex;
    using Condition = utils::Condition;

public:
    class Job;

    using ThreadId = uint8_t;

    using JobFunc = void(*)(void*, JobSystem&, Job*);

    static constexpr ThreadId invalidThreadId = 0xff;

    class alignas(CACHELINE_SIZE) Job {
    public:
        Job() noexcept {} /* = default; */ /* clang bug */ // NOLINT(modernize-use-equals-default,cppcoreguidelines-pro-type-member-init)
        Job(const Job&) = delete;
        Job(Job&&) = delete;

    private:
        friend class JobSystem;

        // Size is chosen so that we can store at least std::function<>
        // the alignas() qualifier ensures we're multiple of a cache-line.
        static constexpr size_t JOB_STORAGE_SIZE_BYTES =
                sizeof(std::function<void()>) > 48 ? sizeof(std::function<void()>) : 48;
        static constexpr size_t JOB_STORAGE_SIZE_WORDS =
                (JOB_STORAGE_SIZE_BYTES + sizeof(void*) - 1) / sizeof(void*);

        // keep it first, so it's correctly aligned with all architectures
        // this is where we store the job's data, typically a std::function<>
                                                                // v7 | v8
        void* storage[JOB_STORAGE_SIZE_WORDS];                  // 48 | 48
        JobFunc function;                                       //  4 |  8
        uint16_t parent;                                        //  2 |  2
        mutable ThreadId id = invalidThreadId;                  //  1 |  1
        mutable std::atomic<uint8_t> refCount = { 1 };          //  1 |  1
        std::atomic<uint32_t> runningJobCount = { 1 };          //  4 |  4
                                                                //  4 |  0 (padding)
                                                                // 64 | 64
    };

#ifndef WIN32
    // on windows std::function<void()> is bigger and forces the whole structure to be larger
    static_assert(sizeof(Job) == 64);
#endif

    explicit JobSystem(size_t threadCount = 0, size_t adoptableThreadsCount = 1) noexcept;

    ~JobSystem();

    // Make the current thread part of the thread pool.
    void adopt();

    // Remove this adopted thread from the parent. This is intended to be used for
    // shutting down a JobSystem. In particular, this doesn't allow the parent to
    // adopt more thread.
    void emancipate();


    // If a parent is not specified when creating a job, that job will automatically take the
    // root job as a parent.
    // The root job is reset when waited on.
    Job* setRootJob(Job* job) noexcept { return mRootJob = job; }

     // use setRootJob() instead
    UTILS_DEPRECATED
    Job* setMasterJob(Job* job) noexcept { return setRootJob(job); }


    Job* create(Job* parent, JobFunc func) noexcept;

    // NOTE: All methods below must be called from the same thread and that thread must be
    // owned by JobSystem's thread pool.

    /*
     * Job creation examples:
     * ----------------------
     *
     *  struct Functor {
     *   uintptr_t storage[6];
     *   void operator()(JobSystem&, Jobsystem::Job*);
     *  } functor;
     *
     *  struct Foo {
     *   uintptr_t storage[6];
     *   void method(JobSystem&, Jobsystem::Job*);
     *  } foo;
     *
     *  Functor and Foo size muse be <= uintptr_t[6]
     *
     *   createJob()
     *   createJob(parent)
     *   createJob<Foo, &Foo::method>(parent, &foo)
     *   createJob<Foo, &Foo::method>(parent, foo)
     *   createJob<Foo, &Foo::method>(parent, std::ref(foo))
     *   createJob(parent, functor)
     *   createJob(parent, std::ref(functor))
     *   createJob(parent, [ up-to 6 uintptr_t ](JobSystem*, Jobsystem::Job*){ })
     *
     *  Utility functions:
     *  ------------------
     *    These are less efficient, but handle any size objects using the heap if needed.
     *    (internally uses std::function<>), and don't require the callee to take
     *    a (JobSystem&, Jobsystem::Job*) as parameter.
     *
     *  struct BigFoo {
     *   uintptr_t large[16];
     *   void operator()();
     *   void method(int answerToEverything);
     *   static void exec(BigFoo&) { }
     *  } bigFoo;
     *
     *   jobs::createJob(js, parent, [ any-capture ](int answerToEverything){}, 42);
     *   jobs::createJob(js, parent, &BigFoo::method, &bigFoo, 42);
     *   jobs::createJob(js, parent, &BigFoo::exec, std::ref(bigFoo));
     *   jobs::createJob(js, parent, bigFoo);
     *   jobs::createJob(js, parent, std::ref(bigFoo));
     *   etc...
     *
     *  struct SmallFunctor {
     *   uintptr_t storage[3];
     *   void operator()(T* data, size_t count);
     *  } smallFunctor;
     *
     *   jobs::parallel_for(js, data, count, [ up-to 3 uintptr_t ](T* data, size_t count) { });
     *   jobs::parallel_for(js, data, count, smallFunctor);
     *   jobs::parallel_for(js, data, count, std::ref(smallFunctor));
     *
     */

    // creates an empty (no-op) job with an optional parent
    Job* createJob(Job* parent = nullptr) noexcept {
        return create(parent, nullptr);
    }

    // creates a job from a KNOWN method pointer w/ object passed by pointer
    // the caller must ensure the object will outlive the Job
    template<typename T, void(T::*method)(JobSystem&, Job*)>
    Job* createJob(Job* parent, T* data) noexcept {
        Job* job = create(parent, +[](void* storage, JobSystem& js, Job* job) {
            T* const that = static_cast<T*>(static_cast<void**>(storage)[0]);
            (that->*method)(js, job);
        });
        if (job) {
            job->storage[0] = data;
        }
        return job;
    }

    // creates a job from a KNOWN method pointer w/ object passed by value
    template<typename T, void(T::*method)(JobSystem&, Job*)>
    Job* createJob(Job* parent, T data) noexcept {
        static_assert(sizeof(data) <= sizeof(Job::storage), "user data too large");
        Job* job = create(parent, [](void* storage, JobSystem& js, Job* job) {
            T* const that = static_cast<T*>(storage);
            (that->*method)(js, job);
            that->~T();
        });
        if (job) {
            new(job->storage) T(std::move(data));
        }
        return job;
    }

    // creates a job from a KNOWN method pointer w/ object passed by value
    template<typename T, void(T::*method)(JobSystem&, Job*), typename ... ARGS>
    Job* emplaceJob(Job* parent, ARGS&& ... args) noexcept {
        static_assert(sizeof(T) <= sizeof(Job::storage), "user data too large");
        Job* job = create(parent, [](void* storage, JobSystem& js, Job* job) {
            T* const that = static_cast<T*>(storage);
            (that->*method)(js, job);
            that->~T();
        });
        if (job) {
            new(job->storage) T(std::forward<ARGS>(args)...);
        }
        return job;
    }

    // creates a job from a functor passed by value
    template<typename T>
    Job* createJob(Job* parent, T functor) noexcept {
        static_assert(sizeof(functor) <= sizeof(Job::storage), "functor too large");
        Job* job = create(parent, [](void* storage, JobSystem& js, Job* job){
            T* const that = static_cast<T*>(storage);
            that->operator()(js, job);
            that->~T();
        });
        if (job) {
            new(job->storage) T(std::move(functor));
        }
        return job;
    }

    // creates a job from a functor passed by value
    template<typename T, typename ... ARGS>
    Job* emplaceJob(Job* parent, ARGS&& ... args) noexcept {
        static_assert(sizeof(T) <= sizeof(Job::storage), "functor too large");
        Job* job = create(parent, [](void* storage, JobSystem& js, Job* job){
            T* const that = static_cast<T*>(storage);
            that->operator()(js, job);
            that->~T();
        });
        if (job) {
            new(job->storage) T(std::forward<ARGS>(args)...);
        }
        return job;
    }


    /*
     * Jobs are normally finished automatically, this can be used to cancel a job before it is run.
     *
     * Never use this once a flavor of run() has been called.
     */
    void cancel(Job*& job) noexcept;

    /*
     * Adds a reference to a Job.
     *
     * This allows the caller to waitAndRelease() on this job from multiple threads.
     * Use runAndWait() if waiting from multiple threads is not needed.
     *
     * This job MUST BE waited on with waitAndRelease(), or released with release().
     */
    static Job* retain(Job* job) noexcept;

    /*
     * Releases a reference from a Job obtained with runAndRetain() or a call to retain().
     *
     * The job can't be used after this call.
     */
    void release(Job*& job) noexcept;
    void release(Job*&& job) noexcept {
        Job* p = job;
        release(p);
    }

    /*
     * Add job to this thread's execution queue. Its reference will drop automatically.
     * The current thread must be owned by JobSystem's thread pool. See adopt().
     *
     * The job can't be used after this call.
     */
    void run(Job*& job) noexcept;
    void run(Job*&& job) noexcept { // allows run(createJob(...));
        Job* p = job;
        run(p);
    }

    /*
     * Add job to this thread's execution queue. Its reference will drop automatically.
     * The current thread must be owned by JobSystem's thread pool. See adopt().
     * id must be the current thread id obtained with JobSystem::getThreadId(Job*). This
     * API is more efficient than the methods above.
     *
     * The job can't be used after this call.
     */
    void run(Job*& job, ThreadId id) noexcept;
    void run(Job*&& job, ThreadId const id) noexcept { // allows run(createJob(...));
        Job* p = job;
        run(p, id);
    }

    /*
     * Add job to this thread's execution queue and keep a reference to it.
     * The current thread must be owned by JobSystem's thread pool. See adopt().
     *
     * This job MUST BE waited on with wait(), or released with release().
     */
    Job* runAndRetain(Job* job) noexcept;

    /*
     * Wait on a job and destroys it.
     * The current thread must be owned by JobSystem's thread pool. See adopt().
     *
     * The job must first be obtained from runAndRetain() or retain().
     * The job can't be used after this call.
     */
    void waitAndRelease(Job*& job) noexcept;

    /*
     * Runs and wait for a job. This is equivalent to calling
     *  runAndRetain(job);
     *  wait(job);
     *
     * The job can't be used after this call.
     */
    void runAndWait(Job*& job) noexcept;
    void runAndWait(Job*&& job) noexcept { // allows runAndWait(createJob(...));
        Job* p = job;
        runAndWait(p);
    }

    // for debugging
    friend io::ostream& operator << (io::ostream& out, JobSystem const& js);


    // utility functions...

    // set the name of the current thread (on OSes that support it)
    static void setThreadName(const char* threadName) noexcept;

    enum class Priority {
        NORMAL,
        DISPLAY,
        URGENT_DISPLAY,
        BACKGROUND
    };

    static void setThreadPriority(Priority priority) noexcept;
    static void setThreadAffinityById(size_t id) noexcept;

    size_t getParallelSplitCount() const noexcept {
        return mParallelSplitCount;
    }

    size_t getThreadCount() const { return mThreadCount; }

    // returns the current ThreadId, which can be used with run(). This method can only be
    // called from a job's function.
    static ThreadId getThreadId(Job const* job) noexcept {
        assert(job->id != invalidThreadId);
        return job->id;
    }

private:
    // this is just to avoid using std::default_random_engine, since we're in a public header.
    class default_random_engine {
        static constexpr uint32_t m = 0x7fffffffu;
        uint32_t mState; // must be 0 < seed < 0x7fffffff
    public:
        using result_type = uint32_t;

        static constexpr result_type min() noexcept {
            return 1;
        }

        static constexpr result_type max() noexcept {
            return m - 1;
        }

        constexpr explicit default_random_engine(uint32_t const seed = 1u) noexcept
                : mState(((seed % m) == 0u) ? 1u : seed % m) {
        }

        uint32_t operator()() noexcept {
            return mState = uint32_t((uint64_t(mState) * 48271u) % m);
        }
    };

    struct alignas(CACHELINE_SIZE) ThreadState {    // this causes 40-bytes padding
        // make sure storage is cache-line aligned
        WorkQueue workQueue;

        // these are not accessed by the worker threads
        alignas(CACHELINE_SIZE)         // this causes 56-bytes padding
        JobSystem* js;                  // this is in fact const and always initialized
        std::thread thread;             // unused for adopted threads
        default_random_engine rndGen;
    };

    static_assert(sizeof(ThreadState) % CACHELINE_SIZE == 0,
            "ThreadState doesn't align to a cache line");

    ThreadState& getState();

    static void incRef(Job const* job) noexcept;
    void decRef(Job const* job) noexcept;

    Job* allocateJob() noexcept;
    ThreadState* getStateToStealFrom(ThreadState& state) noexcept;
    static bool hasJobCompleted(Job const* job) noexcept;

    void requestExit() noexcept;
    bool exitRequested() const noexcept;
    bool hasActiveJobs() const noexcept;

    void loop(ThreadState* state);
    bool execute(ThreadState& state) noexcept;
    Job* steal(ThreadState& state) noexcept;
    void finish(Job* job) noexcept;

    void put(WorkQueue& workQueue, Job const* job) noexcept;
    Job* pop(WorkQueue& workQueue) noexcept;
    Job* steal(WorkQueue& workQueue) noexcept;

    [[nodiscard]]
    uint32_t wait(std::unique_lock<Mutex>& lock, Job* job) noexcept;
    void wait(std::unique_lock<Mutex>& lock) noexcept;
    void wakeAll() noexcept;
    void wakeOne() noexcept;

    // these have thread contention, keep them together
    Mutex mWaiterLock;
    Condition mWaiterCondition;

    std::atomic<int32_t> mActiveJobs = { 0 };
    Arena<ObjectPoolAllocator<Job>, LockingPolicy::Mutex> mJobPool;

    template <typename T>
    using aligned_vector = std::vector<T, STLAlignedAllocator<T>>;

    // These are essentially const, make sure they're on a different cache-lines than the
    // read-write atomics.
    // We can't use "alignas(CACHELINE_SIZE)" because the standard allocator can't make this
    // guarantee.
    char padding[CACHELINE_SIZE];

    alignas(16) // at least we align to half (or quarter) cache-line
    aligned_vector<ThreadState> mThreadStates;          // actual data is stored offline
    std::atomic<bool> mExitRequested = { false };       // this one is almost never written
    std::atomic<uint16_t> mAdoptedThreads = { 0 };      // this one is almost never written
    Job* const mJobStorageBase;                         // Base for conversion to indices
    uint16_t mThreadCount = 0;                          // total # of threads in the pool
    uint8_t mParallelSplitCount = 0;                    // # of split allowable in parallel_for
    Job* mRootJob = nullptr;

    Mutex mThreadMapLock; // this should have very little contention
    tsl::robin_map<std::thread::id, ThreadState *> mThreadMap;
};

// -------------------------------------------------------------------------------------------------
// Utility functions built on top of JobSystem

namespace jobs {

// These are convenience C++11 style job creation methods that support lambdas
//
// IMPORTANT: these are less efficient to call and may perform heap allocation
//            depending on the capture and parameters
//
template<typename CALLABLE, typename ... ARGS>
JobSystem::Job* createJob(JobSystem& js, JobSystem::Job* parent,
        CALLABLE&& func, ARGS&&... args) noexcept {
    struct Data {
        explicit Data(std::function<void()> f) noexcept: f(std::move(f)) {}
        std::function<void()> f;
        // Renaming the method below could cause an Arrested Development.
        void gob(JobSystem&, JobSystem::Job*) noexcept { f(); }
    };
    return js.emplaceJob<Data, &Data::gob>(parent,
            std::bind(std::forward<CALLABLE>(func), std::forward<ARGS>(args)...));
}

template<typename CALLABLE, typename T, typename ... ARGS,
        typename = std::enable_if_t<
                std::is_member_function_pointer_v<std::remove_reference_t<CALLABLE>>
        >
>
JobSystem::Job* createJob(JobSystem& js, JobSystem::Job* parent,
        CALLABLE&& func, T&& o, ARGS&&... args) noexcept {
    struct Data {
        explicit Data(std::function<void()> f) noexcept: f(std::move(f)) {}
        std::function<void()> f;
        // Renaming the method below could cause an Arrested Development.
        void gob(JobSystem&, JobSystem::Job*) noexcept { f(); }
    };
    return js.emplaceJob<Data, &Data::gob>(parent,
            std::bind(std::forward<CALLABLE>(func), std::forward<T>(o), std::forward<ARGS>(args)...));
}


namespace details {

/**
 * ParallelForJobData（并行循环任务数据）
 * 
 * 这是 parallel_for 的内部数据结构，用于递归分割任务。
 * 
 * 设计思路：
 * - 将大任务递归分割成小任务
 * - 每个分割创建子 Job，并行执行
 * - 当任务足够小时，直接执行
 * 
 * @tparam S 分割器类型（SplitterType）
 * @tparam F 函数对象类型（Functor）
 */
template<typename S, typename F>
struct ParallelForJobData {
    using SplitterType = S;
    using Functor = F;
    using JobData = ParallelForJobData;
    using size_type = uint32_t;

    /**
     * 构造函数
     * 
     * @param start 起始索引
     * @param count 任务数量
     * @param splits 当前分割深度
     * @param functor 要执行的函数对象
     * @param splitter 分割器（决定是否继续分割）
     */
    ParallelForJobData(size_type const start, size_type const count, uint8_t const splits,
            Functor functor,
            const SplitterType& splitter) noexcept
            : start(start), count(count),
              functor(std::move(functor)),
              splits(splits),
              splitter(splitter) {
    }

    /**
     * 并行执行函数
     * 
     * 这是 parallel_for 的核心逻辑：
     * 1. 检查是否应该继续分割
     * 2. 如果应该分割：
     *    - 创建左半部分的子 Job
     *    - 运行左半部分的 Job
     *    - 在当前线程处理右半部分（重用当前 Job，避免创建开销）
     * 3. 如果不应该分割：
     *    - 直接执行任务
     * 
     * @param js JobSystem 引用
     * @param parent 父 Job（必须非空）
     */
    void parallelWithJobs(JobSystem& js, JobSystem::Job* parent) noexcept {
        assert(parent);

        /**
         * 这个分支经常被错误预测（两个分支各占 50% 的调用）
         * 使用 goto 避免分支预测失败的开销
         */
right_side:
        /**
         * 检查是否应该继续分割
         * 
         * 分割条件由 Splitter 决定，通常基于：
         * - 当前分割深度（splits）
         * - 任务数量（count）
         */
        if (splitter.split(splits, count)) {
            /**
             * 应该分割：将任务分成两半
             */
            const size_type lc = count / 2;  // 左半部分的数量
            
            /**
             * 创建左半部分的子 Job
             * 
             * 左半部分：[start, start + lc)
             * 分割深度：splits + 1
             */
            JobSystem::Job* l = js.emplaceJob<JobData, &JobData::parallelWithJobs>(parent,
                    start, lc, splits + uint8_t(1), functor, splitter);
            
            if (UTILS_UNLIKELY(l == nullptr)) {
                /**
                 * 无法创建 Job（Job 池可能已满）
                 * 
                 * 在这种情况下，停止分割，直接执行任务
                 */
                goto execute;
            }

            /**
             * 运行左半部分的 Job
             * 
             * 在创建右半部分之前启动左半部分，这样即使 Job 创建失败，
             * 我们也能并行化（虽然这种情况很少见）
             * 
             * 使用父 Job 的线程 ID，确保子 Job 在同一个线程池中执行
             */
            js.run(l, JobSystem::getThreadId(parent));

            /**
             * 处理右半部分（重用当前 Job）
             * 
             * 不创建新的 Job，而是重用当前 Job 处理右半部分
             * 这样可以避免 Job 创建的开销
             * 
             * 右半部分：[start + lc, start + count)
             */
            start += lc;      // 更新起始索引
            count -= lc;      // 更新任务数量
            ++splits;         // 增加分割深度
            goto right_side;  // 继续处理右半部分（可能继续分割）

        } else {
            /**
             * 不应该分割：直接执行任务
             * 
             * 当任务足够小或达到最大分割深度时，直接执行
             */
execute:
            // 分割完成，执行实际工作！
            functor(start, count);
        }
    }

private:
    size_type start;            // 起始索引（4 字节）
    size_type count;            // 任务数量（4 字节）
    Functor functor;            // 函数对象（大小取决于类型）
    uint8_t splits;             // 当前分割深度（1 字节）
    SplitterType splitter;      // 分割器（1 字节）
};

} // namespace details


// parallel jobs with start/count indices
template<typename S, typename F>
JobSystem::Job* parallel_for(JobSystem& js, JobSystem::Job* parent,
        uint32_t start, uint32_t count, F functor, const S& splitter) noexcept {
    using JobData = details::ParallelForJobData<S, F>;
    return js.emplaceJob<JobData, &JobData::parallelWithJobs>(parent,
            start, count, 0, std::move(functor), splitter);
}

// parallel jobs with pointer/count
template<typename T, typename S, typename F>
JobSystem::Job* parallel_for(JobSystem& js, JobSystem::Job* parent,
        T* data, uint32_t count, F functor, const S& splitter) noexcept {
    auto user = [data, f = std::move(functor)](uint32_t s, uint32_t c) {
        f(data + s, c);
    };
    using JobData = details::ParallelForJobData<S, decltype(user)>;
    return js.emplaceJob<JobData, &JobData::parallelWithJobs>(parent,
            0, count, 0, std::move(user), splitter);
}

// parallel jobs on a Slice<>
template<typename T, typename S, typename F>
JobSystem::Job* parallel_for(JobSystem& js, JobSystem::Job* parent,
        Slice<T> slice, F functor, const S& splitter) noexcept {
    return parallel_for(js, parent, slice.data(), slice.size(), functor, splitter);
}


/**
 * CountSplitter（计数分割器）
 * 
 * 基于任务数量决定是否继续分割的分割器。
 * 
 * 分割条件：
 * - 当前分割深度 < MAX_SPLITS（避免过度分割）
 * - 任务数量 >= COUNT * 2（确保分割后每个子任务至少有 COUNT 个任务）
 * 
 * 使用示例：
 * ```cpp
 * // 当任务数 >= 128 时分割，最多分割 12 次
 * jobs::CountSplitter<64, 12> splitter;
 * 
 * // 当任务数 >= 64 时分割，最多分割 8 次
 * jobs::CountSplitter<32, 8> splitter;
 * ```
 * 
 * @tparam COUNT 最小任务数阈值（分割后每个子任务至少 COUNT 个任务）
 * @tparam MAX_SPLITS 最大分割深度（默认 12）
 */
template<size_t COUNT, size_t MAX_SPLITS = 12>
class CountSplitter {
public:
    /**
     * 判断是否应该分割
     * 
     * @param splits 当前分割深度
     * @param count 当前任务数量
     * @return true 如果应该分割，false 否则
     * 
     * 分割条件：
     * 1. splits < MAX_SPLITS：未达到最大分割深度
     * 2. count >= COUNT * 2：任务数足够大，分割后每个子任务至少有 COUNT 个任务
     * 
     * 示例：
     * - COUNT = 64, count = 100：应该分割（100 >= 128 为 false，但 100 >= 64*2 为 false，实际不会分割）
     * - COUNT = 64, count = 200：应该分割（200 >= 128 为 true）
     * - COUNT = 64, splits = 12：不应该分割（已达到最大分割深度）
     */
    bool split(size_t const splits, size_t const count) const noexcept {
        return (splits < MAX_SPLITS && count >= COUNT * 2);
    }
};

} // namespace jobs
} // namespace utils

#endif // TNT_UTILS_JOBSYSTEM_H
