/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Pass parameters (design principles 2.17).
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
#include <map>
#include <string>
#include <string_view>
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
};

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

using PassMaker = std::function<
    std::unique_ptr<GraphIRPass>(const camel::core::context::context_ptr_t &, const PassArgs &)>;

/// What a pass declares about itself.
struct PassDescriptor {
    std::string doc;
    std::vector<PassParam> params;
    PassMaker make;
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

/// Registers a pass that joins every pipeline once the module is loaded (the module registers
/// it when the program imports it). `spec` is written as on the command line, parameters
/// included. The passes run before the first pass that consumes the graph (a scheduling or
/// a translation pass), ordered by `priority` (lower first), then by `spec`; one the user
/// listed himself runs only where he wrote it. Registering the same spec again is harmless.
void registerDefaultPipelinePass(const std::string &spec, int priority = 0);

/// `pipeline` with the registered default-pipeline passes inserted before its first consuming
/// pass (at the end when there is none, which the fallback scheduler then follows). Unchanged
/// when `enabled` is false.
std::vector<std::string> withDefaultPipelinePasses(
    const std::vector<std::string> &pipeline, const camel::core::context::context_ptr_t &ctx,
    bool enabled = true);

/// Splits a comma-separated pass list (`--passes a,b(x=1,y=2),c`) at the commas outside
/// parentheses and string literals. Entries are trimmed; empty ones are dropped.
std::vector<std::string> splitPassList(std::string_view text);
