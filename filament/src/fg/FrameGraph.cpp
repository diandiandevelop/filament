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

#include "fg/FrameGraph.h"
#include "fg/details/PassNode.h"
#include "fg/details/Resource.h"
#include "fg/details/ResourceNode.h"
#include "fg/details/DependencyGraph.h"

#include "FrameGraphId.h"
#include "FrameGraphPass.h"
#include "FrameGraphRenderPass.h"
#include "FrameGraphTexture.h"
#include "ResourceAllocator.h"

#include "details/Engine.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/CString.h>
#include <utils/StaticString.h>
#include <utils/debug.h>
#include <utils/ostream.h>
#include <utils/Panic.h>

#include <algorithm>
#include <functional>

#include <stdint.h>

namespace filament {

/**
 * 构建器内联构造函数
 * 
 * @param fg 帧图引用
 * @param passNode 通道节点指针
 */
inline FrameGraph::Builder::Builder(FrameGraph& fg, PassNode* passNode) noexcept
        : mFrameGraph(fg),  // 初始化帧图引用
          mPassNode(passNode) {  // 初始化通道节点指针
}

/**
 * 标记副作用
 * 
 * 将通道标记为有副作用（即使没有读取或写入资源也会执行）。
 */
void FrameGraph::Builder::sideEffect() noexcept {
    mPassNode->makeTarget();  // 将通道节点标记为目标
}

/**
 * 获取资源名称
 * 
 * @param handle 帧图句柄
 * @return 资源名称
 */
utils::StaticString FrameGraph::Builder::getName(FrameGraphHandle const handle) const noexcept {
    return mFrameGraph.getResource(handle)->name;  // 返回资源名称
}

/**
 * 声明渲染通道
 * 
 * 声明一个渲染通道并返回其索引。
 * 
 * @param name 通道名称
 * @param desc 渲染通道描述符
 * @return 渲染通道索引
 */
uint32_t FrameGraph::Builder::declareRenderPass(utils::StaticString name,
        FrameGraphRenderPass::Descriptor const& desc) {
    /**
     * 这里可以安全地转换为 RenderPassNode，因为我们不会在这里处理 PresentPassNode。
     * 另外，只有 RenderPassNode 才有渲染目标的概念。
     */
    // it's safe here to cast to RenderPassNode because we can't be here for a PresentPassNode
    // also only RenderPassNodes have the concept of render targets.
    return static_cast<RenderPassNode*>(mPassNode)->declareRenderTarget(mFrameGraph, *this, name, desc);  // 声明渲染目标
}

/**
 * 声明渲染通道（重载版本）
 * 
 * 声明一个使用指定颜色纹理的渲染通道。
 * 
 * @param color 颜色纹理 ID
 * @param index 输出渲染通道索引（可选）
 * @return 颜色纹理 ID（可能已修改）
 */
FrameGraphId<FrameGraphTexture> FrameGraph::Builder::declareRenderPass(
        FrameGraphId<FrameGraphTexture> color, uint32_t* index) {
    color = write(color);  // 将颜色纹理标记为写入
    uint32_t const id = declareRenderPass(getName(color),  // 声明渲染通道
            { .attachments = { .color = { color }}});  // 设置颜色附件
    if (index) *index = id;  // 如果提供了索引指针，则设置索引
    return color;  // 返回颜色纹理 ID
}

// ------------------------------------------------------------------------------------------------

/**
 * 帧图构造函数
 * 
 * 创建帧图对象并初始化内存池和容器。
 * 
 * @param resourceAllocator 资源分配器接口引用
 * @param mode 帧图模式
 */
FrameGraph::FrameGraph(ResourceAllocatorInterface& resourceAllocator, Mode const mode)
        : mResourceAllocator(resourceAllocator),  // 初始化资源分配器
          mArena("FrameGraph Arena", 262144),  // 初始化内存池（256KB）
          mMode(mode),  // 初始化模式
          mResourceSlots(mArena),  // 初始化资源槽容器
          mResources(mArena),  // 初始化资源容器
          mResourceNodes(mArena),  // 初始化资源节点容器
          mPassNodes(mArena)  // 初始化通道节点容器
{
    mResourceSlots.reserve(256);  // 预留资源槽空间
    mResources.reserve(256);  // 预留资源空间
    mResourceNodes.reserve(256);  // 预留资源节点空间
    mPassNodes.reserve(64);  // 预留通道节点空间
}

/**
 * 销毁内部资源
 * 
 * 按顺序销毁所有节点和资源。
 * 注意：销毁顺序很重要。
 */
UTILS_NOINLINE
void FrameGraph::destroyInternal() noexcept {
    /**
     * 销毁顺序很重要
     */
    // the order of destruction is important here
    LinearAllocatorArena& arena = mArena;  // 获取内存池引用
    std::for_each(mPassNodes.begin(), mPassNodes.end(), [&arena](auto item) {  // 遍历通道节点
        arena.destroy(item);  // 销毁通道节点
    });
    std::for_each(mResourceNodes.begin(), mResourceNodes.end(), [&arena](auto item) {  // 遍历资源节点
        arena.destroy(item);  // 销毁资源节点
    });
    std::for_each(mResources.begin(), mResources.end(), [&arena](auto item) {  // 遍历资源
        arena.destroy(item);  // 销毁资源
    });
}

/**
 * 帧图析构函数
 */
FrameGraph::~FrameGraph() noexcept {
    destroyInternal();  // 销毁内部资源
}

/**
 * 重置帧图
 * 
 * 清除所有节点和资源，准备下一帧。
 */
void FrameGraph::reset() noexcept {
    destroyInternal();  // 销毁内部资源
    mPassNodes.clear();  // 清空通道节点
    mResourceNodes.clear();  // 清空资源节点
    mResources.clear();  // 清空资源
    mResourceSlots.clear();  // 清空资源槽
}

/**
 * 编译帧图
 * 
 * 分析依赖关系，剔除不可达节点，并计算资源生命周期。
 * 
 * @return 帧图引用（支持链式调用）
 */
FrameGraph& FrameGraph::compile() noexcept {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);  // 跟踪调用

    DependencyGraph& dependencyGraph = mGraph;  // 获取依赖图引用

    /**
     * 首先剔除不可达的节点
     */
    // first we cull unreachable nodes
    dependencyGraph.cull();  // 1) 剔除不可达的 pass / 资源节点

    /**
     * 更新资源本身的引用计数并
     * 计算活动通道的第一个/最后一个使用者
     */
    /*
     * update the reference counter of the resource themselves and
     * compute first/last users for active passes
     */

    /**
     * 将活动通道节点移到前面
     */
    mActivePassNodesEnd = std::stable_partition(  // 稳定分区
            mPassNodes.begin(), mPassNodes.end(), [](auto const& pPassNode) {  // 遍历通道节点
        return !pPassNode->isCulled();  // 返回是否未剔除
    });

    /**
     * 遍历活动通道节点
     */
    auto first = mPassNodes.begin();  // 开始迭代器
    const auto activePassNodesEnd = mActivePassNodesEnd;  // 活动节点结束迭代器
    while (first != activePassNodesEnd) {  // 遍历活动节点
        PassNode* const passNode = *first;  // 获取通道节点
        first++;  // 递增迭代器
        assert_invariant(!passNode->isCulled());  // 断言未剔除


        auto const& reads = dependencyGraph.getIncomingEdges(passNode);  // 获取读取边
        for (auto const& edge : reads) {  // 遍历读取边
            /**
             * 所有传入边在构造时应该是有效的
             */
            // all incoming edges should be valid by construction
            assert_invariant(dependencyGraph.isEdgeValid(edge));  // 断言边有效
            auto pNode = static_cast<ResourceNode*>(dependencyGraph.getNode(edge->from));  // 获取资源节点
            passNode->registerResource(pNode->resourceHandle);  // 注册资源
        }

        /**
         * 处理写入边
         */
        auto const& writes = dependencyGraph.getOutgoingEdges(passNode);  // 获取写入边
        for (auto const& edge : writes) {  // 遍历写入边
            /**
             * 如果边指向的节点已被剔除，传出边可能无效
             * 但因为我们未被剔除，并且我们是通道，所以我们添加对
             * 我们正在写入的资源的引用。
             */
            // An outgoing edge might be invalid if the node it points to has been culled
            // but because we are not culled, and we're a pass we add a reference to
            // the resource we are writing to.
            auto pNode = static_cast<ResourceNode*>(dependencyGraph.getNode(edge->to));  // 获取资源节点
            passNode->registerResource(pNode->resourceHandle);  // 注册资源
        }

        passNode->resolve();  // 2) 记录首/末资源使用，供生命周期管理
    }

    /**
     * 将资源添加到每个活动通道的相应列表中以进行去虚拟化或销毁
     */
    // add resource to de-virtualize or destroy to the corresponding list for each active pass
    for (auto* pResource : mResources) {  // 遍历所有资源
        VirtualResource* resource = pResource;  // 获取虚拟资源
        if (resource->refcount) {  // 如果引用计数大于 0
            PassNode* pFirst = resource->first;  // 获取第一个使用者
            PassNode* pLast = resource->last;  // 获取最后一个使用者
            assert_invariant(!pFirst == !pLast);  // 断言两者同时存在或不存在
            if (pFirst && pLast) {  // 如果两者都存在
                assert_invariant(!pFirst->isCulled());  // 断言第一个未剔除
                assert_invariant(!pLast->isCulled());  // 断言最后一个未剔除
                pFirst->devirtualize.push_back(resource);  // 添加到第一个通道的去虚拟化列表
                pLast->destroy.push_back(resource);  // 添加到最后一个通道的销毁列表
            }
        }
    }
    // 3) 确定资源生命周期：在哪个 pass 分配 / 销毁

    /**
     * 解析使用标志位
     */
    /*
     * Resolve Usage bits
     */
    for (auto& pNode : mResourceNodes) {  // 遍历所有资源节点
        /**
         * 我们不能在这里使用 isCulled()，因为一些被剔除的资源仍然是活动的。
         * 我们可以使用 "getResource(pNode->resourceHandle)->refcount"，但这很昂贵。
         * 我们也不能删除或重新排序此数组，因为句柄是它的索引。
         * 我们可能需要构建一个活动资源索引数组。
         */
        // we can't use isCulled() here because some culled resource are still active
        // we could use "getResource(pNode->resourceHandle)->refcount" but that's expensive.
        // We also can't remove or reorder this array, as handles are indices to it.
        // We might need to build an array of indices to active resources.
        pNode->resolveResourceUsage(dependencyGraph);  // 4) 解析资源使用标志（采样/附件/读写）
    }

    return *this;  // 返回自身引用
}

/**
 * 执行帧图
 * 
 * 按顺序执行所有活动通道，分配和释放资源。
 * 
 * @param driver 驱动 API 引用
 */
void FrameGraph::execute(backend::DriverApi& driver) noexcept {

    bool const useProtectedMemory = mMode == Mode::PROTECTED;  // 是否使用受保护内存
    auto const& passNodes = mPassNodes;  // 获取通道节点引用
    auto& resourceAllocator = mResourceAllocator;  // 获取资源分配器引用

    FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "FrameGraph");  // 跟踪名称
    driver.pushGroupMarker("FrameGraph");  // 推送组标记

    /**
     * 遍历所有活动通道节点
     */
    auto first = passNodes.begin();  // 开始迭代器
    const auto activePassNodesEnd = mActivePassNodesEnd;  // 活动节点结束迭代器
    while (first != activePassNodesEnd) {  // 遍历活动节点
        PassNode* const node = *first;  // 获取通道节点
        first++;  // 递增迭代器
        assert_invariant(!node->isCulled());  // 断言未剔除

        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, node->getName());  // 跟踪名称
        driver.pushGroupMarker(node->getName());  // 推送组标记

        /**
         * 去虚拟化资源列表
         * 
         * 在该通道首次使用前创建具体的 GPU 资源。
         */
        // devirtualize resourcesList
        for (VirtualResource* resource : node->devirtualize) {  // 遍历去虚拟化列表
            assert_invariant(resource->first == node);  // 断言这是第一个使用者
            resource->devirtualize(resourceAllocator, useProtectedMemory);  // 去虚拟化资源
        }  // 1) 去虚拟化：在该 pass 首次使用前创建具体 GPU 资源

        /**
         * 调用执行
         * 
         * 执行通道，内部发送 DriverApi 命令。
         */
        // call execute
        FrameGraphResources const resources(*this, *node);  // 创建帧图资源
        node->execute(resources, driver);  // 2) 执行 pass，内部发 DriverApi 命令

        /**
         * 销毁具体资源
         * 
         * 生命周期结束后立即销毁，减少占用。
         */
        // destroy concrete resources
        for (VirtualResource* resource : node->destroy) {  // 遍历销毁列表
            assert_invariant(resource->last == node);  // 断言这是最后一个使用者
            resource->destroy(resourceAllocator);  // 销毁资源
        }  // 3) 生命周期结束立即销毁，减少占用
        driver.popGroupMarker();  // 弹出组标记
    }
    driver.popGroupMarker();  // 弹出组标记
}

/**
 * 添加呈现通道
 * 
 * 添加一个呈现通道（用于将最终结果呈现到屏幕）。
 * 
 * @param setup 设置函数
 */
void FrameGraph::addPresentPass(const std::function<void(Builder&)>& setup) noexcept {
    PresentPassNode* node = mArena.make<PresentPassNode>(*this);  // 创建呈现通道节点
    mPassNodes.push_back(node);  // 添加到通道节点列表
    Builder builder(*this, node);  // 创建构建器
    setup(builder);  // 调用设置函数
    builder.sideEffect();  // 标记副作用
}

/**
 * 添加通道（内部方法）
 * 
 * 在通道列表中记录并创建构建器。
 * 
 * @param name 通道名称
 * @param base 通道基类指针
 * @return 构建器
 */
FrameGraph::Builder FrameGraph::addPassInternal(char const* name, FrameGraphPassBase* base) noexcept {
    /**
     * 在通道列表中记录并创建构建器
     */
    // record in our pass list and create the builder
    PassNode* node = mArena.make<RenderPassNode>(*this, name, base);  // 创建渲染通道节点
    base->setNode(node);  // 设置节点
    mPassNodes.push_back(node);  // 添加到通道节点列表
    return { *this, node };  // 返回构建器
}

/**
 * 创建新版本
 * 
 * 为资源创建新版本（用于处理同一资源的多次写入）。
 * 
 * @param handle 帧图句柄
 * @return 新版本的句柄
 */
FrameGraphHandle FrameGraph::createNewVersion(FrameGraphHandle handle) noexcept {
    assert_invariant(handle);  // 断言句柄有效
    ResourceNode* const node = getActiveResourceNode(handle);  // 获取活动资源节点
    assert_invariant(node);  // 断言节点有效
    FrameGraphHandle const parent = node->getParentHandle();  // 获取父句柄
    ResourceSlot& slot = getResourceSlot(handle);  // 获取资源槽
    slot.version = ++handle.version;  // 增加父版本的版本号
    // increase the parent's version
    slot.nid = (ResourceSlot::Index)mResourceNodes.size();  // 创建新的父节点
    // create the new parent node
    ResourceNode* newNode = mArena.make<ResourceNode>(*this, handle, parent);  // 创建新资源节点
    mResourceNodes.push_back(newNode);  // 添加到资源节点列表
    return handle;  // 返回句柄
}

/**
 * 如果需要，为子资源创建新版本
 * 
 * 如果还没有为子资源创建新的 ResourceNode，则创建一个。
 * 
 * @param node 资源节点指针
 * @return 资源节点指针（可能是新创建的）
 */
ResourceNode* FrameGraph::createNewVersionForSubresourceIfNeeded(ResourceNode* node) noexcept {
    ResourceSlot& slot = getResourceSlot(node->resourceHandle);  // 获取资源槽
    if (slot.sid < 0) {  // 如果还没有子资源节点
        /**
         * 如果我们还没有为此资源创建新的 ResourceNode，则创建一个。
         * 我们保留旧的 ResourceNode 索引，以便可以将所有读取指向它。
         */
        // if we don't already have a new ResourceNode for this resource, create one.
        // we keep the old ResourceNode index, so we can direct all the reads to it.
        slot.sid = slot.nid;  // 记录父节点的当前 ResourceNode
        // record the current ResourceNode of the parent
        slot.nid = (ResourceSlot::Index)mResourceNodes.size();  // 创建新的父节点
        // create the new parent node
        node = mArena.make<ResourceNode>(*this, node->resourceHandle, node->getParentHandle());  // 创建新资源节点
        mResourceNodes.push_back(node);  // 添加到资源节点列表
    }
    return node;  // 返回节点
}

/**
 * 添加资源（内部方法）
 * 
 * 添加一个新资源到帧图。
 * 
 * @param resource 虚拟资源指针
 * @return 帧图句柄
 */
FrameGraphHandle FrameGraph::addResourceInternal(VirtualResource* resource) noexcept {
    return addSubResourceInternal(FrameGraphHandle{}, resource);  // 调用子资源添加方法（无父句柄）
}

/**
 * 添加子资源（内部方法）
 * 
 * 添加一个子资源到帧图。
 * 
 * @param parent 父句柄
 * @param resource 虚拟资源指针
 * @return 帧图句柄
 */
FrameGraphHandle FrameGraph::addSubResourceInternal(FrameGraphHandle parent,
        VirtualResource* resource) noexcept {
    FrameGraphHandle const handle(mResourceSlots.size());  // 创建句柄（基于资源槽大小）
    ResourceSlot& slot = mResourceSlots.emplace_back();  // 添加资源槽
    slot.rid = (ResourceSlot::Index)mResources.size();  // 设置资源索引
    slot.nid = (ResourceSlot::Index)mResourceNodes.size();  // 设置资源节点索引
    mResources.push_back(resource);  // 添加到资源列表
    ResourceNode* pNode = mArena.make<ResourceNode>(*this, handle, parent);  // 创建资源节点
    mResourceNodes.push_back(pNode);  // 添加到资源节点列表
    return handle;  // 返回句柄
}

/**
 * 读取资源（内部方法）
 * 
 * 将通道标记为资源的读取者，建立依赖关系。
 * 
 * @param handle 帧图句柄
 * @param passNode 通道节点指针
 * @param connect 连接函数（返回 true 表示成功）
 * @return 帧图句柄
 */
FrameGraphHandle FrameGraph::readInternal(FrameGraphHandle const handle, PassNode* passNode,
        const std::function<bool(ResourceNode*, VirtualResource*)>& connect) {

    assertValid(handle);  // 断言句柄有效

    VirtualResource* const resource = getResource(handle);  // 获取资源
    ResourceNode* const node = getActiveResourceNode(handle);  // 获取活动资源节点

    /**
     * 检查前提条件
     */
    // Check preconditions
    bool const passAlreadyAWriter = node->hasWriteFrom(passNode);  // 检查通道是否已经是写入者
    FILAMENT_CHECK_PRECONDITION(!passAlreadyAWriter)  // 检查通道不是写入者
            << "Pass \"" << passNode->getName() << "\" already writes to \"" << node->getName()
            << "\"";

    /**
     * 检查资源是否从未被写入且不是导入的
     */
    if (!node->hasWriterPass() && !resource->isImported()) {  // 如果没有写入者且不是导入的
        /**
         * TODO: 我们试图读取一个从未被写入且也不是导入的资源，
         * 所以它不可能有有效数据。
         * 这应该是一个错误吗？
         */
        // TODO: we're attempting to read from a resource that was never written and is not
        //       imported either, so it can't have valid data in it.
        //       Should this be an error?
    }

    /**
     * 连接可能失败，如果使用标志使用不正确
     */
    // Connect can fail if usage flags are incorrectly used
    if (connect(node, resource)) {  // 如果连接成功
        if (resource->isSubResource()) {  // 如果是子资源
            /**
             * 这是从子资源读取，所以我们需要从父节点的
             * 节点到子资源添加一个"读取"——但我们可能有两个父节点，一个用于读取，
             * 一个用于写入，所以我们需要使用用于读取的那个。
             */
            // this is a read() from a subresource, so we need to add a "read" from the parent's
            // node to the subresource -- but we may have two parent nodes, one for reads and
            // one for writes, so we need to use the one for reads.
            auto* parentNode = node->getParentNode();  // 获取父节点
            ResourceSlot const& slot = getResourceSlot(parentNode->resourceHandle);  // 获取资源槽
            if (slot.sid >= 0) {  // 如果有父节点的读取节点
                /**
                 * 我们有父节点的读取节点，使用那个
                 */
                // we have a parent's node for reads, use that one
                parentNode = mResourceNodes[slot.sid];
            }
            node->setParentReadDependency(parentNode);
        } else {
            // we're reading from a top-level resource (i.e. not a subresource), but this
            // resource is a parent of some subresource, and it might exist as a version for
            // writing, in this case we need to add a dependency from its "read" version to
            // itself.
            ResourceSlot const& slot = getResourceSlot(handle);
            if (slot.sid >= 0) {
                node->setParentReadDependency(mResourceNodes[slot.sid]);
            }
        }

        // if a resource has a subresource, then its handle becomes valid again as soon as it's used.
        ResourceSlot& slot = getResourceSlot(handle);
        if (slot.sid >= 0) {
            // we can now forget the "read" parent node, which becomes the current one again
            // until the next write.
            slot.sid = -1;
        }

        return handle;
    }

    return {};
}

/**
 * 写入资源（内部方法）
 * 
 * 将通道标记为资源的写入者，建立依赖关系。
 * 
 * @param handle 帧图句柄
 * @param passNode 通道节点指针
 * @param connect 连接函数（返回 true 表示成功）
 * @return 帧图句柄
 */
FrameGraphHandle FrameGraph::writeInternal(FrameGraphHandle handle, PassNode* passNode,
        const std::function<bool(ResourceNode*, VirtualResource*)>& connect) {

    assertValid(handle);  // 断言句柄有效

    VirtualResource* const resource = getResource(handle);  // 获取资源
    ResourceNode* node = getActiveResourceNode(handle);  // 获取活动资源节点
    ResourceNode* parentNode = node->getParentNode();  // 获取父节点

    /**
     * 如果我们要写入子资源，还需要从子资源节点
     * 到父节点的新版本添加一个"写入"边（如果还没有的话）。
     */
    // if we're writing into a subresource, we also need to add a "write" from the subresource
    // node to a new version of the parent's node, if we don't already have one.
    if (resource->isSubResource()) {  // 如果是子资源
        assert_invariant(parentNode);  // 断言父节点存在
        /**
         * 这可能是子资源的子资源，在这种情况下，我们想要最老的祖先，
         * 即启动一切的节点。
         */
        // this could be a subresource from a subresource, and in this case, we want the oldest
        // ancestor, that is, the node that started it all.
        parentNode = ResourceNode::getAncestorNode(parentNode);  // 获取祖先节点
        // FIXME: do we need the equivalent of hasWriterPass() test below
        parentNode = createNewVersionForSubresourceIfNeeded(parentNode);  // 如果需要，创建新版本
    }

    /**
     * 如果此节点已经写入此资源，只需更新使用位
     */
    // if this node already writes to this resource, just update the used bits
    if (!node->hasWriteFrom(passNode)) {  // 如果通道还没有写入
        if (!node->hasWriterPass() && !node->hasReaders()) {  // 如果没有写入者和读取者
            // FIXME: should this also take subresource writes into account
            /**
             * 如果我们还没有写入者或读取者，这只意味着资源刚刚创建
             * 并且从未被写入，所以我们不需要新节点或增加版本号
             */
            // if we don't already have a writer or a reader, it just means the resource was just created
            // and was never written to, so we don't need a new node or increase the version number
        } else {  // 否则
            handle = createNewVersion(handle);  // 创建新版本
            // refresh the node
            node = getActiveResourceNode(handle);  // 刷新节点
        }
    }

    if (connect(node, resource)) {  // 如果连接成功
        if (resource->isSubResource()) {  // 如果是子资源
            node->setParentWriteDependency(parentNode);  // 设置父写入依赖
        }
        if (resource->isImported()) {  // 如果是导入资源
            /**
             * 写入导入资源意味着副作用
             */
            // writing to an imported resource implies a side-effect
            passNode->makeTarget();  // 标记通道为目标
        }
        return handle;  // 返回句柄
    } else {  // 如果连接失败
        // FIXME: we need to undo everything we did to this point
    }

    return {};  // 返回空句柄
}

/**
 * 转发资源（内部方法）
 * 
 * 将一个资源转发到另一个资源，使被替换的资源句柄指向新资源。
 * 
 * @param resourceHandle 资源句柄（新资源）
 * @param replaceResourceHandle 被替换的资源句柄
 * @return 资源句柄（新资源）
 */
FrameGraphHandle FrameGraph::forwardResourceInternal(FrameGraphHandle const resourceHandle,
        FrameGraphHandle const replaceResourceHandle) {

    assertValid(resourceHandle);  // 断言资源句柄有效

    assertValid(replaceResourceHandle);  // 断言被替换资源句柄有效

    ResourceSlot& replacedResourceSlot = getResourceSlot(replaceResourceHandle);  // 获取被替换资源槽
    ResourceNode* const replacedResourceNode = getActiveResourceNode(replaceResourceHandle);  // 获取被替换资源节点

    ResourceSlot const& resourceSlot = getResourceSlot(resourceHandle);  // 获取资源槽
    ResourceNode* const resourceNode = getActiveResourceNode(resourceHandle);  // 获取资源节点
    VirtualResource* const resource = getResource(resourceHandle);  // 获取资源

    replacedResourceNode->setForwardResourceDependency(resourceNode);  // 设置转发资源依赖

    if (resource->isSubResource() && replacedResourceNode->hasWriterPass()) {  // 如果是子资源且被替换资源有写入者
        /**
         * 如果被替换的资源被写入并且被子资源替换——这意味着
         * 现在写入的是那个子资源，我们需要从该子资源
         * 到其父节点添加一个写入依赖（父节点实际上也被写入）。
         * 这通常会在 write() 期间发生，但这里写入已经发生。
         * 我们创建父节点的新版本以确保没有人在此之后写入它
         * （注意：我不完全确定这是否需要/正确）。
         */
        // if the replaced resource is written to and replaced by a subresource -- meaning
        // that now it's that subresource that is being written to, we need to add a
        // write-dependency from this subresource to its parent node (which effectively is
        // being written as well). This would normally happen during write(), but here
        // the write has already happened.
        // We create a new version of the parent node to ensure nobody writes into it beyond
        // this point (note: it's not completely clear to me if this is needed/correct).
        ResourceNode* parentNode = ResourceNode::getAncestorNode(resourceNode);  // 获取祖先节点
        parentNode = createNewVersionForSubresourceIfNeeded(parentNode);  // 如果需要，创建新版本
        resourceNode->setParentWriteDependency(parentNode);  // 设置父写入依赖
    }

    replacedResourceSlot.rid = resourceSlot.rid;  // 复制资源索引
    /**
     * nid 不变，因为我们保留具有图信息的节点
     * FIXME: .sid 应该发生什么？
     */
    // nid is unchanged, because we keep our node which has the graph information
    // FIXME: what should happen with .sid?

    /**
     * 使 replaceResourceHandle 永远无效
     */
    // makes the replaceResourceHandle forever invalid
    replacedResourceSlot.version = -1;  // 设置版本为 -1（无效）

    return resourceHandle;  // 返回资源句柄
}

/**
 * 导入渲染目标
 * 
 * 导入一个外部渲染目标到帧图。
 * 
 * @param name 资源名称
 * @param desc 导入描述符
 * @param target 渲染目标句柄
 * @return 帧图纹理 ID
 */
FrameGraphId<FrameGraphTexture> FrameGraph::import(utils::StaticString name,
        FrameGraphRenderPass::ImportDescriptor const& desc,
        backend::Handle<backend::HwRenderTarget> target) {
    /**
     * 创建一个表示导入渲染目标的资源
     */
    // create a resource that represents the imported render target
    VirtualResource* vresource =
            mArena.make<ImportedRenderTarget>(name,  // 资源名称
                    FrameGraphTexture::Descriptor{  // 纹理描述符
                            .width = desc.viewport.width,  // 宽度
                            .height = desc.viewport.height  // 高度
                    }, desc, target);  // 导入描述符和渲染目标句柄
    return FrameGraphId<FrameGraphTexture>(addResourceInternal(vresource));  // 添加资源并返回 ID
}

/**
 * 检查句柄是否有效
 * 
 * 检查帧图句柄是否已初始化且版本匹配。
 * 
 * @param handle 帧图句柄
 * @return 如果句柄有效返回 true，否则返回 false
 */
bool FrameGraph::isValid(FrameGraphHandle const handle) const {
    /**
     * 下面的代码这样写是为了我们可以轻松设置断点
     */
    // Code below is written this way so that we can set breakpoints easily.
    if (!handle.isInitialized()) {  // 如果句柄未初始化
        return false;  // 返回 false
    }
    ResourceSlot const& slot = getResourceSlot(handle);  // 获取资源槽
    if (handle.version != slot.version) {  // 如果版本不匹配
        return false;  // 返回 false
    }
    return true;  // 返回 true
}

/**
 * 断言句柄有效
 * 
 * 如果句柄无效，则触发断言错误。
 * 
 * @param handle 帧图句柄
 */
void FrameGraph::assertValid(FrameGraphHandle const handle) const {
    FILAMENT_CHECK_PRECONDITION(isValid(handle))  // 检查句柄是否有效
            << "Resource handle is invalid or uninitialized {id=" << (int)handle.index  // 错误消息
            << ", version=" << (int)handle.version << "}";
}

/**
 * 检查通道是否被剔除
 * 
 * @param pass 帧图通道基类常量引用
 * @return 如果通道被剔除返回 true，否则返回 false
 */
bool FrameGraph::isCulled(FrameGraphPassBase const& pass) const noexcept {
    return pass.getNode().isCulled();  // 返回通道节点是否被剔除
}

/**
 * 检查图是否无环
 * 
 * @return 如果图无环返回 true，否则返回 false
 */
bool FrameGraph::isAcyclic() const noexcept {
    return mGraph.isAcyclic();  // 返回依赖图是否无环
}

/**
 * 导出 Graphviz
 * 
 * 将帧图导出为 Graphviz 格式。
 * 
 * @param out 输出流引用
 * @param name 图名称
 */
void FrameGraph::export_graphviz(utils::io::ostream& out, char const* name) const noexcept {
    mGraph.export_graphviz(out, name);  // 导出依赖图为 Graphviz 格式
}

fgviewer::FrameGraphInfo FrameGraph::getFrameGraphInfo(const char *viewName) const {
#if FILAMENT_ENABLE_FGVIEWER
    fgviewer::FrameGraphInfo info{utils::CString(viewName)};
    std::vector<fgviewer::FrameGraphInfo::Pass> passes;

    auto first = mPassNodes.begin();
    const auto activePassNodesEnd = mActivePassNodesEnd;
    while (first != activePassNodesEnd) {
        PassNode *const pass = *first;
        ++first;

        assert_invariant(!pass->isCulled());
        std::vector<fgviewer::ResourceId> reads;
        auto const &readEdges = mGraph.getIncomingEdges(pass);
        for (auto const &edge: readEdges) {
            // all incoming edges should be valid by construction
            assert_invariant(mGraph.isEdgeValid(edge));
            auto resourceNode = static_cast<const ResourceNode*>(mGraph.getNode(edge->from));
            assert_invariant(resourceNode);
            if (resourceNode->getRefCount() == 0)
                continue;

            reads.push_back(resourceNode->resourceHandle.index);
        }

        std::vector<fgviewer::ResourceId> writes;
        auto const &writeEdges = mGraph.getOutgoingEdges(pass);
        for (auto const &edge: writeEdges) {
            // It is possible that the node we're writing to has been culled.
            // In this case we'd like to ignore the edge.
            if (!mGraph.isEdgeValid(edge)) {
                continue;
            }
            auto resourceNode = static_cast<const ResourceNode*>(mGraph.getNode(edge->to));
            assert_invariant(resourceNode);
            if (resourceNode->getRefCount() == 0)
                continue;
            writes.push_back(resourceNode->resourceHandle.index);
        }
        passes.emplace_back(utils::CString(pass->getName()),
            std::move(reads), std::move(writes));
    }

    std::unordered_map<fgviewer::ResourceId, fgviewer::FrameGraphInfo::Resource> resources;
    for (const auto &resourceNode: mResourceNodes) {
        const FrameGraphHandle resourceHandle = resourceNode->resourceHandle;
        if (resources.find(resourceHandle.index) != resources.end())
            continue;

        if (resourceNode->getRefCount() == 0)
            continue;

        std::vector<fgviewer::FrameGraphInfo::Resource::Property> resourceProps;
        auto emplace_resource_property =
            [&resourceProps](utils::CString key, utils::CString value) {
                resourceProps.emplace_back(fgviewer::FrameGraphInfo::Resource::Property{
                    .name = std::move(key),
                    .value = std::move(value)
                });
            };
        auto emplace_resource_descriptor = [this, &emplace_resource_property](
            const FrameGraphHandle& resourceHandle) {
            // TODO: A better way to handle generic resource types. Right now we only have one
            // resource type so it works
            auto descriptor = static_cast<Resource<FrameGraphTexture> const*>(
                getResource(resourceHandle))->descriptor;
            emplace_resource_property("width", utils::to_string(descriptor.width));
            emplace_resource_property("height", utils::to_string(descriptor.height));
            emplace_resource_property("depth", utils::to_string(descriptor.depth));
            emplace_resource_property("format", utils::to_string(descriptor.format));

        };

        if (resourceNode->getParentNode() != nullptr) {
            emplace_resource_property("is_subresource_of",
                    utils::to_string(resourceNode->getParentHandle().index));
        }
        emplace_resource_descriptor(resourceHandle);
        resources.emplace(resourceHandle.index, fgviewer::FrameGraphInfo::Resource(
                              resourceHandle.index,
                              utils::CString(resourceNode->getName()),
                              std::move(resourceProps))
        );
    }

    info.setResources(std::move(resources));
    info.setPasses(std::move(passes));

    // Generate GraphViz DOT data
    utils::io::sstream out;
    this->export_graphviz(out, viewName);
    info.setGraphvizData(utils::CString(out.c_str()));

    return info;
#else
    return fgviewer::FrameGraphInfo();
#endif
}


// ------------------------------------------------------------------------------------------------

/*
 * Explicit template instantiation for FrameGraphTexture which is a known type,
 * to reduce compile time and code size.
 */

template void FrameGraph::present(FrameGraphId<FrameGraphTexture> input);

template FrameGraphId<FrameGraphTexture> FrameGraph::create(utils::StaticString name,
        FrameGraphTexture::Descriptor const& desc) noexcept;

template FrameGraphId<FrameGraphTexture> FrameGraph::createSubresource(FrameGraphId<FrameGraphTexture> parent,
        utils::StaticString name, FrameGraphTexture::SubResourceDescriptor const& desc) noexcept;

template FrameGraphId<FrameGraphTexture> FrameGraph::import(utils::StaticString name,
        FrameGraphTexture::Descriptor const& desc, FrameGraphTexture::Usage usage, FrameGraphTexture const& resource) noexcept;

template FrameGraphId<FrameGraphTexture> FrameGraph::read(PassNode* passNode,
        FrameGraphId<FrameGraphTexture> input, FrameGraphTexture::Usage usage);

template FrameGraphId<FrameGraphTexture> FrameGraph::write(PassNode* passNode,
        FrameGraphId<FrameGraphTexture> input, FrameGraphTexture::Usage usage);

template FrameGraphId<FrameGraphTexture> FrameGraph::forwardResource(
        FrameGraphId<FrameGraphTexture> resource, FrameGraphId<FrameGraphTexture> replacedResource);

} // namespace filament
