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

#ifndef TNT_FILAMENT_FG_DETAILS_DEPENDENCYGRAPH_H
#define TNT_FILAMENT_FG_DETAILS_DEPENDENCYGRAPH_H

#include <utils/ostream.h>
#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/debug.h>

#include <vector>

namespace filament {

/**
 * 依赖图类（有向无环图，DAG）
 * 
 * 一个非常简单的依赖图类，支持剔除未使用的节点。
 * 
 * 功能：
 * - 管理节点和边的依赖关系
 * - 支持节点剔除（移除未使用的节点）
 * - 提供拓扑排序和遍历功能
 */
class DependencyGraph {
public:
    /**
     * 构造函数
     */
    DependencyGraph() noexcept;
    
    /**
     * 析构函数
     */
    ~DependencyGraph() noexcept;
    
    /**
     * 禁止拷贝构造
     */
    DependencyGraph(const DependencyGraph&) noexcept = delete;
    
    /**
     * 禁止拷贝赋值
     */
    DependencyGraph& operator=(const DependencyGraph&) noexcept = delete;

    using NodeID = uint32_t;  // 节点 ID 类型

    class Node;

    /**
     * 边结构
     * 
     * 两个节点之间的链接。
     */
    struct Edge {
        // An Edge can't be modified after it's created (e.g. by copying into it)
        const NodeID from;  // 源节点 ID
        const NodeID to;    // 目标节点 ID

        /**
         * 创建边
         * 
         * 在两个节点之间创建边。
         * 调用者拥有 Edge 对象的所有权，只有在调用 DependencyGraph::clear() 后销毁才是安全的。
         * 在剔除后使用 DependencyGraph::isEdgeValid() 检查边是否仍然在两端连接。
         * 
         * @param graph 要添加边的依赖图引用
         * @param from 图中的源节点指针（不进行运行时检查）
         * @param to 图中的目标节点指针（不进行运行时检查）
         */
        Edge(DependencyGraph& graph, Node* from, Node* to);

        /**
         * 禁止拷贝和移动
         * 
         * 这是为了允许安全地子类化。
         * 子类可以持有自己的数据。
         */
        // Edge can't be copied or moved, this is to allow subclassing safely.
        // Subclasses can hold their own data.
        Edge(Edge const& rhs) noexcept = delete;
        Edge& operator=(Edge const& rhs) noexcept = delete;
    };

    /**
     * 通用节点类
     */
    class Node {
    public:
        /**
         * 创建节点
         * 
         * 创建节点并将其添加到图中。
         * 调用者拥有 Node 对象的所有权，只有在调用 DependencyGraph::clear() 后销毁才是安全的。
         * 
         * @param graph 要添加节点的依赖图引用
         */
        explicit Node(DependencyGraph& graph) noexcept;

        /**
         * 禁止拷贝
         */
        // Nodes can't be copied
        Node(Node const&) noexcept = delete;
        Node& operator=(Node const&) noexcept = delete;

        /**
         * 允许移动
         */
        //! Nodes can be moved
        Node(Node&&) noexcept = default;

        /**
         * 虚析构函数
         */
        virtual ~Node() noexcept = default;

        /**
         * 获取节点唯一 ID
         * 
         * @return 节点 ID
         */
        //! returns a unique id for this node
        NodeID getId() const noexcept { return mId; }

        /**
         * 将此节点标记为目标
         * 
         * 防止此节点被剔除。必须在剔除之前调用。
         */
        /** Prevents this node from being culled. Must be called before culling. */
        void makeTarget() noexcept;

        /**
         * 检查此节点是否是目标
         * 
         * @return 如果是目标返回 true，否则返回 false
         */
        /** Returns true if this Node is a target */
        bool isTarget() const noexcept { return mRefCount >= TARGET; }

        /**
         * 返回此节点是否被剔除
         * 
         * 这仅在调用 DependencyGraph::cull() 后有效。
         * 
         * @return 如果节点被剔除返回 true，否则返回 false
         */
        bool isCulled() const noexcept { return mRefCount == 0; }  // 引用计数为 0 表示被剔除

        /**
         * 获取此节点的引用计数
         * 
         * 返回有多少其他节点链接到此节点。
         * 这仅在调用 DependencyGraph::cull() 后有效。
         * 
         * @return 链接到此节点的节点数量
         */
        uint32_t getRefCount() const noexcept;

    public:
        /**
         * 获取节点名称（虚函数）
         * 
         * @return 节点名称字符串
         */
        //! return the name of this node
        virtual char const* getName() const noexcept;

        /**
         * 输出为 Graphviz 字符串（虚函数）
         * 
         * @return Graphviz 格式的字符串
         */
        //! output itself as a graphviz string
        virtual utils::CString graphvizify() const noexcept;

        /**
         * 输出从此节点出发的边的 Graphviz 颜色字符串（虚函数）
         * 
         * @return Graphviz 颜色字符串
         */
        //! output a graphviz color string for an Edge from this node
        virtual utils::CString graphvizifyEdgeColor() const noexcept;

    private:
        /**
         * 读取我们的节点：即我们对它们的引用
         */
        // nodes that read from us: i.e. we have a reference to them
        friend class DependencyGraph;  // 允许 DependencyGraph 访问私有成员
        static const constexpr uint32_t TARGET = 0x80000000u;  // 目标节点标志（最高位）
        uint32_t mRefCount = 0;     // 对我们的引用数量
        const NodeID mId;           // 唯一 ID
    };

    /**
     * 边容器类型别名
     * 
     * 固定容量的边指针向量。
     */
    using EdgeContainer = utils::FixedCapacityVector<Edge*, std::allocator<Edge*>, false>;
    
    /**
     * 节点容器类型别名
     * 
     * 固定容量的节点指针向量。
     */
    using NodeContainer = utils::FixedCapacityVector<Node*, std::allocator<Node*>, false>;

    /**
     * 清除图
     * 
     * 从图中移除所有边和节点。
     * 注意：这些对象不会被销毁（DependencyGraph 不拥有它们）。
     */
    void clear() noexcept;

    /**
     * 获取所有边的列表
     * 
     * @return 边容器常量引用
     */
    /** return the list of all edges */
    EdgeContainer const& getEdges() const noexcept;

    /**
     * 获取所有节点的列表
     * 
     * @return 节点容器常量引用
     */
    /** return the list of all nodes */
    NodeContainer const& getNodes() const noexcept;

    /**
     * 获取节点的入边列表
     * 
     * @param node 要查询的节点
     * @return 入边列表
     */
    /**
     * Returns the list of incoming edges to a node
     * @param node the node to consider
     * @return A list of incoming edges
     */
    EdgeContainer getIncomingEdges(Node const* node) const noexcept;

    /**
     * 获取节点的出边列表
     * 
     * @param node 要查询的节点
     * @return 出边列表
     */
    /**
     * Returns the list of outgoing edges to a node
     * @param node the node to consider
     * @return A list of outgoing edges
     */
    EdgeContainer getOutgoingEdges(Node const* node) const noexcept;

    /**
     * 根据 ID 获取节点（常量版本）
     * 
     * @param id 节点 ID
     * @return 节点常量指针
     */
    Node const* getNode(NodeID id) const noexcept;

    /**
     * 根据 ID 获取节点（非常量版本）
     * 
     * @param id 节点 ID
     * @return 节点指针
     */
    Node* getNode(NodeID id) noexcept;

    /**
     * 剔除未引用的节点
     * 
     * 链接不会被移除，只更新引用计数。
     */
    //! cull unreferenced nodes. Links ARE NOT removed, only reference counts are updated.
    void cull() noexcept;

    /**
     * 检查边是否有效
     * 
     * 返回边是否有效，即两端都连接到未被剔除的节点。
     * 仅在调用 cull() 后有效。
     * 
     * @param edge 要检查的边
     * @return 如果边有效返回 true，否则返回 false
     */
    /**
     * Return whether an edge is valid, that is if both ends are connected to nodes
     * that are not culled. Valid only after cull() is called.
     * @param edge to check the validity of.
     */
    bool isEdgeValid(Edge const* edge) const noexcept;

    /**
     * 导出图的 Graphviz 视图
     * 
     * @param out 输出流
     * @param name 图名称（可选）
     */
    //! export a graphviz view of the graph
    void export_graphviz(utils::io::ostream& out, const char* name = nullptr) const noexcept;

    /**
     * 检查图是否为无环图
     * 
     * @return 如果是无环图返回 true，否则返回 false
     */
    bool isAcyclic() const noexcept;

private:
    // id must be the node key in the NodeContainer
    uint32_t generateNodeId() noexcept;
    void registerNode(Node* node, NodeID id) noexcept;
    void link(Edge* edge) noexcept;
    static bool isAcyclicInternal(DependencyGraph& graph) noexcept;
    NodeContainer mNodes;
    EdgeContainer mEdges;
};

inline DependencyGraph::Edge::Edge(DependencyGraph& graph,
        Node* from, Node* to)
        : from(from->getId()), to(to->getId()) {
    assert_invariant(graph.mNodes[this->from] == from);
    assert_invariant(graph.mNodes[this->to] == to);
    graph.link(this);
}

} // namespace filament

#endif // TNT_FILAMENT_FG_DETAILS_DEPENDENCYGRAPH_H
