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

#ifndef TNT_FILAMENT_FG_DETAILS_RESOURCENODE_H
#define TNT_FILAMENT_FG_DETAILS_RESOURCENODE_H

#include "fg/details/DependencyGraph.h"
#include "fg/details/Utilities.h"

namespace utils {
class CString;
} // namespace utils

namespace filament {

class FrameGraph;
class ResourceEdgeBase;

/**
 * 资源节点类
 * 
 * 表示帧图中的资源节点（如纹理）。
 * 资源节点可以被子资源节点引用，形成层次结构。
 * 
 * 功能：
 * - 管理资源的读取和写入关系
 * - 跟踪父资源和子资源
 * - 解析资源使用方式
 */
class ResourceNode : public DependencyGraph::Node {
public:
    /**
     * 构造函数
     * 
     * @param fg 帧图引用
     * @param h 资源句柄
     * @param parent 父资源句柄
     */
    ResourceNode(FrameGraph& fg, FrameGraphHandle h, FrameGraphHandle parent) noexcept;
    
    /**
     * 析构函数
     */
    ~ResourceNode() noexcept override;

    /**
     * 禁止拷贝构造
     */
    ResourceNode(ResourceNode const&) = delete;
    
    /**
     * 禁止拷贝赋值
     */
    ResourceNode& operator=(ResourceNode const&) = delete;

    /**
     * 添加出边
     * 
     * 将边添加到出边列表（从资源到通道的边）。
     * 
     * @param edge 边指针
     */
    void addOutgoingEdge(ResourceEdgeBase* edge) noexcept;
    
    /**
     * 设置入边
     * 
     * 设置入边（从通道到资源的边）。
     * 
     * @param edge 边指针
     */
    void setIncomingEdge(ResourceEdgeBase* edge) noexcept;

    // constants
    const FrameGraphHandle resourceHandle;  // 资源句柄


    /**
     * 检查是否有通道写入此资源节点
     * 
     * @return 如果有写入通道返回 true，否则返回 false
     */
    // is a PassNode writing to this ResourceNode
    bool hasWriterPass() const noexcept {
        return mWriterPass != nullptr;  // 检查写入通道指针
    }

    /**
     * 检查是否有任何非剔除节点写入此资源节点
     * 
     * @return 如果有活跃写入者返回 true，否则返回 false
     */
    // is any non culled Node (of any type) writing to this ResourceNode
    bool hasActiveWriters() const noexcept;

    /**
     * 获取指定通道的写入边
     * 
     * 如果指定通道正在写入此资源，返回对应的边。
     * 
     * @param node 通道节点指针
     * @return 写入边指针，如果没有则返回 nullptr
     */
    // is the specified PassNode writing to this resource, if so return the corresponding edge.
    ResourceEdgeBase* getWriterEdgeForPass(PassNode const* node) const noexcept;
    
    /**
     * 检查指定通道是否写入此资源
     * 
     * @param node 通道节点指针
     * @return 如果写入返回 true，否则返回 false
     */
    bool hasWriteFrom(PassNode const* node) const noexcept;


    /**
     * 检查是否有至少一个通道读取此资源节点
     * 
     * @return 如果有读取通道返回 true，否则返回 false
     */
    // is at least one PassNode reading from this ResourceNode
    bool hasReaders() const noexcept {
        return !mReaderPasses.empty();  // 检查读取通道列表是否为空
    }

    /**
     * 检查是否有任何非剔除节点读取此资源节点
     * 
     * @return 如果有活跃读取者返回 true，否则返回 false
     */
    // is any non culled Node (of any type) reading from this ResourceNode
    bool hasActiveReaders() const noexcept;

    /**
     * 获取指定通道的读取边
     * 
     * 如果指定通道正在读取此资源，返回对应的边。
     * 
     * @param node 通道节点指针
     * @return 读取边指针，如果没有则返回 nullptr
     */
    // is the specified PassNode reading this resource, if so return the corresponding edge.
    ResourceEdgeBase* getReaderEdgeForPass(PassNode const* node) const noexcept;


    /**
     * 解析资源使用方式
     * 
     * 根据读取和写入关系确定资源的最终使用方式。
     * 
     * @param graph 依赖图引用
     */
    void resolveResourceUsage(DependencyGraph& graph) noexcept;

    /**
     * 获取父资源句柄
     * 
     * @return 父资源句柄
     */
    // return the parent's handle
    FrameGraphHandle getParentHandle() noexcept {
        return mParentHandle;  // 返回父资源句柄
    }

    /**
     * 获取父资源节点
     * 
     * @return 父资源节点指针
     */
    // return the parent's node
    ResourceNode* getParentNode() noexcept;

    /**
     * 获取最古老的祖先节点
     * 
     * 沿着父节点链向上查找，直到找到根节点。
     * 
     * @param node 资源节点指针
     * @return 祖先节点指针
     */
    // return the oldest ancestor node
    static ResourceNode* getAncestorNode(ResourceNode* node) noexcept;

    /**
     * 设置父资源读取依赖
     * 
     * 这是我们正在读取的父资源，作为我们被读取的传播效果。
     * 
     * @param parent 父资源节点指针
     */
    // this is the parent resource we're reading from, as a propagating effect of
    // us being read from.
    void setParentReadDependency(ResourceNode* parent) noexcept;

    /**
     * 设置父资源写入依赖
     * 
     * 这是我们正在写入的父资源，作为我们被写入的传播效果。
     * 
     * @param parent 父资源节点指针
     */
    // this is the parent resource we're writing to, as a propagating effect of
    // us being writen to.
    void setParentWriteDependency(ResourceNode* parent) noexcept;

    /**
     * 设置转发资源依赖
     * 
     * @param source 源资源节点指针
     */
    void setForwardResourceDependency(ResourceNode* source) noexcept;

    /**
     * 获取节点名称（虚函数）
     * 
     * @return 节点名称字符串
     */
    // virtuals from DependencyGraph::Node
    char const* getName() const noexcept override;

    /**
     * 获取资源句柄（静态方法）
     * 
     * @param node 资源节点指针
     * @return 资源句柄（如果节点为空则返回空句柄）
     */
    static FrameGraphHandle getHandle(ResourceNode const* node) noexcept {
        return node ? node->resourceHandle : FrameGraphHandle{};  // 如果节点有效返回句柄，否则返回空句柄
    }

private:
    FrameGraph& mFrameGraph;
    Vector<ResourceEdgeBase *> mReaderPasses;
    ResourceEdgeBase* mWriterPass = nullptr;
    FrameGraphHandle mParentHandle;
    DependencyGraph::Edge* mParentReadEdge = nullptr;
    DependencyGraph::Edge* mParentWriteEdge = nullptr;
    DependencyGraph::Edge* mForwardedEdge = nullptr;

    // virtuals from DependencyGraph::Node
    utils::CString graphvizify() const noexcept override;
    utils::CString graphvizifyEdgeColor() const noexcept override;
};

} // namespace filament

#endif // TNT_FILAMENT_FG_DETAILS_RESOURCENODE_H
