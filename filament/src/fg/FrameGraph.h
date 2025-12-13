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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPH_H
#define TNT_FILAMENT_FG_FRAMEGRAPH_H

#include "Allocators.h"

#include "fg/Blackboard.h"
#include "fg/FrameGraphId.h"
#include "fg/FrameGraphPass.h"
#include "fg/FrameGraphRenderPass.h"
#include "fg/FrameGraphTexture.h"

#include "fg/details/DependencyGraph.h"
#include "fg/details/Resource.h"
#include "fg/details/Utilities.h"

#include "backend/DriverApiForward.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <functional>

#if FILAMENT_ENABLE_FGVIEWER
#include <fgviewer/FrameGraphInfo.h>
#else
namespace filament::fgviewer {
    class FrameGraphInfo{};
} // namespace filament::fgviewer
#endif

namespace filament {

class ResourceAllocatorInterface;

class FrameGraphPassExecutor;
class PassNode;
class ResourceNode;
class VirtualResource;

class FrameGraph {
public:

    /**
     * FrameGraph 构建器
     * 
     * 用于在添加通道时声明资源使用。
     * 每个通道都会获得一个 Builder 实例，用于声明该通道读取和写入的资源。
     * 
     * 实现细节：
     * - 资源版本管理：每次 read/write 都会创建新版本的资源句柄
     * - 依赖关系：自动建立通道和资源之间的依赖关系
     * - 生命周期：Builder 的生命周期仅限于 setup lambda 的执行期间
     */
    class Builder {
    public:
        Builder(Builder const&) = delete;  // 禁止拷贝构造
        Builder& operator=(Builder const&) = delete;  // 禁止拷贝赋值

        /**
         * 声明渲染通道
         * 
         * 为此通道声明一个 FrameGraphRenderPass。
         * 在此调用后，所有子资源句柄都会获得新版本。
         * 新值在返回的 FrameGraphRenderPass 结构中可用。
         * 
         * 重要：declareRenderPass() 不会假设其附件有 read() 或 write()，
         * 这些必须在调用 declareRenderPass() 之前单独发出。
         * 
         * @param name 指向以 null 结尾的字符串的指针。
         *              指针的生命周期必须延伸到 execute() 之后
         * @param desc FrameGraphRenderPass 的描述符
         * @return 索引，用于在执行阶段检索具体的 FrameGraphRenderPass
         */
        uint32_t declareRenderPass(utils::StaticString name,
                FrameGraphRenderPass::Descriptor const& desc);

        /**
         * 声明渲染通道（辅助方法，单颜色附件）
         * 
         * 辅助方法，用于轻松声明具有单个颜色目标附件的 FrameGraphRenderPass。
         * 
         * 这等价于：
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         *      color = write(color, FrameGraphTexture::Usage::COLOR_ATTACHMENT);
         *      auto id = declareRenderPass(getName(color),
         *              {.attachments = {.color = {color}}});
         *      if (index) *index = id;
         *      return color;
         * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
         * 
         * @param color 颜色附件子资源的句柄
         * @param index FrameGraphRenderPass 的索引（可选输出参数）
         * @return 颜色附件子资源的新版本句柄
         */
        FrameGraphId<FrameGraphTexture> declareRenderPass(
                FrameGraphId<FrameGraphTexture> color, uint32_t* index = nullptr);

        /**
         * 创建虚拟资源
         * 
         * 创建类型为 RESOURCE 的虚拟资源。
         * 虚拟资源在编译阶段之前不会分配实际的 GPU 资源。
         * 
         * @tparam RESOURCE 要创建的资源类型
         * @param name 指向以 null 结尾的字符串的指针。
         *             指针的生命周期必须延伸到 execute() 之后
         * @param desc 此资源的描述符（默认为空描述符）
         * @return 类型化的资源句柄
         */
        template<typename RESOURCE>
        FrameGraphId<RESOURCE> create(utils::StaticString name,
                typename RESOURCE::Descriptor const& desc = {}) noexcept {
            return mFrameGraph.create<RESOURCE>(name, desc);  // 委托给 FrameGraph
        }


        /**
         * 创建子资源
         * 
         * 创建类型为 RESOURCE 的虚拟资源的子资源。
         * 这会在子资源和资源之间添加引用关系。
         * 
         * 子资源通常用于表示资源的一部分（例如，纹理的特定 mip 级别或层）。
         * 
         * @tparam RESOURCE 虚拟资源的类型
         * @param parent 父资源句柄的指针。这将被更新为新版本
         * @param name 子资源的名称
         * @param desc 子资源的描述符（默认为空描述符）
         * @return 子资源的句柄
         */
        template<typename RESOURCE>
        inline FrameGraphId<RESOURCE> createSubresource(FrameGraphId<RESOURCE> parent,
                utils::StaticString name,
                typename RESOURCE::SubResourceDescriptor const& desc = {}) noexcept {
            return mFrameGraph.createSubresource<RESOURCE>(parent, name, desc);  // 委托给 FrameGraph
        }


        /**
         * 声明读取访问
         * 
         * 声明此通道对虚拟资源的读取访问。
         * 这会在通道和资源之间添加引用关系（从通道到资源）。
         * 
         * 重要：输入句柄在此调用后不再有效，必须使用返回的新句柄。
         * 
         * @tparam RESOURCE 资源类型
         * @param input 资源的句柄
         * @param usage 此资源的使用方式。
         *              例如：对于纹理，可以是 sample（采样）或 upload（上传）。
         *              这取决于资源类型（默认为默认读取使用方式）
         * @return 资源的新句柄。输入句柄不再有效
         */
        template<typename RESOURCE>
        inline FrameGraphId<RESOURCE> read(FrameGraphId<RESOURCE> input,
                typename RESOURCE::Usage usage = RESOURCE::DEFAULT_R_USAGE) {
            return mFrameGraph.read<RESOURCE>(mPassNode, input, usage);  // 委托给 FrameGraph
        }

        /**
         * 声明写入访问
         * 
         * 声明此通道对虚拟资源的写入访问。
         * 这会在资源和通道之间添加引用关系（从资源到通道）。
         * 
         * 重要：输入句柄在此调用后不再有效，必须使用返回的新句柄。
         * 
         * @tparam RESOURCE 资源类型
         * @param input 资源的句柄
         * @param usage 此资源的使用方式。
         *              这取决于资源类型（默认为默认写入使用方式）
         * @return 资源的新句柄。输入句柄不再有效
         */
        template<typename RESOURCE>
        [[nodiscard]] FrameGraphId<RESOURCE> write(FrameGraphId<RESOURCE> input,
                typename RESOURCE::Usage usage = RESOURCE::DEFAULT_W_USAGE) {
            return mFrameGraph.write<RESOURCE>(mPassNode, input, usage);  // 委托给 FrameGraph
        }

        /**
         * 标记副作用
         * 
         * 将当前通道标记为叶子节点。添加对它的引用，使其不会被剔除。
         * 
         * 副作用通道是那些即使没有输出资源也应该执行的通道
         * （例如，呈现到屏幕、执行计算等）。
         * 
         * 注意：在导入资源上调用 write() 会自动添加副作用。
         */
        void sideEffect() noexcept;

        /**
         * Retrieves the descriptor associated to a resource
         * @tparam RESOURCE Type of the resource
         * @param handle    Handle to a virtual resource
         * @return          Reference to the descriptor
         */
        template<typename RESOURCE>
        typename RESOURCE::Descriptor const& getDescriptor(FrameGraphId<RESOURCE> handle) const {
            return static_cast<Resource<RESOURCE> const*>(
                    mFrameGraph.getResource(handle))->descriptor;
        }

        /**
         * Retrieves the subresource descriptor associated to a resource
         * @tparam RESOURCE Type of the resource
         * @param handle    Handle to a virtual resource
         * @return          Reference to the subresource descriptor
         */
        template<typename RESOURCE>
        typename RESOURCE::SubResourceDescriptor const& getSubResourceDescriptor(FrameGraphId<RESOURCE> handle) const {
            return static_cast<Resource<RESOURCE> const*>(
                    mFrameGraph.getResource(handle))->subResourceDescriptor;
        }

        /**
         * Retrieves the name of a resource
         * @param handle    Handle to a virtual resource
         * @return          C string to the name of the resource
         */
        utils::StaticString getName(FrameGraphHandle handle) const noexcept;


        /**
         * Helper to creates a FrameGraphTexture resource.
         * @param name      A pointer to a null terminated string.
         *                  The pointer lifetime must extend beyond execute().
         * @param desc      Descriptor for this resources
         * @return          A typed resource handle
         */
        FrameGraphId<FrameGraphTexture> createTexture(utils::StaticString name,
                FrameGraphTexture::Descriptor const& desc = {}) noexcept {
            return create<FrameGraphTexture>(name, desc);
        }

        /**
         * 采样纹理（辅助方法）
         * 
         * 用于常见纹理采样情况的辅助方法。
         * 这等价于调用 read(input, FrameGraphTexture::Usage::SAMPLEABLE)。
         * 
         * @param input FrameGraphTexture 的句柄
         * @return FrameGraphTexture 的新句柄。
         *         输入句柄不再有效
         */
        FrameGraphId<FrameGraphTexture> sample(FrameGraphId<FrameGraphTexture> const input) {
            return read(input, FrameGraphTexture::Usage::SAMPLEABLE);  // 使用 SAMPLEABLE 使用方式
        }

    private:
        friend class FrameGraph;  // 允许 FrameGraph 访问私有成员
        
        /**
         * 构造函数
         * 
         * @param fg FrameGraph 引用
         * @param passNode 通道节点指针
         */
        Builder(FrameGraph& fg, PassNode* passNode) noexcept;
        
        /**
         * 析构函数
         */
        ~Builder() noexcept = default;
        
        /**
         * FrameGraph 引用
         */
        FrameGraph& mFrameGraph;
        
        /**
         * 通道节点指针（常量）
         */
        PassNode* const mPassNode;
    };

    // --------------------------------------------------------------------------------------------

    /**
     * 模式枚举
     * 
     * 控制 FrameGraph 的行为模式。
     */
    enum class Mode {
        UNPROTECTED,  // 未保护模式（默认）
        PROTECTED,    // 保护模式（用于受保护内容）
    };

    /**
     * 构造函数
     * 
     * @param resourceAllocator 资源分配器接口引用
     * @param mode 模式（默认为 UNPROTECTED）
     */
    explicit FrameGraph(ResourceAllocatorInterface& resourceAllocator,
            Mode mode = Mode::UNPROTECTED);
    
    FrameGraph(FrameGraph const&) = delete;  // 禁止拷贝构造
    FrameGraph& operator=(FrameGraph const&) = delete;  // 禁止拷贝赋值
    
    /**
     * 析构函数
     */
    ~FrameGraph() noexcept;

    /**
     * 获取默认黑板（非常量版本）
     * 
     * 黑板用于在通道之间传递资源句柄。
     * 
     * @return 黑板引用
     */
    Blackboard& getBlackboard() noexcept { return mBlackboard; }

    /**
     * 获取默认黑板（常量版本）
     * 
     * @return 黑板常量引用
     */
    Blackboard const& getBlackboard() const noexcept { return mBlackboard; }

    /**
     * 空结构体
     * 
     * 用于没有数据的通道。
     */
    struct Empty { };

    /**
     * Add a pass to the frame graph. Typically:
     *
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     * struct PassData {
     * };
     * auto& pass = addPass<PassData>("Pass Name",
     *      [&](Builder& builder, auto& data) {
     *          // synchronously declare resources here
     *      },
     *      [=](FrameGraphResources const& resources, auto const&, DriverApi& driver) {
     *          // issue backend drawing commands here
     *      }
     * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     *
     * @tparam Data     A user-defined structure containing this pass data
     * @tparam Setup    A lambda of type [](Builder&, Data&).
     * @tparam Execute  A lambda of type [](FrameGraphResources const&, Data const&, DriverApi&)
     *
     * @param name      A name for this pass. Used for debugging only.
     * @param setup     lambda called synchronously, used to declare which and how resources are
     *                  used by this pass. Captures should be done by reference.
     * @param execute   lambda called asynchronously from FrameGraph::execute(),
     *                  where immediate drawing commands can be issued.
     *                  Captures must be done by copy.
     *
     * @return          A reference to a Pass object
     */
    template<typename Data, typename Setup, typename Execute>
    FrameGraphPass<Data>& addPass(const char* name, Setup setup, Execute&& execute);

    template<typename Data, typename Setup>
    FrameGraphPass<Data>& addPass(const char* name, Setup setup);

    /**
     * Adds a simple execute-only pass with side-effect. Use with caution as such a pass is never
     * culled.
     *
     * @tparam Execute  A lambda of type [](DriverApi&)
     * @param name      A name for this pass. Used for debugging only.
     * @param execute   lambda called asynchronously from FrameGraph::execute(),
     *                  where immediate drawing commands can be issued.
     *                  Captures must be done by copy.
     */
    template<typename Execute>
    void addTrivialSideEffectPass(const char* name, Execute&& execute);

    /**
     * 编译
     * 
     * 分配具体资源并剔除未引用的通道。
     * 
     * 编译阶段执行以下操作：
     * 1. 剔除未引用的通道（没有副作用且没有输出被使用的通道）
     * 2. 确定资源的使用标志（基于所有读取和写入）
     * 3. 分配具体资源（在首次使用前分配，在最后使用后销毁）
     * 4. 解析资源版本（确定每个通道使用的具体资源版本）
     * 
     * @return FrameGraph 引用，用于链式调用
     */
    FrameGraph& compile() noexcept;

    /**
     * 执行
     * 
     * 执行所有引用的通道。
     * 
     * 通道按照依赖顺序执行（拓扑排序）。
     * 每个通道的 execute lambda 都会被调用，可以发出后端绘制命令。
     * 
     * @param driver 后端引用，用于执行命令
     */
    void execute(backend::DriverApi& driver) noexcept;

    /**
     * 转发资源
     * 
     * 将一个资源转发到另一个资源，后者被替换。
     * 被替换资源的句柄将永远无效。
     * 
     * 资源转发用于将资源重命名或合并。
     * 例如，可以将中间渲染目标转发到最终输出纹理。
     * 
     * @tparam RESOURCE 资源类型
     * @param resource 被转发的子资源句柄
     * @param replacedResource 被替换的子资源句柄。
     *                         此句柄在此调用后无效
     * @return 转发资源的新版本句柄
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> forwardResource(FrameGraphId<RESOURCE> resource,
            FrameGraphId<RESOURCE> replacedResource);

    /**
     * Create a new resource from the descriptor and forwards it to a specified resource that
     * gets replaced.
     * The replaced resource's handle becomes forever invalid.
     *
     * @tparam RESOURCE             Type of the resources
     * @param name                  A name for the new resource
     * @param desc                  Descriptor to create the new resource
     * @param replacedResource      Handle of the subresource being replaced
     *                              This handle becomes invalid after this call
     * @return                      Handle to a new version of the forwarded resource
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> forwardResource(char const* name,
            typename RESOURCE::Descriptor const& desc,
            FrameGraphId<RESOURCE> replacedResource);

    /**
     * Create a new subresource from the descriptors and forwards it to a specified resource that
     * gets replaced.
     * The replaced resource's handle becomes forever invalid.
     *
     * @tparam RESOURCE             Type of the resources
     * @param name                  A name for the new subresource
     * @param desc                  Descriptor to create the new subresource
     * @param subdesc               Descriptor to create the new subresource
     * @param replacedResource      Handle of the subresource being replaced
     *                              This handle becomes invalid after this call
     * @return                      Handle to a new version of the forwarded resource
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> forwardResource(char const* name,
            typename RESOURCE::Descriptor const& desc,
            typename RESOURCE::SubResourceDescriptor const& subdesc,
            FrameGraphId<RESOURCE> replacedResource);

    /**
     * 呈现资源
     * 
     * 添加对 'input' 的引用，防止其被剔除。
     * 
     * 这通常用于标记最终输出资源（例如，呈现到屏幕的纹理）。
     * 呈现的资源不会被剔除，即使没有通道直接使用它。
     * 
     * @tparam RESOURCE 资源类型
     * @param input 资源句柄
     */
    template<typename RESOURCE>
    void present(FrameGraphId<RESOURCE> input);

    /**
     * 导入资源
     * 
     * 将具体资源导入到帧图。
     * 生命周期管理不会转移到帧图。
     * 
     * 导入的资源是已经存在的 GPU 资源（例如，交换链纹理、外部纹理等）。
     * 帧图不会创建或销毁导入的资源，只是管理它们的使用。
     * 
     * @tparam RESOURCE 要导入的资源类型
     * @param name 此资源的名称
     * @param desc 此资源的描述符
     * @param usage 资源的使用方式
     * @param resource 资源本身的引用
     * @return 可以在帧图中正常使用的句柄
     */
    template<typename RESOURCE>
    FrameGraphId<RESOURCE> import(utils::StaticString name,
            typename RESOURCE::Descriptor const& desc,
            typename RESOURCE::Usage usage,
            const RESOURCE& resource) noexcept;

    /**
     * 导入渲染目标
     * 
     * 将 RenderTarget 作为 FrameGraphTexture 导入到帧图。
     * 之后，此 FrameGraphTexture 可以与 declareRenderPass() 一起使用，
     * 生成的具体 FrameGraphRenderPass 将是此处传递的参数，而不是动态创建的。
     * 
     * 这用于导入已存在的渲染目标（例如，交换链的渲染目标）。
     * 
     * @param name FrameGraphRenderPass 的名称
     * @param desc 导入的 FrameGraphRenderPass 的 ImportDescriptor
     * @param target 要导入的具体 FrameGraphRenderPass 的句柄
     * @return FrameGraphTexture 的句柄
     */
    FrameGraphId<FrameGraphTexture> import(utils::StaticString name,
            FrameGraphRenderPass::ImportDescriptor const& desc,
            backend::Handle<backend::HwRenderTarget> target);


    /**
     * 检查句柄是否有效
     * 
     * 检查句柄是否已初始化且有效。
     * 
     * @param handle 要测试有效性的句柄
     * @return 如果句柄有效返回 true，否则返回 false
     */
    bool isValid(FrameGraphHandle handle) const;

    /**
     * 检查通道是否被剔除
     * 
     * 返回通道在 FrameGraph::compile() 之后是否被剔除。
     * 
     * 被剔除的通道不会被执行。
     * 
     * @param pass 通道引用
     * @return 如果通道被剔除返回 true，否则返回 false
     */
    bool isCulled(FrameGraphPassBase const& pass) const noexcept;

    /**
     * Retrieves the descriptor associated to a resource
     * @tparam RESOURCE Type of the resource
     * @param handle    Handle to a virtual resource
     * @return          Reference to the descriptor
     */
    template<typename RESOURCE>
    typename RESOURCE::Descriptor const& getDescriptor(FrameGraphId<RESOURCE> handle) const {
        return static_cast<Resource<RESOURCE> const*>(getResource(handle))->descriptor;
    }

    /**
     * Retrieves the descriptor associated to a resource
     * @tparam RESOURCE Type of the resource
     * @param handle    Handle to a virtual resource
     * @return          Reference to the descriptor
     */
    template<typename RESOURCE>
    typename RESOURCE::SubResourceDescriptor const& getSubResourceDescriptor(FrameGraphId<RESOURCE> handle) const {
        return static_cast<Resource<RESOURCE> const*>(getResource(handle))->subResourceDescriptor;
    }

    /**
     * 检查是否为无环图
     * 
     * 检查 FrameGraph 是否为无环图（DAG）。
     * 这仅用于测试目的。
     * 性能不期望很好。在 Release 构建中可能总是返回 true。
     * 
     * @return 如果帧图是无环的返回 true，否则返回 false
     */
    bool isAcyclic() const noexcept;

    /**
     * 导出 Graphviz 视图
     * 
     * 导出图的 Graphviz 视图，用于可视化帧图结构。
     * 
     * @param out 输出流
     * @param name 图的名称（可选，默认为 nullptr）
     */
    void export_graphviz(utils::io::ostream& out, const char* name = nullptr) const noexcept;

    /**
     * 获取帧图信息
     * 
     * 导出当前图的 fgviewer::FrameGraphInfo。
     * 
     * 注意：此函数应在 FrameGraph::compile() 之后调用。
     * 
     * @param viewName 视图名称
     * @return 帧图信息
     */
    fgviewer::FrameGraphInfo getFrameGraphInfo(const char *viewName) const;

private:
    friend class FrameGraphResources;
    friend class PassNode;
    friend class ResourceNode;
    friend class RenderPassNode;

    LinearAllocatorArena& getArena() noexcept { return mArena; }
    DependencyGraph& getGraph() noexcept { return mGraph; }
    ResourceAllocatorInterface& getResourceAllocator() noexcept { return mResourceAllocator; }

    /**
     * 资源槽位结构
     * 
     * 存储资源的索引和版本信息。
     * 用于快速查找资源、资源节点和子资源的父节点。
     */
    struct ResourceSlot {
        using Version = FrameGraphHandle::Version;  // 版本类型别名
        using Index = int16_t;  // 索引类型别名
        
        /**
         * 资源索引
         * 
         * VirtualResource* 在 mResources 中的索引
         */
        Index rid = 0;
        
        /**
         * 资源节点索引
         * 
         * ResourceNode* 在 mResourceNodes 中的索引
         */
        Index nid = 0;
        
        /**
         * 子资源父节点索引
         * 
         * ResourceNode* 在 mResourceNodes 中的索引，用于读取子资源的父节点。
         * -1 表示不是子资源。
         */
        Index sid = -1;
        
        /**
         * 版本号
         */
        Version version = 0;
    };
    
    /**
     * 重置
     * 
     * 重置帧图状态，准备下一帧。
     */
    void reset() noexcept;
    
    /**
     * 添加呈现通道
     * 
     * 添加一个呈现通道（用于呈现资源）。
     * 
     * @param setup 设置函数
     */
    void addPresentPass(const std::function<void(Builder&)>& setup) noexcept;
    
    /**
     * 添加通道（内部方法）
     * 
     * @param name 通道名称
     * @param base 通道基类指针
     * @return 构建器
     */
    Builder addPassInternal(const char* name, FrameGraphPassBase* base) noexcept;
    
    /**
     * 创建新版本
     * 
     * 为资源句柄创建新版本。
     * 
     * @param handle 资源句柄
     * @return 新版本的句柄
     */
    FrameGraphHandle createNewVersion(FrameGraphHandle handle) noexcept;
    
    /**
     * 如果需要则为子资源创建新版本
     * 
     * @param node 资源节点指针
     * @return 资源节点指针（可能是新创建的）
     */
    ResourceNode* createNewVersionForSubresourceIfNeeded(ResourceNode* node) noexcept;
    
    /**
     * 添加资源（内部方法）
     * 
     * @param resource 虚拟资源指针
     * @return 资源句柄
     */
    FrameGraphHandle addResourceInternal(VirtualResource* resource) noexcept;
    
    /**
     * 添加子资源（内部方法）
     * 
     * @param parent 父资源句柄
     * @param resource 虚拟资源指针
     * @return 子资源句柄
     */
    FrameGraphHandle addSubResourceInternal(FrameGraphHandle parent, VirtualResource* resource) noexcept;
    
    /**
     * 读取（内部方法）
     * 
     * @param handle 资源句柄
     * @param passNode 通道节点指针
     * @param connect 连接函数（返回是否成功连接）
     * @return 新版本的资源句柄
     */
    FrameGraphHandle readInternal(FrameGraphHandle handle, PassNode* passNode,
            const std::function<bool(ResourceNode*, VirtualResource*)>& connect);
    
    /**
     * 写入（内部方法）
     * 
     * @param handle 资源句柄
     * @param passNode 通道节点指针
     * @param connect 连接函数（返回是否成功连接）
     * @return 新版本的资源句柄
     */
    FrameGraphHandle writeInternal(FrameGraphHandle handle, PassNode* passNode,
            const std::function<bool(ResourceNode*, VirtualResource*)>& connect);
    
    /**
     * 转发资源（内部方法）
     * 
     * @param resourceHandle 资源句柄
     * @param replaceResourceHandle 被替换的资源句柄
     * @return 新版本的句柄
     */
    FrameGraphHandle forwardResourceInternal(FrameGraphHandle resourceHandle,
            FrameGraphHandle replaceResourceHandle);

    void assertValid(FrameGraphHandle handle) const;

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> create(utils::StaticString name,
            typename RESOURCE::Descriptor const& desc) noexcept;

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> createSubresource(FrameGraphId<RESOURCE> parent,
            utils::StaticString name, typename RESOURCE::SubResourceDescriptor const& desc) noexcept;

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> read(PassNode* passNode,
            FrameGraphId<RESOURCE> input, typename RESOURCE::Usage usage);

    template<typename RESOURCE>
    FrameGraphId<RESOURCE> write(PassNode* passNode,
            FrameGraphId<RESOURCE> input, typename RESOURCE::Usage usage);

    ResourceSlot& getResourceSlot(FrameGraphHandle const handle) noexcept {
        assert_invariant((size_t)handle.index < mResourceSlots.size());
        assert_invariant((size_t)mResourceSlots[handle.index].rid < mResources.size());
        assert_invariant((size_t)mResourceSlots[handle.index].nid < mResourceNodes.size());
        return mResourceSlots[handle.index];
    }

    ResourceSlot const& getResourceSlot(FrameGraphHandle const handle) const noexcept {
        return const_cast<FrameGraph*>(this)->getResourceSlot(handle);
    }

    VirtualResource* getResource(FrameGraphHandle const handle) noexcept {
        assert_invariant(handle.isInitialized());
        ResourceSlot const& slot = getResourceSlot(handle);
        assert_invariant((size_t)slot.rid < mResources.size());
        return mResources[slot.rid];
    }

    ResourceNode* getActiveResourceNode(FrameGraphHandle const handle) noexcept {
        assert_invariant(handle);
        ResourceSlot const& slot = getResourceSlot(handle);
        assert_invariant((size_t)slot.nid < mResourceNodes.size());
        return mResourceNodes[slot.nid];
    }

    VirtualResource const* getResource(FrameGraphHandle const handle) const noexcept {
        return const_cast<FrameGraph*>(this)->getResource(handle);
    }

    ResourceNode const* getResourceNode(FrameGraphHandle const handle) const noexcept {
        return const_cast<FrameGraph*>(this)->getActiveResourceNode(handle);
    }

    void destroyInternal() noexcept;

    /**
     * 黑板
     * 
     * 用于在通道之间传递资源句柄。
     */
    Blackboard mBlackboard;
    
    /**
     * 资源分配器接口引用
     * 
     * 用于分配和释放 GPU 资源。
     */
    ResourceAllocatorInterface& mResourceAllocator;
    
    /**
     * 线性分配器内存池
     * 
     * 用于分配帧图内部数据结构（通道、资源节点等）。
     */
    LinearAllocatorArena mArena;
    
    /**
     * 依赖图
     * 
     * 管理通道和资源之间的依赖关系。
     */
    DependencyGraph mGraph;
    
    /**
     * 模式（常量）
     */
    const Mode mMode;

    /**
     * 资源槽位向量
     * 
     * 存储所有资源的槽位信息（索引和版本）。
     */
    Vector<ResourceSlot> mResourceSlots;
    
    /**
     * 虚拟资源向量
     * 
     * 存储所有虚拟资源指针。
     */
    Vector<VirtualResource*> mResources;
    
    /**
     * 资源节点向量
     * 
     * 存储所有资源节点指针。
     */
    Vector<ResourceNode*> mResourceNodes;
    
    /**
     * 通道节点向量
     * 
     * 存储所有通道节点指针。
     */
    Vector<PassNode*> mPassNodes;
    
    /**
     * 活动通道节点结束迭代器
     * 
     * 指向活动通道节点的结束位置（用于区分活动通道和被剔除的通道）。
     */
    Vector<PassNode*>::iterator mActivePassNodesEnd;
};

/**
 * 添加通道（模板方法，带执行函数）
 * 
 * 添加一个通道到帧图，包含设置和执行阶段。
 * 
 * @tparam Data 用户定义的数据结构
 * @tparam Setup 设置 lambda 类型
 * @tparam Execute 执行 lambda 类型
 * @param name 通道名称
 * @param setup 设置 lambda（同步调用）
 * @param execute 执行 lambda（异步调用，会被移动）
 * @return 通道引用
 */
template<typename Data, typename Setup, typename Execute>
FrameGraphPass<Data>& FrameGraph::addPass(char const* name, Setup setup, Execute&& execute) {
    /**
     * 静态断言：检查 Execute lambda 捕获的数据量
     * 
     * Execute lambda 会被复制到通道中，如果捕获太多数据会导致性能问题。
     */
    static_assert(sizeof(Execute) < 2048, "Execute() lambda is capturing too much data.");

    /**
     * 创建 FrameGraph 通道
     * 
     * 使用内存池分配通道对象，移动执行 lambda。
     */
    auto* const pass = mArena.make<FrameGraphPassConcrete<Data, Execute>>(std::forward<Execute>(execute));

    /**
     * 创建构建器并调用设置函数
     * 
     * 设置函数用于声明资源使用。
     */
    Builder builder(addPassInternal(name, pass));
    setup(builder, const_cast<Data&>(pass->getData()));

    /**
     * 返回通道引用给用户
     */
    return *pass;
}

/**
 * 添加通道（模板方法，无执行函数）
 * 
 * 添加一个通道到帧图，只包含设置阶段，没有执行阶段。
 * 
 * @tparam Data 用户定义的数据结构
 * @tparam Setup 设置 lambda 类型
 * @param name 通道名称
 * @param setup 设置 lambda（同步调用）
 * @return 通道引用
 */
template<typename Data, typename Setup>
FrameGraphPass<Data>& FrameGraph::addPass(char const* name, Setup setup) {
    /**
     * 创建没有执行阶段的 FrameGraph 通道
     */
    auto* const pass = mArena.make<FrameGraphPass<Data>>();

    /**
     * 创建构建器并调用设置函数
     */
    Builder builder(addPassInternal(name, pass));
    setup(builder, const_cast<Data&>(pass->getData()));

    /**
     * 返回通道引用给用户
     */
    return *pass;
}

/**
 * 添加简单副作用通道
 * 
 * 添加一个只有执行阶段的简单副作用通道。
 * 此类通道永远不会被剔除。
 * 
 * 注意：谨慎使用，因为此类通道永远不会被剔除。
 * 
 * @tparam Execute 执行 lambda 类型
 * @param name 通道名称
 * @param execute 执行 lambda（异步调用，会被移动）
 */
template<typename Execute>
void FrameGraph::addTrivialSideEffectPass(char const* name, Execute&& execute) {
    /**
     * 添加一个空数据通道，标记为副作用，并执行提供的 lambda
     */
    addPass<Empty>(name, [](Builder& builder, auto&) { builder.sideEffect(); },  // 标记为副作用
            [execute](FrameGraphResources const&, auto const&, backend::DriverApi& driver) {
                execute(driver);  // 执行提供的 lambda
            });
}

template<typename RESOURCE>
void FrameGraph::present(FrameGraphId<RESOURCE> input) {
    // present doesn't add any usage flags, only a dependency
    addPresentPass([&](Builder& builder) { builder.read(input, {}); });
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::create(utils::StaticString name,
        typename RESOURCE::Descriptor const& desc) noexcept {
    VirtualResource* vresource(mArena.make<Resource<RESOURCE>>(name, desc));
    return FrameGraphId<RESOURCE>(addResourceInternal(vresource));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::createSubresource(FrameGraphId<RESOURCE> parent,
        utils::StaticString name, typename RESOURCE::SubResourceDescriptor const& desc) noexcept {
    auto* parentResource = static_cast<Resource<RESOURCE>*>(getResource(parent));
    VirtualResource* vresource(mArena.make<Resource<RESOURCE>>(parentResource, name, desc));
    return FrameGraphId<RESOURCE>(addSubResourceInternal(parent, vresource));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::import(utils::StaticString name,
        typename RESOURCE::Descriptor const& desc,
        typename RESOURCE::Usage usage,
        RESOURCE const& resource) noexcept {
    VirtualResource* vresource(mArena.make<ImportedResource<RESOURCE>>(name, desc, usage, resource));
    return FrameGraphId<RESOURCE>(addResourceInternal(vresource));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::read(PassNode* passNode, FrameGraphId<RESOURCE> input,
        typename RESOURCE::Usage usage) {
    FrameGraphId<RESOURCE> result(readInternal(input, passNode,
            [this, passNode, usage](ResourceNode* node, VirtualResource* vrsrc) {
                Resource<RESOURCE>* resource = static_cast<Resource<RESOURCE>*>(vrsrc);
                return resource->connect(mGraph, node, passNode, usage);
            }));
    return result;
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::write(PassNode* passNode, FrameGraphId<RESOURCE> input,
        typename RESOURCE::Usage usage) {
    FrameGraphId<RESOURCE> result(writeInternal(input, passNode,
            [this, passNode, usage](ResourceNode* node, VirtualResource* vrsrc) {
                Resource<RESOURCE>* resource = static_cast<Resource<RESOURCE>*>(vrsrc);
                return resource->connect(mGraph, passNode, node, usage);
            }));
    return result;
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::forwardResource(FrameGraphId<RESOURCE> resource,
        FrameGraphId<RESOURCE> replacedResource) {
    return FrameGraphId<RESOURCE>(forwardResourceInternal(resource, replacedResource));
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::forwardResource(char const* name,
        typename RESOURCE::Descriptor const& desc,
        FrameGraphId<RESOURCE> replacedResource) {
    FrameGraphId<RESOURCE> handle = create<RESOURCE>(name, desc);
    return forwardResource(handle, replacedResource);
}

template<typename RESOURCE>
FrameGraphId<RESOURCE> FrameGraph::forwardResource(char const* name,
        typename RESOURCE::Descriptor const& desc,
        typename RESOURCE::SubResourceDescriptor const& subdesc,
        FrameGraphId<RESOURCE> replacedResource) {
    FrameGraphId<RESOURCE> handle = create<RESOURCE>(name, desc);
    handle = createSubresource<RESOURCE>(handle, name, subdesc);
    return forwardResource(handle, replacedResource);
}

// ------------------------------------------------------------------------------------------------

/*
 * Prevent implicit instantiation of methods involving FrameGraphTexture which is a known type
 * these are explicitly instantiated in the .cpp file.
 */

extern template void FrameGraph::present(FrameGraphId<FrameGraphTexture> input);

extern template FrameGraphId<FrameGraphTexture> FrameGraph::create(utils::StaticString name,
        FrameGraphTexture::Descriptor const& desc) noexcept;

extern template FrameGraphId<FrameGraphTexture> FrameGraph::createSubresource(FrameGraphId<FrameGraphTexture> parent,
        utils::StaticString name, FrameGraphTexture::SubResourceDescriptor const& desc) noexcept;

extern template FrameGraphId<FrameGraphTexture> FrameGraph::import(utils::StaticString name,
        FrameGraphTexture::Descriptor const& desc, FrameGraphTexture::Usage usage, FrameGraphTexture const& resource) noexcept;

extern template FrameGraphId<FrameGraphTexture> FrameGraph::read(PassNode* passNode,
        FrameGraphId<FrameGraphTexture> input, FrameGraphTexture::Usage usage);

extern template FrameGraphId<FrameGraphTexture> FrameGraph::write(PassNode* passNode,
        FrameGraphId<FrameGraphTexture> input, FrameGraphTexture::Usage usage);

extern template FrameGraphId<FrameGraphTexture> FrameGraph::forwardResource(
        FrameGraphId<FrameGraphTexture> resource, FrameGraphId<FrameGraphTexture> replacedResource);

} // namespace filament

#endif //TNT_FILAMENT_FG_FRAMEGRAPH_H
