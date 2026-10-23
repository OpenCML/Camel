// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Pass parameters (design principles 9.3).
 *
 * Every pass, scheduler and module pass is named on the pipeline as `name` or as
 * `name(key=value, ...)`. The values are Camel constant expressions (bool, int, float and string
 * literals, and the unary and binary operators over them), read by the language's own parser.
 * A pass declares its parameters in a PassDescriptor; the pipeline checks the written ones
 * against the declaration (unknown name, wrong type, missing value are diagnostics) and hands
 * the factory a PassArgs with every parameter set: the written value or the default.
 */

#pragma once

#include "base.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

enum class PassParamType { Bool, Int, Float, String };

const char *passParamTypeName(PassParamType type);

using PassParamValue = std::variant<bool, int64_t, double, std::string>;

struct PassParam {
    std::string name;
    PassParamType type = PassParamType::Bool;
    PassParamValue defaultValue;
    std::string doc;
    /// A parameter without a default: the pipeline must write it. A pass written in Camel sets
    /// this for each of the function's `with` parameters (they have no default in source).
    bool required = false;
};

/// Declares a pass's parameters the way a function declares its own (design principles 4.4):
///
///     declarePassParams(
///         "safe_fp: bool = false, growth_budget: int = 4096",
///         {{"safe_fp", "keep ..."}, {"growth_budget", "nodes ..."}})
///
/// The list is read by the language's own parser and uses a function's parameter grammar: a
/// name, `:`, a type (`bool`, `int`, `float`, `string`), and a default written `= value`. A
/// default is a constant expression evaluated here, once, where the declaration is (design
/// principles 6.5); a call that leaves the parameter out copies that one value. A parameter
/// written without a default is required, and no required parameter may follow a defaulted one.
///
/// `docs` gives a parameter's help line by name; it is documentation, not part of the
/// declaration. A malformed declaration is a defect of the pass and throws std::runtime_error.
std::vector<PassParam> declarePassParams(
    std::string_view declaration,
    std::initializer_list<std::pair<std::string_view, std::string_view>> docs = {});

/// How many parameter defaults have been evaluated since the process started. A declaration
/// evaluates each of its defaults once; a test reads this to prove that binding a call does
/// not evaluate them again.
size_t passParamDefaultEvaluations();

/// The parameters of one pass invocation, defaults filled in. Asking for a name the pass did
/// not declare, or with the wrong type, is a defect of the pass and throws std::logic_error.
class PassArgs {
  public:
    bool getBool(const std::string &name) const;
    int64_t getInt(const std::string &name) const;
    double getFloat(const std::string &name) const;
    const std::string &getString(const std::string &name) const;
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

/// What a pass declares about itself.
struct PassDescriptor {
    std::string doc;
    std::vector<PassParam> params;
    PassMaker make;
    /// The pass reads tag facts (node attributes, numeric permissions, `read_by`); a tag's
    /// `read_by` names it. `registerModulePass` registers such a pass in the tag reader
    /// registry, which the `TagHasNoReader` warning consults (camel/core/tags.h).
    bool readsTags = false;
};

/// Wraps a factory of a pass without parameters.
PassDescriptor passWithoutParameters(PassFactory factory);

/// Registers a pass contributed by a module under `path` (`<module>::<pass>`), with its
/// parameters. Same rules as registerModulePass(path, PassFactory).
void registerModulePass(const std::string &path, PassDescriptor descriptor);

/// Registers a short name for a pass, normally of the module's own: `registerPassAlias("tiles",
/// "tiles::compile")`. A name of the core (`std::`) cannot be redefined; registering the same
/// alias for the same target again is harmless, for another target it replaces the earlier one.
void registerPassAlias(const std::string &alias, const std::string &target);

/// Splits a comma-separated pass list (`--passes a,b(x=1,y=2),c`) at the commas outside
/// parentheses and string literals. Entries are trimmed; empty ones are dropped.
std::vector<std::string> splitPassList(std::string_view text);
