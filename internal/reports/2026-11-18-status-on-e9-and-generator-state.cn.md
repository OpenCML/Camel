# 进展报告：E9 六条错值、生成器状态下移（应 nudge `e843fd6f4`）

`2026-11-18`｜基线 `origin/develop = ccdce9412`｜`fsh = e843fd6f4`

作者在两轮无提交后要求报告这两件事的现状。以下每条都带能复跑的读数。

## 1. E9：六条错值全部转对，全部有守卫

`e9bisect`（`1d42b5bbf`）按 seed 把六条错值逐条二分到「哪一个 pass 让哪一个效果掉了」：

| seed | 梯子前缀 | 症状 |
|---|---|---|
| 131 | `devirtualize inline fold` | 顺序错（`L5` 在前、空行在后） |
| 141 | `inline fold` | 0 行 → 1 空行 |
| 417 | `inline fold` | 只有 `L4 0 0` |
| 419 | `inline cse` | 0 行 → 1 空行 |
| 484 | `inline fold` | 0 行 → 1 空行 |
| 311 | `macro + inline` **合体** | 1 行 → 2 行 |

修复是**两行判据**，都在同一个函数 `closeChainOver`（`src/runtime/draft.cpp`）：

- `survives(pred, user)`：把 `alive(pred) && nodeRegion(pred)==nodeRegion(user)`（raw region）换成 `sameSyncBlock(*this, pred, user)`。`+13/−1`（`2779ad600`）。
- `keepWaiting`：把「后继没有任何别的等待」换成「**后继等过本节点（它在这里的等待正要蒸发）或什么都不等**」。`38/13`（`62e08f9b4`）。

**两处都是同一个形状：一个本来为了防某件事的修复，用了一个**比它要问的更紧**的近似。**
`keepWaiting` 那个近似只在后继有 0 或 1 个等待时等价 —— 那句话同时解释了「它为什么曾经有用」和「它为什么在 311 上坏掉」。

守卫共 **56 个测试**（40 + 16），全部用「把修复换回去」做阳性对照：

- `casedrop`（`4e0f06499`）：`rc24…rc28` 五条，每条 2 格（无 pass 参照 + 最紧红的梯子前缀）× 4 后端 = 40。
- `pin311`（`a8757dd25`）：`rc29_seed311` 四格（参照 / **合体** / `std::macro` 单独 / `std::opt::inline` 单独）× 4 后端 = 16。

**311 的守卫只许钉合体**：`std::opt` 整梯改前改后都是 2 行，钉在整梯上会让守卫坐在它一直绿的地方。作者已把这一条写进合并说明（`934dfca11`：`pinned on the pair of passes that lost it`）。

`pin311` 的阳性对照逐格：换回 `f77e57f9d^1` 的 `draft.cpp` 后，**合体格 4/4 红、另三格 4/4 绿、同段 `rc24–rc28` 十格全绿** ⇒ 换回只退掉 311 这一修，那一红只能归给合体。合并车道在宿主上独立重做了一遍，结论相同。

当前读数：`feat/opt` **1594 / 1594 / 0 / 0**（`e9` **268 / 268**）｜`feat/linear` **869 / 864 / 0 / 3 / 2**｜`feat/compile` **2627 / 2610 / 0 / 17**｜`feat/parse` 121｜`feat/para` 104｜`feat/tools` 70｜`[SUMMARY] total: 5385, pass: 5363, fail: 0, skip: 2, known failures: 20`。

## 2. 生成器状态下移：第一步的接缝被量翻了，卡在一个决定上

`girstep` 零行代码，量出规格书 `2026-11-18-lowering-generator-state-to-gir.cn.md` 第 9.2 节「缺一个参数位 setter」不对，缺口在三处（已写成该页第 10 节，`8460c58b8`）：

1. **端口**建图时按函数型定死（`builder_helpers.h:417-444`），事后 `setFuncType` 不改端口，而每个 `:accs` 从端口取型；draft 没有改节点/端口型的 API。
2. **pack**（`builder.cpp:6289` → `castNodeTo`）在建图时就要具体类型 —— 就是 9.1 走法 1 那行 `DynamicCastForbidden`。
3. **call 点比签名更早**（`builder_operators.cpp:2284`）：每个 `yield` 都 call `__gen_step__(state)`，签名留占位会在第一处 call 死。

**⇒ 所以「建完再改写」要重写的是 body 的建，不是签名的字。** 作者已采纳这个措辞（`d8cf4cb5d`：`build-then-rewrite rebuilds the body`）。

**卡点（这就是要你点头的那一条）**：`test/plans/feat/opt/.../stream.plan.toml:1205-1211` 的 `verify_script` 断言 `labels` 图里**恰有 1 个**名为 `'s'`、型 `var string` 的节点。今天那个节点是**外层格子**；裁定落地后名为 `'s'` 的只剩 step 的局部，而它在 `__gen_step__` 图里 ⇒ **这条断言会从 1 变 0，而它钉住的正是裁定要拆掉的东西。**

**⇒ 17 个文件里第一个必须变的不是快照，是这条断言。而改它要你点头。**

再往前一步（「探测遍」的副作用回收：诊断去重 / `idIndex_` / module 注册）已在第 10.2 节各给了一个可跑的读数，但**没有动代码** —— 因为在「重建 body」这条路你认不认之前，写它就是把一个前提未定的事做大。

## 3. 本轮为什么没有继续派工

`docs/roadmap.cn.md` 仍是 **0 条 `- [ ]`**；E9 已收口；作者裁定全部已答或已入树；唯一的下一件事就是上面那条断言。所以这里**不是没人发现该做什么，是那件事的前置还没定** —— 而「用猜的默认开工」今晚已经付过一次代价（`girstep` 第一步根本不存在）。

## 4. 一处我自己的错，留档

`0c6b5a5aa` 里我修两条死引用时用了 `Set-Content -NoNewline`，把 `src/execute/exec_plan.cpp` 的行尾归一了，**34 行行尾噪音进了 `develop`**。`develop` 不重写，所以它留着了。`plan.cn.md` 那半是干净的。下次改单行文本用 `edit`。

顺带修掉的两条既有红（都在 `check-doc-links --citations`，与任何改动无关，但让每次全量 `-Check` 带一个常数失败）：`EffectNotExecuted` 在 `src/`、`include/` 里全树零命中却仍被 `plan.cn.md:75` 引用 ⇒ 该步作废；`exec_plan.cpp:1259` 引的文档在 `questions/` 不在 `reviews/`。