# 卡：一条 XFAIL 标记"陈旧"的真身——不是转绿，是**判词丢了**

**发现者**：`lane-block-hash`（`D-1128-57` 的收尾件，`lane/xfail-onnx-markers`）。起点是决策者说
`ort_loop_loaded_weights` 的 XFAIL 已经陈旧（"要么绿了、要么断言换了"）。

## 现象

改之前那一条的判词只有两行、什么也没说：

```
[FAIL] ort_loop_loaded_weights                              550.56 ms
[INFO] expected exit 0, got exit 1
[INFO] known failure changed its mind: expected "kind: ExportUnsupported, message: in 'arm4.1.pb':
       the dtype or shape of a value changes from one call of a recursion to the next",
       got "expected exit 0, got exit 1"
```

同一个 plan 里另一条 `ort_loop_values_dynamic` 一直是老老实实的 `[XFAIL] expected exit 0, got exit 1`。
差别不在"红不红"，在**判词里还有没有信息**。

## 追到底（探针，跑完即删，没进提交）

`test/cases/modules/onnx/ort_loop_loaded_weights.cml` 的出口是自吞的：

```cml
match export_model<...>(...) {
    case error e => sync { println(e); 0 }   // ← 把拒绝打印成值，再答 0
    case _ => 1
}
println(exported)
if (get_env('ONNX_OUT') ?? '') == '' || exported == 1 then 0 else 1
```

camel 的真实输出（探针抓的原文）：

```
rc: 0
stdout: "{ kind: ExportUnsupported, message: in 'arm4.1.pb': the dtype or shape of a value changes
         from one call of a recursion to the next; this exporter does not yet carry an array whose
         length changes between rounds as an ONNX sequence, ...
stderr: ''
files:  ['native_0.npy', 'native_1.npy', 'native_2.npy', 'native_3.npy', 'w.npy', 'x.npy']   ← 没有 m.onnx
```

- **`exit 0` 不是绿**：`.cml` 里那个 `if … then 0 else 1` 只是一个表达式的值，没当返回码用，所以
  无论如何都是 0。python 侧 `camel()` 只看返回码，于是放行；下一步 `onnx.load(m.onnx)` 崩在
  `FileNotFoundError`，判词退化成 runner 的那句模板话。
- 于是"这个用例现在是什么状态"这件事，**从判词里读不出来**了：它仍红（导出被拒），但红在哪、拒的
  是哪句，全丢了。陈旧的是**标记**（`known_fail_msg` 还钉着旧句子），不是事实。

## 修法（不是改期望）

1. `known_fail_reason` 两条都还引着被撤回的前提 `a loop can only carry values of a fixed dtype
   and shape`（`test/plans` 里 2 命中、`src` 里 0）⇒ 改成引**树里真在打的那句拒绝**；
   `known_fail_note` 写明"task-383 落地时转绿"（`known_fail_ends_when` 只有 `{prints}`/`{passes}`
   两种形状，"谁修"只能落 note）。
2. `known_fail_msg` 换成现在真打的那句 `this exporter does not yet carry an array whose length
   changes between rounds as an ONNX sequence`。
3. **用例侧加一句**（`camel()` 里）：程序打印了 `kind: ExportUnsupported` 就带着拒绝原文抛
   `FAIL: the export was refused:`。不动语义、不动 `expect_*`、不动结束条件。

改后：

```
[XFAIL] ort_loop_values_dynamic                             962.64 ms
[XFAIL] ort_loop_loaded_weights                             836.98 ms
[SUMMARY] total: 2, pass: 0, fail: 0, known failures: 2
[KNOWN] …ort_loop_loaded_weights: prints "ort loop loaded weights: worst"
```

## 规矩（这一条要记住的形状）

> **一个吞掉拒绝的用例，会把判词退化成一句没有信息量的话。修法是让拒绝原文能被抛出来，不是改期望。**

- 判据分辨：`exit 0` 只说明"这条路走完了"，不说明"这件事对了"；`case error e => println(e); 0`
  这类"把错误当值再答成功"的写法，在用例里等价于**把红灯的线剪了**。
- 同一形状的第二例：`--verify-ops` 那句两侧文本同错，所以比出来是绿的——**绿不等于对**。
- 自查一句：**任何 XFAIL 的 `known_fail_msg`，必须在"判词文本"里真的出现过**；`known failure
  changed its mind` 是 runner 在替你看这句话，它一喊，先查"判词是不是丢了"，别急着改期望。
- 加标记三件套的顺序：`known_fail_reason` 引**树里现存的**那句（不是被撤回的那句）、
  `known_fail_note` 写谁修、`known_fail_msg` 钉**现在的判词文本**。

## 附一：同一个病的四条 slow 红（`tier = "slow"`，promotion 不跑它们）

同一天在 `test/plans/feat/modules/onnx.plan.toml` 里读出 9 条红，其中 5 条在 141 名快层表内，另
4 条**都不在快层表**里：

| 名 | 判词形状 | 归类 |
|---|---|---|
| `modules.onnx/ort_core_slice` | `export failed: … UnhandledError 0x04000051`，`ort core slice: 5 failing` | **既有**（已对拍，见附二） |
| `modules.onnx/export_loop_for` | `stdout does not contain 'onnx::loop node for: PASS'` | 既有 |
| `modules.onnx/pass_export_equals_export_model` | `stdout does not contain 'onnx::export_model equiv: PASS'` | 既有 |
| `modules.onnx/resnetbn_train_step_export` | `251.60 s cpu of 199.00 s budget` | **预算/压力类** |

四条是**同一个病**：用例自己把导出的拒绝吞了（`case error e => println(e); 0` 一类写法，或 fixture
把 export 的失败当成"值"再继续），于是 `export failed:` 后面跟的是 `UnhandledError`——**判词丢了**，
和本页正文那条 `ort_loop_loaded_weights` 一模一样。修法也一样：**让拒绝原文能被抛出来**，不是改期望。

## 附二：`ort_core_slice` 的对拍——不是本轮 `:op/slice_sub` 的回归

失败集中在**开区间切片**子例（`posAll` = `t[..]`、`posEnd` = `t[..b]`、`sweep` 扫边界、`negAll` /
`negOne` 负界），而这一轮恰好给 `:op/slice_sub` 加了 2 条 `kRuleKeyExceptions` 例外，重合得查。
判据与读数（一次构建、`-Jobs 2`、`-QueueOnTreeLock`、`BUILD_EXIT=0`、两棵树 `TREE_STATE=clean`）：

| 树 | 失败子例集合（`HashSet`） | 计数 |
|---|---|---|
| `b3c4a3ff52^1`（`ab522285fa`，`lane/key-tables` 合并**前**） | `negAll, negOne, posAll, posEnd, sweep` | 5 |
| `44182d9899`（合并后当前尖端） | `negAll, negOne, posAll, posEnd, sweep` | 5 |

- **交 = 5，两边独有 = 0，`ort core slice: 5 failing` 两边同数** ⇒ **两边都红** ⇒ 早已存在，**不是**本轮
  `kRuleKeyExceptions` 两条 `:op/slice_sub` 带来的回归。（若"合并前绿、现在红"，按规矩要停手报负责人，
  不自己改。）
- 复现方式（探针计划跑完即删，不进提交）：`case = "…/cases/modules/onnx/ort_core_slice.py"`，
  `expect_stdout_contains` 故意写一个不存在的串让 run 判红，再 `-FetchFile
  'fail/<plan>/ort_core_slice/output.txt' -FromThisRun` 取逐子例的行，按 `^(\w+): export failed`
  收名字集合。
- 注意 `[provenance]` 会写 `(dirty: 1 files)`：那 1 个就是探针计划本身，两边相同，不影响对拍结论。

## 附三：账上没有 ≠ 没红（今天第二次）

- **事实**：这 4 条在 `2026-11-28-promotion-74/75/76-reds.tsv`（141 名快层表）里**一条都没有**，但在
  `internal/reports/2026-11-28-daily-all-reds.tsv`（262 行，全日闸门）里**逐名都在**。
- **原因**：它们是 `tier = "slow"`，**promotion 的快层根本不跑它们**；红不红只有全日闸门看得见。
- **口径**：说"表外红 0"必须**说清对哪张表**——对 141 快层表是 0，对 262 全日表也是 0；只对前者说 0
  会把 slow 层的红当成不存在。这和今天 `--verify-ops` 那句（两侧文本同错所以比出来是绿的）是同一个
  形状的错：**账上没有，不等于没红；比出来绿，不等于对。**