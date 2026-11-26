// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the expression tree: patterns and the value-level `match`, the static forms
// over types, control, every operator level from `=` down to unary, the annotation and the member
// access built from it, and the loop.

#include "builder_internal.h"

namespace {

// A token as a diagnostic shows it: quoted, on one line, not longer than a few words.
inline std::string quoteForDiagnostic(std::string text) {
    if (const size_t nl = text.find('\n'); nl != std::string::npos) {
        text = text.substr(0, nl) + "...";
    }
    if (text.size() > 24) {
        size_t cut = 20;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut) + "...";
    }
    return "'" + text + "'";
}

inline syn::TokenRef tokenOf(syn::Child child) { return child.token(); }

} // namespace

/*
pattern
    : 'error' identDef
    | identRef (enumTuplePattern | enumStructPattern)?
    | literal
    | '_' // wildcard
    ;
fieldPattern
    : identRef | '_'
    ;
*/
Value Builder::visitPattern(cst::Pattern context) {
    ENTER("Pattern");
    node_ptr_t res;
    if (context->get<cst::Literal>()) {
        Literal literal = value_cast<Literal>(visitLiteral(context->get<cst::Literal>()));
        res             = createNodeAs<LiteralLoad>(literal);
        if (context->get<cst::Literal>()->tok(TK::RawString)) {
            // `case r"C:\dir"`: the pattern's value is the text, not what escapes would make.
            unwrapNodeAs<LiteralLoad>(res)->setFromRawString();
        }
        setNodeTokenRangeByContext(res, context->get<cst::Literal>());
    } else if (context->text() == "_") {
        res = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(res, context);
    } else if (context->get<cst::IdentRef>()) {
        // The two fixed patterns over a tagged value (design principles 3.8): `v` binds the
        // value taken out of a `T?` / `T!`, `error e` binds the error of a failed `T!`. Both
        // are a reference node; `error e` carries the bound name as its child. Whether the
        // scrutinee has such a type is known to the graph builder only, which reports an
        // identifier pattern over any other type by the rule itself: only the two marks `T?` and
        // `T!` can be taken apart (design principles 3.8). The parser reads the second
        // word of a pattern only after `error` (`parsePattern`), so the name here is the error's.
        if (context->get<cst::EnumTuplePattern>() || context->get<cst::EnumStructPattern>() ||
            context->get<cst::IdentRef>()->text().find("::") != std::string::npos) {
            // A variant without fields is written by its qualified name (`case Shape::Empty`):
            // a plain name would be a binding, and what an identifier pattern binds over a
            // value that is not `T?` / `T!` has not been decided.
            res = any2node(visitEnumPattern(context, context->get<cst::IdentRef>()));
            LEAVE("Pattern");
            return res;
        }
        res = createNodeAs<RefDataLoad>(Reference(context->get<cst::IdentRef>()->text()));
        setNodeTokenRangeByContext(res, context);
        if (context->get<cst::IdentDef>()) {
            node_ptr_t bound =
                createNodeAs<RefDataLoad>(Reference(context->get<cst::IdentDef>()->text()));
            setNodeTokenRangeByContext(bound, context->get<cst::IdentDef>());
            *res << bound;
        }
    } else {
        // A pattern is an identifier, a variant path, a literal or `_`. What the fields of a
        // variant bind is read by the enum patterns above; the parser builds no other shape.
        ASSERT(false, "a pattern of another shape reached the builder");
        res = createNodeAs<NullLoad>();
        setNodeTokenRangeByContext(res, context);
    }
    LEAVE("Pattern");
    return res;
}

/*
matchCase
    : CASE pattern ('|' pattern)* whenClause? '=>' blockExpr
    ;
*/
Value Builder::visitMatchCase(cst::MatchCase context) {
    ENTER("MatchCase");
    // `case a | b => body` enters the body when any of its patterns matches. A branch has one
    // value per arm, so the case becomes one arm per pattern, each with its own copy of the
    // body (not only the first pattern is read). An alternative that is the wildcard
    // matches everything: the whole case is then the wildcard arm.
    std::vector<node_ptr_t> patterns;
    for (const auto patternContext : context->all<cst::Pattern>()) {
        node_ptr_t pattern = any2node(visitPattern(patternContext));
        if (pattern->type() == LoadType::Null) {
            patterns.assign(1, pattern);
            break;
        }
        patterns.push_back(pattern);
    }
    // The guard of the case, read once and shared by its arms as the body is: each arm builds
    // its own copy of it, in the scope of that arm's pattern, so the names the pattern binds
    // are the ones it reads (and one arm's guard never sees another arm's bindings).
    node_ptr_t guardNode = nullptr;
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        guardNode = any2node(visitExpr(whenClause->expr()));
    }
    std::vector<node_ptr_t> caseNodes;
    for (const node_ptr_t &pattern : patterns) {
        node_ptr_t caseNode = createNodeAs<MatchCaseLoad>();
        setNodeTokenRangeByContext(caseNode, context);
        node_ptr_t exprNode = any2node(visitBlockExpr(context->lastChild().node()));
        *caseNode << pattern << exprNode;
        if (guardNode) {
            *caseNode << guardNode;
        }
        registerAstSemanticBundle(
            caseNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(caseNode, context->tok(TK::KwCase), "ast.match.case"),
                    -1,
                    "case"),
                semanticPart(
                    camel::source::SemanticRole::CaseValue,
                    nodeOrigin(pattern),
                    0,
                    "pattern"),
                semanticPart(
                    camel::source::SemanticRole::CaseBody,
                    nodeOrigin(exprNode),
                    -1,
                    "body"),
            });
        caseNodes.push_back(caseNode);
    }
    LEAVE("MatchCase");
    return caseNodes;
}

/*
staticForExpr
    : STATIC staticForHeader '=>' blockExpr
    ;
staticForHeader
    : FOR '(' identDef (',' identDef)* ')' IN ('fields' typeExpr | 'indices' dataExpr)
    ;
`indices` walks the positions of a tuple value; its operand is child 0 as an
ordinary expression, where `fields` has a type there. The head may name up to three things --
`fields`'s element is the triple (name, type, tags), every other element is one thing -- and
whether the count fits is read from the element (`SemanticDiag::StaticHeadNameCount`).
*/
Value Builder::visitStaticForExpr(cst::StaticForExpr context) {
    ENTER("StaticForExpr");
    const auto bindings = context->all<cst::IdentDef>();
    const std::string valueName =
        bindings.size() > 0 ? std::string(bindings[0]->text()) : std::string("_");
    const std::string typeName =
        bindings.size() > 1 ? std::string(bindings[1]->text()) : std::string("_");
    const std::string tagsName =
        bindings.size() > 2 ? std::string(bindings[2]->text()) : std::string("_");
    const auto mode      = context->tok(TK::Identifier);
    const bool byIndices = mode && mode.text() == "indices";
    node_ptr_t forNode =
        createNodeAs<StaticForLoad>(valueName, typeName, tagsName, byIndices, bindings.size());
    setNodeTokenRangeByContext(forNode, context);
    node_ptr_t shapeNode =
        byIndices ? any2node(visitExpr(context->expr())) : any2node(visitType(context->type()));
    *forNode << shapeNode;
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *forNode << bodyNode;
    registerAstSemanticBundle(
        forNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(forNode, context->tok(TK::KwStatic), "ast.static.for.strong"),
                -1,
                "static"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(forNode, context->tok(TK::KwFor), "ast.static.for"),
                -1,
                "for"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(shapeNode),
                -1,
                byIndices ? "indices" : "type"),
        });
    LEAVE("StaticForExpr");
    return forNode;
}

/*
staticLoopExpr
    : STATIC LOOP loopState (',' loopState)* staticLoopHeader DO blockExpr
    ;
The children of the node are described at StaticLoopLoad: a Repeated Data of the first values,
the walked shape (a type for `fields`, a value for `indices` and for a sequence), and the body.
The head's own production is `staticLoopHeader`: `for i in <序列>`, without the
parentheses `static for` still has.
*/
Value Builder::visitStaticLoopExpr(cst::StaticLoopExpr context) {
    ENTER("StaticLoopExpr");
    std::vector<Reference> state;
    node_ptr_t inits = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(inits, context);
    for (const auto entry : context->all<cst::LoopState>()) {
        state.emplace_back(entry->get<cst::IdentDef>()->text());
        *inits << any2node(visitExpr(entry->expr()));
    }
    const auto bindings = context->all<cst::IdentDef>();
    const std::string valueName =
        bindings.size() > 0 ? std::string(bindings[0]->text()) : std::string("_");
    const std::string typeName =
        bindings.size() > 1 ? std::string(bindings[1]->text()) : std::string("_");
    const std::string tagsName =
        bindings.size() > 2 ? std::string(bindings[2]->text()) : std::string("_");
    // The subject word of the head is the only direct identifier token of the node (`acc`,
    // `i` and the names of the states are inside their own nodes): `fields`, `indices`, or
    // nothing at all when what follows `in` is a sequence.
    const auto mode       = context->tok(TK::Identifier);
    const bool byIndices  = mode && mode.text() == "indices";
    const bool byFields   = mode && mode.text() == "fields";
    const bool bySequence = !byIndices && !byFields;
    node_ptr_t loopNode   = createNodeAs<StaticLoopLoad>(
        state,
        valueName,
        typeName,
        tagsName,
        byIndices,
        bySequence,
        bindings.size());
    setNodeTokenRangeByContext(loopNode, context);
    *loopNode << inits;
    node_ptr_t shapeNode = byIndices || bySequence ? any2node(visitExpr(context->expr()))
                                                   : any2node(visitType(context->type()));
    *loopNode << shapeNode;
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *loopNode << bodyNode;
    registerAstSemanticBundle(
        loopNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(
                    loopNode,
                    context->tok(TK::KwStatic),
                    "ast.static.loop.strong"),
                -1,
                "static"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(loopNode, context->tok(TK::KwLoop), "ast.static.loop"),
                -1,
                "loop"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(shapeNode),
                -1,
                byIndices ? "indices" : (bySequence ? "sequence" : "type")),
        });
    LEAVE("StaticLoopExpr");
    return loopNode;
}
/*
ctrlExpr
    : IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    | MATCH identRef '{' matchCase+ '}'
    | TRY stmtBlock catchClause+ (FINALLY stmtBlock)?
    ;
*/
Value Builder::visitCtrlExpr(syn::Node context) {
    ENTER("CtrlExpr");
    node_ptr_t ctrlNode = nullptr;
    switch (context.kind()) {
    case NK::IfExpr: // IF logicalOrExpr THEN blockExpr (ELSE blockExpr)?
    {
        ctrlNode = createNodeAs<IfExprLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
        node_ptr_t condNode = any2node(visitExpr(context->expr()));
        node_ptr_t thenNode = any2node(visitBlockExpr(context->child(3).node()));
        *ctrlNode << condNode;
        *ctrlNode << thenNode;
        node_ptr_t elseBlockNode = createNodeAs<OptionalLoad>("StmtBlock");
        *ctrlNode << elseBlockNode;
        if (context->child(5).node()) {
            setNodeTokenRangeByContext(elseBlockNode, context->child(5).node());
            *elseBlockNode << any2node(visitBlockExpr(context->child(5).node()));
        } else {
            node_ptr_t stmts = createNodeAs<StmtBlockLoad>();
            *stmts << createNodeAs<RepeatedLoad>("Stmt");
            *elseBlockNode << stmts;
        }
        registerAstSemanticBundle(
            ctrlNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwIf), "ast.if.keyword"),
                    -1,
                    "if"),
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwThen), "ast.if.then"),
                    -1,
                    "then"),
                semanticPart(
                    camel::source::SemanticRole::Keyword,
                    context->tok(TK::KwElse)
                        ? deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwElse), "ast.if.else")
                        : camel::source::kInvalidOriginId,
                    -1,
                    "else"),
                semanticPart(
                    camel::source::SemanticRole::BranchCondition,
                    nodeOrigin(condNode),
                    -1,
                    "condition"),
                semanticPart(
                    camel::source::SemanticRole::BranchTarget,
                    nodeOrigin(thenNode),
                    0,
                    "then"),
                semanticPart(
                    camel::source::SemanticRole::BranchTarget,
                    elseBlockNode->empty() ? camel::source::kInvalidOriginId
                                           : nodeOrigin(elseBlockNode->front()),
                    1,
                    "else"),
            });
    } break;
    case NK::MatchTypeExpr: // MATCH TYPE typeExpr '{' typeCase* '}'
    {
        ctrlNode = any2node(visitMatchTypeExpr(cst::MatchTypeExpr(context)));
    } break;
    case NK::StaticForExpr: {
        ctrlNode = any2node(visitStaticForExpr(cst::StaticForExpr(context)));
    } break;
    case NK::MatchExpr: // MATCH logicalOrExpr '{' matchCase+ '}'
    {
        ctrlNode = createNodeAs<MatchExprLoad>();
        setNodeTokenRangeByContext(ctrlNode, context);
        node_ptr_t subjectNode = any2node(visitExpr(context->expr()));
        // The subject is evaluated once, before the cases; the graph builder tests its value.
        *ctrlNode << subjectNode;
        node_ptr_t casesNode = createNodeAs<RepeatedLoad>("MatchCase");
        setNodeTokenRangeByContext(casesNode, context);
        cst::MatchCase wildcardCase;
        bool wildcardSeen = false;
        for (const auto matchCase : context->all<cst::MatchCase>()) {
            if (wildcardSeen) {
                // The branch takes its last arm when no value matches, and `case _` always
                // matches: a case written after it is never taken (it is not entered in
                // the wildcard's place).
                diags_->of(SemanticDiag::MatchCaseAfterWildcard)
                    .at(rangeOf(matchCase->firstToken(), matchCase->lastToken()))
                    .commit();
                continue;
            }
            // One node per pattern of the case (see visitMatchCase).
            for (const node_ptr_t &caseNode :
                 value_cast<std::vector<node_ptr_t>>(visitMatchCase(matchCase))) {
                *casesNode << caseNode;
            }
            // The first case that matches is taken, so nothing after the wildcard can be. A
            // guarded wildcard is not a wildcard: `case _ when c` is not taken when `c` is
            // false, so the cases written after it are still reached (and the guarded case
            // does not cover the match by itself).
            if (casesNode->size() > 0) {
                const node_ptr_t &lastCase = casesNode->at(casesNode->size() - 1);
                if (lastCase->front()->type() == LoadType::Null && lastCase->size() <= 2) {
                    wildcardCase = matchCase;
                    wildcardSeen = true;
                }
            }
        }
        if (wildcardCase && casesNode->size() == 1 &&
            subjectNode->load()->type() == LoadType::Data &&
            std::dynamic_pointer_cast<RefDataLoad>(subjectNode->load()) != nullptr) {
            // Only the wildcard is left: the match is its body, whatever the value. A branch
            // without a value to compare is read as an `if` on a boolean, so this is built as
            // one whose condition is true. Reading the name has no effect, so dropping the
            // read is not observable; a subject that is a call is kept in the general form
            // below, which evaluates it.
            ctrlNode = createNodeAs<IfExprLoad>();
            setNodeTokenRangeByContext(ctrlNode, context);
            node_ptr_t condNode = createNodeAs<LiteralLoad>(Literal(LiteralType::Boolean, "true"));
            setNodeTokenRangeByContext(condNode, context);
            node_ptr_t thenNode      = casesNode->at(0)->at(1);
            node_ptr_t elseBlockNode = createNodeAs<OptionalLoad>("StmtBlock");
            setNodeTokenRangeByContext(elseBlockNode, wildcardCase->lastChild().node());
            *elseBlockNode << any2node(visitBlockExpr(wildcardCase->lastChild().node()));
            *ctrlNode << condNode << thenNode << elseBlockNode;
            registerAstSemanticBundle(
                ctrlNode,
                {
                    semanticPart(
                        camel::source::SemanticRole::Keyword,
                        deriveAstAnchorOrigin(
                            ctrlNode,
                            context->tok(TK::KwMatch),
                            "ast.match.keyword"),
                        -1,
                        "match"),
                    semanticPart(
                        camel::source::SemanticRole::BranchCondition,
                        nodeOrigin(condNode),
                        -1,
                        "condition"),
                    semanticPart(
                        camel::source::SemanticRole::BranchTarget,
                        nodeOrigin(thenNode),
                        0,
                        "then"),
                    semanticPart(
                        camel::source::SemanticRole::BranchTarget,
                        nodeOrigin(elseBlockNode->front()),
                        1,
                        "else"),
                });
            break;
        }
        *ctrlNode << casesNode;
        std::string subjectText                        = syn::Node(context->expr())->text();
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(ctrlNode, context->tok(TK::KwMatch), "ast.match.keyword"),
                -1,
                "match"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(subjectNode),
                -1,
                subjectText),
        };
        for (size_t i = 0; i < casesNode->size(); ++i) {
            auto caseOrigin = nodeOrigin(casesNode->at(i));
            parts.push_back(semanticPart(
                camel::source::SemanticRole::BranchTarget,
                caseOrigin,
                static_cast<int32_t>(i),
                "case"));
        }
        registerAstSemanticBundle(ctrlNode, std::move(parts));
    } break;

    default:
        // Every kind `visitExpr` sends here has a case above. `try`/`catch`/`finally` are not
        // part of the language (design principles 3.9) and are not tokens, so the
        // parser never builds a node for them.
        throw std::runtime_error("Invalid control expression");
    }
    LEAVE("CtrlExpr");
    return ctrlNode;
}

/*
assignExpr
    : logicalOrExpr (
        ('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=')
        logicalOrExpr
    )?
    ;
*/
Value Builder::visitAssignExpr(cst::AssignExpr context) {
    ENTER("AssignExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "=") {
            op = BinaryDataOp::Assign;
        } else if (strOp == "+=") {
            op = BinaryDataOp::AssignAdd;
        } else if (strOp == "-=") {
            op = BinaryDataOp::AssignSub;
        } else if (strOp == "*=") {
            op = BinaryDataOp::AssignMul;
        } else if (strOp == "/=") {
            op = BinaryDataOp::AssignDiv;
        } else if (strOp == "%=") {
            op = BinaryDataOp::AssignMod;
        } else if (strOp == "^=") {
            op = BinaryDataOp::AssignExp;
        } else if (strOp == "@=") {
            op = BinaryDataOp::AssignMat;
        } else if (strOp == "&=") {
            op = BinaryDataOp::AssignAnd;
        } else if (strOp == "|=") {
            op = BinaryDataOp::AssignOr;
        } else {
            throw std::runtime_error("Invalid assignment operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.assign.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("AssignExpr");
    return lhsNode;
}

/*
logicalOrExpr
    : logicalAndExpr ('||' logicalAndExpr)*
    ;
*/
Value Builder::visitLogicalOrExpr(cst::LogicalOrExpr context) {
    ENTER("LogicalOrExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::LogicalOr);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.logic.or"),
                    -1,
                    "||"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalOrExpr");
    return lhsNode;
}

/*
orExpr  : andExpr ('|' andExpr)* ;
andExpr : equalityExpr ('&' equalityExpr)* ;
Ordinary binary operators that evaluate both sides: BinaryDataOp::Or and And. `||` and `&&`
are LogicalOr and LogicalAnd, which a later stage turns into branches.
*/
Value Builder::visitEagerLogicExpr(syn::Node context) {
    const bool isOr    = context.is(NK::OrExpr);
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    const size_t count = context->exprs().size();
    for (size_t i = 1; i < count; ++i) {
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode =
            createNodeAs<BinaryExprLoad>(isOr ? BinaryDataOp::Or : BinaryDataOp::And);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        isOr ? "ast.or" : "ast.and"),
                    -1,
                    isOr ? "|" : "&"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    return lhsNode;
}

/*
logicalAndExpr
    : orExpr ('&&' orExpr)*
    ;
*/
Value Builder::visitLogicalAndExpr(cst::LogicalAndExpr context) {
    ENTER("LogicalAndExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::LogicalAnd);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.logic.and"),
                    -1,
                    "&&"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("LogicalAndExpr");
    return lhsNode;
}

/*
equalityExpr
    : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
    ;
*/
Value Builder::visitEqualityExpr(cst::EqualityExpr context) {
    ENTER("EqualityExpr");
    node_ptr_t lhsNode =
        any2node(visitExpr(context->expr(0))); // get the left hand side of the equality expr
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "===") {
            op = BinaryDataOp::StrictEq;
        } else if (strOp == "!==") {
            op = BinaryDataOp::StrictNeq;
        } else if (strOp == "==") {
            op = BinaryDataOp::Eq;
        } else if (strOp == "!=") {
            op = BinaryDataOp::Neq;
        } else {
            throw std::runtime_error("Invalid equality operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.equality.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("EqualityExpr");
    return lhsNode;
}

/*
relationalExpr
    : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
    ;
*/
Value Builder::visitRelationalExpr(cst::RelationalExpr context) {
    ENTER("RelationalExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); ++i) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "<") {
            op = BinaryDataOp::Less;
        } else if (strOp == ">") {
            op = BinaryDataOp::Greater;
        } else if (strOp == "<=") {
            op = BinaryDataOp::LessEq;
        } else if (strOp == ">=") {
            op = BinaryDataOp::GreaterEq;
        } else {
            throw std::runtime_error("Invalid relational operator: " + strOp);
        }
        if (strOp == ">" && context->exprs().size() > 2) {
            // `f<a, 0>` at the end of a line, followed by a line that goes on to contain a
            // `<...>` of its own (`'total = {}'->format<total>->println`): the first `>` is
            // read as a comparison between `0` and the next line, that line's `<` as another,
            // and its `>` closes the arguments. The errors that follow (an undefined
            // operator, a name used before its declaration) do not point there. A chain of
            // comparisons (`a > b < c`) is never what a program means, so a `>` in one that
            // ends its line is reported.
            syn::TokenRef opToken   = tokenOf(context->child(i * 2 - 1));
            syn::TokenRef nextToken = context->expr(i)->firstToken();
            if (opToken && nextToken && nextToken->line() > opToken->line()) {
                diags_->of(SyntaxDiag::ComparisonAcrossLines)
                    .at(rangeOf(opToken))
                    .commit(quoteForDiagnostic(nextToken->text()));
            }
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.relational.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("RelationalExpr");
    return lhsNode;
}

/*
additiveExpr
    : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
    ;
*/
Value Builder::visitAdditiveExpr(cst::AdditiveExpr context) {
    ENTER("AdditiveExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        BinaryDataOp op;
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "+") {
            op = BinaryDataOp::Add;
        } else if (strOp == "-") {
            op = BinaryDataOp::Sub;
        } else {
            throw std::runtime_error("Invalid additive operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.additive.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("AdditiveExpr");
    return lhsNode;
}

/*
multiplicativeExpr
    : nullableExpr (('*' | '/' | '^' | '@' | '%') nullableExpr)*
    ;
*/
Value Builder::visitMultiplicativeExpr(cst::MultiplicativeExpr context) {
    ENTER("MultiplicativeExpr");
    BinaryDataOp op;
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp = context->child(i * 2 - 1)->text();
        if (strOp == "*") {
            op = BinaryDataOp::Mul;
        } else if (strOp == "/") {
            op = BinaryDataOp::Div;
        } else if (strOp == "^") {
            op = BinaryDataOp::Pow;
        } else if (strOp == "@") {
            op = BinaryDataOp::Mat;
        } else if (strOp == "%") {
            op = BinaryDataOp::Mod;
        } else {
            throw std::runtime_error("Invalid multiplicative operator: " + strOp);
        }
        node_ptr_t rhsNode      = any2node(visitExpr(context->expr(i)));
        node_ptr_t dataExprNode = createNodeAs<BinaryExprLoad>(op);
        setNodeTokenRangeByContext(dataExprNode, context->expr(i));
        *dataExprNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataExprNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataExprNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.multiplicative.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataExprNode;
    }
    LEAVE("MultiplicativeExpr");
    return lhsNode;
}

/*
nullableExpr
    : unaryExpr (('??' | '!!') dataExpr)?
    ;
*/
Value Builder::visitNullableExpr(cst::NullableExpr context) {
    ENTER("NullableExpr");
    node_ptr_t res = any2node(visitExpr(context->expr()));

    if (context->childCount() > 1) {
        string strOp = context->child(1)->text();
        ReservedDataOp op;
        if (strOp == "??") {
            op = ReservedDataOp::NullThen;
        } else if (strOp == "!!") {
            op = ReservedDataOp::ErrorThen;
        } else {
            throw std::runtime_error("Invalid nullable operator: " + strOp);
        }
        // `a ?? b` / `a !! b`: a branch on the tag of `a` (design principles 3.9).
        if (context->expr(1)) {
            node_ptr_t dataExprNode = createNodeAs<ReservedExprLoad>(op);
            // Located at the operator: that is where its diagnostics point.
            syn::TokenRef opToken = tokenOf(context->child(1));
            setNodeTokenRange(dataExprNode, opToken->index(), opToken->index() + 1);
            setNodeOriginByOffsets(
                dataExprNode,
                static_cast<size_t>(opToken->begin()),
                static_cast<size_t>(opToken->end()));
            node_ptr_t rhsNode = any2node(visitExpr(context->expr(1)));
            *dataExprNode << res << rhsNode;
            registerAstSemanticBundle(
                dataExprNode,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(
                            dataExprNode,
                            tokenOf(context->child(1)),
                            "ast.nullable.operator"),
                        -1,
                        strOp),
                    semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(res), -1, "lhs"),
                    semanticPart(
                        camel::source::SemanticRole::Argument,
                        nodeOrigin(rhsNode),
                        0,
                        "rhs"),
                });
            res = dataExprNode;
        }
    }

    LEAVE("NullableExpr");
    return res;
}

/*
unaryExpr
    : linkExpr (AS typeExpr)?
    | ('!' | '-' | '~') linkExpr
    ;
*/
Value Builder::visitUnaryExpr(syn::Node context) {
    ENTER("UnaryExpr");
    node_ptr_t res = nullptr;

    switch (context.is(NK::CastExpr) ? 1 : 2) {
    case 1: {
        {
            if (!context->tok(TK::KwAs)) {
                throw std::runtime_error("Invalid unary operator: " + context->child(0)->text());
            }
            node_ptr_t dataNode = any2node(visitExpr(context->expr()));
            node_ptr_t typeNode = any2node(visitType(context->type()));
            res                 = createNodeAs<ReservedExprLoad>(ReservedDataOp::As);
            setNodeTokenRangeByContext(res, context);
            *res << dataNode << typeNode;
            registerAstSemanticBundle(
                res,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(res, context->tok(TK::KwAs), "ast.unary.keyword"),
                        -1,
                        "as"),
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(dataNode),
                        -1,
                        "value"),
                    semanticPart(
                        camel::source::SemanticRole::ReturnType,
                        nodeOrigin(typeNode),
                        -1,
                        "type"),
                });
        }
        break;
    }
    case 2: {
        UnaryDataOp op;
        string strOp = context->child(0)->text();
        if (strOp == "!") {
            op = UnaryDataOp::Not;
        } else if (strOp == "-") {
            op = UnaryDataOp::Neg;
        } else if (strOp == "~") {
            op = UnaryDataOp::Inv;
        } else {
            throw std::runtime_error("Invalid unary operator: " + strOp);
        }
        if (op == UnaryDataOp::Neg && context->expr().is(NK::Literal)) {
            // A minus applied directly to a number is the negative number: the literal -2, the
            // smallest integer -9223372036854775808 (whose digits alone do not fit), a typed
            // `let b: i32 = -2147483648`. Only the minus right before the literal: `-2 ^ 2`
            // negates the power and `-(2)` the parenthesis.
            const auto literal   = cst::Literal(context->expr());
            const bool integer   = static_cast<bool>(literal->tok(TK::Integer));
            const bool imaginary = static_cast<bool>(literal->tok(TK::Imaginary));
            if ((integer || literal->tok(TK::Real) || imaginary) && literal->childCount() == 1) {
                res = createNodeAs<LiteralLoad>(Literal(
                    imaginary ? LiteralType::Imaginary
                              : (integer ? LiteralType::Integer : LiteralType::Real),
                    "-" + literal->text()));
                setNodeTokenRangeByContext(res, context);
                break;
            }
        }
        node_ptr_t dataNode = any2node(visitExpr(context->expr()));
        res                 = createNodeAs<UnaryExprLoad>(op);
        setNodeTokenRangeByContext(res, context);
        *res << dataNode;
        registerAstSemanticBundle(
            res,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(res, tokenOf(context->child(0)), "ast.unary.operator"),
                    -1,
                    strOp),
                semanticPart(
                    camel::source::SemanticRole::Argument,
                    nodeOrigin(dataNode),
                    0,
                    "operand"),
            });
        break;
    }
    default:
        throw std::runtime_error("Invalid alternative in UnaryExpr");
    }

    LEAVE("UnaryExpr");
    return res;
}

/*
linkExpr
    : annoExpr ('->' annoExpr)*
    ;
*/
Value Builder::visitLinkExpr(cst::LinkExpr context) {
    ENTER("LinkExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp        = context->child(i * 2 - 1)->text();
        node_ptr_t rhsNode  = any2node(visitExpr(context->expr(i)));
        node_ptr_t linkNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        setNodeTokenRangeByContext(linkNode, context);
        linkNode->loadAs<ReservedExprLoad>()->setPipe(true);
        // What the arrow hands on is the receiver of the call, wherever the call is written.
        node_ptr_t dataList      = nullptr;
        node_ptr_t calleeNode    = rhsNode;
        node_ptr_t namedDataList = nullptr;
        if (rhsNode->type() == LoadType::Data &&
            rhsNode->loadAs<DataLoad>()->dataType() == DataType::ReservedExpr &&
            rhsNode->loadAs<ReservedExprLoad>()->op() == ReservedDataOp::Call) {
            // `x->f(a, b)` is `f(x, a, b)` (design principles 4.4): the receiver is the first
            // ordinary argument of the call written on the right, not the value its result is
            // applied to. The call keeps its callee and its named arguments.
            calleeNode    = rhsNode->at(0);
            namedDataList = rhsNode->at(2);
            // The merged call stands for the one on the right: a positional argument written
            // after a name there is still the same refusal.
            linkNode->loadAs<ReservedExprLoad>()->setPositionalAfterNamed(
                rhsNode->loadAs<ReservedExprLoad>()->positionalAfterNamed());
            calleeNode->setParent(nullptr);
            namedDataList->setParent(nullptr);
            dataList = createNodeAs<RepeatedLoad>("Data");
            setNodeTokenRangeBetween(dataList, context->expr(i - 1), context->expr(i));
            *dataList << lhsNode;
            // The written arguments move from the call on the right into the merged list; a
            // node is in one place, so they are detached from it first.
            node_ptr_t written = rhsNode->at(1);
            for (const auto &argument : *written) {
                argument->setParent(nullptr);
                *dataList << argument;
            }
        } else {
            dataList = createNodeAs<RepeatedLoad>("Data");
            setNodeTokenRangeByContext(dataList, context->expr(i));
            namedDataList = createNodeAs<RepeatedLoad>("NamedData");
            setNodeTokenRangeByContext(namedDataList, context->expr(i));
            *dataList << lhsNode;
        }
        *linkNode << calleeNode << dataList << namedDataList;
        std::vector<camel::source::SemanticPart> parts = {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(
                    linkNode,
                    tokenOf(context->child(i * 2 - 1)),
                    "ast.link.operator"),
                -1,
                strOp),
            semanticPart(camel::source::SemanticRole::Callee, nodeOrigin(calleeNode), -1, "callee"),
            semanticPart(
                camel::source::SemanticRole::Receiver,
                nodeOrigin(lhsNode),
                -1,
                "receiver"),
            semanticPart(camel::source::SemanticRole::ArgList, nodeOrigin(dataList), -1, "args"),
        };
        // The receiver is argument 0; the arguments written after the arrow follow it.
        int32_t slot = 0;
        for (const auto &argument : *dataList) {
            const char *label = slot == 0 ? "receiver" : "arg";
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Argument,
                nodeOrigin(argument),
                slot++,
                label));
        }
        registerAstSemanticBundle(linkNode, std::move(parts));
        lhsNode = linkNode;
    }
    LEAVE("LinkExpr");
    return lhsNode;
}

/*
rangeExpr
    : additiveExpr (('..' | '<>') additiveExpr)*
    ;
`..` is the interval and `<>` the composition (design principles 4.2). The two are different node
kinds below, so the readings are decided by the token rather than left to the types of the
operands: an interval and a composition are not one construct in the tree.
*/
Value Builder::visitRangeExpr(cst::RangeExpr context) {
    ENTER("RangeExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr(0)));
    for (size_t i = 1; i < context->exprs().size(); i++) {
        string strOp        = context->child(i * 2 - 1)->text();
        const bool compose  = strOp == "<>";
        node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(
            compose ? ReservedDataOp::Compose : ReservedDataOp::Comp);
        setNodeTokenRangeByContext(dataNode, context);
        node_ptr_t rhsNode = any2node(visitExpr(context->expr(i)));
        *dataNode << lhsNode << rhsNode;
        registerAstSemanticBundle(
            dataNode,
            {
                semanticPart(
                    camel::source::SemanticRole::Operator,
                    deriveAstAnchorOrigin(
                        dataNode,
                        tokenOf(context->child(i * 2 - 1)),
                        "ast.comp.operator"),
                    -1,
                    strOp),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(lhsNode), 0, "lhs"),
                semanticPart(camel::source::SemanticRole::Argument, nodeOrigin(rhsNode), 1, "rhs"),
            });
        lhsNode = dataNode;
    }
    LEAVE("RangeExpr");
    return lhsNode;
}

/*
annoExpr
    : accessExpr (({isAdjacent()}? (indices | parentArgues | angledValues | '!')) | memberAccess)*
    ;
*/
Value Builder::visitAnnoExpr(cst::AnnoExpr context) {
    ENTER("AnnoExpr");
    // `node_of(c)` in a rule's condition :
    // "条件函数按 `const` 形参追加隐藏的节点形参，构建期把 `node_of(c)` 换成它；不引入新类型"
    // and "名字是 `node_of(c)`，只接受该规则（或 `fact` 的 `case`）自己的形参名". The call is
    // read here, before the callee is built, and becomes the hidden node parameter the rule
    // declaration appended for that `const` parameter. A name that is not a `const` parameter of
    // this rule is refused here, by name, and `node_of` written where no such parameter exists is
    // refused too, because the map is empty outside a rule condition.
    if (context->expr().is(NK::RefData) && context->expr().text() == "node_of") {
        // The argument is read from the tree, not built: `node_of(c)` is not a call of a
        // function, and building `c` would put a node in the tree that nothing reads. It is one
        // written name and nothing else: `node_of` names a parameter, so anything else (a call,
        // an arithmetic expression) has no parameter to name and is refused below, by name.
        std::string argument;
        if (const cst::ParentArgues argues = context->get<cst::ParentArgues>()) {
            if (const cst::ArgumentList args = argues->get<cst::ArgumentList>()) {
                if (const cst::IndexValues values = args->get<cst::IndexValues>()) {
                    const auto exprs = values->exprs();
                    if (exprs.size() == 1 && exprs[0].kind() == NK::RefData) {
                        argument = std::string(exprs[0].text());
                    }
                }
            }
        }
        const bool writtenArgument = !argument.empty();
        const auto found =
            writtenArgument ? ruleWhenNodeParams_.find(argument) : ruleWhenNodeParams_.end();
        if (writtenArgument && found != ruleWhenNodeParams_.end()) {
            node_ptr_t nodeRef = createNodeAs<RefDataLoad>(Reference(found->second));
            setNodeTokenRangeByContext(nodeRef, context);
            LEAVE("AnnoExpr");
            return nodeRef;
        }
        diags_->of(SemanticDiag::NodeOfNotAParameter)
            .at(rangeOf(context->firstToken(), context->lastToken()))
            .commit(argument, ruleWhenName_);
        // The diagnostic is the report; the condition still needs a value, and `false` says
        // "this rule does not apply here", which is what a name that holds no node means.
        node_ptr_t noNode = createNodeAs<LiteralLoad>(Literal(LiteralType::Boolean, "false"));
        setNodeTokenRangeByContext(noNode, context);
        LEAVE("AnnoExpr");
        return noNode;
    }
    node_ptr_t lhsNode = any2node(visitExpr(context->expr()));
    for (size_t i = 1; i < context->childCount(); i++) {
        auto child = context->child(i);
        if (child.isNode()) {
            node_ptr_t exprNode = nullptr;
            if (child.node().is(NK::Indices)) {
                exprNode = createNodeAs<BinaryExprLoad>(BinaryDataOp::Index);
                setNodeTokenRangeByContext(exprNode, cst::Indices(child.node()));
                node_ptr_t rhsNode = any2node(visitIndices(cst::Indices(child.node())));
                *exprNode << lhsNode << rhsNode;
                registerAstSemanticBundle(
                    exprNode,
                    {
                        // The operator is the bracket: a fault of the index (out of range) is
                        // reported there, as a division by zero is reported at its `/`.
                        semanticPart(
                            camel::source::SemanticRole::Operator,
                            deriveAstAnchorOrigin(
                                exprNode,
                                cst::Indices(child.node())->firstToken(),
                                "ast.index.operator"),
                            -1,
                            "[]"),
                        semanticPart(
                            camel::source::SemanticRole::IndexExpr,
                            nodeOrigin(rhsNode),
                            -1,
                            "indices"),
                        semanticPart(
                            camel::source::SemanticRole::Receiver,
                            nodeOrigin(lhsNode),
                            -1,
                            "target"),
                    });
            } else if (child.node().is(NK::ParentArgues)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
                setNodeTokenRangeByContext(exprNode, cst::ParentArgues(child.node()));
                const ArgumentValues values =
                    value_cast<ArgumentValues>(visitParentArgues(cst::ParentArgues(child.node())));
                node_ptr_t dataList      = values.data;
                node_ptr_t namedDataList = values.named;
                if (values.positionalAfterNamed != nullptr) {
                    // `f(a: 1, 2)`: the call refuses it, at the argument out of place
                    // (design principles 4.4).
                    exprNode->loadAs<ReservedExprLoad>()->setPositionalAfterNamed(
                        nodeOrigin(values.positionalAfterNamed));
                }
                *exprNode << lhsNode << dataList << namedDataList;
                std::vector<camel::source::SemanticPart> parts = {
                    semanticPart(
                        camel::source::SemanticRole::Callee,
                        nodeOrigin(lhsNode),
                        -1,
                        "callee"),
                    semanticPart(
                        camel::source::SemanticRole::ArgList,
                        nodeOrigin(dataList),
                        -1,
                        "args"),
                };
                for (size_t argIndex = 0; argIndex < dataList->size(); ++argIndex) {
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::Argument,
                        nodeOrigin(dataList->at(argIndex)),
                        static_cast<int32_t>(argIndex),
                        "arg"));
                }
                registerAstSemanticBundle(exprNode, std::move(parts));
            } else if (child.node().is(NK::MemberAccess)) {
                auto access = cst::MemberAccess(child.node());
                if (access->child(1).isToken() && access->child(1).text() == "[") {
                    exprNode = makeStaticAccess(
                        lhsNode,
                        access->child(0),
                        access->child(2).node(),
                        access);
                } else {
                    exprNode =
                        makeMemberAccess(lhsNode, access->child(0), access->child(1), access);
                }
            } else if (child.node().is(NK::StructData)) {
                // `Shape::Rect { w: 2 }`: a struct-shape variant is built from a struct literal,
                // which is one argument. The graph builder decides by the shape of the variant
                // whether the literal is its payload or its only field.
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
                setNodeTokenRangeByContext(exprNode, cst::StructData(child.node()));
                node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
                *dataList << any2node(visitStructData(cst::StructData(child.node())));
                *exprNode << lhsNode << dataList << createNodeAs<RepeatedLoad>("NamedData");
                registerAstSemanticBundle(
                    exprNode,
                    {
                        semanticPart(
                            camel::source::SemanticRole::Callee,
                            nodeOrigin(lhsNode),
                            -1,
                            "callee"),
                        semanticPart(
                            camel::source::SemanticRole::ArgList,
                            nodeOrigin(dataList),
                            -1,
                            "args"),
                        semanticPart(
                            camel::source::SemanticRole::Argument,
                            nodeOrigin(dataList->front()),
                            0,
                            "payload"),
                    });
            } else if (child.node().is(NK::TypeArguments)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Instantiate);
                setNodeTokenRangeByContext(exprNode, cst::TypeArguments(child.node()));
                // Positional (`typeList`) or named (`A = int`): a named argument is a
                // NamedTypeLoad that holds its type.
                node_ptr_t typeArgs;
                if (child.node().get<cst::TypeList>()) {
                    typeArgs = any2node(visitTypeList(child.node().get<cst::TypeList>()));
                } else {
                    typeArgs = createNodeAs<RepeatedLoad>("Type");
                    setNodeTokenRangeByContext(typeArgs, cst::TypeArguments(child.node()));
                    for (const auto named : child.node().all<cst::NamedTypeArgument>()) {
                        node_ptr_t namedNode = createNodeAs<NamedTypeLoad>(
                            Reference(named->tok(TK::Identifier)->text()));
                        setNodeTokenRangeByContext(namedNode, named);
                        *namedNode << any2node(visitType(named->type()));
                        *typeArgs << namedNode;
                    }
                }
                *exprNode << lhsNode << typeArgs;
                registerAstSemanticBundle(
                    exprNode,
                    {
                        semanticPart(
                            camel::source::SemanticRole::Callee,
                            nodeOrigin(lhsNode),
                            -1,
                            "callee"),
                        semanticPart(
                            camel::source::SemanticRole::GenericArgument,
                            nodeOrigin(exprNode->at(1)),
                            -1,
                            "typeArgs"),
                    });
            } else if (child.node().is(NK::AngledValues)) {
                exprNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
                setNodeTokenRangeByContext(exprNode, cst::AngledValues(child.node()));
                const ArgumentValues values =
                    value_cast<ArgumentValues>(visitAngledValues(cst::AngledValues(child.node())));
                node_ptr_t dataList      = values.data;
                node_ptr_t namedDataList = values.named;
                *exprNode << lhsNode << dataList << namedDataList;
                std::vector<camel::source::SemanticPart> parts = {
                    semanticPart(
                        camel::source::SemanticRole::Callee,
                        nodeOrigin(lhsNode),
                        -1,
                        "callee"),
                    semanticPart(
                        camel::source::SemanticRole::GenericArgument,
                        nodeOrigin(dataList),
                        -1,
                        "typeArgs"),
                };
                for (size_t argIndex = 0; argIndex < dataList->size(); ++argIndex) {
                    parts.push_back(semanticPart(
                        camel::source::SemanticRole::GenericArgument,
                        nodeOrigin(dataList->at(argIndex)),
                        static_cast<int32_t>(argIndex),
                        "typeArg"));
                }
                registerAstSemanticBundle(exprNode, std::move(parts));
            }
            lhsNode = exprNode;
        } else {
            // Every shape of an `annoExpr` is read above; the postfix `!` is not one of them any
            // more (the parser refuses it: the mark belongs to a `T!`, not to a value).
            ASSERT(false, "an annoExpr child of another shape reached the builder");
        }
    }
    LEAVE("AnnoExpr");
    return lhsNode;
}

node_ptr_t
Builder::makeMemberAccess(node_ptr_t lhs, syn::Child dot, syn::Child member, syn::Node range) {
    syn::TokenRef memberToken = tokenOf(member);
    std::string memberText    = member->text();
    size_t memberBegin        = memberToken ? memberToken->begin() : 0;
    size_t memberEnd          = memberToken ? memberToken->end() : 0;
    if (memberToken && memberToken.kind() == TK::Real) {
        // `t.0.1`: the lexer reads `0.1` as one REAL. It stands for two indices, the first
        // applied here and the second to its result.
        const size_t split = memberText.find('.');
        const bool plain   = split != std::string::npos &&
                             memberText.find_first_not_of("0123456789.") == std::string::npos;
        if (!plain) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(rangeOf(memberToken))
                .commit("member index", memberText);
        } else {
            node_ptr_t first = createNodeAs<ReservedExprLoad>(ReservedDataOp::Access);
            setNodeTokenRangeByContext(first, range);
            Reference firstRef(memberText.substr(0, split));
            node_ptr_t firstMember = createNodeAs<RefDataLoad>(firstRef);
            setNodeTokenRange(
                firstMember,
                static_cast<size_t>(memberToken->index()),
                static_cast<size_t>(memberToken->index() + 1));
            setNodeOriginByOffsets(
                firstMember,
                memberBegin,
                memberBegin + split,
                "ast.access.member");
            *first << lhs << firstMember;
            registerAstSemanticBundle(
                first,
                {
                    semanticPart(
                        camel::source::SemanticRole::Operator,
                        deriveAstAnchorOrigin(first, tokenOf(dot), "ast.access.dot"),
                        -1,
                        "."),
                    semanticPart(
                        camel::source::SemanticRole::Receiver,
                        nodeOrigin(lhs),
                        -1,
                        "target"),
                    semanticPart(
                        camel::source::SemanticRole::MemberName,
                        nodeOrigin(firstMember),
                        -1,
                        firstRef.ident()),
                });
            lhs         = first;
            memberBegin = memberBegin + split + 1;
            memberText  = memberText.substr(split + 1);
        }
    }
    node_ptr_t dataNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Access);
    setNodeTokenRangeByContext(dataNode, range);
    Reference ref(memberText);
    node_ptr_t rhsNode = createNodeAs<RefDataLoad>(ref);
    if (memberToken) {
        setNodeTokenRange(
            rhsNode,
            static_cast<size_t>(memberToken->index()),
            static_cast<size_t>(memberToken->index() + 1));
        setNodeOriginByOffsets(rhsNode, memberBegin, memberEnd, "ast.access.member");
    }
    *dataNode << lhs << rhsNode;
    registerAstSemanticBundle(
        dataNode,
        {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(dataNode, tokenOf(dot), "ast.access.dot"),
                -1,
                "."),
            semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(lhs), -1, "target"),
            semanticPart(
                camel::source::SemanticRole::MemberName,
                nodeOrigin(rhsNode),
                -1,
                ref.ident()),
        });
    return dataNode;
}

/*
accessExpr
    : primaryData ('.' (IDENTIFIER | INTEGER | REAL | '[' dataExpr ']'))*
    ;
*/
Value Builder::visitAccessExpr(cst::AccessExpr context) {
    ENTER("AccessExpr");
    node_ptr_t lhsNode = any2node(visitExpr(context->expr()));
    size_t i           = 1;
    while (i < context->childCount()) {
        std::string strOp = context->child(i)->text();
        if (strOp != ".") {
            throw std::runtime_error("Invalid access operator: " + strOp);
        }
        if (context->child(i + 1).isToken() && context->child(i + 1).text() == "[") {
            // `.` '[' dataExpr ']': a field or element named by a compile-time key.
            const auto &keyChild = context->child(i + 2);
            lhsNode = makeStaticAccess(lhsNode, context->child(i), keyChild.node(), context);
            i += 4;
            continue;
        }
        lhsNode = makeMemberAccess(lhsNode, context->child(i), context->child(i + 1), context);
        i += 2;
    }
    LEAVE("AccessExpr");
    return lhsNode;
}

node_ptr_t
Builder::makeStaticAccess(node_ptr_t lhs, syn::Child dot, syn::Node key, syn::Node range) {
    node_ptr_t dataNode = createNodeAs<StaticAccessLoad>();
    setNodeTokenRangeByContext(dataNode, range);
    node_ptr_t keyNode = any2node(visitExpr(key));
    *dataNode << lhs << keyNode;
    registerAstSemanticBundle(
        dataNode,
        {
            semanticPart(
                camel::source::SemanticRole::Operator,
                deriveAstAnchorOrigin(dataNode, tokenOf(dot), "ast.access.static"),
                -1,
                ".[]"),
            semanticPart(camel::source::SemanticRole::Receiver, nodeOrigin(lhs), -1, "target"),
            semanticPart(camel::source::SemanticRole::IndexExpr, nodeOrigin(keyNode), -1, "key"),
        });
    return dataNode;
}

/*
pattern
    : identRef ('(' (pattern (',' pattern)*)? ','? ')' | '{' (IDENTIFIER (':' pattern)? ','?)* '}'
      | identDef)?
    | literal
    | '_'
    ;
*/
Value Builder::visitEnumPattern(cst::Pattern context, cst::IdentRef ref) {
    ENTER("EnumPattern");
    const auto structPattern = context->get<cst::EnumStructPattern>();
    node_ptr_t node =
        createNodeAs<EnumPatternLoad>(Reference(ref->text()), static_cast<bool>(structPattern));
    setNodeTokenRangeByContext(node, context);
    auto &fields    = node->loadAs<EnumPatternLoad>()->fields();
    const auto bind = [&](cst::Pattern pattern, const std::string &name) {
        std::string binding;
        if (pattern->text() != "_") {
            // A field is a `fieldPattern`: a name or `_`. The parser refuses a nested pattern and
            // a literal where it is written (`parsePattern`), so a name is all that arrives.
            ASSERT(
                pattern->get<cst::IdentRef>() && !pattern->get<cst::EnumTuplePattern>() &&
                    !pattern->get<cst::EnumStructPattern>(),
                "a field pattern of another shape reached the builder");
            binding = pattern->get<cst::IdentRef>()->text();
        }
        fields.push_back(EnumPatternField{name, binding});
    };
    if (structPattern) {
        for (size_t i = 0; i < structPattern->childCount(); ++i) {
            const auto child = structPattern->child(i);
            if (!child.isToken() || child.token()->kind() != TK::Identifier) {
                continue;
            }
            std::string name = child.text();
            const auto next  = structPattern->child(i + 1);
            if (next.isToken() && next.token()->kind() == TK::Colon) {
                const auto value = structPattern->child(i + 2);
                if (value.isNode()) {
                    bind(cst::Pattern(value.node()), name);
                }
            } else {
                // `{ w, h }` binds the field of that name.
                fields.push_back(EnumPatternField{name, name});
            }
        }
    } else if (const auto tuplePattern = context->get<cst::EnumTuplePattern>()) {
        for (const auto pattern : tuplePattern->all<cst::Pattern>()) {
            bind(pattern, "");
        }
    }
    LEAVE("EnumPattern");
    return node;
}

/*
loopExpr : 'loop' loopState (',' loopState)* loopFor? loopWhile? 'do' blockExpr ;
loopState : identDef '=' logicalOrExpr ;
The children of the node are described at LoopExprLoad.
*/
Value Builder::visitLoopExpr(cst::LoopExpr context) {
    std::vector<Reference> state;
    node_ptr_t inits = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(inits, context);
    for (const auto entry : context->all<cst::LoopState>()) {
        state.emplace_back(entry->get<cst::IdentDef>()->text());
        *inits << any2node(visitExpr(entry->expr()));
    }
    const auto forClause   = context->get<cst::LoopFor>();
    const auto whileClause = context->get<cst::LoopWhile>();
    Reference binder;
    if (forClause) {
        binder = Reference(forClause->get<cst::IdentDef>()->text());
    }
    node_ptr_t loopNode = createNodeAs<LoopExprLoad>(state, binder);
    setNodeTokenRangeByContext(loopNode, context);
    *loopNode << inits;
    node_ptr_t sequence = createNodeAs<OptionalLoad>("Data");
    if (forClause) {
        setNodeTokenRangeByContext(sequence, forClause);
        *sequence << any2node(visitExpr(forClause->expr()));
    }
    node_ptr_t condition = createNodeAs<OptionalLoad>("Data");
    if (whileClause) {
        setNodeTokenRangeByContext(condition, whileClause);
        *condition << any2node(visitExpr(whileClause->expr()));
    }
    *loopNode << sequence << condition;
    // The expression after `do`, read as the arm of an `if` is: always a block on the AST.
    *loopNode << any2node(visitBlockExpr(context->lastChild().node()));
    return loopNode;
}

/*
editExpr : 'edit' identDef '=' logicalOrExpr stmtBlock ;
viewExpr : 'view' identDef '=' logicalOrExpr stmtBlock ;
The children of the node are described at BlockHeadLoad.
*/
Value Builder::visitEditExpr(cst::EditExpr context) {
    return visitBlockHeaded(context, DataType::EditExpr);
}

Value Builder::visitViewExpr(cst::ViewExpr context) {
    return visitBlockHeaded(context, DataType::ViewExpr);
}

Value Builder::visitBlockHeaded(syn::Node context, DataType type) {
    const char *kindName = type == DataType::EditExpr ? "EditExpr" : "ViewExpr";
    ENTER(kindName);
    node_ptr_t node = createNodeAs<BlockHeadLoad>(type, Reference(context.get<cst::IdentDef>().text()));
    setNodeTokenRangeByContext(node, context);
    // [0] the function the block is over. [1] the body: a StmtBlock whether the source wrote
    // statements or a single expression, as the arm of an `if` is (design 15.1, "块里最后一个
    // 表达式是块的值"). A head that lost a piece to a syntax error is built with the pieces it kept.
    if (const syn::Node over = context.expr(); over) {
        *node << any2node(visitExpr(over));
    }
    if (const syn::Child body = context.lastChild(); body.isNode()) {
        *node << any2node(visitBlockExpr(body.node()));
    }
    LEAVE(kindName);
    return node;
}

// ---- Dispatch on the kind of a node ----
