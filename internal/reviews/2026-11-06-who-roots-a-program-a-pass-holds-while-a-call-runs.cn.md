# 一个 pass 在运行期造出、只被 C++ 持有的 Program，谁给它 root？

**状态**：待裁。`2026-11-04-does-a-graph-version-outside-the-roots-keep-its-static-values.cn.md`
说的是「图版本活着却不在任何根里」；这一页把它收到更小的一处：**那个「活着」的持有者是一个
Program，而持有它的是 C++ 的栈/成员，不是收集器能看见的东西**。本分支修掉了其中一类持有者
（`GraphDraftGroup`），另有一类仍在窗口里，两条 `poison:modules.nn` 用例因此**仍然 rc=134**
（修前修后同为 134，读数在 §3）。

## 1. 读数

判据用例（`test/plans/feat/modules/nn.plan.toml` 的 `gru_lite_large_bytecode` 与
`residual_mlp_large_bytecode`，pipeline 见该文件的 `args`）：

```
camel --gc-check poison test/cases/modules/nn/gru_lite.cml \
      std::opt::specialize std::opt tensor::fuse std::opt::dce std::opt::fold \
      std::macro std::opt::devirtualize std::fvm            # rc=134
```

两条用例在 `std::macro` 那一站才开始崩（`specialize opt fuse dce fold` 逐段加都 rc=0，
加上 `macro` 就是 134），与 11-04 那页的崩溃链一致。

**同一个缺陷还有一只不依赖 poison 的眼睛**（本页新增）：把移动压力打开，

```
camel --gc generational --gc-stress safepoint=1,kind=minor <case> <pipeline>    # rc=139
camel --gc generational --gc-stress safepoint=1,kind=minor --gc-check verify=both <case> <pipeline>
```

无 poison 也 **SIGSEGV**（rc=139），带 `verify=both` 时验证器在**修前**就打印了完整路径：

```
runtimeGraphRoots[13] -> Program.graph -> Tuple[2]:<model: GruLite> -> Function.program
  -> Program.graph -> Tuple[37]:<model: GruLite>  | region=auto.cache | target=0x…
Reference points into an evacuated part of a young space (stale pointer …)
```

即陈旧指针在被 trace 到的图版本的静态区里，而那个图版本属于一个**经由 Function 值才被看见的
Program**。这条读数不依赖 poison，可以进 fast 层（代价见 §4）。

## 2. 已确认的根因与已修的一半

`GCGraphManager::reserve()` 只在 `running_ == 0` 时把新 Program 挂进 `provisional_` + `gcRoots_`；
`running_ > 0` 时它依赖「run 期造出来的 program 由为它做的 Function 值保活」。**这个前提对
`GraphDraftGroup` 不成立**：组把它的 program 存在 C++ 成员里，没有任何值命名它，而组是在一个
正在运行的调用里造的（`rule_expand`、`remat`、`jvp`、`split`、`autodiff` 都是）。于是组里的图版本
在组活着的时候不被 trace，期间的一次安全点搬走它静态区的值，槽里留下旧地址。
本分支让 `GraphDraftGroup` 在构造时 `pin` 自己的 program、析构时 `unpin`（`GCGraphManager::pin/unpin`
与 `Context::pinRuntimeProgram/unpinRuntimeProgram`），量到的效果：诊断打印里那批 `graphs=657
root=rules::…` 的未 root 持有者（`running=1`）不再出现。

**剩下的一半**：`pin` 之后仍然出现在「未 root 持有者」名单里的，是成批的 9 图 Program，持有的正是
模型那组图（`main` / `train` / `train$spec0` / `predict` / `gru_cell` / `__root__` …），`running=0`
时打印，`detached_` 每批增长 1。它们是「一次 commit 的 `committed`／一个 run 期 `makeProgram` 的
副本」，而**具体是哪一个调用点**本轮没有钉住（下一步：给 `reserve()` 的调用点做一次地址级采样，
`out/latest/libs/libcamel.so` 带符号，`addr2line` 可读）。把「被某个未 root 的 Program 持有的图」
全部临时 root（诊断变体），两条用例立刻 rc=0 且输出正确 9 行 / 1 行——**所以这一类就是缺陷的全部**，
缺的只是「谁来 root 它」。

## 3. 修前/修后（本分支）

| 读数 | 修前 `42ad7a35f` | 本分支 HEAD |
|---|---|---|
| `gru_lite_large_bytecode`（poison） | `rc=134` | `rc=134`（**未修好**） |
| `residual_mlp_large_bytecode`（poison） | `rc=134` | `rc=134`（**未修好**） |
| 同上，`--gc-stress safepoint=1,kind=minor` | `rc=139` | `rc=139` |
| 诊断变体「root 所有未 root 持有者的图」 | `rc=134` | `rc=0`，输出正确 |

stderr 首行两次相同：`[camel] GC poison: a pointer to a Camel object was used after a safepoint
moved the object …`。

## 4. 三条路与代价

**A. 让 `running_ > 0` 时预留的 Program 一出生就是 root（= `lane-gcfreed` 的 road A）。**
代价已量过（`lane-gcfreed` 的 `2cc52a23c`）：修不了这两条，且 `feat/mm/graph_space` 三条变红
（一次 run 造 4000 个梯度 program，全部挂到 settle 才放：graph space 13910 KB > 6000、
bytecode vector 140037 > 40000、JIT 表 4000 个版本），闸门耗时 2 倍。**不建议原样重来。**

**B. 每个把 Program 存在 C++ 里的持有者自己 `pin`，用够就 `unpin`（本分支已按此做了一类）。**
代价：要逐个持有者认领（`committed`、`makeProgram` 的调用点、session 的 work 表），漏一个就还是
一个窗口；收益：不改变存活期，`graph_space` 的内存判据不受影响（本分支 6 个文件、
`+52/−9`）。

**C. 让收集器在「run 期」按台账而不是按根判定存活（`running_ > 0` 时 `programs_` / `graphs_`
即存活集），并只为「带静态区的图版本」付 trace 代价。**
代价：每个安全点都要走一遍全台账（poison 模式下每个安全点都搬空新生代，`gc_poison_sweep`
的 3272 条副本会付出这个代价），要先量一次再谈；收益：一次性关掉整类窗口，不必再靠认领。

## 默认建议（按 B 继续，不等回复）

把 §2 剩下的那批 9 图 Program 的调用点钉住（先做地址采样），
按 B 给每个持有者一对 `pin/unpin`；同一轮里把 §1 的 `--gc-stress safepoint=1,kind=minor`
（不带 poison）加成一条 fast 层可跑的用例**放在钉住之后**——它今天就是红的，先用它当尺子，不要
先把它写进计划。C 留作 B 漏无可漏之后的读数量。

## 关联

- `2026-11-04-does-a-graph-version-outside-the-roots-keep-its-static-values.cn.md`：本条的前一页；
- `lane-gcfreed` 的 `2cc52a23c`（road A 的实现与代价）与 `180a56b30`（reserve 即 root 的第二半）；
- `docs/technical/36_gc_handle_rules.md`：root 规则表，本条应在 `Program` 那一行补一句
  「持有者在 C++ 里时由持有者 `pin`」。
## 决策者答复（2026-11-13）

**走 B，但做成结构上的保证，不靠逐个认领。** 默认建议的 B 是"每个持有者自己 `pin/unpin`"，漏一个就还是一个窗口；这正是目标架构（`docs/design/architecture/README.cn.md`）要去掉的那种"各处自觉"。定为：

1. **一个持有即 root 的句柄**：在 `graph` 层加一个 RAII 类型（名字如 `ProgramHold`），构造时 `pin`、析构时 `unpin`、可移动不可复制。C++ 里任何活得过一次安全点的 `Program` 持有者（`GraphDraftGroup`、提交的 `committed`、`makeProgram` 的调用点、会话的工作表……）都只能经它持有，不许存裸的 `Program *` 或 `shared_ptr<Program>`。`GraphDraftGroup` 现有的 `pin/unpin` 改成用它。
2. **检查脚本**：除这个句柄与 `GCGraphManager` 自己以外，类成员里出现 `Program *`、`shared_ptr<Program>` 即报错。先以"只报告"列出现有持有者，作为改造清单；清单清空后转为硬门槛。这比地址采样更全：采样只能找到这两条用例碰到的那批。
3. **尺子先立**：`--gc-stress safepoint=1,kind=minor`（不带 poison）的那条用例先进 fast 层，标 `known_fail` 并写明本页，改完转绿；两条 poison 用例同时转绿。
4. **C 不做**，除非 1、2 做完仍有窗口；A 不重来（代价已量过）。
5. `docs/technical/36_gc_handle_rules.md` 的 `Program` 一行补一句："C++ 持有者只经 `ProgramHold`"。

归第十一节第 1 路（返工清扫）。
