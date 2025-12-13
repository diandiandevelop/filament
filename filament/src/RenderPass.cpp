/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "RenderPass.h"

#include "RenderPrimitive.h"
#include "ShadowMap.h"
#include "SharedHandle.h"

#include "details/Material.h"
#include "details/MaterialInstance.h"
#include "details/View.h"

#include "components/RenderableManager.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>
#include <private/filament/Variant.h>

#include <filament/MaterialEnums.h>

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>

#include "private/backend/CircularBuffer.h"
#include "private/backend/CommandStream.h"

#include <private/utils/Tracing.h>

#include <utils/JobSystem.h>
#include <utils/Panic.h>
#include <utils/Range.h>
#include <utils/Slice.h>
#include <utils/compiler.h>
#include <utils/debug.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

using namespace utils;
using namespace filament::math;

namespace filament {

using namespace backend;

/**
 * 添加自定义命令
 * 
 * 向渲染通道构建器添加自定义命令。
 * 
 * @param channel 通道索引
 * @param pass 通道类型
 * @param custom 自定义命令类型
 * @param order 命令顺序
 * @param command 命令函数
 * @return 构建器引用（支持链式调用）
 */
RenderPassBuilder& RenderPassBuilder::customCommand(
        uint8_t channel,
        RenderPass::Pass pass,
        RenderPass::CustomCommand custom,
        uint32_t order,
        RenderPass::Executor::CustomCommandFn const& command) {
    /**
     * 如果自定义命令向量尚未创建，创建它
     */
    if (!mCustomCommands.has_value()) {
        // construct the vector the first time
        mCustomCommands.emplace();
    }
    /**
     * 添加自定义命令到向量
     */
    mCustomCommands->emplace_back(channel, pass, custom, order, command);
    return *this;
}

/**
 * 构建渲染通道
 * 
 * 从构建器创建渲染通道对象。
 * 
 * @param engine 引擎引用
 * @param driver 驱动 API 引用
 * @return 渲染通道对象
 */
RenderPass RenderPassBuilder::build(FEngine const& engine, DriverApi& driver) const {
    /**
     * 确保可渲染数据存在
     */
    assert_invariant(mRenderableSoa);
    /**
     * 创建渲染通道对象
     */
    return RenderPass{ engine, driver, *this };
}

// ------------------------------------------------------------------------------------------------

/**
 * 缓冲区对象句柄删除器
 * 
 * 用于自动销毁缓冲区对象句柄。
 * 
 * @param handle 缓冲区对象句柄
 */
void RenderPass::BufferObjectHandleDeleter::operator()(BufferObjectHandle handle) noexcept {
    if (handle) { // this is common case
        /**
         * 销毁缓冲区对象
         */
        driver.get().destroyBufferObject(handle);
    }
}

/**
 * 描述符堆句柄删除器
 * 
 * 用于自动销毁描述符堆句柄。
 * 
 * @param handle 描述符堆句柄
 */
void RenderPass::DescriptorSetHandleDeleter::operator()(
        DescriptorSetHandle handle) noexcept {
    if (handle) { // this is common case
        /**
         * 销毁描述符堆
         */
        driver.get().destroyDescriptorSet(handle);
    }
}

// ------------------------------------------------------------------------------------------------

/**
 * RenderPass 构造函数
 * 
 * 从构建器创建渲染通道，分配命令缓冲区，添加命令，排序并实例化。
 * 
 * @param engine 引擎引用
 * @param driver 驱动 API 引用
 * @param builder 构建器引用
 */
RenderPass::RenderPass(FEngine const& engine, DriverApi& driver,
        RenderPassBuilder const& builder) noexcept
        : mRenderableSoa(*builder.mRenderableSoa),  // 保存可渲染数据引用
          mColorPassDescriptorSet(builder.mColorPassDescriptorSet) {  // 保存颜色通道描述符堆

    // compute the number of commands we need
    /**
     * 更新累积的图元数量
     */
    updateSummedPrimitiveCounts(
            const_cast<FScene::RenderableSoa&>(mRenderableSoa), builder.mVisibleRenderables);

    /**
     * 计算基础命令数量（基于可见图元）
     */
    uint32_t commandCount =
            FScene::getPrimitiveCount(mRenderableSoa, builder.mVisibleRenderables.last);
    /**
     * 检查是否需要颜色通道和深度通道
     */
    const bool colorPass  = bool(builder.mCommandTypeFlags & CommandTypeFlags::COLOR);
    const bool depthPass  = bool(builder.mCommandTypeFlags & CommandTypeFlags::DEPTH);
    /**
     * 根据通道类型调整命令数量
     * 颜色通道需要 2 倍命令（可能是双通道渲染）
     */
    commandCount *= uint32_t(colorPass * 2 + depthPass);
    commandCount += 1; // for the sentinel

    /**
     * 计算自定义命令数量
     */
    uint32_t const customCommandCount =
            builder.mCustomCommands.has_value() ? builder.mCustomCommands->size() : 0;

    // FIXME: builder.mArena must be thread safe eventually
    /**
     * 从内存池分配命令缓冲区
     */
    Command* const commandBegin = builder.mArena.alloc<Command>(commandCount + customCommandCount);
    Command* commandEnd = commandBegin + (commandCount + customCommandCount);
    assert_invariant(commandBegin);

    // FIXME: builder.mArena must be thread safe eventually
    /**
     * 检查是否使用了堆分配（性能警告）
     */
    if (UTILS_UNLIKELY(builder.mArena.getAllocator().isHeapAllocation(commandBegin))) {
        static bool sLogOnce = true;
        if (UTILS_UNLIKELY(sLogOnce)) {
            sLogOnce = false;
            PANIC_LOG("RenderPass arena is full, using slower system heap. Please increase "
                      "the appropriate constant (e.g. FILAMENT_PER_RENDER_PASS_ARENA_SIZE_IN_MB).");
        }
    }

    /**
     * 添加渲染命令
     */
    appendCommands(engine, { commandBegin, commandCount },
            builder.mVisibleRenderables,
            builder.mCommandTypeFlags,
            builder.mFlags,
            builder.mVisibilityMask,
            builder.mVariant,
            builder.mCameraPosition,
            builder.mCameraForwardVector);

    /**
     * 添加自定义命令
     */
    if (builder.mCustomCommands.has_value()) {
        mCustomCommands.reserve(customCommandCount);
        Command* p = commandBegin + commandCount;
        for (auto const& [channel, passId, command, order, fn]: builder.mCustomCommands.value()) {
            appendCustomCommand(p++, channel, passId, command, order, fn);
        }
    }

    // sort commands once we're done adding commands
    /**
     * 排序命令（按材质、深度等）
     */
    commandEnd = resize(builder.mArena,
            sortCommands(commandBegin, commandEnd));

    /**
     * 如果启用自动实例化，进行实例化处理
     */
    if (engine.isAutomaticInstancingEnabled()) {
        /**
         * 计算立体渲染眼睛数量
         */
        int32_t stereoscopicEyeCount = 1;
        if (builder.mFlags & IS_INSTANCED_STEREOSCOPIC) {
            stereoscopicEyeCount *= engine.getConfig().stereoscopicEyeCount;
        }
        /**
         * 执行实例化（合并相同材质的图元）
         */
        commandEnd = resize(builder.mArena,
                instanceify(driver,
                        engine.getPerRenderableDescriptorSetLayout().getHandle(),
                        commandBegin, commandEnd, stereoscopicEyeCount));
    }

    // these are `const` from this point on...
    /**
     * 保存命令缓冲区范围（从此时起为 const）
     */
    mCommandBegin = commandBegin;
    mCommandEnd = commandEnd;
}

/**
 * RenderPass 析构函数
 * 
 * 注意：此析构函数实际上比较重，因为它内联了 ~vector<> 的析构。
 */
// this destructor is actually heavy because it inlines ~vector<>
RenderPass::~RenderPass() noexcept = default;

/**
 * 调整内存池大小
 * 
 * 将内存池回退到指定位置，释放未使用的内存。
 * 
 * @param arena 内存池引用
 * @param last 最后一个有效命令的指针
 * @return 最后一个有效命令的指针
 */
RenderPass::Command* RenderPass::resize(Arena& arena, Command* const last) noexcept {
    /**
     * 回退内存池到指定位置
     */
    arena.rewind(last);
    return last;
}

/**
 * 添加渲染命令
 * 
 * 为可见的可渲染对象生成渲染命令。
 * 
 * 实现细节：
 * - 如果可见可渲染对象为空，只添加 SENTINEL 命令
 * - 如果可见可渲染对象数量较少，使用单线程生成命令
 * - 如果可见可渲染对象数量较多，使用多线程并行生成命令
 * - 使用 JobSystem 进行并行处理
 * 
 * @param engine 引擎常量引用
 * @param commands 命令缓冲区切片
 * @param visibleRenderables 可见可渲染对象的范围
 * @param commandTypeFlags 命令类型标志
 * @param renderFlags 渲染标志
 * @param visibilityMask 可见性掩码
 * @param variant 材质变体
 * @param cameraPosition 相机位置
 * @param cameraForwardVector 相机前向量
 */
void RenderPass::appendCommands(FEngine const& engine,
        Slice<Command> commands,
        Range<uint32_t> const visibleRenderables,
        CommandTypeFlags const commandTypeFlags,
        RenderFlags const renderFlags,
        FScene::VisibleMaskType const visibilityMask,
        Variant const variant,
        float3 const cameraPosition,
        float3 const cameraForwardVector) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 跟踪可见可渲染对象的数量
     */
    // trace the number of visible renderables
    FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "visibleRenderables", visibleRenderables.size());
    
    /**
     * 如果没有可渲染对象，我们仍然需要 SENTINEL，命令缓冲区大小应该正好为 1。
     */
    if (UTILS_UNLIKELY(visibleRenderables.empty())) {
        // no renderables, we still need the sentinel and the command buffer size should be
        // exactly 1.
        assert_invariant(commands.size() == 1);
        Command* curr = commands.data();
        curr->key = uint64_t(Pass::SENTINEL);  // 设置 SENTINEL
        return;  // 提前返回
    }

    /**
     * 获取作业系统引用
     */
    JobSystem& js = engine.getJobSystem();

    /**
     * 获取最新的累积图元数量（generateCommands() 需要）
     */
    // up-to-date summed primitive counts needed for generateCommands()
    FScene::RenderableSoa const& soa = mRenderableSoa;

    /**
     * 获取命令缓冲区指针和大小
     */
    Command* curr = commands.data();
    size_t const commandCount = commands.size();

    /**
     * 获取立体渲染眼睛数量
     */
    auto stereoscopicEyeCount = engine.getConfig().stereoscopicEyeCount;

    /**
     * 创建工作函数（lambda）
     * 
     * 此函数用于并行生成命令，处理指定范围的可渲染对象。
     */
    auto work = [commandTypeFlags, curr, &soa,
                 variant, renderFlags, visibilityMask,
                 cameraPosition, cameraForwardVector, stereoscopicEyeCount]
            (uint32_t const startIndex, uint32_t const indexCount) {
        /**
         * 为指定范围的可渲染对象生成命令
         */
        generateCommands(commandTypeFlags, curr,
                soa, { startIndex, startIndex + indexCount },
                variant, renderFlags, visibilityMask,
                cameraPosition, cameraForwardVector, stereoscopicEyeCount);
    };

    /**
     * 如果可见可渲染对象数量较少，使用单线程处理
     */
    if (visibleRenderables.size() <= JOBS_PARALLEL_FOR_COMMANDS_COUNT) {
        /**
         * 直接调用工作函数，单线程处理所有可渲染对象
         */
        work(visibleRenderables.first, visibleRenderables.size());
    } else {
        /**
         * 使用多线程并行处理
         * 
         * 将可渲染对象范围分割成多个块，每个块由一个线程处理。
         * 使用 CountSplitter 来分割工作负载。
         */
        auto* jobCommandsParallel = parallel_for(js, nullptr,
                visibleRenderables.first, uint32_t(visibleRenderables.size()),
                std::cref(work), jobs::CountSplitter<JOBS_PARALLEL_FOR_COMMANDS_COUNT>());
        /**
         * 运行并等待所有并行任务完成
         */
        js.runAndWait(jobCommandsParallel);
    }
    
    /**
     * 生成绘制命令支持多线程分块；空命令槽末尾写入 sentinel
     * 
     * 注意：多线程生成命令时，每个线程处理不同的命令缓冲区范围，
     * 因此不需要额外的同步。命令缓冲区的大小已经预先计算好。
     */
    // 生成绘制命令支持多线程分块；空命令槽末尾写入 sentinel

    /**
     * 始终添加一个 "eof" 命令
     * 
     * "eof" 命令。这些命令保证在命令缓冲区中最后排序。
     * 它用作命令流的结束标记。
     */
    // Always add an "eof" command
    // "eof" command. These commands are guaranteed to be sorted last in the
    // command buffer.
    curr[commandCount - 1].key = uint64_t(Pass::SENTINEL);  // 设置 SENTINEL 命令

    /**
     * 遍历所有命令并调用 prepareProgram()。
     * 
     * 这必须从主线程完成，因为 prepareProgram() 可能涉及资源分配和编译。
     * 此步骤确保所有着色器程序在使用前都已准备好。
     */
    // Go over all the commands and call prepareProgram().
    // This must be done from the main thread.
    for (Command const* first = curr, *last = curr + commandCount ; first != last ; ++first) {
        /**
         * 检查是否为普通通道命令（非自定义命令）
         */
        if (UTILS_LIKELY((first->key & CUSTOM_MASK) == uint64_t(CustomCommand::PASS))) {
            /**
             * 获取材质并准备着色器程序
             * 
             * 使用 CRITICAL 优先级，因为这些程序将在当前帧中使用。
             */
            auto ma = first->info.mi->getMaterial();
            ma->prepareProgram(first->info.materialVariant, CompilerPriorityQueue::CRITICAL);
        }
    }
    // 主线程确保相关 Program 已准备好（编译/链接）
}

/**
 * 添加自定义命令
 * 
 * 向命令缓冲区添加自定义命令，并设置命令键。
 * 
 * @param commands 命令指针（要写入的位置）
 * @param channel 通道索引（限制在有效范围内）
 * @param pass 通道类型
 * @param custom 自定义命令类型
 * @param order 命令顺序（用于排序）
 * @param command 命令函数（将被移动到 mCustomCommands 中）
 */
void RenderPass::appendCustomCommand(Command* commands,
        uint8_t channel, Pass pass, CustomCommand custom, uint32_t const order,
        Executor::CustomCommandFn command) {
    /**
     * 检查顺序值是否在有效范围内
     */
    assert_invariant((uint64_t(order) << CUSTOM_ORDER_SHIFT) <=  CUSTOM_ORDER_MASK);

    /**
     * 限制通道索引在有效范围内
     */
    channel = std::min(channel, uint8_t(CHANNEL_COUNT - 1));

    /**
     * 将命令函数添加到自定义命令向量中
     */
    uint32_t const index = mCustomCommands.size();
    mCustomCommands.push_back(std::move(command));  // 移动命令函数

    /**
     * 构建命令键
     * 
     * 命令键包含：
     * - pass: 通道类型
     * - channel: 通道索引（左移 CHANNEL_SHIFT 位）
     * - custom: 自定义命令类型
     * - order: 命令顺序（左移 CUSTOM_ORDER_SHIFT 位）
     * - index: 命令函数在 mCustomCommands 中的索引
     */
    uint64_t cmd = uint64_t(pass);
    cmd |= uint64_t(channel) << CHANNEL_SHIFT;
    cmd |= uint64_t(custom);
    cmd |= uint64_t(order) << CUSTOM_ORDER_SHIFT;
    cmd |= uint64_t(index);

    /**
     * 设置命令键
     */
    commands->key = cmd;
}

/**
 * 排序命令
 * 
 * 对命令缓冲区中的命令进行排序。
 * 排序后，SENTINEL 命令会被放在最后。
 * 
 * @param begin 命令缓冲区起始指针
 * @param end 命令缓冲区结束指针
 * @return 最后一个有效命令的指针（SENTINEL 之前）
 */
RenderPass::Command* RenderPass::sortCommands(
        Command* const begin, Command* const end) noexcept {
    FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "sort commands");

    /**
     * 使用 std::sort 对命令进行排序
     * 
     * 排序基于 Command 的 operator<，它比较命令键。
     * 排序后的顺序决定了命令的执行顺序。
     */
    std::sort(begin, end);

    /**
     * 查找最后一个有效命令
     * 
     * SENTINEL 命令的键为 Pass::SENTINEL，它们会被排序到最后。
     * 使用 std::partition_point 找到第一个 SENTINEL 命令的位置。
     */
    // find the last command
    Command* const last = std::partition_point(begin, end,
            [](Command const& c) {
                return c.key != uint64_t(Pass::SENTINEL);  // 找到第一个 SENTINEL
            });

    return last;  // 返回最后一个有效命令的指针
}

/**
 * 实例化命令
 * 
 * 通过扫描已排序的命令流，查找重复的绘制命令，并将它们替换为实例化命令。
 * 
 * 算法：
 * - 扫描已排序的命令流，查找具有相同绘制参数和状态的命令
 * - 当找到重复命令时，将它们合并为一个实例化命令
 * - 第一个命令被修改为实例化命令，其他命令被标记为 SENTINEL
 * - 为实例化数据创建临时 UBO 和描述符堆
 * 
 * 限制：
 * - 当前依赖于"重复绘制"命令在排序后是连续的（这需要一些运气）
 * - 可以通过在排序键中包含一些或所有这些"重复"参数来改进
 *   （例如，光栅状态、图元句柄等），键甚至可以使用这些参数的小哈希值
 * - 不支持蒙皮或变形（因为比较数据不易访问）
 * - 不支持手动或混合实例化
 * 
 * @param driver 驱动 API 引用
 * @param perRenderableDescriptorSetLayoutHandle 每个可渲染对象的描述符堆布局句柄
 * @param curr 当前命令指针
 * @param last 最后一个命令指针
 * @param eyeCount 眼睛数量（用于立体渲染）
 * @return 最后一个有效命令的指针
 */
RenderPass::Command* RenderPass::instanceify(DriverApi& driver,
        DescriptorSetLayoutHandle perRenderableDescriptorSetLayoutHandle,
        Command* curr, Command* const last,
        int32_t const eyeCount) const noexcept {
    FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "instanceify");

    /**
     * instanceify 通过扫描已排序的命令流来工作，查找重复的绘制命令。
     * 当找到一个时，它被替换为实例化命令。
     * "重复"绘制是指最终使用相同绘制参数和状态的绘制。
     * 
     * 当前，这在一定程度上依赖于"重复绘制"在排序后是连续的（这需要一些运气），
     * 我们可以通过在排序键中包含一些或所有这些"重复"参数来改进
     * （例如，光栅状态、图元句柄等），键甚至可以使用这些参数的小哈希值。
     */
    // instanceify works by scanning the **sorted** command stream, looking for repeat draw
    // commands. When one is found, it is replaced by an instanced command.
    // A "repeat" draw is one that ends-up using the same draw parameters and state.
    // Currently, this relies somewhat on luck that "repeat draws" are found consecutively,
    // we could improve this by including some or all of these "repeat" parameters in the
    // sorting key (e.g. raster state, primitive handle, etc...), the key could even use a small
    // hash of those parameters.

    /**
     * 统计节省的绘制调用次数（用于调试，当前未使用）
     */
    UTILS_UNUSED uint32_t drawCallsSavedCount = 0;

    /**
     * 第一个 SENTINEL 命令的位置（用于后续清理）
     */
    Command* firstSentinel = nullptr;
    
    /**
     * UBO 数据指针（从可渲染数据中获取）
     */
    PerRenderableData const* uboData = nullptr;
    
    /**
     * 临时缓冲区（用于存储实例化数据，稍后上传到 GPU）
     */
    PerRenderableData* stagingBuffer = nullptr;
    
    /**
     * 临时缓冲区大小
     */
    uint32_t stagingBufferSize = 0;
    
    /**
     * 实例化图元的偏移量（在临时缓冲区中的位置）
     */
    uint32_t instancedPrimitiveOffset = 0;
    
    /**
     * 命令总数
     */
    size_t const count = last - curr;

    /**
     * 最大实例数量
     * 
     * TODO: 对于实例化的情况，我们实际上可以使用 128 而不是 64 个实例
     */
    // TODO: for the case of instancing we could actually use 128 instead of 64 instances
    constexpr size_t maxInstanceCount = CONFIG_MAX_INSTANCES;

    /**
     * 扫描排序后的命令流，尝试合并为实例化绘制
     */
    while (curr != last) { // 扫描排序后的命令流，尝试合并为 instanced draw
        /**
         * 当前，如果我们有蒙皮或变形，我们不能使用自动实例化。
         * 这是因为用于比较的变形/蒙皮数据不易访问；而且
         * 我们假设每个可渲染对象的描述符堆只需要保存此 UBO（在实例化情况下）
         * （如果我们支持蒙皮/变形，这将不成立，在这种情况下我们需要保留
         * 默认描述符堆的内容）。
         * 如果使用手动或混合实例化，我们也不能使用自动实例化。
         * 
         * TODO: 支持蒙皮/变形的自动实例化
         */
        // Currently, if we have skinning or morphing, we can't use auto instancing. This is
        // because the morphing/skinning data for comparison is not easily accessible; and also
        // because we're assuming that the per-renderable descriptor-set only has the
        // OBJECT_UNIFORMS descriptor active (i.e. the skinning/morphing descriptors are unused).
        // We also can't use auto-instancing if manual- or hybrid- instancing is used.
        // TODO: support auto-instancing for skinning/morphing
        
        /**
         * 默认情况下，下一个命令就是当前命令的下一个
         */
        Command const* e = curr + 1;
        
        /**
         * 检查是否可以实例化
         * 
         * 可以实例化的条件：
         * - 没有蒙皮
         * - 没有变形
         * - 实例数量 <= 1（不是已经实例化的命令）
         */
        if (UTILS_LIKELY(
                !curr->info.hasSkinning && !curr->info.hasMorphing &&
                 curr->info.instanceCount <= 1))
        {
            /**
             * 确保没有混合实例化
             */
            assert_invariant(!curr->info.hasHybridInstancing);
            
            /**
             * 查找可以合并为实例化的命令范围
             * 
             * 由于 UBO 大小限制，我们不能有超过 maxInstanceCount 的实例。
             * 我们查找从 curr 开始，最多 maxInstanceCount 个命令，找出所有可以实例化的命令。
             * 
             * 图元必须完全相同才能被实例化。
             * 当前，实例化不支持蒙皮/变形。
             */
            // we can't have nice things! No more than maxInstanceCount due to UBO size limits
            e = std::find_if_not(curr, std::min(last, curr + maxInstanceCount),
                    [lhs = *curr](Command const& rhs) {
                        // primitives must be identical to be instanced
                        // Currently, instancing doesn't support skinning/morphing.
                        /**
                         * 比较两个命令是否相同（可以实例化）
                         * 
                         * 需要比较的字段：
                         * - 材质实例
                         * - 渲染图元句柄
                         * - 顶点缓冲区信息句柄
                         * - 索引偏移量
                         * - 索引数量
                         * - 光栅状态
                         */
                        return lhs.info.mi == rhs.info.mi &&
                               lhs.info.rph == rhs.info.rph &&
                               lhs.info.vbih == rhs.info.vbih &&
                               lhs.info.indexOffset == rhs.info.indexOffset &&
                               lhs.info.indexCount == rhs.info.indexCount &&
                               lhs.info.rasterState == rhs.info.rasterState;
                    });
        }

        /**
         * 计算实例数量
         */
        uint32_t const instanceCount = e - curr;
        
        /**
         * 确保实例数量有效
         */
        assert_invariant(instanceCount > 0);
        assert_invariant(instanceCount <= CONFIG_MAX_INSTANCES);

        /**
         * 如果找到多个可以实例化的命令，进行实例化处理
         */
        if (UTILS_UNLIKELY(instanceCount > 1)) {
            /**
             * 统计节省的绘制调用次数
             */
            drawCallsSavedCount += instanceCount - 1; // 统计节省的 draw 次数

            /**
             * 仅在需要时分配临时缓冲区
             */
            // allocate our staging buffer only if needed
            if (UTILS_UNLIKELY(!stagingBuffer)) {
                /**
                 * 创建一个临时 UBO 来保存每个图元的每个可渲染对象数据。
                 * `curr->info.index` 被更新，以便这个（现在是实例化的）命令可以
                 * 在正确的位置绑定 UBO（每个实例数据所在的位置）。
                 * 此对象的生命周期是此 RenderPass 及其所有执行器中最长的。
                 */
                // Create a temporary UBO for holding the per-renderable data of each primitive,
                // The `curr->info.index` is updated so that this (now instanced) command can
                // bind the UBO in the right place (where the per-instance data is).
                // The lifetime of this object is the longest of this RenderPass and all its
                // executors.

                /**
                 * 创建用于实例化的临时 UBO
                 * 
                 * 大小：count * sizeof(PerRenderableData) + sizeof(PerRenderableUib)
                 * - count * sizeof(PerRenderableData): 所有实例的数据
                 * - sizeof(PerRenderableUib): UBO 头部
                 */
                // create a temporary UBO for instancing
                mInstancedUboHandle = BufferObjectSharedHandle{
                        driver.createBufferObject(
                                count * sizeof(PerRenderableData) + sizeof(PerRenderableUib),
                                BufferObjectBinding::UNIFORM, BufferUsage::STATIC), driver };

                /**
                 * TODO: 对于小尺寸，使用流内联缓冲区
                 * TODO: 对于较大的堆缓冲区，使用池
                 * 
                 * 分配足够大的缓冲区来存储所有实例数据
                 */
                // TODO: use stream inline buffer for small sizes
                // TODO: use a pool for larger heap buffers
                // buffer large enough for all instances data
                stagingBufferSize = count * sizeof(PerRenderableData);
                stagingBuffer = static_cast<PerRenderableData*>(malloc(stagingBufferSize));
                
                /**
                 * 获取 UBO 数据指针
                 */
                uboData = mRenderableSoa.data<FScene::UBO>();
                assert_invariant(uboData);

                /**
                 * 我们还需要一个描述符堆来保存自定义 UBO。
                 * 
                 * 这有效是因为我们当前假设描述符堆在实例化情况下只需要保存此 UBO
                 * （如果我们支持蒙皮/变形，这将不成立，在这种情况下我们需要保留
                 * 默认描述符堆的内容）。
                 * 这与 UBO 具有相同的生命周期（见上文）。
                 */
                // We also need a descriptor-set to hold the custom UBO. This works because
                // we currently assume the descriptor-set only needs to hold this UBO in the
                // instancing case (it wouldn't be true if we supported skinning/morphing, and
                // in this case we would need to preserve the default descriptor-set content).
                // This has the same lifetime as the UBO (see above).
                mInstancedDescriptorSetHandle = DescriptorSetSharedHandle{
                        driver.createDescriptorSet(perRenderableDescriptorSetLayoutHandle),
                        driver
                };
                
                /**
                 * 更新描述符堆，绑定 UBO
                 */
                driver.updateDescriptorSetBuffer(mInstancedDescriptorSetHandle,
                        +PerRenderableBindingPoints::OBJECT_UNIFORMS,
                        mInstancedUboHandle, 0, sizeof(PerRenderableUib));
            }

            /**
             * 将 UBO 数据复制到临时缓冲区
             * 
             * 确保有足够的空间
             */
            // copy the ubo data to a staging buffer
            assert_invariant(instancedPrimitiveOffset + instanceCount
                             <= stagingBufferSize / sizeof(PerRenderableData));
            
            /**
             * 复制每个实例的 UBO 数据
             */
            for (uint32_t i = 0; i < instanceCount; i++) {
                stagingBuffer[instancedPrimitiveOffset + i] = uboData[curr[i].info.index];
            }

            /**
             * 使第一个命令成为实例化命令
             * 
             * - 更新实例数量（乘以眼睛数量，用于立体渲染）
             * - 更新索引（指向临时缓冲区中的位置）
             * - 更新描述符堆句柄（使用实例化描述符堆）
             */
            // make the first command instanced
            curr[0].info.instanceCount = instanceCount * eyeCount;  // 实例数量
            curr[0].info.index = instancedPrimitiveOffset;  // 索引偏移量
            curr[0].info.dsh = mInstancedDescriptorSetHandle;  // 描述符堆句柄

            /**
             * 更新实例化图元偏移量
             */
            instancedPrimitiveOffset += instanceCount;

            /**
             * 取消现在已实例化的命令
             * 
             * 第一个命令保留（已修改为实例化命令），其他命令标记为 SENTINEL。
             */
            // cancel commands that are now instances
            firstSentinel = !firstSentinel ? curr : firstSentinel;  // 记录第一个 SENTINEL 位置
            for (uint32_t i = 1; i < instanceCount; i++) {
                curr[i].key = uint64_t(Pass::SENTINEL);  // 标记为 SENTINEL
            }
        }

        /**
         * 移动到下一个命令（跳过已处理的命令）
         */
        curr = const_cast<Command*>(e);
    }

    /**
     * 如果有实例化的图元，需要上传数据并清理
     */
    if (UTILS_UNLIKELY(firstSentinel)) {
        /**
         * 调试日志（已注释）
         * DLOG(INFO) << "auto-instancing, saving " << drawCallsSavedCount << " draw calls, out of "
         *            << count;
         */
        // DLOG(INFO) << "auto-instancing, saving " << drawCallsSavedCount << " draw calls, out of "
        //            << count;
        
        /**
         * 我们有实例化的图元
         * 复制我们的实例化 UBO 数据到 GPU
         * 
         * 使用 updateBufferObjectUnsynchronized 进行异步更新。
         * 回调函数会在数据上传完成后释放临时缓冲区。
         */
        // we have instanced primitives
        // copy our instanced ubo data
        driver.updateBufferObjectUnsynchronized(mInstancedUboHandle, {
                stagingBuffer, sizeof(PerRenderableData) * instancedPrimitiveOffset,
                /**
                 * 回调函数：在上传完成后释放临时缓冲区
                 */
                +[](void* buffer, size_t, void*) {
                    free(buffer);  // 释放 malloc 分配的内存
                }
        }, 0);

        /**
         * 清空临时缓冲区指针（内存将在回调中释放）
         */
        stagingBuffer = nullptr;

        /**
         * 移除所有已取消的命令
         * 
         * 使用 std::remove_if 将所有 SENTINEL 命令移到末尾，
         * 返回最后一个有效命令的指针。
         */
        // remove all the canceled commands
        auto const lastCommand = std::remove_if(firstSentinel, last, [](auto const& command) {
            return command.key == uint64_t(Pass::SENTINEL);  // 查找 SENTINEL 命令
        });

        /**
         * 返回最后一个有效命令的指针
         */
        return lastCommand;
    }

    /**
     * 如果没有实例化，确保临时缓冲区为空
     */
    assert_invariant(stagingBuffer == nullptr);
    
    /**
     * 返回最后一个命令的指针（没有变化）
     */
    return last;
}


/**
 * 设置颜色命令
 * 
 * 为颜色通道设置命令的键和光栅状态。
 * 此函数仅用于提高代码可读性，我们希望它被内联。
 * 
 * 实现细节：
 * - 根据材质属性（混合模式、屏幕空间折射等）决定使用混合通道还是颜色通道
 * - 构建命令键，包含通道类型、材质变体、混合模式等信息
 * - 设置光栅状态（剔除、深度测试、颜色/深度写入等）
 * - 对于 SSR 通道，不透明对象的混合模式必须关闭
 * 
 * @param cmdDraw 命令引用（将被修改）
 * @param variant 材质变体
 * @param mi 材质实例指针
 * @param inverseFrontFaces 是否反转正面（用于镜像渲染）
 * @param hasDepthClamp 是否支持深度夹紧
 */
/* static */
UTILS_ALWAYS_INLINE // This function exists only to make the code more readable. we want it inlined.
inline              // and we don't need it in the compilation unit
void RenderPass::setupColorCommand(Command& cmdDraw, Variant variant,
        FMaterialInstance const* const UTILS_RESTRICT mi,
        bool const inverseFrontFaces, bool const hasDepthClamp) noexcept {

    FMaterial const * const UTILS_RESTRICT ma = mi->getMaterial();  // 获取材质
    /**
     * 过滤变体：根据材质是否支持光照过滤变体
     */
    variant = Variant::filterVariant(variant, ma->isVariantLit());

    /**
     * 下面，我们评估两个命令以避免分支
     * 
     * 我们同时构建混合通道键和颜色通道键，然后根据条件选择使用哪个。
     */
    // Below, we evaluate both commands to avoid a branch

    /**
     * 构建混合通道键
     * 
     * 混合通道用于透明对象，需要特殊的排序和混合处理。
     */
    uint64_t keyBlending = cmdDraw.key;
    keyBlending &= ~(PASS_MASK | BLENDING_MASK);  // 清除通道和混合掩码
    keyBlending |= uint64_t(Pass::BLENDED);  // 设置为混合通道
    keyBlending |= uint64_t(CustomCommand::PASS);  // 设置为普通通道命令

    /**
     * 确定是否使用混合命令
     * 
     * 混合命令用于：
     * - 非屏幕空间折射材质
     * - 非不透明且非遮罩的混合模式
     */
    BlendingMode const blendingMode = ma->getBlendingMode();
    bool const hasScreenSpaceRefraction = ma->getRefractionMode() == RefractionMode::SCREEN_SPACE;
    bool const isBlendingCommand = !hasScreenSpaceRefraction &&
            (blendingMode != BlendingMode::OPAQUE && blendingMode != BlendingMode::MASKED);

    /**
     * 构建颜色通道键
     * 
     * 颜色通道用于不透明对象，或屏幕空间折射对象。
     */
    uint64_t keyDraw = cmdDraw.key;
    keyDraw &= ~(PASS_MASK | BLENDING_MASK | MATERIAL_MASK);  // 清除相关掩码
    keyDraw |= uint64_t(hasScreenSpaceRefraction ? Pass::REFRACT : Pass::COLOR);  // 设置通道类型
    keyDraw |= uint64_t(CustomCommand::PASS);  // 设置为普通通道命令
    keyDraw |= mi->getSortingKey(); // already all set-up for direct or'ing  // 添加排序键
    keyDraw |= makeField(variant.key, MATERIAL_VARIANT_KEY_MASK, MATERIAL_VARIANT_KEY_SHIFT);  // 添加材质变体
    keyDraw |= makeField(ma->getRasterState().alphaToCoverage, BLENDING_MASK, BLENDING_SHIFT);  // 添加 Alpha-to-Coverage

    /**
     * 根据条件选择使用混合键还是颜色键
     */
    cmdDraw.key = isBlendingCommand ? keyBlending : keyDraw;
    
    /**
     * 设置光栅状态（从材质获取）
     */
    cmdDraw.info.rasterState = ma->getRasterState();

    /**
     * 对于 SSR 通道，不透明对象（包括遮罩对象）的混合模式必须关闭
     * 
     * 参见 Material.cpp。
     */
    // for SSR pass, the blending mode of opaques (including MASKED) must be off
    // see Material.cpp.
    const bool blendingMustBeOff = !isBlendingCommand && Variant::isSSRVariant(variant);
    cmdDraw.info.rasterState.blendFunctionSrcAlpha = blendingMustBeOff ?
            BlendFunction::ONE : cmdDraw.info.rasterState.blendFunctionSrcAlpha;  // 关闭混合
    cmdDraw.info.rasterState.blendFunctionDstAlpha = blendingMustBeOff ?
            BlendFunction::ZERO : cmdDraw.info.rasterState.blendFunctionDstAlpha;  // 关闭混合

    /**
     * 设置其他光栅状态
     */
    cmdDraw.info.rasterState.inverseFrontFaces = inverseFrontFaces;  // 反转正面
    cmdDraw.info.rasterState.culling = mi->getCullingMode();  // 剔除模式
    cmdDraw.info.rasterState.colorWrite = mi->isColorWriteEnabled();  // 颜色写入
    cmdDraw.info.rasterState.depthWrite = mi->isDepthWriteEnabled();  // 深度写入
    cmdDraw.info.rasterState.depthFunc = mi->getDepthFunc();  // 深度测试函数
    cmdDraw.info.rasterState.depthClamp = hasDepthClamp;  // 深度夹紧
    cmdDraw.info.materialVariant = variant;  // 材质变体
    
    /**
     * 我们保持 "RasterState::colorWrite" 为材质设置的值（可能被禁用）
     */
    // we keep "RasterState::colorWrite" to the value set by material (could be disabled)
}

/**
 * 生成命令
 * 
 * 为可见的可渲染对象生成绘制命令。
 * 
 * 实现细节：
 * - generateCommands() 同时写入绘制和深度命令，这样我们只需要遍历可渲染对象列表一次
 * - （原则上，我们可以将此方法拆分为两个，代价是遍历列表两次）
 * - 使用模板函数 generateCommandsImpl<> 来生成不同通道类型的专用版本
 * 
 * @param commandTypeFlags 命令类型标志（COLOR、DEPTH 等）
 * @param commands 命令缓冲区指针
 * @param soa 可渲染数据（SoA 结构）
 * @param range 可见可渲染对象的范围
 * @param variant 材质变体
 * @param renderFlags 渲染标志
 * @param visibilityMask 可见性掩码
 * @param cameraPosition 相机位置
 * @param cameraForward 相机前向量
 * @param instancedStereoEyeCount 实例化立体渲染眼睛数量
 */
/* static */
UTILS_NOINLINE
void RenderPass::generateCommands(CommandTypeFlags commandTypeFlags, Command* const commands,
        FScene::RenderableSoa const& soa, Range<uint32_t> const range,
        Variant const variant, RenderFlags const renderFlags,
        FScene::VisibleMaskType const visibilityMask,
        float3 const cameraPosition, float3 const cameraForward,
        uint8_t instancedStereoEyeCount) noexcept {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * generateCommands() 同时写入绘制和深度命令，这样我们只需要遍历可渲染对象列表一次。
     * （原则上，我们可以将此方法拆分为两个，代价是遍历列表两次）
     */
    // generateCommands() writes both the draw and depth commands simultaneously such that
    // we go throw the list of renderables just once.
    // (in principle, we could have split this method into two, at the cost of going through
    // the list twice)

    /**
     * 计算所需的最大存储空间
     * 
     * 透明对象需要渲染两次，所以颜色通道需要 2 倍命令。
     */
    // compute how much maximum storage we need
    // double the color pass for transparent objects that need to render twice
    const bool colorPass  = bool(commandTypeFlags & CommandTypeFlags::COLOR);
    const bool depthPass  = bool(commandTypeFlags & CommandTypeFlags::DEPTH);
    const size_t commandsPerPrimitive = uint32_t(colorPass * 2 + depthPass);  // 每个图元的命令数
    
    /**
     * 计算命令缓冲区的起始和结束偏移量
     */
    const size_t offsetBegin = FScene::getPrimitiveCount(soa, range.first) * commandsPerPrimitive;
    const size_t offsetEnd   = FScene::getPrimitiveCount(soa, range.last) * commandsPerPrimitive;
    Command* curr = commands + offsetBegin;  // 当前命令指针
    Command* const last = commands + offsetEnd;  // 最后一个命令指针

    /**
     * 下面的 switch {} 用于强制编译器根据我们处理的通道类型生成不同版本的
     * "generateCommandsImpl"。
     *
     * 我们使用模板函数（而不是仅仅内联），这样编译器能够生成实际的独立版本的
     * generateCommandsImpl<>，这更容易调试且不影响性能（只是一个预测跳转）。
     */
    /*
     * The switch {} below is to coerce the compiler into generating different versions of
     * "generateCommandsImpl" based on which pass we're processing.
     *
     *  We use a template function (as opposed to just inlining), so that the compiler is
     *  able to generate actual separate versions of generateCommandsImpl<>, which is much
     *  easier to debug and doesn't impact performance (it's just a predicted jump).
     */

    /**
     * 根据通道类型调用相应的模板函数
     */
    switch (commandTypeFlags & (CommandTypeFlags::COLOR | CommandTypeFlags::DEPTH)) {
        case CommandTypeFlags::COLOR:
            /**
             * 生成颜色通道命令
             */
            curr = generateCommandsImpl<CommandTypeFlags::COLOR>(commandTypeFlags, curr,
                    soa, range,
                    variant, renderFlags, visibilityMask, cameraPosition, cameraForward,
                    instancedStereoEyeCount);
            break;
        case CommandTypeFlags::DEPTH:
            /**
             * 生成深度通道命令
             */
            curr = generateCommandsImpl<CommandTypeFlags::DEPTH>(commandTypeFlags, curr,
                    soa, range,
                    variant, renderFlags, visibilityMask, cameraPosition, cameraForward,
                    instancedStereoEyeCount);
            break;
        default:
            /**
             * 我们不应该到达这里
             */
            // we should never end-up here
            break;
    }

    /**
     * 确保当前指针不超过结束指针
     */
    assert_invariant(curr <= last);

    /**
     * 命令可能被跳过，取消所有未使用的命令槽位
     */
    // commands may have been skipped, cancel all of them.
    while (curr != last) {
        curr->key = uint64_t(Pass::SENTINEL);  // 标记为 SENTINEL
        ++curr;
    }
}

/**
 * 生成命令实现（模板函数）
 * 
 * 为可见的可渲染对象生成绘制或深度命令。
 * 这是一个模板函数，根据 commandTypeFlags 生成颜色通道或深度通道的专用版本。
 * 
 * 实现细节：
 * - 遍历可见可渲染对象范围
 * - 为每个可渲染对象的每个图元生成命令
 * - 计算到相机的距离用于排序
 * - 处理蒙皮、变形、实例化等特殊情况
 * - 设置命令键和光栅状态
 * 
 * @tparam commandTypeFlags 命令类型标志（COLOR 或 DEPTH）
 * @param extraFlags 额外标志（DEPTH_CONTAINS_SHADOW_CASTERS 等）
 * @param curr 当前命令指针（将被更新）
 * @param soa 可渲染数据（SoA 结构）
 * @param range 可见可渲染对象的范围
 * @param variant 材质变体
 * @param renderFlags 渲染标志
 * @param visibilityMask 可见性掩码
 * @param cameraPosition 相机位置
 * @param cameraForward 相机前向量
 * @param instancedStereoEyeCount 实例化立体渲染眼睛数量
 * @return 更新后的命令指针
 */
/* static */
template<RenderPass::CommandTypeFlags commandTypeFlags>
UTILS_NOINLINE
RenderPass::Command* RenderPass::generateCommandsImpl(CommandTypeFlags extraFlags,
        Command* UTILS_RESTRICT curr,
        FScene::RenderableSoa const& UTILS_RESTRICT soa, Range<uint32_t> range,
        Variant const variant, RenderFlags renderFlags, FScene::VisibleMaskType visibilityMask,
        float3 cameraPosition, float3 cameraForward, uint8_t instancedStereoEyeCount) noexcept {

    /**
     * 确定通道类型（编译时常量）
     */
    constexpr bool isColorPass  = bool(commandTypeFlags & CommandTypeFlags::COLOR);
    constexpr bool isDepthPass  = bool(commandTypeFlags & CommandTypeFlags::DEPTH);
    /**
     * 静态断言：只支持颜色或深度通道，不支持同时两者
     */
    static_assert(isColorPass != isDepthPass, "only color or depth pass supported");

    /**
     * 提取额外标志
     */
    bool const depthContainsShadowCasters =
            bool(extraFlags & CommandTypeFlags::DEPTH_CONTAINS_SHADOW_CASTERS);  // 深度通道包含阴影投射者

    bool const depthFilterAlphaMaskedObjects =
            bool(extraFlags & CommandTypeFlags::DEPTH_FILTER_ALPHA_MASKED_OBJECTS);  // 深度通道过滤 Alpha 遮罩对象

    bool const filterTranslucentObjects =
            bool(extraFlags & CommandTypeFlags::FILTER_TRANSLUCENT_OBJECTS);  // 过滤半透明对象

    /**
     * 提取渲染标志
     */
    bool const hasShadowing =
            renderFlags & HAS_SHADOWING;  // 是否有阴影

    bool const viewInverseFrontFaces =
            renderFlags & HAS_INVERSE_FRONT_FACES;  // 视图是否反转正面

    bool const hasInstancedStereo =
            renderFlags & IS_INSTANCED_STEREOSCOPIC;  // 是否有实例化立体渲染

    bool const hasDepthClamp =
            renderFlags & HAS_DEPTH_CLAMP;  // 是否支持深度夹紧

    /**
     * 预计算相机位置与相机前向量的点积
     * 
     * 这用于优化距离计算，避免在循环中重复计算。
     */
    float const cameraPositionDotCameraForward = dot(cameraPosition, cameraForward);

    /**
     * 获取 SoA 数据指针
     * 
     * 使用 SoA（Structure of Arrays）布局以提高缓存局部性。
     */
    auto const* const UTILS_RESTRICT soaWorldAABBCenter = soa.data<FScene::WORLD_AABB_CENTER>();  // 世界 AABB 中心
    auto const* const UTILS_RESTRICT soaVisibility      = soa.data<FScene::VISIBILITY_STATE>();  // 可见性状态
    auto const* const UTILS_RESTRICT soaPrimitives      = soa.data<FScene::PRIMITIVES>();  // 图元数组
    auto const* const UTILS_RESTRICT soaSkinning        = soa.data<FScene::SKINNING_BUFFER>();  // 蒙皮缓冲区
    auto const* const UTILS_RESTRICT soaMorphing        = soa.data<FScene::MORPHING_BUFFER>();  // 变形缓冲区
    auto const* const UTILS_RESTRICT soaVisibilityMask  = soa.data<FScene::VISIBLE_MASK>();  // 可见性掩码
    auto const* const UTILS_RESTRICT soaInstanceInfo    = soa.data<FScene::INSTANCES>();  // 实例信息
    auto const* const UTILS_RESTRICT soaDescriptorSet   = soa.data<FScene::DESCRIPTOR_SET_HANDLE>();  // 描述符堆句柄

    /**
     * 命令模板（用于构建命令）
     */
    Command cmd;

    /**
     * 如果是深度通道，初始化默认光栅状态
     */
    if constexpr (isDepthPass) {
        cmd.info.materialVariant = variant;  // 材质变体
        cmd.info.rasterState = {};  // 清零光栅状态
        /**
         * 颜色写入：仅在拾取变体或 VSM 变体时启用
         */
        cmd.info.rasterState.colorWrite = Variant::isPickingVariant(variant) || Variant::isVSMVariant(variant);
        cmd.info.rasterState.depthWrite = true;  // 深度写入始终启用
        cmd.info.rasterState.depthFunc = RasterState::DepthFunc::GE;  // 深度测试函数：大于等于
        cmd.info.rasterState.alphaToCoverage = false;  // Alpha-to-Coverage 关闭
        cmd.info.rasterState.depthClamp = hasDepthClamp;  // 深度夹紧
    }

    /**
     * 遍历可见可渲染对象范围
     */
    for (uint32_t i = range.first; i < range.last; ++i) {
        /**
         * 检查此可渲染对象是否通过可见性掩码
         */
        // Check if this renderable passes the visibilityMask.
        if (UTILS_UNLIKELY(!(soaVisibilityMask[i] & visibilityMask))) {
            continue;  // 跳过不可见的对象
        }

        /**
         * 计算到相机的有符号距离
         * 
         * 从相机平面到对象中心的有符号距离。
         * 正距离表示在相机前方。
         * 一些中心在相机后方的对象仍然可能可见，所以它们的距离将为负
         * （这在阴影贴图中经常发生）。
         * 
         * 使用中心对于大型 AABB 不是很好。相反，我们可以尝试使用
         * 包围球上的最近点：
         *      d = soaWorldAABBCenter[i] - cameraPosition;
         *      d -= normalize(d) * length(soaWorldAABB[i].halfExtent);
         * 然而，这对于大型平面根本不起作用。
         * 
         * 下面的代码等价于：
         * float3 d = soaWorldAABBCenter[i] - cameraPosition;
         * float distance = dot(d, cameraForward);
         * 但节省了几条指令，因为部分数学计算在循环外完成。
         * 
         * 我们取反到相机的距离，以创建一个可以正确排序的位模式，这有效是因为：
         * - 正距离（现在是负的），由于浮点数表示，仍将按其绝对值排序。
         * - 负距离（现在是正的）将在所有其他对象之前排序，我们不太关心它们的顺序
         *   （即，相机后方的对象应该首先绘制吗？——不清楚，可能无关紧要）。
         *   这里，靠近相机（但在后方）的对象将首先绘制。
         * 
         * 保持数学顺序的替代方案如下：
         *   distanceBits ^= ((int32_t(distanceBits) >> 31) | 0x80000000u);
         */
        // Signed distance from camera plane to object's center. Positive distances are in front of
        // the camera. Some objects with a center behind the camera can still be visible
        // so their distance will be negative (this happens a lot for the shadow map).

        // Using the center is not very good with large AABBs. Instead, we can try to use
        // the closest point on the bounding sphere instead:
        //      d = soaWorldAABBCenter[i] - cameraPosition;
        //      d -= normalize(d) * length(soaWorldAABB[i].halfExtent);
        // However this doesn't work well at all for large planes.

        // Code below is equivalent to:
        // float3 d = soaWorldAABBCenter[i] - cameraPosition;
        // float distance = dot(d, cameraForward);
        // but saves a couple of instruction, because part of the math is done outside the loop.

        // We negate the distance to the camera in order to create a bit pattern that will
        // be sorted properly, this works because:
        // - positive distances (now negative), will still be sorted by their absolute value
        //   due to float representation.
        // - negative distances (now positive) will be sorted BEFORE everything else, and we
        //   don't care too much about their order (i.e. should objects far behind the camera
        //   be sorted first? -- unclear, and probably irrelevant).
        //   Here, objects close to the camera (but behind) will be drawn first.
        // An alternative that keeps the mathematical ordering is given here:
        //   distanceBits ^= ((int32_t(distanceBits) >> 31) | 0x80000000u);
        float const distance = -(dot(soaWorldAABBCenter[i], cameraForward) - cameraPositionDotCameraForward);
        /**
         * 将距离转换为位模式（用于排序键）
         */
        uint32_t const distanceBits = reinterpret_cast<uint32_t const&>(distance);

        /**
         * 计算每个图元的面绕序反转
         * 
         * 视图级别的反转与对象级别的反转进行异或运算。
         */
        // calculate the per-primitive face winding order inversion
        bool const inverseFrontFaces = viewInverseFrontFaces ^ soaVisibility[i].reversedWindingOrder;
        
        /**
         * 检查是否有变形和蒙皮
         */
        bool const hasMorphing = soaVisibility[i].morphing;
        bool const hasSkinning = soaVisibility[i].skinning;
        bool const hasSkinningOrMorphing = hasSkinning || hasMorphing;

        /**
         * 构建可渲染对象变体
         * 
         * 如果已经是 SSR 变体，SRE 位已经设置。
         */
        // if we are already an SSR variant, the SRE bit is already set
        static_assert(Variant::SPECIAL_SSR & Variant::SRE);
        Variant renderableVariant{ variant };

        /**
         * 我们不能同时有 SSR 和阴影（由构造保证）
         */
        // we can't have SSR and shadowing together by construction
        bool const isSsrVariant = Variant::isSSRVariant(variant);
        assert_invariant((isSsrVariant && !hasShadowing) || !isSsrVariant);
        
        /**
         * 设置阴影接收者变体（除非在 SSR 模式下）
         */
        if (!isSsrVariant) {
            // set the SRE variant, unless we're in SSR mode
            renderableVariant.setShadowReceiver(soaVisibility[i].receiveShadows && hasShadowing);
        }

        /**
         * 设置蒙皮变体
         */
        renderableVariant.setSkinning(hasSkinningOrMorphing);

        /**
         * 获取蒙皮和变形绑定信息
         */
        FRenderableManager::SkinningBindingInfo const& skinning = soaSkinning[i];
        FRenderableManager::MorphingBindingInfo const& morphing = soaMorphing[i];

        /**
         * 根据通道类型设置命令键和状态
         */
        if constexpr (isColorPass) {
            /**
             * 颜色通道：设置雾变体并设置通道类型
             */
            renderableVariant.setFog(soaVisibility[i].fog && Variant::isFogVariant(variant));
            cmd.key = uint64_t(Pass::COLOR);
        } else if constexpr (isDepthPass) {
            /**
             * 深度通道：设置通道类型、自定义命令标志、Z 桶（用于排序）
             */
            cmd.key = uint64_t(Pass::DEPTH);
            cmd.key |= uint64_t(CustomCommand::PASS);
            /**
             * Z 桶：使用距离的高 10 位进行分桶，用于深度排序
             */
            cmd.key |= makeField(distanceBits >> 22u, Z_BUCKET_MASK, Z_BUCKET_SHIFT);
            cmd.info.materialVariant.setSkinning(hasSkinningOrMorphing);  // 设置蒙皮变体
            cmd.info.rasterState.inverseFrontFaces = inverseFrontFaces;  // 反转正面
        }

        /**
         * 设置命令键的其他字段
         */
        cmd.key |= makeField(soaVisibility[i].priority, PRIORITY_MASK, PRIORITY_SHIFT);  // 优先级
        cmd.key |= makeField(soaVisibility[i].channel, CHANNEL_MASK, CHANNEL_SHIFT);  // 通道

        /**
         * 设置实例信息
         * 
         * 如果有实例缓冲区，使用实例缓冲区的索引；否则使用可渲染对象索引。
         */
        cmd.info.index = soaInstanceInfo[i].buffer ? soaInstanceInfo[i].buffer->getIndex() : i;
        cmd.info.hasHybridInstancing = bool(soaInstanceInfo[i].buffer);  // 是否有混合实例化
        cmd.info.instanceCount = soaInstanceInfo[i].count;  // 实例数量
        cmd.info.hasMorphing = bool(morphing.handle);  // 是否有变形
        cmd.info.hasSkinning = bool(skinning.handle);  // 是否有蒙皮

        /**
         * soaInstanceInfo[i].count 是用户请求的实例数量，用于手动或混合实例化。
         * 实例化立体渲染将实例数量乘以眼睛数量。
         */
        // soaInstanceInfo[i].count is the number of instances the user has requested, either for
        // manual or hybrid instancing. Instanced stereo multiplies the number of instances by the
        // eye count.
        if (hasInstancedStereo) {
            cmd.info.instanceCount *= instancedStereoEyeCount;  // 乘以眼睛数量
        }

        /**
         * 设置描述符堆句柄
         * 
         * soaDescriptorSet[i] 要么填充了公共描述符堆，要么填充了真正的每个可渲染对象的描述符堆，
         * 这取决于例如蒙皮/变形/实例化。
         */
        // soaDescriptorSet[i] is either populated with a common descriptor-set or truly with
        // a per-renderable one, depending on for e.g. skinning/morphing/instancing.
        cmd.info.dsh = soaDescriptorSet[i];

        /**
         * 始终设置蒙皮偏移量，即使蒙皮关闭，这不会造成任何成本。
         */
        // always set the skinningOffset, even when skinning is off, this doesn't cost anything.
        cmd.info.skinningOffset = soaSkinning[i].offset * sizeof(PerRenderableBoneUib::BoneData);

        /**
         * 检查是否为阴影投射者
         */
        const bool shadowCaster = soaVisibility[i].castShadows & hasShadowing;
        const bool writeDepthForShadowCasters = depthContainsShadowCasters & shadowCaster;

        /**
         * 获取可渲染对象的图元数组
         */
        Slice<const FRenderPrimitive> primitives = soaPrimitives[i];
        
        /**
         * 这是我们的热循环。它被编写为避免分支。
         * 修改此代码时，始终确保它保持高效。
         * 
         * 此循环遍历可渲染对象的所有图元，为每个图元生成命令。
         */
        /*
         * This is our hot loop. It's written to avoid branches.
         * When modifying this code, always ensure it stays efficient.
         */
        for (auto const& primitive: primitives) {
            /**
             * 获取图元的材质实例
             */
            FMaterialInstance const* const mi = primitive.getMaterialInstance();
            
            /**
             * 检查材质实例是否有效
             * 
             * 这可能发生，因为 RenderPrimitives 可以用空 MaterialInstance 初始化。
             * 在这种情况下，跳过该图元。
             */
            if (UTILS_UNLIKELY(!mi)) {
                // This can happen because RenderPrimitives can be initialized with
                // a null MaterialInstance. In this case, skip the primitive.
                /**
                 * 如果是颜色通道，需要跳过两个命令槽位（透明对象可能需要两次绘制）
                 */
                if constexpr (isColorPass) {
                    curr->key = uint64_t(Pass::SENTINEL);  // 标记为 SENTINEL
                    ++curr;
                }
                /**
                 * 跳过当前命令槽位
                 */
                curr->key = uint64_t(Pass::SENTINEL);  // 标记为 SENTINEL
                ++curr;
                continue;  // 跳过此图元
            }

            /**
             * TODO: 如果此图元既没有蒙皮也没有变形，我们应该禁用 SKN 变体。
             */
            // TODO: we should disable the SKN variant if this primitive doesn't have either
            //       skinning or morphing.

            /**
             * 获取材质并设置命令信息
             */
            FMaterial const* const ma = mi->getMaterial();
            cmd.info.mi = mi;  // 材质实例
            cmd.info.rph = primitive.getHwHandle();  // 渲染图元硬件句柄
            cmd.info.vbih = primitive.getVertexBufferInfoHandle();  // 顶点缓冲区信息句柄
            cmd.info.indexOffset = primitive.getIndexOffset();  // 索引偏移量
            cmd.info.indexCount = primitive.getIndexCount();  // 索引数量
            cmd.info.type = primitive.getPrimitiveType();  // 图元类型
            cmd.info.morphingOffset = primitive.getMorphingBufferOffset();  // 变形缓冲区偏移量
            
            /**
             * FIXME: 变形目标缓冲区
             * 
             * 注意：变形目标缓冲区的处理被注释掉了，可能需要后续实现。
             */
// FIXME: morphtarget buffer
//            cmd.info.morphTargetBuffer = morphing.morphTargetBuffer ?
//                    morphing.morphTargetBuffer->getHwHandle() : SamplerGroupHandle{};

            /**
             * 如果是颜色通道，设置颜色命令
             */
            if constexpr (isColorPass) {
                /**
                 * 设置颜色命令（包括混合模式、材质变体、光栅状态等）
                 */
                setupColorCommand(cmd, renderableVariant, mi,
                        inverseFrontFaces, hasDepthClamp);
                
                /**
                 * 检查是否为混合通道
                 */
                const bool blendPass = Pass(cmd.key & PASS_MASK) == Pass::BLENDED;
                
                /**
                 * 如果是混合通道，处理透明对象的排序和混合
                 */
                if (blendPass) {
                    /**
                     * TODO: 至少对于透明对象，AABB 应该是每个图元的，
                     *       但这会破坏"局部"混合顺序，它依赖于所有图元具有相同的 Z。
                     * 
                     * 混合通道：
                     *   这将为混合对象进行从后到前的排序，并在给定的 Z 值或全局范围内
                     *   遵循显式排序。
                     */
                    // TODO: at least for transparent objects, AABB should be per primitive
                    //       but that would break the "local" blend-order, which relies on
                    //       all primitives having the same Z
                    // blend pass:
                    //   This will sort back-to-front for blended, and honor explicit ordering
                    //   for a given Z value, or globally.
                    
                    /**
                     * 清除混合顺序和距离掩码
                     */
                    cmd.key &= ~BLEND_ORDER_MASK;  // 清除混合顺序
                    cmd.key &= ~BLEND_DISTANCE_MASK;  // 清除混合距离
                    
                    /**
                     * 写入距离（取反，用于从后到前排序）
                     */
                    // write the distance
                    cmd.key |= makeField(~distanceBits,
                            BLEND_DISTANCE_MASK, BLEND_DISTANCE_SHIFT);
                    
                    /**
                     * 如果启用了全局排序，清除距离
                     */
                    // clear the distance if global ordering is enabled
                    cmd.key &= ~select(primitive.isGlobalBlendOrderEnabled(),
                            BLEND_DISTANCE_MASK);
                    
                    /**
                     * 写入混合顺序
                     */
                    // write blend order
                    cmd.key |= makeField(primitive.getBlendOrder(),
                            BLEND_ORDER_MASK, BLEND_ORDER_SHIFT);


                    /**
                     * 获取透明模式
                     */
                    const TransparencyMode mode = mi->getTransparencyMode();

                    /**
                     * 处理透明对象，两种技术：
                     *
                     *   - TWO_PASSES_ONE_SIDE: 先在深度缓冲区中绘制正面，
                     *     然后在颜色缓冲区中使用深度测试绘制正面。
                     *     在此模式下，我们实际上不更改用户的剔除模式。
                     *
                     *   - TWO_PASSES_TWO_SIDES: 先绘制背面，
                     *     然后绘制正面，两者都在颜色缓冲区中。
                     *     在此模式下，我们覆盖用户的剔除模式。
                     */
                    // handle transparent objects, two techniques:
                    //
                    //   - TWO_PASSES_ONE_SIDE: draw the front faces in the depth buffer then
                    //     front faces with depth test in the color buffer.
                    //     In this mode we actually do not change the user's culling mode
                    //
                    //   - TWO_PASSES_TWO_SIDES: draw back faces first,
                    //     then front faces, both in the color buffer.
                    //     In this mode, we override the user's culling mode.

                    /**
                     * TWO_PASSES_TWO_SIDES: 此命令将在第二个发出，绘制正面
                     */
                    // TWO_PASSES_TWO_SIDES: this command will be issued 2nd, draw front faces
                    cmd.info.rasterState.culling =
                            (mode == TransparencyMode::TWO_PASSES_TWO_SIDES) ?
                            CullingMode::BACK : cmd.info.rasterState.culling;  // 剔除背面

                    /**
                     * 创建第二个命令的键（用于双通道透明）
                     */
                    uint64_t key = cmd.key;

                    /**
                     * 在下一个命令之后绘制此命令（标记为第二个通道）
                     */
                    // draw this command AFTER THE NEXT ONE
                    key |= makeField(1, BLEND_TWO_PASS_MASK, BLEND_TWO_PASS_SHIFT);

                    /**
                     * 修正 TransparencyMode::DEFAULT -- 即取消命令
                     */
                    // correct for TransparencyMode::DEFAULT -- i.e. cancel the command
                    key |= select(mode == TransparencyMode::DEFAULT);

                    /**
                     * 如果要求过滤半透明对象，取消命令
                     */
                    // cancel command if asked to filter translucent objects
                    key |= select(filterTranslucentObjects);

                    /**
                     * 如果正面和背面都被剔除，取消命令
                     */
                    // cancel command if both front and back faces are culled
                    key |= select(mi->getCullingMode() == CullingMode::FRONT_AND_BACK);

                    /**
                     * 写入第二个命令（正面）
                     */
                    *curr = cmd;
                    curr->key = key;
                    ++curr;

                    /**
                     * TWO_PASSES_TWO_SIDES: 此命令将首先发出，绘制背面（即剔除正面）
                     */
                    // TWO_PASSES_TWO_SIDES: this command will be issued first, draw back sides (i.e. cull front)
                    cmd.info.rasterState.culling =
                            (mode == TransparencyMode::TWO_PASSES_TWO_SIDES) ?
                            CullingMode::FRONT : cmd.info.rasterState.culling;  // 剔除正面

                    /**
                     * TWO_PASSES_ONE_SIDE: 此命令将首先发出，仅在深度缓冲区中绘制（背面）
                     */
                    // TWO_PASSES_ONE_SIDE: this command will be issued first, draw (back side) in depth buffer only
                    cmd.info.rasterState.depthWrite |=  select(mode == TransparencyMode::TWO_PASSES_ONE_SIDE);  // 启用深度写入
                    cmd.info.rasterState.colorWrite &= ~select(mode == TransparencyMode::TWO_PASSES_ONE_SIDE);  // 禁用颜色写入
                    cmd.info.rasterState.depthFunc =
                            (mode == TransparencyMode::TWO_PASSES_ONE_SIDE) ?
                            SamplerCompareFunc::GE : cmd.info.rasterState.depthFunc;  // 深度测试函数
                } else {
                    /**
                     * 颜色通道（不透明对象）：
                     * 
                     * 这将按 Z 对对象进行分桶，从前到后，然后在每个桶中按材质排序。
                     * 我们使用距离的高 10 位，这通过其 log2 对深度进行分桶，
                     * 并在每个桶中分成 4 个线性块。
                     */
                    // color pass:
                    // This will bucket objects by Z, front-to-back and then sort by material
                    // in each buckets. We use the top 10 bits of the distance, which
                    // bucketizes the depth by its log2 and in 4 linear chunks in each bucket.
                    cmd.key &= ~Z_BUCKET_MASK;  // 清除 Z 桶掩码
                    cmd.key |= makeField(distanceBits >> 22u, Z_BUCKET_MASK, Z_BUCKET_SHIFT);  // 设置 Z 桶
                }

                /**
                 * 写入命令（不透明对象或第一个透明通道）
                 */
                *curr = cmd;
                
                /**
                 * 如果正面和背面都被剔除，取消命令
                 */
                // cancel command if both front and back faces are culled
                curr->key |= select(mi->getCullingMode() == CullingMode::FRONT_AND_BACK);

            /**
             * 如果是深度通道，处理深度渲染
             */
            } else if constexpr (isDepthPass) {
                /**
                 * 获取剔除模式（如果有阴影，使用阴影剔除模式；否则使用普通剔除模式）
                 */
                const CullingMode cullingMode = hasShadowing ? mi->getShadowCullingMode() : mi->getCullingMode();
                
                /**
                 * 获取光栅状态和透明模式
                 */
                const RasterState rs = ma->getRasterState();
                const TransparencyMode mode = mi->getTransparencyMode();
                const BlendingMode blendingMode = ma->getBlendingMode();
                
                /**
                 * 检查是否为半透明对象
                 */
                const bool translucent = (blendingMode != BlendingMode::OPAQUE
                        && blendingMode != BlendingMode::MASKED);
                
                /**
                 * 检查是否为拾取变体
                 */
                const bool isPickingVariant = Variant::isPickingVariant(variant);

                /**
                 * 添加排序键（已经全部设置好，可以直接进行或运算）
                 */
                cmd.key |= mi->getSortingKey(); // already all set-up for direct or'ing
                
                /**
                 * 设置剔除模式
                 */
                cmd.info.rasterState.culling = cullingMode;

                /**
                 * 设置深度写入
                 * 
                 * FIXME: writeDepthForShadowCasters 是否应该优先于 mi->getDepthWrite()？
                 * 
                 * 深度写入在以下情况下启用：
                 * - 材质实例启用了深度写入
                 * - 透明模式为 TWO_PASSES_ONE_SIDE
                 * - 是拾取变体
                 * - 不是半透明对象（如果启用了过滤半透明对象）
                 * - 不是 Alpha 遮罩对象（如果启用了过滤 Alpha 遮罩对象）
                 * - 或者是阴影投射者（如果深度通道包含阴影投射者）
                 */
                // FIXME: should writeDepthForShadowCasters take precedence over mi->getDepthWrite()?
                cmd.info.rasterState.depthWrite = (1 // only keep bit 0
                        & (mi->isDepthWriteEnabled() | (mode == TransparencyMode::TWO_PASSES_ONE_SIDE)
                                                     | isPickingVariant)
                                                   & !(filterTranslucentObjects & translucent)
                                                   & !(depthFilterAlphaMaskedObjects & rs.alphaToCoverage))
                                                  | writeDepthForShadowCasters;

                /**
                 * 写入命令
                 */
                *curr = cmd;
                
                /**
                 * 如果正面和背面都被剔除，取消命令
                 */
                // cancel command if both front and back faces are culled
                curr->key |= select(cullingMode == CullingMode::FRONT_AND_BACK);
            }

            /**
             * 移动到下一个命令槽位
             */
            ++curr;
        }
    }
    
    /**
     * 返回更新后的命令指针
     */
    return curr;
}

/**
 * 更新累积的图元数量
 * 
 * 计算每个可渲染对象的累积图元数量。
 * 这对于确定命令缓冲区中的偏移量很有用。
 * 
 * 算法：
 * - 对于每个可渲染对象 i，summedPrimitiveCount[i] 存储了
 *   所有索引 < i 的可渲染对象的图元总数。
 * - summedPrimitiveCount[vr.last] 存储了所有可见可渲染对象的图元总数。
 * 
 * @param renderableData 可渲染数据（SoA 结构）
 * @param vr 可见可渲染对象的范围
 */
void RenderPass::updateSummedPrimitiveCounts(
        FScene::RenderableSoa& renderableData, Range<uint32_t> vr) noexcept {
    /**
     * 获取图元数组指针
     */
    auto const* const UTILS_RESTRICT primitives = renderableData.data<FScene::PRIMITIVES>();
    
    /**
     * 获取累积图元数量数组指针
     */
    uint32_t* const UTILS_RESTRICT summedPrimitiveCount = renderableData.data<FScene::SUMMED_PRIMITIVE_COUNT>();
    
    /**
     * 累积图元数量
     */
    uint32_t count = 0;
    
    /**
     * 遍历所有可见可渲染对象
     */
    for (uint32_t const i : vr) {
        /**
         * 存储当前累积数量（即索引 < i 的所有对象的图元总数）
         */
        summedPrimitiveCount[i] = count;
        
        /**
         * 累加当前对象的图元数量
         */
        count += primitives[i].size();
    }
    
    /**
     * 存储最终累积数量（所有可见对象的图元总数）
     * 
     * 我们保证在 vr.last 处有足够的空间。
     */
    // we're guaranteed to have enough space at the end of vr
    summedPrimitiveCount[vr.last] = count;
}

// ------------------------------------------------------------------------------------------------

/**
 * 覆盖多边形偏移
 * 
 * 设置用于所有绘制调用的多边形偏移。
 * 如果 polygonOffset 为 nullptr，则禁用覆盖。
 * 
 * @param polygonOffset 多边形偏移指针（nullptr 表示禁用覆盖）
 */
void RenderPass::Executor::overridePolygonOffset(PolygonOffset const* polygonOffset) noexcept {
    /**
     * 设置覆盖标志并保存多边形偏移值
     * 
     * 注意：在 if 条件中使用赋值是故意的，用于同时设置标志和值。
     */
    if ((mPolygonOffsetOverride = (polygonOffset != nullptr))) { // NOLINT(*-assignment-in-if-condition)
        mPolygonOffset = *polygonOffset;  // 复制多边形偏移值
    }
}

/**
 * 覆盖裁剪矩形
 * 
 * 设置用于所有绘制调用的裁剪矩形。
 * 
 * @param scissor 裁剪矩形视口
 */
void RenderPass::Executor::overrideScissor(backend::Viewport const& scissor) noexcept {
    mScissorOverride = true;  // 启用覆盖
    mScissor = scissor;  // 保存裁剪矩形
}

/**
 * 执行命令（无参数版本）
 * 
 * 执行所有命令。
 * 
 * @param engine 引擎常量引用
 * @param driver 驱动 API 引用
 */
void RenderPass::Executor::execute(FEngine const& engine, DriverApi& driver) const noexcept {
    /**
     * 调用完整版本，执行所有命令
     */
    execute(engine, driver, mCommands.begin(), mCommands.end());
}

/**
 * 应用裁剪视口
 * 
 * 将裁剪矩形相对于裁剪视口进行偏移和裁剪。
 * 
 * 算法：
 * 1. 将裁剪视口和裁剪矩形转换为 {left, bottom, right, top} 格式
 * 2. 将裁剪矩形相对于裁剪视口进行偏移
 * 3. 将裁剪矩形裁剪到裁剪视口范围内
 * 4. 裁剪到正 int32_t 范围内
 * 5. 转换回 Viewport 格式
 * 
 * 注意：clang 会对此函数进行向量化优化！
 * 
 * @param scissorViewport 裁剪视口
 * @param scissor 裁剪矩形
 * @return 应用偏移和裁剪后的裁剪矩形
 */
UTILS_NOINLINE // no need to be inlined
backend::Viewport RenderPass::Executor::applyScissorViewport(
        backend::Viewport const& scissorViewport, backend::Viewport const& scissor) noexcept {
    /**
     * 裁剪矩形已设置，我们需要应用偏移/裁剪
     * 
     * 注意：clang 会对此进行向量化！
     */
    // scissor is set, we need to apply the offset/clip
    // clang vectorizes this!
    constexpr int64_t maxvali = std::numeric_limits<int32_t>::max();  // int32_t 最大值

    /**
     * 我们在下面使用 64 位进行所有偏移/裁剪计算，以避免溢出
     */
    // we do all the offsetting/clipping math in 64 bits below to avoid overflows
    struct {
        int64_t l;  // 左
        int64_t b;  // 底
        int64_t r;  // 右
        int64_t t;  // 顶
    } svp, s;  // svp = scissor viewport, s = scissor

    /**
     * 将裁剪视口转换为 {left, bottom, right, top} 格式
     */
    // convert scissorViewport to {left,bottom,right,top} format
    svp.l = scissorViewport.left;
    svp.b = scissorViewport.bottom;
    svp.r = svp.l + scissorViewport.width;
    svp.t = svp.b + scissorViewport.height;

    /**
     * 将裁剪矩形转换为 {left, bottom, right, top} 格式，
     * 并相对于裁剪视口的 left, bottom 进行偏移
     */
    // convert scissor to {left,bottom,right,top} format and offset by scissorViewport's left,bottom
    s.l = svp.l + scissor.left;
    s.b = svp.b + scissor.bottom;
    s.r = s.l + scissor.width;
    s.t = s.b + scissor.height;

    /**
     * 裁剪到裁剪视口范围内
     */
    // clip to the scissorViewport
    s.l = std::max(s.l, svp.l);
    s.b = std::max(s.b, svp.b);
    s.r = std::min(s.r, svp.r);
    s.t = std::min(s.t, svp.t);

    /**
     * 裁剪到正 int32_t 范围内
     */
    // clip to positive int32_t
    s.l = std::max(s.l, int64_t(0));
    s.b = std::max(s.b, int64_t(0));
    s.r = std::min(s.r, maxvali);
    s.t = std::min(s.t, maxvali);

    /**
     * 确保右 >= 左，顶 >= 底
     */
    assert_invariant(s.r >= s.l && s.t >= s.b);

    /**
     * 转换回 Viewport 格式
     */
    // convert back to Viewport format
    return { int32_t(s.l), int32_t(s.b), uint32_t(s.r - s.l), uint32_t(s.t - s.b) };
}

/**
 * 执行命令（完整版本）
 * 
 * 执行命令范围内的所有命令。
 * 
 * 实现细节：
 * - 使用批处理来管理命令缓冲区容量
 * - 跟踪当前绑定的管线状态、图元句柄等，避免重复绑定
 * - 处理裁剪矩形、多边形偏移等覆盖
 * - 处理材质实例、描述符堆、管线的绑定
 * - 处理蒙皮、变形等特殊情况
 * 
 * 性能优化：
 * - 只在状态改变时更新绑定
 * - 使用批处理来减少命令缓冲区分配
 * - 内层循环经过高度优化，修改时需谨慎
 * 
 * @param engine 引擎常量引用
 * @param driver 驱动 API 引用
 * @param first 命令范围起始指针
 * @param last 命令范围结束指针
 */
UTILS_NOINLINE // no need to be inlined
void RenderPass::Executor::execute(FEngine const& engine, DriverApi& driver,
        Command const* first, Command const* last) const noexcept {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);
    FILAMENT_TRACING_CONTEXT(FILAMENT_TRACING_CATEGORY_FILAMENT);

    /**
     * 获取命令缓冲区最小容量
     */
    size_t const capacity = engine.getMinCommandBufferSize();
    
    /**
     * 获取循环缓冲区引用
     */
    CircularBuffer const& circularBuffer = driver.getCircularBuffer();

    /**
     * 如果有命令需要执行
     */
    if (first != last) {
        FILAMENT_TRACING_VALUE(FILAMENT_TRACING_CATEGORY_FILAMENT, "commandCount", last - first);

        /**
         * 裁剪矩形与渲染通道关联，因此跟踪可以是局部的。
         * 
         * 初始化当前裁剪矩形为最大范围（无裁剪）。
         */
        // The scissor rectangle is associated to a render pass, so the tracking can be local.
        backend::Viewport currentScissor{ 0, 0, INT32_MAX, INT32_MAX };
        
        /**
         * 检查是否有裁剪覆盖或裁剪视口
         */
        bool const hasScissorOverride = mScissorOverride;
        bool const hasScissorViewport = mHasScissorViewport;
        
        /**
         * 如果有裁剪覆盖或裁剪视口，设置初始裁剪矩形
         */
        if (UTILS_UNLIKELY(hasScissorViewport || hasScissorOverride)) {
            /**
             * 我们不应该同时有覆盖和裁剪视口
             */
            // we should never have both an override and scissor-viewport
            assert_invariant(!hasScissorViewport || !hasScissorOverride);
            currentScissor = mScissor;  // 设置当前裁剪矩形
            driver.scissor(mScissor);  // 应用裁剪矩形
        }

        /**
         * 确定是否使用 VSM 描述符堆布局
         * 
         * 如果我们有 mColorPassDescriptorSet，我们需要使用它的 "VSM" 概念来选择
         * 描述符堆布局。材质总是提供两者。
         * 如果我们没有 mColorPassDescriptorSet，这无关紧要，因为布局仅通过变体选择。
         */
        // If we have a mColorPassDescriptorSet, we need to use its idea of "VSM" to select
        // the descriptor set layout. Materials always offer both.
        // If we don't have a mColorPassDescriptorSet, it doesn't matter because the layout
        // are chosen via the variant only.
        bool const useVsmDescriptorSetLayout =
                mColorPassDescriptorSet ? mColorPassDescriptorSet->isVSM() : false;

        /**
         * 检查是否有多边形偏移覆盖
         */
        bool const polygonOffsetOverride = mPolygonOffsetOverride;
        
        /**
         * 初始化管线状态（使用多边形偏移覆盖）
         */
        PipelineState pipeline{
                // initialize with polygon offset override
                .polygonOffset = mPolygonOffset,  // 多边形偏移
        };

        /**
         * 设置每个可渲染对象的描述符堆布局
         */
        pipeline.pipelineLayout.setLayout[+DescriptorSetBindingPoints::PER_RENDERABLE] =
                engine.getPerRenderableDescriptorSetLayout().getHandle();

        /**
         * 当前绑定的管线状态（用于状态跟踪，避免重复绑定）
         */
        PipelineState currentPipeline{};
        
        /**
         * 当前绑定的图元句柄（用于状态跟踪，避免重复绑定）
         */
        Handle<HwRenderPrimitive> currentPrimitiveHandle{};

        /**
         * 当前绑定的材质实例和材质（用于状态跟踪）
         */
        FMaterialInstance const* UTILS_RESTRICT mi = nullptr;
        FMaterial const* UTILS_RESTRICT ma = nullptr;
        
        /**
         * 自定义命令数组指针
         */
        auto const* UTILS_RESTRICT pCustomCommands = mCustomCommands.data();

        /**
         * 单个 Command 在 CircularBuffer 中占用的最大空间。
         * 
         * 当下面的内层循环添加 DriverApi 命令或当我们更改 CommandStream 协议时，
         * 必须重新评估此值。当前最大值为 248 字节。
         * 
         * 批处理大小通过添加每个绘制调用可能发出的所有命令的大小来计算：
         */
        // Maximum space occupied in the CircularBuffer by a single `Command`. This must be
        // reevaluated when the inner loop below adds DriverApi commands or when we change the
        // CommandStream protocol. Currently, the maximum is 248 bytes.
        // The batch size is calculated by adding the size of all commands that can possibly be
        // emitted per draw call:
        constexpr size_t maxCommandSizeInBytes =
                sizeof(COMMAND_TYPE(scissor)) +  // 裁剪
                sizeof(COMMAND_TYPE(bindDescriptorSet)) +  // 绑定描述符堆（每个视图）
                sizeof(COMMAND_TYPE(bindDescriptorSet)) +  // 绑定描述符堆（每个材质）
                sizeof(COMMAND_TYPE(bindPipeline)) +  // 绑定管线
                sizeof(COMMAND_TYPE(bindRenderPrimitive)) +  // 绑定渲染图元
                sizeof(COMMAND_TYPE(bindDescriptorSet)) + backend::CustomCommand::align(sizeof(NoopCommand) + 8) +  // 绑定描述符堆（每个可渲染对象）
                sizeof(COMMAND_TYPE(setPushConstant)) +  // 设置推送常量
                sizeof(COMMAND_TYPE(draw2));  // 绘制

        /**
         * 可以发出并保证适合当前 CircularBuffer 分配的命令数量。
         * 
         * 实际上，我们会有大量的余量，特别是如果不使用蒙皮和变形。
         * 使用 2 MiB 缓冲区（默认值），一个批次是 6553 个命令（即绘制调用）。
         */
        // Number of Commands that can be issued and guaranteed to fit in the current
        // CircularBuffer allocation. In practice, we'll have tons of headroom especially if
        // skinning and morphing aren't used. With a 2 MiB buffer (the default) a batch is
        // 6553 commands (i.e. draw calls).
        size_t const batchCommandCount = capacity / maxCommandSizeInBytes;
        
        /**
         * 批处理循环：将命令分成批次处理
         */
        while(first != last) {
            /**
             * 计算当前批次的结束位置
             */
            Command const* const batchLast = std::min(first + batchCommandCount, last);

            /**
             * 实际需要写入的命令数量（可能小于 batchCommandCount）
             */
            // actual number of commands we need to write (can be smaller than batchCommandCount)
            size_t const commandCount = batchLast - first;
            size_t const commandSizeInBytes = commandCount * maxCommandSizeInBytes;

            /**
             * 检查我们是否有足够的容量来写入这些 commandCount 命令，如果没有，
             * 请求一个新的 CircularBuffer 分配，大小为 `capacity` 字节。
             */
            // check we have enough capacity to write these commandCount commands, if not,
            // request a new CircularBuffer allocation of `capacity` bytes.
            if (UTILS_UNLIKELY(circularBuffer.getUsed() > capacity - commandSizeInBytes)) {
                /**
                 * FIXME: 最终我们不能在这里刷新，因为这将是辅助命令缓冲区。
                 *        我们需要另一个解决方案来处理溢出。
                 */
                // FIXME: eventually we can't flush here because this will be a secondary
                //        command buffer. We will need another solution for overflows.
                const_cast<FEngine&>(engine).flush();  // 刷新引擎，分配新的缓冲区
            }

            /**
             * 将 first 减 1，因为内层循环使用 ++first（先递增再使用）
             */
            first--;
            
            /**
             * 内层循环：处理当前批次中的每个命令
             * 
             * 注意：修改下面的代码时要小心，这是热内层循环。
             */
            while (++first != batchLast) {
                /**
                 * 确保命令不是 SENTINEL
                 */
                assert_invariant(first->key != uint64_t(Pass::SENTINEL));

                /**
                 * 注意：修改下面的代码时要小心，这是热内层循环。
                 */
                /*
                 * Be careful when changing code below, this is the hot inner-loop
                 */

                /**
                 * 处理自定义命令
                 * 
                 * 如果命令不是普通通道命令，则执行自定义命令。
                 */
                if (UTILS_UNLIKELY((first->key & CUSTOM_MASK) != uint64_t(CustomCommand::PASS))) {
                    /**
                     * 自定义命令可能会更改当前绑定的材质实例，因此重置
                     */
                    mi = nullptr; // custom command could change the currently bound MaterialInstance
                    
                    /**
                     * 从命令键中提取自定义命令索引
                     */
                    uint32_t const index = (first->key & CUSTOM_INDEX_MASK) >> CUSTOM_INDEX_SHIFT;
                    assert_invariant(index < mCustomCommands.size());
                    
                    /**
                     * 执行自定义命令
                     */
                    pCustomCommands[index]();
                    
                    /**
                     * 重置当前状态（自定义命令可能改变了状态）
                     */
                    currentPipeline = {};
                    currentPrimitiveHandle ={};
                    continue;  // 跳过后续处理
                }

                /**
                 * 检查图元句柄是否有效
                 * 
                 * 如果可渲染对象没有设置几何体，图元句柄可能无效。
                 */
                // primitiveHandle may be invalid if no geometry was set on the renderable.
                if (UTILS_UNLIKELY(!first->info.rph)) {
                    continue;  // 跳过无效图元
                }

                /**
                 * 获取图元信息并设置管线状态
                 */
                // per-renderable uniform
                PrimitiveInfo const info = first->info;
                pipeline.rasterState = info.rasterState;  // 光栅状态
                pipeline.vertexBufferInfo = info.vbih;  // 顶点缓冲区信息句柄
                pipeline.primitiveType = info.type;  // 图元类型
                assert_invariant(pipeline.vertexBufferInfo);  // 确保顶点缓冲区信息有效

                /**
                 * 检查材质实例是否改变
                 * 
                 * 如果材质实例改变，需要更新材质、裁剪矩形、多边形偏移、
                 * 模板状态、描述符堆布局等。
                 */
                if (UTILS_UNLIKELY(mi != info.mi)) {
                    /**
                     * 这总是在第一次时被采用
                     */
                    // this is always taken the first time
                    assert_invariant(info.mi);

                    /**
                     * 更新材质实例和材质
                     */
                    mi = info.mi;
                    ma = mi->getMaterial();

                    /**
                     * 应用裁剪矩形
                     * 
                     * 如果我们有裁剪覆盖，材质实例和裁剪视口被忽略（通常用于阴影贴图）。
                     */
                    // if we have the scissor override, the material instance and scissor-viewport
                    // are ignored (typically used for shadow maps).
                    if (!hasScissorOverride) {
                        /**
                         * 应用材质实例的裁剪矩形
                         */
                        // apply the MaterialInstance scissor
                        backend::Viewport scissor = mi->getScissor();
                        if (hasScissorViewport) {
                            /**
                             * 如果有裁剪视口，应用它
                             */
                            // apply the scissor viewport if any
                            scissor = applyScissorViewport(mScissor, scissor);
                        }
                        /**
                         * 如果裁剪矩形改变，更新并应用
                         */
                        if (scissor != currentScissor) {
                            currentScissor = scissor;
                            driver.scissor(scissor);
                        }
                    }

                    /**
                     * 设置多边形偏移（如果没有覆盖）
                     */
                    if (UTILS_LIKELY(!polygonOffsetOverride)) {
                        pipeline.polygonOffset = mi->getPolygonOffset();
                    }
                    
                    /**
                     * 设置模板状态
                     */
                    pipeline.stencilState = mi->getStencilState();

                    /**
                     * 设置每个视图的描述符堆布局
                     * 
                     * 每个材质都有自己的每个视图描述符堆布局版本，
                     * 因为它取决于材质特性（例如，有光照/无光照）。
                     * 
                     * TODO: 问题：Variant::isValidDepthVariant(info.materialVariant) 和
                     *       Variant::isSSRVariant(info.materialVariant) 是常量吗？
                     *       如果是，我们可以预计算 ma->getPerViewDescriptorSetLayout()。
                     */
                    // Each material has its own version of the per-view descriptor-set layout,
                    // because it depends on the material features (e.g. lit/unlit)
                    // TODO: QUESTION: are
                    //      Variant::isValidDepthVariant(info.materialVariant) and
                    //      Variant::isSSRVariant(info.materialVariant)
                    //      constant? If so we could precompute ma->getPerViewDescriptorSetLayout()
                    pipeline.pipelineLayout.setLayout[+DescriptorSetBindingPoints::PER_VIEW] =
                            ma->getPerViewDescriptorSetLayout(info.materialVariant,
                                    useVsmDescriptorSetLayout).getHandle();

                    /**
                     * 设置每个材质的描述符堆布局
                     * 
                     * 每个材质都有一个每个材质的描述符堆布局，它编码材质的参数（UBO 和采样器）。
                     */
                    // Each material has a per-material descriptor-set layout which encodes the
                    // material's parameters (ubo and samplers)
                    pipeline.pipelineLayout.setLayout[+DescriptorSetBindingPoints::PER_MATERIAL] =
                            ma->getDescriptorSetLayout(info.materialVariant).getHandle();

                    /**
                     * 绑定每个视图的描述符堆
                     * 
                     * 如果我们有 ColorPassDescriptorSet，我们使用它来绑定每个视图的描述符堆
                     * （理想情况下，仅在它改变时）。
                     * 如果我们没有，这意味着描述符堆已经绑定，我们从材质上面获得的布局应该匹配。
                     * 这对于我们有已知的每个视图描述符堆布局的情况是成立的，
                     * 例如：后处理、阴影贴图、SSR 和结构通道。
                     */
                    // If we have a ColorPassDescriptorSet we use it to bind the per-view
                    // descriptor-set (ideally only if it changed).
                    // If we don't, it means the descriptor-set is already bound and the layout we
                    // got from the material above should match. This is the case for situations
                    // where we have a known per-view descriptor-set layout,
                    // e.g.: postfx, shadow-maps, ssr and structure passes.
                    if (mColorPassDescriptorSet) {
                        /**
                         * 处理后处理材质
                         * 
                         * 可能在这里获得后处理材质（尽管技术上还不是公共 API，
                         * 但它被 IBLPrefilterLibrary 使用）。
                         * 理想情况下，我们会有更正式的计算 API。
                         * 在这种情况下，我们需要设置后处理描述符堆。
                         */
                        if (UTILS_UNLIKELY(ma->getMaterialDomain() == MaterialDomain::POST_PROCESS)) {
                            // It is possible to get a post-process material here (even though it's
                            // not technically a public API yet, it is used by the IBLPrefilterLibrary.
                            // Ideally we would have a more formal compute API). In this case, we need
                            // to set the post-process descriptor-set.
                            engine.getPostProcessManager().bindPostProcessDescriptorSet(driver);
                        } else {
                            /**
                             * 我们有 ColorPassDescriptorSet，我们需要通过它来绑定
                             * 每个视图的描述符堆，因为它的布局可能根据材质而改变。
                             */
                            // We have a ColorPassDescriptorSet, we need to go through it for binding
                            // the per-view descriptor-set because its layout can change based on the
                            // material.
                            mColorPassDescriptorSet->bind(driver, ma->getPerViewLayoutIndex());
                        }
                    } else {
                        /**
                         * 如果我们在这里，这意味着每个视图的描述符堆是常量且已设置。
                         * 这对于后处理、SSR、结构和阴影通道是成立的。
                         * 所有这些通道都使用静态描述符堆布局（尽管每个通道可能不同）。
                         * 特别是，每个视图的 UBO 必须与所有材质域兼容。
                         * 这对于后处理和 SSR 是成立的（由构造保证）。
                         * 然而，阴影和结构有自己的 UBO，但其内容（必须）与
                         * POST_PROCESS 和 COMPUTE 材质兼容。
                         */
                        // if we're here it means the per-view descriptor set is constant and
                        // already set. This will be the case for postfx, ssr, structure and
                        // shadow passes. All these passes use a static descriptor set layout
                        // (albeit potentially different for each pass). In particular the
                        // per-view UBO must be compatible for all material domains.
                        // This is the case by construction for postfx, ssr. However, shadows
                        // and structure have their own UBO, but it's content is (must be)
                        // compatible with POST_PROCESS and COMPUTE materials.
                    }

                    /**
                     * 绑定材质实例的描述符堆
                     * 
                     * 每个材质实例都有自己的描述符堆。这绑定它。
                     */
                    // Each MaterialInstance has its own descriptor set. This binds it.
                    mi->use(driver, info.materialVariant);
                }

                /**
                 * 确保材质有效
                 */
                assert_invariant(ma);
                
                /**
                 * 获取着色器程序
                 */
                pipeline.program = ma->getProgram(info.materialVariant);

                /**
                 * 检查管线状态是否改变
                 * 
                 * 如果管线状态改变，更新并绑定新管线。
                 */
                if (UTILS_UNLIKELY(memcmp(&pipeline, &currentPipeline, sizeof(PipelineState)) != 0)) {
                    currentPipeline = pipeline;  // 更新当前管线状态
                    driver.bindPipeline(pipeline);  // 绑定管线
                }

                /**
                 * 检查图元句柄是否改变
                 * 
                 * 如果图元句柄改变，更新并绑定新图元。
                 */
                if (UTILS_UNLIKELY(info.rph != currentPrimitiveHandle)) {
                    currentPrimitiveHandle = info.rph;  // 更新当前图元句柄
                    driver.bindRenderPrimitive(info.rph);  // 绑定渲染图元
                }

                /**
                 * 绑定每个可渲染对象的统一缓冲区
                 * 
                 * 没有必要尝试跳过此命令，因为后端已经这样做了。
                 */
                // Bind per-renderable uniform block. There is no need to attempt to skip this command
                // because the backends already do this.
                uint32_t const offset = info.index * sizeof(PerRenderableData);  // 计算偏移量

                /**
                 * 确保描述符堆句柄有效
                 */
                assert_invariant(info.dsh);
                
                /**
                 * 绑定每个可渲染对象的描述符堆
                 * 
                 * 偏移量包含：
                 * - offset: 每个可渲染对象数据的偏移量
                 * - info.skinningOffset: 蒙皮数据的偏移量
                 */
                driver.bindDescriptorSet(info.dsh,
                        +DescriptorSetBindingPoints::PER_RENDERABLE,
                        {{ offset, info.skinningOffset }, driver});

                /**
                 * 如果有变形，设置推送常量
                 */
                if (UTILS_UNLIKELY(info.hasMorphing)) {
                    driver.setPushConstant(ShaderStage::VERTEX,
                            +PushConstantIds::MORPHING_BUFFER_OFFSET, int32_t(info.morphingOffset));
                }

                /**
                 * 发出绘制命令
                 */
                driver.draw2(info.indexOffset, info.indexCount, info.instanceCount);
            }
        }

        /**
         * 如果剩余空间小于容量的一半，我们立即刷新，
         * 以便为可能稍后到来的命令留出一些余量。
         */
        // If the remaining space is less than half the capacity, we flush right away to
        // allow some headroom for commands that might come later.
        if (UTILS_UNLIKELY(circularBuffer.getUsed() > capacity / 2)) {
            /**
             * FIXME: 最终我们不能在这里刷新，因为这将是辅助命令缓冲区。
             */
            // FIXME: eventually we can't flush here because this will be a secondary
            //        command buffer.
            const_cast<FEngine&>(engine).flush();  // 刷新引擎
        }
    }
}

// ------------------------------------------------------------------------------------------------

/**
 * Executor 构造函数（从 RenderPass 创建）
 * 
 * 从渲染通道创建执行器，复制命令范围和共享资源。
 * 
 * @param pass 渲染通道常量引用
 * @param b 命令范围起始指针
 * @param e 命令范围结束指针
 */
RenderPass::Executor::Executor(RenderPass const& pass, Command const* b, Command const* e) noexcept
        : mCommands(b, e),  // 命令范围
          mCustomCommands(pass.mCustomCommands.data(), pass.mCustomCommands.size()),  // 自定义命令
          mInstancedUboHandle(pass.mInstancedUboHandle),  // 实例化 UBO 句柄
          mInstancedDescriptorSetHandle(pass.mInstancedDescriptorSetHandle),  // 实例化描述符堆句柄
          mColorPassDescriptorSet(pass.mColorPassDescriptorSet),  // 颜色通道描述符堆
          mScissor(pass.mScissorViewport),  // 裁剪视口
          mPolygonOffsetOverride(false),  // 多边形偏移覆盖（默认关闭）
          mScissorOverride(false) {  // 裁剪覆盖（默认关闭）
    /**
     * 检查是否有裁剪视口（非默认值）
     */
    mHasScissorViewport = mScissor != backend::Viewport{ 0, 0, INT32_MAX, INT32_MAX };
    
    /**
     * 确保命令范围在渲染通道的有效范围内
     */
    assert_invariant(b >= pass.begin());
    assert_invariant(e <= pass.end());
}

/**
 * Executor 默认构造函数
 * 
 * 创建空的执行器（用于延迟初始化）。
 */
RenderPass::Executor::Executor() noexcept
        : mPolygonOffsetOverride(false),  // 多边形偏移覆盖（默认关闭）
          mScissorOverride(false),  // 裁剪覆盖（默认关闭）
          mHasScissorViewport(false) {  // 无裁剪视口
}

/**
 * Executor 移动构造函数
 */
RenderPass::Executor::Executor(Executor&& rhs) noexcept = default;

/**
 * Executor 移动赋值运算符
 */
RenderPass::Executor& RenderPass::Executor::operator=(Executor&& rhs) noexcept = default;

/**
 * Executor 析构函数
 * 
 * 注意：此析构函数实际上比较重，因为它内联了 ~vector<> 的析构。
 */
// this destructor is actually heavy because it inlines ~vector<>
RenderPass::Executor::~Executor() noexcept = default;

} // namespace filament
