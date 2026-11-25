// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two test-only passes whose parameters are not a declaration.
 *
 * `test::params` carries its parameter list here, in C++, and a plan pins that list: it exists so
 * that a case can write every type of parameter in one call and read back what the pipeline made
 * of it. `test::declared` has no list of its own: its parameters are the declaration `test.cml`
 * writes, which is why a case runs it with `-I .` and its own copy of that file.
 *
 * Everything else about them is the rule for every pass (design principles 9.3): the name is the
 * path a pipeline writes, and the implementation is the function that answers it.
 *
 * They live in libcamel and not in `test.cmo` (where the other `test::` passes are), and the reason
 * is the pairing `test::declared` needs: a case puts its own `test.cml`
 * on the module search path (`-I .`) and that file declares the pass's parameters, so the
 * declaration and the implementation are found in two different places on purpose. A `.cml`
 * declaration is paired with an implementation by the module that carries both, and the case's
 * `test.cml` carries only the declaration: when the kernel sat in `test.cmo`, resolving the name
 * loaded the case's module instead, which declares the pass and exports no such function, and the
 * pipeline refused it (`PassNotExportedByModule`). Compiled into libcamel, the name resolves before
 * any module is imported, exactly as it did before the test channel became a module of its own.
 */

#include "camel/execute/pass/registration.h"

#include "src/stdlib/std/nvm/nodevm.h"

#include <format>
#include <memory>
#include <ostream>
#include <vector>

using camel::core::context::context_ptr_t;

namespace {

/// Runs the function a function parameter resolved to, with one `int`. A function takes no
/// constant, so the value a case can read back is what calling it gives, and a declaration a case
/// writes says `(n: int) => int` for that reason. `callFunction` opens the run the function's
/// program needs; the value is the PassArgs' own, rooted for as long as the arguments live.
int64_t callFunctionArgument(
    const context_ptr_t &ctx, std::shared_ptr<NodeVMSchedPass> &vm, const PassFunction &function,
    int64_t n) {
    if (vm == nullptr) {
        vm = std::make_shared<NodeVMSchedPass>(ctx);
    }
    const slot_t argument = camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(n);
    const slot_t answer   = vm->callFunction(
        camel::core::rtdata::fromSlot<::Function *>(function.value), std::span(&argument, 1));
    return camel::core::rtdata::fromSlot<camel::core::rtdata::Int64>(answer);
}

/// A test-only pass whose call can be read back: it prints every parameter the declaration names,
/// the value it was given, whether that value was written, and how many default expressions have
/// been evaluated in the process. A plan pins the declaration's count with `declared=`, writes each
/// name in one call, and compares the count of two calls that use the defaults (equal: a default is
/// evaluated at the declaration, not at each call).
class PassParamsTestPass final : public GraphIRPass {
  public:
    PassParamsTestPass(
        const context_ptr_t &ctx, const PassArgs &args, std::vector<PassParam> declared)
        : GraphIRPass(ctx), args_(args), declared_(std::move(declared)) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "test::params";
        for (const auto &p : declared_) {
            os << ' ' << p.name << '=';
            switch (p.type) {
            case PassParamType::Bool:
                os << (args_.getBool(p.name) ? "true" : "false");
                break;
            case PassParamType::Int:
                os << args_.getInt(p.name);
                break;
            case PassParamType::Float:
                os << std::format("{}", args_.getFloat(p.name));
                break;
            case PassParamType::String:
                os << args_.getString(p.name);
                break;
            case PassParamType::IntArray:
                // Not declared by this pass (its list is the registration's own); the case is here
                // so the switch covers every type a parameter may have.
                os << args_.text(p.name);
                break;
            case PassParamType::Function:
                // A function parameter is the function the call named, so the only honest thing to
                // print is what calling it gives: a case's declaration says `(n: int) => int`, and
                // the number printed is 5 through that function. A value that were still the text
                // would have nothing to call (camel/execute/pass/params.h).
                os << callFunctionArgument(context_, vm_, args_.getFunction(p.name), 5);
                break;
            }
            os << (args_.given(p.name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", declared_.size());
        return program;
    }

  private:
    PassArgs args_;
    std::vector<PassParam> declared_;
    std::shared_ptr<NodeVMSchedPass> vm_;
};

/// A test-only pass whose parameters are a declaration and not a list in C++: it has none of its
/// own, so the names, types and defaults the pipeline binds are exactly the ones a `.cml` on the
/// module search path writes for `test::declared` (`test.cml` in a case's own directory, `-I .`).
/// It prints every parameter the declaration named, the value the declared type read from the word,
/// and whether that value was written -- which is what a case that checks how a type is read
/// compares against. A function parameter has no value a word reads as text, so what it prints is
/// what calling the function gives (`callFunctionArgument`). A declaration the pipeline cannot read
/// (a type it has no value for, a default that does not fit it) is refused while the pass is
/// resolved, so nothing here ever asks for a parameter by a type the declaration did not write.
class DeclaredParamsTestPass final : public GraphIRPass {
  public:
    DeclaredParamsTestPass(const context_ptr_t &ctx, const PassArgs &args)
        : GraphIRPass(ctx), args_(args) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        const std::vector<std::string> names = args_.names();
        os << "test::declared";
        for (const auto &name : names) {
            os << ' ' << name << '=';
            // A function parameter is the function the call named, and what a case can read back
            // is what calling it gives (the declaration says `(n: int) => int`); every other
            // parameter prints the value its declared type read from the word.
            if (args_.isFunction(name)) {
                os << callFunctionArgument(context_, vm_, args_.getFunction(name), 5);
            } else {
                os << args_.text(name);
            }
            os << (args_.given(name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", names.size());
        return program;
    }

  private:
    PassArgs args_;
    std::shared_ptr<NodeVMSchedPass> vm_;
};

/// The parameters of `test::params`, one of every type the declaration can write. This is the one
/// list in C++ the pass-declaration guard allows, so it is pinned there by count.
std::vector<PassParam> paramsTestList() {
    return {
        {"flag", PassParamType::Bool, false, "a parameter that must be given", true},
        {"n", PassParamType::Int, int64_t{5}, "an int with a constant expression default", false},
        {"ratio", PassParamType::Float, 1.5, "a float", false},
        {"label", PassParamType::String, std::string("x"), "a string", false},
        {"count", PassParamType::Int, int64_t{7}, "another int", false},
    };
}

using camel::execute::PassKernel;

const PassKernel paramsTestKernel(
    "test::params",
    [](const context_ptr_t &ctx, const PassArgs &args) -> std::unique_ptr<GraphIRPass> {
        return std::make_unique<PassParamsTestPass>(ctx, args, paramsTestList());
    },
    paramsTestList());

/// `test::declared` is in no default pipeline, so naming it is the whole switch, and the
/// declaration it reads is the one its `.cml` writes (there is no parameter list here to fall back
/// to, which is the point).
const PassKernel declaredParamsTestKernel(
    "test::declared",
    [](const context_ptr_t &ctx, const PassArgs &args) -> std::unique_ptr<GraphIRPass> {
        return std::make_unique<DeclaredParamsTestPass>(ctx, args);
    });

} // namespace