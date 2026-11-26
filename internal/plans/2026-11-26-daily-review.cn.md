# 每日审查：11-25 18:30 至 11-26 17:30 合进 integration 的代码

审查范围是 integration（车道都先进它，develop 这一天没动）。三个只读子代理分头审了编译与内核、标准库与后端、工具与钩子；下面只列决策者逐条核实成立的，按轻重排。前七条与第十、十五、十六条是元层工具，归决策者这边修；其余交车道。

## 一、会出错的

1. **晋级不看整层运行的退出码，也不核对读数是否完整就发布**（`scripts/promote.mjs` 读 `summary.json` 处）：`readSummary` 只要求 `test_timings` 非空，层运行中途死掉、留下半份读数时，看起来没有新红，`--publish` 就会把 develop 推上去；上次红表里有、这次读数里缺的用例算"消失"而不是失败；`fail` 以外的异常状态不算红。修法：层运行以"跑完"以外的方式结束就判失败；读数的用例总数与上次晋级对账，缺的逐名列出、不许发布；非通过的状态一律按红。
2. **超时被重跑了两遍，记录还写错**：运行器自己已经把超时用例以两倍时限单独重跑一次（`run-tests.mjs` 的 `RERUN_TIMEOUT_FACTOR`），晋级脚本又以 `CAMEL_TEST_TIMEOUT_SCALE=2` 把整个计划重跑一遍，实际时限到了四倍；记录里写 `run_alone: true`，但那是整计划、四路并行跑的。修法：只留运行器那一处重跑，晋级只读它的结果，记录如实写。
3. **块的身份哈希会撞**（`src/kernel/runtime/block_form.{h,cpp}`）：区域内部的节点（分支臂、循环体）不进哈希，头文件自己写着这是"已知缺口"；递归哈希被调函数时成员 `ports_` 被清空且不恢复，外层剩下的部分按被调函数的形参位置算。两个只在区域里不同、或调用之后用了不同形参的块体会得到同一个名字，而 `BlockPrograms::add` 对同一个 URI 只留第一个，于是会静默地跑错程序。修法：哈希走遍所有区域；递归时保存并恢复 `ports_`；各加一条"两个只差在这一处的块体名字不同"的用例。
4. **推送钩子只在远端名叫 `origin` 时才查过程痕迹**（`.husky/pre-push`）：按地址推（决策者就是这样推 fsh-dev）或远端起了别的名字时，这一查整段跳过；而 `scripts/hooks/pre-push` 是按项目远端的地址认的，同一个钩子的两半认法不一。修法：两半都按项目远端的地址认。
5. **令牌池的 `take N` 边等边攥着已拿到的令牌**（`internal/tools/host-tokens.sh`）：两个各要 13 个的调用在 24 个的池子里各攥住 12 个互相等，三十分钟后都以"没令牌"退出。修法：拿不够就把已拿的全还回去、退避再试（或者只用 `take-up-to`），自测加这一种。
6. **长任务的子进程没有时限，按进程号杀进程时身份可能是错的**（`scripts/lib/job.mjs`）：`Children.run` 不设时限，子进程卡死时工作进程的心跳照样在跳，锁永远不放；`isSameProcess` 在启动时间没记下或读不到时直接答"是同一个"，可能整棵杀掉一个被复用了进程号的无关进程。`scripts/merge-batch.mjs` 的构建也是前台 `spawnSync`、没有时限。修法：每类子进程都有时限，超时整棵杀并判失败；启动时间不明就不按进程号杀，报出来；合并工具的构建加时限。
7. **钩子检查的探针时限是摆设**（`internal/tools/push-hooks-guard.ps1`）：先 `ReadToEnd` 读完输出才 `WaitForExit`，钩子卡住就永远卡住，超时分支到不了；只看到痕迹检查器的输出就判通过，`scripts/hooks/pre-push` 没被调用也照样通过。修法：异步读输出、按时限等，超时整棵杀；判据改成两半都应答。

## 二、第二份真相与按名字的特例

8. **算子记录上的 `mayNotTerminate` 是没人读的第二份**（`operator_record.h`）：真正被读的是 `OperatorTraits::mayNotTerminate`，`facts.cpp` 的注释却说读的是记录上的。删掉记录上那一份，注释改对。
9. **性质位的打包与 FNV 哈希写了两份，已经走样**：`block_form.cpp` 与 `fact_persist.cpp` 各打一遍，前者少了 `callsGraphs`、`readsGraphs` 两位。收成一个函数两处都调。
10. **构建队列的等待项有两套剪枝**：`camel-waiter.ps1`（按命名互斥量判活）只有它自己的测试在调，生产里跑的是 `camel-gate-state.ps1` 的 `Select-LiveCamelWaiters`。留在用的那套，另一套连同测试删掉。
11. **`conv2d` 梯度有两套算子，新的一套在步长不整除时形状算错**：`*_shaped` 版本从输出反推输入尺寸，同一个公式在 `nn/tensor_ops.cpp`（类型推导）与 `tensor/kernels/conv.cpp`（内核）各写一遍；正向卷积按向下取整算输出，步长不整除时反推出的尺寸比真实的小。`conv2d_bias_grad_shaped` 与原版完全相同。修法：库里的律通过"转置要读的值有端口"拿到输入的形状，用原版算子；`*_shaped` 三个删掉。在读值端口落地之前先不删，但加一条步长不整除的用例，看它现在是不是错的。
12. **新代码里还有按名字认算子**：ONNX 导出用字符串比 `:struct/append_arr`（`exporter.cpp` 新增那段），转置用 URI 字符串造 `:op/enter`（`walkmachinery.cpp`）。这两个都是编译器自己造的算子，按编号认、按编号造。
13. **`:op/fill`、`:op/access`、`:op/access_key` 改成不会故障，没有测试钉住**：加用例钉住这三类节点可以自由安排；连同"位置不是常量的图在提交时被拒"那一条一起。
14. **C++ 后端不设调用的算子编号**：`setCallOperator` 在 `nvm`、`fvm`、宏求值、并行后端都设了，`std::cpp` 生成的代码没有，块在那里会拿到空编号。要么设上，要么在 C++ 后端明确拒绝块算子并说明原因。

## 三、检查器与过程痕迹

15. **过程痕迹检查有几处漏**：日期的正则写死了 2025 与 2026 两年，明年一月起所有日期都漏；裸提交哈希（不带反引号的）不认；闸门第 4 道不带基线跑、必然红，钩子带基线跑是绿的，两处判法不一。修法：年份写成任意年；钩子与闸门用同一种判法（只判新增的行），基线文件按原定删掉。
16. **代码与测试里还有痕迹**：`src/kernel/facts/facts.cpp` 引了一个带日期的内部问题页；`src/compile/src/gir/builder_generics.cpp` 有一段事故经过；`test/cases/transforms/transpose/cotangent_enters_the_array_port.cml` 写着"作者的某某裁定"；`tensor/kernels/conv.cpp` 引了改名前已不存在的 `conv2dInputGradStated`。
17. **计划说明里钉死了检查条数**：`selfchecks.plan.toml` 里 18 处 `desc` 写着"（N checks）"，`gates.mjs` 写着"十道"。条数改了这些说明就过时，删掉条数。
