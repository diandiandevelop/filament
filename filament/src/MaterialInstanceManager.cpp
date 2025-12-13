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


#include "MaterialInstanceManager.h"

#include "details/Engine.h"
#include "details/Material.h"
#include "details/MaterialInstance.h"

#include <utils/debug.h>

#include <iterator>
#include <utility>

#include <stdint.h>

namespace filament {

using Record = MaterialInstanceManager::Record;

/**
 * 获取材质实例（自动分配索引）
 * 
 * 从记录中获取一个可用的材质实例。
 * 如果有可用的实例，则返回它；否则创建新的实例。
 * 
 * @return 材质实例指针和索引的对
 */
std::pair<FMaterialInstance*, int32_t> Record::getInstance() {
    /**
     * 如果有可用的实例（mAvailable < mInstances.size()）
     */
    if (mAvailable < mInstances.size()) {
        /**
         * 获取当前可用索引并递增
         */
        auto index = mAvailable++;
        /**
         * 返回该实例和索引
         */
        return { mInstances[index], index };
    }
    /**
     * 确保可用索引等于实例数量（需要创建新实例）
     */
    assert_invariant(mAvailable == mInstances.size());
    /**
     * 获取材质名称
     */
    auto& name = mMaterial->getName();
    /**
     * 创建新的材质实例
     */
    FMaterialInstance* inst = mMaterial->createInstance(name.c_str_safe());
    /**
     * 将新实例添加到列表
     */
    mInstances.push_back(inst);
    /**
     * 返回新实例和递增后的索引
     */
    return { inst, mAvailable++ };
}

/**
 * 获取材质实例（固定索引）
 * 
 * 根据固定索引获取材质实例。
 * 
 * @param fixedInstanceindex 固定实例索引
 * @return 材质实例指针
 */
FMaterialInstance* Record::getInstance(int32_t const fixedInstanceindex) const {
    /**
     * 确保索引在有效范围内
     */
    assert_invariant(fixedInstanceindex >= 0 &&  fixedInstanceindex < int32_t(mInstances.size()));
    /**
     * 返回指定索引的实例
     */
    return mInstances[fixedInstanceindex];
}

/**
 * 在 cpp 中定义以避免内联
 */
// Defined in cpp to avoid inlining
/**
 * 拷贝构造函数
 * 
 * 默认实现，拷贝所有成员。
 */
Record::Record(Record const& rhs) noexcept = default;

/**
 * 拷贝赋值操作符
 * 
 * 默认实现，拷贝所有成员。
 */
Record& Record::operator=(Record const& rhs) noexcept = default;

/**
 * 移动构造函数
 * 
 * 默认实现，移动所有成员。
 */
Record::Record(Record&& rhs) noexcept = default;

/**
 * 移动赋值操作符
 * 
 * 默认实现，移动所有成员。
 */
Record& Record::operator=(Record&& rhs) noexcept = default;

/**
 * 终止记录
 * 
 * 销毁记录中的所有材质实例。
 * 
 * @param engine 引擎引用
 */
void Record::terminate(FEngine& engine) {
    /**
     * 遍历所有实例并销毁它们
     */
    std::for_each(mInstances.begin(), mInstances.end(),
            [&engine](auto instance) { engine.destroy(instance); });
}

/**
 * 默认构造函数
 * 
 * 创建空的材质实例管理器。
 */
MaterialInstanceManager::MaterialInstanceManager() noexcept {}

/**
 * 拷贝构造函数
 * 
 * 默认实现，拷贝所有材质记录。
 */
MaterialInstanceManager::MaterialInstanceManager(
        MaterialInstanceManager const& rhs) noexcept = default;

/**
 * 移动构造函数
 * 
 * 默认实现，移动所有材质记录。
 */
MaterialInstanceManager::MaterialInstanceManager(MaterialInstanceManager&& rhs) noexcept = default;

/**
 * 拷贝赋值操作符
 * 
 * 默认实现，拷贝所有材质记录。
 */
MaterialInstanceManager& MaterialInstanceManager::operator=(
        MaterialInstanceManager const& rhs) noexcept = default;

/**
 * 移动赋值操作符
 * 
 * 默认实现，移动所有材质记录。
 */
MaterialInstanceManager& MaterialInstanceManager::operator=(
        MaterialInstanceManager&& rhs) noexcept = default;

/**
 * 析构函数
 * 
 * 默认实现。注意：必须在析构前调用 terminate() 来清理资源。
 */
MaterialInstanceManager::~MaterialInstanceManager() = default;

/**
 * 终止材质实例管理器
 * 
 * 销毁所有材质记录及其实例。
 * 
 * @param engine 引擎引用
 */
void MaterialInstanceManager::terminate(FEngine& engine) {
    /**
     * 遍历所有材质记录并终止它们
     */
    std::for_each(mMaterials.begin(), mMaterials.end(), [&engine](auto& record) {
        record.terminate(engine);
    });
}

/**
 * 获取材质记录
 * 
 * 获取指定材质的记录。如果记录不存在，则创建新的。
 * 
 * @param ma 材质指针
 * @return 材质记录的引用
 */
Record& MaterialInstanceManager::getRecord(FMaterial const* const ma) const {
    /**
     * 查找是否已存在该材质的记录
     */
    auto itr = std::find_if(mMaterials.begin(), mMaterials.end(), [ma](auto& record) {
        return ma == record.mMaterial;
    });
    /**
     * 如果未找到，创建新记录
     */
    if (itr == mMaterials.end()) {
        mMaterials.emplace_back(ma);
        /**
         * 获取新插入记录的迭代器
         */
        itr = std::prev(mMaterials.end());
    }
    /**
     * 返回记录的引用
     */
    return *itr;
}

/**
 * 获取材质实例（自动分配索引）
 * 
 * 获取指定材质的实例，自动分配索引。
 * 
 * @param ma 材质指针
 * @return 材质实例指针
 */
FMaterialInstance* MaterialInstanceManager::getMaterialInstance(FMaterial const* ma) const {
    /**
     * 获取记录并获取实例（自动分配索引）
     */
    auto [inst, index] = getRecord(ma).getInstance();
    return inst;
}

/**
 * 获取材质实例（固定索引）
 * 
 * 根据固定索引获取指定材质的实例。
 * 
 * @param ma 材质指针
 * @param fixedIndex 固定实例索引
 * @return 材质实例指针
 */
FMaterialInstance* MaterialInstanceManager::getMaterialInstance(FMaterial const* ma,
        int32_t const fixedIndex) const {
    /**
     * 获取记录并获取指定索引的实例
     */
    return getRecord(ma).getInstance(fixedIndex);
}

/**
 * 获取固定材质实例（返回实例和索引）
 * 
 * 获取指定材质的实例，并返回实例和索引的对。
 * 
 * @param ma 材质指针
 * @return 材质实例指针和索引的对
 */
std::pair<FMaterialInstance*, int32_t> MaterialInstanceManager::getFixedMaterialInstance(
        FMaterial const* ma) {
    /**
     * 获取记录并获取实例（自动分配索引）
     */
    return getRecord(ma).getInstance();
}


} // namespace filament
