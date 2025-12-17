/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PIPELINESTATE_H
#define TNT_FILAMENT_BACKEND_PIPELINESTATE_H

#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <array>

#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

//! \privatesection

/**
 * 管线布局结构
 * 
 * 定义渲染管线的描述符堆布局，用于指定着色器如何访问资源。
 * 
 * 用途：
 * - 描述着色器程序使用的描述符堆布局
 * - 在 Vulkan 后端中对应 VkPipelineLayout
 * - 在 OpenGL 后端中用于验证资源绑定
 * 
 * 组成：
 * - setLayout: 描述符堆布局数组，最多支持 MAX_DESCRIPTOR_SET_COUNT 个描述符堆
 */
struct PipelineLayout {
    /**
     * Descriptor set layout array
     * 
     * 描述符堆布局数组
     * - 每个元素对应一个描述符堆的布局
     * - 数组大小为 MAX_DESCRIPTOR_SET_COUNT（通常为 4）
     * - 空句柄表示该描述符堆未使用
     */
    using SetLayout = std::array<Handle<HwDescriptorSetLayout>, MAX_DESCRIPTOR_SET_COUNT>;
    SetLayout setLayout;      // 16 bytes
};

/**
 * 管线状态结构
 * 
 * 包含渲染管线所需的所有状态信息，用于配置图形渲染管线。
 * 
 * 用途：
 * - 在渲染命令中传递完整的管线配置
 * - 用于管线状态缓存和比较
 * - 支持不同后端的管线状态管理
 * 
 * 组成：
 * - program: 着色器程序句柄
 * - vertexBufferInfo: 顶点缓冲区信息句柄
 * - pipelineLayout: 管线布局（描述符堆布局）
 * - rasterState: 光栅化状态（混合、深度测试等）
 * - stencilState: 模板测试状态
 * - polygonOffset: 多边形偏移（用于深度偏移）
 * - primitiveType: 图元类型（三角形、线条等）
 * 
 * 内存布局：
 * - 总大小约 52 字节（包含 padding）
 * - 设计为紧凑结构，便于在命令流中传递
 */
struct PipelineState {
    /**
     * Shader program handle
     * 
     * 着色器程序句柄
     * - 指向编译后的着色器程序
     * - 包含顶点、片段、计算着色器
     */
    Handle<HwProgram> program;                                              //  4 bytes
    
    /**
     * Vertex buffer info handle
     * 
     * 顶点缓冲区信息句柄
     * - 指向顶点缓冲区布局信息
     * - 包含顶点属性定义
     */
    Handle<HwVertexBufferInfo> vertexBufferInfo;                            //  4 bytes
    
    /**
     * Pipeline layout
     * 
     * 管线布局
     * - 定义描述符堆的布局
     * - 用于资源绑定验证
     */
    PipelineLayout pipelineLayout;                                          // 16 bytes
    
    /**
     * Rasterization state
     * 
     * 光栅化状态
     * - 面剔除模式
     * - 混合方程和函数
     * - 深度写入和测试
     * - 颜色写入掩码
     * - Alpha to coverage
     */
    RasterState rasterState;                                                //  4 bytes
    
    /**
     * Stencil test state
     * 
     * 模板测试状态
     * - 前后面的模板操作
     * - 模板测试函数和掩码
     */
    StencilState stencilState;                                              // 12 bytes
    
    /**
     * Polygon offset
     * 
     * 多边形偏移
     * - slope: 斜率因子
     * - constant: 常量偏移
     * - 用于解决 Z-fighting 问题
     */
    PolygonOffset polygonOffset;                                            //  8 bytes
    
    /**
     * Primitive type
     * 
     * 图元类型
     * - 默认值：TRIANGLES（三角形）
     * - 支持点、线、三角形等
     */
    PrimitiveType primitiveType = PrimitiveType::TRIANGLES;                 //  1 byte
    
    /**
     * Padding bytes
     * 
     * 填充字节
     * - 用于结构体对齐
     * - 必须初始化为 0
     */
    uint8_t padding[3] = {};                                                //  3 bytes
};

} // namespace filament::backend

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::PipelineState& ps);
#endif

#endif //TNT_FILAMENT_BACKEND_PIPELINESTATE_H
