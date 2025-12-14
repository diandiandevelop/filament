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

//! \file

#ifndef TNT_FILAMENT_BACKEND_PIXELBUFFERDESCRIPTOR_H
#define TNT_FILAMENT_BACKEND_PIXELBUFFERDESCRIPTOR_H

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>

#include <utils/compiler.h>
#include <utils/debug.h>

#include <stddef.h>
#include <stdint.h>

namespace utils::io {
class ostream;
} // namespace utils::io

namespace filament::backend {

/**
 * A descriptor to an image in main memory, typically used to transfer image data from the CPU
 * to the GPU.
 *
 * A PixelBufferDescriptor owns the memory buffer it references, therefore PixelBufferDescriptor
 * cannot be copied, but can be moved.
 *
 * PixelBufferDescriptor releases ownership of the memory-buffer when it's destroyed.
 */
/**
 * 像素缓冲区描述符
 * 
 * 用于主内存中图像数据的描述符，通常用于将图像数据从 CPU 传输到 GPU。
 * 
 * 特性：
 * - PixelBufferDescriptor 拥有其引用的内存缓冲区
 * - 不支持拷贝，但支持移动语义
 * - 析构时通过回调释放缓冲区所有权
 * 
 * 用途：
 * - 上传纹理数据到 GPU
 * - 上传压缩纹理数据
 * - 支持部分图像更新（通过 left, top, stride 参数）
 * 
 * 继承关系：
 * - 继承自 BufferDescriptor
 * - 添加了像素格式、类型、对齐等图像特定参数
 * 
 * 支持的图像类型：
 * - 未压缩图像（RGBA、RGB 等）
 * - 压缩图像（ETC2、ASTC、DXT 等）
 */
class UTILS_PUBLIC PixelBufferDescriptor : public BufferDescriptor {
public:
    /**
     * 像素数据格式类型别名
     * 
     * 定义像素的颜色通道组成（R、RG、RGB、RGBA 等）。
     */
    using PixelDataFormat = backend::PixelDataFormat;
    
    /**
     * 像素数据类型类型别名
     * 
     * 定义像素数据的存储类型（UBYTE、FLOAT、COMPRESSED 等）。
     */
    using PixelDataType = backend::PixelDataType;

    /**
     * 默认构造函数
     * 
     * 创建一个空的像素缓冲区描述符。
     */
    PixelBufferDescriptor() = default;

    /**
     * Creates a new PixelBufferDescriptor referencing an image in main memory
     *
     * @param buffer    Virtual address of the buffer containing the image
     * @param size      Size in bytes of the buffer containing the image
     * @param format    Format of the image pixels
     * @param type      Type of the image pixels
     * @param alignment Alignment in bytes of pixel rows
     * @param left      Left coordinate in pixels
     * @param top       Top coordinate in pixels
     * @param stride    Stride of a row in pixels
     * @param handler   Handler to dispatch the callback or nullptr for the default handler
     * @param callback  A callback used to release the CPU buffer
     * @param user      An opaque user pointer passed to the callback function when it's called
     */
    /**
     * 构造函数：创建引用主内存中图像的描述符（完整参数版本）
     * 
     * 创建一个 PixelBufferDescriptor，引用指定的图像数据，支持部分图像更新。
     * 
     * @param buffer    包含图像数据的缓冲区虚拟地址
     *                  - 必须是有效的内存地址
     *                  - 缓冲区在回调调用前必须保持有效
     * 
     * @param size      包含图像数据的缓冲区大小（字节）
     * 
     * @param format    图像像素格式（R、RG、RGB、RGBA 等）
     * 
     * @param type      图像像素数据类型（UBYTE、FLOAT、HALF 等）
     * 
     * @param alignment 像素行的字节对齐
     *                  - 通常为 1、4 或 8
     *                  - 用于优化内存访问
     * 
     * @param left      图像在缓冲区中的左坐标（像素）
     *                  - 用于部分图像更新
     *                  - 0 表示从图像左上角开始
     * 
     * @param top       图像在缓冲区中的顶坐标（像素）
     *                  - 用于部分图像更新
     *                  - 0 表示从图像左上角开始
     * 
     * @param stride    行的跨距（像素）
     *                  - 0 表示使用默认跨距（根据宽度和格式计算）
     *                  - 非零值用于指定自定义行跨距
     * 
     * @param handler   回调处理器（可选），nullptr 表示使用默认处理器
     * 
     * @param callback  用于释放 CPU 缓冲区的回调函数
     * 
     * @param user      传递给回调函数的不透明用户指针（可选）
     * 
     * 使用场景：
     * - 上传完整纹理
     * - 更新纹理的某个区域
     * - 需要自定义行对齐或跨距的情况
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment,
            uint32_t left, uint32_t top, uint32_t stride,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              left(left), top(top), stride(stride),
              format(format), type(type), alignment(alignment) {
    }

    /**
     * 构造函数：创建引用主内存中图像的描述符（简化版本，使用默认处理器）
     * 
     * 创建一个 PixelBufferDescriptor，使用默认回调处理器。
     * 
     * @param buffer    包含图像数据的缓冲区虚拟地址
     * @param size      包含图像数据的缓冲区大小（字节）
     * @param format    图像像素格式
     * @param type      图像像素数据类型
     * @param alignment 像素行的字节对齐（默认 1）
     * @param left      图像在缓冲区中的左坐标（像素，默认 0）
     * @param top       图像在缓冲区中的顶坐标（像素，默认 0）
     * @param stride    行的跨距（像素，默认 0 表示自动计算）
     * @param callback  用于释放 CPU 缓冲区的回调函数（可选）
     * @param user      传递给回调函数的不透明用户指针（可选）
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment = 1,
            uint32_t left = 0, uint32_t top = 0, uint32_t stride = 0,
            Callback callback = nullptr, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              left(left), top(top), stride(stride),
              format(format), type(type), alignment(alignment) {
    }

    /**
     * Creates a new PixelBufferDescriptor referencing an image in main memory
     *
     * @param buffer    Virtual address of the buffer containing the image
     * @param size      Size in bytes of the buffer containing the image
     * @param format    Format of the image pixels
     * @param type      Type of the image pixels
     * @param handler   Handler to dispatch the callback or nullptr for the default handler
     * @param callback  A callback used to release the CPU buffer
     * @param user      An opaque user pointer passed to the callback function when it's called
     */
    /**
     * 构造函数：创建引用主内存中图像的描述符（最简版本，带自定义处理器）
     * 
     * 创建一个 PixelBufferDescriptor，使用默认的对齐和坐标设置。
     * 
     * @param buffer    包含图像数据的缓冲区虚拟地址
     * @param size      包含图像数据的缓冲区大小（字节）
     * @param format    图像像素格式
     * @param type      图像像素数据类型
     * @param handler   回调处理器（可选），nullptr 表示使用默认处理器
     * @param callback  用于释放 CPU 缓冲区的回调函数
     * @param user      传递给回调函数的不透明用户指针（可选）
     * 
     * 默认设置：
     * - alignment = 1（无特殊对齐要求）
     * - left = 0, top = 0（从图像左上角开始）
     * - stride = 0（自动计算行跨距）
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              stride(0), format(format), type(type), alignment(1) {
    }

    /**
     * 构造函数：创建引用主内存中图像的描述符（最简版本，使用默认处理器）
     * 
     * 创建一个 PixelBufferDescriptor，使用默认的对齐、坐标和处理器设置。
     * 
     * @param buffer    包含图像数据的缓冲区虚拟地址
     * @param size      包含图像数据的缓冲区大小（字节）
     * @param format    图像像素格式
     * @param type      图像像素数据类型
     * @param callback  用于释放 CPU 缓冲区的回调函数（可选）
     * @param user      传递给回调函数的不透明用户指针（可选）
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type,
            Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              stride(0), format(format), type(type), alignment(1) {
    }


    /**
     * Creates a new PixelBufferDescriptor referencing a compressed image in main memory
     *
     * @param buffer    Virtual address of the buffer containing the image
     * @param size      Size in bytes of the buffer containing the image
     * @param format    Compressed format of the image
     * @param imageSize Compressed size of the image
     * @param handler   Handler to dispatch the callback or nullptr for the default handler
     * @param callback  A callback used to release the CPU buffer
     * @param user      An opaque user pointer passed to the callback function when it's called
     */
    /**
     * 构造函数：创建引用压缩图像的描述符（带自定义处理器）
     * 
     * 创建一个 PixelBufferDescriptor，用于上传压缩纹理数据（ETC2、ASTC、DXT 等）。
     * 
     * @param buffer      包含压缩图像数据的缓冲区虚拟地址
     * @param size        包含压缩图像数据的缓冲区大小（字节）
     * @param format      压缩图像格式（ETC2_RGB8、ASTC_4x4 等）
     * @param imageSize   压缩图像的大小（字节）
     *                    - 用于验证数据完整性
     *                    - 必须与实际压缩数据大小匹配
     * @param handler     回调处理器（可选），nullptr 表示使用默认处理器
     * @param callback    用于释放 CPU 缓冲区的回调函数
     * @param user        传递给回调函数的不透明用户指针（可选）
     * 
     * 压缩格式支持：
     * - ETC2/EAC（Android 标准）
     * - ASTC（移动设备常用）
     * - DXT/S3TC（桌面平台）
     * - BPTC/BC6H/BC7（高质量压缩）
     * 
     * 注意：
     * - type 自动设置为 PixelDataType::COMPRESSED
     * - alignment 固定为 1
     * - 不支持部分更新（left、top、stride 不适用）
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            backend::CompressedPixelDataType format, uint32_t imageSize,
            CallbackHandler* handler, Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, handler, callback, user),
              imageSize(imageSize), compressedFormat(format), type(PixelDataType::COMPRESSED),
              alignment(1) {
    }

    /**
     * 构造函数：创建引用压缩图像的描述符（使用默认处理器）
     * 
     * 创建一个 PixelBufferDescriptor，用于上传压缩纹理数据，使用默认回调处理器。
     * 
     * @param buffer      包含压缩图像数据的缓冲区虚拟地址
     * @param size        包含压缩图像数据的缓冲区大小（字节）
     * @param format      压缩图像格式
     * @param imageSize   压缩图像的大小（字节）
     * @param callback    用于释放 CPU 缓冲区的回调函数（可选）
     * @param user        传递给回调函数的不透明用户指针（可选）
     */
    PixelBufferDescriptor(void const* buffer, size_t size,
            backend::CompressedPixelDataType format, uint32_t imageSize,
            Callback callback, void* user = nullptr) noexcept
            : BufferDescriptor(buffer, size, callback, user),
              imageSize(imageSize), compressedFormat(format), type(PixelDataType::COMPRESSED),
              alignment(1) {
    }

    // --------------------------------------------------------------------------------------------

    template<typename T, void(T::*method)(void const*, size_t)>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment,
            uint32_t left, uint32_t top, uint32_t stride, T* data,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, type, alignment, left, top, stride,
                handler, [](void* b, size_t s, void* u) {
                    (static_cast<T*>(u)->*method)(b, s); }, data };
    }

    template<typename T, void(T::*method)(void const*, size_t)>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, T* data,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, type, handler, [](void* b, size_t s, void* u) {
                    (static_cast<T*>(u)->*method)(b, s); }, data };
    }

    template<typename T, void(T::*method)(void const*, size_t)>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            backend::CompressedPixelDataType format, uint32_t imageSize, T* data,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, imageSize, handler, [](void* b, size_t s, void* u) {
                    (static_cast<T*>(u)->*method)(b, s); }, data
        };
    }

    template<typename T>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, uint8_t alignment,
            uint32_t left, uint32_t top, uint32_t stride, T&& functor,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, type, alignment, left, top, stride,
                handler, [](void* b, size_t s, void* u) {
                    T* const that = static_cast<T*>(u);
                    that->operator()(b, s);
                    delete that;
                }, new T(std::forward<T>(functor))
        };
    }

    template<typename T>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            PixelDataFormat format, PixelDataType type, T&& functor,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, type,
                 handler, [](void* b, size_t s, void* u) {
                    T* const that = static_cast<T*>(u);
                    that->operator()(b, s);
                    delete that;
                }, new T(std::forward<T>(functor))
        };
    }

    template<typename T>
    static PixelBufferDescriptor make(void const* buffer, size_t size,
            backend::CompressedPixelDataType format, uint32_t imageSize, T&& functor,
            CallbackHandler* handler = nullptr) noexcept {
        return { buffer, size, format, imageSize,
                 handler, [](void* b, size_t s, void* u) {
                    T* const that = static_cast<T*>(u);
                    that->operator()(b, s);
                    delete that;
                }, new T(std::forward<T>(functor))
        };
    }
    /**
     * Computes the size in bytes for a pixel of given dimensions and format
     *
     * @param format    Format of the image pixels
     * @param type      Type of the image pixels
     * @return The size of the specified pixel in bytes
     */
    /**
     * 计算指定格式和类型的像素大小（字节）
     * 
     * 根据像素格式和数据类型计算单个像素占用的字节数。
     * 
     * @param format 图像像素格式（R、RG、RGB、RGBA 等）
     * @param type   图像像素数据类型（UBYTE、FLOAT、HALF 等）
     * @return       指定像素的大小（字节）
     *               - 压缩格式返回 0（压缩格式的像素大小不固定）
     * 
     * 计算逻辑：
     * 1. 确定通道数（根据 format：R=1, RG=2, RGB=3, RGBA=4）
     * 2. 确定每通道字节数（根据 type：UBYTE/BYTE=1, USHORT/SHORT/HALF=2, UINT/INT/FLOAT=4）
     * 3. 处理特殊情况（如 UINT_10F_11F_11F_REV、UINT_2_10_10_10_REV 等打包格式）
     * 
     * 使用场景：
     * - 计算纹理上传所需的内存大小
     * - 验证缓冲区大小是否正确
     * - 计算行跨距
     */
    static constexpr size_t computePixelSize(PixelDataFormat format, PixelDataType type) noexcept {
        if (type == PixelDataType::COMPRESSED) {
            return 0;
        }

        size_t n = 0;
        switch (format) {
            case PixelDataFormat::R:
            case PixelDataFormat::R_INTEGER:
            case PixelDataFormat::DEPTH_COMPONENT:
            case PixelDataFormat::ALPHA:
                n = 1;
                break;
            case PixelDataFormat::RG:
            case PixelDataFormat::RG_INTEGER:
            case PixelDataFormat::DEPTH_STENCIL:
                n = 2;
                break;
            case PixelDataFormat::RGB:
            case PixelDataFormat::RGB_INTEGER:
                n = 3;
                break;
            case PixelDataFormat::UNUSED:// shouldn't happen (used to be rgbm)
            case PixelDataFormat::RGBA:
            case PixelDataFormat::RGBA_INTEGER:
                n = 4;
                break;
        }

        size_t bpp = n;
        switch (type) {
            case PixelDataType::COMPRESSED:// Impossible -- to squash the IDE warnings
            case PixelDataType::UBYTE:
            case PixelDataType::BYTE:
                // nothing to do
                break;
            case PixelDataType::USHORT:
            case PixelDataType::SHORT:
            case PixelDataType::HALF:
                bpp *= 2;
                break;
            case PixelDataType::UINT:
            case PixelDataType::INT:
            case PixelDataType::FLOAT:
                bpp *= 4;
                break;
            case PixelDataType::UINT_10F_11F_11F_REV:
                // Special case, format must be RGB and uses 4 bytes
                assert_invariant(format == PixelDataFormat::RGB);
                bpp = 4;
                break;
            case PixelDataType::UINT_2_10_10_10_REV:
                // Special case, format must be RGBA and uses 4 bytes
                assert_invariant(format == PixelDataFormat::RGBA);
                bpp = 4;
                break;
            case PixelDataType::USHORT_565:
                // Special case, format must be RGB and uses 2 bytes
                assert_invariant(format == PixelDataFormat::RGB);
                bpp = 2;
                break;
        }
        return bpp;
    }

    // --------------------------------------------------------------------------------------------

    /**
     * Computes the size in bytes needed to fit an image of given dimensions and format
     *
     * @param format    Format of the image pixels
     * @param type      Type of the image pixels
     * @param stride    Stride of a row in pixels
     * @param height    Height of the image in rows
     * @param alignment Alignment in bytes of pixel rows
     * @return The buffer size needed to fit this image in bytes
     */
    /**
     * 计算容纳指定尺寸和格式图像所需的缓冲区大小（字节）
     * 
     * 根据图像格式、类型、跨距、高度和对齐要求计算所需的缓冲区大小。
     * 
     * @param format    图像像素格式
     * @param type      图像像素数据类型
     * @param stride    行的跨距（像素）
     *                  - 通常等于图像宽度
     *                  - 可能大于宽度（用于对齐或填充）
     * @param height    图像高度（行数）
     * @param alignment 像素行的字节对齐
     *                  - 必须大于 0
     *                  - 通常为 1、4 或 8
     *                  - 用于优化内存访问性能
     * @return          容纳此图像所需的缓冲区大小（字节）
     * 
     * 计算步骤：
     * 1. 计算每像素字节数（bpp = computePixelSize(format, type)）
     * 2. 计算每行字节数（bpr = bpp * stride）
     * 3. 对齐每行字节数（bprAligned = (bpr + alignment - 1) & ~(alignment - 1)）
     * 4. 计算总大小（total = bprAligned * height）
     * 
     * 对齐计算说明：
     * - 使用位运算实现向上对齐
     * - 公式：(value + alignment - 1) & ~(alignment - 1)
     * - 例如：alignment=4 时，将值向上对齐到 4 的倍数
     * 
     * 使用场景：
     * - 分配图像缓冲区
     * - 验证上传缓冲区大小
     * - 计算部分图像更新的偏移量
     */
    static constexpr size_t computeDataSize(PixelDataFormat format, PixelDataType type,
            size_t stride, size_t height, size_t alignment) noexcept {
        assert_invariant(alignment);

        size_t bpp = computePixelSize(format, type);
        size_t const bpr = bpp * stride;
        size_t const bprAligned = (bpr + (alignment - 1)) & (~alignment + 1);
        return bprAligned * height;
    }

    /**
     * Left coordinate in pixels
     * 
     * 左坐标（像素）
     * - 图像在缓冲区中的左坐标
     * - 用于部分图像更新
     * - 0 表示从图像左上角开始
     */
    //! left coordinate in pixels
    uint32_t left = 0;
    
    /**
     * Top coordinate in pixels
     * 
     * 顶坐标（像素）
     * - 图像在缓冲区中的顶坐标
     * - 用于部分图像更新
     * - 0 表示从图像左上角开始
     */
    //! top coordinate in pixels
    uint32_t top = 0;
    
    /**
     * 联合体：存储未压缩或压缩图像的不同参数
     * 
     * 使用联合体是因为未压缩和压缩图像使用不同的参数：
     * - 未压缩图像：使用 stride 和 format
     * - 压缩图像：使用 imageSize 和 compressedFormat
     */
    union {
        /**
         * 未压缩图像参数
         */
        struct {
            /**
             * Stride in pixels
             * 
             * 行的跨距（像素）
             * - 一行像素的数量
             * - 0 表示使用默认跨距（根据宽度计算）
             * - 可能大于实际宽度（用于对齐）
             */
            //! stride in pixels
            uint32_t stride;
            
            /**
             * Pixel data format
             * 
             * 像素数据格式
             * - 定义颜色通道组成（R、RG、RGB、RGBA 等）
             * - 用于未压缩图像
             */
            //! Pixel data format
            PixelDataFormat format;
        };
        
        /**
         * 压缩图像参数
         */
        struct {
            /**
             * Compressed image size
             * 
             * 压缩图像大小（字节）
             * - 压缩图像数据的总大小
             * - 用于验证数据完整性
             */
            //! compressed image size
            uint32_t imageSize;
            
            /**
             * Compressed image format
             * 
             * 压缩图像格式
             * - 定义压缩算法（ETC2、ASTC、DXT 等）
             * - 用于压缩图像
             */
            //! compressed image format
            backend::CompressedPixelDataType compressedFormat;
        };
    };
    
    /**
     * Pixel data type
     * 
     * 像素数据类型
     * - 定义像素的存储类型（UBYTE、FLOAT、COMPRESSED 等）
     * - 使用 4 位存储（支持最多 16 种类型）
     */
    //! pixel data type
    PixelDataType type : 4;
    
    /**
     * Row alignment in bytes
     * 
     * 行的字节对齐
     * - 像素行的字节对齐要求
     * - 使用 4 位存储（支持 1-15 字节对齐）
     * - 通常为 1、4 或 8
     */
    //! row alignment in bytes
    uint8_t alignment  : 4;
};

} // namespace backend::filament

#if !defined(NDEBUG)
utils::io::ostream& operator<<(utils::io::ostream& out, const filament::backend::PixelBufferDescriptor& b);
#endif

#endif // TNT_FILAMENT_BACKEND_PIXELBUFFERDESCRIPTOR_H
