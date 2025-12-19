/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "SpirvFixup.h"

namespace filamat {

// 修复SPIR-V反汇编文本中的裁剪距离装饰（将filament_gl_ClipDistance装饰为gl_ClipDistance内置变量）
// @param spirvDisassembly SPIR-V反汇编文本的引用（将被修改）
// @return 如果替换成功返回true，否则返回false
bool fixupClipDistance(std::string& spirvDisassembly) {
    // 步骤1：查找OpDecorate指令中filament_gl_ClipDistance的Location装饰
    size_t p = spirvDisassembly.find("OpDecorate %filament_gl_ClipDistance Location");
    if (p == std::string::npos) {
        // 如果未找到，返回false
        return false;
    }
    // 步骤2：查找该行的结束位置
    size_t lineEnd = spirvDisassembly.find('\n', p);
    if (lineEnd == std::string::npos) {
        lineEnd = spirvDisassembly.size();
    }
    // 步骤3：将Location装饰替换为BuiltIn ClipDistance装饰
    spirvDisassembly.replace(p, lineEnd - p,
            "OpDecorate %filament_gl_ClipDistance BuiltIn ClipDistance");
    return true;
}

} // namespace filamat
