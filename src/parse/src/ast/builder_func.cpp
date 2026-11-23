// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the function: its declaration, the lambda form and the decorators over it,
// and the two lists every call and every signature is written with -- parameters, arguments,
// indices and the paired slot `a: T` / `a = v` inside them.

#include "builder_internal.h"

namespace {

/// Appends one `Parameter` part per parameter written in a function type load (`with` parameters
/// first, then the ordinary ones; each with its slot in the signature and the position it is
/// written at). A port of the function's graph stands for one parameter, so that what is said
/// about a parameter -- the refusal of a function value that came in through it -- points at the
/// parameter itself. The list keeps its own `params` part as well, for what reads the parameter
/// list as one place.
inline void appendParameterParts(
    std::vector<camel::source::SemanticPart> &parts, const node_ptr_t &funcTypeNode) {
    if (!funcTypeNode || funcTypeNode->size() < 2) {
        return;
    }
    const auto add = [&](const node_ptr_t &params, size_t firstSlot) {
        if (!params) {
            return;
        }
        for (size_t i = 0; i < params->size(); ++i) {
            parts.push_back(semanticPart(
                camel::source::SemanticRole::Parameter,
                nodeOrigin(params->at(i)),
                static_cast<int32_t>(firstSlot + i),
                "param"));
        }
    };
    add(funcTypeNode->at(0), 0);
    add(funcTypeNode->at(1), funcTypeNode->at(0)->size());
}

} // namespace

/*
funcData   : modifiers? angledParams? parentParams ('=>' typeExpr)? '=>' blockExpr ;
*/
Value Builder::visitFuncData(cst::FuncData context) {
    ENTER("FuncData");
    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    setNodeTokenRangeByContext(funcTypeNode, context);
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
    }
    funcType->setModifiers(modSet);

    if (context->get<cst::AngledParams>()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->get<cst::AngledParams>()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->get<cst::ParentParams>()) {
        node_ptr_t normParams = any2node(visitParentParams(context->get<cst::ParentParams>()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(
        typeOptNode,
        context->firstToken()->index(),
        (context->type() ? context->type()->lastToken()->index()
                         : context->get<cst::ParentParams>()->firstToken()->index()) +
            1);
    setNodeOriginByOffsets(
        typeOptNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>((context->type() ? context->type()->lastToken()
                                             : context->get<cst::ParentParams>()->firstToken())
                                ->end()));
    if (context->type()) {
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    node_ptr_t blockNode = any2node(visitBlockExpr(context->lastChild().node()));

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>();
    setNodeTokenRangeByContext(funcNode, context);
    *funcNode << funcTypeNode;
    *funcNode << blockNode;
    std::vector<camel::source::SemanticPart> semanticParts{
        semanticPart(
            camel::source::SemanticRole::GenericParameter,
            nodeOrigin(funcTypeNode->at(0)),
            -1,
            "withParams"),
        semanticPart(
            camel::source::SemanticRole::Parameter,
            nodeOrigin(funcTypeNode->at(1)),
            -1,
            "params"),
        semanticPart(
            camel::source::SemanticRole::ReturnType,
            typeOptNode->empty() ? camel::source::kInvalidOriginId
                                 : nodeOrigin(typeOptNode->front()),
            -1,
            "returnType"),
        semanticPart(camel::source::SemanticRole::ValueProducer, nodeOrigin(blockNode), -1, "body"),
    };
    // Each parameter as written, with its own position and its slot in the signature: a port of
    // the function's graph stands for one parameter, and what is said about a parameter points at
    // where it is written.
    appendParameterParts(semanticParts, funcTypeNode);
    registerAstSemanticBundle(funcNode, std::move(semanticParts));

    LEAVE("FuncData");
    return funcNode;
}

/*
funcAnno   : '@' identRef (angledValues | parentIdents)? ;
A decorator (`@name`, `@name<args>`). Names in parentheses are the removed `@numeric(...)`
spelling: it is reported here, where it is written, and names the tag form that replaced it.
*/
bool Builder::rejectParenthesizedName(cst::FuncAnno context) {
    if (!context->get<cst::ParentIdents>()) {
        return false;
    }
    const std::string annotation = context->get<cst::IdentRef>()->text();
    const std::string why =
        annotation == "numeric"
            ? "the numeric permissions are tags now: write '#[reassociate]' (or "
              "'#[reassociate, contract]', '#[finite_math]', ...) before the function or the "
              "block"
            : "an annotation takes '<...>' arguments, not '(...)'; a tag is written '#name', "
              "'#name(args)' or '#[a, b]'";
    diags_->of(SemanticDiag::InvalidAnnotation)
        .at(rangeOf(context->firstToken(), context->lastToken()))
        .commit(annotation, why);
    return true;
}

bool Builder::rejectVjpAnnotation(cst::FuncAnno context) {
    const std::string annotation = context->get<cst::IdentRef>()->text();
    if (annotation != "vjp" && annotation != "jvp") {
        return false;
    }
    const std::string tag = annotation == "vjp" ? "vjp" : "jvp";
    diags_->of(SemanticDiag::InvalidAnnotation)
        .at(rangeOf(context->firstToken(), context->lastToken()))
        .commit(
            annotation,
            "a derivative rule is a declaration tag now: write '#" + tag +
                "(rule)' before the function (design principles 6.2, 8)");
    return true;
}

Value Builder::visitFuncAnno(cst::FuncAnno context) {
    ENTER("FuncAnno");

    Reference ref = value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>()));
    if (!context->get<cst::AngledValues>()) {
        // `@name`: the decorator itself.
        node_ptr_t refNode = createNodeAs<RefDataLoad>(ref);
        setNodeTokenRangeByContext(refNode, context);
        LEAVE("FuncAnno");
        return refNode;
    }
    // `@name<args>`: the decorator bound to its with-arguments.
    node_ptr_t annoNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
    setNodeTokenRangeByContext(annoNode, context);
    *annoNode << createNodeAs<RefDataLoad>(ref);
    const ArgumentValues values =
        value_cast<ArgumentValues>(visitAngledValues(context->get<cst::AngledValues>()));
    *annoNode << values.data << values.named;

    LEAVE("FuncAnno");
    return annoNode;
}

/*
funcDecl   :
        funcAnno*
        (WITH angledParams)?
        EXPORT? modifiers?
        FUNC identRef parentParams ('=>' typeExpr)? stmtBlock ;
The result type is written `=> T` (design principles 4.4); a `:` there introduces no type and
is refused at the `:`.
*/
Value Builder::visitFuncDecl(cst::FuncDecl context) {
    ENTER("FuncDecl");

    // Decorators are applied to the function. The removed `@name(args)` spelling and the removed
    // `@vjp<rule>` decorator are reported and add no decorator (numeric permissions and custom
    // derivative rules are tags now, design principles 7.2, 8).
    node_ptr_t annoListNode = nullptr;
    for (const auto &funcAnno : context->all<cst::FuncAnno>()) {
        if (rejectParenthesizedName(funcAnno) || rejectVjpAnnotation(funcAnno)) {
            continue;
        }
        if (!annoListNode) {
            annoListNode = createNodeAs<RepeatedLoad>("FuncAnno");
        }
        *annoListNode << any2node(visitFuncAnno(funcAnno));
    }

    // A function written without a body is a declaration of an operator of a C++ module (design
    // principles 6.4): the signature is the whole of it. Its token range and origin end with the
    // signature, not with a block that is not there.
    const auto blockContext = context->get<cst::StmtBlock>();
    node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
    TokenRange typeRange    = {
        context->firstToken()->index(),
        blockContext ? blockContext->firstToken()->index() + 1 : context->lastToken()->index() + 1};
    setNodeTokenRange(funcTypeNode, typeRange);
    setNodeOriginByOffsets(
        funcTypeNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(
            blockContext ? blockContext->firstToken()->begin() : context->lastToken()->end()));
    auto funcType = unwrapNodeAs<FuncTypeLoad>(funcTypeNode);

    auto modifier = context->get<cst::Modifiers>();
    ModifierSet modSet;
    if (modifier) {
        if (modifier->tokCount(TK::KwSync) > 0) {
            modSet.insert(Modifier::Sync);
        }
    }
    funcType->setModifiers(modSet);

    // The type parameters declared at the head (`func first::<T>(...)`, design principles 5.5).
    // They are the names the declaration's type variables are; the GCT builder puts them in
    // scope before it visits a parameter type, so one of them wins over a type of the same name.
    // One written with `...` is a pack and not one type (`func zip::<...A>`, 5.6).
    if (const auto typeParams = context->get<cst::TypeParams>()) {
        std::vector<TypeParam> params;
        for (const auto ident : typeParams->all<cst::IdentDef>()) {
            params.emplace_back(ident->text(), ident->writtenAsPack());
        }
        funcType->setTypeParams(std::move(params));
    }

    if (context->get<cst::AngledParams>()) {
        node_ptr_t withParams = any2node(visitAngledParams(context->get<cst::AngledParams>()));
        *funcTypeNode << withParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    if (context->get<cst::ParentParams>()) {
        node_ptr_t normParams = any2node(visitParentParams(context->get<cst::ParentParams>()));
        *funcTypeNode << normParams;
    } else {
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
    }

    node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
    setNodeTokenRange(typeOptNode, typeRange);
    setNodeOriginByOffsets(
        typeOptNode,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(
            blockContext ? blockContext->firstToken()->begin() : context->lastToken()->end()));
    if (context->type()) {
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;
    if (context->get<cst::ResourceClause>()) {
        *funcTypeNode << any2node(visitResourceClause(context->get<cst::ResourceClause>()));
    }

    // The name: an identifier, or the operator of an overload (`func operator +(a, b)`, design
    // principles 4.3). An overload is a function under the reserved name of its operator
    // (`__add__`), chosen by the symbol and the number of parameters (`-` with one is the
    // negation). It is not qualified by an enclosing namespace: an operator has no name a
    // program could qualify, and every overload of it is one set (10.2). A symbol that cannot
    // be overloaded with that many parameters gets a name no program can write; the GCT
    // builder reports it with the reason.
    Reference ref;
    std::string operatorSymbol;
    syn::Node nameNode = context->get<cst::IdentRef>();
    if (const auto operatorName = context->get<cst::OperatorName>()) {
        nameNode           = operatorName;
        operatorSymbol     = operatorName->text().substr(std::string_view("operator").size());
        const size_t arity = funcTypeNode->at(1)->size();
        const auto group   = camel::core::overloadableOperatorGroup(operatorSymbol, arity);
        ref                = Reference(group ? std::string(*group) : "operator " + operatorSymbol);
    } else {
        ref = qualifyRef(value_cast<Reference>(visitIdentRef(context->get<cst::IdentRef>())));
    }
    if (context->tok(TK::KwExport)) {
        export_->addRef(ref);
    }

    // The body, when the declaration has one. A `FuncDataLoad` without a `StmtBlock` child is a
    // body-less declaration: the GCT builder makes a FUNC of it that no graph is built from.
    node_ptr_t blockNode = nullptr;
    if (blockContext) {
        blockNode = any2node(visitStmtBlock(blockContext));
    }

    node_ptr_t whenNode = nullptr;
    std::string whenText;
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        whenNode = any2node(visitExpr(whenClause->expr()));
        whenText = std::string(whenClause->expr().sourceText());
    }

    node_ptr_t funcNode = createNodeAs<FuncDataLoad>(ref);
    setNodeTokenRangeByContext(funcNode, context);
    unwrapNodeAs<FuncDataLoad>(funcNode)->setWhen(whenNode);
    unwrapNodeAs<FuncDataLoad>(funcNode)->setWhenText(std::move(whenText));
    *funcNode << funcTypeNode;
    *funcNode << blockNode;
    // The signature as the node the function is built from knows it: the same parts the body of a
    // lambda registers (`visitFuncData`), so that the graph of a declared function reads its
    // parameters from where its body is built, whichever of the two nodes it is entered from.
    {
        std::vector<camel::source::SemanticPart> funcParts{
            semanticPart(
                camel::source::SemanticRole::GenericParameter,
                nodeOrigin(funcTypeNode->at(0)),
                -1,
                "withParams"),
            semanticPart(
                camel::source::SemanticRole::Parameter,
                nodeOrigin(funcTypeNode->at(1)),
                -1,
                "params"),
            semanticPart(
                camel::source::SemanticRole::ReturnType,
                typeOptNode->empty() ? camel::source::kInvalidOriginId
                                     : nodeOrigin(typeOptNode->front()),
                -1,
                "returnType"),
            semanticPart(
                camel::source::SemanticRole::ValueProducer,
                blockNode ? nodeOrigin(blockNode) : camel::source::kInvalidOriginId,
                -1,
                "body"),
        };
        appendParameterParts(funcParts, funcTypeNode);
        registerAstSemanticBundle(funcNode, std::move(funcParts));
    }

    node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(ref);
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDoc(docOf(context));
    unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setOperatorSymbol(operatorSymbol);
    if (!operatorSymbol.empty()) {
        unwrapNodeAs<FuncDeclLoad>(funcDeclNode)->setDeclaredIn(namespaceStack_);
    }
    if (annoListNode) {
        *funcDeclNode << annoListNode;
    }
    // The tag uses written before the function, after the decorators. They are read when the
    // function is compiled: a tag declared `on func` belongs to the declaration, one declared
    // `on nodes` to the nodes of its body (design principles 6.2).
    addTagUses(funcDeclNode, context);
    *funcDeclNode << funcNode;
    setNodeTokenRangeByContext(funcDeclNode, context);
    std::vector<camel::source::SemanticPart> declSemanticParts{
        semanticPart(
            camel::source::SemanticRole::FuncName,
            deriveAstAnchorOrigin(funcDeclNode, nameNode, "ast.func.name"),
            -1,
            ref.toString()),
        semanticPart(
            camel::source::SemanticRole::GenericParameter,
            nodeOrigin(funcTypeNode->at(0)),
            -1,
            "withParams"),
        semanticPart(
            camel::source::SemanticRole::Parameter,
            nodeOrigin(funcTypeNode->at(1)),
            -1,
            "params"),
        semanticPart(
            camel::source::SemanticRole::ReturnType,
            typeOptNode->empty() ? camel::source::kInvalidOriginId
                                 : nodeOrigin(typeOptNode->front()),
            -1,
            "returnType"),
        semanticPart(
            camel::source::SemanticRole::ValueProducer,
            blockNode ? nodeOrigin(blockNode) : camel::source::kInvalidOriginId,
            -1,
            "body"),
    };
    // Each parameter as written, with its own position and its slot in the signature: the
    // declaration's own node is where the parameter list is read as one place, and where the
    // parameters of a declaration without a body are the whole of it.
    appendParameterParts(declSemanticParts, funcTypeNode);
    registerAstSemanticBundle(funcDeclNode, std::move(declSemanticParts));

    LEAVE("FuncDecl");
    return funcDeclNode;
}

/*
restValue : '...' dataExpr ;
The argument is a compile-time tuple whose elements are the arguments (design principles 5.5);
GCT keeps the marker and GIR expands it at the call.
*/
Value Builder::visitRestValue(cst::RestValue context) {
    ENTER("RestValue");
    node_ptr_t res = createNodeAs<DataLoad>(DataType::SpreadArg);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitExpr(context->expr()));
    LEAVE("RestValue");
    return res;
}

/*
keyTypePair  : identDef ':' typeExpr ;
*/
Value Builder::visitKeyTypePair(cst::KeyTypePair context) {
    ENTER("KeyTypePair");
    Reference ref(context->get<cst::IdentDef>()->text());
    node_ptr_t namedTypeNode = createNodeAs<NamedTypeLoad>(ref);
    setNodeTokenRangeByContext(namedTypeNode, context);
    if (context->type()) {
        *namedTypeNode << any2node(visitType(context->type()));
    }
    // The tags of a field (design principles 6.2): kept with the named type, so that the type
    // declaration that owns the field can record them (`#tag x: int`).
    addTagUses(namedTypeNode, context);
    LEAVE("KeyTypePair");
    return namedTypeNode;
}

/*
keyValuePair : identDef ':' dataExpr ;
*/
Value Builder::visitKeyValuePair(cst::KeyValuePair context) {
    ENTER("KeyValuePair");
    // A key is always a name; only a text that ends inside the pair leaves the node without one,
    // and a placeholder key keeps the rest of the file checked.
    Reference ref(
        context->get<cst::IdentDef>() ? context->get<cst::IdentDef>()->text() : std::string("_"));
    node_ptr_t namedDataNode = createNodeAs<NamedDataLoad>(ref);
    setNodeTokenRangeByContext(namedDataNode, context);
    if (context->expr()) {
        *namedDataNode << any2node(visitExpr(context->expr()));
    }
    LEAVE("KeyValuePair");
    return namedDataNode;
}

/*
keyParamPair : ('var' | 'const' | 'constas')? identDef (':' typeExpr ('=' dataExpr)?)?
    | '...' identDef (':' typeExpr)? ;
A type left out is a lambda's parameter (design principles 5.9): it stands as an InferTypeLoad,
which GCT lowers to a hole and GIR fills from the function type the position expects. A `constas`
parameter writes its type; one left out stays a pair with no type child, so GCT can report it
(design principles 5.4).
*/
Value Builder::visitKeyParamPair(cst::KeyParamPair context) {
    ENTER("KeyParamPair");
    const bool isRest    = static_cast<bool>(context->tok(TK::Ellipsis)) ||
                           static_cast<bool>(context->tok(TK::DotDot));
    const bool isConstas = context->has(TK::KwConstas);
    bool isVar           = false;
    if (context->tok(TK::KwVar)) {
        isVar = true;
    }
    const bool isConst = context->has(TK::KwConst);
    // `...ParamsOf<F>` in a function type's parameter list has no name: the pack is its type.
    Reference ref(
        context->get<cst::IdentDef>() ? std::string(context->get<cst::IdentDef>()->text())
                                      : std::string("__pack"));
    node_ptr_t namedPairNode = createNodeAs<NamedPairLoad>(ref, isVar, isConst, isRest, isConstas);
    setNodeTokenRangeByContext(namedPairNode, context);
    if (isRest || isConstas) {
        // `...ts` is untyped: the tuple of the arguments left over is made at the call. A type
        // written after either is still read, so that GCT can report it (the rest parameter
        // refuses one; a `constas` needs one).
        if (context->type()) {
            *namedPairNode << any2node(visitType(context->type()));
        }
        // The default of a `constas` parameter of the `with` list.
        if (isConstas && context->type() && context->expr()) {
            node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
            setNodeTokenRangeByContext(dataExprOptNode, context->expr());
            *dataExprOptNode << any2node(visitExpr(context->expr()));
            *namedPairNode << dataExprOptNode;
        }
    } else if (context->type()) {
        *namedPairNode << any2node(visitType(context->type()));
    } else {
        node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
        setNodeTokenRangeByContext(inferNode, context->get<cst::IdentDef>());
        *namedPairNode << inferNode;
    }
    if (!isRest && !isConstas) {
        node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
        if (context->expr()) {
            setNodeTokenRangeByContext(dataExprOptNode, context->expr());
            *dataExprOptNode << any2node(visitExpr(context->expr()));
        }
        *namedPairNode << dataExprOptNode;
    }
    LEAVE("KeyParamPair");
    return namedPairNode;
}

/*
tupleParam : '(' identDef (',' identDef)* ','? ')' ;
A lambda parameter destructured as a tuple: the parameter has a name the program does not write,
and the names take the elements of the tuple it is passed (the rule of `let (a, b) = p`).
*/
Value Builder::visitTupleParam(cst::TupleParam context) {
    ENTER("TupleParam");
    std::vector<std::string> names;
    for (const auto &identDef : context->all<cst::IdentDef>()) {
        names.push_back(identDef->text());
    }
    // The name is internal: no program writes it, and the count of such parameters before it in
    // the file makes it unique. It does not depend on where the parameter is written, so a file
    // formatted differently has the same tree.
    Reference ref("_tuple_" + std::to_string(tupleParams_++));
    node_ptr_t namedPairNode = createNodeAs<NamedPairLoad>(ref);
    namedPairNode->loadAs<NamedPairLoad>()->setTupleNames(std::move(names));
    setNodeTokenRangeByContext(namedPairNode, context);
    node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
    setNodeTokenRangeByContext(inferNode, context);
    *namedPairNode << inferNode;
    node_ptr_t dataExprOptNode = createNodeAs<OptionalLoad>("DataExpr");
    *namedPairNode << dataExprOptNode;
    LEAVE("TupleParam");
    return namedPairNode;
}

/*
dataList     : dataExpr (',' dataExpr)* ;
*/
Value Builder::visitDataList(cst::DataList context) {
    ENTER("DataList");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &dataExpr : context->exprs()) {
        *rep << any2node(visitExpr(dataExpr));
    }
    LEAVE("DataList");
    return rep;
}

/*
identList    : identDef (',' identDef)* ;
*/
Value Builder::visitIdentList(cst::IdentList context) {
    ENTER("IdentList");
    std::vector<Reference> refs;
    for (const auto &identDef : context->all<cst::IdentDef>()) {
        refs.emplace_back(identDef->text());
    }
    LEAVE("IdentList");
    return refs;
}

/*
indexValues  : indexValue (',' indexValue)* ;
*/
Value Builder::visitIndexValues(cst::IndexValues context) {
    ENTER("IndexValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &indexValue : context->exprs()) {
        *rep << any2node(visitExpr(indexValue));
    }
    LEAVE("IndexValues");
    return rep;
}

/*
pairedValues : keyValuePair (',' keyValuePair)* ;
In a call's parentheses the list may also hold a positional argument written after a named one:
it is a StrayPositional, and the call refuses it. The named arguments are all
this function returns.
*/
Value Builder::visitPairedValues(cst::PairedValues context) {
    ENTER("PairedValues");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(rep, context);
    for (const auto &keyValuePair : context->all<cst::KeyValuePair>()) {
        *rep << any2node(visitKeyValuePair(keyValuePair));
    }
    LEAVE("PairedValues");
    return rep;
}

/*
strayPositional : indexValue ;
*/
Value Builder::visitStrayPositional(cst::StrayPositional context) {
    ENTER("StrayPositional");
    node_ptr_t value = nullptr;
    if (context->expr()) {
        value = any2node(visitExpr(context->expr()));
    }
    LEAVE("StrayPositional");
    return value;
}

/*
pairedParams : keyParamPair (',' keyParamPair)* ;
*/
Value Builder::visitPairedParams(cst::PairedParams context) {
    ENTER("PairedParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    for (const syn::Node child : context->nodes()) {
        if (child.is(NK::KeyParamPair)) {
            *rep << any2node(visitKeyParamPair(cst::KeyParamPair(child)));
        } else if (child.is(NK::TupleParam)) {
            *rep << any2node(visitTupleParam(cst::TupleParam(child)));
        }
    }
    LEAVE("PairedParams");
    return rep;
}

/*
argumentList : indexValues (',' pairedValues)? | pairedValues ;
*/
Value Builder::visitArgumentList(cst::ArgumentList context) {
    ENTER("ArgumentList");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    if (context->get<cst::IndexValues>()) {
        setNodeTokenRangeByContext(dataList, context->get<cst::IndexValues>());
        dataList = any2node(visitIndexValues(context->get<cst::IndexValues>()));
    }
    node_ptr_t namedDataList   = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::PairedValues>()) {
        const auto &paired = context->get<cst::PairedValues>();
        setNodeTokenRangeByContext(namedDataList, paired);
        namedDataList = any2node(visitPairedValues(paired));
        for (const auto &stray : paired->all<cst::StrayPositional>()) {
            // The first one is where the report points; more than one is the same refusal.
            if (strayPositional == nullptr) {
                strayPositional = any2node(visitStrayPositional(stray));
            }
        }
    }
    LEAVE("ArgumentList");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
indices : '[' (sliceIndices | dataExpr) (',' (sliceIndices | dataExpr))* ']' ;
sliceIndices : ('..' dataExpr? | dataExpr ('..' dataExpr?)?) | rangeCall ;
*/
Value Builder::visitIndices(cst::Indices context) {
    ENTER("Indices");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    for (const syn::Node entry : context->nodes()) {
        if (entry.is(NK::SliceIndices)) {
            // Three slots, one per part, an unwritten part a null load: the graph builder tells
            // a slice from an element subscript by the node, not by counting children. Which
            // slot a part takes follows from the `..` tokens the tree kept: a part before the
            // first `..` is the start, one after it the end. A stepped slice writes its three
            // parts inside `range(...)`, where the `range` token is the marker of that reading
            // and the parts are written in order.
            const cst::SliceIndices slice(entry);
            node_ptr_t sliceNode = createNodeAs<SliceLoad>();
            setNodeTokenRangeByContext(sliceNode, slice);
            node_ptr_t slots[3] = {nullptr, nullptr, nullptr};
            size_t marks        = 0;
            size_t written      = 0;
            bool stepped        = false;
            for (size_t i = 0; i < slice->childCount(); ++i) {
                const auto child = slice->child(i);
                if (child.isToken()) {
                    if (child.token().kind() == TK::DotDot) {
                        ++marks;
                    } else if (child.token().kind() == TK::Identifier) {
                        // Any name here is the marker of the stepped form: a dimension that is not
                        // a slice is a data expression, and the parser only kept a name in a
                        // `sliceIndices` for `range(...)`, whose parts are written in order. What
                        // the name is decides nothing, so it is not read.
                        stepped = true;
                    }
                    continue;
                }
                const size_t slot = stepped ? written : marks;
                if (slot < 3) {
                    slots[slot] = any2node(visitExpr(child.node()));
                }
                ++written;
            }
            for (node_ptr_t &slot : slots) {
                if (slot == nullptr) {
                    slot = createNodeAs<NullLoad>();
                    setNodeTokenRangeByContext(slot, slice);
                }
                *sliceNode << slot;
            }
            *dataList << sliceNode;
        } else {
            *dataList << any2node(visitExpr(entry));
        }
    }
    LEAVE("Indices");
    return dataList;
}

/*
parentParams : '(' pairedParams? ','? ')' ;
*/
Value Builder::visitParentParams(cst::ParentParams context) {
    ENTER("ParentParams");
    node_ptr_t rep = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(rep, context);
    const auto &pairedParams = context->get<cst::PairedParams>();
    if (pairedParams) {
        rep = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("ParentParams");
    return rep;
}

/*
parentArgues : '(' argumentList? ','? ')' ;
*/
Value Builder::visitParentArgues(cst::ParentArgues context) {
    ENTER("ParentArgues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList   = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::ArgumentList>()) {
        ArgumentValues values =
            value_cast<ArgumentValues>(visitArgumentList(context->get<cst::ArgumentList>()));
        dataList        = values.data;
        namedDataList   = values.named;
        strayPositional = values.positionalAfterNamed;
    }
    LEAVE("ParentArgues");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
angledParams : '<' pairedParams? ','? '>' ;
*/
Value Builder::visitAngledParams(cst::AngledParams context) {
    ENTER("AngledParams");
    node_ptr_t res = createNodeAs<RepeatedLoad>("NamedPair");
    setNodeTokenRangeByContext(res, context);
    const auto &pairedParams = context->get<cst::PairedParams>();
    if (pairedParams) {
        res = any2node(visitPairedParams(pairedParams));
    }
    LEAVE("AngledParams");
    return res;
}

/*
angledValues : '<' argumentList? ','? '>' ;
*/
Value Builder::visitAngledValues(cst::AngledValues context) {
    ENTER("AngledValues");
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    node_ptr_t namedDataList   = createNodeAs<RepeatedLoad>("NamedData");
    node_ptr_t strayPositional = nullptr;
    if (context->get<cst::ArgumentList>()) {
        ArgumentValues values =
            value_cast<ArgumentValues>(visitArgumentList(context->get<cst::ArgumentList>()));
        dataList        = values.data;
        namedDataList   = values.named;
        strayPositional = values.positionalAfterNamed;
    }
    LEAVE("AngledValues");
    return ArgumentValues{dataList, namedDataList, strayPositional};
}

/*
identDef : IDENTIFIER ;
*/
Value Builder::visitIdentDef(cst::IdentDef context) {
    ENTER("IdentDef");
    Reference ref(context->tok(TK::Identifier)->text());
    LEAVE("IdentDef");
    return ref;
}

/*
identRef : (IDENTIFIER '::')* IDENTIFIER ;
*/
Value Builder::visitIdentRef(cst::IdentRef context) {
    ENTER("IdentRef");
    Reference ref(context->text());
    LEAVE("IdentRef");
    return ref;
}
