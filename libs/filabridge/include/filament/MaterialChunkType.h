/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef TNT_FILAMAT_MATERIAL_CHUNK_TYPES_H
#define TNT_FILAMAT_MATERIAL_CHUNK_TYPES_H

#include <stdint.h>

#include <utils/compiler.h>

namespace filamat {

// Pack an eight character string into a 64 bit integer.
constexpr inline uint64_t charTo64bitNum(const char str[9]) noexcept {
    return
        (  (static_cast<uint64_t >(str[0]) << 56))
        | ((static_cast<uint64_t >(str[1]) << 48) & 0x00FF000000000000U)
        | ((static_cast<uint64_t >(str[2]) << 40) & 0x0000FF0000000000U)
        | ((static_cast<uint64_t >(str[3]) << 32) & 0x000000FF00000000U)
        | ((static_cast<uint64_t >(str[4]) << 24) & 0x00000000FF000000U)
        | ((static_cast<uint64_t >(str[5]) << 16) & 0x0000000000FF0000U)
        | ((static_cast<uint64_t >(str[6]) <<  8) & 0x000000000000FF00U)
        | ( static_cast<uint64_t >(str[7])        & 0x00000000000000FFU);
}

enum UTILS_PUBLIC ChunkType : uint64_t {
    Unknown  = charTo64bitNum("UNKNOWN "),  // 未知块类型
    MaterialUib = charTo64bitNum("MAT_UIB "),  // 材质统一缓冲区接口块
    MaterialSib = charTo64bitNum("MAT_SIB "),  // 材质采样器接口块
    MaterialSubpass = charTo64bitNum("MAT_SUB "),  // 材质子通道
    MaterialGlsl = charTo64bitNum("MAT_GLSL"),  // 材质GLSL着色器代码
    MaterialEssl1 = charTo64bitNum("MAT_ESS1"),  // 材质ESSL 1.0着色器代码
    MaterialSpirv = charTo64bitNum("MAT_SPIR"),  // 材质SPIR-V着色器代码
    MaterialMetal = charTo64bitNum("MAT_METL"),  // 材质Metal着色器代码
    MaterialWgsl = charTo64bitNum("MAT_WGSL"),  // 材质WGSL着色器代码
    MaterialMetalLibrary = charTo64bitNum("MAT_MLIB"),  // 材质Metal库
    MaterialShaderModels = charTo64bitNum("MAT_SMDL"),  // 材质着色模型
    MaterialBindingUniformInfo = charTo64bitNum("MAT_UFRM"),  // 材质绑定统一变量信息
    MaterialAttributeInfo = charTo64bitNum("MAT_ATTR"),  // 材质属性信息
    MaterialDescriptorBindingsInfo = charTo64bitNum("MAT_DBDI"),  // 材质描述符绑定信息
    MaterialDescriptorSetLayoutInfo = charTo64bitNum("MAT_DSLI"),  // 材质描述符集布局信息
    MaterialProperties = charTo64bitNum("MAT_PROP"),  // 材质属性
    MaterialConstants = charTo64bitNum("MAT_CONS"),  // 材质常量
    MaterialPushConstants = charTo64bitNum("MAT_PCON"),  // 材质推送常量

    MaterialName = charTo64bitNum("MAT_NAME"),  // 材质名称
    MaterialVersion = charTo64bitNum("MAT_VERS"),  // 材质版本
    MaterialCompilationParameters = charTo64bitNum("MAT_CPRM"),  // 材质编译参数
    MaterialCacheId = charTo64bitNum("MAT_UUID"),  // 材质缓存ID
    MaterialFeatureLevel = charTo64bitNum("MAT_FEAT"),  // 材质功能级别
    MaterialShading = charTo64bitNum("MAT_SHAD"),  // 材质着色模式
    MaterialBlendingMode = charTo64bitNum("MAT_BLEN"),  // 材质混合模式
    MaterialBlendFunction = charTo64bitNum("MAT_BLFN"),  // 材质混合函数
    MaterialTransparencyMode = charTo64bitNum("MAT_TRMD"),  // 材质透明度模式
    MaterialMaskThreshold = charTo64bitNum("MAT_THRS"),  // 材质遮罩阈值
    MaterialShadowMultiplier = charTo64bitNum("MAT_SHML"),  // 材质阴影乘数
    MaterialSpecularAntiAliasing = charTo64bitNum("MAT_SPAA"),  // 材质镜面反射抗锯齿
    MaterialSpecularAntiAliasingVariance = charTo64bitNum("MAT_SVAR"),  // 材质镜面反射抗锯齿方差
    MaterialSpecularAntiAliasingThreshold = charTo64bitNum("MAT_STHR"),  // 材质镜面反射抗锯齿阈值
    MaterialClearCoatIorChange = charTo64bitNum("MAT_CIOR"),  // 材质清漆IOR变化
    MaterialDomain = charTo64bitNum("MAT_DOMN"),  // 材质域
    MaterialVariantFilterMask = charTo64bitNum("MAT_VFLT"),  // 材质变体过滤掩码
    MaterialRefraction = charTo64bitNum("MAT_REFM"),  // 材质折射模式
    MaterialRefractionType = charTo64bitNum("MAT_REFT"),  // 材质折射类型
    MaterialReflectionMode = charTo64bitNum("MAT_REFL"),  // 材质反射模式

    MaterialRequiredAttributes = charTo64bitNum("MAT_REQA"),  // 材质必需属性
    MaterialDoubleSidedSet = charTo64bitNum("MAT_DOSS"),  // 材质双面设置标志
    MaterialDoubleSided = charTo64bitNum("MAT_DOSI"),  // 材质双面标志

    MaterialColorWrite = charTo64bitNum("MAT_CWRIT"),  // 材质颜色写入
    MaterialDepthWriteSet = charTo64bitNum("MAT_DEWS"),  // 材质深度写入设置标志
    MaterialDepthWrite = charTo64bitNum("MAT_DWRIT"),  // 材质深度写入
    MaterialDepthTest = charTo64bitNum("MAT_DTEST"),  // 材质深度测试
    MaterialInstanced = charTo64bitNum("MAT_INSTA"),  // 材质实例化
    MaterialCullingMode = charTo64bitNum("MAT_CUMO"),  // 材质剔除模式
    MaterialAlphaToCoverageSet = charTo64bitNum("MAT_A2CS"),  // 材质Alpha到覆盖设置标志
    MaterialAlphaToCoverage = charTo64bitNum("MAT_A2CO"),  // 材质Alpha到覆盖

    MaterialHasCustomDepthShader =charTo64bitNum("MAT_CSDP"),  // 材质是否有自定义深度着色器

    MaterialVertexDomain = charTo64bitNum("MAT_VEDO"),  // 材质顶点域
    MaterialInterpolation = charTo64bitNum("MAT_INTR"),  // 材质插值类型
    MaterialStereoscopicType = charTo64bitNum("MAT_STER"),  // 材质立体类型

    DictionaryText = charTo64bitNum("DIC_TEXT"),  // 字典文本
    DictionarySpirv = charTo64bitNum("DIC_SPIR"),  // 字典SPIR-V
    DictionaryMetalLibrary = charTo64bitNum("DIC_MLIB"),  // 字典Metal库

    MaterialCrc32 = charTo64bitNum("MAT_CRC "),  // 材质CRC32校验

    MaterialSource = charTo64bitNum("MAT_SRC "),  // 材质源代码
};

} // namespace filamat

#endif // TNT_FILAMAT_MATERIAL_CHUNK_TYPES_H
