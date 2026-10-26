// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/entry_args.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/module.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_types.h"
#include "camel/utils/number_text.h"
#include "camel/utils/path.h"
#include "camel/utils/utf8.h"

#include <charconv>
#include <concepts>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace camel::execute {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

using camel::core::error::RuntimeDiag;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using type::Type;
using type::TypeCode;

namespace {

struct LoaderRegistry {
    std::mutex mutex;
    std::unordered_map<uint32_t, EntryArgumentLoader> loaders;
};

LoaderRegistry &loaderRegistry() {
    static LoaderRegistry registry;
    return registry;
}

struct SaverRegistry {
    std::mutex mutex;
    std::unordered_map<uint32_t, EntryResultSaver> savers;
};

SaverRegistry &saverRegistry() {
    static SaverRegistry registry;
    return registry;
}

EntryResultSaver findSaver(TypeCode code) {
    auto &registry = saverRegistry();
    std::lock_guard lock(registry.mutex);
    auto it = registry.savers.find(static_cast<uint32_t>(code));
    return it == registry.savers.end() ? nullptr : it->second;
}

EntryArgumentLoader findLoader(TypeCode code) {
    auto &registry = loaderRegistry();
    std::lock_guard lock(registry.mutex);
    auto it = registry.loaders.find(static_cast<uint32_t>(code));
    return it == registry.loaders.end() ? nullptr : it->second;
}

/// The objects bound to parameters. Each stays rooted for the rest of the process (it is a
/// constant of the entry graph from then on), which also keeps its address current when the
/// collector moves it. Never destroyed: the allocator may be gone first at exit.
struct BoundObject {
    std::string name;
    std::unique_ptr<mm::RootHandle> root;
    Type *type;
};

struct BoundObjects {
    std::mutex mutex;
    std::vector<BoundObject> objects;
};

BoundObjects &boundObjects() {
    static BoundObjects *objects = new BoundObjects();
    return *objects;
}

/// A value read from the command line. An object of the auto space is rooted from the moment
/// it exists: reading the next value allocates and may collect (and move) it.
struct Loaded {
    slot_t raw = NullSlot;
    std::unique_ptr<mm::RootHandle> root;
    Type *type = nullptr;

    slot_t slot() const { return root ? rtdata::toSlot<rtdata::Object *>(root->get()) : raw; }
};

Loaded rooted(slot_t value, Type *valueType) {
    Loaded loaded;
    loaded.type = valueType;
    if (valueType && valueType->isGCTraced() && value != NullSlot) {
        loaded.root = std::make_unique<mm::RootHandle>(
            mm::autoSpace(),
            rtdata::fromSlot<rtdata::Object *>(value),
            valueType,
            "entry argument");
    } else {
        loaded.raw = value;
    }
    return loaded;
}

[[noreturn]] void refuse(std::string message) { throw std::runtime_error(std::move(message)); }

/// The catalogued sentence for a text that could not be read as a number, without its final
/// period: the reason clause EntryArgumentInvalid puts after its colon. `InvalidNumberText`
/// says the text is not a number; `ConversionOutOfRange` says it is one the type cannot hold and
/// gives the range (both are the sentences the conversion functions raise).
std::string numberTextRefusal(
    camel::core::error::RuntimeDiag diag, std::vector<std::string> args) {
    std::string message = camel::core::error::runtimeDiagMessage(diag, std::move(args));
    if (!message.empty() && message.back() == '.') {
        message.pop_back();
    }
    return message;
}

/// A text that is not a number at all (wrong form, trailing junk, an empty text).
[[noreturn]] void refuseNotANumber(
    const std::string &text, const std::string &target, const std::string &what) {
    refuse(numberTextRefusal(
        camel::core::error::RuntimeDiag::InvalidNumberText, {text, target, what}));
}

/// A number the declared type cannot hold (overflow or underflow): the text is quoted as a
/// written value is, and the reason names the type and its range.
[[noreturn]] void refuseOutOfRange(
    const std::string &text, const std::string &target, const std::string &reason) {
    refuse(numberTextRefusal(
        camel::core::error::RuntimeDiag::ConversionOutOfRange,
        {"'" + text + "'", target, reason}));
}

/// The two parts of a complex text, read at the declared width (camel/utils/number_text.h). A
/// part that is not a number, or is a number that width cannot hold, is refused: the reason
/// names the part and its range.
template <std::floating_point T>
std::pair<T, T> complexFromText(
    const std::string &text, const char *target, bool complexText,
    const camel::utils::ComplexTextParts &parts) {
    const std::string range = camel::utils::complexRangeText<T>();
    const auto check       = [&](std::string_view partText, const char *part, T &out) {
        switch (camel::utils::parseNumberToken<T>(partText, out)) {
        case camel::utils::NumberTextStatus::Ok:
            return;
        case camel::utils::NumberTextStatus::Underflow:
            // A nonzero part too small to tell from zero is not zero: the same refusal as a real
            // parameter's, with the part named.
            refuseOutOfRange(text, target, camel::core::floatUnderflowReason(target, part));
        case camel::utils::NumberTextStatus::OutOfRange:
            refuseOutOfRange(
                text,
                target,
                std::format("the {} part is outside the range of {} ({})", part, target, range));
        case camel::utils::NumberTextStatus::OutOfRangeInteger:
            // The sentence the translated program has for the same text (EntryNumberText's
            // outOfRangeInteger): it names the integer's range, not this part's.
            refuseOutOfRange(
                text,
                target,
                std::format(
                    "the value is outside the range of an integer ({})",
                    camel::utils::integerRangeText<int64_t>()));
        default:
            refuseNotANumber(
                text,
                std::string(camel::utils::complexNumberTextHelp),
                "a complex number");
        }
    };
    T real{};
    T imaginary{};
    if (!complexText) {
        // A text that is a real number is the real part, with an imaginary part of zero.
        check(text, "real", real);
        return {real, imaginary};
    }
    if (!parts.real.empty()) {
        check(parts.real, "real", real);
    }
    check(parts.imaginary, "imaginary", imaginary);
    return {real, imaginary};
}

Loaded loadValue(const std::string &text, Type *declared, const std::string &what);

/// One integer parameter, read at the width it is declared with, as one whole number token of
/// the language (parseNumberToken: decimal, `0x`, `0b` or `0o` octal; a leading zero is decimal
/// data text): `i32` is an int32 all the way, so a text outside that range is refused with
/// the range of `i32` rather than read as an int64 and narrowed (design principles 5.1: a value
/// is never cut to fit a type). `target` is the declared type's name in the message.
template <std::signed_integral T>
Loaded loadInteger(const std::string &text, Type *declared, const char *target) {
    T value{};
    switch (camel::utils::parseNumberToken<T>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        break;
    case camel::utils::NumberTextStatus::OutOfRange:
        refuseOutOfRange(text, target, camel::core::integerRangeReason<T>(target));
    default:
        refuseNotANumber(text, target, "an integer");
    }
    return rooted(rtdata::toSlot<T>(value), declared);
}

/// One floating-point parameter, read at the width it is declared with, as one whole number
/// token of the language: a real token or an integer token (`0xFF`, `0o77`; a source literal
/// takes that type too). A nonzero real token too small to tell from zero is refused, not read
/// as zero, the same rule a source literal and a conversion function apply. `f32` is a
/// float all the way, so a text outside its range is refused with the range of `f32` rather than
/// read as a double and narrowed. `target` is the declared type's name in the message.
template <std::floating_point T>
Loaded loadFloat(const std::string &text, Type *declared, const char *target) {
    T value{};
    switch (camel::utils::parseNumberToken<T>(text, value)) {
    case camel::utils::NumberTextStatus::Ok:
        break;
    case camel::utils::NumberTextStatus::Underflow:
        refuseOutOfRange(text, target, camel::core::floatUnderflowReason(target));
    case camel::utils::NumberTextStatus::OutOfRange:
        refuseOutOfRange(text, target, camel::core::floatRangeReason<T>(target));
    case camel::utils::NumberTextStatus::OutOfRangeInteger:
        refuseOutOfRange(
            text,
            target,
            std::format(
                "the value is outside the range of an integer ({})",
                camel::utils::integerRangeText<int64_t>()));
    default:
        refuseNotANumber(text, target, "a number");
    }
    return rooted(rtdata::toSlot<T>(value), declared);
}

/// A struct or tuple: `text` is a directory, the leaves are files named by their path in the
/// aggregate (`fc1.w`), the layout nn.save_params writes.
Loaded loadAggregate(
    const std::string &dir, Type *declared, const std::string &path, const std::string &what) {
    const bool isStruct = declared->code() == TypeCode::Struct;
    const size_t count  = isStruct ? static_cast<type::StructType *>(declared)->size()
                                   : static_cast<type::TupleType *>(declared)->size();
    std::vector<Loaded> fields;
    fields.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string key =
            isStruct ? std::string(static_cast<type::StructType *>(declared)->fieldName(i))
                     : std::to_string(i);
        Type *fieldType       = isStruct ? static_cast<type::StructType *>(declared)->typeAt(i)
                                         : static_cast<type::TupleType *>(declared)->typeAt(i);
        const std::string sub = path.empty() ? key : path + "." + key;
        if (fieldType->code() == TypeCode::Struct || fieldType->code() == TypeCode::Tuple) {
            fields.push_back(loadAggregate(dir, fieldType, sub, what));
        } else if (findLoader(fieldType->code())) {
            fields.push_back(loadValue(dir + "/" + sub, fieldType, what + "." + sub));
        } else {
            refuse(
                std::format(
                    "its member '{}' has type '{}', which cannot be read from a directory "
                    "(only members of a type with a file loader, such as Tensor, can)",
                    sub,
                    fieldType->toString()));
        }
    }
    // The leaves are rooted; the aggregate is allocated after all of them and filled from the
    // roots, so a collection during the loads or this allocation moves nothing out from under it.
    slot_t value = NullSlot;
    if (isStruct) {
        auto *object = ::Struct::create(count, mm::autoSpace());
        for (size_t i = 0; i < count; ++i) {
            object->set<slot_t>(i, fields[i].slot());
        }
        value = rtdata::toSlot<::Struct *>(object);
    } else {
        auto *object = ::Tuple::create(count, mm::autoSpace());
        for (size_t i = 0; i < count; ++i) {
            object->set<slot_t>(i, fields[i].slot());
        }
        value = rtdata::toSlot<::Tuple *>(object);
    }
    // The declared type is kept: it is the layout the program reads the aggregate with.
    return rooted(value, declared);
}

Loaded loadValue(const std::string &text, Type *declared, const std::string &what) {
    (void)what;
    switch (declared->code()) {
    case TypeCode::Int64:
        return loadInteger<int64_t>(text, declared, "int");
    case TypeCode::Int32:
        return loadInteger<int32_t>(text, declared, "i32");
    case TypeCode::Float64:
        return loadFloat<double>(text, declared, "float");
    case TypeCode::Float32:
        return loadFloat<float>(text, declared, "f32");
    case TypeCode::Complex64:
    case TypeCode::Complex128: {
        // The text of a complex number (language reference 2.5.3): a real part, the sign of the
        // imaginary part and the imaginary part (`1+2i`, `0.5-0.25i`), or only the imaginary
        // part with its sign (`-2i`). A text that is not written as a complex number but is a
        // number is its real part with an imaginary part of zero: a literal takes its type from
        // context (design principles 5.1), so `1.5` is `1.5+0i`, as `let z: c128 = 1.5` takes
        // it. Each part is read by the language's whole-token float parser at the declared width
        // (number_text.h), the same rule a float parameter follows, so the parts of a complex
        // value and a real value are read the same way.
        camel::utils::ComplexTextParts parts;
        const bool complexText = camel::utils::splitComplexText(text, parts);
        const bool single      = declared->code() == TypeCode::Complex64;
        double real            = 0.0;
        double imaginary       = 0.0;
        if (single) {
            const auto [realF, imaginaryF] =
                complexFromText<float>(text, "c64", complexText, parts);
            real      = static_cast<double>(realF);
            imaginary = static_cast<double>(imaginaryF);
        } else {
            std::tie(real, imaginary) = complexFromText<double>(text, "c128", complexText, parts);
        }
        return rooted(
            rtdata::toSlot<rtdata::Complex *>(rtdata::Complex::create(
                real,
                imaginary,
                declared->code(),
                mm::autoSpace())),
            declared);
    }
    case TypeCode::Bool: {
        if (text != "true" && text != "false") {
            refuse(std::format("'{}' is not a bool (write true or false)", text));
        }
        return rooted(rtdata::toSlot<bool>(text == "true"), declared);
    }
    case TypeCode::String:
        if (!camel::utf8::isValid(text)) {
            refuse("it is not valid UTF-8 (a string is always valid UTF-8)");
        }
        return rooted(rtdata::toSlot<::String *>(::String::from(text, mm::autoSpace())), declared);
    case TypeCode::Struct:
    case TypeCode::Tuple:
        return loadAggregate(text, declared, "", what);
    default:
        break;
    }
    if (const EntryArgumentLoader loader = findLoader(declared->code())) {
        Type *valueType    = declared;
        const slot_t value = loader(text, declared, valueType);
        if (value != NullSlot) {
            return rooted(value, valueType ? valueType : declared);
        }
        // NullSlot is the loader saying it does not take this type: a Tensor is a numeric array,
        // and the loader that reads a .npy file does not take the other arrays, which are
        // reported by the message below instead of by the loader's own.
    }
    refuse(
        std::format(
            "a value of type '{}' cannot be given on the command line (int, float, bool, "
            "string, types with a file loader such as Tensor, and structs or tuples of those "
            "can)",
            declared->toString()));
}

/// Writes `value` to `path`; a struct or tuple becomes the directory `path` with one file per
/// leaf, named by its path in the aggregate (the layout loadAggregate reads).
void saveValue(slot_t value, Type *valueType, const std::string &path, const std::string &dir) {
    const auto writeText = [&](const std::string &text) {
        std::ofstream out(camel::utils::pathFromUtf8(path), std::ios::binary | std::ios::trunc);
        out << text << "\n";
        if (!out) {
            throw std::runtime_error("the file cannot be written");
        }
    };
    if (!valueType) {
        throw std::runtime_error("the type of the value is unknown");
    }
    switch (valueType->code()) {
    case TypeCode::Int32:
        return writeText(std::to_string(rtdata::fromSlot<int32_t>(value)));
    case TypeCode::Int64:
        return writeText(std::to_string(rtdata::fromSlot<int64_t>(value)));
    case TypeCode::Float32:
        return writeText(camel::utils::floatText(rtdata::fromSlot<float>(value)));
    case TypeCode::Float64:
        return writeText(camel::utils::floatText(rtdata::fromSlot<double>(value)));
    case TypeCode::Bool:
        return writeText(rtdata::fromSlot<bool>(value) ? "true" : "false");
    case TypeCode::String:
        return writeText(rtdata::fromSlot<::String *>(value)->toString());
    case TypeCode::Struct:
    case TypeCode::Tuple: {
        const bool isStruct = valueType->code() == TypeCode::Struct;
        const size_t count  = isStruct ? static_cast<type::StructType *>(valueType)->size()
                                       : static_cast<type::TupleType *>(valueType)->size();
        // Nested aggregates do not nest directories: the leaf files of the whole value lie in
        // one directory, named by their path (`fc1.w`).
        const bool top             = dir.empty();
        const std::string leafRoot = top ? path : dir;
        if (top) {
            std::filesystem::create_directories(camel::utils::pathFromUtf8(path));
        }
        for (size_t i = 0; i < count; ++i) {
            const std::string key =
                isStruct ? std::string(static_cast<type::StructType *>(valueType)->fieldName(i))
                         : std::to_string(i);
            Type *fieldType    = isStruct ? static_cast<type::StructType *>(valueType)->typeAt(i)
                                          : static_cast<type::TupleType *>(valueType)->typeAt(i);
            const slot_t field = isStruct ? rtdata::fromSlot<::Struct *>(value)->get<slot_t>(i)
                                          : rtdata::fromSlot<::Tuple *>(value)->get<slot_t>(i);
            const std::string leaf = top ? leafRoot + "/" + key : path + "." + key;
            saveValue(field, fieldType, leaf, leafRoot);
        }
        return;
    }
    default:
        break;
    }
    if (const EntryResultSaver saver = findSaver(valueType->code())) {
        // An empty path is the saver saying it does not write this type (see saveValue's
        // counterpart of the loader's NullSlot above).
        if (!saver(value, valueType, path).empty()) {
            return;
        }
    }
    throw std::runtime_error(
        std::format(
            "a value of type '{}' cannot be written to a file (int, float, bool, string, "
            "types with a file format such as Tensor, and structs or tuples of those can)",
            valueType->toString()));
}

std::string portNameOf(const GCGraph *graph, size_t argIndex) {
    const auto *funcType = graph->funcType();
    if (funcType && argIndex < funcType->argNamesCount()) {
        return std::string(funcType->argNameAt(argIndex));
    }
    return std::format("#{}", argIndex);
}

void remember(Loaded &loaded, const std::string &name) {
    if (!loaded.root) {
        return;
    }
    auto &bound = boundObjects();
    std::lock_guard lock(bound.mutex);
    bound.objects.push_back(BoundObject{name, std::move(loaded.root), loaded.type});
}

} // namespace

bool parseEntryArgument(const std::string &spec, EntryArgument &out) {
    const size_t eq = spec.find('=');
    if (eq == std::string::npos || eq == 0) {
        return false;
    }
    out.name = spec.substr(0, eq);
    out.text = spec.substr(eq + 1);
    return true;
}

void registerEntryArgumentLoader(TypeCode code, EntryArgumentLoader loader) {
    auto &registry = loaderRegistry();
    std::lock_guard lock(registry.mutex);
    registry.loaders.insert_or_assign(static_cast<uint32_t>(code), std::move(loader));
}

void registerEntryResultSaver(TypeCode code, EntryResultSaver saver) {
    auto &registry = saverRegistry();
    std::lock_guard lock(registry.mutex);
    registry.savers.insert_or_assign(static_cast<uint32_t>(code), std::move(saver));
}

/// An error value nobody handled reached the exit of the run (design principles 3.9): the value
/// an entry graph returned, or the value a pass ended with (9.1). It is reported on standard
/// error as a diagnostic, in the format the command line chose, located where the error value
/// says it was made (`file:line:column`, the file name without its directory; an error made by an
/// operator names the operator instead and has no position).
void reportUnhandledError(core::context::Context &ctx, slot_t fallible, bool json) {
    const auto *info = rtdata::fromSlot<const ::Struct *>(rtdata::fallibleError(fallible));
    const auto text  = [&](std::string_view field) {
        const auto *str = info->get<const ::String *>(type::errorFieldIndex(field));
        return str ? str->toString() : std::string();
    };
    const std::string location = text("location");

    // `file:line:column`, read from the right: a file name may contain a colon.
    std::string file;
    size_t line = 0, column = 0;
    bool located = false;
    if (const size_t c2 = location.rfind(':'); c2 != std::string::npos && c2 > 0) {
        if (const size_t c1 = location.rfind(':', c2 - 1); c1 != std::string::npos) {
            const auto number = [&](size_t from, size_t to, size_t &out) {
                const char *first = location.data() + from, *last = location.data() + to;
                const auto parsed = std::from_chars(first, last, out);
                return first != last && parsed.ec == std::errc() && parsed.ptr == last && out > 0;
            };
            if (number(c1 + 1, c2, line) && number(c2 + 1, location.size(), column)) {
                file    = location.substr(0, c1);
                located = true;
            }
        }
    }

    auto builder = core::error::DiagnosticBuilder::of(RuntimeDiag::UnhandledError);
    if (located) {
        const core::error::CharPos at{line - 1, column - 1};
        builder.at(core::error::CharRange{at, at});
    } else {
        // An error made by an operator names the operator and has no position of its own:
        // the report is then located at `main`, whose exit it reached.
        if (const uint64_t origin = entryFunctionOrigin(ctx);
            origin != camel::source::kInvalidOriginId) {
            builder.atOrigin(origin);
        }
        if (!location.empty()) {
            builder.note("the error was made by " + location + ", which records no position");
        }
    }
    // The message of an error made from a fault is a sentence; the catalog entry ends it.
    std::string message = text("message");
    while (!message.empty() && message.back() == '.') {
        message.pop_back();
    }
    core::error::Diagnostic diagnostic = builder.commit(text("kind"), message);
    diagnostic.sourceContext           = ctx.sourceContext();
    if (located) {
        // The error value names the file without its directory; the modules of the program
        // know where it is.
        diagnostic.modulePath = file;
        for (const auto &module : ctx.allUserModules()) {
            if (module &&
                camel::utils::pathToUtf8(camel::utils::pathFromUtf8(module->path()).filename()) ==
                    file) {
                diagnostic.modulePath = module->path();
                break;
            }
        }
        diagnostic.moduleName = camel::utils::pathToUtf8(camel::utils::pathFromUtf8(file).stem());
    }
    diagnostic.stage = "run";

    std::cout.flush();
    if (json) {
        std::cerr << "[\n" << diagnostic.toJson() << "\n]\n";
    } else {
        const bool rich = core::error::diagnosticFormat() == core::error::DiagFormat::Rich;
        std::cerr << diagnostic.toText(std::cerr) << (rich ? "\n\n" : "\n");
    }
    std::cerr.flush();
}

int reportPassErrorValue(const core::context::context_ptr_t &ctx, slot_t error, bool json) {
    // A pass that failed is an error value nobody handled arriving at the exit of the pipeline
    // (design principles 9.1, 3.9): the same report, the same non-zero exit code -- the one a
    // failed result of `main` gets (deriveProcessExitCode).
    ctx->setProcessExitCode(1);
    reportUnhandledError(*ctx, error, json);
    return ctx->processExitCodeOr(1);
}

void writeEntryResultTo(
    const core::context::context_ptr_t &context, std::string path, bool jsonDiagnostics) {
    // The context owns the handler: it must not own the context in turn.
    core::context::Context *ctx = context.get();
    context->setEntryResultHandler(
        [ctx, path = std::move(path), jsonDiagnostics](GCGraph *graph, slot_t value) {
            std::string why;
            try {
                // Only a `main` that declares its return type has a result (design principles
                // 9.8). The value an undeclared one happens to end with (that of its last
                // statement, `println('a')` giving 'a') is neither printed nor written.
                // An error value such a body ends with is not reported at run time either:
                // nothing takes it, which the compiler warns about (UnusedFallibleValue,
                // design principles 3.9).
                const bool declared = ctx->entryResultDeclared();
                Type *resultType =
                    declared && graph && graph->funcType() && graph->funcType()->hasExitType()
                        ? graph->funcType()->exitType()
                        : nullptr;
                if (type::TupleType *fallible = type::asFallible(resultType)) {
                    if (rtdata::isErrorValue(value)) {
                        // The exit code is non-zero (Context::captureProcessExitCode).
                        reportUnhandledError(*ctx, value, jsonDiagnostics);
                        return;
                    }
                    value      = rtdata::falliblePayload(value);
                    resultType = fallible->payloadType();
                }
                const bool none = !resultType || resultType->code() == TypeCode::Void;
                // The types the process exit code is derived from
                // (Context::captureProcessExitCode).
                const bool exitCode = !none && (resultType->code() == TypeCode::Int32 ||
                                                resultType->code() == TypeCode::Int64 ||
                                                resultType->code() == TypeCode::Bool);
                if (path.empty()) {
                    if (!none && !exitCode) {
                        // One piece, like a println: the program's own output is complete by now.
                        std::ostringstream text;
                        rtdata::printSlot(text, value, resultType);
                        text << '\n';
                        std::cout << text.str();
                        std::cout.flush();
                    }
                    return;
                }
                if (none) {
                    why = declared ? "'main' returns no value"
                                   : "'main' declares no return type, so the value of its body is "
                                     "not the result of the program";
                } else {
                    saveValue(value, resultType, path, "");
                    return;
                }
            } catch (const camel::core::error::Diagnostic &diagnostic) {
                why = diagnostic.message;
            } catch (const std::exception &e) {
                why = e.what();
            }
            auto diag = ctx->rtmDiags()->of(RuntimeDiag::EntryResultNotWritten);
            if (const uint64_t origin = entryFunctionOrigin(*ctx);
                origin != camel::source::kInvalidOriginId) {
                diag.atOrigin(origin);
            }
            diag.commit(path, why);
        });
}

uint64_t entryFunctionOrigin(const core::context::Context &context) {
    if (const auto source = context.sourceContext()) {
        return source->debugMap().entryOrigin();
    }
    return camel::source::kInvalidOriginId;
}

std::vector<std::string> unboundEntryParameters(const GCGraph *root) {
    std::vector<std::string> names;
    if (!root) {
        return names;
    }
    const size_t withCount = root->withPorts().size();
    for (size_t i = 0; i < withCount + root->normPorts().size(); ++i) {
        names.push_back(portNameOf(root, i));
    }
    return names;
}

std::vector<BoundEntryArgument> boundEntryArguments() {
    auto &bound = boundObjects();
    std::lock_guard lock(bound.mutex);
    std::vector<BoundEntryArgument> out;
    out.reserve(bound.objects.size());
    for (const BoundObject &object : bound.objects) {
        out.push_back(
            BoundEntryArgument{
                object.name,
                rtdata::toSlot<rtdata::Object *>(object.root->get()),
                object.type});
    }
    return out;
}

const camel::runtime::Program *bindEntryArgumentSpecs(
    const core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::span<const std::string> specs) {
    std::vector<EntryArgument> arguments;
    for (const std::string &spec : specs) {
        EntryArgument argument;
        if (!parseEntryArgument(spec, argument)) {
            auto diag = context->rtmDiags()->of(RuntimeDiag::EntryArgumentInvalid);
            if (const uint64_t origin = entryFunctionOrigin(*context);
                origin != camel::source::kInvalidOriginId) {
                diag.atOrigin(origin);
            }
            diag.commit(spec, "an argument is written name=value");
            return nullptr;
        }
        arguments.push_back(std::move(argument));
    }
    return bindEntryArguments(context, program, arguments);
}

const camel::runtime::Program *bindEntryArguments(
    const core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::span<const EntryArgument> arguments) {
    GCGraph *root = program ? program->root() : nullptr;
    if (!root || arguments.empty()) {
        return program;
    }
    const auto invalid = [&](const std::string &name,
                             const std::string &why) -> const camel::runtime::Program * {
        auto diag = context->rtmDiags()->of(RuntimeDiag::EntryArgumentInvalid);
        if (const uint64_t origin = entryFunctionOrigin(*context);
            origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(name, why);
        return nullptr;
    };

    struct Formal {
        std::string name;
        gc_node_ref_t port;
        bool isWith;
        bool isVar;
        Type *type;
    };
    std::vector<Formal> formals;
    const auto *funcType   = root->funcType();
    const size_t withCount = root->withPorts().size();
    for (size_t i = 0; i < withCount + root->normPorts().size(); ++i) {
        const bool isWith = i < withCount;
        const size_t k    = isWith ? i : i - withCount;
        const bool known =
            funcType && (isWith ? k < funcType->withTypesCount() : k < funcType->normTypesCount());
        formals.push_back(
            Formal{
                .name   = portNameOf(root, i),
                .port   = isWith ? root->withPorts()[k] : root->normPorts()[k],
                .isWith = isWith,
                .isVar  = known && (isWith ? funcType->withIsVarAt(k) : funcType->normIsVarAt(k)),
                .type   = known ? (isWith ? funcType->withTypeAt(k) : funcType->normTypeAt(k))
                                : root->node(isWith ? root->withPorts()[k] : root->normPorts()[k])
                                      ->dataType(),
            });
    }
    std::string known;
    for (const Formal &formal : formals) {
        known += (known.empty() ? "" : ", ") + formal.name;
    }

    std::vector<std::pair<const Formal *, Loaded>> bound;
    std::unordered_set<std::string> seen;
    for (const EntryArgument &argument : arguments) {
        const Formal *formal = nullptr;
        for (const Formal &candidate : formals) {
            if (candidate.name == argument.name) {
                formal = &candidate;
                break;
            }
        }
        if (!formal) {
            return invalid(
                argument.name,
                formals.empty() ? std::string("'main' takes no parameters")
                                : std::format(
                                      "'main' has no parameter of that name (its parameters: {})",
                                      known));
        }
        if (!seen.insert(argument.name).second) {
            return invalid(argument.name, "the argument is given more than once");
        }
        if (formal->isVar) {
            return invalid(
                argument.name,
                "a 'var' parameter is a variable of the caller, which the command line cannot "
                "provide");
        }
        try {
            bound.emplace_back(formal, loadValue(argument.text, formal->type, argument.name));
        } catch (const camel::core::error::RuntimeFault &fault) {
            std::string why;
            for (const std::string &part : fault.formatArgs()) {
                why += (why.empty() ? "" : ": ") + part;
            }
            return invalid(argument.name, why.empty() ? std::string(fault.what()) : why);
        } catch (const camel::core::error::Diagnostic &diagnostic) {
            return invalid(argument.name, diagnostic.message);
        } catch (const std::exception &e) {
            return invalid(argument.name, e.what());
        }
    }

    camel::runtime::RuntimeGraphDraftSession session(context, program);
    camel::runtime::GraphDraft &draft = session.rootDraft();
    for (auto &[formal, loaded] : bound) {
        const gc_node_ref_t port = draft.draftIdOfSourceRef(formal->port);
        const slot_t value       = loaded.slot();
        const gc_node_ref_t node =
            draft.materializeStaticValue(value, loaded.type, camel::runtime::kGCNodeFlagConstant);
        draft.replaceAllValueUses(port, node);
        if (formal->isWith) {
            draft.removeWithPort(port);
        } else {
            draft.removeNormPort(port);
        }
        draft.eraseNode(port);
    }
    // The entry graph keeps a function type that matches its remaining ports: their names
    // are how the rest of the pipeline refers to the parameters still open.
    {
        type::FunctionTypeFactory factory;
        std::unordered_set<const Formal *> gone;
        for (auto &entry : bound) {
            gone.insert(entry.first);
        }
        for (const Formal &formal : formals) {
            if (gone.contains(&formal)) {
                continue;
            }
            if (formal.isWith) {
                factory.addWithArg(formal.name, formal.type, formal.isVar);
            } else {
                factory.addNormArg(formal.name, formal.type, formal.isVar);
            }
        }
        if (funcType && funcType->hasExitType()) {
            factory.setExitType(funcType->exitType());
        }
        draft.setFuncType(factory.build());
    }
    // A loaded value may have a more exact type than the parameter declares (the shape of a
    // tensor read from a file): the types of the nodes that read it follow.
    (void)camel::runtime::reinferDraftTypes(draft);
    const camel::runtime::Program *result = session.commit();
    for (auto &[formal, loaded] : bound) {
        remember(loaded, formal->name);
    }
    return result;
}

} // namespace camel::execute
