# 经 `std::opt` 跑的程序“少优化了一次”，要不要告诉它

**状态**：等负责人回答（默认见末节：保持现状）

## 问句

`std::opt` 的流水线永远在它前面插一步 `std::defunctionalize report=false`。
流（`int..` 这类）经过 `std::defunctionalize` 时会被**拒绝兜底**：这一族原样留下、程序照常跑、结果正确，只是少优化一次。
但 `report=false` 让那条 `RuntimeDiag::DefunctionalizationIncomplete` 告警**不出现在 stderr**：

```
`src/kernel/execute/pass.cpp:2397-2403`
2397  /// Defunctionalization is the step the optimizer is written against: no call through a function
2398  /// value and no closure is left when `std::opt` starts, so specialization meets a call it can
2399  /// specialize rather than a tag it would have to guess at.
2400  constexpr FixedLoweringStep fixedLoweringSteps[] = {
2401      // The step says nothing about itself here: a program that wants the report names the pass.
2402      {"std::defunctionalize report=false", "std::opt"},
2403  };
```

**⇒ 用户看到的是：程序跑通、结果正确，却不知道“这里少优化了一次、原因是什么”。**
单独跑 `std::defunctionalize`（不带 `report=false`）才看得见那条告警。

## 读数

```
用例 `test/cases/opt/generic/interval_bounds_constant.cml`，同一棵树、同一台机：
`std::opt std::nvm`             exit 0，输出 `round 0..3 / counted 4`，stderr **无** DefunctionalizationIncomplete
`std::defunctionalize std::nvm` exit 0，同一输出，stderr **有**：
   warning[RuntimeDiag::DefunctionalizationIncomplete]: Defunctionalization left '() => (int, int..)? pure'
   DEFUNCTIONALIZE refused=1 reason='the value is read by a node that is not a call: a stream of this signature
   is carried where the pass would have to change the type that names it'
```
⇒ 程序行为**没有区别**，区别只在“要不要把它少优化了一次这件事说出来”。

## 两边的理由

**保持现状（`report=false`）**
```
· 第 2401 行的注释自己写着：“The step says nothing about itself here: a program that wants the report names the pass.”
  ⇒ 这是**有意**的：默认流水线不出告警，想要报告的人自己写 `std::defunctionalize`
· 一旦改成 `report=true`，**每个用流又跑 `std::opt` 的程序都会多一行告警**，而它并不代表错误
```

**改成 `report=true`（或加一个降噪后的提示）**
```
· “少优化一次”是性能口径的事实，用户可能正因为它慢而来查；今天他们看不到任何线索
· 但告警一旦默认打开，就会变成噪音，用户会开始忽略告警 —— 那比没有告警更糟
```

## 默认（不等回答也照这个）

**保持现状**：`std::opt` 前插的那一步继续 `report=false`。
**理由**：那条注释是既有的有意选择；而“把默认流水线的告警打开”是一件会改变所有流式程序 stderr 的事，收益（少数人排查性能）小于代价（所有人多一行噪音）。
**可被这两条推翻**：① 负责人要求默认说；② 出现“用户因为看不到原因而误判成 bug”的实际案例（届时可以只在**优化被跳过且该 pass 被显式要求**时提示，而不是默认全打）。

## 影响面

- 无测试红、无行为差异；这条纯粹是“要不要说”的口径问题。
- 相关机制留在 `std::defunctionalize` 里：流类型今天由拒绝兜底接住（保持原样、不崩），真正的“让流被去函数化”是另一件大改（call 扫描 / 改写产物 / 替换表三处同格）。
## 答复（决策者）

保持现状。`std::opt` 里少做一次去函数化不是错误，默认流水线不为它出告警；想知道原因的人写 `std::defunctionalize` 就能看到报告。
