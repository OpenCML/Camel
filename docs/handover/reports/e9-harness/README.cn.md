# E9 随机差分的参考实现

`e9_difftest.py` 是 CC 论文 E9 用的测试工具原样副本（run、analyze、reduce 三个子命令），放在这里供第 7 路移植成仓库自己的慢层工具，**不是可直接运行的工具**：它 `from common import *`，依赖归档仓库里的一个公共模块，用到的名字是 `SRC`（仓库根）、`CAMEL`（`camel` 可执行文件）、`PINDIR`（安装根，`$CAMEL_HOME`）、`PIN`（构建标识）、`ENV`（运行环境）、`EVAL`、`OUT`（工作与输出目录）、`run`（带超时运行子进程，返回 `rc/out/err/timeout`）、`pmap`（按并发度映射）、`write_json`、`write_jsonl`。仓库里已有的依赖是 `test/tools/progen` 的 `runner.py`、`generate.py`、`model.py`、`reduce.py`。

移植时：把这些名字换成 `test/tools/` 已有的等价物，输出写到测试的工作目录；矩阵、判定与基线见 `docs/handover/reviews/questions-2026-10-29.md`"返工（2026-11-14）：CC 评测在 `29ea0a011` 上查出的优化遍缺陷"的补充一节。移植完成后删掉本目录。
