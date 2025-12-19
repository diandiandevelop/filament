/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "BlobDictionary.h"

#include <assert.h>

namespace filamat {

// 添加blob到字典中（如果已存在则返回现有索引，否则添加新blob并返回新索引）
// @param vblob 要添加的blob数据（uint8_t向量）
// @return blob在字典中的索引
size_t BlobDictionary::addBlob(const std::vector<uint8_t>& vblob) noexcept {
    // 将uint8_t向量转换为string_view
    std::string_view blob((char*) vblob.data(), vblob.size());
    // 检查blob是否已存在于字典中
    auto iter = mBlobIndices.find(blob);
    if (iter != mBlobIndices.end()) {
        // 已存在，返回现有索引
        return iter->second;
    }
    // 不存在，创建新的字符串并添加到字典
    mBlobs.emplace_back(std::make_unique<std::string>(blob));
    // 将新blob添加到索引映射表
    mBlobIndices.emplace(*mBlobs.back(), mBlobs.size() - 1);
    // 返回新blob的索引
    return mBlobs.size() - 1;
}

} // namespace filamat
