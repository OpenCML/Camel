// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The C++ entrance's implementation: the items `CppRegistration` states, one setter per item, into
 * the record `Registry::registerOperator` merges (operator_record.h).
 *
 * One item, one statement. A setter that finds its item already stated by this registration refuses
 * with std::invalid_argument naming the operator and the item: two setters of one item are one
 * entrance stating it twice, which is a defect of the registration and not the agreement between
 * two entrances that `Registry::complete` checks.
 *
 * --- what this file does not define, and why ---------------------------------
 * `kernel(void *)` has no definition here. It is the one declared member the reviewed header gives
 * no place to: this class's only state is `Record record_`, and `Record` has no kernel member --
 * operator_record.h says so in as many words ("Not part of the record: a target registers its own
 * against the same id") -- and no accessor hands the pointer to `Registry::registerOperator`.
 * Keeping it in a side table in this file would be a place nothing reads, which is not a definition
 * of the setter. The header needs the field and the reader before this setter can mean anything;
 * neither was added here because operator_record.h is reviewed and a `.cpp` does not get to widen
 * it.
 *
 * Seven setters state an item the record keeps as a bare value with no "somebody stated it"
 * witness: `reusesOperand`, `elementwise`, `readsGraphs`, `macro`, `foldable`, `withAfterNorm` (a
 * bool each) and `laws` (a vector, where an empty vector is not distinguishable from no statement).
 * For those a second statement cannot be told from a repetition, and the later answer stands
 * instead of being refused. `numeric` is refused whenever the semantics are not the "nothing
 * declared" class, which is the one reading of that member a statement can be told from.
 */

#include "camel/ops/operator_record.h"

#include <format>
#include <stdexcept>
#include <utility>

namespace camel::ops {

namespace {

/// One entrance states one item once: the second statement of an item is a defect of the
/// registration, named with the operator and the item it is about.
[[noreturn]] void refuseSecondStatement(std::string_view uri, std::string_view item) {
    throw std::invalid_argument(std::format(
        "{}: the C++ registration states \"{}\" twice; one entrance states an item once",
        uri,
        item));
}

} // namespace

CppRegistration::CppRegistration(std::string_view uri, bool mayFault) {
    // `panic` is the constructor's item, and it is stated whether it is true or false: a C++
    // operator always says one of the two answers (there is no default in the declaration).
    record_.key         = sem::OperatorKey::of(uri);
    record_.panic.value = mayFault;
    record_.panic.byCpp = true;
}

// ---- `uses` ---------------------------------------------------------------------------------

CppRegistration &CppRegistration::pure() {
    // `pure` is the empty access list and not a second field: the two setters state one item.
    return uses(std::span<const sem::ResourceAccess>{});
}

CppRegistration &CppRegistration::uses(std::span<const sem::ResourceAccess> accesses) {
    if (record_.uses.stated()) {
        refuseSecondStatement(record_.key.uri(), "uses");
    }
    record_.uses.value.accesses.assign(accesses.begin(), accesses.end());
    record_.uses.byCpp = true;
    return *this;
}

CppRegistration &CppRegistration::usesFunctionIn(bool isWith, size_t which) {
    // The same item as `pure()` and `uses()`: what the operator touches is the function value's,
    // so the access list is empty and the operand is named instead.
    if (record_.uses.stated()) {
        refuseSecondStatement(record_.key.uri(), "uses");
    }
    record_.uses.value.functionOperand = sem::ResourceBinding{
        isWith ? sem::ResourceBinding::Kind::With : sem::ResourceBinding::Kind::Norm,
        static_cast<uint8_t>(which)};
    record_.uses.byCpp = true;
    return *this;
}

// ---- The type of a call ---------------------------------------------------------------------

CppRegistration &CppRegistration::signature(const type::FunctionType &type, bool fallible) {
    if (record_.signature.stated()) {
        refuseSecondStatement(record_.key.uri(), "signature");
    }
    record_.signature.value = Signature{&type, fallible};
    record_.signature.byCpp = true;
    return *this;
}

CppRegistration &CppRegistration::resolver(type::resolver_ptr_t rule) {
    if (record_.resolver.stated()) {
        refuseSecondStatement(record_.key.uri(), "resolver");
    }
    record_.resolver.value = std::move(rule);
    record_.resolver.byCpp = true;
    return *this;
}

CppRegistration &CppRegistration::folder(TypeFolder folder) {
    if (record_.folder.stated()) {
        refuseSecondStatement(record_.key.uri(), "folder");
    }
    record_.folder.value = std::move(folder);
    record_.folder.byCpp = true;
    return *this;
}

// ---- The properties a pass asks about -------------------------------------------------------

CppRegistration &CppRegistration::inPlaceForm(std::string_view form) {
    // `plain` stays empty: this record is the operator, not its form. The form's own entry is
    // filled from here when the registry completes it (operator_record.h, `InPlaceForm::plain`).
    if (record_.inPlace.has_value()) {
        refuseSecondStatement(record_.key.uri(), "inPlaceForm");
    }
    record_.inPlace = InPlaceForm{form, std::string_view{}};
    return *this;
}

CppRegistration &CppRegistration::reusesOperand(bool reuses) {
    // No witness in the record for a second statement; the later answer stands (file header).
    record_.reusesOperand = reuses;
    return *this;
}

CppRegistration &CppRegistration::handsOnOperandValue(bool onlyOutsideArrays) {
    if (record_.handsOn.has_value()) {
        refuseSecondStatement(record_.key.uri(), "handsOn");
    }
    record_.handsOn = HandsOnValue{onlyOutsideArrays};
    return *this;
}

CppRegistration &CppRegistration::conversion(ConversionTypes types) {
    if (record_.conversion.has_value()) {
        refuseSecondStatement(record_.key.uri(), "conversion");
    }
    record_.conversion = types;
    return *this;
}

CppRegistration &CppRegistration::grant(AttrGrant grant) {
    if (record_.grant != nullptr) {
        refuseSecondStatement(record_.key.uri(), "grant");
    }
    record_.grant = std::make_shared<const AttrGrant>(std::move(grant));
    return *this;
}

CppRegistration &CppRegistration::callSpec(CallSpec spec) {
    if (record_.callSpec.has_value()) {
        refuseSecondStatement(record_.key.uri(), "callSpec");
    }
    record_.callSpec = std::move(spec);
    return *this;
}

CppRegistration &CppRegistration::callForm(CallForm form) {
    if (record_.callForm.has_value()) {
        refuseSecondStatement(record_.key.uri(), "callForm");
    }
    record_.callForm = std::move(form);
    return *this;
}

// ---- The single statements of what was stated twice -----------------------------------------

CppRegistration &CppRegistration::elementwise() {
    record_.elementwise = true;
    return *this;
}

CppRegistration &CppRegistration::calls(sem::ElementCalls shape) {
    if (record_.elementCalls.has_value()) {
        refuseSecondStatement(record_.key.uri(), "elementCalls");
    }
    record_.elementCalls = std::move(shape);
    // The record's own reading: an operator with an `elementCalls` statement calls graphs.
    record_.callsGraphs = true;
    return *this;
}

CppRegistration &CppRegistration::readsGraphs() {
    record_.readsGraphs = true;
    return *this;
}

CppRegistration &CppRegistration::macro() {
    record_.macro = true;
    return *this;
}

CppRegistration &CppRegistration::foldable(bool foldable) {
    record_.foldable = foldable;
    return *this;
}

CppRegistration &CppRegistration::withAfterNorm() {
    record_.withAfterNorm = true;
    return *this;
}

// ---- What it computes -----------------------------------------------------------------------

CppRegistration &CppRegistration::indexing(sem::InstantiateFn instantiate) {
    if (static_cast<bool>(record_.indexing)) {
        refuseSecondStatement(record_.key.uri(), "indexing");
    }
    record_.indexing = std::move(instantiate);
    return *this;
}

CppRegistration &CppRegistration::laws(std::vector<sem::ReductionLaw> laws) {
    // An empty list is not distinguishable from no statement; see the file header.
    record_.laws = std::move(laws);
    return *this;
}

CppRegistration &CppRegistration::numeric(sem::NumericSemantics numeric) {
    // `Opaque` is the record's own word for "nothing declared", so it is the one class that does
    // not count as a statement here.
    if (record_.numeric.cls != sem::NumericClass::Opaque) {
        refuseSecondStatement(record_.key.uri(), "numeric");
    }
    record_.numeric = std::move(numeric);
    return *this;
}

CppRegistration &CppRegistration::algebra(BinaryLaws laws) {
    if (record_.binary.has_value()) {
        refuseSecondStatement(record_.key.uri(), "binary");
    }
    record_.binary = std::move(laws);
    return *this;
}

CppRegistration &CppRegistration::definition(sem::Definition definition) {
    if (record_.definition.has_value()) {
        refuseSecondStatement(record_.key.uri(), "definition");
    }
    record_.definition = std::move(definition);
    return *this;
}

// ---- The items stated so far ----------------------------------------------------------------

const Record &CppRegistration::stated() const { return record_; }

} // namespace camel::ops