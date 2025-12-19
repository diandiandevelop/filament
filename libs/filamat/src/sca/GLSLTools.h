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

#ifndef TNT_STATICCODEANALYZER_H
#define TNT_STATICCODEANALYZER_H

#include <deque>
#include <list>
#include <optional>
#include <set>
#include <string>

#include <filamat/MaterialBuilder.h>

#include <ShaderLang.h>

class TIntermNode;

namespace glslang {
class TPoolAllocator;
}

namespace filamat {

// Used for symbol tracking during static code analysis.
// 用于静态代码分析期间的符号跟踪
struct Access {
    enum Type {
        Swizzling,        // Swizzle操作（如.xyz）
        DirectIndexForStruct,  // 结构体直接索引（如.member）
        FunctionCall      // 函数调用
    };
    Type type;            // 访问类型
    std::string string;   // 访问字符串（字段名、操作符名或函数名）
    size_t parameterIdx = 0; // Only used when type == FunctionCall;
                              // 仅在type == FunctionCall时使用（参数索引）
};

// Record of symbol interactions in a statement involving a symbol. Can track a sequence of up to
// (and in this order):
// Function call: foo(material)
// DirectIndexForStruct e.g: material.baseColor
// Swizzling e.g: material.baseColor.xyz
// Combinations are possible. e.g: foo(material.baseColor.xyz)
// 记录涉及符号的语句中的符号交互。可以跟踪以下顺序的序列：
// 函数调用：foo(material)
// DirectIndexForStruct 例如：material.baseColor
// Swizzling 例如：material.baseColor.xyz
// 可以组合。例如：foo(material.baseColor.xyz)
class Symbol {
public:
    // 默认构造函数
    Symbol() = default;
    // 使用名称构造符号
    explicit Symbol(const std::string& name) {
        mName = name;
    }

    // 获取符号名称
    std::string& getName() {
        return mName;
    }

    // 获取访问列表
    std::list<Access>& getAccesses() {
        return mAccesses;
    };

    // 设置符号名称
    void setName(const std::string& name) {
        mName = name;
    }

    // 添加访问操作（添加到列表前端）
    void add(const Access& access) {
        mAccesses.push_front(access);
    }

    // 将符号转换为字符串表示（名称.访问1.访问2...）
    std::string toString() const {
        std::string str(mName);
        for (const Access& access: mAccesses) {
            str += ".";
            str += access.string;
        }
        return str;
    }

    // 检查是否有结构体直接索引访问
    bool hasDirectIndexForStruct() const noexcept {
        return std::any_of(mAccesses.begin(), mAccesses.end(), [](auto&& access) {
            return access.type == Access::Type::DirectIndexForStruct;
        });
    }

    // 获取结构体直接索引访问的字段名称
    std::string getDirectIndexStructName() const noexcept {
        for (const Access& access : mAccesses) {
            if (access.type == Access::Type::DirectIndexForStruct) {
                return access.string;
            }
        }
        return "";
    }

private:
    std::list<Access> mAccesses;
    std::string mName;
};

// GLSLang清理器类：用于保存和恢复glslang线程池分配器
class GLSLangCleaner {
public:
    // 构造函数：保存当前线程池分配器
    GLSLangCleaner();
    // 析构函数：恢复线程池分配器
    ~GLSLangCleaner();

private:
    glslang::TPoolAllocator* mAllocator;  // 保存的分配器指针
};

// GLSL工具类：提供GLSL着色器分析和处理功能
class GLSLTools {
public:
    // 初始化GLSL工具（初始化glslang进程）
    static void init();
    // 关闭GLSL工具（清理glslang进程）
    static void shutdown();

    // 片段着色器信息结构体
    struct FragmentShaderInfo {
        bool userMaterialHasCustomDepth = false;  // 用户材质是否有自定义深度
    };

    // Return true if:
    // The shader is syntactically and semantically valid AND
    // The shader features a material() function AND
    // The shader features a prepareMaterial() function AND
    // prepareMaterial() is called at some point in material() call chain.
    // 如果以下条件都满足则返回FragmentShaderInfo：
    // 着色器在语法和语义上有效 AND
    // 着色器包含material()函数 AND
    // 着色器包含prepareMaterial()函数 AND
    // prepareMaterial()在material()调用链中的某个点被调用
    // @param shaderCode 着色器代码字符串
    // @param model 着色器模型
    // @param materialDomain 材质域
    // @param targetApi 目标API
    // @param targetLanguage 目标语言
    // @param hasCustomSurfaceShading 是否有自定义表面着色
    // @return 如果着色器有效返回FragmentShaderInfo，否则返回std::nullopt
    static std::optional<FragmentShaderInfo> analyzeFragmentShader(const std::string& shaderCode,
            filament::backend::ShaderModel model, MaterialBuilder::MaterialDomain materialDomain,
            MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
            bool hasCustomSurfaceShading) noexcept;

    // 分析顶点着色器（检查语法和语义，验证材质函数是否存在）
    // @param shaderCode 着色器代码字符串
    // @param model 着色器模型
    // @param materialDomain 材质域
    // @param targetApi 目标API
    // @param targetLanguage 目标语言
    // @return 如果着色器有效返回true，否则返回false
    static bool analyzeVertexShader(const std::string& shaderCode,
            filament::backend::ShaderModel model,
            MaterialBuilder::MaterialDomain materialDomain, MaterialBuilder::TargetApi targetApi,
            MaterialBuilder::TargetLanguage targetLanguage) noexcept;

    // 分析计算着色器（检查语法和语义，验证材质函数是否存在）
    // @param shaderCode 着色器代码字符串
    // @param model 着色器模型
    // @param targetApi 目标API
    // @param targetLanguage 目标语言
    // @return 如果着色器有效返回true，否则返回false
    static bool analyzeComputeShader(const std::string& shaderCode,
            filament::backend::ShaderModel model, MaterialBuilder::TargetApi targetApi,
            MaterialBuilder::TargetLanguage targetLanguage) noexcept;

        // Public for unit tests.
    using Property = MaterialBuilder::Property;
    using ShaderModel = filament::backend::ShaderModel;
    // Use static code analysis on the fragment shader AST to guess properties used in user provided
    // glgl code. Populate properties accordingly.
    // 使用静态代码分析在片段着色器AST上猜测用户提供的GLSL代码中使用的属性。相应地填充属性。
    // @param type 着色器阶段
    // @param shaderCode 着色器代码字符串
    // @param properties 输出参数，找到的属性列表
    // @param targetApi 目标API（默认OPENGL）
    // @param targetLanguage 目标语言（默认GLSL）
    // @param model 着色器模型（默认DESKTOP）
    // @return 如果成功找到属性返回true，否则返回false
    bool findProperties(
            filament::backend::ShaderStage type,
            const std::string& shaderCode,
            MaterialBuilder::PropertyList& properties,
            MaterialBuilder::TargetApi targetApi = MaterialBuilder::TargetApi::OPENGL,
            MaterialBuilder::TargetLanguage targetLanguage = MaterialBuilder::TargetLanguage::GLSL,
            ShaderModel model = ShaderModel::DESKTOP) const noexcept;

    // use 100 for ES environment, 110 for desktop; this is the GLSL version, not SPIR-V or Vulkan
    // this is intended to be used with glslang's parse() method, which will figure out the actual
    // version.
    // 获取GLSL默认版本（ES环境使用100，桌面使用110；这是GLSL版本，不是SPIR-V或Vulkan版本）
    // 这旨在与glslang的parse()方法一起使用，该方法将确定实际版本
    // @param model 着色器模型
    // @return GLSL版本号
    static int getGlslDefaultVersion(filament::backend::ShaderModel model);

    // The shading language version. Corresponds to #version $VALUE.
    // returns the version and a boolean (true for essl, false for glsl)
    // 获取着色语言版本（对应于#version $VALUE）
    // @param model 着色器模型
    // @param featureLevel 功能级别
    // @return 版本号和对的布尔值（true表示ES，false表示桌面）
    static std::pair<int, bool> getShadingLanguageVersion(
            filament::backend::ShaderModel model,
            filament::backend::FeatureLevel featureLevel);

    // 根据目标API和目标语言获取glslang消息标志
    // @param targetApi 目标API
    // @param targetLanguage 目标语言
    // @return glslang消息标志
    static EShMessages glslangFlagsFromTargetApi(MaterialBuilder::TargetApi targetApi,
            MaterialBuilder::TargetLanguage targetLanguage);

    // 准备着色器解析器（设置glslang着色器的环境配置）
    // @param targetApi 目标API
    // @param targetLanguage 目标语言
    // @param shader glslang着色器对象
    // @param stage 着色器阶段
    // @param version GLSL版本号
    static void prepareShaderParser(MaterialBuilder::TargetApi targetApi,
            MaterialBuilder::TargetLanguage targetLanguage, glslang::TShader& shader,
            EShLanguage stage, int version);

    // 为着色器添加纹理LOD偏置（重载版本，使用默认入口点和符号名）
    // @param shader glslang着色器对象
    static void textureLodBias(glslang::TShader& shader);

    // 检查着色器是否有自定义深度（检查是否调用discard或写入gl_FragDepth）
    // @param root AST根节点
    // @param entryPoint 入口点节点
    // @return 如果有自定义深度返回true，否则返回false
    static bool hasCustomDepth(TIntermNode* root, TIntermNode* entryPoint);


private:
    // Traverse a function definition and retrieve all symbol written to and all symbol passed down
    // in a function call.
    // Start in the function matching the signature provided and follow all out and inout calls.
    // Does NOT recurse to follow function calls.
    // 遍历函数定义并检索所有写入的符号和在函数调用中传递的所有符号。
    // 从匹配提供的签名的函数开始，跟踪所有out和inout调用。
    // 不递归跟踪函数调用。
    // @param functionSignature 函数签名（完全限定名）
    // @param root AST根节点
    // @param symbols 输出参数，找到的符号列表
    // @return 总是返回true
    static bool findSymbolsUsage(std::string_view functionSignature, TIntermNode& root,
            std::deque<Symbol>& symbols) noexcept;


    // Determine how a function affect one of its parameter by following all write and function call
    // operations on that parameter. e.g to follow material(in out  MaterialInputs), call
    // findPropertyWritesOperations("material", 0, ...);
    // Does nothing if the parameter is not marked as OUT or INOUT
    // 通过跟踪该参数上的所有写入和函数调用操作，确定函数如何影响其参数之一。
    // 例如，要跟踪material(in out MaterialInputs)，调用findPropertyWritesOperations("material", 0, ...);
    // 如果参数未标记为OUT或INOUT，则不执行任何操作
    // @param functionName 函数签名（完全限定名）
    // @param parameterIdx 参数索引
    // @param rootNode AST根节点
    // @param properties 输出参数，找到的属性列表
    // @return 如果成功返回true，否则返回false
    bool findPropertyWritesOperations(std::string_view functionName, size_t parameterIdx,
            TIntermNode* rootNode, MaterialBuilder::PropertyList& properties) const noexcept;

    // Look at a symbol access and find out if it affects filament MaterialInput fields. Will follow
    // function calls if necessary.
    // 查看符号访问并确定它是否影响filament MaterialInput字段。必要时将跟踪函数调用。
    // @param symbol 符号对象（包含访问链信息）
    // @param rootNode AST根节点
    // @param properties 输出参数，找到的属性列表
    void scanSymbolForProperty(Symbol& symbol, TIntermNode* rootNode,
            MaterialBuilder::PropertyList& properties) const noexcept;

    // add lod bias to texture() calls
    // 为着色器添加纹理LOD偏置（完整版本，修改AST以添加lodBias参数到texture调用）
    // @param intermediate glslang中间表示对象
    // @param root AST根节点
    // @param entryPointSignatureish 入口点函数签名（部分匹配）
    // @param lodBiasSymbolName lodBias符号名称
    static void textureLodBias(glslang::TIntermediate* intermediate, TIntermNode* root,
            const char* entryPointSignatureish, const char* lodBiasSymbolName) noexcept;
};

} // namespace filamat

#endif //TNT_STATICCODEANALYZER_H
