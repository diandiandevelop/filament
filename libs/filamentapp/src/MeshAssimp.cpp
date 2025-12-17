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

/**
 * OpenGL纹理过滤和包装模式常量定义
 * 
 * 这些常量用于将Assimp的OpenGL纹理参数转换为Filament的纹理采样器参数。
 * Assimp使用OpenGL常量来表示纹理过滤和包装模式。
 */
#define GL_NEAREST                        0x2600  // 最近邻过滤
#define GL_LINEAR                         0x2601  // 线性过滤
#define GL_NEAREST_MIPMAP_NEAREST         0x2700  // 最近邻Mipmap，最近邻过滤
#define GL_LINEAR_MIPMAP_NEAREST          0x2701  // 线性Mipmap，最近邻过滤
#define GL_NEAREST_MIPMAP_LINEAR          0x2702  // 最近邻Mipmap，线性过滤
#define GL_LINEAR_MIPMAP_LINEAR           0x2703  // 线性Mipmap，线性过滤
#define GL_TEXTURE_MAG_FILTER             0x2800  // 放大过滤模式
#define GL_TEXTURE_MIN_FILTER             0x2801  // 缩小过滤模式
#define GL_TEXTURE_WRAP_S                 0x2802  // S方向包装模式
#define GL_TEXTURE_WRAP_T                 0x2803  // T方向包装模式

#include <filamentapp/MeshAssimp.h>

#include <stdlib.h>
#include <string.h>

#include <array>
#include <iostream>

#include <filament/Color.h>
#include <filament/VertexBuffer.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>

#include <math/norm.h>
#include <math/vec3.h>
#include <math/TVecHelpers.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/pbrmaterial.h>

#include <stb_image.h>

#include <backend/DriverEnums.h>

#include "generated/resources/filamentapp.h"

using namespace filament;
using namespace filamat;
using namespace filament::math;
using namespace utils;

/**
 * AlphaMode - 透明度模式枚举
 */
enum class AlphaMode : uint8_t {
    OPAQUE,      // 不透明
    MASKED,      // 遮罩（Alpha测试）
    TRANSPARENT  // 透明（Alpha混合）
};

/**
 * MaterialConfig - 材质配置结构体
 * 用于描述GLTF材质的各种属性
 */
struct MaterialConfig {
    bool doubleSided = false;              // 是否双面渲染
    bool unlit = false;                    // 是否无光照（自发光）
    bool hasVertexColors = false;          // 是否有顶点颜色
    AlphaMode alphaMode = AlphaMode::OPAQUE; // 透明度模式
    float maskThreshold = 0.5f;            // Alpha遮罩阈值
    uint8_t baseColorUV = 0;              // 基础颜色贴图使用的UV通道
    uint8_t metallicRoughnessUV = 0;       // 金属度粗糙度贴图使用的UV通道
    uint8_t emissiveUV = 0;                // 自发光贴图使用的UV通道
    uint8_t aoUV = 0;                      // 环境光遮蔽贴图使用的UV通道
    uint8_t normalUV = 0;                  // 法线贴图使用的UV通道

    /**
     * 获取最大UV通道索引
     * @return 最大UV通道索引
     */
    uint8_t maxUVIndex() {
        return std::max({baseColorUV, metallicRoughnessUV, emissiveUV, aoUV, normalUV});
    }
};

/**
 * 将布尔值追加到位掩码中
 * 
 * 执行步骤：
 * 1. 将位掩码左移1位
 * 2. 将布尔值追加到最低位
 * 
 * @param bitmask 位掩码的引用（会被修改）
 * @param b 要追加的布尔值
 */
void appendBooleanToBitMask(uint64_t &bitmask, bool b) {
    bitmask <<= 1;
    bitmask |= b;
}

/**
 * 计算材质配置的哈希值
 * 
 * 执行步骤：
 * 1. 将maskThreshold复制到位掩码的低位
 * 2. 将各种布尔配置追加到位掩码中
 * 3. 返回哈希值（用于材质缓存）
 * 
 * @param config 材质配置
 * @return 64位哈希值
 */
uint64_t hashMaterialConfig(MaterialConfig config) {
    uint64_t bitmask = 0;
    memcpy(&bitmask, &config.maskThreshold, sizeof(config.maskThreshold));
    appendBooleanToBitMask(bitmask, config.doubleSided);
    appendBooleanToBitMask(bitmask, config.unlit);
    appendBooleanToBitMask(bitmask, config.hasVertexColors);
    appendBooleanToBitMask(bitmask, config.alphaMode == AlphaMode::OPAQUE);
    appendBooleanToBitMask(bitmask, config.alphaMode == AlphaMode::MASKED);
    appendBooleanToBitMask(bitmask, config.alphaMode == AlphaMode::TRANSPARENT);
    appendBooleanToBitMask(bitmask, config.baseColorUV == 0);
    appendBooleanToBitMask(bitmask, config.metallicRoughnessUV == 0);
    appendBooleanToBitMask(bitmask, config.emissiveUV == 0);
    appendBooleanToBitMask(bitmask, config.aoUV == 0);
    appendBooleanToBitMask(bitmask, config.normalUV == 0);
    return bitmask;
}

/**
 * 根据材质配置生成着色器代码
 * 
 * 执行步骤：
 * 1. 生成UV坐标获取代码（根据配置的UV通道）
 * 2. 如果是有光照材质，生成法线贴图采样代码
 * 3. 生成基础颜色采样代码
 * 4. 如果是透明模式，添加预乘Alpha代码
 * 5. 如果是有光照材质，生成PBR参数采样代码（金属度、粗糙度、AO、自发光）
 * 
 * @param config 材质配置
 * @return 生成的着色器代码字符串
 */
std::string shaderFromConfig(MaterialConfig config) {
    std::string shader = R"SHADER(
        void material(inout MaterialInputs material) {
    )SHADER";

    shader += "float2 normalUV = getUV" + std::to_string(config.normalUV) + "();\n";
    shader += "float2 baseColorUV = getUV" + std::to_string(config.baseColorUV) + "();\n";
    shader += "float2 metallicRoughnessUV = getUV" + std::to_string(config.metallicRoughnessUV) + "();\n";
    shader += "float2 aoUV = getUV" + std::to_string(config.aoUV) + "();\n";
    shader += "float2 emissiveUV = getUV" + std::to_string(config.emissiveUV) + "();\n";

    if (!config.unlit) {
        shader += R"SHADER(
            material.normal = texture(materialParams_normalMap, normalUV).xyz * 2.0 - 1.0;
            material.normal.y = -material.normal.y;
        )SHADER";
    }

    shader += R"SHADER(
        prepareMaterial(material);
        material.baseColor = texture(materialParams_baseColorMap, baseColorUV);
        material.baseColor *= materialParams.baseColorFactor;
    )SHADER";

    if (config.alphaMode == AlphaMode::TRANSPARENT) {
        shader += R"SHADER(
            material.baseColor.rgb *= material.baseColor.a;
        )SHADER";
    }

    if (!config.unlit) {
        shader += R"SHADER(
            vec4 metallicRoughness = texture(materialParams_metallicRoughnessMap, metallicRoughnessUV);
            material.roughness = materialParams.roughnessFactor * metallicRoughness.g;
            material.metallic = materialParams.metallicFactor * metallicRoughness.b;
            material.ambientOcclusion = texture(materialParams_aoMap, aoUV).r;
            material.emissive.rgb = texture(materialParams_emissiveMap, emissiveUV).rgb;
            material.emissive.rgb *= materialParams.emissiveFactor.rgb;
            material.emissive.a = 0.0;
        )SHADER";
    }

    shader += "}\n";
    return shader;
}

/**
 * 根据材质配置创建材质对象
 * 
 * 执行步骤：
 * 1. 从配置生成着色器代码
 * 2. 初始化材质构建器
 * 3. 设置材质名称、着色器代码、双面渲染
 * 4. 要求UV0属性（如果使用UV1则也要求）
 * 5. 定义所有材质参数（贴图和因子）
 * 6. 根据Alpha模式设置混合模式
 * 7. 设置着色模式（有光照/无光照）
 * 8. 构建材质包并创建材质对象
 * 
 * @param engine Filament引擎引用
 * @param config 材质配置
 * @return 材质对象指针
 */
Material* createMaterialFromConfig(Engine& engine, MaterialConfig config ) {
    std::string shader = shaderFromConfig(config);
    MaterialBuilder::init();
    MaterialBuilder builder;
    builder
            .name("material")
            .material(shader.c_str())
            .doubleSided(config.doubleSided)
            .require(VertexAttribute::UV0)
            .parameter("baseColorMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("baseColorFactor", MaterialBuilder::UniformType::FLOAT4)
            .parameter("metallicRoughnessMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("aoMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("emissiveMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("normalMap", MaterialBuilder::SamplerType::SAMPLER_2D)
            .parameter("metallicFactor", MaterialBuilder::UniformType::FLOAT)
            .parameter("roughnessFactor", MaterialBuilder::UniformType::FLOAT)
            .parameter("normalScale", MaterialBuilder::UniformType::FLOAT)
            .parameter("aoStrength", MaterialBuilder::UniformType::FLOAT)
            .parameter("emissiveFactor", MaterialBuilder::UniformType::FLOAT3);

    // 如果使用了UV1，则要求UV1属性
    if (config.maxUVIndex() > 0) {
        builder.require(VertexAttribute::UV1);
    }

    // 根据Alpha模式设置混合模式
    switch(config.alphaMode) {
        case AlphaMode::MASKED : builder.blending(MaterialBuilder::BlendingMode::MASKED);
            builder.maskThreshold(config.maskThreshold);
            break;
        case AlphaMode::TRANSPARENT : builder.blending(MaterialBuilder::BlendingMode::TRANSPARENT);
            break;
        default : builder.blending(MaterialBuilder::BlendingMode::OPAQUE);
    }

    // 设置着色模式
    builder.shading(config.unlit ? Shading::UNLIT : Shading::LIT);

    // 构建材质包并创建材质
    Package pkg = builder.build(engine.getJobSystem());
    return Material::Builder().package(pkg.getData(), pkg.getSize()).build(engine);
}

/**
 * 创建1x1像素纹理实现
 * 
 * 执行步骤：
 * 1. 分配内存存储单个像素数据
 * 2. 设置像素值
 * 3. 创建纹理对象（RGBA8格式，1x1大小）
 * 4. 设置纹理图像数据
 * 5. 生成Mipmap
 * 6. 返回纹理对象
 * 
 * @param textureData 纹理像素数据（RGBA格式，32位）
 * @return 纹理对象指针
 */
Texture* MeshAssimp::createOneByOneTexture(uint32_t pixel) {
    // 分配单个像素的内存
    uint32_t *textureData = (uint32_t *) malloc(sizeof(uint32_t));
    *textureData = pixel;

    // 创建1x1的RGBA8纹理
    Texture *texturePtr = Texture::Builder()
            .width(uint32_t(1))
            .height(uint32_t(1))
            .levels(0xff)  // 自动生成所有Mipmap级别
            .format(Texture::InternalFormat::RGBA8)
            .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
            .build(mEngine);

    // 创建像素缓冲区描述符（使用free作为释放回调）
    Texture::PixelBufferDescriptor defaultNormalBuffer(textureData,
            size_t(1 * 1 * 4),
            Texture::Format::RGBA,
            Texture::Type::UBYTE,
            (Texture::PixelBufferDescriptor::Callback) &free);

    // 设置纹理图像并生成Mipmap
    texturePtr->setImage(mEngine, 0, std::move(defaultNormalBuffer));
    texturePtr->generateMipmaps(mEngine);

    return texturePtr;
}

/**
 * 获取场景节点中UV坐标的最小值和最大值
 * 
 * 递归遍历节点及其子节点，查找指定UV通道的最小和最大坐标值。
 * 用于判断UV坐标是否在有符号归一化范围内（[-1, 1]）。
 * 
 * 执行步骤：
 * 1. 遍历当前节点的所有网格
 * 2. 检查网格是否有指定UV通道的纹理坐标
 * 3. 更新最小和最大UV值
 * 4. 递归处理所有子节点
 * 
 * @param scene Assimp场景对象
 * @param node 当前节点
 * @param minUV 输出的最小UV值（会被修改）
 * @param maxUV 输出的最大UV值（会被修改）
 * @param uvIndex UV通道索引（0或1）
 */
void getMinMaxUV(const aiScene *scene, const aiNode* node, float2 &minUV,
        float2 &maxUV, uint32_t uvIndex) {
    for (size_t i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        if (!mesh->HasTextureCoords(uvIndex)) {
            continue;
        }
        const float3* uv = reinterpret_cast<const float3*>(mesh->mTextureCoords[uvIndex]);
        const size_t numVertices = mesh->mNumVertices;
        const size_t numFaces = mesh->mNumFaces;
        if (numVertices == 0 || numFaces == 0) {
            continue;
        }
        if (uv) {
            for (size_t j = 0; j < numVertices; j++) {
                minUV = min(uv[j].xy, minUV);
                maxUV = max(uv[j].xy, maxUV);
            }
        }
    }
    // 递归处理子节点
    for (size_t i = 0 ; i < node->mNumChildren ; ++i) {
        getMinMaxUV(scene, node->mChildren[i], minUV, maxUV, uvIndex);
    }
}

/**
 * 转换UV坐标为ushort2格式（模板函数）
 * 
 * 根据模板参数决定使用有符号归一化格式还是半精度浮点格式。
 * 
 * 执行步骤：
 * 1. 如果SNORMUVS为true，使用packSnorm16打包为有符号归一化short2
 * 2. 如果SNORMUVS为false，转换为半精度浮点half2
 * 3. 使用bit_cast转换为ushort2格式
 * 
 * @param uv 输入的UV坐标（float2）
 * @return 转换后的UV坐标（ushort2）
 */
template<bool SNORMUVS>
static ushort2 convertUV(float2 uv) {
    if (SNORMUVS) {
        // 有符号归一化格式：打包到short2
        short2 uvshort(packSnorm16(uv));
        return bit_cast<ushort2>(uvshort);
    } else {
        // 半精度浮点格式：转换为half2
        half2 uvhalf(uv);
        return bit_cast<ushort2>(uvhalf);
    }
}

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 创建默认纹理（白色纹理和默认法线贴图）
 * 2. 创建默认颜色材质（不透明）
 * 3. 设置默认材质参数（基础颜色、金属度、粗糙度、反射率）
 * 4. 创建默认透明颜色材质
 * 5. 设置默认透明材质参数
 */
MeshAssimp::MeshAssimp(Engine& engine) : mEngine(engine) {
    // 创建默认纹理：白色纹理（0xffffffff = 白色RGBA）和默认法线贴图（0xffff8080 = (1, 1, 0.5, 1)）
    mDefaultMap = createOneByOneTexture(0xffffffff);
    mDefaultNormalMap = createOneByOneTexture(0xffff8080);

    // 创建默认颜色材质（不透明）
    mDefaultColorMaterial = Material::Builder()
            .package(FILAMENTAPP_AIDEFAULTMAT_DATA, FILAMENTAPP_AIDEFAULTMAT_SIZE)
            .build(mEngine);

    // 设置默认材质参数
    mDefaultColorMaterial->setDefaultParameter("baseColor",   RgbType::LINEAR, float3{0.8});
    mDefaultColorMaterial->setDefaultParameter("metallic",    0.0f);
    mDefaultColorMaterial->setDefaultParameter("roughness",   0.4f);
    mDefaultColorMaterial->setDefaultParameter("reflectance", 0.5f);

    // 创建默认透明颜色材质
    mDefaultTransparentColorMaterial = Material::Builder()
            .package(FILAMENTAPP_AIDEFAULTTRANS_DATA, FILAMENTAPP_AIDEFAULTTRANS_SIZE)
            .build(mEngine);

    // 设置默认透明材质参数
    mDefaultTransparentColorMaterial->setDefaultParameter("baseColor", RgbType::LINEAR, float3{0.8});
    mDefaultTransparentColorMaterial->setDefaultParameter("metallic",  0.0f);
    mDefaultTransparentColorMaterial->setDefaultParameter("roughness", 0.4f);
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 销毁所有可渲染实体
 * 2. 销毁顶点缓冲区和索引缓冲区
 * 3. 销毁所有材质实例
 * 4. 销毁默认材质
 * 5. 销毁GLTF材质缓存中的材质
 * 6. 销毁默认纹理
 * 7. 销毁所有纹理
 * 8. 销毁实体本身
 */
MeshAssimp::~MeshAssimp() {
    // 销毁所有可渲染实体
    for (Entity renderable : mRenderables) {
        mEngine.destroy(renderable);
    }
    // 销毁缓冲区
    mEngine.destroy(mVertexBuffer);
    mEngine.destroy(mIndexBuffer);
    // 销毁材质实例
    for (auto& item : mMaterialInstances) {
        mEngine.destroy(item.second);
    }
    // 销毁默认材质
    mEngine.destroy(mDefaultColorMaterial);
    mEngine.destroy(mDefaultTransparentColorMaterial);
    // 销毁GLTF材质缓存
    for (auto& item : mGltfMaterialCache) {
        auto material = item.second;
        mEngine.destroy(material);
    }
    // 销毁默认纹理
    mEngine.destroy(mDefaultNormalMap);
    mEngine.destroy(mDefaultMap);
    // 销毁所有纹理
    for (Texture* texture : mTextures) {
        mEngine.destroy(texture);
    }
    // destroy the Entities itself
    // 销毁实体本身
    EntityManager::get().destroy(mRenderables.size(), mRenderables.data());
}

/**
 * State - 状态包装结构体（模板）
 * 
 * 用于包装数据向量，提供缓冲区描述符所需的接口。
 * 在缓冲区使用完毕后自动释放内存。
 */
template<typename T>
struct State {
    std::vector<T> state;
    explicit State(std::vector<T>&& state) : state(std::move(state)) { }
    /**
     * 释放回调函数
     * @param buffer 缓冲区指针
     * @param size 缓冲区大小
     * @param user 用户数据（State对象指针）
     */
    static void free(void* buffer, size_t size, void* user) {
        auto* const that = static_cast<State<T>*>(user);
        delete that;
    }
    size_t size() const { return state.size() * sizeof(T); }
    T const * data() const { return state.data(); }
};

//TODO: Remove redundant method from sample_full_pbr
/**
 * 从文件路径加载纹理
 * 
 * 执行步骤：
 * 1. 检查文件路径是否为空
 * 2. 验证文件是否存在
 * 3. 根据sRGB和Alpha通道选择纹理格式
 * 4. 使用stbi_load加载图像数据
 * 5. 创建纹理对象
 * 6. 设置纹理图像数据
 * 7. 生成Mipmap
 * 
 * @param engine Filament引擎指针
 * @param filePath 纹理文件路径
 * @param map 输出的纹理指针
 * @param sRGB 是否为sRGB格式（用于颜色贴图）
 * @param hasAlpha 是否包含Alpha通道
 */
static void loadTexture(Engine *engine, const std::string &filePath, Texture **map,
        bool sRGB, bool hasAlpha) {

    if (!filePath.empty()) {
        Path path(filePath);
        if (path.exists()) {
            int w, h, n;
            int numChannels = hasAlpha ? 4 : 3;

            Texture::InternalFormat inputFormat;
            if (sRGB) {
                inputFormat = hasAlpha ? Texture::InternalFormat::SRGB8_A8 : Texture::InternalFormat::SRGB8;
            } else {
                inputFormat = hasAlpha ? Texture::InternalFormat::RGBA8 : Texture::InternalFormat::RGB8;
            }

            Texture::Format outputFormat = hasAlpha ? Texture::Format::RGBA : Texture::Format::RGB;

            uint8_t *data = stbi_load(path.getAbsolutePath().c_str(), &w, &h, &n, numChannels);
            if (data != nullptr) {
                *map = Texture::Builder()
                        .width(uint32_t(w))
                        .height(uint32_t(h))
                        .levels(0xff)
                        .format(inputFormat)
                        .build(*engine);

                Texture::PixelBufferDescriptor buffer(data,
                        size_t(w * h * numChannels),
                        outputFormat,
                        Texture::Type::UBYTE,
                        (Texture::PixelBufferDescriptor::Callback) &stbi_image_free);

                (*map)->setImage(*engine, 0, std::move(buffer));
                (*map)->generateMipmaps(*engine);
            } else {
                std::cout << "The texture " << path << " could not be loaded" << std::endl;
            }
        } else {
            std::cout << "The texture " << path << " does not exist" << std::endl;
        }
    }
}

/**
 * 从嵌入纹理加载纹理（Assimp嵌入纹理）
 * 
 * 执行步骤：
 * 1. 根据Alpha通道确定通道数
 * 2. 根据sRGB和Alpha通道选择纹理格式
 * 3. 使用stbi_load_from_memory从内存加载图像数据
 * 4. 创建纹理对象
 * 5. 设置纹理图像数据
 * 6. 生成Mipmap
 * 
 * @param engine Filament引擎指针
 * @param embeddedTexture Assimp嵌入纹理对象
 * @param map 输出的纹理指针
 * @param sRGB 是否为sRGB格式
 * @param hasAlpha 是否包含Alpha通道
 */
void loadEmbeddedTexture(Engine *engine, aiTexture *embeddedTexture, Texture **map,
        bool sRGB, bool hasAlpha) {

    int w, h, n;
    int numChannels = hasAlpha ? 4 : 3;

    // 根据sRGB和Alpha通道选择内部格式
    Texture::InternalFormat inputFormat;
    if (sRGB) {
        inputFormat = hasAlpha ? Texture::InternalFormat::SRGB8_A8 : Texture::InternalFormat::SRGB8;
    } else {
        inputFormat = hasAlpha ? Texture::InternalFormat::RGBA8 : Texture::InternalFormat::RGB8;
    }

    Texture::Format outputFormat = hasAlpha ? Texture::Format::RGBA : Texture::Format::RGB;

    // 从内存加载图像数据（Assimp嵌入纹理）
    uint8_t *data = stbi_load_from_memory((unsigned char *) embeddedTexture->pcData,
            embeddedTexture->mWidth, &w, &h, &n, numChannels);

    // 创建纹理对象
    *map = Texture::Builder()
            .width(uint32_t(w))
            .height(uint32_t(h))
            .levels(0xff)
            .format(inputFormat)
            .build(*engine);

    // 设置纹理图像数据（使用free作为释放回调）
    Texture::PixelBufferDescriptor defaultBuffer(data,
            size_t(w * h * numChannels),
            outputFormat,
            Texture::Type::UBYTE,
            (Texture::PixelBufferDescriptor::Callback) &free);

    (*map)->setImage(*engine, 0, std::move(defaultBuffer));
    (*map)->generateMipmaps(*engine);
}

// Takes a texture filename and returns the index of the embedded texture,
// -1 if the texture is not embedded
/**
 * 从纹理文件名获取嵌入纹理索引
 * 
 * Assimp使用"*N"格式表示嵌入纹理，其中N是纹理索引。
 * 
 * 执行步骤：
 * 1. 检查路径是否以'*'开头
 * 2. 检查后续字符是否全为数字
 * 3. 如果是，解析数字作为纹理索引
 * 4. 否则返回-1（非嵌入纹理）
 * 
 * @param path Assimp字符串路径
 * @return 嵌入纹理索引，如果不是嵌入纹理则返回-1
 */
int32_t getEmbeddedTextureId(const aiString& path) {
    const char *pathStr = path.C_Str();
    if (path.length >= 2 && pathStr[0] == '*') {
        // 检查后续字符是否全为数字
        for (int i = 1; i < path.length; i++) {
            if (!isdigit(pathStr[i])) {
                return -1;
            }
        }
        return std::atoi(pathStr + 1); // NOLINT
    }
    return -1;
}

/**
 * 将Assimp纹理映射模式转换为Filament纹理采样器包装模式
 * 
 * @param mapMode Assimp纹理映射模式
 * @return Filament纹理采样器包装模式
 */
TextureSampler::WrapMode aiToFilamentMapMode(aiTextureMapMode mapMode) {
    switch(mapMode) {
        case aiTextureMapMode_Clamp :
            return TextureSampler::WrapMode::CLAMP_TO_EDGE;
        case aiTextureMapMode_Mirror :
            return TextureSampler::WrapMode::MIRRORED_REPEAT;
        default:
            return TextureSampler::WrapMode::REPEAT;
    }
}

/**
 * 将Assimp最小过滤模式转换为Filament纹理采样器最小过滤模式
 * 
 * @param aiMinFilter Assimp最小过滤模式（OpenGL常量）
 * @return Filament纹理采样器最小过滤模式
 */
TextureSampler::MinFilter aiMinFilterToFilament(unsigned int aiMinFilter) {
    switch(aiMinFilter) {
        case GL_NEAREST: return TextureSampler::MinFilter::NEAREST;
        case GL_LINEAR: return TextureSampler::MinFilter::LINEAR;
        case GL_NEAREST_MIPMAP_NEAREST: return TextureSampler::MinFilter::NEAREST_MIPMAP_NEAREST;
        case GL_LINEAR_MIPMAP_NEAREST: return TextureSampler::MinFilter::LINEAR_MIPMAP_NEAREST;
        case GL_NEAREST_MIPMAP_LINEAR: return TextureSampler::MinFilter::NEAREST_MIPMAP_LINEAR;
        case GL_LINEAR_MIPMAP_LINEAR: return TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
        default: return TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR;
    }
}

/**
 * 将Assimp最大过滤模式转换为Filament纹理采样器最大过滤模式
 * 
 * @param aiMagFilter Assimp最大过滤模式（OpenGL常量）
 * @return Filament纹理采样器最大过滤模式
 */
TextureSampler::MagFilter aiMagFilterToFilament(unsigned int aiMagFilter) {
    switch(aiMagFilter) {
        case GL_NEAREST: return TextureSampler::MagFilter::NEAREST;
        default: return TextureSampler::MagFilter::LINEAR;
    }
}

// TODO: Change this to a member function (requires some alteration of cmakelsts.txt)
/**
 * 从路径设置纹理到材质实例
 * 
 * 执行步骤：
 * 1. 转换Assimp过滤模式为Filament过滤模式
 * 2. 创建纹理采样器（包含过滤和包装模式）
 * 3. 检查是否为嵌入纹理
 * 4. 根据参数名确定是否为sRGB和是否有Alpha通道
 * 5. 加载纹理（嵌入或文件）
 * 6. 将纹理设置到材质实例的指定参数
 * 
 * @param scene Assimp场景对象
 * @param engine Filament引擎指针
 * @param textures 纹理列表（用于资源管理）
 * @param textureFile 纹理文件路径或嵌入纹理标识
 * @param materialName 材质名称
 * @param textureDirectory 纹理文件目录
 * @param mapMode 纹理映射模式（包装模式）
 * @param parameterName 材质参数名称
 * @param outMaterials 输出的材质实例映射表
 * @param aiMinFilterType Assimp最小过滤模式
 * @param aiMagFilterType Assimp最大过滤模式
 */
void setTextureFromPath(const aiScene *scene, Engine *engine,
        std::vector<filament::Texture*> textures, const aiString &textureFile,
        const std::string &materialName, const std::string &textureDirectory,
        aiTextureMapMode *mapMode, const char *parameterName,
        std::map<std::string, MaterialInstance *> &outMaterials,
        unsigned int aiMinFilterType=0, unsigned int aiMagFilterType=0) {

    // 转换过滤模式
    TextureSampler::MinFilter minFilterType = aiMinFilterToFilament(aiMinFilterType);
    TextureSampler::MagFilter magFilterType = aiMagFilterToFilament(aiMagFilterType);

    // 创建纹理采样器
    TextureSampler sampler;
    if (mapMode) {
        // 使用提供的映射模式（U、V、W三个方向）
        sampler = TextureSampler(
                minFilterType,
                magFilterType,
                aiToFilamentMapMode(mapMode[0]),
                aiToFilamentMapMode(mapMode[1]),
                aiToFilamentMapMode(mapMode[2]));
    } else {
        // 使用默认的REPEAT模式
        sampler = TextureSampler(
                minFilterType,
                magFilterType,
                TextureSampler::WrapMode::REPEAT);
    }

    Texture* textureMap = nullptr;
    int32_t embeddedId = getEmbeddedTextureId(textureFile);

    // TODO: change this in refactor
    // 根据参数名确定纹理格式：baseColorMap和emissiveMap使用sRGB，baseColorMap有Alpha
    bool isSRGB = strcmp(parameterName, "baseColorMap") == 0 || strcmp(parameterName, "emissiveMap") == 0;
    bool hasAlpha = strcmp(parameterName, "baseColorMap") == 0;

    // 加载纹理（嵌入或文件）
    if (embeddedId != -1) {
        loadEmbeddedTexture(engine, scene->mTextures[embeddedId], &textureMap, isSRGB, hasAlpha);
    } else {
        loadTexture(engine, textureDirectory + textureFile.C_Str(), &textureMap, isSRGB, hasAlpha);
    }

    textures.push_back(textureMap);

    // 将纹理设置到材质实例
    if (textureMap != nullptr) {
        outMaterials[materialName]->setParameter(parameterName, textureMap, sampler);
    }
}

/**
 * 计算变换后的轴对齐包围盒（AABB）（模板函数）
 * 
 * 执行步骤：
 * 1. 初始化最小和最大边界值
 * 2. 遍历所有索引
 * 3. 根据索引获取顶点位置
 * 4. 应用变换矩阵
 * 5. 更新最小和最大边界
 * 6. 返回包围盒
 * 
 * @param vertices 顶点数组
 * @param indices 索引数组
 * @param count 索引数量
 * @param transform 4x4变换矩阵
 * @return 变换后的轴对齐包围盒
 */
template<typename VECTOR, typename INDEX>
Box computeTransformedAABB(VECTOR const* vertices, INDEX const* indices, size_t count,
        const mat4f& transform) noexcept {
    size_t stride = sizeof(VECTOR);
    filament::math::float3 bmin(std::numeric_limits<float>::max());
    filament::math::float3 bmax(std::numeric_limits<float>::lowest());
    // 遍历所有索引，计算变换后的顶点位置
    for (size_t i = 0; i < count; ++i) {
        VECTOR const* p = reinterpret_cast<VECTOR const*>(
                (char const*) vertices + indices[i] * stride);
        const filament::math::float3 v(p->x, p->y, p->z);
        // 应用变换矩阵
        float3 tv = (transform * float4(v, 1.0f)).xyz;
        // 更新边界
        bmin = min(bmin, tv);
        bmax = max(bmax, tv);
    }
    return Box().set(bmin, bmax);
}

/**
 * 从文件加载网格实现
 * 
 * 执行步骤：
 * 1. 创建资源对象并设置文件路径
 * 2. 调用setFromFile解析文件并构建资源数据
 * 3. 根据UV格式创建顶点缓冲区（支持有符号归一化UV）
 * 4. 设置顶点数据（位置、切向量、UV坐标）
 * 5. 创建索引缓冲区
 * 6. 为每个网格创建可渲染实体
 * 7. 设置材质和几何数据
 * 8. 建立层次结构变换关系
 * 9. 接管材质实例的所有权
 * 
 * @param path 模型文件路径
 * @param materials 材质实例映射表（会被此函数接管所有权）
 * @param overrideMaterial 是否覆盖所有材质为默认材质
 */
void MeshAssimp::addFromFile(const Path& path,
        std::map<std::string, MaterialInstance*>& materials, bool overrideMaterial) {

    Asset asset;
    asset.file = path;

    { // This scope to make sure we're not using std::move()'d objects later

        // TODO: if we had a way to allocate temporary buffers from the engine with a
        // "command buffer" lifetime, we wouldn't need to have to deal with freeing the
        // std::vectors here.

        //TODO: a lot of these method arguments should probably be class or global variables
        if (!setFromFile(asset, materials)) {
            return;
        }

        VertexBuffer::Builder vertexBufferBuilder = VertexBuffer::Builder()
                .vertexCount((uint32_t)asset.positions.size())
                .bufferCount(4)
                .attribute(VertexAttribute::POSITION,     0, VertexBuffer::AttributeType::HALF4)
                .attribute(VertexAttribute::TANGENTS,     1, VertexBuffer::AttributeType::SHORT4)
                .normalized(VertexAttribute::TANGENTS);

        if (asset.snormUV0) {
            vertexBufferBuilder.attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::SHORT2)
                .normalized(VertexAttribute::UV0);
        } else {
            vertexBufferBuilder.attribute(VertexAttribute::UV0, 2, VertexBuffer::AttributeType::HALF2);
        }

        if (asset.snormUV1) {
            vertexBufferBuilder.attribute(VertexAttribute::UV1, 3, VertexBuffer::AttributeType::SHORT2)
                    .normalized(VertexAttribute::UV1);
        } else {
            vertexBufferBuilder.attribute(VertexAttribute::UV1, 3, VertexBuffer::AttributeType::HALF2);
        }

        mVertexBuffer = vertexBufferBuilder.build(mEngine);

        // 创建State对象包装数据向量（用于缓冲区描述符）
        // State对象会在缓冲区使用完毕后自动释放内存
        auto ps = new State<half4>(std::move(asset.positions));      // 位置数据（half4格式）
        auto ns = new State<short4>(std::move(asset.tangents));       // 切向量数据（short4格式，有符号归一化）
        auto t0s = new State<ushort2>(std::move(asset.texCoords0));  // UV0坐标数据（ushort2格式）
        auto t1s = new State<ushort2>(std::move(asset.texCoords1));  // UV1坐标数据（ushort2格式）
        auto is = new State<uint32_t>(std::move(asset.indices));      // 索引数据（uint32格式）

        // 设置顶点缓冲区的各个属性缓冲区
        // 缓冲区0：位置（half4格式，4个half值）
        mVertexBuffer->setBufferAt(mEngine, 0,
                VertexBuffer::BufferDescriptor(ps->data(), ps->size(), State<half4>::free, ps));

        // 缓冲区1：切向量（short4格式，有符号归一化）
        mVertexBuffer->setBufferAt(mEngine, 1,
                VertexBuffer::BufferDescriptor(ns->data(), ns->size(), State<short4>::free, ns));

        // 缓冲区2：UV0坐标（ushort2格式）
        mVertexBuffer->setBufferAt(mEngine, 2,
                VertexBuffer::BufferDescriptor(t0s->data(), t0s->size(), State<ushort2>::free, t0s));

        // 缓冲区3：UV1坐标（ushort2格式）
        mVertexBuffer->setBufferAt(mEngine, 3,
                VertexBuffer::BufferDescriptor(t1s->data(), t1s->size(), State<ushort2>::free, t1s));

        // 创建索引缓冲区并设置索引数据
        mIndexBuffer = IndexBuffer::Builder().indexCount(uint32_t(is->size())).build(mEngine);
        mIndexBuffer->setBuffer(mEngine,
                IndexBuffer::BufferDescriptor(is->data(), is->size(), State<uint32_t>::free, is));
    }

    // 始终添加默认材质（使用其默认参数），这样我们就不会使用网格中的任何默认值
    // 这确保了材质的一致性
    if (materials.find(AI_DEFAULT_MATERIAL_NAME) == materials.end()) {
        materials[AI_DEFAULT_MATERIAL_NAME] = mDefaultColorMaterial->createInstance();
    }

    // 创建可渲染实体（为每个网格创建一个实体）
    size_t startIndex = mRenderables.size();
    mRenderables.resize(startIndex + asset.meshes.size());
    EntityManager::get().create(asset.meshes.size(), mRenderables.data() + startIndex);
    // 创建根实体（用于层次结构）
    EntityManager::get().create(1, &rootEntity);

    TransformManager& tcm = mEngine.getTransformManager();
    // 添加根变换实例（单位矩阵）
    tcm.create(rootEntity, TransformManager::Instance{}, mat4f());

    // 为每个网格创建可渲染实体
    for (auto& mesh : asset.meshes) {
        // 创建可渲染管理器构建器（指定部分数量）
        RenderableManager::Builder builder(mesh.parts.size());
        builder.boundingBox(mesh.aabb);  // 设置包围盒
        builder.screenSpaceContactShadows(true);  // 启用屏幕空间接触阴影

        // 为每个部分设置几何和材质
        size_t partIndex = 0;
        for (auto& part : mesh.parts) {
            // 设置几何数据（三角形图元，使用共享的顶点和索引缓冲区）
            builder.geometry(partIndex, RenderableManager::PrimitiveType::TRIANGLES,
                    mVertexBuffer, mIndexBuffer, part.offset, part.count);

            // 根据overrideMaterial标志选择材质
            if (overrideMaterial) {
                // 使用默认材质
                builder.material(partIndex, materials[AI_DEFAULT_MATERIAL_NAME]);
            } else {
                // 查找材质实例
                auto pos = materials.find(part.material);

                if (pos != materials.end()) {
                    // 使用已存在的材质实例
                    builder.material(partIndex, pos->second);
                } else {
                    // 创建新的材质实例（根据透明度选择不透明或透明材质）
                    MaterialInstance* colorMaterial;
                    if (part.opacity < 1.0f) {
                        // 透明材质：使用默认透明颜色材质
                        colorMaterial = mDefaultTransparentColorMaterial->createInstance();
                        colorMaterial->setParameter("baseColor", RgbaType::sRGB,
                                sRGBColorA { part.baseColor, part.opacity });
                    } else {
                        // 不透明材质：使用默认颜色材质
                        colorMaterial = mDefaultColorMaterial->createInstance();
                        colorMaterial->setParameter("baseColor", RgbType::sRGB, part.baseColor);
                        colorMaterial->setParameter("reflectance", part.reflectance);
                    }
                    // 设置PBR参数
                    colorMaterial->setParameter("metallic", part.metallic);
                    colorMaterial->setParameter("roughness", part.roughness);
                    builder.material(partIndex, colorMaterial);
                    // 将材质实例添加到映射表中
                    materials[part.material] = colorMaterial;
                }
            }
            partIndex++;
        }

        // 获取当前网格的实体索引
        const size_t meshIndex = &mesh - asset.meshes.data();
        Entity entity = mRenderables[startIndex + meshIndex];
        // 如果网格有部分，构建可渲染实体
        if (!mesh.parts.empty()) {
            builder.build(mEngine, entity);
        }
        // 建立层次结构：设置父节点变换
        auto pindex = asset.parents[meshIndex];
        TransformManager::Instance parent((pindex < 0) ?
                tcm.getInstance(rootEntity) : tcm.getInstance(mRenderables[pindex]));
        // 创建变换实例（使用网格的局部变换矩阵）
        tcm.create(entity, parent, mesh.transform);
    }

    // Takes over the ownership of the material instances so that resources are gracefully
    // destroyed in a correct order. The caller doesn't need to handle the destruction.
    mMaterialInstances.swap(materials);
}

using Assimp::Importer;

/**
 * 从文件设置资源数据实现
 * 
 * 执行步骤：
 * 1. 创建Assimp导入器并设置导入选项
 * 2. 读取文件并应用后处理（生成法线、切向量、UV坐标、三角化等）
 * 3. 检测文件格式是否为GLTF
 * 4. 验证场景和根节点
 * 5. 统计顶点和索引总数（用于预分配）
 * 6. 获取UV坐标范围（判断是否为有符号归一化格式）
 * 7. 根据UV格式选择处理函数（模板特化）
 * 8. 递归处理场景节点树
 * 9. 计算每个网格的包围盒
 * 10. 更新全局包围盒
 * 
 * @param asset 输出的资源数据
 * @param outMaterials 输出的材质实例映射表
 * @return 是否成功
 */
bool MeshAssimp::setFromFile(Asset& asset, std::map<std::string, MaterialInstance*>& outMaterials) {
    Importer importer;
    // 设置导入器属性：移除线和点图元，忽略COLLADA的UP方向，保持层次结构
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE,
            aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_COLLADA_IGNORE_UP_DIRECTION, true);
    importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);

    // 读取文件并应用后处理标志
    aiScene const* scene = importer.ReadFile(asset.file,
            // normals and tangents
            // 生成平滑法线和切向量
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            // UV Coordinates
            // 生成UV坐标
            aiProcess_GenUVCoords |
            // topology optimization
            // 拓扑优化：查找实例、优化网格、合并相同顶点
            aiProcess_FindInstances |
            aiProcess_OptimizeMeshes |
            aiProcess_JoinIdenticalVertices |
            // misc optimization
            // 其他优化：改善缓存局部性、按类型排序
            aiProcess_ImproveCacheLocality |
            aiProcess_SortByPType |
            // we only support triangles
            // 三角化（我们只支持三角形）
            aiProcess_Triangulate);

    // 检测文件格式是否为GLTF
    size_t index = importer.GetImporterIndex(asset.file.getExtension().c_str());
    const aiImporterDesc* importerDesc = importer.GetImporterInfo(index);
    bool isGLTF = importerDesc &&
            (!strncmp("glTF Importer",  importerDesc->mName, 13) ||
             !strncmp("glTF2 Importer", importerDesc->mName, 14));

    if (!scene) {
        std::cout << "No scene" << std::endl;
    }

    if (scene && !scene->mRootNode) {
        std::cout << "No root node" << std::endl;
    }

    // we could use those, but we want to keep the graph if any, for testing
    //      aiProcess_OptimizeGraph
    //      aiProcess_PreTransformVertices

    /**
     * Lambda函数：递归统计场景中所有节点的顶点和索引总数
     * 
     * 执行步骤：
     * 1. 遍历当前节点的所有网格
     * 2. 累加顶点数量和索引数量
     * 3. 递归处理所有子节点
     * 
     * 用于预分配顶点和索引缓冲区的大小，避免多次重新分配内存。
     * 
     * @param node 当前节点
     * @param totalVertexCount 累计的顶点总数（会被修改）
     * @param totalIndexCount 累计的索引总数（会被修改）
     */
    const std::function<void(aiNode const* node, size_t& totalVertexCount, size_t& totalIndexCount)>
            countVertices = [scene, &countVertices]
            (aiNode const* node, size_t& totalVertexCount, size_t& totalIndexCount) {
        // 遍历当前节点的所有网格，累加顶点和索引数量
        for (size_t i = 0; i < node->mNumMeshes; i++) {
            aiMesh const *mesh = scene->mMeshes[node->mMeshes[i]];
            totalVertexCount += mesh->mNumVertices;  // 累加顶点数

            const aiFace *faces = mesh->mFaces;
            const size_t numFaces = mesh->mNumFaces;
            // 累加索引数（所有面都是三角形，每个面3个索引）
            totalIndexCount += numFaces * faces[0].mNumIndices;
        }

        // 递归处理所有子节点
        for (size_t i = 0; i < node->mNumChildren; i++) {
            countVertices(node->mChildren[i], totalVertexCount, totalIndexCount);
        }
    };

    if (scene) {
        size_t deep = 0;
        size_t depth = 0;
        size_t matCount = 0;

        aiNode const* node = scene->mRootNode;

        size_t totalVertexCount = 0;
        size_t totalIndexCount = 0;

        // 统计顶点和索引总数（用于预分配缓冲区大小）
        countVertices(node, totalVertexCount, totalIndexCount);

        // 预分配缓冲区大小（避免多次重新分配内存，提高性能）
        asset.positions.reserve(asset.positions.size() + totalVertexCount);
        asset.tangents.reserve(asset.tangents.size() + totalVertexCount);
        asset.texCoords0.reserve(asset.texCoords0.size() + totalVertexCount);
        asset.texCoords1.reserve(asset.texCoords1.size() + totalVertexCount);
        asset.indices.reserve(asset.indices.size() + totalIndexCount);

        // 获取UV0坐标的最小值和最大值（用于判断是否可以使用有符号归一化格式）
        float2 minUV0 = float2(std::numeric_limits<float>::max());
        float2 maxUV0 = float2(std::numeric_limits<float>::lowest());
        getMinMaxUV(scene, node, minUV0, maxUV0, 0);
        // 获取UV1坐标的最小值和最大值
        float2 minUV1 = float2(std::numeric_limits<float>::max());
        float2 maxUV1 = float2(std::numeric_limits<float>::lowest());
        getMinMaxUV(scene, node, minUV1, maxUV1, 1);

        // 判断UV0坐标是否在有符号归一化范围内（[-1, 1]）
        // 如果是，可以使用SHORT2有符号归一化格式，节省内存
        asset.snormUV0 = minUV0.x >= -1.0f && minUV0.x <= 1.0f && maxUV0.x >= -1.0f && maxUV0.x <= 1.0f &&
                         minUV0.y >= -1.0f && minUV0.y <= 1.0f && maxUV0.y >= -1.0f && maxUV0.y <= 1.0f;

        // 判断UV1坐标是否在有符号归一化范围内（[-1, 1]）
        asset.snormUV1 = minUV1.x >= -1.0f && minUV1.x <= 1.0f && maxUV1.x >= -1.0f && maxUV1.x <= 1.0f &&
                         minUV1.y >= -1.0f && minUV1.y <= 1.0f && maxUV1.y >= -1.0f && maxUV1.y <= 1.0f;

        // 根据UV格式选择处理函数（模板特化，优化UV转换性能）
        // 如果UV坐标在有符号归一化范围内，使用SHORT2格式；否则使用HALF2格式
        if (asset.snormUV0) {
            if (asset.snormUV1) {
                // UV0和UV1都使用有符号归一化格式
                processNode<true, true>(asset, outMaterials,
                                        scene, isGLTF, deep, matCount, node, -1, depth);
            } else {
                // UV0使用有符号归一化格式，UV1使用半精度浮点格式
                processNode<true, false>(asset, outMaterials,
                                        scene, isGLTF, deep, matCount, node, -1, depth);
            }
        } else {
            if (asset.snormUV1) {
                // UV0使用半精度浮点格式，UV1使用有符号归一化格式
                processNode<false, true>(asset, outMaterials,
                                        scene, isGLTF, deep, matCount, node, -1, depth);
            } else {
                // UV0和UV1都使用半精度浮点格式
                processNode<false, false>(asset, outMaterials,
                                        scene, isGLTF, deep, matCount, node, -1, depth);
            }
        }

        // 计算每个网格的包围盒（AABB）并找到整个模型的包围盒
        for (auto& mesh : asset.meshes) {
            // 计算局部空间的包围盒（未变换）
            mesh.aabb = RenderableManager::computeAABB(
                    asset.positions.data(),
                    asset.indices.data() + mesh.offset,
                    mesh.count);

            // 计算世界空间的包围盒（应用累积变换矩阵）
            Box transformedAabb = computeTransformedAABB(
                    asset.positions.data(),
                    asset.indices.data() + mesh.offset,
                    mesh.count,
                    mesh.accTransform);

            float3 aabbMin = transformedAabb.getMin();
            float3 aabbMax = transformedAabb.getMax();

            if (!isinf(aabbMin.x) && !isinf(aabbMax.x)) {
                if (minBound.x > maxBound.x) {
                    minBound.x = aabbMin.x;
                    maxBound.x = aabbMax.x;
                } else {
                    minBound.x = fmin(minBound.x, aabbMin.x);
                    maxBound.x = fmax(maxBound.x, aabbMax.x);
                }
            }

            if (!isinf(aabbMin.y) && !isinf(aabbMax.y)) {
                if (minBound.y > maxBound.y) {
                    minBound.y = aabbMin.y;
                    maxBound.y = aabbMax.y;
                } else {
                    minBound.y = fmin(minBound.y, aabbMin.y);
                    maxBound.y = fmax(maxBound.y, aabbMax.y);
                }
            }

            if (!isinf(aabbMin.z) && !isinf(aabbMax.z)) {
                if (minBound.z > maxBound.z) {
                    minBound.z = aabbMin.z;
                    maxBound.z = aabbMax.z;
                } else {
                    minBound.z = fmin(minBound.z, aabbMin.z);
                    maxBound.z = fmax(maxBound.z, aabbMax.z);
                }
            }
        }

        return true;
    }
    return false;
}

/**
 * 处理场景节点实现（模板函数，根据UV格式优化）
 * 
 * 递归处理场景节点树，提取网格数据、材质信息，建立层次结构。
 * 
 * 执行步骤：
 * 1. 获取节点的变换矩阵并转置（Assimp使用行主序，Filament使用列主序）
 * 2. 创建新的网格条目，设置父节点索引和变换
 * 3. 计算累积变换矩阵（包含父节点变换）
 * 4. 遍历节点的所有网格：
 *    a. 提取顶点数据（位置、法线、切向量、UV坐标）
 *    b. 处理缺失的切向量（如果不存在则生成）
 *    c. 打包切向量为四元数
 *    d. 转换UV坐标为指定格式（根据模板参数）
 *    e. 提取索引数据
 *    f. 处理材质（GLTF或传统材质）
 *    g. 创建网格部分（Part）
 * 5. 递归处理子节点
 * 
 * @param asset 资源数据（会被修改）
 * @param outMaterials 输出的材质实例映射表
 * @param scene Assimp场景对象
 * @param isGLTF 是否为GLTF格式
 * @param deep 当前深度
 * @param matCount 材质计数器
 * @param node 当前节点
 * @param parentIndex 父节点索引（-1表示根节点）
 * @param depth 输出的最大深度（会被修改）
 */
template<bool SNORMUV0, bool SNORMUV1>
void MeshAssimp::processNode(Asset& asset,
        std::map<std::string,
        MaterialInstance *> &outMaterials,
        const aiScene *scene,
        bool isGLTF,
        size_t deep,
        size_t matCount,
        const aiNode *node,
        int parentIndex,
        size_t &depth) const {
    // 获取节点变换矩阵并转置（Assimp使用行主序，Filament使用列主序）
    mat4f const& current = transpose(*reinterpret_cast<mat4f const*>(&node->mTransformation));

    size_t totalIndices = 0;
    // 添加父节点索引
    asset.parents.push_back(parentIndex);
    // 创建新网格条目
    asset.meshes.push_back(Mesh{});
    asset.meshes.back().offset = asset.indices.size();
    asset.meshes.back().transform = current;

    // 计算累积变换矩阵（包含父节点变换）
    mat4f parentTransform = parentIndex >= 0 ? asset.meshes[parentIndex].accTransform : mat4f();
    asset.meshes.back().accTransform = parentTransform * current;

    for (size_t i = 0; i < node->mNumMeshes; i++) {
            aiMesh const* mesh = scene->mMeshes[node->mMeshes[i]];

            float3 const* positions  = reinterpret_cast<float3 const*>(mesh->mVertices);
            float3 const* tangents   = reinterpret_cast<float3 const*>(mesh->mTangents);
            float3 const* bitangents = reinterpret_cast<float3 const*>(mesh->mBitangents);
            float3 const* normals    = reinterpret_cast<float3 const*>(mesh->mNormals);
            float3 const* texCoords0 = reinterpret_cast<float3 const*>(mesh->mTextureCoords[0]);
            float3 const* texCoords1 = reinterpret_cast<float3 const*>(mesh->mTextureCoords[1]);

            const size_t numVertices = mesh->mNumVertices;

            if (numVertices > 0) {
                const aiFace* faces = mesh->mFaces;
                const size_t numFaces = mesh->mNumFaces;

                if (numFaces > 0) {
                    size_t indicesOffset = asset.positions.size();

                    for (size_t j = 0; j < numVertices; j++) {
                        float3 normal = normals[j];
                        float3 tangent;
                        float3 bitangent;

                        // Assimp always returns 3D tex coords but we only support 2D tex coords.
                        // Assimp总是返回3D纹理坐标，但我们只支持2D纹理坐标
                        float2 texCoord0 = texCoords0 ? texCoords0[j].xy : float2{0.0};
                        float2 texCoord1 = texCoords1 ? texCoords1[j].xy : float2{0.0};
                        // If the tangent and bitangent don't exist, make arbitrary ones. This only
                        // occurs when the mesh is missing texture coordinates, because assimp
                        // computes tangents for us. (search up for aiProcess_CalcTangentSpace)
                        // 如果切向量和副切向量不存在，生成任意值。这通常发生在网格缺少纹理坐标时，
                        // 因为Assimp会为我们计算切向量（见aiProcess_CalcTangentSpace）
                        if (!tangents) {
                            // 生成任意切向量：使用法线和X轴叉积生成副切向量，再生成切向量
                            bitangent = normalize(cross(normal, float3{1.0, 0.0, 0.0}));
                            tangent = normalize(cross(normal, bitangent));
                        } else {
                            tangent = tangents[j];
                            bitangent = bitangents[j];
                        }

                        // 打包切向量框架为四元数（用于法线贴图）
                        quatf q = filament::math::details::TMat33<float>::packTangentFrame({tangent, bitangent, normal});
                        asset.tangents.push_back(packSnorm16(q.xyzw));
                        // 根据模板参数转换UV坐标格式
                        asset.texCoords0.emplace_back(convertUV<SNORMUV0>(texCoord0));
                        asset.texCoords1.emplace_back(convertUV<SNORMUV1>(texCoord1));

                        // 存储顶点位置（half4格式，w分量用于其他用途）
                        asset.positions.emplace_back(positions[j], 1.0_h);
                    }

                    // Populate the index buffer. All faces are triangles at this point because we
                    // asked assimp to perform triangulation.
                    // 填充索引缓冲区。此时所有面都是三角形，因为我们要求Assimp进行三角化
                    size_t indicesCount = numFaces * faces[0].mNumIndices;
                    size_t indexBufferOffset = asset.indices.size();
                    totalIndices += indicesCount;

                    // 提取索引数据（需要加上顶点偏移量，因为顶点是累积添加的）
                    for (size_t j = 0; j < numFaces; ++j) {
                        const aiFace& face = faces[j];
                        for (size_t k = 0; k < face.mNumIndices; ++k) {
                            asset.indices.push_back(uint32_t(face.mIndices[k] + indicesOffset));
                        }
                    }

                    uint32_t materialId = mesh->mMaterialIndex;
                    aiMaterial const* material = scene->mMaterials[materialId];

                    aiString name;
                    std::string materialName;

                    if (material->Get(AI_MATKEY_NAME, name) != AI_SUCCESS) {
                        if (isGLTF) {
                            while (outMaterials.find("_mat_" + std::to_string(matCount))
                                   != outMaterials.end()) {
                                matCount++;
                            }
                            materialName = "_mat_" + std::to_string(matCount);
                        } else {
                            materialName = AI_DEFAULT_MATERIAL_NAME;
                        }
                    } else {
                        materialName = name.C_Str();
                    }

                    if (isGLTF && outMaterials.find(materialName) == outMaterials.end()) {
                        std::string dirName = asset.file.getParent();
                        processGLTFMaterial(scene, material, materialName, dirName, outMaterials);
                    }

                    aiColor3D color;
                    sRGBColor baseColor{1.0f};
                    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
                        baseColor = *reinterpret_cast<sRGBColor*>(&color);
                    }

                    float opacity;
                    if (material->Get(AI_MATKEY_OPACITY, opacity) != AI_SUCCESS) {
                        opacity = 1.0f;
                    }
                    if (opacity <= 0.0f) opacity = 1.0f;

                    float shininess;
                    if (material->Get(AI_MATKEY_SHININESS, shininess) != AI_SUCCESS) {
                        shininess = 0.0f;
                    }

                    // convert shininess to roughness
                    float roughness = sqrt(2.0f / (shininess + 2.0f));

                    float metallic = 0.0f;
                    float reflectance = 0.5f;
                    if (material->Get(AI_MATKEY_COLOR_SPECULAR, color) == AI_SUCCESS) {
                        // if there's a non-grey specular color, assume a metallic surface
                        if (color.r != color.g && color.r != color.b) {
                            metallic = 1.0f;
                            baseColor = *reinterpret_cast<sRGBColor*>(&color);
                        } else {
                            if (baseColor.r == 0.0f && baseColor.g == 0.0f && baseColor.b == 0.0f) {
                                metallic = 1.0f;
                                baseColor = *reinterpret_cast<sRGBColor*>(&color);
                            } else {
                                // TODO: the conversion formula is correct
                                // reflectance = sqrtf(color.r / 0.16f);
                            }
                        }
                    }

                    asset.meshes.back().parts.push_back({
                            indexBufferOffset, indicesCount, materialName,
                            baseColor, opacity, metallic, roughness, reflectance
                    });
                }
            }
        }

    if (node->mNumMeshes > 0) {
        asset.meshes.back().count = totalIndices;
    }

    if (node->mNumChildren) {
        parentIndex = static_cast<int>(asset.meshes.size()) - 1;
        deep++;
        depth = std::max(deep, depth);
        for (size_t i = 0, c = node->mNumChildren; i < c; i++) {
            processNode<SNORMUV0, SNORMUV1>(asset, outMaterials, scene,
                                            isGLTF, deep, matCount, node->mChildren[i], parentIndex, depth);
        }
        deep--;
    }
}

/**
 * 处理GLTF材质实现
 * 
 * 从Assimp材质对象提取GLTF PBR材质参数，创建或获取缓存的材质，
 * 加载纹理并设置到材质实例中。
 * 
 * 执行步骤：
 * 1. 提取材质属性（双面、无光照、Alpha模式等）
 * 2. 提取各贴图使用的UV通道索引
 * 3. 计算材质配置哈希值
 * 4. 从缓存获取或创建材质
 * 5. 创建材质实例
 * 6. 加载并设置各种贴图（基础颜色、金属度粗糙度、AO、法线、自发光）
 * 7. 设置材质因子（基础颜色因子、金属度因子、粗糙度因子、自发光因子）
 * 
 * @param scene Assimp场景对象
 * @param material Assimp材质对象
 * @param materialName 材质名称
 * @param dirName 材质文件所在目录
 * @param outMaterials 输出的材质实例映射表
 */
void MeshAssimp::processGLTFMaterial(const aiScene* scene, const aiMaterial* material,
        const std::string& materialName, const std::string& dirName,
         std::map<std::string, MaterialInstance*>& outMaterials) const {

    aiString baseColorPath;    // 基础颜色贴图路径
    aiString AOPath;            // 环境光遮蔽贴图路径
    aiString MRPath;            // 金属度粗糙度贴图路径
    aiString normalPath;        // 法线贴图路径
    aiString emissivePath;      // 自发光贴图路径
    aiTextureMapMode mapMode[3]; // 纹理映射模式（U、V、W三个方向）
    MaterialConfig matConfig;   // 材质配置

    // 提取材质属性
    material->Get(AI_MATKEY_TWOSIDED, matConfig.doubleSided);
    material->Get(AI_MATKEY_GLTF_UNLIT, matConfig.unlit);

    // 提取Alpha模式
    aiString alphaMode;
    material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode);
    if (strcmp(alphaMode.C_Str(), "BLEND") == 0) {
        matConfig.alphaMode = AlphaMode::TRANSPARENT;
    } else if (strcmp(alphaMode.C_Str(), "MASK") == 0) {
        matConfig.alphaMode = AlphaMode::MASKED;
        float maskThreshold = 0.5;
        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, maskThreshold);
        matConfig.maskThreshold = maskThreshold;
    }

    // 提取各贴图使用的UV通道索引
    material->Get(_AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE,
                  matConfig.baseColorUV);
    material->Get(_AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE,
                  matConfig.metallicRoughnessUV);
    material->Get(_AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, aiTextureType_LIGHTMAP, 0, matConfig.aoUV);
    material->Get(_AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, aiTextureType_NORMALS, 0, matConfig.normalUV);
    material->Get(_AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, aiTextureType_EMISSIVE, 0, matConfig.emissiveUV);

    uint64_t configHash = hashMaterialConfig(matConfig);

    if (mGltfMaterialCache.find(configHash) == mGltfMaterialCache.end()) {
        mGltfMaterialCache[configHash] = createMaterialFromConfig(mEngine, matConfig);
    }

    outMaterials[materialName] = mGltfMaterialCache[configHash]->createInstance();

    // TODO: is there a way to use the same material for multiple mask threshold values?
//    if (matConfig.alphaMode == masked) {
//        float maskThreshold = 0.5;
//        material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, maskThreshold);
//        outMaterials[materialName]->setParameter("maskThreshold", maskThreshold);
//    }

    // 加载GLTF文件的材质属性值（因子）
    // 这些值用于与贴图颜色相乘，控制材质的最终外观
    aiColor4D baseColorFactor;    // 基础颜色因子（RGBA）
    aiColor3D emissiveFactor;      // 自发光因子（RGB）
    float metallicFactor = 1.0;    // 金属度因子（默认1.0）
    float roughnessFactor = 1.0;   // 粗糙度因子（默认1.0）

    // TODO: is occlusion strength available on Assimp now?
    // TODO: 环境光遮蔽强度是否在Assimp中可用？

    // 创建默认纹理采样器（用于没有指定过滤模式的贴图）
    // 使用线性Mipmap线性过滤（三线性过滤），线性放大过滤，重复包装模式
    TextureSampler sampler(
            TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,  // 三线性过滤
            TextureSampler::MagFilter::LINEAR,                // 线性放大过滤
            TextureSampler::WrapMode::REPEAT);                // 重复包装模式

    if (material->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, &baseColorPath,
            nullptr, nullptr, nullptr, nullptr, mapMode) == AI_SUCCESS) {
        unsigned int minType = 0;
        unsigned int magType = 0;
        material->Get("$tex.mappingfiltermin", AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, minType);
        material->Get("$tex.mappingfiltermag", AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, magType);

        setTextureFromPath(scene, &mEngine, mTextures, baseColorPath,
                materialName, dirName, mapMode, "baseColorMap", outMaterials, minType, magType);
    } else {
        outMaterials[materialName]->setParameter("baseColorMap", mDefaultMap, sampler);
    }

    if (material->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &MRPath,
            nullptr, nullptr, nullptr, nullptr, mapMode) == AI_SUCCESS) {
        unsigned int minType = 0;
        unsigned int magType = 0;
        material->Get("$tex.mappingfiltermin", AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, minType);
        material->Get("$tex.mappingfiltermag", AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, magType);

        setTextureFromPath(scene, &mEngine, mTextures, MRPath, materialName,
                dirName, mapMode, "metallicRoughnessMap", outMaterials, minType, magType);
    } else {
        outMaterials[materialName]->setParameter("metallicRoughnessMap", mDefaultMap, sampler);
        outMaterials[materialName]->setParameter("metallicFactor", mDefaultMetallic);
        outMaterials[materialName]->setParameter("roughnessFactor", mDefaultRoughness);
    }

    if (material->GetTexture(aiTextureType_LIGHTMAP, 0, &AOPath, nullptr,
            nullptr, nullptr, nullptr, mapMode) == AI_SUCCESS) {
        unsigned int minType = 0;
        unsigned int magType = 0;
        material->Get("$tex.mappingfiltermin", aiTextureType_LIGHTMAP, 0, minType);
        material->Get("$tex.mappingfiltermag", aiTextureType_LIGHTMAP, 0, magType);
        setTextureFromPath(scene, &mEngine, mTextures, AOPath, materialName,
                dirName, mapMode, "aoMap", outMaterials, minType, magType);
    } else {
        outMaterials[materialName]->setParameter("aoMap", mDefaultMap, sampler);
    }

    if (material->GetTexture(aiTextureType_NORMALS, 0, &normalPath, nullptr,
            nullptr, nullptr, nullptr, mapMode) == AI_SUCCESS) {
        unsigned int minType = 0;
        unsigned int magType = 0;
        material->Get("$tex.mappingfiltermin", aiTextureType_NORMALS, 0, minType);
        material->Get("$tex.mappingfiltermag", aiTextureType_NORMALS, 0, magType);
        setTextureFromPath(scene, &mEngine, mTextures, normalPath, materialName,
                dirName, mapMode, "normalMap", outMaterials, minType, magType);
    } else {
        outMaterials[materialName]->setParameter("normalMap", mDefaultNormalMap, sampler);
    }

    if (material->GetTexture(aiTextureType_EMISSIVE, 0, &emissivePath, nullptr,
            nullptr, nullptr, nullptr, mapMode) == AI_SUCCESS) {
        unsigned int minType = 0;
        unsigned int magType = 0;
        material->Get("$tex.mappingfiltermin", aiTextureType_EMISSIVE, 0, minType);
        material->Get("$tex.mappingfiltermag", aiTextureType_EMISSIVE, 0, magType);
        setTextureFromPath(scene, &mEngine, mTextures, emissivePath, materialName,
                dirName, mapMode, "emissiveMap", outMaterials, minType, magType);
    }  else {
        outMaterials[materialName]->setParameter("emissiveMap", mDefaultMap, sampler);
        outMaterials[materialName]->setParameter("emissiveFactor", mDefaultEmissive);
    }

    // 如果GLTF文件有材质因子值，覆盖默认因子值
    // 这些因子值会与对应的贴图颜色相乘，控制材质的最终外观
    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallicFactor) == AI_SUCCESS) {
        outMaterials[materialName]->setParameter("metallicFactor", metallicFactor);
    }

    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughnessFactor) == AI_SUCCESS) {
        outMaterials[materialName]->setParameter("roughnessFactor", roughnessFactor);
    }

    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveFactor) == AI_SUCCESS) {
        // 转换自发光因子为sRGB颜色格式
        sRGBColor emissiveFactorCast = *reinterpret_cast<sRGBColor*>(&emissiveFactor);
        outMaterials[materialName]->setParameter("emissiveFactor", emissiveFactorCast);
    }

    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColorFactor) == AI_SUCCESS) {
        // 转换基础颜色因子为sRGB颜色格式（包含Alpha通道）
        sRGBColorA baseColorFactorCast = *reinterpret_cast<sRGBColorA*>(&baseColorFactor);
        outMaterials[materialName]->setParameter("baseColorFactor", baseColorFactorCast);
    }

    aiBool isSpecularGlossiness = false;
    if (material->Get(AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS, isSpecularGlossiness) == AI_SUCCESS) {
        if (isSpecularGlossiness) {
            std::cout << "Warning: pbrSpecularGlossiness textures are not currently supported" << std::endl;
        }
    }
}
