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

#include "private/backend/CircularBuffer.h"

#include <utils/Logger.h>
#include <utils/Panic.h>
#include <utils/architecture.h>
#include <utils/ashmem.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ostream.h>

#if !defined(WIN32) && !defined(__EMSCRIPTEN__)
#    include <sys/mman.h>
#    include <unistd.h>
#    define HAS_MMAP 1
#else
#    define HAS_MMAP 0
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32)
#    include <windows.h> // for VirtualAlloc, VirtualProtect, VirtualFree
#    include <utils/unwindows.h>
#endif

using namespace utils;

namespace filament::backend {

/**
 * 页面大小（静态成员）
 * 
 * 在程序启动时初始化，用于内存对齐和分配。
 */
size_t CircularBuffer::sPageSize = arch::getPageSize();

/**
 * CircularBuffer 构造函数
 * 
 * 创建循环缓冲区，分配内存并初始化头尾指针。
 * 
 * @param size 缓冲区大小（字节）
 */
CircularBuffer::CircularBuffer(size_t size)
    : mSize(size) {
    mData = alloc(size);
    mTail = mData;  // 尾指针：写入位置
    mHead = mData;  // 头指针：读取位置
}

/**
 * CircularBuffer 析构函数
 * 
 * 释放分配的内存。
 */
CircularBuffer::~CircularBuffer() noexcept {
    dealloc();
}

/**
 * 分配循环缓冲区内存
 * 
 * 分配策略（按优先级）：
 * 1. 硬循环缓冲区（Hard Circular Buffer）：
 *    - 如果系统支持 mmap()，创建两个虚拟地址范围映射到同一物理页
 *    - 优点：真正的循环，无需特殊处理
 *    - 使用 ashmem（Android 共享内存）或匿名 mmap
 * 
 * 2. 软循环缓冲区（Soft Circular Buffer）：
 *    - 如果无法分配足够的连续地址空间，使用两个相邻的缓冲区
 *    - 在 circularize() 中需要特殊处理
 * 
 * 3. 回退模式：
 *    - 如果系统不支持 mmap，使用 malloc 分配两个相邻缓冲区
 * 
 * @param size 缓冲区大小（字节）
 * @return 分配的内存指针，失败返回 nullptr
 */
UTILS_NOINLINE
void* CircularBuffer::alloc(size_t size) {
#if HAS_MMAP
    void* data = nullptr;
    void* vaddr = MAP_FAILED;           // 第一个映射地址
    void* vaddr_shadow = MAP_FAILED;    // 第二个映射地址（循环的"影子"）
    void* vaddr_guard = MAP_FAILED;     // 保护页（防止内存损坏）
    size_t const BLOCK_SIZE = getBlockSize();
    
    /**
     * 步骤 1：创建 ashmem 共享内存区域
     * 
     * ashmem 是 Android 的共享内存机制，允许两个进程共享同一块物理内存。
     * 这里我们使用它来创建硬循环缓冲区。
     */
    int const fd = ashmem_create_region("filament::CircularBuffer", size + BLOCK_SIZE);
    if (fd >= 0) {
        /**
         * 步骤 2：预留足够的地址空间
         * 
         * 先预留整个地址范围（size * 2 + BLOCK_SIZE），然后立即取消映射。
         * 这样可以确保后续的映射能够使用连续的地址空间。
         */
        void* reserve_vaddr = mmap(nullptr, size * 2 + BLOCK_SIZE,
                PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserve_vaddr != MAP_FAILED) {
            munmap(reserve_vaddr, size * 2 + BLOCK_SIZE);
            
            /**
             * 步骤 3：映射循环缓冲区的第一个副本
             * 
             * 将 ashmem 文件描述符映射到虚拟地址空间。
             */
            vaddr = mmap(reserve_vaddr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
            if (vaddr != MAP_FAILED) {
                /**
                 * 步骤 4：填充地址空间（预分配页面）
                 * 
                 * 因为这是循环缓冲区，所有页面最终都会被分配，不如现在就分配。
                 * 这样可以避免后续的页面错误。
                 */
                memset(vaddr, 0, size);
                
                /**
                 * 步骤 5：映射循环缓冲区的第二个副本（"影子"）
                 * 
                 * 将同一个 ashmem 文件描述符映射到第一个副本之后的位置。
                 * 这样两个虚拟地址范围指向同一块物理内存，实现真正的循环。
                 */
                vaddr_shadow = mmap((char*)vaddr + size, size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
                if (vaddr_shadow != MAP_FAILED && (vaddr_shadow == (char*)vaddr + size)) {
                    /**
                     * 步骤 6：映射保护页
                     * 
                     * 在第二个副本之后映射一个不可访问的页面（PROT_NONE），
                     * 用于检测缓冲区溢出。
                     */
                    vaddr_guard = mmap((char*)vaddr_shadow + size, BLOCK_SIZE, PROT_NONE,
                            MAP_PRIVATE, fd, (off_t)size);
                    if (vaddr_guard != MAP_FAILED && (vaddr_guard == (char*)vaddr_shadow + size)) {
                        // 成功！创建了硬循环缓冲区
                        mAshmemFd = fd;
                        data = vaddr;
                    }
                }
            }
        }
    }

    /**
     * 回退到软循环缓冲区模式
     * 
     * 如果 ashmem 失败（mAshmemFd < 0），清理已分配的资源并回退到软循环缓冲区。
     */
    if (UTILS_UNLIKELY(mAshmemFd < 0)) {
        // ashmem 失败，清理已分配的资源
        if (vaddr_guard != MAP_FAILED) {
            munmap(vaddr_guard, BLOCK_SIZE);
        }

        if (vaddr_shadow != MAP_FAILED) {
            munmap(vaddr_shadow, size);
        }

        if (vaddr != MAP_FAILED) {
            munmap(vaddr, size);
        }

        if (fd >= 0) {
            close(fd);
        }

        /**
         * 使用匿名 mmap 分配软循环缓冲区
         * 
         * 分配两个缓冲区大小（size * 2）加上保护页（BLOCK_SIZE）。
         * 软循环缓冲区需要两个相邻的缓冲区，在 circularize() 中需要特殊处理。
         */
        data = mmap(nullptr, size * 2 + BLOCK_SIZE,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        FILAMENT_CHECK_POSTCONDITION(data != MAP_FAILED) <<
                "couldn't allocate " << (size * 2 / 1024) <<
                " KiB of virtual address space for the command buffer";

        LOG(WARNING) << "Using 'soft' CircularBuffer (" << (size * 2 / 1024) << " KiB)";

        /**
         * 在末尾设置保护页
         * 
         * 将最后一个页面设置为不可访问（PROT_NONE），用于检测缓冲区溢出。
         */
        void* guard = (void*)(uintptr_t(data) + size * 2);
        mprotect(guard, BLOCK_SIZE, PROT_NONE);
    }
    return data;
#elif defined(WIN32)
    /**
     * Windows 平台：使用 VirtualAlloc
     * 
     * 在 Windows 上，使用 VirtualAlloc 而不是 malloc 来分配虚拟内存。
     * 这样可以让我们通过 VirtualProtect 设置页面保护。
     */
    size_t const BLOCK_SIZE = getBlockSize();
    void* data = VirtualAlloc(nullptr, size * 2 + BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    FILAMENT_CHECK_POSTCONDITION(data != nullptr)
            << "couldn't allocate " << (size * 2 / 1024)
            << " KiB of virtual address space for the command buffer";

    /**
     * 在末尾设置保护页
     * 
     * 将最后一个页面设置为不可访问（PAGE_NOACCESS），用于检测缓冲区溢出。
     */
    void* guard = (void*)(uintptr_t(data) + size * 2);
    DWORD oldProtect = 0;
    BOOL ok = VirtualProtect(guard, BLOCK_SIZE, PAGE_NOACCESS, &oldProtect);
    FILAMENT_CHECK_POSTCONDITION(ok) << "VirtualProtect failed to set guard page";
    return data;
#else
    /**
     * 其他平台：使用 malloc
     * 
     * 对于不支持 mmap 的平台（如 Emscripten），使用简单的 malloc 分配两个缓冲区。
     */
    return ::malloc(2 * size);
#endif
}

/**
 * 释放循环缓冲区内存
 * 
 * 根据平台使用相应的释放方法：
 * - Linux/Android: munmap（如果使用 ashmem，还需要关闭文件描述符）
 * - Windows: VirtualFree
 * - 其他: free
 */
UTILS_NOINLINE
void CircularBuffer::dealloc() noexcept {
#if HAS_MMAP
    if (mData) {
        size_t const BLOCK_SIZE = getBlockSize();
        /**
         * 取消映射整个地址范围（包括两个副本和保护页）
         */
        munmap(mData, mSize * 2 + BLOCK_SIZE);
        /**
         * 如果使用了 ashmem，关闭文件描述符
         */
        if (mAshmemFd >= 0) {
            close(mAshmemFd);
            mAshmemFd = -1;
        }
    }
#elif defined(WIN32)
    if (mData) {
        /**
         * Windows: 使用 VirtualFree 释放虚拟内存
         */
        VirtualFree(mData, 0, MEM_RELEASE);
    }
#else
    /**
     * 其他平台: 使用 free 释放内存
     */
    ::free(mData);
#endif
    mData = nullptr;
}


/**
 * 获取当前缓冲区范围并循环化
 * 
 * 返回当前可用的缓冲区范围（从 tail 到 head），并处理循环逻辑。
 * 
 * 循环处理：
 * - 硬循环缓冲区（mAshmemFd > 0）：
 *   如果 head 超过了第一个副本的末尾，将其循环到第二个副本的相应位置。
 *   由于两个副本映射到同一物理内存，这是真正的循环。
 * 
 * - 软循环缓冲区（mAshmemFd <= 0）：
 *   如果 head 超过了第一个缓冲区的末尾，将其重置到开头。
 *   这需要特殊处理，因为两个缓冲区是独立的。
 * 
 * @return 缓冲区范围（tail 和 head 指针）
 */
CircularBuffer::Range CircularBuffer::getBuffer() noexcept {
    /**
     * 保存当前范围（用于返回）
     */
    Range const range{ .tail = mTail, .head = mHead };

    char* const pData = static_cast<char*>(mData);
    char const* const pEnd = pData + mSize;  // 第一个副本的末尾
    char const* const pHead = static_cast<char const*>(mHead);
    
    /**
     * 检查 head 是否超过了第一个副本的末尾
     */
    if (UTILS_UNLIKELY(pHead >= pEnd)) {
        size_t const overflow = pHead - pEnd;  // 超出的大小
        
        if (UTILS_LIKELY(mAshmemFd > 0)) {
            /**
             * 硬循环缓冲区：将 head 循环到第二个副本的相应位置
             * 
             * 由于两个副本映射到同一物理内存，这是真正的循环。
             * 
             * 布局示例：
             * Data         Tail  End   Head              [virtual]
             *  v             v    v     v
             *  +-------------:----+-----:--------------+
             *  |             :    |     :              |
             *  +-----:------------+--------------------+
             *       Head          |<------ copy ------>| [physical]
             */
            assert_invariant(overflow <= mSize);
            mHead = static_cast<void*>(pData + overflow);
        } else {
            /**
             * 软循环缓冲区：将 head 重置到开头
             * 
             * 这需要特殊处理，因为两个缓冲区是独立的。
             * 
             * 布局示例：
             * Data         Tail  End   Head
             *  v             v    v     v
             *  +-------------:----+-----+--------------+
             *  |             :    |     :              |
             *  +-----|------------+-----|--------------+
             *        |<---------------->|
             *           sliding window
             */
            mHead = mData;
        }
    }
    
    /**
     * 将 tail 更新为 head，准备下一轮写入
     */
    mTail = mHead;

    return range;
}

} // namespace filament::backend
