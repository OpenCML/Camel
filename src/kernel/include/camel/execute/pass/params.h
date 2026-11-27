// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Pass parameters (design principles 9.3).
 *
 * A pass is a function of the standard library: its declaration is an ordinary body-less Camel
 * function, and its parameters are the declaration's `with` parameters. The pipeline names a pass
 * as a word of its own and writes each parameter after it as `key=value`:
 *
 *     camel m.cml std::opt safe_fp=true growth_budget=8192 std::fvm
 *
 * The values are Camel constant expressions (the literals of the declaration's type, `null`, and
 * the unary and binary operators over them), read by the language's own parser. Which constants a
 * word may be is the *declared* type's business and not the word's (design principles 9.3): a
 * `bool`, `int` or `float` parameter takes a constant of its type, a `string` one the word as
 * written, an `int[]` one an array constant, and a nullable `T?` one also takes `null`. The
 * pipeline checks the written ones against the declaration (unknown name, wrong type, missing
 * value are diagnostics) and hands the factory a PassArgs with every parameter set: the written
 * value or the default the declaration evaluates once, where it is.
 *
 * One type is not a constant: a function parameter (`cost: (n: Node, inputs: float[]) => float`)
 * takes the *function value* its word names, written `<module>::<function>` -- the same value
 * `rewrite::extract<cost>` takes. The pipeline resolves the name while it reads the call, so a
 * name that is no function is a diagnostic of the call at the position of the word, and the pass
 * reads the value back with `getFunction`, which answers the value itself and never the text.
 * A function parameter has no default: every call writes it.
 */

#pragma once

#include "base.h"

#include "camel/core/oper_id.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class PassParamType { Bool, Int, Float, String, IntArray, Function };

/// The type as a declaration writes it, with the `?` a nullable parameter carries: `bool`, `int`,
/// `float`, `string`, `int[]`, `int[]?`, `string?`, `function`.
std::string passParamTypeName(PassParamType type, bool nullable = false);

/// `std::monostate` is `null`: the value of a parameter declared `T?` that no word gave and that
/// the declaration's default does not fill either.
using PassParamValue =
    std::variant<bool, int64_t, double, std::string, std::vector<int64_t>, std::monostate>;

/// A value as the declaration's type writes it: `true`, `5`, `1.5`, `"x"`, `[0, 1]`, and `null`
/// for the value a nullable parameter has when it is not there.
std::string passParamValueText(const PassParamValue &value);

struct PassParam {
    std::string name;
    PassParamType type = PassParamType::Bool;
    PassParamValue defaultValue;
    std::string doc;
    /// A parameter without a default: the pipeline must write it.
    bool required = false;
    /// The declaration wrote `T?`: `null` is a value of this parameter, so leaving it out is not
    /// the same as a parameter that has no value at all.
    bool nullable = false;
    /// The type as the declaration wrote it, when that says more than `type` does: a function
    /// parameter's `(n: Node, inputs: float[]) => float`. Empty for the scalar types, whose name
    /// `passParamTypeName` gives.
    std::string written = {};
};

/// A function-valued parameter, resolved: the qualified name as the call wrote it and the
/// function value it names.
struct PassFunction {
    std::string name; // `<module>::<function>`, as written
    slot_t value = 0; // the function value (`::Function *`)
    /// The function's type, as the library declares it (`(n: Node, inputs: float[]) => float` for
    /// `rules::nodes`): what a pass hands on when it gives the value to something that calls it,
    /// rather than calling it itself (`rewrite::extract` builds a cost model over the value).
    camel::core::type::Type *type = nullptr;
    /// Keeps the value alive, and where it is after every safepoint, while this argument lives.
    /// The pipeline makes it; a pass only reads `value`.
    std::shared_ptr<void> root;
};

/// The parameters of one pass invocation, defaults filled in. Asking for a name the pass did
/// not declare, or with the wrong type, is a defect of the pass and throws std::logic_error.
class PassArgs {
  public:
    bool getBool(const std::string &name) const;
    int64_t getInt(const std::string &name) const;
    double getFloat(const std::string &name) const;
    const std::string &getString(const std::string &name) const;
    const std::vector<int64_t> &getIntArray(const std::string &name) const;
    /// Whether the value is `null`: the parameter is declared `T?` and no word gave it, or the
    /// word `null` did. Every getter above refuses a `null` value, so a parameter that may be
    /// null is asked this first.
    bool isNull(const std::string &name) const;
    /// The names this invocation carries, in name order: every parameter of the declaration, the
    /// written ones and the ones that fell back to their default.
    std::vector<std::string> names() const;
    /// The value as the declaration's type writes it (`[0, 1]`, `null`, `"x"`, `5`).
    std::string text(const std::string &name) const;
    /// Whether the pipeline wrote the parameter (as against the default being used).
    bool given(const std::string &name) const;

    /// The function a function-valued parameter was resolved to: its value, and the qualified name
    /// the call wrote. Throws std::logic_error when the parameter was never resolved, which is a
    /// defect of the pass (it does not fall back to the text: the text is not the value).
    const PassFunction &getFunction(const std::string &name) const;
    /// Whether this parameter was resolved to a function value, for a pass that reads the
    /// parameters a declaration gave without knowing their types one by one.
    bool isFunction(const std::string &name) const;

    /// Used by the pipeline parser and by tests.
    void set(const std::string &name, PassParamValue value, bool written);
    /// Used by the pipeline, when it resolves a function-valued parameter.
    void setFunction(const std::string &name, PassFunction function);

  private:
    const PassParamValue &at(const std::string &name) const;
    std::map<std::string, PassParamValue> values_;
    std::map<std::string, bool> written_;
    std::map<std::string, PassFunction> functions_;
};

using PassMaker = std::function<std::unique_ptr<GraphIRPass>(
    const camel::core::context::context_ptr_t &, const PassArgs &)>;

/// What a pass contributes to the pipeline: how to make it. Its name, documentation and
/// parameters are the standard library's declaration, not a second declaration in C++.
struct PassDescriptor {
    PassMaker make;
    /// The parameters of a pass with no `.cml` declaration to read them from: a test-only pass, or a
    /// pass written in Camel, whose declaration is the `with` list of the function itself. A
    /// built-in pass's parameters are its declaration's `with` list, and the pipeline never reads
    /// this field for one.
    std::vector<PassParam> params;
    /// The pass reads tag facts (node attributes, numeric permissions, `read_by`); a tag's
    /// `read_by` names it. `registerModulePass` registers such a pass in the tag reader
    /// registry, which the `TagHasNoReader` warning consults (camel/core/tags.h).
    bool readsTags = false;
};

/// Wraps a factory of a pass without parameters.
PassDescriptor passWithoutParameters(PassFactory factory);

/// Registers the implementation of a pass under its function path (`<module>::<pass>`): the
/// core's kernels under `std::`, a module's under its own name. The declaration of the pass, with
/// its documentation and parameters, is the module's `.cml` file; the core registers its kernels
/// through the same path, so there is one registry of implementations and no separate pass
/// registry.
void registerModulePass(const std::string &path, PassDescriptor descriptor);

/// Registers the kernel of the pass the operator `pass` names.
///
/// The operator is the function's identity in the operator table (`OperatorTable::intern` of
/// `<module>::<pass>`, the same id a call of that function has), so a pass is registered once, by
/// what it is, and not once per spelling of its name: a name table beside the operator table is
/// the second truth this replaces. A pass's declaration -- its name, documentation, parameters and
/// applicability -- stays the module's `.cml` file; this registers only the kernel.
///
/// One registration per operator. A second one for the same id is refused (`ASSERT`): it is the
/// same pass declared twice, which two kernels can never answer consistently.
void registerPassKernel(camel::core::OperId pass, PassDescriptor descriptor);

/// The kernel registered for `pass`, or nullopt when nothing registered one: the pass the
/// operator names is a declaration the core has no implementation of.
std::optional<PassDescriptor> findPassKernel(camel::core::OperId pass);

/// Splits a comma-separated pass list (`--passes std::opt safe_fp=true,std::fvm`) at the commas
/// outside string literals. Entries are trimmed; empty ones are dropped.
std::vector<std::string> splitPassList(std::string_view text);

/// Groups the words of a pipeline into specifications: a word with `=` is a parameter of the
/// pass before it, and every other word starts a new pass. `first` is the first word that is a
/// pass (the positional input file is not).
std::vector<std::string> groupPassWords(const std::vector<std::string> &words, size_t first = 0);