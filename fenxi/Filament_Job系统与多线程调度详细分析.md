# Filament Job 系统与多线程调度详细分析

## 概述

Filament 使用基于工作窃取（Work-Stealing）的 Job 系统来实现高效的多线程并行处理。该系统设计用于在渲染管线中并行执行各种任务，如视锥剔除、光照剔除、命令生成等。

## 架构设计

### 1. 核心组件

```
┌─────────────────────────────────────────────────────────┐
│  JobSystem（主任务系统）                                  │
│  - 工作窃取队列（Work-Stealing Dequeue）                 │
│  - 线程池管理                                             │
│  - Job 生命周期管理                                       │
└──────────────┬──────────────────────────────────────────┘
               │
       ┌───────┴───────┬───────────┬──────────┐
       ▼               ▼           ▼          ▼
┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
│JobQueue  │  │AsyncJob  │  │parallel_│  │WorkSteal │
│（后端）   │  │Queue     │  │for      │  │Dequeue   │
└──────────┘  └──────────┘  └──────────┘  └──────────┘
```

### 2. 线程模型

```
┌─────────────────────────────────────────────────────────┐
│  主线程（用户线程）                                       │
│  - 创建 Job                                              │
│  - 提交 Job 到队列                                       │
│  - 等待 Job 完成                                         │
└──────────────┬──────────────────────────────────────────┘
               │ adopt() / run()
               ▼
┌─────────────────────────────────────────────────────────┐
│  工作线程池（Worker Threads）                            │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐        │
│  │Thread 0│  │Thread 1│  │Thread 2│  │Thread N│        │
│  │WorkQueue│ │WorkQueue│ │WorkQueue│ │WorkQueue│        │
│  └────┬───┘  └────┬───┘  └────┬───┘  └────┬───┘        │
│       │           │           │           │              │
│       └───────────┴───────────┴───────────┘              │
│                    │                                     │
│                    ▼                                     │
│            Work-Stealing（工作窃取）                      │
└─────────────────────────────────────────────────────────┘
```

## JobSystem 详解

### 1. JobSystem 初始化

**位置**：`libs/utils/src/JobSystem.cpp:177`

**线程数计算**：
```cpp
// 默认线程数 = CPU 核心数 - 1（主线程占用一个核心）
unsigned int hwThreads = std::thread::hardware_concurrency();
if (UTILS_HAS_HYPER_THREADING) {
    // 避免使用超线程，简化性能分析
    hwThreads = (hwThreads + 1) / 2;
}
threadPoolCount = hwThreads - 1;
threadPoolCount = std::max(1u, threadPoolCount);  // 至少 1 个线程
threadPoolCount = std::min(32u, threadPoolCount); // 最多 32 个线程
```

**关键参数**：
- `MAX_JOB_COUNT = 16384`：最大 Job 数量
- `CACHELINE_SIZE`：缓存行大小（64 字节），用于避免伪共享
- `mParallelSplitCount`：并行分割次数（用于 `parallel_for`）

### 2. Job 结构

**位置**：`libs/utils/include/utils/JobSystem.h:64`

```cpp
class alignas(CACHELINE_SIZE) Job {
    void* storage[JOB_STORAGE_SIZE_WORDS];  // Job 数据存储（48 字节）
    JobFunc function;                       // Job 执行函数指针
    uint16_t parent;                        // 父 Job 索引
    ThreadId id;                            // 执行线程 ID
    std::atomic<uint8_t> refCount;          // 引用计数
    std::atomic<uint32_t> runningJobCount; // 运行中的子 Job 计数
};
```

**设计要点**：
- **缓存行对齐**：避免伪共享（False Sharing）
- **引用计数**：支持多线程等待同一个 Job
- **父子关系**：支持 Job 依赖和同步
- **运行计数**：跟踪子 Job 完成情况

### 3. 工作窃取队列（Work-Stealing Dequeue）

**位置**：`libs/utils/include/utils/WorkStealingDequeue.h`

**数据结构**：
```
     top (steal)                    bottom (push/pop)
      v                               v
      |----|----|----|----|----|----|
      [0]  [1]  [2]  [3]  [4]  [5]
      
- push(): 从 bottom 添加（主线程）
- pop(): 从 bottom 移除（主线程）
- steal(): 从 top 窃取（工作线程）
```

**关键特性**：
- **无锁设计**：使用原子操作，避免互斥锁开销
- **固定大小**：必须是 2 的幂（使用位掩码优化）
- **双端操作**：主线程从底部操作，工作线程从顶部窃取
- **内存顺序**：使用 `memory_order_seq_cst` 保证顺序一致性

### 4. Job 生命周期

#### 4.1 创建 Job

```cpp
// 方式 1：使用函数指针
Job* job = js.createJob(parent, [](JobSystem& js, Job* job) {
    // Job 逻辑
});

// 方式 2：使用成员函数
Job* job = js.createJob<MyClass, &MyClass::method>(parent, &object);

// 方式 3：使用函数对象
struct Functor {
    void operator()(JobSystem& js, Job* job) { }
};
Job* job = js.createJob(parent, Functor{});
```

#### 4.2 运行 Job

```cpp
// 异步运行（立即返回）
js.run(job);

// 运行并保留引用（用于等待）
Job* retained = js.runAndRetain(job);

// 运行并等待完成
js.runAndWait(job);
```

#### 4.3 等待 Job

```cpp
// 等待并释放引用
js.waitAndRelease(job);

// 等待（不释放引用）
while (!hasJobCompleted(job)) {
    js.execute(state);  // 执行其他 Job
}
```

### 5. 工作线程循环

**位置**：`libs/utils/src/JobSystem.cpp:445`

```cpp
void JobSystem::loop(ThreadState* state) {
    setThreadName("JobSystem::loop");
    setThreadPriority(Priority::DISPLAY);
    
    // 注册线程到线程映射表
    mThreadMap[std::this_thread::get_id()] = state;
    
    // 主循环
    do {
        if (!execute(*state)) {
            // 队列为空，等待新 Job
            std::unique_lock lock(mWaiterLock);
            while (!exitRequested() && !hasActiveJobs()) {
                wait(lock);
            }
        }
    } while (!exitRequested());
}
```

**执行流程**：
1. 从自己的队列 `pop()` 获取 Job
2. 如果队列为空，尝试 `steal()` 其他线程的 Job
3. 执行 Job
4. 如果仍然没有 Job，进入等待状态

### 6. 工作窃取算法

**位置**：`libs/utils/src/JobSystem.cpp:404`

```cpp
Job* JobSystem::steal(ThreadState& state) noexcept {
    Job* job = nullptr;
    do {
        // 随机选择一个线程
        ThreadState* target = getStateToStealFrom(state);
        if (target) {
            // 从目标线程的队列顶部窃取
            job = steal(target->workQueue);
        }
    } while (!job && hasActiveJobs());
    return job;
}
```

**窃取策略**：
- **随机选择**：使用随机数生成器选择目标线程
- **避免自窃取**：不窃取自己的队列
- **重试机制**：如果没有可窃取的 Job，继续尝试

## parallel_for 详解

### 1. 并行循环

**位置**：`libs/utils/include/utils/JobSystem.h:576`

`parallel_for` 是 JobSystem 提供的并行循环工具，用于将大任务分割成多个小任务并行执行。

**使用示例**：
```cpp
// 并行处理数组
auto* job = jobs::parallel_for(js, parent,
    0, count,  // 起始索引和数量
    [](uint32_t start, uint32_t count) {
        // 处理 [start, start + count) 范围的数据
    },
    jobs::CountSplitter<64>()  // 分割器：当 count >= 128 时分割
);
js.runAndWait(job);
```

### 2. 分割策略

**CountSplitter**：
```cpp
template<size_t COUNT, size_t MAX_SPLITS = 12>
class CountSplitter {
    bool split(size_t splits, size_t count) const noexcept {
        return (splits < MAX_SPLITS && count >= COUNT * 2);
    }
};
```

**分割逻辑**：
- 当 `count >= COUNT * 2` 且 `splits < MAX_SPLITS` 时分割
- 分割为两个子任务：`[start, start + count/2)` 和 `[start + count/2, start + count)`
- 递归分割直到满足停止条件

### 3. 并行执行流程

```
parallel_for(start=0, count=1000)
    │
    ├─> split: [0, 500) 和 [500, 1000)
    │
    ├─> Job 1: [0, 500)
    │   ├─> split: [0, 250) 和 [250, 500)
    │   │   ├─> Job 1.1: [0, 250) → execute
    │   │   └─> Job 1.2: [250, 500) → execute
    │
    └─> Job 2: [500, 1000)
        ├─> split: [500, 750) 和 [750, 1000)
        │   ├─> Job 2.1: [500, 750) → execute
        │   └─> Job 2.2: [750, 1000) → execute
```

## JobQueue（后端任务队列）

### 1. 设计目的

`JobQueue` 是 Filament 后端（Backend）使用的任务队列，用于处理异步操作（如回调、资源清理等）。

**位置**：`filament/backend/src/JobQueue.h`

**特点**：
- **线程安全**：支持多生产者多消费者
- **批处理**：支持批量获取任务
- **任务取消**：支持取消未执行的任务
- **有序执行**：保证任务按提交顺序执行

### 2. Worker 类型

#### 2.1 AmortizationWorker（摊销工作器）

**特点**：
- **非阻塞**：在主线程中定期调用 `process()`
- **批处理**：一次处理多个任务
- **低延迟**：适合需要快速响应的场景

**使用场景**：
- 每帧处理少量异步任务
- 资源清理
- 回调执行

**示例**：
```cpp
auto worker = AmortizationWorker::create(queue);
// 每帧调用
worker->process(2);  // 处理最多 2 个任务
```

#### 2.2 ThreadWorker（线程工作器）

**特点**：
- **专用线程**：在独立线程中运行
- **阻塞等待**：队列为空时阻塞
- **顺序执行**：保证任务按顺序执行

**使用场景**：
- 长时间运行的任务
- 需要等待的操作（如栅栏等待）
- 后台处理

**示例**：
```cpp
ThreadWorker::Config config{
    .name = "BackendWorker",
    .priority = Priority::DISPLAY
};
auto worker = ThreadWorker::create(queue, config);
```

### 3. AsyncJobQueue（异步任务队列）

**位置**：`libs/utils/include/utils/AsyncJobQueue.h`

**特点**：
- **单线程**：只有一个工作线程
- **顺序执行**：任务按提交顺序执行
- **批量处理**：一次处理所有待处理任务

**使用场景**：
- Driver 回调执行
- 资源释放
- 平台特定操作

## 在渲染管线中的应用

### 1. 视锥剔除

**位置**：`filament/src/details/View.cpp`

```cpp
// 并行执行视锥剔除
auto* cullingJob = jobs::parallel_for(js, parent,
    visibleRenderables.first, visibleRenderables.size(),
    [&](uint32_t start, uint32_t count) {
        // 对 [start, start + count) 范围的物体进行视锥剔除
        cull(soa, {start, start + count}, camera);
    },
    jobs::CountSplitter<CULLING_JOB_SPLIT_COUNT>()
);
js.runAndWait(cullingJob);
```

### 2. 命令生成

**位置**：`filament/src/RenderPass.cpp:207`

```cpp
// 并行生成渲染命令
if (visibleRenderables.size() > JOBS_PARALLEL_FOR_COMMANDS_COUNT) {
    auto* jobCommandsParallel = parallel_for(js, nullptr,
        visibleRenderables.first, visibleRenderables.size(),
        work,  // 命令生成函数
        jobs::CountSplitter<JOBS_PARALLEL_FOR_COMMANDS_COUNT>()
    );
    js.runAndWait(jobCommandsParallel);
}
```

### 3. 光照网格化（Froxelization）

**位置**：`filament/src/details/View.cpp`

```cpp
// 并行处理光照网格化
auto* froxelizeJob = jobs::parallel_for(js, parent,
    0, lightCount,
    [&](uint32_t start, uint32_t count) {
        // 处理 [start, start + count) 范围的光照
        froxelize(lights, {start, start + count});
    },
    jobs::CountSplitter<FROXELIZE_JOB_SPLIT_COUNT>()
);
js.runAndWait(froxelizeJob);
```

### 4. 根 Job 管理

**位置**：`filament/src/details/Renderer.cpp:621`

```cpp
void FRenderer::renderInternal(FView const* view, bool flush) {
    // 创建根 Job，防止子任务泄漏
    JobSystem& js = engine.getJobSystem();
    auto* rootJob = js.setRootJob(js.createJob());
    
    // 执行渲染（可能创建多个子 Job）
    renderJob(rootArenaScope, view);
    
    // 等待所有 Job 完成
    js.runAndWait(rootJob);
}
```

**根 Job 的作用**：
- **防止泄漏**：确保所有子 Job 都完成
- **同步点**：作为所有渲染任务的同步点
- **资源管理**：确保资源在使用完毕后释放

## 性能优化

### 1. 缓存行对齐

**目的**：避免伪共享（False Sharing）

```cpp
class alignas(CACHELINE_SIZE) Job {
    // Job 数据
};

struct alignas(CACHELINE_SIZE) ThreadState {
    WorkQueue workQueue;  // 每个线程的工作队列
    // ...
};
```

**效果**：
- 减少缓存一致性协议的开销
- 提高多线程性能

### 2. 无锁数据结构

**WorkStealingDequeue**：
- 使用原子操作代替互斥锁
- 减少线程阻塞
- 提高并发性能

### 3. 工作窃取

**优势**：
- **负载均衡**：自动平衡线程负载
- **减少等待**：空闲线程主动窃取工作
- **提高利用率**：充分利用所有 CPU 核心

### 4. 批处理

**JobQueue::popBatch()**：
- 减少锁竞争
- 提高缓存局部性
- 减少函数调用开销

### 5. 内存顺序优化

**策略**：
- 使用 `memory_order_relaxed` 减少同步开销
- 只在必要时使用 `memory_order_seq_cst`
- 避免过度同步

## 线程安全

### 1. 原子操作

**引用计数**：
```cpp
// 增加引用计数（relaxed，因为不涉及同步）
job->refCount.fetch_add(1, std::memory_order_relaxed);

// 减少引用计数（acq_rel，需要同步）
job->refCount.fetch_sub(1, std::memory_order_acq_rel);
```

### 2. 条件变量

**等待机制**：
```cpp
std::unique_lock lock(mWaiterLock);
while (!exitRequested() && !hasActiveJobs()) {
    wait(lock);  // 等待条件变量通知
}
```

### 3. 线程注册

**线程映射**：
```cpp
// 注册线程到 JobSystem
mThreadMap[std::this_thread::get_id()] = state;

// 获取当前线程的状态
ThreadState& state = getState();
```

## 调试和性能分析

### 1. 线程命名

```cpp
JobSystem::setThreadName("JobSystem::loop");
```

**用途**：
- 在调试器中识别线程
- 性能分析工具中的线程标识

### 2. 线程优先级

```cpp
JobSystem::setThreadPriority(Priority::DISPLAY);
```

**优先级级别**：
- `NORMAL`：普通优先级
- `DISPLAY`：显示优先级（渲染线程）
- `URGENT_DISPLAY`：紧急显示优先级
- `BACKGROUND`：后台优先级

### 3. 性能追踪

```cpp
FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_JOBSYSTEM);
FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_JOBSYSTEM, "job->function");
```

**用途**：
- Systrace 性能分析
- Perfetto 可视化
- 性能瓶颈识别

## 最佳实践

### 1. Job 大小

**建议**：
- **不要太小**：避免 Job 创建和执行的开销超过实际工作
- **不要太大**：避免单个 Job 阻塞其他任务
- **平衡**：根据任务特性选择合适的粒度

### 2. 并行度

**建议**：
- 使用 `parallel_for` 处理大数据集
- 设置合理的分割阈值
- 避免过度并行化

### 3. 同步点

**建议**：
- 使用根 Job 管理任务生命周期
- 避免不必要的等待
- 在等待时执行其他任务

### 4. 资源管理

**建议**：
- 确保 Job 中访问的资源在 Job 执行期间有效
- 使用引用计数管理共享资源
- 避免在 Job 中持有锁

## 总结

### Filament Job 系统的特点

1. **高效**：基于工作窃取的无锁设计
2. **灵活**：支持多种任务类型和执行模式
3. **可扩展**：自动适应不同硬件配置
4. **易用**：提供简洁的 API 和工具函数

### 关键设计决策

1. **工作窃取**：实现负载均衡和高效并行
2. **无锁队列**：减少同步开销
3. **缓存行对齐**：避免伪共享
4. **父子 Job**：支持任务依赖和同步

### 性能优势

- **高并发**：充分利用多核 CPU
- **低延迟**：减少线程阻塞和上下文切换
- **可扩展**：自动适应硬件配置
- **高效**：最小化同步开销

通过这套 Job 系统，Filament 能够在渲染管线中高效地并行执行各种任务，显著提高渲染性能。

