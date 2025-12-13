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

#include "DFG.h"

#include "ZstdHelper.h"

#include "details/Engine.h"
#include "details/Texture.h"

#include <filament/Texture.h>

#include <backend/DriverEnums.h>

#include <utils/debug.h>
#include <utils/compiler.h>
#include <utils/Panic.h>

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <utility>

#include "generated/resources/dfg.h"

namespace filament {

/**
 * 初始化 DFG LUT
 * 
 * 创建并填充 DFG 查找表纹理。
 * 支持 Zstd 压缩的 LUT 数据，如果数据被压缩则先解压。
 * 
 * @param engine 引擎引用
 */
void DFG::init(FEngine& engine) {
    /**
     * 计算 LUT 数据大小
     * 
     * LUT 是 DFG_LUT_SIZE x DFG_LUT_SIZE 的 RGB 纹理，每个通道是 16 位浮点数。
     */
    constexpr size_t fp16Count = DFG_LUT_SIZE * DFG_LUT_SIZE * 3;
    constexpr size_t byteCount = fp16Count * sizeof(uint16_t);

    /**
     * 创建纹理构建器
     */
    Texture::Builder builder = Texture::Builder()
            .width(DFG_LUT_SIZE)
            .height(DFG_LUT_SIZE)
            .format(backend::TextureFormat::RGB16F);

    /**
     * 检查 LUT 数据是否被 Zstd 压缩
     */
    if (ZstdHelper::isCompressed(DFG_PACKAGE, DFG_DFG_SIZE)) {
        const size_t decodedSize = ZstdHelper::getDecodedSize(DFG_PACKAGE, byteCount);
        assert_invariant(decodedSize == byteCount);

        /**
         * 如果 LUT 被 Zstd 压缩，解压它
         */
        void* decodedData = malloc(decodedSize);

        FILAMENT_CHECK_POSTCONDITION(decodedData)
                << "Couldn't allocate " << decodedSize << " bytes for DFG LUT decompression.";

        if (UTILS_LIKELY(decodedData)) {
            bool const success = ZstdHelper::decompress(
                    decodedData, decodedSize, DFG_PACKAGE, byteCount);

            FILAMENT_CHECK_POSTCONDITION(success) << "Couldn't decompress DFG LUT.";

            /**
             * 使用解压后的数据创建纹理
             * 
             * 注意：setImage 的回调会在数据上传后释放缓冲区。
             */
            Texture* lut = builder.build(engine);
            lut->setImage(engine, 0, { decodedData, decodedSize,
                    Texture::Format::RGB, Texture::Type::HALF,
                    +[](void* buffer, size_t, void*) { free(buffer); } });
            mLUT = downcast(lut);
            return;
        }
    }

    /**
     * LUT 数据未压缩，直接使用
     */
    assert_invariant(DFG_DFG_SIZE == byteCount);
    Texture* lut = builder.build(engine);
    lut->setImage(engine, 0, { DFG_PACKAGE, byteCount, Texture::Format::RGB, Texture::Type::HALF });
    mLUT = downcast(lut);
}

/**
 * 终止 DFG LUT
 * 
 * 释放 DFG LUT 纹理资源。
 * 
 * @param engine 引擎引用
 */
void DFG::terminate(FEngine& engine) noexcept {
    if (mLUT) {
        engine.destroy(mLUT);
    }
}

} // namespace filament
