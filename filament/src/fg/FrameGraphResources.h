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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPHRESOURCES_H
#define TNT_FILAMENT_FG_FRAMEGRAPHRESOURCES_H

#include "fg/details/Resource.h"
#include "fg/FrameGraphId.h"

#include "backend/DriverEnums.h"
#include "backend/Handle.h"

namespace filament {

class FrameGraph;
class PassNode;
class VirtualResource;
struct FrameGraphRenderPass;

/**
 * 帧图资源访问类
 * 
 * 用于在执行阶段检索具体资源。
 * 提供类型安全的资源访问接口。
 */
class FrameGraphResources {
public:
    /**
     * 构造函数
     * 
     * @param fg 帧图引用
     * @param passNode 通道节点引用
     */
    FrameGraphResources(FrameGraph& fg, PassNode& passNode) noexcept;
    
    /**
     * 禁止拷贝构造
     */
    FrameGraphResources(FrameGraphResources const&) = delete;
    
    /**
     * 禁止拷贝赋值
     */
    FrameGraphResources& operator=(FrameGraphResources const&) = delete;

    /**
     * 渲染通道信息结构
     * 
     * 包含渲染目标和渲染参数。
     */
    struct RenderPassInfo {
        backend::Handle<backend::HwRenderTarget> target;  // 渲染目标句柄
        backend::RenderPassParams params;  // 渲染参数
    };

    /**
     * 获取正在执行的通道名称
     * 
     * @return 指向以 null 结尾的字符串的指针。调用者不拥有所有权。
     */
    const char* getPassName() const noexcept;

    /**
     * 获取具体资源
     * 
     * 根据虚拟资源句柄检索具体资源。
     * 
     * @tparam RESOURCE 资源类型
     * @param handle 虚拟资源句柄
     * @return 具体资源的常量引用
     */
    template<typename RESOURCE>
    RESOURCE const& get(FrameGraphId<RESOURCE> handle) const;

    /**
     * 获取资源描述符
     * 
     * 获取与资源关联的描述符。
     * 
     * @tparam RESOURCE 资源类型
     * @param handle 虚拟资源句柄
     * @return 描述符的常量引用
     */
    template<typename RESOURCE>
    typename RESOURCE::Descriptor const& getDescriptor(FrameGraphId<RESOURCE> handle) const;

    /**
     * 获取子资源描述符
     * 
     * 获取与子资源关联的描述符。
     * 
     * @tparam RESOURCE 资源类型
     * @param handle 虚拟资源句柄
     * @return 子资源描述符的常量引用
     */
    template<typename RESOURCE>
    typename RESOURCE::SubResourceDescriptor const& getSubResourceDescriptor(
            FrameGraphId<RESOURCE> handle) const;

    /**
     * 获取资源使用方式
     * 
     * 获取与资源关联的使用方式。
     * 
     * @tparam RESOURCE 资源类型
     * @param handle 虚拟资源句柄
     * @return 使用方式的常量引用
     */
    template<typename RESOURCE>
    typename RESOURCE::Usage const& getUsage(FrameGraphId<RESOURCE> handle) const;

    /**
     * 分离资源
     * 
     * 从帧图中分离（导出）资源，此时其生命周期不再由 FrameGraph 管理。
     * 此资源稍后可以使用 FrameGraph::import() 再次被 FrameGraph 使用，
     * 但注意这不会将生命周期管理转移回 FrameGraph。
     *
     * @tparam RESOURCE 资源类型
     * @param handle 虚拟资源句柄
     * @param pOutResource 返回时填充导出的资源
     * @param pOutDescriptor 返回时填充导出的资源描述符
     */
    template<typename RESOURCE>
    void detach(FrameGraphId<RESOURCE> handle,
            RESOURCE* pOutResource, typename RESOURCE::Descriptor* pOutDescriptor) const;

    /**
     * 获取渲染通道信息
     * 
     * 获取与 Builder::userRenderTarget() 关联的渲染通道信息。
     * 
     * @param id Builder::userRenderTarget() 返回的标识符
     * @return 适合创建渲染通道的 RenderPassInfo 结构
     */
    RenderPassInfo getRenderPassInfo(uint32_t id = 0u) const;

    /**
     * 获取纹理句柄（辅助函数）
     * 
     * 检索 FrameGraphTexture 资源的句柄。
     * 
     * @param handle FrameGraphTexture 句柄
     * @return 后端具体纹理句柄
     */
    backend::Handle<backend::HwTexture> getTexture(FrameGraphId<FrameGraphTexture> const handle) const {
        return get(handle).handle;  // 返回纹理的硬件句柄
    }

private:
    VirtualResource& getResource(FrameGraphHandle handle) const;

    FrameGraph& mFrameGraph;
    PassNode& mPassNode;
};

// ------------------------------------------------------------------------------------------------

/**
 * 获取具体资源（模板实现）
 * 
 * 从虚拟资源获取具体资源对象。
 */
template<typename RESOURCE>
RESOURCE const& FrameGraphResources::get(FrameGraphId<RESOURCE> handle) const {
    return static_cast<Resource<RESOURCE> const&>(getResource(handle)).resource;  // 返回具体资源
}

/**
 * 获取资源描述符（模板实现）
 */
template<typename RESOURCE>
typename RESOURCE::Descriptor const& FrameGraphResources::getDescriptor(
        FrameGraphId<RESOURCE> handle) const {
    return static_cast<Resource<RESOURCE> const&>(getResource(handle)).descriptor;  // 返回描述符
}

/**
 * 获取子资源描述符（模板实现）
 */
template<typename RESOURCE>
typename RESOURCE::SubResourceDescriptor const& FrameGraphResources::getSubResourceDescriptor(
        FrameGraphId<RESOURCE> handle) const {
    return static_cast<Resource<RESOURCE> const&>(getResource(handle)).subResourceDescriptor;  // 返回子资源描述符
}

/**
 * 获取资源使用方式（模板实现）
 */
template<typename RESOURCE>
typename RESOURCE::Usage const& FrameGraphResources::getUsage(
        FrameGraphId<RESOURCE> handle) const {
    return static_cast<Resource<RESOURCE> const&>(getResource(handle)).usage;  // 返回使用方式
}

/**
 * 分离资源（模板实现）
 * 
 * 标记资源为已分离，并复制资源数据到输出参数。
 */
template<typename RESOURCE>
void FrameGraphResources::detach(FrameGraphId<RESOURCE> handle, RESOURCE* pOutResource,
        typename RESOURCE::Descriptor* pOutDescriptor) const {
    Resource<RESOURCE>& concrete = static_cast<Resource<RESOURCE>&>(getResource(handle));  // 获取具体资源
    concrete.detached = true;  // 标记为已分离
    assert_invariant(pOutResource);  // 断言输出指针有效
    *pOutResource = concrete.resource;  // 复制资源
    if (pOutDescriptor) {
        *pOutDescriptor = concrete.descriptor;  // 复制描述符（如果提供）
    }
}

} // namespace filament

#endif //TNT_FILAMENT_FG_FRAMEGRAPHRESOURCES_H
