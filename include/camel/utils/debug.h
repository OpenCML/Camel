// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#ifndef NDEBUG
#define EXEC_WHEN_DEBUG(code)                                                                      \
    do {                                                                                           \
        code;                                                                                      \
    } while (0)
#else
#define EXEC_WHEN_DEBUG(code)
#endif
