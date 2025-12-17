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

#include <filamentapp/IcoSphere.h>

#include <array>

static constexpr float X = .525731112119133606f;
static constexpr float Z = .850650808352039932f;
static constexpr float N = 0.f;

const IcoSphere::VertexList IcoSphere::sVertices = {
        { -X, N,  Z }, { X,  N,  Z }, { -X, N,  -Z }, { X,  N,  -Z },
        { N,  Z,  X }, { N,  Z,  -X }, { N,  -Z, X }, { N,  -Z, -X },
        { Z,  X,  N }, { -Z, X,  N }, { Z,  -X, N }, { -Z, -X, N }
};

const IcoSphere::TriangleList IcoSphere::sTriangles = {
        {  1,   4, 0 }, {  4,  9,  0 }, { 4,   5, 9 }, { 8, 5,   4 }, {  1,  8,  4 },
        {  1,  10, 8 }, { 10,  3,  8 }, { 8,   3, 5 }, { 3, 2,   5 }, {  3,  7,  2 },
        {  3,  10, 7 }, { 10,  6,  7 }, { 6,  11, 7 }, { 6, 0,  11 }, {  6,  1,  0 },
        { 10,   1, 6 }, { 11,  0,  9 }, { 2,  11, 9 }, { 5, 2,   9 }, { 11,  2,  7 }
};

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 调用make_icosphere生成网格数据
 * 
 * @param subdivisions 细分级别
 */
IcoSphere::IcoSphere(size_t subdivisions) {
    mMesh = make_icosphere(subdivisions);
}

/**
 * 为边获取或创建中点顶点实现
 * 
 * 执行步骤：
 * 1. 创建边的键（确保first < second，避免重复）
 * 2. 在查找表中查找该边
 * 3. 如果不存在，创建新顶点（边的中点，归一化到单位球面）
 * 4. 返回顶点索引
 * 
 * @param lookup 边查找表
 * @param vertices 顶点列表
 * @param first 边的第一个顶点索引
 * @param second 边的第二个顶点索引
 * @return 中点顶点的索引
 */
IcoSphere::Index IcoSphere::vertex_for_edge(
        Lookup& lookup, VertexList& vertices, Index first, Index second) {
    Lookup::key_type key(first, second);
    // 确保键的有序性，避免重复
    if (key.first > key.second) {
        std::swap(key.first, key.second);
    }

    // 尝试插入边，如果已存在则返回现有顶点索引
    auto inserted = lookup.insert({ key, (Lookup::mapped_type)vertices.size() });
    if (inserted.second) {
        // 新边：计算中点并归一化到单位球面
        auto edge0 = vertices[first];
        auto edge1 = vertices[second];
        auto point = normalize(edge0 + edge1);
        vertices.push_back(point);
    }

    return inserted.first->second;
}

/**
 * 细分三角形列表实现
 * 
 * 执行步骤：
 * 1. 为每个三角形的三条边获取或创建中点顶点
 * 2. 将每个三角形细分为4个小三角形
 * 3. 返回细分后的三角形列表
 * 
 * @param vertices 顶点列表（会被修改，添加新顶点）
 * @param triangles 要细分的三角形列表
 * @return 细分后的三角形列表
 */
IcoSphere::TriangleList IcoSphere::subdivide(VertexList& vertices, TriangleList const& triangles) {
    Lookup lookup;
    TriangleList result;
    for (Triangle const& each : triangles) {
        // 获取三条边的中点顶点
        std::array<Index, 3> mid;
        mid[0] = vertex_for_edge(lookup, vertices, each.vertex[0], each.vertex[1]);
        mid[1] = vertex_for_edge(lookup, vertices, each.vertex[1], each.vertex[2]);
        mid[2] = vertex_for_edge(lookup, vertices, each.vertex[2], each.vertex[0]);
        // 将原三角形细分为4个小三角形
        result.push_back({ each.vertex[0], mid[0], mid[2] });
        result.push_back({ each.vertex[1], mid[1], mid[0] });
        result.push_back({ each.vertex[2], mid[2], mid[1] });
        result.push_back({ mid[0], mid[1], mid[2] });
    }
    return result;
}

/**
 * 生成二十面体球网格实现
 * 
 * 执行步骤：
 * 1. 从基础二十面体开始（12个顶点，20个三角形）
 * 2. 根据细分级别递归细分三角形
 * 3. 返回最终的网格数据
 * 
 * @param subdivisions 细分级别
 * @return 索引网格数据
 */
IcoSphere::IndexedMesh IcoSphere::make_icosphere(int subdivisions) {
    VertexList vertices = sVertices;
    TriangleList triangles = sTriangles;
    // 递归细分
    for (int i = 0; i < subdivisions; ++i) {
        triangles = subdivide(vertices, triangles);
    }
    return { vertices, triangles };
}
