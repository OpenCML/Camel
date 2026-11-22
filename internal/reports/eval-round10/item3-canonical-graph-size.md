# 第 3 项：案例程序的规范图规模（节点数、边数、字节数）

**树**：`D:\Projects\Camel-wt\eval`（分支 `lane-eval`），HEAD = `2d7c5c3a1`，定版提交，工作树干净。
**构建**：Release，`Camel v0.2.45 (Build 261119_051158_2d7c5c3a14)`，建在宿主 `88zjwei` 的 `~/camel-work/eval`；
`out/latest/build-info.json` 记 `revision = 2d7c5c3a146d0572670def9370a07d5ebe71d38a`、`dirty: false`、
源指纹 `0244fca0cbac…`（1344 个文件）、选项 `09afb0dfe7d8`（默认：`ENABLE_FASTVM_JIT=ON`、`CAMEL_ENABLE_PYTHON=ON`、
`CAMEL_TENSOR_BLAS=ON`、`CAMEL_TARGET_ARCH=native`）。**本机没有构建**（本机构建会蓝屏），走的是
`internal/tools/camel-remote.ps1` 的转发。

**量法**：`camel <file> std::jgir canonical=true`，读它写到 stdout 的规范文档（`"format": "camel.gir"`, `"version": 2`）。

- **节点数** = 文档里 `graphs[*].nodes` 的条数之和（跨文档的全部可达图）。
- **边数** = 每个节点 `inputs[]` 的引用条数之和。规范文档**不写 `edges` 数组**（那是非规范/egraph 文档的形状），
  边以"节点上的输入引用"存在，`kind` 取 `normal|context|control|sync|order` 等。
- **字节数** = 该遍写到 stdout 的整份文档字节数（`dump(2)` 加一个结尾换行），不是源文件字节数。
- 只取 stdout；`std::jgir` 是转译遍，返回空图，程序本身不执行，所以 stdout 里只有文档（`stderr` 为空，退出码 0）。

## 1. 先说清楚「六个写法版本」是哪六个

**树里找不到"案例程序的六个写法"。`examples/case-study/` 只有四个**：

| # | 文件 | 是什么 |
| --- | --- | --- |
| 1 | `examples/case-study/train.cml` | 案例程序原版 |
| 2 | `examples/case-study/writings/renamed.cml` | `w` 改名 `trained`（`w0` → `start`） |
| 3 | `examples/case-study/writings/swapped.cml` | `let X = ...` 与 `let y = ...` 两条声明对调 |
| 4 | `examples/case-study/writings/moved.cml` | `let w = train(...)` 移到第一条 `println` 之后 |

第 5 个 `.cml` 是 `examples/case-study/edits.cml`，那是**改写遍（pass 库）**，不是写法版本，没量。
`README.md` 只点名这四个；`test/tools/writing-freedom.mjs --group spelling` 也只比这四个。

全树 `grep` 无「六种写法 / six writings / six versions」的出处；下发这一段只有在
`internal/reviews/questions-2026-10-29.md` 的「下发」小节里，需求原文在归档仓库
`ECOOP2027/handoff/eval-round10-request.md`——**该仓库与文件在本机不存在**（`D:\Projects\Camel-Archive` 无，
全盘搜 `eval-round10*`、`case-study-request*` 无命中），所以第六个写法没有可查的出处。

**因此本页不猜，也不把三种可能的"六"取舍成一种**：把树里**所有**成对的写法（16 个文件、8 组）全部量了，
按「同一个程序的两种/多种写法」分组给出。要哪六个，从表里取哪六行即可；三组候选的完整读数都在。
逐组说明见 §2、§3，`NOTES.md` 记每个数字怎么来。

## 2. 主表：案例程序的四个写法（这是"案例程序"本体的全部写法）

| 版本 | 文件 | 图数 | 节点数 | 边数 | 字节数 | sha256(前 16) |
| --- | --- | --- | --- | --- | --- | --- |
| original | `examples/case-study/train.cml` | 5 | **63** | **93** | **29945** | `f3978b42249f6224` |
| renamed | `examples/case-study/writings/renamed.cml` | 5 | 63 | 93 | 29945 | `f3978b42249f6224` |
| swapped | `examples/case-study/writings/swapped.cml` | 5 | 63 | 93 | 29945 | `f2fa36d4f1a20af8` |
| moved | `examples/case-study/writings/moved.cml` | 5 | 63 | **94** | **30060** | `66138fb5950779fd` |

读数（本树、本构建，各一次）：

1. **改名：逐字节相同**。`renamed` 与 `train.cml` 同 29945 字节、同 sha256——规范文档看不见名字。
2. **交换：同规模、不同文档**。63 节点 / 93 边 / 同样 29945 字节，但 sha256 不同：两处常量（`y` 的 `[2,-1,1,3]`
   与 `X` 的 `[1,0,0,1,1,1,2,1]`）在规范编号里互换了位置。这是 `README.md` 第 2 条记的规范编号缺陷
   （原则 9.5 的 tie 用书写顺序解决而不是结构），本页只报规模，不重复裁定。
3. **移动：多一条边**。63 节点不变、93 → **94** 边，多的是 `:io/println` 指向 `train` 调用的那条 `order` 边
   （entry 图 `g1` 从 50 条变 51 条）；字节因此从 29945 涨到 30060。

## 3. 另外四组「同一程序的两种写法」（同一棵树上，同一构建，同一命令）

这三组不是 `train.cml` 的写法，是另外两个程序；放在这里是因为**任何对"六个"的读法都会落到它们中的一组**，
给了数字就不必再猜。

| 组 | 版本 | 文件 | 图数 | 节点数 | 边数 | 字节数 | sha256(前 16) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 一个梯度步（var 单元） | var | `test/cases/compile/var/step_var.cml` | 3 | 23 | 29 | 9183 | `b75fd6d66d7b3909` |
|  | let | `test/cases/compile/var/step_let.cml` | 3 | 23 | 29 | 9183 | `b75fd6d66d7b3909` |
| 循环体的累加 | var | `test/cases/compile/var/loop_var.cml` | 3 | 24 | 23 | 9801 | `24eaf169d3619272` |
|  | let | `test/cases/compile/var/loop_let.cml` | 3 | 24 | 23 | 9801 | `24eaf169d3619272` |
| 分支里的赋值 | var | `test/cases/compile/var/arm_var.cml` | 3 | 13 | 13 | 5418 | `838e74d0cb36fdc9` |
|  | let | `test/cases/compile/var/arm_let.cml` | 3 | 13 | 13 | 5418 | `838e74d0cb36fdc9` |
| `sync` 链：纯且不故障 | first | `test/cases/compile/chain/pure_call_first.cml` | 3 | 10 | 8 | 3947 | `ac891942c54a8cf0` |
|  | second | `test/cases/compile/chain/pure_call_second.cml` | 3 | 10 | 8 | 3947 | `ac891942c54a8cf0` |
| `sync` 链：带效应 | first | `test/cases/compile/chain/effect_call_first.cml` | 3 | 8 | 9 | 3677 | `28a4f2a28736c649` |
|  | second | `test/cases/compile/chain/effect_call_second.cml` | 3 | 8 | 9 | 3677 | `27d7dea24c8391e7` |
| `sync` 链：纯但会故障 | first | `test/cases/compile/chain/fault_call_first.cml` | 3 | 10 | 10 | 4178 | `478f1af0ceac50e8` |
|  | second | `test/cases/compile/chain/fault_call_second.cml` | 3 | 10 | 10 | 4178 | `e2e497783b43c97c` |

两点读数值得写下来（都只报规模事实）：

- **`var` 三对今天都是逐字节相同**：`step_var` 与 `step_let` 同 9183 字节、同 sha256，`loop` 对、`arm` 对同样相同。
  `README.md` 记的旧读数是"step_var 10930 / step_let 9179，red"，在 `2d7c5c3a1` 上 **不成立**——`var` 化成 SSA
  那笔落地后，两种写法已经是同一份规范文档。旧数不复用（作者明令），以本页为准。
- **带效应 / 会故障的两组：同规模、不同文档**（3677/3677、4178/4178 字节，sha256 不同）。规模一样，
  差别在编号与边的写法上，与"这两组必须分家"的判据一致；本页只报规模。

## 4. 这些数怎么核对得上

- 用同一套数法量 `train.cml` 得 **63 节点 / 93 边**，与 `examples/case-study/README.md` 记的
  "63 nodes and 93 edges" 逐字相同；逐图分解也对得上 README 的图规模清单（根 2、`loss` 14、`step` 8、
  `train` 13、入口 26 = 63）：

  | 图 | 节点 | 边 | 区域 |
  | --- | --- | --- | --- |
  | `g0`（根） | 2 | 1 | 0 |
  | `g1`（入口 `main`） | 26 | 50（`moved` 为 51） | 0 |
  | `g2`（`loss`） | 14 | 15 | 0 |
  | `g3`（`step`） | 8 | 8 | 0 |
  | `g4`（`train`） | 13 | 19 | 2 |
  | 合计 | **63** | **93** | **2** |

- **字节数与本仓库 `README.md` 的 29941 不同（本页 29945，差 4）**：那是 pin `96a27e435` 上的旧读数，
  本页没有沿用、也没有拿它推算；原因未查（不在本项范围内）。同一棵树、同一构建的复跑是稳定的
  （两次都是 29945、同 sha256）。

## 5. 复现命令（一条）

```
camel examples/case-study/train.cml std::jgir canonical=true
```

其余文件把路径换掉即可：`writings/renamed.cml`、`writings/swapped.cml`、`writings/moved.cml`、
`test/cases/compile/var/step_{var,let}.cml`、`loop_{var,let}.cml`、`arm_{var,let}.cml`、
`test/cases/compile/chain/{pure,effect,fault}_call_{first,second}.cml`。

在宿主上的完整复跑（含取数脚本）见 `NOTES.md` 第 4 节；`NOTES.md` 同时也记了构建标识与每个数字的来源。