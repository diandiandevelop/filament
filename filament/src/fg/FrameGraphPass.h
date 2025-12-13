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

#ifndef TNT_FILAMENT_FG_FRAMEGRAPHPASS_H
#define TNT_FILAMENT_FG_FRAMEGRAPHPASS_H

#include "backend/DriverApiForward.h"

#include "fg/FrameGraphResources.h"

#include <utils/Allocator.h>

namespace filament {

/**
 * 帧图通道执行器基类
 * 
 * 定义通道的执行接口。
 * 子类需要实现 execute() 方法来执行实际的渲染操作。
 */
class FrameGraphPassExecutor {
    friend class FrameGraph;
    friend class PassNode;
    friend class RenderPassNode;

protected:
    /**
     * 执行通道（纯虚函数）
     * 
     * 子类必须实现此方法来执行实际的渲染操作。
     * 
     * @param resources 帧图资源引用
     * @param driver 驱动 API 引用
     */
    virtual void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept = 0;

public:
    /**
     * 默认构造函数
     */
    FrameGraphPassExecutor() noexcept = default;
    
    /**
     * 虚析构函数
     */
    virtual ~FrameGraphPassExecutor() noexcept;
    
    /**
     * 禁止拷贝构造
     */
    FrameGraphPassExecutor(FrameGraphPassExecutor const&) = delete;
    
    /**
     * 禁止拷贝赋值
     */
    FrameGraphPassExecutor& operator = (FrameGraphPassExecutor const&) = delete;
};

/**
 * 帧图通道基类
 * 
 * 提供通道的基本功能，包括节点关联。
 */
class FrameGraphPassBase : protected FrameGraphPassExecutor {
    friend class FrameGraph;
    friend class PassNode;
    friend class RenderPassNode;
    PassNode* mNode = nullptr;  // 关联的通道节点
    
    /**
     * 设置节点
     * 
     * @param node 通道节点指针
     */
    void setNode(PassNode* node) noexcept { mNode = node; }
    
    /**
     * 获取节点
     * 
     * @return 通道节点常量引用
     */
    PassNode const& getNode() const noexcept { return *mNode; }

public:
    using FrameGraphPassExecutor::FrameGraphPassExecutor;
    ~FrameGraphPassBase() noexcept override;
};

/**
 * 帧图通道模板类
 * 
 * 提供类型安全的通道数据访问。
 * 
 * @tparam Data 通道数据类型
 */
template<typename Data>
class FrameGraphPass : public FrameGraphPassBase {
    friend class FrameGraph;

    /**
     * 允许分配器实例化我们
     */
    // allow our allocators to instantiate us
    template<typename, typename, typename, typename>
    friend class utils::Arena;

    /**
     * 默认执行方法（空实现）
     * 
     * 子类可以重写此方法或使用 execute 函数对象。
     */
    void execute(FrameGraphResources const&, backend::DriverApi&) noexcept override {}

protected:
    /**
     * 受保护构造函数
     * 
     * 只能由 FrameGraph 或分配器创建。
     */
    FrameGraphPass() = default;
    Data mData;  // 通道数据

public:
    /**
     * 获取通道数据
     * 
     * @return 通道数据常量引用
     */
    Data const& getData() const noexcept { return mData; }
    
    /**
     * 箭头操作符
     * 
     * 提供便捷的数据访问。
     * 
     * @return 通道数据常量指针
     */
    Data const* operator->() const { return &mData; }
};

/**
 * 帧图通道具体实现类
 * 
 * 使用函数对象（或 lambda）执行通道的模板类。
 * 
 * @tparam Data 通道数据类型
 * @tparam Execute 执行函数类型（函数对象或 lambda）
 */
template<typename Data, typename Execute>
class FrameGraphPassConcrete : public FrameGraphPass<Data> {
    friend class FrameGraph;

    /**
     * 允许分配器实例化我们
     */
    // allow our allocators to instantiate us
    template<typename, typename, typename, typename>
    friend class utils::Arena;

    /**
     * 构造函数
     * 
     * @param execute 执行函数对象（会被移动）
     */
    explicit FrameGraphPassConcrete(Execute&& execute) noexcept
            : mExecute(std::move(execute)) {  // 移动执行函数
    }

    /**
     * 执行通道
     * 
     * 调用执行函数对象，传入资源、数据和驱动 API。
     * 
     * @param resources 帧图资源引用
     * @param driver 驱动 API 引用
     */
    void execute(FrameGraphResources const& resources, backend::DriverApi& driver) noexcept final {
        mExecute(resources, this->mData, driver);  // 调用执行函数
    }

    Execute mExecute;  // 执行函数对象
};

} // namespace filament

#endif //TNT_FILAMENT_FG_FRAMEGRAPHPASS_H
