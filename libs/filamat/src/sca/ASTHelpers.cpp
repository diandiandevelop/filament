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

#include "ASTHelpers.h"

#include "GLSLTools.h"

#include <intermediate.h>
#include <localintermediate.h>

#include <utils/Log.h>

using namespace glslang;

namespace ASTHelpers {

// Traverse the AST to find the definition of a function based on its name/signature.
// e.g: prepareMaterial(struct-MaterialInputs-vf4-vf41;
// 遍历AST查找函数定义（基于函数名称/签名）
// 例如：prepareMaterial(struct-MaterialInputs-vf4-vf41;
class FunctionDefinitionFinder : public TIntermTraverser {
public:
    // 构造函数：初始化函数名称和是否使用完全限定名标志
    // @param functionName 要查找的函数名称（可以是完全限定名或简单名称）
    // @param useFQN 是否使用完全限定名进行匹配（true表示完全匹配，false表示只匹配函数名）
    explicit FunctionDefinitionFinder(std::string_view functionName, bool useFQN = true)
            : mFunctionName(functionName), mUseFQN(useFQN) {
    }

    // 访问聚合节点：检查是否为函数定义节点并匹配函数名
    // @param visit 访问模式
    // @param node 聚合节点
    // @return 如果找到匹配的函数定义返回false（停止遍历），否则返回true（继续遍历）
    bool visitAggregate(TVisit, TIntermAggregate* node) override {
        // 只处理函数定义节点
        if (node->getOp() == EOpFunction) {
            bool match;
            if (mUseFQN) {
                // 使用完全限定名进行精确匹配
                match = node->getName() == mFunctionName;
            } else {
                // 只比较函数名（忽略参数签名）
                std::string_view const prospectFunctionName = getFunctionName(node->getName());
                std::string_view const cleanedFunctionName = getFunctionName(mFunctionName);
                match = prospectFunctionName == cleanedFunctionName;
            }
            if (match) {
                // 找到匹配的函数定义，保存节点并停止遍历
                mFunctionDefinitionNode = node;
                return false;
            }
        }
        return true;
    }

    // 获取找到的函数定义节点
    // @return 函数定义节点，如果未找到返回nullptr
    TIntermAggregate* getFunctionDefinitionNode() const noexcept {
        return mFunctionDefinitionNode;
    }
private:
    const std::string_view mFunctionName;
    bool mUseFQN;
    TIntermAggregate* mFunctionDefinitionNode = nullptr;
};


// Traverse the AST to find out if a function is called in the function node or in any of its
// child function call nodes.
// 遍历AST查找函数是否在函数节点或其子函数调用节点中被调用
class FunctionCallFinder : public TIntermTraverser {
public:
    // 构造函数：初始化要查找的函数名和AST根节点
    // @param functionName 要查找的函数名称
    // @param root AST根节点（用于查找函数定义）
    FunctionCallFinder(std::string_view functionName, TIntermNode& root) :
            mFunctionName(functionName), mRoot(root) {}

    // 检查函数是否被调用
    // @return 如果函数被调用返回true，否则返回false
    bool functionWasCalled() const noexcept {
        return mFunctionFound;
    }

    // 访问聚合节点：检查函数调用并递归查找调用链
    // @param visit 访问模式
    // @param node 聚合节点
    // @return 总是返回true（继续遍历）
    bool visitAggregate(TVisit, TIntermAggregate* node) override {
        // 只处理函数调用节点
        if (node->getOp() != EOpFunctionCall) {
            return true;
        }
        std::string_view const functionCalledName = node->getName();
        if (functionCalledName == mFunctionName) {
            // 直接找到目标函数调用
            mFunctionFound = true;
        } else {
            // This function call site A is not what we were looking for but it could be in A's body
            // so we recurse inside A to check that.
            // 这个函数调用点A不是我们要找的，但它可能在A的函数体内，所以递归进入A检查
            // 查找被调用函数的定义
            FunctionDefinitionFinder finder(functionCalledName);
            mRoot.traverse(&finder);
            TIntermNode* functionDefNode = finder.getFunctionDefinitionNode();

            // Recurse to follow call chain.
            // 递归跟踪调用链（检查被调用的函数内部是否调用了目标函数）
            mFunctionFound |= isFunctionCalled(mFunctionName, *functionDefNode, mRoot);
        }
        return true;
    }

private:
    const std::string_view mFunctionName;
    TIntermNode& mRoot;
    bool mFunctionFound = false;
};

// For debugging and printing out an AST portion. Mostly incomplete but complete enough for our need
// TODO: Add more switch cases as needed.
// 将GLSL操作符转换为字符串（用于调试）
// @param op GLSL操作符枚举值
// @return 操作符名称字符串
std::string to_string(TOperator op) {
    // 根据操作符类型返回对应的字符串表示
    switch (op) {
        case EOpSequence:               return "EOpSequence";
        case EOpAssign:                 return "EOpAssign";
        case EOpAddAssign:              return "EOpAddAssign";
        case EOpSubAssign:              return "EOpSubAssign";
        case EOpMulAssign:              return "EOpMulAssign";
        case EOpDivAssign:              return "EOpDivAssign";
        case EOpVectorSwizzle:          return "EOpVectorSwizzle";
        case EOpIndexDirectStruct:      return "EOpIndexDirectStruct";
        case EOpFunction:               return "EOpFunction";
        case EOpFunctionCall:           return "EOpFunctionCall";
        case EOpParameters:             return "EOpParameters";
        // branch
        case EOpKill:                   return "EOpKill";
        case EOpTerminateInvocation:    return "EOpTerminateInvocation";
        case EOpDemote:                 return "EOpDemote";
        case EOpTerminateRayKHR:        return "EOpTerminateRayKHR";
        case EOpIgnoreIntersectionKHR:  return "EOpIgnoreIntersectionKHR";
        case EOpReturn:                 return "EOpReturn";
        case EOpBreak:                  return "EOpBreak";
        case EOpContinue:               return "EOpContinue";
        case EOpCase:                   return "EOpCase";
        case EOpDefault:                return "EOpDefault";
        default:
            return std::to_string((int)op);
    }
}

// 获取结构体直接索引访问的字段名称字符串
// @param node 二元节点（结构体索引访问）
// @return 字段名称字符串
std::string getIndexDirectStructString(const TIntermBinary& node) {
    // 步骤1：从节点的左操作数（结构体）获取结构体类型列表
    const TTypeList& structNode = *(node.getLeft()->getType().getStruct());
    // 步骤2：从节点的右操作数获取索引常量（必须是常量联合节点）
    TIntermConstantUnion* index =   node.getRight() ->getAsConstantUnion();
    // 步骤3：使用索引值从结构体类型列表中获取对应字段的类型，然后获取字段名称
    return structNode[index->getConstArray()[0].getIConst()].type->getFieldName().c_str();
}


// 从函数签名中提取函数名称（去除参数列表）
// @param functionSignature 函数签名（如"functionName(param1, param2)"）
// @return 函数名称（如"functionName"）
std::string_view getFunctionName(std::string_view functionSignature) noexcept {
    // 查找左括号位置，提取括号前的函数名
    auto indexParenthesis = functionSignature.find('(');
    return functionSignature.substr(0, indexParenthesis);
}

// 根据函数签名查找函数定义节点（使用完全限定名）
// @param functionSignature 函数签名（完全限定名，包含参数类型）
// @param rootNode AST根节点
// @return 函数定义节点，如果未找到返回nullptr
glslang::TIntermAggregate* getFunctionBySignature(std::string_view functionSignature,
        TIntermNode& rootNode) noexcept {
    // 使用完全限定名查找函数定义
    FunctionDefinitionFinder functionDefinitionFinder(functionSignature);
    rootNode.traverse(&functionDefinitionFinder);
    return functionDefinitionFinder.getFunctionDefinitionNode();
}

// 根据函数名称查找函数定义节点（仅使用函数名，不使用参数类型）
// @param functionName 函数名称（不包含参数列表）
// @param rootNode AST根节点
// @return 函数定义节点，如果未找到返回nullptr
glslang::TIntermAggregate* getFunctionByNameOnly(std::string_view functionName,
        TIntermNode& rootNode) noexcept {
    // 使用函数名查找函数定义（不使用完全限定名）
    FunctionDefinitionFinder functionDefinitionFinder(functionName, false);
    rootNode.traverse(&functionDefinitionFinder);
    return functionDefinitionFinder.getFunctionDefinitionNode();
}

// 检查函数是否在函数节点或其子函数调用中被调用
// @param functionName 要查找的函数名称
// @param functionNode 要搜索的函数节点
// @param rootNode AST根节点（用于查找函数定义）
// @return 如果函数被调用返回true，否则返回false
bool isFunctionCalled(std::string_view functionName, TIntermNode& functionNode,
        TIntermNode& rootNode) noexcept {
    // 使用FunctionCallFinder遍历器查找函数调用
    FunctionCallFinder traverser(functionName, rootNode);
    functionNode.traverse(&traverser);
    return traverser.functionWasCalled();
}

// 将glslang存储限定符转换为函数参数限定符
// @param q glslang存储限定符
// @return 函数参数限定符
static FunctionParameter::Qualifier glslangQualifier2FunctionParameter(TStorageQualifier q) {
    // 根据glslang限定符类型返回对应的函数参数限定符
    switch (q) {
        case EvqIn: return FunctionParameter::Qualifier::IN;
        case EvqInOut: return FunctionParameter::Qualifier::INOUT;
        case EvqOut: return FunctionParameter::Qualifier::OUT;
        case EvqConstReadOnly : return FunctionParameter::Qualifier::CONST;
        default: return FunctionParameter::Qualifier::IN;
    }
}

// 获取函数的参数列表
// @param func 函数聚合节点
// @param output 输出参数，函数参数列表
void getFunctionParameters(TIntermAggregate* func,
        std::vector<FunctionParameter>& output) noexcept {
    // 如果函数节点为空，直接返回
    if (func == nullptr) {
        return;
    }

    // Does it have a list of params
    // The second aggregate is the list of instructions, but the function may be empty
    // 是否有参数列表
    // 第二个聚合是指令列表，但函数可能为空
    if (func->getSequence().empty()) {
        return;
    }

    // A function aggregate has a sequence of two aggregate children:
    // Index 0 is a list of params (IntermSymbol).
    // 函数聚合有两个聚合子节点序列：
    // 索引0是参数列表（IntermSymbol）。
    // 步骤1：遍历函数参数列表（第一个子聚合节点的序列）
    for(TIntermNode* parameterNode : func->getSequence().at(0)->getAsAggregate()->getSequence()) {
        // 步骤2：将参数节点转换为符号节点
        TIntermSymbol* parameter = parameterNode->getAsSymbolNode();
        // 步骤3：提取参数信息（名称、类型、限定符）并创建FunctionParameter对象
        FunctionParameter const p = {
                parameter->getName().c_str(),  // 参数名称
                parameter->getType().getCompleteString().c_str(),  // 参数类型完整字符串
                glslangQualifier2FunctionParameter(parameter->getType().getQualifier().storage)  // 转换存储限定符
        };
        // 步骤4：将参数添加到输出列表
        output.push_back(p);
    }
}

// 添加缩进（用于调试输出格式化）
void NodeToString::pad() {
    // 根据深度添加缩进空格
    for (int i = 0; i < depth; ++i) {
        utils::slog.d << "    ";
    }
}

// 访问二元节点（用于调试输出）
// @param node 二元节点
// @return 返回true继续遍历
bool NodeToString::visitBinary(TVisit, TIntermBinary* node) {
    pad();
    utils::slog.d << "Binary " << to_string(node->getOp()) << utils::io::endl;
    return true;
}

// 访问一元节点（用于调试输出）
// @param node 一元节点
// @return 返回true继续遍历
bool NodeToString::visitUnary(TVisit, TIntermUnary* node) {
    pad();
    utils::slog.d << "Unary " << to_string(node->getOp()) << utils::io::endl;
    return true;
}

// 访问聚合节点（用于调试输出）
// @param node 聚合节点
// @return 返回true继续遍历
bool NodeToString::visitAggregate(TVisit, TIntermAggregate* node) {
    pad();
    utils::slog.d << "Aggregate " << to_string(node->getOp());
    utils::slog.d << " " << node->getName().c_str();
    utils::slog.d << utils::io::endl;
    return true;
}

// 访问选择节点（if/else，用于调试输出）
// @return 返回true继续遍历
bool NodeToString::visitSelection(TVisit, TIntermSelection*) {
    pad();
    utils::slog.d << "Selection " << utils::io::endl;
    return true;
}

// 访问常量联合节点（用于调试输出）
void NodeToString::visitConstantUnion(TIntermConstantUnion*) {
    pad();
    utils::slog.d << "ConstantUnion " << utils::io::endl;
}

// 访问符号节点（用于调试输出）
// @param node 符号节点
void NodeToString::visitSymbol(TIntermSymbol* node) {
    pad();
    utils::slog.d << "Symbol " << node->getAsSymbolNode()->getName().c_str() << utils::io::endl;
}

// 访问循环节点（for/while，用于调试输出）
// @return 返回true继续遍历
bool NodeToString::visitLoop(TVisit, TIntermLoop*) {
    pad();
    utils::slog.d << "Loop " << utils::io::endl;
    return true;
}

// 访问分支节点（return/break/continue，用于调试输出）
// @param branch 分支节点
// @return 返回true继续遍历
bool NodeToString::visitBranch(TVisit, TIntermBranch* branch) {
    pad();
    utils::slog.d << "Branch " << to_string(branch->getFlowOp()) << utils::io::endl;
    return true;
}

// 访问switch节点（用于调试输出）
// @return 返回true继续遍历
bool NodeToString::visitSwitch(TVisit, TIntermSwitch*) {
    utils::slog.d << "Switch " << utils::io::endl;
    return true;
}

} // namespace ASTHelpers
