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

#include "fg/details/DependencyGraph.h"

#include <private/utils/Tracing.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/CString.h>
#include <utils/ostream.h>

#include <iterator>
#include <cstdint>

namespace filament {

/**
 * 构造函数
 * 
 * 初始化依赖图，预分配节点和边的容器空间。
 */
DependencyGraph::DependencyGraph() noexcept {
    /**
     * 为向量预分配合理的默认大小
     */
    // Some reasonable defaults size for our vectors
    mNodes.reserve(8);   // 预分配 8 个节点
    mEdges.reserve(16);  // 预分配 16 条边
}

DependencyGraph::~DependencyGraph() noexcept = default;

/**
 * 生成节点 ID
 * 
 * 返回当前节点数量作为新节点的 ID。
 * 
 * @return 新节点 ID
 */
uint32_t DependencyGraph::generateNodeId() noexcept {
    return mNodes.size();
}

/**
 * 注册节点
 * 
 * 将节点添加到依赖图中。
 * 注意：此时 Node* 可能尚未完全构造。
 * 
 * @param node 节点指针
 * @param id 节点 ID
 */
void DependencyGraph::registerNode(Node* node, NodeID const id) noexcept {
    /**
     * 断言：ID 必须等于当前节点数量（确保 ID 连续）
     */
    // Node* is not fully constructed here
    assert_invariant(id == mNodes.size());

    /**
     * 手动增长固定大小向量
     */
    // here we manually grow the fixed-size vector
    NodeContainer& nodes = mNodes;
    if (UTILS_UNLIKELY(nodes.capacity() == nodes.size())) {
        nodes.reserve(nodes.capacity() * 2);  // 容量翻倍
    }
    nodes.push_back(node);  // 添加节点
}

/**
 * 检查边是否有效
 * 
 * 边有效当且仅当源节点和目标节点都未被剔除。
 * 
 * @param edge 边指针
 * @return 如果边有效返回 true，否则返回 false
 */
bool DependencyGraph::isEdgeValid(Edge const* edge) const noexcept {
    auto& nodes = mNodes;
    Node const* from = nodes[edge->from];  // 源节点
    Node const* to = nodes[edge->to];      // 目标节点
    return !from->isCulled() && !to->isCulled();  // 两个节点都未被剔除
}

/**
 * 链接边
 * 
 * 将边添加到依赖图中。
 * 
 * @param edge 边指针
 */
void DependencyGraph::link(Edge* edge) noexcept {
    /**
     * 手动增长固定大小向量
     */
    // here we manually grow the fixed-size vector
    EdgeContainer& edges = mEdges;
    if (UTILS_UNLIKELY(edges.capacity() == edges.size())) {
        edges.reserve(edges.capacity() * 2);  // 容量翻倍
    }
    edges.push_back(edge);  // 添加边
}

/**
 * 获取所有边
 * 
 * @return 边容器常量引用
 */
DependencyGraph::EdgeContainer const& DependencyGraph::getEdges() const noexcept {
    return mEdges;
}

/**
 * 获取所有节点
 * 
 * @return 节点容器常量引用
 */
DependencyGraph::NodeContainer const& DependencyGraph::getNodes() const noexcept {
    return mNodes;
}

/**
 * 获取节点的入边
 * 
 * 返回所有指向指定节点的边。
 * 
 * @param node 节点指针
 * @return 入边容器
 */
DependencyGraph::EdgeContainer DependencyGraph::getIncomingEdges(
        Node const* node) const noexcept {
    /**
     * TODO: 可能需要更高效的实现
     */
    // TODO: we might need something more efficient
    auto result = EdgeContainer::with_capacity(mEdges.size());  // 预分配容量
    NodeID const nodeId = node->getId();  // 获取节点 ID
    /**
     * 复制所有目标节点为当前节点的边
     */
    std::copy_if(mEdges.begin(), mEdges.end(),
            std::back_insert_iterator<EdgeContainer>(result),
            [nodeId](auto edge) { return edge->to == nodeId; });
    return result;
}

/**
 * 获取节点的出边
 * 
 * 返回所有从指定节点出发的边。
 * 
 * @param node 节点指针
 * @return 出边容器
 */
DependencyGraph::EdgeContainer DependencyGraph::getOutgoingEdges(
        Node const* node) const noexcept {
    /**
     * TODO: 可能需要更高效的实现
     */
    // TODO: we might need something more efficient
    auto result = EdgeContainer::with_capacity(mEdges.size());  // 预分配容量
    NodeID const nodeId = node->getId();  // 获取节点 ID
    /**
     * 复制所有源节点为当前节点的边
     */
    std::copy_if(mEdges.begin(), mEdges.end(),
            std::back_insert_iterator<EdgeContainer>(result),
            [nodeId](auto edge) { return edge->from == nodeId; });
    return result;
}

/**
 * 获取节点（常量版本）
 * 
 * @param id 节点 ID
 * @return 节点常量指针
 */
DependencyGraph::Node const* DependencyGraph::getNode(NodeID const id) const noexcept {
    return mNodes[id];
}

/**
 * 获取节点（非常量版本）
 * 
 * @param id 节点 ID
 * @return 节点指针
 */
DependencyGraph::Node* DependencyGraph::getNode(NodeID const id) noexcept {
    return mNodes[id];
}

/**
 * 剔除未使用的节点
 * 
 * 使用引用计数算法剔除未被引用的节点。
 * 算法步骤：
 * 1. 更新引用计数：遍历所有边，增加源节点的引用计数
 * 2. 标记初始未引用节点：引用计数为 0 的节点入栈
 * 3. 传播剔除：从栈中取出节点，减少其入边源节点的引用计数，
 *    如果引用计数变为 0，则将该节点入栈
 * 
 * 结果：所有未被引用的节点（包括其依赖链）都会被标记为剔除。
 */
void DependencyGraph::cull() noexcept {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    auto& nodes = mNodes;
    auto& edges = mEdges;

    /**
     * 更新引用计数
     * 
     * 遍历所有边，增加源节点的引用计数。
     * 引用计数表示有多少条边指向该节点。
     */
    // update reference counts
    for (Edge* const pEdge : edges) {
        Node* node = nodes[pEdge->from];  // 获取源节点
        node->mRefCount++;  // 增加引用计数
    }

    /**
     * 剔除引用计数为 0 的节点
     * 
     * 使用栈进行广度优先遍历，标记所有未引用的节点。
     */
    // cull nodes with a 0 reference count
    auto stack = NodeContainer::with_capacity(nodes.size());  // 预分配栈空间
    /**
     * 初始化：将所有引用计数为 0 的节点入栈
     */
    for (Node* const pNode : nodes) {
        if (pNode->getRefCount() == 0) {
            stack.push_back(pNode);
        }
    }
    /**
     * 传播剔除：从栈中取出节点，减少其入边源节点的引用计数
     */
    while (!stack.empty()) {
        Node* const pNode = stack.back();  // 获取栈顶节点
        stack.pop_back();  // 弹出栈顶
        EdgeContainer const& incoming = getIncomingEdges(pNode);  // 获取入边
        /**
         * 遍历所有入边，减少源节点的引用计数
         */
        for (Edge* edge : incoming) {
            Node* pLinkedNode = getNode(edge->from);  // 获取源节点
            if (--pLinkedNode->mRefCount == 0) {  // 减少引用计数
                stack.push_back(pLinkedNode);  // 如果变为 0，入栈继续传播
            }
        }
    }
}

/**
 * 清空依赖图
 * 
 * 移除所有边和节点。
 * 注意：这些对象不会被销毁（DependencyGraph 不拥有它们）。
 */
void DependencyGraph::clear() noexcept {
    mEdges.clear();  // 清空边
    mNodes.clear();  // 清空节点
}

/**
 * 导出 Graphviz 格式的图
 * 
 * 将依赖图导出为 Graphviz DOT 格式，用于可视化。
 * 
 * @param out 输出流
 * @param name 图名称（可选）
 */
void DependencyGraph::export_graphviz(utils::io::ostream& out, char const* name) const noexcept {
#ifndef NDEBUG
    /**
     * 设置图的基本属性
     */
    const char* graphName = name ? name : "graph";  // 默认名称
    out << "digraph \"" << graphName << "\" {\n";  // 有向图
    out << "rankdir = LR\n";  // 从左到右布局
    out << "bgcolor = black\n";  // 黑色背景
    out << "node [shape=rectangle, fontname=\"helvetica\", fontsize=10]\n\n";  // 节点样式

    auto const& nodes = mNodes;

    /**
     * 输出所有节点
     */
    for (Node const* node : nodes) {
        uint32_t id = node->getId();  // 节点 ID
        utils::CString s = node->graphvizify();  // 获取节点 Graphviz 字符串
        out << "\"N" << id << "\" " << s.c_str() << "\n";  // 输出节点定义
    }

    out << "\n";
    /**
     * 输出所有边
     */
    for (Node const* node : nodes) {
        uint32_t id = node->getId();  // 节点 ID

        /**
         * 获取出边并分区：有效边在前，无效边在后
         */
        auto edges = getOutgoingEdges(node);
        auto first = edges.begin();
        auto pos = std::partition(first, edges.end(),
                [this](auto const& edge) { return isEdgeValid(edge); });  // 分区：有效边在前

        utils::CString s = node->graphvizifyEdgeColor();  // 获取边颜色

        /**
         * 渲染有效边（实线）
         */
        // render the valid edges
        if (first != pos) {
            out << "N" << id << " -> { ";
            while (first != pos) {
                Node const* ref = getNode((*first++)->to);  // 获取目标节点
                out << "N" << ref->getId() << " ";  // 输出目标节点 ID
            }
            out << "} [color=" << s.c_str() << "2]\n";  // 有效边颜色（较亮）
        }

        /**
         * 渲染无效边（虚线）
         */
        // render the invalid edges
        if (first != edges.end()) {
            out << "N" << id << " -> { ";
            while (first != edges.end()) {
                Node const* ref = getNode((*first++)->to);  // 获取目标节点
                out << "N" << ref->getId() << " ";  // 输出目标节点 ID
            }
            out << "} [color=" << s.c_str() << "4 style=dashed]\n";  // 无效边颜色（较暗，虚线）
        }
    }

    out << "}" << utils::io::endl;  // 结束图定义
#endif
}

/**
 * 检查图是否为无环图（DAG）
 * 
 * 使用拓扑排序算法检查图中是否存在环。
 * 
 * @return 如果图是无环的返回 true，否则返回 false
 */
bool DependencyGraph::isAcyclic() const noexcept {
#ifndef NDEBUG
    /**
     * 在图的副本上工作，避免修改原图
     */
    // We work on a copy of the graph
    DependencyGraph graph;
    graph.mEdges = mEdges;  // 复制边
    graph.mNodes = mNodes;  // 复制节点
    return isAcyclicInternal(graph);  // 在副本上检查
#else
    return true;  // 发布版本总是返回 true
#endif
}

/**
 * 检查图是否为无环图（内部实现）
 * 
 * 使用拓扑排序算法：
 * 1. 重复查找叶子节点（只有入边没有出边的节点）
 * 2. 如果找不到叶子节点，说明存在环
 * 3. 移除叶子节点及其边，继续迭代
 * 
 * @param graph 依赖图引用（会被修改）
 * @return 如果图是无环的返回 true，否则返回 false
 */
bool DependencyGraph::isAcyclicInternal(DependencyGraph& graph) noexcept {
#ifndef NDEBUG
    while (!graph.mNodes.empty() && !graph.mEdges.empty()) {
        /**
         * 检查是否至少有一个叶子节点
         * 
         * 叶子节点：只有入边没有出边的节点。
         */
        // check if we have at lest one leaf (i.e. nodes that have incoming but no outgoing edges)
        auto pos = std::find_if(graph.mNodes.begin(), graph.mNodes.end(),
                [&graph](Node const* node) {
                    /**
                     * 查找是否有从该节点出发的边
                     */
                    auto pos = std::find_if(graph.mEdges.begin(), graph.mEdges.end(),
                            [node](Edge const* edge) {
                                return edge->from == node->getId();  // 检查是否为源节点
                            });
                    return pos == graph.mEdges.end();  // 如果没有出边，则是叶子节点
                });

        /**
         * 如果找不到叶子节点，说明存在环
         */
        if (pos == graph.mNodes.end()) {
            return false;   // cyclic（有环）
        }

        /**
         * 移除叶子节点的所有边（入边和出边）
         */
        // remove the leaf's edges
        auto last = std::remove_if(graph.mEdges.begin(), graph.mEdges.end(),
                [&pos](Edge const* edge) {
            return edge->to == (*pos)->getId() || edge->from == (*pos)->getId();  // 与叶子节点相关的边
        });
        graph.mEdges.erase(last, graph.mEdges.end());  // 删除边

        /**
         * 移除叶子节点
         */
        // remove the leaf
        graph.mNodes.erase(pos);  // 删除节点
    }
#endif
    return true; // acyclic（无环）
}

// ------------------------------------------------------------------------------------------------

/**
 * 节点构造函数
 * 
 * 创建节点并将其注册到依赖图中。
 * 
 * @param graph 依赖图引用
 */
DependencyGraph::Node::Node(DependencyGraph& graph) noexcept : mId(graph.generateNodeId()) {
    graph.registerNode(this, mId);  // 注册节点
}

/**
 * 获取引用计数
 * 
 * 如果节点是目标节点（TARGET 标志位设置），返回 1，否则返回实际引用计数。
 * 
 * @return 引用计数
 */
uint32_t DependencyGraph::Node::getRefCount() const noexcept {
    return (mRefCount & TARGET) ? 1u : mRefCount;  // 目标节点总是返回 1
}

/**
 * 将节点标记为目标节点
 * 
 * 目标节点不会被剔除，即使没有引用。
 * 必须在剔除之前调用。
 */
void DependencyGraph::Node::makeTarget() noexcept {
    /**
     * 断言：引用计数必须为 0 或已经是 TARGET
     */
    assert_invariant(mRefCount == 0 || mRefCount == TARGET);
    mRefCount = TARGET;  // 设置 TARGET 标志位
}

/**
 * 获取节点名称
 * 
 * 默认返回 "unknown"，子类可以重写。
 * 
 * @return 节点名称
 */
char const* DependencyGraph::Node::getName() const noexcept {
    return "unknown";
}

/**
 * 生成 Graphviz 节点字符串
 * 
 * 生成用于 Graphviz 可视化的节点定义字符串。
 * 
 * @return Graphviz 节点字符串
 */
utils::CString DependencyGraph::Node::graphvizify() const noexcept {
#ifndef NDEBUG
    utils::CString s;

    uint32_t const id = getId();  // 节点 ID
    const char* const nodeName = getName();  // 节点名称
    uint32_t const refCount = getRefCount();  // 引用计数

    /**
     * 构建 Graphviz 节点定义
     */
    s.append("[label=\"");  // 开始标签
    s.append(nodeName);  // 节点名称
    s.append("\\nrefs: ");  // 换行，引用计数标签
    s.append(utils::to_string(refCount));  // 引用计数值
    s.append(", id: ");  // ID 标签
    s.append(utils::to_string(id));  // ID 值
    s.append("\", style=filled, fillcolor=");  // 填充样式
    s.append(refCount ? "skyblue" : "skyblue4");  // 颜色（有引用：浅蓝，无引用：深蓝）
    s.append("]");  // 结束标签

    return s;
#else
    return {};  // 发布版本返回空
#endif
}

/**
 * 生成 Graphviz 边颜色字符串
 * 
 * 返回用于 Graphviz 可视化的边颜色。
 * 
 * @return Graphviz 边颜色字符串
 */
utils::CString DependencyGraph::Node::graphvizifyEdgeColor() const noexcept {
#ifndef NDEBUG
    return utils::CString{ "darkolivegreen" };  // 深橄榄绿色
#else
    return {};  // 发布版本返回空
#endif
}

} // namespace filament
