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
 * Author: Camel Contributors
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Frontend checks stop before module loading, execution roots and managed heaps are needed.
#include "check.h"
#include "camel/parse/parse.h"
#include "camel/utils/assert.h"
#include "camel/utils/str.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace camel::core::error;

int checkFrontend(
    std::istream &input, const diagnostics_ptr_t &diagnostics, bool lexicalOnly, bool json,
    std::ostream &output) {
    camel::parse::CamelParser parser(diagnostics);
    parser.setSourceContext(std::make_shared<camel::source::SourceContext>());
    auto reportErrors = [&](bool limited) {
        parser.dumpDiagnostics(output, json);
        if (!json && diagnosticFormat() == DiagFormat::Rich && diagnostics->hasErrors()) {
            const size_t errors   = diagnostics->count(Severity::Error);
            const size_t warnings = diagnostics->count(Severity::Warn);
            output << "\ncamel: " << errors << (errors == 1 ? " error" : " errors");
            if (warnings)
                output << ", " << warnings << (warnings == 1 ? " warning" : " warnings");
            if (limited)
                output << "; stopped at the limit of " << errors
                       << " errors (--max-errors <n> raises it, 0 removes it)";
            output << std::endl;
        }
        return 1;
    };
    try {
        // Keep AST construction for -s: its frontend diagnostics are part of this command.
        if (!(lexicalOnly ? parser.lex(input) : parser.parse(input)))
            return reportErrors(false);
        diagnostics->fetchAll(parser.tokenPositions());
        std::vector<std::string> warnings;
        for (const Diagnostic *warning : diagnostics->findBySeverity(Severity::Warn)) {
            if (admitShownWarning())
                warnings.push_back(json ? warning->toJson() : warning->toText(std::cerr));
        }
        if (!warnings.empty()) {
            if (json)
                std::cerr << "[\n" << strutil::join(warnings, ",\n") << "\n]\n";
            else {
                const bool rich = diagnosticFormat() == DiagFormat::Rich;
                std::cerr << strutil::join(warnings, rich ? "\n\n" : "\n")
                          << (rich ? "\n\n" : "\n");
            }
            std::cerr << std::flush;
        }
        return 0;
    } catch (const DiagnosticsLimitExceededBaseException &) {
        return reportErrors(true);
    } catch (Diagnostic &diagnostic) {
        RangeConverter converter(parser.tokenPositions());
        diagnostic.fetchRange(converter);
        output << "Uncaught diagnostic: "
               << (json ? diagnostic.toJson() : diagnostic.toText(output)) << std::endl;
    } catch (const std::exception &error) {
        if (json)
            output << "[\n"
                   << DiagnosticBuilder::of(InternalDiag::UnknownInternalError)
                          .commit(std::string(error.what()))
                          .toJson()
                   << "\n]" << std::endl;
        else
            output << "camel: internal error: " << error.what()
                   << "\nThis is a defect of the compiler, not an error of the program. Please "
                      "report it with the program that shows it."
                   << std::endl;
        ASSERT(false, error.what());
    } catch (...) {
        output << "Unknown error occurred." << std::endl;
        ASSERT(false, "Unknown error occurred.");
    }
    return 1;
}
