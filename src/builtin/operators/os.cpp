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
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "os.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

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

static DWORD originalMode = 0;
static HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
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
        raw.c_cc[VMIN] = 1;
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

void __sleep__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arg = frame.get(nargs[0]);

    auto pd = arg->as<Int32Data>(Type::Int32());

    if (pd->data() < 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sleep> requires a non-negative integer");
        frame.set(self, Data::null());
        return;
    }

    try {
        std::this_thread::sleep_for(std::chrono::milliseconds(pd->data()));
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("<sleep> encountered an error: ") + e.what());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, Data::null());
}

void __whoami__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::string username;

#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(buffer, &len)) {
        username = buffer;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        frame.set(self, Data::null());
        return;
    }
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        username = pw->pw_name;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        frame.set(self, Data::null());
        return;
    }
#endif

    data_ptr_t result = std::make_shared<StringData>(username);
    frame.set(self, result);
    return;
}

void __exit__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    throw CamelRuntimeException(RuntimeExceptionCode::ForceExit, "<exit> operator invoked");
}

void __set_terminal_raw_mode__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arg = frame.get(nargs[0]);

    bool enable = arg->as<BoolData>(Type::Bool())->data();
    bool success = Terminal::setRawMode(enable);

    if (!success) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<set_terminal_raw_mode> failed to modify terminal mode");
    }

    frame.set(self, Data::null());
    return;
}

void __has_input__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    bool available = Terminal::hasInput();

    frame.set(self, std::make_shared<BoolData>(available));
    return;
}

void __get_char__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::string input = Terminal::readInput(1);

    frame.set(self, std::make_shared<StringData>(input));
    return;
}

void __get_chars__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    int maxChars = -1; // Default to read all available characters
    maxChars = frame.get(nargs[0])->as<Int32Data>(Type::Int32())->data();

    std::string input = Terminal::readInput(maxChars);

    frame.set(self, std::make_shared<StringData>(input));
    return;
}

void __clear_input_buffer__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    Terminal::clearInputBuffer();

    frame.set(self, Data::null());
    return;
}
