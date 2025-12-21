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

/**
 * JobSystem（任务系统）
 * 
 * 一个高性能的工作窃取（work-stealing）任务调度系统。
 * 
 * 设计特点：
 * - 工作窃取：当线程自己的队列为空时，从其他线程窃取任务
 * - 无锁队列：使用无锁数据结构减少竞争
 * - 缓存友好：Job 结构对齐到缓存行，减少伪共享
 * - 父子任务：支持任务依赖关系，父任务等待所有子任务完成
 * 
 * 使用流程：
 * 1. 创建 JobSystem（指定线程数）
 * 2. 调用 adopt() 将当前线程加入线程池（可选）
 * 3. 创建 Job（使用 createJob()）
 * 4. 运行 Job（使用 run() 或 runAndWait()）
 * 5. 等待 Job 完成（使用 waitAndRelease()）
 */
class JobSystem {
    static constexpr size_t MAX_JOB_COUNT = 1 << 14; // 16384
    // 最大任务数量：16384
    static constexpr uint32_t JOB_COUNT_MASK = MAX_JOB_COUNT - 1;
    // 任务计数掩码：用于从 runningJobCount 中提取任务计数
    static constexpr uint32_t WAITER_COUNT_SHIFT = 24;
    // 等待者计数偏移：runningJobCount 的高 8 位存储等待者数量
    static_assert(MAX_JOB_COUNT <= 0x7FFE, "MAX_JOB_COUNT must be <= 0x7FFE");
    using WorkQueue = WorkStealingDequeue<uint16_t, MAX_JOB_COUNT>;
    // 工作队列：无锁的双端队列，支持工作窃取
    using Mutex = utils::Mutex;
    using Condition = utils::Condition;

public:
    class Job;

    using ThreadId = uint8_t;
    // 线程 ID 类型：8 位，最多支持 256 个线程

    using JobFunc = void(*)(void*, JobSystem&, Job*);
    // 任务函数类型：接收存储指针、JobSystem 引用和 Job 指针

    static constexpr ThreadId invalidThreadId = 0xff;
    // 无效线程 ID：0xff

    /**
     * Job（任务）
     * 
     * 表示一个可执行的任务单元。
     * 
     * 设计特点：
     * - 对齐到缓存行（64 字节）：避免伪共享，提高性能
     * - 内联存储：使用 storage 数组存储函数对象，避免堆分配
     * - 引用计数：支持多个线程等待同一个任务
     * - 父子关系：支持任务依赖，父任务等待所有子任务完成
     * 
     * 内存布局（64 字节，对齐到缓存行）：
     * - storage: 48 字节 - 存储函数对象（如 std::function）
     * - function: 4/8 字节 - 任务函数指针
     * - parent: 2 字节 - 父任务索引
     * - id: 1 字节 - 执行线程 ID
     * - refCount: 1 字节 - 引用计数（原子）
     * - runningJobCount: 4 字节 - 运行中的子任务计数（原子）
     * - padding: 4/0 字节 - 对齐填充
     */
    class alignas(CACHELINE_SIZE) Job {
    public:
        Job() noexcept {} /* = default; */ /* clang bug */ // NOLINT(modernize-use-equals-default,cppcoreguidelines-pro-type-member-init)
        Job(const Job&) = delete;
        Job(Job&&) = delete;

    private:
        friend class JobSystem;

        // Size is chosen so that we can store at least std::function<>
        // the alignas() qualifier ensures we're multiple of a cache-line.
        // 大小选择为至少可以存储 std::function<>
        // alignas() 限定符确保是缓存行的倍数
        static constexpr size_t JOB_STORAGE_SIZE_BYTES =
                sizeof(std::function<void()>) > 48 ? sizeof(std::function<void()>) : 48;
        // 存储大小（字节）：至少 48 字节，或 std::function<void()> 的大小（取较大者）
        static constexpr size_t JOB_STORAGE_SIZE_WORDS =
                (JOB_STORAGE_SIZE_BYTES + sizeof(void*) - 1) / sizeof(void*);
        // 存储大小（字）：向上取整到指针大小的倍数

        // keep it first, so it's correctly aligned with all architectures
        // this is where we store the job's data, typically a std::function<>
        // 保持它在第一位，以便在所有架构上正确对齐
        // 这里存储任务的数据，通常是 std::function<>
                                                                // v7 | v8
        void* storage[JOB_STORAGE_SIZE_WORDS];                  // 48 | 48
        // 存储数组：用于存储函数对象（如 lambda、std::function）
        JobFunc function;                                       //  4 |  8
        // 任务函数指针：如果为 nullptr，表示空任务（no-op）
        uint16_t parent;                                        //  2 |  2
        // 父任务索引：0x7FFF 表示无父任务（根任务）
        mutable ThreadId id = invalidThreadId;                  //  1 |  1
        // 执行线程 ID：记录执行此任务的线程 ID（用于调试和性能分析）
        mutable std::atomic<uint8_t> refCount = { 1 };          //  1 |  1
        // 引用计数（原子）：支持多个线程等待同一个任务
        std::atomic<uint32_t> runningJobCount = { 1 };          //  4 |  4
        // 运行中的子任务计数（原子）：
        // - 低 24 位：运行中的子任务数量（包括自己）
        // - 高 8 位：等待此任务完成的线程数量
                                                                //  4 |  0 (padding)
                                                                // 64 | 64
    };

#ifndef WIN32
    // on windows std::function<void()> is bigger and forces the whole structure to be larger
    static_assert(sizeof(Job) == 64);
#endif

    /**
     * 构造函数
     * 
     * @param threadCount 工作线程数（0 表示自动检测）
     * @param adoptableThreadsCount 可被 adopt 的线程槽数量（用于外部线程加入）
     */
    explicit JobSystem(size_t threadCount = 0, size_t adoptableThreadsCount = 1) noexcept;

    /**
     * 析构函数
     * 
     * 请求所有线程退出并等待它们完成。
     */
    ~JobSystem();

    // Make the current thread part of the thread pool.
    /**
     * 将当前线程加入线程池
     * 
     * 使当前线程成为 JobSystem 的一部分，可以执行和创建任务。
     * 必须在调用 run()、wait() 等方法前调用。
     */
    void adopt();

    // Remove this adopted thread from the parent. This is intended to be used for
    // shutting down a JobSystem. In particular, this doesn't allow the parent to
    // adopt more thread.
    /**
     * 将当前线程从线程池中移除
     * 
     * 用于关闭 JobSystem。调用后，该线程不能再执行或创建任务。
     * 注意：这不会允许父 JobSystem 接受更多线程。
     */
    void emancipate();


    // If a parent is not specified when creating a job, that job will automatically take the
    // root job as a parent.
    // The root job is reset when waited on.
    /**
     * 设置根任务
     * 
     * 如果创建任务时未指定父任务，该任务会自动将根任务作为父任务。
     * 根任务在等待时会被重置。
     * 
     * @param job 根任务指针
     * @return 设置的根任务指针
     */
    Job* setRootJob(Job* job) noexcept { return mRootJob = job; }

     // use setRootJob() instead
    /**
     * 设置主任务（已弃用）
     * 
     * 使用 setRootJob() 代替。
     */
    UTILS_DEPRECATED
    Job* setMasterJob(Job* job) noexcept { return setRootJob(job); }


    /**
     * 创建任务（内部方法）
     * 
     * @param parent 父任务（nullptr 表示使用根任务）
     * @param func 任务函数指针
     * @return 创建的任务指针，如果创建失败返回 nullptr
     */
    Job* create(Job* parent, JobFunc func) noexcept;

    // NOTE: All methods below must be called from the same thread and that thread must be
    // owned by JobSystem's thread pool.
    // 注意：下面的所有方法必须从同一线程调用，且该线程必须属于 JobSystem 的线程池。

    /*
     * Job creation examples:
     * ----------------------
     * 任务创建示例：
     * 
     * 高效方法（内联存储，无堆分配）：
     * - 函数对象大小必须 <= uintptr_t[6]（48 字节）
     * - 函数签名：void operator()(JobSystem&, JobSystem::Job*)
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
     *  Functor 和 Foo 的大小必须 <= uintptr_t[6]
     *
     *   createJob()                    // 创建空任务
     *   createJob(parent)              // 创建空任务，指定父任务
     *   createJob<Foo, &Foo::method>(parent, &foo)      // 方法指针，对象指针
     *   createJob<Foo, &Foo::method>(parent, foo)       // 方法指针，对象值
     *   createJob<Foo, &Foo::method>(parent, std::ref(foo))  // 方法指针，对象引用
     *   createJob(parent, functor)     // 函数对象，值传递
     *   createJob(parent, std::ref(functor))  // 函数对象，引用传递
     *   createJob(parent, [ up-to 6 uintptr_t ](JobSystem*, Jobsystem::Job*){ })  // Lambda
     *
     *  Utility functions:
     *  ------------------
     *    These are less efficient, but handle any size objects using the heap if needed.
     *    (internally uses std::function<>), and don't require the callee to take
     *    a (JobSystem&, Jobsystem::Job*) as parameter.
     *  工具函数：
     *  - 效率较低，但可以处理任意大小的对象（必要时使用堆分配）
     *  - 内部使用 std::function<>，不需要函数签名包含 (JobSystem&, JobSystem::Job*)
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
    /**
     * 创建空任务（无操作）
     * 
     * @param parent 父任务（nullptr 表示使用根任务）
     * @return 创建的任务指针
     */
    Job* createJob(Job* parent = nullptr) noexcept {
        return create(parent, nullptr);
    }

    // creates a job from a KNOWN method pointer w/ object passed by pointer
    // the caller must ensure the object will outlive the Job
    /**
     * 从已知方法指针创建任务（对象通过指针传递）
     * 
     * 调用者必须确保对象在任务执行期间保持有效。
     * 
     * @tparam T 对象类型
     * @tparam method 方法指针
     * @param parent 父任务
     * @param data 对象指针
     * @return 创建的任务指针
     */
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
    /**
     * 从已知方法指针创建任务（对象通过值传递）
     * 
     * 对象会被复制到任务的存储中，任务执行后自动析构。
     * 
     * @tparam T 对象类型（大小必须 <= Job::storage）
     * @tparam method 方法指针
     * @param parent 父任务
     * @param data 对象值
     * @return 创建的任务指针
     */
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
    /**
     * 从已知方法指针创建任务（就地构造对象）
     * 
     * 使用参数在任务的存储中直接构造对象，避免额外的复制。
     * 
     * @tparam T 对象类型（大小必须 <= Job::storage）
     * @tparam method 方法指针
     * @tparam ARGS 构造参数类型
     * @param parent 父任务
     * @param args 构造参数（转发）
     * @return 创建的任务指针
     */
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
    /**
     * 从函数对象创建任务（值传递）
     * 
     * 函数对象会被复制到任务的存储中，任务执行后自动析构。
     * 
     * @tparam T 函数对象类型（大小必须 <= Job::storage）
     * @param parent 父任务
     * @param functor 函数对象
     * @return 创建的任务指针
     */
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
    /**
     * 从函数对象创建任务（就地构造）
     * 
     * 使用参数在任务的存储中直接构造函数对象，避免额外的复制。
     * 
     * @tparam T 函数对象类型（大小必须 <= Job::storage）
     * @tparam ARGS 构造参数类型
     * @param parent 父任务
     * @param args 构造参数（转发）
     * @return 创建的任务指针
     */
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
    /**
     * 取消任务
     * 
     * 在任务运行前取消它。任务通常会自动完成，此方法用于在运行前取消。
     * 
     * 注意：一旦调用了任何形式的 run()，就不要再使用此方法。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
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
    /**
     * 增加任务的引用计数
     * 
     * 允许调用者从多个线程等待此任务。
     * 如果不需要从多个线程等待，使用 runAndWait()。
     * 
     * 此任务必须使用 waitAndRelease() 等待，或使用 release() 释放。
     * 
     * @param job 任务指针
     * @return 增加引用后的任务指针
     */
    static Job* retain(Job* job) noexcept;

    /*
     * Releases a reference from a Job obtained with runAndRetain() or a call to retain().
     *
     * The job can't be used after this call.
     */
    /**
     * 释放任务的引用
     * 
     * 释放通过 runAndRetain() 或 retain() 获得的引用。
     * 
     * 调用后任务不能再使用。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
     */
    void release(Job*& job) noexcept;
    /**
     * 释放任务的引用（右值版本）
     * 
     * 允许 release(createJob(...)) 这样的调用。
     */
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
    /**
     * 将任务添加到当前线程的执行队列
     * 
     * 任务的引用会自动减少。当前线程必须属于 JobSystem 的线程池（见 adopt()）。
     * 
     * 调用后任务不能再使用。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
     */
    void run(Job*& job) noexcept;
    /**
     * 将任务添加到当前线程的执行队列（右值版本）
     * 
     * 允许 run(createJob(...)) 这样的调用。
     */
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
    /**
     * 将任务添加到指定线程的执行队列
     * 
     * 任务的引用会自动减少。当前线程必须属于 JobSystem 的线程池（见 adopt()）。
     * id 必须是当前线程 ID（通过 JobSystem::getThreadId(Job*) 获得）。
     * 此 API 比上面的方法更高效（避免线程 ID 查找）。
     * 
     * 调用后任务不能再使用。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
     * @param id 线程 ID
     */
    void run(Job*& job, ThreadId id) noexcept;
    /**
     * 将任务添加到指定线程的执行队列（右值版本）
     */
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
    /**
     * 将任务添加到当前线程的执行队列并保留引用
     * 
     * 当前线程必须属于 JobSystem 的线程池（见 adopt()）。
     * 
     * 此任务必须使用 waitAndRelease() 等待，或使用 release() 释放。
     * 
     * @param job 任务指针
     * @return 保留引用后的任务指针
     */
    Job* runAndRetain(Job* job) noexcept;

    /*
     * Wait on a job and destroys it.
     * The current thread must be owned by JobSystem's thread pool. See adopt().
     *
     * The job must first be obtained from runAndRetain() or retain().
     * The job can't be used after this call.
     */
    /**
     * 等待任务完成并释放
     * 
     * 等待任务完成，然后释放引用。当前线程必须属于 JobSystem 的线程池（见 adopt()）。
     * 
     * 任务必须首先通过 runAndRetain() 或 retain() 获得。
     * 调用后任务不能再使用。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
     */
    void waitAndRelease(Job*& job) noexcept;

    /*
     * Runs and wait for a job. This is equivalent to calling
     *  runAndRetain(job);
     *  wait(job);
     *
     * The job can't be used after this call.
     */
    /**
     * 运行并等待任务完成
     * 
     * 等价于调用：
     *   runAndRetain(job);
     *   waitAndRelease(job);
     * 
     * 调用后任务不能再使用。
     * 
     * @param job 任务指针（调用后会被置为 nullptr）
     */
    void runAndWait(Job*& job) noexcept;
    /**
     * 运行并等待任务完成（右值版本）
     * 
     * 允许 runAndWait(createJob(...)) 这样的调用。
     */
    void runAndWait(Job*&& job) noexcept { // allows runAndWait(createJob(...));
        Job* p = job;
        runAndWait(p);
    }

    // for debugging
    friend io::ostream& operator << (io::ostream& out, JobSystem const& js);


    // utility functions...
    // 工具函数...

    // set the name of the current thread (on OSes that support it)
    /**
     * 设置当前线程名称
     * 
     * 在支持的操作系统上设置线程名称（用于调试和性能分析）。
     * 
     * @param threadName 线程名称
     */
    static void setThreadName(const char* threadName) noexcept;

    /**
     * 线程优先级枚举
     */
    enum class Priority {
        NORMAL,          // 普通优先级
        DISPLAY,         // 显示优先级（用于渲染相关任务）
        URGENT_DISPLAY,  // 紧急显示优先级（最高优先级）
        BACKGROUND       // 后台优先级（最低优先级）
    };

    /**
     * 设置当前线程优先级
     * 
     * @param priority 优先级
     */
    static void setThreadPriority(Priority priority) noexcept;
    
    /**
     * 设置当前线程的 CPU 亲和性
     * 
     * 将线程绑定到指定的 CPU 核心。
     * 
     * @param id CPU 核心 ID
     */
    static void setThreadAffinityById(size_t id) noexcept;

    /**
     * 获取并行分割次数
     * 
     * 返回 parallel_for 的最大分割深度。
     * 
     * @return 并行分割次数
     */
    size_t getParallelSplitCount() const noexcept {
        return mParallelSplitCount;
    }

    /**
     * 获取线程数
     * 
     * @return 工作线程数
     */
    size_t getThreadCount() const { return mThreadCount; }

    // returns the current ThreadId, which can be used with run(). This method can only be
    // called from a job's function.
    /**
     * 获取当前线程 ID
     * 
     * 返回执行任务的线程 ID，可用于 run() 方法。
     * 此方法只能从任务的函数中调用。
     * 
     * @param job 任务指针
     * @return 线程 ID
     */
    static ThreadId getThreadId(Job const* job) noexcept {
        assert(job->id != invalidThreadId);
        return job->id;
    }

private:
    // this is just to avoid using std::default_random_engine, since we're in a public header.
    /**
     * 默认随机数生成器
     * 
     * 为了避免在公共头文件中使用 std::default_random_engine。
     * 使用线性同余生成器（LCG）算法。
     */
    class default_random_engine {
        static constexpr uint32_t m = 0x7fffffffu;
        // 模数：2^31 - 1（梅森素数）
        uint32_t mState; // must be 0 < seed < 0x7fffffff
        // 状态：必须满足 0 < seed < 0x7fffffff

    public:
        using result_type = uint32_t;

        static constexpr result_type min() noexcept {
            return 1;
        }

        static constexpr result_type max() noexcept {
            return m - 1;
        }

        /**
         * 构造函数
         * 
         * @param seed 种子值（默认 1）
         */
        constexpr explicit default_random_engine(uint32_t const seed = 1u) noexcept
                : mState(((seed % m) == 0u) ? 1u : seed % m) {
        }

        /**
         * 生成下一个随机数
         * 
         * 使用线性同余生成器：state = (state * 48271) % m
         * 
         * @return 随机数
         */
        uint32_t operator()() noexcept {
            return mState = uint32_t((uint64_t(mState) * 48271u) % m);
        }
    };

    /**
     * 线程状态
     * 
     * 对齐到缓存行，避免伪共享。
     * 
     * 内存布局：
     * - workQueue: 工作队列（无锁双端队列）
     * - js: JobSystem 指针（实际上 const，始终初始化）
     * - thread: 线程对象（对于 adopted 线程未使用）
     * - rndGen: 随机数生成器（用于工作窃取时的随机选择）
     */
    struct alignas(CACHELINE_SIZE) ThreadState {    // this causes 40-bytes padding
        // 对齐到缓存行，这会导致 40 字节的填充
        // make sure storage is cache-line aligned
        // 确保存储是缓存行对齐的
        WorkQueue workQueue;
        // 工作队列：此线程的任务队列

        // these are not accessed by the worker threads
        // 这些不被工作线程访问
        alignas(CACHELINE_SIZE)         // this causes 56-bytes padding
        // 对齐到缓存行，这会导致 56 字节的填充
        JobSystem* js;                  // this is in fact const and always initialized
        // JobSystem 指针：实际上是 const，始终初始化
        std::thread thread;             // unused for adopted threads
        // 线程对象：对于 adopted 线程未使用
        default_random_engine rndGen;
        // 随机数生成器：用于工作窃取时的随机选择
    };

    static_assert(sizeof(ThreadState) % CACHELINE_SIZE == 0,
            "ThreadState doesn't align to a cache line");

    /**
     * 获取当前线程的状态
     * 
     * @return 当前线程的 ThreadState 引用
     */
    ThreadState& getState();

    /**
     * 增加任务的引用计数
     * 
     * @param job 任务指针
     */
    static void incRef(Job const* job) noexcept;
    
    /**
     * 减少任务的引用计数
     * 
     * 如果引用计数归零，销毁任务。
     * 
     * @param job 任务指针
     */
    void decRef(Job const* job) noexcept;

    /**
     * 分配任务对象
     * 
     * @return 分配的任务指针，如果分配失败返回 nullptr
     */
    Job* allocateJob() noexcept;
    
    /**
     * 获取要窃取的目标线程状态
     * 
     * 随机选择一个线程作为工作窃取的目标。
     * 
     * @param state 当前线程状态
     * @return 目标线程状态指针，如果只有一个线程返回 nullptr
     */
    ThreadState* getStateToStealFrom(ThreadState& state) noexcept;
    
    /**
     * 检查任务是否已完成
     * 
     * @param job 任务指针
     * @return 如果任务已完成返回 true，否则返回 false
     */
    static bool hasJobCompleted(Job const* job) noexcept;

    /**
     * 请求所有线程退出
     */
    void requestExit() noexcept;
    
    /**
     * 检查是否已请求退出
     * 
     * @return 如果已请求退出返回 true，否则返回 false
     */
    bool exitRequested() const noexcept;
    
    /**
     * 检查是否有活跃任务
     * 
     * @return 如果有活跃任务返回 true，否则返回 false
     */
    bool hasActiveJobs() const noexcept;

    /**
     * 工作线程主循环
     * 
     * @param state 线程状态指针
     */
    void loop(ThreadState* state);
    
    /**
     * 执行一个任务
     * 
     * @param state 线程状态
     * @return 如果执行了任务返回 true，否则返回 false
     */
    bool execute(ThreadState& state) noexcept;
    
    /**
     * 从其他线程窃取任务
     * 
     * @param state 当前线程状态
     * @return 窃取的任务指针，如果没有可窃取的任务返回 nullptr
     */
    Job* steal(ThreadState& state) noexcept;
    
    /**
     * 完成任务
     * 
     * 减少运行计数，如果任务完成则通知父任务并唤醒等待的线程。
     * 
     * @param job 任务指针
     */
    void finish(Job* job) noexcept;

    /**
     * 将任务放入队列
     * 
     * @param workQueue 工作队列
     * @param job 任务指针
     */
    void put(WorkQueue& workQueue, Job const* job) noexcept;
    
    /**
     * 从队列中弹出任务
     * 
     * @param workQueue 工作队列
     * @return 任务指针，如果队列为空返回 nullptr
     */
    Job* pop(WorkQueue& workQueue) noexcept;
    
    /**
     * 从队列中窃取任务
     * 
     * @param workQueue 工作队列
     * @return 任务指针，如果队列为空返回 nullptr
     */
    Job* steal(WorkQueue& workQueue) noexcept;

    /**
     * 等待任务完成
     * 
     * @param lock 互斥锁（已锁定）
     * @param job 任务指针
     * @return runningJobCount 的值
     */
    [[nodiscard]]
    uint32_t wait(std::unique_lock<Mutex>& lock, Job* job) noexcept;
    
    /**
     * 等待条件变量
     * 
     * @param lock 互斥锁（已锁定）
     */
    void wait(std::unique_lock<Mutex>& lock) noexcept;
    
    /**
     * 唤醒所有等待的线程
     */
    void wakeAll() noexcept;
    
    /**
     * 唤醒一个等待的线程
     */
    void wakeOne() noexcept;

    // these have thread contention, keep them together
    // 这些有线程竞争，保持它们在一起
    Mutex mWaiterLock;
    // 等待者锁：保护等待条件变量
    Condition mWaiterCondition;
    // 等待条件变量：用于线程等待和唤醒

    std::atomic<int32_t> mActiveJobs = { 0 };
    // 活跃任务计数（原子）：当前正在执行或等待执行的任务数
    Arena<ObjectPoolAllocator<Job>, LockingPolicy::Mutex> mJobPool;
    // Job 对象池：用于分配和回收 Job 对象

    template <typename T>
    using aligned_vector = std::vector<T, STLAlignedAllocator<T>>;
    // 对齐向量：使用对齐分配器的向量

    // These are essentially const, make sure they're on a different cache-lines than the
    // read-write atomics.
    // We can't use "alignas(CACHELINE_SIZE)" because the standard allocator can't make this
    // guarantee.
    // 这些本质上是 const，确保它们与读写原子变量在不同的缓存行上。
    // 我们不能使用 "alignas(CACHELINE_SIZE)"，因为标准分配器无法保证这一点。
    char padding[CACHELINE_SIZE];
    // 填充：确保后续数据在不同的缓存行上

    alignas(16) // at least we align to half (or quarter) cache-line
    // 至少对齐到半（或四分之一）缓存行
    aligned_vector<ThreadState> mThreadStates;          // actual data is stored offline
    // 线程状态向量：实际数据离线存储
    std::atomic<bool> mExitRequested = { false };       // this one is almost never written
    // 退出请求标志（原子）：几乎从不写入
    std::atomic<uint16_t> mAdoptedThreads = { 0 };      // this one is almost never written
    // 已 adopt 的线程数（原子）：几乎从不写入
    Job* const mJobStorageBase;                         // Base for conversion to indices
    // Job 存储基址：用于将指针转换为索引
    uint16_t mThreadCount = 0;                          // total # of threads in the pool
    // 线程池中的线程总数
    uint8_t mParallelSplitCount = 0;                    // # of split allowable in parallel_for
    // parallel_for 允许的分割次数
    Job* mRootJob = nullptr;
    // 根任务：如果创建任务时未指定父任务，使用根任务作为父任务

    Mutex mThreadMapLock; // this should have very little contention
    // 线程映射表锁：保护线程映射表的访问（应该有很少的竞争）
    tsl::robin_map<std::thread::id, ThreadState *> mThreadMap;
    // 线程映射表：从线程 ID 到 ThreadState 指针的映射（用于 adopt() 机制）
};

// -------------------------------------------------------------------------------------------------
// Utility functions built on top of JobSystem
// 基于 JobSystem 构建的工具函数

namespace jobs {

// These are convenience C++11 style job creation methods that support lambdas
//
// IMPORTANT: these are less efficient to call and may perform heap allocation
//            depending on the capture and parameters
//
/**
 * 工具函数命名空间
 * 
 * 这些是便捷的 C++11 风格的任务创建方法，支持 lambda。
 * 
 * 重要提示：这些函数调用效率较低，可能执行堆分配（取决于捕获和参数）。
 */

/**
 * 创建任务（通用可调用对象版本）
 * 
 * 支持任意可调用对象（函数、lambda、函数对象等）和参数。
 * 内部使用 std::function 和 std::bind，可能进行堆分配。
 * 
 * @tparam CALLABLE 可调用对象类型
 * @tparam ARGS 参数类型
 * @param js JobSystem 引用
 * @param parent 父任务
 * @param func 可调用对象（转发）
 * @param args 参数（转发）
 * @return 创建的任务指针
 */
template<typename CALLABLE, typename ... ARGS>
JobSystem::Job* createJob(JobSystem& js, JobSystem::Job* parent,
        CALLABLE&& func, ARGS&&... args) noexcept {
    struct Data {
        explicit Data(std::function<void()> f) noexcept: f(std::move(f)) {}
        std::function<void()> f;
        // Renaming the method below could cause an Arrested Development.
        // 重命名下面的方法可能会导致 Arrested Development（这是一个内部笑话）
        void gob(JobSystem&, JobSystem::Job*) noexcept { f(); }
    };
    return js.emplaceJob<Data, &Data::gob>(parent,
            std::bind(std::forward<CALLABLE>(func), std::forward<ARGS>(args)...));
}

/**
 * 创建任务（成员函数指针版本）
 * 
 * 支持成员函数指针和对象，以及额外参数。
 * 内部使用 std::function 和 std::bind，可能进行堆分配。
 * 
 * @tparam CALLABLE 成员函数指针类型
 * @tparam T 对象类型
 * @tparam ARGS 额外参数类型
 * @param js JobSystem 引用
 * @param parent 父任务
 * @param func 成员函数指针（转发）
 * @param o 对象（转发）
 * @param args 额外参数（转发）
 * @return 创建的任务指针
 */
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
        // 重命名下面的方法可能会导致 Arrested Development（这是一个内部笑话）
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
/**
 * 并行循环（起始索引/数量版本）
 * 
 * 使用起始索引和数量来并行执行任务。
 * 
 * @tparam S 分割器类型
 * @tparam F 函数对象类型
 * @param js JobSystem 引用
 * @param parent 父任务
 * @param start 起始索引
 * @param count 任务数量
 * @param functor 函数对象（接收 start 和 count 作为参数）
 * @param splitter 分割器（决定是否继续分割）
 * @return 创建的任务指针
 * 
 * 示例：
 * ```cpp
 * jobs::parallel_for(js, parent, 0, 1000, 
 *     [](uint32_t start, uint32_t count) {
 *         for (uint32_t i = start; i < start + count; ++i) {
 *             // 处理索引 i
 *         }
 *     }, splitter);
 * ```
 */
template<typename S, typename F>
JobSystem::Job* parallel_for(JobSystem& js, JobSystem::Job* parent,
        uint32_t start, uint32_t count, F functor, const S& splitter) noexcept {
    using JobData = details::ParallelForJobData<S, F>;
    return js.emplaceJob<JobData, &JobData::parallelWithJobs>(parent,
            start, count, 0, std::move(functor), splitter);
}

// parallel jobs with pointer/count
/**
 * 并行循环（指针/数量版本）
 * 
 * 使用指针和数量来并行执行任务。
 * 函数对象接收指针和数量作为参数。
 * 
 * @tparam T 元素类型
 * @tparam S 分割器类型
 * @tparam F 函数对象类型（接收 T* 和 uint32_t 作为参数）
 * @param js JobSystem 引用
 * @param parent 父任务
 * @param data 数据指针
 * @param count 元素数量
 * @param functor 函数对象（接收 T* 和 uint32_t 作为参数）
 * @param splitter 分割器（决定是否继续分割）
 * @return 创建的任务指针
 * 
 * 示例：
 * ```cpp
 * int array[1000];
 * jobs::parallel_for(js, parent, array, 1000,
 *     [](int* data, uint32_t count) {
 *         for (uint32_t i = 0; i < count; ++i) {
 *             data[i] = i * 2;
 *         }
 *     }, splitter);
 * ```
 */
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
/**
 * 并行循环（Slice 版本）
 * 
 * 使用 Slice 来并行执行任务。
 * 这是指针/数量版本的便捷包装。
 * 
 * @tparam T 元素类型
 * @tparam S 分割器类型
 * @tparam F 函数对象类型（接收 T* 和 uint32_t 作为参数）
 * @param js JobSystem 引用
 * @param parent 父任务
 * @param slice 数据切片
 * @param functor 函数对象（接收 T* 和 uint32_t 作为参数）
 * @param splitter 分割器（决定是否继续分割）
 * @return 创建的任务指针
 * 
 * 示例：
 * ```cpp
 * Slice<int> slice = ...;
 * jobs::parallel_for(js, parent, slice,
 *     [](int* data, uint32_t count) {
 *         // 处理数据
 *     }, splitter);
 * ```
 */
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
