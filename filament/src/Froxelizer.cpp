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

#include "Froxelizer.h"

#include "Allocators.h"
#include "Intersections.h"

#include "components/LightManager.h"

#include "details/Engine.h"
#include "details/Scene.h"

#include <private/filament/EngineEnums.h>
#include <private/utils/Tracing.h>
#include <private/backend/DriverApi.h>

#include <filament/Box.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <backend/DriverEnums.h>

#include <utils/architecture.h>
#include <utils/BinaryTreeArray.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/JobSystem.h>
#include <utils/Logger.h>
#include <utils/ostream.h>
#include <utils/Slice.h>

#include <math/fast.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/scalar.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

using namespace filament::math;
using namespace utils;

namespace filament {

using namespace backend;

// The number of froxel buffer entries is only limited by the maximum UBO size (see
// getFroxelBufferByteCount()), each entry consumes 4 bytes, so with a 16KB UBO, we get
// 4096 froxels.
// Increasing this value too much adds pressure on the record buffer, which is also limited
// to min(16K[ubo], 64K[uint16]) entries. In practice not all froxels are used.
constexpr size_t FROXEL_BUFFER_MAX_ENTRY_COUNT = 8192;

// Froxel buffer UBO is an array of uvec4. Make sure that the buffer is properly aligned.
static_assert(FROXEL_BUFFER_MAX_ENTRY_COUNT % 4 == 0u);

// TODO: these should come from a configuration object on View or Camera
static constexpr size_t FROXEL_SLICE_COUNT = 16;

// This is overridden by setOptions()
static constexpr float FROXEL_FIRST_SLICE_DEPTH_DEFAULT   = 5;      // 5m
static constexpr float FROXEL_LAST_SLICE_DISTANCE_DEFAULT = 100;    // 100m

// Buffer needed for Froxelizer internal data structures (~256 KiB)
constexpr size_t PER_FROXELDATA_ARENA_SIZE = sizeof(float4) *
                                             (FROXEL_BUFFER_MAX_ENTRY_COUNT +
                                              FROXEL_BUFFER_MAX_ENTRY_COUNT + 3 +
                                              FROXEL_SLICE_COUNT / 4 + 1);

// Number of lights processed by one group (e.g. 32)
static constexpr size_t LIGHT_PER_GROUP = sizeof(Froxelizer::LightGroupType) * 8;

// Number of groups (i.e. jobs) to use for froxelization (e.g. 8)
static constexpr size_t GROUP_COUNT =
        (CONFIG_MAX_LIGHT_COUNT + LIGHT_PER_GROUP - 1) / LIGHT_PER_GROUP;

// This depends on the maximum number of lights (currently 256)
static_assert(CONFIG_MAX_LIGHT_INDEX <= std::numeric_limits<Froxelizer::RecordBufferType>::max(),
        "can't have more than 256 lights");

struct Froxelizer::FroxelThreadData :
        public std::array<LightGroupType, FROXEL_BUFFER_MAX_ENTRY_COUNT> {
};


/**
 * 模糊相等比较（矩阵）
 * 
 * 比较两个矩阵是否相等（逐位比较）。
 * 如果两个矩阵不同，返回 false。
 * 如果两个矩阵相同，但某些元素仅相差 +0 或 -0，可能返回 false。
 * 如果包含 NaN，行为未定义。
 * 
 * @param l 左侧矩阵
 * @param r 右侧矩阵
 * @return 如果矩阵相等则返回 true
 */
// Returns false if the two matrices are different. May return false if they're the
// same, with some elements only differing by +0 or -0. Behaviour is undefined with NaNs.
static bool fuzzyEqual(mat4f const& UTILS_RESTRICT l, mat4f const& UTILS_RESTRICT r) noexcept {
    /**
     * 将矩阵转换为 uint32_t 数组进行逐位比较
     */
    auto const li = reinterpret_cast<uint32_t const*>( reinterpret_cast<char const*>(&l) );
    auto const ri = reinterpret_cast<uint32_t const*>( reinterpret_cast<char const*>(&r) );
    /**
     * 累积异或结果
     */
    uint32_t result = 0;
    /**
     * 逐位比较所有元素（clang 会完全向量化此循环）
     */
    for (size_t i = 0; i < sizeof(mat4f) / sizeof(uint32_t); i++) {
        // clang fully vectorizes this
        result |= li[i] ^ ri[i];
    }
    /**
     * 如果所有位都相同，result 为 0
     */
    return result == 0;
}

/**
 * 获取 Froxel 缓冲区字节数
 * 
 * 计算 Froxel 缓冲区的大小，确保 16 字节对齐以适配 uvec4 数组。
 * 
 * @param driverApi 驱动 API 引用
 * @return 缓冲区字节数
 */
size_t Froxelizer::getFroxelBufferByteCount(FEngine::DriverApi& driverApi) noexcept {
    // Make sure that targetSize is 16-byte aligned so that it'll fit properly into an array of
    // uvec4.
    /**
     * 计算目标大小（16 字节对齐）
     */
    size_t const targetSize = (driverApi.getMaxUniformBufferSize() / 16) * 16;
    /**
     * 返回最大条目数 * 条目大小和目标大小的较小值
     */
    return std::min(FROXEL_BUFFER_MAX_ENTRY_COUNT * sizeof(FroxelEntry), targetSize);
}

/**
 * 获取 Froxel 记录缓冲区字节数
 * 
 * 计算 Froxel 记录缓冲区的大小，确保 16 字节对齐。
 * 最大大小为 64K 条目，因为使用 16 位索引。
 * 
 * @param driverApi 驱动 API 引用
 * @return 缓冲区字节数
 */
size_t Froxelizer::getFroxelRecordBufferByteCount(FEngine::DriverApi& driverApi) noexcept {
    // Make sure that targetSize is 16-byte aligned so that it'll fit properly into an array of
    // uvec4. The maximum size is 64K entries, because we're using 16 bits indices.
    /**
     * 计算目标大小（16 字节对齐）
     */
    size_t const targetSize = (driverApi.getMaxUniformBufferSize() / 16) * 16;
    /**
     * 返回 uint16_t 最大值和目标大小的较小值
     */
    return std::min(size_t(std::numeric_limits<uint16_t>::max()), targetSize);
}

View::FroxelConfigurationInfo Froxelizer::getFroxelConfigurationInfo() const noexcept {
    // this must return the configuration during the last update()
    return mFroxelConfigurationInfo;
}

/**
 * Froxelizer 构造函数
 * 
 * 初始化 Froxelizer，创建内存池和缓冲区。
 * 
 * @param engine 引擎引用
 */
Froxelizer::Froxelizer(FEngine& engine)
    : mArena("froxel", PER_FROXELDATA_ARENA_SIZE),  // 创建内存池
      mZLightNear(FROXEL_FIRST_SLICE_DEPTH_DEFAULT),  // 初始化近平面深度（默认 5m）
      mZLightFar(FROXEL_LAST_SLICE_DISTANCE_DEFAULT),  // 初始化远平面距离（默认 100m）
      mUserZLightNear(FROXEL_FIRST_SLICE_DEPTH_DEFAULT),  // 用户设置的近平面深度
      mUserZLightFar(FROXEL_LAST_SLICE_DISTANCE_DEFAULT) {  // 用户设置的远平面距离

    /**
     * 确保记录缓冲区使用字节类型
     */
    static_assert(std::is_same_v<RecordBufferType, uint8_t>,
            "Record Buffer must use bytes");

    DriverApi& driverApi = engine.getDriverApi();

    /**
     * 如果功能级别为 0，不初始化（不支持）
     */
    if (UTILS_UNLIKELY(driverApi.getFeatureLevel() == FeatureLevel::FEATURE_LEVEL_0)) {
        return;
    }

    /**
     * 计算 Froxel 缓冲区大小和条目数
     */
    size_t const froxelBufferByteCount = getFroxelBufferByteCount(driverApi);
    mFroxelBufferEntryCount = froxelBufferByteCount / sizeof(FroxelEntry);
    /**
     * 确保是 16 的倍数（有助于向量化）
     */
    mFroxelBufferEntryCount &= ~0xF; // make sure it's a multiple of 16 (helps vectorizing)
    /**
     * 确保至少为 16（其他地方也需要）
     */
    assert_invariant(mFroxelBufferEntryCount >= 16); // that's also needed elsewhere

    /**
     * 计算 Froxel 记录缓冲区大小和条目数
     */
    size_t const froxelRecordBufferByteCount = getFroxelRecordBufferByteCount(driverApi);
    mFroxelRecordBufferEntryCount = froxelRecordBufferByteCount / sizeof(uint8_t);
    /**
     * 确保不超过 uint16_t 最大值
     */
    assert_invariant(mFroxelRecordBufferEntryCount <= std::numeric_limits<uint16_t>::max());

    /**
     * 创建记录缓冲区（统一缓冲区，动态使用）
     */
    mRecordsBuffer = driverApi.createBufferObject(
            froxelRecordBufferByteCount,
            BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);

    /**
     * 创建 Froxel 缓冲区（统一缓冲区，动态使用）
     */
    mFroxelsBuffer = driverApi.createBufferObject(
            froxelBufferByteCount,
            BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);
}

Froxelizer::~Froxelizer() {
    // make sure we called terminate()
}

void Froxelizer::terminate(DriverApi& driverApi) noexcept {
    // call reset() on our LinearAllocator arenas
    mArena.reset();

    mBoundingSpheres = nullptr;
    mPlanesY = nullptr;
    mPlanesX = nullptr;
    mDistancesZ = nullptr;

    if (mRecordsBuffer) {
        driverApi.destroyBufferObject(mRecordsBuffer);
    }
    if (mFroxelsBuffer) {
        driverApi.destroyBufferObject(mFroxelsBuffer);
    }
}

/**
 * 设置选项
 * 
 * 设置光源的近平面和远平面距离。
 * 
 * @param zLightNear 光源近平面距离
 * @param zLightFar 光源远平面距离
 */
void Froxelizer::setOptions(float const zLightNear, float const zLightFar) noexcept {
    /**
     * 如果选项改变，需要重新计算
     */
    if (UTILS_UNLIKELY(mUserZLightNear != zLightNear || mUserZLightFar != zLightFar)) {
        mUserZLightNear = zLightNear;
        mUserZLightFar = zLightFar;
        mDirtyFlags |= OPTIONS_CHANGED;
    }
}

/**
 * 设置视口
 * 
 * 设置 Froxelizer 的视口，如果视口改变则标记为脏。
 * 
 * @param viewport 视口信息
 */
void Froxelizer::setViewport(filament::Viewport const& viewport) noexcept {
    /**
     * 如果视口改变，更新并标记为脏
     */
    if (UTILS_UNLIKELY(mViewport != viewport)) {
        mViewport = viewport;
        mDirtyFlags |= VIEWPORT_CHANGED;
    }
}

/**
 * 设置投影矩阵
 * 
 * 设置 Froxelizer 的投影矩阵和近远平面，如果改变则标记为脏。
 * 
 * @param projection 投影矩阵
 * @param near 近平面距离
 * @param far 远平面距离（未使用）
 */
void Froxelizer::setProjection(const mat4f& projection,
        float const near, UTILS_UNUSED float const far) noexcept {
    /**
     * 如果投影矩阵或近远平面改变，更新并标记为脏
     */
    if (UTILS_UNLIKELY(!fuzzyEqual(mProjection, projection) || mNear != near || mFar != far)) {
        mProjection = projection;
        mNear = near;
        mFar = far;
        mDirtyFlags |= PROJECTION_CHANGED;
    }
}

/**
 * 准备 Froxelizer
 * 
 * 准备 Froxelizer 进行渲染，分配缓冲区并更新状态。
 * 
 * @param driverApi 驱动 API 引用
 * @param rootArenaScope 根内存池作用域
 * @param viewport 视口信息
 * @param projection 投影矩阵
 * @param projectionNear 投影近平面距离
 * @param projectionFar 投影远平面距离
 * @param clipTransform 裁剪空间变换（仅用于调试）
 * @return 如果需要更新 uniform 则返回 true
 */
bool Froxelizer::prepare(
        FEngine::DriverApi& driverApi, RootArenaScope& rootArenaScope,
        filament::Viewport const& viewport,
        const mat4f& projection, float const projectionNear, float const projectionFar,
        float4 const& clipTransform) noexcept {
    /**
     * 验证投影参数
     */
    assert_invariant(projectionFar > projectionNear);
    assert_invariant(projectionNear > 0);
    /**
     * 设置视口和投影
     */
    setViewport(viewport);
    setProjection(projection, projectionNear, projectionFar);

    // Only for debugging
    /**
     * 保存裁剪空间变换（仅用于调试）
     */
    mClipTransform = clipTransform;

    /**
     * 如果需要更新，执行更新
     */
    bool uniformsNeedUpdating = false;
    if (UTILS_UNLIKELY(mDirtyFlags)) {
        uniformsNeedUpdating = update();
    }

    /*
     * Allocations that need to persist until the driver consumes them are done from
     * the command stream.
     */

    // froxel buffer (16 KiB with 4096 froxels)
    /**
     * 分配 Froxel 缓冲区（16 KiB，4096 个 froxel）
     */
    mFroxelBufferUser.set(
            driverApi.allocatePod<FroxelEntry>(mFroxelBufferEntryCount),
            mFroxelBufferEntryCount);

    // record buffer (64 KiB max)
    /**
     * 分配记录缓冲区（最大 64 KiB）
     */
    mRecordBufferUser.set(
            driverApi.allocatePod<RecordBufferType>(mFroxelRecordBufferEntryCount),
            mFroxelRecordBufferEntryCount);

    /*
     * Temporary allocations for processing all froxel data
     */

    // light records per froxel (~256 KiB with 4096 froxels)
    /**
     * 分配每个 froxel 的光源记录（~256 KiB，4096 个 froxel）
     */
    mLightRecords.set(
            rootArenaScope.allocate<LightRecord>(getFroxelBufferEntryCount(), CACHELINE_SIZE),
            getFroxelBufferEntryCount());

    // froxel thread data (~256KiB with 8192 max froxels and 256 lights)
    /**
     * 分配 Froxel 线程数据（~256 KiB，最大 8192 个 froxel 和 256 个光源）
     */
    mFroxelShardedData.set(
            rootArenaScope.allocate<FroxelThreadData>(GROUP_COUNT, CACHELINE_SIZE),
            uint32_t(GROUP_COUNT));

    /**
     * 验证所有缓冲区已分配
     */
    assert_invariant(mFroxelBufferUser.begin());
    assert_invariant(mRecordBufferUser.begin());
    assert_invariant(mLightRecords.begin());
    assert_invariant(mFroxelShardedData.begin());

    // initialize buffers that need to be
    /**
     * 初始化需要初始化的缓冲区（清零光源记录）
     */
    memset(mLightRecords.data(), 0, mLightRecords.sizeInBytes());

    return uniformsNeedUpdating;
}

/**
 * 计算 Froxel 网格分辨率
 * 
 * 根据视口和缓冲区预算计算 Froxel 网格的分辨率。
 * 
 * @param dim 输出：Froxel 维度（像素）
 * @param countX 输出：X 方向 Froxel 数量
 * @param countY 输出：Y 方向 Froxel 数量
 * @param countZ 输出：Z 方向 Froxel 数量（切片数）
 * @param froxelBufferEntryCount Froxel 缓冲区条目数
 * @param viewport 视口信息
 */
// Compute froxel grid resolution based on viewport and buffer budget.
void Froxelizer::computeFroxelLayout(
        uint2* dim, uint16_t* countX, uint16_t* countY, uint16_t* countZ,
        size_t const froxelBufferEntryCount, filament::Viewport const& viewport) noexcept {

    /**
     * 向上舍入到 8 的倍数（提高着色器性能）
     */
    auto roundTo8 = [](uint32_t const v) { return (v + 7u) & ~7u; };

    /**
     * 确保宽度和高度至少为 16
     */
    const uint32_t width  = std::max(16u, viewport.width);
    const uint32_t height = std::max(16u, viewport.height);

    // calculate froxel dimension from FROXEL_BUFFER_ENTRY_COUNT_MAX and viewport
    // - Start from the maximum number of froxels we can use in the x-y plane
    /**
     * 从最大 Froxel 缓冲区条目数和视口计算 Froxel 维度
     * - 从 x-y 平面可用的最大 Froxel 数量开始
     */
    constexpr size_t froxelSliceCount = FROXEL_SLICE_COUNT;
    /**
     * 计算 x-y 平面的 Froxel 数量
     */
    size_t const froxelPlaneCount = froxelBufferEntryCount / froxelSliceCount;
    // - compute the number of square froxels we need in width and height, rounded down
    //   solving: |  froxelCountX * froxelCountY == froxelPlaneCount
    //            |  froxelCountX / froxelCountY == width / height
    /**
     * 计算宽度和高度方向需要的方形 Froxel 数量（向下舍入）
     * 求解：froxelCountX * froxelCountY == froxelPlaneCount
     *      froxelCountX / froxelCountY == width / height
     */
    size_t froxelCountX = size_t(std::sqrt(froxelPlaneCount * width  / height));
    size_t froxelCountY = size_t(std::sqrt(froxelPlaneCount * height / width));
    // - compute the froxels dimensions, rounded up
    /**
     * 计算 Froxel 维度（向上舍入）
     */
    size_t const froxelSizeX = (width  + froxelCountX - 1) / froxelCountX;
    size_t const froxelSizeY = (height + froxelCountY - 1) / froxelCountY;
    // - and since our froxels must be square, only keep the largest dimension

    //  make sure we're at lease multiple of 8 to improve performance in the shader
    /**
     * 由于 Froxel 必须是方形的，只保留最大维度
     * 确保至少是 8 的倍数以提高着色器性能
     */
    size_t const froxelDimension = roundTo8((roundTo8(froxelSizeX) >= froxelSizeY) ? froxelSizeX : froxelSizeY);

    // Here we recompute the froxel counts which may have changed a little due to the rounding
    // and the squareness requirement of froxels
    /**
     * 由于舍入和方形要求，重新计算 Froxel 数量
     */
    froxelCountX = (width  + froxelDimension - 1) / froxelDimension;
    froxelCountY = (height + froxelDimension - 1) / froxelDimension;

    /**
     * 验证计算结果
     */
    assert_invariant(froxelCountX);
    assert_invariant(froxelCountY);
    assert_invariant(froxelCountX * froxelCountY <= froxelPlaneCount);

    /**
     * 输出结果
     */
    *dim = froxelDimension;
    *countX = uint16_t(froxelCountX);
    *countY = uint16_t(froxelCountY);
    *countZ = uint16_t(froxelSliceCount);
}

/**
 * 更新边界球
 * 
 * 计算每个 Froxel 的边界球，用于聚光灯计算。
 * 通过相交 3 个平面找到每个 Froxel 的 8 个角点。
 * 
 * @param boundingSpheres 输出：边界球数组（每个 Froxel 一个）
 * @param froxelCountX X 方向 Froxel 数量
 * @param froxelCountY Y 方向 Froxel 数量
 * @param froxelCountZ Z 方向 Froxel 数量
 * @param planesX X 方向平面数组
 * @param planesY Y 方向平面数组
 * @param planesZ Z 方向平面数组
 */
UTILS_NOINLINE
void Froxelizer::updateBoundingSpheres(
        float4* const UTILS_RESTRICT boundingSpheres,
        size_t froxelCountX, size_t froxelCountY, size_t froxelCountZ,
        float4 const* UTILS_RESTRICT planesX,
        float4 const* UTILS_RESTRICT planesY,
        float const* UTILS_RESTRICT planesZ) noexcept {

    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    // TODO: this could potentially be parallel_for'ized

    /*
     * Now compute the bounding sphere of each froxel, which is needed for spotlights
     * We intersect 3 planes of the frustum to find each 8 corners.
     */

    /**
     * 假设 Froxel 数量大于 0（优化提示）
     */
    UTILS_ASSUME(froxelCountX > 0);
    UTILS_ASSUME(froxelCountY > 0);

    /**
     * 遍历所有 Froxel（Z、Y、X 顺序）
     */
    for (size_t iz = 0, fi = 0, nz = froxelCountZ; iz < nz; ++iz) {
        /**
         * 为当前 Z 切片设置前后平面
         */
        float4 planes[6];
        planes[4] =  float4{ 0, 0, 1, planesZ[iz + 0] };  // 前平面
        planes[5] = -float4{ 0, 0, 1, planesZ[iz + 1] };  // 后平面
        for (size_t iy = 0, ny = froxelCountY; iy < ny; ++iy) {
            /**
             * 为当前 Y 切片设置上下平面
             */
            planes[2] =  planesY[iy];      // 下平面
            planes[3] = -planesY[iy + 1];  // 上平面
            for (size_t ix = 0, nx = froxelCountX; ix < nx; ++ix) {
                /**
                 * 为当前 X 切片设置左右平面
                 */
                planes[0] =  planesX[ix];      // 左平面
                planes[1] = -planesX[ix + 1];  // 右平面

                /**
                 * 计算 Froxel 的 8 个角点（通过相交 3 个平面）
                 */
                float3 const p0 = planeIntersection(planes[0], planes[2], planes[4]);  // 左下前
                float3 const p1 = planeIntersection(planes[1], planes[2], planes[4]);  // 右下前
                float3 const p2 = planeIntersection(planes[0], planes[3], planes[4]);  // 左上前
                float3 const p3 = planeIntersection(planes[1], planes[3], planes[4]);  // 右上前
                float3 const p4 = planeIntersection(planes[0], planes[2], planes[5]);  // 左下后
                float3 const p5 = planeIntersection(planes[1], planes[2], planes[5]);  // 右下后
                float3 const p6 = planeIntersection(planes[0], planes[3], planes[5]);  // 左上后
                float3 const p7 = planeIntersection(planes[1], planes[3], planes[5]);  // 右上后

                /**
                 * 计算中心点（8 个角点的平均值）
                 */
                float3 const c = (p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7) * 0.125f;

                /**
                 * 计算每个角点到中心的距离平方
                 */
                float const d0 = length2(p0 - c);
                float const d1 = length2(p1 - c);
                float const d2 = length2(p2 - c);
                float const d3 = length2(p3 - c);
                float const d4 = length2(p4 - c);
                float const d5 = length2(p5 - c);
                float const d6 = length2(p6 - c);
                float const d7 = length2(p7 - c);

                /**
                 * 计算半径（最大距离）
                 */
                float const r = std::sqrt(std::max({ d0, d1, d2, d3, d4, d5, d6, d7 }));

                /**
                 * 验证索引正确性
                 */
                assert_invariant(getFroxelIndex(ix, iy, iz, froxelCountX, froxelCountY) == fi);
                /**
                 * 保存边界球（中心点和半径）
                 */
                boundingSpheres[fi++] = { c, r };
            }
        }
    }
}

/**
 * 更新 Froxelizer
 * 
 * 当选项/视口/投影改变时重新计算 Froxel 网格，更新切片平面和计数。
 * 
 * @return 如果需要更新 uniform 则返回 true
 */
UTILS_NOINLINE
// Recompute froxel grid when options/view/projection change; updates slice planes and counts.
bool Froxelizer::update() noexcept {
    /**
     * 是否需要更新 uniform
     */
    bool uniformsNeedUpdating = false;

    /**
     * 如果选项或投影改变，清理并更新光源近远平面
     */
    if (UTILS_UNLIKELY(mDirtyFlags & (OPTIONS_CHANGED|PROJECTION_CHANGED))) {

        // sanitize the user's near/far
        /**
         * 清理用户的近远平面值
         */
        float zLightNear = mUserZLightNear;
        float zLightFar = mUserZLightFar;
        /**
         * 如果远平面等于近平面，使用投影的近远平面
         */
        if (zLightFar == zLightNear) {
            zLightNear = mNear;
            zLightFar = mFar;
        }
        /**
         * 如果远平面小于近平面，交换它们
         */
        if (zLightFar < zLightNear) {
            std::swap(zLightFar, zLightNear);
        }
        /**
         * 如果近平面超出投影范围，限制到投影近平面
         */
        if (zLightNear < mNear || zLightNear >= mFar) {
            zLightNear = mNear;
        }
        /**
         * 如果远平面超出投影范围，限制到投影远平面
         */
        if (zLightFar > mFar || zLightFar <= mNear) {
            zLightFar = mFar;
        }

        /**
         * 验证清理后的值
         */
        assert_invariant(zLightNear < zLightFar);
        assert_invariant(zLightNear >= mNear && zLightNear <= mFar);
        assert_invariant(zLightFar <= mFar && zLightNear >= mNear);

        /**
         * 确保近平面不大于远平面
         */
        zLightNear = std::min(zLightNear, zLightFar);
        /**
         * 如果值改变，更新并标记视口为脏
         */
        if (zLightFar != mZLightFar || zLightNear != mZLightNear) {
            mDirtyFlags |= VIEWPORT_CHANGED;
            mZLightNear = zLightNear;
            mZLightFar = zLightFar;
        }
    }

    /**
     * 如果视口改变，重新计算 Froxel 布局和分配内存
     */
    if (UTILS_UNLIKELY(mDirtyFlags & VIEWPORT_CHANGED)) {
        filament::Viewport const& viewport = mViewport;

        /**
         * 计算 Froxel 布局（维度、X/Y/Z 方向数量）
         */
        uint2 froxelDimension;
        uint16_t froxelCountX, froxelCountY, froxelCountZ;
        computeFroxelLayout(&froxelDimension, &froxelCountX, &froxelCountY, &froxelCountZ,
                getFroxelBufferEntryCount(), viewport);

        /**
         * 保存 Froxel 维度
         */
        mFroxelDimension = froxelDimension;
        // note: because froxelDimension is a power-of-two and viewport is an integer, mClipFroxel
        // is an exact value (which is not true for 1/mClipToFroxelX, btw)
        /**
         * 计算裁剪空间到 Froxel 空间的转换因子
         * 注意：由于 froxelDimension 是 2 的幂且 viewport 是整数，mClipFroxel 是精确值
         */
        mClipToFroxelX = float(viewport.width)  / float(2 * froxelDimension.x);
        mClipToFroxelY = float(viewport.height) / float(2 * froxelDimension.y);

        /**
         * 标记需要更新 uniform
         */
        uniformsNeedUpdating = true;

        /**
         * 调试日志：输出 Froxel 配置信息
         */
        DLOG(INFO) << "Froxel: " << viewport.width << "x" << viewport.height << " / "
                   << froxelDimension.x << "x" << froxelDimension.y << io::endl
                   << "Froxel: " << froxelCountX << "x" << froxelCountY << "x" << froxelCountZ
                   << " = " << (froxelCountX * froxelCountY * froxelCountZ) << " ("
                   << getFroxelBufferEntryCount() - froxelCountX * froxelCountY * froxelCountZ
                   << " lost)";

        /**
         * 保存 Froxel 计数
         */
        mFroxelCountX = froxelCountX;
        mFroxelCountY = froxelCountY;
        mFroxelCountZ = froxelCountZ;
        const uint32_t froxelCount = uint32_t(froxelCountX * froxelCountY * froxelCountZ);
        mFroxelCount = froxelCount;

        /**
         * 如果已有 Z 距离数组，回退内存池（LinearAllocator 使用 rewind 而不是 free）
         */
        if (mDistancesZ) {
            // this is a LinearAllocator arena, use rewind() instead of free (which is a no op).
            mArena.rewind(mDistancesZ);
        }

        /**
         * 分配新的内存：Z 距离、X/Y 平面、边界球
         */
        mDistancesZ      = mArena.alloc<float>(froxelCountZ + 1);      // Z 方向距离（+1 用于最后一个边界）
        mPlanesX         = mArena.alloc<float4>(froxelCountX + 1);    // X 方向平面（+1 用于最后一个边界）
        mPlanesY         = mArena.alloc<float4>(froxelCountY + 1);    // Y 方向平面（+1 用于最后一个边界）
        mBoundingSpheres = mArena.alloc<float4>(froxelCount);         // 边界球数组

        /**
         * 验证所有分配成功
         */
        assert_invariant(mDistancesZ);
        assert_invariant(mPlanesX);
        assert_invariant(mPlanesY);
        assert_invariant(mBoundingSpheres);

        /**
         * 初始化第一个 Z 距离为 0
         */
        mDistancesZ[0] = 0.0f;
        const float zLightNear = mZLightNear;
        const float zLightFar = mZLightFar;
        /**
         * 计算线性化因子（用于对数分布）
         * 使用对数分布以在近处提供更高的分辨率
         */
        const float linearizer = std::log2(zLightFar / zLightNear) / float(std::max(1u, mFroxelCountZ - 1u));
        // for a strange reason when, vectorizing this loop, clang does some math in double
        // and generates conversions to float. not worth it for so little iterations.
        /**
         * 计算每个 Z 切片的距离（对数分布）
         * 注意：禁用向量化以避免 clang 进行不必要的 double 计算
         */
#if defined(__clang__)
        #pragma clang loop vectorize(disable) unroll(disable)
#endif
        for (ssize_t i = 1, n = mFroxelCountZ; i <= n; i++) {
            /**
             * 使用指数函数计算距离：z = zFar * 2^(linearizer * (i - n))
             */
            mDistancesZ[i] = zLightFar * std::exp2(float(i - n) * linearizer);
        }

        // for the inverse-transformation (view-space z to z-slice)
        /**
         * 保存线性化因子和其倒数（用于从视图空间 Z 到 Z 切片的逆变换）
         */
        mLinearizer = { linearizer, 1.0f / linearizer };

        /**
         * 初始化 Z 参数（在相机改变时更新）
         */
        mParamsZ[0] = 0; // updated when camera changes
        mParamsZ[1] = 0; // updated when camera changes
        mParamsZ[2] = 0; // updated when camera changes
        mParamsZ[3] = mFroxelCountZ;  // Z 方向 Froxel 数量
        /**
         * 初始化 Froxel 索引参数
         */
        mParamsF[0] = 1;  // 步长
        mParamsF[1] = uint32_t(mFroxelCountX);  // X 方向 Froxel 数量
        mParamsF[2] = uint32_t(mFroxelCountX * mFroxelCountY);  // X*Y 方向 Froxel 数量（用于索引计算）
    }

    /**
     * 如果投影或视口改变，重新计算平面和 Z 参数
     */
    if (UTILS_UNLIKELY(mDirtyFlags & (PROJECTION_CHANGED | VIEWPORT_CHANGED))) {
        /**
         * 验证所有数组已分配
         */
        assert_invariant(mDistancesZ);
        assert_invariant(mPlanesX);
        assert_invariant(mPlanesY);
        assert_invariant(mBoundingSpheres);

        // clip-space dimensions
        /**
         * 计算裁剪空间中的 Froxel 尺寸
         */
        const float froxelWidthInClipSpace  = float(2 * mFroxelDimension.x) / float(mViewport.width);
        const float froxelHeightInClipSpace = float(2 * mFroxelDimension.y) / float(mViewport.height);
        float4 * const UTILS_RESTRICT planesX = mPlanesX;
        float4 * const UTILS_RESTRICT planesY = mPlanesY;

        // Planes are transformed by the inverse-transpose of the transform matrix.
        // So to transform a plane in clip-space to view-space, we need to apply
        // the transpose(inverse(viewFromClipMatrix)), i.e.: transpose(projection)
        /**
         * 计算投影矩阵的转置（用于将裁剪空间平面变换到视图空间）
         * 平面通过变换矩阵的逆转置进行变换
         * 因此要将裁剪空间平面变换到视图空间，需要应用 transpose(inverse(viewFromClipMatrix))，即 transpose(projection)
         */
        const mat4f trProjection(transpose(mProjection));

        // generate the horizontal planes from their clip-space equation
        /**
         * 从裁剪空间方程生成水平平面（X 方向）
         */
        for (size_t i = 0, n = mFroxelCountX; i <= n; ++i) {
            /**
             * 计算裁剪空间 X 坐标（-1 到 1）
             */
            float const x = (float(i) * froxelWidthInClipSpace) - 1.0f;
            /**
             * 变换平面到视图空间
             */
            float4 const p = trProjection * float4{ -1, 0, 0, x };
            /**
             * 归一化平面法向量（p.w 保证为 0）
             */
            planesX[i] = float4{ normalize(p.xyz), 0 };  // p.w is guaranteed to be 0
        }

        // generate the vertical planes from their clip-space equation
        /**
         * 从裁剪空间方程生成垂直平面（Y 方向）
         */
        for (size_t i = 0, n = mFroxelCountY; i <= n; ++i) {
            /**
             * 计算裁剪空间 Y 坐标（-1 到 1）
             */
            float const y = (float(i) * froxelHeightInClipSpace) - 1.0f;
            /**
             * 变换平面到视图空间
             */
            float4 const p = trProjection * float4{ 0, 1, 0, -y };
            /**
             * 归一化平面法向量（p.w 保证为 0）
             */
            planesY[i] = float4{ normalize(p.xyz), 0 };  // p.w is guaranteed to be 0
        }

        /**
         * 更新所有 Froxel 的边界球
         */
        updateBoundingSpheres(mBoundingSpheres,
                mFroxelCountX, mFroxelCountY, mFroxelCountZ,
                planesX, planesY, mDistancesZ);

        // note: none of the values below are affected by the projection offset, scale or rotation.
        /**
         * 计算 Z 参数（用于从屏幕空间 Z 到视图空间 Z 的转换）
         * 注意：以下值不受投影偏移、缩放或旋转影响
         */
        float const Pz = mProjection[2][2];  // 投影矩阵 Z 缩放
        float const Pw = mProjection[3][2];  // 投影矩阵 Z 偏移
        /**
         * 判断是透视投影还是正交投影
         */
        if (mProjection[2][3] != 0) {
            // With our inverted DX convention, we have the simple relation:
            // z_view = -near / z_screen
            // ==> i = log2(-z / far) / linearizer + zcount
            // ==> i = -log2(z_screen * (far/near)) * (1/linearizer) + zcount
            // ==> i = log2(z_screen * (far/near)) * (-1/linearizer) + zcount
            /**
             * 透视投影：使用倒置 DX 约定
             * z_view = -near / z_screen
             * 推导出切片索引公式
             */
            mParamsZ[0] = mZLightFar / Pw;      // 缩放因子
            mParamsZ[1] = 0.0f;                  // 偏移（透视投影为 0）
            mParamsZ[2] = -mLinearizer[1];       // 线性化因子（负值）
        } else {
            // orthographic projection
            // z_view = (1 - z_screen) * (near - far) - near
            // z_view = z_screen * (far - near) - far
            // our ortho matrix is in inverted-DX convention
            //   Pz =   1 / (far - near)
            //   Pw = far / (far - near)
            /**
             * 正交投影：使用倒置 DX 约定
             * z_view = z_screen * (far - near) - far
             * 投影矩阵：Pz = 1 / (far - near), Pw = far / (far - near)
             */
            mParamsZ[0] = -1.0f / (Pz * mZLightFar);  // -(far-near) / mZLightFar
            mParamsZ[1] =    Pw / (Pz * mZLightFar);  //         far / mZLightFar
            mParamsZ[2] = mLinearizer[1];            // 线性化因子（正值）
        }
        /**
         * 标记需要更新 uniform
         */
        uniformsNeedUpdating = true;
    }
    assert_invariant(mZLightNear >= mNear);

    if (UTILS_UNLIKELY(mDirtyFlags)) {
        mFroxelConfigurationInfo = {
            mFroxelCountX, mFroxelCountY, mFroxelCountZ,
            mViewport.width, mViewport.height,
            mFroxelDimension,
            mZLightFar,
            mLinearizer[0],
            mProjection,
            mClipTransform
        };
    }

    mDirtyFlags = 0;
    return uniformsNeedUpdating;
}

/**
 * 获取指定位置的 Froxel
 * 
 * 返回指定 (x, y, z) 位置的 Froxel，包含其 6 个平面。
 * 
 * @param x X 方向索引
 * @param y Y 方向索引
 * @param z Z 方向索引
 * @return Froxel 对象
 */
Froxel Froxelizer::getFroxelAt(size_t const x, size_t const y, size_t const z) const noexcept {
    /**
     * 验证索引在有效范围内
     */
    assert_invariant(x < mFroxelCountX);
    assert_invariant(y < mFroxelCountY);
    assert_invariant(z < mFroxelCountZ);
    /**
     * 创建 Froxel 并设置其 6 个平面
     */
    Froxel froxel;
    froxel.planes[Froxel::LEFT]   =  mPlanesX[x];                    // 左平面
    froxel.planes[Froxel::BOTTOM] =  mPlanesY[y];                    // 下平面
    froxel.planes[Froxel::NEAR]   =  float4{ 0, 0, 1, mDistancesZ[z] };  // 近平面（Z 方向）
    froxel.planes[Froxel::RIGHT]  = -mPlanesX[x + 1];               // 右平面（取反）
    froxel.planes[Froxel::TOP]    = -mPlanesY[y + 1];               // 上平面（取反）
    froxel.planes[Froxel::FAR]    = -float4{ 0, 0, 1, mDistancesZ[z+1] };  // 远平面（Z 方向，取反）
    return froxel;
}

/**
 * 查找 Z 切片索引
 * 
 * 根据视图空间 Z 坐标查找对应的 Z 切片索引。
 * 使用对数分布，在近处提供更高的分辨率。
 * 
 * @param viewSpaceZ 视图空间 Z 坐标
 * @return Z 切片索引
 */
UTILS_NOINLINE
size_t Froxelizer::findSliceZ(float const viewSpaceZ) const noexcept {
    // The vastly common case is that z<0, so we always do the math for this case
    // and we "undo" it below otherwise. This works because we're using fast::log2 which
    // doesn't care if given a negative number (we'd have to use abs() otherwise).

    // This whole function is now branch-less.

    /**
     * 计算切片索引（假设 z < 0，这是最常见的情况）
     * 使用 fast::log2 可以处理负数，无需 abs()
     * 公式：i = log2(-z / far) * (1/linearizer) + zcount
     */
    int s = int( fast::log2(-viewSpaceZ / mZLightFar) * mLinearizer[1] + float(mFroxelCountZ) );

    // there are cases where z can be negative here, e.g.:
    // - the light is visible, but its center is behind the camera
    // - the camera's near is behind the camera (e.g. with shadowmap cameras)
    // in that case just return the first slice
    /**
     * 如果 z >= 0（在相机后面），返回第一个切片
     * 这种情况可能发生在：
     * - 光源可见但其中心在相机后面
     * - 相机的近平面在相机后面（例如阴影贴图相机）
     */
    s = viewSpaceZ < 0 ? s : 0;

    // clamp between [0, mFroxelCountZ)
    /**
     * 限制在有效范围内 [0, mFroxelCountZ)
     */
    return size_t(clamp(s, 0, mFroxelCountZ - 1));
}

/**
 * 裁剪坐标转换为索引
 * 
 * 将裁剪空间坐标（-1 到 1）转换为 Froxel 索引（0 到 count-1）。
 * 
 * @param clip 裁剪空间坐标 (x, y)
 * @return (X 索引, Y 索引) 对
 */
std::pair<size_t, size_t> Froxelizer::clipToIndices(float2 const& clip) const noexcept {
    // clip coordinates between [-1, 1], conversion to index between [0, count[
    // (clip + 1) * 0.5 * dimension / froxelsize
    // clip * 0.5 * dimension / froxelsize + 0.5 * dimension / froxelsize
    /**
     * 转换 X 坐标：clip.x * mClipToFroxelX + mClipToFroxelX
     * 等价于 (clip.x + 1) * 0.5 * dimension / froxelsize
     */
    const size_t xi = size_t(clamp(int(clip.x * mClipToFroxelX + mClipToFroxelX), 0, mFroxelCountX - 1));
    /**
     * 转换 Y 坐标：clip.y * mClipToFroxelY + mClipToFroxelY
     */
    const size_t yi = size_t(clamp(int(clip.y * mClipToFroxelY + mClipToFroxelY), 0, mFroxelCountY - 1));
    return { xi, yi };
}


/**
 * 提交数据到 GPU
 * 
 * 将 Froxel 缓冲区和记录缓冲区数据上传到 GPU。
 * 
 * @param driverApi 驱动 API 引用
 */
void Froxelizer::commit(DriverApi& driverApi) {
    // send data to GPU
    /**
     * 更新 Froxel 缓冲区（包含每个 Froxel 的光源列表信息）
     */
    driverApi.updateBufferObject(mFroxelsBuffer,
            { mFroxelBufferUser.data(), mFroxelBufferEntryCount * sizeof(FroxelEntry) }, 0);

    /**
     * 更新记录缓冲区（包含光源索引列表）
     */
    driverApi.updateBufferObject(mRecordsBuffer,
            { mRecordBufferUser.data(), mFroxelRecordBufferEntryCount }, 0);

#ifndef NDEBUG
    /**
     * 调试模式下清除缓冲区（验证数据已提交）
     */
    mFroxelBufferUser.clear();
    mRecordBufferUser.clear();
    mFroxelShardedData.clear();
#endif
}

/**
 * Froxelize 光源
 * 
 * 将光源分配到相应的 Froxel 中。
 * 注意：此函数异步调用。
 * 
 * @param engine 引擎引用
 * @param viewMatrix 视图矩阵
 * @param lightData 光源数据（SOA 格式）
 */
void Froxelizer::froxelizeLights(FEngine& engine,
        mat4f const& UTILS_RESTRICT viewMatrix,
        const FScene::LightSoa& UTILS_RESTRICT lightData) noexcept {
    // note: this is called asynchronously
    /**
     * 执行 Froxelize 循环（将光源分配到 Froxel）
     */
    froxelizeLoop(engine, viewMatrix, lightData);
    /**
     * 分配记录并压缩（构建 GPU 缓冲区）
     */
    froxelizeAssignRecordsCompress();

#ifndef NDEBUG
    /**
     * 调试模式：验证所有光源索引有效
     */
    if (lightData.size()) {
        // go through every froxel
        auto const& recordBufferUser(mRecordBufferUser);
        auto gpuFroxelEntries(mFroxelBufferUser);
        gpuFroxelEntries.set(gpuFroxelEntries.begin(),
                mFroxelCountX * mFroxelCountY * mFroxelCountZ);
        /**
         * 遍历每个 Froxel
         */
        for (auto const& entry : gpuFroxelEntries) {
            // go through every light for that froxel
            /**
             * 遍历该 Froxel 的每个光源
             */
            for (size_t i = 0; i < entry.count(); i++) {
                // get the light index
                /**
                 * 验证索引在有效范围内
                 */
                assert_invariant(entry.offset() + i < mFroxelRecordBufferEntryCount);

                /**
                 * 获取光源索引
                 */
                size_t const lightIndex = recordBufferUser[entry.offset() + i];
                /**
                 * 验证索引不超过最大光源索引
                 */
                assert_invariant(lightIndex <= CONFIG_MAX_LIGHT_INDEX);

                // make sure it corresponds to an existing light
                /**
                 * 确保索引对应一个存在的光源（排除方向光）
                 */
                assert_invariant(lightIndex < lightData.size() - FScene::DIRECTIONAL_LIGHTS_COUNT);
            }
        }
    }
#endif
}

void Froxelizer::froxelizeLoop(FEngine& engine,
        const mat4f& UTILS_RESTRICT viewMatrix,
        const FScene::LightSoa& UTILS_RESTRICT lightData) const noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    Slice<FroxelThreadData> froxelThreadData = mFroxelShardedData;
    memset(froxelThreadData.data(), 0, froxelThreadData.sizeInBytes());

    auto& lcm = engine.getLightManager();
    auto const* UTILS_RESTRICT spheres      = lightData.data<FScene::POSITION_RADIUS>();
    auto const* UTILS_RESTRICT directions   = lightData.data<FScene::DIRECTION>();
    auto const* UTILS_RESTRICT instances    = lightData.data<FScene::LIGHT_INSTANCE>();

    auto process = [ this, &froxelThreadData,
                     spheres, directions, instances, &viewMatrix, &lcm ]
            (size_t const count, size_t const offset, size_t const stride) {

        FILAMENT_TRACING_NAME(FILAMENT_TRACING_CATEGORY_FILAMENT, "FroxelizeLoop Job");

        const mat4f& projection = mProjection;
        const mat3f& vn = viewMatrix.upperLeft();

        // We use minimum cone angle of 0.5 degrees because too small angles cause issues in the
        // sphere/cone intersection test, due to floating-point precision.
        constexpr float maxInvSin = 114.59301f;         // 1 / sin(0.5 degrees)
        constexpr float maxCosSquared = 0.99992385f;    // cos(0.5 degrees)^2

        for (size_t i = offset; i < count; i += stride) {
            const size_t j = i + FScene::DIRECTIONAL_LIGHTS_COUNT;
            FLightManager::Instance const li = instances[j];
            LightParams light = {
                    .position = (viewMatrix * float4{ spheres[j].xyz, 1 }).xyz,     // to view-space
                    .cosSqr = std::min(maxCosSquared, lcm.getCosOuterSquared(li)),  // spot only
                    .axis = vn * directions[j],                                     // spot only
                    .invSin = lcm.getSinInverse(li),                                // spot only
                    .radius = spheres[j].w,
            };
            // infinity means "point-light"
            if (light.invSin != std::numeric_limits<float>::infinity()) {
                light.invSin = std::min(maxInvSin, light.invSin);
            }

            const size_t group = i % GROUP_COUNT;
            const size_t bit   = i / GROUP_COUNT;
            assert_invariant(bit < LIGHT_PER_GROUP);

            FroxelThreadData& threadData = froxelThreadData[group];
            froxelizePointAndSpotLight(threadData, bit, projection, light);
        }
    };

    // we do 64 lights per job
    JobSystem& js = engine.getJobSystem();

    constexpr bool SINGLE_THREADED = false;
    if constexpr (!SINGLE_THREADED) {
        auto *parent = js.createJob();
        for (size_t i = 0; i < GROUP_COUNT; i++) {
            js.run(jobs::createJob(js, parent, std::cref(process),
                    lightData.size() - FScene::DIRECTIONAL_LIGHTS_COUNT, i, GROUP_COUNT));
        }
        js.runAndWait(parent);
    } else {
        js.runAndWait(jobs::createJob(js, nullptr, std::cref(process),
                lightData.size() - FScene::DIRECTIONAL_LIGHTS_COUNT, 0, 1)
        );
    }
}

void Froxelizer::froxelizeAssignRecordsCompress() noexcept {
    FILAMENT_TRACING_CALL(FILAMENT_TRACING_CATEGORY_FILAMENT);

    Slice<const FroxelThreadData> froxelThreadData = mFroxelShardedData;

    // Convert froxel data from N groups of M bits to LightRecord::bitset, so we can
    // easily compare adjacent froxels, for compaction. The conversion loops below get
    // inlined and vectorized in release builds.

    // this gets very well vectorized...

    Slice records(mLightRecords);
    for (size_t j = 0, jc = getFroxelBufferEntryCount() ; j < jc; j++) {
        using container_type = LightRecord::bitset::container_type;
        constexpr size_t r = sizeof(container_type) / sizeof(LightGroupType);
        UTILS_UNROLL
        for (size_t i = 0; i < LightRecord::bitset::WORLD_COUNT; i++) {
            container_type b = froxelThreadData[i * r][j];
            UTILS_UNROLL
            for (size_t k = 0; k < r; k++) {
                b |= (container_type(froxelThreadData[i * r + k][j]) << (LIGHT_PER_GROUP * k));
            }
            records[j].lights.getBitsAt(i) = b;
        }
    }

    LightRecord::bitset allLights{};
    for (size_t j = 0, jc = getFroxelBufferEntryCount(); j < jc; j++) {
        allLights |= records[j].lights;
    }

    uint16_t offset = 0;
    FroxelEntry* const UTILS_RESTRICT froxels = mFroxelBufferUser.data();

    const size_t froxelCountX = mFroxelCountX;
    RecordBufferType* const UTILS_RESTRICT froxelRecords = mRecordBufferUser.data();

    // Initialize the first record with all lights in the scene -- this will be used only if
    // we run out of record space.

    // Our light count cannot be larger than 255 because it's stored in a uint8_t. This should
    // be guaranteed by CONFIG_MAX_LIGHT_COUNT
    assert_invariant(allLights.count() <= std::numeric_limits<uint8_t>::max());

    const uint8_t allLightsCount = allLights.count();
    offset += allLightsCount;
    allLights.forEachSetBit([p = froxelRecords](size_t l) mutable {
        // make sure to keep this code branch-less
        const size_t word = l / LIGHT_PER_GROUP;
        const size_t bit  = l % LIGHT_PER_GROUP;
        l = (bit * GROUP_COUNT) | (word % GROUP_COUNT);
        *p++ = RecordBufferType(l);
    });

    // how many froxel record entries were reused (for debugging)
    UTILS_UNUSED size_t reused = 0;

    for (size_t i = 0, c = mFroxelCount; i < c;) {
        LightRecord b = records[i];
        if (b.lights.none()) {
            froxels[i++].u32 = 0;
            continue;
        }

        // We have a limitation of 255 spot + 255 point lights per froxel.
        assert_invariant(b.lights.count() <= std::numeric_limits<uint8_t>::max());

        // note: initializer list for union cannot have more than one element
        FroxelEntry entry{ offset, uint8_t(b.lights.count()) };
        const size_t lightCount = entry.count();

        if (UTILS_UNLIKELY(offset + lightCount >= mFroxelRecordBufferEntryCount)) {
            // DLOG(INFO) << "out of space: " << i << ", at " << offset;
            // note: instead of dropping froxels we could look for similar records we've already
            // filed up.
            do {
                froxels[i] = { 0u, allLightsCount };
                if (records[i].lights.none()) {
                    froxels[i].u32 = 0;
                }
            } while(++i < c);
            goto out_of_memory;
        }

        // iterate the bitfield
        auto * const beginPoint = froxelRecords + offset;
        b.lights.forEachSetBit([p = beginPoint](size_t l) mutable {
            // make sure to keep this code branch-less
            const size_t word = l / LIGHT_PER_GROUP;
            const size_t bit  = l % LIGHT_PER_GROUP;
            l = (bit * GROUP_COUNT) | (word % GROUP_COUNT);
            *p++ = RecordBufferType(l);
        });

        offset += lightCount;

#ifndef NDEBUG
        if (lightCount) { reused--; }
#endif
        do {
#ifndef NDEBUG
            if (lightCount) { reused++; }
#endif
            froxels[i++].u32 = entry.u32;
            if (i >= c) break;

            if (records[i].lights != b.lights && i >= froxelCountX) {
                // if this froxel record doesn't match the previous one on its left,
                // we re-try with the record above it, which saves many froxel records
                // (north of 10% in practice).
                b = records[i - froxelCountX];
                entry.u32 = froxels[i - froxelCountX].u32;
            }
        } while(records[i].lights == b.lights);
    }
out_of_memory:
    // FIXME: on big-endian systems we need to change the endianness of the record buffer
    ;
}

static float2 project(mat4f const& p, float3 const& v) noexcept {
    const float vx = v[0];
    const float vy = v[1];
    const float vz = v[2];
    const float x = p[0].x * vx + p[1].x * vy + p[2].x * vz + p[3].x;
    const float y = p[0].y * vx + p[1].y * vy + p[2].y * vz + p[3].y;
    const float w = p[0].w * vx + p[1].w * vy + p[2].w * vz + p[3].w;
    return float2{ x, y } * (1.0f / w);
}

void Froxelizer::froxelizePointAndSpotLight(
        FroxelThreadData& froxelThread, size_t bit,
        mat4f const& UTILS_RESTRICT projection,
        const LightParams& UTILS_RESTRICT light) const noexcept {

    if (UTILS_UNLIKELY(light.position.z + light.radius < -mZLightFar)) { // z values are negative
        // This light is fully behind LightFar, it doesn't light anything
        // (we could avoid this check if we culled lights using LightFar instead of the
        // culling camera's far plane)
        return;
    }

    // the code below works with radius^2
    const float4 s = { light.position, light.radius * light.radius };

#ifdef DEBUG_FROXEL
    const size_t x0 = 0;
    const size_t x1 = mFroxelCountX - 1;
    const size_t y0 = 0;
    const size_t y1 = mFroxelCountY - 1;
    const size_t z0 = 0;
    const size_t z1 = mFroxelCountZ - 1;
#else
    // find a reasonable bounding-box in froxel space for the sphere by projecting
    // its (clipped) bounding-box to clip-space and converting to froxel indices.
    Box const aabb = { light.position, light.radius };
    const float znear = std::min(-mNear, aabb.center.z + aabb.halfExtent.z); // z values are negative
    const float zfar  =                  aabb.center.z - aabb.halfExtent.z;

    // TODO: we need to investigate if doing all this actually saves time
    //       e.g.: we could only do the z-min/max which is much easier to compute.

    const float2 pts[8] = {
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{ 1, 1 }, znear }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{ 1,-1 }, znear }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{-1, 1 }, znear }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{-1,-1 }, znear }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{ 1, 1 }, zfar  }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{ 1,-1 }, zfar  }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{-1, 1 }, zfar  }),
        project(projection, { aabb.center.xy + aabb.halfExtent.xy * float2{-1,-1 }, zfar  }),
    };

    float2 pmin = std::numeric_limits<float>::max();
    float2 pmax = 0;
    for (auto pt: pts) {
        pmin = min(pmin, pt);
        pmax = max(pmax, pt);
    }

    const auto [x0, y0] = clipToIndices(pmin);
    const size_t z0 = findSliceZ(znear);

    const auto [x1, y1] = clipToIndices(pmax);
    const size_t z1 = findSliceZ(zfar);

    assert_invariant(x0 <= x1);
    assert_invariant(y0 <= y1);
    assert_invariant(z0 <= z1);

#endif

    const size_t zcenter = findSliceZ(s.z);
    float4 const * const UTILS_RESTRICT planesX = mPlanesX;
    float4 const * const UTILS_RESTRICT planesY = mPlanesY;
    float const * const UTILS_RESTRICT planesZ = mDistancesZ;
    float4 const * const UTILS_RESTRICT boundingSpheres = mBoundingSpheres;
    for (size_t iz = z0 ; iz <= z1; ++iz) {
        float4 cz(s);
        // froxel that contain the center of the sphere is special, we don't even need to do the
        // intersection check, it's always true.
        if (UTILS_LIKELY(iz != zcenter)) {
            cz = spherePlaneIntersection(s, (iz < zcenter) ? planesZ[iz + 1] : planesZ[iz]);
        }

        if (cz.w > 0) { // intersection of light with this plane (slice)
            // the sphere (light) intersects this slice's plane, and we now have a new smaller
            // sphere centered there. Now, find x & y slices that contain the sphere's center
            // (note: this changes with the Z slices)
            const float2 clip = project(projection, cz.xyz);
            auto const [xcenter, ycenter] = clipToIndices(clip);

            for (size_t iy = y0; iy <= y1; ++iy) {
                float4 cy(cz);
                // froxel that contain the center of the sphere is special, we don't even need to
                // do the intersection check, it's always true.
                if (UTILS_LIKELY(iy != ycenter)) {
                    float4 const& plane = iy < ycenter ? planesY[iy + 1] : planesY[iy];
                    cy = spherePlaneIntersection(cz, plane);
                }

                if (cy.w > 0) {
                    // The reduced sphere from the previous stage intersects this horizontal plane,
                    // and we now have new smaller sphere centered on these two previous planes
                    size_t bx = std::numeric_limits<size_t>::max(); // horizontal begin index
                    size_t ex = 0; // horizontal end index

                    // find the "begin" index (left side)
                    for (size_t ix = x0; ix < x1 + 1; ++ix) {
                        // The froxel that contains the center of the sphere is special,
                        // we don't even need to do the intersection check, it's always true.
                        if (UTILS_LIKELY(ix != xcenter)) {
                            float4 const& plane = ix < xcenter ? planesX[ix + 1] : planesX[ix];
                            if (spherePlaneIntersection(cy, plane).w > 0) {
                                // The reduced sphere from the previous stage intersects this
                                // vertical plane, we record the min/max froxel indices
                                bx = std::min(bx, ix);
                                ex = std::max(ex, ix);
                            }
                        } else {
                            // this is the froxel containing the center of the sphere, it is
                            // definitely participating
                            bx = std::min(bx, ix);
                            ex = std::max(ex, ix);
                        }
                    }

                    if (UTILS_UNLIKELY(bx > ex)) {
                        continue;
                    }

                    // the loops below assume 1-past the end for the right side of the range
                    ex++;
                    assert_invariant(bx <= mFroxelCountX && ex <= mFroxelCountX);

                    size_t fi = getFroxelIndex(bx, iy, iz);
                    if (light.invSin != std::numeric_limits<float>::infinity()) {
                        // This is a spotlight (common case)
                        // this loops gets vectorized (on arm64) w/ clang
                        while (bx++ != ex) {
                            // see if this froxel intersects the cone
                            bool const intersect = sphereConeIntersectionFast(boundingSpheres[fi],
                                    light.position, light.axis, light.invSin, light.cosSqr);
                            froxelThread[fi++] |= LightGroupType(intersect) << bit;
                        }
                    } else {
                        // this loops gets vectorized (on arm64) w/ clang
                        while (bx++ != ex) {
                            froxelThread[fi++] |= LightGroupType(1) << bit;
                        }
                    }
                }
            }
        }
    }
}

/*
 *
 * lightTree            output the light tree structure there (must be large enough to hold a complete tree)
 * lightList            list if lights
 * lightData            scene's light data SoA
 * lightRecordsOffset   offset in the record buffer where to find the light list
 */
void Froxelizer::computeLightTree(
        LightTreeNode* lightTree,
        Slice<const RecordBufferType> lightList,
        const FScene::LightSoa& lightData,
        size_t lightRecordsOffset) noexcept {

    // number of lights in this record
    const size_t count = lightList.size();

    // the width of the tree is the next power-of-two (if not already a power of two)
    const size_t w = 1u << (log2i(count) + (popcount(count) == 1 ? 0 : 1));

    // height of the tree
    const size_t h = log2i(w) + 1u;

    auto const* UTILS_RESTRICT zrange = lightData.data<FScene::SCREEN_SPACE_Z_RANGE>() + 1;
    BinaryTreeArray::traverse(h,
            [lightTree, lightRecordsOffset, zrange, indices = lightList.data(), count]
            (size_t const index, size_t const col, size_t const next) {
                // indices[] cannot be accessed past 'col'
                const float min = (col < count) ? zrange[indices[col]].x : 1.0f;
                const float max = (col < count) ? zrange[indices[col]].y : 0.0f;
                lightTree[index] = {
                        .min = min,
                        .max = max,
                        .next = uint16_t(next),
                        .offset = uint16_t(lightRecordsOffset + col),
                        .isLeaf = 1,
                        .count = 1,
                        .reserved = 0,
                };
            },
            [lightTree](size_t const index, size_t const l, size_t const r, size_t const next) {
                lightTree[index] = {
                        .min = std::min(lightTree[l].min, lightTree[r].min),
                        .max = std::max(lightTree[l].max, lightTree[r].max),
                        .next = uint16_t(next),
                        .offset = 0,
                        .isLeaf = 0,
                        .count = 0,
                        .reserved = 0,
                };
            });
}

} // namespace filament
