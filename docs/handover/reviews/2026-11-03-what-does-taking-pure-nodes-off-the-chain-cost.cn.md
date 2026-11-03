# 纯节点离开链位要付 59 条红和一条封存拒绝：P0-2 第二步还做吗？

**问：作者裁定的 P0-2 第二步——「照甲把纯节点从链上拿掉」——量的结果是①为那 13 条买的正好是零，②带出 57 条新红，其中一条是封存检查直接拒绝造出来的 `GATE`。那么第二步是停在这里（规则留、链位不动），还是先补 yield 次序的另一处承载再谈拿掉链位？**

## 1. 作者原估：13 条，且只在 `feat/linear` 上量过

裁定原文（2026-11-03 10:10，`e44468eaf`）：

> 13 条错数（`use 10 → 30`、`branch 10801 → 80801`）说明，缺的是**读和下一次写之间的顺序** …… 以前这个顺序是纯节点占着链位时顺带给的。

估这一步代价的那份读数（`docs/handover/questions/2026-11-03-is-a-var-read-outside-the-pure-value-rule.cn.md` §2）只跑了 `feat/linear`：`873 / pass 871 / fail 0` → `873 / pass 858 / fail 13`。

## 2. 实测全闸门：红 2 → 59

树 `order`（`origin/develop` = `e3453a60a`），`camel-remote.ps1 -Tier fast -Jobs 4`，`feat/linear,feat/compile,feat/opt` 三族 + `feat.compile/transforms.plan.toml` + `feat.transforms/split.plan.toml`：

| 族 | 改前（`e3453a60a` 与 `10c71dc58` 逐名相同） | 第二步（`7ca7fedfc`） |
| --- | --- | --- |
| `feat.compile` | 2187 = 2178 过 / **2 红** / 7 known | 2187 = 2143 过 / **37 红** / 7 known |
| `feat.opt` | 1174 = 1174 过 / **0 红** | 1174 = 1152 过 / **22 红** |
| `feat.linear` | 875 = 871 过 / **0 红** / 2 known / 2 skip | 875 = 871 过 / **0 红** / 2 known / 2 skip |
| `transforms` | 80 = **77 过 / 2 红 / 1 known** | 80 = **76 过 / 3 红 / 1 known** |
| `split` | 19 = **19 过** | 19 = **19 过** |
| 合计 | 4255 = 4242 过 / **2 红** | 4255 = 4185 过 / **59 红** |

**那 13 条判据在第二步下是 259/259 全过**（`branch 10801 80802 8`、`use: 10 counter 3`），但它们在**不做第二步**时也是全过。所以这一步为它们买的正好是零，付出的是另外 57 条读数。

## 3. 一条封存拒绝：造出来的 `GATE` 不合法

`feat.compile.transforms` 的新红 `jvp_effect.runs_as_written_and_zero_tangent`，报文首行：

```
():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph
'jvp_of_g' has GATE node 8 without a wait: no sync predecessor and no control
input. The pass gave up without a diagnostic of its own: it built or was given
a gra...
```

`edges.cn.md` §4.6 与「`GATE` 没有 wait 就是内部错误」这条封存检查一起说明：把纯调用语句的链位换成读它的 `GATE`，在块的第一条语句、以及链从别处接进来的形状上，造出的是**一个不合法的图**。这与「代价大」不是一回事——代价大可以谈，不合法没有谈的余地。

## 4. 34 条生成器红灯：yield 次序今天靠那个链位

`feat.compile.stream` 从 **0 → 34 全红**：`generator_sync_order*`、`generator_early_stop*`、`generator_loop*`、`generator_cell_across_pulls*`、`generator_body_fault_ends_stream*`、`generator_error_element*`、`generators_loop_computed_initialiser`、`generators_match_binding_across_yield`、`generators_error_binding_across_yield`、`generator_local_across_yield`、`generator_destructured_local`、`stream.interval_loop_cml`。

读法：**生成器的 yield 次序就靠那个链位**。也就是说，「纯节点占链位」不是历史遗留的浪费，它今天**承担着 yield 次序的语义**。这一句要留在这里，否则下一个人还会以为拿掉它只是省事。

`feat.opt` 的 22 条同类：`camel_passes.{cse,cse_written_variable,fold,fold.constant_budget}.{by_cpp,by_camel}` 图快照、`camel_passes.fold_fault.{subject,property}`（`the division no longer orders the print`：可能出故障的语句的先后也是链位给的）、`algebra_rules.{divergence,library_only}`、`keep_shapes.real_pipeline.*`、`pass_written.inventory`、`inline`、`gate_value`、`effects_kept`。

## 5. 第一步是真实的缺边，留着

第二步已 `git revert`（`77b98d18b`），车道回落到 `10c71dc58`，`ahead=1`。留下的那半步是：

- `docs/design/effects/edges.cn.md` §5 第 6 条现在写着规则：**对同一个 cell，一次写排在上一次写之后的每一次读之后，用顺序边表示**；读包括 `:var/get` 和读了捕获变量的调用；**链位不是这件事的载体**。
- `src/compile/gir/builder_expr.cpp#recordWrite` 把「读了捕获变量的调用」也当读排序。**以前这个顺序是纯节点占着链位时顺带给的**——那处 `pendingUses_` 里 `isRead == false` 的调用从前只被留着「等下一次写来碰」，而没有一次写来碰它。

读数：三族 + `transforms` + `split` 与 `e3453a60a` **逐名相同**（compile 2 红、opt 0、linear 0、`transforms` 80=77/2/1、`split` 19/19）。把这条调用读的边单独关掉重跑 `feat/linear.var_reference` 仍是 259/259，所以它今天还没有用例依赖它——是一处**补上的缺边，不是一处被修的错数**。

## 6. 我方默认

**第二步不做**，第一步保留；`edges.cn.md` §5 第 6 条的规则留着——它是规则，与「要不要拿掉链位」是两件事。第三步（拿掉链位）若要重开，先解掉第 3 节那条封存拒绝，并给 yield 次序找到另一处承载。

## 7. 留给作者判的一句

**若纯节点必须离开链位，那么 yield 次序就需要另一个承载——那是设计问题，不是实现问题。**