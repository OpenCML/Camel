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
 */

#include "camel/execute/pass/registration.h"

#include <format>
#include <memory>
#include <ostream>
#include <vector>

using camel::core::context::context_ptr_t;

namespace {

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
            }
            os << (args_.given(p.name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", declared_.size());
        return program;
    }

  private:
    PassArgs args_;
    std::vector<PassParam> declared_;
};

/// A test-only pass whose parameters are a declaration and not a list in C++: it has none of its
/// own, so the names, types and defaults the pipeline binds are exactly the ones a `.cml` on the
/// module search path writes for `test::declared` (`test.cml` in a case's own directory, `-I .`).
/// It prints every parameter the declaration named, the value the declared type read from the word,
/// and whether that value was written -- which is what a case that checks how a type is read
/// compares against. A declaration the pipeline cannot read (a type it has no value for, a default
/// that does not fit it) is refused while the pass is resolved, so nothing here ever asks for a
/// parameter by a type the declaration did not write.
class DeclaredParamsTestPass final : public GraphIRPass {
  public:
    DeclaredParamsTestPass(const context_ptr_t &ctx, const PassArgs &args)
        : GraphIRPass(ctx), args_(args) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        const std::vector<std::string> names = args_.names();
        os << "test::declared";
        for (const auto &name : names) {
            os << ' ' << name << '=' << args_.text(name)
               << (args_.given(name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", names.size());
        return program;
    }

  private:
    PassArgs args_;
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