// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <exception>

namespace camel::core::error {

class BuildAbortException : public std::exception {
  public:
    BuildAbortException() {}
};

} // namespace camel::core::error
