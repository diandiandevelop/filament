# Filament 着色器编译系统详细分析

## 目录
1. [概述](#概述)
2. [ShaderCompilerService 架构](#shadercompilerservice-架构)
3. [编译模式](#编译模式)
4. [编译流程](#编译流程)
5. [Blob 缓存](#blob-缓存)
6. [性能优化](#性能优化)

---

## 概述

Filament 的着色器编译系统负责将 GLSL 着色器代码编译和链接成 OpenGL 程序。系统支持同步和异步编译，以及程序缓存（Blob Cache）以加速后续加载。

### 核心组件

- **ShaderCompilerService**：着色器编译服务，管理编译流程
- **CompilerThreadPool**：编译线程池（异步模式）
- **OpenGLBlobCache**：程序二进制缓存
- **OpenGLProgramToken**：程序编译令牌，跟踪编译状态

---

## ShaderCompilerService 架构

### 1. 类结构

```cpp
class ShaderCompilerService {
public:
    using program_token_t = std::shared_ptr<OpenGLProgramToken>;
    
    // 创建程序（异步编译）
    program_token_t createProgram(utils::CString const& name, Program&& program);
    
    // 获取程序（阻塞直到编译完成）
    GLuint getProgram(program_token_t& token);
    
    // 每帧调用，处理编译状态
    void tick();
    
private:
    enum class Mode {
        UNDEFINED,      // 未初始化
        SYNCHRONOUS,    // 同步编译
        THREAD_POOL,    // 线程池异步编译（最常见）
        ASYNCHRONOUS    // KHR_parallel_shader_compile 异步编译
    };
    
    OpenGLDriver& mDriver;
    OpenGLBlobCache mBlobCache;              // 程序二进制缓存
    CallbackManager mCallbackManager;        // 回调管理器
    CompilerThreadPool mCompilerThreadPool; // 编译线程池
    
    Mode mMode = Mode::UNDEFINED;
};
```

### 2. 编译模式

**三种编译模式：**

1. **SYNCHRONOUS（同步）**：在主线程同步编译
2. **THREAD_POOL（线程池）**：在后台线程池异步编译（最常见）
3. **ASYNCHRONOUS（异步）**：使用 `KHR_parallel_shader_compile` 扩展异步编译

**模式选择：**

```cpp
void ShaderCompilerService::init() noexcept {
    if (isParallelShaderCompileSupported()) {
        // 检查是否支持 KHR_parallel_shader_compile
        mMode = Mode::ASYNCHRONOUS;
    } else if (mShaderCompilerThreadCount > 0) {
        // 使用线程池
        mMode = Mode::THREAD_POOL;
        mCompilerThreadPool.init(mShaderCompilerThreadCount);
    } else {
        // 同步编译
        mMode = Mode::SYNCHRONOUS;
    }
}
```

---

## 编译流程

### 1. 创建程序

**`createProgram()` 流程：**

```cpp
program_token_t ShaderCompilerService::createProgram(
        utils::CString const& name, 
        Program&& program) {
    
    // 1. 创建程序令牌
    auto token = std::make_shared<OpenGLProgramToken>(...);
    token->state = OpenGLProgramToken::State::WAITING;
    
    // 2. 开始编译
    compileProgram(token, std::move(program));
    
    return token;
}
```

### 2. 编译程序

**`compileProgram()` 根据模式选择编译方式：**

#### THREAD_POOL 模式

```cpp
case Mode::THREAD_POOL: {
    // 将编译任务加入线程池队列
    mCompilerThreadPool.queue(priorityQueue, token,
            [this, &gl, program = std::move(program), token]() mutable {
                // 1. 检查令牌状态（WAITING 或 CANCELED）
                OpenGLProgramToken::State tokenState = WAITING;
                if (!token->state.compare_exchange_strong(tokenState, LOADING)) {
                    // 已取消，退出
                    return;
                }
                
                // 2. 尝试从缓存加载
                if (!tryRetrievingProgram(mBlobCache, mDriver.mPlatform, program, token)) {
                    // 3. 编译着色器
                    compileShaders(gl, program.getShadersSource(), ...);
                    
                    // 4. 链接程序
                    linkProgram(gl, token);
                    
                    // 5. 检查链接状态
                    bool linked = checkLinkStatusAndCleanupShaders(token);
                }
                
                // 6. 通知完成
                token->signal();
                
                // 7. 尝试缓存程序
                tryCachingProgram(mBlobCache, mDriver.mPlatform, token);
                
                // 8. 更新状态
                token->state.store(COMPLETED);
            });
    break;
}
```

#### SYNCHRONOUS/ASYNCHRONOUS 模式

```cpp
case Mode::SYNCHRONOUS:
case Mode::ASYNCHRONOUS: {
    // 1. 尝试从缓存加载
    if (!tryRetrievingProgram(mBlobCache, mDriver.mPlatform, program, token)) {
        // 2. 编译着色器
        compileShaders(gl, program.getShadersSource(), ...);
        
        // 3. 在下一帧检查链接状态
        runAtNextTick(priorityQueue, token, [this, token](Job const&) {
            if (mMode == Mode::ASYNCHRONOUS) {
                // 检查链接是否完成
                if (token->gl.program) {
                    return isLinkCompleted(token);
                }
                // 检查编译是否完成
                if (!isCompileCompleted(token)) {
                    return false;
                }
            }
            // 链接程序
            if (!token->gl.program) {
                linkProgram(mDriver.getContext(), token);
                if (mMode == Mode::ASYNCHRONOUS) {
                    return false; // 等待链接完成
                }
            }
            return true;
        });
    }
    break;
}
```

### 3. 编译着色器

**`compileShaders()` 实现：**

```cpp
static void ShaderCompilerService::compileShaders(
        OpenGLContext& context,
        Program::ShaderSource shadersSource,
        FixedCapacityVector<SpecializationConstant> const& specializationConstants,
        bool multiview,
        program_token_t const& token) {
    
    // 1. 构建特化常量字符串
    CString specializationConstantString;
    for (size_t id = 0; id < specializationConstants.size(); id++) {
        appendSpecConstantString(specializationConstantString, id, 
                                specializationConstants[id]);
    }
    
    // 2. 编译每个着色器阶段
    for (size_t i = 0; i < Program::SHADER_TYPE_COUNT; i++) {
        const ShaderStage stage = static_cast<ShaderStage>(i);
        const std::string& shader = shadersSource[i];
        
        if (shader.empty()) continue;
        
        // 3. 构建着色器源码（版本 + 前导码 + 特化常量 + 打包函数 + 主体）
        std::array<std::string_view, 5> sources = {
            version, prolog,
            { specializationConstantString.data(), ... },
            packingFunctions,
            { body.data(), body.size() - 1 }
        };
        
        // 4. 创建着色器对象
        GLuint shaderId = glCreateShader(glShaderType);
        glShaderSource(shaderId, count, shaderStrings.data(), lengths.data());
        glCompileShader(shaderId);
        
        // 5. 保存着色器 ID
        token->gl.shaders[i] = shaderId;
    }
}
```

### 4. 链接程序

**`linkProgram()` 实现：**

```cpp
static void ShaderCompilerService::linkProgram(
        OpenGLContext const& context,
        program_token_t const& token) {
    
    // 1. 创建程序对象
    GLuint program = glCreateProgram();
    
    // 2. 附加所有着色器
    for (size_t i = 0; i < Program::SHADER_TYPE_COUNT; i++) {
        if (token->gl.shaders[i]) {
            glAttachShader(program, token->gl.shaders[i]);
        }
    }
    
    // 3. 链接程序
    glLinkProgram(program);
    
    // 4. 保存程序 ID
    token->gl.program = program;
}
```

### 5. 获取程序

**`getProgram()` 阻塞直到编译完成：**

```cpp
GLuint ShaderCompilerService::getProgram(program_token_t& token) {
    // 初始化（等待编译完成）
    GLuint program = initialize(token);
    
    // 令牌被销毁，变为无效
    assert_invariant(token == nullptr);
    
    return program;
}
```

**`initialize()` 等待令牌就绪：**

```cpp
GLuint ShaderCompilerService::initialize(program_token_t& token) {
    // 确保令牌就绪（阻塞直到编译完成）
    ensureTokenIsReady(token);
    
    GLuint program = token->gl.program;
    
    if (mMode != Mode::THREAD_POOL) {
        // 检查链接状态
        bool linked = checkLinkStatusAndCleanupShaders(token);
        // 尝试缓存程序
        tryCachingProgram(mBlobCache, mDriver.mPlatform, token);
    }
    
    token = nullptr;
    return program;
}
```

---

## Blob 缓存

### 1. 缓存机制

**程序二进制缓存（Blob Cache）：**

- **目的**：避免重复编译相同的着色器程序
- **存储**：程序二进制数据（平台特定格式）
- **键**：程序源码 + 特化常量的哈希值

### 2. 缓存加载

**`tryRetrievingProgram()` 实现：**

```cpp
static bool ShaderCompilerService::tryRetrievingProgram(
        OpenGLBlobCache& cache,
        OpenGLPlatform& platform,
        Program const& program,
        program_token_t const& token) noexcept {
    
    // 1. 计算程序哈希
    uint64_t key = computeProgramHash(program);
    
    // 2. 从缓存加载
    BlobCacheEntry entry;
    if (cache.get(key, entry)) {
        // 3. 使用程序二进制创建程序
        GLuint programId = platform.loadProgramBinary(entry.data, entry.size);
        if (programId) {
            token->gl.program = programId;
            return true; // 缓存命中
        }
    }
    
    return false; // 缓存未命中，需要编译
}
```

### 3. 缓存存储

**`tryCachingProgram()` 实现：**

```cpp
static void ShaderCompilerService::tryCachingProgram(
        OpenGLBlobCache& cache,
        OpenGLPlatform& platform,
        program_token_t const& token) noexcept {
    
    if (!token->gl.program) return;
    
    // 1. 获取程序二进制
    ProgramBinary binary;
    if (platform.getProgramBinary(token->gl.program, binary)) {
        // 2. 计算程序哈希
        uint64_t key = computeProgramHash(token->program);
        
        // 3. 存储到缓存
        cache.put(key, binary.data, binary.size);
    }
}
```

---

## 性能优化

### 1. 异步编译

**线程池模式：**

- 编译在后台线程进行，不阻塞主线程
- 使用优先级队列管理编译任务
- 支持取消正在编译的程序

### 2. Blob 缓存

**缓存优势：**

- 避免重复编译相同的程序
- 显著减少首次加载时间
- 跨会话持久化（如果平台支持）

### 3. 特化常量

**编译时优化：**

```cpp
// 特化常量在编译时内联，生成优化的代码
#define SPIRV_CROSS_CONSTANT_ID_0 1.0
#define SPIRV_CROSS_CONSTANT_ID_1 2.0
```

### 4. 批量编译

**同步模式下的批量处理：**

```cpp
void ShaderCompilerService::compilePendingSynchronousPrograms() {
    // 每帧只编译有限数量的程序，避免卡顿
    if (shouldCompileSynchronousProgramThisTick()) {
        compilePendingSynchronousProgramNow(token);
    }
}
```

---

## 总结

Filament 的着色器编译系统通过以下设计实现了高效的着色器编译：

1. **多模式支持**：同步、线程池、异步三种模式
2. **Blob 缓存**：避免重复编译
3. **异步编译**：不阻塞主线程
4. **优先级管理**：重要程序优先编译
5. **取消机制**：支持取消不需要的程序

这些设计使得 Filament 能够在保持灵活性的同时，实现高效的着色器编译性能。

