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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "userdef.h"

#include "antlr4-runtime/antlr4-runtime.h"

#include "config.h"

#include "compile/gct.h"
#include "compile/gir.h"
#include "parse/ast.h"
#include "parse/cst-dump.h"

#include "common/error/base.h"
#include "common/error/diagnostic.h"
#include "common/error/listener.h"

#include "utils/log.h"

#define DEBUG_LEVEL -1

using namespace std;
using namespace antlr4;

void dumpTokens(CommonTokenStream &tokens) {
    while (true) {
        Token *token = tokens.LT(1);
        if (token->getType() == Token::EOF) {
            break;
        }
        cout << setw(4) << right << token->getTokenIndex() << " [" << setw(3) << right << token->getLine() << ":"
             << setw(3) << left << token->getCharPositionInLine() << "] (" << token->getChannel()
             << ") : " << token->getText() << endl;
        tokens.consume();
    }
    tokens.reset();
}

bool buildCST(tree::ParseTree *&cst, OpenCMLParser &parser, ostream &os, string errorFormat) {
    auto interpreter = parser.getInterpreter<atn::ParserATNSimulator>();
    parser.removeErrorListeners();

    try {
        interpreter->setPredictionMode(atn::PredictionMode::SLL);
        parser.setErrorHandler(make_shared<BailErrorStrategy>());
        cst = parser.program();
    } catch (ParseCancellationException &e) {
        debug(1) << "Parse failed, retrying with LL mode." << endl;

        CamelErrorListener *listener = nullptr;

        if (errorFormat == "text") {
            listener = new CamelErrorListener("stdin", os);
        } else if (errorFormat == "json") {
            listener = new JSONErrorListener("stdin", os);
        } else {
            error << "Unknown error format: " << errorFormat << endl;
            return false;
        }

        parser.addErrorListener(listener);

        parser.reset();
        interpreter->setPredictionMode(atn::PredictionMode::LL);
        parser.setErrorHandler(make_shared<DefaultErrorStrategy>());

        try {
            cst = parser.program();
        } catch (exception &e) {
            debug(1) << "Parse failed. " << e.what() << endl;
            return false;
        }

        if (listener->hasErrors()) {
            return false;
        }
    } catch (exception &e) {
        debug(1) << "Parse failed. " << e.what() << endl;
        return false;
    }
    return true;
}

bool buildAST(AST::node_ptr_t &ast, tree::ParseTree *cst, diagnostics_ptr_t diagnostics) {
    auto constructor = AST::Constructor();
    ast = constructor.construct(cst, diagnostics);
    return ast != nullptr && !diagnostics->hasErrors();
}

bool buildGCT(GCT::node_ptr_t &gct, AST::node_ptr_t &ast, diagnostics_ptr_t diagnostics) {
    initTypes();
    auto constructor = GCT::Constructor();
    gct = constructor.construct(ast, diagnostics);
    return gct != nullptr && !diagnostics->hasErrors();
}

bool buildGIR(GIR::graph_ptr_t &gir, GCT::node_ptr_t &gct, context_ptr_t &ctx, diagnostics_ptr_t diagnostics) {
    auto constructor = GIR::Constructor(ctx);
    gir = constructor.construct(gct, diagnostics);
    return gir != nullptr && !diagnostics->hasErrors();
}

void UserDefinedModule::build() {
    if (built_) {
        return;
    }
}