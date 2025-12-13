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

#include <backend/Handle.h>

#include <utils/compiler.h>
#include <utils/CallStack.h>
#include <utils/ostream.h>

#ifndef NDEBUG
#   include <utils/CString.h>
#   include <string_view>
#endif

#include <stddef.h>

using namespace utils;

namespace filament::backend {

#ifndef NDEBUG

/**
 * 命名空间前缀
 * 
 * 用于从类型名称中移除命名空间前缀，使输出更简洁。
 */
static char const * const kOurNamespace = "filament::backend::";

/**
 * 从字符串中移除所有指定子串
 * 
 * 用于清理类型名称，移除命名空间前缀。
 * 
 * @param str 要处理的字符串
 * @param what 要移除的子串
 * @return 处理后的字符串引用
 */
// 从字符串中移除所有 "what" 的出现
UTILS_NOINLINE
static CString& removeAll(CString& str, const std::string_view what) noexcept {
    if (!what.empty()) {
        const CString empty;
        size_t pos = 0;
        while ((pos = std::string_view{ str.data(), str.size() }.find(what, pos)) != std::string_view::npos) {
            str.replace(pos, what.length(), empty);
        }
    }
    return str;
}

/**
 * 记录句柄到输出流（辅助函数）
 * 
 * 将句柄类型名称和 ID 输出到流中。
 * 
 * @tparam T 句柄类型
 * @param out 输出流
 * @param typeName 类型名称（会被修改，移除命名空间前缀）
 * @param id 句柄 ID
 * @return 输出流引用
 */
template <typename T>
UTILS_NOINLINE
static io::ostream& logHandle(io::ostream& out, CString& typeName, T id) noexcept {
    return out << removeAll(typeName, kOurNamespace) << " @ " << id;
}

/**
 * 句柄输出流操作符
 * 
 * 将句柄输出到流中，格式为：类型名 @ ID
 * 
 * @tparam T 资源类型
 * @param out 输出流
 * @param h 句柄
 * @return 输出流引用
 */
template <typename T>
io::ostream& operator<<(io::ostream& out, const Handle<T>& h) noexcept {
    CString s{ CallStack::typeName<Handle<T>>() };
    return logHandle(out, s, h.getId());
}

/**
 * 显式实例化流操作符（避免内联）
 * 
 * 为所有硬件资源类型显式实例化流操作符，确保它们不会被内联，
 * 这样可以减少代码大小并提高调试体验。
 */
template io::ostream& operator<<(io::ostream& out, const Handle<HwVertexBuffer>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwIndexBuffer>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwRenderPrimitive>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwProgram>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwTexture>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwRenderTarget>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwFence>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwSwapChain>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwStream>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwTimerQuery>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwBufferObject>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwDescriptorSet>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwDescriptorSetLayout>& h) noexcept;
template io::ostream& operator<<(io::ostream& out, const Handle<HwVertexBufferInfo>& h) noexcept;

#endif

} // namespace filament::backend
