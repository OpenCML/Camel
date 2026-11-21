// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the lowering of data expressions -- literals, array/struct/tuple data, comprehensions, spreads, a name read as a value, and the typed lowering of a literal into the type its position declares.

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
Literal(Value value) ;
enum LiteralType {
    String,
    FString,
    Integer,
    Real,
    Boolean,
    Null,
};
*/
node_ptr_t Builder::visitLiteral(const AST::node_ptr_t &ast) {
    ENTER("Literal");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for Literal");

    // Extract the literal load and its value
    const auto &literal  = ast->loadAs<AST::LiteralLoad>();
    const Literal &value = literal->value();
    const auto &str      = value.data();
    data_ptr_t data;
    // The value the literal was written as, exactly (design principles 5.1). The type it
    // takes is decided by its context; this is what the context converts or folds.
    std::optional<literal::Value> exactValue;

    // Handle different literal types
    switch (value.type()) {
    case LiteralType::String: {
        // Decode escaped characters in the string and create a StringData object. A formatted
        // string's pattern is decoded with `doubleBraces`: a brace that an escape produces is a
        // literal brace of that pattern, and `format` must see it doubled.
        //
        // Every escape form is decoded here, so an unknown escape is reported here too, once per
        // spelling, in the lexical catalogue: an octal escape is simply unknown, and so is
        // `\q`. The text keeps the pair as written; the diagnostic is an error, so a program
        // that writes one does not compile (a string that
        // silently becomes something else is dearer than one that refuses to build).
        //
        // A raw string (`r"C:\dir"`) is not decoded at all: its text is the value, so a backslash
        // in it is a backslash -- there is no escape to be unknown, and the table is not run.
        std::vector<std::string> unknownEscapes;
        std::string decoded =
            literal->fromRawString()
                ? str
                : decodeEscapes(str, literal->fromFormattedString(), &unknownEscapes);
        for (const std::string &escape : unknownEscapes) {
            diags_->of(LexicalDiag::InvalidEscapeSequence)
                .at(ast->load()->tokenRange())
                .commit(escape);
        }
        // A string is always valid UTF-8 (design principles 11.1); `'\xE4'` alone is not.
        // Write the character itself or `\u{4E2D}`; a byte sequence is a `Bytes` value.
        if (!camel::utf8::isValid(decoded)) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("string (not valid UTF-8; use \\u{...} or the character itself)", str);
            throw BuildAbortException();
        }
        data = tt::as_shared<Data>(make_shared<StringData>(decoded));
    } break;
    case LiteralType::FString: {
        // Unreachable: the AST builder lowers every formatted string to the same
        // `'...'->format<...>` tree (design principles 11.5; AST Builder::visitFStringData),
        // and a pattern refuses one instead of producing a formatted-string literal. There is
        // therefore no source text that can put a FString literal in the GCT.
        GUARD(
            false,
            "A formatted-string literal reached the GCT: the AST builder no longer lowers "
            "every formatted string to a format call.");
        // A debug build aborts above; a release build reports the compiler defect instead of
        // blaming the program for a feature it never wrote.
        diags_->of(InternalDiag::UnknownInternalError)
            .at(ast->load()->tokenRange())
            .commit(
                "a formatted-string literal reached the GCT; the front end should have lowered "
                "it to a format call");
        throw BuildAbortException();
    } break;
    case LiteralType::Integer: {
        // Exact arithmetic. A literal that an int cannot hold does not wrap around
        // (as `9223372036854775808` or `1e400` would). The value is read by
        // the same whole-number-token rule an entry argument is (camel/utils/number_text.h):
        // one grammar, one parser for the two places a number is written as text.
        int64_t value = 0;
        switch (camel::utils::parseNumberToken<int64_t>(str, value)) {
        case camel::utils::NumberTextStatus::Ok:
            break;
        case camel::utils::NumberTextStatus::OutOfRange:
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(str, "int", "out of range (-9223372036854775808 to 9223372036854775807)");
            throw BuildAbortException();
        default:
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("integer", str);
            throw BuildAbortException();
        }
        data       = makeDataFromLiteral(value);
        exactValue = literal::Value::fromIntegerText(str);
    } break;
    case LiteralType::Real: {
        // The double nearest to the number written, read by the same whole-number-token rule an
        // entry argument is (camel/utils/number_text.h). One too large for a double was an
        // exception that reached the user as bare text.
        double value = 0.0;
        switch (camel::utils::parseNumberToken<double>(str, value)) {
        case camel::utils::NumberTextStatus::Ok:
            break;
        case camel::utils::NumberTextStatus::Underflow:
            // A nonzero number too small to tell from zero is a value the type cannot hold, not
            // zero (design principles 5.1): the literal is refused with that reason.
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(str, "float", "too small to be represented (the nearest value is zero)");
            throw BuildAbortException();
        case camel::utils::NumberTextStatus::OutOfRange:
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(str, "float", "out of range (at most about 1.8e308 in magnitude)");
            throw BuildAbortException();
        default:
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("real", str);
            throw BuildAbortException();
        }
        data       = makeDataFromLiteral(value);
        exactValue = literal::Value::fromRealText(str);
    } break;
    case LiteralType::Imaginary: {
        // `4i`: the value is imaginary, the type is the default c128 until the context says
        // otherwise (design principles 5.1).
        exactValue = literal::Value::fromImaginaryText(str);
        if (!exactValue.has_value()) {
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("imaginary number", str);
            throw BuildAbortException();
        }
        data = *exactValue->toData(Type::Complex128());
    } break;
    case LiteralType::Boolean: {
        // Handle boolean literals ("true" or "false")
        if (str == "true") {
            data = makeDataFromLiteral(true);
        } else if (str == "false") {
            data = makeDataFromLiteral(false);
        } else {
            // Invalid boolean literal
            diags_->of(SemanticDiag::InvalidLiteral)
                .at(ast->load()->tokenRange())
                .commit("boolean", str);
            throw BuildAbortException();
        }
    } break;
    case LiteralType::Null: {
        data = Data::null();
    } break;
    default: {
        throw std::runtime_error("Unknown literal type");
        throw BuildAbortException();
    }
    }

    // Create a DataLoad node to represent the literal and return it
    node_ptr_t res = tree_->makeData(data);
    if (exactValue.has_value()) {
        tree_->setLiteralValue(res, std::move(*exactValue));
    }
    if (value.type() == LiteralType::String && literal->fromFormattedString()) {
        // The pattern of a formatted string: a format specification that does not fit its
        // argument is reported at the interpolation (the GIR builder knows the types).
        res->setFromFormattedString();
    }
    LEAVE("Literal");
    return res;
}

/*
ArrayData() : Data* data ;
*/
/// The kind of the number literal written directly as an element of an array literal; nothing
/// for any other element (a name, a call, a nested literal, a string).
static std::optional<literal::Kind> writtenNumberKind(const AST::node_ptr_t &item) {
    if (item == nullptr || item->type() != AST::LoadType::Data) {
        return std::nullopt;
    }
    if (item->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Literal) {
        return std::nullopt;
    }
    switch (item->loadAs<AST::LiteralLoad>()->value().type()) {
    case LiteralType::Integer:
        return literal::Kind::Integer;
    case LiteralType::Real:
        return literal::Kind::Float;
    case LiteralType::Imaginary:
        return literal::Kind::Imaginary;
    default:
        return std::nullopt;
    }
}

/// An element of a composite literal as it was written: the value it was written as, when it
/// is a numeric literal (design principles 5.1), and the position it was written at, which is
/// where the element is reported when the type of its position cannot hold it. A nested
/// composite, a name and a computed element have no value of their own, but they have a
/// position, and so do the elements inside them.
static Tree::LiteralElement writtenElement(const node_ptr_t &node) {
    Tree::LiteralElement element;
    if (node == nullptr) {
        return element;
    }
    element.origin = node->origin();
    if (node->type() == NodeKind::DATA) {
        if (const literal::Value *exact = Tree::of(node)->literalValueOf(node)) {
            element.value = *exact;
        }
    }
    return element;
}

node_ptr_t Builder::visitArrayData(const AST::node_ptr_t &ast) {
    ENTER("ArrayData");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for ArrayData");
    if (ast->size() > 1) {
        node_ptr_t res = visitComprehension(ast);
        LEAVE("ArrayData");
        return res;
    }

    ArrayDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);

    // `[1, null, 3]`: a `null` among other elements makes the array one of `T?` (design
    // principles 3.8), whose elements are put into that form when the program runs. Which
    // `T` is known to the GIR builder only, so every element is left to it as a computed one
    // (visitDataNode joins their types and adds the CASTs).
    const auto &items   = ast->atAs<AST::RepeatedLoad>(0);
    bool hasNullLiteral = false;
    for (const auto &item : *items) {
        hasNullLiteral |= item->type() == AST::LoadType::Data &&
                          item->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Literal &&
                          item->loadAs<AST::LiteralLoad>()->value().type() == LiteralType::Null;
    }
    const bool allComputed = hasNullLiteral && items->size() > 1;

    // With no context to say which type they have, the number literals of one array take the
    // widest kind among them (design principles 5.1; as in Go): `[1, 2.0]` is a `float[]`.
    // The order of the kinds is integer < decimal < imaginary: a complex element widens every
    // other number to c128, the default complex type.
    bool hasInteger = false, hasReal = false, hasImaginary = false;
    for (const auto &item : *items) {
        if (const std::optional<literal::Kind> kind = writtenNumberKind(item); kind.has_value()) {
            hasInteger |= *kind == literal::Kind::Integer;
            hasReal |= *kind == literal::Kind::Float;
            hasImaginary |= *kind == literal::Kind::Imaginary;
        }
    }
    const bool widenToFloat = hasInteger && hasReal && !hasImaginary;
    // The values the elements were written as and their positions, in the order the data holds
    // them: the elements keep their exact values and their places until GIR decides the type
    // of the array (design principles 5.1).
    std::vector<Tree::LiteralElement> literalElements;
    literalElements.reserve(items->size());

    for (const auto &item : *items) {
        node_ptr_t dataNode = visitData(item);
        if (hasImaginary && writtenNumberKind(item) != literal::Kind::Imaginary) {
            // An integer or decimal literal beside an imaginary one is a complex value.
            dataNode = literalNodeInType(dataNode, Type::Complex128(), item);
        } else if (widenToFloat && writtenNumberKind(item) == literal::Kind::Integer) {
            // An integer literal beside a decimal one is a value of the float type, exactly.
            dataNode = literalNodeInType(dataNode, Type::Float(), item);
        }
        literalElements.push_back(writtenElement(dataNode));
        if (allComputed) {
            auto [refNode, refData] = makeRefData(dataNode);
            *execNode << refNode;
            dangling = true;
            factory.add(refData);
            continue;
        }
        auto [data, _] = extractData(dataNode, execNode, dangling);
        try {
            factory.add(data);
        } catch (Diagnostic &d) {
            // The element that does not fit the others: the data layer knows the types, the
            // position is known here.
            d.range = item->load()->tokenRange();
            diags_->add(std::move(d));
            throw BuildAbortException();
        }
    }

    auto arrayData = factory.build();
    // The value each element was written as is kept even when another element is computed: the
    // position of the whole literal decides the type of the elements it did write, and an element
    // converted from the double it was lowered to instead of from the value written is rounded
    // twice (design principles 5.1; the rule on
    // `expected-type-through-literals-and-calls`, item 3). A computed element has no value of
    // its own, but it keeps its position, which is where it is reported.
    tree_->setLiteralElements(arrayData, std::move(literalElements));
    node_ptr_t res = tree_->makeData(arrayData);

    if (dangling) {
        *execNode << res;
        res = execNode;
    }

    LEAVE("ArrayData");
    return res;
}

node_ptr_t Builder::visitComprehension(const AST::node_ptr_t &ast) {
    const AST::node_ptr_t &elementAst = ast->at(1);
    const AST::node_ptr_t &nameAst    = ast->at(2);
    const AST::node_ptr_t &sourceAst  = ast->at(3);
    const AST::node_ptr_t conditionAst =
        ast->at(4)->empty() ? AST::node_ptr_t{} : ast->at(4)->at(0);
    const Reference nameRef = nameAst->loadAs<AST::RefDataLoad>()->ref();
    if (!validateIdent(nameRef.ident())) {
        diags_->of(SemanticDiag::ReservedIdentifier).at(nameAst->load()->tokenRange()).commit();
        throw BuildAbortException();
    }
    const auto place = [&](const node_ptr_t &node, const AST::node_ptr_t &at, const char *what) {
        setOriginFromAst(context_, node, at, camel::source::OriginKind::GctNode, what);
        return node;
    };
    // The function `(x) => body`: what a lambda with an expression body is, with the type of
    // `x` left to the call that receives it (a type variable no program can write).
    const auto elementFunction = [&](const AST::node_ptr_t &body) {
        FunctionTypeFactory factory;
        factory.addNormArg(
            nameRef.ident(),
            TypeVarType::create("$element" + std::to_string(idIndex_++), true),
            false);
        FunctionType *type  = factory.buildOpen(); // a function being defined: see openSignature_
        node_ptr_t typeNode = place(tree_->makeType(type), body, "gct.func.type");
        const ValueUseScope valueUseScope(*this);
        valueUse_           = {};
        node_ptr_t bodyNode = place(makeExec(false), body, "gct.block");
        *bodyNode << visitData(body);
        node_ptr_t funcNode = place(tree_->makeFunc(std::string()), body, "gct.func");
        *funcNode << typeNode << bodyNode;
        return funcNode;
    };
    // `value->op<function>`.
    const auto apply = [&](const std::string &op, const node_ptr_t &function, const node_ptr_t &value) {
        node_ptr_t withNode = place(tree_->makeWith(1), ast, "gct.with");
        *withNode << place(tree_->makeDRef(op), ast, "gct.dref") << function;
        node_ptr_t linkNode = place(tree_->makeLink(1), ast, "gct.link");
        *linkNode << withNode << value;
        return linkNode;
    };
    node_ptr_t result = visitData(sourceAst);
    if (conditionAst) {
        result = apply(
            camel::core::compilerNamedOperator("filter"), elementFunction(conditionAst), result);
    }
    // The two operators themselves, not what the names `filter` and `map` stand for where
    // the comprehension is written: a function of the program with one of those names must
    // not change what a comprehension means (the GIR builder resolves the reserved prefix
    // in the built-in module, visitDRefNode). Both names are made by
    // `core::compilerNamedOperator`, the one place a compiler-made operator name is built.
    return apply(camel::core::compilerNamedOperator("map"), elementFunction(elementAst), result);
}

namespace {

/// The element type of the array type `type` when a literal written where `type` is expected
/// has elements to put into `T?` / `T!` (directly, or in arrays nested in it); else nullptr.
Type *taggedElementTarget(Type *type) {
    if (type == nullptr || type->code() != TypeCode::Array || containsTypeVar(type)) {
        return nullptr;
    }
    Type *elem = tt::as_ptr<ArrayType>(type)->elemType();
    return asTagged(elem) != nullptr || taggedElementTarget(elem) != nullptr ? elem : nullptr;
}

} // namespace

namespace {

/// The source text the origin of a literal covers (`1.0e39`), when it is short and on one
/// line; empty otherwise. A diagnostic that quotes a literal quotes what was written.
std::string sourceTextAt(
    const camel::source::source_context_ptr_t &context, camel::source::origin_id_t origin) {
    const auto *sourceContext = context.get();
    const auto *record = sourceContext != nullptr && origin != camel::source::kInvalidOriginId
                             ? sourceContext->origin(origin)
                             : nullptr;
    const auto *span   = record != nullptr ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file =
        span != nullptr && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->endOffset < span->startOffset ||
        span->endOffset > file->content.size()) {
        return {};
    }
    std::string text = file->content.substr(span->startOffset, span->endOffset - span->startOffset);
    if (text.size() > 60 || text.find('\n') != std::string::npos) {
        return {};
    }
    return text;
}

} // namespace

bool Builder::rejectTextInLiteral(
    const data_ptr_t &data, Type *type, camel::source::origin_id_t where) {
    if (data == nullptr || type == nullptr || type->assignableFrom(data->type())) {
        return false;
    }
    // A text where the position expects another type is a type mismatch, not a literal that
    // does not fit: there is no implicit text-to-number (or text-to-anything) conversion
    // (design principles 5.1). The element's own position is where it is written, as
    // reports an element whose value does not fit (see the GIR builder's literalElementInType).
    if (data->type()->code() == TypeCode::String) {
        diags_->of(SemanticDiag::DeclaredTypeMismatch)
            .atOrigin(where)
            .commit("string", type->toString());
        return true;
    }
    if (!isComposite(data->type()->code())) {
        return false;
    }
    const auto *written  = tree_->literalElementsOf(data);
    const auto elementAt = [&](size_t index, camel::source::origin_id_t fallback) {
        const Tree::LiteralElement *element =
            written != nullptr && index < written->size() ? &(*written)[index] : nullptr;
        return element != nullptr && element->origin != camel::source::kInvalidOriginId
                   ? element->origin
                   : fallback;
    };
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        auto *structType  = tt::as_ptr<StructType>(type);
        const auto object = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return false;
        }
        size_t index = 0;
        for (const auto &[name, fieldValue] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return false;
            }
            const camel::source::origin_id_t fieldWhere = elementAt(index, where);
            ++index;
            if (rejectTextInLiteral(fieldValue, structType->typeAt(*fieldIndex), fieldWhere)) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Tuple && data->type()->code() == TypeCode::Tuple) {
        auto *tupleType   = tt::as_ptr<TupleType>(type);
        const auto object = std::dynamic_pointer_cast<TupleData>(data);
        if (tupleType == nullptr || object == nullptr ||
            object->raw().size() != tupleType->size()) {
            return false;
        }
        for (size_t index = 0; index < object->raw().size(); ++index) {
            if (rejectTextInLiteral(
                    object->raw()[index],
                    tupleType->typeAt(index),
                    elementAt(index, where))) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        auto *arrayType   = tt::as_ptr<ArrayType>(type);
        const auto object = std::dynamic_pointer_cast<ArrayData>(data);
        Type *elementType = arrayType != nullptr ? arrayType->elemType() : nullptr;
        if (elementType == nullptr || object == nullptr) {
            return false;
        }
        for (size_t index = 0; index < object->raw().size(); ++index) {
            if (rejectTextInLiteral(object->raw()[index], elementType, elementAt(index, where))) {
                return true;
            }
        }
    }
    return false;
}

namespace {

/// Whether a type has a position of an open rank (`T[..]`) anywhere: at that position a value of
/// the element type is the rank-zero value itself (design principles 5.4), so the literal standing
/// there is the position's to convert and not one the declaration can convert as data.
bool hasRankZeroPosition(Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Array) {
        const auto *arrayType = tt::as_ptr<ArrayType>(type);
        if (arrayType == nullptr) {
            return false;
        }
        return arrayType->rankZeroType() != nullptr || hasRankZeroPosition(arrayType->elemType());
    }
    if (type->code() == TypeCode::Struct) {
        const auto *structType = tt::as_ptr<StructType>(type);
        for (size_t i = 0; structType != nullptr && i < structType->size(); ++i) {
            if (hasRankZeroPosition(structType->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    if (type->code() == TypeCode::Tuple) {
        const auto *tupleType = tt::as_ptr<TupleType>(type);
        for (size_t i = 0; tupleType != nullptr && i < tupleType->size(); ++i) {
            if (hasRankZeroPosition(tupleType->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    return false;
}

} // namespace

node_ptr_t Builder::declaredValue(const node_ptr_t &value, Type *type, const AST::node_ptr_t &ast) {
    // A literal of the declared type, converted now (`let x: float = 1`). A declared `T?` /
    // `T!` and a type written with a type variable (decided when the generic function is
    // instantiated) are left to the CAST, and so is a function type, which a literal cannot
    // have. A type written as `typeof e` is left to the CAST too: it is inferred in the graph
    // builder, and until then it is not a type a literal could be converted to.
    if (value->type() == NodeKind::DATA && asTagged(type) == nullptr && !containsTypeVar(type)) {
        // The value the literal was written as, exactly (design principles 5.1): `let x: f32 =
        // 0.1` is the f32 nearest to one tenth, not the same double rounded twice, and `let x:
        // float = 1` is 1.0 (an integer literal takes the float type its context asks for).
        if (const literal::Value *exact = Tree::of(value)->literalValueOf(value);
            exact != nullptr && type->isPrimitive() &&
            (camel::core::type::isNumericScalar(type->code()) ||
             camel::core::type::isComplex(type->code()))) {
            if (auto converted = exact->toData(type); converted.has_value()) {
                return tree_->makeData(*converted);
            }
            if (exact->hasImaginaryPart() && !camel::core::type::isComplex(type->code())) {
                // A complex literal where a real type is declared: no implicit conversion
                // (design principles 5.1). `re(z)` and `im(z)` are the explicit operations.
                const camel::source::source_context_ptr_t sourceContext =
                    context_ != nullptr ? context_->sourceContext() : nullptr;
                std::string written = sourceTextAt(sourceContext, value->origin());
                if (written.empty()) {
                    written = exact->toString();
                }
                diags_->of(SemanticDiag::LiteralNotRepresentable)
                    .at(ast->load()->tokenRange())
                    .commit(
                        written,
                        type->toString(),
                        "a complex value does not become a real one; use re() or im()");
                throw BuildAbortException();
            }
            if (exact->kind() == literal::Kind::Float && isIntegerPrimitive(type)) {
                // A float literal where an integer is asked for: not a range problem, but a
                // conversion the language does not do (`1.0 as int` is an error too).
                reportLiteralCastFailure(diags_, ast->load()->tokenRange(), value->data(), type);
                throw BuildAbortException();
            }
            const camel::source::source_context_ptr_t sourceContext =
                context_ != nullptr ? context_->sourceContext() : nullptr;
            std::string written = sourceTextAt(sourceContext, value->origin());
            if (written.empty()) {
                written = exact->kind() == literal::Kind::Float ? exact->toFloatText()
                                                                : exact->toString();
            }
            diags_->of(SemanticDiag::LiteralNotRepresentable)
                .at(ast->load()->tokenRange())
                .commit(
                    written,
                    type->toString(),
                    exact->underflows(type)
                        ? "too small to be represented (the nearest value is zero)"
                        : "out of range (" + literal::rangeText(type) + ")");
            throw BuildAbortException();
        }
        // An unresolved literal (`[n, m]`, `{ a: x, b: 2.0 }`: some elements are values) takes
        // the declared type through the annotation CAST below, where its elements are made the
        // element (field) type — they are positions with an expected type too (design
        // principles 5.1). Converting its data now would keep the type its values give it.
        if (!isUnresolvedLiteral(value->data())) {
            // `T[..]` includes rank zero, which is the element itself (design principles 5.4): a
            // literal that is that element -- a number, a text, a tuple -- takes the element
            // type, and the declaration's `T[..]` is read by the annotation CAST below, whose
            // reading is the same one (Builder::visitCastNode). `T[]` states one dimension and
            // `T[3, ..]` a prefix the value must have; neither states a rank zero
            // (ArrayType::rankZeroType answers nullptr there), so a literal is refused there as
            // before.
            Type *literalType = type;
            if (type->code() == TypeCode::Array &&
                value->data()->type()->code() != TypeCode::Array) {
                if (Type *rankZero = tt::as_ptr<ArrayType>(type)->rankZeroType();
                    rankZero != nullptr) {
                    literalType = rankZero;
                }
            }
            // A text field of a composite literal is reported at the field itself, as a type
            // mismatch; a lone text value keeps the declaration's position, as the same check
            // reports it
            // (`let x: int = '42'` is a mismatch at the declaration), and anything else the
            // conversion cannot take is reported for the literal as a whole below.
            if (isComposite(value->data()->type()->code()) &&
                rejectTextInLiteral(value->data(), literalType, value->origin())) {
                throw BuildAbortException();
            }
            data_ptr_t converted = camel::core::data::convertDataTo(value->data(), literalType);
            if (converted != nullptr && converted->type()->equals(literalType)) {
                return tree_->makeData(converted);
            }
            if (converted == nullptr && hasRankZeroPosition(literalType)) {
                // The type has a position of `T[..]`, and a literal there is converted where a
                // position converts its literal: the element becomes the rank-zero value the
                // position promises (design principles 5.4), through the annotation CAST below
                // (`Builder::coerceTo`), which reports what the literal cannot be. Converting it
                // here would need the element types the literal is written at, which are the
                // position's, not the data layer's.
            } else if (converted == nullptr) {
                reportLiteralCastFailure(
                    diags_,
                    ast->load()->tokenRange(),
                    value->data(),
                    literalType);
                throw BuildAbortException();
            }
            // A conversion that comes back as another type is not what the declaration says:
            // an `f32[..]` states less about the value than the value states itself, and the
            // name takes the declared type through the annotation CAST below, which is the one
            // place a declaration gives its value a type (design principles 5.1, 5.4).
        }
    }
    // Any other expression: an annotation CAST (see the declaration). An expression of the
    // declared type is not refused as "dynamic type casting" (`var acc: V = p`
    // with `p: V`); converting a value to its own type does nothing (design principles 5.1).
    node_ptr_t castNode = tree_->makeCast(type, true);
    *castNode << value;
    setOriginFromAst(context_, castNode, ast, camel::source::OriginKind::GctNode, "gct.cast");
    return castNode;
}

namespace {

/// Whether an array position states a rank of its own: a shape whose dimensions are all written
/// as constants (`f32[2, 2]`, `f32[3]`). Those are the positions a literal is one array of
/// (design principles 5.1, 5.4); a shape with a dimension left open keeps the ordinary build.
///
/// A rank of one is a shape too. Leaving it out would be the whole difference
/// between `let v: float[3] = [1.0, 2.0, 3.0]` (which builds the literal into the stated shape)
/// and `[1.0, 2.0, 3.0] as float[3]` (which would not: the ordinary build makes a `float[]`, and
/// the `as` path then refuses it on `castSafety` with "the value cannot become that type" -- a
/// value that fits, refused by the one position that could not build it). Nothing about the literal's
/// own rank decides this: the position's shape does, and `[a, b, c]` in a `f32[3]` position is
/// that shape however it is spelled. The length it writes is checked here, in the same place a
/// rank-two literal's is, so a mismatch names the extent instead of falling to a conversion that
/// could not say why.
bool statesARank(const ArrayType *type) {
    const std::span<const ExtentForm> extents = type->extents();
    if (extents.empty()) {
        return false;
    }
    for (const ExtentForm &extent : extents) {
        if (!extent.isConstant()) {
            return false;
        }
    }
    return true;
}

/// Whether `type` states exactly the one dimension of no elements that `[]` writes: `f32[0]`.
///
/// The empty literal writes extent 0 in one dimension, and a position that states `f32[0]` states
/// the same extent. Extent 0 equals extent 0, so the position takes that literal, and refusing it
/// reads "no elements" as "no shape" -- the two are not the same statement, and a zero-length
/// array is a shape a program may declare, pass and compute with (an empty tensor's reduction is
/// `InvalidArgument`, not a compile error).
///
/// The same question is asked in three places and they answer alike: this literal-vs-position
/// check, the ranked comparison of the literal's lengths to the stated extents
/// (`visitRankedArrayInto` below, where `[]` into `f32[0]` compares 0 to 0), and a `f32[0]`
/// parameter taking a `[]` argument. A position that states *another* rank or another extent still
/// does not take `[]`: `f32[2]` asks for two elements, `f32[0, 0]` for a second axis the literal
/// does not write.
bool isADimensionOfNoElements(const ArrayType *type) {
    const std::span<const ExtentForm> extents = type->extents();
    return extents.size() == 1 && extents.front().isConstant() && extents.front().constant() == 0;
}

} // namespace

node_ptr_t Builder::visitDataInto(const AST::node_ptr_t &ast, Type *type, bool &entered) {
    entered        = false;
    Type *elemType = taggedElementTarget(type);
    if (type != nullptr && elemType == nullptr && !containsTypeVar(type) &&
        asTagged(type) == nullptr && ast->type() == AST::LoadType::Data) {
        // Design principles 5.1: the elements of an array literal and the fields of a struct
        // literal are positions with an expected type, like a declaration's. They are built
        // with the type the position gives them (`let xs: float[] = [1, 2.0]`, `let p: P = {
        // a: 1, b: 2.0 }`), so a literal element is converted now, exactly, and an `int`
        // array is never stored where a `float[]` was declared.
        const auto &load = ast->loadAs<AST::DataLoad>();
        if (load->dataType() == AST::DataType::Array && ast->size() > 1) {
            // `[f(x) for x in xs]`: a comprehension, whose elements are not written in the first
            // child, is a computation and takes the type of its position where it is checked, not
            // an empty literal of it.
            return visitData(ast);
        }
        if (load->dataType() == AST::DataType::Array) {
            const auto &items = *ast->atAs<AST::RepeatedLoad>(0);
            if (type->code() == TypeCode::Array) {
                if (items.empty()) {
                    const auto *emptyInto = tt::as_ptr<ArrayType>(type);
                    if (emptyInto != nullptr && statesARank(emptyInto) &&
                        !isADimensionOfNoElements(emptyInto)) {
                        // `[]` writes one dimension of no elements: it is not the shape the
                        // position states, and it is not reshaped into it either. A position that
                        // states that same dimension (`f32[0]`) is the one stated shape `[]`
                        // writes, and it is taken below like any other empty array of the
                        // position's type.
                        diags_->of(SemanticDiag::DimensionMismatch)
                            .at(ast->load()->tokenRange())
                            .commit(
                                "The array literal written here",
                                "0",
                                "the position states the shape '" + type->toString() + "'");
                        throw BuildAbortException();
                    }
                    if (emptyInto != nullptr && statesARank(emptyInto)) {
                        // `f32[0]`: the literal writes the one dimension of no elements the
                        // position states, so it is built *as* that shape -- the stated extent is
                        // the literal's own extent, zero, and the value carries it. Built as the
                        // open `f32[]` below, the declaration's type was checked but not applied,
                        // and the name bound a value no `f32[0]` parameter would take.
                        return visitRankedArrayInto(ast, type, entered);
                    }
                    // `[]` in a position of type `T[]` is the empty array of `T` (design
                    // principles 5.1, 5.2): the elements it does not have are the only thing
                    // its type leaves open, so it takes the type of the position
                    // (`[] as int[]`, a recursive type's array field, a loop state).
                    entered = true;
                    return tree_->makeData(
                        tt::as_shared<Data>(
                            ArrayData::create(tt::as_ptr<ArrayType>(type)->elemType())));
                }
                const auto *statedType = tt::as_ptr<ArrayType>(type);
                if (statedType != nullptr && statesARank(statedType)) {
                    // A position that states a rank of its own (`f32[2, 2]`) takes the nested
                    // literal as one array of that shape, and refuses a literal that writes
                    // another shape (design principles 5.1, 5.4).
                    return visitRankedArrayInto(ast, type, entered);
                }
                return visitArrayInto(ast, type, entered);
            }
        } else if (load->dataType() == AST::DataType::Struct && type->code() == TypeCode::Struct) {
            return visitStructInto(ast, type, entered);
        } else if (load->dataType() == AST::DataType::Tuple && type->code() == TypeCode::Tuple) {
            // A tuple literal is positional like an array, and its elements are positions with an
            // expected type too (`let t: (f32, f32) = (1.0, 2.0)`; design principles 5.1). Built
            // as the declared type, each element is converted from the value it was written as;
            // left to declaredValue, the whole tuple was converted from the doubles its elements
            // had been lowered to, and every element was rounded twice (
            // on `expected-type-through-literals-and-calls`, item 3).
            return visitTupleInto(ast, type, entered);
        }
    }
    if (elemType == nullptr || ast->type() != AST::LoadType::Data ||
        ast->loadAs<AST::DataLoad>()->dataType() != AST::DataType::Array ||
        ast->atAs<AST::RepeatedLoad>(0)->size() == 0) {
        return visitData(ast);
    }
    // Design principles 3.8: the elements of an array literal are a position where a value
    // enters `T?` / `T!` when the element type is known, as it is here. Each element is the
    // value under a CAST to the element type (the conversion an annotated `let` writes), so
    // the array is built from computed elements of exactly that type.
    entered = true;
    ArrayDataFactory factory;
    node_ptr_t execNode = makeExec(false, true);
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(item, elemType, nested);
        if (asTagged(elemType) != nullptr) {
            node_ptr_t castNode = tree_->makeCast(elemType, true);
            *castNode << dataNode;
            setOriginFromAst(
                context_,
                castNode,
                item,
                camel::source::OriginKind::GctNode,
                "gct.cast");
            dataNode = castNode;
        } else if (!nested && dataNode->type() == NodeKind::DATA) {
            // An inner literal without elements to put into a form (`[]`): the literal of
            // the element type.
            const auto &data = dataNode->data();
            if (auto converted = data->convertTo(elemType)) {
                dataNode = tree_->makeData(converted);
            }
        }
        auto [refNode, refData] = makeRefData(dataNode);
        *execNode << refNode;
        factory.add(refData);
    }
    *execNode << tree_->makeData(factory.build());
    return execNode;
}

node_ptr_t
Builder::literalNodeInType(const node_ptr_t &node, Type *type, const AST::node_ptr_t &at) {
    if (node == nullptr || type == nullptr || node->type() != NodeKind::DATA) {
        return node;
    }
    const data_ptr_t &data = node->data();
    if (type->equals(data->type())) {
        return node;
    }
    // The value the literal was written as, exactly (design principles 5.1): an integer
    // literal becomes the float type its position asks for without being rounded twice.
    const literal::Value *exact = Tree::of(node)->literalValueOf(node);
    if (exact == nullptr || !type->isPrimitive()) {
        return node;
    }
    // The element is quoted as it is written (`1e40`, not the canonical `1e+40`), as a literal
    // in any other position with an expected type is (design principles 5.1).
    const std::string written = sourceTextAt(
        context_ != nullptr ? context_->sourceContext() : nullptr,
        at->load()->origin());
    if (!literal::kindEnters(*exact, type)) {
        // A decimal literal where an integer is asked for: the fraction would be lost.
        reportLiteralCastFailure(diags_, at->load()->tokenRange(), data, type, written);
        throw BuildAbortException();
    }
    if (auto converted = exact->toData(type); converted.has_value()) {
        node_ptr_t res = tree_->makeData(*converted);
        // The converted literal is the same value written: later positions round it again from
        // the exact value, never from the double it became here (design principles 5.1).
        tree_->setLiteralValue(res, *exact);
        setOriginFromAst(context_, res, at, camel::source::OriginKind::GctNode, "gct.data");
        return res;
    }
    reportLiteralCastFailure(diags_, at->load()->tokenRange(), data, type, written);
    throw BuildAbortException();
}

namespace {

/// Whether `ast` is an array literal written with brackets (`[1.0, 2.0]`): the levels a nested
/// literal writes are the levels its brackets write, whatever its elements turn out to be.
bool isArrayLiteralData(const AST::node_ptr_t &ast) {
    return ast != nullptr && ast->type() == AST::LoadType::Data &&
           ast->loadAs<AST::DataLoad>()->dataType() == AST::DataType::Array;
}

/// The shape a nested array literal writes, one length per level (`[[a, b], [c, d]]` is [2, 2]),
/// as the brackets write it. `at` holds the level's own literal, which is where a level is
/// reported; `shaped` is false when a level is not one shape — a row beside an element, or two
/// rows of different lengths — and then no rank-N array has that shape.
///
/// When `shaped` is false, `lengths` holds only the levels walked before the disagreement: a half
/// shape, not a wrong one. So a caller reads `shaped` first and `lengths` second; whoever reads
/// `lengths` first and `shaped` after is one `if` away from the same accident as the rank-one
/// literal walk had (a judgement that worked out the lengths and did not hand them out).
void literalShapeOf(
    const AST::node_ptr_t &ast, std::vector<int64_t> &lengths, std::vector<AST::node_ptr_t> &at,
    bool &shaped) {
    if (!isArrayLiteralData(ast)) {
        return;
    }
    const auto &items = *ast->atAs<AST::RepeatedLoad>(0);
    lengths.push_back(static_cast<int64_t>(items.size()));
    at.push_back(ast);
    if (items.empty()) {
        // `[]` writes one level of no elements; whether a level inside it is written says
        // nothing, because there is none.
        return;
    }
    const bool rows = isArrayLiteralData(items.front());
    std::vector<int64_t> rowShape;
    for (size_t index = 0; index < items.size(); ++index) {
        if (isArrayLiteralData(items[index]) != rows) {
            shaped = false;
            return;
        }
        if (!rows) {
            break;
        }
        std::vector<int64_t> sub;
        std::vector<AST::node_ptr_t> subAt;
        literalShapeOf(items[index], sub, subAt, shaped);
        if (!shaped) {
            return;
        }
        if (index == 0) {
            rowShape = std::move(sub);
            at.insert(at.end(), subAt.begin(), subAt.end());
        } else if (sub != rowShape) {
            shaped = false;
            return;
        }
    }
    lengths.insert(lengths.end(), rowShape.begin(), rowShape.end());
}

} // namespace

node_ptr_t
Builder::visitRankedArrayInto(const AST::node_ptr_t &ast, Type *arrayType, bool &entered) {
    ENTER("ArrayDataRanked");
    entered            = false;
    const auto *aType  = tt::as_ptr<ArrayType>(arrayType);
    const auto extents = aType->extents();
    Type *elemType     = aType->elemType();

    std::vector<int64_t> lengths;
    std::vector<AST::node_ptr_t> levelAt;
    bool shaped = true;
    literalShapeOf(ast, lengths, levelAt, shaped);
    const auto report = [&](const AST::node_ptr_t &where,
                            const std::string &subject,
                            int64_t written,
                            const std::string &stated) {
        diags_->of(SemanticDiag::DimensionMismatch)
            .at(where->load()->tokenRange())
            .commit(subject, std::to_string(written), stated);
    };
    if (!shaped || lengths.size() != extents.size()) {
        // The literal writes a shape of its own — a flat `[1.0, 2.0, 3.0, 4.0]` is one
        // dimension, whatever the position states — and the position takes only the shape it
        // states (design principles 5.1, 5.4).
        report(
            ast,
            "The array literal written here",
            lengths.empty() ? 0 : lengths.front(),
            "the position states the shape '" + arrayType->toString() + "'");
        throw BuildAbortException();
    }
    for (size_t level = 0; level < lengths.size(); ++level) {
        if (!extents[level].isConstant() || extents[level].constant() == lengths[level]) {
            continue;
        }
        report(
            level < levelAt.size() ? levelAt[level] : ast,
            level == 0 ? "The array literal written here" : "This row",
            lengths[level],
            "the position states " + std::to_string(extents[level].constant()) +
                " for this dimension");
        throw BuildAbortException();
    }

    // The elements of the literal in the order the flattened value holds them, each built in the
    // element type the position gives it, as an array literal's elements are (design principles
    // 5.1). The shape is the one the literal writes, which is the position's by the checks above.
    //
    // An element the program computes is one of those elements like any other, and it is read in
    // this order for the same reason: the brackets write the shape and the leaves are what fills
    // it, so `[[1.0, x], [x, 2.0]]` is four elements in row order, not two rows of them. Read row
    // by row, each row became a value of its own and the outer literal's element type became the
    // leaf type those rows happened to give it (`float`) -- a type no position states, which the
    // rows (`float[]`) then did not fit, and the report named the first row (`Cannot insert data
    // of type 'float[]' into a(n) array with element type 'float'`). The value the position asks
    // for is flat, so its elements are read flat; the one thing a computed leaf changes is that
    // it cannot be folded, so it stays a hole in that same value and the node that computes it
    // stands for it there.
    std::vector<AST::node_ptr_t> leaves;
    const auto collect = [&](const auto &self, const AST::node_ptr_t &node) -> void {
        const auto &items = *node->atAs<AST::RepeatedLoad>(0);
        if (!items.empty() && isArrayLiteralData(items.front())) {
            for (const auto &item : items) {
                self(self, item);
            }
            return;
        }
        leaves.insert(leaves.end(), items.begin(), items.end());
    };
    collect(collect, ast);

    ArrayDataFactory factory;
    factory.setElemType(elemType);
    factory.setExtents(lengths);
    node_ptr_t execNode = makeExec(false, true);
    std::vector<Tree::LiteralElement> literalElements;
    bool dangling = false;
    for (const auto &leaf : leaves) {
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(leaf, elemType, nested);
        if (!nested) {
            dataNode = literalNodeInType(dataNode, elemType, leaf);
        }
        literalElements.push_back(writtenElement(dataNode));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        try {
            factory.add(data);
        } catch (Diagnostic &d) {
            d.range = leaf->load()->tokenRange();
            diags_->add(std::move(d));
            throw BuildAbortException();
        }
    }
    const data_ptr_t built = factory.build();
    tree_->setLiteralElements(built, std::move(literalElements));
    node_ptr_t res = tree_->makeData(built);
    // The node is the literal as it was written, not the last element built for it: what the
    // literal is named by, and where a report about it points, is the literal itself.
    setOriginFromAst(context_, res, ast, camel::source::OriginKind::GctNode, "gct.data");
    // The built value is the value of the declared type: its shape is the shape the position
    // states, so nothing is left to declaredValue (which would drop the shape).
    entered = true;
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("ArrayDataRanked");
    return res;
}

node_ptr_t Builder::visitArrayInto(const AST::node_ptr_t &ast, Type *arrayType, bool &entered) {
    ENTER("ArrayData");
    entered        = false;
    Type *elemType = tt::as_ptr<ArrayType>(arrayType)->elemType();
    if (elemType == nullptr || containsTypeVar(elemType)) {
        LEAVE("ArrayData");
        return visitData(ast);
    }
    ArrayDataFactory factory;
    // The position names the element type: the literal is that array even where an element is
    // a computed value (`[[a, b]]` for an `int[][]`), so the type is not left to the elements
    // to fix (and a nested literal is not read as the type of an outer position).
    factory.setElemType(elemType);
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    // Each element's position, as the ordinary build keeps it.
    std::vector<Tree::LiteralElement> literalElements;
    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(item, elemType, nested);
        if (!nested) {
            dataNode = literalNodeInType(dataNode, elemType, item);
        }
        literalElements.push_back(writtenElement(dataNode));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        try {
            factory.add(data);
        } catch (Diagnostic &d) {
            // The element that does not fit the others: the data layer knows the types, the
            // position is known here.
            d.range = item->load()->tokenRange();
            diags_->add(std::move(d));
            throw BuildAbortException();
        }
    }
    const data_ptr_t built = factory.build();
    // Each element's value as it was written is kept even when another element of the literal is
    // computed (design principles 5.1): the position of the whole literal decides the type of
    // the elements it did write, and an element converted from the double it was lowered to
    // instead of from the value written is rounded twice (the rule on
    // `expected-type-through-literals-and-calls`, item 3). A computed element has no value of
    // its own, but it keeps its position, which is where it is reported.
    tree_->setLiteralElements(built, std::move(literalElements));
    node_ptr_t res = tree_->makeData(built);
    // The literal is the value of the type asked for when the two types are the same. When
    // they are not (a named struct type, say), the caller still applies the declared value or
    // the annotation CAST, which attaches that type as it does for any value.
    entered = arrayType->equals(built->type());
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("ArrayData");
    return res;
}

/// The tuple literal `ast` built with every element in the type its position gives it
/// (`let t: (f32, f32) = (1.0, 2.0)`, `let p: (P, int) = (p0, 1)`): a literal element becomes
/// the element type exactly, converted from the value it was written as; an element that is
/// computed has no value of its own and keeps its position as a hole, and an element that is
/// not a literal keeps its own type, which the GIR builder reports if it does not fit (design
/// principles 5.1). Only when the literal writes as many elements as the tuple type: any other
/// shape is left to the ordinary build, which reports it.
node_ptr_t Builder::visitTupleInto(const AST::node_ptr_t &ast, Type *tupleType, bool &entered) {
    ENTER("TupleData");
    entered           = false;
    auto *tType       = tt::as_ptr<TupleType>(tupleType);
    const auto &items = *ast->atAs<AST::RepeatedLoad>(0);
    if (tType == nullptr || items.size() != tType->size()) {
        LEAVE("TupleData");
        return visitData(ast);
    }
    for (size_t i = 0; i < tType->size(); ++i) {
        if (tType->typeAt(i) == nullptr || containsTypeVar(tType->typeAt(i))) {
            LEAVE("TupleData");
            return visitData(ast);
        }
    }
    // The tuple is the one the position names, as for an array (`visitArrayInto`: the element
    // type comes from the position, not from the elements): a computed element is a hole with
    // no type of its own, and the position's type is what the remaining elements are converted
    // into. Left to the factory, the type would come from the elements — the hole becoming
    // whatever a hole is — and a literal nested in another literal would then fall back to its
    // ordinary build and lose the position's element types (design principles 5.1).
    data_vec_t data;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    std::vector<Tree::LiteralElement> literalElements;
    for (size_t i = 0; i < items.size(); ++i) {
        Type *elemType      = tType->typeAt(i);
        bool nested         = false;
        node_ptr_t dataNode = visitDataInto(items[i], elemType, nested);
        if (!nested) {
            dataNode = literalNodeInType(dataNode, elemType, items[i]);
        }
        literalElements.push_back(writtenElement(dataNode));
        auto [element, elementDangling] = extractData(dataNode, execNode, dangling);
        if (elementDangling) {
            dangling = true;
        }
        if (!elementDangling && (element == nullptr || element->type() == nullptr ||
                                 !elemType->assignableFrom(element->type()))) {
            // An element written as a variable keeps its own type (design principles 5.1): the
            // tuple is of an unnamed tuple type whose element is of another type, and the
            // position it is written in reports it, as it would without this build.
            LEAVE("TupleData");
            return visitData(ast);
        }
        data.push_back(element);
    }
    const data_ptr_t built = TupleData::create(tupleType, std::move(data));
    tree_->setLiteralElements(built, std::move(literalElements));
    node_ptr_t res = tree_->makeData(built);
    entered        = true;
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("TupleData");
    return res;
}

node_ptr_t Builder::visitStructInto(const AST::node_ptr_t &ast, Type *structType, bool &entered) {
    ENTER("StructData");
    entered            = false;
    auto *sType        = tt::as_ptr<StructType>(structType);
    const auto &fields = *ast->atAs<AST::RepeatedLoad>(0);
    // A literal that does not write exactly the fields of the type is built as usual and
    // reported by the conversion that finds it of another type.
    if (fields.size() != sType->size()) {
        LEAVE("StructData");
        return visitData(ast);
    }
    for (const auto &child : fields) {
        const auto &named = child->loadAs<AST::NamedDataLoad>();
        if (!sType->findField(named->ref().ident()).has_value()) {
            LEAVE("StructData");
            return visitData(ast);
        }
    }
    StructDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    // The fields are built in the type's order (the declaration's order for a named type),
    // not the order they are written in: a literal at a position of a struct type is a value
    // of that type (design principles 5.2), so `{ y: 1, x: 2 }` at `Point` is a `Point` whose
    // fields are `x, y`. Each field keeps the position it was written at, for diagnostics
    // a field the field type cannot take is reported at the field, not at the
    // literal.
    std::unordered_map<std::string, Tree::LiteralElement> literalElementsByName;
    for (size_t i = 0; i < sType->size(); ++i) {
        const std::string name(sType->fieldName(i));
        const AST::node_ptr_t *written = nullptr;
        for (const auto &child : fields) {
            if (child->loadAs<AST::NamedDataLoad>()->ref().ident() == name) {
                written = &child;
                break;
            }
        }
        GUARD(written != nullptr, "A struct literal field was checked to exist by name.");
        Type *fieldType      = sType->typeAt(i);
        const auto &valueAst = (*written)->atAs<AST::DataLoad>(0);
        bool nested          = false;
        node_ptr_t fieldNode = visitDataInto(valueAst, fieldType, nested);
        if (asTagged(fieldType) != nullptr) {
            // A field of `T?` / `T!`: the value enters the form, as it does at any position
            // whose type is written (design principles 3.8).
            node_ptr_t castNode = tree_->makeCast(fieldType, true);
            *castNode << fieldNode;
            setOriginFromAst(
                context_,
                castNode,
                valueAst,
                camel::source::OriginKind::GctNode,
                "gct.cast");
            fieldNode = castNode;
        } else if (!nested) {
            fieldNode = literalNodeInType(fieldNode, fieldType, valueAst);
        }
        literalElementsByName.emplace(name, writtenElement(fieldNode));
        auto [data, fieldDangling] = extractData(fieldNode, execNode, dangling);
        if (fieldDangling) {
            dangling = true;
        }
        if (!fieldDangling && (data == nullptr || data->type() == nullptr ||
                               !fieldType->assignableFrom(data->type()))) {
            // A field written as a variable keeps its own type (design principles 5.1): the
            // literal is of an unnamed struct type whose field is of another type, and the
            // conversion at the position it is written in reports it, as it would without
            // building the literal in the position's type.
            LEAVE("StructData");
            return visitData(ast);
        }
        factory.add(name, data);
    }
    const data_ptr_t built = factory.buildAs(sType);
    if (built == nullptr) {
        // A field is not there or is another type: the conversion that finds the literal of
        // another type reports it.
        LEAVE("StructData");
        return visitData(ast);
    }
    {
        // A StructData holds its fields in the type's order, so the elements are recorded in
        // that order whatever order they were written in; a diagnostic then points at the
        // element that failed, not at the literal. This holds when a field is computed too: the
        // fields the literal did write keep the value they were written as (design principles
        //  5.1; the rule on `expected-type-through-literals-and-calls`, item 3).
        const auto object = std::dynamic_pointer_cast<StructData>(built);
        std::vector<Tree::LiteralElement> literalElements;
        if (object != nullptr) {
            literalElements.reserve(object->raw().size());
            for (const auto &[name, _] : object->raw()) {
                literalElements.push_back(literalElementsByName.at(name));
            }
        }
        tree_->setLiteralElements(built, std::move(literalElements));
    }
    node_ptr_t res = tree_->makeData(built);
    // The literal is a value of the type the position gives it: its fields are that type's
    // fields, in that type's order.
    entered = true;
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("StructData");
    return res;
}

/*
StructData() : NamedData* dataList ;
*/
node_ptr_t Builder::visitStructData(const AST::node_ptr_t &ast) {
    ENTER("StructData");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for StructData");

    StructDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    std::unordered_set<string> fieldNames;
    // The value each field was written as and its position, keyed by name: a StructData holds its
    // fields in name order, and the conversion walks the elements by index (design principles
    // 5.1; see visitArrayData).
    std::unordered_map<std::string, Tree::LiteralElement> literalElementsByName;

    for (const auto &child : *ast->atAs<AST::RepeatedLoad>(0)) {
        const auto &namedPair = child->loadAs<AST::NamedDataLoad>();
        const string &name    = namedPair->ref().ident();
        if (!fieldNames.insert(name).second) {
            // As in a struct type. The later value is not dropped silently.
            diags_->of(SemanticDiag::DuplicateDictKey).at(namedPair->tokenRange()).commit(name);
            throw BuildAbortException();
        }
        node_ptr_t dataNode = visitData(child->atAs<AST::DataLoad>(0));
        literalElementsByName.emplace(name, writtenElement(dataNode));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        factory.add(name, data);
    }

    auto structData = factory.build();
    {
        // The value every field was written as is kept even when another field is computed: the
        // position of the whole literal decides each written field's type, and a field converted
        // from the double it was lowered to is rounded twice (design principles 5.1).
        const auto object = std::dynamic_pointer_cast<StructData>(structData);
        std::vector<Tree::LiteralElement> literalElements;
        if (object != nullptr) {
            literalElements.reserve(object->raw().size());
            for (const auto &[name, _] : object->raw()) {
                literalElements.push_back(literalElementsByName.at(name));
            }
        }
        tree_->setLiteralElements(structData, std::move(literalElements));
    }
    node_ptr_t res = tree_->makeData(structData);

    // If there are dangling nodes, attach them to the execution node
    if (dangling) {
        *execNode << res;
        res = execNode;
    }
    LEAVE("StructData");
    return res;
}

/*
TupleData() : Data* data ;
*/
node_ptr_t Builder::visitTupleData(const AST::node_ptr_t &ast) {
    ENTER("TupleData");
    // Ensure the AST node is of the expected type (Data)
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for TupleData");

    TupleDataFactory factory;
    bool dangling       = false;
    node_ptr_t execNode = makeExec(false, true);
    // Each element's position and exact value, as for an array or struct literal:
    // a tuple is positional, and an element the element type cannot take is reported at the
    // element.
    std::vector<Tree::LiteralElement> literalElements;

    for (const auto &item : *ast->atAs<AST::RepeatedLoad>(0)) {
        node_ptr_t dataNode = visitData(item);
        literalElements.push_back(writtenElement(dataNode));
        auto [data, _] = extractData(dataNode, execNode, dangling);
        factory.add(data);
    }

    auto tupleData = factory.build();
    // As for an array or a struct literal: each element's value as written is kept even when
    // another element is computed (design principles 5.1; the rule on
    // `expected-type-through-literals-and-calls`, item 3).
    tree_->setLiteralElements(tupleData, std::move(literalElements));
    node_ptr_t res = tree_->makeData(tupleData);

    // If there are dangling nodes, attach them to the execution node
    if (dangling) {
        *execNode << res;
        res = execNode;
    }

    LEAVE("TupleData");
    return res;
}

/*
SpreadArg() : Data data ;
A rest argument (design principles 5.5): the operand is a compile-time tuple; the arguments it
stands for are decided when the call is instantiated.
*/
node_ptr_t Builder::visitSpreadArg(const AST::node_ptr_t &ast) {
    ENTER("SpreadArg");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for SpreadArg");
    node_ptr_t operand    = visitData(ast->atAs<AST::DataLoad>(0));
    node_ptr_t spreadNode = tree_->makeSpreadArg();
    *spreadNode << operand;
    setOriginFromAst(context_, spreadNode, ast, camel::source::OriginKind::GctNode, "gct.spread");
    LEAVE("SpreadArg");
    return spreadNode;
}
/*
RefData(Ref ref) ;
*/
node_ptr_t Builder::visitRefData(const AST::node_ptr_t &ast) {
    ENTER("RefData");
    GUARD(ast->type() == AST::LoadType::Data, "Expected DataLoad type for RefData");
    const auto &refData = ast->loadAs<AST::RefDataLoad>();
    node_ptr_t refNode  = tree_->makeDRef(refData->ref());
    LEAVE("RefData");
    return refNode;
}

} // namespace camel::compile::gct