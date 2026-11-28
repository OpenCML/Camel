# task-420 / task-421 静态地图（只读取证，未构建、未修改）

来源：`lane-witness`，树 `D:\Projects\Camel-wt\witness` @ `c12c06f527`（`lane/draft-ref-oob-wip`）。
本页只用已有的只读读数，不新增推断；行号取自该 sha 的 `src/stdlib/transforms/transpose/walk.cpp`
与 `src/compile/src/gir/builder_control.cpp`、`src/stdlib/transforms/transpose/cotangent.cpp`。

## 1. 结论两行

- **task-421 前提更正**：`stateBase` 在 `walk.cpp` 里**只有一处**——`:2945 stateBase = iterates ? 1 : 0`；
  `stateCount` 由同一条三元式在 `:2941` 决定；真正的拒绝门槛是 `:2946`。交接页说"五处写死 `(iterates ? 1 : 2)`"不成立，
  那个写法在本文件里根本不存在。
- **task-420 = 路径 2**：非法 `0xFFFFFFFF` 是 `loopNorm` 自己的兜底失败后 push 进去的，**与 `stateInitials` 无数据流**。

## 2. `task-421` 的改动面

判定来源（`src/stdlib/transforms/transpose/walk.cpp`）：

```
:2941  const size_t stateCount = iterates ? regionPorts.size() - 1 : regionPorts.size();
:2945  const size_t stateBase  = iterates ? 1 : 0;
:2946  if (regionPorts.size() > 0 && regionPorts.size() < (iterates ? 2u : 3u)) { refuse(TransposeRefusalCause::State); ... }
```

`stateBase` 的五个使用点都只是**读**，不再另行判断：`:3671`、`:3865`、`:3935`、`:4047-4048`。
`:2935-2944` 的注释解释了两条读法：`while` 循环没有流可拉，端口就是状态，减掉两个机器值会算错。

前端侧（`src/compile/src/gir/builder_control.cpp`）：

```
:3942  addRegionLoopNode(kind, loopValueType)              ← 用变量 kind
:4377  addRegionLoopNode(LoopKind::While, machineType)     ← while 那支写死 While
```

端口布局两支：

```
:4065  const std::string streamName = "$stream";
:4066  const std::string doneName   = "$done";
:4079-4081 machineTypes{stream, Type::Bool()} + stateTypes ⇒ TupleType
:4148  端口名映射 i==0 ? "$stream" : i==1 ? "$done" : stateNames[i-2]
:4156-4182 declarePorts 体，add 顺序写死：:4176 add($stream) → :4177 add($done) → :4178 状态名
:4354  while 的 body 区域也调同一个 declarePorts
:4358-4363 carried 的端口从 2 + k + j 起读（假设前两位已占，与 add 顺序一致）
```

端口的唯一判据：

```
names.size() >= 2 && names[0] == "$stream" && names[1] == "$done"  ⇒ 流式布局（{stream,done,state…}）
否则                                                              ⇒ 只有状态的布局
```

理由：`$` 前缀是保留名，状态名来自用户的 `stateNames`，不可能以 `$` 开头；`add` 顺序写死，位置判据与名字判据同一答案。
仅有的两种歧义形状：0 个端口（已被 `:2946` 的门槛 refuse 成 `State` 原因）、1 个端口（只可能是状态布局）。

## 3. `task-420` 的根因链

```
walk.cpp:4136            std::vector<gc_node_ref_t> loopNorm;            ← 唯一声明
walk.cpp:4141-4147       iterates ? push_back(backwardsRange) : push_back(firstRound)
walk.cpp:4151-4182       每状态：kNoComponent 跳过；stateCount==1 ⇒ ctLoop；
                         多状态 ⇒ slot = memberCotangents[ref][i]，缺则 rt::kInvalidNodeRef（:4162-4165）
                         seed = slot==Invalid ? zeroOfSpaceOf(..., stateCotangentTypes[i], rt::kInvalidNodeRef) : slot（:4173-4176）
                         仍 Invalid 且类型非空 ⇒ seed = zeroOfType(type, type)（:4177-4180）
                         push_back(seed)（:4181）
walk.cpp:4183-4196       caught：componentZero = zeroOfSpaceOf(..., componentType, rt::kInvalidNodeRef)（:4191-4192）
                         仍 Invalid 且类型非空 ⇒ zeroOfType（:4193-4195）⇒ push_back（:4196）
walk.cpp:4198            draft.setNormInputs(loopNode, loopNorm)
```

关键：`:4175` 与 `:4192` 把 `shapeFrom` **恒传 `rt::kInvalidNodeRef`** ⇒ 凡"零靠 `zerosLikeUri` 按形状构造"的切空间，
在这两处**构造上必然**走到 `cotangent.cpp:270` 返回 Invalid；随后 `zeroOfType` 兜底（`walk.cpp:524-526`，转发
`zeroOfCotangentType`）若也给不出节点，push 进去的就是 `0xFFFFFFFF` = `rt::kInvalidNodeRef`，
`:4198` 立刻接收 ⇒ 运行期那句 `The norm input list of node 29 names draft node 4294967295 …`。

`stateInitials` 与这条链无关：它在 `:3933` 声明、`:3936` push、`:3940` 判，之后只在 `:4248`/`:4252`/`:4285-4286` 被**读**。
`:3935` 那条"单一状态累加"的 `stateInitials[i] == rt::kInvalidNodeRef` 检查实际位于 `:4246-4254`，
位置在 `:4198 setNormInputs` **之后** ⇒ 写 `loopNorm` 之前没有同一批初值的检查。

## 4. 修法（协调者已定，非本卡提出）

- 两处兜底"都失败"的情况从 `push_back` 改为 `refuse`：`walk.cpp:4177-4181`（state 的 seed）与 `:4193-4196`（caught 的 componentZero）。
- 复用 `TransposeRefusalCause::State` + 一行点名**下标**的文本；**不新增 enum、不改别的拒绝文本**；**不补零**。
- 独立一笔提交，排在 `task-419` 之后。

## 5. 探针（只一处）

在 `walk.cpp:4198` 之前遍历 `loopNorm`，打出 `== rt::kInvalidNodeRef` 的**下标**；
配合 `:4151` 的 `stateCount`、`stateComponent[i]` 与 `:4183` 的 `caught.size()`，
即可把下标映射回"哪个状态的 seed"或"第几个 caught 的零"，从而分开 `:4177-4181` 与 `:4193-4196` 两处。

## 6. `zeroOfSpaceOf` 的四个 `kInvalidNodeRef` 分支

`src/stdlib/transforms/transpose/cotangent.cpp:250-275`：

```
:253  space == nullptr
:258  DerivativeRegistry::instance().findTangentSpace(space) == nullptr
:264  registered->zero 无值，且 registered->zerosLikeUri 为空
:270  zerosLikeUri 有值，但 shapeFrom == rt::kInvalidNodeRef
```

`:266-270` 的注释原文（这就是"拒绝而不补零"的仓库规矩）：

> No value of this walk's to read the shape from, and no zero built in its place: a zero
> array answered a law's `shape(...)` read with a value that was not the argument, and the
> answer to "no shape" is the refusal, not an invented array.

`zeroOfType` 只是转发：`walk.cpp:524-526` → `zeroOfCotangentType(walkCtx, primal, tangentType)`。

## 7. 交接页漂移清单

交接页（`task-420-handover.md` / `task-420-draft-ref-handover.md`）中的 `3630 / 3642 / 3836 / 3906 / 4018-4019`
**全部作废**——那些行号现在落在注释或别的代码上。改指：

```
:2941  stateCount 的三元式
:2945  stateBase 的唯一一处
:2946  真正的拒绝门槛
```

## 8. 来源与边界

- 全部读数来自**只读静态取证**（`lane-witness`，树 `D:\Projects\Camel-wt\witness` @ `c12c06f527`）。
- 本卡**未 build、未 test、未改代码、未提交、未推远端**；主检出 `git -C D:\Projects\Camel status --porcelain` 为空。
- 静态能定的边界：**数据流上非法值不是从 `stateInitials` 来的**（路径 2 那一侧）；
  "究竟是 `:4177-4181` 还是 `:4193-4196` 漏的"静态判不了（两处形状相同，取决于运行期哪个类型为空/零构造失败），
  须由第 5 节的单点探针回答。