// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// What this file owns: the values written as data: struct/array/tuple literals, shape
// comprehension, ref and constas, the plain literals, and the formatted string -- which is built
// here as the `format` call and the pattern it is the shorthand of, together with the reading that
// pattern needs.

#include "builder_internal.h"

/*
structData
    : '{' (pairedValues ','?)? '}' // no list comprehension because the struct is immutable
    | '{' staticForExpr '}'        // a struct comprehension
    ;
*/
Value Builder::visitStructData(cst::StructData context) {
    ENTER("StructData");
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        // `{ static for n, F in fields T => e }`: the value has the fields of `T`, each from one
        // round; the generator is built once and the rounds are packed in the graph builder.
        node_ptr_t compNode = createNodeAs<StructCompLoad>();
        setNodeTokenRangeByContext(compNode, context);
        *compNode << any2node(visitStaticForExpr(generator));
        LEAVE("StructData");
        return compNode;
    }
    node_ptr_t dataNode = createNodeAs<StructDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::PairedValues>()) {
        *dataNode << any2node(visitPairedValues(context->get<cst::PairedValues>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("NamedData");
    }
    LEAVE("StructData");
    return dataNode;
}

/*
arrayData
    : '[' ((indexValues ','?) | dataExpr FOR identRef IN dataExpr (IF dataExpr)?)? ']'
    ;
*/
Value Builder::visitArrayData(cst::ArrayData context) {
    ENTER("ArrayData");
    node_ptr_t dataNode = createNodeAs<ArrayDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::IndexValues>()) {
        *dataNode << any2node(visitIndexValues(context->get<cst::IndexValues>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
        if (context->tok(TK::KwFor)) {
            // `[e for x in xs if c]` (design principles 4.2): `filter` then `map`. The parts
            // are kept as they are written, after the (empty) element list: the element
            // expression, the name, the source and the optional condition. The GCT builder
            // lowers them to the two calls (visitComprehension).
            std::vector<syn::Node> exprs;
            for (const auto &expr : context->exprs()) {
                exprs.push_back(expr);
            }
            // The name after `for` is an expression node of its own when a bare name is one.
            const size_t written  = 2 + (context->has(TK::KwIf) ? 1 : 0);
            const bool nameIsExpr = exprs.size() == written + 1;
            const syn::Node nameSyntax =
                nameIsExpr ? exprs[1] : syn::Node(context->get<cst::IdentRef>());
            if (nameIsExpr) {
                exprs.erase(exprs.begin() + 1);
            }
            *dataNode << any2node(visitExpr(exprs[0]));
            node_ptr_t nameNode = createNodeAs<RefDataLoad>(Reference(nameSyntax->text()));
            setNodeTokenRangeByContext(nameNode, nameSyntax);
            *dataNode << nameNode;
            *dataNode << any2node(visitExpr(exprs[1]));
            node_ptr_t condNode = createNodeAs<OptionalLoad>("Data");
            if (exprs.size() > 2) {
                setNodeTokenRangeByContext(condNode, exprs[2]);
                *condNode << any2node(visitExpr(exprs[2]));
            }
            *dataNode << condNode;
        }
    }
    LEAVE("ArrayData");
    return dataNode;
}

/*
tupleData
    : '(' dataList? ','? ')'
    | '(' staticForExpr ')'        // a tuple comprehension
    ;
*/
Value Builder::visitTupleData(cst::TupleData context) {
    ENTER("TupleData");
    if (const auto generator = context->get<cst::StaticForExpr>()) {
        node_ptr_t compNode = createNodeAs<TupleCompLoad>();
        setNodeTokenRangeByContext(compNode, context);
        *compNode << any2node(visitStaticForExpr(generator));
        LEAVE("TupleData");
        return compNode;
    }
    // `(e)`: a parenthesized expression, not a tuple (three children: the brackets and the
    // list; a trailing comma would be a fourth).
    if (context->get<cst::DataList>() && context->get<cst::DataList>()->exprs().size() == 1 &&
        context->childCount() == 3) {
        Value inner = visitExpr(context->get<cst::DataList>()->expr(0));
        LEAVE("TupleData");
        return inner;
    }
    node_ptr_t dataNode = createNodeAs<TupleDataLoad>();
    setNodeTokenRangeByContext(dataNode, context);
    if (context->get<cst::DataList>()) {
        *dataNode << any2node(visitDataList(context->get<cst::DataList>()));
    } else {
        *dataNode << createNodeAs<RepeatedLoad>("Data");
    }
    LEAVE("TupleData");
    return dataNode;
}

/*
shapeComp : typeExpr '{' staticForExpr '}' ;
The shape is child 0, the generator child 1. The tree keeps both: the graph builder reads the
generator's rounds and takes the field names and the kind from the shape.
*/
Value Builder::visitShapeComp(cst::ShapeComp context) {
    ENTER("ShapeComp");
    node_ptr_t compNode = createNodeAs<ShapeCompLoad>();
    setNodeTokenRangeByContext(compNode, context);
    *compNode << any2node(visitType(context->type()));
    *compNode << any2node(visitStaticForExpr(context->get<cst::StaticForExpr>()));
    LEAVE("ShapeComp");
    return compNode;
}

/*
primaryData : identRef | literal | arrayData | structData | tupleData | funcData ;
The tree has no node for the rule. A name as an operand is a RefData node, a literal a Literal.
*/
Value Builder::visitRefData(cst::RefData context) {
    Reference ref(context->text());
    node_ptr_t res = createNodeAs<RefDataLoad>(ref);
    setNodeTokenRangeByContext(res, context);
    return res;
}

Value Builder::visitConstasData(cst::ConstasData context) {
    Reference ref(context->get<cst::IdentDef>()->text());
    node_ptr_t res = createNodeAs<ConstAsLoad>(ref);
    setNodeTokenRangeByContext(res, context);
    return res;
}

Value Builder::visitLiteralData(cst::Literal context) {
    if (context->tok(TK::FString)) {
        return visitFStringData(context);
    }
    Literal literal = value_cast<Literal>(visitLiteral(context));
    node_ptr_t res  = createNodeAs<LiteralLoad>(literal);
    if (context->tok(TK::RawString)) {
        // A raw string's text is its value: the GCT must not decode anything in it.
        unwrapNodeAs<LiteralLoad>(res)->setFromRawString();
    }
    setNodeTokenRangeByContext(res, context);
    return res;
}

namespace {

/// The text of a formatted string that is not an interpolation: only `\${` is decoded here,
/// because it is what keeps `${` from starting an interpolation. Every character escape, the
/// backtick and the bare `\$` (not followed by `{`) included, is left for decodeEscapes, the one
/// table of every string literal.
std::string decodeFStringText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            if (text[i + 1] == '$' && i + 2 < text.size() && text[i + 2] == '{') {
                out += "${"; // `\${` is a literal `${`
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

/// An interpolation is empty when it holds no token that is neither white space nor a comment
///  White space is never a token, so this is: every token
/// is Eof or a comment. `Command` (the ignored `#!` line) and `Error` are not comments, and either
/// leaves the interpolation non-empty.
bool isEmptyInterpolation(const syn::TokenBuffer &tokens) {
    for (const syn::Token &token : tokens.tokens()) {
        switch (token.kind) {
        case syn::TokenKind::Eof:
        case syn::TokenKind::LineComment:
        case syn::TokenKind::DocComment:
        case syn::TokenKind::BlockComment:
            break;
        default:
            return false;
        }
    }
    return true;
}

/// Shown when the text taken as a format specification contains `=>`: the ':' of a lambda's
/// return type at the top level of an interpolation was read as the start of the specification.
/// The same sentence is used where the format check fails (gir/builder_operators.cpp).
constexpr std::string_view kLambdaNeedsParenthesesHint =
    "a lambda with a return type needs its own parentheses at the top level of an "
    "interpolation: the ':' of the return type is read as the start of the format "
    "specification. Write it as ${((x: int): int => x)}";

/// The end (just past the closing `}`) of a valid `\u{H…}` escape that starts at `i`, or npos.
/// Valid means what decodeEscapes decodes: 1 to 6 hex digits and a Unicode scalar value.
size_t unicodeEscapeEnd(std::string_view text, size_t i) {
    if (i + 2 >= text.size() || text[i] != '\\' || text[i + 1] != 'u' || text[i + 2] != '{') {
        return std::string_view::npos;
    }
    size_t j       = i + 3;
    size_t digits  = 0;
    uint32_t value = 0;
    while (j < text.size() && digits < 6 && std::isxdigit(static_cast<unsigned char>(text[j]))) {
        const char d = text[j];
        value        = value * 16 + (d <= '9' ? d - '0' : (d | 0x20) - 'a' + 10);
        ++digits;
        ++j;
    }
    if (digits == 0 || j >= text.size() || text[j] != '}' || !camel::utf8::isScalarValue(value)) {
        return std::string_view::npos;
    }
    return j + 1;
}

/// The pattern text for `format`: the literal braces of a formatted string are doubled, so
/// `format` prints them. A valid `\u{H…}` is a character escape, not format syntax: its braces are
/// left alone for decodeEscapes, which doubles a brace the escape produces. An invalid one
/// is kept as written, so its braces are format syntax and are doubled here.
std::string escapeFormatBraces(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\\') {
            // An even run of backslashes is `\\` pairs; the last backslash of an odd run opens
            // an escape.
            size_t run = 0;
            while (i + run < text.size() && text[i + run] == '\\') {
                ++run;
            }
            out.append(text.substr(i, run - (run % 2)));
            i += run - (run % 2);
            if (run % 2 == 0) {
                continue;
            }
            const size_t end = unicodeEscapeEnd(text, i);
            if (end != std::string_view::npos) {
                out.append(text.substr(i, end - i));
                i = end;
                continue;
            }
            out += text[i];
            ++i;
            continue;
        }
        if (c == '{' || c == '}') {
            out += c;
        }
        out += c;
        ++i;
    }
    return out;
}

/// A sub-tree of an interpolation has the token indices of its own buffer; a token range is
/// only a hint for a diagnostic, so it is moved to the formatted-string token, which is what
/// the file shows. Origins, which are exact, are untouched.
void setTokenRangeDeep(const node_ptr_t &node, size_t begin, size_t end) {
    if (!node) {
        return;
    }
    node->load()->setTokenRange(begin, end);
    for (const auto &child : *node) {
        setTokenRangeDeep(child, begin, end);
    }
}

} // namespace

/*
 * A formatted string is the same tree as `'...'->format<...>`: `format` applied to the
 * interpolated expressions, then to the pattern. Building that tree here keeps every later
 * stage (types, passes, schedulers, the C++ translator) unaware of the shorthand.
 *
 * The pattern is the text of the string with each `${EXPR}` replaced by `{}` (or `{:SPEC}`),
 * the braces of the literal text doubled and `\${` unescaped. The pieces of text between the
 * interpolations keep their escapes for decodeEscapes.
 */
Value Builder::visitFStringData(cst::Literal context) {
    ENTER("FStringData");
    const syn::TokenRef token = context->firstToken();
    const std::string &source = context->tree()->source();
    const std::string_view whole(source);
    const syn::TokenBuffer &buffer = context->tree()->tokens();
    const auto interpolations      = buffer.interpolationsOf(token->index());

    // Where an interpolation lies in the file: from its `$` to just past its `}`. A diagnostic
    // about the interpolation itself (not about the expression inside it) points here.
    const auto interpolationRange = [&buffer](const syn::FStringInterpolation &part) {
        return CharRange{
            {buffer.lineOf(part.open), buffer.columnOf(part.open)},
            {buffer.lineOf(part.close + 1), buffer.columnOf(part.close + 1)}};
    };

    std::vector<node_ptr_t> args;
    std::string pattern;
    size_t cursor = token->begin() + 1; // just past the opening backtick
    bool failed   = false;

    for (const auto &part : interpolations) {
        pattern += escapeFormatBraces(
            decodeFStringText(whole.substr(cursor, static_cast<size_t>(part.open) - cursor)));

        if (part.hasSpec) {
            pattern += "{:" +
                       std::string(whole.substr(
                           part.specBegin,
                           static_cast<size_t>(part.specEnd) - part.specBegin)) +
                       "}";
        } else {
            pattern += "{}";
        }
        cursor = static_cast<size_t>(part.close) + 1;

        syn::TokenBuffer tokens = syn::Lexer::tokenizeRange(source, part.exprBegin, part.exprEnd);
        // No token that is neither white space nor a comment is `${}`, `${ }`, `${:spec}` or
        // `${/*c*/}`: an interpolation without an expression is its own mistake, not a parse
        // error inside an expression (item 1). `${/**/ 1}` has a token and is
        // an expression.
        if (isEmptyInterpolation(tokens)) {
            diags_->of(SyntaxDiag::EmptyInterpolation).at(interpolationRange(part)).commit();
            failed = true;
            continue;
        }
        const size_t beforeExpression = diags_->count();
        syn::Tree sub                 = syn::Parser::parseExpression(std::move(tokens));
        syn::reportLexicalErrors(sub.tokens(), *diags_);
        if (sub.tokens().errors().empty()) {
            syn::reportSyntaxErrors(sub, *diags_);
        }
        if (!sub.root() || sub.hasErrors()) {
            // The first ':' outside every bracket ends the expression. A lambda with a return
            // type has such a ':' at its top level, so its `=>` ended up in the specification
            // and the expression is incomplete: say what the fix is (item 1).
            if (part.hasSpec) {
                const std::string_view spec = whole.substr(
                    part.specBegin,
                    static_cast<size_t>(part.specEnd) - part.specBegin);
                if (spec.find("=>") != std::string_view::npos) {
                    diags_->addRelatedToErrorsFrom(
                        beforeExpression,
                        DiagLabel{
                            interpolationRange(part),
                            std::string(kLambdaNeedsParenthesesHint)});
                }
            }
            failed = true;
            continue;
        }
        node_ptr_t exprNode = any2node(visitExpr(sub.root()));
        setTokenRangeDeep(exprNode, token->index(), token->index() + 1);
        args.push_back(exprNode);
    }
    pattern += escapeFormatBraces(
        decodeFStringText(whole.substr(cursor, static_cast<size_t>(token->end()) - 1 - cursor)));

    if (failed) {
        // The errors are already reported; a placeholder keeps the tree well formed.
        node_ptr_t res = createNodeAs<LiteralLoad>(Literal(LiteralType::String, ""));
        setNodeTokenRangeByContext(res, context);
        LEAVE("FStringData");
        return res;
    }

    node_ptr_t literalNode = createNodeAs<LiteralLoad>(Literal(LiteralType::String, pattern));
    setNodeTokenRangeByContext(literalNode, context);
    unwrapNodeAs<LiteralLoad>(literalNode)->setFromFormattedString();

    node_ptr_t refNode = createNodeAs<RefDataLoad>(qualifyRef(Reference("format")));
    setNodeTokenRangeByContext(refNode, context);

    node_ptr_t withArgs = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(withArgs, context);
    for (const auto &arg : args) {
        *withArgs << arg;
    }
    node_ptr_t withNamed = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(withNamed, context);

    node_ptr_t bindNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Bind);
    setNodeTokenRangeByContext(bindNode, context);
    *bindNode << refNode << withArgs << withNamed;

    node_ptr_t normArgs = createNodeAs<RepeatedLoad>("Data");
    setNodeTokenRangeByContext(normArgs, context);
    *normArgs << literalNode;
    node_ptr_t normNamed = createNodeAs<RepeatedLoad>("NamedData");
    setNodeTokenRangeByContext(normNamed, context);

    node_ptr_t callNode = createNodeAs<ReservedExprLoad>(ReservedDataOp::Call);
    setNodeTokenRangeByContext(callNode, context);
    *callNode << bindNode << normArgs << normNamed;
    LEAVE("FStringData");
    return callNode;
}

/*
literal
    : ('+' | '-')? INTEGER
    | ('+' | '-')? REAL
    | STRING
    | MULTI_STR
    | FSTRING
    | TRUE
    | FALSE
    | NULL
    ;
*/
Value Builder::visitLiteral(cst::Literal context) {
    LiteralType type;
    string data = context->text();
    if (context->tok(TK::Integer)) {
        type = LiteralType::Integer;
    } else if (context->tok(TK::Real)) {
        type = LiteralType::Real;
    } else if (context->tok(TK::Imaginary)) {
        // Keep the trailing `i`: the literal's text is what the GCT reads the value from.
        type = LiteralType::Imaginary;
    } else if (context->tok(TK::String) || context->tok(TK::FString)) {
        // The text of a string constant, as a pattern takes it (`case 'x'`). The formatted
        // strings of an expression go to visitFStringData (visitLiteralData), and the pattern
        // production does not take one, so what arrives here is a plain string.
        data = data.substr(1, data.size() - 2); // Remove quotes
        type = LiteralType::String;
    } else if (context->tok(TK::RawString)) {
        // `r"..."` and `r#"..."#`: the text between the delimiters is the value, with nothing
        // decoded. Strip `r`, the `#`s that open it and the quote, then the quote and the same
        // number of `#`s at the end; the caller marks the load raw so that the GCT does not run
        // the escape table over it (a `\n` written in a raw string is a backslash and an `n`).
        size_t hashes = 0;
        while (1 + hashes < data.size() && data[1 + hashes] == '#') {
            ++hashes;
        }
        const size_t inner = hashes + 2; // `r`, the `#`s, the opening quote
        GUARD(
            data.size() >= inner + hashes + 1,
            "A raw string token is `r`, some `#`, `\"`, its text, `\"` and the same `#`.");
        data = data.substr(inner, data.size() - inner - 1 - hashes);
        type = LiteralType::String;
    } else if (context->tok(TK::MultiStr)) {
        data = data.substr(3, data.size() - 6); // Remove triple quotes
        type = LiteralType::String;
    } else if (context->tok(TK::KwTrue) || context->tok(TK::KwFalse)) {
        type = LiteralType::Boolean;
    } else if (context->tok(TK::KwNull)) {
        type = LiteralType::Null;
    } else {
        throw std::runtime_error("Unknown literal type in visitLiteral");
    }
    Literal literal(type, data);
    return literal;
}
