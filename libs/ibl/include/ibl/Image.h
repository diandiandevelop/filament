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

#ifndef IBL_IMAGE_H
#define IBL_IMAGE_H

#include <math/scalar.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/compiler.h>

#include <memory>

namespace filament {
namespace ibl {

/**
 * Image - 图像类
 * 
 * 用于存储和处理2D图像数据，每个像素为float3格式（RGB）。
 * 支持自定义步长（stride），可以引用外部数据或拥有自己的数据。
 */
class UTILS_PUBLIC Image {
public:
    /**
     * 默认构造函数
     * 创建一个空的图像对象
     */
    Image();
    
    /**
     * 构造函数
     * 
     * @param w 图像宽度
     * @param h 图像高度
     * @param stride 行步长（字节数），如果为0则使用宽度
     */
    Image(size_t w, size_t h, size_t stride = 0);

    /**
     * 重置图像
     * 
     * 释放所有资源，将图像重置为空状态
     */
    void reset();

    /**
     * 设置图像数据
     * 
     * 将当前图像设置为引用另一个图像的数据（不复制数据）
     * 
     * @param image 源图像
     */
    void set(Image const& image);

    /**
     * 创建子图像
     * 
     * 创建一个引用源图像子区域的新图像（不复制数据）
     * 
     * @param image 源图像
     * @param x 子图像左上角X坐标
     * @param y 子图像左上角Y坐标
     * @param w 子图像宽度
     * @param h 子图像高度
     */
    void subset(Image const& image, size_t x, size_t y, size_t w, size_t h);

    /**
     * 检查图像是否有效
     * 
     * @return true如果图像数据有效，false否则
     */
    bool isValid() const { return mData != nullptr; }

    /**
     * 获取图像宽度
     * 
     * @return 图像宽度（像素数）
     */
    size_t getWidth() const { return mWidth; }

    /**
     * 获取行步长
     * 
     * @return 行步长（像素数）
     */
    size_t getStride() const { return mBpr / getBytesPerPixel(); }

    /**
     * 获取图像高度
     * 
     * @return 图像高度（像素数）
     */
    size_t getHeight() const { return mHeight; }

    /**
     * 获取每行字节数
     * 
     * @return 每行字节数
     */
    size_t getBytesPerRow() const { return mBpr; }

    /**
     * 获取每个像素的字节数
     * 
     * @return 每个像素的字节数（float3 = 12字节）
     */
    size_t getBytesPerPixel() const { return sizeof(math::float3); }

    /**
     * 获取图像数据指针
     * 
     * @return 图像数据指针
     */
    void* getData() const { return mData; }

    /**
     * 获取图像总大小
     * 
     * @return 图像总大小（字节数）
     */
    size_t getSize() const { return mBpr * mHeight; }

    /**
     * 获取指定像素的指针
     * 
     * @param x 像素X坐标
     * @param y 像素Y坐标
     * @return 像素数据指针
     */
    void* getPixelRef(size_t x, size_t y) const;

    /**
     * 分离图像数据所有权
     * 
     * 将图像数据的所有权转移给调用者，图像对象变为空
     * 
     * @return 图像数据的unique_ptr
     */
    std::unique_ptr<uint8_t[]> detach() { return std::move(mOwnedData); }

private:
    size_t mBpr = 0;                      // 每行字节数（Bytes Per Row）
    size_t mWidth = 0;                     // 图像宽度
    size_t mHeight = 0;                    // 图像高度
    std::unique_ptr<uint8_t[]> mOwnedData; // 拥有的数据（如果图像拥有数据）
    void* mData = nullptr;                 // 数据指针（可能指向mOwnedData或外部数据）
};

/**
 * 获取指定像素的指针实现（内联函数）
 * 
 * 执行步骤：
 * 1. 计算像素在内存中的偏移量：y * 每行字节数 + x * 每像素字节数
 * 2. 返回像素数据指针
 * 
 * @param x 像素X坐标
 * @param y 像素Y坐标
 * @return 像素数据指针
 */
inline void* Image::getPixelRef(size_t x, size_t y) const {
    return static_cast<uint8_t*>(mData) + y * getBytesPerRow() + x * getBytesPerPixel();
}

} // namespace ibl
} // namespace filament

#endif /* IBL_IMAGE_H */
