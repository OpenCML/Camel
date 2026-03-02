/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * @file windows_parser_guard.h
 * @brief Windows 下在包含解析器相关头（state.h、compile.h、parse.h 等）之前必须包含本头，
 *       以消除 minwindef/wingdi 的 TRUE/FALSE/ERROR/CONST/IN 与 ANTLR 解析器符号的冲突。
 *
 * 用法：在任意会间接包含 OpenCMLParser/OpenCMLLexer 或 antlr4-runtime 的 .cpp 中，
 *       将本头作为该 TU 的第一个包含（或至少在任何可能拉入 Windows 的头之前）。
 *
 * 注意：若该 TU 后续会包含 httplib/winsock（需要 IN、CONST），则不要用本头，
 *       改为通过包含顺序保证 state.h 在拉入 Windows 的头之前被包含（见 server.cpp）。
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
