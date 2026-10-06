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
 * Created: Oct. 06, 2026
 * Updated: Oct. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Arguments of the entry function, given from outside the program.
 *
 * `main` may take parameters, ordinary ones and `with` ones. The entry graph takes the same
 * parameters and passes them on (see Builder::build), so a program whose `main` has parameters
 * compiles to an entry graph with ports. A value given from outside (`--arg name=value` on the
 * command line) is bound here: the port becomes a constant of the entry graph, exactly as a
 * capture is bound when a function is specialized. Binding is partial application: parameters
 * that get no value stay ports.
 *
 *   - A scheduling pass needs every parameter bound; the pass driver refuses to run an entry
 *     graph that still has ports and names them (RuntimeDiag::EntryArgumentMissing).
 *   - A translation pass decides for itself what a remaining port means (onnx::export makes it
 *     an input of the model).
 *
 * How the text of a value is read depends on the declared type of the parameter:
 *   int / float / bool / string   the literal itself (`3`, `0.5`, `true`, any text);
 *   a struct or tuple             a directory: every leaf is read from
 *                                 `<dir>/<field.path>` (fields and tuple indices joined by
 *                                 dots, the layout nn.save_params writes);
 *   a type defined by a module    the loader the module registered for it
 *                                 (registerEntryArgumentLoader; the tensor module reads a
 *                                 `.npy` file for a Tensor parameter).
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace camel::execute {

/// One `name=value` given from outside.
struct EntryArgument {
    std::string name;
    std::string text;
};

/// Splits `name=value`. Returns false when there is no `=` or the name is empty.
bool parseEntryArgument(const std::string &spec, EntryArgument &out);

/**
 * Reads a value of a module-defined type from its command-line text (a file path, usually).
 * `declared` is the type of the parameter (or of the struct field) the value is for; the loader
 * checks the value against it and stores the exact type of what it read in `valueType`. The
 * result may be a fresh object of the auto space. Failure is an exception (std::exception or
 * a RuntimeFault) whose message says why.
 */
using EntryArgumentLoader = std::function<
    slot_t(const std::string &text, core::type::Type *declared, core::type::Type *&valueType)>;

/// Registers the loader for the values of the type with code `code`. Modules call this when
/// they are loaded; registering a code again replaces the earlier loader.
void registerEntryArgumentLoader(core::type::TypeCode code, EntryArgumentLoader loader);

/**
 * Binds `arguments` to the parameters of the entry graph `root` and returns the entry graph
 * that results (`root` itself when there is nothing to bind). A name that is not a parameter,
 * a name given twice or a value that cannot be read as the parameter's type is reported as
 * RuntimeDiag::EntryArgumentInvalid in the runtime diagnostics, located at `main`; nullptr is
 * returned then.
 */
runtime::GCGraph *bindEntryArguments(
    const core::context::context_ptr_t &context, runtime::GCGraph *root,
    std::span<const EntryArgument> arguments);

/// The same for arguments as written on a command line (`name=value` each). A text without
/// `=` is reported like any other invalid argument.
runtime::GCGraph *bindEntryArgumentSpecs(
    const core::context::context_ptr_t &context, runtime::GCGraph *root,
    std::span<const std::string> specs);

/// The names of the parameters of the entry graph that have no value, `with` ones first.
std::vector<std::string> unboundEntryParameters(const runtime::GCGraph *root);

/// The parameter name an object was bound for by bindEntryArguments (`w`, or `m.fc1.w` for a
/// leaf of a struct), or empty. For translation passes that name what they emit.
std::string entryArgumentNameOf(slot_t value);

/// Where diagnostics about the entry function are located: the definition of `main`.
uint64_t entryFunctionOrigin(const core::context::Context &context);

/// The key under which the Builder registers the origin of `main` in the debug map.
inline constexpr const char *kEntryFunctionOriginKey = "__entry__";

} // namespace camel::execute
