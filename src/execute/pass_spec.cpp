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
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "pass_spec.h"

#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/parse.h"
#include "camel/utils/escape.h"
#include "camel/utils/token.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ast = camel::parse::ast;
using namespace camel::execute::detail;

// -----------------------------------------------------------------------------
// PassArgs
// -----------------------------------------------------------------------------

const char *passParamTypeName(PassParamType type) {
    switch (type) {
    case PassParamType::Bool:
        return "bool";
    case PassParamType::Int:
        return "int";
    case PassParamType::Float:
        return "float";
    case PassParamType::String:
        return "string";
    }
    return "?";
}

void PassArgs::set(const std::string &name, PassParamValue value, bool written) {
    values_[name]  = std::move(value);
    written_[name] = written;
}

const PassParamValue &PassArgs::at(const std::string &name) const {
    auto it = values_.find(name);
    if (it == values_.end()) {
        throw std::logic_error("pass parameter '" + name + "' is not declared by the pass");
    }
    return it->second;
}

template <typename T> static const T &getAs(const PassParamValue &value, const std::string &name) {
    if (const T *p = std::get_if<T>(&value)) {
        return *p;
    }
    throw std::logic_error("pass parameter '" + name + "' is asked for with the wrong type");
}

bool PassArgs::getBool(const std::string &name) const { return getAs<bool>(at(name), name); }
int64_t PassArgs::getInt(const std::string &name) const { return getAs<int64_t>(at(name), name); }
double PassArgs::getFloat(const std::string &name) const { return getAs<double>(at(name), name); }
const std::string &PassArgs::getString(const std::string &name) const {
    return getAs<std::string>(at(name), name);
}
bool PassArgs::given(const std::string &name) const {
    auto it = written_.find(name);
    return it != written_.end() && it->second;
}

PassDescriptor passWithoutParameters(PassFactory factory) {
    PassDescriptor d;
    d.make = [factory = std::move(factory)](
                 const camel::core::context::context_ptr_t &ctx,
                 const PassArgs &) { return factory(ctx); };
    return d;
}

// -----------------------------------------------------------------------------
// splitPassList
// -----------------------------------------------------------------------------

std::vector<std::string> splitPassList(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    int depth      = 0;
    char quote     = 0;
    const auto add = [&] {
        size_t first = current.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            size_t last = current.find_last_not_of(" \t\r\n");
            out.push_back(current.substr(first, last - first + 1));
        }
        current.clear();
    };
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote != 0) {
            current += c;
            if (c == '\\' && i + 1 < text.size()) {
                current += text[++i];
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            depth = std::max(0, depth - 1);
        } else if (c == ',' && depth == 0) {
            add();
            continue;
        }
        current += c;
    }
    add();
    return out;
}

// -----------------------------------------------------------------------------
// Constant expressions
// -----------------------------------------------------------------------------

namespace {

struct EvalError {
    std::string message;
};

bool isNumber(const PassParamValue &v) {
    return std::holds_alternative<int64_t>(v) || std::holds_alternative<double>(v);
}
double asDouble(const PassParamValue &v) {
    return std::holds_alternative<int64_t>(v) ? static_cast<double>(std::get<int64_t>(v))
                                              : std::get<double>(v);
}
const char *kindOf(const PassParamValue &v) {
    switch (v.index()) {
    case 0:
        return "bool";
    case 1:
        return "int";
    case 2:
        return "float";
    default:
        return "string";
    }
}

PassParamValue evalLiteral(const Literal &lit) {
    const std::string &text = lit.data();
    switch (lit.type()) {
    case LiteralType::Boolean:
        return text == "true";
    case LiteralType::String:
        return decodeEscapes(text);
    case LiteralType::Integer: {
        const IntegerLiteral parsed = parseIntegerLiteral(text);
        if (parsed.status != IntegerLiteralStatus::Ok) {
            throw EvalError{std::format("the integer '{}' is out of range or malformed", text)};
        }
        if (parsed.inexact) {
            throw EvalError{std::format("'{}' is not an integer; write it as a float", text)};
        }
        return parsed.value;
    }
    case LiteralType::Real:
        try {
            return parseNumber<double>(text);
        } catch (const std::exception &) {
            throw EvalError{std::format("the number '{}' is malformed or out of range", text)};
        }
    case LiteralType::FString:
        throw EvalError{"formatted strings are not constants"};
    case LiteralType::Imaginary:
        throw EvalError{"a complex number is not a parameter value"};
    case LiteralType::Null:
        throw EvalError{"null is not a parameter value"};
    }
    throw EvalError{"unsupported literal"};
}

PassParamValue evalNode(const ast::node_ptr_t &node);

PassParamValue evalUnary(ast::UnaryDataOp op, const PassParamValue &v) {
    switch (op) {
    case ast::UnaryDataOp::Not:
        if (auto *b = std::get_if<bool>(&v))
            return !*b;
        throw EvalError{std::format("'!' needs a bool, not a {}", kindOf(v))};
    case ast::UnaryDataOp::Neg:
        if (auto *i = std::get_if<int64_t>(&v)) {
            if (*i == INT64_MIN)
                throw EvalError{"integer overflow in negation"};
            return -*i;
        }
        if (auto *d = std::get_if<double>(&v))
            return -*d;
        throw EvalError{std::format("'-' needs a number, not a {}", kindOf(v))};
    case ast::UnaryDataOp::Inv:
        if (auto *i = std::get_if<int64_t>(&v))
            return ~*i;
        throw EvalError{std::format("'~' needs an int, not a {}", kindOf(v))};
    }
    throw EvalError{"unsupported operator"};
}

PassParamValue evalBinary(ast::BinaryDataOp op, const PassParamValue &a, const PassParamValue &b) {
    using Op       = ast::BinaryDataOp;
    const auto bad = [&]() -> EvalError {
        return EvalError{std::format(
            "operator '{}' does not apply to {} and {}",
            ast::to_string(op),
            kindOf(a),
            kindOf(b))};
    };
    switch (op) {
    case Op::And:
    case Op::LogicalAnd:
    case Op::Or:
    case Op::LogicalOr:
        if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
            const bool x = std::get<bool>(a), y = std::get<bool>(b);
            return (op == Op::And || op == Op::LogicalAnd) ? (x && y) : (x || y);
        }
        throw bad();
    case Op::Eq:
    case Op::Neq:
    case Op::StrictEq:
    case Op::StrictNeq: {
        bool eq;
        if (isNumber(a) && isNumber(b)) {
            eq = (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b))
                     ? std::get<int64_t>(a) == std::get<int64_t>(b)
                     : asDouble(a) == asDouble(b);
        } else if (a.index() == b.index()) {
            eq = a == b;
        } else {
            throw bad();
        }
        return (op == Op::Eq || op == Op::StrictEq) ? eq : !eq;
    }
    case Op::Less:
    case Op::LessEq:
    case Op::Greater:
    case Op::GreaterEq: {
        int cmp;
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            const int64_t x = std::get<int64_t>(a), y = std::get<int64_t>(b);
            cmp = x < y ? -1 : x > y ? 1 : 0;
        } else if (isNumber(a) && isNumber(b)) {
            const double x = asDouble(a), y = asDouble(b);
            cmp = x < y ? -1 : x > y ? 1 : 0;
        } else if (
            std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
            cmp = std::get<std::string>(a).compare(std::get<std::string>(b));
        } else {
            throw bad();
        }
        return op == Op::Less      ? cmp < 0
               : op == Op::LessEq  ? cmp <= 0
               : op == Op::Greater ? cmp > 0
                                   : cmp >= 0;
    }
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Mod:
    case Op::Pow: {
        if (op == Op::Add && std::holds_alternative<std::string>(a) &&
            std::holds_alternative<std::string>(b)) {
            return std::get<std::string>(a) + std::get<std::string>(b);
        }
        if (!isNumber(a) || !isNumber(b))
            throw bad();
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            const int64_t x = std::get<int64_t>(a), y = std::get<int64_t>(b);
            int64_t r     = 0;
            bool overflow = false;
            switch (op) {
            case Op::Add:
                overflow = __builtin_add_overflow(x, y, &r);
                break;
            case Op::Sub:
                overflow = __builtin_sub_overflow(x, y, &r);
                break;
            case Op::Mul:
                overflow = __builtin_mul_overflow(x, y, &r);
                break;
            case Op::Div:
            case Op::Mod:
                if (y == 0)
                    throw EvalError{"division by zero"};
                if (x == INT64_MIN && y == -1)
                    throw EvalError{"integer overflow"};
                r = op == Op::Div ? x / y : x % y;
                break;
            case Op::Pow: {
                if (y < 0)
                    throw EvalError{"an int raised to a negative power; use a float"};
                r = 1;
                for (int64_t k = 0; k < y && !overflow; ++k)
                    overflow = __builtin_mul_overflow(r, x, &r);
                break;
            }
            default:
                break;
            }
            if (overflow)
                throw EvalError{"integer overflow"};
            return r;
        }
        const double x = asDouble(a), y = asDouble(b);
        switch (op) {
        case Op::Add:
            return x + y;
        case Op::Sub:
            return x - y;
        case Op::Mul:
            return x * y;
        case Op::Div:
            return x / y;
        case Op::Mod:
            return std::fmod(x, y);
        default:
            return std::pow(x, y);
        }
    }
    default:
        throw EvalError{
            std::format("operator '{}' is not allowed in a constant", ast::to_string(op))};
    }
}

PassParamValue evalNode(const ast::node_ptr_t &node) {
    if (node->type() != ast::LoadType::Data) {
        throw EvalError{"not a constant expression"};
    }
    const auto data = std::dynamic_pointer_cast<ast::DataLoad>(node->load());
    switch (data->dataType()) {
    case ast::DataType::Literal:
        return evalLiteral(node->loadAs<ast::LiteralLoad>()->value());
    case ast::DataType::UnaryExpr:
        return evalUnary(node->loadAs<ast::UnaryExprLoad>()->op(), evalNode(node->at(0)));
    case ast::DataType::BinaryExpr:
        return evalBinary(
            node->loadAs<ast::BinaryExprLoad>()->op(),
            evalNode(node->at(0)),
            evalNode(node->at(1)));
    case ast::DataType::Ref:
        throw EvalError{std::format(
            "'{}' is not a constant (only literals and operators on them are allowed)",
            node->loadAs<ast::RefDataLoad>()->ref().toString())};
    default:
        throw EvalError{"only bool, int, float and string constants are allowed"};
    }
}

std::string withArticle(const char *type) {
    return (type[0] == 'i' ? "an " : "a ") + std::string(type);
}

bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string trimmed(const std::string &s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return std::string();
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

} // namespace

// -----------------------------------------------------------------------------
// parseSpec
// -----------------------------------------------------------------------------

std::optional<std::string>
camel::execute::detail::parseSpec(const std::string &spec, ParsedSpec &out) {
    out               = {};
    const size_t open = spec.find('(');
    if (open == std::string::npos) {
        out.name = trimmed(spec);
        return std::nullopt;
    }
    out.name           = trimmed(spec.substr(0, open));
    const size_t close = spec.find_last_not_of(" \t\r\n");
    if (spec[close] != ')') {
        return std::format("column {}: missing ')' after the parameters", spec.size() + 1);
    }
    const std::string argText = spec.substr(open + 1, close - open - 1);
    if (trimmed(argText).empty()) {
        return std::nullopt;
    }

    // The parameters are read by the language's parser, as the arguments of a call.
    const std::string source = "let v = f(" + argText + ");\n";
    auto sources             = std::make_shared<camel::source::SourceContext>();
    auto diags =
        std::make_shared<camel::core::error::Diagnostics>("pass", "<pass specification>", sources);
    camel::parse::CamelParser parser(diags);
    parser.setSourceContext(sources);
    std::istringstream is(source);
    if (!parser.parse(is) || !parser.ast()) {
        std::string why   = "the parameters are not valid Camel syntax";
        const auto errors = diags->errors();
        if (!errors.empty()) {
            why += ": " + errors.front()->message;
            while (!why.empty() && why.back() == '.')
                why.pop_back();
        }
        return why;
    }
    // Module -> Stmts -> DataDecl -> Data -> call -> arguments
    try {
        auto node        = parser.ast();
        node             = node->at(2)->at(0); // DataDecl
        node             = node->at(1)->at(0); // the call
        const auto &args = node->at(1);
        size_t cursor    = open + 1;
        for (size_t i = 0; i < args->size(); ++i) {
            const auto &arg = args->at(i);
            const bool assign =
                arg->type() == ast::LoadType::Data &&
                std::dynamic_pointer_cast<ast::BinaryExprLoad>(arg->load()) &&
                arg->loadAs<ast::BinaryExprLoad>()->op() == ast::BinaryDataOp::Assign &&
                std::dynamic_pointer_cast<ast::RefDataLoad>(arg->at(0)->load());
            if (!assign) {
                return std::format(
                    "parameter {} is not written key=value (a pass takes named parameters only)",
                    i + 1);
            }
            WrittenParam param;
            param.key = arg->at(0)->loadAs<ast::RefDataLoad>()->ref().toString();
            // The column of the key: the next occurrence as a whole word.
            size_t at = cursor;
            while ((at = spec.find(param.key, at)) != std::string::npos) {
                const bool left  = at == 0 || !isIdentifierChar(spec[at - 1]);
                const size_t end = at + param.key.size();
                const bool right = end >= spec.size() || !isIdentifierChar(spec[end]);
                if (left && right)
                    break;
                ++at;
            }
            param.column = at == std::string::npos ? open + 2 : at + 1;
            if (at != std::string::npos)
                cursor = at + param.key.size();
            try {
                param.value = evalNode(arg->at(1));
            } catch (const EvalError &e) {
                return std::format(
                    "column {}: parameter '{}': {}",
                    param.column,
                    param.key,
                    e.message);
            }
            out.params.push_back(std::move(param));
        }
    } catch (const std::out_of_range &) {
        return "the parameters could not be read";
    }
    return std::nullopt;
}

std::string camel::execute::detail::describeParams(const std::vector<PassParam> &declared) {
    if (declared.empty()) {
        return "no parameters";
    }
    std::string text;
    for (const auto &p : declared) {
        if (!text.empty())
            text += ", ";
        text += p.name + ": " + passParamTypeName(p.type) + " = ";
        if (auto *b = std::get_if<bool>(&p.defaultValue))
            text += *b ? "true" : "false";
        else if (auto *i = std::get_if<int64_t>(&p.defaultValue))
            text += std::to_string(*i);
        else if (auto *d = std::get_if<double>(&p.defaultValue))
            text += std::format("{}", *d);
        else
            text += "\"" + std::get<std::string>(p.defaultValue) + "\"";
    }
    return text;
}

std::optional<std::string> camel::execute::detail::bindArgs(
    const std::string &passName, const std::vector<PassParam> &declared, const ParsedSpec &parsed,
    PassArgs &args) {
    for (const auto &p : declared) {
        args.set(p.name, p.defaultValue, false);
    }
    std::set<std::string> seen;
    for (const auto &w : parsed.params) {
        const auto it = std::find_if(declared.begin(), declared.end(), [&](const PassParam &p) {
            return p.name == w.key;
        });
        if (it == declared.end()) {
            return std::format(
                "column {}: '{}' has no parameter '{}' (it takes: {})",
                w.column,
                passName,
                w.key,
                describeParams(declared));
        }
        if (!seen.insert(w.key).second) {
            return std::format("column {}: parameter '{}' is written twice", w.column, w.key);
        }
        PassParamValue value = w.value;
        if (it->type == PassParamType::Float && std::holds_alternative<int64_t>(value)) {
            value = static_cast<double>(std::get<int64_t>(value));
        }
        bool ok = false;
        switch (it->type) {
        case PassParamType::Bool:
            ok = std::holds_alternative<bool>(value);
            break;
        case PassParamType::Int:
            ok = std::holds_alternative<int64_t>(value);
            break;
        case PassParamType::Float:
            ok = std::holds_alternative<double>(value);
            break;
        case PassParamType::String:
            ok = std::holds_alternative<std::string>(value);
            break;
        }
        if (!ok) {
            return std::format(
                "column {}: parameter '{}' is {}, not {}",
                w.column,
                w.key,
                withArticle(kindOf(value)),
                withArticle(passParamTypeName(it->type)));
        }
        args.set(w.key, std::move(value), true);
    }
    return std::nullopt;
}
