// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/type_identity_selftest.h"

#include "camel/core/type/base.h"
#include "camel/core/derivative.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/extent.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/text.h"

#include <format>
#include <span>
#include <stdexcept>
#include <string>
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

ExtentForm dim(int64_t constant, Terms terms) { return ExtentForm::linear(constant, std::move(terms)); }

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
    Type *i = Type::Int64();
    Type *f = Type::Float64();
    Type *s = Type::String();
    std::vector<Type *> out = {
        Type::Int32(),      Type::Int64(),   Type::Float32(),  Type::Float64(), Type::Bool(),
        Type::String(),     Type::Void(),    Type::Complex64(), Type::Complex128(),
        Type::BFloat16(),   Type::Float16(),
        ArrayType::create(f), arrayOf(f, {3}), arrayOf(f, {4}), arrayOf(f, {3, 4}), ArrayType::create(i),
        ArrayType::create(ArrayType::create(i)),
        TupleType::create(std::vector<Type *>{}),
        TupleType::create(std::vector<Type *>{i}),
        TupleType::create(std::vector<Type *>{i, s}),
        TupleType::create(std::vector<Type *>{arrayOf(i, {2}), i}),
        TupleType::create(std::vector<Type *>{arrayOf(i, {3}), i}),
        TupleType::nullable(i), TupleType::nullable(arrayOf(f, {2})), TupleType::nullable(arrayOf(f, {3})),
        TupleType::fallible(i), TupleType::fallible(s),
        structOf({{"x", i}, {"y", i}}), structOf({{"y", i}, {"x", i}}), structOf({{"x", i}}),
        structOf({{"x", arrayOf(i, {2})}}), structOf({{"x", arrayOf(i, {3})}}),
        StreamType::create(i), StreamType::create(s),
        functionOf({i}, i), functionOf({i}, f), functionOf({i, i}, i), functionOf({}, i),
        functionOf({i}, Type::Void()), functionOf({i}, arrayOf(i, {2})), functionOf({i}, arrayOf(i, {3})),
        functionOf({i}, i, {i}), functionOf({i}, i, {i}, true),
        linearArray(f, {dim(0, {{"n", 1}})}), linearArray(f, {dim(0, {{"m", 1}})}),
        linearArray(f, {dim(1, {{"n", 1}})}), linearArray(f, {dim(0, {{"n", 2}})}),
        linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}}), dim(0, {{"m", 1}})}),
        linearArray(f, {dim(0, {{"m", 1}}), dim(0, {{"n", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}, {"m", 1}})}), linearArray(f, {dim(0, {{"a", 1}, {"b", 1}})}),
        linearArray(f, {dim(0, {{"n", 1}}), ExtentForm::constant(4)}),
    };
    return out;
}

} // namespace

std::string debugRunTypeIdentitySelfTest() {
    const std::vector<Type *> types = corpus();
    size_t pairs = 0;

    for (Type *a : types) {
        // reflexive: a type is itself, and assignable from itself
        require(a->equals(a), "equals is not reflexive for " + show(a));
        require(a->assignableFrom(a), "assignableFrom is not reflexive for " + show(a));
        // an unknown source is accepted by no type: the answer cannot depend on the target
        require(!a->assignableFrom(nullptr), "assignableFrom(nullptr) is true for " + show(a));
        require(a->unify(a) == a || (a->unify(a) != nullptr && a->unify(a)->equals(a)),
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
                        "assignability is not transitive: " + show(a) + " <- " + show(b) +
                            " <- " + show(c));
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
            "unify(" + show(a) + ", " + show(b) + ") = " + show(joined) + ", not " + show(expected));
    };

    // an extent is a linear form over names, and its equality is decidable: the same form up to
    // the names (design principles 5.4). Dimensions that relate differently are different types.
    const auto n  = linearArray(f, {dim(0, {{"n", 1}})});
    const auto m  = linearArray(f, {dim(0, {{"m", 1}})});
    const auto n1 = linearArray(f, {dim(1, {{"n", 1}})});
    const auto n2 = linearArray(f, {dim(0, {{"n", 2}})});
    require(n->equals(m) && sameCanonicalType(n, m), "f32[n] and f32[m] are not one type");
    require(!n->equals(n1) && !sameCanonicalType(n, n1), "f32[n] and f32[n + 1] are one type");
    require(!n->equals(n2) && !n1->equals(n2), "f32[n], f32[n + 1] and f32[2 * n] are not three types");
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
    require(n->mangle() != n1->mangle() && n->mangle() == m->mangle(), "the mangled name does not follow the identity");
    expectJoin(n, n, n);
    expectJoin(n, n1, ArrayType::create(f));
    require(n->assignableFrom(n1) && n1->assignableFrom(n), "a named extent asks for nothing at the type level");

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
        TupleType::nullable(arrayOf(f, {2})), TupleType::nullable(arrayOf(f, {3})),
        TupleType::nullable(ArrayType::create(f)));
    expectJoin(
        TupleType::fallible(arrayOf(f, {2})), TupleType::fallible(arrayOf(f, {3})),
        TupleType::fallible(ArrayType::create(f)));
    expectJoin(
        structOf({{"x", arrayOf(i, {2})}, {"y", s}}), structOf({{"x", arrayOf(i, {3})}, {"y", s}}),
        structOf({{"x", ArrayType::create(i)}, {"y", s}}));
    expectJoin(
        functionOf({i}, arrayOf(i, {2})), functionOf({i}, arrayOf(i, {3})),
        functionOf({i}, ArrayType::create(i)));
    require(
        TupleType::create(std::vector<Type *>{i, s})->unify(TupleType::create(std::vector<Type *>{s, i})) ==
            nullptr,
        "tuples of other members unify");
    require(
        TupleType::create(std::vector<Type *>{i})->unify(TupleType::create(std::vector<Type *>{i, i})) ==
            nullptr,
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
        Type::Int32(),  Type::Int64(),    Type::Float32(),   Type::Float64(), Type::Bool(),
        Type::String(), Type::Complex64(), Type::Complex128(), Type::BFloat16(), Type::Float16(),
    };
    for (Type *to : primitives) {
        for (Type *from : primitives) {
            const CastSafety safety = to->castSafetyFrom(from);
            if (to->equals(from)) {
                require(safety == CastSafety::Safe, "`" + show(from) + " as " + show(to) + "` is refused");
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
        Type *named  = linearArray(f, {dim(0, {{"n", 1}})});
        Type *other  = linearArray(f, {dim(0, {{"m", 1}})});
        Type *tangent = camel::core::gradientTypeOf(named);
        require(tangent != nullptr, "an array of floats has no gradient type");
        require(
            tangent == camel::core::gradientTypeOf(other),
            "f64[n] and f64[m] have two gradient types");
    }

    // and two spellings of one type share the answer (the cache is keyed by canonical id)
        Type *pair  = structOf({{"x", i}, {"y", f}});
        Type *again = structOf({{"x", i}, {"y", f}});
        require(isPureDataType(pair) && pureDataLayout(pair) == pureDataLayout(again), "a pure struct has two layouts");
    }

    return std::format("{} types, {} pairs", types.size(), pairs);
}

} // namespace camel::core::type
