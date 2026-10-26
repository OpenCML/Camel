# 一臂 `void`、另一臂是 **cell 型值**：原则 190 与 (乙) 相撞；另有一条 develop 既有的建图期段错误

这条来自"捕获写入的新版本怎么带"那条车道（`handover/captured-write-tuple`）。它按纪律**没有擅自定语义**，把两处相撞的东西和一条既有崩溃一起报上来。

## 一、崩溃点与最小复现（都是主机实测）
- 断言点：`src/compile/gir/builder_control.cpp:1140` `Builder::coerceArmExit`，`ASSERT(coerced != nullptr, "A branch arm is joined only with a type its value enters.")`。实测场景：一条臂输出 `void`（`then null` 那臂），`joinType = var int[]?`（另一臂是"写捕获的调用"，按 (乙) 交出 **cell 型**的版本 `var int[]`），于是 `coerceTo(void → var int[]?)` 得到 `nullptr`；Release 下断言被 NDEBUG 编掉、空指针继续被用 ⇒ **SIGSEGV**（stdout 空、exit 为 null）。
- 最小复现（不依赖那条车道）：`/tmp/b.cml`
  ```
  func visit(i:int):void{ var x:int=i; return if i>3 then null else (x=x+1) }
  ```
  **纯赋值臂、没有任何捕获写**，在**合入树**与**该车道树**的 `out/latest` 上**同样**段错误（exit=139）。⇒ **只要分支的一臂是 cell 型值就必崩**，这是 develop 上**既有**的洞，不是这条车道引入的；那条车道只是让"写捕获的调用"也走到那里。

## 二、相撞的两处
- 设计原则 190：一臂 `void`、另一臂 `T` ⇒ 分支是 `T?`，`void` 臂给 `null`。
- (乙)：调用节点的值是"（返回值，每个被写变量的新版本……）"。
- 当 `T` 是 **cell** 时，`T?` 装不进 `null`（cell 不是值）⇒ 190 的写法在 `T` 为 cell 时**无法成立**。

## 三、我们的临时读法（请你确认或否决）
你在 (乙) 的原话里有"**用到返回值的地方接返回值的访问节点；用不到就不建**"⇒ 我们据此把版本**只在被取用的位置**物化：**没人取用的位置（本例 `void` 函数的 `return`）分支臂仍是 `void`**，因此既不与 190 相撞、也不崩。
请你定**最终形状**是下面哪一种（我们照做，不猜）：
- (a) 取不到值的位置，分支汇合**不取值、丢掉臂的值**（把 189 的"没写 `else` 的 `if` 汇合处不取值"推广开，顺带修掉上面那条既有段错误）；
- (b) 把"可空 cell"定义清楚，让 `null` 能进 `var T?`；
- (c) (乙) 收窄成"只有被取用时才交版本"（＝我们现在的临时读法，**不能**修掉既有段错误）。

## 四、另一条 develop 既有的红（同一条车道顺带撞到）
Debug tier **95/96 全红**来自 `node.h:537` `Node::check` 的断言，链条是 `isGenericHere(builder.h:1321)` → `Node::ruleBinding()` 被用在 **DECL** 上 ← `declareModule`（`program.cpp:254`）；这些文件都不在那条车道的改动范围内 ⇒ 也是既有问题，一并请你过目。（它当时是用 gdb 把 `Node::check` 首字节改成 `ret` 才越过这条、拿到上面第一条崩溃点的。）