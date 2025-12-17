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

#include <filameshio/MeshReader.h>
#include <filameshio/filamesh.h>

#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/Fence.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/VertexBuffer.h>

#include <meshoptimizer.h>

#include <utils/EntityManager.h>
#include <utils/Log.h>
#include <utils/Path.h>

#include <string>
#include <vector>
#include <map>
#include <string>

#include <fcntl.h>
#if !defined(WIN32)
#    include <unistd.h>
#else
#    include <io.h>
#endif

using namespace filament;
using namespace filamesh;
using namespace filament::math;

/**
 * 默认材质名称常量
 */
#define DEFAULT_MATERIAL "DefaultMaterial"

//------------------------------------------------------------------------------
//-------------------------Begin Material Registry------------------------------
//------------------------------------------------------------------------------

/**
 * MaterialRegistry实现类（PIMPL模式）
 * 
 * 使用PIMPL模式隐藏实现细节，提供稳定的ABI。
 * 包含材质名称到材质实例的映射表。
 */
struct MeshReader::MaterialRegistry::MaterialRegistryImpl {
    std::map<utils::CString, filament::MaterialInstance*> materialMap;  // 材质名称到材质实例的映射表
};

/**
 * 构造函数实现
 * 
 * 执行步骤：
 * 1. 创建实现对象（PIMPL模式）
 */
MeshReader::MaterialRegistry::MaterialRegistry()
    : mImpl(new MaterialRegistryImpl()) {
}

/**
 * 拷贝构造函数实现
 * 
 * 执行步骤：
 * 1. 深拷贝实现对象
 * 
 * @param rhs 源对象
 */
MeshReader::MaterialRegistry::MaterialRegistry(const MaterialRegistry& rhs)
    : mImpl(new MaterialRegistryImpl(*rhs.mImpl)) {
}

/**
 * 拷贝赋值运算符实现
 * 
 * 执行步骤：
 * 1. 深拷贝实现对象
 * 
 * @param rhs 源对象
 * @return 当前对象的引用
 */
MeshReader::MaterialRegistry& MeshReader::MaterialRegistry::operator=(const MaterialRegistry& rhs) {
    *mImpl = *rhs.mImpl;
    return *this;
}

/**
 * 析构函数实现
 * 
 * 执行步骤：
 * 1. 删除实现对象
 */
MeshReader::MaterialRegistry::~MaterialRegistry() {
    delete mImpl;
}

/**
 * 移动构造函数实现
 * 
 * 执行步骤：
 * 1. 交换实现对象指针
 * 
 * @param rhs 源对象（右值引用）
 */
MeshReader::MaterialRegistry::MaterialRegistry(MaterialRegistry&& rhs) 
    : mImpl(nullptr) {
    std::swap(mImpl, rhs.mImpl);
}

/**
 * 移动赋值运算符实现
 * 
 * 执行步骤：
 * 1. 删除当前实现对象
 * 2. 交换实现对象指针
 * 
 * @param rhs 源对象（右值引用）
 * @return 当前对象的引用
 */
MeshReader::MaterialRegistry& MeshReader::MaterialRegistry::operator=(MaterialRegistry&& rhs) {
    delete mImpl;
    mImpl = nullptr;
    std::swap(mImpl, rhs.mImpl);
    return *this;
}

/**
 * 获取材质实例实现
 * 
 * 执行步骤：
 * 1. 在映射表中查找指定名称的材质
 * 2. 如果找到，返回材质实例指针
 * 3. 如果未找到，返回nullptr
 * 
 * @param name 材质名称
 * @return 材质实例指针，如果未找到则返回nullptr
 */
filament::MaterialInstance* MeshReader::MaterialRegistry::getMaterialInstance(
        const utils::CString& name) {
    // Try to find the requested material
    // 尝试查找请求的材质
    auto miter = mImpl->materialMap.find(name);
    // If it exists, return it
    // 如果存在，返回它
    if (miter != mImpl->materialMap.end()) {
        return miter->second;
    }
    // If it doesn't exist, give a dummy value
    // 如果不存在，返回空值
    return nullptr;
}

/**
 * 注册材质实例实现
 * 
 * 执行步骤：
 * 1. 将材质实例添加到映射表中
 * 
 * @param name 材质名称
 * @param materialInstance 材质实例指针
 */
void MeshReader::MaterialRegistry::registerMaterialInstance(const utils::CString& name,
        filament::MaterialInstance* materialInstance) {
    // Add the material to our map
    // 将材质添加到映射表中
    mImpl->materialMap[name] = materialInstance;
}

/**
 * 注销材质实例实现
 * 
 * 执行步骤：
 * 1. 在映射表中查找指定名称的材质
 * 2. 如果找到，从映射表中删除
 * 
 * @param name 材质名称
 */
void MeshReader::MaterialRegistry::unregisterMaterialInstance(const utils::CString& name) {
    auto miter = mImpl->materialMap.find(name);
    // Remove it from the map if it existed
    // 如果存在，从映射表中删除
    if (miter != mImpl->materialMap.end()) {
        mImpl->materialMap.erase(miter);
    }
}

/**
 * 注销所有材质实例实现
 * 
 * 执行步骤：
 * 1. 清空映射表
 */
void MeshReader::MaterialRegistry::unregisterAll() {
    mImpl->materialMap.clear();
}

/**
 * 获取已注册材质数量实现
 * 
 * @return 已注册材质的数量
 */
std::size_t MeshReader::MaterialRegistry::numRegistered() const noexcept {
    return mImpl->materialMap.size();
}

/**
 * 获取所有已注册材质（包含名称）实现
 * 
 * 执行步骤：
 * 1. 遍历映射表
 * 2. 将材质名称和材质实例指针复制到输出数组
 * 
 * @param materialList 输出的材质实例指针数组
 * @param materialNameList 输出的材质名称数组
 */
void MeshReader::MaterialRegistry::getRegisteredMaterials(filament::MaterialInstance** materialList,
        utils::CString* materialNameList) const {
    for (const auto& materialPair : mImpl->materialMap) {
        (*materialNameList++) = materialPair.first;
        (*materialList++) = materialPair.second;
    }
}

/**
 * 获取所有已注册材质（不包含名称）实现
 * 
 * 执行步骤：
 * 1. 遍历映射表
 * 2. 将材质实例指针复制到输出数组
 * 
 * @param materialList 输出的材质实例指针数组
 */
void MeshReader::MaterialRegistry::getRegisteredMaterials(
        filament::MaterialInstance** materialList) const {
    for (const auto& materialPair : mImpl->materialMap) {
        (*materialList++) = materialPair.second;
    }
}

/**
 * 获取所有已注册材质名称实现
 * 
 * 执行步骤：
 * 1. 遍历映射表
 * 2. 将材质名称复制到输出数组
 * 
 * @param materialNameList 输出的材质名称数组
 */
void MeshReader::MaterialRegistry::getRegisteredMaterialNames(
        utils::CString* materialNameList) const {
    for (const auto& materialPair : mImpl->materialMap) {
        (*materialNameList++) = materialPair.first;
    }
}

//------------------------------------------------------------------------------
//---------------------------End Material Registry------------------------------
//------------------------------------------------------------------------------


/**
 * 获取文件大小
 * 
 * 执行步骤：
 * 1. 将文件指针移动到文件末尾
 * 2. 获取当前位置（即文件大小）
 * 3. 将文件指针重置到文件开头
 * 
 * @param fd 文件描述符
 * @return 文件大小（字节数）
 */
static size_t fileSize(int fd) {
    size_t filesize;
    filesize = (size_t) lseek(fd, 0, SEEK_END);  // 移动到文件末尾并获取大小
    lseek(fd, 0, SEEK_SET);  // 重置到文件开头
    return filesize;
}

namespace filamesh {

/**
 * 从文件加载网格实现
 * 
 * 执行步骤：
 * 1. 打开文件（只读模式）
 * 2. 获取文件大小
 * 3. 分配内存并读取整个文件
 * 4. 验证文件魔数（MAGICID）
 * 5. 调用loadMeshFromBuffer解析网格数据
 * 6. 等待GPU完成操作
 * 7. 释放内存并关闭文件
 * 
 * @param engine Filament引擎指针
 * @param path 文件路径
 * @param materials 材质注册表
 * @return 加载的网格对象
 */
MeshReader::Mesh MeshReader::loadMeshFromFile(filament::Engine* engine, const utils::Path& path,
        MaterialRegistry& materials) {

    Mesh mesh;

    // 打开文件（只读模式）
    int fd = open(path.c_str(), O_RDONLY);

    // 获取文件大小并分配内存
    size_t size = fileSize(fd);
    char* data = (char*) malloc(size);
    read(fd, data, size);

    if (data) {
        // 验证文件魔数（检查是否为有效的filamesh文件）
        char *p = data;
        char *magic = p;
        p += sizeof(MAGICID);

        if (!strncmp(MAGICID, magic, 8)) {
            // 从缓冲区加载网格（使用nullptr作为释放回调，因为我们会手动释放）
            mesh = loadMeshFromBuffer(engine, data, nullptr, nullptr, materials);
        }

        // 等待GPU完成所有操作（确保资源已上传到GPU）
        Fence::waitAndDestroy(engine->createFence());
        free(data);
    }
    close(fd);

    return mesh;
}

/**
 * 从缓冲区加载网格实现（使用默认材质）
 * 
 * 执行步骤：
 * 1. 创建材质注册表
 * 2. 注册默认材质
 * 3. 调用完整版本的loadMeshFromBuffer
 * 
 * @param engine Filament引擎指针
 * @param data 网格数据缓冲区
 * @param destructor 数据释放回调函数
 * @param user 用户数据指针
 * @param defaultMaterial 默认材质实例
 * @return 加载的网格对象
 */
MeshReader::Mesh MeshReader::loadMeshFromBuffer(filament::Engine* engine,
        void const* data, Callback destructor, void* user,
        MaterialInstance* defaultMaterial) {
    MaterialRegistry reg;
    // 注册默认材质
    reg.registerMaterialInstance(utils::CString(DEFAULT_MATERIAL), defaultMaterial);
    return loadMeshFromBuffer(engine, data, destructor, user, reg);
}

/**
 * 从缓冲区加载网格实现（完整版本）
 * 
 * 执行步骤：
 * 1. 验证文件魔数（MAGICID）
 * 2. 解析文件头（Header）
 * 3. 定位顶点数据、索引数据、部分数据
 * 4. 解析材质名称列表
 * 5. 创建索引缓冲区（处理压缩）
 * 6. 创建顶点缓冲区（处理压缩和交错/非交错格式）
 * 7. 创建可渲染实体和部分
 * 8. 设置材质
 * 
 * @param engine Filament引擎指针
 * @param data 网格数据缓冲区
 * @param destructor 数据释放回调函数（在数据不再需要时调用）
 * @param user 用户数据指针（传递给destructor）
 * @param materials 材质注册表
 * @return 加载的网格对象
 */
MeshReader::Mesh MeshReader::loadMeshFromBuffer(filament::Engine* engine,
        void const* data, Callback destructor, void* user,
        MaterialRegistry& materials) {
    const uint8_t* p = (const uint8_t*) data;
    
    // 验证文件魔数（检查是否为有效的filamesh文件）
    if (strncmp(MAGICID, (const char *) p, 8)) {
        utils::slog.e << "Magic string not found." << utils::io::endl;
        return {};
    }
    p += 8;  // 跳过魔数

    // 解析文件头（包含网格的元数据信息）
    Header* header = (Header*) p;
    p += sizeof(Header);

    // 定位顶点数据区域
    uint8_t const* vertexData = p;
    p += header->vertexSize;

    // 定位索引数据区域
    uint8_t const* indices = p;
    p += header->indexSize;

    // 定位部分数据区域（每个部分对应一个子网格）
    Part* parts = (Part*) p;
    p += header->parts * sizeof(Part);

    // 读取材质数量
    uint32_t materialCount = (uint32_t) *p;
    p += sizeof(uint32_t);

    // 读取材质名称列表（每个材质名称以null结尾）
    std::vector<std::string> partsMaterial(materialCount);
    for (size_t i = 0; i < materialCount; i++) {
        uint32_t nameLength = (uint32_t) *p;  // 读取名称长度
        p += sizeof(uint32_t);
        partsMaterial[i] = (const char*) p;  // 读取名称字符串
        p += nameLength + 1; // null terminated（跳过null终止符）
    }

    Mesh mesh;

    // 创建索引缓冲区
    // 根据索引类型（UI16或UINT）设置缓冲区类型
    mesh.indexBuffer = IndexBuffer::Builder()
            .indexCount(header->indexCount)  // 索引数量
            .bufferType(header->indexType == UI16 ? IndexBuffer::IndexType::USHORT
                    : IndexBuffer::IndexType::UINT)  // 索引类型（16位或32位）
            .build(*engine);

    // If the index buffer is compressed, then decode the indices into a temporary buffer.
    // The user callback can be called immediately afterwards because the source data does not get
    // passed to the GPU.
    // 如果索引缓冲区被压缩，则解码索引到临时缓冲区。
    // 用户回调可以立即调用，因为源数据不会传递给GPU。
    const size_t indicesSize = header->indexSize;
    if (header->flags & COMPRESSION) {
        // 索引缓冲区被压缩，需要解码
        size_t indexSize = header->indexType == UI16 ? sizeof(uint16_t) : sizeof(uint32_t);
        size_t indexCount = header->indexCount;
        size_t uncompressedSize = indexSize * indexCount;
        void* uncompressed = malloc(uncompressedSize);
        
        // 使用meshoptimizer解码索引缓冲区
        int err = meshopt_decodeIndexBuffer(uncompressed, indexCount, indexSize, indices,
                indicesSize);
        if (err) {
            utils::slog.e << "Unable to decode index buffer." << utils::io::endl;
            return {};
        }
        
        // 如果提供了释放回调，立即调用（因为源数据不再需要）
        if (destructor) {
            destructor((void*) indices, indicesSize, user);
        }
        
        // 设置解码后的索引数据（使用free作为释放回调）
        auto freecb = [](void* buffer, size_t size, void* user) { free(buffer); };
        mesh.indexBuffer->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(uncompressed, uncompressedSize, freecb, nullptr));
    } else {
        // 索引缓冲区未压缩，直接使用源数据
        mesh.indexBuffer->setBuffer(*engine,
                IndexBuffer::BufferDescriptor(indices, indicesSize, destructor, user));
    }

    // 创建顶点缓冲区构建器
    VertexBuffer::Builder vbb;
    vbb.vertexCount(header->vertexCount)  // 顶点数量
            .bufferCount(1)  // 缓冲区数量（单个缓冲区包含所有属性）
            .normalized(VertexAttribute::COLOR)  // 颜色属性使用归一化格式
            .normalized(VertexAttribute::TANGENTS);  // 切向量属性使用归一化格式

    // 根据标志位确定UV坐标类型（有符号归一化SHORT2或半精度浮点HALF2）
    VertexBuffer::AttributeType uvtype = (header->flags & TEXCOORD_SNORM16) ?
            VertexBuffer::AttributeType::SHORT2 : VertexBuffer::AttributeType::HALF2;

    // 设置顶点属性（位置、切向量、颜色、UV0）
    vbb
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::HALF4,
                        header->offsetPosition, uint8_t(header->stridePosition))  // 位置（half4格式）
            .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::SHORT4,
                        header->offsetTangents, uint8_t(header->strideTangents))  // 切向量（short4格式，归一化）
            .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::UBYTE4,
                        header->offsetColor, uint8_t(header->strideColor))  // 颜色（ubyte4格式，归一化）
            .attribute(VertexAttribute::UV0, 0, uvtype,
                        header->offsetUV0, uint8_t(header->strideUV0))  // UV0坐标
            .normalized(VertexAttribute::UV0, header->flags & TEXCOORD_SNORM16);  // 如果使用SHORT2，则归一化

    // 检查是否有UV1坐标（通过检查偏移量和步长是否为最大值来判断）
    constexpr uint32_t uintmax = std::numeric_limits<uint32_t>::max();
    const bool hasUV1 = header->offsetUV1 != uintmax && header->strideUV1 != uintmax;

    if (hasUV1) {
        // 如果存在UV1，添加UV1属性
        vbb
            .attribute(VertexAttribute::UV1, 0, VertexBuffer::AttributeType::HALF2,
                    header->offsetUV1, uint8_t(header->strideUV1))  // UV1坐标（half2格式）
            .normalized(VertexAttribute::UV1);  // UV1使用归一化格式
    }

    // 构建顶点缓冲区
    mesh.vertexBuffer = vbb.build(*engine);

    // If the vertex buffer is compressed, then decode the vertices into a temporary buffer.
    // The user callback can be called immediately afterwards because the source data does not get
    // passed to the GPU.
    // 如果顶点缓冲区被压缩，则解码顶点到临时缓冲区。
    // 用户回调可以立即调用，因为源数据不会传递给GPU。
    const size_t verticesSize = header->vertexSize;
    if (header->flags & COMPRESSION) {
        // 顶点缓冲区被压缩，需要解码
        // 计算解压后的顶点大小（所有属性的大小之和）
        size_t vertexSize = sizeof(half4) + sizeof(short4) + sizeof(ubyte4) + sizeof(ushort2) +
                (hasUV1 ? sizeof(ushort2) : 0);
        size_t vertexCount = header->vertexCount;
        size_t uncompressedSize = vertexSize * vertexCount;
        void* uncompressed = malloc(uncompressedSize);
        
        // 跳过压缩头，定位到压缩数据
        const uint8_t* srcdata = vertexData + sizeof(CompressionHeader);
        int err = 0;
        
        if (header->flags & INTERLEAVED) {
            // 交错格式：所有属性交错存储，一次性解码
            err |= meshopt_decodeVertexBuffer(uncompressed, vertexCount, vertexSize, srcdata,
                    vertexSize);
        } else {
            // 非交错格式：每个属性单独压缩，需要分别解码
            const CompressionHeader* sizes = (CompressionHeader*) vertexData;
            uint8_t* dstdata = (uint8_t*) uncompressed;
            auto decode = meshopt_decodeVertexBuffer;

            // 解码位置数据
            err |= decode(dstdata, vertexCount, sizeof(half4), srcdata, sizes->positions);
            srcdata += sizes->positions;
            dstdata += sizeof(half4) * vertexCount;

            // 解码切向量数据
            err |= decode(dstdata, vertexCount, sizeof(short4), srcdata, sizes->tangents);
            srcdata += sizes->tangents;
            dstdata += sizeof(short4) * vertexCount;

            // 解码颜色数据
            err |= decode(dstdata, vertexCount, sizeof(ubyte4), srcdata, sizes->colors);
            srcdata += sizes->colors;
            dstdata += sizeof(ubyte4) * vertexCount;

            // 解码UV0数据
            err |= decode(dstdata, vertexCount, sizeof(ushort2), srcdata, sizes->uv0);

            // 如果存在UV1，解码UV1数据
            if (sizes->uv1) {
                srcdata += sizes->uv0;
                dstdata += sizeof(ushort2) * vertexCount;
                err |= decode(dstdata, vertexCount, sizeof(ushort2), srcdata, sizes->uv1);
            }
        }
        
        if (err) {
            utils::slog.e << "Unable to decode vertex buffer." << utils::io::endl;
            return {};
        }
        
        // 如果提供了释放回调，立即调用（因为源数据不再需要）
        if (destructor) {
            destructor((void*) vertexData, verticesSize, user);
        }
        
        // 设置解码后的顶点数据（使用free作为释放回调）
        auto freecb = [](void* buffer, size_t size, void* user) { free(buffer); };
        mesh.vertexBuffer->setBufferAt(*engine, 0,
                IndexBuffer::BufferDescriptor(uncompressed, uncompressedSize, freecb, nullptr));
    } else {
        // 顶点缓冲区未压缩，直接使用源数据
        mesh.vertexBuffer->setBufferAt(*engine, 0,
                VertexBuffer::BufferDescriptor(vertexData, verticesSize, destructor, user));
    }

    // 创建可渲染实体
    mesh.renderable = utils::EntityManager::get().create();

    // 创建可渲染管理器构建器（指定部分数量）
    RenderableManager::Builder builder(header->parts);
    builder.boundingBox(header->aabb);  // 设置包围盒

    // 获取默认材质实例
    const auto defaultmi = materials.getMaterialInstance(utils::CString(DEFAULT_MATERIAL));
    
    // 为每个部分设置几何和材质
    for (size_t i = 0; i < header->parts; i++) {
        // 设置几何数据（三角形图元，使用共享的顶点和索引缓冲区）
        builder.geometry(i, RenderableManager::PrimitiveType::TRIANGLES,
                mesh.vertexBuffer, mesh.indexBuffer, parts[i].offset,
                parts[i].minIndex, parts[i].maxIndex, parts[i].indexCount);

        // It may happen that there are more parts than materials
        // therefore we have to use Part::material instead of i.
        // 可能发生部分数量多于材质数量的情况，因此必须使用Part::material而不是i
        uint32_t materialIndex = parts[i].material;
        if (materialIndex >= partsMaterial.size()) {
            utils::slog.e << "Material index (" << materialIndex << ") of mesh part ("
                    << i << ") is out of bounds (" << partsMaterial.size() << ")" << utils::io::endl;
            continue;
        }

        // 获取材质名称并查找材质实例
        const utils::CString materialName(
                partsMaterial[materialIndex].c_str(), partsMaterial[materialIndex].size());
        const auto mat = materials.getMaterialInstance(materialName);
        
        if (mat == nullptr) {
            // 如果材质未找到，使用默认材质并注册
            builder.material(i, defaultmi);
            materials.registerMaterialInstance(materialName, defaultmi);
        } else {
            // 使用找到的材质实例
            builder.material(i, mat);
        }
    }
    
    // 构建可渲染实体
    builder.build(*engine, mesh.renderable);

    return mesh;
}

} // namespace filamesh
