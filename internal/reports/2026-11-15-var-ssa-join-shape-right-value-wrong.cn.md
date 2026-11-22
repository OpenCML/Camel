# 三件一起落地：②a 的形状对了，运行时的值还是错的

日期 2026-11-15。分支 `lane-varscope`，基线 `f1efa81f0`。三处改动（②a 值合并点、折叠承认合并点、
放开设值算子的逃逸判定）在同一棵树里编过、跑过，**然后全部撤回**，`git diff --stat` 为空。

## ②a 确实把合并点做成了带值的

`q1`（`var x = 0; if c then { x = 1 }; println(x)`）的图：

```
N04 [label="JOIN", type="JOIN", value_type="int"]     ← 不再是 void
N08 -> N04 [0|0 WITH]                                 ← 臂的值（闸门出口）
N02 -> N04 [0|1 WITH]                                 ← 分支之前的版本（原来是 null）
:var/get 计数 = 0                                     ← 合并点被折叠认了，读没了
```

三条判据里的"形状"部分全部达成：合并点带值、槽 1 是分支前的版本、`:var/get` 从 1 变 0。

## 但运行时的值不对

```
q1:  nvm true=0 false=0   fvm 0/0   jit 0/0   prl 0/0      （应为 1 / 0）
q3:  get=0, stdout 1                                       ✅ 直线仍对
q2:  get=0, nvm/fvm/jit/prl 全部 0                          （应为 3）
```

`q2`（循环体里写）也一起坏了：`LOOP` 那条路径的版本来源既不是赋值算子也不是值合并点。

## 结论

**折叠把这两个读折叠到了"单元造出来时的值"（0），而不是分支/循环携带的版本。**
也就是说 `variableReadSource_[read].source` 在这两个形状上并不是我假设的合并点/循环节点；
先前的 `v0` 与 `v1` 两个读数说的"来源是合并点"只在**图上的顺序边**成立，不是折叠读的那个字段。

下一步应当**先在折叠里打印 `source` 的类型与 kind**（`q1` 分支一次、`q2` 循环一次），
确认 `source` 到底是谁，再决定"接受合并点"那句放在哪个分支里。在没确认之前，
"三件一起落地"不能再试——这一次的读数说明：形状对了，值还是错的，而且和上一轮
"单独放开逃逸"错得一模一样（都是退回初值）。

## 读数汇总

| 项 | 结果 |
|---|---|
| `q3` 直线 | `:var/get` 0，stdout `1` ✅ |
| `q1` 分支 | `:var/get` 0，四后端 stdout `0 / 0` ❌（基线 `1 / 0`） |
| `q1` 合并点 | `value_type="int"`，槽 1 = `N02`（分支前的版本）✅ |
| `q2` 循环 | `:var/get` 0，四后端 stdout `0` ❌（基线 `3`） |
| `feat/parse` | `total: 115, pass: 115, fail: 0`（远端 `88zjwei`，带三处改动构建） |
| 树 | 三处改动已 `git checkout --` 撤回，`git diff --stat` 为空 |

`[provenance] tip (dirty: 3 files) | binary built 2026-11-15T03:11:21-05:00 | tree /home/zjwei/camel-work/varscope | tier fast | jobs 8`