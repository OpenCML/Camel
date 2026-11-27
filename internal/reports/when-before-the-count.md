# 先读 `when`，再数候选：一个根因、三张脸

分支 `lane/arrangeable`（本条车道）。起点是 `registry.docs_complete` 里
`src/stdlib/stream.cml:165` 的一条文档红：`stream` 模块的 `zip` 文档示例

```
zip(from_array([1, 2]), from_array(['a', 'b', 'c']))->collect
```

报 `SemanticDiag::AmbiguousCall`（`The call to 'zip' with (int.., string..) fits more than one
overload`）。这一条文档红背后是同一个根因的三面：**歧义**（zip）、**误绑**（`len(Bytes!)`）、
**管道的退让被跳过**（`->` 拆一层再问的那一趟没走到）。三面都是"候选的 `when` 没在数候选之前读"。

## 1. 判据：三种诊断各自的触发条件

- `SemanticDiag::NoMatchingFunction`：没有任何候选的**签名**收得下这组实参。
- `SemanticDiag::WhenConditionUnmet`：至少一条候选的签名收得下，但它的 `when` 对这组实参不成立，
  而**没有一条成立**。报错逐条列出 `签名 — when 条件`（`describeGenericCandidate` 的写法），
  不去重：两条臂的签名文本可以逐字相同，只有条件不同（前一条车道
  `internal/reports/when-at-the-call-site.md` 第 37–38 行已经踩过这个坑）。
- `SemanticDiag::AmbiguousCall`：**两个及以上**候选的 `when` 都成立，且没有一个比其余的"更具体"
  （4.3 的 exact 规则先跑）。

所以顺序是死的：**签名 → `when` → 数**。把 `when` 的判断放到数之后，三条诊断就都会错，而错的方向
取决于候选的个数：

- 签名收得下 + 条件为假 + **另有一个真候选** ⇒ 2 个候选 ⇒ 假的 `AmbiguousCall`（zip）。
- 签名收得下 + 条件为假 + **它自己是唯一候选** ⇒ 静默绑定一个不适用的声明（`len(Bytes!)`）。
  这一面最坏：程序照编，没有诊断，跑的是另一条声明。

## 2. 三张脸落在哪一处代码

三面都在**调用点**，不在核心：`OperatorGroup::resolve`/`resolveForCall` 从来不读 `when`（核心不认识
声明里的条件），筛选一直留在编译器的调用点（前一条车道的落地说明，同上第 37 行）。

调用点有两条路，`when` 的读法在两条路上不一致：

- `Builder::resolveGroupedCall`（`src/compile/src/gir/builder_operators.cpp`）——前一条车道修好的那条：
  逐条候选"签名收下 → 再问它自己的 `when`"，条件为假的不是候选，一个都不剩时两条报告
  （`WhenConditionUnmet`、`NoMatchingFunction`）之前都先 `yieldToPipe(gct)`。
- `Builder::chooseOperatorOverload`（同文件，本车道修的）——名字**既有一个内建/模块算子组、又有程序侧
  声明**时走这条（`zip` 正是：`stream` 模块有一条 `export func zip::<T, U>(a: T.., b: U..)`，prelude 有一条
  `#op(':struct/zip')` body-less 声明）。它数候选用的是
  `builtinAccepts = set.ops->resolve(withTypes, normTypes, statics).has_value()`——**只问签名，不问条件**。
  于是 prelude 的 `zip::<...A>(...xs: ...A) when ...is_array::<A>()` 被算成"也接得住
  `(int.., string..)`"的第二个候选。

改前逐字（`test/results` 取回，未截断）：

```
[Error]: The call to 'zip' with (int.., string..) fits more than one overload. More than one
function takes these arguments; make the choice explicit: annotate the literal with its type
(for example '2 as f32') or convert an argument with 'as'. The overloads that take them:
zip: (a: T.., b: U..) => (T, U).. (this module); the built-in or module operator <:struct/zip>:
(...xs: ...A) => (...match type A { case E[...Dims] => E; case E[...] => E; case set => KeyOf<A>; },)[]
(name=SemanticDiag::AmbiguousCall, code=0x030000F4)
```

`(int.., string..)` 是两条流，`is_array::<(int.., string..)>` 逐成员都不成立——prelude 那条根本不该出现在
"take them"里。修法就是把它在**数之前**筛掉：`builder_operators.cpp` 新增 `ruleHolds` 谓词，用
`ruleVerdictForCall` 的三答案（`Applies / ConditionUnmet / NotThisRule`）问每条规则；`builtinAccepts`、
`builtinExact`（4.3 的 exact 判定）、歧义报告里的候选清单三处共用它；只有 `Applies` 算候选，
`ConditionUnmet` 进 `unmetConditions`，写法与模板那条一致（`签名 — when 条件`）。

## 3. 第二张脸：`len(Bytes!)` 一直被静默绑到不当的声明

改完第一面，`feat/compile` 里出现一条新红：`compile.diag_render/render.runtime_chunk_range`。它的正文是

```
println('some/file.bin'->read_chunk<-1, 4>->len !! 0)
```

golden 期待的是 **`fs.read_chunk` 的运行期诊断**（负的 offset 与 size），即这个程序必须**编译通过**。
它以前能编过，是因为 `len(Bytes!)` 被静默绑到了条件为假的 `len::<A>(x: A) when is_array::<A>()`
（`A = Bytes!`，`is_array` 为假），真跑的是数组那条声明。修好第一面之后这条 call 正确地报
`WhenConditionUnmet`，于是用例从绿变红——红得对，病在别处。

## 4. 第三张脸：`->` 的传递本来就有，缺的是"报之前先让给它"

设计原则（3.9 / `docs/design-principles.cn.md`，"`->` 自动传递空值与错误"）已经写明机制：

> 先按包着的值找 `f` 的重载，**连同 `when` 条件**……匹配不上再拆开。

实现也早就在：`visitPipeLink`（`builder_composition.cpp`）先照常建一次，接收者是 `T?`/`T!` 就抛
`PipeTagged`；然后**用包着的值再试一趟**（scratch），一个重载都不收才 `peelPipeReceiver`
（`builder_control.cpp`）：按 tag 分支，失败/为空的那臂原样交下去，另一臂拿载荷再调一次，结果包回
`U!`/`U?`。所以缺的不是机制，是**谁来触发它**：`->` 的那一趟问的是"有没有重载原样收下这个 `T!`"，
它靠 `yieldToPipe` 把"没有"翻译成"让给 pipe"。

`resolveGroupedCall` 的两处报告之前都写了 `yieldToPipe(gct)`（`builder_call.cpp:1424`、`:1719`）；
`chooseOperatorOverload` 的 `WhenConditionUnmet` 报告是本车道新加的，**漏了这一步**。于是
"有没有重载原样收下 `Bytes!`"这一趟被一条硬诊断打断，拆载荷那一趟根本没走。补上
`yieldToPipe(call)` 之后（就一行）：

- `len(Bytes!)` → 这一趟筛空 → 让给 pipe → 拆开 → `len(x: Bytes)`（`prelude.cml:919`）→ `int!` → `!! -1`；
- 同一个机制对另一条路径也成立：`int[]! ->len` 拆开后仍选中 `len::<A> when is_array::<A>()`
  （条件说的就是"载荷是数组"），结果 `int!`。

两条都成了用例（`test/cases/compile/pipe_propagation/operator_payload.cml` / `operator_array_payload.cml`，
`compile.pipe_propagation` 计划，nvm/fvm 双后端同判），`render.runtime_chunk_range` 的用例与 golden
**一个字没动**地回到绿。

## 5. 读数（同树、改动前后，A 类口径）

| 读数 | 改前 | 改后 |
|---|---|---|
| `modules.stream_library`（8 条 / 4 后端） | 7 通过、`zip_takes_streams_unqualified` 红（`AmbiguousCall`） | `[SUMMARY] total: 8, pass: 8, fail: 0`，`RUN_EXIT=0` |
| `registry.docs_complete` 的问题数 | 66（含 `stream.cml:165 'zip'`） | 65，`stream.cml#zip` 那条消失 |
| `feat/compile`（109 plan / 2693 条） | 34 红（含新红 `render.runtime_chunk_range`） | 33 红，**33 条全在 630 行红表里**，`render.runtime_chunk_range` 绿 |
| `compile.pipe_propagation` | 无 `operator_payload`/`operator_array_payload` 两条 | 两条 nvm/fvm 全绿（`-1`、`3`） |

每次跑带 `check-doc-links exit 0`、`[TREE] clean`。

## 6. 没做的

- 没动核心 `OperatorGroup::resolve`/`resolveForCall`（核心不读 `when`，筛选留在调用点，是前一条车道
  定的界）；没加特例（没有"只在另有一个候选时才筛"这种写法：条件为假的声明在任何位置都不是候选）。
- 同类但**没动**的一处：`builder_generics.cpp:2109` 的 `accepted()` 也用
  `ops->resolve(...)` 回答"算子接不接这次调用"（是 `with` 参数/是否等待的判定，不是歧义计数）。
  它会不会有同样的毛病，要自己的用例才能定；本车道不顺手改。
- `stream.cml` 里 `unfold`/`iterate`/`map`/`filter`/`take`/`scan`/`tee` 那七条文档红是另一回事
  （生成器把 `f<...>` 写在调用位置，`This '<' touches 'map', so it operates on the file`），属语法，不在本页。