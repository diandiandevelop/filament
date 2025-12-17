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

#ifndef TNT_FILAMENT_SAMPLE_MESH_ASSIMP_H
#define TNT_FILAMENT_SAMPLE_MESH_ASSIMP_H

namespace filament {
    class Engine;
    class VertexBuffer;
    class IndexBuffer;
    class Material;
    class MaterialInstance;
    class Renderable;
}

#include <unordered_map>
#include <map>
#include <vector>

#include <math/mat3.h>
#include <math/mat4.h>
#include <math/quat.h>
#include <math/vec3.h>

#include <utils/EntityManager.h>
#include <utils/Path.h>

#include <filamat/MaterialBuilder.h>
#include <filament/Color.h>
#include <filament/Box.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <assimp/scene.h>

/**
 * MeshAssimp - 基于Assimp库的网格加载类
 * 
 * 用于从各种3D模型格式（OBJ、FBX、GLTF等）加载网格数据到Filament。
 * 支持材质加载、纹理处理、PBR材质参数、层次结构变换等。
 */
class MeshAssimp {
public:
    using mat4f = filament::math::mat4f;      // 4x4浮点矩阵类型别名
    using half4 = filament::math::half4;      // 4分量半精度浮点向量类型别名
    using short4 = filament::math::short4;     // 4分量短整型向量类型别名
    using half2 = filament::math::half2;      // 2分量半精度浮点向量类型别名
    using ushort2 = filament::math::ushort2;   // 2分量无符号短整型向量类型别名
    
    /**
     * 构造函数
     * @param engine Filament引擎引用
     */
    explicit MeshAssimp(filament::Engine& engine);
    /** 析构函数：清理资源 */
    ~MeshAssimp();

    // This function takes over the ownership of `materials` to prevent crashes due to the
    // incorrect order of resource destruction.
    /**
     * 从文件加载网格
     * 
     * 此函数会接管materials的所有权，以防止因资源销毁顺序错误导致的崩溃。
     * 
     * @param path 模型文件路径
     * @param materials 材质实例映射表（会被此函数接管所有权）
     * @param overrideMaterial 是否覆盖所有材质为默认材质，默认false
     */
    void addFromFile(const utils::Path& path,
            std::map<std::string, filament::MaterialInstance*>& materials,
            bool overrideMaterial = false);

    /**
     * 获取所有可渲染实体
     * @return 可渲染实体列表
     */
    const std::vector<utils::Entity> getRenderables() const noexcept {
        return mRenderables;
    }

    //For use with normalizing coordinates
    /** 模型包围盒的最小边界（用于坐标归一化） */
    filament::math::float3 minBound = filament::math::float3(1.0f);
    /** 模型包围盒的最大边界（用于坐标归一化） */
    filament::math::float3 maxBound = filament::math::float3(-1.0f);
    /** 根实体（用于层次结构变换） */
    utils::Entity rootEntity;

private:
    /**
     * Part - 网格部分结构
     * 表示一个网格中使用相同材质的部分
     */
    struct Part {
        size_t offset;                      // 在索引缓冲区中的偏移量
        size_t count;                       // 索引数量
        std::string material;                // 材质名称
        filament::sRGBColor baseColor;      // 基础颜色（sRGB空间）
        float opacity;                      // 不透明度
        float metallic;                     // 金属度
        float roughness;                    // 粗糙度
        float reflectance;                  // 反射率
    };

    /**
     * Mesh - 网格结构
     * 表示一个完整的网格，可能包含多个部分
     */
    struct Mesh {
        size_t offset;                      // 在索引缓冲区中的偏移量
        size_t count;                       // 索引数量
        std::vector<Part> parts;             // 网格部分列表
        filament::Box aabb;                 // 轴对齐包围盒
        mat4f transform;                    // 局部变换矩阵
        mat4f accTransform;                  // 累积变换矩阵（包含父节点）
    };

    /**
     * Asset - 资源结构
     * 存储从文件加载的完整网格数据
     */
    struct Asset {
        utils::Path file;                   // 文件路径
        std::vector<uint32_t> indices;      // 索引数据
        std::vector<half4> positions;       // 顶点位置（half4格式，w分量用于其他用途）
        std::vector<short4> tangents;      // 切向量（打包为short4）
        std::vector<ushort2> texCoords0;    // 第一套UV坐标
        std::vector<ushort2> texCoords1;    // 第二套UV坐标
        bool snormUV0;                      // UV0是否为有符号归一化格式
        bool snormUV1;                      // UV1是否为有符号归一化格式
        std::vector<Mesh> meshes;           // 网格列表
        std::vector<int> parents;           // 父节点索引列表（-1表示根节点）
    };

    /**
     * 从文件设置资源数据
     * @param asset 输出的资源数据
     * @param outMaterials 输出的材质实例映射表
     * @return 是否成功
     */
    bool setFromFile(Asset& asset, std::map<std::string, filament::MaterialInstance*>& outMaterials);

    /**
     * 处理GLTF材质
     * @param scene Assimp场景对象
     * @param material Assimp材质对象
     * @param materialName 材质名称
     * @param dirName 材质文件所在目录
     * @param outMaterials 输出的材质实例映射表
     */
    void processGLTFMaterial(const aiScene* scene, const aiMaterial* material,
            const std::string& materialName, const std::string& dirName,
            std::map<std::string, filament::MaterialInstance*>& outMaterials) const;

    /**
     * 处理场景节点（模板函数，根据UV格式优化）
     * @param asset 资源数据
     * @param outMaterials 输出的材质实例映射表
     * @param scene Assimp场景对象
     * @param isGLTF 是否为GLTF格式
     * @param deep 当前深度
     * @param matCount 材质计数器
     * @param node 当前节点
     * @param parentIndex 父节点索引
     * @param depth 输出的最大深度
     */
    template<bool SNORMUV0S, bool SNORMUV1S>
    void processNode(Asset& asset,
                     std::map<std::string, filament::MaterialInstance*>& outMaterials,
                     const aiScene *scene,
                     bool isGLTF,
                     size_t deep,
                     size_t matCount,
                     const aiNode *node,
                     int parentIndex,
                     size_t &depth) const;

    /**
     * 创建1x1像素的纹理（用于默认纹理）
     * @param textureData 纹理像素数据（RGBA格式，32位）
     * @return 纹理对象指针
     */
    filament::Texture* createOneByOneTexture(uint32_t textureData);
    
    filament::Engine& mEngine;                                    // Filament引擎引用
    filament::VertexBuffer* mVertexBuffer = nullptr;              // 顶点缓冲区
    filament::IndexBuffer* mIndexBuffer = nullptr;                // 索引缓冲区

    filament::Material* mDefaultColorMaterial = nullptr;          // 默认颜色材质
    filament::Material* mDefaultTransparentColorMaterial = nullptr; // 默认透明颜色材质
    mutable std::unordered_map<uint64_t, filament::Material*> mGltfMaterialCache; // GLTF材质缓存（按配置哈希）
    std::map<std::string, filament::MaterialInstance*> mMaterialInstances; // 材质实例映射表

    filament::Texture* mDefaultMap = nullptr;                      // 默认纹理（白色）
    filament::Texture* mDefaultNormalMap = nullptr;               // 默认法线贴图（(0.5, 0.5, 1.0)）
    float mDefaultMetallic = 0.0f;                                 // 默认金属度
    float mDefaultRoughness = 0.4f;                               // 默认粗糙度
    filament::sRGBColor mDefaultEmissive = filament::sRGBColor({0.0f, 0.0f, 0.0f}); // 默认自发光颜色

    std::vector<utils::Entity> mRenderables;                     // 可渲染实体列表

    std::vector<filament::Texture*> mTextures;                    // 纹理列表（用于资源管理）
};

#endif // TNT_FILAMENT_SAMPLE_MESH_ASSIMP_H
