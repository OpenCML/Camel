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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

namespace camel::core {

/// Thrown by the `exit` operator: the program asks to end with `code`. It is neither an error
/// nor a fault, so there is no diagnostic. It unwinds the scheduler the way a fault does (the
/// tasks in flight finish or are skipped, nothing after the call runs) and the pass driver ends
/// the pipeline as a completed run whose exit code is `code`: the process then leaves through
/// the same path as a program whose `main` returned, with the same flushing and clean-up.
///
/// Deliberately not a std::exception: handlers that turn exceptions into errors do not see it.
struct ProcessExit {
    int code = 0;
};

} // namespace camel::core
