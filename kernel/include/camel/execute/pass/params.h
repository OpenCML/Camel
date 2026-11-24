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
 */

#pragma once

#include "base.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

enum class PassParamType { Bool, Int, Float, String, IntArray };

/// The type as a declaration writes it, with the `?` a nullable parameter carries: `bool`, `int`,
/// `float`, `string`, `int[]`, `int[]?`, `string?`.
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

    /// Used by the pipeline parser and by tests.
    void set(const std::string &name, PassParamValue value, bool written);

  private:
    const PassParamValue &at(const std::string &name) const;
    std::map<std::string, PassParamValue> values_;
    std::map<std::string, bool> written_;
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

/// Registers a short name for a pass, normally of the module's own: `registerPassAlias("tiles",
/// "tiles::compile")`. A name of the core (`std::`) cannot be redefined; registering the same
/// alias for the same target again is harmless, for another target it replaces the earlier one.
void registerPassAlias(const std::string &alias, const std::string &target);

/// Splits a comma-separated pass list (`--passes std::opt safe_fp=true,std::fvm`) at the commas
/// outside string literals. Entries are trimmed; empty ones are dropped.
std::vector<std::string> splitPassList(std::string_view text);

/// Groups the words of a pipeline into specifications: a word with `=` is a parameter of the
/// pass before it, and every other word starts a new pass. `first` is the first word that is a
/// pass (the positional input file is not).
std::vector<std::string> groupPassWords(const std::vector<std::string> &words, size_t first = 0);