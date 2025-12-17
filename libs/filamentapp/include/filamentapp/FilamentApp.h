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

#ifndef TNT_FILAMENT_SAMPLE_FILAMENTAPP_H
#define TNT_FILAMENT_SAMPLE_FILAMENTAPP_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <SDL.h>

#include <filament/Engine.h>
#include <filament/Viewport.h>

#include <camutils/Manipulator.h>

#include <utils/Path.h>
#include <utils/Entity.h>

#include "Config.h"
#include "IBL.h"

namespace filament {
class Renderer;
class Scene;
class View;
} // namespace filament

namespace filagui {
class ImGuiHelper;
} // namespace filagui

class IBL;
class MeshAssimp;

// For customizing the vulkan backend
namespace filament::backend {
#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
class VulkanPlatform;
#endif

#if defined(FILAMENT_SUPPORTS_WEBGPU)
class WebGPUPlatform;
#endif

}

/**
 * FilamentApp - Filament渲染引擎应用程序框架类
 * 
 * 该类提供了完整的Filament渲染应用程序框架，包括窗口管理、事件处理、
 * 渲染循环、相机控制、IBL（基于图像的光照）加载等功能。
 * 主要用于示例程序和测试应用。
 */
class FilamentApp {
public:
    /** 设置回调函数类型：在场景初始化时调用 */
    using SetupCallback = std::function<void(filament::Engine*, filament::View*, filament::Scene*)>;
    /** 清理回调函数类型：在场景销毁时调用 */
    using CleanupCallback =
            std::function<void(filament::Engine*, filament::View*, filament::Scene*)>;
    /** 预渲染回调函数类型：在每帧渲染前调用 */
    using PreRenderCallback = std::function<void(filament::Engine*, filament::View*,
            filament::Scene*, filament::Renderer*)>;
    /** 后渲染回调函数类型：在每帧渲染后调用 */
    using PostRenderCallback = std::function<void(filament::Engine*, filament::View*,
            filament::Scene*, filament::Renderer*)>;
    /** ImGui回调函数类型：用于UI渲染 */
    using ImGuiCallback = std::function<void(filament::Engine*, filament::View*)>;
    /** 动画回调函数类型：用于场景动画更新 */
    using AnimCallback = std::function<void(filament::Engine*, filament::View*, double now)>;
    /** 窗口大小改变回调函数类型 */
    using ResizeCallback = std::function<void(filament::Engine*, filament::View*)>;
    /** 文件拖放回调函数类型 */
    using DropCallback = std::function<void(std::string_view)>;

    /**
     * 获取FilamentApp单例实例
     * @return FilamentApp的引用
     */
    static FilamentApp& get();

    /** 析构函数：清理资源 */
    ~FilamentApp();

    /**
     * 设置动画回调函数
     * @param animation 动画回调函数，用于每帧更新场景动画
     */
    void animate(AnimCallback animation) { mAnimation = animation; }

    /**
     * 设置窗口大小改变回调函数
     * @param resize 窗口大小改变时的回调函数
     */
    void resize(ResizeCallback resize) { mResize = resize; }

    /**
     * 设置文件拖放处理函数
     * @param handler 处理拖放文件的回调函数
     */
    void setDropHandler(DropCallback handler) { mDropHandler = handler; }

    /**
     * 运行应用程序主循环
     * @param config 应用程序配置参数
     * @param setup 场景初始化回调函数
     * @param cleanup 场景清理回调函数
     * @param imgui ImGui UI回调函数（可选）
     * @param preRender 预渲染回调函数（可选）
     * @param postRender 后渲染回调函数（可选）
     * @param width 窗口宽度，默认1024
     * @param height 窗口高度，默认640
     */
    void run(const Config& config, SetupCallback setup, CleanupCallback cleanup,
            ImGuiCallback imgui = ImGuiCallback(), PreRenderCallback preRender = PreRenderCallback(),
            PostRenderCallback postRender = PostRenderCallback(),
            size_t width = 1024, size_t height = 640);

    /**
     * 标记需要重新配置相机
     * 在下一帧渲染前会重新配置相机参数
     */
    void reconfigureCameras() { mReconfigureCameras = true; }

    /**
     * 获取默认材质
     * @return 默认材质的常量指针
     */
    filament::Material const* getDefaultMaterial() const noexcept { return mDefaultMaterial; }
    /**
     * 获取透明材质
     * @return 透明材质的常量指针
     */
    filament::Material const* getTransparentMaterial() const noexcept { return mTransparentMaterial; }
    /**
     * 获取IBL（基于图像的光照）对象
     * @return IBL对象的指针
     */
    IBL* getIBL() const noexcept { return mIBL.get(); }
    /**
     * 获取污垢纹理（用于屏幕空间反射等效果）
     * @return 污垢纹理的指针
     */
    filament::Texture* getDirtTexture() const noexcept { return mDirt; }
    /**
     * 获取GUI视图
     * @return GUI视图的指针
     */
    filament::View* getGuiView() const noexcept;

    /**
     * 关闭应用程序
     * 设置关闭标志，主循环会在下一帧退出
     */
    void close() { mClosed = true; }

    /**
     * 设置侧边栏宽度
     * @param width 侧边栏宽度（像素）
     */
    void setSidebarWidth(int width) { mSidebarWidth = width; }
    /**
     * 设置窗口标题
     * @param title 窗口标题字符串
     */
    void setWindowTitle(const char* title) { mWindowTitle = title; }
    /**
     * 设置相机焦距
     * @param focalLength 焦距值（毫米），默认28.0
     */
    void setCameraFocalLength(float focalLength) { mCameraFocalLength = focalLength; }
    /**
     * 设置相机近远裁剪平面
     * @param near 近裁剪平面距离
     * @param far 远裁剪平面距离
     */
    void setCameraNearFar(float near, float far) { mCameraNear = near; mCameraFar = far; }

    /**
     * 添加离屏渲染视图
     * @param view 要添加的离屏视图指针
     */
    void addOffscreenView(filament::View* view) { mOffscreenViews.push_back(view); }

    /**
     * 获取跳过的帧数统计
     * @return 跳过的帧数
     */
    size_t getSkippedFrameCount() const { return mSkippedFrames; }

    /**
     * 加载IBL（基于图像的光照）资源
     * @param path IBL资源路径，可以是目录或单个文件
     */
    void loadIBL(std::string_view path);


    // debugging: enable/disable the froxel grid
    /**
     * 启用/禁用相机视锥体可视化
     * @param enabled true启用，false禁用
     */
    void setCameraFrustumEnabled(bool enabled) noexcept;
    /**
     * 启用/禁用方向光阴影视锥体可视化
     * @param enabled true启用，false禁用
     */
    void setDirectionalShadowFrustumEnabled(bool enabled) noexcept;
    /**
     * 启用/禁用Froxel网格可视化（用于光照计算）
     * @param enabled true启用，false禁用
     */
    void setFroxelGridEnabled(bool enabled) noexcept;
    /**
     * 检查相机视锥体可视化是否启用
     * @return true已启用，false已禁用
     */
    bool isCameraFrustumEnabled() const noexcept;
    /**
     * 检查方向光阴影视锥体可视化是否启用
     * @return true已启用，false已禁用
     */
    bool isDirectionalShadowFrustumEnabled() const noexcept;
    /**
     * 检查Froxel网格可视化是否启用
     * @return true已启用，false已禁用
     */
    bool isFroxelGridEnabled() const noexcept;

    FilamentApp(const FilamentApp& rhs) = delete;
    FilamentApp(FilamentApp&& rhs) = delete;
    FilamentApp& operator=(const FilamentApp& rhs) = delete;
    FilamentApp& operator=(FilamentApp&& rhs) = delete;

    /**
     * Returns the path to the Filament root for loading assets. This is determined from the
     * executable folder, which allows users to launch samples from any folder.
     *
     * This takes into account multi-configuration CMake generators, like Visual Studio or Xcode,
     * that have different executable paths compared to single-configuration generators, like Ninja.
     */
    static const utils::Path& getRootAssetsPath();

private:
    /** 私有构造函数：单例模式 */
    FilamentApp();

    /** 相机操作器类型别名 */
    using CameraManipulator = filament::camutils::Manipulator<float>;

    /**
     * 将SDL键盘扫描码转换为相机操作器按键
     * @param scancode SDL键盘扫描码
     * @param key 输出的相机操作器按键引用
     * @return 转换是否成功
     */
    static bool manipulatorKeyFromKeycode(SDL_Scancode scancode, CameraManipulator::Key& key);

    /**
     * CView - 视图封装类
     * 
     * 封装了Filament的View对象，提供鼠标和键盘事件处理、
     * 相机操作器管理等功能。
     */
    class CView {
    public:
        /**
         * 构造函数
         * @param renderer 渲染器引用
         * @param name 视图名称
         */
        CView(filament::Renderer& renderer, std::string name);
        /** 虚析构函数 */
        virtual ~CView();

        /**
         * 设置相机操作器
         * @param cm 相机操作器指针
         */
        void setCameraManipulator(CameraManipulator* cm);
        /**
         * 设置视口
         * @param viewport 视口参数
         */
        void setViewport(filament::Viewport const& viewport);
        /**
         * 设置相机
         * @param camera 相机指针
         */
        void setCamera(filament::Camera* camera);
        /**
         * 检查点是否在视口内
         * @param x X坐标
         * @param y Y坐标
         * @return true在视口内，false不在
         */
        bool intersects(ssize_t x, ssize_t y);

        /**
         * 处理鼠标按下事件
         * @param button 鼠标按钮（1=左键，2=中键，3=右键）
         * @param x X坐标
         * @param y Y坐标
         */
        virtual void mouseDown(int button, ssize_t x, ssize_t y);
        /**
         * 处理鼠标释放事件
         * @param x X坐标
         * @param y Y坐标
         */
        virtual void mouseUp(ssize_t x, ssize_t y);
        /**
         * 处理鼠标移动事件
         * @param x X坐标
         * @param y Y坐标
         */
        virtual void mouseMoved(ssize_t x, ssize_t y);
        /**
         * 处理鼠标滚轮事件
         * @param x 滚轮滚动量
         */
        virtual void mouseWheel(ssize_t x);
        /**
         * 处理按键按下事件
         * @param scancode SDL键盘扫描码
         */
        virtual void keyDown(SDL_Scancode scancode);
        /**
         * 处理按键释放事件
         * @param scancode SDL键盘扫描码
         */
        virtual void keyUp(SDL_Scancode scancode);

        /**
         * 获取视图对象（常量版本）
         * @return 视图对象的常量指针
         */
        filament::View const* getView() const { return view; }
        /**
         * 获取视图对象
         * @return 视图对象的指针
         */
        filament::View* getView() { return view; }
        /**
         * 获取相机操作器
         * @return 相机操作器指针
         */
        CameraManipulator* getCameraManipulator() { return mCameraManipulator; }

    private:
        /** 鼠标操作模式枚举 */
        enum class Mode : uint8_t {
            NONE,   // 无操作
            ROTATE, // 旋转
            TRACK   // 平移
        };

        filament::Engine& engine;              // Filament引擎引用
        filament::Viewport mViewport;          // 视口参数
        filament::View* view = nullptr;        // Filament视图对象
        CameraManipulator* mCameraManipulator = nullptr; // 相机操作器
        std::string mName;                     // 视图名称
    };

    /**
     * GodView - 上帝视角视图类
     * 
     * 继承自CView，提供调试用的上帝视角相机功能。
     */
    class GodView : public CView {
    public:
        using CView::CView;
        /**
         * 设置上帝视角相机
         * @param camera 上帝视角相机指针
         */
        void setGodCamera(filament::Camera* camera);
    };

    /**
     * Window - 窗口管理类
     * 
     * 负责管理SDL窗口、Filament引擎、渲染器、交换链、
     * 相机和视图等核心渲染资源。
     */
    class Window {
        friend class FilamentApp;
    public:
        /**
         * 构造函数
         * @param filamentApp FilamentApp实例指针
         * @param config 应用程序配置
         * @param title 窗口标题
         * @param w 窗口宽度
         * @param h 窗口高度
         */
        Window(FilamentApp* filamentApp, const Config& config,
               std::string title, size_t w, size_t h);
        /** 虚析构函数 */
        virtual ~Window();

        /**
         * 处理鼠标按下事件
         * @param button 鼠标按钮
         * @param x X坐标
         * @param y Y坐标
         */
        void mouseDown(int button, ssize_t x, ssize_t y);
        /**
         * 处理鼠标释放事件
         * @param x X坐标
         * @param y Y坐标
         */
        void mouseUp(ssize_t x, ssize_t y);
        /**
         * 处理鼠标移动事件
         * @param x X坐标
         * @param y Y坐标
         */
        void mouseMoved(ssize_t x, ssize_t y);
        /**
         * 处理鼠标滚轮事件
         * @param x 滚轮滚动量
         */
        void mouseWheel(ssize_t x);
        /**
         * 处理按键按下事件
         * @param scancode SDL键盘扫描码
         */
        void keyDown(SDL_Scancode scancode);
        /**
         * 处理按键释放事件
         * @param scancode SDL键盘扫描码
         */
        void keyUp(SDL_Scancode scancode);
        /**
         * 处理窗口大小改变事件
         * 重新配置相机和视口
         */
        void resize();

        /**
         * 获取渲染器
         * @return 渲染器指针
         */
        filament::Renderer* getRenderer() { return mRenderer; }
        /**
         * 获取交换链
         * @return 交换链指针
         */
        filament::SwapChain* getSwapChain() { return mSwapChain; }

        /**
         * 获取SDL窗口对象
         * @return SDL窗口指针
         */
        SDL_Window* getSDLWindow() {
            return mWindow;
        }

    private:
        /**
         * 为窗口配置相机
         * 根据窗口大小、DPI缩放、侧边栏宽度等参数配置所有相机
         */
        void configureCamerasForWindow();
        /**
         * 修正高DPI显示器的鼠标坐标
         * @param x X坐标的引用（会被修改）
         * @param y Y坐标的引用（会被修改）
         */
        void fixupMouseCoordinatesForHdpi(ssize_t& x, ssize_t& y) const;

        FilamentApp* const mFilamentApp = nullptr;  // FilamentApp实例指针
        Config mConfig;                              // 应用程序配置
        const bool mIsHeadless;                      // 是否为无头模式（无窗口）

        SDL_Window* mWindow = nullptr;               // SDL窗口对象
        filament::Renderer* mRenderer = nullptr;     // Filament渲染器
        filament::Engine::Backend mBackend;         // 渲染后端类型

        CameraManipulator* mMainCameraMan;          // 主相机操作器
        CameraManipulator* mDebugCameraMan;         // 调试相机操作器
        filament::SwapChain* mSwapChain = nullptr;  // 交换链对象

        utils::Entity mCameraEntities[3];           // 相机实体数组（主相机、调试相机、正交相机）
        filament::Camera* mCameras[3] = { nullptr }; // 相机对象数组
        filament::Camera* mMainCamera;              // 主相机
        filament::Camera* mDebugCamera;             // 调试相机（用于GodView）
        filament::Camera* mOrthoCamera;              // 正交相机（用于阴影贴图视图）

        std::vector<std::unique_ptr<CView>> mViews; // 视图列表
        CView* mMainView;   // 主视图
        CView* mUiView;     // UI视图（ImGui）
        CView* mDepthView; // 深度视图
        GodView* mGodView;  // 上帝视角视图（调试用）
        CView* mOrthoView;  // 方向光阴影贴图视图

        size_t mWidth = 0;      // 窗口宽度（物理像素）
        size_t mHeight = 0;     // 窗口高度（物理像素）
        ssize_t mLastX = 0;      // 上次鼠标X坐标
        ssize_t mLastY = 0;      // 上次鼠标Y坐标

        CView* mMouseEventTarget = nullptr; // 当前接收鼠标事件的视图

        // 跟踪哪个视图应该接收按键的keyUp事件
        std::unordered_map<SDL_Scancode, CView*> mKeyEventTarget;
    };

    friend class Window;
    /**
     * 初始化SDL库
     * 仅初始化事件子系统
     */
    void initSDL();

    /**
     * 从配置加载IBL
     * @param config 应用程序配置
     */
    void loadIBL(const Config& config);
    /**
     * 从配置加载污垢纹理
     * @param config 应用程序配置
     */
    void loadDirt(const Config& config);

    filament::Engine* mEngine = nullptr;                    // Filament引擎实例
    filament::Scene* mScene = nullptr;                     // 场景对象
    std::unique_ptr<IBL> mIBL;                             // IBL对象（基于图像的光照）
    filament::Texture* mDirt = nullptr;                     // 污垢纹理（用于屏幕空间反射）
    bool mClosed = false;                                   // 应用程序关闭标志
    uint64_t mTime = 0;                                     // 上一帧时间戳

    filament::Material const* mDefaultMaterial = nullptr;   // 默认材质
    filament::Material const* mTransparentMaterial = nullptr; // 透明材质
    filament::Material const* mDepthMaterial = nullptr;     // 深度可视化材质
    filament::MaterialInstance* mDepthMI = nullptr;         // 深度可视化材质实例
    std::unique_ptr<filagui::ImGuiHelper> mImGuiHelper;     // ImGui辅助对象
    AnimCallback mAnimation;                                // 动画回调函数
    ResizeCallback mResize;                                 // 窗口大小改变回调函数
    DropCallback mDropHandler;                              // 文件拖放处理函数
    int mSidebarWidth = 0;                                  // 侧边栏宽度
    size_t mSkippedFrames = 0;                              // 跳过的帧数统计
    std::string mWindowTitle;                               // 窗口标题
    std::vector<filament::View*> mOffscreenViews;          // 离屏渲染视图列表
    float mCameraFocalLength = 28.0f;                       // 相机焦距（毫米）
    float mCameraNear = 0.1f;                              // 相机近裁剪平面
    float mCameraFar = 100.0f;                              // 相机远裁剪平面
    bool mReconfigureCameras = false;                       // 是否需要重新配置相机
    uint8_t mFroxelInfoAge = 0x42;                          // Froxel信息年龄（用于检测更新）
    uint8_t mFroxelGridEnabled = 0;                        // Froxel网格可视化启用标志
    uint8_t mDirectionalShadowFrustumEnabled = 0x2;         // 方向光阴影视锥体可视化启用标志
    uint8_t mCameraFrustumEnabled = 0x2;                    // 相机视锥体可视化启用标志

#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
    filament::backend::VulkanPlatform* mVulkanPlatform = nullptr;
#endif

#if defined(FILAMENT_SUPPORTS_WEBGPU)
    filament::backend::WebGPUPlatform* mWebGPUPlatform = nullptr;
#endif

};

#endif // TNT_FILAMENT_SAMPLE_FILAMENTAPP_H
