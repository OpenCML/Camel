/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Mar. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"

#include <memory>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
#include <thread>

#ifdef _WIN32
#include <Lmcons.h>
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <passwd.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

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
bool hasInput() { return _kbhit() != 0; }
std::string readInput(int maxChars = -1) {
    std::string result;
    while (hasInput() && (maxChars < 0 || static_cast<int>(result.size()) < maxChars)) {
        char ch = _getch();
        result += ch;
    }
    return result;
}
void clearInputBuffer() {
    while (_kbhit())
        _getch();
}
} // namespace Terminal
#else
#include <termios.h>
#include <unistd.h>
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
    while (hasInput() && (maxChars < 0 || static_cast<int>(result.size()) < maxChars)) {
        if (read(STDIN_FILENO, &ch, 1) <= 0)
            break;
        result += ch;
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

std::unordered_map<std::string, operator_t> getOSOpsMap() {
    return {
        {"exit", __os_exit__},
        {"sleep", __os_sleep__},
        {"whoami", __os_whoami__},
        {"cpu_count", __os_cpu_count__},
        {"set_terminal_raw_mode", __os_set_terminal_raw_mode__},
        {"has_input", __os_has_input__},
        {"get_char", __os_get_char__},
        {"get_chars", __os_get_chars__},
        {"clear_input_buffer", __os_clear_input_buffer__},
    };
}

slot_t __os_sleep__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 ms = norm.get<Int64>(0);
    if (ms < 0) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<sleep> requires a non-negative integer");
    }
    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("<sleep> ") + e.what());
    }
    return NullSlot;
}

slot_t __os_whoami__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string username;
#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(buffer, &len)) {
        username = buffer;
    } else {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<whoami> failed");
    }
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        username = pw->pw_name;
    } else {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<whoami> failed");
    }
#endif
    return toSlot(String::from(username, mm::autoSpace()));
}

slot_t __os_cpu_count__(ArgsView &with, ArgsView &norm, Context &ctx) {
    unsigned int count = std::thread::hardware_concurrency();
    if (count == 0) {
        count = 1;
    }
    return toSlot(static_cast<Int64>(count));
}

slot_t __os_exit__(ArgsView &with, ArgsView &norm, Context &ctx) {
    throwRuntimeFault(RuntimeDiag::RuntimeError, "<exit> operator invoked");
    exit(1);
    return NullSlot;
}

slot_t __os_set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool enable = norm.get<Bool>(0);
    if (!Terminal::setRawMode(enable)) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "<set_terminal_raw_mode> failed");
    }
    return NullSlot;
}

slot_t __os_has_input__(ArgsView &with, ArgsView &norm, Context &) {
    return toSlot(Terminal::hasInput());
}

slot_t __os_get_char__(ArgsView &with, ArgsView &norm, Context &) {
    return toSlot(String::from(Terminal::readInput(1), mm::autoSpace()));
}

slot_t __os_get_chars__(ArgsView &with, ArgsView &norm, Context &) {
    Int64 maxChars = norm.get<Int64>(0);
    return toSlot(String::from(Terminal::readInput(static_cast<int>(maxChars)), mm::autoSpace()));
}

slot_t __os_clear_input_buffer__(ArgsView &with, ArgsView &norm, Context &) {
    Terminal::clearInputBuffer();
    return NullSlot;
}
