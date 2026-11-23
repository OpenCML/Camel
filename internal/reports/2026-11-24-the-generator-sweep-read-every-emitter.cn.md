# 生成器全查：13 个吐出源码的入口，0 个能自证

日期：2026-11-24　读数树：`lane/generator-sweep`（基线 `develop = 948e7af31b`）
条目：`D-1123-70`（原文：全查所有吐出 Camel 源码的地方，找五种旧写法，判据是每个生成器出一段样例交给解析器编回去零失败）

## 一、清单与总数

**13 个**吐出源码（或面向读者/测试的 Camel 文本）的入口。一行一个，后面是它写什么。

| # | 入口 | 写什么 |
|---|---|---|
| 1 | `scripts/stdlib-reference.mjs` | `docs/stdlib-reference.md`（签名行 + `@example` 的 Camel 样例） |
| 2 | `scripts/camel-for-ai.mjs` | `docs/reference/camel-for-ai{,-diagnostics,-stdlib-signatures}.md` |
| 3 | `scripts/diagnostics-catalog.mjs` | `semantic.cpp`、`base.h`、`docs/diagnostics.md`（含每条诊断的 Fix/Help 建议） |
| 4 | `scripts/check-grammar.mjs` | `docs/language/{02,08}` 的文法块与上下文关键字表（中英两份） |
| 5 | `scripts/gen-case-tables.mjs` | `src/stdlib/str/case_tables.cpp` |
| 6 | `scripts/gen_export_matrix.py` | `export_matrix.csv` / `export_matrix.md` |
| 7 | `cmake/generate_libcamel_exports.mjs` | 配置期的 libcamel `.def`（构建产物，不入树） |
| 8 | `benchmarks/resnetbn/tools/gen_model_cml.py` | `benchmarks/resnetbn/model.cml` |
| 9 | `benchmarks/compile/gen_modules.py` | 基准运行时的 `OUT_DIR/main.cml` 与 `bm_NNNN.cml` |
| 10 | `test/tools/progen/generate.py`（+`model.py`） | 测试运行时按 seed 生成的程序 |
| 11 | `test/cases/modules/std/array/sort_reference.py` | 测试运行时拼的两个程序（`small.cml`、`large.cml`） |
| 12 | `src/stdlib/std/cml/cml.cpp`（`std::cml`） | 把值/程序打印成 Camel 文本 |
| 13 | `src/tools/format`（`camel-format`） | 重排后的整份程序 |

**不算在内**：`gir`/`rgir`/`jgir`（`src/stdlib/std/gir/dot/graphviz.cpp` 与 JSON 编码）打印的是图的 DOT/JSON——算子名、端口、类型，没有程序文本，五种写法无处出现。`test/tools/spelling/check.py` 是检查器本身，不是生成器（它的口径见第五节）。

## 二、三个数

- **通：0**
- **拒：6**
- **没有只读模式：7**

0 + 6 + 7 = 13。**没有一个入口在这棵树上给出绿色读数**：六个能不带编译器自检的，全部是红的；七个根本没有自检。

## 三、拒的六条，逐条带证据

**1. `scripts/stdlib-reference.mjs` — 拒（`--verify-doc`，exit 1；`--write` 同样停在这一句）**

```
81 declaration(s) in the signature files carry a body; a declaration has no body
```

这一段在模式分派**之前**，所以六种模式（含 `--write`）全部 exit 1：**这个生成器今天在这棵树上一次也没跑成**。

**2. `scripts/camel-for-ai.mjs` — 拒（`--check`，exit 1）**

```
docs/reference/camel-for-ai-diagnostics.md: the 'diagnostics' region is not what its source produces (327 entries)
docs/reference/camel-for-ai-stdlib-signatures.md: the 'stdlib-signatures' region is not what its source produces (571 entries)
```

这是 #1 的下游：`scripts/check-generated.mjs` 的头注释自己写着两个生成器「不是独立的」，签名目录从参考页读。**一个原因、两个后果**，不是两条发现。

**3. `scripts/diagnostics-catalog.mjs` — 拒（`--check`，exit 1）**

```
src/kernel/core/error/diagnostics/messages/semantic.cpp is not what src/kernel/diagnostics/registry.tsv produces
```

`internal/reports/generated-files-and-the-one-that-writes-back.md` 描述的那次事故的形状：`registry.tsv` 那一行被改过、`semantic.cpp` 没重新生成。差异 4 行，落在 `ForRequiresIterable` 的 message 与 help 上——**编译器今天印的句子已经不是 registry 里那一句**。

**4. `scripts/check-grammar.mjs` — 拒（`--check`，exit 1）**

```
error: contextual keyword 'operator' (scripts/context-keywords.mjs) cites a reading the parser no longer has:
return cur_ == TK::Identifier && tree_.tokens_.text(pos_) == "operator" &&
```

上下文关键字表把一行已经不在解析器里的读法当成证据；表与解析器对不上。

**5. `scripts/gen_export_matrix.py` — 拒（`--check`，exit 1，崩在输入路径上）**

```
FileNotFoundError: ... modules/onnx/lowering.cpp
```

布局搬到 `src/stdlib/onnx/lowering.cpp` 之后，这个生成器还按旧根读文件。**它不是「目标文件陈旧」，是它读不到输入**——所以它连一次比较都没做成。

**6. `benchmarks/resnetbn/tools/gen_model_cml.py` — 拒（`--check`，exit 1）**

树里那份 `model.cml`（457 行）与生成器现在产出的（266 行）差 **407 行**（299 只在树里、108 只在生成侧），不是格式差异：两边头部的 `import` 行就不同。目标文件陈旧。

## 四、没有只读模式的七个，各自生成什么

| # | 入口 | 它生成什么 | 能不能被检查 |
|---|---|---|---|
| 5 | `gen-case-tables.mjs` | `case_tables.cpp` 的 Unicode 表（数据，非 Camel 源码） | 只有 `--version/--ucd`，没有比较；产物是一张纯数据表，编进库里才被读到 |
| 7 | `generate_libcamel_exports.mjs` | 配置期 `.def` | 不检查，也不该检查：产物是构建期的，不入树 |
| 9 | `gen_modules.py` | 基准的多模块工程 | 只能写；读数靠基准自身跑起来 |
| 10 | `progen/generate.py` | 测试程序 | 只能写；读数靠 `feat/opt/progen` 计划与 `test/tools/spelling/check.py` 的采样（都要构建） |
| 11 | `sort_reference.py` | 两个程序 | 只能写；读数靠 `array.sort_reference`（`feat/modules/std`，fast 层） |
| 12 | `std::cml` 打印器 | 程序文本 | 没有自检；靠 `feat.trans.cml::print_and_recompile` 与 `spelling/check.py` 的采样（都要构建） |
| 13 | `camel-format` | 重排后的程序 | 无 `--check`；不带 `-i` 时打到 stdout，`camel-format f \| diff - f` 就是 dry-run，但必须构建；正例在 `feat/format` 的 golden（`golden/compose_and_return_type.cml` 正是 `<>` 与 `=>` 那两条） |

**这一栏的结论**：七个里五个的读数只在构建之后才存在。**在 CI 的静态层里，「无法被检查」与「检查通过」是同一件事**——本轮五条真发现全部落在这里。

## 五、判据的两个缺口（这才是这一条的真发现）

上一轮已经有一笔按这条做的：`6cced63230`「write the five new spellings where Camel source is generated」，58 个文件、约 100 处，并留下检查器 `test/tools/spelling/check.py` 与计划 `feat.trans.source_spelling`。它**是抽查，不是全查**，两处口径把本次的发现漏掉了：

**缺口一：文件表是列举的，`.cml` 一概不读。**

`check.py` 的 `LINT_FILES` 是十个根（`registry.tsv`、`cml.cpp`、`stdlib-reference.mjs`、`progen`、`scale`、`perfbaseline`、`test/cases/{trans,measured,tools}`、`benchmarks`），并且 `path.suffix not in (".py",".mjs",".cpp",".tsv",".h")` 直接跳过——**任何 `.cml` 文件都读不到**，`test/cases/modules/**` 也不在表里。

于是：

- **41 条 `@example` 仍写死写法**（30 条 `(x: int): int => …` 冒号返回类型 + 11 条 `(x) => e` 无类型 lambda），分布在 `src/stdlib/` 9 个声明文件：`graph.cml` 12、`opt.cml` 4、`tiles/signatures/tiles.cml` 5、`rules.cml` 3、`re.cml` 2、`tree.cml` 2、`para.cml` 1、`test_support.cml` 1，以及 `array.cml` 8、`prelude.cml` 3。它们被 `stdlib-reference.mjs` 逐字搬进 `docs/stdlib-reference.md`（样例行例如 L3557 `find_index<(x) => x > 4>`、L5938 `graph_of((x: int): int => x + 1)`）。
   `--verify-docs` 那一模式的判据就是「每条 `@example` 都编得回去」，它记录的非编译样例表 `EXAMPLE_STOPS` 只有 `a semantic error` 一类、35 条，**没有语法一类**——所以这 41 条若真编回去，按那张表的算法就是 41 条新失败。但**今天连这一步都到不了**：`signatureBodyProblems()` 在模式分派之前就 `exit 1`（`scripts/stdlib-reference.mjs` 的 `bodyProblems` 一段在 `mode === '--write' || mode === '--verify-doc'` 之前），于是这个脚本**六种模式全部**在 81 条带体声明上停下——`--write` 也出不来。这既解释了「今天新加的三个导出不在参考页里」，也解释了这 41 条为什么没人发现：**测量它们的那一步被更前面的一道门挡着，而那道门不在任何闸里**（`scripts/gates.mjs` 十道闸里没有它，`scripts/release-gate.mjs` 跑的是 `--verify-ops` 与 `--verify-doc`，没有 `--verify-docs`）。
- **`sort_reference.py` 的两个程序**（见第六节）：它落在 `test/cases/modules/std/`，不在表里。
- `scripts/check-doc-snippets.mjs` **把这两张页整页豁免**：`docs/stdlib-reference.md` 的理由写的是「generated: signature lines, one per overload, not programs」，`docs/diagnostics.md` 是「each example is a program that raises the diagnostic on purpose」。两张页因此同时躲开了两条检查路径。

**缺口二：「三竖括号 lambda」那条规则只认带冒号的那种。**

```python
("paren lambda", re.compile(r"(?<!func )\([^()]*\)\s*:\s*[\w(\[]+\s*=>"))
```

它要求 `)` 之后先有 `:`。所以 `(x) => c` 这种**无类型**的旧写法它一条都不认——而五种写法里的第 4 条恰恰是「lambda 以 `func` 开头」，`(x) => c` 与 `(x: int): int => …` 是同一条规则的两个面。

后果直接可读：`registry.tsv` **在** `LINT_FILES` 里，`check.py` 跑过它并且是绿的，而里面今天仍有两条旧写法——

```
L124 (ComprehensionSourceRefused): … is 'xs->filter<(x) => c>->map<(x) => e>'
L421 (CompOperandNotUnary):        The composition `f <> g` is the lambda `(x) => g(f(x))`
```

这两句是**编译器会印出来的修复建议**（`semantic.cpp` 与 `docs/diagnostics.md` 都是从这一列生成的）。读者按建议写，就写下一个解析器拒的 lambda。同一份 `test/plans/feat/compile/stream.plan.toml` 的 desc 早已改成 `func (x) { g(f(x)) }`——计划里的新写法与 registry 里的旧写法在同一个仓库里并存。

把 `check.py` 的三条规则原样跑在本树的文件表上，读数 3 条命中（两条是 `test/cases/trans/spelling/greedy.cml` 的 `/// @example` 注释，一条是 `named_type.cml` 的注释）；把同一组规则跑在 `src/stdlib/` 的 776 个文件上，**43 条命中**。

## 六、这一轮改了什么

**一处，2 行：`test/cases/modules/std/array/sort_reference.py`。** 它是上一轮那笔 6cced63230 自己改坏的地方，而它写出来的文本没有任何检查读过。

改前（`n = 200000` 代入后，实测由 `run()` 打桩取得，未编译）：

```camel
    let down = up->map<func (i: int) => int 200000 - 1 - i>
    let d = sort(up->map<(i: int): int => (i * 7919) % 200000>)
```

两处都编不过：第一行是 **`func` 头没有块体**（原 f-string 里 `{n}` 是替换，`{{`/`}}` 才是字面花括号，转换时把 `{n}` 当成了块体的花括号，于是花括号整个消失了）；第二行是**冒号返回类型**，正是五种写法里的第 1 种、上一轮没碰的那一行。

改后（同一打桩读数）：

```camel
    let down = up->map<func (i: int) => int { 200000 - 1 - i }>
    let d = sort(up->map<func (i: int) => int { (i * 7919) % 200000 }>)
```

两个程序合计 918 行输出，五种写法命中 **2 → 0**。读数本身（`array.sort_reference` 是否转绿）要构建，**本车道不跑构建**，留给点名合时的那一次。

**没有改的，以及为什么：**

- `stdlib-reference` 的 41 条 `@example`：那条车道在查同一生成器的 81 条带体声明，且 `--write` 今天出不来——两件事撞在同一个生成器的同一个模式上，一起改会让读数分不开。**报，不改。**
- `registry.tsv` 的两条旧写法：改它要跑 `--write`，而 `--check` 今天已经因为 `ForRequiresIterable` 红着，一笔里会混进别人那 4 行。**报，不改。**
- `model.cml` 的 407 行、`gen_export_matrix.py` 的旧根、`check-grammar` 的关键字行：都不是「旧写法」，是各自的东西陈旧或错位。**报，不改。**
- `test/tools/spelling/check.py` 的两处口径（`.cml` 不读、无类型 lambda 不认）：这是判据，按规矩**报给作者，不自行放宽或收紧**。

## 七、给下一个人的一句

**「每个生成器都能跑通」这句话，在这棵树上的正确读数是 0/13。** 不是坏消息里的坏消息：六个能自检的都在报红，说明自检这一层还在工作；真正的洞是那七个没有自检的——它们的读数只在构建之后存在，而构建之后的读数今天没有一道闸在替它们看。