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

#include "RenderPrimitive.h"

#include <filament/RenderableManager.h>
#include <filament/MaterialEnums.h>

#include "details/IndexBuffer.h"
#include "details/MaterialInstance.h"
#include "details/VertexBuffer.h"

#include <private/backend/CommandStream.h>
#include <backend/DriverApiForward.h>

#include <utils/debug.h>

#include <stddef.h>

namespace filament {

/**
 * 初始化渲染图元
 * 
 * 从 RenderableManager::Entry 初始化渲染图元。
 * 
 * @param factory 硬件渲染图元工厂
 * @param driver 驱动 API 引用
 * @param entry RenderableManager 条目，包含材质实例、顶点缓冲区、索引缓冲区等信息
 */
void FRenderPrimitive::init(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver,
        FRenderableManager::Entry const& entry) noexcept {

    /**
     * 确保材质实例存在
     */
    assert_invariant(entry.materialInstance);

    /**
     * 设置材质实例（转换为内部类型）
     */
    mMaterialInstance = downcast(entry.materialInstance);
    /**
     * 设置混合顺序
     */
    mBlendOrder = entry.blendOrder;
    /**
     * 设置是否启用全局混合顺序
     */
    mGlobalBlendOrderEnabled = entry.globalBlendOrderEnabled;

    /**
     * 如果存在索引缓冲区和顶点缓冲区，设置渲染图元
     */
    if (entry.indices && entry.vertices) {
        /**
         * 转换顶点缓冲区和索引缓冲区为内部类型
         */
        FVertexBuffer const* vertexBuffer = downcast(entry.vertices);
        FIndexBuffer const* indexBuffer = downcast(entry.indices);
        /**
         * 设置渲染图元的几何数据
         */
        set(factory, driver, entry.type, vertexBuffer, indexBuffer, entry.offset, entry.count);
    }
}

/**
 * 终止渲染图元
 * 
 * 销毁硬件渲染图元，释放驱动资源。
 * 如果句柄不存在，则不执行任何操作。
 * 
 * @param factory 硬件渲染图元工厂
 * @param driver 驱动 API 引用
 */
void FRenderPrimitive::terminate(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver) {
    /**
     * 如果存在句柄，销毁硬件渲染图元
     */
    if (mHandle) {
        factory.destroy(driver, mHandle);  // 销毁硬件渲染图元
    }
}

/**
 * 设置渲染图元
 * 
 * 设置渲染图元的几何数据，包括顶点缓冲区、索引缓冲区、图元类型等。
 * 
 * @param factory 硬件渲染图元工厂
 * @param driver 驱动 API 引用
 * @param type 图元类型（三角形、线条等）
 * @param vertexBuffer 顶点缓冲区指针
 * @param indexBuffer 索引缓冲区指针
 * @param offset 索引偏移量
 * @param count 索引数量
 */
void FRenderPrimitive::set(HwRenderPrimitiveFactory& factory, backend::DriverApi& driver,
        RenderableManager::PrimitiveType const type,
        FVertexBuffer const* vertexBuffer, FIndexBuffer const* indexBuffer,
        size_t const offset, size_t const count) noexcept {
    /**
     * 如果已存在句柄，先销毁旧的渲染图元
     */
    if (mHandle) {
        factory.destroy(driver, mHandle);
    }

    /**
     * 获取顶点缓冲区中启用的属性位集合
     */
    AttributeBitset const enabledAttributes = vertexBuffer->getDeclaredAttributes();

    /**
     * 获取顶点缓冲区和索引缓冲区的硬件句柄
     */
    auto const& ebh = vertexBuffer->getHwHandle();
    auto const& ibh = indexBuffer->getHwHandle();

    /**
     * 创建新的渲染图元句柄
     */
    mHandle = factory.create(driver, ebh, ibh, type);
    /**
     * 保存顶点缓冲区信息句柄
     */
    mVertexBufferInfoHandle = vertexBuffer->getVertexBufferInfoHandle();

    /**
     * 保存图元类型
     */
    mPrimitiveType = type;
    /**
     * 保存索引偏移量
     */
    mIndexOffset = offset;
    /**
     * 保存索引数量
     */
    mIndexCount = count;
    /**
     * 保存启用的属性位集合
     */
    mEnabledAttributes = enabledAttributes;
}

} // namespace filament
