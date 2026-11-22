// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the type expressions and the type-level match: `typeExpr` with its ?/!/..
// marks, arrays and their dims, specialization, refs, packs, struct/tuple/func/enum types, and
// `match type` with its arms and cases, including the case-pattern machinery those cases share.

#include "builder_internal.h"

/*
matchTypeExpr
    : MATCH TYPE typeExpr '{' typeCase* '}'
    ;
*/
Value Builder::visitMatchTypeExpr(cst::MatchTypeExpr context) {
    ENTER("MatchTypeExpr");
    node_ptr_t matchNode = createNodeAs<TypeMatchLoad>();
    setNodeTokenRangeByContext(matchNode, context);
    node_ptr_t typeNode = any2node(visitType(context->type()));
    *matchNode << typeNode;
    node_ptr_t casesNode = createNodeAs<RepeatedLoad>("TypeCase");
    setNodeTokenRangeByContext(casesNode, context);
    for (const auto typeCase : context->all<cst::TypeCase>()) {
        *casesNode << any2node(visitTypeCase(typeCase));
    }
    *matchNode << casesNode;
    registerAstSemanticBundle(
        matchNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(matchNode, context->tok(TK::KwMatch), "ast.match.type"),
                -1,
                "match"),
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(matchNode, context->tok(TK::KwType), "ast.match.type.type"),
                -1,
                "type"),
            semanticPart(
                camel::source::SemanticRole::BranchCondition,
                nodeOrigin(typeNode),
                -1,
                "type"),
        });
    LEAVE("MatchTypeExpr");
    return matchNode;
}

TypeCaseKind Builder::typeCaseKindOf(cst::TypeCasePattern pattern) {
    const std::string text = pattern.text();
    if (const auto kind = typeCaseKindOfWord(text)) {
        return *kind;
    }
    // A word that names no shape is told, where it is written, instead of being read as `_`.
    diags_->of(SemanticDiag::StaticMatchPatternCannotMatch)
        .at(rangeOf(pattern->firstToken(), pattern->lastToken()))
        .commit(
            text,
            "it is not a shape word (struct, tuple, array, func, nullable, fallible, map, set, "
            "#leaf, _)");
    return TypeCaseKind::Wildcard;
}

template <class List> static std::vector<cst::TypeCasePattern> patternsOf(const List &list) {
    std::vector<cst::TypeCasePattern> out;
    for (const auto pattern : list) {
        out.push_back(pattern);
    }
    return out;
}

std::vector<TypeCasePattern>
Builder::typeCasePatternsOf(const std::vector<cst::TypeCasePattern> &written) {
    std::vector<TypeCasePattern> patterns;
    for (const auto pattern : written) {
        TypeCasePattern entry;
        // A pattern with a type child is an identity pattern (`case int`, `case T`); the
        // shape words carry no type.
        if (const auto typeNode = pattern.type()) {
            entry.kind = TypeCaseKind::Identity;
            entry.type = any2node(visitType(typeNode));
        } else {
            entry.kind = typeCaseKindOf(pattern);
        }
        patterns.push_back(std::move(entry));
    }
    return patterns;
}

/*
typeCase
    : CASE typeCasePattern ('|' typeCasePattern)* whenClause? '=>' blockExpr
    ;
A case may carry the guard a value-level `match` case carries (design principles 5.5):
`case p when c => body`, kept as child [1] after the body so the body stays child
[0] for the reader that builds it.
*/
Value Builder::visitTypeCase(cst::TypeCase context) {
    ENTER("TypeCase");
    node_ptr_t caseNode = createNodeAs<TypeCaseLoad>(
        typeCasePatternsOf(patternsOf(context->all<cst::TypeCasePattern>())));
    setNodeTokenRangeByContext(caseNode, context);
    node_ptr_t bodyNode = any2node(visitBlockExpr(context->lastChild().node()));
    *caseNode << bodyNode;
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        *caseNode << any2node(visitExpr(whenClause->expr()));
        unwrapNodeAs<TypeCaseLoad>(caseNode)->setGuardText(
            std::string(whenClause->expr().sourceText()));
    }
    registerAstSemanticBundle(
        caseNode,
        {
            semanticPart(
                camel::source::SemanticRole::Keyword,
                deriveAstAnchorOrigin(caseNode, context->tok(TK::KwCase), "ast.match.type.case"),
                -1,
                "case"),
            semanticPart(camel::source::SemanticRole::CaseBody, nodeOrigin(bodyNode), -1, "body"),
        });
    LEAVE("TypeCase");
    return caseNode;
}

/*
enumType
    : 'enum' '{' (enumVariant (',' enumVariant)*)? ','? '}'
    ;
enumVariant
    : identDef ('(' typeList? ','? ')' | '{' (enumField (',' enumField)*)? ','? '}')?
    ;
enumField
    : identDef ':' typeExpr
    ;
*/
Value Builder::visitEnumType(cst::EnumType context) {
    ENTER("EnumType");
    node_ptr_t typeNode = createNodeAs<EnumTypeLoad>();
    setNodeTokenRangeByContext(typeNode, context);
    auto &variants = typeNode->loadAs<EnumTypeLoad>()->variants();
    for (const auto variantContext : context->all<cst::EnumVariant>()) {
        EnumVariantDecl variant;
        variant.name = variantContext->get<cst::IdentDef>()->text();
        // The field types are the child nodes of the declaration, one list per variant in the
        // order the variants are written; GCT reads them back in that order.
        node_ptr_t fieldTypes = createNodeAs<RepeatedLoad>("EnumField");
        setNodeTokenRangeByContext(fieldTypes, variantContext);
        if (const auto tuple = variantContext->get<cst::TypeList>()) {
            variant.shape = EnumVariantShape::Tuple;
            for (const auto type : tuple->types()) {
                *fieldTypes << any2node(visitType(type));
            }
        } else if (variantContext->get<cst::EnumField>()) {
            variant.shape = EnumVariantShape::Struct;
            for (const auto field : variantContext->all<cst::EnumField>()) {
                variant.fieldNames.push_back(field->get<cst::IdentDef>()->text());
                *fieldTypes << any2node(visitType(field->type()));
            }
        }
        variants.push_back(std::move(variant));
        *typeNode << fieldTypes;
    }
    LEAVE("EnumType");
    return typeNode;
}

/*
typeExpr
    : arrayType ('?' | '!' | '..')*
    ;
*/
Value Builder::visitTypeExpr(cst::TypeExpr context) {
    ENTER("TypeExpr");
    node_ptr_t lhsNode = any2node(visitType(context->type(0)));
    // The marks, in the order they were read, innermost first: `T?!` is a fallible nullable,
    // `T?..` is the stream of a nullable `T`, and `T..?` is the possibly absent stream. `...` is
    // the stream (design principles 5.4), `?`/`!` the nullable and fallible marks.
    for (size_t i = 1; i < context->childCount(); ++i) {
        const std::string mark    = context->child(i)->text();
        node_ptr_t markedTypeNode = mark == ".." ? createNodeAs<StreamTypeLoad>()
                                                 : createNodeAs<NullableTypeLoad>(mark == "!");
        setNodeTokenRangeByContext(markedTypeNode, context);
        *markedTypeNode << lhsNode;
        lhsNode = markedTypeNode;
    }
    LEAVE("TypeExpr");
    return lhsNode;
}

/*
arrayType
    : specType arrayDim+
    ;
*/
Value Builder::visitArrayType(cst::ArrayType context) {
    ENTER("ArrayType");
    node_ptr_t res = any2node(visitType(context->type()));
    std::vector<DimGroup> groups;
    for (const auto dim : context->all<cst::ArrayDim>()) {
        groups.push_back(dimGroupOf(dim));
    }
    if (!groups.empty()) {
        node_ptr_t arrayTypeNode = createNodeAs<ArrayTypeLoad>(std::move(groups));
        setNodeTokenRangeByContext(arrayTypeNode, context);
        *arrayTypeNode << res;
        // The extent expressions stay children of the node as well: everything written is in
        // the tree, and a reader that walks it sees `?` as the one item that has no node.
        for (const DimGroup &group : arrayTypeNode->loadAs<ArrayTypeLoad>()->dims()) {
            for (const auto &extent : group.extents) {
                if (extent) {
                    *arrayTypeNode << extent;
                }
            }
        }
        res = arrayTypeNode;
    }
    LEAVE("ArrayType");
    return res;
}

/*
arrayDim
    : '[' (']' | arrayDimItem (',' arrayDimItem)* ','?)? ']'
    ;
arrayDimItem : '?' | '...' identDef? | dataExpr
*/
Value Builder::visitArrayDim(cst::ArrayDim context) {
    node_ptr_t res = createNodeAs<ArrayDimLoad>(dimGroupOf(context));
    setNodeTokenRangeByContext(res, context);
    for (const auto &extent : res->loadAs<ArrayDimLoad>()->dim().extents) {
        if (extent) {
            *res << extent;
        }
    }
    return res;
}

DimGroup Builder::dimGroupOf(cst::ArrayDim context) {
    DimGroup group;
    bool packNameNext = false;
    for (size_t i = 0; i < context->childCount(); ++i) {
        const auto child = context->child(i);
        if (child.isToken()) {
            if (child.token().is(TK::Question)) {
                group.extents.push_back(nullptr);
                ++group.itemCount;
                packNameNext = false;
            } else if (child.token().is(TK::Ellipsis) || child.token().is(TK::DotDot)) {
                if (group.pack != DimPack::None) {
                    group.packRepeated = true;
                } else {
                    group.pack     = DimPack::Anonymous;
                    group.packItem = group.itemCount;
                }
                ++group.itemCount;
                packNameNext = true;
            }
            continue;
        }
        const auto node = child.node();
        if (packNameNext && node.is(NK::IdentDef)) {
            group.pack     = DimPack::Named;
            group.packName = node.text();
            packNameNext   = false;
            continue;
        }
        packNameNext = false;
        if (node.isExpr()) {
            group.extents.push_back(any2node(visitExpr(node)));
            ++group.itemCount;
        }
    }
    return group;
}

/*
typeOrData : typeExpr | arrayDim | CONST dataExpr ;
The tree has a node for the second alternative only.
*/
Value Builder::visitConstData(cst::ConstData context) { return visitExpr(context->expr()); }

/*
specType
    : primaryType ('<' typeOrData (',' typeOrData)* '>')?
    ;
The node exists only with arguments: its first type child is the type that is specialized.
*/
Value Builder::visitSpecType(cst::SpecType context) {
    const auto types        = context->types();
    const size_t count      = types.size();
    node_ptr_t res          = any2node(visitType(types[0]));
    node_ptr_t specTypeNode = createNodeAs<SpecTypeLoad>();
    setNodeTokenRangeByContext(specTypeNode, context);
    if (count > 1) {
        node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("TypeOrData");
        setNodeTokenRangeBetween(repeatNode, types[1], types[count - 1]);
        for (size_t i = 1; i < count; ++i) {
            if (types[i].is(NK::ConstData) &&
                types[i].get<cst::ConstData>()->expr().is(NK::ArrayData)) {
                // A shape is the constant it is made of; `const` was how the shape of a module
                // type was written before array types took it over (plan P3, 4.1).
                diags_->of(SemanticDiag::InvalidTypeArgument)
                    .at(rangeOf(types[i].firstToken(), types[i].lastToken()))
                    .commit(
                        types[0].text(),
                        std::format(
                            "a shape is written without 'const': '{}<f32, [3]>'",
                            types[0].text()));
            }
            *repeatNode << any2node(visitType(types[i]));
        }
        *specTypeNode << res << repeatNode;
        res = specTypeNode;
    }
    return res;
}

/*
primaryType
    : structType | identRef | '(' typeExpr ')' | tupleType | funcType
    | TYPEOF dataExpr | TYPEAS identDef
    ;
The tree has no node for the rule, one kind per alternative instead.
*/
Value Builder::visitRefType(cst::RefType context) {
    node_ptr_t res = createNodeAs<RefTypeLoad>(Reference(context->text()));
    setNodeTokenRangeByContext(res, context);
    return res;
}

/*
matchTypeType : 'match' 'type' typeExpr '{' typeArm* '}'
typeArm       : 'case' typeCasePattern ('|' typeCasePattern)* whenClause? '=>' typeExpr
The cases are the cases of `match type` over values, and each holds the type it gives.
*/
Value Builder::visitMatchTypeType(cst::MatchTypeType context) {
    node_ptr_t res = createNodeAs<MatchTypeTypeLoad>();
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitType(context->type()));
    node_ptr_t arms = createNodeAs<RepeatedLoad>("TypeCase");
    setNodeTokenRangeByContext(arms, context);
    for (const auto arm : context->all<cst::TypeArm>()) {
        *arms << any2node(visitTypeArm(arm));
    }
    *res << arms;
    return res;
}

Value Builder::visitTypeArm(cst::TypeArm context) {
    node_ptr_t arm = createNodeAs<TypeCaseLoad>(
        typeCasePatternsOf(patternsOf(context->all<cst::TypeCasePattern>())));
    setNodeTokenRangeByContext(arm, context);
    *arm << any2node(visitType(context->type()));
    // The guard of a type function's case, after the type it gives for the same reason the value
    // case keeps it after its body: child [0] stays what the case answers.
    if (const auto whenClause = context->get<cst::WhenClause>()) {
        *arm << any2node(visitExpr(whenClause->expr()));
        unwrapNodeAs<TypeCaseLoad>(arm)->setGuardText(std::string(whenClause->expr().sourceText()));
    }
    return arm;
}

Value Builder::visitPackType(cst::PackType context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::Pack);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitType(context->type()));
    return res;
}

Value Builder::visitTypeOfExpr(cst::TypeOfExpr context) {
    node_ptr_t res = createNodeAs<TypeExprLoad>(TypeOp::TypeOf);
    setNodeTokenRangeByContext(res, context);
    *res << any2node(visitExpr(context->expr()));
    return res;
}

/*
structType
    : '{' (keyTypePair (',' keyTypePair)*)? ','? '}'
    ;
*/
Value Builder::visitStructType(cst::StructType context) {
    ENTER("StructType");
    node_ptr_t res        = createNodeAs<StructTypeLoad>();
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("NamedType");
    setNodeTokenRangeByContext(repeatNode, context);
    for (const auto pair : context->all<cst::KeyTypePair>()) {
        node_ptr_t pairNode = any2node(visitKeyTypePair(pair));
        *repeatNode << pairNode;
    }
    *res << repeatNode;
    LEAVE("StructType");
    return res;
}

/*
typeList
    : typeExpr (',' typeExpr)*
    ;
*/
Value Builder::visitTypeList(cst::TypeList context) {
    ENTER("TypeList");
    node_ptr_t repeatNode = createNodeAs<RepeatedLoad>("Type");
    setNodeTokenRangeByContext(repeatNode, context);
    for (const auto typeExpr : context->types()) {
        node_ptr_t typeExprNode = any2node(visitType(typeExpr));
        *repeatNode << typeExprNode;
    }
    LEAVE("TypeList");
    return repeatNode;
}

/*
tupleType
    : '(' typeList? ','? ')'
    ;
*/
Value Builder::visitTupleType(cst::TupleType context) {
    ENTER("TupleType");
    node_ptr_t res = createNodeAs<TupleTypeLoad>();
    setNodeTokenRangeByContext(res, context);
    if (context->get<cst::TypeList>()) {
        node_ptr_t repeatNode = any2node(visitTypeList(context->get<cst::TypeList>()));
        *res << repeatNode;
    } else {
        *res << createNodeAs<RepeatedLoad>("Type");
    }
    LEAVE("TupleType");
    return res;
}

/*
funcType
    : modifiers? angledParams? parentParams '=>' typeExpr
    ;
*/
Value Builder::visitFuncType(cst::FuncType context) {
    ENTER("FuncType");

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
    if (context->type()) {
        setNodeTokenRangeByContext(typeOptNode, context->type());
        setNodeTokenRangeByContext(typeOptNode, context->type());
        node_ptr_t typeNode = any2node(visitType(context->type()));
        *typeOptNode << typeNode;
    }
    *funcTypeNode << typeOptNode;

    if (context->get<cst::ResourceClause>()) {
        *funcTypeNode << any2node(visitResourceClause(context->get<cst::ResourceClause>()));
    }

    LEAVE("FuncType");
    return funcTypeNode;
}
