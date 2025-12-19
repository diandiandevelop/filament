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

#ifndef TNT_SCAHELPERS_H_H
#define TNT_SCAHELPERS_H_H

#include <string>
#include <vector>
#include <intermediate.h>

class TIntermNode;

namespace ASTHelpers {

class NodeToString : public glslang::TIntermTraverser {
    void pad();
public:
    using TVisit = glslang::TVisit;
    bool visitBinary(TVisit, glslang::TIntermBinary* node) override;
    bool visitUnary(TVisit, glslang::TIntermUnary* node) override;
    bool visitAggregate(TVisit, glslang::TIntermAggregate* node) override;
    bool visitSelection(TVisit, glslang::TIntermSelection*) override;
    void visitConstantUnion(glslang::TIntermConstantUnion*) override;
    void visitSymbol(glslang::TIntermSymbol* node) override;
    bool visitLoop(TVisit, glslang::TIntermLoop*) override;
    bool visitBranch(TVisit, glslang::TIntermBranch*) override;
    bool visitSwitch(TVisit, glslang::TIntermSwitch*) override;
};

// Extract the name of a function from its glslang mangled signature. e.g: Returns prepareMaterial
// for input "prepareMaterial(struct-MaterialInputs-vf4-f1-f1-f1-f1-vf41;".
// 从glslang混淆的函数签名中提取函数名称。例如：对于输入"prepareMaterial(struct-MaterialInputs-vf4-f1-f1-f1-f1-vf41;"返回prepareMaterial
// @param functionSignature 函数签名（glslang混淆格式）
// @return 函数名称字符串视图
std::string_view getFunctionName(std::string_view functionSignature) noexcept;

// Traverse the AST root, looking for function definition. Returns the Function definition node
// matching the provided glslang mangled signature. Example of signature inputs:
// prepareMaterial(struct-MaterialInputs-vf4-f1-f1-f1-f1-vf41;
// main(
// 遍历AST根节点，查找函数定义。返回与提供的glslang混淆签名匹配的函数定义节点。签名输入示例：
// prepareMaterial(struct-MaterialInputs-vf4-f1-f1-f1-f1-vf41;
// main(
// @param functionSignature 函数签名（glslang混淆格式，完全限定名）
// @param root AST根节点
// @return 函数定义节点，如果未找到返回nullptr
glslang::TIntermAggregate* getFunctionBySignature(std::string_view functionSignature,
        TIntermNode& root) noexcept;

// Traverse the AST root, looking for function definition. Returns the Function definition node
// matching the provided function name. Example of signature inputs:
// prepareMaterial
// main
// This function is useful when looking for a function with variable signature. e.g: prepareMaterial
// and material functions take a struct which can vary in size depending on the property of the
// material processed.
// 遍历AST根节点，查找函数定义。返回与提供的函数名称匹配的函数定义节点。签名输入示例：
// prepareMaterial
// main
// 此函数在查找具有可变签名的函数时很有用。例如：prepareMaterial和material函数接受一个结构体，
// 该结构体的大小可能根据处理的材质属性而变化。
// @param functionName 函数名称（不包含参数列表）
// @param root AST根节点
// @return 函数定义节点，如果未找到返回nullptr
glslang::TIntermAggregate* getFunctionByNameOnly(std::string_view functionName,
        TIntermNode& root) noexcept;

// Recursively traverse the AST function node provided, looking for a call to the specified
// function. Traverse all function calls found in each function.
// 递归遍历提供的AST函数节点，查找对指定函数的调用。遍历在每个函数中找到的所有函数调用。
// @param functionName 要查找的函数名称
// @param functionNode 要搜索的函数节点
// @param rootNode AST根节点（用于查找函数定义）
// @return 如果函数被调用返回true，否则返回false
bool isFunctionCalled(std::string_view functionName, TIntermNode& functionNode,
        TIntermNode& rootNode) noexcept;

struct FunctionParameter {
    enum Qualifier { IN, OUT, INOUT, CONST };
    std::string name;
    std::string type;
    Qualifier qualifier;
};

// Traverse function definition node, looking for parameters and populate params vector.
// 遍历函数定义节点，查找参数并填充参数向量。
// @param func 函数聚合节点
// @param output 输出参数，函数参数列表
void getFunctionParameters(glslang::TIntermAggregate* func,
        std::vector<FunctionParameter>& output) noexcept;

// 将GLSL操作符转换为字符串（用于调试）
// @param op GLSL操作符枚举值
// @return 操作符名称字符串
std::string to_string(glslang::TOperator op);

// 获取结构体直接索引访问的字段名称字符串
// @param node 二元节点（结构体索引访问）
// @return 字段名称字符串
std::string getIndexDirectStructString(const glslang::TIntermBinary& node);

} // namespace ASTutils
#endif //TNT_SCAHELPERS_H_H
