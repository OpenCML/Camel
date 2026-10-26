#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The contextual keywords of Camel: words the parser reads as a keyword in one position and as an
// ordinary name everywhere else. A `CAMEL_KEYWORD` of include/camel/parse/syntax/token_kinds.def is
// reserved in every position; these words are not tokens at all, so their one table is kept here and
// everything that has to agree with it is generated from it or checked against it:
//
//   * the table of the language reference (2.4.1, both languages): `scripts/check-grammar.mjs
//     --write` writes the block, `--check` fails when it is out of date;
//   * the grammar productions of the parser: `scripts/check-grammar.mjs --check` allows a
//     production to quote these words where the parser decides on the text, and refuses any other
//     word that is not a token;
//   * the TextMate grammar of editors/vscode: `editors/vscode/scripts/check-grammar.mjs` checks
//     that its "contextual-keywords" rule names exactly these words, once each.
//
// Every entry says where it is a keyword (`zh`, `en`) and carries the verbatim line of
// `src/parse/syntax/parser*.cpp` that reads it as one. `--check` requires each of those lines to be
// in the parser still, so a reading that disappears takes the word off the page instead of leaving a
// sentence behind that nothing implements (design principles 6.4).
//
// Not here, and not in this form:
//   * `func`, `type` and `field` are reserved keywords (token_kinds.def) that the parser also reads
//     as tag position names after `on`, and `func` also names the shape of a function type in a
//     `match type` case (`typeCasePattern`); they are listed in the reserved table, not here;
//   * `yield` was listed as a contextual keyword by, and the author corrected
//     that: it
//     is an ordinary keyword, in the Control row with `return`. Its lexical half is not
//     on develop (no `KwYield` in token_kinds.def).

export const CONTEXT_KEYWORD_GROUPS = [
    { id: 'declaration', zh: '声明头', en: 'Declaration heads' },
    { id: 'clause', zh: '声明里的子句', en: 'Clauses of a declaration' },
    { id: 'type', zh: '类型与模式位置', en: 'Type and pattern positions' },
    { id: 'tag-position', zh: '标签位置名', en: 'Tag position names' },
]

export const CONTEXT_KEYWORDS = [
    {
        word: 'tag',
        group: 'declaration',
        zh: '标签声明：`tag` 后面跟名字，再跟 `::`、`(`、`=` 或 `on`（`tag leaf on type`、`tag finite = [...]`）',
        en: 'a tag declaration: `tag` followed by a name and then `::`, `(`, `=` or `on` (`tag leaf on type`, `tag finite = [...]`)',
        evidence: ['if (!atWord(n, "tag") || peek(n + 1) != TK::Identifier) {'],
    },
    {
        word: 'rule',
        group: 'declaration',
        zh: '规则声明：`rule` 后面跟名字，再跟 `(`、`::` 或 `<`',
        en: 'a rule declaration: `rule` followed by a name and then `(`, `::` or `<`',
        evidence: ['return tree_.tokens_.text(i) == "rule";'],
    },
    {
        word: 'fact',
        group: 'declaration',
        zh: '事实声明：`fact` 后面跟名字，再跟 `(`、`:` 或 `::`',
        en: 'a fact declaration: `fact` followed by a name and then `(`, `:` or `::`',
        evidence: ['return tree_.tokens_.text(tokenIndexAt(at)) == "fact";'],
    },
    {
        word: 'resource',
        group: 'declaration',
        zh: '资源声明：`resource` 后面跟名字（`export resource fs`）',
        en: 'a resource declaration: `resource` followed by a name (`export resource fs`)',
        evidence: ['return atWord(n, "resource") && peek(n + 1) == TK::Identifier;'],
    },
    {
        word: 'on',
        group: 'clause',
        zh: '标签声明里，位置表之前的 `on`（后面是位置名）',
        en: "the `on` of a tag declaration, before its list of positions (a position name follows)",
        evidence: ['if (!atWord(0, "on")) {'],
    },
    {
        word: 'read_by',
        group: 'clause',
        zh: '标签声明里，位置表之后的 `read_by`（后面是读取这个标签的遍）',
        en: 'the `read_by` of a tag declaration, after its list of positions (the passes that read the tag follow)',
        evidence: ['if (atWord(0, "read_by")) {'],
    },
    {
        word: 'when',
        group: 'clause',
        zh: '适用条件：函数与规则的签名之后、体之前，以及标签声明的末尾（`func f(x: T): T when is_integer::<T>()`）',
        en: "a condition: after the signature of a function or a rule and before its body, and at the end of a tag declaration (`func f(x: T): T when is_integer::<T>()`)",
        evidence: ['if (atWord(0, "when")) {'],
    },
    {
        word: 'uses',
        group: 'clause',
        zh: '资源子句：函数签名或函数类型里声明效应集合的 `uses`（`uses fs`、`uses s, t`）',
        en: "the `uses` of a resource clause: the effects a function's signature or a function type declares (`uses fs`, `uses s, t`)",
        evidence: ['if (atWord(0, "uses") || atWord(0, "pure")) {'],
    },
    {
        word: 'pure',
        group: 'clause',
        zh: '资源子句：承诺不使用任何资源的 `pure`（与 `uses` 同一位置）',
        en: 'the `pure` of a resource clause: a promise to touch no resource at all (the same position as `uses`)',
        evidence: ['if (atWord(0, "pure")) {'],
    },
    {
        word: 'join',
        group: 'clause',
        zh: '事实声明的尾子句：两条事实怎样合并（事实体之后，一行一个）',
        en: 'a trailing clause of a fact declaration: how two facts merge (after the fact body, one per line)',
        evidence: ['static const std::string_view kClauses[] = {"join", "meet", "widen", "narrow"};'],
    },
    {
        word: 'meet',
        group: 'clause',
        zh: '事实声明的尾子句：等价的写法怎样相遇',
        en: 'a trailing clause of a fact declaration: how equivalent writings meet',
        evidence: ['static const std::string_view kClauses[] = {"join", "meet", "widen", "narrow"};'],
    },
    {
        word: 'widen',
        group: 'clause',
        zh: '事实声明的尾子句：循环加速的写法',
        en: 'a trailing clause of a fact declaration: the widening of a loop',
        evidence: ['static const std::string_view kClauses[] = {"join", "meet", "widen", "narrow"};'],
    },
    {
        word: 'narrow',
        group: 'clause',
        zh: '事实声明的尾子句：加速之后再收紧的写法',
        en: 'a trailing clause of a fact declaration: the tightening that follows widening',
        evidence: ['static const std::string_view kClauses[] = {"join", "meet", "widen", "narrow"};'],
    },
    {
        word: 'operator',
        group: 'clause',
        zh: '紧跟 `func` 之后、后面是一个运算符时（`func operator +(a: V, b: V): V`）',
        en: "directly after `func` when an operator follows (`func operator +(a: V, b: V): V`)",
        evidence: ['return cur_ == TK::Identifier && tree_.tokens_.text(pos_) == "operator" &&'],
    },
    {
        word: 'struct',
        group: 'type',
        zh: '`match type` 的 `case` 模式里，命名结构体这种形状',
        en: 'in a `match type` case pattern: names the shape of a struct',
        evidence: ['if (word == "struct" || word == "tuple" || word == "array") {'],
    },
    {
        word: 'tuple',
        group: 'type',
        zh: '`match type` 的 `case` 模式里，命名元组这种形状',
        en: 'in a `match type` case pattern: names the shape of a tuple',
        evidence: ['if (word == "struct" || word == "tuple" || word == "array") {'],
    },
    {
        word: 'array',
        group: 'type',
        zh: '`match type` 的 `case` 模式里，命名数组这种形状',
        en: 'in a `match type` case pattern: names the shape of an array',
        evidence: ['if (word == "struct" || word == "tuple" || word == "array") {'],
    },
    {
        word: 'leaf',
        group: 'type',
        zh: '`match type` 的 `case` 模式里，`#` 之后的 `leaf`（`case #leaf =>`）',
        en: 'in a `match type` case pattern: the `leaf` after a `#` (`case #leaf =>`)',
        evidence: ['if (cur_ == TK::Identifier && tree_.tokens().text(pos_) == "leaf") {'],
    },
    {
        word: 'error',
        group: 'type',
        zh: '`match` 的 `case` 模式里，失败的那个 `T!` 取出来的错误：两个词的写法 `error e`',
        en: 'in a `case` pattern of a `match`: the error taken out of a failed `T!`, written as the two words `error e`',
        evidence: ['const bool isError = atWord(0, "error");'],
    },
    {
        word: 'fields',
        group: 'type',
        zh: '`static for` 与 `static loop` 的头里，`in` 之后的 `fields`（后面是一个类型，给出名字、类型、标签）',
        en: 'in the head of a `static for` or a `static loop`: the `fields` after `in` (a type follows, and it walks names, types and tags)',
        evidence: ['if (atWord(0, "fields")) {'],
    },
    {
        word: 'indices',
        group: 'type',
        zh: '同一个头里，`in` 之后的 `indices`（后面是一个值，逐个给出元素的位置）',
        en: 'in the same head: the `indices` after `in` (a value follows, and it walks the positions of its elements)',
        evidence: ['} else if (atWord(0, "indices")) {'],
    },
    {
        word: 'unordered',
        group: 'type',
        zh: '资源声明里，名字之后的 `unordered`（`resource net unordered`：这个资源上的效应不保持书写顺序）',
        en: 'in a resource declaration, after the name: `unordered` (`resource net unordered`: the effects on it keep no written order)',
        evidence: ['} else if (atWord(0, "unordered")) {'],
    },
    {
        word: 'field',
        group: 'tag-position',
        zh: '标签声明的 `on` 之后：标签贴在结构体字段上',
        en: 'after the `on` of a tag declaration: the tag holds for a struct field',
        evidence: ['"func", "type", "field", "nodes", "param"};'],
    },
    {
        word: 'nodes',
        group: 'tag-position',
        zh: '标签声明的 `on` 之后：标签贴在区域内按词法写下的节点上',
        en: 'after the `on` of a tag declaration: the tag holds for the nodes written lexically in a region',
        evidence: ['"func", "type", "field", "nodes", "param"};'],
    },
    {
        word: 'param',
        group: 'tag-position',
        zh: '标签声明的 `on` 之后：标签贴在函数参数上',
        en: 'after the `on` of a tag declaration: the tag holds for a function parameter',
        evidence: ['"func", "type", "field", "nodes", "param"};'],
    },
]

/** The entries of one group, in table order. */
export function contextKeywordsOfGroup(id) {
    return CONTEXT_KEYWORDS.filter((entry) => entry.group === id)
}

/** The block of the language reference (2.4.1) that lists the table, for one language. */
export function renderContextKeywordTable(lang) {
    const head =
        lang === 'zh' ? '| 分组 | 词 | 在哪里是关键字 |' : '| Group | Word | Where it is a keyword |'
    const rows = [head, '|---|---|---|']
    for (const group of CONTEXT_KEYWORD_GROUPS) {
        for (const entry of contextKeywordsOfGroup(group.id)) {
            rows.push(`| ${group[lang]} | \`${entry.word}\` | ${entry[lang]} |`)
        }
    }
    return rows.join('\n')
}