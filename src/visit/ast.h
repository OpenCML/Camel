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
#include "core/error/build.h"
#include "core/struct/scope.h"
#include "core/struct/sem.h"
#include "core/struct/tree.h"

class ASTNode;

using ast_ptr_t = std::shared_ptr<ASTNode>;

extern ast_ptr_t copyFuncDeRefNode;
extern ast_ptr_t castFuncDeRefNode;
extern ast_ptr_t typeFuncDeRefNode;
extern ast_ptr_t indexFuncDeRefNode;
extern ast_ptr_t chainFuncDeRefNode;

extern ast_ptr_t addFuncDeRefNode;
extern ast_ptr_t subFuncDeRefNode;
extern ast_ptr_t mulFuncDeRefNode;
extern ast_ptr_t divFuncDeRefNode;
extern ast_ptr_t modFuncDeRefNode;
extern ast_ptr_t powFuncDeRefNode;
extern ast_ptr_t interFuncDeRefNode;
extern ast_ptr_t unionFuncDeRefNode;

extern ast_ptr_t iAddFuncDeRefNode;
extern ast_ptr_t iSubFuncDeRefNode;
extern ast_ptr_t iMulFuncDeRefNode;
extern ast_ptr_t iDivFuncDeRefNode;
extern ast_ptr_t iModFuncDeRefNode;
extern ast_ptr_t iPowFuncDeRefNode;
extern ast_ptr_t iInterFuncDeRefNode;
extern ast_ptr_t iUnionFuncDeRefNode;

extern ast_ptr_t ltFuncDeRefNode;
extern ast_ptr_t gtFuncDeRefNode;
extern ast_ptr_t leFuncDeRefNode;
extern ast_ptr_t geFuncDeRefNode;
extern ast_ptr_t eqFuncDeRefNode;
extern ast_ptr_t neFuncDeRefNode;
extern ast_ptr_t andFuncDeRefNode;
extern ast_ptr_t orFuncDeRefNode;

void initFuncDeRefNodes();

class ASTNode : public AbstractTreeNode<sem_ptr_t>, std::enable_shared_from_this<ASTNode> {
  public:
    ASTNode(sem_ptr_t sem) : AbstractTreeNode(sem) {}
    virtual ~ASTNode() = default;

    SemNodeType type() const { return data->type(); }
    std::string toString() const { return data->toString(); }

    ASTNode &operator<<(const ast_ptr_t &node) {
        node->parent = this;
        this->push_back(node);
        return *this;
    }
};

template <typename NodeType, typename... Args> ast_ptr_t createAstNode(Args &&...args) {
    return std::make_shared<ASTNode>(std::make_shared<NodeType>(std::forward<Args>(args)...));
}

class ASTConstructor : public OpenCMLVisitor {
  public:
    ASTConstructor() {
        typeScope_ = std::make_shared<Scope<std::string, type_ptr_t>>();
        initFuncDeRefNodes();
    };
    virtual ~ASTConstructor() = default;

    ast_ptr_t construct(antlr4::tree::ParseTree *tree) {
        typeScope_->clear();
        root_ = nullptr;
        root_->parent = nullptr;
        visit(tree);
        return root_;
    }

  private:
    ast_ptr_t root_ = nullptr;
    size_t indentIndex_ = 0;
    scope_ptr_t<std::string, type_ptr_t> typeScope_;

    void pushScope() { typeScope_ = std::make_shared<Scope<std::string, type_ptr_t>>(typeScope_); }
    void popScope() { typeScope_ = typeScope_->outer(); } // TODO: Shall we free the scope?

    value_ptr_t extractStaticValue(const ast_ptr_t &node);
    std::pair<ast_ptr_t, value_ptr_t> makeDanglingValue(const ast_ptr_t &expr);
    std::pair<value_ptr_t, bool> extractValue(const ast_ptr_t &node, ast_ptr_t &execNode, bool &dangling);

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
    std::any visitEntityChain(OpenCMLParser::EntityChainContext *context);
    std::any visitEntityLink(OpenCMLParser::EntityLinkContext *context);
    std::any visitEntityUnit(OpenCMLParser::EntityUnitContext *context);
    std::any visitEntityWith(OpenCMLParser::EntityWithContext *context);
    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context);
    std::any visitRelaExpr(OpenCMLParser::RelaExprContext *context);
    std::any visitAddExpr(OpenCMLParser::AddExprContext *context);
    std::any visitMultiExpr(OpenCMLParser::MultiExprContext *context);
    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);
    std::any visitPrimExpr(OpenCMLParser::PrimExprContext *context);
    std::any visitLiteral(OpenCMLParser::LiteralContext *context);
    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);
    std::any visitUnaryType(OpenCMLParser::UnaryTypeContext *context);
    std::any visitAtomType(OpenCMLParser::AtomTypeContext *context);
    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context);
    std::any visitPrimType(OpenCMLParser::PrimTypeContext *context);
    std::any visitStructType(OpenCMLParser::StructTypeContext *context);
    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context);
    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);
};