/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 26, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"
#include "struct/tree.h"
#include "struct/sem.h"


class ASTNode : public AbstractTreeNode<sem_ptr_t> {
  public:
    ASTNode(sem_ptr_t sem) : AbstractTreeNode(sem) {}

    std::string toString() const {
        return data->toString();
    }
};

using ast_ptr_t = std::shared_ptr<ASTNode>;

class ASTConstructor : public OpenCMLVisitor {

    std::any dumpCST(antlr4::tree::ParseTree *context, std::string nodeName) { return ""; };

    std::any visitProgram(OpenCMLParser::ProgramContext *context);

    std::any visitStmtList(OpenCMLParser::StmtListContext *context);

    std::any visitStmt(OpenCMLParser::StmtContext *context);

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context);

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context);

    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context);

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context);

    std::any visitAssignStmt(OpenCMLParser::AssignStmtContext *context);

    std::any visitWithDef(OpenCMLParser::WithDefContext *context);

    std::any visitFuncDef(OpenCMLParser::FuncDefContext *context);

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context);

    std::any visitLambdaExpr(OpenCMLParser::LambdaExprContext *context);

    std::any visitCarrier(OpenCMLParser::CarrierContext *context);

    std::any visitAnnotation(OpenCMLParser::AnnotationContext *context);

    std::any visitAnnotations(OpenCMLParser::AnnotationsContext *context);

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context);

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context);

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context);

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context);

    std::any visitTypeList(OpenCMLParser::TypeListContext *context);

    std::any visitIdentList(OpenCMLParser::IdentListContext *context);

    std::any visitValueList(OpenCMLParser::ValueListContext *context);

    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context);

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context);

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context);

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context);

    std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context);

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);

    std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context);

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);

    std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context);

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);

    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context);

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);

    std::any visitPrimEntity(OpenCMLParser::PrimEntityContext *context);

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context);

    std::any visitEntity(OpenCMLParser::EntityContext *context);

    std::any visitEntityChain(OpenCMLParser::EntityChainContext *context);

    std::any visitEntityLink(OpenCMLParser::EntityLinkContext *context);

    std::any visitEntityCall(OpenCMLParser::EntityCallContext *context);

    std::any visitEntitySpread(OpenCMLParser::EntitySpreadContext *context);

    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context);

    std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context);

    std::any visitAddExpr(OpenCMLParser::AddExprContext *context);

    std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context);

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);

    std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context);

    std::any visitLiteral(OpenCMLParser::LiteralContext *context);

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);

    std::any visitType(OpenCMLParser::TypeContext *context);

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context);

    std::any visitPrimType(OpenCMLParser::PrimTypeContext *context);

    std::any visitStructType(OpenCMLParser::StructTypeContext *context);

    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context);

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);
};