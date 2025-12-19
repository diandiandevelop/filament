/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef TNT_FILAMENT_SUBPASSINFO_H
#define TNT_FILAMENT_SUBPASSINFO_H

#include <backend/DriverEnums.h>

#include <utils/CString.h>

namespace filament {

using Type = backend::SubpassType;          // 子通道类型别名
using Format = backend::SamplerFormat;      // 采样器格式别名
using Precision = backend::Precision;       // 精度别名

/**
 * 子通道信息结构
 * 描述渲染子通道（subpass）的信息
 */
struct SubpassInfo {
    // 默认构造函数
    SubpassInfo() = default;
    /**
     * 构造函数
     * @param block 子通道所属的块名称
     * @param name 子通道名称
     * @param type 子通道类型
     * @param format 子通道格式
     * @param precision 精度
     * @param attachmentIndex 附件索引
     * @param binding 绑定索引
     */
    SubpassInfo(utils::CString block, utils::CString name, Type type, Format format,
            Precision precision, uint8_t attachmentIndex, uint8_t binding) noexcept
            : block(std::move(block)), name(std::move(name)), type(type), format(format),
            precision(precision), attachmentIndex(attachmentIndex), binding(binding),
            isValid(true) {
    }
    // name of the block this subpass belongs to - 此子通道所属的块名称
    utils::CString block = utils::CString("MaterialParams");
    utils::CString name;    // name of this subpass - 子通道名称
    Type type;              // type of this subpass - 子通道类型
    Format format;          // format of this subpass - 子通道格式
    Precision precision;    // precision of this subpass - 子通道精度
    uint8_t attachmentIndex = 0;    // 附件索引
    uint8_t binding = 0;            // 绑定索引
    bool isValid = false;           // 是否有效
};

} // namespace filament

#endif // TNT_FILAMENT_SUBPASSINFO_H
