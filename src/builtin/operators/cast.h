// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"

slot_t __itoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __atos__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ctoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ztoc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ctoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ztoz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

// bf16 and f16 are floating types of the language with a written conversion of their own (design
// principles 7.3): `bf16(x)`, `f16(x)` and `x as bf16` all come here, round to nearest with ties
// to even once, and need no numeric permission. `h` in a name is bf16 and `f6` is f16.
slot_t __itobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __bftobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stobf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __itof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ltof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __ftof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __dtof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __f6tof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __stof6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __bftoi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __bftol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __bftof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __bftod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __f6toi__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __f6tol__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __f6tof__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __f6tod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
