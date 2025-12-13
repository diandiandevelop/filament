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

#include "details/Texture.h"

#include "details/Engine.h"
#include "details/Stream.h"

namespace filament {

/**
 * 获取纹理宽度
 * 
 * @param level Mip 级别
 * @return 宽度（像素）
 */
size_t Texture::getWidth(size_t const level) const noexcept {
    return downcast(this)->getWidth(level);
}

/**
 * 获取纹理高度
 * 
 * @param level Mip 级别
 * @return 高度（像素）
 */
size_t Texture::getHeight(size_t const level) const noexcept {
    return downcast(this)->getHeight(level);
}

/**
 * 获取纹理深度
 * 
 * @param level Mip 级别
 * @return 深度（像素）
 */
size_t Texture::getDepth(size_t const level) const noexcept {
    return downcast(this)->getDepth(level);
}

/**
 * 获取 Mip 级别数量
 * 
 * @return Mip 级别数量
 */
size_t Texture::getLevels() const noexcept {
    return downcast(this)->getLevelCount();
}

/**
 * 获取纹理采样器类型
 * 
 * @return 采样器类型枚举值
 */
Texture::Sampler Texture::getTarget() const noexcept {
    return downcast(this)->getTarget();
}

/**
 * 获取纹理内部格式
 * 
 * @return 内部格式枚举值
 */
Texture::InternalFormat Texture::getFormat() const noexcept {
    return downcast(this)->getFormat();
}

/**
 * 设置纹理图像数据（3D 版本）
 * 
 * @param engine 引擎引用
 * @param level Mip 级别
 * @param xoffset X 偏移量
 * @param yoffset Y 偏移量
 * @param zoffset Z 偏移量
 * @param width 宽度
 * @param height 高度
 * @param depth 深度
 * @param buffer 像素缓冲区描述符（移动语义）
 */
void Texture::setImage(Engine& engine, size_t const level,
        uint32_t const xoffset, uint32_t const yoffset, uint32_t const zoffset,
        uint32_t const width, uint32_t const height, uint32_t const depth,
        PixelBufferDescriptor&& buffer) const {
    downcast(this)->setImage(downcast(engine),
            level, xoffset, yoffset, zoffset, width, height, depth, std::move(buffer));
}

/**
 * 设置纹理图像数据（立方体贴图版本）
 * 
 * @param engine 引擎引用
 * @param level Mip 级别
 * @param buffer 像素缓冲区描述符（移动语义）
 * @param faceOffsets 立方体贴图各面的偏移量
 */
void Texture::setImage(Engine& engine, size_t const level,
        PixelBufferDescriptor&& buffer, const FaceOffsets& faceOffsets) const {
    downcast(this)->setImage(downcast(engine), level, std::move(buffer), faceOffsets);
}

/**
 * 设置外部图像（ExternalImageHandleRef 版本）
 * 
 * @param engine 引擎引用
 * @param image 外部图像句柄引用
 */
void Texture::setExternalImage(Engine& engine, ExternalImageHandleRef image) noexcept {
    downcast(this)->setExternalImage(downcast(engine), image);
}

/**
 * 设置外部图像（void* 版本）
 * 
 * @param engine 引擎引用
 * @param image 外部图像指针
 */
void Texture::setExternalImage(Engine& engine, void* image) noexcept {
    downcast(this)->setExternalImage(downcast(engine), image);
}

/**
 * 设置外部图像（多平面版本）
 * 
 * @param engine 引擎引用
 * @param image 外部图像指针
 * @param plane 平面索引
 */
void Texture::setExternalImage(Engine& engine, void* image, size_t const plane) noexcept {
    downcast(this)->setExternalImage(downcast(engine), image, plane);
}

/**
 * 设置外部流
 * 
 * @param engine 引擎引用
 * @param stream 流指针
 */
void Texture::setExternalStream(Engine& engine, Stream* stream) noexcept {
    downcast(this)->setExternalStream(downcast(engine), downcast(stream));
}

/**
 * 生成 Mipmap
 * 
 * @param engine 引擎引用
 */
void Texture::generateMipmaps(Engine& engine) const noexcept {
    downcast(this)->generateMipmaps(downcast(engine));
}

/**
 * 检查纹理格式是否受支持
 * 
 * @param engine 引擎引用
 * @param format 内部格式
 * @return 如果支持则返回 true
 */
bool Texture::isTextureFormatSupported(Engine& engine, InternalFormat const format) noexcept {
    return FTexture::isTextureFormatSupported(downcast(engine), format);
}

/**
 * 检查纹理格式是否支持 Mipmap
 * 
 * @param engine 引擎引用
 * @param format 内部格式
 * @return 如果支持 Mipmap 则返回 true
 */
bool Texture::isTextureFormatMipmappable(Engine& engine, InternalFormat const format) noexcept {
    return FTexture::isTextureFormatMipmappable(downcast(engine), format);
}

/**
 * 检查纹理格式是否为压缩格式
 * 
 * @param format 内部格式
 * @return 如果是压缩格式则返回 true
 */
bool Texture::isTextureFormatCompressed(InternalFormat const format) noexcept {
    return FTexture::isTextureFormatCompressed(format);
}

/**
 * 检查是否支持受保护纹理
 * 
 * @param engine 引擎引用
 * @return 如果支持则返回 true
 */
bool Texture::isProtectedTexturesSupported(Engine& engine) noexcept {
    return FTexture::isProtectedTexturesSupported(downcast(engine));
}

/**
 * 检查是否支持纹理通道重排
 * 
 * @param engine 引擎引用
 * @return 如果支持则返回 true
 */
bool Texture::isTextureSwizzleSupported(Engine& engine) noexcept {
    return FTexture::isTextureSwizzleSupported(downcast(engine));
}

/**
 * 计算纹理数据大小
 * 
 * @param format 像素格式
 * @param type 数据类型
 * @param stride 行跨度（字节）
 * @param height 高度（像素）
 * @param alignment 对齐方式（字节）
 * @return 数据大小（字节）
 */
size_t Texture::computeTextureDataSize(Format const format, Type const type, size_t const stride,
        size_t const height, size_t const alignment) noexcept {
    return FTexture::computeTextureDataSize(format, type, stride, height, alignment);
}

/**
 * 验证像素格式和类型组合
 * 
 * @param internalFormat 内部格式
 * @param format 像素格式
 * @param type 数据类型
 * @return 如果组合有效则返回 true
 */
bool Texture::validatePixelFormatAndType(InternalFormat internalFormat, Format format, Type type) noexcept {
    return FTexture::validatePixelFormatAndType(internalFormat, format, type);
}

/**
 * 获取最大纹理尺寸
 * 
 * @param engine 引擎引用
 * @param type 采样器类型
 * @return 最大尺寸（像素）
 */
size_t Texture::getMaxTextureSize(Engine& engine, Sampler type) noexcept {
    return FTexture::getMaxTextureSize(downcast(engine), type);

}
/**
 * 获取最大数组纹理层数
 * 
 * @param engine 引擎引用
 * @return 最大层数
 */
size_t Texture::getMaxArrayTextureLayers(Engine& engine) noexcept {
    return FTexture::getMaxArrayTextureLayers(downcast(engine));
}

} // namespace filament
