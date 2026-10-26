// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The resources of an operator (docs/design/op-semantics, section 8): what an operator reads
 * and writes besides its operands' values, declared as data so that a scheduler can tell two
 * effects that touch the same thing from two that do not.
 *
 * Only some things have an identity a program can observe (design principles 10.5): a `var`
 * cell, an object changed in place through a `var` parameter or an in-place form, an object a
 * module wraps (Tiles' device memory, a Python object). Every other value can be copied or
 * shared at will and is not a resource. Besides those, some state is named by no value at all:
 * the file system, the console, the random generator of a module. A declaration says, per
 * access:
 *
 *   resource   the class of the thing (ResourceClass) and, for a module's object or for state
 *              no value names, which one (`tiles:device`, `fs`, `rng`)
 *   binding    which operand names it (an ordinary or a with operand; a capture in the
 *              footprint of a graph), or none for ambient state
 *   region     which part of it: the whole, or the elements an access map of the node's index
 *              instance reaches (camel/core/index_semantics.h); unknown reads as whole
 *   mode       read, write or read-modify-write
 *
 * and, for the operator as a whole, how its result relates to the resources (a new object,
 * one of its operands, a view of one, possibly shared with one), whether the result is a new
 * resource (`:var/new` makes a cell, `tiles:upload` device memory), and whether running it is
 * visible outside the program (I/O, the clock, the generators: externalEffect).
 *
 * Three relations are kept apart (design 8.2): computation dependence is the graph's data
 * edges; a resource conflict is two accesses to overlapping parts of one resource, at least one
 * of them a write, which the queries of camel/runtime/resources.h find; asynchronous
 * completion is not the core's concern (a node's result is there when it has run). `sync` keeps
 * its meaning: a sync body is how the front end orders conflicting effects in the graph
 * (control edges), never a synchronisation primitive of a device.
 *
 * An operator that declares nothing is read conservatively (conservativeResources): a pure one
 * touches no resource and returns a new value; any other reads, writes and may return every
 * operand, and has an external effect when its traits say so.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {
class Type;
}

namespace camel::core::sem {

/// The class of a resource (design principles 10.5, "有身份的东西要列明").
enum class ResourceClass : uint8_t {
    /// Any of the others: what an undeclared operator may touch.
    Unknown = 0,
    /// A `var` cell: `:var/new` makes one; var parameters and captured variables pass it on.
    Variable,
    /// An object changed in place: the array an in-place form (OperatorTable::setInPlaceForm)
    /// changes.
    Object,
    /// An object a module wraps; the resource's name says whose (`tiles:device`).
    Foreign,
    /// State no value names; the resource's name says which (`fs`, `stdout`, `rng`). Two ambient
    /// resources are the same exactly when their names are.
    Ambient,
    /// A value with identity a handle type declares (`#resource type Stream<T>`); the resource's
    /// name is the declaring type's qualified name, and the value node tells one handle from
    /// another (design effects plan 3.2). Two handles of one declaration are two resources.
    Handle,
};

std::string_view resourceClassName(ResourceClass cls);

/// A resource as a declaration names it: its class, and the name a Foreign or an Ambient
/// resource has (empty for the others).
struct Resource {
    ResourceClass cls = ResourceClass::Unknown;
    std::string name;

    bool operator==(const Resource &) const = default;
};

/// Which value names the resource an access is to.
struct ResourceBinding {
    enum class Kind : uint8_t {
        Norm,    // the ordinary operand `index`
        With,    // the with operand `index`
        Capture, // the capture `index` of a graph (a graph's footprint, camel/runtime/resources.h)
        Ambient, // no value: the resource is named by its name (ResourceClass::Ambient)
    };
    Kind kind     = Kind::Norm;
    uint8_t index = 0;

    bool operator==(const ResourceBinding &) const = default;
};

/// Which part of the resource an access reaches.
struct ResourceRegion {
    enum class Kind : uint8_t {
        Whole,   // all of it
        Mapped,  // the elements that one map of the node's index instance reaches over its domain
        Unknown, // not declared: read as Whole
    };
    /// Mapped: the access map of the norm or with operand `operand`, or the write map.
    enum class Map : uint8_t { Norm, With, Write };

    Kind kind       = Kind::Whole;
    Map map         = Map::Write;
    uint8_t operand = 0;

    bool operator==(const ResourceRegion &) const = default;
};

enum class AccessMode : uint8_t { Read, Write, ReadModifyWrite };

std::string_view accessModeName(AccessMode mode);

/// True for Write and ReadModifyWrite.
inline bool writes(AccessMode mode) { return mode != AccessMode::Read; }

struct ResourceAccess {
    Resource resource;
    ResourceBinding binding;
    ResourceRegion region;
    AccessMode mode = AccessMode::ReadModifyWrite;

    bool operator==(const ResourceAccess &) const = default;
};

/// How the operator's result relates to the resources its operands name.
struct ResultAlias {
    enum class Kind : uint8_t {
        Fresh,    // a new object that nothing else refers to (or a value without identity)
        Same,     // the norm operand `operand` itself (an in-place form returns its array)
        View,     // a window of the norm operand `operand`, given by the instance's write map
        MayAlias, // possibly the norm operand `operand`, or an object it holds; 0xFF: any operand
        Unknown,  // nothing is known: may be or share anything the operands reach
    };
    Kind kind       = Kind::Fresh;
    uint8_t operand = 0xFF;

    bool operator==(const ResultAlias &) const = default;
};

std::string_view resultAliasName(ResultAlias::Kind kind);

/**
 * The resources of an operator (design 8.1). A spec without access, allocation or external
 * effect is the declaration of an operator that touches no resource.
 */
struct ResourceSpec {
    std::vector<ResourceAccess> accesses;
    ResultAlias result;
    /// The result is a new resource of this class (`:var/new` makes a Variable, `tiles:upload`
    /// a Foreign `tiles:device`); nullopt when it is not one.
    std::optional<Resource> allocates;
    /// Running the operator is visible outside the program (I/O, clocks, generators, the
    /// process): such operators keep the order the program gives them among themselves.
    bool externalEffect = false;

    /// Touches no resource: no access, nothing allocated, no external effect (how the result
    /// relates to the operands' resources is not a touch).
    bool none() const { return accesses.empty() && !allocates && !externalEffect; }
    bool operator==(const ResourceSpec &) const = default;
};

/**
 * The resources a function type publishes (design effects plan 2.2): what a call to a function
 * of this type touches, named in the callee's parameter numbering (with parameters first, then
 * ordinary ones, as `varMarks` numbers them).
 *
 * `Generic` is a type without a resource clause: it promises nothing, and a call through it is
 * read conservatively. `Concrete` is what a declaration or an inferred body says; `pure` is the
 * Concrete signature with nothing at all.
 *
 * A parameter use keeps the class and the mode of the declared access (`uses s` will be a
 * Handle; a `var` parameter a Variable), because the runtime maps each use to the call's value
 * and the class decides which values may be the same resource.
 */
struct ResourceSignature {
    enum class Mode : uint8_t { Generic, Concrete };

    struct ParamUse {
        /// The parameter, with parameters first and ordinary ones after them (0..withCount-1,
        /// then withCount..).
        uint8_t index = 0;
        ResourceClass cls = ResourceClass::Unknown;
        /// The name a Handle or Foreign resource carries (the declaration's qualified name).
        std::string name;
        AccessMode mode = AccessMode::ReadModifyWrite;

        bool operator==(const ParamUse &) const = default;
    };

    struct AmbientUse {
        std::string name;
        AccessMode mode = AccessMode::ReadModifyWrite;

        bool operator==(const AmbientUse &) const = default;
    };

    Mode mode = Mode::Generic; // the default: a type that says nothing
    /// Sorted by parameter index; at most one use per parameter.
    std::vector<ParamUse> params;
    /// The captures used, by capture position (only an inferred closure type has them).
    std::vector<uint8_t> captures;
    /// Sorted by name; at most one use per ambient resource.
    std::vector<AmbientUse> ambient;
    /// Classes the call may touch without naming a value (`anyValue`).
    std::vector<ResourceClass> anyOf;
    ResultAlias result;
    /// The call's result is a new resource of this class.
    std::optional<Resource> allocates;
    /// Running the call is visible outside the program. An ambient resource is the state the
    /// program does not own, so a signature that names one carries this too: writing `stdout` in a
    /// clause promises the effect that comes with it (design principles 3.3).
    bool external = false;

    /// Concrete and touching nothing: `pure`.
    bool pure() const {
        return mode == Mode::Concrete && params.empty() && captures.empty() && ambient.empty() &&
               anyOf.empty() && !allocates && !external;
    }
    bool operator==(const ResourceSignature &) const = default;
};

/// The signature a declared spec publishes, in the parameter numbering of a function with
/// `withCount` with parameters (with parameters first, ordinary ones after them).
ResourceSignature signatureOf(const ResourceSpec &spec, size_t withCount);

/// What naming a resource in a signature means, beyond the resource itself: an ambient resource is
/// state the program does not own, reached without being handed it, so a signature that names one
/// also runs visibly outside the program (design principles 3.3). A written clause has no word of
/// its own for that component, so the resource has to bring it -- otherwise a clause could never
/// cover the body that reaches the very resource it names. Both the signature a spec publishes and
/// the one a written clause builds end with this, so the two agree.
inline void ambientCarriesExternal(ResourceSignature &signature) {
    if (!signature.ambient.empty()) {
        signature.external = true;
    }
}

/// Whether a call that touches `source` may stand where `target` is promised: one signature
/// covers another when it allows no less, so the caller is told at least what it gets.
///
/// This is the whole of the resource half of function assignability (design effects plan 2.2,
/// design principles 3.3), and it is what "the resources the argument infers are within the
/// resources the parameter asks for" means at a call. A `Generic` target promises nothing and
/// covers anything; a `Generic` source cannot be proved to fit a `Concrete` target, so it does
/// not cover it; a `pure` target covers only a source that touches nothing.
bool covers(const ResourceSignature &target, const ResourceSignature &source);

/// The ambient resources the language gives every program (design principles 3.3): `stdout`,
/// `stderr`, `stdin`, `clock`, `env` and `fs`. They need no declaration and every module sees
/// them; a module may still write `resource fs` to say it uses it, and a group may name them.
std::span<const std::string_view> standardResourceNames();
bool isStandardResource(std::string_view name);

/// Whether values of `type` are handles (design effects plan 3.1): the type is a nominal type
/// declaration written `#resource`. A handle is one resource per value; two values of the type
/// are two resources.
bool isHandleType(const type::Type *type);
/// The resource name a handle of `type` carries: the declaration's qualified name
/// (`module::Type`), empty when `type` is not a handle type.
std::string handleResourceName(const type::Type *type);

/// The text form of a signature: "" for Generic, "pure", "uses s, fs".
std::string toString(const ResourceSignature &signature);

// ---- Builders for declarations --------------------------------------------------------------

/// An access to the resource named by the ordinary operand `operand`, whole.
ResourceAccess
operandAccess(ResourceClass cls, uint8_t operand, AccessMode mode, std::string name = {});
/// An access to the ambient resource `name`.
ResourceAccess ambientAccess(std::string name, AccessMode mode);
/// The spec of an operator whose one touch is the ambient resource `name`, with an effect
/// visible outside the program: a file system call, the console, the clock, a generator.
ResourceSpec externalAmbient(std::string name, AccessMode mode);

/**
 * The conservative reading of an operator without a declaration (design 4.2, 8.1): nothing for
 * a pure one; otherwise a read-modify-write of every operand, whole and of unknown class, a
 * result that may alias any operand, and the external effect the traits give.
 */
ResourceSpec
conservativeResources(size_t normCount, size_t withCount, bool pure, bool externalEffect);

/// The problem with a spec in words (design 4.4), nullopt when it is consistent in itself: a
/// Foreign or Ambient resource has a name and the others have none; an ambient access is bound
/// to no operand and every other access is; an Unknown class is not declared (it is the
/// conservative reading, not a declaration); a Same or View result names an operand.
std::optional<std::string> checkResources(const ResourceSpec &spec);

/// Whether the spec writes the resource that the ordinary (or with) operand `index` names.
bool writesOperand(const ResourceSpec &spec, bool isWith, size_t index);

/// Whether the spec reads or writes a resource of class `cls` at all (`writesOnly`: writes).
bool touchesClass(const ResourceSpec &spec, ResourceClass cls, bool writesOnly = false);

/// The declaration against the `var` parameters of the operator's signature (design principles
/// 3.3: the resource declaration is the truth about effects, the signature is checked against
/// it): nullopt when they agree. A `var` parameter is declared written; a cell (Variable) or an
/// object changed in place (Object) is written only through a `var` parameter; a Foreign
/// resource is a handle a module wraps and may be written through an ordinary operand.
std::optional<std::string> checkVarParameters(
    const ResourceSpec &spec, const std::vector<bool> &withVars, const std::vector<bool> &normVars);

/// The text form, for `camel inspect --ops` and diagnostics: "rmw variable norm 0 whole; result
/// fresh", "read ambient fs; external", "none".
std::string toString(const ResourceSpec &spec);
std::string toString(const ResourceAccess &access);

} // namespace camel::core::sem
