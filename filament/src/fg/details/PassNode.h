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

#ifndef TNT_FILAMENT_FG_DETAILS_PASSNODE_H
#define TNT_FILAMENT_FG_DETAILS_PASSNODE_H

#include "fg/details/DependencyGraph.h"
#include "fg/details/Utilities.h"
#include "fg/FrameGraph.h"
#include "fg/FrameGraphRenderPass.h"

#include "backend/DriverApiForward.h"

#include <backend/TargetBufferInfo.h>

#include <unordered_set>

namespace utils {
class CString;
} // namespace utils

namespace filament {

class FrameGraph;
class FrameGraphResources;
class FrameGraphPassExecutor;
class ResourceNode;

/**
 * 通道节点类
 * 
 * 表示帧图中的一个通道（Pass）。
 * 通道是帧图的基本执行单元，可以读取和写入资源。
 * 
 * 功能：
 * - 管理通道需要的资源
 * - 跟踪需要具体化和销毁的资源
 * - 提供执行和解析接口
 */
class PassNode : public DependencyGraph::Node {
protected:
    friend class FrameGraphResources;
    FrameGraph& mFrameGraph;  // 帧图引用
    std::unordered_set<FrameGraphHandle::Index> mDeclaredHandles;  // 已声明的资源句柄索引集合
public:
    /**
     * 构造函数
     * 
     * @param fg 帧图引用
     */
    explicit PassNode(FrameGraph& fg) noexcept;
    
    /**
     * 移动构造函数
     */
    PassNode(PassNode&& rhs) noexcept;
    
    /**
     * 禁止拷贝构造
     */
    PassNode(PassNode const&) = delete;
    
    /**
     * 禁止拷贝赋值
     */
    PassNode& operator=(PassNode const&) = delete;
    
    /**
     * 虚析构函数
     */
    ~PassNode() noexcept override;
    using NodeID = DependencyGraph::NodeID;

    /**
     * 注册资源
     * 
     * 将资源标记为被此通道需要。
     * 
     * @param resourceHandle 资源句柄
     */
    void registerResource(FrameGraphHandle resourceHandle) noexcept;

    /**
     * 执行通道（纯虚函数）
     * 
     * 子类必须实现此方法来执行实际的渲染操作。
     * 
     * @param resources 帧图资源引用
     * @param driver 驱动 API 引用
     */
    virtual void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept = 0;
    
    /**
     * 解析通道（纯虚函数）
     * 
     * 子类必须实现此方法来解析通道的配置（计算丢弃标志、视口等）。
     */
    virtual void resolve() noexcept = 0;
    
    /**
     * 生成 Graphviz 边颜色
     * 
     * @return 边颜色字符串
     */
    utils::CString graphvizifyEdgeColor() const noexcept override;

    Vector<VirtualResource*> devirtualize;         // 执行前需要创建的资源（具体化）
    Vector<VirtualResource*> destroy;              // 执行后需要销毁的资源
};

/**
 * 渲染通道节点类
 * 
 * 表示一个渲染通道，用于绘制到纹理附件。
 * 
 * 功能：
 * - 管理渲染目标和附件
 * - 处理渲染通道的配置和参数
 * - 支持导入外部渲染目标
 */
class RenderPassNode : public PassNode {
public:
    /**
     * 渲染通道数据结构
     * 
     * 存储渲染通道的所有配置和数据。
     */
    class RenderPassData {
    public:
        static constexpr size_t ATTACHMENT_COUNT = backend::MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 2;  // 附件数量
        utils::StaticString name{};  // 通道名称
        FrameGraphRenderPass::Descriptor descriptor;  // 渲染通道描述符
        bool imported = false;  // 是否为导入的渲染目标
        backend::TargetBufferFlags targetBufferFlags = {};  // 目标缓冲区标志
        FrameGraphId<FrameGraphTexture> attachmentInfo[ATTACHMENT_COUNT] = {};  // 附件信息数组
        ResourceNode* incoming[ATTACHMENT_COUNT] = {};  // 入边附件节点（读取源）
        ResourceNode* outgoing[ATTACHMENT_COUNT] = {};  // 出边附件节点（写入目标）
        
        /**
         * 后端数据结构
         * 
         * 存储硬件渲染目标和渲染参数。
         */
        struct {
            backend::Handle<backend::HwRenderTarget> target;  // 渲染目标句柄
            backend::RenderPassParams params;  // 渲染参数
        } backend;

        /**
         * 具体化渲染目标
         * 
         * 从虚拟资源创建实际的渲染目标。
         * 
         * @param fg 帧图引用
         * @param resourceAllocator 资源分配器接口
         */
        void devirtualize(FrameGraph& fg, ResourceAllocatorInterface& resourceAllocator) noexcept;
        
        /**
         * 销毁渲染目标
         * 
         * 释放渲染目标资源。
         * 
         * @param resourceAllocator 资源分配器接口
         */
        void destroy(ResourceAllocatorInterface& resourceAllocator) const noexcept;
    };

    /**
     * 构造函数
     * 
     * @param fg 帧图引用
     * @param name 通道名称
     * @param base 通道基类指针
     */
    RenderPassNode(FrameGraph& fg, const char* name, FrameGraphPassBase* base) noexcept;
    
    /**
     * 移动构造函数
     */
    RenderPassNode(RenderPassNode&& rhs) noexcept;
    
    /**
     * 析构函数
     */
    ~RenderPassNode() noexcept override;

    /**
     * 声明渲染目标
     * 
     * 在通道中声明一个渲染目标。
     * 
     * @param fg 帧图引用
     * @param builder 帧图构建器引用
     * @param name 渲染目标名称
     * @param descriptor 渲染通道描述符
     * @return 渲染目标 ID
     */
    uint32_t declareRenderTarget(FrameGraph& fg, FrameGraph::Builder& builder,
            utils::StaticString name, FrameGraphRenderPass::Descriptor const& descriptor);

    /**
     * 获取渲染通道数据
     * 
     * @param id 渲染目标 ID
     * @return 渲染通道数据常量指针
     */
    RenderPassData const* getRenderPassData(uint32_t id) const noexcept;

private:
    // virtuals from DependencyGraph::Node
    char const* getName() const noexcept override { return mName; }
    utils::CString graphvizify() const noexcept override;
    void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept override;
    void resolve() noexcept override;

    // constants
    const char* const mName = nullptr;
    UniquePtr<FrameGraphPassBase, LinearAllocatorArena> mPassBase;

    // set during setup
    std::vector<RenderPassData> mRenderTargetData;
};

class PresentPassNode : public PassNode {
public:
    explicit PresentPassNode(FrameGraph& fg) noexcept;
    PresentPassNode(PresentPassNode&& rhs) noexcept;
    ~PresentPassNode() noexcept override;
    PresentPassNode(PresentPassNode const&) = delete;
    PresentPassNode& operator=(PresentPassNode const&) = delete;
    void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept override;
    void resolve() noexcept override;
private:
    // virtuals from DependencyGraph::Node
    char const* getName() const noexcept override;
    utils::CString graphvizify() const noexcept override;
};

} // namespace filament

#endif // TNT_FILAMENT_FG_DETAILS_PASSNODE_H
