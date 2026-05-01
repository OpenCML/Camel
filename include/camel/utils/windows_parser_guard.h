/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * @file windows_parser_guard.h
 * @brief On Windows, include this header before parser-related headers
 *        (state.h,
 * compile.h, parse.h, etc.) to eliminate symbol conflicts
 *        between minwindef/wingdi
 * TRUE/FALSE/ERROR/CONST/IN and the ANTLR parser.
 *
 * Usage: in any .cpp that may indirectly
 * include OpenCMLParser/OpenCMLLexer or
 *        antlr4-runtime, make this header the first
 * include in the TU (or at
 *        least include it before any header that may pull in Windows
 * headers).
 *
 * Note: if the TU later includes httplib/winsock (which needs IN and CONST),
 * do
 * not use this header; instead, rely on include order so state.h is
 *       included before
 * Windows headers are pulled in (see server.cpp).
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
#undef TRUE
#undef FALSE
#undef ERROR
#undef CONST
#undef IN
#endif
