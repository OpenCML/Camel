# `std::opt` 会不会用上读者模块里声明的 `rule`

交接说明 5.32 第 0 条与 `docs/handover/reviews/questions-2026-10-24-evening.md` 的"负责人 2026-10-25：文档按目标写"要求面向读者的文档按已经裁定的目标语言写。"读者的 `rule` 被 `std::opt` 用上"属于哪一类，要负责人裁定：它是**已经裁定、实现未到**，还是**没有裁定**（那样第 17 章的正文要改成已经裁定并已落地的那条路）。判据同 `doc-status-marks-that-need-a-ruling.md`：未决的规则归语言作者。

## Q1：今天树上的行为

模块里声明一条定律，`std::opt` 不用它。探针 `/tmp/h547probe/probe_law.cml`：

```cml
// A law declared in the reader's own module.
module probe

rule bad(x: int) { x * 2 => x + 2 }

func twice(x: int): int {
    return x * 2
}

func main(): int sync {
    println(twice(5))
    return 0
}
```

```
$ camel /tmp/h547probe/probe_law.cml std::opt std::gir
        N02 [label=":op/mul_l", type="OPER", origin=1642, span="3:21-3:22", srcfile="/tmp/h547probe/probe_law.cml", value_type="int"];
        N05 [label=":op/add_l", type="OPER", origin=1646, span="3:30-3:31", srcfile="/tmp/h547probe/probe_law.cml", value_type="int"];
            label="twice";
            N08 [label=":op/mul_l", type="OPER", origin=1658, span="6:13-6:14", srcfile="/tmp/h547probe/probe_law.cml", value_type="int"];
$ camel /tmp/h547probe/probe_law.cml
10
```

`twice` 的结点还是 `:op/mul_l`（源里第 6 行的 `x * 2`），没被 `bad` 改写；值也还是 `10`。`std::opt` 饱和的是标准库自己写死的那一批：`stdlib/rules.cml` 335 行 `func simplifyWith(p: Program, strict: bool): Program!`、363 行 `func saturateWith(p: Program, strict: bool): Program!`——两者都没有规则集参数。

## Q2：已经裁定、并已落地的那条路

规则集作为 `with` 参数交给遍。`stdlib/rewrite.cml` 191 行：

```
/// @example let rs: Rule[] = []; p->run<rs>    // the run
#op('rewrite:run')
with <rules: Rule[], rounds: int = 16, strict: bool = false, explain: bool = false>
export func run(p: Program): Run<Program>!
```

`docs/handover/reviews/stdlib-rewrite.md` 第 5 条："规则集是配置，放尖括号……`f->seq<[algebra, fusion]>`、`f->repeat<algebra, rounds: 8>`"；第 24 行："通过：……规则集作 `with` 参数都已落地"。

## Q3：第 17 章现在的处理与需要裁定的两点

正文按"读者的 `rule` 被 `std::opt` 用上"写，`fact` 按 `docs/design/rewrite/v2.cn.md` 7.5 写；两处今天编译不过的片段登记在 `scripts/check-doc-snippets.mjs` 的待实现名单里（`PENDING_MAX` 1 → 5，`.cn.md`/`.en.md` 各两条）：

```
pending docs/tutorial/17_teaching_a_law.cn.md:227 (key 22087797dae1e989, ...) is exempted: chapter 17: a law guarded by a `fact` as its premise, used by `std::opt`, is ruled, and this tree can compile neither the `fact` nor the premise -- does not compile: error[SyntaxDiag::UnexpectedToken]: Unexpected 'fact'; expected a declaration or the end of the file.
pending docs/tutorial/17_teaching_a_law.cn.md:316 (key 954fe466d30bb486, ...) is exempted: chapter 17: `fact` replaces `analysis` and the declaration is ruled, and this tree cannot compile it yet -- does not compile: error[SyntaxDiag::UnexpectedToken]: Unexpected 'fact'; expected a declaration or the end of the file.
```

请裁定：

1. "`std::opt` 用模块里声明的 `rule`"是裁定的目标语言吗？若是，实现它需要一起裁定：用哪些模块的规则、以什么顺序、两个模块同名不同义的规则怎么办、与 `rules=` 之类显式给的规则集怎么合并。
2. 若它不是目标，第 17 章那一段要改成 Q2 那条路（规则集交给遍），本次登记的两条待实现片段随之撤销。

## Q4：顺带发现——图转储的 `text` 块永远不比

`scripts/check-doc-snippets.mjs` 的 `checkSnippet` 对任何 `isGraphDump(passes)` 的片段直接返回 `{ status: 'compiled' }`（603 行），`text` 块不参与比较。所以"只差一张目标图"的片段**不会失败，也就进不了待实现名单**：第 17 章能登记，是因为它引用的整个模块（含 `fact`）今天编译不过。要让"`std::opt` 用上读者的定律"单独成为一条可登记的目标断言，需要给优化器一个 stdout 报告通道，或让图转储的 `text` 参与比较。