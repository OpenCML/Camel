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
 *
 * The value `main` returns goes out the same way it would come in (writeEntryResultTo; the
 * output of the command line, `-o` / `--output <file>`, standard output by default). Written to
 * a file: a scalar as a text file holding the literal, a struct
 * or tuple as a directory with one file per leaf, a module-defined type through the saver the
 * module registered (a Tensor as a `.npy` file).
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

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
 * Binds `arguments` to the parameters of the entry graph of `program` and returns the program
 * that results (`program` itself when there is nothing to bind). A name that is not a parameter,
 * a name given twice or a value that cannot be read as the parameter's type is reported as
 * RuntimeDiag::EntryArgumentInvalid in the runtime diagnostics, located at `main`; nullptr is
 * returned then.
 */
const runtime::Program *bindEntryArguments(
    const core::context::context_ptr_t &context, const runtime::Program *program,
    std::span<const EntryArgument> arguments);

/// The same for arguments as written on a command line (`name=value` each). A text without
/// `=` is reported like any other invalid argument.
const runtime::Program *bindEntryArgumentSpecs(
    const core::context::context_ptr_t &context, const runtime::Program *program,
    std::span<const std::string> specs);

/**
 * Writes a value of a module-defined type to `path` (the counterpart of EntryArgumentLoader).
 * Returns the path it wrote, which may be `path` with the format's extension added. Failure is
 * an exception whose message says why.
 */
using EntryResultSaver =
    std::function<std::string(slot_t value, core::type::Type *type, const std::string &path)>;

/// Registers the saver for the values of the type with code `code`.
void registerEntryResultSaver(core::type::TypeCode code, EntryResultSaver saver);

/**
 * Has the value the entry graph returns written to the output when a scheduling pass has run
 * the program. `path` is the output file, or empty for standard output.
 *
 *   - `main` returns nothing: nothing is written; with a file, that is
 *     RuntimeDiag::EntryResultNotWritten.
 *   - `main` returns an exit code (int, bool, byte: the types the process exit code is derived
 *     from): the value stays the exit code. It is written only to a file that was asked for,
 *     never to standard output, so a program whose `main` returns its status prints what it
 *     printed before.
 *   - any other value is the result of the program. On standard output it is printed as
 *     `println` prints it, on a line of its own. To a file it is written in the layout the
 *     argument loaders read (see the head of this file).
 *
 * A value that cannot be written to the file is RuntimeDiag::EntryResultNotWritten, located at
 * `main`.
 *
 * All of this holds for a `main` that declares its return type (Context::entryResultDeclared;
 * design principles 9.8). The value of a `main` that declares none is not a result: nothing
 * is printed, and with a file that is EntryResultNotWritten.
 *
 * Under a declared `T!`: a value that has not failed is treated as its `T`. A failed one is an
 * error nobody handled: RuntimeDiag::UnhandledError on standard error (as JSON when
 * `jsonDiagnostics`), located where the error value was made, and a non-zero exit code.
 */
void writeEntryResultTo(
    const core::context::context_ptr_t &context, std::string path, bool jsonDiagnostics = false);

/// The names of the parameters of the entry graph that have no value, `with` ones first.
std::vector<std::string> unboundEntryParameters(const runtime::GCGraph *root);

/// A value bindEntryArguments made a constant of the entry graph.
struct BoundEntryArgument {
    std::string name;       // the parameter
    slot_t value;           // the object as it is now (the collector may have moved it)
    core::type::Type *type; // its exact type
};

/// The objects (tensors, structs, tuples, strings) bound so far, in binding order. For
/// translation passes that name what they emit after the parameter it came from. Scalars are
/// not listed: a number has no identity to find again in a graph.
std::vector<BoundEntryArgument> boundEntryArguments();

/// Where diagnostics about the entry function are located: the definition of `main`.
uint64_t entryFunctionOrigin(const core::context::Context &context);

/// The key under which the Builder registers the origin of `main` in the debug map.
inline constexpr const char *kEntryFunctionOriginKey = "__entry__";

} // namespace camel::execute
