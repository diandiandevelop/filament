/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "JobQueue.h"

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>

namespace filament::backend {

JobQueue::JobQueue(PassKey) {}

/**
 * 添加任务到队列
 * 
 * 这是线程安全的生产者操作，支持：
 * 1. 普通添加：自动生成 JobId
 * 2. 预分配 JobId：使用预先分配的 JobId（用于取消机制）
 * 
 * 线程安全：
 * - 使用互斥锁保护队列操作
 * - 使用条件变量通知等待的消费者
 * 
 * @param job 要添加的任务（函数对象）
 * @param preIssuedJobId 预先分配的 JobId（可选，InvalidJobId 表示自动生成）
 * @return JobId，如果队列正在停止返回 InvalidJobId
 */
JobQueue::JobId JobQueue::push(Job job, JobId const preIssuedJobId/* = InvalidJobId*/) {
    JobId jobId = preIssuedJobId;
    {
        /**
         * 临界区：保护队列操作
         */
        std::lock_guard<std::mutex> lock(mQueueMutex);
        
        /**
         * 检查队列是否正在停止
         * 
         * 如果队列正在停止，拒绝添加新任务
         */
        if (mIsStopping) {
            return InvalidJobId;
        }

        if (jobId == InvalidJobId) {
            /**
             * 情况 1：自动生成 JobId
             * 
             * 生成新的 JobId 并将任务添加到映射表
             */
            jobId = genNextJobId();
            mJobsMap[jobId] = std::move(job);
        } else {
            /**
             * 情况 2：使用预先分配的 JobId
             * 
             * 这用于支持任务取消机制：
             * 1. 调用 issueJobId() 预先分配 JobId
             * 2. 检查 JobId 是否有效（可以取消）
             * 3. 调用 push() 使用预分配的 JobId
             */
            // 使用 issueJobId() 预先分配的 JobId
            auto it = mJobsMap.find(jobId);
            if (it == mJobsMap.end()) {
                /**
                 * 预分配的 Job 不存在，可能原因：
                 * - 用户传递了错误的 ID（不太可能）
                 * - Job 已被取消（很可能）
                 */
                return InvalidJobId;
            }
            /**
             * 确保预分配的 Job 尚未填充
             * 
             * 如果已经填充，说明有逻辑错误
             */
            FILAMENT_CHECK_PRECONDITION(!static_cast<bool>(it->second))
                    << "pre-issued job has already been populated";
            it->second = std::move(job);
        }
        
        /**
         * 将 JobId 添加到执行顺序队列
         * 
         * mJobOrder 保证任务按提交顺序执行
         */
        mJobOrder.push(jobId);
    }

    /**
     * 通知等待的消费者
     * 
     * ThreadWorker 可能在等待新任务，需要唤醒
     * 使用 notify_one() 而不是 notify_all()，因为只需要唤醒一个线程
     */
    mQueueCondition.notify_one();

    return jobId;
}

/**
 * 从队列中获取任务
 * 
 * 这是线程安全的消费者操作，支持：
 * 1. 阻塞模式（ThreadWorker）：队列为空时等待
 * 2. 非阻塞模式（AmortizationWorker）：队列为空时立即返回
 * 
 * 线程安全：
 * - 使用互斥锁保护队列操作
 * - 使用条件变量实现阻塞等待
 * 
 * 任务取消处理：
 * - 如果任务在添加到队列后被取消，跳过该任务
 * - 继续尝试获取下一个任务
 * 
 * @param shouldBlock 是否阻塞等待（true = ThreadWorker，false = AmortizationWorker）
 * @return 任务，如果队列为空返回空任务
 */
JobQueue::Job JobQueue::pop(bool shouldBlock) {
    std::unique_lock<std::mutex> lock(mQueueMutex);

    decltype(mJobsMap)::iterator it;

    /**
     * 循环直到获取到有效任务或队列为空
     * 
     * 循环的原因：任务可能在添加到队列后被取消
     */
    while (true) {
        if (shouldBlock) {
            /**
             * 阻塞模式：等待直到有任务或队列停止
             * 
             * 条件变量等待条件：
             * - 队列不为空（!mJobOrder.empty()）
             * - 或队列正在停止（mIsStopping）
             */
            mQueueCondition.wait(lock, [this] { return !mJobOrder.empty() || mIsStopping; });
        }

        /**
         * 检查队列是否为空
         */
        if (mJobOrder.empty()) {
            /**
             * 队列为空：
             * - 如果 shouldBlock == true：队列正在停止
             * - 如果 shouldBlock == false：没有任务
             */
            return nullptr;
        }

        /**
         * 从顺序队列中获取下一个 JobId
         */
        JobId jobId = mJobOrder.front();
        mJobOrder.pop();

        /**
         * 在映射表中查找任务
         */
        it = mJobsMap.find(jobId);
        if (it != mJobsMap.end()) {
            /**
             * 找到任务，退出循环
             */
            break;
        }

        /**
         * 任务不存在（已被取消）
         * 
         * 如果执行到这里，任务必须在添加后被取消了
         * 因此，我们应该继续循环并尝试获取下一个可用任务
         */
    }

    /**
     * 移动任务并移除映射
     * 
     * 使用 move 避免不必要的拷贝
     */
    Job job = std::move(it->second);
    mJobsMap.erase(it);
    return job;
}

/**
 * 批量获取任务
 * 
 * 这是非阻塞的批量操作，用于 AmortizationWorker。
 * 一次获取多个任务可以减少锁竞争和提高缓存局部性。
 * 
 * 线程安全：
 * - 使用互斥锁保护队列操作
 * - 非阻塞：如果队列为空，立即返回空向量
 * 
 * @param maxJobsToPop 最大任务数
 *                    - 0：不获取任何任务
 *                    - > 0：获取最多 maxJobsToPop 个任务
 *                    - < 0：获取所有待处理任务
 * @return 任务向量，如果队列为空返回空向量
 */
utils::FixedCapacityVector<JobQueue::Job> JobQueue::popBatch(int const maxJobsToPop) {
    utils::FixedCapacityVector<Job> jobs;

    /**
     * 快速路径：如果 maxJobsToPop == 0，直接返回
     */
    if (UTILS_UNLIKELY(maxJobsToPop == 0)) {
        return jobs;
    }

    /**
     * 临界区：保护队列操作
     */
    std::lock_guard<std::mutex> lock(mQueueMutex);
    
    /**
     * 快速路径：如果队列为空，直接返回
     */
    if (mJobOrder.empty()) {
        return jobs;
    }

    /**
     * 计算要获取的任务数
     * 
     * 如果 maxJobsToPop < 0，获取所有任务
     * 否则，获取 min(maxJobsToPop, 队列大小) 个任务
     */
    size_t jobsToTake = mJobOrder.size();
    if (0 < maxJobsToPop && maxJobsToPop < static_cast<int>(jobsToTake)) {
        jobsToTake = maxJobsToPop;
    }
    
    /**
     * 预分配向量容量，避免多次重新分配
     */
    jobs.reserve(jobsToTake);

    /**
     * 批量获取任务
     * 
     * 循环直到：
     * - 获取了足够多的任务（jobsToTake == 0）
     * - 或队列为空（mJobOrder.empty()）
     */
    while (0 < jobsToTake && !mJobOrder.empty()) {
        /**
         * 从顺序队列中获取 JobId
         */
        JobId jobId = mJobOrder.front();
        mJobOrder.pop();

        /**
         * 在映射表中查找任务
         */
        auto it = mJobsMap.find(jobId);
        if (UTILS_UNLIKELY(it == mJobsMap.end())) {
            /**
             * 任务不存在（已被取消）
             * 
             * 跳过该任务，继续下一个
             */
            continue;
        }

        /**
         * 移动任务到结果向量并移除映射
         */
        jobs.push_back(std::move(it->second));
        --jobsToTake;
        mJobsMap.erase(it);
    }

    return jobs;
}

JobQueue::JobId JobQueue::issueJobId() noexcept {
    std::lock_guard<std::mutex> lock(mQueueMutex);
    JobId const jobId = genNextJobId();
    // Preallocate a job, which serves two main purposes. It provides a valid jobId that can be
    // checked for integrity when passed to the `push` method, and it enables job cancellation for
    // tasks that are yet to be pushed.
    mJobsMap[jobId];
    return jobId;
}

bool JobQueue::cancel(JobId const jobId) noexcept {
    std::lock_guard<std::mutex> lock(mQueueMutex);

    auto it = mJobsMap.find(jobId);
    if (it == mJobsMap.end()) {
        return false; // Job not found, must have been completed or canceled.
    }

    mJobsMap.erase(it);

    return true;
}

void JobQueue::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mQueueMutex);
        mIsStopping = true;
    }
    mQueueCondition.notify_all(); // Wake up all waiting threads
}

JobQueue::JobId JobQueue::genNextJobId() noexcept {
    // We assume this method is called within the critical section.
    JobId newJobId = mNextJobId++;
    // We assume the job ID won't overflow or wraps around to zero within the application's lifetime.
    assert_invariant(newJobId != InvalidJobId);
    return newJobId;
}

JobWorker::~JobWorker() = default;

void JobWorker::terminate() {
    // This is called from workers `terminate()`, which may hinder the concurrent use of multiple
    // workers. Consider removing this line and require the owner/caller to explicitly invoke it to
    // enable multiple worker instances.
    if (mQueue) {
        mQueue->stop();
    }
}

AmortizationWorker::AmortizationWorker(JobQueue::Ptr queue, PassKey)
    : JobWorker(std::move(queue)) {
}

AmortizationWorker::~AmortizationWorker() = default;

void AmortizationWorker::process(int const jobCount) {
    if (!mQueue || jobCount == 0) {
        return;
    }

    if (jobCount == 1) {
        // Handle single job without vector allocation.
        if (auto job = mQueue->pop(false)) {
            job();
        }
        return;
    }

    // Handle batch (jobCount > 1 or jobCount < 0 for "all pending jobs")
    utils::FixedCapacityVector<JobQueue::Job> jobs = mQueue->popBatch(jobCount);
    if (jobs.empty()) {
        return;
    }

    for (auto& job: jobs) {
        job();
    }
}

void AmortizationWorker::terminate() {
    JobWorker::terminate();

    // Drain all pending jobs.
    process(-1);
}

ThreadWorker::ThreadWorker(JobQueue::Ptr queue, Config config, PassKey)
        : JobWorker(std::move(queue)), mConfig(std::move(config)) {
    mThread = std::thread([this]() {
        utils::JobSystem::setThreadName(mConfig.name.data());
        utils::JobSystem::setThreadPriority(mConfig.priority);

        if (mConfig.onBegin) {
            mConfig.onBegin();
        }

        while (JobQueue::Job job = mQueue->pop(true)) {
            job();
        }

        if (mConfig.onEnd) {
            mConfig.onEnd();
        }
    });
}

ThreadWorker::~ThreadWorker() = default;

void ThreadWorker::terminate() {
    JobWorker::terminate();

    if (mThread.joinable()) {
        mThread.join();
    }
}

} // namespace utils
