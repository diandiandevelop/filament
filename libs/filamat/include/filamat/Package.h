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

#ifndef TNT_FILAMAT_PACKAGE_H
#define TNT_FILAMAT_PACKAGE_H

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>  // memcpy

#include <cstddef>
#include <functional>

#include <utils/compiler.h>

namespace filamat {

// 包类，用于存储材质数据的二进制包
class UTILS_PUBLIC Package {
public:
    Package() = default;

    // Regular constructor
    // 常规构造函数，分配指定大小的内存
    explicit Package(size_t size) : mSize(size) {
        mPayload = new uint8_t[size];
    }

    // 从现有数据构造包
    Package(const void* src, size_t size) : Package(size) {
        memcpy(mPayload, src, size);
    }

    // Move Constructor
    // 移动构造函数
    Package(Package&& other) noexcept : mPayload(other.mPayload), mSize(other.mSize),
            mValid(other.mValid) {
        other.mPayload = nullptr;
        other.mSize = 0;
        other.mValid = false;
    }

    // Move assignment
    // 移动赋值运算符
    Package& operator=(Package&& other) noexcept {
        std::swap(mPayload, other.mPayload);
        std::swap(mSize, other.mSize);
        std::swap(mValid, other.mValid);
        return *this;
    }

    // Copy assignment operator disallowed.
    // 禁止复制赋值运算符
    Package& operator=(const Package& other) = delete;

    // Copy constructor disallowed.
    // 禁止复制构造函数
    Package(const Package& other) = delete;

    ~Package() {
        delete[] mPayload;
    }

    // 获取数据指针
    uint8_t* getData() const noexcept {
        return mPayload;
    }

    // 获取数据大小
    size_t getSize() const noexcept {
        return mSize;
    }

    // 获取数据结束位置的指针
    uint8_t* getEnd() const noexcept {
        return mPayload + mSize;
    }

    // 设置包的有效性标志
    void setValid(bool valid) noexcept {
        mValid = valid;
    }

    // 检查包是否有效
    bool isValid() const noexcept {
        return mValid;
    }

    // 创建无效包（静态工厂方法）
    static Package invalidPackage() {
        Package package(0);
        package.setValid(false);
        return package;
    }

private:
    uint8_t* mPayload = nullptr;  // 数据负载指针
    size_t mSize = 0;              // 数据大小
    bool mValid = true;            // 有效性标志
};

} // namespace filamat
#endif
