# 84 条「校验脚本退出 1」里，只有 12 条要动程序

- 时间：2026-11-27
- 底表：`D:\Temp\promo28-reds.tsv`（488 条，`P = 1d7e8727b2`）；产物 `D:\Temp\promo28\fail\`
- 逐条表：`D:\Temp\t155-verdicts.cn.md`（84 行 + 每桶 3 条原文）
- 方法：只读判定，未起一次 run，仓库改动 0 行

## 计数（和 = 84）

| 类 | 条数 | 处置 |
|---|---|---|
| A 脚本过期（位置/措辞） | 40 | 机械重录（并入位置重录那一刀） |
| B 读数过期（节点数/副本集合/前驱集合/图 dump 形状/CPU 预算） | 10 | 逐条重录；其中 4 条是预算判定，**不改** |
| C 用例过期（旧写法被收紧） | 22 | 改用例；其中 3 条已被 `08ba99113f` 修好并验绿 |
| D 被测程序缺陷 | 12 | 见下 |
| **脚本坏了** | **0** | 没有一条因语法/环境/Windows 特性失败 |

折叠成“失败发生在哪一层”：断言期望与实测不符 50 · 被测程序输出不对 34 · verify 脚本自身报错 **0**。

## 决定性证据：程序报的位置是对的，旧的是记录

verify 里写死的位置 = `eb20efab7b` **之前**那份用例的行号；实测位置**正好落在现文件里那个运算符/调用名的起始列**上。

```
never_argument         脚本 16 → 实测 18（旧文件里调用就在 16 行，一行式拆成 3 行 ⇒ +2），列 23 不变
never_tagged_position* 脚本 17/18 → 实测 19，列 35/41 不变
unused_fallible.if_arms 脚本 11/12/15 → 实测 11/13/17（旧文件行尾注释自己写着 // line 11 / 12 / 15）
faults(_optimized)/fault.in_map_many 脚本允许 [9,10] → 实测 8（旧 then/else 各一行，重排版并成一行）
json.decode_as/kind.no_document     脚本 12:75 / 12:59 → 实测 14:35 / 14:19（14:19 正是 `kind(` 的起始列）
onnx/export_impure_op               脚本 13:89 → 实测 14:17（正是 `next_key` 的起始列）
consistency.entry/error_from_attempt 脚本 6:37 → 实测 7:16（正是 `return 100 / x` 的除号）
trans.macro 六条                     全部 +1 行、列不变
```

## D 类 12 条（要动程序）

| 计划 / 测试 | 失败原文（摘要） | 读法 |
|---|---|---|
| `compile.diag_json/passes_never_run_json` | stdout 尾部仍有 `2` / `2011` | pass 答空图后 `std::nvm` 仍执行了程序；`docs/cli.md:171` 与 nvm fallback 规则要求不执行 |
| `compile.higher_order_clause/node_role_reads_the_fact` | `camel exited 1`，stderr **空**，stdout 只有 GIR dump | 静默失败、没有诊断 —— 最坏的一种 |
| `compositions/grad_vjp_g_zero.{fvm,nvm}`、`grad_vmap_f.{fvm,nvm}` | 程序自带拒绝：*cannot differentiate through this call of a function value … the function is the result of a call of `vjp::vjp<…>`* | 同目录 `jvp_vjp_f` / `vjp_single` / `hessian_of_a_quadratic` 三条绿 ⇒ 这一组合是缺口 |
| `modules.onnx/export_loop_carried_array_field` | 期望的 `UnhandledError 22:81` 根本没发出：exit 0、stdout 只有 `1` | 拒绝没发 |
| `modules.onnx/pass_export_refusal_located.request_in_arm_json` | 实测 range `17:11-30:12`，脚本要 end=21 | start 一致、end 变宽 ⇒ 拒绝的范围覆盖到块尾，要判是“范围本就该到 30”还是“报宽了” |
| `opt.algebra_rules/algebra_rules.tensor.table` | *the library grew a graph it rewrote: 'array_combine' grew: nodes 9 against the reference's 8, tensor_mul 3 against the reference's 2* | 与“重写提交把被替换的产物留在图里”同域（另见重写事务那页） |
| `transforms/transpose_loop_escape.{3 条}` | 期望 `carried=… origin=… captures=… answer=summed`，实测 `TRANSPOSE function=read_one node=33 state=0 no-cotangent-space` | 转置对 loop-escape 值拒绝给余切 |

## 两条附带读数

1. **82/84 已在 `065bfb1690` 的 531 条基准里** ⇒ 这批全是老红、不是今天引入；2 条例外是 `mm.gc_poison_sweep` 的合成副本。
2. 原先对“第 3 类多是第 1、2 类下游”的预判**成立且现在有了数**：40 + 22 = 62 条是下游，10 条是读数，12 条是真缺陷。
   ⇒ 第 3 类**不该另立一刀**：重录位置（A）+ 改用例（C）做完，84 里应有 62 条自己消失。