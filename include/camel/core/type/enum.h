// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Sum types: `type Shape = enum { Circle(float), Rect { w: float, h: float }, Empty }`
 * (design principles 5.3).
 *
 * Two objects describe an enum:
 *
 *  - EnumDecl, one per declaration. It holds the name, the type parameters and the variants
 *    as written: field types in terms of the parameters (`Node(Tree<T>, T, Tree<T>)`). An
 *    enum is nominal: two declarations with the same variants are different types.
 *  - EnumType, one per instance: the declaration with its type arguments (`Tree<int>`). The
 *    field types of an instance are the written ones with the arguments put in, computed the
 *    first time they are asked for. They may name the instance itself (a recursive enum), so
 *    they are never computed while the instance is made, and two instances never compare
 *    their variants: equality is the declaration and the arguments.
 *
 * Like `Map` and `Set`, EnumType is an OtherType registered by the core: every backend, the
 * collector, the passes and the exporters meet a value as an opaque traced reference whose
 * object (rtdata::Variant) prints, compares, clones and traces itself by its static type. One
 * type code serves every enum; asEnumType tells an enum from other module types.
 *
 * The prototype of a generic declaration (`Tree`, no arguments) is what the name resolves to
 * before arguments are applied. It is never the type of a value.
 *
 * A pending instance has the hole type `_` among its arguments: the type of a variant whose
 * fields do not determine every argument (`Option::None`, `Either::Left(1)`). Such a value
 * holds nothing of the undetermined type, so its representation is that of every other
 * instance of the declaration; it enters a position of a complete instance by a CAST that
 * changes nothing at run time (entersImplicitly), exactly as `null` enters a `T?`.
 */

#pragma once

#include "camel/core/source/ids.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::type {

class EnumType;

/// How a variant carries its data: nothing (`Empty`), a tuple (`Circle(float)`) or a struct
/// (`Rect { w: float, h: float }`).
enum class VariantShape : uint8_t { Unit, Tuple, Struct };

/// One variant: its name, its shape, and its fields in the order written. Field names are
/// given for the struct shape only. The field types are those of one instance (or, in an
/// EnumDecl, the written ones).
struct EnumVariant {
    std::string name;
    VariantShape shape = VariantShape::Unit;
    std::vector<std::string> fieldNames;
    std::vector<Type *> fieldTypes;
    /// Where each field's type is written, parallel to `fieldTypes`. It is kept for diagnostics
    /// (a field that cannot be a map key is reported at its declaration) and is not part of the
    /// type: two instances of a declaration differ in their fields' types, not in their origins.
    std::vector<camel::source::origin_id_t> fieldOrigins;

    size_t fieldCount() const { return fieldTypes.size(); }
    /// The position of the field `name` of a struct variant.
    std::optional<size_t> findField(std::string_view name) const;
    /// `Circle(float)`, `Rect { w: float, h: float }`, `Empty`: how the variant is written.
    std::string signature() const;
};

/// One `type Name<params> = enum { ... }` declaration. Lives as long as the process (like
/// every type object); never copied.
class EnumDecl {
  public:
    /// A declaration whose variants are not known yet: the name is in scope from here on, so
    /// that the variants (and other declarations of the module) can refer to it. `name` is
    /// the name as written with its namespace path (`Shape`, `geo::Shape`); `module` is the
    /// module that declares the type (an operator on a value of it is looked for there as well,
    /// design principles 5.7).
    static EnumDecl *create(
        const std::string &name, std::vector<std::string> params,
        std::string module = std::string());

    const std::string &name() const { return name_; }
    /// The name the declaring module is loaded under (`models`), empty when the declaration was
    /// made outside a module. Not part of the type's identity: the declaration is.
    const std::string &moduleName() const { return module_; }
    /// The name without its namespace path: what a value prints (`Shape::Circle`).
    const std::string &shortName() const { return shortName_; }
    /// A number no other declaration of the process has: part of the mangled name.
    uint32_t id() const { return id_; }

    /// The parameter names, and the variables standing for them in the written field types.
    const std::vector<std::string> &params() const { return params_; }
    bool generic() const { return !params_.empty(); }

    /// The declaration is written `#resource` (design effects plan 3.1): a value of the type is
    /// a handle, one resource per value, named by this declaration.
    bool handle() const { return handle_; }
    void setHandle(bool handle = true) { handle_ = handle; }

    /// Sets the variants as written. Called once, when the declaration has been read.
    void define(std::vector<EnumVariant> variants);
    bool defined() const { return defined_.load(std::memory_order_acquire); }
    const std::vector<EnumVariant> &variants() const { return variants_; }
    std::optional<size_t> findVariant(std::string_view name) const;

    /// What the name denotes before arguments are applied: the only instance of a declaration
    /// without parameters, the prototype (no arguments) of a generic one.
    EnumType *prototype() const;

  private:
    EnumDecl(const std::string &name, std::vector<std::string> params, std::string module);

    std::string name_;
    std::string shortName_;
    std::string module_;
    uint32_t id_;
    std::vector<std::string> params_;
    std::vector<EnumVariant> variants_;
    std::atomic<bool> defined_{false};
    bool handle_ = false;
};

class EnumType final : public OtherType {
  public:
    /// The one type code of every enum.
    static TypeCode typeCode();

    /// The instance of `decl` with these arguments (one per parameter; none for a declaration
    /// without parameters). Interned on the argument objects, so a recursive field type finds
    /// the instance it is part of. An instance holding a type variable is not interned (the
    /// declaration's binder and a use are different types; see collection.cpp).
    static EnumType *get(EnumDecl *decl, std::span<Type *const> args);

    /// The argument of a pending instance that its value does not determine. Prints `_`.
    static Type *hole();

    EnumDecl *decl() const { return decl_; }
    EnumDecl *declaration() const { return decl_; }
    /// Not the prototype of a generic declaration.
    bool prototype() const { return paramCount_ == 0 && decl_->generic(); }
    /// The arguments are written (not the prototype). A pending instance is complete in this
    /// sense: it has one argument per parameter, some of them the hole.
    bool complete() const { return paramCount_ == decl_->params().size(); }
    /// Some argument is the hole, here or in an enum among the arguments (`Option<Option<_>>`).
    bool pending() const;

    /// The variants of this instance, with its arguments put into the field types. A complete
    /// and non-pending instance only; computed once, on the first call.
    const std::vector<EnumVariant> &variants() const;
    const EnumVariant &variant(size_t tag) const { return variants()[tag]; }
    size_t variantCount() const { return decl_->variants().size(); }
    std::optional<size_t> findVariant(std::string_view name) const {
        return decl_->findVariant(name);
    }
    /// The type of field `index` of variant `tag` in this instance.
    Type *fieldType(size_t tag, size_t index) const { return variant(tag).fieldTypes[index]; }

    bool typeArguments() const override { return true; }
    OtherType *cloneWithParams(std::span<Type *const> params) const override;
    OtherType *
    cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const override;

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    /// The same declaration and equal arguments.
    bool equals(Type *type) const override;
    /// Two instances of one declaration whose arguments differ only where one has the hole:
    /// the instance with the hole filled in from the other (`Option<_>` and `Option<int>` give
    /// `Option<int>`). The arms of a branch, the elements of an array literal.
    Type *unify(Type *other) const override;
    /// From an equal type only, and from a pending instance that this one completes.
    bool assignableFrom(Type *sourceType) const override;
    /// Safe from an equal type and from a pending instance of the same declaration that this
    /// one completes (`Option<_>` to `Option<int>`); forbidden otherwise.
    CastSafety castSafetyFrom(Type *sourceType) const override;
    /// The value itself: an enum value does not depend on the arguments it holds no data of.
    slot_t castSlotFrom(slot_t value, Type *sourceType) const override;

  private:
    EnumType(EnumDecl *decl, std::span<Type *const> args);

    EnumDecl *decl_;
    mutable std::once_flag variantsOnce_;
    mutable std::vector<EnumVariant> variants_;
};

/// `type` as an enum instance (the prototype of a generic declaration excluded), or nullptr.
const EnumType *asEnumType(const Type *type);
EnumType *asEnumType(Type *type);
/// Like asEnumType, also for the prototype.
EnumType *asEnumTypeOrPrototype(Type *type);

/// Whether the hole occurs anywhere in `type` (in the arguments of an enum, the elements of a
/// tuple, array or struct, a nullable or fallible payload): the type of a value whose type was
/// not fully determined where it was written.
bool containsEnumHole(Type *type);

/// Whether a value of the pending enum type `source` enters a position of type `target`: the
/// same declaration, and every argument equal or the hole in `source` (recursively for
/// enums among the arguments).
bool enumEntersFrom(Type *target, Type *source);

} // namespace camel::core::type