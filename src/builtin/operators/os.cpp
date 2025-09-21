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
 * Updated: Sep. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "os.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include <memory>

#ifdef _WIN32
#include <Lmcons.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
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
