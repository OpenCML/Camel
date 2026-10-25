// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/parse/ast.h"
#include <string>

namespace ASTCodeGen {

class Generator {
  public:
    Generator()  = default;
    ~Generator() = default;
    std::string generate(const AST::node_ptr_t &ast);

  private:
    std::string generateNode(const AST::node_ptr_t &node);
    std::string generateChildren(const AST::node_ptr_t &node);

    std::string generateModuleLoad(const AST::node_ptr_t &node);
    std::string generateImportLoad(const AST::node_ptr_t &node);
    std::string generateExportLoad(const AST::node_ptr_t &node);
    std::string generateDataDeclLoad(const AST::node_ptr_t &node);
    std::string generateFuncDeclLoad(const AST::node_ptr_t &node);
    std::string extractReturnType(const AST::node_ptr_t &funcTypeNode);
    void collectParamsFromNode(
        const AST::node_ptr_t &node, std::vector<std::string> &paramsVec, Generator *generator);
    void collectParams(
        const AST::node_ptr_t &node, std::vector<std::string> &paramsVec, Generator *generator);
    std::string generateTypeDeclLoad(const AST::node_ptr_t &node);
    std::string generateResourceDeclLoad(const AST::node_ptr_t &node);

    std::string generateStmtLoad(const AST::node_ptr_t &node);
    std::string generateExprStmtLoad(const AST::node_ptr_t &node);
    std::string generateExitStmtLoad(const AST::node_ptr_t &node);
    std::string generateStmtBlockLoad(const AST::node_ptr_t &node);

    std::string generateDataLoad(const AST::node_ptr_t &node);
    std::string generateUnaryExprLoad(const AST::node_ptr_t &node);
    std::string generateBinaryExprLoad(const AST::node_ptr_t &node);
    std::string generateReservedExprLoad(const AST::node_ptr_t &node);
    std::string generateIfExprLoad(const AST::node_ptr_t &node);
    std::string generateMatchExprLoad(const AST::node_ptr_t &node);
    std::string generateLiteralLoad(const AST::node_ptr_t &node);
    std::string generateListDataLoad(const AST::node_ptr_t &node);
    std::string generateDictDataLoad(const AST::node_ptr_t &node);
    std::string generateTupleDataLoad(const AST::node_ptr_t &node);
    std::string generateFuncDataLoad(const AST::node_ptr_t &node);
    std::string generateRefDataLoad(const AST::node_ptr_t &node);

    std::string generateTypeLoad(const AST::node_ptr_t &node);
    std::string generateNullableTypeLoad(const AST::node_ptr_t &node);
    std::string generateTypeExprLoad(const AST::node_ptr_t &node);
    std::string generateArrayTypeLoad(const AST::node_ptr_t &node);
    std::string generateStructTypeLoad(const AST::node_ptr_t &node);
    std::string generateTupleTypeLoad(const AST::node_ptr_t &node);
    std::string generateFuncTypeLoad(const AST::node_ptr_t &node);
    std::string generateInferTypeLoad(const AST::node_ptr_t &node);
    std::string generateRefTypeLoad(const AST::node_ptr_t &node);

    std::string generateNamedDataLoad(const AST::node_ptr_t &node);
    std::string generateNamedTypeLoad(const AST::node_ptr_t &node);
    std::string generateNamedPairLoad(const AST::node_ptr_t &node);
    std::string generateParamDeclLoad(const AST::node_ptr_t &node);
    std::string generateParamDataLoad(const AST::node_ptr_t &node);
};

} // namespace ASTCodeGen
