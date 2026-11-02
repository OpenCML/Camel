# `transpose(f)(x, ȳ)` 这条判据已被收回：它现在教在哪些地方

**一句话**：作者 2026-11-02 11:30 裁定 **`transpose(g)` 把每个参数都当线性，返回 `(ȳ) => (每个参数一个余切)`，已知值靠捕获**。据此，**"转置后的函数先接 `f` 的参数、最后接余切"这条规格已经不成立**，而它今天仍然写在两处地方，且**这两处现在都不能改**（理由在第三节）。

## 一、清单（全树只有三处，无可争之处）

扫描：`git grep -n 'transpose(f)(' -- docs stdlib`，以及 `git grep -n 'transpose' -- docs/language docs/reference docs/design-principles.cn.md stdlib`（61 处命中，逐处归类）。

| 位置 | 它是什么 | 类 | 处置 |
| --- | --- | --- | --- |
| `stdlib/signatures/transforms.cml:74-78` | **规格**原文：「The transposed function takes the parameters of `f` first and the cotangent last -- `transpose(f)(x, 1.0)` … The type is `(..ParamsOf<F>, ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`」 | **B** | 显式作废；**待形状落地那一笔同改**（见第三节） |
| `docs/stdlib-reference.md:1452-1453` | 上一处的**生成副本**（`transforms.cml` 的文档注释生成物） | **B** | 随源文件重生成，不手改 |
| `stdlib/signatures/transforms.cml:80` | `/// @example let back = transpose(f)` 及其 fragment 说明 | **C（例子）** | 例子本身不写调用形状，**不受影响**；但 `@example` 必须能编译 ⇒ 也不能先改规格（见第三节） |

**A 类（教程/参考里的示例代码）= 0 处。** `docs/tutorial/tutorial.{cn,en}.md`、`docs/tutorial/examples/15_tensor.cml` 里的 `transpose(a)` 是**数组的 `transpose`**（`(t: float[..]) => float[..]`），与变换算子无关，一处不动。

**C 类（只是提到名字、不教形状）= 57 处**（这轮扫到的 61 处减去 B 类的 4 处），都是 `#transpose(rule)` 这个**标签**或数组 `transpose`：`stdlib/rulesets/transpose.cml`（11）、`stdlib/rulesets/vmap.cml`（10）、`stdlib/vjp.cml`（9）、`stdlib/autodiff.cml`（8）、`stdlib/prelude.cml`（4）、`stdlib/signatures/tensor.cml`（3）、`stdlib/graph.cml`（2）、`stdlib/types.cml`（2）、`docs/reference/camel-for-ai-stdlib-signatures.md`（3，生成物）、`docs/design-principles.cn.md:661`。**全部不动**（`stdlib/vjp.cml` 更是 b2 的）。

**档案（不重写）**：`docs/handover/m3-transpose-branch-questions.cn.md:17`、`docs/handover/reviews/questions-2026-11-01-whether-transpose-takes-the-cotangent-beside-the-parameters.md` 等 —— 那是"当时为什么这么定"的记录，改了就和自身对不上。

**一页相关但还没人裁归属的**：`docs/handover/questions/2026-11-02-transpose-tangent-parameters.cn.md` 问的正是这件事（「`transpose(lin)` 该收一个还是收一串」），11:30 的裁定已经回答它。**这页是 `lane-resid` 的，我不动它，也不擅自挪进 `reviews/`**：挪页和落裁定是同一笔，应由它的作者做。

## 二、为什么"作废"这件事必须写成页而不是改那两行

三条理由，任一条都够：

1. **改了会红。** `transforms.cml` 的 `@example`（`:80`）**必须能编译**，`modules.stdlib_registry` 会编译每个 `@example`（`registry.docs_complete`）。今天编译器还只接受 `(..ParamsOf<F>, ct)`，把规格文字先改成捕获式，等于**文档说一套、例子编一套**，而且改动会被 `registry.docs_complete` 直接报出来。
2. **那不是我的文件。** 规格文字在 `stdlib/signatures/transforms.cml`，`stdlib/**` 归 b2；`docs/stdlib-reference.md` 是它的生成物 —— 手改生成物是本仓库明令禁止的事（`catalog.doc_is_generated` 会红）。
3. **一次改完才不留下自相矛盾的中间态。** 形状落地那一笔要同时动：`transforms.cml:74-78` 的规格、`:80` 的 `@example`、`docs/stdlib-reference.md`（重生成）、`test/plans/feat/transforms/split.plan.toml:155` 的 `expect_output_contains`、以及 `test/cases` 里那 23 个文件 / 50 处（见
   `docs/handover/reports/2026-11-02-what-each-transpose-call-becomes.cn.md`）。

## 三、留给落地那一笔的编辑清单（照抄即可）

```
stdlib/signatures/transforms.cml:74-78   把「参数在前、余切在后」换成「每个参数都当线性，余切是唯一实参；
                                         已知值靠捕获 transpose((d) => f_lin(x, d))(ȳ)」
stdlib/signatures/transforms.cml:80      @example 改成捕获式的片段（且必须能编译）
docs/stdlib-reference.md                 node scripts/diagnostics-catalog.mjs --write 之外，走 stdlib 参考生成脚本重生成
test/plans/feat/transforms/split.plan.toml:155   expect_output_contains 里的 type=(float, float, float) => (float, float) 改成新口数
test/cases/**                            23 个文件 / 50 处，逐条照表改（值/余切按被转置函数的形参个数切）
```

**判据**：落地那一笔跑完，`docs/stdlib-reference.md` 里不再出现 `transpose(f)(x, 1.0)` 与 `(..ParamsOf<F>, ct:` 这两串，且 `node scripts/check-doc-links.mjs` 与 `feat/compile/diag_catalog` 同时绿。