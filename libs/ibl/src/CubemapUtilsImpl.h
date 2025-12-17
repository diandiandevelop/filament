/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef IBL_CUBEMAPUTILSIMPL_H
#define IBL_CUBEMAPUTILSIMPL_H

#include <ibl/CubemapUtils.h>

#include <utils/compiler.h>
#include <utils/JobSystem.h>

namespace filament {
namespace ibl {

/**
 * 使用多线程处理立方体贴图实现（模板函数）
 * 
 * 执行步骤：
 * 1. 为每个面创建状态对象（从原型复制）
 * 2. 为每个面创建并行作业
 * 3. 如果状态为空状态，使用并行处理（parallel_for）
 * 4. 如果状态不为空，单线程处理（避免数据竞争）
 * 5. 等待所有线程完成
 * 6. 对每个面的状态执行归约操作
 * 
 * @param cm 立方体贴图对象
 * @param js 作业系统
 * @param proc 扫描线处理函数
 * @param reduce 归约处理函数
 * @param prototype 状态原型
 */
template<typename STATE>
void CubemapUtils::process(
        Cubemap& cm,
        utils::JobSystem& js,
        CubemapUtils::ScanlineProc<STATE> proc,
        ReduceProc<STATE> reduce,
        const STATE& prototype) {
    using namespace utils;

    const size_t dim = cm.getDimensions();

    // 为每个面创建状态对象（从原型复制）
    STATE states[6];
    for (STATE& s : states) {
        s = prototype;
    }

    // 创建父作业
    JobSystem::Job* parent = js.createJob();
    
    // 为每个面创建并行作业
    for (size_t faceIndex = 0; faceIndex < 6; faceIndex++) {

        // Lambda函数：处理单个面的作业
        auto perFaceJob = [faceIndex, &states, &cm, dim, &proc]
                (utils::JobSystem& js, utils::JobSystem::Job* parent) {
            STATE& s = states[faceIndex];
            Image& image(cm.getImageForFace((Cubemap::Face)faceIndex));

            // here we must limit how much we capture so we can use this closure
            // by value.
            // 这里我们必须限制捕获的内容，以便可以按值使用此闭包
            // Lambda函数：并行处理扫描线任务
            auto parallelJobTask = [&s, &image, &proc, dim = uint16_t(dim),
                                    faceIndex = uint8_t(faceIndex)](size_t y0, size_t c) {
                // 处理指定范围的扫描线
                for (size_t y = y0; y < y0 + c; y++) {
                    Cubemap::Texel* data =
                            static_cast<Cubemap::Texel*>(image.getPixelRef(0, y));
                    proc(s, y, (Cubemap::Face)faceIndex, data, dim);
                }
            };

            // 检查状态是否为空状态
            constexpr bool isStateLess = std::is_same<STATE, CubemapUtils::EmptyState>::value;
            if (UTILS_LIKELY(isStateLess)) {
                // create the job, copying it by value
                // 创建作业，按值复制
                // 如果状态为空，可以使用并行处理（无数据竞争）
                auto job = jobs::parallel_for(js, parent, 0, uint32_t(dim),
                        parallelJobTask, jobs::CountSplitter<64, 8>());
                // not need to signal here, since we're just scheduling work
                // 这里不需要信号，因为我们只是在调度工作
                js.run(job);
            } else {
                // if we have a per-thread STATE, we can't parallel_for()
                // 如果每个线程有状态，不能使用parallel_for()（避免数据竞争）
                parallelJobTask(0, dim);
            }
        };

        // not need to signal here, since we're just scheduling work
        // 这里不需要信号，因为我们只是在调度工作
        js.run(jobs::createJob(js, parent, perFaceJob, std::ref(js), parent));
    }

    // wait for all our threads to finish
    // 等待所有线程完成
    js.runAndWait(parent);

    // 对每个面的状态执行归约操作
    for (STATE& s : states) {
        reduce(s);
    }
}

/**
 * 单线程处理立方体贴图实现（模板函数）
 * 
 * 执行步骤：
 * 1. 从原型创建状态对象
 * 2. 顺序处理每个面的每一行
 * 3. 对所有面执行归约操作
 * 
 * @param cm 立方体贴图对象
 * @param js 作业系统（未使用，但保留接口一致性）
 * @param proc 扫描线处理函数
 * @param reduce 归约处理函数
 * @param prototype 状态原型
 */
template<typename STATE>
void CubemapUtils::processSingleThreaded(
        Cubemap& cm,
        utils::JobSystem& js,
        CubemapUtils::ScanlineProc<STATE> proc,
        ReduceProc<STATE> reduce,
        const STATE& prototype) {
    using namespace utils;

    const size_t dim = cm.getDimensions();

    // 从原型创建状态对象
    STATE s;
    s = prototype;

    // 顺序处理每个面的每一行
    for (size_t faceIndex = 0; faceIndex < 6; faceIndex++) {
        const Cubemap::Face f = (Cubemap::Face)faceIndex;
        Image& image(cm.getImageForFace(f));
        for (size_t y = 0; y < dim; y++) {
            Cubemap::Texel* data = static_cast<Cubemap::Texel*>(image.getPixelRef(0, y));
            proc(s, y, f, data, dim);
        }
    }
    
    // 执行归约操作
    reduce(s);
}


} // namespace ibl
} // namespace filament

#endif // IBL_CUBEMAPUTILSIMPL_H
