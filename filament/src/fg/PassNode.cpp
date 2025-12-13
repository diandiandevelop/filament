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

#include "fg/details/PassNode.h"

#include "fg/FrameGraph.h"
#include "fg/details/ResourceNode.h"

#include "ResourceAllocator.h"

#include <details/Texture.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/CString.h>

#include <string>

using namespace filament::backend;

namespace filament {

/**
 * 通道节点构造函数
 * 
 * 创建通道节点并初始化相关数据结构。
 * 
 * @param fg 帧图引用
 */
PassNode::PassNode(FrameGraph& fg) noexcept
        : Node(fg.getGraph()),  // 初始化依赖图节点
          mFrameGraph(fg),  // 保存帧图引用
          devirtualize(fg.getArena()),  // 初始化具体化函数对象（使用 Arena 分配器）
          destroy(fg.getArena()) {  // 初始化销毁函数对象（使用 Arena 分配器）
}

/**
 * 移动构造函数
 * 
 * @param rhs 右值引用
 */
PassNode::PassNode(PassNode&& rhs) noexcept = default;

/**
 * 析构函数
 */
PassNode::~PassNode() noexcept = default;

/**
 * 生成 Graphviz 边颜色
 * 
 * 返回用于 Graphviz 可视化的边颜色。
 * 
 * @return 边颜色字符串（红色）
 */
utils::CString PassNode::graphvizifyEdgeColor() const noexcept {
    return utils::CString{"red"};  // 通道节点使用红色边
}

/**
 * 注册资源
 * 
 * 将资源标记为被此通道需要，并更新资源的生命周期信息。
 * 
 * @param resourceHandle 资源句柄
 */
void PassNode::registerResource(FrameGraphHandle const resourceHandle) noexcept {
    VirtualResource* resource = mFrameGraph.getResource(resourceHandle);  // 获取虚拟资源
    resource->neededByPass(this);  // 标记资源被此通道需要
    mDeclaredHandles.insert(resourceHandle.index);  // 将资源索引添加到已声明句柄集合
}

// ------------------------------------------------------------------------------------------------

/**
 * 渲染通道节点构造函数
 * 
 * 创建渲染通道节点。
 * 
 * @param fg 帧图引用
 * @param name 通道名称
 * @param base 通道基类指针
 */
RenderPassNode::RenderPassNode(FrameGraph& fg, const char* name, FrameGraphPassBase* base) noexcept
        : PassNode(fg),  // 初始化通道节点基类
          mName(name),  // 保存名称
          mPassBase(base, fg.getArena()) {  // 保存通道基类指针（使用 Arena 分配器）
}
/**
 * 移动构造函数
 * 
 * @param rhs 右值引用
 */
RenderPassNode::RenderPassNode(RenderPassNode&& rhs) noexcept = default;

/**
 * 析构函数
 */
RenderPassNode::~RenderPassNode() noexcept = default;

/**
 * 执行渲染通道
 * 
 * 执行渲染通道的完整流程：
 * 1. 具体化渲染目标（从虚拟资源创建具体资源）
 * 2. 执行通道（调用用户代码）
 * 3. 销毁渲染目标（释放临时资源）
 * 
 * @param resources 帧图资源引用
 * @param driver 驱动 API 引用
 */
void RenderPassNode::execute(FrameGraphResources const& resources, DriverApi& driver) noexcept {

    FrameGraph& fg = mFrameGraph;
    ResourceAllocatorInterface& resourceAllocator = fg.getResourceAllocator();

    /**
     * 创建渲染目标
     * 
     * 将虚拟资源具体化为实际的渲染目标。
     */
    // create the render targets
    for (auto& rt : mRenderTargetData) {
        rt.devirtualize(fg, resourceAllocator);  // 具体化渲染目标
    }

    /**
     * 执行通道
     * 
     * 调用用户提供的执行函数。
     */
    mPassBase->execute(resources, driver);

    /**
     * 销毁渲染目标
     * 
     * 释放临时创建的渲染目标资源。
     */
    // destroy the render targets
    for (auto& rt : mRenderTargetData) {
        rt.destroy(resourceAllocator);  // 销毁渲染目标
    }
}

/**
 * 声明渲染目标
 * 
 * 为渲染通道声明一个渲染目标，并收集附件的资源节点信息。
 * 这些信息将用于后续计算丢弃标志。
 * 
 * @param fg 帧图引用
 * @param name 渲染目标名称
 * @param descriptor 渲染通道描述符
 * @return 渲染目标数据索引
 */
uint32_t RenderPassNode::declareRenderTarget(FrameGraph& fg, FrameGraph::Builder&,
        utils::StaticString name, FrameGraphRenderPass::Descriptor const& descriptor) {

    RenderPassData data;  // 创建渲染通道数据
    data.name = name;  // 设置名称
    data.descriptor = descriptor;  // 设置描述符

    /**
     * 检索传入附件的 ResourceNode，这将用于后续计算丢弃标志
     */
    // retrieve the ResourceNode of the attachments coming to us -- this will be used later
    // to compute the discard flags.

    DependencyGraph const& dependencyGraph = fg.getGraph();  // 获取依赖图
    auto incomingEdges = dependencyGraph.getIncomingEdges(this);  // 获取传入边

    /**
     * 遍历所有附件
     */
    for (size_t i = 0; i < RenderPassData::ATTACHMENT_COUNT; i++) {
        FrameGraphId<FrameGraphTexture> const& handle =
                data.descriptor.attachments[i];  // 获取附件句柄
        if (handle) {  // 如果附件存在
            data.attachmentInfo[i] = handle;  // 保存附件信息

            /**
             * 查找传入边中与此附件对应的资源节点
             * TODO: 这效率不高
             */
            // TODO: this is not very efficient
            auto incomingPos = std::find_if(incomingEdges.begin(), incomingEdges.end(),  // 查找边
                    [&dependencyGraph, handle]  // Lambda 捕获
                            (DependencyGraph::Edge const* edge) {  // Lambda 参数
                        ResourceNode const* node = static_cast<ResourceNode const*>(  // 转换为资源节点
                                dependencyGraph.getNode(edge->from));  // 获取源节点
                        return node->resourceHandle == handle;  // 检查资源句柄是否匹配
                    });

            if (incomingPos != incomingEdges.end()) {  // 如果找到传入边
                data.incoming[i] = const_cast<ResourceNode*>(  // 保存传入资源节点
                        static_cast<ResourceNode const*>(
                                dependencyGraph.getNode((*incomingPos)->from)));  // 获取源节点
            }

            /**
             * 这可能是传出或传入（如果没有传出）
             */
            // this could be either outgoing or incoming (if there are no outgoing)
            data.outgoing[i] = fg.getActiveResourceNode(handle);  // 获取活动资源节点
            if (data.outgoing[i] == data.incoming[i]) {  // 如果传出和传入相同
                data.outgoing[i] = nullptr;  // 清空传出节点
            }
        }
    }

    uint32_t const id = mRenderTargetData.size();  // 获取当前大小作为 ID
    mRenderTargetData.push_back(data);  // 添加到渲染目标数据列表
    return id;  // 返回索引
}

/**
 * 解析渲染目标
 * 
 * 解析渲染目标的所有属性，包括：
 * - 计算丢弃标志（开始和结束）
 * - 计算只读深度/模板标志
 * - 确定渲染目标尺寸
 * - 处理导入的渲染目标
 */
void RenderPassNode::resolve() noexcept {
    using namespace backend;

    /**
     * 遍历所有渲染目标数据
     */
    for (auto& rt : mRenderTargetData) {

        /**
         * 初始化尺寸范围
         */
        uint32_t minWidth = std::numeric_limits<uint32_t>::max();  // 最小宽度（初始化为最大值）
        uint32_t minHeight = std::numeric_limits<uint32_t>::max();  // 最小高度（初始化为最大值）
        uint32_t maxWidth = 0;  // 最大宽度（初始化为 0）
        uint32_t maxHeight = 0;  // 最大高度（初始化为 0）

        /**
         * 计算丢弃标志
         */
        /*
         * Compute discard flags
         */

        ImportedRenderTarget* pImportedRenderTarget = nullptr;  // 导入渲染目标指针（初始化为空）
        rt.backend.params.flags.discardStart    = TargetBufferFlags::NONE;  // 开始丢弃标志（初始化为无）
        rt.backend.params.flags.discardEnd      = TargetBufferFlags::NONE;  // 结束丢弃标志（初始化为无）
        rt.backend.params.readOnlyDepthStencil  = 0;  // 只读深度/模板标志（初始化为 0）

        /**
         * 定义深度和模板附件的索引
         */
        constexpr size_t DEPTH_INDEX = MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 0;  // 深度附件索引
        constexpr size_t STENCIL_INDEX = MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 1;  // 模板附件索引

        /**
         * 遍历所有附件（颜色 + 深度 + 模板）
         */
        for (size_t i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + 2; i++) {
            if (rt.descriptor.attachments[i]) {  // 如果附件存在
                const TargetBufferFlags target = getTargetBufferFlagsAt(i);  // 获取目标缓冲区标志

                rt.targetBufferFlags |= target;  // 累积目标缓冲区标志

                /**
                 * 计算结束丢弃标志
                 * 
                 * 仅在以下情况丢弃：
                 * - 我们正在写入此附件
                 * - 之后没有读取者
                 * 
                 * 注意：如果我们根本没有写入，不要丢弃，因为此附件可能在我们之后有其他读取者。
                 * 
                 * TODO: 如果我们是最后一个读取者，也可以设置丢弃标志，
                 *       即如果 rt->incoming[i] 的最后一个读取者是我们。
                 */
                // Discard at the end only if we are writing to this attachment AND later reading
                // from it. (in particular, don't discard if we're not writing at all, because this
                // attachment might have other readers after us).
                // TODO: we could set the discard flag if we are the last reader, i.e.
                //       if rt->incoming[i] last reader is us.
                if (rt.outgoing[i] && !rt.outgoing[i]->hasActiveReaders()) {  // 有出边且没有活跃读取者
                    rt.backend.params.flags.discardEnd |= target;  // 设置结束丢弃标志
                }
                
                /**
                 * 计算只读深度/模板标志
                 * 
                 * 如果附件没有出边或没有写入通道，则标记为只读。
                 */
                if (!rt.outgoing[i] || !rt.outgoing[i]->hasWriterPass()) {
                    if (i == DEPTH_INDEX) {
                        rt.backend.params.readOnlyDepthStencil |= RenderPassParams::READONLY_DEPTH;  // 只读深度
                    } else if (i == STENCIL_INDEX) {
                        rt.backend.params.readOnlyDepthStencil |= RenderPassParams::READONLY_STENCIL;  // 只读模板
                    }
                }
                
                /**
                 * 计算开始丢弃标志
                 * 
                 * 如果此附件之前没有写入者，则在开始时丢弃。
                 */
                // Discard at the start if this attachment has no prior writer
                if (!rt.incoming[i] || !rt.incoming[i]->hasActiveWriters()) {  // 没有入边或没有活跃写入者
                    rt.backend.params.flags.discardStart |= target;  // 设置开始丢弃标志
                }
                /**
                 * 获取资源并检查是否为导入的渲染目标
                 */
                VirtualResource* pResource = mFrameGraph.getResource(rt.descriptor.attachments[i]);  // 获取虚拟资源
                Resource<FrameGraphTexture>* pTextureResource = static_cast<Resource<FrameGraphTexture>*>(pResource);  // 转换为纹理资源

                /**
                 * 检查是否为导入的渲染目标
                 */
                pImportedRenderTarget = pImportedRenderTarget ?
                        pImportedRenderTarget : pResource->asImportedRenderTarget();  // 如果还没有找到，尝试转换

                /**
                 * 更新附件采样数
                 * 
                 * 如果描述符中未指定采样数，且使用方式允许，则更新纹理资源的采样数。
                 */
                // update attachment sample count if not specified and usage permits it
                if (!rt.descriptor.samples &&  // 描述符中未指定采样数
                    none(pTextureResource->usage & TextureUsage::SAMPLEABLE)) {  // 且纹理不可采样
                    pTextureResource->descriptor.samples = rt.descriptor.samples;  // 更新采样数
                }

                /**
                 * 计算所有附件的最小/最大尺寸
                 * 
                 * 用于确定渲染目标的最终尺寸。
                 */
                // figure out the min/max dimensions across all attachments
                const uint32_t w = pTextureResource->descriptor.width;  // 纹理宽度
                const uint32_t h = pTextureResource->descriptor.height;  // 纹理高度
                minWidth = std::min(minWidth, w);  // 更新最小宽度
                maxWidth = std::max(maxWidth, w);  // 更新最大宽度
                minHeight = std::min(minHeight, h);  // 更新最小高度
                maxHeight = std::max(maxHeight, h);  // 更新最大高度
            }
        }
        /**
         * 清除操作也意味着开始丢弃
         * 
         * 如果附件被清除，则在开始时丢弃其内容。
         */
        // additionally, clear implies discardStart
        rt.backend.params.flags.discardStart |= (
                rt.descriptor.clearFlags & rt.targetBufferFlags);  // 清除标志与目标缓冲区标志的交集

        /**
         * 断言：所有附件的尺寸必须匹配
         */
        assert_invariant(minWidth == maxWidth);  // 宽度必须一致
        assert_invariant(minHeight == maxHeight);  // 高度必须一致
        assert_invariant(any(rt.targetBufferFlags));  // 必须至少有一个目标缓冲区标志

        /**
         * 如果所有附件的尺寸匹配，则渲染目标尺寸没有歧义。
         * 如果它们不匹配，我们选择一个能够容纳所有附件的尺寸。
         */
        // of all attachments size matches there are no ambiguity about the RT size.
        // if they don't match however, we select a size that will accommodate all attachments.
        uint32_t const width = maxWidth;  // 使用最大宽度
        uint32_t const height = maxHeight;  // 使用最大高度

        /**
         * 如果未指定尺寸（自动模式），则更新描述符
         */
        // Update the descriptor if no size was specified (auto mode)
        if (!rt.descriptor.viewport.width) {  // 如果宽度为 0
            rt.descriptor.viewport.width = width;  // 设置宽度
        }
        if (!rt.descriptor.viewport.height) {  // 如果高度为 0
            rt.descriptor.viewport.height = height;  // 设置高度
        }

        /**
         * 处理特殊的导入渲染目标
         * 为此，我们检查第一个颜色附件是否为 ImportedRenderTarget，
         * 并用导入目标的实际值覆盖我们刚刚计算的参数
         */
        /*
         * Handle the special imported render target
         * To do this we check the first color attachment for an ImportedRenderTarget
         * and we override the parameters we just calculated
         */

        if (pImportedRenderTarget) {  // 如果是导入渲染目标
            rt.imported = true;  // 标记为导入

            /**
             * 用导入目标的实际值覆盖我们刚刚计算的值
             */
            // override the values we just calculated with the actual values from the imported target
            rt.targetBufferFlags     = pImportedRenderTarget->importedDesc.attachments;  // 目标缓冲区标志
            rt.descriptor.viewport   = pImportedRenderTarget->importedDesc.viewport;  // 视口
            rt.descriptor.clearColor = pImportedRenderTarget->importedDesc.clearColor;  // 清除颜色
            rt.descriptor.clearFlags = pImportedRenderTarget->importedDesc.clearFlags;  // 清除标志
            rt.descriptor.samples    = pImportedRenderTarget->importedDesc.samples;  // 采样数
            rt.backend.target        = pImportedRenderTarget->target;  // 渲染目标句柄

            /**
             * 我们可能会多次到达这里，例如如果渲染目标被多个通道使用
             * （这暗示了读回操作）。在这种情况下，我们不希望第二次清除它，
             * 因此我们清除导入通道的清除标志。
             */
            // We could end-up here more than once, for instance if the rendertarget is used
            // by multiple passes (this would imply a read-back, btw). In this case, we don't want
            // to clear it the 2nd time, so we clear the imported pass's clear flags.
            pImportedRenderTarget->importedDesc.clearFlags = TargetBufferFlags::NONE;  // 清除导入描述符的清除标志

            /**
             * 但不要丢弃导入目标告诉我们要保留的附件
             */
            // but don't discard attachments the imported target tells us to keep
            rt.backend.params.flags.discardStart &= ~pImportedRenderTarget->importedDesc.keepOverrideStart;  // 保留开始丢弃覆盖
            rt.backend.params.flags.discardEnd   &= ~pImportedRenderTarget->importedDesc.keepOverrideEnd;  // 保留结束丢弃覆盖
        }

        /**
         * 设置后端参数
         */
        rt.backend.params.viewport = rt.descriptor.viewport;  // 视口
        rt.backend.params.clearColor = rt.descriptor.clearColor;  // 清除颜色
        rt.backend.params.flags.clear = rt.descriptor.clearFlags & rt.targetBufferFlags;  // 清除标志（与目标缓冲区标志的交集）
    }
}

/**
 * 具体化渲染目标
 * 
 * 将虚拟资源具体化为实际的渲染目标。
 * 
 * @param fg 帧图引用
 * @param resourceAllocator 资源分配器接口引用
 */
void RenderPassNode::RenderPassData::devirtualize(FrameGraph& fg,
        ResourceAllocatorInterface& resourceAllocator) noexcept {
    assert_invariant(any(targetBufferFlags));  // 断言至少有一个目标缓冲区标志
    if (UTILS_LIKELY(!imported)) {  // 如果不是导入的渲染目标（常见情况）

        /**
         * 收集颜色附件信息
         */
        MRT colorInfo{};  // 多渲染目标颜色信息
        for (size_t i = 0; i < MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT; i++) {  // 遍历所有颜色附件
            if (attachmentInfo[i]) {  // 如果附件存在
                auto const* pResource = static_cast<Resource<FrameGraphTexture> const*>(  // 转换为纹理资源
                        fg.getResource(attachmentInfo[i]));  // 获取资源
                colorInfo[i].handle = pResource->resource.handle;  // 纹理句柄
                colorInfo[i].level = pResource->subResourceDescriptor.level;  // Mip 级别
                colorInfo[i].layer = pResource->subResourceDescriptor.layer;  // 层索引
            }
        }

        /**
         * 收集深度和模板附件信息
         */
        TargetBufferInfo info[2] = {};  // 深度和模板信息（2 个元素）
        for (size_t i = 0; i < 2; i++) {  // 遍历深度和模板附件
            if (attachmentInfo[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + i]) {  // 如果附件存在
                auto const* pResource = static_cast<Resource<FrameGraphTexture> const*>(  // 转换为纹理资源
                        fg.getResource(attachmentInfo[MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT + i]));  // 获取资源
                info[i].handle = pResource->resource.handle;  // 纹理句柄
                info[i].level = pResource->subResourceDescriptor.level;  // Mip 级别
                info[i].layer = pResource->subResourceDescriptor.layer;  // 层索引
            }
        }

        /**
         * 创建渲染目标
         */
        backend.target = resourceAllocator.createRenderTarget(  // 创建渲染目标
                name, targetBufferFlags,  // 名称和目标缓冲区标志
                backend.params.viewport.width,  // 视口宽度
                backend.params.viewport.height,  // 视口高度
                descriptor.samples, descriptor.layerCount,  // 采样数和层数
                colorInfo, info[0], info[1]);  // 颜色、深度和模板信息
    }
}

/**
 * 销毁渲染目标
 * 
 * 释放渲染目标资源。
 * 
 * @param resourceAllocator 资源分配器接口引用
 */
void RenderPassNode::RenderPassData::destroy(
        ResourceAllocatorInterface& resourceAllocator) const noexcept {
    if (UTILS_LIKELY(!imported)) {  // 如果不是导入的渲染目标（常见情况）
        resourceAllocator.destroyRenderTarget(backend.target);  // 销毁渲染目标
    }
}

/**
 * 获取渲染通道数据
 * 
 * 根据索引获取渲染通道数据。
 * 
 * @param id 渲染目标数据索引
 * @return 渲染通道数据常量指针（如果索引无效则返回 nullptr）
 */
RenderPassNode::RenderPassData const* RenderPassNode::getRenderPassData(uint32_t const id) const noexcept {
    return id < mRenderTargetData.size() ? &mRenderTargetData[id] : nullptr;  // 如果索引有效则返回数据，否则返回 nullptr
}

/**
 * Graphviz 可视化
 * 
 * 生成用于 Graphviz 可视化的节点标签字符串（仅在调试模式下）。
 * 
 * @return Graphviz 节点标签字符串
 */
utils::CString RenderPassNode::graphvizify() const noexcept {
#ifndef NDEBUG  // 仅在调试模式下
    utils::CString s;  // 创建字符串

    uint32_t const id = getId();  // 获取节点 ID
    const char* const nodeName = getName();  // 获取节点名称
    uint32_t const refCount = getRefCount();  // 获取引用计数

    /**
     * 构建 Graphviz 节点标签
     */
    s.append("[label=\"");  // 开始标签
    s.append(nodeName);  // 节点名称
    s.append("\\nrefs: ");  // 引用计数标签
    s.append(utils::to_string(refCount));  // 引用计数
    s.append(", id: ");  // ID 标签
    s.append(utils::to_string(id));  // 节点 ID

    /**
     * 添加渲染目标数据信息
     */
    for (auto const& rt :mRenderTargetData) {  // 遍历渲染目标数据
        s.append("\\nS:");  // 开始丢弃标志标签
        s.append(utils::to_string(rt.backend.params.flags.discardStart));  // 开始丢弃标志
        s.append(", E:");  // 结束丢弃标志标签
        s.append(utils::to_string(rt.backend.params.flags.discardEnd));  // 结束丢弃标志
        s.append(", C:");  // 清除标志标签
        s.append(utils::to_string(rt.backend.params.flags.clear));  // 清除标志
    }

    s.append("\", ");  // 结束标签

    /**
     * 设置节点样式和填充颜色
     */
    s.append("style=filled, fillcolor=");  // 样式和填充颜色
    s.append(refCount ? "darkorange" : "darkorange4");  // 根据引用计数选择颜色
    s.append("]");  // 结束样式

    return s;  // 返回字符串
#else  // 非调试模式
    return {};  // 返回空字符串
#endif
}

// ------------------------------------------------------------------------------------------------

/**
 * 呈现通道节点构造函数
 * 
 * 创建呈现通道节点。
 * 
 * @param fg 帧图引用
 */
PresentPassNode::PresentPassNode(FrameGraph& fg) noexcept
        : PassNode(fg) {  // 初始化通道节点基类
}

/**
 * 移动构造函数
 * 
 * @param rhs 右值引用
 */
PresentPassNode::PresentPassNode(PresentPassNode&& rhs) noexcept = default;

/**
 * 析构函数
 */
PresentPassNode::~PresentPassNode() noexcept = default;

/**
 * 获取名称
 * 
 * @return 通道名称（"Present"）
 */
char const* PresentPassNode::getName() const noexcept {
    return "Present";  // 返回呈现通道名称
}

/**
 * Graphviz 可视化
 * 
 * 生成用于 Graphviz 可视化的节点标签字符串（仅在调试模式下）。
 * 
 * @return Graphviz 节点标签字符串
 */
utils::CString PresentPassNode::graphvizify() const noexcept {
#ifndef NDEBUG  // 仅在调试模式下
    utils::CString s;  // 创建字符串
    uint32_t const id = getId();  // 获取节点 ID
    s.append("[label=\"Present , id: ");  // 开始标签
    s.append(utils::to_string(id));  // 节点 ID
    s.append("\", style=filled, fillcolor=red3]");  // 样式和填充颜色（红色）
    return s;  // 返回字符串
#else  // 非调试模式
    return {};  // 返回空字符串
#endif
}

/**
 * 执行呈现通道
 * 
 * 呈现通道不需要执行任何操作，因为呈现由驱动自动处理。
 * 
 * @param resources 帧图资源常量引用（未使用）
 * @param driver 驱动 API 引用（未使用）
 */
void PresentPassNode::execute(FrameGraphResources const&, DriverApi&) noexcept {
    // 呈现通道不需要执行任何操作
}

/**
 * 解析呈现通道
 * 
 * 呈现通道不需要解析任何内容。
 */
void PresentPassNode::resolve() noexcept {
    // 呈现通道不需要解析
}

} // namespace filament
