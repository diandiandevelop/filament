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

#include "FrameGraphId.h"

#include "details/DependencyGraph.h"

#include "fg/FrameGraph.h"
#include "fg/details/PassNode.h"
#include "fg/details/ResourceNode.h"

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/CString.h>

#include <new>
#include <cstdint>

namespace filament {

/**
 * 资源节点构造函数
 * 
 * 创建资源节点并初始化依赖图节点和资源句柄。
 * 
 * @param fg 帧图引用
 * @param h 资源句柄
 * @param parent 父资源句柄
 */
ResourceNode::ResourceNode(FrameGraph& fg, FrameGraphHandle const h, FrameGraphHandle const parent) noexcept
        : Node(fg.getGraph()),  // 初始化依赖图节点
          resourceHandle(h),  // 初始化资源句柄
          mFrameGraph(fg),  // 初始化帧图引用
          mReaderPasses(fg.getArena()),  // 初始化读取通道列表（使用帧图内存池）
          mParentHandle(parent) {  // 初始化父句柄
}

/**
 * 资源节点析构函数
 * 
 * 清理所有边和依赖关系。
 */
ResourceNode::~ResourceNode() noexcept {
    VirtualResource* resource = mFrameGraph.getResource(resourceHandle);  // 获取资源
    assert_invariant(resource);  // 断言资源有效
    resource->destroyEdge(mWriterPass);  // 销毁写入边
    for (auto* pEdge : mReaderPasses) {  // 遍历读取边
        resource->destroyEdge(pEdge);  // 销毁读取边
    }
    delete mParentReadEdge;  // 删除父读取边
    delete mParentWriteEdge;  // 删除父写入边
    delete mForwardedEdge;  // 删除转发边
}

/**
 * 获取父节点
 * 
 * 获取父资源节点。
 * 
 * @return 父资源节点指针（如果没有父节点则返回 nullptr）
 */
ResourceNode* ResourceNode::getParentNode() noexcept {
    ResourceNode* const parentNode = mParentHandle ?  // 如果有父句柄
            mFrameGraph.getActiveResourceNode(mParentHandle) : nullptr;  // 获取活动资源节点
    assert_invariant(mParentHandle == ResourceNode::getHandle(parentNode));  // 断言句柄匹配
    return parentNode;  // 返回父节点
}

/**
 * 获取祖先节点
 * 
 * 向上遍历父节点链，返回最顶层的祖先节点。
 * 
 * @param node 起始节点
 * @return 祖先节点指针
 */
ResourceNode* ResourceNode::getAncestorNode(ResourceNode* node) noexcept {
    ResourceNode* ancestor = node;  // 初始化为起始节点
    do {  // 循环遍历父节点
        node = node->getParentNode();  // 获取父节点
        ancestor = node ? node : ancestor;  // 如果父节点存在则更新祖先，否则保持当前祖先
    } while (node);  // 直到没有父节点
    return ancestor;  // 返回祖先节点
}

/**
 * 获取资源名称
 * 
 * @return 资源名称 C 字符串
 */
char const* ResourceNode::getName() const noexcept {
    return mFrameGraph.getResource(resourceHandle)->name.c_str();  // 返回资源名称
}

/**
 * 添加传出边
 * 
 * 添加一个指向读取通道的边。
 * 
 * @param edge 资源边基类指针
 */
void ResourceNode::addOutgoingEdge(ResourceEdgeBase* edge) noexcept {
    mReaderPasses.push_back(edge);  // 添加到读取通道列表
}

/**
 * 设置传入边
 * 
 * 设置指向写入通道的边（只能有一个写入者）。
 * 
 * @param edge 资源边基类指针
 */
void ResourceNode::setIncomingEdge(ResourceEdgeBase* edge) noexcept {
    assert_invariant(mWriterPass == nullptr);  // 断言没有写入者
    mWriterPass = edge;  // 设置写入边
}

/**
 * 检查是否有活动读取者
 * 
 * 检查是否有未被剔除的读取通道。
 * 
 * @return 如果有活动读取者返回 true，否则返回 false
 */
bool ResourceNode::hasActiveReaders() const noexcept {
    /**
     * 这里我们不使用 mReaderPasses，因为这不会考虑子资源
     */
    // here we don't use mReaderPasses because this wouldn't account for subresources
    DependencyGraph& dependencyGraph = mFrameGraph.getGraph();  // 获取依赖图
    auto const& readers = dependencyGraph.getOutgoingEdges(this);  // 获取传出边
    for (auto const& reader : readers) {  // 遍历读取者
        if (!dependencyGraph.getNode(reader->to)->isCulled()) {  // 如果读取者未被剔除
            return true;  // 返回 true
        }
    }
    return false;  // 返回 false
}

/**
 * 检查是否有活动写入者
 * 
 * 检查是否有写入通道。
 * 
 * @return 如果有活动写入者返回 true，否则返回 false
 */
bool ResourceNode::hasActiveWriters() const noexcept {
    /**
     * 这里我们不使用 mReaderPasses，因为这不会考虑子资源
     */
    // here we don't use mReaderPasses because this wouldn't account for subresources
    DependencyGraph const& dependencyGraph = mFrameGraph.getGraph();  // 获取依赖图
    auto const& writers = dependencyGraph.getIncomingEdges(this);  // 获取传入边
    /**
     * 如果我们自己未被剔除，写入者按定义也不会被剔除
     */
    // writers are not culled by definition if we're not culled ourselves
    return !writers.empty();  // 返回是否非空
}

/**
 * 获取通道的读取边
 * 
 * 查找指向指定通道的读取边。
 * 
 * @param node 通道节点常量指针
 * @return 资源边基类指针（如果未找到则返回 nullptr）
 */
ResourceEdgeBase* ResourceNode::getReaderEdgeForPass(PassNode const* node) const noexcept {
    auto pos = std::find_if(mReaderPasses.begin(), mReaderPasses.end(),  // 查找边
            [node](ResourceEdgeBase const* edge) {  // Lambda 函数
                return edge->to == node->getId();  // 检查目标节点 ID
            });
    return pos != mReaderPasses.end() ? *pos : nullptr;  // 如果找到则返回边，否则返回 nullptr
}

/**
 * 获取通道的写入边
 * 
 * 查找来自指定通道的写入边。
 * 
 * @param node 通道节点常量指针
 * @return 资源边基类指针（如果未找到则返回 nullptr）
 */
ResourceEdgeBase* ResourceNode::getWriterEdgeForPass(PassNode const* node) const noexcept {
    return mWriterPass && mWriterPass->from == node->getId() ? mWriterPass : nullptr;  // 如果写入边存在且来源匹配则返回，否则返回 nullptr
}

/**
 * 检查是否有来自指定通道的写入
 * 
 * @param node 通道节点常量指针
 * @return 如果有写入返回 true，否则返回 false
 */
bool ResourceNode::hasWriteFrom(PassNode const* node) const noexcept {
    return bool(getWriterEdgeForPass(node));  // 返回是否有写入边
}


/**
 * 设置父读取依赖
 * 
 * 在父节点和当前节点之间建立读取依赖关系。
 * 
 * @param parent 父资源节点指针
 */
void ResourceNode::setParentReadDependency(ResourceNode* parent) noexcept {
    if (!mParentReadEdge) {  // 如果还没有父读取边
        mParentReadEdge = new(std::nothrow) DependencyGraph::Edge(mFrameGraph.getGraph(), parent, this);  // 创建依赖图边
    }
}


/**
 * 设置父写入依赖
 * 
 * 在当前节点和父节点之间建立写入依赖关系。
 * 
 * @param parent 父资源节点指针
 */
void ResourceNode::setParentWriteDependency(ResourceNode* parent) noexcept {
    if (!mParentWriteEdge) {  // 如果还没有父写入边
        mParentWriteEdge = new(std::nothrow) DependencyGraph::Edge(mFrameGraph.getGraph(), this, parent);  // 创建依赖图边
    }
}

/**
 * 设置转发资源依赖
 * 
 * 在当前节点和源节点之间建立转发依赖关系。
 * 
 * @param source 源资源节点指针
 */
void ResourceNode::setForwardResourceDependency(ResourceNode* source) noexcept {
    assert_invariant(!mForwardedEdge);  // 断言还没有转发边
    mForwardedEdge = new(std::nothrow) DependencyGraph::Edge(mFrameGraph.getGraph(), this, source);  // 创建依赖图边
}


/**
 * 解析资源使用
 * 
 * 根据读取和写入通道解析资源的使用标志。
 * 
 * @param graph 依赖图引用
 */
void ResourceNode::resolveResourceUsage(DependencyGraph& graph) noexcept {
    VirtualResource* pResource = mFrameGraph.getResource(resourceHandle);  // 获取资源
    assert_invariant(pResource);  // 断言资源有效
    if (pResource->refcount) {  // 如果引用计数大于 0
        pResource->resolveUsage(graph, mReaderPasses.data(), mReaderPasses.size(), mWriterPass);  // 解析使用标志
    }
}

/**
 * Graphviz 可视化
 * 
 * 生成用于 Graphviz 可视化的节点标签字符串（仅在调试模式下）。
 * 
 * @return Graphviz 节点标签字符串
 */
utils::CString ResourceNode::graphvizify() const noexcept {
#ifndef NDEBUG  // 仅在调试模式下
    utils::CString s;  // 创建字符串

    uint32_t const id = getId();  // 获取节点 ID
    const char* const nodeName = getName();  // 获取节点名称
    VirtualResource const* const pResource = mFrameGraph.getResource(resourceHandle);  // 获取资源
    FrameGraph::ResourceSlot const& slot = mFrameGraph.getResourceSlot(resourceHandle);  // 获取资源槽

    /**
     * 构建 Graphviz 节点标签
     */
    s.append("[label=\"");  // 开始标签
    s.append(nodeName);  // 节点名称
    s.append("\\nrefs: ");  // 引用计数标签
    s.append(utils::to_string(pResource->refcount));  // 引用计数
    s.append(", id: ");  // ID 标签
    s.append(utils::to_string(id));  // 节点 ID
    s.append("\\nversion: ");  // 版本标签
    s.append(utils::to_string(resourceHandle.version));  // 句柄版本
    s.append("/");  // 分隔符
    s.append(utils::to_string(slot.version));  // 槽版本
    if (pResource->isImported()) {  // 如果是导入资源
        s.append(", imported");  // 添加导入标记
    }
    s.append("\\nusage: ");  // 使用方式标签
    s.append(pResource->usageString().c_str());  // 使用方式字符串
    s.append("\", ");  // 结束标签

    /**
     * 设置节点样式和填充颜色
     */
    s.append("style=filled, fillcolor=");  // 样式和填充颜色
    s.append(pResource->refcount ? "skyblue" : "skyblue4");  // 根据引用计数选择颜色
    s.append("]");  // 结束样式

    return s;  // 返回字符串
#else  // 非调试模式
    return {};  // 返回空字符串
#endif
}

/**
 * Graphviz 边颜色
 * 
 * 返回用于 Graphviz 可视化的边颜色。
 * 
 * @return 边颜色字符串
 */
utils::CString ResourceNode::graphvizifyEdgeColor() const noexcept {
    return "darkolivegreen";  // 返回深橄榄绿色
}

} // namespace filament
