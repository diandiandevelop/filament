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

#include "details/Engine.h"

#include "ResourceAllocator.h"

#include "details/BufferObject.h"
#include "details/Camera.h"
#include "details/Fence.h"
#include "details/IndexBuffer.h"
#include "details/IndirectLight.h"
#include "details/Material.h"
#include "details/Renderer.h"
#include "details/Scene.h"
#include "details/SkinningBuffer.h"
#include "details/Skybox.h"
#include "details/Stream.h"
#include "details/SwapChain.h"
#include "details/Sync.h"
#include "details/Texture.h"
#include "details/VertexBuffer.h"
#include "details/View.h"

#include <filament/Engine.h>

#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/Panic.h>
#include <utils/Slice.h>

#include <chrono>

#include <stddef.h>
#include <stdint.h>

using namespace utils;

namespace filament {

namespace backend {
class Platform;
}

using namespace math;
using namespace backend;

/**
 * 销毁引擎（单指针版本）
 * 
 * 销毁引擎实例并释放所有相关资源。
 * 
 * @param engine 要销毁的引擎指针
 * 
 * 实现：将调用转发到内部实现类（FEngine）进行实际销毁
 */
void Engine::destroy(Engine* engine) {
    FEngine::destroy(downcast(engine));
}

#if UTILS_HAS_THREADING
/**
 * 从线程令牌获取引擎实例
 * 
 * 在多线程环境中，每个线程可以通过令牌获取其关联的引擎实例。
 * 这用于线程本地存储（TLS）。
 * 
 * @param token 线程令牌（由引擎创建时返回）
 * @return 引擎实例指针，如果令牌无效则返回 nullptr
 * 
 * 实现：从线程本地存储中获取引擎实例
 */
Engine* Engine::getEngine(void* token) {
    return FEngine::getEngine(token);
}
#endif

/**
 * 获取驱动接口
 * 
 * 返回底层图形驱动接口的常量指针。
 * 驱动接口用于直接访问底层图形 API（OpenGL、Vulkan 等）。
 * 
 * @return 驱动接口常量指针
 * 
 * 实现：从内部实现类获取驱动接口的地址
 */
Driver const* Engine::getDriver() const noexcept {
    return std::addressof(downcast(this)->getDriver());
}

/**
 * 销毁引擎（双指针版本）
 * 
 * 销毁引擎实例并将指针设置为 nullptr。
 * 这是一个便捷方法，用于避免悬空指针。
 * 
 * @param pEngine 指向引擎指针的指针
 *                - 如果为 nullptr，则不执行任何操作
 *                - 如果非空，销毁引擎并将指针设置为 nullptr
 * 
 * 实现：
 * 1. 检查指针是否有效
 * 2. 如果有效，销毁引擎
 * 3. 将指针设置为 nullptr 以避免悬空指针
 */
void Engine::destroy(Engine** pEngine) {
    if (pEngine) {
        Engine* engine = *pEngine;  // 保存引擎指针
        FEngine::destroy(downcast(engine));  // 销毁引擎
        *pEngine = nullptr;  // 清空指针，避免悬空指针
    }
}

// -----------------------------------------------------------------------------------------------
// Resource management
// -----------------------------------------------------------------------------------------------

/**
 * 获取默认材质
 * 
 * 返回引擎的默认材质。默认材质用于渲染没有指定材质的对象。
 * 
 * @return 默认材质常量指针
 * 
 * 实现：从内部实现类获取默认材质
 */
const Material* Engine::getDefaultMaterial() const noexcept {
    return downcast(this)->getDefaultMaterial();
}

/**
 * 获取后端类型
 * 
 * 返回引擎使用的图形后端类型（OpenGL、Vulkan、Metal 等）。
 * 
 * @return 后端类型枚举值
 * 
 * 实现：从内部实现类获取后端类型
 */
Backend Engine::getBackend() const noexcept {
    return downcast(this)->getBackend();
}

/**
 * 获取平台接口
 * 
 * 返回底层平台接口指针。平台接口用于访问平台特定功能。
 * 
 * @return 平台接口指针
 * 
 * 实现：从内部实现类获取平台接口
 */
Platform* Engine::getPlatform() const noexcept {
    return downcast(this)->getPlatform();
}

/**
 * 创建渲染器
 * 
 * 创建一个新的渲染器实例。渲染器用于执行实际的渲染操作。
 * 
 * @return 渲染器指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建渲染器
 */
Renderer* Engine::createRenderer() noexcept {
    return downcast(this)->createRenderer();
}

/**
 * 创建视图
 * 
 * 创建一个新的视图实例。视图定义了渲染的场景、相机和渲染选项。
 * 
 * @return 视图指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建视图
 */
View* Engine::createView() noexcept {
    return downcast(this)->createView();
}

/**
 * 创建场景
 * 
 * 创建一个新的场景实例。场景包含要渲染的实体（渲染对象、光源等）。
 * 
 * @return 场景指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建场景
 */
Scene* Engine::createScene() noexcept {
    return downcast(this)->createScene();
}

/**
 * 创建相机
 * 
 * 为指定的实体创建一个相机组件。
 * 
 * @param entity 实体ID
 * @return 相机指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建相机组件
 */
Camera* Engine::createCamera(Entity const entity) noexcept {
    return downcast(this)->createCamera(entity);
}

/**
 * 获取相机组件
 * 
 * 获取指定实体的相机组件。
 * 
 * @param entity 实体ID
 * @return 相机指针，如果实体没有相机组件则返回 nullptr
 * 
 * 实现：从内部实现类获取相机组件
 */
Camera* Engine::getCameraComponent(Entity const entity) noexcept {
    return downcast(this)->getCameraComponent(entity);
}

/**
 * 销毁相机组件
 * 
 * 销毁指定实体的相机组件。
 * 
 * @param entity 实体ID
 * 
 * 实现：将调用转发到内部实现类销毁相机组件
 */
void Engine::destroyCameraComponent(Entity const entity) noexcept {
    downcast(this)->destroyCameraComponent(entity);
}

/**
 * 创建围栏
 * 
 * 创建一个新的围栏对象。围栏用于同步 GPU 和 CPU 之间的操作。
 * 
 * @return 围栏指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建围栏
 */
Fence* Engine::createFence() noexcept {
    return downcast(this)->createFence();
}

/**
 * 创建交换链（从原生窗口）
 * 
 * 从原生窗口创建一个新的交换链。交换链用于呈现渲染结果到屏幕。
 * 
 * @param nativeWindow 原生窗口句柄
 *                     - 平台特定类型（例如：Android 的 ANativeWindow*）
 * @param flags 交换链标志
 *              - CONFIG_TRANSPARENT: 透明背景
 *              - CONFIG_READABLE: 可读（用于读取像素）
 *              - CONFIG_SRGB_COLORSPACE: sRGB 色彩空间
 * @return 交换链指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建交换链
 */
SwapChain* Engine::createSwapChain(void* nativeWindow, uint64_t const flags) noexcept {
    return downcast(this)->createSwapChain(nativeWindow, flags);
}

/**
 * 创建交换链（指定尺寸）
 * 
 * 创建一个指定尺寸的交换链。这用于离屏渲染。
 * 
 * @param width 宽度（像素）
 * @param height 高度（像素）
 * @param flags 交换链标志
 *              - CONFIG_TRANSPARENT: 透明背景
 *              - CONFIG_READABLE: 可读（用于读取像素）
 *              - CONFIG_SRGB_COLORSPACE: sRGB 色彩空间
 * @return 交换链指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建交换链
 */
SwapChain* Engine::createSwapChain(uint32_t const width, uint32_t const height, uint64_t const flags) noexcept {
    return downcast(this)->createSwapChain(width, height, flags);
}

/**
 * 创建同步对象
 * 
 * 创建一个新的同步对象。同步对象用于同步 GPU 命令的执行。
 * 
 * @return 同步对象指针，如果创建失败则返回 nullptr
 * 
 * 实现：调用内部实现类创建同步对象
 */
Sync* Engine::createSync() noexcept {
    return downcast(this)->createSync();
}

/**
 * 销毁缓冲区对象
 * 
 * 销毁指定的缓冲区对象并释放相关资源。
 * 
 * @param p 缓冲区对象指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁缓冲区对象
 */
bool Engine::destroy(const BufferObject* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁顶点缓冲区
 * 
 * 销毁指定的顶点缓冲区并释放相关资源。
 * 
 * @param p 顶点缓冲区指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁顶点缓冲区
 */
bool Engine::destroy(const VertexBuffer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁索引缓冲区
 * 
 * 销毁指定的索引缓冲区并释放相关资源。
 * 
 * @param p 索引缓冲区指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁索引缓冲区
 */
bool Engine::destroy(const IndexBuffer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁蒙皮缓冲区
 * 
 * 销毁指定的蒙皮缓冲区并释放相关资源。
 * 
 * @param p 蒙皮缓冲区指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁蒙皮缓冲区
 */
bool Engine::destroy(const SkinningBuffer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁变形目标缓冲区
 * 
 * 销毁指定的变形目标缓冲区并释放相关资源。
 * 
 * @param p 变形目标缓冲区指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁变形目标缓冲区
 */
bool Engine::destroy(const MorphTargetBuffer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁间接光
 * 
 * 销毁指定的间接光对象并释放相关资源。
 * 
 * @param p 间接光指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁间接光
 */
bool Engine::destroy(const IndirectLight* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁材质
 * 
 * 销毁指定的材质对象并释放相关资源。
 * 
 * @param p 材质指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁材质
 */
bool Engine::destroy(const Material* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁材质实例
 * 
 * 销毁指定的材质实例对象并释放相关资源。
 * 
 * @param p 材质实例指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁材质实例
 */
bool Engine::destroy(const MaterialInstance* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁渲染器
 * 
 * 销毁指定的渲染器对象并释放相关资源。
 * 
 * @param p 渲染器指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁渲染器
 */
bool Engine::destroy(const Renderer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁视图
 * 
 * 销毁指定的视图对象并释放相关资源。
 * 
 * @param p 视图指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁视图
 */
bool Engine::destroy(const View* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁场景
 * 
 * 销毁指定的场景对象并释放相关资源。
 * 
 * @param p 场景指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁场景
 */
bool Engine::destroy(const Scene* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁天空盒
 * 
 * 销毁指定的天空盒对象并释放相关资源。
 * 
 * @param p 天空盒指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁天空盒
 */
bool Engine::destroy(const Skybox* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁颜色分级
 * 
 * 销毁指定的颜色分级对象并释放相关资源。
 * 
 * @param p 颜色分级指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁颜色分级
 */
bool Engine::destroy(const ColorGrading* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁流
 * 
 * 销毁指定的流对象并释放相关资源。
 * 
 * @param p 流指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁流
 */
bool Engine::destroy(const Stream* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁纹理
 * 
 * 销毁指定的纹理对象并释放相关资源。
 * 
 * @param p 纹理指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁纹理
 */
bool Engine::destroy(const Texture* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁渲染目标
 * 
 * 销毁指定的渲染目标对象并释放相关资源。
 * 
 * @param p 渲染目标指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁渲染目标
 */
bool Engine::destroy(const RenderTarget* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁围栏
 * 
 * 销毁指定的围栏对象并释放相关资源。
 * 
 * @param p 围栏指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁围栏
 */
bool Engine::destroy(const Fence* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁交换链
 * 
 * 销毁指定的交换链对象并释放相关资源。
 * 
 * @param p 交换链指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁交换链
 */
bool Engine::destroy(const SwapChain* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁同步对象
 * 
 * 销毁指定的同步对象并释放相关资源。
 * 
 * @param p 同步对象指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁同步对象
 */
bool Engine::destroy(const Sync* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁实例缓冲区
 * 
 * 销毁指定的实例缓冲区对象并释放相关资源。
 * 
 * @param p 实例缓冲区指针
 * @return true 如果成功销毁，false 如果对象无效或已被销毁
 * 
 * 实现：将调用转发到内部实现类销毁实例缓冲区
 */
bool Engine::destroy(const InstanceBuffer* p) {
    return downcast(this)->destroy(downcast(p));
}

/**
 * 销毁实体
 * 
 * 销毁指定的实体及其所有组件。
 * 
 * @param e 实体ID
 * 
 * 实现：将调用转发到内部实现类销毁实体
 */
void Engine::destroy(Entity const e) {
    downcast(this)->destroy(e);
}

/**
 * 检查缓冲区对象是否有效
 * 
 * 检查指定的缓冲区对象是否仍然有效（未被销毁）。
 * 
 * @param p 缓冲区对象指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const BufferObject* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查顶点缓冲区是否有效
 * 
 * 检查指定的顶点缓冲区是否仍然有效（未被销毁）。
 * 
 * @param p 顶点缓冲区指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const VertexBuffer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查围栏是否有效
 * 
 * 检查指定的围栏是否仍然有效（未被销毁）。
 * 
 * @param p 围栏指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Fence* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查同步对象是否有效
 * 
 * 检查指定的同步对象是否仍然有效（未被销毁）。
 * 
 * @param p 同步对象指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Sync* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查索引缓冲区是否有效
 * 
 * 检查指定的索引缓冲区是否仍然有效（未被销毁）。
 * 
 * @param p 索引缓冲区指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const IndexBuffer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查蒙皮缓冲区是否有效
 * 
 * 检查指定的蒙皮缓冲区是否仍然有效（未被销毁）。
 * 
 * @param p 蒙皮缓冲区指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const SkinningBuffer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查变形目标缓冲区是否有效
 * 
 * 检查指定的变形目标缓冲区是否仍然有效（未被销毁）。
 * 
 * @param p 变形目标缓冲区指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const MorphTargetBuffer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查间接光是否有效
 * 
 * 检查指定的间接光对象是否仍然有效（未被销毁）。
 * 
 * @param p 间接光指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const IndirectLight* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查材质是否有效
 * 
 * 检查指定的材质对象是否仍然有效（未被销毁）。
 * 
 * @param p 材质指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Material* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查材质实例是否有效（相对于材质）
 * 
 * 检查指定的材质实例是否仍然有效，并且属于指定的材质。
 * 
 * @param m 材质指针
 * @param p 材质实例指针
 * @return true 如果材质实例有效且属于指定材质，false 否则
 * 
 * 实现：从内部实现类查询材质实例有效性
 */
bool Engine::isValid(const Material* m, const MaterialInstance* p) const {
    return downcast(this)->isValid(downcast(m), downcast(p));
}

/**
 * 检查材质实例是否有效（昂贵版本）
 * 
 * 执行更彻底的检查，验证材质实例的完整有效性。
 * 这比普通的 isValid 更慢，但更可靠。
 * 
 * @param p 材质实例指针
 * @return true 如果材质实例完全有效，false 否则
 * 
 * 实现：从内部实现类执行昂贵的有效性检查
 * 
 * 注意：此方法可能较慢，仅在必要时使用
 */
bool Engine::isValidExpensive(const MaterialInstance* p) const {
    return downcast(this)->isValidExpensive(downcast(p));
}

/**
 * 检查渲染器是否有效
 * 
 * 检查指定的渲染器是否仍然有效（未被销毁）。
 * 
 * @param p 渲染器指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Renderer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查场景是否有效
 * 
 * 检查指定的场景是否仍然有效（未被销毁）。
 * 
 * @param p 场景指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Scene* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查天空盒是否有效
 * 
 * 检查指定的天空盒是否仍然有效（未被销毁）。
 * 
 * @param p 天空盒指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Skybox* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查颜色分级是否有效
 * 
 * 检查指定的颜色分级对象是否仍然有效（未被销毁）。
 * 
 * @param p 颜色分级指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const ColorGrading* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查交换链是否有效
 * 
 * 检查指定的交换链是否仍然有效（未被销毁）。
 * 
 * @param p 交换链指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const SwapChain* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查流是否有效
 * 
 * 检查指定的流对象是否仍然有效（未被销毁）。
 * 
 * @param p 流指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Stream* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查纹理是否有效
 * 
 * 检查指定的纹理是否仍然有效（未被销毁）。
 * 
 * @param p 纹理指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const Texture* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查渲染目标是否有效
 * 
 * 检查指定的渲染目标是否仍然有效（未被销毁）。
 * 
 * @param p 渲染目标指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const RenderTarget* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查视图是否有效
 * 
 * 检查指定的视图是否仍然有效（未被销毁）。
 * 
 * @param p 视图指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const View* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 检查实例缓冲区是否有效
 * 
 * 检查指定的实例缓冲区是否仍然有效（未被销毁）。
 * 
 * @param p 实例缓冲区指针
 * @return true 如果对象有效，false 如果对象无效或已被销毁
 * 
 * 实现：从内部实现类查询对象有效性
 */
bool Engine::isValid(const InstanceBuffer* p) const {
    return downcast(this)->isValid(downcast(p));
}

/**
 * 获取缓冲区对象数量
 * 
 * 返回引擎当前管理的缓冲区对象数量。
 * 
 * @return 缓冲区对象数量
 * 
 * 实现：从内部实现类获取缓冲区对象计数
 */
size_t Engine::getBufferObjectCount() const noexcept {
    return downcast(this)->getBufferObjectCount();
}

/**
 * 获取视图数量
 * 
 * 返回引擎当前管理的视图数量。
 * 
 * @return 视图数量
 * 
 * 实现：从内部实现类获取视图计数
 */
size_t Engine::getViewCount() const noexcept {
    return downcast(this)->getViewCount();
}

/**
 * 获取场景数量
 * 
 * 返回引擎当前管理的场景数量。
 * 
 * @return 场景数量
 * 
 * 实现：从内部实现类获取场景计数
 */
size_t Engine::getSceneCount() const noexcept {
    return downcast(this)->getSceneCount();
}

/**
 * 获取交换链数量
 * 
 * 返回引擎当前管理的交换链数量。
 * 
 * @return 交换链数量
 * 
 * 实现：从内部实现类获取交换链计数
 */
size_t Engine::getSwapChainCount() const noexcept {
    return downcast(this)->getSwapChainCount();
}

/**
 * 获取流数量
 * 
 * 返回引擎当前管理的流数量。
 * 
 * @return 流数量
 * 
 * 实现：从内部实现类获取流计数
 */
size_t Engine::getStreamCount() const noexcept {
    return downcast(this)->getStreamCount();
}

/**
 * 获取索引缓冲区数量
 * 
 * 返回引擎当前管理的索引缓冲区数量。
 * 
 * @return 索引缓冲区数量
 * 
 * 实现：从内部实现类获取索引缓冲区计数
 */
size_t Engine::getIndexBufferCount() const noexcept {
    return downcast(this)->getIndexBufferCount();
}

/**
 * 获取蒙皮缓冲区数量
 * 
 * 返回引擎当前管理的蒙皮缓冲区数量。
 * 
 * @return 蒙皮缓冲区数量
 * 
 * 实现：从内部实现类获取蒙皮缓冲区计数
 */
size_t Engine::getSkinningBufferCount() const noexcept {
    return downcast(this)->getSkinningBufferCount();
}

/**
 * 获取变形目标缓冲区数量
 * 
 * 返回引擎当前管理的变形目标缓冲区数量。
 * 
 * @return 变形目标缓冲区数量
 * 
 * 实现：从内部实现类获取变形目标缓冲区计数
 */
size_t Engine::getMorphTargetBufferCount() const noexcept {
    return downcast(this)->getMorphTargetBufferCount();
}

/**
 * 获取实例缓冲区数量
 * 
 * 返回引擎当前管理的实例缓冲区数量。
 * 
 * @return 实例缓冲区数量
 * 
 * 实现：从内部实现类获取实例缓冲区计数
 */
size_t Engine::getInstanceBufferCount() const noexcept {
    return downcast(this)->getInstanceBufferCount();
}

/**
 * 获取顶点缓冲区数量
 * 
 * 返回引擎当前管理的顶点缓冲区数量。
 * 
 * @return 顶点缓冲区数量
 * 
 * 实现：从内部实现类获取顶点缓冲区计数
 */
size_t Engine::getVertexBufferCount() const noexcept {
    return downcast(this)->getVertexBufferCount();
}

/**
 * 获取间接光数量
 * 
 * 返回引擎当前管理的间接光数量。
 * 
 * @return 间接光数量
 * 
 * 实现：从内部实现类获取间接光计数
 */
size_t Engine::getIndirectLightCount() const noexcept {
    return downcast(this)->getIndirectLightCount();
}

/**
 * 获取材质数量
 * 
 * 返回引擎当前管理的材质数量。
 * 
 * @return 材质数量
 * 
 * 实现：从内部实现类获取材质计数
 */
size_t Engine::getMaterialCount() const noexcept {
    return downcast(this)->getMaterialCount();
}

/**
 * 获取纹理数量
 * 
 * 返回引擎当前管理的纹理数量。
 * 
 * @return 纹理数量
 * 
 * 实现：从内部实现类获取纹理计数
 */
size_t Engine::getTextureCount() const noexcept {
    return downcast(this)->getTextureCount();
}

/**
 * 获取天空盒数量
 * 
 * 返回引擎当前管理的天空盒数量。
 * 
 * @return 天空盒数量
 * 
 * 实现：从内部实现类获取天空盒计数
 */
size_t Engine::getSkyboxeCount() const noexcept {
    return downcast(this)->getSkyboxeCount();
}

/**
 * 获取颜色分级数量
 * 
 * 返回引擎当前管理的颜色分级数量。
 * 
 * @return 颜色分级数量
 * 
 * 实现：从内部实现类获取颜色分级计数
 */
size_t Engine::getColorGradingCount() const noexcept {
    return downcast(this)->getColorGradingCount();
}

/**
 * 获取渲染目标数量
 * 
 * 返回引擎当前管理的渲染目标数量。
 * 
 * @return 渲染目标数量
 * 
 * 实现：从内部实现类获取渲染目标计数
 */
size_t Engine::getRenderTargetCount() const noexcept {
    return downcast(this)->getRenderTargetCount();
}


/**
 * 刷新并等待
 * 
 * 刷新所有待处理的 GPU 命令并等待它们完成。
 * 这是一个阻塞调用，会等待所有命令执行完毕。
 * 
 * 实现：将调用转发到内部实现类刷新并等待
 * 
 * 注意：此方法会阻塞直到所有命令完成，可能较慢
 */
void Engine::flushAndWait() {
    downcast(this)->flushAndWait();
}

/**
 * 刷新并等待（带超时）
 * 
 * 刷新所有待处理的 GPU 命令并等待它们完成，但最多等待指定时间。
 * 
 * @param timeout 超时时间（纳秒）
 *                0 表示无限等待（等同于 flushAndWait()）
 * @return true 如果所有命令在超时前完成，false 如果超时
 * 
 * 实现：将调用转发到内部实现类刷新并等待（带超时）
 * 
 * 注意：如果超时，某些命令可能仍在执行
 */
bool Engine::flushAndWait(uint64_t timeout) {
    return downcast(this)->flushAndWait(timeout);
}

/**
 * 刷新命令队列
 * 
 * 刷新所有待处理的 GPU 命令，但不等待它们完成。
 * 这是一个非阻塞调用，命令会在后台执行。
 * 
 * 实现：将调用转发到内部实现类刷新命令队列
 * 
 * 注意：此方法不会等待命令完成，立即返回
 */
void Engine::flush() {
    downcast(this)->flush();
}

/**
 * 获取实体管理器
 * 
 * 返回引擎的实体管理器。实体管理器用于创建和管理实体。
 * 
 * @return 实体管理器引用
 * 
 * 实现：从内部实现类获取实体管理器
 */
EntityManager& Engine::getEntityManager() noexcept {
    return downcast(this)->getEntityManager();
}

/**
 * 获取可渲染对象管理器
 * 
 * 返回引擎的可渲染对象管理器。可渲染对象管理器用于创建和管理可渲染对象。
 * 
 * @return 可渲染对象管理器引用
 * 
 * 实现：从内部实现类获取可渲染对象管理器
 */
RenderableManager& Engine::getRenderableManager() noexcept {
    return downcast(this)->getRenderableManager();
}

/**
 * 获取光源管理器
 * 
 * 返回引擎的光源管理器。光源管理器用于创建和管理光源。
 * 
 * @return 光源管理器引用
 * 
 * 实现：从内部实现类获取光源管理器
 */
LightManager& Engine::getLightManager() noexcept {
    return downcast(this)->getLightManager();
}

/**
 * 获取变换管理器
 * 
 * 返回引擎的变换管理器。变换管理器用于管理实体的位置、旋转和缩放。
 * 
 * @return 变换管理器引用
 * 
 * 实现：从内部实现类获取变换管理器
 */
TransformManager& Engine::getTransformManager() noexcept {
    return downcast(this)->getTransformManager();
}

/**
 * 启用精确平移
 * 
 * 启用变换管理器的精确平移模式。精确平移使用双精度浮点数，提高大坐标的精度。
 * 
 * 实现：通过变换管理器启用精确平移
 */
void Engine::enableAccurateTranslations() noexcept  {
    getTransformManager().setAccurateTranslationsEnabled(true);
}

/**
 * 流分配
 * 
 * 从引擎的流分配器中分配内存。这用于临时缓冲区分配。
 * 
 * @param size 要分配的字节数
 * @param alignment 内存对齐要求（必须是 2 的幂）
 * @return 分配的内存指针，如果分配失败则返回 nullptr
 * 
 * 实现：从内部实现类的流分配器分配内存
 */
void* Engine::streamAlloc(size_t const size, size_t const alignment) noexcept {
    return downcast(this)->streamAlloc(size, alignment);
}

/**
 * 执行（单线程环境）
 * 
 * 执行引擎的命令队列。此方法仅用于单线程环境。
 * 
 * 此方法会：
 * 1. 刷新命令队列
 * 2. 执行命令
 * 
 * 注意：
 * - 此方法会丢弃布尔返回值（在多线程环境中表示线程退出）
 * - 仅在单线程环境中使用
 * 
 * 实现：
 * 1. 检查是否在单线程环境中（如果不是会触发断言）
 * 2. 刷新命令队列
 * 3. 执行命令
 */
void Engine::execute() {
    FILAMENT_CHECK_PRECONDITION(!UTILS_HAS_THREADING)
            << "Execute is meant for single-threaded platforms.";
    downcast(this)->flush();
    downcast(this)->execute();
}

/**
 * 获取作业系统
 * 
 * 返回引擎的作业系统。作业系统用于并行任务执行。
 * 
 * @return 作业系统引用
 * 
 * 实现：从内部实现类获取作业系统
 */
JobSystem& Engine::getJobSystem() noexcept {
    return downcast(this)->getJobSystem();
}

/**
 * 检查是否暂停
 * 
 * 检查引擎是否处于暂停状态。此方法仅用于多线程环境。
 * 
 * @return true 如果引擎已暂停，false 如果正在运行
 * 
 * 实现：从内部实现类查询暂停状态
 * 
 * 注意：仅在多线程环境中使用，否则会触发断言
 */
bool Engine::isPaused() const noexcept(UTILS_HAS_THREADING) {
    FILAMENT_CHECK_PRECONDITION(UTILS_HAS_THREADING)
            << "Pause is meant for multi-threaded platforms.";
    return downcast(this)->isPaused();
}

/**
 * 设置暂停状态
 * 
 * 暂停或恢复引擎的执行。此方法仅用于多线程环境。
 * 
 * @param paused true 暂停引擎，false 恢复执行
 * 
 * 实现：将调用转发到内部实现类设置暂停状态
 * 
 * 注意：仅在多线程环境中使用，否则会触发断言
 */
void Engine::setPaused(bool const paused) {
    FILAMENT_CHECK_PRECONDITION(UTILS_HAS_THREADING)
            << "Pause is meant for multi-threaded platforms.";
    downcast(this)->setPaused(paused);
}

/**
 * 获取调试注册表
 * 
 * 返回引擎的调试注册表。调试注册表用于访问和修改调试选项。
 * 
 * @return 调试注册表引用
 * 
 * 实现：从内部实现类获取调试注册表
 */
DebugRegistry& Engine::getDebugRegistry() noexcept {
    return downcast(this)->getDebugRegistry();
}

/**
 * 泵送消息队列
 * 
 * 处理引擎内部的消息队列。这用于处理异步操作和回调。
 * 
 * 实现：将调用转发到内部实现类处理消息队列
 * 
 * 注意：应该定期调用此方法以处理待处理的消息
 */
void Engine::pumpMessageQueues() {
    downcast(this)->pumpMessageQueues();
}

/**
 * 取消保护
 * 
 * 取消引擎的保护状态。这用于某些特殊场景下的操作。
 * 
 * 实现：将调用转发到内部实现类取消保护
 * 
 * 注意：此方法的使用需要谨慎，仅在必要时使用
 */
void Engine::unprotected() noexcept {
    downcast(this)->unprotected();
}

/**
 * 设置自动实例化启用状态
 * 
 * 启用或禁用自动实例化。自动实例化会自动合并相同材质的对象以提高性能。
 * 
 * @param enable true 启用自动实例化，false 禁用
 * 
 * 实现：将调用转发到内部实现类设置自动实例化状态
 */
void Engine::setAutomaticInstancingEnabled(bool const enable) noexcept {
    downcast(this)->setAutomaticInstancingEnabled(enable);
}

/**
 * 检查自动实例化是否启用
 * 
 * 检查引擎是否启用了自动实例化。
 * 
 * @return true 如果启用了自动实例化，false 如果禁用
 * 
 * 实现：从内部实现类查询自动实例化状态
 */
bool Engine::isAutomaticInstancingEnabled() const noexcept {
    return downcast(this)->isAutomaticInstancingEnabled();
}

/**
 * 获取支持的特性级别
 * 
 * 返回引擎支持的最高特性级别。
 * 
 * @return 支持的特性级别
 *         - FEATURE_LEVEL_0: 基础特性（OpenGL ES 2.0）
 *         - FEATURE_LEVEL_1: 标准特性（OpenGL ES 3.0）
 *         - FEATURE_LEVEL_2: 高级特性（OpenGL ES 3.1+）
 * 
 * 实现：从内部实现类获取支持的特性级别
 */
FeatureLevel Engine::getSupportedFeatureLevel() const noexcept {
    return downcast(this)->getSupportedFeatureLevel();
}

/**
 * 设置活动特性级别
 * 
 * 设置引擎使用的活动特性级别。活动级别不能超过支持的特性级别。
 * 
 * @param featureLevel 要设置的特性级别
 * @return 实际设置的特性级别（如果请求的级别不支持，会返回支持的最高级别）
 * 
 * 实现：将调用转发到内部实现类设置活动特性级别
 */
FeatureLevel Engine::setActiveFeatureLevel(FeatureLevel const featureLevel) {
    return downcast(this)->setActiveFeatureLevel(featureLevel);
}

/**
 * 获取活动特性级别
 * 
 * 返回引擎当前使用的活动特性级别。
 * 
 * @return 活动特性级别
 * 
 * 实现：从内部实现类获取活动特性级别
 */
FeatureLevel Engine::getActiveFeatureLevel() const noexcept {
    return downcast(this)->getActiveFeatureLevel();
}

/**
 * 获取最大自动实例数
 * 
 * 返回自动实例化的最大实例数。
 * 
 * @return 最大自动实例数
 * 
 * 实现：从内部实现类获取最大自动实例数
 */
size_t Engine::getMaxAutomaticInstances() const noexcept {
    return downcast(this)->getMaxAutomaticInstances();
}

/**
 * 获取引擎配置
 * 
 * 返回引擎的配置信息。
 * 
 * @return 引擎配置常量引用
 * 
 * 实现：从内部实现类获取引擎配置
 */
const Engine::Config& Engine::getConfig() const noexcept {
    return downcast(this)->getConfig();
}

/**
 * 检查是否支持立体渲染
 * 
 * 检查引擎是否支持指定类型的立体渲染。
 * 
 * @param type 立体渲染类型（参数未使用，保留用于未来扩展）
 * @return true 如果支持立体渲染，false 否则
 * 
 * 实现：从内部实现类查询立体渲染支持
 */
bool Engine::isStereoSupported(StereoscopicType) const noexcept {
    return downcast(this)->isStereoSupported();
}

/**
 * 检查是否支持异步操作
 * 
 * 检查引擎是否支持异步操作（如异步纹理上传）。
 * 
 * @return true 如果支持异步操作，false 否则
 * 
 * 实现：从内部实现类查询异步操作支持
 */
bool Engine::isAsynchronousOperationSupported() const noexcept {
    return downcast(this)->isAsynchronousOperationSupported();
}

/**
 * 获取最大立体眼睛数
 * 
 * 返回引擎支持的最大立体眼睛数。
 * 
 * @return 最大立体眼睛数
 * 
 * 实现：调用静态方法获取最大立体眼睛数
 */
size_t Engine::getMaxStereoscopicEyes() noexcept {
    return FEngine::getMaxStereoscopicEyes();
}

/**
 * 获取稳定时钟时间（纳秒）
 * 
 * 返回当前稳定时钟的时间戳（纳秒）。
 * 稳定时钟是单调递增的，不受系统时间调整影响。
 * 
 * @return 稳定时钟时间戳（纳秒）
 * 
 * 实现：使用 std::chrono::steady_clock 获取当前时间
 */
uint64_t Engine::getSteadyClockTimeNano() noexcept {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

/**
 * 获取特性标志列表
 * 
 * 返回所有可用的特性标志列表。
 * 
 * @return 特性标志切片（只读）
 * 
 * 实现：从内部实现类获取特性标志列表
 */
Slice<const Engine::FeatureFlag> Engine::getFeatureFlags() const noexcept {
    return downcast(this)->getFeatureFlags();
}

/**
 * 设置特性标志
 * 
 * 设置指定名称的特性标志的值。
 * 
 * @param name 特性标志名称（C 字符串）
 * @param value 要设置的值
 * @return true 如果成功设置，false 如果特性标志不存在
 * 
 * 实现：将调用转发到内部实现类设置特性标志
 */
bool Engine::setFeatureFlag(char const* name, bool const value) noexcept {
    return downcast(this)->setFeatureFlag(name, value);
}

/**
 * 获取特性标志
 * 
 * 获取指定名称的特性标志的值。
 * 
 * @param name 特性标志名称（C 字符串）
 * @return 特性标志的值（如果存在），否则返回 std::nullopt
 * 
 * 实现：从内部实现类获取特性标志值
 */
std::optional<bool> Engine::getFeatureFlag(char const* name) const noexcept {
    return downcast(this)->getFeatureFlag(name);
}

/**
 * 获取特性标志指针
 * 
 * 获取指定名称的特性标志的指针。可以直接修改指针指向的值。
 * 
 * @param name 特性标志名称（C 字符串，不能为 nullptr）
 * @return 特性标志的指针（如果存在），否则返回 nullptr
 * 
 * 实现：从内部实现类获取特性标志指针
 * 
 * 注意：返回的指针可以直接修改，修改会立即生效
 */
bool* Engine::getFeatureFlagPtr(char const* UTILS_NONNULL name) const noexcept {
    return downcast(this)->getFeatureFlagPtr(name);
}

#if defined(__EMSCRIPTEN__)
/**
 * 重置后端状态（Emscripten 专用）
 * 
 * 重置 WebGL 后端的状态。这在 Emscripten 环境中用于处理 WebGL 上下文丢失。
 * 
 * 实现：将调用转发到内部实现类重置后端状态
 * 
 * 注意：此方法仅在 Emscripten 编译时可用
 */
void Engine::resetBackendState() noexcept {
    downcast(this)->resetBackendState();
}
#endif

} // namespace filament
