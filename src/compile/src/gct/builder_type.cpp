// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the lowering of type expressions -- visitType and every case below it, the
// extent arithmetic, and the namespace-aware lookup that resolves a written type name.

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
Type(TypeType type) :=
    StreamType | NullableType | TypeExpr | ArrayType | StructType | TupleType
    | FuncType | SpecType | RefType | EnumType | PackType ;
An `InferType` node never reaches this dispatch: it is the marker of a type left out of a
lambda's parameter list, and the signature builders read it where it stands (design
principles 5.9).
*/
Type *Builder::visitType(const AST::node_ptr_t &ast) {
    ENTER("Type");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for visitType");

    // Extract the type load from the AST node
    const auto &type = ast->loadAs<AST::TypeLoad>();
    Type *res;
    // Whether this type is the base of `Name<...>`, the one place a type function may stand.
    const bool asTypeFunctionBase = std::exchange(typeFunctionBase_, false);

    // Handle different type categories based on the TypeType enumeration
    switch (type->typeType()) {
    case AST::TypeType::Null:
        res = visitNullableType(ast);
        break;
    case AST::TypeType::Stream:
        res = visitStreamType(ast);
        break;
    case AST::TypeType::Expr:
        res = visitTypeExpr(ast);
        break;
    case AST::TypeType::Array:
        res = visitArrayType(ast);
        break;
    case AST::TypeType::ArrayDim:
        // A shape is an argument of a type, never a type of its own.
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit("a shape", "a shape is written after the element type, as in 'f32[3]'");
        throw BuildAbortException();
    case AST::TypeType::Struct:
        res = visitStructType(ast);
        break;
    case AST::TypeType::Tuple:
        res = visitTupleType(ast);
        break;
    case AST::TypeType::Func:
        res = visitFuncType(ast);
        break;
    case AST::TypeType::Spec:
        res = visitSpecType(ast);
        break;
    case AST::TypeType::Match:
        res = visitMatchTypeType(ast);
        break;
    case AST::TypeType::Ref:
        res = visitRefType(ast);
        break;
    case AST::TypeType::Enum:
        // An enum is declared, not written where a value of it is expected: its variants are
        // names of the module (`type Shape = enum {...}`), and a use is that name.
        diags_->of(SemanticDiag::EnumWrittenWhereATypeIsExpected)
            .at(ast->load()->tokenRange())
            .commit();
        throw BuildAbortException();
    default:
        throw std::runtime_error("Unknown type type");
    }

    // `Map` and `Set` are always written with their arguments.
    if (const CollectionType *coll = res ? asCollectionTypeOrPrototype(res) : nullptr;
        coll != nullptr && !coll->complete() && !asTypeFunctionBase) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(
                coll->toString(),
                coll->isMap() ? "it is written Map<K, V>, with a key type and a value type"
                              : "it is written Set<T>, with an element type");
        throw BuildAbortException();
    }

    if (res != nullptr && res->code() == TypeCode::TypeFunc && !asTypeFunctionBase) {
        const auto *typeFunc = static_cast<TypeFunctionType *>(res);
        diags_->of(SemanticDiag::TypeFunctionArguments)
            .at(ast->load()->tokenRange())
            .commit(
                typeFunc->toString(),
                std::format(
                    "it takes {} type argument(s) and is used without any",
                    typeFunc->params().size()));
        throw BuildAbortException();
    }

    if (auto *recorder = context_ ? context_->semanticRecorder() : nullptr) {
        recorder->typeExpression(ast->load()->origin(), res);
    }
    LEAVE("Type");
    return res;
}

/*
StreamType : Type type ;
*/
Type *Builder::visitStreamType(const AST::node_ptr_t &ast) {
    ENTER("StreamType");
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for StreamType");
    Type *elemType = visitType(ast->atAs<AST::TypeLoad>(0));
    LEAVE("StreamType");
    return StreamType::create(elemType);
}

/*
NullableType : Type type ;
*/
Type *Builder::visitNullableType(const AST::node_ptr_t &ast) {
    ENTER("NullableType");
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for NullableType");
    Type *payload       = visitType(ast->atAs<AST::TypeLoad>(0));
    const bool fallible = ast->loadAs<AST::NullableTypeLoad>()->fallible();
    // The same mark twice (`T??`, `T!!`) adds nothing a program could tell apart; `T?!` and
    // `T!?` are allowed (design principles 3.8).
    if (TupleType *inner = asTagged(payload); inner && inner->isFallible() == fallible) {
        diags_->of(SemanticDiag::TaggedTypeNesting)
            .at(ast->load()->tokenRange())
            .commit(payload->toString() + (fallible ? "!" : "?"), inner->payloadType()->toString());
        throw BuildAbortException();
    }
    Type *type = fallible ? TupleType::fallible(payload) : TupleType::nullable(payload);
    LEAVE("NullableType");
    return type;
}

Type *Builder::typeOfPlaceholder(const node_ptr_t &expression) {
    // The expression is the front end's. The type layer is handed a type variable named for it, so
    // nothing in a type can be asked for a node of a build: the tree holds the expression and the
    // graph builder asks it (Tree::typeOfOperandOf) where the type is used.
    GUARD(expression != nullptr, "typeof holds the expression it was written with.");
    return TypeVarType::create(tree_->makeTypeOfPlaceholder(expression), false);
}

/*
TypeExpr(TypeOp op) := Type lhs, Type rhs ;
enum TypeOp {
    TypeOf
}
The reserved type operators (`&`, `\`, `+`, `-`, `^`) and the older spelling of specialization were
read as `TypeOp` codes and refused here; they are not in the language, so the parser refuses them
first and `typeof` is the only operation a `TypeExpr` carries.
*/
Type *Builder::visitTypeExpr(const AST::node_ptr_t &ast) {
    ENTER("TypeExpr");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TypeExpr");

    Type *res;
    const auto &typeExpr = ast->loadAs<AST::TypeExprLoad>();
    AST::TypeOp op       = typeExpr->op();

    switch (op) {
    case AST::TypeOp::TypeOf: {
        // `typeof e`: the type of the value `e` will have. The expression stays the front end's:
        // the type layer is handed a type variable named for it, and the graph builder infers `e`
        // where this type is used and replaces the variable by the inferred type. The node is not
        // attached to a parent: the tree keeps it, and it is visited per instance.
        GUARD(ast->size() == 1, "typeof has its expression as its only child.");
        res = typeOfPlaceholder(visitData(ast->at(0)));
    } break;
    case AST::TypeOp::Pack: {
        // `..E`: a type expression written once for each member of a pack (design principles 5.6).
        // The pack is not written where the expansion stands. It is either one of the head
        // parameters of the enclosing declaration that was written with `..`, or a pack the body
        // computes (`..ParamsOf<F>`, a pack written out); what the body may not do is name two
        // different head packs, since then nothing says which one it ranges over. Anything else
        // that leaves the expansion without members is reported where a call would bind it.
        GUARD(ast->size() == 1, "A pack expansion has its type as its only child.");
        Type *body = visitType(ast->at(0));
        if (body == nullptr) {
            res = Type::Void();
            break;
        }
        std::vector<std::string> named;
        collectTypeVarsWithoutPlaceholders(body, named);
        std::vector<std::string> packs;
        for (const std::string &name : named) {
            if (std::find(declaredPackVars_.begin(), declaredPackVars_.end(), name) !=
                declaredPackVars_.end()) {
                packs.push_back(name);
            }
        }
        if (packs.size() > 1) {
            std::string declared;
            for (const std::string &name : declaredPackVars_) {
                declared += (declared.empty() ? "" : ", ") + std::string("...") + name;
            }
            std::string wrote;
            for (const std::string &name : named) {
                wrote += (wrote.empty() ? "" : ", ") + name;
            }
            diags_->of(SemanticDiag::PackExpansionNeedsOnePack)
                .at(ast->load()->tokenRange())
                .commit(
                    "..." + body->toString(),
                    "its head declares " + declared + ", and the body names " + wrote);
            throw BuildAbortException();
        }
        res = TypeCallType::makeExpand(body);
    } break;
    }

    LEAVE("TypeExpr");
    return res;
}

/*
ArrayType() : Type type, extent* ;
One `[...]` is one dimension, also when it is empty: `T[][]` is an array of arrays, and `T[3]`
is one dimension of three. The pack makes the rank open after the extents written before it.
*/
Type *Builder::visitArrayType(const AST::node_ptr_t &ast) {
    ENTER("ArrayType");
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for ArrayType");
    const auto &arrayTypeLoad = ast->loadAs<AST::ArrayTypeLoad>();
    Type *type                = visitType(ast->atAs<AST::TypeLoad>(0));
    for (const AST::DimGroup &group : arrayTypeLoad->dims()) {
        const bool pack = group.pack != AST::DimPack::None;
        if (pack && (group.packRepeated || group.packItem + 1 != group.itemCount)) {
            // A pack is the trailing dimensions the type does not name; it can only be last,
            // and only once (design principles 5.4).
            diags_->of(SemanticDiag::DimensionAfterAnyRank).at(ast->load()->tokenRange()).commit();
            throw BuildAbortException();
        }
        // A stream is consumed once and the elements of an array are read more than once, so
        // `T..[3]` has no meaning (design principles 5.4).
        // The dimension is read against the stream here, so the element of the array the
        // dimension would make is refused before the array is built.
        if (type != nullptr && type->code() == TypeCode::Stream) {
            diags_->of(SemanticDiag::StreamInContainer)
                .at(ast->load()->tokenRange())
                .commit(type->toString(), "an array");
            throw BuildAbortException();
        }
        const auto extents = visitExtents(group, ast);
        if (!extents) {
            throw BuildAbortException();
        }
        try {
            if (!pack) {
                type = ArrayType::create(type, *extents);
            } else if (group.pack == AST::DimPack::Anonymous) {
                type = ArrayType::openRank(type, *extents);
            } else {
                type = ArrayType::create(type, *extents, group.packName);
            }
        } catch (const std::invalid_argument &) {
            // A negative extent is not a dimension; `?` is the unknown extent.
            diags_->of(SemanticDiag::InvalidExtentExpression)
                .at(ast->load()->tokenRange())
                .commit();
            throw BuildAbortException();
        }
    }
    LEAVE("ArrayType");
    return type;
}

/*
StructType() : NamedType* types ;
*/
Type *Builder::visitStructType(const AST::node_ptr_t &ast) {
    ENTER("StructType");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for StructType");

    StructTypeFactory factory;
    // The fields of the struct a type declaration writes are its own: a tag written on one of
    // them belongs to that declaration (visitTypeDecl). A struct written anywhere else has no
    // declaration to hold its field tags, which is reported rather than dropped.
    const node_ptr_t owner = std::exchange(fieldTagOwner_, nullptr);
    size_t index           = 0;
    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedType = child->loadAs<AST::NamedTypeLoad>();
        const string &name    = namedType->getRef().ident();
        if (owner != nullptr) {
            tree_->addFieldTagUses(owner, index, tagUsesOf(child));
        } else {
            for (const auto &use : tagUsesOf(child)) {
                diags_->of(SemanticDiag::TagOnUnnamedField)
                    .atOrigin(use.origin)
                    .commit(namedType->getRef().toString());
            }
        }
        ++index;
        Type *type = visitType(child->atAs<AST::TypeLoad>(0));

        if (!factory.add(name, type)) {
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedType->tokenRange()).commit(name);
            throw BuildAbortException();
        }
    }
    LEAVE("StructType");
    return factory.build();
}
/*
TupleType() : Type* types ;
*/
Type *Builder::visitTupleType(const AST::node_ptr_t &ast) {
    ENTER("TupleType");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for TupleType");

    // Create a vector to store the types of the tuple elements
    vector<Type *> types;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        Type *type = visitType(child);
        types.push_back(type);
    }
    // Create a TupleType object using the collected types
    Type *tupleType = expandTypePacks(TupleType::create(types));
    LEAVE("TupleType");
    return tupleType;
}

/*
FuncType(Modifier[] modifiers, string uri)
    : NamedPair* withParams, NamedPair* normParams, Type? ExitType ;
A `with` parameter may write a default (`with <rounds: int = 8>`). The default is not part of
the function type: it is lowered here, in the scope of the signature, and handed to the caller
(visitFuncData keeps it on the FUNC node) so that GIR evaluates it once, where the declaration
is, and fills the value into each call that leaves the parameter out (design principles 4.4).
*/
Type *Builder::visitFuncType(const AST::node_ptr_t &ast, Tree::ParameterDefaults *defaultsOut) {
    ENTER("FuncType");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for FuncType");

    // Extract the function type load from the AST node
    auto const &typeLoad = ast->loadAs<AST::FuncTypeLoad>();

    // A declaration declares its type variables at its head (design principles 5.5): the names in
    // `::<T, ...>` are the type variables of the signature and of the body, and a use of one in
    // a type is an occurrence an argument can bind. A plain name that no head declares is an
    // ordinary type name; if no type answers it, it is an unknown type (UnresolvedTypeReference).
    const bool signature = std::exchange(declSignature_, false);
    const bool open      = std::exchange(openSignature_, false);
    // Named dimensions (design principles 5.4). A declaration gets a scope of its own, and only
    // its parameter types bind into it; the return type may use the names the parameters bound.
    // A function type nested in another signature sees those names but binds none of its own.
    struct DimGuard {
        Builder &self;
        std::shared_ptr<std::set<std::string>> names;
        std::shared_ptr<std::set<std::string>> bound;
        std::shared_ptr<std::set<std::string>> packs;
        std::shared_ptr<std::map<std::string, size_t>> uses;
        bool bind;
        explicit DimGuard(Builder &builder)
            : self(builder), names(builder.dimNames_), bound(builder.boundDims_),
              packs(builder.dimPacks_), uses(builder.dimUses_), bind(builder.dimBindAllowed_) {}
        ~DimGuard() {
            self.dimNames_       = names;
            self.boundDims_      = bound;
            self.dimPacks_       = packs;
            self.dimUses_        = uses;
            self.dimBindAllowed_ = bind;
        }
    } dimGuard(*this);
    if (signature) {
        dimNames_  = std::make_shared<std::set<std::string>>();
        boundDims_ = std::make_shared<std::set<std::string>>();
        dimPacks_  = std::make_shared<std::set<std::string>>();
        dimUses_   = std::make_shared<std::map<std::string, size_t>>();
    }
    dimBindAllowed_ = false;
    if (signature) {
        // The head names are in scope from here on: a parameter type, a return type, a function
        // type nested in either, and the body all resolve them before any type of the module. A
        // name declared here therefore wins over a type of the same name, which is the point of
        // declaring it (design principles 5.5).
        for (const AST::TypeParam &param : typeLoad->typeParams()) {
            const std::string &name = param.name();
            if (std::find(declaredTypeVars_.begin(), declaredTypeVars_.end(), name) !=
                declaredTypeVars_.end()) {
                diags_->of(SemanticDiag::TypeVariableRedeclared)
                    .at(ast->load()->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            Type *occurrence = TypeVarType::create(name, false);
            typeScope_->insert(Reference(name), occurrence);
            declaredTypeVars_.push_back(name);
            if (headTypeVarTypes_) {
                (*headTypeVarTypes_)[name] = occurrence;
            }
            if (param.pack()) {
                // A pack (`..A`) is written only in an expansion, so it is recorded where an
                // expansion can find the name its body names (the `TypeOp::Pack` case).
                declaredPackVars_.push_back(name);
            }
        }
    }
    if (signature) {
        // A `constas n: int` parameter is a value the signature uses, not a dimension the
        // types bind; its name is known to the types from the start. It is counted as used
        // twice so the one-use suggestion below belongs to dimension names only.
        for (const size_t list : {size_t{0}, size_t{1}}) {
            for (const auto &paramPair : *ast->atAs<AST::RepeatedLoad>(list)) {
                const auto &paramLoad = paramPair->loadAs<AST::NamedPairLoad>();
                if (paramLoad->isConstas()) {
                    dimNames_->insert(paramLoad->getRef().ident());
                    (*dimUses_)[paramLoad->getRef().ident()] = 2;
                }
            }
        }
    }

    FunctionTypeFactory factory;

    std::unordered_set<string> paramNames;

    // The parameters of a lambda that leave their type out (design principles 5.9). The
    // signature stands `void` in their place; the index (with parameters first) and the origin
    // of the pair go to the FUNC node, and GIR, which alone knows the function type the lambda
    // stands in, fills the type or reports the parameter.
    std::vector<Tree::UnwrittenParam> unwrittenParams;
    size_t paramIndex      = 0;
    const auto paramTypeOf = [&](const AST::node_ptr_t &paramPair, const string &name) -> Type * {
        const size_t index              = paramIndex++;
        const AST::node_ptr_t &typeNode = paramPair->at(0);
        const bool omitted = typeNode->type() == AST::LoadType::Type &&
                             typeNode->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::Infer;
        if (!omitted) {
            return visitType(typeNode);
        }
        if (!open) {
            // A function type and a declaration write every parameter's type; the parser only
            // leaves one out in a lambda's parameter list.
            diags_->of(SemanticDiag::LambdaParameterWithoutType)
                .atOrigin(typeNode->load()->origin())
                .commit(name, "a function type and a declaration write every parameter's type");
            throw BuildAbortException();
        }
        unwrittenParams.push_back({index, typeNode->load()->origin()});
        return Type::Void();
    };

    // A function type's parameter written `..P` where `P` is a type variable, or `..E` for a type
    // expression `E`, is a pack of parameters: one parameter for each member of the pack once it
    // is known (design principles 5.6). A name that is no type variable keeps its meaning of
    // "any parameter list". A declaration's own rest parameter is not one (it is typed
    // `..xs: ..A`, below).
    const auto packOf = [&](const AST::node_ptr_t &paramPair, const string &name) -> Type * {
        if (open || signature) {
            return nullptr;
        }
        if (paramPair->size() > 0) {
            Type *written = visitType(paramPair->at(0));
            return TypeCallType::isExpand(written) ? written : TypeCallType::makeExpand(written);
        }
        if (const auto bound = typeScope_->get(Reference(name)); bound.has_value()) {
            return (*bound)->code() == TypeCode::TypeVar ? TypeCallType::makeExpand(*bound)
                                                         : nullptr;
        }
        if (signatureVisit_ && !typeScope_->has(Reference(name), false)) {
            // A name the declaration's head does not declare, written after `..` in a parameter
            // list of its signature, is a pack bound at its first occurrence by the function the
            // argument turns out to be, as a dimension name is bound by the first shape that
            // writes it (design principles 5.4, 5.6).
            Type *variable = TypeVarType::create(name, false);
            typeScope_->insert(Reference(name), variable);
            declaredTypeVars_.push_back(name);
            if (headTypeVarTypes_) {
                (*headTypeVarTypes_)[name] = variable;
            }
            return TypeCallType::makeExpand(variable);
        }
        return nullptr;
    };

    // Process "with" parameters (context parameters)
    // Their types use the type variables the head declares (design principles 5.5), like the
    // ordinary parameters below.
    Tree::ParameterDefaults defaults;
    // The last `with` parameter that wrote a default: a later parameter without one is refused,
    // since a call that leaves the earlier ones out can then only give the others by name.
    std::string lastDefaultName;
    const auto &withParams = *ast->atAs<AST::RepeatedLoad>(0);
    for (size_t wi = 0; wi < withParams.size(); ++wi) {
        const auto &paramPair     = withParams[wi];
        const auto &paramLoad     = paramPair->loadAs<AST::NamedPairLoad>();
        const bool isVar          = paramLoad->isVar();
        const Reference &paramRef = paramLoad->getRef();
        const string &name        = paramRef.ident();
        if (paramLoad->isRest()) {
            if (Type *pack = packOf(paramPair, name)) {
                defaults.with.push_back({nullptr, camel::source::kInvalidOriginId});
                factory.addWithArg(name + "_" + std::to_string(wi), pack, false);
                continue;
            }
            // A pack among the `with` parameters (`with <..args>`): last, like the rest parameter
            // of the ordinary ones, and typed or not. A pack that writes a type (`with <..args:
            // T>`) takes `with` arguments of that type, on the rule an ordinary parameter follows
            // one type further out (design principles 4.4, 5.6); `with <..args>` takes any type.
            // What the body may do with the pack is built where the body is.
            if (wi + 1 != withParams.size()) {
                diags_->of(SemanticDiag::RestParameterNotLast)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            if (!paramNames.insert(name).second) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            defaults.with.push_back({nullptr, camel::source::kInvalidOriginId});
            factory.addWithRest(
                name,
                paramPair->size() > 0 ? visitType(paramPair->at(0)) : nullptr);
            // The pack takes one slot in the numbering of the parameters the function wrote, which
            // is the numbering the unwritten parameters of a lambda are recorded against (`with`
            // parameters first): a parameter written after it (an ordinary rest parameter, say) is
            // one place further on than its position in the source suggests.
            ++paramIndex;
            continue;
        }
        Type *type = nullptr;
        if (paramLoad->isConstas()) {
            // A `constas` parameter of the `with` list is a value a call gives in angle brackets
            // (design principles 5.4); it writes its type and may default.
            if (!signature) {
                throw std::runtime_error("A constas parameter outside a function declaration.");
                throw BuildAbortException();
            }
            if (paramPair->size() == 0) {
                diags_->of(SemanticDiag::ConstasTypeRequired)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            type = visitType(paramPair->at(0));
        } else {
            dimBindAllowed_ = signature;
            type            = paramTypeOf(paramPair, name);
            dimBindAllowed_ = false;
        }

        // The default of a `with` parameter (design principles 4.4). The
        // expression is lowered here, in the scope of the signature; GIR evaluates it where the
        // declaration is, once, when it builds the graph (visitFuncNode / visitDeclNode).
        node_ptr_t defaultExpr = nullptr;
        const auto &dataNode =
            paramPair->size() > 1 ? paramPair->optAtAs<AST::DataLoad>(1) : nullptr;
        if (dataNode) {
            defaultExpr     = visitData(dataNode);
            lastDefaultName = name;
        } else if (!lastDefaultName.empty()) {
            diags_->of(SemanticDiag::WithParameterAfterDefault)
                .at(paramLoad->tokenRange())
                .commit(name, lastDefaultName);
            throw BuildAbortException();
        }
        defaults.with.push_back(
            {defaultExpr, dataNode ? dataNode->load()->origin() : camel::source::kInvalidOriginId});

        if (!paramNames.insert(name).second) {
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        if (paramLoad->isConstas()) {
            factory.addWithConstasArg(name, type);
        } else {
            // The query of a fact marks its `with` parameters `const`: the compiler must compute
            // the argument, since it is part of which fact this is.
            factory.addWithArg(name, type, isVar, paramLoad->isConst());
        }
    }

    // Process normal parameters
    const auto &normParams = *ast->atAs<AST::RepeatedLoad>(1);
    // The last ordinary parameter that wrote a default. The rule is the same as for `with`
    // parameters (design principles 4.4): a later one must write a default too, so that a call
    // that leaves an earlier one out can still be read as giving the others by name.
    std::string lastNormDefaultName;
    for (size_t pi = 0; pi < normParams.size(); ++pi) {
        const auto &paramPair     = normParams[pi];
        const auto &paramLoad     = paramPair->loadAs<AST::NamedPairLoad>();
        const Reference &paramRef = paramLoad->getRef();
        const string &name        = paramRef.ident();
        // Every ordinary parameter takes a place in the defaults list, so that an index into it
        // is a parameter's index: `constas` values and the rest parameter included.
        defaults.norm.push_back({});
        if (paramLoad->isConstas()) {
            // Only a function declaration offers `constas` in its parameter list (the parser
            // admits it nowhere else). A declaration may have any number of them, in any
            // position, and their values are not read by a type necessarily: the body may use
            // them (design principles 5.4).
            if (!signature) {
                // The parser admits `constas` only in a declaration's own parameter list
                // (`parseParentParams(true, true)`, the single call site in parser.cpp), so this
                // is a compiler bug rather than anything a program can write.
                throw std::runtime_error("A constas parameter outside a function declaration.");
                throw BuildAbortException();
            }
            if (paramPair->size() == 0) {
                diags_->of(SemanticDiag::ConstasTypeRequired)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            Type *const type = visitType(paramPair->at(0));
            if (!paramNames.insert(name).second) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            factory.addConstasArg(name, type);
            continue;
        }
        if (paramLoad->isRest()) {
            if (Type *pack = packOf(paramPair, name)) {
                factory.addNormArg(name + "_" + std::to_string(pi), pack, false);
                continue;
            }
            // A rest parameter is untyped and last (design principles 5.5); it stands in the
            // signature as a `void` parameter that no call passes a value for.
            if (open && !signature) {
                // A lambda's rest parameter (`(..xs) => body`) stands for the parameters of the
                // function type the lambda is in. That type is not known at this stage -- it builds
                // a signature, and the expected type is GIR's -- so the slot is recorded the way an
                // omitted parameter type is, with one difference: it absorbs every parameter from
                // here on rather than taking one type (`rest`). GIR expands it where the expected
                // type is in hand. The flag comes from the written parameter (`isRest()`), never
                // from the arity.
                factory.addNormArg(name, Type::Void(), false);
                unwrittenParams.push_back({paramIndex++, paramLoad->origin(), true});
                continue;
            }
            if (pi + 1 != normParams.size()) {
                diags_->of(SemanticDiag::RestParameterNotLast)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            // A rest parameter is untyped, or typed with the pack variable its declaration
            // declares (`..xs: ..A`, with `::<..A>` at the head): the call binds the variable to
            // the types of the arguments the rest parameter takes.
            Type *packVariable = nullptr;
            if (paramPair->size() > 0) {
                Type *written = visitType(paramPair->at(0));
                if (TypeCallType::isExpand(written) &&
                    static_cast<TypeCallType *>(written)->paramsSpan()[0]->code() ==
                        TypeCode::TypeVar) {
                    packVariable = static_cast<TypeCallType *>(written)->paramsSpan()[0];
                } else {
                    diags_->of(SemanticDiag::RestParameterTypeNotSupported)
                        .at(paramLoad->tokenRange())
                        .commit(name);
                    throw BuildAbortException();
                }
            }
            if (!paramNames.insert(name).second) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .at(paramLoad->tokenRange())
                    .commit(name);
                throw BuildAbortException();
            }
            factory.addRestArg(name, packVariable);
            continue;
        }
        const bool isVar = paramLoad->isVar();
        Type *type       = nullptr;
        {
            dimBindAllowed_ = signature;
            type            = paramTypeOf(paramPair, name);
            dimBindAllowed_ = false;
        }
        // The default of an ordinary parameter (design principles 4.4):
        // the same rule as for a `with` parameter. The expression is lowered here, in the
        // scope of the signature; GIR evaluates it where the declaration is, once, when it
        // builds the graph, and fills the value into each call that leaves the parameter out.
        node_ptr_t defaultExpr = nullptr;
        const auto &dataNode   = paramPair->optAtAs<AST::DataLoad>(1);
        if (dataNode) {
            defaultExpr         = visitData(dataNode);
            lastNormDefaultName = name;
        } else if (!lastNormDefaultName.empty()) {
            diags_->of(SemanticDiag::ParamAfterDefault)
                .at(paramLoad->tokenRange())
                .commit(name, lastNormDefaultName);
            throw BuildAbortException();
        }
        defaults.norm.back() = {
            defaultExpr,
            dataNode ? dataNode->load()->origin() : camel::source::kInvalidOriginId};

        if (!paramNames.insert(name).second) {
            diags_->of(SemanticDiag::DuplicateParameter).at(paramLoad->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        factory.addNormArg(name, type, isVar);
    }

    // The return type may use the head's type variables like any other type.
    const auto &exitTypeNode = ast->optAtAs<AST::TypeLoad>(2);
    if (exitTypeNode) {
        dimBindAllowed_ = false;
        factory.setExitType(visitType(exitTypeNode));
    }

    LEAVE("FuncType");
    if (!unwrittenParams.empty()) {
        pendingUnwrittenParams_ = std::move(unwrittenParams);
    }
    FunctionType *built = open ? static_cast<FunctionType *>(factory.buildOpen()) : factory.build();
    if (!open && !signature) {
        // A pack of parameters that is known already (`(..ParamsOf<F>) => R` for a written-out
        // `F`) is its members.
        built = static_cast<FunctionType *>(expandTypePacks(built));
    }
    // A type parameter declared at the head and written nowhere in the signature is a name the
    // declaration never uses: no argument could give it a type, and the function could never be
    // instantiated (design principles 5.5).
    if (signature) {
        std::vector<std::string> used;
        collectTypeVarsWithoutPlaceholders(built, used);
        // A `rule`'s head is written once and its sides are three declarations of the compiler's
        // making, so "the signature" of a rule side is not where the rule's parameters are written:
        // `mul_tensor_t::<T, X, W>` names `T` only on its right side (`(ct: T[..])`), which is the
        // author's line and cannot be written any other way. What the rule writes is carried here
        // from the rule's own text (`src/parse/ast/builder.cpp`, `visitRuleDecl`), so the check
        // reads the whole declaration and not the one side it happens to be building.
        const auto &acrossTheRule = ast->loadAs<AST::FuncTypeLoad>()->typeParamsWrittenElsewhere();
        pendingUnusedHeadVars_.clear();
        for (const std::string &name : declaredTypeVars_) {
            if (std::find(used.begin(), used.end(), name) == used.end() &&
                !writtenHeadVars_.contains(name) &&
                std::find(acrossTheRule.begin(), acrossTheRule.end(), name) ==
                    acrossTheRule.end()) {
                pendingUnusedHeadVars_.push_back(name);
            }
        }
    }
    // A dimension name written once constrains nothing: another parameter (or the return type)
    // would have to write it to make it a relation (design principles 5.4).
    if (signature && dimUses_) {
        for (const auto &[name, uses] : *dimUses_) {
            if (uses == 1) {
                diags_->of(SemanticDiag::DimensionNameUsedOnce)
                    .at(ast->load()->tokenRange())
                    .commit(name);
            }
        }
    }
    // The clause the type writes (design effects plan 3.1): what a call through a value of this
    // type touches. A type without one stays Generic, which promises nothing, so nothing is set
    // and a call through it is read conservatively.
    const sem::ResourceSignature resources = resourceSignatureOf(ast, built);
    // A clause that writes the capability and nothing else is Generic (`f! spelling`): it promises
    // nothing about resources, but it is still a clause the type has to carry, or a caller and the
    // check of the body would read a signature that says nothing at all (design principles 9.5).
    if (resources.mode == sem::ResourceSignature::Mode::Concrete || resources.spelling) {
        if (built->asOpen() != nullptr) {
            // An open signature is not interned and is resolved by each call site; it is this
            // object the resolver copies from (step 2: instantiateFunctionType copies the
            // resources), so the clause is set on it in place.
            built->asOpen()->setResources(resources);
        } else {
            built = built->withResources(resources);
        }
    }
    if (defaultsOut != nullptr) {
        *defaultsOut = std::move(defaults);
    }
    return built;
}

// DESIGN-QUESTION(map-duplicate-keys): in an array literal written under `as Map<...>`, two
// equal constant keys are an error. Entries that are not constants (they refer to a name) are
// not looked at; at run time the later entry of two equal keys wins. A set takes duplicates.
void Builder::rejectDuplicateLiteralKeys(
    const data_ptr_t &data, Type *targetType, const AST::node_ptr_t &ast) {
    const CollectionType *coll = asCollectionTypeOrPrototype(targetType);
    const auto array           = std::dynamic_pointer_cast<ArrayData>(data);
    if (coll == nullptr || !coll->isMap() || !array) {
        return;
    }
    const auto holes = array->holes();
    // Candidates by their text, so that a long literal is not compared pair by pair.
    std::unordered_map<std::string, std::vector<data_ptr_t>> seen;
    for (size_t i = 0; i < array->raw().size(); ++i) {
        if (std::find(holes.begin(), holes.end(), i) != holes.end()) {
            continue;
        }
        const auto entry = std::dynamic_pointer_cast<TupleData>(array->raw()[i]);
        if (!entry || !entry->resolved() || entry->raw().size() != 2 || !entry->raw()[0]) {
            continue;
        }
        const data_ptr_t &key             = entry->raw()[0];
        std::vector<data_ptr_t> &sameText = seen[key->toString()];
        for (const data_ptr_t &earlier : sameText) {
            if (earlier->equals(key)) {
                diags_->of(SemanticDiag::DuplicateCollectionKey)
                    .at(ast->load()->tokenRange())
                    .commit(key->toString(), targetType->toString());
                throw BuildAbortException();
            }
        }
        sameText.push_back(key);
    }
}

namespace {

/// The plain name a member of a pattern writes inside `...`, or empty: `...Rest` writes one, and
/// `...ParamsOf<F>` writes a type expression, which a pattern's member can be but a name it cannot
/// bind.
std::string packRestNameOf(const AST::node_ptr_t &member) {
    if (member == nullptr || member->type() != AST::LoadType::Type) {
        return {};
    }
    if (member->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Expr) {
        return {};
    }
    const auto written = member->loadAs<AST::TypeExprLoad>();
    if (written->op() != AST::TypeOp::Pack || member->size() == 0) {
        return {};
    }
    const AST::node_ptr_t &inner = member->at(0);
    if (inner == nullptr || inner->type() != AST::LoadType::Type ||
        inner->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Ref) {
        return {};
    }
    const auto ref = inner->loadAs<AST::RefTypeLoad>();
    return ref->ref().paths().empty() ? ref->ref().ident() : std::string();
}

/// The plain name a member of a pattern writes as it stands, or empty: `(W, ...Rest)` names its
/// first member `W`, and `(f32[2], ...Rest)` names none.
std::string writtenNameOf(const AST::node_ptr_t &member) {
    if (member == nullptr || member->type() != AST::LoadType::Type ||
        member->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Ref) {
        return {};
    }
    const auto ref = member->loadAs<AST::RefTypeLoad>();
    return ref->ref().paths().empty() ? ref->ref().ident() : std::string();
}

} // namespace

bool Builder::isPackPattern(const AST::node_ptr_t &typeAst) const {
    if (typeAst == nullptr || typeAst->type() != AST::LoadType::Type ||
        typeAst->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Tuple) {
        return false;
    }
    if (typeAst->size() == 0) {
        return false;
    }
    const auto &members = *typeAst->atAs<AST::RepeatedLoad>(0);
    return members.size() > 0 && !packRestNameOf(members.at(members.size() - 1)).empty();
}

void Builder::packPatternNamesOf(const AST::node_ptr_t &typeAst, std::vector<std::string> &bound) {
    if (!isPackPattern(typeAst)) {
        return;
    }
    // The names the case has written so far, and the types the scopes and the imports answer,
    // decide which of the pattern's names are new (design principles 5.4 binds a dimension name the
    // same way: a name a type here already answers is a reference).
    const auto &members = *typeAst->atAs<AST::RepeatedLoad>(0);
    // The members the pattern writes out bind first, then the rest binds what is left of the pack.
    for (size_t i = 0; i + 1 < members.size(); ++i) {
        const std::string member = writtenNameOf(members.at(i));
        if (!member.empty() && patternNameIsNew(member, bound)) {
            bound.push_back(member);
        }
    }
    const std::string rest = packRestNameOf(members.at(members.size() - 1));
    if (!rest.empty() && patternNameIsNew(rest, bound)) {
        bound.push_back(rest);
    }
}

const AST::DimGroup *Builder::arrayPatternDimsOf(const AST::node_ptr_t &typeAst) {
    if (typeAst == nullptr || typeAst->type() != AST::LoadType::Type ||
        typeAst->loadAs<AST::TypeLoad>()->typeType() != AST::TypeType::Array) {
        return nullptr;
    }
    const auto &groups = typeAst->loadAs<AST::ArrayTypeLoad>()->dims();
    if (groups.size() != 1) {
        return nullptr;
    }
    const AST::DimGroup &group = groups.front();
    // A pack is the trailing dimensions; a case pattern that states extents before it states a
    // shape, which is what an identity case selects.
    if (group.pack == AST::DimPack::None || !group.extents.empty() || group.packRepeated ||
        group.packItem + 1 != group.itemCount) {
        return nullptr;
    }
    return &group;
}

bool Builder::patternNameIsNew(
    const std::string &name, const std::vector<std::string> &bound) const {
    if (std::find(bound.begin(), bound.end(), name) != bound.end()) {
        return false;
    }
    if (typeScope_->get(Reference(name)).has_value()) {
        return false;
    }
    return module_ == nullptr || !module_->getImportedType(Reference(name)).has_value();
}

/*
MatchTypeType : Type subject, RepeatedLoad TypeCase ;
A `match type` in a type position (design principles 5.6): a type of its own while the subject
still has a type variable, the type of the case that selects the subject once it has not.
*/
Type *Builder::visitMatchTypeType(const AST::node_ptr_t &ast) {
    ENTER("MatchTypeType");
    Type *subject = visitType(ast->atAs<AST::TypeLoad>(0));
    std::vector<MatchTypeArm> arms;
    bool catchAll = false;
    for (const auto &armAst : *ast->atAs<AST::RepeatedLoad>(1)) {
        if (catchAll) {
            // `#leaf` and `_` select every type, so a case after them selects nothing.
            diags_->of(SemanticDiag::StaticMatchUnreachableCase)
                .at(armAst->load()->tokenRange())
                .commit("this case");
            throw BuildAbortException();
        }
        MatchTypeArm arm;
        const auto caseLoad = armAst->loadAs<AST::TypeCaseLoad>();
        // A case that writes the rest of a pack (`case (W, ...Rest)`) takes the subject pack apart,
        // and the names it writes stand for the members it reached (design principles 5.6: a type
        // pack takes apart the way a value pack does). A case that writes an array whose dims pack
        // it names (`case E[...Dims]`) or leaves anonymous (`case E[...]`) takes an array type
        // apart the same way: `E` is the element type and `Dims` the dimensions after it. They are
        // bound in this case alone -- in the patterns and in the type the case gives -- and a name
        // a type here already answers is that type, as a dimension name written once too often is a
        // reference and not a new name (5.4).
        const auto &patterns = caseLoad->patterns();
        // A guarded `_` or `#leaf` selects the types its predicate holds for, not every type, so a
        // case written after it can still be selected and is not reported as unreachable.
        const bool guarded = AST::typeCaseGuardOf(armAst) != nullptr;
        std::vector<std::string> written;
        std::vector<bool> takesArrayApart(patterns.size(), false);
        for (size_t i = 0; i < patterns.size(); ++i) {
            packPatternNamesOf(patterns[i].type, written);
            // The element name is decided here, before the scope holds it: a name that is already
            // a type here would make the case an identity pattern, which selects one type.
            const AST::DimGroup *dims = arrayPatternDimsOf(patterns[i].type);
            const std::string element =
                dims != nullptr ? writtenNameOf(patterns[i].type->at(0)) : std::string();
            if (dims != nullptr && !element.empty() && patternNameIsNew(element, written)) {
                written.push_back(element);
                takesArrayApart[i] = true;
            }
        }
        // The dimensions a case binds are the case's own: the pattern binds the pack and the type
        // the case gives may put it back (`E[?, ...Dims]`) or ask how many there are (`int[Dims]`),
        // and nothing outside the case sees either.
        struct CaseDimsScope {
            Builder &self;
            std::shared_ptr<std::set<std::string>> packs;
            bool bind;
            bool entered = false;
            ~CaseDimsScope() {
                if (entered) {
                    self.dimPacks_       = std::move(packs);
                    self.dimBindAllowed_ = bind;
                }
            }
        } caseDimsScope{*this, dimPacks_, dimBindAllowed_};
        struct PatternScope {
            Builder &self;
            scope_ptr_t<Reference, Type *> outer;
            bool entered = false;
            ~PatternScope() {
                if (entered) {
                    self.typeScope_ = std::move(outer);
                }
            }
        } patternScope{*this, typeScope_};
        if (!written.empty()) {
            pushScope();
            patternScope.entered = true;
            for (const std::string &name : written) {
                typeScope_->insert(Reference(name), TypeVarType::create(name, false));
            }
        }
        for (size_t i = 0; i < patterns.size(); ++i) {
            const AST::TypeCasePattern &pattern = patterns[i];
            const GCT::TypeCaseKind kind        = static_cast<GCT::TypeCaseKind>(pattern.kind);
            if (kind == GCT::TypeCaseKind::Identity) {
                if (isPackPattern(pattern.type)) {
                    arm.packPatterns.push_back(visitType(pattern.type));
                } else if (takesArrayApart[i]) {
                    // The pack name is this case's to bind; the dims it stands for are read from
                    // the subject when the case selects it (`takeArrayApart`).
                    caseDimsScope.entered = true;
                    dimPacks_          = dimPacks_ != nullptr
                                             ? std::make_shared<std::set<std::string>>(*dimPacks_)
                                             : std::make_shared<std::set<std::string>>();
                    const bool binding = std::exchange(dimBindAllowed_, true);
                    arm.arrayPatterns.push_back(visitType(pattern.type));
                    dimBindAllowed_ = binding;
                } else if (Type *identity = visitType(pattern.type)) {
                    arm.identities.push_back(identity);
                }
                continue;
            }
            arm.shapes |= typeCaseBit(kind);
            if (!guarded &&
                (kind == GCT::TypeCaseKind::Leaf || kind == GCT::TypeCaseKind::Wildcard)) {
                catchAll = true;
            }
        }
        arm.result = visitType(armAst->atAs<AST::TypeLoad>(0));
        if (const AST::node_ptr_t guardAst = AST::typeCaseGuardOf(armAst)) {
            // The guard stays in Camel: it is built as the data expression it is, with the type
            // arguments of the enclosing declaration in scope, and nothing of it is decided here.
            // The build that can run a function asks it for an answer (typevar.h
            // `typeCaseGuardEvaluator`), once per function and set of type arguments.
            auto guard       = std::make_shared<camel::core::type::TypeCaseGuard>();
            guard->condition = visitData(guardAst);
            guard->context   = module_.get();
            // The condition as the declaration wrote it, not the code a node generates: a type
            // that keeps the case prints the guard back (`MatchTypeType::toString`), and the
            // reference the compiler publishes compares that text with the source.
            guard->text = armAst->loadAs<AST::TypeCaseLoad>()->guardText();
            // What the type layer identifies the guard by: this module and that text
            // (`typeCaseGuardKey`). The two handles above are the build's, not the case's.
            guard->module = module_ != nullptr ? module_->name() : std::string();
            arm.guard     = std::move(guard);
        }
        arms.push_back(std::move(arm));
    }
    LEAVE("MatchTypeType");
    return MatchTypeType::create(subject, arms);
}

/*
SpecType : Type base, RepeatedLoad typeOrData ;
*/
Type *Builder::visitSpecType(const AST::node_ptr_t &ast) {
    ENTER("SpecType");
    GUARD(ast->type() == AST::LoadType::Type, "Expected TypeLoad type for SpecType");
    GUARD(ast->size() >= 1, "SpecType must have base type");
    typeFunctionBase_ = ast->size() >= 2;
    Type *base        = visitType(ast->atAs<AST::TypeLoad>(0));
    if (ast->size() < 2) {
        LEAVE("SpecType");
        return base;
    }
    // The arguments in source order: types, and constants written `const <data>`. What an
    // argument means is the type's business (`Type::applyArguments`); an argument the type does
    // not accept is an error here, never dropped.
    std::vector<TypeArgument> arguments;
    const auto &argsNode = ast->atAs<AST::RepeatedLoad>(1);
    for (const auto &child : *argsNode) {
        if (child->type() == AST::LoadType::Type &&
            child->loadAs<AST::TypeLoad>()->typeType() == AST::TypeType::ArrayDim) {
            // A shape argument: the extents stay compile-time forms over the names the
            // signature binds, not a runtime array constant (plan P3, 4.1).
            const AST::DimGroup &group = child->loadAs<AST::ArrayDimLoad>()->dim();
            if (group.pack != AST::DimPack::None) {
                // `f32[..s]` would make the element type's rank open; only an array
                // type takes a pack, and a shape argument is not a type.
                diags_->of(SemanticDiag::InvalidTypeArgument)
                    .at(child->load()->tokenRange())
                    .commit(base->toString(), "a shape argument has no trailing pack (`...`)");
                throw BuildAbortException();
            }
            std::shared_ptr<const std::vector<camel::core::type::ExtentForm>> extents =
                visitExtents(group, child);
            if (!extents) {
                throw BuildAbortException();
            }
            arguments.push_back(
                TypeArgument{.type = nullptr, .data = nullptr, .extents = std::move(extents)});
            continue;
        }
        if (child->type() == AST::LoadType::Type) {
            arguments.push_back(TypeArgument{.type = visitType(child), .data = nullptr});
            continue;
        }
        const data_ptr_t data = extractStaticDataFromNode(visitData(child));
        if (!data) {
            diags_->of(SemanticDiag::InvalidTypeArgument)
                .at(child->load()->tokenRange())
                .commit(base->toString(), "a 'const' argument must be a compile-time constant");
            throw BuildAbortException();
        }
        arguments.push_back(TypeArgument{.type = nullptr, .data = data});
    }
    if (base->code() == TypeCode::TypeFunc) {
        // A type declared with parameters: the arguments replace them in its body.
        const auto *typeFunc = static_cast<TypeFunctionType *>(base);
        type_vec_t types;
        for (const TypeArgument &argument : arguments) {
            if (argument.type == nullptr) {
                diags_->of(SemanticDiag::TypeFunctionArguments)
                    .at(ast->load()->tokenRange())
                    .commit(
                        typeFunc->toString(),
                        "its parameters are types, and a constant ('const ...') was given");
                throw BuildAbortException();
            }
            types.push_back(argument.type);
        }
        if (types.size() != typeFunc->params().size()) {
            diags_->of(SemanticDiag::TypeFunctionArguments)
                .at(ast->load()->tokenRange())
                .commit(
                    typeFunc->toString(),
                    std::format(
                        "it takes {} type argument(s) and {} were given",
                        typeFunc->params().size(),
                        types.size()));
            throw BuildAbortException();
        }
        Type *applied = nullptr;
        if (std::any_of(types.begin(), types.end(), hasTypeOfPlaceholder)) {
            // Applying the declaration now would put an unresolved `typeof` inside an ordinary
            // value type (a named struct, an array, a tuple, ...). Consumers of such a type quite
            // correctly use it before the graph builder's type-expression resolution: a struct
            // literal would check its fields against the placeholder. Keep the declaration and
            // its arguments as a type application instead; `resolveTypeNode` evaluates it after
            // each `typeof` has become the expression's actual type.
            applied =
                TypeCallType::makeTypeFunctionCall(const_cast<TypeFunctionType *>(typeFunc), types);
        } else {
            applied = typeFunc->apply(types);
        }
        // An argument that is still a `typeof` is not concrete yet -- to the type layer it is a
        // type variable -- and `typeof e` is a type expression that the
        // graph builder resolves where the type is used, without evaluating `e`. Reporting "not
        // applicable" here would refuse the application before anything had a chance to give the
        // `typeof` its type.
        const bool concrete = std::none_of(types.begin(), types.end(), [](Type *argument) {
            return containsTypeVar(argument);
        });
        // A pack is what `ParamsOf<F>` computes, and the `..` around it is read by the type that
        // holds it: it is reported where it is used bare, not here.
        // A `match type` whose case a guard decides is not a refusal either: the predicate is a
        // Camel function, and this phase runs none (it has no build behind it), so the match waits
        // and the graph builder settles it once the instance's type arguments are bound
        // (typevar.h `typeCaseGuardEvaluator`).
        if (concrete && containsTypeCall(applied) && !TypeCallType::isPack(applied) &&
            !containsGuardedTypeMatch(applied)) {
            // The arguments are concrete and the type function does not accept them.
            std::string what = applied->toString();
            std::string why  = "it is not defined for the type it is given";
            describeUnevaluatedTypeCall(applied, what, why);
            diags_->of(SemanticDiag::TypeFunctionNotApplicable)
                .at(ast->load()->tokenRange())
                .commit(what, why);
            throw BuildAbortException();
        }
        LEAVE("SpecType");
        return applied;
    }
    if (arguments.empty()) {
        LEAVE("SpecType");
        return base;
    }
    if (const CollectionType *coll = asCollectionTypeOrPrototype(base);
        coll != nullptr && !arguments.empty() && arguments.front().type != nullptr) {
        Type *keyType = arguments.front().type;
        // A stream is consumed once and the element of a set, like the key of a map, is read
        // more than once: the linearity refusal says so before the key rules look at what the
        // type is otherwise (design principles 5.4).
        if (keyType->code() == TypeCode::Stream) {
            diags_->of(SemanticDiag::StreamInContainer)
                .at(ast->load()->tokenRange())
                .commit(keyType->toString(), coll->isMap() ? "a map" : "a set");
            throw BuildAbortException();
        }
        // The key of a map, the element of a set (DESIGN-QUESTION(map-key-types)).
        camel::source::origin_id_t fieldOrigin = camel::source::kInvalidOriginId;
        std::string fieldLabel;
        if (std::string problem = collectionKeyProblem(keyType, &fieldOrigin, &fieldLabel);
            !problem.empty()) {
            auto diag =
                diags_->of(SemanticDiag::CollectionKeyNotAllowed).at(ast->load()->tokenRange());
            if (fieldOrigin != camel::source::kInvalidOriginId) {
                // The enum is a key for what its fields are: say where the field that is not
                // one is declared.
                diag.relatedOrigin(fieldOrigin, fieldLabel + " is declared here");
            }
            diag.commit(keyType->toString(), coll->toString(), problem);
            throw BuildAbortException();
        }
    }
    std::string error;
    // What the arguments denote is the type's answer (`Type::applyArguments`): the core array
    // type, a module type and a type that takes no arguments are all asked the same way, so a
    // type the core gains does not need a second path here.
    const Type *res = base->applyArguments(arguments, error);
    if (res == nullptr) {
        diags_->of(SemanticDiag::InvalidTypeArgument)
            .at(ast->load()->tokenRange())
            .commit(base->toString(), error.empty() ? std::string("no reason given") : error);
        throw BuildAbortException();
    }
    // A stream is consumed once, and the elements of a map or a set are read more than once
    // (design principles 5.4). The element of a set and the
    // key of a map are refused by the key rules already ('values of this type have no order');
    // this is the value of a map, which no key rule looks at.
    if (const CollectionType *coll = asCollectionTypeOrPrototype(res); coll != nullptr) {
        if (Type *value = coll->valueType();
            value != nullptr && value->code() == TypeCode::Stream) {
            diags_->of(SemanticDiag::StreamInContainer)
                .at(ast->load()->tokenRange())
                .commit(value->toString(), "a map");
            throw BuildAbortException();
        }
    }
    LEAVE("SpecType");
    return const_cast<Type *>(res);
}

namespace {

using camel::core::type::ExtentForm;

/// `form * factor`: a constant multiple keeps the extent linear.
ExtentForm scaleExtent(const ExtentForm &form, int64_t factor) {
    if (form.isUnknown()) {
        return form;
    }
    std::vector<std::pair<std::string, int64_t>> terms;
    for (const auto &[name, coefficient] : form.terms()) {
        terms.emplace_back(name, coefficient * factor);
    }
    return ExtentForm::linear(form.constant() * factor, std::move(terms));
}

/// `a + sign * b`.
ExtentForm addExtents(const ExtentForm &a, const ExtentForm &b, int64_t sign) {
    if (a.isUnknown() || b.isUnknown()) {
        return ExtentForm::unknown();
    }
    std::vector<std::pair<std::string, int64_t>> terms = a.terms();
    for (const auto &[name, coefficient] : b.terms()) {
        terms.emplace_back(name, coefficient * sign);
    }
    return ExtentForm::linear(a.constant() + sign * b.constant(), std::move(terms));
}

/// The expression an extent stands for, so a non-linear operation can be built from linear ones.
std::shared_ptr<const camel::core::type::ExtentExpr> exprOfExtent(const ExtentForm &form) {
    using camel::core::type::ExtentExpr;
    if (form.isComputed()) {
        return form.expression();
    }
    std::shared_ptr<const ExtentExpr> sum;
    for (const auto &[name, coefficient] : form.terms()) {
        std::shared_ptr<const ExtentExpr> term = ExtentExpr::makeName(name);
        if (coefficient != 1) {
            term = ExtentExpr::make(
                ExtentExpr::Op::Mul,
                ExtentExpr::makeConstant(coefficient),
                std::move(term));
        }
        sum = sum ? ExtentExpr::make(ExtentExpr::Op::Add, std::move(sum), std::move(term))
                  : std::move(term);
    }
    if (!sum) {
        return ExtentExpr::makeConstant(form.constant());
    }
    if (form.constant() != 0) {
        sum = ExtentExpr::make(
            ExtentExpr::Op::Add,
            std::move(sum),
            ExtentExpr::makeConstant(form.constant()));
    }
    return sum;
}

/// `lhs op rhs` where the result is no linear form: an expression, folded when both sides are
/// constants.
ExtentForm
computedExtent(camel::core::type::ExtentExpr::Op op, const ExtentForm &lhs, const ExtentForm &rhs) {
    using camel::core::type::ExtentExpr;
    if (lhs.isUnknown() || rhs.isUnknown()) {
        return ExtentForm::unknown();
    }
    auto expr = ExtentExpr::make(op, exprOfExtent(lhs), exprOfExtent(rhs));
    if (lhs.isConstant() && rhs.isConstant()) {
        if (const auto value = expr->evaluate({})) {
            return ExtentForm::constant(*value);
        }
        return ExtentForm::unknown();
    }
    return ExtentForm::computed(std::move(expr));
}

} // namespace

std::shared_ptr<const std::vector<camel::core::type::ExtentForm>>
Builder::visitExtents(const AST::DimGroup &group, const AST::node_ptr_t &ast) {
    using camel::core::type::ExtentForm;
    if (group.pack == AST::DimPack::Named) {
        // A pack name lives in its own namespace: it may be reused, but it may not name a
        // dimension too (design principles 5.4).
        if (dimBindAllowed_) {
            if (dimNames_ != nullptr && dimNames_->contains(group.packName)) {
                diags_->of(SemanticDiag::DimensionNameAlreadyBound)
                    .at(ast->load()->tokenRange())
                    .commit(group.packName);
                return nullptr;
            }
            if (dimPacks_ != nullptr) {
                dimPacks_->insert(group.packName);
            }
        } else if (dimPacks_ == nullptr || !dimPacks_->contains(group.packName)) {
            diags_->of(SemanticDiag::DimensionNameNotBound)
                .at(ast->load()->tokenRange())
                .commit("..." + group.packName);
            return nullptr;
        }
    }
    auto extents = std::make_shared<std::vector<ExtentForm>>();
    extents->reserve(group.extents.size());
    for (const auto &item : group.extents) {
        if (!item) {
            extents->push_back(ExtentForm::unknown());
            continue;
        }
        extents->push_back(visitExtent(item));
    }
    if (group.extents.empty() && group.pack == AST::DimPack::None) {
        // `[]` is one unknown dimension (`T[]` = `T[?]`); only the pack makes an empty list
        // mean an open rank.
        extents->push_back(ExtentForm::unknown());
    }
    return extents;
}

camel::core::type::ExtentForm Builder::visitExtent(const AST::node_ptr_t &ast) {
    using camel::core::type::ExtentForm;
    const auto report = [&]() -> ExtentForm {
        diags_->of(SemanticDiag::InvalidExtentExpression).at(ast->load()->tokenRange()).commit();
        throw BuildAbortException();
    };
    if (ast->type() != AST::LoadType::Data) {
        return report();
    }
    const auto &load = ast->loadAs<AST::DataLoad>();
    switch (load->dataType()) {
    case AST::DataType::Literal: {
        const Literal &literal = ast->loadAs<AST::LiteralLoad>()->value();
        if (literal.type() != LiteralType::Integer) {
            return report();
        }
        const IntegerLiteral parsed = parseIntegerLiteral(literal.data());
        if (parsed.status != IntegerLiteralStatus::Ok) {
            return report();
        }
        return ExtentForm::constant(parsed.value);
    }
    case AST::DataType::ConstAs:
        // `constas` is not written in a shape; the token is a syntax error there.
        return report();
    case AST::DataType::Ref: {
        const Reference &ref = ast->loadAs<AST::RefDataLoad>()->ref();
        if (!ref.plain()) {
            diags_->of(SemanticDiag::DimensionNameNotBound)
                .at(ast->load()->tokenRange())
                .commit(ref.toString());
            throw BuildAbortException();
        }
        const std::string name = ref.ident();
        if (dimNames_ != nullptr && dimNames_->contains(name)) {
            // A use in the return type counts: the name relates the parameter that binds it to
            // the result (`f32[n] -> f32[n + 1]`).
            if (dimUses_ != nullptr) {
                ++(*dimUses_)[name];
            }
            return ExtentForm::linear({{name, 1}});
        }
        if (dimPacks_ != nullptr && dimPacks_->contains(name)) {
            // A dimension pack written where an extent stands is how many dimensions it stands for
            // -- the count `indices` walks in the value layer, spelled with the name the type
            // already gave the pack: `int[rest]` is the array of the rank `rest` reaches, and a
            // `match type` case that binds `Dims` writes `int[Dims]` (design principles 5.4).
            return ExtentForm::linear({{name, 1}});
        }
        if (dimBindAllowed_ && dimNames_ != nullptr) {
            // The first plain name in a parameter type binds the dimension; the ones after it
            // in the same signature write the same extent (design principles 5.4).
            dimNames_->insert(name);
            if (boundDims_ != nullptr) {
                boundDims_->insert(name);
            }
            if (dimUses_ != nullptr) {
                ++(*dimUses_)[name];
            }
            return ExtentForm::linear({{name, 1}});
        }
        diags_->of(SemanticDiag::DimensionNameNotBound)
            .at(ast->load()->tokenRange())
            .commit(ref.toString());
        throw BuildAbortException();
    }
    case AST::DataType::ReservedExpr: {
        // A call of a function in an extent: `f32[conv_out(n, k, s, p)]`. The function is run at
        // build time when every name its arguments use is bound at a call (the pure functions
        // the build-time evaluation folds); until then the extent is unknown.
        if (ast->loadAs<AST::ReservedExprLoad>()->op() != AST::ReservedDataOp::Call) {
            return report();
        }
        std::vector<std::string> names;
        for (const auto &argument : *ast->atAs<AST::RepeatedLoad>(1)) {
            const ExtentForm form = visitExtent(argument);
            if (form.isUnknown()) {
                return ExtentForm::unknown();
            }
            for (const std::string &name : form.usedNames()) {
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
        node_ptr_t call = visitData(ast);
        return ExtentForm::computed(
            camel::core::type::ExtentExpr::makeCall(
                std::shared_ptr<const void>(call, [](const void *) {}),
                std::move(names),
                ast->load()->geneCode()));
    }
    case AST::DataType::UnaryExpr: {
        const auto &unary = ast->loadAs<AST::UnaryExprLoad>();
        if (unary->op() != AST::UnaryDataOp::Neg) {
            return ExtentForm::unknown();
        }
        const ExtentForm inner = visitExtent(ast->at(0));
        if (inner.isComputed()) {
            return ExtentForm::computed(
                camel::core::type::ExtentExpr::make(
                    camel::core::type::ExtentExpr::Op::Neg,
                    inner.expression()));
        }
        return scaleExtent(inner, -1);
    }
    case AST::DataType::BinaryExpr: {
        const auto &binary   = ast->loadAs<AST::BinaryExprLoad>();
        const ExtentForm lhs = visitExtent(ast->at(0));
        const ExtentForm rhs = visitExtent(ast->at(1));
        using Op             = camel::core::type::ExtentExpr::Op;
        switch (binary->op()) {
        case AST::BinaryDataOp::Add:
            return lhs.isComputed() || rhs.isComputed() ? computedExtent(Op::Add, lhs, rhs)
                                                        : addExtents(lhs, rhs, 1);
        case AST::BinaryDataOp::Sub:
            return lhs.isComputed() || rhs.isComputed() ? computedExtent(Op::Sub, lhs, rhs)
                                                        : addExtents(lhs, rhs, -1);
        case AST::BinaryDataOp::Mul:
            // A constant multiple keeps the extent linear; the product of two named dimensions is
            // an expression, evaluated once its names are bound (design principles 5.4).
            if (!lhs.isComputed() && !rhs.isComputed()) {
                if (lhs.isConstant() && !rhs.isUnknown()) {
                    return scaleExtent(rhs, lhs.constant());
                }
                if (rhs.isConstant() && !lhs.isUnknown()) {
                    return scaleExtent(lhs, rhs.constant());
                }
            }
            return computedExtent(Op::Mul, lhs, rhs);
        case AST::BinaryDataOp::Div:
            return computedExtent(Op::Div, lhs, rhs);
        case AST::BinaryDataOp::Mod:
            return computedExtent(Op::Mod, lhs, rhs);
        default:
            return ExtentForm::unknown();
        }
    }
    default:
        return report();
    }
}

/*
RefType(Ref ref) ;
*/
Type *Builder::visitRefType(const AST::node_ptr_t &ast) {
    ENTER("RefType");
    // Ensure the AST node is of the expected type (Type)
    GUARD(ast->load()->type() == AST::LoadType::Type, "Expected TypeLoad type for RefType");

    // Extract the reference type load from the AST node
    auto const &typeLoad = ast->loadAs<AST::RefTypeLoad>();
    const Reference &ref = typeLoad->ref();

    if (signatureVisit_ && !declaredTypeVars_.empty() &&
        std::find(declaredTypeVars_.begin(), declaredTypeVars_.end(), ref.ident()) !=
            declaredTypeVars_.end()) {
        writtenHeadVars_.insert(ref.ident());
    }
    Type *type = lookupType(ref, ast);
    LEAVE("RefType");
    return type;
}

bool Builder::usableNamespace(const Reference &ref) const {
    std::string written = ref.toString();
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0;) {
        std::string candidate;
        for (size_t i = 0; i < depth; ++i) {
            candidate += namespacePath_[i] + "::";
        }
        if (ownNamespaces_.contains(candidate + written)) {
            return true;
        }
    }
    return module_->hasImportedPrefix(ref);
}

void Builder::rejectTypeCycle(
    const std::shared_ptr<camel::core::module::Module> &source, const Reference &remote,
    const Reference &ref, const AST::node_ptr_t &ast) {
    const auto key = std::make_pair(
        static_cast<const camel::core::module::Module *>(source.get()),
        remote.toString());
    if (context_->onReportedTypeCycle(key.first, key.second)) {
        throw BuildAbortException(); // reported where the cycle closed
    }
    const auto &inProgress = context_->typeDeclarationsInProgress();
    const auto start       = std::ranges::find(inProgress, key);
    if (start == inProgress.end()) {
        return; // not on a cycle: the name is reported as not resolved
    }
    // From the declaration that is needed again to the one that needs it, then that one again:
    // `a::A -> b::B -> a::A`.
    const auto shown = [](const std::pair<const camel::core::module::Module *, std::string> &d) {
        return d.first->name() + "::" + d.second;
    };
    std::string chain;
    for (auto it = start; it != inProgress.end(); ++it) {
        chain += shown(*it) + " -> ";
        context_->markTypeCycle(it->first, it->second);
    }
    chain += shown(key);
    diags_->of(SemanticDiag::ModuleTypeCycle)
        .at(ast->load()->tokenRange())
        .commit(ref.toString(), chain);
    throw BuildAbortException();
}

Type *Builder::lookupType(const Reference &ref, const AST::node_ptr_t &ast) {
    const std::string written = ref.toString();
    // A declaration that failed declares nothing (visitTypeDecl): a use of its name follows from
    // that error, and is not built from the half-built type the declaration left where its own
    // fields were resolved. The scopes are asked after this, because a name is in its own scope
    // while its fields are read (a struct that holds itself through an array needs that).
    if (poisonedTypes_.contains(written)) {
        throw BuildAbortException();
    }
    // The module's own name may qualify its names (design principles 10.2).
    std::string own = written;
    if (const std::string &self = module_->name(); !self.empty() && own.starts_with(self + "::")) {
        own = own.substr(self.size() + 2);
    }
    // Layer 1 and 2: the types of the enclosing scopes and of the module, from the innermost
    // namespace out. The built-in type names (`int`, `Map`, ...) are at the top of the scopes.
    std::optional<Type *> found;
    std::optional<Reference> foundRef;
    for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && !found;) {
        std::vector<std::string> parts(namespacePath_.begin(), namespacePath_.begin() + depth);
        const Reference candidate(own);
        parts.insert(parts.end(), candidate.paths().begin(), candidate.paths().end());
        parts.push_back(candidate.ident());
        Reference lookup(parts);
        found = typeScope_->get(lookup);
        if (found) {
            foundRef = std::move(lookup);
        }
    }
    // What the builders report about a use of a type name: the type it denotes and where that
    // type is declared, when the declaration is in the sources (not for a built-in name or a
    // type variable). The language server reads this instead of resolving type names itself.
    const auto report = [&](Type *type, origin_id_t decl) -> Type * {
        if (auto *recorder = context_ ? context_->semanticRecorder() : nullptr) {
            recorder->typeReferenced(ast->load()->origin(), ref.ident(), decl, type);
        }
        return type;
    };
    // Where the module a name is imported from declared it.
    const auto importedOrigin = [&](const Reference &name) -> origin_id_t {
        if (const auto source = module_->importedSource(name, ImportLayer::Named)) {
            return source->first->typeDeclarationOrigin(source->second);
        }
        return 0;
    };
    if (found) {
        // A use of a name the declaration declares at its head is a binding occurrence: an
        // argument at that position can give the variable its type (design principles 5.5). The
        // scope entry is the declaration's own occurrence, so a name of an enclosing declaration
        // (or a static-for's dimension) is still a plain use.
        if (found.value()->code() == TypeCode::TypeVar && headTypeVarTypes_) {
            const auto &name = tt::as_ptr<TypeVarType>(found.value())->name();
            const auto head  = headTypeVarTypes_->find(name);
            if (head != headTypeVarTypes_->end() && head->second == found.value()) {
                return report(TypeVarType::create(name, true), camel::source::kInvalidOriginId);
            }
        }
        // An imported type named `Map` or `Set` comes before the built-in of that name.
        if (isCollectionPrototype(found.value())) {
            if (const auto imported = module_->getImportedType(ref); imported.has_value()) {
                return report(imported.value(), importedOrigin(ref));
            }
        }
        return report(found.value(), module_->typeDeclarationOrigin(*foundRef));
    }
    // The types the module itself exports without a type declaration in this file: a `.cml`
    // beside a `.cmo` is that C++ module's declaration module, so what the C++ module exports
    // is directly visible here, as if it were written at the top of this file (design
    // principles 6.4, rule 1).
    if (const auto exported = module_->getExportedType(Reference(own)); exported.has_value()) {
        return exported.value();
    }
    // Inside a headed block, the head's module is where a handle type comes from and the only place
    // it comes from (graph-editing design 15.1): the types that module exports are readable by
    // bare name in the body the head stands for, and the same names outside such a body stay
    // unresolved, which is what says the block's head, not the program, is their source.
    for (auto head = blockHeadModules_.rbegin(); head != blockHeadModules_.rend(); ++head) {
        if (const auto exported = (*head)->getExportedType(ref); exported.has_value()) {
            return exported.value();
        }
    }
    // Layer 3: imported by name, or through a module or namespace prefix.
    if (const auto imported = module_->getImportedType(ref, ImportLayer::Named)) {
        return report(imported.value(), importedOrigin(ref));
    }
    if (const auto source = module_->importedSource(ref, ImportLayer::Named)) {
        // Imported, and not declared (yet): perhaps the types form a cycle across modules.
        rejectTypeCycle(source->first, source->second, ref, ast);
    }
    // Layer 4: through a `using namespace` in force; two that give different types are an
    // error where the name is used.
    struct UsedType {
        std::string shown;
        Type *type;
        origin_id_t origin;
    };
    std::vector<UsedType> viaUsing;
    for (const auto &frame : usingFrames_) {
        for (const Reference &using_ : frame) {
            const std::string prefixed = using_.toString() + "::" + written;
            Type *type                 = nullptr;
            origin_id_t origin         = 0;
            // The full name of the candidate, for AmbiguousName (design principles 10.2).
            std::string shown = prefixed;
            for (size_t depth = namespacePath_.size() + 1; depth-- > 0 && type == nullptr;) {
                std::string candidate;
                for (size_t i = 0; i < depth; ++i) {
                    candidate += namespacePath_[i] + "::";
                }
                if (auto own = typeScope_->get(Reference(candidate + prefixed))) {
                    type   = own.value();
                    origin = module_->typeDeclarationOrigin(Reference(candidate + prefixed));
                    shown  = module_->qualifiedName(Reference(candidate + prefixed));
                }
            }
            if (type == nullptr) {
                if (auto imported =
                        module_->getImportedType(Reference(prefixed), ImportLayer::Named)) {
                    type   = imported.value();
                    origin = importedOrigin(Reference(prefixed));
                    if (const auto source =
                            module_->importedSource(Reference(prefixed), ImportLayer::Named)) {
                        shown = source->first->qualifiedName(source->second);
                    }
                }
            }
            if (type != nullptr && std::ranges::none_of(viaUsing, [&](const auto &have) {
                    return have.type == type;
                })) {
                viaUsing.push_back(UsedType{shown, type, origin});
            }
        }
    }
    if (viaUsing.size() == 1) {
        return report(viaUsing.front().type, viaUsing.front().origin);
    }
    if (viaUsing.size() > 1) {
        std::string sources;
        for (const auto &[name, type, origin] : viaUsing) {
            (void)type;
            (void)origin;
            sources += (sources.empty() ? "" : ", ") + name;
        }
        diags_->of(SemanticDiag::AmbiguousName)
            .at(ast->load()->tokenRange())
            .commit(written, sources);
        throw BuildAbortException();
    }
    // Layer 5: what the built-in module and the modules of operators bring along, and what the
    // prelude exports with them (the prelude is the built-in module's declaration file, so a
    // name of it needs no import -- design principles 10.2). The prelude's own names are its own
    // layer where it is written, so this asks only for the modules that read it.
    if (const auto ambient = module_->getImportedType(ref, ImportLayer::Ambient)) {
        return report(ambient.value(), 0);
    }
    if (context_ != nullptr && context_->preludeModule() != nullptr &&
        context_->preludeModule().get() != module_.get()) {
        if (const auto fromPrelude =
                context_->preludeModule()->getExportedType(Reference(written))) {
            return report(fromPrelude.value(), 0);
        }
    }
    if (wholeImportFailed_ || poisonedTypes_.contains(written)) {
        throw BuildAbortException();
    }
    // A plain name that answers no type is an unknown type. A declaration's type variables are
    // declared at its head, never inferred from a parameter type (design principles 5.5); a
    // dimension name still binds where a shape first writes it (5.4), which is a separate pass.
    diags_->of(SemanticDiag::UnresolvedTypeReference).at(ast->load()->tokenRange()).commit(written);
    throw BuildAbortException();
}

} // namespace camel::compile::gct