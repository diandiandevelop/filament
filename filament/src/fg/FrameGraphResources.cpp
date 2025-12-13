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
#include "fg/FrameGraphResources.h"
#include "fg/details/PassNode.h"
#include "fg/details/ResourceNode.h"

namespace filament {

/**
 * 帧图资源构造函数
 * 
 * 创建帧图资源对象，用于在通道执行期间访问资源。
 * 
 * @param fg 帧图引用
 * @param passNode 通道节点引用
 */
FrameGraphResources::FrameGraphResources(FrameGraph& fg, PassNode& passNode) noexcept
    : mFrameGraph(fg),  // 保存帧图引用
      mPassNode(passNode) {  // 保存通道节点引用
}

/**
 * 获取通道名称
 * 
 * @return 通道名称 C 字符串
 */
const char* FrameGraphResources::getPassName() const noexcept {
    return mPassNode.getName();  // 返回通道节点名称
}

/**
 * 获取资源
 * 
 * 获取虚拟资源引用。此方法返回引用而不是指针，以表达如果失败必须断言（或抛出异常），
 * 不能返回 nullptr，因为公共 API 不返回指针。
 * 我们仍然使用 FILAMENT_CHECK_PRECONDITION()，因为这些失败是由于后置条件未满足。
 * 
 * @param handle 帧图句柄
 * @return 虚拟资源引用
 */
// this perhaps weirdly returns a reference, this is to express the fact that if this method
// fails, it has to assert (or throw), it can't return for e.g. a nullptr, because the public
// API doesn't return pointers.
// We still use FILAMENT_CHECK_PRECONDITION() because these failures are due to post conditions not met.
VirtualResource& FrameGraphResources::getResource(FrameGraphHandle const handle) const {
    FILAMENT_CHECK_PRECONDITION(handle) << "Uninitialized handle when using FrameGraphResources.";  // 检查句柄已初始化

    VirtualResource* const resource = mFrameGraph.getResource(handle);  // 获取虚拟资源

    auto& declaredHandles = mPassNode.mDeclaredHandles;  // 获取已声明句柄集合
    const bool hasReadOrWrite = declaredHandles.find(handle.index) != declaredHandles.cend();  // 检查是否已声明访问

    FILAMENT_CHECK_PRECONDITION(hasReadOrWrite)  // 检查已声明访问
            << "Pass \"" << mPassNode.getName() << "\" didn't declare any access to resource \""
            << resource->name.c_str() << "\"";

    assert_invariant(resource->refcount);  // 断言引用计数大于 0

    return *resource;  // 返回资源引用
}

/**
 * 获取渲染通道信息
 * 
 * 获取渲染通道的后端渲染目标和参数。
 * 
 * @param id 渲染目标数据索引
 * @return 渲染通道信息（包含渲染目标句柄和参数）
 */
FrameGraphResources::RenderPassInfo FrameGraphResources::getRenderPassInfo(uint32_t const id) const {
    /**
     * 此转换是安全的，因为只能从 RenderPassNode 调用此方法
     */
    // this cast is safe because this can only be called from a RenderPassNode
    RenderPassNode const& renderPassNode = static_cast<RenderPassNode const&>(mPassNode);  // 转换为渲染通道节点
    RenderPassNode::RenderPassData const* pRenderPassData = renderPassNode.getRenderPassData(id);  // 获取渲染通道数据

    FILAMENT_CHECK_PRECONDITION(pRenderPassData) << "using invalid RenderPass index " << id  // 检查数据有效
                                                 << " in Pass \"" << mPassNode.getName() << "\"";

    return { pRenderPassData->backend.target, pRenderPassData->backend.params };  // 返回渲染目标和参数
}

} // namespace filament
