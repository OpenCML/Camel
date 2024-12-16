/**
 * Copyright (c) 2024 Beijing Jiaotong University
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 17, 2024
 * Updated: Oct. 19, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"

class CSTDumpVisitor : public OpenCMLVisitor {
    int depth = 0;
    std::vector<bool> visible;

    std::any dumpCST(antlr4::tree::ParseTree *context, std::string nodeName);
    
    std::any visitProgram(OpenCMLParser::ProgramContext *context) { return dumpCST(context, "Program"); };

    std::any visitDecl(OpenCMLParser::DeclContext *context) { return dumpCST(context, "Decl"); };

    std::any visitStmt(OpenCMLParser::StmtContext *context) { return dumpCST(context, "Stmt"); };

    std::any visitStmtList(OpenCMLParser::StmtListContext *context) { return dumpCST(context, "StmtList"); };

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context) { return dumpCST(context, "ModuleDecl"); };

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context) { return dumpCST(context, "ImportDecl"); };

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context) { return dumpCST(context, "ExportDecl"); };

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context) { return dumpCST(context, "FuncDecl"); };

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context) { return dumpCST(context, "LetStmt"); };

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context) { return dumpCST(context, "UseStmt"); };

    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context) { return dumpCST(context, "TypeStmt"); };

    std::any visitEnumStmt(OpenCMLParser::EnumStmtContext *context) { return dumpCST(context, "EnumStmt"); };

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context) { return dumpCST(context, "ExprStmt"); };

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context) { return dumpCST(context, "RetStmt"); };

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context) { return dumpCST(context, "StmtBlock"); };

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context) { return dumpCST(context, "LambdaExpr"); };

    std::any visitCarrier(OpenCMLParser::CarrierContext *context) { return dumpCST(context, "Carrier"); };

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context) { return dumpCST(context, "Annotation"); };

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context) { return dumpCST(context, "Annotations"); };

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context) { return dumpCST(context, "Modifiers"); };

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context) { return dumpCST(context, "IndexValue"); };

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context) { return dumpCST(context, "KeyTypePair"); };

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context) { return dumpCST(context, "KeyValuePair"); };

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context) { return dumpCST(context, "KeyParamPair"); };

    std::any visitTypeList(OpenCMLParser::TypeListContext *context) { return dumpCST(context, "TypeList"); };

    std::any visitIdentList(OpenCMLParser::IdentListContext *context) { return dumpCST(context, "IdentList"); };

    std::any visitValueList(OpenCMLParser::ValueListContext *context) { return dumpCST(context, "ValueList"); };

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context) { return dumpCST(context, "IndexValues"); };

    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context) { return dumpCST(context, "PairedTypes"); };

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context) { return dumpCST(context, "PairedValues"); };

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context) { return dumpCST(context, "PairedParams"); };

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context) { return dumpCST(context, "ArgumentList"); };

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context) { return dumpCST(context, "BracedIdents"); };

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context) { return dumpCST(context, "BracketIdents"); };

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context) { return dumpCST(context, "MemberAccess"); };

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context) { return dumpCST(context, "ParentParams"); };

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context) { return dumpCST(context, "ParentArgues"); };

    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context) { return dumpCST(context, "ParentValues"); };

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context) { return dumpCST(context, "AngledParams"); };

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context) { return dumpCST(context, "AngledValues"); };

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context) { return dumpCST(context, "BlockExpr"); };

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context) { return dumpCST(context, "DataExpr"); };

    std::any visitStructExpr(OpenCMLParser::StructExprContext *context) { return dumpCST(context, "StructExpr"); };

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context) { return dumpCST(context, "LogicalOrExpr"); };

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context) { return dumpCST(context, "LogicalAndExpr"); };

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context) { return dumpCST(context, "EqualityExpr"); };

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context) { return dumpCST(context, "RelationalExpr"); };

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context) { return dumpCST(context, "AdditiveExpr"); };

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context) { return dumpCST(context, "MultiplicativeExpr"); };

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context) { return dumpCST(context, "NullableExpr"); };

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context) { return dumpCST(context, "UnaryExpr"); };

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context) { return dumpCST(context, "LinkExpr"); };

    std::any visitBindExpr(OpenCMLParser::BindExprContext *context) { return dumpCST(context, "BindExpr"); };

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context) { return dumpCST(context, "WithExpr"); };

    std::any visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context) { return dumpCST(context, "AnnotatedExpr"); };

    std::any visitDictExpr(OpenCMLParser::DictExprContext *context) { return dumpCST(context, "DictExpr"); };

    std::any visitListExpr(OpenCMLParser::ListExprContext *context) { return dumpCST(context, "ListExpr"); };

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context) { return dumpCST(context, "PrimaryData"); };

    std::any visitLiteral(OpenCMLParser::LiteralContext *context) { return dumpCST(context, "Literal"); };

    std::any visitTypeName(OpenCMLParser::TypeNameContext *context) { return dumpCST(context, "TypeName"); };

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context) { return dumpCST(context, "TypeExpr"); };

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context) { return dumpCST(context, "UnionType"); };

    std::any visitListType(OpenCMLParser::ListTypeContext *context) { return dumpCST(context, "ListType"); };

    std::any visitArgsType(OpenCMLParser::ArgsTypeContext *context) { return dumpCST(context, "ArgsType"); };

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context) { return dumpCST(context, "PrimaryType"); };

    std::any visitDictExprType(OpenCMLParser::DictExprTypeContext *context) { return dumpCST(context, "DictExprType"); };

    std::any visitDictType(OpenCMLParser::DictTypeContext *context) { return dumpCST(context, "DictType"); };

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context) { return dumpCST(context, "LambdaType"); };

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context) { return dumpCST(context, "IdentDef"); };

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context) { return dumpCST(context, "IdentRef"); };
};
