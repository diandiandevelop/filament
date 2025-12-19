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

#ifndef TNT_FILAMAT_UIBGENERATOR_H
#define TNT_FILAMAT_UIBGENERATOR_H

#include <backend/DriverEnums.h>

#include <utils/BitmaskEnum.h>

#include <type_traits>

#include <stddef.h>
#include <stdint.h>

namespace filament {

class BufferInterfaceBlock;

// UBO生成器类，用于生成和管理统一缓冲区对象（UBO）
class UibGenerator {
public:
    // tag to represent a generated ubo.
    // 表示生成的UBO的标签
    enum class Ubo : uint8_t {
        FrameUniforms,              // uniforms updated per view - 每视图更新的统一变量
        ObjectUniforms,             // uniforms updated per renderable - 每个可渲染对象更新的统一变量
        BonesUniforms,              // bones data, per renderable - 骨骼数据，每个可渲染对象
        MorphingUniforms,           // morphing uniform/sampler updated per render primitive - 变形统一变量/采样器，每个渲染基元更新
        LightsUniforms,             // lights data array - 光源数据数组
        ShadowUniforms,             // punctual shadow data - 点光源阴影数据
        FroxelRecordUniforms,       // froxel records - froxel记录
        FroxelsUniforms,            // froxels - froxel数据
        MaterialParams,             // material instance ubo - 材质实例UBO
        // Update utils::Enum::count<>() below when adding values here
        // These are limited by CONFIG_BINDING_COUNT (currently 10)
        // When adding an UBO here, make sure to also update
        //      MaterialBuilder::writeCommonChunks() if needed
        // 在此处添加值时，更新下面的utils::Enum::count<>()
        // 这些受CONFIG_BINDING_COUNT限制（当前为10）
        // 在此处添加UBO时，如果需要，确保也更新MaterialBuilder::writeCommonChunks()
    };

    // 绑定信息结构，包含描述符集和绑定点
    struct Binding {
        backend::descriptor_set_t set;        // 描述符集
        backend::descriptor_binding_t binding; // 绑定点
    };

    // return the BufferInterfaceBlock for the given UBO tag
    // 返回给定UBO标签的BufferInterfaceBlock
    static BufferInterfaceBlock const& get(Ubo ubo) noexcept;

    // return the {set, binding } for the given UBO tag
    // 返回给定UBO标签的{set, binding}
    static Binding getBinding(Ubo ubo) noexcept;

    // deprecate these...
    static BufferInterfaceBlock const& getPerViewUib() noexcept;
    static BufferInterfaceBlock const& getPerRenderableUib() noexcept;
    static BufferInterfaceBlock const& getLightsUib() noexcept;
    static BufferInterfaceBlock const& getShadowUib() noexcept;
    static BufferInterfaceBlock const& getPerRenderableBonesUib() noexcept;
    static BufferInterfaceBlock const& getPerRenderableMorphingUib() noexcept;
    static BufferInterfaceBlock const& getFroxelRecordUib() noexcept;
    static BufferInterfaceBlock const& getFroxelsUib() noexcept;
};

} // namespace filament

template<>
struct utils::EnableIntegerOperators<filament::UibGenerator::Ubo> : public std::true_type {};

template<>
inline constexpr size_t utils::Enum::count<filament::UibGenerator::Ubo>() { return 9; }


#endif // TNT_FILAMAT_UIBGENERATOR_H
