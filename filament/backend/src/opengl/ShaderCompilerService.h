/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_OPENGL_SHADERCOMPILERSERVICE_H
#define TNT_FILAMENT_BACKEND_OPENGL_SHADERCOMPILERSERVICE_H

#include "CallbackManager.h"
#include "CompilerThreadPool.h"
#include "OpenGLBlobCache.h"

#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Program.h>

#include <utils/CString.h>
#include <utils/FixedCapacityVector.h>
#include <utils/JobSystem.h>

#include <array>
#include <functional>
#include <list>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <stdint.h>

namespace filament::backend {

class OpenGLDriver;
class OpenGLContext;
class OpenGLPlatform;
class Program;
class CallbackHandler;

/**
 * 着色器编译服务类
 * 
 * 处理着色器编译，支持异步编译。
 * 
 * 主要功能：
 * 1. 异步着色器编译和链接
 * 2. 程序二进制缓存（Blob Cache）
 * 3. 多种编译模式（同步、线程池、异步）
 * 4. 回调管理（通知编译完成）
 * 
 * 编译模式：
 * - SYNCHRONOUS：同步编译（在主线程）
 * - THREAD_POOL：线程池编译（使用共享上下文）
 * - ASYNCHRONOUS：异步编译（使用 KHR_parallel_shader_compile）
 * 
 * 设计特点：
 * - 使用 Token 模式管理编译状态
 * - 支持编译取消和资源清理
 * - 自动选择最佳编译模式
 * - 支持程序二进制缓存以减少编译时间
 */
class ShaderCompilerService {
    struct OpenGLProgramToken;  // 前向声明：程序 Token 结构

public:
    using program_token_t = std::shared_ptr<OpenGLProgramToken>;  // 程序 Token 类型
    using shaders_t = std::array<GLuint, Program::SHADER_TYPE_COUNT>;  // 着色器数组类型

    /**
     * 构造函数
     * 
     * @param driver OpenGLDriver 引用
     */
    explicit ShaderCompilerService(OpenGLDriver& driver);

    // 禁止拷贝和移动
    ShaderCompilerService(ShaderCompilerService const& rhs) = delete;
    ShaderCompilerService(ShaderCompilerService&& rhs) = delete;
    ShaderCompilerService& operator=(ShaderCompilerService const& rhs) = delete;
    ShaderCompilerService& operator=(ShaderCompilerService&& rhs) = delete;

    /**
     * 析构函数
     * 
     * 清理所有资源。
     */
    ~ShaderCompilerService() noexcept;

    /**
     * 检查是否支持并行着色器编译
     * 
     * @return 如果支持并行编译返回 true，否则返回 false
     */
    bool isParallelShaderCompileSupported() const noexcept;

    /**
     * 初始化编译服务
     * 
     * 根据硬件支持选择最佳编译模式。
     */
    void init() noexcept;
    
    /**
     * 终止编译服务
     * 
     * 清理所有资源，包括线程池和待处理任务。
     */
    void terminate() noexcept;

    /**
     * 创建程序（编译 + 链接）
     * 
     * 如果支持，则异步创建程序。
     * 
     * @param name 程序名称
     * @param program 程序对象（将被移动）
     * @return 程序 Token
     */
    program_token_t createProgram(utils::CString const& name, Program&& program);

    /**
     * 获取 GL 程序
     * 
     * 返回 GL 程序 ID，必要时会阻塞。
     * Token 会被销毁并变为无效。
     * 
     * @param token 程序 Token 引用（将被置为 nullptr）
     * @return GL 程序 ID
     */
    GLuint getProgram(program_token_t& token);

    /**
     * 每帧必须调用
     * 
     * 处理待处理的编译任务和回调。
     */
    void tick();

    /**
     * 终止程序编译
     * 
     * 销毁有效的 Token 和所有关联资源。
     * 用于"取消"程序编译。
     * 
     * 如果已经调用了 `initialize(token)`，则不会调用此函数。
     * 
     * @param token 程序 Token 引用（将被置为 nullptr）
     */
    static void terminate(program_token_t& token);

    /**
     * 在 Token 中存储用户数据指针
     * 
     * @param token 程序 Token
     * @param user 用户数据指针
     */
    static void setUserData(const program_token_t& token, void* user) noexcept;

    /**
     * 检索存储在 Token 中的用户数据指针
     * 
     * @param token 程序 Token
     * @return 用户数据指针
     */
    static void* getUserData(const program_token_t& token) noexcept;

    /**
     * 发出一个回调句柄
     * 
     * @return 回调句柄
     */
    CallbackManager::Handle issueCallbackHandle() const noexcept;

    /**
     * 将回调句柄返回给回调管理器
     * 
     * @param handle 回调句柄
     */
    void submitCallbackHandle(CallbackManager::Handle handle) noexcept;

    /**
     * 当所有活动程序准备就绪时调用回调
     * 
     * @param handler 回调处理器
     * @param callback 回调函数
     * @param user 用户数据指针
     */
    void notifyWhenAllProgramsAreReady(
            CallbackHandler* handler, CallbackHandler::Callback callback, void* user);

private:
    /**
     * 任务结构
     * 
     * 表示一个待执行的任务。
     * 任务函数返回 true 表示任务完成，可以移除。
     */
    struct Job {
        template<typename FUNC>
        Job(FUNC&& fn) : fn(std::forward<FUNC>(fn)) {} // NOLINT(*-explicit-constructor)
        Job(std::function<bool(Job const& job)> fn,
                CallbackHandler* handler, void* user, CallbackHandler::Callback callback)
                : fn(std::move(fn)), handler(handler), user(user), callback(callback) {
        }
        std::function<bool(Job const& job)> fn;  // 任务函数（返回 true 表示完成）
        CallbackHandler* handler = nullptr;      // 回调处理器
        void* user = nullptr;                    // 用户数据指针
        CallbackHandler::Callback callback{};   // 回调函数
    };

    /**
     * 编译模式枚举
     */
    enum class Mode {
        UNDEFINED,      // init() 尚未被调用
        SYNCHRONOUS,    // 同步着色器编译（在主线程）
        THREAD_POOL,    // 使用线程池的异步着色器编译（最常见）
        ASYNCHRONOUS    // 使用 KHR_parallel_shader_compile 的异步着色器编译
    };

    OpenGLDriver& mDriver;                    // OpenGL 驱动引用
    OpenGLBlobCache mBlobCache;              // 程序二进制缓存
    CallbackManager mCallbackManager;        // 回调管理器
    CompilerThreadPool mCompilerThreadPool;   // 编译线程池

    uint32_t mShaderCompilerThreadCount = 0u;  // 着色器编译线程数
    Mode mMode = Mode::UNDEFINED;              // 编译模式（在调用 init() 后有效）

    using ContainerType = std::tuple<CompilerPriorityQueue, program_token_t, Job>;
    std::vector<ContainerType> mRunAtNextTickOps;  // 下次 tick 时运行的任务

    std::list<program_token_t> mCanceledTokens;   // 已取消的 Token 列表
    
    // 这些成员仅在主线程上访问，当 mMode 不是 SYNCHRONOUS 时完全未使用
    uint32_t mNumProgramsCreatedSynchronouslyThisTick = 0u;  // 本 tick 同步创建的程序数
    uint32_t mNumTicksUntilNextSynchronousProgram = 0u;     // 直到下一个同步程序的 tick 数
    using PendingSynchronousProgram = std::tuple<program_token_t, Program>;
    std::vector<PendingSynchronousProgram> mPendingSynchronousPrograms;  // 待处理的同步程序

    /**
     * 初始化程序 Token
     * 
     * 确保 Token 已准备好，返回 GL 程序 ID。
     * Token 会被销毁。
     * 
     * @param token 程序 Token 引用（将被置为 nullptr）
     * @return GL 程序 ID
     */
    GLuint initialize(program_token_t& token);
    
    /**
     * 确保 Token 已准备好
     * 
     * 如果 Token 尚未准备好，会阻塞直到准备好。
     * 
     * @param token 程序 Token
     */
    void ensureTokenIsReady(program_token_t const& token);

    // THREAD_POOL 模式的方法
    /**
     * 处理线程池模式下已取消的 Token
     * 
     * 清理已取消 Token 的 GL 资源。
     */
    void handleCanceledTokensForThreadPool();

    // SYNCHRONOUS 和 ASYNCHRONOUS 模式的方法
    /**
     * 在下次 tick 时运行任务
     * 
     * @param priority 优先级
     * @param token 程序 Token
     * @param job 任务
     */
    void runAtNextTick(CompilerPriorityQueue priority, program_token_t const& token,
            Job job);
    
    /**
     * 执行 tick 任务
     * 
     * 执行所有待处理的任务。
     */
    void executeTickOps();
    
    /**
     * 取消 tick 任务
     * 
     * @param token 程序 Token
     * @return 如果找到并移除了任务返回 true
     */
    bool cancelTickOp(program_token_t const& token) noexcept;

    /**
     * 检查是否应该在本 tick 编译同步程序
     * 
     * @return 如果应该编译返回 true
     */
    bool shouldCompileSynchronousProgramThisTick() const noexcept;
    
    /**
     * 编译待处理的同步程序
     * 
     * 根据优先级和限制编译待处理的程序。
     */
    void compilePendingSynchronousPrograms();
    
    /**
     * 立即编译待处理的同步程序
     * 
     * @param token 程序 Token
     */
    void compilePendingSynchronousProgramNow(program_token_t const& token);
    
    /**
     * 取消待处理的同步程序
     * 
     * @param token 程序 Token
     */
    void cancelPendingSynchronousProgram(program_token_t const& token) noexcept;

    /**
     * 编译程序
     * 
     * 根据编译模式编译程序。
     * 
     * @param token 程序 Token
     * @param program 程序对象（将被移动）
     */
    void compileProgram(program_token_t const& token, Program&& program);

    /**
     * 编译着色器
     * 
     * 使用给定的 `shaderSource` 编译着色器。
     * `gl.shaders` 在此方法后总是填充有效的着色器 ID。
     * 但这不一定意味着着色器已成功编译。
     * 可以通过稍后调用 `checkCompileStatus` 来检查错误。
     * 
     * @param context OpenGLContext 引用
     * @param shadersSource 着色器源代码
     * @param specializationConstants 特化常量
     * @param multiview 是否使用多视图
     * @param token 程序 Token
     */
    static void compileShaders(OpenGLContext& context, Program::ShaderSource shadersSource,
            utils::FixedCapacityVector<Program::SpecializationConstant> const&
                    specializationConstants,
            bool multiview, program_token_t const& token);

    /**
     * 检查着色器编译是否完成
     * 
     * 当扩展 `KHR_parallel_shader_compile` 启用时，你可能想调用此方法。
     * 
     * @param token 程序 Token
     * @return 如果编译完成返回 true
     */
    static bool isCompileCompleted(program_token_t const& token) noexcept;

    /**
     * 检查着色器的编译状态并在失败时记录错误
     * 
     * @param token 程序 Token
     */
    static void checkCompileStatus(program_token_t const& token);

    /**
     * 通过链接已编译的着色器创建程序
     * 
     * `gl.program` 在此方法后总是填充有效的程序 ID。
     * 但这不一定意味着程序已成功链接。
     * 可以通过稍后调用 `checkLinkStatusAndCleanupShaders` 来检查错误。
     * 
     * @param context OpenGLContext 引用
     * @param token 程序 Token
     */
    static void linkProgram(OpenGLContext const& context, program_token_t const& token);

    /**
     * 检查程序链接是否完成
     * 
     * 当扩展 `KHR_parallel_shader_compile` 启用时，你可能想调用此方法。
     * 
     * @param token 程序 Token
     * @return 如果链接完成返回 true
     */
    static bool isLinkCompleted(program_token_t const& token) noexcept;

    /**
     * 检查程序的链接状态并在失败时记录错误
     * 
     * 返回链接结果。
     * 无论结果如何，都会清理着色器。
     * 
     * @param token 程序 Token
     * @return 如果链接成功返回 true
     */
    static bool checkLinkStatusAndCleanupShaders(program_token_t const& token) noexcept;

    /**
     * 尝试从缓存中检索程序
     * 
     * 如果从缓存加载，返回 `true`。
     * 
     * @param cache 程序二进制缓存
     * @param platform OpenGLPlatform 引用
     * @param program 程序对象
     * @param token 程序 Token
     * @return 如果从缓存加载返回 true
     */
    static bool tryRetrievingProgram(OpenGLBlobCache& cache, OpenGLPlatform& platform,
            Program const& program, program_token_t const& token) noexcept;

    /**
     * 尝试缓存程序（如果尚未缓存）
     * 
     * 仅在程序有效时缓存它。
     * 
     * @param cache 程序二进制缓存
     * @param platform OpenGLPlatform 引用
     * @param token 程序 Token
     */
    static void tryCachingProgram(OpenGLBlobCache& cache, OpenGLPlatform& platform,
            program_token_t const& token) noexcept;
};

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_OPENGL_SHADERCOMPILERSERVICE_H
