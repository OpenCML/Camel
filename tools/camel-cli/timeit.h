// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <ostream>

#ifndef CAMEL_CLI_TIMEIT_H
#define CAMEL_CLI_TIMEIT_H

#include <iosfwd>

/**
 * Timeit mode runs a single named pass repeatedly from a freshly rebuilt graph.
 */
int runTimeitMode(std::ostream &os, bool useJsonFormat);

#endif
