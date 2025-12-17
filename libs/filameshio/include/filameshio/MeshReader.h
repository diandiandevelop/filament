/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef TNT_FILAMENT_FILAMESHIO_MESHREADER_H
#define TNT_FILAMENT_FILAMESHIO_MESHREADER_H

#include <utils/compiler.h>
#include <utils/Entity.h>
#include <utils/CString.h>

namespace filament {
    class Engine;
    class VertexBuffer;
    class IndexBuffer;
    class MaterialInstance;
}

namespace utils {
    class Path;
}

namespace filamesh {


/**
 * MeshReader - 网格读取器类
 * 
 * 此API可用于读取以"filamesh"格式存储的网格，该格式由同名命令行工具生成。
 * 此文件格式在Filament发行版的"docs/filamesh.md"中有文档说明。
 * 
 * This API can be used to read meshes stored in the "filamesh" format produced
 * by the command line tool of the same name. This file format is documented in
 * "docs/filamesh.md" in the Filament distribution.
 */
class UTILS_PUBLIC MeshReader {
public:
    /**
     * 回调函数类型
     * 
     * 用于在缓冲区不再需要时释放内存的回调函数。
     * 
     * @param buffer 要释放的缓冲区指针
     * @param size 缓冲区大小
     * @param user 用户数据指针
     */
    using Callback = void(*)(void* buffer, size_t size, void* user);

    /**
     * MaterialRegistry - 材质注册表类
     * 
     * 用于跟踪和管理材质实例的类。
     * 可以将命名的材质实例注册到注册表中，然后在加载网格时通过名称查找对应的材质。
     */
    class MaterialRegistry {
    public:
         /**
          * 默认构造函数
          */
         MaterialRegistry();
         
         /**
          * 拷贝构造函数
          * 
          * @param rhs 源对象
          */
         MaterialRegistry(const MaterialRegistry& rhs);
         
         /**
          * 拷贝赋值运算符
          * 
          * @param rhs 源对象
          * @return 当前对象的引用
          */
         MaterialRegistry& operator=(const MaterialRegistry& rhs);
         
         /**
          * 析构函数
          */
         ~MaterialRegistry();
         
         /**
          * 移动构造函数
          * 
          * @param rhs 源对象（将被移动）
          */
         MaterialRegistry(MaterialRegistry&&);
         
         /**
          * 移动赋值运算符
          * 
          * @param rhs 源对象（将被移动）
          * @return 当前对象的引用
          */
         MaterialRegistry& operator=(MaterialRegistry&&);

         /**
          * 根据名称获取材质实例
          * 
          * @param name 材质名称
          * @return 材质实例指针，如果未找到则返回nullptr
          */
         filament::MaterialInstance* getMaterialInstance(const utils::CString& name);

         /**
          * 注册材质实例
          * 
          * 将材质实例与名称关联并注册到注册表中。
          * 如果该名称已存在，将替换原有的材质实例。
          * 
          * @param name 材质名称
          * @param materialInstance 材质实例指针
          */
         void registerMaterialInstance(const utils::CString& name,
                 filament::MaterialInstance* materialInstance);

         /**
          * 注销材质实例
          * 
          * 从注册表中移除指定名称的材质实例。
          * 
          * @param name 要注销的材质名称
          */
         void unregisterMaterialInstance(const utils::CString& name);

         /**
          * 注销所有材质实例
          * 
          * 清空注册表中的所有材质实例。
          */
         void unregisterAll();

         /**
          * 获取已注册的材质实例数量
          * 
          * @return 已注册的材质实例数量
          */
         size_t numRegistered() const noexcept;

         /**
          * 获取所有已注册的材质实例和名称
          * 
          * 将已注册的材质实例和名称分别填充到提供的数组中。
         * 调用者需要确保数组大小至少为 numRegistered()。
          * 
          * @param materialList 材质实例指针数组（输出）
          * @param materialNameList 材质名称数组（输出）
          */
         void getRegisteredMaterials(filament::MaterialInstance** materialList,
                 utils::CString* materialNameList) const;

         /**
          * 获取所有已注册的材质实例
          * 
          * 将已注册的材质实例填充到提供的数组中。
          * 调用者需要确保数组大小至少为 numRegistered()。
          * 
          * @param materialList 材质实例指针数组（输出）
          */
         void getRegisteredMaterials(filament::MaterialInstance** materialList) const;

         /**
          * 获取所有已注册的材质名称
          * 
          * 将已注册的材质名称填充到提供的数组中。
          * 调用者需要确保数组大小至少为 numRegistered()。
          * 
          * @param materialNameList 材质名称数组（输出）
          */
         void getRegisteredMaterialNames(utils::CString* materialNameList) const;

     private:
         struct MaterialRegistryImpl;  // 前向声明：实现细节
         MaterialRegistryImpl* mImpl;   // 实现指针（PIMPL模式）
    };

    /**
     * Mesh结构体
     * 
     * 存储从filamesh文件加载的网格数据。
     */
    struct Mesh {
        utils::Entity renderable;                    // 可渲染实体
        filament::VertexBuffer* vertexBuffer = nullptr;  // 顶点缓冲区指针
        filament::IndexBuffer* indexBuffer = nullptr;     // 索引缓冲区指针
    };

    /**
     * 从指定文件加载filamesh可渲染对象
     * 
     * 执行步骤：
     * 1. 读取filamesh文件
     * 2. 解析文件头、顶点数据、索引数据
     * 3. 从材质注册表中查找材质实例
     * 4. 如果找不到匹配的材质，使用默认材质
     * 5. 创建顶点缓冲区和索引缓冲区
     * 6. 创建可渲染对象并返回
     * 
     * 材质匹配规则：
     * - 如果filamesh文件中的材质名称在注册表中找到，使用注册表中的材质
     * - 如果找不到匹配的材质，使用默认材质
     * - 可以通过在注册表中添加名为"DefaultMaterial"的材质来覆盖默认材质
     * 
     * @param engine Filament引擎指针
     * @param path 文件路径
     * @param materials 材质注册表
     * @return 加载的网格数据
     */
    static Mesh loadMeshFromFile(filament::Engine* engine,
            const utils::Path& path,
            MaterialRegistry& materials);

    /**
     * 从内存缓冲区加载filamesh可渲染对象（使用材质注册表）
     * 
     * 执行步骤：
     * 1. 解析内存缓冲区中的filamesh数据
     * 2. 从材质注册表中查找材质实例
     * 3. 如果找不到匹配的材质，使用默认材质
     * 4. 创建顶点缓冲区和索引缓冲区
     * 5. 创建可渲染对象并返回
     * 
     * 材质匹配规则：
     * - 如果filamesh文件中的材质名称在注册表中找到，使用注册表中的材质
     * - 如果找不到匹配的材质，使用默认材质
     * - 可以通过在注册表中添加名为"DefaultMaterial"的材质来覆盖默认材质
     * 
     * @param engine Filament引擎指针
     * @param data 内存缓冲区指针（包含filamesh数据）
     * @param destructor 析构回调函数（当缓冲区不再需要时调用，用于释放内存）
     * @param user 用户数据指针（传递给析构回调函数）
     * @param materials 材质注册表
     * @return 加载的网格数据
     */
    static Mesh loadMeshFromBuffer(filament::Engine* engine,
            void const* data, Callback destructor, void* user,
            MaterialRegistry& materials);

    /**
     * 从内存缓冲区加载filamesh可渲染对象（使用默认材质）
     * 
     * 执行步骤：
     * 1. 解析内存缓冲区中的filamesh数据
     * 2. 将所有图元分配指定的默认材质
     * 3. 创建顶点缓冲区和索引缓冲区
     * 4. 创建可渲染对象并返回
     * 
     * 注意：此版本不使用材质注册表，所有图元都使用同一个默认材质。
     * 
     * @param engine Filament引擎指针
     * @param data 内存缓冲区指针（包含filamesh数据）
     * @param destructor 析构回调函数（当缓冲区不再需要时调用，用于释放内存）
     * @param user 用户数据指针（传递给析构回调函数）
     * @param defaultMaterial 默认材质实例指针（所有图元都使用此材质）
     * @return 加载的网格数据
     */
    static Mesh loadMeshFromBuffer(filament::Engine* engine,
            void const* data, Callback destructor, void* user,
            filament::MaterialInstance* defaultMaterial);
};

} // namespace filamesh

#endif // TNT_FILAMENT_FILAMESHIO_MESHREADER_H
