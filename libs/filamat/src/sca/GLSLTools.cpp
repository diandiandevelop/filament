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

#include "GLSLTools.h"

#include <filamat/Enums.h>
#include <filament/MaterialEnums.h>
#include <filamat/MaterialBuilder.h>

#include <utils/Log.h>

#include "ASTHelpers.h"

// GLSLANG headers
#include <InfoSink.h>
#include <localintermediate.h>

#include "builtinResource.h"

using namespace utils;
using namespace glslang;
using namespace filament::backend;

namespace filamat {

// GLSLang清理器构造函数：保存当前线程池分配器
GLSLangCleaner::GLSLangCleaner() {
    mAllocator = &GetThreadPoolAllocator();
}

// GLSLang清理器析构函数：恢复线程池分配器
GLSLangCleaner::~GLSLangCleaner() {
    GetThreadPoolAllocator().pop();
    SetThreadPoolAllocator(mAllocator);
}

// ------------------------------------------------------------------------------------------------

// 根据材质域获取材质函数名称
// @param domain 材质域枚举值
// @return 材质函数名称字符串视图
static std::string_view getMaterialFunctionName(MaterialBuilder::MaterialDomain domain) noexcept {
    // 根据材质域返回对应的函数名称
    switch (domain) {
        case MaterialBuilder::MaterialDomain::SURFACE:
            return "material";        // 表面材质函数
        case MaterialBuilder::MaterialDomain::POST_PROCESS:
            return "postProcess";    // 后处理材质函数
        case MaterialBuilder::MaterialDomain::COMPUTE:
            return "compute";        // 计算材质函数
    }
};

// ------------------------------------------------------------------------------------------------

static const TIntermTyped* findLValueBase(const TIntermTyped* node, Symbol& symbol);

// 符号跟踪器类：用于遍历AST收集符号访问信息
class SymbolsTracer : public TIntermTraverser {
public:
    // 构造函数：初始化符号事件队列
    explicit SymbolsTracer(std::deque<Symbol>& events) : mEvents(events) {
    }

    // Function call site.
    // 访问函数调用节点
    bool visitAggregate(TVisit, TIntermAggregate* node) override {
        // 只处理函数调用操作
        if (node->getOp() != EOpFunctionCall) {
            return true;
        }

        // Find function name.
        // 查找函数名称
        std::string const functionName = node->getName().c_str();

        // Iterate on function parameters.
        // 遍历函数参数
        for (size_t parameterIdx = 0; parameterIdx < node->getSequence().size(); parameterIdx++) {
            TIntermNode* parameter = node->getSequence().at(parameterIdx);
            // Parameter is not a pure symbol. It is indexed or swizzled.
            // 参数不是纯符号。它是索引或swizzle的。
            if (parameter->getAsBinaryNode()) {
                Symbol symbol;
                std::vector<Symbol> events;
                // 查找左值基符号
                const TIntermTyped* n = findLValueBase(parameter->getAsBinaryNode(), symbol);
                if (n != nullptr && n->getAsSymbolNode() != nullptr) {
                    const TString& symbolTString = n->getAsSymbolNode()->getName();
                    symbol.setName(symbolTString.c_str());
                    events.push_back(symbol);
                }

                // 为每个符号添加函数调用访问信息
                for (Symbol symbol : events) {
                    Access const fCall = { Access::FunctionCall, functionName, parameterIdx };
                    symbol.add(fCall);
                    mEvents.push_back(symbol);
                }
            }
            // Parameter is a pure symbol.
            // 参数是纯符号
            if (parameter->getAsSymbolNode()) {
                Symbol s(parameter->getAsSymbolNode()->getName().c_str());
                Access const fCall = {Access::FunctionCall, functionName, parameterIdx};
                s.add(fCall);
                mEvents.push_back(s);
            }
        }

        return true;
    }

    // Assign operations
    // 访问赋值操作节点
    bool visitBinary(TVisit, TIntermBinary* node) override {
        TOperator const op = node->getOp();
        Symbol symbol;
        // 检查各种赋值操作（=, +=, /=, -=, *=）
        if (op == EOpAssign || op == EOpAddAssign || op == EOpDivAssign || op == EOpSubAssign
            || op == EOpMulAssign ) {
            // 查找左值的基符号
            const TIntermTyped* n = findLValueBase(node->getLeft(), symbol);
            if (n != nullptr && n->getAsSymbolNode() != nullptr) {
                const TString& symbolTString = n->getAsSymbolNode()->getName();
                symbol.setName(symbolTString.c_str());
                mEvents.push_back(symbol);
                return false; // Don't visit subtree since we just traced it with findLValueBase()
                // 不要访问子树，因为我们刚刚用findLValueBase()跟踪了它
            }
        }
        return true;
    }

private:
    std::deque<Symbol>& mEvents;
};

// Meant to explore the Lvalue in an assignment. Depth traverse the left child of an assignment
// binary node to find out the symbol and all access applied on it.
// 查找赋值中的左值基符号。深度遍历赋值二元节点的左子节点，找出符号及其上的所有访问操作
// @param node 类型化节点（通常是赋值操作的左值）
// @param symbol 输出参数，符号对象（将填充访问链信息）
// @return 基符号节点，如果未找到返回nullptr
static const TIntermTyped* findLValueBase(const TIntermTyped* node, Symbol& symbol) {
    do {
        // Make sure we have a binary node
        // 确保我们有一个二元节点
        const TIntermBinary* binary = node->getAsBinaryNode();
        if (binary == nullptr) {
            // 如果不是二元节点，说明已经到达基符号
            return node;
        }

        // Check Operator
        // 检查操作符（只处理索引和swizzle操作）
        TOperator const op = binary->getOp();
        if (op != EOpIndexDirect && op != EOpIndexIndirect && op != EOpIndexDirectStruct
            && op != EOpVectorSwizzle && op != EOpMatrixSwizzle) {
            // 不是索引或swizzle操作，返回nullptr
            return nullptr;
        }
        // 创建访问信息并添加到符号
        Access access;
        if (op == EOpIndexDirectStruct) {
            // 结构体直接索引：获取字段名称
            access.string = ASTHelpers::getIndexDirectStructString(*binary);
            access.type = Access::DirectIndexForStruct;
        } else {
            // 其他操作（索引、swizzle）：转换为字符串
            access.string = ASTHelpers::to_string(op) ;
            access.type = Access::Swizzling;
        }
        symbol.add(access);
        // 继续向左遍历
        node = node->getAsBinaryNode()->getLeft();
    } while (true);
}

// ------------------------------------------------------------------------------------------------

// 分析计算着色器（检查语法和语义，验证材质函数是否存在）
// @param shaderCode 着色器代码字符串
// @param model 着色器模型
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @return 如果着色器有效返回true，否则返回false
bool GLSLTools::analyzeComputeShader(const std::string& shaderCode,
        filament::backend::ShaderModel model, MaterialBuilder::TargetApi targetApi,
        MaterialBuilder::TargetLanguage targetLanguage) noexcept {

    // Parse to check syntax and semantic.
    // 步骤1：解析着色器代码以检查语法和语义
    const char* shaderCString = shaderCode.c_str();

    // 步骤2：创建计算着色器对象并设置源代码
    TShader tShader(EShLanguage::EShLangCompute);
    tShader.setStrings(&shaderCString, 1);

    // 步骤3：初始化GLSLang清理器（管理线程池分配器）
    GLSLangCleaner const cleaner;
    // 步骤4：获取GLSL版本和glslang标志
    const int version = getGlslDefaultVersion(model);
    EShMessages const msg = glslangFlagsFromTargetApi(targetApi, targetLanguage);
    // 步骤5：解析着色器
    bool const ok = tShader.parse(&DefaultTBuiltInResource, version, false, msg);
    if (!ok) {
        utils::slog.e << "ERROR: Unable to parse compute shader:" << utils::io::endl;
        utils::slog.e << tShader.getInfoLog() << utils::io::endl;
        return false;
    }

    // 步骤6：获取材质函数名称（compute）
    auto materialFunctionName = getMaterialFunctionName(filament::MaterialDomain::COMPUTE);

    // 步骤7：获取AST根节点并检查是否存在材质函数定义
    TIntermNode* root = tShader.getIntermediate()->getTreeRoot();
    // Check there is a material function definition in this shader.
    // 检查着色器中是否有材质函数定义
    TIntermNode* materialFctNode = ASTHelpers::getFunctionByNameOnly(materialFunctionName, *root);
    if (materialFctNode == nullptr) {
        utils::slog.e << "ERROR: Invalid compute shader:" << utils::io::endl;
        utils::slog.e << "ERROR: Unable to find " << materialFunctionName << "() function" << utils::io::endl;
        return false;
    }

    return true;
}

// 分析片段着色器（检查语法和语义，提取片段着色器信息）
// @param shaderCode 着色器代码字符串
// @param model 着色器模型
// @param materialDomain 材质域
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @param hasCustomSurfaceShading 是否有自定义表面着色
// @return 如果着色器有效返回FragmentShaderInfo，否则返回std::nullopt
std::optional<GLSLTools::FragmentShaderInfo> GLSLTools::analyzeFragmentShader(
        const std::string& shaderCode,
        filament::backend::ShaderModel model, MaterialBuilder::MaterialDomain materialDomain,
        MaterialBuilder::TargetApi targetApi, MaterialBuilder::TargetLanguage targetLanguage,
        bool hasCustomSurfaceShading) noexcept {

    assert_invariant(materialDomain != MaterialBuilder::MaterialDomain::COMPUTE);

    // Parse to check syntax and semantic.
    // 步骤1：解析着色器代码以检查语法和语义
    const char* shaderCString = shaderCode.c_str();

    // 步骤2：创建片段着色器对象并设置源代码
    TShader tShader(EShLanguage::EShLangFragment);
    tShader.setStrings(&shaderCString, 1);

    // 步骤3：初始化GLSLang清理器（管理线程池分配器）
    GLSLangCleaner const cleaner;
    // 步骤4：获取GLSL版本和glslang标志
    const int version = getGlslDefaultVersion(model);
    EShMessages const msg = glslangFlagsFromTargetApi(targetApi, targetLanguage);
    // 步骤5：解析着色器
    bool const ok = tShader.parse(&DefaultTBuiltInResource, version, false, msg);
    if (!ok) {
        utils::slog.e << "ERROR: Unable to parse fragment shader:" << utils::io::endl;
        utils::slog.e << tShader.getInfoLog() << utils::io::endl;
        return std::nullopt;
    }

    // 步骤6：获取材质函数名称（根据材质域）
    auto materialFunctionName = getMaterialFunctionName(materialDomain);

    // 步骤7：获取AST根节点并检查是否存在材质函数定义
    TIntermNode* root = tShader.getIntermediate()->getTreeRoot();
    // Check there is a material function definition in this shader.
    // 检查着色器中是否有材质函数定义
    TIntermNode* materialFctNode = ASTHelpers::getFunctionByNameOnly(materialFunctionName, *root);
    if (materialFctNode == nullptr) {
        utils::slog.e << "ERROR: Invalid fragment shader:" << utils::io::endl;
        utils::slog.e << "ERROR: Unable to find " << materialFunctionName << "() function" << utils::io::endl;
        return std::nullopt;
    }

    // 步骤8：检查是否有自定义深度（discard或gl_FragDepth写入）
    FragmentShaderInfo result {
        .userMaterialHasCustomDepth = GLSLTools::hasCustomDepth(root, materialFctNode)
    };

    // If this is a post-process material, at this point we've successfully met all the
    // requirements.
    // 如果是后处理材质，此时已满足所有要求
    if (materialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
        return result;
    }

    // 步骤9：检查是否存在prepareMaterial函数定义
    // Check there is a prepareMaterial function definition in this shader.
    TIntermAggregate* prepareMaterialNode =
            ASTHelpers::getFunctionByNameOnly("prepareMaterial", *root);
    if (prepareMaterialNode == nullptr) {
        utils::slog.e << "ERROR: Invalid fragment shader:" << utils::io::endl;
        utils::slog.e << "ERROR: Unable to find prepareMaterial() function" << utils::io::endl;
        return std::nullopt;
    }

    // 步骤10：检查prepareMaterial是否在材质函数中被调用
    std::string_view const prepareMaterialSignature = prepareMaterialNode->getName();
    bool const prepareMaterialCalled = ASTHelpers::isFunctionCalled(
            prepareMaterialSignature, *materialFctNode, *root);
    if (!prepareMaterialCalled) {
        utils::slog.e << "ERROR: Invalid fragment shader:" << utils::io::endl;
        utils::slog.e << "ERROR: prepareMaterial() is not called" << utils::io::endl;
        return std::nullopt;
    }

    // 步骤11：如果有自定义表面着色，检查是否存在surfaceShading函数
    if (hasCustomSurfaceShading) {
        materialFctNode = ASTHelpers::getFunctionByNameOnly("surfaceShading", *root);
        if (materialFctNode == nullptr) {
            utils::slog.e << "ERROR: Invalid fragment shader:" << utils::io::endl;
            utils::slog.e << "ERROR: Unable to find surfaceShading() function"
                          << utils::io::endl;
            return std::nullopt;
        }
    }

    return result;
}

// 分析顶点着色器（检查语法和语义，验证材质函数是否存在）
// @param shaderCode 着色器代码字符串
// @param model 着色器模型
// @param materialDomain 材质域
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @return 如果着色器有效返回true，否则返回false
bool GLSLTools::analyzeVertexShader(const std::string& shaderCode,
        filament::backend::ShaderModel model,
        MaterialBuilder::MaterialDomain materialDomain, MaterialBuilder::TargetApi targetApi,
        MaterialBuilder::TargetLanguage targetLanguage) noexcept {

    assert_invariant(materialDomain != MaterialBuilder::MaterialDomain::COMPUTE);

    // TODO: After implementing post-process vertex shaders, properly analyze them here.
    // TODO: 实现后处理顶点着色器后，在这里正确分析它们
    if (materialDomain == MaterialBuilder::MaterialDomain::POST_PROCESS) {
        return true;
    }

    // Parse to check syntax and semantic.
    // 步骤1：解析着色器代码以检查语法和语义
    const char* shaderCString = shaderCode.c_str();

    // 步骤2：创建顶点着色器对象并设置源代码
    TShader tShader(EShLanguage::EShLangVertex);
    tShader.setStrings(&shaderCString, 1);

    // 步骤3：初始化GLSLang清理器（管理线程池分配器）
    GLSLangCleaner const cleaner;
    // 步骤4：获取GLSL版本和glslang标志
    const int version = getGlslDefaultVersion(model);
    EShMessages const msg = glslangFlagsFromTargetApi(targetApi, targetLanguage);
    // 步骤5：解析着色器
    bool const ok = tShader.parse(&DefaultTBuiltInResource, version, false, msg);
    if (!ok) {
        utils::slog.e << "ERROR: Unable to parse vertex shader" << utils::io::endl;
        utils::slog.e << tShader.getInfoLog() << utils::io::endl;
        return false;
    }

    // 步骤6：获取AST根节点并检查是否存在materialVertex函数定义
    TIntermNode* root = tShader.getIntermediate()->getTreeRoot();
    // Check there is a material function definition in this shader.
    // 检查着色器中是否有材质函数定义
    TIntermNode* materialFctNode = ASTHelpers::getFunctionByNameOnly("materialVertex", *root);
    if (materialFctNode == nullptr) {
        utils::slog.e << "ERROR: Invalid vertex shader" << utils::io::endl;
        utils::slog.e << "ERROR: Unable to find materialVertex() function" << utils::io::endl;
        return false;
    }

    return true;
}

// 初始化GLSL工具（初始化glslang进程）
// @note 每次调用InitializeProcess必须与FinalizeProcess的调用匹配
void GLSLTools::init() {
    // Each call to InitializeProcess must be matched with a call to FinalizeProcess.
    InitializeProcess();
}

// 关闭GLSL工具（清理glslang进程）
void GLSLTools::shutdown() {
    FinalizeProcess();
}

// 查找着色器中的材质属性（通过分析AST查找属性写入操作）
// @param type 着色器阶段
// @param shaderCode 着色器代码字符串
// @param properties 输出参数，找到的属性列表
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @param model 着色器模型
// @return 如果成功找到属性返回true，否则返回false
bool GLSLTools::findProperties(
        filament::backend::ShaderStage type,
        const std::string& shaderCode,
        MaterialBuilder::PropertyList& properties,
        MaterialBuilder::TargetApi targetApi,
        MaterialBuilder::TargetLanguage targetLanguage,
        ShaderModel model) const noexcept {
    // 步骤1：准备着色器代码字符串
    const char* shaderCString = shaderCode.c_str();

    // 步骤2：定义lambda函数，将ShaderStage转换为EShLanguage
    auto getShaderStage = [](ShaderStage type) {
        switch (type) {
            case ShaderStage::VERTEX:   return EShLanguage::EShLangVertex;
            case ShaderStage::FRAGMENT: return EShLanguage::EShLangFragment;
            case ShaderStage::COMPUTE:  return EShLanguage::EShLangCompute;
        }
    };

    // 步骤3：创建着色器对象并设置源代码
    TShader tShader(getShaderStage(type));
    tShader.setStrings(&shaderCString, 1);

    // 步骤4：初始化GLSLang清理器（管理线程池分配器）
    GLSLangCleaner const cleaner;
    // 步骤5：获取GLSL版本和glslang标志
    const int version = getGlslDefaultVersion(model);
    EShMessages const msg = glslangFlagsFromTargetApi(targetApi, targetLanguage);
    const TBuiltInResource* builtins = &DefaultTBuiltInResource;
    // 步骤6：解析着色器
    bool const ok = tShader.parse(builtins, version, false, msg);
    if (!ok) {
        // Even with all properties set the shader doesn't build. This is likely a syntax error
        // with user provided code.
        // 即使设置了所有属性，着色器也无法构建。这可能是用户提供的代码的语法错误。
        utils::slog.e << tShader.getInfoLog() << utils::io::endl;
        return false;
    }

    // 步骤7：获取AST根节点
    TIntermNode* rootNode = tShader.getIntermediate()->getTreeRoot();

    // 步骤8：根据着色器阶段确定主函数名称（片段着色器使用"material"，顶点着色器使用"materialVertex"）
    std::string_view const mainFunction(type == ShaderStage::FRAGMENT ?
            "material" : "materialVertex");

    // 步骤9：查找材质函数定义并获取其完全限定名
    TIntermAggregate* functionMaterialDef = ASTHelpers::getFunctionByNameOnly(mainFunction, *rootNode);
    std::string_view const materialFullyQualifiedName = functionMaterialDef->getName();
    // 步骤10：查找属性写入操作（从材质函数的第一个参数开始）
    return findPropertyWritesOperations(materialFullyQualifiedName, 0, rootNode, properties);
}

// 查找函数中属性写入操作（递归查找函数调用链中的属性写入）
// @param functionName 函数签名（完全限定名）
// @param parameterIdx 参数索引
// @param rootNode AST根节点
// @param properties 输出参数，找到的属性列表
// @return 如果成功返回true，否则返回false
bool GLSLTools::findPropertyWritesOperations(std::string_view functionName, size_t parameterIdx,
        TIntermNode* rootNode, MaterialBuilder::PropertyList& properties) const noexcept {

    // 根据函数签名查找函数定义
    glslang::TIntermAggregate* functionMaterialDef =
            ASTHelpers::getFunctionBySignature(functionName, *rootNode);
    if (functionMaterialDef == nullptr) {
        utils::slog.e << "Unable to find function '" << functionName << "' definition."
                << utils::io::endl;
        return false;
    }

    // 获取函数参数列表
    std::vector<ASTHelpers::FunctionParameter> functionMaterialParameters;
    ASTHelpers::getFunctionParameters(functionMaterialDef, functionMaterialParameters);

    // 检查参数索引是否有效
    if (functionMaterialParameters.size() <= parameterIdx) {
        utils::slog.e << "Unable to find function '" << functionName <<  "' parameterIndex: " <<
                parameterIdx << utils::io::endl;
        return false;
    }

    // The function has no instructions, it cannot write properties, let's skip all the work
    // 函数没有指令，无法写入属性，跳过所有工作
    if (functionMaterialDef->getSequence().size() < 2) {
        return true;
    }

    // Make sure the parameter is either out or inout. Othwerise (const or in), there is no point
    // tracing its usage.
    // 确保参数是out或inout。否则（const或in），跟踪其使用没有意义。
    ASTHelpers::FunctionParameter::Qualifier const qualifier =
            functionMaterialParameters.at(parameterIdx).qualifier;
    if (qualifier == ASTHelpers::FunctionParameter::IN ||
        qualifier == ASTHelpers::FunctionParameter::CONST) {
        return true;
    }

    std::deque<Symbol> symbols;
    findSymbolsUsage(functionName, *rootNode, symbols);

    // Iterate over symbols to see if the parameter we are interested in what written.
    std::string const parameterName = functionMaterialParameters.at(parameterIdx).name;
    for (Symbol symbol: symbols) {
        // This is not the symbol we are interested in.
        if (symbol.getName() != parameterName) {
            continue;
        }

        // This is a direct assignment of the variable. X =
        if (symbol.getAccesses().empty()) {
            continue;
        }

        scanSymbolForProperty(symbol, rootNode, properties);
    }
    return true;
}

// 扫描符号以查找属性（分析符号的访问链以确定写入的属性）
// @param symbol 符号对象（包含访问链信息）
// @param rootNode AST根节点
// @param properties 输出参数，找到的属性列表
void GLSLTools::scanSymbolForProperty(Symbol& symbol,
        TIntermNode* rootNode,
        MaterialBuilder::PropertyList& properties) const noexcept {
    // 遍历符号的所有访问操作
    for (const Access& access : symbol.getAccesses()) {
        if (access.type == Access::Type::FunctionCall) {
            // Do NOT look into prepareMaterial call.
            // 不要查看prepareMaterial调用
            if (access.string.find("prepareMaterial(struct") != std::string::npos) {
                continue;
            }
            // If the full symbol is passed, we need to look inside the function to known
            // how it is used. Otherwise, if a DirectIndexForStruct is passed, we can just check
            // if the parameter is out or inout.
            // 如果传递了完整符号，我们需要查看函数内部以了解如何使用它。
            // 否则，如果传递了DirectIndexForStruct，我们可以只检查参数是out还是inout。
            if (symbol.hasDirectIndexForStruct()) {
                TIntermAggregate* functionCall =
                        ASTHelpers::getFunctionBySignature(access.string, *rootNode);
                std::vector<ASTHelpers::FunctionParameter> functionCallParameters;
                ASTHelpers::getFunctionParameters(functionCall, functionCallParameters);

                ASTHelpers::FunctionParameter const& parameter =
                        functionCallParameters.at(access.parameterIdx);
                if (parameter.qualifier == ASTHelpers::FunctionParameter::OUT ||
                    parameter.qualifier == ASTHelpers::FunctionParameter::INOUT) {
                    const std::string& propName = symbol.getDirectIndexStructName();
                    if (Enums::isValid<Property>(propName)) {
                        MaterialBuilder::Property const p = Enums::toEnum<Property>(propName);
                        properties[size_t(p)] = true;
                    }
                }
            } else {
                findPropertyWritesOperations(access.string, access.parameterIdx, rootNode,
                        properties);
            }
            return;
        }

        // If DirectIndexForStruct, issue the appropriate setProperty.
        // 如果是DirectIndexForStruct，设置相应的属性
        if (access.type == Access::Type::DirectIndexForStruct) {
            if (Enums::isValid<Property>(access.string)) {
                MaterialBuilder::Property const p = Enums::toEnum<Property>(access.string);
                properties[size_t(p)] = true;
            }
            return;
        }

        // Swizzling only happens at the end of the access chain and is ignored.
        // Swizzling只发生在访问链的末尾，被忽略
    }
}

// 查找函数中符号的使用情况（通过遍历AST收集符号访问信息）
// @param functionSignature 函数签名（完全限定名）
// @param root AST根节点
// @param symbols 输出参数，找到的符号列表
// @return 总是返回true
bool GLSLTools::findSymbolsUsage(std::string_view functionSignature, TIntermNode& root,
        std::deque<Symbol>& symbols) noexcept {
    // 根据函数签名查找函数AST节点
    TIntermNode* functionAST = ASTHelpers::getFunctionBySignature(functionSignature, root);
    // 使用SymbolsTracer遍历器收集符号信息
    SymbolsTracer variableTracer(symbols);
    functionAST->traverse(&variableTracer);
    return true;
}

// use 100 for ES environment, 110 for desktop; this is the GLSL version, not SPIR-V or Vulkan
// this is intended to be used with glslang's parse() method, which will figure out the actual
// version.
// 获取GLSL默认版本（用于ES环境使用100，桌面使用110；这是GLSL版本，不是SPIR-V或Vulkan版本）
// 这旨在与glslang的parse()方法一起使用，该方法将确定实际版本
// @param model 着色器模型
// @return GLSL版本号
int GLSLTools::getGlslDefaultVersion(ShaderModel model) {
        switch (model) {
        case ShaderModel::MOBILE:
            return 100;  // ES环境使用100
        case ShaderModel::DESKTOP:
            return 110;  // 桌面使用110
    }
}

// The shading language version. Corresponds to #version $VALUE.
// 获取着色语言版本（对应于#version $VALUE）
// @param model 着色器模型
// @param featureLevel 功能级别
// @return 版本号和对的布尔值（true表示ES，false表示桌面）
std::pair<int, bool> GLSLTools::getShadingLanguageVersion(ShaderModel model,
        filament::backend::FeatureLevel featureLevel) {
    using FeatureLevel = filament::backend::FeatureLevel;
    switch (model) {
        case ShaderModel::MOBILE:
            // 移动端：根据功能级别返回不同的ES版本
            switch (featureLevel) {
                case FeatureLevel::FEATURE_LEVEL_0:     return { 100, true };  // ES 1.0
                case FeatureLevel::FEATURE_LEVEL_1:     return { 300, true };  // ES 3.0
                case FeatureLevel::FEATURE_LEVEL_2:     return { 310, true };  // ES 3.1
                case FeatureLevel::FEATURE_LEVEL_3:     return { 310, true };  // ES 3.1
            }
        case ShaderModel::DESKTOP:
            // 桌面端：根据功能级别返回不同的GLSL版本
            return { featureLevel >= FeatureLevel::FEATURE_LEVEL_2 ? 430 : 410, false };
    }
}

// 根据目标API和目标语言获取glslang消息标志
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @return glslang消息标志
EShMessages GLSLTools::glslangFlagsFromTargetApi(
        MaterialBuilder::TargetApi targetApi,
        MaterialBuilder::TargetLanguage targetLanguage) {
    using TargetApi = MaterialBuilder::TargetApi;
    using TargetLanguage = MaterialBuilder::TargetLanguage;

    switch (targetLanguage) {
        case TargetLanguage::GLSL:
            // GLSL目标：使用默认消息标志
            assert_invariant(targetApi == TargetApi::OPENGL);
            return EShMessages::EShMsgDefault;

        case TargetLanguage::SPIRV:
            // issue messages for SPIR-V generation
            // 为SPIR-V生成发出消息
            using Type = std::underlying_type_t<EShMessages>;
            auto msg = (Type)EShMessages::EShMsgSpvRules;
            if (targetApi == TargetApi::VULKAN) {
                // issue messages for Vulkan-requirements of GLSL for SPIR-V
                // 为SPIR-V的GLSL的Vulkan要求发出消息
                msg |= (Type)EShMessages::EShMsgVulkanRules;
            }
            if (targetApi == TargetApi::METAL) {
                // FIXME: We have to use EShMsgVulkanRules for metal, otherwise compilation will
                //        choke on gl_VertexIndex.
                // FIXME: 我们必须为Metal使用EShMsgVulkanRules，否则编译会在gl_VertexIndex上失败
                msg |= (Type)EShMessages::EShMsgVulkanRules;
            }
            if (targetApi == TargetApi::WEBGPU) {
                // FIXME: We have to use EShMsgVulkanRules for WEBGPU, otherwise compilation will
                //        choke on gl_VertexIndex.
                // FIXME: 我们必须为WEBGPU使用EShMsgVulkanRules，否则编译会在gl_VertexIndex上失败
                msg |= (Type)EShMessages::EShMsgVulkanRules;
            }
            return (EShMessages)msg;
    }
}

// 准备着色器解析器（设置glslang着色器的环境配置）
// @param targetApi 目标API
// @param targetLanguage 目标语言
// @param shader glslang着色器对象
// @param stage 着色器阶段
// @param version GLSL版本号
void GLSLTools::prepareShaderParser(MaterialBuilder::TargetApi targetApi,
        MaterialBuilder::TargetLanguage targetLanguage, glslang::TShader& shader,
        EShLanguage stage, int version) {
    // We must only set up the SPIRV environment when we actually need to output SPIRV
    // 我们只有在实际需要输出SPIR-V时才设置SPIR-V环境
    if (targetLanguage == MaterialBuilder::TargetLanguage::SPIRV) {
        // 启用自动绑定映射
        shader.setAutoMapBindings(true);
        // 根据目标API设置环境配置
        switch (targetApi) {
            case MaterialBuilderBase::TargetApi::OPENGL:
                // OpenGL：设置OpenGL客户端环境
                shader.setEnvInput(EShSourceGlsl, stage, EShClientOpenGL, version);
                shader.setEnvClient(EShClientOpenGL, EShTargetOpenGL_450);
                break;
            case MaterialBuilderBase::TargetApi::WEBGPU:
            case MaterialBuilderBase::TargetApi::VULKAN:
            case MaterialBuilderBase::TargetApi::METAL:
                // Vulkan/Metal/WebGPU：设置Vulkan客户端环境
                shader.setEnvInput(EShSourceGlsl, stage, EShClientVulkan, version);
                shader.setEnvClient(EShClientVulkan, EShTargetVulkan_1_1);
                break;
            // TODO: Handle webgpu here
            // TODO: 在这里处理webgpu
            case MaterialBuilderBase::TargetApi::ALL:
                // can't happen
                // 不应该发生
                break;
        }
        // 设置SPIR-V目标版本
        shader.setEnvTarget(EShTargetSpv, EShTargetSpv_1_3);
    }
}

// 为着色器添加纹理LOD偏置（重载版本，使用默认入口点和符号名）
// @param shader glslang着色器对象
void GLSLTools::textureLodBias(TShader& shader) {
    TIntermediate* intermediate = shader.getIntermediate();
    TIntermNode* root = intermediate->getTreeRoot();
    // 调用完整版本，使用默认的材质函数入口点和lodBias符号名
    textureLodBias(intermediate, root,
            "material(struct-MaterialInputs",
            "filament_lodBias");
}

// 聚合遍历器适配器类：将函数对象适配为TIntermTraverser（用于遍历AST中的聚合节点）
// @tparam F 函数对象类型（接受TVisit和TIntermAggregate*参数）
template<typename F>
class AggregateTraverserAdapter : public glslang::TIntermTraverser {
    F closure;  // 函数对象（闭包）
public:
    // 构造函数：初始化遍历器和函数对象
    // @param closure 函数对象（将被调用来处理聚合节点）
    explicit AggregateTraverserAdapter(F closure)
            : TIntermTraverser(true, false, false, false),  // 只访问聚合节点
              closure(closure) { }

    // 访问聚合节点：调用函数对象处理节点
    // @param visit 访问模式
    // @param node 聚合节点
    // @return 函数对象的返回值（是否继续遍历）
    bool visitAggregate(glslang::TVisit visit, glslang::TIntermAggregate* node) override {
        return closure(visit, node);
    }
};

// 遍历AST中的聚合节点（使用函数对象处理每个聚合节点）
// @tparam F 函数对象类型
// @param root AST根节点
// @param closure 函数对象（将被调用来处理每个聚合节点）
template<typename F>
void traverseAggregate(TIntermNode* root, F&& closure) {
    // 创建适配器并遍历AST
    AggregateTraverserAdapter adapter(std::forward<std::decay_t<F>>(closure));
    root->traverse(&adapter);
}

// 为着色器添加纹理LOD偏置（完整版本，修改AST以添加lodBias参数到texture调用）
// @param intermediate glslang中间表示对象
// @param root AST根节点
// @param entryPointSignatureish 入口点函数签名（部分匹配）
// @param lodBiasSymbolName lodBias符号名称
void GLSLTools::textureLodBias(TIntermediate* intermediate, TIntermNode* root,
        const char* entryPointSignatureish, const char* lodBiasSymbolName) noexcept {

    // First, find the "lodBias" symbol and entry point
    // 首先，查找"lodBias"符号和入口点
    const std::string functionName{ entryPointSignatureish };
    TIntermSymbol* pIntermSymbolLodBias = nullptr;
    TIntermNode* pEntryPointRoot = nullptr;
    // 遍历AST查找入口点函数和lodBias符号
    traverseAggregate(root,
            [&](TVisit, TIntermAggregate* node) {
                if (node->getOp() == glslang::EOpSequence) {
                    return true;
                }
                // 查找入口点函数（函数名以functionName开头）
                if (node->getOp() == glslang::EOpFunction) {
                    if (node->getName().rfind(functionName, 0) == 0) {
                        pEntryPointRoot = node;
                    }
                    return false;
                }
                // 在链接器对象中查找lodBias符号
                if (node->getOp() == glslang::EOpLinkerObjects) {
                    for (TIntermNode* item: node->getSequence()) {
                        TIntermSymbol* symbol = item->getAsSymbolNode();
                        if (symbol && symbol->getBasicType() == TBasicType::EbtFloat) {
                            if (symbol->getName() == lodBiasSymbolName) {
                                pIntermSymbolLodBias = symbol;
                                break;
                            }
                        }
                    }
                }
                return true;
            });

    // 如果找不到入口点，直接返回（可能材质没有用户定义的代码，如深度材质）
    if (!pEntryPointRoot) {
        // This can happen if the material doesn't have user defined code,
        // e.g. with the depth material. We just do nothing then.
        // 如果材质没有用户定义的代码（例如深度材质），可能会发生这种情况。我们什么都不做。
        return;
    }

    // 如果找不到lodBias符号，记录错误并返回
    if (!pIntermSymbolLodBias) {
        // something went wrong
        // 出错了
        utils::slog.e << "lod bias ignored because \"" << lodBiasSymbolName << "\" was not found!"
                      << utils::io::endl;
        return;
    }

    // add lod bias to texture calls
    // we need to run this only from the user's main entry point
    // 为texture调用添加lod偏置
    // 我们只需要从用户的主入口点运行此操作
    traverseAggregate(pEntryPointRoot,
            [&](TVisit, TIntermAggregate* node) {
                // skip everything that's not a texture() call
                // 跳过所有不是texture()调用的内容
                if (node->getOp() != glslang::EOpTexture) {
                    return true;
                }

                TIntermSequence& sequence = node->getSequence();

                // first check that we have the correct sampler
                // 首先检查我们是否有正确的采样器
                TIntermTyped* pTyped = sequence[0]->getAsTyped();
                if (!pTyped) {
                    return false;
                }

                TSampler const& sampler = pTyped->getType().getSampler();
                // sampler2DArrayShadow不支持lod偏置
                if (sampler.isArrayed() && sampler.isShadow()) {
                    // sampler2DArrayShadow is not supported
                    return false;
                }

                // Then add the lod bias to the texture() call
                // 然后将lod偏置添加到texture()调用
                if (sequence.size() == 2) {
                    // we only have 2 parameters, add the 3rd one
                    // 我们只有2个参数，添加第3个参数
                    TIntermSymbol* symbol = intermediate->addSymbol(*pIntermSymbolLodBias);
                    sequence.push_back(symbol);
                } else if (sequence.size() == 3) {
                    // load bias is already specified
                    // lod偏置已经指定，将其与现有值相加
                    TIntermSymbol* symbol = intermediate->addSymbol(*pIntermSymbolLodBias);
                    TIntermTyped* pAdd = intermediate->addBinaryMath(TOperator::EOpAdd,
                            sequence[2]->getAsTyped(), symbol,
                            node->getLoc());
                    sequence[2] = pAdd;
                }

                return false;
            });
}

// 检查着色器是否有自定义深度（检查是否调用discard或写入gl_FragDepth）
// @param root AST根节点
// @param entryPoint 入口点节点
// @return 如果有自定义深度返回true，否则返回false
bool GLSLTools::hasCustomDepth(TIntermNode* root, TIntermNode* entryPoint) {

    // 内部类：用于遍历AST查找discard或gl_FragDepth写入
    class HasCustomDepth : public glslang::TIntermTraverser {
        using TVisit = glslang::TVisit;
        TIntermNode* const root;        // shader root
        bool hasCustomDepth = false;

    public:
        // 检查入口点是否有自定义深度
        bool operator()(TIntermNode* entryPoint) noexcept {
            entryPoint->traverse(this);
            return hasCustomDepth;
        }

        explicit HasCustomDepth(TIntermNode* root) : root(root) {}

        bool visitAggregate(TVisit, TIntermAggregate* node) override {
            if (node->getOp() == EOpFunctionCall) {
                // we have a function call, "recurse" into it to see if we call discard or
                // write to gl_FragDepth.
                // 我们有一个函数调用，"递归"进入它以查看是否调用discard或写入gl_FragDepth

                // find the entry point corresponding to that call
                // 查找与该调用对应的入口点
                TIntermNode* const entryPoint =
                        ASTHelpers::getFunctionBySignature(node->getName(), *root);

                // this should never happen because the shader has already been validated
                // 这不应该发生，因为着色器已经被验证
                assert_invariant(entryPoint);

                // 递归检查被调用的函数
                hasCustomDepth = hasCustomDepth || HasCustomDepth{ root }(entryPoint);

                return !hasCustomDepth;
            }
            return true;
        }

        // this checks if we write gl_FragDepth
        // 检查是否写入gl_FragDepth
        bool visitBinary(TVisit, glslang::TIntermBinary* node) override {
            TOperator const op = node->getOp();
            Symbol symbol;
            // 检查赋值操作（包括各种复合赋值）
            if (op == EOpAssign ||
                op == EOpAddAssign ||
                op == EOpDivAssign ||
                op == EOpSubAssign ||
                op == EOpMulAssign) {
                // 查找左值的基符号
                const TIntermTyped* n = findLValueBase(node->getLeft(), symbol);
                if (n != nullptr && n->getAsSymbolNode() != nullptr) {
                    const TString& symbolTString = n->getAsSymbolNode()->getName();
                    // 如果写入的是gl_FragDepth，标记为有自定义深度
                    if (symbolTString == "gl_FragDepth") {
                        hasCustomDepth = true;
                    }
                    // Don't visit subtree since we just traced it with findLValueBase()
                    // 不要访问子树，因为我们刚刚用findLValueBase()跟踪了它
                    return false;
                }
            }
            return true;
        }

        // this check if we call `discard`
        // 检查是否调用`discard`
        bool visitBranch(TVisit, glslang::TIntermBranch* branch) override {
            // 如果调用了discard（EOpKill），标记为有自定义深度
            if (branch->getFlowOp() == EOpKill) {
                hasCustomDepth = true;
                return false;
            }
            return true;
        }

    } hasCustomDepth(root);

    // 使用内部遍历器检查入口点
    return hasCustomDepth(entryPoint);
}

} // namespace filamat
