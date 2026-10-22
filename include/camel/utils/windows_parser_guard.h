// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file windows_parser_guard.h
 * @brief Establish the Windows header options used by parser hosts.
 *
 * The handwritten parser's token names no longer collide with Windows macros.
 * Preserve those macros: later Winsock and HTTP headers need IN, CONST, TRUE and FALSE.
 */

#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
