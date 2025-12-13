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

#include "AtlasAllocator.h"

#include <utils/compiler.h>
#include <utils/algorithm.h>
#include <utils/debug.h>
#include <utils/QuadTree.h>

#include <algorithm>
#include <utility>

#include <stddef.h>
#include <stdint.h>

namespace filament {

using namespace utils;

/**
 * 反 Morton 码解码
 * 
 * 将 Morton 码（Z-order 曲线编码）解码为 (x, y) 坐标对。
 * 
 * Morton 码是一种将 2D 坐标编码为单个数字的方法，用于空间索引。
 * 
 * @param m Morton 码（16 位）
 * @return (x, y) 坐标对
 */
static constexpr std::pair<uint8_t, uint8_t> unmorton(uint16_t const m) noexcept {
    uint32_t r = (m | (uint32_t(m) << 15u)) & 0x55555555u;
    r = (r | (r >> 1u)) & 0x33333333u;
    r = (r | (r >> 2u)) & 0x0f0f0f0fu;
    r =  r | (r >> 4u);
    return { uint8_t(r), uint8_t(r >> 16u) };
}

/**
 * AtlasAllocator 构造函数
 * 
 * 创建分配器并计算最大纹理大小的 2 的幂指数。
 * 
 * @param maxTextureSize 最大纹理大小
 * 
 * 实现：将大小向下舍入到小于或等于指定大小的最大 2 的幂。
 */
AtlasAllocator::AtlasAllocator(size_t const maxTextureSize) noexcept {
    // 向下舍入到小于或等于指定大小的最大 2 的幂。
    mMaxTextureSizePot = (sizeof(maxTextureSize) * 8 - 1u) - clz(maxTextureSize);
}

/**
 * 分配纹理空间
 * 
 * 在指定层中分配一个正方形区域。
 * 
 * @param textureSize 纹理大小（必须是 2 的幂）
 * @return 分配结果，包含层索引和视口。如果分配失败，layer 为 -1。
 */
AtlasAllocator::Allocation AtlasAllocator::allocate(size_t const textureSize) noexcept {
    Allocation result{};
    const size_t powerOfTwo = (sizeof(textureSize) * 8 - 1u) - clz(textureSize);

    /**
     * 检查纹理大小是否太大
     */
    if (UTILS_UNLIKELY(powerOfTwo > mMaxTextureSizePot)) {
        return result;
    }

    /**
     * 检查纹理大小是否太小
     * 
     * 如果请求的大小与最大大小的差值 >= QUAD_TREE_DEPTH，则太小。
     */
    if (UTILS_UNLIKELY(mMaxTextureSizePot - powerOfTwo >= QUAD_TREE_DEPTH)) {
        return result;
    }

    /**
     * 计算层索引并分配
     * 
     * layer = 0 表示最大大小，layer 越大表示大小越小。
     */
    const size_t layer = (mMaxTextureSizePot - powerOfTwo);
    const NodeId loc = allocateInLayer(LAYERS_DEPTH + layer);
    
    if (loc.l >= 0) {
        assert_invariant(loc.l - LAYERS_DEPTH == int8_t(layer));
        const size_t dimension = 1u << powerOfTwo;
        
        /**
         * 从 Morton 码（四叉树位置）找到纹理中的位置
         */
        const auto [x, y] = unmorton(loc.code);
        
        /**
         * 缩放到我们的最大分配大小
         * 
         * 使用掩码提取层内的坐标，然后左移 powerOfTwo 位得到实际像素坐标。
         */
        const uint32_t mask = (1u << layer) - 1u;
        result.viewport.left   = int32_t(x & mask) << powerOfTwo;
        result.viewport.bottom = int32_t(y & mask) << powerOfTwo;
        result.viewport.width  = dimension;
        result.viewport.height = dimension;
        
        /**
         * 从 Morton 码中提取层索引
         */
        result.layer = loc.code >> (2 * layer);
    }
    return result;
}

/**
 * 清空所有分配
 * 
 * 重置所有节点并更新最大纹理大小。
 * 
 * @param maxTextureSize 新的最大纹理大小（默认 1024）
 */
void AtlasAllocator::clear(size_t const maxTextureSize) noexcept {
    std::fill(mQuadTree.begin(), mQuadTree.end(), Node{});
    mMaxTextureSizePot = (sizeof(maxTextureSize) * 8 - 1u) - clz(maxTextureSize);
}

/**
 * 在指定层中分配节点
 * 
 * 使用四叉树遍历找到最合适的未分配节点。
 * 
 * 分配策略：
 * 1. 寻找最深的未分配且没有子节点的节点（最佳适配）
 * 2. 如果找到的节点深度小于目标深度，创建层次结构
 * 3. 更新父节点的子节点计数
 * 
 * @param maxHeight 最大高度（层深度）
 * @return 分配的节点 ID，如果分配失败，l 为 -1
 */
AtlasAllocator::NodeId AtlasAllocator::allocateInLayer(size_t const maxHeight) noexcept {
    using namespace QuadTreeUtils;

    NodeId candidate{ -1, 0 };
    if (UTILS_UNLIKELY(maxHeight > QuadTree::height())) {
        return candidate;
    }

    const int8_t n = int8_t(maxHeight);

    /**
     * 遍历四叉树寻找最佳分配位置
     * 
     * 从根节点 (0, 0) 开始，遍历到深度 n。
     */
    QuadTree::traverse(0, 0, n,
            [this, n, &candidate](NodeId const& curr) -> QuadTree::TraversalResult {

                // 我们不应该到达比 n 更高的层级
                assert_invariant(curr.l <= n);

                // 获取正在处理的节点
                const size_t i = index(curr.l, curr.code);
                Node const& node = mQuadTree[i];

                /**
                 * 如果节点已分配，其下的整个树都不可用
                 */
                if (node.isAllocated()) {
                    // 如果节点已分配，它不能有子节点
                    assert_invariant(!node.hasChildren());
                    return QuadTree::TraversalResult::SKIP_CHILDREN;
                }

                /**
                 * 寻找最深的未分配且没有子节点的节点
                 * 
                 * 这是我们尝试分配的最大深度内的最佳适配节点。
                 */
                if (curr.l > candidate.l && !node.hasChildren()) {
                    candidate = curr;
                    // 特殊情况：找到完全匹配的节点，直接退出。
                    if (curr.l == n) {
                        return QuadTree::TraversalResult::EXIT;
                    }
                }

                /**
                 * 我们只想找到已经有兄弟节点的适配节点，以实现"最佳适配"分配。
                 * 所以如果父节点（我们正在寻找的节点的父节点）没有子节点，
                 * 我们跳过其下的整个树。
                 */
                if (!node.hasChildren()) {
                    return QuadTree::TraversalResult::SKIP_CHILDREN;
                }

                // 继续向下遍历树
                return QuadTree::TraversalResult::RECURSE;
            });

    /**
     * 如果找到候选节点，进行分配
     */
    if (candidate.l >= 0) {
        const size_t i = index(candidate.l, candidate.code);
        Node& allocation = mQuadTree[i];
        assert_invariant(!allocation.isAllocated());
        assert_invariant(!allocation.hasChildren());

        if (candidate.l == n) {
            /**
             * 情况 1：候选节点深度等于目标深度
             * 
             * 直接标记为已分配，并更新父节点的子节点计数。
             */
            allocation.allocated = true;
            if (UTILS_LIKELY(n > 0)) {
                // 根节点没有父节点
                const size_t p = parent(candidate.l, candidate.code);
                Node& parent = mQuadTree[p];
                assert_invariant(!parent.isAllocated());
                assert_invariant(parent.hasChildren());
                assert_invariant(!parent.hasAllChildren());
                parent.children++;

#ifndef NDEBUG
                /**
                 * 调试检查：验证所有父节点都未分配且至少有一个子节点。
                 */
                NodeId ppp = candidate;
                while (ppp.l > 0) {
                    const size_t pi = QuadTreeUtils::parent(ppp.l, ppp.code);
                    ppp = NodeId{ int8_t(ppp.l - 1), uint8_t(ppp.code >> 2) };
                    Node const& node = mQuadTree[pi];
                    assert_invariant(!node.isAllocated());
                    assert_invariant(node.hasChildren());
                }
#endif
            }
        } else if (candidate.l < int8_t(QuadTree::height())) {
            /**
             * 情况 2：候选节点深度小于目标深度
             * 
             * 需要创建从候选节点到目标深度的层次结构。
             */

            if (candidate.l > 0) {
                /**
                 * 首先更新父节点的子节点计数（第一个节点没有父节点）。
                 */
                size_t const pi = parent(candidate.l, candidate.code);
                Node& parentNode = mQuadTree[pi];
                assert_invariant(!parentNode.isAllocated());
                assert_invariant(!parentNode.hasAllChildren());
                parentNode.children++;
            }

            /**
             * 从候选节点开始遍历，创建层次结构并找到目标深度的节点
             */
            NodeId found{ -1, 0 };
            QuadTree::traverse(candidate.l, candidate.code,
                    [this, n, &found](NodeId const& curr) -> QuadTree::TraversalResult {
                        size_t const j = index(curr.l, curr.code);
                        Node& node = mQuadTree[j];
                        if (curr.l == n) {
                            /**
                             * 到达目标深度，标记为已分配
                             */
                            found = curr;
                            assert_invariant(!node.hasChildren());
                            node.allocated = true;
                            return QuadTree::TraversalResult::EXIT;
                        }
                        /**
                         * 在路径上的每个节点增加子节点计数
                         */
                        assert_invariant(!node.hasAllChildren());
                        node.children++;
                        return QuadTree::TraversalResult::RECURSE;
                    });

            assert_invariant(found.l != -1);
            candidate = found;
        }
    }
    return candidate;
}

} // namespace filament
