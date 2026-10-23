// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "pass_spec.h"

#include "camel/core/source/manager.h"
#include "camel/parse/ast.h"
#include "camel/parse/parse.h"
#include "camel/utils/escape.h"
#include "camel/utils/token.h"

#include <algorithm>
#include <cctype>
#include <atomic>
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
        } else if (c == ',') {
            add();
            continue;
        }
        current += c;
    }
    add();
    return out;
}

std::vector<std::string> groupPassWords(const std::vector<std::string> &words, size_t first) {
    std::vector<std::string> out;
    for (size_t i = first; i < words.size(); ++i) {
        const std::string &word = words[i];
        const size_t equals     = word.find('=');
        // A parameter is a word whose text before `=` is a bare name (`safe_fp=true`); a word
        // that is a whole pass (`std::fvm stats=""`) starts a specification of its own.
        const bool parameter =
            equals != std::string::npos && equals > 0 &&
            std::all_of(
                word.begin(), word.begin() + static_cast<std::ptrdiff_t>(equals), [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
                });
        if (parameter && !out.empty()) {
            out.back() += " " + word;
        } else {
            out.push_back(word);
        }
    }
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

} // namespace

// -----------------------------------------------------------------------------
// declarePassParams
// -----------------------------------------------------------------------------

namespace {

PassParamType declarationType(const std::string &written, const std::string &parameter) {
    if (written == "bool") {
        return PassParamType::Bool;
    }
    if (written == "int" || written == "i32" || written == "i64" || written == "int32" ||
        written == "int64") {
        return PassParamType::Int;
    }
    if (written == "float" || written == "f32" || written == "f64" || written == "float32" ||
        written == "float64") {
        return PassParamType::Float;
    }
    if (written == "string" || written == "str") {
        return PassParamType::String;
    }
    throw std::runtime_error(std::format(
        "pass declaration: the parameter '{}' has the type '{}'; a pass parameter is bool, int, "
        "float or string",
        parameter,
        written));
}

PassParamValue zeroValue(PassParamType type) {
    switch (type) {
    case PassParamType::Bool:
        return false;
    case PassParamType::Int:
        return int64_t{0};
    case PassParamType::Float:
        return 0.0;
    case PassParamType::String:
        return std::string();
    }
    return false;
}

bool fits(PassParamType type, const PassParamValue &value) {
    switch (type) {
    case PassParamType::Bool:
        return std::holds_alternative<bool>(value);
    case PassParamType::Int:
        return std::holds_alternative<int64_t>(value);
    case PassParamType::Float:
        return std::holds_alternative<double>(value);
    case PassParamType::String:
        return std::holds_alternative<std::string>(value);
    }
    return false;
}

/// The parameters of a declaration are the NamedPair children of its function type, in order.
void collectParamPairs(const ast::node_ptr_t &node, std::vector<ast::node_ptr_t> &out) {
    if (node->type() == ast::LoadType::NamedPair) {
        out.push_back(node);
        return;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        collectParamPairs(node->at(i), out);
    }
}

std::string firstParseError(
    const std::shared_ptr<camel::core::error::Diagnostics> &diags, std::string prefix) {
    const auto errors = diags->errors();
    if (errors.empty()) {
        return prefix;
    }
    std::string why = prefix + ": " + errors.front()->message;
    while (!why.empty() && why.back() == '.') {
        why.pop_back();
    }
    return why;
}

} // namespace

std::vector<PassParam> camel::execute::detail::readPassParams(
    const ast::node_ptr_t &withParams, std::string_view passName) {
    std::vector<PassParam> declared;
    try {
        std::vector<ast::node_ptr_t> pairs;
        collectParamPairs(withParams, pairs);
        bool sawDefault = false;
        std::string defaulted;
        for (const auto &pair : pairs) {
            const auto named = pair->loadAs<ast::NamedPairLoad>();
            if (named->isRest() || named->isVar() || named->isConst() || named->isConstas() ||
                named->isTupleParam()) {
                throw std::runtime_error(std::format(
                    "pass declaration '{}': the parameter '{}' is not an ordinary parameter",
                    passName,
                    named->getRef().toString()));
            }
            const std::string name = named->getRef().toString();
            if (pair->size() == 0 || pair->at(0)->type() != ast::LoadType::Type) {
                throw std::runtime_error(std::format(
                    "pass declaration '{}': the parameter '{}' has no type", passName, name));
            }
            const auto typeLoad =
                std::dynamic_pointer_cast<ast::TypeLoad>(pair->at(0)->load());
            if (!typeLoad || typeLoad->typeType() != ast::TypeType::Ref) {
                throw std::runtime_error(std::format(
                    "pass declaration '{}': the parameter '{}' has a type that is not a pass "
                    "parameter type",
                    passName,
                    name));
            }
            PassParam param;
            param.name = name;
            param.type = declarationType(
                pair->at(0)->loadAs<ast::RefTypeLoad>()->ref().toString(), name);
            if (pair->size() > 1 && pair->at(1)->size() > 0) {
                const auto &expr = pair->at(1)->at(0);
                PassParamValue value;
                try {
                    value = evalNode(expr);
                } catch (const EvalError &e) {
                    throw std::runtime_error(std::format(
                        "pass declaration '{}': the default of '{}' is not a constant: {}",
                        passName,
                        name,
                        e.message));
                }
                if (param.type == PassParamType::Float &&
                    std::holds_alternative<int64_t>(value)) {
                    value = static_cast<double>(std::get<int64_t>(value));
                }
                if (!fits(param.type, value)) {
                    throw std::runtime_error(std::format(
                        "pass declaration '{}': the default of '{}' is {}, not {}",
                        passName,
                        name,
                        withArticle(kindOf(value)),
                        withArticle(passParamTypeName(param.type))));
                }
                param.defaultValue = std::move(value);
                param.required     = false;
                sawDefault = true;
                defaulted  = name;
            } else {
                if (sawDefault) {
                    throw std::runtime_error(std::format(
                        "pass declaration '{}': the parameter '{}' has no default and follows "
                        "'{}', which has one",
                        passName,
                        name,
                        defaulted));
                }
                param.defaultValue = zeroValue(param.type);
                param.required     = true;
            }
            declared.push_back(std::move(param));
        }
    } catch (const std::out_of_range &) {
        throw std::runtime_error(
            std::format("pass declaration '{}': the parameters could not be read", passName));
    }
    return declared;
}

// -----------------------------------------------------------------------------
// parseSpec
// -----------------------------------------------------------------------------

std::optional<ParamProblem>
camel::execute::detail::parseSpec(const std::string &spec, ParsedSpec &out) {
    out                       = {};
    const size_t firstContent = spec.find_first_not_of(" \t\r\n");
    if (firstContent == std::string::npos) {
        ParamProblem problem;
        problem.kind   = ParamProblemKind::Syntax;
        problem.text   = "the pass specification is empty";
        problem.column = 0;
        problem.length = 1;
        return problem;
    }
    size_t nameEnd = firstContent;
    while (nameEnd < spec.size() && !std::isspace(static_cast<unsigned char>(spec[nameEnd]))) {
        ++nameEnd;
    }
    out.name = spec.substr(firstContent, nameEnd - firstContent);

    // A parameter is a word of its own after the name. The parentheses form is gone.
    if (const size_t open = out.name.find('('); open != std::string::npos) {
        ParamProblem problem;
        problem.kind   = ParamProblemKind::Syntax;
        problem.text   = "a pass parameter is written key=value after the pass name, not in "
                         "parentheses";
        problem.column = firstContent + open;
        problem.length = 1;
        return problem;
    }

    size_t cursor = nameEnd;
    while (true) {
        while (cursor < spec.size() && std::isspace(static_cast<unsigned char>(spec[cursor]))) {
            ++cursor;
        }
        if (cursor >= spec.size()) {
            break;
        }
        const size_t wordStart = cursor;
        std::string word;
        char quote = 0;
        while (cursor < spec.size()) {
            const char c = spec[cursor];
            if (quote != 0) {
                word += c;
                ++cursor;
                if (c == '\\' && cursor < spec.size()) {
                    word += spec[cursor++];
                } else if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                word += c;
                ++cursor;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            word += c;
            ++cursor;
        }
        if (quote != 0) {
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Syntax;
            problem.text   = std::format("'{}': the string is not closed", word);
            problem.column = wordStart;
            problem.length = word.size();
            return problem;
        }
        const size_t equals = word.find('=');
        if (equals == std::string::npos) {
            ParamProblem problem;
            problem.kind = ParamProblemKind::Syntax;
            if (const size_t colon = word.find(':'); colon != std::string::npos &&
                                                     word.find("::") == std::string::npos) {
                problem.text = std::format(
                    "'{}': a pass parameter is written key=value on the command line; "
                    "key: value is the spelling of a named argument in Camel source",
                    word);
            } else {
                problem.text = std::format(
                    "'{}' is not a parameter: a parameter is written key=value", word);
            }
            problem.column = wordStart;
            problem.length = word.size();
            return problem;
        }
        const std::string key   = word.substr(0, equals);
        const std::string value = word.substr(equals + 1);
        if (key.empty() || !std::all_of(key.begin(), key.end(), isIdentifierChar)) {
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Syntax;
            problem.text   = std::format("'{}' is not a parameter name", key);
            problem.column = wordStart;
            problem.length = word.size();
            return problem;
        }
        if (value.empty()) {
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Syntax;
            problem.text   = std::format("the parameter '{}' has no value", key);
            problem.column = wordStart;
            problem.length = word.size();
            return problem;
        }

        // The value is a Camel constant expression, read by the language's parser as the value
        // of one named argument. Whether it has to be one is the declaration's business: a bool,
        // int or float parameter takes a constant of its type, while a string parameter takes the
        // word as written (design principles 9.3). The declaration is not known here, so a value
        // that does not read as a constant keeps its text and the reason, and `bindArgs` asks for
        // the one the declared type needs.
        WrittenParam param;
        param.key    = key;
        param.raw    = value;
        param.column = wordStart;
        param.length = word.size();
        const std::string source = "let v = f(" + key + ": " + value + ");\n";
        auto sources             = std::make_shared<camel::source::SourceContext>();
        auto diags               = std::make_shared<camel::core::error::Diagnostics>(
            "pass", "<pass specification>", sources);
        camel::parse::CamelParser parser(diags);
        parser.setSourceContext(sources);
        std::istringstream is(source);
        if (!parser.parse(is) || diags->hasErrors() || !parser.ast()) {
            param.constantProblem = firstParseError(
                diags, std::format("the value of '{}' is not a constant", key));
            out.params.push_back(std::move(param));
            continue;
        }
        try {
            const auto &call  = parser.ast()->at(2)->at(0)->at(1)->at(0);
            const auto &named = call->at(2)->at(0);
            param.value       = evalNode(named->at(0));
            param.isConstant  = true;
        } catch (const std::out_of_range &) {
            param.constantProblem = std::format("the value of '{}' could not be read", key);
        } catch (const EvalError &e) {
            param.constantProblem =
                std::format("the value of '{}' is not a constant: {}", key, e.message);
        }
        out.params.push_back(std::move(param));
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
        text += p.name + ": " + passParamTypeName(p.type);
        if (p.required) {
            text += " (required)";
            continue;
        }
        text += " = ";
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

std::optional<ParamProblem> camel::execute::detail::bindArgs(
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
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Unknown;
            problem.name   = w.key;
            problem.column = w.column;
            problem.length = w.key.size();
            return problem;
        }
        if (!seen.insert(w.key).second) {
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Duplicate;
            problem.name   = w.key;
            problem.column = w.column;
            problem.length = w.key.size();
            return problem;
        }
        // How the word is read is the declared type's business, not the text's (design
        // principles 9.3): a string parameter takes the word as written, so `out=model.onnx` names
        // that file, while a bool, int or float parameter takes a constant of its type and refuses
        // a bare word. A value the language read as a string constant keeps that reading, which is
        // how a quoted value with a space (`out="a b.onnx"`) arrives whole.
        PassParamValue value;
        if (it->type == PassParamType::String) {
            value = (w.isConstant && std::holds_alternative<std::string>(w.value)) ? w.value
                                                                                 : w.raw;
            args.set(w.key, std::move(value), true);
            continue;
        }
        if (!w.isConstant) {
            ParamProblem problem;
            problem.kind   = ParamProblemKind::Syntax;
            problem.text   = w.constantProblem;
            problem.name   = w.key;
            problem.column = w.column;
            problem.length = w.length;
            return problem;
        }
        value = w.value;
        if (it->type == PassParamType::Float && std::holds_alternative<int64_t>(value)) {
            value = static_cast<double>(std::get<int64_t>(value));
        }
        if (!fits(it->type, value)) {
            ParamProblem problem;
            problem.kind     = ParamProblemKind::WrongType;
            problem.name     = w.key;
            problem.found    = kindOf(value);
            problem.expected = passParamTypeName(it->type);
            problem.column   = w.column;
            problem.length   = w.key.size();
            return problem;
        }
        args.set(w.key, std::move(value), true);
    }
    // A parameter without a default has to be written: the pass has no value to fall back to.
    for (const auto &p : declared) {
        if (p.required && !seen.contains(p.name)) {
            ParamProblem problem;
            problem.kind = ParamProblemKind::Missing;
            problem.name = p.name;
            return problem;
        }
    }
    return std::nullopt;
}


