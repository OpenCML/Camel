# 回滚要还原的是指针，还是内容？

**日期**：2026-11-19
**提出者**：`declaredFuncType` 删除车道（`lane-drop-declared-functype`，基线 `origin/develop` = `3217f0312`）
**状态**：等作者裁定。**默认建议：还原内容。** 本页是自检的一个洞，与「删掉声明函数类型那一位」是两件事，分属两笔提交。
**触发**：删 `declaredFuncType` 时量出来：那一位是 open 签名原地改型在自检里唯一的痕迹；删掉它之后，原地改型就完全看不见了。

## 1. 事实：签名是原地改的

`DraftGraphBuilder::setExitType`（`src/compile/gir/draft_graph_builder.cpp`）在签名还是 open 时：

```
    camel::core::type::FunctionType *funcType = draft().funcType();
    if (auto *open = funcType->asOpen()) {
        open->setExitType(exitType);              // 改的是指针指向的那个对象
    } else {
        draft().setFuncType(funcType->withExitType(exitType));   // 只有这一支换指针
    }
```

`OpenFunctionType::setExitType` 就是一次赋值（`src/core/type/composite/func.cpp:354`：`exitType_ = internComponent(type);`）。调用者是 `builder_helpers.h:94-105`（`setGraphReturnFromResult`）：函数体跑完、结果类型已知时才补上 exit——这是生产路径，不是角落。

## 2. 快照与回滚只存指针、只写指针

```
include/camel/runtime/draft.h（UndoSnapshot，Types 类）
    camel::core::type::FunctionType *funcType    = nullptr;
    camel::core::type::TupleType *closureType    = nullptr;
```

`takeUndoSnapshot`（`draft_undo.cpp`）拷的是这些指针；`replayJournal` 写回的也是这些指针。`sameTypes` 比的也全是指针。

## 3. 于是自检有一个看不见的情形

一个 segment 在 open 签名上 `setExitType`，然后回滚：

- 签名**内容**（exit 类型）已经改了，回滚没有、也无法把它改回去；
- 六项比较（删 `declaredFuncType` 后五项）全部相等——`funcType_` 的指针根本没动；
- `sameTypes` 返回相等，`undoMismatch` 返回空，自检报 `ok`。

删掉 `declaredFuncType` 之前，那一位在「首次 `setExitType`」时从 `nullptr` 变成 `funcType_`，自检至少还能看见「这一位没写回」；删掉之后，连这条痕迹也没有了。**注意：它当年能看见的也只是「位有没有写回」，从来不是「内容有没有改回」。**

## 4. 两难

- **还原内容**：`UndoTypes` 记的不该是 `FunctionType *`，而该是让签名能回到原样的东西——open 签名的 exit（以及 `setResources` 写的资源签名，同样是原地写）。回滚时写回它。代价：每个 segment 多存一个 `Type *`（或若干），回滚多一次写入；好处：原地改型才真的可回滚，自检也才看得见它。
- **还原指针**（现状）：只保证「换了一个签名」能回滚；「同一个签名被改了」不能。要守住现状，就只能**禁止原地改型**——`setExitType` 一律 `withExitType` 换新指针——但那会改掉 `OpenFunctionType` 的共享语义（`func.h:24`：`setExitType()` 只存在于 `OpenFunctionType`，构造时不可改），不是一处小改。

## 5. 默认建议（本页写死，作者可推翻）

**还原内容。** 理由：回滚的契约是「draft 回到 mark 找到的样子」，签名内容也是 draft 的样子；只还原指针的回滚在 open 签名上不成立，而自检把这种不成立报成 `ok`，比不报更糟。

## 6. 要作者回答的一句

`UndoTypes` 要不要从「指针」改成「能让签名回到原样的内容」（至少 open 签名的 exit，`setResources` 的资源签名同理）？如果要，这一页的改动**不在**删 `declaredFuncType` 那一笔里做。

## 决策者答（2026-11-23）：被重构取代

类型成为不可变的值之后（核心抽象第 5、17 节），"还原指针"就等于"还原内容"。重构总方案第 5 节规定旧代码只修用户可见的错，这一条不在旧代码上改。

带进 阶段 3b 编辑会话与事实 的验收输入：编辑会话撤销后签名必须回到保存点的样子，作为会话不变式用例
