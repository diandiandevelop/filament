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

/**
 * animation.cpp - Filament 动画示例
 * 
 * 本示例演示了如何使用 Filament 渲染引擎创建一个动态动画的彩色三角形。
 * 主要功能：
 * 1. 创建一个包含位置和颜色信息的三角形
 * 2. 实现顶点位置动画（Y坐标正弦波动）
 * 3. 实现整体旋转变换动画
 * 4. 展示顶点数据如何从CPU传输到GPU（OpenGL后端）
 */

// 参数解析工具，用于解析命令行参数（如选择渲染后端）
#include "common/arguments.h"

// Filament 核心渲染组件
#include <filament/Camera.h>              // 相机：控制视图和投影
#include <filament/Engine.h>              // 引擎：Filament的核心，管理所有资源
#include <filament/IndexBuffer.h>        // 索引缓冲区：定义顶点的连接顺序
#include <filament/Material.h>            // 材质：定义渲染外观
#include <filament/MaterialInstance.h>   // 材质实例：材质的可配置实例
#include <filament/RenderableManager.h>  // 可渲染对象管理器：管理场景中的可渲染实体
#include <filament/Scene.h>              // 场景：包含所有要渲染的对象
#include <filament/Skybox.h>             // 天空盒：场景背景
#include <filament/TransformManager.h>   // 变换管理器：管理对象的位置、旋转、缩放
#include <filament/VertexBuffer.h>       // 顶点缓冲区：存储顶点数据（位置、颜色等）
#include <filament/View.h>               // 视图：定义渲染视口和相机

// 工具类
#include <utils/EntityManager.h>         // 实体管理器：ECS架构中的实体管理

// Filament应用框架
#include <filamentapp/Config.h>          // 应用配置
#include <filamentapp/FilamentApp.h>     // Filament应用主类

// 标准库
#include <cmath>                         // 数学函数（sin, cos等）

// 生成的资源文件（包含预编译的材质数据）
#include "generated/resources/resources.h"

using namespace filament;
using utils::Entity;
using utils::EntityManager;

/**
 * App 结构体 - 存储应用程序的所有渲染资源
 * 
 * 这些资源在初始化时创建，在清理时销毁
 */
struct App {
    VertexBuffer* vb;      // 顶点缓冲区：存储三角形的顶点数据（位置+颜色）
    IndexBuffer* ib;       // 索引缓冲区：定义顶点的连接顺序（0->1->2形成三角形）
    Material* mat;         // 材质：定义如何渲染三角形（使用预编译的BakedColor材质）
    Camera* cam;           // 相机：控制视图和投影矩阵
    Entity camera;         // 相机实体：ECS架构中的相机实体ID
    Skybox* skybox;        // 天空盒：场景的背景颜色
    Entity renderable;     // 可渲染实体：ECS架构中代表三角形的实体ID
};

/**
 * Vertex 结构体 - 定义单个顶点的数据结构
 * 
 * 内存布局（12字节）：
 * - position: float2 (8字节) - 2D坐标 (x, y)
 * - color: uint32_t (4字节)  - ARGB格式的颜色值
 * 
 * 注意：这个结构体定义了CPU端的数据格式，需要与VertexBuffer的配置匹配
 */
struct Vertex {
    filament::math::float2 position;  // 顶点位置：2D坐标 (x, y)
    uint32_t color;                    // 顶点颜色：32位ARGB格式 (0xAARRGGBB)
};

/**
 * TRIANGLE_VERTICES - 三角形的三个顶点数据（CPU内存）
 * 
 * 顶点布局：等边三角形，中心在原点
 * - 顶点0：位置(1, 0)，红色   - 位于右侧
 * - 顶点1：位置(cos(2π/3), sin(2π/3))，绿色 - 位于左上
 * - 顶点2：位置(cos(4π/3), sin(4π/3))，蓝色 - 位于左下
 * 
 * 颜色格式说明（ARGB，32位）：
 * - 0xffff0000: A=255(ff), R=255(ff), G=0(00), B=0(00) = 红色
 * - 0xff00ff00: A=255(ff), R=0(00), G=255(ff), B=0(00) = 绿色
 * - 0xff0000ff: A=255(ff), R=0(00), G=0(00), B=255(ff) = 蓝色
 * 
 * 总大小：3个顶点 × 12字节 = 36字节
 */
static Vertex TRIANGLE_VERTICES[3] = {
    {{1, 0}, 0xffff0000u},          // 顶点0：右侧，红色
    {{cos(M_PI * 2 / 3), sin(M_PI * 2 / 3)}, 0xff00ff00u},   // 顶点1：左上，绿色
    {{cos(M_PI * 4 / 3), sin(M_PI * 4 / 3)}, 0xff0000ffu},    // 顶点2：左下，蓝色
};

/**
 * TRIANGLE_INDICES - 索引数组，定义如何连接顶点形成三角形
 * 
 * 索引顺序：0 -> 1 -> 2
 * 这表示使用顶点0、1、2按顺序连接形成一个三角形
 * 
 * 注意：索引是16位无符号短整型（USHORT），对应IndexBuffer的配置
 */
static constexpr uint16_t TRIANGLE_INDICES[3] = { 0, 1, 2 };

/**
 * main 函数 - 程序入口
 * 
 * 使用 FilamentApp 框架运行应用程序，框架会处理窗口创建、事件循环等
 */
int main(int argc, char** argv) {
    // 配置应用程序
    Config config;
    config.title = "animation";                                    // 窗口标题
    config.backend = samples::parseArgumentsForBackend(argc, argv); // 解析命令行参数选择后端（OpenGL/Vulkan/Metal等）

    // 应用程序状态
    App app;

    /**
     * setup 函数 - 初始化所有渲染资源
     * 
     * 这个lambda函数在引擎初始化完成后被调用，用于创建所有需要的渲染资源
     * 
     * 参数：
     * - engine: Filament引擎实例，用于创建所有资源
     * - view: 渲染视图，控制渲染视口
     * - scene: 场景对象，包含所有要渲染的实体
     */
    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        // ========== 1. 创建天空盒 ==========
        // 天空盒是场景的背景，这里设置为深蓝色
        app.skybox = Skybox::Builder()
            .color({0.1, 0.125, 0.25, 1.0})  // RGBA颜色：(深蓝，不透明)
            .build(*engine);
        scene->setSkybox(app.skybox);         // 将天空盒添加到场景

        // 禁用后处理效果（如色调映射、抗锯齿等），简化渲染流程
        view->setPostProcessingEnabled(false);
        // ========== 2. 创建顶点缓冲区（VertexBuffer）==========
        /**
         * VertexBuffer 是存储顶点数据的GPU缓冲区
         * 
         * 数据流程：
         * 1. 在CPU内存中定义顶点数据（TRIANGLE_VERTICES）
         * 2. 配置VertexBuffer的属性布局（告诉GPU如何解析数据）
         * 3. 通过setBufferAt()将CPU数据上传到GPU显存
         * 
         * 内存布局说明（每个顶点12字节）：
         * [0-7]   : position (float2) = 8字节
         * [8-11]  : color (ubyte4) = 4字节
         * 总计：12字节/顶点 × 3顶点 = 36字节
         */
        app.vb = VertexBuffer::Builder()
                // 1. 设置顶点数量：3个（对应三角形的3个顶点）
                .vertexCount(3)
                
                // 2. 设置缓冲区数量：1个（所有顶点属性都存储在同一个缓冲区中）
                //    也可以使用多个缓冲区分离不同属性（如位置一个缓冲区，颜色另一个缓冲区）
                .bufferCount(1)
                
                // 3. 配置POSITION属性（顶点坐标）
                .attribute(
                    VertexAttribute::POSITION,                    // 属性类型：位置坐标
                    0,                                            // 缓冲区索引：使用第0个缓冲区
                    VertexBuffer::AttributeType::FLOAT2,          // 数据类型：2个float（x, y坐标）
                    0,                                            // 字节偏移：从缓冲区第0字节开始读取
                    12                                            // 顶点步长：每个顶点占12字节（到下一个顶点的距离）
                )
                
                // 4. 配置COLOR属性（顶点颜色）
                .attribute(
                    VertexAttribute::COLOR,                      // 属性类型：颜色
                    0,                                            // 缓冲区索引：同样使用第0个缓冲区
                    VertexBuffer::AttributeType::UBYTE4,          // 数据类型：4个无符号字节（ARGB各1字节）
                    8,                                            // 字节偏移：从第8字节开始（position占前8字节）
                    12                                            // 顶点步长：每个顶点占12字节
                )
                
                // 5. 标记COLOR属性需要归一化
                //    UBYTE4的范围是0-255，归一化后转换为0.0-1.0的浮点数
                //    这样在着色器中可以直接使用（0.0-1.0范围）
                .normalized(VertexAttribute::COLOR)
                
                // 构建VertexBuffer对象
                // 此时会在GPU上分配内存，但还没有上传数据
                .build(*engine);

        // ========== 3. 上传顶点数据到GPU ==========
        /**
         * setBufferAt() - 将CPU内存中的顶点数据上传到GPU显存
         * 
         * 调用链：
         * setBufferAt() 
         *   → FVertexBuffer::setBufferAt() 
         *     → DriverApi::updateBufferObject() 
         *       → OpenGLDriver::updateBufferObject() 
         *         → glBufferData() / glBufferSubData()
         *           → [DMA传输: CPU内存 → GPU显存]
         * 
         * 参数说明：
         * - engine: Filament引擎
         * - 0: 缓冲区索引（第0个缓冲区）
         * - BufferDescriptor: 数据描述符
         *   - TRIANGLE_VERTICES: CPU内存中的数据指针
         *   - 36: 数据大小（字节）
         *   - nullptr: 回调函数（这里为null，表示数据不会被自动释放）
         * 
         * 注意：此时数据通过DMA异步传输到GPU，不会阻塞CPU
         */
        app.vb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(TRIANGLE_VERTICES, 36, nullptr));
        // ========== 4. 创建索引缓冲区（IndexBuffer）==========
        /**
         * IndexBuffer 定义顶点的连接顺序
         * 
         * 作用：
         * - 告诉GPU如何连接顶点形成图元（这里是三角形）
         * - 可以重用顶点数据（一个顶点可以被多个三角形共享）
         * 
         * 这里使用索引 [0, 1, 2] 表示：
         * - 使用顶点0、1、2按顺序连接形成一个三角形
         */
        app.ib = IndexBuffer::Builder()
                .indexCount(3)                              // 索引数量：3个
                .bufferType(IndexBuffer::IndexType::USHORT)  // 索引类型：16位无符号短整型（0-65535）
                .build(*engine);
        
        // 上传索引数据到GPU
        // 数据大小：3个索引 × 2字节(USHORT) = 6字节
        app.ib->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(TRIANGLE_INDICES, 6, nullptr));

        // ========== 5. 创建材质（Material）==========
        /**
         * Material 定义如何渲染对象的外观
         * 
         * RESOURCES_BAKEDCOLOR_DATA 是预编译的材质数据
         * 这个材质会直接使用顶点颜色，不需要额外的纹理或光照计算
         */
        app.mat = Material::Builder()
                .package(RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE)
                .build(*engine);

        // ========== 6. 创建可渲染实体（Renderable）==========
        /**
         * Renderable 是ECS架构中的可渲染组件
         * 它将顶点缓冲区、索引缓冲区、材质组合在一起，形成一个可渲染的对象
         */
        app.renderable = EntityManager::get().create();  // 创建实体ID
        scene->addEntity(app.renderable);                 // 将实体添加到场景（但此时还没有配置渲染数据）

        // ========== 7. 创建相机（Camera）==========
        /**
         * Camera 控制视图和投影矩阵
         * 定义从哪个角度观察场景，以及如何将3D坐标投影到2D屏幕
         */
        app.camera = utils::EntityManager::get().create();  // 创建相机实体ID
        app.cam = engine->createCamera(app.camera);          // 创建相机组件
        view->setCamera(app.cam);                            // 将相机绑定到视图
    };

    /**
     * cleanup 函数 - 清理所有渲染资源
     * 
     * 在应用程序退出时被调用，释放所有分配的资源
     * 注意：必须按照正确的顺序销毁资源，避免悬空指针
     */
    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        // 销毁天空盒
        engine->destroy(app.skybox);
        
        // 销毁可渲染实体（从场景中移除）
        engine->destroy(app.renderable);
        
        // 销毁材质
        engine->destroy(app.mat);
        
        // 销毁顶点缓冲区和索引缓冲区（释放GPU内存）
        engine->destroy(app.vb);
        engine->destroy(app.ib);

        // 销毁相机组件和实体
        engine->destroyCameraComponent(app.camera);
        utils::EntityManager::get().destroy(app.camera);
    };

    /**
     * animate 函数 - 动画循环（每帧调用）
     * 
     * 这个lambda函数在每一帧渲染前被调用，用于更新动画状态
     * 
     * 参数：
     * - engine: Filament引擎
     * - view: 渲染视图
     * - now: 当前时间（秒），从程序启动开始计算
     * 
     * 本函数实现两种动画效果：
     * 1. 顶点位置动画：顶点0的Y坐标按正弦波变化
     * 2. 旋转变换：整个三角形绕Z轴旋转
     */
    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {

        // ========== 代码片段：测试重建VertexBuffer（已禁用）==========
        // 这段代码展示了如何每帧重建VertexBuffer，但通常不推荐这样做（性能开销大）
        #if 0
        engine->destroy(app.vb);
        auto vb = app.vb = VertexBuffer::Builder()
                .vertexCount(3)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, 12)
                .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::UBYTE4, 8, 12)
                .normalized(VertexAttribute::COLOR)
                .build(*engine);
        #else
        // 使用已存在的VertexBuffer（推荐方式）
        auto vb = app.vb;
        #endif

        // ========== 1. 更新顶点数据（CPU端）==========
        /**
         * 步骤1：在CPU内存中修改顶点数据
         * 
         * 这里修改顶点0的Y坐标，使其按正弦波变化
         * sin(now * 4) 表示：
         * - 频率为4（每秒4个周期）
         * - 范围在-1到1之间
         */
        void* verts = malloc(36);                              // 分配临时内存（36字节）
        TRIANGLE_VERTICES[0].position.y = sin(now * 4);        // 修改顶点0的Y坐标（正弦动画）
        memcpy(verts, TRIANGLE_VERTICES, 36);                   // 复制顶点数据到临时内存

        // ========== 2. 上传更新后的顶点数据到GPU ==========
        /**
         * setBufferAt() - 将更新后的顶点数据上传到GPU显存
         * 
         * 【核心：顶点数据传输到GPU的完整流程】
         * 
         * 调用链（OpenGL后端）：
         * 1. vb->setBufferAt()
         *    ↓
         * 2. FVertexBuffer::setBufferAt() 
         *    [filament/src/details/VertexBuffer.cpp:345]
         *    - 获取对应的BufferObject句柄
         *    - 调用DriverApi::updateBufferObject()
         *    ↓
         * 3. OpenGLDriver::updateBufferObject()
         *    [filament/backend/src/opengl/OpenGLDriver.cpp:2839]
         *    - glBindBuffer(GL_ARRAY_BUFFER, vbo_id)  // 绑定VBO
         *    - glBufferData(GL_ARRAY_BUFFER, 36, verts, GL_STATIC_DRAW)
         *      // 将CPU数据上传到GPU（通过DMA异步传输）
         *    ↓
         * 4. scheduleDestroy(buffer)
         *    - 在数据上传完成后，调用回调函数释放CPU内存
         * 
         * 数据传输机制：
         * - glBufferData() 是异步的，通过DMA（直接内存访问）传输数据
         * - 数据从CPU内存 → 系统总线 → PCIe → GPU显存
         * - 不会阻塞CPU，GPU驱动会在适当时机完成传输
         * 
         * 内存管理：
         * - BufferDescriptor持有CPU内存指针
         * - 回调函数free()在数据上传完成后自动释放内存
         * - 这确保了内存不会过早释放（数据可能还在传输中）
         * 
         * 参数说明：
         * - engine: Filament引擎
         * - 0: 缓冲区索引（第0个缓冲区）
         * - BufferDescriptor(verts, 36, free):
         *   - verts: CPU内存中的数据指针
         *   - 36: 数据大小（字节）
         *   - free: 回调函数，数据上传后自动调用free(verts)释放内存
         */
        vb->setBufferAt(*engine, 0, 
                VertexBuffer::BufferDescriptor(verts, 36,
                    (VertexBuffer::BufferDescriptor::Callback) free));

        // ========== 3. 重建可渲染对象（每帧重建，性能开销较大）==========
        /**
         * 注意：这里每帧都销毁并重建Renderable，这不是最佳实践
         * 
         * 更好的做法：
         * - 只在初始化时创建一次Renderable
         * - 只更新变换矩阵（通过TransformManager）
         * 
         * 当前实现的原因：
         * - 可能是为了演示目的，展示如何动态更新渲染对象
         * - 或者是为了确保顶点数据更新后立即生效
         */
        auto& rcm = engine->getRenderableManager();
        rcm.destroy(app.renderable);  // 销毁旧的Renderable组件
        
        // 重新构建Renderable，配置渲染参数
        RenderableManager::Builder(1)  // 1个渲染图元（primitive）
                // 边界盒：用于视锥剔除和碰撞检测
                // 这里设置为一个较大的盒子，确保三角形始终可见
                .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
                
                // 材质实例：使用材质的默认实例
                // 参数：图元索引，材质实例
                .material(0, app.mat->getDefaultInstance())
                
                // 几何数据：定义如何渲染
                // 参数：图元索引，图元类型（三角形），顶点缓冲区，索引缓冲区，起始索引，索引数量
                .geometry(0, 
                         RenderableManager::PrimitiveType::TRIANGLES,  // 三角形图元
                         app.vb,                                        // 顶点缓冲区
                         app.ib,                                        // 索引缓冲区
                         0,                                             // 索引起始位置
                         3)                                             // 使用3个索引（一个三角形）
                
                // 渲染选项
                .culling(false)          // 禁用背面剔除（两面都渲染）
                .receiveShadows(false)   // 不接收阴影
                .castShadows(false)      // 不投射阴影
                
                // 构建Renderable组件并附加到实体
                .build(*engine, app.renderable);

        // ========== 4. 更新相机投影矩阵 ==========
        /**
         * 设置正交投影（Orthographic Projection）
         * 
         * 正交投影特点：
         * - 平行投影，没有透视效果
         * - 物体大小不随距离变化
         * - 适合2D渲染或等距视图
         * 
         * 投影参数：
         * - left, right: 左右边界（根据宽高比调整）
         * - bottom, top: 上下边界（固定为-ZOOM到+ZOOM）
         * - near, far: 近远平面（0到1）
         */
        constexpr float ZOOM = 1.5f;                    // 缩放因子
        const uint32_t w = view->getViewport().width;   // 视口宽度
        const uint32_t h = view->getViewport().height;  // 视口高度
        const float aspect = (float) w / h;              // 宽高比
        
        // 设置正交投影矩阵
        // 左右边界根据宽高比调整，保持正确的比例
        app.cam->setProjection(Camera::Projection::ORTHO,
            -aspect * ZOOM,   // 左边界
            aspect * ZOOM,    // 右边界
            -ZOOM,            // 下边界
            ZOOM,             // 上边界
            0,                // 近平面
            1);               // 远平面

        // ========== 5. 更新旋转变换 ==========
        /**
         * 设置旋转变换矩阵
         * 
         * 实现效果：整个三角形绕Z轴旋转
         * - 旋转角度 = now（时间），所以会持续旋转
         * - 旋转轴 = (0, 0, 1)，即Z轴（垂直于屏幕）
         * 
         * 变换矩阵的作用：
         * - 在顶点着色器中，每个顶点位置会乘以这个矩阵
         * - 实现物体的旋转、平移、缩放等变换
         */
        auto& tcm = engine->getTransformManager();
        tcm.setTransform(
                tcm.getInstance(app.renderable),  // 获取实体的变换组件实例
                filament::math::mat4f::rotation(  // 创建旋转矩阵
                    now,                          // 旋转角度（弧度）
                    filament::math::float3{ 0, 0, 1 }  // 旋转轴（Z轴）
                )
        );
    });

    /**
     * 运行Filament应用程序
     * 
     * FilamentApp::run() 会：
     * 1. 初始化Filament引擎
     * 2. 创建窗口和渲染上下文
     * 3. 调用setup函数初始化资源
     * 4. 进入主循环：
     *    - 调用animate函数更新动画
     *    - 渲染场景
     *    - 交换缓冲区显示画面
     * 5. 退出时调用cleanup函数清理资源
     * 
     * 参数：
     * - config: 应用配置（窗口标题、后端等）
     * - setup: 初始化函数
     * - cleanup: 清理函数
     */
    FilamentApp::get().run(config, setup, cleanup);

    return 0;
}
