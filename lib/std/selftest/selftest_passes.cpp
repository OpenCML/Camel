// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two self-testing passes of the core's type layer, `std::arraytype` and `std::identitytype`:
 * each runs the check the type layer carries for the tests and prints what it found. They exist so
 * that a plan can ask a built compiler to check itself; their declarations are in stdlib/std.cml.
 */

#include "camel/execute/pass/opt.h"
#include "camel/execute/pass/registration.h"

#include "camel/core/type/array_type_selftest.h"
#include "camel/core/type/type_identity_selftest.h"

#include <ostream>

using namespace camel::core::context;

namespace {

class ArrayTypeSelfTestPass final : public GraphIRPass {
  public:
    explicit ArrayTypeSelfTestPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "ARRAY_TYPE_SELFTEST ok " << camel::core::type::debugRunArrayTypeSelfTest() << "\n";
        return program;
    }
};

class TypeIdentitySelfTestPass final : public GraphIRPass {
  public:
    explicit TypeIdentitySelfTestPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "TYPE_IDENTITY_SELFTEST ok " << camel::core::type::debugRunTypeIdentitySelfTest()
           << "\n";
        return program;
    }
};

using camel::execute::makePass;
using camel::execute::PassKernel;

const PassKernel arrayTypeKernel("std::arraytype", makePass<ArrayTypeSelfTestPass>());
const PassKernel typeIdentityKernel("std::identitytype", makePass<TypeIdentitySelfTestPass>());

} // namespace
