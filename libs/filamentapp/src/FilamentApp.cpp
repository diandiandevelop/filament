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

#include <filamentapp/FilamentApp.h>

#include "KeyInputConversion.h"

#if defined(WIN32)
#    include <SDL_syswm.h>
#    include <utils/unwindows.h>
#endif

#include <iostream>

#include <imgui.h>

#include <utils/EntityManager.h>
#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/Path.h>

#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/Renderer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/View.h>

#include <backend/Platform.h>

#ifndef NDEBUG
#include <filament/DebugRegistry.h>
#endif

#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
#include <backend/platforms/VulkanPlatform.h>
#include <filamentapp/VulkanPlatformHelper.h>
#endif

#if defined(FILAMENT_SUPPORTS_WEBGPU)
    #if defined(__ANDROID__)
        #include "backend/platforms/WebGPUPlatformAndroid.h"
    #elif defined(__APPLE__)
        #include "backend/platforms/WebGPUPlatformApple.h"
    #elif defined(__linux__)
        #include "backend/platforms/WebGPUPlatformLinux.h"
    #elif defined(WIN32)
        #include "backend/platforms/WebGPUPlatformWindows.h"
    #endif
#endif

#include <filagui/ImGuiHelper.h>

#include <filamentapp/Cube.h>
#include <filamentapp/Grid.h>
#include <filamentapp/NativeWindowHelper.h>

#include <stb_image.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include <stdint.h>

#include "generated/resources/filamentapp.h"

using namespace filament;
using namespace filagui;
using namespace filament::math;
using namespace utils;

namespace {

using namespace filament::backend;

#if defined(FILAMENT_SUPPORTS_WEBGPU)
/**
 * FilamentAppWebGPUPlatform - WebGPU平台实现类
 * 
 * 继承自WebGPUPlatform，提供WebGPU后端配置功能。
 * 支持强制指定WebGPU的后端类型（Vulkan或Metal）。
 */
class FilamentAppWebGPUPlatform : public WebGPUPlatform {
    #if defined(__ANDROID__)
        class FilamentAppWebGPUPlatform : public WebGPUPlatformAndroid {
    #elif defined(__APPLE__)
        class FilamentAppWebGPUPlatform : public WebGPUPlatformApple {
    #elif defined(__linux__)
        class FilamentAppWebGPUPlatform : public WebGPUPlatformLinux {
    #elif defined(WIN32)
        class FilamentAppWebGPUPlatform : public WebGPUPlatformWindows {
    #endif
public:
    /**
     * 构造函数
     * @param backend WebGPU后端类型（用于强制指定底层后端）
     */
    FilamentAppWebGPUPlatform(Config::WebGPUBackend backend)
        : mBackend(backend) {}

    /**
     * 获取WebGPU平台配置实现
     * 
     * 执行步骤：
     * 1. 根据配置的后端类型设置forceBackendType
     * 2. 如果是不支持的后端类型，记录错误并忽略
     * 3. 返回配置对象
     * 
     * @return WebGPU平台配置
     */
    virtual WebGPUPlatform::Configuration getConfiguration() const noexcept override {
        WebGPUPlatform::Configuration config = {};
        switch (mBackend) {
            case Config::WebGPUBackend::VULKAN:
                // 强制使用Vulkan作为WebGPU的底层后端
                config.forceBackendType = wgpu::BackendType::Vulkan;
                break;
            case Config::WebGPUBackend::METAL:
                // 强制使用Metal作为WebGPU的底层后端
                config.forceBackendType = wgpu::BackendType::Metal;
                break;
            case Config::WebGPUBackend::DEFAULT:
                // 使用默认后端（由WebGPU运行时选择）
                break;
            default:
                // 不支持的后端类型，记录错误
                LOG(ERROR) << "FilamentApp: Unsupported webgpu backend was selected(="
                           << (int) mBackend << "). Selection is ignored.";
                break;
        }
        return config;
    }

private:
    Config::WebGPUBackend const mBackend;  // WebGPU后端类型
};
#endif

}

/**
 * 获取FilamentApp单例实例实现
 * 
 * @return FilamentApp的引用
 */
FilamentApp& FilamentApp::get() {
    static FilamentApp filamentApp;
    return filamentApp;
}

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 初始化SDL库
 */
FilamentApp::FilamentApp() {
    initSDL();
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 退出SDL库
 */
FilamentApp::~FilamentApp() {
    SDL_Quit();
}

/**
 * 获取GUI视图实现
 * 
 * @return GUI视图的指针
 */
View* FilamentApp::getGuiView() const noexcept {
    return mImGuiHelper->getView();
}

/**
 * 运行应用程序主循环实现
 * 
 * 执行步骤：
 * 1. 创建窗口和渲染资源
 * 2. 创建材质（深度可视化、默认、透明）
 * 3. 创建调试可视化对象（相机视锥体、Froxel网格等）
 * 4. 加载IBL和污垢纹理
 * 5. 设置场景和视图
 * 6. 调用用户设置回调
 * 7. 初始化ImGui（如果提供回调）
 * 8. 进入主循环：
 *    - 处理事件（鼠标、键盘、窗口等）
 *    - 更新动画
 *    - 更新相机
 *    - 更新Froxel网格可视化
 *    - 渲染所有视图
 * 9. 清理资源
 */
void FilamentApp::run(const Config& config, SetupCallback setupCallback,
        CleanupCallback cleanupCallback, ImGuiCallback imguiCallback,
        PreRenderCallback preRender, PostRenderCallback postRender,
        size_t width, size_t height) {
    // 设置窗口标题
    mWindowTitle = config.title;
    // 创建窗口对象（包含SDL窗口、Filament引擎、渲染器、相机、视图等）
    std::unique_ptr<FilamentApp::Window> window(
            new FilamentApp::Window(this, config, config.title, width, height));

    // 创建深度可视化材质（用于调试视图显示深度信息）
    mDepthMaterial = Material::Builder()
            .package(FILAMENTAPP_DEPTHVISUALIZER_DATA, FILAMENTAPP_DEPTHVISUALIZER_SIZE)
            .build(*mEngine);

    mDepthMI = mDepthMaterial->createInstance();

    // 创建默认材质（不透明，用于默认渲染）
    mDefaultMaterial = Material::Builder()
            .package(FILAMENTAPP_AIDEFAULTMAT_DATA, FILAMENTAPP_AIDEFAULTMAT_SIZE)
            .build(*mEngine);

    // 创建透明材质（用于调试可视化对象）
    mTransparentMaterial = Material::Builder()
            .package(FILAMENTAPP_TRANSPARENTCOLOR_DATA, FILAMENTAPP_TRANSPARENTCOLOR_SIZE)
            .build(*mEngine);

    // 创建相机视锥体可视化立方体（红色）
    std::unique_ptr<Cube> cameraCube{ new Cube(*mEngine, mTransparentMaterial, { 1, 0, 0 }) };
    // 创建Froxel网格可视化网格（黄色）
    std::unique_ptr<Grid> cameraGrid{ new Grid(*mEngine, mTransparentMaterial, { 1, 1, 0 }) };

    // we can't cull the light-frustum because it's not applied a rigid transform
    // and currently, filament assumes that for culling
    // 我们不能对光视锥体进行剔除，因为它没有应用刚体变换，而Filament目前假设剔除需要刚体变换
    // 创建方向光阴影视锥体可视化立方体（4个，对应4级级联阴影贴图）
    std::vector<Cube> lightmapCubes;
    lightmapCubes.reserve(4);
    lightmapCubes.emplace_back(*mEngine, mTransparentMaterial, float3{ 0, 1, 0 }, false);  // 绿色
    lightmapCubes.emplace_back(*mEngine, mTransparentMaterial, float3{ 0, 0, 1 }, false);  // 蓝色
    lightmapCubes.emplace_back(*mEngine, mTransparentMaterial, float3{ 1, 1, 0 }, false);  // 黄色
    lightmapCubes.emplace_back(*mEngine, mTransparentMaterial, float3{ 1, 0, 0 }, false);  // 红色

    // 创建场景对象
    mScene = mEngine->createScene();

    // 设置主视图的可见层（0x4）和Froxel可视化
    window->mMainView->getView()->setVisibleLayers(0x4, 0x4);
    window->mMainView->getView()->setFroxelVizEnabled(true);

    // 如果启用分屏视图，添加调试可视化对象到场景
    if (config.splitView) {
        // 添加相机视锥体可视化（实体和线框）
        mScene->addEntity(cameraCube->getSolidRenderable());
        mScene->addEntity(cameraCube->getWireFrameRenderable());
        // 添加方向光阴影视锥体可视化（4个级联）
        for (auto&& cube : lightmapCubes) {
            mScene->addEntity(cube.getSolidRenderable());
            mScene->addEntity(cube.getWireFrameRenderable());
        }

        // 设置各调试视图的可见层
        window->mDepthView->getView()->setVisibleLayers(0x4, 0x4);   // 深度视图：层0x4
        window->mGodView->getView()->setVisibleLayers(0x6, 0x6);     // 上帝视角：层0x6
        window->mOrthoView->getView()->setVisibleLayers(0x6, 0x6);  // 正交视图：层0x6

        // only preserve the color buffer for additional views; depth and stencil can be discarded.
        // 对于附加视图，只保留颜色缓冲区；深度和模板缓冲区可以丢弃（优化性能）
        window->mDepthView->getView()->setShadowingEnabled(false);
        window->mGodView->getView()->setShadowingEnabled(false);
        window->mOrthoView->getView()->setShadowingEnabled(false);
    }

    // froxel debug grid always added (but hidden)
    // Froxel调试网格始终添加（但默认隐藏）
    mScene->addEntity(cameraGrid->getWireFrameRenderable());

    // 加载污垢纹理和IBL资源
    loadDirt(config);
    loadIBL(config);

    // 为所有非UI视图设置场景（UI视图不需要场景）
    for (auto& view : window->mViews) {
        if (view.get() != window->mUiView) {
            view->getView()->setScene(mScene);
        }
    }

    // 调用用户设置回调（允许用户设置场景、添加实体等）
    setupCallback(mEngine, window->mMainView->getView(), mScene);

    // 如果提供了ImGui回调，初始化ImGui
    if (imguiCallback) {
        // 创建ImGui辅助对象（使用Roboto字体）
        mImGuiHelper = std::make_unique<ImGuiHelper>(mEngine, window->mUiView->getView(),
            getRootAssetsPath() + "assets/fonts/Roboto-Medium.ttf");
        ImGuiIO& io = ImGui::GetIO();
        
        // Windows平台：设置ImGui的主视口平台句柄（用于窗口管理）
        #ifdef WIN32
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            SDL_GetWindowWMInfo(window->getSDLWindow(), &wmInfo);
            ImGui::GetMainViewport()->PlatformHandleRaw = wmInfo.info.win.window;
        #endif
        
        // 设置剪贴板回调函数（使用SDL的剪贴板功能）
        io.SetClipboardTextFn = [](void*, const char* text) {
            SDL_SetClipboardText(text);
        };
        io.GetClipboardTextFn = [](void*) -> const char* {
            return SDL_GetClipboardText();
        };
        io.ClipboardUserData = nullptr;
    }

    bool mousePressed[3] = { false };

    int sidebarWidth = mSidebarWidth;
    float cameraFocalLength = mCameraFocalLength;
    float cameraNear = mCameraNear;
    float cameraFar = mCameraFar;

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    SDL_Window* sdlWindow = window->getSDLWindow();

    while (!mClosed) {
        if (mWindowTitle != SDL_GetWindowTitle(sdlWindow)) {
            SDL_SetWindowTitle(sdlWindow, mWindowTitle.c_str());
        }

        if (mSidebarWidth != sidebarWidth ||
            mCameraFocalLength != cameraFocalLength ||
            mCameraNear != cameraNear ||
            mCameraFar != cameraFar) {
            window->configureCamerasForWindow();
            sidebarWidth = mSidebarWidth;
            cameraFocalLength = mCameraFocalLength;
            cameraNear = mCameraNear;
            cameraFar = mCameraFar;
        }

        if (!UTILS_HAS_THREADING) {
            mEngine->execute();
        }

        // Allow the app to animate the scene if desired.
        if (mAnimation) {
            double now = (double) SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
            mAnimation(mEngine, window->mMainView->getView(), now);
        }

        // Loop over fresh events twice: first stash them and let ImGui process them, then allow
        // the app to process the stashed events. This is done because ImGui might wish to block
        // certain events from the app (e.g., when dragging the mouse over an obscuring window).
        // 事件处理分为两轮：
        // 第一轮：暂存事件并让ImGui处理（ImGui可能会阻止某些事件传递给应用）
        // 第二轮：应用处理暂存的事件
        constexpr int kMaxEvents = 16;
        SDL_Event events[kMaxEvents];
        int nevents = 0;
        // 第一轮：收集事件并让ImGui处理
        while (nevents < kMaxEvents && SDL_PollEvent(&events[nevents]) != 0) {
            if (mImGuiHelper) {
                ImGuiIO& io = ImGui::GetIO();
                SDL_Event* event = &events[nevents];
                switch (event->type) {
                    case SDL_MOUSEWHEEL: {
                        // 处理鼠标滚轮事件（水平和垂直）
                        if (event->wheel.x > 0) io.MouseWheelH += 1;
                        if (event->wheel.x < 0) io.MouseWheelH -= 1;
                        if (event->wheel.y > 0) io.MouseWheel += 1;
                        if (event->wheel.y < 0) io.MouseWheel -= 1;
                        break;
                    }
                    case SDL_MOUSEBUTTONDOWN: {
                        // 记录鼠标按下状态（用于ImGui）
                        if (event->button.button == SDL_BUTTON_LEFT) mousePressed[0] = true;
                        if (event->button.button == SDL_BUTTON_RIGHT) mousePressed[1] = true;
                        if (event->button.button == SDL_BUTTON_MIDDLE) mousePressed[2] = true;
                        break;
                    }
                    case SDL_TEXTINPUT: {
                        // 处理文本输入事件
                        io.AddInputCharactersUTF8(event->text.text);
                        break;
                    }
                    case SDL_KEYUP:
                    case SDL_KEYDOWN: {
                        // 处理键盘事件：转换修饰键和按键
                        SDL_Scancode const scancode = event->key.keysym.scancode;
                        SDL_Keycode const keycode = event->key.keysym.sym;

                        auto modState = SDL_GetModState();
                        // 设置修饰键状态
                        io.AddKeyEvent(ImGuiMod_Ctrl, (modState & KMOD_CTRL) != 0);
                        io.AddKeyEvent(ImGuiMod_Shift, (modState & KMOD_SHIFT) != 0);
                        io.AddKeyEvent(ImGuiMod_Alt, (modState & KMOD_ALT) != 0);
                        io.AddKeyEvent(ImGuiMod_Super, (modState & KMOD_GUI) != 0);
                        // 转换并添加按键事件
                        io.AddKeyEvent(
                                filamentapp_utils::ImGui_ImplSDL2_KeyEventToImGuiKey(keycode, scancode),
                                event->type == SDL_KEYDOWN);
                        break;
                    }
                }
            }
            nevents++;
        }

        // Now, loop over the events a second time for app-side processing.
        // 第二轮：应用处理暂存的事件（检查ImGui是否要捕获事件）
        for (int i = 0; i < nevents; i++) {
            const SDL_Event& event = events[i];
            ImGuiIO* io = mImGuiHelper ? &ImGui::GetIO() : nullptr;
            switch (event.type) {
                case SDL_QUIT:
                    // 窗口关闭事件
                    mClosed = true;
                    break;
                case SDL_KEYDOWN:
                    // 按键按下事件
                    if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        mClosed = true;  // ESC键关闭应用
                    }
#ifndef NDEBUG
                    // 调试模式：PrintScreen键触发帧捕获
                    if (event.key.keysym.scancode == SDL_SCANCODE_PRINTSCREEN) {
                        DebugRegistry& debug = mEngine->getDebugRegistry();
                        bool* captureFrame =
                                debug.getPropertyAddress<bool>("d.renderer.doFrameCapture");
                        *captureFrame = true;
                    }
#endif
                    window->keyDown(event.key.keysym.scancode);
                    break;
                case SDL_KEYUP:
                    // 按键释放事件
                    window->keyUp(event.key.keysym.scancode);
                    break;
                case SDL_MOUSEWHEEL:
                    // 鼠标滚轮事件（检查ImGui是否要捕获）
                    if (!io || !io->WantCaptureMouse)
                        window->mouseWheel(event.wheel.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    // 鼠标按下事件（检查ImGui是否要捕获）
                    if (!io || !io->WantCaptureMouse)
                        window->mouseDown(event.button.button, event.button.x, event.button.y);
                    break;
                case SDL_MOUSEBUTTONUP:
                    // 鼠标释放事件（检查ImGui是否要捕获）
                    if (!io || !io->WantCaptureMouse)
                        window->mouseUp(event.button.x, event.button.y);
                    break;
                case SDL_MOUSEMOTION:
                    // 鼠标移动事件（检查ImGui是否要捕获）
                    if (!io || !io->WantCaptureMouse)
                        window->mouseMoved(event.motion.x, event.motion.y);
                    break;
                case SDL_DROPFILE:
                    // 文件拖放事件
                    if (mDropHandler) {
                        mDropHandler(event.drop.file);
                    }
                    SDL_free(event.drop.file);  // 释放SDL分配的文件路径内存
                    break;
                case SDL_WINDOWEVENT:
                    // 窗口事件
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_RESIZED:
                            // 窗口大小改变：重新配置相机和视口
                            window->resize();
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
        }

        // Calculate the time step.
        // 计算时间步长（用于动画和相机更新）
        static Uint64 frequency = SDL_GetPerformanceFrequency();
        Uint64 now = SDL_GetPerformanceCounter();
        // 计算自上一帧以来的时间（秒），第一帧使用默认值（1/60秒）
        const float timeStep = mTime > 0 ? (float)((double)(now - mTime) / frequency) :
                (float)(1.0f / 60.0f);
        mTime = now;

        // Populate the UI scene, regardless of whether Filament wants to a skip frame. We should
        // always let ImGui generate a command list; if it skips a frame it'll destroy its widgets.
        // 填充UI场景，无论Filament是否要跳过帧。我们应该始终让ImGui生成命令列表；
        // 如果跳过一帧，它会销毁其小部件。
        if (mImGuiHelper) {

            // Inform ImGui of the current window size in case it was resized.
            // 通知ImGui当前窗口大小（如果窗口被调整大小）
            if (config.headless) {
                // 无头模式：直接使用窗口尺寸
                mImGuiHelper->setDisplaySize(window->mWidth, window->mHeight);
            } else {
                // 有窗口模式：考虑DPI缩放
                int windowWidth, windowHeight;
                int displayWidth, displayHeight;
                SDL_GetWindowSize(window->mWindow, &windowWidth, &windowHeight);
                SDL_GL_GetDrawableSize(window->mWindow, &displayWidth, &displayHeight);
                // 设置显示大小和DPI缩放比例
                mImGuiHelper->setDisplaySize(windowWidth, windowHeight,
                        windowWidth > 0 ? ((float)displayWidth / windowWidth) : 0,
                        displayHeight > 0 ? ((float)displayHeight / windowHeight) : 0);
            }

            // Setup mouse inputs (we already got mouse wheel, keyboard keys & characters
            // from our event handler)
            // 设置鼠标输入（我们已经从事件处理器获取了鼠标滚轮、键盘按键和字符）
            ImGuiIO& io = ImGui::GetIO();
            int mx, my;
            Uint32 buttons = SDL_GetMouseState(&mx, &my);
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
            // 设置鼠标按钮状态（结合事件和当前状态）
            io.MouseDown[0] = mousePressed[0] || (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
            io.MouseDown[1] = mousePressed[1] || (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
            io.MouseDown[2] = mousePressed[2] || (buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
            mousePressed[0] = mousePressed[1] = mousePressed[2] = false;  // 清除事件状态

            // TODO: Update to a newer SDL and use SDL_CaptureMouse() to retrieve mouse coordinates
            // outside of the client area; see the imgui SDL example.
            // 如果窗口有输入焦点，设置鼠标位置
            if ((SDL_GetWindowFlags(window->mWindow) & SDL_WINDOW_INPUT_FOCUS) != 0) {
                io.MousePos = ImVec2((float)mx, (float)my);
            }

            // Populate the UI Scene.
            // 填充UI场景（生成ImGui渲染命令）
            mImGuiHelper->render(timeStep, imguiCallback);
        }

        // Update the camera manipulators for each view.
        // 更新每个视图的相机操作器（处理相机移动、旋转等）
        for (auto const& view : window->mViews) {
            auto* cm = view->getCameraManipulator();
            if (cm) {
                cm->update(timeStep);
            }
        }

        // Update the position and orientation of the two cameras.
        // 更新两个相机的位置和朝向
        filament::math::float3 eye, center, up;
        // 更新主相机
        window->mMainCameraMan->getLookAt(&eye, &center, &up);
        window->mMainCamera->lookAt(eye, center, up);

        // 更新调试相机（使用与主相机相同的曝光参数）
        window->mDebugCameraMan->getLookAt(&eye, &center, &up);
        window->mDebugCamera->lookAt(eye, center, up);
        window->mDebugCamera->setExposure(
            window->mMainCamera->getAperture(),
            window->mMainCamera->getShutterSpeed(),
            window->mMainCamera->getSensitivity());

        // 更新正交相机（使用与主相机相同的曝光参数）
        window->mOrthoCamera->setExposure(
            window->mMainCamera->getAperture(),
            window->mMainCamera->getShutterSpeed(),
            window->mMainCamera->getSensitivity());

        // 更新Froxel网格可视化（如果配置改变）
        auto const fci = window->mMainView->getView()->getFroxelConfigurationInfo();
        if (UTILS_UNLIKELY(fci.age != mFroxelInfoAge)) {
            // Froxel配置已改变，更新网格可视化
            mFroxelInfoAge = fci.age;
            auto width = fci.info.width;              // Froxel网格宽度
            auto height = fci.info.height;            // Froxel网格高度
            auto depth = fci.info.depth;              // Froxel网格深度
            auto froxelDimension = fci.info.froxelDimension;  // 每个Froxel的尺寸
            auto viewportWidth = fci.info.viewportWidth;      // 视口宽度
            auto viewportHeight = fci.info.viewportHeight;    // 视口高度
            auto zLightFar = fci.info.zLightFar;              // 光照远平面
            auto linearizer = fci.info.linearizer;            // 线性化因子
            auto p = fci.info.p;                              // 投影矩阵
            auto ct = fci.info.clipTransform;                 // 裁剪变换
            
            // 更新网格：使用自定义坐标生成器将Froxel网格坐标转换为NDC坐标
            cameraGrid->update(width, height, depth,
                // X轴坐标生成器：将Froxel索引转换为NDC X坐标
                [=](int const i) {
                    float x = float(2 * i * froxelDimension.x) / float(viewportWidth ) - 1.0f;
                    x = (x - ct.z) / ct.x;  // 应用裁剪变换
                    return x;
                },
                // Y轴坐标生成器：将Froxel索引转换为NDC Y坐标
                [=](int const j) {
                    float y =  float(2 * j * froxelDimension.y) / float(viewportHeight) - 1.0f;
                    y = (y - ct.w) / ct.y;  // 应用裁剪变换
                    return y;
                },
                // Z轴坐标生成器：将Froxel索引转换为NDC Z坐标
                [=](int const k) {
                    // 计算视图空间Z坐标（使用指数分布）
                    float const z_view = -zLightFar * std::exp2(float(k - depth) * linearizer);
                    // 变换到裁剪空间
                    auto c = p * float4{ 0, 0, z_view, 1 };
                    // 计算NDC Z坐标
                    float const z_clip_dx = k == 0 ? 1.0f : c.z / c.w;
                    float const z_clip_gl = (1 - z_clip_dx) * 2.0f - 1.0f;
                    return z_clip_gl;
            });
        }

        // 更新调试可视化对象的层掩码（控制显示/隐藏）
        auto& rcm = mEngine->getRenderableManager();
        if (config.splitView) {
            // 设置相机视锥体可视化的层掩码
            rcm.setLayerMask(rcm.getInstance(cameraCube->getSolidRenderable()),     0x3, mCameraFrustumEnabled);
            rcm.setLayerMask(rcm.getInstance(cameraCube->getWireFrameRenderable()), 0x3, mCameraFrustumEnabled);
        }
        // 设置Froxel网格可视化的层掩码
        rcm.setLayerMask(rcm.getInstance(cameraGrid->getWireFrameRenderable()), 0x3, mFroxelGridEnabled);

        // Update the cube distortion matrix used for frustum visualization.
        // 更新用于视锥体可视化的立方体变形矩阵
        auto const csm = window->mMainView->getView()->getDirectionalShadowCameras();
        // show/hide the cascades
        // 显示/隐藏级联阴影贴图
        // 首先隐藏所有级联立方体
        for (size_t i = 0 ; i < 4; i++) {
            rcm.setLayerMask(rcm.getInstance(lightmapCubes[i].getSolidRenderable()), 0x3, 0x0);
            rcm.setLayerMask(rcm.getInstance(lightmapCubes[i].getWireFrameRenderable()), 0x3, 0x0);
        }
        // 如果有级联阴影相机，更新对应的立方体
        if (!csm.empty()) {
            for (size_t i = 0, c = csm.size(); i < c; i++) {
                if (csm[i]) {
                    // 将立方体映射到级联阴影相机的视锥体
                    lightmapCubes[i].mapFrustum(*mEngine, csm[i]);
                }
                // 设置层掩码（如果相机存在且启用可视化）
                uint8_t const layer = csm[i] ? mDirectionalShadowFrustumEnabled : 0x0;
                rcm.setLayerMask(rcm.getInstance(lightmapCubes[i].getSolidRenderable()), 0x3, layer);
                rcm.setLayerMask(rcm.getInstance(lightmapCubes[i].getWireFrameRenderable()), 0x3, layer);
            }
        }

        // 更新相机视锥体和Froxel网格可视化
        cameraCube->mapFrustum(*mEngine, window->mMainCamera);
        cameraGrid->mapFrustum(*mEngine, window->mMainCamera);

        // Delay rendering for roughly one monitor refresh interval
        // TODO: Use SDL_GL_SetSwapInterval for proper vsync
        // 延迟渲染大约一个显示器刷新间隔（简单的垂直同步实现）
        // TODO: 使用SDL_GL_SetSwapInterval实现正确的垂直同步
        SDL_DisplayMode Mode;
        int refreshIntervalMS = (SDL_GetDesktopDisplayMode(
            SDL_GetWindowDisplayIndex(window->mWindow), &Mode) == 0 &&
            Mode.refresh_rate != 0) ? round(1000.0 / Mode.refresh_rate) : 16;
        SDL_Delay(refreshIntervalMS);

        Renderer* renderer = window->getRenderer();

        // 调用预渲染回调（在渲染开始前）
        if (preRender) {
            preRender(mEngine, window->mViews[0]->getView(), mScene, renderer);
        }

        // 如果需要重新配置相机，执行配置
        if (mReconfigureCameras) {
            window->configureCamerasForWindow();
            mReconfigureCameras = false;
        }

        // 如果启用分屏视图，设置正交视图的相机（用于显示级联阴影贴图）
        if (config.splitView) {
            if(!window->mOrthoView->getView()->hasCamera()) {
                auto const csm = window->mMainView->getView()->getDirectionalShadowCameras();
                if (!csm.empty()) {
                    // here we could choose the cascade
                    // 这里可以选择要显示的级联（当前使用第0级）
                    Camera const* debugDirectionalShadowCamera = csm[0];
                    if (debugDirectionalShadowCamera) {
                        window->mOrthoView->setCamera(
                                const_cast<Camera*>(debugDirectionalShadowCamera));
                    }
                }
            }
        }

        // ========== 渲染阶段 ==========
        // 开始渲染帧
        if (renderer->beginFrame(window->getSwapChain())) {
            // 渲染离屏视图（如果有）
            for (filament::View* offscreenView: mOffscreenViews) {
                renderer->render(offscreenView);
            }
            // 渲染窗口中的所有视图（主视图、深度视图、上帝视角、正交视图、UI视图）
            for (auto const& view: window->mViews) {
                renderer->render(view->getView());
            }
            // 调用后渲染回调（在渲染完成后）
            if (postRender) {
                postRender(mEngine, window->mViews[0]->getView(), mScene, renderer);
            }
            // 结束渲染帧（提交到GPU）
            renderer->endFrame();
        } else {
            // 如果beginFrame返回false，表示跳过了这一帧（例如交换链未准备好）
            ++mSkippedFrames;
        }
    }

    // ========== 清理资源阶段 ==========
    // 销毁ImGui辅助对象
    if (mImGuiHelper) {
        mImGuiHelper.reset();
    }

    // 调用用户清理回调（允许用户清理自定义资源）
    cleanupCallback(mEngine, window->mMainView->getView(), mScene);

    // 销毁调试可视化对象
    cameraCube.reset();
    cameraGrid.reset();
    lightmapCubes.clear();
    
    // 销毁窗口对象（会自动销毁所有视图、相机、渲染器等）
    window.reset();

    // 销毁IBL对象
    mIBL.reset();
    
    // 销毁材质和材质实例
    mEngine->destroy(mDepthMI);
    mEngine->destroy(mDepthMaterial);
    mEngine->destroy(mDefaultMaterial);
    mEngine->destroy(mTransparentMaterial);
    
    // 销毁场景
    mEngine->destroy(mScene);
    
    // 销毁引擎（这会清理所有Filament资源）
    Engine::destroy(&mEngine);
    mEngine = nullptr;

    // 销毁平台对象（如果使用Vulkan后端）
#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
    if (mVulkanPlatform) {
        filamentapp::destroyVulkanPlatform(mVulkanPlatform);
    }
#endif

    // 销毁平台对象（如果使用WebGPU后端）
#if defined(FILAMENT_SUPPORTS_WEBGPU)
    if (mWebGPUPlatform) {
        delete mWebGPUPlatform;
    }
#endif

}

// RELATIVE_ASSET_PATH is set inside samples/CMakeLists.txt and used to support multi-configuration
// generators, like Visual Studio or Xcode.
#ifndef RELATIVE_ASSET_PATH
#define RELATIVE_ASSET_PATH "."
#endif

/**
 * 获取根资源路径实现
 * 
 * 返回Filament根目录路径，用于加载资源。
 * 路径由可执行文件所在目录和相对资源路径组成。
 * 考虑多配置CMake生成器（如Visual Studio或Xcode）的不同可执行文件路径。
 * 
 * 执行步骤：
 * 1. 获取当前可执行文件的目录
 * 2. 拼接相对资源路径（RELATIVE_ASSET_PATH）
 * 3. 返回静态路径对象（首次调用时初始化）
 * 
 * @return 根资源路径的常量引用
 */
const utils::Path& FilamentApp::getRootAssetsPath() {
    static const utils::Path root = utils::Path::getCurrentExecutable().getParent() + RELATIVE_ASSET_PATH;
    return root;
}

/**
 * 加载IBL资源实现
 * 
 * 执行步骤：
 * 1. 检查路径是否存在
 * 2. 保存旧的IBL对象（延迟释放，因为场景持有引用）
 * 3. 创建新的IBL对象
 * 4. 根据路径类型（文件或目录）选择加载方式
 * 5. 设置天空盒的层掩码
 * 6. 将天空盒和间接光照设置到场景中
 * 
 * @param path IBL资源路径
 */
void FilamentApp::loadIBL(std::string_view path) {
    Path iblPath(path);
    if (!iblPath.exists()) {
        std::cerr << "The specified IBL path does not exist: " << iblPath << std::endl;
        return;
    }

    // Note that IBL holds a skybox, and Scene also holds a reference.  We cannot release IBL's
    // skybox until after new skybox has been set in the scene.
    // 注意：IBL持有天空盒，场景也持有引用。在新天空盒设置到场景之前不能释放IBL的天空盒。
    std::unique_ptr<IBL> oldIBL = std::move(mIBL);
    mIBL = std::make_unique<IBL>(*mEngine);

    // 根据路径类型选择加载方式
    if (!iblPath.isDirectory()) {
        // 单个文件：尝试作为等距圆柱投影图加载
        if (!mIBL->loadFromEquirect(iblPath)) {
            std::cerr << "Could not load the specified IBL: " << iblPath << std::endl;
            mIBL.reset(nullptr);
            return;
        }
    } else {
        // 目录：尝试从目录加载（立方体贴图或KTX格式）
        if (!mIBL->loadFromDirectory(iblPath)) {
            std::cerr << "Could not load the specified IBL: " << iblPath << std::endl;
            mIBL.reset(nullptr);
            return;
        }
    }

    // 如果加载成功，设置到场景中
    if (mIBL != nullptr) {
        mIBL->getSkybox()->setLayerMask(0x7, 0x4);  // 设置层掩码
        mScene->setSkybox(mIBL->getSkybox());
        mScene->setIndirectLight(mIBL->getIndirectLight());
    }
}

/**
 * 从配置加载IBL实现
 * 
 * 执行步骤：
 * 1. 检查配置中是否指定了IBL目录
 * 2. 如果指定了，调用loadIBL(std::string_view)加载
 * 
 * @param config 应用程序配置
 */
void FilamentApp::loadIBL(const Config& config) {
    if (config.iblDirectory.empty()) {
        return;
    }
    loadIBL(config.iblDirectory);
}

/**
 * 加载污垢纹理实现
 * 
 * 执行步骤：
 * 1. 检查配置中是否指定了污垢纹理路径
 * 2. 验证文件是否存在且为文件类型
 * 3. 使用stbi_load加载图像（RGB格式）
 * 4. 创建纹理对象
 * 5. 设置纹理图像数据
 * 
 * @param config 应用程序配置
 */
void FilamentApp::loadDirt(const Config& config) {
    if (!config.dirt.empty()) {
        Path dirtPath(config.dirt);

        if (!dirtPath.exists()) {
            std::cerr << "The specified dirt file does not exist: " << dirtPath << std::endl;
            return;
        }

        if (!dirtPath.isFile()) {
            std::cerr << "The specified dirt path is not a file: " << dirtPath << std::endl;
            return;
        }

        int w, h, n;

        // 加载图像数据（强制加载为RGB格式）
        unsigned char* data = stbi_load(dirtPath.getAbsolutePath().c_str(), &w, &h, &n, 3);
        assert(n == 3);

        // 创建RGB8格式的纹理
        mDirt = Texture::Builder()
                .width(w)
                .height(h)
                .format(Texture::InternalFormat::RGB8)
                .build(*mEngine);

        // 设置纹理图像（使用stbi_image_free作为释放回调）
        mDirt->setImage(*mEngine, 0, { data, size_t(w * h * 3),
                Texture::Format::RGB, Texture::Type::UBYTE,
                (Texture::PixelBufferDescriptor::Callback)&stbi_image_free });
    }
}

/**
 * 初始化SDL库实现
 * 
 * 执行步骤：
 * 1. 初始化SDL事件子系统
 * 2. 检查初始化是否成功
 */
void FilamentApp::initSDL() {
    FILAMENT_CHECK_POSTCONDITION(SDL_Init(SDL_INIT_EVENTS) == 0) << "SDL_Init Failure";
}

/**
 * 设置相机视锥体可视化启用状态实现
 * 
 * @param enabled true启用，false禁用
 */
void FilamentApp::setCameraFrustumEnabled(bool enabled) noexcept {
    mCameraFrustumEnabled = enabled ? 0x2 : 0x0;
}

/**
 * 设置方向光阴影视锥体可视化启用状态实现
 * 
 * @param enabled true启用，false禁用
 */
void FilamentApp::setDirectionalShadowFrustumEnabled(bool enabled) noexcept {
    mDirectionalShadowFrustumEnabled = enabled ? 0x2 : 0x0;
}

/**
 * 设置Froxel网格可视化启用状态实现
 * 
 * @param enabled true启用，false禁用
 */
void FilamentApp::setFroxelGridEnabled(bool enabled) noexcept {
    mFroxelGridEnabled = enabled ? 0x3 : 0x0;
}

/**
 * 检查相机视锥体可视化是否启用实现
 * 
 * @return true已启用，false已禁用
 */
bool FilamentApp::isCameraFrustumEnabled() const noexcept {
    return !!mCameraFrustumEnabled;
}

/**
 * 检查方向光阴影视锥体可视化是否启用实现
 * 
 * @return true已启用，false已禁用
 */
bool FilamentApp::isDirectionalShadowFrustumEnabled() const noexcept {
    return !!mDirectionalShadowFrustumEnabled;
}

/**
 * 检查Froxel网格可视化是否启用实现
 * 
 * @return true已启用，false已禁用
 */
bool FilamentApp::isFroxelGridEnabled() const noexcept {
    return !!mFroxelGridEnabled;
}

// ------------------------------------------------------------------------------------------------

/**
 * Window构造函数实现
 * 
 * 执行步骤：
 * 1. 设置窗口位置（居中）
 * 2. 设置窗口标志（显示、高DPI支持、可调整大小等）
 * 3. 创建SDL窗口（即使是无头模式也需要创建，否则SDL无法轮询事件）
 * 4. 创建Filament引擎（根据配置选择后端和平台）
 * 5. 创建交换链（无头模式或原生窗口）
 * 6. 创建渲染器
 * 7. 创建相机（主相机、调试相机、正交相机）
 * 8. 设置相机曝光
 * 9. 创建视图（主视图、深度视图、上帝视角、正交视图、UI视图）
 * 10. 设置相机操作器
 * 11. 配置相机
 * 12. 设置初始相机位置
 * 
 * @param filamentApp FilamentApp实例指针
 * @param config 应用程序配置
 * @param title 窗口标题
 * @param w 窗口宽度
 * @param h 窗口高度
 */
FilamentApp::Window::Window(FilamentApp* filamentApp,
        const Config& config, std::string title, size_t w, size_t h)
        : mFilamentApp(filamentApp), mConfig(config), mIsHeadless(config.headless) {
    const int x = SDL_WINDOWPOS_CENTERED;
    const int y = SDL_WINDOWPOS_CENTERED;
    //转成2进制 保证每位是否为1 不重复
    uint32_t windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;       // 1. 初始化窗口标志：组合“可见”+“支持高DPI”        
    if (config.resizeable) {          // 2. 如果配置为可调整大小，追加“可调整”标志
        windowFlags |= SDL_WINDOW_RESIZABLE;  // 可调整大小      // 等价于 windowFlags = windowFlags | SDL_WINDOW_RESIZABLE
    }

    if (config.headless) {
        windowFlags |= SDL_WINDOW_HIDDEN;  // 无头模式：隐藏窗口
    }

    // Even if we're in headless mode, we still need to create a window, otherwise SDL will not poll
    // events.
    // 即使是无头模式，我们仍然需要创建窗口，否则SDL将无法轮询事件
    mWindow = SDL_CreateWindow(title.c_str(), x, y, (int) w, (int) h, windowFlags);

    /**
     * Lambda函数：创建Filament引擎
     * 
     * 执行步骤：
     * 1. 根据编译时标志选择默认后端（如果指定了DEFAULT）
     * 2. 配置立体渲染参数
     * 3. 根据后端类型创建平台对象（Vulkan或WebGPU）
     * 4. 构建并返回引擎对象
     */
    auto const createEngine = [&config, this]() {
        auto backend = config.backend;

        // This mirrors the logic for choosing a backend given compile-time flags and client having
        // provided DEFAULT as the backend (see PlatformFactory.cpp)
        // 这反映了根据编译时标志和客户端提供DEFAULT后端时选择后端的逻辑（见PlatformFactory.cpp）
        #if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(FILAMENT_IOS) && \
            !defined(__APPLE__) && defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
            if (backend == Engine::Backend::DEFAULT) {
                backend = Engine::Backend::VULKAN;  // 默认使用Vulkan后端
            }
        #endif

        // 配置引擎：立体渲染参数
        Engine::Config engineConfig = {};
        engineConfig.stereoscopicEyeCount = config.stereoscopicEyeCount;
#if defined(FILAMENT_SAMPLES_STEREO_TYPE_INSTANCED)
        engineConfig.stereoscopicType = Engine::StereoscopicType::INSTANCED;  // 实例化立体渲染
#elif defined (FILAMENT_SAMPLES_STEREO_TYPE_MULTIVIEW)
        engineConfig.stereoscopicType = Engine::StereoscopicType::MULTIVIEW;   // 多视图立体渲染
#else
        engineConfig.stereoscopicType = Engine::StereoscopicType::NONE;         // 无立体渲染
#endif

        // 根据后端类型创建平台对象
        backend::Platform* platform = nullptr;
#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN)
        if (backend == Engine::Backend::VULKAN) {
            platform = mFilamentApp->mVulkanPlatform =
                    filamentapp::createVulkanPlatform(config.vulkanGPUHint.c_str());
        }
#endif

#if defined(FILAMENT_SUPPORTS_WEBGPU)
        if (backend == Engine::Backend::WEBGPU) {
            platform = mFilamentApp->mWebGPUPlatform =
                    new FilamentAppWebGPUPlatform(config.forcedWebGPUBackend);
        }
#endif

        // 构建引擎对象
        return Engine::Builder()
                .backend(backend)
                .featureLevel(config.featureLevel)
                .platform(platform)
                .config(&engineConfig)
                .build();
    };

    // 根据是否为无头模式创建引擎和交换链
    if (config.headless) {
        // 无头模式：创建引擎和交换链（不绑定到窗口）
        mFilamentApp->mEngine = createEngine();
        mSwapChain = mFilamentApp->mEngine->createSwapChain((uint32_t) w, (uint32_t) h);
        mWidth = w;
        mHeight = h;
    } else {
        // 有窗口模式：获取原生窗口句柄
        void* nativeWindow = ::getNativeWindow(mWindow);

        // Create the Engine after the window in case this happens to be a single-threaded platform.
        // For single-threaded platforms, we need to ensure that Filament's OpenGL context is
        // current, rather than the one created by SDL.
        // 在窗口创建后创建引擎，以防这是单线程平台。
        // 对于单线程平台，我们需要确保Filament的OpenGL上下文是当前的，而不是SDL创建的。
        mFilamentApp->mEngine = createEngine();

        // get the resolved backend
        // 获取解析后的后端（可能从DEFAULT变为具体后端）
        mBackend = config.backend = mFilamentApp->mEngine->getBackend();

        void* nativeSwapChain = nativeWindow;

#if defined(__APPLE__)
        // macOS平台：准备原生窗口并设置Metal层
        ::prepareNativeWindow(mWindow);

        void* metalLayer = nullptr;

        if (config.backend == filament::Engine::Backend::METAL || config.backend == filament::Engine::Backend::VULKAN
            || config.backend == filament::Engine::Backend::WEBGPU) {
            metalLayer = setUpMetalLayer(nativeWindow);
            // The swap chain on both native Metal and MoltenVK is a CAMetalLayer.
            // 原生Metal和MoltenVK的交换链都是CAMetalLayer
            nativeSwapChain = metalLayer;
        }

#endif

        // Write back the active feature level.
        // 写回实际的功能级别（可能被引擎调整）
        config.featureLevel = mFilamentApp->mEngine->getActiveFeatureLevel();

        // 创建交换链（绑定到原生窗口，包含模板缓冲区）
        mSwapChain = mFilamentApp->mEngine->createSwapChain(nativeSwapChain,
                filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
    }

    // 创建渲染器
    mRenderer = mFilamentApp->mEngine->createRenderer();

    // create cameras
    // 创建相机：主相机、调试相机、正交相机
    utils::EntityManager& em = utils::EntityManager::get();
    em.create(3, mCameraEntities);
    mCameras[0] = mMainCamera = mFilamentApp->mEngine->createCamera(mCameraEntities[0]);
    mCameras[1] = mDebugCamera = mFilamentApp->mEngine->createCamera(mCameraEntities[1]);
    mCameras[2] = mOrthoCamera = mFilamentApp->mEngine->createCamera(mCameraEntities[2]);

    // set exposure
    // 设置曝光参数（光圈f/16，快门1/125秒，ISO 100）
    for (auto camera : mCameras) {
        camera->setExposure(16.0f, 1 / 125.0f, 100.0f);
    }

    // create views
    // 创建视图：主视图、深度视图、上帝视角、正交视图、UI视图
    mViews.emplace_back(mMainView = new CView(*mRenderer, "Main View"));
    if (config.splitView) {
        mViews.emplace_back(mDepthView = new CView(*mRenderer, "Depth View"));
        mViews.emplace_back(mGodView = new GodView(*mRenderer, "God View"));
        mViews.emplace_back(mOrthoView = new CView(*mRenderer, "Shadow View"));
    }
    mViews.emplace_back(mUiView = new CView(*mRenderer, "UI View"));

    // set-up the camera manipulators
    // 设置相机操作器：主相机和调试相机各一个
    mMainCameraMan = CameraManipulator::Builder()
            .targetPosition(0, 0, -4)      // 目标位置
            .flightMoveDamping(15.0)        // 飞行移动阻尼
            .build(config.cameraMode);
    mDebugCameraMan = CameraManipulator::Builder()
            .targetPosition(0, 0, -4)
            .flightMoveDamping(15.0)
            .build(config.cameraMode);

    // 设置主视图的相机和操作器
    mMainView->setCamera(mMainCamera);
    mMainView->setCameraManipulator(mMainCameraMan);
    if (config.splitView) {
        // Depth view always uses the main camera
        // 深度视图始终使用主相机
        mDepthView->setCamera(mMainCamera);
        mDepthView->setCameraManipulator(mMainCameraMan);

        // The god view uses the main camera for culling, but the debug camera for viewing
        // 上帝视角使用主相机进行剔除，但使用调试相机进行观察
        mGodView->setCamera(mMainCamera);
        mGodView->setGodCamera(mDebugCamera);
        mGodView->setCameraManipulator(mDebugCameraMan);
    }

    // configure the cameras
    // 配置相机（设置投影矩阵和视口）
    configureCamerasForWindow();

    // 设置初始相机位置和朝向
    mMainCamera->lookAt({4, 0, -4}, {0, 0, -4}, {0, 1, 0});
}

/**
 * Window析构函数实现
 * 
 * 执行步骤：
 * 1. 清除所有视图（自动销毁视图对象）
 * 2. 销毁相机组件和实体
 * 3. 销毁渲染器
 * 4. 销毁交换链
 * 5. 销毁SDL窗口
 * 6. 删除相机操作器
 */
FilamentApp::Window::~Window() {
    mViews.clear();  // 清除所有视图（unique_ptr自动销毁）
    utils::EntityManager& em = utils::EntityManager::get();
    // 销毁相机组件和实体
    for (auto e : mCameraEntities) {
        mFilamentApp->mEngine->destroyCameraComponent(e);
        em.destroy(e);
    }
    // 销毁渲染器和交换链
    mFilamentApp->mEngine->destroy(mRenderer);
    mFilamentApp->mEngine->destroy(mSwapChain);
    // 销毁SDL窗口
    SDL_DestroyWindow(mWindow);
    // 删除相机操作器
    delete mMainCameraMan;
    delete mDebugCameraMan;
}

/**
 * 处理鼠标按下事件实现
 * 
 * 执行步骤：
 * 1. 修正高DPI坐标
 * 2. 转换Y坐标（SDL使用左上角为原点，Filament使用左下角）
 * 3. 查找包含该点的视图
 * 4. 设置鼠标事件目标并转发事件
 * 
 * @param button 鼠标按钮（1=左键，2=中键，3=右键）
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::Window::mouseDown(int button, ssize_t x, ssize_t y) {
    fixupMouseCoordinatesForHdpi(x, y);
    y = mHeight - y;  // 转换Y坐标（SDL左上角原点 -> Filament左下角原点）
    for (auto const& view : mViews) {
        if (view->intersects(x, y)) {
            mMouseEventTarget = view.get();
            view->mouseDown(button, x, y);
            break;
        }
    }
}

/**
 * 处理鼠标滚轮事件实现
 * 
 * 执行步骤：
 * 1. 如果有活动的鼠标事件目标，直接转发
 * 2. 否则查找包含上次鼠标位置的视图并转发
 * 
 * @param x 滚轮滚动量
 */
void FilamentApp::Window::mouseWheel(ssize_t x) {
    if (mMouseEventTarget) {
        mMouseEventTarget->mouseWheel(x);
    } else {
        for (auto const& view : mViews) {
            if (view->intersects(mLastX, mLastY)) {
                view->mouseWheel(x);
                break;
            }
        }
    }
}

/**
 * 处理鼠标释放事件实现
 * 
 * 执行步骤：
 * 1. 修正高DPI坐标
 * 2. 如果有活动的鼠标事件目标，转发事件并清除目标
 * 
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::Window::mouseUp(ssize_t x, ssize_t y) {
    fixupMouseCoordinatesForHdpi(x, y);
    if (mMouseEventTarget) {
        y = mHeight - y;  // 转换Y坐标
        mMouseEventTarget->mouseUp(x, y);
        mMouseEventTarget = nullptr;  // 清除鼠标事件目标
    }
}

/**
 * 处理鼠标移动事件实现
 * 
 * 执行步骤：
 * 1. 修正高DPI坐标
 * 2. 转换Y坐标
 * 3. 如果有活动的鼠标事件目标，转发事件
 * 4. 更新上次鼠标位置
 * 
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::Window::mouseMoved(ssize_t x, ssize_t y) {
    fixupMouseCoordinatesForHdpi(x, y);
    y = mHeight - y;  // 转换Y坐标
    if (mMouseEventTarget) {
        mMouseEventTarget->mouseMoved(x, y);
    }
    mLastX = x;  // 更新上次鼠标位置
    mLastY = y;
}

/**
 * 处理按键按下事件实现
 * 
 * 执行步骤：
 * 1. 检查该键是否已经按下（防止按键重复）
 * 2. 确定接收该键keyUp事件的视图：
 *    - 如果有活动的鼠标事件目标，使用该视图
 *    - 否则使用包含当前鼠标位置的视图
 * 3. 转发keyDown事件到目标视图
 * 4. 记录目标视图（用于后续的keyUp事件）
 * 
 * @param key SDL键盘扫描码
 */
void FilamentApp::Window::keyDown(SDL_Scancode key) {
    auto& eventTarget = mKeyEventTarget[key];

    // keyDown events can be sent multiple times per key (for key repeat)
    // If this key is already down, do nothing.
    // keyDown事件可能被多次发送（按键重复），如果该键已经按下，则不处理
    if (eventTarget) {
        return;
    }

    // Decide which view will get this key's corresponding keyUp event.
    // If we're currently in a mouse grap session, it should be the mouse grab's target view.
    // Otherwise, it should be whichever view we're currently hovering over.
    // 决定哪个视图将接收该键对应的keyUp事件。
    // 如果当前有鼠标抓取会话，应该是鼠标抓取的目标视图。
    // 否则，应该是当前鼠标悬停的视图。
    CView* targetView = nullptr;
    if (mMouseEventTarget) {
        targetView = mMouseEventTarget;
    } else {
        for (auto const& view : mViews) {
            if (view->intersects(mLastX, mLastY)) {
                targetView = view.get();
                break;
            }
        }
    }

    if (targetView) {
        targetView->keyDown(key);
        eventTarget = targetView;  // 记录目标视图
    }
}

/**
 * 处理按键释放事件实现
 * 
 * 执行步骤：
 * 1. 检查是否有记录的目标视图
 * 2. 转发keyUp事件到目标视图
 * 3. 清除目标视图记录
 * 
 * @param key SDL键盘扫描码
 */
void FilamentApp::Window::keyUp(SDL_Scancode key) {
    auto& eventTarget = mKeyEventTarget[key];
    if (!eventTarget) {
        return;
    }
    eventTarget->keyUp(key);
    eventTarget = nullptr;  // 清除目标视图记录
}

/**
 * 修正高DPI显示器的鼠标坐标实现
 * 
 * 执行步骤：
 * 1. 获取窗口的可绘制区域大小（物理像素）
 * 2. 获取窗口的逻辑大小（点）
 * 3. 根据DPI缩放比例修正坐标
 * 
 * 在高DPI显示器上，窗口的逻辑大小和物理像素大小不同，
 * 需要将鼠标坐标从逻辑坐标转换为物理坐标。
 * 
 * @param x X坐标的引用（会被修改）
 * @param y Y坐标的引用（会被修改）
 */
void FilamentApp::Window::fixupMouseCoordinatesForHdpi(ssize_t& x, ssize_t& y) const {
    int dw, dh, ww, wh;
    SDL_GL_GetDrawableSize(mWindow, &dw, &dh);  // 获取可绘制区域大小（物理像素）
    SDL_GetWindowSize(mWindow, &ww, &wh);        // 获取窗口大小（逻辑点）
    // 根据DPI缩放比例修正坐标
    x = x * dw / ww;
    y = y * dh / wh;
}

/**
 * 处理窗口大小改变事件实现
 * 
 * 执行步骤：
 * 1. 获取原生窗口句柄
 * 2. 如果是macOS平台且使用Metal/Vulkan/WebGPU后端，调整Metal层大小
 * 3. 重新配置相机（更新投影矩阵和视口）
 * 4. 调用用户提供的resize回调（必须在configureCamerasForWindow之后调用）
 */
void FilamentApp::Window::resize() {
    void* nativeWindow = ::getNativeWindow(mWindow);

#if defined(__APPLE__)
    // macOS平台：如果使用Metal后端，需要调整CAMetalLayer大小
    if (mBackend == filament::Engine::Backend::METAL) {
        resizeMetalLayer(nativeWindow);
    }

#if defined(FILAMENT_DRIVER_SUPPORTS_VULKAN) || defined(FILAMENT_SUPPORTS_WEBGPU)
    // macOS平台：如果使用Vulkan或WebGPU后端（通过MoltenVK），也需要调整Metal层大小
    if (mBackend == filament::Engine::Backend::VULKAN || mBackend == filament::Engine::Backend::WEBGPU) {
        resizeMetalLayer(nativeWindow);
    }
#endif

#endif

    // 重新配置相机（更新投影矩阵和视口）
    configureCamerasForWindow();

    // Call the resize callback, if this FilamentApp has one. This must be done after
    // configureCamerasForWindow, so the viewports are correct.
    // 调用resize回调（如果存在）。必须在configureCamerasForWindow之后调用，确保视口正确。
    if (mFilamentApp->mResize) {
        mFilamentApp->mResize(mFilamentApp->mEngine, mMainView->getView());
    }
}

/**
 * 为窗口配置相机实现
 * 
 * 执行步骤：
 * 1. 获取窗口物理尺寸和DPI缩放比例
 * 2. 计算主视图宽度（考虑侧边栏）
 * 3. 计算宽高比
 * 4. 配置主相机投影（支持立体渲染和普通渲染）
 * 5. 配置调试相机投影（固定45度FOV）
 * 6. 根据是否分屏视图设置各视图的视口
 * 
 * 注意：此函数在窗口大小改变、相机参数改变时调用
 */
void FilamentApp::Window::configureCamerasForWindow() {
    float dpiScaleX = 1.0f;
    float dpiScaleY = 1.0f;

    // If the app is not headless, query the window for its physical & virtual sizes.
    // 如果应用不是无头模式，查询窗口的物理和虚拟尺寸
    if (!mIsHeadless) {
        uint32_t width, height;
        SDL_GL_GetDrawableSize(mWindow, (int*) &width, (int*) &height);
        mWidth = (size_t) width;
        mHeight = (size_t) height;

        int virtualWidth, virtualHeight;
        SDL_GetWindowSize(mWindow, &virtualWidth, &virtualHeight);
        // 计算DPI缩放比例
        dpiScaleX = (float) width / virtualWidth;
        dpiScaleY = (float) height / virtualHeight;
    }

    const uint32_t width = mWidth;
    const uint32_t height = mHeight;

    const float3 at(0, 0, -4);
    const double ratio = double(height) / double(width);
    const int sidebar = mFilamentApp->mSidebarWidth * dpiScaleX;  // 侧边栏宽度（考虑DPI缩放）

    const bool splitview = mViews.size() > 2;  // 是否分屏视图

    const uint32_t mainWidth = std::max(2, (int) width - sidebar);  // 主视图宽度

    double near = mFilamentApp->mCameraNear;
    double far = mFilamentApp->mCameraFar;
    auto aspectRatio = double(mainWidth) / height;
    
    // 配置主相机投影
    if (mMainView->getView()->getStereoscopicOptions().enabled) {
        // 立体渲染模式：支持4个眼睛（模拟中心凹渲染）
        const int ec = mConfig.stereoscopicEyeCount;
        aspectRatio = double(mainWidth) / ec / height;

        mat4 projections[4];
        projections[0] = Camera::projection(mFilamentApp->mCameraFocalLength, aspectRatio, near, far);
        projections[1] = projections[0];
        // simulate foveated rendering
        // 模拟中心凹渲染：外围眼睛使用2倍焦距
        projections[2] = Camera::projection(mFilamentApp->mCameraFocalLength * 2.0, aspectRatio, near, far);
        projections[3] = projections[2];
        mMainCamera->setCustomEyeProjection(projections, 4, projections[0], near, far);
    } else {
        // 普通渲染模式：使用镜头投影
        mMainCamera->setLensProjection(mFilamentApp->mCameraFocalLength, aspectRatio, near, far);
    }
    
    // 配置调试相机投影（固定45度FOV）
    mDebugCamera->setProjection(45.0, aspectRatio, 0.0625, 4096, Camera::Fov::VERTICAL);

    // We're in split view when there are more views than just the Main and UI views.
    // 当视图数量超过主视图和UI视图时，处于分屏视图模式
    if (splitview) {
        // 分屏视图：将主视图区域分成2x2网格
        uint32_t const vpw = mainWidth / 2;  // 每个视图的宽度
        uint32_t const vph = height / 2;     // 每个视图的高度
        mMainView->setViewport ({ sidebar +            0,            0, vpw, vph });   // 左上：主视图
        mDepthView->setViewport({ sidebar + int32_t(vpw),            0, vpw, vph });   // 右上：深度视图
        mGodView->setViewport  ({ sidebar + int32_t(vpw), int32_t(vph), vpw, vph });  // 右下：上帝视角
        mOrthoView->setViewport({ sidebar +            0, int32_t(vph), vpw, vph });   // 左下：正交视图（阴影）
    } else {
        // 单视图模式：主视图占据除侧边栏外的全部区域
        mMainView->setViewport({ sidebar, 0, mainWidth, height });
    }
    // UI视图始终占据整个窗口
    mUiView->setViewport({ 0, 0, width, height });
}

// ------------------------------------------------------------------------------------------------

/**
 * CView构造函数实现
 * 
 * 执行步骤：
 * 1. 保存引擎引用
 * 2. 创建Filament视图对象
 * 3. 设置视图名称
 * 
 * @param renderer 渲染器引用
 * @param name 视图名称
 */
FilamentApp::CView::CView(Renderer& renderer, std::string name)
        : engine(*renderer.getEngine()), mName(name) {
    view = engine.createView();
    view->setName(name.c_str());
}

/**
 * CView析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁Filament视图对象
 */
FilamentApp::CView::~CView() {
    engine.destroy(view);
}

/**
 * 设置视口实现
 * 
 * 执行步骤：
 * 1. 保存视口参数
 * 2. 设置Filament视图的视口
 * 3. 如果存在相机操作器，更新其视口
 * 
 * @param viewport 视口参数
 */
void FilamentApp::CView::setViewport(filament::Viewport const& viewport) {
    mViewport = viewport;
    view->setViewport(viewport);
    if (mCameraManipulator) {
        mCameraManipulator->setViewport(viewport.width, viewport.height);
    }
}

/**
 * 处理鼠标按下事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，开始抓取（button == 3表示右键）
 * 
 * @param button 鼠标按钮
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::CView::mouseDown(int button, ssize_t x, ssize_t y) {
    if (mCameraManipulator) {
        mCameraManipulator->grabBegin(x, y, button == 3);  // button == 3表示右键
    }
}

/**
 * 处理鼠标释放事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，结束抓取
 * 
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::CView::mouseUp(ssize_t x, ssize_t y) {
    if (mCameraManipulator) {
        mCameraManipulator->grabEnd();
    }
}

/**
 * 处理鼠标移动事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，更新抓取位置
 * 
 * @param x X坐标
 * @param y Y坐标
 */
void FilamentApp::CView::mouseMoved(ssize_t x, ssize_t y) {
    if (mCameraManipulator) {
        mCameraManipulator->grabUpdate(x, y);
    }
}

/**
 * 处理鼠标滚轮事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，执行滚动操作
 * 
 * @param x 滚轮滚动量
 */
void FilamentApp::CView::mouseWheel(ssize_t x) {
    if (mCameraManipulator) {
        mCameraManipulator->scroll(0, 0, x);
    }
}

/**
 * 将SDL键盘扫描码转换为相机操作器按键实现
 * 
 * 支持的按键映射：
 * - W -> FORWARD（前进）
 * - A -> LEFT（左移）
 * - S -> BACKWARD（后退）
 * - D -> RIGHT（右移）
 * - E -> UP（上移）
 * - Q -> DOWN（下移）
 * 
 * @param scancode SDL键盘扫描码
 * @param key 输出的相机操作器按键引用
 * @return 转换是否成功
 */
bool FilamentApp::manipulatorKeyFromKeycode(SDL_Scancode scancode, CameraManipulator::Key& key) {
    switch (scancode) {
        case SDL_SCANCODE_W:
            key = CameraManipulator::Key::FORWARD;
            return true;
        case SDL_SCANCODE_A:
            key = CameraManipulator::Key::LEFT;
            return true;
        case SDL_SCANCODE_S:
            key = CameraManipulator::Key::BACKWARD;
            return true;
        case SDL_SCANCODE_D:
            key = CameraManipulator::Key::RIGHT;
            return true;
        case SDL_SCANCODE_E:
            key = CameraManipulator::Key::UP;
            return true;
        case SDL_SCANCODE_Q:
            key = CameraManipulator::Key::DOWN;
            return true;
        default:
            return false;
    }
}

/**
 * 处理按键释放事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，转换扫描码为操作器按键
 * 2. 如果转换成功，调用操作器的keyUp方法
 * 
 * @param scancode SDL键盘扫描码
 */
void FilamentApp::CView::keyUp(SDL_Scancode scancode) {
    if (mCameraManipulator) {
        CameraManipulator::Key key;
        if (manipulatorKeyFromKeycode(scancode, key)) {
            mCameraManipulator->keyUp(key);
        }
    }
}

/**
 * 处理按键按下事件实现
 * 
 * 执行步骤：
 * 1. 如果存在相机操作器，转换扫描码为操作器按键
 * 2. 如果转换成功，调用操作器的keyDown方法
 * 
 * @param scancode SDL键盘扫描码
 */
void FilamentApp::CView::keyDown(SDL_Scancode scancode) {
    if (mCameraManipulator) {
        CameraManipulator::Key key;
        if (manipulatorKeyFromKeycode(scancode, key)) {
            mCameraManipulator->keyDown(key);
        }
    }
}

/**
 * 检查点是否在视口内实现
 * 
 * 执行步骤：
 * 1. 检查X坐标是否在视口范围内
 * 2. 检查Y坐标是否在视口范围内
 * 3. 返回是否都在范围内
 * 
 * @param x X坐标
 * @param y Y坐标
 * @return true在视口内，false不在
 */
bool FilamentApp::CView::intersects(ssize_t x, ssize_t y) {
    if (x >= mViewport.left && x < mViewport.left + mViewport.width)
        if (y >= mViewport.bottom && y < mViewport.bottom + mViewport.height)
            return true;

    return false;
}

/**
 * 设置相机操作器实现
 * 
 * @param cm 相机操作器指针
 */
void FilamentApp::CView::setCameraManipulator(CameraManipulator* cm) {
    mCameraManipulator = cm;
}

/**
 * 设置相机实现
 * 
 * @param camera 相机指针
 */
void FilamentApp::CView::setCamera(Camera* camera) {
    view->setCamera(camera);
}

/**
 * 设置上帝视角相机实现
 * 
 * 用于调试视图，允许使用独立的相机观察场景。
 * 
 * @param camera 上帝视角相机指针
 */
void FilamentApp::GodView::setGodCamera(Camera* camera) {
    getView()->setDebugCamera(camera);
}
