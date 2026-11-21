// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The declarations the GCT builder's translation units share: the source-origin bookkeeping
// every one of them writes through, and the literal helpers more than one of them calls.

#pragma once

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

// The helpers below were written where these namespaces are open, and they name their types
// unqualified. Only the GCT builder's own translation units include this header, and each of them
// opens the same namespaces for the same reason, so this repeats their context rather than adding
// one: a unit that includes this header reads exactly what it read before the helpers moved.
using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gct {

inline camel::source::origin_id_t nodeOrigin(const node_ptr_t &node) {
    return node ? node->origin() : camel::source::kInvalidOriginId;
}

/// Whether the type variable `name` occurs in a type written below `node`. A head parameter a
/// declaration writes only in its body (`match type T`, a cast to `T`) is used by the body even
/// though the signature never names it, and is not an unused one.
inline bool usesTypeVar(const node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return false;
    }
    const auto inType = [&](Type *type) {
        if (type == nullptr) {
            return false;
        }
        std::vector<std::string> names;
        collectTypeVarsWithoutPlaceholders(type, names);
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    switch (node->type()) {
    case NodeKind::TYPE:
        if (inType(node->dataType())) {
            return true;
        }
        break;
    case NodeKind::DATA:
        if (node->data() != nullptr && inType(node->data()->type())) {
            return true;
        }
        break;
    case NodeKind::CAST:
        if (inType(node->targetType())) {
            return true;
        }
        break;
    case NodeKind::TCASE:
        // An identity pattern names its type in the payload, not as a child node, so the walk
        // below does not reach it.
        for (size_t i = 0; i < node->typeCaseTypeCount(); ++i) {
            if (inType(node->typeCaseTypeAt(i))) {
                return true;
            }
        }
        break;
    default:
        break;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (usesTypeVar(node->at(i), name)) {
            return true;
        }
    }
    return false;
}

inline camel::source::SemanticPart semanticPart(
    camel::source::SemanticRole role, camel::source::origin_id_t origin, int32_t slot = -1,
    const std::string &label = "") {
    return camel::source::SemanticPart{
        .role   = role,
        .origin = origin,
        .slot   = slot,
        .label  = label};
}

inline std::vector<camel::source::origin_id_t> collectChildOrigins(const node_ptr_t &node) {
    std::vector<camel::source::origin_id_t> origins;
    if (!node) {
        return origins;
    }
    for (const auto &child : *node) {
        auto origin = nodeOrigin(child);
        if (origin != camel::source::kInvalidOriginId) {
            origins.push_back(origin);
        }
    }
    return origins;
}

inline const camel::source::SemanticBundle *
astSemantic(const context_ptr_t &context, const AST::node_ptr_t &ast) {
    if (!context || !ast) {
        return nullptr;
    }
    auto sourceContext = context->sourceContext();
    return sourceContext ? sourceContext->astSemantic(ast->load()->origin()) : nullptr;
}

inline camel::source::origin_id_t findSemanticOrigin(
    const camel::source::SemanticBundle *bundle, camel::source::SemanticRole role,
    int32_t slot = -1) {
    if (!bundle) {
        return camel::source::kInvalidOriginId;
    }
    for (const auto &part : bundle->parts) {
        if (part.role == role && (slot < 0 || part.slot == slot)) {
            return part.origin;
        }
    }
    return camel::source::kInvalidOriginId;
}

inline camel::source::origin_id_t
findBindingOrigin(const context_ptr_t &context, const AST::node_ptr_t &ast, size_t slot) {
    return findSemanticOrigin(
        astSemantic(context, ast),
        camel::source::SemanticRole::BindingName,
        static_cast<int32_t>(slot));
}

inline void setOriginFromOrigin(
    const context_ptr_t &context, const node_ptr_t &node, camel::source::origin_id_t parent,
    camel::source::OriginKind kind = camel::source::OriginKind::GctNode,
    const std::string &label = "gct", bool synthetic = false,
    std::vector<camel::source::origin_id_t> inputs = {}) {
    if (!context || !node || parent == camel::source::kInvalidOriginId) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    node->setOrigin(sourceContext->deriveOrigin(
        parent,
        camel::source::OriginStage::GCT,
        kind,
        label,
        camel::source::kInvalidSpanId,
        synthetic,
        std::move(inputs)));
}

/// The parts a node already carries, so that a node visited in more than one place keeps them when
/// the place it stands at follows the AST node it was lowered from.
struct SemanticParts {
    std::vector<camel::source::SemanticPart> parts;
    std::vector<camel::source::origin_id_t> inputs;
};

inline SemanticParts ownSemanticParts(const context_ptr_t &context, const node_ptr_t &node) {
    SemanticParts own;
    if (!context || !node || node->origin() == camel::source::kInvalidOriginId) {
        return own;
    }
    auto sourceContext = context->sourceContext();
    const auto *bundle = sourceContext ? sourceContext->gctSemantic(node->origin()) : nullptr;
    if (bundle == nullptr) {
        return own;
    }
    own.parts  = bundle->parts;
    own.inputs = bundle->mergedInputs;
    return own;
}

inline void setOriginFromAst(
    const context_ptr_t &context, const node_ptr_t &node, const AST::node_ptr_t &ast,
    camel::source::OriginKind kind = camel::source::OriginKind::GctNode,
    const std::string &label = "gct", bool synthetic = false,
    std::vector<camel::source::origin_id_t> inputs = {}) {
    // GCT is a lossy lowering of AST:
    // an AST node may be rewritten into a different semantic shape, but we still inherit the
    // AST primarySpan along the main derivation chain so diagnostics and later GIR/debugger paths
    // can recover the main source-responsibility span.
    if (!context || !node || !ast) {
        return;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return;
    }
    auto astOrigin = ast->load()->origin();
    if (astOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    node->setOrigin(sourceContext->deriveOrigin(
        astOrigin,
        camel::source::OriginStage::GCT,
        kind,
        label,
        camel::source::kInvalidSpanId,
        synthetic,
        std::move(inputs)));
}

inline void registerGctSemantic(
    const context_ptr_t &context, const node_ptr_t &node, const AST::node_ptr_t &ast,
    std::vector<camel::source::SemanticPart> extraParts  = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    if (!context || !node) {
        return;
    }
    auto sourceContext = context->sourceContext();
    auto mainOrigin    = nodeOrigin(node);
    if (!sourceContext || mainOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    std::vector<camel::source::SemanticPart> parts;
    if (const auto *bundle = astSemantic(context, ast)) {
        parts = bundle->parts;
        if (mergedInputs.empty()) {
            mergedInputs = bundle->mergedInputs;
        }
    }
    parts.insert(parts.end(), extraParts.begin(), extraParts.end());
    if (mergedInputs.empty()) {
        mergedInputs = collectChildOrigins(node);
    }
    sourceContext->registerGctSemantic(
        mainOrigin,
        camel::source::SemanticBundle{
            .mainOrigin      = mainOrigin,
            .parts           = std::move(parts),
            .mergedInputs    = std::move(mergedInputs),
            .synthetic       = synthetic,
            .syntheticReason = syntheticReason});
}

inline data_ptr_t extractStaticDataFromNode(const node_ptr_t &node) {
    if (node->type() == NodeKind::DATA) {
        return node->data();
    } else if (node->type() == NodeKind::DREF) {
        return make_shared<RefData>(node->name());
    } else {
        return nullptr;
    }
}

/// A tuple, array or struct literal that still names values (`(h, k)`): it is built when the
/// program runs, from what the names stand for, so it is not a constant element of a literal
/// that encloses it.
inline bool isUnresolvedLiteral(const data_ptr_t &data) {
    if (!data || !data->type()) {
        return false;
    }
    switch (data->type()->code()) {
    case TypeCode::Tuple:
    case TypeCode::Array:
    case TypeCode::Struct: {
        const auto composite = std::dynamic_pointer_cast<CompositeData>(data);
        return composite && !composite->resolved();
    }
    default:
        return false;
    }
}

/// Whether `type` is what the built-in names `Map` and `Set` resolve to.
inline bool isCollectionPrototype(Type *type) {
    const CollectionType *coll = asCollectionTypeOrPrototype(type);
    return coll != nullptr && !coll->complete();
}

inline bool validateIdent(const std::string &str) {
    // If the string length is less than 4, return true directly, indicating the identifier is valid
    if (str.length() < 4) {
        return true;
    }
    // Check if the string starts and ends with "__"
    // If true, return false indicating the identifier is invalid; otherwise, return true
    return !(str.substr(0, 2) == "__" && str.substr(str.length() - 2) == "__");
}

// Defined with the literal builder below; the subscript builder makes the constants a slice
// did not write (start 0, step 1) out of it.
template <typename T> inline data_ptr_t makeDataFromLiteral(const T &value);

// A primitive type whose values are integers (`i32`, `int`): a float literal is not converted
// to one by itself (design principles 5.1, "数值类型不做隐式转换").
inline bool isIntegerPrimitive(Type *type) {
    return type != nullptr && type->isPrimitive() &&
           (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64);
}

// A literal that could not be converted to the type its position asks for: out of the range
// of a narrower numeric type (`let b: i32 = 3000000000`), a decimal literal in an integer type
// (`let x: int = 1.9`), or a value of another type. One diagnostic says which literal, which
// type, and why.
//
// A text where a number is expected is not a range problem and not a text that is not a number
// either: there is no implicit text-to-number conversion (design principles 5.1), so it is a
// type mismatch, reported with the diagnostic the same position gives a variable of type
// `string` (DeclaredTypeMismatch in a declared position, DynamicCastForbidden for an explicit
// `as`).
//
// `written` is the element's text as it is in the source, when the caller can read it (design
// principles 5.1: the message quotes the source's spelling); empty falls back to the value.
// `explicitCast` says the literal stands under an `as` conversion rather than in a declared
// position.
template <typename Diags, typename Range>
inline void reportLiteralCastFailure(
    Diags &diags, const Range &range, const data_ptr_t &data, Type *type,
    const std::string &written = {}, bool explicitCast = false) {
    if (data->type()->code() == TypeCode::String || type->code() == TypeCode::String) {
        // Either way round (`'1.5'` where a float is expected, `42` where a text is expected):
        // a value and a text are not converted implicitly (design principles 5.1).
        if (explicitCast) {
            diags->of(SemanticDiag::DynamicCastForbidden)
                .at(range)
                .commit(data->type()->toString(), type->toString());
        } else {
            diags->of(SemanticDiag::DeclaredTypeMismatch)
                .at(range)
                .commit(data->type()->toString(), type->toString());
        }
        return;
    }
    std::string reason;
    if (const std::string notFit = literalNotFitReason(data, type); !notFit.empty()) {
        reason = notFit;
    } else if (data->type()->isPrimitive() && isIntegerPrimitive(type)) {
        // A numeric type that is not an integer keeps its fraction (`1.9` -> `int`).
        reason = "the value would be truncated";
    } else {
        reason = "the value cannot become that type";
    }
    diags->of(SemanticDiag::LiteralNotRepresentable)
        .at(range)
        .commit(written.empty() ? literalValueText(data) : written, type->toString(), reason);
}

template <typename T> inline data_ptr_t makeDataFromLiteral(const T &value) {
    return tt::as_shared<Data>(make_shared<PrimaryData<T>>(value));
}

/**
 * The value a generator's state slot of this type holds before anything is written into it.
 *
 * A generator body's every local is a cell with a type and no value yet (`:var/new/empty`): what it
 * holds is written where its declaration stands, which may be a later pull. The state the machine
 * hands from one pull to the next is a value, and a value has no "nothing written yet", so the slot
 * starts at this value instead. It is never observed: reading a local before its declaration is
 * `SemanticDiag::UnresolvedReference`, refused at compile time, in a plain body, inside an `if` arm
 * and in a generator body alike. The only read of an unwritten slot is the machine packing the state
 * at a `yield`, and what it packs is never read back before the declaration writes it.
 *
 * `nullptr` means the type has no such value, and the slot keeps its cell instead. Those types are
 * a function type (a closure needs a graph and a resource set, not a constant), `Ref` and a
 * `#resource` (their value is a box a call makes), `T!` and an enum (`int!`, `Option`: a member has
 * to be chosen, which is the program's decision and not the machine's), and a type variable. If one
 * of them is ever to get an "unwritten" form, this is the door: one place decides, and the state
 * tuple stops needing the cell.
 */
inline data_ptr_t zeroValueOf(Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    switch (type->code()) {
    case TypeCode::Int32:
        return makeDataFromLiteral<int32_t>(0);
    case TypeCode::Int64:
        return makeDataFromLiteral<int64_t>(0);
    case TypeCode::Float32:
        return makeDataFromLiteral<float>(0.0F);
    case TypeCode::Float64:
        return makeDataFromLiteral<double>(0.0);
    case TypeCode::Bool:
        return makeDataFromLiteral<bool>(false);
    case TypeCode::String:
        // A string is `StringData`, not `PrimaryData<std::string>`: the literal path builds it the
        // same way (`visitLiteral`).
        return tt::as_shared<Data>(make_shared<StringData>(std::string{}));
    case TypeCode::Tuple: {
        // A member without a zero makes the whole tuple one: half a value is not a value.
        const auto *tuple = tt::as_ptr<TupleType>(type);
        TupleDataFactory factory;
        for (size_t i = 0; i < tuple->size(); ++i) {
            const data_ptr_t member = zeroValueOf(tuple->typeAt(i));
            if (member == nullptr) {
                return nullptr;
            }
            factory.add(member);
        }
        return factory.build();
    }
    default:
        return nullptr;
    }
}

} // namespace camel::compile::gct