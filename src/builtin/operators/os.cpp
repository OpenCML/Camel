/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "os.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"

#include <memory>
#include <thread>

#ifdef _WIN32
#include <Lmcons.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef _WIN32
// ========================
// Windows 实现
// ========================
#include <conio.h>
#include <windows.h>

// Terminal control namespace
namespace Terminal {

static DWORD originalMode  = 0;
static HANDLE hStdin       = GetStdHandle(STD_INPUT_HANDLE);
static bool rawModeEnabled = false;

// 设置或取消原始输入模式
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

// 检查是否有输入
bool hasInput() { return _kbhit() != 0; }

// 读取输入，最多读取 maxChars 个字符，-1 代表全部读取
std::string readInput(int maxChars = -1) {
    std::string result;
    while (hasInput() && (maxChars < 0 || static_cast<int>(result.size()) < maxChars)) {
        char ch = _getch();
        result += ch;
    }
    return result;
}

// 清空输入缓冲区
void clearInputBuffer() {
    while (_kbhit()) {
        _getch(); // 丢弃字符
    }
}

} // namespace Terminal

#else
// ========================
// Unix/Linux/macOS 实现
// ========================
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace Terminal {

static struct termios originalTermios;
static bool rawModeEnabled = false;

// 设置或取消原始输入模式
bool setRawMode(bool enable) {
    if (enable && !rawModeEnabled) {
        if (!isatty(STDIN_FILENO))
            return false;
        if (tcgetattr(STDIN_FILENO, &originalTermios) == -1)
            return false;

        struct termios raw = originalTermios;
        raw.c_lflag &= ~(ECHO | ICANON); // 关闭回显和标准缓冲
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

// 检查是否有输入
bool hasInput() {
    fd_set fds;
    struct timeval tv = {0, 0};

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

// 读取输入，最多读取 maxChars 个字符，-1 代表全部读取
std::string readInput(int maxChars = -1) {
    std::string result;
    char ch;
    while (hasInput() && (maxChars < 0 || static_cast<int>(result.size()) < maxChars)) {
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n <= 0)
            break;
        result += ch;
    }
    return result;
}

// 清空输入缓冲区
void clearInputBuffer() {
    char ch;
    while (hasInput()) {
        if (read(STDIN_FILENO, &ch, 1) <= 0)
            break;
    }
}

} // namespace Terminal
#endif

namespace GIR = GraphIR;

slot_t __sleep__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int ms = norm.get<Int>(0);

    if (ms < 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sleep> requires a non-negative integer");
        return NullSlot;
    }

    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("<sleep> encountered an error: ") + e.what());
        return NullSlot;
    }

    return NullSlot;
}

slot_t __whoami__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::string username;

#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(buffer, &len)) {
        username = buffer;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        return NullSlot;
    }
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        username = pw->pw_name;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        return NullSlot;
    }
#endif

    String *str = String::from(username, mm::autoSpace());
    return toSlot(str);
}

slot_t __exit__(ArgsView &with, ArgsView &norm, Context &ctx) {
    throw CamelRuntimeException(RuntimeExceptionCode::ForceExit, "<exit> operator invoked");
}

slot_t __set_terminal_raw_mode__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool enable  = norm.get<Bool>(0);
    bool success = Terminal::setRawMode(enable);

    if (!success) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<set_terminal_raw_mode> failed to modify terminal mode");
    }

    return NullSlot;
}

slot_t __has_input__(ArgsView &with, ArgsView &norm, Context &) {
    Bool available = Terminal::hasInput();
    return toSlot(available);
}

slot_t __get_char__(ArgsView &with, ArgsView &norm, Context &) {
    std::string input = Terminal::readInput(1);
    String *str       = String::from(input, mm::autoSpace());
    return toSlot(str);
}

slot_t __get_chars__(ArgsView &with, ArgsView &norm, Context &) {
    Int maxChars      = norm.get<Int>(0);
    std::string input = Terminal::readInput(maxChars);
    String *str       = String::from(input, mm::autoSpace());
    return toSlot(str);
}

slot_t __clear_input_buffer__(ArgsView &with, ArgsView &norm, Context &) {
    Terminal::clearInputBuffer();
    return NullSlot;
}
