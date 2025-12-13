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

#ifndef TNT_FILAMENT_DETAILS_SKINNINGBUFFER_H
#define TNT_FILAMENT_DETAILS_SKINNINGBUFFER_H

#include <filament/SkinningBuffer.h>

#include "downcast.h"

#include <private/filament/EngineEnums.h>
#include <private/filament/UibStructs.h>

#include <backend/DriverApiForward.h>
#include <backend/Handle.h>

#include <utils/FixedCapacityVector.h>

#include <math/mat4.h>
#include <math/vec2.h>

#include <stddef.h>
#include <stdint.h>

// for gtest
class FilamentTest_Bones_Test;

namespace filament {

class FEngine;
class FRenderableManager;

/**
 * 蒙皮缓冲区实现类
 * 
 * 管理骨骼动画的骨骼变换数据。
 * 骨骼动画使用骨骼层次结构来控制网格变形。
 * 
 * 实现细节：
 * - 使用 Uniform Buffer Object (UBO) 存储骨骼变换矩阵
 * - 骨骼数量向上舍入到 CONFIG_MAX_BONE_COUNT 的倍数（对齐要求）
 * - 支持两种骨骼数据格式：Bone（四元数+平移）和 mat4f（完整矩阵）
 * - 使用纹理存储骨骼索引和权重对（用于 GPU 蒙皮）
 */
class FSkinningBuffer : public SkinningBuffer {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器
     */
    FSkinningBuffer(FEngine& engine, const Builder& builder);

    /**
     * 终止蒙皮缓冲区
     * 
     * 释放驱动资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    // frees driver resources, object becomes invalid
    void terminate(FEngine& engine);

    /**
     * 设置骨骼变换（Bone 格式）
     * 
     * 使用四元数和平移向量表示骨骼变换。
     * 
     * @param engine 引擎引用
     * @param transforms 骨骼变换数组（Bone 格式）
     * @param count 骨骼数量
     * @param offset 起始偏移量
     */
    void setBones(FEngine& engine, RenderableManager::Bone const* transforms, size_t count, size_t offset);
    
    /**
     * 设置骨骼变换（mat4f 格式）
     * 
     * 使用完整的 4x4 矩阵表示骨骼变换。
     * 
     * @param engine 引擎引用
     * @param transforms 骨骼变换矩阵数组
     * @param count 骨骼数量
     * @param offset 起始偏移量
     */
    void setBones(FEngine& engine, math::mat4f const* transforms, size_t count, size_t offset);
    
    /**
     * 获取骨骼数量
     * 
     * @return 骨骼数量
     */
    size_t getBoneCount() const noexcept { return mBoneCount; }

    /**
     * 获取物理骨骼数量
     * 
     * 将骨骼数量向上舍入到着色器中 UBO 的大小（CONFIG_MAX_BONE_COUNT 的倍数）。
     * 这确保了 UBO 对齐要求。
     * 
     * @param count 逻辑骨骼数量
     * @return 物理骨骼数量（向上舍入）
     */
    // round count to the size of the UBO in the shader
    static size_t getPhysicalBoneCount(size_t const count) noexcept {
        /**
         * 确保 CONFIG_MAX_BONE_COUNT 是 2 的幂
         */
        static_assert((CONFIG_MAX_BONE_COUNT & (CONFIG_MAX_BONE_COUNT - 1)) == 0);
        /**
         * 向上舍入到 CONFIG_MAX_BONE_COUNT 的倍数
         */
        return (count + CONFIG_MAX_BONE_COUNT - 1) & ~(CONFIG_MAX_BONE_COUNT - 1);
    }

private:
    friend class FilamentTest_Bones_Test;
    friend class SkinningBuffer;
    friend class FRenderableManager;

    /**
     * 设置骨骼变换（Bone 格式，静态方法）
     * 
     * @param engine 引擎引用
     * @param handle 缓冲区对象句柄
     * @param transforms 骨骼变换数组（Bone 格式）
     * @param boneCount 骨骼数量
     * @param offset 起始偏移量
     */
    static void setBones(FEngine& engine, backend::Handle<backend::HwBufferObject> handle,
            RenderableManager::Bone const* transforms, size_t boneCount, size_t offset) noexcept;

    /**
     * 设置骨骼变换（mat4f 格式，静态方法）
     * 
     * @param engine 引擎引用
     * @param handle 缓冲区对象句柄
     * @param transforms 骨骼变换矩阵数组
     * @param boneCount 骨骼数量
     * @param offset 起始偏移量
     */
    static void setBones(FEngine& engine, backend::Handle<backend::HwBufferObject> handle,
            math::mat4f const* transforms, size_t boneCount, size_t offset) noexcept;

    /**
     * 创建骨骼数据
     * 
     * 将变换矩阵转换为着色器使用的骨骼数据格式。
     * 包括变换矩阵的转置和余子式（用于法线变换）。
     * 
     * @param transform 变换矩阵
     * @return 骨骼数据
     */
    static PerRenderableBoneUib::BoneData makeBone(math::mat4f transform) noexcept;

    /**
     * 获取硬件句柄
     * 
     * @return 缓冲区对象句柄
     */
    backend::Handle<backend::HwBufferObject> getHwHandle() const noexcept {
        return mHandle;
    }

    /**
     * 创建骨骼索引和权重纹理句柄
     * 
     * 创建用于存储骨骼索引和权重对的纹理。
     * 
     * @param engine 引擎引用
     * @param count 对的数量
     * @return 纹理句柄
     */
    static backend::TextureHandle createIndicesAndWeightsHandle(FEngine& engine, size_t count);

    /**
     * 设置骨骼索引和权重数据
     * 
     * 将骨骼索引和权重对更新到纹理。
     * 
     * @param engine 引擎引用
     * @param textureHandle 纹理句柄
     * @param pairs 索引和权重对数组
     * @param count 对的数量
     */
    static void setIndicesAndWeightsData(FEngine& engine,
          backend::Handle<backend::HwTexture> textureHandle,
          const utils::FixedCapacityVector<math::float2>& pairs,
          size_t count);

    backend::Handle<backend::HwBufferObject> mHandle;  // 缓冲区对象句柄
    uint32_t mBoneCount;  // 骨骼数量
};

FILAMENT_DOWNCAST(SkinningBuffer)

} // namespace filament

#endif //TNT_FILAMENT_DETAILS_SKINNINGBUFFER_H
