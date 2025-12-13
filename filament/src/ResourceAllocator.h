/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef TNT_FILAMENT_RESOURCEALLOCATOR_H
#define TNT_FILAMENT_RESOURCEALLOCATOR_H

#include <filament/Engine.h>

#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/TargetBufferInfo.h>

#include "backend/DriverApiForward.h"

#include <utils/StaticString.h>
#include <utils/Hash.h>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <cstddef>
#include <stddef.h>
#include <stdint.h>

namespace filament {

class ResourceAllocatorDisposer;

// The only reason we use an interface here is for unit-tests, so we can mock this allocator.
// This is not too time-critical, so that's okay.

/**
 * 资源分配器销毁接口
 * 
 * 用于单元测试的接口，允许模拟此分配器。
 * 这不是时间关键的，所以可以接受。
 */
// The only reason we use an interface here is for unit-tests, so we can mock this allocator.
// This is not too time-critical, so that's okay.
class ResourceAllocatorDisposerInterface {
public:
    /**
     * 销毁纹理
     * 
     * @param handle 纹理句柄
     */
    virtual void destroy(backend::TextureHandle handle) noexcept = 0;
protected:
    virtual ~ResourceAllocatorDisposerInterface();
};

/**
 * 资源分配器接口
 * 
 * 定义资源分配器的基本接口，用于创建和销毁渲染目标和纹理。
 */
class ResourceAllocatorInterface {
public:
    /**
     * 创建渲染目标
     * 
     * @param name 名称
     * @param targetBufferFlags 目标缓冲区标志
     * @param width 宽度
     * @param height 高度
     * @param samples 采样数
     * @param layerCount 层数
     * @param color 颜色缓冲区
     * @param depth 深度缓冲区信息
     * @param stencil 模板缓冲区信息
     * @return 渲染目标句柄
     */
    virtual backend::RenderTargetHandle createRenderTarget(utils::StaticString name,
            backend::TargetBufferFlags targetBufferFlags,
            uint32_t width,
            uint32_t height,
            uint8_t samples,
            uint8_t layerCount,
            backend::MRT color,
            backend::TargetBufferInfo depth,
            backend::TargetBufferInfo stencil) noexcept = 0;

    /**
     * 销毁渲染目标
     * 
     * @param h 渲染目标句柄
     */
    virtual void destroyRenderTarget(backend::RenderTargetHandle h) noexcept = 0;

    /**
     * 创建纹理
     * 
     * @param name 名称
     * @param target 采样器类型
     * @param levels Mip 级别数
     * @param format 纹理格式
     * @param samples 采样数
     * @param width 宽度
     * @param height 高度
     * @param depth 深度
     * @param swizzle 通道重排
     * @param usage 使用标志
     * @return 纹理句柄
     */
    virtual backend::TextureHandle createTexture(utils::StaticString name, backend::SamplerType target,
            uint8_t levels,backend::TextureFormat format, uint8_t samples,
            uint32_t width, uint32_t height, uint32_t depth,
            std::array<backend::TextureSwizzle, 4> swizzle,
            backend::TextureUsage usage) noexcept = 0;

    /**
     * 销毁纹理
     * 
     * @param h 纹理句柄
     */
    virtual void destroyTexture(backend::TextureHandle h) noexcept = 0;

    /**
     * 获取销毁器
     * 
     * @return 销毁器接口引用
     */
    virtual ResourceAllocatorDisposerInterface& getDisposer() noexcept = 0;

protected:
    virtual ~ResourceAllocatorInterface();
};

/**
 * 资源分配器
 * 
 * 管理 GPU 资源的分配和缓存，特别是纹理和渲染目标。
 * 使用缓存机制重用相同规格的资源。
 */
class ResourceAllocator final : public ResourceAllocatorInterface {
public:
    /**
     * 构造函数（带销毁器）
     * 
     * @param disposer 销毁器共享指针
     * @param config 引擎配置
     * @param driverApi 驱动 API 引用
     */
    explicit ResourceAllocator(std::shared_ptr<ResourceAllocatorDisposer> disposer,
            Engine::Config const& config, backend::DriverApi& driverApi) noexcept;

    /**
     * 构造函数（不带销毁器）
     * 
     * @param config 引擎配置
     * @param driverApi 驱动 API 引用
     */
    explicit ResourceAllocator(
            Engine::Config const& config, backend::DriverApi& driverApi) noexcept;

    /**
     * 析构函数
     */
    ~ResourceAllocator() noexcept override;

    /**
     * 终止资源分配器
     * 
     * 清理所有缓存的资源。
     */
    void terminate() noexcept;

    backend::RenderTargetHandle createRenderTarget(utils::StaticString name,
            backend::TargetBufferFlags targetBufferFlags,
            uint32_t width,
            uint32_t height,
            uint8_t samples,
            uint8_t layerCount,
            backend::MRT color,
            backend::TargetBufferInfo depth,
            backend::TargetBufferInfo stencil) noexcept override;

    void destroyRenderTarget(backend::RenderTargetHandle h) noexcept override;

    backend::TextureHandle createTexture(utils::StaticString name, backend::SamplerType target,
            uint8_t levels, backend::TextureFormat format, uint8_t samples,
            uint32_t width, uint32_t height, uint32_t depth,
            std::array<backend::TextureSwizzle, 4> swizzle,
            backend::TextureUsage usage) noexcept override;

    void destroyTexture(backend::TextureHandle h) noexcept override;

    ResourceAllocatorDisposerInterface& getDisposer() noexcept override;

    /**
     * 垃圾回收
     * 
     * 清理过期的缓存资源。
     * 
     * @param skippedFrame 是否跳过了帧（默认 false）
     */
    void gc(bool skippedFrame = false) noexcept;

private:
    /**
     * 缓存最大年龄
     */
    size_t const mCacheMaxAge;

    /**
     * 纹理键结构
     * 
     * 用于纹理缓存的键，包含纹理的所有规格信息。
     */
    struct TextureKey {
        utils::StaticString name; // doesn't participate in the hash - 名称（不参与哈希）
        backend::SamplerType target;  // 采样器类型
        uint8_t levels;  // Mip 级别数
        backend::TextureFormat format;  // 纹理格式
        uint8_t samples;  // 采样数
        uint32_t width;  // 宽度
        uint32_t height;  // 高度
        uint32_t depth;  // 深度
        backend::TextureUsage usage;  // 使用标志
        std::array<backend::TextureSwizzle, 4> swizzle;  // 通道重排

        /**
         * 获取纹理大小（字节）
         * 
         * @return 纹理大小
         */
        size_t getSize() const noexcept;

        bool operator==(const TextureKey& other) const noexcept {
            return target == other.target &&
                   levels == other.levels &&
                   format == other.format &&
                   samples == other.samples &&
                   width == other.width &&
                   height == other.height &&
                   depth == other.depth &&
                   usage == other.usage &&
                   swizzle == other.swizzle;
        }

        friend size_t hash_value(TextureKey const& k) {
            size_t seed = 0;
            utils::hash::combine_fast(seed, k.target);
            utils::hash::combine_fast(seed, k.levels);
            utils::hash::combine_fast(seed, k.format);
            utils::hash::combine_fast(seed, k.samples);
            utils::hash::combine_fast(seed, k.width);
            utils::hash::combine_fast(seed, k.height);
            utils::hash::combine_fast(seed, k.depth);
            utils::hash::combine_fast(seed, k.usage);
            utils::hash::combine_fast(seed, k.swizzle[0]);
            utils::hash::combine_fast(seed, k.swizzle[1]);
            utils::hash::combine_fast(seed, k.swizzle[2]);
            utils::hash::combine_fast(seed, k.swizzle[3]);
            return seed;
        }
    };

    /**
     * 纹理缓存负载结构
     * 
     * 存储缓存的纹理句柄及其元数据。
     */
    struct TextureCachePayload {
        backend::TextureHandle handle;  // 纹理句柄
        size_t age = 0;  // 年龄（自上次使用以来的帧数）
        uint32_t size = 0;  // 纹理大小（字节）
    };

    /**
     * 哈希器模板
     * 
     * 为键类型提供哈希函数。
     */
    template<typename T>
    struct Hasher {
        /**
         * 计算哈希值
         * 
         * @param s 键引用
         * @return 哈希值
         */
        std::size_t operator()(T const& s) const noexcept {
            return hash_value(s);  // 使用全局 hash_value 函数
        }
    };

    /**
     * 句柄类型的哈希器特化
     * 
     * 直接使用句柄的 ID 作为哈希值。
     */
    template<typename T>
    struct Hasher<backend::Handle<T>> {
        /**
         * 计算哈希值
         * 
         * @param s 句柄引用
         * @return 句柄 ID
         */
        std::size_t operator()(backend::Handle<T> const& s) const noexcept {
            return s.getId();  // 使用句柄 ID
        }
    };

    /**
     * 转储缓存信息（调试用）
     * 
     * @param brief 是否简要输出
     */
    inline void dump(bool brief = false) const noexcept;

    /**
     * 关联容器模板类
     * 
     * 使用 std::vector 而不是 std::multimap，因为我们不期望缓存中有很多项，
     * 而且 std::multimap 会生成大量代码。std::multimap 在约 1000 项时才开始
     * 显著更好。
     */
    template<typename Key, typename Value, typename Hasher = Hasher<Key>>
    class AssociativeContainer {
        /**
         * 我们使用 std::vector 而不是 std::multimap，因为我们不期望缓存中有很多项，
         * 而且 std::multimap 会生成大量代码。std::multimap 在约 1000 项时才开始
         * 显著更好。
         */
        // We use a std::vector instead of a std::multimap because we don't expect many items
        // in the cache and std::multimap generates tons of code. std::multimap starts getting
        // significantly better around 1000 items.
        using Container = std::vector<std::pair<Key, Value>>;  // 键值对向量
        Container mContainer;  // 容器

    public:
        /**
         * 构造函数
         */
        AssociativeContainer();
        
        /**
         * 析构函数
         */
        ~AssociativeContainer() noexcept;
        
        using iterator = typename Container::iterator;  // 迭代器类型
        using const_iterator = typename Container::const_iterator;  // 常量迭代器类型
        using key_type = typename Container::value_type::first_type;  // 键类型
        using value_type = typename Container::value_type::second_type;  // 值类型

        /**
         * 获取容器大小
         */
        size_t size() const { return mContainer.size(); }
        
        /**
         * 检查是否为空
         */
        bool empty() const { return size() == 0; }
        
        /**
         * 获取开始迭代器
         */
        iterator begin() { return mContainer.begin(); }
        const_iterator begin() const { return mContainer.begin(); }
        
        /**
         * 获取结束迭代器
         */
        iterator end() { return mContainer.end(); }
        const_iterator end() const  { return mContainer.end(); }
        
        /**
         * 擦除元素
         * 
         * @param it 迭代器
         * @return 下一个迭代器
         */
        iterator erase(iterator it);
        
        /**
         * 查找元素
         * 
         * @param key 键
         * @return 迭代器
         */
        const_iterator find(key_type const& key) const;
        iterator find(key_type const& key);
        
        /**
         * 就地构造元素
         * 
         * @param args 构造参数
         */
        template<typename ... ARGS>
        void emplace(ARGS&&... args);
    };

    using CacheContainer = AssociativeContainer<TextureKey, TextureCachePayload>;  // 缓存容器类型

    /**
     * 清除缓存条目
     * 
     * 从缓存中移除指定条目并释放资源。
     * 
     * @param pos 迭代器位置
     * @return 下一个迭代器
     */
    CacheContainer::iterator
    purge(CacheContainer::iterator const& pos);

    backend::DriverApi& mBackend;  // 后端驱动 API 引用
    std::shared_ptr<ResourceAllocatorDisposer> mDisposer;  // 销毁器共享指针
    CacheContainer mTextureCache;  // 纹理缓存容器
    size_t mAge = 0;  // 当前年龄（用于垃圾回收）
    uint32_t mCacheSize = 0;  // 当前缓存大小（字节）
    uint32_t mCacheSizeHiWaterMark = 0;  // 缓存大小高水位标记（字节）
    static constexpr bool mEnabled = true;  // 是否启用缓存

    friend class ResourceAllocatorDisposer;  // 友元：销毁器类
};

/**
 * 资源分配器销毁器
 * 
 * 管理正在使用的纹理，跟踪哪些纹理已从缓存中检出。
 */
class ResourceAllocatorDisposer final : public ResourceAllocatorDisposerInterface {
    using TextureKey = ResourceAllocator::TextureKey;  // 纹理键类型别名
public:
    /**
     * 构造函数
     * 
     * @param driverApi 驱动 API 引用
     */
    explicit ResourceAllocatorDisposer(backend::DriverApi& driverApi) noexcept;
    
    /**
     * 析构函数
     */
    ~ResourceAllocatorDisposer() noexcept override;
    
    /**
     * 终止销毁器
     * 
     * 清理所有资源。
     */
    void terminate() noexcept;
    
    /**
     * 销毁纹理
     * 
     * 如果纹理正在使用中，延迟销毁；否则立即销毁。
     * 
     * @param handle 纹理句柄
     */
    void destroy(backend::TextureHandle handle) noexcept override;

private:
    friend class ResourceAllocator;  // 友元：资源分配器类
    
    /**
     * 检出纹理
     * 
     * 将纹理标记为正在使用。
     * 
     * @param handle 纹理句柄
     * @param key 纹理键
     */
    void checkout(backend::TextureHandle handle, TextureKey key);
    
    /**
     * 检入纹理
     * 
     * 将纹理标记为不再使用，返回纹理键（如果存在）。
     * 
     * @param handle 纹理句柄
     * @return 纹理键（如果纹理已检出）
     */
    std::optional<TextureKey> checkin(backend::TextureHandle handle);

    using InUseContainer = ResourceAllocator::AssociativeContainer<backend::TextureHandle, TextureKey>;  // 正在使用的纹理容器类型
    backend::DriverApi& mBackend;  // 后端驱动 API 引用
    InUseContainer mInUseTextures;  // 正在使用的纹理容器
};

} // namespace filament


#endif //TNT_FILAMENT_RESOURCEALLOCATOR_H
