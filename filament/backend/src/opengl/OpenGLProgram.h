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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_OPENGLPROGRAM_H
#define TNT_FILAMENT_BACKEND_OPENGL_OPENGLPROGRAM_H

// 驱动基类
#include "DriverBase.h"

// OpenGL 相关头文件
#include "BindingMap.h"              // 绑定映射
#include "OpenGLContext.h"           // OpenGL 上下文
#include "ShaderCompilerService.h"   // 着色器编译服务

// 后端驱动接口
#include <private/backend/Driver.h>

// 后端枚举和程序
#include <backend/DriverEnums.h>     // 驱动枚举
#include <backend/Program.h>         // 程序接口

// Utils 工具库
#include <utils/bitset.h>            // 位集合
#include <utils/compiler.h>          // 编译器工具
#include <utils/FixedCapacityVector.h>  // 固定容量向量
#include <utils/Slice.h>             // 切片

// 标准库
#include <limits>                     // 数值限制

#include <stddef.h>                  // 标准定义
#include <stdint.h>                  // 标准整数类型

namespace filament::backend {

class OpenGLDriver;

/**
 * Push Constant 包
 * 
 * 包含顶点和片段着色器的 Push Constant 数据。
 * 
 * 字段说明：
 * - vertexConstants: 顶点着色器的 Push Constant 列表（位置和类型对）
 * - fragmentConstants: 片段着色器的 Push Constant 列表（位置和类型对）
 */
struct PushConstantBundle {
    utils::Slice<const std::pair<GLint, ConstantType>> vertexConstants;    // 顶点着色器常量
    utils::Slice<const std::pair<GLint, ConstantType>> fragmentConstants; // 片段着色器常量
};

/**
 * OpenGL 程序类
 * 
 * 封装 OpenGL 着色器程序，管理程序状态、绑定映射和 Push Constant。
 * 
 * 主要功能：
 * 1. 延迟初始化：程序在首次使用时才编译和链接
 * 2. 绑定映射：管理描述符堆和绑定到 OpenGL 绑定点的映射
 * 3. Push Constant：管理顶点和片段着色器的 Push Constant
 * 4. ES2 支持：为 ES2 提供 Uniform 更新功能
 * 
 * 注意：OpenGLProgram 的大小必须 <= 96 字节，以保持在较小的 Handle 桶中
 */
class OpenGLProgram : public HwProgram {
public:

    /**
     * 默认构造函数
     * 
     * 创建一个空的 OpenGLProgram。
     */
    OpenGLProgram() noexcept;

    /**
     * 构造函数
     * 
     * 从 Program 对象创建 OpenGLProgram。
     * 程序不会立即编译，而是在首次使用时延迟编译。
     * 
     * @param gld OpenGLDriver 引用
     * @param program 程序对象（将被移动）
     */
    OpenGLProgram(OpenGLDriver& gld, Program&& program) noexcept;

    /**
     * 析构函数
     * 
     * 清理程序资源，包括删除 OpenGL 程序和编译令牌。
     */
    ~OpenGLProgram() noexcept;

    /**
     * 检查程序是否有效
     * 
     * 程序有效当且仅当有编译令牌或已编译的程序对象。
     * 
     * @return 如果程序有效返回 true，否则返回 false
     */
    bool isValid() const noexcept { return mToken || gl.program != 0; }

    /**
     * 使用程序
     * 
     * 使此程序成为当前使用的程序。
     * 如果是首次使用，会触发延迟初始化（编译和链接）。
     * 
     * @param gld OpenGLDriver 指针
     * @param context OpenGLContext 引用
     * @return 成功返回 true，失败返回 false
     */
    bool use(OpenGLDriver* const gld, OpenGLContext& context) noexcept {
        // 根据构造，mToken 和 gl.program 不可能同时非空
        assert_invariant(!mToken || !gl.program);

        if (UTILS_UNLIKELY(mToken && !gl.program)) {
            // 首次使用程序，触发延迟初始化
            initialize(*gld);
        }

        if (UTILS_UNLIKELY(!gl.program)) {
            // 编译失败（token 应该为 null）
            assert_invariant(!mToken);
            return false;
        }

        // 使用程序
        context.useProgram(gl.program);
        return true;
    }

    /**
     * 获取缓冲区绑定
     * 
     * 返回指定描述符堆和绑定的 OpenGL 缓冲区绑定点。
     * 
     * @param set 描述符堆索引
     * @param binding 绑定索引
     * @return OpenGL 缓冲区绑定点
     */
    GLuint getBufferBinding(descriptor_set_t set, descriptor_binding_t binding) const noexcept {
        return mBindingMap.get(set, binding);
    }

    /**
     * 获取纹理单元
     * 
     * 返回指定描述符堆和绑定的 OpenGL 纹理单元。
     * 
     * @param set 描述符堆索引
     * @param binding 绑定索引
     * @return OpenGL 纹理单元
     */
    GLuint getTextureUnit(descriptor_set_t set, descriptor_binding_t binding) const noexcept {
        return mBindingMap.get(set, binding);
    }

    /**
     * 获取活动描述符
     * 
     * 返回指定描述符堆中活动的描述符位集合。
     * 
     * @param set 描述符堆索引
     * @return 活动描述符位集合
     */
    utils::bitset64 getActiveDescriptors(descriptor_set_t set) const noexcept {
        return mBindingMap.getActiveDescriptors(set);
    }

    // ES2 专用方法

    /**
     * 更新 Uniform（仅 ES2）
     * 
     * 更新指定 Uniform 缓冲区的 Uniform 值。
     * 仅用于 ES2，因为 ES2 不支持 UBO，需要手动更新每个 Uniform。
     * 
     * @param index Uniform 绑定索引
     * @param id Uniform 缓冲区 ID
     * @param buffer Uniform 数据缓冲区
     * @param age Uniform 缓冲区年龄（用于检测更新）
     * @param offset Uniform 缓冲区偏移
     */
    void updateUniforms(uint32_t index, GLuint id, void const* buffer, uint16_t age, uint32_t offset) const noexcept;

    /**
     * 设置 Rec709 色彩空间（仅 ES2）
     * 
     * 设置 Rec709 色彩空间标志。
     * 仅用于 ES2。
     * 
     * @param rec709 是否为 Rec709 色彩空间
     */
    void setRec709ColorSpace(bool rec709) const noexcept;

    /**
     * 获取 Push Constant 包
     * 
     * 返回包含顶点和片段着色器 Push Constant 的包。
     * 
     * @return Push Constant 包
     */
    PushConstantBundle getPushConstants() {
        auto fragBegin = mPushConstants.begin() + mPushConstantFragmentStageOffset;
        return {
                .vertexConstants = { mPushConstants.begin(), fragBegin },
                .fragmentConstants = { fragBegin, mPushConstants.end() },
        };
    }

private:
    /**
     * 延迟初始化数据
     * 
     * 存储程序首次使用前需要的数据。
     * 这些数据在程序编译完成后会被删除。
     */
    struct LazyInitializationData;

    /**
     * 初始化程序
     * 
     * 从编译令牌获取已编译的程序，并初始化程序状态。
     * 
     * @param gld OpenGLDriver 引用
     */
    void initialize(OpenGLDriver& gld);

    /**
     * 初始化程序状态
     * 
     * 从已编译的程序初始化绑定映射、Push Constant 等状态。
     * 
     * @param context OpenGLContext 引用
     * @param program OpenGL 程序对象 ID
     * @param lazyInitializationData 延迟初始化数据
     */
    void initializeProgramState(OpenGLContext& context, GLuint program,
            LazyInitializationData& lazyInitializationData);

    BindingMap mBindingMap;     // 绑定映射：8 字节 + 外联 256 字节

    ShaderCompilerService::program_token_t mToken{};    // 编译令牌：16 字节

    // 注意：如果需要，可以用原始指针和 uint8_t（用于大小）替换，
    // 以将容器大小减少到 9 字节
    utils::FixedCapacityVector<std::pair<GLint, ConstantType>> mPushConstants;  // Push Constant：16 字节

    // 仅 ES2 需要
    using LocationInfo = utils::FixedCapacityVector<GLint>;  // Uniform 位置信息类型

    /**
     * Uniform 记录（仅 ES2）
     * 
     * 存储 ES2 的 Uniform 信息，包括 Uniform 数据、位置和更新状态。
     */
    struct UniformsRecord {
        Program::UniformInfo uniforms;  // Uniform 信息
        LocationInfo locations;         // Uniform 位置
        mutable GLuint id = 0;          // Uniform 缓冲区 ID（用于检测更新）
        mutable uint16_t age = std::numeric_limits<uint16_t>::max();  // Uniform 缓冲区年龄
        mutable uint32_t offset = 0;     // Uniform 缓冲区偏移
    };
    UniformsRecord const* mUniformsRecords = nullptr;  // Uniform 记录数组（仅 ES2）
    GLint mRec709Location : 24;     // Rec709 色彩空间 Uniform 位置：4 字节

    // 片段着色器 Push Constant 数组偏移
    GLint mPushConstantFragmentStageOffset : 8;      // 1 字节

public:
    /**
     * OpenGL 程序对象
     * 
     * 存储 OpenGL 程序对象 ID。
     */
    struct {
        GLuint program = 0;  // OpenGL 程序对象 ID
    } gl;                                               // 4 字节
};

// 如果 OpenGLProgram 大于 96 字节，它将落入更大的 Handle 桶中
static_assert(sizeof(OpenGLProgram) <= 96); // 当前 96 字节

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_OPENGL_OPENGLPROGRAM_H
