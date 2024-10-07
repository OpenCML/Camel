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
 * Created: Mar. 26, 2024
 * Updated: Oct. 7, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <queue>
#include <regex>
#include <string>

#include "antlr/OpenCMLVisitor.h"
#include "antlr4-runtime.h"
#include "common/ast.h"
#include "common/error/build.h"
#include "common/scope.h"
#include "common/tree.h"

namespace AbstractSyntaxTree {

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
            node_ptr_t execNode = createNode<ExecLoad>();
            node_ptr_t rhsNode = std::any_cast<node_ptr_t>(visit(dataVec[i]));

            std::string op = context->children[i * 2 - 1]->getText();
            node_ptr_t funcNode = InnerFuncDRefNodes::opNodesMap[op];

            auto [lhsData, lhsDangling] = extractData(lhsNode, execNode);
            auto [rhsData, rhsDangling] = extractData(rhsNode, execNode);
            node_ptr_t dataNode =
                createNode<DataLoad>(std::make_shared<TupleData>(data_list_t{lhsData, rhsData}),
                                     CREATE_DOUBLE_UNREF_LIST(lhsDangling, lhsData, rhsDangling, rhsData));

            if (lhsDangling || rhsDangling) {
                dataNode = reparent(dataNode, execNode);
            }

            lhsNode = linkFunc(dataNode, funcNode);
        }

        return lhsNode;
    }

    std::any visitProgram(OpenCMLParser::ProgramContext *context);

    std::any visitStmtList(OpenCMLParser::StmtListContext *context);

    std::any visitStmt(OpenCMLParser::StmtContext *context);

    std::any visitLetStmt(OpenCMLParser::LetStmtContext *context);

    std::any visitUseStmt(OpenCMLParser::UseStmtContext *context);

    std::any visitTypeStmt(OpenCMLParser::TypeStmtContext *context);

    std::any visitExprStmt(OpenCMLParser::ExprStmtContext *context);

    std::any visitWaitStmt(OpenCMLParser::WaitStmtContext *context);

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

    std::any visitIndexKTPair(OpenCMLParser::IndexKTPairContext *context);

    std::any visitIndexKVPair(OpenCMLParser::IndexKVPairContext *context);

    std::any visitTypeList(OpenCMLParser::TypeListContext *context);

    std::any visitIdentList(OpenCMLParser::IdentListContext *context);

    std::any visitValueList(OpenCMLParser::ValueListContext *context);

    std::any visitPairedTypes(OpenCMLParser::PairedTypesContext *context);

    std::any visitPairedValues(OpenCMLParser::PairedValuesContext *context);

    std::any visitPairedParams(OpenCMLParser::PairedParamsContext *context);

    std::any visitIndexKVPairs(OpenCMLParser::IndexKVPairsContext *context);

    std::any visitArgumentList(OpenCMLParser::ArgumentListContext *context);

    std::any visitBracedPairedValues(OpenCMLParser::BracedPairedValuesContext *context);

    std::any visitBracedIdents(OpenCMLParser::BracedIdentsContext *context);

    std::any visitBracedStmts(OpenCMLParser::BracedStmtsContext *context);

    std::any visitBracedValues(OpenCMLParser::BracedValuesContext *context);

    std::any visitBracedIndexKVPairs(OpenCMLParser::BracedIndexKVPairsContext *context);

    std::any visitBracketIdents(OpenCMLParser::BracketIdentsContext *context);

    std::any visitBracketValues(OpenCMLParser::BracketValuesContext *context);

    std::any visitMemberAccess(OpenCMLParser::MemberAccessContext *context);

    std::any visitParentParams(OpenCMLParser::ParentParamsContext *context);

    std::any visitParentArgues(OpenCMLParser::ParentArguesContext *context);

    std::any visitParentValues(OpenCMLParser::ParentValuesContext *context);

    std::any visitAngledParams(OpenCMLParser::AngledParamsContext *context);

    std::any visitAngledValues(OpenCMLParser::AngledValuesContext *context);

    std::any visitEntityExpr(OpenCMLParser::EntityExprContext *context);

    std::any visitTernaryExpr(OpenCMLParser::TernaryExprContext *context);

    std::any visitLogicalOrExpr(OpenCMLParser::LogicalOrExprContext *context);

    std::any visitLogicalAndExpr(OpenCMLParser::LogicalAndExprContext *context);

    std::any visitEqualityExpr(OpenCMLParser::EqualityExprContext *context);

    std::any visitRelationalExpr(OpenCMLParser::RelationalExprContext *context);

    std::any visitAdditiveExpr(OpenCMLParser::AdditiveExprContext *context);

    std::any visitMultiplicativeExpr(OpenCMLParser::MultiplicativeExprContext *context);

    std::any visitUnaryExpr(OpenCMLParser::UnaryExprContext *context);

    std::any visitLinkExpr(OpenCMLParser::LinkExprContext *context);

    std::any visitWithExpr(OpenCMLParser::WithExprContext *context);

    std::any visitAnnotatedExpr(OpenCMLParser::AnnotatedExprContext *context);

    std::any visitPrimaryExpr(OpenCMLParser::PrimaryExprContext *context);

    std::any visitLiteral(OpenCMLParser::LiteralContext *context);

    std::any visitTypeExpr(OpenCMLParser::TypeExprContext *context);

    std::any visitArrayType(OpenCMLParser::ArrayTypeContext *context);

    std::any visitAtomType(OpenCMLParser::AtomTypeContext *context);

    std::any visitLambdaType(OpenCMLParser::LambdaTypeContext *context);

    std::any visitPrimaryType(OpenCMLParser::PrimaryTypeContext *context);

    std::any visitStructType(OpenCMLParser::StructTypeContext *context);

    std::any visitSpecialType(OpenCMLParser::SpecialTypeContext *context);

    std::any visitIdentRef(OpenCMLParser::IdentRefContext *context);
};
} // namespace AbstractSyntaxTree
