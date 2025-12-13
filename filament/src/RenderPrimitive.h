/*
 * Copyright (C) 2015 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_RENDERPRIMITIVE_H
#define TNT_FILAMENT_DETAILS_RENDERPRIMITIVE_H

#include <filament/RenderableManager.h>

#include "components/RenderableManager.h"

#include "details/MaterialInstance.h"

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <stdint.h>

namespace filament {

class FEngine;
class FVertexBuffer;
class FIndexBuffer;
class FRenderer;
class HwRenderPrimitiveFactory;

/**
 * 渲染图元
 * 
 * 表示一个可渲染的图元，包含几何数据、材质实例和渲染状态。
 */
class FRenderPrimitive {
public:
    FRenderPrimitive() noexcept = default;

    /**
     * 初始化渲染图元
     * 
     * @param factory 渲染图元工厂
     * @param driver 驱动 API 引用
     * @param entry 可渲染管理器条目
     */
    void init(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver,
            FRenderableManager::Entry const& entry) noexcept;

    /**
     * 设置渲染图元
     * 
     * @param factory 渲染图元工厂
     * @param driver 驱动 API 引用
     * @param type 图元类型
     * @param vertexBuffer 顶点缓冲区
     * @param indexBuffer 索引缓冲区
     * @param offset 索引偏移
     * @param count 索引数量
     */
    void set(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver,
            RenderableManager::PrimitiveType type,
            FVertexBuffer const* vertexBuffer, FIndexBuffer const* indexBuffer, size_t offset,
            size_t count) noexcept;

    /**
     * 终止渲染图元
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param factory 渲染图元工厂
     * @param driver 驱动 API 引用
     */
    void terminate(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver);

    /**
     * 获取材质实例
     */
    const FMaterialInstance* getMaterialInstance() const noexcept { return mMaterialInstance; }
    
    /**
     * 获取硬件句柄
     */
    backend::RenderPrimitiveHandle getHwHandle() const noexcept { return mHandle; }
    
    /**
     * 获取顶点缓冲区信息句柄
     */
    backend::VertexBufferInfoHandle getVertexBufferInfoHandle() const { return mVertexBufferInfoHandle; }
    
    /**
     * 获取索引偏移
     */
    uint32_t getIndexOffset() const noexcept { return mIndexOffset; }
    
    /**
     * 获取索引数量
     */
    uint32_t getIndexCount() const noexcept { return mIndexCount; }
    
    /**
     * 获取变形缓冲区偏移
     */
    uint32_t getMorphingBufferOffset() const noexcept { return mMorphingBufferOffset; }

    /**
     * 获取图元类型
     */
    backend::PrimitiveType getPrimitiveType() const noexcept { return mPrimitiveType; }
    
    /**
     * 获取启用的属性
     */
    AttributeBitset getEnabledAttributes() const noexcept { return mEnabledAttributes; }
    
    /**
     * 获取混合顺序
     */
    uint16_t getBlendOrder() const noexcept { return mBlendOrder; }
    
    /**
     * 是否启用全局混合顺序
     */
    bool isGlobalBlendOrderEnabled() const noexcept { return mGlobalBlendOrderEnabled; }

    /**
     * 设置材质实例
     */
    void setMaterialInstance(FMaterialInstance const* mi) noexcept { mMaterialInstance = mi; }

    /**
     * 设置混合顺序
     * 
     * @param order 混合顺序（15 位）
     */
    void setBlendOrder(uint16_t const order) noexcept {
        mBlendOrder = static_cast<uint16_t>(order & 0x7FFF);
    }

    /**
     * 设置全局混合顺序启用状态
     */
    void setGlobalBlendOrderEnabled(bool const enabled) noexcept {
        mGlobalBlendOrderEnabled = enabled;
    }

    /**
     * 设置变形缓冲区偏移
     */
    void setMorphingBufferOffset(uint32_t const offset) noexcept {
        mMorphingBufferOffset = offset;
    }

private:
    /**
     * 这些第一个字段是从 PrimitiveInfo 解引用的，保持它们在一起
     */
    FMaterialInstance const* mMaterialInstance = nullptr;  // 材质实例
    backend::Handle<backend::HwRenderPrimitive> mHandle = {};  // 硬件渲染图元句柄
    backend::Handle<backend::HwVertexBufferInfo> mVertexBufferInfoHandle = {};  // 顶点缓冲区信息句柄
    uint32_t mIndexOffset = 0;  // 索引偏移
    uint32_t mIndexCount = 0;  // 索引数量
    uint32_t mMorphingBufferOffset = 0;  // 变形缓冲区偏移
    /**
     * PrimitiveInfo 字段结束
     */

    AttributeBitset mEnabledAttributes = {};  // 启用的属性
    uint16_t mBlendOrder = 0;  // 混合顺序
    bool mGlobalBlendOrderEnabled = false;  // 是否启用全局混合顺序
    backend::PrimitiveType mPrimitiveType = backend::PrimitiveType::TRIANGLES;  // 图元类型
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_RENDERPRIMITIVE_H
