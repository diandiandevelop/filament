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

#include "fg/details/Resource.h"

#include "fg/details/PassNode.h"
#include "fg/details/ResourceNode.h"

#include <utils/Panic.h>
#include <utils/CString.h>

using namespace filament::backend;

namespace filament {

/**
 * 虚拟资源析构函数
 */
VirtualResource::~VirtualResource() noexcept = default;

/**
 * 添加出边
 * 
 * 将边添加到资源节点的出边列表。
 * 
 * @param node 资源节点指针
 * @param edge 边指针
 */
UTILS_ALWAYS_INLINE
void VirtualResource::addOutgoingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept {
    node->addOutgoingEdge(edge);
}

/**
 * 设置入边
 * 
 * 设置资源节点的入边。
 * 
 * @param node 资源节点指针
 * @param edge 边指针
 */
UTILS_ALWAYS_INLINE
void VirtualResource::setIncomingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept {
    node->setIncomingEdge(edge);
}

/**
 * 转换为依赖图节点（ResourceNode 版本）
 * 
 * 此函数不能放在头文件中，因为会添加对 ResourceNode.h 的依赖，我们倾向于避免。
 * 
 * @param node 资源节点指针
 * @return 依赖图节点指针
 */
UTILS_ALWAYS_INLINE
DependencyGraph::Node* VirtualResource::toDependencyGraphNode(ResourceNode* node) noexcept {
    // this can't go to the header file, because it would add a dependency on ResourceNode.h,
    // which we prefer to avoid
    return node;  // ResourceNode 继承自 DependencyGraph::Node
}

/**
 * 转换为依赖图节点（PassNode 版本）
 * 
 * 此函数不能放在头文件中，因为会添加对 PassNode.h 的依赖，我们倾向于避免。
 * 
 * @param node 通道节点指针
 * @return 依赖图节点指针
 */
UTILS_ALWAYS_INLINE
DependencyGraph::Node* VirtualResource::toDependencyGraphNode(PassNode* node) noexcept {
    // this can't go to the header file, because it would add a dependency on PassNode.h
    // which we prefer to avoid
    return node;  // PassNode 继承自 DependencyGraph::Node
}

/**
 * 获取通道的读取边
 * 
 * 获取资源节点与通道节点之间的读取边。
 * 
 * @param resourceNode 资源节点指针
 * @param passNode 通道节点指针
 * @return 读取边指针
 */
UTILS_ALWAYS_INLINE
ResourceEdgeBase* VirtualResource::getReaderEdgeForPass(
        ResourceNode* resourceNode, PassNode* passNode) noexcept {
    // this can't go to the header file, because it would add a dependency on PassNode.h
    // which we prefer to avoid
    return resourceNode->getReaderEdgeForPass(passNode);
}

/**
 * 获取通道的写入边
 * 
 * 获取资源节点与通道节点之间的写入边。
 * 
 * @param resourceNode 资源节点指针
 * @param passNode 通道节点指针
 * @return 写入边指针
 */
UTILS_ALWAYS_INLINE
ResourceEdgeBase* VirtualResource::getWriterEdgeForPass(
        ResourceNode* resourceNode, PassNode* passNode) noexcept {
    // this can't go to the header file, because it would add a dependency on PassNode.h
    // which we prefer to avoid
    return resourceNode->getWriterEdgeForPass(passNode);
}

/**
 * 标记资源被通道需要
 * 
 * 更新资源的引用计数和生命周期信息。
 * 
 * @param pNode 通道节点指针
 */
void VirtualResource::neededByPass(PassNode* pNode) noexcept {
    refcount++;  // 增加引用计数
    /**
     * 确定第一个需要此资源的通道
     */
    // figure out which is the first pass to need this resource
    first = first ? first : pNode;  // 如果 first 为空，设置为当前通道
    /**
     * 确定最后一个需要此资源的通道
     */
    // figure out which is the last pass to need this resource
    last = pNode;  // 总是更新为当前通道（最后访问的通道）

    /**
     * 如果存在父资源，也扩展其生命周期
     */
    // also extend the lifetime of our parent resource if any
    if (parent != this) {
        parent->neededByPass(pNode);  // 递归更新父资源
    }
}

// ------------------------------------------------------------------------------------------------

/**
 * 导入渲染目标析构函数
 */
ImportedRenderTarget::~ImportedRenderTarget() noexcept = default;

/**
 * 导入渲染目标构造函数
 * 
 * 创建导入的渲染目标资源。
 * 
 * @param resourceName 资源名称
 * @param mainAttachmentDesc 主附件描述符
 * @param importedDesc 导入描述符
 * @param target 渲染目标句柄
 */
ImportedRenderTarget::ImportedRenderTarget(utils::StaticString resourceName,
        FrameGraphTexture::Descriptor const& mainAttachmentDesc,
        FrameGraphRenderPass::ImportDescriptor const& importedDesc,
        Handle<HwRenderTarget> target)
        : ImportedResource<FrameGraphTexture>(resourceName, mainAttachmentDesc,
                usageFromAttachmentsFlags(importedDesc.attachments), {}),  // 从附件标志计算使用方式
          target(target),  // 渲染目标句柄
          importedDesc(importedDesc) {  // 导入描述符
}

/**
 * 断言连接使用方式
 * 
 * 确保导入的渲染目标只能用作附件。
 * 
 * @param u 使用方式
 */
UTILS_NOINLINE
void ImportedRenderTarget::assertConnect(FrameGraphTexture::Usage const u) {
    /**
     * 定义所有附件使用方式的组合
     */
    constexpr auto ANY_ATTACHMENT = FrameGraphTexture::Usage::COLOR_ATTACHMENT |
                                    FrameGraphTexture::Usage::DEPTH_ATTACHMENT |
                                    FrameGraphTexture::Usage::STENCIL_ATTACHMENT;

    /**
     * 检查使用方式是否只包含附件标志
     */
    FILAMENT_CHECK_PRECONDITION(none(u & ~ANY_ATTACHMENT))
            << "Imported render target resource \"" << name.c_str()
            << "\" can only be used as an attachment (usage=" << utils::to_string(u).c_str() << ')';
}

/**
 * 连接（通道到资源，写入）
 * 
 * 创建从通道节点到资源节点的边（写入操作）。
 * 
 * @param graph 依赖图引用
 * @param passNode 通道节点指针
 * @param resourceNode 资源节点指针
 * @param u 纹理使用方式
 * @return 如果连接成功返回 true
 */
bool ImportedRenderTarget::connect(DependencyGraph& graph, PassNode* passNode,
        ResourceNode* resourceNode, TextureUsage const u) {
    // pass Node to resource Node edge (a write to)
    assertConnect(u);  // 断言使用方式有效
    return Resource::connect(graph, passNode, resourceNode, u);  // 调用基类连接方法
}

/**
 * 连接（资源到通道，读取）
 * 
 * 创建从资源节点到通道节点的边（读取操作）。
 * 
 * @param graph 依赖图引用
 * @param resourceNode 资源节点指针
 * @param passNode 通道节点指针
 * @param u 纹理使用方式
 * @return 如果连接成功返回 true
 */
bool ImportedRenderTarget::connect(DependencyGraph& graph, ResourceNode* resourceNode,
        PassNode* passNode, TextureUsage const u) {
    // resource Node to pass Node edge (a read from)
    assertConnect(u);  // 断言使用方式有效
    return Resource::connect(graph, resourceNode, passNode, u);  // 调用基类连接方法
}

/**
 * 从附件标志计算使用方式
 * 
 * 将目标缓冲区标志转换为纹理使用方式。
 * 
 * @param attachments 目标缓冲区标志
 * @return 纹理使用方式
 */
FrameGraphTexture::Usage ImportedRenderTarget::usageFromAttachmentsFlags(
        TargetBufferFlags const attachments) noexcept {

    /**
     * 检查颜色附件
     */
    if (any(attachments & TargetBufferFlags::COLOR_ALL))
        return FrameGraphTexture::Usage::COLOR_ATTACHMENT;

    /**
     * 检查深度和模板附件（同时）
     */
    if ((attachments & TargetBufferFlags::DEPTH_AND_STENCIL) == TargetBufferFlags::DEPTH_AND_STENCIL)
        return FrameGraphTexture::Usage::DEPTH_ATTACHMENT | FrameGraphTexture::Usage::STENCIL_ATTACHMENT;

    /**
     * 检查深度附件
     */
    if (any(attachments & TargetBufferFlags::DEPTH))
        return FrameGraphTexture::Usage::DEPTH_ATTACHMENT;

    /**
     * 检查模板附件
     */
    if (any(attachments & TargetBufferFlags::STENCIL))
        return FrameGraphTexture::Usage::STENCIL_ATTACHMENT;

    /**
     * 不应该到达这里，返回默认值
     */
    // we shouldn't be here
    return FrameGraphTexture::Usage::COLOR_ATTACHMENT;
}

// ------------------------------------------------------------------------------------------------

template class Resource<FrameGraphTexture>;
template class ImportedResource<FrameGraphTexture>;

} // namespace filament
