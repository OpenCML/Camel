// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/type_identity_selftest.h"

#include "camel/core/derivative.h"
#include "camel/core/type/base.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/extent.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typecall.h"
#include "camel/core/type/typevar.h"

#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {

namespace {

[[noreturn]] void fail(const std::string &what) {
    throw std::runtime_error("type identity self-test: " + what);
}

void require(bool condition, const std::string &what) {
    if (!condition) {
        fail(what);
    }
}

std::string show(const Type *type) { return type == nullptr ? "<null>" : type->toString(); }

Type *arrayOf(Type *elem, std::initializer_list<int64_t> extents) {
    const std::vector<int64_t> shape(extents);
    return ArrayType::create(elem, std::span<const int64_t>(shape));
}

using Terms = std::vector<std::pair<std::string, int64_t>>;

/// `elem[forms...]` over linear extents: each entry is a constant and the names it multiplies.
Type *linearArray(Type *elem, std::initializer_list<ExtentForm> forms) {
    const std::vector<ExtentForm> list(forms);
    return ArrayType::create(elem, std::span<const ExtentForm>(list), {});
}

ExtentForm dim(int64_t constant, Terms terms) {
    return ExtentForm::linear(constant, std::move(terms));
}

Type *structOf(std::initializer_list<std::pair<const char *, Type *>> fields) {
    StructTypeFactory factory;
    for (const auto &[name, type] : fields) {
        factory.add(name, type);
    }
    return factory.build();
}

Type *functionOf(
    std::initializer_list<Type *> params, Type *exit, std::initializer_list<Type *> withs = {},
    bool withRecordsValue = false) {
    FunctionTypeFactory factory;
    size_t index = 0;
    for (Type *with : withs) {
        factory.addWithArg(std::format("w{}", index++), with, false, withRecordsValue);
    }
    index = 0;
    for (Type *param : params) {
        factory.addNormArg(std::format("a{}", index++), param, false);
    }
    factory.setExitType(exit);
    return factory.build();
}

// Every type constructor, with a few shapes of each: the corpus the properties run over.
std::vector<Type *> corpus() {
    Type *i                 = Type::Int64();
    Type *f                 = Type::Float64();
    Type *s                 = Type::String();
    std::vector<Type *> out = {
        Type::Int32(),
        Type::Int64(),
        Type::Float32(),
        Type::Float64(),
        Type::Bool(),
        Type::String(),
        Type::Void(),
        Type::Complex64(),
        Type::Complex128(),
        Type::BFloat16(),
        Type::Float16(),
        ArrayType::create(f),
        arrayOf(f, {3}),
        arrayOf(f, {4}),
        arrayOf(f, {3, 4}),
        ArrayType::create(i),
        ArrayType::create(ArrayType::create(i)),
        TupleType::create(std::vector<Type *>{}),
        TupleType::create(std::vector<Type *>{i}),
        TupleType::create(std::vector<Type *>{i, s}),
        TupleType::create(std::vector<Type *>{arrayOf(i, {2}), i}),
        TupleType::create(std::vector<Type *>{arrayOf(i, {3}), i}),
        TupleType::nullable(i),
        TupleType::nullable(arrayOf(f, {2})),
        TupleType::nullable(arrayOf(f, {3})),
        TupleType::fallible(i),
        TupleType::fallible(s),
        structOf({{"x", i}, {"y", i}}),
        structOf({{"y", i}, {"x", i}}),
        structOf({{"x", i}}),
        structOf({{"x", arrayOf(i, {2})}}),
        structOf({{"x", arrayOf(i, {3})}}),
        StreamType::create(i),
        StreamType::create(s),
        functionOf({i}, i),
        functionOf({i}, f),
        functionOf({i, i}, i),
        functionOf({}, i),
        functionOf({i}, Type::Void()),
        functionOf({i}, arrayOf(i, {2})),
        functionOf({i}, arrayOf(i, {3})),
        functionOf({i}, i, {i}),
        functionOf({i}, i, {i}, true),
        linearArray(f, {dim(0, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"m", 1}})}),
        linearArray(f, {dim(1, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"n", 2}})}),
        linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"m", 1}})}),
        linearArray(f, {dim(0, {{"m", 1}}), dim(0, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}, {"m", 1}})}),
        linearArray(f, {dim(0, {{"a", 1}, {"b", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}}), ExtentForm::constant(4)}),
    };
    return out;
}

} // namespace

std::string debugRunTypeIdentitySelfTest() {
    const std::vector<Type *> types = corpus();
    size_t pairs                    = 0;
    // How many properties the guard readings below check: they are not pairs of the corpus but
    // what a case's guard is identified by, and the count is printed so a reading that stopped
    // asking can be seen.
    size_t guardReadings = 0;
    // The same count for the three places that name an object instead of a spelling.
    size_t addressReadings = 0;

    for (Type *a : types) {
        // reflexive: a type is itself, and assignable from itself
        require(a->equals(a), "equals is not reflexive for " + show(a));
        require(a->assignableFrom(a), "assignableFrom is not reflexive for " + show(a));
        // an unknown source is accepted by no type: the answer cannot depend on the target
        require(!a->assignableFrom(nullptr), "assignableFrom(nullptr) is true for " + show(a));
        require(
            a->unify(a) == a || (a->unify(a) != nullptr && a->unify(a)->equals(a)),
            "unify(a, a) is not a for " + show(a));
    }

    for (Type *a : types) {
        for (Type *b : types) {
            ++pairs;
            const bool ab = a->equals(b);
            const bool ba = b->equals(a);
            require(ab == ba, "equals is not symmetric for " + show(a) + " and " + show(b));
            if (ab) {
                // one identity: equal types are assignable both ways, share a canonical id, and
                // unify to themselves
                require(
                    a->assignableFrom(b) && b->assignableFrom(a),
                    "equal types are not assignable both ways: " + show(a) + " and " + show(b));
                require(
                    sameCanonicalType(a, b),
                    "equal types have two canonical ids: " + show(a) + " and " + show(b));
                require(a->unify(b) != nullptr, "equal types do not unify: " + show(a));
            } else {
                require(
                    !sameCanonicalType(a, b),
                    "unequal types share a canonical id: " + show(a) + " and " + show(b));
            }
            // a least common type, when there is one, is both: it takes either side
            Type *joined  = a->unify(b);
            Type *joinedR = b->unify(a);
            require(
                (joined == nullptr) == (joinedR == nullptr),
                "unify is not symmetric for " + show(a) + " and " + show(b));
            if (joined != nullptr) {
                require(
                    joined->equals(joinedR),
                    "unify gives two types for " + show(a) + " and " + show(b));
                if (a->code() != TypeCode::Void && b->code() != TypeCode::Void) {
                    require(
                        joined->assignableFrom(a) && joined->assignableFrom(b),
                        "unify of " + show(a) + " and " + show(b) + " (" + show(joined) +
                            ") does not take both");
                }
            }
        }
    }

    // transitivity of assignability, where no `void` stands in the middle (it discards)
    for (Type *a : types) {
        for (Type *b : types) {
            if (!a->assignableFrom(b) || a->code() == TypeCode::Void) {
                continue;
            }
            for (Type *c : types) {
                if (b->assignableFrom(c) && b->code() != TypeCode::Void) {
                    require(
                        a->assignableFrom(c),
                        "assignability is not transitive: " + show(a) + " <- " + show(b) + " <- " +
                            show(c));
                }
            }
        }
    }

    Type *i = Type::Int64();
    Type *f = Type::Float64();
    Type *s = Type::String();

    const auto expectJoin = [&](Type *a, Type *b, Type *expected) {
        Type *joined = a->unify(b);
        require(
            joined != nullptr && joined->equals(expected),
            "unify(" + show(a) + ", " + show(b) + ") = " + show(joined) + ", not " +
                show(expected));
    };

    // an extent is a linear form over names, and its equality is decidable: the same form up to
    // the names (design principles 5.4). Dimensions that relate differently are different types.
    const auto n  = linearArray(f, {dim(0, {{"n", 1}})});
    const auto m  = linearArray(f, {dim(0, {{"m", 1}})});
    const auto n1 = linearArray(f, {dim(1, {{"n", 1}})});
    const auto n2 = linearArray(f, {dim(0, {{"n", 2}})});
    require(n->equals(m) && sameCanonicalType(n, m), "f32[n] and f32[m] are not one type");
    require(!n->equals(n1) && !sameCanonicalType(n, n1), "f32[n] and f32[n + 1] are one type");
    require(
        !n->equals(n2) && !n1->equals(n2),
        "f32[n], f32[n + 1] and f32[2 * n] are not three types");
    require(!n->equals(ArrayType::create(f)), "f32[n] and f32[?] are one type");
    require(
        linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"m", 1}})})
            ->equals(linearArray(f, {dim(0, {{"m", 1}}), dim(0, {{"n", 1}})})),
        "f32[n, m] and f32[m, n] are not one type");
    require(
        !linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"n", 1}})})
             ->equals(linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"m", 1}})})),
        "f32[n, n] and f32[n, m] are one type");
    require(
        linearArray(f, {dim(0, {{"n", 1}, {"m", 1}})})
            ->equals(linearArray(f, {dim(0, {{"a", 1}, {"b", 1}})})),
        "f32[n + m] and f32[a + b] are not one type");
    require(
        n->mangle() != n1->mangle() && n->mangle() == m->mangle(),
        "the mangled name does not follow the identity");
    expectJoin(n, n, n);
    expectJoin(n, n1, ArrayType::create(f));
    require(
        n->assignableFrom(n1) && n1->assignableFrom(n),
        "a named extent asks for nothing at the type level");

    // `void` discards in the result of a function and nowhere else
    require(
        !TupleType::create(std::vector<Type *>{Type::Void(), i})
             ->assignableFrom(TupleType::create(std::vector<Type *>{s, i})),
        "a void tuple member accepts a string");
    require(
        !structOf({{"x", Type::Void()}})->assignableFrom(structOf({{"x", s}})),
        "a void field accepts a string");
    require(
        !functionOf({Type::Void()}, i)->assignableFrom(functionOf({s}, i)),
        "a void parameter accepts a string");
    require(
        functionOf({i}, Type::Void())->assignableFrom(functionOf({i}, s)),
        "a function that discards its result does not accept one that gives a string");

    // what a function type is: its parameters, its result, its resources and its `with` marks
    require(
        !functionOf({i}, i, {i}, false)->equals(functionOf({i}, i, {i}, true)),
        "a with parameter that records a value is the same as one that does not");
    require(
        !functionOf({i}, i, {i}, false)->assignableFrom(functionOf({i}, i, {i}, true)) &&
            !functionOf({i}, i, {i}, true)->assignableFrom(functionOf({i}, i, {i}, false)),
        "functions that differ in a recorded with parameter are assignable");
    require(
        !sameCanonicalType(functionOf({i}, i, {i}, false), functionOf({i}, i, {i}, true)),
        "functions that differ in a recorded with parameter share a canonical id");

    // the least common type of two branch arms (design principles 5.1, 5.4)
    expectJoin(
        TupleType::create(std::vector<Type *>{arrayOf(i, {2}), i}),
        TupleType::create(std::vector<Type *>{arrayOf(i, {3}), i}),
        TupleType::create(std::vector<Type *>{ArrayType::create(i), i}));
    expectJoin(
        TupleType::nullable(arrayOf(f, {2})),
        TupleType::nullable(arrayOf(f, {3})),
        TupleType::nullable(ArrayType::create(f)));
    expectJoin(
        TupleType::fallible(arrayOf(f, {2})),
        TupleType::fallible(arrayOf(f, {3})),
        TupleType::fallible(ArrayType::create(f)));
    expectJoin(
        structOf({{"x", arrayOf(i, {2})}, {"y", s}}),
        structOf({{"x", arrayOf(i, {3})}, {"y", s}}),
        structOf({{"x", ArrayType::create(i)}, {"y", s}}));
    expectJoin(
        functionOf({i}, arrayOf(i, {2})),
        functionOf({i}, arrayOf(i, {3})),
        functionOf({i}, ArrayType::create(i)));
    require(
        TupleType::create(std::vector<Type *>{i, s})
                ->unify(TupleType::create(std::vector<Type *>{s, i})) == nullptr,
        "tuples of other members unify");
    require(
        TupleType::create(std::vector<Type *>{i})
                ->unify(TupleType::create(std::vector<Type *>{i, i})) == nullptr,
        "tuples of other sizes unify");
    require(
        TupleType::nullable(i)->unify(TupleType::fallible(i)) == nullptr,
        "a nullable and a fallible unify");
    require(
        structOf({{"x", i}, {"y", i}})->unify(structOf({{"y", i}, {"x", i}})) == nullptr,
        "anonymous structs in another written order unify");
    require(
        functionOf({i}, i)->unify(functionOf({f}, i)) == nullptr,
        "functions that take other parameters unify");

    // `as` between primitive types: one table, and it admits only a type to itself. A number of
    // another type is converted by the conversion function, not by `as` (design principles 5.1)
    const std::vector<Type *> primitives = {
        Type::Int32(),
        Type::Int64(),
        Type::Float32(),
        Type::Float64(),
        Type::Bool(),
        Type::String(),
        Type::Complex64(),
        Type::Complex128(),
        Type::BFloat16(),
        Type::Float16(),
    };
    for (Type *to : primitives) {
        for (Type *from : primitives) {
            const CastSafety safety = to->castSafetyFrom(from);
            if (to->equals(from)) {
                require(
                    safety == CastSafety::Safe,
                    "`" + show(from) + " as " + show(to) + "` is refused");
            } else {
                require(
                    safety == CastSafety::Forbidden,
                    "`" + show(from) + " as " + show(to) + "` is allowed");
            }
        }
    }

    // What is cached about a type is never computed from something still being defined: a struct
    // asked about while its declaration is being read is not "not pure" for good.
    {
        StructDecl *decl  = StructDecl::create("IdentitySelfTestPoint", {});
        StructType *point = StructType::declare(decl, {}, {"a", "b"});
        require(!isPureDataType(point), "a struct with no field types yet is pure");
        require(pureDataLayout(point) == nullptr, "a struct with no field types yet has a layout");
        decl->define({{"a", i}, {"b", f}});
        point->completeFields({{"a", i}, {"b", f}});
        require(
            isPureDataType(point),
            "a struct asked about while it was declared stays not pure once it is complete");
        require(pureDataLayout(point) != nullptr, "the complete struct has no layout");
        // what is cached about a type is keyed by its canonical id: one entry, one answer, for two
        // spellings of one type
        {
            Type *named   = linearArray(f, {dim(0, {{"n", 1}})});
            Type *other   = linearArray(f, {dim(0, {{"m", 1}})});
            Type *tangent = camel::core::gradientTypeOf(named);
            require(tangent != nullptr, "an array of floats has no gradient type");
            require(
                tangent == camel::core::gradientTypeOf(other),
                "f64[n] and f64[m] have two gradient types");
        }

        // and two spellings of one type share the answer (the cache is keyed by canonical id)
        Type *pair  = structOf({{"x", i}, {"y", f}});
        Type *again = structOf({{"x", i}, {"y", f}});
        require(
            isPureDataType(pair) && pureDataLayout(pair) == pureDataLayout(again),
            "a pure struct has two layouts");
    }

    // A pack expansion: `..E` is `E` written once for each member of a pack a call bound, and the
    // type it makes is the tuple of those results (typecall.h `$expand` over `$pack`, the
    // expansion in typevar.cpp). This is the shape a declaration with a pack parameter needs for
    // its result type: `(..KeyOf<A>)[]` over three map types is the array of a three-member tuple,
    // which is the type `zip` has over maps. The type function is one the compiler still answers
    // (`KeyOf`, typecall.cpp `kTypeFunctions`): the ones a library writes take their members apart
    // in the type layer, and this test is about the expansion over a pack.
    {
        Type *packVar   = TypeVarType::create("A", false);
        Type *keyOfA    = TypeCallType::create("KeyOf", std::span<Type *const>(&packVar, 1));
        Type *expansion = TypeCallType::makeExpand(keyOfA);
        require(
            expansion->toString() == "...KeyOf<A>",
            "an expansion prints as '...' and its body: " + expansion->toString());
        require(
            TypeCallType::makeExpand(keyOfA) == expansion,
            "two expansions of one body are two objects");
        require(TypeCallType::isExpand(expansion), "an expansion is not seen as one");
        require(containsTypeCall(expansion), "containsTypeCall does not see an expansion");

        std::vector<Type *> three{MapType::get(i, f), MapType::get(s, f), MapType::get(f, f)};
        type_bindings_t packed{
            {"A", TypeCallType::makePack(std::span<Type *const>(three.data(), three.size()))}};
        Type *expanded = substituteTypeVars(expansion, packed);
        require(
            expanded->toString() == "(int, string, float)",
            "three members did not expand to a three-member tuple: " + show(expanded));
        // The whole result type of `zip`'s declaration: the tuple of elements, as an array.
        Type *result =
            substituteTypeVars(ArrayType::create(TupleType::create({expansion})), packed);
        require(
            result->toString() == "(int, string, float)[]",
            "the array of the expanded tuple is not the declared result type: " + show(result));
        require(sameCanonicalType(result, result), "the expanded result type has no identity");

        // A pack bound to one member expands to a one-member tuple, not to that member: a written
        // `..E` denotes a sequence of results, and how a parameter list reads a sequence is the
        // declaration's business.
        std::vector<Type *> one{MapType::get(i, f)};
        require(
            substituteTypeVars(
                expansion,
                {{"A", TypeCallType::makePack(std::span<Type *const>(one.data(), one.size()))}})
                    ->toString() == "(int,)",
            "one member did not expand to a one-member tuple");
        // A pack bound to nothing is an empty sequence, and its expansion is the empty tuple.
        std::vector<Type *> none;
        require(
            substituteTypeVars(
                expansion,
                {{"A", TypeCallType::makePack(std::span<Type *const>(none.data(), none.size()))}})
                    ->toString() == "()",
            "an empty pack did not expand to the empty tuple");
        // A pack the bindings do not name leaves the node waiting, exactly as an application over
        // an unbound type variable does.
        require(
            substituteTypeVars(expansion, {{"unused", i}}) == expansion,
            "an unrelated binding changed the expansion");
        // A single-type binding of that name is not a pack, so the node still waits: a name is
        // either a pack or a type variable, and which one it is belongs to the declaration.
        Type *notAPack = substituteTypeVars(expansion, {{"A", ArrayType::create(i)}});
        require(
            TypeCallType::isExpand(notAPack),
            "a single-type binding expanded a pack: " + show(notAPack));
    }

    // What identifies a `match type` case's guard: what the guard says, and not where the object
    // that holds it happens to live. Two guards written in one module with one text are one case,
    // so the table hands out one type; a guard that says something else, or the same words read in
    // another module, is another case; and the same guard on two subjects is two cases because the
    // subject is part of what the type says.
    {
        Type *number = Type::Int64();
        Type *text   = Type::String();
        const auto guarded = [](Type *subject, std::string_view module,
                                std::string_view written) -> Type * {
            MatchTypeArm arm;
            arm.shapes = typeShapeBits(subject);
            arm.result = subject;
            auto guard    = std::make_shared<TypeCaseGuard>();
            guard->module = std::string(module);
            guard->text   = std::string(written);
            arm.guard     = std::move(guard);
            const std::vector<MatchTypeArm> arms{arm};
            return MatchTypeType::create(subject, arms);
        };

        Type *first  = guarded(number, "selftest", "is_numeric::<T>()");
        Type *second = guarded(number, "selftest", "is_numeric::<T>()");
        require(
            first == second,
            "two guards that say the same thing are two types: " + show(first) + " and " +
                show(second));
        Type *otherText = guarded(number, "selftest", "is_float::<T>()");
        require(
            first != otherText, "two guards that say different things are one type: " + show(first));
        Type *otherModule = guarded(number, "another", "is_numeric::<T>()");
        require(
            first != otherModule,
            "the same words read in another module are the same guard: " + show(first));
        Type *otherSubject = guarded(text, "selftest", "is_numeric::<T>()");
        require(
            first != otherSubject,
            "the same guard on another subject is one type: " + show(first) + " and " +
                show(otherSubject));
        require(
            first->toString().find("when is_numeric::<T>()") != std::string::npos,
            "a guarded case does not print its guard: " + show(first));
        ++guardReadings;
    }

    // The three other places the type layer tells two things apart by an object it was handed
    // rather than by what they say (`git grep -n "reinterpret_cast<uintptr_t>" -- include/camel/
    // core/type src/core/type`; the guard above was the fourth). Two of the three name an object
    // the process never frees, and the type layer has nothing else to say about it: a named
    // struct's fields are not known when its type is made, so the declaration is the only thing
    // that can be the identity. The third names a node a build owns, and a question page asks
    // whether that is the right thing to name (docs/handover/questions,
    // "a type that names a node its build owns"). These readings pin today's answer so that a
    // change to it is a red rather than a silence.
    {
        // A named struct: one declaration asked twice is one type, and two declarations that spell
        // one name are two (struct.cpp, `declaredKey`).
        Type *integer            = Type::Int64();
        StructDecl *declaration  = StructDecl::create("IdentitySelftestNamed", {}, "selftest");
        StructType *named        = StructType::declare(declaration, {}, {"a"});
        declaration->define({{"a", integer}});
        named->completeFields({{"a", integer}});
        require(
            StructType::get(declaration, {}) == named,
            "one declaration asked twice is two struct types: " + show(named));
        StructDecl *repeated     = StructDecl::create("IdentitySelftestNamed", {}, "selftest");
        StructType *repeatedType = StructType::declare(repeated, {}, {"a"});
        require(
            repeatedType != named,
            "two declarations that spell one name are one struct type: " + show(named));

        // An enum: the same reading, and a declaration's own prototype is one type.
        EnumDecl *enumDeclaration = EnumDecl::create("IdentitySelftestEnum", {}, "selftest");
        EnumType *enumType        = enumDeclaration->prototype();
        require(
            enumDeclaration->prototype() == enumType,
            "one enum declaration asked twice is two enum types: " + show(enumType));
        EnumDecl *enumRepeated     = EnumDecl::create("IdentitySelftestEnum", {}, "selftest");
        EnumType *enumRepeatedType = enumRepeated->prototype();
        require(
            enumRepeatedType != enumType,
            "two enum declarations that spell one name are one enum type: " + show(enumType));

        // `typeof e`: the placeholder names the expression node it was made with, and a clone of
        // it is the same type -- which is what makes the node, and not the text of the expression,
        // the thing that is named (typevar.cpp, `TypeOfType`).
        static const int firstNode  = 0;
        static const int secondNode = 1;
        TypeOfType *nothing         = TypeOfType::create(&firstNode);
        require(
            TypeOfType::create(&firstNode) == nothing,
            "one expression node asked twice is two `typeof` placeholders");
        require(nothing->clone(true) == nothing, "a clone of a `typeof` placeholder is another type");
        require(
            TypeOfType::create(&secondNode) != nothing,
            "two expression nodes are one `typeof` placeholder");
        ++addressReadings;
    }

    return std::format(
        "{} types, {} pairs, {} guard readings, {} address readings",
        types.size(),
        pairs,
        guardReadings,
        addressReadings);
}

} // namespace camel::core::type
