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

#include "SimpleFieldChunk.h"

namespace filamat {

// uint8_t类型的模板特化：写入uint8_t值
template<>
void SimpleFieldChunk<uint8_t>::flatten(Flattener &f) {
    f.writeUint8(t);
}

// uint32_t类型的模板特化：写入uint32_t值
template<>
 void SimpleFieldChunk<uint32_t>::flatten(Flattener &f) {
    f.writeUint32(t);
}

// uint64_t类型的模板特化：写入uint64_t值
template<>
 void SimpleFieldChunk<uint64_t>::flatten(Flattener &f) {
    f.writeUint64(t);
}

// bool类型的模板特化：写入bool值
template<>
void SimpleFieldChunk<bool>::flatten(Flattener &f) {
    f.writeBool(t);
}

// const char*类型的模板特化：写入字符串
template<>
 void SimpleFieldChunk<const char*>::flatten(Flattener &f) {
    f.writeString(t);
}

// float类型的模板特化：将float按位解释为uint32_t写入
template<>
void SimpleFieldChunk<float>::flatten(Flattener &f) {
    f.writeUint32(reinterpret_cast<uint32_t&>(t));
}

} // namespace filamat
