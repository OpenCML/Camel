# `std::run` 之后的默认后端：交接

`D-1128-17` 规矩的四节。枝 `lane/std-run`，树 `D:\Projects\Camel-wt\backend-speed`。

## 当前一步

`src/kernel/execute/pass.cpp:117` 的别名目标由 `std::nvm` 改成 `std::fvm`，一笔提交 `445b8a0f4aad`：

```
{"std::default", "std::nvm"}  ->  {"std::default", "std::fvm"}
```

没有改名、没有删 `std::default`、没有引入 `std::run`、没有碰 docs。计划页
`internal/plans/2026-11-27-std-run.cn.md` 的第 1–3 项（引入 `std::run`、把 fallback 默认改成它、删掉
`std::default`）**在尖端一项都没做**（`git grep -l 'std::run'` 只命中 docs/ 与 internal/，src/ stdlib/ test/
零命中），那是另一条独立的实现卡，别塞进这条。

## 读数

改后，整层快层，本尖端，只带这一行改动：

```
[SUMMARY] total: 9860, pass: 9644, fail: 155, skip: 28, known failures: 33,
          timed out: 2, killed by a signal: 1, re-run: 2, re-run passed: 0
BUILD_EXIT=0  RUN_EXIT=1
[provenance] tip 0f157f24fd93 | tree /home/zjwei/camel-work/backend-speed | tier fast
numstat: 1 1 src/kernel/execute/pass.cpp
```

与 `internal/reports/2026-11-28-promotion-26-reds.tsv`（绝对路径
`D:\Projects\Camel-wt\coord\internal\reports\…`，149 名）逐名比，得到"甲 = 7"：

```
callee_arity.commit_accepts_the_call
declaration_evaluator
split_print.nvm
foreach_lambda_family.range_map
foreach_lambda_family.range_reduce
foreach_lambda.range_form
foreach_lambda.range_filter
```

**这 7 条不作数**：那张表测在 `9c6f270c87`，本次跑在 `0f157f24fd`，中间隔着一整批合入；
**跨尖端比就是方向相反的同一种错**（与 `lane/onnx-g2` 那次的假红同型）。报错形态也印证：
`split_print.nvm` 显式写着 `std::nvm` 并报 `breaks its edge invariants`，三条 `foreach_lambda*` 是
lambda 参数类型推断，`callee_arity.commit_accepts_the_call` 是 `Camel Invariant Violation`，
另两条 stderr 为空。**至少五条与后端无关。**

## 下一步怎么接

1. **分母要自己这一轮生产，别借上一轮的。** 同尖端、同 tier（fast）的整层红表由 promo64 生产，
   产物约定在 `D:\Temp\promo64-*-fails.tsv`。拿它作分母，把上面 7 条逐名对：
   - 7 条全在 ⇒ **甲 = 0**，只报乙/丙（乙 = 分母有、本枝已不红；丙 = 两边都红）；
   - 有哪条不在 ⇒ **只对那几条**跑一次"回退一行的对照组"，不要整层跑两次。
2. **三个显式后端**：`std::nvm` / `std::fvm` / `std::jit` 各一条用例的真退出码，证明换默认没弄坏显式指定。
   这是这张卡的真正判据（"默认值换了，显式指定的三路必须仍绿"）。
3. **类 1 / 类 2 的分类**：判"fvm 与 nvm 答案不同"要**每条的 nvm 与 fvm 两边输出**；现在只有一侧，
   不可分类，别凭一侧下结论。

## 边界

- 不改语义凑绿；不动冻结目录；不推远端；一次一条 run。
- 别删/改 `docs/`、别动 `std::default` 的名字与其余出现处（`src/tools/camel-cli/config.cpp:359`、
  `src/tools/camel-cli/opencmlrc.cpp:200`、`config.h:62` 仍是 `std::default`，这是有意的）。
- `lane/onnx-g2` 那 4 笔提交停在 `2ae321eec9aa`，与本枝无关，别把两枝的读数混在一张表里。