# 一个构造、一次成员读会不会故障：记录照 kind 的答案说

路线三把旧的节点种类搬成算子之后，同一个操作在两处答过「会不会故障」：kind 节点答 `false`
（`kernel/runtime/graph.cpp#nodeMayFault`，`Construction` 与 `Access` 两臂），算子记录答
`true`（`:op/fill`、`:op/access`、`:op/access_key`，`std/std/builtin/executors/builtin.cpp`）。
两处不一致，节点从 kind 形状换成算子形状时答案就翻转；量到的是**全部**这类节点一起翻转
（2026-11-25 的 4238 个 `oper=-` 节点）。裁定是**以 kind 节点的 `false` 为准**，改记录那一侧。

这一页就是那一步：记录、声明、一处因此变假的注释；三条计划读数；以及决策者要的
「拿 `:op/access` 读动态值的地方」清单（第六节，不混进清零计数）。

## 一、改了什么

| 文件 | 改动 |
|---|---|
| `src/stdlib/std/builtin/executors/builtin.cpp` | `:op/fill`、`:op/access`、`:op/access_key` 三条记录 `mayFault = true` → `false`；上面那段「每个都可能故障」的理由整段重写为 kind 侧的理由（构造：模板是目标类型的常量，类型是调用自己的结果类型，运行期没有可检查的；成员读：位置是调用的编译期参数） |
| `src/stdlib/prelude.cml` | `fill`、`access`、`access_key` 三个声明去掉 `panic`，各加一句说明为什么不会故障（哪一侧的答案、为什么） |
| `src/stdlib/std/builtin/operators/struct.cpp` | 内核注释里「位置与名字不是编译期的：程序会写它们（`access::<T,U><at>(x)`）」这一句**改假了**，改成「位置与名字是构造器从类型里取的；源码形式的调用在建成节点前就被拒（`cml.cpp#memberReadExpression` 说为什么），两个报告是给手搭图的守卫」 |

**一处对裁定的扩大解释（已单独报）**：`access_key` 是同一个 `Accs` kind 的第二种拼法
（`GCNodeKind::Accs` 的 `accsKind` 分 `TupleIndex`/`StructKey`，`nodeMayFault` 对两者都答
`false`）。只改 `access` 会让同一种读的两种拼法答两个值 —— 正是这条裁定要消掉的东西，所以
按 kind 侧把第三个也改成 `false`（记录 + 声明 + 注释）。若裁定收紧，删三行即可。

## 二、计数（一条命令量两处，3 → 0，跑两次）

```powershell
# 记录那一侧：这三条记录里还说着「可能故障」的行
git grep -cE 'registry\.set\(":op/(fill|access|access_key)".*mayFault = true' -- src
# 声明那一侧：这三个声明里还带 panic 的行
git grep -cE '^export func (fill|access|access_key)::<.*panic$' -- src/stdlib/prelude.cml
```

改前（`HEAD` = `c7792df5e`，即这一页报告所在提交）：

```
HEAD:src/stdlib/std/builtin/executors/builtin.cpp:3
HEAD:src/stdlib/prelude.cml:3
```

改后（工作树，第一次跑）：两条命令都没有匹配，`exit 1`（`git grep` 无匹配的退出码）。
第二次跑（复现，换成逐行）：三条记录读出 `mayFault = false`，三个声明读出没有 `panic`：

```
src/stdlib/std/builtin/executors/builtin.cpp:822:    registry.set(":op/fill", {.pure = true, .elementwise = false, .mayFault = false});
src/stdlib/std/builtin/executors/builtin.cpp:823:    registry.set(":op/access", {.pure = true, .elementwise = false, .mayFault = false});
src/stdlib/std/builtin/executors/builtin.cpp:824:    registry.set(":op/access_key", {.pure = true, .elementwise = false, .mayFault = false});
src/stdlib/prelude.cml:1015:export func fill::<T, ...F>(template: T, ...fields: ...F) => T
src/stdlib/prelude.cml:1034:export func access::<T, U>(x: T) => U
src/stdlib/prelude.cml:1045:export func access_key::<T, U>(x: T) => U
```

**这一步的判据不是清零**（「98 个真读者清零」是下一步），是「改完仍无新红」。下面三条计划
是它的正证：一条查两份存储逐算子对读，两条查 `mayFault` 的直接下游（`may_be_rearranged`）。

## 三、计划读数（真退出码与 `[SUMMARY]`）

在 88 主机上，一次一个计划、`-Jobs 4`，`tip c7792df5e`（dirty: 3，即本页改动），
`check-doc-links` exit 0。

| 计划 | 退出码 | `[SUMMARY]` | 说明 |
|---|---:|---|---|
| `feat/modules/op_record.plan.toml` | **0** | `total: 2, pass: 2, fail: 0, re-run: 1, re-run passed: 1` | `uses_reach_the_record` 45.16 s（逐算子对读声明与记录）；`cpp_registration_states_panic` 首跑超时、复跑 180 ms 通过（宿主 load 41） |
| `feat/tools/facts.plan.toml` | **0** | `total: 11, pass: 11, fail: 0` | 含 `compare_arrangeable*` 四条，`arrangeable` 的读数在这里是对的 |
| `feat/tools/arrangeable.plan.toml`（第一次） | 1 | `total: 2, pass: 1, fail: 1` | 失败的是 **CPU 预算**，不是答案不一致：工具的 `exit_code` 是 0，且它只在 `red == false` 时才打印的结论行打印了「every operator's answer agrees, over 2983 cases, 238571 nodes and 177 operator rows; 7478 node(s) … not decided here」。meta.json：`cpu_ms 451810` 对 `cpu_limit_ms 450000`（超 0.4%），`wall 127391 ms` |
| `feat/tools/arrangeable.plan.toml`（第二次） | **0** | `total: 2, pass: 2, fail: 0` | 同一条命令、同一棵树、`facts.arrangeable_every_operator_agrees` 114.53 s，`[CPU] 442.1 s`（在 450 s 预算内） |

第一次那条红是**预算边沿**：这条计划每读一个 `.cml` 起一个 `camel`（本轮读出 2983 个可读），
宿主当时 load 29–43（同机 7 条别的运行），CPU 451.8 s 对 450 s 的预算差 1.7 s；20 分钟后同样的
树、同样的命令、442.1 s 通过。工具自己的判定两轮都是「every operator's answer agrees」。
预算不是我能改的判据，所以记在这里而不动它。

## 四、与晋级红表逐名比

`internal/reports/2026-11-26-promotion-9-reds.tsv`（`git show 5450962ad:` 读，630 行）：

- `modules.op_record`、`tools.facts`、`tools.arrangeable` 三条计划在表里**一行也没有** ⇒ 没有
  可逐名比的失败行；本轮三条全绿（arrangeable 的第一次是预算边沿，见上）。
- 层（`tier`）：`op_record` 两条都是 `fast`；`facts` 十一条都随计划的 `fast`；`arrangeable`
  是 `tier = "slow"` 的计划（`facts.arrangeable_conversion_pair` 单条标 `fast`，另一条随计划），
  以计划文件为靶时整份跑。

## 五、要重编的翻译单元数（同一条命令量两次）

仪器：`camel-remote.ps1` 一跑里的 build 段，`[k/N] Building CXX object …` 的行数就是要重编的
翻译单元数（其余是同一次构建的链接步）。

| 改动 | 要重编的 TU |
|---|---:|
| 记录 + 声明（`builtin.cpp`、`prelude.cml`） | **1**（`builtin.cpp`；`prelude.cml` 是数据，不产生 TU） |
| 再加内核注释改正（`struct.cpp`） | **2**（`builtin.cpp`、`struct.cpp`） |
| 头文件 | **0**（这一步没碰内核或编译头） |

对比基线：这两跑里另有 `build_stamp.cpp` 的 1–2 个 TU，那是构建戳，不是这次改动引起的。
下一步的稀疏槽位要动内核头（`GCFillBody`/`makeFillPayload` 一带），同一个仪器会给出大得多的数，
到时按同一口径量两次。

## 六、附录：拿 `:op/access` 读「动态值」的地方（决策者逐个看，不计入清零）

先看**写**的一侧：前端从来没有把「位置」当运行期值传过。两个构造器
（`Builder::memberReadNodeOf`，`compile/src/gir/builder_control.cpp:375` 与 `:425`）的位置参数分别是
`uint32_t` 与 `std::string`，落成 `addStaticDataNode` 的常量并 `rememberConstant`，位置是调用自己的
`with` 实参，值是一个普通操作数。**所以语言里没有一条路能造出「位置由运行期算出」的 access 调用。**

再看**读**的一侧，六处都考虑过「位置是算出来的」，各自的处置：

| # | 位置 | 对「算出来的位置/名字」怎么处置 |
|---|---|---|
| 1 | `std/std/builtin/operators/struct.cpp#__access_value__`（`:1596-1625`） | **真的执行**：从 `with` 的 frame 槽里取位置/名字，越界 `IndexOutOfRange`、无此字段 `FieldNotFound`。唯一会故障的地方 |
| 2 | `std/cpp/function_emitter.cpp#lowerAccess`（`:1860-1870`） | **拒绝**：「reads a field … by a name the program computes, and a record is a C++ aggregate with no field table to look that name up in」 |
| 3 | `onnx/exporter.cpp`（`:4140`、`:4150`） | **导出期拒绝**：`unsupported`，「the export reads a place at export time」 |
| 4 | `tiles/src/compiler.cpp:744-757` | **拒绝**：`!member.constant` → 抛「a member read's member has to be known when specializing」 |
| 5 | `transforms/node_operator.h#memberReadOf`（`:486`） | **当成不是一次读**：`constantOf` 为空就 `nullopt`，于是遍看不见这次读 |
| 6 | `autodiff/pullback.cpp#operAccessPlaceOf`（`:916-930`，冻结） | 同样 `nullopt`；它的注释点名了「a place the program computes」 |
| 7 | `std/cml/cml.cpp#memberReadExpression`（`:3848` 起） | 按 `with` 参数**类型**（`string`/`int`）分两种拼法并印后缀；它要的是常量才能印出字面 |

结论与建议写在计数之外：**语言只造常量位置的读**，所以记录答 `false` 对语言造得出的调用是对的；
六处读法里一处执行、五处拒绝或看不见，没有一处会读到一个「动态值」还回答一个答案。5、6 两处
（遍与求导）遇到手搭的图会**静默地不当成读**——那是它们各自的旧行为，不是这一步引入的，记在这里
供逐个看时一起裁。

## 七、还没做的

- **98 个真读者清零**（`kind=` 口径）没有开始：它要先有这一步的答案，否则读法上还是两种形状。
- **稀疏/换序槽位**（模板前用 `int` 逐个成员写位置）只在 `prelude` 的声明里写明，内核还不支持；
  下一步照声明补，补的是声明已承认的形状。
- `transforms/` 照常迁（不再算冻结）；冻结只剩 `autodiff/` 的求导引擎与规则表、以及 C++ 优化遍。
- `:op/` 改名仍未做（676 文件 / 4558 处），也不在这条分支上。