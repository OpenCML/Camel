# 遍流水线的来源名：实现今天写 `<pipeline>`，裁定写 `(pipeline)`

## 是什么

命令行上入口文件之后那串遍词是一段合成的源文本（`docs/handover/reviews/questions-2026-10-24-evening.md`
§`pass-name-position-synthetic`）。裁定把它的**来源名**写作 `(pipeline)`，而编译器的文本输出与 JSON
今天都写 `<pipeline>`。差的只是一对括号，但它是用户可见、且被用例与文档钉住的字符串，所以不自行择一。

## 逐字证据

- 文本形式（实测，`unknown_pass_stage`）：`--> <pipeline>:1:30`
- JSON 形式（同一诊断）：`"modulePath": "<pipeline>"`、`"moduleName": "pipeline"`
- 裁定逐字：来源名叫 `(pipeline)`；文本格式 `(pipeline):1:<列>`；JSON 的来源**同样写** `(pipeline)`

## 今天的读数

库里凡是钉到来源名的地方都是 `<pipeline>`：`src/execute/pass.cpp` 的来源名字符串、两条流水线位置用例、
以及本轮按裁定新写下的 `docs/cli.md` JSON 诊断一节（那两条用例的文本形式已按裁定把 `line 0` 改成 `1`，
列 30/26 不动；JSON 侧已给出真实范围 `0:0–0:17`，来源仍写 `<pipeline>`）。

## 选项与代价

1. **保持 `<pipeline>`**：一个字都不改，`docs/cli.md` 与实现一致；与裁定字面相差一对括号，
   下一位读到裁定的人还会再问一次。
2. **改成 `(pipeline)`**：与裁定字面一致；要改来源名字符串与若干钉值，下游（日志解析、IDE 提示、
   任何按 `<...>` 认"合成来源"的代码）要一起看一遍。
3. **两种都接受，只把通则写清**（例如"合成来源用尖括号 `<...>`，不用圆括号"）：需要在
   `docs/cli.md` 补一条通则，否则同一个问题会以别的形式再来（例如以后出现别的合成来源）。

## 卡住什么

只卡"文档与实现对不上"这一处；其余工作不等它。