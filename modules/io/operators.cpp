// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The terminal, which is what `io` is for until `Stream` exists: what it has
 * waiting to be read, the keys themselves, and raw mode. The standard streams and
 * `print`/`println`/`input`/`eprintln` are the prelude's operators, not this module's.
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/utf8.h"

#include <format>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
using namespace camel::core::type;

#ifdef _WIN32
namespace Terminal {
static DWORD originalMode  = 0;
static HANDLE hStdin       = GetStdHandle(STD_INPUT_HANDLE);
static bool rawModeEnabled = false;
bool setRawMode(bool enable) {
    if (enable && !rawModeEnabled) {
        if (!GetConsoleMode(hStdin, &originalMode))
            return false;
        DWORD rawMode = originalMode;
        rawMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        rawMode |= ENABLE_PROCESSED_INPUT;
        if (!SetConsoleMode(hStdin, rawMode))
            return false;
        rawModeEnabled = true;
    } else if (!enable && rawModeEnabled) {
        SetConsoleMode(hStdin, originalMode);
        rawModeEnabled = false;
    }
    return true;
}
bool isTerminal() {
    DWORD mode;
    return GetConsoleMode(hStdin, &mode) != 0;
}
bool hasInput() { return _kbhit() != 0; }
// `maxChars` counts characters (code points), and a character is read whole.
std::string readInput(int maxChars = -1) {
    std::string result;
    int chars = 0;
    while (hasInput() && (maxChars < 0 || chars < maxChars)) {
        char ch = _getch();
        result += ch;
        size_t more = camel::utf8::sequenceLength(static_cast<unsigned char>(ch)) - 1;
        while (more-- > 0 && hasInput())
            result += static_cast<char>(_getch());
        ++chars;
    }
    return result;
}
void clearInputBuffer() {
    while (_kbhit())
        _getch();
}
} // namespace Terminal
#else
namespace Terminal {
static struct termios originalTermios;
static bool rawModeEnabled = false;
bool setRawMode(bool enable) {
    if (enable && !rawModeEnabled) {
        if (!isatty(STDIN_FILENO))
            return false;
        if (tcgetattr(STDIN_FILENO, &originalTermios) == -1)
            return false;
        struct termios raw = originalTermios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
            return false;
        rawModeEnabled = true;
    } else if (!enable && rawModeEnabled) {
        tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
        rawModeEnabled = false;
    }
    return true;
}
bool isTerminal() { return isatty(STDIN_FILENO) != 0; }
bool hasInput() {
    fd_set fds;
    struct timeval tv = {0, 0};
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}
std::string readInput(int maxChars = -1) {
    std::string result;
    char ch;
    int chars = 0;
    while (hasInput() && (maxChars < 0 || chars < maxChars)) {
        if (read(STDIN_FILENO, &ch, 1) <= 0)
            break;
        result += ch;
        size_t more = camel::utf8::sequenceLength(static_cast<unsigned char>(ch)) - 1;
        while (more-- > 0 && read(STDIN_FILENO, &ch, 1) > 0)
            result += ch;
        ++chars;
    }
    return result;
}
void clearInputBuffer() {
    char ch;
    while (hasInput())
        read(STDIN_FILENO, &ch, 1);
}
} // namespace Terminal
#endif

std::unordered_map<std::string, operator_t> getIOOpsMap() {
    return {
        {"set_terminal_raw_mode", __io_set_terminal_raw_mode__},
        {"has_input", __io_has_input__},
        {"get_chars", __io_get_chars__},
        {"clear_input_buffer", __io_clear_input_buffer__},
    };
}

slot_t __io_set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, Context &ctx) {
    static const TupleType *resultType = TupleType::fallible(Type::Void());
    Bool enable = norm.get<Bool>(0);
    if (!Terminal::setRawMode(enable)) {
        // Only turning raw mode on can fail: off is always a success.
        return makeError(
            Terminal::isTerminal() ? "IOError" : "NotATerminal",
            Terminal::isTerminal() ? "the terminal refused raw mode"
                                   : "standard input is not a terminal",
            "<io::set_terminal_raw_mode>");
    }
    return makeOk(NullSlot, resultType);
}

slot_t __io_has_input__(ArgsView &with, ArgsView &norm, Context &) {
    return toSlot(Terminal::hasInput());
}

slot_t __io_get_chars__(ArgsView &with, ArgsView &norm, Context &) {
    Int64 maxChars = norm.get<Int64>(0);
    if (maxChars < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "io.get_chars",
            std::format("the number of characters is {}; it must not be negative", maxChars));
    }
    return toSlot(checkedString(
        Terminal::readInput(static_cast<int>(maxChars)),
        mm::autoSpace(),
        "io.get_chars"));
}

slot_t __io_clear_input_buffer__(ArgsView &with, ArgsView &norm, Context &) {
    Terminal::clearInputBuffer();
    return NullSlot;
}