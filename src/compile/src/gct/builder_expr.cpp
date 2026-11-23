// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the lowering of statements and of control-flow expressions -- blocks, exits, if/loop/match, the static forms, the operator expressions and shape comprehensions.

#include "builder.h"
#include <exception>

#include "camel/compile/semantic_recorder.h"
#include "camel/core/data/composite/array.h"
#include "camel/core/data/composite/struct.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/module/userdef.h"
#include "camel/core/operator_names.h"
#include "camel/core/tags.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/native.h"
#include "camel/core/type/other.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typecall.h"
#include "camel/core/type/typevar.h"
#include "camel/parse/ast/type.h"
#include "camel/utils/escape.h"
#include "camel/utils/number_text.h"
#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"
#include <stdexcept>

#include "builder_internal.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gct {



/*
ExprStmt() : Data data ;
*/
node_ptr_t Builder::visitExprStmt(const AST::node_ptr_t &ast) {
    ENTER("ExprStmt");
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExprStmt");
    node_ptr_t exprNode = visitData(ast->atAs<AST::DataLoad>(0));
    LEAVE("ExprStmt");
    return exprNode;
}

/*
ExitStmt(ExitType type) : Data* data ;
*/
node_ptr_t Builder::visitExitStmt(const AST::node_ptr_t &ast) {
    ENTER("ExitStmt");
    // Ensure the AST node is of the expected type (Stmt)
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for ExitStmt");

    // Create an ExitLoad node to represent the exit statement.
    const AST::ExitType astExit = ast->loadAs<AST::ExitStmtLoad>()->exitType();
    // A `yield` is a node of this tree: the generator body is built in GIR, and the GIR splits the
    // body into arms at the `yield` it finds there, so the node is the only thing it can find. The
    // machine in this file still claims every `yield` before the statement builder is reached, so
    // nothing builds one today: this line is the seam the GIR machine lands on. An unclaimed `yield`
    // arrives one layer down (`gir::Builder::visitExitNode`).
    node_ptr_t exitNode = tree_->makeExit(
        astExit == AST::ExitType::Yield ? ExitType::Yield : ExitType::Return);
    if (astExit == AST::ExitType::Return) {
        reportExitInUnusedArm(ast);
    }

    // Retrieve the data nodes associated with the exit statement
    const auto &dataNodes = ast->atAs<AST::RepeatedLoad>(0);

    // If there are data nodes, process them
    if (dataNodes->size() > 0) {
        // Case 1: Single data node
        if (dataNodes->size() == 1) {
            // Lowered once: lowering twice and dropping the first result would double the
            // work at every `return` nested in the returned expression (2^n lowerings at
            // depth n) and repeat the diagnostics of the expression.
            *exitNode << visitData(dataNodes->front());
        } else {
            // Case 2: Multiple data nodes - build once with Factory to avoid recomputing the type.
            TupleDataFactory tupleFactory;
            bool dangling       = false;
            node_ptr_t execNode = makeExec(false, true);

            for (const auto &item : *dataNodes) {
                node_ptr_t dataNode = visitData(item);
                auto [data, _]      = extractData(dataNode, execNode, dangling);
                tupleFactory.add(data);
            }

            auto tupleData      = tupleFactory.build();
            node_ptr_t dataNode = tree_->makeData(tupleData);

            if (dangling) {
                *execNode << dataNode;
                dataNode = execNode;
            }

            *exitNode << dataNode;
        }
    }

    LEAVE("ExitStmt");
    return exitNode;
}

void Builder::enterArmOf(const AST::node_ptr_t &ctrl, bool valueUnused) {
    valueUse_.unusedCtrl  = nullptr;
    valueUse_.blockUnused = valueUnused;
    valueUse_.unusedArmOf = valueUnused ? ctrl : nullptr;
}

void Builder::reportExitInUnusedArm(const AST::node_ptr_t &exit) {
    const AST::node_ptr_t &ctrl = valueUse_.unusedArmOf;
    if (ctrl == nullptr) {
        return;
    }
    // `return null` and a bare `return` are reported like any other: an arm needs no value
    // to compile (design principles 3.7: an `if` without `else` has none, and its arm may
    // be of any type), so one written there was most likely meant to leave the function.
    // One report per statement: the arm of `case a | b` is lowered once for each pattern.
    const auto range = exit->load()->tokenRange();
    if (!reportedExits_.insert({range.start, range.end}).second) {
        return;
    }
    const bool isMatch  = ctrl->loadAs<AST::DataLoad>()->dataType() == AST::DataType::MatchExpr;
    const char *keyword = "return";
    const char *branch  = isMatch ? "match" : "if";
    diags_->of(SemanticDiag::ExitInUnusedBranch)
        .at(range)
        .relatedOrigin(
            ctrl->load()->origin(),
            std::format("the value of this '{}' is not used", branch))
        .commit(keyword, branch);
}

/// An EXEC of the statement block.
node_ptr_t Builder::makeExec(bool synced, bool operands) {
    return tree_->makeExec(synced, operands);
}

/*
StmtBlock(bool sync) : Stmt* stmts ;
*/
node_ptr_t Builder::visitDecoratedBlock(const AST::node_ptr_t &ast) {
    ENTER("DecoratedBlock");
    node_ptr_t execNode = visitStmtBlock(ast, false);
    FunctionTypeFactory factory; // no parameters; the result is the block's value
    FunctionType *type  = factory.buildOpen();
    node_ptr_t typeNode = tree_->makeType(type);
    node_ptr_t funcNode = tree_->makeFunc("");
    funcNode->setDecoratedBlock();
    *funcNode << typeNode << execNode;
    for (const auto &annoAst : *ast->atAs<AST::RepeatedLoad>(1)) {
        GUARD(annoAst->type() == AST::LoadType::Data, "A decorator should lower to AST Data.");
        node_ptr_t annoNode = tree_->makeAnno();
        *annoNode << visitData(annoAst);
        *funcNode << annoNode;
    }
    node_ptr_t linkNode = tree_->makeLink(0);
    *linkNode << funcNode;
    setOriginFromAst(context_, typeNode, ast, camel::source::OriginKind::GctNode, "gct.block.type");
    setOriginFromAst(context_, funcNode, ast, camel::source::OriginKind::GctNode, "gct.block.func");
    setOriginFromAst(context_, linkNode, ast, camel::source::OriginKind::GctNode, "gct.block.call");
    LEAVE("DecoratedBlock");
    return linkNode;
}

node_ptr_t Builder::visitStmtBlock(const AST::node_ptr_t &ast, bool decorators) {
    ENTER("StmtBlock");
    // Ensure the AST node is of the expected type (Stmt)
    GUARD(ast->type() == AST::LoadType::Stmt, "Expected StmtLoad type for StmtBlock");
    // The block's children after the statement list are its decorators (`FuncAnno`) and the
    // tags written before it (`#tag { ... }`); only a decorator list makes it decorated.
    const bool decorated = decorators && ast->size() > 1 &&
                           ast->at(1)->type() == AST::LoadType::Repeated &&
                           ast->at(1)->loadAs<AST::RepeatedLoad>()->nodeName() == "FuncAnno";
    if (decorated) {
        LEAVE("StmtBlock");
        return visitDecoratedBlock(ast);
    }

    // Extract the statement block load and create an ExecLoad node
    // The ExecLoad node represents the execution of the statement block
    const auto &stmtBlock = ast->loadAs<AST::StmtBlockLoad>();
    // Numeric permissions are permission tags, resolved by the graph builder where they are
    // written (design principles 7.2, 7.5): the statement block carries none of its own.
    node_ptr_t execNode = makeExec(stmtBlock->synced());

    // Push a new scope for the statement block
    pushScope();
    // The `using namespace` written in the block apply to the whole block, and to it only
    // (design principles 10.2).
    std::vector<Reference> blockUsings;
    for (const auto &stmt : *ast->atAs<AST::RepeatedLoad>(0)) {
        if (stmt->type() == AST::LoadType::Stmt &&
            stmt->loadAs<AST::StmtLoad>()->stmtType() == AST::StmtType::UsingNamespace) {
            blockUsings.push_back(stmt->loadAs<AST::UsingNamespaceLoad>()->ref());
        }
    }
    tree_->addUsings(execNode, blockUsings);
    struct UsingFrame {
        std::vector<std::vector<Reference>> &frames;
        UsingFrame(std::vector<std::vector<Reference>> &f, std::vector<Reference> u) : frames(f) {
            frames.push_back(std::move(u));
        }
        ~UsingFrame() { frames.pop_back(); }
    } usingFrame(usingFrames_, blockUsings);

    // Whether anything takes the value of this block (see ValueUse). The value of a block is
    // that of its last statement; the value of any other statement nothing takes.
    // (A function or type declaration is not a statement with a value.)
    const bool blockUnused = std::exchange(valueUse_.blockUnused, false);
    const auto &stmts      = ast->atAs<AST::RepeatedLoad>(0);
    const AST::Node *last  = nullptr;
    for (const auto &stmt : *stmts) {
        const auto stmtType = stmt->loadAs<AST::StmtLoad>()->stmtType();
        if (stmtType != AST::StmtType::Func && stmtType != AST::StmtType::Type) {
            last = stmt.get();
        }
    }
    for (const auto &stmt : *stmts) {
        // Ensure each item in the block is a statement
        GUARD(stmt->type() == AST::LoadType::Stmt, "Expected a statement in StmtBlock");
        const bool stmtUnused = blockUnused || stmt.get() != last;
        const auto stmtType   = stmt->loadAs<AST::StmtLoad>()->stmtType();
        valueUse_.unusedCtrl  = nullptr;
        valueUse_.blockUnused = false;
        valueUse_.stmtExpr =
            stmtType == AST::StmtType::Expr && stmt->size() > 0 ? stmt->at(0).get() : nullptr;
        if (stmtUnused && stmtType == AST::StmtType::Expr && stmt->size() > 0) {
            // An `if` / `match` written as a statement whose value nothing takes.
            const auto &expr = stmt->at(0);
            const auto kind  = expr->type() == AST::LoadType::Data
                                   ? expr->loadAs<AST::DataLoad>()->dataType()
                                   : AST::DataType::Literal;
            if (kind == AST::DataType::IfExpr || kind == AST::DataType::MatchExpr) {
                valueUse_.unusedCtrl = expr.get();
            }
        } else if (stmtType == AST::StmtType::Block) {
            // A block written as a statement is part of this body: its exits are exits of
            // the enclosing arm, and its value is used when the statement's is.
            valueUse_.blockUnused = stmtUnused;
        }
        // Process the statement and attach it to the ExecLoad node
        *execNode << visitStmt(stmt);
    }
    valueUse_.unusedCtrl  = nullptr;
    valueUse_.blockUnused = false;

    // Pop the scope after processing the block
    popScope();

    LEAVE("StmtBlock");
    setOriginFromAst(context_, execNode, ast, camel::source::OriginKind::GctNode, "gct.block");
    registerGctSemantic(context_, execNode, ast);
    // The tags written before the block (`#kernel { ... }`): they hold for the statements
    // written in it (design principles 6.2).
    attachTagUses(execNode, ast);
    // Return the ExecLoad node representing the processed statement block
    return execNode;
}

node_ptr_t Builder::visitData(const AST::node_ptr_t &ast) {
    ENTER("Data");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for visitData");
    const auto &data    = ast->loadAs<AST::DataLoad>();
    node_ptr_t dataNode = nullptr;

    switch (data->dataType()) {
    case AST::DataType::UnaryExpr:
        dataNode = visitUnaryExpr(ast);
        break;
    case AST::DataType::BinaryExpr:
        dataNode = visitBinaryExpr(ast);
        break;
    case AST::DataType::ReservedExpr:
        dataNode = visitReservedExpr(ast);
        break;
    case AST::DataType::IfExpr:
        dataNode = visitIfExpr(ast);
        break;
    case AST::DataType::MatchExpr:
        dataNode = visitMatchExpr(ast);
        break;
    case AST::DataType::TypeMatch:
        dataNode = visitMatchType(ast);
        break;
    case AST::DataType::StaticFor:
        dataNode = visitStaticFor(ast);
        break;
    case AST::DataType::StaticLoop:
        dataNode = visitStaticLoop(ast);
        break;
    case AST::DataType::StaticAccess:
        dataNode = visitStaticAccess(ast);
        break;
    case AST::DataType::LoopExpr:
        dataNode = visitLoopExpr(ast);
        break;
    case AST::DataType::Literal:
        dataNode = visitLiteral(ast);
        break;
    case AST::DataType::Array:
        dataNode = visitArrayData(ast);
        break;
    case AST::DataType::Struct:
        dataNode = visitStructData(ast);
        break;
    case AST::DataType::Tuple:
        dataNode = visitTupleData(ast);
        break;
    case AST::DataType::SpreadArg:
        dataNode = visitSpreadArg(ast);
        break;
    case AST::DataType::StructComp:
        dataNode = visitStaticComprehension(ast, true);
        break;
    case AST::DataType::TupleComp:
        dataNode = visitStaticComprehension(ast, false);
        break;
    case AST::DataType::ShapeComp:
        dataNode = visitShapeComprehension(ast);
        break;
    case AST::DataType::Func:
        dataNode = visitFuncData(ast);
        break;
    case AST::DataType::Ref:
        dataNode = visitRefData(ast);
        break;
    case AST::DataType::ConstAs:
        // Only the expression of a `const` type argument can hold one, and visitSpecType reads
        // it before visiting the expression as data.
        diags_->of(SemanticDiag::ConstasNotAllowed)
            .at(ast->load()->tokenRange())
            .commit(ast->loadAs<AST::ConstAsLoad>()->ref().toString());
        throw BuildAbortException();
    default:
        throw std::runtime_error("Unknown data type");
    }

    LEAVE("Data");
    // A write's parts are the write's: what names its two sides and the place of the operator it
    // was written with is what it said itself (visitBinaryExpr), and the AST node here is one
    // expression that stands for both sides at once. Only the place the node stands at follows the
    // AST node, as it does for every other data node.
    const auto ownParts = ownSemanticParts(context_, dataNode);
    setOriginFromAst(context_, dataNode, ast, camel::source::OriginKind::GctNode, "gct.data");
    registerGctSemantic(context_, dataNode, ast, ownParts.parts, ownParts.inputs);
    return dataNode;
}

/*
enum UnaryDataOp {
    Not,
    Neg,
    Inv
}
UnaryExpr(UnaryDataOp op) := Data data ;
*/
node_ptr_t Builder::visitUnaryExpr(const AST::node_ptr_t &ast) {
    ENTER("UnaryExpr");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for UnaryExpr");

    // Extract the unary expression load and the associated data node
    const auto &unaryExpr   = ast->loadAs<AST::UnaryExprLoad>();
    const auto &dataASTNode = ast->atAs<AST::DataLoad>(0);

    node_ptr_t opNode = nullptr;

    // Determine the unary operator and create the corresponding node
    switch (unaryExpr->op()) {
    case AST::UnaryDataOp::Not: {
        opNode = tree_->makeDRef("__not__");
    } break;
    case AST::UnaryDataOp::Neg: {
        opNode = tree_->makeDRef("__neg__");
    } break;
    case AST::UnaryDataOp::Inv: {
        opNode = tree_->makeDRef("__inv__");
    } break;
    default:
        // If the operator is unknown, assert and return nullptr
        throw std::runtime_error("Unknown unary operation");
        return nullptr;
    }
    auto unaryAstBundle = astSemantic(context_, ast);
    setOriginFromOrigin(
        context_,
        opNode,
        findSemanticOrigin(unaryAstBundle, camel::source::SemanticRole::Operator),
        camel::source::OriginKind::GctNode,
        "gct.unary.operator");

    // Create a LinkLoad node to link the operator and the operand
    node_ptr_t operandNode = visitData(dataASTNode);
    node_ptr_t linkNode    = tree_->makeLink(1);
    *linkNode << opNode << operandNode;
    setOriginFromAst(context_, linkNode, ast, camel::source::OriginKind::GctNode, "gct.unary.link");
    registerGctSemantic(
        context_,
        linkNode,
        ast,
        {
            semanticPart(camel::source::SemanticRole::Operator, nodeOrigin(opNode), -1, "operator"),
            semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(operandNode),
                0,
                "operand"),
        },
        {nodeOrigin(opNode), nodeOrigin(operandNode)});

    LEAVE("UnaryExpr");
    // Return the constructed LinkLoad node
    return linkNode;
}

/*
enum BinaryDataOp {
    Assign,
    AssignAdd,
    AssignSub,
    AssignMul,
    AssignDiv,
    AssignMod,
    AssignMat,
    AssignExp,
    AssignAnd,
    AssignOr,
    Or,
    And,
    Eq,
    Neq,
    StrictEq,
    StrictNeq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Mat,
    Pow,
    Index, // Data obj, Data* indices
}
BinaryExpr(BinaryDataOp op) := Data lhs, Data rhs ;
*/
node_ptr_t Builder::visitBinaryExpr(const AST::node_ptr_t &ast) {
    ENTER("BinaryExpr");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for BinaryExpr");

    // Extract the binary expression load and the left-hand side (LHS) AST node
    const auto &binaryExpr = ast->loadAs<AST::BinaryExprLoad>();
    const auto &lhsASTNode = ast->atAs<AST::DataLoad>(0);

    node_ptr_t opNode = nullptr;

    // A subscript writes each dimension of the value it reads: a position `i`, a range `a:b`
    // (either end left out, a step after a second colon) or an integer array `idx` (design
    // principles 5.4). A dimension written `:` names nothing and is kept whole. Every written
    // dimension is one node of the core's own -- the index along that dimension for a position or
    // an integer array, which drops the dimension, and the slice along it for a range, which keeps
    // it -- so they are collected here in the order the subscript wrote them and built from the
    // last one back.
    struct SubscriptDimension {
        size_t axis          = 0;       // the dimension of the value the subscript writes on
        AST::node_ptr_t item = nullptr; // the dimension as written
        bool namesRange      = false;   // its load names a bound: a range, not a position
    };
    std::vector<SubscriptDimension> writtenDimensions;
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        size_t dimension = 0;
        for (const auto &item : *ast->atAs<AST::RepeatedLoad>(1)) {
            const size_t at = dimension++;
            if (item->type() != AST::LoadType::Data ||
                item->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Slice) {
                writtenDimensions.push_back({at, item, false});
                continue;
            }
            bool namesRange = false;
            for (size_t part = 0; part < 3; ++part) {
                namesRange = namesRange || item->at(part)->type() != AST::LoadType::Null;
            }
            if (namesRange) {
                writtenDimensions.push_back({at, item, true});
            }
        }
    }

    // A subscript is one node per written dimension, from the last dimension written back to the
    // first (design principles 5.4). Going backwards is what keeps the axis of every dimension the
    // one the subscript gave it: a node takes or slices one dimension and leaves the number of
    // every dimension before it as it was, so `t[a:b, c]` slices on axis 0 and then takes the
    // position on axis 1 of that slice, and the reading of the sliced dimension is the same one
    // `t[a:b]` has. The dimensions the subscript does not write are kept whole and stay where they
    // are; the axis of a written dimension is the position it has in the subscript, counted from
    // the first dimension, so `t[i]` -- the first dimension's position -- is the node a two-word
    // call is.
    if (binaryExpr->op() == AST::BinaryDataOp::Index) {
        const auto intConstant = [this](int64_t value) {
            return tree_->makeData(makeDataFromLiteral<int64_t>(value));
        };
        // The operator of one dimension's node: the subscript's own operator, which is what a
        // diagnostic about any of the dimensions it wrote points at.
        const auto makeOperator = [&](const char *group) {
            node_ptr_t node = tree_->makeDRef(group);
            setOriginFromOrigin(
                context_,
                node,
                findSemanticOrigin(
                    astSemantic(context_, ast),
                    camel::source::SemanticRole::Operator),
                camel::source::OriginKind::GctNode,
                "gct.binary.operator");
            return node;
        };
        // A dimension written as a position or an integer array: the index node along that
        // dimension, which is the value with the dimension dropped. The axis is the node's third
        // word and is written only when it is not the first dimension, so `t[i]` is the two-word
        // call the module declarations of `__idx__` carry and the adjoints' call sites write.
        const auto buildIndex = [&](const SubscriptDimension &dim, const node_ptr_t &base) {
            node_ptr_t indexNode = visitData(dim.item);
            node_ptr_t opNode    = makeOperator("__idx__");
            node_ptr_t node      = tree_->makeLink(2);
            *node << opNode << base << indexNode;
            if (dim.axis != 0) {
                *node << intConstant(static_cast<int64_t>(dim.axis));
            }
            setOriginFromAst(
                context_,
                node,
                ast,
                camel::source::OriginKind::GctNode,
                "gct.binary.link");
            registerGctSemantic(
                context_,
                node,
                ast,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        nodeOrigin(opNode),
                        -1,
                        "operator"),
                    semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(base), 0, "lhs"),
                    semanticPart(
                        camel::source::SemanticRole::IndexExpr,
                        nodeOrigin(indexNode),
                        0,
                        "index"),
                },
                {nodeOrigin(opNode), nodeOrigin(base), nodeOrigin(indexNode)});
            return node;
        };
        // A dimension written as a range: the slice node along it, which is the value with that
        // dimension taking the positions the range names. The call is the slice node itself: the
        // value, the axis, the two bounds, the step, and a two-bit mask (bit 0 end, bit 1 start)
        // saying which of the start and the end the program wrote -- `t[a:]` and `t[:b]` differ,
        // and a negative step needs to know whether the start was given. A part that was not
        // written is passed as the value it stands for, 0 for the start and 1 for the step; the
        // end is read only when written.
        const auto buildSlice = [&](const SubscriptDimension &dim, const node_ptr_t &base) {
            const AST::node_ptr_t &boundsOf = dim.item;
            node_ptr_t bound[3]             = {nullptr, nullptr, nullptr};
            bool written[3]                 = {false, false, false};
            for (size_t i = 0; i < 3; ++i) {
                written[i] = boundsOf != nullptr && boundsOf->at(i)->type() != AST::LoadType::Null;
                if (written[i]) {
                    bound[i] = visitData(boundsOf->at(i));
                }
            }
            bound[0]                  = written[0] ? bound[0] : intConstant(0);
            bound[1]                  = written[1] ? bound[1] : intConstant(0);
            bound[2]                  = written[2] ? bound[2] : intConstant(1);
            node_ptr_t endWrittenNode = intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0));
            node_ptr_t opNode         = makeOperator("__slice__");
            node_ptr_t node           = tree_->makeLink(2);
            *node << opNode << base << intConstant(static_cast<int64_t>(dim.axis));
            std::vector<camel::source::SemanticPart> parts = {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    nodeOrigin(opNode),
                    -1,
                    "operator"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(base), 0, "lhs"),
            };
            std::vector<camel::source::origin_id_t> inputs = {nodeOrigin(opNode), nodeOrigin(base)};
            for (size_t i = 0; i < 3; ++i) {
                *node << bound[i];
                if (written[i]) {
                    inputs.push_back(nodeOrigin(bound[i]));
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::IndexExpr,
                        nodeOrigin(bound[i]),
                        static_cast<int32_t>(i),
                        i == 0 ? "start" : (i == 1 ? "end" : "step")));
                }
            }
            *node << endWrittenNode;
            setOriginFromAst(
                context_,
                node,
                ast,
                camel::source::OriginKind::GctNode,
                "gct.binary.link");
            registerGctSemantic(context_, node, ast, std::move(parts), std::move(inputs));
            return node;
        };
        if (writtenDimensions.empty()) {
            // A subscript that writes no bound anywhere (`t[:]`, `t[:, :]`) keeps every dimension,
            // which is the whole slice of the first axis: the same value, and the one node an
            // all-`:` subscript builds.
            writtenDimensions.push_back({0, nullptr, true});
        }
        node_ptr_t value = visitData(lhsASTNode);
        for (size_t i = writtenDimensions.size(); i-- > 0;) {
            const SubscriptDimension &dim = writtenDimensions[i];
            value = dim.namesRange ? buildSlice(dim, value) : buildIndex(dim, value);
        }
        LEAVE("BinaryExpr");
        return value;
    }

    // Determine the binary operator and create the corresponding node
    const bool negatedStrictNeq = binaryExpr->op() == AST::BinaryDataOp::StrictNeq;
    switch (binaryExpr->op()) {
    case AST::BinaryDataOp::LogicalOr:
    case AST::BinaryDataOp::LogicalAnd: {
        // `||` and `&&` are branches, not operators (design principles 4.2).
        node_ptr_t res = visitLogicalExpr(ast);
        LEAVE("BinaryExpr");
        return res;
    }
    case AST::BinaryDataOp::StrictEq: {
        // `a === b` is the elementwise equality (design principles 4.2): the group holding the
        // scalar comparison, which the tensor module also exports its array entry into. The
        // whole comparison is `==`, the reduction of this one, and is a different group.
        opNode = tree_->makeDRef("__strict_eq__");
    } break;
    case AST::BinaryDataOp::StrictNeq: {
        // `a !== b` is the negation of `a === b` (design principles 4.2): the comparison built
        // here is `===`'s and the negation is wrapped around its call below, so the operands are
        // evaluated once and the result is negated element by element. It is built here rather
        // than in the builder -- where `!=` becomes `!(==)` -- because `!==` is not an
        // overloadable form (operator_names.cpp): no name is looked up for it, so it never
        // reaches the code that would give it a written group to negate.
        opNode = tree_->makeDRef("__strict_eq__");
    } break;
    case AST::BinaryDataOp::Index:
        // A subscript is built above, one node per dimension it writes (design principles 5.4).
        throw std::runtime_error("A subscript reached the ordinary binary operands");
        return nullptr;
    default: {
        // Every other operator is a call of the group the table of operator_names.cpp gives
        // its symbol: plain and compound alike.
        const auto group = camel::core::binaryOperatorGroup(AST::to_string(binaryExpr->op()));
        GUARD(group.has_value(), "Unknown binary operation");
        if (!group)
            return nullptr;
        const std::string groupName(*group);
        // `a = b` and `a op= b` write the left side; they are not calls (design principles 3.5).
        // One node of kind ASSIGN carries the two sides, and a compound assignment carries the
        // group of the operator it follows: that operator's set of operand types is what decides
        // whether the write is defined, so it is the operator the graph builder asks. Which group
        // is the plain assignment, and which are the compound ones, is the table in
        // `operator_names.cpp`: this line asks it instead of
        // spelling the name.
        const bool compound =
            camel::core::plainGroupOfCompoundAssignment(groupName).has_value();
        if (groupName == camel::core::kAssignmentGroup || compound) {
            const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
            node_ptr_t target      = visitData(lhsASTNode);
            node_ptr_t value       = visitData(rhsASTNode);
            node_ptr_t write       = tree_->makeAssign(
                compound ? AssignmentForm::Compound : AssignmentForm::Plain,
                compound ? std::string_view(groupName) : std::string_view());
            *write << target << value;
            const auto operatorOrigin = findSemanticOrigin(
                astSemantic(context_, ast), camel::source::SemanticRole::Operator);
            // The write stands where the statement does (`x += 5`), and it keeps the operator's
            // own place as a part of what it stands for: the token the program wrote, which is
            // where a fault of the operator is reported (a `%=` by zero is a DivisionByZero at
            // the `%=`, test/plans/feat/linear/operators.plan.toml). The store the write makes
            // reads that part back, as a call reads its callee's place from the reference it was
            // written with (Builder::storeOperatorOrigin): a node carries one origin, so the
            // operator's place is a part of the bundle rather than the node's own origin.
            setOriginFromAst(
                context_,
                write,
                ast,
                camel::source::OriginKind::GctNode,
                "gct.data");
            registerGctSemantic(
                context_,
                write,
                ast,
                {semanticPart(
                     camel::source::SemanticRole::Operator, operatorOrigin, -1, "operator"),
                 semanticPart(
                     camel::source::SemanticRole::Argument, nodeOrigin(target), 0, "lhs"),
                 semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(value), 1, "rhs")},
                {operatorOrigin, nodeOrigin(target), nodeOrigin(value)});
            LEAVE("BinaryExpr");
            return write;
        }
        opNode = tree_->makeDRef(groupName);
    } break;
    }
    auto binaryAstBundle = astSemantic(context_, ast);
    setOriginFromOrigin(
        context_,
        opNode,
        findSemanticOrigin(binaryAstBundle, camel::source::SemanticRole::Operator),
        camel::source::OriginKind::GctNode,
        "gct.binary.operator");

    // Create a LinkLoad node to link the operator and the operands
    node_ptr_t res     = tree_->makeLink(2);
    node_ptr_t lhsNode = visitData(lhsASTNode);
    *res << opNode << lhsNode;
    std::vector<camel::source::SemanticPart> parts = {
        semanticPart(camel::source::SemanticRole::Operator, nodeOrigin(opNode), -1, "operator"),
        semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
    };
    std::vector<camel::source::origin_id_t> mergedInputs = {
        nodeOrigin(opNode),
        nodeOrigin(lhsNode)};

    // The right-hand side of an ordinary binary operator. A subscript never reaches here: it is
    // built above, one node per dimension it writes (design principles 5.4).
    {
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t rhsNode     = visitData(rhsASTNode);
        *res << rhsNode;
        mergedInputs.push_back(nodeOrigin(rhsNode));
        parts.push_back(
            semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"));
    }
    setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.binary.link");
    registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));

    if (negatedStrictNeq) {
        // `!(a === b)`: one call of `!` over the comparison linked above, so the operands are
        // evaluated once. The negation carries the operator's origin, so a diagnostic about it
        // points at `!==` and not at a name the program did not write.
        node_ptr_t notOperator = tree_->makeDRef("__not__");
        setOriginFromOrigin(
            context_,
            notOperator,
            nodeOrigin(opNode),
            camel::source::OriginKind::GctNode,
            "gct.binary.negation");
        node_ptr_t negation = tree_->makeLink(1);
        *negation << notOperator << res;
        setOriginFromAst(
            context_,
            negation,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.binary.negation");
        res = negation;
    }

    LEAVE("BinaryExpr");
    return res;
}

/*
`a && b` is `if a then b else false`, `a || b` is `if a then true else b` (design principles
2.11): a branch on the left side with the right side as one arm, so the right side is
evaluated only when the left does not decide. The node built has the children the `if` would
have; its kind keeps what was written (see BranchKind). Neither can be overloaded: no
name is looked up for them. The eager, overloadable pair is `&` and `|` (`__and__`,
`__or__`).
*/
node_ptr_t Builder::visitLogicalExpr(const AST::node_ptr_t &ast) {
    ENTER("LogicalExpr");
    const auto &logicalExpr = ast->loadAs<AST::BinaryExprLoad>();
    const bool isAnd        = logicalExpr->op() == AST::BinaryDataOp::LogicalAnd;

    node_ptr_t brchNode = tree_->makeBrch(isAnd ? BranchKind::LogicalAnd : BranchKind::LogicalOr);
    // Located where the binary expression is, like the operators `&` and `|`.
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.logical");

    node_ptr_t lhsNode = visitData(ast->atAs<AST::DataLoad>(0));
    node_ptr_t rhsNode = visitData(ast->atAs<AST::DataLoad>(1));
    // The arm that is not the right side: the value the left side has already decided.
    node_ptr_t decidedNode =
        tree_->makeData(tt::as_shared<Data>(make_shared<PrimaryData<bool>>(!isAnd)));
    setOriginFromAst(
        context_,
        decidedNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.logical.decided");

    const auto armOf = [&](CaseType caseType, node_ptr_t value) {
        node_ptr_t execNode = makeExec();
        setOriginFromOrigin(
            context_,
            execNode,
            nodeOrigin(value),
            camel::source::OriginKind::GctNode,
            "gct.logical.arm");
        *execNode << value;
        node_ptr_t caseNode = tree_->makeCase(caseType);
        *caseNode << execNode;
        return caseNode;
    };
    *brchNode << lhsNode << armOf(CaseType::True, isAnd ? rhsNode : decidedNode)
              << armOf(CaseType::Else, isAnd ? decidedNode : rhsNode);

    node_ptr_t res = brchNode;
    LEAVE("LogicalExpr");
    return res;
}

/*
enum ReservedDataOp {
    NullThen,
    NullThen,
    ErrorThen,
    Call, // Data obj, Data* args, NamedData* kwargs
    Bind, // Data obj, Data* args, NamedData* kwargs
    Comp, // Data lhs, Data rhs -- `a..b`, two readings
    Compose, // Data lhs, Data rhs -- `a<>b`, the composition alone
    As, // Data lhs, Type rhs
    Access, // Data obj, RefData ref
}
ReservedExpr(ReservedDataOp op) := Data lhs, Any? rhs ;
*/
node_ptr_t Builder::visitReservedExpr(const AST::node_ptr_t &ast) {
    ENTER("ReservedExpr");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ReservedExpr");

    node_ptr_t res           = nullptr;
    const auto &reservedExpr = ast->loadAs<AST::ReservedExprLoad>();
    const auto &lhsASTNode   = ast->atAs<AST::DataLoad>(0);

    // Handle different reserved operators
    switch (reservedExpr->op()) {
    case AST::ReservedDataOp::NullThen:
    case AST::ReservedDataOp::ErrorThen: {
        // `a ?? b` / `a !! b`: a branch on the tag of `a` whose first arm is `b` (design
        // principles 3.9). The graph builder knows the types; it adds the arm that takes the
        // value out of `a`.
        const bool onNull = reservedExpr->op() == AST::ReservedDataOp::NullThen;
        res               = tree_->makeBrch(onNull ? BranchKind::NullThen : BranchKind::ErrorThen);
        // The node is located at the operator.
        setOriginFromAst(
            context_,
            res,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.tagged.branch");
        node_ptr_t lhsNode  = visitData(lhsASTNode);
        node_ptr_t rhsNode  = visitData(ast->atAs<AST::DataLoad>(1));
        node_ptr_t execNode = makeExec();
        setOriginFromOrigin(
            context_,
            execNode,
            nodeOrigin(rhsNode),
            camel::source::OriginKind::GctNode,
            "gct.tagged.fallback");
        *execNode << rhsNode;
        node_ptr_t caseNode = tree_->makeCase(CaseType::True);
        *caseNode << execNode;
        *res << lhsNode << caseNode;
    } break;
    case AST::ReservedDataOp::Call: {
        // Handle the "Call" operator
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        // A positional argument written after a named one is refused where the call is built:
        // the parser keeps it apart (an argument list writes its positional arguments first),
        // and the call names itself in the report (design principles 4.4).
        if (const camel::source::origin_id_t stray = reservedExpr->positionalAfterNamed();
            stray != camel::source::kInvalidOriginId) {
            AST::node_ptr_t at = lhsASTNode;
            while (at != nullptr && at->type() == AST::LoadType::Data &&
                   at->loadAs<AST::DataLoad>()->dataType() == AST::DataType::ReservedExpr &&
                   at->loadAs<AST::ReservedExprLoad>()->op() == AST::ReservedDataOp::Bind &&
                   at->size() > 0) {
                at = at->at(0);
            }
            const std::string name =
                at != nullptr && at->type() == AST::LoadType::Data &&
                        at->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Ref
                    ? at->loadAs<AST::RefDataLoad>()->ref().toString()
                    : std::string("the call");
            diags_->of(SemanticDiag::ParamArgumentPositionalAfterNamed)
                .atOrigin(stray)
                .commit(name);
            throw BuildAbortException();
        }
        // `..(1, 2)` is the two arguments written out: a tuple literal is expanded here,
        // where its elements are still AST nodes. Any other `..e` keeps its marker for GIR,
        // which expands a `..ts` rest binding or a tuple comprehension.
        const auto spreadLiteral = [](const AST::node_ptr_t &arg) -> AST::node_ptr_t {
            if (arg->type() != AST::LoadType::Data ||
                arg->loadAs<AST::DataLoad>()->dataType() != AST::DataType::SpreadArg) {
                return nullptr;
            }
            const auto &operand = arg->atAs<AST::DataLoad>(0);
            if (operand->type() != AST::LoadType::Data ||
                operand->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Tuple) {
                return nullptr;
            }
            return operand;
        };
        size_t argCount = 0;
        for (const auto &argNode : *argsNode) {
            const AST::node_ptr_t literal = spreadLiteral(argNode);
            argCount += literal != nullptr ? literal->atAs<AST::RepeatedLoad>(0)->size() : 1;
        }
        res = tree_->makeLink(argCount);
        if (reservedExpr->pipe()) {
            tree_->markPipe(res);
        }
        node_ptr_t calleeNode = visitData(lhsASTNode);
        *res << calleeNode;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
        };
        std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(calleeNode)};
        int32_t argSlot                                      = 0;
        // The name of each argument, aligned with the arguments in the order written: empty for
        // a positional one. The name is not a child of the LINK; the call checks it against the
        // callee when the call is resolved (design principles 4.4).
        std::vector<std::optional<std::string>> argNames;
        const auto appendArgument = [&](node_ptr_t gctArg) {
            *res << gctArg;
            argNames.push_back(std::nullopt);
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(gctArg),
                argSlot++,
                "arg"));
        };
        for (size_t ai = 0; ai < argsNode->size(); ++ai) {
            const auto &argNode = argsNode->at(ai);
            if (const AST::node_ptr_t literal = spreadLiteral(argNode); literal != nullptr) {
                if (ai + 1 != argsNode->size()) {
                    // `..e` stands for every argument it leaves: nothing can follow it.
                    const auto &operand = argNode->atAs<AST::DataLoad>(0);
                    diags_->of(SemanticDiag::RestArgumentNotLast)
                        .atOrigin(operand->load()->origin())
                        .commit(
                            operand->type() == AST::LoadType::Data &&
                                    operand->loadAs<AST::DataLoad>()->dataType() ==
                                        AST::DataType::Ref
                                ? operand->loadAs<AST::RefDataLoad>()->ref().toString()
                                : std::string("expression"));
                    throw BuildAbortException();
                }
                for (const auto &element : *literal->atAs<AST::RepeatedLoad>(0)) {
                    appendArgument(visitData(element));
                }
                continue;
            }
            node_ptr_t gctArg = visitData(argNode);
            appendArgument(gctArg);
        }
        for (const auto &argNode : *kwargsNode) {
            // `f(1, by: 2)`: an ordinary argument given by name (design principles 4.4). The
            // value is an argument of the call like any other; the name is kept beside it and
            // resolved against the callee's declaration when the call is built.
            const auto &named = argNode->loadAs<AST::NamedDataLoad>();
            node_ptr_t gctArg = visitData(argNode->at(0));
            *res << gctArg;
            argNames.push_back(named->ref().ident());
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(gctArg),
                argSlot++,
                "arg"));
        }
        tree_->setArgumentNames(res, std::move(argNames));
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.call");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
    } break;

    case AST::ReservedDataOp::Instantiate: {
        res = tree_->makeTypeApplication();
        *res << visitData(lhsASTNode);
        for (const auto &argument : *ast->atAs<AST::RepeatedLoad>(1)) {
            if (argument->type() == AST::LoadType::NamedType) {
                // `A = int`: the name as a constant, then the type (design principles 5.9).
                const auto &named = argument->loadAs<AST::NamedTypeLoad>();
                *res << tree_->makeData(std::make_shared<StringData>(named->getRef().toString()));
                node_ptr_t namedType = tree_->makeType(visitType(argument->at(0)));
                setOriginFromAst(
                    context_,
                    namedType,
                    argument->at(0),
                    camel::source::OriginKind::GctNode,
                    "gct.type_application.argument");
                *res << namedType;
                continue;
            }
            Type *type          = visitType(argument);
            node_ptr_t typeNode = tree_->makeType(type);
            setOriginFromAst(
                context_,
                typeNode,
                argument,
                camel::source::OriginKind::GctNode,
                "gct.type_application.argument");
            *res << typeNode;
        }
        setOriginFromAst(
            context_,
            res,
            ast,
            camel::source::OriginKind::GctNode,
            "gct.type_application");
    } break;

    case AST::ReservedDataOp::Bind: {
        const auto &argsNode   = ast->atAs<AST::RepeatedLoad>(1);
        const auto &kwargsNode = ast->atAs<AST::RepeatedLoad>(2);
        res                    = tree_->makeWith(argsNode->size() + kwargsNode->size());
        node_ptr_t calleeNode  = visitData(lhsASTNode);
        *res << calleeNode;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
        };
        std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(calleeNode)};
        int32_t argSlot                                      = 0;
        // The name of each argument, aligned with the arguments; empty for a positional one.
        // The name is not a child of the WITH: the binding checks it against the callee when
        // the call is resolved (design principles 4.4).
        std::vector<std::optional<std::string>> argNames;
        for (auto &argNode : *argsNode) {
            node_ptr_t gctArg = visitData(argNode);
            *res << gctArg;
            argNames.push_back(std::nullopt);
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::GenericArgument,
                nodeOrigin(gctArg),
                argSlot++,
                "withArg"));
        }
        for (auto &argNode : *kwargsNode) {
            const auto &named = argNode->loadAs<AST::NamedDataLoad>();
            node_ptr_t gctArg = visitData(argNode->at(0));
            *res << gctArg;
            argNames.push_back(named->ref().ident());
            mergedInputs.push_back(nodeOrigin(gctArg));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::GenericArgument,
                nodeOrigin(gctArg),
                argSlot++,
                "withArg"));
        }
        tree_->setArgumentNames(res, std::move(argNames));
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.bind");
        registerGctSemantic(context_, res, ast, std::move(parts), std::move(mergedInputs));
    } break;

    case AST::ReservedDataOp::Comp:
    case AST::ReservedDataOp::Compose: {
        // `a..b` has two readings and `a<>b` has one (design principles 4.2). GCT has no types,
        // so the construct goes on as one call of the compiler's own name for it, which the GIR
        // builder resolves: `Comp` keeps both readings while the corpus still writes the
        // composition as `..`, and `Compose` is the composition alone.
        //
        // It is not handed to the built-in macro operator `oper::MacroCmp` (`:macro/cmp`,
        // src/builtin/operators/macro.cpp), whose resolver takes two functions and answers with
        // the exit type of the right one. That path cannot build a composition at all — a
        // value of a function type is a graph, and no built-in executor has one — and it has no
        // reading for two integers. The operator and its executor stay registered; the compiler
        // has no user for them.
        const bool compose = reservedExpr->op() == AST::ReservedDataOp::Compose;
        const auto &rhsASTNode = ast->atAs<AST::DataLoad>(1);
        node_ptr_t opNode  = tree_->makeDRef(std::string(
            compose ? camel::core::kComposeCall : camel::core::kCompositionCall));
        res                = tree_->makeLink(2);
        auto compAstBundle = astSemantic(context_, ast);
        setOriginFromOrigin(
            context_,
            opNode,
            findSemanticOrigin(compAstBundle, camel::source::SemanticRole::Operator),
            camel::source::OriginKind::GctNode,
            "gct.comp.operator");
        node_ptr_t lhsNode = visitData(lhsASTNode);
        node_ptr_t rhsNode = visitData(rhsASTNode);
        *res << opNode << lhsNode << rhsNode;
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.comp");
        registerGctSemantic(
            context_,
            res,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    nodeOrigin(opNode),
                    -1,
                    "operator"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            },
            {nodeOrigin(opNode), nodeOrigin(lhsNode), nodeOrigin(rhsNode)});
    } break;

    case AST::ReservedDataOp::As: {
        const auto &rhsASTNode = ast->atAs<AST::TypeLoad>(1);
        Type *dstType          = nullptr;
        {
            dstType = visitType(rhsASTNode);
        }
        // `[1, null] as (int?)[]`: the literal takes the form of its elements from the type.
        bool enteredLiteral  = false;
        const auto &dataNode = visitDataInto(lhsASTNode, dstType, enteredLiteral);
        if (enteredLiteral) {
            res = dataNode;
            break;
        }
        // A conversion to a type written with a type variable is decided when the generic
        // function is instantiated, for a literal as for any other value.
        // A conversion into `T?` / `T!` is a CAST node for a literal too: the value is put
        // into the form when the program runs.
        // A conversion into a Map or a Set builds the collection when the program runs (a
        // CAST), for a literal too; what can be said about the literal is said here.
        const bool intoCollection = asCollectionTypeOrPrototype(dstType) != nullptr;
        if (intoCollection && dataNode->type() == NodeKind::DATA) {
            rejectDuplicateLiteralKeys(dataNode->data(), dstType, ast);
        }
        if (dataNode->type() == NodeKind::DATA && !containsTypeVar(dstType) &&
            asTagged(dstType) == nullptr && !intoCollection) {
            const auto &data        = dataNode->data();
            const CastSafety safety = dstType->castSafetyFrom(data->type());
            if (safety == CastSafety::Forbidden && data->type()->isPrimitive() &&
                dstType->isPrimitive()) {
                // The one table of `as` (Type::castSafetyFrom) refuses a literal as it refuses a
                // variable: `5 as float` and `n as float` are the same answer (design principles
                // 5.1). A literal takes the type of its position; a number of another type is made
                // by the conversion function.
                diags_->of(SemanticDiag::DynamicCastForbidden)
                    .at(ast->load()->tokenRange())
                    .commit(data->type()->toString(), dstType->toString());
                throw BuildAbortException();
            }
            if (safety != CastSafety::Safe) {
                reportLiteralCastFailure(
                    diags_,
                    ast->load()->tokenRange(),
                    data,
                    dstType,
                    {},
                    /*explicitCast=*/true);
                throw BuildAbortException();
            }
            auto convertedData = data->convertTo(dstType);
            if (convertedData) {
                res = tree_->makeData(convertedData);
            } else {
                reportLiteralCastFailure(
                    diags_,
                    ast->load()->tokenRange(),
                    data,
                    dstType,
                    {},
                    /*explicitCast=*/true);
                throw BuildAbortException();
            }
        } else {
            res = tree_->makeCast(dstType);
            *res << dataNode;
            setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.cast");
            registerGctSemantic(
                context_,
                res,
                ast,
                {
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(dataNode),
                        -1,
                        "value"),
                    semanticPart(
                        camel::source::SemanticRole::ReturnType,
                        findSemanticOrigin(
                            astSemantic(context_, ast),
                            camel::source::SemanticRole::ReturnType),
                        -1,
                        "type"),
                },
                {nodeOrigin(dataNode)});
        }
    } break;

    case AST::ReservedDataOp::Access: {
        const auto &refASTNode  = ast->atAs<AST::RefDataLoad>(1);
        const auto &refDataLoad = refASTNode->loadAs<AST::RefDataLoad>();
        const auto &ref         = refDataLoad->ref();
        try {
            size_t index = std::stoul(ref.ident());
            res          = tree_->makeAccs(index);
        } catch (const std::exception &) {
            // Not an index, treat as named access
            res = tree_->makeAccs(ref);
        }
        node_ptr_t targetNode = visitData(lhsASTNode);
        *res << targetNode;
        setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.access");
        registerGctSemantic(
            context_,
            res,
            ast,
            {
                semanticPart(
                    camel::source::SemanticRole::Receiver,
                    nodeOrigin(targetNode),
                    -1,
                    "target"),
                semanticPart(
                    camel::source::SemanticRole::MemberName,
                    findSemanticOrigin(
                        astSemantic(context_, ast),
                        camel::source::SemanticRole::MemberName),
                    -1,
                    ref.ident()),
            },
            {nodeOrigin(targetNode)});
    } break;

    default:
        throw std::runtime_error("Unknown reserved operation");
        res = nullptr;
    }

    LEAVE("ReservedExpr");
    return res;
}
/*
IfExpr() : Data cond, StmtBlock then, StmtBlock? else ;
*/
node_ptr_t Builder::visitIfExpr(const AST::node_ptr_t &ast) {
    ENTER("IfExpr");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for IfExpr");

    // Create a branch node to represent the conditional expression
    node_ptr_t brchNode = tree_->makeBrch();
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.if");

    // Whether this `if` is a statement whose value nothing takes: its arms are then lowered
    // as arms of an unused branch (see ValueUse).
    const bool valueUnused = std::exchange(valueUse_.unusedCtrl, nullptr) == ast.get();
    // Design principles 3.7: an `if` without `else` has no value, so it can only stand as
    // a statement. (As the last statement of a block it gives the block no value either;
    // what then expects one reports the missing value by the type.)
    const bool isStatement = std::exchange(valueUse_.stmtExpr, nullptr) == ast.get();
    // The AST gives an `if` without `else` an empty block in its place, so an `else` that
    // is written and empty is the same thing here and in the GIR builder (buildBranch).
    const auto &elseBlock = ast->optAtAs<AST::StmtBlockLoad>(2);
    if (!isStatement && (!elseBlock || elseBlock->atAs<AST::RepeatedLoad>(0)->size() == 0)) {
        diags_->of(SemanticDiag::IfWithoutElseAsValue).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    }
    const ValueUseScope valueUseScope(*this);

    // condition
    // Process the condition expression and attach it to the branch node
    node_ptr_t condNode = visitData(ast->atAs<AST::DataLoad>(0));
    *brchNode << condNode;

    // Process the "then" block and attach it as the "True" case
    const auto &thenBlock = ast->atAs<AST::StmtBlockLoad>(1);
    enterArmOf(ast, valueUnused);
    node_ptr_t thenNode = visitStmtBlock(thenBlock);
    node_ptr_t trueCase = tree_->makeCase(CaseType::True);
    setOriginFromOrigin(
        context_,
        trueCase,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchTarget,
            0),
        camel::source::OriginKind::GctNode,
        "gct.if.then");
    *trueCase << thenNode;
    *brchNode << trueCase;

    // Process the optional "else" block and attach it as the "Else" case
    const auto &elseNode = ast->optAtAs<AST::StmtBlockLoad>(2);
    node_ptr_t elseCase  = tree_->makeCase(CaseType::Else);
    setOriginFromOrigin(
        context_,
        elseCase,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchTarget,
            1),
        camel::source::OriginKind::GctNode,
        "gct.if.else");
    *brchNode << elseCase;
    if (elseNode) {
        // If an "else" block exists, process and attach it
        enterArmOf(ast, valueUnused);
        node_ptr_t elseBlock = visitStmtBlock(elseNode);
        *elseCase << elseBlock;
    } else {
        // empty else block
        // If no "else" block exists, attach an empty execution block
        *elseCase << makeExec();
    }
    registerGctSemantic(
        context_,
        brchNode,
        ast,
        {
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(condNode),
                -1,
                "condition"),
            semanticPart(
                camel::source::SemanticRole::BranchTarget,
                nodeOrigin(trueCase),
                0,
                "then"),
            semanticPart(
                camel::source::SemanticRole::BranchTarget,
                nodeOrigin(elseCase),
                1,
                "else"),
        },
        {nodeOrigin(condNode), nodeOrigin(trueCase), nodeOrigin(elseCase)});
    LEAVE("IfExpr");
    return brchNode;
}

/*
LoopExpr(state, binder) : Repeated Data inits, Optional Data sequence, Optional Data condition,
                          StmtBlock body ;
A loop (design principles 3.10) becomes one LOOP node (camel/compile/gct/node.h,
Node::loopKind): the first values and the sequence are expressions where the loop is; the
condition and the body are scopes of their own, the way the arms of an `if` are; a loop body
written in a block or a function under a permission tag is part of it. The names of the state
and of the element are declared by the
graph builder, which gives the condition and the body their own graphs.
*/
node_ptr_t Builder::visitLoopExpr(const AST::node_ptr_t &ast) {
    ENTER("LoopExpr");
    const auto &load    = ast->loadAs<AST::LoopExprLoad>();
    const auto &condAst = ast->optAtAs<AST::DataLoad>(2);
    const bool hasFor   = load->hasFor();
    const auto kind = hasFor
                          ? (condAst ? camel::core::LoopKind::ForWhile : camel::core::LoopKind::For)
                          : camel::core::LoopKind::While;
    std::vector<std::string> state;
    for (const Reference &name : load->state()) {
        state.push_back(name.toString());
    }
    node_ptr_t loopNode =
        tree_->makeLoop(kind, state, hasFor ? load->binder().toString() : std::string());
    setOriginFromAst(context_, loopNode, ast, camel::source::OriginKind::GctNode, "gct.loop");
    const ValueUseScope valueUseScope(*this);
    valueUse_ = {};

    // The first values, then the sequence: evaluated once, where the loop is.
    for (const auto &init : *ast->atAs<AST::RepeatedLoad>(0)) {
        *loopNode << visitData(init);
    }
    if (hasFor) {
        *loopNode << visitData(ast->optAtAs<AST::DataLoad>(1));
    }
    // The condition: a scope of its own whose one statement is the condition.
    if (condAst) {
        node_ptr_t condExec = makeExec();
        setOriginFromAst(
            context_,
            condExec,
            condAst,
            camel::source::OriginKind::GctNode,
            "gct.loop.condition");
        *condExec << visitData(condAst);
        *loopNode << condExec;
    }
    // The body: its value is the next state, so a `return` in it ends the body with that
    // value, as in an arm whose value is used.
    enterArmOf(ast, false);
    *loopNode << visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(3));
    registerGctSemantic(context_, loopNode, ast);
    LEAVE("LoopExpr");
    return loopNode;
}

/*

*/
node_ptr_t Builder::visitMatchExpr(const AST::node_ptr_t &ast) {
    ENTER("MatchExpr");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchExpr");

    // The subject is any expression, evaluated once before the cases; a name among them is
    // read here, once, and the arms read the value through the branch.
    const auto &subjectAst = ast->atAs<AST::DataLoad>(0);
    const bool valueUnused = std::exchange(valueUse_.unusedCtrl, nullptr) == ast.get();
    const ValueUseScope valueUseScope(*this);

    node_ptr_t subjectNode = visitData(subjectAst);
    node_ptr_t brchNode    = tree_->makeBrch();
    setOriginFromOrigin(
        context_,
        subjectNode,
        findSemanticOrigin(
            astSemantic(context_, ast),
            camel::source::SemanticRole::BranchCondition),
        camel::source::OriginKind::GctNode,
        "gct.match.scrutinee");
    setOriginFromAst(context_, brchNode, ast, camel::source::OriginKind::GctNode, "gct.match");
    *brchNode << subjectNode;
    const node_ptr_t dRefNode = subjectNode;

    std::vector<camel::source::SemanticPart> parts = {
        semanticPart(
            camel::source::SemanticRole::BranchCondition,
            nodeOrigin(dRefNode),
            -1,
            "scrutinee"),
    };
    std::vector<camel::source::origin_id_t> mergedInputs = {nodeOrigin(dRefNode)};

    for (const auto &aCaseNode : *ast->atAs<AST::RepeatedLoad>(1)) {
        // Handle a value case
        const auto &caseLoadType = aCaseNode->front()->type();
        node_ptr_t gCaseNode     = nullptr;

        if (caseLoadType == AST::LoadType::Data &&
            aCaseNode->front()->loadAs<AST::DataLoad>()->dataType() == AST::DataType::EnumPattern) {
            // `case Shape::Rect { w, h }`: the variant is written by its name and the fields
            // the pattern binds; the graph builder resolves it against the enum matched.
            const auto &pattern = aCaseNode->front()->loadAs<AST::EnumPatternLoad>();
            std::vector<std::string> fields, bindings;
            for (const AST::EnumPatternField &field : pattern->fields()) {
                fields.push_back(field.name);
                bindings.push_back(field.binding);
            }
            gCaseNode = tree_->makeEnumCase(
                pattern->ref().toString(),
                fields,
                bindings,
                pattern->structShape());
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        } else if (
            caseLoadType == AST::LoadType::Data &&
            aCaseNode->front()->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Ref) {
            // `case v` / `case error e`: the fixed patterns over a `T?` / `T!` (design
            // principles 3.8). The graph builder, which knows the scrutinee's type, binds
            // the name in the arm or reports the pattern.
            const auto &pattern    = aCaseNode->front();
            const bool isError     = pattern->size() > 0;
            const std::string name = (isError ? pattern->front() : pattern)
                                         ->loadAs<AST::RefDataLoad>()
                                         ->ref()
                                         .toString();
            gCaseNode = tree_->makeCase(isError ? CaseType::Error : CaseType::Bind, name);
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        } else if (caseLoadType != AST::LoadType::Null) {
            gCaseNode               = tree_->makeCase(CaseType::Value);
            node_ptr_t caseDataNode = visitData(aCaseNode->atAs<AST::DataLoad>(0));
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseDataNode << caseExprNode;
            mergedInputs.push_back(nodeOrigin(caseDataNode));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::CaseValue,
                nodeOrigin(caseDataNode),
                static_cast<int32_t>(parts.size()),
                "caseValue"));
        } else {
            // Handle the "else" case
            gCaseNode = tree_->makeCase(CaseType::Else);
            enterArmOf(ast, valueUnused);
            node_ptr_t caseExprNode = visitStmtBlock(aCaseNode->atAs<AST::StmtBlockLoad>(1));
            *gCaseNode << caseExprNode;
        }

        setOriginFromOrigin(
            context_,
            gCaseNode,
            aCaseNode->load()->origin(),
            camel::source::OriginKind::GctNode,
            "gct.match.case");

        // The guard of the case, when it has one. `case p when c => ...` is taken only when the
        // pattern matches *and* `c` holds, so the graph builder evaluates the guard in the arm
        // this case becomes and sends the value on to the cases written after it when it does
        // not hold. The guard is kept beside the
        // CASE rather than among its children: the arm's body is the last child every reader of
        // that node takes, and the guard is read in the arm's scope, where the names the
        // pattern binds are in force.
        if (aCaseNode->size() > 2) {
            enterArmOf(ast, valueUnused);
            tree_->setCaseGuard(gCaseNode, visitData(aCaseNode->at(2)));
            gCaseNode->setGuardedCase();
        }
        mergedInputs.push_back(nodeOrigin(gCaseNode));
        parts.push_back(semanticPart(
            camel::source::SemanticRole::BranchTarget,
            nodeOrigin(gCaseNode),
            static_cast<int32_t>(parts.size()),
            "case"));

        // Attach the case node to the branch node
        *brchNode << gCaseNode;
    }
    registerGctSemantic(context_, brchNode, ast, std::move(parts), std::move(mergedInputs));
    LEAVE("MatchExpr");
    return brchNode;
}

/*
matchTypeExpr
    : MATCH TYPE typeExpr '{' typeCase* '}'
    ;
*/
node_ptr_t Builder::visitMatchType(const AST::node_ptr_t &ast) {
    ENTER("MatchType");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for MatchType");
    node_ptr_t matchNode = tree_->makeTypeMatch();
    setOriginFromAst(
        context_,
        matchNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.match.type");

    const auto &typeAst = ast->atAs<AST::TypeLoad>(0);
    Type *subject       = visitType(typeAst);
    node_ptr_t typeNode = tree_->makeType(subject);
    setOriginFromAst(
        context_,
        typeNode,
        typeAst,
        camel::source::OriginKind::GctNode,
        "gct.match.type.subject");
    *matchNode << typeNode;

    for (const auto &caseAst : *ast->atAs<AST::RepeatedLoad>(1)) {
        const auto &caseLoad = caseAst->loadAs<AST::TypeCaseLoad>();
        uint16_t patterns    = 0;
        std::vector<Type *> identities;
        bool catchAll        = false;
        bool unreachableSeen = false;
        // A guarded `_` or `#leaf` selects the types its predicate holds for, so a case after it
        // is reachable (design principles 5.5).
        const bool guarded = AST::typeCaseGuardOf(caseAst) != nullptr;
        for (const AST::TypeCasePattern &pattern : caseLoad->patterns()) {
            const GCT::TypeCaseKind kind = static_cast<GCT::TypeCaseKind>(pattern.kind);
            if (kind == GCT::TypeCaseKind::Identity) {
                Type *patternType = visitType(pattern.type);
                if (patternType == nullptr) {
                    continue;
                }
                const std::string written = pattern.type->load()->geneCode();
                if (patternType->code() == TypeCode::Void) {
                    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                        .atOrigin(pattern.type->load()->origin())
                        .commit(written, "'void' is not the type of a value");
                    throw BuildAbortException();
                }
                if (isTypeOfPlaceholder(patternType)) {
                    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                        .atOrigin(pattern.type->load()->origin())
                        .commit(written, "a 'typeof' expression is not a type identity");
                    throw BuildAbortException();
                }
                for (Type *earlier : identities) {
                    if (earlier == patternType || typeEquals(earlier, patternType)) {
                        diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
                            .atOrigin(pattern.type->load()->origin())
                            .commit(
                                written,
                                "the same type is already written earlier in this case");
                        throw BuildAbortException();
                    }
                }
                if (catchAll && !unreachableSeen) {
                    // `#leaf` and `_` select every type, so nothing after them can be selected.
                    diags_->of(SemanticDiag::StaticMatchUnreachableCase)
                        .at(caseAst->load()->tokenRange())
                        .commit("this case");
                    unreachableSeen = true;
                }
                identities.push_back(patternType);
                continue;
            }
            patterns |= typeCaseBit(kind);
            if (!guarded &&
                (kind == GCT::TypeCaseKind::Leaf || kind == GCT::TypeCaseKind::Wildcard)) {
                catchAll = true;
            }
        }
        node_ptr_t caseNode = tree_->makeTypeCase(patterns, identities);
        setOriginFromAst(
            context_,
            caseNode,
            caseAst,
            camel::source::OriginKind::GctNode,
            "gct.match.type.case");
        node_ptr_t bodyNode = visitStmtBlock(caseAst->atAs<AST::StmtBlockLoad>(0));
        *caseNode << bodyNode;
        // The guard of the case is the case's own, kept by the tree beside it rather than made a
        // child: the body stays child 0, exactly as a value `match` case keeps its guard
        // (Tree::setCaseGuard). The graph builder reads it once the pattern has selected the
        // subject, with the type arguments of the instance bound.
        if (const AST::node_ptr_t guardAst = AST::typeCaseGuardOf(caseAst)) {
            tree_->setCaseGuard(caseNode, visitData(guardAst));
        }
        *matchNode << caseNode;
    }
    LEAVE("MatchType");
    return matchNode;
}

void Builder::checkStaticHeadNames(
    const AST::node_ptr_t &ast, const std::string &shape, size_t named) {
    // The parts of the element, in order: `fields T` gives the field's name, its type and its
    // tags; `indices ts` gives the position and a written sequence gives the element, each one
    // thing. A head may write fewer names than the element has parts -- `for n in fields T` reads
    // the field's name and nothing else -- never more: there would be nothing for the extra name
    // to be, and the names are read positionally, so `_` takes a position like any other.
    const size_t parts = shape == "fields" ? 3 : 1;
    if (named <= parts) {
        return;
    }
    diags_->of(SemanticDiag::StaticHeadNameCount)
        .at(ast->load()->tokenRange())
        .commit(std::to_string(named), shape, std::to_string(parts));
    throw BuildAbortException();
}

/*
staticForExpr
    : STATIC staticForHeader '=>' blockExpr
    ;
staticForHeader
    : FOR '(' identDef (',' identDef)* ')' IN ('fields' typeExpr | 'indices' dataExpr)
    ;
The walked shape is child 0: a TYPE for `fields`, the operand value for `indices`.
The head names up to three things, and how many the element gives is read here, where the walked
shape is known (`checkStaticHeadNames`): `fields` gives the triple (name, type, tags) and
`indices` gives one thing.
*/
node_ptr_t Builder::visitStaticFor(const AST::node_ptr_t &ast) {
    ENTER("StaticFor");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticFor");
    const auto &load = ast->loadAs<AST::StaticForLoad>();
    checkStaticHeadNames(ast, load->byIndices() ? "indices" : "fields", load->nameCount());

    node_ptr_t forNode = tree_->makeStaticFor(
        load->valueName(),
        load->typeName(),
        load->tagsName(),
        load->byIndices());
    setOriginFromAst(context_, forNode, ast, camel::source::OriginKind::GctNode, "gct.static.for");

    if (load->byIndices()) {
        // `indices ts` walks a tuple value: the operand is an ordinary expression whose type is
        // read when the expansion happens, so it is built as one here.
        *forNode << visitData(ast->at(0));
    } else {
        const auto &typeAst = ast->atAs<AST::TypeLoad>(0);
        Type *subject       = visitType(typeAst);
        node_ptr_t typeNode = tree_->makeType(subject);
        setOriginFromAst(
            context_,
            typeNode,
            typeAst,
            camel::source::OriginKind::GctNode,
            "gct.static.for.type");
        *forNode << typeNode;
    }

    // `F` names the type of one field in the body only. It is an ordinary type variable of the
    // enclosing function's instance: the graph builder binds it to the field type per round.
    pushScope();
    if (!load->byIndices()) {
        typeScope_->insert(
            Reference(load->typeName()),
            TypeVarType::create(load->typeName(), false));
    }
    node_ptr_t bodyNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(1));
    popScope();
    *forNode << bodyNode;

    LEAVE("StaticFor");
    return forNode;
}

/*
staticLoopExpr
    : STATIC LOOP loopState (',' loopState)* staticLoopHeader DO blockExpr
staticLoopHeader
    : FOR (identDef | '_') (',' (identDef | '_'))* IN
      ('fields' typeExpr | 'indices' dataExpr | dataExpr)
The AST node holds the first values of the state (a Repeated Data), the walked shape, and the
body (StaticLoopLoad). The GCT node holds the shape first, then the first values, then the EXEC
body. `F` is an ordinary type variable of the enclosing function's instance, bound per field
by the graph builder; `n` (or `j` for `indices`, or the element's name for a written sequence)
and the state names are bound per round. The head of `static loop` writes `in <序列>`; `static for`
still writes it between parentheses.
How many names the element gives is read here (`checkStaticHeadNames`): the triple (name, type,
tags) for `fields`, one thing for everything else.
*/
node_ptr_t Builder::visitStaticLoop(const AST::node_ptr_t &ast) {
    ENTER("StaticLoop");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticLoop");
    const auto &load = ast->loadAs<AST::StaticLoopLoad>();
    // `fields T` is the only head that names a type (`F`): `indices ts` and a written sequence
    // bind one name, the entry itself.
    const bool byFields = !load->byIndices() && !load->bySequence();
    checkStaticHeadNames(
        ast,
        byFields ? "fields" : (load->byIndices() ? "indices" : "a written sequence"),
        load->nameCount());

    std::vector<std::string> state;
    state.reserve(load->state().size());
    for (const Reference &name : load->state()) {
        state.push_back(name.toString());
    }
    node_ptr_t loopNode = tree_->makeStaticLoop(
        state,
        load->valueName(),
        load->typeName(),
        load->tagsName(),
        load->byIndices(),
        load->bySequence());
    setOriginFromAst(
        context_,
        loopNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.static.loop");

    const ValueUseScope valueUseScope(*this);
    valueUse_ = {};

    if (byFields) {
        const auto &typeAst = ast->atAs<AST::TypeLoad>(1);
        Type *subject       = visitType(typeAst);
        node_ptr_t typeNode = tree_->makeType(subject);
        setOriginFromAst(
            context_,
            typeNode,
            typeAst,
            camel::source::OriginKind::GctNode,
            "gct.static.loop.type");
        *loopNode << typeNode;
    } else {
        // `indices ts` walks a tuple value and a written sequence is an ordinary expression:
        // both are built as data, and what they hold is read when the expansion happens.
        *loopNode << visitData(ast->at(1));
    }

    // The first values, evaluated once where the loop is, before the names of the head exist.
    for (const auto &init : *ast->atAs<AST::RepeatedLoad>(0)) {
        *loopNode << visitData(init);
    }

    // `F` names the type of one field in the body only (as in `static for`).
    pushScope();
    if (byFields) {
        typeScope_->insert(
            Reference(load->typeName()),
            TypeVarType::create(load->typeName(), false));
    }
    // A written sequence's round name is a value, never a type: `for i in 0..5` binds the round's
    // integer and `for x in ts` binds the element `ts.[i]`, whose type is the type at that
    // position. It is bound by the graph builder at each round,
    // as the `j` of `indices` is, so nothing of it goes into the type scope here; a body that uses
    // it where a type belongs writes `typeof x`, and `fields T` is the spelling that gives a
    // field's name and type.
    node_ptr_t bodyNode = visitStmtBlock(ast->atAs<AST::StmtBlockLoad>(2));
    popScope();
    *loopNode << bodyNode;

    registerGctSemantic(context_, loopNode, ast);
    LEAVE("StaticLoop");
    return loopNode;
}

node_ptr_t Builder::visitStaticAccess(const AST::node_ptr_t &ast) {
    ENTER("StaticAccess");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StaticAccess");
    node_ptr_t accessNode = tree_->makeStaticAccess();
    setOriginFromAst(
        context_,
        accessNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.static.access");
    node_ptr_t targetNode = visitData(ast->at(0));
    node_ptr_t keyNode    = visitData(ast->at(1));
    *accessNode << targetNode << keyNode;
    LEAVE("StaticAccess");
    return accessNode;
}

/*
structData : '{' (pairedValues ','?)? '}' | '{' staticForExpr '}'
tupleData  : '(' dataList? ','? ')' | '(' staticForExpr ')'
The comprehension alternatives are their own AST nodes (StructComp / TupleComp): a generator and
nothing else. The GCT node holds the shape and the generator; the rounds are packed in GIR.
*/
node_ptr_t Builder::visitStaticComprehension(const AST::node_ptr_t &ast, bool isStruct) {
    ENTER("StaticComprehension");
    node_ptr_t compNode = tree_->makeComprehension(isStruct);
    setOriginFromAst(
        context_,
        compNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.static.comprehension");
    GUARD(ast->size() == 1, "A static comprehension has its generator as its only child.");
    *compNode << visitStaticFor(ast->at(0));
    LEAVE("StaticComprehension");
    return compNode;
}

node_ptr_t Builder::visitShapeComprehension(const AST::node_ptr_t &ast) {
    ENTER("ShapeComprehension");
    GUARD(
        ast->size() == 2 && ast->at(0)->type() == AST::LoadType::Type,
        "A shape comprehension has its type first, its generator second.");
    node_ptr_t compNode = tree_->makeShapeComprehension();
    setOriginFromAst(
        context_,
        compNode,
        ast,
        camel::source::OriginKind::GctNode,
        "gct.static.shape");
    *compNode << visitStaticFor(ast->at(1));
    Type *shape          = visitType(ast->at(0));
    node_ptr_t shapeNode = tree_->makeType(shape);
    setOriginFromAst(
        context_,
        shapeNode,
        ast->at(0),
        camel::source::OriginKind::GctNode,
        "gct.static.shape.type");
    *compNode << shapeNode;
    LEAVE("ShapeComprehension");
    return compNode;
}

} // namespace camel::compile::gct