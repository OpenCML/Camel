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
