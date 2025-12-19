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

#ifndef TNT_SPIRVFIXUP_H
#define TNT_SPIRVFIXUP_H

#include <string>

namespace filamat {

/**
 * Performs a "fixup" operation on SPIR-V disassembly text, decorating the filament_gl_ClipDistance
 * output as the canonical gl_ClipDistance built-in.
 *
 * glslang does not support the EXT_clip_cull_distance extension. Writing directly to
 * gl_ClipDistance results in an error.
 *
 * To get around this, an ES shader should write instead to filament_gl_ClipDistance. After
 * compiling to SPIR-V, this function will modify the SPIR_V disassembly and decorate
 * filament_gl_ClipDistance as if it were gl_ClipDistance.
 *
 * For example, the source GLSL:
 * ~~~~~~~~~~
 * #version 310 es
 *
 * // The location is required but does not matter and will be replaced.
 * layout(location = 100) out float filament_gl_ClipDistance[1];
 *
 * void main() {
 *     filament_gl_ClipDistance[0] = 0.0f;
 * }
 * ~~~~~~~~~~
 *
 * This should only be used in SPIR-V generated for an ES environment.
 *
 * @param spirvDisassembly a reference to the SPIR-V disassembly, will be modified
 * @return true if the replacement was successful, false otherwise
 */
// 修复SPIR-V反汇编文本中的裁剪距离装饰（将filament_gl_ClipDistance装饰为gl_ClipDistance内置变量）
// glslang不支持EXT_clip_cull_distance扩展，直接写入gl_ClipDistance会导致错误
// 为了解决这个问题，ES着色器应该写入filament_gl_ClipDistance，编译为SPIR-V后，此函数会修改SPIR-V反汇编
// 并将filament_gl_ClipDistance装饰为gl_ClipDistance
// 此函数仅应用于为ES环境生成的SPIR-V
// @param spirvDisassembly SPIR-V反汇编文本的引用（将被修改）
// @return 如果替换成功返回true，否则返回false
bool fixupClipDistance(std::string& spirvDisassembly);

}

#endif  // TNT_SPIRVFIXUP_H
