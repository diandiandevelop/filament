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

#ifndef TNT_FILAMENT_RENDERPASS_H
#define TNT_FILAMENT_RENDERPASS_H

#include "Allocators.h"

#include "SharedHandle.h"

#include "details/Camera.h"
#include "details/Scene.h"

#include "private/filament/Variant.h"
#include "private/filament/EngineEnums.h"

#include <backend/DriverApiForward.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>

#include <utils/Allocator.h>
#include <utils/BitmaskEnum.h>
#include <utils/FixedCapacityVector.h>
#include <utils/Range.h>
#include <utils/Slice.h>
#include <utils/architecture.h>
#include <utils/debug.h>

#include <math/mathfwd.h>

#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <tuple>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace filament {

namespace backend {
class CommandBufferQueue;
}

class FMaterialInstance;
class FRenderPrimitive;
class RenderPassBuilder;
class ColorPassDescriptorSet;

class RenderPass {
public:
    /*
     *   Command key encoding
     *   --------------------
     *
     *   CCC   = Channel
     *   PP    = Pass
     *   a     = alpha masking
     *   ppp   = priority
     *   t     = two-pass transparency ordering
     *   0     = reserved, must be zero
     *
     *
     * TODO: we need to add a "primitive id" in the low-bits of material-id, so that
     *       auto-instancing can work better
     *
     *   DEPTH command (b00)
     *   |  3|1| 2| 2| 2|1| 3 | 2|  6   |   10     |               32               |
     *   +---+-+--+--+--+-+---+--+------+----------+--------------------------------+
     *   |CCC|0|00|01|00|0|ppp|00|000000| Z-bucket |          material-id           |
     *   +---+-+--+--+--+-+---+--+------+----------+--------------------------------+
     *   | correctness        |      optimizations (truncation allowed)             |
     *
     *
     *   COLOR (b01) and REFRACT (b10) commands
     *   |  3|1| 2| 2| 2|1| 3 | 2|  6   |   10     |               32               |
     *   +---+-+--+--+--+-+---+--+------+----------+--------------------------------+
     *   |CCC|0|01|01|00|a|ppp|00|000000| Z-bucket |          material-id           |
     *   |CCC|0|10|01|00|a|ppp|00|000000| Z-bucket |          material-id           | refraction
     *   +---+-+--+--+--+-+---+--+------+----------+--------------------------------+
     *   | correctness        |      optimizations (truncation allowed)             |
     *
     *
     *   BLENDED command (b11)
     *   |  3|1| 2| 2| 2|1| 3 | 2|              32                |         15    |1|
     *   +---+-+--+--+--+-+---+--+--------------------------------+---------------+-+
     *   |CCC|0|11|01|00|0|ppp|00|         ~distanceBits          |   blendOrder  |t|
     *   +---+-+--+--+--+-+---+--+--------------------------------+---------------+-+
     *   | correctness                                                              |
     *
     *
     *   CUSTOM command (prologue)
     *   |  3|1| 2| 2| 2|         22           |               32               |
     *   +---+-+--+--+--+----------------------+--------------------------------+
     *   |CCC|0|PP|00|00|        order         |      custom command index      |
     *   +---+-+--+--+--+----------------------+--------------------------------+
     *   | correctness                                                          |
     *
     *
     *   CUSTOM command (epilogue)
     *   |  3|1| 2| 2| 2|         22           |               32               |
     *   +---+-+--+--+--+----------------------+--------------------------------+
     *   |CCC|0|PP|10|00|        order         |      custom command index      |
     *   +---+-+--+--+--+----------------------+--------------------------------+
     *   | correctness                                                          |
     *
     *
     *   SENTINEL command
     *   |                                   64                                  |
     *   +--------.--------.--------.--------.--------.--------.--------.--------+
     *   |11111111 11111111 11111111 11111111 11111111 11111111 11111111 11111111|
     *   +-----------------------------------------------------------------------+
     */
    using CommandKey = uint64_t;

    static constexpr uint64_t CHANNEL_COUNT                 = CONFIG_RENDERPASS_CHANNEL_COUNT;

    static constexpr uint64_t BLEND_ORDER_MASK              = 0xFFFEllu;
    static constexpr unsigned BLEND_ORDER_SHIFT             = 1;

    static constexpr uint64_t BLEND_TWO_PASS_MASK           = 0x1llu;
    static constexpr unsigned BLEND_TWO_PASS_SHIFT          = 0;

    static constexpr uint64_t MATERIAL_INSTANCE_ID_MASK     = 0x00000FFFllu;
    static constexpr unsigned MATERIAL_INSTANCE_ID_SHIFT    = 0;

    static constexpr uint64_t MATERIAL_VARIANT_KEY_MASK     = 0x000FF000llu;
    static constexpr unsigned MATERIAL_VARIANT_KEY_SHIFT    = 12;

    static constexpr uint64_t MATERIAL_ID_MASK              = 0xFFF00000llu;
    static constexpr unsigned MATERIAL_ID_SHIFT             = 20;

    static constexpr uint64_t BLEND_DISTANCE_MASK           = 0xFFFFFFFF0000llu;
    static constexpr unsigned BLEND_DISTANCE_SHIFT          = 16;

    static constexpr uint64_t MATERIAL_MASK                 = 0xFFFFFFFFllu;
    static constexpr unsigned MATERIAL_SHIFT                = 0;

    static constexpr uint64_t Z_BUCKET_MASK                 = 0x3FF00000000llu;
    static constexpr unsigned Z_BUCKET_SHIFT                = 32;

    static constexpr uint64_t PRIORITY_MASK                 = 0x001C000000000000llu;
    static constexpr unsigned PRIORITY_SHIFT                = 50;

    static constexpr uint64_t BLENDING_MASK                 = 0x0020000000000000llu;
    static constexpr unsigned BLENDING_SHIFT                = 53;

    static constexpr uint64_t CUSTOM_MASK                   = 0x0300000000000000llu;
    static constexpr unsigned CUSTOM_SHIFT                  = 56;

    static constexpr uint64_t PASS_MASK                     = 0x0C00000000000000llu;
    static constexpr unsigned PASS_SHIFT                    = 58;

    static constexpr unsigned CHANNEL_SHIFT                 = 61;
    static constexpr uint64_t CHANNEL_MASK                  = (CHANNEL_COUNT - 1) << CHANNEL_SHIFT;


    static constexpr uint64_t CUSTOM_ORDER_MASK             = 0x003FFFFF00000000llu;
    static constexpr unsigned CUSTOM_ORDER_SHIFT            = 32;

    static constexpr uint64_t CUSTOM_INDEX_MASK             = 0x00000000FFFFFFFFllu;
    static constexpr unsigned CUSTOM_INDEX_SHIFT            = 0;

    // we assume Variant fits in 8-bits.
    static_assert(sizeof(Variant::type_t) == 1);

    enum class Pass : uint64_t {    // 6-bits max
        DEPTH    = uint64_t(0x00) << PASS_SHIFT,
        COLOR    = uint64_t(0x01) << PASS_SHIFT,
        REFRACT  = uint64_t(0x02) << PASS_SHIFT,
        BLENDED  = uint64_t(0x03) << PASS_SHIFT,
        SENTINEL = 0xffffffffffffffffllu
    };

    enum class CustomCommand : uint64_t {    // 2-bits max
        PROLOGUE    = uint64_t(0x0) << CUSTOM_SHIFT,
        PASS        = uint64_t(0x1) << CUSTOM_SHIFT,
        EPILOGUE    = uint64_t(0x2) << CUSTOM_SHIFT
    };

    /**
     * 命令类型标志枚举
     * 
     * 定义渲染通道的类型和过滤选项。
     */
    enum class CommandTypeFlags : uint32_t {
        COLOR = 0x1,    // 仅生成颜色通道
        DEPTH = 0x2,    // 仅生成深度通道（例如阴影贴图）

        /**
         * 深度缓冲区包含阴影投射者
         * 阴影投射者无论混合模式（或 alpha 遮罩）如何都会渲染到深度缓冲区
         */
        // shadow-casters are rendered in the depth buffer, regardless of blending (or alpha masking)
        DEPTH_CONTAINS_SHADOW_CASTERS = 0x4,
        /**
         * 深度通道过滤 alpha 遮罩对象
         * alpha 测试对象不会渲染到深度缓冲区
         */
        // alpha-tested objects are not rendered in the depth buffer
        DEPTH_FILTER_ALPHA_MASKED_OBJECTS = 0x08,

        /**
         * 过滤半透明对象
         * alpha 混合对象不会渲染到深度缓冲区
         */
        // alpha-blended objects are not rendered in the depth buffer
        FILTER_TRANSLUCENT_OBJECTS = 0x10,

        /**
         * 阴影贴图命令
         * 生成阴影贴图的命令
         */
        // generate commands for shadow map
        SHADOW = DEPTH | DEPTH_CONTAINS_SHADOW_CASTERS,
        /**
         * SSAO 命令
         * 生成 SSAO（屏幕空间环境光遮蔽）的命令
         */
        // generate commands for SSAO
        SSAO = DEPTH | FILTER_TRANSLUCENT_OBJECTS,
        /**
         * 屏幕空间反射命令
         * 生成屏幕空间反射的命令
         */
        // generate commands for screen-space reflections
        SCREEN_SPACE_REFLECTIONS = COLOR | FILTER_TRANSLUCENT_OBJECTS
    };

    /**
     * 排序材质键是 32 位，编码为：
     *
     * |     12     |   8    |     12     |
     * +------------+--------+------------+
     * |  material  |variant |  instance  |
     * +------------+--------+------------+
     *
     * 变体在构建命令时插入，因为我们在此之前不知道它
     */
    /*
     * The sorting material key is 32 bits and encoded as:
     *
     * |     12     |   8    |     12     |
     * +------------+--------+------------+
     * |  material  |variant |  instance  |
     * +------------+--------+------------+
     *
     * The variant is inserted while building the commands, because we don't know it before that
     */
    /**
     * 创建材质排序键
     * 
     * 根据材质 ID 和实例 ID 创建排序键。
     * 
     * @param materialId 材质 ID
     * @param instanceId 实例 ID
     * @return 排序键
     */
    static CommandKey makeMaterialSortingKey(uint32_t const materialId, uint32_t const instanceId) noexcept {
        CommandKey const key = ((materialId << MATERIAL_ID_SHIFT) & MATERIAL_ID_MASK) |  // 材质 ID（12 位）
                         ((instanceId << MATERIAL_INSTANCE_ID_SHIFT) & MATERIAL_INSTANCE_ID_MASK);  // 实例 ID（12 位）
        return (key << MATERIAL_SHIFT) & MATERIAL_MASK;  // 返回材质键（32 位）
    }

    /**
     * 创建字段（模板辅助函数）
     * 
     * 将值编码到命令键的指定字段中。
     * 
     * @tparam T 值类型
     * @param value 值
     * @param mask 掩码
     * @param shift 移位量
     * @return 编码后的字段值
     */
    template<typename T>
    static CommandKey makeField(T value, uint64_t const mask, unsigned const shift) noexcept {
        assert_invariant(!((uint64_t(value) << shift) & ~mask));  // 断言值在掩码范围内
        return uint64_t(value) << shift;  // 移位并返回
    }

    /**
     * 选择（布尔值到全 1 或全 0）
     * 
     * 根据布尔值返回全 1 或全 0。
     * 
     * @tparam T 值类型
     * @param boolish 布尔值
     * @return 如果为 true 返回全 1，否则返回 0
     */
    template<typename T>
    static CommandKey select(T boolish) noexcept {
        return boolish ? std::numeric_limits<uint64_t>::max() : uint64_t(0);  // 返回全 1 或 0
    }

    /**
     * 选择（布尔值到指定值或 0）
     * 
     * 根据布尔值返回指定值或 0。
     * 
     * @tparam T 值类型
     * @param boolish 布尔值
     * @param value 如果为 true 返回的值
     * @return 如果为 true 返回 value，否则返回 0
     */
    template<typename T>
    static CommandKey select(T boolish, uint64_t value) noexcept {
        return boolish ? value : uint64_t(0);  // 返回 value 或 0
    }

    /**
     * 图元信息结构（56 字节）
     * 
     * 包含渲染图元的所有必要信息。
     */
    struct PrimitiveInfo { // 56 bytes
        /**
         * 联合体：材质实例指针或填充
         * 使用填充确保在所有平台上此字段为 64 位
         */
        union {
            FMaterialInstance const* mi;  // 材质实例指针
            uint64_t padding; // make this field 64 bits on all platforms
        };
        backend::RenderPrimitiveHandle rph;                 // 4 bytes - 渲染图元句柄
        backend::VertexBufferInfoHandle vbih;               // 4 bytes - 顶点缓冲区信息句柄
        backend::DescriptorSetHandle dsh;                   // 4 bytes - 描述符堆句柄
        uint32_t indexOffset;                               // 4 bytes - 索引偏移
        uint32_t indexCount;                                // 4 bytes - 索引数量
        uint32_t index = 0;                                 // 4 bytes - 索引
        uint32_t skinningOffset = 0;                        // 4 bytes - 蒙皮偏移
        uint32_t morphingOffset = 0;                        // 4 bytes - 变形偏移

        backend::RasterState rasterState;                   // 4 bytes - 光栅化状态

        uint16_t instanceCount;                             // 2 bytes - 实例数量 [MSb: user]
        Variant materialVariant;                            // 1 byte - 材质变体
        backend::PrimitiveType type : 3;                    // 1 byte - 图元类型（3 位）
        bool hasSkinning : 1;                               // 1 bit - 是否有蒙皮
        bool hasMorphing : 1;                               // 1 bit - 是否有变形
        bool hasHybridInstancing : 1;                       // 1 bit - 是否有混合实例化

        uint32_t rfu[2];                                    // 8 bytes - 保留字段
    };
    static_assert(sizeof(PrimitiveInfo) == 56);

    /**
     * 命令结构（64 字节，8 字节对齐）
     * 
     * 表示一个渲染命令，包含排序键和图元信息。
     */
    struct alignas(8) Command {     // 64 bytes
        CommandKey key = 0;         //  8 bytes - 命令键（用于排序）
        PrimitiveInfo info;    // 56 bytes - 图元信息
        /**
         * 小于比较运算符（用于排序）
         * 
         * @param rhs 右侧命令
         * @return 如果此命令的键小于右侧命令的键则返回 true
         */
        bool operator < (Command const& rhs) const noexcept { return key < rhs.key; }
        /**
         * 放置 new 运算符
         * 
         * 声明为 "throw" 以避免编译器的空检查。
         * 
         * @param size 大小（未使用）
         * @param ptr 内存指针
         * @return 内存指针
         */
        // placement new declared as "throw" to avoid the compiler's null-check
        void* operator new (size_t, void* ptr) {
            assert_invariant(ptr);
            return ptr;
        }
    };
    static_assert(sizeof(Command) == 64);
    static_assert(std::is_trivially_destructible_v<Command>,
            "Command isn't trivially destructible");

    /**
     * 渲染标志类型
     */
    using RenderFlags = uint8_t;
    /**
     * 有阴影
     */
    static constexpr RenderFlags HAS_SHADOWING             = 0x01;
    /**
     * 反转前表面
     */
    static constexpr RenderFlags HAS_INVERSE_FRONT_FACES   = 0x02;
    /**
     * 实例化立体渲染
     */
    static constexpr RenderFlags IS_INSTANCED_STEREOSCOPIC = 0x04;
    /**
     * 有深度夹紧
     */
    static constexpr RenderFlags HAS_DEPTH_CLAMP           = 0x08;

    /**
     * 命令使用的内存池
     * 
     * 使用线性分配器（带回退）、无锁策略、高水位标记跟踪、静态区域策略。
     */
    // Arena used for commands
    using Arena = utils::Arena<
            utils::LinearAllocatorWithFallback,
            utils::LockingPolicy::NoLock,
            utils::TrackingPolicy::HighWatermark,
            utils::AreaPolicy::StaticArea>;

    /**
     * RenderPass 只能移动
     */
    // RenderPass can only be moved
    RenderPass(RenderPass&& rhs) = default;
    /**
     * 禁止移动赋值（如果需要可以支持）
     */
    RenderPass& operator=(RenderPass&& rhs) = delete;  // could be supported if needed

    /**
     * RenderPass 不能拷贝
     */
    // RenderPass can't be copied
    RenderPass(RenderPass const& rhs) = delete;
    RenderPass& operator=(RenderPass const& rhs) = delete;

    /**
     * 析构函数
     * 
     * 注意：分配的命令不会被释放，它们由 Arena 拥有。
     */
    // allocated commands ARE NOT freed, they're owned by the Arena
    ~RenderPass() noexcept;

    /**
     * 设置裁剪视口
     * 
     * 指定裁剪矩形的视口，即最终裁剪矩形会偏移视口的左上角，
     * 并裁剪到视口的宽度/高度。
     * 
     * @param viewport 视口
     */
    // Specifies the viewport for the scissor rectangle, that is, the final scissor rect is
    // offset by the viewport's left-top and clipped to the viewport's width/height.
    void setScissorViewport(backend::Viewport const viewport) noexcept {
        mScissorViewport = viewport;
    }

    /**
     * 获取命令开始迭代器
     * 
     * @return 第一个命令的指针
     */
    Command const* begin() const noexcept { return mCommandBegin; }
    /**
     * 获取命令结束迭代器
     * 
     * @return 最后一个命令之后的指针
     */
    Command const* end() const noexcept { return mCommandEnd; }
    /**
     * 检查是否为空
     * 
     * @return 如果没有命令则返回 true
     */
    bool empty() const noexcept { return begin() == end(); }

    /**
     * 缓冲区对象句柄删除器
     * 
     * 用于自动管理缓冲区对象句柄的生命周期。
     */
    class BufferObjectHandleDeleter {
        std::reference_wrapper<backend::DriverApi> driver;  // 驱动 API 引用
    public:
        /**
         * 构造函数
         * 
         * @param driver 驱动 API 引用
         */
        explicit BufferObjectHandleDeleter(backend::DriverApi& driver) noexcept : driver(driver) { }
        /**
         * 删除操作符
         * 
         * @param handle 缓冲区对象句柄
         */
        void operator()(backend::BufferObjectHandle handle) noexcept;
    };

    /**
     * 描述符堆句柄删除器
     * 
     * 用于自动管理描述符堆句柄的生命周期。
     */
    class DescriptorSetHandleDeleter {
        std::reference_wrapper<backend::DriverApi> driver;  // 驱动 API 引用
    public:
        /**
         * 构造函数
         * 
         * @param driver 驱动 API 引用
         */
        explicit DescriptorSetHandleDeleter(backend::DriverApi& driver) noexcept : driver(driver) { }
        /**
         * 删除操作符
         * 
         * @param handle 描述符堆句柄
         */
        void operator()(backend::DescriptorSetHandle handle) noexcept;
    };

    /**
     * 缓冲区对象共享句柄类型
     */
    using BufferObjectSharedHandle = SharedHandle<
            backend::HwBufferObject, BufferObjectHandleDeleter>;

    /**
     * 描述符堆共享句柄类型
     */
    using DescriptorSetSharedHandle = SharedHandle<
            backend::HwDescriptorSet, DescriptorSetHandleDeleter>;

    /**
     * 执行器类
     * 
     * 保存给定通道要执行的命令范围。
     */
    /*
     * Executor holds the range of commands to execute for a given pass
     */
    class Executor {
        /**
         * 自定义命令函数类型
         */
        using CustomCommandFn = std::function<void()>;
        friend class RenderPass;
        friend class RenderPassBuilder;

        /**
         * 这些字段在创建后是常量
         */
        // these fields are constant after creation
        utils::Slice<const Command> mCommands;  // 命令切片
        utils::Slice<const CustomCommandFn> mCustomCommands;  // 自定义命令切片
        BufferObjectSharedHandle mInstancedUboHandle;  // 实例化 UBO 句柄
        DescriptorSetSharedHandle mInstancedDescriptorSetHandle;  // 实例化描述符堆句柄
        ColorPassDescriptorSet const* mColorPassDescriptorSet = nullptr;  // 颜色通道描述符堆
        /**
         * 存储裁剪视口或裁剪覆盖
         */
        // this stores either the scissor-viewport or the scissor override
        backend::Viewport mScissor{ 0, 0, INT32_MAX, INT32_MAX };

        /**
         * 多边形偏移覆盖值
         */
        // value of the polygon offset override
        backend::PolygonOffset mPolygonOffset{};
        /**
         * 是否覆盖 MaterialInstance 的多边形偏移
         */
        // whether to override the polygon offset from the MaterialInstance
        bool mPolygonOffsetOverride : 1;
        /**
         * 是否覆盖 MaterialInstance 的裁剪矩形
         */
        // whether to override the scissor rectangle from the MaterialInstance
        bool mScissorOverride : 1;
        /**
         * 是否设置了裁剪视口
         */
        // whether the scissor-viewport is set
        bool mHasScissorViewport : 1;

        Executor(RenderPass const& pass, Command const* b, Command const* e) noexcept;

        void execute(FEngine const& engine, backend::DriverApi& driver,
                Command const* first, Command const* last) const noexcept;

        static backend::Viewport applyScissorViewport(
                backend::Viewport const& scissorViewport,
                backend::Viewport const& scissor) noexcept;

    public:
        // fixme: needed in ShadowMapManager
        Executor() noexcept;

        // can't be copied
        Executor(Executor const& rhs) noexcept = delete;
        Executor& operator=(Executor const& rhs) noexcept = delete;

        // can be moved
        Executor(Executor&& rhs) noexcept;
        Executor& operator=(Executor&& rhs) noexcept;

        ~Executor() noexcept;

        // if non-null, overrides the material's polygon offset
        void overridePolygonOffset(backend::PolygonOffset const* polygonOffset) noexcept;

        void overrideScissor(backend::Viewport const& scissor) noexcept;

        void execute(FEngine const& engine, backend::DriverApi& driver) const noexcept;
    };

    /**
     * 获取执行器
     * 
     * 返回此通道的新执行器（使用所有命令）。
     * 
     * @return 执行器对象
     */
    // returns a new executor for this pass
    Executor getExecutor() const {
        return getExecutor(mCommandBegin, mCommandEnd);  // 使用所有命令
    }

    /**
     * 获取执行器（指定范围）
     * 
     * 返回指定命令范围的新执行器。
     * 
     * @param b 命令开始指针
     * @param e 命令结束指针
     * @return 执行器对象
     */
    Executor getExecutor(Command const* b, Command const* e) const {
        return { *this, b, e };  // 创建执行器
    }

private:
    friend class FRenderer;
    friend class RenderPassBuilder;
    RenderPass(FEngine const& engine, backend::DriverApi& driver,
            RenderPassBuilder const& builder) noexcept;

    /**
     * 这是此类的主要函数，使用当前相机、几何和设置的标志
     * 将命令追加到通道。如果需要，可以多次调用。
     */
    // This is the main function of this class, this appends commands to the pass using
    // the current camera, geometry and flags set. This can be called multiple times if needed.
    /**
     * 追加命令
     * 
     * 生成并追加渲染命令到通道。
     * 
     * @param engine 引擎常量引用
     * @param commands 命令切片（输出）
     * @param visibleRenderables 可见可渲染对象范围
     * @param commandTypeFlags 命令类型标志
     * @param renderFlags 渲染标志
     * @param visibilityMask 可见性掩码
     * @param variant 材质变体
     * @param cameraPosition 相机位置
     * @param cameraForwardVector 相机前向量
     */
    void appendCommands(FEngine const& engine,
            utils::Slice<Command> commands,
            utils::Range<uint32_t> visibleRenderables,
            CommandTypeFlags commandTypeFlags,
            RenderFlags renderFlags,
            FScene::VisibleMaskType visibilityMask,
            Variant variant,
            math::float3 cameraPosition,
            math::float3 cameraForwardVector) const noexcept;

    /**
     * 追加自定义命令
     * 
     * @param commands 命令数组
     * @param channel 通道索引
     * @param pass 通道类型
     * @param custom 自定义命令类型
     * @param order 顺序
     * @param command 命令函数
     */
    // Appends a custom command.
    void appendCustomCommand(Command* commands,
            uint8_t channel, Pass pass, CustomCommand custom, uint32_t order,
            Executor::CustomCommandFn command);

    /**
     * 调整命令数组大小
     * 
     * 在需要时扩展命令数组。
     * 
     * @param arena 内存分配器
     * @param last 最后一个命令指针
     * @return 调整后的最后一个命令指针
     */
    static Command* resize(Arena& arena, Command* last) noexcept;

    /**
     * 排序命令然后修剪哨兵
     * 
     * 对命令进行排序，并移除哨兵命令。
     * 
     * @param begin 命令开始指针
     * @param end 命令结束指针
     * @return 排序后的结束指针（哨兵之前）
     */
    // sorts commands then trims sentinels
    static Command* sortCommands(
            Command* begin, Command* end) noexcept;

    /**
     * 实例化命令然后修剪哨兵
     * 
     * 将命令转换为实例化渲染命令，并移除哨兵。
     * 
     * @param driver 驱动 API 引用
     * @param perRenderableDescriptorSetLayoutHandle 每个可渲染对象的描述符堆布局句柄
     * @param begin 命令开始指针
     * @param end 命令结束指针
     * @param eyeCount 眼睛数量（立体渲染）
     * @return 实例化后的结束指针（哨兵之前）
     */
    // instanceify commands then trims sentinels
    Command* instanceify(backend::DriverApi& driver,
            backend::DescriptorSetLayoutHandle perRenderableDescriptorSetLayoutHandle,
            Command* begin, Command* end,
            int32_t eyeCount) const noexcept;

    /**
     * 我们选择每个作业的命令数量以最小化 JobSystem 开销。
     */
    // We choose the command count per job to minimize JobSystem overhead.
    static constexpr size_t JOBS_PARALLEL_FOR_COMMANDS_COUNT = 128;  // 每个作业的命令数量
    static constexpr size_t JOBS_PARALLEL_FOR_COMMANDS_SIZE  =
            sizeof(Command) * JOBS_PARALLEL_FOR_COMMANDS_COUNT;  // 每个作业的大小

    static_assert(JOBS_PARALLEL_FOR_COMMANDS_SIZE % utils::CACHELINE_SIZE == 0,
            "Size of Commands jobs must be multiple of a cache-line size");  // 断言大小是缓存行的倍数

    /**
     * 生成命令
     * 
     * 为可见可渲染对象生成渲染命令。
     * 
     * @param commandTypeFlags 命令类型标志
     * @param commands 命令数组（输出）
     * @param soa 可渲染对象 SoA 数据
     * @param range 可见可渲染对象范围
     * @param variant 材质变体
     * @param renderFlags 渲染标志
     * @param visibilityMask 可见性掩码
     * @param cameraPosition 相机位置
     * @param cameraForward 相机前向量
     * @param instancedStereoEyeCount 实例化立体眼睛数量
     */
    static inline void generateCommands(CommandTypeFlags commandTypeFlags, Command* commands,
            FScene::RenderableSoa const& soa, utils::Range<uint32_t> range,
            Variant variant, RenderFlags renderFlags,
            FScene::VisibleMaskType visibilityMask,
            math::float3 cameraPosition, math::float3 cameraForward,
            uint8_t instancedStereoEyeCount) noexcept;

    /**
     * 生成命令实现（模板特化）
     * 
     * 根据命令类型标志生成特定类型的命令。
     * 
     * @tparam commandTypeFlags 命令类型标志（编译时常量）
     * @param extraFlags 额外标志
     * @param curr 当前命令指针（输出）
     * @param soa 可渲染对象 SoA 数据
     * @param range 可见可渲染对象范围
     * @param variant 材质变体
     * @param renderFlags 渲染标志
     * @param visibilityMask 可见性掩码
     * @param cameraPosition 相机位置
     * @param cameraForward 相机前向量
     * @param instancedStereoEyeCount 实例化立体眼睛数量
     * @return 生成命令后的当前指针
     */
    template<CommandTypeFlags commandTypeFlags>
    static Command* generateCommandsImpl(CommandTypeFlags extraFlags,
            Command* curr, FScene::RenderableSoa const& soa, utils::Range<uint32_t> range,
            Variant variant, RenderFlags renderFlags, FScene::VisibleMaskType visibilityMask,
            math::float3 cameraPosition, math::float3 cameraForward,
            uint8_t instancedStereoEyeCount) noexcept;

    /**
     * 设置颜色命令
     * 
     * 配置颜色通道命令的参数。
     * 
     * @param cmdDraw 命令引用（输出）
     * @param variant 材质变体
     * @param mi 材质实例指针
     * @param inverseFrontFaces 是否反转前表面
     * @param hasDepthClamp 是否有深度夹紧
     */
    static void setupColorCommand(Command& cmdDraw, Variant variant,
            FMaterialInstance const* mi, bool inverseFrontFaces, bool hasDepthClamp) noexcept;

    /**
     * 更新累计图元数量
     * 
     * 更新可渲染对象的累计图元数量（用于自动实例化）。
     * 
     * @param renderableData 可渲染对象数据（SoA 布局）
     * @param vr 可见可渲染对象范围
     */
    static void updateSummedPrimitiveCounts(
            FScene::RenderableSoa& renderableData, utils::Range<uint32_t> vr) noexcept;

    FScene::RenderableSoa const& mRenderableSoa;  // 可渲染对象 SoA 数据引用
    ColorPassDescriptorSet const* const mColorPassDescriptorSet;  // 颜色通道描述符堆指针
    backend::Viewport mScissorViewport{ 0, 0, INT32_MAX, INT32_MAX };  // 裁剪视口
    Command const* /* const */ mCommandBegin = nullptr;   // 指向第一个命令的指针
    // Pointer to the first command
    Command const* /* const */ mCommandEnd = nullptr;     // 指向最后一个命令之后的指针
    // Pointer to one past the last command
    mutable BufferObjectSharedHandle mInstancedUboHandle;  // 实例化图元的 UBO 句柄
    // ubo for instanced primitives
    mutable DescriptorSetSharedHandle mInstancedDescriptorSetHandle;  // 保存 UBO 的描述符堆
    // a descriptor-set to hold the ubo
    /**
     * 自定义命令的向量
     */
    // a vector for our custom commands
    using CustomCommandVector = utils::FixedCapacityVector<Executor::CustomCommandFn>;  // 自定义命令向量类型
    mutable CustomCommandVector mCustomCommands;  // 自定义命令向量
};

/**
 * 渲染通道构建器类
 * 
 * 用于构建 RenderPass 的构建器模式类。
 * 提供链式 API 来配置渲染通道的参数。
 */
class RenderPassBuilder {
    friend class RenderPass;  // 友元类

    RenderPass::Arena& mArena;  // 内存分配器引用
    RenderPass::CommandTypeFlags mCommandTypeFlags{};  // 命令类型标志
    FScene::RenderableSoa const* mRenderableSoa = nullptr;  // 可渲染对象 SoA 数据指针
    utils::Range<uint32_t> mVisibleRenderables{};  // 可见可渲染对象范围
    math::float3 mCameraPosition{};  // 相机位置
    math::float3 mCameraForwardVector{};  // 相机前向量
    RenderPass::RenderFlags mFlags{};  // 渲染标志
    Variant mVariant{};  // 材质变体
    ColorPassDescriptorSet const* mColorPassDescriptorSet = nullptr;  // 颜色通道描述符堆指针
    FScene::VisibleMaskType mVisibilityMask = std::numeric_limits<FScene::VisibleMaskType>::max();  // 可见性掩码（默认全 1）

    /**
     * 自定义命令记录类型
     * 
     * 元组：通道、通道类型、自定义命令类型、顺序、命令函数
     */
    using CustomCommandRecord = std::tuple<
            uint8_t,  // 通道索引
            RenderPass::Pass,  // 通道类型
            RenderPass::CustomCommand,  // 自定义命令类型
            uint32_t,  // 顺序
            RenderPass::Executor::CustomCommandFn>;  // 命令函数

    using CustomCommandContainer = std::vector<CustomCommandRecord>;  // 自定义命令容器类型

    /**
     * 我们将其设为可选，因为它不经常使用，我们不想
     * 默认构造它。出于同样的原因，我们使用 std::vector<>
     */
    // we make this optional because it's not used often, and we don't want to have
    // to construct it by default. For the same reason we use a std::vector<>
    std::optional<CustomCommandContainer> mCustomCommands;  // 自定义命令容器（可选）

public:
    /**
     * 构造函数
     * 
     * @param arena 内存分配器引用
     */
    explicit RenderPassBuilder(RenderPass::Arena& arena) : mArena(arena) { }

    /**
     * 设置命令类型标志
     * 
     * @param commandTypeFlags 命令类型标志
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& commandTypeFlags(RenderPass::CommandTypeFlags const commandTypeFlags) noexcept {
        mCommandTypeFlags = commandTypeFlags;  // 设置命令类型标志
        return *this;  // 返回自身引用
    }

    /**
     * 指定要为其生成命令的几何
     */
    // specifies the geometry to generate commands for
    /**
     * 设置几何数据
     * 
     * @param soa 可渲染对象 SoA 数据引用
     * @param vr 可见可渲染对象范围
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& geometry(
            FScene::RenderableSoa const& soa, utils::Range<uint32_t> const vr) noexcept {
        mRenderableSoa = &soa;  // 设置 SoA 数据指针
        mVisibleRenderables = vr;  // 设置可见范围
        return *this;  // 返回自身引用
    }

    /**
     * 指定相机信息（例如用于排序命令）
     */
    // Specifies camera information (e.g. used for sorting commands)
    /**
     * 设置相机信息
     * 
     * @param position 相机位置
     * @param forward 相机前向量
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& camera(math::float3 position, math::float3 forward) noexcept {
        mCameraPosition = position;  // 设置相机位置
        mCameraForwardVector = forward;  // 设置相机前向量
        return *this;  // 返回自身引用
    }

    /**
     * 控制命令生成方式的标志
     */
    //  flags controlling how commands are generated
    /**
     * 设置渲染标志
     * 
     * @param flags 渲染标志
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& renderFlags(RenderPass::RenderFlags const flags) noexcept {
        mFlags = flags;  // 设置渲染标志
        return *this;  // 返回自身引用
    }

    /**
     * 设置渲染标志（位操作版本）
     * 
     * 允许设置特定标志，类似于上面的方法但允许设置特定标志。
     * 
     * @param mask 掩码（哪些位有效）
     * @param value 值（位的值）
     * @return 构建器引用（支持链式调用）
     */
    // like above but allows to set specific flags
    RenderPassBuilder& renderFlags(
            RenderPass::RenderFlags const mask, RenderPass::RenderFlags value) noexcept {
        value &= mask;  // 限制值在掩码范围内
        mFlags &= ~mask;  // 清除掩码位
        mFlags |= value;  // 设置新值
        return *this;  // 返回自身引用
    }

    /**
     * 设置要使用的变体
     */
    // variant to use
    /**
     * 设置材质变体
     * 
     * @param variant 材质变体
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& variant(Variant const variant) noexcept {
        mVariant = variant;  // 设置变体
        return *this;  // 返回自身引用
    }

    /**
     * 设置颜色通道描述符堆
     */
    // variant to use
    /**
     * 设置颜色通道描述符堆
     * 
     * @param colorPassDescriptorSet 颜色通道描述符堆指针
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& colorPassDescriptorSet(ColorPassDescriptorSet const* colorPassDescriptorSet) noexcept {
        mColorPassDescriptorSet = colorPassDescriptorSet;  // 设置描述符堆指针
        return *this;  // 返回自身引用
    }

    /**
     * 设置可见性掩码
     * 
     * 设置可见性掩码，它与每个可渲染对象的 VISIBLE_MASK 进行 AND 运算
     * 以确定可渲染对象在此通道中是否可见。
     * 默认为全 1，这意味着此渲染通道中的所有可渲染对象都将被渲染。
     */
    // Sets the visibility mask, which is AND-ed against each Renderable's VISIBLE_MASK to
    // determine if the renderable is visible for this pass.
    // Defaults to all 1's, which means all renderables in this render pass will be rendered.
    /**
     * 设置可见性掩码
     * 
     * @param mask 可见性掩码
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& visibilityMask(FScene::VisibleMaskType const mask) noexcept {
        mVisibilityMask = mask;  // 设置可见性掩码
        return *this;  // 返回自身引用
    }

    /**
     * 添加自定义命令
     * 
     * @param channel 通道索引
     * @param pass 通道类型
     * @param custom 自定义命令类型
     * @param order 顺序
     * @param command 命令函数
     * @return 构建器引用（支持链式调用）
     */
    RenderPassBuilder& customCommand(
            uint8_t channel,
            RenderPass::Pass pass,
            RenderPass::CustomCommand custom,
            uint32_t order,
            const RenderPass::Executor::CustomCommandFn& command);

    /**
     * 构建渲染通道
     * 
     * 根据构建器配置创建 RenderPass 对象。
     * 
     * @param engine 引擎常量引用
     * @param driver 驱动 API 引用
     * @return 渲染通道对象
     */
    RenderPass build(FEngine const& engine, backend::DriverApi& driver) const;
};


} // namespace filament

template<> struct utils::EnableBitMaskOperators<filament::RenderPass::CommandTypeFlags>
        : public std::true_type {};

#endif // TNT_FILAMENT_RENDERPASS_H
