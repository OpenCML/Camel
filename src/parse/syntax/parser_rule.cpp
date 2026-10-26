// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The productions of rule and fact declarations (rewrite design 2 and 7.5): a rule is a pattern,
 * what it becomes and, optionally, a condition; a fact says what the compiler may rely on for a
 * value, by reading how the value was built. `rule`, `when` and `fact` are names that mean
 * something only here (`rule` and `fact` at the start of a declaration, `when` between the
 * parameters and the body), so a program may still have a variable called `rule`.
 */

#include "parser_impl.h"

namespace camel::parse::syntax {

/// Whether the current token starts a rule declaration: the name `rule` followed by the name of
/// the rule, then its type parameters, its parameters or its context (`rule mul_one(`,
/// `rule sub_self::<T>(`, `rule fuse<f, g>(`). `n` is the offset of the `rule` token (1 after
/// `export`).
bool ParserImpl::atRuleDecl(uint32_t n) const {
    if (peek(n) != TK::Identifier || peek(n + 1) != TK::Identifier) {
        return false;
    }
    const TK after = peek(n + 2);
    if (after != TK::LParen && after != TK::ColonColon && after != TK::Lt && after != TK::LAngle) {
        return false;
    }
    // The text of the token at offset `n`.
    uint32_t i = pos_;
    for (uint32_t k = 0; k < n; ++k) {
        ++i;
        while (isTrivia(toks_[i].kind)) {
            ++i;
        }
    }
    return tree_.tokens_.text(i) == "rule";
}

/// ruleDecl : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
///            ruleContext? parentParams whenClause? '{' ruleBody '}'
/// The first IDENTIFIER is the name `rule`; the condition, when there is one, is the `whenClause`
/// between the parameters and the body. The parameters are the pattern variables; `const x: T`
/// only matches a constant. The tags before it are the rule's requirements (`#requires(reassociate)`,
/// design principles 7.2); the AST reads them from the tree. The `with` parameters are written
/// where a function writes its own, before `export` (design principles 4.4, 5.5): the rule's
/// context parameters, with the same defaults a function's have.
/// The condition is part of the signature, so it is written between the parameters and the body,
/// in the one position the parser reads it in (design principles 4.4): a `when` after
/// the closing brace is not kept as a second spelling of the same thing, and is reported by name
/// (`SyntaxDiag::WhenAfterBody`) rather than as a token that did not fit.
void ParserImpl::parseRuleDecl() {
    const size_t m = mark();
    parseFrontMatter();
    parseRuleDeclRest(m);
}

void ParserImpl::parseRuleDeclRest(size_t m) {
    if (eat(TK::KwWith)) {
        parseAngledParams();
    }
    eat(TK::KwExport);
    bump(); // `rule`
    parseIdentDef();
    if (cur_ == TK::ColonColon && (peek(1) == TK::Lt || peek(1) == TK::LAngle)) {
        parseTypeParams();
    }
    if (cur_ == TK::Lt || cur_ == TK::LAngle) {
        parseRuleContext();
    }
    parseParentParams();
    // The applicability condition, between the parameters and the body: it is part of the
    // signature (design principles 4.4), written in the same position a function writes it in.
    if (atWord(0, "when")) {
        parseWhenClause();
    }
    if (!panic_ && expect(TK::LBrace)) {
        {
            Scope scope(*this, false);
            parseRuleBody();
        }
        expect(TK::RBrace);
    }
    // A `when` after the closing brace is not read as a condition any more. It is
    // not read as a condition any more, and it is not reported as a token that did not fit: the
    // program is told where the condition belongs.
    if (!panic_ && cur_ == TK::Identifier && tree_.tokens_.text(pos_) == "when") {
        error(SyntaxError::Kind::WhenAfterBody, {}, pos_);
        bump(); // `when`: consumed, so the parse goes on after the condition
        parseExpr();
    }
    close(m, NK::RuleDecl);
}

/// ruleContext : '<' identDef (',' identDef)* ','? '>'
/// The names a rule is generic over besides its parameters (`rule fuse<f, g>(xs: int[])`).
void ParserImpl::parseRuleContext() {
    const size_t m = mark();
    bump(); // `<`
    parseIdentDef();
    while (eat(TK::Comma) && cur_ == TK::Identifier) {
        parseIdentDef();
    }
    if (!panic_ && !eat(TK::Gt)) {
        unexpected({e(TK::Comma), e(TK::Gt)});
    }
    close(m, NK::RuleContext);
}

/// ruleBody : dataExpr ('=>' | '<=>') dataExpr
/// `=>` replaces the pattern by the right side; `<=>` says both sides are the same value.
void ParserImpl::parseRuleBody() {
    const size_t m = mark();
    parseExpr();
    if (!panic_) {
        if (cur_ == TK::FatArrow || cur_ == TK::Equiv) {
            bump();
        } else {
            unexpected({e(TK::FatArrow), e(TK::Equiv)});
        }
    }
    if (!panic_) {
        parseExpr();
    }
    close(m, NK::RuleBody);
}

/// The index of the token at the offset `n` from the current position: the token `peek(n)` reads,
/// trivia skipped. The lookaheads below walk the tokens themselves, and need the index they read.
uint32_t ParserImpl::tokenIndexAt(uint32_t n) const {
    uint32_t i = pos_;
    for (uint32_t k = 0; k < n; ++k) {
        ++i;
        while (isTrivia(toks_[i].kind)) {
            ++i;
        }
    }
    return i;
}

/// The offset just after the `<...>` whose `<` is at the offset `angle`, or kNone when the
/// brackets do not close. A `with` parameter list is skipped textually: the declaration it
/// belongs to is what decides whether it is read at all.
uint32_t ParserImpl::afterAngledParams(uint32_t angle) const {
    if (peek(angle) != TK::Lt && peek(angle) != TK::LAngle) {
        return kNone;
    }
    uint32_t i = tokenIndexAt(angle);
    int32_t depth = 0;
    while (toks_[i].kind != TK::Eof) {
        const TK kind = toks_[i].kind;
        if (kind == TK::Lt || kind == TK::LAngle) {
            ++depth;
        } else if (kind == TK::Gt) {
            if (--depth == 0) {
                break;
            }
        }
        ++i;
        while (isTrivia(toks_[i].kind)) {
            ++i;
        }
    }
    if (toks_[i].kind == TK::Eof) {
        return kNone;
    }
    uint32_t count = 0;
    for (uint32_t j = pos_; j < i; ++count) {
        ++j;
        while (isTrivia(toks_[j].kind)) {
            ++j;
        }
    }
    return count + 1;
}

/// Whether a fact declaration starts at the offset `n`: the word `fact`, after the `with`
/// parameters it may take and an optional `export`, followed by the fact's name and then its
/// parameters, its `::<...>` or its type (`fact range(v: int): Interval`, `fact depth::<T>(v: T)`).
/// `fact` is not reserved: a declaration starts with it only when a name and one of those
/// follows, so a program may still call a value `fact`.
bool ParserImpl::atFactDecl(uint32_t n) const {
    uint32_t at = n;
    if (peek(at) == TK::KwWith) {
        at = afterAngledParams(at + 1);
        if (at == kNone) {
            return false;
        }
    }
    if (peek(at) == TK::KwExport) {
        ++at;
    }
    if (peek(at) != TK::Identifier || peek(at + 1) != TK::Identifier) {
        return false;
    }
    const TK after = peek(at + 2);
    if (after != TK::LParen && after != TK::Colon && after != TK::ColonColon) {
        return false;
    }
    return tree_.tokens_.text(tokenIndexAt(at)) == "fact";
}

/// factDecl : frontMatter? ('with' angledParams)? 'export'? IDENTIFIER identDef typeParams?
///            parentParams? ':' typeExpr factBody factClause*
/// The IDENTIFIER is the name `fact`, which has a meaning only here: a program may use it as an
/// ordinary name elsewhere. The head is the name of the fact, its parameters, and the type of the
/// value the fact is about; the body says how the value was built, and the trailing clauses say
/// how two facts are merged (`join`), how equivalent writings meet (`meet`), and how a loop is
/// accelerated and tightened afterwards (`widen`, `narrow`).
/// The `with` parameters are the ones the fact takes: they are written on the query
/// (`range<5>(x)`), a fact with different arguments is a different fact, and an argument must be
/// a value the compiler can compute.
void ParserImpl::parseFactDecl() {
    const size_t m = mark();
    parseFrontMatter();
    parseFactDeclRest(m);
}

void ParserImpl::parseFactDeclRest(size_t m) {
    if (eat(TK::KwWith)) {
        parseAngledParams();
    }
    eat(TK::KwExport);
    bump(); // `fact`
    parseIdentDef();
    if (cur_ == TK::ColonColon && (peek(1) == TK::Lt || peek(1) == TK::LAngle)) {
        parseTypeParams();
    }
    if (cur_ == TK::LParen) {
        parseParentParams();
    }
    if (!panic_ && expect(TK::Colon)) {
        parseType();
    }
    if (!panic_) {
        parseFactBody();
    }
    // The trailing clauses, in the order they were written: each word names one binary function,
    // and each is read at most once.
    static const std::string_view kClauses[] = {"join", "meet", "widen", "narrow"};
    bool read[4] = {false, false, false, false};
    while (!panic_ && cur_ == TK::Identifier) {
        const std::string_view word = tree_.tokens_.text(pos_);
        int which                   = -1;
        for (int i = 0; i < 4; ++i) {
            if (word == kClauses[i]) {
                which = i;
            }
        }
        if (which < 0 || read[which]) {
            break;
        }
        read[which] = true;
        parseFactClause();
    }
    close(m, NK::FactDecl);
}

/// factBody : 'match' logicalOrExpr '{' factCase* '}'
/// The subject names the value the clauses decompose: it is the fact's own parameter, which the
/// query hands over, so it is read rather than compiled.
void ParserImpl::parseFactBody() {
    const size_t m = mark();
    if (cur_ != TK::KwMatch) {
        unexpected({e(TK::KwMatch)});
        return close(m, NK::FactBody);
    }
    bump(); // `match`
    parseLogicalOr();
    if (panic_ || !expect(TK::LBrace)) {
        return close(m, NK::FactBody);
    }
    {
        Scope scope(*this, true);
        if (cur_ != TK::KwCase) {
            unexpected({e(TK::KwCase)});
        }
        while (cur_ == TK::KwCase && !panic_) {
            parseFactCase();
        }
        if (!panic_ && cur_ != TK::RBrace) {
            unexpected({e(TK::KwCase), e(TK::RBrace)});
        }
    }
    eat(TK::RBrace);
    close(m, NK::FactBody);
}

/// factCase : 'case' factPattern '=>' dataExpr
/// The last case is `case _`: what the fact says of a value none of the other clauses matched.
void ParserImpl::parseFactCase() {
    const size_t m = mark();
    bump(); // `case`
    parseFactPattern();
    if (!panic_ && expect(TK::FatArrow)) {
        parseExpr();
    }
    close(m, NK::FactCase);
}

/// factPattern : 'const' identDef (':' typeExpr)? | '_' | dataExpr
/// `const c` matches a constant of the fact's type and binds it to `c`; `_` is the case that says
/// what the fact is of a value no other case matched, and is written last; anything else is read
/// the way a rule's pattern is, so `x + y` binds what the value was built from.
void ParserImpl::parseFactPattern() {
    const size_t m = mark();
    if (cur_ == TK::KwConst) {
        bump();
        parseIdentDef();
        if (!panic_ && eat(TK::Colon)) {
            parseType();
        }
    } else if (cur_ == TK::Underscore) {
        bump();
    } else {
        parseExpr();
    }
    close(m, NK::FactPattern);
}

/// factClause : IDENTIFIER dataExpr
/// The IDENTIFIER is join, meet, widen or narrow: each clause names one binary function, and the
/// words mean something only in this position.
void ParserImpl::parseFactClause() {
    const size_t m = mark();
    bump(); // join / meet / widen / narrow
    parseExpr();
    close(m, NK::FactClause);
}

} // namespace camel::parse::syntax
