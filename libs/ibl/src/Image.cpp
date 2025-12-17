/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <ibl/Image.h>

#include <utility>

namespace filament {
namespace ibl {

/**
 * 默认构造函数实现
 * 
 * 创建一个空的图像对象，所有成员变量使用默认值
 */
Image::Image() = default;

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 计算每行字节数（如果stride为0则使用宽度）
 * 2. 设置宽度和高度
 * 3. 分配内存并设置数据指针
 * 
 * @param w 图像宽度
 * @param h 图像高度
 * @param stride 行步长（像素数），如果为0则使用宽度
 */
Image::Image(size_t w, size_t h, size_t stride)
        : mBpr((stride ? stride : w) * sizeof(math::float3)),  // 每行字节数
          mWidth(w),                                            // 宽度
          mHeight(h),                                           // 高度
          mOwnedData(new uint8_t[mBpr * h]),                    // 分配内存
          mData(mOwnedData.get()) {                             // 设置数据指针
}

/**
 * 重置图像实现
 * 
 * 执行步骤：
 * 1. 释放拥有的数据
 * 2. 重置所有成员变量为0或nullptr
 */
void Image::reset() {
    mOwnedData.release();  // 释放拥有的数据
    mWidth = 0;
    mHeight = 0;
    mBpr = 0;
    mData = nullptr;
}

/**
 * 设置图像数据实现
 * 
 * 执行步骤：
 * 1. 释放当前拥有的数据
 * 2. 复制源图像的尺寸和步长信息
 * 3. 设置数据指针指向源图像的数据（不复制数据，只是引用）
 * 
 * @param image 源图像
 */
void Image::set(Image const& image) {
    mOwnedData.release();  // 释放当前拥有的数据
    mWidth = image.mWidth;
    mHeight = image.mHeight;
    mBpr = image.mBpr;
    mData = image.mData;  // 引用源图像的数据
}

/**
 * 创建子图像实现
 * 
 * 执行步骤：
 * 1. 释放当前拥有的数据
 * 2. 设置子图像的尺寸
 * 3. 使用源图像的步长
 * 4. 设置数据指针指向源图像的子区域（不复制数据，只是引用）
 * 
 * @param image 源图像
 * @param x 子图像左上角X坐标
 * @param y 子图像左上角Y坐标
 * @param w 子图像宽度
 * @param h 子图像高度
 */
void Image::subset(Image const& image, size_t x, size_t y, size_t w, size_t h) {
    mOwnedData.release();  // 释放当前拥有的数据
    mWidth = w;            // 设置子图像宽度
    mHeight = h;           // 设置子图像高度
    mBpr = image.mBpr;     // 使用源图像的步长
    mData = static_cast<uint8_t*>(image.getPixelRef(x, y));  // 指向源图像的子区域
}

} // namespace ibl
} // namespace filament

