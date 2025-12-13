/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef TNT_FILAMENT_ATLASALLOCATOR_H
#define TNT_FILAMENT_ATLASALLOCATOR_H

#include <utils/QuadTree.h>

#include <filament/Viewport.h>

#include <private/filament/EngineEnums.h>

#include <stdint.h>
#include <stddef.h>

class AtlasAllocator_AllocateFirstLevel_Test;
class AtlasAllocator_AllocateSecondLevel_Test;
class AtlasAllocator_AllocateMixed0_Test;
class AtlasAllocator_AllocateMixed1_Test;
class AtlasAllocator_AllocateMixed2_Test;

namespace filament {

/**
 * 2D 图集分配器
 * 
 * 一个 2D 分配器，用于管理纹理图集中的空间分配。
 * 
 * 特性：
 * - 分配必须是正方形且大小为 2 的幂
 * - 硬编码深度为 4，即只允许 4 种分配大小
 * - 不实际分配内存，只管理抽象 2D 图像中的空间
 * 
 * 使用场景：
 * - 阴影贴图图集
 * - 其他需要将多个纹理打包到一个大纹理中的场景
 */
class AtlasAllocator {

    /**
     * 四叉树节点
     * 
     * 使用四叉树跟踪已分配的区域。四叉树的每个节点存储此 Node 数据结构。
     * 
     * Node 跟踪：
     * - 是否已分配（如果已分配，则没有子节点）
     * - 子节点数量（但不跟踪具体是哪些子节点）
     */
    struct Node {
        /**
         * 检查节点是否已分配
         * 
         * 如果已分配，则没有子节点。
         */
        constexpr bool isAllocated() const noexcept { return allocated; }
        
        /**
         * 检查节点是否有子节点
         * 
         * 如果有子节点，则未分配。
         */
        constexpr bool hasChildren() const noexcept { return children != 0; }
        
        /**
         * 检查节点是否有所有四个子节点
         * 
         * 前提：hasChildren() 为 true。
         */
        constexpr bool hasAllChildren() const noexcept { return children == 4; }
        
        bool allocated      : 1;    // 是否已分配（true / false）
        uint8_t children    : 3;    // 子节点数量（0, 1, 2, 3, 4）
    };

    /**
     * 层深度
     * 
     * 这决定了可以使用的层数（3 层 == 64 个四叉树条目）。
     */
    static constexpr size_t LAYERS_DEPTH = 3u;

    /**
     * 四叉树深度
     * 
     * 这决定了从基础大小可以有多少个"子大小"。
     * 例如：最大纹理大小为 1024 时，可以分配 1024、512、256 和 128 的纹理。
     */
    static constexpr size_t QUAD_TREE_DEPTH = 4u;

    /**
     * 层深度限制检查
     * 
     * LAYERS_DEPTH 限制了层数，确保不超过配置的最大阴影层数。
     */
    static_assert(CONFIG_MAX_SHADOW_LAYERS <= 1u << (LAYERS_DEPTH * 2u));

    /**
     * 四叉树类型
     * 
     * QuadTreeArray 的最大深度限制为 7。
     */
    using QuadTree = utils::QuadTreeArray<Node, LAYERS_DEPTH + QUAD_TREE_DEPTH>;
    using NodeId = QuadTree::NodeId;  // 节点 ID 类型

public:
    /**
     * 构造函数
     * 
     * 创建分配器并指定最大纹理大小。必须是 2 的幂。
     * 允许的分配大小是小于或等于此大小的四个 2 的幂。
     * 
     * @param maxTextureSize 最大纹理大小（必须是 2 的幂）
     */
    explicit AtlasAllocator(size_t maxTextureSize) noexcept;

    /**
     * 分配结构
     * 
     * 表示一个分配的位置和大小。
     */
    struct Allocation {
        int32_t layer = -1;    // 层索引（-1 表示分配失败）
        Viewport viewport;      // 视口（位置和大小）
    };
    
    /**
     * 分配一个正方形区域
     * 
     * 分配大小为 `textureSize` 的正方形。必须是允许的 2 的幂之一（见上方说明）。
     * 
     * @param textureSize 纹理大小（必须是 2 的幂）
     * @return 分配结果，包含层索引和视口。如果分配失败，layer 为 -1。
     */
    Allocation allocate(size_t textureSize) noexcept;

    /**
     * 清空所有分配并重置最大纹理大小
     * 
     * @param maxTextureSize 新的最大纹理大小（默认 1024）
     */
    void clear(size_t maxTextureSize = 1024) noexcept;

private:
    friend AtlasAllocator_AllocateFirstLevel_Test;
    friend AtlasAllocator_AllocateSecondLevel_Test;
    friend AtlasAllocator_AllocateMixed0_Test;
    friend AtlasAllocator_AllocateMixed1_Test;
    friend AtlasAllocator_AllocateMixed2_Test;

    QuadTree::NodeId allocateInLayer(size_t n) noexcept;

    // quad-tree array to store the allocated list
    QuadTree mQuadTree{};
    uint8_t mMaxTextureSizePot = 0;
};

} // namespace filament

#endif // TNT_FILAMENT_ATLASALLOCATOR_H
