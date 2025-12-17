/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SAMPLE_ICOSPHERE_H
#define TNT_FILAMENT_SAMPLE_ICOSPHERE_H

#include <math/vec3.h>

#include <map>
#include <vector>

/**
 * IcoSphere - 二十面体球生成类
 * 
 * 通过细分二十面体生成球体网格。使用递归细分算法，
 * 每次细分将每个三角形分成4个小三角形，从而得到更平滑的球面。
 */
class IcoSphere {
public:
    /** 索引类型 */
    using Index = uint16_t;

    /**
     * Triangle - 三角形结构
     * 包含三个顶点的索引
     */
    struct Triangle {
        Index vertex[3]; // 三个顶点的索引
    };

    using TriangleList = std::vector<Triangle>;              // 三角形列表类型
    using VertexList = std::vector<filament::math::float3>;   // 顶点列表类型
    using IndexedMesh = std::pair<VertexList, TriangleList>;  // 索引网格类型（顶点+三角形）

    /**
     * 构造函数
     * @param subdivisions 细分级别，0表示基础二十面体，每增加1细分一次
     */
    explicit IcoSphere(size_t subdivisions);

    /**
     * 获取完整网格数据
     * @return 索引网格的常量引用
     */
    IndexedMesh const& getMesh() const { return mMesh; }
    /**
     * 获取顶点列表
     * @return 顶点列表的常量引用
     */
    VertexList const& getVertices() const { return mMesh.first; }
    /**
     * 获取三角形列表
     * @return 三角形列表的常量引用
     */
    TriangleList const& getIndices() const { return mMesh.second; }

private:
    static const IcoSphere::VertexList sVertices;    // 基础二十面体的12个顶点
    static const IcoSphere::TriangleList sTriangles; // 基础二十面体的20个三角形

    /** 边到顶点的查找表类型（用于细分时避免重复顶点） */
    using Lookup = std::map<std::pair<Index, Index>, Index>;
    /**
     * 为边获取或创建中点顶点
     * @param lookup 边查找表
     * @param vertices 顶点列表
     * @param first 边的第一个顶点索引
     * @param second 边的第二个顶点索引
     * @return 中点顶点的索引
     */
    Index vertex_for_edge(Lookup& lookup, VertexList& vertices, Index first, Index second);
    /**
     * 细分三角形列表
     * 将每个三角形分成4个小三角形
     * @param vertices 顶点列表（会被修改，添加新顶点）
     * @param triangles 要细分的三角形列表
     * @return 细分后的三角形列表
     */
    TriangleList subdivide(VertexList& vertices, TriangleList const& triangles);
    /**
     * 生成二十面体球网格
     * @param subdivisions 细分级别
     * @return 索引网格数据
     */
    IndexedMesh make_icosphere(int subdivisions);

    IndexedMesh mMesh; // 生成的网格数据
};

#endif //TNT_FILAMENT_SAMPLE_ICOSPHERE_H
