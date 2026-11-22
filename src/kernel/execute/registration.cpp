// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The registry's entry point and the readings of the two parameters every pass of one family
 * declares (camel/execute/pass/registration.h).
 */

#include "camel/execute/pass/registration.h"

namespace camel::execute {

void registerPassKernel(const std::string &path, PassMaker make, bool readsTags) {
    PassDescriptor descriptor;
    descriptor.make      = std::move(make);
    descriptor.readsTags = readsTags;
    registerModulePass(path, std::move(descriptor));
}

void registerPassKernel(const std::string &path, PassMaker make, std::vector<PassParam> params) {
    PassDescriptor descriptor;
    descriptor.make   = std::move(make);
    descriptor.params = std::move(params);
    registerModulePass(path, std::move(descriptor));
}

std::string pathArgument(const PassArgs &args, const std::string &name) {
    return args.isNull(name) ? std::string() : args.getString(name);
}

size_t constantBudgetOf(const PassArgs &args) {
    const int64_t bytes = args.getInt("constant_budget");
    return bytes < 0 ? 0 : static_cast<size_t>(bytes);
}

size_t settleRunsOf(const PassArgs &args) {
    return static_cast<size_t>(args.getInt("settle_runs"));
}

} // namespace camel::execute
