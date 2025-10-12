/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Sep. 13, 2025
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */
#pragma once

#include "src/parse/ast/ast.h"
#include <string>

namespace ASTCodeGen {

class Generator {
  public:
    Generator() = default;
    ~Generator() = default;
    std::string generate(const AbstractSyntaxTree::node_ptr_t &ast);

  private:
    std::string generateNode(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateChildren(const AbstractSyntaxTree::node_ptr_t &node);

    std::string generateModuleLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateImportLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateExportLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateDataDeclLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateFuncDeclLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string extractReturnType(const AbstractSyntaxTree::node_ptr_t &funcTypeNode);
    void collectParamsFromNode(
        const AbstractSyntaxTree::node_ptr_t &node, std::vector<std::string> &paramsVec,
        Generator *generator);
    void collectParams(
        const AbstractSyntaxTree::node_ptr_t &node, std::vector<std::string> &paramsVec,
        Generator *generator);
    std::string generateTypeDeclLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateNameDeclLoad(const AbstractSyntaxTree::node_ptr_t &node);

    std::string generateStmtLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateExprStmtLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateExitStmtLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateStmtBlockLoad(const AbstractSyntaxTree::node_ptr_t &node);

    std::string generateDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateUnaryExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateBinaryExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateReservedExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateIfExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateMatchExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateTryExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateLiteralLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateListDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateDictDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateTupleDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateFuncDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateRefDataLoad(const AbstractSyntaxTree::node_ptr_t &node);

    std::string generateTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateNullableTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateTypeExprLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateArrayTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateStructTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateTupleTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateFuncTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateUnitTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateInferTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateDataTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateRefTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);

    std::string generateNamedDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateNamedTypeLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateNamedPairLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateParamDeclLoad(const AbstractSyntaxTree::node_ptr_t &node);
    std::string generateParamDataLoad(const AbstractSyntaxTree::node_ptr_t &node);
};

} // namespace ASTCodeGen
