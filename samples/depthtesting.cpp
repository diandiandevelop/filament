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

// 包含命令行参数解析工具
#include "common/arguments.h"

// Filament 渲染引擎核心头文件
#include <filament/Camera.h>           // 相机类
#include <filament/Engine.h>           // 渲染引擎
#include <filament/IndexBuffer.h>      // 索引缓冲区
#include <filament/Material.h>         // 材质
#include <filament/MaterialInstance.h> // 材质实例
#include <filament/RenderableManager.h> // 可渲染对象管理器
#include <filament/Scene.h>            // 场景
#include <filament/Skybox.h>           // 天空盒
#include <filament/TransformManager.h> // 变换管理器
#include <filament/VertexBuffer.h>     // 顶点缓冲区
#include <filament/View.h>             // 视图

// ImGui 用于创建调试UI界面
#include <imgui.h>

// 实体管理器工具
#include <utils/EntityManager.h>

// Filament 应用框架配置和应用类
#include <filamentapp/Config.h>
#include <filamentapp/FilamentApp.h>

// 数学库，用于三角函数计算
#include <cmath>

// 生成的资源文件（包含预编译的着色器）
#include "generated/resources/resources.h"

using namespace filament;
using utils::Entity;
using utils::EntityManager;

// 应用程序状态结构体，保存所有渲染相关的资源
struct App {
    VertexBuffer* vb;              // 顶点缓冲区
    IndexBuffer* ib;                // 索引缓冲区
    Material* mat;                  // 材质对象
    Camera* cam;                    // 相机对象
    Entity camera;                  // 相机实体
    Skybox* skybox;                 // 天空盒
    Entity whiteTriangle;           // 白色三角形实体
    Entity colorTriangle;           // 彩色三角形实体
    MaterialInstance::DepthFunc depthFunc; // 深度测试函数类型
};

// 顶点数据结构：包含位置和颜色
struct Vertex {
    filament::math::float2 position; // 2D位置坐标
    uint32_t color;                  // RGBA颜色（打包为32位整数）
};

// 定义三角形的三个顶点，形成一个等边三角形
// 顶点位置：第一个在右侧(1,0)，另外两个均匀分布在圆周上
// 颜色：红色、绿色、蓝色
static const Vertex TRIANGLE_VERTICES[3] = {
    {{1, 0}, 0xffff0000u},  // 右侧顶点，红色 (RGBA: FF0000FF)
    {{cos(M_PI * 2 / 3), sin(M_PI * 2 / 3)}, 0xff00ff00u},  // 左上顶点，绿色 (RGBA: 00FF00FF)
    {{cos(M_PI * 4 / 3), sin(M_PI * 4 / 3)}, 0xff0000ffu},  // 左下顶点，蓝色 (RGBA: 0000FFFF)
};

// 三角形索引：定义三个顶点如何连接成三角形（逆时针顺序）
static constexpr uint16_t TRIANGLE_INDICES[3] = { 0, 1, 2 };

int main(int argc, char** argv) {
    // 配置应用程序
    Config config;
    config.title = "depthtesting";  // 窗口标题
    config.backend = samples::parseArgumentsForBackend(argc, argv);  // 从命令行参数解析渲染后端

    App app;
    // 初始化函数：设置场景、相机、几何体等
    auto setup = [&app](Engine* engine, View* view, Scene* scene) {
        // 创建天空盒，使用深蓝色背景
        app.skybox = Skybox::Builder().color({0.1, 0.125, 0.25, 1.0}).build(*engine);
        scene->setSkybox(app.skybox);

        // 创建相机实体和相机组件
        app.camera = utils::EntityManager::get().create();
        app.cam = engine->createCamera(app.camera);
        view->setCamera(app.cam);
        view->setPostProcessingEnabled(false);  // 禁用后处理效果
        
        // 创建顶点缓冲区
        // 顶点数量：3个
        // 缓冲区数量：1个
        // 属性定义：
        //   - POSITION: 位置属性，使用FLOAT2类型，偏移0，步长12字节
        //   - COLOR: 颜色属性，使用UBYTE4类型，偏移8字节，步长12字节
        //   颜色属性需要归一化（0-255映射到0.0-1.0）
        app.vb = VertexBuffer::Builder()
                .vertexCount(3)
                .bufferCount(1)
                .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT2, 0, 12)
                .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::UBYTE4, 8, 12)
                .normalized(VertexAttribute::COLOR)
                .build(*engine);
        // 设置顶点数据（36字节 = 3个顶点 × 12字节/顶点）
        app.vb->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(TRIANGLE_VERTICES, 36, nullptr));
        
        // 创建索引缓冲区
        // 索引数量：3个
        // 索引类型：16位无符号短整型
        app.ib = IndexBuffer::Builder()
                .indexCount(3)
                .bufferType(IndexBuffer::IndexType::USHORT)
                .build(*engine);
        // 设置索引数据（6字节 = 3个索引 × 2字节/索引）
        app.ib->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(TRIANGLE_INDICES, 6, nullptr));

        // 创建白色三角形（使用默认材质）
        app.whiteTriangle = EntityManager::get().create();
        RenderableManager::Builder(1)  // 1个渲染原语槽位
                .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})  // 包围盒
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0, 3)  // 几何体定义
                .culling(false)         // 禁用背面剔除
                .receiveShadows(false)  // 不接收阴影
                .castShadows(false)     // 不投射阴影
                .build(*engine, app.whiteTriangle);
        scene->addEntity(app.whiteTriangle);

        // 创建彩色三角形（使用自定义材质）
        app.colorTriangle = EntityManager::get().create();
        // 从预编译的资源包中创建材质（使用baked color着色器）
        app.mat = Material::Builder()
                .package(RESOURCES_BAKEDCOLOR_DATA, RESOURCES_BAKEDCOLOR_SIZE)
                .build(*engine);
        RenderableManager::Builder(1)
                .boundingBox({{ -1, -1, -1 }, { 1, 1, 1 }})
                .material(0, app.mat->getDefaultInstance())  // 使用材质的默认实例
                .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, app.vb, app.ib, 0, 3)
                .culling(false)
                .receiveShadows(false)
                .castShadows(false)
                .priority(5)  // 设置渲染优先级为5，确保在白色三角形之后绘制
                .build(*engine, app.colorTriangle);
        scene->addEntity(app.colorTriangle);

        // 初始化深度测试函数为"大于等于"（GE = Greater or Equal）
        app.depthFunc = MaterialInstance::DepthFunc::GE;
    };

    // 清理函数：释放所有创建的资源
    auto cleanup = [&app](Engine* engine, View*, Scene*) {
        engine->destroy(app.skybox);
        engine->destroy(app.whiteTriangle);
        engine->destroy(app.colorTriangle);
        engine->destroy(app.mat);
        engine->destroy(app.vb);
        engine->destroy(app.ib);

        // 销毁相机组件和实体
        engine->destroyCameraComponent(app.camera);
        utils::EntityManager::get().destroy(app.camera);
    };

    // GUI函数：创建调试界面，允许用户实时修改深度测试函数
    auto gui = [&app](Engine* engine, View* view) {
        int depthFuncSelection = (int) app.depthFunc;
        // 创建下拉菜单，包含所有可用的深度测试函数选项
        // 选项包括：小于等于、大于等于、严格小于、严格大于、等于、不等于、总是通过、永远不通过
        ImGui::Combo("Depth Function", &depthFuncSelection,
                "Less or equal\0Greater or equal\0Strictly less than\0"
                "Strictly greater than\0Equal\0Not equal\0Always\0Never\0\0");
        // 如果用户改变了选择，更新材质实例的深度测试函数
        if (depthFuncSelection != (int) app.depthFunc) {
            app.depthFunc = (MaterialInstance::DepthFunc) depthFuncSelection;
            app.mat->getDefaultInstance()->setDepthFunc(app.depthFunc);
        }
    };

    // 动画函数：每帧调用，更新相机投影和三角形旋转
    FilamentApp::get().animate([&app](Engine* engine, View* view, double now) {
        constexpr float ZOOM = 1.5f;  // 缩放因子
        // 获取视口尺寸
        const uint32_t w = view->getViewport().width;
        const uint32_t h = view->getViewport().height;
        const float aspect = (float) w / h;  // 计算宽高比
        
        // 设置正交投影相机
        // 参数：投影类型、左边界、右边界、下边界、上边界、近平面、远平面
        app.cam->setProjection(Camera::Projection::ORTHO,
                -aspect * ZOOM, aspect * ZOOM,  // 左右边界（根据宽高比调整）
                -ZOOM, ZOOM,                     // 上下边界
                -5, 5);                          // 近远平面
        
        // 获取变换管理器并旋转彩色三角形
        // 绕Y轴旋转，旋转角度为当前时间（now），实现持续旋转动画
        auto& tcm = engine->getTransformManager();
        tcm.setTransform(tcm.getInstance(app.colorTriangle),
                filament::math::mat4f::rotation(now, filament::math::float3{ 0, 1, 0 }));
    });

    // 运行应用程序，传入配置和回调函数
    FilamentApp::get().run(config, setup, cleanup, gui);

    return 0;
}
