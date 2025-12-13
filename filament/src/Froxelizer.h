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

#ifndef TNT_FILAMENT_DETAILS_FROXELIZER_H
#define TNT_FILAMENT_DETAILS_FROXELIZER_H

#include "Allocators.h"

#include "details/Scene.h"
#include "details/Engine.h"

#include "private/filament/EngineEnums.h"
#include "private/filament/UibStructs.h"

#include <filament/View.h>
#include <filament/Viewport.h>

#include <backend/Handle.h>

#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/bitset.h>
#include <utils/Slice.h>

#include <cstdint>
#include <cstddef>
#include <limits>
#include <utility>

namespace filament {

class FEngine;
class FCamera;
class FTexture;

/**
 * Froxel 类
 * 
 * 表示一个 Froxel（Frustum Voxel），即视锥体中的一个体素单元。
 * 用于将光源分配到空间区域中，以便在着色器中高效地查找影响像素的光源。
 */
class Froxel {
public:
    /**
     * 平面枚举
     * 
     * 定义 Froxel 的 6 个边界平面
     */
    enum Planes {
        LEFT,    // 左平面
        RIGHT,   // 右平面
        BOTTOM,  // 下平面
        TOP,     // 上平面
        NEAR,    // 近平面
        FAR      // 远平面
    };
    /**
     * 6 个边界平面（每个平面用 float4 表示：法向量 xyz + 距离 w）
     */
    math::float4 planes[6];
};

//
// Light UBO           Froxel Record UBO      per-froxel light list texture
// {4 x float4}            {index into        RG_U16 {offset, point-count, spot-count}
// (spot/point            light texture}
//                     {uint4 -> 16 indices}
//
//  +----+                     +-+                     +----+
// 0|....| <------------+     0| |         +-----------|0230| (e.g. offset=02, 3-lights)
// 1|....|<--------+     \    1| |        /            |    |
// 2:    :          \     +---2|0|<------+             |    |
// 3:    : <-------- \--------3|3|                     :    :
// 4:    :            +------- :1:                     :    :
//  :    :                     : :                     :    :
//  :    :                     | |                     |    |
//  :    :                     | |                     |    |
//  :    :                     +-+                     |    |
//  :    :                  65536 max                  +----+
//  |....|                                          h = num froxels
//  |....|
//  +----+
// 256 lights max
//

/**
 * Froxelizer 类
 * 
 * 将光源分配到 Froxel 网格中，用于高效的光照计算。
 * Froxel 是一个 3D 网格，将视锥体划分为多个体素单元。
 * 每个 Froxel 包含影响该区域的光源列表。
 */
class Froxelizer {
public:
    /**
     * 构造函数
     * 
     * @param engine 引擎引用
     */
    explicit Froxelizer(FEngine& engine);
    /**
     * 析构函数
     */
    ~Froxelizer();

    /**
     * 终止 Froxelizer
     * 
     * 清理所有资源，必须在析构前调用。
     * 
     * @param driverApi 驱动 API 引用
     */
    void terminate(backend::DriverApi& driverApi) noexcept;

    /**
     * 获取记录缓冲区句柄
     * 
     * 返回包含光源索引记录的 GPU 缓冲区。
     * 在构造后有效。
     * 
     * @return 记录缓冲区句柄
     */
    // GPU buffer containing records. Valid after construction.
    backend::Handle<backend::HwBufferObject> getRecordBuffer() const noexcept {
        return mRecordsBuffer;
    }

    /**
     * 获取 Froxel 缓冲区句柄
     * 
     * 返回包含 Froxel 数据的 GPU 缓冲区。
     * 在构造后有效。
     * 
     * @return Froxel 缓冲区句柄
     */
    // GPU buffer containing froxels. Valid after construction.
    backend::Handle<backend::HwBufferObject> getFroxelBuffer() const noexcept {
        return mFroxelsBuffer;
    }

    /**
     * 设置选项
     * 
     * 设置光源的近平面和远平面距离。
     * 
     * @param zLightNear 光源近平面距离
     * @param zLightFar 光源远平面距离
     */
    void setOptions(float zLightNear, float zLightFar) noexcept;

    /**
     * 准备 Froxelization
     * 
     * 为 Froxelization 分配每帧数据结构。
     * 
     * @param driverApi 驱动 API 引用（用于在流中分配内存）
     * @param rootArenaScope 根分配器作用域（用于分配每帧内存）
     * @param viewport 视口（用于计算 Froxel 尺寸）
     * @param projection 相机投影矩阵
     * @param projectionNear 近平面
     * @param projectionFar 远平面
     * @param clipTransform 裁剪变换（调试用，已包含在投影中的 clipTransform）
     * @return 如果需要调用 updateUniforms() 则返回 true
     */
    /*
     * Allocate per-frame data structures for froxelization.
     *
     * driverApi         used to allocate memory in the stream
     * arena             used to allocate per-frame memory
     * viewport          used to calculate froxel dimensions
     * projection        camera projection matrix
     * projectionNear    near plane
     * projectionFar     far plane
     * clipTransform     [debugging] the clipTransform that's already included in the projection
     *
     * return true if updateUniforms() needs to be called
     */
    bool prepare(backend::DriverApi& driverApi, RootArenaScope& rootArenaScope,
            Viewport const& viewport,
            const math::mat4f& projection, float projectionNear, float projectionFar,
            math::float4 const& clipTransform) noexcept;

    /**
     * 获取指定位置的 Froxel
     * 
     * @param x X 方向索引
     * @param y Y 方向索引
     * @param z Z 方向索引
     * @return Froxel 对象
     */
    Froxel getFroxelAt(size_t x, size_t y, size_t z) const noexcept;
    /**
     * 获取 X 方向 Froxel 数量
     * 
     * @return X 方向 Froxel 数量
     */
    size_t getFroxelCountX() const noexcept { return mFroxelCountX; }
    /**
     * 获取 Y 方向 Froxel 数量
     * 
     * @return Y 方向 Froxel 数量
     */
    size_t getFroxelCountY() const noexcept { return mFroxelCountY; }
    /**
     * 获取 Z 方向 Froxel 数量
     * 
     * @return Z 方向 Froxel 数量
     */
    size_t getFroxelCountZ() const noexcept { return mFroxelCountZ; }
    /**
     * 获取总 Froxel 数量
     * 
     * @return 总 Froxel 数量（X * Y * Z）
     */
    size_t getFroxelCount() const noexcept { return mFroxelCount; }

    /**
     * 获取光源远平面距离
     * 
     * @return 光源远平面距离
     */
    float getLightFar() const noexcept { return mZLightFar; }

    /**
     * Froxelize 光源
     * 
     * 使用光源数据更新记录和 Froxel 纹理。
     * 此函数是线程安全的。
     * 
     * @param engine 引擎引用
     * @param viewMatrix 视图矩阵
     * @param lightData 光源数据（SOA 格式）
     */
    // Update Records and Froxels texture with lights data. This is thread-safe.
    void froxelizeLights(FEngine& engine, math::mat4f const& viewMatrix,
            const FScene::LightSoa& lightData) noexcept;

    /**
     * 更新 Uniform 数据
     * 
     * 将 Froxelizer 的参数更新到 PerViewUib 结构中。
     * 
     * @param s PerViewUib 引用
     */
    void updateUniforms(PerViewUib& s) const {
        s.zParams = mParamsZ;  // Z 方向参数
        s.fParams = mParamsF;  // Froxel 索引参数
        s.froxelCountXY = math::float2{ mViewport.width, mViewport.height } / mFroxelDimension;  // Froxel 计数（XY 方向）
    }

    /**
     * 获取 Froxel 缓冲区字节数
     * 
     * 计算所需的 Froxel 缓冲区大小。
     * 
     * @param driverApi 驱动 API 引用
     * @return 缓冲区大小（字节）
     */
    static size_t getFroxelBufferByteCount(FEngine::DriverApi& driverApi) noexcept;

    /**
     * 获取 Froxel 记录缓冲区字节数
     * 
     * 计算所需的记录缓冲区大小。
     * 
     * @param driverApi 驱动 API 引用
     * @return 缓冲区大小（字节）
     */
    static size_t getFroxelRecordBufferByteCount(FEngine::DriverApi& driverApi) noexcept;

    /**
     * 提交 Froxel 数据到 GPU
     * 
     * 将计算好的 Froxel 数据上传到 GPU 缓冲区。
     * 
     * @param driverApi 驱动 API 引用
     */
    // send froxel data to GPU
    void commit(backend::DriverApi& driverApi);


    /**
     * 仅用于测试/调试...
     */
    /*
     * Only for testing/debugging...
     */

    /**
     * Froxel 条目结构
     * 
     * 表示一个 Froxel 的光源列表条目。
     * 包含偏移量和光源数量，打包在一个 32 位整数中。
     */
    struct FroxelEntry {
        /**
         * 构造函数
         * 
         * @param offset 偏移量（16 位）
         * @param count 光源数量（8 位）
         */
        FroxelEntry(uint16_t const offset, uint8_t const count) noexcept
            : u32((offset << 16) | count) { }  // 打包：offset 在高 16 位，count 在低 8 位

        /**
         * 获取光源数量
         * 
         * @return 光源数量（低 8 位）
         */
        uint8_t count() const noexcept { return u32 & 0xFFu; }
        
        /**
         * 获取偏移量
         * 
         * @return 偏移量（高 16 位）
         */
        uint16_t offset() const noexcept { return u32 >> 16u; }
        
        uint32_t u32 = 0;  // 打包的 32 位值
    };
    static_assert(sizeof(FroxelEntry) == 4u);  // 确保大小为 4 字节

    /**
     * 记录缓冲区类型
     * 
     * 我们不能轻易更改这个，因为着色器期望每个 uint4 有 16 个索引。
     */
    // We can't change this easily because the shader expects 16 indices per uint4
    using RecordBufferType = uint8_t;  // 记录缓冲区类型（每个索引 1 字节）

    /**
     * 获取 Froxel 缓冲区用户切片
     * 
     * @return Froxel 缓冲区切片
     */
    utils::Slice<const FroxelEntry> getFroxelBufferUser() const { return mFroxelBufferUser; }
    
    /**
     * 获取记录缓冲区用户切片
     * 
     * @return 记录缓冲区切片
     */
    utils::Slice<const RecordBufferType> getRecordBufferUser() const { return mRecordBufferUser; }

    /**
     * 光源组类型
     * 
     * 选择这个类型是为了让 froxelizePointAndSpotLight() 向量化 4 个 Froxel 测试/聚光灯。
     * 使用 256 个光源时，这意味着 8 个作业（256 / 32）用于 Froxelization。
     */
    // This is chosen so froxelizePointAndSpotLight() vectorizes 4 froxel tests / spotlight
    // with 256 lights this implies 8 jobs (256 / 32) for froxelization.
    using LightGroupType = uint32_t;  // 光源组类型（32 位，每组 32 个光源）

    /**
     * 获取 Froxel 配置信息
     * 
     * @return Froxel 配置信息
     */
    View::FroxelConfigurationInfo getFroxelConfigurationInfo() const noexcept;

private:
    size_t getFroxelBufferEntryCount() const noexcept {
        // We guarantee that mFroxelBufferEntryCount is a multiple of 16. With this knowledge
        // the compiler can do a much better job at vectorizing. For similar reasons, it's
        // important to keep mFroxelBufferEntryCount an uint32_t (as opposed to a size_t).
        assert_invariant((mFroxelBufferEntryCount & 0xF) == 0);
        UTILS_ASSUME((mFroxelBufferEntryCount & 0xF) == 0);
        UTILS_ASSUME(mFroxelBufferEntryCount >= 16);
        return mFroxelBufferEntryCount;
    }

    /**
     * 光源记录结构
     * 
     * 使用位集跟踪哪些光源影响某个 Froxel。
     */
    struct LightRecord {
        /**
         * 位集类型
         * 
         * 每个位代表一个光源，最多支持 CONFIG_MAX_LIGHT_COUNT 个光源。
         */
        using bitset = utils::bitset<uint64_t, (CONFIG_MAX_LIGHT_COUNT + 63) / 64>;  // 位集类型
        bitset lights;  // 光源位集
    };

    /**
     * 光源参数结构
     * 
     * 存储用于 Froxelization 的光源参数。
     */
    struct LightParams {
        math::float3 position;  // 光源位置
        float cosSqr;  // 聚光灯外角余弦的平方
        math::float3 axis;  // 聚光灯方向轴
        /**
         * 必须初始化为无穷大以表示这是点光源
         */
        // this must be initialized to indicate this is a point light
        float invSin = std::numeric_limits<float>::infinity();  // 1/sin(外角)（点光源为无穷大）
        /**
         * 半径不在热循环中使用，所以放在最后
         */
        // radius is not used in the hot loop, so leave it at the end
        float radius;  // 光源半径
    };

    /**
     * 光源树节点结构
     * 
     * 用于光源的 Z 范围树，用于快速剔除不在 Z 范围内的光源。
     */
    struct LightTreeNode {
        float min;          // 光源 Z 范围最小值
        float max;          // 光源 Z 范围最大值

        uint16_t next;      // 范围测试失败时的下一个节点
        uint16_t offset;    // 记录缓冲区中的偏移量

        uint8_t isLeaf;     // 是否为叶子节点
        uint8_t count;      // 记录缓冲区中的光源数量
        uint16_t reserved;  // 保留字段
    };

    struct FroxelThreadData;

    /**
     * 设置视口
     * 
     * @param viewport 视口
     */
    inline void setViewport(Viewport const& viewport) noexcept;
    
    /**
     * 设置投影
     * 
     * @param projection 投影矩阵
     * @param near 近平面
     * @param far 远平面
     */
    inline void setProjection(const math::mat4f& projection, float near, float far) noexcept;
    
    /**
     * 更新内部状态
     * 
     * 如果视口或投影发生变化，更新 Froxel 网格。
     * 
     * @return 如果状态已更新则返回 true
     */
    bool update() noexcept;

    /**
     * Froxelize 循环
     * 
     * 主循环，将光源分配到 Froxel 中。
     * 
     * @param engine 引擎引用
     * @param viewMatrix 视图矩阵
     * @param lightData 光源数据（SOA 格式）
     */
    void froxelizeLoop(FEngine& engine,
            math::mat4f const& viewMatrix, const FScene::LightSoa& lightData) const noexcept;

    /**
     * Froxelize 分配记录压缩
     * 
     * 压缩并分配光源记录到 Froxel 缓冲区。
     */
    void froxelizeAssignRecordsCompress() noexcept;

    /**
     * Froxelize 点和聚光灯
     * 
     * 将单个点光源或聚光灯分配到相关的 Froxel 中。
     * 
     * @param froxelThread Froxel 线程数据引用
     * @param bit 光源位索引
     * @param projection 投影矩阵
     * @param light 光源参数
     */
    void froxelizePointAndSpotLight(FroxelThreadData& froxelThread, size_t bit,
            math::mat4f const& projection, const LightParams& light) const noexcept;

    /**
     * 计算光源树
     * 
     * 构建光源的 Z 范围树，用于快速剔除。
     * 
     * @param lightTree 光源树节点数组（输出）
     * @param lightList 光源列表
     * @param lightData 光源数据（SOA 格式）
     * @param lightRecordsOffset 光源记录偏移量
     */
    static void computeLightTree(LightTreeNode* lightTree,
            utils::Slice<const RecordBufferType> lightList,
            const FScene::LightSoa& lightData, size_t lightRecordsOffset) noexcept;

    /**
     * 更新边界球
     * 
     * 为每个 Froxel 计算边界球，用于快速相交测试。
     * 
     * @param boundingSpheres 边界球数组（输出）
     * @param froxelCountX X 方向 Froxel 数量
     * @param froxelCountY Y 方向 Froxel 数量
     * @param froxelCountZ Z 方向 Froxel 数量
     * @param planesX X 方向平面数组
     * @param planesY Y 方向平面数组
     * @param planesZ Z 方向平面数组
     */
    static void updateBoundingSpheres(
            math::float4* UTILS_RESTRICT boundingSpheres,
            size_t froxelCountX, size_t froxelCountY, size_t froxelCountZ,
            math::float4 const* UTILS_RESTRICT planesX,
            math::float4 const* UTILS_RESTRICT planesY,
            float const* UTILS_RESTRICT planesZ) noexcept;

    /**
     * 获取 Froxel 索引（静态版本）
     * 
     * 根据 3D 坐标计算 Froxel 的线性索引。
     * 
     * @param ix X 索引
     * @param iy Y 索引
     * @param iz Z 索引
     * @param froxelCountX X 方向 Froxel 数量
     * @param froxelCountY Y 方向 Froxel 数量
     * @return Froxel 索引
     */
    static size_t getFroxelIndex(size_t const ix, size_t const iy, size_t const iz,
            size_t const froxelCountX, size_t const froxelCountY) noexcept {
        return ix + (iy * froxelCountX) + (iz * froxelCountX * froxelCountY);  // 线性索引计算
    }

    /**
     * 获取 Froxel 索引（实例版本）
     * 
     * @param ix X 索引
     * @param iy Y 索引
     * @param iz Z 索引
     * @return Froxel 索引
     */
    size_t getFroxelIndex(size_t const ix, size_t const iy, size_t const iz) const noexcept {
        return getFroxelIndex(ix, iy, iz, mFroxelCountX, mFroxelCountY);  // 使用成员变量
    }

    /**
     * 查找 Z 切片
     * 
     * 根据视图空间 Z 坐标查找对应的 Z 切片索引。
     * 
     * @param viewSpaceZ 视图空间 Z 坐标
     * @return Z 切片索引
     */
    size_t findSliceZ(float viewSpaceZ) const noexcept UTILS_PURE;

    /**
     * 裁剪坐标到索引
     * 
     * 将裁剪空间坐标转换为 Froxel 索引。
     * 
     * @param clip 裁剪坐标
     * @return (X 索引, Y 索引) 对
     */
    std::pair<size_t, size_t> clipToIndices(math::float2 const& clip) const noexcept;

    /**
     * 计算 Froxel 布局
     * 
     * 根据缓冲区大小和视口计算 Froxel 网格的尺寸和数量。
     * 
     * @param dim Froxel 尺寸（输出）
     * @param countX X 方向数量（输出）
     * @param countY Y 方向数量（输出）
     * @param countZ Z 方向数量（输出）
     * @param froxelBufferEntryCount Froxel 缓冲区条目数
     * @param viewport 视口
     */
    static void computeFroxelLayout(
            math::uint2* dim, uint16_t* countX, uint16_t* countY, uint16_t* countZ,
            size_t froxelBufferEntryCount, Viewport const& viewport) noexcept;

    // internal state dependent on the viewport and needed for froxelizing
    LinearAllocatorArena mArena;                        // ~256 KiB

    // 4096 froxels fits in a 16KiB buffer, the minimum guaranteed in GLES 3.x and Vulkan 1.1
    uint32_t mFroxelBufferEntryCount = 4096;

    // 16384 entries is our minimum with a 16KiB buffer
    uint32_t mFroxelRecordBufferEntryCount = 16384;

    // allocations in the private froxel arena
    float* mDistancesZ = nullptr;
    math::float4* mPlanesX = nullptr;
    math::float4* mPlanesY = nullptr;
    math::float4* mBoundingSpheres = nullptr;           // 64 KiB w/ 4096 froxels

    // allocations in the per frame arena
    //        max |  real | size
    //       8192 |  4096 | 512 KiB
    //       8192 |  8192 | 768 KiB
    //      65536 | 65536 | 6.0 MiB
    utils::Slice<LightRecord> mLightRecords;            // 256 KiB w/  256 lights and 4096 froxels
    utils::Slice<FroxelThreadData> mFroxelShardedData;  // 256 KiB w/  256 lights and 8192 max froxels

    // allocations in the command stream
    utils::Slice<FroxelEntry> mFroxelBufferUser;        //  16 KiB w/ 4096 froxels
    utils::Slice<RecordBufferType> mRecordBufferUser;   //  16 KiB to 64 KiB

    uint16_t mFroxelCountX = 0;
    uint16_t mFroxelCountY = 0;
    uint16_t mFroxelCountZ = 0;
    uint32_t mFroxelCount = 0;
    math::uint2 mFroxelDimension = {};
    math::float4 mClipTransform = { 1, 1, 0, 0 };

    math::mat4f mProjection;
    math::float2 mLinearizer{};
    float mClipToFroxelX = 0.0f;
    float mClipToFroxelY = 0.0f;
    backend::BufferObjectHandle mRecordsBuffer;
    backend::BufferObjectHandle mFroxelsBuffer;

    // needed for update()
    Viewport mViewport;
    math::float4 mParamsZ = {};
    math::uint3 mParamsF = {};
    float mNear = 0.0f;        // camera near
    float mFar = 0.0f;         // culling camera far
    float mZLightNear;
    float mZLightFar;
    float mUserZLightNear;
    float mUserZLightFar;

    // track if we need to update our internal state before froxelizing
    uint8_t mDirtyFlags = 0;
    enum {
        VIEWPORT_CHANGED = 0x01,
        PROJECTION_CHANGED = 0x02,
        OPTIONS_CHANGED = 0x04
    };

    View::FroxelConfigurationInfo mFroxelConfigurationInfo{};
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_FROXELIZER_H
