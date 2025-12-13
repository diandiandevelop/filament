/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_DETAILS_SKYBOX_H
#define TNT_FILAMENT_DETAILS_SKYBOX_H

#include "downcast.h"

#include <filament/Skybox.h>

#include <private/backend/DriverApi.h>

#include <utils/compiler.h>
#include <utils/Entity.h>

namespace filament {

class FEngine;
class FTexture;
class FMaterial;
class FMaterialInstance;
class FRenderableManager;

/**
 * 天空盒实现类
 * 
 * 管理场景的天空盒，用于渲染背景。
 * 天空盒可以是纹理或纯色。
 */
class FSkybox : public Skybox {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     * @param builder 构建器引用
     */
    FSkybox(FEngine& engine, const Builder& builder) noexcept;

    /**
     * 创建天空盒材质
     * 
     * 创建用于渲染天空盒的材质。
     * 
     * @param engine 引擎引用
     * @return 材质常量指针
     */
    static FMaterial const* createMaterial(FEngine& engine);

    /**
     * 终止天空盒
     * 
     * 释放资源，对象变为无效。
     * 
     * @param engine 引擎引用
     */
    void terminate(FEngine& engine) noexcept;

    /**
     * 获取实体
     * 
     * @return 天空盒实体
     */
    utils::Entity getEntity() const noexcept { return mSkybox; }

    /**
     * 设置层掩码
     * 
     * 控制天空盒在哪些层可见。
     * 
     * @param select 选择掩码（哪些位有效）
     * @param values 值掩码（位的值）
     */
    void setLayerMask(uint8_t select, uint8_t values) noexcept;
    
    /**
     * 获取层掩码
     * 
     * @return 层掩码
     */
    uint8_t getLayerMask() const noexcept { return mLayerMask; }

    /**
     * 获取强度
     * 
     * @return 天空盒强度
     */
    float getIntensity() const noexcept { return mIntensity; }

    /**
     * 获取纹理
     * 
     * @return 天空盒纹理常量指针（如果没有纹理则返回 nullptr）
     */
    FTexture const* getTexture() const noexcept { return mSkyboxTexture; }

    /**
     * 设置颜色
     * 
     * 设置纯色天空盒的颜色。
     * 
     * @param color 颜色（RGBA）
     */
    void setColor(math::float4 color) noexcept;

private:
    /**
     * 我们不拥有这些对象
     */
    // we don't own these
    FTexture const* mSkyboxTexture = nullptr;  // 天空盒纹理指针（不拥有）

    /**
     * 我们拥有这些对象
     */
    // we own these
    FMaterialInstance* mSkyboxMaterialInstance = nullptr;  // 天空盒材质实例指针（拥有）
    utils::Entity mSkybox;  // 天空盒实体
    FRenderableManager& mRenderableManager;  // 可渲染对象管理器引用
    float mIntensity = 0.0f;  // 强度
    uint8_t mLayerMask = 0x1;  // 层掩码（默认第 0 层）
};

FILAMENT_DOWNCAST(Skybox)

} // namespace filament


#endif // TNT_FILAMENT_DETAILS_SKYBOX_H
