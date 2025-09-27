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
 * Updated: Sep. 27, 2025
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

// Terminal control for raw mode and non-blocking input
#ifdef _WIN32
// ========================
// Windows 实现
// ========================
#include <conio.h>
#include <windows.h>

namespace Terminal {

static DWORD originalMode = 0;
static HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
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

std::string readInput() {
    char ch = _getch();
    return std::string(1, ch);
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

bool setRawMode(bool enable) {
    if (enable && !rawModeEnabled) {
        if (!isatty(STDIN_FILENO))
            return false;
        if (tcgetattr(STDIN_FILENO, &originalTermios) == -1)
            return false;

        struct termios raw = originalTermios;
        raw.c_lflag &= ~(ECHO | ICANON); // 关闭回显和标准输入缓冲
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

bool hasInput() {
    fd_set fds;
    struct timeval tv = {0, 0};

    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

std::string readInput() {
    char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0)
        return "";
    return std::string(1, ch);
}

} // namespace Terminal

#endif

namespace GIR = GraphIR;

OperatorReturnCode __sleep__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (ins.size() != 1) {
        ctx.rtmDiags()->of(RuntimeDiag::IncorrectArgsCount).commit("<sleep>", 1, ins.size());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    const data_ptr_t &arg = frame.get(ins[0]);
    if (!Type::castSafetyCheck(arg->type(), Type::Int64())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<sleep>", "int64", arg->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    auto pd = arg->as<Int64Data>(Type::Int64());
    if (pd->data() < 0) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sleep> requires a non-negative integer");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(pd->data()));
    frame.set(self, Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __whoami__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (!ins.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::IncorrectArgsCount).commit("<whoami>", 0, ins.size());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    std::string username;

#ifdef _WIN32
    char buffer[UNLEN + 1];
    DWORD len = UNLEN + 1;
    if (GetUserNameA(buffer, &len)) {
        username = buffer;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
#else
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        username = pw->pw_name;
    } else {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<whoami> failed to get username");
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }
#endif

    data_ptr_t result = std::make_shared<StringData>(username);
    frame.set(self, result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __exit__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    throw CamelRuntimeException(RuntimeExceptionCode::ForceExit, "<exit> operator invoked");
}

OperatorReturnCode __set_terminal_raw_mode__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (ins.size() != 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncorrectArgsCount)
            .commit("<set_terminal_raw_mode>", 1, ins.size());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    const data_ptr_t &arg = frame.get(ins[0]);
    if (!Type::castSafetyCheck(arg->type(), Type::Bool())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<set_terminal_raw_mode>", "bool", arg->type()->toString());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    bool enable = arg->as<BoolData>(Type::Bool())->data();
    bool success = Terminal::setRawMode(enable);

    if (!success) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<set_terminal_raw_mode> failed to modify terminal mode");
    }

    frame.set(self, Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __has_input__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (!ins.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::IncorrectArgsCount).commit("<has_input>", 0, ins.size());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    bool available = Terminal::hasInput();

    frame.set(self, std::make_shared<BoolData>(available));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __get_char__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    if (!ins.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::IncorrectArgsCount).commit("<get_char>", 0, ins.size());
        frame.set(self, Data::null());
        return OperatorReturnCode::OK;
    }

    std::string input = Terminal::readInput();

    frame.set(self, std::make_shared<StringData>(input));
    return OperatorReturnCode::OK;
}
