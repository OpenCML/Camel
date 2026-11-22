# 声明函数类型那一位：判据不冗余，但它唯一能判的是它自己

**日期**：2026-11-19
**提出者**：`declaredFuncType` 删除车道（`lane-drop-declared-functype`，基线 `origin/develop` = `3217f0312`）
**状态**：等作者裁定。测量已做完；**代码未改、未提交**，工作树在 `3217f0312` 上干净。
**触发**：作者裁定 A（删字段与它的钩子），并要求先量「`declaredFuncType` 的不等是否有独立意义」。

## 1. 判据在哪里被判

`sameTypes` 全仓只有一个调用点，且只在回滚自检里：

```
src/runtime/draft_undo.cpp:136  bool sameTypes(const GraphDraft &draft, const UndoSnapshot &snapshot)
src/runtime/draft_undo.cpp:220      if (!sameTypes(draft, snapshot)) { return UndoClass::Types; }
```

`undoMismatch` 的调用者是 `rollbackTo` 的自检分支，而快照只在 `selfCheck_ != Off` 时取（`draft_undo.cpp:247-252`）。因此这一位**只在自检里被比较**，生产回滚不拿它做判据。

它进 `draft_undo.cpp` 的历史也说明这一点：

```
$ git log -S "declaredFuncType" --format='%h %s' -- src/runtime/draft_undo.cpp
9db2972af refactor: the journal has a file of its own, and the log has one undo
84f7008e6 feat: a rollback checks the draft it put back against the one the mark found
```

两笔都是回滚自检，不是读者。

## 2. 结论：不冗余——存在「只有它不等」的可达情形

（a）**生产可达**。新 draft 的 `declaredFuncType_` 是 `nullptr`（`draft.h:1241`），而 `funcType_` 在构造时就已写入声明签名（`builder.cpp:148/152`、`draft_construct.cpp:755` 等只调 `setFuncType`，不碰声明位）。签名仍 open 时，`DraftGraphBuilder::setExitType` 走**原地改型**分支：

```
src/compile/gir/draft_graph_builder.cpp:87-97
    camel::core::type::FunctionType *funcType = draft().funcType();
    draft().setDeclaredFuncType(funcType);            // declared: nullptr -> A
    if (auto *open = funcType->asOpen()) {
        open->setExitType(exitType);                  // funcType_ 指针不变，内容在指针后面改
    } else {
        draft().setFuncType(funcType->withExitType(exitType));   // 只有这一支 funcType_ 才变
    }
```

`builder_helpers.h:94-105`（`setGraphReturnFromResult`）走的正是 open 分支：函数体跑完、结果类型已知时才 `setExitType`。这一刻，`sameTypes` 比较的六项里**只有 `declaredFuncType_` 变了**：`funcType_` 指针、`closureType_`、`captureNames_`、`runtimeSlotTypes_`、`staticSlotTypes_` 全不动（`draft.h:1527-1532`）。

（b）**探针可达**。`undo_selfcheck.cpp:184-189` 的 Types 探针在「draft 已持空签名」时正是**单独写** `declaredFuncType`（`setDeclaredFuncType(old)`）再比回写。

所以它是「一段只补了 open 签名」这种情形里**唯一**能报 `Types` 的判据。⇒ **不冗余。**

## 3. 但它抓不到真正危险的那一半

上面 (a) 的 open 分支是原地改型（`src/core/type/composite/func.cpp:354`：`void OpenFunctionType::setExitType(Type *type) { exitType_ = internComponent(type); }`）。快照只存指针（`draft.h:1527-1532`），回滚也只写回指针（`draft_undo.cpp:485-493`）。于是：

- 一个 segment 在 open 签名上 `setExitType` 后回滚，**签名内容已经被改掉**，而六项比较全部相等（指针没动），自检**不会报**。
- `declaredFuncType` 的比较能抓到的只是「这一位有没有被写回」，不是「签名内容有没有被改回」。

它是自己那条记录的见证人，不是签名的见证人。

## 4. 所以 A 删掉不丢别的检查

`declaredFuncType` 今天的唯一判据就是它自己：字段一删，被见证的位与见证一起消失；`funcType` / `closureType` / `captureNames` / `runtimeSlotTypes` / `staticSlotTypes` 五项覆盖一字不减（`sameTypes` 其余四行、`UndoTypes` 与 `UndoSnapshot` 其余成员原样）。

留给 Types 探针的只有一件事：184-189 那一支改 `nullopt` 后，该情形不再有探针——按作者的收紧要求，**不新造非空签名去补**。

## 5. 与「零读者」那句话说清

作者在 `draft.h:1240` 写下「read by nobody yet」。这句在 `ddf4181c01` 当时为真；今天那三笔（`9b1fcc126`/`84f7008e6`/`9db2972af`）给这一位配了钩子、日志记录、快照字段、比较与回滚——**它不再零读者，只是那些读者全都属于回滚自检**，没有一个读者拿它决定图的行为。这正是 A 成立的原因，也是这一页要留下的区别。

## 6. 要作者回答的一句话

判据不冗余（有可达的单独不等），但它唯一能判的对象就是它自己；A 删掉的是「状态 + 自指的见证」，不是任何别的检查。若认可这一点，我按 A 执行：字段/访问器/setter/钩子调用、builder 写入与注释、`draft_undo.cpp` 那 6 处（138/190/370/488 与 `draft.h:1327/1528`）、探针支改 `nullopt`；顺带改 `2026-11-19-the-packing-slot-came-back-under-another-name.cn.md:30,35` 并记下本节这句。

## 默认建议（写死）

**默认建议**：按 §6 的 **A** 执行 —— 删 `declaredFuncType` 的字段/访问器/setter/钩子调用、`builder` 的写入
与那两处注释、`src/runtime/draft_undo.cpp` 那 6 处（138/190/370/488 与 `draft.h` 的 1327/1528），
并把 `undo_selfcheck.cpp` 的 Types 探针那一支改 `nullopt`（**不新造非空签名去补**）。
**判据**：`sameTypes` 其余五项（`funcType`/`closureType`/`captureNames`/`runtimeSlotTypes`/`staticSlotTypes`）
覆盖一字不减；`git grep declaredFuncType -- src include test` 归零；`-Plan feat/linear,feat/compile -Jobs 8`
逐条与 `3217f0312` 相同。§3 那件「open 签名原地改型、快照只存指针、自检抓不到」**单列另修**，
不在这一笔里顺手改（它要改快照的形状，不是删一个字段）。

## 决策者答（2026-11-23）：被重构取代

`declaredFuncType` 只给自检作见证。开放类型随前端重构消失（核心抽象第 17 节），develop 上还剩 3 个文件，不单独投入，随前端类型检查阶段一起删。

（无专属验收输入，见同族页。）
