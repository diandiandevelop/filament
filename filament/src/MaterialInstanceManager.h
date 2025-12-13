/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <utils/bitset.h>

#include <vector>

namespace filament {

class FMaterial;
class FMaterialInstance;
class FEngine;

/**
 * MaterialInstanceManager 类
 * 
 * 管理按材质映射的材质实例缓存。
 * 拥有缓存允许我们在帧之间重用实例。
 */
// This class manages a cache of material instances mapped by materials. Having a cache allows us to
// re-use instances across frames.
class MaterialInstanceManager {
public:
    /**
     * Record 类
     * 
     * 表示一个材质的记录，包含该材质的所有实例。
     */
    class Record {
    public:
        /**
         * 构造函数
         * 
         * @param material 材质指针
         */
        Record(FMaterial const* material)
            : mMaterial(material),
              mAvailable(0) {}  // 初始可用实例数为 0
        ~Record() = default;

        /**
         * 拷贝构造函数
         */
        Record(Record const& rhs) noexcept;
        /**
         * 拷贝赋值操作符
         */
        Record& operator=(Record const& rhs) noexcept;
        /**
         * 移动构造函数
         */
        Record(Record&& rhs) noexcept;
        /**
         * 移动赋值操作符
         */
        Record& operator=(Record&& rhs) noexcept;

        /**
         * 终止记录
         * 
         * 清理所有实例。
         * 
         * @param engine 引擎引用
         */
        void terminate(FEngine& engine);
        /**
         * 重置记录
         * 
         * 将所有实例标记为不可用。
         */
        void reset() { mAvailable = 0; }
        /**
         * 获取实例
         * 
         * 返回一个可用的实例和其索引。
         * 
         * @return (实例指针, 索引) 对
         */
        std::pair<FMaterialInstance*, int32_t> getInstance();
        /**
         * 获取固定索引的实例
         * 
         * @param fixedInstanceindex 固定实例索引
         * @return 实例指针
         */
        FMaterialInstance* getInstance(int32_t fixedInstanceindex) const;

    private:
        FMaterial const* mMaterial = nullptr;  // 材质指针
        std::vector<FMaterialInstance*> mInstances;  // 实例向量
        uint32_t mAvailable;  // 可用实例数

        friend class MaterialInstanceManager;
    };

    /**
     * 无效固定索引常量
     */
    constexpr static int32_t INVALID_FIXED_INDEX = -1;

    /**
     * 默认构造函数
     */
    MaterialInstanceManager() noexcept;
    /**
     * 拷贝构造函数
     */
    MaterialInstanceManager(MaterialInstanceManager const& rhs) noexcept;
    /**
     * 移动构造函数
     */
    MaterialInstanceManager(MaterialInstanceManager&& rhs) noexcept;
    /**
     * 拷贝赋值操作符
     */
    MaterialInstanceManager& operator=(MaterialInstanceManager const& rhs) noexcept;
    /**
     * 移动赋值操作符
     */
    MaterialInstanceManager& operator=(MaterialInstanceManager&& rhs) noexcept;

    /**
     * 析构函数
     */
    ~MaterialInstanceManager();

    /**
     * 终止 MaterialInstanceManager
     * 
     * 销毁所有缓存的材质实例。
     * 这需要在相应 Material 销毁之前完成。
     * 
     * @param engine 引擎引用
     */
    /*
     * Destroy all the cached material instances. This needs to be done before the destruction of
     * the corresponding Material.
     */
    void terminate(FEngine& engine);

    /**
     * 获取材质实例
     * 
     * 给定材质，返回一个材质实例。
     * 实现将尝试在缓存中查找可用实例。
     * 如果未找到，则创建新实例并添加到缓存中。
     * 
     * @param ma 材质指针
     * @return 材质实例指针
     */
    /*
     * This returns a material instance given a material. The implementation will try to find an
     * available instance in the cache. If one is not found, then a new instance will be created and
     * added to the cache.
     */
    FMaterialInstance* getMaterialInstance(FMaterial const* ma) const;

    /**
     * 获取固定索引的材质实例
     * 
     * 给定材质和索引，返回材质实例。
     * `fixedIndex` 应该是 getFixedMaterialInstance 返回的值。
     * 
     * @param ma 材质指针
     * @param fixedIndex 固定索引
     * @return 材质实例指针
     */
    /*
     * This returns a material instance given a material and an index. The `fixedIndex` should be
     * a value returned by getiFixedMaterialInstance.
     */
    FMaterialInstance* getMaterialInstance(FMaterial const* ma, int32_t const fixedIndex) const;

    /**
     * 获取固定材质实例
     * 
     * 给定材质，返回材质实例和索引。
     * 这用于两个 framegraph 通道需要引用同一材质实例的情况。
     * 返回的索引可用于 `getFixedMaterialInstance` 来获取材质的特定实例
     * （而不是记录缓存中的随机条目）。
     * 
     * @param ma 材质指针
     * @return (材质实例指针, 索引) 对
     */
    /*
     * This returns a material instance and an index given a material. This is needed for the
     * case when two framegraph passes need to refer to the same material instance.
     * The returned index can be used with `getFixedMaterialInstance` to get a specific instance
     * of a material (and not a random entry in the record cache).
     */
    std::pair<FMaterialInstance*, int32_t> getFixedMaterialInstance(FMaterial const* ma);


    /**
     * 重置所有材质实例
     * 
     * 将所有材质实例标记为未使用。
     * 通常，您应该在帧开始时调用此方法。
     */
    /*
     * Marks all of the material instances as unused. Typically, you'd call this at the beginning of
     * a frame.
     */
    void reset() {
        std::for_each(mMaterials.begin(), mMaterials.end(), [](auto& record) { record.reset(); });
    }

private:
    /**
     * 获取材质记录
     * 
     * 获取指定材质的记录。如果记录不存在，则创建新的。
     * 
     * @param material 材质指针
     * @return 材质记录的引用
     */
    Record& getRecord(FMaterial const* material) const;

    /**
     * 材质记录向量
     * 
     * 存储所有材质的记录，每个记录包含该材质的所有实例。
     * 使用 mutable 以允许在 const 方法中修改（用于缓存）。
     */
    mutable std::vector<Record> mMaterials;  // 材质记录向量
};

} // namespace filament
