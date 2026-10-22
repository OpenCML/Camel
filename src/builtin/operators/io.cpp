/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "io.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/complex.h"

#include "complex_format.h"
#include "float_format.h"
#include "fmt/args.h"
#include "text_format.h"

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
#include "fmt/core.h"

#include <iostream>
#include <mutex>
#include <sstream>

#include "complex_format.h"
#include "float_format.h"

namespace {

// What one print or println produces reaches standard output in one piece: under a parallel
// scheduler calls with no order between them print at the same time, and a line written in
// several steps came out with another call's text (or only its line end) in the middle.
void writeToStdout(const std::string &text, bool flush) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    std::cout << text;
    if (flush) {
        std::cout.flush();
    }
}

// `eprintln` writes the standard error stream, which is its own resource (`stderr`), so it has
// its own lock: a line still reaches the stream in one piece under a parallel scheduler.
void writeToStderr(const std::string &text) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    std::cerr << text;
    std::cerr.flush();
}

/// What `print` and `println` write for their arguments, without a line end: the arguments
/// separated by one space, or, with a format (`print<fmt>(...)`), the format with the
/// arguments put in. Returns the first argument (an effect function returns a value, design
/// principles 3.1), null when there is none. `formatted` says whether a format was given.
slot_t writeArguments(ArgsView &with, ArgsView &norm, std::ostringstream &out, bool &formatted) {
    slot_t result = norm.size() > 0 ? norm.slot(0) : NullSlot;
    formatted     = with.size() > 0;
    if (!formatted) {
        for (size_t i = 0; i < norm.size(); i++) {
            if (i > 0) {
                out << " ";
            }
            printSlot(out, norm.slot(i), norm.type(i));
        }
        return result;
    }
    String *fmtStrObj  = with.get<String *>(0);
    std::string fmtStr = fmtStrObj->toString();

    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (size_t i = 0; i < norm.size(); ++i) {
        TypeCode code = norm.code(i);
        switch (code) {
        case TypeCode::Int32:
            store.push_back(norm.get<Int32>(i));
            break;
        case TypeCode::Int64:
            store.push_back(norm.get<Int64>(i));
            break;
        case TypeCode::Float32:
            store.push_back(camel::builtin::LanguageFloat<Float32>{norm.get<Float32>(i)});
            break;
        case TypeCode::Float64:
            store.push_back(camel::builtin::LanguageFloat<Float64>{norm.get<Float64>(i)});
            break;
        case TypeCode::Complex64: {
            const Complex *value = norm.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float32>{
                value != nullptr ? value->realF() : Float32{0},
                value != nullptr ? value->imaginaryF() : Float32{0}});
            break;
        }
        case TypeCode::Complex128: {
            const Complex *value = norm.get<Complex *>(i);
            store.push_back(camel::builtin::LanguageComplex<Float64>{
                value != nullptr ? value->real() : Float64{0},
                value != nullptr ? value->imaginary() : Float64{0}});
            break;
        }
        default:
            std::ostringstream oss;
            printSlot(oss, norm.slot(i), norm.type(i));
            store.push_back(camel::builtin::CodePointText{oss.str()});
            break;
        }
    }
    try {
        out << fmt::vformat(fmtStr, store);
    } catch (const fmt::format_error &e) {
        throwRuntimeFault(RuntimeDiag::FormatFailed, std::string(fmtStr), std::string(e.what()));
    }
    return result;
}

} // namespace

// `print` ends nothing with a line end, with a format or without; `println` ends what it
// writes with exactly one. (The formatted forms used to add one more: `print<fmt>` a line end
// and `println<fmt>` an empty line. Design principles 10.6 calls that a defect of the
// interpreter; the generated C++ programs copied it and were fixed with it.)
slot_t __op_print__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::ostringstream out;
    bool formatted      = false;
    const slot_t result = writeArguments(with, norm, out, formatted);
    // A formatted print is flushed as it was when it ended a line.
    writeToStdout(out.str(), formatted);
    return result;
}

slot_t __op_println__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::ostringstream out;
    bool formatted      = false;
    const slot_t result = writeArguments(with, norm, out, formatted);
    out << '\n';
    writeToStdout(out.str(), true);
    return result;
}

// `eprintln` is `println` on the standard error stream: same text, one line end, and it gives
// back its first argument (design principles 3.1). It is in the prelude (the author's ruling on
// H134 question 2), so a program writes it without an import.
slot_t __op_eprintln__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::ostringstream out;
    bool formatted      = false;
    const slot_t result = writeArguments(with, norm, out, formatted);
    out << '\n';
    writeToStderr(out.str());
    return result;
}

slot_t __op_input__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::stringstream oss;

    for (size_t i = 0; i < norm.size(); i++) {
        if (i > 0) {
            oss << " ";
        }
        slot_t data = norm.slot(i);
        printSlot(oss, data, norm.type(i));
    }

    std::cout << oss.str();

    std::string input;
    std::getline(std::cin, input);

    String *str = camel::core::rtdata::checkedString(input, mm::autoSpace(), "input");
    return toSlot(str);
}
