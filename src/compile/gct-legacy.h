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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 26, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <queue>
#include <regex>
#include <string>

#include "parse/antlr/OpenCMLVisitor.h"
#include "antlr4-runtime/antlr4-runtime.h"
#include "common/error/build.h"
#include "common/gct.h"
#include "common/scope.h"
#include "common/tree.h"

namespace GraphConstructTree {

namespace InnerFuncDRefNodes {
extern node_ptr_t __copy__;
extern node_ptr_t __cast__;
extern node_ptr_t __type__;
extern node_ptr_t __index__;

extern node_ptr_t __as__;
extern node_ptr_t __is__;

extern node_ptr_t __add__;
extern node_ptr_t __sub__;
extern node_ptr_t __mul__;
extern node_ptr_t __div__;
extern node_ptr_t __mod__;
extern node_ptr_t __pow__;
extern node_ptr_t __inter__;
extern node_ptr_t __union__;

extern node_ptr_t __assn__;
extern node_ptr_t __assn_add__;
extern node_ptr_t __assn_sub__;
extern node_ptr_t __assn_mul__;
extern node_ptr_t __assn_div__;
extern node_ptr_t __assn_mod__;
extern node_ptr_t __assn_pow__;
extern node_ptr_t __assn_inter__;
extern node_ptr_t __assn_union__;

extern node_ptr_t __lt__;
extern node_ptr_t __gt__;
extern node_ptr_t __le__;
extern node_ptr_t __ge__;
extern node_ptr_t __eq__;
extern node_ptr_t __ne__;
extern node_ptr_t __and__;
extern node_ptr_t __or__;

extern node_ptr_t __not__;
extern node_ptr_t __neg__;
extern node_ptr_t __rev__;

extern node_ptr_t __ifexpr__;

extern std::unordered_map<std::string, node_ptr_t> nodesMap;
extern std::unordered_map<std::string, node_ptr_t> opNodesMap;

void init();
} // namespace InnerFuncDRefNodes

class Node : public AbstractTreeNode<load_ptr_t> {
  public:
    Node(load_ptr_t load) : AbstractTreeNode(load) {}
    virtual ~Node() = default;

    NodeType type() const { return load_->type(); }
    std::string toString() const { return load_->toString(); }

    Node &operator<<(const node_ptr_t &node) {
        node->setParent(this);
        this->push_back(node);
        return *this;
    }
};

class Constructor : public OpenCMLVisitor {
  public:
    Constructor() {
        typeScope_ = std::make_shared<Scope<std::string, type_ptr_t>>();
        InnerFuncDRefNodes::init();
    };
    virtual ~Constructor() = default;

    node_ptr_t construct(antlr4::tree::ParseTree *tree) {
        typeScope_->clear();
        root_ = nullptr;
        visit(tree);
        return root_;
    }

    std::queue<BuildWarning> &warns() { return warnQueue_; }

  private:
    node_ptr_t root_;
    size_t indentIndex_ = 0;
    scope_ptr_t<std::string, type_ptr_t> typeScope_;
    std::unordered_map<void *, func_type_ptr_t> funcDecls_;

    std::queue<BuildWarning> warnQueue_;

    void reportWarning(const std::string &msg, antlr4::Token *token) { warnQueue_.emplace(msg, token); }

    void pushScope() { typeScope_ = std::make_shared<Scope<std::string, type_ptr_t>>(typeScope_); }
    void popScope() { typeScope_ = typeScope_->outer(); } // TODO: Shall we free the scope?

    data_ptr_t extractStaticData(const node_ptr_t &node);
    std::pair<node_ptr_t, data_ptr_t> makeRefData(const node_ptr_t &expr);
    std::pair<data_ptr_t, bool> extractData(const node_ptr_t &node, node_ptr_t &execNode);
    std::pair<data_ptr_t, bool> extractData(const node_ptr_t &node, node_ptr_t &execNode, bool &dangling);

    template <typename Ctx, typename Val> node_ptr_t visitBinaryOpList(Ctx *context, std::vector<Val *> dataVec) {
        node_ptr_t lhsNode = std::any_cast<node_ptr_t>(visit(dataVec[0]));

        for (size_t i = 1; i < dataVec.size(); i++) {
            node_ptr_t execNode = createNodeAs<ExecLoad>();
            node_ptr_t rhsNode = std::any_cast<node_ptr_t>(visit(dataVec[i]));

            std::string op = context->children[i * 2 - 1]->getText();
            node_ptr_t funcNode = InnerFuncDRefNodes::opNodesMap[op];

            auto [lhsData, lhsDangling] = extractData(lhsNode, execNode);
            auto [rhsData, rhsDangling] = extractData(rhsNode, execNode);
            node_ptr_t dataNode = createDataNode<TupleData>(data_list_t{lhsData, rhsData});

            if (lhsDangling || rhsDangling) {
                dataNode = reparent(dataNode, execNode);
            }

            lhsNode = linkFunc(dataNode, funcNode);
        }

        return lhsNode;
    }

    // Auto-generated visitor methods

    std::any visitProgram(OpenCMLParser::ProgramContext *context);

    std::any visitDecl(OpenCMLParser::DeclContext *context);

    std::any visitStmt(OpenCMLParser::StmtContext *context);

    std::any visitStmtList(OpenCMLParser::StmtListContext *context);

    std::any visitModuleDecl(OpenCMLParser::ModuleDeclContext *context);

    std::any visitImportDecl(OpenCMLParser::ImportDeclContext *context);

    std::any visitExportDecl(OpenCMLParser::ExportDeclContext *context);

    std::any visitBlockStmt(OpenCMLParser::BlockStmtContext *context);

    std::any visitStmtBlock(OpenCMLParser::StmtBlockContext *context);

    std::any visitBlockExpr(OpenCMLParser::BlockExprContext *context);

    std::any visitFuncData(OpenCMLParser::FuncDataContext *context);

    std::any visitFuncDecl(OpenCMLParser::FuncDeclContext *context);

    std::any visitParentIdents(OpenCMLParser::ParentIdentsContext *context);

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);

    std::any visitCarrier(OpenCMLParser::CarrierContext *context);

    std::any visitDataDecl(OpenCMLParser::DataDeclContext *context);

    std::any visitTypeDecl(OpenCMLParser::TypeDeclContext *context);

    std::any visitUseDecl(OpenCMLParser::UseDeclContext *context);

    std::any visitRetStmt(OpenCMLParser::RetStmtContext *context);

    std::any visitImplMark(OpenCMLParser::ImplMarkContext *context);

    std::any visitModifiers(OpenCMLParser::ModifiersContext *context);

    std::any visitIndexValue(OpenCMLParser::IndexValueContext *context);

    std::any visitKeyTypePair(OpenCMLParser::KeyTypePairContext *context);

    std::any visitKeyValuePair(OpenCMLParser::KeyValuePairContext *context);

    std::any visitKeyParamPair(OpenCMLParser::KeyParamPairContext *context);

    std::any visitDataList(OpenCMLParser::DataListContext *context);

    std::any visitIdentList(OpenCMLParser::IdentListContext *context);

    std::any visitIndexValues(OpenCMLParser::IndexValuesContext *context);

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context);

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context);

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context);

    std::any visitIndices(OpenCMLParser::IndicesContext *context);

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context);

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);

    std::any visitPattern(OpenCMLParser::PatternContext *context);

    std::any visitMatchCase(OpenCMLParser::MatchCaseContext *context);

    std::any visitCatchClause(OpenCMLParser::CatchClauseContext *context);

    std::any visitCtrlExpr(OpenCMLParser::CtrlExprContext *context);

    std::any visitDataExpr(OpenCMLParser::DataExprContext *context);

    std::any visitWaitExpr(OpenCMLParser::WaitExprContext *context);

    std::any visitAssignExpr(OpenCMLParser::AssignExprContext *context);

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context);

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context);

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context);

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context);

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context);

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context);

    std::any visitNullableExpr(OpenCMLParser::NullableExprContext *context);

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context);

    std::any visitBindExpr(OpenCMLParser::BindExprContext *context);

    std::any visitAnnoExpr(OpenCMLParser::AnnoExprContext *context);

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context);

    std::any visitDictData(OpenCMLParser::DictDataContext *context);

    std::any visitListData(OpenCMLParser::ListDataContext *context);

    std::any visitTupleData(OpenCMLParser::TupleDataContext *context);

    std::any visitPrimaryData(OpenCMLParser::PrimaryDataContext *context);

    std::any visitLiteral(OpenCMLParser::LiteralContext *context);

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);

    std::any visitUnionType(OpenCMLParser::UnionTypeContext *context);

    std::any visitInterType(OpenCMLParser::InterTypeContext *context);

    std::any visitDiffType(OpenCMLParser::DiffTypeContext *context);

    std::any visitKeyUnionDiffType(OpenCMLParser::KeyUnionDiffTypeContext *context);

    std::any visitKeyInterType(OpenCMLParser::KeyInterTypeContext *context);

    std::any visitTypeUnit(OpenCMLParser::TypeUnitContext *context);

    std::any visitListType(OpenCMLParser::ListTypeContext *context);

    std::any visitTypeOrData(OpenCMLParser::TypeOrDataContext *context);

    std::any visitSpecType(OpenCMLParser::SpecTypeContext *context);

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context);

    std::any visitDictType(OpenCMLParser::DictTypeContext *context);

    std::any visitTypeList(OpenCMLParser::TypeListContext *context);

    std::any visitTupleType(OpenCMLParser::TupleTypeContext *context);

    std::any visitFuncType(OpenCMLParser::FuncTypeContext *context);

    std::any visitIdentDef(OpenCMLParser::IdentDefContext *context);

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);

    // End of auto-generated visitor methods
};

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    return std::make_shared<Node>(std::make_shared<LoadType>(std::forward<Args>(args)...));
}

template <typename DataType, typename... Args> node_ptr_t createDataNode(Args &&...args) {
    return createNodeAs<DataLoad>(std::make_shared<DataType>(std::forward<Args>(args)...));
}
} // namespace GraphConstructTree
