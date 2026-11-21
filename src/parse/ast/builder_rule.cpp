// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the rewrite surface: `rule` and `fact` -- the sides, the const parameters,
// the head the reader indexes a ruleset by, and the parameter-type string that tells two laws of
// one operator apart.

#include "builder_internal.h"

namespace {

/// A rule's parameters as one string: the type written for each, in order, with the whitespace
/// between the tokens squeezed out and the positions joined by `|` (`T[...]|T`). Empty for a rule
/// that takes none.
///
/// This is the part of a rule's signature the head cannot answer -- the head is the operator, and
/// two laws of one operator with different parameter types are two laws (design principles 5.4,
/// 9.11) -- and it is read here, where the declaration is
/// still syntax, for the same reason the head is: at the stage a module's declarations are read a
/// rule's `__lhs` has no body, so nothing later has these types as they were written.
std::string parameterTypesOf(cst::ParentParams params) {
    std::string joined;
    const auto paired = params ? params->get<cst::PairedParams>() : cst::PairedParams();
    if (!paired) {
        return joined;
    }
    bool first = true;
    for (const syn::Node child : paired->nodes()) {
        if (!child.is(NK::KeyParamPair)) {
            continue; // a tuple parameter: one position, many types, not the one this string holds
        }
        const cst::KeyParamPair pair(child);
        const auto type = pair->type();
        if (!first) {
            joined += '|';
        }
        first = false;
        if (type) {
            for (const char c : type->text()) {
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    joined += c;
                }
            }
        }
    }
    return joined;
}

// Whether an expression reads a name as data: a `RefData` of that text, at any depth (`x * c`
// reads `x` and `c`). The left side of a rule is what binds its `const` parameters, and a read
// there is the position that matches the constant. A member name (`x.c`) and a type are not
// `RefData` nodes, so neither is read here, and neither binds anything.
inline bool readsDataName(syn::Node node, const std::string &name) {
    if (!node) {
        return false;
    }
    if (node.is(NK::RefData) && node.text() == name) {
        return true;
    }
    for (const syn::Node &child : node.nodes()) {
        if (readsDataName(child, name)) {
            return true;
        }
    }
    return false;
}

} // namespace

/*
ruleDecl : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
           parentParams whenClause? '{' ruleBody '}' ;
ruleBody : dataExpr ('=>' | '<=>') dataExpr ;

A rule is the shorthand of a pair of functions with the same parameters, the left one the pattern
and the right one what it becomes (rewrite design 2.1, graph store 5.2), and of the value that
holds them (rewrite design 2 (second version) 8.5):

    func name__lhs(params) => left
    func name__rhs(params) => right
    let name = __rule('name', twoWay, needs, consts, name__lhs, name__rhs)

`__rule` is an operator of the module `rewrite`, imported by the declaration itself. The rule is
the value `name` (type `Rule`): it is held in a variable, passed and returned like any value,
stored in a `Rule[]`, exported and imported. `consts` has bit i set when the parameter i is
`const`.

A rule that declares `with` parameters is a function of them instead (design principles 5.5):

    with <tile: int>
    rule tile_matmul(a: f32[m, k], b: f32[k, n]) when m % tile == 0 { ... }

makes the three sides functions of `tile` as well, and

    with <tile: int>
    func tile_matmul(): Rule => __rule_when('tile_matmul', ..., tile_matmul__lhs<tile>, ...)

so that `tile_matmul<64>` -- the same spelling a function's partial application has -- calls it
and gives the `Rule`, and `tile_matmul` alone does too when every parameter has a default.
*/
Value Builder::visitRuleDecl(cst::RuleDecl context) {
    ENTER("RuleDecl");
    const std::string name = std::string(context->get<cst::IdentDef>()->text());
    const Reference ref    = qualifyRef(Reference(name));
    if (context->has(TK::KwExport)) {
        export_->addRef(ref);
    }
    const auto body  = context->get<cst::RuleBody>();
    const auto sides = body->exprs();
    // The head of the rule's left side, read here, where the left side is still syntax: at the
    // stage a module's declarations are read a rule's `__lhs` has no body, so nothing later can
    // read the head from the graphs. Only a ruleset module is indexed -- its name says so -- and
    // the module is named explicitly, by the diagnostics the module is being read with.
    if (diags_ != nullptr && sides[0]) {
        std::string shape;
        uint32_t arity        = 0;
        std::string head      = leftHeadOf(sides[0], shape, arity);
        const auto range      = extractTokenRangeFromContext(context);
        std::string rhsCallee = sides[1] ? rightCalleeOf(sides[1]) : std::string();
        // The names the left side binds, read here for the same reason as the head: the tuple both
        // sides of the rule take is written in this expression and nowhere else.
        std::string variables = variableNamesOf(sides[0]);
        // Which of the pattern's names the right side writes: a family whose transform calls only
        // the name the right side names has nothing to bind them with, so the index refuses such a
        // rule where it is written (the rule is wrong here, not at the call that would use it).
        std::string outerUses =
            sides[1] ? outerNamesUsedInRight(sides[0], sides[1]) : std::string();
        // The rule's condition as written (`is_same::<T, int>()`), or empty. It is part of what a
        // rule *is*: two rules filed under one key with the same condition could not be told apart
        // by the engine, so that pair is a duplicate, while two that differ in their condition are
        // a family it dispatches over.
        const auto conditionClause = context->get<cst::WhenClause>();
        std::string condition      = conditionClause ? conditionClause->text() : std::string();
        // The type of each parameter, in order: the other half of what a duplicate is. A key holds
        // one rule per parameter-type list, so the two
        // laws of `T[...]` and `T` are filed under one key and the engine picks the one the
        // operand's own type enters -- the declaration is not refused for writing the same
        // `is_float::<T>()` twice.
        std::string parameterTypes = parameterTypesOf(context->get<cst::ParentParams>());
        collectRuleHead(
            diags_->moduleName(),
            std::move(head),
            name,
            std::move(rhsCallee),
            std::move(shape),
            std::move(variables),
            std::move(outerUses),
            std::move(condition),
            std::move(parameterTypes),
            arity,
            range.start,
            range.end);
    }
    const bool twoWay = body->has(TK::Equiv);
    const auto params = context->get<cst::ParentParams>();
    // The rule's `with` parameters, written before `export` as a function writes them (design
    // principles 4.4, 5.5). They are the rule's own, so the left side, the right side and the
    // condition are all functions of them and read them by name, and the value the rule is
    // written as is a function of them too.
    const auto withParams = context->get<cst::AngledParams>();
    // The rule's applicability condition is part of its signature, so it is written between the
    // parameters and the body (`rule add_zero::<T>(x: T) when is_integer::<T>() { … }`, design
    // principles 4.4). A trailing `when` is a syntax error and is
    // reported by name (`SyntaxDiag::WhenAfterBody`); its expression is still read here, so the
    // refusal that follows is about the placement and not about a missing condition.
    const auto whenClause = context->get<cst::WhenClause>();
    const auto when       = whenClause ? whenClause->expr() : context->expr();
    // A rule declares its type parameters the way a function does, after its name
    // (`rule sub_self::<T>(x: T) { ... }`): every function the rule becomes is generic over
    // them, so the sides bind and instantiate like an ordinary generic function.
    std::vector<std::string> typeParams;
    if (const auto declared = context->get<cst::TypeParams>()) {
        for (const auto ident : declared->all<cst::IdentDef>()) {
            typeParams.emplace_back(ident->text());
        }
    }
    int64_t consts = 0;
    if (const auto paired = params ? params->get<cst::PairedParams>() : cst::PairedParams()) {
        int bit = 0;
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (pair->has(TK::KwConst)) {
                consts |= int64_t{1} << bit;
            }
            ++bit;
        }
    }

    // A `const` parameter is matched by the left side, and the left side is what binds it: the
    // parameter it is declared with is the constant's value in the condition, and the node of
    // that constant is the hidden node parameter the condition takes when the declaration has
    // one. A left side that reads no such name gives the parameter no position to match a
    // constant in, so the declaration is refused here, where it is written: the rule says
    // nothing about the program, and a condition that reaches the node through `node_of` has no
    // node to read -- which is the state the engine's own guards report as an internal error at
    // every match. `consts` is what the rule value carries (`__rule`), and it is read here the
    // same way, so the check covers exactly the parameters the engine treats as constant.
    if (params) {
        const auto paired = params->get<cst::PairedParams>();
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (!pair->has(TK::KwConst)) {
                continue;
            }
            const auto ident = pair->get<cst::IdentDef>();
            if (!ident) {
                continue;
            }
            const std::string written(ident->text());
            if (readsDataName(sides[0], written)) {
                continue;
            }
            diags_->of(SemanticDiag::RuleConstParamNotMatched)
                .at(rangeOf(ident->firstToken(), ident->lastToken()))
                .commit(written, name);
        }
    }

    // A `with` parameter of a rule is a compile-time value the rule is specialised by: every
    // function the declaration becomes -- the two sides and the condition -- takes it as a
    // `constas` parameter, so the instance an application makes reads it from a static slot and
    // both sides may write it (design principles 5.4, 5.5). The grammar writes the type of a `with`
    // parameter of a rule, and `constasParams` below reads every pair it writes: a pair without one
    // is the parser's to refuse, not this pass's.
    // `#requires(...)`: the numeric permissions the rule needs (design principles 7.2). The
    // arguments are the names of the standard permission tags, not values: a permission tag is
    // a compile-time fact, so the requirement is checked here, when the rule is compiled, and
    // an argument that names no permission is a located diagnostic rather than a silent pass.
    // The mask is handed to `__rule`, which the rewrite engines compare with the policy (a rule
    // whose requirement is not met is not applied).
    camel::core::sem::PermissionMask needs = 0;
    for (const auto use : context->all<cst::TagUse>()) {
        for (const auto tagRef : use->all<cst::TagRef>()) {
            const auto ident = tagRef->get<cst::IdentRef>();
            const std::string written(ident->text());
            if (written != "requires") {
                diags_->of(SemanticDiag::RuleTagNotRequires)
                    .at(rangeOf(ident->firstToken(), ident->lastToken()))
                    .commit(written);
                continue;
            }
            const syn::TokenBuffer &tokens = context.tree()->tokens();
            const size_t begin             = tagRef->firstToken().index();
            const size_t end               = tagRef->lastToken().index();
            size_t argCount                = 0;
            const auto resolve             = [&](size_t first, size_t last) {
                while (first <= last && syn::isTrivia(tokens[first].kind)) {
                    ++first;
                }
                while (last >= first && last != SIZE_MAX && syn::isTrivia(tokens[last].kind)) {
                    --last;
                }
                if (first > last || last == SIZE_MAX) {
                    return;
                }
                ++argCount;
                std::string name;
                for (size_t i = first; i <= last; ++i) {
                    if (!syn::isTrivia(tokens[i].kind)) {
                        name += tokens.text(i);
                    }
                }
                camel::core::sem::PermissionMask permission =
                    camel::core::tag::permissionOfName(name);
                if (permission == 0) {
                    // A group this program declares (`tag fast = [reassociate, contract]`):
                    // its declaration is not in the core's registry, so it is expanded here.
                    permission = programPermissionOf(name);
                }
                if (permission != 0) {
                    needs = static_cast<camel::core::sem::PermissionMask>(needs | permission);
                    return;
                }
                const auto at = rangeOf(
                    syn::TokenRef(context.tree(), first),
                    syn::TokenRef(context.tree(), last));
                // `#requires` names the core's permission tags; a name that is not one (a typo,
                // or a tag of the program that grants no permission) is reported here, at the
                // argument, with the list of the permissions that can be named.
                diags_->of(SemanticDiag::TagNotAPermission).at(at).commit(name);
            };
            bool inArguments = false;
            size_t argFrom   = 0;
            for (size_t i = begin; i <= end; ++i) {
                if (syn::isTrivia(tokens[i].kind)) {
                    continue;
                }
                const std::string_view text = tokens.text(i);
                if (!inArguments) {
                    if (text == "(") {
                        inArguments = true;
                        argFrom     = i + 1;
                    }
                    continue;
                }
                if (text == ")") {
                    resolve(argFrom, i == 0 ? 0 : i - 1);
                    inArguments = false;
                } else if (text == ",") {
                    resolve(argFrom, i == 0 ? 0 : i - 1);
                    argFrom = i + 1;
                }
            }
            if (!inArguments && argCount == 0) {
                // `#requires` without any argument requires nothing, which is never
                // what was meant, so say how many arguments the form takes.
                diags_->of(SemanticDiag::TagArgumentCount)
                    .at(rangeOf(tagRef->firstToken(), tagRef->lastToken()))
                    .commit("requires", "1", "0");
            }
        }
    }

    if (!ruleImportAdded_ && typeParams.empty()) {
        ruleImportAdded_ = true;
        auto import_     = std::make_shared<ImportLoad>();
        import_->setTokenRange(extractTokenRangeFromContext(context));
        import_->setPath("rewrite");
        import_->setRefs({Reference("__rule")});
        imports_.push_back(import_);
    }

    // The rule's `with` parameters as the `constas` parameters of every function the declaration
    // becomes: same names, same declared types, in the order they are written, ahead of the rule's
    // own parameters. A `constas` parameter is a compile-time value, so it is no port of a side:
    // the ports stay the rule's own parameters, and the value an application binds is read from the
    // instance's static slot (design principles 5.4, 5.5).
    const auto constasParams = [&](node_ptr_t ownParams) {
        node_ptr_t list = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(list, context);
        if (withParams) {
            for (const auto pair : withParams->get<cst::PairedParams>()->all<cst::KeyParamPair>()) {
                const auto ident = pair->get<cst::IdentDef>();
                if (!ident || !pair->type()) {
                    continue;
                }
                node_ptr_t named = createNodeAs<NamedPairLoad>(
                    Reference(std::string(ident->text())),
                    false,
                    false,
                    false,
                    true);
                setNodeTokenRangeByContext(named, pair);
                *named << any2node(visitType(pair->type()));
                node_ptr_t noDefault = createNodeAs<OptionalLoad>("DataExpr");
                setNodeTokenRangeByContext(noDefault, pair);
                *named << noDefault;
                *list << named;
            }
        }
        // The rule's own parameters follow the `with` ones. They come from `ownParams`, a list
        // the caller builds to hand them over and keeps nothing else of, so each one is taken
        // out of it before it is attached: a node belongs to the container that lists it and to
        // no other (tree.h, `operator<<`), and the pair `visitPairedParams` attached to
        // `ownParams` still has that list as its parent here.
        while (!ownParams->empty()) {
            *list << ownParams->takeAt(0);
        }
        return list;
    };

    // The type parameters the rule writes *anywhere* -- its left side, its right side, its
    // condition. A rule's head is written once, but the sides are three declarations of the
    // compiler's making, so a name the user writes on the right is absent from the left: the
    // unused-parameter check reads one declaration's own signature, and `rule
    // mul_tensor_t::<T, X, W>(x: X, w: W) { mul_tensor(x, w) => (ct: T[...]) => … }` names `T`
    // only in the right side's cotangent. What the rule uses is the union, read here from the
    // rule's own tokens, and carried to every side.
    std::vector<std::string> writtenAcrossTheRule;
    {
        const syn::TokenBuffer &tokens = context.tree()->tokens();
        const auto writesName          = [&](syn::Node node, const std::string &name) {
            if (!node) {
                return false;
            }
            // Token *indices*, not byte offsets: `begin()`/`end()` are offsets into the source.
            const uint32_t first = node->firstToken().index();
            const uint32_t last  = node->lastToken().index();
            for (uint32_t i = first; i <= last && i < tokens.size(); ++i) {
                if (!syn::isTrivia(tokens[i].kind) && tokens.text(i) == name) {
                    return true;
                }
            }
            return false;
        };
        for (const std::string &name : typeParams) {
            if (writesName(sides[0], name) || writesName(sides[1], name) ||
                writesName(when, name)) {
                writtenAcrossTheRule.push_back(name);
            }
        }
    }
    // One side as a function of the rule's parameters: `func name__<side>(params) => expr`. A side
    // is read as a pattern the engine matches and rebuilds with, so its ports are the rule's own
    // parameters; a `with` parameter is a `constas` parameter of it, which the instance reads from
    // a static slot, and either side may write it (design principles 5.5).
    const auto side = [&](const std::string &suffix, syn::Node expr) {
        const Reference sideRef = qualifyRef(Reference(name + suffix));
        node_ptr_t funcTypeNode = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(funcTypeNode, context);
        unwrapNodeAs<FuncTypeLoad>(funcTypeNode)->setTypeParamNames(typeParams);
        unwrapNodeAs<FuncTypeLoad>(funcTypeNode)
            ->setTypeParamsWrittenElsewhere(writtenAcrossTheRule);
        *funcTypeNode << createNodeAs<RepeatedLoad>("NamedPair");
        *funcTypeNode << constasParams(
            params ? any2node(visitParentParams(params)) : createNodeAs<RepeatedLoad>("NamedPair"));
        node_ptr_t typeOptNode = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(typeOptNode, context);
        *funcTypeNode << typeOptNode;

        node_ptr_t funcNode = createNodeAs<FuncDataLoad>(sideRef);
        setNodeTokenRangeByContext(funcNode, context);
        *funcNode << funcTypeNode;
        *funcNode << any2node(visitBlockExpr(expr));

        node_ptr_t funcDeclNode = createNodeAs<FuncDeclLoad>(sideRef);
        *funcDeclNode << funcNode;
        setNodeTokenRangeByContext(funcDeclNode, context);
        return funcDeclNode;
    };
    node_ptr_t lhsFunc = side("__lhs", sides[0]);
    node_ptr_t rhsFunc = side("__rhs", sides[1]);

    // A condition is a fourth function of the same parameters: `func name__when(params) => expr`.
    // A parameter that is not `const` is a node of the draft being rewritten (`graph::Node`); a
    // `const` one is the constant's value, of the type it was declared with (rewrite design 2
    // (second version) 1), so the guard is written on the value
    // and needs neither `constant(node, sample)` nor the `match … case null` layers around it.
    node_ptr_t whenFunc;
    if (when) {
        // The condition of this rule is being built: `node_of(c)` is read here and nowhere else
        // and the name of the rule is what
        // a refusal names.
        ruleWhenName_     = name;
        const auto paired = params ? params->get<cst::PairedParams>() : cst::PairedParams();
        const auto constValueParam = [](cst::KeyParamPair pair) {
            return static_cast<bool>(pair->has(TK::KwConst)) && static_cast<bool>(pair->type());
        };
        bool nodesUsed = false;
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                nodesUsed = nodesUsed || !constValueParam(pair);
            }
        }
        if (!whenImportAdded_ && typeParams.empty()) {
            whenImportAdded_   = true;
            auto rewriteImport = std::make_shared<ImportLoad>();
            rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
            rewriteImport->setPath("rewrite");
            rewriteImport->setRefs({Reference("__rule_when")});
            imports_.push_back(rewriteImport);
        }
        if (nodesUsed && !whenNodeImportAdded_) {
            whenNodeImportAdded_ = true;
            auto graphImport     = std::make_shared<ImportLoad>();
            graphImport->setTokenRange(extractTokenRangeFromContext(context));
            graphImport->setPath("graph");
            graphImport->setRefs({Reference("Node")});
            imports_.push_back(graphImport);
        }
        const Reference whenRef = qualifyRef(Reference(name + "__when"));
        // The hidden node parameter of every `const` parameter (the rule on
        // `rule_when_const_node`: "条件函数按 `const` 形参追加隐藏的节点形参，构建期把
        // `node_of(c)` 换成它；不引入新类型"). The condition takes the rule's parameters first,
        // in the order they are written, and then one node per `const` parameter, in the same
        // order. `node_of(c)` becomes the name of that parameter, and the map is what refuses a
        // name that is not one of this rule's own `const` parameters.
        std::vector<std::string> whenNodeNames;
        // Each hidden parameter is the node of a `const` parameter, so its value type is the
        // declared type of that parameter (`node_of(c)` with `const c: int` is a `Node<int>`).
        std::vector<std::pair<std::string, syn::Node>> whenNodeTypes;
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (constValueParam(pair)) {
                    whenNodeNames.emplace_back(
                        std::string(pair->get<cst::IdentDef>()->text()) + "__node");
                    whenNodeTypes.emplace_back(whenNodeNames.back(), pair->type());
                }
            }
        }
        for (const std::string &nodeName : whenNodeNames) {
            ruleWhenNodeParams_.emplace(nodeName.substr(0, nodeName.size() - 6), nodeName);
        }
        // A rule's parameter as the condition reads it: the declaration's type is the type of the
        // value the node stands for, so the parameter is a node whose value type is that type
        // (`x: T` is `Node<T>`, `x: int` is `Node<int>`), and a parameter written without a type is
        // a node whose value type the declaration does not state (design principles 5.9). The node
        // of a value the condition is handed carries the same type, so a query in the condition
        // reads the fact of a value of the type the rule declared.
        const auto nodeParamType = [&](cst::KeyParamPair at) -> node_ptr_t {
            if (!at->type()) {
                node_ptr_t bare = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(bare, at);
                return bare;
            }
            node_ptr_t applied = createNodeAs<SpecTypeLoad>();
            setNodeTokenRangeByContext(applied, at);
            node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
            setNodeTokenRangeByContext(arguments, at);
            *arguments << any2node(visitType(at->type()));
            node_ptr_t head = createNodeAs<RefTypeLoad>(Reference("Node"));
            setNodeTokenRangeByContext(head, at);
            *applied << head << arguments;
            return applied;
        };
        node_ptr_t whenParams = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(whenParams, context);
        if (params) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                node_ptr_t named = createNodeAs<NamedPairLoad>(
                    Reference(std::string(pair->get<cst::IdentDef>()->text())),
                    false,
                    false);
                setNodeTokenRangeByContext(named, pair);
                node_ptr_t paramType;
                if (constValueParam(pair)) {
                    paramType = any2node(visitType(pair->type()));
                } else {
                    paramType = nodeParamType(pair);
                }
                *named << paramType << createNodeAs<OptionalLoad>("DataExpr");
                *whenParams << named;
            }
        }
        for (const auto &[nodeName, nodeValueType] : whenNodeTypes) {
            node_ptr_t named = createNodeAs<NamedPairLoad>(Reference(nodeName), false, false);
            setNodeTokenRangeByContext(named, context);
            node_ptr_t paramType;
            if (nodeValueType) {
                paramType = createNodeAs<SpecTypeLoad>();
                setNodeTokenRangeByContext(paramType, context);
                node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
                setNodeTokenRangeByContext(arguments, context);
                *arguments << any2node(visitType(nodeValueType));
                node_ptr_t head = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(head, context);
                *paramType << head << arguments;
            } else {
                paramType = createNodeAs<RefTypeLoad>(Reference("Node"));
                setNodeTokenRangeByContext(paramType, context);
            }
            *named << paramType << createNodeAs<OptionalLoad>("DataExpr");
            *whenParams << named;
        }
        node_ptr_t whenType = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(whenType, when);
        unwrapNodeAs<FuncTypeLoad>(whenType)->setTypeParamNames(typeParams);
        // The condition is a function of the rule's `with` parameters as the sides are, so it
        // reads them by name (`when m % tile == 0`) and they are `constas` parameters of it too.
        *whenType << createNodeAs<RepeatedLoad>("NamedPair");
        *whenType << constasParams(whenParams);
        node_ptr_t whenResult = createNodeAs<OptionalLoad>("Type");
        node_ptr_t boolType   = createNodeAs<RefTypeLoad>(Reference("bool"));
        setNodeTokenRangeByContext(boolType, when);
        *whenResult << boolType;
        *whenType << whenResult;
        node_ptr_t whenData = createNodeAs<FuncDataLoad>(whenRef);
        setNodeTokenRangeByContext(whenData, when);
        *whenData << whenType << any2node(visitBlockExpr(when));
        // `node_of` reaches a node only here, in the condition of the rule it belongs to: the two
        // sides and every other condition of the program meet an empty map and are refused.
        ruleWhenNodeParams_.clear();
        ruleWhenName_.clear();
        whenFunc = createNodeAs<FuncDeclLoad>(whenRef);
        setNodeTokenRangeByContext(whenFunc, when);
        *whenFunc << whenData;
    }

    // let name = __rule('name', twoWay, needs, consts, name__lhs, name__rhs)
    const auto literal = [&](LiteralType type, const std::string &text) {
        node_ptr_t literalNode = createNodeAs<LiteralLoad>(Literal(type, text));
        setNodeTokenRangeByContext(literalNode, context);
        return literalNode;
    };
    const auto reference = [&](const std::string &text) {
        node_ptr_t refNode = createNodeAs<RefDataLoad>(Reference(text));
        setNodeTokenRangeByContext(refNode, context);
        return refNode;
    };
    // One side as the maker takes it: the name of the side's function; or, for the condition, which
    // is a function of the rule's `with` parameters, that name with the parameters bound
    // (`name__when<tile>`), the partial application of the condition. The condition is called by
    // the engine with the values its function value carries, so the value reaches it there, while
    // the two sides are patterns and take the rule's parameters alone.
    node_ptr_t args = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(args, context);
    *args << literal(LiteralType::String, name);
    *args << literal(LiteralType::Boolean, twoWay ? "true" : "false");
    *args << literal(LiteralType::Integer, std::to_string(needs));
    *args << literal(LiteralType::Integer, std::to_string(consts));
    *args << reference(name + "__lhs");
    *args << reference(name + "__rhs");
    std::string makerName = "__rule";
    if (whenFunc) {
        *args << reference(name + "__when");
        makerName = "__rule_when";
    }
    // A rule that is instantiated -- one whose head declares type parameters, or whose `with`
    // parameters the instance is specialised by -- has no function values to give: its functions
    // are templates (a generic function is instantiated where it is called, and a function of a
    // `constas` parameter has no value either). The value carries the id the maker registers the
    // templates under, and the values bound to the `with` parameters follow the id, so the rule
    // value is a `Rule` of the values it was bound to (design principles 5.4, 5.5).
    const bool instantiated = !typeParams.empty() || withParams;
    if (instantiated) {
        makerName = "__rule_generic";
        if (!genericRuleImportAdded_) {
            genericRuleImportAdded_ = true;
            auto rewriteImport      = std::make_shared<ImportLoad>();
            rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
            rewriteImport->setPath("rewrite");
            rewriteImport->setRefs({Reference("__rule_generic")});
            imports_.push_back(rewriteImport);
        }
        // The value of each `with` parameter, in the order it is declared: the maker puts them in
        // the rule value, and the instance the engine makes reads them from there.
        if (withParams) {
            for (const auto pair : withParams->get<cst::PairedParams>()->all<cst::KeyParamPair>()) {
                *args << reference(std::string(pair->get<cst::IdentDef>()->text()));
            }
        }
    }
    node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(call, context);
    *call << reference(makerName) << args << createNodeAs<RepeatedLoad>("NamedData");

    if (withParams) {
        // A rule with `with` parameters is a function of them (design principles 5.5):
        // `with <tile: int> func name(): Rule => __rule_generic('name', ..., name__lhs, name__rhs,
        // tile)`. `name<64>`, the spelling a function's partial application has, calls it and gives
        // the `Rule`; used as it is (`name`), the call is made with the defaults, so a rule whose
        // parameters all have one is a `Rule` value by its name alone.
        if (!ruleTypeImportAdded_) {
            ruleTypeImportAdded_ = true;
            auto ruleImport      = std::make_shared<ImportLoad>();
            ruleImport->setTokenRange(extractTokenRangeFromContext(context));
            ruleImport->setPath("rewrite");
            ruleImport->setRefs({Reference("Rule")});
            imports_.push_back(ruleImport);
        }
        node_ptr_t bindType = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(bindType, context);
        unwrapNodeAs<FuncTypeLoad>(bindType)->setTypeParamNames({});
        *bindType << any2node(visitAngledParams(withParams));
        *bindType << createNodeAs<RepeatedLoad>("NamedPair");
        node_ptr_t bindResult = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(bindResult, context);
        node_ptr_t ruleType = createNodeAs<RefTypeLoad>(Reference("Rule"));
        setNodeTokenRangeByContext(ruleType, context);
        *bindResult << ruleType;
        *bindType << bindResult;

        node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(stmts, context);
        node_ptr_t valueStmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(valueStmt, context);
        *valueStmt << call;
        *stmts << valueStmt;
        node_ptr_t bindBody = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(bindBody, context);
        *bindBody << stmts;

        node_ptr_t bindData = createNodeAs<FuncDataLoad>(ref);
        setNodeTokenRangeByContext(bindData, context);
        *bindData << bindType << bindBody;
        node_ptr_t bindDecl = createNodeAs<FuncDeclLoad>(ref);
        setNodeTokenRangeByContext(bindDecl, context);
        // The function the declaration is written as, not one the program wrote: the graph
        // builder marks its graph, and a use of the name as a value makes the call that binds
        // the parameters instead of a function value (`ruleBindingValueOf`).
        unwrapNodeAs<FuncDeclLoad>(bindDecl)->setRuleBinding();
        *bindDecl << bindData;

        LEAVE("RuleDecl");
        std::vector<node_ptr_t> bound{lhsFunc, rhsFunc};
        if (whenFunc) {
            bound.push_back(whenFunc);
        }
        bound.push_back(bindDecl);
        return bound;
    }

    // A rule without `with` parameters is a module-level value, not a function:
    // `let name = __rule(...)` (rewrite design 2 (second version) 8.5). A value can be held,
    // passed, returned and put in a `Rule[]`; a function that made one could only be called.
    node_ptr_t typeList = createNodeAs<RepeatedLoad>("Type");
    setNodeTokenRangeByContext(typeList, context);
    node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(dataList, context);
    *dataList << call;
    node_ptr_t valueDecl = createNodeAs<DataDeclLoad>(false, std::vector<Reference>{ref});
    unwrapNodeAs<DataDeclLoad>(valueDecl)->setDoc(docOf(context));
    setNodeTokenRangeByContext(valueDecl, context);
    *valueDecl << typeList << dataList;
    // The AST tooling reads the binding and the initializer of a declaration; a rule declaration
    // has the same two, under the name of the rule.
    std::vector<camel::source::SemanticPart> parts;
    const auto nameToken = context->get<cst::IdentDef>();
    parts.push_back(semanticPart(
        camel::source::SemanticRole::BindingName,
        nameToken ? deriveAstAnchorOrigin(valueDecl, nameToken, "ast.data.binding")
                  : nodeOrigin(valueDecl),
        0,
        name));
    parts.push_back(semanticPart(
        camel::source::SemanticRole::ValueProducer,
        nodeOrigin(valueDecl->atAs<RepeatedLoad>(1)),
        -1,
        "initializer"));
    registerAstSemanticBundle(valueDecl, std::move(parts));

    LEAVE("RuleDecl");
    std::vector<node_ptr_t> declarations{lhsFunc, rhsFunc};
    if (whenFunc) {
        declarations.push_back(whenFunc);
    }
    declarations.push_back(valueDecl);
    return declarations;
}

/*
factDecl : ('with' angledParams)? 'export'? 'fact' identDef typeParams? parentParams? ':' typeExpr
           'match' logicalOrExpr '{' factCase* '}' factClause*
factCase : 'case' factPattern '=>' dataExpr
factPattern : 'const' identDef (':' typeExpr)? | dataExpr
factClause : IDENTIFIER dataExpr

A fact is compiled to plain functions and one call of `__fact`:

    func depth__initial() => 0
    func depth__join(a0: int, a1: int) => max(a0, a1)
    func depth__case0(c: int) => c                      (a `const` case: the matcher binds the
    func depth__case0_fact(c: int) => 1                  value, not a node)
    func depth__case1(x: int, y: int) => x + y          (a case over the construction: the
    func depth__case1_fact(x: Node, y: Node) => max(depth::<int>(x), depth::<int>(y)) + 1
    func depth(v: Node<int>): int => __fact('depth', depth__initial, depth__join, ...)     matcher
reads it, nothing runs it)

The cases are the engine's clauses: a pattern function carries the shape the matcher reads (its
parameters are the pattern variables, typed `int`; a `const` case declares the type of its
constant), and a fact function computes the fact of a value that matched. `case _`, which must be
the last case, becomes `initial`: what the fact says of a value no other case matched, and what a
value that depends on itself settles on.

Each trailing clause names one binary function, and the fact merges two facts by calling it:
`join` (required) is how the facts of two writings of one value are merged, `meet` how equivalent
writings meet, `widen` accelerates a loop and `narrow` tightens it afterwards. `meet`, `widen` and
`narrow` default to `join`; the first version computes no loop acceleration. A clause is read in a
function of its own, of the fact's type, so that a name of several overloads (or a lambda) is
resolved where the fact's type is known.

The declaration's `with` parameters are the arguments the query is written with
(`depth<8>(x)`, and one with a default may be left out). They are copied onto the query function
and marked `const` there: the compiler must compute the argument, since the argument is part of
which fact this is, and the engine computes and caches one fact per argument. Every function the
declaration becomes carries them as well, so a case or a trailing clause names one and reads the
value: on the query they are `const`, and on the functions the engine calls they are plain
`with` parameters, which the engine fills with the arguments the query was written with.

The head parameters are the parts of a value the query is written on
(`aligned(x, y)` for `fact aligned(a: int, b: int)`), and are `Node` in the generated function; a
parameter whose type the head writes is a node of that value type (`Node<T>`, `Node<int>`), so a
caller's node gives the query its type arguments and a `const` case's constant is the head's type.
A declaration with none is the one-value form (`depth(n)`).

A fact may declare type parameters (`fact range::<T>(v: T): Interval`, rewrite design 7.5): every
function the declaration becomes is generic over them, so an instance of the query is an instance
of all of them at once. The query names them, in the order the declaration writes them, as the
argument after the fact's name; the type arguments of the instance replace that text, and they are
part of which fact this is: two queries at two types are two instances, computed and kept apart.
*/
Value Builder::visitFactDecl(cst::FactDecl context) {
    ENTER("FactDecl");
    if (!factImportAdded_) {
        factImportAdded_   = true;
        auto rewriteImport = std::make_shared<ImportLoad>();
        rewriteImport->setTokenRange(extractTokenRangeFromContext(context));
        rewriteImport->setPath("rewrite");
        rewriteImport->setRefs({Reference("__fact")});
        imports_.push_back(rewriteImport);
        auto graphImport = std::make_shared<ImportLoad>();
        graphImport->setTokenRange(extractTokenRangeFromContext(context));
        graphImport->setPath("graph");
        graphImport->setRefs({Reference("Node")});
        imports_.push_back(graphImport);
    }

    const std::string name = std::string(context->get<cst::IdentDef>()->text());
    const Reference ref    = qualifyRef(Reference(name));
    if (context->has(TK::KwExport)) {
        export_->addRef(ref);
    }
    // The type parameters, as the declaration writes them (`fact range::<T>(v: T)`): every
    // function the declaration becomes is generic over them, and the query names them to the
    // engine so that the instance it is asked for says which types its functions were made at.
    std::vector<std::string> typeParams;
    if (const auto declared = context->get<cst::TypeParams>()) {
        for (const auto ident : declared->all<cst::IdentDef>()) {
            typeParams.emplace_back(ident->text());
        }
    }
    std::string typeParamNames;
    for (const std::string &one : typeParams) {
        typeParamNames += (typeParamNames.empty() ? "" : ",") + one;
    }
    const auto typeExprs = context->types();
    if (typeExprs.size() != 1) {
        // A fact declaration writes the type of the fact after its parameters, and that is the
        // only type it writes here (`fact depth(n): int`): the cases and the `join` all give a
        // value of it (rewrite design 7.5). The declaration that writes none is refused by the
        // parser, where the `:` and its type belong, and a file the parser refused is never given
        // an AST, so no program reaches this; the guard keeps the read below off an empty list.
        ASSERT(false, "a fact declaration with no single type reached the builder");
        LEAVE("FactDecl");
        return Value();
    }
    const syn::Node factType = typeExprs[0];

    std::vector<syn::Node> cases;
    if (const auto body = context->get<cst::FactBody>()) {
        for (const auto each : body->all<cst::FactCase>()) {
            cases.push_back(each);
        }
    }
    // One case takes whatever the others do not, and it is written last: the value of `case _` is
    // also what a value that depends on itself settles on, so every fact needs it (rewrite design
    // 7.5). A body that writes no case at all is the same defect -- the parser asks for a case
    // where the body is empty, before this builder is reached -- and `cases.back()` is only read
    // once there is one.
    const syn::Node lastPattern =
        cases.empty() ? syn::Node() : cases.back()->get<cst::FactPattern>();
    if (!lastPattern || !lastPattern->has(TK::Underscore)) {
        diags_->of(SemanticDiag::FactCaseDefaultMissing)
            .at(rangeOf(context->firstToken()))
            .commit(name);
        LEAVE("FactDecl");
        return Value();
    }
    const syn::Node initialRhs = cases.back()->expr(0);

    // The trailing clauses, as written. Each names one binary function; only `join` is required.
    syn::Node joinClause, meetClause, widenClause, narrowClause;
    for (const auto clause : context->all<cst::FactClause>()) {
        const std::string keyword = clause->tok(TK::Identifier).text();
        const auto exprs          = clause->exprs();
        if (exprs.size() != 1) {
            continue;
        }
        if (keyword == "join") {
            joinClause = exprs[0];
        } else if (keyword == "meet") {
            meetClause = exprs[0];
        } else if (keyword == "widen") {
            widenClause = exprs[0];
        } else if (keyword == "narrow") {
            narrowClause = exprs[0];
        }
    }
    if (!joinClause) {
        // `join` says how the facts of two writings of one value are merged, and it is the one
        // clause that has no default: a fact without it has no value for a value the program
        // wrote two ways (rewrite design 7.5).
        diags_->of(SemanticDiag::FactJoinRequired).at(rangeOf(context->firstToken())).commit(name);
        LEAVE("FactDecl");
        return Value();
    }

    // The `with` parameters, as written. Every function that carries them builds its own copy:
    // an expression tree is not shared between two declarations.
    std::vector<std::string> withNames;
    if (const auto params = context->get<cst::AngledParams>()) {
        if (const auto paired = params->get<cst::PairedParams>()) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (pair->get<cst::IdentDef>()) {
                    withNames.push_back(std::string(pair->get<cst::IdentDef>()->text()));
                }
            }
        }
    }
    // The `with` parameters of the declaration, once per function that carries them: an expression
    // tree is not shared between two declarations. On the query function they are `const`, the
    // mark that says "the compiler must compute this argument"; on the functions the engine calls
    // they are plain `with` parameters, because the engine passes the query's arguments to them as
    // the `with` arguments of the call, so a case names one and reads the value.
    const auto withPairs = [&](bool asConstas) {
        std::vector<node_ptr_t> built;
        const auto params = context->get<cst::AngledParams>();
        if (!params) {
            return built;
        }
        const auto paired = params->get<cst::PairedParams>();
        if (!paired) {
            return built;
        }
        for (const auto pair : paired->all<cst::KeyParamPair>()) {
            if (!pair->get<cst::IdentDef>()) {
                continue;
            }
            const std::string paramName(pair->get<cst::IdentDef>()->text());
            node_ptr_t named = createNodeAs<NamedPairLoad>(
                Reference(paramName),
                false,
                /*isConst=*/asConstas,
                false,
                false);
            setNodeTokenRangeByContext(named, context);
            if (pair->type()) {
                *named << any2node(visitType(pair->type()));
            } else {
                node_ptr_t inferNode = createNodeAs<InferTypeLoad>(Reference());
                setNodeTokenRangeByContext(inferNode, pair->get<cst::IdentDef>());
                *named << inferNode;
            }
            node_ptr_t defaultOpt = createNodeAs<OptionalLoad>("DataExpr");
            if (pair->expr()) {
                setNodeTokenRangeByContext(defaultOpt, pair->expr());
                *defaultOpt << any2node(visitExpr(pair->expr()));
            }
            *named << defaultOpt;
            built.push_back(named);
        }
        return built;
    };

    const auto literal = [&](LiteralType type, const std::string &text) {
        node_ptr_t node = createNodeAs<LiteralLoad>(Literal(type, text));
        setNodeTokenRangeByContext(node, context);
        return node;
    };
    const auto reference = [&](const std::string &text) {
        node_ptr_t node = createNodeAs<RefDataLoad>(Reference(text));
        setNodeTokenRangeByContext(node, context);
        return node;
    };
    const auto callOf = [&](node_ptr_t callee, std::vector<node_ptr_t> theArgs) {
        node_ptr_t dataList = createNodeAs<RepeatedLoad>("Data");
        setNodeTokenRangeByContext(dataList, context);
        for (const auto &each : theArgs) {
            *dataList << each;
        }
        node_ptr_t namedList = createNodeAs<RepeatedLoad>("NamedData");
        setNodeTokenRangeByContext(namedList, context);
        node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
        setNodeTokenRangeByContext(call, context);
        *call << callee << dataList << namedList;
        return call;
    };
    const auto blockOf = [&](node_ptr_t expr) {
        node_ptr_t stmt = createNodeAs<ExprStmtLoad>();
        setNodeTokenRangeByContext(stmt, context);
        *stmt << expr;
        node_ptr_t stmts = createNodeAs<RepeatedLoad>("Stmt");
        setNodeTokenRangeByContext(stmts, context);
        *stmts << stmt;
        node_ptr_t block = createNodeAs<StmtBlockLoad>();
        setNodeTokenRangeByContext(block, context);
        *block << stmts;
        return block;
    };
    const auto makeFunc = [&](const std::string &fname,
                              const std::vector<node_ptr_t> &withParams,
                              const std::vector<std::pair<std::string, node_ptr_t>> &params,
                              node_ptr_t retType,
                              node_ptr_t body) -> node_ptr_t {
        const Reference fnRef = qualifyRef(Reference(fname));
        node_ptr_t funcType   = createNodeAs<FuncTypeLoad>();
        setNodeTokenRangeByContext(funcType, context);
        unwrapNodeAs<FuncTypeLoad>(funcType)->setTypeParamNames(typeParams);
        node_ptr_t withList = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(withList, context);
        for (const auto &each : withParams) {
            *withList << each;
        }
        *funcType << withList;
        node_ptr_t normParams = createNodeAs<RepeatedLoad>("NamedPair");
        setNodeTokenRangeByContext(normParams, context);
        for (const auto &[pname, ptype] : params) {
            node_ptr_t named = createNodeAs<NamedPairLoad>(Reference(pname), false, false);
            setNodeTokenRangeByContext(named, context);
            *named << ptype << createNodeAs<OptionalLoad>("DataExpr");
            *normParams << named;
        }
        *funcType << normParams;
        node_ptr_t returnType = createNodeAs<OptionalLoad>("Type");
        setNodeTokenRangeByContext(returnType, context);
        *returnType << retType;
        *funcType << returnType;
        node_ptr_t funcData = createNodeAs<FuncDataLoad>(fnRef);
        setNodeTokenRangeByContext(funcData, context);
        *funcData << funcType << body;
        node_ptr_t decl = createNodeAs<FuncDeclLoad>(fnRef);
        setNodeTokenRangeByContext(decl, context);
        *decl << funcData;
        return decl;
    };
    const auto typeOf  = [&](syn::Node node) { return any2node(visitType(node)); };
    const auto refType = [&](const std::string &typeName) {
        node_ptr_t node = createNodeAs<RefTypeLoad>(Reference(typeName));
        setNodeTokenRangeByContext(node, context);
        return node;
    };

    std::vector<node_ptr_t> declarations;
    declarations.push_back(makeFunc(
        name + "__initial",
        withPairs(false),
        {},
        typeOf(factType),
        any2node(visitBlockExpr(initialRhs))));
    // One function per clause: it calls the function the clause names, of the fact's type, so that
    // a name with several overloads is resolved where the type is known.
    const auto mergeFunc = [&](const std::string &suffix, syn::Node clause) -> node_ptr_t {
        std::vector<std::pair<std::string, node_ptr_t>> params = {
            {"a0", typeOf(factType)},
            {"a1", typeOf(factType)}};
        node_ptr_t callee = any2node(visitExpr(clause));
        node_ptr_t body   = blockOf(callOf(callee, {reference("a0"), reference("a1")}));
        return makeFunc(name + suffix, withPairs(false), params, typeOf(factType), body);
    };
    declarations.push_back(mergeFunc("__join", joinClause));
    if (meetClause) {
        declarations.push_back(mergeFunc("__meet", meetClause));
    }
    if (widenClause) {
        declarations.push_back(mergeFunc("__widen", widenClause));
    }
    if (narrowClause) {
        declarations.push_back(mergeFunc("__narrow", narrowClause));
    }

    // The head parameters: what the query is written on. A parameter's declared type is the type
    // of the value the node stands for, so the query's parameter is `Node<T>` -- a node whose
    // value type is `T` -- and `T` is inferred from a caller's node the way any generic call infers
    // its type arguments (design principles 5.9). Parameters written without a type are nodes
    // whose value type the declaration does not state. The first one gives a `const` case the type
    // of its constant when the case writes none.
    const auto nodeOfType = [&](syn::Node valueType, cst::KeyParamPair at) -> node_ptr_t {
        node_ptr_t applied = createNodeAs<SpecTypeLoad>();
        setNodeTokenRangeByContext(applied, at);
        node_ptr_t arguments = createNodeAs<RepeatedLoad>("TypeOrData");
        setNodeTokenRangeByContext(arguments, at);
        *arguments << any2node(visitType(valueType));
        *applied << refType("Node") << arguments;
        return applied;
    };
    std::vector<std::pair<std::string, node_ptr_t>> queryParams;
    std::vector<std::string> queryArgs;
    syn::Node firstHeadType;
    if (const auto params = context->get<cst::ParentParams>()) {
        if (const auto paired = params->get<cst::PairedParams>()) {
            for (const auto pair : paired->all<cst::KeyParamPair>()) {
                if (!pair->get<cst::IdentDef>()) {
                    continue;
                }
                queryArgs.push_back(std::string(pair->get<cst::IdentDef>()->text()));
                queryParams.emplace_back(
                    queryArgs.back(),
                    pair->type() ? nodeOfType(pair->type(), pair) : refType("Node"));
                if (!firstHeadType && pair->type()) {
                    firstHeadType = pair->type();
                }
            }
        }
    }
    if (queryArgs.empty()) {
        // The one-value form: the query is written on the value itself, as a node whose value type
        // the declaration does not state.
        queryArgs.push_back("n");
        queryParams.emplace_back("n", refType("Node"));
    }

    // The names bound by a case's pattern. A bare reference is a variable; the callee of a call is
    // not (`abs(x)` binds x, not abs), and a qualified name belongs to a module.
    const std::function<void(syn::Node, std::vector<std::string> &)> collectVars =
        [&](syn::Node node, std::vector<std::string> &out) {
            if (!node || node.empty()) {
                return;
            }
            if (node.kind() == NK::RefData) {
                const std::string text = std::string(node.text());
                if (text.find("::") == std::string::npos &&
                    std::find(out.begin(), out.end(), text) == out.end()) {
                    out.push_back(text);
                }
                return;
            }
            if (node.kind() == NK::AnnoExpr && node->get<cst::ParentArgues>()) {
                for (size_t i = 1; i < node->childCount(); ++i) {
                    collectVars(node->child(i).node(), out);
                }
                return;
            }
            for (size_t i = 0; i < node->childCount(); ++i) {
                collectVars(node->child(i).node(), out);
            }
        };

    struct CompiledCase {
        std::string patternFn;
        std::string factFn;
        uint64_t constMask = 0;
    };
    std::vector<CompiledCase> compiled;
    for (size_t index = 0; index + 1 < cases.size(); ++index) {
        const syn::Node one         = cases[index];
        const auto pattern          = one->get<cst::FactPattern>();
        const syn::Node patternExpr = pattern ? pattern->expr(0) : syn::Node();
        const syn::Node constDef    = pattern ? pattern->get<cst::IdentDef>() : syn::Node();
        const std::string constName = constDef ? constDef->text() : std::string();
        const syn::Node rhs         = one->expr(0);
        // `case const c => ...`: the case is about one constant, and the fact function is given
        // the value itself. The type is the one written, or the head's first parameter's.
        syn::Node constType;
        if (!constName.empty() && pattern) {
            const auto written = pattern->types();
            constType          = written.size() == 1 ? written[0] : syn::Node();
        }
        if (!constName.empty()) {
            if (!constType) {
                constType = firstHeadType;
            }
            if (!constType) {
                // A `const` case matches one constant and the fact function is given the value
                // itself, so the constant needs a type: written in the case, or the head's first
                // parameter's, which is what the declaration says the value it is about is
                // (rewrite design 7.5).
                diags_->of(SemanticDiag::FactConstCaseTypeRequired)
                    .at(rangeOf(context->firstToken()))
                    .commit(constName, name);
                LEAVE("FactDecl");
                return Value();
            }
        }
        std::vector<std::string> vars;
        if (!constName.empty()) {
            vars.push_back(constName);
        } else {
            collectVars(patternExpr, vars);
        }
        if (vars.empty()) {
            // The cases read how a value was built, and the parts they read are the variables
            // their patterns name (`case x + y` binds the two operands), so a case whose pattern
            // names none leaves the fact nothing to compute from (rewrite design 7.5).
            diags_->of(SemanticDiag::FactCaseBindsNoVariable)
                .at(rangeOf(context->firstToken()))
                .commit(name);
            LEAVE("FactDecl");
            return Value();
        }

        std::vector<std::pair<std::string, node_ptr_t>> patternParams;
        std::vector<std::pair<std::string, node_ptr_t>> factParams;
        uint64_t mask = 0;
        for (size_t i = 0; i < vars.size(); ++i) {
            if (i == 0 && !constName.empty()) {
                patternParams.emplace_back(vars[i], typeOf(constType));
                factParams.emplace_back(vars[i], typeOf(constType));
                mask |= uint64_t{1} << i;
            } else {
                patternParams.emplace_back(vars[i], refType("int"));
                factParams.emplace_back(vars[i], refType("Node"));
            }
        }
        const std::string prefix = name + "__case" + std::to_string(index);
        node_ptr_t patternBody;
        if (!constName.empty()) {
            // The pattern binds the constant itself: the matcher binds the node and the fact
            // function receives the value.
            patternBody = blockOf(reference(constName));
        } else {
            patternBody = any2node(visitBlockExpr(patternExpr));
        }
        declarations.push_back(makeFunc(
            prefix,
            withPairs(false),
            patternParams,
            constName.empty() ? refType("int") : typeOf(constType),
            patternBody));
        declarations.push_back(makeFunc(
            prefix + "_fact",
            withPairs(false),
            factParams,
            typeOf(factType),
            any2node(visitBlockExpr(rhs))));
        compiled.push_back({prefix, prefix + "_fact", mask});
    }

    // The query function: `func depth(n: Node): int => __fact(...)`. Its parameters are the parts
    // of a value a rule's condition hands it; without a parameter list the query takes the value
    // itself (`depth(n)`), as it always has.
    node_ptr_t args = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(args, context);
    *args << literal(LiteralType::String, name);
    // The type parameters, named so that the maker knows which types its functions were made at;
    // the type arguments of the instance replace this text (the fact's type arguments are part of
    // what the fact is).
    *args << literal(LiteralType::String, typeParamNames);
    *args << reference(name + "__initial");
    *args << reference(name + "__join");
    *args << reference(meetClause ? name + "__meet" : name + "__join");
    *args << reference(widenClause ? name + "__widen" : name + "__join");
    *args << reference(narrowClause ? name + "__narrow" : name + "__join");
    // How many arguments the query was written with, then the arguments themselves: the engine
    // computes and caches one fact per argument.
    *args << literal(LiteralType::Integer, std::to_string(withNames.size()));
    for (const std::string &paramName : withNames) {
        *args << reference(paramName);
    }
    for (const std::string &arg : queryArgs) {
        *args << reference(arg);
    }
    for (const CompiledCase &one : compiled) {
        *args << reference(one.patternFn);
        *args << reference(one.factFn);
        *args << literal(LiteralType::Integer, std::to_string(one.constMask));
    }
    node_ptr_t call = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(call, context);
    *call << reference("__fact") << args << createNodeAs<RepeatedLoad>("NamedData");
    declarations.push_back(
        makeFunc(name, withPairs(true), queryParams, typeOf(factType), blockOf(call)));

    LEAVE("FactDecl");
    return declarations;
}
