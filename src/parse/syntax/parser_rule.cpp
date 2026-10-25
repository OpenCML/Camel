// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The productions of rule declarations (rewrite design 2): a rule is a pattern, what it becomes
 * and, optionally, a condition. `rule` and `when` are names that mean something only here
 * (`rule` at the start of a declaration, `when` between the parameters and the body), so a
 * program may still have a variable called `rule`.
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

/// ruleDecl : frontMatter? 'export'? IDENTIFIER identDef typeParams? ruleContext? parentParams
///            whenClause? '{' ruleBody '}'
/// The first IDENTIFIER is the name `rule`; the condition, when there is one, is the `whenClause`
/// between the parameters and the body. The parameters are the pattern variables; `const x: T`
/// only matches a constant. The tags before it are the rule's requirements (`#requires<reassociate>`,
/// design principles 7.2); the AST reads them from the tree.
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
    // A `when` after the closing brace is not read as a condition any more. It is not read as a
    // condition any more, and it is not reported as a token that did not fit: the program is
    // told where the condition belongs.
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

/// Whether the current token starts an analysis declaration: the name `analysis` followed by
/// the name of the analysis, then its parameters or `:` (`analysis range: Interval`,
/// `analysis pair(a, b): bool`). `n` is the offset of the `analysis` token (1 after `export`).
bool ParserImpl::atAnalysisDecl(uint32_t n) const {
    if (peek(n) != TK::Identifier || peek(n + 1) != TK::Identifier) {
        return false;
    }
    const TK after = peek(n + 2);
    if (after != TK::LParen && after != TK::Colon) {
        return false;
    }
    uint32_t i = pos_;
    for (uint32_t k = 0; k < n; ++k) {
        ++i;
        while (isTrivia(toks_[i].kind)) {
            ++i;
        }
    }
    return tree_.tokens_.text(i) == "analysis";
}

/// analysisDecl : frontMatter? 'export'? IDENTIFIER identDef parentParams? ':' typeExpr
///                '{' analysisClause* '}'
/// The IDENTIFIER is the name `analysis`, which has a meaning only here: a program may use it
/// as an ordinary name elsewhere. The parameters are pattern variables: a query within a rule's
/// condition hands it the parts of the matched value (`analysis pair(a: Node, b: Node)` is read
/// as `pair(x, y)`); a declaration without them is queried with the value itself.
void ParserImpl::parseAnalysisDecl() {
    const size_t m = mark();
    parseFrontMatter();
    parseAnalysisDeclRest(m);
}

void ParserImpl::parseAnalysisDeclRest(size_t m) {
    eat(TK::KwExport);
    bump(); // `analysis`
    parseIdentDef();
    if (cur_ == TK::LParen) {
        parseParentParams();
    }
    if (!panic_ && expect(TK::Colon)) {
        parseType();
    }
    if (!panic_ && expect(TK::LBrace)) {
        while (cur_ != TK::RBrace && cur_ != TK::Eof) {
            parseAnalysisClause();
            eat(TK::Semi);
        }
        expect(TK::RBrace);
    }
    close(m, NK::AnalysisDecl);
}

/// analysisClause : IDENTIFIER '=>' dataExpr
///                | IDENTIFIER '(' identList? ')' '=>' dataExpr
///                | IDENTIFIER '(' ('const' identDef ':' typeExpr | dataExpr) ')' '=>' dataExpr
/// The first IDENTIFIER is initial, join or widen; on starts the third form. The words
/// are names that mean something only in a clause, so a program may still use them elsewhere.
void ParserImpl::parseAnalysisClause() {
    const size_t m = mark();
    const std::string_view keyword = tree_.tokens_.text(pos_);
    bump();
    if (keyword == "join" || keyword == "widen") {
        if (!panic_ && expect(TK::LParen)) {
            if (cur_ != TK::RParen && cur_ != TK::Eof) {
                parseIdentList();
            }
            expect(TK::RParen);
        }
    } else if (keyword == "on") {
        if (!panic_ && expect(TK::LParen)) {
            if (cur_ == TK::KwConst) {
                bump();
                parseIdentDef();
                if (!panic_ && expect(TK::Colon)) {
                    parseType();
                }
            } else if (cur_ != TK::RParen && cur_ != TK::Eof) {
                parseExpr();
            }
            expect(TK::RParen);
        }
    }
    if (!panic_ && expect(TK::FatArrow)) {
        parseExpr();
    }
    close(m, NK::AnalysisClause);
}

} // namespace camel::parse::syntax
