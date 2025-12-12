/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_BINDINGMAP_H
#define TNT_FILAMENT_BACKEND_OPENGL_BINDINGMAP_H

// 后端枚举
#include <backend/DriverEnums.h>

// OpenGL 头文件
#include "gl_headers.h"

// Utils 工具库
#include <utils/bitset.h>   // 位集合
#include <utils/debug.h>    // 调试工具

#include <new>              // 内存分配

#include <stddef.h>         // 标准定义
#include <stdint.h>         // 标准整数类型
#include <string.h>         // 字符串操作

namespace filament::backend {

/**
 * 绑定映射类
 * 
 * 管理描述符集（descriptor set）和绑定（binding）之间的映射关系。
 * 
 * 主要功能：
 * 1. 存储描述符集和绑定的映射关系
 * 2. 跟踪活动的描述符绑定
 * 3. 使用压缩存储以节省内存
 * 
 * 设计特点：
 * - 使用位字段压缩存储（7 位用于 binding，1 位用于 sampler 标志）
 * - 使用位集合跟踪活动的描述符
 * - 支持多个描述符集（MAX_DESCRIPTOR_SET_COUNT）
 * - 每个描述符集支持多个绑定（MAX_DESCRIPTOR_COUNT）
 * 
 * 使用场景：
 * - 在着色器程序中查找描述符绑定的位置
 * - 跟踪哪些描述符绑定是活动的
 * - 优化描述符集的绑定操作
 */
class BindingMap {
    /**
     * 压缩绑定结构
     * 
     * 使用位字段压缩存储绑定信息以节省内存。
     * 这实际上是 GLuint，但我们只需要 8 位。
     */
    struct CompressedBinding {
        uint8_t binding : 7;  // 绑定索引（7 位，最大 127）
        uint8_t sampler : 1;  // 是否为采样器类型（1 位标志）
    };

    /**
     * 存储数组
     * 
     * 二维数组：[描述符集索引][绑定索引] -> CompressedBinding
     * 使用指针数组以避免大对象在栈上分配。
     */
    CompressedBinding (*mStorage)[MAX_DESCRIPTOR_COUNT];

    /**
     * 活动描述符位集合
     * 
     * 每个描述符集对应一个位集合，用于跟踪哪些绑定是活动的。
     * 这允许快速查找活动的绑定，而无需遍历整个数组。
     */
    utils::bitset64 mActiveDescriptors[MAX_DESCRIPTOR_SET_COUNT];

public:
    /**
     * 构造函数
     * 
     * 分配存储数组的内存。
     * 在调试模式下，使用 0xFF 填充内存以便检测未初始化的访问。
     */
    BindingMap() noexcept
            : mStorage(new (std::nothrow) CompressedBinding[MAX_DESCRIPTOR_SET_COUNT][MAX_DESCRIPTOR_COUNT]) {
#ifndef NDEBUG
        // 在调试模式下，用 0xFF 填充内存以便检测未初始化的访问
        memset(mStorage, 0xFF, sizeof(CompressedBinding[MAX_DESCRIPTOR_SET_COUNT][MAX_DESCRIPTOR_COUNT]));
#endif
    }

    /**
     * 析构函数
     * 
     * 释放存储数组的内存。
     */
    ~BindingMap() noexcept {
        delete [] mStorage;
    }

    // 禁止拷贝和移动
    BindingMap(BindingMap const&) noexcept = delete;
    BindingMap(BindingMap&&) noexcept = delete;
    BindingMap& operator=(BindingMap const&) noexcept = delete;
    BindingMap& operator=(BindingMap&&) noexcept = delete;

    /**
     * 绑定条目结构
     * 
     * 表示一个描述符绑定的信息。
     */
    struct Binding {
        GLuint binding;        // 绑定索引（OpenGL 绑定位置）
        DescriptorType type;   // 描述符类型（UNIFORM_BUFFER、SAMPLER_2D 等）
    };

    /**
     * 插入绑定映射
     * 
     * 将描述符绑定信息存储到映射中。
     * 
     * @param set 描述符集索引
     * @param binding 绑定索引
     * @param entry 绑定条目（包含绑定索引和类型）
     * 
     * 执行流程：
     * 1. 验证索引范围
     * 2. 压缩存储绑定信息（7 位 binding + 1 位 sampler 标志）
     * 3. 在活动描述符位集合中设置对应位
     */
    void insert(descriptor_set_t set, descriptor_binding_t binding, Binding entry) noexcept {
        assert_invariant(set < MAX_DESCRIPTOR_SET_COUNT);
        assert_invariant(binding < MAX_DESCRIPTOR_COUNT);
        assert_invariant(entry.binding < 128); // 我们目前为类型保留 1 位，所以 binding 最大为 127
        // 压缩存储：将 binding 和 sampler 标志打包到 8 位中
        mStorage[set][binding] = { uint8_t(entry.binding),
                                   DescriptorSetLayoutBinding::isSampler(entry.type) };
        // 标记此绑定为活动
        mActiveDescriptors[set].set(binding);
    }

    /**
     * 获取绑定索引
     * 
     * 从映射中检索指定描述符集和绑定的 OpenGL 绑定索引。
     * 
     * @param set 描述符集索引
     * @param binding 绑定索引
     * @return OpenGL 绑定索引
     */
    GLuint get(descriptor_set_t set, descriptor_binding_t binding) const noexcept {
        assert_invariant(set < MAX_DESCRIPTOR_SET_COUNT);
        assert_invariant(binding < MAX_DESCRIPTOR_COUNT);
        return mStorage[set][binding].binding;
    }

    /**
     * 获取活动描述符位集合
     * 
     * 返回指定描述符集的活动描述符位集合。
     * 这允许快速查找哪些绑定是活动的，而无需遍历整个数组。
     * 
     * @param set 描述符集索引
     * @return 活动描述符位集合
     */
    utils::bitset64 getActiveDescriptors(descriptor_set_t set) const noexcept {
        return mActiveDescriptors[set];
    }
};

} // namespace filament::backend

#endif //TNT_FILAMENT_BACKEND_OPENGL_BINDINGMAP_H
