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

#ifndef TNT_FILAMENT_FG_DETAILS_RESOURCE_H
#define TNT_FILAMENT_FG_DETAILS_RESOURCE_H

#include "fg/FrameGraphId.h"
#include "fg/FrameGraphTexture.h"
#include "fg/FrameGraphRenderPass.h"
#include "fg/details/DependencyGraph.h"

#include <utils/Panic.h>
#include <utils/StaticString.h>

namespace filament {
class ResourceAllocatorInterface;
} // namespace::filament

namespace filament {

class PassNode;
class ResourceNode;
class ImportedRenderTarget;

/*
 * ResourceEdgeBase only exists to enforce type safety
 */
class ResourceEdgeBase : public DependencyGraph::Edge {
public:
    using Edge::Edge;
};

/*
 * The generic parts of virtual resources.
 */
class VirtualResource {
public:
    // constants
    VirtualResource* parent;
    utils::StaticString name;

    // computed during compile()
    uint32_t refcount = 0;
    PassNode* first = nullptr;  // pass that needs to instantiate the resource
    PassNode* last = nullptr;   // pass that can destroy the resource

    explicit VirtualResource(utils::StaticString const name) noexcept : parent(this), name(name) {
    }

    VirtualResource(VirtualResource* parent,
            utils::StaticString const name) noexcept : parent(parent), name(name) {
    }
    VirtualResource(VirtualResource const& rhs) noexcept = delete;
    VirtualResource& operator=(VirtualResource const&) = delete;
    virtual ~VirtualResource() noexcept;

    // updates first/last/refcount
    void neededByPass(PassNode* pNode) noexcept;

    bool isSubResource() const noexcept { return parent != this; }

    VirtualResource* getResource() noexcept {
        VirtualResource* p = this;
        while (p->parent != p) {
            p = p->parent;
        }
        return p;
    }

    /*
     * Called during FrameGraph::compile(), this gives an opportunity for this resource to
     * calculate its effective usage flags.
     */
    virtual void resolveUsage(DependencyGraph& graph,
            ResourceEdgeBase const* const* edges, size_t count,
            ResourceEdgeBase const* writer) noexcept = 0;

    /* Instantiate the concrete resource */
    virtual void devirtualize(ResourceAllocatorInterface& resourceAllocator,
            bool useProtectedMemory) noexcept = 0;

    /* Destroy the concrete resource */
    virtual void destroy(ResourceAllocatorInterface& resourceAllocator) noexcept = 0;

    /* Destroy an Edge instantiated by this resource */
    virtual void destroyEdge(DependencyGraph::Edge* edge) noexcept = 0;

    virtual utils::CString usageString() const noexcept = 0;

    virtual bool isImported() const noexcept { return false; }

    // this is to workaround our lack of RTTI -- otherwise we could use dynamic_cast
    virtual ImportedRenderTarget* asImportedRenderTarget() noexcept { return nullptr; }

protected:
    void addOutgoingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept;
    void setIncomingEdge(ResourceNode* node, ResourceEdgeBase* edge) noexcept;
    // these exist only so we don't have to include PassNode.h or ResourceNode.h
    static DependencyGraph::Node* toDependencyGraphNode(ResourceNode* node) noexcept;
    static DependencyGraph::Node* toDependencyGraphNode(PassNode* node) noexcept;
    static ResourceEdgeBase* getReaderEdgeForPass(ResourceNode* resourceNode, PassNode* passNode) noexcept;
    static ResourceEdgeBase* getWriterEdgeForPass(ResourceNode* resourceNode, PassNode* passNode) noexcept;
};

// ------------------------------------------------------------------------------------------------

/**
 * 资源模板类
 * 
 * 虚拟资源的资源特定部分。
 * 
 * @tparam RESOURCE 资源类型（如 FrameGraphTexture）
 */
/*
 * Resource specific parts of a VirtualResource
 */
template<typename RESOURCE>
class Resource : public VirtualResource {
    using Usage = typename RESOURCE::Usage;  // 使用方式类型别名

public:
    using Descriptor = typename RESOURCE::Descriptor;  // 描述符类型别名
    using SubResourceDescriptor = typename RESOURCE::SubResourceDescriptor;  // 子资源描述符类型别名

    /**
     * 具体资源对象
     * 
     * 仅在调用 devirtualize() 后有效。
     */
    // valid only after devirtualize() has been called
    RESOURCE resource{};  // 具体资源对象

    /**
     * 使用方式
     * 
     * 仅在调用 resolveUsage() 后有效。
     */
    // valid only after resolveUsage() has been called
    Usage usage{};  // 使用方式

    /**
     * 我们的具体（子）资源描述符——用于创建它
     */
    // our concrete (sub)resource descriptors -- used to create it.
    Descriptor descriptor;  // 资源描述符
    SubResourceDescriptor subResourceDescriptor;  // 子资源描述符

    /**
     * 资源是否已分离
     */
    // whether the resource was detached
    bool detached = false;  // 分离标志

    /**
     * 带有从此资源添加的数据的边
     */
    /**
     * 资源边类
     * 
     * 带有从此资源添加的数据的边。
     */
    // An Edge with added data from this resource
    class UTILS_PUBLIC ResourceEdge : public ResourceEdgeBase {
    public:
        Usage usage;  // 使用方式
        
        /**
         * 构造函数
         * 
         * @param graph 依赖图引用
         * @param from 源节点指针
         * @param to 目标节点指针
         * @param usage 使用方式
         */
        ResourceEdge(DependencyGraph& graph,
                DependencyGraph::Node* from, DependencyGraph::Node* to, Usage usage) noexcept
                : ResourceEdgeBase(graph, from, to),  // 初始化基类
                  usage(usage) {  // 设置使用方式
        }
    };

    /**
     * 构造函数（根资源）
     * 
     * @param name 资源名称
     * @param desc 资源描述符
     */
    UTILS_NOINLINE
    Resource(utils::StaticString name, Descriptor const& desc) noexcept
        : VirtualResource(name),  // 初始化虚拟资源基类
          descriptor(desc) {  // 设置描述符
    }

    /**
     * 构造函数（子资源）
     * 
     * @param parent 父资源指针
     * @param name 资源名称
     * @param desc 子资源描述符
     */
    UTILS_NOINLINE
    Resource(Resource* parent, utils::StaticString name, SubResourceDescriptor const& desc) noexcept
            : VirtualResource(parent, name),  // 初始化虚拟资源基类
              descriptor(RESOURCE::generateSubResourceDescriptor(parent->descriptor, desc)),  // 生成子资源描述符
              subResourceDescriptor(desc) {  // 设置子资源描述符
    }

    /**
     * 析构函数
     */
    ~Resource() noexcept = default;

    /**
     * 连接（通道节点到资源节点，写入）
     * 
     * 创建从通道节点到资源节点的边（写入操作）。
     * 
     * @param graph 依赖图引用
     * @param passNode 通道节点指针
     * @param resourceNode 资源节点指针
     * @param u 使用方式
     * @return 如果连接成功返回 true
     * 
     * TODO: 我们应该检查使用标志是否正确（例如，写入标志不用于读取）
     */
    // pass Node to resource Node edge (a write to)
    UTILS_NOINLINE
    virtual bool connect(DependencyGraph& graph,
            PassNode* passNode, ResourceNode* resourceNode, Usage u) {
        // TODO: we should check that usage flags are correct (e.g. a write flag is not used for reading)
        /**
         * 检查是否已存在写入边
         */
        ResourceEdge* edge = static_cast<ResourceEdge*>(getWriterEdgeForPass(resourceNode, passNode));  // 获取写入边
        if (edge) {  // 如果边已存在
            edge->usage |= u;  // 更新使用方式（按位或）
        } else {  // 如果边不存在
            /**
             * 创建新的资源边
             */
            edge = new ResourceEdge(graph,  // 依赖图
                    toDependencyGraphNode(passNode),  // 源节点
                    toDependencyGraphNode(resourceNode),  // 目标节点
                    u);  // 使用方式
            setIncomingEdge(resourceNode, edge);  // 设置入边
        }
        return true;  // 返回成功
    }

    /**
     * 连接（资源节点到通道节点，读取）
     * 
     * 创建从资源节点到通道节点的边（读取操作）。
     * 
     * @param graph 依赖图引用
     * @param resourceNode 资源节点指针
     * @param passNode 通道节点指针
     * @param u 使用方式
     * @return 如果连接成功返回 true
     * 
     * TODO: 我们应该检查使用标志是否正确（例如，写入标志不用于读取）
     * 
     * 如果 passNode 已经是 resourceNode 的读取者，则只需更新使用标志
     */
    // resource Node to pass Node edge (a read from)
    UTILS_NOINLINE
    virtual bool connect(DependencyGraph& graph,
            ResourceNode* resourceNode, PassNode* passNode, Usage u) {
        // TODO: we should check that usage flags are correct (e.g. a write flag is not used for reading)
        // if passNode is already a reader of resourceNode, then just update the usage flags
        /**
         * 检查是否已存在读取边
         */
        ResourceEdge* edge = static_cast<ResourceEdge*>(getReaderEdgeForPass(resourceNode, passNode));  // 获取读取边
        if (edge) {  // 如果边已存在
            edge->usage |= u;  // 更新使用方式（按位或）
        } else {  // 如果边不存在
            /**
             * 创建新的资源边
             */
            edge = new ResourceEdge(graph,  // 依赖图
                    toDependencyGraphNode(resourceNode),  // 源节点
                    toDependencyGraphNode(passNode),  // 目标节点
                    u);  // 使用方式
            addOutgoingEdge(resourceNode, edge);  // 添加出边
        }
        return true;  // 返回成功
    }

protected:
    /**
     * 下面的虚函数必须在头文件中，因为 RESOURCE 仅在编译时已知
     */
    /*
     * The virtual below must be in a header file as RESOURCE is only known at compile time
     */

    /**
     * 解析使用方式（实现）
     * 
     * 根据所有边和写入边计算资源的有效使用方式。
     * 
     * @param graph 依赖图引用
     * @param edges 边数组指针
     * @param count 边数量
     * @param writer 写入边指针
     */
    void resolveUsage(DependencyGraph& graph,
            ResourceEdgeBase const* const* edges, size_t const count,
            ResourceEdgeBase const* writer) noexcept override {
        /**
         * 遍历所有边，累积使用方式
         */
        for (size_t i = 0; i < count; i++) {
            if (graph.isEdgeValid(edges[i])) {  // 如果边有效
                /**
                 * 这个边保证是 ResourceEdge<RESOURCE>（通过构造）
                 */
                // this Edge is guaranteed to be a ResourceEdge<RESOURCE> by construction
                ResourceEdge const* const edge = static_cast<ResourceEdge const*>(edges[i]);  // 转换为资源边
                usage |= edge->usage;  // 累积使用方式（按位或）
            }
        }

        /**
         * 这里不检查边的有效性，因为即使边无效，
         * 我们被调用（未被剔除）的事实意味着我们需要考虑它，
         * 例如，因为资源可能在渲染目标中需要
         */
        // here don't check for the validity of Edge because even if the edge is invalid
        // the fact that we're called (not culled) means we need to take it into account
        // e.g. because the resource could be needed in a render target
        if (writer) {  // 如果有写入边
            ResourceEdge const* const edge = static_cast<ResourceEdge const*>(writer);  // 转换为资源边
            usage |= edge->usage;  // 累积使用方式（按位或）
        }

        /**
         * 将使用标志传播到父资源
         */
        // propagate usage bits to the parents
        Resource* p = this;  // 从当前资源开始
        while (p != p->parent) {  // 当不是根资源时
            p = static_cast<Resource*>(p->parent);  // 向上查找父资源
            p->usage |= usage;  // 累积使用方式（按位或）
        }
    }

    /**
     * 销毁边（实现）
     * 
     * @param edge 边指针
     */
    void destroyEdge(DependencyGraph::Edge* edge) noexcept override {
        /**
         * 这个边保证是 ResourceEdge<RESOURCE>（通过构造）
         */
        // this Edge is guaranteed to be a ResourceEdge<RESOURCE> by construction
        delete static_cast<ResourceEdge *>(edge);  // 删除资源边
    }

    /**
     * 具体化资源（实现）
     * 
     * 从虚拟资源创建实际的硬件资源。
     * 
     * @param resourceAllocator 资源分配器接口
     * @param useProtectedMemory 是否使用受保护内存
     */
    void devirtualize(ResourceAllocatorInterface& resourceAllocator,
            bool useProtectedMemory) noexcept override {
        if (!isSubResource()) {  // 如果不是子资源
            resource.create(resourceAllocator, name, descriptor, usage, useProtectedMemory);  // 创建资源
        } else {  // 如果是子资源
            /**
             * 资源保证在我们之前通过构造初始化
             */
            // resource is guaranteed to be initialized before we are by construction
            resource = static_cast<Resource const*>(parent)->resource;  // 使用父资源的资源对象
        }
    }

    /**
     * 销毁资源（实现）
     * 
     * @param resourceAllocator 资源分配器接口
     */
    void destroy(ResourceAllocatorInterface& resourceAllocator) noexcept override {
        if (detached || isSubResource()) {  // 如果已分离或是子资源
            return;  // 不销毁（子资源不拥有资源对象）
        }
        resource.destroy(resourceAllocator);  // 销毁资源
    }

    /**
     * 获取使用方式字符串（实现）
     * 
     * @return 使用方式字符串
     */
    utils::CString usageString() const noexcept override {
        return utils::to_string(usage);  // 转换为字符串
    }
};

/**
 * 导入资源模板类
 * 
 * 导入资源就像常规资源一样，除了它是直接从具体资源构造的，
 * 并且显然不会创建/销毁具体资源。
 * 
 * @tparam RESOURCE 资源类型
 */
/*
 * An imported resource is just like a regular one, except that it's constructed directly from
 * the concrete resource and it, evidently, doesn't create/destroy the concrete resource.
 */
template<typename RESOURCE>
class ImportedResource : public Resource<RESOURCE> {
public:
    using Descriptor = typename RESOURCE::Descriptor;  // 描述符类型别名
    using Usage = typename RESOURCE::Usage;  // 使用方式类型别名

    /**
     * 构造函数
     * 
     * @param name 资源名称
     * @param desc 资源描述符
     * @param usage 使用方式
     * @param rsrc 具体资源对象
     */
    UTILS_NOINLINE
    ImportedResource(utils::StaticString name, Descriptor const& desc, Usage usage, RESOURCE const& rsrc) noexcept
            : Resource<RESOURCE>(name, desc) {  // 初始化基类
        this->resource = rsrc;  // 设置资源对象
        this->usage = usage;  // 设置使用方式
    }

protected:
    /**
     * 具体化资源（重写）
     * 
     * 导入资源不需要具体化。
     */
    void devirtualize(ResourceAllocatorInterface&, bool) noexcept override {
        // imported resources don't need to devirtualize
    }
    
    /**
     * 销毁资源（重写）
     * 
     * 导入资源从不销毁具体资源。
     */
    void destroy(ResourceAllocatorInterface&) noexcept override {
        // imported resources never destroy the concrete resource
    }

    /**
     * 检查是否为导入资源（重写）
     * 
     * @return 总是返回 true
     */
    bool isImported() const noexcept override { return true; }

    /**
     * 连接（通道到资源，写入）（重写）
     * 
     * 在连接前断言使用方式有效。
     */
    UTILS_NOINLINE
    bool connect(DependencyGraph& graph,
            PassNode* passNode, ResourceNode* resourceNode, FrameGraphTexture::Usage u) override {
        assertConnect(u);  // 断言使用方式有效
        return Resource<RESOURCE>::connect(graph, passNode, resourceNode, u);  // 调用基类方法
    }

    /**
     * 连接（资源到通道，读取）（重写）
     * 
     * 在连接前断言使用方式有效。
     */
    UTILS_NOINLINE
    bool connect(DependencyGraph& graph,
            ResourceNode* resourceNode, PassNode* passNode, FrameGraphTexture::Usage u) override {
        assertConnect(u);  // 断言使用方式有效
        return Resource<RESOURCE>::connect(graph, resourceNode, passNode, u);  // 调用基类方法
    }

private:
    /**
     * 断言连接使用方式
     * 
     * 确保请求的使用方式是导入资源可用使用方式的子集。
     * 
     * @param u 请求的使用方式
     */
    UTILS_NOINLINE
    void assertConnect(FrameGraphTexture::Usage u) {
        FILAMENT_CHECK_PRECONDITION((u & this->usage) == u)  // 检查使用方式是否包含在可用使用方式中
                << "Requested usage " << utils::to_string(u).c_str()
                << " not available on imported resource \"" << this->name.c_str() << "\" with usage "
                << utils::to_string(this->usage).c_str();
    }
};


class ImportedRenderTarget : public ImportedResource<FrameGraphTexture> {
public:
    backend::Handle<backend::HwRenderTarget> target;
    FrameGraphRenderPass::ImportDescriptor importedDesc;

    UTILS_NOINLINE
    ImportedRenderTarget(utils::StaticString name,
            FrameGraphTexture::Descriptor const& mainAttachmentDesc,
            FrameGraphRenderPass::ImportDescriptor const& importedDesc,
            backend::Handle<backend::HwRenderTarget> target);

    ~ImportedRenderTarget() noexcept override;

protected:
    UTILS_NOINLINE
    bool connect(DependencyGraph& graph,
            PassNode* passNode, ResourceNode* resourceNode, FrameGraphTexture::Usage u) override;

    UTILS_NOINLINE
    bool connect(DependencyGraph& graph,
            ResourceNode* resourceNode, PassNode* passNode, FrameGraphTexture::Usage u) override;

    ImportedRenderTarget* asImportedRenderTarget() noexcept override { return this; }

private:
    void assertConnect(FrameGraphTexture::Usage u);

    static FrameGraphTexture::Usage usageFromAttachmentsFlags(
            backend::TargetBufferFlags attachments) noexcept;
};

// ------------------------------------------------------------------------------------------------

// prevent implicit instantiation of Resource<FrameGraphTexture> which is a known type
extern template class Resource<FrameGraphTexture>;
extern template class ImportedResource<FrameGraphTexture>;

} // namespace filament

#endif // TNT_FILAMENT_FG_DETAILS_RESOURCE_H
