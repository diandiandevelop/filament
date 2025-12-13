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

#ifndef TNT_FILAMENT_COMPONENTS_TRANSFORMMANAGER_H
#define TNT_FILAMENT_COMPONENTS_TRANSFORMMANAGER_H

#include "downcast.h"

#include <filament/TransformManager.h>

#include <utils/compiler.h>
#include <utils/SingleInstanceComponentManager.h>
#include <utils/Entity.h>
#include <utils/Slice.h>

#include <math/mat4.h>

namespace filament {

/**
 * 变换管理器实现类
 * 
 * 管理场景中实体的变换组件，包括局部变换和世界变换。
 * 支持父子关系和层次结构，使用链表结构管理子节点。
 * 支持精确变换模式（使用双精度存储平移部分）。
 * 
 * 实现细节：
 * - 使用结构体数组（SoA）存储变换数据
 * - 使用链表结构管理父子关系
 * - 支持事务模式以批量更新变换
 * - 支持精确变换模式（双精度平移）
 */
class UTILS_PRIVATE FTransformManager : public TransformManager {
public:
    using Instance = Instance;  // 实例类型别名

    /**
     * 构造函数
     */
    FTransformManager() noexcept;
    
    /**
     * 析构函数
     */
    ~FTransformManager() noexcept;

    /**
     * 终止
     * 
     * 释放所有资源。
     */
    // free-up all resources
    void terminate() noexcept;


    /*
    * Component Manager APIs
    */

    /**
     * 检查实体是否有变换组件
     * 
     * @param e 实体
     * @return 如果有组件返回 true，否则返回 false
     */
    bool hasComponent(utils::Entity const e) const noexcept {
        return mManager.hasComponent(e);  // 检查是否有组件
    }

    /**
     * 获取实体的变换实例
     * 
     * @param e 实体
     * @return 变换实例
     */
    Instance getInstance(utils::Entity const e) const noexcept {
        return { mManager.getInstance(e) };  // 获取实例
    }

    /**
     * 获取组件数量
     * 
     * @return 组件数量
     */
    size_t getComponentCount() const noexcept {
        return mManager.getComponentCount();  // 返回组件数量
    }

    /**
     * 检查是否为空
     * 
     * @return 如果为空返回 true，否则返回 false
     */
    bool empty() const noexcept {
        return mManager.empty();  // 检查是否为空
    }

    /**
     * 获取实例对应的实体
     * 
     * @param i 实例
     * @return 实体
     */
    utils::Entity getEntity(Instance const i) const noexcept {
        return mManager.getEntity(i);  // 获取实体
    }

    /**
     * 获取所有实体数组
     * 
     * @return 实体数组常量指针
     */
    utils::Entity const* getEntities() const noexcept {
        return mManager.getEntities();  // 返回实体数组
    }

    /**
     * 设置精确变换启用状态
     * 
     * 启用后，平移部分使用双精度存储（高精度部分存储在单独的字段中）。
     * 
     * @param enable 是否启用
     */
    void setAccurateTranslationsEnabled(bool enable) noexcept;

    /**
     * 检查精确变换是否启用
     * 
     * @return 如果启用返回 true，否则返回 false
     */
    bool isAccurateTranslationsEnabled() const noexcept {
        return mAccurateTranslations;  // 返回精确变换标志
    }

    /**
     * 创建变换组件（无父节点）
     * 
     * @param entity 实体
     */
    void create(utils::Entity entity);

    /**
     * 创建变换组件（单精度版本）
     * 
     * @param entity 实体
     * @param parent 父节点实例
     * @param localTransform 局部变换矩阵
     */
    void create(utils::Entity entity, Instance parent, const math::mat4f& localTransform);

    /**
     * 创建变换组件（双精度版本）
     * 
     * @param entity 实体
     * @param parent 父节点实例
     * @param localTransform 局部变换矩阵（双精度）
     */
    void create(utils::Entity entity, Instance parent, const math::mat4& localTransform);

    /**
     * 销毁变换组件
     * 
     * @param e 实体
     */
    void destroy(utils::Entity e) noexcept;

    /**
     * 设置父节点
     * 
     * @param i 实例
     * @param newParent 新父节点实例
     */
    void setParent(Instance i, Instance newParent) noexcept;

    /**
     * 获取父节点实体
     * 
     * @param i 实例
     * @return 父节点实体（如果没有父节点返回空实体）
     */
    utils::Entity getParent(Instance i) const noexcept;

    /**
     * 获取子节点数量
     * 
     * @param i 实例
     * @return 子节点数量
     */
    size_t getChildCount(Instance i) const noexcept;

    /**
     * 获取子节点
     * 
     * 将子节点实体复制到提供的数组中。
     * 
     * @param i 实例
     * @param children 输出数组
     * @param count 数组大小
     * @return 实际复制的子节点数量
     */
    size_t getChildren(Instance i, utils::Entity* children, size_t count) const noexcept;

    /**
     * 获取子节点迭代器开始
     * 
     * @param parent 父节点实例
     * @return 子节点迭代器
     */
    children_iterator getChildrenBegin(Instance parent) const noexcept;

    /**
     * 获取子节点迭代器结束
     * 
     * @param parent 父节点实例
     * @return 子节点迭代器结束标记
     */
    children_iterator getChildrenEnd(Instance parent) const noexcept;

    /**
     * 打开局部变换事务
     * 
     * 在事务模式下，局部变换的更新不会立即传播到子节点。
     * 必须在调用 commitLocalTransformTransaction() 后才会更新。
     */
    void openLocalTransformTransaction() noexcept;

    /**
     * 提交局部变换事务
     * 
     * 提交事务并更新所有受影响的子节点的世界变换。
     */
    void commitLocalTransformTransaction() noexcept;

    /**
     * 垃圾回收
     * 
     * 清理已销毁实体的组件数据。
     * 
     * @param em 实体管理器引用
     */
    void gc(utils::EntityManager& em) noexcept;

    /**
     * 获取所有世界变换
     * 
     * 返回所有实体的世界变换矩阵切片。
     * 
     * @return 世界变换矩阵切片
     */
    utils::Slice<const math::mat4f> getWorldTransforms() const noexcept {
        return mManager.slice<WORLD>();  // 返回世界变换切片
    }

    /**
     * 设置变换（单精度版本）
     * 
     * @param ci 实例
     * @param model 模型矩阵
     */
    void setTransform(Instance ci, const math::mat4f& model) noexcept;

    /**
     * 设置变换（双精度版本）
     * 
     * @param ci 实例
     * @param model 模型矩阵（双精度）
     */
    void setTransform(Instance ci, const math::mat4& model) noexcept;

    /**
     * 获取局部变换
     * 
     * @param ci 实例
     * @return 局部变换矩阵常量引用
     */
    const math::mat4f& getTransform(Instance const ci) const noexcept {
        return mManager[ci].local;  // 返回局部变换
    }

    /**
     * 获取世界变换
     * 
     * @param ci 实例
     * @return 世界变换矩阵常量引用
     */
    const math::mat4f& getWorldTransform(Instance const ci) const noexcept {
        return mManager[ci].world;  // 返回世界变换
    }

    /**
     * 获取精确局部变换
     * 
     * 返回包含高精度平移部分的局部变换矩阵（双精度）。
     * 
     * @param ci 实例
     * @return 精确局部变换矩阵（双精度）
     */
    math::mat4 getTransformAccurate(Instance const ci) const noexcept {
        math::mat4f const& local = mManager[ci].local;  // 获取局部变换
        math::float3 const localTranslationLo = mManager[ci].localTranslationLo;  // 获取高精度平移部分
        math::mat4 r(local);  // 转换为双精度矩阵
        r[3].xyz += localTranslationLo;  // 添加高精度平移部分
        return r;  // 返回精确变换
    }

    /**
     * 获取精确世界变换
     * 
     * 返回包含高精度平移部分的世界变换矩阵（双精度）。
     * 
     * @param ci 实例
     * @return 精确世界变换矩阵（双精度）
     */
    math::mat4 getWorldTransformAccurate(Instance const ci) const noexcept {
        math::mat4f const& world = mManager[ci].world;  // 获取世界变换
        math::float3 const worldTranslationLo = mManager[ci].worldTranslationLo;  // 获取高精度平移部分
        math::mat4 r(world);  // 转换为双精度矩阵
        r[3].xyz += worldTranslationLo;  // 添加高精度平移部分
        return r;  // 返回精确变换
    }

private:
    struct Sim;  // 前向声明

    /**
     * 验证节点
     * 
     * 验证节点的父子关系是否正确。
     * 
     * @param i 实例
     */
    void validateNode(Instance i) noexcept;
    
    /**
     * 移除节点
     * 
     * 从父子关系链中移除节点。
     * 
     * @param i 实例
     */
    void removeNode(Instance i) noexcept;
    
    /**
     * 更新节点
     * 
     * 更新节点的世界变换和所有子节点。
     * 
     * @param i 实例
     */
    void updateNode(Instance i) noexcept;
    
    /**
     * 更新节点变换
     * 
     * 仅更新节点的世界变换，不更新子节点。
     * 
     * @param i 实例
     */
    void updateNodeTransform(Instance i) noexcept;
    
    /**
     * 插入节点
     * 
     * 将节点插入到父子关系链中。
     * 
     * @param i 实例
     * @param p 父节点实例
     */
    void insertNode(Instance i, Instance p) noexcept;
    
    /**
     * 交换节点
     * 
     * 交换两个节点在数组中的位置。
     * 
     * @param i 实例
     * @param j 实例
     */
    void swapNode(Instance i, Instance j) noexcept;
    
    /**
     * 变换子节点
     * 
     * 递归更新所有子节点的世界变换。
     * 
     * @param manager 管理器引用
     * @param firstChild 第一个子节点实例
     */
    void transformChildren(Sim& manager, Instance firstChild) noexcept;

    /**
     * 计算所有世界变换
     * 
     * 计算所有节点的世界变换。
     */
    void computeAllWorldTransforms() noexcept;

    /**
     * 计算世界变换
     * 
     * 根据父节点变换和局部变换计算世界变换。
     * 
     * @param outWorld 输出世界变换矩阵
     * @param inoutWorldTranslationLo 输入输出高精度世界平移部分
     * @param pt 父节点世界变换矩阵
     * @param local 局部变换矩阵
     * @param ptTranslationLo 父节点高精度平移部分
     * @param localTranslationLo 局部高精度平移部分
     * @param accurate 是否使用精确模式
     */
    static void computeWorldTransform(math::mat4f& outWorld, math::float3& inoutWorldTranslationLo,
            math::mat4f const& pt, math::mat4f const& local,
            math::float3 const& ptTranslationLo, math::float3 const& localTranslationLo,
            bool accurate);

    friend class children_iterator;  // 允许子节点迭代器访问私有成员

    /**
     * 字段索引枚举
     * 
     * 用于访问结构体数组中的不同字段。
     */
    enum {
        LOCAL,          // 局部变换（相对于父节点），如果没有父节点则为世界变换
        WORLD,          // 世界变换
        LOCAL_LO,       // 精确局部平移（高精度部分）
        WORLD_LO,       // 精确世界平移（高精度部分）
        PARENT,         // 父节点实例
        FIRST_CHILD,    // 第一个子节点实例
        NEXT,           // 下一个兄弟节点实例
        PREV,           // 上一个兄弟节点实例
    };

    /**
     * 基础组件管理器类型
     * 
     * 使用结构体数组（SoA）存储组件数据。
     */
    using Base = utils::SingleInstanceComponentManager<
            math::mat4f,    // 局部变换
            math::mat4f,    // 世界变换
            math::float3,   // 精确局部平移
            math::float3,   // 精确世界平移
            Instance,       // 父节点
            Instance,       // 第一个子节点
            Instance,       // 下一个兄弟节点
            Instance        // 上一个兄弟节点
    >;

    /**
     * 组件管理器结构
     * 
     * 继承自 Base，提供代理访问接口。
     */
    struct Sim : public Base {
        using Base::gc;  // 垃圾回收方法
        using Base::swap;  // 交换方法

        /**
         * 获取结构体数组引用
         * 
         * @return 结构体数组引用
         */
        SoA& getSoA() { return mData; }

        /**
         * 代理结构
         * 
         * 提供对组件字段的统一访问接口。
         * 所有方法都会被内联。
         */
        struct Proxy {
            /**
             * 构造函数
             * 
             * 所有内容都会被内联。
             */
            // all of these gets inlined
            UTILS_ALWAYS_INLINE
            Proxy(Base& sim, utils::EntityInstanceBase::Type i) noexcept
                    : local{ sim, i } { }  // 初始化第一个字段

            /**
             * 联合体
             * 
             * 此联合体的特定用法是允许的。所有字段都是相同的类型（Field）。
             */
            union {
                // this specific usage of union is permitted. All fields are identical
                Field<LOCAL>        local;  // 局部变换字段
                Field<WORLD>        world;  // 世界变换字段
                Field<LOCAL_LO>     localTranslationLo;  // 精确局部平移字段
                Field<WORLD_LO>     worldTranslationLo;  // 精确世界平移字段
                Field<PARENT>       parent;  // 父节点字段
                Field<FIRST_CHILD>  firstChild;  // 第一个子节点字段
                Field<NEXT>         next;  // 下一个兄弟节点字段
                Field<PREV>         prev;  // 上一个兄弟节点字段
            };
        };

        /**
         * 下标运算符（非常量版本）
         * 
         * @param i 实例
         * @return 代理对象
         */
        UTILS_ALWAYS_INLINE Proxy operator[](Instance i) noexcept {
            return { *this, i };  // 返回代理对象
        }
        
        /**
         * 下标运算符（常量版本）
         * 
         * @param i 实例
         * @return 代理对象
         */
        UTILS_ALWAYS_INLINE const Proxy operator[](Instance i) const noexcept {
            return { const_cast<Sim&>(*this), i };  // 返回代理对象（需要 const_cast 因为 Proxy 构造函数接受非常量引用）
        }
    };

    Sim mManager;  // 组件管理器
    bool mLocalTransformTransactionOpen = false;  // 局部变换事务是否打开
    bool mAccurateTranslations = false;  // 是否启用精确变换
};

FILAMENT_DOWNCAST(TransformManager)

} // namespace filament

#endif // TNT_FILAMENT_COMPONENTS_TRANSFORMMANAGER_H
