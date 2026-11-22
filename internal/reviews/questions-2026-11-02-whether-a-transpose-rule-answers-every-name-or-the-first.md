# 一条转置规则，是"只答它左侧第一个名字的余切"，还是"答左侧每个名字的余切"？

**状态：树已经按 B 落地并被实测逼出来的，等作者确认这个读法。** 车道 `c2181c95f`（原 `lane-m4-assembly`，该分支已删；M4 第三步：`transpose.cpp` 的五条 `law == "…"` 换成展开规则右侧）先按 A 试过：一条规则只答第一个名字，第二位靠"把两个操作数的值对调"问同一条规则。A 在 `x + c`、`x * c` 上对，在 `x / c` 上**直接炸**——`grad((x) => x / 4.0)` 报 `RuntimeDiag::DivisionByZero`（`stdlib/autodiff.cml#div_cotangent`，`return ct / c`），因为对调把"被除数"塞进了除数槽，而那里的被除数是 `x * dw`，`dw` 是常量的零切向。所以这一问今天是"B 已经落地，请确认对不对"，不再是"在 A 和 B 之间选"。

## 两条事实

- `stdlib/autodiff.cml` 原来只给第一个名字的函数（`mul_cotangent(c, ct)`、`sub_cotangent(ct)`、`neg_cotangent(ct)`…）；第二位的那一半（`add_cotangent_c`、`sub_cotangent_c`、`mul_cotangent_c`、`div_cotangent_c`）是后来加的，**在 A 之下没有调用者**——`git grep` 只中声明与文档。B 之下它们就是第二位答案，各有调用者。
- `src/parse/ast/rule_head.cpp#indexRule` 的键是 `head@arity`，同一个键上的第二条规则**具名拒绝**（`SemanticDiag::DuplicateRuleForOperator`，用例 `rulesets_duplicate_head_refused`，plan `rulesets` 63/63）。所以 A 之下"位次"无处安放；B 之下不需要它——位次由右侧元组的顺序承担，一个键仍然一条规则。

## B 今天的样子（已落地）

- 规则集（`stdlib/rulesets/transpose.cml`）：`x / c => (ct: T) => (div_cotangent(c, ct), div_cotangent_c(x, c, ct))`，`x - c`、`x * c`、`x + c` 同形；`-x` 一元，仍然只答一个值。
- 展开器（`modules/transforms/rule_expand.cpp#expandRuleRightSide`）：`position` 选元组的第 `i` 个分量，并且**只拷这个分量用得到的节点**——所以一个位次只问它自己那份答案读的值，另一个名字的值没有也不问。
- `transpose`（`modules/transforms/transpose.cpp#ruleCotangent`）：操作数按左侧顺序交给规则，不做值对调；某位次缺它要读的值就具名拒绝 `TRANSPOSE … skip=cotangent-needs-a-value uri=… position=… reason=…`。

实测（`nvm`，plan `feat.compile.transforms`，用例 `grad_of_a_division`，52/52）：

| 函数 | 点与余切 | 答 | 定律 |
|---|---|---|---|
| `grad((x) => x * 3.0)` | 1.0 | `3.0` | 同 |
| `grad((x) => x - 1.0)` | 1.0 | `1.0` | 同 |
| `grad((x) => -x)` | 1.0 | `-1.0` | 同（修好的旧错：曾答 `1.0`） |
| `grad((x) => x / 4.0)` | 8.0，ct=1.0 | `0.25` | 同（A 之下是崩溃/除零） |
| `grad((x) => 4.0 / x)` | 2.0，ct=1.0 | `-1.0` | 同（`-(1*4/(2*2))`） |

`x / 4.0` 这一条之所以绕不开：`grad` 是 `transpose(split(jvp(f)))`，路上要转置 `autodiff::div_tangent`，它的正文 `dx / w - x * dw / (w * w)` 里除数 `w * w` 是**算出来的值**，所以 `div_t` 的第二位在这条路上**可达**——A 之下它答不出正确的数，只会答错的数或除零。

## 要作者答的一句

**"一条规则答它左侧每个名字的余切，按左侧写的顺序返回一个元组"是不是对的读法？** 若是，这一笔已经这样落地，问题是历史；若不是（一条规则只能答第一个名字），那么 `x - c`、`x / c` 的第二位就没有地方写，请说一句"同一个键上按位次再放一条规则"算不算 19:50 说的"同键同类型条件"——今天它会被 `DuplicateRuleForOperator` 拒掉。